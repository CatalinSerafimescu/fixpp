// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/core/sync/async_mutex.hpp
//
// Awaitable mutex `fixpp::sync::async_mutex`.
//
// ─────────────────────────────────────────────────────────────────────────────
// BSL-1.0 algorithm attribution (per [const §V.4]):
//
//   The lock-free state encoding (std::atomic<uintptr_t> state_ with
//   not_locked / locked_no_waiters sentinels, LIFO waiter-list pointer
//   in the high bits, and the acquire / unlock CAS protocol) is derived
//   from the public-domain / BSL-1.0-licensed design published by
//   Lewis Baker in cppcoro (https://github.com/lewissbaker/cppcoro) and
//   independently described in the avast/asio-mutex repository
//   (https://github.com/avast/asio-mutex). The fixpp implementation
//   extends that algorithm with:
//     - a mutex-owned residual FIFO (next_drain_head_) replacing the
//       awaiter-owned residual_ field (RC-A);
//     - a three-state per-waiter phase machine (RC-A);
//     - strand-local single-pass reap with in_flight_resumers_ barrier (048);
//     - active_holders_count_ epoch counter (RC-α); and
//     - a PMR-aware slot_allocator for the cancellation handler closure
//       (RC-C).
//   The algorithm core (LIFO push / exchange-based drain / FIFO grant) is
//   due to Lewis Baker / cppcoro; all post-RC additions are original work.
// ─────────────────────────────────────────────────────────────────────────────
//
// Design anchor: .specify/2f-async-mutex.md v1.6 (errata E-1..E-5)
// Data model:    specs/048-async-mutex-strand-reap/data-model.md
// Contracts:     specs/048-async-mutex-strand-reap/contracts/async_mutex-contract.md
//                specs/006-async-mutex/contracts/async_mutex_awaiter.hpp
//                specs/006-async-mutex/contracts/async_lock_guard.hpp
//                specs/006-async-mutex/contracts/completion_policy.hpp
//
// Erratum E-1 (2026-05-18): The async_mutex_awaiter is a frame-local variable
// inside async_lock()'s own coroutine frame — NOT separately heap-allocated via
// global operator new. The asio completion handler produced by use_awaitable is
// stored via placement-new into the awaiter's inline slot_storage_ buffer (32 B).
// This achieves zero global heap allocation on both the uncontended and contended
// paths when mr==nullptr and HALO fires (§4.3.4 case 1).
//
// Erratum E-5 (048 — strand-local reap): cancel_and_drain() is narrowed to the
// strand-serialised contract its two consumers actually use. The cross-thread
// convergence machinery (drain_latch_state / concurrent_channel / Dekker
// handshake / active_acquirers_count_ / quiescence park) is removed. The new
// terminal condition is: (active_holders_count_==0) AND
// (in_flight_resumers_==0) AND (both lists empty in one pass). The drain is
// uninterruptible (disable_cancellation). Reentrant callers await
// draining_complete_ then return the terminal result. See data-model.md.

#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <memory>
#include <memory_resource>
#include <new>
#include <type_traits>
#include <utility>

// ASIO — standalone asio/1.36.0 (Conan dep).
#include <asio/any_io_executor.hpp>
#include <asio/as_tuple.hpp>
#include <asio/async_result.hpp>
#include <asio/awaitable.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_state.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/dispatch.hpp>
#include <asio/error.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>

#include "fixpp/core/error.hpp"

namespace fixpp::sync {

// ─────────────────────────────────────────────────────────────────────────────
// T007: E6 — completion_policy enum
// Source: [2f §4.1], data-model E6, contracts/completion_policy.hpp
// ─────────────────────────────────────────────────────────────────────────────

// Per-mutex completion policy. Immutable after construction.
// Governs the inline-vs-post behaviour of unlock()'s drain handoff.
enum class completion_policy : std::uint8_t {
    dispatch = 0,  // ASIO dispatch: inline iff running_in_this_thread(), else post.
                   // Default; matches [2d §7.4] surface.
    post = 1,      // Always post through the bound executor (one hop per resume).
};

// expected_t alias — mirrors fixpp::core::expected_t<T>.
template <class T>
using expected_t = std::expected<T, fixpp::core::error>;

// Forward declarations (full definitions follow below).
class async_mutex;
class async_lock_guard;

namespace detail {

// ─────────────────────────────────────────────────────────────────────────────
// T008: E2 phase enum — waiter_phase (three-state, RC-A v1.1 collapse)
// Source: [2f §4.2], data-model E2, contracts/async_mutex_awaiter.hpp
// ─────────────────────────────────────────────────────────────────────────────
enum class waiter_phase : std::uint8_t {
    queued = 0,     // pushed onto LIFO (state_) or spliced into next_drain_head_;
                    //   still cancellable.
    granted = 1,    // drain CAS-granted ownership; await_resume returns guard.
                    //   Terminal.
    cancelled = 2,  // cancellation handler (or reaper) CAS-acquired this waiter;
                    //   await_resume returns unexpected{sync_lock_aborted}. Terminal.
};

// ─────────────────────────────────────────────────────────────────────────────
// 058 T004 (research.md D-1; data-model.md) — generation-tagged packed
// free-list head: `{generation:54, slot_index:10}` packed into a u64.
// `slot_index == free_list_empty_index` (0x3FF) marks an empty list. Shared,
// bit-identical, by both the pop (async_lock) and the push (release_ref) so
// the encoding cannot drift between the two call sites.
// ─────────────────────────────────────────────────────────────────────────────
inline constexpr std::uint32_t free_list_empty_index = 0x3FFU;

struct free_list_head {
    std::uint64_t generation;
    std::uint32_t slot_index;
};

constexpr std::uint64_t pack_free_list_head(std::uint64_t generation,
                                            std::uint32_t slot_index) noexcept {
    return (generation << 10) | (slot_index & 0x3FFU);
}

constexpr free_list_head unpack_free_list_head(std::uint64_t packed) noexcept {
    return free_list_head{.generation = packed >> 10,
                          .slot_index = static_cast<std::uint32_t>(packed & 0x3FFU)};
}

// Forward declaration of slot_allocator (full skeleton below).
class slot_allocator;

// Forward declaration of async_mutex_awaiter.
// Full definition appears AFTER async_lock_guard (since async_mutex_awaiter
// references expected_t<async_lock_guard> in its function pointer types, and
// async_lock_guard must be complete for that instantiation).
struct async_mutex_awaiter;
struct waiter_record;

}  // namespace detail

// ─────────────────────────────────────────────────────────────────────────────
// T013: E1 — async_mutex skeleton
// Source: [2f §4.1], [2f §6.2], data-model E1, contracts/async_mutex.hpp
// ─────────────────────────────────────────────────────────────────────────────

class async_mutex {
public:
    // ─────────────────────────────────────────────────────────────────────────
    // Constructors
    // ─────────────────────────────────────────────────────────────────────────

    // Default: unlocked mutex with dispatch completion policy.
    // constexpr, noexcept, no executor dependency ([arch §5.5]).
    constexpr async_mutex() noexcept = default;

    // Explicit completion policy.
    explicit constexpr async_mutex(completion_policy cp) noexcept : policy_(cp) {}

    // Non-copyable, non-movable.
    async_mutex(async_mutex const&) = delete;
    async_mutex(async_mutex&&) = delete;
    async_mutex& operator=(async_mutex const&) = delete;
    async_mutex& operator=(async_mutex&&) = delete;

    // ─────────────────────────────────────────────────────────────────────────
    // Destructor — RC#3 fix: std::terminate() precondition.
    // Fires in BOTH debug AND release if the mutex is held, waiters are
    // present, OR any resumer is in-flight (058 T017 — research.md D-3).
    // Callers MUST drain via cancel_and_drain() before destruction.
    // T050 (US3) finalizes the destructor; US1 checks only the state_ sentinel;
    // 058 T017 adds the in_flight_resumers_ term.
    // ─────────────────────────────────────────────────────────────────────────
    ~async_mutex() noexcept(false);

    // ─────────────────────────────────────────────────────────────────────────
    // Primary acquire surface
    // ─────────────────────────────────────────────────────────────────────────

    // async_lock — acquire the mutex asynchronously.
    // EXACT signature per [2f §4.1] lines 505-506, contracts/async_mutex.hpp.
    // mr == nullptr → embedded awaiter (HALO-eligible); mr != nullptr → PMR.
    //
    // Erratum E-1 conformance: async_lock is itself an asio::awaitable<>
    // coroutine. The async_mutex_awaiter is a local variable in this
    // coroutine's frame — NOT separately heap-allocated via global operator new.
    [[nodiscard]] asio::awaitable<expected_t<async_lock_guard>> async_lock(
        std::pmr::memory_resource* mr = nullptr) noexcept;

    // ─────────────────────────────────────────────────────────────────────────
    // Drain primitive
    // ─────────────────────────────────────────────────────────────────────────

    // cancel_and_drain — drain the mutex of all current and future acquisitions.
    // EXACT signature per [2f §4.1] lines 579-580, contracts/async_mutex.hpp.
    //
    // NARROWED contract (Erratum E-5 / 048):
    //   - Must be called on the owning strand, co-located with all
    //     acquire/cancel/unlock of this mutex.
    //   - Drain overlap with another thread's acquire/unlock is UNDEFINED.
    //   - Ordinary cross-thread async_lock/unlock contention stays SUPPORTED.
    //   - The drain is UNINTERRUPTIBLE (disables own cancellation).
    //   - Reentrant callers on the strand await draining_complete_, then
    //     return the terminal result (NOT ok eagerly).
    //   - Terminal: (active_holders_count_==0) AND (in_flight_resumers_==0)
    //     AND (both lists empty in one pass).
    //
    // 058 T018 (research.md D-2/D-3; contracts/async_mutex-contract-delta.md)
    // — cross-executor teardown, made SAFE for the parked-then-reaped case:
    //   - A waiter that parked on a DIFFERENT executor than the owning
    //     strand and was subsequently reaped by this drain has its abort
    //     resume posted onto that waiter's OWN stored executor (ordinary
    //     cross-thread contention, already supported). The runner's
    //     in_flight_resumers_ decrement is RELEASE; this drain's terminal
    //     read (and the destructor's, D-3) is ACQUIRE — establishing the
    //     happens-before that makes destroying the mutex immediately after
    //     a completed drain memory-safe even though that resumer ran on a
    //     different core.
    //   - Explicit EXCLUSION: a cross-executor waiter that was GRANTED
    //     (not reaped-as-cancelled) becomes a cross-executor *holder*; if
    //     its unlock() overlaps this drain, that is the "Drain overlap ...
    //     is UNDEFINED" case above and is NOT made safe by this ordering
    //     (active_holders_count_ decrements before the holder's state_ CAS,
    //     so the drain can observe 0/finalize while that CAS still targets
    //     freed memory — and the destructor guard cannot catch it, both
    //     counters reading 0). Callers that keep the drain strand-local,
    //     co-located with ALL acquire/cancel/unlock (the documented
    //     contract above), never reach this.
    [[nodiscard]] asio::awaitable<expected_t<void>> cancel_and_drain() noexcept;

    // ─────────────────────────────────────────────────────────────────────────
    // Release
    // ─────────────────────────────────────────────────────────────────────────

    // unlock — release the mutex.
    void unlock() noexcept;

    // ─────────────────────────────────────────────────────────────────────────
    // Accessor
    // ─────────────────────────────────────────────────────────────────────────

    [[nodiscard]] completion_policy policy() const noexcept { return policy_; }

#ifdef FIXPP_ASYNC_MUTEX_TEST_SEAM
    // 058 T020 (research.md D-4/D-7): test-only seam presetting/inspecting
    // the bump-allocator counter, so the pre-fix u32 wrap-and-reissue defect
    // can be witnessed without 2^32 real attempts. No-op / absent unless the
    // standalone test_pool_exhaustion_reuse target defines the macro (same
    // ODR discipline as the pop-phase seam: standalone target, zero
    // fixpp_sync linkage — research.md D-7).
    //
    // test_seam_slot_attached_awaiter is declared here (needs waiter_pool_
    // storage, private below) but DEFINED out-of-line after
    // detail::waiter_record is complete (mirrors async_lock/unlock).
    void test_seam_preset_waiter_pool_next(std::uint32_t v) noexcept {
        waiter_pool_next_.store(v, std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint32_t test_seam_waiter_pool_next() const noexcept {
        return waiter_pool_next_.load(std::memory_order_relaxed);
    }
    // Reads the `attached_awaiter_` identity currently stored at pool slot
    // `idx`, interpreting the slot storage as a (possibly still-live)
    // waiter_record. Two reads returning different pointers proves a second,
    // unrelated allocation attempt clobbered a still-parked waiter's memory
    // (the reissue-of-a-live-slot oracle) — not merely that "an allocation
    // succeeded", which is non-discriminating.
    [[nodiscard]] void const* test_seam_slot_attached_awaiter(std::size_t idx) const noexcept;

    // 058 T026 (research.md D-5/D-6, spec FR-005/FR-006): mutable pool-slot
    // accessor, used ONLY to fault-inject the provably-impossible states the
    // T023 chain-walk traps and the T024 null-awaiter trap exist to catch —
    // no legitimate API sequence can produce a `granted` record mid
    // chain-walk or a null `attached_awaiter_` on a scheduled record (see the
    // trap-site comments in `unlock()` / the resume runner above). A
    // death-test child uses this to directly corrupt `phase_` /
    // `attached_awaiter_` on a genuinely-live (real `async_lock()`-parked)
    // record immediately before driving the real unlock()/resume path into
    // the corresponding trap.
    [[nodiscard]] detail::waiter_record* test_seam_mutable_slot(std::size_t idx) noexcept;

    // 058 Gate-B (2026-07-03 test-oracle-hygiene fix): lifetime-safe
    // free-list-membership read. Reads ONLY the mutex's own
    // `waiter_pool_free_` head (an async_mutex member, alive for the whole
    // test) — never a possibly-already-`~waiter_record()`'d pool slot. Used
    // by the chain-walk CAS-loss witnesses to prove a released waiter_record
    // was returned to the free list (as the new head) WITHOUT reading the
    // destroyed record's own members. No production behaviour or layout
    // change (accessor only; no new data member).
    [[nodiscard]] std::uint32_t test_seam_free_list_head_slot_index() const noexcept {
        return detail::unpack_free_list_head(waiter_pool_free_.load(std::memory_order_acquire))
            .slot_index;
    }
#endif

private:
    // ─────────────────────────────────────────────────────────────────────────
    // State encoding constants (normative — low-bit sentinel design)
    // ─────────────────────────────────────────────────────────────────────────

    // not_locked = 1: free; low bit set, distinguishable from any 8-byte-aligned
    // waiter pointer (alignof(async_mutex_awaiter) >= 8 enforced below).
    static constexpr uintptr_t not_locked = 1;

    // locked_no_waiters = 0: held; LIFO list empty.
    static constexpr uintptr_t locked_no_waiters = 0;

    // ─────────────────────────────────────────────────────────────────────────
    // Fields (layout order is normative per [2f §4.1]; cache-line locality)
    // ─────────────────────────────────────────────────────────────────────────

    // Primary state atom — Lewis-Baker / cppcoro encoding.
    // I-01..I-05 ordering sites.
    std::atomic<uintptr_t> state_{not_locked};

    // RC-A v1.1 — mutex-owned residual FIFO chain.
    // I-10..I-12 ordering sites.
    std::atomic<detail::waiter_record*> next_drain_head_{nullptr};

    static constexpr std::size_t waiter_pool_capacity_ = 512;
    static constexpr std::size_t waiter_record_storage_size_ = 256;

    // 058 T003 (data-model.md; research.md D-1): `free_link` is PERSISTENT
    // per-slot metadata (mutex-lifetime), not a `waiter_record` member — it
    // stays unused until T010 wires the free-list push to it. Kept at 256 B
    // total (248 + 4, rounded to alignof(max_align_t)=16) to preserve the
    // layout golden.
    struct waiter_pool_slot {
        alignas(std::max_align_t) std::byte storage[248];
        std::atomic<std::uint32_t> free_link;
    };
    static_assert(sizeof(waiter_pool_slot) == waiter_record_storage_size_,
                  "fixpp::sync: waiter_pool_slot must stay 256 B (layout golden; data-model.md).");

    std::array<waiter_pool_slot, waiter_pool_capacity_> waiter_pool_storage_{};
    std::atomic<std::uint32_t> waiter_pool_next_{0};

    // 058 T004 (research.md D-1): generation-tagged packed head, retyped from
    // `std::atomic<detail::waiter_record*>` (same 8 B — layout golden held).
    // Initialised to the empty-list sentinel (generation 0, slot_index =
    // free_list_empty_index).
    std::atomic<std::uint64_t> waiter_pool_free_{
        detail::pack_free_list_head(0, detail::free_list_empty_index)};

    // RC-B v1.1 — drain flag; set by cancel_and_drain(), never cleared.
    // I-13..I-16 ordering sites.
    std::atomic<bool> draining_{false};

    // RC-B v1.1 — concurrent-call serialiser for cancel_and_drain().
    std::atomic_flag drain_in_progress_ = ATOMIC_FLAG_INIT;

    // v1.2 / v1.3 RC-α — winner-only post-CAS holder count.
    // 048: demoted to relaxed — all inc/dec happen on the one owning strand.
    std::atomic<std::uint32_t> active_holders_count_{0};

    // 048 (Erratum E-5) — in-flight-resumer count.
    // Incremented inside schedule_record_resume() BEFORE the asio::post (sole
    // incrementer; every path: reap/grant/drained-bypass/on_cancel).
    // Decremented in the resume runner AFTER release_ref(record) — the LAST
    // statement in the runner (the drain observing 0 may destroy the mutex
    // immediately; nothing touches the mutex after the decrement).
    // 058 T016/T018 (research.md D-2): the decrement is RELEASE; the drain
    // terminal condition and the destructor guard (D-3) both read it with
    // ACQUIRE. The increment stays relaxed (program-order-before the
    // asio::post on the SAME thread; asio::post itself supplies the
    // synchronizes-with edge into the posted handler). The decrement's
    // reader is NOT always strand-confined: the resume runner this barrier
    // guards is posted onto the WAITER's own stored executor, which may
    // differ from the drain's owning strand for a waiter parked
    // cross-executor and then reaped (contracts/async_mutex-contract-
    // delta.md). The release/acquire pairing is the happens-before that
    // keeps that cross-executor teardown memory-safe.
    // This is THE barrier that keeps the mutex alive until every posted resumer
    // has dereferenced record->mutex_ (fixes P1-1 UAF — data-model.md).
    std::atomic<std::uint32_t> in_flight_resumers_{0};

    // 048 (Erratum E-5) — set true at finalize, AFTER the state_ CAS.
    // A reentrant cancel_and_drain() on the strand yields while this is false,
    // then returns the terminal result — NOT ok eagerly (fixes P1-2).
    std::atomic<bool> draining_complete_{false};

    // Per-mutex completion policy (immutable after construction).
    completion_policy const policy_{completion_policy::dispatch};

    friend struct detail::async_mutex_awaiter;
    friend struct detail::waiter_record;
};

// ─────────────────────────────────────────────────────────────────────────────
// Compile-time invariants (placed AFTER async_mutex but BEFORE awaiter):
// ─────────────────────────────────────────────────────────────────────────────

static_assert(sizeof(uintptr_t) >= sizeof(void*),
              "fixpp::sync: state encoding requires uintptr_t to fit a pointer.");
static_assert(std::atomic<uintptr_t>::is_always_lock_free,
              "fixpp::sync: async_mutex requires lock-free std::atomic<uintptr_t>.");
static_assert(std::atomic<fixpp::sync::detail::waiter_record*>::is_always_lock_free,
              "fixpp::sync: next_drain_head_ atomic exchange requires lock-free "
              "std::atomic<waiter_record*>.");

// ─────────────────────────────────────────────────────────────────────────────
// T012: E3 — async_lock_guard (full definition)
// Source: [2f §4.4], data-model E3, contracts/async_lock_guard.hpp
//
// sizeof(async_lock_guard) == sizeof(async_mutex*) == 8 B (one pointer only).
//
// Placed BEFORE async_mutex_awaiter's full definition because the awaiter's
// invoke_fn_t uses expected_t<async_lock_guard> which requires async_lock_guard
// to be a complete type.
// ─────────────────────────────────────────────────────────────────────────────

class async_lock_guard {
public:
    // Default-constructed guard — disengaged; safe to move-into.
    async_lock_guard() noexcept = default;

    // Move ctor — source becomes empty.
    async_lock_guard(async_lock_guard&& other) noexcept : mutex_(other.mutex_) {
        other.mutex_ = nullptr;
    }

    // Destructive move-assignment (RC#1 / N-P1-3 close). T031.
    // If *this is engaged, unlock its mutex first; then take ownership of
    // other's mutex. Self-assignment is a no-op (this == &other guard).
    async_lock_guard& operator=(async_lock_guard&& other) noexcept {
        if (this == &other) return *this;
        if (mutex_) mutex_->unlock();
        mutex_ = other.mutex_;
        other.mutex_ = nullptr;
        return *this;
    }

    async_lock_guard(async_lock_guard const&) = delete;
    async_lock_guard& operator=(async_lock_guard const&) = delete;

    // Destructor — calls mutex_->unlock() if engaged. T031.
    ~async_lock_guard() noexcept {
        if (mutex_) mutex_->unlock();
    }

    // Explicit early release. Disengages the guard and returns the back-pointer.
    [[nodiscard]] async_mutex* release() noexcept {
        auto* m = mutex_;
        mutex_ = nullptr;
        return m;
    }

    // Returns true iff the guard holds an engaged mutex pointer.
    [[nodiscard]] bool owns_lock() const noexcept { return mutex_ != nullptr; }

private:
    // Engaged constructor — private + friend-only (Opus N-P3-1 close).
    // [[clang::lifetimebound]]: guard MUST NOT outlive its mutex.
    explicit async_lock_guard(async_mutex* mutex [[clang::lifetimebound]]) noexcept
        : mutex_(mutex) {}

    // Friends that may construct an engaged guard.
    friend class async_mutex;
    friend struct detail::async_mutex_awaiter;

    async_mutex* mutex_{nullptr};
};

// T078 size contract: sizeof(async_lock_guard) == sizeof(async_mutex*).
// Full static_assert is in T078; noted here for clarity.

namespace detail {

// ─────────────────────────────────────────────────────────────────────────────
// T058: E5 — slot_allocator (RC-C, re-anchored by Erratum E-4).
//
// Erratum E-4 (2026-05-19): asio 1.36.0's cancellation_slot has NO
// allocator-binding hook (cancellation_signal::prepare_memory ->
// thread_info_base::allocate(cancellation_signal_tag); a per-thread recycling
// cache, not bind_allocator-aware). slot_allocator is therefore NOT bound to
// the cancellation slot. It is retained as the typed, Allocator-shaped
// storage-policy wrapper for the allocation 2f *does* control — the
// waiter_record fallback — and is unit-verified by §9 seam #21 in isolation.
// The cancellation-handler closure uses asio's per-thread recycler (zero
// global new/delete in steady state by construction; one-time per-thread
// first-touch is §6.4 bench-soft).
//
// Three exhaustive cases (post-E-4, re-anchored to waiter_record storage):
//   case 1 (mr == nullptr): the per-mutex waiter_pool_ arm (E-2) — modelled
//          here by the inline buffer; over-capacity -> std::bad_alloc, which
//          the caller's trap converts to unexpected{sync_lock_alloc_failed}.
//   case 2: N/A — the waiter_record is never coroutine-frame-resident.
//   case 3 (mr != nullptr): std::pmr::polymorphic_allocator<void>{mr} —
//          modelled here by forwarding allocate/deallocate to mr; mr
//          exhaustion -> std::bad_alloc -> trap -> sync_lock_alloc_failed.
//
// The production waiter_record allocation lives inline in async_lock()
// (per-mutex waiter_pool_ freelist / pmr_waiter_block) and realises the same
// three cases directly; slot_allocator carries the policy for seam #21.
// ─────────────────────────────────────────────────────────────────────────────

class slot_allocator {
public:
    using value_type = std::byte;

    slot_allocator(async_mutex_awaiter* awaiter, std::pmr::memory_resource* mr) noexcept
        : awaiter_(awaiter), mr_(mr) {}

    [[nodiscard]] std::byte* allocate(std::size_t n) {
        if (mr_ != nullptr) {
            return static_cast<std::byte*>(mr_->allocate(n, alignof(std::max_align_t)));
        }

        if (!used_inline_ && n <= awaiter_inline_capacity_) {
            used_inline_ = true;
            return inline_storage();
        }
        throw std::bad_alloc{};
    }

    void deallocate(std::byte* p, std::size_t n) noexcept {
        if (mr_ != nullptr) {
            mr_->deallocate(p, n, alignof(std::max_align_t));
            return;
        }
        if (p == inline_storage()) used_inline_ = false;
    }

    bool operator==(slot_allocator const& other) const noexcept {
        return awaiter_ == other.awaiter_ && mr_ == other.mr_;
    }

private:
    static constexpr std::size_t awaiter_inline_capacity_ = 32;

    [[nodiscard]] std::byte* inline_storage() noexcept;

    async_mutex_awaiter* awaiter_;
    std::pmr::memory_resource* mr_;
    bool used_inline_{false};
};

// ─────────────────────────────────────────────────────────────────────────────
// async_mutex_awaiter — intrusive waiter node (full definition).
// One node per in-flight contended async_lock() call.
//
// Erratum E-1 conformance (2026-05-18):
//   The awaiter is a frame-local variable inside async_lock()'s coroutine frame
//   (NOT separately heap-allocated via global operator new). The asio completion
//   handler is stored via placement-new into the 32-byte slot_storage_ buffer,
//   making the contended path zero-global-heap when mr==nullptr + HALO fires.
//
//   Field mapping per Erratum E-1:
//     - coro_ (the design's "stored continuation") is replaced by the completion
//       handler stored in slot_storage_ via placement-new.
//     - result_ points at a local variable in async_lock()'s frame.
//     - invoke_fn_ / destroy_fn_: type-erased pointers into slot_storage_.
//     - All other fields (mutex_, next_, phase_, slot_, result_, slot_storage_)
//       are unchanged from the design layout.
//
// Defined AFTER async_lock_guard because invoke_fn_t uses
// expected_t<async_lock_guard> which requires async_lock_guard to be complete.
//
// Design: [2f §4.2], data-model E2, Erratum E-1.
//
// alignas(8): LIFO state_ encoding's low-bit not_locked sentinel requires
// waiter pointers to be >= 8-byte-aligned.
// ─────────────────────────────────────────────────────────────────────────────

struct alignas(std::max_align_t) waiter_record {
    async_mutex* mutex_{};
    waiter_record* next_{nullptr};
    std::atomic<waiter_phase> phase_{waiter_phase::queued};
    fixpp::sync::expected_t<fixpp::sync::async_lock_guard> result_;
    std::atomic<async_mutex_awaiter*> attached_awaiter_{nullptr};
    alignas(std::max_align_t) std::array<std::byte, 64> exec_storage_{};
    std::atomic<std::uint32_t> refcount_{0};

    // 058 T044 (tasks.md Phase 7.5, discharges W-6 by construction): the ONLY
    // executor type ever stored here in production is asio::any_io_executor
    // (async_lock() captures `co_await asio::this_coro::executor`, which is
    // always this type). This static_assert proves at compile time that it
    // fits `exec_storage_`, which makes store_executor()'s
    // `sizeof(RawExecutor) > sizeof(exec_storage_)` fail arm (A7,
    // store_executor's `return false;` below) STRUCTURALLY UNREACHABLE for
    // every instantiation that ever occurs: `if constexpr` on a false
    // condition preprocesses the `return false;` branch out of the compiled
    // program for the only type that is ever substituted for `Executor`. Its
    // sibling — destroy_executor()'s `if (destroy_exec_fn_ != nullptr)`
    // null-fn guard — is likewise dead: with store_executor() infallible, the
    // only record that would ever reach destroy_executor() with a null
    // destroy_exec_fn_ was a record whose store_executor() call had failed;
    // no such record is ever constructed. (Every record between construction
    // at :1154 and a successful store_executor() at :1162 is released via
    // the explicit two release_ref() calls in the store_executor()-fail arm
    // above it, not via destroy_executor(); destroy_executor() is reached
    // only along paths where store_executor() already succeeded and set
    // destroy_exec_fn_.) Neither arm has a witness; both are waived
    // (tasks.md W-6) as discharged-by-construction, not exercised.
    static_assert(sizeof(asio::any_io_executor) <= sizeof(exec_storage_),
                  "async_mutex waiter_record: asio::any_io_executor must fit "
                  "exec_storage_ (store_executor()'s size-fail arm is "
                  "otherwise reachable for the only production executor "
                  "type)");

    // 048: latch param removed — in_flight_resumers_ is now a plain member of
    // async_mutex; schedule_record_resume increments it before posting.
    using resume_fn_t = void (*)(void*, waiter_record*) noexcept;
    using destroy_exec_fn_t = void (*)(void*) noexcept;

    resume_fn_t resume_fn_{nullptr};
    destroy_exec_fn_t destroy_exec_fn_{nullptr};

    template <typename Executor>
    bool store_executor(Executor&& ex) noexcept;

    void destroy_executor() noexcept {
        if (destroy_exec_fn_ != nullptr) {
            destroy_exec_fn_(exec_storage_.data());
            destroy_exec_fn_ = nullptr;
            resume_fn_ = nullptr;
        }
    }

    static void add_ref(waiter_record* record, std::uint32_t count = 1) noexcept {
        record->refcount_.fetch_add(count, std::memory_order_relaxed);
    }

    static void release_ref(waiter_record* record) noexcept;

    // 048: schedule_record_resume as a static method so it can access
    // async_mutex::in_flight_resumers_ via the waiter_record friend relationship.
    // Increments in_flight_resumers_ BEFORE the post (sole incrementer).
    static void schedule_resume(waiter_record* record) noexcept;
};

struct alignas(8) async_mutex_awaiter {
    async_mutex* mutex_{nullptr};
    waiter_record* record_{nullptr};
    asio::cancellation_slot slot_{};
    alignas(8) std::array<std::byte, 32> slot_storage_{};

    using invoke_fn_t = void (*)(
        void* storage, fixpp::sync::expected_t<fixpp::sync::async_lock_guard> result) noexcept;
    using destroy_fn_t = void (*)(void* storage) noexcept;

    invoke_fn_t invoke_fn_{nullptr};
    destroy_fn_t destroy_fn_{nullptr};

    template <typename H>
    void store_handler(H&& h) noexcept {
        using RawH = std::remove_cvref_t<H>;
        static_assert(sizeof(RawH) <= sizeof(slot_storage_),
                      "async_mutex_awaiter: handler too large for slot_storage_");
        static_assert(alignof(RawH) <= 8,
                      "async_mutex_awaiter: handler over-aligned for slot_storage_");
        ::new (slot_storage_.data()) RawH(std::forward<H>(h));
        invoke_fn_ = [](void* s,
                        fixpp::sync::expected_t<fixpp::sync::async_lock_guard> r) noexcept {
            // The handler must be MOVED OUT of slot_storage_ and the in-buffer
            // object destroyed BEFORE it is invoked: invoking the asio
            // awaitable_handler resumes the waiter coroutine, which runs to
            // co_return and destroys its frame — including this frame-local
            // async_mutex_awaiter and its slot_storage_ (Erratum E-1). A
            // post-invocation `hp->~RawH()` (or any touch of `s`) is then a
            // heap-use-after-free (TSan: US1 sync_fifo_fairness drain cycle,
            // US2 sync_cancellation_mid_wait). `local` lives on the posted
            // resume runner's stack (Erratum E-3 guarantees we are NOT nested
            // in the destroyed frame), so it safely outlives the resume.
            auto* hp = std::launder(reinterpret_cast<RawH*>(s));
            RawH local = std::move(*hp);
            hp->~RawH();
            std::move(local)(std::move(r));
        };
        destroy_fn_ = [](void* s) noexcept { std::launder(reinterpret_cast<RawH*>(s))->~RawH(); };
    }

    void invoke_handler(fixpp::sync::expected_t<fixpp::sync::async_lock_guard> result) noexcept {
        auto* fn = invoke_fn_;
        invoke_fn_ = nullptr;
        destroy_fn_ = nullptr;
        fn(slot_storage_.data(), std::move(result));
    }

    // 058 T043 (tasks.md Phase 7.5, discharges W-8 by removal): a defensive
    // `destroy_handler()` (destroy-the-stored-handler-without-invoking-it) was
    // grep-verified to have ZERO call sites tree-wide and removed rather than
    // waived at 0% lcov. Every stored handler is always reached via
    // invoke_handler() through the posted resume runner: the fast-path grant,
    // the contended-grant chain-walk, the assign-throws catch arm, the
    // draining_ arm, and cancel_and_drain() all route through
    // schedule_record_resume() -> resume_fn_ -> invoke_handler() with an
    // error or success result — none of them destroy the handler in place.
    // Destroy-without-invoke was therefore dead, not a masked leak.

    void on_cancel(asio::cancellation_type) const noexcept;
};

template <typename Executor>
bool waiter_record::store_executor(Executor&& ex) noexcept {
    using RawExecutor = std::remove_cvref_t<Executor>;
    static_assert(alignof(RawExecutor) <= alignof(std::max_align_t),
                  "waiter_record executor alignment exceeds exec_storage_ alignment");
    if constexpr (sizeof(RawExecutor) > sizeof(exec_storage_)) {
        return false;
    } else {
        ::new (exec_storage_.data()) RawExecutor(std::forward<Executor>(ex));
        // 048 (Erratum E-5): latch param removed from resume_fn_t.
        // in_flight_resumers_ is decremented here, AFTER release_ref, as the
        // LAST statement. Ordering: the drain observing in_flight_resumers_==0
        // may finalize and the caller may destroy the mutex immediately — nothing
        // must touch `m` after the decrement.
        // 058 T016/T018 (research.md D-2): the decrement below is a RELEASE,
        // paired with an ACQUIRE at the drain terminal condition and the
        // destructor guard — NOT relaxed. The drain is strand-local BY
        // CONTRACT (cancel_and_drain() itself must run co-located with all
        // acquire/cancel/unlock), but this resume runner is intentionally
        // cross-executor: it is posted onto the WAITER's own stored executor,
        // which may differ from the drain's owning strand (a waiter parked
        // cross-executor and then reaped by the drain). The release/acquire
        // pairing is what makes that cross-executor case memory-safe.
        resume_fn_ = [](void* storage, waiter_record* record) noexcept {
            auto* exec = std::launder(reinterpret_cast<RawExecutor*>(storage));
            auto runner = [record]() mutable {
                // Capture mutex pointer BEFORE release_ref which may free record.
                auto* m = record->mutex_;
                auto* awaiter = record->attached_awaiter_.load(std::memory_order_acquire);
                if (awaiter != nullptr) {
                    awaiter->slot_.clear();
                    awaiter->invoke_handler(std::move(record->result_));
                } else {
                    // 058 T024 (research.md D-6, spec FR-006 / AM-P3-2):
                    // attached_awaiter_ is nulled ONLY at the async_lock()
                    // coroutine tail (`:1187`), strictly AFTER this runner
                    // invokes the handler for THIS SAME schedule (each record
                    // is resumed at most once — the single-schedule
                    // invariant: every schedule_record_resume() call site
                    // transitions phase_ via a one-shot CAS/exchange before
                    // scheduling). A null awaiter observed here is therefore
                    // structurally impossible. Trap loudly instead of
                    // silently dropping `record->result_` — which, for a
                    // granted record, holds a live async_lock_guard; letting
                    // it fall through to ~waiter_record() (below, via
                    // release_ref) would run the guard's destructor and call
                    // unlock() on a mutex no one ever took ownership of (a
                    // phantom unlock / mutual-exclusion break). Terminating
                    // here means that destructor never runs, so no defensive
                    // `result_` disarm is needed — D-6's guidance (IF one
                    // were ever added, it MUST disengage via
                    // `async_lock_guard::release()`, the leaked-lock
                    // direction, NEVER invoke `unlock()`) does not apply to
                    // this primary fix.
                    assert(awaiter != nullptr &&
                           "async_mutex: resume runner observed a null "
                           "attached_awaiter_ for a scheduled record — "
                           "impossible invariant break");
                    std::terminate();
                }
                release_ref(record);  // may free record; do not touch record after
                // Decrement LAST — the drain may destroy the mutex once it
                // observes 0; nothing may touch m after this line.
                // 058 T016 (research.md D-2): RELEASE. Paired with the ACQUIRE
                // load at the drain terminal condition and the destructor
                // guard. This is the happens-before edge that makes
                // cross-executor drain-then-destroy memory-safe: every write
                // this runner made into mutex-owned storage above (the
                // free-list push inside release_ref, in particular) is
                // guaranteed visible to whichever thread's acquire load
                // observes the resulting 0 — closing the write-after-free
                // window on weakly-ordered hardware (contracts/async_mutex-
                // contract-delta.md).
                m->in_flight_resumers_.fetch_sub(1, std::memory_order_release);
            };

            // v1.6 Erratum E-3: 2f waiter resumption is ALWAYS posted, never
            // inline-dispatched. The resume site is intrinsically re-entrant —
            // always inside unlock()/on_cancel(), nested within another asio
            // awaitable coroutine on the bound-executor thread. An inline
            // asio::dispatch there resumes the parked waiter re-entrantly, and
            // asio's awaitable_thread/awaitable_frame chaining is not
            // re-entrant across a nested coroutine resume -> heap-use-after-free
            // (TSan: US1 sync_fifo_fairness, US2 sync_cancellation_mid_wait).
            // completion_policy() is preserved as a semantic knob; both
            // policies post for waiter resumption.
            //
            // 058 T025 (research.md D-8, spec FR-007 / AM-P3-3) — OOM
            // disposition, settled: `asio::post` here can allocate (queuing
            // the posted work item) and this lambda is `noexcept`, so an
            // allocator-exhaustion `bad_alloc` escaping it is an ACCEPTED
            // fail-stop (`std::terminate`), consistent with this primitive's
            // terminate-on-contract-violation posture elsewhere (the
            // destructor guard, the T023/T024 traps above). This is NOT
            // closed the way the pre-grant slot-assign allocation is
            // (`store_executor`/`inherited_slot.assign` failures above fail
            // CLOSED with `sync_lock_alloc_failed`, `:1106-1127`): the
            // difference is grant ORDERING, not an oversight. Slot-assign
            // runs pre-commitment — no waiter has been granted yet, so
            // returning an error is a legitimate outcome. This post runs
            // POST-GRANT: the waiter already owns the lock (or has been
            // definitively cancelled) and MUST be resumed — there is no
            // error channel left to report through, and silently dropping
            // the resume would leak a granted holder or hang a cancelled
            // waiter forever, which is worse than a loud fail-stop. See
            // contracts/async_mutex-contract-delta.md "OOM disposition on
            // the resume path" for the full contract-level statement.
            asio::post(*exec, std::move(runner));
        };
        destroy_exec_fn_ = [](void* storage) noexcept {
            std::launder(reinterpret_cast<RawExecutor*>(storage))->~RawExecutor();
        };
        return true;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Compile-time invariants for the awaiter (placed AFTER the struct definition;
// alignof on an incomplete class is ill-formed).
// ─────────────────────────────────────────────────────────────────────────────

// Alignment: low-bit not_locked sentinel must be distinguishable from real ptr.
static_assert(alignof(async_mutex_awaiter) >= 8,
              "fixpp::sync: async_mutex_awaiter must be 8-byte-aligned so the "
              "low-bit `not_locked` sentinel (= 1) is distinguishable from a real "
              "waiter pointer.");
static_assert(alignof(waiter_record) >= 8,
              "fixpp::sync: waiter_record must be 8-byte-aligned so the "
              "low-bit `not_locked` sentinel (= 1) is distinguishable from a real "
              "waiter pointer.");
static_assert(sizeof(waiter_record) <= 248,
              "fixpp::sync: waiter_record exceeds waiter_pool storage budget.");

// T060: §1.1 / §6.4 awaiter byte budget — HALO-eligibility precondition.
// The frame-local awaiter (Erratum E-2 split: intrusive identity moved to
// waiter_record) must stay within the published ≤ 96 B ceiling so it fits in
// the caller's coroutine-frame free space and the HALO elision (§6.4, seam #9)
// remains viable. async_lock's await_ready/await_suspend equivalents are the
// inline header-only async_initiate lambda below — no out-of-line escape.
static_assert(sizeof(async_mutex_awaiter) <= 96,
              "fixpp::sync: async_mutex_awaiter exceeds the §1.1 ≤ 96 B HALO budget.");

// ─────────────────────────────────────────────────────────────────────────────
// 058 T006 — compile-gated free-list pop test seam (research.md D-7).
// Zero production footprint: absent entirely unless FIXPP_ASYNC_MUTEX_TEST_SEAM
// is defined (the standalone `test_async_mutex_aba_interleave` target only).
// Default no-op (nullptr); the seam-enabled witness overrides the pointer to
// pin a thread at each half of the free-list pop window. No call sites are
// wired into the pop yet — that lands with the T007/T008 witnesses.
// ─────────────────────────────────────────────────────────────────────────────
#ifdef FIXPP_ASYNC_MUTEX_TEST_SEAM
enum class async_mutex_seam_phase : std::uint8_t {
    pop_pre_link_load,  // head load -> free-link load (AM-P1 part 2)
    pop_pre_cas,        // free-link load -> CAS (AM-P1 part 1)

    // 058 T046 (F4/F6 — coverage-design gate empirical correction, see
    // .specify/decisions/058-async-mutex-hardening-coverage-design.md
    // "EMPIRICAL CORRECTION"): pin unlock()'s two terminal-CAS-fail ->
    // recursive-unlock windows so a seam-installed hook can force a
    // concurrent waiter arrival into the narrow gap between
    // `state_.exchange(locked_no_waiters)` and the immediately-following
    // CAS-back-to-`not_locked`. Both arms are reachable in-contract but not
    // reliably organic on the seam-OFF coverage lane (F4 flaky ~0.3-1.5%;
    // F6 0/all-trials) — see test_async_mutex_terminal_cas_recursive_unlock.cpp.
    unlock_pre_terminal_cas_fast,  // F4: no-waiters fast path (:1379 CAS)
    unlock_pre_terminal_cas_fifo,  // F6: FIFO walk exhausted, all cancelled (:1439 CAS)

    // 058 Gate-B MAJOR-2 (async_mutex.hpp contended-acquire loop, ~:1250):
    // deterministic reproduction of the pre-fix `old_state` staleness
    // livelock. Two cooperating phases pin the SAME acquirer thread twice:
    //   - acq_pre_state_reload: immediately BEFORE the loop's initial
    //     `old_state = state_.load()` — lets the test release a temporary
    //     holder with zero waiters queued, so the reload deterministically
    //     observes `not_locked`.
    //   - acq_pre_notlocked_cas: immediately AFTER the loop observes
    //     `old_state == not_locked`, BEFORE the not_locked -> locked_no_waiters
    //     CAS — lets a second thread win the lock first, forcing the pinned
    //     acquirer's CAS to fail. Pre-fix, the CAS's `exp2` local is discarded
    //     on failure and the loop retries against the stale `old_state`
    //     forever (busy-spin, never re-queues). Post-fix, the CAS operates
    //     directly on `old_state`, which is refreshed on failure, and the
    //     next iteration correctly falls through to the queueing branch.
    //     See test_async_mutex_acquire_livelock.cpp.
    acq_pre_state_reload,
    acq_pre_notlocked_cas,

    // 058 Gate-B MAJOR-1 (unlock() chain-walk CAS-loss, residual walk
    // ~:1345 / fresh FIFO walk ~:1426): pin unlock() immediately AFTER it
    // loads a waiter's `phase_` as `queued`, BEFORE the `queued -> granted`
    // CAS, so a concurrent `on_cancel()` can win the `queued -> cancelled`
    // race first — the `ph = expected_ph` arm the coverage-design doc had
    // waived as "hard to force" (2026-07-03 Gate-B correction: reachable,
    // must be witnessed, not waived). Both list walks get their own phase
    // since they are structurally distinct code (residual FIFO chain vs.
    // the fresh LIFO->FIFO reversal). See
    // test_async_mutex_chain_walk_cas_loss.cpp.
    unlock_pre_grant_cas_residual,
    unlock_pre_grant_cas_fifo,
};

inline void (*async_mutex_test_seam)(async_mutex_seam_phase) noexcept = nullptr;
#endif

}  // namespace detail

}  // namespace fixpp::sync

// ─────────────────────────────────────────────────────────────────────────────
// T026/T027/T028/T029/T030/T031 — Out-of-line method bodies.
// ─────────────────────────────────────────────────────────────────────────────

// T050 (US3) final: fire terminate if held or waiters present.
// US1: fires terminate if state_ != not_locked (mutex is held or has waiters).
// 058 T017 (research.md D-3 / AM-P2-2): widened with a THIRD OR-term —
// in_flight_resumers_ != 0 — closing the silent cancel/grant-delivered-then-
// destroy UAF that neither of the first two terms detects (a resume runner
// can be posted-but-not-yet-run with state_ already restored to not_locked
// and next_drain_head_ already empty; see tests/sync/test_destructor_release_
// death.cpp T013/T014). ACQUIRE — paired with the RELEASE decrement in the
// resume runner (T016); observing 0 here happens-before every write that
// runner made into mutex-owned pool storage, so a false-negative (reading a
// stale nonzero-as-if-zero, or missing a real writer's in-flight state) is
// ruled out on weakly-ordered hardware, not just x86.
//
// Deliberately NOT gated on active_holders_count_ (research.md D-3, Gate-A
// both reviewers): active_holders_count_ is not a valid teardown barrier — it
// is decremented EARLY in unlock() (:961-ish), before unlock() finishes
// touching state_/draining_, so a ==0 reading does not prove the mutex is
// untouched. A holders-based OR-term would also be REDUNDANT with the
// existing `state_ != not_locked` term, not merely "not a barrier": every
// path that increments active_holders_count_ does so only while `state_`
// already reads as held (a grant transitions state_ away from not_locked in
// the same CAS/exchange that hands out the holder slot), and every path that
// would leave active_holders_count_ > 0 at destroy time necessarily leaves
// state_ != not_locked too (unlock() only restores state_ == not_locked on
// the fully-drained fast path, which is exactly the path that also decrements
// active_holders_count_ back to 0 first). So active_holders_count_ > 0 =>
// state_ != not_locked always holds, and dropping a holders term loses no
// detection — it would be dead weight, not a partial fix (Fable coverage-gate
// H3). The genuine cross-executor granted-holder-vs-drain UAF this exposes
// (a waiter parked cross-executor, granted, whose unlock() overlaps a drain)
// is out of scope for this guard and is instead handled by the CONTRACT
// (cancel_and_drain() must be strand-local; contracts/async_mutex-contract-
// delta.md "Explicit EXCLUSION").
inline fixpp::sync::async_mutex::~async_mutex() noexcept(false) {
    uintptr_t s = state_.load(std::memory_order_acquire);
    if (s != not_locked || next_drain_head_.load(std::memory_order_acquire) != nullptr ||
        in_flight_resumers_.load(std::memory_order_acquire) != 0) {
        std::terminate();
    }
}

#ifdef FIXPP_ASYNC_MUTEX_TEST_SEAM
// 058 T020 (research.md D-4/D-7): out-of-line so `detail::waiter_record` is
// complete. Test-only; no-op / absent unless FIXPP_ASYNC_MUTEX_TEST_SEAM is
// defined (standalone target only, per the D-7 ODR discipline — see the
// in-class declaration above for the rationale).
inline void const* fixpp::sync::async_mutex::test_seam_slot_attached_awaiter(
    std::size_t idx) const noexcept {
    auto const* record = std::launder(
        reinterpret_cast<detail::waiter_record const*>(waiter_pool_storage_[idx].storage));
    return record->attached_awaiter_.load(std::memory_order_relaxed);
}

// 058 T026 (research.md D-5/D-6): out-of-line for the same reason as
// test_seam_slot_attached_awaiter above (detail::waiter_record completeness).
// Test-only; no-op / absent unless FIXPP_ASYNC_MUTEX_TEST_SEAM is defined.
inline fixpp::sync::detail::waiter_record* fixpp::sync::async_mutex::test_seam_mutable_slot(
    std::size_t idx) noexcept {
    return std::launder(
        reinterpret_cast<detail::waiter_record*>(waiter_pool_storage_[idx].storage));
}
#endif

inline std::byte* fixpp::sync::detail::slot_allocator::inline_storage() noexcept {
    return awaiter_->slot_storage_.data();
}

inline void fixpp::sync::detail::waiter_record::release_ref(waiter_record* record) noexcept {
    if (record->refcount_.fetch_sub(1, std::memory_order_acq_rel) != 1) {
        return;
    }

    auto* mutex = record->mutex_;
    record->destroy_executor();
    record->~waiter_record();

    auto* begin = reinterpret_cast<std::byte*>(mutex->waiter_pool_storage_.data());
    auto* end = begin + sizeof(mutex->waiter_pool_storage_);
    auto* raw = reinterpret_cast<std::byte*>(record);
    if (raw >= begin && raw < end) {
        // 058 T010 (research.md D-1): generation-tagged push. Writes the
        // PERSISTENT per-slot `free_link` (mutex-lifetime slot metadata) —
        // never the just-destroyed record (`~waiter_record()` already ran
        // above) — closing the Gate-A-1 lifetime blocker. Carries the
        // CURRENTLY-LOADED generation UNCHANGED (only the pop bumps it; a
        // push-side bump would silently defeat the T012 mutation-check by
        // advancing the head on every push regardless of the pop fix).
        using waiter_pool_slot = fixpp::sync::async_mutex::waiter_pool_slot;
        auto this_idx = static_cast<std::uint32_t>(reinterpret_cast<waiter_pool_slot*>(raw) -
                                                   mutex->waiter_pool_storage_.data());

        auto head = mutex->waiter_pool_free_.load(std::memory_order_relaxed);
        for (;;) {
            auto unpacked = detail::unpack_free_list_head(head);
            mutex->waiter_pool_storage_[this_idx].free_link.store(unpacked.slot_index,
                                                                  std::memory_order_relaxed);
            auto desired = detail::pack_free_list_head(unpacked.generation, this_idx);
            if (mutex->waiter_pool_free_.compare_exchange_weak(
                    head, desired, std::memory_order_release, std::memory_order_acquire)) {
                break;
            }
            // CAS failed: `head` was reloaded; retry with the fresh link
            // store above (re-executed each iteration, never hoisted out of
            // the loop — a stale free_link write would lose the chain).
        }
        return;
    }

    struct pmr_waiter_block {
        std::pmr::memory_resource* mr;
        alignas(fixpp::sync::detail::waiter_record)
            std::byte storage[sizeof(fixpp::sync::detail::waiter_record)];
    };

    auto* block = reinterpret_cast<pmr_waiter_block*>(raw - offsetof(pmr_waiter_block, storage));
    block->mr->deallocate(block, sizeof(pmr_waiter_block), alignof(pmr_waiter_block));
}

// 048 (Erratum E-5): sole incrementer of in_flight_resumers_.
// Increments BEFORE the post so the drain's terminal condition cannot fire
// before the resumer has been counted. Defined after async_mutex is complete
// (so in_flight_resumers_ is accessible via the friend relationship).
inline void fixpp::sync::detail::waiter_record::schedule_resume(waiter_record* record) noexcept {
    add_ref(record);  // scheduled resumer ref
    // Access in_flight_resumers_ through the waiter_record friend.
    record->mutex_->in_flight_resumers_.fetch_add(1, std::memory_order_relaxed);
    record->resume_fn_(record->exec_storage_.data(), record);
}

namespace {

inline void push_residual(std::atomic<fixpp::sync::detail::waiter_record*>& head,
                          fixpp::sync::detail::waiter_record* residual) noexcept {
    if (residual == nullptr) return;

    auto* tail = residual;
    while (tail->next_ != nullptr) tail = tail->next_;

    auto* old_head = head.load(std::memory_order_acquire);
    do {
        tail->next_ = old_head;
    } while (!head.compare_exchange_weak(old_head, residual, std::memory_order_release,
                                         std::memory_order_acquire));
}

// 048 (Erratum E-5): delegates to waiter_record::schedule_resume which can
// access async_mutex::in_flight_resumers_ via the friend relationship.
inline void schedule_record_resume(fixpp::sync::detail::waiter_record* record) noexcept {
    fixpp::sync::detail::waiter_record::schedule_resume(record);
}

}  // namespace

inline void fixpp::sync::detail::async_mutex_awaiter::on_cancel(
    asio::cancellation_type) const noexcept {
    auto* record = record_;
    if (record == nullptr) return;

    waiter_phase expected = waiter_phase::queued;
    if (record->phase_.compare_exchange_strong(expected, waiter_phase::cancelled,
                                               std::memory_order_acq_rel,
                                               std::memory_order_acquire)) {
        record->result_ = fixpp::sync::expected_t<fixpp::sync::async_lock_guard>{
            std::unexpected(fixpp::core::error::sync_lock_aborted)};
        schedule_record_resume(record);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// T026: async_lock — awaitable coroutine (Erratum E-1 conforming).
// [2f §4.1], [2f §4.2], [2f §4.2.1], [2f §4.2.2].
//
// 048 (Erratum E-5): active_acquirers_count_ REMOVED (vestigial — the corrected
// terminal condition does not read it; async_lock's initiation body from the
// draining_ load to the state_ push is synchronous, so the reap cannot
// interleave a half-finished acquirer on the one strand; the drain contract
// forbids cross-thread overlap — research.md D-2/W-3b).
//
// async_lock is itself an asio::awaitable<expected_t<async_lock_guard>>
// coroutine. The async_mutex_awaiter is declared as a LOCAL VARIABLE in this
// frame — it is NOT heap-allocated via global operator new (Erratum E-1).
//
// The asio completion handler (produced by use_awaitable) is stored via
// placement-new into the awaiter's inline slot_storage_ buffer (32 B).
// sizeof(awaitable_handler<any_io_executor, T>) == 8 B (one pointer) on
// asio/1.36.0, leaving 24 B headroom within the 32-byte buffer.
//
// Result lifetime: `result` is a local variable in this frame. `awaiter.result_`
// points at it. Both are valid from contended-path entry through co_return.
// ─────────────────────────────────────────────────────────────────────────────

inline asio::awaitable<fixpp::sync::expected_t<fixpp::sync::async_lock_guard>>
fixpp::sync::async_mutex::async_lock(std::pmr::memory_resource* mr) noexcept {
    using detail::async_mutex_awaiter;
    using detail::waiter_phase;
    using detail::waiter_record;

    async_mutex_awaiter awaiter;
    awaiter.mutex_ = this;
    auto bound_executor = co_await asio::this_coro::executor;
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation{});
    auto cancellation_state = co_await asio::this_coro::cancellation_state;
    auto inherited_slot = cancellation_state.slot();

    auto result = co_await asio::async_initiate<const asio::use_awaitable_t<>&,
                                                void(expected_t<async_lock_guard>)>(
        [this, &awaiter, mr, bound_executor, inherited_slot](auto handler) mutable {
            if (draining_.load(std::memory_order_acquire)) {
                std::move(handler)(expected_t<async_lock_guard>{
                    std::unexpected(fixpp::core::error::sync_lock_drained)});
                return;
            }

            // Step 2: fast-path CAS not_locked → locked_no_waiters (I-01, §4.2.1 step 2).
            uintptr_t expected_state = not_locked;
            if (state_.compare_exchange_strong(expected_state, locked_no_waiters,
                                               std::memory_order_acquire,
                                               std::memory_order_relaxed)) {
                active_holders_count_.fetch_add(1, std::memory_order_relaxed);
                {
                    async_lock_guard guard{this};
                    std::move(handler)(expected_t<async_lock_guard>{std::move(guard)});
                }
                return;
            }

            auto* record = [&]() -> waiter_record* {
                if (mr != nullptr) {
                    struct pmr_waiter_block {
                        std::pmr::memory_resource* mr;
                        alignas(waiter_record) std::byte storage[sizeof(waiter_record)];
                    };
                    try {
                        auto* block = static_cast<pmr_waiter_block*>(
                            mr->allocate(sizeof(pmr_waiter_block), alignof(pmr_waiter_block)));
                        block->mr = mr;
                        return std::launder(reinterpret_cast<waiter_record*>(block->storage));
                    } catch (std::bad_alloc const&) {
                        return nullptr;
                    }
                }

                // 058 T009 (research.md D-1/D-7): generation-tagged pop.
                // Load the packed head; if empty, fall through to the bounded
                // bump allocator below. Otherwise chase the PERSISTENT
                // per-slot `free_link` (mutex-lifetime, never the destroyed
                // record — closes the Gate-A-1 reuse-race BLOCKER) and
                // install a new head whose generation is bumped by exactly
                // one relative to the head just observed — the bump is what
                // defeats the ABA on a stale CAS (D-1).
                auto head = waiter_pool_free_.load(std::memory_order_acquire);
                for (;;) {
                    auto unpacked = detail::unpack_free_list_head(head);
                    if (unpacked.slot_index == detail::free_list_empty_index) {
                        break;
                    }
#ifdef FIXPP_ASYNC_MUTEX_TEST_SEAM
                    // 058 T007/T008/T009 (research.md D-7): pin the pop here
                    // so a seam-installed hook can interleave a concurrent
                    // pop/push cycle (part 2: reuse race on the free-link
                    // atomic load below). No-op unless the standalone
                    // test_async_mutex_aba_interleave target defines the
                    // macro and installs a non-null hook.
                    if (detail::async_mutex_test_seam) {
                        detail::async_mutex_test_seam(
                            detail::async_mutex_seam_phase::pop_pre_link_load);
                    }
#endif
                    auto next_idx = waiter_pool_storage_[unpacked.slot_index].free_link.load(
                        std::memory_order_acquire);
#ifdef FIXPP_ASYNC_MUTEX_TEST_SEAM
                    // 058 T007/T009 (research.md D-7): pin the pop here so a
                    // seam-installed hook can interleave a concurrent
                    // pop-pop-push cycle recreating the same head (part 1:
                    // ABA on the generation-tagged CAS below).
                    if (detail::async_mutex_test_seam) {
                        detail::async_mutex_test_seam(detail::async_mutex_seam_phase::pop_pre_cas);
                    }
#endif
                    auto desired = detail::pack_free_list_head(unpacked.generation + 1, next_idx);
                    if (waiter_pool_free_.compare_exchange_weak(
                            head, desired, std::memory_order_acq_rel, std::memory_order_acquire)) {
                        return std::launder(reinterpret_cast<waiter_record*>(
                            waiter_pool_storage_[unpacked.slot_index].storage));
                    }
                    // CAS failed: compare_exchange_weak reloaded `head` with
                    // the current atomic value; loop retries against it.
                }

                // 058 T021 (research.md D-4): bounded CAS bump allocator.
                // The prior `fetch_add(1)` incremented UNCONDITIONALLY even on
                // capacity-check-FAILING attempts (the `>= capacity` check ran
                // AFTER the increment), so a sustained exhaustion storm
                // eventually wraps the u32 counter back into [0, capacity) and
                // RE-ISSUES an already-live slot (AM-P2-3). Load first; if
                // already at/over capacity, fail closed WITHOUT incrementing —
                // the counter can never advance past `waiter_pool_capacity_`,
                // so it can never wrap.
                auto cur = waiter_pool_next_.load(std::memory_order_acquire);
                for (;;) {
                    if (cur >= waiter_pool_capacity_) {
                        return nullptr;
                    }
                    if (waiter_pool_next_.compare_exchange_weak(
                            cur, cur + 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
                        break;
                    }
                    // CAS failed: compare_exchange_weak reloaded `cur` with the
                    // current atomic value; loop retries against it (re-checks
                    // the capacity bound on the reloaded value).
                }
                return std::launder(
                    reinterpret_cast<waiter_record*>(waiter_pool_storage_[cur].storage));
            }();
            if (record == nullptr) {
                std::move(handler)(expected_t<async_lock_guard>{
                    std::unexpected(fixpp::core::error::sync_lock_alloc_failed)});
                return;
            }

            ::new (record) waiter_record{};
            record->mutex_ = this;
            record->phase_.store(waiter_phase::queued, std::memory_order_relaxed);
            record->attached_awaiter_.store(&awaiter, std::memory_order_release);
            waiter_record::add_ref(record, 2);  // creator + attached awaiter
            awaiter.record_ = record;

            {
                if (!record->store_executor(bound_executor)) {
                    waiter_record::release_ref(record);
                    waiter_record::release_ref(record);
                    std::move(handler)(expected_t<async_lock_guard>{
                        std::unexpected(fixpp::core::error::sync_lock_alloc_failed)});
                    return;
                }

                awaiter.store_handler(std::move(handler));

                if (inherited_slot.is_connected()) {
                    awaiter.slot_ = inherited_slot;
                    // 048 T014 / FR-003: inherited_slot.assign allocates the
                    // cancellation-slot handler storage and can throw bad_alloc under
                    // OOM; escaping the noexcept await_suspend would std::terminate the
                    // whole process. Fail closed instead. The handler is already moved
                    // into the awaiter (store_handler above), so complete via the POSTED
                    // runner (E-3 — never an immediate invoke), NOT the moved-from local
                    // handler and NOT the store_executor exit above (which precedes
                    // store_handler). Ref-balance mirrors the draining_ branch below:
                    // add_ref(2){creator,attached} → schedule_record_resume adds the
                    // scheduled-resumer ref → release creator here → attached + scheduled
                    // freed by async_lock's tail + the runner → 0. (research D-3.)
                    try {
                        inherited_slot.assign([&awaiter](asio::cancellation_type type) noexcept {
                            awaiter.on_cancel(type);
                        });
                    } catch (...) {
                        record->result_ = expected_t<async_lock_guard>{
                            std::unexpected(fixpp::core::error::sync_lock_alloc_failed)};
                        record->phase_.store(waiter_phase::cancelled, std::memory_order_release);
                        schedule_record_resume(record);
                        waiter_record::release_ref(record);  // creator
                        return;
                    }
                }
            }

            if (draining_.load(std::memory_order_acquire)) {
                record->result_ = expected_t<async_lock_guard>{
                    std::unexpected(fixpp::core::error::sync_lock_drained)};
                record->phase_.store(waiter_phase::cancelled, std::memory_order_release);
                schedule_record_resume(record);
                waiter_record::release_ref(record);  // creator
                return;
            }

#ifdef FIXPP_ASYNC_MUTEX_TEST_SEAM
            // 058 Gate-B MAJOR-2: see async_mutex_seam_phase::acq_pre_state_reload.
            if (detail::async_mutex_test_seam) {
                detail::async_mutex_test_seam(detail::async_mutex_seam_phase::acq_pre_state_reload);
            }
#endif
            uintptr_t old_state = state_.load(std::memory_order_acquire);

            while (true) {
                if (old_state == not_locked) {
#ifdef FIXPP_ASYNC_MUTEX_TEST_SEAM
                    // 058 Gate-B MAJOR-2: see
                    // async_mutex_seam_phase::acq_pre_notlocked_cas.
                    if (detail::async_mutex_test_seam) {
                        detail::async_mutex_test_seam(
                            detail::async_mutex_seam_phase::acq_pre_notlocked_cas);
                    }
#endif
                    // 058 Gate-B MAJOR-2 fix: CAS directly on `old_state` (not a
                    // fresh local) so a failed CAS refreshes `old_state` with the
                    // actual current value. Pre-fix, a fresh `exp2 = not_locked`
                    // local was discarded on failure, leaving the outer
                    // `old_state` permanently stale at `not_locked` — the loop
                    // then retried the same doomed not_locked -> locked_no_waiters
                    // CAS forever instead of re-observing the real (now-held)
                    // state and falling through to the queueing branch below
                    // (busy-spin livelock; deadlock on a shared/oversubscribed
                    // single-thread executor). See
                    // test_async_mutex_acquire_livelock.cpp.
                    if (state_.compare_exchange_weak(old_state, locked_no_waiters,
                                                     std::memory_order_acquire,
                                                     std::memory_order_acquire)) {
                        active_holders_count_.fetch_add(1, std::memory_order_relaxed);
                        record->phase_.store(waiter_phase::granted, std::memory_order_release);
                        record->result_ = expected_t<async_lock_guard>{async_lock_guard{this}};
                        schedule_record_resume(record);
                        waiter_record::release_ref(record);  // creator
                        return;
                    }
                    continue;
                }

                if (old_state != locked_no_waiters) {
                    record->next_ = reinterpret_cast<waiter_record*>(old_state);
                } else {
                    record->next_ = nullptr;
                }

                waiter_record::add_ref(record);  // list membership
                if (state_.compare_exchange_weak(old_state, reinterpret_cast<uintptr_t>(record),
                                                 std::memory_order_release,
                                                 std::memory_order_acquire)) {
                    waiter_record::release_ref(record);  // creator
                    return;
                }
                waiter_record::release_ref(record);  // failed membership attempt
            }
        },
        asio::use_awaitable);

    // Restore the caller coroutine's default (terminal-only) cancellation
    // filter. async_lock()'s total-cancel enablement (set above for the
    // acquisition) MUST be scoped strictly to this operation: per
    // [2f §4.2.3] / §4.5.1 window 3 a stale/late `total` arriving after the
    // operation has completed (e.g. post-grant) is a no-op and must NOT abort
    // the caller's subsequent awaits. Leaving the filter mutated leaked
    // `operation_aborted` into the caller (seam #17 LateSignal/GrantOrCancel).
    co_await asio::this_coro::reset_cancellation_state(asio::enable_terminal_cancellation{});

    if (awaiter.record_ != nullptr) {
        auto* record = awaiter.record_;
        record->attached_awaiter_.store(nullptr, std::memory_order_release);
        detail::waiter_record::release_ref(record);
        awaiter.record_ = nullptr;
    }
    co_return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// T030: unlock — drain walker.
// [2f §4.5.2], data-model E1, I-04..I-18.
//
// On entry: the caller holds the lock (was granted by fast-path CAS or by
// unlock's drain walker).  We must find the next waiter (if any) and grant
// it the lock, or set state_ back to not_locked if the list is empty.
//
// 048 (Erratum E-5): Under draining_, the latch->notify() call is REMOVED
// (the latch is gone). The drain loop re-reaps both lists after yielding,
// so unlock() need only do the CAS (which it already does) and return.
//
// Erratum E-1: "resume the waiter" is now awaiter->invoke_handler(result)
// instead of resume_fn_(result). The awaiter node is frame-local; do NOT
// delete it — it lives in async_lock()'s coroutine frame.
// ─────────────────────────────────────────────────────────────────────────────

inline void fixpp::sync::async_mutex::unlock() noexcept {
    using detail::waiter_phase;
    using detail::waiter_record;

    active_holders_count_.fetch_sub(1, std::memory_order_relaxed);

    if (draining_.load(std::memory_order_acquire)) {
        // 048: Under draining_, do not splice; only restore state_ to not_locked
        // so the drain's terminal CAS succeeds. The drain loop re-reaps any
        // residuals the holder may have had. No latch->notify() (latch removed).
        uintptr_t expected = locked_no_waiters;
        state_.compare_exchange_strong(expected, not_locked, std::memory_order_acq_rel,
                                       std::memory_order_acquire);
        return;
    }

    waiter_record* head_residual = next_drain_head_.exchange(nullptr, std::memory_order_acq_rel);

    if (head_residual != nullptr) {
        waiter_record* cur = head_residual;
        while (cur != nullptr) {
            waiter_phase ph = cur->phase_.load(std::memory_order_acquire);
            if (ph == waiter_phase::queued) {
                waiter_phase expected_ph = waiter_phase::queued;
#ifdef FIXPP_ASYNC_MUTEX_TEST_SEAM
                // 058 Gate-B MAJOR-1: see
                // async_mutex_seam_phase::unlock_pre_grant_cas_residual.
                if (detail::async_mutex_test_seam) {
                    detail::async_mutex_test_seam(
                        detail::async_mutex_seam_phase::unlock_pre_grant_cas_residual);
                }
#endif
                if (cur->phase_.compare_exchange_strong(expected_ph, waiter_phase::granted,
                                                        std::memory_order_acq_rel,
                                                        std::memory_order_acquire)) {
                    active_holders_count_.fetch_add(1, std::memory_order_relaxed);
                    cur->result_ = expected_t<async_lock_guard>{async_lock_guard{this}};
                    auto* tail = cur->next_;
                    cur->next_ = nullptr;
                    if (tail != nullptr) push_residual(next_drain_head_, tail);
                    detail::waiter_record::release_ref(cur);  // list membership
                    schedule_record_resume(cur);
                    return;
                }
                ph = expected_ph;
            }

            if (ph == waiter_phase::cancelled) {
                waiter_record* nxt = cur->next_;
                cur->next_ = nullptr;
                detail::waiter_record::release_ref(cur);  // list membership
                cur = nxt;
            } else {
                // 058 T023 (research.md D-5, spec FR-005 / AM-P3-1): `ph` here
                // can only be `queued` (handled above, and which always
                // returns on a successful CAS) or `cancelled` (handled
                // above); a `granted` record is structurally impossible — the
                // ONLY site that transitions a record's phase_ to `granted`
                // is the CAS immediately above, which unchains it (splices
                // the tail to `next_drain_head_`) and returns in the same
                // breath, never leaving a granted record for a later pass of
                // this walk to observe. unlock() is also holder-serialized
                // (only one unlock() executes per mutex at a time), so no
                // concurrent walk can race this one. Trap loudly instead of
                // silently stepping past a corrupted invariant.
                assert(false &&
                       "async_mutex: granted record observed mid chain-walk "
                       "(residual list) — impossible invariant break");
                std::terminate();
            }
        }
    }

    uintptr_t state_snapshot = state_.exchange(locked_no_waiters, std::memory_order_acq_rel);

    if (state_snapshot == not_locked || state_snapshot == locked_no_waiters) {
#ifdef FIXPP_ASYNC_MUTEX_TEST_SEAM
        // 058 T046 (F4): pin here so a seam-installed hook can force a
        // concurrent waiter's queuing CAS into the window between the
        // exchange above and the terminal CAS below. No-op unless the
        // standalone test_async_mutex_terminal_cas_recursive_unlock target
        // defines the macro and installs a non-null hook.
        if (detail::async_mutex_test_seam) {
            detail::async_mutex_test_seam(
                detail::async_mutex_seam_phase::unlock_pre_terminal_cas_fast);
        }
#endif
        uintptr_t expected2 = locked_no_waiters;
        if (!state_.compare_exchange_strong(expected2, not_locked, std::memory_order_acq_rel,
                                            std::memory_order_acquire)) {
            active_holders_count_.fetch_add(1, std::memory_order_relaxed);
            unlock();
        }
        return;
    }

    auto* lifo_head = reinterpret_cast<waiter_record*>(state_snapshot);
    waiter_record* fifo_head = nullptr;
    {
        auto* cur = lifo_head;
        while (cur != nullptr) {
            auto* nxt = cur->next_;
            cur->next_ = fifo_head;
            fifo_head = cur;
            cur = nxt;
        }
    }

    waiter_record* fifo_cur = fifo_head;
    while (fifo_cur != nullptr) {
        waiter_phase ph = fifo_cur->phase_.load(std::memory_order_acquire);
        if (ph == waiter_phase::queued) {
            waiter_phase expected_ph = waiter_phase::queued;
#ifdef FIXPP_ASYNC_MUTEX_TEST_SEAM
            // 058 Gate-B MAJOR-1: see
            // async_mutex_seam_phase::unlock_pre_grant_cas_fifo.
            if (detail::async_mutex_test_seam) {
                detail::async_mutex_test_seam(
                    detail::async_mutex_seam_phase::unlock_pre_grant_cas_fifo);
            }
#endif
            if (fifo_cur->phase_.compare_exchange_strong(expected_ph, waiter_phase::granted,
                                                         std::memory_order_acq_rel,
                                                         std::memory_order_acquire)) {
                active_holders_count_.fetch_add(1, std::memory_order_relaxed);
                fifo_cur->result_ = expected_t<async_lock_guard>{async_lock_guard{this}};
                auto* tail = fifo_cur->next_;
                fifo_cur->next_ = nullptr;
                if (tail != nullptr) push_residual(next_drain_head_, tail);
                detail::waiter_record::release_ref(fifo_cur);  // list membership
                schedule_record_resume(fifo_cur);
                return;
            }
            ph = expected_ph;
        }

        if (ph == waiter_phase::cancelled) {
            waiter_record* nxt = fifo_cur->next_;
            fifo_cur->next_ = nullptr;
            detail::waiter_record::release_ref(fifo_cur);  // list membership
            fifo_cur = nxt;
        } else {
            // 058 T023 (research.md D-5, spec FR-005 / AM-P3-1): sibling trap
            // for the fresh LIFO->FIFO walk — same reasoning as the residual
            // walk's trap above: `granted` is set only by the CAS immediately
            // above (which unchains and returns), and unlock() is
            // holder-serialized, so a `granted` record here is structurally
            // impossible. Trap loudly rather than silently stepping past it.
            assert(false &&
                   "async_mutex: granted record observed mid chain-walk "
                   "(FIFO list) — impossible invariant break");
            std::terminate();
        }
    }

#ifdef FIXPP_ASYNC_MUTEX_TEST_SEAM
    // 058 T046 (F6): pin here so a seam-installed hook can force a
    // concurrent waiter's queuing CAS into the window between the exchange
    // above and this terminal CAS, after the FIFO walk exhausts an
    // all-cancelled chain (no grant, so no early return above).
    if (detail::async_mutex_test_seam) {
        detail::async_mutex_test_seam(detail::async_mutex_seam_phase::unlock_pre_terminal_cas_fifo);
    }
#endif
    uintptr_t expected3 = locked_no_waiters;
    if (!state_.compare_exchange_strong(expected3, not_locked, std::memory_order_acq_rel,
                                        std::memory_order_acquire)) {
        active_holders_count_.fetch_add(1, std::memory_order_relaxed);
        unlock();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// cancel_and_drain — 048 (Erratum E-5): unified strand-local quiescence loop.
//
// Design: research.md D-2; data-model.md state diagram; contracts/async_mutex-contract.md.
//
// Terminal condition (INV-D): (active_holders_count_==0) AND
//   (in_flight_resumers_==0) AND (both lists empty in one pass).
// in_flight_resumers_ is the UAF barrier — the mutex must NOT be destroyed
// until every posted resumer has decremented it.
//
// Reentrant caller: observes draining_ already set, awaits draining_complete_,
//   then returns expected_t<void>{} — NOT ok eagerly (fixes P1-2).
//
// The drain is UNINTERRUPTIBLE: co_await disable_cancellation() at entry so
// teardown always runs to completion (contract E-5; matches Session::close's
// disable_cancellation, session.cpp:1334).
// ─────────────────────────────────────────────────────────────────────────────
inline asio::awaitable<fixpp::sync::expected_t<void>>
fixpp::sync::async_mutex::cancel_and_drain() noexcept {
    using detail::waiter_phase;
    using detail::waiter_record;

    // Step 1: disable own cancellation — teardown runs to completion (E-5).
    co_await asio::this_coro::reset_cancellation_state(asio::disable_cancellation{});

    // Step 2: idempotent / reentrant fast path.
    // If already draining_ (set by the first caller), this is a reentrant
    // caller on the strand. It must NOT return ok eagerly — it awaits the
    // first drain's terminal completion and then returns the terminal result
    // (fixes P1-2 false-success — research.md D-2 step 1).
    if (draining_.load(std::memory_order_acquire)) {
        auto ex = co_await asio::this_coro::executor;
        while (!draining_complete_.load(std::memory_order_acquire)) {
            co_await asio::post(ex, asio::use_awaitable);
        }
        co_return expected_t<void>{};
    }

    // Step 3: first-caller election — only ONE becomes the reaper.
    // (drain_in_progress_ is kept for potential future multi-caller paths;
    // on the owning strand only one caller runs at a time, so this always
    // succeeds for the first call and the reentrant path above handles others.)
    if (drain_in_progress_.test_and_set(std::memory_order_acq_rel)) {
        // Should not happen on a single owning strand, but handle defensively:
        // the reentrant path above catches draining_==true; if we somehow
        // reach here, yield until draining_ is set, then await completion.
        auto ex = co_await asio::this_coro::executor;
        while (!draining_.load(std::memory_order_acquire)) {
            co_await asio::post(ex, asio::use_awaitable);
        }
        while (!draining_complete_.load(std::memory_order_acquire)) {
            co_await asio::post(ex, asio::use_awaitable);
        }
        co_return expected_t<void>{};
    }

    // Step 4: set draining_ — gates new acquirers (fast-fail sync_lock_drained
    // at async_lock entry :780/:868). Published BEFORE the reap so a new
    // acquirer entering after this store is guaranteed to fast-fail.
    auto bound_ex = co_await asio::this_coro::executor;
    draining_.store(true, std::memory_order_release);

    // ── Helpers ────────────────────────────────────────────────────────────

    auto reverse_lifo = [](waiter_record* head) -> waiter_record* {
        waiter_record* prev = nullptr;
        while (head) {
            auto* n = head->next_;
            head->next_ = prev;
            prev = head;
            head = n;
        }
        return prev;
    };

    // reap_chain: for each waiter, CAS queued→cancelled (single-winner with
    // on_cancel); winner sets result_=sync_lock_aborted and schedules a posted
    // resume (incrementing in_flight_resumers_ inside schedule_record_resume).
    // Loser drops list membership only (on_cancel or grant already owns it).
    auto reap_chain = [&](waiter_record* chain) {
        while (chain != nullptr) {
            auto* next = chain->next_;
            waiter_phase expected = waiter_phase::queued;
            if (chain->phase_.compare_exchange_strong(expected, waiter_phase::cancelled,
                                                      std::memory_order_acq_rel,
                                                      std::memory_order_acquire)) {
                chain->result_ = expected_t<async_lock_guard>{
                    std::unexpected(fixpp::core::error::sync_lock_aborted)};
                detail::waiter_record::release_ref(chain);  // list membership
                schedule_record_resume(chain);  // ++in_flight_resumers_ inside
            } else {
                // CAS lost: granted (holder will quiesce) or the waiter's
                // own on_cancel beat the reaper. Drop list membership only.
                detail::waiter_record::release_ref(chain);
            }
            chain = next;
        }
    };

    // reap_both_lists: exchange both atomic list heads out synchronously and
    // reap each chain. Returns true if BOTH lists were empty this pass.
    auto reap_both_lists = [&]() -> bool {
        auto raw_state = state_.exchange(locked_no_waiters, std::memory_order_acq_rel);
        auto* lifo_head = (raw_state == not_locked || raw_state == locked_no_waiters)
                              ? nullptr
                              : reinterpret_cast<waiter_record*>(raw_state);
        auto* fifo_head = next_drain_head_.exchange(nullptr, std::memory_order_acq_rel);
        bool both_empty = (lifo_head == nullptr && fifo_head == nullptr);
        reap_chain(reverse_lifo(lifo_head));
        reap_chain(fifo_head);
        return both_empty;
    };

    // ── Step 5: unified quiescence loop ────────────────────────────────────
    //
    // Each pass: reap both lists (catches waiters the pre-drain holder spliced
    // after unlock()); then check the FULL terminal condition (INV-D). If not
    // terminal, yield so posted resumers run (--in_flight_resumers_) and a
    // pre-drain holder's unlock() runs. The precondition (holders release
    // promptly) guarantees termination.
    //
    // INV-D: terminal = (holders==0) AND (resumers==0) AND (lists empty).
    // Must NOT terminate on active_holders_count_==0 alone — in_flight_resumers_
    // is the UAF barrier (research.md D-2; fixes P1-1).
    // 058 T016 (research.md D-2): ACQUIRE — paired with the RELEASE decrement
    // in the resume runner (:626-ish, `fetch_sub(..., release)`). Observing 0
    // here happens-before every write the runner made into mutex-owned pool
    // storage, so a caller that destroys the mutex immediately after this
    // drain returns cannot race those writes (contracts/async_mutex-
    // contract-delta.md; scoped to the parked-then-reaped case — see the
    // destructor's own comment for the granted-holder EXCLUSION).
    for (;;) {
        bool both_empty = reap_both_lists();
        if (active_holders_count_.load(std::memory_order_relaxed) == 0 &&
            in_flight_resumers_.load(std::memory_order_acquire) == 0 && both_empty) {
            break;
        }
        co_await asio::post(bound_ex, asio::use_awaitable);
    }

    // ── Step 6: finalize ───────────────────────────────────────────────────
    //
    // CAS state_ locked_no_waiters→not_locked (the drain holds the lock since
    // the initial exchange; restore the free state for the destructor invariant).
    // THEN store draining_complete_=true (release, ordered AFTER state_ store,
    // no co_await between them) — a reentrant caller observing draining_complete_
    // also observes state_==not_locked (research.md D-2 step 5).
    uintptr_t expected_state = locked_no_waiters;
    bool finalized = state_.compare_exchange_strong(expected_state, not_locked,
                                                    std::memory_order_acq_rel,
                                                    std::memory_order_acquire);
    // P3-2 (Gate B): the loop only breaks once both lists are empty in a pass, so the
    // drain holds the lock (locked_no_waiters) at this point and the CAS is invariant.
    // Assert it — an internal regression must NOT publish draining_complete_ while
    // state_ is non-terminal (which would return ok with a still-locked mutex).
    assert(finalized && "cancel_and_drain finalize: state_ was not locked_no_waiters");
    (void)finalized;
    draining_complete_.store(true, std::memory_order_release);
    co_return expected_t<void>{};
}
