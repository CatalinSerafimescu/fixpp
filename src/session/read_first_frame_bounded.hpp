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
#include <asio/cancellation_type.hpp>
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
// default]].
//
// ⚠️ SINCE THE SWEEP FIX, THE RESET IS LOAD-BEARING FOR A SECOND REASON, and it
// is not the delivery one. The loop below READS `cancellation_state::cancelled()`
// as a durable record of "the join cancelled me". That field is ASSIGNED, not
// accumulated (asio/cancellation_state.hpp: `cancelled_ = in_filter_(in)`), so
// under a NARROWER in-filter a later emit whose filtered value is `none` would
// erase an already-recorded cancel — after which this loop would re-sleep on an
// arm the join had already retired, and hang. `enable_total_cancellation` passes
// everything, and nothing in asio emits `none` today, so it is unreachable as
// written. Narrow this filter and it stops being unreachable, with every cell
// still green. `Clock::sleep_until` is a NESTED co_await, so it shares this
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
// ⚠️ THE `catch (...)` IS LOAD-BEARING. IT WAS REMOVED ONCE, ON REVIEW, AND THAT
// WAS WRONG — the reasoning is kept because the argument for removing it is
// entirely plausible and will be made again.
//
// The case for removal: this arm completing normally makes `outcome.index() == 1`
// and the caller reject the connection as `transport_handshake_timeout`, so a
// `bad_alloc` is reported as a peer timeout. That mislabelling is REAL, and
// `system_clock_source::sleep_until` allocates a strand, a timer and a map node
// PER CALL on exactly the engine-scope fallback this caller lands on — so it is
// a concrete possibility, not a hypothetical.
//
// Why removal is nevertheless WORSE, from asio's own source: an arm that
// completes WITH an exception cancels nothing. asio/experimental/
// cancellation_condition.hpp, wait_for_one_success:
//
//     return e != no_error ? cancellation_type::none : cancel_type_;
//
// So if this arm throws, the group does not cancel the read — it simply keeps
// waiting on it WITH NO DEADLINE AT ALL, and awaitable_operators returns the
// read's own result, so the caller never learns the bound was dropped. That is
// FR-014/SC-011 — an unbounded first-frame read against an untrusted peer — and
// it is strictly worse than a misleading error enum on a rejected connection.
//
// ⚠️ AND IT IS NOT A REGRESSION THE CATCH INTRODUCED: the PRE-#377 arm had the
// SAME hole. `timer.async_wait(redirect_error(use_awaitable, ec))` absorbed error
// codes but not exceptions, so a throwing async_wait deleted the deadline there
// too, by the identical mechanism. The catch-all is an IMPROVEMENT on the old
// behaviour, not a widening of it — it converts "bound silently deleted" into
// "connection rejected", which is the safe direction.
//
// The residual is therefore accepted and disclosed rather than fixed: an
// allocation failure inside the deadline arm is reported as
// transport_handshake_timeout. Distinguishing it needs a new error value in a
// family the codebase pins contiguous, which is not this change's business.
inline asio::awaitable<void> await_deadline(fixpp::core::Clock& clock,
                                            fixpp::core::steady_time_point deadline) {
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
    for (;;) {
        try {
            co_await clock.sleep_until(deadline);
            co_return;  // the deadline was actually reached
        } catch (const std::system_error&) {  // NOLINT(bugprone-empty-catch) — the decision is
                                              // made below; there is nothing to do in the handler.
            // operation_aborted, and it does NOT say which of two very different
            // things happened. See the disambiguation immediately below.
        } catch (...) {  // NOLINT(bugprone-empty-catch) — see the note above the function.
            // Absorbed so this arm COMPLETES rather than throws. A throwing arm
            // cancels nothing (wait_for_one_success), which would leave the read
            // running with no deadline; completing rejects the connection instead.
            co_return;
        }

        // ── WHY THIS IS A LOOP, AND WHY THE CLOCK IS THE ORACLE (#377 round 2) ──
        // `operation_aborted` out of sleep_until has TWO causes and they need
        // opposite responses:
        //
        //   (a) the JOIN cancelled this arm — the read won, this arm must retire.
        //   (b) somebody called `Clock::cancel_sleeps()`, which is GLOBAL over the
        //       whole clock and sweeps EVERY registered sleeper.
        //
        // (b) is not exotic and it is not a teardown path. A Session in LogoutSent
        // that receives the peer's confirming 35=5 calls cancel_sleeps() to wake
        // its own logout wait (src/session/session.cpp, the LogoutSent case), and
        // absent a per-session clock_override that Session's clock IS this engine
        // clock (session.cpp: `effective_clock_ = cfg_.clock_override ? ... :
        // engine_.clock`). So one routine Logout on ANY session would sweep the
        // accept path's deadline here.
        //
        // Treating (b) as (a) is a SILENT DROPPED LOGON: this arm completes, the
        // join reports outcome.index() == 1, and the caller rejects a perfectly
        // healthy inbound connection as transport_handshake_timeout and closes it
        // without a log at that site.
        //
        // ⚠️ THIS IS A HAZARD #377 CREATED. Before it, the deadline was a private
        // asio::steady_timer built inside this helper and registered with no
        // Clock, so cancel_sleeps() could not reach it. Putting the deadline on
        // the shared engine clock is what made the accept path reachable from
        // every other session's traffic. Do not "simplify" this back to a single
        // sleep.
        //
        // The exception cannot tell (a) from (b) — both are operation_aborted —
        // so ask the two authorities that can. Re-sleeping targets the SAME
        // ABSOLUTE INSTANT, so this cannot push the deadline forward however many
        // times it is swept, and it still terminates AT the deadline.
        // ⚠️ CONDITION, not an invariant this code enforces: the sequence
        // throw -> read cancelled() -> read steady_now() -> re-enter sleep_until
        // is atomic with respect to the group's emit only because both run on the
        // SAME serialized executor — the accept-loop strand in production, a
        // single-threaded ioc.run() in the cells. An emit arriving from another
        // thread inside that window would be recorded in `cancelled_` but would
        // have no installed downstream slot to reach, and the fresh sleep_until
        // would not re-deliver it. asio already forbids emitting off the op's
        // executor, so this is not a new rule — but it is now a CORRECTNESS
        // dependency of this helper rather than only asio hygiene.
        auto cs = co_await asio::this_coro::cancellation_state;
        if (cs.cancelled() != asio::cancellation_type::none) {
            co_return;  // (a) the join cancelled us — retire, as before.
        }
        if (clock.steady_now() >= deadline) {
            co_return;  // the deadline genuinely elapsed; report it as a timeout.
        }
        // (b) a spurious sweep with time still on the clock. Re-arm to the same
        // instant and keep waiting — the connection is NOT dropped.
        //
        // ⚠️ THIS LOOP DOES NOT DELAY Engine::stop(), and that is an ORDERING
        // property worth stating because it is the obvious worry. stop()
        // total-cancels every loop in its STEP 1 (entry.session_cancel.emit),
        // whereas Session::close(terminal) — the other cancel_sleeps() caller —
        // runs in STEP 4, AFTER the join. So during teardown branch (1) has
        // already fired and this arm retires; a sweep cannot reach it first.
        // If that ordering ever changes, this loop is a place stop() could
        // start waiting, so re-derive it rather than trusting this note.
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
