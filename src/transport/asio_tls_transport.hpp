// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// src/transport/asio_tls_transport.hpp — INTERNAL header (NOT installed).
//
// Declares `fixpp::transport::asio_tls_transport`: the v1.0 reference
// implementation of `TlsTransport` over ASIO tcp::socket + asio::ssl::stream
// (OpenSSL back-end). Also declares the free-function factory
// `make_asio_tls_transport(...)` consumed by non-construction-time callers
// (the 2i C-ABI / runtime reconnect mint path per [arch §5.3]).
//
// Design anchors:
//   [2h §4.5] — asio_tls_transport fields, state machine, invariants.
//   data-model E-9 — canonical field set (lines 270-290).
//   [arch §5.3] — engine-bootstrap carve-out for the throwing constructor.
//   [arch §5.6] — frozen-at-open; unique_ptr ownership by Session.
//
// CONTRACT NOTES (for T027 — the body author):
//
//   FR-007  IN-FLIGHT EXCLUSIVITY
//     At most one in-flight async_read_some AND one in-flight async_write
//     per instance. A concurrent second call returns IMMEDIATELY with
//     transport_read_in_progress / transport_write_in_progress.
//     async_connect / async_handshake answer by ENTRY STATE, not call count:
//     closed → 98; a state that forbids the call → 97; otherwise it ATTEMPTS (#339).
//     Implementation note: the in-flight flags are strand-confined booleans
//     (NOT atomics) — all Transport coroutines run on `exec_`'s strand per
//     [2d §4.8], so unlocked reads are safe. They live in *timer_epochs_ and
//     are managed by detail::inflight_flag_guard (#342 / #346).
//     `cancel()` is synchronous and does NOT touch the flags or state.
//     ⚠️ Its "only modifies `cancel_signal_`" claim was struck 2026-08-31
//     (#333) — no such member exists. See the Thread-safety model note below.
//
//   FR-026  PINSET CAPTURE CACHING CONTRACT
//     `cfg.pinset->snapshot()` is captured ONCE at `async_handshake` start
//     into `captured_pinset_` BEFORE the OpenSSL handshake protocol exchange
//     begins per [2g §6.5.1] BINDING CONTRACT. The SSL_VERIFY_PEER trampoline
//     retrieves this snapshot via SSL_get_ex_data — it MUST NOT call
//     cfg.pinset->find / contains / snapshot mid-verification.
//
//   FR-029a DEFAULT SOCKET KNOBS (Clarifications 2026-05-27 Q5=A)
//     tcp_nodelay = true (Nagle OFF).
//     so_linger_enabled = false (no linger).
//     Applied by async_connect after the kernel 3-way handshake completes.
//
//   [feedback_asio_post_resume_bounces_to_spawn_executor] — D-18 REMINDER
//     Do NOT use `co_await asio::post(other_exec, use_awaitable)` to hop
//     executors — only the completion handler runs there; the coroutine body
//     bounces back to the spawn executor. To pin a coroutine body to a
//     different executor, use nested `co_spawn(other_exec, fn, use_awaitable)`
//     and put the work inside `fn`. ALL Transport coroutines must be
//     co_spawn'd on `exec_` at call sites.

#pragma once

#include <asio/any_io_executor.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/ssl/context.hpp>
#include <asio/ssl/stream.hpp>
#include <fixpp/core/error.hpp>                 // core::expected_t<T>
#include <fixpp/tls/peer_identity.hpp>          // fixpp::tls::peer_identity
#include <fixpp/tls/pinset.hpp>                 // fixpp::tls::pin_snapshot
#include <fixpp/tls/security_profile.hpp>       // fixpp::tls::SslCtxConfig
#include <fixpp/transport/listener_events.hpp>  // 013 T039: ListenerEvents
#include <fixpp/transport/tls_transport.hpp>    // TlsTransport + handshake_result
#include <fixpp/transport/transport.hpp>        // Transport + ConnectInfo + Config
#include <functional>
#include <memory>
#include <memory_resource>
#include <optional>

// The shared timer-epoch counter block (D-4.1) — internal, src/ only.
#include "timer_epoch_state.hpp"

namespace fixpp::transport {

// ─────────────────────────────────────────────────────────────────────────────
// asio_tls_transport
//
// Concrete implementation of TlsTransport over ASIO + OpenSSL (SSL_CTX).
// Minted by asio_tls_transport_factory::make(...) per FR-026 reconnect mint
// pattern (fresh Transport per attempt; previous Transport destroyed first).
//
// Throwing constructor (engine-bootstrap carve-out per [arch §5.3]):
//   The constructor may throw if OpenSSL context initialisation fails
//   (e.g., SSL_CTX_new returns null). This is acceptable only at session-open
//   time inside the factory's trap_throw envelope.
//
// State machine (E-1 transitions held in state_):
//   fresh       → (async_connect success)      → connected
//   connected   → (async_handshake success)    → handshaken
//   connected   → (async_handshake cancel/err) → closed
//   handshaken  → (close())                    → closed
//   *           → (close())                    → closed
//   connected / handshaken + cancel mid-IO     → state unchanged (cancel ≠ close)
//
// Thread-safety model:
//   All async methods and the strand-confined in-flight flags
//   (timer_epochs_->read_in_flight / write_in_flight, #346) are confined to
//   the session strand provided at
//   construction. ⚠️ cancel() was documented off-strand-safe via a
//   `cancel_signal_` that does not exist; struck 2026-08-31 (#333).
// ─────────────────────────────────────────────────────────────────────────────
class asio_tls_transport final : public TlsTransport {
public:
    // ── State type ─────────────────────────────────────────────────────────
    enum class state_t : std::uint8_t {
        fresh,       // after construction; no connect attempted yet
        connected,   // TCP 3-way handshake complete; TLS not started
        handshaken,  // TLS handshake complete; read/write active
        closed       // close() called or fatal error; no further async ops
    };

    // ── TLS role (RC#A — P1-1 fix) ─────────────────────────────────────────
    // Distinguishes initiator (client) from acceptor (server) so async_handshake
    // passes the correct stream_base role to OpenSSL. The role is private to
    // the impl class; the public TlsTransport::async_handshake virtual does NOT
    // expose it ([const §XIV.2]: no extra pure-virtual added).
    enum class role_t : std::uint8_t { client, server };

    // ── Timer-epoch guard (research.md D-4.1, FR-014) ───────────────────────
    //
    // The counter block is the SHARED `fixpp::transport::timer_epoch_state`
    // from "transport/timer_epoch_state.hpp" — one type for both transports,
    // as data-model.md §4 and D-4.1 specify. This transport uses BOTH fields;
    // the why-two-counters argument (a sequencing property of the callers that
    // nothing in this class enforces) lives with the type.

    // ── Constructor (throwing — [arch §5.3] engine-bootstrap carve-out) ───
    //
    // Builds the asio::ssl::context from `ssl_cfg` fields (protocol version
    // floor/ceiling, cipher list, verify flags, etc.). May throw if OpenSSL
    // SSL_CTX_new fails or if cert/key material loading fails. The factory
    // wraps this in trap_throw and surfaces transport_factory_failed.
    //
    // MUST NOT be called outside of a trap_throw boundary at runtime.
    explicit asio_tls_transport(asio::any_io_executor exec, Transport::Config cfg,
                                fixpp::tls::SslCtxConfig ssl_cfg);

    // ── Factory-path constructor (FR-026 shared-context path) ──────────────
    //
    // Called by asio_tls_transport_factory::make() when the factory holds a
    // pre-built asio::ssl::context. The shared context was built ONCE at
    // factory construction (setup_ssl_ctx_() equivalent ran there) and is
    // referenced here without rebuilding. This ctor DOES NOT call setup_ssl_ctx_().
    //
    // ssl_cfg is forwarded for handshake-time fields (pinset, mr, profile,
    // caps) — the crypto configuration (ciphers, key material) is already
    // baked into shared_ctx.
    //
    // May throw on socket initialisation failure (ENOMEM from socket(2)).
    // Wrapped in trap_throw by the factory's make().
    struct from_factory_tag {};
    asio_tls_transport(from_factory_tag, asio::any_io_executor exec, Transport::Config cfg,
                       fixpp::tls::SslCtxConfig ssl_cfg,
                       std::shared_ptr<asio::ssl::context> shared_ctx);

    // ── Accept-adoption constructor (US3 — asio_listener mint path per FR-024) ─
    //
    // Adopts an already-connected TCP socket produced by
    // asio::ip::tcp::acceptor::async_accept. The OS-side 3-way handshake has
    // already completed; state_ starts in state_t::connected. async_connect is
    // therefore one-shot-already-spent — calling it returns
    // transport_already_connected. The FSM (or test) calls async_handshake
    // directly to drive the TLS handshake.
    //
    // accepted_socket MUST be bound to the same executor as `exec` (the
    // listener typically binds via `acceptor_.async_accept(exec)` overload, or
    // the test does the equivalent). Throws on OpenSSL SSL_CTX setup failure
    // per the [arch §5.3] engine-bootstrap carve-out.
    asio_tls_transport(asio::any_io_executor exec, Transport::Config cfg,
                       fixpp::tls::SslCtxConfig ssl_cfg, asio::ip::tcp::socket accepted_socket);

    // ── Accept-adoption factory-path constructor (FR-026 + US3 combined) ───
    //
    // Like the from_factory_tag ctor but also adopts an already-connected socket
    // (server-mode acceptor path). Sets state_ = connected + role_ = server.
    asio_tls_transport(from_factory_tag, asio::any_io_executor exec, Transport::Config cfg,
                       fixpp::tls::SslCtxConfig ssl_cfg,
                       std::shared_ptr<asio::ssl::context> shared_ctx,
                       asio::ip::tcp::socket accepted_socket);

    // Non-copyable; non-movable (ssl_stream_ holds a ref to socket_).
    asio_tls_transport(asio_tls_transport const&) = delete;
    asio_tls_transport& operator=(asio_tls_transport const&) = delete;
    asio_tls_transport(asio_tls_transport&&) = delete;
    asio_tls_transport& operator=(asio_tls_transport&&) = delete;

    // Not `= default`: the destructor BODY retires every armed timer epoch
    // (D-4.1 item 3) before members are destroyed, so a stranded timer
    // handler that observes the retirement is guaranteed `this` is still
    // alive. Nothing else changes — members are still destroyed in reverse
    // declaration order after the body runs. Body in the .cpp.
    ~asio_tls_transport() override;

    // ── Transport overrides (bodies in asio_tls_transport.cpp — T027) ─────

    [[nodiscard]] asio::awaitable<core::expected_t<ConnectInfo>> async_connect(
        Endpoint const& ep) override;

    [[nodiscard]] asio::awaitable<core::expected_t<std::size_t>> async_read_some(
        std::span<std::byte> buf [[clang::lifetimebound]]) override;

    [[nodiscard]] asio::awaitable<core::expected_t<std::size_t>> async_write(
        std::span<const std::byte> bytes [[clang::lifetimebound]]) override;

    [[nodiscard]] core::expected_t<void> cancel() noexcept override;

    [[nodiscard]] core::expected_t<void> close() noexcept override;

    // #348: the graceful counterpart. Drives SSL shutdown through asio's
    // ssl::stream so the close-notify alert is actually drained to the socket,
    // and first CANCELS AND JOINS any suspended SSL op so that the alert is not
    // skipped at the only call sites that have one. Quiesce + shutdown share a
    // single Config::tls_close_timeout budget, with an abortive fallback if the
    // op does not unwind inside it. See Transport::close_async().
    [[nodiscard]] asio::awaitable<core::expected_t<void>> close_async() override;

    // ── TlsTransport override ───────────────────────────────────────────────

    [[nodiscard]] asio::awaitable<core::expected_t<handshake_result>> async_handshake(
        fixpp::tls::SslCtxConfig const& cfg [[clang::lifetimebound]]) override;

    // ── 013 T039 — ListenerEvents association ──────────────────────────────
    //
    // Called by asio_listener after make_accepted returns an accepted transport.
    // The pointer is non-owning; ListenerEvents is owned by asio_listener::Config
    // whose lifetime spans the listener (and therefore outlives every transport
    // minted by it). Null by default (initiator side); set only on the acceptor.
    // [FR-028 / data-model §E-5]
    void set_listener_events(ListenerEvents* le) noexcept { listener_events_ = le; }

    // T011 (D5/E-5/INV-7/023): engine-internal accessor for the socket's executor.
    // Used by the INV-7 debug assert in engine.cpp to verify
    // `transport.socket_executor() == session_strand` at every construction site.
    // Returns the underlying TCP socket's associated executor (the executor the
    // socket was constructed with — in engine-managed sessions this MUST equal
    // the per-session strand after T010). Non-const because get_executor() is
    // not const on asio::ip::tcp::socket.
    [[nodiscard]] asio::any_io_executor socket_executor() noexcept {
        return socket_.get_executor();
    }

    // ── Timer-epoch accessor (D-4.1 mechanism 2; D-9 mechanism 2) ───────────
    //
    // Used by the T3-T5 witness cells to observe the retire-point-omitted
    // mutant (D-6.4). This is exactly what it buys — not broader guard
    // coverage; the sibling "guard omitted" mutant has no killer and is
    // discharged structurally (D-9). Const, internal-header-only: no
    // production branch, no FIXPP_TEST_HOOKS.
    [[nodiscard]] std::shared_ptr<const timer_epoch_state> timer_epochs() const noexcept {
        return timer_epochs_;
    }

    // ── close_async fault seam (#360; internal-header-only) ─────────────────
    //
    // Makes the statement after close_async()'s state transition throw ON
    // DEMAND, so the `catch (...)` recovery path has a witness again.
    //
    // WHY A SEAM AND NOT A CANCELLATION. #348's witness reached that handler by
    // emitting a cancellation during the quiesce; #358 replaced close_async()'s
    // entry reset with `disable_cancellation{}` to fix a measured hang, after
    // which nothing is recorded, nothing throws, and the route is gone. The
    // cell stayed GREEN while losing all of its power over the branch — the 2x2
    // is at the `catch` itself. Reverting the entry reset is NOT the way back:
    // it re-opens the #358 hang.
    //
    // SCOPE. The MEMBER is unconditional and the SETTER is FIXPP_TEST_HOOKS-gated
    // — the repo's standing split for exactly this shape (`Engine`'s
    // `test_hook_pre_publish_` / `set_pre_publish_hook`, and the dozen seams in
    // `file_store`). Gating the member would ODR-mismatch, because the library is
    // compiled without the macro and this header is included from both sides;
    // gating only a method declaration cannot. The result is that no production
    // build has any way to install the hook, and `grep FIXPP_TEST_HOOKS` finds
    // this seam alongside every other. src/ is not an installed include root
    // either, so SC-010/SC-017 hold twice over.
    //
    // ⚠️ WHAT THE WITNESS DOES AND DOES NOT COVER. The hook fires at ONE point:
    // the first of the two remaining real throw sources on the mainline
    // (deadline-timer construction), i.e. after the quiesce and before the
    // shutdown. It therefore re-establishes exactly the ENTRY STATE the lost
    // witness had — quiesce completed, socket still open, state_ already
    // `closed` — and says nothing about a throw from anywhere else in the try.
    // That is a like-for-like restoration, not broader coverage.
#ifdef FIXPP_TEST_HOOKS
    void set_close_fault_hook(std::function<void()> hook) { close_fault_hook_ = std::move(hook); }
#endif  // FIXPP_TEST_HOOKS

    // ── Test-access friend ──────────────────────────────────────────────────
    //
    // Grants state_ visibility to integration tests that need to verify the
    // exact in-flight exclusivity path (e.g., inject state = connected then
    // call async_handshake twice). Used by:
    //   tests/transport/test_inflight_exclusivity.cpp (DISABLED_ integration cells)
    //   tests/transport/test_tls_handshake_pinset_rotation.cpp (rotation cell)
    friend class asio_tls_transport_test_access;

private:
    // Common SSL_CTX setup shared by both ctors. Reads ssl_cfg_, populates
    // *ssl_ctx_ (protocol bounds, cipher suites, kx groups, sigalgs, verify
    // trampoline, cert/key chain, trust anchors). Throws on any OpenSSL
    // failure — callers (both ctors) run inside the [arch §5.3] / [2a §4.2]
    // trap_throw boundary.
    void setup_ssl_ctx_();

    // Apply FR-029 / FR-029a socket options (TCP_NODELAY, SO_LINGER,
    // recv/send buffer sizes, TCP keepalive) on socket_. Called by
    // async_connect() after the OS-side connect completes (initiator leg)
    // and by the accept-adoption ctor (acceptor leg). Best-effort —
    // individual setsockopt failures are silently dropped (matches the
    // initiator-side semantics; the gate is the post-condition assertion
    // in the T019 cells).
    void apply_socket_options_() noexcept;

private:
    // ── Configuration (frozen at construction) ──────────────────────────────
    Transport::Config cfg_;             // per-session knobs (E-7)
    fixpp::tls::SslCtxConfig ssl_cfg_;  // TLS profile; pinset lives here

    // ── ASIO execution context ──────────────────────────────────────────────
    asio::any_io_executor exec_;  // session executor ([2d §4.8])

    // ── Network objects ─────────────────────────────────────────────────────
    // socket_ MUST be declared before ssl_stream_ so it is destroyed last.
    asio::ip::tcp::socket socket_;  // underlying TCP socket

    // ssl_ctx_ holds the asio::ssl::context (wrapping OpenSSL SSL_CTX*).
    // In the per-ctor build path it is an owned unique_ptr converted to shared_ptr.
    // In the factory path it is a shared_ptr pointing to the factory's cached
    // context (FR-026). All paths satisfy: ssl_ctx_ must outlive ssl_stream_.
    std::shared_ptr<asio::ssl::context> ssl_ctx_;

    // ssl_stream_ is constructed lazily at async_handshake start. It wraps
    // socket_ by reference — therefore socket_ and ssl_ctx_ MUST outlive it.
    // Declared as optional so we defer construction until the TCP socket is
    // in a connected state (the ASIO ssl::stream constructor sets up internal
    // SSL state that requires a valid socket fd on some impls).
    std::optional<asio::ssl::stream<asio::ip::tcp::socket&>> ssl_stream_;

    // ── Handshake-time pinset snapshot (captured once per [2g §6.5.1]) ─────
    std::shared_ptr<const fixpp::tls::pin_snapshot> captured_pinset_;

    // ── Peer identity (populated by verify_peer trampoline via ssl_cfg_) ───
    fixpp::tls::peer_identity peer_id_;

    // ── State ───────────────────────────────────────────────────────────────
    state_t state_{state_t::fresh};

    // ── Role (initiator vs acceptor — set by ctor; used by async_handshake) ─
    role_t role_{role_t::client};

    // ── Timer-epoch guard (D-4.1) ────────────────────────────────────────────
    // Held by shared_ptr, not by value: a timer handler captures a copy of
    // this pointer (never `this`), so `*timer_epochs_` outlives the
    // transport unconditionally even when the owner destroys `this` with no
    // drain (D-4.0 — reconnect_fsm.cpp:250-252/284-286 and
    // engine.cpp:841-844 all destroy synchronously on the failure arm).
    // Safe as a default member initializer: every asio_tls_transport ctor is
    // already throwing ([arch §5.3] carve-out) and one already does a
    // make_shared (ssl_ctx_ above) inside the same trap_throw boundary.
    std::shared_ptr<timer_epoch_state> timer_epochs_{std::make_shared<timer_epoch_state>()};

    // #360 fault seam — ALWAYS compiled in (so this .cpp, which never sees
    // FIXPP_TEST_HOOKS, can always test it) and null in production, because only
    // a FIXPP_TEST_HOOKS build has a setter to install it with.
    // Not in *timer_epochs_: that block's admission rule is "must survive the
    // transport's destruction", and this is read only from a live frame.
    std::function<void()> close_fault_hook_{};

    // #346: read/write in-flight flags live in *timer_epochs_, managed by
    // detail::inflight_flag_guard — rationale in that header.

    // INVARIANT: every flag guarding an operation on ssl_stream_ MUST appear
    // here. close() uses this to decide whether sending close_notify would
    // mutate SSL state underneath a suspended operation. connect_in_flight_ is
    // deliberately ABSENT -- ssl_stream_ is not engaged during connect, so a
    // connect cannot conflict with SSL_shutdown. A future async op that touches
    // ssl_stream_ belongs in this predicate; that is the rule close() relies on
    // and could not state at its own call site.
    //
    // ⚠️ SECOND ADMISSIBLE GUARD, added with close_async (#348): a state_
    // TRANSITION taken BEFORE the suspension. close_async sets state_ = closed
    // and only then suspends, and every other async entry point rejects on
    // state_ != handshaken -- so no SSL op can start underneath it and it needs
    // no flag here. An operation that suspends on ssl_stream_ WITHOUT first
    // closing state_ must add a flag to this predicate.
    //
    // ⚠️ close_async READS this predicate ACROSS its own suspensions, which no
    // other caller does: its quiesce loop waits on a plain steady_timer while a
    // read or write is still in flight and re-reads the predicate each time.
    // That is sound only because the flags live in *timer_epochs_ and are
    // cleared by inflight_flag_guard on the guarded frame's exit -- a flag
    // cleared by a statement below a co_await would be cleared too late, or not
    // at all on a destroyed frame, and the loop would spin to its deadline.
    [[nodiscard]] bool ssl_op_suspended_() const noexcept {
        return timer_epochs_->read_in_flight || timer_epochs_->write_in_flight ||
               timer_epochs_->handshake_in_flight;
    }

    // ── 013 T039 — ListenerEvents sink (null on initiator side) ─────────────
    // Non-owning pointer to the listener's event ring. Non-null only when this
    // transport was minted via make_accepted (acceptor path). Used by
    // async_handshake to emit session_event_tls_validation_failed on verify_peer
    // rejection. [FR-026 / FR-028 / data-model §E-5]
    ListenerEvents* listener_events_{nullptr};

    // ── Cancellation ────────────────────────────────────────────────────────
    //
    // cancel() propagates via asio::ip::tcp::socket::cancel(ec), which
    // triggers operation_aborted on every in-flight read / write / connect /
    // handshake bound to socket_. ⚠️ "which is thread-safe" was struck
    // 2026-08-31 (#333): asio's basic_stream_socket @par Thread Safety block
    // says "Shared objects: Unsafe" and carves out only specific SYNCHRONOUS
// ⚠️ THE ENTRY RESET IS NOT UNIFORM — see the CANCELLATION
    // TIMING block in transport.hpp, which is the canonical statement.
    // This comment used to say every public coroutine opens with
    // reset_cancellation_state(enable_total_cancellation()) (D-17). That
    // is now false for FIVE of six methods on this class: close_async
    // installs disable_cancellation{}, and connect/handshake/read/write
    // install the TWO-argument form whose OUT filter maps any accepted
    // cancellation to `terminal` for the composed child op. Derive the
    // shape from what each method AWAITS; do not assume it from here. No per-transport cancellation_signal
    // is held — the socket itself is the cancel sink.
};

// ─────────────────────────────────────────────────────────────────────────────
// make_asio_tls_transport — noexcept factory (2i C-ABI / runtime reconnect mint)
//
// Wraps the throwing asio_tls_transport constructor in the [2a §4.2]
// trap_throw envelope and returns expected_t::unexpected{
// error::transport_factory_failed} on any OS / OpenSSL / PMR throw.
//
// Per the FR-028 reconnect contract: each async_connect retry operates on a
// FRESH Transport minted by this call; the previous Transport MUST be destroyed
// before this function is invoked. The factory (asio_tls_transport_factory) owns
// this policy; callers of this free function are responsible for the same
// destroy-before-mint ordering.
//
// Body lives in src/transport/transport_factory.cpp (T028).
[[nodiscard]] core::expected_t<std::unique_ptr<Transport>> make_asio_tls_transport(
    asio::any_io_executor exec, Transport::Config cfg, fixpp::tls::SslCtxConfig ssl_cfg,
    std::pmr::memory_resource* mr) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// make_accepted_asio_tls_transport — noexcept factory (US3 / FR-024 path).
//
// Adopts an already-connected TCP socket produced by
// asio::ip::tcp::acceptor::async_accept. Returns a Transport in
// state_t::connected; the FSM calls async_handshake to drive TLS handshake.
//
// Used exclusively by asio_listener::async_accept; no SessionConfig / 2i C-ABI
// path mints accept-side Transports.
//
// Body lives in src/transport/transport_factory.cpp alongside the initiator
// mint helper.
[[nodiscard]] core::expected_t<std::unique_ptr<Transport>> make_accepted_asio_tls_transport(
    asio::any_io_executor exec, Transport::Config cfg, fixpp::tls::SslCtxConfig ssl_cfg,
    asio::ip::tcp::socket accepted_socket, std::pmr::memory_resource* mr) noexcept;

}  // namespace fixpp::transport
