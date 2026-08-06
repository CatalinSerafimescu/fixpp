// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/session/read_first_frame_bounded.hpp — internal session header.
//
// read_first_frame_bounded, extracted from the engine.cpp anonymous namespace
// to enable direct unit testing (088 T008).
//
// Previously defined in engine.cpp anonymous namespace; moved here as `inline`
// to allow the test translation unit to include and call the function without a
// separate compiled object. engine.cpp includes this header (replacing the
// anonymous-namespace definition). The test includes it via the
// ${CMAKE_SOURCE_DIR}/src include path added to the test target.
//
// Internal header: NOT under include/fixpp/ (not part of the public API).
// Do NOT include from public headers or library consumers.
//
// Anchors: specs/088-firstframe-budget-timer-lifetime/contracts/read_first_frame_bounded.md;
// research.md D-5; tasks.md T008.
#pragma once

#include <algorithm>
#include <array>
#include <asio/awaitable.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <chrono>
#include <cstddef>
#include <fixpp/core/error.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/wire/framer.hpp>
#include <memory_resource>
#include <span>
#include <vector>

namespace fixpp::session::detail {

// ── Bounded first-frame read (FR-014 / E-2 / C1 steps 2-3) ──────────────────
// Reads raw bytes from an accepted (not-yet-TLS-handshaken, post-handshake) TCP
// transport into `buf` with a deadline. Returns the number of bytes read on
// success, or an error on timeout / over-budget / read-fail.
//
// Used AFTER async_handshake succeeds — we read TLS application-data bytes.
//
// Invariant: returns when >= 1 complete FIX frame is present in buf, OR when
// the deadline fires, OR when the byte budget is exceeded, OR on read error.
// "Complete frame" == Framer::feed returns at least one frame_view.
//
// [FR-014; E-2; data-model "Bounded first-frame read"]
inline asio::awaitable<fixpp::core::expected_t<std::size_t>> read_first_frame_bounded(
    fixpp::transport::Transport& transport, std::vector<std::byte>& buf,
    std::chrono::milliseconds deadline, std::size_t max_bytes) {
    using namespace std::chrono_literals;
    using fixpp::core::error;

    auto exec = co_await asio::this_coro::executor;
    asio::steady_timer timer{exec};
    timer.expires_after(deadline);

    bool timed_out = false;
    // 015 /simplify (Q-2) — the deadline must CANCEL the in-flight async_read_some,
    // not merely set a flag the loop checks between reads: a peer that completes the
    // TLS handshake then stalls would otherwise block the read forever (FR-014 /
    // SC-011). transport.cancel() aborts the pending read → the read-error arm below
    // returns transport_handshake_timeout.
    timer.async_wait([&timed_out, &transport](const std::error_code& ec) {
        if (!ec) {
            timed_out = true;
            transport.cancel();
        }
    });

    // Build a framer to detect frame boundaries. Capacity is max_bytes + 1 (not
    // max_bytes) to match the read clamp below (C1) — the loop admits up to one
    // byte over budget so a frame that completes exactly at the boundary can
    // still be found by framer.feed before the budget check rejects (research.md
    // D-1 / D-1a: at capacity max_bytes, the framer would reject the (max_bytes+1)th
    // byte before any parse, making the frame-vs-budget decision unreachable).
    fixpp::wire::pmr_carry_buffer carry{max_bytes + 1, std::pmr::new_delete_resource()};
    std::array<fixpp::wire::frame_view, 1> out_frames{};
    fixpp::wire::Framer framer;

    std::array<std::byte, 4096> read_buf{};
    while (!timed_out) {
        // C1 clamp: never request more than the remaining room up to
        // max_bytes + 1, so cumulative buf.size() can never exceed max_bytes + 1.
        std::size_t const room = (max_bytes + 1) - buf.size();
        std::size_t const want = std::min(read_buf.size(), room);

        auto read_r =
            co_await transport.async_read_some(std::span<std::byte>{read_buf.data(), want});
        if (!read_r.has_value()) {
            // Deadline (Q-2): the timer callback cancelled this read → the error
            // arm closes + reclaims (FR-014). The specific code is unobservable
            // here (no Session, no log surface) so it is not special-cased; the
            // between-reads deadline still returns transport_handshake_timeout below.
            timer.cancel();
            co_return std::unexpected(read_r.error());
        }
        std::size_t n = *read_r;
        buf.insert(buf.end(), read_buf.data(), read_buf.data() + n);

        // Feed ONLY the newly-read bytes into the stateful Framer — it accumulates
        // unconsumed bytes in `carry` across calls, so re-feeding the whole `buf`
        // would duplicate the already-carried prefix and corrupt a fragmented first
        // frame (a valid Logon split across TLS reads → rejected). Mirrors the
        // incremental feed in run_read_pump. [F-015-001]
        //
        // Frame-first: FR-007 requires exactly one budget decision point, and it
        // must not preempt a frame that already completed within budget (S3/S4).
        auto feed_r = framer.feed(std::span<const std::byte>{read_buf.data(), n}, carry,
                                  std::span<fixpp::wire::frame_view>{out_frames});
        if (!feed_r.has_value()) {
            timer.cancel();
            if (feed_r.error() == error::wire_frame_too_large)
                co_return std::unexpected(error::wire_frame_too_large);
            co_return std::unexpected(feed_r.error());
        }
        if (!feed_r->empty()) {
            // First complete frame available. Return its EXACT length so the caller
            // delivers ONLY the first frame (buf[0..len)) to on_inbound_frame and
            // carries any surplus (buf[len..], a coalesced next frame) into the
            // read-pump (F-015-002). buf accumulates raw bytes in arrival order, so
            // buf[0..len) is byte-for-byte the first frame the framer emitted.
            timer.cancel();
            co_return (*feed_r)[0].bytes().size();
        }

        // Single budget decision point (FR-007), strict `>` (exceeds, not
        // reaches — FR-014), evaluated AFTER framer.feed had its chance to find
        // a frame already complete within budget.
        if (buf.size() > max_bytes) {
            timer.cancel();
            co_return std::unexpected(error::wire_frame_too_large);
        }
    }
    co_return std::unexpected(error::transport_handshake_timeout);
}

}  // namespace fixpp::session::detail
