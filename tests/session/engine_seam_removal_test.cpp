// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/engine_seam_removal_test.cpp — 015 T019 [US4]
//
// SC-006 / FR-009 proof, in two parts:
//
//  (1) Grep gate — zero occurrences of the removed per-config peer-identity test
//      seam identifier anywhere under src/ + include/ + tests/. The needle is
//      assembled from fragments at runtime so THIS file contains no contiguous
//      occurrence to trip its own gate (no self-exclude needed).
//
//  (2) Live-binding witness — an mTLS session whose live handshake identity (set
//      via the production attach primitive, exactly as the engine accept loop
//      does) is on the policy reaches Active and emits peer_identity_bound. The
//      authorize() decision is therefore driven by the LIVE identity, not a
//      config seam (which no longer exists). The full real-loopback-TLS witness
//      is test_live_identity_binding.cpp's LiveTlsCertCnDrivesAuthorizationDecision.
//
// Anchors: FR-009; SC-006; C4/E-6; data-model §E-2/E-4.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/session/compid_authorization_policy.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_event.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/tls/peer_identity.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <future>
#include <span>
#include <string>
#include <vector>

#include "support/identity_injecting_transport.hpp"
#include "support/minimal_dictionary.hpp"

#ifndef FIXPP_TEST_SOURCE_DIR
#error "FIXPP_TEST_SOURCE_DIR must be defined (set by tests/session/CMakeLists.txt)"
#endif

using namespace std::chrono_literals;

namespace {

static std::string fix_field(int tag, std::string_view val) {
    return std::to_string(tag) + "=" + std::string(val) + "\x01";
}

static std::vector<std::byte> make_logon_frame(std::string_view begin_string, std::uint32_t seq,
                                               std::string_view sender, std::string_view target) {
    std::string body;
    body += fix_field(35, "A");
    body += fix_field(34, std::to_string(seq));
    body += fix_field(49, sender);
    body += fix_field(56, target);
    body += fix_field(98, "0");
    body += fix_field(108, "30");

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

class MinimalTransportFactory final : public fixpp::transport::TransportFactory {
public:
    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::transport::Transport>> make(
        asio::any_io_executor, fixpp::tls::SslCtxConfig,
        std::pmr::memory_resource*) noexcept override {
        return std::unexpected{fixpp::core::error::transport_factory_failed};
    }
    [[nodiscard]] fixpp::core::expected_t<void> reload_credentials(
        std::shared_ptr<fixpp::tls::cert_source>) noexcept override {
        return {};
    }
    [[nodiscard]] std::shared_ptr<fixpp::tls::cert_source> cert_source_snapshot()
        const noexcept override {
        return nullptr;
    }
};

// ── (1) Grep gate ───────────────────────────────────────────────────────────
TEST(EngineSeamRemoval, NoSeamReferenceInAnyTree) {
    // Assemble the needle from fragments so this file holds no contiguous match.
    const std::string needle = std::string{"logon_peer"} + "_identity_override";
    const std::string root = FIXPP_TEST_SOURCE_DIR;
    const std::string cmd = "grep -rn -- '" + needle +
                            "' "
                            "'" +
                            root + "/src' '" + root + "/include' '" + root + "/tests' 2>/dev/null";

    FILE* pipe = ::popen(cmd.c_str(), "r");
    ASSERT_NE(pipe, nullptr) << "popen(grep) failed";

    std::string out;
    char buf[512];
    while (std::fgets(buf, sizeof(buf), pipe) != nullptr) {
        out += buf;
    }
    ::pclose(pipe);  // grep exits 1 (no match) when clean — status is not the gate

    EXPECT_TRUE(out.empty())
        << "SC-006 / FR-009: the per-config peer-identity test seam must have ZERO "
        << "references across src/ + include/ + tests/. Offending lines:\n"
        << out;
}

// ── (2) Live-binding witness ─────────────────────────────────────────────────
TEST(EngineSeamRemoval, LiveIdentityDrivesAuthorization) {
    asio::io_context ioc;
    fixpp::core::EngineConfig engine{};
    engine.executor = ioc.get_executor();

    fixpp::session::CompIdAuthorizationPolicy policy;
    policy.add_binding("PEER-PROD-01", "ACCEPTOR");

    fixpp::session::SessionConfig cfg;
    cfg.sender_comp_id = "INITIATOR";
    cfg.target_comp_id = "ACCEPTOR";
    cfg.begin_string = "FIX.4.2";
    cfg.heartbeat_interval = std::chrono::seconds{30};
    cfg.logout_disconnect_timeout_ms = 2000;
    cfg.role = fixpp::session::session_role::initiator;
    cfg.executor_override = ioc.get_executor();
    cfg.security_profile =
        fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::mtls_ca};
    cfg.compid_authorization_policy = std::move(policy);
    cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    cfg.transport_factory_override = std::make_shared<MinimalTransportFactory>();
    cfg.transport_send = [](std::span<const std::byte>) noexcept {};

    fixpp::session::Session session{engine, cfg};

    {
        auto open_fut = asio::co_spawn(ioc, session.open(), asio::use_future);
        ioc.run_for(500ms);
        ioc.restart();
        ASSERT_EQ(open_fut.wait_for(0s), std::future_status::ready);
        ASSERT_TRUE(open_fut.get().has_value());
    }
    ASSERT_EQ(session.state(), fixpp::session::fsm_state::LogonSent);

    // Drive identity through the PRODUCTION live_peer_id_ path (no config seam).
    fixpp::tls::peer_identity pid;
    pid.subject_dn = "CN=PEER-PROD-01";
    fixpp::test_support::inject_live_identity(session, std::move(pid));

    auto logon_ack = make_logon_frame("FIX.4.2", 1, "ACCEPTOR", "INITIATOR");
    {
        auto feed_fut = asio::co_spawn(
            ioc, session.on_inbound_frame(std::span<const std::byte>{logon_ack}), asio::use_future);
        ioc.run_for(500ms);
        ioc.restart();
        ASSERT_EQ(feed_fut.wait_for(0s), std::future_status::ready);
        (void)feed_fut.get();
    }

    EXPECT_EQ(session.state(), fixpp::session::fsm_state::Active)
        << "On-list live handshake identity must drive authorize() → Active "
        << "(arm 1-live), proving the gate binds the live identity, not a seam.";

    bool bound = false;
    for (const auto& ev : session.recent_events()) {
        if (std::holds_alternative<fixpp::session::session_event_peer_identity_bound>(ev)) {
            bound = true;
            break;
        }
    }
    EXPECT_TRUE(bound) << "peer_identity_bound must be emitted from the live-identity arm.";
}

}  // namespace
