// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_reconnect_cancel_mid_handshake.cpp — T008 [P] [US1] Phase 3
//
// cancellation_type::total mid-handshake aborts the attempt and releases the
// in-flight transport; ASan no-leak / no orphaned socket across N ≥ 3
// cancelled attempts.
//
// Anchors: FR-004; US1 AC3; SC-004;
//   [[feedback_asio_cospawn_total_cancellation_default]]: co_spawn defaults to
//   terminal-only cancellation; the coroutine MUST reset to
//   enable_total_cancellation() or a total stop silently hangs.
//
// TDD RED witness:
//   Current stub in reconnect_fsm.cpp:53-61 makes ONE make() call then returns
//   success without any co_await on async_handshake. Firing a total-cancel signal
//   finds nothing to cancel — the awaitable completes before the signal arrives.
//
//   The live_transport_count assertion fails because the stub never holds a
//   transport long enough for the counter to stay non-zero when the cancel fires;
//   but more importantly the stub returns SUCCESS when it should return a cancel
//   error. EXPECT_FALSE(result.has_value()) → FAILS RED (stub returns success).

#include <gtest/gtest.h>

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/redirect_error.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <fixpp/core/error.hpp>
#include <fixpp/session/reconnect_fsm.hpp>
#include <fixpp/tls/peer_identity.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/reconnect_policy.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <future>
#include <memory>
#include <memory_resource>
#include <span>
#include <vector>

using namespace std::chrono_literals;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// SlowTlsTransport — a TlsTransport that blocks in async_handshake for
// `latency` milliseconds before completing (or returning cancelled).
// Implements all 5 Transport virtuals + the 1 TlsTransport virtual.
// Counts alive instances via a shared counter for leak detection.
// ─────────────────────────────────────────────────────────────────────────────
class SlowTlsTransport final : public fixpp::transport::TlsTransport {
public:
    SlowTlsTransport(asio::any_io_executor exec, std::chrono::milliseconds handshake_latency,
                     std::atomic<int>& live_count)
        : exec_{std::move(exec)}, latency_{handshake_latency}, live_count_{live_count} {
        ++live_count_;
    }

    ~SlowTlsTransport() override { --live_count_; }

    // ── Transport overrides ─────────────────────────────────────────────────

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<fixpp::transport::ConnectInfo>>
    async_connect(fixpp::transport::Endpoint const& /*ep*/) override {
        co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
        co_await asio::post(exec_, asio::use_awaitable);
        if (auto cs = co_await asio::this_coro::cancellation_state;
            cs.cancelled() != asio::cancellation_type::none) {
            co_return std::unexpected{fixpp::core::error::transport_connect_cancelled};
        }
        fixpp::transport::ConnectInfo info;
        info.remote = fixpp::transport::Endpoint{"127.0.0.1", 0};
        info.local = fixpp::transport::Endpoint{"127.0.0.1", 0};
        info.family = 2;
        co_return info;
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<std::size_t>> async_read_some(
        std::span<std::byte> buf [[clang::lifetimebound]]) override {
        co_return std::unexpected{fixpp::core::error::transport_read_eof};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<std::size_t>> async_write(
        std::span<const std::byte> buf [[clang::lifetimebound]]) override {
        co_return buf.size();
    }

    [[nodiscard]] fixpp::core::expected_t<void> cancel() noexcept override { return {}; }

    [[nodiscard]] fixpp::core::expected_t<void> close() noexcept override { return {}; }

    // ── TlsTransport override ───────────────────────────────────────────────

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<fixpp::transport::handshake_result>>
    async_handshake(fixpp::tls::SslCtxConfig const& cfg [[clang::lifetimebound]]) override {
        using E = fixpp::core::error;
        co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());

        if (auto cs = co_await asio::this_coro::cancellation_state;
            cs.cancelled() != asio::cancellation_type::none) {
            co_return std::unexpected{E::transport_handshake_cancelled};
        }

        // Block for latency_ to allow a cancel signal to arrive.
        asio::steady_timer timer{exec_};
        timer.expires_after(latency_);
        asio::error_code wait_ec;
        co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, wait_ec));

        if (wait_ec == asio::error::operation_aborted) {
            co_return std::unexpected{E::transport_handshake_cancelled};
        }

        // Check for total-cancel after the wait.
        if (auto cs = co_await asio::this_coro::cancellation_state;
            cs.cancelled() != asio::cancellation_type::none) {
            co_return std::unexpected{E::transport_handshake_cancelled};
        }

        // Successful handshake (only reached if not cancelled).
        std::pmr::memory_resource* mr = cfg.mr ? cfg.mr : std::pmr::get_default_resource();
        co_return fixpp::transport::handshake_result{
            .peer_id = fixpp::tls::peer_identity{},
            .captured_pinset = nullptr,
            .negotiated_cipher = std::pmr::string{"TLS_AES_128_GCM_SHA256", mr},
        };
    }

private:
    asio::any_io_executor exec_;
    std::chrono::milliseconds latency_;
    std::atomic<int>& live_count_;
};

// ─────────────────────────────────────────────────────────────────────────────
// SlowHandshakeFactory — creates SlowTlsTransport instances.
// ─────────────────────────────────────────────────────────────────────────────
class SlowHandshakeFactory final : public fixpp::transport::TransportFactory {
public:
    explicit SlowHandshakeFactory(std::chrono::milliseconds handshake_latency)
        : handshake_latency_{handshake_latency} {}

    std::atomic<int> make_call_count{0};
    std::atomic<int> live_transport_count{0};

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::transport::Transport>> make(
        asio::any_io_executor exec, fixpp::tls::SslCtxConfig /*ssl_cfg*/,
        std::pmr::memory_resource* /*mr*/) noexcept override {
        ++make_call_count;
        return std::make_unique<SlowTlsTransport>(std::move(exec), handshake_latency_,
                                                  live_transport_count);
    }

    [[nodiscard]] fixpp::core::expected_t<void> reload_credentials(
        std::shared_ptr<fixpp::tls::cert_source> /*new_source*/) noexcept override {
        return {};
    }

    [[nodiscard]] std::shared_ptr<fixpp::tls::cert_source> cert_source_snapshot()
        const noexcept override {
        return nullptr;
    }

private:
    std::chrono::milliseconds handshake_latency_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Fixture
// ─────────────────────────────────────────────────────────────────────────────
class ReconnectCancelMidHandshakeTest : public ::testing::Test {
protected:
    asio::io_context ioc;

    static fixpp::transport::ReconnectPolicy make_policy(std::uint32_t max_attempts) {
        fixpp::transport::ReconnectPolicy policy;
        policy.max_attempts = max_attempts;
        policy.schedule =
            std::pmr::vector<std::chrono::milliseconds>{std::pmr::get_default_resource()};
        policy.schedule.push_back(0ms);
        policy.jitter = 0.0;
        return policy;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Cell: total-cancel mid-handshake aborts, releases transport, no leak.
//
// RED witness:
//   The stub returns success without awaiting async_handshake. The cancel
//   signal fires after the stub already completed → result is success
//   → EXPECT_FALSE(result.has_value()) FAILS RED.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(ReconnectCancelMidHandshakeTest, TotalCancelMidHandshakeReleasesTransport) {
    auto factory = std::make_shared<SlowHandshakeFactory>(50ms);

    fixpp::session::ReconnectFsm fsm(factory.get(), make_policy(100), std::chrono::seconds{30},
                                     2000ms);

    asio::cancellation_signal cancel_sig;

    auto fut = asio::co_spawn(ioc, fsm.drive_reconnect_attempt(),
                              asio::bind_cancellation_slot(cancel_sig.slot(), asio::use_future));

    // Fire total-cancel after 20ms (handshake latency is 50ms — should be in flight).
    asio::steady_timer timer{ioc};
    timer.expires_after(20ms);
    timer.async_wait([&cancel_sig](asio::error_code ec) {
        if (!ec) {
            cancel_sig.emit(asio::cancellation_type::total);
        }
    });

    ioc.run_for(1s);
    ioc.restart();

    ASSERT_EQ(fut.wait_for(std::chrono::seconds{0}), std::future_status::ready)
        << "drive_reconnect_attempt did not complete after total-cancel. "
        << "HANG: the coroutine was not reset to enable_total_cancellation() — "
        << "total-cancel is silently filtered (co_spawn default is terminal-only).";

    auto result = fut.get();

    // RED: stub returns success and ignores cancel.
    EXPECT_FALSE(result.has_value())
        << "Expected error (cancelled) after total-cancel mid-handshake; got success. "
        << "RED: stub returns success without reaching async_handshake.";

    // No orphaned transport: RAII must have released it.
    EXPECT_EQ(factory->live_transport_count.load(), 0)
        << "Transport was not released after cancel. "
        << "live_transport_count=" << factory->live_transport_count.load();
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell: N ≥ 3 successive cancelled attempts leave no leaks.
// RED: same — result is success (stub) → first iteration FAILS.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(ReconnectCancelMidHandshakeTest, RepeatedCancelsLeaveNoLeaksAcrossNAttempts) {
    constexpr int kAttempts = 3;
    auto factory = std::make_shared<SlowHandshakeFactory>(30ms);

    for (int i = 0; i < kAttempts; ++i) {
        fixpp::session::ReconnectFsm fsm(factory.get(), make_policy(100), std::chrono::seconds{30},
                                         2000ms);

        asio::cancellation_signal cancel_sig;

        auto fut =
            asio::co_spawn(ioc, fsm.drive_reconnect_attempt(),
                           asio::bind_cancellation_slot(cancel_sig.slot(), asio::use_future));

        asio::steady_timer timer{ioc};
        timer.expires_after(10ms);
        timer.async_wait([&cancel_sig](asio::error_code ec) {
            if (!ec) {
                cancel_sig.emit(asio::cancellation_type::total);
            }
        });

        ioc.run_for(500ms);
        ioc.restart();

        ASSERT_EQ(fut.wait_for(std::chrono::seconds{0}), std::future_status::ready)
            << "Attempt " << i << ": drive_reconnect_attempt did not complete. "
            << "HANG: enable_total_cancellation() not set in coroutine.";

        auto result = fut.get();
        EXPECT_FALSE(result.has_value())
            << "Attempt " << i << ": expected cancel error, got success.";

        EXPECT_EQ(factory->live_transport_count.load(), 0)
            << "Attempt " << i << ": orphaned transport after cancel. "
            << "live_transport_count=" << factory->live_transport_count.load();
    }
}

}  // namespace
