// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/transport/test_listener_acceptor.cpp — [2h §9 seam #14] (T034).
//
// Exercises `fixpp::transport::asio_listener` per US3:
//   - FR-023 / FR-024 — fresh Transport minted per accept
//   - FR-025 + Clarifications 2026-05-27 Q4=A Option-A cancel contract:
//       (1) close listening socket  → subsequent connects refused
//       (2) cancel in-flight async_accept → transport_accept_cancelled
//       (3) already-resumed unique_ptr<Transport> UNAFFECTED
//   - Endpoint::backlog honoured at OS level
//
// Cells 7-8 (gate-b/r2 RC#A) require a real TLS loopback fixture pair and
// are guarded by FIXPP_TLS_FIXTURE_DIR (skipped when empty).

#include <gtest/gtest.h>

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/post.hpp>
#include <asio/redirect_error.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <fixpp/core/error.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <future>
#include <optional>
#include <string>
#include <thread>

#include "transport/asio_listener.hpp"
#include "transport/loopback_tls_fixture.hpp"

namespace {

using namespace std::chrono_literals;
using fixpp::core::error;
using fixpp::transport::asio_listener;
using fixpp::transport::Endpoint;

class counting_cert_source final : public fixpp::tls::cert_source {
public:
    explicit counting_cert_source(std::shared_ptr<fixpp::tls::cert_source> inner)
        : inner_{std::move(inner)} {}
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<fixpp::tls::local_credentials>>
    load_credentials() override {
        ++calls_;
        co_return co_await inner_->load_credentials();
    }
    [[nodiscard]] fixpp::core::expected_t<std::span<const fixpp::tls::Certificate>>
    load_trust_anchors() [[clang::lifetimebound]] override {
        return inner_->load_trust_anchors();
    }
    [[nodiscard]] int calls() const noexcept { return calls_.load(); }

private:
    std::shared_ptr<fixpp::tls::cert_source> inner_;
    std::atomic<int> calls_{0};
};

// ── Stub SslCtxConfig for cells that don't exercise the mint path ───────────
// asio_listener's ctor + cancel + acceptor-side accept surface do NOT touch
// ssl_cfg. Only `async_accept` → make_accepted_asio_tls_transport touches it
// (and on a stub it returns transport_factory_failed; the cell verifies the
// LISTENER surface, not mint success).
fixpp::tls::SslCtxConfig stub_ssl_cfg() { return fixpp::tls::SslCtxConfig{}; }

asio_listener::Config make_listener_cfg(std::uint16_t port = 0, std::uint32_t backlog = 16) {
    asio_listener::Config cfg;
    cfg.bind_endpoint = Endpoint{"127.0.0.1", port, backlog};
    cfg.ssl_cfg = stub_ssl_cfg();
    return cfg;
}

// Open a raw TCP socket to (host, port) on `ioc`. Returns the connected
// socket on success; surfaces the error_code on failure. Synchronous —
// the test drives the io_context via run() / run_for().
struct ConnectResult {
    asio::ip::tcp::socket socket;
    asio::error_code ec;
};

ConnectResult sync_tcp_connect(asio::io_context& ioc, std::string const& host, std::uint16_t port,
                               std::chrono::milliseconds timeout = 10s) {
    asio::ip::tcp::socket sock{ioc};
    asio::error_code ec;
    asio::ip::tcp::endpoint ep{asio::ip::make_address(host), port};

    // Bounded with run_for(), not a steady_timer: a timer race can complete
    // both the timer expiry and the connect in the same reactor pass, and
    // cancel() cannot retract an already-dequeued handler — so a timer-based
    // bound can convert a successful connect into a timeout.
    bool done = false;
    sock.async_connect(ep, [&](asio::error_code cec) {
        ec = cec;
        done = true;
    });

    ioc.run_for(timeout);
    if (!done) {
        asio::error_code ignored;
        sock.close(ignored);
        ioc.run();  // let the aborted connect handler run
        ec = asio::error::timed_out;
    }
    ioc.restart();

    return ConnectResult{std::move(sock), ec};
}

}  // namespace

// ════════════════════════════════════════════════════════════════════════════
// Cell 1 — ctor binds at OS-picked port; bound_endpoint() exposes the port.
// ════════════════════════════════════════════════════════════════════════════
TEST(ListenerAcceptor, BindsAtOsPickedPort) {
    asio::io_context ioc;
    asio_listener listener{ioc.get_executor(), make_listener_cfg(0, 16)};

    const auto bound = listener.bound_endpoint();
    EXPECT_EQ(bound.host, "127.0.0.1");
    EXPECT_GT(bound.port, 0u) << "port=0 should resolve to an OS-picked port";
}

// ════════════════════════════════════════════════════════════════════════════
// Cell 2 — cancel() with no pending accept returns success and is idempotent.
// ════════════════════════════════════════════════════════════════════════════
TEST(ListenerAcceptor, CancelIsIdempotent) {
    asio::io_context ioc;
    asio_listener listener{ioc.get_executor(), make_listener_cfg()};

    auto first = listener.cancel();
    auto second = listener.cancel();

    EXPECT_TRUE(first.has_value()) << "first cancel() must succeed";
    EXPECT_TRUE(second.has_value()) << "second cancel() must be a no-op success";
}

// ════════════════════════════════════════════════════════════════════════════
// Cell 3 — in-flight async_accept + cancel() →
//          expected_t::unexpected{transport_accept_cancelled}.
//
// Spawns async_accept; before any client connects, calls listener.cancel().
// The acceptor's close() surfaces operation_aborted; the listener maps to
// transport_accept_cancelled per [2h §6.6]:1191. FR-025 Option-A action (2).
// ════════════════════════════════════════════════════════════════════════════
TEST(ListenerAcceptor, CancelCompletesInflightAcceptWithCancelled) {
    asio::io_context ioc;
    asio_listener listener{ioc.get_executor(), make_listener_cfg()};

    auto fut = asio::co_spawn(ioc.get_executor(), listener.async_accept(), asio::use_future);

    // Schedule the cancel after the first async_accept queues. Running the
    // io_context drains both the queued accept and the post that triggers
    // cancel — and the cancel-triggered operation_aborted on the accept.
    asio::post(ioc, [&] {
        auto rc = listener.cancel();
        EXPECT_TRUE(rc.has_value());
    });

    ioc.run();

    auto result = fut.get();
    ASSERT_FALSE(result.has_value()) << "in-flight async_accept must error after cancel()";
    EXPECT_EQ(result.error(), error::transport_accept_cancelled);
}

// ════════════════════════════════════════════════════════════════════════════
// Cell 4 — post-cancel async_accept surfaces transport_accept_cancelled
// without dispatching to the OS. FR-025 Option-A action (1) verified at the
// LISTENER level rather than at OS level, because some platforms (notably
// WSL2's Hyper-V loopback stack) do not reliably return ECONNREFUSED to a
// connect against a closed port. The spec wording "TCP RST or connection-
// refused per OS" is OS-policy commentary; the binding contract is that the
// listener no longer accepts new work — which is exactly what we assert.
// ════════════════════════════════════════════════════════════════════════════
TEST(ListenerAcceptor, AcceptAfterCancelReturnsCancelled) {
    asio::io_context ioc;
    asio_listener listener{ioc.get_executor(), make_listener_cfg()};

    ASSERT_TRUE(listener.cancel().has_value());

    // Submitting async_accept on a cancelled listener must error out
    // promptly with transport_accept_cancelled — the acceptor handle is
    // closed; the asio::ip::tcp::acceptor::async_accept surfaces
    // operation_aborted / bad_descriptor, which the listener maps.
    auto fut = asio::co_spawn(ioc.get_executor(), listener.async_accept(), asio::use_future);

    ioc.run();

    auto result = fut.get();
    ASSERT_FALSE(result.has_value()) << "post-cancel async_accept must NOT yield a Transport";
    // RC#H (P3-1): asio_listener::async_accept now pre-checks acceptor_.is_open()
    // before dispatching to the OS — closed handle returns transport_accept_cancelled
    // directly, avoiding the bad_descriptor → transport_factory_failed path.
    EXPECT_EQ(result.error(), error::transport_accept_cancelled)
        << "post-cancel async_accept must return transport_accept_cancelled; "
           "got variant: "
        << static_cast<int>(result.error());
}

// ════════════════════════════════════════════════════════════════════════════
// Cell 5 — async_accept reach the connected raw TCP socket. We don't exercise
// the TLS mint path here (it requires real SSL_CTX fixtures); we verify that
// the listener observes the connect.
//
// This cell covers:
//   (a) FR-024 "fresh Transport minted per accept" — listener.async_accept
//       returns (success or factory failure) AFTER a client connects.
// ════════════════════════════════════════════════════════════════════════════
TEST(ListenerAcceptor, AcceptObservesClientConnect) {
    asio::io_context listener_ioc;
    asio_listener listener{listener_ioc.get_executor(), make_listener_cfg()};

    const std::uint16_t port = listener.bound_endpoint().port;

    std::atomic<bool> accept_completed{false};
    auto fut = asio::co_spawn(
        listener_ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            auto result = co_await listener.async_accept();
            accept_completed.store(true, std::memory_order_release);
            // Mint fails under stub SslCtxConfig — that's expected. We're
            // verifying the accept SURFACE, not the mint result.
            (void)result;
            co_return;
        },
        asio::use_future);

    // Run the listener_ioc on a dedicated thread so the client can drive
    // its own ioc on the test thread.
    std::thread io_thread{[&] { listener_ioc.run(); }};

    // Connect a raw TCP client.
    asio::io_context client_ioc;
    auto cr = sync_tcp_connect(client_ioc, "127.0.0.1", port, 10s);
    EXPECT_FALSE(static_cast<bool>(cr.ec)) << "client connect failed: " << cr.ec.message();

    // Bound the wait so a hang cannot wedge CI. This is a LIVENESS bound, NOT a
    // latency expectation, and the distinction sets the budget: `wait_for` returns
    // the moment the future is ready, so a generous bound costs a healthy run
    // nothing and only delays the report of a genuine hang. A bound sized against
    // how fast this normally resumes instead fails whenever the runner is merely
    // slow (#328).
    //
    // Captured, not asserted. No fatal assertion runs before the join below —
    // a std::thread must not be joinable when one fires (it would call
    // std::terminate on unwind).
    const auto status = fut.wait_for(10s);

    // Tear down.
    (void)listener.cancel();
    listener_ioc.stop();
    if (io_thread.joinable()) {
        io_thread.join();
    }

    ASSERT_EQ(status, std::future_status::ready)
        << "async_accept did not resume after client connect";

    // Not implied by the wait above: async_accept reports every failure it models
    // through its expected<> channel, but the future also becomes ready if the
    // coroutine frame itself throws (an allocation failure in the mint path), and
    // that leaves this flag false.
    EXPECT_TRUE(accept_completed.load(std::memory_order_acquire))
        << "the accept coroutine completed without reaching its post-accept store";
}

// ════════════════════════════════════════════════════════════════════════════
// Cell 6 — Endpoint::backlog is forwarded to the OS listen() depth. We can't
// reliably overflow the backlog on modern Linux (the kernel may silently
// double the queue via /proc/sys/net/core/somaxconn), so this cell only
// verifies that the listener constructs successfully with a custom backlog
// AND that the bound endpoint preserves the requested backlog field.
//
// The FR-024 backlog tunability claim is therefore tested via the
// constructor-survives-config path; deep "65th client RST" coverage moves
// to a fuzz / stress cell post-MVP.
// ════════════════════════════════════════════════════════════════════════════
TEST(ListenerAcceptor, BacklogConfigAcceptedAtConstruction) {
    asio::io_context ioc;
    auto cfg = make_listener_cfg(0, 4);  // small backlog
    asio_listener small_listener{ioc.get_executor(), cfg};
    EXPECT_GT(small_listener.bound_endpoint().port, 0u);
    EXPECT_EQ(small_listener.bound_endpoint().backlog, 4u);

    auto cfg2 = make_listener_cfg(0, 128);
    asio_listener large_listener{ioc.get_executor(), cfg2};
    EXPECT_GT(large_listener.bound_endpoint().port, 0u);
    EXPECT_EQ(large_listener.bound_endpoint().backlog, 128u);
}

// ════════════════════════════════════════════════════════════════════════════
// Cell 7 — Full TLS handshake via LoopbackTlsFixture (RC#A gate-b/r2).
//
// async_accept returns a Transport in state=connected + role=server.
// Simultaneously the client connects (async_connect) and both sides drive
// async_handshake. Asserts:
//   (a) accept returns a non-null Transport (FR-024 fresh mint).
//   (b) server-side async_handshake returns a non-empty handshake_result.
//   (c) client-side async_handshake returns a non-empty handshake_result.
//
// Guards with FIXPP_TLS_FIXTURE_DIR — skipped when certs not present.
// Anchor: .specify/2h-transport.md §4.6 FR-023/FR-024.
// ════════════════════════════════════════════════════════════════════════════
TEST(ListenerAcceptor, FullTlsHandshake) {
    if (std::string(FIXPP_TLS_FIXTURE_DIR).empty()) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    using namespace fixpp::transport;
    using namespace fixpp::transport::test;
    using fixpp::core::error;
    using fixpp::core::expected_t;

    asio::io_context ioc;
    LoopbackTlsFixture fixture{FIXPP_TLS_FIXTURE_DIR, ioc.get_executor()};

    // Results collected by the coroutines.
    expected_t<std::unique_ptr<Transport>> accept_result =
        std::unexpected{error::transport_accept_cancelled};
    expected_t<handshake_result> server_hs_result =
        std::unexpected{error::transport_handshake_cancelled};
    expected_t<handshake_result> client_hs_result =
        std::unexpected{error::transport_handshake_cancelled};

    // Server coroutine: accept + handshake.
    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            accept_result = co_await fixture.listener().async_accept();
            if (!accept_result.has_value()) {
                co_return;
            }
            auto* tls = dynamic_cast<TlsTransport*>(accept_result->get());
            if (!tls) {
                co_return;
            }
            server_hs_result = co_await tls->async_handshake(fixture.ssl_cfg());
        },
        asio::detached);

    // Client coroutine: connect + handshake.
    auto client = fixture.make_client(ioc.get_executor());
    auto* client_tls = dynamic_cast<TlsTransport*>(client.get());
    ASSERT_NE(client_tls, nullptr);

    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            auto conn = co_await client->async_connect(fixture.server_endpoint());
            if (!conn.has_value()) {
                co_return;
            }
            client_hs_result = co_await client_tls->async_handshake(fixture.ssl_cfg());
        },
        asio::detached);

    ioc.run_for(std::chrono::seconds{10});

    ASSERT_TRUE(accept_result.has_value()) << "async_accept must return a Transport; error="
                                           << static_cast<int>(accept_result.error());
    EXPECT_TRUE(server_hs_result.has_value()) << "server async_handshake must succeed; error="
                                              << static_cast<int>(server_hs_result.error());
    EXPECT_TRUE(client_hs_result.has_value()) << "client async_handshake must succeed; error="
                                              << static_cast<int>(client_hs_result.error());
}

// ════════════════════════════════════════════════════════════════════════════
// Cell 8 — Already-resumed Transport UNAFFECTED by listener cancel() (RC#A).
//
// Option-A contract action (3): listener.cancel() does NOT close or modify
// Transports it already returned — ownership has transferred.
//
// Sequence:
//   1. Accept one client → TLS handshake → server Transport in handshaken state.
//   2. Call listener.cancel().
//   3. Verify the server-side Transport can still async_write (not closed).
//
// Anchor: .specify/2h-transport.md §4.6 FR-025 Option-A.
// ════════════════════════════════════════════════════════════════════════════
TEST(ListenerAcceptor, AlreadyResumedTransportUnaffectedByCancel) {
    if (std::string(FIXPP_TLS_FIXTURE_DIR).empty()) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    using namespace fixpp::transport;
    using namespace fixpp::transport::test;
    using fixpp::core::error;
    using fixpp::core::expected_t;

    asio::io_context ioc;
    LoopbackTlsFixture fixture{FIXPP_TLS_FIXTURE_DIR, ioc.get_executor()};

    std::unique_ptr<Transport> server_transport;
    expected_t<handshake_result> server_hs = std::unexpected{error::transport_handshake_cancelled};
    expected_t<handshake_result> client_hs = std::unexpected{error::transport_handshake_cancelled};

    // Server: accept + handshake, store transport.
    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            auto ar = co_await fixture.listener().async_accept();
            if (!ar.has_value()) co_return;
            server_transport = std::move(*ar);
            auto* tls = dynamic_cast<TlsTransport*>(server_transport.get());
            if (!tls) co_return;
            server_hs = co_await tls->async_handshake(fixture.ssl_cfg());
        },
        asio::detached);

    // Client: connect + handshake.
    auto client = fixture.make_client(ioc.get_executor());
    auto* client_tls = dynamic_cast<TlsTransport*>(client.get());
    ASSERT_NE(client_tls, nullptr);

    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            auto conn = co_await client->async_connect(fixture.server_endpoint());
            if (!conn.has_value()) co_return;
            client_hs = co_await client_tls->async_handshake(fixture.ssl_cfg());
        },
        asio::detached);

    ioc.run_for(std::chrono::seconds{10});
    ioc.restart();

    ASSERT_TRUE(server_hs.has_value())
        << "server handshake must succeed before testing cancel isolation";

    // Cancel the listener AFTER we already hold server_transport.
    fixture.cancel_listener();

    // The server Transport must still be usable (write a small payload).
    // We expect the write to succeed or at minimum not crash / assert closed.
    // The client peer is still alive (client Transport not closed yet).
    expected_t<std::size_t> write_result = std::unexpected{error::transport_write_in_progress};

    const std::array<std::byte, 4> payload{};
    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            write_result =
                co_await server_transport->async_write(std::span<const std::byte>{payload});
        },
        asio::detached);

    ioc.run_for(std::chrono::seconds{5});

    // The write must not return transport_factory_failed or any "closed" sentinel.
    // Acceptable outcomes: success (bytes written) or connection-related error
    // (peer may have reset) — but NOT transport_accept_cancelled (which would
    // indicate the listener incorrectly forwarded its cancel to the Transport).
    EXPECT_NE(write_result.error_or(error::transport_read_eof), error::transport_accept_cancelled)
        << "listener cancel() must NOT affect already-resumed Transport";
}

TEST(ListenerAcceptor, AcceptUsesCachedServerSslContextAcrossConnections) {
    if (std::string(FIXPP_TLS_FIXTURE_DIR).empty()) GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    namespace tls = fixpp::tls;
    using fixpp::core::expected_t;
    tls::file_cert_source::Config cs_cfg;
    cs_cfg.leaf_path = std::string(FIXPP_TLS_FIXTURE_DIR) + "/leaf_rsa2048.pem";
    cs_cfg.private_key_path = std::string(FIXPP_TLS_FIXTURE_DIR) + "/leaf_rsa2048.key";
    cs_cfg.ca_bundle_path = std::string(FIXPP_TLS_FIXTURE_DIR) + "/ca.pem";
    auto server_inner =
        tls::file_cert_source::make_file_cert_source(cs_cfg, std::pmr::new_delete_resource());
    auto client_cs =
        tls::file_cert_source::make_file_cert_source(cs_cfg, std::pmr::new_delete_resource());
    ASSERT_TRUE(server_inner.has_value() && client_cs.has_value());
    auto server_cs = std::make_shared<counting_cert_source>(std::move(*server_inner));
    tls::SslCtxConfig server_ssl_cfg;
    server_ssl_cfg.profile = tls::SecurityProfile::mtls_ca;
    server_ssl_cfg.cs = server_cs;
    tls::SslCtxConfig client_ssl_cfg;
    client_ssl_cfg.profile = tls::SecurityProfile::mtls_ca;
    client_ssl_cfg.cs = std::move(*client_cs);
    auto client_factory = fixpp::transport::make_asio_tls_transport_factory({}, client_ssl_cfg);
    ASSERT_TRUE(client_factory.has_value());
    asio::io_context ioc;
    asio_listener::Config cfg;
    cfg.bind_endpoint = Endpoint{"127.0.0.1", 0, 16};
    cfg.ssl_cfg = server_ssl_cfg;
    asio_listener listener{ioc.get_executor(), std::move(cfg)};
    ASSERT_EQ(server_cs->calls(), 0);
    for (int i = 0; i != 3; ++i) {
        expected_t<std::unique_ptr<fixpp::transport::Transport>> accepted =
            std::unexpected{error::transport_accept_cancelled};
        expected_t<fixpp::transport::handshake_result>
            server_hs = std::unexpected{error::transport_handshake_cancelled},
            client_hs = std::unexpected{error::transport_handshake_cancelled};
        asio::co_spawn(
            ioc.get_executor(),
            [&]() -> asio::awaitable<void> {
                accepted = co_await listener.async_accept();
                if (accepted)
                    server_hs =
                        co_await dynamic_cast<fixpp::transport::TlsTransport*>(accepted->get())
                            ->async_handshake(server_ssl_cfg);
            },
            asio::detached);
        auto client = (*client_factory)->make(ioc.get_executor(), client_ssl_cfg, nullptr);
        ASSERT_TRUE(client.has_value());
        auto* client_tls = dynamic_cast<fixpp::transport::TlsTransport*>(client->get());
        ASSERT_NE(client_tls, nullptr);
        asio::co_spawn(
            ioc.get_executor(),
            [&, client = std::move(*client), client_tls]() mutable -> asio::awaitable<void> {
                auto conn = co_await client->async_connect(
                    Endpoint{"127.0.0.1", listener.bound_endpoint().port, 0});
                if (conn) client_hs = co_await client_tls->async_handshake(client_ssl_cfg);
            },
            asio::detached);
        ioc.run_for(std::chrono::seconds{10});
        ioc.restart();
        ASSERT_TRUE(accepted.has_value());
        EXPECT_TRUE(server_hs.has_value());
        EXPECT_TRUE(client_hs.has_value());
        EXPECT_EQ(server_cs->calls(), 1);
    }
}
