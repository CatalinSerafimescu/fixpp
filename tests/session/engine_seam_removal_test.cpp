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
#include <filesystem>
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
#include <fstream>
#include <future>
#include <span>
#include <string>
#include <vector>

#include "support/identity_injecting_transport.hpp"
#include "support/minimal_dictionary.hpp"
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
// ⚠️ THE MISS BRANCH TAKES `drain_or_report`, AND NOT MERELY BECAUSE NO `Clock&` IS
// IN SCOPE. The fixture's `engine` is a `fixpp::core::EngineConfig`, whose `clock` is a
// DATA MEMBER (`include/fixpp/core/engine_config.hpp`) and is never assigned anywhere in
// this file. So `cancel_and_drain_or_report(ioc, *engine.clock, ...)` compiles and then
// dereferences a null `shared_ptr` ON THE MISS PATH -- a fault inside a failure handler,
// which is the one place it is least likely to be diagnosed correctly. `drain_or_report`
// is required here, not just permitted.
// (`Engine::clock()` -- the accessor, not this member -- belongs to
// `fixpp::session::Engine`, a type this file never names. Writing `engine.clock()` here
// would be a compile error, which is the SAFE failure; the member spelling is the silent
// one, and is why this is worth a comment at all.)
//
// Rationale and the teardown-shape rule live at the primitive, not duplicated here
// (#324).

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

// ── (1) Source-scan gate ────────────────────────────────────────────────────
TEST(EngineSeamRemoval, NoSeamReferenceInAnyTree) {
    // Assemble the needle from fragments so this file holds no contiguous match.
    const std::string needle = std::string{"logon_peer"} + "_identity_override";
    const std::string root = FIXPP_TEST_SOURCE_DIR;

    // Portable recursive source scan (mirrors `grep -rn` over src/include/tests;
    // no shell, so it runs identically on every platform).
    namespace fs = std::filesystem;
    std::string out;
    for (const char* sub : {"/src", "/include", "/tests"}) {
        const fs::path base = root + sub;
        std::error_code ec;
        if (!fs::exists(base, ec)) continue;
        for (fs::recursive_directory_iterator it{base, ec}, end; it != end && !ec;
             it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            std::ifstream ifs(it->path());
            if (!ifs) continue;
            std::string line;
            int lineno = 0;
            while (std::getline(ifs, line)) {
                ++lineno;
                if (line.find(needle) != std::string::npos) {
                    out += it->path().string() + ":" + std::to_string(lineno) + ": " + line + "\n";
                }
            }
        }
    }

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
        // Replaces an explicit `wait_for(0s) == ready` assertion. ⚠️ NOT the same predicate
        // at the same point -- that wording was here and was wrong. The assertion fired the
        // instant the window returned; the primitive re-checks after ONE MORE `kPumpSlice`
        // grace pump and a second `restart()`, so a future that becomes ready just after the
        // window now passes where the assertion failed. That boundary grace is deliberate and
        // is what every migrated site gets. Normalisation, not a hazard fix.
        if (!fixpp::test_support::run_window_then_ready(ioc, open_fut, 500ms,
                                                        "LiveIdentityDrivesAuthorization/open")) {
            fixpp::test_support::drain_or_report(ioc, "LiveIdentityDrivesAuthorization/open");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "LiveIdentityDrivesAuthorization/open";
            return;
        }
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
        if (!fixpp::test_support::run_window_then_ready(
                ioc, feed_fut, 500ms, "LiveIdentityDrivesAuthorization/logon-ack")) {
            fixpp::test_support::drain_or_report(ioc, "LiveIdentityDrivesAuthorization/logon-ack");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "LiveIdentityDrivesAuthorization/logon-ack";
            return;
        }
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
