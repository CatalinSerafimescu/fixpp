// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/support/transport_double.hpp
//
// Seam #0 — in-memory bidirectional frame transport test double (T013).
//
// Feeds verified inbound frames to the session under test and captures all
// outbound frames emitted by the session. Used by Phase 3–6 seam tests that
// exercise the FIX establishment FSM without a real TCP/TLS connection.
//
// Anchors: data-model.md E5 (admin messages); tasks.md T013.
// This header may be included from awaitable coroutine contexts.
// No std::mutex here — synchronisation is the test fixture's responsibility
// (tests run single-threaded through the mock clock / io_context; seam tests
// drive everything on one thread). ([const §XV.9] grep gate: safe.)
#pragma once

#include <algorithm>
#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <cassert>
#include <cstddef>
#include <deque>
#include <fixpp/core/error.hpp>
#include <fixpp/tls/cert_source.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <functional>
#include <memory>
#include <memory_resource>
#include <span>
#include <utility>
#include <vector>

namespace fixpp::session::test {

// ── Frame buffer ──────────────────────────────────────────────────────────────

using Frame = std::vector<std::byte>;

// ── TransportDouble ───────────────────────────────────────────────────────────

// Bidirectional in-memory frame transport for unit tests.
//
// INBOUND: call feed_inbound(frame) to push a pre-built FIX frame into the
// queue; the session pumps it via on_inbound_frame().
//
// OUTBOUND: every frame the session "sends" is appended to sent_frames().
// The test verifies the sequence and content.
//
// Lifecycle: stateless reset via reset(); each test starts fresh.
class TransportDouble {
public:
    TransportDouble() = default;

    TransportDouble(const TransportDouble&) = delete;
    TransportDouble& operator=(const TransportDouble&) = delete;
    TransportDouble(TransportDouble&&) = default;
    TransportDouble& operator=(TransportDouble&&) = default;

    // ── Inbound (test → session) ──────────────────────────────────────────────

    /// Enqueue a pre-built inbound FIX frame. Bytes are deep-copied.
    void feed_inbound(std::span<const std::byte> frame) {
        inbound_.emplace_back(frame.begin(), frame.end());
    }

    /// Convenience: feed from a string_view (ASCII FIX wire bytes).
    void feed_inbound_str(std::string_view sv) {
        const auto* bp = reinterpret_cast<const std::byte*>(sv.data());
        feed_inbound(std::span<const std::byte>{bp, sv.size()});
    }

    /// Pop the next inbound frame (returns empty span if queue is empty).
    /// The transport double retains ownership; the span is valid until the
    /// next call to pop_inbound().
    [[nodiscard]] std::span<const std::byte> pop_inbound() {
        if (inbound_.empty()) {
            return {};
        }
        current_in_ = std::move(inbound_.front());
        inbound_.pop_front();
        return {current_in_.data(), current_in_.size()};
    }

    [[nodiscard]] bool has_inbound() const noexcept { return !inbound_.empty(); }
    [[nodiscard]] std::size_t inbound_count() const noexcept { return inbound_.size(); }

    // ── Outbound (session → test) ─────────────────────────────────────────────

    /// Called by the session when it "transmits" a frame. Captures a copy.
    void capture_outbound(std::span<const std::byte> frame) {
        sent_frames_.emplace_back(frame.begin(), frame.end());
    }

    /// All captured outbound frames in emit order.
    [[nodiscard]] const std::vector<Frame>& sent_frames() const noexcept { return sent_frames_; }

    [[nodiscard]] std::size_t sent_count() const noexcept { return sent_frames_.size(); }

    /// Access the Nth sent frame (0-based).
    [[nodiscard]] std::span<const std::byte> sent(std::size_t n) const {
        assert(n < sent_frames_.size());
        return {sent_frames_[n].data(), sent_frames_[n].size()};
    }

    // ── Reset ─────────────────────────────────────────────────────────────────

    void reset() {
        inbound_.clear();
        sent_frames_.clear();
        current_in_.clear();
    }

private:
    std::deque<Frame> inbound_;       // pending inbound frames
    Frame current_in_;                // backing store for pop_inbound() span
    std::vector<Frame> sent_frames_;  // all captured outbound frames
};

// ── ScriptedStream / ScriptedReadTransport ────────────────────────────────────
//
// 093-inbound-frame-dispositions (quickstart Q-16, readable across T): a
// fixpp::transport::Transport whose inbound side is a FINITE script of chunks. Each
// async_read_some completes at initiation, without suspending: it serves the next
// chunk whole, or transport_read_eof once the script is exhausted. So in a race
// against a deadline the read arm is always ready first, and a reader that never
// stops on its own reaches EOF rather than spinning.
//
// `on_read` runs inside each read, before its chunk is served, with the chunk's
// index; a cell uses it to move a mock clock while the read is in flight. The
// stream is shared, so the cell keeps observing it after the engine has taken the
// transport.
struct ScriptedStream {
    std::vector<Frame> chunks;                 // served one per read, in order
    std::function<void(std::size_t)> on_read;  // the chunk's index, before it is served
    std::size_t reads_served = 0;              // chunks served so far
    std::vector<Frame> written;                // every async_write, in order
    bool closed = false;                       // close() was called
};

class ScriptedReadTransport final : public fixpp::transport::Transport {
public:
    explicit ScriptedReadTransport(std::shared_ptr<ScriptedStream> stream)
        : stream_{std::move(stream)} {}

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<fixpp::transport::ConnectInfo>>
    async_connect(fixpp::transport::Endpoint const& /*ep*/) override {
        co_return fixpp::transport::ConnectInfo{};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<std::size_t>> async_read_some(
        std::span<std::byte> buf) override {
        if (stream_->closed) {
            co_return std::unexpected(fixpp::core::error::transport_already_closed);
        }
        if (stream_->reads_served == stream_->chunks.size()) {
            co_return std::unexpected(fixpp::core::error::transport_read_eof);
        }
        std::size_t const idx = stream_->reads_served++;
        if (stream_->on_read) stream_->on_read(idx);
        Frame const& chunk = stream_->chunks[idx];
        assert(chunk.size() <= buf.size() && "a chunk must fit one read");
        std::copy(chunk.begin(), chunk.end(), buf.begin());
        co_return chunk.size();
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<std::size_t>> async_write(
        std::span<const std::byte> bytes) override {
        if (stream_->closed) {
            co_return std::unexpected(fixpp::core::error::transport_already_closed);
        }
        stream_->written.emplace_back(bytes.begin(), bytes.end());
        co_return bytes.size();
    }

    [[nodiscard]] fixpp::core::expected_t<void> cancel() noexcept override { return {}; }

    [[nodiscard]] fixpp::core::expected_t<void> close() noexcept override {
        stream_->closed = true;
        return {};
    }

private:
    std::shared_ptr<ScriptedStream> stream_;
};

// Mints ScriptedReadTransport over one shared stream, as a plaintext factory, so an
// engine-managed initiator (SessionConfig::transport_factory_override with
// insecure_plain_tcp) connects to it, sends its Logon and runs its read pump over
// the script.
class ScriptedReadTransportFactory final : public fixpp::transport::TransportFactory {
public:
    explicit ScriptedReadTransportFactory(std::shared_ptr<ScriptedStream> stream)
        : stream_{std::move(stream)} {}

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::transport::Transport>> make(
        asio::any_io_executor /*exec*/, fixpp::tls::SslCtxConfig /*ssl_cfg*/,
        std::pmr::memory_resource* /*mr*/) noexcept override {
        return std::make_unique<ScriptedReadTransport>(stream_);
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
};

}  // namespace fixpp::session::test
