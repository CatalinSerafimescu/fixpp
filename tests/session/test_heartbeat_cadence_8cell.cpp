// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_heartbeat_cadence_8cell.cpp — T018 [US1] Phase 3 RED
//
// Heartbeat/TestRequest 8-cell matrix:
//   4 cadence axes × 2 executor modes (per_session_strand / direct_executor)
//
// Cadence axes:
//   A. No outbound for HeartBtInt → Heartbeat(0) emitted
//   B. No inbound for 1.2×HeartBtInt → TestRequest(1) emitted
//   C. No inbound reply within 2×HeartBtInt total → session_test_request_unanswered(74)
//   D. Inbound Heartbeat TestReqID mismatch → session_testreqid_mismatch(118)
//
// Anchors: spec.md §US1 / FR-003..FR-007; data-model.md §E-1;
//   plan.md §Test plan T018; [FIX-SL §4.3.2] / [FIX-SL §4.4];
//   contracts/reconnect_fsm.hpp; [2d §4.8] 2-executor modes.
//
// Production-shape: drives bytes through Session::on_inbound_frame();
//   mock_clock::advance() for deterministic time.
//
// RED witness: run_heartbeat_cadence / run_inbound_liveness_watch stubs
//   co_return {} (immediately) without arming any timer. All 8 assertions
//   FAIL RED until T024 impl lands.

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

namespace {

static std::string field(int tag, std::string_view val) {
    return std::to_string(tag) + "=" + std::string(val) + "\x01";
}

static std::vector<std::byte> make_fix_frame(std::string_view begin_string,
                                             std::string_view msg_type, std::uint32_t seq,
                                             std::string_view sender, std::string_view target,
                                             std::string_view extra = {}) {
    std::string body;
    body += field(35, msg_type);
    body += field(34, std::to_string(seq));
    body += field(49, sender);
    body += field(52, "20240101-00:00:00.000");
    body += field(56, target);
    if (!extra.empty()) body += std::string(extra);

    std::string msg;
    msg += "8=" + std::string(begin_string) + "\x01";
    msg += "9=" + std::to_string(body.size()) + "\x01";
    msg += body;
    unsigned int cs = 0;
    for (unsigned char c : msg) cs += c;
    cs &= 0xFFU;
    char csbuf[5];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    msg += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    frame.reserve(msg.size());
    for (char c : msg) frame.push_back(static_cast<std::byte>(c));
    return frame;
}

static std::vector<std::byte> make_logon(std::string_view bs, std::uint32_t seq, std::string_view s,
                                         std::string_view t, int hbt = 5) {
    std::string extra;
    extra += field(98, "0");
    extra += field(108, std::to_string(hbt));
    return make_fix_frame(bs, "A", seq, s, t, extra);
}

// Heartbeat with optional TestReqID(112).
static std::vector<std::byte> make_heartbeat(std::string_view bs, std::uint32_t seq,
                                             std::string_view s, std::string_view t,
                                             std::string_view testreqid = {}) {
    std::string extra;
    if (!testreqid.empty()) extra += field(112, testreqid);
    return make_fix_frame(bs, "0", seq, s, t, extra);
}

struct OutboundCapture {
    std::vector<std::vector<std::byte>> frames;
    void operator()(std::span<const std::byte> data) {
        frames.emplace_back(data.begin(), data.end());
    }
};

static bool is_msg_type(std::span<const std::byte> frame, std::string_view type) {
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    return wire.find("35=" + std::string(type) + "\x01") != std::string::npos;
}

static bool is_heartbeat(std::span<const std::byte> frame) { return is_msg_type(frame, "0"); }
static bool is_test_request(std::span<const std::byte> frame) { return is_msg_type(frame, "1"); }

}  // namespace

// ── Shared helpers for both executor modes ────────────────────────────────────

struct HeartbeatCadenceFixture {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine{};
    OutboundCapture capture;

    explicit HeartbeatCadenceFixture() {
        auto utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    fixpp::session::SessionConfig make_cfg(fixpp::session::threading_mode mode) {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = 5s;  // short for testing
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.transport_send = [this](std::span<const std::byte> d) { capture(d); };
        cfg.role = fixpp::session::session_role::acceptor;
        cfg.mode = mode;
        if (mode == fixpp::session::threading_mode::direct_executor)
            cfg.already_serialized_executor = true;
        // RC#C (gate-b/r1): bilateral_lenient — tests here don't exercise reset semantics.
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        return cfg;
    }

    bool drive_to_active(fixpp::session::Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        ioc.run_for(50ms);
        ioc.restart();
        if (!fut.get().has_value()) return false;
        auto logon = make_logon("FIX.4.2", 1, "TW", "ISLD", 5);
        auto f2 = asio::co_spawn(ioc, s.on_inbound_frame(logon), asio::use_future);
        ioc.run_for(50ms);
        ioc.restart();
        f2.get();
        return s.state() == fixpp::session::fsm_state::Active;
    }

    void advance_time(std::chrono::milliseconds delta) {
        clock->advance(delta);
        ioc.run_for(50ms);
        ioc.restart();
    }

    bool saw_heartbeat() const {
        for (const auto& f : capture.frames)
            if (is_heartbeat(f)) return true;
        return false;
    }

    bool saw_test_request() const {
        for (const auto& f : capture.frames)
            if (is_test_request(f)) return true;
        return false;
    }
};

// ── Cell A (strand): no outbound → Heartbeat emitted at HeartBtInt ─────────
TEST(HeartbeatCadence8Cell, CellA_Strand_NoOutboundHeartbeatEmitted) {
    HeartbeatCadenceFixture fix;
    auto cfg = fix.make_cfg(fixpp::session::threading_mode::per_session_strand);
    fixpp::session::Session sess(fix.engine, cfg);
    ASSERT_TRUE(fix.drive_to_active(sess)) << "Precondition: session must reach Active";

    // Advance clock beyond HeartBtInt (5s) with no outbound activity.
    fix.advance_time(6000ms);

    // RED: run_heartbeat_cadence stub co_returns {} immediately without arming
    // any timer. No Heartbeat is emitted → saw_heartbeat() returns false.
    EXPECT_TRUE(fix.saw_heartbeat())
        << "Cell A (strand): Heartbeat(0) must be emitted after 5s idle outbound. "
        << "RED: heartbeat cadence stub does not arm timer → FAILS RED per T018.";
}

// ── Cell A (direct): same axis, direct_executor mode ───────────────────────
TEST(HeartbeatCadence8Cell, CellA_Direct_NoOutboundHeartbeatEmitted) {
    HeartbeatCadenceFixture fix;
    auto cfg = fix.make_cfg(fixpp::session::threading_mode::direct_executor);
    fixpp::session::Session sess(fix.engine, cfg);
    ASSERT_TRUE(fix.drive_to_active(sess));

    fix.advance_time(6000ms);

    EXPECT_TRUE(fix.saw_heartbeat())
        << "Cell A (direct): Heartbeat(0) must be emitted after 5s idle outbound. "
        << "RED: FAILS RED per T018 design.";
}

// ── Cell B (strand): no inbound → TestRequest emitted at 1.2×HeartBtInt ────
TEST(HeartbeatCadence8Cell, CellB_Strand_NoInboundTestRequestEmitted) {
    HeartbeatCadenceFixture fix;
    auto cfg = fix.make_cfg(fixpp::session::threading_mode::per_session_strand);
    fixpp::session::Session sess(fix.engine, cfg);
    ASSERT_TRUE(fix.drive_to_active(sess));

    // Advance past 1.2 × 5s = 6s with no inbound traffic.
    fix.advance_time(7000ms);

    EXPECT_TRUE(fix.saw_test_request())
        << "Cell B (strand): TestRequest(1) must be emitted at 1.2×HeartBtInt (6s) "
        << "with no inbound traffic. RED: liveness watch stub does not arm timer → FAILS RED.";
}

// ── Cell B (direct): same axis, direct_executor mode ───────────────────────
TEST(HeartbeatCadence8Cell, CellB_Direct_NoInboundTestRequestEmitted) {
    HeartbeatCadenceFixture fix;
    auto cfg = fix.make_cfg(fixpp::session::threading_mode::direct_executor);
    fixpp::session::Session sess(fix.engine, cfg);
    ASSERT_TRUE(fix.drive_to_active(sess));

    fix.advance_time(7000ms);

    EXPECT_TRUE(fix.saw_test_request())
        << "Cell B (direct): TestRequest(1) must be emitted at 1.2×HeartBtInt. "
        << "RED: FAILS RED per T018 design.";
}

// ── Cell C (strand): unanswered TR → session_test_request_unanswered(74) ──
TEST(HeartbeatCadence8Cell, CellC_Strand_UnansweredTestRequestDisconnects) {
    HeartbeatCadenceFixture fix;
    auto cfg = fix.make_cfg(fixpp::session::threading_mode::per_session_strand);
    fixpp::session::Session sess(fix.engine, cfg);
    ASSERT_TRUE(fix.drive_to_active(sess));

    // Advance past 2 × HeartBtInt = 10s without any inbound reply.
    fix.advance_time(11000ms);

    // Session must transition to Disconnected.
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Disconnected)
        << "Cell C (strand): no TR reply within 2×HeartBtInt → Disconnected "
        << "(session_test_request_unanswered=74). RED: stub never times out → FAILS RED.";
}

// ── Cell C (direct): same axis, direct_executor mode ───────────────────────
TEST(HeartbeatCadence8Cell, CellC_Direct_UnansweredTestRequestDisconnects) {
    HeartbeatCadenceFixture fix;
    auto cfg = fix.make_cfg(fixpp::session::threading_mode::direct_executor);
    fixpp::session::Session sess(fix.engine, cfg);
    ASSERT_TRUE(fix.drive_to_active(sess));

    fix.advance_time(11000ms);

    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Disconnected)
        << "Cell C (direct): no TR reply within 2×HeartBtInt → Disconnected. "
        << "RED: FAILS RED per T018 design.";
}

// ── Cell D (strand): Heartbeat TestReqID mismatch → testreqid_mismatch(118) ─
TEST(HeartbeatCadence8Cell, CellD_Strand_TestReqIdMismatchDetected) {
    HeartbeatCadenceFixture fix;
    auto cfg = fix.make_cfg(fixpp::session::threading_mode::per_session_strand);
    fixpp::session::Session sess(fix.engine, cfg);
    ASSERT_TRUE(fix.drive_to_active(sess));

    // Advance to trigger TestRequest emission (>1.2×HeartBtInt).
    fix.advance_time(7000ms);

    // Now send a Heartbeat with a WRONG TestReqID.
    auto bad_hb = make_heartbeat("FIX.4.2", 2, "TW", "ISLD", "WRONG-ID-XYZ");
    auto fut = asio::co_spawn(fix.ioc, sess.on_inbound_frame(bad_hb), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(fix.ioc, fut, 50ms,
                                                    "CellD_Strand_TestReqIdMismatchDetected")) {
        fixpp::test_support::cancel_and_drain_or_report(fix.ioc, *clock,
                                                        "CellD_Strand_TestReqIdMismatchDetected");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss
                      << "CellD_Strand_TestReqIdMismatchDetected";
        return;
    }
    fut.get();

    // Session must disconnect (session_testreqid_mismatch=118).
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Disconnected)
        << "Cell D (strand): Heartbeat with wrong TestReqID → Disconnected "
        << "(session_testreqid_mismatch=118). RED: stub allows → FAILS RED per T018.";
}

// ── Cell D (direct): same axis, direct_executor mode ───────────────────────
TEST(HeartbeatCadence8Cell, CellD_Direct_TestReqIdMismatchDetected) {
    HeartbeatCadenceFixture fix;
    auto cfg = fix.make_cfg(fixpp::session::threading_mode::direct_executor);
    fixpp::session::Session sess(fix.engine, cfg);
    ASSERT_TRUE(fix.drive_to_active(sess));

    fix.advance_time(7000ms);

    auto bad_hb = make_heartbeat("FIX.4.2", 2, "TW", "ISLD", "WRONG-ID-XYZ");
    auto fut = asio::co_spawn(fix.ioc, sess.on_inbound_frame(bad_hb), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(fix.ioc, fut, 50ms,
                                                    "CellD_Direct_TestReqIdMismatchDetected")) {
        fixpp::test_support::cancel_and_drain_or_report(fix.ioc, *clock,
                                                        "CellD_Direct_TestReqIdMismatchDetected");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss
                      << "CellD_Direct_TestReqIdMismatchDetected";
        return;
    }
    fut.get();

    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Disconnected)
        << "Cell D (direct): Heartbeat wrong TestReqID → Disconnected. "
        << "RED: FAILS RED per T018 design.";
}
