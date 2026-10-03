// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/coverage_adversarial_test.cpp
//
// 005 Phase 8 /speckit-verify Step 4 coverage uplift — adversarial-input tests
// targeting defensive code paths in admin_messages.cpp + session.cpp that are
// reachable from FFI callers and rogue counterparties but not exercised by
// the seam-test corpus (which uses well-formed inputs by construction).
//
// Covers:
//   A. Builder buffer-too-small: build_logon / build_logout / build_heartbeat /
//      build_test_request / build_reject with undersized `out` spans.
//   B. Malformed fields: interpret_logon's skip of a non-digit tag char and a
//      tag without '=' (B-1); and, in Session::on_inbound_frame, the same two
//      shapes as field 3 of a frame arriving in Active, which the header scan
//      records as a fault (092 data-model E-1) and contract C-2 row D-8
//      disregards (B-2).
//   C. Logon-ack with msg_seq_num=0 → Disconnected (Guard (4)'s LogonSent-row seq==0 check).
//   D. cancel_sleeps() mid-Logout-graceful-sleep → system_error catch
//      absorbing the operation_aborted exception (run_logout_phase1's wake-early catch).
#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/admin_messages.hpp>
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

// ── #289: bounded pumps ──────────────────────────────────────────────────────
//
// Where a site in this file is migrated it uses `run_window_then_ready` plus a
// miss-branch drain (tests/support/pump_until_ready.hpp). The window is PRESERVED:
// the hazard #289 names is the UNCONDITIONAL `get()`, not the fixed window.
//
// The site label passed to `run_window_then_ready` is the FORCING SEAM: exporting
// FIXPP_FORCE_WINDOW_MISS=<label> makes exactly that site take its miss branch, with
// no source edit and no rebuild. It is a WEAKER witness than textual mutation and
// does not replace it -- see the primitive.
//
// Rationale and the teardown-shape rule live at the primitive, not duplicated here
// (#324).

using namespace std::chrono_literals;

namespace fixpp::session::test {

namespace {

std::vector<std::byte> bytes_of(std::string_view s) {
    std::vector<std::byte> v;
    v.reserve(s.size());
    for (char c : s) {
        v.push_back(static_cast<std::byte>(c));
    }
    return v;
}

// Build a complete FIX frame with valid BodyLength(9) + CheckSum(10) wrapping the
// caller-supplied body. The caller controls the body's exact bytes (used here to
// inject malformed fields the well-formed seam tests can't produce).
std::vector<std::byte> wrap_frame(std::string_view body) {
    std::string full = "8=FIX.4.2\x01";
    full += "9=" + std::to_string(body.size()) + "\x01";
    full += body;
    unsigned int cs = 0;
    for (unsigned char c : full) {
        cs += c;
    }
    cs &= 0xFFU;
    std::array<char, 8> csbuf{};
    std::snprintf(csbuf.data(), csbuf.size(), "%03u", cs);
    full += "10=" + std::string(csbuf.data()) + "\x01";
    return bytes_of(full);
}

}  // namespace

// ── Category A: builder buffer-too-small ─────────────────────────────────────
// Each builder's first append_raw fails with insufficient buffer space; the
// `if (auto r = w.append_raw(...); !r) return std::unexpected(r.error());`
// branch fires.

TEST(AdversarialBuilderBufferTooSmall, BuildLogonZeroBuf) {
    std::array<std::byte, 0> buf{};
    auto r = build_logon(std::span<std::byte>{buf}, 1, "TW", "ISLD", "FIX.4.2", 30,
                         "20240101-00:00:00.000");
    EXPECT_FALSE(r.has_value());
}

TEST(AdversarialBuilderBufferTooSmall, BuildLogoutZeroBuf) {
    std::array<std::byte, 0> buf{};
    auto r = build_logout(std::span<std::byte>{buf}, 1, "TW", "ISLD", "", "FIX.4.2",
                          "20240101-00:00:00.000");
    EXPECT_FALSE(r.has_value());
}

TEST(AdversarialBuilderBufferTooSmall, BuildHeartbeatZeroBuf) {
    std::array<std::byte, 0> buf{};
    auto r = build_heartbeat(std::span<std::byte>{buf}, 1, "TW", "ISLD", "", "FIX.4.2",
                             "20240101-00:00:00.000");
    EXPECT_FALSE(r.has_value());
}

TEST(AdversarialBuilderBufferTooSmall, BuildTestRequestZeroBuf) {
    std::array<std::byte, 0> buf{};
    auto r = build_test_request(std::span<std::byte>{buf}, 1, "TW", "ISLD", "TR1", "FIX.4.2",
                                "20240101-00:00:00.000");
    EXPECT_FALSE(r.has_value());
}

TEST(AdversarialBuilderBufferTooSmall, BuildRejectZeroBuf) {
    std::array<std::byte, 0> buf{};
    auto r = build_reject(std::span<std::byte>{buf}, 1, "TW", "ISLD", 2, 35, "0", 5, "FIX.4.2",
                          "20240101-00:00:00.000");
    EXPECT_FALSE(r.has_value());
}

// Mid-failure path: ~30-byte buffer holds the header prefix but not enough for
// the full body. Exercises a different append_raw failure site than size-0.
TEST(AdversarialBuilderBufferTooSmall, BuildLogonMidFailure) {
    std::array<std::byte, 30> buf{};
    auto r = build_logon(std::span<std::byte>{buf}, 1, "TW", "ISLD", "FIX.4.2", 30,
                         "20240101-00:00:00.000");
    EXPECT_FALSE(r.has_value());
}

// ── Category B-1: interpret_logon SOH-scanner skip-malformed-field ───────────

TEST(AdversarialInterpretLogon, NonDigitTagCharIsSkipped) {
    // Embed a garbage field whose tag starts with a non-digit byte ('X')
    // between BeginString and the real Logon fields. The parser's SOH scanner
    // should walk past it via the skip-malformed-field branch.
    std::string body =
        "X9=garbage\x01"
        "35=A\x01"
        "34=1\x01"
        "49=TW\x01"
        "52=20240101-00:00:00.000\x01"
        "56=ISLD\x01"
        "98=0\x01"
        "108=30\x01";
    auto frame = wrap_frame(body);
    // Outcome may be ok or error depending on validation; coverage is the goal.
    (void)interpret_logon(std::span<const std::byte>{frame}, "TW", "ISLD", "FIX.4.2");
}

TEST(AdversarialInterpretLogon, TagWithoutEqualsIsSkipped) {
    std::string body =
        "999\x01"  // tag with no '='
        "35=A\x01"
        "34=1\x01"
        "49=TW\x01"
        "52=20240101-00:00:00.000\x01"
        "56=ISLD\x01"
        "98=0\x01"
        "108=30\x01";
    auto frame = wrap_frame(body);
    (void)interpret_logon(std::span<const std::byte>{frame}, "TW", "ISLD", "FIX.4.2");
}

// ── Fixture for Session-level adversarial inputs (Categories B-2 + C + D) ─────

class AdversarialSessionTest : public ::testing::Test {
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

    SessionConfig make_cfg() {
        SessionConfig cfg;
        cfg.sender_comp_id = "TW";
        cfg.target_comp_id = "ISLD";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = std::chrono::seconds{30};
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        // RC#C (gate-b/r1): bilateral_lenient — tests here don't exercise reset semantics.
        cfg.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
        return cfg;
    }

    fixpp::core::expected_t<void> open_session(Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms,
                                                        "AdversarialSessionTest::open_session")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "AdversarialSessionTest::open_session");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "AdversarialSessionTest::open_session";
            return std::unexpected(fixpp::test_support::kWindowMissSentinel);
        }
        return fut.get();
    }

    fixpp::core::expected_t<void> feed(Session& s, std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(frame), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms,
                                                        "AdversarialSessionTest::feed")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "AdversarialSessionTest::feed");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "AdversarialSessionTest::feed";
            return std::unexpected(fixpp::test_support::kWindowMissSentinel);
        }
        return fut.get();
    }

    // Category B-2: reaches Active with the Category C Logon-ack, feeds `faulty_body`
    // (whose field 3 is the fault and which carries MsgSeqNum 2 after it), then a
    // conformant Heartbeat at MsgSeqNum 2. Contract C-2 row D-8: the faulty frame
    // draws nothing and leaves the session Active with NextNumIn unmoved, so the
    // Heartbeat is in sequence and draws nothing either.
    void expect_field3_fault_disregarded(std::string const& faulty_body) {
        auto cfg = make_cfg();
        TransportDouble td;
        cfg.transport_send = [&td](std::span<const std::byte> frame) {
            td.capture_outbound(frame);
        };
        Session s(engine, cfg);
        ASSERT_TRUE(open_session(s).has_value()) << "setup: open()";
        std::string const ack =
            "35=A\x01"
            "34=1\x01"
            "49=ISLD\x01"
            "52=20240101-00:00:00.000\x01"
            "56=TW\x01"
            "98=0\x01"
            "108=30\x01";
        ASSERT_TRUE(feed(s, wrap_frame(ack)).has_value()) << "setup: the Logon-ack";
        ASSERT_EQ(s.state(), fsm_state::Active) << "setup: the Logon-ack must reach Active";
        td.reset();

        (void)feed(s, wrap_frame(faulty_body));
        EXPECT_EQ(s.state(), fsm_state::Active) << "D-8: the faulty frame must not disconnect";
        EXPECT_TRUE(td.sent_frames().empty())
            << "D-8: the faulty frame must draw nothing (no Reject); sent:"
            << printable_all(td.sent_frames());
        EXPECT_EQ(s.garbled_frame_count(), 1U)
            << "093 C-2 step 1: a frame whose field 3 is not 35 is one garbled frame";

        td.reset();
        std::string const heartbeat =
            "35=0\x01"
            "34=2\x01"
            "49=ISLD\x01"
            "52=20240101-00:00:00.000\x01"
            "56=TW\x01";
        (void)feed(s, wrap_frame(heartbeat));
        EXPECT_EQ(s.state(), fsm_state::Active)
            << "D-8: NextNumIn must still be 2, so the Heartbeat at 2 is in sequence";
        EXPECT_TRUE(td.sent_frames().empty())
            << "D-8: the in-sequence Heartbeat must draw nothing; sent:"
            << printable_all(td.sent_frames());
    }

private:
    static std::string printable_all(std::vector<std::vector<std::byte>> const& frames) {
        std::string out;
        for (auto const& f : frames) {
            out += "\n  ";
            for (std::byte const b : f) {
                char const c = static_cast<char>(b);
                out += (c == '\x01') ? '|' : c;
            }
        }
        return out;
    }
};

// ── Category B-2: a malformed field 3 in Active is disregarded (092 D-8) ─────
// specs/092-garbled-frame-reject contract C-2 row D-8. 093-inbound-frame-dispositions
// (contract C-2 step 1) now takes this shape before the fault branch: the outcome is
// the same disregard, and the frame is also counted as one garbled frame. Each cell
// reaches Active first, so the frame meets the Active arm and not the LogonSent
// arm. The peer's CompIDs are the session's
// counterparty's (49=ISLD, 56=TW), so no identity check is what fails.

TEST_F(AdversarialSessionTest, InboundFrameMalformedTagInField3IsDisregarded) {
    expect_field3_fault_disregarded(
        "X35=garbage\x01"  // field 3: a non-digit tag byte
        "35=0\x01"         // Heartbeat
        "34=2\x01"
        "49=ISLD\x01"
        "52=20240101-00:00:00.000\x01"
        "56=TW\x01");
}

TEST_F(AdversarialSessionTest, InboundFrameTagWithoutEqualsInField3IsDisregarded) {
    expect_field3_fault_disregarded(
        "999\x01"  // field 3: a tag without '='
        "35=0\x01"
        "34=2\x01"
        "49=ISLD\x01"
        "52=20240101-00:00:00.000\x01"
        "56=TW\x01");
}

// ── Category C: Logon-ack with msg_seq_num=0 → Disconnected ───────────────────
// Exercises Guard (4)'s LogonSent-row — `if (seq == 0) { fsm_state_ = Disconnected; }`

// ── Category C-bis: Logon-ack with out-of-sequence seqnum → Disconnected ──────
// Exercises Guard (4)'s LogonSent-row — `if (!chk) { fsm_state_ = Disconnected; }`
// (seqnum_mgr_.check_inbound returns error for too-high or too-low seqnums).

TEST_F(AdversarialSessionTest, LogonAckOutOfSequenceForcesDisconnected) {
    Session s(engine, make_cfg());
    ASSERT_TRUE(open_session(s).has_value());
    ASSERT_EQ(s.state(), fsm_state::LogonSent);

    // Logon-ack with seq=5 (expected was 1) — seqnum check fails → Disconnected.
    std::string body =
        "35=A\x01"
        "34=5\x01"
        "49=ISLD\x01"
        "52=20240101-00:00:00.000\x01"
        "56=TW\x01"
        "98=0\x01"
        "108=30\x01";
    (void)feed(s, wrap_frame(body));
    EXPECT_EQ(s.state(), fsm_state::Disconnected);
}

// ── Category C-ter: session_arena fallback when both overrides are null ──────
// Exercises resolve_session_arena's `return std::pmr::get_default_resource();` — the
// third-fallback rung of resolve_session_arena's never-null resolution chain.

TEST(AdversarialSessionArena, ResolveArenaThirdFallbackToDefaultResource) {
    fixpp::core::EngineConfig engine{};
    // Both cfg.session_arena and engine.default_session_resource null — resolution
    // must fall through to std::pmr::get_default_resource() (never null).
    SessionConfig cfg;
    Session s(engine, cfg);
    EXPECT_NE(s.session_arena(), nullptr) << "I-18: session_arena() must never return null";
}

TEST_F(AdversarialSessionTest, LogonAckSeqZeroForcesDisconnected) {
    Session s(engine, make_cfg());
    ASSERT_TRUE(open_session(s).has_value());
    ASSERT_EQ(s.state(), fsm_state::LogonSent);

    // Logon-ack with seq=0 — parse_seqnum returns 0 → Disconnected.
    // Peer's SenderCompID = our target_comp_id, peer's TargetCompID = our sender_comp_id
    // so interpret_logon's CompID check passes — only the seq=0 branch should fire.
    std::string body =
        "35=A\x01"
        "34=0\x01"
        "49=ISLD\x01"
        "52=20240101-00:00:00.000\x01"
        "56=TW\x01"
        "98=0\x01"
        "108=30\x01";
    (void)feed(s, wrap_frame(body));
    EXPECT_EQ(s.state(), fsm_state::Disconnected);
}

// ── Category D: cancel_sleeps() mid-Logout → system_error catch ──────────────
// Exercises run_logout_phase1's catch (const std::system_error&) absorbing
// operation_aborted thrown by sleep_until when cancel_sleeps fires during
// the 2-second graceful-close timeout window.

TEST_F(AdversarialSessionTest, LogoutGracefulCancelMidSleep) {
    auto cfg = make_cfg();
    TransportDouble td;
    cfg.transport_send = [&td](std::span<const std::byte> frame) { td.capture_outbound(frame); };
    Session s(engine, cfg);
    ASSERT_TRUE(open_session(s).has_value());
    ASSERT_EQ(s.state(), fsm_state::LogonSent);

    // Drive to Active via Logon-ack (seq=1). Peer's 49/56 must match the
    // session's target/sender (interpret_logon's CompID check).
    std::string body =
        "35=A\x01"
        "34=1\x01"
        "49=ISLD\x01"
        "52=20240101-00:00:00.000\x01"
        "56=TW\x01"
        "98=0\x01"
        "108=30\x01";
    ASSERT_TRUE(feed(s, wrap_frame(body)).has_value());
    ASSERT_EQ(s.state(), fsm_state::Active);

    // Graceful close in background — emits Logout, enters LogoutSent, starts sleep.
    auto close_fut = asio::co_spawn(ioc, s.close(close_mode::graceful), asio::use_future);
    ioc.run_for(50ms);
    ioc.restart();

    // Cancel all sleeps → sleep_until resumes with system_error(operation_aborted),
    // which the Logout coroutine's catch block absorbs (run_logout_phase1's wake-early catch).
    clock->cancel_sleeps();
    if (!fixpp::test_support::run_window_then_ready(ioc, close_fut, 100ms,
                                                    "LogoutGracefulCancelMidSleep/close")) {
        fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                        "LogoutGracefulCancelMidSleep/close");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss << "LogoutGracefulCancelMidSleep/close";
        return;
    }

    EXPECT_EQ(s.state(), fsm_state::Disconnected);
    (void)close_fut.get();
}

}  // namespace fixpp::session::test
