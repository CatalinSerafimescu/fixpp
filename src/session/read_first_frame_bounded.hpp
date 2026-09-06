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
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/this_coro.hpp>
#include <chrono>
#include <cstddef>
#include <fixpp/core/clock.hpp>  // Clock::steady_now / sleep_until; steady_time_point
#include <fixpp/core/error.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/wire/framer.hpp>
#include <memory_resource>
#include <span>
#include <system_error>  // std::system_error — the shape Clock::sleep_until throws on cancel
#include <vector>

namespace fixpp::session::detail {

// Sleeps until the ABSOLUTE deadline `read_first_frame_bounded` computed exactly
// once, before its loop (FR-017 / D-1b).
//
// ── ARM-ONCE IS NOW A PROPERTY OF THE SIGNATURE, NOT A RULE TO OBEY (#377) ───
// This used to take an `asio::steady_timer&` armed by the caller with
// `expires_after`, and carried a prohibition: "MUST NOT call expires_after() —
// a re-arm here would silently remove the deadline with every existing test
// still green". A prohibition is only as good as the next editor reading it.
// Taking an absolute `steady_time_point` removes the thing being prohibited:
// re-sleeping to the SAME absolute instant is idempotent, so this arm cannot
// push the deadline forward no matter how many loop iterations call it. The
// re-arm mutant now has to be written one level up, where the deadline is
// computed — see the B6 note in read_first_frame_bounded_test.cpp, which is
// the cell that kills it.
//
// Resets to total cancellation FIRST (FR-005/FR-006/D-2): co_spawn's initial
// cancellation state is terminal-only, and `operator||` co_spawns each arm —
// without this reset, Engine::stop()'s cancellation_type::total would be
// silently swallowed by this arm [[feedback_asio_cospawn_total_cancellation_
// default]]. `Clock::sleep_until` is a NESTED co_await, so it shares this
// frame's cancellation state and inherits the reset; both shipped clocks then
// honour the per-op slot (mock_clock.cpp's `cs.assign`, and system_clock_source
// via `steady_timer::async_wait`), which is what lets the join's cancel of the
// losing arm actually retire it.
//
// The catch replaces the old `redirect_error` (D-3), which is not available
// here because `sleep_until` returns an awaitable rather than an async op.
// D-3's requirement is unchanged and is what it exists for: THIS ARM MAY NOT
// THROW ON THE NORMAL PATH, because `outcome.index()` is the join's sole
// discriminator. A bare `co_await clock.sleep_until(...)` throws
// operation_aborted on EVERY established connection — the read arm winning is
// the common case, and wait_for_one_success then cancels this arm — so an
// uncaught throw would make the normal path an exception.
//
// ⚠️ EXACTLY ONE HANDLER, AND THE ABSENCE OF `catch (...)` IS DELIBERATE. It was
// written with one, and that was a WIDENING rather than a port. The pre-#377 arm
// was `timer.async_wait(redirect_error(use_awaitable, ec))`, which absorbed every
// ERROR CODE and no EXCEPTION — a `bad_alloc` out of asio's own allocation
// propagated. A catch-all absorbs those too, after which this arm completes
// normally, `outcome.index() == 1`, and the caller rejects the connection with
// `transport_handshake_timeout`: an OOM silently relabelled as a peer timeout, a
// misdiagnosis the old code did not make. `system_clock_source::sleep_until`
// allocates a timer per call on the engine-scope fallback, so bad_alloc is a
// concrete possibility here, not a hypothetical. Catching only `system_error` is
// the faithful equivalent — it is the exception form of "an error code arrived".
// Do not re-add the catch-all.
//
// (session.cpp's logout-timeout sleep DOES carry both handlers. Do not
// generalise from it: its second handler absorbs into a noexcept window per
// FR-15, a constraint this arm does not have.)
inline asio::awaitable<void> await_deadline(fixpp::core::Clock& clock,
                                            fixpp::core::steady_time_point deadline) {
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
    try {
        co_await clock.sleep_until(deadline);
    } catch (const std::system_error&) {  // NOLINT(bugprone-empty-catch) — see D-3 above: the
                                          // join's outcome.index() is the control-flow decision,
                                          // not this arm's error. Deliberately no action.
        // Losing arm: operation_aborted, exactly what redirect_error absorbed.
        // Anything that is NOT a system_error propagates, as it did pre-#377.
    }
}

// ── Bounded first-frame read (FR-014 / E-2 / C1 steps 2-3) ──────────────────
// Reads raw bytes from an accepted (not-yet-TLS-handshaken, post-handshake) TCP
// transport into `buf` with a deadline. Returns the number of bytes read on
// success, or an error on timeout / over-budget / read-fail.
//
// Used AFTER async_handshake succeeds — we read TLS application-data bytes.
//
// Invariant: a complete frame ALWAYS wins over the budget — this returns as
// soon as >= 1 complete FIX frame is present in buf, checked BEFORE the budget
// decision on every iteration (FR-002/FR-007). The byte budget only fires when
// no frame is extractable from what has been read so far. Also returns on
// deadline fire or read error. "Complete frame" == Framer::feed returns at
// least one frame_view.
//
// [FR-014; E-2; data-model "Bounded first-frame read"]
[[nodiscard]] inline asio::awaitable<fixpp::core::expected_t<std::size_t>> read_first_frame_bounded(
    fixpp::transport::Transport& transport, std::vector<std::byte>& buf,
    fixpp::core::Clock& clock, std::chrono::milliseconds deadline, std::size_t max_bytes) {
    using fixpp::core::error;

    using namespace asio::experimental::awaitable_operators;

    // Resolved to an ABSOLUTE instant exactly ONCE, before the loop (FR-017 /
    // D-1b). await_deadline (above) re-sleeps to this same instant on every
    // iteration, which is idempotent — see its header for why arm-once is now
    // structural rather than a prohibition.
    //
    // ⚠️ THE RE-ARM MUTANT LIVES ON THIS LINE NOW. Moving this computation
    // inside the loop (`clock.steady_now() + deadline` per iteration) pushes
    // the deadline forward forever and is what B6 kills.
    //
    // `deadline` stays RELATIVE in the signature so every call site keeps its
    // literal (`5s`, `50ms`) and contracts/read_first_frame_bounded.md's P-
    // clauses still read as written; the clock is what makes it testable.
    fixpp::core::steady_time_point const abs_deadline = clock.steady_now() + deadline;

    // 015 /simplify (Q-2) — the deadline must CANCEL the in-flight async_read_some,
    // not merely set a flag the loop checks between reads: a peer that completes the
    // TLS handshake then stalls would otherwise block the read forever (FR-014 /
    // SC-011). Joining the read against await_deadline (FR-005/FR-006/D-2) aborts
    // the pending read the instant the deadline fires — parallel_group::async_wait
    // retires BOTH arms before the join completes, so no handler armed by this
    // call can outlive this coroutine's frame.
    //
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
    for (;;) {
        // C1 clamp: never request more than the remaining room up to
        // max_bytes + 1, so cumulative buf.size() can never exceed max_bytes + 1.
        std::size_t const room = (max_bytes + 1) - buf.size();
        std::size_t const want = std::min(read_buf.size(), room);

        // The join (D-2): parallel_group::async_wait completes only once BOTH
        // arms have retired, so the loser's handler (the read's cancel, or the
        // deadline's own wait) is always retired before this co_await returns —
        // no explicit sleep-cancel/transport.cancel() bookkeeping is needed on
        // any return path below (FR-005/FR-006). This holds for the Clock arm
        // (#377) on the same terms it held for the timer: the group cancels the
        // loser through its per-op slot, and both shipped clocks complete a
        // slot-cancelled sleep_until with operation_aborted rather than leaving
        // it parked. A Clock that IGNORED the slot would hang here instead —
        // that is the property to check when adding one, not a count.
        auto outcome =
            co_await (transport.async_read_some(std::span<std::byte>{read_buf.data(), want}) ||
                      await_deadline(clock, abs_deadline));
        if (outcome.index() == 1) {
            // Deadline arm won (D-3: outcome.index() is a sound discriminator
            // because neither arm throws — await_deadline absorbs, see its
            // catch handlers).
            co_return std::unexpected(error::transport_handshake_timeout);
        }
        auto read_r = std::get<0>(std::move(outcome));
        if (!read_r.has_value()) {
            // Read arm won with an error — e.g. the deadline's cancellation
            // raced the read and lost, surfacing as transport_read_cancelled
            // (FR-014). Not special-cased: the caller (no Session, no log
            // surface here) treats every read-arm error uniformly.
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
            // Propagated verbatim, including a framer-sourced wire_frame_too_large.
            // (088 /simplify: the previous form special-cased that enum and then
            // returned the identical value on both arms — a branch that made no
            // distinction, while B4's own comment discusses exactly the
            // framer-vs-budget provenance of this error. A reader checking the code
            // for that distinction found a branch that did not make one.)
            co_return std::unexpected(feed_r.error());
        }
        if (!feed_r->empty()) {
            // First complete frame available. Return its EXACT length so the caller
            // delivers ONLY the first frame (buf[0..len)) to on_inbound_frame and
            // carries any surplus (buf[len..], a coalesced next frame) into the
            // read-pump (F-015-002). buf accumulates raw bytes in arrival order, so
            // buf[0..len) is byte-for-byte the first frame the framer emitted.
            co_return (*feed_r)[0].bytes().size();
        }

        // Single budget decision point (FR-007), strict `>` (exceeds, not
        // reaches — FR-014), evaluated AFTER framer.feed had its chance to find
        // a frame already complete within budget.
        if (buf.size() > max_bytes) {
            co_return std::unexpected(error::wire_frame_too_large);
        }
    }
}

}  // namespace fixpp::session::detail
