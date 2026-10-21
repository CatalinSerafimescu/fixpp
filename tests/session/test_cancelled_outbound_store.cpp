// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_cancelled_outbound_store.cpp
//
// Rulings R-15, R-16, R-19, R-20 and R-21 (.specify/536-537-538-recovery-liveness.md §10,
// D-OUT-1 to D-OUT-10; §5, the table "The cancelled outbound store (§10)"): an outbound
// frame whose store call was cancelled is not transmitted, its number is taken back, the
// session ends, and each entry point of Session returns a value and throws nothing, the
// same way on both timings.
//
// Every cell here drives a Session directly, with no Engine, or a bare store. The cells
// an Engine drives are in test_cancelled_outbound_store_engine.cpp and
// test_cancelled_outbound_store_measurement.cpp; the graceful Logout's are in
// test_logout_not_sent.cpp.
//
// Two timings of one cancellation at a store call:
//   returned  the store call is waiting for the FileStore's lock, behind an earlier
//             store call of the session that the cell holds inside the store's offload
//             hook, when the cell emits. The store returns store_cancelled.
//   thrown    the cancellation is recorded before the store's lock is reached: the cell
//             emits in the call expression of the store method (the recording wrapper's
//             call hook), or posts the emit from there so that it runs while the call
//             stands at the store's leading post.
// A cell shows its window from the wrapper's record before it asserts anything else; a
// missing record fails as "the path was not exercised".
//
// The suites:
//   DirectSend               D-OUT-9: Session::send awaited by the cell's own coroutine.
//   DiscardingCaller         D-OUT-2, D-OUT-4: an inbound arm whose first reply's leaf
//                            discards the emit's result.
//   LogonRoot                D-OUT-4, D-OUT-5: the initiator's Logon emission.
//   HydrationFlag            D-CAN-1 (R-20), in the second-call form.
//   SupersededReplyLogon     D-OUT-2: a superseded Logon arm whose reply's store is
//                            cancelled.
//   LivenessLoop             D-OUT-5: the loop's Heartbeat.
//   TeardownReset            D-OUT-10: a frame in its store call across a close's
//                            teardown reset.
//   StoreCallOrder           D-OUT-7 (R-21): the order in which a FileStore's store()
//                            calls return.

#include <gtest/gtest.h>

#include <algorithm>
#include <asio/awaitable.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/error.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/system_error.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <memory_resource>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "session/held_file_store_scene.hpp"
#include "session/logon_arm_fixtures.hpp"
#include "session/mock_reconnect_factory.hpp"
#include "session/plain_engine_rig.hpp"
#include "support/calling_thread_readings.hpp"
#include "support/keep_running.hpp"
#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"
#include "support/recording_store.hpp"
#include "support/session_test_access.hpp"
#include "support/temp_dir.hpp"

using namespace std::chrono_literals;

namespace {

namespace plain_rig = fixpp::test_support::plain_rig;
namespace store_hold = fixpp::test_support::store_hold;
using fixpp::core::error;
using fixpp::session::close_mode;
using fixpp::session::direction_t;
using fixpp::session::fsm_state;
using fixpp::session::seqnum_t;
using fixpp::session::session_test_access;
using fixpp::test_support::await_and_read_thread;
using fixpp::test_support::CallingThreadReadings;
using fixpp::test_support::keep_running;
using fixpp::test_support::keep_running_for;
using fixpp::test_support::MockReconnectFactory;
using fixpp::test_support::RecordingStoreFactory;
using fixpp::test_support::store_op;
using fixpp::test_support::StoreCall;
using fixpp::test_support::StoreCallLog;
using fixpp::test_support::logon_arm::DirectAcceptor;
using fixpp::test_support::logon_arm::ring_text;

// What a store call's record says, for a failure message.
std::string record_text(std::optional<StoreCall> const& c) {
    if (!c) return "not called";
    if (!c->entered()) return "called, and its body never started";
    if (!c->returned()) return "entered, and never returned";
    return c->error ? "returned error " + std::to_string(static_cast<int>(*c->error))
                    : std::string{"returned a value"};
}

bool is_outbound_store(StoreCall const& call) {
    return call.op == store_op::store && call.dir == direction_t::outbound;
}
bool is_first_hydration_read(StoreCall const& call) {
    return call.op == store_op::next_seqnum && call.dir == direction_t::inbound && !call.increment;
}
bool is_inbound_persist(StoreCall const& call) {
    return call.op == store_op::next_seqnum && call.dir == direction_t::inbound && call.increment;
}

// The session's next outbound number, in memory.
seqnum_t next_outbound(fixpp::session::Session& s) {
    return session_test_access::seqnum_mgr(s).peek_outbound();
}

std::size_t count_of(std::vector<fsm_state> const& ring, fsm_state st) {
    return static_cast<std::size_t>(std::count(ring.begin(), ring.end(), st));
}

// The states written after `from`'s entries, in `ring`.
std::vector<fsm_state> written_after(std::vector<fsm_state> const& from,
                                     std::vector<fsm_state> const& ring) {
    if (ring.size() < from.size()) return {};
    return {ring.begin() + static_cast<std::ptrdiff_t>(from.size()), ring.end()};
}

enum class Timing : std::uint8_t { returned, thrown };

// ── A directly driven acceptor on a FileStore, logged on ─────────────────────────────
//
// The store's offload hook is installed for the fixture's lifetime, so a cell can hold
// one store call of the session inside the FileStore, with the store's lock taken, as
// the cells under an Engine do (held_file_store_scene.hpp).
struct HeldDirectSession {
    // Declared first, destroyed last: the hook outlives the acceptor's file I/O pool.
    store_hold::scoped_hook hook;
    DirectAcceptor f{DirectAcceptor::Options{.file_store = true}};
    // Destroyed first: a hold still in flight is released before the acceptor drains
    // its io_context and joins its pool.
    store_hold::release_on_exit release_guard;
    std::vector<std::byte> payload = plain_rig::to_bytes(
        "35=D\x01"
        "11=ORD001\x01"
        "54=1\x01"
        "55=AAPL\x01");
    std::vector<std::byte> logon_bytes;

    // open(), the peer's Logon, Active; then the store log is armed.
    [[nodiscard]] bool to_active() {
        if (!f.open()) return false;
        logon_bytes = f.logon();
        CallingThreadReadings logon_call;
        if (!f.feed_plain(logon_bytes, logon_call)) return false;
        if (f.session->state() != fsm_state::Active) return false;
        f.log->arm();
        return true;
    }

    [[nodiscard]] std::future<fixpp::core::expected_t<void>> spawn_send() {
        return asio::co_spawn(f.ioc, f.session->send(std::span<const std::byte>{payload}),
                              asio::use_future);
    }

    // One Session::send, caught inside its store()'s offload. False when the hook did
    // not enter its hold.
    [[nodiscard]] bool hold_one_send(std::future<fixpp::core::expected_t<void>>& held) {
        store_hold::arm();
        held = spawn_send();
        return f.run_until([] { return store_hold::holding(); });
    }
};

// ── D-OUT-9, a directly driven send, both timings ────────────────────────────────────

struct DirectSendOutcome {
    bool active = false;
    bool hold_entered = false;  // returned timing only
    bool queued = false;        // returned timing: entered, not returned, hold in flight
    std::size_t cancel_mark = 0;
    std::size_t release_mark = 0;  // returned timing only
    seqnum_t seq = 0;              // the number of the send under test
    std::optional<StoreCall> store_call;
    CallingThreadReadings call;
    fsm_state state_after_call = fsm_state::NotConnected;
    bool hold_timed_out = false;

    // The window of the timing, from the wrapper's record.
    [[nodiscard]] bool exercised(Timing const timing) const {
        if (!active || !call.completed || !store_call.has_value() || cancel_mark == 0) {
            return false;
        }
        if (timing == Timing::thrown) {
            return store_call->entered() && cancel_mark > store_call->entered_at &&
                   !store_call->returned();
        }
        return hold_entered && queued && !hold_timed_out && store_call->entered_at < cancel_mark &&
               store_call->returned() && store_call->returned_at > cancel_mark &&
               store_call->returned_at < release_mark &&
               store_call->error == error::store_cancelled;
    }
    [[nodiscard]] std::string text() const {
        return "active=" + std::to_string(active) +
               " hold_entered=" + std::to_string(hold_entered) +
               " store_queued=" + std::to_string(queued) +
               " call_completed=" + std::to_string(call.completed) +
               " hold_timed_out=" + std::to_string(hold_timed_out) +
               " cancel_mark=" + std::to_string(cancel_mark) +
               " release_mark=" + std::to_string(release_mark) +
               " the store call: " + record_text(store_call);
    }
};

// Session::send awaited by the cell's coroutine, bound to a cancellation signal of the
// cell's. The gates are those of test_cancelled_outbound_store_measurement.cpp.
DirectSendOutcome run_direct_send(Timing const timing) {
    DirectSendOutcome o;
    HeldDirectSession h;
    auto& f = h.f;
    if (!h.to_active()) return o;
    o.active = true;

    std::future<fixpp::core::expected_t<void>> held_fut;
    if (timing == Timing::returned) {
        if (!h.hold_one_send(held_fut)) return o;
        o.hold_entered = true;
    }
    o.seq = next_outbound(*f.session);
    auto const mark = f.log->mark();
    auto store_call = [&] {
        return f.log->first_after(mark, [&](StoreCall const& call) {
            return is_outbound_store(call) && call.first == o.seq;
        });
    };
    asio::cancellation_signal cancel;
    if (timing == Timing::thrown) {
        // Posted from the call expression of store(): the post is queued ahead of the
        // one FileStore::store makes when its body starts, so the emit runs while the
        // store call stands at its leading post.
        f.log->on_call = [&, posted = false](StoreCall const& call) mutable {
            if (posted || !is_outbound_store(call) || call.first != o.seq) return;
            posted = true;
            asio::post(f.ioc, [&] {
                o.cancel_mark = f.log->mark();
                cancel.emit(asio::cancellation_type::terminal);
            });
        };
    }
    auto fut =
        asio::co_spawn(f.ioc,
                       await_and_read_thread(
                           [&] { return f.session->send(std::span<const std::byte>{h.payload}); },
                           o.call, [&] { o.state_after_call = f.session->state(); }),
                       asio::bind_cancellation_slot(cancel.slot(), asio::use_future));
    if (timing == Timing::returned) {
        // The gate, with no sleep: the store call's body has started; the handlers that
        // are ready then run until none is, which takes the call to the store's lock.
        if (f.run_until([&] { return store_call() != nullptr && store_call()->entered(); })) {
            f.ioc.poll();
            f.ioc.restart();
            o.queued = !store_call()->returned() && store_hold::holding();
        }
        if (o.queued) {
            o.cancel_mark = f.log->mark();
            cancel.emit(asio::cancellation_type::terminal);
            // The bound: the hold is released only below, so nothing but the cell's
            // cancellation can end the store call's wait before then.
            (void)f.run_until([&] { return store_call()->returned(); });
        }
        o.release_mark = f.log->mark();
        store_hold::release();
        (void)f.run_until([&] { return DirectAcceptor::ready(held_fut); });
    }
    (void)f.run_until([&] { return DirectAcceptor::ready(fut); });
    f.log->on_call = nullptr;
    if (auto const* c = store_call(); c != nullptr) o.store_call = *c;
    o.hold_timed_out = store_hold::timed_out();
    return o;
}

// What a caller of Session::send gets, and its thread after the call (D-OUT-1's table,
// the root `Session::send`; D-OUT-9).
void expect_direct_send_ending(DirectSendOutcome const& o) {
    EXPECT_FALSE(o.call.threw) << "Session::send must return a value; it " << o.call.call_text();
    EXPECT_TRUE(o.call.returned_error(error::dispatch_aborted))
        << "Session::send " << o.call.call_text();
    EXPECT_EQ(o.state_after_call, fsm_state::Disconnected) << "the state after the call";
    EXPECT_FALSE(o.call.cancellation_recorded)
        << "a cancellation is still recorded on the calling thread after the call";
    EXPECT_FALSE(o.call.next_await_threw)
        << "the caller's next co_await of an asio operation threw";
}

TEST(DirectSend, StoreCancelledAtTheStoresLock_ReturnsDispatchAbortedAndLeavesTheThreadClear) {
    auto const o = run_direct_send(Timing::returned);
    ASSERT_TRUE(o.exercised(Timing::returned)) << "the path was not exercised: " << o.text();
    expect_direct_send_ending(o);
}

TEST(DirectSend, StoreCancelledAtItsLeadingPost_ReturnsDispatchAbortedAndLeavesTheThreadClear) {
    auto const o = run_direct_send(Timing::thrown);
    ASSERT_TRUE(o.exercised(Timing::thrown)) << "the path was not exercised: " << o.text();
    expect_direct_send_ending(o);
}

// The pair: one cancellation of a send's store call, on its two timings. The readings a
// caller of Session::send can take must not tell the timings apart.
TEST(DirectSend, StoreCancelled_BothTimingsLeaveTheSameReadings) {
    auto const thrown = run_direct_send(Timing::thrown);
    ASSERT_TRUE(thrown.exercised(Timing::thrown))
        << "the path was not exercised, thrown timing: " << thrown.text();
    auto const returned = run_direct_send(Timing::returned);
    ASSERT_TRUE(returned.exercised(Timing::returned))
        << "the path was not exercised, returned timing: " << returned.text();

    // fixpp#583 questions what a lock sequence leaves on its caller's thread, which is
    // where the returned timing gets the last two readings. Each reading below compares
    // the two timings and names no absolute state: a change on one side alone fails it,
    // and D-OUT-9's reset must then change with it (note §8).
    EXPECT_EQ(thrown.call.call_text(), returned.call.call_text()) << "what the call returned";
    EXPECT_EQ(thrown.state_after_call, returned.state_after_call) << "the state after the call";
    EXPECT_EQ(thrown.call.cancellation_recorded, returned.call.cancellation_recorded)
        << "whether a cancellation is recorded on the calling thread after the call";
    EXPECT_EQ(thrown.call.next_await_threw, returned.call.next_await_threw)
        << "whether the caller's next co_await of an asio operation threw";
}

// ── D-OUT-2, D-OUT-4, a caller that discards the result, both timings ────────────────
//
// The inbound frame is a Heartbeat whose SendingTime(52) is outside the threshold: its
// arm answers with a Reject and then a Logout, through leaves that discard the emit's
// result. Its MsgSeqNum(34) is above the session's next inbound number, so the arm makes
// no store call ahead of the Reject's: an in-sequence frame would first persist its
// number, and that persist would queue at the FileStore's lock behind the held store.
// Re-derive: Session::consume_rejected_seqnum_, and SeqnumManager::check_inbound's
// branch for a number above the next one.

constexpr std::uint32_t kAboveNextInbound = 7;

struct DiscardOutcome {
    bool active = false;
    bool hold_entered = false;  // returned timing only
    bool queued = false;        // returned timing: the Reject's store stood at the lock
    bool hook_ran = false;      // thrown timing
    std::size_t cancel_mark = 0;
    std::size_t release_mark = 0;
    seqnum_t reject_seq = 0;  // the Reject's own number
    std::optional<StoreCall> reject_store;
    bool store_call_ahead_of_the_reject = false;  // the arm's inbound persist
    CallingThreadReadings call;
    fsm_state state_after_call = fsm_state::NotConnected;
    seqnum_t next_outbound_after_call = 0;
    std::vector<fsm_state> ring_before;
    std::vector<fsm_state> ring_at_end;
    // Read after the hold has been released and no handler is ready.
    std::size_t rejects_transmitted = 0;
    std::size_t logouts_transmitted = 0;
    bool hold_timed_out = false;

    [[nodiscard]] bool exercised(Timing const timing) const {
        if (!active || !call.completed || !reject_store.has_value() ||
            store_call_ahead_of_the_reject) {
            return false;
        }
        if (timing == Timing::thrown) return hook_ran && !reject_store->entered();
        return hold_entered && queued && !hold_timed_out &&
               reject_store->entered_at < cancel_mark && reject_store->returned() &&
               reject_store->returned_at > cancel_mark &&
               reject_store->returned_at < release_mark &&
               reject_store->error == error::store_cancelled;
    }
    [[nodiscard]] std::string text() const {
        return "active=" + std::to_string(active) +
               " hold_entered=" + std::to_string(hold_entered) +
               " reject_store_queued=" + std::to_string(queued) +
               " hook_ran=" + std::to_string(hook_ran) +
               " call_completed=" + std::to_string(call.completed) +
               " a_store_call_ahead_of_the_reject=" +
               std::to_string(store_call_ahead_of_the_reject) +
               " hold_timed_out=" + std::to_string(hold_timed_out) +
               " the Reject's store: " + record_text(reject_store);
    }
};

DiscardOutcome run_discarding_caller(Timing const timing) {
    DiscardOutcome o;
    HeldDirectSession h;
    auto& f = h.f;
    if (!h.to_active()) return o;
    o.active = true;

    std::future<fixpp::core::expected_t<void>> held_fut;
    if (timing == Timing::returned) {
        if (!h.hold_one_send(held_fut)) return o;
        o.hold_entered = true;
    }
    o.reject_seq = next_outbound(*f.session);
    o.ring_before = f.ring();
    auto const mark = f.log->mark();
    auto reject_store = [&] { return f.log->first_after(mark, is_outbound_store); };
    asio::cancellation_signal cancel;
    if (timing == Timing::thrown) {
        f.log->on_call = [&](StoreCall const& call) {
            if (o.hook_ran || !is_outbound_store(call)) return;
            o.hook_ran = true;
            // Terminal: the leaf's sequence-number lock has left the thread's filter
            // terminal, so a total emit would be dropped.
            o.cancel_mark = f.log->mark();
            cancel.emit(asio::cancellation_type::terminal);
        };
    }
    auto const stale = plain_rig::to_bytes(plain_rig::message(
        f.cfg.begin_string, "0", kAboveNextInbound, "TW", "ISLD", "20230101-00:00:00.000"));
    auto fut = f.feed(stale, o.call, cancel, [&] {
        o.state_after_call = f.session->state();
        o.next_outbound_after_call = next_outbound(*f.session);
    });
    if (timing == Timing::returned) {
        if (f.run_until([&] { return reject_store() != nullptr && reject_store()->entered(); })) {
            f.ioc.poll();
            f.ioc.restart();
            o.queued = !reject_store()->returned() && store_hold::holding();
        }
        if (o.queued) {
            o.cancel_mark = f.log->mark();
            cancel.emit(asio::cancellation_type::terminal);
            // The bound: the hold is released only below, so nothing but the cell's
            // cancellation can end the wait of the Reject's store before then.
            (void)f.run_until([&] { return reject_store()->returned(); });
        }
        if (auto const* c = reject_store(); c != nullptr) o.reject_store = *c;
        o.release_mark = f.log->mark();
        store_hold::release();
        (void)f.run_until([&] { return DirectAcceptor::ready(held_fut); });
    }
    (void)f.run_until([&] { return DirectAcceptor::ready(fut); });
    f.log->on_call = nullptr;
    if (!o.reject_store) {
        if (auto const* c = reject_store(); c != nullptr) o.reject_store = *c;
    }
    o.store_call_ahead_of_the_reject = f.log->first_after(mark, is_inbound_persist) != nullptr;
    // The hold is released and the call has returned: whatever the session still had
    // ready runs here, before the transport callback's record is read.
    f.ioc.poll();
    f.ioc.restart();
    o.rejects_transmitted = f.sent_count("3");
    o.logouts_transmitted = f.sent_count("5");
    o.ring_at_end = f.ring();
    o.hold_timed_out = store_hold::timed_out();
    return o;
}

void expect_discarding_caller_ending(DiscardOutcome const& o) {
    EXPECT_EQ(o.rejects_transmitted, 0U) << "the Reject was transmitted";
    // D-OUT-1's row "callbacks and events the arm orders after the reply": the arm does
    // not go on to its next reply.
    EXPECT_EQ(o.logouts_transmitted, 0U) << "a Logout was transmitted after the Reject's store "
                                            "was cancelled";
    EXPECT_FALSE(o.call.threw) << "on_inbound_frame must return a value; it " << o.call.call_text();
    EXPECT_TRUE(o.call.returned_error(error::store_cancelled))
        << "on_inbound_frame " << o.call.call_text();
    EXPECT_EQ(o.state_after_call, fsm_state::Disconnected) << "the state after the call";
    EXPECT_EQ(o.next_outbound_after_call, o.reject_seq)
        << "the next outbound number in memory is not the Reject's own";
    EXPECT_FALSE(o.call.cancellation_recorded)
        << "a cancellation is still recorded on the calling thread after the call";
    auto const written = written_after(o.ring_before, o.ring_at_end);
    EXPECT_EQ(count_of(written, fsm_state::Disconnected), 1U)
        << "states written since the frame was handed over: " << ring_text(written);
}

TEST(DiscardingCaller, RejectsStoreCancelledAtTheStoresLock_EndsTheArmAndReturnsStoreCancelled) {
    auto const o = run_discarding_caller(Timing::returned);
    ASSERT_TRUE(o.exercised(Timing::returned)) << "the path was not exercised: " << o.text();
    expect_discarding_caller_ending(o);
}

TEST(DiscardingCaller, RejectsStoreCancelledBeforeItsAwait_EndsTheArmAndReturnsStoreCancelled) {
    auto const o = run_discarding_caller(Timing::thrown);
    ASSERT_TRUE(o.exercised(Timing::thrown)) << "the path was not exercised: " << o.text();
    expect_discarding_caller_ending(o);
}

// ── A directly driven initiator on a persistent store ────────────────────────────────
//
// Session::open() emits the Logon itself (the session is not engine-managed), through
// the transport callback. Session::drive_reconnect() connects through a
// MockReconnectFactory and emits the Logon over the mock transport it made. The store is
// a FileStore behind the recording wrapper. HeartBtInt is zero and the clock is a mock
// clock that nothing advances, so no timer of the session can end a wait of a cell.
class DirectInitiator {
public:
    struct Options {
        bool reset_on_logon = false;
    };

    // First, so destroyed last: the session's executor is on it.
    asio::io_context ioc;

private:
    // Keeps `ioc` from stopping between two pumps when the FileStore's offload work is
    // released on the pool thread (tests/support/keep_running.hpp). Released first in the
    // destructor, before the drain, which waits for the io_context to stop.
    keep_running keep_{keep_running_for(ioc)};
    asio::thread_pool file_pool_{1};
    struct temp_dir {
        std::filesystem::path path = fixpp::test_support::unique_temp_dir("b28_logon_root");
        temp_dir() = default;
        ~temp_dir() { (void)fixpp::test_support::try_remove_temp_dir(path); }
        temp_dir(temp_dir const&) = delete;
        temp_dir& operator=(temp_dir const&) = delete;
    } dir_;

public:
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;
    fixpp::session::SessionConfig cfg;
    std::shared_ptr<StoreCallLog> log;
    std::shared_ptr<MockReconnectFactory> transport_factory =
        std::make_shared<MockReconnectFactory>();
    std::vector<std::string> sent;  // each frame the session gave its transport callback
    std::unique_ptr<fixpp::session::Session> session;

    explicit DirectInitiator(Options const& o) {
        using namespace std::chrono;
        clock = std::make_shared<fixpp::core::mock_clock>(
            system_clock::time_point{} + seconds{1704067200}, fixpp::core::steady_time_point{},
            ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();

        cfg.role = fixpp::session::session_role::initiator;
        cfg.sender_comp_id = "TW";
        cfg.target_comp_id = "ISLD";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = seconds{0};
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        cfg.reset_on_logon = o.reset_on_logon;
        cfg.transport_factory_override = transport_factory;
        cfg.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 19099};
        cfg.transport_send = [this](std::span<const std::byte> f) {
            sent.emplace_back(reinterpret_cast<char const*>(f.data()), f.size());
        };
        fixpp::session::FileStore::Config file_cfg;
        file_cfg.directory = dir_.path;
        file_cfg.sender_comp_id = cfg.sender_comp_id;
        file_cfg.target_comp_id = cfg.target_comp_id;
        file_cfg.file_io_executor = file_pool_.get_executor();
        auto factory = std::make_shared<RecordingStoreFactory>(file_cfg);
        log = factory->log;
        cfg.store_factory = std::move(factory);
        session = std::make_unique<fixpp::session::Session>(engine, cfg);
    }

    DirectInitiator(DirectInitiator const&) = delete;
    DirectInitiator& operator=(DirectInitiator const&) = delete;

    // The body runs while every member is alive: the session is closed, and whatever is
    // still suspended on the io_context is resumed here, over live storage.
    // cancel_and_drain_or_report carries its own catch.
    ~DirectInitiator() {
        keep_.reset();
        try {
            log->on_call = nullptr;
            asio::co_spawn(ioc, session->close(close_mode::terminal), asio::detached);
        } catch (...) {  // NOLINT(bugprone-empty-catch) — the drain below still runs.
        }
        fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, "DirectInitiator");
    }

    template <class Fut>
    [[nodiscard]] static bool ready(Fut const& fut) {
        return fut.wait_for(std::chrono::seconds{0}) == std::future_status::ready;
    }
    template <class Pred>
    [[nodiscard]] bool run_until(Pred pred) {
        return fixpp::test_support::pump_until(ioc, std::move(pred),
                                               fixpp::test_support::kPumpBudget,
                                               fixpp::test_support::kPumpSlice, nullptr);
    }

    // Awaits `make_call()` from a coroutine of the cell's, bound to `cancel`.
    [[nodiscard]] std::future<void> call(
        std::function<asio::awaitable<fixpp::core::expected_t<void>>()> make_call,
        CallingThreadReadings& out, asio::cancellation_signal& cancel,
        std::function<void()> after_call = {}) {
        return asio::co_spawn(
            ioc, await_and_read_thread(std::move(make_call), out, std::move(after_call)),
            asio::bind_cancellation_slot(cancel.slot(), asio::use_future));
    }

    // How many Logons the session has handed to a transport: its callback, and the mock
    // transport of the last drive_reconnect().
    [[nodiscard]] std::size_t logons_transmitted() const {
        std::size_t n = 0;
        for (auto const& f : sent) {
            n += static_cast<std::size_t>(plain_rig::field_value(f, "35") == "A");
        }
        if (auto* const t = transport_factory->last_transport; t != nullptr) {
            auto const bytes = t->outbound_bytes_seen();
            n += plain_rig::frames_of_type(
                     std::string_view{reinterpret_cast<char const*>(bytes.data()), bytes.size()},
                     "A")
                     .size();
        }
        return n;
    }

    [[nodiscard]] std::vector<fsm_state> ring() const {
        auto const r = session->fsm_visit_history();
        return {r.begin(), r.end()};
    }
};

// ── D-OUT-4 and D-OUT-5: the Logon root ──────────────────────────────────────────────

enum class LogonRootInput : std::uint8_t {
    // D-OUT-4, every root: the wrapper returns store_cancelled for the Logon's store,
    // and nothing is emitted.
    logon_store_returns_cancelled,
    // D-OUT-5, a cancellation thrown at each store call of emit_initiator_logon_: the
    // cell's terminal cancellation, emitted in the call expression.
    hydration_read_cancelled,
    reset_cancelled,  // the store's reset(), with reset_on_logon set
    logon_store_cancelled,
    // The same as logon_store_cancelled, with the emission reached through
    // Session::drive_reconnect in place of Session::open.
    logon_store_cancelled_through_drive_reconnect,
    // The returned twin: the wrapper returns store_cancelled from the first hydration
    // read, and nothing is emitted.
    hydration_read_returns_cancelled,
    // An exception that is not a cancellation: the first hydration read's body throws.
    hydration_read_throws,
};

struct LogonRootOutcome {
    bool ready = false;               // the fixture reached the call under test
    bool hook_ran = false;            // thrown inputs
    std::optional<StoreCall> target;  // the store call the input aims at
    CallingThreadReadings call;
    fsm_state state_after_call = fsm_state::NotConnected;
    seqnum_t next_outbound_after_call = 0;
    std::vector<fsm_state> ring_before;
    std::vector<fsm_state> ring_after_call;
    std::size_t logons_transmitted = 0;  // by the call under test
};

bool is_store_reset(StoreCall const& call) { return call.op == store_op::reset; }

LogonRootOutcome run_logon_root(LogonRootInput const input) {
    using enum LogonRootInput;
    LogonRootOutcome o;
    DirectInitiator f{DirectInitiator::Options{.reset_on_logon = input == reset_cancelled}};
    const bool through_reconnect = input == logon_store_cancelled_through_drive_reconnect;
    if (through_reconnect) {
        // The first Logon, through open(), with nothing injected.
        auto opened = asio::co_spawn(f.ioc, f.session->open(), asio::use_future);
        if (!f.run_until([&] { return DirectInitiator::ready(opened); })) return o;
        if (!opened.get().has_value() || f.session->state() != fsm_state::LogonSent) return o;
    }
    o.ready = true;

    std::function<bool(StoreCall const&)> target;
    switch (input) {
        case hydration_read_cancelled:
        case hydration_read_returns_cancelled:
        case hydration_read_throws:
            target = is_first_hydration_read;
            break;
        case reset_cancelled:
            target = is_store_reset;
            break;
        case logon_store_returns_cancelled:
        case logon_store_cancelled:
        case logon_store_cancelled_through_drive_reconnect:
            target = is_outbound_store;
            break;
    }
    f.log->arm();
    asio::cancellation_signal cancel;
    switch (input) {
        case hydration_read_cancelled:
        case reset_cancelled:
        case logon_store_cancelled:
        case logon_store_cancelled_through_drive_reconnect:
            f.log->on_call = [&](StoreCall const& c) {
                if (o.hook_ran || !target(c)) return;
                o.hook_ran = true;
                // Terminal: it passes whatever filter the awaits before this call left.
                cancel.emit(asio::cancellation_type::terminal);
            };
            break;
        case logon_store_returns_cancelled:
        case hydration_read_returns_cancelled:
            f.log->fail_in = target;
            f.log->fail_code = error::store_cancelled;
            break;
        case hydration_read_throws:
            f.log->throw_in = target;
            break;
    }
    o.ring_before = f.ring();
    auto const logons_before = f.logons_transmitted();
    auto const mark = f.log->mark();
    auto fut =
        f.call([&] { return through_reconnect ? f.session->drive_reconnect() : f.session->open(); },
               o.call, cancel,
               [&] {
                   o.state_after_call = f.session->state();
                   o.next_outbound_after_call = next_outbound(*f.session);
                   o.ring_after_call = f.ring();
               });
    (void)f.run_until([&] { return DirectInitiator::ready(fut); });
    f.log->on_call = nullptr;
    if (auto const* c = f.log->first_after(mark, target); c != nullptr) o.target = *c;
    o.logons_transmitted = f.logons_transmitted() - logons_before;
    return o;
}

std::string logon_root_text(LogonRootOutcome const& o) {
    return "fixture_ready=" + std::to_string(o.ready) + " hook_ran=" + std::to_string(o.hook_ran) +
           " call_completed=" + std::to_string(o.call.completed) +
           " the store call: " + record_text(o.target);
}

// The window of a thrown input: the hook ran, and the call's body never started.
::testing::AssertionResult thrown_window(LogonRootOutcome const& o) {
    if (o.ready && o.hook_ran && o.call.completed && o.target.has_value() && !o.target->entered()) {
        return ::testing::AssertionSuccess();
    }
    return ::testing::AssertionFailure() << "the path was not exercised: " << logon_root_text(o);
}

// What a thrown input asserts (§5, "D-OUT-5, the Logon root"). `entry` names the entry
// point for the messages.
void expect_logon_root_cancelled(LogonRootOutcome const& o, char const* entry) {
    EXPECT_FALSE(o.call.threw) << entry << " must return a value; it " << o.call.call_text();
    EXPECT_TRUE(o.call.returned_error(error::store_cancelled))
        << entry << " " << o.call.call_text();
    EXPECT_EQ(o.state_after_call, fsm_state::Disconnected) << "the state after the call";
    EXPECT_EQ(o.logons_transmitted, 0U) << "a Logon was transmitted";
    EXPECT_FALSE(o.call.cancellation_recorded)
        << "a cancellation is still recorded on the calling thread after the call";
    EXPECT_FALSE(o.call.next_await_threw)
        << "the caller's next co_await of an asio operation threw";
    auto const written = written_after(o.ring_before, o.ring_after_call);
    EXPECT_EQ(count_of(written, fsm_state::Disconnected), 1U)
        << "states written by the call: " << ring_text(written);
}

// D-OUT-4, every root, the initiator's Logon: a store that returns store_cancelled for
// the Logon, with no cancellation emitted.
TEST(LogonRoot, LogonsStoreReturnsStoreCancelled_OpenReturnsStoreCancelledAndSendsNoLogon) {
    auto const o = run_logon_root(LogonRootInput::logon_store_returns_cancelled);
    ASSERT_TRUE(o.ready && o.call.completed && o.target.has_value() && o.target->returned() &&
                o.target->error == error::store_cancelled)
        << "the path was not exercised: " << logon_root_text(o);

    EXPECT_FALSE(o.call.threw) << "open must return a value; it " << o.call.call_text();
    EXPECT_TRUE(o.call.returned_error(error::store_cancelled)) << "open " << o.call.call_text();
    EXPECT_EQ(o.state_after_call, fsm_state::Disconnected) << "the state after the call";
    EXPECT_EQ(o.logons_transmitted, 0U) << "the Logon was transmitted";
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the ASSERT above
    EXPECT_EQ(o.next_outbound_after_call, o.target->first)
        << "the next outbound number in memory is not the Logon's own";
}

TEST(LogonRoot, HydrationReadCancelledBeforeItsAwait_OpenReturnsStoreCancelled) {
    auto const o = run_logon_root(LogonRootInput::hydration_read_cancelled);
    ASSERT_TRUE(thrown_window(o));
    expect_logon_root_cancelled(o, "open");
}

TEST(LogonRoot, StoreResetCancelledBeforeItsAwait_OpenReturnsStoreCancelled) {
    auto const o = run_logon_root(LogonRootInput::reset_cancelled);
    ASSERT_TRUE(thrown_window(o));
    expect_logon_root_cancelled(o, "open");
}

TEST(LogonRoot, LogonsStoreCancelledBeforeItsAwait_OpenReturnsStoreCancelled) {
    auto const o = run_logon_root(LogonRootInput::logon_store_cancelled);
    ASSERT_TRUE(thrown_window(o));
    expect_logon_root_cancelled(o, "open");
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): thrown_window() holds
    EXPECT_EQ(o.next_outbound_after_call, o.target->first)
        << "the next outbound number in memory is not the Logon's own";
}

// The same store call, with the emission reached through Session::drive_reconnect.
TEST(LogonRoot, LogonsStoreCancelledBeforeItsAwait_DriveReconnectReturnsStoreCancelled) {
    auto const o = run_logon_root(LogonRootInput::logon_store_cancelled_through_drive_reconnect);
    ASSERT_TRUE(thrown_window(o));
    expect_logon_root_cancelled(o, "drive_reconnect");
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): thrown_window() holds
    EXPECT_EQ(o.next_outbound_after_call, o.target->first)
        << "the next outbound number in memory is not the Logon's own";
}

// The returned twin, a control for the code: the store reports the cancelled read as
// store_cancelled, and nothing is emitted.
TEST(LogonRoot, Control_HydrationReadReturnsStoreCancelled_OpenReturnsStoreCancelled) {
    auto const o = run_logon_root(LogonRootInput::hydration_read_returns_cancelled);
    ASSERT_TRUE(o.ready && o.call.completed && o.target.has_value() && o.target->returned() &&
                o.target->error == error::store_cancelled)
        << "the path was not exercised: " << logon_root_text(o);

    EXPECT_TRUE(o.call.returned_error(error::store_cancelled)) << "open " << o.call.call_text();
    EXPECT_EQ(o.state_after_call, fsm_state::Disconnected) << "the state after the call";
}

// An exception that is not a cancellation.
TEST(LogonRoot, HydrationReadThrowsAnotherException_OpenReturnsDispatchAborted) {
    auto const o = run_logon_root(LogonRootInput::hydration_read_throws);
    ASSERT_TRUE(o.ready && o.call.completed && o.target.has_value() && o.target->entered() &&
                !o.target->returned())
        << "the path was not exercised: the read's body must start and not return; "
        << logon_root_text(o);

    EXPECT_FALSE(o.call.threw) << "open must return a value; it " << o.call.call_text();
    EXPECT_TRUE(o.call.returned_error(error::dispatch_aborted)) << "open " << o.call.call_text();
    EXPECT_EQ(o.state_after_call, fsm_state::Disconnected) << "the state after the call";
}

// ── D-CAN-1 (R-20), the hydration flag, in the second-call form ──────────────────────
//
// First call, open(): the cell's terminal cancellation is emitted in the call expression
// of the first hydration read. Second call, drive_reconnect(), with nothing emitted: it
// reaches emit_initiator_logon_ again, and its hydration must read the store. With the
// re-entrancy flag left set by the first call, ensure_hydrated_ would return before it
// reads.
TEST(HydrationFlag, AfterACancelledHydrationRead_TheNextLogonEmissionReadsTheStore) {
    DirectInitiator f{DirectInitiator::Options{}};
    f.log->arm();
    asio::cancellation_signal cancel;
    bool hook_ran = false;
    f.log->on_call = [&](StoreCall const& c) {
        if (hook_ran || !is_first_hydration_read(c)) return;
        hook_ran = true;
        cancel.emit(asio::cancellation_type::terminal);
    };
    CallingThreadReadings first;
    auto const mark1 = f.log->mark();
    auto fut1 = f.call([&] { return f.session->open(); }, first, cancel);
    bool const first_done = f.run_until([&] { return DirectInitiator::ready(fut1); });
    f.log->on_call = nullptr;
    std::optional<StoreCall> first_read;
    if (auto const* r = f.log->first_after(mark1, is_first_hydration_read)) first_read = *r;

    CallingThreadReadings second;
    asio::cancellation_signal none;
    auto const mark2 = f.log->mark();
    auto fut2 = f.call([&] { return f.session->drive_reconnect(); }, second, none);
    bool const second_done = f.run_until([&] { return DirectInitiator::ready(fut2); });
    std::optional<StoreCall> inbound_read;
    std::optional<StoreCall> outbound_read;
    if (auto const* r = f.log->first_after(mark2, is_first_hydration_read)) inbound_read = *r;
    if (auto const* r = f.log->first_after(mark2, [](StoreCall const& c) {
            return c.op == store_op::next_seqnum && c.dir == direction_t::outbound && !c.increment;
        })) {
        outbound_read = *r;
    }

    ASSERT_TRUE(hook_ran && first_done && first.completed && first_read.has_value() &&
                !first_read->entered())
        << "the path was not exercised: hook_ran=" << hook_ran << " first_call_done=" << first_done
        << " the first call's read: " << record_text(first_read);
    ASSERT_TRUE(second_done && second.completed)
        << "the path was not exercised: the second call did not complete";

    EXPECT_FALSE(first.threw) << "open must return a value; it " << first.call_text();
    EXPECT_TRUE(first.returned_error(error::store_cancelled)) << "open " << first.call_text();
    EXPECT_TRUE(inbound_read.has_value() && inbound_read->entered())
        << "the second call's inbound hydration read: " << record_text(inbound_read)
        << " (drive_reconnect " << second.call_text() << ")";
    EXPECT_TRUE(outbound_read.has_value() && outbound_read->entered())
        << "the second call's outbound hydration read: " << record_text(outbound_read)
        << " (drive_reconnect " << second.call_text() << ")";
}

// ── D-OUT-2, a superseded Logon arm whose reply's store is cancelled ─────────────────
//
// A directly driven acceptor on the closing leg (logon_arm_fixtures.hpp). In the call
// expression of the reply Logon's store the cell reads the state ring, spawns the
// driver's close(), which runs to its first suspension before the hook returns, and then
// either emits its cancellation (thrown) or lets the wrapper return store_cancelled for
// that call (returned). close(graceful) is the close used: begun in LogonReceived, its
// phase 1 records LogoutSent itself, so a state the arm recorded shows in the ring.

struct SupersededReplyOutcome {
    bool opened = false;
    bool hook_ran = false;
    bool closing_in_hook = false;
    std::optional<StoreCall> reply_store;
    CallingThreadReadings call;
    std::vector<fsm_state> ring_after_spawn;  // the close has run to its first suspension
    std::vector<fsm_state> ring_after_call;   // when on_inbound_frame has returned
    seqnum_t next_outbound_after_call = 0;
    std::size_t reply_logons_after_call = 0;
    bool close_returned = false;

    [[nodiscard]] bool exercised(Timing const timing) const {
        if (!opened || !hook_ran || !closing_in_hook || !call.completed ||
            !reply_store.has_value()) {
            return false;
        }
        if (timing == Timing::thrown) return !reply_store->entered();
        return reply_store->returned() && reply_store->error == error::store_cancelled;
    }
    [[nodiscard]] std::string text() const {
        return "opened=" + std::to_string(opened) + " hook_ran=" + std::to_string(hook_ran) +
               " closing_in_hook=" + std::to_string(closing_in_hook) +
               " call_completed=" + std::to_string(call.completed) +
               " the reply Logon's store: " + record_text(reply_store);
    }
};

SupersededReplyOutcome run_superseded_reply(Timing const timing) {
    SupersededReplyOutcome o;
    DirectAcceptor f{DirectAcceptor::Options{}};
    if (!f.open()) return o;
    o.opened = true;

    f.log->arm();
    asio::cancellation_signal cancel;
    std::future<fixpp::core::expected_t<void>> close_fut;
    // The close spawned below makes an outbound store() call of its own, the Logout's,
    // so the reply Logon's is chosen by its number.
    seqnum_t reply_seq = 0;
    auto is_reply_logons_store = [&](StoreCall const& call) {
        return reply_seq != 0 && is_outbound_store(call) && call.first == reply_seq;
    };
    f.log->on_call = [&](StoreCall const& call) {
        if (o.hook_ran || !is_outbound_store(call)) return;
        o.hook_ran = true;
        reply_seq = call.first;
        close_fut = f.spawn_close(close_mode::graceful);
        o.closing_in_hook = session_test_access::is_closing(*f.session);
        o.ring_after_spawn = f.ring();
        // Terminal: the leaf's sequence-number lock has left the thread's filter
        // terminal, so a total emit would be dropped.
        if (timing == Timing::thrown) cancel.emit(asio::cancellation_type::terminal);
    };
    if (timing == Timing::returned) {
        f.log->fail_in = is_reply_logons_store;
        f.log->fail_code = error::store_cancelled;
    }
    auto const mark = f.log->mark();
    auto const logon = f.logon();
    auto fut = f.feed(logon, o.call, cancel, [&] {
        o.ring_after_call = f.ring();
        o.next_outbound_after_call = next_outbound(*f.session);
        o.reply_logons_after_call = f.sent_count("A");
    });
    (void)f.run_until([&] { return DirectAcceptor::ready(fut); });
    f.log->on_call = nullptr;
    if (auto const* c = f.log->first_after(mark, is_reply_logons_store); c != nullptr) {
        o.reply_store = *c;
    }
    // The bound: close(graceful)'s grace wait is the one real-time timer here, and it is
    // the fixture's logout_disconnect_timeout_ms, far below the pump's budget.
    if (close_fut.valid()) {
        o.close_returned = f.run_until([&] { return DirectAcceptor::ready(close_fut); });
    }
    return o;
}

void expect_superseded_reply_ending(SupersededReplyOutcome const& o) {
    EXPECT_FALSE(o.call.threw) << "on_inbound_frame must return a value; it " << o.call.call_text();
    EXPECT_TRUE(o.call.returned_success()) << "on_inbound_frame " << o.call.call_text();
    // The arm wrote no state: when the call has returned, the ring holds what the close
    // had written when the hook returned, and nothing more.
    EXPECT_EQ(o.ring_after_call, o.ring_after_spawn)
        << "ring when the call returned: " << ring_text(o.ring_after_call)
        << "; ring when the hook returned: " << ring_text(o.ring_after_spawn);
    EXPECT_EQ(o.reply_logons_after_call, 0U) << "the reply Logon was transmitted";
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): exercised() holds
    EXPECT_EQ(o.next_outbound_after_call, o.reply_store->first)
        << "the next outbound number in memory is not the reply Logon's own";
    EXPECT_TRUE(o.close_returned) << "the driver's close() did not return";
}

TEST(SupersededReplyLogon, StoreCancelledBeforeItsAwait_ReturnsSuccessAndTakesTheNumberBack) {
    auto const o = run_superseded_reply(Timing::thrown);
    ASSERT_TRUE(o.exercised(Timing::thrown)) << "the path was not exercised: " << o.text();
    expect_superseded_reply_ending(o);
}

TEST(SupersededReplyLogon, StoreReturnsStoreCancelled_ReturnsSuccessAndTakesTheNumberBack) {
    auto const o = run_superseded_reply(Timing::returned);
    ASSERT_TRUE(o.exercised(Timing::returned)) << "the path was not exercised: " << o.text();
    expect_superseded_reply_ending(o);
}

// ── D-OUT-5, the liveness loop ───────────────────────────────────────────────────────
//
// A directly driven acceptor on the callback transport and a mock clock, with HeartBtInt
// 30 s, so that "not transmitted" is read from the transport callback's record. The
// wrapper returns store_cancelled for the store() of the loop's Heartbeat, which is the
// next outbound number once the session is Active.
TEST(LivenessLoop, HeartbeatsStoreReturnsStoreCancelled_EndsTheSessionAndSendsNothing) {
    constexpr auto kHeartBtInt = std::chrono::seconds{30};
    DirectAcceptor f{DirectAcceptor::Options{.tweak_cfg = [&](fixpp::session::SessionConfig& c) {
        c.heartbeat_interval = kHeartBtInt;
    }}};
    ASSERT_TRUE(f.open());
    auto const logon = f.msg("A", 1,
                             "98=0\x01"
                             "108=30\x01");
    CallingThreadReadings logon_call;
    ASSERT_TRUE(f.feed_plain(logon, logon_call) && f.session->state() == fsm_state::Active)
        << "setup: the session did not reach Active";

    auto const heartbeat_seq = next_outbound(*f.session);
    auto is_heartbeats_store = [heartbeat_seq](StoreCall const& c) {
        return is_outbound_store(c) && c.first == heartbeat_seq;
    };
    f.log->arm();
    f.log->fail_in = is_heartbeats_store;
    f.log->fail_code = error::store_cancelled;
    auto const mark = f.log->mark();
    f.sent.clear();

    // Inbound traffic at 20 s keeps the inbound side fresh, so at HeartBtInt the loop
    // owes a Heartbeat and no TestRequest.
    // Staging (ci/mock-clock-staging-sweep.sh): KIND A. This advance is a time stamp:
    // what reads it is the delivery below, on this thread, which stamps the frame's
    // SendingTime and the last inbound instant from the clock. The loop's sleep is not
    // due before HeartBtInt, so no waiter depends on it.
    f.clock->advance(20s);
    auto const peer_heartbeat = f.msg("0", 2);
    CallingThreadReadings heartbeat_call;
    ASSERT_TRUE(f.feed_plain(peer_heartbeat, heartbeat_call) && heartbeat_call.returned_success())
        << "setup: the peer's Heartbeat was not accepted";
    // Staging: KIND D. The sleep this advance fires was armed from the Logon's anchors,
    // which predate both advances.
    f.clock->advance(11s);
    bool const store_returned = f.run_until([&] {
        auto const* c = f.log->first_after(mark, is_heartbeats_store);
        return c != nullptr && c->returned();
    });
    std::optional<StoreCall> heartbeats_store;
    if (auto const* c = f.log->first_after(mark, is_heartbeats_store)) heartbeats_store = *c;
    f.ioc.poll();
    f.ioc.restart();
    ASSERT_TRUE(store_returned && heartbeats_store.has_value() &&
                heartbeats_store->error == error::store_cancelled)
        << "the path was not exercised: the Heartbeat's store: " << record_text(heartbeats_store);

    EXPECT_EQ(f.sent_count("0"), 0U) << "the Heartbeat was transmitted";
    EXPECT_EQ(f.session->state(), fsm_state::Disconnected);
    EXPECT_EQ(next_outbound(*f.session), heartbeat_seq)
        << "the next outbound number in memory is not the Heartbeat's own";

    // Further intervals draw no frame: the loop has ended.
    for (int i = 0; i < 2; ++i) {
        // Staging: KIND C. This advance must fire nothing, and that is the oracle of
        // the assertion below; a liveness loop still running would write a frame.
        f.clock->advance(kHeartBtInt + 1s);
        f.ioc.poll();
        f.ioc.restart();
    }
    EXPECT_TRUE(f.sent.empty()) << "the session wrote a frame after its Heartbeat's store was "
                                   "cancelled: "
                                << f.sent.size() << " frame(s)";

    auto closed = f.spawn_close(close_mode::terminal);
    EXPECT_TRUE(f.run_until([&] { return DirectAcceptor::ready(closed); }))
        << "teardown: close(terminal) did not complete";
}

// ── D-OUT-10, a frame in its store call across a close's teardown reset ──────────────
//
// A directly driven acceptor with a transport callback, a MemoryStore behind the
// recording wrapper, reset_on_disconnect set and HeartBtInt zero. The window of both
// cells is read from the wrapper: the session's next outbound number is 1 when the store
// call under test returns. A run in which it is not fails as "the path was not
// exercised".

DirectAcceptor::Options reset_on_disconnect_options() {
    return DirectAcceptor::Options{
        .tweak_cfg = [](fixpp::session::SessionConfig& c) { c.reset_on_disconnect = true; }};
}

// The cell spawns send and then close(terminal). send runs to the store's leading post;
// close has no live transport and no liveness loop to wait for, so it reaches the
// teardown reset's counter write without a suspension and suspends in the store's
// reset(). The send's store call then returns.
TEST(TeardownReset, SendInItsStoreCallAcrossTheReset_IsNotHandedToTheSink) {
    DirectAcceptor f{reset_on_disconnect_options()};
    ASSERT_TRUE(f.open());
    auto const logon = f.logon();
    CallingThreadReadings logon_call;
    ASSERT_TRUE(f.feed_plain(logon, logon_call) && f.session->state() == fsm_state::Active)
        << "setup: the session did not reach Active";

    auto const payload = plain_rig::to_bytes(
        "35=D\x01"
        "11=ORD001\x01"
        "54=1\x01"
        "55=AAPL\x01");
    auto const send_seq = next_outbound(*f.session);
    auto is_sends_store = [send_seq](StoreCall const& c) {
        return is_outbound_store(c) && c.first == send_seq;
    };
    f.log->arm();
    std::optional<seqnum_t> next_outbound_when_the_store_returned;
    bool closing_when_the_store_returned = false;
    f.log->on_return = [&](StoreCall const& c) {
        if (next_outbound_when_the_store_returned || !is_sends_store(c)) return;
        next_outbound_when_the_store_returned = next_outbound(*f.session);
        closing_when_the_store_returned = session_test_access::is_closing(*f.session);
    };
    auto const mark = f.log->mark();
    f.sent.clear();

    CallingThreadReadings send_call;
    asio::cancellation_signal none;
    auto send_fut = asio::co_spawn(
        f.ioc,
        await_and_read_thread([&] { return f.session->send(std::span<const std::byte>{payload}); },
                              send_call),
        asio::bind_cancellation_slot(none.slot(), asio::use_future));
    auto close_fut = f.spawn_close(close_mode::terminal);
    bool const send_done = f.run_until([&] { return DirectAcceptor::ready(send_fut); });
    bool const close_done = f.run_until([&] { return DirectAcceptor::ready(close_fut); });
    f.log->on_return = nullptr;
    std::optional<StoreCall> sends_store;
    if (auto const* c = f.log->first_after(mark, is_sends_store)) sends_store = *c;

    ASSERT_TRUE(send_done && send_call.completed && sends_store.has_value() &&
                sends_store->returned() &&
                next_outbound_when_the_store_returned == std::optional<seqnum_t>{1} &&
                closing_when_the_store_returned)
        << "the path was not exercised: send_done=" << send_done
        << " the send's store: " << record_text(sends_store) << " next_outbound_when_it_returned="
        << (next_outbound_when_the_store_returned
                ? std::to_string(*next_outbound_when_the_store_returned)
                : std::string{"(not read)"})
        << " closing_when_it_returned=" << closing_when_the_store_returned;

    EXPECT_EQ(f.sent_count("D"), 0U)
        << "the frame was handed to the transport callback after the close's teardown reset";
    EXPECT_FALSE(send_call.threw) << "Session::send must return a value; it "
                                  << send_call.call_text();
    EXPECT_TRUE(send_call.returned_error(error::dispatch_aborted))
        << "Session::send " << send_call.call_text();
    EXPECT_TRUE(close_done) << "close(terminal) did not complete";
}

// The reply Logon of the acceptor's Logon arm is held in its store call by the wrapper;
// the driver calls close(terminal), whose teardown reset writes the session's next
// outbound number; the hold is then released. The arm is superseded by the close, and
// the Logon's number is no longer below the next one.
TEST(TeardownReset, SupersededReplyLogonInItsStoreCallAcrossTheReset_ReturnsDispatchAborted) {
    DirectAcceptor f{reset_on_disconnect_options()};
    ASSERT_TRUE(f.open());
    f.log->arm();
    f.log->hold_in = is_outbound_store;
    std::optional<seqnum_t> next_outbound_when_the_store_returned;
    f.log->on_return = [&](StoreCall const& c) {
        if (next_outbound_when_the_store_returned || !is_outbound_store(c)) return;
        next_outbound_when_the_store_returned = next_outbound(*f.session);
    };
    auto const mark = f.log->mark();

    CallingThreadReadings call;
    asio::cancellation_signal none;
    auto const logon = f.logon();
    auto fut = f.feed(logon, call, none);
    ASSERT_TRUE(f.run_until([&] { return f.log->holding(); }))
        << "the path was not exercised: the reply Logon's store was not held";

    // The driver's close(terminal). Nothing it awaits is behind the held store call, so
    // it completes while the call is held; the bound is one bounded pump.
    auto close_fut = f.spawn_close(close_mode::terminal);
    bool const close_returned = f.run_until([&] { return DirectAcceptor::ready(close_fut); });
    bool const held_through_the_close = f.log->holding() && !DirectAcceptor::ready(fut);

    f.log->release_hold();
    bool const call_done = f.run_until([&] { return DirectAcceptor::ready(fut); });
    f.log->on_return = nullptr;
    std::optional<StoreCall> reply_store;
    if (auto const* c = f.log->first_after(mark, is_outbound_store)) reply_store = *c;

    ASSERT_TRUE(close_returned && held_through_the_close && call_done && call.completed &&
                !f.log->hold_timed_out() && reply_store.has_value() && reply_store->returned() &&
                next_outbound_when_the_store_returned == std::optional<seqnum_t>{1})
        << "the path was not exercised: close_returned=" << close_returned
        << " store_held_through_the_close=" << held_through_the_close << " call_done=" << call_done
        << " hold_timed_out=" << f.log->hold_timed_out()
        << " the reply Logon's store: " << record_text(reply_store)
        << " next_outbound_when_it_returned="
        << (next_outbound_when_the_store_returned
                ? std::to_string(*next_outbound_when_the_store_returned)
                : std::string{"(not read)"});

    EXPECT_FALSE(call.threw) << "on_inbound_frame must return a value; it " << call.call_text();
    EXPECT_TRUE(call.returned_error(error::dispatch_aborted))
        << "on_inbound_frame " << call.call_text();
    EXPECT_EQ(f.sent_count("A"), 0U) << "the reply Logon was handed to the transport callback";
}

// ── D-OUT-7 (R-21): the order in which a FileStore's store() calls return ────────────
//
// Three store() calls for the outbound direction are made in order from one thread's
// io_context, and the first is held in the store's offload hook, so the other two queue
// at the store's lock. What is read is the order in which the cell's own awaits of the
// calls completed.

// Every call that no cancellation ended returned after every call made before it.
// `completed` is the order in which the awaits completed; the calls were made in the
// order of their sequence numbers, from `first`.
::testing::AssertionResult returned_in_call_order(std::vector<seqnum_t> const& completed,
                                                  seqnum_t const first, std::size_t const calls,
                                                  std::set<seqnum_t> const& cancelled = {}) {
    auto position = [&](seqnum_t seq) {
        return std::find(completed.begin(), completed.end(), seq) - completed.begin();
    };
    for (seqnum_t seq = first; seq < first + calls; ++seq) {
        if (position(seq) == static_cast<std::ptrdiff_t>(completed.size())) {
            return ::testing::AssertionFailure() << "store(" << seq << ") did not return";
        }
        if (cancelled.contains(seq)) continue;
        for (seqnum_t earlier = first; earlier < seq; ++earlier) {
            if (position(earlier) > position(seq)) {
                return ::testing::AssertionFailure()
                       << "store(" << seq << ") returned before store(" << earlier
                       << "), which was made earlier";
            }
        }
    }
    return ::testing::AssertionSuccess();
}

enum class StoreUnderOrderTest : std::uint8_t {
    file_store,  // the shipped FileStore, with nothing in front of it
    // The FileStore behind the recording wrapper, which keeps the first call's result
    // from its caller until the later calls have returned: a store that does not keep
    // the order sentence.
    returns_the_first_call_late,
};

struct OrderOutcome {
    bool made = false;                // the store was made
    bool hold_entered = false;        // the first call stood in the offload hook
    bool queued = false;              // no call had returned when the last one was at the lock
    std::vector<seqnum_t> completed;  // in the order the awaits completed
    std::vector<seqnum_t> completed_before_release;
    // Each call's error, by its sequence number less one; nullopt for a value.
    std::vector<std::optional<error>> results = std::vector<std::optional<error>>(3);
    std::vector<bool> threw = std::vector<bool>(3);
    bool all_returned = false;
    bool hold_timed_out = false;
    bool late_return_timed_out = false;
};

asio::awaitable<void> one_store_call(fixpp::session::MessageStore& store, seqnum_t const seq,
                                     std::span<const std::byte> const frame, OrderOutcome& o) {
    try {
        auto r = co_await store.store(seq, frame, direction_t::outbound);
        if (!r) o.results[seq - 1] = r.error();
    } catch (...) {
        o.threw[seq - 1] = true;
    }
    o.completed.push_back(seq);
}

// `cancel_the_second`: the second call is bound to a cancellation signal of the cell's,
// which is emitted while the call waits at the store's lock.
OrderOutcome run_store_calls(StoreUnderOrderTest const kind, bool const cancel_the_second) {
    OrderOutcome o;
    // Declared first, destroyed last: the hook outlives the pool's offloads.
    store_hold::scoped_hook const hook;
    asio::io_context ioc;
    // The FileStore's offload work is released on the pool thread
    // (tests/support/keep_running.hpp).
    auto const keep = keep_running_for(ioc);
    asio::thread_pool file_pool{1};
    store_hold::release_on_exit const release_guard;
    struct temp_dir {
        std::filesystem::path path = fixpp::test_support::unique_temp_dir("b28_store_order");
        ~temp_dir() { (void)fixpp::test_support::try_remove_temp_dir(path); }
    } const dir;

    fixpp::session::FileStore::Config file_cfg;
    file_cfg.directory = dir.path;
    file_cfg.sender_comp_id = "ISLD";
    file_cfg.target_comp_id = "TW";
    file_cfg.file_io_executor = file_pool.get_executor();
    std::shared_ptr<StoreCallLog> log;
    fixpp::core::expected_t<std::unique_ptr<fixpp::session::MessageStore>> made =
        std::unexpected(error::store_factory_failed);
    if (kind == StoreUnderOrderTest::file_store) {
        fixpp::session::FileStoreFactory factory{file_cfg};
        made = factory.make(file_cfg.sender_comp_id, file_cfg.target_comp_id,
                            std::pmr::get_default_resource(), 0, file_cfg.file_io_executor);
    } else {
        RecordingStoreFactory factory{file_cfg};
        log = factory.log;
        log->arm();
        log->late_return_in = [](StoreCall const& c) {
            return is_outbound_store(c) && c.first == 1;
        };
        made = factory.make(file_cfg.sender_comp_id, file_cfg.target_comp_id,
                            std::pmr::get_default_resource(), 0, file_cfg.file_io_executor);
    }
    if (!made.has_value()) return o;
    o.made = true;
    auto& store = **made;

    auto pump = [&](auto pred) {
        return fixpp::test_support::pump_until(ioc, std::move(pred),
                                               fixpp::test_support::kPumpBudget,
                                               fixpp::test_support::kPumpSlice, nullptr);
    };
    auto const frame = plain_rig::to_bytes(plain_rig::message(
        "FIX.4.2", "D", 1, "ISLD", "TW", "20240101-00:00:00.000", "11=ORD001\x01"));
    asio::cancellation_signal cancel;

    store_hold::arm();
    asio::co_spawn(ioc, one_store_call(store, 1, frame, o), asio::detached);
    o.hold_entered = pump([] { return store_hold::holding(); });
    if (o.hold_entered) {
        // Each later call is started alone, and the ready handlers then run until none
        // is, which takes it past the store's leading post to the lock.
        asio::co_spawn(ioc, one_store_call(store, 2, frame, o),
                       asio::bind_cancellation_slot(cancel.slot(), asio::detached));
        ioc.poll();
        ioc.restart();
        asio::co_spawn(ioc, one_store_call(store, 3, frame, o), asio::detached);
        ioc.poll();
        ioc.restart();
        o.queued = o.completed.empty() && store_hold::holding();
    }
    if (o.queued && cancel_the_second) {
        cancel.emit(asio::cancellation_type::terminal);
        // The bound: the hold is released only below, so nothing but the cell's
        // cancellation can complete a call before then.
        (void)pump([&] { return !o.completed.empty(); });
        ioc.poll();
        ioc.restart();
    }
    o.completed_before_release = o.completed;
    store_hold::release();
    if (log) {
        // The first call's result is in and kept from its caller; the two later calls
        // return first, and only then is it let through.
        (void)pump([&] { return o.completed.size() == 2 && log->returning_late(); });
        log->release_late_return();
    }
    o.all_returned = pump([&] { return o.completed.size() == 3; });
    o.hold_timed_out = store_hold::timed_out();
    o.late_return_timed_out = log && log->late_return_timed_out();
    if (!o.all_returned) {
        // A call still suspended refers to this frame's locals: resume it before they
        // are destroyed.
        if (log) log->release_late_return();
        fixpp::test_support::drain_or_report(ioc, "StoreCallOrder");
    }
    return o;
}

std::string order_text(std::vector<seqnum_t> const& completed) {
    std::string t;
    for (auto const seq : completed) t += std::to_string(seq) + " ";
    return t.empty() ? std::string{"(none)"} : t;
}

::testing::AssertionResult order_gate(OrderOutcome const& o) {
    if (o.made && o.hold_entered && o.queued && o.all_returned && !o.hold_timed_out &&
        !o.late_return_timed_out) {
        return ::testing::AssertionSuccess();
    }
    return ::testing::AssertionFailure()
           << "the path was not exercised: store_made=" << o.made
           << " hold_entered=" << o.hold_entered << " later_calls_queued=" << o.queued
           << " all_returned=" << o.all_returned << " hold_timed_out=" << o.hold_timed_out
           << " late_return_timed_out=" << o.late_return_timed_out
           << " completed: " << order_text(o.completed);
}

// The control: a premise of the shipped store, and no statement of this batch.
TEST(StoreCallOrder, Control_FileStore_StoreCallsReturnInTheOrderTheyWereMade) {
    auto const o = run_store_calls(StoreUnderOrderTest::file_store, /*cancel_the_second=*/false);
    ASSERT_TRUE(order_gate(o));
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_TRUE(!o.threw[i] && !o.results[i]) << "store(" << i + 1 << ") did not succeed";
    }
    EXPECT_TRUE(o.completed_before_release.empty())
        << "a call returned while the first was held: " << order_text(o.completed_before_release);
    EXPECT_TRUE(returned_in_call_order(o.completed, 1, 3))
        << "the awaits completed in the order: " << order_text(o.completed);
}

// The control's positive control: the same calls through a store that returns the
// second and the third before the first. The order check must report it; this arm
// expects that report and prints it.
TEST(StoreCallOrder, PositiveControl_AStoreThatReturnsALaterCallFirst_IsReported) {
    auto const o = run_store_calls(StoreUnderOrderTest::returns_the_first_call_late,
                                   /*cancel_the_second=*/false);
    ASSERT_TRUE(order_gate(o));
    auto const check = returned_in_call_order(o.completed, 1, 3);
    EXPECT_FALSE(check) << "the order check passed a store that returned its calls in the order: "
                        << order_text(o.completed);
    if (!check) std::cout << "[ REPORTED ] " << check.message() << '\n';
}

// The exception the order sentence makes: a call that a cancellation ended may return
// before earlier calls. The second call is cancelled at the lock; the store has no
// frame for its number then, so it refuses the third as out of order.
TEST(StoreCallOrder, Control_FileStore_ACallCancelledAtTheLockReturnsEarlyAndTheOthersInOrder) {
    auto const o = run_store_calls(StoreUnderOrderTest::file_store, /*cancel_the_second=*/true);
    ASSERT_TRUE(order_gate(o));
    EXPECT_EQ(o.completed_before_release, (std::vector<seqnum_t>{2}))
        << "returned before the hold was released: " << order_text(o.completed_before_release);
    EXPECT_TRUE(!o.threw[1] && o.results[1] == error::store_cancelled)
        << "the cancelled call did not return store_cancelled";
    EXPECT_TRUE(!o.threw[0] && !o.results[0]) << "the held call did not succeed";
    EXPECT_TRUE(!o.threw[2] && o.results[2] == error::store_seqnum_out_of_order)
        << "the call after the cancelled one was not refused as out of order";
    EXPECT_TRUE(returned_in_call_order(o.completed, 1, 3, {2}))
        << "the awaits completed in the order: " << order_text(o.completed);
}

// Its positive control: with the first call returned late, the third returns before it.
TEST(StoreCallOrder, PositiveControl_ACancelledCallAndALaterCallReturnedFirst_IsReported) {
    auto const o = run_store_calls(StoreUnderOrderTest::returns_the_first_call_late,
                                   /*cancel_the_second=*/true);
    ASSERT_TRUE(order_gate(o));
    auto const check = returned_in_call_order(o.completed, 1, 3, {2});
    EXPECT_FALSE(check) << "the order check passed a store that returned its calls in the order: "
                        << order_text(o.completed);
    if (!check) std::cout << "[ REPORTED ] " << check.message() << '\n';
}

}  // namespace
