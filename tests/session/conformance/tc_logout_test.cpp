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
// The sites in this file use `run_window_then_ready`
// (tests/support/pump_until_ready.hpp). The window is PRESERVED: the hazard
// #289 names is the UNCONDITIONAL `get()`, not the fixed window. On a
// manually-driven io_context a `get()` the window did not satisfy blocks with
// nothing left to pump it -- a deadlock ctest reports as a timeout.
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

    // Let phase 1 start (Logout emitted, LogoutSent).
    f.ioc.run_for(100ms);
    f.ioc.restart();

    EXPECT_GE(f.transport.sent_count(), 1u) << "Graceful close must emit Logout";
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::LogoutSent)
        << "After emitting Logout, FSM should be LogoutSent";

    // #289 NOT MIGRATED, and NOT because the census said so -- the census never saw
    // this site. `close_fut.get()` below sits SEVEN lines under this window and
    // `ci/pump-census.sh`'s lookahead is SIX, so this row was never in
    // `ci/expected-pump-sites.txt` and cannot leave it. It is the #289 shape in full:
    // a fixed window, no `clock->advance()` between it and the `get()`, and an
    // unconditional `get()` that blocks with nothing left to pump it if the window
    // does not complete the close. Pre-existing on `main`, untouched by the
    // conformance-directory migration, deferred to the next batch of the sequence
    // with its own RED arm. This file's pin being empty is a statement about the
    // CENSUS, not about the file.

    // Peer never confirms: advance clock past 2 s timeout.
    f.clock->advance(std::chrono::seconds{3});
    f.ioc.run_for(200ms);
    f.ioc.restart();

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

    // #289 NOT MIGRATED -- same class as the site above, and invisible to the census
    // for the same reason (`close_fut.get()` is twenty-one lines below). WEAKER than
    // that one, deliberately stated as such: the intervening `f.feed(...)` IS
    // migrated, and on its miss branch `drain_or_report` pumps for up to
    // `kQuiesceBudget` (5 s), which will usually complete `close_fut` before the
    // `get()` is reached. The hazard is attenuated, not removed -- nothing checks
    // `close_fut`'s readiness. Deferred with the site above.
    f.ioc.run_for(100ms);
    f.ioc.restart();

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

    auto r = close_fut.get();
    EXPECT_TRUE(r.has_value());
}

}  // namespace fixpp::session::conformance_test
