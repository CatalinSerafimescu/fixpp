// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_106_one_ending.cpp — 106-logon-guards-write-bounds (tasks.md T003, T075).
//
// The one-ending cells (research D11; contract C-4.6) that compile on the base: write_timeout_ms
// unset. A pair that needs the limit is written in tasks.md T105. Each asserts FR-017: one new
// Disconnected entry, onLogout at most once (once from Active), one close, and the reason records
// FR-017 names (one for the give-up or the establishment deadline; none for close()).

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_event.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <memory>
#include <optional>
#include <variant>

#include "session/resend_gap_scene.hpp"
#include "session/test_106_support.hpp"
#include "support/hooked_store.hpp"
#include "support/session_executor_of.hpp"

namespace {

using namespace std::chrono_literals;
using fixpp::session::fsm_state;
using fixpp::session::session_role;
namespace t106 = fixpp::test_support::t106;
namespace rgs = fixpp::test_support::resend_gap;

class OneEnding106 : public ::testing::TestWithParam<session_role> {};

// tasks.md T075: the give-up decided while the loop's TestRequest write is held, then close() at
// C = G + 100 ms, inside B = 5000 ms (logout_disconnect_timeout_ms), the mock clock stepped to
// G + B at once after close() was spawned. close() takes no claim: one Disconnected entry, onLogout
// once, the give-up's one slot-74 record, the transport closed, close() returned. Inequality: C
// precedes G + B by 4.9 s, so close() runs while the give-up's ending is writing; close()'s grace
// is a wall-clock timer of B, which the pump's budget exceeds.
TEST_P(OneEnding106, GiveUpThenClose) {
    t106::GiveUpParams p;
    p.logout_ms = 5000;
    t106::GiveUpRig r{GetParam(), p};
    ASSERT_TRUE(r.d.to_active()) << "setup";
    auto const t0 = r.now();
    r.d.hold_write_of("1");
    r.at(t106::saturating_add(t0, r.threshold()));
    ASSERT_TRUE(r.d.parked() && r.d.started("1") == 1U) << "setup: the TestRequest is not held";
    auto const disconnected_before = r.disconnected();
    auto const logouts_before = r.d.app->logouts;
    auto const g = t106::give_up_instant_emit_not_returned(r.last_inbound(), r.threshold());
    r.at(g);
    EXPECT_EQ(r.records(), 1U) << "no give-up record at G";
    r.at(g + 100ms);
    auto close_fut =
        asio::co_spawn(fixpp::test_support::session_executor_of(r.session(), r.d.s.ioc),
                       r.session().close(fixpp::session::close_mode::graceful), asio::use_future);
    r.at(g + std::chrono::milliseconds{r.p.logout_ms});
    EXPECT_TRUE(r.d.s.pump_until([&] { return rgs::Scene::ready(close_fut); }, 15s))
        << "close() did not return";
    r.d.s.drain();
    EXPECT_TRUE(r.d.closed()) << "the transport is open";
    EXPECT_EQ(r.d.s.state(), fsm_state::Disconnected);
    EXPECT_EQ(r.disconnected() - disconnected_before, 1U) << "new Disconnected entries";
    EXPECT_EQ(r.d.app->logouts - logouts_before, 1) << "onLogout calls";
    EXPECT_EQ(r.records(), 1U) << "give-up records";
}

// tasks.md T167 (research D11 row 3: an ending Logout other than R-1 takes the ending claim before
// the owner, and a refused claim writes nothing for a Logout fixpp starts; the reply to the peer's
// Logout is written whatever the claim's outcome, spec.md R21, claim-ok: a clarification id):
// Engine::stop()'s step 1 has run, so
// closing_or_stopped_() holds and claim_ending_() refuses every claim. A frame below NextNumIn then
// draws an ending Logout other than R-1: its leaf writes no Logout, and the caller's tail records
// the one Disconnected entry (send_ending_logout_: the Disconnected writes of its callers' tails
// are their own). On the base the leaf writes the too-low Logout. Its mutant: the leaf's claim test
// deleted (inbound_logout_leaf_).
TEST_P(OneEnding106, EngineStopStep1_ThenTooLow_NoLogout) {
    rgs::Scene s{rgs::Options{.role = GetParam()}};
    ASSERT_TRUE(s.open_to_active()) << "setup";
    ASSERT_GE(s.next_in(), 2U) << "setup: MsgSeqNum 1 is not below NextNumIn";
    auto const mark = s.sent.size();
    auto const disconnected_before = t106::count_disconnected(*s.session);
    fixpp::session::session_test_access::note_engine_stop(*s.session);
    (void)s.feed(s.heartbeat(1));
    s.drain();
    EXPECT_EQ(s.count("5", mark), 0U)
        << "a Logout after Engine::stop()'s step 1: " << s.written(mark);
    EXPECT_EQ(s.state(), fsm_state::Disconnected) << "the too-low arm's ending did not run";
    EXPECT_EQ(t106::count_disconnected(*s.session) - disconnected_before, 1U)
        << "new Disconnected entries";
}

// tasks.md T089 (research D11 N-12): the loop's give-up block meets a claim held elsewhere in the
// state that leaves it reachable: close() between setting closing and its Disconnected record,
// with the session still Active. close()'s phase 1 leaves Active once its Logout has been written
// (LogoutSent), so the cell holds that Logout's write. The TestRequest is written at L + T, so the
// loop sleeps to G = L + 2T + 1 ns (105's instant for a written TestRequest); close(graceful) is
// spawned at C = G - 100 ms and its Logout's write is held. At G the loop wakes into the block,
// whose claim is refused (closing_or_stopped_()): no slot-74 record, and the loop leaves there (its
// count drops to 0 at G, the one return the loop takes at G with the session Active and no ending
// phase entered). Inequalities: close()'s grace is a wall-clock timer of B = 2000 ms and the mock
// steps after close() are issued at once, so the grace has not run out at G; at its expiry close()
// closes the transport, which fails the held Logout. FR-017: one new Disconnected entry
// (close()'s), onLogout once, the transport closed, no reason record.
TEST_P(OneEnding106, CloseDuringGiveUpBlock_NoRecord) {
    t106::GiveUpParams p;
    p.logout_ms = 2000;
    t106::GiveUpRig r{GetParam(), p};
    ASSERT_TRUE(r.d.to_active()) << "setup";
    auto const l = r.last_inbound();
    r.at(t106::saturating_add(l, r.threshold()));
    ASSERT_EQ(r.completed_since("1", 0), 1U) << "setup: the TestRequest was not written at L + T";
    auto const g =
        t106::give_up_instant_written(l, r.threshold(), t106::saturating_add(l, r.threshold()));
    auto const c = g - 100ms;
    r.at(c);
    ASSERT_EQ(r.d.s.state(), fsm_state::Active) << "setup: the session ended before C";
    ASSERT_EQ(fixpp::session::session_test_access::liveness_count(r.session()), 1)
        << "setup: the liveness loop is not running at C";
    auto const disconnected_before = r.disconnected();
    auto const logouts_before = r.d.app->logouts;
    r.d.hold_write_of("5");
    auto close_fut =
        asio::co_spawn(fixpp::test_support::session_executor_of(r.session(), r.d.s.ioc),
                       r.session().close(fixpp::session::close_mode::graceful), asio::use_future);
    r.d.s.drain();
    ASSERT_TRUE(r.d.parked() && r.d.started("5") == 1U)
        << "setup: close()'s Logout is not held in its write";
    r.at(g - 1ns);
    ASSERT_EQ(r.d.s.state(), fsm_state::Active) << "setup: the session left Active before G";
    ASSERT_EQ(fixpp::session::session_test_access::liveness_count(r.session()), 1)
        << "setup: the liveness loop left before G";
    r.at(g);
    EXPECT_EQ(fixpp::session::session_test_access::liveness_count(r.session()), 0)
        << "the liveness loop did not leave at G: the give-up block was not reached";
    EXPECT_EQ(r.records(), 0U) << "the give-up block recorded with the claim held by close()";
    EXPECT_TRUE(r.d.s.pump_until([&] { return rgs::Scene::ready(close_fut); }, 15s))
        << "close() did not return";
    r.d.s.drain();
    EXPECT_TRUE(r.d.closed()) << "the transport is open";
    EXPECT_EQ(r.d.s.state(), fsm_state::Disconnected);
    EXPECT_EQ(r.disconnected() - disconnected_before, 1U) << "new Disconnected entries";
    EXPECT_EQ(r.d.app->logouts - logouts_before, 1) << "onLogout calls";
    EXPECT_EQ(r.records(), 0U) << "give-up records";
}

// spec.md R22 (claim-ok: a clarification id): the establishment deadline expires while a
// close(graceful) waits in LogoutSent, before the session's first Active. close() owns the ending
// and writes no reason record (research D11), so the deadline's event and log record are that
// ending's one reason record, and the pump's expiry closes the connection at once. The rig is
// Od29's (inbound_frame_dispositions_test.cpp): an acceptor's Logon held in its reset unit,
// close(graceful) spawned there, the unit released. Instants: the deadline is D = A +
// logon_timeout_ms, A the mock clock's steady_now() at accept, which nothing advances before D. At
// D - 1 ns no event, no record, and the close still waits; at D the event once, the record once,
// the call returned, the transport closed. The close-at-once budget is a quarter of close()'s
// wall-clock grace (logout_disconnect_timeout_ms), so the grace timer cannot complete the call
// inside it; the mock logout timeout (4000 ms) is never reached. FR-017: one new Disconnected
// entry, no onLogout (the session never reached Active). Its mutant: the pump's record gated on the
// claim alone (run_read_pump's expire).
TEST(OneEnding106Deadline, DuringGracefulClose_ClosesAtOnceAndRecordsOnce) {
    constexpr std::uint32_t kLogoutTimeoutMs = 4000;
    constexpr std::uint32_t kEstablishmentTimeoutMs = 1000;
    static_assert(kEstablishmentTimeoutMs < kLogoutTimeoutMs,
                  "the deadline must precede the mock-clock logout timeout");
    // The deadline's record, spelled out here, not taken from the production call site.
    static constexpr char kEstablishmentRecordFormat[] =
        "session not established within logon_timeout_ms={}";
    std::chrono::milliseconds const bound{kLogoutTimeoutMs / 4};
    auto const step = std::chrono::milliseconds{fixpp::test_support::kHoldBound} / 4;

    auto const factory = std::make_shared<fixpp::test_support::HookedStoreFactory>();
    auto const held = std::make_shared<bool>(false);
    auto const release = std::make_shared<bool>(false);
    factory->hooks.on_reset = [held] { *held = true; };
    factory->hooks.release_when = [release] { return *release; };
    auto const store_log = factory->log;

    t106::LogCapture log;
    auto const app = std::make_shared<t106::pr::CountingApp>();
    t106::pr::Rig rig{app};
    auto cfg = rig.cfg(session_role::acceptor);
    cfg.logout_disconnect_timeout_ms = kLogoutTimeoutMs;
    cfg.logon_timeout_ms = kEstablishmentTimeoutMs;
    cfg.store_factory = factory;
    cfg.logger_override = log.logger;
    auto const accepted_at = rig.clock->steady_now();
    auto const deadline = accepted_at + std::chrono::milliseconds{kEstablishmentTimeoutMs};

    bool const up = rig.start(std::move(cfg)) && rig.connect_peer();
    if (up) {
        rig.peer.send(rig.msg("A", 1,
                              "98=0\x01"
                              "108=30\x01"
                              "141=Y\x01"));
    }
    bool const holding = up && rig.run_until([&] { return *held; }, step);
    auto const s = holding ? rig.session() : nullptr;
    bool const clock_still = rig.clock->steady_now() == accepted_at;

    struct Call {
        bool done = false;
        std::optional<fixpp::core::expected_t<void>> result;
    };
    auto const graceful = std::make_shared<Call>();
    std::size_t disconnected_before = 0;
    auto close_called = std::chrono::steady_clock::now();
    bool logout_sent = false;
    if (s) {
        disconnected_before = t106::count_disconnected(*s);
        close_called = std::chrono::steady_clock::now();
        asio::co_spawn(
            s->executor().underlying(),
            [s, graceful]() -> asio::awaitable<void> {
                graceful->result = co_await s->close(fixpp::session::close_mode::graceful);
                graceful->done = true;
            },
            asio::detached);
        logout_sent = rig.run_until([&] { return s->state() == fsm_state::LogoutSent; }, step);
    }
    if (logout_sent) {
        *release = true;
        rig.settle();
    }
    auto const events_of_timeout = [&] {
        std::size_t n = 0;
        for (auto const& ev : s->recent_events()) {
            n += std::holds_alternative<fixpp::session::session_event_establishment_timeout>(ev)
                     ? 1U
                     : 0U;
        }
        return n;
    };
    auto const records_now = [&]() -> std::optional<std::size_t> {
        auto const r = log.flushed_records_of(FIXPP_FORMAT_ID(kEstablishmentRecordFormat));
        if (!r) return std::nullopt;
        return r->size();
    };

    // D - 1 ns: nothing expired.
    bool waiting_before = false;
    std::size_t events_before = 0;
    std::optional<std::size_t> records_before;
    if (logout_sent) {
        rig.clock->step_to(deadline - 1ns);
        rig.settle();
        waiting_before =
            !graceful->done && s->state() == fsm_state::LogoutSent && !rig.peer.read_ended;
        events_before = events_of_timeout();
        records_before = records_now();
    }

    // D: the pump's expiry.
    auto const left = [&] {
        auto const l = close_called + bound - std::chrono::steady_clock::now();
        return l > std::chrono::steady_clock::duration::zero()
                   ? l
                   : std::chrono::steady_clock::duration::zero();
    };
    if (logout_sent) rig.clock->step_to(deadline);
    bool const returned = logout_sent && rig.run_until([&] { return graceful->done; }, left());
    bool const closed = returned && rig.run_until([&] { return rig.peer.read_ended; }, left());
    auto const state_after = s ? s->state() : fsm_state::NotConnected;
    auto const disconnected_after = s ? t106::count_disconnected(*s) : 0U;
    auto const events_after = s ? events_of_timeout() : 0U;
    bool const hold_timed_out = store_log->hold_timed_out;
    if (!graceful->done && logout_sent) {
        rig.clock->advance(std::chrono::milliseconds{kLogoutTimeoutMs + 1U});
        (void)rig.run_until([&] { return graceful->done; });
    }
    rig.stop();
    auto const records_after = log.records_of(FIXPP_FORMAT_ID(kEstablishmentRecordFormat)).size();

    ASSERT_TRUE(holding && s && logout_sent && clock_still) << "setup";
    EXPECT_FALSE(hold_timed_out) << "the unit was released by the cell, not by its bound";
    EXPECT_TRUE(waiting_before) << "the graceful close still waits in LogoutSent at D - 1 ns";
    EXPECT_EQ(events_before, 0U) << "establishment-timeout events at D - 1 ns";
    EXPECT_EQ(records_before, std::optional<std::size_t>{0U})
        << "establishment-timeout records at D - 1 ns (nullopt: the flush did not complete)";
    EXPECT_TRUE(returned) << "the graceful close's call returns within the bound after D";
    EXPECT_TRUE(graceful->result && graceful->result->has_value()) << "the graceful close's result";
    EXPECT_TRUE(closed) << "the transport closes within the bound after D";
    EXPECT_EQ(state_after, fsm_state::Disconnected);
    EXPECT_EQ(disconnected_after - disconnected_before, 1U) << "new Disconnected entries";
    EXPECT_EQ(app->logouts, 0) << "onLogout calls (the session never reached Active)";
    EXPECT_EQ(events_after, 1U) << "establishment-timeout events at D";
    EXPECT_EQ(records_after, 1U) << "establishment-timeout records at D";
}

INSTANTIATE_TEST_SUITE_P(BothRoles, OneEnding106,
                         ::testing::Values(session_role::acceptor, session_role::initiator),
                         [](::testing::TestParamInfo<session_role> const& i) {
                             return i.param == session_role::acceptor ? "Acceptor" : "Initiator";
                         });

}  // namespace
