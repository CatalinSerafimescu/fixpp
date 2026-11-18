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

// Counts fromAdmin calls by MsgType, as 103's TooHighLogoutState103 cells do. A cell can make
// fromAdmin throw on or reject a Logout(35=5), make toAdmin throw on one, and read the
// manager's numbers inside onLogout.
class AdminCountingApp104 final : public rg::GapApp {
public:
    enum class on_logout_from_admin : std::uint8_t { accept, throw_, reject };
    on_logout_from_admin logout_from_admin = on_logout_from_admin::accept;
    bool throw_on_logout_to_admin = false;
    fixpp::session::Session const* session = nullptr;  // read inside onLogout when set

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
        if (throw_on_logout_to_admin && msg.msg_type() == "5") {
            throw std::runtime_error("AdminCountingApp104: toAdmin");
        }
    }
    std::function<void()> on_logout_hook;  // run inside each onLogout, after the reading
    void onLogout(const fixpp::session::SessionId& /*id*/) override {
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
// store_io_failure. Every other call, the reset calls and the flush included, goes to the
// HookedStore, so its per-call record and its log read as for a HookedStore.
enum class refuse_mode : std::uint8_t { none, cancelled, io_failure };
class RefusingStore104 final : public fixpp::session::MessageStore {
public:
    RefusingStore104(std::unique_ptr<HookedStore> inner, refuse_mode* next) noexcept
        : fixpp::session::MessageStore(flush_thunk_for<RefusingStore104>()),
          inner_{std::move(inner)},
          next_{next} {}
    asio::awaitable<fixpp::core::expected_t<void>> store(seqnum_t seq,
                                                         std::span<const std::byte> frame,
                                                         direction_t dir) noexcept override {
        if (dir == direction_t::outbound && *next_ != refuse_mode::none) {
            auto const m = *next_;
            *next_ = refuse_mode::none;
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
    std::unique_ptr<HookedStore> inner_;
    refuse_mode* next_;
};
class RefusingStoreFactory104 final : public fixpp::session::MessageStoreFactory {
public:
    RefusingStoreFactory104(std::shared_ptr<HookedStoreFactory> hooked, refuse_mode* next)
        : hooked_{std::move(hooked)}, next_{next} {}
    [[nodiscard]] bool yields_persistent_store() const noexcept override { return true; }
    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::session::MessageStore>> make(
        std::string_view, std::string_view, std::pmr::memory_resource*, std::size_t,
        asio::any_io_executor) noexcept override {
        ++hooked_->log->stores_made;
        return std::make_unique<RefusingStore104>(
            std::make_unique<HookedStore>(hooked_->outbound_next, std::move(hooked_->hooks),
                                          hooked_->log, hooked_->mode),
            next_);
    }

private:
    std::shared_ptr<HookedStoreFactory> hooked_;
    refuse_mode* next_;
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
            s.cfg.store_factory =
                std::make_shared<RefusingStoreFactory104>(factory, &ctl.refuse_next_store);
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
        std::string const written_in_hold = s.written(after_reply);
        bool const hold_in_place = !r.log().hold_timed_out;
        r.ctl.released = true;
        bool const frame_returned =
            s.pump_until([&] { return rg::Scene::ready(frame_fut); }, kReleaseBudget);
        Numbers104 const mgr = r.manager_numbers();

        EXPECT_EQ(s.count("5", mark), 1U) << "setup: A's reply: " << s.written(mark);
        EXPECT_TRUE(held) << "no reset_to was held at the receipt; reset calls: reset() "
                          << r.log().reset_calls << ", reset_to " << r.log().reset_to_calls;
        EXPECT_TRUE(written_in_hold.empty())
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

// R-1r: route B, on the pump route research D10's R-1r row and T011's record name. The
// Logout is too high by one; a GapFill fills to S; a TestRequest at S passes it, which wakes the
// bound, and the TestRequest's Heartbeat store is held while the bound acknowledges and runs
// its unit. After the release nothing that arm writes reaches the wire or the store's numbers,
// and the store and the manager read (1, 1).
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
