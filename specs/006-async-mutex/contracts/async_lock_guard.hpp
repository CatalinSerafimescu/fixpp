// SPDX-License-Identifier: AGPL-3.0-or-later
//
// specs/006-async-mutex/contracts/async_lock_guard.hpp
//
// ─────────────────────────────────────────────────────────────────────────────
// SHAPE ORACLE (NOT THE BUILD HEADER)
// This file is a planning-phase contract artifact.
// It specifies the exact class shape that the implementation MUST match.
// It is NOT compiled into the library.
//
// Build header target: include/fixpp/core/sync/async_mutex.hpp
//   (async_lock_guard is defined in the fixpp::sync namespace within the main
//   async_mutex header)
// ─────────────────────────────────────────────────────────────────────────────
//
// Source of truth: .specify/2f-async-mutex.md v1.5 §4.4
//
// async_lock_guard — the RAII handle returned by async_lock's awaitable
// completion. Movable, non-copyable. Releases the mutex on destruction via
// async_mutex::unlock().
//
// sizeof(async_lock_guard) == sizeof(async_mutex*) == 8 B.
// Per [arch §5.5]: the guard does NOT own the mutex (the consumer that
// constructed the mutex is the owner).
//
// Key design decisions (v1.1 closes):
// - Engaged constructor is private + friend-only (Opus N-P3-1 close). The v1.0
//   public adopt-locked ctor + public try_lock() admitted a same-mutex aliasing
//   bug; v1.1 closes the surface. The only legal guard-construction path is
//   co_await m.async_lock(). Test seam #20 uses friend access for the fixture.
// - Move-assignment is DESTRUCTIVE (RC#1 / N-P1-3 close): moving into an
//   engaged guard unlocks the previously-owned mutex first.
// - [[clang::lifetimebound]] on the engaged constructor per [2b §6.4] /
//   [2b §6.6] precedent.
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

namespace fixpp::sync {

class async_mutex;

namespace detail {
class async_mutex_awaiter;
}  // namespace detail

class async_lock_guard {
public:
    // Default-constructed guard — disengaged; safe to move-into.
    async_lock_guard() noexcept = default;

    // Move ctor — source becomes empty.
    async_lock_guard(async_lock_guard&& other) noexcept;

    // Destructive move-assignment (RC#1 / N-P1-3 close).
    // If *this is engaged, unlock its mutex first; then take ownership of
    // other's mutex. Self-assignment is a no-op (this == &other guard).
    async_lock_guard& operator=(async_lock_guard&& other) noexcept;

    async_lock_guard(async_lock_guard const&)            = delete;
    async_lock_guard& operator=(async_lock_guard const&) = delete;

    // Destructor — calls mutex_->unlock() if engaged.
    ~async_lock_guard() noexcept;

    // Explicit early release. Disengages the guard and returns the
    // back-pointer; subsequent destruction is a no-op. Useful when the caller
    // wants to shorten the critical section.
    [[nodiscard]] async_mutex* release() noexcept;

    // Returns true iff the guard holds an engaged mutex pointer.
    [[nodiscard]] bool owns_lock() const noexcept;

private:
    // Engaged constructor — private + friend-only (Opus N-P3-1 close).
    // Called only from detail::async_mutex_awaiter::await_resume() and
    // from the §9 seam #20 test fixture (via friend access).
    // [[clang::lifetimebound]] — the guard MUST NOT outlive its mutex.
    explicit async_lock_guard(async_mutex* mutex
                              [[clang::lifetimebound]]) noexcept;

    friend class detail::async_mutex_awaiter;

    async_mutex* mutex_ {nullptr};
};

}  // namespace fixpp::sync

// ─────────────────────────────────────────────────────────────────────────────
// Usage contract (consumer):
//   auto guard_or_err = co_await mutex.async_lock();
//   if (!guard_or_err) co_return std::unexpected(map(guard_or_err.error()));
//   auto guard = std::move(*guard_or_err);  // RAII; releases on scope exit.
//   // ... critical section ...
//   // guard.release() if you need explicit early unlock.
// ─────────────────────────────────────────────────────────────────────────────
