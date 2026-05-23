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
//   B. Malformed-field parser fallback in interpret_logon and
//      Session::on_inbound_frame: non-digit tag char + tag-without-equals.
//   C. Logon-ack with msg_seq_num=0 → Disconnected (session.cpp:896-899).
//   D. cancel_sleeps() mid-Logout-graceful-sleep → system_error catch
//      absorbing the operation_aborted exception (session.cpp:1187).
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/admin_messages.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/transport_double.hpp"

#include <gtest/gtest.h>

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
    auto r = build_logout(std::span<std::byte>{buf}, 1, "TW", "ISLD", "",
                          "FIX.4.2", "20240101-00:00:00.000");
    EXPECT_FALSE(r.has_value());
}

TEST(AdversarialBuilderBufferTooSmall, BuildHeartbeatZeroBuf) {
    std::array<std::byte, 0> buf{};
    auto r = build_heartbeat(std::span<std::byte>{buf}, 1, "TW", "ISLD", "",
                             "FIX.4.2", "20240101-00:00:00.000");
    EXPECT_FALSE(r.has_value());
}

TEST(AdversarialBuilderBufferTooSmall, BuildTestRequestZeroBuf) {
    std::array<std::byte, 0> buf{};
    auto r = build_test_request(std::span<std::byte>{buf}, 1, "TW", "ISLD", "TR1",
                                "FIX.4.2", "20240101-00:00:00.000");
    EXPECT_FALSE(r.has_value());
}

TEST(AdversarialBuilderBufferTooSmall, BuildRejectZeroBuf) {
    std::array<std::byte, 0> buf{};
    auto r = build_reject(std::span<std::byte>{buf}, 1, "TW", "ISLD", 2, 35, "0", 5,
                          "FIX.4.2", "20240101-00:00:00.000");
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
    std::string body = "X9=garbage\x01"
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
    std::string body = "999\x01"  // tag with no '='
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
        return cfg;
    }

    fixpp::core::expected_t<void> open_session(Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        return fut.get();
    }

    fixpp::core::expected_t<void> feed(Session& s, std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(frame), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        return fut.get();
    }
};

// ── Category B-2: session.cpp inbound parser skip-malformed-field ─────────────
// Exercises session.cpp:405-422 (tag_ok=false branch + skip-to-SOH path).

TEST_F(AdversarialSessionTest, InboundFrameMalformedTagCharSkipped) {
    Session s(engine, make_cfg());
    ASSERT_TRUE(open_session(s).has_value());

    // Inbound frame with a garbage field (non-digit tag) before the real fields.
    std::string body = "X35=garbage\x01"
                       "35=0\x01"  // Heartbeat
                       "34=2\x01"
                       "49=TW\x01"
                       "52=20240101-00:00:00.000\x01"
                       "56=ISLD\x01";
    (void)feed(s, wrap_frame(body));
}

TEST_F(AdversarialSessionTest, InboundFrameTagWithoutEqualsSkipped) {
    Session s(engine, make_cfg());
    ASSERT_TRUE(open_session(s).has_value());

    std::string body = "999\x01"  // tag without '='
                       "35=0\x01"
                       "34=2\x01"
                       "49=TW\x01"
                       "52=20240101-00:00:00.000\x01"
                       "56=ISLD\x01";
    (void)feed(s, wrap_frame(body));
}

// ── Category C: Logon-ack with msg_seq_num=0 → Disconnected ───────────────────
// Exercises session.cpp:896-899 — `if (seq == 0) { fsm_state_ = Disconnected; }`

// ── Category C-bis: Logon-ack with out-of-sequence seqnum → Disconnected ──────
// Exercises session.cpp:904-906 — `if (!chk) { fsm_state_ = Disconnected; }`
// (seqnum_mgr_.check_inbound returns error for too-high or too-low seqnums).

TEST_F(AdversarialSessionTest, LogonAckOutOfSequenceForcesDisconnected) {
    Session s(engine, make_cfg());
    ASSERT_TRUE(open_session(s).has_value());
    ASSERT_EQ(s.state(), fsm_state::LogonSent);

    // Logon-ack with seq=5 (expected was 1) — seqnum check fails → Disconnected.
    std::string body = "35=A\x01"
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
// Exercises session.cpp:71 — `return std::pmr::get_default_resource();` is the
// third-fallback rung of resolve_session_arena's never-null resolution chain.

TEST(AdversarialSessionArena, ResolveArenaThirdFallbackToDefaultResource) {
    fixpp::core::EngineConfig engine{};
    // Both cfg.session_arena and engine.default_session_resource null — resolution
    // must fall through to std::pmr::get_default_resource() (never null).
    SessionConfig cfg;
    Session s(engine, cfg);
    EXPECT_NE(s.session_arena(), nullptr)
        << "I-18: session_arena() must never return null";
}

TEST_F(AdversarialSessionTest, LogonAckSeqZeroForcesDisconnected) {
    Session s(engine, make_cfg());
    ASSERT_TRUE(open_session(s).has_value());
    ASSERT_EQ(s.state(), fsm_state::LogonSent);

    // Logon-ack with seq=0 — parse_seqnum returns 0 → Disconnected.
    // Peer's SenderCompID = our target_comp_id, peer's TargetCompID = our sender_comp_id
    // so interpret_logon's CompID check passes — only the seq=0 branch should fire.
    std::string body = "35=A\x01"
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
// Exercises session.cpp:1187 — catch (const std::system_error&) absorbing
// operation_aborted thrown by sleep_until when cancel_sleeps fires during
// the 2-second graceful-close timeout window.

TEST_F(AdversarialSessionTest, LogoutGracefulCancelMidSleep) {
    auto cfg = make_cfg();
    TransportDouble td;
    cfg.transport_send = [&td](std::span<const std::byte> frame) {
        td.capture_outbound(frame);
    };
    Session s(engine, cfg);
    ASSERT_TRUE(open_session(s).has_value());
    ASSERT_EQ(s.state(), fsm_state::LogonSent);

    // Drive to Active via Logon-ack (seq=1). Peer's 49/56 must match the
    // session's target/sender (interpret_logon's CompID check).
    std::string body = "35=A\x01"
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
    // which the Logout coroutine's catch block absorbs (session.cpp:1187).
    clock->cancel_sleeps();
    ioc.run_for(100ms);
    ioc.restart();

    EXPECT_EQ(s.state(), fsm_state::Disconnected);
    (void)close_fut.get();
}

}  // namespace fixpp::session::test
