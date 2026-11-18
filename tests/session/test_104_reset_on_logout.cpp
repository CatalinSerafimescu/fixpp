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
//   - blocking_write_state::starts counts a parked write that has not completed;
//   - blocking_write_state's opt-in byte record holds each write's bytes, in write-start
//     order, only while record_bytes is set.
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
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/message_store_factory.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_event.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/wire/offset_table.hpp>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "session/mock_reconnect_factory.hpp"
#include "session/plain_engine_rig.hpp"
#include "session/resend_gap_scene.hpp"
#include "support/blocking_write_transport.hpp"
#include "support/engine_test_access.hpp"
#include "support/hooked_store.hpp"
#include "support/seqnum_manager_test_access.hpp"
#include "support/session_executor_of.hpp"
#include "support/session_test_access.hpp"
#include "support/store_double.hpp"
#include "support/transport_double.hpp"

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

// The value of `tag` in a recorded write's bytes (its first occurrence after a SOH), or empty.
std::string recorded_field(std::vector<std::byte> const& bytes, std::string_view tag) {
    std::string const wire(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::string const needle = "\x01" + std::string{tag} + "=";
    auto const at = wire.find(needle);
    if (at == std::string::npos) return {};
    auto const from = at + needle.size();
    auto const end = wire.find('\x01', from);
    return wire.substr(from, end == std::string::npos ? std::string::npos : end - from);
}

// Arm 5: the opt-in byte record (research D10, the fifth test-support addition). With
// record_bytes set, two writes are recorded in write-start order, each with its MsgType, its
// start index and its completion; with it clear, a write records nothing.
void arm_byte_record(asio::io_context& ioc) {
    auto stream = std::make_shared<fixpp::session::test::ScriptedStream>();
    auto state = std::make_shared<fixpp::session::test::blocking_write_state>();
    fixpp::session::test::BlockingWriteTransport transport{ioc.get_executor(), stream, state};
    auto const as_bytes = [](std::string_view const t) {
        std::vector<std::byte> out;
        for (char const c : t) out.push_back(static_cast<std::byte>(c));
        return out;
    };
    auto const first = as_bytes(
        "8=FIX.4.2\x01"
        "35=0\x01"
        "34=7\x01");
    auto const second = as_bytes(
        "8=FIX.4.2\x01"
        "35=3\x01"
        "34=8\x01");
    auto const third = as_bytes(
        "8=FIX.4.2\x01"
        "35=5\x01"
        "34=9\x01");

    bool done = false;
    asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            state->record_bytes = true;
            (void)co_await transport.async_write(std::span<const std::byte>{first});
            (void)co_await transport.async_write(std::span<const std::byte>{second});
            state->record_bytes = false;
            (void)co_await transport.async_write(std::span<const std::byte>{third});
            done = true;
        },
        asio::detached);
    bool const finished = pump_until(ioc, [&] { return done; }, kReleaseBudget);

    EXPECT_TRUE(finished) << "arm5: the writes did not complete";
    EXPECT_EQ(state->starts, 3U) << "arm5: starts";
    ASSERT_EQ(state->recorded.size(), 2U) << "arm5: the record holds the two recorded writes";
    EXPECT_EQ(recorded_field(state->recorded[0].bytes, "35"), "0") << "arm5: the first's 35";
    EXPECT_EQ(recorded_field(state->recorded[0].bytes, "34"), "7") << "arm5: the first's 34";
    EXPECT_EQ(recorded_field(state->recorded[1].bytes, "35"), "3") << "arm5: the second's 35";
    EXPECT_EQ(recorded_field(state->recorded[1].bytes, "34"), "8") << "arm5: the second's 34";
    EXPECT_EQ(state->recorded[0].start, 1U) << "arm5: the first's start index";
    EXPECT_EQ(state->recorded[1].start, 2U) << "arm5: the second's start index";
    EXPECT_TRUE(state->recorded[0].completed) << "arm5: the first's completion";
    EXPECT_TRUE(state->recorded[1].completed) << "arm5: the second's completion";
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
    {
        asio::io_context ioc;
        arm_byte_record(ioc);
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

// Counts fromAdmin calls by MsgType, as 103's TooHighLogoutState103 cells do. A cell can make
// fromAdmin throw on or reject a Logout(35=5), make toAdmin throw on one or on a Reject(35=3),
// run a hook at each toAdmin, read the manager's numbers inside onLogout, and read the state
// inside onCreate.
class AdminCountingApp104 final : public rg::GapApp {
public:
    enum class on_logout_from_admin : std::uint8_t { accept, throw_, reject };
    on_logout_from_admin logout_from_admin = on_logout_from_admin::accept;
    bool throw_on_logout_to_admin = false;
    bool throw_on_reject_to_admin = false;
    // Run inside each toAdmin with its MsgType, after the call is counted and before a throw.
    std::function<void(std::string_view)> to_admin_hook;
    fixpp::session::Session const* session = nullptr;  // read inside onLogout and onCreate
    int on_create_calls = 0;
    std::optional<fsm_state> state_at_create;  // session->state() inside the first onCreate
    int on_logout_calls = 0;

    std::vector<std::string> from_admin_types;
    struct Numbers {
        seqnum_t next_in = 0;
        seqnum_t next_out = 0;
    };
    std::vector<Numbers> on_logout_numbers;  // one entry per onLogout call

    fixpp::core::expected_t<void> fromAdmin(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
        const fixpp::session::SessionId& /*id*/) override {
        from_admin_types.emplace_back(msg.msg_type());
        if (msg.msg_type() == "5") {
            if (logout_from_admin == on_logout_from_admin::throw_) {
                throw std::runtime_error("AdminCountingApp104: fromAdmin");
            }
            if (logout_from_admin == on_logout_from_admin::reject) {
                return std::unexpected(fixpp::core::error::app_do_not_send);
            }
        }
        return {};
    }
    void toAdmin(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
                 const fixpp::session::SessionId& id) override {
        GapApp::toAdmin(msg, id);
        if (to_admin_hook) to_admin_hook(msg.msg_type());
        if (throw_on_logout_to_admin && msg.msg_type() == "5") {
            throw std::runtime_error("AdminCountingApp104: toAdmin");
        }
        if (throw_on_reject_to_admin && msg.msg_type() == "3") {
            throw std::runtime_error("AdminCountingApp104: toAdmin on a Reject");
        }
    }
    void onCreate(const fixpp::session::SessionId& /*id*/) override {
        ++on_create_calls;
        if (session != nullptr && !state_at_create) state_at_create = session->state();
    }
    std::function<void()> on_logout_hook;  // run inside each onLogout, after the reading
    void onLogout(const fixpp::session::SessionId& /*id*/) override {
        ++on_logout_calls;
        if (on_logout_hook) on_logout_hook();
        if (session == nullptr) return;
        auto& m = session_test_access::seqnum_mgr(const_cast<fixpp::session::Session&>(*session));
        on_logout_numbers.push_back(
            {.next_in = m.next_inbound_unsafe(), .next_out = m.next_outbound_unsafe()});
    }
    [[nodiscard]] int from_admin_count(std::string_view msg_type) const {
        int n = 0;
        for (auto const& t : from_admin_types) n += static_cast<int>(t == msg_type);
        return n;
    }
};

// A MessageStore that forwards to a shared StoreDouble, so a cell keeps the double after the
// session is gone. It reports itself persistent, as StoreDouble's factories do by default.
class SharedStoreDouble104 final : public fixpp::session::MessageStore {
public:
    explicit SharedStoreDouble104(std::shared_ptr<fixpp::session::test::StoreDouble> d) noexcept
        : fixpp::session::MessageStore(flush_thunk_for<SharedStoreDouble104>()), d_{std::move(d)} {}
    asio::awaitable<fixpp::core::expected_t<void>> store(seqnum_t seq,
                                                         std::span<const std::byte> frame,
                                                         direction_t dir) noexcept override {
        return d_->store(seq, frame, dir);
    }
    asio::awaitable<fixpp::core::expected_t<void>> retrieve(
        seqnum_t begin, seqnum_t end, direction_t dir,
        fixpp::session::retrieve_visitor& visitor) noexcept override {
        return d_->retrieve(begin, end, dir, visitor);
    }
    asio::awaitable<fixpp::core::expected_t<seqnum_t>> next_seqnum(
        direction_t dir, bool increment) noexcept override {
        return d_->next_seqnum(dir, increment);
    }
    asio::awaitable<fixpp::core::expected_t<void>> reset() noexcept override { return d_->reset(); }

private:
    std::shared_ptr<fixpp::session::test::StoreDouble> d_;
};
class StoreDoubleFactory104 final : public fixpp::session::MessageStoreFactory {
public:
    std::shared_ptr<fixpp::session::test::StoreDouble> store =
        std::make_shared<fixpp::session::test::StoreDouble>();
    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::session::MessageStore>> make(
        std::string_view, std::string_view, std::pmr::memory_resource*, std::size_t,
        asio::any_io_executor) noexcept override {
        return std::make_unique<SharedStoreDouble104>(store);
    }
};

// tasks.md T028's store double: a HookedStore behind a store whose next armed outbound
// store() is refused without reaching the HookedStore, with store_cancelled or with
// store_io_failure, or (io_failure_after_store, tasks.md T038's R-5b) forwarded to the
// HookedStore and then answered store_io_failure, so the inner store has taken the number.
// Every other call, the reset calls and the flush included, goes to the HookedStore, so its
// per-call record and its log read as for a HookedStore.
//
// T038 adds two readings on the same double, through StoreSide104: the MsgType of every
// outbound store() call, recorded at the call whatever its outcome; and one selected outbound
// next_seqnum(_, false), which the HookedStore holds (its on_outbound_read, armed here), with
// the value it returned and StoreLog::writes.size() at its return.
enum class refuse_mode : std::uint8_t { none, cancelled, io_failure, io_failure_after_store };
struct StoreSide104 {
    bool hold_next_read = false;   // select the next outbound next_seqnum(_, false)
    bool arm_hooked_hold = false;  // read by the HookedStore's on_outbound_read
    bool read_selected = false;    // the selected read was issued
    std::optional<seqnum_t> held_read_value;
    std::optional<std::size_t> held_read_returned_at;  // StoreLog::writes.size() at its return
    std::vector<std::string> outbound_store_types;     // 35 of each outbound store() call
};
class RefusingStore104 final : public fixpp::session::MessageStore {
public:
    RefusingStore104(std::unique_ptr<HookedStore> inner, refuse_mode* next, StoreSide104* side,
                     StoreLog* log) noexcept
        : fixpp::session::MessageStore(flush_thunk_for<RefusingStore104>()),
          inner_{std::move(inner)},
          next_{next},
          side_{side},
          log_{log} {}
    asio::awaitable<fixpp::core::expected_t<void>> store(seqnum_t seq,
                                                         std::span<const std::byte> frame,
                                                         direction_t dir) noexcept override {
        if (dir == direction_t::outbound) {
            side_->outbound_store_types.push_back(rg::field(
                std::string_view{reinterpret_cast<const char*>(frame.data()), frame.size()}, 35));
        }
        if (dir == direction_t::outbound && *next_ != refuse_mode::none) {
            auto const m = *next_;
            *next_ = refuse_mode::none;
            if (m == refuse_mode::io_failure_after_store) return stored_then_failed(seq, frame);
            return refused(m);
        }
        return inner_->store(seq, frame, dir);
    }
    asio::awaitable<fixpp::core::expected_t<void>> retrieve(
        seqnum_t begin, seqnum_t end, direction_t dir,
        fixpp::session::retrieve_visitor& visitor) noexcept override {
        return inner_->retrieve(begin, end, dir, visitor);
    }
    asio::awaitable<fixpp::core::expected_t<seqnum_t>> next_seqnum(
        direction_t dir, bool increment) noexcept override {
        if (dir == direction_t::outbound && !increment && side_->hold_next_read) {
            side_->hold_next_read = false;
            side_->read_selected = true;
            return held_read();
        }
        return inner_->next_seqnum(dir, increment);
    }
    asio::awaitable<fixpp::core::expected_t<void>> reset() noexcept override {
        return inner_->reset();
    }
    asio::awaitable<fixpp::core::expected_t<void>> reset_to(seqnum_t next_in,
                                                            seqnum_t next_out) noexcept override {
        return inner_->reset_to(next_in, next_out);
    }
    asio::awaitable<fixpp::core::expected_t<void>> flush_for_session_close() {
        return inner_->flush_for_session_close();
    }

private:
    static asio::awaitable<fixpp::core::expected_t<void>> refused(refuse_mode const m) {
        co_return std::unexpected(m == refuse_mode::cancelled
                                      ? fixpp::core::error::store_cancelled
                                      : fixpp::core::error::store_io_failure);
    }
    asio::awaitable<fixpp::core::expected_t<void>> stored_then_failed(
        seqnum_t const seq, std::span<const std::byte> frame) {
        (void)co_await inner_->store(seq, frame, direction_t::outbound);
        co_return std::unexpected(fixpp::core::error::store_io_failure);
    }
    // The HookedStore's next_seqnum reads arm_hooked_hold synchronously when it is called, so
    // the hold it selects is this read's.
    asio::awaitable<fixpp::core::expected_t<seqnum_t>> held_read() {
        side_->arm_hooked_hold = true;
        auto r = co_await inner_->next_seqnum(direction_t::outbound, false);
        if (r) side_->held_read_value = *r;
        side_->held_read_returned_at = log_->writes.size();
        co_return r;
    }
    std::unique_ptr<HookedStore> inner_;
    refuse_mode* next_;
    StoreSide104* side_;
    StoreLog* log_;
};
class RefusingStoreFactory104 final : public fixpp::session::MessageStoreFactory {
public:
    RefusingStoreFactory104(std::shared_ptr<HookedStoreFactory> hooked, refuse_mode* next,
                            StoreSide104* side)
        : hooked_{std::move(hooked)}, next_{next}, side_{side} {}
    [[nodiscard]] bool yields_persistent_store() const noexcept override { return true; }
    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::session::MessageStore>> make(
        std::string_view, std::string_view, std::pmr::memory_resource*, std::size_t,
        asio::any_io_executor) noexcept override {
        ++hooked_->log->stores_made;
        return std::make_unique<RefusingStore104>(
            std::make_unique<HookedStore>(hooked_->outbound_next, std::move(hooked_->hooks),
                                          hooked_->log, hooked_->mode),
            next_, side_, hooked_->log.get());
    }

private:
    std::shared_ptr<HookedStoreFactory> hooked_;
    refuse_mode* next_;
    StoreSide104* side_;
};

enum class store_kind : std::uint8_t { hooked, memory, store_double, refusing };

// What a Rol104 builds; the defaults are R-1y's and R-1z's.
struct Opts104 {
    session_role role = session_role::initiator;
    bool reset_on_logout = true;
    bool reset_on_disconnect = false;
    store_kind store = store_kind::hooked;
    std::uint32_t logout_timeout_ms = kBoundT;
    bool with_clock = true;
    bool fail_held = false;  // HookedStore's fail_held: a held operation returns store_io_failure
    // When set, the session's TransportFactory (SessionConfig::transport_factory_override), so a
    // standalone initiator can drive_reconnect().
    std::shared_ptr<fixpp::transport::TransportFactory> transport_factory = nullptr;
    // The session's HeartBtInt and the HeartBtInt(108) of the peer's Logon.
    std::chrono::seconds heartbeat = rg::kHeartBtInt;
};

// A Scene whose session has reset_on_logout and a HookedStore. Every member a store hook, a
// spawned close or a cell-local delivery touches is declared before the Scene, so it outlives
// whatever the Scene's teardown resumes; the destructor's body releases a hold first.
class Rol104 {
public:
    struct Control {
        bool hold_next_store = false;    // the next outbound store() holds until `released`
        bool hold_next_persist = false;  // the next inbound persist holds until `released`
        bool released = false;
        bool throw_next_write = false;  // the config-time sink throws on its next write
        refuse_mode refuse_next_store = refuse_mode::none;  // store_kind::refusing only
        StoreSide104 side;                                  // store_kind::refusing only
    };
    struct CloseRun {
        bool started = false;  // set when spawn_close() is called, and never cleared
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
    std::shared_ptr<StoreDoubleFactory104> double_factory =
        std::make_shared<StoreDoubleFactory104>();
    std::shared_ptr<AdminCountingApp104> app = std::make_shared<AdminCountingApp104>();
    rg::Scene s;

    explicit Rol104(session_role const role) : Rol104(Opts104{.role = role}) {}
    explicit Rol104(Opts104 const& o)
        : s{rg::Options{.role = o.role,
                        .heartbeat = o.heartbeat,
                        .with_clock = o.with_clock,
                        .app = app,
                        .with_store = o.store == store_kind::memory,
                        .logout_timeout_ms = o.logout_timeout_ms}} {
        factory->hooks.on_outbound_store = [c = &ctl] {
            if (!c->hold_next_store) return false;
            c->hold_next_store = false;
            return true;
        };
        factory->hooks.release_when = [c = &ctl] { return c->released; };
        factory->hooks.on_inbound_persist_call = [c = &ctl] {
            if (!c->hold_next_persist) return false;
            c->hold_next_persist = false;
            return true;
        };
        factory->hooks.on_outbound_read = [c = &ctl] {
            if (!c->side.arm_hooked_hold) return false;
            c->side.arm_hooked_hold = false;
            return true;
        };
        factory->hooks.fail_held = o.fail_held;
        // These cells read the state at a reset call; the transport reading is not read.
        factory->log->call_snapshot = [this] { return CallReading{.state = s.session->state()}; };
        s.cfg.reset_on_logout = o.reset_on_logout;
        s.cfg.reset_on_disconnect = o.reset_on_disconnect;
        if (o.store == store_kind::hooked) s.cfg.store_factory = factory;
        if (o.store == store_kind::store_double) s.cfg.store_factory = double_factory;
        if (o.transport_factory) {
            s.cfg.transport_factory_override = o.transport_factory;
            s.cfg.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 19099};
        }
        if (o.store == store_kind::refusing) {
            s.cfg.store_factory = std::make_shared<RefusingStoreFactory104>(
                factory, &ctl.refuse_next_store, &ctl.side);
        }
        s.cfg.transport_send = [c = &ctl,
                                sink = s.cfg.transport_send](std::span<const std::byte> f) {
            if (c->throw_next_write) {
                c->throw_next_write = false;
                throw std::runtime_error("Rol104: the sink's write failed");
            }
            sink(f);
        };
        s.session = std::make_unique<fixpp::session::Session>(s.engine, s.cfg);
        app->session = s.session.get();
    }
    Rol104(Rol104 const&) = delete;
    Rol104& operator=(Rol104 const&) = delete;
    ~Rol104() { ctl.released = true; }

    [[nodiscard]] StoreLog& log() const { return *factory->log; }

    struct Numbers {
        seqnum_t in = 0;
        seqnum_t out = 0;
        bool operator==(Numbers const&) const = default;
    };
    [[nodiscard]] Numbers manager_numbers() const {
        auto& m = session_test_access::seqnum_mgr(*s.session);
        return {.in = m.next_inbound_unsafe(), .out = m.next_outbound_unsafe()};
    }
    // The store's next inbound and outbound numbers: a StoreDouble's directly, a MemoryStore's
    // (the HookedStore's inner one, or the session's own) through next_seqnum(dir, false),
    // pumped. Empty when a read fails or does not return.
    [[nodiscard]] std::optional<Numbers> store_numbers(store_kind const k) {
        if (k == store_kind::store_double) {
            return Numbers{.in = double_factory->store->current_next_inbound(),
                           .out = double_factory->store->current_next_outbound()};
        }
        fixpp::session::MessageStore* st = k == store_kind::hooked || k == store_kind::refusing
                                               ? factory->log->inner.get()
                                               : session_test_access::store(*s.session);
        if (st == nullptr) return std::nullopt;
        auto const read = std::make_shared<std::optional<Numbers>>();
        auto const done = std::make_shared<bool>(false);
        asio::co_spawn(
            s.ioc,
            [st, read, done]() -> asio::awaitable<void> {
                auto in = co_await st->next_seqnum(direction_t::inbound, false);
                auto out = co_await st->next_seqnum(direction_t::outbound, false);
                if (in && out) *read = Numbers{.in = *in, .out = *out};
                *done = true;
            },
            asio::detached);
        if (!s.pump_until([&] { return *done; }, kReleaseBudget)) return std::nullopt;
        return *read;
    }
    [[nodiscard]] int phase() const { return session_test_access::peer_logout_phase(*s.session); }
    [[nodiscard]] int bound_count() const {
        return session_test_access::peer_logout_bound_count(*s.session);
    }

    // Starts close(mode); close_run records its result and StoreLog::writes in the handler
    // in which close() returns.
    void spawn_close(close_mode const mode) {
        close_run.started = true;
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
    // Whether A's reply is written under the in-flight close(terminal) is not pinned here; when
    // one is written, the held store was its store.
    auto const logouts = s.of_type("5", mark);
    if (!logouts.empty() && !r.log().store_events.empty()) {
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
    // Reach by the arm: only the in-sequence Logout's arm hands the frame to fromAdmin; a frame
    // drained at the drain test never reaches it.
    EXPECT_EQ(r.app->from_admin_count("5"), 1) << "the in-sequence Logout's fromAdmin calls";
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

// ── T024: R-1a and R-1b, an unanswered Logout under reset_on_logout ──────────────────────

namespace {

using Numbers104 = Rol104::Numbers;

// close(graceful)'s phase 1 waits for the peer's Logout on close_grace, a real-time timer of
// logout_disconnect_timeout_ms. R-1a's waits are ratios of it: a quarter for the Logout's emit,
// four times it for close() to return past the grace.
constexpr std::uint32_t kGraceMs = 400;

std::string_view store_name(store_kind const k) {
    switch (k) {
        case store_kind::hooked:
            return "HookedStore";
        case store_kind::memory:
            return "MemoryStore";
        case store_kind::store_double:
            return "StoreDouble";
        case store_kind::refusing:
            return "RefusingStore";
    }
    return "?";
}

// R-1a, one role and store. `answered` is the forced-hit arm (R-1c's input): the peer answers
// the Logout in LogoutSent, and the numbers end at (1, 1), which shows the cell's reading can
// see a reset. The unanswered arm's numbers after the emit are past 1 on both sides (the
// session has sent its Logon and its Logout and received the peer's Logon), so "equal to the
// numbers after the emit" cannot hold by a reset.
void unanswered_timeout(session_role const role, store_kind const store, bool const answered) {
    SCOPED_TRACE(std::string{rg::role_name(role)} + "/" + std::string{store_name(store)} +
                 (answered ? "/answered" : "/unanswered"));
    Rol104 r{Opts104{.role = role, .store = store, .logout_timeout_ms = kGraceMs}};
    auto& s = r.s;
    ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
    auto const mark = s.sent.size();
    r.spawn_close(close_mode::graceful);
    bool const emitted =
        s.pump_until([&] { return s.state() == fsm_state::LogoutSent && s.count("5", mark) == 1U; },
                     std::chrono::milliseconds{kGraceMs} / 4);
    // Before the timeout: the Logout was sent, so "no reset" cannot come from a not-sent route.
    ASSERT_TRUE(emitted) << "setup: no 35=5 captured in LogoutSent; the session wrote "
                         << s.written(mark) << " and is " << rg::state_name(s.state());
    Numbers104 const mgr_after_emit = r.manager_numbers();
    auto const store_after_emit = r.store_numbers(store);
    ASSERT_TRUE(store_after_emit.has_value()) << "setup: the store's numbers were not read";
    if (answered) (void)s.feed(s.logout(mgr_after_emit.in));

    EXPECT_TRUE(r.close_returned_within(4 * std::chrono::milliseconds{kGraceMs}))
        << "close() did not return";
    Numbers104 const mgr_end = r.manager_numbers();
    auto const store_end = r.store_numbers(store);
    ASSERT_TRUE(store_end.has_value()) << "the store's numbers were not read after close()";
    if (answered) {
        EXPECT_EQ(mgr_end, (Numbers104{.in = 1, .out = 1})) << "the answered twin's manager";
        EXPECT_EQ(*store_end, (Numbers104{.in = 1, .out = 1})) << "the answered twin's store";
        return;
    }
    EXPECT_EQ(mgr_end, mgr_after_emit)
        << "manager numbers after close(): " << mgr_end.in << "/" << mgr_end.out
        << ", after the Logout's emit: " << mgr_after_emit.in << "/" << mgr_after_emit.out;
    EXPECT_EQ(*store_end, *store_after_emit)
        << "store numbers after close(): " << store_end->in << "/" << store_end->out
        << ", after the Logout's emit: " << store_after_emit->in << "/" << store_after_emit->out;
    if (store == store_kind::store_double) {
        EXPECT_EQ(r.double_factory->store->reset_call_count(), 0U)
            << "a reset() or reset_to call reached the StoreDouble";
    }
}

namespace plain_rig = fixpp::test_support::plain_rig;

// R-1b's grace: close_grace, real time. The cell's wait for the drop's close is a quarter of
// it, so a close that the grace ends cannot meet the wait.
constexpr std::uint32_t kDropGraceMs = 4000;

// R-1b on an engine-managed initiator: close(graceful) sends the Logout, the peer drops the
// connection in LogoutSent (or, in the forced-hit arm, answers it), and close() returns.
void unanswered_transport_drop(bool const answered) {
    SCOPED_TRACE(answered ? "answered" : "dropped");
    auto const app = std::make_shared<plain_rig::CountingApp>();
    auto const factory = std::make_shared<HookedStoreFactory>();
    plain_rig::Rig rig{app};
    auto c = rig.cfg(session_role::initiator);
    c.reset_on_logout = true;
    c.store_factory = factory;
    c.logout_disconnect_timeout_ms = kDropGraceMs;
    ASSERT_TRUE(rig.start(c)) << "setup: the engine did not start";
    ASSERT_TRUE(rig.to_active()) << "setup: the session did not reach Active";
    auto const sess = rig.session();
    ASSERT_NE(sess, nullptr);

    struct CloseRun {
        bool done = false;
        std::chrono::steady_clock::time_point at{};
    };
    auto const run = std::make_shared<CloseRun>();
    auto const started = std::chrono::steady_clock::now();
    asio::any_io_executor const ex = sess->executor().underlying();
    asio::co_spawn(
        ex,
        [sess, run]() -> asio::awaitable<void> {
            (void)co_await sess->close(close_mode::graceful);
            run->at = std::chrono::steady_clock::now();
            run->done = true;
        },
        asio::detached);
    bool const emitted = rig.run_until(
        [&] {
            return rig.state() == fsm_state::LogoutSent &&
                   plain_rig::frames_of_type(rig.peer.received, "5").size() == 1U;
        },
        std::chrono::milliseconds{kDropGraceMs} / 4);
    ASSERT_TRUE(emitted) << "setup: no 35=5 reached the peer in LogoutSent";

    auto read_store = [&]() -> std::optional<Numbers104> {
        auto const st = factory->log->inner;
        auto const out = std::make_shared<std::optional<Numbers104>>();
        auto const done = std::make_shared<bool>(false);
        asio::co_spawn(
            rig.ioc,
            [st, out, done]() -> asio::awaitable<void> {
                auto in = co_await st->next_seqnum(direction_t::inbound, false);
                auto o = co_await st->next_seqnum(direction_t::outbound, false);
                if (in && o) *out = Numbers104{.in = *in, .out = *o};
                *done = true;
            },
            asio::detached);
        if (!rig.run_until([&] { return *done; }, kReleaseBudget)) return std::nullopt;
        return *out;
    };
    auto mgr = [&] {
        auto& m = session_test_access::seqnum_mgr(*sess);
        return Numbers104{.in = m.next_inbound_unsafe(), .out = m.next_outbound_unsafe()};
    };
    Numbers104 const mgr_after_emit = mgr();
    auto const store_after_emit = read_store();
    ASSERT_TRUE(store_after_emit.has_value()) << "setup: the store's numbers were not read";

    if (answered) {
        rig.peer.send(rig.msg("5", mgr_after_emit.in));
    } else {
        rig.peer.close();
    }
    bool const closed =
        rig.run_until([&] { return run->done; }, std::chrono::milliseconds{kDropGraceMs} / 4);
    EXPECT_TRUE(closed) << "close() did not return within a quarter of the grace";
    EXPECT_LT(run->at - started, std::chrono::milliseconds{kDropGraceMs})
        << "close() returned at the grace deadline: the timer, not the drop, ended it";
    Numbers104 const mgr_end = mgr();
    auto const store_end = read_store();
    ASSERT_TRUE(store_end.has_value()) << "the store's numbers were not read after close()";
    if (answered) {
        EXPECT_EQ(mgr_end, (Numbers104{.in = 1, .out = 1})) << "the answered twin's manager";
        EXPECT_EQ(*store_end, (Numbers104{.in = 1, .out = 1})) << "the answered twin's store";
    } else {
        EXPECT_EQ(mgr_end, mgr_after_emit)
            << "manager numbers after close(): " << mgr_end.in << "/" << mgr_end.out;
        EXPECT_EQ(*store_end, *store_after_emit)
            << "store numbers after close(): " << store_end->in << "/" << store_end->out;
        EXPECT_TRUE(factory->log->calls.empty())
            << "a reset() or reset_to call: reset() " << factory->log->reset_calls << ", reset_to "
            << factory->log->reset_to_calls;
    }
    rig.stop();
}

}  // namespace

TEST(ResetOnLogout104, Unanswered_Timeout_NoReset) {
    for (auto const role : kRoles) {
        for (auto const store : {store_kind::memory, store_kind::store_double}) {
            unanswered_timeout(role, store, /*answered=*/false);
            unanswered_timeout(role, store, /*answered=*/true);
        }
    }
}

TEST(ResetOnLogout104, Unanswered_TransportDrop_NoReset) {
    unanswered_transport_drop(/*answered=*/false);
    unanswered_transport_drop(/*answered=*/true);
}

// ── T025: R-1c, R-1d and R-1e, the reset at receipt ──────────────────────────────────────
//
// These cells read whether the transport is open at a reset call, so they run an Engine
// session on plain_rig, whose peer socket can see the session's end close
// (plain_rig::peer_sees_hang_up). HookedStore's per-call record takes that reading and the
// state at each reset() and reset_to call.

namespace {

// One engine-managed session on plain_rig with reset_on_logout, a HookedStore and an
// admin-counting application. Members the hooks and spawned coroutines touch are declared
// before the Rig.
class Live104 {
public:
    struct CloseRun {
        bool done = false;
        std::vector<StoreLog::Write> writes_at_return;
    };
    Rol104::Control ctl;
    std::shared_ptr<CloseRun> close_run = std::make_shared<CloseRun>();
    std::shared_ptr<HookedStoreFactory> factory = std::make_shared<HookedStoreFactory>();
    std::shared_ptr<AdminCountingApp104> app = std::make_shared<AdminCountingApp104>();
    plain_rig::Rig rig{app};
    std::shared_ptr<fixpp::session::Session> sess;

    explicit Live104(session_role const role, std::uint32_t const logout_timeout_ms = kBoundT) {
        factory->hooks.on_outbound_store = [c = &ctl] {
            if (!c->hold_next_store) return false;
            c->hold_next_store = false;
            return true;
        };
        factory->hooks.release_when = [c = &ctl] { return c->released; };
        factory->log->call_snapshot = [this] {
#if defined(FIXPP_TEST_PEER_HANG_UP_READING)
            bool const open = !plain_rig::peer_sees_hang_up(rig.peer);
#else
            bool const open = false;
#endif
            return CallReading{.state = sess ? sess->state() : fsm_state::NotConnected,
                               .transport_open = open};
        };
        cfg_ = rig.cfg(role);
        cfg_.reset_on_logout = true;
        cfg_.store_factory = factory;
        cfg_.logout_disconnect_timeout_ms = logout_timeout_ms;
    }
    Live104(Live104 const&) = delete;
    Live104& operator=(Live104 const&) = delete;
    ~Live104() {
        ctl.released = true;
        rig.stop();
    }

    [[nodiscard]] bool start_to_active() {
        // The Engine makes the Session at its connection, so it is looked up once Active.
        if (!rig.start(cfg_) || !rig.to_active()) return false;
        sess = rig.session();
        if (!sess) return false;
        app->session = sess.get();
        return true;
    }
    // The session's config, edited before start_to_active().
    [[nodiscard]] fixpp::session::SessionConfig& config() { return cfg_; }
    [[nodiscard]] StoreLog& log() const { return *factory->log; }
    [[nodiscard]] Rol104::Numbers manager_numbers() const {
        auto& m = session_test_access::seqnum_mgr(*sess);
        return {.in = m.next_inbound_unsafe(), .out = m.next_outbound_unsafe()};
    }
    [[nodiscard]] std::optional<Rol104::Numbers> store_numbers() {
        auto const st = factory->log->inner;
        auto const out = std::make_shared<std::optional<Rol104::Numbers>>();
        auto const done = std::make_shared<bool>(false);
        asio::co_spawn(
            rig.ioc,
            [st, out, done]() -> asio::awaitable<void> {
                auto in = co_await st->next_seqnum(direction_t::inbound, false);
                auto o = co_await st->next_seqnum(direction_t::outbound, false);
                if (in && o) *out = Rol104::Numbers{.in = *in, .out = *o};
                *done = true;
            },
            asio::detached);
        if (!rig.run_until([&] { return *done; }, kReleaseBudget)) return std::nullopt;
        return *out;
    }
    [[nodiscard]] std::vector<std::string> peer_frames(std::string_view msg_type) const {
        return plain_rig::frames_of_type(rig.peer.received, msg_type);
    }
    void spawn_close(close_mode const mode) {
        asio::any_io_executor const ex = sess->executor().underlying();
        auto const s = sess;
        auto const run = close_run;
        auto const lg = factory->log;
        asio::co_spawn(
            ex,
            [s, run, lg, mode]() -> asio::awaitable<void> {
                (void)co_await s->close(mode);
                run->writes_at_return = lg->writes;
                run->done = true;
            },
            asio::detached);
    }
    [[nodiscard]] bool close_returned_within(std::chrono::milliseconds const budget) {
        if (rig.run_until([&] { return close_run->done; }, budget)) return true;
        rig.clock->advance(std::chrono::milliseconds{cfg_.logon_timeout_ms} +
                           std::chrono::milliseconds{1});
        (void)rig.run_until([&] { return close_run->done; });
        return false;
    }
    [[nodiscard]] std::string gap_fill(std::uint32_t begin, std::uint32_t new_seq_no) const {
        return rig.msg("4", begin,
                       "43=Y\x01"
                       "122=" +
                           rig.sending_time() + "\x01" + "123=Y\x01" +
                           "36=" + std::to_string(new_seq_no) + "\x01");
    }

private:
    fixpp::session::SessionConfig cfg_;
};

// The reset of a receipt: the record holds a single call, a reset_to(1, 1) seen with the
// session Disconnected and its transport open.
void expect_receipt_reset(StoreLog const& log) {
    ASSERT_EQ(log.calls.size(), 1U)
        << "reset calls: reset() " << log.reset_calls << ", reset_to " << log.reset_to_calls;
    auto const& c = log.calls.front();
    EXPECT_EQ(c.kind, StoreLog::call_kind::reset_to) << "the reset was a reset(), not the unit";
    EXPECT_EQ(c.next_in, seqnum_t{1});
    EXPECT_EQ(c.next_out, seqnum_t{1});
    ASSERT_TRUE(c.reading.has_value());
    EXPECT_EQ(c.reading->state, fsm_state::Disconnected)
        << "the state at the reset: " << rg::state_name(c.reading->state);
#if defined(FIXPP_TEST_PEER_HANG_UP_READING)
    EXPECT_TRUE(c.reading->transport_open) << "the transport was closed at the reset";
#else
    std::cout << "[ READING  ] the transport reading is not made on this platform\n";
#endif
}

}  // namespace

// R-1c: close(graceful) sends the Logout, the peer answers in LogoutSent, and L's arm resets
// at receipt: reset_to at the hook, Disconnected, the transport open, phase 1 not yet woken.
TEST(ResetOnLogout104, Answered_ResetAtReceipt) {
    // Declared before the session, so the hooks that read them outlive its teardown.
    int woken = 0;
    std::optional<int> woken_at_reset;
    Live104 l{session_role::acceptor};
    ASSERT_TRUE(l.start_to_active()) << "setup: the session did not reach Active";
    l.factory->log->call_snapshot = [inner = l.factory->log->call_snapshot, &woken,
                                     &woken_at_reset] {
        if (!woken_at_reset) woken_at_reset = woken;
        return inner();
    };
    l.spawn_close(close_mode::graceful);
    ASSERT_TRUE(l.rig.run_until([&] {
        return l.rig.state() == fsm_state::LogoutSent && l.peer_frames("5").size() == 1U &&
               static_cast<bool>(session_test_access::logout_wait_wake(*l.sess));
    })) << "setup: phase 1's Logout was not sent";
    auto& wake = session_test_access::logout_wait_wake(*l.sess);
    wake = [orig = wake, &woken] {
        ++woken;
        orig();
    };
    l.rig.peer.send(l.rig.msg("5", l.manager_numbers().in));
    EXPECT_TRUE(l.close_returned_within(4 * std::chrono::milliseconds{kBoundT}))
        << "close() did not return";
    expect_receipt_reset(l.log());
    ASSERT_TRUE(woken_at_reset.has_value()) << "no reset call";
    EXPECT_EQ(*woken_at_reset, 0) << "phase 1 had been woken at the reset";
    EXPECT_EQ(l.manager_numbers(), (Rol104::Numbers{.in = 1, .out = 1}));
    auto const st = l.store_numbers();
    ASSERT_TRUE(st.has_value()) << "the store's numbers were not read";
    EXPECT_EQ(*st, (Rol104::Numbers{.in = 1, .out = 1}));
}

// R-1d: the peer's in-sequence Logout in Active; A's reply carries the pre-reset next
// outbound, onLogout sees the pre-reset numbers, and the reset is at receipt.
TEST(ResetOnLogout104, PeerRequest_ResetAtReceipt) {
    for (auto const role : kRoles) {
        SCOPED_TRACE(rg::role_name(role));
        Live104 l{role};
        ASSERT_TRUE(l.start_to_active()) << "setup: the session did not reach Active";
        auto const before = l.manager_numbers();
        auto const sent_before = l.peer_frames("5").size();
        l.rig.peer.send(l.rig.msg("5", before.in));
        ASSERT_TRUE(l.rig.run_until([&] {
            return l.rig.state() == fsm_state::Disconnected &&
                   l.peer_frames("5").size() == sent_before + 1;
        })) << "setup: A's reply was not written";
        auto const reply = l.peer_frames("5").back();
        EXPECT_EQ(plain_rig::field_value(reply, "34"), std::to_string(before.out))
            << "the reply's MsgSeqNum";
        ASSERT_EQ(l.app->on_logout_numbers.size(), 1U) << "onLogout calls";
        EXPECT_EQ(l.app->on_logout_numbers.front().next_in, before.in + 1)
            << "NextNumIn inside onLogout";
        EXPECT_EQ(l.app->on_logout_numbers.front().next_out, before.out + 1)
            << "the next outbound number inside onLogout";
        l.spawn_close(close_mode::terminal);
        EXPECT_TRUE(l.close_returned_within(kReleaseBudget)) << "close() did not return";
        expect_receipt_reset(l.log());
    }
}

// R-1e: a too-high Logout, the gap filled past S, the bound's acknowledgement and its reset.
TEST(ResetOnLogout104, TooHighLogout_ResetAtAcknowledgement) {
    Live104 l{session_role::acceptor};
    ASSERT_TRUE(l.start_to_active()) << "setup: the session did not reach Active";
    auto const n = l.manager_numbers().in;
    auto const sent_before = l.peer_frames("5").size();
    l.rig.peer.send(l.rig.msg("5", n + 3));
    ASSERT_TRUE(l.rig.run_until([&] {
        return session_test_access::peer_logout_phase(*l.sess) == kPending;
    })) << "setup: no pending mark";
    l.rig.peer.send(l.gap_fill(n, n + 4));
    ASSERT_TRUE(l.rig.run_until([&] {
        return l.rig.state() == fsm_state::Disconnected &&
               l.peer_frames("5").size() == sent_before + 1;
    })) << "setup: the bound did not acknowledge";
    auto const reply = l.peer_frames("5").back();
    ASSERT_EQ(l.app->on_logout_numbers.size(), 1U) << "onLogout calls";
    EXPECT_EQ(l.app->on_logout_numbers.front().next_in, n + 4) << "NextNumIn inside onLogout";
    EXPECT_EQ(std::to_string(l.app->on_logout_numbers.front().next_out - 1),
              plain_rig::field_value(reply, "34"))
        << "the next outbound number inside onLogout";
    l.spawn_close(close_mode::terminal);
    EXPECT_TRUE(l.close_returned_within(kReleaseBudget)) << "close() did not return";
    expect_receipt_reset(l.log());
}

// ── T026: R-1f, a too-high Logout's bound when close() runs ─────────────────────────────

// R-1f(1): close() while the 103 mark is pending claims the bound: no reset_to and no reset,
// the numbers as before, and close() returns with the mock clock unmoved (so below
// logon_timeout_ms) and no close_reset_wait_expired.
TEST(ResetOnLogout104, TooHighLogout_ClaimedByClose_NoReset) {
    for (auto const role : kRoles) {
        SCOPED_TRACE(rg::role_name(role));
        Rol104 r{role};
        auto& s = r.s;
        ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
        seqnum_t const n = s.next_in();
        (void)s.feed(s.logout(n + 3));
        ASSERT_EQ(r.phase(), kPending) << "setup: no pending mark";
        auto const mgr_before = r.manager_numbers();
        auto const store_before = r.store_numbers(store_kind::hooked);
        ASSERT_TRUE(store_before.has_value()) << "setup: the store's numbers were not read";

        r.spawn_close(close_mode::terminal);
        EXPECT_TRUE(r.close_returned_within(kReleaseBudget))
            << "close() did not return with the mock clock unmoved";
        EXPECT_TRUE(r.log().calls.empty()) << "a reset call: reset() " << r.log().reset_calls
                                           << ", reset_to " << r.log().reset_to_calls;
        EXPECT_EQ(r.manager_numbers(), mgr_before) << "the manager's numbers moved";
        auto const store_after = r.store_numbers(store_kind::hooked);
        ASSERT_TRUE(store_after.has_value()) << "the store's numbers were not read";
        EXPECT_EQ(*store_after, *store_before) << "the store's numbers moved";
        EXPECT_FALSE(r.close_reset_wait_expired()) << "close_reset_wait_expired was emitted";
    }
}

// R-1f(2), (1)'s forced-hit arm: close() while the bound is acking, its reply's store held:
// one reset_to, seen with the session Disconnected, finished before close() returns.
TEST(ResetOnLogout104, TooHighLogout_AckingWhenClosed_ResetOnce) {
    constexpr int kAcking = 2;  // session_test_access::peer_logout_phase's `acking`
    for (auto const role : kRoles) {
        SCOPED_TRACE(rg::role_name(role));
        Rol104 r{role};
        auto& s = r.s;
        ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
        seqnum_t const n = s.next_in();
        (void)s.feed(s.logout(n + 3));
        ASSERT_EQ(r.phase(), kPending) << "setup: no pending mark";
        r.ctl.hold_next_store = true;
        auto frame_fut = s.feed_spawn(s.gap_fill(n, n + 4));
        bool const held = s.pump_until(
            [&] { return !r.log().store_events.empty() && r.phase() == kAcking; }, kHoldWindow);
        r.spawn_close(close_mode::terminal);
        (void)s.pump_until([&] { return r.close_run.done; }, kHoldWindow);
        r.ctl.released = true;
        bool const close_returned = r.close_returned_within(kReleaseBudget);
        (void)s.pump_until([&] { return rg::Scene::ready(frame_fut); }, kReleaseBudget);

        ASSERT_TRUE(held) << "setup: the bound's reply store was not held in acking";
        EXPECT_TRUE(close_returned) << "close() did not return after the release";
        EXPECT_FALSE(r.log().hold_timed_out) << "the hold ended by its bound, not its release";
        expect_one_reset_to_one_one_disconnected(r);
        bool finished_before_return = false;
        for (auto const& w : r.close_run.writes_at_return) {
            finished_before_return = finished_before_return || w.op == "reset_to 1 1";
        }
        EXPECT_TRUE(finished_before_return)
            << "the reset_to had not finished when close() returned";
    }
}

// ── T027: R-1g, R-1n and R-3, receipts that end before or at the reset point ──────────────

namespace {

using App104 = AdminCountingApp104;

// More fields than the session's lowered inbound entry cap, so the arm's late parse fails
// (093's input: unparseable_frame_disposition_test.cpp's late-site cells).
std::string late_parse_filler() {
    std::string out;
    for (std::size_t i = 0; i < fixpp::wire::default_max_offset_entries + 100; ++i) {
        out += std::to_string(5000 + i) + "=v\x01";
    }
    return out;
}

// close(terminal), pumped with the mock clock unmoved: a close() that returns here returns at
// no elapsed part of logon_timeout_ms, so "well inside" that bound.
void close_and_expect_no_reset(Rol104& r, std::string_view const after) {
    r.spawn_close(close_mode::terminal);
    EXPECT_TRUE(r.close_returned_within(kReleaseBudget))
        << "close() did not return with the mock clock unmoved";
    EXPECT_TRUE(r.log().calls.empty())
        << "a reset call after " << after << ": reset() " << r.log().reset_calls << ", reset_to "
        << r.log().reset_to_calls;
}

enum class receipt_end : std::uint8_t { from_admin_throw, from_admin_reject, late_parse_failure };

// R-1g, one parameter on one role, then the forced-hit arm (the same fixture with fromAdmin
// returning, R-1d's input), which resets.
void receipt_ends_before_reset_point(session_role const role, receipt_end const end) {
    SCOPED_TRACE(rg::role_name(role));
    {
        Rol104 r{role};
        auto& s = r.s;
        ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
        seqnum_t const n = s.next_in();
        auto const mark = s.sent.size();
        std::vector<std::byte> lo;
        if (end == receipt_end::from_admin_throw) {
            r.app->logout_from_admin = App104::on_logout_from_admin::throw_;
            lo = s.logout(n);
        } else if (end == receipt_end::from_admin_reject) {
            r.app->logout_from_admin = App104::on_logout_from_admin::reject;
            lo = s.logout(n);
        } else {
            session_test_access::lower_inbound_entry_cap(*s.session,
                                                         fixpp::wire::default_max_offset_entries);
            lo = s.frame("5", n, late_parse_filler());
        }
        auto const res = s.feed(std::move(lo));
        // The arm's existing ending.
        EXPECT_EQ(s.count("5", mark), 1U) << "A's reply: " << s.written(mark);
        if (end == receipt_end::from_admin_throw) {
            EXPECT_FALSE(res.has_value()) << "the throw's error";
            if (!res) EXPECT_EQ(res.error(), fixpp::core::error::app_callback_threw);
            EXPECT_EQ(s.state(), fsm_state::Disconnected);
        } else if (end == receipt_end::from_admin_reject) {
            EXPECT_EQ(s.count("3", mark), 1U) << "the reject's Reject: " << s.written(mark);
            EXPECT_EQ(s.state(), fsm_state::Disconnected);
        } else {
            EXPECT_EQ(s.count("3", mark), 0U) << "a late parse failure sends no Reject";
            EXPECT_FALSE(s.session->is_open()) << "the late parse failure did not close";
        }
        EXPECT_TRUE(r.log().calls.empty())
            << "a reset call after the arm: reset() " << r.log().reset_calls << ", reset_to "
            << r.log().reset_to_calls;
        close_and_expect_no_reset(r, "close()");
    }
    {
        SCOPED_TRACE("forced hit: fromAdmin returns");
        Rol104 r{role};
        auto& s = r.s;
        ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
        (void)s.feed(s.logout(s.next_in()));
        r.spawn_close(close_mode::terminal);
        EXPECT_TRUE(r.close_returned_within(kReleaseBudget)) << "close() did not return";
        EXPECT_FALSE(r.log().calls.empty()) << "the forced hit made no reset call";
    }
}

enum class reply_end : std::uint8_t {
    to_admin_throw,
    assign_failure,
    store_failure,
    write_failure,
    persist_failure
};

// R-1n, one row on one role: reset for StoreFailure and WriteFailure (the leaf's discarded
// result), no reset for ToAdminThrow, AssignFailure and PersistFailure.
void reply_ending(session_role const role, reply_end const end) {
    SCOPED_TRACE(rg::role_name(role));
    bool const resets = end == reply_end::store_failure || end == reply_end::write_failure;
    Rol104 r{Opts104{
        .role = role,
        .store = end == reply_end::store_failure ? store_kind::refusing : store_kind::hooked,
        .fail_held = end == reply_end::persist_failure}};
    auto& s = r.s;
    ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
    seqnum_t const n = s.next_in();
    auto const mark = s.sent.size();
    switch (end) {
        case reply_end::to_admin_throw:
            r.app->throw_on_logout_to_admin = true;
            break;
        case reply_end::assign_failure:
            fixpp::session::seqnum_manager_test_access::set_counters(
                session_test_access::seqnum_mgr(*s.session), n, fixpp::session::seqnum_max);
            break;
        case reply_end::store_failure:
            r.ctl.refuse_next_store = refuse_mode::io_failure;
            break;
        case reply_end::write_failure:
            r.ctl.throw_next_write = true;
            break;
        case reply_end::persist_failure:
            r.ctl.released = true;  // the selected persist completes, then fails at once
            r.ctl.hold_next_persist = true;
            break;
    }
    auto const res = s.feed(s.logout(n));
    EXPECT_EQ(s.state(), fsm_state::Disconnected) << "the arm's ending";
    // The reply is sent before the persist, so only PersistFailure's reply reaches the wire.
    EXPECT_EQ(s.count("5", mark), end == reply_end::persist_failure ? 1U : 0U)
        << "replies on the wire: " << s.written(mark);
    if (end == reply_end::to_admin_throw && !res) {
        EXPECT_EQ(res.error(), fixpp::core::error::app_callback_threw);
    }
    if (end == reply_end::to_admin_throw || end == reply_end::assign_failure ||
        end == reply_end::persist_failure) {
        EXPECT_FALSE(res.has_value()) << "the arm returned success";
    }
    if (end == reply_end::store_failure) {
        EXPECT_EQ(r.ctl.refuse_next_store, refuse_mode::none)
            << "the reply's store was not refused";
    }
    if (end == reply_end::write_failure) {
        EXPECT_FALSE(r.ctl.throw_next_write) << "the reply's write was not attempted";
    }
    if (end == reply_end::persist_failure) {
        EXPECT_FALSE(r.ctl.hold_next_persist) << "the Logout's persist was not selected";
    }
    if (resets) {
        // At the arm, before any close(): the reset point's unit.
        expect_one_reset_to_one_one_disconnected(r);
        r.spawn_close(close_mode::terminal);
        EXPECT_TRUE(r.close_returned_within(kReleaseBudget)) << "close() did not return";
        EXPECT_EQ(r.log().reset_calls, 0) << "a reset() call";
        EXPECT_EQ(r.reset_tos().size(), 1U) << "reset_to calls after close()";
    } else {
        EXPECT_TRUE(r.log().calls.empty())
            << "a reset call after the arm: reset() " << r.log().reset_calls << ", reset_to "
            << r.log().reset_to_calls;
        close_and_expect_no_reset(r, "close()");
    }
}

}  // namespace

TEST(ResetOnLogout104, ReceiptEndsBeforeResetPoint_NoReset_FromAdminThrow) {
    for (auto const role : kRoles)
        receipt_ends_before_reset_point(role, receipt_end::from_admin_throw);
}
TEST(ResetOnLogout104, ReceiptEndsBeforeResetPoint_NoReset_FromAdminReject) {
    for (auto const role : kRoles) {
        receipt_ends_before_reset_point(role, receipt_end::from_admin_reject);
    }
}
TEST(ResetOnLogout104, ReceiptEndsBeforeResetPoint_NoReset_LateParseFailure) {
    for (auto const role : kRoles) {
        receipt_ends_before_reset_point(role, receipt_end::late_parse_failure);
    }
}
TEST(ResetOnLogout104, ReplyEndings_ToAdminThrow) {
    for (auto const role : kRoles) reply_ending(role, reply_end::to_admin_throw);
}
TEST(ResetOnLogout104, ReplyEndings_AssignFailure) {
    for (auto const role : kRoles) reply_ending(role, reply_end::assign_failure);
}
TEST(ResetOnLogout104, ReplyEndings_StoreFailure) {
    for (auto const role : kRoles) reply_ending(role, reply_end::store_failure);
}
TEST(ResetOnLogout104, ReplyEndings_WriteFailure) {
    for (auto const role : kRoles) reply_ending(role, reply_end::write_failure);
}
TEST(ResetOnLogout104, ReplyEndings_PersistFailure) {
    for (auto const role : kRoles) reply_ending(role, reply_end::persist_failure);
}

// R-3: the acceptor's Logon arm exits superseded with the state still LogonReceived (its reply
// Logon's store held while close(graceful) commits `closing` and holds in its flush); an
// in-sequence peer Logout then reaches arm A. A writes nothing in LogonReceived
// (inbound_logout_leaf_ replies in Active only), resets at its reset point, and close()
// returns only after that unit.
TEST(ResetOnLogout104, LogonReceivedLogoutWhileClosing_ResetOnceByTheArm) {
    bool flush_released = false;  // declared before the session: the flush hook reads it
    Rol104 r{session_role::acceptor};
    r.factory->hooks.flush_until = [&flush_released] { return flush_released; };
    auto& s = r.s;
    struct release_on_exit {
        bool& f;
        ~release_on_exit() { f = true; }
    } const release_flush{flush_released};
    auto open_fut = asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                                   s.session->open(), asio::use_future);
    ASSERT_TRUE(s.pump_until([&] { return rg::Scene::ready(open_fut); }))
        << "setup: open() did not return";
    ASSERT_TRUE(open_fut.get().has_value()) << "setup: open() failed";
    r.ctl.hold_next_store = true;
    auto logon_fut = s.feed_spawn(s.logon(1));
    ASSERT_TRUE(s.pump_until([&] { return !r.log().store_events.empty(); }, kHoldWindow))
        << "setup: the reply Logon's store was not held";
    ASSERT_EQ(s.state(), fsm_state::LogonReceived) << "setup: not in LogonReceived";
    r.spawn_close(close_mode::graceful);
    ASSERT_TRUE(s.pump_until([&] { return r.log().flushes_begun == 1; }, kHoldWindow))
        << "setup: close(graceful) did not reach its flush";
    r.ctl.released = true;
    ASSERT_TRUE(s.pump_until([&] { return rg::Scene::ready(logon_fut); }, kHoldWindow))
        << "setup: the Logon arm did not return";
    ASSERT_EQ(s.state(), fsm_state::LogonReceived)
        << "setup: the Logon arm's superseded exit left " << rg::state_name(s.state());
    auto const mark = s.sent.size();
    auto lo_fut = s.feed_spawn(s.logout(2));
    bool const frame_returned = s.pump_until([&] { return rg::Scene::ready(lo_fut); }, kHoldWindow);
    bool const close_before_flush = r.close_run.done;
    flush_released = true;
    bool const close_returned = r.close_returned_within(kReleaseBudget);

    EXPECT_TRUE(frame_returned) << "the Logout's frame did not return";
    EXPECT_FALSE(close_before_flush) << "close() returned while its flush was held";
    EXPECT_TRUE(close_returned) << "close() did not return after the flush";
    EXPECT_FALSE(r.log().flush_hold_timed_out) << "the flush ended by its bound";
    EXPECT_EQ(s.count("5", mark), 0U) << "A wrote a Logout in LogonReceived: " << s.written(mark);
    expect_one_reset_to_one_one_disconnected(r);
    bool finished_before_return = false;
    for (auto const& w : r.close_run.writes_at_return) {
        finished_before_return = finished_before_return || w.op == "reset_to 1 1";
    }
    EXPECT_TRUE(finished_before_return) << "the reset_to had not finished when close() returned";
    EXPECT_EQ(r.log().reset_calls, 0) << "a reset() call";
}

// ── T028: R-1v, the reply's store cancelled ──────────────────────────────────────────────

// R-1v: the refusing double returns store_cancelled for A's reply. The number is taken back,
// the session is Disconnected, no frame is written, no reset call is made, and a later close()
// returns with the mock clock unmoved and no close_reset_wait_expired. The forced-hit arm is
// R-1n's StoreFailure input on the same double, which resets at the arm.
TEST(ResetOnLogout104, ReplyStoreCancelled_NoReset) {
    for (auto const role : kRoles) {
        SCOPED_TRACE(rg::role_name(role));
        {
            Rol104 r{Opts104{.role = role, .store = store_kind::refusing}};
            auto& s = r.s;
            ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
            seqnum_t const n = s.next_in();
            auto const out_before = r.manager_numbers().out;
            auto const mark = s.sent.size();
            r.ctl.refuse_next_store = refuse_mode::cancelled;
            (void)s.feed(s.logout(n));
            EXPECT_EQ(r.ctl.refuse_next_store, refuse_mode::none)
                << "setup: the reply's store was not refused";
            EXPECT_EQ(r.manager_numbers().out, out_before)
                << "the reply's number was not taken back";
            EXPECT_EQ(s.state(), fsm_state::Disconnected);
            EXPECT_EQ(s.sent.size(), mark) << "a frame was written: " << s.written(mark);
            EXPECT_TRUE(r.log().calls.empty())
                << "a reset call after the arm: reset() " << r.log().reset_calls << ", reset_to "
                << r.log().reset_to_calls;
            close_and_expect_no_reset(r, "close()");
            EXPECT_FALSE(r.close_reset_wait_expired()) << "close_reset_wait_expired was emitted";
        }
        {
            SCOPED_TRACE("forced hit: the reply's store refused (StoreFailure)");
            reply_ending(role, reply_end::store_failure);
        }
    }
}

// ── T029: R-1h and R-1i ──────────────────────────────────────────────────────────────────

namespace {

std::size_t count_frames(std::vector<std::byte> const& bytes, std::string_view msg_type) {
    std::string const wire(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::string const needle =
        "\x01"
        "35=" +
        std::string{msg_type} + "\x01";
    std::size_t n = 0;
    for (auto pos = wire.find(needle); pos != std::string::npos; pos = wire.find(needle, pos + 1)) {
        ++n;
    }
    return n;
}

}  // namespace

// R-1h: a standalone initiator resets at the peer Logout's reset point; drive_reconnect() then
// installs the factory's transport and writes the reconnect Logon (RR-4 does not refuse it),
// the numbers advance, and close(terminal) on the new connection makes no reset. The first
// half is the cell's forced hit.
TEST(ResetOnLogout104, PeerLogoutThenReconnect_NoResetOnNewConnection) {
    auto const tf = std::make_shared<fixpp::test_support::MockReconnectFactory>();
    Rol104 r{Opts104{.role = session_role::initiator, .transport_factory = tf}};
    auto& s = r.s;
    ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
    (void)s.feed(s.logout(s.next_in()));
    ASSERT_EQ(s.state(), fsm_state::Disconnected) << "setup: A did not end the connection";
    // The reset at the reset point, before any close.
    expect_one_reset_to_one_one_disconnected(r);
    auto const out_before = r.manager_numbers().out;

    auto rc_fut = asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                                 s.session->drive_reconnect(), asio::use_future);
    ASSERT_TRUE(s.pump_until([&] { return rg::Scene::ready(rc_fut); }))
        << "drive_reconnect() did not return";
    auto const rc = rc_fut.get();
    EXPECT_TRUE(rc.has_value()) << "drive_reconnect() failed: "
                                << (rc ? 0 : static_cast<int>(rc.error()));
    ASSERT_NE(tf->last_transport, nullptr) << "no transport was installed";
    EXPECT_EQ(count_frames(tf->last_transport->outbound_bytes_seen(), "A"), 1U)
        << "the reconnect Logon was not written on the new transport";
    EXPECT_EQ(r.manager_numbers().out, out_before + 1) << "the reconnect Logon's number";
    auto const calls_before_close = r.log().calls.size();

    r.spawn_close(close_mode::terminal);
    EXPECT_TRUE(r.close_returned_within(kReleaseBudget)) << "close() did not return";
    EXPECT_EQ(r.log().calls.size(), calls_before_close)
        << "a reset call at the new connection's close: reset() " << r.log().reset_calls
        << ", reset_to " << r.log().reset_to_calls;
}

// R-1i: the receipt reset's store call fails (a persistent StoreDouble's next reset() fails):
// logged, the arm returns as it would without the failure, Disconnected, the manager at (1, 1),
// and the teardown does not retry it.
TEST(ResetOnLogout104, ReceiptResetStoreFailure_LoggedThenDisconnected) {
    for (auto const role : kRoles) {
        SCOPED_TRACE(rg::role_name(role));
        Rol104 r{Opts104{.role = role, .store = store_kind::store_double}};
        auto& s = r.s;
        ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
        r.double_factory->store->fail_next_reset();
        auto const res = s.feed(s.logout(s.next_in()));
        EXPECT_TRUE(res.has_value()) << "the arm's return changed with the store's failure";
        EXPECT_EQ(s.state(), fsm_state::Disconnected);
        EXPECT_EQ(r.manager_numbers(), (Rol104::Numbers{.in = 1, .out = 1}))
            << "the manager at the arm's end";
        EXPECT_EQ(r.double_factory->store->reset_call_count(), 1U)
            << "the receipt reset's store call";
        r.spawn_close(close_mode::terminal);
        EXPECT_TRUE(r.close_returned_within(kReleaseBudget)) << "close() did not return";
        EXPECT_EQ(r.double_factory->store->reset_call_count(), 1U)
            << "the teardown retried the reset";
    }
}

// ── T030: R-1j, R-1k and R-1s, close() waits for a unit ──────────────────────────────────

// R-1j: L's arm's reset_to is held. The hold is reached in L's arm (Disconnected, the
// transport open, phase 1 not woken); close_grace, a real-time timer of
// logout_disconnect_timeout_ms (an eighth of kHoldBound here), expires during the hold and
// closes the transport while close() is still pending on the unit; the mock clock is never
// moved. After the release, close() returns and the numbers are (1, 1).
TEST(ResetOnLogout104, CloseWaitsForReceiptUnit) {
    constexpr auto kGrace = std::chrono::duration_cast<std::chrono::milliseconds>(kHoldBound) / 8;
    int woken = 0;  // declared before the session: the wrapped wake writes it
    std::optional<int> woken_at_reset;
    Live104 l{session_role::acceptor, static_cast<std::uint32_t>(kGrace.count())};
    l.factory->hooks.on_reset = [&] { woken_at_reset = woken; };
    ASSERT_TRUE(l.start_to_active()) << "setup: the session did not reach Active";
    l.spawn_close(close_mode::graceful);
    ASSERT_TRUE(l.rig.run_until([&] {
        return l.rig.state() == fsm_state::LogoutSent && l.peer_frames("5").size() == 1U &&
               static_cast<bool>(session_test_access::logout_wait_wake(*l.sess));
    })) << "setup: phase 1's Logout was not sent";
    auto& wake = session_test_access::logout_wait_wake(*l.sess);
    wake = [orig = wake, &woken] {
        ++woken;
        orig();
    };
    l.rig.peer.send(l.rig.msg("5", l.manager_numbers().in));
    bool const held = l.rig.run_until([&] { return woken_at_reset.has_value(); }, kHoldWindow);
    bool const transport_closed =
        l.rig.run_until([&] { return l.rig.peer.read_ended; }, kReleaseBudget);
    bool const close_pending = !l.close_run->done;
    l.ctl.released = true;
    bool const close_returned = l.close_returned_within(kReleaseBudget);

    ASSERT_TRUE(held) << "setup: no reset call was held";
    ASSERT_FALSE(l.log().calls.empty());
    auto const& c = l.log().calls.front();
    EXPECT_EQ(c.kind, StoreLog::call_kind::reset_to) << "the held call was a reset()";
    ASSERT_TRUE(c.reading.has_value());
    EXPECT_EQ(c.reading->state, fsm_state::Disconnected) << rg::state_name(c.reading->state);
#if defined(FIXPP_TEST_PEER_HANG_UP_READING)
    EXPECT_TRUE(c.reading->transport_open) << "the transport was closed at the held call";
#else
    std::cout << "[ READING  ] the transport reading is not made on this platform\n";
#endif
    EXPECT_EQ(*woken_at_reset, 0) << "phase 1 had been woken at the held call";
    EXPECT_TRUE(transport_closed) << "close_grace did not close the transport during the hold";
    EXPECT_TRUE(close_pending) << "close() returned during the hold";
    EXPECT_TRUE(close_returned) << "close() did not return after the release";
    EXPECT_FALSE(l.log().hold_timed_out) << "the hold ended by its bound, not its release";
    EXPECT_EQ(l.manager_numbers(), (Rol104::Numbers{.in = 1, .out = 1}));
    auto const st = l.store_numbers();
    ASSERT_TRUE(st.has_value()) << "the store's numbers were not read";
    EXPECT_EQ(*st, (Rol104::Numbers{.in = 1, .out = 1}));
}

// R-1k: A in Active, its reply's store held; close(terminal) started during the hold does not
// return before the release; then exactly one reset_to(1, 1), no reset, finished before
// close() returns.
TEST(ResetOnLogout104, CloseDuringReplyStore_OneResetByTheArm) {
    for (auto const role : kRoles) {
        SCOPED_TRACE(rg::role_name(role));
        Rol104 r{role};
        auto& s = r.s;
        ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
        r.ctl.hold_next_store = true;
        auto frame_fut = s.feed_spawn(s.logout(s.next_in()));
        bool const held = s.pump_until([&] { return !r.log().store_events.empty(); }, kHoldWindow);
        r.spawn_close(close_mode::terminal);
        bool const close_before_release =
            s.pump_until([&] { return r.close_run.done; }, kHoldWindow);
        r.ctl.released = true;
        bool const close_returned = r.close_returned_within(kReleaseBudget);
        (void)s.pump_until([&] { return rg::Scene::ready(frame_fut); }, kReleaseBudget);

        ASSERT_TRUE(held) << "setup: the reply's store was not held";
        EXPECT_FALSE(close_before_release) << "close() returned during the reply's held store";
        EXPECT_TRUE(close_returned) << "close() did not return after the release";
        EXPECT_FALSE(r.log().hold_timed_out) << "the hold ended by its bound, not its release";
        expect_one_reset_to_one_one_disconnected(r);
        bool finished_before_return = false;
        for (auto const& w : r.close_run.writes_at_return) {
            finished_before_return = finished_before_return || w.op == "reset_to 1 1";
        }
        EXPECT_TRUE(finished_before_return)
            << "the reset_to had not finished when close() returned";
        EXPECT_EQ(r.log().reset_calls, 0) << "a reset() call";
    }
}

// R-1s: no reset knob; the initiator's Logon arm runs its 141=Y reset unit, whose reset_to is
// held; close(terminal) started during the hold does not return before the release.
TEST(ResetOnLogout104, CloseWaitsForLogonArmUnit_NoKnob) {
    Rol104 r{Opts104{.role = session_role::initiator, .reset_on_logout = false}};
    r.factory->hooks.on_reset = [] {};  // holds the first reset call, with release_when
    auto& s = r.s;
    auto open_fut = asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                                   s.session->open(), asio::use_future);
    ASSERT_TRUE(s.pump_until([&] { return rg::Scene::ready(open_fut); }))
        << "setup: open() did not return";
    ASSERT_TRUE(open_fut.get().has_value()) << "setup: open() failed";
    auto ack_fut = s.feed_spawn(s.frame("A", 1,
                                        "98=0\x01"
                                        "108=" +
                                            std::to_string(rg::kHeartBtInt.count()) +
                                            "\x01"
                                            "141=Y\x01"));
    bool const held = s.pump_until([&] { return !r.log().calls.empty(); }, kHoldWindow);
    r.spawn_close(close_mode::terminal);
    bool const close_before_release = s.pump_until([&] { return r.close_run.done; }, kHoldWindow);
    r.ctl.released = true;
    bool const close_returned = r.close_returned_within(kReleaseBudget);
    (void)s.pump_until([&] { return rg::Scene::ready(ack_fut); }, kReleaseBudget);

    ASSERT_TRUE(held) << "setup: the Logon arm's reset call was not issued";
    EXPECT_EQ(r.log().calls.front().kind, StoreLog::call_kind::reset_to)
        << "setup: the held call is not the Logon arm's unit";
    EXPECT_FALSE(close_before_release) << "close() returned during the held reset_to";
    EXPECT_TRUE(close_returned) << "close() did not return after the release";
    EXPECT_FALSE(r.log().hold_timed_out) << "the hold ended by its bound, not its release";
}

// ── T031: R-1l, R-1u and R-1x ────────────────────────────────────────────────────────────

namespace {

enum class held_call : std::uint8_t { reply_store, persist };

// R-1l, one arm: no clock, rol only, A in Active; HookedStore holds the reply's store or the
// Logout's inbound persist after completing it. close(terminal) returns during the hold; no
// reset call is made at any point; after the release the arm runs no unit. `live` attaches a
// ScriptedReadTransport to the acceptor (its own seam), so the session writes through a live
// transport; otherwise the Scene's config-time sink is the session's transport (the
// transport_send twin, B-563-5).
void close_while_pending_no_clock(held_call const held, bool const live) {
    SCOPED_TRACE(
        std::string{held == held_call::reply_store ? "HeldInReplyStore" : "HeldInPersist"} +
        (live ? "/live transport" : "/transport_send"));
    Rol104 r{Opts104{.role = session_role::acceptor, .with_clock = false}};
    auto& s = r.s;
    auto const stream = std::make_shared<fixpp::session::test::ScriptedStream>();
    auto open_fut = asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                                   s.session->open(), asio::use_future);
    ASSERT_TRUE(s.pump_until([&] { return rg::Scene::ready(open_fut); })) << "setup: open()";
    ASSERT_TRUE(open_fut.get().has_value()) << "setup: open() failed";
    if (live) {
        s.session->attach_accepted_transport(
            std::make_unique<fixpp::session::test::ScriptedReadTransport>(stream),
            fixpp::transport::handshake_result{});
    }
    (void)s.feed(s.logon(1));
    ASSERT_EQ(s.state(), fsm_state::Active) << "setup: the session did not reach Active";
    seqnum_t const n = s.next_in();
    seqnum_t const n_out = r.manager_numbers().out;
    auto const sent_mark = s.sent.size();
    auto const live_mark = stream->written.size();
    if (held == held_call::reply_store) {
        r.ctl.hold_next_store = true;
    } else {
        r.ctl.hold_next_persist = true;
    }
    auto frame_fut = s.feed_spawn(s.logout(n));
    bool const reached = s.pump_until(
        [&] {
            return held == held_call::reply_store ? !r.log().store_events.empty()
                                                  : !r.ctl.hold_next_persist;
        },
        kHoldWindow);
    r.spawn_close(close_mode::terminal);
    bool const close_during_hold = s.pump_until([&] { return r.close_run.done; }, kHoldWindow);
    auto const live_at_close = stream->written.size();
    bool const reset_before_release = !r.log().calls.empty();
    r.ctl.released = true;
    bool const frame_returned =
        s.pump_until([&] { return rg::Scene::ready(frame_fut); }, kReleaseBudget);

    ASSERT_TRUE(reached) << "setup: the held call was not reached";
    EXPECT_TRUE(close_during_hold) << "close() did not return during the hold";
    EXPECT_TRUE(frame_returned) << "the Logout's frame did not return";
    EXPECT_FALSE(r.log().hold_timed_out) << "the hold ended by its bound, not its release";
    EXPECT_FALSE(reset_before_release) << "a reset call before the release";
    EXPECT_TRUE(r.log().calls.empty()) << "a reset call: reset() " << r.log().reset_calls
                                       << ", reset_to " << r.log().reset_to_calls;
    EXPECT_EQ(r.manager_numbers().in, n + 1) << "the manager's NextNumIn";
    auto const st = r.store_numbers(store_kind::hooked);
    ASSERT_TRUE(st.has_value()) << "the store's numbers were not read";
    if (held == held_call::reply_store) {
        ASSERT_FALSE(r.log().store_events.empty());
        EXPECT_EQ(r.log().store_events.front().seq, n_out) << "the held store's number";
        EXPECT_EQ(st->in, n) << "the store's NextNumIn: the persist after the hold was not refused";
        if (live) {
            EXPECT_EQ(stream->written.size(), live_at_close)
                << "a frame on the live transport after close() returned";
            (void)live_mark;
        } else {
            EXPECT_EQ(s.count("5", sent_mark), 1U)
                << "the transport_send twin's reply: " << s.written(sent_mark);
        }
    } else {
        EXPECT_EQ(st->in, n + 1) << "the store's NextNumIn: the held persist did not land";
    }
}

}  // namespace

TEST(ResetOnLogout104, CloseWhilePending_NoClock_NoReset_HeldInReplyStore) {
    close_while_pending_no_clock(held_call::reply_store, /*live=*/true);
    close_while_pending_no_clock(held_call::reply_store, /*live=*/false);
}

TEST(ResetOnLogout104, CloseWhilePending_NoClock_NoReset_HeldInPersist) {
    close_while_pending_no_clock(held_call::persist, /*live=*/true);
}

// R-1u: A in Active with a clock; close(terminal) is started inside the onLogout that the
// arm's Disconnected write fires. Exactly one reset_to, seen with the session Disconnected,
// no reset, finished before close() returns.
TEST(ResetOnLogout104, CloseInsideOnLogout_ResetOnceByTheArm) {
    for (auto const role : kRoles) {
        SCOPED_TRACE(rg::role_name(role));
        Rol104 r{role};
        auto& s = r.s;
        ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
        bool started = false;
        r.app->on_logout_hook = [&r, &started] {
            if (started) return;
            started = true;
            r.spawn_close(close_mode::terminal);
        };
        (void)s.feed(s.logout(s.next_in()));
        bool const close_returned = r.close_returned_within(kReleaseBudget);
        r.app->on_logout_hook = nullptr;

        ASSERT_TRUE(started) << "setup: onLogout did not run";
        EXPECT_TRUE(close_returned) << "close() did not return";
        expect_one_reset_to_one_one_disconnected(r);
        bool finished_before_return = false;
        for (auto const& w : r.close_run.writes_at_return) {
            finished_before_return = finished_before_return || w.op == "reset_to 1 1";
        }
        EXPECT_TRUE(finished_before_return)
            << "the reset_to had not finished when close() returned";
        EXPECT_EQ(r.log().reset_calls, 0) << "a reset() call";
    }
}

// R-1x: a direct Session, A in Active, rol, a clock. The cell spawns the Logout's
// on_inbound_frame bound to its own cancellation slot and emits a terminal cancellation on it
// from inside the onLogout that the reset point fires. Exactly one reset_to(1, 1), the numbers
// (1, 1), no reset. (Its "marker `reached`" assertion lands with the marker's accessor, T040.)
TEST(ResetOnLogout104, TerminalEmissionAtResetPoint_ResetOnce) {
    for (auto const role : kRoles) {
        SCOPED_TRACE(rg::role_name(role));
        asio::cancellation_signal sig;  // declared before the session: the frame's slot is on it
        bool done = false;
        Rol104 r{role};
        auto& s = r.s;
        ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
        bool emitted = false;
        r.app->on_logout_hook = [&sig, &emitted] {
            if (emitted) return;
            emitted = true;
            sig.emit(asio::cancellation_type::terminal);
        };
        r.frames.push_back(s.logout(s.next_in()));
        auto const bytes = std::span<const std::byte>{r.frames.back()};
        asio::co_spawn(
            fixpp::test_support::session_executor_of(*s.session, s.ioc),
            s.session->on_inbound_frame(bytes),
            asio::bind_cancellation_slot(
                sig.slot(), [&done](std::exception_ptr const&,
                                    fixpp::core::expected_t<void> const&) { done = true; }));
        bool const frame_done = s.pump_until([&] { return done; }, kReleaseBudget);
        r.app->on_logout_hook = nullptr;

        ASSERT_TRUE(emitted) << "setup: onLogout did not run";
        EXPECT_TRUE(frame_done) << "the frame did not complete";
        expect_one_reset_to_one_one_disconnected(r);
        EXPECT_EQ(r.manager_numbers(), (Rol104::Numbers{.in = 1, .out = 1}));
        r.spawn_close(close_mode::terminal);
        EXPECT_TRUE(r.close_returned_within(kReleaseBudget)) << "close() did not return";
        EXPECT_EQ(r.log().reset_calls, 0) << "a reset() call";
        EXPECT_EQ(r.reset_tos().size(), 1U) << "reset_to calls after close()";
    }
}

// ── T032: R-1w, close() is not held when a receipt ends without a unit ───────────────────
//
// The Storeless arm is dropped: a write held in BlockingWriteTransport does not outlast
// close()'s close_async() (tasks.md T032's recorded reading).

namespace {

int close_reset_wait_expired_count(fixpp::session::Session const& sess) {
    int n = 0;
    for (auto const& ev : sess.recent_events()) {
        n += static_cast<int>(
            std::holds_alternative<fixpp::session::session_event_close_reset_wait_expired>(ev));
    }
    return n;
}

}  // namespace

// R-1w FromAdminThrow: close(terminal) is started during A's held reply store; the hold is
// released and fromAdmin throws. With the mock clock moved to just below logon_timeout_ms
// (the advance below states the ratio), close() has returned, with no
// close_reset_wait_expired and no reset.
TEST(ResetOnLogout104, CloseNotHeldWhenReceiptEndsWithoutUnit_FromAdminThrow) {
    for (auto const role : kRoles) {
        SCOPED_TRACE(rg::role_name(role));
        Rol104 r{role};
        auto& s = r.s;
        ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
        r.app->logout_from_admin = App104::on_logout_from_admin::throw_;
        r.ctl.hold_next_store = true;
        auto frame_fut = s.feed_spawn(s.logout(s.next_in()));
        bool const held = s.pump_until([&] { return !r.log().store_events.empty(); }, kHoldWindow);
        r.spawn_close(close_mode::terminal);
        (void)s.pump_until([&] { return r.close_run.done; }, kHoldWindow);
        r.ctl.released = true;
        (void)s.pump_until([&] { return rg::Scene::ready(frame_fut); }, kReleaseBudget);
        s.mock->advance(std::chrono::milliseconds{s.cfg.logon_timeout_ms} * 99 / 100);
        bool const returned = s.pump_until([&] { return r.close_run.done; }, kReleaseBudget);
        EXPECT_TRUE(returned) << "close() had not returned below logon_timeout_ms";
        if (!returned) (void)r.close_returned_within(kReleaseBudget);

        ASSERT_TRUE(held) << "setup: the reply's store was not held";
        EXPECT_EQ(r.app->from_admin_count("5"), 1) << "setup: fromAdmin did not run on the Logout";
        EXPECT_EQ(close_reset_wait_expired_count(*s.session), 0) << "close_reset_wait_expired was emitted";
        EXPECT_TRUE(r.log().calls.empty()) << "a reset call: reset() " << r.log().reset_calls
                                           << ", reset_to " << r.log().reset_to_calls;
    }
}

// R-1w StuckPending, the forced hit of the expiry event: A's reply store is held and never
// released while close(terminal) waits; the mock clock moved past logon_timeout_ms ends the
// wait with one close_reset_wait_expired. The cell's pumps stay inside kHoldBound, so the hold
// is still in place when the clock moves.
TEST(ResetOnLogout104, CloseNotHeldWhenReceiptEndsWithoutUnit_StuckPending) {
    for (auto const role : kRoles) {
        SCOPED_TRACE(rg::role_name(role));
        Rol104 r{role};
        auto& s = r.s;
        ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
        r.ctl.hold_next_store = true;
        auto frame_fut = s.feed_spawn(s.logout(s.next_in()));
        bool const held = s.pump_until([&] { return !r.log().store_events.empty(); }, kHoldWindow);
        r.spawn_close(close_mode::terminal);
        (void)s.pump_until([&] { return r.close_run.done; }, kHoldWindow / 2);
        s.mock->advance(std::chrono::milliseconds{s.cfg.logon_timeout_ms} +
                        std::chrono::milliseconds{1});
        bool const returned = s.pump_until([&] { return r.close_run.done; }, kHoldWindow);
        bool const hold_in_place = !r.log().hold_timed_out;
        r.ctl.released = true;
        (void)s.pump_until([&] { return rg::Scene::ready(frame_fut); }, kReleaseBudget);
        if (!returned) (void)r.close_returned_within(kReleaseBudget);

        ASSERT_TRUE(held) << "setup: the reply's store was not held";
        EXPECT_TRUE(hold_in_place) << "the hold reached its bound before the clock moved";
        EXPECT_TRUE(returned) << "close() did not return past logon_timeout_ms";
        EXPECT_EQ(close_reset_wait_expired_count(*s.session), 1) << "close_reset_wait_expired events";
    }
}

// ── T033: R-1o, a Logout in LogonSent or NotConnected ────────────────────────────────────
//
// The arms share one store double: a HookedStore whose MemoryStore is seeded to NextNumIn
// kHydratedN, so the session hydrates above 1. The mTLS arms run a direct initiator started by
// open() with an mtls_ca profile and a CompID binding (test_compid_binding_mtls_fail_closed.cpp's
// Cell B): such an initiator has no live identity, so the Logon path's gate fails closed.

namespace {

constexpr seqnum_t kHydratedN = 5;

std::shared_ptr<fixpp::session::MemoryStore> hydrated_store() {
    auto st = std::make_shared<fixpp::session::MemoryStore>(
        fixpp::session::MemoryStore::Config{.policy = fixpp::session::capacity_policy::unbounded});
    asio::io_context seed;
    asio::co_spawn(
        seed,
        [st]() -> asio::awaitable<void> {
            for (seqnum_t i = 1; i < kHydratedN; ++i) {
                (void)co_await st->next_seqnum(direction_t::inbound, true);
            }
        },
        asio::detached);
    seed.run();
    return st;
}

bool compid_authorization_failed(rg::Scene const& s) {
    for (auto const& ev : s.session->recent_events()) {
        if (std::holds_alternative<fixpp::session::session_event_compid_authorization_failed>(ev)) {
            return true;
        }
    }
    return false;
}

enum class logon_sent_arm : std::uint8_t { resets, mtls_unauthorised, rol_off_mtls };

// One R-1o LogonSent arm: open() as an initiator hydrated at kHydratedN, then the peer's
// Logout at 34 = 2, below NextNumIn.
void logon_sent_logout(logon_sent_arm const arm) {
    bool const rol = arm != logon_sent_arm::rol_off_mtls;
    bool const mtls = arm != logon_sent_arm::resets;
    Rol104 r{Opts104{.role = session_role::initiator, .reset_on_logout = rol}};
    r.factory->log->inner = hydrated_store();
    auto& s = r.s;
    if (mtls) {
        s.cfg.security_profile =
            fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::mtls_ca};
        s.cfg.compid_authorization_policy.add_binding("CN=TW-PROD-01,O=Peer,C=US", {"TW"});
        s.session = std::make_unique<fixpp::session::Session>(s.engine, s.cfg);
        r.app->session = s.session.get();
    }
    auto open_fut = asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                                   s.session->open(), asio::use_future);
    ASSERT_TRUE(s.pump_until([&] { return rg::Scene::ready(open_fut); })) << "setup: open()";
    ASSERT_TRUE(open_fut.get().has_value()) << "setup: open() failed";
    ASSERT_EQ(s.state(), fsm_state::LogonSent) << "setup: not in LogonSent";
    ASSERT_EQ(s.next_in(), kHydratedN) << "setup: the session did not hydrate above 1";
    auto const mark = s.sent.size();
    auto const res = s.feed(s.logout(2));
    EXPECT_EQ(s.state(), fsm_state::Disconnected);
    if (arm == logon_sent_arm::resets) {
        EXPECT_EQ(s.count("5", mark), 0U) << "a 35=5 written by fixpp: " << s.written(mark);
        expect_one_reset_to_one_one_disconnected(r);
        return;
    }
    EXPECT_TRUE(r.log().calls.empty()) << "a reset call: reset() " << r.log().reset_calls
                                       << ", reset_to " << r.log().reset_to_calls;
    if (arm == logon_sent_arm::mtls_unauthorised) {
        EXPECT_TRUE(compid_authorization_failed(s)) << "no authorisation-failed event";
    } else {
        EXPECT_EQ(s.count("5", mark), 0U) << "a 35=5 written by fixpp: " << s.written(mark);
        EXPECT_TRUE(res.has_value()) << "the arm did not return success";
        EXPECT_FALSE(compid_authorization_failed(s)) << "an authorisation-failed event";
    }
}

}  // namespace

// R-1o Resets, the forced hit of the other three arms' "no reset".
TEST(ResetOnLogout104, LogonSentLogout_Resets) { logon_sent_logout(logon_sent_arm::resets); }

TEST(ResetOnLogout104, LogonSentLogout_MtlsUnauthorised_NoReset) {
    logon_sent_logout(logon_sent_arm::mtls_unauthorised);
}

// The authorisation-failed event's absence reads the same event type by the same predicate as
// the mTLS arm, whose event is its forced hit.
TEST(ResetOnLogout104, LogonSentLogout_RolOff_MtlsUnauthorised_Unchanged) {
    logon_sent_logout(logon_sent_arm::rol_off_mtls);
}

// R-1o NotConnected: an acceptor's first frame is a Logout; no reset call follows, after the
// frame or after close().
TEST(ResetOnLogout104, NotConnectedLogout_NoReset) {
    Rol104 r{Opts104{.role = session_role::acceptor}};
    r.factory->log->inner = hydrated_store();
    auto& s = r.s;
    auto open_fut = asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                                   s.session->open(), asio::use_future);
    ASSERT_TRUE(s.pump_until([&] { return rg::Scene::ready(open_fut); })) << "setup: open()";
    ASSERT_TRUE(open_fut.get().has_value()) << "setup: open() failed";
    ASSERT_EQ(s.state(), fsm_state::NotConnected) << "setup: not in NotConnected";
    (void)s.feed(s.logout(2));
    EXPECT_TRUE(r.log().calls.empty())
        << "a reset call after the frame: reset() " << r.log().reset_calls << ", reset_to "
        << r.log().reset_to_calls;
    close_and_expect_no_reset(r, "close()");
}

// ── T034: R-1p, R-1q and R-1r, what runs while a reset unit is held (contract RR-4) ───────
//
// Each cell holds the reset unit's reset_to (HookedStore's on_reset, with release_when), or an
// in-flight arm's store, and observes the session during the hold, by the pump discipline of
// this file's header. A hold that the base never reaches is read as the cell's own assertion,
// not as a setup failure: the base runs no reset unit at receipt.

namespace {

// R-1p's HeartBtInt: small, so that the cell's two clock advances, each past one HeartBtInt,
// cross the liveness loop's Heartbeat, TestRequest and liveness-disconnect deadlines.
constexpr std::chrono::seconds kSmallHeartBtInt{1};

// True when HookedStore's record holds a call and its first call is a reset_to.
bool first_call_is_reset_to(Rol104 const& r) {
    return !r.log().calls.empty() && r.log().calls.front().kind == StoreLog::call_kind::reset_to;
}

}  // namespace

// R-1p: A in Active with a small HeartBtInt; the receipt unit's reset_to is held. During the
// hold the mock clock moves past one HeartBtInt, then past two. No frame is written after A's
// reply, and after the release the manager reads (1, 1).
TEST(ResetOnLogout104, LivenessDuringReceiptUnit_NumbersNothing) {
    for (auto const role : kRoles) {
        SCOPED_TRACE(rg::role_name(role));
        Rol104 r{Opts104{.role = role, .heartbeat = kSmallHeartBtInt}};
        r.factory->hooks.on_reset = [] {};  // holds the first reset call, until `released`
        auto& s = r.s;
        ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
        auto const mark = s.sent.size();
        auto frame_fut = s.feed_spawn(s.logout(s.next_in()));
        // The base holds nothing, and its frame returns; the hold's window is the bound.
        (void)s.pump_until(
            [&] { return !r.log().calls.empty() || rg::Scene::ready(frame_fut); }, kHoldWindow);
        bool const held = first_call_is_reset_to(r);
        auto const after_reply = s.sent.size();
        // Two advances, each past one HeartBtInt, each followed by half a hold window.
        s.mock->advance(kSmallHeartBtInt + std::chrono::milliseconds{1});
        s.pump_for(kHoldWindow / 2);
        s.mock->advance(kSmallHeartBtInt);
        s.pump_for(kHoldWindow / 2);
        auto const frames_in_hold = s.sent.size() - after_reply;
        std::string const written_in_hold = s.written(after_reply);
        bool const hold_in_place = !r.log().hold_timed_out;
        r.ctl.released = true;
        bool const frame_returned =
            s.pump_until([&] { return rg::Scene::ready(frame_fut); }, kReleaseBudget);
        Numbers104 const mgr = r.manager_numbers();

        EXPECT_EQ(s.count("5", mark), 1U) << "setup: A's reply: " << s.written(mark);
        EXPECT_TRUE(held) << "no reset_to was held at the receipt; reset calls: reset() "
                          << r.log().reset_calls << ", reset_to " << r.log().reset_to_calls;
        EXPECT_EQ(frames_in_hold, 0U)
            << "frames written after A's reply during the hold: " << written_in_hold;
        EXPECT_TRUE(hold_in_place) << "the hold reached its bound before the release";
        EXPECT_TRUE(frame_returned) << "the Logout's frame did not return";
        EXPECT_EQ(mgr, (Numbers104{.in = 1, .out = 1}))
            << "the manager after the release: " << mgr.in << "/" << mgr.out;
    }
}

// R-1q: route B; the bound's reset_to is held. An application message at the pre-reset
// NextNumIn, fed during the hold, is drained: no ResendRequest, no inbound advance, no fromApp.
TEST(ResetOnLogout104, BoundRouteFrameDuringUnit_Drained) {
    for (auto const role : kRoles) {
        SCOPED_TRACE(rg::role_name(role));
        Rol104 r{role};
        r.factory->hooks.on_reset = [] {};  // holds the first reset call, until `released`
        auto& s = r.s;
        ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
        seqnum_t const n = s.next_in();
        (void)s.feed(s.logout(n + 3));
        ASSERT_EQ(r.phase(), kPending) << "setup: no pending mark";
        auto const ack_mark = s.sent.size();
        auto gap_fut = s.feed_spawn(s.gap_fill(n, n + 4));
        // The base holds nothing, and its bound ends; the hold's window is the bound.
        (void)s.pump_until(
            [&] {
                return !r.log().calls.empty() ||
                       (rg::Scene::ready(gap_fut) && r.bound_count() == 0);
            },
            kHoldWindow);
        bool const held = first_call_is_reset_to(r);
        seqnum_t const next_in_before = s.next_in();
        auto const delivered_before = r.app->delivered.size();
        auto const mark = s.sent.size();
        auto frame_fut = s.feed_spawn(s.order(n + 4));
        bool const frame_returned =
            s.pump_until([&] { return rg::Scene::ready(frame_fut); }, kHoldWindow);
        seqnum_t const next_in_after = s.next_in();
        auto const delivered_after = r.app->delivered.size();
        auto const resends = s.count("2", mark);
        bool const hold_in_place = !r.log().hold_timed_out;
        r.ctl.released = true;
        (void)s.pump_until([&] { return rg::Scene::ready(gap_fut) && r.bound_count() == 0; },
                           kReleaseBudget);

        EXPECT_EQ(s.count("5", ack_mark), 1U)
            << "setup: the bound's acknowledgement: " << s.written(ack_mark);
        EXPECT_TRUE(held) << "no reset_to was held at the bound's acknowledgement; reset calls: "
                          << "reset() " << r.log().reset_calls << ", reset_to "
                          << r.log().reset_to_calls;
        EXPECT_TRUE(frame_returned) << "the frame fed during the hold did not return";
        EXPECT_EQ(resends, 0U) << "a ResendRequest: " << s.written(mark);
        EXPECT_EQ(next_in_after, next_in_before)
            << "NextNumIn moved: " << next_in_before << " -> " << next_in_after;
        EXPECT_EQ(delivered_after, delivered_before) << "fromApp received the frame";
        EXPECT_TRUE(hold_in_place) << "the hold reached its bound before the release";
    }
}

// R-1r, the pump-route arm: route B, on the route T011's record names. The Logout is too high
// by one; a GapFill fills to S; a TestRequest at S passes it, which wakes the bound, and the
// TestRequest's Heartbeat store is held while the bound acknowledges and runs its unit. After
// the release nothing that arm writes reaches the wire or the store's numbers, and the store
// and the manager read (1, 1). It is a reach pin: no store call follows the unit on this
// route, so the RR-4 boundaries are witnessed by the seam arms below (research D10's R-1r row).
TEST(ResetOnLogout104, BoundRouteInFlightPumpArm_RefusedAtTheBoundary) {
    for (auto const role : kRoles) {
        SCOPED_TRACE(rg::role_name(role));
        Rol104 r{role};
        auto& s = r.s;
        ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
        seqnum_t const n = s.next_in();
        (void)s.feed(s.logout(n + 1));
        ASSERT_EQ(r.phase(), kPending) << "setup: no pending mark";
        (void)s.feed(s.gap_fill(n, n + 1));
        ASSERT_EQ(s.next_in(), n + 1) << "setup: the gap was not filled to S";
        ASSERT_EQ(r.phase(), kPending) << "setup: the mark left pending at S";
        auto const mark = s.sent.size();
        r.ctl.hold_next_store = true;
        auto frame_fut = s.feed_spawn(s.test_request(n + 1));
        bool const held_while_bound_ended = s.pump_until(
            [&] { return !r.log().store_events.empty() && r.bound_count() == 0; }, kHoldWindow);
        bool const hold_in_place = !r.log().hold_timed_out;
        r.ctl.released = true;
        bool const frame_returned =
            s.pump_until([&] { return rg::Scene::ready(frame_fut); }, kReleaseBudget);
        Numbers104 const mgr = r.manager_numbers();
        auto const st = r.store_numbers(store_kind::hooked);

        EXPECT_TRUE(held_while_bound_ended)
            << "setup: the Heartbeat's store was not held while the bound ended";
        auto const logouts = s.of_type("5", mark);
        EXPECT_EQ(logouts.size(), 1U) << "setup: the bound's acknowledgement: " << s.written(mark);
        // The held store's number was assigned before the bound's reply's: it is the arm's.
        if (!logouts.empty() && !r.log().store_events.empty()) {
            EXPECT_EQ(rg::field(logouts.front(), 34),
                      std::to_string(r.log().store_events.front().seq + 1))
                << "the held store was not the in-flight arm's";
        }
        EXPECT_EQ(s.count("0", mark), 0U)
            << "the in-flight arm's Heartbeat reached the wire: " << s.written(mark);
        EXPECT_TRUE(hold_in_place) << "the hold reached its bound before the release";
        EXPECT_TRUE(frame_returned) << "the TestRequest's frame did not return";
        expect_one_reset_to_one_one_disconnected(r);
        EXPECT_EQ(mgr, (Numbers104{.in = 1, .out = 1}))
            << "the manager after the release: " << mgr.in << "/" << mgr.out;
        ASSERT_TRUE(st.has_value()) << "the store's numbers were not read";
        EXPECT_EQ(*st, (Numbers104{.in = 1, .out = 1}))
            << "the store after the release: " << st->in << "/" << st->out;
    }
}

// R-1r's seam arms. No pump arm issues a store call once the bound's reset point has run (the
// recipe is in research D10's R-1r row), so each RR-4 boundary is driven directly. Route B is
// brought through its reset point: a too-high Logout, a GapFill past S, the bound's
// acknowledgement and its unit. Then one call is made through session_test_access, as an
// emitter in flight across the unit would make it: store_then_emit with the outbound number
// after the acknowledgement's (the one an emitter that assigned between the acknowledgement and
// the unit holds; a store takes outbound numbers in order), or the inbound persist. Each is
// refused at its boundary: no store call, nothing on the wire, store and manager (1, 1).
namespace {

enum class seam_call : std::uint8_t { store_then_emit, persist };

void bound_route_seam_call(session_role const role, seam_call const call) {
    SCOPED_TRACE(std::string{rg::role_name(role)} +
                 (call == seam_call::store_then_emit ? "/store_then_emit" : "/persist"));
    int outbound_stores = 0;  // declared before the session: the store hooks write them
    int inbound_persists = 0;
    Rol104 r{role};
    r.factory->hooks.on_outbound_store = [c = &outbound_stores] {
        ++*c;
        return false;
    };
    r.factory->hooks.on_inbound_persist_call = [c = &inbound_persists] {
        ++*c;
        return false;
    };
    auto& s = r.s;
    ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
    seqnum_t const n = s.next_in();
    (void)s.feed(s.logout(n + 3));
    ASSERT_EQ(r.phase(), kPending) << "setup: no pending mark";
    auto const ack_mark = s.sent.size();
    (void)s.feed(s.gap_fill(n, n + 4));
    ASSERT_TRUE(s.pump_until([&] { return r.bound_count() == 0; }, kReleaseBudget))
        << "setup: the bound did not end";
    auto const acks = s.of_type("5", ack_mark);
    ASSERT_EQ(acks.size(), 1U) << "setup: the bound's acknowledgement: " << s.written(ack_mark);
    auto const stamp = static_cast<seqnum_t>(std::stoul(rg::field(acks.front(), 34)) + 1);

    int const stores_before = outbound_stores;
    int const persists_before = inbound_persists;
    auto const wire_mark = s.sent.size();
    std::future<fixpp::core::expected_t<void>> fut;
    if (call == seam_call::store_then_emit) {
        r.frames.push_back(rg::Scene::raw("0", stamp, "ISLD", s.sending_time()));
        fut = asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                             session_test_access::store_then_emit(
                                 *s.session, stamp, std::span<const std::byte>{r.frames.back()}),
                             asio::use_future);
    } else {
        fut = asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                             session_test_access::persist_inbound_advance(*s.session),
                             asio::use_future);
    }
    bool const returned = s.pump_until([&] { return rg::Scene::ready(fut); }, kReleaseBudget);
    std::optional<fixpp::core::expected_t<void>> result;
    bool threw = false;
    if (returned) {
        try {
            result = fut.get();
        } catch (...) {
            threw = true;
        }
    }

    EXPECT_TRUE(returned) << "the seam call did not return";
    EXPECT_FALSE(threw) << "the seam call threw";
    if (result.has_value()) {
        if (call == seam_call::store_then_emit) {
            EXPECT_FALSE(result->has_value()) << "store_then_emit returned success";
            if (!result->has_value()) {
                EXPECT_EQ(result->error(), fixpp::core::error::dispatch_aborted)
                    << "store_then_emit's refusal";
            }
        } else {
            EXPECT_TRUE(result->has_value()) << "persist_inbound_advance_ returned an error";
        }
    }
    EXPECT_EQ(outbound_stores, stores_before) << "an outbound store call reached the store";
    EXPECT_EQ(inbound_persists, persists_before) << "an inbound persist reached the store";
    EXPECT_EQ(s.sent.size(), wire_mark) << "written after the unit: " << s.written(wire_mark);
    Numbers104 const mgr = r.manager_numbers();
    EXPECT_EQ(mgr, (Numbers104{.in = 1, .out = 1}))
        << "the manager after the seam call: " << mgr.in << "/" << mgr.out;
    auto const st = r.store_numbers(store_kind::hooked);
    ASSERT_TRUE(st.has_value()) << "the store's numbers were not read";
    EXPECT_EQ(*st, (Numbers104{.in = 1, .out = 1}))
        << "the store after the seam call: " << st->in << "/" << st->out;
}

}  // namespace

TEST(ResetOnLogout104, BoundRouteInFlightPumpArm_RefusedAtTheBoundary_SeamStoreThenEmit) {
    for (auto const role : kRoles) bound_route_seam_call(role, seam_call::store_then_emit);
}

TEST(ResetOnLogout104, BoundRouteInFlightPumpArm_RefusedAtTheBoundary_SeamPersist) {
    for (auto const role : kRoles) bound_route_seam_call(role, seam_call::persist);
}

// ── T036: R-1t, Engine::stop() while A's reply store is held ──────────────────────────
//
// An Engine session on plain_rig (Live104), rol only, A in Active, A's reply store held in
// HookedStore. The step-3 mark (engine_test_access::set_before_role_loop_join_hook) runs on
// stop()'s control strand after step 2's close join and immediately before step 3's role-loop
// join. stop() must join the role loop, which is held inside A's arm, before step 4 enters
// close(): so the mark runs with the session not closing, close() is not entered while the
// hold lasts however far the mock clock moves, and the one reset is A's reset_to, finished
// before stop() returns.

namespace {

// Every real-time pump from the hold's start to its release is a fraction of kHoldBound, and
// their sum stays below it (the static_assert), so the hold is ended by the cell's release.
// The mock clock's advance takes no real time and is not part of the sum.
constexpr auto kStopOrderBudget = kHoldWindow;      // until the mark ran or close() began
constexpr auto kStopSettle = kHoldWindow / 2;       // after the clock's advance
constexpr auto kStopNotDoneBudget = kHoldWindow / 2;  // the pump that must not see stop return
static_assert(kHoldWindow + kStopOrderBudget + kStopSettle + kStopNotDoneBudget < kHoldBound);

}  // namespace

TEST(ResetOnLogout104, EngineStopDuringReplyStore_JoinWaitsPastTheBound_OneResetByTheArm) {
    bool mark_ran = false;  // declared before the session: the Engine's hook writes it
    struct StopRun {
        bool done = false;
        std::vector<StoreLog::Call> calls_at_return;   // StoreLog::calls as stop() returned
        std::vector<StoreLog::Write> writes_at_return;  // StoreLog::writes as stop() returned
    };
    auto const stop_run = std::make_shared<StopRun>();
    Live104 l{session_role::acceptor};
    auto& c = l.config();
    ASSERT_TRUE(c.heartbeat_interval.has_value()) << "setup: the rig sets HeartBtInt";
    // HeartBtInt is four times logon_timeout_ms, so the one advance past logon_timeout_ms
    // crosses no Heartbeat, TestRequest or liveness-disconnect deadline.
    c.logon_timeout_ms = static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(*c.heartbeat_interval).count() / 4);
    ASSERT_TRUE(l.start_to_active()) << "setup: the session did not reach Active";
    l.ctl.hold_next_store = true;
    l.rig.peer.send(l.rig.msg("5", l.manager_numbers().in));
    ASSERT_TRUE(l.rig.run_until([&] { return !l.log().store_events.empty(); }, kHoldWindow))
        << "setup: A's reply store was not held";

    // From here every check is non-fatal until stop() has returned.
    fixpp::session::engine_test_access::set_before_role_loop_join_hook(
        *l.rig.engine, [&mark_ran]() -> asio::awaitable<void> {
            mark_ran = true;
            co_return;
        });
    {
        auto* const eng = l.rig.engine.get();
        auto const lg = l.factory->log;
        asio::co_spawn(
            l.rig.ioc,
            [eng, lg, stop_run]() -> asio::awaitable<void> {
                co_await eng->stop();
                stop_run->calls_at_return = lg->calls;
                stop_run->writes_at_return = lg->writes;
                stop_run->done = true;
            },
            asio::detached);
    }
    auto& sess = *l.sess;
    (void)l.rig.run_until([&] { return mark_ran || session_test_access::is_closing(sess); },
                          kStopOrderBudget);
    bool const mark_ran_first = mark_ran;
    bool const closing_at_mark = session_test_access::is_closing(sess);
    l.rig.clock->advance(std::chrono::milliseconds{c.logon_timeout_ms} +
                         std::chrono::milliseconds{1});
    l.rig.settle(kStopSettle);
    bool const closing_after_advance = session_test_access::is_closing(sess);
    int const expired_in_hold = close_reset_wait_expired_count(sess);
    bool const stop_returned_in_hold =
        l.rig.run_until([&] { return stop_run->done; }, kStopNotDoneBudget);
    bool const hold_in_place = !l.log().hold_timed_out;

    l.ctl.released = true;
    bool const stop_returned = l.rig.run_until([&] { return stop_run->done; });
    if (!stop_returned) {
        // close()'s wait, if it was entered, ends past logon_timeout_ms; then stop() is drained.
        l.rig.clock->advance(std::chrono::milliseconds{c.logon_timeout_ms} +
                             std::chrono::milliseconds{1});
        if (!l.rig.run_until([&] { return stop_run->done; })) {
            fixpp::test_support::cancel_and_drain_or_report(l.rig.ioc, *l.rig.clock,
                                                            "ResetOnLogout104 R-1t stop");
        }
    }

    EXPECT_TRUE(mark_ran_first) << "the step-3 mark did not run";
    EXPECT_FALSE(closing_at_mark) << "close() had begun when the step-3 mark ran";
    EXPECT_FALSE(closing_after_advance) << "close() began during the hold";
    EXPECT_EQ(expired_in_hold, 0) << "close_reset_wait_expired during the hold";
    EXPECT_FALSE(stop_returned_in_hold) << "stop() returned during the hold";
    EXPECT_TRUE(hold_in_place) << "the hold reached its bound before the release";
    EXPECT_TRUE(stop_returned) << "stop() did not return after the release";
    EXPECT_FALSE(l.log().hold_timed_out) << "the hold ended by its bound, not its release";
    int reset_tos = 0;
    int resets = 0;
    for (auto const& call : stop_run->calls_at_return) {
        reset_tos += static_cast<int>(call.kind == StoreLog::call_kind::reset_to);
        resets += static_cast<int>(call.kind == StoreLog::call_kind::reset);
    }
    EXPECT_EQ(reset_tos, 1) << "reset_to calls before stop() returned; reset() calls: " << resets;
    EXPECT_EQ(resets, 0) << "a reset() call before stop() returned";
    bool finished_before_return = false;
    for (auto const& w : stop_run->writes_at_return) {
        finished_before_return = finished_before_return || w.op == "reset_to 1 1";
    }
    EXPECT_TRUE(finished_before_return) << "the reset_to had not finished when stop() returned";
}

// ── T037: R-2, both knobs, the peer answers ──────────────────────────────────────────

// R-2 (D2): rol and rod; close(graceful) sends the Logout and the peer answers in LogoutSent.
// Two resets: L's arm's reset_to, seen with the session Disconnected and the transport open,
// then the teardown's reset(); the numbers end at (1, 1).
TEST(ResetOnLogout104, BothKnobs_PeerAnswers_TwoResets_EndsOneOne) {
    Live104 l{session_role::acceptor};
    l.config().reset_on_disconnect = true;
    ASSERT_TRUE(l.start_to_active()) << "setup: the session did not reach Active";
    l.spawn_close(close_mode::graceful);
    ASSERT_TRUE(l.rig.run_until([&] {
        return l.rig.state() == fsm_state::LogoutSent && l.peer_frames("5").size() == 1U &&
               static_cast<bool>(session_test_access::logout_wait_wake(*l.sess));
    })) << "setup: phase 1's Logout was not sent";
    l.rig.peer.send(l.rig.msg("5", l.manager_numbers().in));
    EXPECT_TRUE(l.close_returned_within(4 * std::chrono::milliseconds{kBoundT}))
        << "close() did not return";

    auto const& calls = l.log().calls;
    ASSERT_EQ(calls.size(), 2U) << "reset calls: reset() " << l.log().reset_calls << ", reset_to "
                                << l.log().reset_to_calls;
    EXPECT_EQ(calls[0].kind, StoreLog::call_kind::reset_to) << "the first call was a reset()";
    EXPECT_EQ(calls[0].next_in, seqnum_t{1});
    EXPECT_EQ(calls[0].next_out, seqnum_t{1});
    ASSERT_TRUE(calls[0].reading.has_value());
    EXPECT_EQ(calls[0].reading->state, fsm_state::Disconnected)
        << "the state at the reset_to: " << rg::state_name(calls[0].reading->state);
#if defined(FIXPP_TEST_PEER_HANG_UP_READING)
    EXPECT_TRUE(calls[0].reading->transport_open) << "the transport was closed at the reset_to";
#else
    std::cout << "[ READING  ] the transport reading is not made on this platform\n";
#endif
    EXPECT_EQ(calls[1].kind, StoreLog::call_kind::reset) << "the second call was not the teardown's";
    EXPECT_EQ(l.manager_numbers(), (Rol104::Numbers{.in = 1, .out = 1}));
    auto const st = l.store_numbers();
    ASSERT_TRUE(st.has_value()) << "the store's numbers were not read";
    EXPECT_EQ(*st, (Rol104::Numbers{.in = 1, .out = 1}));
}

// ── T038 part A: R-5a, R-5b, R-5c, R-5e, R-5i and R-5j, a writer in flight at a reset point ──
//
// Contract RR-5, RR-6, O21 and O13-r; research D10's rows. Each cell holds one store call of a
// writer (HookedStore's per-call selectors), lets a reset point or a close-out run during the
// hold, then releases it and reads what the writer did after. Every pump inside a hold is a
// fraction of kHoldBound, stated where it is declared; their sum stays below kHoldBound (the
// static_assert), so the cell's release ends the hold. Each forced-hit arm is its own TEST, on
// the same fixture and observation path as the arm it stands for.

namespace {

// The pumps inside one hold: until the hold is reached, then until the work that runs during
// it has ended. Each is a quarter of kHoldBound.
constexpr auto kReachBudget = kHoldWindow;
constexpr auto kInHoldBudget = kHoldWindow;
static_assert(kReachBudget + kInHoldBudget < kHoldBound);

// The number of Disconnected entries in the state history (a membership count; no order is
// read from fsm_visit_history()).
int disconnected_entries(fixpp::session::Session const& sess) {
    int n = 0;
    for (auto const st : sess.fsm_visit_history())
        n += static_cast<int>(st == fsm_state::Disconnected);
    return n;
}

// The index in StoreLog::writes of the first "reset_to 1 1", if there is one.
std::optional<std::size_t> reset_to_one_one_at(StoreLog const& log) {
    for (std::size_t i = 0; i < log.writes.size(); ++i) {
        if (log.writes[i].op == "reset_to 1 1") return i;
    }
    return std::nullopt;
}

// C1 on the one held outbound store: its store_applied is stamped before the unit's
// "reset_to 1 1" completed, and its store_returned after it.
void expect_held_store_spans_the_reset_to(StoreLog const& log) {
    auto const at = reset_to_one_one_at(log);
    ASSERT_EQ(log.store_events.size(), 2U) << "the held store's two events";
    EXPECT_TRUE(at.has_value()) << "C1: no reset_to(1, 1) on the record; reset calls: reset() "
                                << log.reset_calls << ", reset_to " << log.reset_to_calls;
    if (!at) return;
    EXPECT_LE(log.store_events[0].writes_at, *at)
        << "C1: store_applied is not before the unit's reset_to";
    EXPECT_GT(log.store_events[1].writes_at, *at)
        << "C1: store_returned is not after the unit's reset_to";
}

// The outbound store() calls of a MsgType on StoreSide104's record, from `from`.
std::size_t stored_of_type(StoreSide104 const& side, std::string_view msg_type,
                           std::size_t from = 0) {
    std::size_t n = 0;
    for (std::size_t i = from; i < side.outbound_store_types.size(); ++i) {
        n += static_cast<std::size_t>(side.outbound_store_types[i] == msg_type);
    }
    return n;
}

// ── R-5a ──────────────────────────────────────────────────────────────────────────────

enum class gap_fill_arm : std::uint8_t { one_delivery, deadline };

// R-5a: route B; the too-high Logout's S is N + 3. OneDelivery: one delivery of a GapFill
// N → N + 4, which passes S and posts the bound's wake, then an in-sequence GapFill at N + 4
// whose NewSeqNo X is above its own 34 + 1; the second GapFill's persist is held while the
// bound runs. Deadline: one GapFill N → N + 2, below S, its persist held while the mock clock
// passes logout_disconnect_timeout_ms and the bound acknowledges at its deadline.
void in_flight_gap_fill(session_role const role, gap_fill_arm const arm, bool const rol) {
    SCOPED_TRACE(std::string{rg::role_name(role)} +
                 (arm == gap_fill_arm::one_delivery ? "/OneDelivery" : "/Deadline") +
                 (rol ? "" : "/rol off"));
    int persists = 0;         // declared before the session: the persist selector writes them
    int hold_persist_at = 0;  // the ordinal of the inbound persist that is held; 0 holds none
    Rol104 r{Opts104{.role = role, .reset_on_logout = rol}};
    r.factory->hooks.on_inbound_persist_call = [&persists, &hold_persist_at] {
        return ++persists == hold_persist_at;
    };
    auto& s = r.s;
    ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
    seqnum_t const n = s.next_in();
    (void)s.feed(s.logout(n + 3));
    ASSERT_EQ(r.phase(), kPending) << "setup: no pending mark";
    ASSERT_EQ(r.bound_count(), 1) << "setup: no bound";
    auto const mark = s.sent.size();
    seqnum_t const x = n + 7;
    std::future<fixpp::core::expected_t<void>> fut;
    if (arm == gap_fill_arm::one_delivery) {
        hold_persist_at = persists + 2;  // the second GapFill's
        r.spawn_one_delivery(s.gap_fill(n, n + 4), s.gap_fill(n + 4, x));
    } else {
        hold_persist_at = persists + 1;
        fut = s.feed_spawn(s.gap_fill(n, n + 2));
    }
    bool const held = s.pump_until([&] { return persists >= hold_persist_at; }, kReachBudget);
    if (arm == gap_fill_arm::deadline) {
        s.mock->advance(std::chrono::milliseconds{kBoundT} + std::chrono::milliseconds{1});
    }
    bool const bound_ended = s.pump_until([&] { return r.bound_count() == 0; }, kInHoldBudget);
    auto const acks_in_hold = s.count("5", mark);
    auto const state_in_hold = s.state();
    bool const hold_in_place = !r.log().hold_timed_out;
    r.ctl.released = true;
    bool const returned = s.pump_until(
        [&] { return arm == gap_fill_arm::one_delivery ? r.delivery.done : rg::Scene::ready(fut); },
        kReleaseBudget);
    std::optional<fixpp::core::expected_t<void>> gap_result;
    if (returned) {
        gap_result = arm == gap_fill_arm::one_delivery ? r.delivery.second : fut.get();
    }
    Numbers104 const mgr = r.manager_numbers();

    // Reach: the selected persist was held while the bound acknowledged and wrote Disconnected.
    EXPECT_TRUE(held) << "setup: the GapFill's persist was not reached";
    EXPECT_TRUE(bound_ended) << "setup: the bound did not end during the hold";
    EXPECT_EQ(acks_in_hold, 1U) << "setup: the bound's acknowledgement: " << s.written(mark);
    EXPECT_EQ(state_in_hold, fsm_state::Disconnected)
        << "setup: the state during the hold: " << rg::state_name(state_in_hold);
    EXPECT_TRUE(hold_in_place) << "the hold reached its bound before the release";
    EXPECT_TRUE(returned) << "the GapFill's on_inbound_frame did not return";
    if (arm == gap_fill_arm::one_delivery) {
        EXPECT_TRUE(r.delivery.first.has_value() && r.delivery.first->has_value())
            << "the first GapFill's on_inbound_frame";
    }

    if (!rol) {
        // The forced hit: no reset point, so the GapFill's set_next_inbound lands.
        EXPECT_EQ(mgr.in, x) << "the manager's NextNumIn: " << mgr.in;
        return;
    }
    EXPECT_EQ(mgr, (Numbers104{.in = 1, .out = 1}))
        << "the manager after the release: " << mgr.in << "/" << mgr.out;
    auto const st = r.store_numbers(store_kind::hooked);
    ASSERT_TRUE(st.has_value()) << "the store's numbers were not read";
    EXPECT_EQ(*st, (Numbers104{.in = 1, .out = 1}))
        << "the store after the release: " << st->in << "/" << st->out;
    expect_one_reset_to_one_one_disconnected(r);
    EXPECT_TRUE(gap_result.has_value() && gap_result->has_value())
        << "the GapFill's on_inbound_frame did not return success";
    EXPECT_EQ(disconnected_entries(*s.session), 1) << "Disconnected entries in the history";
}

}  // namespace

TEST(ResetOnLogout104, InFlightGapFillAcrossBoundUnit_ManagerStaysAtOne_OneDelivery) {
    for (auto const role : kRoles) in_flight_gap_fill(role, gap_fill_arm::one_delivery, true);
}

TEST(ResetOnLogout104, InFlightGapFillAcrossBoundUnit_ManagerStaysAtOne_Deadline) {
    for (auto const role : kRoles) in_flight_gap_fill(role, gap_fill_arm::deadline, true);
}

// R-5a's forced hit: the OneDelivery input with reset_on_logout off.
TEST(ResetOnLogout104, InFlightGapFillAcrossBoundUnit_ManagerStaysAtOne_OneDelivery_RolOff) {
    for (auto const role : kRoles) in_flight_gap_fill(role, gap_fill_arm::one_delivery, false);
}

// ── R-5b ──────────────────────────────────────────────────────────────────────────────

namespace {

// R-5b: A in Active on the refusing double. The application frame's store() is forwarded and
// then answered store_io_failure (a persistent-store failure), so store_then_emit's reconcile
// reads the store's next outbound number; that read is held (StoreSide104's selected read)
// while a peer Logout at N reaches arm A's reset point. The read then returns the number it
// read before the hold.
void send_reconcile_across_receipt_unit(session_role const role, bool const rol) {
    SCOPED_TRACE(std::string{rg::role_name(role)} + (rol ? "" : "/rol off"));
    Rol104 r{Opts104{.role = role, .reset_on_logout = rol, .store = store_kind::refusing}};
    auto& s = r.s;
    ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
    seqnum_t const n = s.next_in();
    auto const mark = s.sent.size();
    std::string const payload =
        "35=D\x01"
        "11=ORD-104-R5B\x01" +
        std::string{rg::kSideAndTime};
    r.frames.emplace_back(reinterpret_cast<std::byte const*>(payload.data()),
                          reinterpret_cast<std::byte const*>(payload.data()) + payload.size());
    r.ctl.refuse_next_store = refuse_mode::io_failure_after_store;
    r.ctl.side.hold_next_read = true;
    auto send_fut = asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                                   s.session->send(std::span<const std::byte>{r.frames.back()}),
                                   asio::use_future);
    bool const read_held = s.pump_until([&] { return r.ctl.side.read_selected; }, kReachBudget);
    auto lo_fut = s.feed_spawn(s.logout(n));
    bool const logout_returned =
        s.pump_until([&] { return rg::Scene::ready(lo_fut); }, kInHoldBudget);
    bool const read_returned_in_hold = r.ctl.side.held_read_returned_at.has_value();
    bool const hold_in_place = !r.log().hold_timed_out;
    r.ctl.released = true;
    bool const send_returned =
        s.pump_until([&] { return rg::Scene::ready(send_fut); }, kReleaseBudget);
    std::optional<fixpp::core::expected_t<void>> send_r;
    if (send_returned) send_r = send_fut.get();
    Numbers104 const mgr = r.manager_numbers();

    EXPECT_EQ(r.ctl.refuse_next_store, refuse_mode::none)
        << "setup: the application frame's store was not refused";
    EXPECT_TRUE(read_held) << "setup: the reconcile's read was not reached";
    EXPECT_TRUE(logout_returned) << "setup: the peer Logout's frame did not return in the hold";
    EXPECT_FALSE(read_returned_in_hold) << "the reconcile's read returned before the release";
    EXPECT_TRUE(hold_in_place) << "the hold reached its bound before the release";
    EXPECT_TRUE(send_returned) << "send() did not return after the release";
    EXPECT_TRUE(r.ctl.side.held_read_value.has_value()) << "the held read returned no number";

    if (!rol) {
        // The forced hit: no reset point, so the reconcile's set_next_outbound lands.
        if (r.ctl.side.held_read_value) {
            EXPECT_EQ(mgr.out, *r.ctl.side.held_read_value)
                << "the manager's next outbound: " << mgr.out
                << ", the read: " << *r.ctl.side.held_read_value;
        }
        return;
    }
    EXPECT_EQ(mgr, (Numbers104{.in = 1, .out = 1}))
        << "the manager after the release: " << mgr.in << "/" << mgr.out;
    auto const st = r.store_numbers(store_kind::refusing);
    ASSERT_TRUE(st.has_value()) << "the store's numbers were not read";
    EXPECT_EQ(*st, (Numbers104{.in = 1, .out = 1}))
        << "the store after the release: " << st->in << "/" << st->out;
    ASSERT_TRUE(send_r.has_value());
    EXPECT_FALSE(send_r->has_value()) << "send() returned success";
    if (!send_r->has_value()) {
        EXPECT_EQ(send_r->error(), fixpp::core::error::store_io_failure)
            << "send()'s error: " << static_cast<int>(send_r->error());
    }
    EXPECT_EQ(s.count("D", mark), 0U) << "the application frame was written: " << s.written(mark);
    // The order without which the cell is vacuous: the read returned after the reset_to.
    auto const at = reset_to_one_one_at(r.log());
    EXPECT_TRUE(at.has_value()) << "no reset_to(1, 1) on the record";
    if (at && r.ctl.side.held_read_returned_at) {
        EXPECT_GT(*r.ctl.side.held_read_returned_at, *at)
            << "the reconcile's read returned before the unit's reset_to";
    }
    expect_one_reset_to_one_one_disconnected(r);
}

}  // namespace

TEST(ResetOnLogout104, SendReconcileAcrossReceiptUnit_ManagerOutboundStaysAtOne) {
    for (auto const role : kRoles) send_reconcile_across_receipt_unit(role, true);
}

// R-5b's forced hit: the same input with reset_on_logout off.
TEST(ResetOnLogout104, SendReconcileAcrossReceiptUnit_ManagerOutboundStaysAtOne_RolOff) {
    for (auto const role : kRoles) send_reconcile_across_receipt_unit(role, false);
}

// ── R-5c ──────────────────────────────────────────────────────────────────────────────

namespace {

// R-5c: route B on the refusing double (for its record of outbound store() calls). The gap is
// filled to S, so the mark stays pending; then an in-sequence frame at S with a malformed tag
// after its header (unparseable_frame_disposition_test.cpp's malformed-tag shape) draws a
// Reject through dispose_unparseable_. Its consume advances past S, which wakes the bound,
// and its persist is held while the bound acknowledges. The application's toAdmin throws on a
// Reject.
void in_flight_reject(session_role const role, bool const rol) {
    SCOPED_TRACE(std::string{rg::role_name(role)} + (rol ? "" : "/rol off"));
    Rol104 r{Opts104{.role = role, .reset_on_logout = rol, .store = store_kind::refusing}};
    r.app->throw_on_reject_to_admin = true;
    auto& s = r.s;
    ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
    seqnum_t const n = s.next_in();
    (void)s.feed(s.logout(n + 3));
    ASSERT_EQ(r.phase(), kPending) << "setup: no pending mark";
    (void)s.feed(s.gap_fill(n, n + 3));
    ASSERT_EQ(s.next_in(), n + 3) << "setup: the gap was not filled to S";
    ASSERT_EQ(r.phase(), kPending) << "setup: the mark left pending at S";
    auto const mark = s.sent.size();
    auto const stores_mark = r.ctl.side.outbound_store_types.size();
    r.ctl.hold_next_persist = true;
    auto fut = s.feed_spawn(s.frame("D", n + 3, "9x9=1\x01"));
    bool const held = s.pump_until([&] { return !r.ctl.hold_next_persist; }, kReachBudget);
    bool const bound_ended = s.pump_until([&] { return r.bound_count() == 0; }, kInHoldBudget);
    auto const acks_in_hold = s.count("5", mark);
    auto const state_in_hold = s.state();
    bool const hold_in_place = !r.log().hold_timed_out;
    r.ctl.released = true;
    bool const returned = s.pump_until([&] { return rg::Scene::ready(fut); }, kReleaseBudget);
    Numbers104 const mgr = r.manager_numbers();

    EXPECT_TRUE(held) << "setup: the frame's persist was not reached";
    EXPECT_TRUE(bound_ended) << "setup: the bound did not end during the hold";
    EXPECT_EQ(acks_in_hold, 1U) << "setup: the bound's acknowledgement: " << s.written(mark);
    EXPECT_EQ(state_in_hold, fsm_state::Disconnected)
        << "setup: the state during the hold: " << rg::state_name(state_in_hold);
    EXPECT_TRUE(hold_in_place) << "the hold reached its bound before the release";
    EXPECT_TRUE(returned) << "the frame's on_inbound_frame did not return";
    // The store-call record can report a call: the bound's acknowledgement was stored.
    EXPECT_EQ(stored_of_type(r.ctl.side, "5", stores_mark), 1U)
        << "setup: the acknowledgement's store call was not recorded";

    if (!rol) {
        // The forced hit: no reset point, so the Reject is numbered and reaches toAdmin.
        EXPECT_EQ(r.app->to_admin_count("3"), 1) << "toAdmin calls for a 35=3";
        return;
    }
    EXPECT_EQ(r.app->to_admin_count("3"), 0) << "toAdmin calls for a 35=3";
    EXPECT_EQ(mgr.out, seqnum_t{1}) << "the manager's next outbound: " << mgr.out;
    auto const st = r.store_numbers(store_kind::refusing);
    ASSERT_TRUE(st.has_value()) << "the store's numbers were not read";
    EXPECT_EQ(*st, (Numbers104{.in = 1, .out = 1}))
        << "the store after the release: " << st->in << "/" << st->out;
    EXPECT_EQ(stored_of_type(r.ctl.side, "3", stores_mark), 0U) << "a Reject's store call";
    EXPECT_EQ(s.count("3", mark), 0U) << "a Reject was written: " << s.written(mark);
    expect_one_reset_to_one_one_disconnected(r);
    EXPECT_EQ(disconnected_entries(*s.session), 1) << "Disconnected entries in the history";
}

}  // namespace

TEST(ResetOnLogout104, InFlightRejectAcrossBoundUnit_RefusedBeforeToAdmin) {
    for (auto const role : kRoles) in_flight_reject(role, true);
}

// R-5c's forced hit: the same input with reset_on_logout off.
TEST(ResetOnLogout104, InFlightRejectAcrossBoundUnit_RefusedBeforeToAdmin_RolOff) {
    for (auto const role : kRoles) in_flight_reject(role, false);
}

// ── R-5e ──────────────────────────────────────────────────────────────────────────────

namespace {

// R-5e: A in Active, reset_on_logout and reset_on_disconnect, no clock, on the refusing double
// (for its record of outbound store() calls). The reply's store completes and then holds until
// a reset() is issued after close() began (hold_until_close_reset's condition, written as
// release_when so that the cell's release also ends it), and fromAdmin rejects the Logout.
// `close` starts close(terminal) during the hold; the forced hit releases the hold with no
// close().
void close_out_then_from_admin_reject(session_role const role, bool const close) {
    SCOPED_TRACE(std::string{rg::role_name(role)} + (close ? "" : "/no close()"));
    Rol104 r{Opts104{.role = role,
                     .reset_on_disconnect = true,
                     .store = store_kind::refusing,
                     .with_clock = false}};
    r.factory->hooks.hold_until_close_reset = true;
    r.factory->hooks.release_when = [c = &r.ctl, lg = r.factory->log.get()] {
        return c->released || lg->resets_issued_after_close_began != 0;
    };
    r.factory->log->close_began = [c = &r.close_run] { return c->started; };
    r.app->logout_from_admin = AdminCountingApp104::on_logout_from_admin::reject;
    auto& s = r.s;
    ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
    seqnum_t const n = s.next_in();
    auto const mark = s.sent.size();
    auto const stores_mark = r.ctl.side.outbound_store_types.size();
    r.ctl.hold_next_store = true;
    auto fut = s.feed_spawn(s.logout(n));
    bool const held = s.pump_until([&] { return !r.log().store_events.empty(); }, kReachBudget);
    bool close_returned = false;
    if (close) {
        r.spawn_close(close_mode::terminal);
        close_returned = r.close_returned_within(kReleaseBudget);
    } else {
        r.ctl.released = true;
    }
    bool const returned = s.pump_until([&] { return rg::Scene::ready(fut); }, kReleaseBudget);
    bool const hold_timed_out = r.log().hold_timed_out;
    r.ctl.released = true;

    EXPECT_TRUE(held) << "setup: the reply's store was not held";
    EXPECT_FALSE(hold_timed_out) << "the hold reached its bound";
    EXPECT_TRUE(returned) << "the Logout's frame did not return";
    EXPECT_EQ(stored_of_type(r.ctl.side, "5", stores_mark), 1U)
        << "setup: the reply's store call was not recorded";
    EXPECT_EQ(r.app->from_admin_count("5"), 1) << "setup: the Logout's fromAdmin calls";

    if (!close) {
        // The forced hit: no close-out, so the Reject is numbered, reaches toAdmin and is stored.
        EXPECT_EQ(r.app->to_admin_count("3"), 1) << "toAdmin calls for a 35=3";
        EXPECT_EQ(stored_of_type(r.ctl.side, "3", stores_mark), 1U) << "the Reject's store call";
        return;
    }
    EXPECT_EQ(r.app->to_admin_count("3"), 0) << "toAdmin calls for a 35=3";
    EXPECT_EQ(stored_of_type(r.ctl.side, "3", stores_mark), 0U) << "a Reject's store call";
    EXPECT_EQ(s.count("3", mark), 0U) << "a Reject was written: " << s.written(mark);
    EXPECT_EQ(disconnected_entries(*s.session), 1) << "Disconnected entries in the history";
    EXPECT_TRUE(close_returned) << "close() did not return";
    EXPECT_EQ(r.log().reset_to_calls, 0) << "a reset_to call";
    EXPECT_EQ(r.log().reset_calls, 1) << "reset() calls";
    Numbers104 const mgr = r.manager_numbers();
    EXPECT_EQ(mgr, (Numbers104{.in = 1, .out = 1}))
        << "the manager after close(): " << mgr.in << "/" << mgr.out;
}

}  // namespace

TEST(ResetOnLogout104, CloseOutThenFromAdminReject_RejectRefusedBeforeToAdmin) {
    for (auto const role : kRoles) close_out_then_from_admin_reject(role, true);
}

// R-5e's forced hit: the same fixture with no close(), the hold released by release_when.
TEST(ResetOnLogout104, CloseOutThenFromAdminReject_RejectRefusedBeforeToAdmin_NoClose) {
    for (auto const role : kRoles) close_out_then_from_admin_reject(role, false);
}

// ── R-5i ──────────────────────────────────────────────────────────────────────────────

namespace {

enum class reject_arm : std::uint8_t { sending_time, arm_d };

// R-5i: route B, the too-high Logout's S is N + 3. An in-sequence frame at N, below S, draws
// the SendingTime Reject (SendingTime ten minutes behind) or Arm D's (43=Y with OrigSendingTime
// after SendingTime). The bound is fired by its deadline, the mock clock passing
// logout_disconnect_timeout_ms during the hold. RolOn holds the Reject's store, selected at
// its toAdmin (nothing suspends from toAdmin to the store call); RolOff holds the frame's
// consume persist.
void in_flight_sending_time_reject(session_role const role, reject_arm const arm, bool const rol) {
    SCOPED_TRACE(std::string{rg::role_name(role)} +
                 (arm == reject_arm::sending_time ? "/SendingTime" : "/ArmD") +
                 (rol ? "/RolOn" : "/RolOff"));
    Rol104 r{Opts104{.role = role, .reset_on_logout = rol}};
    auto& s = r.s;
    ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
    seqnum_t const n = s.next_in();
    (void)s.feed(s.logout(n + 3));
    ASSERT_EQ(r.phase(), kPending) << "setup: no pending mark";
    ASSERT_EQ(r.bound_count(), 1) << "setup: no bound";
    auto const mark = s.sent.size();
    auto frame = arm == reject_arm::sending_time
                     ? s.order_with_stale_sending_time(n)
                     : s.frame("D", n,
                               "43=Y\x01"
                               "122=" +
                                   s.sending_time(std::chrono::seconds{60}) + "\x01" +
                                   rg::Scene::order_fields(n));
    if (rol) {
        r.app->to_admin_hook = [c = &r.ctl](std::string_view const t) {
            if (t == "3") c->hold_next_store = true;
        };
    } else {
        r.ctl.hold_next_persist = true;
    }
    auto fut = s.feed_spawn(std::move(frame));
    bool const held =
        s.pump_until([&] { return rol ? !r.log().store_events.empty() : !r.ctl.hold_next_persist; },
                     kReachBudget);
    s.mock->advance(std::chrono::milliseconds{kBoundT} + std::chrono::milliseconds{1});
    bool const bound_ended = s.pump_until([&] { return r.bound_count() == 0; }, kInHoldBudget);
    auto const acks_in_hold = s.count("5", mark);
    auto const state_in_hold = s.state();
    auto const events_in_hold = r.log().store_events.size();
    bool const hold_in_place = !r.log().hold_timed_out;
    r.ctl.released = true;
    r.app->to_admin_hook = nullptr;
    bool const returned = s.pump_until([&] { return rg::Scene::ready(fut); }, kReleaseBudget);
    std::optional<fixpp::core::expected_t<void>> result;
    if (returned) result = fut.get();

    EXPECT_EQ(disconnected_entries(*s.session), 1) << "Disconnected entries in the history";
    EXPECT_TRUE(result.has_value() && result->has_value())
        << "the frame's on_inbound_frame did not return success";
    // Reach: step 1 was passed (the Reject's toAdmin ran), and the bound wrote Disconnected
    // while the selected call was held.
    EXPECT_EQ(r.app->to_admin_count("3"), 1) << "toAdmin calls for a 35=3";
    EXPECT_TRUE(held) << "setup: the selected store call was not reached";
    EXPECT_TRUE(bound_ended) << "setup: the bound did not end during the hold";
    EXPECT_EQ(acks_in_hold, 1U) << "setup: the bound's acknowledgement: " << s.written(mark);
    EXPECT_EQ(state_in_hold, fsm_state::Disconnected)
        << "setup: the state during the hold: " << rg::state_name(state_in_hold);
    EXPECT_TRUE(hold_in_place) << "the hold reached its bound before the release";
    EXPECT_TRUE(returned) << "the frame's on_inbound_frame did not return";
    if (rol) {
        EXPECT_EQ(events_in_hold, 1U) << "setup: the Reject's store returned during the hold";
        expect_held_store_spans_the_reset_to(r.log());
    }
}

}  // namespace

TEST(ResetOnLogout104, InFlightSendingTimeRejectAcrossBound_OneDisconnected_SendingTime_RolOn) {
    for (auto const role : kRoles) {
        in_flight_sending_time_reject(role, reject_arm::sending_time, true);
    }
}

TEST(ResetOnLogout104, InFlightSendingTimeRejectAcrossBound_OneDisconnected_SendingTime_RolOff) {
    for (auto const role : kRoles) {
        in_flight_sending_time_reject(role, reject_arm::sending_time, false);
    }
}

TEST(ResetOnLogout104, InFlightSendingTimeRejectAcrossBound_OneDisconnected_ArmD_RolOn) {
    for (auto const role : kRoles) in_flight_sending_time_reject(role, reject_arm::arm_d, true);
}

TEST(ResetOnLogout104, InFlightSendingTimeRejectAcrossBound_OneDisconnected_ArmD_RolOff) {
    for (auto const role : kRoles) in_flight_sending_time_reject(role, reject_arm::arm_d, false);
}

// ── R-5j ──────────────────────────────────────────────────────────────────────────────

namespace {

// R-5j: a direct initiator hydrated at kHydratedN (R-1o's input), with an application. open()'s
// Logon store is held; a peer Logout at 34 = 2 is delivered to the LogonSent arm during the
// hold; then the hold is released and open() returns.
void direct_open_logon_across_logon_sent_reset(bool const rol) {
    SCOPED_TRACE(rol ? "" : "rol off");
    Rol104 r{Opts104{.role = session_role::initiator, .reset_on_logout = rol}};
    r.factory->log->inner = hydrated_store();
    auto& s = r.s;
    r.ctl.hold_next_store = true;
    auto const mark = s.sent.size();
    auto open_fut = asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                                   s.session->open(), asio::use_future);
    bool const held = s.pump_until([&] { return !r.log().store_events.empty(); }, kReachBudget);
    auto const state_at_hold = s.state();
    auto lo_fut = s.feed_spawn(s.logout(2));
    bool const logout_returned =
        s.pump_until([&] { return rg::Scene::ready(lo_fut); }, kInHoldBudget);
    auto const state_in_hold = s.state();
    auto const events_in_hold = r.log().store_events.size();
    bool const hold_in_place = !r.log().hold_timed_out;
    r.ctl.released = true;
    bool const open_returned =
        s.pump_until([&] { return rg::Scene::ready(open_fut); }, kReleaseBudget);
    std::optional<fixpp::core::expected_t<void>> open_r;
    if (open_returned) open_r = open_fut.get();

    EXPECT_TRUE(held) << "setup: the Logon's store was not held";
    EXPECT_EQ(state_at_hold, fsm_state::LogonSent)
        << "setup: the state at the hold: " << rg::state_name(state_at_hold);
    EXPECT_TRUE(logout_returned) << "setup: the peer Logout's frame did not return in the hold";
    EXPECT_EQ(state_in_hold, fsm_state::Disconnected)
        << "setup: the state after the LogonSent arm: " << rg::state_name(state_in_hold);
    EXPECT_EQ(events_in_hold, 1U) << "setup: the Logon's store returned during the hold";
    EXPECT_TRUE(hold_in_place) << "the hold reached its bound before the release";
    EXPECT_TRUE(open_returned) << "open() did not return after the release";
    EXPECT_TRUE(open_r.has_value() && open_r->has_value()) << "open() did not return success";

    if (!rol) {
        // The forced hit: no reset point, so the Logon is transmitted.
        EXPECT_EQ(s.count("A", mark), 1U) << "the Logon on the wire: " << s.written(mark);
        return;
    }
    EXPECT_EQ(s.count("A", mark), 0U) << "a 35=A was written: " << s.written(mark);
    EXPECT_EQ(s.state(), fsm_state::Disconnected) << rg::state_name(s.state());
    EXPECT_EQ(disconnected_entries(*s.session), 1) << "Disconnected entries in the history";
    expect_one_reset_to_one_one_disconnected(r);
    EXPECT_EQ(r.app->on_create_calls, 1) << "onCreate calls";
    EXPECT_EQ(r.app->state_at_create, std::optional<fsm_state>{fsm_state::Disconnected})
        << "the state inside onCreate: "
        << (r.app->state_at_create ? rg::state_name(*r.app->state_at_create) : "not read");
    EXPECT_EQ(r.app->on_logout_calls, 0) << "onLogout calls";
    expect_held_store_spans_the_reset_to(r.log());
}

}  // namespace

TEST(ResetOnLogout104, DirectOpenLogonAcrossLogonSentReset_OpenReturnsSuccess) {
    direct_open_logon_across_logon_sent_reset(true);
}

// R-5j's forced hit: reset_on_logout off.
TEST(ResetOnLogout104, DirectOpenLogonAcrossLogonSentReset_OpenReturnsSuccess_RolOff) {
    direct_open_logon_across_logon_sent_reset(false);
}

// ── T038 part B: R-5f, R-5g and R-5h, frames in flight across a reset point (contract RR-6) ──
//
// The live-transport arms (R-5f, R-5g GateWait, R-5h GateWait) run a direct acceptor with a
// BlockingWriteTransport attached through attach_accepted_transport (the acceptor's own seam,
// as R-1l attaches a ScriptedReadTransport), so they run on the acceptor only. Their wire is the
// transport's byte record: a frame is written when its entry is completed, and "after" a frame
// means a start index above that frame's. The config-time sink arms read the Scene's `sent`, on
// both roles. The two-site readings of the write-start count are taken by the store's
// release_when, at its first call that returns true, and by its on_reset, at the first reset
// call; on_reset routes that call through the hold path, so ctl.released is already true when a
// cell's unit runs.

namespace {

namespace bwt = fixpp::session::test;

// A recorded live write: its start index, MsgType and MsgSeqNum.
struct LiveFrame104 {
    std::size_t start = 0;
    std::string type;
    std::string seq;
};

// The completed writes of the byte record whose start index is above `after`, in start order.
std::vector<LiveFrame104> written_after(bwt::blocking_write_state const& st, std::size_t after) {
    std::vector<LiveFrame104> out;
    for (auto const& w : st.recorded) {
        if (!w.completed || w.start <= after) continue;
        out.push_back({.start = w.start,
                       .type = recorded_field(w.bytes, "35"),
                       .seq = recorded_field(w.bytes, "34")});
    }
    return out;
}
std::size_t count_type(std::vector<LiveFrame104> const& frames, std::string_view type) {
    std::size_t n = 0;
    for (auto const& f : frames) n += static_cast<std::size_t>(f.type == type);
    return n;
}
std::string describe(std::vector<LiveFrame104> const& frames) {
    std::string out;
    for (auto const& f : frames) {
        out += (out.empty() ? "" : " ") + std::string{"35="} + f.type + "/34=" + f.seq + "@" +
               std::to_string(f.start);
    }
    return out.empty() ? std::string{"nothing"} : out;
}

// Opens a direct acceptor with the byte record on, attaches the held-write transport, and feeds
// the peer's Logon; true when the session is Active.
bool open_live_acceptor(Rol104& r, std::shared_ptr<bwt::blocking_write_state> const& st) {
    auto& s = r.s;
    st->record_bytes = true;
    auto open_fut = asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                                   s.session->open(), asio::use_future);
    if (!s.pump_until([&] { return rg::Scene::ready(open_fut); })) return false;
    if (!open_fut.get().has_value()) return false;
    s.session->attach_accepted_transport(
        std::make_unique<bwt::BlockingWriteTransport>(s.ioc.get_executor(),
                                                      std::make_shared<bwt::ScriptedStream>(), st),
        fixpp::transport::handshake_result{});
    (void)s.feed(s.logon(1));
    return s.state() == fsm_state::Active;
}

// The two readings of the write-start count, and what on_reset reads beside it.
struct GateReadings104 {
    std::optional<std::size_t> starts_at_release;  // the first release_when call returning true
    bool parked_at_release = false;
    std::optional<std::size_t> starts_at_reset;  // on_reset, at the first reset call
    bool awaiting_resend_at_reset = false;
    // The first reset call's kind (HookedStore records a call before it fires on_reset): a
    // reset_to is the unit's; a reset() is a teardown's, made where no unit ran.
    std::optional<StoreLog::call_kind> reset_kind;
    [[nodiscard]] bool at_unit() const { return reset_kind == StoreLog::call_kind::reset_to; }
};
void install_gate_readings(Rol104& r, GateReadings104& g,
                           std::shared_ptr<bwt::blocking_write_state> const& st) {
    r.factory->hooks.release_when = [c = &r.ctl, gr = &g, st] {
        if (c->released && !gr->starts_at_release) {
            gr->starts_at_release = st->starts;
            gr->parked_at_release = st->parked;
        }
        return c->released;
    };
    r.factory->hooks.on_reset = [gr = &g, st, sess = r.s.session.get(), lg = r.factory->log.get()] {
        if (!lg->calls.empty()) gr->reset_kind = lg->calls.back().kind;
        gr->starts_at_reset = st->starts;
        gr->awaiting_resend_at_reset = session_test_access::resend_outstanding(*sess);
    };
}

// C2 on the one selected outbound store: both its events are stamped before the unit's
// "reset_to 1 1" completed, and the write-start count read at its release equals the count read
// at the unit's reset call.
void expect_store_returned_before_the_reset_to(StoreLog const& log, GateReadings104 const& g) {
    auto const at = reset_to_one_one_at(log);
    ASSERT_EQ(log.store_events.size(), 2U) << "the selected store's two events";
    EXPECT_TRUE(at.has_value()) << "C2: no reset_to(1, 1) on the record; reset calls: reset() "
                                << log.reset_calls << ", reset_to " << log.reset_to_calls;
    EXPECT_TRUE(g.at_unit()) << "C2: the first reset call was not the unit's reset_to";
    if (at) {
        EXPECT_LE(log.store_events[0].writes_at, *at)
            << "C2: store_applied is not before the unit's reset_to";
        EXPECT_LE(log.store_events[1].writes_at, *at)
            << "C2: store_returned is not before the unit's reset_to";
    }
    if (g.starts_at_release && g.starts_at_reset && g.at_unit()) {
        EXPECT_EQ(*g.starts_at_release, *g.starts_at_reset)
            << "C2: a write started between store_returned and the unit's reset call";
    }
}

// A one-shot toAdmin selector: at the first toAdmin of `type`, the next outbound store() is
// selected (nothing suspends from that toAdmin to its store call).
struct StoreSelector104 {
    bool selected = false;
};
void select_store_at_to_admin(Rol104& r, StoreSelector104& sel, std::string const type) {
    r.app->to_admin_hook = [c = &r.ctl, sl = &sel, type](std::string_view const t) {
        if (t != type || sl->selected) return;
        sl->selected = true;
        c->hold_next_store = true;
    };
}

// ── R-5f ──────────────────────────────────────────────────────────────────────────────

// R-5f: route B on the live transport and the refusing double (for its store-call record).
// S = N + 3, the gap filled to S by one GapFill, so AwaitingResend is off and the bound is not
// woken. A ResendRequest at S + 1 with no BeginSeqNo(7) draws a Reject, whose store is
// selected at its toAdmin and held. The mock clock passes logout_disconnect_timeout_ms; the
// bound's reply is stored and its write parks. The Reject's store is released, passes D-OUT-10
// and waits at the write gate; then the reply's write is released.
void too_high_resend_request_across_bound_unit(bool const rol) {
    SCOPED_TRACE(rol ? "rol" : "rol off");
    GateReadings104 g;  // declared before the session: its hooks write them
    StoreSelector104 sel;
    auto const st = std::make_shared<bwt::blocking_write_state>();
    Rol104 r{Opts104{
        .role = session_role::acceptor, .reset_on_logout = rol, .store = store_kind::refusing}};
    install_gate_readings(r, g, st);
    select_store_at_to_admin(r, sel, "3");
    auto& s = r.s;
    ASSERT_TRUE(open_live_acceptor(r, st)) << "setup: the session did not reach Active";
    seqnum_t const n = s.next_in();
    seqnum_t const big_s = n + 3;
    (void)s.feed(s.logout(big_s));
    ASSERT_EQ(r.phase(), kPending) << "setup: no pending mark";
    (void)s.feed(s.gap_fill(n, big_s));
    ASSERT_EQ(s.next_in(), big_s) << "setup: the gap was not filled to S";
    ASSERT_EQ(r.phase(), kPending) << "setup: the mark left pending at S";
    ASSERT_EQ(r.bound_count(), 1) << "setup: no bound";
    ASSERT_FALSE(s.resend_outstanding()) << "setup: AwaitingResend is still on";
    int const baseline = r.app->to_admin_count("2");
    auto const stores_mark = r.ctl.side.outbound_store_types.size();

    auto fut = s.feed_spawn(s.frame("2", big_s + 1, "16=0\x01"));
    bool const held = s.pump_until([&] { return !r.log().store_events.empty(); }, kReachBudget);
    st->arm_block();
    s.mock->advance(std::chrono::milliseconds{kBoundT} + std::chrono::milliseconds{1});
    bool const reply_parked = s.pump_until([&] { return st->parked; }, kInHoldBudget);
    bool const parked_is_reply = !st->recorded.empty() &&
                                 recorded_field(st->recorded.back().bytes, "35") == "5" &&
                                 !st->recorded.back().completed;
    std::size_t const reply_start = st->recorded.empty() ? 0 : st->recorded.back().start;
    r.ctl.released = true;
    bool const at_gate = s.pump_until(
        [&] { return g.starts_at_release.has_value() && r.log().store_events.size() == 2; },
        kHoldWindow);
    std::size_t const starts_before_reply_release = st->starts;
    bool const hold_in_place = !r.log().hold_timed_out;
    st->release();
    bool const returned = s.pump_until([&] { return rg::Scene::ready(fut); }, kReleaseBudget);
    bool const bound_ended = s.pump_until([&] { return r.bound_count() == 0; }, kReleaseBudget);
    std::optional<fixpp::core::expected_t<void>> result;
    if (returned) result = fut.get();
    auto const after_reply = written_after(*st, reply_start);
    Numbers104 const mgr = r.manager_numbers();

    if (rol) {
        EXPECT_EQ(r.app->to_admin_count("2"), baseline) << "toAdmin calls for a 35=2";
    }
    // Reach, readable on the base: the Reject's store was held while the reply's write parked,
    // and after its release no write started until the reply's write was released.
    EXPECT_TRUE(held) << "setup: the Reject's store was not held";
    EXPECT_EQ(r.app->to_admin_count("3"), 1) << "toAdmin calls for a 35=3";
    EXPECT_TRUE(reply_parked && parked_is_reply) << "setup: the bound's reply did not park";
    EXPECT_TRUE(at_gate) << "setup: the Reject's store did not return after its release";
    EXPECT_TRUE(g.parked_at_release) << "setup: no write was parked at the Reject's release";
    if (g.starts_at_release) {
        EXPECT_EQ(starts_before_reply_release, *g.starts_at_release)
            << "a write started while the Reject waited behind the parked reply";
    }
    EXPECT_TRUE(hold_in_place) << "the hold reached its bound before the release";
    EXPECT_TRUE(returned) << "the ResendRequest's on_inbound_frame did not return";
    EXPECT_TRUE(bound_ended) << "the bound did not end";

    if (!rol) {
        // The forced hit: nothing is frozen, so the leaf calls toAdmin once more, and the first
        // write started after the reply is the Reject.
        EXPECT_EQ(r.app->to_admin_count("2"), baseline + 1) << "toAdmin calls for a 35=2";
        EXPECT_TRUE(!after_reply.empty() && after_reply.front().type == "3" &&
                    after_reply.front().start == reply_start + 1)
            << "written after the reply: " << describe(after_reply);
        return;
    }
    EXPECT_EQ(stored_of_type(r.ctl.side, "2", stores_mark), 0U)
        << "a ResendRequest's store call after the input";
    EXPECT_EQ(count_type(after_reply, "2"), 0U)
        << "a ResendRequest written after the reply: " << describe(after_reply);
    EXPECT_EQ(count_type(after_reply, "3"), 0U)
        << "a Reject written after the reply: " << describe(after_reply);
    EXPECT_EQ(mgr, (Numbers104{.in = 1, .out = 1}))
        << "the manager after the release: " << mgr.in << "/" << mgr.out;
    auto const store = r.store_numbers(store_kind::refusing);
    ASSERT_TRUE(store.has_value()) << "the store's numbers were not read";
    EXPECT_EQ(*store, (Numbers104{.in = 1, .out = 1}))
        << "the store after the release: " << store->in << "/" << store->out;
    expect_one_reset_to_one_one_disconnected(r);
    EXPECT_TRUE(result.has_value() && result->has_value())
        << "the ResendRequest's on_inbound_frame did not return success";
    EXPECT_EQ(disconnected_entries(*s.session), 1) << "Disconnected entries in the history";
    // Reach at the head: the arm reached the leaf after the reset point.
    EXPECT_TRUE(g.at_unit()) << "the first reset call was not the unit's reset_to";
    if (g.at_unit()) {
        EXPECT_FALSE(g.awaiting_resend_at_reset)
            << "AwaitingResend was on at the unit's reset call";
    }
    EXPECT_TRUE(s.resend_outstanding()) << "AwaitingResend is off after the release";
    expect_store_returned_before_the_reset_to(r.log(), g);
}

}  // namespace

TEST(ResetOnLogout104, TooHighResendRequestAcrossBoundUnit_NoToAdminForRefusedNumber) {
    too_high_resend_request_across_bound_unit(true);
}

// R-5f's forced hit: the same input with reset_on_logout off.
TEST(ResetOnLogout104, TooHighResendRequestAcrossBoundUnit_NoToAdminForRefusedNumber_RolOff) {
    too_high_resend_request_across_bound_unit(false);
}

// ── R-5g ──────────────────────────────────────────────────────────────────────────────

namespace {

enum class crossing_arm : std::uint8_t { store_held, gate_wait };

// R-5g: A in Active, close(graceful) with a clock. Phase 1's Logout store is selected at its
// toAdmin. StoreHeld (C1, on the config-time sink): the store is held while an in-sequence
// peer Logout reaches arm A, then released. GateWait (C2, on the live transport): an
// application send's write is parked first, so phase 1's Logout, recorded with no hold, waits at
// the write gate while the peer Logout reaches arm A; then the parked write is released.
void crossing_logout(session_role const role, crossing_arm const arm, bool const rol) {
    SCOPED_TRACE(std::string{rg::role_name(role)} +
                 (arm == crossing_arm::store_held ? "/StoreHeld" : "/GateWait") +
                 (rol ? "" : "/rol off"));
    bool const live = arm == crossing_arm::gate_wait;
    GateReadings104 g;  // declared before the session: its hooks write them
    StoreSelector104 sel;
    auto const st = std::make_shared<bwt::blocking_write_state>();
    Rol104 r{Opts104{.role = role, .reset_on_logout = rol}};
    if (live) {
        install_gate_readings(r, g, st);
        r.ctl.released = true;  // the selected store records both events with no hold
    }
    select_store_at_to_admin(r, sel, "5");
    auto& s = r.s;
    if (live) {
        ASSERT_TRUE(open_live_acceptor(r, st)) << "setup: the session did not reach Active";
    } else {
        ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
    }
    seqnum_t const n = s.next_in();
    auto const sent_mark = s.sent.size();
    std::size_t const wire_mark = st->starts;

    std::future<fixpp::core::expected_t<void>> send_fut;
    bool app_parked = true;
    std::size_t app_start = 0;
    if (live) {
        std::string const payload =
            "35=D\x01"
            "11=ORD-104-R5G\x01" +
            std::string{rg::kSideAndTime};
        r.frames.emplace_back(reinterpret_cast<std::byte const*>(payload.data()),
                              reinterpret_cast<std::byte const*>(payload.data()) + payload.size());
        st->arm_block();
        send_fut = asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                                  s.session->send(std::span<const std::byte>{r.frames.back()}),
                                  asio::use_future);
        app_parked = s.pump_until([&] { return st->parked; }, kReachBudget);
        app_start = st->starts;
    }
    r.spawn_close(close_mode::graceful);
    bool const selected =
        s.pump_until([&] { return r.log().store_events.size() == (live ? 2U : 1U); }, kReachBudget);
    auto lo_fut = s.feed_spawn(s.logout(n));
    bool const frame_returned =
        s.pump_until([&] { return rg::Scene::ready(lo_fut); }, kInHoldBudget);
    auto const sent_at_frame = s.sent.size();
    std::size_t const starts_at_frame = st->starts;
    std::size_t const logouts_before_release =
        live ? count_type(written_after(*st, wire_mark), "5") : s.count("5", sent_mark);
    bool const hold_in_place = !r.log().hold_timed_out;
    if (live) {
        st->release();
    } else {
        r.ctl.released = true;
    }
    bool const close_returned = r.close_returned_within(kReleaseBudget);
    r.ctl.released = true;
    if (live) st->release();

    std::string const held_seq =
        r.log().store_events.empty() ? std::string{} : std::to_string(r.log().store_events[0].seq);
    std::vector<std::string> logouts;  // the 34 of each 35=5 written by fixpp from the mark
    if (live) {
        for (auto const& f : written_after(*st, wire_mark)) {
            if (f.type == "5") logouts.push_back(f.seq);
        }
    } else {
        for (auto const& f : s.of_type("5", sent_mark)) logouts.push_back(rg::field(f, 34));
    }

    EXPECT_EQ(logouts.size(), 1U) << "35=5 frames written by fixpp: " << logouts.size();
    if (logouts.size() == 1U) {
        EXPECT_EQ(logouts.front(), held_seq) << "the 35=5 written is not phase 1's Logout";
    }
    // Reach, readable on the base.
    EXPECT_TRUE(app_parked) << "setup: the application frame's write did not park";
    EXPECT_TRUE(selected) << "setup: phase 1's Logout store was not reached";
    EXPECT_TRUE(frame_returned) << "setup: the peer Logout's frame did not return";
    EXPECT_EQ(logouts_before_release, 0U) << "a 35=5 written before the release";
    EXPECT_TRUE(hold_in_place) << "the hold reached its bound before the release";
    EXPECT_TRUE(close_returned) << "close() did not return";
    if (live) {
        EXPECT_TRUE(g.parked_at_release) << "setup: no write was parked at the Logout's store";
        if (g.starts_at_release) {
            EXPECT_EQ(starts_at_frame, *g.starts_at_release)
                << "a write started while phase 1's Logout waited behind the parked frame";
        }
        auto const after_frame = written_after(*st, starts_at_frame);
        EXPECT_TRUE(after_frame.size() == 1U && after_frame.front().type == "5")
            << "written after the peer Logout's frame returned: " << describe(after_frame);
        if (send_fut.valid()) {
            EXPECT_TRUE(s.pump_until([&] { return rg::Scene::ready(send_fut); }, kReleaseBudget))
                << "send() did not return";
        }
    } else {
        EXPECT_EQ(s.sent.size() - sent_at_frame, 1U)
            << "written after the peer Logout's frame returned: " << s.written(sent_at_frame);
    }

    if (!rol) {
        if (live) {
            // The forced hit: the first write started after the parked frame is our Logout.
            auto const after_app = written_after(*st, app_start);
            EXPECT_TRUE(!after_app.empty() && after_app.front().type == "5" &&
                        after_app.front().start == app_start + 1)
                << "written after the application frame: " << describe(after_app);
        }
        return;
    }
    expect_one_reset_to_one_one_disconnected(r);
    auto const store = r.store_numbers(store_kind::hooked);
    ASSERT_TRUE(store.has_value()) << "the store's numbers were not read";
    EXPECT_EQ(*store, (Numbers104{.in = 1, .out = 1}))
        << "the store after close(): " << store->in << "/" << store->out;
    EXPECT_EQ(disconnected_entries(*s.session), 1) << "Disconnected entries in the history";
    if (live) {
        if (g.starts_at_reset && g.at_unit()) {
            auto const after_reset = written_after(*st, *g.starts_at_reset);
            EXPECT_TRUE(after_reset.size() == 1U && after_reset.front().type == "5")
                << "written after the reset point: " << describe(after_reset);
        }
        expect_store_returned_before_the_reset_to(r.log(), g);
    } else {
        expect_held_store_spans_the_reset_to(r.log());
    }
}

}  // namespace

TEST(ResetOnLogout104, CrossingLogout_OneLogoutOurs_StoreHeld) {
    for (auto const role : kRoles) crossing_logout(role, crossing_arm::store_held, true);
}

TEST(ResetOnLogout104, CrossingLogout_OneLogoutOurs_GateWait) {
    crossing_logout(session_role::acceptor, crossing_arm::gate_wait, true);
}

// R-5g's forced hits, one per observation path: reset_on_logout off.
TEST(ResetOnLogout104, CrossingLogout_OneLogoutOurs_StoreHeld_RolOff) {
    for (auto const role : kRoles) crossing_logout(role, crossing_arm::store_held, false);
}

TEST(ResetOnLogout104, CrossingLogout_OneLogoutOurs_GateWait_RolOff) {
    crossing_logout(session_role::acceptor, crossing_arm::gate_wait, false);
}

// ── R-5h ──────────────────────────────────────────────────────────────────────────────

namespace {

enum class liveness_arm : std::uint8_t { gate_wait, store_held, store_held_at_arm_l };

// StoreHeldAtArmL's pumps inside the Heartbeat's hold: until the hold, until LogoutSent, until
// the peer Logout's frame returns. Each is a quarter of kHoldBound.
static_assert(kReachBudget + kInHoldBudget + kInHoldBudget < kHoldBound);

// R-5h. The Heartbeat's store is selected at its toAdmin. GateWait (C2, live transport,
// HeartBtInt above logout_disconnect_timeout_ms): route B as R-5f's setup; the mock clock passes
// logout_disconnect_timeout_ms with the next write armed to park, so the bound's reply parks;
// then it passes HeartBtInt, and the Heartbeat, recorded with no hold, waits at the write gate;
// then the reply's write is released. StoreHeld (C1, sink, HeartBtInt half of
// logout_disconnect_timeout_ms): route B; the clock passes HeartBtInt, the Heartbeat's store is
// held, then the clock passes logout_disconnect_timeout_ms (the loop is suspended in that
// store), the bound acknowledges and runs its unit, and the store is released.
// StoreHeldAtArmL (C1, sink): A in Active; the Heartbeat's store is held while close(graceful)'s
// phase 1 sends its Logout and the peer's Logout reaches arm L; then it is released.
void liveness_heartbeat_across_unit(session_role const role, liveness_arm const arm,
                                    bool const rol) {
    SCOPED_TRACE(std::string{rg::role_name(role)} +
                 (arm == liveness_arm::gate_wait
                      ? "/GateWait"
                      : (arm == liveness_arm::store_held ? "/StoreHeld" : "/StoreHeldAtArmL")) +
                 (rol ? "" : "/rol off"));
    bool const live = arm == liveness_arm::gate_wait;
    GateReadings104 g;  // declared before the session: its hooks write them
    StoreSelector104 sel;
    auto const st = std::make_shared<bwt::blocking_write_state>();
    Rol104 r{Opts104{.role = role,
                     .reset_on_logout = rol,
                     .heartbeat = live ? rg::kHeartBtInt : kSmallHeartBtInt}};
    if (live) {
        install_gate_readings(r, g, st);
        r.ctl.released = true;  // the selected store records both events with no hold
    }
    select_store_at_to_admin(r, sel, "0");
    auto& s = r.s;
    if (live) {
        ASSERT_TRUE(open_live_acceptor(r, st)) << "setup: the session did not reach Active";
    } else {
        ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
    }
    seqnum_t const n = s.next_in();
    if (arm != liveness_arm::store_held_at_arm_l) {
        (void)s.feed(s.logout(n + 3));
        ASSERT_EQ(r.phase(), kPending) << "setup: no pending mark";
        (void)s.feed(s.gap_fill(n, n + 3));
        ASSERT_EQ(s.next_in(), n + 3) << "setup: the gap was not filled to S";
        ASSERT_EQ(r.phase(), kPending) << "setup: the mark left pending at S";
        ASSERT_FALSE(s.resend_outstanding()) << "setup: AwaitingResend is still on";
    }
    auto const sent_mark = s.sent.size();
    std::size_t reply_start = 0;  // live: the reply's start index
    bool reached = false;         // the selected store was reached (and, live, the reply parked)
    bool during = false;          // the work run during the hold or the gate wait completed
    std::size_t starts_before_release = 0;
    std::future<fixpp::core::expected_t<void>> lo_fut;
    if (live) {
        st->arm_block();
        s.mock->advance(std::chrono::milliseconds{kBoundT} + std::chrono::milliseconds{1});
        bool const parked = s.pump_until([&] { return st->parked; }, kReachBudget);
        bool const is_reply = !st->recorded.empty() &&
                              recorded_field(st->recorded.back().bytes, "35") == "5" &&
                              !st->recorded.back().completed;
        reply_start = st->recorded.empty() ? 0 : st->recorded.back().start;
        s.mock->advance(rg::kHeartBtInt);
        reached = parked && is_reply &&
                  s.pump_until([&] { return r.log().store_events.size() == 2; }, kInHoldBudget);
        during = s.state() == fsm_state::Active;  // the reply is still in its write
        starts_before_release = st->starts;
        st->release();
    } else {
        s.mock->advance(kSmallHeartBtInt + std::chrono::milliseconds{1});
        reached = s.pump_until([&] { return !r.log().store_events.empty(); }, kReachBudget);
        if (arm == liveness_arm::store_held) {
            s.mock->advance(std::chrono::milliseconds{kBoundT} - kSmallHeartBtInt);
            during = s.pump_until([&] { return r.bound_count() == 0; }, kInHoldBudget) &&
                     s.count("5", sent_mark) == 1U && s.state() == fsm_state::Disconnected;
        } else {
            r.spawn_close(close_mode::graceful);
            bool const logout_sent = s.pump_until(
                [&] { return s.state() == fsm_state::LogoutSent && s.count("5", sent_mark) == 1U; },
                kInHoldBudget);
            lo_fut = s.feed_spawn(s.logout(n));
            during = logout_sent &&
                     s.pump_until([&] { return rg::Scene::ready(lo_fut); }, kInHoldBudget) &&
                     s.state() == fsm_state::Disconnected;
        }
        r.ctl.released = true;
    }
    bool const hold_in_place = !r.log().hold_timed_out;
    bool const loop_ended = s.pump_until(
        [&] { return session_test_access::liveness_count(*s.session) == 0; }, kReleaseBudget);
    bool close_returned = true;
    if (arm == liveness_arm::store_held_at_arm_l) {
        close_returned = r.close_returned_within(kReleaseBudget);
    }
    r.ctl.released = true;
    if (live) (void)s.pump_until([&] { return r.bound_count() == 0; }, kReleaseBudget);

    // The frames written after the reply (route B: the bound's 35=5; arm L: phase 1's 35=5).
    std::vector<std::string> after;  // MsgTypes
    if (live) {
        for (auto const& f : written_after(*st, reply_start)) after.push_back(f.type);
    } else {
        std::size_t i = sent_mark;
        while (i < s.sent.size() && rg::field(s.sent[i], 35) != "5") ++i;
        for (std::size_t j = i + 1; j < s.sent.size(); ++j)
            after.push_back(rg::field(s.sent[j], 35));
    }
    std::string after_text;
    for (auto const& t : after) after_text += "35=" + t + " ";
    std::size_t heartbeats_after = 0;
    for (auto const& t : after) heartbeats_after += static_cast<std::size_t>(t == "0");

    if (rol) {
        EXPECT_EQ(heartbeats_after, 0U) << "a 35=0 written after the reply: " << after_text;
    }
    // Reach, readable on the base.
    EXPECT_TRUE(reached) << "setup: the Heartbeat's store was not reached";
    EXPECT_TRUE(during) << "setup: the work during the hold did not complete";
    EXPECT_TRUE(hold_in_place) << "the hold reached its bound before the release";
    EXPECT_TRUE(loop_ended) << "the liveness loop did not return";
    EXPECT_TRUE(close_returned) << "close() did not return";
    if (live) {
        EXPECT_TRUE(g.parked_at_release)
            << "setup: the reply was not parked at the Heartbeat's store";
        if (g.starts_at_release) {
            EXPECT_EQ(starts_before_release, *g.starts_at_release)
                << "a write started while the Heartbeat waited behind the parked reply";
        }
    }

    if (!rol) {
        // The forced hit: nothing is frozen, so the Heartbeat is written after the reply; on the
        // live transport it is the first write started after it.
        EXPECT_GE(heartbeats_after, 1U) << "written after the reply: " << after_text;
        if (live) {
            auto const w = written_after(*st, reply_start);
            EXPECT_TRUE(!w.empty() && w.front().type == "0" && w.front().start == reply_start + 1)
                << "written after the reply: " << describe(w);
        }
        return;
    }
    EXPECT_TRUE(after.empty()) << "written after the reply: " << after_text;
    Numbers104 const mgr = r.manager_numbers();
    EXPECT_EQ(mgr, (Numbers104{.in = 1, .out = 1}))
        << "the manager after the release: " << mgr.in << "/" << mgr.out;
    auto const store = r.store_numbers(store_kind::hooked);
    ASSERT_TRUE(store.has_value()) << "the store's numbers were not read";
    EXPECT_EQ(*store, (Numbers104{.in = 1, .out = 1}))
        << "the store after the release: " << store->in << "/" << store->out;
    EXPECT_EQ(disconnected_entries(*s.session), 1) << "Disconnected entries in the history";
    if (live) {
        expect_store_returned_before_the_reset_to(r.log(), g);
    } else {
        expect_held_store_spans_the_reset_to(r.log());
    }
}

}  // namespace

TEST(ResetOnLogout104, LivenessHeartbeatAcrossBoundUnit_OneEnding_GateWait) {
    liveness_heartbeat_across_unit(session_role::acceptor, liveness_arm::gate_wait, true);
}

TEST(ResetOnLogout104, LivenessHeartbeatAcrossBoundUnit_OneEnding_StoreHeld) {
    for (auto const role : kRoles) {
        liveness_heartbeat_across_unit(role, liveness_arm::store_held, true);
    }
}

TEST(ResetOnLogout104, LivenessHeartbeatAcrossBoundUnit_OneEnding_StoreHeldAtArmL) {
    for (auto const role : kRoles) {
        liveness_heartbeat_across_unit(role, liveness_arm::store_held_at_arm_l, true);
    }
}

// R-5h's forced hits, one per observation path: reset_on_logout off.
TEST(ResetOnLogout104, LivenessHeartbeatAcrossBoundUnit_OneEnding_GateWait_RolOff) {
    liveness_heartbeat_across_unit(session_role::acceptor, liveness_arm::gate_wait, false);
}

TEST(ResetOnLogout104, LivenessHeartbeatAcrossBoundUnit_OneEnding_StoreHeld_RolOff) {
    for (auto const role : kRoles) {
        liveness_heartbeat_across_unit(role, liveness_arm::store_held, false);
    }
}
