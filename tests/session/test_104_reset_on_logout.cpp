// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_104_reset_on_logout.cpp — test-only.
//
// Feature 104 (specs/104-sequence-reset-fidelity/), fixpp#595: an unanswered Logout does not
// reset under reset_on_logout. This file holds the feature's reset-on-Logout cells.
//
// Its first cell, HookedStoreRecord104.ResetAndResetToRecordedApart (tasks.md T016), is the
// known positive of the test-support additions research D10 names (tasks.md T015). It drives a
// HookedStore and a BlockingWriteTransport directly, with no Session, one arm per addition:
//   - the per-call record tells a reset() from a reset_to, with reset_to's targets and the
//     snapshot's readings taken at the call's entry, before the on_reset hook;
//   - Hooks::on_inbound_persist_call holds the inbound persist it selects, and a persist it
//     does not select returns unheld;
//   - a reset_to issued during a held outbound store falls between that store's two events,
//     and an unheld store records neither;
//   - blocking_write_state::starts counts a parked write that has not completed.
//
// The cells R-1y and R-1z (tasks.md T019, T020; research D10's rows) run on
// resend_gap_scene.hpp's Scene with reset_on_logout set, a HookedStore and the Scene's mock
// clock, each on both roles. The Scene's first session is replaced before it opens, so the
// cell's session is built from the edited config. They read the reset calls on HookedStore's
// per-call record (StoreLog::calls), whose snapshot reads the session's state at each call.
//
// Pumps. A held store operation yields the strand by posts until it is released, or until
// kHoldBound passes, when the log's hold_timed_out is set and the operation returns. So each
// arm observes a hold over a window of a quarter of kHoldBound, and pumps for at most half of
// kHoldBound after a release: the hold's own deadline cannot end it inside either, and an arm
// asserts hold_timed_out is false. Every arm releases what it holds and drains its context on
// every exit path.
#include <gtest/gtest.h>

#include <array>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_event.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "session/resend_gap_scene.hpp"
#include "support/blocking_write_transport.hpp"
#include "support/hooked_store.hpp"
#include "support/session_executor_of.hpp"
#include "support/session_test_access.hpp"

namespace {

using fixpp::session::direction_t;
using fixpp::session::fsm_state;
using fixpp::session::seqnum_t;
using fixpp::test_support::CallReading;
using fixpp::test_support::HookedStore;
using fixpp::test_support::kHoldBound;
using fixpp::test_support::StoreLog;

constexpr auto kHoldWindow = std::chrono::duration_cast<std::chrono::milliseconds>(kHoldBound) / 4;
constexpr auto kReleaseBudget =
    std::chrono::duration_cast<std::chrono::milliseconds>(kHoldBound) / 2;
constexpr auto kSlice = std::chrono::milliseconds{1};

// Runs the context in slices until done() or the budget passes; returns done().
bool pump_until(asio::io_context& ioc, const std::function<bool()>& done,
                std::chrono::milliseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (!done() && std::chrono::steady_clock::now() < deadline) {
        ioc.restart();
        ioc.run_for(kSlice);
    }
    return done();
}

// Runs the context for the whole window (a held operation keeps it busy with posts).
void pump_for(asio::io_context& ioc, std::chrono::milliseconds window) {
    const auto deadline = std::chrono::steady_clock::now() + window;
    while (std::chrono::steady_clock::now() < deadline) {
        ioc.restart();
        ioc.run_for(kSlice);
    }
}

// Arm 1: a reset_to and a reset(), called directly, read as two distinct records with their
// counts. The one-shot on_reset hook fires at the reset_to and changes the snapshot's source,
// so an entry taken after the hook reads the changed value.
void arm_record(asio::io_context& ioc) {
    auto log = std::make_shared<StoreLog>();
    CallReading source{.state = fsm_state::Active, .transport_open = true};
    log->call_snapshot = [&source] { return source; };
    HookedStore::Hooks hooks;
    hooks.on_reset = [&source] {
        source = CallReading{.state = fsm_state::LogoutSent, .transport_open = false};
    };
    HookedStore store{1, std::move(hooks), log};

    bool done = false;
    std::optional<fixpp::core::expected_t<void>> r_reset_to;
    std::optional<fixpp::core::expected_t<void>> r_reset;
    asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            r_reset_to = co_await store.reset_to(2, 1);
            source = CallReading{.state = fsm_state::Disconnected, .transport_open = false};
            r_reset = co_await store.reset();
            done = true;
        },
        asio::detached);
    const bool finished = pump_until(ioc, [&] { return done; }, kReleaseBudget);
    ASSERT_TRUE(finished) << "arm1: the reset_to and the reset did not complete";
    ASSERT_TRUE(r_reset_to.has_value() && r_reset_to->has_value()) << "arm1: reset_to failed";
    ASSERT_TRUE(r_reset.has_value() && r_reset->has_value()) << "arm1: reset failed";

    ASSERT_EQ(log->calls.size(), 2U) << "arm1: one record per call";
    const auto& first = log->calls[0];
    const auto& second = log->calls[1];
    EXPECT_EQ(first.kind, StoreLog::call_kind::reset_to) << "arm1: the first call is a reset_to";
    EXPECT_EQ(first.next_in, seqnum_t{2}) << "arm1: reset_to's inbound target";
    EXPECT_EQ(first.next_out, seqnum_t{1}) << "arm1: reset_to's outbound target";
    ASSERT_TRUE(first.reading.has_value()) << "arm1: the reset_to's reading";
    EXPECT_EQ(first.reading->state, fsm_state::Active)
        << "arm1: the reset_to's reading is taken before the on_reset hook";
    EXPECT_TRUE(first.reading->transport_open)
        << "arm1: the reset_to's reading is taken before the on_reset hook";
    EXPECT_EQ(second.kind, StoreLog::call_kind::reset) << "arm1: the second call is a reset()";
    EXPECT_EQ(second.next_in, seqnum_t{0}) << "arm1: a reset() carries no targets";
    EXPECT_EQ(second.next_out, seqnum_t{0}) << "arm1: a reset() carries no targets";
    ASSERT_TRUE(second.reading.has_value()) << "arm1: the reset()'s reading";
    EXPECT_EQ(second.reading->state, fsm_state::Disconnected)
        << "arm1: the reset()'s reading is taken at its own call";
    EXPECT_FALSE(second.reading->transport_open)
        << "arm1: the reset()'s reading is taken at its own call";
    EXPECT_EQ(log->reset_to_calls, 1) << "arm1: one reset_to counted";
    EXPECT_EQ(log->reset_calls, 1) << "arm1: one reset() counted";

    // The completion log is unchanged: it still names both writes, in completion order.
    ASSERT_EQ(log->writes.size(), 2U) << "arm1: StoreLog::writes";
    EXPECT_EQ(log->writes[0].op, "reset_to 2 1") << "arm1: StoreLog::writes";
    EXPECT_EQ(log->writes[1].op, "reset") << "arm1: StoreLog::writes";
}

// Arm 2: two next_seqnum(inbound, true) calls, with on_inbound_persist_call selecting the
// second: the first completes, the second completes its write and then holds until released.
void arm_inbound_persist_call(asio::io_context& ioc) {
    auto log = std::make_shared<StoreLog>();
    int consulted = 0;
    bool released = false;
    HookedStore::Hooks hooks;
    hooks.on_inbound_persist_call = [&consulted] { return ++consulted == 2; };
    hooks.release_when = [&released] { return released; };
    HookedStore store{1, std::move(hooks), log};

    bool first_done = false;
    bool second_done = false;
    std::optional<fixpp::core::expected_t<seqnum_t>> r1;
    std::optional<fixpp::core::expected_t<seqnum_t>> r2;
    asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            r1 = co_await store.next_seqnum(direction_t::inbound, true);
            first_done = true;
            r2 = co_await store.next_seqnum(direction_t::inbound, true);
            second_done = true;
        },
        asio::detached);

    const bool first_finished = pump_until(ioc, [&] { return first_done; }, kHoldWindow);
    if (first_finished) pump_for(ioc, kHoldWindow);
    const bool second_done_while_held = second_done;
    const int consulted_while_held = consulted;
    const std::size_t writes_while_held = log->writes.size();
    released = true;
    const bool second_finished = pump_until(ioc, [&] { return second_done; }, kReleaseBudget);

    EXPECT_TRUE(first_finished) << "arm2: the unselected first persist held";
    EXPECT_FALSE(second_done_while_held) << "arm2: the selected second persist did not hold";
    EXPECT_EQ(consulted_while_held, 2) << "arm2: the selector is called on every persist";
    EXPECT_EQ(writes_while_held, 2U)
        << "arm2: the selected persist completes its write before it holds";
    EXPECT_TRUE(second_finished) << "arm2: the held persist did not return after its release";
    EXPECT_FALSE(log->hold_timed_out) << "arm2: the hold ended by its bound, not its release";
    ASSERT_TRUE(r1.has_value() && r1->has_value()) << "arm2: the first persist failed";
    ASSERT_TRUE(r2.has_value() && r2->has_value()) << "arm2: the second persist failed";
    EXPECT_EQ(**r2, **r1 + 1) << "arm2: the two persists advance the counter in order";
}

// Arm 3: one outbound store held by on_outbound_store, and a reset_to issued during the hold:
// the store's store_applied is stamped before the reset_to's entry in StoreLog::writes, and its
// store_returned after it. A later store that on_outbound_store does not select records
// neither event.
void arm_store_events(asio::io_context& ioc) {
    auto log = std::make_shared<StoreLog>();
    int consulted = 0;
    bool released = false;
    HookedStore::Hooks hooks;
    hooks.on_outbound_store = [&consulted] { return ++consulted == 1; };
    hooks.release_when = [&released] { return released; };
    HookedStore store{1, std::move(hooks), log};
    const std::array<std::byte, 4> frame{std::byte{'3'}, std::byte{'5'}, std::byte{'='},
                                         std::byte{'0'}};

    bool held_done = false;
    std::optional<fixpp::core::expected_t<void>> r_held;
    asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            r_held =
                co_await store.store(1, std::span<const std::byte>{frame}, direction_t::outbound);
            held_done = true;
        },
        asio::detached);
    const bool applied = pump_until(ioc, [&] { return !log->store_events.empty(); }, kHoldWindow);

    bool reset_done = false;
    std::optional<fixpp::core::expected_t<void>> r_reset_to;
    asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            r_reset_to = co_await store.reset_to(1, 2);
            reset_done = true;
        },
        asio::detached);
    const bool reset_finished = pump_until(ioc, [&] { return reset_done; }, kHoldWindow);
    const bool held_done_before_release = held_done;
    const std::size_t events_before_release = log->store_events.size();
    released = true;
    const bool held_finished = pump_until(ioc, [&] { return held_done; }, kReleaseBudget);

    EXPECT_TRUE(applied) << "arm3: the held store did not record store_applied";
    EXPECT_TRUE(reset_finished) << "arm3: the reset_to during the hold did not complete";
    EXPECT_FALSE(held_done_before_release) << "arm3: the selected store did not hold";
    EXPECT_EQ(events_before_release, 1U) << "arm3: only store_applied before the release";
    EXPECT_TRUE(held_finished) << "arm3: the held store did not return after its release";
    EXPECT_FALSE(log->hold_timed_out) << "arm3: the hold ended by its bound, not its release";
    ASSERT_TRUE(r_held.has_value() && r_held->has_value()) << "arm3: the held store failed";
    ASSERT_TRUE(r_reset_to.has_value() && r_reset_to->has_value()) << "arm3: reset_to failed";

    std::optional<std::size_t> reset_to_index;
    for (std::size_t i = 0; i < log->writes.size(); ++i) {
        if (log->writes[i].op == "reset_to 1 2") reset_to_index = i;
    }
    ASSERT_TRUE(reset_to_index.has_value()) << "arm3: the reset_to's entry in StoreLog::writes";
    ASSERT_EQ(log->store_events.size(), 2U) << "arm3: the held store's two events";
    const auto& ev_applied = log->store_events[0];
    const auto& ev_returned = log->store_events[1];
    EXPECT_EQ(ev_applied.kind, StoreLog::store_event_kind::store_applied) << "arm3: first event";
    EXPECT_EQ(ev_returned.kind, StoreLog::store_event_kind::store_returned) << "arm3: second event";
    EXPECT_EQ(ev_applied.seq, seqnum_t{1}) << "arm3: store_applied's seq";
    EXPECT_EQ(ev_returned.seq, seqnum_t{1}) << "arm3: store_returned's seq";
    EXPECT_LE(ev_applied.writes_at, *reset_to_index)
        << "arm3: store_applied is stamped before the reset_to's entry";
    EXPECT_GT(ev_returned.writes_at, *reset_to_index)
        << "arm3: store_returned is stamped after the reset_to's entry";

    // An unheld store: its seq is the store's next outbound number, so it succeeds.
    bool unheld_done = false;
    std::optional<fixpp::core::expected_t<void>> r_unheld;
    asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            auto next = co_await store.next_seqnum(direction_t::outbound, false);
            if (!next) {
                r_unheld = std::unexpected(next.error());
            } else {
                r_unheld = co_await store.store(*next, std::span<const std::byte>{frame},
                                                direction_t::outbound);
            }
            unheld_done = true;
        },
        asio::detached);
    const bool unheld_finished = pump_until(ioc, [&] { return unheld_done; }, kReleaseBudget);
    EXPECT_TRUE(unheld_finished) << "arm3: the unheld store did not complete";
    ASSERT_TRUE(r_unheld.has_value() && r_unheld->has_value()) << "arm3: the unheld store failed";
    EXPECT_EQ(consulted, 2) << "arm3: on_outbound_store is called on every outbound store";
    EXPECT_EQ(log->store_events.size(), 2U) << "arm3: an unheld store records no event";
}

// Arm 4: the write-start count. With arm_block(), one write reads starts 1 and writes 0 while
// parked, and 1 and 1 after release(). The park has no deadline of its own; the pump's bound
// only ends an arm whose write never parks or never completes.
void arm_write_starts(asio::io_context& ioc) {
    auto stream = std::make_shared<fixpp::session::test::ScriptedStream>();
    auto state = std::make_shared<fixpp::session::test::blocking_write_state>();
    fixpp::session::test::BlockingWriteTransport transport{ioc.get_executor(), stream, state};
    const std::array<std::byte, 4> bytes{std::byte{'3'}, std::byte{'5'}, std::byte{'='},
                                         std::byte{'0'}};
    state->arm_block();

    bool done = false;
    std::optional<fixpp::core::expected_t<std::size_t>> r;
    asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            r = co_await transport.async_write(std::span<const std::byte>{bytes});
            done = true;
        },
        asio::detached);
    const bool parked = pump_until(ioc, [&] { return state->parked; }, kReleaseBudget);
    const std::size_t starts_parked = state->starts;
    const std::size_t writes_parked = state->writes;
    state->release();
    const bool finished = pump_until(ioc, [&] { return done; }, kReleaseBudget);
    if (!finished) {
        (void)transport.close();
        (void)pump_until(ioc, [&] { return done; }, kReleaseBudget);
    }

    EXPECT_TRUE(parked) << "arm4: the armed write did not park";
    EXPECT_EQ(starts_parked, 1U) << "arm4: a parked write counts as started";
    EXPECT_EQ(writes_parked, 0U) << "arm4: a parked write is not completed";
    EXPECT_TRUE(finished) << "arm4: the write did not complete after release()";
    EXPECT_EQ(state->starts, 1U) << "arm4: starts after release()";
    EXPECT_EQ(state->writes, 1U) << "arm4: writes after release()";
    ASSERT_TRUE(r.has_value() && r->has_value()) << "arm4: the write failed";
}

TEST(HookedStoreRecord104, ResetAndResetToRecordedApart) {
    {
        asio::io_context ioc;
        arm_record(ioc);
    }
    {
        asio::io_context ioc;
        arm_inbound_persist_call(ioc);
    }
    {
        asio::io_context ioc;
        arm_store_events(ioc);
    }
    {
        asio::io_context ioc;
        arm_write_starts(ioc);
    }
}

}  // namespace

// ── R-1y and R-1z: a too-high Logout's bound under reset_on_logout ──────────────────────

namespace {

namespace rg = fixpp::test_support::resend_gap;
using fixpp::session::close_mode;
using fixpp::session::session_role;
using fixpp::session::session_test_access;
using fixpp::test_support::HookedStoreFactory;

constexpr std::uint32_t kBoundT = 2000;  // the too-high Logout's bound T, ms, on the mock clock
constexpr int kNone = 0;                 // session_test_access::peer_logout_phase's `none`
constexpr int kPending = 1;              // session_test_access::peer_logout_phase's `pending`

// A Scene whose session has reset_on_logout and a HookedStore. Every member a store hook, a
// spawned close or a cell-local delivery touches is declared before the Scene, so it outlives
// whatever the Scene's teardown resumes; the destructor's body releases a hold first.
class Rol104 {
public:
    struct Control {
        bool hold_next_store = false;  // the next outbound store() holds until `released`
        bool released = false;
    };
    struct CloseRun {
        bool done = false;
        std::optional<fixpp::core::expected_t<void>> result;
        std::vector<StoreLog::Write> writes_at_return;  // StoreLog::writes as close() returned
    };
    struct Delivery {
        bool done = false;
        std::optional<fixpp::core::expected_t<void>> first;
        std::optional<fixpp::core::expected_t<void>> second;
    };

    Control ctl;
    CloseRun close_run;
    Delivery delivery;
    std::deque<std::vector<std::byte>> frames;  // a cell-local delivery's bytes
    std::shared_ptr<HookedStoreFactory> factory = std::make_shared<HookedStoreFactory>();
    rg::Scene s;

    explicit Rol104(session_role const role)
        : s{rg::Options{.role = role, .logout_timeout_ms = kBoundT}} {
        factory->hooks.on_outbound_store = [c = &ctl] {
            if (!c->hold_next_store) return false;
            c->hold_next_store = false;
            return true;
        };
        factory->hooks.release_when = [c = &ctl] { return c->released; };
        // These cells read the state at a reset call; the transport reading is not read.
        factory->log->call_snapshot = [this] { return CallReading{.state = s.session->state()}; };
        s.cfg.reset_on_logout = true;
        s.cfg.store_factory = factory;
        s.session = std::make_unique<fixpp::session::Session>(s.engine, s.cfg);
    }
    Rol104(Rol104 const&) = delete;
    Rol104& operator=(Rol104 const&) = delete;
    ~Rol104() { ctl.released = true; }

    [[nodiscard]] StoreLog& log() const { return *factory->log; }
    [[nodiscard]] int phase() const { return session_test_access::peer_logout_phase(*s.session); }
    [[nodiscard]] int bound_count() const {
        return session_test_access::peer_logout_bound_count(*s.session);
    }

    // Starts close(mode); close_run records its result and StoreLog::writes in the handler
    // in which close() returns.
    void spawn_close(close_mode const mode) {
        asio::co_spawn(
            fixpp::test_support::session_executor_of(*s.session, s.ioc),
            [this, mode]() -> asio::awaitable<void> {
                close_run.result = co_await s.session->close(mode);
                close_run.writes_at_return = factory->log->writes;
                close_run.done = true;
            },
            asio::detached);
    }
    // Pumps the close for `budget`; true if it returned. When it has not, the mock clock is
    // moved past logon_timeout_ms (the bound of close()'s wait) and the close is pumped to
    // its end, so the cell ends on its assertion and not on a hang.
    [[nodiscard]] bool close_returned_within(std::chrono::milliseconds const budget) {
        if (s.pump_until([this] { return close_run.done; }, budget)) return true;
        s.mock->advance(std::chrono::milliseconds{s.cfg.logon_timeout_ms} +
                        std::chrono::milliseconds{1});
        (void)s.pump_until([this] { return close_run.done; });
        return false;
    }

    // Delivers two frames back to back from one coroutine, as the read pump does: nothing
    // runs on the context between the first frame's return and the second's start.
    void spawn_one_delivery(std::vector<std::byte> first, std::vector<std::byte> second) {
        frames.push_back(std::move(first));
        frames.push_back(std::move(second));
        auto const a = std::span<const std::byte>{frames[frames.size() - 2]};
        auto const b = std::span<const std::byte>{frames.back()};
        asio::co_spawn(
            fixpp::test_support::session_executor_of(*s.session, s.ioc),
            [this, a, b]() -> asio::awaitable<void> {
                delivery.first = co_await s.session->on_inbound_frame(a);
                delivery.second = co_await s.session->on_inbound_frame(b);
                delivery.done = true;
            },
            asio::detached);
    }

    // The reset_to calls on the record.
    [[nodiscard]] std::vector<StoreLog::Call> reset_tos() const {
        std::vector<StoreLog::Call> out;
        for (auto const& c : log().calls) {
            if (c.kind == StoreLog::call_kind::reset_to) out.push_back(c);
        }
        return out;
    }
    [[nodiscard]] bool close_reset_wait_expired() const {
        for (auto const& ev : s.session->recent_events()) {
            if (std::holds_alternative<fixpp::session::session_event_close_reset_wait_expired>(
                    ev)) {
                return true;
            }
        }
        return false;
    }
};

// Exactly one reset_to(1, 1), and at it the session was Disconnected.
void expect_one_reset_to_one_one_disconnected(Rol104 const& r) {
    auto const calls = r.reset_tos();
    ASSERT_EQ(calls.size(), 1U) << "reset_to calls; reset() calls: " << r.log().reset_calls;
    EXPECT_EQ(calls.front().next_in, seqnum_t{1}) << "the reset_to's inbound target";
    EXPECT_EQ(calls.front().next_out, seqnum_t{1}) << "the reset_to's outbound target";
    ASSERT_TRUE(calls.front().reading.has_value()) << "the reset_to's reading";
    EXPECT_EQ(calls.front().reading->state, fsm_state::Disconnected)
        << "the state at the reset_to: " << rg::state_name(calls.front().reading->state);
}

// R-1y: a second too-high Logout while the mark is pending is ignored, the gap is filled
// past the first S, and the bound acknowledges once and resets once.
void duplicate_too_high_logout(session_role const role, bool const route_a) {
    SCOPED_TRACE(rg::role_name(role));
    Rol104 r{role};
    auto& s = r.s;
    ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
    seqnum_t const n = s.next_in();
    auto const mark = s.sent.size();
    (void)s.feed(s.logout(n + 3));
    ASSERT_EQ(r.phase(), kPending) << "setup: no pending mark";
    ASSERT_EQ(r.bound_count(), 1) << "setup: no bound";
    if (route_a) {
        // The gap is filled to S: the resend closes and the mark stays pending.
        (void)s.feed(s.gap_fill(n, n + 3));
        ASSERT_EQ(s.next_in(), n + 3) << "setup: the gap was not filled to S";
        ASSERT_FALSE(s.resend_outstanding()) << "setup: the resend is still outstanding";
        ASSERT_EQ(r.phase(), kPending) << "setup: the mark left pending at S";
        (void)s.feed(s.logout(n + 5));  // the duplicate, above N
        EXPECT_EQ(r.bound_count(), 1) << "a second bound started";
        (void)s.feed(s.gap_fill(n + 3, n + 6));
    } else {
        ASSERT_TRUE(s.resend_outstanding()) << "setup: no resend outstanding";
        (void)s.feed(s.logout(n + 5));  // the duplicate, with the resend outstanding
        EXPECT_EQ(r.bound_count(), 1) << "a second bound started";
        (void)s.feed(s.gap_fill(n, n + 4));
    }
    EXPECT_EQ(s.count("5", mark), 1U) << "acknowledgement Logouts: " << s.written(mark);
    if (r.bound_count() != 0) {
        // A bound still running is ended at its own deadline, so the cell ends on its
        // assertions and not with the bound alive.
        s.pass(std::chrono::milliseconds{kBoundT});
    }
    EXPECT_EQ(r.bound_count(), 0) << "the bound did not end";
    expect_one_reset_to_one_one_disconnected(r);

    r.spawn_close(close_mode::terminal);
    EXPECT_TRUE(r.close_returned_within(kReleaseBudget)) << "close() did not return";
    EXPECT_EQ(r.log().reset_calls, 0)
        << "a reset() call; reset_to calls: " << r.log().reset_to_calls;
    EXPECT_EQ(r.reset_tos().size(), 1U) << "reset_to calls after close()";
}

// R-1z, CloseDuringReplyStore: the in-sequence Logout at S claims the pending mark; its reply's
// store is held; close(terminal) starts during the hold and returns only after the release,
// with the one reset_to finished by then.
void claims_pending_close_during_reply_store(session_role const role) {
    SCOPED_TRACE(rg::role_name(role));
    Rol104 r{role};
    auto& s = r.s;
    ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
    seqnum_t const n = s.next_in();
    auto const mark = s.sent.size();
    (void)s.feed(s.logout(n + 3));
    ASSERT_EQ(r.phase(), kPending) << "setup: no pending mark";
    (void)s.feed(s.gap_fill(n, n + 3));
    ASSERT_EQ(s.next_in(), n + 3) << "setup: the gap was not filled to S";
    ASSERT_EQ(r.phase(), kPending) << "setup: the mark left pending at S";

    r.ctl.hold_next_store = true;
    auto frame_fut = s.feed_spawn(s.logout(n + 3));
    bool const held = s.pump_until([&] { return !r.log().store_events.empty(); }, kHoldWindow);
    r.spawn_close(close_mode::terminal);
    bool const close_before_release = s.pump_until([&] { return r.close_run.done; }, kHoldWindow);
    r.ctl.released = true;
    bool const close_returned = r.close_returned_within(kReleaseBudget);
    bool const frame_returned =
        s.pump_until([&] { return rg::Scene::ready(frame_fut); }, kReleaseBudget);

    EXPECT_TRUE(held) << "the reply's store was not held";
    EXPECT_EQ(r.phase(), kNone) << "the in-sequence Logout did not claim the mark";
    EXPECT_FALSE(close_before_release) << "close() returned during the reply's held store";
    EXPECT_TRUE(close_returned) << "close() did not return after the release";
    EXPECT_TRUE(frame_returned) << "the Logout's frame did not return";
    EXPECT_FALSE(r.log().hold_timed_out) << "the hold ended by its bound, not its release";
    expect_one_reset_to_one_one_disconnected(r);
    bool finished_before_return = false;
    for (auto const& w : r.close_run.writes_at_return) {
        finished_before_return = finished_before_return || w.op == "reset_to 1 1";
    }
    EXPECT_TRUE(finished_before_return) << "the reset_to had not finished when close() returned";
    EXPECT_EQ(r.log().reset_calls, 0) << "a reset() call";
    EXPECT_FALSE(r.close_reset_wait_expired()) << "close_reset_wait_expired was emitted";
    auto const logouts = s.of_type("5", mark);
    ASSERT_EQ(logouts.size(), 1U) << "acknowledgement Logouts: " << s.written(mark);
    if (!r.log().store_events.empty()) {
        EXPECT_EQ(rg::field(logouts.front(), 34), std::to_string(r.log().store_events.front().seq))
            << "the held store was not the Logout's";
    }
}

// R-1z, OneDelivery: a GapFill past S and the in-sequence Logout at the new N in one delivery.
// The GapFill's advance posts the bound's wake, which has not run when the Logout passes the
// drain test; the Logout's arm claims the mark, the bound exits unacknowledged, and the arm's
// reply is the one acknowledgement and its reset_to the one reset.
void claims_pending_one_delivery(session_role const role) {
    SCOPED_TRACE(rg::role_name(role));
    Rol104 r{role};
    auto& s = r.s;
    ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
    seqnum_t const n = s.next_in();
    auto const mark = s.sent.size();
    (void)s.feed(s.logout(n + 3));
    ASSERT_EQ(r.phase(), kPending) << "setup: no pending mark";
    ASSERT_EQ(r.bound_count(), 1) << "setup: no bound";

    r.spawn_one_delivery(s.gap_fill(n, n + 5), s.logout(n + 5));
    bool const delivered = s.pump_until([&] { return r.delivery.done; }, kReleaseBudget);
    bool const bound_ended = s.pump_until([&] { return r.bound_count() == 0; }, kReleaseBudget);

    EXPECT_TRUE(delivered) << "the delivery did not return";
    EXPECT_TRUE(r.delivery.first.has_value() && r.delivery.first->has_value())
        << "the GapFill's on_inbound_frame";
    EXPECT_TRUE(r.delivery.second.has_value() && r.delivery.second->has_value())
        << "the Logout's on_inbound_frame";
    EXPECT_EQ(r.phase(), kNone) << "the in-sequence Logout did not claim the mark";
    EXPECT_TRUE(bound_ended) << "the bound did not end";
    EXPECT_EQ(s.count("5", mark), 1U) << "acknowledgement Logouts: " << s.written(mark);
    expect_one_reset_to_one_one_disconnected(r);

    r.spawn_close(close_mode::terminal);
    EXPECT_TRUE(r.close_returned_within(kReleaseBudget)) << "close() did not return";
    EXPECT_EQ(r.log().reset_calls, 0) << "a reset() call";
    EXPECT_EQ(r.reset_tos().size(), 1U) << "reset_to calls after close()";
    EXPECT_FALSE(r.close_reset_wait_expired()) << "close_reset_wait_expired was emitted";
}

constexpr session_role kRoles[] = {session_role::initiator, session_role::acceptor};

}  // namespace

TEST(ResetOnLogout104, DuplicateTooHighLogoutWhilePending_OneResetByTheBound_RouteA) {
    for (auto const role : kRoles) duplicate_too_high_logout(role, /*route_a=*/true);
}

TEST(ResetOnLogout104, DuplicateTooHighLogoutWhilePending_OneResetByTheBound_RouteB) {
    for (auto const role : kRoles) duplicate_too_high_logout(role, /*route_a=*/false);
}

TEST(ResetOnLogout104, ClaimsAPendingBound_OneResetByTheArm_CloseDuringReplyStore) {
    for (auto const role : kRoles) claims_pending_close_during_reply_store(role);
}

TEST(ResetOnLogout104, ClaimsAPendingBound_OneResetByTheArm_OneDelivery) {
    for (auto const role : kRoles) claims_pending_one_delivery(role);
}
