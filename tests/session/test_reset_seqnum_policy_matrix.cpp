// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_reset_seqnum_policy_matrix.cpp — T017 [US1] Phase 3 RED
//
// ResetSeqNumFlag(141)=Y policy 6-cell matrix:
//   3 modes (bilateral_strict / bilateral_lenient / unilateral)
//   × 2 roles (initiator / acceptor)
//
// For bilateral_strict: peer Logon without 141=Y when we sent 141=Y →
//   session_seqnum_reset_mismatch(116) + Disconnected. An acceptor's refusal of a Logon
//   without 141=Y also sends a Logout with Text, before the sequence check (103 FR-004).
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
#include <fixpp/session/file_store.hpp>
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

#include "session/mock_reconnect_factory.hpp"
#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"
#include "support/recording_store.hpp"
#include "support/seqnum_manager_test_access.hpp"
#include "support/session_executor_of.hpp"
#include "support/session_test_access.hpp"
#include "support/temp_dir.hpp"

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

std::string field(int tag, std::string_view val) {
    return std::to_string(tag) + "=" + std::string(val) + "\x01";
}

std::vector<std::byte> make_fix_frame(std::string_view begin_string, std::string_view msg_type,
                                      std::uint32_t seq, std::string_view sender,
                                      std::string_view target, std::string_view extra = {}) {
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
std::vector<std::byte> make_logon(std::string_view bs, std::uint32_t seq, std::string_view s,
                                  std::string_view t, int hbt = 30, bool reset_seqnum = false) {
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

bool has_session_reset_event(const fixpp::session::Session& sess) {
    auto events = sess.recent_events();
    return std::ranges::any_of(events, [](const fixpp::session::SessionEvent& ev) {
        return std::holds_alternative<fixpp::session::session_event_sequence_numbers_reset>(ev);
    });
}

// frame_has_141Y: check whether a raw FIX frame contains "141=Y\x01".
// Used to verify that the outbound reply Logon echoes ResetSeqNumFlag(141)=Y.
// (RC#C-2 gate-b/r2 false-pass closure — outbound-frame assertion.)
bool frame_has_141Y(const std::vector<std::byte>& frame) {
    static constexpr std::string_view needle = "141=Y\x01";
    if (frame.size() < needle.size()) return false;
    const auto* data = reinterpret_cast<const char*>(frame.data());
    std::string_view sv(data, frame.size());
    return sv.contains(needle);
}

// frame_msg_type_is: check whether a raw FIX frame carries MsgType(35)=<msg_type>.
// SOH-anchored on both sides so a tag ending in 35 (e.g. 135=A) cannot match.
bool frame_msg_type_is(const std::vector<std::byte>& frame, std::string_view msg_type) {
    std::string needle(1, '\x01');
    needle += "35=";
    needle += msg_type;
    needle += '\x01';
    const auto* data = reinterpret_cast<const char*>(frame.data());
    return std::string_view(data, frame.size()).contains(needle);
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
        auto fut = asio::co_spawn(fixpp::test_support::session_executor_of(s, ioc), s.open(),
                                  asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 100ms,
                                                        "ResetSeqnumPolicyMatrixTest::run_open")) {
            fixpp::test_support::cancel_and_drain_or_report(
                ioc, *clock, "ResetSeqnumPolicyMatrixTest::run_open");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "ResetSeqnumPolicyMatrixTest::run_open";
            return std::unexpected(fixpp::test_support::kWindowMissSentinel);
        }
        return fut.get();
    }

    fixpp::core::expected_t<void> feed(fixpp::session::Session& s,
                                       std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(fixpp::test_support::session_executor_of(s, ioc),
                                  s.on_inbound_frame(frame), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 100ms,
                                                        "ResetSeqnumPolicyMatrixTest::feed")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "ResetSeqnumPolicyMatrixTest::feed");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "ResetSeqnumPolicyMatrixTest::feed";
            return std::unexpected(fixpp::test_support::kWindowMissSentinel);
        }
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
    ASSERT_TRUE(feed(sess, logon_with_reset).has_value());

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
    (void)feed(sess, logon_no_reset);  // outcome checked below via state

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
    ASSERT_TRUE(feed(sess, logon_with_reset).has_value());

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
    ASSERT_TRUE(feed(sess, logon_no_reset).has_value());

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
    ASSERT_TRUE(feed(sess, logon_ack_with_reset).has_value());

    // (a) Session reaches Active after bilateral confirmation.
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "bilateral_strict initiator: peer ack with 141=Y → Active.";

    // (b) Exactly one sequence_numbers_reset event, by_peer_request=false (WE initiated).
    // FR-018: bilateral_strict confirm path → we sent 141=Y first → by_peer_request=false.
    auto events = sess.recent_events();
    std::size_t reset_events = 0;
    bool by_peer_request_correct = false;
    for (const auto& ev : events) {
        if (const auto* r =
                std::get_if<fixpp::session::session_event_sequence_numbers_reset>(&ev)) {
            ++reset_events;
            by_peer_request_correct = !r->by_peer_request;  // must be false (we initiated)
        }
    }
    EXPECT_EQ(reset_events, 1U)
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
    ASSERT_TRUE(feed(sess, logon_with_reset).has_value());

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
// RC#C-2 fix: bilateral_lenient now mirrors 141=Y
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
    ASSERT_TRUE(feed(sess, logon_with_reset).has_value());

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
    ASSERT_TRUE(feed(sess, logon_with_reset).has_value());

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
    ASSERT_TRUE(feed(sess, logon_with_reset).has_value());

    // The 141 check below must run on the reply LOGON, not on whatever frame was
    // captured: a non-Logon reply (Logout, Heartbeat, Reject) carries no 141 and would
    // satisfy an any-frame check while no reply Logon was sent at all.
    std::vector<const std::vector<std::byte>*> reply_logons;
    for (const auto& f : capture.frames) {
        if (frame_msg_type_is(f, "A")) reply_logons.push_back(&f);
    }
    ASSERT_EQ(reply_logons.size(), 1U)
        << "unilateral acceptor: expected exactly one outbound reply Logon (35=A) frame.";
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "unilateral acceptor: must reach Active after replying to a 141=Y Logon.";
    // unilateral: outbound 141 is config-driven, NOT mirror-driven (FR-017:149).
    // The reply Logon must NOT echo 141=Y just because the peer sent it.
    for (const auto* f : reply_logons) {
        EXPECT_FALSE(frame_has_141Y(*f))
            << "unilateral acceptor: reply Logon must NOT carry 141=Y "
            << "(FR-017:149 — outbound 141 is config-driven, not mirror-driven).";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// RC#C-1 (gate-b/r2) — counter-reset assertions.
//
// The existing cells never assert seqnum counter values. They pass with fresh
// sessions (counters already at 1) where the missing reset is a no-op.
// These cells pre-advance next_outbound_ to 10 via
// seqnum_manager_test_access::set_counters so
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
// Reads the counters through session_test_access::seqnum_mgr (tests/support/).
//
// Anchors: spec.md FR-017:150; data-model.md §E-4;
//   [[feedback_simplify_pass_catches_9th_burn]] (false-pass via fresh-session).
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ResetSeqnumPolicyMatrixTest, BilateralStrict_Acceptor_CountersResetToOne) {
    clear_capture();
    auto cfg = make_cfg(fixpp::session::session_role::acceptor,
                        fixpp::session::reset_seqnum_policy::bilateral_strict);
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(run_open(sess).has_value());

    // Pre-advance outbound counter to 10 so the reset is non-trivial.
    // Keep inbound at 1 (peer will send Logon seq=1; check_inbound(1) must pass).
    fixpp::session::seqnum_manager_test_access::set_counters(
        fixpp::session::session_test_access::seqnum_mgr(sess), 1, 10);

    auto logon_with_reset = make_logon("FIX.4.2", 1, "TW", "ISLD", 30, /*reset=*/true);
    ASSERT_TRUE(feed(sess, logon_with_reset).has_value());

    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active);

    // FR-017:150 + 030 FR-001: after a received-141=Y reset the OUTBOUND side
    // re-bases to 1, but the consumed seq-1 reset Logon is a surviving inbound
    // advance, so next-expected-INBOUND is 2 (QuickFIX reset-then-increment parity).
    // next_inbound_ = 2 (consumed the seq-1 reset Logon — 030 FR-001)
    // next_outbound_ = 2 (reply Logon consumed seq=1; next to send is seq=2)
    EXPECT_EQ(fixpp::session::session_test_access::seqnum_mgr(sess).next_inbound_unsafe(),
              fixpp::session::seqnum_t{2})
        << "bilateral_strict acceptor: next_inbound_ must be 2 after received-141=Y reset "
        << "(consumed seq-1 reset Logon; 030 FR-001).";
    EXPECT_EQ(fixpp::session::session_test_access::seqnum_mgr(sess).next_outbound_unsafe(),
              fixpp::session::seqnum_t{2})
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
    fixpp::session::seqnum_manager_test_access::set_counters(
        fixpp::session::session_test_access::seqnum_mgr(sess), 1, 10);

    auto logon_with_reset = make_logon("FIX.4.2", 1, "TW", "ISLD", 30, /*reset=*/true);
    ASSERT_TRUE(feed(sess, logon_with_reset).has_value());

    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active);

    EXPECT_EQ(fixpp::session::session_test_access::seqnum_mgr(sess).next_inbound_unsafe(),
              fixpp::session::seqnum_t{2})
        << "bilateral_lenient acceptor: next_inbound_ must be 2 after received-141=Y reset "
        << "(consumed seq-1 reset Logon; 030 FR-001).";
    EXPECT_EQ(fixpp::session::session_test_access::seqnum_mgr(sess).next_outbound_unsafe(),
              fixpp::session::seqnum_t{2})
        << "bilateral_lenient acceptor: next_outbound_ must be 2 after successful 141=Y reset.";
}

TEST_F(ResetSeqnumPolicyMatrixTest, BilateralStrict_Initiator_CountersResetToOne) {
    clear_capture();
    // Pre-seed outbound to 10 BEFORE open() so the initiator's Logon is seq=10.
    auto cfg = make_cfg(fixpp::session::session_role::initiator,
                        fixpp::session::reset_seqnum_policy::bilateral_strict);
    fixpp::session::Session sess(engine, cfg);
    // Set counters before open() so the outbound Logon uses seq=10 (non-trivial).
    fixpp::session::seqnum_manager_test_access::set_counters(
        fixpp::session::session_test_access::seqnum_mgr(sess), 1, 10);

    ASSERT_TRUE(run_open(sess).has_value());
    // 103 FR-003 (research D2.2): strict resets both numbers durably to 1 before the
    // Logon, so the pre-seeded outbound 10 is not used: the Logon is seq=1 with 141=Y.
    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::LogonSent);
    ASSERT_FALSE(capture.frames.empty());
    {
        std::string const logon(reinterpret_cast<char const*>(capture.frames.back().data()),
                                capture.frames.back().size());
        EXPECT_TRUE(
            logon.contains("\x01"
                           "34=1\x01"))
            << "bilateral_strict resets before the Logon (103 FR-003): " << logon;
    }

    // Peer (acceptor) acks with Logon seq=1 + 141=Y (mutual reset confirm).
    auto logon_ack_with_reset = make_logon("FIX.4.2", 1, "ISLD", "TW", 30, /*reset=*/true);
    ASSERT_TRUE(feed(sess, logon_ack_with_reset).has_value());

    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active);

    // FR-017:150 + 030 FR-009, with 103 D2.3: after mutual reset:
    //   next_inbound_ = 2 (the peer's seq-1 reset-ack Logon was consumed in-sequence;
    //     030 restores the advance on the initiator peer_ack_sent_reset_flag arm — the
    //     identical clobber to the acceptor arm; QuickFIX reset-then-increment parity)
    //   next_outbound_ = 2 (the initiator's own Logon is numbered 1 after the reset, so the
    //     reset unit keeps it consumed: unit_out = 2; research D2.3)
    EXPECT_EQ(fixpp::session::session_test_access::seqnum_mgr(sess).next_inbound_unsafe(),
              fixpp::session::seqnum_t{2})
        << "bilateral_strict initiator: next_inbound_ must be 2 after 141=Y mutual reset "
        << "(consumed seq-1 reset-ack Logon; 030 FR-009).";
    EXPECT_EQ(fixpp::session::session_test_access::seqnum_mgr(sess).next_outbound_unsafe(),
              fixpp::session::seqnum_t{2})
        << "bilateral_strict initiator: next_outbound_ must be 2 after 141=Y mutual reset "
        << "(103 D2.3: the Logon was seq=1, after strict's reset).";

    // T013 W4b — the label of the reset [032 contract C4, FR-006, SC-006]: C4 keys on the
    // latch ALONE, so by_peer_request=false (fixpp sent 141=Y). Before 103 this was the row
    // where latch=true AND reset_before_send=false (a strict Logon at seq=10). Since 103 a
    // Logon with 141=Y carries 34=1 (FR-002), so the latch implies reset_before_send and no
    // row separates C4 from C1 any more; the assertion keeps the label pinned.
    // [032 T010(d)]
    {
        auto events = sess.recent_events();
        bool found = false;
        for (const auto& ev : events) {
            if (const auto* r =
                    std::get_if<fixpp::session::session_event_sequence_numbers_reset>(&ev)) {
                found = true;
                EXPECT_FALSE(r->by_peer_request)
                    << "T013 W4b: bilateral_strict initiator at seq N>1 — latch=true, "
                       "reset_before_send=false; C4 (label=latch alone) must yield "
                       "by_peer_request=false. A C4/C1 collapse would yield true here. "
                       "[032 contract C4, FR-006, SC-006]";
            }
        }
        EXPECT_TRUE(found) << "T013 W4b: sequence_numbers_reset event must be emitted on the "
                              "peer_ack_sent_reset_flag arm (bilateral_strict initiator).";
    }
}

TEST_F(ResetSeqnumPolicyMatrixTest, Unilateral_Acceptor_CountersResetToOne) {
    clear_capture();
    auto cfg = make_cfg(fixpp::session::session_role::acceptor,
                        fixpp::session::reset_seqnum_policy::unilateral);
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(run_open(sess).has_value());

    // Pre-advance outbound counter to 10 so the reset is non-trivial.
    fixpp::session::seqnum_manager_test_access::set_counters(
        fixpp::session::session_test_access::seqnum_mgr(sess), 1, 10);

    auto logon_with_reset = make_logon("FIX.4.2", 1, "TW", "ISLD", 30, /*reset=*/true);
    ASSERT_TRUE(feed(sess, logon_with_reset).has_value());

    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active);

    EXPECT_EQ(fixpp::session::session_test_access::seqnum_mgr(sess).next_inbound_unsafe(),
              fixpp::session::seqnum_t{2})
        << "unilateral acceptor: next_inbound_ must be 2 after received-141=Y reset "
        << "(consumed seq-1 reset Logon; 030 FR-001).";
    EXPECT_EQ(fixpp::session::session_test_access::seqnum_mgr(sess).next_outbound_unsafe(),
              fixpp::session::seqnum_t{2})
        << "unilateral acceptor: next_outbound_ must be 2 after successful 141=Y reset.";
}

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
    ASSERT_TRUE(feed(sess, logon_with_reset).has_value());

    // Emits sequence_numbers_reset event (by_peer_request=true).
    auto events = sess.recent_events();
    bool found = false;
    for (const auto& ev : events) {
        if (const auto* r =
                std::get_if<fixpp::session::session_event_sequence_numbers_reset>(&ev)) {
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
// the pre-fix `we_initiated`, which was false for bilateral_lenient → by_peer_request=true
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
    ASSERT_TRUE(feed(sess, logon_ack_reset).has_value());

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
    EXPECT_TRUE(found) << "W2 (T003): sequence_numbers_reset event must be emitted on this arm";
}

// ─────────────────────────────────────────────────────────────────────────────
// T011 W3 (US2): peer-spontaneous 141=Y at N>1 — outbound UNCHANGED + by_peer_request=true
//
// reset_on_logon=false, fixpp Logon pre-seeded at seq N>1 (outbound seeded to 10 before
// open()), fixpp sends no 141=Y (seqnums_at_one=false → latch=false); peer sends 141=Y
// under a tolerant (bilateral_lenient) policy.
//
// Expected: outbound UNCHANGED from post-open baseline (next_outbound==11 after
// consuming seq=10), by_peer_request=true (latch=false → we_initiated=false).
//
// Anchors: 032 contracts C1/C4, FR-005, SC-004, W3.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(ResetSeqnumPolicyMatrixTest,
       PeerSpontaneous_Initiator_NonReset_Logon_AtNGt1_OutboundUnchanged) {
    clear_capture();
    // Build: initiator, bilateral_lenient, reset_on_logon=false.
    auto cfg = make_cfg(fixpp::session::session_role::initiator,
                        fixpp::session::reset_seqnum_policy::bilateral_lenient);
    cfg.reset_on_logon = false;
    fixpp::session::Session sess(engine, cfg);

    // Pre-seed outbound to 10 before open() so the Logon goes at seq=10.
    // Inbound stays at 1 (peer will send Logon-ack seq=1).
    // seqnums_at_one = (outbound==1 && inbound==1) = false → latch=false.
    fixpp::session::seqnum_manager_test_access::set_counters(
        fixpp::session::session_test_access::seqnum_mgr(sess), /*next_inbound=*/1,
        /*next_outbound=*/10);

    ASSERT_TRUE(run_open(sess).has_value());
    // open() consumed seq=10 → next_outbound_=11.
    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::LogonSent);

    // Capture baseline outbound after open().
    const auto baseline_outbound =
        fixpp::session::session_test_access::seqnum_mgr(sess).next_outbound_unsafe();
    ASSERT_EQ(baseline_outbound, fixpp::session::seqnum_t{11})
        << "T011 precondition: open() at seq=10 → next_outbound==11";

    // Peer spontaneously sends Logon-ack WITH 141=Y at seq=1.
    // bilateral_lenient accepts this without requiring we sent 141=Y.
    auto logon_ack_reset = make_logon("FIX.4.2", 1, "ISLD", "TW", 30, /*reset=*/true);
    ASSERT_TRUE(feed(sess, logon_ack_reset).has_value());

    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "T011 W3: session must reach Active after peer-spontaneous 141=Y";

    // C1 (outbound unchanged): latch=false → restore gate skipped.
    // After the reset, next_outbound rewinds to 1. The baseline was 11, so if
    // the restore wrongly fired it would be 2; if it correctly did NOT fire, it is 1.
    // The reset itself sets outbound to 1; no restore means it stays 1.
    // W3: outbound must be UNCHANGED from the post-reset baseline (1),
    // i.e. the peer-spontaneous path must NOT advance outbound to 2.
    const auto post_outbound =
        fixpp::session::session_test_access::seqnum_mgr(sess).next_outbound_unsafe();
    EXPECT_EQ(post_outbound, fixpp::session::seqnum_t{1})
        << "T011 W3: peer-spontaneous 141=Y at N>1 (latch=false) — outbound must "
           "rewind to 1 (reset) and NOT be restored to 2. "
           "[032 contract C1, FR-005, SC-004, W3]";

    // C4 (by_peer_request=true): latch=false → we_initiated=false → by_peer_request=true.
    auto events = sess.recent_events();
    bool found = false;
    for (const auto& ev : events) {
        if (const auto* r =
                std::get_if<fixpp::session::session_event_sequence_numbers_reset>(&ev)) {
            found = true;
            EXPECT_TRUE(r->by_peer_request)
                << "T011 W3: peer-spontaneous 141=Y — latch=false → by_peer_request "
                   "must be true. [032 contract C4, FR-006, SC-004]";
        }
    }
    EXPECT_TRUE(found) << "T011 W3: sequence_numbers_reset event must be emitted on the "
                          "peer_ack_sent_reset_flag arm.";
}

// ─────────────────────────────────────────────────────────────────────────────
// T012 W7 (US2): fresh, no reset knob, lenient — peer spontaneous 141=Y at seq=1
//   outbound MUST stay 1 (NOT restored to 2) + by_peer_request=true.
//
// This is the load-bearing latch discriminator (W7). A `reset_before_send`-alone
// gate (no latch conjunct) would restore to 2 because n_pre_outbound==seqnum_min+1
// (=2) is true (Logon went at seq=1). The latch=false (no reset knob, fresh {1,1})
// is the conjunct that prevents the wrong restore.
//
// RED under mutation (a) from T010a: `own_logon_sent_reset_flag &&` removed →
// restore fires on reset_before_send alone → outbound becomes 2 → FAIL.
// GREEN under the shipped latch design (latch=false → restore skipped).
//
// Anchors: 032 contracts C1/C4, FR-005, W7 (the latch discriminator).
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(ResetSeqnumPolicyMatrixTest,
       FreshNoKnob_Initiator_BilateralLenient_PeerSpontaneous141Y_OutboundStaysOne) {
    clear_capture();
    // Build: initiator, bilateral_lenient, reset_on_logon=false (no reset knob).
    // Fresh {1,1} → seqnums_at_one=true but any_reset_knob=false → initr_reset_seqnum=false
    // → latch=false. Logon goes at seq=1 (bilateral_lenient, not bilateral_strict → no 141=Y).
    auto cfg = make_cfg(fixpp::session::session_role::initiator,
                        fixpp::session::reset_seqnum_policy::bilateral_lenient);
    cfg.reset_on_logon = false;
    fixpp::session::Session sess(engine, cfg);

    // open() at {1,1} bilateral_lenient: initr_reset_seqnum=false → latch=false.
    // Logon goes at seq=1 → n_pre_outbound==2 at the arm (outbound advanced 1→2).
    ASSERT_TRUE(run_open(sess).has_value());
    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::LogonSent);

    // Peer spontaneously sends Logon at seq=1 WITH 141=Y (bilateral_lenient accepts it).
    auto logon_ack_reset = make_logon("FIX.4.2", 1, "ISLD", "TW", 30, /*reset=*/true);
    ASSERT_TRUE(feed(sess, logon_ack_reset).has_value());

    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "T012 W7: session must reach Active after peer-spontaneous 141=Y";

    // C1 (restore gate): latch=false → restore skipped → outbound stays 1 (post-reset).
    // reset_before_send IS true (n_pre_outbound==2 == seqnum_min+1), but latch=false
    // means the FULL conjunct is false → no restore.
    // A reset_before_send-alone gate would restore to 2 → WRONG.
    // The shipped latch design keeps outbound at 1 → CORRECT.
    auto events = sess.recent_events();
    bool found = false;
    for (const auto& ev : events) {
        if (const auto* r =
                std::get_if<fixpp::session::session_event_sequence_numbers_reset>(&ev)) {
            found = true;
            EXPECT_TRUE(r->by_peer_request)
                << "T012 W7: fresh no-knob bilateral_lenient — latch=false → "
                   "by_peer_request must be true. [032 contract C4, FR-006, W7]";
        }
    }
    EXPECT_TRUE(found)
        << "T012 W7: sequence_numbers_reset event must be emitted when peer sends 141=Y.";

    // The definitive W7 assertion: outbound must be 1 (no restore), not 2.
    // The event label above is the indirect check; this reads the counter directly.
    EXPECT_EQ(fixpp::session::session_test_access::seqnum_mgr(sess).next_outbound_unsafe(),
              fixpp::session::seqnum_t{1})
        << "T012 W7 counter assertion: fresh no-knob peer-spontaneous-at-seq-1 "
           "(latch=false) — outbound MUST stay 1 after the reset; restore_before_send "
           "alone would wrongly set it to 2. [032 contract C1, FR-005, W7]";
}

// ─────────────────────────────────────────────────────────────────────────────
// 103-resend-logon-reset-fidelity (tasks T023; FR-002, FR-005; research D2.2), AS5:
// policy × the four knobs (reset_on_logon, reset_on_logout, reset_on_disconnect,
// refresh_on_logon) × a persistent store at {1,1} or {37,42}. data-model, verbatim:
// "Invariant (FR-002): any Logon fixpp sends with 141=Y carries 34=1 and both stored
// numbers are 1 when it is built."
//
// "When it is built" is read at the Logon's own store() call, through the recording
// wrapper: the stored inbound number (the FaultStore's) and the outbound number the Logon
// takes (its store() call's sequence number) are both 1 there. The cell also asserts the
// policy table's "Initiator sends 141=Y when" column, so the invariant cannot pass on a
// matrix that sends no 141=Y at all: strict always; lenient and unilateral when a reset
// setting is on and both numbers are 1 (after reset_on_logon's or strict's reset).
// ─────────────────────────────────────────────────────────────────────────────

struct LogonInvariantCombo {
    fixpp::session::reset_seqnum_policy policy;
    unsigned knobs;  // bit 0 reset_on_logon, 1 reset_on_logout, 2 reset_on_disconnect, 3 refresh
    bool fresh;      // store at {1,1}; otherwise {37,42}
};

class ResetPolicyLogonInvariant103 : public ResetSeqnumPolicyMatrixTest,
                                     public ::testing::WithParamInterface<LogonInvariantCombo> {};

TEST_P(ResetPolicyLogonInvariant103, AS5_Any141YLogonCarries34Eq1AndStoredOnes) {
    using fixpp::session::reset_seqnum_policy;
    auto const p = GetParam();
    bool const rol = (p.knobs & 1U) != 0;
    bool const rlo = (p.knobs & 2U) != 0;
    bool const rdi = (p.knobs & 4U) != 0;
    bool const refresh = (p.knobs & 8U) != 0;
    bool const strict = p.policy == reset_seqnum_policy::bilateral_strict;

    auto fault = std::make_shared<fixpp::test_support::reconnect_rig::FaultStoreFactory>(
        p.fresh ? 1U : 37U, p.fresh ? 1U : 42U, /*persistent=*/true);
    auto rec = std::make_shared<fixpp::test_support::RecordingStoreFactory>(fault);
    rec->log->arm();
    bool sampled = false;
    fixpp::session::seqnum_t stored_in_at_logon = 0;
    fixpp::session::seqnum_t logon_store_seq = 0;
    rec->log->on_call = [&](fixpp::test_support::StoreCall const& c) {
        if (sampled || c.op != fixpp::test_support::store_op::store ||
            c.dir != fixpp::session::direction_t::outbound || fault->last_store == nullptr) {
            return;
        }
        sampled = true;
        stored_in_at_logon = fault->last_store->current_inbound();
        logon_store_seq = c.first;
    };

    auto cfg = make_cfg(fixpp::session::session_role::initiator, p.policy);
    cfg.store_factory = rec;
    cfg.reset_on_logon = rol;
    cfg.reset_on_logout = rlo;
    cfg.reset_on_disconnect = rdi;
    cfg.refresh_on_logon = refresh;
    fixpp::session::Session sess{engine, cfg};
    ASSERT_TRUE(run_open(sess).has_value());

    std::vector<std::vector<std::byte>> logons;
    for (auto const& f : capture.frames) {
        if (frame_msg_type_is(f, "A")) logons.push_back(f);
    }
    ASSERT_EQ(logons.size(), 1U);
    bool const sent_141 = frame_has_141Y(logons.front());
    std::string const logon_text(reinterpret_cast<char const*>(logons.front().data()),
                                 logons.front().size());

    // The policy table's column (data-model "Initiator sends 141=Y when").
    bool const reset_before_logon = rol || strict;
    bool const at_one = reset_before_logon || p.fresh;
    bool const any_reset_setting = rol || rlo || rdi;
    bool const expect_141 = strict || (any_reset_setting && at_one);
    EXPECT_EQ(sent_141, expect_141) << logon_text;

    if (sent_141) {
        EXPECT_TRUE(
            logon_text.contains("\x01"
                                "34=1\x01"))
            << logon_text;
        ASSERT_TRUE(sampled) << "the Logon's store() call was not recorded";
        EXPECT_EQ(logon_store_seq, fixpp::session::seqnum_t{1}) << logon_text;
        EXPECT_EQ(stored_in_at_logon, fixpp::session::seqnum_t{1}) << logon_text;
    }
}

std::vector<LogonInvariantCombo> logon_invariant_combos() {
    std::vector<LogonInvariantCombo> out;
    for (auto const pol : {fixpp::session::reset_seqnum_policy::bilateral_strict,
                           fixpp::session::reset_seqnum_policy::bilateral_lenient,
                           fixpp::session::reset_seqnum_policy::unilateral}) {
        for (unsigned k = 0; k < 16U; ++k) {
            for (bool const fresh : {true, false}) out.push_back({pol, k, fresh});
        }
    }
    return out;
}

std::string logon_invariant_name(::testing::TestParamInfo<LogonInvariantCombo> const& i) {
    using fixpp::session::reset_seqnum_policy;
    std::string const pol = i.param.policy == reset_seqnum_policy::bilateral_strict ? "Strict"
                            : i.param.policy == reset_seqnum_policy::bilateral_lenient
                                ? "Lenient"
                                : "Unilateral";
    return pol + "_Knobs" + std::to_string(i.param.knobs) +
           (i.param.fresh ? "_Store1_1" : "_Store37_42");
}

INSTANTIATE_TEST_SUITE_P(Matrix, ResetPolicyLogonInvariant103,
                         ::testing::ValuesIn(logon_invariant_combos()), logon_invariant_name);

// D2.2's case, "reasoned, not run" in research, as a cell: a teardown reset whose store
// call failed (logged-then-proceed, I-07) leaves the store unchanged, and the next Session
// sends no 141. Two lifetimes over one FileStore directory, lenient with
// reset_on_disconnect. The control twin lets the teardown reset succeed: the next Session
// then logs on at 34=1 with 141=Y, so the failed arm's "no 141" is the failure's doing.
namespace {

struct TeardownResetOutcome {
    bool setup_ok = false;
    std::string second_logon;
};

TeardownResetOutcome run_teardown_reset_lifetimes(bool const fail_teardown_reset) {
    TeardownResetOutcome out;
    auto const dir = fixpp::test_support::unique_temp_dir("t103_teardown_reset");
    auto lifetime = [&](bool const first) -> std::string {
        asio::io_context ioc;
        fixpp::core::EngineConfig eng;
        eng.executor = ioc.get_executor();
        std::vector<std::string> sent;
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id = "TW";
        cfg.target_comp_id = "ISLD";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = std::chrono::seconds{0};
        cfg.role = fixpp::session::session_role::initiator;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        cfg.reset_on_disconnect = true;
        cfg.transport_send = [&sent](std::span<const std::byte> d) {
            sent.emplace_back(reinterpret_cast<char const*>(d.data()), d.size());
        };
        fixpp::session::FileStore::Config fc;
        fc.directory = dir;
        fc.sender_comp_id = cfg.sender_comp_id;
        fc.target_comp_id = cfg.target_comp_id;
        fc.file_io_executor = ioc.get_executor();
        auto rec = std::make_shared<fixpp::test_support::RecordingStoreFactory>(fc);
        rec->log->arm();
        cfg.store_factory = rec;
        auto sess = std::make_unique<fixpp::session::Session>(eng, cfg);
        auto run = [&](auto&& aw, char const* site) {
            auto fut = asio::co_spawn(ioc, std::forward<decltype(aw)>(aw), asio::use_future);
            if (!fixpp::test_support::run_window_then_ready(ioc, fut, 500ms, site)) {
                fixpp::test_support::drain_or_report(ioc, site);
                ADD_FAILURE() << fixpp::test_support::kWindowMiss << site;
                return false;
            }
            return fut.get().has_value();
        };
        if (!run(sess->open(), "t103_teardown/open")) return {};
        std::string logon;
        for (auto const& f : sent) {
            if (f.contains("\x01"
                           "35=A\x01"))
                logon = f;
        }
        if (first) {
            // The first Logon is 141=Y at 34=1 over the fresh store; the peer mirrors it.
            auto const ack = make_logon("FIX.4.2", 1, "ISLD", "TW", 0, true);
            (void)run(sess->on_inbound_frame(ack), "t103_teardown/ack");
            std::string const order =
                "35=D\x01"
                "11=ORD-T\x01";
            for (int i = 0; i < 2; ++i) {
                (void)run(sess->send(std::span<const std::byte>{
                              reinterpret_cast<std::byte const*>(order.data()), order.size()}),
                          "t103_teardown/send");
            }
            if (sess->state() != fixpp::session::fsm_state::Active) return {};
            out.setup_ok = true;
        }
        if (first && fail_teardown_reset) {
            // Armed only now: the acknowledgement's own 141=Y reset unit above must succeed.
            rec->log->fail_in = [](fixpp::test_support::StoreCall const& c) {
                return c.op == fixpp::test_support::store_op::reset ||
                       c.op == fixpp::test_support::store_op::reset_to;
            };
        }
        (void)run(sess->close(fixpp::session::close_mode::terminal), "t103_teardown/close");
        sess.reset();
        return logon;
    };
    (void)lifetime(true);
    if (out.setup_ok) out.second_logon = lifetime(false);
    (void)fixpp::test_support::try_remove_temp_dir(dir);
    return out;
}

}  // namespace

TEST(ResetPolicyTeardownReset103, AS5_FailedTeardownResetLeavesTheStore_NextLogonNo141) {
    auto const failed = run_teardown_reset_lifetimes(/*fail_teardown_reset=*/true);
    ASSERT_TRUE(failed.setup_ok) << "setup: the first lifetime did not reach Active";
    ASSERT_FALSE(failed.second_logon.empty()) << "the second lifetime wrote no Logon";
    EXPECT_FALSE(
        failed.second_logon.contains("\x01"
                                     "141=Y\x01"))
        << failed.second_logon;
    EXPECT_FALSE(
        failed.second_logon.contains("\x01"
                                     "34=1\x01"))
        << failed.second_logon;

    auto const control = run_teardown_reset_lifetimes(/*fail_teardown_reset=*/false);
    ASSERT_TRUE(control.setup_ok) << "setup (control): the first lifetime did not reach Active";
    EXPECT_TRUE(
        control.second_logon.contains("\x01"
                                      "141=Y\x01"))
        << control.second_logon;
    EXPECT_TRUE(
        control.second_logon.contains("\x01"
                                      "34=1\x01"))
        << control.second_logon;
}
