// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/session/seqnum_manager.cpp
//
// fixpp::session::SeqnumManager — inbound/outbound sequence-number counter
// bodies (005 US2 / T031 / data-model E3).
//
// Anchors:
//   data-model.md E3 / E4 / Invariants I-2 / I-4 / I-8
//   research.md D-7 ([2f §7.3] defence-in-depth async_mutex pattern)
//   spec FR-008/009/010; [FIX-SL §4.1]
//   include/fixpp/session/seqnum_manager.hpp (interface)
//   include/fixpp/core/sync/async_mutex.hpp (fixpp::sync::async_mutex)
//
// async_mutex usage per [2f §7.3]:
//   auto lk = co_await mutex_.async_lock();
//   if (!lk) { ... handle sync_lock_aborted / sync_lock_drained ... }
//   // hold lk; RAII unlocks on scope exit.
//
// Discipline (I-7, Karpathy no-alloc hot path):
//   No std::string, no std::vector, no std::function in the check/assign paths.
//   The async_mutex fast path (uncontended CAS) does not suspend on the session strand.
//   Per-thread recycler handles the cancellation-handler closure (E-4 pattern).

#include <asio/awaitable.hpp>
#include <asio/cancellation_type.hpp>  // NOLINT(misc-include-cleaner) — IWYU: operator! via cancellation_slot
#include <coroutine>  // NOLINT(misc-include-cleaner) — IWYU: coroutine_handle/suspend_never via awaitable machinery
#include <expected>
#include <fixpp/core/error.hpp>
#include <fixpp/core/sync/async_mutex.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/seqnum_manager.hpp>
#include <utility>

namespace fixpp::session {

// ── check_inbound ─────────────────────────────────────────────────────────────
//
// Acquire the mutex, compare seq vs next_inbound_, advance if in-seq.
// Return error variants without advancing on mismatch.
//
// Error variants:
//   session_seqnum_too_low (69)          — seq < next_inbound_ (no PossDup — S-010 OOS)
//   session_test_request_unanswered (74) — seq > next_inbound_ (too-high stand-in;
//     slot 70 session_seqnum_gap_unrecoverable deleted per 013 T006a;
//     013 Phase 3 T026: session.cpp intercepts too-high BEFORE check_inbound and
//     routes via reconnect_fsm_.enter_awaiting_resend() per FR-009)
//
// The mutex serialises both inbound and outbound counter operations (D-7).
// Under the per-session-strand discipline the fast-path CAS always succeeds;
// no suspension in the common case.

asio::awaitable<fixpp::core::expected_t<void>> SeqnumManager::check_inbound(seqnum_t seq) noexcept {
    using fixpp::core::error;

    // Acquire mutex — RAII scoped_lock, unlocks on scope exit.
    auto lk_result = co_await mutex_.async_lock();
    if (!lk_result) {
        // Mutex was drained (session teardown) or cancelled.
        // Surface as session_already_closed (session is terminating).
        co_return std::unexpected(error::session_already_closed);
    }
    // lk holds the scoped_lock; mutex is held until lk goes out of scope.
    auto lk = std::move(*lk_result);

    if (seq < next_inbound_) {
        // Too-low: session-fatal per [FIX-SL §4.1]. Counter NOT advanced.
        co_return std::unexpected(error::session_seqnum_too_low);
    }

    if (seq > next_inbound_) {
        // Too-high: session_seqnum_too_high (slot 120, 014 FR-016 / E-4).
        // session.cpp intercepts too-high BEFORE calling check_inbound in
        // Active state (via reconnect_fsm_ per 013 Phase 3 FR-009). This
        // path is reached for LogonSent / LogonReceived states where too-high
        // is still session-fatal. Slot 70 session_seqnum_gap_unrecoverable
        // deleted per 013 T006a; slot 74 session_test_request_unanswered
        // was the stand-in — replaced here by the semantically-correct slot.
        // Zero behavioural change: all 3 callers (session.cpp) discard the
        // returned code and disconnect (I-8; contracts/error_slots.hpp).
        co_return std::unexpected(error::session_seqnum_too_high);
    }

    // In-sequence: advance exactly by 1 (I-2 zero drift).
    ++next_inbound_;
    co_return fixpp::core::expected_t<void>{};
}

// ── assign_outbound ───────────────────────────────────────────────────────────
//
// Acquire the mutex, check for overflow, assign next_outbound_, advance.
//
// Returns the ASSIGNED seq (the value to stamp on the outbound message).
// After this call, next_outbound_ == assigned + 1.
//
// Overflow: if next_outbound_ == seqnum_max, the increment would wrap.
// Per I-8: detect before assigning, return store_seqnum_overflow (slot 60).
// The counter is NOT advanced on overflow.

asio::awaitable<fixpp::core::expected_t<seqnum_t>> SeqnumManager::assign_outbound() noexcept {
    using fixpp::core::error;

    auto lk_result = co_await mutex_.async_lock();
    if (!lk_result) {
        co_return std::unexpected(error::session_already_closed);
    }
    auto lk = std::move(*lk_result);

    // Overflow check: at seqnum_max the next value would need to be
    // seqnum_max+1 which overflows. Detect at seqnum_max itself.
    if (next_outbound_ == seqnum_max) {
        // Session-fatal, no wrap (I-8). Reuse [2e §6.7] store_seqnum_overflow.
        co_return std::unexpected(error::store_seqnum_overflow);
    }

    const seqnum_t assigned = next_outbound_;
    ++next_outbound_;  // advance by exactly 1 (I-2)
    co_return fixpp::core::expected_t<seqnum_t>{assigned};
}

// ── reset_to_one ──────────────────────────────────────────────────────────────
//
// Acquire the mutex, reset both counters to seqnum_min (1).
// Called on a successful ResetSeqNumFlag(141)=Y handshake (FR-017:150).
// Production path — NOT FIXPP_TEST_HOOKS-gated.

asio::awaitable<fixpp::core::expected_t<void>> SeqnumManager::reset_to_one() noexcept {
    auto lk_result = co_await mutex_.async_lock();
    if (!lk_result) {
        co_return std::unexpected(fixpp::core::error::session_already_closed);
    }
    auto lk = std::move(*lk_result);

    next_inbound_ = seqnum_min;
    next_outbound_ = seqnum_min;
    co_return fixpp::core::expected_t<void>{};
}

// ── set_next_inbound ────────────────────────────────────────────────────────
//
// Acquire the mutex, set next_inbound_ to n. Called when an inbound
// SequenceReset(35=4) (GapFill or Reset mode) carries NewSeqNo(36) > expected.
// Production path. Only the inbound counter moves.

asio::awaitable<fixpp::core::expected_t<void>> SeqnumManager::set_next_inbound(
    seqnum_t n) noexcept {
    auto lk_result = co_await mutex_.async_lock();
    if (!lk_result) {
        co_return std::unexpected(fixpp::core::error::session_already_closed);
    }
    auto lk = std::move(*lk_result);

    next_inbound_ = n;
    co_return fixpp::core::expected_t<void>{};
}

}  // namespace fixpp::session
