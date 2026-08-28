// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/session/test_engine_session_strand.cpp
//
// 023-engine-session-strand — T006 [US1] + T016 [US2] + T021 [US3] witnesses.
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
//   V-4  SingleThreadedSuiteUnchanged  [T021/T022 characterization]
//        Compile-time guard: lookup() returns Session* (raw pointer) before T024.
//        Characterization baseline: 42 session tests (ctest -R '^session_') GREEN
//        pre-T021 (no rewrites).  This cell itself does NOT test session behaviour;
//        it just asserts the current lookup() return type so T026 can verify the
//        "exactly one ABI diff" claim when T024 changes it to shared_ptr<Session>.
//        GREEN now; compile-fails after T024 (that is the expected one diff).
//        [C-7/V-4/SC-003/FR-007; tasks T021/T022]
//
//   V-5  AbiBaselineCapture  [T021/T022 scaffolding]
//        The nm baseline for Engine's public interface is saved at:
//          tests/abi/baseline/libfixpp_session_engine_pre023.nm
//        T026 compares the post-T024 nm output and asserts exactly the one
//        expected diff: lookup() mangling.
//        Note: Itanium ABI does NOT encode the return type in the mangled symbol
//        name for non-template non-virtual functions, so nm alone cannot detect
//        the Session*→shared_ptr<Session> change.  The compile-time V-4 gate
//        (static_assert on decltype) is the primary "one diff" detector; the nm
//        baseline is secondary corroboration that NO OTHER Engine symbols change.
//        [C-7/V-5/C-4/SC-004; tasks T021/T022/T026]
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
//   V-11 SnapshotReadersMtSafe  [T021/T022 US3 RED-phase]
//        PART 1 — TSan-race (compiles and runs NOW, RED pre-T026):
//          A raw std::thread spins calling lookup() and acceptor_bound_endpoint()
//          with NO synchronisation against engine executor threads.  The engine
//          runs and stop() clears concurrently → TSan DATA RACE on registry_ and
//          listener_endpoints_ (same race class as V-8, broader surface).
//          halt_on_error=1 aborts → process exits → test is RED.
//          GREEN post-T026 when D-SNAP snapshot readers are installed.
//        PART 2 — keepalive (DEFERRED until T024):
//          A shared_ptr<Session> handle obtained before clear() must outlive the
//          clear() while the Engine is alive.  Guarded with #if 0 until T024
//          changes lookup() → shared_ptr<Session>.  See TODO-T024 below.
//        PART 3 — lease/~Engine assert (DEFERRED until T025):
//          ~Engine must debug-assert no outstanding lookup()/snapshot handles.
//          Guarded with #if 0 until T025 adds the lease control block.
//          See TODO-T025 below.
//        [C-8/V-11/E-7/INV-9/INV-9a; research D-SNAP; tasks T021/T022]
//
//   V-12 StopBeforeAwaitedPublish
//        stop() races the accept loop before any peer connects; confirms the
//        stopped-disposition path (INV-2a) is functional.  ALREADY-GREEN because
//        INV-2a was implemented in T013 publish_entry. [C-6/V-12; T016/T017]
//
//   V-16 PostDrainLateSendFastFails_WithoutPosting
//        Pauses stop() after its send_counter_ drain has already observed zero,
//        starts a fresh Engine::send() in that post-drain window, then lets
//        stop() clear/return and destroys the Engine. Proves the outer
//        enroll-then-recheck admission gate: the late send sees stopped_=true
//        and fast-fails without posting any control-strand lambda that could
//        dereference freed Engine state. [gate-b/r3 P1; FR-012/R7]
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
//   - V-11 Part 1: same real-race pattern as V-8 (no shared sync object with
//     engine threads).  Parts 2 and 3 are behind #if 0 guards waiting for T024/T025.
//
// Anchors: tasks.md T006/T007/T008/T016/T017/T021/T022;
//          contracts/engine-session-strand.md C-7 (V-1/V-3/V-4/V-5/V-9/V-10),
//          C-8 (V-8/V-11), C-6 (V-12); research.md D2/D5/R8/D-SNAP;
//          data-model E-7/INV-9/INV-9a; [const §IX §2].

#include <gtest/gtest.h>

#include <asio/any_io_executor.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/steady_timer.hpp>
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
#include "support/wait_until.hpp"

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
// MUST NOT be called while other threads are in ioc.run() — restart() is UB then.
// Use wait_until_observed() instead when worker threads own the ioc.
static bool wait_pred(asio::io_context& ioc, auto pred,
                      std::chrono::milliseconds budget) {
    auto end = std::chrono::steady_clock::now() + budget;
    while (!pred() && std::chrono::steady_clock::now() < end) {
        ioc.run_for(50ms);
        ioc.restart();
    }
    return pred();
}

// Sleep-poll wait, for use once worker threads are already inside ioc.run():
// calling restart() from the main thread while workers are in run() is asio UB
// (SEGFAULT under gcc-release). This file's former `wait_until_observed` was one
// of the three copies #315 hoisted; the shared helper polls on a 1 ms slice
// where the local one used 2 ms, so every wait here detects at least as fast.
using fixpp::test_support::wait_until_observed;

// Wait for both sessions to reach fsm_state::Active via lookup + state() check.
// Safer than counting onLogon callbacks because it directly observes the FSM state.
static bool wait_both_active(asio::io_context& ioc,
                             fixpp::session::Engine& engine,
                             const SessionId& acc_id, const SessionId& ini_id,
                             std::chrono::milliseconds budget) {
    return wait_pred(ioc, [&]() -> bool {
        // T024: lookup() now returns shared_ptr<Session> (bounded handle).
        // operator bool() and -> work the same as with raw Session*.
        auto a = engine.lookup(acc_id);
        auto i = engine.lookup(ini_id);
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

    ASSERT_TRUE(engine->start().has_value()) << "engine.start() failed";

    // Phase 1: establish both sessions (single-threaded drive until both Active).
    bool both_active = wait_both_active(ioc, *engine, acc_id, ini_id, 8000ms);
    if (!both_active) {
        stop_engine_sync(ioc, *engine);
        FAIL() << "V-1: sessions did not reach Active within 8s";
    }

    // Phase 2: Add background threads → genuinely multi-threaded executor.
    // work_guard keeps workers alive so ioc.run_for()/restart() is not needed
    // from the main thread (restart() while workers are in run() is asio UB).
    // [[feedback_single_threaded_harness_masks_strand_races]]
    auto wg = asio::make_work_guard(ioc);
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
        // wait_until_observed: no ioc.run_for()/restart() while t1/t2 are in run().
        bool stop_done = wait_until_observed(
            [&]{ return stop_fut.wait_for(0ms) == std::future_status::ready; }, 8000ms);
        wg.reset();   // release guard → workers exit when queue drains
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

    ASSERT_TRUE(engine->start().has_value()) << "engine.start() failed";

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

    ASSERT_TRUE(engine->start().has_value()) << "engine.start() failed";

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
        // wait_until_observed: no restart() while t1/t2 are in ioc.run().
        bool send_done = wait_until_observed(
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
    bool fa_fired = wait_until_observed(
        [&]{ return app->from_app_count.load(std::memory_order_acquire) >= 1; }, 5000ms);
    EXPECT_TRUE(fa_fired) << "V-9(a): fromApp must fire on the acceptor after initiator send";

    if (fa_fired) {
        // Wait for re-entrant send to resolve (done OR error; MUST NOT hang).
        bool re_resolved = wait_until_observed(
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
        // wait_until_observed: no restart() while t1/t2 are in ioc.run().
        bool stop_done = wait_until_observed(
            [&]{ return stop_fut.wait_for(0ms) == std::future_status::ready; }, 8000ms);
        ioc.stop();   // safe: stop() from any thread is defined behaviour
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
        // ioc was stopped above; no workers running — restart() is safe here.
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

    ASSERT_TRUE(engine->start().has_value()) << "engine.start() failed";

    // Establish both sessions.
    bool both_active = wait_both_active(ioc, *engine, acc_id, ini_id, 8000ms);
    if (!both_active) {
        stop_engine_sync(ioc, *engine);
        FAIL() << "V-10: sessions did not reach Active within 8s";
    }

    // Access the live session objects via lookup().
    // T024: lookup() returns shared_ptr<Session> (bounded handle).
    auto acc_session = engine->lookup(acc_id);
    auto ini_session = engine->lookup(ini_id);

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

    // V-10 core assertion (gate-b/r1 #6 strengthened):
    // socket_.get_executor() must EQUAL the session's own strand executor —
    // NOT merely be "some strand" (a wrong or double-wrapped strand passes
    // the weaker target<strand_t>() != nullptr check but fails this equality).
    //
    // asio::any_io_executor::operator== compares the contained executor by
    // identity (same strand object).  This rejects:
    //   - a bare io_context executor (pre-T011 RED)
    //   - a double-wrapped strand (a strand of a strand)
    //   - the wrong session's strand
    //
    // acc_session->executor().underlying() is the any_io_executor holding the
    // asio::strand<asio::any_io_executor> that was emplaced in start() for this
    // session.  The socket's executor must be exactly this object.
    //
    // The older target<strand_t>() != nullptr check is kept alongside to
    // preserve its RED-proof explanation; both must pass.

    auto& acc_sock = fixpp::transport::asio_tls_transport_test_access::socket_of(*acc_tls);
    auto& ini_sock = fixpp::transport::asio_tls_transport_test_access::socket_of(*ini_tls);

    using strand_t = asio::strand<asio::any_io_executor>;

    // Stronger equality check: socket executor == session's own strand.
    const asio::any_io_executor acc_expected_exec = acc_session->executor().underlying();
    const asio::any_io_executor ini_expected_exec = ini_session->executor().underlying();

    EXPECT_EQ(acc_sock.get_executor(), acc_expected_exec)
        << "V-10: acceptor socket executor != session's own strand.\n"
        << "  A bare executor, wrong strand, or double-wrapped strand all fail here.\n"
        << "  Pre-T011 RED: socket on bare exec_; post-T011 GREEN: socket on session_strand.\n"
        << "  [gate-b/r1 #6: equality check, not just is-a-strand]";

    EXPECT_EQ(ini_sock.get_executor(), ini_expected_exec)
        << "V-10: initiator socket executor != session's own strand.\n"
        << "  A bare executor, wrong strand, or double-wrapped strand all fail here.\n"
        << "  Pre-T011 RED: socket on bare exec_; post-T011 GREEN: socket on session_strand.\n"
        << "  [gate-b/r1 #6: equality check, not just is-a-strand]";

    // Legacy target<strand_t>() != nullptr checks (kept for RED-proof documentation).
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
//   This retargeted V-8 witnesses the GENUINELY HB-free control-plane data race.
//
// ── Historical RED mechanism (pre-T026) ──────────────────────────────────────
//   PUBLIC READER vs MAP MUTATION (no live peer needed):
//
//     Thread-reader (raw std::thread): calls engine.acceptor_bound_endpoint(acc_id)
//       and/or engine.lookup(acc_id) in a tight loop — these read listener_endpoints_
//       and registry_ DIRECTLY WITHOUT any synchronisation (no strand, no mutex,
//       no atomic).  [pre-T026 engine.cpp:1227-1230, 134-136]
//
//     Engine executor threads: the accept loop WRITES listener_endpoints_[id] at
//       run_accept_loop startup; stop() CLEARS both maps.
//
//     NO happens-before edge exists between the reader thread and the engine
//     executor threads — the reader is a raw std::thread with no synchronisation.
//     TSan observes: WRITE in engine-thread vs READ in reader-thread → DATA RACE.
//
//   RED expected: pre-T018 (public readers not yet protected by D-SNAP/T023-T024).
//
// ── Current GREEN mechanism (post-T026 / D-SNAP installed) ───────────────────
//   acceptor_bound_endpoint() and lookup() now load an immutable snapshot via
//   std::atomic<shared_ptr<const Snapshot>> (atomic_load — no direct map access).
//   The map mutations (write + clear) still happen on the control strand, but the
//   raw std::thread reader never touches the maps directly.  TSan observes only
//   atomic operations between the reader and engine threads → DATA RACE gone → GREEN.
//
//   The reader thread still spins with no explicit sync object — the synchronisation
//   is entirely inside the atomic<shared_ptr> load, which is the point of D-SNAP.
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
// Widen the overlap window:
//   The reader spins throughout the whole start→stop duration (not just a brief
//   window) so even on a fast machine the read reliably overlaps the write or clear.
//
// GREEN expected: post-T026 (D-SNAP snapshot readers installed).
//
// Anti-hang: 15s hard budget; engine always stop()'d before test exit; reader
//   thread holds a stop flag set under engine teardown.
//
// No live peer needed: the listener binds before any peer connects; the write
//   at listener_endpoints_[id] happens during accept-loop startup.
//
// [DD-2026-06-06; spec SC-002; contract V-8; tasks T016/T017; research D-SNAP;
//  gate-b/r1 #7: historical RED vs current GREEN mechanism documented separately]

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

    ASSERT_TRUE(engine->start().has_value()) << "engine.start() failed";

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

    ASSERT_TRUE(engine->start().has_value()) << "engine.start() failed";

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
    // After stop(), lookup() returns nullptr (empty shared_ptr, registry cleared in step 5).
    // This confirms that NO live transport was published behind the in-progress stop().
    //
    // [INV-2a: publish_entry checks stopped_.load(acquire) FIRST → false → no publish]
    // [contract C-6: stopped disposition → loop closes without initiating any read]
    auto acc_session = engine->lookup(acc_id);
    EXPECT_EQ(acc_session, nullptr)
        << "V-12: acceptor session must NOT be reachable after stop() clears the registry\n"
        << "  A non-null Session* here would indicate the registry was NOT cleared\n"
        << "  (or that lookup() was called before the registry clear completed).\n"
        << "  [INV-2a check in publish_entry (T013) ensures: stopped_ true → no publish\n"
        << "   of live_transport → loop closes without entering read pump]\n"
        << "  V-12 is ALREADY-GREEN because the INV-2a check was implemented in T013.\n"
        << "  T019 is a no-op/confirmation per tasks.md T016/T017.";
}

// ── V-12b: StopBeforePublish_WithLiveTransport (gate-b/r1 #3) ────────────────
//
// Strengthened V-12 witness using the FIXPP_TEST_HOOKS pre-publish seam.
// Drives stop() while a live transport exists but publish_entry has NOT yet run
// (the seam pauses the accept loop between step 7 and step 7a).
//
// Mechanism:
//   1. Register an acceptor session.  Install engine.test_hook_pre_publish_ hook.
//   2. Start the engine (loops on ≥2 threads).
//   3. Connect a loopback TLS initiator (drives the accept loop past handshake
//      and Logon-frame to the seam point: transport created + attached).
//   4. The hook signals seam_reached and then waits for seam_release.
//   5. Test observes seam_reached, calls stop().
//   6. Test sets seam_release so the hook returns, the accept loop calls
//      publish_entry, which observes stopped_=true → stopped disposition.
//   7. Assert: no session published (lookup returns nullptr after stop()).
//      The stopped disposition branch in publish_entry must have been taken.
//
// This is the GENUINE V-12 witness per contracts/C-6 + gate-b/r1 finding #3:
// the seam ensures the loop had a live transport when publish_entry was called,
// yet the stopped disposition was correctly observed.
//
// Coverage: publish_entry's stopped-disposition branch (stopped_=true → co_return
// false) is now reachable with a real transport → GREEN-covered.
//
// [contracts C-6/V-12/INV-2a; gate-b/r1 #3; data-model D-PUB]

TEST(EngineSessionStrand, V12b_StopBeforePublish_WithLiveTransport) {
    const char* fixture_dir = get_fixture_dir();
    if (!fixture_dir || fixture_dir[0] == '\0')
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";

    asio::io_context ioc;
    const uint16_t port = reserve_free_port(ioc);
    auto fac = make_tls_factory(fixture_dir);
    if (!fac) GTEST_SKIP() << "TLS factory build failed (cert/key not available)";

    // Seam control:
    //   seam_reached: set by the hook when transport is attached, pre-publish.
    //   seam_release: test sets this to true to release the hook after stop().
    std::atomic<bool> seam_reached{false};
    std::atomic<bool> seam_release{false};

    auto acc_cfg = make_session_cfg(
        fac, "ACCEPTOR_V12B", "INITIATOR_V12B",
        fixpp::session::session_role::acceptor, "INITIATOR_V12B",
        ioc.get_executor(), port);
    auto ini_cfg = make_session_cfg(
        fac, "INITIATOR_V12B", "ACCEPTOR_V12B",
        fixpp::session::session_role::initiator, "ACCEPTOR_V12B",
        ioc.get_executor(), port);

    // Use a fast reconnect policy so that if the first TCP connect attempt races
    // the accept loop's listener bind (which is synchronous but happens after
    // co_spawn scheduling), the initiator retries quickly (100ms) instead of
    // the defaults_quickfix_compat 30s.  This makes the seam reliably reachable
    // within the 8s wait budget.
    {
        fixpp::transport::ReconnectPolicy fast_policy;
        fast_policy.schedule =
            std::pmr::vector<std::chrono::milliseconds>{std::pmr::get_default_resource()};
        fast_policy.schedule.push_back(std::chrono::milliseconds{100});
        fast_policy.jitter = 0.0;
        fast_policy.max_attempts = 0;  // unbounded
        ini_cfg.reconnect_policy = std::move(fast_policy);
    }

    const SessionId acc_id = SessionId::from_config(acc_cfg);

    fixpp::core::EngineConfig ecfg;
    ecfg.executor = ioc.get_executor();
    ecfg.clock = make_mock_clock(ioc);

    auto engine = std::make_unique<fixpp::session::Engine>(ioc.get_executor(), std::move(ecfg));

    if (!engine->register_session(std::move(acc_cfg)).has_value()) {
        stop_engine_sync(ioc, *engine);
        FAIL() << "V-12b: acceptor register_session failed";
    }
    if (!engine->register_session(std::move(ini_cfg)).has_value()) {
        stop_engine_sync(ioc, *engine);
        FAIL() << "V-12b: initiator register_session failed";
    }

    // Install the pre-publish seam hook via the public setter (FIXPP_TEST_HOOKS).
    // The hook runs on the session strand (coroutine context of run_accept_loop).
    // It: (1) signals seam_reached, (2) polls seam_release in short increments
    //     (yielding the session strand each time via a timer), (3) returns so the
    //     accept loop can call publish_entry and observe stopped_=true.
    engine->set_pre_publish_hook([&seam_reached, &seam_release]() -> asio::awaitable<void> {
        // Signal the test: transport is attached, pre-publish seam reached.
        seam_reached.store(true, std::memory_order_release);

        // Build a short-lived timer on the current (session-strand) executor.
        auto exec = co_await asio::this_coro::executor;
        asio::steady_timer t{exec};

        // Poll seam_release in short increments to yield the session strand.
        // Total budget: 5s (stop() will cancel us before then via session_cancel).
        while (!seam_release.load(std::memory_order_acquire)) {
            t.expires_after(std::chrono::milliseconds{5});
            try {
                co_await t.async_wait(asio::use_awaitable);
            } catch (...) {
                break;  // cancelled (stop() emits total) → let publish_entry observe stopped_
            }
        }
        co_return;
    });

    ASSERT_TRUE(engine->start().has_value()) << "engine.start() failed";

    // Drive ioc on 2 background threads so the accept loop and initiator
    // can run concurrently with the test's stop() call.
    // work_guard keeps workers alive so restart() is never needed from the main thread.
    auto wg = asio::make_work_guard(ioc);
    std::thread t1{[&ioc]{ ioc.run(); }};
    std::thread t2{[&ioc]{ ioc.run(); }};

    // Wait for the seam to be reached (transport created, pre-publish pause).
    // Budget: 8s (covers TLS handshake + Logon frame delivery).
    // wait_until_observed: no restart() while t1/t2 are in ioc.run().
    bool seam_reached_ok = wait_until_observed(
        [&]{ return seam_reached.load(std::memory_order_acquire); },
        std::chrono::seconds{8});

    if (!seam_reached_ok) {
        // Seam not reached — release and clean up.
        seam_release.store(true, std::memory_order_release);
        wg.reset();
        t1.join();
        t2.join();
        GTEST_SKIP() << "V-12b: seam not reached within 8s (loopback initiator may not have connected)";
    }

    // Seam reached: call stop() NOW while the accept loop is paused with a live
    // transport between step 7 and step 7a.  stop() sets stopped_=true on the
    // control strand.  publish_entry will observe this.
    //
    // Ordering strategy to avoid deadlock:
    //   (a) Spawn stop() — stop() queues on the control strand.
    //   (b) Poll until engine->stopped() is true (stop() step 1 ran).
    //       At this point: stopped_ = true; no entry.live_transport (publish_entry
    //       not yet called) → stop() step 2 skips acceptor close → no session-strand
    //       dependency from stop() for the acceptor.
    //   (c) Set seam_release = true — hook exits its poll loop next iteration.
    //   (d) Poll until stop() completes — accept loop calls publish_entry
    //       (observes stopped_ = true → stopped disposition), calls close(terminal),
    //       co_returns → counter decrements → join completes.
    {
        auto stop_fut = asio::co_spawn(
            ioc.get_executor(), engine->stop(), asio::use_future);

        // (b) Poll until stopped_ = true (stop() step 1 on control strand).
        // wait_until_observed: no restart() while t1/t2 are in ioc.run().
        bool stopped_flag_set = wait_until_observed(
            [&]{ return engine->stopped(); },
            3000ms);

        // (c) Release the seam — hook exits, accept loop proceeds to publish_entry.
        seam_release.store(true, std::memory_order_release);

        // (d) Poll until stop() coroutine fully completes.
        bool done = wait_until_observed(
            [&]{ return stop_fut.wait_for(0ms) == std::future_status::ready; },
            10000ms);

        wg.reset();   // release guard → workers exit when queue drains
        t1.join();
        t2.join();

        (void)stopped_flag_set;  // informational only; stop() will complete regardless
        ASSERT_TRUE(done) << "V-12b: engine.stop() did not complete within 10s";
        stop_fut.get();
    }

    ASSERT_TRUE(engine->stopped()) << "V-12b: engine must be stopped";

    // V-12b primary assertion: stopped disposition was taken.
    // publish_entry observed stopped_=true → did NOT write entry.session or
    // entry.live_transport → accept loop called close(terminal) and returned.
    // After stop(), lookup() returns nullptr (registry cleared).
    auto acc_session = engine->lookup(acc_id);
    EXPECT_EQ(acc_session, nullptr)
        << "V-12b: acceptor session must NOT be reachable.\n"
        << "  publish_entry must have taken the stopped disposition (INV-2a):\n"
        << "  stopped_=true → co_return false → no live_transport published.\n"
        << "  If non-null: the stopped disposition branch was NOT exercised.\n"
        << "  [gate-b/r1 #3: V-12 seam now reaches publish_entry with live transport]";
}

// ── V-4: SingleThreadedSuiteUnchanged ────────────────────────────────────────
//
// V-4 characterization (T021/T022):
//   The existing single-threaded session test suite (42 tests, ctest -R '^session_')
//   must remain GREEN with no rewrites across the US3 implementation (SC-003/FR-007).
//
// V-4 BASELINE (pre-T021): 42 session tests pass under linux-clang-debug.
//   Test names: session_smoke … session_tc_liveness (see T022 report).
//   V-4 is verified by running `ctest -R '^session_'` — NOT by this cell.
//
// This test IS V-4's compile-time gate and also serves V-5:
//   It verifies that Engine::lookup() currently returns Session* (raw pointer),
//   NOT std::shared_ptr<Session> (which T024 will introduce).
//
//   static_assert on the return type of lookup() pinned to the CURRENT signature.
//   This cell is GREEN now (pre-T024) and compile-fails after T024 (the expected
//   "one diff" signal for V-5).  T026 updates it to shared_ptr<Session> (GREEN again).
//
// V-5 ABI baseline:
//   The nm baseline (Engine public symbols, pre-T024) is saved at:
//     tests/abi/baseline/libfixpp_session_engine_pre023.nm
//   Generated by:
//     nm --defined-only build/linux-clang-debug/lib/libfixpp_session.a |
//       c++filt | grep 'fixpp::session::Engine::' | grep -E '^[0-9a-f]+ T' | sort
//   See tasks.md T022 report for the captured content.
//
//   Note: Itanium ABI does NOT encode the return type of non-template non-virtual
//   functions in the mangled symbol name.  The mangled symbol for lookup() is:
//     _ZNK5fixpp7session6Engine6lookupERKNS0_9SessionIdE
//   before AND after T024 (the return type change Session*→shared_ptr<Session> is
//   INVISIBLE to nm).  The compile-time static_assert in this cell is therefore
//   the PRIMARY "one diff" detector for V-5.  The nm baseline provides secondary
//   corroboration that NO OTHER Engine symbols change (symbol set is stable).
//
// [C-7/V-4; C-4/V-5; SC-003/SC-004/FR-007; tasks T021/T022]

TEST(EngineSessionStrand, V4V5_SingleThreadedBaselineAndAbiGate) {
    // V-4: compile-time assertion that lookup() returns Session* (pre-T024 signature).
    // This pinned static_assert is the V-5 "one diff" compile-time gate:
    //   - GREEN now: lookup() → Session* (raw pointer, as declared in engine.hpp:249)
    //   - compile-FAIL after T024: lookup() → std::shared_ptr<Session>
    //     → that compile failure is the expected single ABI change for V-5
    //   - T026 updates this to shared_ptr<Session> (GREEN again)
    //
    // [anchor: engine.hpp:249 "Session* lookup(SessionId const& id) const"]
    // [anchor: contracts C-4 "one intended, recorded change only"]
    // T024 (FR-008/SC-004/D-SNAP): lookup() return type changed from Session* to
    // std::shared_ptr<Session>.  This static_assert is flipped to verify the new
    // type (the "one intended ABI diff" confirmed by V-5).  The Itanium mangled
    // symbol name does NOT encode the return type, so the nm baseline at
    // tests/abi/baseline/libfixpp_session_engine_pre023.nm shows 0 symbol-name
    // changes — but this compile-time check confirms the signature changed.
    // [tasks T024/T026; contracts C-4/C-8; research D8/D-SNAP]
    using LookupReturnType = decltype(
        std::declval<fixpp::session::Engine const&>().lookup(
            std::declval<fixpp::session::SessionId const&>()));
    static_assert(
        std::is_same_v<LookupReturnType, std::shared_ptr<fixpp::session::Session>>,
        "V-4/V-5: Engine::lookup() must return std::shared_ptr<Session> (T024).\n"
        "If this static_assert FAILS, T024's return-type change was not applied.\n"
        "anchor: contracts/engine-session-strand.md C-4 + research.md D-SNAP");

    // V-5: runtime confirmation that the Engine public API compiles correctly
    // with the current shared_ptr<Session> return type.  Verifies the ABI gate is wired.
    //
    // The nm baseline at tests/abi/baseline/libfixpp_session_engine_pre023.nm
    // was captured pre-T024 (T022); T026 diffs the post-T024 nm output against it
    // and confirms exactly 0 symbol-NAME changes (return type is not in the mangled
    // name, but the symbol SET is stable).
    //
    // C-4 update (gate-b/r1 #5): the PR ships ONE ABI-changing delta (lookup() return
    // type) plus TWO additive backward-compatible engine-internal public additions:
    //   1. SessionConfig::engine_adopt_strand (new optional field, aggregate-init safe)
    //   2. fixpp::core::adopt_strand_t + make_session_executor(adopt_strand_t, …)
    // These two additive changes are intentional and documented in contracts/C-4.
    //
    // No runtime assertion is needed here — the static_assert above IS the gate.
    // This PASS confirms the static_assert compiled and the type is correct.
    SUCCEED();  // static_assert above is the meaningful assertion; this marks the cell green
}

// ── V-11: SnapshotReadersMtSafe ──────────────────────────────────────────────
//
// US3 RED-phase witness.  Three parts — only Part 1 runs now (compiles and
// executes with the current shared_ptr<Session> return type from lookup()).
// Parts 2 and 3 are deferred (#if 0) until T025 respectively.
//
// ── PART 1 — TSan-race ───────────────────────────────────────────────────────
//
// Witnesses the same public-reader data-race as V-8, but with a broader
// surface (both lookup() AND acceptor_bound_endpoint() in the reader thread)
// and while a LIVE session pair is running (not just an acceptor-only setup).
//
// ── Historical RED mechanism (pre-T026) ──────────────────────────────────────
//   A raw std::thread spins calling:
//     engine.lookup(acc_id)               → reads registry_ (unordered_map) DIRECTLY
//     engine.lookup(ini_id)               → reads registry_ DIRECTLY
//     engine.acceptor_bound_endpoint(id)  → reads listener_endpoints_ DIRECTLY
//   with NO shared sync object between t_reader and the engine threads.
//   stop() clears registry_ and listener_endpoints_ on the control strand —
//   concurrent WRITE vs t_reader's concurrent READ.
//   Under TSan with halt_on_error=1: DATA RACE → process aborted → test RED.
//
// ── Current GREEN mechanism (post-T026 / D-SNAP installed) ───────────────────
//   lookup() and acceptor_bound_endpoint() now load an immutable snapshot via
//   std::atomic<shared_ptr<const Snapshot>> (atomic_load).  No direct map access
//   from the raw reader thread; the maps are only touched on the control strand.
//   TSan observes only atomic operations → DATA RACE gone → GREEN.
//
// Key difference from V-8: V-11 uses a LIVE session (established Active state)
// so the registry_ is populated and lookup() actually finds an entry — wider
// race surface than V-8's acceptor-only setup.
//
// GREEN expected: post-T026 (T023/T024 installed D-SNAP; lookup() reads snapshot).
//
// [gate-b/r1 #7: historical RED mechanism and current GREEN mechanism documented
//  separately for V-11 Part 1, matching the V-8 comment update]
//
// ── PART 2 — keepalive (DEFERRED until T024) ─────────────────────────────────
//
// TODO-T024: after T024 changes lookup() → shared_ptr<Session>, enable this block.
// It verifies: a shared_ptr<Session> handle obtained BEFORE clear() (while the
// engine is alive) keeps its Session alive ACROSS the registry_.clear() call.
// This witnesses the bounded-handle keepalive: the Session is not freed until
// the last shared_ptr copy is destroyed, even after stop()/clear().
//
// Assertion sequence (to enable in T026):
//   1. Establish a session.
//   2. Obtain: auto handle = engine.lookup(acc_id);   // shared_ptr<Session>
//              ASSERT_NE(handle, nullptr);
//   3. Call stop().  stop() clears registry_ — but the handle's control block
//      keeps the Session alive.
//   4. ASSERT: handle != nullptr (still valid)
//              handle->state() is observable (not UAF)
//   This proves the bounded keepalive contract (INV-9/C-8/FR-008/FR-014).
//
// ── PART 3 — lease/~Engine assert (DEFERRED until T025) ─────────────────────
//
// TODO-T025: after T025 adds the debug-only lease control block, enable this block.
// It verifies: ~Engine debug-asserts that no outstanding lookup()/snapshot handles
// remain (the lease counter must be zero at destruction).
//
// Assertion sequence (to enable in T026):
//   1. Obtain a handle: auto h = engine.lookup(acc_id);
//   2. Call stop() + destroy the engine (unique_ptr reset).
//   3. ASSERT: engine destruction aborted (SIGABRT) because h is still alive.
//      OR: ASSERT: if h is destroyed first, destruction succeeds (counter=0).
//   This witnesses INV-9a/C-8 (the bounded-handle precondition).
//   In the NON-abort path: h is destroyed before engine → counter=0 → PASS.
//   In the ABORT path: h is alive when engine destroys → counter=1 → abort.
//   Test uses the NON-abort path: destroy h, then stop+destroy engine.
//
// [C-8/V-11; data-model E-7/INV-9/INV-9a; research D-SNAP/R7; tasks T021/T022]

TEST(EngineSessionStrand, V11_SnapshotReadersMtSafe) {
    const char* fixture_dir = get_fixture_dir();
    if (!fixture_dir || fixture_dir[0] == '\0')
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";

    asio::io_context ioc;
    const uint16_t port = reserve_free_port(ioc);
    auto fac = make_tls_factory(fixture_dir);
    if (!fac) GTEST_SKIP() << "TLS factory build failed (cert/key not available)";

    // Use an ACCEPTOR-ONLY session (same as V-8) to ensure the race window is wide:
    // the reader thread starts spinning BEFORE and DURING accept-loop startup, so
    // both the WRITE (listener_endpoints_ insert in the accept loop) and the CLEAR
    // (stop()'s clear()) race with the reader.
    //
    // A live-session pair would require wait_both_active (slow) before starting the
    // reader, potentially closing the race window between the last map write and the
    // first reader iteration.  V-11 uses the same approach as V-8 to guarantee the
    // race is exposed, but extends the reader to cover ALL THREE public readers
    // (lookup for both acc_id and ini_id, and acceptor_bound_endpoint) — wider
    // surface than V-8 which reads only acc_id.
    //
    // [Race surface: listener_endpoints_ WRITE (accept-loop startup) vs READ
    //  (t_reader→acceptor_bound_endpoint), and registry_ CLEAR (stop()) vs READ
    //  (t_reader→lookup).  No HB edge between t_reader and engine threads.]
    auto acc_cfg = make_session_cfg(
        fac, "ACCEPTOR_V11", "INITIATOR_V11",
        fixpp::session::session_role::acceptor, "INITIATOR_V11",
        ioc.get_executor(), port);
    // Register the acceptor id for lookup(); lookup() returns nullptr pre-establish
    // (correct per the "null is NOT an error" contract), but the find() itself still
    // reads registry_ — that read races the stop()-clear.
    const SessionId acc_id = SessionId::from_config(acc_cfg);
    // Use a synthesized initiator id to widen the registry_ read surface (lookup returns
    // nullptr for an unregistered id, but STILL reads registry_ — wider race surface).
    auto ini_cfg_dummy = make_session_cfg(
        fac, "INITIATOR_V11", "ACCEPTOR_V11",
        fixpp::session::session_role::initiator, "ACCEPTOR_V11",
        ioc.get_executor(), port);
    const SessionId ini_id = SessionId::from_config(ini_cfg_dummy);

    fixpp::core::EngineConfig ecfg;
    ecfg.executor = ioc.get_executor();
    ecfg.clock = make_mock_clock(ioc);

    auto engine = std::make_unique<fixpp::session::Engine>(ioc.get_executor(), std::move(ecfg));

    if (!engine->register_session(std::move(acc_cfg)).has_value()) {
        stop_engine_sync(ioc, *engine);
        FAIL() << "V-11: acceptor register_session failed";
    }
    // Do NOT register ini_cfg_dummy — we want lookup(ini_id) to do a registry_.find()
    // that returns end() (no entry).  The find() itself reads the unordered_map →
    // races the stop()-clear() just as a successful find() would.

    ASSERT_TRUE(engine->start().has_value()) << "engine.start() failed";

    // Part 1: TSan-race witness.
    //
    // Start engine executor threads t1 and t2.  Then start t_reader (a raw std::thread,
    // no asio executor — no implicit TSan synchronisation with engine threads).
    // t_reader spins calling all three public readers throughout the start→stop window:
    //   - acceptor_bound_endpoint(acc_id): reads listener_endpoints_
    //   - lookup(acc_id):                  reads registry_ (entry found)
    //   - lookup(ini_id):                  reads registry_ (entry NOT found — but find() runs)
    //
    // Race window 1 (write): accept loop writes listener_endpoints_[acc_id] at startup
    //   (engine.cpp ~:617) while t_reader reads it.  TSan: WRITE vs READ.
    // Race window 2 (clear): stop() clears listener_endpoints_ and registry_
    //   (engine.cpp ~:1128-1130) while t_reader reads them.  TSan: CLEAR vs READ.
    //
    // NO shared sync object between t_reader and the engine threads (only relaxed
    // reader_stop at exit — set AFTER stop() completes, so during stop() there
    // is zero HB between the two sides).
    //
    // V-11 RED SIGNAL (pre-T026):
    //   TSan fires DATA RACE on listener_endpoints_ or registry_ → halt_on_error=1
    //   aborts the process.
    // V-11 GREEN (post-T026):
    //   D-SNAP installed: acceptor_bound_endpoint() and lookup() go through
    //   atomic_load of the immutable snapshot → no unsynchronised map access.

    // Start engine executor threads AFTER engine.start() (per [[feedback_fork_inherited_asio_pool_deadlock]]).
    // work_guard keeps workers alive so restart() is never needed from the main thread.
    auto wg = asio::make_work_guard(ioc);
    std::thread t1{[&ioc]{ ioc.run(); }};
    std::thread t2{[&ioc]{ ioc.run(); }};

    std::atomic<bool> reader_stop{false};
    std::atomic<int>  reader_iters{0};

    // t_reader: raw std::thread, no asio executor.  Spins all three public readers.
    std::thread t_reader{[&engine, &acc_id, &ini_id, &reader_stop, &reader_iters]() {
        while (!reader_stop.load(std::memory_order_relaxed)) {
            // Read listener_endpoints_ via acceptor_bound_endpoint().
            (void)engine->acceptor_bound_endpoint(acc_id);
            // Read registry_ via lookup() — one successful find, one failed find.
            (void)engine->lookup(acc_id);
            (void)engine->lookup(ini_id);
            reader_iters.fetch_add(1, std::memory_order_relaxed);
        }
    }};

    // Let the accept loop start and write listener_endpoints_ (race window 1).
    // 30ms matches V-8's window — sufficient for the accept loop to run on t1/t2.
    // sleep_for instead of ioc.run_for()+restart(): restart() while t1/t2 are in
    // ioc.run() is asio UB (SEGFAULT under gcc-release).
    std::this_thread::sleep_for(std::chrono::milliseconds{30});

    // Call stop(): triggers the registry_.clear() + listener_endpoints_.clear() (race window 2).
    {
        auto stop_fut = asio::co_spawn(
            ioc.get_executor(), engine->stop(), asio::use_future);
        // wait_until_observed: no restart() while t1/t2 are in ioc.run().
        bool done = wait_until_observed(
            [&]{ return stop_fut.wait_for(0ms) == std::future_status::ready; },
            12000ms);

        // Signal t_reader to stop AFTER stop() completes (no HB edge during stop()).
        reader_stop.store(true, std::memory_order_relaxed);

        wg.reset();   // release guard → workers exit when queue drains
        t1.join();
        t2.join();
        t_reader.join();

        ASSERT_TRUE(done) << "V-11 Part 1: engine.stop() did not complete within 12s";
        stop_fut.get();
    }

    // Confirm the reader ran enough iterations to cover the race windows.
    EXPECT_GT(reader_iters.load(), 0)
        << "V-11 Part 1: reader thread must have iterated at least once";

    // Confirm engine is stopped (reached only if TSan did not abort with halt_on_error=1).
    EXPECT_TRUE(engine->stopped())
        << "V-11 Part 1: engine must be stopped after stop() completes";

    // ── V-11 PART 2: keepalive [T024] ────────────────────────────────────────
    // A shared_ptr<Session> handle obtained BEFORE stop()/clear() keeps the
    // Session alive ACROSS the registry_.clear() call.
    // [anchor: C-8 "bounded handle — valid while Engine is alive";
    //  data-model INV-9 "keepalive across registry_.clear()"]
    //
    // Requires a live session pair so that publish_entry() runs and the session
    // appears in the snapshot (acceptor-only sessions are never published until
    // an initiator connects).  Reuses the main ioc (restart after Part 1).
    {
        ioc.restart();
        const uint16_t port2 = reserve_free_port(ioc);
        fixpp::core::EngineConfig ecfg2;
        ecfg2.executor = ioc.get_executor();
        ecfg2.clock = make_mock_clock(ioc);
        auto acc_cfg2 = make_session_cfg(
            fac, "ACC_P2", "INI_P2", fixpp::session::session_role::acceptor, "INI_P2",
            ioc.get_executor(), port2);
        auto ini_cfg2 = make_session_cfg(
            fac, "INI_P2", "ACC_P2", fixpp::session::session_role::initiator, "ACC_P2",
            ioc.get_executor(), port2);
        const SessionId acc_id2 = SessionId::from_config(acc_cfg2);
        const SessionId ini_id2 = SessionId::from_config(ini_cfg2);

        auto engine2 = std::make_unique<fixpp::session::Engine>(ioc.get_executor(), std::move(ecfg2));
        if (!engine2->register_session(std::move(acc_cfg2)).has_value()) {
            stop_engine_sync(ioc, *engine2);
            FAIL() << "V-11 Part 2: acceptor register_session failed";
        }
        if (!engine2->register_session(std::move(ini_cfg2)).has_value()) {
            stop_engine_sync(ioc, *engine2);
            FAIL() << "V-11 Part 2: initiator register_session failed";
        }
        if (!engine2->start().has_value()) {
            stop_engine_sync(ioc, *engine2);
            FAIL() << "V-11 Part 2: engine2.start() failed";
        }

        // Wait for both sessions to reach Active (publish_entry has run).
        bool both_active = wait_both_active(ioc, *engine2, acc_id2, ini_id2, 8000ms);
        if (!both_active) {
            stop_engine_sync(ioc, *engine2);
            FAIL() << "V-11 Part 2: sessions did not reach Active within 8s";
        }

        // 1. Obtain handle BEFORE stop() — lookup returns non-null (session published).
        std::shared_ptr<fixpp::session::Session> handle = engine2->lookup(acc_id2);
        if (!handle) {
            stop_engine_sync(ioc, *engine2);
            FAIL() << "V-11 Part 2: lookup() returned null for active session";
        }
        fixpp::session::Session* raw_ptr = handle.get();
        EXPECT_EQ(handle->state(), fsm_state::Active)
            << "V-11 Part 2: session must be Active before stop()";

        // 2. Call stop() — clears registry_, but handle keeps Session alive.
        stop_engine_sync(ioc, *engine2);
        ASSERT_TRUE(engine2->stopped());

        // 3. Engine is still alive (unique_ptr not reset); handle keeps Session.
        ASSERT_NE(handle, nullptr)
            << "V-11 Part 2: handle must remain non-null after stop()";
        ASSERT_EQ(handle.get(), raw_ptr)
            << "V-11 Part 2: Session must not have been reallocated (keepalive)";
        (void)handle->state();  // Must not crash/UAF.

        // 4. Destroy handle BEFORE engine2 goes out of scope (counter → 0).
        handle.reset();
    }  // engine2 destroyed: ~Engine with lease_counter_ == 0 → no abort.

    // ── V-11 PART 3: lease / ~Engine assert [T025] ───────────────────────────
    // [anchor: C-8 "~Engine MUST debug-assert no outstanding handles";
    //  data-model INV-9a; research R7 "NOT a stop() drain"]
    //
    // Verify ~Engine with zero outstanding handles does NOT abort.
    // The ABORT path (handle outlives ~Engine) is intentionally not exercised
    // (SIGABRT would kill the test process); it is documented in INV-9a.
#ifndef NDEBUG
    {
        ioc.restart();
        const uint16_t port3 = reserve_free_port(ioc);
        fixpp::core::EngineConfig ecfg3;
        ecfg3.executor = ioc.get_executor();
        ecfg3.clock = make_mock_clock(ioc);
        auto acc_cfg3 = make_session_cfg(
            fac, "ACC_P3", "INI_P3", fixpp::session::session_role::acceptor, "INI_P3",
            ioc.get_executor(), port3);
        auto ini_cfg3 = make_session_cfg(
            fac, "INI_P3", "ACC_P3", fixpp::session::session_role::initiator, "ACC_P3",
            ioc.get_executor(), port3);
        const SessionId acc_id3 = SessionId::from_config(acc_cfg3);
        const SessionId ini_id3 = SessionId::from_config(ini_cfg3);

        auto engine3 = std::make_unique<fixpp::session::Engine>(ioc.get_executor(), std::move(ecfg3));
        if (!engine3->register_session(std::move(acc_cfg3)).has_value()) {
            stop_engine_sync(ioc, *engine3);
            FAIL() << "V-11 Part 3: acceptor register_session failed";
        }
        if (!engine3->register_session(std::move(ini_cfg3)).has_value()) {
            stop_engine_sync(ioc, *engine3);
            FAIL() << "V-11 Part 3: initiator register_session failed";
        }
        if (!engine3->start().has_value()) {
            stop_engine_sync(ioc, *engine3);
            FAIL() << "V-11 Part 3: engine3.start() failed";
        }

        bool both_active3 = wait_both_active(ioc, *engine3, acc_id3, ini_id3, 8000ms);
        if (!both_active3) {
            stop_engine_sync(ioc, *engine3);
            FAIL() << "V-11 Part 3: sessions did not reach Active within 8s";
        }

        // 1. Obtain handle.
        auto h = engine3->lookup(acc_id3);
        if (!h) {
            stop_engine_sync(ioc, *engine3);
            FAIL() << "V-11 Part 3: lookup() returned null for active session";
        }

        // 2. Stop the engine.
        stop_engine_sync(ioc, *engine3);

        // 3. Destroy handle BEFORE engine destruction (counter → 0).
        h.reset();

        // 4. Destroy engine3 — ~Engine: lease_counter_ == 0 → PASS (no abort).
        engine3.reset();
        SUCCEED() << "V-11 Part 3: ~Engine with no outstanding handles did not abort";
    }
#endif
}

// ── V-13: SendVsFsmTransition_NoRace (gate-b/r1 #1) ─────────────────────────
//
// Witnesses the data race that existed when Engine::send() read kl->state()
// (fsm_state_) on the control strand, while the per-session strand FSM writer
// concurrently transitioned the session to Disconnected (or any non-Active state).
//
// ── Historical RED mechanism (pre-fix) ───────────────────────────────────────
//   Engine::send() Step B (on control_strand_) called:
//     if (!kl || kl->state() != fsm_state::Active) { reject; }
//   Session::state() reads fsm_state_ (session.hpp:560) which is single-writer on
//   the per-session strand.  Any concurrent FSM transition (e.g. stop()-induced
//   terminal-close) from the session strand raced this control-strand read.
//   TSan with halt_on_error=1: DATA RACE on fsm_state_ → process aborts → RED.
//
// ── Current GREEN mechanism (post-fix) ───────────────────────────────────────
//   The kl->state() call is removed from the control strand entirely.  Step B
//   now only null-checks kl (session published).  The Active check moved to Step C
//   (session-strand lambda), where fsm_state_ is owned.  No cross-strand read.
//
// Mechanism:
//   1. Establish a session pair on a ≥3-thread executor.
//   2. Spawn 3 sender threads, each calling Engine::send() in a tight loop.
//   3. Call Engine::stop() (triggers FSM transitions) while the senders loop.
//   4. Under TSan: any cross-strand fsm_state_ read fires DATA RACE → RED.
//   5. Post-fix: all fsm_state_ reads are on the session strand → GREEN.
//
// Thread count: 3 engine threads + 3 sender threads = 6 total.
// Anti-hang: 10s stop budget; senders stop on sender_stop flag.
//
// [contracts C-0/C-1; gate-b/r1 #1; research D6/R7; session.hpp:556/560]

TEST(EngineSessionStrand, V13_SendVsFsmTransition_NoRace) {
    const char* fixture_dir = get_fixture_dir();
    if (!fixture_dir || fixture_dir[0] == '\0')
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";

    asio::io_context ioc;
    const uint16_t port = reserve_free_port(ioc);
    auto fac = make_tls_factory(fixture_dir);
    if (!fac) GTEST_SKIP() << "TLS factory build failed (cert/key not available)";

    auto acc_cfg = make_session_cfg(
        fac, "ACCEPTOR_V13", "INITIATOR_V13",
        fixpp::session::session_role::acceptor, "INITIATOR_V13",
        ioc.get_executor(), port);
    auto ini_cfg = make_session_cfg(
        fac, "INITIATOR_V13", "ACCEPTOR_V13",
        fixpp::session::session_role::initiator, "ACCEPTOR_V13",
        ioc.get_executor(), port);
    const SessionId acc_id = SessionId::from_config(acc_cfg);
    const SessionId ini_id = SessionId::from_config(ini_cfg);

    fixpp::core::EngineConfig ecfg;
    ecfg.executor = ioc.get_executor();
    ecfg.clock = make_mock_clock(ioc);

    auto engine = std::make_unique<fixpp::session::Engine>(ioc.get_executor(), std::move(ecfg));

    if (!engine->register_session(std::move(acc_cfg)).has_value()) {
        stop_engine_sync(ioc, *engine);
        FAIL() << "V-13: acceptor register_session failed";
    }
    if (!engine->register_session(std::move(ini_cfg)).has_value()) {
        stop_engine_sync(ioc, *engine);
        FAIL() << "V-13: initiator register_session failed";
    }

    ASSERT_TRUE(engine->start().has_value()) << "engine.start() failed";

    // Wait for both sessions to reach Active (ensures fsm_state_ is Active
    // before senders start, widening the send-vs-transition race window).
    bool both_active = wait_both_active(ioc, *engine, acc_id, ini_id, 8000ms);
    if (!both_active) {
        stop_engine_sync(ioc, *engine);
        FAIL() << "V-13: sessions did not reach Active within 8s";
    }

    // Start engine executor threads.  These drive the session-strand FSM
    // transitions (Active→Disconnecting→Disconnected) during stop().
    // work_guard keeps workers alive while we sleep (no ioc.run_for/restart UB).
    auto wg = asio::make_work_guard(ioc);
    std::thread t1{[&ioc]{ ioc.run(); }};
    std::thread t2{[&ioc]{ ioc.run(); }};
    std::thread t3{[&ioc]{ ioc.run(); }};

    std::atomic<bool> sender_stop{false};
    std::atomic<int>  send_iters{0};

    // Dummy payload — send will be rejected once the session transitions out of
    // Active, but that is tolerated.  We care only about no data race on fsm_state_.
    const std::array<std::byte, 4> dummy{};
    const std::span<const std::byte> payload_view{dummy};

    // 3 sender threads: each fires Engine::send() in a loop.
    // Pre-fix: kl->state() on control strand races session-strand FSM writes.
    // Post-fix: no cross-strand fsm_state_ read → TSan clean.
    auto sender_fn = [&]() {
        while (!sender_stop.load(std::memory_order_relaxed)) {
            auto fut = asio::co_spawn(
                ioc.get_executor(),
                engine->send(acc_id, payload_view),
                asio::use_future);
            if (fut.wait_for(std::chrono::milliseconds{200}) == std::future_status::ready) {
                (void)fut.get();
            }
            send_iters.fetch_add(1, std::memory_order_relaxed);
        }
    };
    std::thread ts1{sender_fn};
    std::thread ts2{sender_fn};
    std::thread ts3{sender_fn};

    // Allow senders to overlap with the Active state before triggering stop().
    // sleep_for instead of ioc.run_for()+restart(): restart() while workers are
    // in ioc.run() is asio UB that manifests as SEGFAULT under gcc-release.
    std::this_thread::sleep_for(std::chrono::milliseconds{50});

    // stop() drives the session-strand FSM to Disconnected while senders loop.
    // Under TSan pre-fix: DATA RACE on fsm_state_ → process abort.
    {
        auto stop_fut = asio::co_spawn(
            ioc.get_executor(), engine->stop(), asio::use_future);
        // wait_until_observed: no ioc.run_for()/restart() while workers are running.
        bool done = wait_until_observed(
            [&]{ return stop_fut.wait_for(0ms) == std::future_status::ready; },
            10000ms);

        sender_stop.store(true, std::memory_order_relaxed);
        ts1.join();
        ts2.join();
        ts3.join();
        wg.reset();   // release guard → workers exit when queue drains
        t1.join();
        t2.join();
        t3.join();

        ASSERT_TRUE(done) << "V-13: engine.stop() did not complete within 10s";
        stop_fut.get();
    }

    EXPECT_GT(send_iters.load(), 0)
        << "V-13: sender threads must have iterated at least once";
    EXPECT_TRUE(engine->stopped())
        << "V-13: engine must be stopped after stop() completes";
}

// ── V-14: StartStopCounterOrdering_NoUAF (gate-b/r1 #2) ─────────────────────
//
// Witnesses the TOCTOU UAF that existed when outstanding_counter_ was assigned
// AFTER the spawn loop in Engine::start().
//
// ── Historical RED mechanism (pre-fix) ───────────────────────────────────────
//   start() assigned outstanding_counter_ = counter at the END of the loop:
//     for (...) { ++(*counter); co_spawn(..., counter); }   // loop body
//     outstanding_counter_ = counter;                       // ← AFTER spawns
//   If stop() ran between the first co_spawn and this assignment:
//     stop() observed outstanding_counter_ == null → skipped the join →
//     registry_.clear() freed SessionEntry objects while the spawned loops
//     held SessionEntry& references → UAF.
//   Under ASan: heap-use-after-free; under TSan: data race on outstanding_counter_.
//
// ── Current GREEN mechanism (post-fix) ───────────────────────────────────────
//   outstanding_counter_ = counter is assigned BEFORE the loop.  stop() always
//   observes a valid counter.  Any spawned loop that incremented the counter
//   before stop() loads it is included in the join; if stop() drains before
//   all spawns increment, it waits correctly because the counter is valid.
//
// Mechanism:
//   1. Register 2 acceptor sessions (wider loop body).
//   2. Start engine on 3 threads.
//   3. Call start() immediately followed by stop() — the stop() may run while
//      start()'s loop is still co_spawning (the TOCTOU window).
//   4. Under ASan/TSan: pre-fix UAF/race; post-fix: clean.
//
// Thread count: 3 engine threads + main = 4 total.
// Anti-hang: 10s budget.
//
// [contracts C-0/INV-4a; gate-b/r1 #2; engine.cpp start()/stop()]

TEST(EngineSessionStrand, V14_StartStopCounterOrdering_NoUAF) {
    const char* fixture_dir = get_fixture_dir();
    if (!fixture_dir || fixture_dir[0] == '\0')
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";

    asio::io_context ioc;
    auto fac = make_tls_factory(fixture_dir);
    if (!fac) GTEST_SKIP() << "TLS factory build failed (cert/key not available)";

    // Register 2 acceptors to widen the spawn loop (more spawns = wider TOCTOU).
    const uint16_t port1 = reserve_free_port(ioc);
    const uint16_t port2 = reserve_free_port(ioc);

    fixpp::core::EngineConfig ecfg;
    ecfg.executor = ioc.get_executor();
    ecfg.clock = make_mock_clock(ioc);

    auto engine = std::make_unique<fixpp::session::Engine>(ioc.get_executor(), std::move(ecfg));

    auto acc1_cfg = make_session_cfg(
        fac, "ACC1_V14", "INI1_V14",
        fixpp::session::session_role::acceptor, "INI1_V14",
        ioc.get_executor(), port1);
    if (!engine->register_session(std::move(acc1_cfg)).has_value()) {
        stop_engine_sync(ioc, *engine);
        FAIL() << "V-14: acceptor1 register_session failed";
    }

    auto acc2_cfg = make_session_cfg(
        fac, "ACC2_V14", "INI2_V14",
        fixpp::session::session_role::acceptor, "INI2_V14",
        ioc.get_executor(), port2);
    if (!engine->register_session(std::move(acc2_cfg)).has_value()) {
        stop_engine_sync(ioc, *engine);
        FAIL() << "V-14: acceptor2 register_session failed";
    }

    // Drive the ioc on 3 threads so spawned loops begin executing immediately —
    // this narrows the window between co_spawn and the old post-loop assignment.
    std::thread t1{[&ioc]{ ioc.run(); }};
    std::thread t2{[&ioc]{ ioc.run(); }};
    std::thread t3{[&ioc]{ ioc.run(); }};

    // start() spawns loops for all registered sessions.
    // Pre-fix: outstanding_counter_ assigned AFTER loop → concurrent stop() sees null.
    // Post-fix: assigned BEFORE loop → stop() always finds a valid counter.
    ASSERT_TRUE(engine->start().has_value()) << "engine.start() failed";

    // Immediately call stop() — exercises the start/stop concurrent window.
    {
        auto stop_fut = asio::co_spawn(
            ioc.get_executor(), engine->stop(), asio::use_future);
        bool done = wait_pred(ioc,
            [&]{ return stop_fut.wait_for(0ms) == std::future_status::ready; },
            10000ms);

        ioc.stop();
        t1.join();
        t2.join();
        t3.join();

        ASSERT_TRUE(done) << "V-14: engine.stop() did not complete within 10s";
        stop_fut.get();
    }

    EXPECT_TRUE(engine->stopped())
        << "V-14: engine must be stopped after stop() completes";
}

// ── V-15: SendCounterEnrolledBeforeControlHop_NoUAF (gate-b/r2 P1) ──────────
//
// Witnesses the UAF window that existed when send_counter_ was bumped INSIDE
// the control-strand lambda (Step B) rather than at send-entry (outer frame).
//
// ── Historical UAF mechanism (pre-fix) ───────────────────────────────────────
//   Engine::send() co_spawn'd a lambda onto control_strand_; the lambda did
//   the stopped_/registry checks and ONLY THEN bumped ++(*send_counter_).
//   A posted-but-not-yet-run lambda was invisible to stop()'s send_counter_
//   drain: stop() could observe 0, drain, clear registry_, return, the caller
//   could destroy the Engine, and the queued lambda would then dereference
//   freed `this` (reads stopped_, registry_) — UAF.
//
// ── Current GREEN mechanism (post-fix, gate-b/r2 P1) ─────────────────────────
//   send_counter_ is bumped in the outer coroutine frame before the co_spawn.
//   stop()'s drain therefore waits for any posted-but-not-yet-run send, keeping
//   Engine alive until the full two-hop completes.  The captured shared_ptr<sc>
//   keeps the counter object alive past Engine destruction so the guard's
//   decrement is always safe.
//
// Mechanism:
//   1. Establish a session pair on a ≥3-thread executor.
//   2. Spawn many sender coroutines concurrently (saturates the control strand
//      queue — maximizes the probability that some sends are posted but not run
//      when stop() drains).
//   3. Concurrently call Engine::stop() immediately after the sends are posted.
//   4. Assert that stop() completes without crash/TSan/ASan finding.
//   5. Destroy the Engine — any UAF on `this` would be caught here by ASan.
//   6. Assert that every send returned a result (not a hang): no deadlock.
//
// Thread count: 3 engine threads + main.
// Anti-hang: 12s stop budget; each send has a 200ms future-wait.
//
// [gate-b/r2 P1; spec.md FR-012/R7; engine.cpp send_counter_/counter_guard]

TEST(EngineSessionStrand, V15_SendCounterEnrolledBeforeControlHop_NoUAF) {
    const char* fixture_dir = get_fixture_dir();
    if (!fixture_dir || fixture_dir[0] == '\0')
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";

    asio::io_context ioc;
    const uint16_t port = reserve_free_port(ioc);
    auto fac = make_tls_factory(fixture_dir);
    if (!fac) GTEST_SKIP() << "TLS factory build failed (cert/key not available)";

    auto acc_cfg = make_session_cfg(
        fac, "ACCEPTOR_V15", "INITIATOR_V15",
        fixpp::session::session_role::acceptor, "INITIATOR_V15",
        ioc.get_executor(), port);
    auto ini_cfg = make_session_cfg(
        fac, "INITIATOR_V15", "ACCEPTOR_V15",
        fixpp::session::session_role::initiator, "ACCEPTOR_V15",
        ioc.get_executor(), port);
    // Capture ids before the configs are moved into register_session.
    const SessionId acc_id = SessionId::from_config(acc_cfg);
    const SessionId ini_id = SessionId::from_config(ini_cfg);

    fixpp::core::EngineConfig ecfg;
    ecfg.executor = ioc.get_executor();
    ecfg.clock = make_mock_clock(ioc);

    auto engine = std::make_unique<fixpp::session::Engine>(ioc.get_executor(), std::move(ecfg));

    if (!engine->register_session(std::move(acc_cfg)).has_value()) {
        stop_engine_sync(ioc, *engine);
        FAIL() << "V-15: acceptor register_session failed";
    }
    if (!engine->register_session(std::move(ini_cfg)).has_value()) {
        stop_engine_sync(ioc, *engine);
        FAIL() << "V-15: initiator register_session failed";
    }

    ASSERT_TRUE(engine->start().has_value()) << "engine.start() failed";

    // Wait for both sessions to reach Active so sends have a real session to target.
    bool both_active = wait_both_active(ioc, *engine, acc_id, ini_id, 8000ms);
    if (!both_active) {
        stop_engine_sync(ioc, *engine);
        FAIL() << "V-15: sessions did not reach Active within 8s";
    }

    // Start engine executor threads.
    // work_guard keeps workers alive so restart() is never needed from the main thread.
    auto wg = asio::make_work_guard(ioc);
    std::thread t1{[&ioc]{ ioc.run(); }};
    std::thread t2{[&ioc]{ ioc.run(); }};
    std::thread t3{[&ioc]{ ioc.run(); }};

    // Post N send coroutines concurrently onto the ioc.  Each is co_spawn'd
    // onto the ioc executor; they will queue up on the control_strand_.
    // The goal: maximize the number of sends that are POSTED (send_counter_
    // already bumped) but NOT YET RUN on the control strand when stop() fires.
    constexpr int kNumSends = 32;
    const std::array<std::byte, 4> dummy{};
    const std::span<const std::byte> payload_view{dummy};

    std::vector<std::future<expected_t<void>>> send_futs;
    send_futs.reserve(kNumSends);
    for (int i = 0; i < kNumSends; ++i) {
        send_futs.push_back(asio::co_spawn(
            ioc.get_executor(),
            engine->send(ini_id, payload_view),
            asio::use_future));
    }

    // Immediately call stop() — races the queued control-strand sends.
    // Pre-fix: stop() drains (sees counter 0 because bumps are still queued),
    //          clears registry, returns; caller destroys engine; queued lambdas
    //          run and dereference freed `this` → UAF.
    // Post-fix: sends already bumped counter before co_spawn → stop() waits
    //           for all of them to complete → Engine stays alive → no UAF.
    auto stop_fut = asio::co_spawn(
        ioc.get_executor(), engine->stop(), asio::use_future);
    // wait_until_observed: no restart() while t1/t2/t3 are in ioc.run().
    bool stop_done = wait_until_observed(
        [&]{ return stop_fut.wait_for(0ms) == std::future_status::ready; },
        12000ms);

    wg.reset();   // release guard → workers exit when queue drains
    t1.join();
    t2.join();
    t3.join();

    ASSERT_TRUE(stop_done) << "V-15: engine.stop() did not complete within 12s";
    stop_fut.get();

    // Collect all send results — each must have completed (not hung).
    // They may succeed or fail (session gone) — both are fine; what matters is
    // that Engine::~Engine() below does not trigger ASan/TSan on freed memory.
    int completed = 0;
    for (auto& f : send_futs) {
        if (f.wait_for(std::chrono::milliseconds{0}) == std::future_status::ready) {
            (void)f.get();
            ++completed;
        }
    }
    // All sends must have completed once stop() drained the counter.
    EXPECT_EQ(completed, kNumSends)
        << "V-15: all send futures must be ready after stop() drained send_counter_";

    EXPECT_TRUE(engine->stopped())
        << "V-15: engine must be stopped after stop() completes";

    // Destroy the engine — any UAF on stopped_/registry_ from a dangling lambda
    // would manifest here under ASan or as a crash.
    engine.reset();
}

// ── V-16: PostDrainLateSendFastFails_WithoutPosting (gate-b/r3 P1) ──────────
//
// Witnesses the residual UAF window that remained after V-15's fix:
// stop() could set stopped_=true, drain send_counter_ while it was zero, clear
// registry_, return, and then a fresh Engine::send() body starting afterward
// could enqueue a [this]-capturing control-strand lambda before noticing stop.
//
// Mechanism:
//   1. Establish an active session pair on a ≥3-thread executor.
//   2. Install a stop() seam that pauses on the control strand AFTER the
//      send_counter_ drain has observed zero and BEFORE registry_.clear().
//   3. Start stop() and wait until that post-drain seam is reached.
//   4. While stop() is paused, start a fresh Engine::send().
//   5. Assert the late send fast-fails with session_invalid_state_for_send.
//      The only correct path is the OUTER admission gate: enroll, recheck
//      stopped_, return WITHOUT posting the control-strand lambda.
//   6. Release stop(), let it clear/return, then destroy the Engine.
//      ASan/TSan must stay clean: no queued lambda may touch freed `this`.
//
// This forces the exact "send begins after stop()'s drain saw zero" path that
// V-15 did not cover. Thread count: 3 engine threads + main.

TEST(EngineSessionStrand, V16_PostDrainLateSendFastFails_WithoutPosting) {
    const char* fixture_dir = get_fixture_dir();
    if (!fixture_dir || fixture_dir[0] == '\0')
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";

    asio::io_context ioc;
    const uint16_t port = reserve_free_port(ioc);
    auto fac = make_tls_factory(fixture_dir);
    if (!fac) GTEST_SKIP() << "TLS factory build failed (cert/key not available)";

    auto acc_cfg = make_session_cfg(
        fac, "ACCEPTOR_V16", "INITIATOR_V16",
        fixpp::session::session_role::acceptor, "INITIATOR_V16",
        ioc.get_executor(), port);
    auto ini_cfg = make_session_cfg(
        fac, "INITIATOR_V16", "ACCEPTOR_V16",
        fixpp::session::session_role::initiator, "ACCEPTOR_V16",
        ioc.get_executor(), port);
    const SessionId acc_id = SessionId::from_config(acc_cfg);
    const SessionId ini_id = SessionId::from_config(ini_cfg);

    fixpp::core::EngineConfig ecfg;
    ecfg.executor = ioc.get_executor();
    ecfg.clock = make_mock_clock(ioc);

    auto engine = std::make_unique<fixpp::session::Engine>(ioc.get_executor(), std::move(ecfg));

    if (!engine->register_session(std::move(acc_cfg)).has_value()) {
        stop_engine_sync(ioc, *engine);
        FAIL() << "V-16: acceptor register_session failed";
    }
    if (!engine->register_session(std::move(ini_cfg)).has_value()) {
        stop_engine_sync(ioc, *engine);
        FAIL() << "V-16: initiator register_session failed";
    }

    std::atomic<bool> post_drain_reached{false};
    std::atomic<bool> post_drain_release{false};
    engine->set_post_send_drain_hook(
        [&post_drain_reached, &post_drain_release]() -> asio::awaitable<void> {
            post_drain_reached.store(true, std::memory_order_release);

            auto exec = co_await asio::this_coro::executor;
            asio::steady_timer t{exec};
            while (!post_drain_release.load(std::memory_order_acquire)) {
                t.expires_after(std::chrono::milliseconds{5});
                co_await t.async_wait(asio::use_awaitable);
            }
            co_return;
        });

    ASSERT_TRUE(engine->start().has_value()) << "engine.start() failed";

    bool both_active = wait_both_active(ioc, *engine, acc_id, ini_id, 8000ms);
    if (!both_active) {
        stop_engine_sync(ioc, *engine);
        FAIL() << "V-16: sessions did not reach Active within 8s";
    }

    // work_guard keeps workers alive so restart() is never needed from the main thread.
    auto wg = asio::make_work_guard(ioc);
    std::thread t1{[&ioc]{ ioc.run(); }};
    std::thread t2{[&ioc]{ ioc.run(); }};
    std::thread t3{[&ioc]{ ioc.run(); }};

    auto stop_fut = asio::co_spawn(
        ioc.get_executor(), engine->stop(), asio::use_future);

    // wait_until_observed: no restart() while t1/t2/t3 are in ioc.run().
    bool seam_hit = wait_until_observed(
        [&]{ return post_drain_reached.load(std::memory_order_acquire); },
        12000ms);
    ASSERT_TRUE(seam_hit) << "V-16: stop() did not reach the post-send-drain seam";

    const std::array<std::byte, 4> dummy{};
    auto late_send_fut = asio::co_spawn(
        ioc.get_executor(), engine->send(ini_id, std::span<const std::byte>{dummy}),
        asio::use_future);

    bool late_send_done = wait_until_observed(
        [&]{ return late_send_fut.wait_for(0ms) == std::future_status::ready; },
        5000ms);
    ASSERT_TRUE(late_send_done) << "V-16: late send did not complete within 5s";

    auto late_send_res = late_send_fut.get();
    ASSERT_FALSE(late_send_res.has_value())
        << "V-16: late send must fast-fail after stop() drained send_counter_";
    EXPECT_EQ(late_send_res.error(), fixpp::core::error::session_invalid_state_for_send)
        << "V-16: late send must fail at the admission gate, not by half-cleared registry";

    post_drain_release.store(true, std::memory_order_release);

    bool stop_done = wait_until_observed(
        [&]{ return stop_fut.wait_for(0ms) == std::future_status::ready; },
        12000ms);

    wg.reset();   // release guard → workers exit when queue drains
    t1.join();
    t2.join();
    t3.join();

    ASSERT_TRUE(stop_done) << "V-16: engine.stop() did not complete within 12s";
    stop_fut.get();
    EXPECT_TRUE(engine->stopped())
        << "V-16: engine must be stopped after stop() completes";

    engine.reset();
}

// ── V-17: OrphanEntryStopEmit_NoUAF (gate-b/r5; Codex 2nd-opinion) ────────────
//
// Regression guard for the ORPHAN-ENTRY stop() teardown path, which was
// previously untested. An orphan entry has a session_strand (emplaced in
// start()) but never published a session or live_transport — it arises when a
// role loop exits BEFORE publishing, e.g. an initiator whose connect exhausts
// (run_connect_loop: drive_reconnect() fails → close(terminal) + co_return
// without publish; cf. behaviors-and-limitations L2 down-peer initiator). This
// cell forces that: an initiator pointed at a free port with NO listener and a
// 1-attempt reconnect policy → connection refused → exhaust → orphan entry,
// then stop() on a 3-thread ioc. lookup() staying null asserts we are in the
// orphan path (not a live session).
//
// ── What the gate-b/r5 fix changed (inspection-found defect) ──────────────────
//   gate-b/r4 (a26a8e5) dispatched stop() Step 1's per-session cancellation emit
//   fire-and-forget — `asio::dispatch(*session_strand, [&entry]{ emit(); })`. For
//   an orphan entry the posted lambda is drained by NEITHER Step 2 (needs
//   live_transport) NOR Step 4 (needs session), and Step 3's counter-drain does
//   not order it, so there is NO happens-before guaranteeing it runs before
//   registry_.clear() (Step 5) frees entry.session_cancel → a latent dangling
//   `&entry` capture (Codex P1, codex_pr105_4_2ndopinion_review.md). gate-b/r5
//   makes the emit AWAITED (co_spawn + use_awaitable), like Steps 2/4, so it
//   always completes before clear() — orphan entries included.
//
//   NOTE — this defect is inspection-found, NOT deterministically reproducible
//   here: with idle worker threads the posted emit is serviced promptly, long
//   before clear(), so neither ASan (UAF) nor TSan (race) trips on the pre-fix
//   code in this scenario (verified 0/10 ASan, 0/6 TSan). That is precisely why
//   a26a8e5's local sanitizers passed. This cell therefore GUARDS the orphan
//   teardown path (it must tear down cleanly under ASan/TSan) rather than
//   reproducing the race; the fix removes the dangling capture categorically.
//
// Thread count: 3 engine threads + main = 4. Anti-hang: 10s stop budget; the
// orphan forms within ~300ms (loopback connection-refused is sub-ms).
// [engine.cpp stop() Step 1; gate-b/r5; Codex codex_pr105_4_2ndopinion_review.md]

TEST(EngineSessionStrand, V17_OrphanEntryStopEmit_NoUAF) {
    const char* fixture_dir = get_fixture_dir();
    if (!fixture_dir || fixture_dir[0] == '\0')
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";

    asio::io_context ioc;
    // A free port with NOTHING listening → initiator connect is refused.
    const uint16_t port = reserve_free_port(ioc);
    auto fac = make_tls_factory(fixture_dir);
    if (!fac) GTEST_SKIP() << "TLS factory build failed (cert/key not available)";

    // Initiator ONLY (no acceptor registered) → connect to `port` is refused.
    auto ini_cfg = make_session_cfg(
        fac, "INITIATOR", "ACCEPTOR", fixpp::session::session_role::initiator, "ACCEPTOR",
        ioc.get_executor(), port);
    // Bounded reconnect: a single 1ms attempt, then exhaust → run_connect_loop
    // unwinds without publishing → orphan entry (session_strand set, no
    // session / no live_transport).
    fixpp::transport::ReconnectPolicy rp;
    rp.schedule = std::pmr::vector<std::chrono::milliseconds>{std::chrono::milliseconds{1}};
    rp.jitter = 0.0;
    rp.max_attempts = 1;
    ini_cfg.reconnect_policy = std::move(rp);
    const SessionId ini_id = SessionId::from_config(ini_cfg);

    fixpp::core::EngineConfig ecfg;
    ecfg.executor = ioc.get_executor();
    ecfg.clock = make_mock_clock(ioc);
    auto engine = std::make_unique<fixpp::session::Engine>(ioc.get_executor(), std::move(ecfg));

    if (!engine->register_session(std::move(ini_cfg)).has_value()) {
        stop_engine_sync(ioc, *engine);
        FAIL() << "V-17: initiator register_session failed";
    }

    ASSERT_TRUE(engine->start().has_value()) << "engine.start() failed";

    // Worker threads drive the connect loop (which exhausts) + later stop().
    // work_guard keeps run() alive across the idle window (no run_for/restart UB).
    auto wg = asio::make_work_guard(ioc);
    std::thread t1{[&ioc]{ ioc.run(); }};
    std::thread t2{[&ioc]{ ioc.run(); }};
    std::thread t3{[&ioc]{ ioc.run(); }};

    // Let the connect loop exhaust (refused → 1ms → exhaust → co_return) so the
    // entry is an ORPHAN before stop() runs. Generous budget; sub-ms in practice.
    std::this_thread::sleep_for(std::chrono::milliseconds{300});

    // Sanity: the session never published (lookup stays null) — confirms we are
    // exercising the orphan path, not a live session (witness-quality guard).
    EXPECT_EQ(engine->lookup(ini_id), nullptr)
        << "V-17: orphan precondition — initiator must never have published a session";

    {
        auto stop_fut = asio::co_spawn(
            ioc.get_executor(), engine->stop(), asio::use_future);
        bool done = wait_until_observed(
            [&]{ return stop_fut.wait_for(0ms) == std::future_status::ready; },
            10000ms);

        wg.reset();
        t1.join();
        t2.join();
        t3.join();

        ASSERT_TRUE(done) << "V-17: engine.stop() did not complete within 10s";
        stop_fut.get();
    }

    EXPECT_TRUE(engine->stopped())
        << "V-17: engine must be stopped after stop() completes";

    engine.reset();
}

}  // namespace
