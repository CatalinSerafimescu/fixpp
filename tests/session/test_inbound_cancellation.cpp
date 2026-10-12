// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_inbound_cancellation.cpp
//
// Ruling R-14 (.specify/536-537-538-recovery-liveness.md §9, D-CAN-1 to D-CAN-6; §5, the
// table "The inbound cancellation (§9)"): a cancellation, or any other exception, met
// while an inbound frame is processed ends the session at that frame, the same way on
// both timings, and nothing is thrown out of Session::on_inbound_frame.
//
// In every cell here the frame is handed to on_inbound_frame by a coroutine of the
// cell's own, bound to a cancellation signal of the cell's
// (tests/support/calling_thread_readings.hpp). The cells an Engine's read pump drives
// are in test_inbound_cancellation_engine.cpp and
// test_538_cancelled_read_measured_path.cpp.
//
// Two timings of one cancellation:
//   thrown    the cell emits in the call expression of a store method (the recording
//             wrapper's call hook), so the cancellation is already recorded when the
//             session awaits that call: the co_await throws and the call's body never
//             starts. The cell shows that from the wrapper's record before it asserts
//             anything else; a missing record fails as "the path was not exercised".
//   returned  the awaited call is suspended when the cell emits, and reports the
//             cancellation as an error value.
//
// Fixtures: held_file_store_scene.hpp (a session under an Engine on a FileStore; the
// cell's coroutine runs on the session's executor) and logon_arm_fixtures.hpp (a
// directly driven acceptor). A cell asserts nothing fatal while a scene's engine runs.

#include <gtest/gtest.h>

#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/transport/test/mock_transport.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "session/held_file_store_scene.hpp"
#include "session/logon_arm_fixtures.hpp"
#include "session/plain_engine_rig.hpp"
#include "support/calling_thread_readings.hpp"
#include "support/recording_store.hpp"
#include "support/session_test_access.hpp"

namespace {

namespace plain_rig = fixpp::test_support::plain_rig;
namespace store_hold = fixpp::test_support::store_hold;
using fixpp::core::error;
using fixpp::session::close_mode;
using fixpp::session::direction_t;
using fixpp::session::fsm_state;
using fixpp::session::session_test_access;
using fixpp::test_support::await_and_read_thread;
using fixpp::test_support::CallingThreadReadings;
using fixpp::test_support::HeldFileStoreScene;
using fixpp::test_support::store_op;
using fixpp::test_support::StoreCall;
using fixpp::test_support::logon_arm::DirectAcceptor;
using fixpp::test_support::logon_arm::ring_text;

constexpr int kAppMessages = 3;

// What a store call's record says, for a failure message.
std::string record_text(std::optional<StoreCall> const& c) {
    if (!c) return "not called";
    if (!c->entered()) return "called, and its body never started";
    if (!c->returned()) return "entered, and never returned";
    return c->error ? "returned error " + std::to_string(static_cast<int>(*c->error))
                    : std::string{"returned a value"};
}

// The frames `frames` holds of MsgType `type` with PossDupFlag(43)=Y.
std::size_t replays(std::vector<std::string> const& frames, std::string_view type) {
    std::size_t n = 0;
    for (auto const& f : frames) {
        n += static_cast<std::size_t>(plain_rig::field_value(f, "35") == type &&
                                      plain_rig::field_value(f, "43") == "Y");
    }
    return n;
}

// ── On the scene: a session under an Engine, the frame handed over by the cell ───────

// What a cell on the scene recorded. The session is sampled by the cell's coroutine
// as soon as the call has returned, before any other handler.
struct SceneOutcome {
    std::string setup_miss;
    bool gate = false;  // the cell's hook ran, or its emit was made where it names
    CallingThreadReadings call;
    fsm_state state_after_call = fsm_state::NotConnected;
    int logouts_after_call = 0;
    std::optional<StoreCall> store_call;   // the store call the cell aimed at
    std::vector<std::string> peer_frames;  // what the peer read after the cell's frame
    bool hold_timed_out = false;
    bool stop_completed = false;
};

// Spawns the cell's coroutine on the session's executor, bound to `cancel`.
std::future<void> hand_over(fixpp::session::Session& session, std::span<const std::byte> bytes,
                            plain_rig::CountingApp& app, asio::cancellation_signal& cancel,
                            SceneOutcome& o) {
    return asio::co_spawn(
        session.executor().underlying(),
        await_and_read_thread([&session, bytes] { return session.on_inbound_frame(bytes); }, o.call,
                              [&session, &app, &o] {
                                  o.state_after_call = session.state();
                                  o.logouts_after_call = app.logouts;
                              }),
        asio::bind_cancellation_slot(cancel.slot(), asio::use_future));
}

enum class WalkInput : std::uint8_t {
    cancel_at_the_read,  // the cell's cancellation, emitted in the read's call expression
    no_emit,             // the same hook, and nothing emitted
    read_throws,         // the read's body throws an exception that is not a cancellation
};

// A ResendRequest over the stored application messages, handed to on_inbound_frame by
// the cell's coroutine. The store call aimed at is the walk's retrieve of the slot
// after the first replayed one.
SceneOutcome run_walk(WalkInput const input) {
    SceneOutcome o;
    HeldFileStoreScene scene;
    auto& rig = scene.rig;
    auto const app = std::make_shared<plain_rig::CountingApp>();
    rig.app = app;
    asio::cancellation_signal cancel;
    [&] {
        if (!scene.setup(kAppMessages)) return;
        auto session = rig.session();
        if (!session) {
            scene.setup_miss = "the session is not published";
            return;
        }
        auto const next_slot = scene.first + 1;
        auto is_next_slot_read = [next_slot](StoreCall const& call) {
            return call.op == store_op::retrieve && call.dir == direction_t::outbound &&
                   call.first == next_slot;
        };
        bool hook_ran = false;
        scene.log->on_call = [&](StoreCall const& call) {
            if (hook_ran || !is_next_slot_read(call)) return;
            hook_ran = true;
            // Total: the write of the first replayed frame left the thread accepting it.
            if (input == WalkInput::cancel_at_the_read) cancel.emit(asio::cancellation_type::total);
        };
        if (input == WalkInput::read_throws) scene.log->throw_in = is_next_slot_read;

        auto const mark = scene.log->mark();
        auto const rx_mark = rig.peer.received.size();
        auto const bytes =
            plain_rig::to_bytes(rig.msg("2", 2,
                                        "7=" + std::to_string(scene.first) + "\x01" +
                                            "16=" + std::to_string(scene.last) + "\x01"));
        auto fut = hand_over(*session, bytes, *app, cancel, o);
        (void)rig.run_until([&] { return HeldFileStoreScene::ready(fut); });
        o.gate = hook_ran;
        if (auto const* r = scene.log->first_after(mark, is_next_slot_read)) o.store_call = *r;
        rig.settle();
        o.peer_frames = scene.peer_frames_from(rx_mark);
        session.reset();
    }();
    if (scene.log) scene.log->on_call = nullptr;
    scene.finish();
    o.setup_miss = scene.setup_miss;
    o.stop_completed = scene.stop_completed;
    return o;
}

// D-CAN-1, D-CAN-3, D-CAN-4, a directly driven call. The thrown timing of a cancelled
// read of the resend walk.
TEST(InboundCancellation, WalkReadCancelledBeforeItsAwait_EndsTheSessionAndReturnsStoreCancelled) {
    auto const o = run_walk(WalkInput::cancel_at_the_read);
    ASSERT_TRUE(o.setup_miss.empty()) << o.setup_miss;
    ASSERT_TRUE(o.stop_completed);
    ASSERT_TRUE(o.gate && o.call.completed && o.store_call.has_value() && !o.store_call->entered())
        << "the path was not exercised: hook_ran=" << o.gate
        << " call_completed=" << o.call.completed << " the read: " << record_text(o.store_call);

    EXPECT_FALSE(o.call.threw) << "on_inbound_frame must return a value; it " << o.call.call_text();
    EXPECT_TRUE(o.call.returned_error(error::store_cancelled))
        << "on_inbound_frame " << o.call.call_text();
    EXPECT_EQ(o.state_after_call, fsm_state::Disconnected) << "the state after the call";
    EXPECT_EQ(o.logouts_after_call, 1) << "onLogout calls after the call";
    // The calling thread, as the returned timing leaves it (D-CAN-3).
    EXPECT_FALSE(o.call.cancellation_recorded)
        << "a cancellation is still recorded on the calling thread after the call";
    EXPECT_FALSE(o.call.next_await_threw)
        << "the caller's next co_await of an asio operation threw";
}

// The same hook, and nothing emitted: the range is replayed and the session stays up.
TEST(InboundCancellation, Control_WalkReadNotCancelled_RangeReplayed) {
    auto const o = run_walk(WalkInput::no_emit);
    ASSERT_TRUE(o.setup_miss.empty()) << o.setup_miss;
    ASSERT_TRUE(o.stop_completed);
    ASSERT_TRUE(o.gate && o.call.completed && o.store_call.has_value() && o.store_call->entered())
        << "the path was not exercised: the read: " << record_text(o.store_call);

    EXPECT_TRUE(o.call.returned_success()) << "on_inbound_frame " << o.call.call_text();
    EXPECT_EQ(o.state_after_call, fsm_state::Active);
    EXPECT_EQ(o.logouts_after_call, 0);
    EXPECT_FALSE(o.call.cancellation_recorded);
    EXPECT_FALSE(o.call.next_await_threw);
    EXPECT_EQ(replays(o.peer_frames, "D"), static_cast<std::size_t>(kAppMessages))
        << store_hold::printable(o.peer_frames);
}

// D-CAN-2, an exception that is not a cancellation, driven directly: the body of one
// slot's retrieve throws std::runtime_error.
TEST(InboundCancellation, WalkReadThrowsAnotherException_EndsTheSessionAndReturnsDispatchAborted) {
    auto const o = run_walk(WalkInput::read_throws);
    ASSERT_TRUE(o.setup_miss.empty()) << o.setup_miss;
    ASSERT_TRUE(o.stop_completed);
    ASSERT_TRUE(o.gate && o.call.completed && o.store_call.has_value() && o.store_call->entered() &&
                !o.store_call->returned())
        << "the path was not exercised: the read's body must start and not return; the read: "
        << record_text(o.store_call);

    EXPECT_FALSE(o.call.threw) << "on_inbound_frame must return a value; it " << o.call.call_text();
    EXPECT_TRUE(o.call.returned_error(error::dispatch_aborted))
        << "on_inbound_frame " << o.call.call_text();
    EXPECT_EQ(o.state_after_call, fsm_state::Disconnected) << "the state after the call";
}

// ── D-CAN-3, the calling thread after the call, both timings ─────────────────────────

bool is_walk_tail_read(StoreCall const& call) {
    return call.op == store_op::next_seqnum && call.dir == direction_t::outbound && !call.increment;
}

// What the returned timing of a cancelled read of the walk recorded, beyond the scene's
// own outcome.
struct LockedWalkOutcome {
    SceneOutcome o;
    bool hold_entered = false;
    bool queued = false;  // the walk's read was waiting at the store's lock at the emit
    std::size_t cancel_mark = 0;
    std::size_t release_mark = 0;
    // The arm went on to its inbound persist after the walk: a store call it makes only
    // when the walk returned no error.
    bool inbound_persist_called = false;

    // The window: the read returned store_cancelled between the emit and the release.
    // Nothing but the cell's cancellation can end a wait at the lock in that interval.
    [[nodiscard]] bool exercised() const {
        return hold_entered && queued && o.gate && o.call.completed && !o.hold_timed_out &&
               o.store_call.has_value() && o.store_call->returned() &&
               o.store_call->returned_at > cancel_mark &&
               o.store_call->returned_at < release_mark &&
               o.store_call->error == error::store_cancelled;
    }
    [[nodiscard]] std::string text() const {
        return "hold_entered=" + std::to_string(hold_entered) +
               " read_queued=" + std::to_string(queued) +
               " read_returned_before_the_release=" + std::to_string(o.gate) +
               " call_completed=" + std::to_string(o.call.completed) +
               " hold_timed_out=" + std::to_string(o.hold_timed_out) +
               " the read: " + record_text(o.store_call);
    }
};

// The returned timing: the same ResendRequest as run_walk's, handed over while one
// Engine::send is held inside its store(), so the walk's first store read queues at the
// FileStore's lock. The cell's cancellation is emitted while it waits there; the store
// returns store_cancelled from its lock.
LockedWalkOutcome run_walk_read_cancelled_at_the_lock() {
    LockedWalkOutcome w;
    auto& o = w.o;
    HeldFileStoreScene scene;
    auto& rig = scene.rig;
    auto const app = std::make_shared<plain_rig::CountingApp>();
    rig.app = app;
    asio::cancellation_signal cancel;
    [&] {
        if (!scene.setup(kAppMessages)) return;
        auto session = rig.session();
        if (!session) {
            scene.setup_miss = "the session is not published";
            return;
        }
        // The hold: one Engine::send, caught inside its store()'s offload.
        store_hold::arm();
        auto held_fut = scene.send_app();
        if (!rig.run_until([] { return store_hold::holding(); })) {
            scene.setup_miss = "the offload hook did not enter its hold";
            return;
        }
        w.hold_entered = true;
        auto const mark = scene.log->mark();
        auto read = [&] { return scene.log->first_after(mark, is_walk_tail_read); };
        auto const rx_mark = rig.peer.received.size();
        auto const bytes =
            plain_rig::to_bytes(rig.msg("2", 2,
                                        "7=" + std::to_string(scene.first) + "\x01" +
                                            "16=" + std::to_string(scene.last) + "\x01"));
        auto fut = hand_over(*session, bytes, *app, cancel, o);
        // The gate, with no sleep: the read's body has started; the handlers that are
        // ready then run until none is, which takes the call to the store's lock, where
        // it queues behind the held store().
        if (rig.run_until([&] { return read() != nullptr && read()->entered(); })) {
            rig.ioc.poll();
            rig.ioc.restart();
            w.queued = !read()->returned() && store_hold::holding();
        }
        if (w.queued) {
            w.cancel_mark = scene.log->mark();
            cancel.emit(asio::cancellation_type::total);
            // The bound: the hold is released only below, so nothing but the cell's
            // cancellation can end the read's wait before then.
            o.gate = rig.run_until([&] { return read()->returned(); });
            o.store_call = *read();
        }
        // What the session wrote for the request reaches the peer while the hold is
        // still in flight, so the held message is not among it.
        rig.settle();
        o.peer_frames = scene.peer_frames_from(rx_mark);
        w.release_mark = scene.log->mark();
        store_hold::release();
        (void)rig.run_until([&] { return HeldFileStoreScene::ready(held_fut); });
        (void)rig.run_until([&] { return HeldFileStoreScene::ready(fut); });
        w.inbound_persist_called = scene.log->first_after(mark, [](StoreCall const& call) {
            return call.op == store_op::next_seqnum && call.dir == direction_t::inbound &&
                   call.increment;
        }) != nullptr;
        session.reset();
    }();
    scene.finish();
    o.setup_miss = scene.setup_miss;
    o.hold_timed_out = store_hold::timed_out();
    o.stop_completed = scene.stop_completed;
    return w;
}

// The returned twin of WalkReadCancelledBeforeItsAwait_…: the store reports the
// cancelled read as store_cancelled, the walk returns that code unchanged (D-538-4,
// ruling R-13), and the session ends at the arm.
TEST(InboundCancellation, WalkReadCancelledAtTheStoresLock_EndsTheSessionAndReturnsStoreCancelled) {
    auto const w = run_walk_read_cancelled_at_the_lock();
    auto const& o = w.o;
    ASSERT_TRUE(o.setup_miss.empty()) << o.setup_miss;
    ASSERT_TRUE(o.stop_completed);
    ASSERT_TRUE(w.exercised()) << "the path was not exercised: " << w.text();

    EXPECT_TRUE(o.call.returned_error(error::store_cancelled))
        << "on_inbound_frame " << o.call.call_text();
    EXPECT_EQ(o.state_after_call, fsm_state::Disconnected) << "the state after the call";
    EXPECT_EQ(o.logouts_after_call, 1) << "onLogout calls after the call";
    EXPECT_TRUE(o.peer_frames.empty())
        << "the peer read a frame after the ResendRequest whose read was cancelled:\n"
        << store_hold::printable(o.peer_frames);
    EXPECT_FALSE(w.inbound_persist_called)
        << "the arm went on to its inbound persist after the walk's read was cancelled";
}

// The pair: one cancellation of a read of the walk, on its two timings. The readings a
// caller of on_inbound_frame can take must not tell the timings apart.
TEST(InboundCancellation, WalkReadCancelled_BothTimingsLeaveTheSameReadings) {
    auto const thrown = run_walk(WalkInput::cancel_at_the_read);
    ASSERT_TRUE(thrown.setup_miss.empty()) << thrown.setup_miss;
    ASSERT_TRUE(thrown.stop_completed);
    ASSERT_TRUE(thrown.gate && thrown.call.completed && thrown.store_call.has_value() &&
                !thrown.store_call->entered())
        << "the path was not exercised, thrown timing: the read: "
        << record_text(thrown.store_call);
    auto const w = run_walk_read_cancelled_at_the_lock();
    auto const& returned = w.o;
    ASSERT_TRUE(returned.setup_miss.empty()) << returned.setup_miss;
    ASSERT_TRUE(returned.stop_completed);
    ASSERT_TRUE(w.exercised()) << "the path was not exercised, returned timing: " << w.text();

    // fixpp#583 questions what a lock sequence leaves on its caller's thread, which is
    // where the returned timing gets the last two readings. Each reading below compares
    // the two timings and names no absolute state: a change on one side alone fails it,
    // and D-CAN-3's reset must then change with it.
    EXPECT_EQ(thrown.call.call_text(), returned.call.call_text()) << "what the call returned";
    EXPECT_EQ(thrown.state_after_call, returned.state_after_call) << "the state after the call";
    EXPECT_EQ(thrown.logouts_after_call, returned.logouts_after_call)
        << "onLogout calls after the call";
    EXPECT_EQ(thrown.call.cancellation_recorded, returned.call.cancellation_recorded)
        << "whether a cancellation is recorded on the calling thread after the call";
    EXPECT_EQ(thrown.call.next_await_threw, returned.call.next_await_threw)
        << "whether the caller's next co_await of an asio operation threw";
}

// ── D-CAN-4, the inbound persist of an in-sequence Heartbeat ─────────────────────────

bool is_inbound_persist(StoreCall const& call) {
    return call.op == store_op::next_seqnum && call.dir == direction_t::inbound && call.increment;
}

enum class PersistInput : std::uint8_t {
    cancel_at_the_call,  // thrown: the cell's terminal cancellation, in the call expression
    store_fails,         // the wrapper's body returns a chosen code
};

// An in-sequence Heartbeat handed to on_inbound_frame by the cell's coroutine.
SceneOutcome run_persist(PersistInput const input,
                         error const fail_code = error::store_io_failure) {
    SceneOutcome o;
    HeldFileStoreScene scene;
    auto& rig = scene.rig;
    auto const app = std::make_shared<plain_rig::CountingApp>();
    rig.app = app;
    asio::cancellation_signal cancel;
    [&] {
        if (!scene.setup(1)) return;
        auto session = rig.session();
        if (!session) {
            scene.setup_miss = "the session is not published";
            return;
        }
        bool hook_ran = false;
        scene.log->on_call = [&](StoreCall const& call) {
            if (hook_ran || !is_inbound_persist(call)) return;
            hook_ran = true;
            // Terminal: the arm's sequence-number lock has left the thread's filter
            // terminal, so a total emit would be dropped.
            if (input == PersistInput::cancel_at_the_call) {
                cancel.emit(asio::cancellation_type::terminal);
            }
        };
        if (input == PersistInput::store_fails) {
            scene.log->fail_in = is_inbound_persist;
            scene.log->fail_code = fail_code;
        }
        auto const mark = scene.log->mark();
        auto const bytes = plain_rig::to_bytes(rig.heartbeat(2));
        auto fut = hand_over(*session, bytes, *app, cancel, o);
        (void)rig.run_until([&] { return HeldFileStoreScene::ready(fut); });
        o.gate = hook_ran;
        if (auto const* r = scene.log->first_after(mark, is_inbound_persist)) o.store_call = *r;
        session.reset();
    }();
    if (scene.log) scene.log->on_call = nullptr;
    scene.finish();
    o.setup_miss = scene.setup_miss;
    o.stop_completed = scene.stop_completed;
    return o;
}

// The thrown input: the persist's co_await throws, and the call returns the code the
// returned input returns.
TEST(InboundCancellation, InboundPersistCancelledBeforeItsAwait_ReturnsStoreCancelled) {
    auto const o = run_persist(PersistInput::cancel_at_the_call);
    ASSERT_TRUE(o.setup_miss.empty()) << o.setup_miss;
    ASSERT_TRUE(o.stop_completed);
    ASSERT_TRUE(o.gate && o.call.completed && o.store_call.has_value() && !o.store_call->entered())
        << "the path was not exercised: the persist: " << record_text(o.store_call);

    EXPECT_FALSE(o.call.threw) << "on_inbound_frame must return a value; it " << o.call.call_text();
    EXPECT_TRUE(o.call.returned_error(error::store_cancelled))
        << "on_inbound_frame " << o.call.call_text();
    EXPECT_EQ(o.state_after_call, fsm_state::Disconnected) << "the state after the call";
}

// The returned input: the persist is queued at the FileStore's lock, behind a store()
// the cell holds, when the cell emits. The store returns store_cancelled from its lock,
// and on_inbound_frame returns that code unchanged.
TEST(InboundCancellation, InboundPersistCancelledAtTheStoresLock_ReturnsStoreCancelled) {
    SceneOutcome o;
    bool hold_entered = false;
    bool queued = false;
    std::size_t cancel_mark = 0;
    std::size_t release_mark = 0;
    {
        HeldFileStoreScene scene;
        auto& rig = scene.rig;
        auto const app = std::make_shared<plain_rig::CountingApp>();
        rig.app = app;
        asio::cancellation_signal cancel;
        [&] {
            if (!scene.setup(1)) return;
            auto session = rig.session();
            if (!session) {
                scene.setup_miss = "the session is not published";
                return;
            }
            // The hold: one Engine::send, caught inside its store()'s offload.
            store_hold::arm();
            auto held_fut = scene.send_app();
            if (!rig.run_until([] { return store_hold::holding(); })) {
                scene.setup_miss = "the offload hook did not enter its hold";
                return;
            }
            hold_entered = true;
            auto const mark = scene.log->mark();
            auto persist = [&] { return scene.log->first_after(mark, is_inbound_persist); };
            auto const bytes = plain_rig::to_bytes(rig.heartbeat(2));
            auto fut = hand_over(*session, bytes, *app, cancel, o);
            // The gate, with no sleep: the persist's body has started; the handlers
            // that are ready then run until none is, which takes the call to the
            // store's lock, where it queues behind the held store().
            if (rig.run_until([&] { return persist() != nullptr && persist()->entered(); })) {
                rig.ioc.poll();
                rig.ioc.restart();
                queued = !persist()->returned() && store_hold::holding();
            }
            if (queued) {
                cancel_mark = scene.log->mark();
                cancel.emit(asio::cancellation_type::total);
                // The bound: the hold is released only below, so nothing but the
                // cell's cancellation can complete the call before then.
                o.gate = rig.run_until([&] { return HeldFileStoreScene::ready(fut); });
                o.store_call = *persist();
            }
            release_mark = scene.log->mark();
            store_hold::release();
            (void)rig.run_until([&] { return HeldFileStoreScene::ready(held_fut); });
            (void)rig.run_until([&] { return HeldFileStoreScene::ready(fut); });
            session.reset();
        }();
        scene.finish();
        o.setup_miss = scene.setup_miss;
        o.hold_timed_out = store_hold::timed_out();
        o.stop_completed = scene.stop_completed;
    }
    ASSERT_TRUE(o.setup_miss.empty()) << o.setup_miss;
    ASSERT_TRUE(o.stop_completed);
    ASSERT_TRUE(hold_entered && queued && o.gate && o.call.completed && !o.hold_timed_out)
        << "the path was not exercised: hold_entered=" << hold_entered
        << " persist_queued=" << queued << " call_completed_before_the_release=" << o.gate
        << " hold_timed_out=" << o.hold_timed_out;
    ASSERT_TRUE(o.store_call.has_value() && o.store_call->returned() &&
                o.store_call->returned_at > cancel_mark &&
                o.store_call->returned_at < release_mark &&
                o.store_call->error == error::store_cancelled)
        << "the path was not exercised: the persist must return store_cancelled between the "
           "emit and the release; the persist: "
        << record_text(o.store_call);

    EXPECT_TRUE(o.call.returned_error(error::store_cancelled))
        << "on_inbound_frame " << o.call.call_text();
    EXPECT_EQ(o.state_after_call, fsm_state::Disconnected) << "the state after the call";
}

// The guard input: a failed persist that is not a cancellation keeps the one code the
// function has always returned for a failed store call, whatever the store returned.
TEST(InboundCancellation, Guard_InboundPersistFailsWithAnotherCode_ReturnsStoreIoFailure) {
    for (error const code : {error::store_io_failure, error::store_seqnum_out_of_order}) {
        SCOPED_TRACE("the store returned error " + std::to_string(static_cast<int>(code)));
        auto const o = run_persist(PersistInput::store_fails, code);
        ASSERT_TRUE(o.setup_miss.empty()) << o.setup_miss;
        ASSERT_TRUE(o.stop_completed);
        ASSERT_TRUE(o.gate && o.call.completed && o.store_call.has_value() &&
                    o.store_call->returned() && o.store_call->error == code)
            << "the path was not exercised: the persist: " << record_text(o.store_call);

        EXPECT_TRUE(o.call.returned_error(error::store_io_failure))
            << "on_inbound_frame " << o.call.call_text();
        EXPECT_EQ(o.state_after_call, fsm_state::Disconnected) << "the state after the call";
    }
}

// ── D-CAN-5, a superseded Logon arm, the closing leg ─────────────────────────────────
//
// A directly driven acceptor (logon_arm_fixtures.hpp). close(graceful) is the close
// used: begun in LogonReceived, its phase 1 records LogoutSent itself, so a state the
// arm recorded afterwards shows in the ring.

// The stored application messages of the 789 path's seed.
constexpr unsigned kSeeded = 3;

struct ClosingLegOutcome {
    bool opened = false;
    bool hook_ran = false;
    bool closing_in_hook = false;   // close() had begun when the hook returned
    std::optional<StoreCall> read;  // the store call the cell aimed at
    CallingThreadReadings call;
    std::vector<fsm_state> ring_at_hook;      // before the close was spawned
    std::vector<fsm_state> ring_after_spawn;  // the close has run to its first suspension
    std::vector<fsm_state> ring_after_call;   // when on_inbound_frame has returned
    std::vector<fsm_state> ring_at_end;       // the close has returned
    bool close_returned = false;
};

enum class ClosingLegInput : std::uint8_t {
    cancel_at_the_read,  // the cell's terminal cancellation, in the read's call expression
    read_throws,         // the read's body throws an exception that is not a cancellation
};

// The peer's Logon asks for a replay (789 below the session's next outbound number).
// The call hook on the walk's first store read spawns the driver's close(), which runs
// to its first suspension before the hook returns.
ClosingLegOutcome run_closing_leg(ClosingLegInput const input) {
    ClosingLegOutcome o;
    DirectAcceptor f{DirectAcceptor::Options{.enable_789 = true}};
    if (!f.open() || !f.seed_outbound(kSeeded)) return o;
    o.opened = true;

    f.log->arm();
    asio::cancellation_signal cancel;
    std::future<fixpp::core::expected_t<void>> close_fut;
    bool reply_stored = false;
    // The walk's first store read: the first outbound next_seqnum(_, false) after the
    // reply Logon's store().
    auto is_first_read = [&](StoreCall const& call) {
        return reply_stored && call.op == store_op::next_seqnum &&
               call.dir == direction_t::outbound && !call.increment;
    };
    f.log->on_call = [&](StoreCall const& call) {
        if (o.hook_ran) return;
        if (call.op == store_op::store && call.dir == direction_t::outbound) {
            reply_stored = true;
            return;
        }
        if (!is_first_read(call)) return;
        o.hook_ran = true;
        o.ring_at_hook = f.ring();
        close_fut = f.spawn_close(close_mode::graceful);
        o.closing_in_hook = session_test_access::is_closing(*f.session);
        o.ring_after_spawn = f.ring();
        // Terminal: the arm's sequence-number locks have left the thread's filter
        // terminal, so a total emit would be dropped.
        if (input == ClosingLegInput::cancel_at_the_read) {
            cancel.emit(asio::cancellation_type::terminal);
        }
    };
    if (input == ClosingLegInput::read_throws) f.log->throw_in = is_first_read;

    auto const mark = f.log->mark();
    auto const logon = f.logon("789=1\x01");
    auto fut = f.feed(logon, o.call, cancel, [&] { o.ring_after_call = f.ring(); });
    (void)f.run_until([&] { return DirectAcceptor::ready(fut); });
    // The bound: close(graceful)'s grace wait is the one real-time timer here, and it is
    // the fixture's logout_disconnect_timeout_ms, far below the pump's budget.
    if (close_fut.valid()) {
        o.close_returned = f.run_until([&] { return DirectAcceptor::ready(close_fut); });
    }
    f.log->on_call = nullptr;
    if (auto const* r = f.log->first_after(mark, [](StoreCall const& call) {
            return call.op == store_op::next_seqnum && call.dir == direction_t::outbound &&
                   !call.increment;
        })) {
        o.read = *r;
    }
    o.ring_at_end = f.ring();
    return o;
}

// The states written after `from`'s entries, in `ring`.
std::vector<fsm_state> written_after(std::vector<fsm_state> const& from,
                                     std::vector<fsm_state> const& ring) {
    if (ring.size() < from.size()) return {};
    return {ring.begin() + static_cast<std::ptrdiff_t>(from.size()), ring.end()};
}

// The thrown input.
TEST(InboundCancellation, SupersededLogonArm_Closing_ReadCancelledBeforeItsAwait_ReturnsSuccess) {
    auto const o = run_closing_leg(ClosingLegInput::cancel_at_the_read);
    ASSERT_TRUE(o.opened) << "the fixture did not open and seed its session";
    ASSERT_TRUE(o.hook_ran && o.closing_in_hook && o.call.completed && o.read.has_value() &&
                !o.read->entered())
        << "the path was not exercised: hook_ran=" << o.hook_ran
        << " closing_in_hook=" << o.closing_in_hook << " call_completed=" << o.call.completed
        << " the read: " << record_text(o.read);

    EXPECT_FALSE(o.call.threw) << "on_inbound_frame must return a value; it " << o.call.call_text();
    EXPECT_TRUE(o.call.returned_success()) << "on_inbound_frame " << o.call.call_text();
    // The arm recorded no state of its own: when the call has returned, the ring holds
    // what the close had written when the hook returned, and nothing more.
    EXPECT_EQ(o.ring_after_call, o.ring_after_spawn)
        << "ring when the call returned: " << ring_text(o.ring_after_call)
        << "; ring when the hook returned: " << ring_text(o.ring_after_spawn);
    EXPECT_TRUE(o.close_returned);
    EXPECT_EQ(written_after(o.ring_at_hook, o.ring_at_end),
              (std::vector<fsm_state>{fsm_state::LogoutSent, fsm_state::Disconnected}))
        << "states written after the close began: "
        << ring_text(written_after(o.ring_at_hook, o.ring_at_end));
}

// The third input: an exception that is not a cancellation, under the same supersession.
TEST(InboundCancellation,
     SupersededLogonArm_Closing_ReadThrowsAnotherException_ReturnsDispatchAborted) {
    auto const o = run_closing_leg(ClosingLegInput::read_throws);
    ASSERT_TRUE(o.opened) << "the fixture did not open and seed its session";
    ASSERT_TRUE(o.hook_ran && o.closing_in_hook && o.call.completed && o.read.has_value() &&
                o.read->entered() && !o.read->returned())
        << "the path was not exercised: hook_ran=" << o.hook_ran
        << " closing_in_hook=" << o.closing_in_hook << " call_completed=" << o.call.completed
        << " the read: " << record_text(o.read);

    EXPECT_FALSE(o.call.threw) << "on_inbound_frame must return a value; it " << o.call.call_text();
    EXPECT_TRUE(o.call.returned_error(error::dispatch_aborted))
        << "on_inbound_frame " << o.call.call_text();
    EXPECT_EQ(o.ring_after_call, o.ring_after_spawn)
        << "ring when the call returned: " << ring_text(o.ring_after_call)
        << "; ring when the hook returned: " << ring_text(o.ring_after_spawn);
    EXPECT_TRUE(o.close_returned);
}

bool is_first_hydration_read(StoreCall const& call) {
    return call.op == store_op::next_seqnum && call.dir == direction_t::inbound && !call.increment;
}

// The returned twin, at a site that returns its error when superseded: the acceptor's
// first hydration read is held by the wrapper; the driver calls close(); the wrapper
// then returns store_cancelled.
TEST(InboundCancellation,
     SupersededLogonArm_Closing_HydrationReadReturnsStoreCancelled_ReturnsSuccess) {
    DirectAcceptor f{DirectAcceptor::Options{.file_store = true}};
    ASSERT_TRUE(f.open());
    f.log->arm();
    f.log->hold_in = is_first_hydration_read;
    f.log->held_result = error::store_cancelled;

    CallingThreadReadings call;
    asio::cancellation_signal cancel;
    std::vector<fsm_state> ring_after_call;
    auto const mark = f.log->mark();
    auto const logon = f.logon();
    auto fut = f.feed(logon, call, cancel, [&] { ring_after_call = f.ring(); });
    ASSERT_TRUE(f.run_until([&] { return f.log->holding(); }))
        << "the path was not exercised: the hydration read's hold was not entered";

    // The driver's close(). Nothing it awaits is behind the held read, so it completes
    // while the read is held; the bound is one bounded pump.
    auto close_fut = f.spawn_close(close_mode::graceful);
    bool const close_returned = f.run_until([&] { return DirectAcceptor::ready(close_fut); });
    bool const held_through_the_close = f.log->holding() && !DirectAcceptor::ready(fut);
    auto const ring_before_release = f.ring();

    f.log->release_hold();
    bool const call_done = f.run_until([&] { return DirectAcceptor::ready(fut); });
    std::optional<StoreCall> read;
    if (auto const* r = f.log->first_after(mark, is_first_hydration_read)) read = *r;

    ASSERT_TRUE(close_returned && held_through_the_close && call_done && call.completed &&
                !f.log->hold_timed_out() && read.has_value() && read->returned() &&
                read->error == error::store_cancelled)
        << "the path was not exercised: close_returned=" << close_returned
        << " read_held_through_the_close=" << held_through_the_close << " call_done=" << call_done
        << " hold_timed_out=" << f.log->hold_timed_out() << " the read: " << record_text(read);

    EXPECT_TRUE(call.returned_success()) << "on_inbound_frame " << call.call_text();
    // The arm recorded no state of its own after the read returned.
    EXPECT_EQ(ring_after_call, ring_before_release)
        << "ring when the call returned: " << ring_text(ring_after_call)
        << "; ring before the read was released: " << ring_text(ring_before_release);
}

// ── D-CAN-1 (R-20), the hydration flag ───────────────────────────────────────────────

// A cancellation thrown at the acceptor's first hydration read leaves
// ensure_hydrated_ by an exception. The flag it set before the read must be clear when
// on_inbound_frame's caller has the call's outcome, whether a value or an exception.
TEST(InboundCancellation, HydrationReadCancelledBeforeItsAwait_LeavesTheHydratingFlagClear) {
    DirectAcceptor f{DirectAcceptor::Options{.file_store = true}};
    ASSERT_TRUE(f.open());
    f.log->arm();
    asio::cancellation_signal cancel;
    bool hook_ran = false;
    f.log->on_call = [&](StoreCall const& c) {
        if (hook_ran || !is_first_hydration_read(c)) return;
        hook_ran = true;
        cancel.emit(asio::cancellation_type::terminal);
    };
    CallingThreadReadings call;
    bool hydrating_after_call = false;
    auto const mark = f.log->mark();
    auto const logon = f.logon();
    auto fut = f.feed(logon, call, cancel,
                      [&] { hydrating_after_call = session_test_access::hydrating(*f.session); });
    bool const call_done = f.run_until([&] { return DirectAcceptor::ready(fut); });
    f.log->on_call = nullptr;
    std::optional<StoreCall> read;
    if (auto const* r = f.log->first_after(mark, is_first_hydration_read)) read = *r;

    ASSERT_TRUE(hook_ran && call_done && call.completed && read.has_value() && !read->entered())
        << "the path was not exercised: hook_ran=" << hook_ran << " call_done=" << call_done
        << " the read: " << record_text(read);

    EXPECT_FALSE(hydrating_after_call)
        << "hydrating_ is still set after the call (which " << call.call_text() << ")";
}

// ── D-CAN-6: a reply's transmit step, as §9 leaves it ────────────────────────────────
//
// A directly driven acceptor on a live transport: a mock_transport whose every write
// waits `kWriteLatency` before it completes, and honours a cancellation during that
// wait by returning transport_write_cancelled, with the cancellation still recorded on
// the thread (the plain transport does the same; the note's route (w2)). The cell emits
// while a reply's write is in that wait.
//
// The gate, shown from the cell's own readings: the reply's store() has returned, no
// handler is ready, the call has not completed, and the transport has completed no
// further write; so the call is suspended in the write's wait. The bound: that wait's
// own timer is the one thing besides the cell's emit that can end it, and a write it
// ended would be counted by the transport as completed, which the cell asserts did not
// happen.

constexpr std::chrono::milliseconds kWriteLatency{1000};

struct TransmitOutcome {
    bool active = false;
    bool write_in_flight = false;  // the gate
    std::size_t writes_before = 0;
    std::size_t writes_after = 0;
    CallingThreadReadings call;
    fsm_state state_after_call = fsm_state::NotConnected;
    std::string written;  // every byte the transport wrote
};

// Logs the peer on over the mock transport, then hands `make_frame(f)` to
// on_inbound_frame and emits the cell's cancellation while the write of the first
// reply stored afterwards is in flight.
TransmitOutcome run_transmit(std::function<std::vector<std::byte>(DirectAcceptor&)> make_frame) {
    using fixpp::transport::test::mock_transport;
    using fixpp::transport::test::Script;
    TransmitOutcome o;
    DirectAcceptor f{DirectAcceptor::Options{}};
    if (!f.open()) return o;
    Script script;
    script.write_latency = kWriteLatency;
    auto owned = std::make_unique<mock_transport>(f.ioc.get_executor(), std::move(script));
    auto* const transport = owned.get();
    f.session->attach_accepted_transport(std::move(owned), fixpp::transport::handshake_result{});

    CallingThreadReadings logon_call;
    auto const logon = f.logon();
    if (!f.feed_plain(logon, logon_call) || f.session->state() != fsm_state::Active) return o;
    o.active = true;

    f.log->arm();
    auto const mark = f.log->mark();
    auto reply_stored = [&] {
        auto const* s = f.log->first_after(mark, [](StoreCall const& call) {
            return call.op == store_op::store && call.dir == direction_t::outbound;
        });
        return s != nullptr && s->returned();
    };
    o.writes_before = transport->async_writes_observed();
    asio::cancellation_signal cancel;
    auto const frame = make_frame(f);
    auto fut = f.feed(frame, o.call, cancel, [&] { o.state_after_call = f.session->state(); });
    if (f.run_until(reply_stored, kWriteLatency / 2)) {
        f.ioc.poll();
        f.ioc.restart();
        o.write_in_flight =
            !DirectAcceptor::ready(fut) && transport->async_writes_observed() == o.writes_before;
    }
    if (o.write_in_flight) cancel.emit(asio::cancellation_type::total);
    (void)f.run_until([&] { return DirectAcceptor::ready(fut); });
    o.writes_after = transport->async_writes_observed();
    auto const bytes = transport->outbound_bytes_seen();
    o.written.assign(reinterpret_cast<char const*>(bytes.data()), bytes.size());
    return o;
}

// A caller that returns the emit's result: a TestRequest's Heartbeat reply. A control:
// no statement of §9 is on this trace.
TEST(InboundCancellation, Control_TransmitStep_ReplyWriteCancelled_CallerReturnsTheEmitsResult) {
    auto const o = run_transmit([](DirectAcceptor& f) { return f.msg("1", 2, "112=TR1\x01"); });
    ASSERT_TRUE(o.active) << "the session did not reach Active over the mock transport";
    ASSERT_TRUE(o.write_in_flight && o.call.completed && o.writes_after == o.writes_before)
        << "the path was not exercised: write_in_flight=" << o.write_in_flight
        << " call_completed=" << o.call.completed << " writes_before=" << o.writes_before
        << " writes_after=" << o.writes_after;

    EXPECT_FALSE(o.call.threw) << "on_inbound_frame " << o.call.call_text();
    // fixpp#584 flips this assertion: the transmit step's cancellation gets one ending.
    EXPECT_TRUE(o.call.returned_error(error::dispatch_aborted))
        << "on_inbound_frame " << o.call.call_text();
    EXPECT_EQ(o.state_after_call, fsm_state::Disconnected);
    // fixpp#584 flips this assertion: the calling thread is left clear.
    EXPECT_TRUE(o.call.cancellation_recorded)
        << "no cancellation is recorded on the calling thread after the call";
    EXPECT_TRUE(plain_rig::frames_of_type(o.written, "0").empty())
        << "the Heartbeat reply was written";
}

// A caller that discards the emit's result: the Reject of a frame whose SendingTime(52)
// is outside the threshold. The arm goes on to its Logout, whose first await meets the
// cancellation the write left recorded; that is R-14's class, and the catch ends it.
TEST(InboundCancellation, TransmitStep_ReplyWriteCancelled_CallerDiscardsTheResult_EndsAtTheCatch) {
    auto const o = run_transmit([](DirectAcceptor& f) {
        return plain_rig::to_bytes(
            plain_rig::message(f.cfg.begin_string, "0", 2, "TW", "ISLD", "20230101-00:00:00.000"));
    });
    ASSERT_TRUE(o.active) << "the session did not reach Active over the mock transport";
    ASSERT_TRUE(o.write_in_flight && o.call.completed && o.writes_after == o.writes_before)
        << "the path was not exercised: write_in_flight=" << o.write_in_flight
        << " call_completed=" << o.call.completed << " writes_before=" << o.writes_before
        << " writes_after=" << o.writes_after;

    EXPECT_TRUE(plain_rig::frames_of_type(o.written, "3").empty()) << "the Reject was written";
    EXPECT_TRUE(plain_rig::frames_of_type(o.written, "5").empty()) << "a Logout followed";
    // The catch's ending (D-CAN-1, D-CAN-3, D-CAN-4).
    EXPECT_FALSE(o.call.threw) << "on_inbound_frame must return a value; it " << o.call.call_text();
    EXPECT_TRUE(o.call.returned_error(error::store_cancelled))
        << "on_inbound_frame " << o.call.call_text();
    EXPECT_FALSE(o.call.cancellation_recorded)
        << "a cancellation is still recorded on the calling thread after the call";
}

}  // namespace
