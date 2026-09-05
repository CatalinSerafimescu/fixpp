// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_session_plaintext_reconnect.cpp — 043 /speckit-verify coverage witness
//
// Witnesses the plaintext RECONNECT branch:
//   * reconnect_fsm.cpp:265-271 — when is_plaintext_, drive_reconnect_attempt()
//     skips Step 6 (dynamic_cast<TlsTransport*> + async_handshake) entirely and
//     hands the freshly-connected transport to Session::install_reconnected_transport
//     with an EMPTY handshake_result{} (connect → Logon, no handshake / no authz).
//   * session.cpp:419 — install_reconnected_transport's live_peer_id_ guard: for
//     insecure_plain_tcp the empty handshake_result must NOT populate live_peer_id_,
//     which therefore stays nullopt (D-10 #2 MUST — no fake peer identity).
//
// This path is unreachable from the single-shot acceptor roundtrip witness
// (test_session_plaintext_roundtrip.cpp, which only does an initial accept), so it
// surfaced as a §IX.1 new-line coverage gap at /speckit-verify (2026-06-18).
//
// Deterministic FSM-level test (NO live socket, NO mock-clock advance): attempt 0
// of drive_reconnect_attempt() skips the inter-attempt backoff timer (Step 1 is
// guarded `if (n > 0)`), and the hand-rolled factory's cert_source_snapshot()
// returns nullptr (no load_credentials), so the coroutine runs
// make → async_connect(mock, succeeds) → plaintext branch → install, synchronously
// under ioc.run_for(). Mirrors the T014-D ReconnectLoopMintsFreshTransport harness
// (test_reconnect_happy_path.cpp). The mock_transport happens to be a TlsTransport,
// which is precisely why the plaintext branch is meaningful: it must reach install
// WITHOUT casting/handshaking, regardless of the concrete transport type.
//
// Mutation-discrimination:
//   * drop the is_plaintext_ guard (reconnect_fsm.cpp:265) → the FSM takes the TLS
//     path, dynamic_cast<TlsTransport*> succeeds on the mock, then async_handshake
//     runs and (with no real TLS peer) the attempt fails → r.has_value() FALSE.
//   * drop the is_insecure_plain_tcp guard (session.cpp:419) → live_peer_id_ is
//     assigned the empty handshake_result{}.peer_id → has_value() TRUE.
//   Either mutation flips an assertion below.

#include <gtest/gtest.h>

#include <asio/any_io_executor.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/reconnect_fsm.hpp>
#include <fixpp/session/security_profile.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/transport/reconnect_policy.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <future>
#include <memory>
#include <memory_resource>
#include <span>
#include <utility>

#define FIXPP_ALLOW_MOCK_TRANSPORT
#include <fixpp/transport/test/mock_transport.hpp>

#include "support/minimal_dictionary.hpp"
#include "support/pump_until_ready.hpp"

// ── #289: bounded pumps ──────────────────────────────────────────────
//
// This file's one census site uses `run_window_then_ready` plus a miss-branch drain
// (tests/support/pump_until_ready.hpp). The window is PRESERVED: the hazard #289
// names is the UNCONDITIONAL `get()`, not the fixed window.
//
// The site label passed to `run_window_then_ready` is the FORCING SEAM: exporting
// FIXPP_FORCE_WINDOW_MISS=<label> makes exactly that site take its miss branch, with
// no source edit and no rebuild. It is a WEAKER witness than textual mutation and
// does not replace it -- see the primitive.
//
// The drain is the CLOCKED one because the config below sets a NON-ZERO
// `heartbeat_interval` against a `mock_clock`: reaching Active co_spawns a liveness
// loop that parks on `sleep_until`, and only `cancel_sleeps()` releases it. `*clock`
// is the test body's own local, declared above the site.
//
// Rationale and the teardown-shape rule live at the primitive, not duplicated here
// (#324).

using namespace std::chrono_literals;

namespace {

// Hand-rolled plaintext-ish factory: make() mints a mock_transport whose
// async_connect succeeds (default Script connect_info). cert_source_snapshot()
// returns nullptr so the rotation-detect step performs no load. kind() reports
// plaintext for honesty (drive_reconnect_attempt does not consult it).
class PlainReconnectFactory final : public fixpp::transport::TransportFactory {
public:
    std::atomic<int> make_call_count{0};

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::transport::Transport>> make(
        asio::any_io_executor exec, fixpp::tls::SslCtxConfig /*ssl_cfg*/,
        std::pmr::memory_resource* /*mr*/) noexcept override {
        ++make_call_count;
        fixpp::transport::test::Script script;  // default connect_info → connect succeeds
        // Discrimination knob: script the handshake to FAIL. The mock IS-A
        // TlsTransport, so if the is_plaintext_ guard were dropped the FSM would
        // dynamic_cast → async_handshake → transport_handshake_failed → the whole
        // attempt loop fails. Success is therefore reachable ONLY via the
        // handshake-SKIP plaintext branch — assertion (b) below discriminates it.
        script.handshake_succeeds = false;
        return std::make_unique<fixpp::transport::test::mock_transport>(std::move(exec),
                                                                        std::move(script));
    }

    [[nodiscard]] fixpp::core::expected_t<void> reload_credentials(
        std::shared_ptr<fixpp::tls::cert_source> /*new_source*/) noexcept override {
        return {};
    }

    [[nodiscard]] std::shared_ptr<fixpp::tls::cert_source> cert_source_snapshot()
        const noexcept override {
        return nullptr;
    }

    [[nodiscard]] fixpp::transport::transport_security_kind kind() const noexcept override {
        return fixpp::transport::transport_security_kind::plaintext;
    }
};

}  // namespace

// The insecure_plain_tcp enumerator is [[deprecated]] (loud-insecure friction,
// SC-005 / D-9); fixpp-internal/test selection wraps the construction site.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

TEST(PlaintextReconnectTest, PlaintextReconnectSkipsHandshakeAndLeavesPeerIdNullopt) {
    asio::io_context ioc;
    auto utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
    auto clock = std::make_shared<fixpp::core::mock_clock>(utc, fixpp::core::steady_time_point{},
                                                           ioc.get_executor());
    fixpp::core::EngineConfig engine{};
    engine.clock = clock;
    engine.executor = ioc.get_executor();

    fixpp::session::SessionConfig cfg;
    cfg.sender_comp_id = "PLAIN-INITIATOR";
    cfg.target_comp_id = "PLAIN-ACCEPTOR";
    cfg.begin_string = "FIX.4.2";
    cfg.heartbeat_interval = 30s;
    cfg.role = fixpp::session::session_role::initiator;
    cfg.executor_override = ioc.get_executor();
    cfg.security_profile =
        fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::insecure_plain_tcp};
    cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    cfg.transport_send = [](std::span<const std::byte>) {};

    fixpp::session::Session sess(engine, cfg);

    auto factory = std::make_shared<PlainReconnectFactory>();
    fixpp::transport::ReconnectPolicy policy;
    policy.max_attempts = 3;
    policy.schedule = {10ms, 20ms, 40ms};

    fixpp::session::ReconnectFsm fsm(factory.get(), std::move(policy), 30s, 2000ms);
    fsm.set_session_owner(&sess);
    fsm.set_transport_factory(factory.get());
    fsm.set_plaintext_profile(true);
    fsm.set_reconnect_endpoint(fixpp::transport::Endpoint{"127.0.0.1", 12345});

    auto fut = asio::co_spawn(ioc, fsm.drive_reconnect_attempt(), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(ioc, fut, 500ms,
                                                    "PlaintextReconnectSkipsHandshake/attempt")) {
        fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                        "PlaintextReconnectSkipsHandshake/attempt");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss
                      << "PlaintextReconnectSkipsHandshake/attempt";
        return;
    }
    auto r = fut.get();

    // (a) A transport was minted (the FSM reached make()).
    EXPECT_GE(factory->make_call_count.load(), 1)
        << "drive_reconnect_attempt() must mint a transport via the factory";

    // (b) The plaintext branch succeeded WITHOUT a TLS handshake. If the
    //     is_plaintext_ guard were dropped, the FSM would dynamic_cast the mock
    //     to TlsTransport (succeeds) and run async_handshake, which fails with no
    //     real peer → r would be an error.
    ASSERT_TRUE(r.has_value())
        << "plaintext reconnect must succeed via the handshake-skip branch "
           "(connect → install, no async_handshake)";

    // (c) D-10 #2 MUST: a plaintext reconnect installs an empty handshake_result{},
    //     so live_peer_id_ stays nullopt — no fake peer identity.
    EXPECT_FALSE(sess.live_peer_id_has_value_for_test())
        << "insecure_plain_tcp reconnect must leave live_peer_id_ == nullopt "
           "(D-10 #2; session.cpp:419 guard)";
}

#pragma clang diagnostic pop  // -Wdeprecated-declarations (insecure_plain_tcp)
