// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/admin_emit_mixed_path_test.cpp
//
// 010-session-cfg-lifetime T019 / Phase 5 / US3.
//
// FR-008 / SC-005 — gated-emit permutations at admin emit sites 1+2 (RC#G
// mixed-path). Exercises the four mixed-success-mode combinations at each site:
//   (Reject-ok, Logout-ok)     — nominal path, both succeed
//   (Reject-fail, Logout-skip) — assign_outbound fails for Reject → early return,
//                                Logout step is skipped
//   (Reject-ok, Logout-fail)   — first assign succeeds, second fails → Disconnected
//   (Reject-fail, Logout-fail) — same as Reject-fail (first fails; Logout never attempted)
//
// Injection mechanism: SeqnumManager::set_counters_for_test() (FIXPP_TEST_HOOKS)
// sets next_outbound_ to seqnum_max, causing the next assign_outbound() to
// return store_seqnum_overflow. Setting next_outbound_ = seqnum_max-1 allows
// exactly one more assign before overflow.
//
// Site 1 (Q3 SendingTime validation fail in Active/LogonReceived):
//   Step 1: assign_outbound for Reject. If fail → Disconnected (Logout skipped).
//   Step 2: assign_outbound for Logout. If fail → Disconnected.
//
// Site 2 (Active row: inbound Logout → confirming Logout emit):
//   Step 1: assign_outbound for confirming Logout. If fail → Disconnected.
//   (Only one step here; "Reject-ok/Logout-fail" maps to Logout-ok vs Logout-fail.)
//
// NOTE: FIXPP_TEST_HOOKS must be defined for the test target (see CMakeLists.txt).
//
// Anchors: spec.md FR-008 / SC-005; data-model.md §E1 Active row;
//          session.cpp Q3 path + inbound-Logout path; RC#G mixed-path.

#ifndef FIXPP_TEST_HOOKS
#error "admin_emit_mixed_path_test.cpp requires FIXPP_TEST_HOOKS"
#endif

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <future>
#include <limits>
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
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/seqnum_manager.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace fixpp::session::test {

namespace {

// ── Frame builder ─────────────────────────────────────────────────────────────

std::vector<std::byte> build_frame(std::string_view msg_type, std::uint32_t seq,
                                   std::string_view sender, std::string_view target,
                                   std::string_view begin_string,
                                   std::string_view sending_time,
                                   std::string_view extra_fields = {}) {
    std::string body;
    body += "35=" + std::string(msg_type) + "\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=" + std::string(sending_time) + "\x01";
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

// Build a valid Logon frame with correct sending_time.
std::vector<std::byte> build_logon(std::string_view sender, std::string_view target,
                                   std::string_view st, std::uint32_t seq = 1,
                                   int heartbt = 30) {
    std::string extra = "98=0\x01" "108=" + std::to_string(heartbt) + "\x01";
    return build_frame("A", seq, sender, target, "FIX.4.2", st, extra);
}

// Build an inbound ResendRequest (35=2) — triggers the Q3 out-of-scope admin
// path when it arrives in Active state — NOT via the Q3 SendingTime path.
//
// For Q3 path: send a frame with a stale SendingTime (epoch mismatch > 120 s).
// Clock is at 2024-01-01; epoch=0 sending time (1970-01-01) is > 120 s stale.
std::vector<std::byte> build_stale_heartbeat(std::uint32_t seq, std::string_view sender,
                                             std::string_view target) {
    // SendingTime = epoch (1970-01-01-00:00:00.000) — always > 120 s stale vs 2024 clock.
    return build_frame("0", seq, sender, target, "FIX.4.2", "19700101-00:00:00.000");
}

}  // namespace

// ── Fixture ───────────────────────────────────────────────────────────────────

class AdminEmitMixedPathTest : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;

    static constexpr std::string_view kSender   = "SENDER";
    static constexpr std::string_view kTarget   = "TARGET";
    static constexpr std::string_view kBeginStr = "FIX.4.2";
    // Sending time matching the mock clock at t=0.
    static constexpr std::string_view kSendingTime = "20240101-00:00:00.000";

    void SetUp() override {
        using sc     = std::chrono::system_clock;
        auto utc     = sc::time_point{} + std::chrono::seconds{1704067200};
        auto stp     = fixpp::core::steady_time_point{};
        clock        = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock    = clock;
        engine.executor = ioc.get_executor();
    }

    SessionConfig make_cfg(int heartbt_sec = 0) {
        // heartbt_sec = 0 disables liveness loop (no background coroutine to race).
        SessionConfig cfg;
        cfg.sender_comp_id     = std::string(kSender);
        cfg.target_comp_id     = std::string(kTarget);
        cfg.begin_string       = std::string(kBeginStr);
        cfg.heartbeat_interval = std::chrono::seconds{heartbt_sec};
        cfg.security_profile   = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary         = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override  = ioc.get_executor();
        cfg.role               = session_role::initiator;
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

    // Drive initiator to Active (heartbt=0 disables liveness loop).
    bool drive_to_active(Session& s) {
        if (!open_sync(s).has_value()) return false;
        if (s.state() != fsm_state::LogonSent) return false;
        auto peer_logon = build_logon(kTarget, kSender, kSendingTime, 1, 0);
        (void)feed_sync(s, peer_logon);
        return s.state() == fsm_state::Active;
    }

    // Inject seqnum overflow so the Nth next assign_outbound fails.
    // n=0: next assign_outbound fails immediately (next_outbound = seqnum_max).
    // n=1: next assign_outbound succeeds, subsequent one fails.
    void inject_overflow_at(Session& s, int n) {
        seqnum_t val = seqnum_max;
        if (n == 1) {
            // seqnum_max - 1 → first assign succeeds (returns seqnum_max - 1),
            // increments to seqnum_max; second assign fails.
            val = seqnum_max - 1;
        }
        // Keep next_inbound at its current value; only change next_outbound.
        seqnum_t cur_in = s.seqnum_mgr_test_access().next_inbound_unsafe();
        s.seqnum_mgr_test_access().set_counters_for_test(cur_in, val);
    }
};

// ════════════════════════════════════════════════════════════════════════════
// Site 1 — Q3 SendingTime validation fail path (Active/LogonReceived row):
//   Reject(35=3) assign → Logout(35=5) assign → Disconnected
// ════════════════════════════════════════════════════════════════════════════

// Site1: Reject-ok + Logout-ok → Disconnected (nominal path, no injection).
TEST_F(AdminEmitMixedPathTest, Site1_RejectOk_LogoutOk_Disconnected) {
    // FR-008: Q3 path nominal. Both Reject and Logout assigns succeed; session
    // reaches Disconnected per the matrix Active row.
    auto cfg = make_cfg(0);
    Session sess(engine, cfg);
    ASSERT_TRUE(drive_to_active(sess));

    // Feed a Heartbeat with stale SendingTime (epoch) → triggers Q3 path.
    // next-expected inbound seq = 2 (Logon was seq=1).
    auto stale_hb = build_stale_heartbeat(2, kTarget, kSender);
    (void)feed_sync(sess, stale_hb);

    // Both Reject and Logout emitted; session transitions to Disconnected.
    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << "Site1 Reject-ok/Logout-ok: session must reach Disconnected via Q3 path";
}

// Site1: Reject-fail → Logout-skip → Disconnected (early return at assign failure).
TEST_F(AdminEmitMixedPathTest, Site1_RejectFail_LogoutSkipped_Disconnected) {
    // FR-008: when assign_outbound for Reject fails (overflow), the code returns
    // early → Logout step is skipped. Session still reaches Disconnected.
    auto cfg = make_cfg(0);
    Session sess(engine, cfg);
    ASSERT_TRUE(drive_to_active(sess));

    // Inject overflow: next assign_outbound fails immediately.
    inject_overflow_at(sess, 0);

    auto stale_hb = build_stale_heartbeat(2, kTarget, kSender);
    (void)feed_sync(sess, stale_hb);

    // Session must reach Disconnected via the early-return path.
    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << "Site1 Reject-fail: early return at assign, Logout skipped, still Disconnected";
}

// Site1: Reject-ok + Logout-fail → Disconnected.
TEST_F(AdminEmitMixedPathTest, Site1_RejectOk_LogoutFail_Disconnected) {
    // FR-008: first assign_outbound succeeds (Reject built + emitted), second
    // assign_outbound fails (Logout step) → Disconnected via error path.
    auto cfg = make_cfg(0);
    Session sess(engine, cfg);
    ASSERT_TRUE(drive_to_active(sess));

    // Inject: allow exactly one assign_outbound to succeed, then fail on the second.
    inject_overflow_at(sess, 1);

    auto stale_hb = build_stale_heartbeat(2, kTarget, kSender);
    (void)feed_sync(sess, stale_hb);

    // Session reaches Disconnected regardless of which step fails.
    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << "Site1 Reject-ok/Logout-fail: Disconnected via second assign failure";
}

// Site1: Reject-fail + Logout-fail semantically = same as Reject-fail (first
// assign fails, second is never reached). Disconnected either way.
TEST_F(AdminEmitMixedPathTest, Site1_RejectFail_LogoutFail_SameAsRejectFail_Disconnected) {
    // FR-008: when Reject fails, the early return prevents reaching the Logout
    // step. The session reaches Disconnected via the early-return path, not via
    // a subsequent Logout-fail.
    auto cfg = make_cfg(0);
    Session sess(engine, cfg);
    ASSERT_TRUE(drive_to_active(sess));

    // Same injection as Reject-fail (next_outbound = seqnum_max).
    inject_overflow_at(sess, 0);

    auto stale_hb = build_stale_heartbeat(2, kTarget, kSender);
    (void)feed_sync(sess, stale_hb);

    EXPECT_EQ(sess.state(), fsm_state::Disconnected);
}

// ════════════════════════════════════════════════════════════════════════════
// Site 2 — Active row: inbound Logout → confirming Logout assign → Disconnected.
// The confirming Logout has one assign_outbound step.
// ════════════════════════════════════════════════════════════════════════════

// Site2: Logout-ok → Disconnected (nominal confirming Logout).
TEST_F(AdminEmitMixedPathTest, Site2_LogoutOk_Disconnected) {
    // FR-008: inbound Logout in Active → assign succeeds → confirming Logout emitted
    // → Disconnected per matrix Active×Logout cell.
    auto cfg = make_cfg(0);
    Session sess(engine, cfg);
    ASSERT_TRUE(drive_to_active(sess));

    // Inbound Logout from peer (seq=2, matching clock timestamp).
    auto lo = build_frame("5", 2, kTarget, kSender, kBeginStr, kSendingTime);
    (void)feed_sync(sess, lo);

    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << "Site2 Logout-ok: confirming Logout emitted, session reaches Disconnected";
}

// Site2: Logout-fail → Disconnected (assign_outbound fails for confirming Logout).
TEST_F(AdminEmitMixedPathTest, Site2_LogoutFail_Disconnected) {
    // FR-008: when assign_outbound for the confirming Logout fails (overflow),
    // the code returns early with an error. Session still reaches Disconnected
    // (record_state_transition_(Disconnected) fires in the !assign_r branch).
    auto cfg = make_cfg(0);
    Session sess(engine, cfg);
    ASSERT_TRUE(drive_to_active(sess));

    // Inject overflow: next assign_outbound fails immediately.
    inject_overflow_at(sess, 0);

    auto lo = build_frame("5", 2, kTarget, kSender, kBeginStr, kSendingTime);
    (void)feed_sync(sess, lo);

    // Confirming Logout NOT emitted (assign failed) but session still Disconnected.
    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << "Site2 Logout-fail: assign_outbound fails, session still reaches Disconnected";
}

}  // namespace fixpp::session::test
