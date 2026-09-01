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

#include <array>
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
        // ⚠️ Classify, do not credit any nonzero error_code. sync_tcp_connect's
        // own deadline (asio::error::timed_out on its `!done` branch) is an
        // INSTRUMENT failure — a missed handler deadline on THIS connect — not
        // a contract outcome, and crediting it here would let a hung/slow
        // instrument masquerade as a passing "refused" branch. Only the two
        // codes FR-025 action (a) actually names admit true.
        return cr.ec == asio::error::connection_refused || cr.ec == asio::error::connection_reset;
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

// Per-probe classification of one TCP connect to a listener that never
// accepts, used by cell 10 to observe the OS accept queue's depth from the
// client side (the only side a unit test has). A scalar completed-count
// cannot tell "the queue is saturated and this probe is parked" apart from
// "this probe was refused/reset" — both look like "did not increment" — so
// every probe's outcome is classified individually, as observed at the end of
// that probe's bounded pump window. `pending` means only "not yet decided
// within this window" and is NOT a terminal outcome — a probe classified
// pending may be refused after the window closes, on a host whose
// error-report latency exceeds it. Measured latencies and the re-derivation
// recipe: `.specify/decisions/332-backlog-rst-witness-witnesses.md` §3a.
enum class connect_outcome { completed, refused_or_reset, other_error, pending };

struct connect_probe_result {
    int completed = 0;
    int refused_or_reset = 0;
    int other_error = 0;
    int pending = 0;
};

// Fires `probes` connects at `port`, spacing each initiation by one bounded
// pump window, and returns each one's snapshot classification. Earlier probes
// that did not complete remain outstanding while later ones are issued.
//
// ⚠️ THE PACING IS LOAD-BEARING FOR EVERY ARM, not only a saturating one —
// one bounded run per connect, never one run after issuing them all. Fired
// concurrently, a client can complete on the peer's SYN-ACK before the
// accept-queue accounting that would reject it has caught up, so the count
// becomes scheduling-dependent and a saturated listener can report full
// completion. Measured evidence and re-derivation recipe:
// `.specify/decisions/332-backlog-rst-witness-witnesses.md` §4/§8a.
// Pumping a bounded window after each connect, before issuing the next,
// removes that race,
// regardless of whether the target backlog is expected to be saturated.
// `per_connect` is per-CALLER, not a single shared constant: the control
// arm decouples from the saturating arms' tight budget by passing a longer
// one (see kControlPerConnect at the call site) — a generous, not latency-
// coupled, per-probe wait — while keeping the same pacing discipline.
//
// ⚠️ A budget too short for the machine under-counts, which is the SAFE
// direction only because cell 10 asserts its control arm FIRST: an
// under-count reds that arm rather than quietly satisfying the bound the
// cell is testing.
//
// ⚠️ The per-probe outcome is SNAPSHOTTED before any socket is closed. Once
// a probe's `close()` runs, its still-pending handler resolves with
// `operation_aborted`, overwriting the very "pending" signal this function
// exists to observe — the capture-before-teardown shape of #332. So the
// counts below are computed first, and the close+drain that follows cannot
// change them.
connect_probe_result count_completed_connects(asio::io_context& ioc, std::uint16_t port, int probes,
                                               std::chrono::milliseconds per_connect) {
    // ⚠️ reserve() is LOAD-BEARING here, not an optimisation. Every socket below
    // has an outstanding async_connect whose handler holds that socket's address,
    // and asio leaves the behaviour undefined if a socket is moved while an async
    // operation is pending. Reserving exactly `probes` up front is the whole
    // reason no reallocation can move one. Push past `probes` and that guarantee
    // is silently gone.
    std::vector<asio::ip::tcp::socket> sockets;
    sockets.reserve(static_cast<std::size_t>(probes));
    std::vector<connect_outcome> outcome(static_cast<std::size_t>(probes), connect_outcome::pending);
    const asio::ip::tcp::endpoint ep{asio::ip::make_address("127.0.0.1"), port};

    for (int i = 0; i < probes; ++i) {
        sockets.emplace_back(ioc);
        sockets.back().async_connect(ep, [&outcome, i](asio::error_code ec) {
            if (!ec) {
                outcome[static_cast<std::size_t>(i)] = connect_outcome::completed;
            } else if (ec == asio::error::connection_refused || ec == asio::error::connection_reset) {
                outcome[static_cast<std::size_t>(i)] = connect_outcome::refused_or_reset;
            } else if (ec != asio::error::operation_aborted) {
                outcome[static_cast<std::size_t>(i)] = connect_outcome::other_error;
            }
            // operation_aborted cannot fire here: nothing closes a socket until
            // after the snapshot below.
        });
        ioc.run_for(per_connect);
        ioc.restart();
    }

    // SNAPSHOT before any teardown — see the function comment.
    connect_probe_result result;
    for (auto o : outcome) {
        switch (o) {
            case connect_outcome::completed: ++result.completed; break;
            case connect_outcome::refused_or_reset: ++result.refused_or_reset; break;
            case connect_outcome::other_error: ++result.other_error; break;
            case connect_outcome::pending: ++result.pending; break;
        }
    }

    // Every connect that never completed still holds `outcome` by reference
    // and its socket is about to leave scope. Close, then drain, before either
    // does — the stack-use-after-scope shape of #313/#316. This cannot disturb
    // `result`: it was already computed above.
    for (auto& s : sockets) {
        asio::error_code ignored;
        s.close(ignored);
    }
    ioc.run();
    ioc.restart();
    return result;
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
// Cell 6 — the requested Endpoint::backlog survives construction and reads
// back off bound_endpoint().
//
// ⚠️ SCOPE: an accessor round-trip, and nothing more. This cell stays GREEN
// when the constructor never passes the value to listen() at all — it observes
// the Config field, never the socket. Cell 10 is the one that observes the
// configured depth at the OS layer; do not read this cell as covering FR-024's
// tunability claim.
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
            client_logon_write =
                co_await client->async_write(std::span<const std::byte>{logon_request});
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
    ASSERT_TRUE(server_logon_read.has_value()) << "server must read the client Logon; error="
                                               << static_cast<int>(server_logon_read.error());
    EXPECT_EQ(as_view(server_rx), as_view(logon_request));

    ASSERT_TRUE(server_logon_write.has_value()) << "server must write the Logon reply; error="
                                                << static_cast<int>(server_logon_write.error());
    EXPECT_EQ(*server_logon_write, logon_response.size());
    ASSERT_TRUE(client_logon_read.has_value()) << "client must read the Logon reply; error="
                                               << static_cast<int>(client_logon_read.error());
    EXPECT_EQ(as_view(client_rx), as_view(logon_response));
}

// ════════════════════════════════════════════════════════════════════════════
// Cell 8 — Already-resumed Transport UNAFFECTED by listener cancel() (RC#A).
//
// Option-A contract action (3): listener.cancel() does NOT close or modify
// Transports it already returned — ownership has transferred.
//
// FR-025 clause (3) names the protected state as an already-resumed-but-
// NOT-YET-CONSUMED unique_ptr<Transport> — [2h §4.6]'s async_accept doc
// comment: "initially in the 'connected' state ... the FSM issues
// async_handshake (TLS) immediately." That is the pre-handshake state, so
// cancel() must be proven to land BEFORE the handshake, not after it (#332
// Gate B r1 F1). Three phases, in order:
//   1. Accept + raw TCP connect only — server_transport ends up in the
//      pre-handshake "connected" state FR-025(3) protects. No handshake yet.
//   2. Post listener.cancel() ONTO the listener's executor (T034 strand
//      constraint / #333 — cancel() calls acceptor_.close(), which asio names
//      explicitly as not thread safe) while server_transport is still in
//      that pre-handshake state.
//   3. Verify the server-side Transport still supports the full post-cancel
//      surface T034 names: async_handshake, async_write, async_read_some and
//      close — all issued AFTER cancel().
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

    // Phase 1 — accept + raw TCP connect only, no handshake. The accept only
    // terminates if the client connect succeeds (a failed/discarded connect
    // leaves the accept pending forever), so the phase is bounded with
    // run_for() — a liveness bound, not a #328 latency barrier — and the
    // connect result is captured and asserted so a failure is diagnosed by
    // cause rather than by the ASSERT_NE(server_transport, nullptr) symptom.
    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            auto ar = co_await fixture.listener().async_accept();
            if (!ar.has_value()) co_return;
            server_transport = std::move(*ar);
        },
        asio::detached);

    auto client = fixture.make_client(ioc.get_executor());
    auto* client_tls = dynamic_cast<TlsTransport*>(client.get());
    ASSERT_NE(client_tls, nullptr);

    expected_t<ConnectInfo> connect_result = std::unexpected{error::transport_connect_cancelled};
    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            connect_result = co_await client->async_connect(fixture.server_endpoint());
        },
        asio::detached);

    ioc.run_for(std::chrono::seconds{10});
    ioc.restart();

    ASSERT_TRUE(connect_result.has_value())
        << "client async_connect in phase 1 must SUCCEED, or the accept coroutine never "
           "terminates and the run_for() bound above is masking a hang; error="
        << static_cast<int>(connect_result.error());
    ASSERT_NE(server_transport, nullptr);
    auto* server_tls = dynamic_cast<TlsTransport*>(server_transport.get());
    ASSERT_NE(server_tls, nullptr);

    // Phase 2 — cancel the listener while server_transport is held in the
    // pre-handshake state. post_cancel() issues it on the listener's
    // executor and proves it ran — see its ⚠️ note.
    ASSERT_TRUE(post_cancel(ioc, fixture.listener()));

    // Phase 3 — the post-cancel surface: handshake, write, read, close —
    // each must SUCCEED, issued strictly after cancel().
    expected_t<handshake_result> server_hs = std::unexpected{error::transport_handshake_cancelled};
    expected_t<handshake_result> client_hs = std::unexpected{error::transport_handshake_cancelled};
    std::array<std::byte, kClientAfterCancel.size()> server_rx{};
    expected_t<std::size_t> write_result = std::unexpected{error::transport_write_in_progress};
    expected_t<void> read_result = std::unexpected{error::transport_read_eof};
    std::array<std::byte, kServerAfterCancel.size()> client_rx{};
    expected_t<void> client_read_result = std::unexpected{error::transport_read_eof};

    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            server_hs = co_await server_tls->async_handshake(fixture.ssl_cfg());
            if (!server_hs.has_value()) {
                co_return;
            }
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
            client_hs = co_await client_tls->async_handshake(fixture.ssl_cfg());
            if (!client_hs.has_value()) {
                co_return;
            }
            client_read_result = co_await read_exactly(*client, std::span<std::byte>{client_rx});
            if (!client_read_result.has_value()) {
                co_return;
            }
            (void)co_await client->async_write(as_bytes(kClientAfterCancel));
        },
        asio::detached);

    ioc.run_for(std::chrono::seconds{10});

    ASSERT_TRUE(server_hs.has_value())
        << "server async_handshake on an already-resumed (pre-handshake) Transport must "
           "SUCCEED after listener cancel(); error="
        << static_cast<int>(server_hs.error());
    ASSERT_TRUE(client_hs.has_value())
        << "client handshake must succeed after listener cancel(); error="
        << static_cast<int>(client_hs.error());

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

    ASSERT_TRUE(client_read_result.has_value())
        << "client read of the server's post-cancel write must SUCCEED; error="
        << static_cast<int>(client_read_result.error());
    EXPECT_EQ(as_view(client_rx), kServerAfterCancel);

    EXPECT_TRUE(server_transport->close().has_value())
        << "close() on an already-resumed Transport must succeed after listener cancel()";
}

// ════════════════════════════════════════════════════════════════════════════
// connection_refused_or_reset() classifier — must NOT credit a synthetic
// sync_tcp_connect() deadline miss (asio::error::timed_out) as a
// connection-refused verdict. #332 Gate B r1 F3: the pre-fix classifier
// returned `true` on ANY nonzero error_code, so a missed handler deadline on
// the SECOND connect in cell 9 would pass as "refused" without the socket
// ever having been examined.
//
// ⚠️ No FIXPP_TLS_FIXTURE_DIR guard — this cell opens no socket and needs no
// TLS fixture, so it runs on every leg. On this WSL2 host it is the ONLY arm
// that executes the changed branch: cell 9's own two connects both observe
// cr.ec == 0 here (verdict comes from the read branch instead), so without
// this cell the classifier fix would ship with zero coverage of the branch it
// changes.
// ════════════════════════════════════════════════════════════════════════════
TEST(ListenerAcceptor, ConnectionRefusedOrResetRejectsSyntheticTimeout) {
    asio::io_context ioc;
    ConnectResult cr{asio::ip::tcp::socket{ioc}, asio::error::timed_out};
    EXPECT_FALSE(connection_refused_or_reset(ioc, cr));
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

// ════════════════════════════════════════════════════════════════════════════
// Cell 10 — US3 AC3 / FR-024: the configured Endpoint::backlog bounds how many
// clients the OS will complete while the application never accepts.
//
// This is the cell that observes the socket. Cell 6 reads the Config field back
// off bound_endpoint() and is green even against a constructor that never calls
// listen() with it at all, so cell 6 cannot carry FR-024's tunability claim.
//
// ⚠️ WHAT THIS CELL ASSERTS IS NOT AC3's LITERAL MECHANISM WORDING, ON PURPOSE.
// fixpp makes no guarantee about how the OS declines the overflow client —
// reset, refused, or left pending, per OS and configuration
// (spec.md US3 scenario 3). So the mechanism is not assertable PORTABLY, but
// it IS observable locally: every probe's state at its snapshot is recorded (see
// count_completed_connects above) and a non-pending shortfall is named in the
// failure message, so a red on a given runner says which outcome that runner
// produced. The NORMATIVE half — fixpp does not over-promise availability
// under saturated accept rates — is exactly what the assertions below carry.
// The operator-visible consequence is recorded in B&L (`L-012-2`).
//
// ⚠️ Do not "tighten" this into an equality against the configured depth. The
// arms are a deliberately unsaturated control at backlog 64 plus saturating
// arms at 1 and 8, 12 probes each — so the cell's discrimination ceiling is its
// own probe count and no equality against a configured depth is observable
// here. The measured per-host completion counts live in the decision record,
// dated.
//
// ⚠️ No cancel() and no run() on the listener's io_context anywhere in this
// cell: listen() happens in the constructor, so there is nothing to pump.
// T034's strand constraint on Listener::cancel() (#333) is satisfied here for
// a structural reason, not merely because this cell issues no cancel():
// `listener_ioc` is never run by any thread in this cell, so nothing can
// execute on the listener's executor concurrently with construction or
// destruction; and `listener_ioc` is declared first, so the listeners (whose
// destructors DO call close()) are destroyed before their context — the
// #301/#307 ordering rule.
//
// Anchor: specs/012-2h-transport/spec.md US3 AC3; .specify/2h-transport.md §4.6
// FR-024. Witness record: .specify/decisions/332-backlog-rst-witness-witnesses.md.
// ════════════════════════════════════════════════════════════════════════════
TEST(ListenerAcceptor, BacklogBoundsConnectionsCompletedWithoutTheApplication) {
    constexpr int kProbes = 12;
    constexpr std::uint32_t kLowBacklog = 1;
    constexpr std::uint32_t kHighBacklog = 8;
    constexpr std::uint32_t kControlBacklog = 64;  // deliberately > kProbes
    // Wall clock of a SATURATING arm is ~= kPerConnect x (probes that stay
    // pending in that arm), because a pending connect keeps the io_context busy
    // and stops run_for() from returning early — that is the whole cost of a
    // saturating arm, so the budget is the only lever on it.
    constexpr auto kPerConnect = 100ms;
    // The control arm stays PACED (see count_completed_connects' header on why
    // pacing is not optional) but decouples from the saturating arms' tight
    // budget: every one of its 12 connects is expected to complete near-
    // instantly (backlog 64 ≫ probe count), so a generous per-connect wait
    // costs a healthy run nothing while removing the latency coupling F5
    // raised against a fixed 100 ms under host load.
    constexpr auto kControlPerConnect = 2s;

    // Never run: asio_listener performs bind()/listen() in its constructor, so
    // there is no listener-side work for this cell to pump.
    asio::io_context listener_ioc;
    asio_listener control{listener_ioc.get_executor(), make_listener_cfg(0, kControlBacklog)};
    asio_listener low{listener_ioc.get_executor(), make_listener_cfg(0, kLowBacklog)};
    asio_listener high{listener_ioc.get_executor(), make_listener_cfg(0, kHighBacklog)};

    asio::io_context client_ioc;
    const auto r_control = count_completed_connects(client_ioc, control.bound_endpoint().port,
                                                      kProbes, kControlPerConnect);
    const auto r_low =
        count_completed_connects(client_ioc, low.bound_endpoint().port, kProbes, kPerConnect);
    const auto r_high =
        count_completed_connects(client_ioc, high.bound_endpoint().port, kProbes, kPerConnect);

    // (i) NON-VACUITY, and it has to come first. A sweep broken in any of several
    //     ordinary ways — wrong port, a per-connect budget too short for a loaded
    //     machine, ephemeral-port exhaustion — reports a LOW count, which is
    //     precisely the shape (ii) is looking for. Against a backlog wider than
    //     the probe count every connect must complete, so this arm proves the
    //     instrument can report the maximum before any arm below reads a
    //     shortfall as meaningful. ASSERT, not EXPECT: the rest is noise if it
    //     fails.
    ASSERT_EQ(r_control.completed, kProbes)
        << "the sweep could not complete " << kProbes
        << " connects even against a listener whose backlog (" << kControlBacklog
        << ") exceeds that — the assertions below would be measuring the instrument, not the "
           "listener (pending="
        << r_control.pending << ", refused_or_reset=" << r_control.refused_or_reset
        << ", other_error=" << r_control.other_error << ")";

    // (ii) US3 AC3 proper: a listener that never accepts does NOT absorb every
    //      client that arrives. This is the half of AC3 that does not depend on
    //      HOW the OS declines the client — which is the part that varies.
    EXPECT_LT(r_low.completed, kProbes)
        << "a listener configured with backlog=" << kLowBacklog << " completed all " << kProbes
        << " connects without ever accepting one — the configured depth reached neither "
           "listen() nor the kernel, so fixpp is over-promising availability";
    // Mechanism (pending vs refused vs reset) is DIAGNOSTIC ONLY and is never
    // asserted — spec.md US3 scenario 3 permits all three, so asserting any one
    // of them portably would resurrect the pre-2026-09-01 mechanism claim by
    // proxy. The counts are carried in the failure messages so a red on a given
    // runner says which outcome that runner produced.
    EXPECT_GT(r_low.completed, 0)
        << "backlog=" << kLowBacklog << " completed no connects at all — the low arm's "
           "shortfall is a dead listener, not a saturated one (completed=" << r_low.completed
        << ", pending=" << r_low.pending << ", refused_or_reset=" << r_low.refused_or_reset
        << ", other_error=" << r_low.other_error << ")";
    EXPECT_EQ(r_low.other_error, 0)
        << "backlog=" << kLowBacklog << " produced " << r_low.other_error
        << " connect error(s) not classified as completed/refused-or-reset/pending";

    // (iii) ...and the bound TRACKS THE CONFIGURED VALUE rather than merely
    //      existing. This is the assertion that makes the cell a witness for
    //      Endpoint::backlog FORWARDING: a listen() that ignored the config and
    //      hardcoded some small depth still satisfies (ii), and dies here.
    EXPECT_GT(r_high.completed, r_low.completed)
        << "backlog=" << kHighBacklog << " admitted no more connections than backlog="
        << kLowBacklog << " (" << r_high.completed << " vs " << r_low.completed
        << ") — the listener is not forwarding Endpoint::backlog to listen()";
    EXPECT_EQ(r_high.other_error, 0)
        << "backlog=" << kHighBacklog << " produced " << r_high.other_error
        << " connect error(s) not classified as completed/refused-or-reset/pending";

    // ⚠️ NO ARM AT AC3's OWN NAMED DEPTH, and the reason is structural, not
    // an oversight. A two-point relational bracket — bounded above by the probe
    // count, below by the next arm's count — cannot discriminate a clamp that
    // lands strictly between those two bounds; that is a property of the
    // predicate's shape, not of any particular run. Tightening it toward an
    // absolute floor trades that gap for a worse one: the OS may clamp
    // `listen()` silently, so a runner with a smaller `somaxconn` would then
    // false-negative. An arm was attempted on this basis and dropped; the
    // attempt, its mutant, and the outcome are dated in
    // `.specify/decisions/332-backlog-rst-witness-witnesses.md`. T034 stays
    // open on this gap (#332).
}
