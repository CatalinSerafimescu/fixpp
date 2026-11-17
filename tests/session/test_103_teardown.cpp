// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_103_teardown.cpp — test-only.
//
// Feature 103 (specs/103-resend-logon-reset-fidelity/), cell G9 (research D1, the lifetime
// rule; tasks T119). A session coroutine frame is destroyed mid-body only while its
// execution context is destroyed; its RAII destructors then need only be memory-safe and
// restore no invariant. This cell destroys the io_context with an inbound chain suspended
// in its reply's store call and a Session::send suspended in its own, the Session alive,
// and then destroys the Session. Read it in a Debug build and under ASan and UBSan: no
// report and no assertion.
//
// Preconditions, as the rule states them: the Session outlives the frames (it is
// destroyed after the io_context), and nothing runs on the context after it is destroyed.
// No contended gate wait is ever made, so the replay gate has no timer.
//
// The executor is the bare io_context executor (direct_executor; one thread runs the
// context, so it is serialised). Under the default per_session_strand mode the Session holds
// a strand of the io_context, and nothing holding a strand of a context may outlive it, so
// there the Session cannot be destroyed after the context and the teardown is close and
// drain first (the B-103 teardown condition, tasks T151). The cell does not run that mode.
//
// Bucket: session_103_tests. The cell owns its io_context and destroys it inside the cell.
#include <gtest/gtest.h>

#include <array>
#include <asio/as_tuple.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/memory_store_factory.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "session/resend_gap_scene.hpp"
#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"
#include "support/recording_store.hpp"
#include "support/session_executor_of.hpp"
#include "support/session_test_access.hpp"

namespace {

namespace rg = fixpp::test_support::resend_gap;
using fixpp::session::fsm_state;
using fixpp::session::session_test_access;
using fixpp::test_support::store_op;
using fixpp::test_support::StoreCall;

}  // namespace

TEST(Drain103Teardown, G9_ContextDestroyedWithFramesSuspended_NoReportNoAssertion) {
    // The io_context and the Session are destroyed explicitly below, in that order; the
    // clock, the store log and the captures outlive both.
    std::shared_ptr<fixpp::test_support::StoreCallLog> log;
    std::unique_ptr<fixpp::session::Session> session;
    auto ioc = std::make_unique<asio::io_context>();
    std::vector<std::string> sent;
    std::vector<std::vector<std::byte>> fed;

    using namespace std::chrono;
    auto mock = std::make_shared<fixpp::core::mock_clock>(
        system_clock::time_point{} + seconds{1704067200}, fixpp::core::steady_time_point{},
        ioc->get_executor());
    fixpp::core::EngineConfig engine;
    engine.clock = mock;
    engine.executor = ioc->get_executor();
    fixpp::session::SessionConfig cfg;
    cfg.sender_comp_id = "ISLD";
    cfg.target_comp_id = "TW";
    cfg.begin_string = "FIX.4.2";
    cfg.heartbeat_interval = seconds{0};  // no liveness loop
    cfg.role = fixpp::session::session_role::initiator;
    cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    cfg.executor_override = ioc->get_executor();
    cfg.mode = fixpp::session::threading_mode::direct_executor;
    cfg.already_serialized_executor = true;
    auto const f = std::make_shared<fixpp::test_support::RecordingStoreFactory>(
        std::make_shared<fixpp::session::MemoryStoreFactory>(fixpp::session::MemoryStore::Config{
            .policy = fixpp::session::capacity_policy::unbounded}));
    log = f->log;
    cfg.store_factory = f;
    cfg.transport_send = [&sent](std::span<const std::byte> b) {
        sent.emplace_back(reinterpret_cast<const char*>(b.data()), b.size());
    };
    session = std::make_unique<fixpp::session::Session>(engine, cfg);

    auto pump = [&](auto pred) {
        return fixpp::test_support::pump_until(*ioc, pred, fixpp::test_support::kPumpBudget,
                                               fixpp::test_support::kPumpSlice, nullptr);
    };
    auto ready = [](auto const& fut) {
        return fut.wait_for(seconds{0}) == std::future_status::ready;
    };
    auto frame = [&](char const* type, std::uint32_t seq, std::string const& extra) {
        std::array<char, 32> buf{};
        auto const st = fixpp::core::utc_time_to_fix_string(
            mock->now(), fixpp::core::fix_time_precision::millis, std::span<char>{buf});
        return rg::Scene::raw(type, seq, "TW", st ? std::string{st->data(), st->size()} : "",
                              extra);
    };
    auto deliver = [&](std::vector<std::byte> bytes) {
        fed.push_back(std::move(bytes));
        return asio::co_spawn(fixpp::test_support::session_executor_of(*session, *ioc),
                              session->on_inbound_frame(fed.back()), asio::use_future);
    };

    auto open_fut = asio::co_spawn(fixpp::test_support::session_executor_of(*session, *ioc),
                                   session->open(), asio::use_future);
    ASSERT_TRUE(pump([&] { return ready(open_fut); })) << "setup: open()";
    auto logon_fut = deliver(frame("A", 1,
                                   "98=0\x01"
                                   "108=0\x01"));
    ASSERT_TRUE(pump([&] { return ready(logon_fut); })) << "setup: the Logon";
    ASSERT_EQ(session->state(), fsm_state::Active) << "setup";

    // The inbound chain: a TestRequest whose Heartbeat reply is held in its store call.
    log->arm();
    auto const reply_seq = session_test_access::seqnum_mgr(*session).peek_outbound();
    log->hold_in = [reply_seq](StoreCall const& c) {
        return c.op == store_op::store && c.dir == fixpp::session::direction_t::outbound &&
               c.first == reply_seq;
    };
    auto tr_fut = deliver(frame("1", 2, "112=G9\x01"));
    ASSERT_TRUE(pump([&] { return log->holding(); })) << "setup: the reply was not held";
    // The send: its store call returns late, so the send is suspended in it.
    log->late_return_in = [](StoreCall const& c) {
        return c.op == store_op::store && c.dir == fixpp::session::direction_t::outbound;
    };
    auto payload = std::make_shared<std::string>(
        "35=D\x01"
        "11=ORD-G9\x01" +
        std::string{rg::kSideAndTime});
    auto send_fut = asio::co_spawn(
        fixpp::test_support::session_executor_of(*session, *ioc),
        [s = session.get(), payload]() -> asio::awaitable<fixpp::core::expected_t<void>> {
            co_return co_await s->send(std::span<const std::byte>{
                reinterpret_cast<std::byte const*>(payload->data()), payload->size()});
        },
        asio::use_future);
    ASSERT_TRUE(pump([&] { return log->returning_late(); }))
        << "setup: the send's store call was not kept from returning";
    ASSERT_FALSE(ready(tr_fut) || ready(send_fut)) << "setup: a frame was not suspended";
    ASSERT_EQ(session_test_access::replay_gate(*session).timer_waits, 0U)
        << "setup: a contended gate wait was made";

    // The context is destroyed with both frames suspended, the Session alive; then the
    // Session. Each frame was destroyed unresumed, so its future reports a broken promise.
    ioc.reset();
    session.reset();
    EXPECT_THROW((void)tr_fut.get(), std::future_error) << "the inbound chain completed";
    EXPECT_THROW((void)send_fut.get(), std::future_error) << "the send completed";
}

// The bound's wake clearer (data-model "Pending-Logout mark") acts only while an exception unwinds
// the bound's frame. Here the io_context is destroyed with the bound suspended between its publish
// and its race, in its before-race seam's wait on a timer of that context, the Session alive: the
// frame is destroyed unresumed, and the clearer writes no Session state, so the wake the bound
// published is still published (research D4.4's reason for no exit guard: a frame destroyed so may
// outlive its Session). The frame's join hold is destroyed with it, so the counter is 0. The cell
// drops the wake without calling it before it destroys the Session.
TEST(Drain103Teardown, G9_ContextDestroyedWithTheBoundInItsRace_ClearerTouchesNoSessionState) {
    std::unique_ptr<fixpp::session::Session> session;
    auto ioc = std::make_unique<asio::io_context>();
    std::vector<std::string> sent;
    std::vector<std::vector<std::byte>> fed;

    using namespace std::chrono;
    auto mock = std::make_shared<fixpp::core::mock_clock>(
        system_clock::time_point{} + seconds{1704067200}, fixpp::core::steady_time_point{},
        ioc->get_executor());
    fixpp::core::EngineConfig engine;
    engine.clock = mock;
    engine.executor = ioc->get_executor();
    fixpp::session::SessionConfig cfg;
    cfg.sender_comp_id = "ISLD";
    cfg.target_comp_id = "TW";
    cfg.begin_string = "FIX.4.2";
    cfg.heartbeat_interval = seconds{0};  // no liveness loop
    cfg.role = fixpp::session::session_role::initiator;
    cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    cfg.executor_override = ioc->get_executor();
    cfg.mode = fixpp::session::threading_mode::direct_executor;
    cfg.already_serialized_executor = true;
    cfg.logout_disconnect_timeout_ms = 2000;
    cfg.transport_send = [&sent](std::span<const std::byte> b) {
        sent.emplace_back(reinterpret_cast<const char*>(b.data()), b.size());
    };
    session = std::make_unique<fixpp::session::Session>(engine, cfg);

    auto pump = [&](auto pred) {
        return fixpp::test_support::pump_until(*ioc, pred, fixpp::test_support::kPumpBudget,
                                               fixpp::test_support::kPumpSlice, nullptr);
    };
    auto ready = [](auto const& fut) {
        return fut.wait_for(seconds{0}) == std::future_status::ready;
    };
    auto frame = [&](char const* type, std::uint32_t seq, std::string const& extra) {
        std::array<char, 32> buf{};
        auto const st = fixpp::core::utc_time_to_fix_string(
            mock->now(), fixpp::core::fix_time_precision::millis, std::span<char>{buf});
        return rg::Scene::raw(type, seq, "TW", st ? std::string{st->data(), st->size()} : "",
                              extra);
    };
    auto deliver = [&](std::vector<std::byte> bytes) {
        fed.push_back(std::move(bytes));
        return asio::co_spawn(fixpp::test_support::session_executor_of(*session, *ioc),
                              session->on_inbound_frame(fed.back()), asio::use_future);
    };

    auto open_fut = asio::co_spawn(fixpp::test_support::session_executor_of(*session, *ioc),
                                   session->open(), asio::use_future);
    ASSERT_TRUE(pump([&] { return ready(open_fut); })) << "setup: open()";
    auto logon_fut = deliver(frame("A", 1,
                                   "98=0\x01"
                                   "108=0\x01"));
    ASSERT_TRUE(pump([&] { return ready(logon_fut); })) << "setup: the Logon";
    ASSERT_EQ(session->state(), fsm_state::Active) << "setup";

    // A too-high Logout: the mark is set and the bound parks in its before-race seam, on a
    // timer of the io_context that never expires, so only that context holds the frame (a
    // wait on the mock clock would be held by the clock, which the Session keeps alive).
    bool seam_entered = false;
    session_test_access::set_peer_logout_bound_hold_before_race(
        *session, [&seam_entered]() -> asio::awaitable<void> {
            seam_entered = true;
            asio::steady_timer never{co_await asio::this_coro::executor,
                                     asio::steady_timer::time_point::max()};
            (void)co_await never.async_wait(asio::as_tuple(asio::use_awaitable));
        });
    auto lo_fut = deliver(frame("5", 4, ""));
    ASSERT_TRUE(pump([&] { return ready(lo_fut) && seam_entered; }))
        << "setup: the bound did not reach its before-race seam";
    ASSERT_TRUE(session_test_access::peer_logout_wake_published(*session))
        << "setup: the bound did not publish its wake";
    ASSERT_EQ(session_test_access::peer_logout_phase(*session), 1) << "setup: no pending mark";
    ASSERT_EQ(session_test_access::peer_logout_bound_count(*session), 1) << "setup: no bound";

    ioc.reset();
    EXPECT_TRUE(session_test_access::peer_logout_wake_published(*session))
        << "the bound's frame, destroyed unresumed, cleared the Session's wake";
    EXPECT_EQ(session_test_access::peer_logout_bound_count(*session), 0)
        << "the destroyed frame did not release the join counter";
    session_test_access::drop_peer_logout_wake(*session);
    session.reset();
}
