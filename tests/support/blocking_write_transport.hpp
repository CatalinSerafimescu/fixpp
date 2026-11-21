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
//   - its write does not allocate while blocking_write_state::record_bytes is false (the
//     default): it then records counts, never the bytes;
//   - release() is called from the thread that runs the io_context.
//
// 104-sequence-reset-fidelity (tasks.md T015; research D10, the write-start count) adds
// blocking_write_state::starts, counted as async_write's first statement, before the park
// and before its closed test, so a cell can see a write that has started and not completed.
//
// 104-sequence-reset-fidelity (tasks.md T015; research D10, the fifth test-support addition)
// adds an opt-in byte record: with blocking_write_state::record_bytes set, each async_write
// appends a copy of its bytes with its start index (the value of `starts` it counted), and
// marks the entry completed where it counts `writes`, so a cell can read each frame's 35 and
// 34 in write-start order. It allocates; a cell that needs the no-allocation condition
// leaves it off.
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
#include <vector>

#include "support/transport_double.hpp"

namespace fixpp::session::test {

// Shared between the factory's transport and the test.
struct blocking_write_state {
    bool block_next = false;                  // the next write parks until release()
    bool parked = false;                      // a write is parked now
    std::size_t starts = 0;                   // async_write calls begun (104)
    std::size_t writes = 0;                   // writes completed
    std::optional<asio::steady_timer> timer;  // the parked write's wait

    // The opt-in byte record (104): one entry per async_write while record_bytes is set.
    struct recorded_write {
        std::size_t start = 0;   // the write's start index: `starts` as this write counted it
        bool completed = false;  // set where `writes` counts this write
        std::vector<std::byte> bytes;
    };
    bool record_bytes = false;
    std::vector<recorded_write> recorded;  // in write-start order

    // 105-transport-liveness (tasks.md T035): the transport's closes, counted in close() (which
    // Transport::close_async's default calls), and `starts` as the first one read it, so a cell
    // can tell that the connection was closed and which writes had begun by then.
    std::size_t closes = 0;
    std::optional<std::size_t> starts_at_first_close;

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
        ++state_->starts;
        std::optional<std::size_t> rec;
        if (state_->record_bytes) {
            rec = state_->recorded.size();
            state_->recorded.push_back(
                {.start = state_->starts, .bytes = {bytes.begin(), bytes.end()}});
        }
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
        if (rec) state_->recorded[*rec].completed = true;
        co_return bytes.size();
    }

    [[nodiscard]] fixpp::core::expected_t<void> cancel() noexcept override { return {}; }

    [[nodiscard]] fixpp::core::expected_t<void> close() noexcept override {
        if (state_->closes++ == 0) state_->starts_at_first_close = state_->starts;
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
