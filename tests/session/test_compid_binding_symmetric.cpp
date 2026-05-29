// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_compid_binding_symmetric.cpp — T031 [P] [US2] Phase 4 RED
//
// CompID-binding symmetric coverage (FR-024 / US2 AC4 /
// [[feedback_half_restructure_symmetric_api]]).
//
// Two cells proving that BOTH roles use the same CompIdAuthorizationPolicy
// code path at Logon time:
//
//   Cell A (acceptor): peer client cert (SAN-DNS "tw-prod.example.com") binds
//     to peer SenderCompID(49) = "TW". Acceptor verifies the peer is authorized
//     to claim that CompID.
//
//   Cell B (initiator): peer server cert (CN "ISLD-PROD-01") binds to peer
//     TargetCompID(56) = "ISLD". Initiator verifies the peer's identity matches
//     the expected target.
//
// The same CompIdAuthorizationPolicy::authorize() code path is called in BOTH
// arms per FR-024; the role discriminator is WHICH CompID is checked (peer
// SenderCompID on acceptor side; peer TargetCompID on initiator side).
//
// This test is the counter-witness against the 012 RC#B half-restructure trap
// ([[feedback_half_restructure_symmetric_api]]): if T036 only wires the acceptor
// arm and forgets the initiator arm, Cell B fails RED while Cell A goes GREEN.
//
// Anchors: spec.md §US2 AC4 / FR-024; data-model.md §D-10; plan.md T031;
//   [[feedback_half_restructure_symmetric_api]].
//
// RED witness: T036 (Logon-path authorize hookup) not yet landed. Both cells
//   will FAIL because the event is not emitted. After T036, both cells must
//   simultaneously go GREEN (any asymmetry → residual RED in one cell).

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
#include <fixpp/session/compid_authorization_policy.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_event.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/tls/peer_identity.hpp>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

using namespace std::chrono_literals;

namespace {

// ── FIX frame helpers ─────────────────────────────────────────────────────────

static std::string field(int tag, std::string_view val) {
    return std::to_string(tag) + "=" + std::string(val) + "\x01";
}

static std::vector<std::byte> make_logon_frame(std::string_view bs, std::uint32_t seq,
                                                std::string_view sender, std::string_view target,
                                                int hbt = 30) {
    std::string body;
    body += field(35, "A");
    body += field(34, std::to_string(seq));
    body += field(49, sender);
    body += field(52, "20240101-00:00:00.000");
    body += field(56, target);
    body += field(98, "0");
    body += field(108, std::to_string(hbt));

    std::string msg;
    msg += "8=" + std::string(bs) + "\x01";
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

// ── Event helpers ─────────────────────────────────────────────────────────────

static bool has_peer_identity_bound_event(const fixpp::session::Session& sess) {
    auto events = sess.recent_events();
    return std::any_of(events.begin(), events.end(), [](const fixpp::session::SessionEvent& ev) {
        return std::holds_alternative<fixpp::session::session_event_peer_identity_bound>(ev);
    });
}

static bool has_compid_auth_failed_event(const fixpp::session::Session& sess) {
    auto events = sess.recent_events();
    return std::any_of(events.begin(), events.end(), [](const fixpp::session::SessionEvent& ev) {
        return std::holds_alternative<
            fixpp::session::session_event_compid_authorization_failed>(ev);
    });
}

}  // namespace

// ── Fixture ───────────────────────────────────────────────────────────────────

class CompidBindingSymmetricTest : public ::testing::Test {
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
// Cell A: ACCEPTOR — peer client cert ↔ peer SenderCompID(49)
//
// Acceptor: we are ISLD, peer is TW.
// Policy: SAN-DNS "tw-prod.example.com" → authorized CompID "TW".
// Peer Logon carries SenderCompID(49)="TW".
// Expected: Session reaches Active + emits session_event_peer_identity_bound
//   with principal_source=SAN_DNS and bound_compid="TW".
//
// RED: T036 not landed → event not emitted.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(CompidBindingSymmetricTest, CellA_Acceptor_PeerClientCert_BindsSenderCompId) {
    fixpp::session::SessionConfig cfg;
    cfg.sender_comp_id    = "ISLD";
    cfg.target_comp_id    = "TW";
    cfg.begin_string      = "FIX.4.2";
    cfg.heartbeat_interval = 30s;
    cfg.security_profile  = fixpp::test_support::make_minimal_security_profile();
    cfg.dictionary        = fixpp::test_support::make_minimal_dictionary();
    cfg.executor_override = ioc.get_executor();
    cfg.role              = fixpp::session::session_role::acceptor;

    // Policy: SAN-DNS → authorized for SenderCompID "TW".
    cfg.compid_authorization_policy.add_binding("tw-prod.example.com", "TW");

    // Inject peer_identity (client cert) with SAN-DNS only (no CN).
    fixpp::tls::peer_identity pid;
    pid.subject_dn = "";
    pid.san_dns_names_owned.emplace_back("tw-prod.example.com");
    cfg.logon_peer_identity_override = std::move(pid);

    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(run_open(sess).has_value());

    // Peer (TW initiator) sends Logon with SenderCompID(49)="TW".
    auto logon = make_logon_frame("FIX.4.2", 1, "TW", "ISLD");
    feed(sess, logon);

    // Cell A: MUST reach Active (SAN-DNS matches binding → TW authorized).
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "Cell A (acceptor): SAN-DNS matches → Active. "
        << "RED (T036 not landed): authorize() not yet called in Logon path.";

    // session_event_peer_identity_bound must be emitted.
    EXPECT_TRUE(has_peer_identity_bound_event(sess))
        << "Cell A: session_event_peer_identity_bound must be in recent_events(). "
        << "RED (T036 not landed): event not emitted yet.";

    // Must NOT have a failed event.
    EXPECT_FALSE(has_compid_auth_failed_event(sess))
        << "Cell A: session_event_compid_authorization_failed must NOT be emitted on success.";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell B: INITIATOR — peer server cert ↔ peer TargetCompID(56)
//
// Initiator: we are TW, peer is ISLD.
// Policy: CN "ISLD-PROD-01" → authorized CompID "ISLD".
// Peer Logon-ack carries TargetCompID(56)="TW" (our compid as peer's target).
// On the initiator side, we check the PEER'S identity against cfg_.target_comp_id
// (the peer's CompID as we know it) per FR-024.
//
// Expected: Session reaches Active + emits session_event_peer_identity_bound
//   with principal_source=CN and bound_compid="ISLD".
//
// RED: T036 not landed → initiator path not wired.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(CompidBindingSymmetricTest, CellB_Initiator_PeerServerCert_BindsTargetCompId) {
    fixpp::session::SessionConfig cfg;
    cfg.sender_comp_id    = "TW";
    cfg.target_comp_id    = "ISLD";
    cfg.begin_string      = "FIX.4.2";
    cfg.heartbeat_interval = 30s;
    cfg.security_profile  = fixpp::test_support::make_minimal_security_profile();
    cfg.dictionary        = fixpp::test_support::make_minimal_dictionary();
    cfg.executor_override = ioc.get_executor();
    cfg.role              = fixpp::session::session_role::initiator;

    // Policy: CN "ISLD-PROD-01" → authorized for TargetCompID "ISLD".
    cfg.compid_authorization_policy.add_binding("ISLD-PROD-01", "ISLD");

    // Inject peer_identity (server cert) with CN=ISLD-PROD-01.
    fixpp::tls::peer_identity pid;
    pid.subject_dn = "CN=ISLD-PROD-01,O=Exchange,C=US";
    cfg.logon_peer_identity_override = std::move(pid);

    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(run_open(sess).has_value());
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::LogonSent);

    // Peer (ISLD acceptor) sends Logon-ack: SenderCompID=ISLD, TargetCompID=TW.
    auto logon_ack = make_logon_frame("FIX.4.2", 1, "ISLD", "TW");
    feed(sess, logon_ack);

    // Cell B: MUST reach Active (CN matches binding → ISLD authorized for initiator).
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "Cell B (initiator): CN matches → Active. "
        << "RED (T036 not landed): initiator LogonSent path not yet wired.";

    // session_event_peer_identity_bound must be emitted.
    EXPECT_TRUE(has_peer_identity_bound_event(sess))
        << "Cell B: session_event_peer_identity_bound must be in recent_events(). "
        << "RED (T036 not landed): event not emitted on initiator path.";

    // Must NOT have a failed event.
    EXPECT_FALSE(has_compid_auth_failed_event(sess))
        << "Cell B: session_event_compid_authorization_failed must NOT be emitted on success.";
}

// ─────────────────────────────────────────────────────────────────────────────
// Mismatch guard — proves both cells are NOT just silently accepting everything.
//
// Cell C: acceptor + WRONG CompID in Logon → rejected.
// This is the boundary witness ensuring Cell A is not a trivially-always-green test.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(CompidBindingSymmetricTest, CellC_Acceptor_WrongCompId_Rejected) {
    fixpp::session::SessionConfig cfg;
    cfg.sender_comp_id    = "ISLD";
    cfg.target_comp_id    = "TW";
    cfg.begin_string      = "FIX.4.2";
    cfg.heartbeat_interval = 30s;
    cfg.security_profile  = fixpp::test_support::make_minimal_security_profile();
    cfg.dictionary        = fixpp::test_support::make_minimal_dictionary();
    cfg.executor_override = ioc.get_executor();
    cfg.role              = fixpp::session::session_role::acceptor;

    // Policy: only "TW-PROD-01" (CN) → "TW" is allowed.
    // But peer's CN is "STRANGER" — not in the policy.
    cfg.compid_authorization_policy.add_binding("TW-PROD-01", "TW");

    fixpp::tls::peer_identity pid;
    pid.subject_dn = "CN=STRANGER,O=Unknown,C=US";
    cfg.logon_peer_identity_override = std::move(pid);

    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(run_open(sess).has_value());

    auto logon = make_logon_frame("FIX.4.2", 1, "TW", "ISLD");
    feed(sess, logon);

    // STRANGER not in policy → Disconnected.
    EXPECT_NE(sess.state(), fixpp::session::fsm_state::Active)
        << "Cell C: unrecognized principal must not reach Active. "
        << "RED (T036 not landed): Logon path not yet wired.";
}
