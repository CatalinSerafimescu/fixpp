// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/admin_builder_distinct_now_test.cpp
//
// 010-session-cfg-lifetime T018 / Phase 5 / US3.
//
// FR-007 / SC-006 — admin builders exercise the per-message-distinct
// SendingTime(52) branch: clock advance between two emits yields distinct
// SendingTime values.
//
// Tests cover the 4 FR-007 admin builders + Logon as bonus coverage:
//   HB_DistinctNow  — Heartbeat (35=0): inbound TestRequest twice with
//                     clock advance between them; echos carry distinct SendingTime.
//   TR_DistinctNow  — TestRequest (35=1): triggered by liveness loop with
//                     heartbt_int=1s; advance clock past two silence windows
//                     and capture both emitted TestRequests' SendingTime.
//   Logout_DistinctNow — Logout (35=5): two distinct graceful-close cycles are
//                        impractical (session is terminal after close). Two
//                        sessions feed inbound Logout with a clock advance between
//                        them; each session emits a confirming Logout with distinct
//                        SendingTime.
//   Reject_DistinctNow — Reject (35=3): two inbound app-level frames (35=D) in
//                        Active with clock advance between them; two Reject frames
//                        carry distinct SendingTime values.
//   Logon_DistinctNow  — Logon (35=A) [BONUS, not part of FR-007's named admin
//                        builders]: two initiator open() calls with clock advance
//                        between them; outbound Logon frames carry distinct
//                        SendingTime. Retained as additional coverage of the
//                        Logon stamp-then-emit path.
//
// Approach: a transport-capture callback on SessionConfig::transport_send captures
// all outbound frames. Field 52 (SendingTime) is extracted from each frame and
// compared across the two emits.
//
// Anchors: spec.md FR-007; SC-006; data-model.md §E1 (admin-builder stamp).

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace fixpp::session::test {

// ── Frame helpers ─────────────────────────────────────────────────────────────

namespace {

// Extract SendingTime(52) value from a raw FIX frame.
std::string extract_sending_time(std::span<const std::byte> frame) {
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    auto pos = wire.find("52=");
    if (pos == std::string::npos) return {};
    pos += 3;
    auto end = wire.find('\x01', pos);
    if (end == std::string::npos) return {};
    return wire.substr(pos, end - pos);
}

// Extract MsgType(35) value from a raw FIX frame.
std::string extract_msg_type(std::span<const std::byte> frame) {
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    auto pos = wire.find("35=");
    if (pos == std::string::npos) return {};
    pos += 3;
    auto end = wire.find('\x01', pos);
    if (end == std::string::npos) return {};
    return wire.substr(pos, end - pos);
}

std::vector<std::byte> build_frame(std::string_view msg_type, std::uint32_t seq,
                                   std::string_view sender, std::string_view target,
                                   std::string_view begin_string,
                                   std::string_view sending_time_val,
                                   std::string_view extra_fields = {}) {
    std::string body;
    body += "35=" + std::string(msg_type) + "\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=" + std::string(sending_time_val) + "\x01";
    body += "56=" + std::string(target) + "\x01";
    if (!extra_fields.empty()) body += std::string(extra_fields);

    std::string hdr;
    hdr += "8=" + std::string(begin_string) + "\x01";
    hdr += "9=" + std::to_string(body.size()) + "\x01";

    std::string full = hdr + body;
    unsigned int cs  = 0;
    for (unsigned char c : full) cs += c;
    cs &= 0xFFU;
    char csbuf[4];
    std::snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    frame.reserve(full.size());
    for (char c : full) frame.push_back(static_cast<std::byte>(c));
    return frame;
}

// Build Logon with correct timestamp for the given mock_clock UTC epoch.
std::vector<std::byte> build_logon(std::string_view sender, std::string_view target,
                                   std::string_view st, std::uint32_t seq = 1,
                                   int heartbt = 30) {
    std::string extra;
    extra += "98=0\x01";
    extra += "108=" + std::to_string(heartbt) + "\x01";
    return build_frame("A", seq, sender, target, "FIX.4.2", st, extra);
}

}  // namespace

// ── Fixture ───────────────────────────────────────────────────────────────────

class AdminDistinctNowTest : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;
    std::vector<std::vector<std::byte>> captured_frames;

    static constexpr std::string_view kSender   = "SENDER";
    static constexpr std::string_view kTarget   = "TARGET";
    static constexpr std::string_view kBeginStr = "FIX.4.2";

    void SetUp() override {
        // Anchor at 2024-01-01 00:00:00 UTC so inbound frames with that timestamp pass Q3.
        using sc     = std::chrono::system_clock;
        auto utc     = sc::time_point{} + std::chrono::seconds{1704067200};
        auto stp     = fixpp::core::steady_time_point{};
        clock        = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock    = clock;
        engine.executor = ioc.get_executor();
    }

    SessionConfig make_cfg(int heartbt_sec = 30) {
        SessionConfig cfg;
        cfg.sender_comp_id     = std::string(kSender);
        cfg.target_comp_id     = std::string(kTarget);
        cfg.begin_string       = std::string(kBeginStr);
        cfg.heartbeat_interval = std::chrono::seconds{heartbt_sec};
        cfg.security_profile   = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary         = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override  = ioc.get_executor();
        cfg.role               = session_role::initiator;
        cfg.transport_send     = [this](std::span<const std::byte> frame) {
            captured_frames.emplace_back(frame.begin(), frame.end());
        };
        return cfg;
    }

    fixpp::core::expected_t<void> open_sync(Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        return fut.get();
    }

    fixpp::core::expected_t<void> feed_sync(Session& s, std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(frame), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        return fut.get();
    }

    fixpp::core::expected_t<void> close_sync(Session& s, close_mode mode) {
        auto fut = asio::co_spawn(ioc, s.close(mode), asio::use_future);
        ioc.run_for(500ms);
        ioc.restart();
        return fut.get();
    }

    // The mock clock's current UTC as a FIX-format string "YYYYMMDD-HH:MM:SS.mmm".
    std::string clock_utc_str() {
        // Use the same format as stamp_sending_time: 21-char millis-precision UTC.
        // We reconstruct from the mock's now() in seconds-since-epoch.
        // The clock starts at 2024-01-01T00:00:00 UTC; we can compute from the advance.
        // Simpler: just format it ourselves from the known epoch + advance.
        // Actually we call the advance count to know current time. The test below
        // uses a known sending-time format string and verifies != between two calls.
        // We don't need to compute the exact string here — we just verify they differ.
        return {};
    }

    // Drive to Active: open(initiator) → LogonSent → feed peer Logon → Active.
    bool drive_to_active(Session& s) {
        if (!open_sync(s).has_value()) return false;
        if (s.state() != fsm_state::LogonSent) return false;
        // Peer Logon: sender=TARGET, target=SENDER, seq=1.
        // SendingTime must be at mock_clock epoch (2024-01-01-00:00:00.000).
        auto peer_logon = build_logon(kTarget, kSender, "20240101-00:00:00.000");
        (void)feed_sync(s, peer_logon);
        return s.state() == fsm_state::Active;
    }

    // Find nth frame (0-based) with matching MsgType, or nullptr.
    const std::vector<std::byte>* find_frame_with_type(std::string_view msg_type,
                                                        std::size_t skip = 0) const {
        std::size_t count = 0;
        for (const auto& f : captured_frames) {
            auto mt = extract_msg_type(f);
            if (mt == msg_type) {
                if (count == skip) return &f;
                ++count;
            }
        }
        return nullptr;
    }
};

// ── T018 Test 1: Heartbeat (35=0) — per-message-distinct SendingTime ─────────
//
// Trigger: inbound TestRequest → Session emits Heartbeat(35=0) with SendingTime.
// Advance clock by 1 s between two TestRequest triggers.
// Assert: first and second Heartbeat SendingTime values differ.
TEST_F(AdminDistinctNowTest, HB_DistinctNow_TwoHeartbeatsHaveDistinctSendingTime) {
    // FR-007: each admin builder stamps SendingTime from effective_clock.now();
    // advancing the clock between emits yields distinct values.
    auto cfg = make_cfg(30);
    Session sess(engine, cfg);
    ASSERT_TRUE(drive_to_active(sess));
    ASSERT_EQ(sess.state(), fsm_state::Active);

    // Inbound TestRequest seq=2 → Session emits Heartbeat(echo) with current time.
    std::string extra1 = "112=TR-FIRST\x01";
    auto tr1 = build_frame("1", 2, kTarget, kSender, kBeginStr, "20240101-00:00:00.000", extra1);
    (void)feed_sync(sess, tr1);

    // Find the first Heartbeat.
    const auto* hb1 = find_frame_with_type("0");
    ASSERT_NE(hb1, nullptr) << "First Heartbeat must have been emitted";
    std::string st1 = extract_sending_time(*hb1);
    ASSERT_FALSE(st1.empty()) << "First Heartbeat must contain SendingTime(52)";

    // Advance mock clock by 1 second.
    clock->advance(1s);

    // Inbound TestRequest seq=3 → Session emits Heartbeat with new clock time.
    std::string extra2 = "112=TR-SECOND\x01";
    auto tr2 = build_frame("1", 3, kTarget, kSender, kBeginStr, "20240101-00:00:01.000", extra2);
    (void)feed_sync(sess, tr2);

    // Find the second Heartbeat (skip=1 skips the first).
    const auto* hb2 = find_frame_with_type("0", 1);
    ASSERT_NE(hb2, nullptr) << "Second Heartbeat must have been emitted";
    std::string st2 = extract_sending_time(*hb2);
    ASSERT_FALSE(st2.empty()) << "Second Heartbeat must contain SendingTime(52)";

    EXPECT_NE(st1, st2)
        << "Two Heartbeats emitted 1 s apart must have distinct SendingTime(52) values"
        << " (FR-007 per-message-distinct branch). First=" << st1 << " Second=" << st2;

    (void)close_sync(sess, close_mode::terminal);
}

// ── T018 Test 2 (FR-007 admin builder): TestRequest (35=1) — per-message-distinct ──
//
// Trigger: liveness loop fires TestRequest after heartbt_int silence
// (session.cpp:1437+). Advance mock_clock past two consecutive heartbt_int
// windows (with peer Heartbeat echo between to satisfy the liveness contract
// and avoid the unanswered-TR Disconnected branch). Both emitted TestRequest
// frames must carry distinct SendingTime(52) values.
//
// heartbt_int chosen = 1 s (smallest std::chrono::seconds value); advances of 2 s
// per window give comfortable margin over the deadline + grace boundaries.
TEST_F(AdminDistinctNowTest, TR_DistinctNow_TwoLivenessTestRequestsHaveDistinctSendingTime) {
    // FR-007: TestRequest builder stamps SendingTime from effective_clock.now()
    // each time the liveness loop emits one.
    auto cfg = make_cfg(1);  // heartbt_int = 1 s
    Session sess(engine, cfg);
    ASSERT_TRUE(drive_to_active(sess));
    ASSERT_EQ(sess.state(), fsm_state::Active);

    // Discard Logon-handshake frames so find_frame_with_type("1") returns the
    // first TestRequest emitted by the liveness loop, not anything earlier.
    captured_frames.clear();

    // Window 1: silence past heartbt_int → liveness loop emits TR1.
    clock->advance(2s);
    ioc.run_for(200ms);
    ioc.restart();

    const auto* tr1 = find_frame_with_type("1");
    ASSERT_NE(tr1, nullptr)
        << "First TestRequest must have been emitted by liveness loop after heartbt_int silence";
    std::string st1 = extract_sending_time(*tr1);
    ASSERT_FALSE(st1.empty()) << "First TestRequest must contain SendingTime(52)";

    // Extract TestReqID(112) from TR1 and echo it in a Heartbeat so the
    // liveness loop's grace-window check finds pending_test_req_id_ cleared
    // and continues looping (instead of taking the unanswered-TR → Disconnected branch).
    std::string tr1_wire(reinterpret_cast<const char*>(tr1->data()), tr1->size());
    auto tid_pos = tr1_wire.find("112=");
    ASSERT_NE(tid_pos, std::string::npos) << "TR1 must contain TestReqID(112)";
    tid_pos += 4;
    auto tid_end = tr1_wire.find('\x01', tid_pos);
    ASSERT_NE(tid_end, std::string::npos);
    std::string test_req_id = tr1_wire.substr(tid_pos, tid_end - tid_pos);

    std::string hb_extra = "112=" + test_req_id + "\x01";
    auto hb_reply = build_frame("0", 2, kTarget, kSender, kBeginStr,
                                "20240101-00:00:02.000", hb_extra);
    (void)feed_sync(sess, hb_reply);

    // Window 2: advance past another heartbt_int (deadline = new last_inbound +
    // heartbt_int). 2 s gives margin over the deadline + grace boundary.
    clock->advance(2s);
    ioc.run_for(200ms);
    ioc.restart();

    const auto* tr2 = find_frame_with_type("1", 1);
    ASSERT_NE(tr2, nullptr)
        << "Second TestRequest must have been emitted after second silence window";
    std::string st2 = extract_sending_time(*tr2);
    ASSERT_FALSE(st2.empty()) << "Second TestRequest must contain SendingTime(52)";

    EXPECT_NE(st1, st2)
        << "Two TestRequests emitted by liveness loop 2 s apart must have distinct "
        << "SendingTime(52) values (FR-007 per-message-distinct branch). First=" << st1
        << " Second=" << st2;

    (void)close_sync(sess, close_mode::terminal);
}

// ── T018 Test 3 (FR-007 admin builder): Reject (35=3) — per-message-distinct ──
//
// Trigger: inbound unknown application MsgType "D" in Active → Session emits
// Reject(35=3, reason=3 unsupported MsgType). Advance clock by 1 s between
// two triggers. Assert: first and second Reject SendingTime values differ.
//
// Note: session admin MsgTypes (A/0/1/2/3/4/5) bypass the Reject branch.
// Only a non-admin MsgType ("D" = NewOrderSingle) reaches the
// !is_session_admin → build_reject path (session.cpp:964+).
// SendingTime must be within 120 s of mock_clock to pass the Q3 guard.
TEST_F(AdminDistinctNowTest, Reject_DistinctNow_TwoRejectsHaveDistinctSendingTime) {
    // FR-007: Reject builder stamps SendingTime from effective_clock.now().
    auto cfg = make_cfg(30);
    Session sess(engine, cfg);
    ASSERT_TRUE(drive_to_active(sess));
    ASSERT_EQ(sess.state(), fsm_state::Active);

    // First inbound app MsgType "D" (seq=2, SendingTime at clock epoch) →
    // !is_session_admin → Reject(35=3) emitted; session remains Active.
    auto app1 = build_frame("D", 2, kTarget, kSender, kBeginStr, "20240101-00:00:00.000");
    (void)feed_sync(sess, app1);

    const auto* rj1 = find_frame_with_type("3");
    ASSERT_NE(rj1, nullptr) << "First Reject must have been emitted for unknown MsgType 'D'";
    std::string st1 = extract_sending_time(*rj1);
    ASSERT_FALSE(st1.empty());

    ASSERT_EQ(sess.state(), fsm_state::Active) << "Session must remain Active after Reject";

    // Advance clock by 1 second.
    clock->advance(1s);

    // Second inbound app MsgType "D" (seq=3, SendingTime updated by 1 s) → another Reject.
    auto app2 = build_frame("D", 3, kTarget, kSender, kBeginStr, "20240101-00:00:01.000");
    (void)feed_sync(sess, app2);

    const auto* rj2 = find_frame_with_type("3", 1);
    ASSERT_NE(rj2, nullptr) << "Second Reject must have been emitted";
    std::string st2 = extract_sending_time(*rj2);
    ASSERT_FALSE(st2.empty());

    EXPECT_NE(st1, st2)
        << "Two Rejects emitted 1 s apart must have distinct SendingTime(52) values"
        << " (FR-007). First=" << st1 << " Second=" << st2;

    (void)close_sync(sess, close_mode::terminal);
}

// ── T018 Test 3: Logout (35=5) — per-message-distinct SendingTime ────────────
//
// Trigger: inbound Logout in Active → Session emits confirming Logout(35=5).
// We use two separate sessions to get two Logout emits, advancing the clock
// between them, and compare their SendingTime values.
TEST_F(AdminDistinctNowTest, Logout_DistinctNow_TwoLogoutsHaveDistinctSendingTime) {
    // FR-007: Logout builder stamps SendingTime from effective_clock.now().
    // Session 1: emit Logout(35=5) at t=0.
    std::vector<std::vector<std::byte>> captured1, captured2;

    {
        SessionConfig cfg1  = make_cfg(30);
        cfg1.transport_send = [&captured1](std::span<const std::byte> f) {
            captured1.emplace_back(f.begin(), f.end());
        };
        Session sess1(engine, cfg1);
        ASSERT_TRUE(drive_to_active(sess1));

        // Feed inbound Logout → Session emits confirming Logout(35=5).
        auto peer_lo = build_frame("5", 2, kTarget, kSender, kBeginStr, "20240101-00:00:00.000");
        (void)feed_sync(sess1, peer_lo);
        // sess1 is now Disconnected.
    }

    // Advance clock by 1 second.
    clock->advance(1s);

    {
        SessionConfig cfg2  = make_cfg(30);
        cfg2.transport_send = [&captured2](std::span<const std::byte> f) {
            captured2.emplace_back(f.begin(), f.end());
        };
        Session sess2(engine, cfg2);
        ASSERT_TRUE(drive_to_active(sess2));

        auto peer_lo = build_frame("5", 2, kTarget, kSender, kBeginStr, "20240101-00:00:01.000");
        (void)feed_sync(sess2, peer_lo);
    }

    // Find Logout frames from each session.
    std::string st1, st2;
    for (const auto& f : captured1) {
        if (extract_msg_type(f) == "5") { st1 = extract_sending_time(f); break; }
    }
    for (const auto& f : captured2) {
        if (extract_msg_type(f) == "5") { st2 = extract_sending_time(f); break; }
    }

    ASSERT_FALSE(st1.empty()) << "Session 1 must have emitted a Logout(35=5)";
    ASSERT_FALSE(st2.empty()) << "Session 2 must have emitted a Logout(35=5)";
    EXPECT_NE(st1, st2)
        << "Two Logout frames emitted 1 s apart must have distinct SendingTime(52)"
        << " (FR-007 per-message-distinct branch). First=" << st1 << " Second=" << st2;
}

// ── T018 Test 4: Logon (35=A) — per-message-distinct SendingTime ─────────────
//
// Two sessions open() at times 0 and +1 s; each emits an initiator Logon.
// Their SendingTime values must differ.
TEST_F(AdminDistinctNowTest, Logon_DistinctNow_TwoInitiatorLogonsHaveDistinctSendingTime) {
    // FR-007: Logon builder (open() initiator path) stamps SendingTime from clock.
    std::string st1, st2;

    {
        std::vector<std::vector<std::byte>> cap1;
        SessionConfig cfg1  = make_cfg(30);
        cfg1.transport_send = [&cap1](std::span<const std::byte> f) {
            cap1.emplace_back(f.begin(), f.end());
        };
        Session sess1(engine, cfg1);
        (void)open_sync(sess1);
        // First frame emitted by initiator open() is the outbound Logon.
        if (!cap1.empty()) {
            for (const auto& f : cap1) {
                if (extract_msg_type(f) == "A") { st1 = extract_sending_time(f); break; }
            }
        }
        (void)close_sync(sess1, close_mode::terminal);
    }

    // Advance clock by 1 second.
    clock->advance(1s);

    {
        std::vector<std::vector<std::byte>> cap2;
        SessionConfig cfg2  = make_cfg(30);
        cfg2.transport_send = [&cap2](std::span<const std::byte> f) {
            cap2.emplace_back(f.begin(), f.end());
        };
        Session sess2(engine, cfg2);
        (void)open_sync(sess2);
        if (!cap2.empty()) {
            for (const auto& f : cap2) {
                if (extract_msg_type(f) == "A") { st2 = extract_sending_time(f); break; }
            }
        }
        (void)close_sync(sess2, close_mode::terminal);
    }

    ASSERT_FALSE(st1.empty()) << "Session 1 initiator open() must emit Logon(35=A)";
    ASSERT_FALSE(st2.empty()) << "Session 2 initiator open() must emit Logon(35=A)";
    EXPECT_NE(st1, st2)
        << "Two Logons emitted 1 s apart must have distinct SendingTime(52)"
        << " (FR-007). First=" << st1 << " Second=" << st2;
}

}  // namespace fixpp::session::test
