// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_application_engine_send.cpp
//
// 019-app-callbacks gate-b/r1 FIX-5 — real Engine::send tests.
//
// Replaces the 3 substituted tests from test_application_outbound.cpp (Tests 1
// and 8) and test_application_strand.cpp (Test 3 — keepalive) with real
// Engine::send public API tests.
//
// Tests:
//   1. SendFromForeignThreadCrossesWire (a): engine.send() initiated from a
//      std::thread crosses the wire after toApp fires. (FR-006 any-thread;
//      FIX-1 — exec_ first-hop; the foreign thread calls co_spawn from a
//      non-ioc thread, posting the coroutine to the engine's executor.)
//   2. ReentrantSendFromFromAppNoDeadlock (b): engine.send() called from inside a
//      toApp callback does not deadlock. (FR-006 re-entrant edge case)
//   3. SendDrainRaceNoUAF (c): engine.send() + stop() + immediate ~Engine()
//      does not cause UAF under ASan — the send_counter_ drain in stop() ensures
//      all in-flight sends complete before registry_.clear(). (FIX-2)
//
// All three tests use real loopback TLS; they GTEST_SKIP when
// FIXPP_TLS_FIXTURE_DIR is not set (same as engine_lifecycle_test.cpp).
//
// All tests carry internal self-deadlines via ioc.run_for(). No ioc.run().
// [[feedback_fail_placeholder_red_test]]
//
// Anchors: spec.md FR-006/012; data-model.md D6; gate-b/r1 FIX-1/FIX-2/FIX-5;
//          engine_lifecycle_test.cpp (loopback-TLS + port pattern).

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/compid_authorization_policy.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <future>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "support/minimal_dictionary.hpp"

using namespace std::chrono_literals;
using fixpp::core::error;
using fixpp::core::expected_t;
using fixpp::session::Application;
using fixpp::session::SessionId;
using fixpp::wire::MessageView;
using fixpp::wire::access_mode;

namespace {

// ── TLS fixture helpers ────────────────────────────────────────────────────────

const char* get_fixture_dir() {
    const char* env = std::getenv("FIXPP_TLS_FIXTURE_DIR");  // NOLINT(concurrency-mt-unsafe)
#ifdef FIXPP_TLS_FIXTURE_DIR
    static const char* kDir = FIXPP_TLS_FIXTURE_DIR;
#else
    static const char* kDir = nullptr;
#endif
    return (env && env[0] != '\0') ? env : kDir;
}

uint16_t reserve_free_port(asio::io_context& ioc) {
    asio::ip::tcp::acceptor a{ioc};
    asio::ip::tcp::endpoint ep{asio::ip::make_address("127.0.0.1"), 0};
    a.open(ep.protocol());
    a.bind(ep);
    uint16_t port = a.local_endpoint().port();
    a.close();
    return port;
}

std::shared_ptr<fixpp::transport::TransportFactory> make_tls_factory(const char* fixture_dir) {
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

fixpp::session::SessionConfig make_session_cfg(
    asio::io_context& ioc, std::shared_ptr<fixpp::transport::TransportFactory> fac,
    const char* sender, const char* target, fixpp::session::session_role role,
    const char* peer_compid, uint16_t port) {
    fixpp::session::SessionConfig c;
    c.sender_comp_id = sender;
    c.target_comp_id = target;
    c.begin_string = "FIX.4.2";
    c.role = role;
    c.executor_override = ioc.get_executor();
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

std::shared_ptr<fixpp::core::mock_clock> make_mock_clock(asio::io_context& ioc) {
    using namespace std::chrono;
    auto utc = system_clock::time_point{} + seconds{1704067200};
    auto stp = fixpp::core::steady_time_point{} + seconds{0};
    return std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
}

// Poll ioc for up to `budget` until `pred` returns true.
template <typename Pred>
bool wait_until(asio::io_context& ioc, Pred pred, std::chrono::milliseconds budget) {
    auto deadline = std::chrono::steady_clock::now() + budget;
    while (!pred()) {
        ioc.run_for(50ms);
        ioc.restart();
        if (std::chrono::steady_clock::now() > deadline) return false;
    }
    return true;
}

static std::vector<std::byte> make_app_payload() {
    // Include 35=D so the built frame is parseable and toApp can inspect MsgType.
    static const char kPayload[] = "35=D\x01" "11=ORD001\x01" "54=1\x01" "55=AAPL\x01";
    std::vector<std::byte> v;
    for (const char* p = kPayload; *p; ++p)
        v.push_back(static_cast<std::byte>(*p));
    return v;
}

// ── WitnessApplication ─────────────────────────────────────────────────────────

class WitnessApplication : public Application {
public:
    std::atomic<int> from_app_count{0};
    std::atomic<int> to_app_count{0};

    // Used by test 2 for re-entrant send: set by the test before the send.
    std::function<void()> reentrant_fn;

    expected_t<void> fromApp(const MessageView<access_mode::Index>& /*msg*/,
                             const SessionId& /*id*/) override {
        ++from_app_count;
        return {};
    }

    expected_t<void> toApp(const MessageView<access_mode::Index>& /*msg*/,
                           const SessionId& /*id*/) override {
        ++to_app_count;
        // Re-entrant send hook: called at most once (cleared after first call).
        if (reentrant_fn) {
            auto fn = std::move(reentrant_fn);
            fn();
        }
        return {};
    }
};

// ── Shared setup helper ────────────────────────────────────────────────────────
// Builds an engine with one acceptor + one initiator, starts it, and waits for
// Active. Returns true on success; on failure calls ADD_FAILURE and returns false.
bool setup_engine(asio::io_context& ioc, fixpp::session::Engine& engine,
                  std::shared_ptr<fixpp::transport::TransportFactory> fac, uint16_t port,
                  SessionId& out_acc_id, SessionId& out_ini_id) {
    auto acc = make_session_cfg(ioc, fac, "ACCEPTOR", "INITIATOR",
                                fixpp::session::session_role::acceptor, "INITIATOR", port);
    auto ini = make_session_cfg(ioc, fac, "INITIATOR", "ACCEPTOR",
                                fixpp::session::session_role::initiator, "ACCEPTOR", port);
    out_acc_id = fixpp::session::SessionId::from_config(acc);
    out_ini_id = fixpp::session::SessionId::from_config(ini);
    if (!engine.register_session(std::move(acc)).has_value()) {
        ADD_FAILURE() << "acceptor register failed";
        return false;
    }
    if (!engine.register_session(std::move(ini)).has_value()) {
        ADD_FAILURE() << "initiator register failed";
        return false;
    }
    engine.start();
    bool ok = wait_until(ioc,
        [&] {
            auto* a = engine.lookup(out_acc_id);
            auto* i = engine.lookup(out_ini_id);
            return a && i && a->state() == fixpp::session::fsm_state::Active &&
                   i->state() == fixpp::session::fsm_state::Active;
        },
        3000ms);
    if (!ok) ADD_FAILURE() << "sessions did not reach Active within 3s";
    return ok;
}

// ── Test 1: engine.send() initiated from a foreign thread crosses the wire ─────
//
// A std::thread (NOT the ioc thread) calls asio::co_spawn(ioc.get_executor(),
// engine.send(id, payload), use_future). The co_spawn call is thread-safe: it
// posts the coroutine to exec_. The coroutine body (FIX-1: exec_-hop → strand-
// hop → toApp → Session::send) then runs on the ioc thread.
//
// Asserts:
//  - send() returns success
//  - toApp fired once
//  - fromApp fired on the receiving session (the frame crossed the wire)

TEST(ApplicationEngineSend, SendFromForeignThreadCrossesWire) {
    const char* fd = get_fixture_dir();
    if (!fd || fd[0] == '\0') GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";

    asio::io_context ioc;
    auto app = std::make_shared<WitnessApplication>();
    auto fac = make_tls_factory(fd);
    ASSERT_NE(fac, nullptr) << "TLS factory failed";

    fixpp::core::EngineConfig ecfg;
    ecfg.executor = ioc.get_executor();
    ecfg.application = app;
    ecfg.clock = make_mock_clock(ioc);  // needed for 52= SendingTime stamp in send_impl
    fixpp::session::Engine engine{ioc.get_executor(), std::move(ecfg)};

    SessionId acc_id, ini_id;
    ASSERT_TRUE(setup_engine(ioc, engine, fac, reserve_free_port(ioc), acc_id, ini_id));

    // Foreign thread: call co_spawn (thread-safe post) and return immediately.
    // The coroutine runs on the ioc thread (not the foreign thread).
    auto payload = make_app_payload();
    std::future<expected_t<void>> send_fut;
    {
        // The span passed to engine.send() must remain valid until the coroutine's
        // first suspension point (LINE A: payload_copy construction). The coroutine
        // runs on ioc's thread after co_spawn posts it. Capture payload by ref so
        // the span refers to the test-body variable (not a thread-lambda copy that
        // would be destroyed before the coroutine runs). The thread joins before
        // ioc.run_for, so payload is alive when the coroutine starts.
        std::thread t([&ioc, &engine, &ini_id, &payload, &send_fut]() mutable {
            send_fut = asio::co_spawn(
                ioc.get_executor(),
                engine.send(ini_id, std::span<const std::byte>(payload)),
                asio::use_future);
        });
        t.join();
        // payload is still alive here (test-body local); the thread has joined.
        // ioc.run_for (called below) will start the coroutine, which copies
        // payload into payload_copy before any suspension. After that copy,
        // the span is no longer referenced.
    }

    // Drive ioc (on the main thread) until send completes.
    bool done = wait_until(ioc,
        [&] {
            return send_fut.valid() &&
                   send_fut.wait_for(std::chrono::milliseconds{0}) ==
                       std::future_status::ready;
        },
        2000ms);
    ASSERT_TRUE(done) << "engine.send() from foreign thread did not complete";
    auto result = send_fut.get();
    EXPECT_TRUE(result.has_value())
        << "engine.send() must succeed; error=" << static_cast<int>(result.error());

    EXPECT_GE(app->to_app_count.load(), 1) << "toApp must fire for the sent message";

    // Drive ioc until fromApp fires on the receiving (acceptor) session.
    wait_until(ioc, [&] { return app->from_app_count.load() >= 1; }, 1000ms);
    EXPECT_GE(app->from_app_count.load(), 1)
        << "fromApp must fire — the send must cross the wire";

    // Teardown.
    {
        auto sf = asio::co_spawn(ioc, engine.stop(), asio::use_future);
        ioc.run_for(3s);
        ioc.restart();
        sf.get();
    }
    EXPECT_TRUE(engine.stopped());
}

// ── Test 2: engine.send() from inside a toApp callback does not deadlock ───────
//
// When toApp fires for the first message, it issues a re-entrant engine.send()
// via asio::co_spawn (non-blocking post). The re-entrant send runs after the
// current toApp returns (posted behind the current dispatch — no recursion/deadlock).
//
// Asserts: both sends complete, toApp fires at least twice, no deadlock.

TEST(ApplicationEngineSend, ReentrantSendFromToAppNoDeadlock) {
    const char* fd = get_fixture_dir();
    if (!fd || fd[0] == '\0') GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";

    asio::io_context ioc;
    auto app = std::make_shared<WitnessApplication>();
    auto fac = make_tls_factory(fd);
    ASSERT_NE(fac, nullptr) << "TLS factory failed";

    fixpp::core::EngineConfig ecfg;
    ecfg.executor = ioc.get_executor();
    ecfg.application = app;
    ecfg.clock = make_mock_clock(ioc);
    fixpp::session::Engine engine{ioc.get_executor(), std::move(ecfg)};

    SessionId acc_id, ini_id;
    ASSERT_TRUE(setup_engine(ioc, engine, fac, reserve_free_port(ioc), acc_id, ini_id));

    // Wire re-entrant fn: when toApp fires for the first message, issue another
    // engine.send() from within the callback body. asio::co_spawn with detached
    // is non-blocking (posts to exec_); this is the canonical re-entrant pattern.
    std::atomic<bool> reentrant_done{false};
    auto payload = make_app_payload();
    auto exec = ioc.get_executor();
    app->reentrant_fn = [&engine, ini_id, payload, exec, &reentrant_done]() mutable {
        // Non-blocking re-entrant send: post to exec so the send coroutine runs
        // AFTER the current callback (toApp) returns and releases in_dispatch_.
        // asio::post guarantees non-blocking deferred execution — no deadlock even
        // when called from within a session strand callback.
        asio::post(exec, [&engine, ini_id, payload, exec, &reentrant_done]() mutable {
            asio::co_spawn(
                exec,
                engine.send(ini_id, std::span<const std::byte>(payload)),
                [&reentrant_done](std::exception_ptr ep, expected_t<void>) {
                    if (ep) return;
                    reentrant_done.store(true);
                });
        });
    };

    // Issue the first send (triggers re-entrant fn in toApp).
    {
        auto sf = asio::co_spawn(ioc,
            engine.send(ini_id, std::span<const std::byte>(payload)), asio::use_future);
        bool ok = wait_until(ioc,
            [&] {
                return sf.wait_for(std::chrono::milliseconds{0}) ==
                       std::future_status::ready;
            },
            2000ms);
        ASSERT_TRUE(ok) << "first engine.send() did not complete";
        EXPECT_TRUE(sf.get().has_value()) << "first engine.send() must succeed";
    }

    // Wait for re-entrant send to complete.
    bool reentrant_ok = wait_until(ioc, [&] { return reentrant_done.load(); }, 2000ms);
    EXPECT_TRUE(reentrant_ok)
        << "re-entrant engine.send() from inside toApp must complete without deadlock";

    EXPECT_GE(app->to_app_count.load(), 2)
        << "toApp must fire at least twice (first send + re-entrant send)";

    // Teardown.
    {
        auto sf = asio::co_spawn(ioc, engine.stop(), asio::use_future);
        ioc.run_for(3s);
        ioc.restart();
        sf.get();
    }
    EXPECT_TRUE(engine.stopped());
}

// ── Test 3: send_counter_ drain — stop() + ~Engine() is UAF-safe ──────────────
//
// Proves FIX-2: Engine::send coroutines are enrolled in send_counter_; stop()
// drains send_counter_ BEFORE registry_.clear(). So ~Engine() (which destroys
// EngineConfig) can only run after all sends complete, preventing a
// heap-use-after-free when a send dereferences Session::engine_ after the
// EngineConfig is freed.
//
// Method: spawn both an engine.send() and engine.stop() on the same executor
// concurrently. stop() sets stopped_=true (so new sends fail-fast), then drains
// send_counter_ (the in-flight send must finish first), THEN clears the registry.
// After stop() returns, destroy the Engine immediately. Under ASan, any send
// that outlived stop() and dereferences engine_ would be caught.

TEST(ApplicationEngineSend, SendDrainRaceNoUAF) {
    const char* fd = get_fixture_dir();
    if (!fd || fd[0] == '\0') GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";

    asio::io_context ioc;
    auto app = std::make_shared<WitnessApplication>();
    auto fac = make_tls_factory(fd);
    ASSERT_NE(fac, nullptr) << "TLS factory failed";

    auto clock = make_mock_clock(ioc);
    auto engine = std::make_unique<fixpp::session::Engine>(ioc.get_executor(),
        [&] {
            fixpp::core::EngineConfig e;
            e.executor = ioc.get_executor();
            e.application = app;
            e.clock = clock;
            return e;
        }());

    SessionId acc_id, ini_id;
    ASSERT_TRUE(setup_engine(ioc, *engine, fac, reserve_free_port(ioc), acc_id, ini_id));

    // Concurrently issue engine.send() + engine.stop(). The ioc interleaves them.
    // stop() must drain the in-flight send (send_counter_) before registry_.clear().
    auto payload = make_app_payload();
    auto send_fut = asio::co_spawn(ioc,
        engine->send(ini_id, std::span<const std::byte>(payload)), asio::use_future);
    auto stop_fut = asio::co_spawn(ioc, engine->stop(), asio::use_future);

    // Drive ioc until both complete.
    bool done = wait_until(ioc,
        [&] {
            return send_fut.wait_for(std::chrono::milliseconds{0}) ==
                       std::future_status::ready &&
                   stop_fut.wait_for(std::chrono::milliseconds{0}) ==
                       std::future_status::ready;
        },
        5000ms);
    ASSERT_TRUE(done) << "send + stop did not both complete within 5s";

    auto sr = send_fut.get();
    // send may succeed OR return session_invalid_state_for_send (stopped_ race).
    if (!sr.has_value()) {
        EXPECT_EQ(sr.error(), error::session_invalid_state_for_send)
            << "if send fails after stop, error must be session_invalid_state_for_send";
    }
    EXPECT_NO_THROW(stop_fut.get()) << "stop() must not throw";
    EXPECT_TRUE(engine->stopped());

    // Destroy the Engine immediately after stop() returns.
    // FIX-2: send_counter_ was drained by stop() before registry_.clear(), so
    // ~Engine() is safe. Under ASan, any lingering send dereferenceing engine_
    // after this line would trigger a heap-use-after-free.
    engine.reset();

    ioc.run_for(200ms);  // drain any remaining posted work
}

}  // namespace
