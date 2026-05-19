// SPDX-License-Identifier: AGPL-3.0-or-later
//
// specs/006-async-mutex/contracts/async_mutex_awaiter.hpp
//
// ─────────────────────────────────────────────────────────────────────────────
// SHAPE ORACLE (NOT THE BUILD HEADER)
// This file is a planning-phase contract artifact.
// It specifies the exact layout and protocol that the implementation MUST match.
// It is NOT compiled into the library.
//
// Build header target: include/fixpp/core/sync/async_mutex.hpp
//   (detail::async_mutex_awaiter is defined in the fixpp::sync::detail
//   namespace within the main async_mutex header, after the async_mutex class)
// ─────────────────────────────────────────────────────────────────────────────
//
// Source of truth: .specify/2f-async-mutex.md v1.5 §4.2 / §4.2.1 / §4.2.2 /
//                  §4.2.3, AS AMENDED BY v1.6 errata E-2 / E-3 / E-4.
//
// ── E-2 (§4.2, v1.6 — THE WAITER IS TWO OBJECTS, NOT ONE) ──
//   E-1 made async_mutex_awaiter frame-local while §4.2 kept it as the
//   intrusive node linked through state_/next_drain_head_ — a guaranteed
//   use-after-free under prompt cancel-resume of an interior waiter (the
//   frame, hence its next_/phase_/result, is destroyed while state_/
//   next_drain_head_ still point at it). E-2 splits the waiter:
//     - detail::async_mutex_awaiter — stays FRAME-LOCAL + HALO-eligible; it
//       is NO LONGER the intrusive node. Keeps mutex_, the cancellation
//       slot_, the inline completion-handler slot_storage_ + its E-1
//       invoke_fn_/destroy_fn_ trampolines (E-1: the design's coro_
//       continuation is REPLACED by the composed-op completion handler —
//       NO coro_ field), and a detail::waiter_record* record_ attachment
//       pointer.
//     - detail::waiter_record — the STABLE intrusive node (below). state_ /
//       next_drain_head_ store detail::waiter_record*. Carries next_, the
//       phase_ CAS atom, the OWNED terminal result_ (not a pointer),
//       attached_awaiter_, inline exec_storage_ (bound-executor SBO), and a
//       refcount_ reclamation token (single-shot; invariant I-32; sound
//       because of the single-drainer invariant — no hazard ptrs / epoch GC).
//   Mirrors data-model.md E2 (awaiter) + E2a (waiter_record) verbatim.
//
// ── E-3 (§4.6.2, v1.6) ──  2f waiter resumption is ALWAYS post-ed to the
//   bound executor — never inline-dispatched — regardless of completion_policy
//   or running_in_this_thread() (re-entrant inline resume = TSan-diagnosed
//   heap-UAF). on_cancel / the drain winner schedule a post hop, never inline.
//
// ── E-4 (§4.3.4, v1.6, source-verified asio 1.36.0) ──  cancellation_slot has
//   NO allocator-binding hook; detail::slot_allocator is NOT bound to slot_
//   (no asio::bind_allocator). The cancellation closure lives in asio's
//   per-thread recycler (zero global new/delete only in steady state, post
//   per-thread warm-up). slot_storage_ holds ONLY the asio COMPLETION handler
//   (E-1, unchanged); it does NOT back the cancellation closure.
//
// async_mutex_awaiter — the frame-local suspension unit for
// co_await m.async_lock(). Private to fixpp::sync::detail; not part of the
// user surface beyond async_lock's return-shape contract.
//
// HALO eligibility: ≤ 96 B layout (v1.1 budget). alignas(8) so the LIFO
// state_ encoding's low-bit `not_locked` sentinel is distinguishable from
// any real waiter_record pointer (E-2: the sentinel discriminates against
// waiter_record*, not the awaiter).
//
// Three-state waiter_phase enum (v1.1 collapse from v1.0's four states —
// RC-A close). E-2: phase_ now lives on detail::waiter_record:
//   queued    = 0  — pushed onto LIFO or spliced into next_drain_head_;
//                    still cancellable.
//   granted   = 1  — drain CAS-granted ownership; await_resume returns guard.
//                    Terminal.
//   cancelled = 2  — cancellation handler or reaper CAS-acquired; await_resume
//                    returns unexpected{sync_lock_aborted}. Terminal.
//
// v1.4 CAS-then-publish protocol: only the CAS winner writes record->result_;
// the loser observes terminal phase and does NOT touch record->result_.
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <array>
#include <atomic>
#include <coroutine>
#include <cstddef>
#include <cstdint>

// Build header includes these properly; this oracle forward-declares the
// minimum so the EXACT layout + protocol is stated literally, including the
// normative slot_ / result_ fields and the real await_resume() /
// on_cancel() signatures (RC#2 oracle-fidelity policy — this file claims an
// "exact layout and protocol that the implementation MUST match" at the
// banner, so it MUST carry the real fields that drive the §1.1 byte budget):
// #include <asio/awaitable.hpp>
// #include <asio/cancellation_slot.hpp>
// #include <asio/cancellation_type.hpp>
// #include "fixpp/core/error.hpp"
#include <expected>

namespace asio {
class cancellation_slot;
enum class cancellation_type : unsigned int;  // ASIO's underlying type
}  // namespace asio
namespace fixpp::core { enum class error : int; }

namespace fixpp::sync {

class async_mutex;
class async_lock_guard;
// expected_t<T> = fixpp::core::expected_t<T> = std::expected<T, core::error>;
// declared (not erased) so result_ and await_resume() carry their real types.
template <class T>
using expected_t = std::expected<T, fixpp::core::error>;

namespace detail {

// Phase enum DEFINITION (Codex C-P3-7 close — order-valid header sketch).
// Forward-declared in the namespace block above async_mutex (§4.1); the
// complete definition lives here, after the async_mutex class definition.
// v1.1 collapses the v1.0 four-state machine to three states (RC-A close).
enum class waiter_phase : std::uint8_t {
    queued    = 0,  // pushed onto LIFO (state_) or spliced into
                    //   next_drain_head_ (residual FIFO — RC-A); still
                    //   cancellable. The drain skips `cancelled` waiters and
                    //   CASes the first `queued` waiter to `granted`.
    granted   = 1,  // unlock()'s drain (or fast-path acquire) has granted
                    //   ownership; await_resume returns the guard. Terminal.
    cancelled = 2,  // cancellation handler (or cancel_and_drain reaper)
                    //   CAS-acquired this waiter; await_resume returns
                    //   unexpected{sync_lock_aborted}. Terminal.
};

// detail::waiter_record — THE STABLE INTRUSIVE NODE (E-2; data-model.md E2a).
// alignas(8): the LIFO state_ encoding's low-bit `not_locked` sentinel
//             requires waiter_record pointers to be >= 8-byte-aligned.
// Created ONLY on the contended path (uncontended fast path: none — the
// awaiter's record_ is nullptr). Lifetime is INDEPENDENT of the coroutine
// frame; reclaimed single-shot via refcount_ (invariant I-32). Drawn from
// async_mutex::waiter_pool_ when mr==nullptr (zero global new), else from mr.
//
// Layout (field order is normative; implementation MUST preserve it):
struct alignas(8) waiter_record {
    async_mutex*                   mutex_;     // back-pointer to the mutex.
    waiter_record*                 next_;      // intrusive link — reused by
                                               //   both state_'s LIFO chain
                                               //   and next_drain_head_'s
                                               //   FIFO chain (RC-A).
    std::atomic<waiter_phase>      phase_;     // RC-A 3-state machine; the
                                               //   arbitration atom for unlock
                                               //   drain vs. cancellation
                                               //   handler (E-2: lives here).
    expected_t<async_lock_guard>   result_;    // OWNED terminal result (NOT a
                                               //   pointer — E-2). Written by
                                               //   the phase_ CAS winner only
                                               //   (v1.4 CAS-then-publish);
                                               //   read by await_resume via
                                               //   the awaiter's record_.
                                               //   Stable across coroutine-
                                               //   frame destruction.
    std::atomic<async_mutex_awaiter*> attached_awaiter_;  // frame-local
                                               //   awaiter consuming this
                                               //   node; cleared at
                                               //   await_resume. Non-null ⇒
                                               //   one refcount edge (I-32).
    /* alignas(max) */ std::array<std::byte, /*N*/ 32> exec_storage_;
                                               // E-2 Gap-B: inline bound-
                                               //   executor SBO ([2d §4.8]
                                               //   session_executor). static_
                                               //   assert enforces fit;
                                               //   overflow ⇒ unexpected{
                                               //   sync_lock_alloc_failed}.
                                               //   NO heap-allocating
                                               //   any_io_executor. (Exact N
                                               //   is an impl detail; the
                                               //   no-type-erasure-heap
                                               //   contract is normative.)
    std::atomic<std::uint32_t>     refcount_;  // single-shot reclamation
                                               //   token (I-32). Edges: +1
                                               //   creator; one transferred
                                               //   in-lists membership ref
                                               //   per state_⇄next_drain_head_
                                               //   splice; +1 per scheduled
                                               //   prompt-resume; +1 while
                                               //   attached_awaiter_!=nullptr.
                                               //   fetch_sub(1,acq_rel)==1
                                               //   reclaims to waiter_pool_/mr.
};

// async_mutex_awaiter — the FRAME-LOCAL suspension unit (E-2: NOT the node).
// alignas(8) retained for layout discipline; the LIFO sentinel discriminates
// against waiter_record* (above), not against the awaiter.
//
// Layout (field order is normative; implementation MUST preserve it):
struct alignas(8) async_mutex_awaiter {
    async_mutex*               mutex_;        // back-pointer to the mutex.
    waiter_record*             record_;       // E-2 (new): the stable node
                                              //   (waiter_record) this awaiter
                                              //   is attached to on the
                                              //   contended path; nullptr on
                                              //   the uncontended fast path.
                                              //   Intrusive next_/phase_/owned
                                              //   result_ live on *record_.
    asio::cancellation_slot    slot_;         // NORMATIVE field (§4.2 line
                                              //   743). Registered at
                                              //   await_suspend. E-4: NOT
                                              //   asio::bind_allocator-wrapped
                                              //   (no asio hook); closure is
                                              //   owned by asio's per-thread
                                              //   recycler.
    alignas(8) std::array<std::byte, 32> slot_storage_;
                                              // E-1: inline buffer for the asio
                                              //   COMPLETION handler. The
                                              //   design's `coro_` stored
                                              //   continuation is REPLACED by
                                              //   the composed-operation
                                              //   completion handler stored
                                              //   here via placement-new
                                              //   (store_handler) — there is
                                              //   NO coro_ field. E-4: it does
                                              //   NOT back the cancellation
                                              //   closure (asio's per-thread
                                              //   recycler does). §9 seam #21
                                              //   verifies slot_allocator's
                                              //   waiter_record storage cases.

    // E-1 completion-handler type-erasure trampolines into slot_storage_.
    using invoke_fn_t  = void(*)(void* storage,
                                 expected_t<async_lock_guard> result) noexcept;
    using destroy_fn_t = void(*)(void* storage) noexcept;
    invoke_fn_t  invoke_fn_;                   // E-1: type-erased invoke into
                                               //   slot_storage_; replaces the
                                               //   retired coro_.resume().
    destroy_fn_t destroy_fn_;                  // E-1: type-erased destroy of
                                               //   the in-buffer handler.

    // Awaitable protocol:

    // await_ready — uncontended fast path (v1.2 RC-B + v1.3 RC-α close).
    // Awaitable-factory pre-step (NEW v1.3 RC-α): the factory increments
    //   mutex_->active_acquirers_count_.fetch_add(1, acq_rel) BEFORE this call.
    // Step 1: draining_.load(acquire) — if true, fast-fail with
    //   sync_lock_drained, decrement active_acquirers_count_ (RC-α
    //   decrement-point #1), return true. (No waiter_record on this path —
    //   record_ stays nullptr; E-2.)
    // Step 2: state_.compare_exchange_strong(not_locked → locked_no_waiters,
    //   acquire/relaxed). On success: increment active_holders_count_ (winner-
    //   only, post-CAS — RC-α), decrement active_acquirers_count_ (RC-α
    //   decrement-point #2), return true (uncontended — no waiter_record).
    //   On failure: return false.
    bool await_ready() noexcept;

    // await_suspend — enqueue + slot-register + phase init (v1.3 RC-α/RC-B;
    // E-2-amended).
    // Step 1: draining_.load(acquire) defense-in-depth check (fast-fail if
    //   draining_ landed between await_ready steps 1 and 2); decrement
    //   active_acquirers_count_ (RC-α decrement-point #3a), resume inline.
    // Steps 2–6 (E-2): acquire a detail::waiter_record (from
    //   mutex_->waiter_pool_ when mr==nullptr — zero global new — else from
    //   mr), attach record_; store the composed-operation COMPLETION handler
    //   (derived from h) via placement-new into slot_storage_ — store_handler
    //   sets invoke_fn_/destroy_fn_ (E-1: replaces the retired `coro_ = h`);
    //   init record_->phase_ = queued
    //   (relaxed); recover cancellation_state; register on_cancel on slot_
    //   (E-4: NO asio::bind_allocator — no asio hook; the cancellation
    //   closure lives in asio's per-thread recycler); LIFO push CAS retry of
    //   record_ (release/acquire). On CAS-success: decrement
    //   active_acquirers_count_ (RC-α decrement-point #3b).
    // Step 7: if state_ transitions to not_locked mid-push, CAS to
    //   locked_no_waiters, decrement active_acquirers_count_ (decrement-point
    //   #3c), increment active_holders_count_, resume inline.
    void await_suspend(std::coroutine_handle<> h) noexcept;

    // await_resume — read result via record_, clear slot.
    // record_->phase_.load(acquire): granted → return async_lock_guard
    //   {mutex_}; cancelled → return record_->result_ (unexpected
    //   {sync_lock_aborted} or unexpected{sync_lock_drained}). Clears slot_
    //   via slot_.clear() and detaches from record_ (drops the
    //   attached_awaiter_ refcount edge — I-32). On the uncontended fast
    //   path (record_ == nullptr) returns the engaged guard directly.
    // Cancellation-after-resume is a no-op: slot is cleared, and a stale
    // cancel CAS fails because record_->phase_ is already terminal; the node
    // is kept alive by refcount_ until the scheduled resumption runs (E-2).
    // EXACT signature per §4.2 (line 753) — literal, NOT omitted.
    expected_t<async_lock_guard> await_resume() noexcept;

    // on_cancel — invoked from the cancellation_slot handler.
    // CAS record_->phase_: queued → cancelled (acq_rel/acquire) — E-2: the
    //   CAS atom is on the STABLE waiter_record (UAF-safe; the node, not the
    //   frame, is stable). On CAS-success (winner): write record_->result_ =
    //   unexpected{sync_lock_aborted}, then POST the resumption on the bound
    //   executor (E-3: ALWAYS post, never inline-dispatch — re-entrant inline
    //   resume = TSan-diagnosed heap-UAF), holding a resumer refcount edge
    //   (I-32) until the posted completion runs. On CAS-failure (drain won):
    //   no-op (waiter already granted). EXACT parameter type per §4.5 —
    //   asio::cancellation_type, NOT an int placeholder.
    void on_cancel(asio::cancellation_type type) noexcept;
};

// Compile-time alignment invariant — E-2: placed AFTER the detail::
// waiter_record definition (the LIFO sentinel discriminates against
// waiter_record*, and alignof on an incomplete class is ill-formed).
// Implementation MUST include this assertion after the struct definition.
//
// static_assert(alignof(waiter_record) >= 8,
//               "fixpp::sync: detail::waiter_record must be 8-byte-aligned "
//               "so the low-bit `not_locked` sentinel is distinguishable "
//               "from a real waiter_record pointer.");

}  // namespace detail
}  // namespace fixpp::sync

// ─────────────────────────────────────────────────────────────────────────────
// Field-layout sizing reference — E-2-amended (≤ 96 B budget; matches
// data-model.md E2/E2a). The intrusive node's fields (next_/phase_/owned
// result_/attached_awaiter_/exec_storage_/refcount_) moved OFF the
// frame-local awaiter onto detail::waiter_record (stable node), so the
// frame-local awaiter's ≤ 96 B HALO budget has MORE headroom, not less.
//
// detail::async_mutex_awaiter (FRAME-LOCAL, HALO-eligible):
//   async_mutex*               mutex_         :  8 B
//   waiter_record*             record_        :  8 B   (E-2: replaces the
//                                                       removed next_/phase_/
//                                                       result_ trio)
//   asio::cancellation_slot    slot_          : ≈ 16 B (ASIO impl-defined)
//   std::array<std::byte,32>   slot_storage_  : 32 B   (E-1 COMPLETION
//                                                       handler only; E-4: not
//                                                       the cancel closure;
//                                                       REPLACES the retired
//                                                       8 B coro_)
//   invoke_fn_t                invoke_fn_     :  8 B   (E-1 type-erased
//                                                       invoke trampoline)
//   destroy_fn_t               destroy_fn_    :  8 B   (E-1 type-erased
//                                                       destroy trampoline)
//   ─────────────────────────────────────────────────────────
//   Raw total                                 : 8+8+16+32+8+8 = 80 B
//   With alignas(8) trailing padding          : ≈ 80 B
//   Published ceiling                         : ≤ 96 B (extra E-2 headroom)
//
// detail::waiter_record (STABLE node — NOT in the HALO budget; pool/mr-backed):
//   async_mutex*               mutex_, waiter_record* next_,
//   std::atomic<waiter_phase>  phase_, expected_t<...> result_ (OWNED),
//   std::atomic<async_mutex_awaiter*> attached_awaiter_,
//   inline exec_storage_ (bound-executor SBO, [2d §4.8]),
//   std::atomic<std::uint32_t> refcount_ (I-32).
// ─────────────────────────────────────────────────────────────────────────────
