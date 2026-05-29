// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_compid_binding_mtls_fail_closed.cpp
//
// RC#A (gate-b/r1): Fail-closed CompID binding footgun.
//
// The call-site guard for authorize() must fail CLOSED when the session's
// security_profile.k is an mTLS variant (mtls_ca / mtls_pinned) AND no
// peer_identity is available (override not set). Without this guard, the
// production path (real TLS, no test seam) silently admits any peer.
//
// Test cells (acceptor + initiator — symmetric per [[feedback_half_restructure_symmetric_api]]):
//
//   Cell A: acceptor, mtls_ca profile, no peer_identity override, non-empty policy.
//     Expected: Logon REJECTED → Disconnected +
//               session_event_compid_authorization_failed emitted.
//
//   Cell B: initiator, mtls_ca profile, no peer_identity override, non-empty policy.
//     Expected: peer Logon-ack REJECTED → Disconnected +
//               session_event_compid_authorization_failed emitted.
//
//   Cell C: acceptor, one_way_ca profile, no peer_identity override.
//     Expected: Logon ACCEPTED → Active (no client cert → no CompID gate).
//
// RED (before fix): the guard only fires when logon_peer_identity_override is set,
//   so Cells A+B reach Active instead of Disconnected → assertions fail.
//
// Anchors: spec.md FR-019/FR-021/FR-024; triage RC#A; [FR-023 fail-closed].
// [[feedback_half_restructure_symmetric_api]].

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
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/compid_authorization_policy.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_event.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/session/security_profile.hpp>

#include "support/minimal_dictionary.hpp"

using namespace std::chrono_literals;

namespace {

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

static bool has_compid_auth_failed_event(const fixpp::session::Session& sess) {
    auto events = sess.recent_events();
    return std::any_of(events.begin(), events.end(), [](const fixpp::session::SessionEvent& ev) {
        return std::holds_alternative<
            fixpp::session::session_event_compid_authorization_failed>(ev);
    });
}

}  // namespace

// ── Test fixture ──────────────────────────────────────────────────────────────

class MtlsFailClosedTest : public ::testing::Test {
protected:
    asio::io_context                         ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;  // NOLINT
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
// Cell A: Acceptor, mtls_ca, NO override, non-empty policy with a binding.
//
// Without the fix: guard is skipped (override is nullopt) → authorize() never
//   called → session reaches Active. FAILS the Disconnected assertion.
// With the fix: mTLS + no peer_identity → fail-closed → Disconnected +
//   session_event_compid_authorization_failed emitted.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(MtlsFailClosedTest, Acceptor_MtlsCa_NoPeerIdentity_FailsClosed) {
    fixpp::session::SessionConfig cfg;
    cfg.sender_comp_id     = "ISLD";
    cfg.target_comp_id     = "TW";
    cfg.begin_string       = "FIX.4.2";
    cfg.heartbeat_interval = 30s;
    // mTLS profile — client cert expected → CompID gate should fire.
    cfg.security_profile = fixpp::session::SecurityProfile{
        fixpp::session::SecurityProfile::kind::mtls_ca};
    cfg.dictionary        = fixpp::test_support::make_minimal_dictionary();
    cfg.executor_override = ioc.get_executor();
    cfg.role              = fixpp::session::session_role::acceptor;
    // Non-empty policy with a valid binding for "TW".
    cfg.compid_authorization_policy.add_binding("CN=TW-PROD-01,O=Acme,C=US", {"TW"});
    // logon_peer_identity_override is NOT set (nullopt) — simulates production path
    // where real TLS peer_id is unavailable (no mock_transport handshake).

    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(run_open(sess).has_value());

    // Peer sends Logon — no peer_identity is available (no test seam, no real TLS).
    auto logon = make_logon("FIX.4.2", 1, "TW", "ISLD");
    feed(sess, logon);

    // mTLS + no peer_identity → fail CLOSED.
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Disconnected)
        << "Acceptor: mTLS profile + no peer_identity must fail CLOSED "
        << "(Disconnected). RED: without fix, session reaches Active.";

    EXPECT_TRUE(has_compid_auth_failed_event(sess))
        << "session_event_compid_authorization_failed must be emitted. "
        << "RED: without fix, no event emitted (gate not reached).";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell B: Initiator, mtls_ca, NO override, non-empty policy.
//
// Without fix: peer Logon-ack is processed, guard skipped → Active.
// With fix: mTLS + no peer_identity → fail-closed → Disconnected + event.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(MtlsFailClosedTest, Initiator_MtlsCa_NoPeerIdentity_FailsClosed) {
    fixpp::session::SessionConfig cfg;
    cfg.sender_comp_id     = "TW";
    cfg.target_comp_id     = "ISLD";
    cfg.begin_string       = "FIX.4.2";
    cfg.heartbeat_interval = 30s;
    cfg.security_profile = fixpp::session::SecurityProfile{
        fixpp::session::SecurityProfile::kind::mtls_ca};
    cfg.dictionary        = fixpp::test_support::make_minimal_dictionary();
    cfg.executor_override = ioc.get_executor();
    cfg.role              = fixpp::session::session_role::initiator;
    cfg.compid_authorization_policy.add_binding("CN=ISLD-PROD-01,O=Exchange,C=US", {"ISLD"});
    // logon_peer_identity_override NOT set.

    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(run_open(sess).has_value());
    // After open(), initiator should be in LogonSent.
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::LogonSent);

    // Peer (acceptor) sends Logon-ack.
    auto logon_ack = make_logon("FIX.4.2", 1, "ISLD", "TW");
    feed(sess, logon_ack);

    // mTLS + no peer_identity → fail CLOSED on the initiator path too.
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Disconnected)
        << "Initiator: mTLS profile + no peer_identity must fail CLOSED. "
        << "RED: without fix, session reaches Active.";

    EXPECT_TRUE(has_compid_auth_failed_event(sess))
        << "session_event_compid_authorization_failed must be emitted on initiator path. "
        << "RED: without fix, gate skipped entirely.";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell C: Acceptor, one_way_ca, no override — must NOT fail-closed.
//   one_way_ca is server-auth-only (no client cert) → CompID gate is inapplicable.
//   Session should reach Active (backward compat for non-mTLS sessions).
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(MtlsFailClosedTest, Acceptor_OneWayCa_NoPeerIdentity_Accepts) {
    fixpp::session::SessionConfig cfg;
    cfg.sender_comp_id     = "ISLD";
    cfg.target_comp_id     = "TW";
    cfg.begin_string       = "FIX.4.2";
    cfg.heartbeat_interval = 30s;
    // one_way_ca: server auth only → no client cert → CompID gate skipped.
    cfg.security_profile = fixpp::session::SecurityProfile{
        fixpp::session::SecurityProfile::kind::one_way_ca};
    cfg.dictionary        = fixpp::test_support::make_minimal_dictionary();
    cfg.executor_override = ioc.get_executor();
    cfg.role              = fixpp::session::session_role::acceptor;
    // No override, no policy needed.

    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(run_open(sess).has_value());

    auto logon = make_logon("FIX.4.2", 1, "TW", "ISLD");
    feed(sess, logon);

    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "one_way_ca profile: no client cert → CompID gate skipped → Active.";
    EXPECT_FALSE(has_compid_auth_failed_event(sess))
        << "one_way_ca: no compid_authorization_failed event expected.";
}
