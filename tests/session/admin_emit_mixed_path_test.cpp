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
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/seqnum_manager.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <future>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"

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

// ── Frame builder ─────────────────────────────────────────────────────────────

std::vector<std::byte> build_frame(std::string_view msg_type, std::uint32_t seq,
                                   std::string_view sender, std::string_view target,
                                   std::string_view begin_string, std::string_view sending_time,
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
    unsigned int cs = 0;
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
                                   std::string_view st, std::uint32_t seq = 1, int heartbt = 30) {
    std::string extra =
        "98=0\x01"
        "108=" +
        std::to_string(heartbt) + "\x01";
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

    // Captures every outbound frame emitted via cfg.transport_send so per-test
    // assertions can verify the gated-emit contract (W3.3 / /simplify B-5+C-1 fix).
    // BEFORE W3.3 the tests asserted only sess.state() == Disconnected — that
    // matches even if the gated emit ships NO frame OR an unintended frame; it
    // could not catch a contract violation. transport_send capture closes that gap.
    std::vector<std::vector<std::byte>> captured_frames;

    static constexpr std::string_view kSender = "SENDER";
    static constexpr std::string_view kTarget = "TARGET";
    static constexpr std::string_view kBeginStr = "FIX.4.2";
    // Sending time matching the mock clock at t=0.
    static constexpr std::string_view kSendingTime = "20240101-00:00:00.000";

    void SetUp() override {
        using sc = std::chrono::system_clock;
        auto utc = sc::time_point{} + std::chrono::seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    SessionConfig make_cfg(int heartbt_sec = 0) {
        // heartbt_sec = 0 disables liveness loop (no background coroutine to race).
        SessionConfig cfg;
        cfg.sender_comp_id = std::string(kSender);
        cfg.target_comp_id = std::string(kTarget);
        cfg.begin_string = std::string(kBeginStr);
        cfg.heartbeat_interval = std::chrono::seconds{heartbt_sec};
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.role = session_role::initiator;
        // RC#C (gate-b/r1): bilateral_lenient — tests here don't exercise reset semantics.
        cfg.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
        // W3.3 — capture every outbound frame for per-test gated-emit assertions.
        cfg.transport_send = [this](std::span<const std::byte> frame) {
            captured_frames.emplace_back(frame.begin(), frame.end());
        };
        return cfg;
    }

    // Extract MsgType(35) value from a raw FIX frame.
    static std::string extract_msg_type(std::span<const std::byte> frame) {
        std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
        auto pos = wire.find("35=");
        if (pos == std::string::npos) return {};
        pos += 3;
        auto end = wire.find('\x01', pos);
        if (end == std::string::npos) return {};
        return wire.substr(pos, end - pos);
    }

    // Count outbound frames of a given MsgType captured AFTER `drive_to_active`.
    // Filters out the Logon-handshake frame (35=A) the initiator emits in open().
    std::size_t count_admin_frames_with_type(std::string_view msg_type) const {
        std::size_t n = 0;
        for (const auto& f : captured_frames) {
            if (extract_msg_type(f) == msg_type) ++n;
        }
        return n;
    }

    // Discard handshake-phase outbound frames (Logon) so subsequent assertions
    // see ONLY the admin frames emitted by the path under test.
    void clear_handshake_captures() { captured_frames.clear(); }

    fixpp::core::expected_t<void> open_sync(Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms,
                                                        "AdminEmitMixedPathTest::open_sync")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "AdminEmitMixedPathTest::open_sync");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "AdminEmitMixedPathTest::open_sync";
            return std::unexpected(fixpp::test_support::kWindowMissSentinel);
        }
        return fut.get();
    }

    fixpp::core::expected_t<void> feed_sync(Session& s, std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(frame), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms,
                                                        "AdminEmitMixedPathTest::feed_sync")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "AdminEmitMixedPathTest::feed_sync");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "AdminEmitMixedPathTest::feed_sync";
            return std::unexpected(fixpp::test_support::kWindowMissSentinel);
        }
        return fut.get();
    }

    fixpp::core::expected_t<void> close_sync(Session& s, close_mode mode) {
        auto fut = asio::co_spawn(ioc, s.close(mode), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 500ms,
                                                        "AdminEmitMixedPathTest::close_sync")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "AdminEmitMixedPathTest::close_sync");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "AdminEmitMixedPathTest::close_sync";
            return std::unexpected(fixpp::test_support::kWindowMissSentinel);
        }
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
    clear_handshake_captures();

    // Feed a Heartbeat with stale SendingTime (epoch) → triggers Q3 path.
    // next-expected inbound seq = 2 (Logon was seq=1).
    auto stale_hb = build_stale_heartbeat(2, kTarget, kSender);
    (void)feed_sync(sess, stale_hb);

    // Both Reject and Logout emitted; session transitions to Disconnected.
    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << "Site1 Reject-ok/Logout-ok: session must reach Disconnected via Q3 path";

    // W3.3 — gated-emit contract: BOTH Reject (35=3) and Logout (35=5) frames
    // must have been emitted via transport_send (assign_outbound succeeded for both).
    EXPECT_EQ(count_admin_frames_with_type("3"), 1U)
        << "Site1 Reject-ok/Logout-ok: exactly 1 Reject(35=3) must be emitted";
    EXPECT_EQ(count_admin_frames_with_type("5"), 1U)
        << "Site1 Reject-ok/Logout-ok: exactly 1 Logout(35=5) must be emitted after Reject";
}

// Site1: Reject-fail → Logout-skip → Disconnected (early return at assign failure).
TEST_F(AdminEmitMixedPathTest, Site1_RejectFail_LogoutSkipped_Disconnected) {
    // FR-008: when assign_outbound for Reject fails (overflow), the code returns
    // early → Logout step is skipped. Session still reaches Disconnected.
    auto cfg = make_cfg(0);
    Session sess(engine, cfg);
    ASSERT_TRUE(drive_to_active(sess));
    clear_handshake_captures();

    // Inject overflow: next assign_outbound fails immediately.
    inject_overflow_at(sess, 0);

    auto stale_hb = build_stale_heartbeat(2, kTarget, kSender);
    (void)feed_sync(sess, stale_hb);

    // Session must reach Disconnected via the early-return path.
    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << "Site1 Reject-fail: early return at assign, Logout skipped, still Disconnected";

    // W3.3 — gated-emit contract: NEITHER Reject (35=3) NOR Logout (35=5) may be
    // emitted (assign_outbound failed BEFORE Reject was built; early-return skips
    // Logout entirely). This guards against the failure mode where a frame is
    // emitted without a successful assign_outbound.
    EXPECT_EQ(count_admin_frames_with_type("3"), 0U)
        << "Site1 Reject-fail: Reject(35=3) must NOT be emitted (assign failed pre-build)";
    EXPECT_EQ(count_admin_frames_with_type("5"), 0U)
        << "Site1 Reject-fail: Logout(35=5) must NOT be emitted (early return)";
}

// Site1: Reject-ok + Logout-fail → Disconnected.
TEST_F(AdminEmitMixedPathTest, Site1_RejectOk_LogoutFail_Disconnected) {
    // FR-008: first assign_outbound succeeds (Reject built + emitted), second
    // assign_outbound fails (Logout step) → Disconnected via error path.
    auto cfg = make_cfg(0);
    Session sess(engine, cfg);
    ASSERT_TRUE(drive_to_active(sess));
    clear_handshake_captures();

    // Inject: allow exactly one assign_outbound to succeed, then fail on the second.
    inject_overflow_at(sess, 1);

    auto stale_hb = build_stale_heartbeat(2, kTarget, kSender);
    (void)feed_sync(sess, stale_hb);

    // Session reaches Disconnected regardless of which step fails.
    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << "Site1 Reject-ok/Logout-fail: Disconnected via second assign failure";

    // W3.3 — gated-emit contract: Reject (35=3) IS emitted (first assign succeeded);
    // Logout (35=5) must NOT be emitted (second assign failed before Logout was built).
    EXPECT_EQ(count_admin_frames_with_type("3"), 1U)
        << "Site1 Reject-ok/Logout-fail: exactly 1 Reject(35=3) must be emitted "
           "(first assign succeeded)";
    EXPECT_EQ(count_admin_frames_with_type("5"), 0U)
        << "Site1 Reject-ok/Logout-fail: Logout(35=5) must NOT be emitted "
           "(second assign failed pre-build)";
}

// Site1: Reject-fail + Logout-fail semantically = same as Reject-fail (first
// assign fails, second is never reached). Disconnected either way.
//
// Retained as a separate test (rather than merged with Site1_RejectFail_LogoutSkipped)
// to provide an explicit property-test "the RejectFail path is identical regardless
// of what subsequent Logout-step injection would say" — equivalence-class witness.
TEST_F(AdminEmitMixedPathTest, Site1_RejectFail_LogoutFail_SameAsRejectFail_Disconnected) {
    // FR-008: when Reject fails, the early return prevents reaching the Logout
    // step. The session reaches Disconnected via the early-return path, not via
    // a subsequent Logout-fail.
    auto cfg = make_cfg(0);
    Session sess(engine, cfg);
    ASSERT_TRUE(drive_to_active(sess));
    clear_handshake_captures();

    // Same injection as Reject-fail (next_outbound = seqnum_max).
    inject_overflow_at(sess, 0);

    auto stale_hb = build_stale_heartbeat(2, kTarget, kSender);
    (void)feed_sync(sess, stale_hb);

    EXPECT_EQ(sess.state(), fsm_state::Disconnected);

    // W3.3 — gated-emit contract: equivalence-class witness vs RejectFail_LogoutSkipped.
    // Neither frame is emitted (early return at first assign).
    EXPECT_EQ(count_admin_frames_with_type("3"), 0U)
        << "Site1 Reject-fail equivalence class: Reject(35=3) must NOT be emitted";
    EXPECT_EQ(count_admin_frames_with_type("5"), 0U)
        << "Site1 Reject-fail equivalence class: Logout(35=5) must NOT be emitted";
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
    clear_handshake_captures();

    // Inbound Logout from peer (seq=2, matching clock timestamp).
    auto lo = build_frame("5", 2, kTarget, kSender, kBeginStr, kSendingTime);
    (void)feed_sync(sess, lo);

    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << "Site2 Logout-ok: confirming Logout emitted, session reaches Disconnected";

    // W3.3 — gated-emit contract: exactly 1 confirming Logout (35=5) emitted.
    EXPECT_EQ(count_admin_frames_with_type("5"), 1U)
        << "Site2 Logout-ok: exactly 1 confirming Logout(35=5) must be emitted";
}

// Site2: Logout-fail → Disconnected (assign_outbound fails for confirming Logout).
TEST_F(AdminEmitMixedPathTest, Site2_LogoutFail_Disconnected) {
    // FR-008: when assign_outbound for the confirming Logout fails (overflow),
    // the code returns early with an error. Session still reaches Disconnected
    // (record_state_transition_(Disconnected) fires in the !assign_r branch).
    auto cfg = make_cfg(0);
    Session sess(engine, cfg);
    ASSERT_TRUE(drive_to_active(sess));
    clear_handshake_captures();

    // Inject overflow: next assign_outbound fails immediately.
    inject_overflow_at(sess, 0);

    auto lo = build_frame("5", 2, kTarget, kSender, kBeginStr, kSendingTime);
    (void)feed_sync(sess, lo);

    // Confirming Logout NOT emitted (assign failed) but session still Disconnected.
    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << "Site2 Logout-fail: assign_outbound fails, session still reaches Disconnected";

    // W3.3 — gated-emit contract: confirming Logout(35=5) must NOT be emitted
    // (assign_outbound failed pre-build; the Disconnected transition fires in the
    // !assign_r branch but no outbound frame goes through transport_send).
    EXPECT_EQ(count_admin_frames_with_type("5"), 0U)
        << "Site2 Logout-fail: confirming Logout(35=5) must NOT be emitted "
           "(assign failed pre-build); state-transition-only path";
}

}  // namespace fixpp::session::test
