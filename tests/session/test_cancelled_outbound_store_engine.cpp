// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_cancelled_outbound_store_engine.cpp
//
// Rulings R-19 and R-23 (.specify/536-537-538-recovery-liveness.md §10, D-OUT-3 and
// D-OUT-10; §5, the rows "a second send in flight", "an inbound arm in flight" and
// "059's reconcile over a number assigned after the take-back"): a frame whose outbound
// store() is cancelled while another frame of the same session is already under way.
// Each cell runs an acceptor session under an Engine on the plain transport
// (held_file_store_scene.hpp), facing a raw peer.
//
// The suites:
//   SecondFrameInFlight          frame k's Engine::send is cancelled at its store
//                                call's leading post while a second frame is under
//                                way: a second Engine::send, or an inbound arm that
//                                answers with a Reject and then a Logout.
//   ReconcileAfterTheTakeBack    on a FileStore, frame k is cancelled at the store's
//                                lock with a second send and an inbound arm's persist
//                                queued behind it, in both orders of arrival at the
//                                lock.
//
// A cell asserts nothing while its engine runs, and stops the engine on every path.

#include <gtest/gtest.h>

#include <algorithm>
#include <asio/awaitable.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/co_spawn.hpp>
#include <asio/post.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/memory_store_factory.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "session/held_file_store_scene.hpp"
#include "session/plain_engine_rig.hpp"
#include "support/recording_store.hpp"
#include "support/session_test_access.hpp"

namespace {

namespace plain_rig = fixpp::test_support::plain_rig;
namespace store_hold = fixpp::test_support::store_hold;
using fixpp::core::error;
using fixpp::session::direction_t;
using fixpp::session::fsm_state;
using fixpp::session::seqnum_t;
using fixpp::session::session_test_access;
using fixpp::test_support::HeldFileStoreScene;
using fixpp::test_support::store_op;
using fixpp::test_support::StoreCall;

struct CountingApp final : fixpp::session::Application {
    int to_app = 0;
    int logouts = 0;
    std::function<void(int)> on_to_app;
    fixpp::core::expected_t<void> toApp(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
        const fixpp::session::SessionId& /*id*/) override {
        ++to_app;
        if (on_to_app) on_to_app(to_app);
        return {};
    }
    void onLogout(const fixpp::session::SessionId& /*id*/) override { ++logouts; }
};

// The MsgType(35) of each frame retrieve visits for one outbound number.
class TypeVisitor final : public fixpp::session::retrieve_visitor {
public:
    std::string types;
    asio::awaitable<fixpp::core::expected_t<fixpp::session::visit_result>> on_frame(
        seqnum_t /*seq*/, std::span<const std::byte> frame) noexcept override {
        std::string_view const text{reinterpret_cast<char const*>(frame.data()), frame.size()};
        types += (types.empty() ? "" : "+") + plain_rig::field_value(text, "35");
        co_return fixpp::session::visit_result::cont;
    }
};

// What the store holds outbound, read from the store itself: the MsgType of the frame
// under each number of [lo, hi] that has one, and the store's next outbound number.
struct StoreContent {
    bool read = false;
    std::map<seqnum_t, std::string> types;
    std::optional<seqnum_t> next_outbound;

    [[nodiscard]] std::string type_at(seqnum_t seq) const {
        auto const it = types.find(seq);
        return it == types.end() ? std::string{} : it->second;
    }
    [[nodiscard]] std::string text() const {
        std::string t;
        for (auto const& [seq, type] : types) t += std::to_string(seq) + ":35=" + type + " ";
        return t + "next_outbound=" + (next_outbound ? std::to_string(*next_outbound) : "?");
    }
};

asio::awaitable<StoreContent> read_store(fixpp::session::MessageStore* store, seqnum_t lo,
                                         seqnum_t hi) {
    StoreContent out;
    for (seqnum_t n = lo; n <= hi; ++n) {
        TypeVisitor visitor;
        auto const r = co_await store->retrieve(n, n, direction_t::outbound, visitor);
        if (r && !visitor.types.empty()) out.types[n] = visitor.types;
    }
    auto const next = co_await store->next_seqnum(direction_t::outbound, false);
    if (next) out.next_outbound = *next;
    out.read = true;
    co_return out;
}

// One frame the peer read: its MsgType(35) and MsgSeqNum(34).
struct WireFrame {
    std::string type;
    unsigned seq = 0;
    bool operator==(WireFrame const&) const = default;
};

// For a failure message: gtest prints a WireFrame through this.
void PrintTo(WireFrame const& f, std::ostream* os) { *os << "35=" << f.type << "/34=" << f.seq; }

std::vector<WireFrame> wire_frames(std::vector<std::string> const& frames) {
    std::vector<WireFrame> out;
    for (auto const& f : frames) {
        out.push_back({plain_rig::field_value(f, "35"),
                       static_cast<unsigned>(std::stoul(plain_rig::field_value(f, "34")))});
    }
    return out;
}

std::string wire_text(std::vector<WireFrame> const& wire) {
    std::string t;
    for (auto const& f : wire) t += "35=" + f.type + "/34=" + std::to_string(f.seq) + " ";
    return t.empty() ? std::string{"(no frame)"} : t;
}

std::string result_text(std::optional<fixpp::core::expected_t<void>> const& r) {
    if (!r) return "did not complete";
    return r->has_value() ? std::string{"success"}
                          : "error " + std::to_string(static_cast<int>(r->error()));
}

bool failed_with(std::optional<fixpp::core::expected_t<void>> const& r, error const e) {
    return r.has_value() && !r->has_value() && r->error() == e;
}

// What a coroutine of the cell's that hands a frame to on_inbound_frame recorded.
struct Inbound {
    bool started = false;
    bool completed = false;
    bool threw = false;
    std::optional<error> returned_error;

    [[nodiscard]] bool returned_success() const { return completed && !threw && !returned_error; }
    [[nodiscard]] std::string text() const {
        if (!completed) return "did not complete";
        if (threw) return "threw";
        return returned_error
                   ? "returned error " + std::to_string(static_cast<int>(*returned_error))
                   : std::string{"returned success"};
    }
};

asio::awaitable<void> deliver(fixpp::session::Session& session, std::span<const std::byte> bytes,
                              Inbound& o) {
    o.started = true;
    try {
        auto r = co_await session.on_inbound_frame(bytes);
        if (!r) o.returned_error = r.error();
    } catch (...) {
        o.threw = true;
    }
    o.completed = true;
}

// ── D-OUT-3, D-OUT-10: a second frame in flight ──────────────────────────────────────
//
// One application message is sent in setup; the session's next outbound number is then
// k. Frame k is an Engine::send bound to a cancellation signal of the cell's.
//
// The gate, with no sleep and no store hook. Frame k's toApp callback runs in the
// handler that then assigns k and calls store(), which suspends at the store's leading
// post. From inside that callback the cell posts two handlers to the session's executor,
// in this order: one that starts the second frame, and one that emits a terminal
// cancellation on frame k's signal. Both are queued ahead of the completion of the
// store's leading post, so when the emit runs the second frame has been started and
// frame k still stands at its leading post. Two Engine::send calls spawned from outside
// the session do not overlap on one I/O thread: the first store call's leading post
// completes before the second send is assigned.
//
// The second frame:
//   a second Engine::send;
//   an inbound arm: a Heartbeat with a SendingTime(52) outside the threshold, handed to
//   Session::on_inbound_frame, whose arm answers with a Reject and then a Logout through
//   leaves that discard the emit's result.
//
// The stores: a MemoryStoreFactory with the unbounded policy, which is what
// src/capi/config.cpp gives a C-ABI session, with no wrapper; and the scene's FileStore
// behind the recording wrapper.

enum class Store : std::uint8_t { memory, file };
enum class Second : std::uint8_t { app_send, inbound_arm };

struct TwoFrameOutcome {
    std::string setup_miss;
    unsigned k = 0;
    // Read when the emit ran.
    bool emit_ran = false;
    long long next_outbound_at_emit = -1;
    bool first_ready_at_emit = false;
    bool inbound_running_at_emit = false;
    // After both have completed.
    std::optional<fixpp::core::expected_t<void>> first;   // frame k's Engine::send
    std::optional<fixpp::core::expected_t<void>> second;  // the second Engine::send
    Inbound inbound;                                      // the inbound arm
    fsm_state state = fsm_state::NotConnected;
    int logouts = 0;
    long long next_outbound = -1;
    StoreContent store;           // the FileStore only
    std::vector<WireFrame> wire;  // every frame the peer read after setup, to end-of-stream
    bool peer_read_ended = false;
    bool stop_completed = false;
};

TwoFrameOutcome run_two_frames(Store const store, Second const second) {
    TwoFrameOutcome o;
    HeldFileStoreScene scene;
    auto& rig = scene.rig;
    auto app = std::make_shared<CountingApp>();
    rig.app = app;
    if (store == Store::memory) {
        scene.tweak_cfg = [](fixpp::session::SessionConfig& c) {
            fixpp::session::MemoryStore::Config mcfg;
            mcfg.policy = fixpp::session::capacity_policy::unbounded;
            c.store_factory = std::make_shared<fixpp::session::MemoryStoreFactory>(mcfg);
        };
    }
    asio::cancellation_signal cancel;
    [&] {
        if (!scene.setup(1)) return;
        auto session = rig.session();
        if (!session) {
            scene.setup_miss = "the session is not published";
            return;
        }
        auto const strand = session->executor().underlying();
        auto next_outbound = [&] {
            return static_cast<long long>(
                session_test_access::seqnum_mgr(*session).peek_outbound());
        };
        o.k = scene.last + 1;
        if (next_outbound() != static_cast<long long>(o.k)) {
            scene.setup_miss =
                "the next outbound number after setup is not the one after the "
                "setup message's";
            return;
        }
        auto const rx_mark = rig.peer.received.size();
        auto const to_app_base = app->to_app;

        // A Heartbeat from the peer, in sequence, stamped a year before the clock.
        auto const stale = plain_rig::to_bytes(
            plain_rig::message(rig.begin_string, "0", 2, "TW", "ISLD", "20230101-00:00:00.000"));
        std::future<void> inbound_fut;
        std::future<fixpp::core::expected_t<void>> first_fut;
        std::future<fixpp::core::expected_t<void>> second_fut;

        app->on_to_app = [&](int const n) {
            if (n != to_app_base + 1) return;
            if (second == Second::inbound_arm) {
                asio::post(strand, [&] {
                    inbound_fut = asio::co_spawn(strand, deliver(*session, stale, o.inbound),
                                                 asio::use_future);
                });
            } else {
                asio::post(strand, [&] { second_fut = scene.send_app(); });
            }
            asio::post(strand, [&] {
                o.emit_ran = true;
                o.next_outbound_at_emit = next_outbound();
                o.first_ready_at_emit = first_fut.valid() && HeldFileStoreScene::ready(first_fut);
                o.inbound_running_at_emit = o.inbound.started && !o.inbound.completed;
                cancel.emit(asio::cancellation_type::terminal);
            });
        };

        first_fut = scene.send_app(asio::bind_cancellation_slot(cancel.slot(), asio::use_future));

        if (rig.run_until([&] { return HeldFileStoreScene::ready(first_fut); })) {
            o.first = first_fut.get();
        }
        if (second_fut.valid() &&
            rig.run_until([&] { return HeldFileStoreScene::ready(second_fut); })) {
            o.second = second_fut.get();
        }
        if (second == Second::inbound_arm) {
            (void)rig.run_until(
                [&] { return inbound_fut.valid() && HeldFileStoreScene::ready(inbound_fut); });
        }
        app->on_to_app = nullptr;
        // A short window: a frame the session wrote is read before the stop.
        rig.settle();
        o.state = session->state();
        o.logouts = app->logouts;
        o.next_outbound = next_outbound();

        if (store == Store::file) {
            if (auto* const s = scene.log->store; s != nullptr) {
                auto read_fut =
                    asio::co_spawn(rig.ioc, read_store(s, o.k, o.k + 2), asio::use_future);
                if (rig.run_until([&] { return HeldFileStoreScene::ready(read_fut); })) {
                    o.store = read_fut.get();
                }
            }
        }

        session.reset();
        scene.finish();
        o.peer_read_ended = rig.run_until([&] { return rig.peer.read_ended; });
        o.wire = wire_frames(scene.peer_frames_from(rx_mark));
    }();
    app->on_to_app = nullptr;
    scene.finish();
    o.setup_miss = scene.setup_miss;
    o.stop_completed = scene.stop_completed;
    return o;
}

// The gate: the second frame was under way, and frame k not yet completed, when the emit
// ran; and frame k's send then ended as a cancelled store.
::testing::AssertionResult two_frame_gate(TwoFrameOutcome const& o, Second const second) {
    if (!o.setup_miss.empty()) return ::testing::AssertionFailure() << o.setup_miss;
    if (!o.stop_completed) return ::testing::AssertionFailure() << "Engine::stop did not complete";
    const bool second_under_way = second == Second::app_send
                                      ? o.next_outbound_at_emit == static_cast<long long>(o.k) + 2
                                      : o.inbound_running_at_emit;
    if (!o.emit_ran || o.first_ready_at_emit || !second_under_way) {
        return ::testing::AssertionFailure()
               << "the path was not exercised: emit_ran=" << o.emit_ran
               << " frame_k_completed_at_the_emit=" << o.first_ready_at_emit
               << " next_outbound_at_the_emit=" << o.next_outbound_at_emit << " (k=" << o.k
               << ") inbound_arm_running_at_the_emit=" << o.inbound_running_at_emit;
    }
    if (!failed_with(o.first, error::dispatch_aborted)) {
        return ::testing::AssertionFailure()
               << "the path was not exercised: frame k's send did not end as a cancelled store; "
                  "it "
               << result_text(o.first);
    }
    return ::testing::AssertionSuccess();
}

std::size_t frames_of(std::vector<WireFrame> const& wire, std::string_view type) {
    return static_cast<std::size_t>(std::count_if(
        wire.begin(), wire.end(), [&](WireFrame const& f) { return f.type == type; }));
}

TEST(SecondFrameInFlight, MemoryStore_SecondSend_NeitherFrameIsTransmittedAndTheNumberIsTakenBack) {
    auto const o = run_two_frames(Store::memory, Second::app_send);
    ASSERT_TRUE(two_frame_gate(o, Second::app_send));
    // The window: a second send that started after frame k's cancellation was handled
    // is refused at its state test, and no number of it is in flight.
    ASSERT_FALSE(failed_with(o.second, error::session_invalid_state_for_send))
        << "the path was not exercised: the second send started after the session had ended";

    EXPECT_TRUE(failed_with(o.second, error::dispatch_aborted))
        << "the second send " << result_text(o.second);
    EXPECT_TRUE(o.peer_read_ended) << "the peer's read did not end";
    EXPECT_TRUE(o.wire.empty()) << "the peer read, after setup: " << wire_text(o.wire);
    EXPECT_EQ(o.next_outbound, static_cast<long long>(o.k))
        << "the next outbound number in memory is not frame k's own";
    EXPECT_EQ(o.state, fsm_state::Disconnected);
    EXPECT_EQ(o.logouts, 1) << "onLogout calls";
}

TEST(SecondFrameInFlight,
     MemoryStore_InboundArm_PeerReadsTheLogoutAtTheCancelledNumberAndNoReject) {
    auto const o = run_two_frames(Store::memory, Second::inbound_arm);
    ASSERT_TRUE(two_frame_gate(o, Second::inbound_arm));

    EXPECT_EQ(frames_of(o.wire, "3"), 0U) << "the peer read a Reject: " << wire_text(o.wire);
    // fixpp#534 owns this boundary: what an inbound arm that is already running sends
    // after the session has ended. A change of it flips this assertion.
    EXPECT_EQ(o.wire, (std::vector<WireFrame>{{"5", o.k}}))
        << "the peer must read one frame, the Logout, numbered k=" << o.k
        << "; it read: " << wire_text(o.wire);
    EXPECT_EQ(o.next_outbound, static_cast<long long>(o.k) + 1)
        << "the next outbound number in memory is not the one after the Logout's";
    EXPECT_TRUE(o.inbound.returned_success()) << "on_inbound_frame " << o.inbound.text();
    EXPECT_TRUE(o.peer_read_ended) << "the peer's read did not end";
    EXPECT_EQ(o.state, fsm_state::Disconnected);
    EXPECT_EQ(o.logouts, 1) << "onLogout calls";
}

// Control: on a persistent store the second frame is refused by the store as out of
// order, and feature 059's route ends it without transmitting.
TEST(SecondFrameInFlight, Control_FileStore_SecondSend_RefusedByTheStoreAndNotTransmitted) {
    auto const o = run_two_frames(Store::file, Second::app_send);
    ASSERT_TRUE(two_frame_gate(o, Second::app_send));

    EXPECT_TRUE(failed_with(o.second, error::store_seqnum_out_of_order))
        << "the second send " << result_text(o.second);
    EXPECT_TRUE(o.peer_read_ended) << "the peer's read did not end";
    EXPECT_TRUE(o.wire.empty()) << "the peer read, after setup: " << wire_text(o.wire);
    EXPECT_EQ(o.next_outbound, static_cast<long long>(o.k))
        << "the next outbound number in memory is not frame k's own";
    ASSERT_TRUE(o.store.read) << "the store was not read";
    EXPECT_EQ(o.store.next_outbound, std::optional<seqnum_t>{o.k}) << o.store.text();
}

// The arm is suspended at its inbound persist when frame k is cancelled, so neither
// reply has a number yet. Control: what the peer reads afterwards starts at k and has no
// hole, and the store holds every frame the peer read.
TEST(SecondFrameInFlight, Control_FileStore_InboundArm_WireStartsAtTheCancelledNumberWithNoHole) {
    auto const o = run_two_frames(Store::file, Second::inbound_arm);
    ASSERT_TRUE(two_frame_gate(o, Second::inbound_arm));
    ASSERT_TRUE(o.store.read) << "the store was not read";

    EXPECT_FALSE(o.wire.empty()) << "the peer read no frame after the cancellation";
    for (std::size_t i = 0; i < o.wire.size(); ++i) {
        EXPECT_EQ(o.wire[i].seq, o.k + i)
            << "the peer read a number with a lower one missing: " << wire_text(o.wire);
        EXPECT_EQ(o.store.type_at(o.wire[i].seq), o.wire[i].type)
            << "the store does not hold a frame the peer read: " << wire_text(o.wire)
            << "; the store: " << o.store.text();
    }
    EXPECT_TRUE(o.peer_read_ended) << "the peer's read did not end";
}

// The one reading of that scene the take-back changes: the Reject takes the cancelled
// frame's number, so the store accepts it.
TEST(SecondFrameInFlight,
     FileStore_InboundArm_PeerReadsTheRejectAtTheCancelledNumberThenTheLogout) {
    auto const o = run_two_frames(Store::file, Second::inbound_arm);
    ASSERT_TRUE(two_frame_gate(o, Second::inbound_arm));
    ASSERT_TRUE(o.store.read) << "the store was not read";

    EXPECT_EQ(o.wire, (std::vector<WireFrame>{{"3", o.k}, {"5", o.k + 1}}))
        << "the peer must read the Reject, numbered k=" << o.k
        << ", and then the Logout; it read: " << wire_text(o.wire);
    EXPECT_TRUE(o.store.type_at(o.k) == "3" && o.store.type_at(o.k + 1) == "5")
        << "the store does not hold both replies: " << o.store.text();
}

// ── D-OUT-10: 059's reconcile over a number assigned after the take-back (R-23) ──────
//
// A pin of a stated limitation. One Engine::send (number h) is held inside its store()'s
// offload, so the store's lock stays taken. Three store calls are then queued behind it,
// one at a time:
//   frame k      an Engine::send bound to a cancellation signal of the cell's;
//   frame k + 1  a second Engine::send;
//   the persist  the inbound persist of an arm that will answer with a Reject and then
//                a Logout (the Heartbeat of the suite above, sent by the peer).
// Order 1 queues them as k, k + 1, the persist; Order 2 as k, the persist, k + 1.
//
// How the order is established, with no sleep. Each call is started alone; the cell
// waits for the wrapper's record that its body was entered, and then runs the ready
// handlers until none is, which takes the call past the FileStore's leading post to the
// lock; only then is the next call started. What the wrapper records is the entry of its
// own body, not the arrival at the lock: the arrival is taken from the entry order and
// the exhausted poll between two starts. The order in which the lock was then granted is
// read from the order in which the inner calls returned.
//
// The arms: frame k cancelled at the lock, with all three queued; and the control, with
// the hold released and nothing emitted.

enum class Order : std::uint8_t { send_then_persist, persist_then_send };

struct ReconcileOutcome {
    std::string setup_miss;
    unsigned h = 0;
    unsigned k = 0;
    bool hold_entered = false;
    std::optional<StoreCall> store_k;
    std::optional<StoreCall> store_k1;
    std::optional<StoreCall> persist;
    bool all_queued = false;  // each entered and not returned, with the hold in flight
    std::size_t cancel_mark = 0;
    std::size_t release_mark = 0;
    std::optional<fixpp::core::expected_t<void>> held;    // frame h's sender
    std::optional<fixpp::core::expected_t<void>> first;   // frame k's
    std::optional<fixpp::core::expected_t<void>> second;  // frame k + 1's
    // The arm's own outbound store calls, in call order: the Reject's, then the Logout's.
    std::vector<StoreCall> arm_stores;
    bool reconcile_read_made = false;  // a next_seqnum(outbound, false) after the queueing
    fsm_state state = fsm_state::NotConnected;
    int logouts = 0;
    long long next_outbound = -1;
    StoreContent store;
    std::vector<WireFrame> wire;  // every frame the peer read after setup, to end-of-stream
    bool peer_read_ended = false;
    bool hold_timed_out = false;
    bool stop_completed = false;

    // The frames among `wire` that are the arm's replies.
    [[nodiscard]] std::vector<WireFrame> replies() const {
        std::vector<WireFrame> out;
        for (auto const& f : wire) {
            if (f.type == "3" || f.type == "5") out.push_back(f);
        }
        return out;
    }
};

ReconcileOutcome run_lock_order(Order const order, bool const cancel_k) {
    ReconcileOutcome o;
    HeldFileStoreScene scene;
    auto& rig = scene.rig;
    auto app = std::make_shared<CountingApp>();
    rig.app = app;
    asio::cancellation_signal cancel;
    [&] {
        if (!scene.setup(1)) return;
        auto session = rig.session();
        if (!session) {
            scene.setup_miss = "the session is not published";
            return;
        }
        o.h = scene.last + 1;
        o.k = scene.last + 2;
        auto const rx_mark = rig.peer.received.size();

        // The hold: frame h, caught inside its store()'s offload.
        store_hold::arm();
        auto held_fut = scene.send_app();
        if (!rig.run_until([] { return store_hold::holding(); })) {
            scene.setup_miss = "the offload hook did not enter its hold";
            return;
        }
        o.hold_entered = true;

        auto const queue_mark = scene.log->mark();
        auto store_of = [&](seqnum_t seq) {
            return scene.log->first_after(queue_mark, [seq](StoreCall const& c) {
                return c.op == store_op::store && c.dir == direction_t::outbound && c.first == seq;
            });
        };
        auto persist_of = [&] {
            return scene.log->first_after(queue_mark, [](StoreCall const& c) {
                return c.op == store_op::next_seqnum && c.dir == direction_t::inbound &&
                       c.increment;
            });
        };
        // Waits for the call's entry, then runs the ready handlers until none is.
        auto queue = [&](auto find) {
            if (!rig.run_until([&] { return find() != nullptr && find()->entered(); })) {
                return false;
            }
            rig.ioc.poll();
            rig.ioc.restart();
            return !find()->returned() && store_hold::holding();
        };

        auto first_fut =
            scene.send_app(asio::bind_cancellation_slot(cancel.slot(), asio::use_future));
        bool queued = queue([&] { return store_of(o.k); });
        std::future<fixpp::core::expected_t<void>> second_fut;
        auto start_second = [&] {
            second_fut = scene.send_app();
            return queue([&] { return store_of(o.k + 1); });
        };
        auto start_arm = [&] {
            // A Heartbeat from the peer, in sequence, stamped a year before the clock.
            rig.peer.send(plain_rig::message(rig.begin_string, "0", 2, "TW", "ISLD",
                                             "20230101-00:00:00.000"));
            return queue(persist_of);
        };
        if (order == Order::send_then_persist) {
            queued = queued && start_second() && start_arm();
        } else {
            queued = queued && start_arm() && start_second();
        }
        o.all_queued = queued && store_of(o.k) != nullptr && !store_of(o.k)->returned() &&
                       store_of(o.k + 1) != nullptr && !store_of(o.k + 1)->returned() &&
                       persist_of() != nullptr && !persist_of()->returned();
        if (!o.all_queued) return;

        if (cancel_k) {
            o.cancel_mark = scene.log->mark();
            cancel.emit(asio::cancellation_type::terminal);
            // The bound: the hold is released only below, so nothing but the cell's
            // cancellation can complete frame k's send before then.
            if (rig.run_until([&] { return HeldFileStoreScene::ready(first_fut); })) {
                o.first = first_fut.get();
            }
        }
        o.release_mark = scene.log->mark();
        store_hold::release();

        if (rig.run_until([&] { return HeldFileStoreScene::ready(held_fut); })) {
            o.held = held_fut.get();
        }
        if (!cancel_k && rig.run_until([&] { return HeldFileStoreScene::ready(first_fut); })) {
            o.first = first_fut.get();
        }
        if (rig.run_until([&] { return HeldFileStoreScene::ready(second_fut); })) {
            o.second = second_fut.get();
        }
        // The arm has ended when it has recorded Disconnected, which it does after its
        // Logout leaf, and every store call it made has returned.
        auto arm_stores = [&] {
            std::vector<StoreCall> out;
            for (auto const& c : scene.log->calls()) {
                if (c.called_at > o.release_mark && c.op == store_op::store &&
                    c.dir == direction_t::outbound) {
                    out.push_back(c);
                }
            }
            return out;
        };
        (void)rig.run_until([&] {
            auto const calls = arm_stores();
            return calls.size() >= 2 && calls[0].returned() && calls[1].returned() &&
                   session->state() == fsm_state::Disconnected;
        });
        // A short window: a frame the session wrote is read before the stop.
        rig.settle();

        o.store_k = *store_of(o.k);
        o.store_k1 = *store_of(o.k + 1);
        o.persist = *persist_of();
        o.arm_stores = arm_stores();
        o.reconcile_read_made = scene.log->first_after(queue_mark, [](StoreCall const& c) {
            return c.op == store_op::next_seqnum && c.dir == direction_t::outbound && !c.increment;
        }) != nullptr;
        o.state = session->state();
        o.logouts = app->logouts;
        o.next_outbound =
            static_cast<long long>(session_test_access::seqnum_mgr(*session).peek_outbound());

        if (auto* const s = scene.log->store; s != nullptr) {
            auto read_fut = asio::co_spawn(rig.ioc, read_store(s, o.h, o.k + 4), asio::use_future);
            if (rig.run_until([&] { return HeldFileStoreScene::ready(read_fut); })) {
                o.store = read_fut.get();
            }
        }

        session.reset();
        scene.finish();
        o.peer_read_ended = rig.run_until([&] { return rig.peer.read_ended; });
        o.wire = wire_frames(scene.peer_frames_from(rx_mark));
    }();
    store_hold::release();
    scene.finish();
    o.setup_miss = scene.setup_miss;
    o.hold_timed_out = store_hold::timed_out();
    o.stop_completed = scene.stop_completed;
    return o;
}

std::string call_text(std::optional<StoreCall> const& c) {
    if (!c) return "not called";
    if (!c->entered()) return "called, and its body never started";
    if (!c->returned()) return "entered, and never returned";
    return c->error ? "returned error " + std::to_string(static_cast<int>(*c->error))
                    : std::string{"returned a value"};
}

// The gate: the three calls were queued in the order the cell names; frame k's store()
// was cancelled at the lock, between the emit and the release (or, in the control,
// stored after the release); and the lock was granted in the order the cell names.
::testing::AssertionResult lock_order_gate(ReconcileOutcome const& o, Order const order,
                                           bool const cancel_k) {
    if (!o.setup_miss.empty()) return ::testing::AssertionFailure() << o.setup_miss;
    if (!o.hold_entered || !o.all_queued || o.hold_timed_out) {
        return ::testing::AssertionFailure()
               << "the path was not exercised: the three calls were not queued behind the hold: "
                  "hold_entered="
               << o.hold_entered << " all_queued=" << o.all_queued
               << " hold_timed_out=" << o.hold_timed_out;
    }
    if (!o.stop_completed) return ::testing::AssertionFailure() << "Engine::stop did not complete";
    if (!o.store_k || !o.store_k1 || !o.persist) {
        return ::testing::AssertionFailure() << "the path was not exercised: a call's record is "
                                                "missing";
    }
    const bool entry_order = order == Order::send_then_persist
                                 ? o.store_k->entered_at < o.store_k1->entered_at &&
                                       o.store_k1->entered_at < o.persist->entered_at
                                 : o.store_k->entered_at < o.persist->entered_at &&
                                       o.persist->entered_at < o.store_k1->entered_at;
    if (!entry_order) {
        return ::testing::AssertionFailure() << "the path was not exercised: entry order";
    }
    const bool frame_k =
        cancel_k
            ? o.store_k->returned() && o.store_k->returned_at > o.cancel_mark &&
                  o.store_k->returned_at < o.release_mark &&
                  o.store_k->error == error::store_cancelled
            : o.store_k->returned() && o.store_k->returned_at > o.release_mark && !o.store_k->error;
    if (!frame_k) {
        return ::testing::AssertionFailure()
               << "the path was not exercised: frame k's store() " << call_text(o.store_k)
               << (cancel_k ? "; it must return store_cancelled between the emit and the release"
                            : "; in the control it must be stored after the release");
    }
    if (!o.store_k1->returned() || !o.persist->returned()) {
        return ::testing::AssertionFailure()
               << "the path was not exercised: frame k + 1's store() " << call_text(o.store_k1)
               << "; the persist " << call_text(o.persist);
    }
    const bool grant_order = order == Order::send_then_persist
                                 ? o.store_k1->returned_at < o.persist->returned_at
                                 : o.persist->returned_at < o.store_k1->returned_at;
    if (!grant_order) {
        return ::testing::AssertionFailure()
               << "the path was not exercised: the lock was not granted in the order the calls "
                  "were queued";
    }
    return ::testing::AssertionSuccess();
}

// What both orders read when frame k is cancelled.
void expect_cancelled_frame_ending(ReconcileOutcome const& o) {
    EXPECT_TRUE(failed_with(o.first, error::dispatch_aborted))
        << "frame k's sender: " << result_text(o.first);
    EXPECT_TRUE(failed_with(o.second, error::store_seqnum_out_of_order))
        << "frame k + 1's sender: " << result_text(o.second);
    EXPECT_EQ(o.state, fsm_state::Disconnected);
    EXPECT_EQ(o.logouts, 1) << "onLogout calls";
    EXPECT_TRUE(o.peer_read_ended) << "the peer's read did not end";
}

// Order 1, frame k + 1 ahead of the persist.
TEST(ReconcileAfterTheTakeBack,
     SecondSendAheadOfThePersist_PeerReadsNeitherReplyAndStoreHoldsTheReject) {
    auto const o = run_lock_order(Order::send_then_persist, /*cancel_k=*/true);
    ASSERT_TRUE(lock_order_gate(o, Order::send_then_persist, true));
    ASSERT_TRUE(o.store.read) << "the store was not read";
    expect_cancelled_frame_ending(o);

    EXPECT_TRUE(o.replies().empty() && o.store.type_at(o.k) == "3")
        << "the peer must read neither reply, and the store must hold the Reject at k=" << o.k
        << "; the peer read: " << wire_text(o.wire) << "; the store: " << o.store.text();
    ASSERT_GE(o.arm_stores.size(), 2U) << "the arm made fewer than two outbound store calls";
    EXPECT_TRUE(o.arm_stores[1].returned() && o.arm_stores[1].error.has_value())
        << "the Logout's store call was not refused: " << call_text(o.arm_stores[1]);
    EXPECT_EQ(o.next_outbound, static_cast<long long>(o.k) + 1)
        << "the next outbound number in memory is not the one after k";
}

// Order 2, the persist ahead of frame k + 1.
TEST(ReconcileAfterTheTakeBack,
     PersistAheadOfTheSecondSend_PeerReadsBothRepliesFromTheCancelledNumber) {
    auto const o = run_lock_order(Order::persist_then_send, /*cancel_k=*/true);
    ASSERT_TRUE(lock_order_gate(o, Order::persist_then_send, true));
    ASSERT_TRUE(o.store.read) << "the store was not read";
    expect_cancelled_frame_ending(o);

    // fixpp#534: this reading of the peer is the one that issue's batch flips.
    EXPECT_EQ(o.replies(), (std::vector<WireFrame>{{"3", o.k}, {"5", o.k + 1}}))
        << "the peer must read the Reject at k=" << o.k
        << " and the Logout at k + 1; it read: " << wire_text(o.wire);
    EXPECT_TRUE(o.store.type_at(o.k) == "3" && o.store.type_at(o.k + 1) == "5")
        << "the store does not hold both replies: " << o.store.text();
}

// The two orders differ on the wire. fixpp#534: once a Disconnected session stops
// sending, they do not, and this cell is the one that issue's batch removes or flips. Of
// Order 1's readings only "the store holds the Reject" can change with it.
TEST(ReconcileAfterTheTakeBack, TheTwoLockOrdersDifferInWhatThePeerReads) {
    auto const order1 = run_lock_order(Order::send_then_persist, /*cancel_k=*/true);
    ASSERT_TRUE(lock_order_gate(order1, Order::send_then_persist, true));
    auto const order2 = run_lock_order(Order::persist_then_send, /*cancel_k=*/true);
    ASSERT_TRUE(lock_order_gate(order2, Order::persist_then_send, true));

    EXPECT_NE(order1.replies(), order2.replies())
        << "both orders: the peer read the replies " << wire_text(order1.replies());
}

// The control of each order: frame k released in place of cancelled. Every sender is
// told success, the peer reads every frame in order, and no reconcile runs.
void expect_released_frame_ending(ReconcileOutcome const& o) {
    EXPECT_TRUE(o.held.has_value() && o.held->has_value()) << "frame h: " << result_text(o.held);
    EXPECT_TRUE(o.first.has_value() && o.first->has_value()) << "frame k: " << result_text(o.first);
    EXPECT_TRUE(o.second.has_value() && o.second->has_value())
        << "frame k + 1: " << result_text(o.second);
    ASSERT_EQ(o.wire.size(), 5U) << "the peer read: " << wire_text(o.wire);
    for (std::size_t i = 0; i < o.wire.size(); ++i) {
        EXPECT_EQ(o.wire[i].seq, o.h + i) << "the peer read: " << wire_text(o.wire);
    }
    EXPECT_EQ(frames_of(o.wire, "D"), 3U) << wire_text(o.wire);
    EXPECT_EQ(frames_of(o.wire, "3"), 1U) << wire_text(o.wire);
    EXPECT_EQ(frames_of(o.wire, "5"), 1U) << wire_text(o.wire);
    EXPECT_FALSE(o.reconcile_read_made) << "a reconcile read of the store's next outbound number "
                                           "was made";
}

TEST(ReconcileAfterTheTakeBack,
     Control_SecondSendAheadOfThePersist_FrameReleased_EveryFrameInOrder) {
    auto const o = run_lock_order(Order::send_then_persist, /*cancel_k=*/false);
    ASSERT_TRUE(lock_order_gate(o, Order::send_then_persist, false));
    expect_released_frame_ending(o);
}

TEST(ReconcileAfterTheTakeBack,
     Control_PersistAheadOfTheSecondSend_FrameReleased_EveryFrameInOrder) {
    auto const o = run_lock_order(Order::persist_then_send, /*cancel_k=*/false);
    ASSERT_TRUE(lock_order_gate(o, Order::persist_then_send, false));
    expect_released_frame_ending(o);
}

}  // namespace
