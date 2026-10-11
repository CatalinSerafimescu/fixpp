// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_cancelled_outbound_store_measurement.cpp
//
// A measurement of the code as it stands, and not a cell of a design
// (.specify/536-537-538-recovery-liveness.md §8, "Noticed and left alone", the bullet on
// `Session::store_then_emit`): what happens to an application message whose outbound
// store() is cancelled after its sequence number has been assigned.
//
// The cancellation is the application's own: it cancels its co_await of Engine::send.
// Engine::stop does not cancel a send. The fixture is held_file_store_scene.hpp.
//
// Session::store_then_emit has two dispositions for a cancelled store, and which one a
// send takes depends on where the cancellation lands (specs/059-outbound-store-fail-
// closed/contracts/store-then-emit-disposition.md, items 2 and 5; research.md D5, D7):
//
//   Returned   the store call is waiting for the store's lock, behind another store
//              call of the session, when the cancellation arrives. The store returns
//              store_cancelled.
//   Thrown     the cancellation arrives while the store call stands at a suspension
//              that precedes its lock (FileStore::store's leading post), and the thread
//              accepts it. The store call's next co_await, of the lock, then throws
//              operation_aborted before the lock is asked for.
//
// One cell per route. Each asserts its route from the wrapper's record of the store
// call, and fails as "the path was not exercised" when that record is not the
// route's. It then asserts what a peer can observe: whether the frame reached the
// transport, and what a ResendRequest for its sequence number draws.
//
// Control_NotCancelled_FrameStoredAndReplayed is the Returned cell's setup with no
// cancellation: the queued send completes when the hold is released. Each observation
// the two route cells assert takes its other value there.
//
// The gates, with no sleep:
//   Returned   one Engine::send is held inside its store(); the send under test then
//              enters store(), and the test runs the ready handlers until none is, so
//              the call stands queued at the lock; then the test emits the cancellation.
//   Thrown     the wrapper's call hook, which runs in the call expression of store(),
//              posts the emit to the session's strand. The post made there is queued
//              ahead of the one FileStore::store makes when its body starts, so the emit
//              runs after the store call has suspended at its leading post and before
//              that post completes.
//
// Every arm asserts nothing while its engine runs, and stops the engine on every path.

#include <gtest/gtest.h>

#include <asio/awaitable.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/co_spawn.hpp>
#include <asio/post.hpp>
#include <asio/use_future.hpp>
#include <cstddef>
#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <future>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "session/held_file_store_scene.hpp"
#include "session/plain_engine_rig.hpp"
#include "support/recording_store.hpp"

namespace {

namespace plain_rig = fixpp::test_support::plain_rig;
namespace store_hold = fixpp::test_support::store_hold;
using fixpp::core::error;
using fixpp::session::direction_t;
using fixpp::session::fsm_state;
using fixpp::session::seqnum_t;
using fixpp::test_support::HeldFileStoreScene;
using fixpp::test_support::store_op;
using fixpp::test_support::StoreCall;

enum class Route : std::uint8_t { returned, thrown, not_cancelled };

// What the session's store holds for one outbound sequence number, read from the
// store itself.
struct StoreSlot {
    bool read = false;
    std::optional<error> retrieve_error;  // nullopt: retrieve returned a value
    std::size_t frames = 0;               // frames retrieve visited
    std::optional<seqnum_t> next_outbound;
};

class CountingVisitor final : public fixpp::session::retrieve_visitor {
public:
    std::size_t frames = 0;
    asio::awaitable<fixpp::core::expected_t<fixpp::session::visit_result>> on_frame(
        seqnum_t /*seq*/, std::span<const std::byte> /*frame*/) noexcept override {
        ++frames;
        co_return fixpp::session::visit_result::cont;
    }
};

struct Outcome {
    std::string setup_miss;
    unsigned seq = 0;              // MsgSeqNum(34) of the send under test
    bool hold_entered = false;     // Returned route only
    bool held_frame_read = false;  // Returned route: the peer read the held send, once released
    // The send's store() call, as the wrapper recorded it.
    std::optional<StoreCall> store_call;
    bool store_queued = false;     // Returned route: entered, not returned, hold in flight
    std::size_t cancel_mark = 0;   // log position taken immediately before the emit
    std::size_t release_mark = 0;  // Returned route: immediately before the release
    bool hold_timed_out = false;
    std::optional<fixpp::core::expected_t<void>> send_result;  // Engine::send's result
    fsm_state state_after_send = fsm_state::NotConnected;
    StoreSlot slot;
    bool original_read_before_rr = false;  // the peer read the send's own frame before asking
    bool resend_requested = false;         // the session was Active, so the peer asked
    std::vector<std::string> after_rr;     // what the peer read after its ResendRequest
    std::vector<std::string> all_app;      // every 35=D frame the peer read, to end-of-stream
    bool peer_read_ended = false;
    bool stop_completed = false;
};

Outcome run_route(Route const route) {
    Outcome o;
    HeldFileStoreScene scene;
    auto& rig = scene.rig;
    asio::cancellation_signal cancel;
    [&] {
        if (!scene.setup(1)) return;

        const bool held = route != Route::thrown;
        std::future<fixpp::core::expected_t<void>> held_fut;
        o.seq = scene.last + 1;
        if (held) {
            store_hold::arm();
            held_fut = scene.send_app();
            if (!rig.run_until([] { return store_hold::holding(); })) {
                scene.setup_miss = "the offload hook did not enter its hold";
                return;
            }
            o.hold_entered = true;
            o.seq = scene.last + 2;  // the held send has the number after setup's
        }

        auto const send_mark = scene.log->mark();
        auto store_call = [&] {
            return scene.log->first_after(send_mark, [&](StoreCall const& call) {
                return call.op == store_op::store && call.dir == direction_t::outbound &&
                       call.first == o.seq;
            });
        };
        if (route == Route::thrown) {
            auto const session = rig.session();
            if (!session) {
                scene.setup_miss = "the session is not published";
                return;
            }
            scene.log->on_call = [&, strand = session->executor().underlying(),
                                  posted = false](StoreCall const& call) mutable {
                if (posted || call.op != store_op::store || call.dir != direction_t::outbound ||
                    call.first != o.seq) {
                    return;
                }
                posted = true;
                asio::post(strand, [&] {
                    o.cancel_mark = scene.log->mark();
                    cancel.emit(asio::cancellation_type::terminal);
                });
            };
        }

        auto send_fut =
            scene.send_app(asio::bind_cancellation_slot(cancel.slot(), asio::use_future));
        if (held) {
            // The gate: the entry record, then the ready handlers until none is, which
            // takes the call past FileStore::store's leading post to the lock.
            if (rig.run_until([&] { return store_call() != nullptr && store_call()->entered(); })) {
                rig.ioc.poll();
                rig.ioc.restart();
                o.store_queued = !store_call()->returned() && store_hold::holding();
            }
            if (route == Route::returned) {
                o.cancel_mark = scene.log->mark();
                cancel.emit(asio::cancellation_type::terminal);
            } else {
                o.release_mark = scene.log->mark();
                store_hold::release();
            }
        }
        if (rig.run_until([&] { return HeldFileStoreScene::ready(send_fut); })) {
            o.send_result = send_fut.get();
        }
        o.state_after_send = rig.state();
        if (auto const* c = store_call(); c != nullptr) o.store_call = *c;
        scene.log->on_call = nullptr;

        if (held) {
            if (route == Route::returned) {
                o.release_mark = scene.log->mark();
                store_hold::release();
            }
            (void)rig.run_until([&] { return HeldFileStoreScene::ready(held_fut); });
            // The held send's own frame is read before the ResendRequest goes out, so
            // it is not counted among the frames that follow the request.
            auto const held_seq = std::to_string(scene.last + 1);
            o.held_frame_read = rig.run_until([&] {
                for (auto const& f : plain_rig::frames_of_type(rig.peer.received, "D")) {
                    if (plain_rig::field_value(f, "34") == held_seq) return true;
                }
                return false;
            });
        }

        // What the store holds for the number, read from the store.
        if (auto* const store = scene.log->store; store != nullptr) {
            auto read_fut = asio::co_spawn(
                rig.ioc,
                [store, seq = o.seq]() -> asio::awaitable<StoreSlot> {
                    StoreSlot s;
                    CountingVisitor visitor;
                    auto const r =
                        co_await store->retrieve(seq, seq, direction_t::outbound, visitor);
                    if (!r) s.retrieve_error = r.error();
                    s.frames = visitor.frames;
                    auto const next = co_await store->next_seqnum(direction_t::outbound, false);
                    if (next) s.next_outbound = *next;
                    s.read = true;
                    co_return s;
                },
                asio::use_future);
            if (rig.run_until([&] { return HeldFileStoreScene::ready(read_fut); })) {
                o.slot = read_fut.get();
            }
        }

        // What a ResendRequest for the number draws, where the session can still answer.
        if (o.state_after_send == fsm_state::Active) {
            o.resend_requested = true;
            // A frame the send did transmit is read before the ResendRequest goes out,
            // for the same reason as the held send's.
            if (o.send_result.has_value() && o.send_result->has_value()) {
                auto const seq = std::to_string(o.seq);
                o.original_read_before_rr = rig.run_until([&] {
                    for (auto const& f : plain_rig::frames_of_type(rig.peer.received, "D")) {
                        if (plain_rig::field_value(f, "34") == seq) return true;
                    }
                    return false;
                });
            }
            auto const rx_mark = rig.peer.received.size();
            scene.peer_resend_request(2, o.seq, o.seq);
            (void)rig.run_until([&] { return !scene.peer_frames_from(rx_mark).empty(); });
            // The stop, then the peer's end-of-stream: every byte the session wrote has
            // been read by then.
            scene.finish();
            o.peer_read_ended = rig.run_until([&] { return rig.peer.read_ended; });
            o.after_rr = scene.peer_frames_from(rx_mark);
        } else {
            scene.finish();
            o.peer_read_ended = rig.run_until([&] { return rig.peer.read_ended; });
        }
        o.all_app = plain_rig::frames_of_type(rig.peer.received, "D");
    }();
    if (scene.log) scene.log->on_call = nullptr;
    scene.finish();
    o.setup_miss = scene.setup_miss;
    o.hold_timed_out = store_hold::timed_out();
    o.stop_completed = scene.stop_completed;
    return o;
}

// The peer read an application message with this MsgSeqNum(34), first transmission.
bool peer_read_original(Outcome const& o) {
    for (auto const& f : o.all_app) {
        if (plain_rig::field_value(f, "34") == std::to_string(o.seq) &&
            plain_rig::field_value(f, "43") != "Y") {
            return true;
        }
    }
    return false;
}

std::string error_text(std::optional<error> const& e) {
    return e ? std::to_string(static_cast<int>(*e)) : std::string{"none"};
}

TEST(CancelledOutboundStoreMeasurement, Returned_StoreCancelledAtItsLock) {
    auto const o = run_route(Route::returned);
    ASSERT_TRUE(o.setup_miss.empty()) << o.setup_miss;

    // The route: the store was entered and stood queued behind the held store() before
    // the emit, and returned store_cancelled after it, while the hold was in flight.
    ASSERT_TRUE(o.hold_entered && o.store_call.has_value() && o.store_queued && !o.hold_timed_out)
        << "the path was not exercised: hold_entered=" << o.hold_entered
        << " store_call_recorded=" << o.store_call.has_value() << " store_queued=" << o.store_queued
        << " hold_timed_out=" << o.hold_timed_out;
    ASSERT_TRUE(o.held_frame_read) << "the peer did not read the held send after its release";
    auto const& call = *o.store_call;
    ASSERT_TRUE(call.entered_at < o.cancel_mark && call.returned() &&
                call.returned_at > o.cancel_mark && call.returned_at < o.release_mark &&
                call.error == error::store_cancelled)
        << "the path was not exercised: store() must return store_cancelled after the emit and "
           "before the hold is released. entered_at="
        << call.entered_at << " cancel_mark=" << o.cancel_mark
        << " returned_at=" << call.returned_at << " release_mark=" << o.release_mark
        << " error=" << error_text(call.error);
    EXPECT_TRUE(o.stop_completed);
    EXPECT_TRUE(o.peer_read_ended);

    // Today: the send succeeds and the session stays Active.
    ASSERT_TRUE(o.send_result.has_value()) << "Engine::send did not complete";
    EXPECT_TRUE(o.send_result->has_value()) << "error " << static_cast<int>(o.send_result->error());
    EXPECT_EQ(o.state_after_send, fsm_state::Active);

    // (a) The frame reached the transport.
    EXPECT_TRUE(o.original_read_before_rr && peer_read_original(o))
        << "the peer did not read 34=" << o.seq << ":\n"
        << store_hold::printable(o.all_app);

    // The store does not hold it, and its outbound counter stands at that number.
    ASSERT_TRUE(o.slot.read) << "the store was not read";
    EXPECT_EQ(o.slot.retrieve_error, std::optional<error>{error::store_seqnum_gap});
    EXPECT_EQ(o.slot.frames, 0U);
    EXPECT_EQ(o.slot.next_outbound, std::optional<seqnum_t>{o.seq});

    // (b) A ResendRequest for the number draws one GapFill over it, and no replay.
    ASSERT_TRUE(o.resend_requested);
    ASSERT_EQ(o.after_rr.size(), 1U) << store_hold::printable(o.after_rr);
    auto const& answer = o.after_rr.front();
    EXPECT_EQ(plain_rig::field_value(answer, "35"), "4") << store_hold::printable(o.after_rr);
    EXPECT_EQ(plain_rig::field_value(answer, "123"), "Y") << store_hold::printable(o.after_rr);
    EXPECT_EQ(plain_rig::field_value(answer, "34"), std::to_string(o.seq));
    EXPECT_EQ(plain_rig::field_value(answer, "36"), std::to_string(o.seq + 1));
}

TEST(CancelledOutboundStoreMeasurement, Thrown_CancelledAtTheStoreCallsFirstSuspension) {
    auto const o = run_route(Route::thrown);
    ASSERT_TRUE(o.setup_miss.empty()) << o.setup_miss;

    // The route: the body of store() started, the emit ran after that, and the store
    // call never returned a result: it ended by an exception.
    ASSERT_TRUE(o.store_call.has_value() && o.store_call->entered() &&
                o.cancel_mark > o.store_call->entered_at && !o.store_call->returned())
        << "the path was not exercised: store_call_recorded=" << o.store_call.has_value()
        << " body_entered=" << (o.store_call.has_value() && o.store_call->entered())
        << " entered_at=" << (o.store_call.has_value() ? o.store_call->entered_at : 0)
        << " cancel_mark=" << o.cancel_mark
        << " returned=" << (o.store_call.has_value() && o.store_call->returned());
    EXPECT_TRUE(o.stop_completed);
    EXPECT_TRUE(o.peer_read_ended);

    // Today: the send fails with dispatch_aborted and the session is Disconnected.
    ASSERT_TRUE(o.send_result.has_value()) << "Engine::send did not complete";
    ASSERT_FALSE(o.send_result->has_value());
    EXPECT_EQ(o.send_result->error(), error::dispatch_aborted);
    EXPECT_EQ(o.state_after_send, fsm_state::Disconnected);

    // (a) The frame did not reach the transport.
    EXPECT_FALSE(peer_read_original(o)) << "the peer read 34=" << o.seq << ":\n"
                                        << store_hold::printable(o.all_app);

    // The store does not hold it, and its outbound counter stands at that number.
    ASSERT_TRUE(o.slot.read) << "the store was not read";
    EXPECT_EQ(o.slot.retrieve_error, std::optional<error>{error::store_seqnum_gap});
    EXPECT_EQ(o.slot.frames, 0U);
    EXPECT_EQ(o.slot.next_outbound, std::optional<seqnum_t>{o.seq});

    // (b) Not asked on this connection: the session is no longer Active, so it
    // answers no ResendRequest here.
    EXPECT_FALSE(o.resend_requested);
}

// The Returned cell's setup, and no cancellation.
TEST(CancelledOutboundStoreMeasurement, Control_NotCancelled_FrameStoredAndReplayed) {
    auto const o = run_route(Route::not_cancelled);
    ASSERT_TRUE(o.setup_miss.empty()) << o.setup_miss;

    ASSERT_TRUE(o.hold_entered && o.store_call.has_value() && o.store_queued && !o.hold_timed_out)
        << "the path was not exercised: hold_entered=" << o.hold_entered
        << " store_call_recorded=" << o.store_call.has_value() << " store_queued=" << o.store_queued
        << " hold_timed_out=" << o.hold_timed_out;
    ASSERT_TRUE(o.held_frame_read) << "the peer did not read the held send after its release";
    auto const& call = *o.store_call;
    EXPECT_TRUE(call.returned() && call.returned_at > o.release_mark && !call.error)
        << "the queued store() must return a value once the hold is released. returned_at="
        << call.returned_at << " release_mark=" << o.release_mark
        << " error=" << error_text(call.error);
    EXPECT_EQ(o.cancel_mark, 0U);
    EXPECT_TRUE(o.stop_completed);
    EXPECT_TRUE(o.peer_read_ended);

    ASSERT_TRUE(o.send_result.has_value()) << "Engine::send did not complete";
    EXPECT_TRUE(o.send_result->has_value());
    EXPECT_EQ(o.state_after_send, fsm_state::Active);
    EXPECT_TRUE(o.original_read_before_rr && peer_read_original(o))
        << store_hold::printable(o.all_app);

    // The store holds the frame, and its outbound counter has passed the number.
    ASSERT_TRUE(o.slot.read) << "the store was not read";
    EXPECT_EQ(o.slot.retrieve_error, std::optional<error>{});
    EXPECT_EQ(o.slot.frames, 1U);
    EXPECT_EQ(o.slot.next_outbound, std::optional<seqnum_t>{o.seq + 1});

    // A ResendRequest for the number draws the frame, replayed.
    ASSERT_TRUE(o.resend_requested);
    ASSERT_EQ(o.after_rr.size(), 1U) << store_hold::printable(o.after_rr);
    auto const& answer = o.after_rr.front();
    EXPECT_EQ(plain_rig::field_value(answer, "35"), "D") << store_hold::printable(o.after_rr);
    EXPECT_EQ(plain_rig::field_value(answer, "43"), "Y") << store_hold::printable(o.after_rr);
    EXPECT_EQ(plain_rig::field_value(answer, "34"), std::to_string(o.seq));
}

}  // namespace
