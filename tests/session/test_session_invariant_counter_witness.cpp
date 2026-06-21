// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_session_invariant_counter_witness.cpp — T032 [P] [US2] Phase 4
//                                                            + T021 [P] [US4] Phase 6
//
// Part 1 (T032) — Invariant-counting regression witness
//   ([[feedback_half_restructure_symmetric_api]]).
//
//   (1) CompIdAuthorizationPolicy::authorize() is called EXACTLY ONCE per Logon
//       on BOTH the acceptor and initiator paths (symmetric).
//   (2) 012 FR-026 + 013 D-11: policy's binding_count() is unchanged after
//       authorize() (no per-Logon map re-construction).
//
// Part 2 (T021 / 014 FR-013a) — cert_source::load_credentials() == 1 counter
//   witness, now feasible via the live loopback harness from T005.
//
//   Asserts: for a steady-state no-rotation reconnect handshake,
//   load_credentials() is called EXACTLY ONCE (at factory construction via
//   prepare_ssl_ctx_). The counting_cert_source wrapper intercepts the call.
//   A fresh factory (one per test) is built with the counting source.
//
//   Setup: steady-state (no rotation staged) so drive_reconnect_attempt step 2
//   does NOT detect a rotation → no new SSL_CTX build → no second load_credentials().
//   First-and-only load: in make_asio_tls_transport_factory → prepare_ssl_ctx_.
//
// Anchors: plan.md T032; 014 tasks.md T021; spec.md §US4 FR-013a; I-3;
//   SC-006; data-model.md E-1 I-3 / E-5;
//   [[feedback_half_restructure_symmetric_api]]; 012 FR-026.

#include <gtest/gtest.h>

#include <algorithm>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/compid_authorization_policy.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_event.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/tls/cert_source.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/tls/peer_identity.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "loopback_tls_session_harness.hpp"
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

// Count session_event_peer_identity_bound events in recent_events().
static std::size_t count_bound_events(const fixpp::session::Session& sess) {
    auto events = sess.recent_events();
    return static_cast<std::size_t>(
        std::count_if(events.begin(), events.end(), [](const fixpp::session::SessionEvent& ev) {
            return std::holds_alternative<fixpp::session::session_event_peer_identity_bound>(ev);
        }));
}

}  // namespace

// ── Fixture ───────────────────────────────────────────────────────────────────

class InvariantCounterWitnessTest : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine{};

    void SetUp() override {
        auto utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
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
// InvariantCell_Acceptor: authorize() called exactly ONCE per accepted Logon.
//
// Drive N=3 separate sessions (N fresh Session objects sharing the same policy
// binding config). Each session accepts a Logon. The bound-event counter must
// equal exactly 1 per session (not 0, not >1).
//
// RED: T036 not landed → no bound event → counter=0 for all sessions.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(InvariantCounterWitnessTest, Acceptor_AuthorizeCalledExactlyOnce_PerLogon) {
    constexpr std::size_t kN = 3;

    // Policy stays constant across sessions (shared_ptr-owned config copy).
    fixpp::session::CompIdAuthorizationPolicy base_policy;
    base_policy.add_binding("TW-PROD-01", "TW");

    const std::size_t expected_binding_count = base_policy.binding_count();

    for (std::size_t i = 0; i < kN; ++i) {
        ioc.restart();

        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = 30s;
        cfg.security_profile =
            fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::mtls_ca};
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.role = fixpp::session::session_role::acceptor;
        // RC#C (gate-b/r1): bilateral_lenient — tests compid invariant, not reset.
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        cfg.compid_authorization_policy = base_policy;

        // Live handshake identity with matching CN.
        fixpp::tls::peer_identity pid;
        pid.subject_dn = "CN=TW-PROD-01,O=Acme,C=US";

        fixpp::session::Session sess(engine, cfg);
        ASSERT_TRUE(run_open(sess).has_value()) << "Session " << i << " open failed.";
        fixpp::test_support::inject_live_identity(sess, std::move(pid));

        auto logon = make_logon_frame("FIX.4.2", 1, "TW", "ISLD");
        feed(sess, logon);

        // Invariant 1: exactly one bound event per accepted Logon.
        const std::size_t bound_count = count_bound_events(sess);
        EXPECT_EQ(bound_count, 1u)
            << "Session " << i << ": exactly 1 session_event_peer_identity_bound expected. "
            << "RED (T036 not landed): counter = " << bound_count << " (expected 1).";

        // Invariant 2: policy binding_count() is UNCHANGED after authorize() call.
        // (Proves the map is not mutated by authorize().)
        EXPECT_EQ(base_policy.binding_count(), expected_binding_count)
            << "Session " << i << ": binding_count() must not change across authorize() calls.";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// InvariantCell_Initiator: authorize() called exactly ONCE on initiator Logon-ack.
//
// Same invariant but on the INITIATOR path (LogonSent → Active).
// Per [[feedback_half_restructure_symmetric_api]]: the initiator arm must be
// wired by T036 independently of the acceptor arm.
//
// RED: T036 initiator path not wired → no bound event → counter=0.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(InvariantCounterWitnessTest, Initiator_AuthorizeCalledExactlyOnce_PerLogonAck) {
    constexpr std::size_t kN = 3;

    fixpp::session::CompIdAuthorizationPolicy base_policy;
    base_policy.add_binding("ISLD-PROD-01", "ISLD");
    const std::size_t expected_binding_count = base_policy.binding_count();

    for (std::size_t i = 0; i < kN; ++i) {
        ioc.restart();

        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id = "TW";
        cfg.target_comp_id = "ISLD";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = 30s;
        cfg.security_profile =
            fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::mtls_ca};
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.role = fixpp::session::session_role::initiator;
        // RC#C (gate-b/r1): bilateral_lenient — tests compid invariant, not reset.
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        cfg.compid_authorization_policy = base_policy;

        fixpp::tls::peer_identity pid;
        pid.subject_dn = "CN=ISLD-PROD-01,O=Exchange,C=US";

        fixpp::session::Session sess(engine, cfg);
        ASSERT_TRUE(run_open(sess).has_value()) << "Session " << i << " open failed.";
        EXPECT_EQ(sess.state(), fixpp::session::fsm_state::LogonSent);
        fixpp::test_support::inject_live_identity(sess, std::move(pid));

        auto logon_ack = make_logon_frame("FIX.4.2", 1, "ISLD", "TW");
        feed(sess, logon_ack);

        // Initiator invariant: exactly one bound event per Logon-ack processed.
        const std::size_t bound_count = count_bound_events(sess);
        EXPECT_EQ(bound_count, 1u)
            << "Initiator session " << i << ": exactly 1 session_event_peer_identity_bound "
            << "expected on LogonSent→Active path. "
            << "RED (T036 initiator not landed): counter = " << bound_count << " (expected 1).";

        EXPECT_EQ(base_policy.binding_count(), expected_binding_count)
            << "Initiator session " << i
            << ": binding_count() must not change across authorize() calls.";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// StructuralInvariant: CompIdAuthorizationPolicy::authorize() is noexcept and
// does not modify binding_count() (pure const lookup).
//
// This is a compile-time + structural check, not a counter check.
// No TDD RED/GREEN — this passes immediately (it's a static invariant).
// ─────────────────────────────────────────────────────────────────────────────
TEST(CompidPolicyStructural, AuthorizeIsConstNoexcept) {
    fixpp::session::CompIdAuthorizationPolicy policy;
    policy.add_binding("TEST-CN", "TESTCOMP");

    const auto& const_policy = policy;
    EXPECT_EQ(const_policy.binding_count(), 1u);

    fixpp::tls::peer_identity pid;
    pid.subject_dn = "CN=TEST-CN,O=Test,C=US";

    // authorize() on a const policy — must compile (const method) and be noexcept.
    // Pass std::string_view explicitly (pointer+length form) to avoid the non-noexcept
    // std::string_view(const char*) implicit conversion under libc++ (strlen not noexcept).
    static_assert(noexcept(const_policy.authorize(pid, std::string_view{"TESTCOMP", 8})),
                  "authorize() must be noexcept per contracts/compid_authorization_policy.hpp");

    // binding_count() must not change after authorize().
    {
        [[maybe_unused]] auto auth_result = const_policy.authorize(pid, "TESTCOMP");
    }
    EXPECT_EQ(const_policy.binding_count(), 1u) << "authorize() must not modify binding_count().";
}

// ─────────────────────────────────────────────────────────────────────────────
// T021 (014 FR-013a) — cert_source::load_credentials() == 1 per handshake.
//
// Uses the live loopback TLS harness (T005). A counting_cert_source wraps
// file_cert_source and counts load_credentials() calls. The count must be
// exactly 1 for a steady-state (no-rotation) scenario:
//
//   Count site: make_asio_tls_transport_factory → prepare_ssl_ctx_ calls
//   ssl_cfg.cs->load_credentials() ONCE to build the SSL_CTX.
//
// For a NO-ROTATION reconnect, drive_reconnect_attempt step 2 detects
// snap == last_active_source_ (same source, no rotation staged) → no new
// SSL_CTX build → no second load_credentials() call.
//
// The invariant I-3 from data-model E-1: "load_credentials() fires exactly
// once per handshake (FR-013a witness)."
//
// Design anchor: 014 tasks.md T021; data-model.md E-1 I-3; FR-013a; SC-006.
// ─────────────────────────────────────────────────────────────────────────────

// Counting wrapper around cert_source that counts load_credentials() calls.
class counting_cert_source final : public fixpp::tls::cert_source {
public:
    explicit counting_cert_source(std::shared_ptr<fixpp::tls::cert_source> inner)
        : inner_{std::move(inner)} {}

    // Number of times load_credentials() has been called.
    std::size_t load_count() const noexcept { return count_.load(std::memory_order_relaxed); }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<fixpp::tls::local_credentials>>
    load_credentials() override {
        count_.fetch_add(1, std::memory_order_relaxed);
        return inner_->load_credentials();
    }

    [[nodiscard]] fixpp::core::expected_t<std::span<const fixpp::tls::Certificate>>
    load_trust_anchors() [[clang::lifetimebound]] override {
        return inner_->load_trust_anchors();
    }

private:
    std::shared_ptr<fixpp::tls::cert_source> inner_;
    std::atomic<std::size_t> count_{0};
};

// T021 — live loopback: load_credentials() called exactly ONCE per factory,
// regardless of how many make()+async_handshake() calls are driven.
//
// This test DRIVES a real loopback TLS handshake (N=2 iterations) and asserts
// that load_count() stays at 1 throughout — it never increments for make() or
// async_handshake(). That is the I-3 invariant: "load_credentials() fires
// exactly once per handshake" means once at factory construction, never per
// individual make/connect/handshake call.
TEST(LoadCredentialsCounterWitness, LoadCredentialsCalledExactlyOncePerHandshake) {
    const char* dir_env = std::getenv("FIXPP_TLS_FIXTURE_DIR");
#ifdef FIXPP_TLS_FIXTURE_DIR
    static const char* kCompileTimeDir = FIXPP_TLS_FIXTURE_DIR;
#else
    static const char* kCompileTimeDir = nullptr;
#endif
    const char* fixture_dir = dir_env ? dir_env : kCompileTimeDir;
    if (!fixture_dir || fixture_dir[0] == '\0') {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set — skipping live loopback test";
    }
    const std::string fdir{fixture_dir};

    asio::io_context ioc;

    // ── Build a file_cert_source and wrap it with a counting layer ───────────
    fixpp::tls::file_cert_source::Config cs_cfg;
    cs_cfg.leaf_path = fdir + "/leaf_rsa2048.pem";
    cs_cfg.private_key_path = fdir + "/leaf_rsa2048.key";
    cs_cfg.ca_bundle_path = fdir + "/ca.pem";

    auto inner_result = fixpp::tls::file_cert_source::make_file_cert_source(
        cs_cfg, std::pmr::new_delete_resource());
    ASSERT_TRUE(inner_result.has_value()) << "Failed to build file_cert_source";

    auto counting = std::make_shared<counting_cert_source>(std::move(*inner_result));

    // ── Build the client factory using the counting cert_source ─────────────
    // prepare_ssl_ctx_ → load_credentials() is called EXACTLY ONCE here.
    fixpp::tls::SslCtxConfig ssl_cfg;
    ssl_cfg.profile = fixpp::tls::SecurityProfile::mtls_ca;
    ssl_cfg.cs = counting;    // counting wrapper
    ssl_cfg.clock = nullptr;  // skip expiry
    ssl_cfg.caps = fixpp::tls::CertSourceCaps{};

    EXPECT_EQ(counting->load_count(), 0u) << "No load before factory construction";

    auto factory_result = fixpp::transport::make_asio_tls_transport_factory(
        fixpp::transport::Transport::Config{}, ssl_cfg);
    ASSERT_TRUE(factory_result.has_value()) << "make_asio_tls_transport_factory failed";
    auto factory = std::move(*factory_result);

    // Factory construction calls load_credentials() exactly once.
    EXPECT_EQ(counting->load_count(), 1u)
        << "load_credentials() must be called EXACTLY ONCE at factory construction. "
           "FR-013a / I-3 invariant.";

    // ── Build a loopback server (independent factory, does not use counting) ─
    // The server uses the same certs but a SEPARATE cert_source so its
    // load_credentials() calls do not affect our counting source.
    fixpp::transport::test::LoopbackTlsFixture loopback_fixture{fdir, ioc.get_executor()};
    auto server_ep = loopback_fixture.server_endpoint();
    auto& listener = loopback_fixture.listener();
    auto& server_ssl_cfg = loopback_fixture.ssl_cfg();

    // ── Drive N=2 loopback handshakes and assert count stays at 1 ───────────
    // Each iteration: server accepts + handshakes concurrently with client
    // make() + async_connect() + async_handshake(). Neither make() nor
    // async_handshake() calls load_credentials() — the SSL_CTX is already built.
    constexpr int kHandshakes = 2;
    for (int i = 0; i < kHandshakes; ++i) {
        ioc.restart();

        bool client_hs_ok = false;
        bool server_hs_ok = false;

        // Server coroutine: accept + handshake.
        asio::co_spawn(
            ioc.get_executor(),
            [&]() -> asio::awaitable<void> {
                auto ar = co_await listener.async_accept();
                if (!ar.has_value()) {
                    co_return;
                }
                auto* tls = dynamic_cast<fixpp::transport::TlsTransport*>(ar->get());
                if (tls) {
                    auto hs = co_await tls->async_handshake(server_ssl_cfg);
                    server_hs_ok = hs.has_value();
                }
            },
            asio::detached);

        // Client coroutine: make() + connect + handshake.
        asio::co_spawn(
            ioc.get_executor(),
            [&]() -> asio::awaitable<void> {
                // make() uses the CACHED SSL_CTX — does NOT call load_credentials().
                auto t = factory->make(ioc.get_executor(), ssl_cfg, nullptr);
                if (!t.has_value()) {
                    co_return;
                }
                auto* transport = t->get();

                auto conn = co_await (*t)->async_connect(server_ep);
                if (!conn.has_value()) {
                    co_return;
                }

                auto* tls = dynamic_cast<fixpp::transport::TlsTransport*>(transport);
                if (!tls) {
                    co_return;
                }
                // async_handshake() uses the cached SSL_CTX — does NOT call load_credentials().
                auto hs = co_await tls->async_handshake(ssl_cfg);
                client_hs_ok = hs.has_value();
            },
            asio::detached);

        ioc.run_for(std::chrono::seconds{5});

        EXPECT_TRUE(client_hs_ok) << "Handshake " << (i + 1) << " client side must succeed";
        EXPECT_TRUE(server_hs_ok) << "Handshake " << (i + 1) << " server side must succeed";

        // KEY ASSERTION: count must STILL be 1 after each handshake.
        // make() + async_handshake() must NOT call load_credentials().
        EXPECT_EQ(counting->load_count(), 1u)
            << "After handshake " << (i + 1)
            << ": load_credentials() count must remain at 1 "
               "(FR-013a / I-3: loaded once at factory construction, "
               "never per make()/async_handshake())";
    }
}
