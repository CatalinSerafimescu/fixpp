// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_538_cancelled_read_measured_path.cpp
//
// fixpp#538, ruling R-13 (.specify/536-537-538-recovery-liveness.md, D-538-4, "Why
// `store_cancelled` aborts" and "What follows a cancelled read"; §5, the D-538-4 row
// whose fixture this file is). Engine::stop meets a resend answer in progress, on the
// fixture of held_file_store_scene.hpp.
//
// ── The walk's tail read waits for the store's lock ──────────────────────────────────
//
// Shape, both cells:
//   1. Application messages go out through Engine::send, and the peer reads them.
//   2. One more Engine::send is held inside its store(), so the store's lock stays taken.
//   3. The peer sends a ResendRequest over the stored application messages.
//   4. The gate, with no sleep: the wrapper records that next_seqnum(outbound, false)
//      was entered after the ResendRequest; the test then runs the handlers that are
//      ready until none is, which takes the call past FileStore::next_seqnum's leading
//      post to the store's lock, where it queues behind the held store().
//
// StopDuringAnswer_PeerReadsNothingMore then spawns Engine::stop itself, pumps to the
// peer's end-of-stream, and only then releases the hold: step 3 of Engine::stop waits
// for the held send, so Rig::stop() cannot be used there. The peer must read no frame
// after its ResendRequest.
//
// Control_ReleaseBeforeStop_RangeReplayed is the same up to the queued tail read. It
// releases the hold and stops the engine only at teardown, after the answer has
// completed: the peer reads the stored messages replayed with PossDupFlag(43)=Y and
// no GapFill. So "no frame" in the first cell cannot be met by an empty range.
//
// The first cell shows that its window was hit before it asserts anything about the
// peer: the wrapper's record of the tail read, ordered against the stop and the
// release. The record also says which way the cancellation arrived: the read's body was
// entered and the store returned store_cancelled, or the read was called and its body
// never started because the walk's co_await threw. FileStore::next_seqnum returns
// store_cancelled only from a failed lock, so the record needs no diagnostic in library
// source. A missing record fails the cell as "the path was not exercised".
//
// ── The neighbouring path: a cancellation recorded before a store call is awaited ────
//
// Measurement_StopAtNextSlotRead_WalkEndsByException and its control pin what the code
// does today on a path the ruling's words do not cover; they are not cells of the
// design. No store call is held. The wrapper's call hook runs in the walk's own call
// expression for the slot after the first replayed frame, that is, after a write on the
// plain transport has left the coroutine thread accepting a total cancellation, and
// before the walk's co_await of that read. There the cell spawns Engine::stop. asio
// starts a spawned coroutine by dispatch, and a dispatch to a strand from inside it
// runs inline, so the stop's step 1 emits its cancellation before the hook returns;
// that inline run is the cell's premise, and its window-hit record is what checks it.
// The control installs the same hook and spawns nothing.
//
// Every arm records and asserts nothing while its engine runs, and stops the engine on
// every path: ~Engine requires a completed stop, so a fatal assertion between start and
// stop would abort the process.

#include <gtest/gtest.h>

#if defined(__linux__)
#include <poll.h>
#endif

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <future>
#include <optional>
#include <string>
#include <vector>

#include "session/held_file_store_scene.hpp"
#include "session/plain_engine_rig.hpp"
#include "support/recording_store.hpp"

namespace {

namespace plain_rig = fixpp::test_support::plain_rig;
namespace store_hold = fixpp::test_support::store_hold;
using fixpp::session::direction_t;
using fixpp::session::fsm_state;
using fixpp::test_support::HeldFileStoreScene;
using fixpp::test_support::store_op;
using fixpp::test_support::StoreCall;

constexpr int kAppMessages = 3;

// The peer's view of an answer: PossDup replays of stored messages, GapFills, the rest.
struct Answer {
    std::vector<std::string> replayed_seqs;  // 34 of each 35=D with 43=Y, in arrival order
    std::size_t gap_fills = 0;
    std::size_t other = 0;
};
Answer classify(std::vector<std::string> const& frames) {
    Answer a;
    for (auto const& f : frames) {
        auto const type = plain_rig::field_value(f, "35");
        if (type == "4") {
            ++a.gap_fills;
        } else if (type == "D" && plain_rig::field_value(f, "43") == "Y") {
            a.replayed_seqs.push_back(plain_rig::field_value(f, "34"));
        } else {
            ++a.other;
        }
    }
    return a;
}

std::vector<std::string> seqs(unsigned first, unsigned last) {
    std::vector<std::string> out;
    for (unsigned s = first; s <= last; ++s) out.push_back(std::to_string(s));
    return out;
}

// The peer has read an answer to a ResendRequest for `count` stored messages: that many
// replays, or a GapFill.
bool answered(std::vector<std::string> const& frames, int count) {
    auto const a = classify(frames);
    return a.gap_fills != 0 || a.replayed_seqs.size() >= static_cast<std::size_t>(count);
}

// ── The tail read at the store's lock ────────────────────────────────────────────────

enum class Arm : std::uint8_t { stop_during_answer, release_before_stop };

struct Outcome {
    std::string setup_miss;
    unsigned first = 0;
    unsigned last = 0;
    bool hold_entered = false;
    // The walk's tail read, as the wrapper recorded it; nullopt when it was never called.
    std::optional<StoreCall> tail_read;
    bool tail_read_queued = false;  // entered, not returned, and the hold in flight, at the gate
    std::size_t stop_mark = 0;     // log position taken immediately before Engine::stop was spawned
    std::size_t release_mark = 0;  // log position taken immediately before the hold was released
    bool hold_timed_out = false;
    bool peer_read_ended_before_release = false;
    bool stop_completed = false;
    bool answered = false;              // control arm: the peer read an answer
    std::vector<std::string> after_rr;  // every frame the peer read after its ResendRequest
};

Outcome run_arm(Arm const arm) {
    Outcome o;
    HeldFileStoreScene scene;
    auto& rig = scene.rig;
    [&] {
        if (!scene.setup(kAppMessages)) return;
        o.first = scene.first;
        o.last = scene.last;

        // 2. The hold: one more Engine::send, caught inside its store()'s offload.
        store_hold::arm();
        auto held_fut = scene.send_app();
        if (!rig.run_until([] { return store_hold::holding(); })) {
            scene.setup_miss = "the offload hook did not enter its hold";
            return;
        }
        o.hold_entered = true;

        // 3. The peer's ResendRequest over the stored application messages.
        auto const rr_mark = scene.log->mark();
        auto const rx_mark = rig.peer.received.size();
        scene.peer_resend_request(2, o.first, o.last);
        auto tail_read = [&] {
            return scene.log->first_after(rr_mark, [](StoreCall const& call) {
                return call.op == store_op::next_seqnum && call.dir == direction_t::outbound &&
                       !call.increment;
            });
        };

        // 4. The gate. The entry record is the trigger; one poll() to exhaustion then
        // runs FileStore::next_seqnum's leading post, and the call queues at the lock.
        if (rig.run_until([&] { return tail_read() != nullptr && tail_read()->entered(); })) {
            rig.ioc.poll();
            rig.ioc.restart();
            o.tail_read_queued = !tail_read()->returned() && store_hold::holding();
        }

        if (arm == Arm::stop_during_answer) {
            o.stop_mark = scene.log->mark();
            scene.spawn_stop();
            o.peer_read_ended_before_release = rig.run_until([&] { return rig.peer.read_ended; });
            o.release_mark = scene.log->mark();
            scene.finish();
        } else {
            o.release_mark = scene.log->mark();
            store_hold::release();
            o.answered = rig.run_until(
                [&] { return answered(scene.peer_frames_from(rx_mark), kAppMessages); });
            (void)rig.run_until([&] { return HeldFileStoreScene::ready(held_fut); });
            // The stop at teardown, after the answer has completed.
            scene.finish();
            (void)rig.run_until([&] { return rig.peer.read_ended; });
        }
        if (auto const* t = tail_read(); t != nullptr) o.tail_read = *t;
        o.after_rr = scene.peer_frames_from(rx_mark);
    }();
    scene.finish();
    o.setup_miss = scene.setup_miss;
    o.hold_timed_out = store_hold::timed_out();
    o.stop_completed = scene.stop_completed;
    return o;
}

// Both cells: the tail read's body was entered after the ResendRequest and stood queued
// at the store's lock, behind the held store(), when the cell acted.
::testing::AssertionResult tail_read_was_queued(Outcome const& o) {
    if (!o.setup_miss.empty()) {
        return ::testing::AssertionFailure() << "setup did not complete: " << o.setup_miss;
    }
    if (o.tail_read.has_value() && !o.tail_read->entered()) {
        return ::testing::AssertionFailure()
               << "the path was not exercised as the design describes it: the walk's tail read "
                  "was called and its body never started, so the cancellation was thrown at "
                  "the walk's co_await and not returned by the store";
    }
    if (!o.hold_entered || !o.tail_read.has_value() || !o.tail_read_queued) {
        return ::testing::AssertionFailure()
               << "the path was not exercised: hold_entered=" << o.hold_entered
               << " tail_read_recorded=" << o.tail_read.has_value()
               << " tail_read_queued_behind_the_hold=" << o.tail_read_queued;
    }
    if (o.hold_timed_out) {
        return ::testing::AssertionFailure()
               << "the path was not exercised: the hold ran out its bound, so the store's "
                  "lock was not held by the cell throughout";
    }
    return ::testing::AssertionSuccess();
}

std::string error_text(StoreCall const& call) {
    return call.error ? std::to_string(static_cast<int>(*call.error)) : std::string{"none"};
}

TEST(CancelledReadMeasuredPath, StopDuringAnswer_PeerReadsNothingMore) {
    auto const o = run_arm(Arm::stop_during_answer);

    // (a) The window was hit, by the route the design names: the read entered the store
    // before the stop was spawned, and the store returned store_cancelled after it,
    // while the hold was still in flight. Checked first: without it the assertions on
    // the peer below would hold for a walk that never met a cancelled read.
    ASSERT_TRUE(tail_read_was_queued(o));
    auto const& tail = *o.tail_read;
    ASSERT_TRUE(tail.entered_at < o.stop_mark && tail.returned() &&
                tail.returned_at > o.stop_mark && tail.returned_at < o.release_mark &&
                tail.error == fixpp::core::error::store_cancelled)
        << "the path was not exercised: the walk's tail read must enter the store before "
           "Engine::stop is spawned, and return store_cancelled after it and before the hold "
           "is released. entered_at="
        << tail.entered_at << " stop_mark=" << o.stop_mark << " returned_at=" << tail.returned_at
        << " release_mark=" << o.release_mark << " error=" << error_text(tail);
    RecordProperty("tail_read_route", "entered the store; the store returned store_cancelled");
    EXPECT_TRUE(o.stop_completed) << "Engine::stop did not complete after the release";

    // (b) R-13: a cancelled read aborts the answer. Nothing more is written, and the
    // peer's connection ends.
    EXPECT_TRUE(o.after_rr.empty())
        << "the peer read a frame after its ResendRequest for " << o.first << ".." << o.last
        << ", whose tail read was cancelled:\n"
        << store_hold::printable(o.after_rr);
    EXPECT_TRUE(o.peer_read_ended_before_release)
        << "the peer's read had not ended when the hold was released";
}

TEST(CancelledReadMeasuredPath, Control_ReleaseBeforeStop_RangeReplayed) {
    auto const o = run_arm(Arm::release_before_stop);

    ASSERT_TRUE(tail_read_was_queued(o));
    auto const& tail = *o.tail_read;
    EXPECT_TRUE(tail.returned() && tail.returned_at > o.release_mark && !tail.error)
        << "the queued tail read must return a value once the hold is released. returned_at="
        << tail.returned_at << " release_mark=" << o.release_mark << " error=" << error_text(tail);
    EXPECT_TRUE(o.answered) << "the peer read no answer to its ResendRequest";
    EXPECT_TRUE(o.stop_completed);

    auto const a = classify(o.after_rr);
    EXPECT_EQ(a.replayed_seqs, seqs(o.first, o.last))
        << "the stored range must be replayed with PossDupFlag(43)=Y, in order:\n"
        << store_hold::printable(o.after_rr);
    EXPECT_EQ(a.gap_fills, 0U) << "a GapFill was written over stored messages:\n"
                               << store_hold::printable(o.after_rr);
}

// ── The neighbouring path ────────────────────────────────────────────────────────────

struct NextSlotOutcome {
    std::string setup_miss;
    unsigned first = 0;
    unsigned last = 0;
    bool hook_ran = false;  // the walk called retrieve for the slot after the first one
    // That read, as the wrapper recorded it.
    std::optional<StoreCall> next_slot_read;
    // Read inside the hook, after Engine::stop was spawned and before the hook returned.
    bool engine_stopped_in_hook = false;
    bool peer_hang_up_in_hook = false;
    // Read by the test between the return of the handler that ran the hook and the
    // next handler.
    fsm_state state_after_hook_handler = fsm_state::NotConnected;
    bool engine_stopped_after_hook_handler = false;
    bool stop_ready_after_hook_handler = false;
    bool peer_hang_up_after_hook_handler = false;
    bool peer_hang_up_after_read_ended = false;  // the same reading, once the peer's read has ended
    fsm_state state_after_answer = fsm_state::NotConnected;  // control arm, before teardown
    // Read from the session itself once the stop the hook spawned had completed. The
    // engine no longer publishes the session then, so the arm keeps its own reference.
    fsm_state state_after_stop = fsm_state::NotConnected;
    bool peer_read_ended = false;
    bool stop_completed = false;
    std::vector<std::string> after_rr;
};

// True when the peer's socket reports that its other end has closed, read from the
// kernel without consuming input. Linux only; false elsewhere. The cells record it and
// assert nothing on it: it says whether the session's transport had been closed at
// that moment, as far as the peer's end of a loopback connection can tell.
bool peer_sees_hang_up(plain_rig::Peer& peer) {
#if defined(__linux__)
    pollfd p{};
    p.fd = peer.sock.native_handle();
    p.events = POLLRDHUP;
    return ::poll(&p, 1, 0) == 1 && (p.revents & (POLLRDHUP | POLLHUP)) != 0;
#else
    (void)peer;
    return false;
#endif
}

NextSlotOutcome run_next_slot_arm(bool const spawn_stop) {
    NextSlotOutcome o;
    HeldFileStoreScene scene;
    auto& rig = scene.rig;
    [&] {
        if (!scene.setup(kAppMessages)) return;
        o.first = scene.first;
        o.last = scene.last;
        auto const next_slot = o.first + 1;
        auto const session = rig.session();

        scene.log->on_call = [&, next_slot](StoreCall const& call) {
            if (o.hook_ran || call.op != store_op::retrieve || call.dir != direction_t::outbound ||
                call.first != next_slot) {
                return;
            }
            o.hook_ran = true;
            if (!spawn_stop) return;
            scene.spawn_stop();
            o.engine_stopped_in_hook = rig.engine->stopped();
            o.peer_hang_up_in_hook = peer_sees_hang_up(rig.peer);
        };

        auto const rr_mark = scene.log->mark();
        auto const rx_mark = rig.peer.received.size();
        scene.peer_resend_request(2, o.first, o.last);

        // One handler at a time, so that the state is read after the handler that ran
        // the hook and before any other.
        auto const deadline = std::chrono::steady_clock::now() + fixpp::test_support::kPumpBudget;
        while (!o.hook_ran && std::chrono::steady_clock::now() < deadline) {
            (void)rig.ioc.run_one_for(fixpp::test_support::kPumpSlice);
            rig.ioc.restart();
        }
        if (o.hook_ran) {
            o.state_after_hook_handler = rig.state();
            o.engine_stopped_after_hook_handler = rig.engine->stopped();
            o.stop_ready_after_hook_handler = scene.spawned_stop_ready();
            o.peer_hang_up_after_hook_handler = peer_sees_hang_up(rig.peer);
        }

        if (spawn_stop) {
            scene.finish();
            if (session && scene.stop_completed) o.state_after_stop = session->state();
            o.peer_read_ended = rig.run_until([&] { return rig.peer.read_ended; });
            o.peer_hang_up_after_read_ended = peer_sees_hang_up(rig.peer);
        } else {
            (void)rig.run_until(
                [&] { return answered(scene.peer_frames_from(rx_mark), kAppMessages); });
            o.state_after_answer = rig.state();
        }
        if (auto const* r = scene.log->first_after(rr_mark, [&](StoreCall const& call) {
                return call.op == store_op::retrieve && call.first == next_slot;
            })) {
            o.next_slot_read = *r;
        }
        o.after_rr = scene.peer_frames_from(rx_mark);
    }();
    if (scene.log) scene.log->on_call = nullptr;
    scene.finish();
    o.setup_miss = scene.setup_miss;
    o.stop_completed = scene.stop_completed;
    return o;
}

TEST(CancelledReadMeasuredPath, Measurement_StopAtNextSlotRead_WalkEndsByException) {
    auto const o = run_next_slot_arm(/*spawn_stop=*/true);
    ASSERT_TRUE(o.setup_miss.empty()) << o.setup_miss;

    // The window: the stop's step 1 ran before the hook returned, and the walk's
    // co_await of the read then threw, so the read's body never started.
    ASSERT_TRUE(o.hook_ran && o.engine_stopped_in_hook && o.next_slot_read.has_value() &&
                !o.next_slot_read->entered())
        << "the path was not exercised: hook_ran=" << o.hook_ran
        << " engine_stopped_in_hook=" << o.engine_stopped_in_hook
        << " next_slot_read_recorded=" << o.next_slot_read.has_value()
        << " body_entered=" << (o.next_slot_read.has_value() && o.next_slot_read->entered());
    // Whether the transport had been closed when the read threw, as far as the peer's
    // socket could tell: printed for the reader of the run, not asserted. The reading
    // taken after the peer's read has ended shows that the instrument can report a
    // closed transport; it is asserted where the instrument exists.
    std::printf(
        "[ MEASURED ] peer_hang_up_in_hook=%d peer_hang_up_after_hook_handler=%d "
        "peer_hang_up_after_read_ended=%d\n",
        o.peer_hang_up_in_hook ? 1 : 0, o.peer_hang_up_after_hook_handler ? 1 : 0,
        o.peer_hang_up_after_read_ended ? 1 : 0);
#if defined(__linux__)
    EXPECT_TRUE(o.peer_hang_up_after_read_ended)
        << "the hang-up reading is not 1 after the peer's read ended, so its two earlier "
           "readings say nothing";
#endif

    // Today: nothing on the path catches the exception. The arm records no state, so
    // the session is still Active when the handler returns, with the engine's stop
    // begun and not complete, and stays so until the stop's own close of the session.
    EXPECT_EQ(o.state_after_hook_handler, fsm_state::Active);
    EXPECT_TRUE(o.engine_stopped_after_hook_handler);
    EXPECT_FALSE(o.stop_ready_after_hook_handler);
    EXPECT_TRUE(o.stop_completed);
    EXPECT_EQ(o.state_after_stop, fsm_state::Disconnected);

    // The peer read the one frame replayed before the read, and nothing after it.
    auto const a = classify(o.after_rr);
    EXPECT_EQ(a.replayed_seqs, seqs(o.first, o.first)) << store_hold::printable(o.after_rr);
    EXPECT_EQ(a.gap_fills, 0U) << store_hold::printable(o.after_rr);
    EXPECT_EQ(a.other, 0U) << store_hold::printable(o.after_rr);
    EXPECT_TRUE(o.peer_read_ended);
}

// The same hook, and no stop: the gate above is told apart from its setup.
TEST(CancelledReadMeasuredPath, Measurement_Control_HookWithoutStop_ReadEntersTheStore) {
    auto const o = run_next_slot_arm(/*spawn_stop=*/false);
    ASSERT_TRUE(o.setup_miss.empty()) << o.setup_miss;

    ASSERT_TRUE(o.hook_ran) << "the path was not exercised: the walk did not read the slot "
                               "after the first one";
    ASSERT_TRUE(o.next_slot_read.has_value());
    EXPECT_TRUE(o.next_slot_read->entered() && o.next_slot_read->returned() &&
                !o.next_slot_read->error)
        << "without a stop the read must enter the store and return a frame. entered="
        << o.next_slot_read->entered() << " error=" << error_text(*o.next_slot_read);
    // The observation of the cell above is "Active with the engine's stop begun". Here
    // no stop has begun.
    EXPECT_FALSE(o.engine_stopped_in_hook);
    EXPECT_FALSE(o.engine_stopped_after_hook_handler);

    auto const a = classify(o.after_rr);
    EXPECT_EQ(a.replayed_seqs, seqs(o.first, o.last)) << store_hold::printable(o.after_rr);
    EXPECT_EQ(a.gap_fills, 0U) << store_hold::printable(o.after_rr);
    EXPECT_EQ(o.state_after_answer, fsm_state::Active);
    EXPECT_TRUE(o.stop_completed);
}

}  // namespace
