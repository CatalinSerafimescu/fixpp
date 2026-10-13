// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_logout_not_sent.cpp
//
// Ruling R-17 (.specify/536-537-538-recovery-liveness.md §10, D-OUT-6; §5, the rows of
// D-OUT-6 in the table "The cancelled outbound store (§10)"): the logout phase of a graceful close
// does not wait for a reply to a Logout it did not send. And the cell behind the C-ABI row for
// fixpp_session_send (§5, "Implementation steps that can send the design back": the reach of a
// cancelled outbound store through the C ABI).
//
// Two clocks run in a graceful close. The logout phase's wait for the peer's Logout is
// on the session's clock, which is a mock clock in every cell here and is not advanced
// while a cell takes its readings. Session::close also races the phase against an
// asio::steady_timer of its own, armed for the same logout_disconnect_timeout_ms in real
// time (brain/components/graceful-logout.md). So every bound below is real time and is
// derived from the session's own logout_disconnect_timeout_ms; a bound of a timeout or
// more could be met by that timer alone, and each cell says which side of the timeout
// its bound is on.
//
// The suites:
//   GracefulLogoutStoreCancelled   the Logout's store() is cancelled at the FileStore's
//                                  lock, by the grace timer or by an escalating terminal
//                                  close, under an Engine.
//   GracefulLogoutNotSent          the other outcomes on which the Logout was not sent,
//                                  on a directly driven Session.
//   EscalationFromToAdmin          a terminal close posted from the Logout's toAdmin, on
//                                  a MemoryStore.
//
// A cell on the Engine scene asserts nothing while its engine runs, and stops the engine
// on every path.

#include <gtest/gtest.h>

#include <algorithm>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/post.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
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
using fixpp::session::seqnum_t;
using fixpp::session::session_test_access;
using fixpp::test_support::CallingThreadReadings;
using fixpp::test_support::HeldFileStoreScene;
using fixpp::test_support::store_op;
using fixpp::test_support::StoreCall;
using fixpp::test_support::logon_arm::DirectAcceptor;
using std::chrono::milliseconds;
using steady = std::chrono::steady_clock;

// What a store call's record says, for a failure message.
std::string record_text(std::optional<StoreCall> const& c) {
    if (!c) return "not called";
    if (!c->entered()) return "called, and its body never started";
    if (!c->returned()) return "entered, and never returned";
    return c->error ? "returned error " + std::to_string(static_cast<int>(*c->error))
                    : std::string{"returned a value"};
}

std::string result_text(std::optional<fixpp::core::expected_t<void>> const& r) {
    if (!r) return "did not complete";
    return r->has_value() ? std::string{"success"}
                          : "error " + std::to_string(static_cast<int>(r->error()));
}

bool succeeded(std::optional<fixpp::core::expected_t<void>> const& r) {
    return r.has_value() && r->has_value();
}

milliseconds since(steady::time_point const t0) {
    return std::chrono::duration_cast<milliseconds>(steady::now() - t0);
}

// The time left until `end`, never negative: a pump's budget.
steady::duration until(steady::time_point const end) {
    auto const now = steady::now();
    return end > now ? end - now : steady::duration::zero();
}

// The part of logout_disconnect_timeout_ms a cell allows a close that must not wait
// for the timeout: half of it.
milliseconds fraction_of(milliseconds const timeout) { return timeout / 2; }

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

// What the store holds for the Logout's number, and its next numbers, read from the store.
struct StoreReading {
    bool read = false;
    std::string type_at_the_logouts_number;  // empty: no frame
    std::optional<seqnum_t> next_outbound;
    std::optional<seqnum_t> next_inbound;

    [[nodiscard]] std::string text() const {
        return "frame at the Logout's number: " +
               (type_at_the_logouts_number.empty() ? std::string{"none"}
                                                   : "35=" + type_at_the_logouts_number) +
               ", next_outbound=" + (next_outbound ? std::to_string(*next_outbound) : "?") +
               ", next_inbound=" + (next_inbound ? std::to_string(*next_inbound) : "?");
    }
};

asio::awaitable<StoreReading> read_store(fixpp::session::MessageStore* store, seqnum_t logout_seq) {
    StoreReading out;
    TypeVisitor visitor;
    auto const r = co_await store->retrieve(logout_seq, logout_seq, direction_t::outbound, visitor);
    if (r) out.type_at_the_logouts_number = visitor.types;
    if (auto const n = co_await store->next_seqnum(direction_t::outbound, false)) {
        out.next_outbound = *n;
    }
    if (auto const n = co_await store->next_seqnum(direction_t::inbound, false)) {
        out.next_inbound = *n;
    }
    out.read = true;
    co_return out;
}

// ── D-OUT-6, the scene with a held write ─────────────────────────────────────────────
//
// A FileStore session under an Engine, with a second file I/O thread: close(graceful)
// first runs the store's flush, which is an offload with no lock, and on one thread it
// would wait behind the held write. One Engine::send is held inside its store();
// close(graceful) is spawned on the session's executor; the Logout's store() queues at
// the store's lock (the gate: the wrapper's record that it was entered, then the ready
// handlers until none is).

// logout_disconnect_timeout_ms of the scene's session. Every bound of a cell is derived
// from the value the session was configured with, not from this constant.
constexpr std::uint32_t kSceneLogoutTimeoutMs = 1000;

enum class LogoutInput : std::uint8_t {
    grace_timer,                  // close's grace timer expires
    escalation,                   // a terminal close escalates
    grace_timer_reset_on_logout,  // the timer input, with reset_on_logout set
    released_before_the_timer,    // the control: the hold is released before the timer
};

struct LogoutOutcome {
    std::string setup_miss;
    milliseconds timeout{0};  // the session's own logout_disconnect_timeout_ms
    unsigned held_seq = 0;
    unsigned logout_seq = 0;
    bool hold_entered = false;
    bool logout_store_queued = false;  // the gate
    std::optional<StoreCall> logout_store;
    std::size_t release_mark = 0;
    // The closes, in real time, with the session's clock not advanced.
    bool graceful_returned_in_bound = false;
    bool terminal_returned_in_bound = false;     // the escalation input
    std::optional<milliseconds> graceful_took;   // from its spawn; set when it returned in bound
    bool clock_advanced_for_the_closes = false;  // the cell had to advance the clock to end them
    std::optional<fixpp::core::expected_t<void>> graceful_result;
    std::optional<fixpp::core::expected_t<void>> terminal_result;
    // The phase's ending, read while the write is still held (the three cancelled inputs).
    bool ending_reached_before_release = false;  // the reading point; each input's is its own
    bool hold_in_flight_at_that_reading = false;
    fsm_state state_before_release = fsm_state::NotConnected;
    std::vector<fsm_state> ring_before_release;
    // After the closes and the held send have returned.
    fsm_state state_after_close = fsm_state::NotConnected;
    std::vector<fsm_state> ring_after_close;
    StoreReading store;
    // MsgType(35) and MsgSeqNum(34) of each frame the peer read after the hold began.
    std::vector<std::string> wire;
    std::size_t logouts_read = 0;  // by the peer, to end-of-stream
    bool peer_read_ended = false;
    bool hold_timed_out = false;
    bool stop_completed = false;
};

LogoutOutcome run_logout_scene(LogoutInput const input) {
    using enum LogoutInput;
    LogoutOutcome o;
    HeldFileStoreScene scene{/*file_io_threads=*/2};
    auto& rig = scene.rig;
    scene.tweak_cfg = [&](fixpp::session::SessionConfig& c) {
        c.logout_disconnect_timeout_ms = kSceneLogoutTimeoutMs;
        c.reset_on_logout = input == grace_timer_reset_on_logout;
        o.timeout = milliseconds{c.logout_disconnect_timeout_ms};
    };
    std::shared_ptr<fixpp::session::Session> session;
    std::future<fixpp::core::expected_t<void>> held_fut;
    std::future<fixpp::core::expected_t<void>> close_fut;
    std::future<fixpp::core::expected_t<void>> terminal_fut;
    std::size_t rx_mark = 0;

    // The gate, and the readings taken with the session's clock not advanced.
    bool const reached = [&] {
        if (!scene.setup(1)) return false;
        session = rig.session();
        if (!session) {
            scene.setup_miss = "the session is not published";
            return false;
        }
        auto const strand = session->executor().underlying();
        o.held_seq = scene.last + 1;
        o.logout_seq = scene.last + 2;
        rx_mark = rig.peer.received.size();

        // The hold: one Engine::send, caught inside its store()'s offload.
        store_hold::arm();
        held_fut = scene.send_app();
        if (!rig.run_until([] { return store_hold::holding(); })) {
            scene.setup_miss = "the offload hook did not enter its hold";
            return false;
        }
        o.hold_entered = true;

        // close(graceful), spawned as fixpp_session_close spawns it.
        auto const close_mark = scene.log->mark();
        auto logout_store = [&] {
            return scene.log->first_after(close_mark, [&](StoreCall const& call) {
                return call.op == store_op::store && call.dir == direction_t::outbound &&
                       call.first == o.logout_seq;
            });
        };
        auto const t0 = steady::now();
        close_fut = asio::co_spawn(strand, session->close(close_mode::graceful), asio::use_future);
        if (rig.run_until([&] { return logout_store() != nullptr && logout_store()->entered(); },
                          fraction_of(o.timeout))) {
            rig.ioc.poll();
            rig.ioc.restart();
            o.logout_store_queued = !logout_store()->returned() && store_hold::holding();
        }
        if (!o.logout_store_queued) return false;

        if (input == released_before_the_timer) {
            o.release_mark = scene.log->mark();
            store_hold::release();
            // The control's close ends at the grace timer: after the timeout, and inside
            // the timeout plus the fraction.
            o.graceful_returned_in_bound =
                rig.run_until([&] { return HeldFileStoreScene::ready(close_fut); },
                              until(t0 + o.timeout + fraction_of(o.timeout)));
            if (o.graceful_returned_in_bound) o.graceful_took = since(t0);
            if (auto const* c = logout_store(); c != nullptr) o.logout_store = *c;
            return true;
        }

        // The bound. Escalation: the fraction, counted from the escalation; the
        // escalation cancels the grace timer, so that timer cannot complete a close.
        // Timer: the timeout plus the fraction, counted from the close's spawn, which is
        // less than two timeouts; the phase's own wait is on the session's clock and
        // cannot end inside it.
        auto bound_end = t0 + o.timeout + fraction_of(o.timeout);
        if (input == escalation) {
            bound_end = steady::now() + fraction_of(o.timeout);
            terminal_fut =
                asio::co_spawn(strand, session->close(close_mode::terminal), asio::use_future);
        }
        (void)rig.run_until([&] { return logout_store()->returned(); }, until(bound_end));
        o.logout_store = *logout_store();
        // The phase's ending is read BEFORE the hold is released. Once it is released the
        // held frame's own send ends too, and that is not the phase's ending: its
        // transmit meets the transport the close has closed, and Session::send records
        // Disconnected for a returned dispatch_aborted with no test of the state (note
        // D-OUT-9, "A send that fails without an exception is not touched"; D-OUT-10,
        // "A value, not D-OUT-2's exception").
        if (input == grace_timer_reset_on_logout) {
            // This close cannot return while the write is held: its teardown reset calls
            // the store's reset(), which queues at the store's lock behind the held
            // write. The reading point is that call entered and not returned: the phase
            // has left, and close() is past its own test of the state.
            auto teardown_reset = [&] {
                return scene.log->first_after(
                    close_mark, [](StoreCall const& call) { return call.op == store_op::reset; });
            };
            o.ending_reached_before_release =
                rig.run_until(
                    [&] { return teardown_reset() != nullptr && teardown_reset()->entered(); },
                    until(bound_end)) &&
                !teardown_reset()->returned();
        } else {
            // With no teardown reset, nothing in a close's terminal part waits for the
            // held write: the closes return while it is held.
            o.ending_reached_before_release = rig.run_until(
                [&] { return HeldFileStoreScene::ready(close_fut); }, until(bound_end));
            if (terminal_fut.valid()) {
                o.ending_reached_before_release =
                    rig.run_until([&] { return HeldFileStoreScene::ready(terminal_fut); },
                                  until(bound_end)) &&
                    o.ending_reached_before_release;
            }
        }
        // No poll for "no handler ready" here: while the write is held, a close that is
        // waiting posts to the session's executor again and again, so such a poll would
        // not return. Each reading point above is a condition the bounded pump saw.
        o.hold_in_flight_at_that_reading =
            store_hold::holding() && !HeldFileStoreScene::ready(held_fut);
        o.state_before_release = session->state();
        {
            auto const ring = session->fsm_visit_history();
            o.ring_before_release.assign(ring.begin(), ring.end());
        }
        o.release_mark = scene.log->mark();
        store_hold::release();
        o.graceful_returned_in_bound =
            rig.run_until([&] { return HeldFileStoreScene::ready(close_fut); }, until(bound_end));
        if (o.graceful_returned_in_bound) o.graceful_took = since(t0);
        if (terminal_fut.valid()) {
            o.terminal_returned_in_bound = rig.run_until(
                [&] { return HeldFileStoreScene::ready(terminal_fut); }, until(bound_end));
        }
        return true;
    }();

    // On every path: the hold released, and a close that is still waiting on the
    // session's clock ended, so that the engine can stop.
    store_hold::release();
    if (close_fut.valid() && !HeldFileStoreScene::ready(close_fut)) {
        o.clock_advanced_for_the_closes = true;
        // Staging (ci/mock-clock-staging-sweep.sh): KIND F. Every reading of the closes
        // was taken above, with the clock not advanced. This advance only ends a logout
        // wait that is still pending, so that the engine can stop; if it were lost, the
        // stop below would not complete inside its pump, and the cell fails on
        // stop_completed.
        rig.clock->advance(2 * o.timeout);
        (void)rig.run_until([&] { return HeldFileStoreScene::ready(close_fut); });
    }
    if (reached) {
        if (HeldFileStoreScene::ready(close_fut)) o.graceful_result = close_fut.get();
        if (terminal_fut.valid() &&
            rig.run_until([&] { return HeldFileStoreScene::ready(terminal_fut); })) {
            o.terminal_result = terminal_fut.get();
        }
        (void)rig.run_until([&] { return HeldFileStoreScene::ready(held_fut); });
        o.state_after_close = session->state();
        auto const ring = session->fsm_visit_history();
        o.ring_after_close.assign(ring.begin(), ring.end());
        if (auto* const store = scene.log->store; store != nullptr) {
            auto read_fut =
                asio::co_spawn(rig.ioc, read_store(store, o.logout_seq), asio::use_future);
            if (rig.run_until([&] { return HeldFileStoreScene::ready(read_fut); })) {
                o.store = read_fut.get();
            }
        }
    }
    session.reset();
    scene.finish();
    o.peer_read_ended = rig.run_until([&] { return rig.peer.read_ended; });
    for (auto const& f : scene.peer_frames_from(rx_mark)) {
        o.wire.push_back(plain_rig::field_value(f, "35") + "/" + plain_rig::field_value(f, "34"));
    }
    o.logouts_read = plain_rig::frames_of_type(rig.peer.received, "5").size();
    o.setup_miss = scene.setup_miss;
    o.hold_timed_out = store_hold::timed_out();
    o.stop_completed = scene.stop_completed;
    return o;
}

std::string joined(std::vector<std::string> const& v) {
    std::string out;
    for (auto const& s : v) out += (out.empty() ? "" : ", ") + s;
    return out.empty() ? std::string{"(no frame)"} : out;
}

std::string ring_text(std::vector<fsm_state> const& ring) {
    std::string t;
    for (auto const st : ring) t += std::to_string(static_cast<int>(st)) + " ";
    return t.empty() ? std::string{"(empty)"} : t;
}

::testing::AssertionResult logout_gate(LogoutOutcome const& o) {
    if (!o.setup_miss.empty()) return ::testing::AssertionFailure() << o.setup_miss;
    if (!o.hold_entered || !o.logout_store_queued || o.hold_timed_out) {
        return ::testing::AssertionFailure()
               << "the path was not exercised: hold_entered=" << o.hold_entered
               << " logout_store_queued=" << o.logout_store_queued
               << " hold_timed_out=" << o.hold_timed_out;
    }
    if (!o.stop_completed) return ::testing::AssertionFailure() << "Engine::stop did not complete";
    return ::testing::AssertionSuccess();
}

// The window of the three cancelled inputs: the Logout's store() returned
// store_cancelled while the hold was in flight.
::testing::AssertionResult logout_store_was_cancelled(LogoutOutcome const& o) {
    if (o.logout_store.has_value() && o.logout_store->returned() &&
        o.logout_store->returned_at < o.release_mark &&
        o.logout_store->error == error::store_cancelled) {
        return ::testing::AssertionSuccess();
    }
    return ::testing::AssertionFailure()
           << "the path was not exercised: the Logout's store() must return store_cancelled "
              "before the hold is released; it "
           << record_text(o.logout_store);
}

// What the timer input and the escalation input both read.
void expect_logout_not_sent_ending(LogoutOutcome const& o) {
    EXPECT_TRUE(o.peer_read_ended) << "the peer's read did not end";
    EXPECT_EQ(o.logouts_read, 0U) << "the peer read a Logout; after the hold began it read: "
                                  << joined(o.wire);
    EXPECT_TRUE(o.graceful_returned_in_bound && !o.clock_advanced_for_the_closes)
        << "close(graceful) did not return inside the bound with the session's clock not "
           "advanced (logout_disconnect_timeout_ms = "
        << o.timeout.count() << ")";
    EXPECT_TRUE(succeeded(o.graceful_result))
        << "close(graceful) " << result_text(o.graceful_result);
    // The phase's ending, read while the write was still held.
    EXPECT_TRUE(o.ending_reached_before_release)
        << "the reading point was not reached while the write was held: the closes did not "
           "return (with reset_on_logout: the close did not reach its teardown reset)";
    EXPECT_TRUE(o.hold_in_flight_at_that_reading)
        << "the held send had ended when the phase's ending was read";
    EXPECT_EQ(o.state_before_release, fsm_state::Disconnected);
    EXPECT_EQ(std::count(o.ring_before_release.begin(), o.ring_before_release.end(),
                         fsm_state::Disconnected),
              1)
        << "the state ring before the hold was released: " << ring_text(o.ring_before_release);
    // After the held send has returned. Its failed transmit may record Disconnected
    // again (the comment at the reading point says where); a state that is not
    // Disconnected must not appear.
    // The ring is compared as a prefix, which holds while it has not wrapped: the scene
    // records fewer states than the ring's capacity.
    EXPECT_EQ(o.state_after_close, fsm_state::Disconnected);
    bool const prefix_kept = o.ring_after_close.size() >= o.ring_before_release.size() &&
                             std::equal(o.ring_before_release.begin(), o.ring_before_release.end(),
                                        o.ring_after_close.begin());
    EXPECT_TRUE(prefix_kept &&
                std::all_of(o.ring_after_close.begin() +
                                static_cast<std::ptrdiff_t>(o.ring_before_release.size()),
                            o.ring_after_close.end(),
                            [](fsm_state st) { return st == fsm_state::Disconnected; }))
        << "a state other than Disconnected was recorded after the phase's ending; the ring "
           "before the release: "
        << ring_text(o.ring_before_release)
        << "and after the held send returned: " << ring_text(o.ring_after_close);
}

TEST(GracefulLogoutStoreCancelled, ByTheGraceTimer_NoLogoutIsSentAndTheCloseDoesNotWait) {
    auto const o = run_logout_scene(LogoutInput::grace_timer);
    ASSERT_TRUE(logout_gate(o));
    ASSERT_TRUE(logout_store_was_cancelled(o));
    expect_logout_not_sent_ending(o);
    ASSERT_TRUE(o.store.read) << "the store was not read";
    EXPECT_EQ(o.store.next_outbound, std::optional<seqnum_t>{o.logout_seq})
        << "the store's next outbound number is not the Logout's own: " << o.store.text();
}

TEST(GracefulLogoutStoreCancelled, ByAnEscalatingTerminalClose_BothClosesReturnAtOnce) {
    auto const o = run_logout_scene(LogoutInput::escalation);
    ASSERT_TRUE(logout_gate(o));
    ASSERT_TRUE(logout_store_was_cancelled(o));
    expect_logout_not_sent_ending(o);
    EXPECT_TRUE(o.terminal_returned_in_bound && !o.clock_advanced_for_the_closes)
        << "the escalating close(terminal) did not return inside the bound with the session's "
           "clock not advanced";
    EXPECT_TRUE(succeeded(o.terminal_result))
        << "close(terminal) " << result_text(o.terminal_result);
    ASSERT_TRUE(o.store.read) << "the store was not read";
    EXPECT_EQ(o.store.next_outbound, std::optional<seqnum_t>{o.logout_seq})
        << "the store's next outbound number is not the Logout's own: " << o.store.text();
}

// The timer input with reset_on_logout set: the teardown reset runs as after a graceful
// close whose Logout was sent.
TEST(GracefulLogoutStoreCancelled, ByTheGraceTimer_ResetOnLogout_TheTeardownResetStillRuns) {
    auto const o = run_logout_scene(LogoutInput::grace_timer_reset_on_logout);
    ASSERT_TRUE(logout_gate(o));
    ASSERT_TRUE(logout_store_was_cancelled(o));
    expect_logout_not_sent_ending(o);
    ASSERT_TRUE(o.store.read) << "the store was not read";
    EXPECT_TRUE(o.store.next_outbound == std::optional<seqnum_t>{1} &&
                o.store.next_inbound == std::optional<seqnum_t>{1})
        << "the store's next numbers are not 1 in each direction: " << o.store.text();
}

// The control: the hold is released before the grace timer, so the Logout is sent. No
// statement of D-OUT-6 is on this trace.
TEST(GracefulLogoutStoreCancelled,
     Control_HoldReleasedBeforeTheTimer_LogoutSentAndCloseEndsAtTheTimer) {
    auto const o = run_logout_scene(LogoutInput::released_before_the_timer);
    ASSERT_TRUE(logout_gate(o));
    ASSERT_TRUE(o.logout_store.has_value() && o.logout_store->returned() &&
                o.logout_store->returned_at > o.release_mark && !o.logout_store->error)
        << "the control's Logout must be stored after the release; its store() "
        << record_text(o.logout_store);

    EXPECT_EQ(o.wire, (std::vector<std::string>{"D/" + std::to_string(o.held_seq),
                                                "5/" + std::to_string(o.logout_seq)}))
        << "the peer must read the held frame and then the Logout; it read: " << joined(o.wire);
    ASSERT_TRUE(o.store.read) << "the store was not read";
    EXPECT_EQ(o.store.type_at_the_logouts_number, "5") << o.store.text();
    // The close ends at the grace timer: not before the timeout, and inside the bound.
    ASSERT_TRUE(o.graceful_returned_in_bound && !o.clock_advanced_for_the_closes)
        << "close(graceful) did not return at the grace timer";
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): set with graceful_returned_in_bound
    EXPECT_GE(o.graceful_took->count(), o.timeout.count())
        << "close(graceful) returned before its grace timer";
    EXPECT_TRUE(succeeded(o.graceful_result))
        << "close(graceful) " << result_text(o.graceful_result);
    EXPECT_EQ(o.state_after_close, fsm_state::Disconnected);
}

// ── D-OUT-6, the other outcomes on which the Logout was not sent ─────────────────────
//
// A directly driven acceptor on the callback transport. The bound is under the timeout:
// on these routes nothing cancels close's grace timer, so a close that waits completes
// at the timeout, and a bound of a timeout or more would be met by a close that waited.

constexpr std::uint32_t kLogoutTimeoutMs = 1000;

struct NotSentOutcome {
    bool active = false;
    bool not_sent = false;  // the gate: the outcome under test was reached
    std::string not_sent_text;
    milliseconds timeout{0};
    bool returned_in_bound = false;
    std::optional<fixpp::core::expected_t<void>> close_result;
    fsm_state state_after_close = fsm_state::NotConnected;
    std::size_t logouts_transmitted = 0;
};

// What the transport callback of the failed-write input recorded.
struct ThrowingSink {
    std::vector<std::string> sent;
    bool threw_on_a_logout = false;
};

enum class NotSentInput : std::uint8_t {
    store_fails_closed,  // a persistent store returns store_io_failure for the Logout
    write_fails,         // the transport callback throws on the Logout
};

NotSentOutcome run_not_sent(NotSentInput const input) {
    NotSentOutcome o;
    auto const sink = std::make_shared<ThrowingSink>();
    DirectAcceptor::Options options{.logout_disconnect_timeout_ms = kLogoutTimeoutMs};
    if (input == NotSentInput::store_fails_closed) {
        options.reports_persistent = true;
    } else {
        options.tweak_cfg = [sink](fixpp::session::SessionConfig& c) {
            c.transport_send = [sink](std::span<const std::byte> f) {
                std::string text{reinterpret_cast<char const*>(f.data()), f.size()};
                if (plain_rig::field_value(text, "35") == "5") {
                    sink->threw_on_a_logout = true;
                    throw std::system_error(std::make_error_code(std::errc::connection_reset));
                }
                sink->sent.push_back(std::move(text));
            };
        };
    }
    DirectAcceptor f{options};
    o.timeout = milliseconds{f.cfg.logout_disconnect_timeout_ms};
    if (!f.open()) return o;
    auto const logon = f.logon();
    CallingThreadReadings logon_call;
    if (!f.feed_plain(logon, logon_call) || f.session->state() != fsm_state::Active) return o;
    o.active = true;

    auto const logout_seq = session_test_access::seqnum_mgr(*f.session).peek_outbound();
    auto is_logouts_store = [logout_seq](StoreCall const& c) {
        return c.op == store_op::store && c.dir == direction_t::outbound && c.first == logout_seq;
    };
    f.log->arm();
    if (input == NotSentInput::store_fails_closed) {
        f.log->fail_in = is_logouts_store;
        f.log->fail_code = error::store_io_failure;
    }
    auto const mark = f.log->mark();
    auto close_fut = f.spawn_close(close_mode::graceful);
    o.returned_in_bound =
        f.run_until([&] { return DirectAcceptor::ready(close_fut); }, fraction_of(o.timeout));
    // A close that waited ends at its grace timer; twice the timeout is past it.
    if (f.run_until([&] { return DirectAcceptor::ready(close_fut); }, 2 * o.timeout)) {
        o.close_result = close_fut.get();
    }
    o.state_after_close = f.session->state();
    std::optional<StoreCall> logouts_store;
    if (auto const* c = f.log->first_after(mark, is_logouts_store)) logouts_store = *c;
    if (input == NotSentInput::store_fails_closed) {
        o.not_sent = logouts_store.has_value() && logouts_store->returned() &&
                     logouts_store->error == error::store_io_failure;
        o.not_sent_text = "the Logout's store() " + record_text(logouts_store);
        o.logouts_transmitted = f.sent_count("5");
    } else {
        o.not_sent = sink->threw_on_a_logout;
        o.not_sent_text = std::string{"the transport callback "} +
                          (sink->threw_on_a_logout ? "threw on a Logout" : "saw no Logout");
        for (auto const& frame : sink->sent) {
            o.logouts_transmitted +=
                static_cast<std::size_t>(plain_rig::field_value(frame, "35") == "5");
        }
    }
    return o;
}

void expect_close_did_not_wait(NotSentOutcome const& o) {
    EXPECT_EQ(o.logouts_transmitted, 0U) << "a Logout reached the peer";
    EXPECT_TRUE(o.returned_in_bound)
        << "close(graceful) did not return inside " << fraction_of(o.timeout).count()
        << " ms, with the session's clock not advanced (logout_disconnect_timeout_ms = "
        << o.timeout.count() << "): it waited for a reply to a Logout that was not sent";
    EXPECT_TRUE(succeeded(o.close_result)) << "close(graceful) " << result_text(o.close_result);
    EXPECT_EQ(o.state_after_close, fsm_state::Disconnected);
}

TEST(GracefulLogoutNotSent, StoreFailsClosedForTheLogout_CloseDoesNotWait) {
    auto const o = run_not_sent(NotSentInput::store_fails_closed);
    ASSERT_TRUE(o.active) << "setup: the session did not reach Active";
    ASSERT_TRUE(o.not_sent) << "the path was not exercised: " << o.not_sent_text;
    expect_close_did_not_wait(o);
}

TEST(GracefulLogoutNotSent, TransportThrowsOnTheLogout_CloseDoesNotWait) {
    auto const o = run_not_sent(NotSentInput::write_fails);
    ASSERT_TRUE(o.active) << "setup: the session did not reach Active";
    ASSERT_TRUE(o.not_sent) << "the path was not exercised: " << o.not_sent_text;
    expect_close_did_not_wait(o);
}

// ── The reach of a cancelled outbound store through the C ABI ────────────────────────
//
// A C-ABI session's store is a MemoryStore, and a C caller's close is spawned on the
// session's executor. The one shipped source of a cancellation at the graceful Logout's
// store call that a C caller can cause is an escalating terminal close. The reading
// under test: that escalation does not reach the Logout's store call. It cancels close's
// grace timer, whose completion is queued behind the store call's leading post, so the
// store call returns before the logout phase is cancelled.
//
// The Application's toAdmin, on the Logout, posts a handler that calls close(terminal).
// Posted, so that it runs after close's race has started its timer; called inline it
// would cancel a timer with no wait pending and prove nothing.
//
// Two readings. The Logout's store call returned success. And the escalation did end the
// phase: both closes return inside the fraction of the timeout, counted from the
// escalation, with the session's clock not advanced. The second is what makes the first
// a result: without it the escalation proved nothing.

struct LogoutToAdminApp final : fixpp::session::Application {
    std::function<void()> on_logout;
    void toAdmin(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& mv,
                 const fixpp::session::SessionId& /*id*/) override {
        if (on_logout && mv.msg_type() == "5") on_logout();
    }
};

TEST(EscalationFromToAdmin,
     Control_TerminalClosePostedFromTheLogoutsToAdmin_StoreCallIsNotCancelled) {
    auto const app = std::make_shared<LogoutToAdminApp>();
    DirectAcceptor f{DirectAcceptor::Options{.logout_disconnect_timeout_ms = kLogoutTimeoutMs,
                                             .application = app}};
    auto const timeout = milliseconds{f.cfg.logout_disconnect_timeout_ms};
    ASSERT_TRUE(f.open());
    auto const logon = f.logon();
    CallingThreadReadings logon_call;
    ASSERT_TRUE(f.feed_plain(logon, logon_call) && f.session->state() == fsm_state::Active)
        << "setup: the session did not reach Active";

    auto const logout_seq = session_test_access::seqnum_mgr(*f.session).peek_outbound();
    auto is_logouts_store = [logout_seq](StoreCall const& c) {
        return c.op == store_op::store && c.dir == direction_t::outbound && c.first == logout_seq;
    };
    f.log->arm();
    auto const mark = f.log->mark();

    std::future<fixpp::core::expected_t<void>> terminal_fut;
    bool escalated = false;
    bool graceful_was_closing_at_the_escalation = false;
    steady::time_point escalated_at;
    app->on_logout = [&] {
        if (escalated) return;
        asio::post(f.ioc, [&] {
            if (escalated) return;
            escalated = true;
            graceful_was_closing_at_the_escalation = session_test_access::is_closing(*f.session);
            escalated_at = steady::now();
            terminal_fut = f.spawn_close(close_mode::terminal);
        });
    };
    auto graceful_fut = f.spawn_close(close_mode::graceful);
    bool const escalation_ran = f.run_until([&] { return escalated; }, fraction_of(timeout));
    // The bound: the fraction of the timeout, from the escalation. The escalation cancels
    // close's grace timer, so that timer cannot complete either close.
    auto const bound_end = escalated_at + fraction_of(timeout);
    bool const graceful_in_bound =
        escalation_ran &&
        f.run_until([&] { return DirectAcceptor::ready(graceful_fut); }, until(bound_end));
    bool const terminal_in_bound =
        escalation_ran && terminal_fut.valid() &&
        f.run_until([&] { return DirectAcceptor::ready(terminal_fut); }, until(bound_end));
    // Past the grace timer, for a close that waited.
    (void)f.run_until([&] { return DirectAcceptor::ready(graceful_fut); }, 2 * timeout);
    app->on_logout = nullptr;
    std::optional<StoreCall> logouts_store;
    if (auto const* c = f.log->first_after(mark, is_logouts_store)) logouts_store = *c;

    ASSERT_TRUE(escalation_ran && graceful_was_closing_at_the_escalation)
        << "the path was not exercised: the handler posted from the Logout's toAdmin did not run "
           "while close(graceful) was in flight: escalation_ran="
        << escalation_ran << " graceful_closing=" << graceful_was_closing_at_the_escalation;
    ASSERT_TRUE(logouts_store.has_value()) << "the path was not exercised: no store call was made "
                                              "for the Logout";

    // The reading behind the C-ABI row. A store call shown cancelled here means the reach
    // is real (note §5, "Implementation steps that can send the design back").
    EXPECT_TRUE(logouts_store->returned() && !logouts_store->error)
        << "the Logout's store call " << record_text(logouts_store);
    // The escalation did end the phase.
    EXPECT_TRUE(graceful_in_bound)
        << "close(graceful) did not return inside " << fraction_of(timeout).count()
        << " ms of the escalation, with the session's clock not advanced";
    EXPECT_TRUE(terminal_in_bound)
        << "close(terminal) did not return inside " << fraction_of(timeout).count()
        << " ms of the escalation, with the session's clock not advanced";
}

}  // namespace
