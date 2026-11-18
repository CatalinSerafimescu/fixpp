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
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/message_store_factory.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_event.hpp>
#include <fixpp/session/session_fsm.hpp>
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

#include "session/plain_engine_rig.hpp"
#include "session/resend_gap_scene.hpp"
#include "support/blocking_write_transport.hpp"
#include "support/hooked_store.hpp"
#include "support/session_executor_of.hpp"
#include "support/session_test_access.hpp"
#include "support/store_double.hpp"

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
    void onLogout(const fixpp::session::SessionId& /*id*/) override {
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

enum class store_kind : std::uint8_t { hooked, memory, store_double };

// What a Rol104 builds; the defaults are R-1y's and R-1z's.
struct Opts104 {
    session_role role = session_role::initiator;
    bool reset_on_logout = true;
    bool reset_on_disconnect = false;
    store_kind store = store_kind::hooked;
    std::uint32_t logout_timeout_ms = kBoundT;
    bool with_clock = true;
};

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
    std::shared_ptr<StoreDoubleFactory104> double_factory =
        std::make_shared<StoreDoubleFactory104>();
    std::shared_ptr<AdminCountingApp104> app = std::make_shared<AdminCountingApp104>();
    rg::Scene s;

    explicit Rol104(session_role const role) : Rol104(Opts104{.role = role}) {}
    explicit Rol104(Opts104 const& o)
        : s{rg::Options{.role = o.role,
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
        // These cells read the state at a reset call; the transport reading is not read.
        factory->log->call_snapshot = [this] { return CallReading{.state = s.session->state()}; };
        s.cfg.reset_on_logout = o.reset_on_logout;
        s.cfg.reset_on_disconnect = o.reset_on_disconnect;
        if (o.store == store_kind::hooked) s.cfg.store_factory = factory;
        if (o.store == store_kind::store_double) s.cfg.store_factory = double_factory;
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
        fixpp::session::MessageStore* st = k == store_kind::hooked
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

// The one reset of a receipt: a reset_to(1, 1) seen with the session Disconnected and its
// transport open, and no other reset call.
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
