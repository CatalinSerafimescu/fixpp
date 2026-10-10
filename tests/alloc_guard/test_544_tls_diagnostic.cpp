// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/alloc_guard/test_544_tls_diagnostic.cpp
//
// fixpp#544 (B35; `.specify/544-hot-path-zero-alloc.md` §3, "The TLS diagnostic"; owner
// default R-5): W-A's run-thread shape on a TLS session. It is a DIAGNOSTIC, run by hand
// under the full interceptor before and after the change; it registers no ctest, so it
// gates nothing. Recipe (one command line, wrapped here without a continuation
// backslash, which GCC's -Wcomment rejects at the end of a `//` comment):
//   GTEST_FILTER='B35TlsDiagnostic.*' python3 tools/check_alloc.py --expect-violation
//       --binary <build>/bin/test_544_tls_diagnostic --mallocnesia <build>/lib/libmallocnesia.so
// and, for the stack of each interception, the same binary under gdb with the interceptor
// preloaded and a breakpoint on the interceptor's report line.
//
// The engine side is the run-thread rig's: one dedicated thread runs the engine's
// io_context, and the test thread only bumps atomics. The peer is a fixpp TLS client
// transport on its OWN io_context and thread, because a TLS peer cannot write with a raw
// `::send`. Its writes are issued by a long-lived coroutine on the peer thread, gated on an
// atomic the test thread advances, so the test thread allocates nothing in the window. The
// peer's own work is therefore inside the interceptor's process-wide count; an interception
// is attributed to the engine side or to the peer by its stack.

#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/fix_time.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/compid_authorization_policy.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/security_profile.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <future>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "session/plain_engine_rig.hpp"
#include "support/minimal_dictionary.hpp"
#include "support/run_thread_engine_rig.hpp"

namespace {

namespace rt = fixpp::test_support::run_thread_rig;
namespace pr = fixpp::test_support::plain_rig;

constexpr int kWarmFrames = 8;
constexpr int kMeasuredFrames = 20;

struct tls_peer {
    std::vector<std::string> frames;       // the Logon, then every Heartbeat
    std::atomic<std::size_t> released{0};  // frames the peer may write
    std::atomic<bool> quit{false};
    std::atomic<bool> connected{false};
    std::atomic<bool> failed{false};
    std::atomic<std::size_t> bytes_read{0};
    std::array<std::byte, 4096> read_buf{};
};

asio::awaitable<void> peer_reader(fixpp::transport::Transport& t, tls_peer& p) {
    for (;;) {
        auto r = co_await t.async_read_some(std::span<std::byte>{p.read_buf});
        if (!r) co_return;
        p.bytes_read.fetch_add(*r, std::memory_order_relaxed);
    }
}

asio::awaitable<void> peer_writer(fixpp::transport::Transport& t,
                                  fixpp::tls::SslCtxConfig const& ssl,
                                  fixpp::transport::Endpoint ep, tls_peer& p) {
    auto c = co_await t.async_connect(ep);
    auto* tls = dynamic_cast<fixpp::transport::TlsTransport*>(&t);
    if (!c || tls == nullptr) {
        p.failed.store(true);
        co_return;
    }
    auto hs = co_await tls->async_handshake(ssl);
    if (!hs) {
        p.failed.store(true);
        co_return;
    }
    asio::co_spawn(co_await asio::this_coro::executor, peer_reader(t, p), asio::detached);
    p.connected.store(true, std::memory_order_release);
    for (std::size_t i = 0; i < p.frames.size(); ++i) {
        // Spins on the peer thread, whose io_context is idle while it waits.
        while (p.released.load(std::memory_order_acquire) <= i) {
            if (p.quit.load(std::memory_order_acquire)) co_return;
            std::this_thread::yield();
        }
        auto const& f = p.frames[i];
        auto w = co_await t.async_write(std::as_bytes(std::span<const char>{f.data(), f.size()}));
        if (!w) {
            p.failed.store(true);
            co_return;
        }
    }
}

template <class Pred>
bool spin_until(Pred pred) {
    auto const limit = std::chrono::steady_clock::now() + rt::kWaitBudget;
    while (!pred()) {
        if (std::chrono::steady_clock::now() >= limit) return false;
        std::this_thread::yield();
    }
    return true;
}

TEST(B35TlsDiagnostic, WA_ActiveHeartbeatOverTls) {
#ifndef FIXPP_TLS_FIXTURE_DIR
    GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
#else
    std::string const dir = FIXPP_TLS_FIXTURE_DIR;
    asio::io_context ioc;
    asio::io_context peer_ioc;
    using namespace std::chrono;
    auto clock = std::make_shared<fixpp::core::mock_clock>(
        system_clock::time_point{} + seconds{1704067200}, fixpp::core::steady_time_point{},
        ioc.get_executor());
    auto app = std::make_shared<rt::App>(rt::hook::signal_from_admin);

    fixpp::tls::file_cert_source::Config cs_cfg;
    cs_cfg.leaf_path = dir + "/leaf_rsa2048.pem";
    cs_cfg.private_key_path = dir + "/leaf_rsa2048.key";
    cs_cfg.ca_bundle_path = dir + "/ca.pem";
    auto cs = fixpp::tls::file_cert_source::make_file_cert_source(cs_cfg,
                                                                  std::pmr::new_delete_resource());
    ASSERT_TRUE(cs.has_value()) << "cert source";
    fixpp::tls::SslCtxConfig ssl;
    ssl.profile = fixpp::tls::SecurityProfile::mtls_ca;
    ssl.cs = std::move(*cs);
    ssl.clock = nullptr;
    ssl.caps = fixpp::tls::CertSourceCaps{};
    auto fres = fixpp::transport::make_asio_tls_transport_factory(
        fixpp::transport::Transport::Config{}, ssl);
    ASSERT_TRUE(fres.has_value()) << "TLS factory";
    std::shared_ptr<fixpp::transport::TransportFactory> fac{std::move(*fres)};

    fixpp::session::SessionConfig c;
    c.sender_comp_id = "ACCEPTOR";
    c.target_comp_id = "INITIATOR";
    c.begin_string = "FIX.4.2";
    c.role = fixpp::session::session_role::acceptor;
    c.executor_override = ioc.get_executor();
    c.security_profile =
        fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::mtls_ca};
    // The fixture's leaf certificate is bound to the peer's CompID, as in
    // tests/session/engine_readpump_test.cpp.
    fixpp::session::CompIdAuthorizationPolicy authz;
    authz.add_binding("fixpp-leaf-rsa2048", "INITIATOR");
    c.compid_authorization_policy = authz;
    c.dictionary = fixpp::test_support::make_minimal_dictionary();
    c.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    c.transport_factory_override = fac;
    c.heartbeat_interval = seconds{30};
    c.logout_disconnect_timeout_ms = 500;
    c.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 0};
    c.transport_send = [](std::span<const std::byte>) {};
    auto const id = fixpp::session::SessionId::from_config(c);

    // Every frame is built before either thread starts.
    std::array<char, 32> st_buf{};
    auto const st_r = fixpp::core::utc_time_to_fix_string(
        clock->now(), fixpp::core::fix_time_precision::millis, std::span<char>{st_buf});
    ASSERT_TRUE(st_r.has_value());
    std::string const st{st_r->data(), st_r->size()};
    tls_peer peer;
    peer.frames.push_back(pr::message("FIX.4.2", "A", 1, "INITIATOR", "ACCEPTOR", st,
                                      "98=0\x01"
                                      "108=30\x01"));
    for (int i = 0; i < kWarmFrames + kMeasuredFrames + 1; ++i) {
        peer.frames.push_back(pr::message("FIX.4.2", "0", static_cast<std::uint32_t>(2 + i),
                                          "INITIATOR", "ACCEPTOR", st));
    }

    fixpp::core::EngineConfig ec;
    ec.executor = ioc.get_executor();
    ec.clock = clock;
    ec.application = app;
    auto engine = std::make_unique<fixpp::session::Engine>(ioc.get_executor(), std::move(ec));
    // The last setup step reached, reported if the session does not reach Active.
    char const* stage = "register/start";
    bool up = engine->register_session(std::move(c)).has_value() && engine->start().has_value();

    std::optional<asio::executor_work_guard<asio::io_context::executor_type>> work;
    std::optional<asio::executor_work_guard<asio::io_context::executor_type>> peer_work;
    std::thread runner;
    std::thread peer_runner;
    std::unique_ptr<fixpp::transport::Transport> client;
    // The run thread starts whatever start() returned, so that stop() below can complete.
    work.emplace(asio::make_work_guard(ioc));
    runner = std::thread{[&] { ioc.run(); }};
    if (up) {
        stage = "listener bind";
        up = spin_until([&] { return engine->acceptor_bound_endpoint(id).port != 0; });
    }
    if (up) {
        auto const port = engine->acceptor_bound_endpoint(id).port;
        stage = "peer connect and handshake";
        auto made = fac->make(peer_ioc.get_executor(), ssl, nullptr);
        up = made.has_value();
        if (up) {
            client = std::move(*made);
            asio::co_spawn(
                peer_ioc,
                peer_writer(*client, ssl, fixpp::transport::Endpoint{"127.0.0.1", port}, peer),
                asio::detached);
            peer_work.emplace(asio::make_work_guard(peer_ioc));
            peer_runner = std::thread{[&] { peer_ioc.run(); }};
            up = spin_until([&] { return peer.connected.load(std::memory_order_acquire); });
        }
    }
    if (up) {
        stage = "Logon to onLogon";
        peer.released.store(1, std::memory_order_release);  // the Logon
        up = spin_until([&] { return app->logons.load(std::memory_order_acquire) != 0U; });
    }

    auto release_and_wait = [&](std::size_t frame_index) {
        peer.released.store(frame_index + 1, std::memory_order_release);
        return spin_until(
            [&] { return app->completions.load(std::memory_order_acquire) >= frame_index; });
    };
    bool warmed = up;
    std::size_t next = 1;
    for (int i = 0; i < kWarmFrames && warmed; ++i, ++next) warmed = release_and_wait(next);
    bool window_done = false;
    if (warmed) {
        bool ok = true;
        if (alloc_guard_start) alloc_guard_start();
        for (int j = 0; j <= kMeasuredFrames && ok; ++j, ++next) ok = release_and_wait(next);
        if (alloc_guard_end) alloc_guard_end();
        window_done = ok;
    }

    // Teardown: the peer's writer, the engine, the client, then both threads.
    peer.quit.store(true, std::memory_order_release);
    auto fut = asio::co_spawn(ioc, engine->stop(), asio::use_future);
    bool const stopped = fut.wait_for(rt::kWaitBudget) == std::future_status::ready;
    if (stopped) fut.get();
    if (client) {
        std::promise<void> closed;
        auto closed_fut = closed.get_future();
        asio::post(peer_ioc, [&] {
            (void)client->close();
            closed.set_value();
        });
        if (peer_runner.joinable()) (void)closed_fut.wait_for(rt::kWaitBudget);
    }
    work.reset();
    peer_work.reset();
    ioc.stop();
    peer_ioc.stop();
    if (runner.joinable()) runner.join();
    if (peer_runner.joinable()) peer_runner.join();
    if (!stopped) (void)engine.release();

    ASSERT_TRUE(up) << "the TLS session did not reach Active: stuck at " << stage
                    << " (peer failed=" << peer.failed.load() << ", peer read "
                    << peer.bytes_read.load() << " bytes)";
    ASSERT_TRUE(warmed) << "a warm-up Heartbeat was not processed";
    EXPECT_TRUE(window_done) << "a Heartbeat written inside the window was not processed";
    EXPECT_TRUE(stopped) << "Engine::stop() did not complete";
    EXPECT_EQ(app->completions.load(),
              static_cast<std::uint64_t>(kWarmFrames + kMeasuredFrames + 1));
#endif
}

}  // namespace
