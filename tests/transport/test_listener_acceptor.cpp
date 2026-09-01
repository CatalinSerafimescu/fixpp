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
#include <asio/buffer.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/post.hpp>
#include <asio/redirect_error.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <array>
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
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "support/reify_test_frame.hpp"
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

// ── Byte-payload helpers ────────────────────────────────────────────────────
std::span<const std::byte> as_bytes(std::string_view s) noexcept {
    return std::span<const std::byte>{reinterpret_cast<std::byte const*>(s.data()), s.size()};
}

std::string_view as_view(std::span<const std::byte> b) noexcept {
    return std::string_view{reinterpret_cast<char const*>(b.data()), b.size()};
}

// Read exactly buf.size() bytes. async_read_some is read_SOME and may return
// short; a single-shot read would make every payload assertion below depend on
// TLS record framing rather than on the round trip it claims to witness.
asio::awaitable<fixpp::core::expected_t<void>> read_exactly(fixpp::transport::Transport& t,
                                                            std::span<std::byte> buf) {
    std::size_t got = 0;
    while (got < buf.size()) {
        auto r = co_await t.async_read_some(buf.subspan(got));
        if (!r.has_value()) {
            co_return std::unexpected{r.error()};
        }
        got += *r;
    }
    co_return fixpp::core::expected_t<void>{};
}

// A byte-exact FIX 4.4 Logon (35=A), so the round trip carries a real Logon
// rather than a Logon-shaped blob.
//
// BodyLength (9=) and CheckSum (10=) come from the shared assembler in
// tests/support/reify_test_frame.hpp, NOT from literals. A hand-computed
// checksum is a RESULT recorded in source: nothing re-derives it, so it goes
// stale silently the moment anyone edits the field list. assemble_frame() is
// header-only with zero fixpp includes, and tests/ is already on this target's
// include path (tests/transport/CMakeLists.txt).
//
// SCOPE: this is a TRANSPORT-level round trip — the assertion is that a
// handshaken pair carries these bytes intact in both directions. It does NOT
// exercise FIX session semantics, and does not claim to.
constexpr char kSoh = '\x01';

std::vector<std::byte> make_logon_frame(std::string const& sender, std::string const& target) {
    std::string const body = std::string("35=A") + kSoh + "34=1" + kSoh + "49=" + sender + kSoh +
                             "56=" + target + kSoh + "52=20260901-00:00:00.000" + kSoh + "98=0" +
                             kSoh + "108=30" + kSoh;
    return fixpp::test_support::assemble_frame(std::string("8=FIX.4.4") + kSoh, body);
}

// Cell 8 post-cancel payloads — distinct in each direction so a round trip
// cannot be satisfied by an echo.
constexpr std::string_view kServerAfterCancel = "server-after-cancel";
constexpr std::string_view kClientAfterCancel = "client-after-cancel";

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

// Has this connection been refused or reset by the peer? FR-025 action (a)
// admits BOTH outcomes — "TCP RST or connection-refused per OS" — and the two
// surface at different syscalls: refusal at connect(), reset at the first read.
// Measured 2026-09-01 on this repo's WSL2 loopback stack: connect() to a
// just-closed acceptor SUCCEEDS and the connection is reset immediately after
// (recv → ECONNRESET), 20/20 trials; a stock Linux CI runner returns
// ECONNREFUSED from connect() instead. A cell asserting only one branch would
// be asserting an OS detail the contract deliberately leaves open.
//
// ⚠️ The read is a LIVENESS bound, NOT a latency expectation (#328). When the
// acceptor really is closed the RST is already queued, so the read completes in
// microseconds and the 10 s budget costs a healthy run nothing. The budget
// expires only when the property under test is VIOLATED — an acceptor left open
// parks the connection in the backlog with nothing ever to read. That four-order
// -of-magnitude headroom is what separates this from the 500 ms bound #328
// closed, where the budget sat at the same order as the normal latency.
// Issue cancel() ON the listener's executor (T034 strand constraint / #333) and
// prove it actually ran.
//
// ⚠️ LOAD-BEARING, and factored precisely so it cannot be half-copied. A posted
// cancel that never runs leaves every later assertion describing an UNCANCELLED
// listener, and the cell goes green having measured nothing — run_for()/restart()
// is exactly the shape where a handler is silently left unrun. Both call sites
// need that guard, and writing it out twice is how the second copy loses it.
// Returning an AssertionResult means one ASSERT_TRUE at the call site carries
// both failure modes, named distinctly.
::testing::AssertionResult post_cancel(asio::io_context& ioc, asio_listener& listener) {
    std::optional<fixpp::core::expected_t<void>> rc;
    asio::post(ioc, [&] { rc = listener.cancel(); });
    ioc.run_for(5s);
    ioc.restart();
    if (!rc.has_value()) {
        return ::testing::AssertionFailure()
               << "the posted cancel() never ran — every assertion after this would be vacuous";
    }
    if (!rc->has_value()) {
        return ::testing::AssertionFailure()
               << "listener.cancel() failed; error=" << static_cast<int>(rc->error());
    }
    return ::testing::AssertionSuccess();
}

bool connection_refused_or_reset(asio::io_context& ioc, ConnectResult& cr) {
    if (cr.ec) {
        return true;  // connection-refused branch
    }
    std::array<std::byte, 1> buf{};
    bool done = false;
    asio::error_code read_ec;
    cr.socket.async_read_some(asio::buffer(buf), [&](asio::error_code ec, std::size_t) {
        read_ec = ec;
        done = true;
    });
    ioc.run_for(10s);

    // ⚠️ CAPTURE THE VERDICT BEFORE THE DRAIN, and do not re-read done/read_ec
    // after it. The drain below closes the socket, which completes the pending
    // read with operation_aborted — setting both flags. Deciding afterwards
    // therefore reports "dead" for a connection that is very much alive, which
    // is the whole property this predicate exists to distinguish. Measured: the
    // flag-only-cancel mutant went GREEN in 10 003 ms when the verdict was read
    // after the drain.
    //
    // done && ec → RST (or EOF) branch. !done → the connection is alive and
    // parked in a backlog, i.e. the listening socket was never closed.
    const bool dead = done && static_cast<bool>(read_ec);

    if (!done) {
        // The read is still pending and its handler holds `done` / `read_ec` by
        // reference. Close and drain before they leave scope — same shape (and
        // same remedy) as sync_tcp_connect's timeout branch above; leaving it
        // outstanding is the stack-use-after-scope of #313/#316.
        asio::error_code ignored;
        cr.socket.close(ignored);
        ioc.run();
    }
    ioc.restart();
    return dead;
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
// without dispatching to the OS. This is the LISTENER-surface half of FR-025
// Option-A action (1): the listener no longer accepts new work.
//
// It is NOT the whole of action (a). This comment used to argue that the spec's
// "TCP RST or connection-refused per OS" was mere OS-policy commentary and that
// the listener-level assertion sufficed — #332 item 3 rejected that, on the
// ground that a cancel() which flagged the listener without ever closing the
// acceptor satisfies this cell. The OS-level half is CELL 9, which connects for
// real and admits both branches the contract names.
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
// Cell 5 — async_accept reaches the connected raw TCP socket. We don't
// exercise the TLS mint path here (it requires real SSL_CTX fixtures).
//
// ⚠️ SCOPE — what this cell proves, and what it does NOT.
//   PROVES: async_accept is not synchronously ready, and it does resume once a
//   client has connected.
//   DOES NOT PROVE CAUSALITY. A mutant that suspends briefly and then returns
//   unexpected{transport_factory_failed} without ever accepting passes every
//   assertion here — the initial poll() leaves the future pending, the raw
//   client still connects into the kernel backlog, and the future still becomes
//   ready. This cell structurally cannot see that mutant: its mint fails BY
//   DESIGN under the stub SslCtxConfig, so there is no Transport to inspect.
//   The causality witness is CELL 7 (FullTlsHandshake), which runs against the
//   real loopback fixture and does obtain a live Transport — the same mutant
//   reds there, at ASSERT_TRUE(accept_result.has_value()).
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

    // The accept must still be pending here: poll() drives the coroutine to its
    // first suspend, and nothing has connected yet. This defeats exactly one
    // mutant -- an async_accept that resumes IMMEDIATELY, which would otherwise
    // satisfy every assertion below because the listening socket's backlog
    // completes the connect anyway. What it establishes is "not synchronously
    // ready", which is strictly weaker than "resumed BECAUSE a client
    // connected"; see the scope note above the cell for what carries that.
    listener_ioc.poll();
    ASSERT_EQ(fut.wait_for(std::chrono::seconds{0}), std::future_status::timeout)
        << "async_accept resumed before any client connected";

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

    // Tear down. STOP AND JOIN BEFORE CANCELLING. cancel() must never be issued
    // while io_thread is still inside run(): asio's basic_socket_acceptor
    // @par Thread Safety block declares "Shared objects: Unsafe", carves out
    // only the SYNCHRONOUS accept, and names close -- which is exactly what
    // asio_listener::cancel() calls -- as not thread safe; cancel gets no
    // carve-out at all. After the join, the test thread is the sole accessor of
    // acceptor_. (#333 settlement; T034 strand constraint.)
    listener_ioc.stop();
    if (io_thread.joinable()) {
        io_thread.join();
    }
    (void)listener.cancel();

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
// async_handshake, then exchange a FIX Logon. Asserts:
//   (a) accept returns a non-null Transport (FR-024 fresh mint).
//   (b) server-side async_handshake returns a non-empty handshake_result.
//   (c) client-side async_handshake returns a non-empty handshake_result.
//   (d) T034 Logon round trip — client Logon arrives byte-exact at the server
//       and the server's reply arrives byte-exact at the client.
//
// ⚠️ THIS CELL CARRIES THE ACCEPT-CAUSALITY CLAIM for the whole file. Cell 5
// establishes only "not synchronously ready" (see its scope note). Here the
// accepted Transport is real and is driven end to end, so an async_accept that
// resumed without ever accepting cannot reach (a), let alone (d).
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

    // Logon round trip (d). Distinct in each direction (49=/56= swapped), so a
    // round trip cannot be satisfied by an echo.
    const std::vector<std::byte> logon_request = make_logon_frame("CLIENT", "SERVER");
    const std::vector<std::byte> logon_response = make_logon_frame("SERVER", "CLIENT");
    std::vector<std::byte> server_rx(logon_request.size());
    std::vector<std::byte> client_rx(logon_response.size());
    expected_t<void> server_logon_read = std::unexpected{error::transport_read_eof};
    expected_t<std::size_t> server_logon_write =
        std::unexpected{error::transport_write_in_progress};
    expected_t<std::size_t> client_logon_write =
        std::unexpected{error::transport_write_in_progress};
    expected_t<void> client_logon_read = std::unexpected{error::transport_read_eof};

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
            if (!server_hs_result.has_value()) {
                co_return;
            }
            // Logon round trip, server half: read the client's Logon, reply.
            server_logon_read =
                co_await read_exactly(**accept_result, std::span<std::byte>{server_rx});
            if (!server_logon_read.has_value()) {
                co_return;
            }
            server_logon_write =
                co_await (*accept_result)->async_write(std::span<const std::byte>{logon_response});
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
            if (!client_hs_result.has_value()) {
                co_return;
            }
            // Logon round trip, client half: send the Logon, read the reply.
            client_logon_write = co_await client->async_write(std::span<const std::byte>{logon_request});
            if (!client_logon_write.has_value()) {
                co_return;
            }
            client_logon_read = co_await read_exactly(*client, std::span<std::byte>{client_rx});
        },
        asio::detached);

    ioc.run_for(std::chrono::seconds{10});

    ASSERT_TRUE(accept_result.has_value()) << "async_accept must return a Transport; error="
                                           << static_cast<int>(accept_result.error());
    ASSERT_NE(accept_result->get(), nullptr) << "async_accept must mint a non-null Transport";
    ASSERT_TRUE(server_hs_result.has_value()) << "server async_handshake must succeed; error="
                                              << static_cast<int>(server_hs_result.error());
    ASSERT_TRUE(client_hs_result.has_value()) << "client async_handshake must succeed; error="
                                              << static_cast<int>(client_hs_result.error());

    // (d) Logon round trip. Assert each leg, so a failure names the direction
    // that broke rather than only the byte comparison at the end.
    ASSERT_TRUE(client_logon_write.has_value())
        << "client must write the Logon; error=" << static_cast<int>(client_logon_write.error());
    EXPECT_EQ(*client_logon_write, logon_request.size());
    ASSERT_TRUE(server_logon_read.has_value())
        << "server must read the client Logon; error="
        << static_cast<int>(server_logon_read.error());
    EXPECT_EQ(as_view(server_rx), as_view(logon_request));

    ASSERT_TRUE(server_logon_write.has_value())
        << "server must write the Logon reply; error="
        << static_cast<int>(server_logon_write.error());
    EXPECT_EQ(*server_logon_write, logon_response.size());
    ASSERT_TRUE(client_logon_read.has_value())
        << "client must read the Logon reply; error="
        << static_cast<int>(client_logon_read.error());
    EXPECT_EQ(as_view(client_rx), as_view(logon_response));
}

// ════════════════════════════════════════════════════════════════════════════
// Cell 8 — Already-resumed Transport UNAFFECTED by listener cancel() (RC#A).
//
// Option-A contract action (3): listener.cancel() does NOT close or modify
// Transports it already returned — ownership has transferred.
//
// Sequence:
//   1. Accept one client → TLS handshake → server Transport in handshaken state.
//   2. Post listener.cancel() ONTO the listener's executor (T034 strand
//      constraint / #333 — cancel() calls acceptor_.close(), which asio names
//      explicitly as not thread safe).
//   3. Verify the server-side Transport still supports the full post-cancel
//      surface T034 names: async_write, async_read_some and close.
//
// ⚠️ THE ASSERTIONS MUST DEMAND SUCCESS, not merely a non-cancelled error.
// This cell previously asserted EXPECT_NE(write_result.error_or(...),
// transport_accept_cancelled). A Transport closed by the cancel returns
// transport_already_closed — which is != transport_accept_cancelled — so the
// exact defect the cell exists to catch passed it. Only success witnesses
// "UNAFFECTED".
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
    ASSERT_TRUE(client_hs.has_value())
        << "client handshake must succeed before testing cancel isolation";
    ASSERT_NE(server_transport, nullptr);

    // Cancel the listener AFTER we already hold server_transport. post_cancel()
    // issues it on the listener's executor and proves it ran — see its ⚠️ note.
    ASSERT_TRUE(post_cancel(ioc, fixture.listener()));

    // Post-cancel surface: write, read, close — each must SUCCEED.
    std::array<std::byte, kClientAfterCancel.size()> server_rx{};
    expected_t<std::size_t> write_result = std::unexpected{error::transport_write_in_progress};
    expected_t<void> read_result = std::unexpected{error::transport_read_eof};

    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            write_result = co_await server_transport->async_write(as_bytes(kServerAfterCancel));
            if (!write_result.has_value()) {
                co_return;
            }
            read_result = co_await read_exactly(*server_transport, std::span<std::byte>{server_rx});
        },
        asio::detached);

    // ⚠️ LOAD-BEARING peer half. Without it the server's read has nothing to
    // observe: the cell would stall out the run_for() below and then fail on a
    // starved read rather than witnessing the post-cancel surface.
    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            std::array<std::byte, kServerAfterCancel.size()> client_rx{};
            auto rr = co_await read_exactly(*client, std::span<std::byte>{client_rx});
            if (!rr.has_value()) {
                co_return;
            }
            (void)co_await client->async_write(as_bytes(kClientAfterCancel));
        },
        asio::detached);

    ioc.run_for(std::chrono::seconds{5});

    ASSERT_TRUE(write_result.has_value())
        << "async_write on an already-resumed Transport must SUCCEED after listener cancel(); "
           "error="
        << static_cast<int>(write_result.error());
    EXPECT_EQ(*write_result, kServerAfterCancel.size());

    ASSERT_TRUE(read_result.has_value())
        << "async_read_some on an already-resumed Transport must SUCCEED after listener "
           "cancel(); error="
        << static_cast<int>(read_result.error());
    EXPECT_EQ(as_view(server_rx), kClientAfterCancel);

    EXPECT_TRUE(server_transport->close().has_value())
        << "close() on an already-resumed Transport must succeed after listener cancel()";
}

// ════════════════════════════════════════════════════════════════════════════
// Cell 9 — FR-025 Option-A action (a): cancel() CLOSES the listening socket,
// so a subsequent client connect is refused at the TCP layer.
//
// The pre-existing coverage for action (a) was cell 4's post-cancel
// async_accept returning transport_accept_cancelled. That witnesses the accept
// SURFACE, not the socket: a cancel() that flagged the listener without ever
// closing the acceptor satisfies it. This cell connects for real.
//
// ⚠️ POSITIVE CONTROL, IN THIS SAME RUN — REMOVING IT MAKES THE CELL VACUOUS.
// The first connect must SUCCEED before the cancel. A dead connection proves
// nothing on its own — a wrong port, an unbound port, or bad arithmetic is dead
// too. Only the before/after pair attributes the death to cancel().
//
// The failure assertion is deliberately LOOSE, and covers BOTH branches FR-025
// names; see connection_refused_or_reset() above for the measurement that
// showed a single-branch assertion is platform-dependent.
//
// Anchor: .specify/2h-transport.md §4.6 FR-025 Option-A action (a).
// ════════════════════════════════════════════════════════════════════════════
TEST(ListenerAcceptor, CancelClosesListeningSocketSoLaterConnectsAreRefused) {
    asio::io_context listener_ioc;
    asio_listener listener{listener_ioc.get_executor(), make_listener_cfg()};
    const std::uint16_t port = listener.bound_endpoint().port;

    asio::io_context client_ioc;
    auto before = sync_tcp_connect(client_ioc, "127.0.0.1", port, 10s);
    ASSERT_FALSE(static_cast<bool>(before.ec))
        << "positive control: the port must ACCEPT a connect before cancel(); "
        << before.ec.message();

    ASSERT_TRUE(post_cancel(listener_ioc, listener));

    auto after = sync_tcp_connect(client_ioc, "127.0.0.1", port, 10s);
    EXPECT_TRUE(connection_refused_or_reset(client_ioc, after))
        << "after cancel() the listening socket must be closed, so a fresh connect must be "
           "refused or reset — it connected and stayed alive instead";
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
