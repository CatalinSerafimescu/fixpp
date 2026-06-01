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

#include <cassert>
#include <cstddef>
#include <deque>
#include <functional>
#include <span>
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

}  // namespace fixpp::session::test
