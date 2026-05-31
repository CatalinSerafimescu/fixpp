// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_compid_binding_principal_extraction.cpp — T030 [P] [US2] Phase 4 RED
//
// Principal extraction 4-cell matrix (FR-022 / US2 AC5 / Clarifications Q2=A).
//
// Canonical-fixed-order first-non-empty wins: CN → SAN_DNS → SAN_URI →
// SHA256_FINGERPRINT. Tests both the direct authorize() unit-cell AND the
// session-integration path for Session::recent_events()::principal_source.
//
// Cells:
//   A — cert with CN only → principal_source = CN
//   B — cert with empty subject_dn + SAN-DNS → principal_source = SAN_DNS
//   C — cert with empty DN + no SAN-DNS + SAN-URI → principal_source = SAN_URI
//   D — cert with no CN / no SANs → principal_source = SHA256_FINGERPRINT
//
// Anchors: spec.md §US2 AC5 / FR-022; data-model.md §D-8; plan.md T030;
//   contracts/compid_authorization_policy.hpp; Clarifications Q2=A;
//   [[feedback_subagent_phase_verification_two_traps]].
//
// RED witness: authorize() Phase 2/3 stub ignores the peer_identity fields and
//   always returns session_compid_unauthorized. The unit-cell calls (Cells A–D)
//   will FAIL GREEN on the "has_value" assertion once the principal matches a
//   binding, but will FAIL RED if the extraction order is wrong. The session-
//   integration cell will FAIL RED because the event is not yet emitted (T036).

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

// ── peer_identity factory helpers ─────────────────────────────────────────────

static fixpp::tls::peer_identity make_cn_only_identity(std::string_view cn_value) {
    // Subject DN contains CN=<value>; no SANs.
    fixpp::tls::peer_identity pid;
    pid.subject_dn = "CN=" + std::string(cn_value) + ",O=Acme,C=US";
    // leaf_fingerprint is zero-initialized (std::array default).
    return pid;
}

static fixpp::tls::peer_identity make_san_dns_identity(std::string_view san_dns) {
    // Empty subject_dn (no CN); SAN-DNS populated.
    fixpp::tls::peer_identity pid;
    pid.subject_dn = "";  // no CN
    pid.san_dns_names_owned.emplace_back(san_dns);
    return pid;
}

static fixpp::tls::peer_identity make_san_uri_identity(std::string_view san_uri) {
    // Empty subject_dn; no SAN-DNS; SAN-URI populated.
    fixpp::tls::peer_identity pid;
    pid.subject_dn = "";
    // san_dns_names_owned is empty.
    pid.san_uris_owned.emplace_back(san_uri);
    return pid;
}

static fixpp::tls::peer_identity make_fingerprint_only_identity(
    std::array<std::byte, 32> fp) {
    // No CN, no SANs — only the raw SHA-256 fingerprint.
    fixpp::tls::peer_identity pid;
    pid.subject_dn = "";
    pid.leaf_fingerprint = fp;
    return pid;
}

// Lower-case hex encode 32 bytes to a 64-char string (mirrors extract_principal).
static std::string fingerprint_to_hex(const std::array<std::byte, 32>& fp) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out(64, '\0');
    for (std::size_t i = 0; i < 32; ++i) {
        const auto b = static_cast<unsigned char>(fp[i]);
        out[2 * i]     = kHex[b >> 4u];
        out[2 * i + 1] = kHex[b & 0xFu];
    }
    return out;
}

// ── Event helpers ─────────────────────────────────────────────────────────────

static const fixpp::session::session_event_peer_identity_bound*
find_peer_identity_bound_event(const fixpp::session::Session& sess) {
    auto events = sess.recent_events();
    for (const auto& ev : events) {
        if (const auto* p =
                std::get_if<fixpp::session::session_event_peer_identity_bound>(&ev)) {
            return p;
        }
    }
    return nullptr;
}

}  // namespace

// ── Unit-cell tests (direct authorize() API) ──────────────────────────────────

// Cell A: CN only → principal_source = CN
// The CN "ACME-PROD-01" is extracted from "CN=ACME-PROD-01,O=Acme,C=US" and
// matched against the binding {"ACME-PROD-01" → "ACME01"}.
TEST(CompidPrincipalExtractionUnit, CellA_CnFirst) {
    fixpp::session::CompIdAuthorizationPolicy policy;
    policy.add_binding("ACME-PROD-01", "ACME01");

    auto pid = make_cn_only_identity("ACME-PROD-01");
    auto result = policy.authorize(pid, "ACME01");

    // RED: authorize() Phase 2 stub ignores pid → always returns unauthorized.
    EXPECT_TRUE(result.has_value())
        << "Cell A: CN-based principal must authorize successfully. "
        << "RED (T034 not landed): stub always returns unauthorized.";
    if (result.has_value()) {
        EXPECT_EQ(result->from, fixpp::session::bound_principal::source::CN)
            << "Cell A: principal_source must be CN when subject_dn has CN= attribute.";
        EXPECT_EQ(result->value, "ACME-PROD-01")
            << "Cell A: value must be the extracted CN.";
    }
}

// Cell B: SAN-DNS wins when CN is absent.
TEST(CompidPrincipalExtractionUnit, CellB_SanDnsWinsOverAbsentCn) {
    fixpp::session::CompIdAuthorizationPolicy policy;
    policy.add_binding("acme-prod.example.com", "ACME01");

    auto pid = make_san_dns_identity("acme-prod.example.com");
    auto result = policy.authorize(pid, "ACME01");

    // RED: stub ignores pid.
    EXPECT_TRUE(result.has_value())
        << "Cell B: SAN-DNS principal must authorize when CN is absent. "
        << "RED (T034 not landed): stub always returns unauthorized.";
    if (result.has_value()) {
        EXPECT_EQ(result->from, fixpp::session::bound_principal::source::SAN_DNS)
            << "Cell B: principal_source must be SAN_DNS.";
        EXPECT_EQ(result->value, "acme-prod.example.com");
    }
}

// Cell C: SAN-URI wins when CN and SAN-DNS are both absent.
TEST(CompidPrincipalExtractionUnit, CellC_SanUriWinsOverAbsentSanDns) {
    fixpp::session::CompIdAuthorizationPolicy policy;
    policy.add_binding("spiffe://cluster.local/ns/prod/sa/acme", "ACME01");

    auto pid = make_san_uri_identity("spiffe://cluster.local/ns/prod/sa/acme");
    auto result = policy.authorize(pid, "ACME01");

    // RED: stub ignores pid.
    EXPECT_TRUE(result.has_value())
        << "Cell C: SAN-URI principal must authorize when CN and SAN-DNS are absent. "
        << "RED (T034 not landed): stub always returns unauthorized.";
    if (result.has_value()) {
        EXPECT_EQ(result->from, fixpp::session::bound_principal::source::SAN_URI)
            << "Cell C: principal_source must be SAN_URI.";
        EXPECT_EQ(result->value, "spiffe://cluster.local/ns/prod/sa/acme");
    }
}

// Cell D: SHA256_FINGERPRINT fallback when no CN and no SANs.
TEST(CompidPrincipalExtractionUnit, CellD_Sha256FingerprintFallback) {
    // Build a known fingerprint and its hex representation.
    std::array<std::byte, 32> fp{};
    for (std::size_t i = 0; i < 32; ++i) {
        fp[i] = static_cast<std::byte>(static_cast<unsigned char>(i + 1));
    }
    const std::string fp_hex = fingerprint_to_hex(fp);

    fixpp::session::CompIdAuthorizationPolicy policy;
    policy.add_binding(fp_hex, "ACME01");

    auto pid = make_fingerprint_only_identity(fp);
    auto result = policy.authorize(pid, "ACME01");

    // RED: stub ignores pid.
    EXPECT_TRUE(result.has_value())
        << "Cell D: SHA-256 fingerprint must authorize when no CN and no SANs. "
        << "RED (T034 not landed): stub always returns unauthorized.";
    if (result.has_value()) {
        EXPECT_EQ(result->from, fixpp::session::bound_principal::source::SHA256_FINGERPRINT)
            << "Cell D: principal_source must be SHA256_FINGERPRINT.";
        EXPECT_EQ(result->value, fp_hex)
            << "Cell D: value must be the 64-char lowercase hex fingerprint.";
    }
}

// ── Session-integration cell: principal_source in session_event_peer_identity_bound ──

class CompidPrincipalExtractionSessionTest : public ::testing::Test {
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

// Session-integration: CN-based extraction → session_event_peer_identity_bound
// with principal_source=CN emitted via recent_events().
//
// RED: T036 event emission not yet wired → event not emitted.
TEST_F(CompidPrincipalExtractionSessionTest, CnExtraction_EmitsBoundEvent_WithCnSource) {
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
    // RC#C (gate-b/r1): bilateral_lenient — cell tests principal extraction, not reset.
    cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;

    // Configure policy: CN "TW-PROD-01" → CompID "TW".
    cfg.compid_authorization_policy.add_binding("TW-PROD-01", "TW");

    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(run_open(sess).has_value());

    // Inject the live handshake identity with CN=TW-PROD-01.
    fixpp::test_support::inject_live_identity(sess, make_cn_only_identity("TW-PROD-01"));

    auto logon = make_logon_frame("FIX.4.2", 1, "TW", "ISLD");
    feed(sess, logon);

    // Session must reach Active.
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "CN matches binding → Active. RED: T036 not yet wired (authorize() not called).";

    // session_event_peer_identity_bound must be emitted.
    const auto* ev = find_peer_identity_bound_event(sess);
    ASSERT_NE(ev, nullptr)
        << "session_event_peer_identity_bound must be in recent_events() on success. "
        << "RED (T036 not landed): event not emitted yet.";
    if (ev != nullptr) {
        EXPECT_EQ(ev->principal_source, fixpp::session::bound_principal::source::CN)
            << "principal_source must be CN.";
        EXPECT_EQ(ev->bound_compid, "TW")
            << "bound_compid must be the matched CompID.";
    }
}
