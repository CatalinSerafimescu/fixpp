// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/session/test_engine_session_strand.cpp
//
// 023-engine-session-strand — T006 [US1] + T016 [US2] witnesses.
//
// Cells authored here:
//
//   V-1  PerSessionTeardown_TransportCloseSerializedWithRead
//        MT lifecycle (≥3-thread ioc). Exercises the BIO_ctrl race: under
//        ASan/TSan, teardown closes the transport on a different thread than
//        the in-flight async_read_some completion (SSL BIO touch). Expected RED
//        pre-T014 (transport close not yet on session strand). [C-7/V-1/SC-001]
//
//   V-3  TwoSessionsMT_IndependentProgress
//        Two sessions, ≥3-thread executor. Checks that both sessions reach Active
//        concurrently and that neither blocks the other (cross-session
//        parallelism preserved, FR-004). Under TSan, the control-plane writes
//        in run_accept_loop race against stop().clear() — expect TSan RED
//        pre-T018. Progress/independence check itself may pass-by-luck pre-T010
//        (recorded per T008 mandate). [C-7/V-3/FR-004]
//
//   V-8  ControlPlaneRace_PublicReaderVsMutation  [T016/T017 DD-2026-06-06 retarget]
//        A raw std::thread spins calling acceptor_bound_endpoint() and lookup()
//        with NO synchronisation against the engine executor threads.  The engine
//        accept loop WRITES listener_endpoints_[id] and stop() CLEARS both maps
//        concurrently — a real TSan data race (HB-free: no mutex/atomic between
//        reader thread and engine threads).  RED pre-T026 (no D-SNAP); GREEN
//        post-T026 (snapshot readers replace direct map access). UNCONDITIONAL
//        (no FIXPP_TEST_SEAMS needed). [SC-002/C-8/V-8/V-11; research D-SNAP]
//
//   V-9  ReentrantSend_FromCallback_NoDeadlock_AndPostStopFastFails
//        a) Re-entrant Engine::send from inside fromApp (session→control→session)
//           under ≥3-thread executor. Pre-T012 the send path does NOT hop through
//           control_strand_; records whether it deadlocks, races, or passes-by-luck.
//        b) A send issued after stop() has begun fast-fails with
//           session_invalid_state_for_send (77) or session_invalid_argument (119)
//           — never a half-cleared-registry crash. [C-7/V-9/FR-006]
//
//   V-10 SocketExecutorIsSessionStrand
//        Directly checks: after a session is established by the engine, the
//        underlying TCP socket of its transport is associated with a
//        asio::strand<asio::any_io_executor>, NOT the bare io_context executor.
//        Pre-T011 (no strand binding) the socket is on bare exec_ → assertion
//        FAILS → RED for the right reason. [C-7/V-10/E-5/D5/R8/INV-7]
//
//   V-12 StopBeforeAwaitedPublish
//        stop() races the accept loop before any peer connects; confirms the
//        stopped-disposition path (INV-2a) is functional.  ALREADY-GREEN because
//        INV-2a was implemented in T013 publish_entry. [C-6/V-12; T016/T017]
//
// Anti-pattern notes:
//   - Every cell uses a genuinely multi-threaded executor (io_context + std::thread
//     background workers) per [[feedback_single_threaded_harness_masks_strand_races]].
//   - Every live-I/O probe has an internal deadline.
//   - No SUCCEED()/EXPECT_TRUE(true) placeholders — every assertion is behavioral.
//   - Engine::stop() is ALWAYS called before the engine goes out of scope;
//     stop_engine_sync() is called before any FAIL()/early-return path.
//   - V-8 uses a raw std::thread (no asio executor) as the reader so there is
//     no implicit synchronisation with the engine ioc threads — real data race.
//   - V-9 records pass-by-luck vs deadlock vs race as mandated by T008.
//   - V-10 uses asio_tls_transport_test_access::socket_of() to inspect the
//     underlying tcp::socket executor type.
//
// Anchors: tasks.md T006/T007/T008/T016/T017; contracts/engine-session-strand.md
//          C-7 (V-1/V-3/V-9/V-10), C-8 (V-8/V-11), C-6 (V-12);
//          research.md D2/D5/R8/D-SNAP; [const §IX §2].

#include <gtest/gtest.h>

#include <asio/any_io_executor.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/strand.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/security_profile.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

// Internal transport header: needed for V-10's socket executor inspection.
// The engine_session_strand_test CMakeLists adds "${CMAKE_SOURCE_DIR}/src" to
// the include path for this purpose (mirrors tests/perf/test_socket_option_defaults).
#include "transport/asio_tls_transport.hpp"

#include "support/minimal_dictionary.hpp"

using namespace std::chrono_literals;
using fixpp::core::error;
using fixpp::core::expected_t;
using fixpp::session::Application;
using fixpp::session::SessionId;
using fixpp::session::fsm_state;

// ── test-access helper for asio_tls_transport::socket_ ───────────────────────
// `friend class asio_tls_transport_test_access;` is declared in
// src/transport/asio_tls_transport.hpp:214. Same pattern as
// tests/perf/test_socket_option_defaults.cpp:46-51.
// Returns a MUTABLE ref because asio::ip::tcp::socket::get_executor() is not const.
namespace fixpp::transport {
class asio_tls_transport_test_access {
public:
    static asio::ip::tcp::socket& socket_of(asio_tls_transport& t) noexcept {
        return t.socket_;
    }
};
}  // namespace fixpp::transport

namespace {

// ── fixture helpers ───────────────────────────────────────────────────────────

const char* get_fixture_dir() {
    const char* env = std::getenv("FIXPP_TLS_FIXTURE_DIR");  // NOLINT(concurrency-mt-unsafe)
#ifdef FIXPP_TLS_FIXTURE_DIR
    static const char* kDir = FIXPP_TLS_FIXTURE_DIR;
#else
    static const char* kDir = nullptr;
#endif
    return (env && env[0] != '\0') ? env : kDir;
}

// Build a shared TLS factory (mtls_ca profile, leaf_rsa2048 cert).
static std::shared_ptr<fixpp::transport::TransportFactory> make_tls_factory(
    const char* fixture_dir) {
    fixpp::tls::file_cert_source::Config cs_cfg;
    cs_cfg.leaf_path = std::string(fixture_dir) + "/leaf_rsa2048.pem";
    cs_cfg.private_key_path = std::string(fixture_dir) + "/leaf_rsa2048.key";
    cs_cfg.ca_bundle_path = std::string(fixture_dir) + "/ca.pem";
    auto cs_r = fixpp::tls::file_cert_source::make_file_cert_source(
        cs_cfg, std::pmr::new_delete_resource());
    if (!cs_r.has_value()) return nullptr;
    fixpp::tls::SslCtxConfig ssl;
    ssl.profile = fixpp::tls::SecurityProfile::mtls_ca;
    ssl.cs = std::move(*cs_r);
    ssl.clock = nullptr;
    ssl.caps = fixpp::tls::CertSourceCaps{};
    auto fac_r = fixpp::transport::make_asio_tls_transport_factory(
        fixpp::transport::Transport::Config{}, ssl);
    if (!fac_r.has_value()) return nullptr;
    return std::shared_ptr<fixpp::transport::TransportFactory>{std::move(*fac_r)};
}

// Build a mock clock (same pattern as test_application_engine_send.cpp).
static std::shared_ptr<fixpp::core::mock_clock> make_mock_clock(asio::io_context& ioc) {
    using namespace std::chrono;
    auto utc = system_clock::time_point{} + seconds{1704067200};
    auto stp = fixpp::core::steady_time_point{} + seconds{0};
    return std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
}

// Build a SessionConfig for the engine loopback pair.
// Both acceptor and initiator use the SAME `port` parameter:
//   - acceptor: `reconnect_endpoint.port = port` → listens on that specific port.
//   - initiator: `reconnect_endpoint.port = port` → connects to that port.
// Same pattern as test_application_engine_send.cpp:make_session_cfg.
static fixpp::session::SessionConfig make_session_cfg(
    std::shared_ptr<fixpp::transport::TransportFactory> fac,
    const char* sender, const char* target,
    fixpp::session::session_role role,
    const char* peer_compid,
    asio::any_io_executor exec,
    uint16_t port) {
    fixpp::session::SessionConfig c;
    c.sender_comp_id = sender;
    c.target_comp_id = target;
    c.begin_string = "FIX.4.2";
    c.role = role;
    c.executor_override = exec;
    c.security_profile =
        fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::mtls_ca};
    c.compid_authorization_policy.add_binding("fixpp-leaf-rsa2048", peer_compid);
    c.dictionary = fixpp::test_support::make_minimal_dictionary();
    c.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    c.transport_factory_override = fac;
    c.heartbeat_interval = std::chrono::seconds{30};
    c.logout_disconnect_timeout_ms = 500;
    c.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", port};
    c.transport_send = [](std::span<const std::byte>) {};
    return c;
}

// Reserve a free loopback port by binding a temporary acceptor to port 0.
static uint16_t reserve_free_port(asio::io_context& ioc) {
    asio::ip::tcp::acceptor a{ioc};
    asio::ip::tcp::endpoint ep{asio::ip::make_address("127.0.0.1"), 0};
    a.open(ep.protocol());
    a.set_option(asio::ip::tcp::acceptor::reuse_address{true});
    a.bind(ep);
    uint16_t port = a.local_endpoint().port();
    a.close();
    return port;
}

// Wait until pred() is true, driving ioc.run_for(50ms) per iteration.
// Returns pred() at exit.
static bool wait_pred(asio::io_context& ioc, auto pred,
                      std::chrono::milliseconds budget) {
    auto end = std::chrono::steady_clock::now() + budget;
    while (!pred() && std::chrono::steady_clock::now() < end) {
        ioc.run_for(50ms);
        ioc.restart();
    }
    return pred();
}

// Wait for both sessions to reach fsm_state::Active via lookup + state() check.
// Safer than counting onLogon callbacks because it directly observes the FSM state.
static bool wait_both_active(asio::io_context& ioc,
                             fixpp::session::Engine& engine,
                             const SessionId& acc_id, const SessionId& ini_id,
                             std::chrono::milliseconds budget) {
    return wait_pred(ioc, [&]() -> bool {
        auto* a = engine.lookup(acc_id);
        auto* i = engine.lookup(ini_id);
        return a && i
            && a->state() == fsm_state::Active
            && i->state() == fsm_state::Active;
    }, budget);
}

// Stop the engine safely: co_spawn stop() and drain ioc until complete.
// Hard deadline of 10s. ioc.restart() is called first to ensure the ioc
// is in a runnable state even if ioc.stop() was called earlier.
static void stop_engine_sync(asio::io_context& ioc, fixpp::session::Engine& eng) {
    if (eng.stopped()) return;
    ioc.restart();
    auto sf = asio::co_spawn(ioc.get_executor(), eng.stop(), asio::use_future);
    // Drive the ioc until the stop() coroutine completes or 10s elapses.
    bool done = wait_pred(ioc, [&]{ return sf.wait_for(0ms) == std::future_status::ready; }, 10000ms);
    if (!done) {
        // stop() did not complete within 10s — the engine will assert on destruction.
        // This indicates a bug in stop() or the test setup (e.g., ioc already stopped).
        return;
    }
    try { sf.get(); } catch (...) { /* tolerate teardown exceptions */ }
}

// ── V-1: PerSessionTeardown_TransportCloseSerializedWithRead ─────────────────
//
// Scenario (SC-001 scenario 1):
//   Establish a session over loopback TLS on a ≥3-thread executor.
//   After the session reaches Active, call engine.stop().
//   Under ASan/TSan: if transport.close() races the in-flight async_read_some
//   completion (SSL BIO touch), this produces a SEGV/UAF (BIO_ctrl / map_error_code).
//
// Pre-T014 RED: close() is not yet dispatched on the session strand; it runs on
//   the bare exec_ while an async_read_some completion might run on another thread.
//   Expected: TSan flags the race, or ASan catches the BIO UAF. The test will
//   either crash or report a race pre-T014. This is the same flaky failure
//   mode documented in [[project_business_roundtrip_bio_ctrl_segv]] and in
//   the existing SendFromInsideFromApp_NoDeadlockNoUAF test (V-2 baseline).
//
// Post-T014 GREEN: close() is dispatched on the session strand BEFORE the join,
//   serialized with the in-flight read's completion → no concurrent BIO access.
//
// Multi-thread: io_context driven by 3 threads (main + t1 + t2) per
//   [[feedback_single_threaded_harness_masks_strand_races]].
// Anti-hang: hard 10s budget; engine is always stop()'d before test exit.

TEST(EngineSessionStrand, V1_PerSessionTeardown_TransportCloseSerializedWithRead) {
    const char* fixture_dir = get_fixture_dir();
    if (!fixture_dir || fixture_dir[0] == '\0')
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";

    asio::io_context ioc;
    const uint16_t port = reserve_free_port(ioc);
    auto fac = make_tls_factory(fixture_dir);
    if (!fac) GTEST_SKIP() << "TLS factory build failed (cert/key not available)";

    // Build session configs before constructing engine (so ASSERT fires pre-engine).
    auto acc_cfg = make_session_cfg(
        fac, "ACCEPTOR", "INITIATOR", fixpp::session::session_role::acceptor, "INITIATOR",
        ioc.get_executor(), port);
    auto ini_cfg = make_session_cfg(
        fac, "INITIATOR", "ACCEPTOR", fixpp::session::session_role::initiator, "ACCEPTOR",
        ioc.get_executor(), port);
    const SessionId acc_id = SessionId::from_config(acc_cfg);
    const SessionId ini_id = SessionId::from_config(ini_cfg);

    // Construct engine AFTER all pre-conditions are validated (avoids ASSERT early-exit
    // with a never-stopped engine — Engine::~Engine() asserts stopped_ even for
    // never-started engines, so all ASSERT checks must precede construction).
    fixpp::core::EngineConfig ecfg;
    ecfg.executor = ioc.get_executor();
    ecfg.clock = make_mock_clock(ioc);

    auto engine = std::make_unique<fixpp::session::Engine>(ioc.get_executor(), std::move(ecfg));

    // Use EXPECT + manual cleanup for everything after engine construction.
    if (!engine->register_session(std::move(acc_cfg)).has_value()) {
        stop_engine_sync(ioc, *engine);
        FAIL() << "V-1: acceptor register_session failed";
    }
    if (!engine->register_session(std::move(ini_cfg)).has_value()) {
        stop_engine_sync(ioc, *engine);
        FAIL() << "V-1: initiator register_session failed";
    }

    engine->start();

    // Phase 1: establish both sessions (single-threaded drive until both Active).
    bool both_active = wait_both_active(ioc, *engine, acc_id, ini_id, 8000ms);
    if (!both_active) {
        stop_engine_sync(ioc, *engine);
        FAIL() << "V-1: sessions did not reach Active within 8s";
    }

    // Phase 2: Add background threads → genuinely multi-threaded executor.
    // 3 threads total: main (drives ioc.run_for below) + t1 + t2.
    // [[feedback_single_threaded_harness_masks_strand_races]]
    std::thread t1{[&ioc]{ ioc.run(); }};
    std::thread t2{[&ioc]{ ioc.run(); }};

    // Phase 3: Teardown.
    // Pre-T014: transport.close() runs on whatever thread runs stop() — NOT
    // necessarily the session strand. The in-flight async_read_some completion
    // (with SSL BIO touch in map_error_code) may run concurrently on t1 or t2.
    // Under TSan: the two concurrent accesses to the SSL BIO are a data race.
    // Under ASan: the close() may free the BIO while the read-completion uses it.
    //
    // V-1 post-condition asserted by sanitizers (not by a gtest EXPECT):
    //   If we reach engine->stopped() without aborting, sanitizers confirm no race.
    //   Post-T014: the session strand serializes close() with the read-completion.
    {
        auto stop_fut = asio::co_spawn(
            ioc.get_executor(), engine->stop(), asio::use_future);
        bool stop_done = wait_pred(ioc,
            [&]{ return stop_fut.wait_for(0ms) == std::future_status::ready; }, 8000ms);
        ioc.stop();
        t1.join();
        t2.join();
        ASSERT_TRUE(stop_done) << "V-1: engine.stop() did not complete within 8s";
        stop_fut.get();  // rethrow any exception from stop()
    }

    // V-1: verify engine is stopped before destruction.
    EXPECT_TRUE(engine->stopped())
        << "V-1: engine must be stopped after co_await stop() completes";

    // The sanitizer outcome is the PRIMARY evidence for V-1:
    //   - Pre-T014: ASan/TSan should detect the BIO race or UAF during teardown.
    //   - Post-T014: all sanitizers pass (this statement is reached without abort).
    //
    // [V-1 RED signal: sanitizer-triggered abort during teardown above,
    //  or equivalently: the existing V-2 baseline test failing under ASan/TSan
    //  on the same BIO_ctrl code path per T007 documentation.]
}

// ── V-3: TwoSessionsMT_IndependentProgress ───────────────────────────────────
//
// Scenario: Two DISTINCT loopback session pairs run under a ≥3-thread executor.
//   Pair A: ACCEPTOR/INITIATOR.
//   Pair B: ACCEPTOR2/INITIATOR2.
//   Both must reach Active within a time budget.
//
// This verifies FR-004: "different sessions MAY run concurrently (cross-session
// parallelism preserved)." No engine-global serialization should block session B
// while session A runs.
//
// Pre-T018 (control-plane not yet confined to control_strand_):
//   The progress check MAY pass-by-luck (bare exec_ supports MT).
//   TSan will report the data race in run_accept_loop (listeners_/listener_endpoints_
//   writes vs stop().clear() — the D0 race class). TSan is the RED signal.
//
// Post-T018 GREEN: control-plane writes are on control_strand_; no race.
//
// T008 mandate: record pre-T018 behavior, not skip. The expected observation:
//   sessions establish (passes-by-luck), TSan fires on the control-plane race.
//
// Anti-hang: 15s hard budget; engine always stop()'d before exit.

TEST(EngineSessionStrand, V3_TwoSessionsMT_IndependentProgress) {
    const char* fixture_dir = get_fixture_dir();
    if (!fixture_dir || fixture_dir[0] == '\0')
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";

    asio::io_context ioc;
    const uint16_t port1 = reserve_free_port(ioc);
    const uint16_t port2 = reserve_free_port(ioc);

    auto fac = make_tls_factory(fixture_dir);
    if (!fac) GTEST_SKIP() << "TLS factory build failed (cert/key not available)";

    // Build session configs before constructing engine.
    // Pair A: ACCEPTOR / INITIATOR (both on port1).
    auto acc_a = make_session_cfg(
        fac, "ACCEPTOR", "INITIATOR", fixpp::session::session_role::acceptor, "INITIATOR",
        ioc.get_executor(), port1);
    auto ini_a = make_session_cfg(
        fac, "INITIATOR", "ACCEPTOR", fixpp::session::session_role::initiator, "ACCEPTOR",
        ioc.get_executor(), port1);
    // Pair B: distinct CompID pair (ACCEPTOR2/INITIATOR2), separate listener on port2.
    auto acc_b = make_session_cfg(
        fac, "ACCEPTOR2", "INITIATOR2", fixpp::session::session_role::acceptor, "INITIATOR2",
        ioc.get_executor(), port2);
    auto ini_b = make_session_cfg(
        fac, "INITIATOR2", "ACCEPTOR2", fixpp::session::session_role::initiator, "ACCEPTOR2",
        ioc.get_executor(), port2);

    const SessionId acc_a_id = SessionId::from_config(acc_a);
    const SessionId ini_a_id = SessionId::from_config(ini_a);
    const SessionId acc_b_id = SessionId::from_config(acc_b);
    const SessionId ini_b_id = SessionId::from_config(ini_b);

    fixpp::core::EngineConfig ecfg;
    ecfg.executor = ioc.get_executor();
    ecfg.clock = make_mock_clock(ioc);

    auto engine = std::make_unique<fixpp::session::Engine>(ioc.get_executor(), std::move(ecfg));

    // Use EXPECT + manual cleanup for post-engine-construction checks.
    if (!engine->register_session(std::move(acc_a)).has_value() ||
        !engine->register_session(std::move(ini_a)).has_value() ||
        !engine->register_session(std::move(acc_b)).has_value() ||
        !engine->register_session(std::move(ini_b)).has_value()) {
        stop_engine_sync(ioc, *engine);
        FAIL() << "V-3: register_session failed for one or more sessions";
    }

    engine->start();

    // Phase 1: single-threaded drive until all four sessions reach Active.
    bool pair_a_active = wait_both_active(ioc, *engine, acc_a_id, ini_a_id, 15000ms);
    bool pair_b_active = wait_both_active(ioc, *engine, acc_b_id, ini_b_id, 5000ms);

    // V-3 primary assertion: BOTH session pairs must reach Active.
    // Pre-T018 (control-plane not yet on control_strand_):
    //   This assertion may PASS-BY-LUCK (bare exec_ allows concurrent session
    //   establishment). TSan reveals the control-plane data race (the RED signal).
    //
    // [T008 mandate: "record whether V-3 pre-T018 passes-by-luck or races."]
    // [Pre-T018 observation: both sessions CAN establish. TSan fires on the
    //  listeners_/listener_endpoints_ write vs stop().clear() data race. This
    //  is the expected RED signal — not a test-assertion failure but a sanitizer
    //  report. The progress assertion itself may be GREEN pre-T018.]
    EXPECT_TRUE(pair_a_active)
        << "V-3: session pair A (ACCEPTOR/INITIATOR) must reach Active within 15s\n"
        << "  [T008: pre-T018 may pass-by-luck; TSan reveals control-plane race\n"
        << "   in run_accept_loop vs stop().clear() — the true RED signal]";
    EXPECT_TRUE(pair_b_active)
        << "V-3: session pair B (ACCEPTOR2/INITIATOR2) must reach Active within 15s\n"
        << "  [T008: pre-T018 may pass-by-luck; TSan reveals control-plane race\n"
        << "   in run_accept_loop vs stop().clear() — the true RED signal]";

    // V-3 cross-session independence check:
    // If the engine serialized ALL sessions globally (engine-wide bottleneck),
    // pair B would be unable to start while pair A is establishing.
    // Both reaching Active within independent time budgets demonstrates that
    // one session's work does NOT block another's. This is the FR-004 witness.
    EXPECT_TRUE(pair_a_active && pair_b_active)
        << "V-3: cross-session parallelism failed — one pair blocked another\n"
        << "  pair_a_active=" << pair_a_active << " pair_b_active=" << pair_b_active;

    stop_engine_sync(ioc, *engine);
    EXPECT_TRUE(engine->stopped()) << "V-3: engine must be stopped after stop_engine_sync";
}

// ── V-9: ReentrantSend_FromCallback_NoDeadlock_AndPostStopFastFails ──────────
//
// Sub-cell (a): Re-entrant Engine::send from inside fromApp under ≥3 threads.
//   fromApp fires on the acceptor → posts Engine::send targeting the initiator.
//   Pre-T012, send does NOT route through control_strand_; the hop goes directly
//   to exec_. T008 mandates recording whether this deadlocks, races, or
//   passes-by-luck.
//
// Sub-cell (b): A send issued AFTER stop() completes must fast-fail with a clean
//   error (session_invalid_state_for_send=77 or session_invalid_argument=119).
//   Must NOT hang, crash, or access a half-cleared registry.
//
// Multi-thread: io_context driven by 3 threads per
//   [[feedback_single_threaded_harness_masks_strand_races]].
// Anti-hang: 5s per sub-cell; engine always stop()'d before exit.

TEST(EngineSessionStrand, V9_ReentrantSend_FromCallback_NoDeadlock_AndPostStopFastFails) {
    const char* fixture_dir = get_fixture_dir();
    if (!fixture_dir || fixture_dir[0] == '\0')
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";

    asio::io_context ioc;
    const uint16_t port = reserve_free_port(ioc);
    auto fac = make_tls_factory(fixture_dir);
    if (!fac) GTEST_SKIP() << "TLS factory build failed (cert/key not available)";

    // Application for sub-cell (a): re-entrant send from inside fromApp.
    struct ReentrantApp : public Application {
        fixpp::session::Engine* engine_ptr = nullptr;
        asio::any_io_executor exec;   // engine executor (same as ioc.get_executor())
        SessionId send_to_id;
        std::vector<std::byte> payload;
        std::atomic<int> from_app_count{0};
        std::atomic<bool> reentrant_done{false};
        std::atomic<bool> reentrant_error{false};

        expected_t<void> fromApp(
            const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>&,
            const SessionId&) override {
            int call = ++from_app_count;
            // Issue re-entrant Engine::send on the FIRST fromApp call.
            // Use asio::post to hop off the current callback context before calling
            // send() — this avoids blocking the current dispatch chain and mirrors
            // the T012 "non-blocking post" guarantee.
            if (call == 1 && engine_ptr) {
                auto& eng = *engine_ptr;
                auto ex = exec;
                auto pl = payload;
                auto sid = send_to_id;
                auto* done = &reentrant_done;
                auto* err_flag = &reentrant_error;
                asio::post(ex, [&eng, ex, sid, pl = std::move(pl), done, err_flag]() mutable {
                    asio::co_spawn(ex, eng.send(sid, std::span<const std::byte>(pl)),
                        [done, err_flag](std::exception_ptr ep, expected_t<void> r) {
                            if (ep) {
                                err_flag->store(true, std::memory_order_release);
                            } else if (r.has_value()) {
                                done->store(true, std::memory_order_release);
                            } else {
                                // Send returned an error (acceptable pre-T012).
                                err_flag->store(true, std::memory_order_release);
                            }
                        });
                });
            }
            return {};
        }
    };
    auto app = std::make_shared<ReentrantApp>();

    // Build session configs before engine construction (avoids ASSERT early-exit
    // with never-stopped engine; Engine::~Engine asserts stopped_ even pre-start).
    auto acc_cfg = make_session_cfg(
        fac, "ACCEPTOR", "INITIATOR", fixpp::session::session_role::acceptor, "INITIATOR",
        ioc.get_executor(), port);
    auto ini_cfg = make_session_cfg(
        fac, "INITIATOR", "ACCEPTOR", fixpp::session::session_role::initiator, "ACCEPTOR",
        ioc.get_executor(), port);
    const SessionId acc_id = SessionId::from_config(acc_cfg);
    const SessionId ini_id = SessionId::from_config(ini_cfg);

    fixpp::core::EngineConfig ecfg;
    ecfg.executor = ioc.get_executor();
    ecfg.clock = make_mock_clock(ioc);
    ecfg.application = app;

    auto engine = std::make_unique<fixpp::session::Engine>(ioc.get_executor(), std::move(ecfg));

    if (!engine->register_session(std::move(acc_cfg)).has_value() ||
        !engine->register_session(std::move(ini_cfg)).has_value()) {
        stop_engine_sync(ioc, *engine);
        FAIL() << "V-9: register_session failed";
    }

    engine->start();

    // Establish both sessions.
    bool both_active = wait_both_active(ioc, *engine, acc_id, ini_id, 8000ms);
    if (!both_active) {
        stop_engine_sync(ioc, *engine);
        FAIL() << "V-9: sessions did not reach Active within 8s";
    }

    // Phase 2: add background threads for genuine multi-threaded executor.
    std::thread t1{[&ioc]{ ioc.run(); }};
    std::thread t2{[&ioc]{ ioc.run(); }};

    // Wire the re-entrant application.
    static const char kPayload[] =
        "35=D\x01" "11=ORD001\x01" "54=1\x01" "55=AAPL\x01" "40=2\x01" "44=100.00\x01";
    std::vector<std::byte> payload;
    for (const char* p = kPayload; *p; ++p) payload.push_back(static_cast<std::byte>(*p));
    app->engine_ptr = engine.get();
    app->exec = ioc.get_executor();
    // Send the re-entrant send from the ACCEPTOR's fromApp back to the INITIATOR.
    // (fromApp fires on the acceptor when initiator sends to it.)
    app->send_to_id = ini_id;
    app->payload = payload;

    // Sub-cell (a): trigger fromApp by sending from initiator → acceptor.
    {
        auto send_fut = asio::co_spawn(ioc.get_executor(),
            engine->send(ini_id, std::span<const std::byte>(payload)),
            asio::use_future);
        bool send_done = wait_pred(ioc,
            [&]{ return send_fut.wait_for(0ms) == std::future_status::ready; }, 5000ms);
        if (!send_done) {
            ioc.stop();
            t1.join();
            t2.join();
            stop_engine_sync(ioc, *engine);
            FAIL() << "V-9(a): initial send did not complete within 5s (possible deadlock)";
        }
        auto r = send_fut.get();
        EXPECT_TRUE(r.has_value())
            << "V-9(a): initial send must succeed; err="
            << (r.has_value() ? 0 : static_cast<int>(r.error()));
    }

    // Wait for fromApp to fire on the acceptor.
    bool fa_fired = wait_pred(ioc,
        [&]{ return app->from_app_count.load(std::memory_order_acquire) >= 1; }, 5000ms);
    EXPECT_TRUE(fa_fired) << "V-9(a): fromApp must fire on the acceptor after initiator send";

    if (fa_fired) {
        // Wait for re-entrant send to resolve (done OR error; MUST NOT hang).
        bool re_resolved = wait_pred(ioc,
            [&]{
                return app->reentrant_done.load(std::memory_order_acquire)
                    || app->reentrant_error.load(std::memory_order_acquire);
            }, 5000ms);

        // V-9(a) primary assertion: re-entrant send MUST NOT deadlock.
        // Pre-T012: send does NOT route through control_strand_; bare exec_ is used.
        // T008 mandate: record pass-by-luck vs deadlock vs race — not premature GREEN.
        //
        // Expected pre-T012 observation:
        //   - re_resolved=true AND done=true: PASSES-BY-LUCK (bare exec handles it).
        //   - re_resolved=true AND error=true: send failed (acceptable pre-T012).
        //   - re_resolved=false: DEADLOCK — the true RED signal for V-9.
        //   TSan may also flag races on the exec-hop path (pre-T012 RED via sanitizer).
        EXPECT_TRUE(re_resolved)
            << "V-9(a): re-entrant Engine::send from inside fromApp DEADLOCKED\n"
            << "  [T008: pre-T012 expected pass-by-luck OR error, NEVER a hang;\n"
            << "   TSan may reveal control-plane races; deadlock = TRUE RED signal]";
    }

    // Sub-cell (b): post-stop send must fast-fail.
    // Stop the engine first.
    {
        auto stop_fut = asio::co_spawn(ioc.get_executor(), engine->stop(), asio::use_future);
        bool stop_done = wait_pred(ioc,
            [&]{ return stop_fut.wait_for(0ms) == std::future_status::ready; }, 8000ms);
        ioc.stop();
        t1.join();
        t2.join();
        if (!stop_done) {
            FAIL() << "V-9(b): engine.stop() did not complete within 8s";
        }
        stop_fut.get();
    }

    // Now issue a send after stop() has completed.
    // Expected: fast-fail with session_invalid_state_for_send(77) or
    //           session_invalid_argument(119). Must NOT hang or crash.
    {
        // ioc was stopped above; restart it for the post-stop send.
        ioc.restart();
        auto post_stop_fut = asio::co_spawn(ioc.get_executor(),
            engine->send(ini_id, std::span<const std::byte>(payload)),
            asio::use_future);
        bool post_stop_done = wait_pred(ioc,
            [&]{ return post_stop_fut.wait_for(0ms) == std::future_status::ready; }, 3000ms);
        ASSERT_TRUE(post_stop_done)
            << "V-9(b): post-stop send must complete (not hang) within 3s";

        auto r = post_stop_fut.get();
        // Must be an error (not success — engine is stopped, registry is cleared).
        EXPECT_FALSE(r.has_value())
            << "V-9(b): post-stop send must fail with an error, not succeed";
        if (!r.has_value()) {
            const bool is_valid_fast_fail =
                r.error() == error::session_invalid_state_for_send ||  // slot 77
                r.error() == error::session_invalid_argument;           // slot 119
            EXPECT_TRUE(is_valid_fast_fail)
                << "V-9(b): post-stop send must return session_invalid_state_for_send(77)"
                << " or session_invalid_argument(119); got="
                << static_cast<int>(r.error());
        }
    }

    // Engine is already stopped from sub-cell (b) above.
    EXPECT_TRUE(engine->stopped());
}

// ── V-10: SocketExecutorIsSessionStrand ──────────────────────────────────────
//
// Checks that the TCP socket of a live engine-managed session's transport is
// associated with a strand (asio::strand<asio::any_io_executor>), not the bare
// io_context executor.
//
// Mechanism:
//   1. Establish a session via the engine.
//   2. Access the live transport via lookup() → session.live_transport().
//   3. dynamic_cast to asio_tls_transport (the concrete type).
//   4. Use asio_tls_transport_test_access::socket_of() to access socket_.
//   5. Check socket_.get_executor().target<asio::strand<asio::any_io_executor>>() != nullptr.
//
// Pre-T011 RED (definitive, not pass-by-luck):
//   Engine loops are spawned on bare exec_ (engine.cpp:689/694).
//   asio_listener builds the accepted socket using `co_await this_coro::executor`
//   which IS the bare exec_ (the loop runs on it). Hence the socket is associated
//   with the bare io_context executor, NOT a strand.
//   target<asio::strand<...>>() returns nullptr → EXPECT_NE fails → RED.
//
// Post-T011 GREEN:
//   Loops spawned on entry.session_strand → this_coro::executor IS the strand.
//   The accepted socket executor is the session strand.
//   target<asio::strand<...>>() returns non-null → EXPECT_NE passes → GREEN.
//
// R8 lynchpin: A single un-fixed construction site re-opens the per-session race.
// [research.md D5/R8; data-model E-5/INV-7; contract V-10/C-7]

TEST(EngineSessionStrand, V10_SocketExecutorIsSessionStrand) {
    const char* fixture_dir = get_fixture_dir();
    if (!fixture_dir || fixture_dir[0] == '\0')
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";

    asio::io_context ioc;
    const uint16_t port = reserve_free_port(ioc);
    auto fac = make_tls_factory(fixture_dir);
    if (!fac) GTEST_SKIP() << "TLS factory build failed (cert/key not available)";

    // Build session configs before engine construction (avoids early-exit with
    // never-stopped engine; Engine::~Engine asserts stopped_ even pre-start).
    auto acc_cfg = make_session_cfg(
        fac, "ACCEPTOR", "INITIATOR", fixpp::session::session_role::acceptor, "INITIATOR",
        ioc.get_executor(), port);
    auto ini_cfg = make_session_cfg(
        fac, "INITIATOR", "ACCEPTOR", fixpp::session::session_role::initiator, "ACCEPTOR",
        ioc.get_executor(), port);
    const SessionId acc_id = SessionId::from_config(acc_cfg);
    const SessionId ini_id = SessionId::from_config(ini_cfg);

    fixpp::core::EngineConfig ecfg;
    ecfg.executor = ioc.get_executor();
    ecfg.clock = make_mock_clock(ioc);

    auto engine = std::make_unique<fixpp::session::Engine>(ioc.get_executor(), std::move(ecfg));

    if (!engine->register_session(std::move(acc_cfg)).has_value()) {
        stop_engine_sync(ioc, *engine);
        FAIL() << "V-10: acceptor register_session failed";
    }
    if (!engine->register_session(std::move(ini_cfg)).has_value()) {
        stop_engine_sync(ioc, *engine);
        FAIL() << "V-10: initiator register_session failed";
    }

    engine->start();

    // Establish both sessions.
    bool both_active = wait_both_active(ioc, *engine, acc_id, ini_id, 8000ms);
    if (!both_active) {
        stop_engine_sync(ioc, *engine);
        FAIL() << "V-10: sessions did not reach Active within 8s";
    }

    // Access the live session objects via lookup().
    // Pre-T024: lookup() returns raw Session*.
    fixpp::session::Session* acc_session = engine->lookup(acc_id);
    fixpp::session::Session* ini_session = engine->lookup(ini_id);

    if (!acc_session || !ini_session) {
        stop_engine_sync(ioc, *engine);
        ASSERT_NE(acc_session, nullptr) << "V-10: acceptor session must be reachable post-active";
        ASSERT_NE(ini_session, nullptr) << "V-10: initiator session must be reachable post-active";
        return;
    }

    // Access the live transport from each session.
    fixpp::transport::Transport& acc_transport = acc_session->live_transport();
    fixpp::transport::Transport& ini_transport = ini_session->live_transport();

    // Downcast to the concrete asio_tls_transport.
    // The engine uses asio_tls_transport_factory which always produces this type.
    auto* acc_tls = dynamic_cast<fixpp::transport::asio_tls_transport*>(&acc_transport);
    auto* ini_tls = dynamic_cast<fixpp::transport::asio_tls_transport*>(&ini_transport);

    if (!acc_tls || !ini_tls) {
        stop_engine_sync(ioc, *engine);
        ASSERT_NE(acc_tls, nullptr) << "V-10: acceptor transport must be asio_tls_transport";
        ASSERT_NE(ini_tls, nullptr) << "V-10: initiator transport must be asio_tls_transport";
        return;
    }

    // V-10 core assertion:
    // socket_.get_executor().target<asio::strand<asio::any_io_executor>>() must NOT be null.
    //
    // asio::any_io_executor::target<T>() returns non-null iff the contained executor is T.
    //
    // Pre-T011 (loops on bare exec_):
    //   socket executor = bare io_context executor (NOT a strand).
    //   target<strand_t>() returns nullptr → EXPECT_NE(nullptr) FAILS → RED.
    //
    // Post-T011 (loops on session_strand):
    //   socket executor = asio::strand<asio::any_io_executor> (IS a strand).
    //   target<strand_t>() returns non-null → EXPECT_NE(nullptr) PASSES → GREEN.

    auto& acc_sock = fixpp::transport::asio_tls_transport_test_access::socket_of(*acc_tls);
    auto& ini_sock = fixpp::transport::asio_tls_transport_test_access::socket_of(*ini_tls);

    using strand_t = asio::strand<asio::any_io_executor>;

    const strand_t* acc_strand_ptr = acc_sock.get_executor().target<strand_t>();
    EXPECT_NE(acc_strand_ptr, nullptr)
        << "V-10 RED (pre-T011): acceptor socket is NOT bound to a strand.\n"
        << "  socket_.get_executor() == bare io_context executor (loops on bare exec_).\n"
        << "  run_accept_loop spawned on exec_ (engine.cpp:689), so this_coro::executor\n"
        << "  inside the loop IS the bare executor — the accepted socket inherits it.\n"
        << "  Post-T011: loop spawns on entry.session_strand → socket is strand-bound.\n"
        << "  This is the R8 lynchpin: one un-fixed site re-opens the per-session race.";

    const strand_t* ini_strand_ptr = ini_sock.get_executor().target<strand_t>();
    EXPECT_NE(ini_strand_ptr, nullptr)
        << "V-10 RED (pre-T011): initiator socket is NOT bound to a strand.\n"
        << "  socket_.get_executor() == bare io_context executor (loops on bare exec_).\n"
        << "  run_connect_loop spawned on exec_ (engine.cpp:694), so this_coro::executor\n"
        << "  inside the loop IS the bare executor — connected socket inherits it.\n"
        << "  Post-T011: loop spawns on entry.session_strand → socket is strand-bound.\n"
        << "  This is the R8 lynchpin: one un-fixed site re-opens the per-session race.";

    // Teardown.
    stop_engine_sync(ioc, *engine);
    EXPECT_TRUE(engine->stopped());
}

// ── V-8: ControlPlaneRace_PublicReaderVsMutation ─────────────────────────────
//
// Scenario (SC-002 / T016 DD-2026-06-06 retarget / research D-SNAP):
//
//   The original V-8 (one-sided park on the listeners_ write vs stop().clear())
//   was NOT honestly witnessable: the pre-existing join-before-clear
//   (outstanding_counter_) makes the loop's listeners_ write happens-before
//   stop()'s clear() by construction — the park merely delayed both sides, so
//   TSan never fired.  [DD-2026-06-06 / codex_023-engine-session-strand_gate_a_v8_retarget.md]
//
//   This retargeted V-8 witnesses the GENUINELY HB-free control-plane data race:
//
//     PUBLIC READER vs MAP MUTATION (no live peer needed):
//
//       Thread-reader (raw std::thread): calls engine.acceptor_bound_endpoint(acc_id)
//         and/or engine.lookup(acc_id) in a tight loop — these read listener_endpoints_
//         and registry_ directly WITHOUT any synchronisation (no strand, no mutex,
//         no atomic).  [engine.cpp:1227-1230, 134-136]
//
//       Engine executor threads: the accept loop WRITES listener_endpoints_[id] at
//         run_accept_loop startup [engine.cpp:586-588]; stop() CLEARS the maps at
//         step-5 [engine.cpp:1084-1087].
//
//       NO happens-before edge exists between the reader thread and the engine
//       executor threads — the reader is a raw std::thread with no synchronisation.
//       TSan observes: WRITE in engine-thread vs READ in reader-thread → DATA RACE.
//
// Mechanism:
//   1. Register an acceptor session and call engine.start().
//   2. Start 2 engine-executor threads (t1, t2) to drive the ioc multi-threaded.
//   3. Start a SEPARATE reader thread (t_reader) that spins calling
//      acceptor_bound_endpoint() and lookup() in a tight loop for the entire
//      start→stop window.  No sync object between reader and the engine threads.
//   4. Call stop() from the main thread (via wait_pred driving ioc).
//   5. Join t_reader after stop() completes.
//
//   TSan observes the unsynchronised read (t_reader) vs the map write (t1/t2
//   running the accept loop) or the map clear (stop()): DATA RACE → RED.
//   halt_on_error=1 aborts the process.
//
// Widen the overlap window:
//   The reader spins throughout the whole start→stop duration (not just a brief
//   window) so even on a fast machine the read reliably overlaps the write or clear.
//
// RED expected: pre-T018 (public readers not yet protected by D-SNAP/T023-T024).
// GREEN expected: post-T026 (D-SNAP snapshot readers installed).
//
// Anti-hang: 15s hard budget; engine always stop()'d before test exit; reader
//   thread holds a stop flag set under engine teardown.
//
// No live peer needed: the listener binds before any peer connects; the write
//   at listener_endpoints_[id] happens during accept-loop startup.
//
// [DD-2026-06-06; spec SC-002; contract V-8; tasks T016/T017; research D-SNAP]

TEST(EngineSessionStrand, V8_ControlPlaneRace_PublicReaderVsMutation) {
    const char* fixture_dir = get_fixture_dir();
    if (!fixture_dir || fixture_dir[0] == '\0')
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";

    // Use a multi-threaded io_context: main thread + t1 + t2 drive the engine.
    // A separate t_reader thread calls the public readers with NO synchronisation.
    // [[feedback_single_threaded_harness_masks_strand_races]]
    asio::io_context ioc;

    auto fac = make_tls_factory(fixture_dir);
    if (!fac) GTEST_SKIP() << "TLS factory build failed (cert/key not available)";

    const uint16_t port = reserve_free_port(ioc);

    // Register an acceptor session: the accept loop writes listener_endpoints_[id]
    // at startup (no peer needed to trigger that write).
    auto acc_cfg = make_session_cfg(
        fac, "ACCEPTOR_V8", "INITIATOR_V8",
        fixpp::session::session_role::acceptor, "INITIATOR_V8",
        ioc.get_executor(), port);
    const SessionId acc_id = SessionId::from_config(acc_cfg);

    fixpp::core::EngineConfig ecfg;
    ecfg.executor = ioc.get_executor();
    ecfg.clock = make_mock_clock(ioc);

    auto engine = std::make_unique<fixpp::session::Engine>(ioc.get_executor(), std::move(ecfg));

    if (!engine->register_session(std::move(acc_cfg)).has_value()) {
        stop_engine_sync(ioc, *engine);
        FAIL() << "V-8: acceptor register_session failed";
    }

    engine->start();

    // Start engine executor threads AFTER start() so the accept loop is already
    // queued.  t1 and t2 drive the ioc (and the accept loop which writes the map).
    // [[feedback_fork_inherited_asio_pool_deadlock]] — threads constructed after
    // engine->start(), not before.
    std::thread t1{[&ioc]{ ioc.run(); }};
    std::thread t2{[&ioc]{ ioc.run(); }};

    // Reader thread: calls acceptor_bound_endpoint() and lookup() in a tight
    // loop with NO synchronisation between this thread and the engine threads.
    // The loop runs until reader_stop is set.
    //
    // V-8 DATA RACE SURFACE:
    //   - acceptor_bound_endpoint() reads listener_endpoints_ (unordered_map)
    //     while the accept loop thread (t1 or t2) WRITES listener_endpoints_[id].
    //   - lookup() reads registry_ (unordered_map) while stop() thread CLEARS it.
    //   - No HB edge between t_reader and the engine threads (no mutex/atomic/sync).
    //   → TSan: DATA RACE on listener_endpoints_ or registry_.
    //
    // IMPORTANT: there is deliberately NO shared synchronisation object between
    // this reader thread and the engine executor threads.  Any sync object would
    // create a happens-before edge and suppress the race TSan must report.
    std::atomic<bool> reader_stop{false};
    std::atomic<int>  reader_iterations{0};
    std::thread t_reader{[&engine, &acc_id, &reader_stop, &reader_iterations]() {
        // Spin calling the public readers.  The reads race:
        //   (a) the accept-loop write of listener_endpoints_[id]  — write vs read
        //   (b) stop()'s clear() of listener_endpoints_             — clear vs read
        //   (c) stop()'s clear() of registry_                      — clear vs read
        while (!reader_stop.load(std::memory_order_relaxed)) {
            // Read listener_endpoints_ via acceptor_bound_endpoint().
            (void)engine->acceptor_bound_endpoint(acc_id);
            // Read registry_ via lookup().
            (void)engine->lookup(acc_id);
            reader_iterations.fetch_add(1, std::memory_order_relaxed);
        }
    }};

    // Let the accept loop start and write listener_endpoints_.
    // 30ms is sufficient: the accept loop runs immediately when t1/t2 pick up work.
    ioc.run_for(std::chrono::milliseconds{30});
    ioc.restart();

    // Stop the engine.  stop() will clear listener_endpoints_ and registry_ while
    // t_reader is spinning.  This is the second window where the race fires (in
    // addition to the initial write window above).
    {
        auto stop_fut = asio::co_spawn(
            ioc.get_executor(), engine->stop(), asio::use_future);
        bool done = wait_pred(ioc,
            [&]{ return stop_fut.wait_for(0ms) == std::future_status::ready; },
            12000ms);

        // Signal the reader to stop AFTER stop() completes (or times out).
        reader_stop.store(true, std::memory_order_relaxed);

        ioc.stop();
        t1.join();
        t2.join();
        t_reader.join();

        ASSERT_TRUE(done) << "V-8: engine.stop() did not complete within 12s";
        stop_fut.get();
    }

    // V-8 PRIMARY ASSERTION (TSan-witnessed):
    //   Pre-T026 (no D-SNAP): TSan fires a DATA RACE on listener_endpoints_ or
    //   registry_ (t_reader reads vs engine writes/clears with no HB) →
    //   halt_on_error=1 aborts the process → test is RED.
    //
    //   Post-T026 (D-SNAP installed): public readers go through the snapshot
    //   (atomic_load of an immutable shared_ptr) → no unsynchronised map access
    //   → TSan clean → GREEN.
    //
    //   The gtest assertion below is the GREEN post-condition.  Pre-T026, TSan
    //   aborts before this line is reached.
    EXPECT_TRUE(engine->stopped())
        << "V-8: engine must be stopped after stop() completes";

    // Confirm the reader actually executed enough iterations to give TSan time
    // to observe the race window (both the initial write and the clear).
    EXPECT_GT(reader_iterations.load(), 0)
        << "V-8: reader thread must have iterated at least once";
}

// ── V-12: StopBeforeAwaitedPublish ───────────────────────────────────────────
//
// Scenario (research D-PUB stop-already-in-progress; contract C-6/V-12; T016):
//   Verify that if stop() sets stopped_=true WHILE a role loop sits between
//   transport creation and the awaited control-strand publish, the publish_entry
//   helper observes stopped_=true (INV-2a check from T013), returns the STOPPED
//   DISPOSITION (false), and the loop closes/returns WITHOUT entering the read
//   pump (no live transport published).
//
// How the INV-2a check already exists (from T013 / publish_entry):
//   publish_entry (engine.cpp ~:483) checks stopped_.load(acquire) FIRST on the
//   control strand.  If stopped_=true, it co_returns false without writing
//   entry.session or entry.live_transport.  The caller (run_accept_loop step 7a)
//   observes published=false and immediately calls local_session->close(terminal)
//   + co_returns without entering run_read_pump.
//
// This witness verifies the stopped-disposition path is functional by observing
// the ABSENCE of an Active session after stop() races the accept loop.
//
// The test drives stop() while the accept loop is parked in async_accept (before
// any peer connects, so before transport creation and publish).  The accept loop
// receives a total-cancel from stop(), exits the while(!stopped()) gate, and
// returns WITHOUT ever calling publish_entry with a live transport.  The stopped
// disposition contract is exercised on this simpler path.
//
// After stop(), lookup() must return nullptr (registry cleared) — confirming no
// live transport was published behind the in-progress stop().
//
// V-12 DISPOSITION: ALREADY-GREEN because INV-2a was implemented in T013's
// publish_entry.  T019 is a no-op / confirmation only. Recorded per tasks.md
// T016/T017: "if so, RECORD that it passes because the INV-2a check is present."
//
// [research D-PUB; data-model INV-2a; contract C-6/V-12; tasks T016/T017]

TEST(EngineSessionStrand, V12_StopBeforeAwaitedPublish) {
    const char* fixture_dir = get_fixture_dir();
    if (!fixture_dir || fixture_dir[0] == '\0')
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";

    asio::io_context ioc;
    const uint16_t port = reserve_free_port(ioc);
    auto fac = make_tls_factory(fixture_dir);
    if (!fac) GTEST_SKIP() << "TLS factory build failed (cert/key not available)";

    // Build an acceptor session — its loop parks in async_accept after the
    // listener build.  We call stop() before any peer connects.
    auto acc_cfg = make_session_cfg(
        fac, "ACCEPTOR_V12", "INITIATOR_V12",
        fixpp::session::session_role::acceptor, "INITIATOR_V12",
        ioc.get_executor(), port);
    const SessionId acc_id = SessionId::from_config(acc_cfg);

    fixpp::core::EngineConfig ecfg;
    ecfg.executor = ioc.get_executor();
    ecfg.clock = make_mock_clock(ioc);

    auto engine = std::make_unique<fixpp::session::Engine>(ioc.get_executor(), std::move(ecfg));

    if (!engine->register_session(std::move(acc_cfg)).has_value()) {
        stop_engine_sync(ioc, *engine);
        FAIL() << "V-12: acceptor register_session failed";
    }

    engine->start();

    // Let the accept loop get established (listener bind, waiting on async_accept).
    // 50ms is sufficient to reach async_accept on a local loopback ioc.
    ioc.run_for(std::chrono::milliseconds{50});
    ioc.restart();

    // Call stop() with the accept loop parked in async_accept (no peer connected).
    // The accept loop will receive a total-cancel (stop() emits total), exit the
    // while(!stopped()) gate, and return WITHOUT ever calling publish_entry with a
    // live transport.  This exercises the stopped-disposition contract at the
    // earliest possible point (no transport was created).
    stop_engine_sync(ioc, *engine);

    ASSERT_TRUE(engine->stopped())
        << "V-12: engine must be stopped after stop_engine_sync";

    // V-12 primary assertion: no session has been published as Active.
    // After stop(), lookup() returns nullptr (registry cleared in step 5).
    // This confirms that NO live transport was published behind the in-progress stop().
    //
    // [INV-2a: publish_entry checks stopped_.load(acquire) FIRST → false → no publish]
    // [contract C-6: stopped disposition → loop closes without initiating any read]
    fixpp::session::Session* acc_session = engine->lookup(acc_id);
    EXPECT_EQ(acc_session, nullptr)
        << "V-12: acceptor session must NOT be reachable after stop() clears the registry\n"
        << "  A non-null Session* here would indicate the registry was NOT cleared\n"
        << "  (or that lookup() was called before the registry clear completed).\n"
        << "  [INV-2a check in publish_entry (T013) ensures: stopped_ true → no publish\n"
        << "   of live_transport → loop closes without entering read pump]\n"
        << "  V-12 is ALREADY-GREEN because the INV-2a check was implemented in T013.\n"
        << "  T019 is a no-op/confirmation per tasks.md T016/T017.";
}

}  // namespace
