// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/conformance/tc_logout_test.cpp
//
// [FIX-TC] TC-009 Logout subset — 005-session-establishment-fsm T045 /
// Phase 6 / US4.
//
// Oracle: QuickFIX/J + QuickFIX-CPP server definitions @ fix42 / fix44.
// Scenarios (TC-009, research D-10):
//
//   13b_UnsolicitedLogoutMessage (fix42 + fix44):
//     "If a logout is received, send a logout"
//     Scenario:
//       I:CONNECT
//       I:Logon(35=A,34=1,…,108=30)
//       E:Logon(35=A,34=1,…,108=30)          ← server sends its Logon reply
//       I:Logout(35=5,34=2,…)               ← peer initiates Logout
//       E:Logout(35=5,34=2,…)               ← server echoes Logout
//       e:DISCONNECT
//
//   Note: "12_*" scenarios appear in D-10 research notes as a pattern but
//   the oracle directories (fix42/fix44) contain only 13b_UnsolicitedLogoutMessage.
//   The "12_*" reference in D-10 appears to be a forward-looking category label
//   for future scenarios; the only in-scope oracle file today is 13b.
//
//   GracefulLogoutTimeout (no oracle file, but spec-derived):
//     Active → server initiates Logout → peer never confirms → 2s clock-bound
//     force-disconnect → Disconnected (session_logout_timeout, slot 73).
//     [FIX-SL §4.6.2] / D-8 / FR-005.
//
// Anchors: research D-10; spec FR-005; data-model E2; error slot 73;
// [FIX-SL §4.6]. [const §VII.5]: ships only in-scope subset green.
#include <gtest/gtest.h>

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
#include "support/store_double.hpp"
#include "support/transport_double.hpp"

using namespace std::chrono_literals;

namespace fixpp::session::conformance_test {
namespace {

// ── Frame builder helpers ──────────────────────────────────────────────────────

static std::vector<std::byte> make_logon_frame(std::string_view begin_string, std::uint32_t seq,
                                               std::string_view sender, std::string_view target,
                                               int heartbt = 30) {
    std::string body;
    body += "35=A\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target) + "\x01";
    body += "98=0\x01";
    body += "108=" + std::to_string(heartbt) + "\x01";

    std::string hdr;
    hdr += "8=" + std::string(begin_string) + "\x01";
    hdr += "9=" + std::to_string(body.size()) + "\x01";

    std::string full = hdr + body;
    unsigned int cs = 0;
    for (unsigned char c : full) {
        cs += c;
    }
    cs &= 0xFFU;
    char csbuf[4];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    for (char c : full) {
        frame.push_back(static_cast<std::byte>(c));
    }
    return frame;
}

static std::vector<std::byte> make_logout_frame(std::string_view begin_string, std::uint32_t seq,
                                                std::string_view sender, std::string_view target) {
    std::string body;
    body += "35=5\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target) + "\x01";

    std::string hdr;
    hdr += "8=" + std::string(begin_string) + "\x01";
    hdr += "9=" + std::to_string(body.size()) + "\x01";

    std::string full = hdr + body;
    unsigned int cs = 0;
    for (unsigned char c : full) {
        cs += c;
    }
    cs &= 0xFFU;
    char csbuf[4];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    for (char c : full) {
        frame.push_back(static_cast<std::byte>(c));
    }
    return frame;
}

// Extract a field value from a SOH-delimited FIX frame.
static std::string extract_field(std::span<const std::byte> frame, std::uint32_t tag_wanted) {
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    std::string needle = std::to_string(tag_wanted) + "=";
    auto pos = wire.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    pos += needle.size();
    auto end = wire.find('\x01', pos);
    if (end == std::string::npos) {
        return {};
    }
    return wire.substr(pos, end - pos);
}

// ── Test fixture ──────────────────────────────────────────────────────────────

//
// ── #289: the `run_for(W); restart(); fut.get()` migration ───────────────────
//
// The sites in this file use `run_window_then_ready`, and the two graceful-close
// sites additionally use `pump_until` (both tests/support/pump_until_ready.hpp).
// Where a window is preserved that is deliberate: the hazard #289 names is the
// UNCONDITIONAL `get()`, not the fixed window. On a manually-driven io_context a
// `get()` the window did not satisfy blocks with nothing left to pump it -- a
// deadlock ctest reports as a timeout, and on a lane with no ctest timeout
// configured, as a wedged job.
//
// A window is NOT preservable where a later step depends on progress the window
// was only assumed to have made. Both graceful-close tests are that case -- see
// the arming argument at their `pump_until` -- so they wait on an observed
// condition instead. The fixture's own sites keep their windows.
//
// ⚠️ A CLOSE PUMP HERE MUST BE BOUNDED BELOW `logout_disconnect_timeout_ms`.
// `Session::close` joins `run_logout_phase1()` with a REAL
// `asio::steady_timer close_grace` armed for that same duration
// (src/session/session.cpp, the `||` in the graceful branch). The mock clock
// does not govern that timer. So a pump whose budget exceeds it will complete
// `close_fut` off the REAL timer whenever the mock path did not fire -- the
// test then goes green having never exercised the mock-clock timeout it exists
// to test. Measured, not reasoned: with the mock advance deleted entirely, a
// 10 s budget passes in 2202 ms instead of failing. Keeping the budget under
// the grace period makes that case report a miss instead.
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

struct SessionFixture {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;
    fixpp::session::test::TransportDouble transport;

    SessionFixture() {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    fixpp::session::SessionConfig make_cfg(std::string_view begin_string = "FIX.4.2") {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = std::string(begin_string);
        cfg.heartbeat_interval = 30s;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.transport_send = [this](std::span<const std::byte> frame) {
            transport.capture_outbound(frame);
        };
        // RC#C (gate-b/r1): bilateral_lenient — conformance tests don't exercise reset.
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        return cfg;
    }

    // Open session and drive to Active (initiator path: LogonSent → Active).
    void open_and_drive_to_active(fixpp::session::Session& sess,
                                  std::string_view begin_string = "FIX.4.2") {
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms)) {
            fixpp::test_support::cancel_and_drain_or_report(
                ioc, *clock, "SessionFixture::open_and_drive_to_active/open");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "SessionFixture::open_and_drive_to_active/open";
            return;
        }
        ASSERT_TRUE(fut.get().has_value()) << "open() failed";

        auto logon = make_logon_frame(begin_string, 1, "TW", "ISLD", 30);
        auto fut2 = asio::co_spawn(ioc, sess.on_inbound_frame(logon), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut2, 200ms)) {
            fixpp::test_support::cancel_and_drain_or_report(
                ioc, *clock, "SessionFixture::open_and_drive_to_active/logon-ack");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "SessionFixture::open_and_drive_to_active/logon-ack";
            return;
        }
        ASSERT_TRUE(fut2.get().has_value()) << "Logon-ack failed";
        ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active);
    }

    void feed(fixpp::session::Session& sess, std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(frame), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, "SessionFixture::feed");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "SessionFixture::feed";
            return;
        }
        auto r = fut.get();
        EXPECT_TRUE(r.has_value()) << "on_inbound_frame returned error";
    }
};

}  // namespace

// ── 13b_UnsolicitedLogoutMessage (fix42) ─────────────────────────────────────
//
// Oracle (QuickFIX/J fix42):
//   iCONNECT
//   I:Logon(35=A,34=1,49=TW,56=ISLD,98=0,108=30)
//   E:Logon(35=A,34=1,49=ISLD,56=TW,98=0,108=30)  ← server's reply
//   I:Logout(35=5,34=2,49=TW,56=ISLD)
//   E:Logout(35=5,34=2,49=ISLD,56=TW)              ← server echoes Logout
//   eDISCONNECT
//
// Observable in-process translation:
//   - Session opens and reaches Active (initiator Logon → peer ack).
//   - Peer sends Logout (inbound).
//   - Session emits a confirming Logout (35=5) on the transport.
//   - Session FSM → Disconnected.
TEST(TC009Logout, Fix42_13b_UnsolicitedLogoutMessage) {
    SessionFixture f;
    auto cfg = f.make_cfg("FIX.4.2");
    fixpp::session::Session sess(f.engine, cfg);
    f.open_and_drive_to_active(sess, "FIX.4.2");

    // Verify session Logon reply was emitted (E:Logon).
    // (The first outbound frame after open() + Logon-ack is the Logon reply.)
    // This depends on whether open() or on_inbound_frame emits the Logon.
    // In the current 005 design, the outbound Logon is emitted by open() on
    // the initiator path or by on_inbound_frame(Logon) on the acceptor path.
    // For the transport surface, we just verify the transport captured frames.
    // The key oracle assertion is the E:Logout step.

    // Oracle step: I:Logout (peer sends Logout, seq=2).
    auto peer_logout = make_logout_frame("FIX.4.2", 2, "TW", "ISLD");
    f.feed(sess, peer_logout);

    // Oracle step: E:Logout (server echoes confirming Logout).
    // The last outbound frame on the transport must be Logout(35=5).
    ASSERT_GE(f.transport.sent_count(), 1u)
        << "Server must emit a confirming Logout (E:Logout in oracle)";
    {
        const auto last = f.transport.sent(f.transport.sent_count() - 1);
        EXPECT_EQ(extract_field(last, 35), "5") << "Confirming Logout must have MsgType=5";
        // FR-013 / FR-002 / FR-003: tag 8 and tag 52 must be present on every outbound frame.
        EXPECT_EQ(extract_field(last, 8), "FIX.4.2")
            << "Confirming Logout must carry 8=FIX.4.2 (negotiated begin_string)";
        EXPECT_EQ(extract_field(last, 52), "20240101-00:00:00.000")
            << "Confirming Logout must carry 52=<mock_clock_now>";
    }

    // Oracle step: eDISCONNECT — session is Disconnected.
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Disconnected)
        << "After unsolicited Logout + echo, session must be Disconnected";
}

// ── 13b_UnsolicitedLogoutMessage (fix44) ─────────────────────────────────────
//
// Same as fix42 but with FIX.4.4 begin_string. The oracle scenario is identical.
TEST(TC009Logout, Fix44_13b_UnsolicitedLogoutMessage) {
    SessionFixture f;
    auto cfg = f.make_cfg("FIX.4.4");
    fixpp::session::Session sess(f.engine, cfg);
    f.open_and_drive_to_active(sess, "FIX.4.4");

    auto peer_logout = make_logout_frame("FIX.4.4", 2, "TW", "ISLD");
    f.feed(sess, peer_logout);

    ASSERT_GE(f.transport.sent_count(), 1u) << "Server must emit confirming Logout";
    {
        const auto last = f.transport.sent(f.transport.sent_count() - 1);
        EXPECT_EQ(extract_field(last, 35), "5") << "Confirming Logout must have MsgType=5";
        // FR-013 / FR-002 / FR-003: tag 8 and tag 52 on every outbound frame.
        EXPECT_EQ(extract_field(last, 8), "FIX.4.4")
            << "Confirming Logout must carry 8=FIX.4.4 (negotiated begin_string)";
        EXPECT_EQ(extract_field(last, 52), "20240101-00:00:00.000")
            << "Confirming Logout must carry 52=<mock_clock_now>";
    }
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Disconnected);
}

// ── Graceful logout timeout (spec-derived, D-8) ───────────────────────────────
//
// Server initiates Logout → peer never responds → 2 s clock-bound timeout
// → Disconnected. Surface: session_logout_timeout (slot 73).
// [FIX-SL §4.6.2] / D-8 / FR-005.
TEST(TC009Logout, GracefulLogoutTimeout) {
    SessionFixture f;
    auto cfg = f.make_cfg("FIX.4.2");
    fixpp::session::Session sess(f.engine, cfg);
    f.open_and_drive_to_active(sess, "FIX.4.2");
    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active);

    // Server initiates Logout (close graceful).
    auto close_fut =
        asio::co_spawn(f.ioc, sess.close(fixpp::session::close_mode::graceful), asio::use_future);

    // Phase 1 must reach LogoutSent BEFORE the `advance()` below, and a fixed
    // window cannot promise that. The dependency is not merely "the assertions
    // read stale state": `run_logout_phase1` computes its deadline as
    // `steady_now() + logout_disconnect_timeout_ms` at ARM time -- see the
    // `sleep_until` that follows the LogoutSent transition in
    // `Session::run_logout_phase1_` (src/session/session.cpp) -- and
    // `mock_clock::advance` wakes only the waiters that already exist when it
    // runs. A sleep armed AFTER `advance(3s)` therefore parks 2 s past the NEW
    // now, nothing remains to reach it, and the `get()` below blocks forever.
    // That is the wedge, not a slow assertion.
    //
    // LogoutSent is used as the arming proxy. It is not claimed to be an exact
    // one -- that would be a claim about where asio reschedules inside
    // `co_await sleep_until`, which nothing here re-runs. It does not need to be
    // exact: if the sleep is somehow NOT yet armed when LogoutSent is observed,
    // the advance is lost, the close guard below misses, and the test reports a
    // bounded failure. The proxy stands on that failure mode, not on asio
    // internals.
    if (!fixpp::test_support::pump_until(
            f.ioc, [&sess] { return sess.state() == fixpp::session::fsm_state::LogoutSent; })) {
        fixpp::test_support::cancel_and_drain_or_report(f.ioc, *f.clock,
                                                        "TC009Logout.GracefulLogoutTimeout/phase1");
        ADD_FAILURE() << fixpp::test_support::kPumpBudgetMiss
                      << "TC009Logout.GracefulLogoutTimeout/phase1";
        return;
    }

    EXPECT_GE(f.transport.sent_count(), 1u) << "Graceful close must emit Logout";
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::LogoutSent)
        << "After emitting Logout, FSM should be LogoutSent";

    // Peer never confirms: advance clock past 2 s timeout.
    f.clock->advance(std::chrono::seconds{3});
    // Bounded pump rather than a fixed window -- a fixed window is what wedged
    // this test on a starved `ctest --parallel` lane. The budget is DERIVED from
    // the config under test, not chosen: it must stay under the real
    // `close_grace` timer (see the warning above the fixture), and deriving it
    // means it cannot drift if that default moves.
    const auto close_budget = std::chrono::milliseconds{cfg.logout_disconnect_timeout_ms} / 2;
    if (!fixpp::test_support::pump_until_ready(f.ioc, close_fut, close_budget)) {
        fixpp::test_support::cancel_and_drain_or_report(f.ioc, *f.clock,
                                                        "TC009Logout.GracefulLogoutTimeout/close");
        ADD_FAILURE() << fixpp::test_support::kPumpBudgetMiss
                      << "TC009Logout.GracefulLogoutTimeout/close";
        return;
    }

    // Session must be force-disconnected.
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Disconnected)
        << "Logout timeout must force-disconnect → Disconnected";

    (void)close_fut.get();
}

// ── Graceful logout both directions (spec-derived) ───────────────────────────
//
// Server initiates Logout → emits E:Logout → peer sends I:Logout → Disconnected.
TEST(TC009Logout, GracefulLogoutBothDirections) {
    SessionFixture f;
    auto cfg = f.make_cfg("FIX.4.2");
    fixpp::session::Session sess(f.engine, cfg);
    f.open_and_drive_to_active(sess, "FIX.4.2");

    auto close_fut =
        asio::co_spawn(f.ioc, sess.close(fixpp::session::close_mode::graceful), asio::use_future);

    // Same arming dependency as GracefulLogoutTimeout above, minus the clock
    // advance: the assertions below read the phase-1 emission, so wait for
    // LogoutSent rather than for a fixed window.
    if (!fixpp::test_support::pump_until(
            f.ioc, [&sess] { return sess.state() == fixpp::session::fsm_state::LogoutSent; })) {
        fixpp::test_support::cancel_and_drain_or_report(
            f.ioc, *f.clock, "TC009Logout.GracefulLogoutBothDirections/phase1");
        ADD_FAILURE() << fixpp::test_support::kPumpBudgetMiss
                      << "TC009Logout.GracefulLogoutBothDirections/phase1";
        return;
    }

    ASSERT_GE(f.transport.sent_count(), 1u);
    {
        const auto last = f.transport.sent(f.transport.sent_count() - 1);
        EXPECT_EQ(extract_field(last, 35), "5");
        // FR-013 / FR-002 / FR-003: tag 8 and tag 52 on every outbound frame.
        EXPECT_EQ(extract_field(last, 8), "FIX.4.2")
            << "Graceful Logout initiation must carry 8=FIX.4.2";
        EXPECT_EQ(extract_field(last, 52), "20240101-00:00:00.000")
            << "Graceful Logout initiation must carry 52=<mock_clock_now>";
    }
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::LogoutSent);

    // Peer confirms with Logout.
    auto peer_logout = make_logout_frame("FIX.4.2", 2, "TW", "ISLD");
    f.feed(sess, peer_logout);

    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Disconnected);

    // The peer's confirming Logout wakes phase 1 via `cancel_sleeps()`, but
    // whether `close_fut` is READY when `feed`'s window returns is a scheduling
    // question, not a guarantee. Guard the `get()` so a lost wake FAILS instead
    // of wedging the binary -- and bound it under the real `close_grace` timer
    // for the same reason as the site above: past that point the timer, not the
    // peer's Logout, is what completes the close.
    const auto close_budget = std::chrono::milliseconds{cfg.logout_disconnect_timeout_ms} / 2;
    if (!fixpp::test_support::pump_until_ready(f.ioc, close_fut, close_budget)) {
        fixpp::test_support::cancel_and_drain_or_report(
            f.ioc, *f.clock, "TC009Logout.GracefulLogoutBothDirections/close");
        ADD_FAILURE() << fixpp::test_support::kPumpBudgetMiss
                      << "TC009Logout.GracefulLogoutBothDirections/close";
        return;
    }

    auto r = close_fut.get();
    EXPECT_TRUE(r.has_value());
}

}  // namespace fixpp::session::conformance_test
