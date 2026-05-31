// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_compid_binding_default_deny.cpp — T029 [P] [US2] Phase 4 RED
//
// CompIdAuthorizationPolicy default-deny gate (FR-023 / US2 AC6).
//
// Rule: an EMPTY CompIdAuthorizationPolicy (the default-constructed form that
// ships in SessionConfig) must reject EVERY Logon with
// error::session_compid_unauthorized (slot 117). The policy is FAIL-CLOSED:
// an operator who forgets to populate bindings is always rejected, never
// silently admitted.
//
// This test drives bytes through Session::on_inbound_frame (production-shaped
// per [[project_005_phase8_completeness_false_pass]]) and asserts:
//   (a) The session transitions to Disconnected (not Active) after the Logon.
//   (b) session_event_compid_authorization_failed{expected_compids=[]}
//       is visible via Session::recent_events().
//
// Anchors: spec.md §US2 AC6 / FR-023; data-model.md §E-3; plan.md T029;
//   contracts/compid_authorization_policy.hpp; D-9 allow-list-only;
//   [[feedback_subagent_phase_verification_two_traps]].
//
// RED witness: Phase 2/3 authorize() stub always returns session_compid_unauthorized,
//   so the session currently reaches Disconnected even without the FR-023 hookup.
//   The integration test (session_event_compid_authorization_failed via
//   Session::recent_events()) will FAIL RED because the Logon path does NOT yet
//   emit the event (T036 impl not yet landed). Both acceptor + initiator cells.

#include <algorithm>
#include <array>
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
#include <fixpp/session/compid_authorization_policy.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_event.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/tls/peer_identity.hpp>

#include "support/identity_injecting_transport.hpp"
#include "support/minimal_dictionary.hpp"

using namespace std::chrono_literals;

namespace {

// ── FIX frame helpers (mirrors test_reset_seqnum_policy_matrix.cpp) ──────────

static std::string field(int tag, std::string_view val) {
    return std::to_string(tag) + "=" + std::string(val) + "\x01";
}

static std::vector<std::byte> make_fix_frame(std::string_view begin_string,
                                              std::string_view msg_type,
                                              std::uint32_t seq,
                                              std::string_view sender,
                                              std::string_view target,
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

static std::vector<std::byte> make_logon(std::string_view bs, std::uint32_t seq,
                                          std::string_view sender, std::string_view target,
                                          int hbt = 30) {
    std::string extra;
    extra += field(98, "0");
    extra += field(108, std::to_string(hbt));
    return make_fix_frame(bs, "A", seq, sender, target, extra);
}

// ── peer_identity factory for test stubs ─────────────────────────────────────

static fixpp::tls::peer_identity make_peer_identity(std::string_view subject_dn) {
    fixpp::tls::peer_identity pid;
    pid.subject_dn = subject_dn;
    return pid;
}

// ── Event helpers ─────────────────────────────────────────────────────────────

static bool has_compid_auth_failed_event(const fixpp::session::Session& sess) {
    auto events = sess.recent_events();
    return std::any_of(events.begin(), events.end(), [](const fixpp::session::SessionEvent& ev) {
        return std::holds_alternative<
            fixpp::session::session_event_compid_authorization_failed>(ev);
    });
}

static const fixpp::session::session_event_compid_authorization_failed*
find_compid_auth_failed_event(const fixpp::session::Session& sess) {
    auto events = sess.recent_events();
    for (const auto& ev : events) {
        if (const auto* p =
                std::get_if<fixpp::session::session_event_compid_authorization_failed>(&ev)) {
            return p;
        }
    }
    return nullptr;
}

}  // namespace

// ── Test fixture ──────────────────────────────────────────────────────────────

class CompidBindingDefaultDenyTest : public ::testing::Test {
protected:
    asio::io_context                         ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig                engine{};

    void SetUp() override {
        auto utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock    = clock;
        engine.executor = ioc.get_executor();
    }

    fixpp::session::SessionConfig make_acceptor_cfg() {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id    = "ISLD";
        cfg.target_comp_id    = "TW";
        cfg.begin_string      = "FIX.4.2";
        cfg.heartbeat_interval = 30s;
        // mTLS so the authorize() gate fires (the live-identity arm requires it).
        cfg.security_profile  = fixpp::session::SecurityProfile{
            fixpp::session::SecurityProfile::kind::mtls_ca};
        cfg.dictionary        = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.role              = fixpp::session::session_role::acceptor;
        // compid_authorization_policy is default-constructed = empty = default-deny.
        // The peer identity is injected post-open() via inject_live_identity (T021).
        // RC#C (gate-b/r1): bilateral_lenient — tests here don't exercise reset semantics.
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
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
};

// ─────────────────────────────────────────────────────────────────────────────
// Cell A: acceptor — empty policy rejects Logon → Disconnected.
//
// The empty policy has no bindings. Any peer_identity maps to "no match".
// Expect: state == Disconnected after the peer Logon.
// Expect: session_event_compid_authorization_failed emitted with
//         expected_compids=[] (principal not found at all).
//
// RED: T036 not yet landed → event not emitted → FAILS RED on event assertion.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(CompidBindingDefaultDenyTest, Acceptor_EmptyPolicy_RejectsLogon) {
    auto cfg = make_acceptor_cfg();
    // compid_authorization_policy is empty (default-deny per FR-023 / D-9).
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(run_open(sess).has_value());

    // Inject the live handshake identity (off-list under the empty policy).
    fixpp::test_support::inject_live_identity(
        sess, make_peer_identity("CN=TW-PROD-01,O=Acme,C=US"));

    // Peer (initiator) sends Logon.
    auto logon = make_logon("FIX.4.2", 1, "TW", "ISLD");
    feed(sess, logon);

    // The session MUST NOT reach Active (default-deny FAIL-CLOSED).
    EXPECT_NE(sess.state(), fixpp::session::fsm_state::Active)
        << "Empty policy must FAIL CLOSED — peer Logon must be rejected. "
        << "RED (T036 not landed): authorize() path not yet wired into Logon.";

    // The session should reach Disconnected (rejected + closed transport).
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Disconnected)
        << "Empty policy: Logon must end in Disconnected. "
        << "RED: may reach another state if reject path not wired.";

    // session_event_compid_authorization_failed must be emitted.
    // RED: T036 event emission not yet wired.
    EXPECT_TRUE(has_compid_auth_failed_event(sess))
        << "session_event_compid_authorization_failed must be in recent_events(). "
        << "RED (T036 not landed): event not emitted yet.";

    // Verify the event carries expected_compids=[] (unrecognized principal).
    const auto* ev = find_compid_auth_failed_event(sess);
    if (ev != nullptr) {
        EXPECT_TRUE(ev->expected_compids.empty())
            << "expected_compids must be empty when principal is not in bindings at all.";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell B: initiator — empty policy rejects peer Logon-ack.
//
// Initiator side: our Logon is sent in open(); when peer's Logon-ack arrives,
// authorize() fires against TargetCompID(56) per FR-024 symmetric coverage.
// Empty policy → Disconnected + event.
//
// RED: T036 not yet landed → event not emitted → FAILS RED on event assertion.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(CompidBindingDefaultDenyTest, Initiator_EmptyPolicy_RejectsLogonAck) {
    fixpp::session::SessionConfig cfg;
    cfg.sender_comp_id    = "TW";
    cfg.target_comp_id    = "ISLD";
    cfg.begin_string      = "FIX.4.2";
    cfg.heartbeat_interval = 30s;
    // mTLS so the authorize() gate fires (the live-identity arm requires it).
    cfg.security_profile  = fixpp::session::SecurityProfile{
        fixpp::session::SecurityProfile::kind::mtls_ca};
    cfg.dictionary        = fixpp::test_support::make_minimal_dictionary();
    cfg.executor_override = ioc.get_executor();
    cfg.role              = fixpp::session::session_role::initiator;
    // RC#C (gate-b/r1): bilateral_lenient — cell tests default-deny, not reset semantics.
    cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    // Empty policy — default-deny.

    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(run_open(sess).has_value());
    // After open(), initiator should be in LogonSent.
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::LogonSent);

    // Inject the live handshake identity (off-list under the empty policy).
    fixpp::test_support::inject_live_identity(
        sess, make_peer_identity("CN=ISLD-PROD-01,O=Exchange,C=US"));

    // Peer (acceptor) sends Logon-ack.
    auto logon_ack = make_logon("FIX.4.2", 1, "ISLD", "TW");
    feed(sess, logon_ack);

    // Empty policy → authorize() rejects peer's TargetCompID → Disconnected.
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Disconnected)
        << "Initiator empty policy: peer Logon-ack must be rejected (Disconnected). "
        << "RED (T036 not landed): authorize() not yet called on LogonSent path.";

    // session_event_compid_authorization_failed must be emitted.
    EXPECT_TRUE(has_compid_auth_failed_event(sess))
        << "session_event_compid_authorization_failed must be in recent_events(). "
        << "RED (T036 not landed): event not emitted yet.";
}
