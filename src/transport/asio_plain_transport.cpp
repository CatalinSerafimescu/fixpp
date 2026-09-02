// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// src/transport/asio_plain_transport.cpp
//
// Implements `fixpp::transport::asio_plain_transport`: a plain-TCP Transport
// over asio::ip::tcp::socket with no TLS layer.
//
// Design anchors:
//   specs/043-plaintext-tcp-transport/contracts/asio_plain_transport.hpp
//   research.md D-1/D-2/D-12 — no handshake; close = socket_.close; same TCP knobs
//   data-model.md E-2 — field set and state transitions
//   asio_tls_transport.cpp — canonical reference implementation pattern

#include "asio_plain_transport.hpp"

#include "inflight_flag_guard.hpp"

#include <asio/any_io_executor.hpp>
#include <asio/async_result.hpp>
#include <asio/connect.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/redirect_error.hpp>
#include <asio/socket_base.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>
#include <fixpp/core/error.hpp>

// Disambiguate between the member and the free function asio::async_connect.
namespace asio_free = asio;

namespace fixpp::transport {

// ─────────────────────────────────────────────────────────────────────────────
// Constructors
// ─────────────────────────────────────────────────────────────────────────────

asio_plain_transport::asio_plain_transport(asio::any_io_executor exec, Transport::Config cfg)
    : cfg_{cfg}, exec_{exec}, socket_{exec} {}

asio_plain_transport::asio_plain_transport(from_accepted_tag, asio::any_io_executor exec,
                                           Transport::Config cfg,
                                           asio::ip::tcp::socket accepted_socket)
    : cfg_{cfg}, exec_{exec}, socket_{std::move(accepted_socket)}, state_{state_t::connected} {
    // Apply socket options immediately on the already-connected socket (D-12).
    apply_socket_options_();
}

// ─────────────────────────────────────────────────────────────────────────────
// ~asio_plain_transport — retires the armed timer epoch (D-4.1 item 3).
//
// This is a destructor-BODY statement, not a retiring member: members
// (including timer_epochs_ itself) are destroyed AFTER this body runs, so
// the retirement is sequenced strictly before socket_'s destruction. A
// stranded connect-timeout handler that observes the retired epoch is
// therefore guaranteed `this` is still alive when it decided not to touch
// it, and guaranteed dead when it did.
//
// Covers the destroy-with-no-drain leg (D-4.0): reconnect_fsm.cpp destroys
// this transport synchronously on the failure arm, with no wait for an
// in-flight timer handler to run first. The in-function retire-before-cancel
// at the arm site (below) does NOT cover a coroutine frame destroyed
// mid-co_await without resuming — this destructor is the only thing that
// does.
// ─────────────────────────────────────────────────────────────────────────────
asio_plain_transport::~asio_plain_transport() { ++timer_epochs_->connect; }

// ─────────────────────────────────────────────────────────────────────────────
// apply_socket_options_ — FR-029 / FR-029a socket-option application.
//
// Mirrors asio_tls_transport::apply_socket_options_() exactly (D-12).
// Best-effort: individual setsockopt failures are silently dropped.
// ─────────────────────────────────────────────────────────────────────────────
void asio_plain_transport::apply_socket_options_() noexcept {
    asio::error_code opt_ec;

    asio::ip::tcp::no_delay no_delay_opt{cfg_.tcp_nodelay};
    socket_.set_option(no_delay_opt, opt_ec);

    if (!cfg_.so_linger_enabled) {
        asio::socket_base::linger linger_opt{false, 0};
        socket_.set_option(linger_opt, opt_ec);
    } else {
        asio::socket_base::linger linger_opt{true, cfg_.so_linger_seconds};
        socket_.set_option(linger_opt, opt_ec);
    }

    if (cfg_.tcp_recv_buf_bytes > 0) {
        asio::socket_base::receive_buffer_size recv_buf{cfg_.tcp_recv_buf_bytes};
        socket_.set_option(recv_buf, opt_ec);
    }

    if (cfg_.tcp_send_buf_bytes > 0) {
        asio::socket_base::send_buffer_size send_buf{cfg_.tcp_send_buf_bytes};
        socket_.set_option(send_buf, opt_ec);
    }

    if (cfg_.tcp_keepalive) {
        asio::socket_base::keep_alive keepalive_opt{true};
        socket_.set_option(keepalive_opt, opt_ec);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// async_connect
// ─────────────────────────────────────────────────────────────────────────────
[[nodiscard]] asio::awaitable<core::expected_t<ConnectInfo>> asio_plain_transport::async_connect(
    Endpoint const& ep) {
    using E = core::error;

    // Enable total cancellation (co_spawn defaults to terminal-only per D-17).
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());

    // FR-006 is unconditional: after close() returns, EVERY async_* answers
    // transport_already_closed. This precedes the one-shot guard below, which
    // would otherwise collapse `closed` into transport_already_connected (#339).
    if (state_ == state_t::closed) {
        co_return std::unexpected{E::transport_already_closed};
    }

    // One-shot guard: only fresh transports can connect.
    if (state_ != state_t::fresh) {
        co_return std::unexpected{E::transport_already_connected};
    }

    // #342 overlap guard: the one-shot test above is a STATE test, and state_
    // does not leave `fresh` until an attempt SUCCEEDS -- so without this a
    // second async_connect issued while the first is still in flight passed
    // straight through and really attempted. asio's composed async_connect
    // calls socket_.close(ec) before each endpoint attempt, so the two
    // attempts corrupt each other (the first can surface as operation_aborted
    // -> transport_connect_timeout, and the shared connect epoch is advanced
    // by whichever finishes first). Overlap is now REFUSED with the variant
    // every contract site already published for it.
    if (connect_in_flight_) {
        co_return std::unexpected{E::transport_already_connected};
    }
    // Cleared on EVERY exit path, including frame destruction under
    // cancellation. A failed attempt leaves state_ == fresh AND clears this,
    // so the Transport stays retryable per FR-007.
    detail::inflight_flag_guard connect_guard{connect_in_flight_};

    // #341: there is NO pre-connect cancellation reap here, deliberately.
    // reset_cancellation_state() above re-constructs the coroutine's
    // cancellation_state from the parent slot, and that ctor emplaces a fresh
    // impl whose `cancelled_` is value-initialised (asio cancellation_state.hpp
    // ctor; awaitable_thread::reset_cancellation_state, asio impl/awaitable.hpp).
    // Emissions that already happened are NOT replayed into the new state, and
    // no suspension point separates the reset from this line, so a reap here
    // could only ever read `none`. Cancellation therefore takes effect from the
    // FIRST REAL SUSPENSION POINT onward -- the reaps below, which follow a
    // co_await, are reachable and are kept.
    // Re-derive: read the cancellation_state(slot, filter) ctor and confirm
    // impl_base::impl_base() zero-initialises cancelled_.

    // ── Resolve ───────────────────────────────────────────────────────────────
    asio::ip::tcp::resolver resolver{exec_};
    asio::error_code resolve_ec;
    auto endpoints = co_await resolver.async_resolve(
        ep.host, std::to_string(ep.port), asio::redirect_error(asio::use_awaitable, resolve_ec));

    if (resolve_ec) {
        if (resolve_ec == asio::error::operation_aborted) {
            co_return std::unexpected{E::transport_connect_cancelled};
        }
        co_return std::unexpected{E::transport_resolve_failed};
    }

    // Post-resolve cancellation reap.
    auto cs = co_await asio::this_coro::cancellation_state;
    if (cs.cancelled() != asio::cancellation_type::none) {
        co_return std::unexpected{E::transport_connect_cancelled};
    }

    // ── Connect with timeout ──────────────────────────────────────────────────
    // Arm a timer that cancels the socket on expiry (mirrors TLS transport).
    asio::steady_timer timer{exec_};
    timer.expires_after(cfg_.connect_timeout);
    // Timer-epoch guard (D-4.1, FR-014): the handler captures a COPY of
    // timer_epochs_, never `this` — nothing here touches `this` until the
    // guard has passed, so a handler stranded by a destroy-with-no-drain
    // (D-4.0) safely no-ops instead of reading/calling through dead memory.
    const std::uint64_t connect_epoch = ++timer_epochs_->connect;
    timer.async_wait([this, epochs = timer_epochs_, connect_epoch](asio::error_code ec) {
        if (ec || connect_epoch != epochs->connect) {
            return;  // cancelled, or this attempt's epoch was retired — no-op.
        }
        asio::error_code ignored;
        socket_.cancel(ignored);
    });

    // Map any accepted cancellation to `terminal` for the forwarded child op
    // so stop()'s total promptly aborts the connect.
    // [[feedback_asio_cospawn_total_cancellation_default]];
    // [[feedback_engine_stop_must_close_transports_total_cancel_insufficient]].
    co_await asio::this_coro::reset_cancellation_state(
        asio::enable_total_cancellation(), [](asio::cancellation_type ct) {
            return ct == asio::cancellation_type::none ? ct : asio::cancellation_type::terminal;
        });

    asio::error_code connect_ec;
    auto connected_ep = co_await asio_free::async_connect(
        socket_, endpoints, asio::redirect_error(asio::use_awaitable, connect_ec));

    // Retire this attempt's epoch BEFORE cancel() — a same-strand handler
    // still queued after cancel() sees the retirement and no-ops instead of
    // touching `this` (D-4.1).
    ++timer_epochs_->connect;
    timer.cancel();

    if (connect_ec) {
        if (connect_ec == asio::error::operation_aborted) {
            cs = co_await asio::this_coro::cancellation_state;
            if (cs.cancelled() != asio::cancellation_type::none) {
                co_return std::unexpected{E::transport_connect_cancelled};
            }
            co_return std::unexpected{E::transport_connect_timeout};
        }
        if (connect_ec == asio::error::connection_refused) {
            co_return std::unexpected{E::transport_connect_refused};
        }
        co_return std::unexpected{E::transport_connect_refused};
    }

    // ── Apply socket options post-connect (FR-029 / FR-029a / D-12) ──────────
    apply_socket_options_();

    // ── Populate ConnectInfo ──────────────────────────────────────────────────
    ConnectInfo info;
    {
        info.remote.host = connected_ep.address().to_string();
        info.remote.port = connected_ep.port();
        info.family = connected_ep.address().is_v4() ? AF_INET : AF_INET6;

        asio::error_code local_ec;
        auto local_ep = socket_.local_endpoint(local_ec);
        if (!local_ec) {
            info.local.host = local_ep.address().to_string();
            info.local.port = local_ep.port();
        }
    }

    state_ = state_t::connected;
    co_return info;
}

// ─────────────────────────────────────────────────────────────────────────────
// async_read_some
// ─────────────────────────────────────────────────────────────────────────────
[[nodiscard]] asio::awaitable<core::expected_t<std::size_t>> asio_plain_transport::async_read_some(
    std::span<std::byte> buf) {
    using E = core::error;

    // Enable total cancellation (D-17).
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());

    // state_ != connected covers both `fresh` (not yet connected) and `closed`.
    // Post-close every async_* returns transport_already_closed per FR-006.
    if (state_ != state_t::connected) {
        co_return std::unexpected{E::transport_already_closed};
    }

    // In-flight exclusivity guard (FR-007 — strand-confined boolean).
    if (read_in_flight_) {
        co_return std::unexpected{E::transport_read_in_progress};
    }

    // #341: no pre-read cancellation reap here, deliberately -- it would be
    // dead. reset_cancellation_state() above re-constructs the cancellation
    // state from the parent slot with `cancelled_` value-initialised, prior
    // emissions are not replayed, and nothing between it and here suspends
    // (the reset and the state read are both await_ready()==true awaiters --
    // asio impl/awaitable.hpp await_transform). A reap here could only read
    // `none`. Cancellation takes effect from the first real suspension point,
    // i.e. the socket_.async_read_some below, which completes with operation_aborted.

    read_in_flight_ = true;

    // NEVER allocate in the read-path completion handler per [const §VIII.5].
    // socket_.async_read_some writes directly into the caller-owned buf.
    asio::error_code ec;
    std::size_t bytes_read = co_await socket_.async_read_some(
        asio::buffer(buf.data(), buf.size()), asio::redirect_error(asio::use_awaitable, ec));

    read_in_flight_ = false;

    if (ec) {
        if (ec == asio::error::operation_aborted) {
            co_return std::unexpected{E::transport_read_cancelled};
        }
        if (ec == asio::error::eof) {
            co_return std::unexpected{E::transport_read_eof};
        }
        co_return std::unexpected{E::transport_read_error};
    }

    co_return bytes_read;
}

// ─────────────────────────────────────────────────────────────────────────────
// async_write
// ─────────────────────────────────────────────────────────────────────────────
[[nodiscard]] asio::awaitable<core::expected_t<std::size_t>> asio_plain_transport::async_write(
    std::span<const std::byte> bytes) {
    using E = core::error;

    // Enable total cancellation (D-17).
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());

    // state_ != connected covers both `fresh` and `closed`.
    if (state_ != state_t::connected) {
        co_return std::unexpected{E::transport_already_closed};
    }

    // In-flight exclusivity guard (FR-007).
    if (write_in_flight_) {
        co_return std::unexpected{E::transport_write_in_progress};
    }

    // #341: no pre-write cancellation reap here, deliberately -- it would be
    // dead. reset_cancellation_state() above re-constructs the cancellation
    // state from the parent slot with `cancelled_` value-initialised, prior
    // emissions are not replayed, and nothing between it and here suspends
    // (the reset and the state read are both await_ready()==true awaiters --
    // asio impl/awaitable.hpp await_transform). A reap here could only read
    // `none`. Cancellation takes effect from the first real suspension point,
    // i.e. the asio::async_write below, which completes with operation_aborted.

    write_in_flight_ = true;

    // Composed write (async_write — NOT async_write_some per FR-004).
    asio::error_code ec;
    std::size_t bytes_written =
        co_await asio::async_write(socket_, asio::buffer(bytes.data(), bytes.size()),
                                   asio::redirect_error(asio::use_awaitable, ec));

    write_in_flight_ = false;

    if (ec) {
        if (ec == asio::error::operation_aborted) {
            if (bytes_written > 0 && bytes_written < bytes.size()) {
                co_return std::unexpected{E::transport_write_short};
            }
            co_return std::unexpected{E::transport_write_cancelled};
        }
        co_return std::unexpected{E::transport_write_error};
    }

    if (bytes_written < bytes.size()) {
        co_return std::unexpected{E::transport_write_short};
    }

    co_return bytes_written;
}

// ─────────────────────────────────────────────────────────────────────────────
// cancel — synchronous, strand-confined, idempotent (FR-005; #333)
// ─────────────────────────────────────────────────────────────────────────────
[[nodiscard]] core::expected_t<void> asio_plain_transport::cancel() noexcept {
    // Does NOT close the socket (FR-005). ⚠️ This read "socket_.cancel() is
    // thread-safe per ASIO docs — the only operation safe to call off-strand"
    // until 2026-08-31 (#333). asio's basic_stream_socket @par Thread Safety
    // block says "Shared objects: Unsafe" and carves out only specific
    // SYNCHRONOUS operations (send/receive/connect/shutdown) — cancel is not
    // among them.
    // Call on the session strand. See Transport::cancel() for the disposition.
    asio::error_code ec;
    socket_.cancel(ec);
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// close — synchronous, strand-confined (FR-006)
// ─────────────────────────────────────────────────────────────────────────────
[[nodiscard]] core::expected_t<void> asio_plain_transport::close() noexcept {
    // Idempotent: second close() returns {} without side-effects.
    if (state_ == state_t::closed) {
        return {};
    }

    // Transition to closed immediately.
    state_ = state_t::closed;

    // Plain transport: NO TLS close-notify / SSL_shutdown / tls_close_timeout
    // wait (D-2, FR-011). Just close the socket directly.
    asio::error_code ec;
    socket_.close(ec);
    // Best-effort; ignore ec.

    return {};
}

}  // namespace fixpp::transport
