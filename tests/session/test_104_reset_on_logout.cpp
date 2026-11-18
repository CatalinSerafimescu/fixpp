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
#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include "support/blocking_write_transport.hpp"
#include "support/hooked_store.hpp"

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
