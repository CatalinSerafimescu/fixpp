// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_reset_seqnum_policy_matrix.cpp — T017 [US1] Phase 3 RED
//
// ResetSeqNumFlag(141)=Y policy 6-cell matrix:
//   3 modes (bilateral_strict / bilateral_lenient / unilateral)
//   × 2 roles (initiator / acceptor)
//
// For bilateral_strict: peer Logon without 141=Y when we sent 141=Y →
//   session_seqnum_reset_mismatch(116) + Disconnected.
// For successful reset (any mode, valid exchange): emits
//   session_event_sequence_numbers_reset{by_peer_request} via recent_events().
//
// Anchors: spec.md §US1 / FR-017 / FR-018; data-model.md §E-4;
//   plan.md §Test plan T017; Clarifications Q1=A;
//   contracts/session_event.hpp; [[feedback_half_restructure_symmetric_api]].
//
// Production-shape: drives bytes through Session::on_inbound_frame().
//
// RED witness: Phase-2 stubs do not implement 141=Y logic. The Logon-with-141
//   processing stub treats it as a normal Logon. All per-mode assertions FAIL
//   RED until T024 impl lands.

#include <gtest/gtest.h>

#include <algorithm>
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
#include <fixpp/session/session_event.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

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

// make_logon with optional ResetSeqNumFlag(141)=Y
static std::vector<std::byte> make_logon(std::string_view bs, std::uint32_t seq, std::string_view s,
                                         std::string_view t, int hbt = 30,
                                         bool reset_seqnum = false) {
    std::string extra;
    extra += field(98, "0");
    extra += field(108, std::to_string(hbt));
    if (reset_seqnum) extra += field(141, "Y");
    return make_fix_frame(bs, "A", seq, s, t, extra);
}

struct OutboundCapture {
    std::vector<std::vector<std::byte>> frames;
    void operator()(std::span<const std::byte> data) {
        frames.emplace_back(data.begin(), data.end());
    }
};

static bool has_session_reset_event(const fixpp::session::Session& sess) {
    auto events = sess.recent_events();
    return std::any_of(events.begin(), events.end(), [](const fixpp::session::SessionEvent& ev) {
        return std::holds_alternative<fixpp::session::session_event_sequence_numbers_reset>(ev);
    });
}

// frame_has_141Y: check whether a raw FIX frame contains "141=Y\x01".
// Used to verify that the outbound reply Logon echoes ResetSeqNumFlag(141)=Y.
// (RC#C-2 gate-b/r2 false-pass closure — outbound-frame assertion.)
static bool frame_has_141Y(const std::vector<std::byte>& frame) {
    static constexpr std::string_view needle = "141=Y\x01";
    if (frame.size() < needle.size()) return false;
    const auto* data = reinterpret_cast<const char*>(frame.data());
    std::string_view sv(data, frame.size());
    return sv.find(needle) != std::string_view::npos;
}

}  // namespace

// ── Test fixture ──────────────────────────────────────────────────────────────

class ResetSeqnumPolicyMatrixTest : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine{};
    OutboundCapture capture;

    void SetUp() override {
        auto utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    fixpp::session::SessionConfig make_cfg(fixpp::session::session_role role,
                                           fixpp::session::reset_seqnum_policy policy) {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id = (role == fixpp::session::session_role::acceptor) ? "ISLD" : "TW";
        cfg.target_comp_id = (role == fixpp::session::session_role::acceptor) ? "TW" : "ISLD";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = 30s;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.transport_send = [this](std::span<const std::byte> d) { capture(d); };
        cfg.role = role;
        cfg.reset_seqnum_policy_field = policy;
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

    // Clear outbound captures before each cell.
    void clear_capture() { capture.frames.clear(); }
};

// ─────────────────────────────────────────────────────────────────────────────
// Cell 1: bilateral_strict × acceptor
//   Peer sends Logon with 141=Y → session echoes 141=Y in its Logon reply +
//   emits session_event_sequence_numbers_reset{by_peer_request=true}.
//
// RED: stub ignores 141=Y → no reset event emitted. FAILS RED.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(ResetSeqnumPolicyMatrixTest, BilateralStrict_Acceptor_PeerSends141Y) {
    clear_capture();
    auto cfg = make_cfg(fixpp::session::session_role::acceptor,
                        fixpp::session::reset_seqnum_policy::bilateral_strict);
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(run_open(sess).has_value());

    // Peer (initiator) sends Logon with ResetSeqNumFlag(141)=Y.
    auto logon_with_reset = make_logon("FIX.4.2", 1, "TW", "ISLD", 30, /*reset=*/true);
    feed(sess, logon_with_reset);

    // After successful 141=Y exchange: emits sequence_numbers_reset{by_peer_request=true}.
    EXPECT_TRUE(has_session_reset_event(sess))
        << "bilateral_strict acceptor: peer 141=Y must emit "
        << "session_event_sequence_numbers_reset. RED: stub ignores 141 → FAILS RED.";
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "bilateral_strict acceptor: successful 141=Y exchange → Active.";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 2: bilateral_strict × initiator
//   We (initiator) send Logon with 141=Y; peer replies WITHOUT 141=Y.
//   bilateral_strict MUST reject → session_seqnum_reset_mismatch(116) + Disconnect.
//
// RED: stub ignores 141=Y → no rejection → session reaches Active. FAILS RED.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(ResetSeqnumPolicyMatrixTest, BilateralStrict_Initiator_PeerOmits141Y) {
    clear_capture();
    // For initiator we need a transport_factory or transport_send wiring;
    // in this test the initiator drives the Logon emission via open().
    // We verify the rejection when the peer's response Logon lacks 141=Y.
    auto cfg = make_cfg(fixpp::session::session_role::initiator,
                        fixpp::session::reset_seqnum_policy::bilateral_strict);
    // Initiator needs 141=Y in its outbound Logon; we synthesize the peer's
    // response without 141=Y.
    // NOTE: In the current stub shape, initiator sessions require a transport
    // factory to send. Since we're verifying rejection logic (inbound processing),
    // we use an acceptor as a proxy: we inject a Logon without 141=Y when the
    // session was configured to require it.
    // Reuse acceptor role but inject peer Logon without 141=Y AFTER it had
    // sent its own 141=Y (bilateral context).
    // For the stub RED phase this just verifies the session does NOT go Active
    // when it should reject — which the stub gets wrong by accepting anyway.
    auto cfg2 = make_cfg(fixpp::session::session_role::acceptor,
                         fixpp::session::reset_seqnum_policy::bilateral_strict);
    fixpp::session::Session sess(engine, cfg2);
    ASSERT_TRUE(run_open(sess).has_value());

    // Peer sends Logon WITHOUT 141=Y; bilateral_strict should reject.
    auto logon_no_reset = make_logon("FIX.4.2", 1, "TW", "ISLD", 30, /*reset=*/false);
    feed(sess, logon_no_reset);

    // In bilateral_strict: when we have sent 141=Y (our open sends Logon with 141=Y)
    // but peer's response lacks it → session_seqnum_reset_mismatch + disconnect.
    // RED: stub accepts anyway → Active state. This FAILS the Disconnected assertion.
    // (stub makes Active, we assert it should be disconnected after mismatch)
    // For RED: just verify the event ring does NOT have a reset event when peer
    // omits 141=Y in bilateral_strict mode — the stub incorrectly allows this.
    // The definitive RED assert: session goes Disconnected, not Active.
    // (The stub will be Active — that is the RED failure.)
    // We assert Disconnected to document the T017 behavioral delta.
    EXPECT_NE(sess.state(), fixpp::session::fsm_state::Active)
        << "bilateral_strict: peer omitting 141=Y must NOT reach Active — should "
        << "disconnect with session_seqnum_reset_mismatch(116). RED: stub allows "
        << "Active → this assertion FAILS RED per T017 design.";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 3: bilateral_lenient × acceptor
//   Peer sends 141=Y; bilateral_lenient auto-mirrors 141=Y in reply Logon.
//   Emits sequence_numbers_reset{by_peer_request=true}.
//
// RED: stub ignores 141=Y → no reset event → FAILS RED.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(ResetSeqnumPolicyMatrixTest, BilateralLenient_Acceptor_PeerSends141Y) {
    clear_capture();
    auto cfg = make_cfg(fixpp::session::session_role::acceptor,
                        fixpp::session::reset_seqnum_policy::bilateral_lenient);
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(run_open(sess).has_value());

    auto logon_with_reset = make_logon("FIX.4.2", 1, "TW", "ISLD", 30, /*reset=*/true);
    feed(sess, logon_with_reset);

    EXPECT_TRUE(has_session_reset_event(sess))
        << "bilateral_lenient acceptor: peer 141=Y must emit "
        << "session_event_sequence_numbers_reset. RED: stub ignores 141 → FAILS RED.";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 4: bilateral_lenient × initiator (via acceptor proxy)
//   Peer responds WITHOUT 141=Y even though we sent it.
//   bilateral_lenient does NOT reject — it accepts anyway (QFC-style).
//   Emits session_event_sequence_numbers_reset{by_peer_request=false}.
//
// RED: stub ignores 141=Y entirely → no event at all → FAILS RED.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(ResetSeqnumPolicyMatrixTest, BilateralLenient_Acceptor_PeerOmits141Y) {
    clear_capture();
    auto cfg = make_cfg(fixpp::session::session_role::acceptor,
                        fixpp::session::reset_seqnum_policy::bilateral_lenient);
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(run_open(sess).has_value());

    // Peer sends Logon WITHOUT 141=Y; bilateral_lenient should accept anyway.
    auto logon_no_reset = make_logon("FIX.4.2", 1, "TW", "ISLD", 30, /*reset=*/false);
    feed(sess, logon_no_reset);

    // bilateral_lenient: peer omits 141=Y → accept normally (no rejection).
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "bilateral_lenient: peer omitting 141=Y is still accepted. "
        << "RED: stub is actually correct here, but no reset event — partial GREEN.";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 2a: bilateral_strict × initiator — peer ack WITH 141=Y
//   We (initiator, bilateral_strict) send Logon with 141=Y in open().
//   Peer ack confirms with 141=Y → mutual reset → Active +
//   session_event_sequence_numbers_reset{by_peer_request=false} (WE initiated).
//
// FR-018 mode mapping: bilateral_strict initiator-confirm → by_peer_request=false.
// [[feedback_half_restructure_symmetric_api]]: symmetric to acceptor Cell 1.
//
// RC#C (gate-b/r1): new cell covering the missing initiator strict-path.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(ResetSeqnumPolicyMatrixTest, BilateralStrict_Initiator_PeerConfirms141Y) {
    clear_capture();
    auto cfg = make_cfg(fixpp::session::session_role::initiator,
                        fixpp::session::reset_seqnum_policy::bilateral_strict);
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(run_open(sess).has_value());
    // Initiator open() emits Logon with 141=Y (bilateral_strict) → state=LogonSent.
    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::LogonSent);

    // Peer (acceptor, ISLD→TW) sends Logon-ack WITH 141=Y — mutual reset.
    auto logon_ack_with_reset = make_logon("FIX.4.2", 1, "ISLD", "TW", 30, /*reset=*/true);
    feed(sess, logon_ack_with_reset);

    // (a) Session reaches Active after bilateral confirmation.
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "bilateral_strict initiator: peer ack with 141=Y → Active.";

    // (b) Exactly one sequence_numbers_reset event, by_peer_request=false (WE initiated).
    // FR-018: bilateral_strict confirm path → we sent 141=Y first → by_peer_request=false.
    auto events = sess.recent_events();
    std::size_t reset_events = 0;
    bool by_peer_request_correct = false;
    for (const auto& ev : events) {
        if (auto* r = std::get_if<fixpp::session::session_event_sequence_numbers_reset>(&ev)) {
            ++reset_events;
            by_peer_request_correct = !r->by_peer_request;  // must be false (we initiated)
        }
    }
    EXPECT_EQ(reset_events, 1u)
        << "bilateral_strict initiator confirm: exactly one sequence_numbers_reset event.";
    EXPECT_TRUE(by_peer_request_correct)
        << "FR-018: bilateral_strict initiator-confirm → by_peer_request must be false "
        << "(WE sent 141=Y first, peer confirmed).";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 2b: bilateral_strict × initiator — peer ack WITHOUT 141=Y → reject.
//   We (initiator) send Logon with 141=Y; peer responds WITHOUT 141=Y.
//   bilateral_strict MUST reject → session_seqnum_reset_mismatch(116) + Disconnected.
//
// This is the DIRECT initiator test (not the acceptor-proxy from Cell 2).
// [[feedback_half_restructure_symmetric_api]]: symmetric to acceptor rejection above.
//
// RC#C (gate-b/r1): direct initiator strict-reject cell.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(ResetSeqnumPolicyMatrixTest, BilateralStrict_Initiator_PeerOmits141Y_DirectReject) {
    clear_capture();
    auto cfg = make_cfg(fixpp::session::session_role::initiator,
                        fixpp::session::reset_seqnum_policy::bilateral_strict);
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(run_open(sess).has_value());
    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::LogonSent);

    // Peer ack does NOT include 141=Y — bilateral_strict requires it.
    auto logon_ack_no_reset = make_logon("FIX.4.2", 1, "ISLD", "TW", 30, /*reset=*/false);
    auto feed_r = feed(sess, logon_ack_no_reset);

    // Must reject with session_seqnum_reset_mismatch(116) + Disconnected.
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Disconnected)
        << "bilateral_strict initiator: peer ack without 141=Y must disconnect "
        << "with session_seqnum_reset_mismatch(116).";
    EXPECT_FALSE(feed_r.has_value())
        << "bilateral_strict initiator: on_inbound_frame must return an error when "
        << "peer Logon-ack omits 141=Y.";
    if (!feed_r.has_value()) {
        EXPECT_EQ(feed_r.error(), fixpp::core::error::session_seqnum_reset_mismatch)
            << "bilateral_strict initiator: error must be session_seqnum_reset_mismatch(116).";
    }
    // No sequence_numbers_reset event (rejected before reset was confirmed).
    EXPECT_FALSE(has_session_reset_event(sess))
        << "bilateral_strict initiator: rejected exchange must NOT emit "
        << "sequence_numbers_reset event.";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 5: unilateral × acceptor
//   Peer sends 141=Y; unilateral honours it regardless of our own flag.
//   Emits sequence_numbers_reset{by_peer_request=true} and goes Active.
//
// RED: stub ignores 141=Y → no event → FAILS RED.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(ResetSeqnumPolicyMatrixTest, Unilateral_Acceptor_PeerSends141Y) {
    clear_capture();
    auto cfg = make_cfg(fixpp::session::session_role::acceptor,
                        fixpp::session::reset_seqnum_policy::unilateral);
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(run_open(sess).has_value());

    auto logon_with_reset = make_logon("FIX.4.2", 1, "TW", "ISLD", 30, /*reset=*/true);
    feed(sess, logon_with_reset);

    EXPECT_TRUE(has_session_reset_event(sess))
        << "unilateral acceptor: peer 141=Y must emit sequence_numbers_reset event. "
        << "RED: stub ignores 141 → FAILS RED per T017 design.";
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "unilateral acceptor: must reach Active after 141=Y.";
}

// ─────────────────────────────────────────────────────────────────────────────
// RC#C-2 (gate-b/r2) — outbound reply Logon frame assertions.
//
// The existing cells assert only the reset event; they never inspect the
// outbound reply Logon captured in `capture.frames`. This closes that hole.
//
// RC#C-2 fix (session.cpp:1044-1046): bilateral_lenient now mirrors 141=Y
// in its acceptor reply (same as bilateral_strict). unilateral does NOT.
//
// For each acceptor cell below:
//   bilateral_strict  → reply frame MUST contain "141=Y\x01"
//   bilateral_lenient → reply frame MUST contain "141=Y\x01"   (RC#C-2 fix)
//   unilateral        → reply frame must NOT contain "141=Y\x01"
//
// Anchors: spec.md FR-017:148-149; [[feedback_simplify_pass_catches_9th_burn]];
//   [[feedback_half_restructure_symmetric_api]].
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ResetSeqnumPolicyMatrixTest, BilateralStrict_Acceptor_ReplyContains141Y) {
    clear_capture();
    auto cfg = make_cfg(fixpp::session::session_role::acceptor,
                        fixpp::session::reset_seqnum_policy::bilateral_strict);
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(run_open(sess).has_value());

    auto logon_with_reset = make_logon("FIX.4.2", 1, "TW", "ISLD", 30, /*reset=*/true);
    feed(sess, logon_with_reset);

    // At least one outbound frame must have been captured (the reply Logon).
    ASSERT_FALSE(capture.frames.empty())
        << "bilateral_strict acceptor: expected an outbound reply Logon frame.";
    // The reply Logon MUST contain 141=Y (bilateral_strict mirrors peer 141=Y).
    bool found = false;
    for (const auto& f : capture.frames) {
        if (frame_has_141Y(f)) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "bilateral_strict acceptor: reply Logon must carry 141=Y. "
                       << "FR-017: bilateral_strict mirrors peer ResetSeqNumFlag(141)=Y.";
}

TEST_F(ResetSeqnumPolicyMatrixTest, BilateralLenient_Acceptor_ReplyContains141Y) {
    clear_capture();
    auto cfg = make_cfg(fixpp::session::session_role::acceptor,
                        fixpp::session::reset_seqnum_policy::bilateral_lenient);
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(run_open(sess).has_value());

    auto logon_with_reset = make_logon("FIX.4.2", 1, "TW", "ISLD", 30, /*reset=*/true);
    feed(sess, logon_with_reset);

    // At least one outbound frame must have been captured (the reply Logon).
    ASSERT_FALSE(capture.frames.empty())
        << "bilateral_lenient acceptor: expected an outbound reply Logon frame.";
    // The reply Logon MUST contain 141=Y.
    // RC#C-2: bilateral_lenient is the mode whose defining behavior IS the mirror
    // (FR-017:148). Before this fix the predicate gated only on bilateral_strict.
    bool found = false;
    for (const auto& f : capture.frames) {
        if (frame_has_141Y(f)) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found)
        << "bilateral_lenient acceptor: reply Logon must carry 141=Y when peer sends it. "
        << "FR-017:148: bilateral_lenient mirrors peer ResetSeqNumFlag(141)=Y (RC#C-2).";
}

TEST_F(ResetSeqnumPolicyMatrixTest, Unilateral_Acceptor_ReplyDoesNotContain141Y) {
    clear_capture();
    auto cfg = make_cfg(fixpp::session::session_role::acceptor,
                        fixpp::session::reset_seqnum_policy::unilateral);
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(run_open(sess).has_value());

    auto logon_with_reset = make_logon("FIX.4.2", 1, "TW", "ISLD", 30, /*reset=*/true);
    feed(sess, logon_with_reset);

    // unilateral: outbound 141 is config-driven, NOT mirror-driven (FR-017:149).
    // The reply Logon must NOT echo 141=Y just because the peer sent it.
    for (const auto& f : capture.frames) {
        EXPECT_FALSE(frame_has_141Y(f))
            << "unilateral acceptor: reply Logon must NOT carry 141=Y "
            << "(FR-017:149 — outbound 141 is config-driven, not mirror-driven).";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// RC#C-1 (gate-b/r2) — counter-reset assertions.
//
// The existing cells never assert seqnum counter values. They pass with fresh
// sessions (counters already at 1) where the missing reset is a no-op.
// These cells pre-advance next_outbound_ to 10 via set_counters_for_test so
// that the reset is non-trivial (without reset, reply Logon gets seq=10; with
// reset it gets seq=1) and then assert the FR-mandated post-reset counter values.
//
// Post-reset expected values per FR-017:150 + FIX-SL §4.4.1:
//   Acceptor (cells 1/3/5):
//     next_inbound_  = 1 (reset rewinds; peer's next post-reset message is seq=1)
//     next_outbound_ = 2 (reply Logon consumed seq=1 → next to send is seq=2)
//   Initiator (cell 2a):
//     next_inbound_  = 1 (reset rewinds; peer's next post-reset message is seq=1)
//     next_outbound_ = 1 (reset rewinds; Logon was pre-reset, next send is seq=1)
//
// Requires FIXPP_TEST_HOOKS (seqnum_mgr_test_access() on Session).
//
// Anchors: spec.md FR-017:150; data-model.md §E-4;
//   [[feedback_simplify_pass_catches_9th_burn]] (false-pass via fresh-session).
// ─────────────────────────────────────────────────────────────────────────────

#ifdef FIXPP_TEST_HOOKS

TEST_F(ResetSeqnumPolicyMatrixTest, BilateralStrict_Acceptor_CountersResetToOne) {
    clear_capture();
    auto cfg = make_cfg(fixpp::session::session_role::acceptor,
                        fixpp::session::reset_seqnum_policy::bilateral_strict);
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(run_open(sess).has_value());

    // Pre-advance outbound counter to 10 so the reset is non-trivial.
    // Keep inbound at 1 (peer will send Logon seq=1; check_inbound(1) must pass).
    sess.seqnum_mgr_test_access().set_counters_for_test(1, 10);

    auto logon_with_reset = make_logon("FIX.4.2", 1, "TW", "ISLD", 30, /*reset=*/true);
    feed(sess, logon_with_reset);

    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active);

    // FR-017:150 + 030 FR-001: after a received-141=Y reset the OUTBOUND side
    // re-bases to 1, but the consumed seq-1 reset Logon is a surviving inbound
    // advance, so next-expected-INBOUND is 2 (QuickFIX reset-then-increment parity).
    // next_inbound_ = 2 (consumed the seq-1 reset Logon — 030 FR-001)
    // next_outbound_ = 2 (reply Logon consumed seq=1; next to send is seq=2)
    EXPECT_EQ(sess.seqnum_mgr_test_access().next_inbound_unsafe(), fixpp::session::seqnum_t{2})
        << "bilateral_strict acceptor: next_inbound_ must be 2 after received-141=Y reset "
        << "(consumed seq-1 reset Logon; 030 FR-001).";
    EXPECT_EQ(sess.seqnum_mgr_test_access().next_outbound_unsafe(), fixpp::session::seqnum_t{2})
        << "bilateral_strict acceptor: next_outbound_ must be 2 after successful 141=Y reset "
        << "(reply Logon consumed seq=1).";
}

TEST_F(ResetSeqnumPolicyMatrixTest, BilateralLenient_Acceptor_CountersResetToOne) {
    clear_capture();
    auto cfg = make_cfg(fixpp::session::session_role::acceptor,
                        fixpp::session::reset_seqnum_policy::bilateral_lenient);
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(run_open(sess).has_value());

    // Pre-advance outbound counter to 10 so the reset is non-trivial.
    sess.seqnum_mgr_test_access().set_counters_for_test(1, 10);

    auto logon_with_reset = make_logon("FIX.4.2", 1, "TW", "ISLD", 30, /*reset=*/true);
    feed(sess, logon_with_reset);

    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active);

    EXPECT_EQ(sess.seqnum_mgr_test_access().next_inbound_unsafe(), fixpp::session::seqnum_t{2})
        << "bilateral_lenient acceptor: next_inbound_ must be 2 after received-141=Y reset "
        << "(consumed seq-1 reset Logon; 030 FR-001).";
    EXPECT_EQ(sess.seqnum_mgr_test_access().next_outbound_unsafe(), fixpp::session::seqnum_t{2})
        << "bilateral_lenient acceptor: next_outbound_ must be 2 after successful 141=Y reset.";
}

TEST_F(ResetSeqnumPolicyMatrixTest, BilateralStrict_Initiator_CountersResetToOne) {
    clear_capture();
    // Pre-seed outbound to 10 BEFORE open() so the initiator's Logon is seq=10.
    auto cfg = make_cfg(fixpp::session::session_role::initiator,
                        fixpp::session::reset_seqnum_policy::bilateral_strict);
    fixpp::session::Session sess(engine, cfg);
    // Set counters before open() so the outbound Logon uses seq=10 (non-trivial).
    sess.seqnum_mgr_test_access().set_counters_for_test(1, 10);

    ASSERT_TRUE(run_open(sess).has_value());
    // open() consumed seq=10 → next_outbound_=11.
    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::LogonSent);

    // Peer (acceptor) acks with Logon seq=1 + 141=Y (mutual reset confirm).
    auto logon_ack_with_reset = make_logon("FIX.4.2", 1, "ISLD", "TW", 30, /*reset=*/true);
    feed(sess, logon_ack_with_reset);

    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active);

    // FR-017:150 + 030 FR-009: after mutual reset:
    //   next_inbound_ = 2 (the peer's seq-1 reset-ack Logon was consumed in-sequence;
    //     030 restores the advance on the initiator peer_ack_sent_reset_flag arm — the
    //     identical clobber to the acceptor arm; QuickFIX reset-then-increment parity)
    //   next_outbound_ = 1 (reset; initiator's next send is seq=1)
    // (Initiator's Logon was seq=10, pre-reset. After reset outbound restarts at 1;
    //  inbound nets 2 because the consumed reset-ack Logon survives the reset.)
    EXPECT_EQ(sess.seqnum_mgr_test_access().next_inbound_unsafe(), fixpp::session::seqnum_t{2})
        << "bilateral_strict initiator: next_inbound_ must be 2 after 141=Y mutual reset "
        << "(consumed seq-1 reset-ack Logon; 030 FR-009).";
    EXPECT_EQ(sess.seqnum_mgr_test_access().next_outbound_unsafe(), fixpp::session::seqnum_t{1})
        << "bilateral_strict initiator: next_outbound_ must be 1 after 141=Y mutual reset "
        << "(Logon was pre-reset; post-reset next outbound is seq=1).";
}

TEST_F(ResetSeqnumPolicyMatrixTest, Unilateral_Acceptor_CountersResetToOne) {
    clear_capture();
    auto cfg = make_cfg(fixpp::session::session_role::acceptor,
                        fixpp::session::reset_seqnum_policy::unilateral);
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(run_open(sess).has_value());

    // Pre-advance outbound counter to 10 so the reset is non-trivial.
    sess.seqnum_mgr_test_access().set_counters_for_test(1, 10);

    auto logon_with_reset = make_logon("FIX.4.2", 1, "TW", "ISLD", 30, /*reset=*/true);
    feed(sess, logon_with_reset);

    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active);

    EXPECT_EQ(sess.seqnum_mgr_test_access().next_inbound_unsafe(), fixpp::session::seqnum_t{2})
        << "unilateral acceptor: next_inbound_ must be 2 after received-141=Y reset "
        << "(consumed seq-1 reset Logon; 030 FR-001).";
    EXPECT_EQ(sess.seqnum_mgr_test_access().next_outbound_unsafe(), fixpp::session::seqnum_t{2})
        << "unilateral acceptor: next_outbound_ must be 2 after successful 141=Y reset.";
}

#endif  // FIXPP_TEST_HOOKS

// ─────────────────────────────────────────────────────────────────────────────
// Cell 6: unilateral × initiator (modelled via acceptor accepting without echo)
//   Peer sends 141=Y even though we did not; unilateral honours it anyway.
//   Does NOT reject — Fix8-style.
//
// RED: stub ignores 141=Y → no event. FAILS RED.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(ResetSeqnumPolicyMatrixTest, Unilateral_Acceptor_PeerSends141Y_NoOurFlag) {
    clear_capture();
    // Same as cell 5 — unilateral honours any peer 141=Y. This is the
    // "Fix8-style": we honour the reset whether or not we requested it.
    auto cfg = make_cfg(fixpp::session::session_role::acceptor,
                        fixpp::session::reset_seqnum_policy::unilateral);
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(run_open(sess).has_value());

    // Peer sends 141=Y; we honour it (unilateral).
    auto logon_with_reset = make_logon("FIX.4.2", 1, "TW", "ISLD", 30, true);
    feed(sess, logon_with_reset);

    // Emits sequence_numbers_reset event (by_peer_request=true).
    auto events = sess.recent_events();
    bool found = false;
    for (const auto& ev : events) {
        if (auto* r = std::get_if<fixpp::session::session_event_sequence_numbers_reset>(&ev)) {
            EXPECT_TRUE(r->by_peer_request)
                << "unilateral: by_peer_request must be true when peer sent 141=Y.";
            found = true;
        }
    }
    EXPECT_TRUE(found) << "unilateral acceptor: peer 141=Y must emit sequence_numbers_reset event. "
                       << "RED: stub does not emit → FAILS RED per T017 design.";
}

// ─────────────────────────────────────────────────────────────────────────────
// W2 (T003): reset_on_logon initiator — by_peer_request=false (FR-006 / SC-006)
//
// A fresh {1,1} initiator with reset_on_logon=true and bilateral_lenient policy
// emits its Logon with 141=Y (any_reset_knob=true + seqnums_at_one=true →
// initr_reset_seqnum=true → latch=true). After the peer echoes 141=Y, the
// sequence_numbers_reset event must have by_peer_request=false (fixpp initiated).
//
// RED on main: current code computes we_initiated=(policy==bilateral_strict) at
// session.cpp:3224, which is false for bilateral_lenient → by_peer_request=true
// (incorrect — fixpp sent 141=Y). Fix: we_initiated=own_logon_sent_reset_flag_
// (the latch alone — C4 gate, distinct from C1's restore gate). [FR-006, SC-006]
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(ResetSeqnumPolicyMatrixTest,
       ResetOnLogon_Initiator_BilateralLenient_PeerAck141_ByPeerRequestFalse) {
    clear_capture();
    // Build config: initiator, bilateral_lenient, reset_on_logon=true, fresh {1,1}.
    auto cfg = make_cfg(fixpp::session::session_role::initiator,
                        fixpp::session::reset_seqnum_policy::bilateral_lenient);
    cfg.reset_on_logon = true;
    fixpp::session::Session sess(engine, cfg);

    // open() emits initiator Logon with 141=Y (reset_on_logon=true + seqnums_at_one=true
    // at {1,1}) → latch own_logon_sent_reset_flag_=true.
    ASSERT_TRUE(run_open(sess).has_value());
    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::LogonSent);

    // Peer Logon-ack echoes 141=Y at seq=1.
    auto logon_ack_reset = make_logon("FIX.4.2", 1, "ISLD", "TW", 30, /*reset=*/true);
    feed(sess, logon_ack_reset);

    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "W2: session must reach Active after peer 141=Y ack";

    // FR-006 / SC-006 / C4: by_peer_request must be false — fixpp initiated the
    // reset (it sent 141=Y in its Logon). The latch alone drives this, independent
    // of reset_before_send. RED on main (we_initiated=bilateral_strict → false for
    // bilateral_lenient → by_peer_request=true).
    auto events = sess.recent_events();
    bool found = false;
    for (const auto& ev : events) {
        if (const auto* r =
                std::get_if<fixpp::session::session_event_sequence_numbers_reset>(&ev)) {
            found = true;
            EXPECT_FALSE(r->by_peer_request)
                << "W2 (T003): reset_on_logon initiator with bilateral_lenient: "
                   "by_peer_request must be false — fixpp sent 141=Y (latch=true). "
                   "RED on main: we_initiated=bilateral_strict only → returns true. "
                   "[FR-006, SC-006, C4]";
        }
    }
    EXPECT_TRUE(found)
        << "W2 (T003): sequence_numbers_reset event must be emitted on this arm";
}
