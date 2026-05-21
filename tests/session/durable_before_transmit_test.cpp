// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/durable_before_transmit_test.cpp
//
// Seam #10 — Durable-before-transmit ordering (005-session-establishment-fsm
// T029 / US2 Phase 4).
//
// TDD red-first: authored BEFORE T034 (session.cpp durable-before-transmit
// wiring). Must FAIL until T034/T036 implement the inbound ordering.
//
// Invariant I-3 ([2e §root cause #1] / [2e §7.6]) has TWO halves:
//   (a) Inbound:  store(seq, frame, inbound) completes BEFORE fromAdmin/fromApp
//                 dispatch.
//   (b) Outbound: store(seq, committed_span, outbound) completes BEFORE
//                 transport::async_write; a cancelled transmit leaves NO
//                 persisted-but-unsent inconsistency.
//
// PHASE 4 SCOPE (this file):
//   Covers only the INBOUND half (a). Asserted by the single test
//   `InboundStoreBeforeFromAdminDispatch` — after on_inbound_frame() returns ok
//   for a valid Logon, the FSM has transitioned (i.e. the inbound dispatch
//   path ran), which under T034's wiring requires store(inbound) to have
//   completed first.
//
// DEFERRED TO PHASE 6 (T046 / seam #11 — cancellation_two_phase_test.cpp):
//   The OUTBOUND half (b). Verifying store(outbound) → transport::async_write
//   ordering AND the cancelled-transmit consistency requires the actual
//   Session::send() → transport binding (the Logout build/send path + the
//   real transport adaptor), which is US4-owned. Until that lands, the
//   outbound assertions would either be SUCCEED()-placeholders (a false-pass
//   shape — see project memory `feedback_tracking_pmr_resource_false_pass`)
//   or rely on a transport wired only by US4. Both options are explicitly
//   rejected here; the outbound assertion lives in T046/seam #11.
//
// tasks.md T029 row carries the matching partial note.
//
// Anchors: data-model.md I-3; [2e §root cause #1]; [2e §7.6]; spec FR-009.
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace fixpp::session::test {

// ── Fixture ───────────────────────────────────────────────────────────────────

class DurableBeforeTransmitTest : public ::testing::Test {
protected:
    asio::io_context                         ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig                engine{};

    void SetUp() override {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock    = clock;
        engine.executor = ioc.get_executor();
    }

    fixpp::core::expected_t<void> open_sync(Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        return fut.get();
    }
};

// ── I-3 inbound half: store(inbound) before fromAdmin/fromApp dispatch ────────
//
// [2e §7.6] N6: store(inbound) completes BEFORE fromAdmin/fromApp dispatch.
//
// Observable property at Phase 4 scope: when on_inbound_frame() processes a
// valid Logon, the FSM transitions to LogonReceived/Active. Under T034's
// wiring this transition (which represents the dispatch point) is ordered
// strictly AFTER the inbound store call and seqnum check; if store() returned
// an error or the seqnum check rejected, the FSM would transition to
// Disconnected instead and the dispatch path would not run.
//
// The test asserts: after a valid Logon is fed, the FSM has advanced past
// NotConnected — which under the implementation in session.cpp can only
// happen via the post-store/post-seqnum-check path.

TEST_F(DurableBeforeTransmitTest, InboundStoreBeforeFromAdminDispatch) {
    fixpp::session::SessionConfig cfg;
    cfg.sender_comp_id    = "ISLD";
    cfg.target_comp_id    = "TW";
    cfg.begin_string      = "FIX.4.2";
    cfg.heartbeat_interval = 30s;
    cfg.security_profile  = fixpp::test_support::make_minimal_security_profile();
    cfg.dictionary        = fixpp::test_support::make_minimal_dictionary();
    cfg.executor_override = ioc.get_executor();

    Session sess(engine, cfg);
    auto open_r = open_sync(sess);
    ASSERT_TRUE(open_r.has_value()) << "open() should succeed";

    // Build a minimal Logon frame (seq=1).
    std::string body = "35=A\x01" "34=1\x01" "49=TW\x01"
                       "52=20240101-00:00:00.000\x01" "56=ISLD\x01"
                       "98=0\x01" "108=30\x01";
    std::string hdr = "8=FIX.4.2\x01" "9=" + std::to_string(body.size()) + "\x01";
    std::string full = hdr + body;
    unsigned int cs = 0;
    for (unsigned char c : full) { cs += c; }
    cs &= 0xFFU;
    char csbuf[4];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> logon_frame;
    for (char c : full) { logon_frame.push_back(static_cast<std::byte>(c)); }

    // Feed the inbound Logon.
    auto fut = asio::co_spawn(
        ioc, sess.on_inbound_frame(logon_frame), asio::use_future);
    ioc.run_for(200ms);
    ioc.restart();
    auto r = fut.get();
    EXPECT_TRUE(r.has_value()) << "on_inbound_frame should succeed for valid Logon";

    // The FSM has transitioned — which under T034 means the store+seqnum
    // check completed first. (If either had failed, the FSM would be
    // Disconnected; if either had not yet run, the FSM would still be
    // NotConnected.)
    const auto st = sess.state();
    EXPECT_TRUE(st == fsm_state::LogonReceived || st == fsm_state::Active)
        << "Post-Logon dispatch must transition past NotConnected; got state="
        << static_cast<int>(st);
}

}  // namespace fixpp::session::test
