// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/support/blocking_write_transport.hpp — test-only.
//
// fixpp#544 (B35; `.specify/544-hot-path-zero-alloc.md` §3, W-D-W's real-chain twin): a
// transport double whose reads come from a ScriptedStream (tests/support/transport_double.hpp)
// and whose next write can be held in its write until the test releases it, so a real
// `Session::send` chain can be parked in its write while other work runs on the strand.
// It is ControlledWriteTransport's arm_block()/release() pattern
// (tests/session/test_live_outbound_serialized.cpp) behind a TransportFactory, so the real
// Engine pump reaches it through `SessionConfig::transport_factory_override`.
//
// CONDITIONS THE TWIN DEPENDS ON, checked by reading:
//   - its write chain matches `asio_plain_transport::async_write`
//     (src/transport/asio_plain_transport.cpp) frame for frame: one coroutine, and one
//     `use_awaitable` adapter where it suspends;
//   - its write does not allocate: it records counts, never the bytes;
//   - release() is called from the thread that runs the io_context.
#pragma once

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/redirect_error.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <cstddef>
#include <fixpp/core/error.hpp>
#include <fixpp/tls/cert_source.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <system_error>
#include <utility>

#include "support/transport_double.hpp"

namespace fixpp::session::test {

// Shared between the factory's transport and the test.
struct blocking_write_state {
    bool block_next = false;                  // the next write parks until release()
    bool parked = false;                      // a write is parked now
    std::size_t writes = 0;                   // writes completed
    std::optional<asio::steady_timer> timer;  // the parked write's wait

    void arm_block() noexcept { block_next = true; }
    void release() {
        block_next = false;
        if (timer) timer->cancel();
    }
};

class BlockingWriteTransport final : public fixpp::transport::Transport {
public:
    BlockingWriteTransport(asio::any_io_executor exec, std::shared_ptr<ScriptedStream> stream,
                           std::shared_ptr<blocking_write_state> state)
        : reader_{std::move(stream)}, state_{std::move(state)} {
        state_->timer.emplace(std::move(exec), asio::steady_timer::time_point::max());
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<fixpp::transport::ConnectInfo>>
    async_connect(fixpp::transport::Endpoint const& ep) override {
        return reader_.async_connect(ep);
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<std::size_t>> async_read_some(
        std::span<std::byte> buf) override {
        return reader_.async_read_some(buf);
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<std::size_t>> async_write(
        std::span<const std::byte> bytes) override {
        if (closed_) co_return std::unexpected(fixpp::core::error::transport_already_closed);
        if (state_->block_next) {
            state_->parked = true;
            // NOLINTBEGIN(bugprone-unchecked-optional-access): the constructor emplaces the timer
            state_->timer->expires_at(asio::steady_timer::time_point::max());
            std::error_code ec;
            co_await state_->timer->async_wait(asio::redirect_error(asio::use_awaitable, ec));
            // NOLINTEND(bugprone-unchecked-optional-access)
            state_->parked = false;
            if (closed_) co_return std::unexpected(fixpp::core::error::transport_already_closed);
        }
        ++state_->writes;
        co_return bytes.size();
    }

    [[nodiscard]] fixpp::core::expected_t<void> cancel() noexcept override { return {}; }

    [[nodiscard]] fixpp::core::expected_t<void> close() noexcept override {
        closed_ = true;
        state_->block_next = false;
        if (state_->timer) state_->timer->cancel();
        return reader_.close();
    }

private:
    ScriptedReadTransport reader_;
    std::shared_ptr<blocking_write_state> state_;
    bool closed_ = false;
};

class BlockingWriteTransportFactory final : public fixpp::transport::TransportFactory {
public:
    BlockingWriteTransportFactory(std::shared_ptr<ScriptedStream> stream,
                                  std::shared_ptr<blocking_write_state> state)
        : stream_{std::move(stream)}, state_{std::move(state)} {}

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::transport::Transport>> make(
        asio::any_io_executor exec, fixpp::tls::SslCtxConfig /*ssl_cfg*/,
        std::pmr::memory_resource* /*mr*/) noexcept override {
        return std::make_unique<BlockingWriteTransport>(std::move(exec), stream_, state_);
    }

    [[nodiscard]] fixpp::core::expected_t<void> reload_credentials(
        std::shared_ptr<fixpp::tls::cert_source> /*new_source*/) noexcept override {
        return {};
    }

    [[nodiscard]] std::shared_ptr<fixpp::tls::cert_source> cert_source_snapshot()
        const noexcept override {
        return nullptr;
    }

    [[nodiscard]] fixpp::transport::transport_security_kind kind() const noexcept override {
        return fixpp::transport::transport_security_kind::plaintext;
    }

private:
    std::shared_ptr<ScriptedStream> stream_;
    std::shared_ptr<blocking_write_state> state_;
};

}  // namespace fixpp::session::test
