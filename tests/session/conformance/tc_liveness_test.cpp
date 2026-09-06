// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/conformance/tc_liveness_test.cpp
//
// [FIX-TC] Liveness subset — TC-004 (partial, T038/T042 US4 retirement).
//
// Oracle: QuickFIX-CPP server definitions @ fix42.
//
// In-scope scenarios (retired with US4 transport wiring / T042 Phase 6):
//
//   4b_ReceivedTestRequest (fix42):
//     "If a test request is received, a matching heartbeat should be sent"
//     Scenario:
//       I:CONNECT
//       I:Logon(35=A,34=1,108=30)
//       E:Logon(35=A,34=1,108=30)
//       I:TestRequest(35=1,34=2,112=HELLO)
//       E:Heartbeat(35=0,34=2,112=HELLO)   ← echo with TestReqID
//       I:Logout(35=5,34=3)
//       E:Logout(35=5,34=3)
//       e:DISCONNECT
//
//   Note: The 4b oracle requires asserting engine-emitted E=Heartbeat(112=HELLO)
//   which was not honest until Session owns a transport member. Now that
//   transport_send_ is wired (US4/T046), the 4b scenario is fully assertable.
//
// Deferred scenario (requires outbound-idle tracking not yet implemented):
//
//   4a_NoDataSentDuringHeartBtInt (fix42):
//     "We should expect heartbeats if we wait around" — tests that the engine
//     emits a Heartbeat(35=0) when no OUTBOUND data has been sent for HeartBtInt.
//     This requires tracking last_outbound_steady_ and firing the outbound-idle
//     Heartbeat path. The current run_liveness_loop tracks INBOUND silence only
//     (TestRequest path). Outbound-idle Heartbeat emission is deferred to a
//     follow-up phase per the Phase 6 scope contract.
//
// Anchors: research D-10; spec FR-006 (HeartBtInt); data-model.md Active row;
// [FIX-SL §4.5]; tasks.md T038/T042. [const §VII.5]: ships only in-scope
// subset green; 4a deferred with traceability.
#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"
#include "support/transport_double.hpp"

using namespace std::chrono_literals;

namespace fixpp::session::test {

namespace {

static std::vector<std::byte> make_raw_frame(std::string_view begin_string,
                                             std::string_view msg_type, std::uint32_t seq,
                                             std::string_view sender, std::string_view target,
                                             std::string_view extra_fields = {}) {
    std::string body;
    body += "35=" + std::string(msg_type) + "\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target) + "\x01";
    if (!extra_fields.empty()) {
        body += std::string(extra_fields);
    }

    std::string hdr;
    hdr += "8=" + std::string(begin_string) + "\x01";
    hdr += "9=" + std::to_string(body.size()) + "\x01";

    std::string full = hdr + body;
    unsigned int cs = 0;
    for (unsigned char c : full) {
        cs += c;
    }
    cs &= 0xFFu;
    char csbuf[8];
    std::snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> result;
    for (char c : full) {
        result.push_back(static_cast<std::byte>(c));
    }
    return result;
}

static std::string extract_field(std::span<const std::byte> frame, std::uint32_t tag) {
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    std::string needle = std::to_string(tag) + "=";
    auto pos = wire.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    pos += needle.size();
    auto end = wire.find('\x01', pos);
    if (end == std::string::npos) {
        return wire.substr(pos);
    }
    return wire.substr(pos, end - pos);
}

}  // namespace

// ── Test fixture ──────────────────────────────────────────────────────────────

//
// ── #289: the `run_for(W); restart(); fut.get()` migration ───────────────────
//
// Most sites in this file use `run_window_then_ready`
// (tests/support/pump_until_ready.hpp). The window is PRESERVED: the hazard
// #289 names is the UNCONDITIONAL `get()`, not the fixed window. On a
// manually-driven io_context a `get()` the window did not satisfy blocks with
// nothing left to pump it -- a deadlock ctest reports as a timeout.
//
// ⚠️ `do_close` IS THE EXCEPTION, and its own comment says why. A fixed window is
// safe where the thing being awaited is driven by QUEUED WORK the window can run.
// It is NOT safe where the window's job is to get a coroutine to a MOCK-CLOCK
// sleep before the test advances that clock: too short a window loses the advance
// outright, and no later pump recovers it. That site is bounded by a CONDITION
// (`pump_until` on `LogoutSent`) instead. `drive_to_active`'s two sites are not
// clock-staged and keep their windows.
//
// Teardown is deliberately NOT a fixture-destructor drain, which is the shape
// PRs #301 and #307 used for fixtures that OWN their Session. Here every
// `Session` is a block-local declared AFTER the fixture, so it dies BEFORE a
// fixture destructor body could run -- and a drain is what RESUMES a suspended
// frame, so a destructor drain would resume it over the destroyed Session. The
// drain runs on the MISS branch instead, in the scope that still owns that
// storage, and it CANCELS THE MOCK CLOCK'S SLEEPS first: the first transition
// to Active co_spawns a detached `run_liveness_loop()` that parks on
// `sleep_until`, holding a work guard that `drain_or_report` cannot release
// (only a Clock can). `cancel_sleeps()` releases the waiters that exist WHEN IT
// RUNS and nothing more, so a miss whose drain itself performs the Active
// transition registers a NEW waiter afterwards. That WAS a documented limitation of
// the primitive, carried unchanged from PR #313; it is now FIXED. These sites call
// `cancel_and_drain_or_report` (`pump_until_ready.hpp`), which alternates the cancel
// with the drain and releases exactly that waiter.

class TC004Liveness : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;

    void SetUp() override {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    SessionConfig make_cfg(int heartbt = 30) {
        SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = std::chrono::seconds{heartbt};
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        // RC#C (gate-b/r1): bilateral_lenient — liveness tests don't exercise reset.
        cfg.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
        return cfg;
    }

    // Drive session to Active state (open + peer Logon-ack).
    void drive_to_active(Session& sess, TransportDouble& td) {
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "TC004Liveness::drive_to_active/open");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "TC004Liveness::drive_to_active/open";
            return;
        }
        ASSERT_TRUE(fut.get().has_value()) << "open() failed";
        ASSERT_EQ(sess.state(), fsm_state::LogonSent);

        // Peer sends Logon ack (seq=1).
        auto logon = make_raw_frame("FIX.4.2", "A", 1, "TW", "ISLD",
                                    "98=0\001"
                                    "108=30\001");
        auto fut2 = asio::co_spawn(ioc, sess.on_inbound_frame(logon), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut2, 200ms)) {
            fixpp::test_support::cancel_and_drain_or_report(
                ioc, *clock, "TC004Liveness::drive_to_active/logon-ack");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "TC004Liveness::drive_to_active/logon-ack";
            return;
        }
        ASSERT_TRUE(fut2.get().has_value()) << "Logon-ack failed";
        ASSERT_EQ(sess.state(), fsm_state::Active);

        // The session emits its own Logon (seq=1) during open().
        // Confirm at least one outbound frame was emitted.
        (void)td;
    }

    // Clean-up close: stage the Logout, advance the mock clock past the timeout, drain.
    //
    // ⚠️ THE STAGING WINDOW IS A CONDITION, NOT A DURATION, AND THAT IS THE WHOLE FIX.
    // `close(graceful)` must still be PENDING when staging returns -- the Logout timeout
    // it parks on only elapses at the `clock->advance` below -- so this is a staging
    // window and not a completion window. An earlier revision drew the wrong conclusion
    // from that correct premise and left the window BLIND (`ioc.run_for(50ms)`), on the
    // reasoning that migrating it would report a miss on the one outcome the helper
    // requires. Migrating it to an OBSERVABLE staging condition does not: `LogoutSent`
    // is reached when the coroutine has emitted Logout and parked, which is exactly
    // "staged but not complete".
    //
    // Blind is what broke it. If the coroutine has not reached its mock-clock sleep when
    // the window returns, `clock->advance()` lands on a timer that is not yet armed, the
    // advance is LOST, and no later pump can complete the close -- it is unrecoverable,
    // not slow. On a starved `ctest --parallel` lane 50 ms of wall clock buys nothing,
    // and the failure is byte-identical to starving the window to `0ms`, which is how it
    // was reproduced. Same shape, same cause and same fix as
    // `tc_logout_test.cpp`'s `GracefulLogoutTimeout/phase1` (#337).
    void do_close(Session& sess, const SessionConfig& cfg) {
        auto close_fut = asio::co_spawn(ioc, sess.close(close_mode::graceful), asio::use_future);

        if (!fixpp::test_support::pump_until(
                ioc, [&sess] { return sess.state() == fsm_state::LogoutSent; },
                fixpp::test_support::kPumpBudget, fixpp::test_support::kPumpSlice,
                "TC004Liveness::do_close/stage")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "TC004Liveness::do_close/stage");
            ADD_FAILURE() << fixpp::test_support::kPumpBudgetMiss
                          << "TC004Liveness::do_close/stage";
            return;
        }

        clock->advance(std::chrono::seconds{3});

        // ⚠️ BOUNDED BELOW `logout_disconnect_timeout_ms`, AND DERIVED RATHER THAN CHOSEN.
        // `Session::close` joins phase 1 with a REAL `asio::steady_timer close_grace`
        // armed for that same duration, which the mock clock does not govern. A budget
        // above it completes `close_fut` off the REAL timer whenever the mock path did
        // not fire, and the test goes green having never exercised the mock-clock
        // timeout it exists to test. Deriving it means it cannot drift if the default
        // moves. The arm for this is NOT a forced miss -- it is deleting the
        // `clock->advance()` above and asserting RED.
        const auto close_budget = std::chrono::milliseconds{cfg.logout_disconnect_timeout_ms} / 2;
        if (!fixpp::test_support::pump_until_ready(ioc, close_fut, close_budget)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, "TC004Liveness::do_close");
            ADD_FAILURE() << fixpp::test_support::kPumpBudgetMiss << "TC004Liveness::do_close";
            return;
        }
        (void)close_fut.get();
    }
};

// ── TC-004 4b: ReceivedTestRequest ────────────────────────────────────────────
//
// Oracle: quickfix-cpp/test/definitions/server/fix42/4b_ReceivedTestRequest.def
//
// Scenario: Active → inbound TestRequest(35=1,112=HELLO) → engine emits
// Heartbeat(35=0,112=HELLO) echoing the peer's TestReqID(112).
//
// This is the primary TC-004 scenario that ships with US4 (transport path now
// available). The engine must emit a Heartbeat with the SAME 112= value as the
// inbound TestRequest. The Heartbeat must be present in the transport double's
// outbound frame list (via transport_send_).
TEST_F(TC004Liveness, Fix42_4b_ReceivedTestRequest) {
    auto cfg = make_cfg(30);
    TransportDouble td;
    cfg.transport_send = [&td](std::span<const std::byte> frame) { td.capture_outbound(frame); };

    Session sess(engine, cfg);
    drive_to_active(sess, td);
    ASSERT_EQ(sess.state(), fsm_state::Active);

    // Record current sent count (includes Logon frame emitted at open).
    const std::size_t before = td.sent_count();

    // Oracle step: send inbound TestRequest(35=1, 34=2, 112=HELLO).
    auto tr_frame = make_raw_frame("FIX.4.2", "1", 2, "TW", "ISLD", "112=HELLO\x01");
    auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(tr_frame), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms)) {
        fixpp::test_support::cancel_and_drain_or_report(
            ioc, *clock, "Fix42_4b_ReceivedTestRequest/test-request");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss
                      << "Fix42_4b_ReceivedTestRequest/test-request";
        return;
    }
    ASSERT_TRUE(fut.get().has_value()) << "on_inbound_frame(TestRequest) failed";

    // Session must remain Active.
    EXPECT_EQ(sess.state(), fsm_state::Active) << "Inbound TestRequest must not change FSM state";

    // Engine must have emitted a Heartbeat (35=0) in response.
    ASSERT_GT(td.sent_count(), before)
        << "Engine must emit a Heartbeat reply to inbound TestRequest";

    // Find the Heartbeat in the newly-emitted frames.
    bool found_hb = false;
    std::string hb_test_req_id;
    for (std::size_t i = before; i < td.sent_count(); ++i) {
        if (extract_field(td.sent(i), 35) == "0") {  // 35=0 = Heartbeat
            found_hb = true;
            hb_test_req_id = extract_field(td.sent(i), 112);
            // FR-013 / FR-002 / FR-003: tag 8 and tag 52 must be on every outbound frame.
            EXPECT_EQ(extract_field(td.sent(i), 8), "FIX.4.2")
                << "Heartbeat reply must carry 8=FIX.4.2 (negotiated begin_string)";
            EXPECT_EQ(extract_field(td.sent(i), 52), "20240101-00:00:00.000")
                << "Heartbeat reply must carry 52=<mock_clock_now>";
            break;
        }
    }
    EXPECT_TRUE(found_hb) << "Engine must emit Heartbeat(35=0) in reply to TestRequest";
    EXPECT_EQ(hb_test_req_id, "HELLO") << "Heartbeat must echo the inbound TestReqID(112=HELLO)";

    do_close(sess, cfg);
}

// ── TC-004 4b variant: TestReqID empty / different value ──────────────────────
//
// Verify that the TestReqID echo is faithful regardless of the value.
// Oracle basis: same 4b scenario, different TestReqID content.
TEST_F(TC004Liveness, Fix42_4b_ReceivedTestRequest_EchoIsExact) {
    auto cfg = make_cfg(30);
    TransportDouble td;
    cfg.transport_send = [&td](std::span<const std::byte> frame) { td.capture_outbound(frame); };

    Session sess(engine, cfg);
    drive_to_active(sess, td);
    ASSERT_EQ(sess.state(), fsm_state::Active);

    const std::size_t before = td.sent_count();

    // Send TestRequest with a non-trivial TestReqID.
    auto tr_frame = make_raw_frame("FIX.4.2", "1", 2, "TW", "ISLD", "112=LIVENESS_PROBE_42\x01");
    auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(tr_frame), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms)) {
        fixpp::test_support::cancel_and_drain_or_report(
            ioc, *clock, "Fix42_4b_ReceivedTestRequest_EchoIsExact/test-request");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss
                      << "Fix42_4b_ReceivedTestRequest_EchoIsExact/test-request";
        return;
    }
    ASSERT_TRUE(fut.get().has_value());
    EXPECT_EQ(sess.state(), fsm_state::Active);

    // Find and check the echoed Heartbeat.
    std::string echoed_id;
    for (std::size_t i = before; i < td.sent_count(); ++i) {
        if (extract_field(td.sent(i), 35) == "0") {
            echoed_id = extract_field(td.sent(i), 112);
            // FR-013 / FR-002 / FR-003: tag 8 and tag 52 on every outbound frame.
            EXPECT_EQ(extract_field(td.sent(i), 8), "FIX.4.2")
                << "Heartbeat reply must carry 8=FIX.4.2";
            EXPECT_EQ(extract_field(td.sent(i), 52), "20240101-00:00:00.000")
                << "Heartbeat reply must carry 52=<mock_clock_now>";
            break;
        }
    }
    EXPECT_EQ(echoed_id, "LIVENESS_PROBE_42")
        << "Heartbeat must echo the exact TestReqID from the inbound TestRequest";

    do_close(sess, cfg);
}

}  // namespace fixpp::session::test
