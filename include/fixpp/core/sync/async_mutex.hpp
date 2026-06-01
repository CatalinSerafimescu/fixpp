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
//     - lazy drain_latch_state via atomic shared_ptr (RC-β);
//     - active_holders_count_ / active_acquirers_count_ epoch counters
//       (RC-α); and
//     - a PMR-aware slot_allocator for the cancellation handler closure
//       (RC-C).
//   The algorithm core (LIFO push / exchange-based drain / FIFO grant) is
//   due to Lewis Baker / cppcoro; all post-RC additions are original work.
// ─────────────────────────────────────────────────────────────────────────────
//
// Design anchor: .specify/2f-async-mutex.md v1.6 (errata E-1..E-4)
// Data model:    specs/006-async-mutex/data-model.md
// Contracts:     specs/006-async-mutex/contracts/async_mutex.hpp
//                specs/006-async-mutex/contracts/async_mutex_awaiter.hpp
//                specs/006-async-mutex/contracts/async_lock_guard.hpp
//                specs/006-async-mutex/contracts/drain_latch_state.hpp
//                specs/006-async-mutex/contracts/completion_policy.hpp
//
// Erratum E-1 (2026-05-18): The async_mutex_awaiter is a frame-local variable
// inside async_lock()'s own coroutine frame — NOT separately heap-allocated via
// global operator new. The asio completion handler produced by use_awaitable is
// stored via placement-new into the awaiter's inline slot_storage_ buffer (32 B).
// This achieves zero global heap allocation on both the uncontended and contended
// paths when mr==nullptr and HALO fires (§4.3.4 case 1).

#pragma once

#include <array>
#include <atomic>
#include <coroutine>
#include <cstddef>
#include <cstdint>
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
#include <asio/experimental/concurrent_channel.hpp>
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

// Forward declaration of drain_latch_state (full skeleton below).
class drain_latch_state;

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
    // Fires in BOTH debug AND release if the mutex is held OR waiters present.
    // Callers MUST drain via cancel_and_drain() before destruction.
    // T050 (US3) finalizes the destructor; US1 checks only the state_ sentinel.
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

    struct waiter_pool_slot {
        alignas(std::max_align_t) std::byte storage[waiter_record_storage_size_];
    };

    std::array<waiter_pool_slot, waiter_pool_capacity_> waiter_pool_storage_{};
    std::atomic<std::uint32_t> waiter_pool_next_{0};
    std::atomic<detail::waiter_record*> waiter_pool_free_{nullptr};

    // RC-B v1.1 — drain flag; set by cancel_and_drain(), never cleared.
    // I-13..I-16 ordering sites.
    std::atomic<bool> draining_{false};

    // RC-B v1.1 — concurrent-call serialiser for cancel_and_drain().
    std::atomic_flag drain_in_progress_ = ATOMIC_FLAG_INIT;

    // v1.2 / v1.3 RC-α — winner-only post-CAS holder count.
    // I-17..I-19 ordering sites.
    std::atomic<std::uint32_t> active_holders_count_{0};

    // NEW v1.3 RC-α — in-flight acquirer epoch counter.
    // I-20..I-22 ordering sites.
    std::atomic<std::uint32_t> active_acquirers_count_{0};

    // NEW v1.3 RC-β; UPDATED v1.4 — lazy drain latch.
    // NOT lock-free in general; cold path only (cancel_and_drain invocation).
    // I-23..I-24 ordering sites.
    std::atomic<std::shared_ptr<detail::drain_latch_state>> drain_latch_ptr_;

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
// T010: E4 — drain_latch_state skeleton
// Source: [2f §4.7.2/§4.7.3], data-model E4, contracts/drain_latch_state.hpp
//
// Body is stubbed; T048 (US3) fills them in.
// ─────────────────────────────────────────────────────────────────────────────

// T048 (US3): channel-backed multi-waiter latch ([2f §4.7.3] I-4/I-7/I-8).
// `released_`/`aborted_` are the terminal observer flags (I-25/I-26/I-27);
// the `concurrent_channel` is the multi-waiter wake surface. `notify()` is a
// non-terminal re-check wake (I-8); `signal_release()`/`signal_abort()` are the
// two mutually-terminal idempotent edges (I-7) and `close()` the channel so
// EVERY parked/future subscriber wakes. Executor is captured lazily from the
// reaper's frame (I-1/I-3; keeps `async_mutex()` constexpr + executor-free).
class drain_latch_state {
public:
    explicit drain_latch_state(asio::any_io_executor ex) : channel_(ex, 1) {}

    std::atomic<bool> released_{false};
    std::atomic<bool> aborted_{false};
    std::atomic<std::uint32_t> in_flight_resumptions_{0};

    // Non-terminal wake: re-check counters (I-8). Idempotent, never blocks;
    // buffer size 1 coalesces bursts and prevents the lost-wakeup window
    // between the reaper's counter read and its park on wait().
    void notify() noexcept { channel_.try_send(asio::error_code{}); }

    // Terminal: drain committed (I-7). close() completes every pending and
    // future async_receive so all subscribers wake exactly once.
    void signal_release() noexcept {
        released_.store(true, std::memory_order_release);
        channel_.close();
    }

    // Terminal: reaper itself cancelled (I-5/I-7).
    void signal_abort() noexcept {
        aborted_.store(true, std::memory_order_release);
        channel_.close();
    }

    // The cancellable park. Returned DIRECTLY (not via a child coroutine) so
    // that `co_await latch->async_wait()` IS the cancellable suspension: with
    // as_tuple, a propagated total cancellation is delivered as
    // `ec == operation_aborted` VALUE (no thrown exception, no nested-awaitable
    // rethrow). channel_closed (terminal signal) / a notify() token arrive as
    // a different ec; the caller then re-checks released_/aborted_.
    auto async_wait() { return channel_.async_receive(asio::as_tuple(asio::use_awaitable)); }

private:
    asio::experimental::concurrent_channel<void(asio::error_code)> channel_;
};

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

    using resume_fn_t = void (*)(void*, waiter_record*,
                                 std::shared_ptr<drain_latch_state>) noexcept;
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

    void destroy_handler() noexcept {
        if (destroy_fn_) {
            destroy_fn_(slot_storage_.data());
            invoke_fn_ = nullptr;
            destroy_fn_ = nullptr;
        }
    }

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
        resume_fn_ = [](void* storage, waiter_record* record,
                        std::shared_ptr<drain_latch_state> latch) noexcept {
            auto* exec = std::launder(reinterpret_cast<RawExecutor*>(storage));
            auto runner = [record, latch = std::move(latch)]() mutable {
                auto* awaiter = record->attached_awaiter_.load(std::memory_order_acquire);
                if (awaiter != nullptr) {
                    awaiter->slot_.clear();
                    awaiter->invoke_handler(std::move(record->result_));
                }
                if (latch) {
                    latch->in_flight_resumptions_.fetch_sub(1, std::memory_order_acq_rel);
                    latch->notify();
                }
                release_ref(record);
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
static_assert(sizeof(waiter_record) <= 256,
              "fixpp::sync: waiter_record exceeds waiter_pool storage budget.");

// T060: §1.1 / §6.4 awaiter byte budget — HALO-eligibility precondition.
// The frame-local awaiter (Erratum E-2 split: intrusive identity moved to
// waiter_record) must stay within the published ≤ 96 B ceiling so it fits in
// the caller's coroutine-frame free space and the HALO elision (§6.4, seam #9)
// remains viable. async_lock's await_ready/await_suspend equivalents are the
// inline header-only async_initiate lambda below — no out-of-line escape.
static_assert(sizeof(async_mutex_awaiter) <= 96,
              "fixpp::sync: async_mutex_awaiter exceeds the §1.1 ≤ 96 B HALO budget.");

}  // namespace detail

}  // namespace fixpp::sync

// ─────────────────────────────────────────────────────────────────────────────
// T026/T027/T028/T029/T030/T031 — Out-of-line method bodies.
// ─────────────────────────────────────────────────────────────────────────────

// T050 (US3) final: fire terminate if held or waiters present.
// US1: fires terminate if state_ != not_locked (mutex is held or has waiters).
inline fixpp::sync::async_mutex::~async_mutex() noexcept(false) {
    uintptr_t s = state_.load(std::memory_order_acquire);
    if (s != not_locked || next_drain_head_.load(std::memory_order_acquire) != nullptr) {
        std::terminate();
    }
}

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
        auto* node = reinterpret_cast<waiter_record*>(raw);
        auto* expected = mutex->waiter_pool_free_.load(std::memory_order_relaxed);
        do {
            node->next_ = expected;
        } while (!mutex->waiter_pool_free_.compare_exchange_weak(
            expected, node, std::memory_order_release, std::memory_order_relaxed));
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

inline void schedule_record_resume(
    fixpp::sync::detail::waiter_record* record,
    std::shared_ptr<fixpp::sync::detail::drain_latch_state> latch = {}) noexcept {
    using record_t = fixpp::sync::detail::waiter_record;
    record_t::add_ref(record);  // scheduled resumer
    if (latch) {
        latch->in_flight_resumptions_.fetch_add(1, std::memory_order_acq_rel);
    }
    record->resume_fn_(record->exec_storage_.data(), record, std::move(latch));
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
// [2f §4.1], [2f §4.2], [2f §4.2.1], [2f §4.2.2], I-20.
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

    active_acquirers_count_.fetch_add(1, std::memory_order_acq_rel);
    auto result = co_await asio::async_initiate<const asio::use_awaitable_t<>&,
                                                void(expected_t<async_lock_guard>)>(
        [this, &awaiter, mr, bound_executor, inherited_slot](auto handler) mutable {
            if (draining_.load(std::memory_order_acquire)) {
                active_acquirers_count_.fetch_sub(1, std::memory_order_acq_rel);
                std::move(handler)(expected_t<async_lock_guard>{
                    std::unexpected(fixpp::core::error::sync_lock_drained)});
                return;
            }

            // Step 2: fast-path CAS not_locked → locked_no_waiters (I-01, §4.2.1 step 2).
            uintptr_t expected_state = not_locked;
            if (state_.compare_exchange_strong(expected_state, locked_no_waiters,
                                               std::memory_order_acquire,
                                               std::memory_order_relaxed)) {
                active_holders_count_.fetch_add(1, std::memory_order_acq_rel);
                active_acquirers_count_.fetch_sub(1, std::memory_order_acq_rel);
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

                auto* free_head = waiter_pool_free_.load(std::memory_order_acquire);
                while (free_head != nullptr) {
                    auto* next = free_head->next_;
                    if (waiter_pool_free_.compare_exchange_weak(free_head, next,
                                                                std::memory_order_acq_rel,
                                                                std::memory_order_acquire)) {
                        return std::launder(reinterpret_cast<waiter_record*>(free_head));
                    }
                }

                auto slot = waiter_pool_next_.fetch_add(1, std::memory_order_acq_rel);
                if (slot >= waiter_pool_capacity_) {
                    return nullptr;
                }
                return std::launder(
                    reinterpret_cast<waiter_record*>(waiter_pool_storage_[slot].storage));
            }();
            if (record == nullptr) {
                active_acquirers_count_.fetch_sub(1, std::memory_order_acq_rel);
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
                    active_acquirers_count_.fetch_sub(1, std::memory_order_acq_rel);
                    waiter_record::release_ref(record);
                    waiter_record::release_ref(record);
                    std::move(handler)(expected_t<async_lock_guard>{
                        std::unexpected(fixpp::core::error::sync_lock_alloc_failed)});
                    return;
                }

                awaiter.store_handler(std::move(handler));

                if (inherited_slot.is_connected()) {
                    awaiter.slot_ = inherited_slot;
                    inherited_slot.assign([&awaiter](asio::cancellation_type type) noexcept {
                        awaiter.on_cancel(type);
                    });
                }
            }

            if (draining_.load(std::memory_order_acquire)) {
                active_acquirers_count_.fetch_sub(1, std::memory_order_acq_rel);
                record->result_ = expected_t<async_lock_guard>{
                    std::unexpected(fixpp::core::error::sync_lock_drained)};
                record->phase_.store(waiter_phase::cancelled, std::memory_order_release);
                schedule_record_resume(record);
                waiter_record::release_ref(record);  // creator
                return;
            }

            uintptr_t old_state = state_.load(std::memory_order_acquire);

            while (true) {
                if (old_state == not_locked) {
                    uintptr_t exp2 = not_locked;
                    if (state_.compare_exchange_weak(exp2, locked_no_waiters,
                                                     std::memory_order_acquire,
                                                     std::memory_order_acquire)) {
                        active_holders_count_.fetch_add(1, std::memory_order_acq_rel);
                        active_acquirers_count_.fetch_sub(1, std::memory_order_acq_rel);
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
                    active_acquirers_count_.fetch_sub(1, std::memory_order_acq_rel);
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
// Erratum E-1: "resume the waiter" is now awaiter->invoke_handler(result)
// instead of resume_fn_(result). The awaiter node is frame-local; do NOT
// delete it — it lives in async_lock()'s coroutine frame.
// ─────────────────────────────────────────────────────────────────────────────

inline void fixpp::sync::async_mutex::unlock() noexcept {
    using detail::waiter_phase;
    using detail::waiter_record;

    active_holders_count_.fetch_sub(1, std::memory_order_acq_rel);

    if (draining_.load(std::memory_order_acquire)) {
        uintptr_t expected = locked_no_waiters;
        state_.compare_exchange_strong(expected, not_locked, std::memory_order_acq_rel,
                                       std::memory_order_acquire);
        if (auto latch = drain_latch_ptr_.load(std::memory_order_acquire)) {
            latch->notify();
        }
        return;
    }

    waiter_record* head_residual = next_drain_head_.exchange(nullptr, std::memory_order_acq_rel);

    if (head_residual != nullptr) {
        waiter_record* cur = head_residual;
        while (cur != nullptr) {
            waiter_phase ph = cur->phase_.load(std::memory_order_acquire);
            if (ph == waiter_phase::queued) {
                waiter_phase expected_ph = waiter_phase::queued;
                if (cur->phase_.compare_exchange_strong(expected_ph, waiter_phase::granted,
                                                        std::memory_order_acq_rel,
                                                        std::memory_order_acquire)) {
                    active_holders_count_.fetch_add(1, std::memory_order_acq_rel);
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
                cur = cur->next_;
            }
        }
    }

    uintptr_t state_snapshot = state_.exchange(locked_no_waiters, std::memory_order_acq_rel);

    if (state_snapshot == not_locked || state_snapshot == locked_no_waiters) {
        uintptr_t expected2 = locked_no_waiters;
        if (!state_.compare_exchange_strong(expected2, not_locked, std::memory_order_acq_rel,
                                            std::memory_order_acquire)) {
            active_holders_count_.fetch_add(1, std::memory_order_acq_rel);
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
            if (fifo_cur->phase_.compare_exchange_strong(expected_ph, waiter_phase::granted,
                                                         std::memory_order_acq_rel,
                                                         std::memory_order_acquire)) {
                active_holders_count_.fetch_add(1, std::memory_order_acq_rel);
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
            fifo_cur = fifo_cur->next_;
        }
    }

    uintptr_t expected3 = locked_no_waiters;
    if (!state_.compare_exchange_strong(expected3, not_locked, std::memory_order_acq_rel,
                                        std::memory_order_acquire)) {
        active_holders_count_.fetch_add(1, std::memory_order_acq_rel);
        unlock();
    }
}

// T049 (US3): cancel_and_drain reaper — [2f §4.7.2] steps (a)–(j), translated
// onto the Erratum-E-2 waiter_record model + Erratum-E-3 posted resumption.
inline asio::awaitable<fixpp::sync::expected_t<void>>
fixpp::sync::async_mutex::cancel_and_drain() noexcept {
    using detail::waiter_phase;
    using detail::waiter_record;

    // Subscriber: park on the epoch latch until a terminal edge; map
    // released_→ok, aborted_→sync_lock_aborted. Loops over non-terminal
    // notify() wakes ([2f §4.7.3] I-8); a cancelled own-wait → aborted.
    auto subscribe =
        [](std::shared_ptr<detail::drain_latch_state> st) -> asio::awaitable<expected_t<void>> {
        while (!st->released_.load(std::memory_order_acquire) &&
               !st->aborted_.load(std::memory_order_acquire)) {
            auto [ec] = co_await st->async_wait();
            if (ec == asio::error::operation_aborted)
                co_return std::unexpected(fixpp::core::error::sync_lock_aborted);
        }
        if (st->aborted_.load(std::memory_order_acquire))
            co_return std::unexpected(fixpp::core::error::sync_lock_aborted);
        co_return expected_t<void>{};
    };

    // ── (a) Idempotent fast path. ────────────────────────────────────────
    if (draining_.load(std::memory_order_acquire)) {
        if (auto st = drain_latch_ptr_.load(std::memory_order_acquire))
            co_return co_await subscribe(std::move(st));
        // drain_latch_ptr_ is null only after a clean signal_release() epoch;
        // on the abort path the latch is kept published (aborted_==true) so
        // this branch is only reachable after a successful prior drain.
        co_return expected_t<void>{};
    }

    // ── (b) Concurrent-call serialiser — only the first becomes reaper. ───
    if (drain_in_progress_.test_and_set(std::memory_order_acq_rel)) {
        auto ex = co_await asio::this_coro::executor;
        while (!draining_.load(std::memory_order_acquire))
            co_await asio::post(ex, asio::use_awaitable);
        if (auto st = drain_latch_ptr_.load(std::memory_order_acquire))
            co_return co_await subscribe(std::move(st));
        // null only after a clean signal_release() epoch; abort path keeps the
        // latch published (aborted_==true) so this branch is only reachable
        // after a successful prior drain (consistent with F-2 fix above).
        co_return expected_t<void>{};
    }

    // ── (c) Lazy executor-bound latch; publish ptr BEFORE draining_ ──────
    //        (v1.4 ordering; [2f §4.7.3] I-1/I-3).
    auto bound_ex = co_await asio::this_coro::executor;
    auto latch = std::make_shared<detail::drain_latch_state>(bound_ex);
    drain_latch_ptr_.store(latch, std::memory_order_release);
    draining_.store(true, std::memory_order_release);

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
                schedule_record_resume(chain, latch);       // resumer ref++
            } else {
                // CAS lost: granted (holder will quiesce) or the waiter's
                // own on_cancel beat the reaper. Drop list membership only.
                detail::waiter_record::release_ref(chain);
            }
            chain = next;
        }
    };

    // ── (e)/(f) Exchange both lists out; reap LIFO (reversed → FIFO) ─────
    auto raw_state = state_.exchange(locked_no_waiters, std::memory_order_acq_rel);
    auto* lifo_head = (raw_state == not_locked || raw_state == locked_no_waiters)
                          ? nullptr
                          : reinterpret_cast<waiter_record*>(raw_state);
    auto* fifo_head = next_drain_head_.exchange(nullptr, std::memory_order_acq_rel);
    reap_chain(reverse_lifo(lifo_head));
    reap_chain(fifo_head);

    // ── (g) Stable re-walk until both lists observe null in one pass ─────
    //        (RC-α; unlock()'s splice is short-circuited under draining_).
    while (true) {
        auto raw_late = state_.exchange(locked_no_waiters, std::memory_order_acq_rel);
        auto* late_lifo = (raw_late == not_locked || raw_late == locked_no_waiters)
                              ? nullptr
                              : reinterpret_cast<waiter_record*>(raw_late);
        auto* late_fifo = next_drain_head_.exchange(nullptr, std::memory_order_acq_rel);
        if (!late_lifo && !late_fifo) break;
        reap_chain(reverse_lifo(late_lifo));
        reap_chain(late_fifo);
    }

    // ── (h) Wait for holders/acquirers/resumptions to quiesce. The reaper's
    //        OWN cancellation is observed via an explicit cancellation-slot
    //        handler (the same proven mechanism as the awaiter's on_cancel) —
    //        NOT by relying on the channel honouring the coroutine slot. The
    //        handler runs synchronously on cancel, sets a flag, and
    //        signal_abort()s (closes the channel) so the parked async_wait()
    //        completes promptly as a VALUE (channel_closed via as_tuple — no
    //        thrown exception). [2f §4.7.3] I-5.
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation{});
    auto reaper_cs = co_await asio::this_coro::cancellation_state;
    auto reaper_slot = reaper_cs.slot();
    std::atomic<bool> reaper_cancelled{false};
    if (reaper_slot.is_connected()) {
        reaper_slot.assign([&reaper_cancelled, latch](asio::cancellation_type) noexcept {
            reaper_cancelled.store(true, std::memory_order_release);
            latch->signal_abort();
        });
    }
    // The reaper's own cancellation can be delivered to this co_await either
    // as a flag via the slot handler (channel closed → value), OR — when asio
    // propagates the parent's cancellation across the awaitable boundary — as
    // a thrown system_error{operation_aborted} at the co_await itself. Both
    // are converted to the §4.7.3 I-5 contract return (unexpected), never an
    // escaping exception (cancel_and_drain is noexcept).
    try {
        while (active_holders_count_.load(std::memory_order_acquire) != 0 ||
               active_acquirers_count_.load(std::memory_order_acquire) != 0 ||
               latch->in_flight_resumptions_.load(std::memory_order_acquire) != 0) {
            auto [ec] = co_await latch->async_wait();
            (void)ec;
            if (reaper_cancelled.load(std::memory_order_acquire)) break;
            // ec == {} (notify token) or channel_closed → re-check counters.
        }
    } catch (...) {
        reaper_cancelled.store(true, std::memory_order_release);
    }
    if (reaper_cancelled.load(std::memory_order_acquire)) {
        // Reaper itself cancelled — propagate ([2f §4.7.3] I-5). signal_abort()
        // wakes every subscriber; draining_ stays true; in-flight resumption
        // handlers retain the latch shared_ptr and finish. No trailing
        // co_await here (a cancellation may be latched in this state).
        //
        // F-2 fix (gate-b/r1): do NOT clear drain_latch_ptr_ here. The latch
        // stays published (aborted_==true) so any fresh reentrant
        // cancel_and_drain() call takes the subscribe() branch at the idempotent
        // fast path (hpp:1167-1170) and observes aborted_==true → returns
        // unexpected{sync_lock_aborted} instead of false success. This closes the
        // UAF: a reentrant caller can no longer return success while in-flight
        // resumption handlers still hold waiter_record refs and dereference the
        // mutex. Follows I-5/I-6/I-7 (binding invariants); the §4.7.2 sketch
        // comment "prior epoch published + cleared" was self-inconsistent with
        // I-5/I-7 on the abort path and is reconciled here: drain_latch_ptr_ is
        // cleared only on the release path (below), after signal_release().
        latch->signal_abort();
        if (reaper_slot.is_connected()) reaper_slot.clear();
        co_return std::unexpected(fixpp::core::error::sync_lock_aborted);
    }

    // Quiesced normally — disarm the cancel handler so a late total is a
    // no-op (it must not signal_abort() after we publish release).
    if (reaper_slot.is_connected()) reaper_slot.clear();

    // ── (i)/(j) Finalize: state_ → not_locked, publish release edge. ────
    uintptr_t expected_state = locked_no_waiters;
    state_.compare_exchange_strong(expected_state, not_locked, std::memory_order_acq_rel,
                                   std::memory_order_acquire);
    latch->signal_release();
    drain_latch_ptr_.store(nullptr, std::memory_order_release);
    co_return expected_t<void>{};
}
