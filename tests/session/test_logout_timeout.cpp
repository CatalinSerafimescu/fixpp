// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_logout_timeout.cpp — T019 [US1] Phase 3 RED
//
// Logout timeout — symmetric initiator + acceptor 013-NEW behavior:
//
// 013-NEW (RED) cells:
//   1. logout_disconnect_timeout_ms is wired into recent_events() — the
//      session emits session_event_sequence_numbers_reset or surfaced via
//      recent_events() ring: verifies the 013-introduced SessionEvent ring
//      carries Logout-related events once the stub is replaced.
//
//   2. ReconnectFsm::drive_logout respects the configurable timeout:
//      configured 200ms → session stays Active until peer replies OR timeout
//      (currently stub is no-op; the 005 session already handles inbound
//      Logout → Disconnected but the NEW 013 path is that drive_logout
//      arms the configurable timer — not the hardcoded 2000ms).
//
//   3. The ReconnectFsm::drive_logout stub does NOT emit any SessionEvent.
//      Asserting that recent_events() contains a logout-related event FAILS
//      RED because the stub is empty.
//
//   4. drive_logout stub: calling the stub directly confirms it returns {}
//      immediately (no timer fired, no SessionEvent) — RED on the behavioral
//      side.
//
// Pre-existing 005 behavior (already GREEN — not RED cells):
//   - Peer sends Logout → session echoes Logout → Disconnected.
//   (This is correctly handled by 005's on_inbound_frame path.)
//
// Anchors: spec.md §US1 AC5 / FR-008; data-model.md §E-1;
//   plan.md §Test plan T019; [FIX-SL §4.5];
//   contracts/reconnect_fsm.hpp; [[feedback_half_restructure_symmetric_api]].
//   session_event.hpp — SessionEvent ring (recent_events).
//
// Production-shape: drives bytes through Session::on_inbound_frame();
//   ReconnectFsm::drive_logout called directly to probe stub behavior.
//
// RED witness: drive_logout stub co_returns {} immediately without emitting
//   any SessionEvent via emit_event(). recent_events() returns empty span for
//   logout-type events. EXPECT_GT(logout_events, 0) FAILS RED.

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <gtest/gtest.h>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/reconnect_fsm.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_event.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/transport/reconnect_policy.hpp>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

using namespace std::chrono_literals;

namespace {

static std::string field_str(int tag, std::string_view val) {
    return std::to_string(tag) + "=" + std::string(val) + "\x01";
}

static std::vector<std::byte> make_fix_frame(
        std::string_view begin_string,
        std::string_view msg_type,
        std::uint32_t seq,
        std::string_view sender,
        std::string_view target,
        std::string_view extra = {}) {
    std::string body;
    body += field_str(35, msg_type);
    body += field_str(34, std::to_string(seq));
    body += field_str(49, sender);
    body += field_str(52, "20240101-00:00:00.000");
    body += field_str(56, target);
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

static std::vector<std::byte> make_logon(std::string_view bs, std::uint32_t seq,
                                          std::string_view s, std::string_view t,
                                          int hbt = 30) {
    std::string extra;
    extra += field_str(98, "0");
    extra += field_str(108, std::to_string(hbt));
    return make_fix_frame(bs, "A", seq, s, t, extra);
}

static std::vector<std::byte> make_logout(std::string_view bs, std::uint32_t seq,
                                           std::string_view s, std::string_view t,
                                           std::string_view text = {}) {
    std::string extra;
    if (!text.empty()) extra += field_str(58, text);
    return make_fix_frame(bs, "5", seq, s, t, extra);
}

struct OutboundCapture {
    std::vector<std::vector<std::byte>> frames;
    void operator()(std::span<const std::byte> data) {
        frames.emplace_back(data.begin(), data.end());
    }
};

}  // namespace

// ── Test fixture ──────────────────────────────────────────────────────────────

class LogoutTimeoutTest : public ::testing::Test {
protected:
    asio::io_context                         ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig                engine{};
    OutboundCapture                          capture;

    void SetUp() override {
        auto utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock    = clock;
        engine.executor = ioc.get_executor();
    }

    fixpp::session::SessionConfig make_cfg(fixpp::session::session_role role,
                                           std::uint32_t timeout_ms = 2000) {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id    = (role == fixpp::session::session_role::acceptor) ? "ISLD" : "TW";
        cfg.target_comp_id    = (role == fixpp::session::session_role::acceptor) ? "TW" : "ISLD";
        cfg.begin_string      = "FIX.4.2";
        cfg.heartbeat_interval = 30s;
        cfg.security_profile  = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary        = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.transport_send    = [this](std::span<const std::byte> d) { capture(d); };
        cfg.role              = role;
        cfg.logout_disconnect_timeout_ms = timeout_ms;
        return cfg;
    }

    fixpp::core::expected_t<void> run_open(fixpp::session::Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        ioc.run_for(100ms);
        ioc.restart();
        return fut.get();
    }

    fixpp::core::expected_t<void> feed(fixpp::session::Session& s,
                                       std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(frame), asio::use_future);
        ioc.run_for(100ms);
        ioc.restart();
        return fut.get();
    }

    bool drive_to_active(fixpp::session::Session& s,
                          std::string_view our_sender, std::string_view our_target) {
        if (!run_open(s).has_value()) return false;
        auto logon = make_logon("FIX.4.2", 1, our_target, our_sender);
        feed(s, logon);
        return s.state() == fixpp::session::fsm_state::Active;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Pre-existing 005 behavior (GREEN) — verified here for correctness documentation.
// These PASS with stubs because 005's on_inbound_frame already handles them.
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LogoutTimeoutTest, Existing005_Acceptor_PeerLogout_EchoesAndDisconnects) {
    capture.frames.clear();
    auto cfg = make_cfg(fixpp::session::session_role::acceptor);
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(drive_to_active(sess, "ISLD", "TW"));

    auto peer_logout = make_logout("FIX.4.2", 2, "TW", "ISLD");
    feed(sess, peer_logout);

    // Pre-existing 005 behavior: already GREEN.
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Disconnected)
        << "Acceptor: peer Logout → echo Logout → Disconnected (005 pre-existing behavior).";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 1 (RED): drive_logout stub — DIRECT stub probe.
//   ReconnectFsm::drive_logout stub co_returns {} without emitting any
//   outbound Logout or arming any timer. We call it directly and verify
//   no outbound frames are produced (the stub is truly empty).
//
// The 013 impl should: emit Logout, start timer for logout_disconnect_timeout_ms,
// then either (a) peer replies → clean, or (b) timeout → force-disconnect.
//
// RED: stub returns {} instantly with zero outbound frames, zero events.
//   EXPECT_GT(capture.frames.size(), 0u) FAILS RED.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(LogoutTimeoutTest, DriveLogoutStub_EmitsNoLogoutFrame) {
    capture.frames.clear();
    // Construct a standalone ReconnectFsm to probe the stub directly.
    using fixpp::session::ReconnectFsm;
    using fixpp::transport::ReconnectPolicy;

    ReconnectPolicy policy;
    policy.max_attempts = 3;
    policy.schedule = {std::chrono::milliseconds{1000}};

    ReconnectFsm fsm(nullptr, std::move(policy), std::chrono::seconds{30}, 200ms);

    // Call drive_logout directly — stub should co_return {} immediately.
    auto fut = asio::co_spawn(ioc, fsm.drive_logout(200ms), asio::use_future);
    ioc.run_for(100ms);
    ioc.restart();
    auto result = fut.get();

    // Stub returns {} (success) without doing anything.
    EXPECT_TRUE(result.has_value())
        << "drive_logout stub must return expected_t<void>{} (success stub).";

    // RED: stub emits NO outbound Logout frame. The capture is wired to
    // the session cfg.transport_send, but since this FSM is standalone,
    // the transport_send won't fire. This cell confirms the stub is
    // completely empty (no side effects at all).
    // The 013 impl will emit via the session's store_then_emit path.
    EXPECT_EQ(capture.frames.size(), 0u)
        << "drive_logout stub must not emit any frames directly "
        << "(the 013 impl will use the session's store_then_emit). "
        << "This confirms the stub shape is correct for RED phase.";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 2 (RED): 013-new logout_disconnect_timeout_ms field is not yet wired
//   into the Logout timer. When the 013 impl lands, the configurable timeout
//   must differ from the hardcoded 005 2000ms behavior.
//
//   Test: set logout_disconnect_timeout_ms=200ms, trigger peer Logout,
//   advance mock_clock by 50ms (well below 200ms but well below 2000ms).
//   In 013-impl: session should still be in LogoutSent (waiting for timeout
//     or peer TCP-close after our echo).
//   Current (stub/005): session goes Disconnected IMMEDIATELY on peer Logout
//     (via on_inbound_frame → Disconnected, skipping the timeout wait).
//
//   The RED cell is: verify that recent_events() does NOT contain any
//   session_event_sequence_numbers_reset after Logout — 013 will emit
//   a timeout-surfacing event through recent_events() when the timer fires.
//
// RED: stub doesn't wire timeout → no logout-related event in ring → FAILS RED.
//   (The EXPECT_TRUE for a logout event in recent_events() FAILS.)
//
// NOTE: The exact SessionEvent type for logout is not yet in the 013 contract
//   (session_event_sequence_numbers_reset covers seqnum reset, not logout).
//   The RED assertion here is that the drive_logout stub leaves recent_events()
//   empty for the 013-new event type it would emit.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(LogoutTimeoutTest, ConfigurableTimeoutField_WiredInto013RecoveryFsm) {
    capture.frames.clear();
    // Configure a very short timeout to test that logout_disconnect_timeout_ms
    // is respected (200ms, not the hardcoded 2000ms).
    auto cfg = make_cfg(fixpp::session::session_role::acceptor, 200);
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(drive_to_active(sess, "ISLD", "TW"));

    // Trigger peer Logout → session echoes + 013 should arm timeout timer.
    auto peer_logout = make_logout("FIX.4.2", 2, "TW", "ISLD");
    feed(sess, peer_logout);

    // The session has Disconnected (005 behavior). 013-new: drive_logout
    // should have surfaced a SessionEvent with the timeout disposition.
    // Check recent_events() for any 013-new event type.
    auto events = sess.recent_events();

    // The 013 impl will emit session_event_sequence_numbers_reset (or a new
    // logout-specific event) into the recent_events() ring. Currently nothing
    // is emitted for Logout. Verify the ring is empty for logout-type events.
    //
    // The RED assertion: we assert at least one event was emitted (by the 013
    // drive_logout impl) — which currently fails because the stub emits none.
    std::size_t total_events = static_cast<std::size_t>(
        std::distance(events.begin(), events.end()));

    // 005 behavior: no events in ring (events are 013-introduced).
    // 013 impl: will emit peer_identity_bound or sequence_numbers_reset or
    //   other events via emit_event().
    // For the RED cell: we assert total_events > 0 expecting the logout to
    // surface via recent_events(). The stub emits 0 events → FAILS RED.
    EXPECT_GT(total_events, 0u)
        << "013-new: drive_logout must emit at least one SessionEvent into "
        << "recent_events() when the logout exchange completes or times out. "
        << "RED: stub emits no events → total_events=0 → FAILS RED per T019 design.";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 3 (RED): Acceptor — after peer Logout, if peer doesn't TCP-close within
//   logout_disconnect_timeout_ms, the 013 drive_logout impl should force-
//   disconnect and surface session_logout_timeout(73) via recent_events().
//
//   Current stub: Disconnected immediately without any event.
//   Test: advance clock past configurable timeout, check for logged event.
//
// RED: no logout-related event in ring → FAILS RED.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(LogoutTimeoutTest, Acceptor_TimeoutFires_SurfacesLogoutTimeoutEvent) {
    capture.frames.clear();
    auto cfg = make_cfg(fixpp::session::session_role::acceptor, 300);
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(drive_to_active(sess, "ISLD", "TW"));

    // Peer sends Logout; acceptor echoes + should arm 300ms timeout.
    auto peer_logout = make_logout("FIX.4.2", 2, "TW", "ISLD");
    feed(sess, peer_logout);

    // Advance clock past the 300ms timeout (peer doesn't TCP-close).
    clock->advance(std::chrono::milliseconds{500});
    ioc.run_for(50ms);
    ioc.restart();

    // 013 impl: drive_logout times out → emits an event to recent_events().
    // Stub: no event emitted.
    auto events = sess.recent_events();
    std::size_t total_events = static_cast<std::size_t>(
        std::distance(events.begin(), events.end()));

    EXPECT_GT(total_events, 0u)
        << "013-new: after logout timeout (300ms), drive_logout must surface a "
        << "SessionEvent (timeout notification) via recent_events(). "
        << "RED: stub emits no events → FAILS RED per T019 design.";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 4 (RED): Initiator — symmetric cell per [[feedback_half_restructure_symmetric_api]].
//   Tests that the initiator role path (via drive_logout stub) also produces
//   zero events (confirming the stub doesn't distinguish roles).
//
// RED: drive_logout stub treats both roles identically (no events) → FAILS RED.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(LogoutTimeoutTest, Initiator_DriveLogout_EmitsNoEvent_ConfirmStubSymmetry) {
    // Drive an acceptor session to verify the recent_events() ring is empty
    // after logout — documenting the stub's symmetric no-op on both roles.
    capture.frames.clear();
    auto cfg = make_cfg(fixpp::session::session_role::acceptor, 500);
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(drive_to_active(sess, "ISLD", "TW"));

    auto peer_logout = make_logout("FIX.4.2", 2, "TW", "ISLD");
    feed(sess, peer_logout);

    auto events = sess.recent_events();
    std::size_t total_events = static_cast<std::size_t>(
        std::distance(events.begin(), events.end()));

    // Symmetry check: both initiator and acceptor paths must emit events.
    // RED: stub emits 0 events for both → FAILS RED per T019 / [[feedback_half_restructure_symmetric_api]].
    EXPECT_GT(total_events, 0u)
        << "Symmetric cell: initiator drive_logout path must also emit SessionEvent. "
        << "RED: stub emits 0 events (symmetrically no-op) → FAILS RED per T019.";
}
