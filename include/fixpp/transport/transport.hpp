// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// fixpp::transport::Transport — abstract pluggable interface (5 pure-virtual)
// + Transport::Config + ConnectInfo. Re-emitted verbatim from [2h §4.1] +
// [2h §4.5] Config; the design doc is the binding upstream.
//
// CONTRACT FILE — published by /speckit-plan as Phase 1 output. /speckit-tasks
// will turn this into the implementing header at include/fixpp/transport/
// transport.hpp. NO behavioural change is permitted between contract and
// implementing header without re-running Gate A.

#pragma once

#include <asio/awaitable.hpp>
#include <asio/cancellation_type.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>  // defines core::expected_t<T>
#include <fixpp/transport/endpoint.hpp>
#include <memory_resource>
#include <span>

namespace fixpp::transport {

// Forward declaration — ConnectInfo is defined below in this header.
struct ConnectInfo;

// ─────────────────────────────────────────────────────────────────────────────
// Transport — encryption-agnostic byte-stream contract.
//
// EXACTLY 5 pure-virtual methods AT the [const §XIV.2] ≤5 cap; matches
// [arch §4.5]'s normative published list. Zero defaulted-virtual headroom on
// the base; the TLS-aware extension is the TlsTransport sub-interface
// (tls_transport.hpp, 1 additional pure-virtual using 1 of the sub-interface's
// 5-slot budget).
//
// All methods run on the session strand per [2d §7.6]. Cancellation flows
// through ASIO native cancellation slots per [const §XI.2]; the awaiter's
// cancellation_state is read with `co_await asio::this_coro::cancellation_state`
// per the [2d §6.5] / [2g §6.4] recipe pattern. cancellation_type::total maps
// to the matching transport_*_cancelled variant per [2h §6.6] — NO thrown
// exception escapes the public surface.
//
// In-flight exclusivity (API-level contract per [2h §4.1] RC#3 close): at
// most one in-flight async_read_some AND one in-flight async_write per
// Transport instance. Concurrent second call returns IMMEDIATELY with
// transport_read_in_progress / transport_write_in_progress per [2h §6.6].
// Strand serialisation is defence-in-depth, NOT binding. async_connect and
// async_handshake are one-shot per Transport lifetime; the ENTRY STATE decides the
// answer, not the call index — see async_connect/async_handshake below (#339).
// ─────────────────────────────────────────────────────────────────────────────
class Transport {
public:
    // Per-Transport-instance knobs. Held inside the impl for the Transport
    // lifetime. Defaults per [2h §4.5] Config + Clarifications 2026-05-27 Q5=A:
    //   tcp_nodelay = true  (Nagle OFF — aligns with QFJ + Fix8; diverges
    //                        from QFC's Nagle-ON anomaly)
    //   so_linger_enabled = false (matches Fix8 explicit + QFJ/QFC OS-default)
    struct Config {
        // ── Connect-time / handshake-time ───────────────────────────────────
        std::chrono::milliseconds connect_timeout{30'000};  // [2h §1.1] cap.
        std::chrono::milliseconds tls_handshake_timeout{
            30'000};  // [2h §1.1] cap; surfaces transport_handshake_timeout.
        std::chrono::milliseconds tls_close_timeout{1'000};  // [2h §4.1] close() bidi shutdown cap.

        // ── Read/Write windows ──────────────────────────────────────────────
        std::size_t max_read_window_bytes{256 *
                                          1024};  // matches [2b §1.2] default_max_frame_bytes.
        std::size_t max_write_size_bytes{1024 * 1024};  // one v1.0-max frame + 4× headroom.

        // ── TCP knobs ───────────────────────────────────────────────────────
        std::int32_t tcp_recv_buf_bytes{0};            // 0 = OS auto-tune.
        std::int32_t tcp_send_buf_bytes{0};            // 0 = OS auto-tune.
        bool tcp_nodelay{true};                        // Clarifications Q5=A — Nagle OFF.
        bool tcp_keepalive{false};                     // Heartbeat is primary keep-alive.
        std::int32_t tcp_keepalive_idle_seconds{120};  // consulted only when tcp_keepalive true.
        std::int32_t tcp_keepalive_interval_seconds{30};
        std::int32_t tcp_keepalive_count{3};
        bool so_reuseaddr{false};       // acceptor-side opt-in.
        bool so_linger_enabled{false};  // Clarifications Q5=A — no linger.
        std::int32_t so_linger_seconds{0};

        // ── PMR (per [arch §6] rule 4) ──────────────────────────────────────
        std::pmr::memory_resource* mr{
            nullptr};  // null → engine default; passed through factory make().
    };

    virtual ~Transport() = default;

    // (1) Establish the connection. For TCP transports, the connect() handshake
    //     (kernel SYN → SYN-ACK → ACK); for TlsTransport, async_handshake is a
    //     SEPARATE step the FSM issues after this completes successfully.
    //
    //     Cancellation: cancellation_type::total → transport_connect_cancelled.
    //     By STATE not call count (#339): closed → 98; connected/handshaken → 97; fresh → attempts.
    [[nodiscard]] virtual asio::awaitable<core::expected_t<ConnectInfo>> async_connect(
        Endpoint const& ep) = 0;

    // (2) Read up to buf.size() bytes from the peer into the CALLER-OWNED buffer
    //     (typically aliases 2b's framer-carry arena; transport NEVER allocates
    //     a read buffer). Awaitable completes with byte count (always > 0 on
    //     success per ASIO async_read_some); EOF → transport_read_eof.
    //
    //     Cancellation: cancellation_type::total → transport_read_cancelled;
    //     partial reads up to cancellation point are LOST per ASIO contract.
    //
    //     [[clang::lifetimebound]] on the parameter — caller MUST keep buffer
    //     alive past awaitable completion.
    [[nodiscard]] virtual asio::awaitable<core::expected_t<std::size_t>> async_read_some(
        std::span<std::byte> buf [[clang::lifetimebound]]) = 0;

    // (3) Composed write — ASIO equivalent of asio::async_write, NOT
    //     async_write_some. Awaitable completes with bytes.size() on success.
    //
    //     [2e §6.1.4] DURABLE-BEFORE-TRANSMIT INVARIANT (BINDING): caller MUST
    //     NOT call async_write until the corresponding outbound store(...) has
    //     linearised. A cancelled async_write MUST NOT roll back the persisted
    //     frame — the contract is "durable then transmit; cancel only cancels
    //     transmit". 2h guarantees by API construction (the FSM sequences
    //     Writer::commit → store → async_write; 2h is purely the wire-side hop).
    //     The [2h §9 seam #8] verifies via fault-injection.
    //
    //     Cancellation: cancellation_type::total → transport_write_cancelled.
    //     Short write → transport_write_short (torn write; FSM disconnects and
    //     recovers via [FIX-SL §4.5.2] ResendRequest). Persisted frame NOT
    //     rolled back per [2e §6.1.4].
    //
    //     [[clang::lifetimebound]] on the parameter — caller MUST keep bytes
    //     alive past awaitable completion (typically the caller pins the
    //     per-message arena that holds Writer::commit's post-commit span per
    //     [2e §6.1.4] / [2b §4.5]).
    [[nodiscard]] virtual asio::awaitable<core::expected_t<std::size_t>> async_write(
        std::span<const std::byte> bytes [[clang::lifetimebound]]) = 0;

    // (4) Cancel any in-flight async_connect / async_read_some / async_write /
    //     async_handshake. Synchronous; idempotent on already-cancelled /
    //     never-issued ops. Returns expected_t<void> for symmetry (only
    //     documented failure: transport_already_closed after close ⚠️ NOT IMPLEMENTED — #340).
    //
    //     ⚠️ CALL IT ON THE SESSION STRAND. This read "thread-safe (ASIO
    //     cancellation_signal is thread-safe)" until 2026-08-31 (#333). Both
    //     halves were wrong, in two different ways:
    //
    //       - MECHANISM MISNAMED. Every shipped impl cancels via
    //         socket_.cancel(ec) (asio_plain_transport.cpp /
    //         asio_tls_transport.cpp); none emits an asio::cancellation_signal
    //         from cancel(). The stated reason named a mechanism that no
    //         implementation of this method uses.
    //
    //       - GUARANTEE DOES NOT EXIST. asio's basic_stream_socket and
    //         basic_socket_acceptor @par Thread Safety blocks both say
    //         "Shared objects: Unsafe" and carve out only specific SYNCHRONOUS
    //         operations (send/receive/connect/shutdown; accept) — cancel is
    //         not among them. And asio/cancellation_signal.hpp carries no
    //         @par Thread Safety block at all, so the cited guarantee is one
    //         asio never makes.
    //
    //     NO off-strand caller is claimed to exist: this corrects the
    //     documented contract, it does not report a live race.
    //     Re-derive: read @par Thread Safety in asio/basic_stream_socket.hpp,
    //     and grep asio/cancellation_signal.hpp for "Thread Safety" (expect
    //     zero hits).
    //
    //     cancel() does NOT close the socket — the FSM may retry a cancelled
    //     connect/read/write. cancel() is the synchronous half of the
    //     [const §XI.2] cancellation contract; awaitable methods complete with
    //     their *_cancelled variants.
    [[nodiscard]] virtual core::expected_t<void> cancel() noexcept = 0;

    // (5) Close the transport. Synchronous on the session strand. After close()
    //     returns, every async_* returns transport_already_closed.
    //     For TLS transports: initiates best-effort bidi TLS shutdown
    //     (SSL_shutdown close-notify) bounded by Config::tls_close_timeout
    //     (1 s default); a truncated close (peer-side missing close-notify)
    //     surfaces as transport_read_truncated and is NOT treated as a hard
    //     error (logged at `warn` level by 2k per [2g §7.8]).
    //
    //     Idempotency: second close() returns expected_t<void>{} without side
    //     effects.
    [[nodiscard]] virtual core::expected_t<void> close() noexcept = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// ConnectInfo — owning-by-value POD describing the negotiated socket endpoints.
// Returned by async_connect. No view fields; no cross-doc lifetime contract.
// The session FSM captures by value across reconnect cycles.
// ─────────────────────────────────────────────────────────────────────────────
struct ConnectInfo {
    Endpoint remote;  // negotiated peer endpoint (post-resolution).
    Endpoint local;   // local-side bind endpoint.
    int family{0};    // AF_INET or AF_INET6.
};

}  // namespace fixpp::transport
