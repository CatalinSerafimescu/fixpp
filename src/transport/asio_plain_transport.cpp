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
#include <chrono>
#include <fixpp/core/error.hpp>

#include "bounded_resolve.hpp"
#include "inflight_flag_guard.hpp"

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
    // WHY 97 and not a new transport_connect_in_progress sibling of the 99/100
    // pair the read/write guards use: 97 is what every published contract site
    // ALREADY named for this case, so the guard makes the contract true instead
    // of rewriting it — and a new variant could not sit in the family anyway,
    // which is pinned contiguous at 94..115 (FR-034 / T006) while error.hpp
    // already runs past 115.
    if (timer_epochs_->connect_in_flight) {
        co_return std::unexpected{E::transport_already_connected};
    }
    // Cleared on EVERY exit path, including frame destruction under
    // cancellation. A failed attempt leaves state_ == fresh AND clears this,
    // so the Transport stays retryable per FR-007.
    detail::inflight_flag_guard connect_guard{timer_epochs_,
                                             &timer_epoch_state::connect_in_flight};

    // #341: no pre-connect cancellation reap here, deliberately -- it would be
    // dead. See the CANCELLATION TIMING note on Transport in transport.hpp
    // for the mechanism and the re-derivation recipe.

    // ONE budget for the whole attempt, shared by the resolve below and the
    // connect timer further down (#361) — the rationale is on resolve_bounded()
    // in bounded_resolve.hpp, stated once because it is one decision.
    const auto connect_deadline = std::chrono::steady_clock::now() + cfg_.connect_timeout;

    // ── Resolve ───────────────────────────────────────────────────────────────
    // Abandonable: asio's resolver takes no cancellation slot, so awaiting it
    // directly is what made stop() unbounded. bounded_resolve.hpp carries the
    // mechanism, the falsified alternatives, and the measured limit of what
    // abandoning buys.
    //
    // Engine::stop()'s `total` reaches the gate timer in there because the entry
    // reset at the top of this function already accepts it — no second reset
    // here, and deliberately no pre-resolve REAP either (#341's note on why that
    // would be dead still applies).
    auto resolved =
        co_await detail::resolve_bounded(exec_, ep.host, std::to_string(ep.port), connect_deadline);
    if (!resolved) {
        co_return std::unexpected{resolved.error()};
    }
    auto endpoints = std::move(*resolved);

    // Post-resolve cancellation reap.
    auto cs = co_await asio::this_coro::cancellation_state;
    if (cs.cancelled() != asio::cancellation_type::none) {
        co_return std::unexpected{E::transport_connect_cancelled};
    }

    // #347: close() runs on this strand and can have executed while we were
    // suspended in the bounded resolve -- which the RESOLVER never observes,
    // because socket_.close() cancels neither it nor the gate timer (#361 made
    // the WAIT abandonable, not the op observable). FR-006 is unconditional, so
    // re-test it here instead of opening a socket the owner already closed.
    if (state_ == state_t::closed) {
        co_return std::unexpected{E::transport_already_closed};
    }

    // ── Connect with timeout ──────────────────────────────────────────────────
    // Arm a timer that cancels the socket on expiry (mirrors TLS transport).
    asio::steady_timer timer{exec_};
    // Shared deadline (#361), not a fresh connect_timeout — see the budget note
    // at the top of this function.
    timer.expires_at(connect_deadline);
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

    // #347: last re-test before committing the state. asio's composed
    // async_connect OPENS the socket it connects, so a close() that landed
    // during the attempt has already been undone by the time we get here --
    // close the socket again rather than publishing `connected` and leaving a
    // live socket behind a close() that already returned.
    if (state_ == state_t::closed) {
        asio::error_code close_ec;
        socket_.close(close_ec);
        co_return std::unexpected{E::transport_already_closed};
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
    if (timer_epochs_->read_in_flight) {
        co_return std::unexpected{E::transport_read_in_progress};
    }

    // #341: no pre-read cancellation reap here, deliberately -- it would be
    // dead. See the CANCELLATION TIMING note on Transport in transport.hpp
    // for the mechanism and the re-derivation recipe.

    // #346: RAII — see inflight_flag_guard.hpp for why not assignment.
    detail::inflight_flag_guard read_guard{timer_epochs_, &timer_epoch_state::read_in_flight};

    // NEVER allocate in the read-path completion handler per [const §VIII.5].
    // socket_.async_read_some writes directly into the caller-owned buf.
    asio::error_code ec;
    std::size_t bytes_read = co_await socket_.async_read_some(
        asio::buffer(buf.data(), buf.size()), asio::redirect_error(asio::use_awaitable, ec));

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

    // #357 — the OUT filter, for the same reason the TLS write has it, on the
    // SAME asio layer. This method awaits the COMPOSED `asio::async_write`, whose
    // write_op installs `enable_partial_cancellation()` (asio/impl/write.hpp) —
    // a mask of terminal|partial that does NOT contain `total`. So Engine::stop()'s
    // `total` was accepted by this frame, forwarded unchanged by the one-argument
    // reset that used to be here, and then dropped one layer down.
    //
    // ⚠️ THIS IS THE PLAIN TRANSPORT, and it was missed when the four TLS sites
    // were fixed — found by a reviewer applying that commit's own argument
    // ("leaving one of four wrong is how the next reader concludes the asymmetry
    // is deliberate") across the file boundary the asio mechanism does not respect.
    //
    // ⚠️ `async_read_some` above does NOT need this and must not get it "for
    // symmetry": it awaits the RAW `socket_.async_read_some`, and a reactive
    // socket op's cancellation handler accepts terminal|partial|total directly
    // (asio/detail/reactive_socket_service_base.hpp). The distinction is COMPOSED
    // vs RAW, not read vs write — re-derive it from the awaited call, never from
    // this list.
    co_await asio::this_coro::reset_cancellation_state(
        asio::enable_total_cancellation(), [](asio::cancellation_type ct) {
            return ct == asio::cancellation_type::none ? ct : asio::cancellation_type::terminal;
        });

    // state_ != connected covers both `fresh` and `closed`.
    if (state_ != state_t::connected) {
        co_return std::unexpected{E::transport_already_closed};
    }

    // In-flight exclusivity guard (FR-007).
    if (timer_epochs_->write_in_flight) {
        co_return std::unexpected{E::transport_write_in_progress};
    }

    // #341: no pre-write cancellation reap here, deliberately -- it would be
    // dead. See the CANCELLATION TIMING note on Transport in transport.hpp
    // for the mechanism and the re-derivation recipe.

    // #346: RAII — see inflight_flag_guard.hpp for why not assignment.
    detail::inflight_flag_guard write_guard{timer_epochs_, &timer_epoch_state::write_in_flight};

    // Composed write (async_write — NOT async_write_some per FR-004).
    asio::error_code ec;
    std::size_t bytes_written =
        co_await asio::async_write(socket_, asio::buffer(bytes.data(), bytes.size()),
                                   asio::redirect_error(asio::use_awaitable, ec));

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
