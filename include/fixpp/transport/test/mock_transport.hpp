// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// fixpp::transport::test::mock_transport — public test header.
// Re-emitted from `specs/012-2h-transport/contracts/mock_transport.hpp` per
// T041 and T042 (the contract authorises inline-in-header impl; v1.0 emits
// every async method inline so no `src/transport/mock_transport.cpp` is
// required).
//
// CONSUMABLE FROM tests/-ONLY TRANSLATION UNITS per [const §VII]. Lives at
// include/fixpp/transport/test/mock_transport.hpp; the build system MUST
// exclude this directory from production target sources. The
// `FIXPP_ALLOW_MOCK_TRANSPORT` build-system token gates the `#error` below —
// production targets do NOT define it.
//
// Drives the session FSM through a pre-recorded byte stream + handshake-
// success/failure scripted outcomes. Implements `TlsTransport` (so the FSM's
// TLS-aware paths fire) but the handshake is purely virtual — no real OpenSSL
// calls. Consumers of this header MAY link without OpenSSL or ASIO networking.
//
// CANCELLATION HONOUR (BINDING per spec FR-037): every async method honours
// the awaiter's `cancellation_state` per [const §XI.2] — the mock does NOT
// short-circuit cancellation by completing instantly. Latency injection
// (`Script::read_latency` / `write_latency` / `handshake_latency`) composes
// `asio::steady_timer::async_wait` waits BEFORE the awaitable completes;
// cancellation-aware completions surface the same `transport_*_cancelled`
// variants the production `asio_tls_transport` does.

#pragma once

#if !defined(FIXPP_ALLOW_MOCK_TRANSPORT)
#error "include/fixpp/transport/test/mock_transport.hpp is a test-only header. \
Define FIXPP_ALLOW_MOCK_TRANSPORT in the consuming test target."
#endif

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/error.hpp>
#include <asio/post.hpp>
#include <asio/redirect_error.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>

#include <chrono>
#include <cstddef>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <fixpp/core/error.hpp>             // core::expected_t<T>
#include <fixpp/tls/peer_identity.hpp>      // fixpp::tls::peer_identity
#include <fixpp/tls/pinset.hpp>             // fixpp::tls::pin_snapshot
#include <fixpp/tls/security_profile.hpp>   // fixpp::tls::SslCtxConfig
#include <fixpp/transport/endpoint.hpp>     // Endpoint
#include <fixpp/transport/tls_transport.hpp>  // TlsTransport + handshake_result
#include <fixpp/transport/transport.hpp>      // Transport + ConnectInfo

namespace fixpp::transport::test {

// ─────────────────────────────────────────────────────────────────────────────
// Script — test-author-supplied programmatic interface enumerating expected
// reads / writes / errors / cancellations in sequence per spec FR-038.
// ─────────────────────────────────────────────────────────────────────────────
struct Script {
    // Queued bytes delivered by async_read_some calls. async_read_some
    // completes with up to buf.size() bytes per call, draining from
    // inbound_bytes via read_cursor_. When the cursor reaches the end,
    // subsequent reads complete with transport_read_eof.
    std::vector<std::byte> inbound_bytes;

    // Optional: expected outbound writes verified against outbound_seen_.
    // Empty vector = no verification. Mismatches surface as a
    // transport_write_short error (the test asserts via diagnostic accessors;
    // we don't throw here — the header is link-light).
    std::vector<std::vector<std::byte>> expected_outbound_writes;

    // Handshake outcome control.
    bool handshake_succeeds {true};

    // Populated when handshake_succeeds — returned in handshake_result.
    fixpp::tls::peer_identity                       peer_identity_to_return;
    std::shared_ptr<const fixpp::tls::pin_snapshot> pinset_snapshot_to_return;
    std::string                                     negotiated_cipher
        = "TLS_AES_128_GCM_SHA256";

    // Latency injection — tests for cancellation race conditions. Each
    // async method composes an asio::steady_timer::async_wait of the
    // corresponding latency BEFORE the awaitable completes; the
    // cancellation-honour binding then surfaces the matching *_cancelled
    // variant on cancellation_type::total.
    std::chrono::milliseconds read_latency      {0};
    std::chrono::milliseconds write_latency     {0};
    std::chrono::milliseconds handshake_latency {0};

    // Partial-write injection — when > 0, async_write writes only the first
    // `partial_write_bytes` of the requested span and returns
    // transport_write_short. Used by the FR-037 partial-write-then-cancel
    // cell to verify the mock honours the same variant the production
    // asio_tls_transport surfaces.
    std::size_t partial_write_bytes {0};

    // ConnectInfo returned by async_connect. Default = loopback IPv4.
    ConnectInfo connect_info {
        .remote = Endpoint{"127.0.0.1", 0},
        .local  = Endpoint{"127.0.0.1", 0},
        .family = 2 /* AF_INET */
    };
};

// ─────────────────────────────────────────────────────────────────────────────
// mock_transport — deterministic TlsTransport impl for FSM tests.
//
// All async methods compose an `asio::post(exec_)` checkpoint (deferred
// resume) and, if the matching latency is non-zero, an
// `asio::steady_timer::async_wait`. Both honour the awaiter's
// `cancellation_state` per [const §XI.2] — emit cancellation_type::total
// during the wait and the method surfaces the matching `*_cancelled` variant.
//
// Strand-confined state (read_cursor_, outbound_seen_, closed_, handshaken_):
// all reads/writes happen on `exec_`'s strand (consumer's responsibility to
// run the methods on that strand; the tests use co_spawn on `exec_`).
// `cancel()` is the only off-strand path and is idempotent.
// ─────────────────────────────────────────────────────────────────────────────
class mock_transport final : public TlsTransport {
public:
    explicit mock_transport(asio::any_io_executor exec, Script script)
        : exec_{std::move(exec)}, script_{std::move(script)} {}

    mock_transport(mock_transport const&)            = delete;
    mock_transport& operator=(mock_transport const&) = delete;
    mock_transport(mock_transport&&)                 = delete;
    mock_transport& operator=(mock_transport&&)      = delete;

    ~mock_transport() override = default;

    // ── Transport overrides ─────────────────────────────────────────────────

    [[nodiscard]] asio::awaitable<core::expected_t<ConnectInfo>>
    async_connect(Endpoint const& /*ep*/) override
    {
        using E = core::error;
        co_await asio::this_coro::reset_cancellation_state(
            asio::enable_total_cancellation());

        if (closed_) {
            co_return std::unexpected{E::transport_already_closed};
        }
        if (auto cs = co_await asio::this_coro::cancellation_state;
            cs.cancelled() != asio::cancellation_type::none) {
            co_return std::unexpected{E::transport_connect_cancelled};
        }

        // Deferred resume — checkpoint so the awaiter's cancellation slot
        // gets a fair chance to fire before we return.
        co_await asio::post(exec_, asio::use_awaitable);
        if (auto cs = co_await asio::this_coro::cancellation_state;
            cs.cancelled() != asio::cancellation_type::none) {
            co_return std::unexpected{E::transport_connect_cancelled};
        }

        co_return script_.connect_info;
    }

    [[nodiscard]] asio::awaitable<core::expected_t<std::size_t>>
    async_read_some(std::span<std::byte> buf [[clang::lifetimebound]]) override
    {
        using E = core::error;
        co_await asio::this_coro::reset_cancellation_state(
            asio::enable_total_cancellation());

        if (closed_) {
            co_return std::unexpected{E::transport_already_closed};
        }
        if (auto cs = co_await asio::this_coro::cancellation_state;
            cs.cancelled() != asio::cancellation_type::none) {
            co_return std::unexpected{E::transport_read_cancelled};
        }

        // Latency injection — async_wait honours cancellation_type::total.
        if (script_.read_latency > std::chrono::milliseconds{0}) {
            asio::steady_timer timer{exec_};
            timer.expires_after(script_.read_latency);
            asio::error_code wait_ec;
            co_await timer.async_wait(
                asio::redirect_error(asio::use_awaitable, wait_ec));
            if (wait_ec == asio::error::operation_aborted) {
                co_return std::unexpected{E::transport_read_cancelled};
            }
        }

        // Drain inbound_bytes against read_cursor_.
        if (read_cursor_ >= script_.inbound_bytes.size()) {
            co_return std::unexpected{E::transport_read_eof};
        }

        const std::size_t available = script_.inbound_bytes.size() - read_cursor_;
        const std::size_t n         = std::min(buf.size(), available);

        for (std::size_t i = 0; i < n; ++i) {
            buf[i] = script_.inbound_bytes[read_cursor_ + i];
        }
        read_cursor_ += n;

        co_return n;
    }

    [[nodiscard]] asio::awaitable<core::expected_t<std::size_t>>
    async_write(std::span<const std::byte> bytes [[clang::lifetimebound]]) override
    {
        using E = core::error;
        co_await asio::this_coro::reset_cancellation_state(
            asio::enable_total_cancellation());

        if (closed_) {
            co_return std::unexpected{E::transport_already_closed};
        }
        if (auto cs = co_await asio::this_coro::cancellation_state;
            cs.cancelled() != asio::cancellation_type::none) {
            co_return std::unexpected{E::transport_write_cancelled};
        }

        // Latency injection.
        if (script_.write_latency > std::chrono::milliseconds{0}) {
            asio::steady_timer timer{exec_};
            timer.expires_after(script_.write_latency);
            asio::error_code wait_ec;
            co_await timer.async_wait(
                asio::redirect_error(asio::use_awaitable, wait_ec));
            if (wait_ec == asio::error::operation_aborted) {
                co_return std::unexpected{E::transport_write_cancelled};
            }
        }

        // Partial-write injection — FR-037 partial-write-then-cancel cell.
        if (script_.partial_write_bytes > 0
            && script_.partial_write_bytes < bytes.size()) {
            for (std::size_t i = 0; i < script_.partial_write_bytes; ++i) {
                outbound_seen_.push_back(bytes[i]);
            }
            ++writes_observed_;
            co_return std::unexpected{E::transport_write_short};
        }

        // Full write — capture into outbound_seen_.
        for (auto b : bytes) {
            outbound_seen_.push_back(b);
        }
        ++writes_observed_;

        co_return bytes.size();
    }

    [[nodiscard]] core::expected_t<void> cancel() noexcept override {
        // Mock cancel is a no-op signal — actual cancellation is driven by the
        // awaiter's cancellation_signal at the test layer (the tests use
        // bind_cancellation_slot on co_spawn and emit on the signal). Per the
        // Transport contract cancel() is documented synchronous + thread-safe.
        return {};
    }

    [[nodiscard]] core::expected_t<void> close() noexcept override {
        closed_ = true;
        return {};
    }

    // ── TlsTransport override ───────────────────────────────────────────────

    [[nodiscard]] asio::awaitable<core::expected_t<handshake_result>>
    async_handshake(fixpp::tls::SslCtxConfig const& cfg [[clang::lifetimebound]]) override
    {
        using E = core::error;
        co_await asio::this_coro::reset_cancellation_state(
            asio::enable_total_cancellation());

        if (closed_) {
            co_return std::unexpected{E::transport_already_closed};
        }
        if (handshaken_) {
            co_return std::unexpected{E::transport_already_connected};
        }
        if (auto cs = co_await asio::this_coro::cancellation_state;
            cs.cancelled() != asio::cancellation_type::none) {
            co_return std::unexpected{E::transport_handshake_cancelled};
        }

        // Latency injection.
        if (script_.handshake_latency > std::chrono::milliseconds{0}) {
            asio::steady_timer timer{exec_};
            timer.expires_after(script_.handshake_latency);
            asio::error_code wait_ec;
            co_await timer.async_wait(
                asio::redirect_error(asio::use_awaitable, wait_ec));
            if (wait_ec == asio::error::operation_aborted) {
                co_return std::unexpected{E::transport_handshake_cancelled};
            }
        }

        if (!script_.handshake_succeeds) {
            co_return std::unexpected{E::transport_handshake_failed};
        }

        handshaken_ = true;

        // Build the scripted handshake_result. The negotiated_cipher uses
        // SslCtxConfig::mr per [2g §4.5]; null mr falls back to the default
        // upstream resource (matching the production semantics).
        std::pmr::memory_resource* mr
            = cfg.mr ? cfg.mr : std::pmr::get_default_resource();
        co_return handshake_result{
            .peer_id           = script_.peer_identity_to_return,
            .captured_pinset   = script_.pinset_snapshot_to_return,
            .negotiated_cipher = std::pmr::string{script_.negotiated_cipher, mr},
        };
    }

    // ── Test-only diagnostics ───────────────────────────────────────────────

    [[nodiscard]] std::size_t bytes_read_so_far() const noexcept {
        return read_cursor_;
    }

    [[nodiscard]] std::vector<std::byte> outbound_bytes_seen() const {
        return outbound_seen_;
    }

    [[nodiscard]] std::size_t async_writes_observed() const noexcept {
        return writes_observed_;
    }

    [[nodiscard]] bool is_closed() const noexcept { return closed_; }
    [[nodiscard]] bool is_handshaken() const noexcept { return handshaken_; }

private:
    asio::any_io_executor  exec_;
    Script                 script_;
    std::size_t            read_cursor_     {0};
    std::vector<std::byte> outbound_seen_;
    std::size_t            writes_observed_ {0};
    bool                   closed_          {false};
    bool                   handshaken_      {false};
};

}  // namespace fixpp::transport::test
