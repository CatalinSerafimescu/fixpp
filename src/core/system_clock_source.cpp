// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/core/system_clock_source.cpp
//
// fixpp::core::system_clock_source out-of-line impl ([2d §4.2] / E2 /
// FR-002/FR-003). The intrusive in-flight-waiter list + its std::mutex live
// here (NOT the asio::awaitable header — [const §XI.3]). Each in-flight
// sleep_until owns a shared_ptr<steady_timer>; the list holds weak_ptrs keyed
// by id. cancel_sleeps() posts ->cancel() onto each timer's OWN executor so
// the cancel is race-free against the timer's io thread (asio timers are not
// thread-safe), and the shared_ptr keeps the timer alive across the post.
// Completion (deadline reached OR operation_aborted) lands on the awaiter's
// bound executor because async_wait(use_awaitable) resumes the awaiting
// coroutine on its own executor (FR-003). Idempotent + re-entrant-safe
// (E1 / Edge Case).
//
// T055 (US6 / D-8): Per-session reusable steady_timer slot pool (E12).
// When sleep_until is called from within a session_executor-bound coroutine,
// we recover the Session* via ex.target<session_executor>()->session_ptr()
// and look up (or lazily create from session_arena) a per-session timer slot.
// Subsequent cycles reset expires_at + re-arm async_wait with ZERO global
// heap allocation (steady state). Non-session callers (engine-scope,
// no session_executor wrapper on the awaiter's executor) fall back to the
// original per-call shared_ptr<steady_timer> from the global heap.
//
// ⚠️ THAT ACCEPTANCE WAS WRITTEN FOR ONE-SHOT CALLERS ("acceptable for
// non-hot-path engine-scope sleeps, e.g. close-timeout machinery") AND IT NOW
// HAS A REPEATING ONE. #377 put read_first_frame_bounded's deadline on this
// path, and it is an `operator||` arm inside the read loop, so it enters
// sleep_until once PER ITERATION. The accept loop runs on
// `asio::strand<any_io_executor>`, not a session_executor, and
// target<session_executor>() is EXACT-TYPE — so it lands here, never in the
// slot pool.
//
// Per call this branch costs a make_strand allocation, the make_shared<timer>,
// an inflight-map node, and two acquisitions of `state::m` (insert + dereg).
// A well-behaved peer completes the first frame in ONE iteration, so the normal
// cost is per-connection and immaterial. The BOUND is the part worth knowing:
// iterations are capped by kFirstFrameMaxBytes + 1 (4097) within the first-frame
// deadline, so a peer dribbling one byte per segment reaches ~4k of each on a
// single connection — and `state::m` is the SAME mutex every session's heartbeat
// sleep_until takes, so an accept burst now contends with steady-state
// heartbeats on it. Derived from those two constants, not witnessed.
//
// ⚠️ NOT COVERED BY tests/alloc_guard/test_clock_sleep_alloc_guard.cpp: every
// cell there builds its executor with make_session_executor(), i.e. it drives
// the SLOT-POOL branch only. The gate named for this function does not test the
// branch described here, and cannot — this branch is ALLOWED to allocate. Do not
// read its green as covering this path.
//
// If it becomes a problem the cheap lever is to cache ONE strand in `state` and
// reuse it for every fallback timer (drops the make_strand and two of the four
// lock acquisitions, for all non-session callers, at the cost of serialising
// unrelated engine-scope timers). Deliberately not done: a shared-core change
// with a blast radius past the caller that provoked it.
// Both threading modes (per_session_strand / direct_executor) use the same
// Session* keying axis (round 2 root cause #1 — D-8).
//
// Slot lifetime — D-23 (cleanup): the slot lives in session_arena, so its
// memory is reclaimed when ~Session destroys the arena. The session_slots
// map's shared_ptr to the slot must therefore be released BEFORE
// ~Session — otherwise the next session whose address happens to coincide
// (heap-reused) finds a stale slot pointing into reclaimed arena memory
// (use-after-free). The cleanup is forget_session(Session*), called from
// Session::~Session() (the canonical lifetime hook) per Clock's
// default-virtual forget_session API.
#include <asio/any_io_executor.hpp>
#include <asio/post.hpp>
#include <asio/steady_timer.hpp>
#include <asio/strand.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <chrono>
#include <cstdint>
#include <fixpp/core/clock.hpp>             // utc_time_point, steady_time_point
#include <fixpp/core/session_executor.hpp>  // session_arena_of; target<session_executor>
#include <fixpp/core/system_clock_source.hpp>
#include <map>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

// fixpp::session::Session is forward-declared via session_executor.hpp —
// used as a map key (opaque pointer) only; no complete type needed here.

namespace fixpp::core {

// ─────────────────────────────────────────────────────────────────────────────
// Per-session timer slot (E12 / D-8).
//
// Allocated ONCE from session_arena at the first sleep_until on that session.
// Lifetime: until forget_session(Session*) erases it (D-23) — canonically
// called from ~Session BEFORE the session_arena is reclaimed.
//
// Timer is constructed on a per-timer strand over engine_exec (D-19): asio
// timers are not thread-safe — cancel_sleeps() posts sp->cancel() onto the
// strand, serialising it against itself across concurrent cancel_sleeps()
// callers. expires_at() and async_wait() initiation are called from the
// awaiter's coroutine (NOT on the timer's strand); the design assumes the
// natural async_wait completion ordering (cancel completes the in-flight
// wait BEFORE the awaiter resumes to start the next sleep) ensures these
// don't overlap with a posted cancel. Concurrent overlapping sleeps on the
// SAME session under direct_executor would already break asio's "one
// outstanding async_wait per timer" rule, so the slot pool's single-timer
// design implicitly requires user-side serialisation under direct_executor.
// ─────────────────────────────────────────────────────────────────────────────
namespace {
struct SessionTimerSlot {
    std::shared_ptr<asio::steady_timer> timer;
};
}  // namespace

struct system_clock_source::state {
    asio::any_io_executor engine_exec;
    std::mutex m;
    std::uint64_t next_id{1};
    std::map<std::uint64_t, std::weak_ptr<asio::steady_timer>> inflight;

    // Per-session reusable timer slot pool (T055 / D-8).
    // Key = Session* (stable session-domain identity — works under both
    // per_session_strand and direct_executor per round-2 root cause #1).
    // Value = shared ownership of the timer slot (the session arena backs the
    // slot control block via allocate_shared; the timer itself lives in the
    // shared_ptr payoad).
    // Protected by `m` (same lock as inflight — avoids a second mutex).
    std::unordered_map<fixpp::session::Session*, std::shared_ptr<SessionTimerSlot>> session_slots;
};

system_clock_source::system_clock_source(asio::any_io_executor exec)
    : impl_(std::make_unique<state>()) {
    impl_->engine_exec = std::move(exec);
}

system_clock_source::~system_clock_source() {
    // Well-formed shutdown has already drained sessions' sleeps (D-9 / root
    // cause #5); abort any straggler so no async_wait is leaked.
    cancel_sleeps();
}

std::size_t system_clock_source::inflight_count() const noexcept {
    std::scoped_lock g(impl_->m);
    return impl_->inflight.size();
}

utc_time_point system_clock_source::now() const noexcept {
    // time_point_cast to nanoseconds: lossless widening on libstdc++ (already
    // ns), best-available on libc++ (µs clock → ns type, last 3 digits = 000).
    return std::chrono::time_point_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now());
}

steady_time_point system_clock_source::steady_now() const noexcept {
    return std::chrono::steady_clock::now();
}

asio::awaitable<void> system_clock_source::sleep_until(steady_time_point deadline) {
    // ── Step 1: recover the awaiter's executor ─────────────────────────────
    // co_await asio::this_coro::executor returns the bound executor (erased
    // to asio::any_io_executor when the coroutine is spawned without an
    // explicit bind_executor).
    asio::any_io_executor awaiter_exec = co_await asio::this_coro::executor;

    // ── Step 2: attempt per-session slot pool lookup (T055 / D-8) ─────────
    // Try to recover a session_executor from the awaiter's bound executor.
    // target<T>() returns a pointer to the stored executor if its type IS T,
    // nullptr otherwise (same pattern as current_trace_context in
    // trace_context.hpp). Works under both threading modes: the wrapper IS
    // the executor type at the binding site (not erased — bind_executor
    // stores the typed wrapper, so target<session_executor>() succeeds when
    // the session was opened via make_session_executor + the coroutine runs
    // on the session's executor).
    const session_executor* const se_ptr = awaiter_exec.target<session_executor>();

    std::shared_ptr<asio::steady_timer> timer;

    if (se_ptr != nullptr) {
        // ── Session-scoped path: reuse or lazily allocate the slot ─────────
        fixpp::session::Session* const sess = se_ptr->session_ptr();

        if (sess != nullptr) {
            std::shared_ptr<SessionTimerSlot> slot;
            {
                std::scoped_lock g(impl_->m);
                auto it = impl_->session_slots.find(sess);
                if (it != impl_->session_slots.end()) {
                    slot = it->second;  // existing slot — zero allocation
                }
            }

            if (!slot) {
                // First sleep_until for this session: allocate the slot ONCE
                // from session_arena (never from the global heap — D-8).
                std::pmr::memory_resource* arena = session_arena_of(*se_ptr);
                // arena is never null on the hot path (I-18 guarantees the
                // session_arena chain always terminates; session_arena_of
                // returns nullptr only for a default-constructed wrapper).
                // If arena IS null (shouldn't happen), fall back to the
                // per-call path below (see non-session branch).
                if (arena != nullptr) {
                    std::pmr::polymorphic_allocator<SessionTimerSlot> alloc{arena};
                    // Allocate the slot control block from session_arena.
                    // The steady_timer executor is a per-timer strand over
                    // engine_exec (D-19 — asio timers not thread-safe;
                    // cancel_sleeps posts cancel() onto the timer's own
                    // executor which must be a serialiser).
                    auto new_slot = std::allocate_shared<SessionTimerSlot>(
                        alloc, SessionTimerSlot{std::make_shared<asio::steady_timer>(
                                   asio::make_strand(impl_->engine_exec))});

                    std::scoped_lock g(impl_->m);
                    // Double-check under lock (another coroutine on the same
                    // session could have raced here — under per_session_strand
                    // this cannot happen, but under direct_executor it can if
                    // the user runs two sleeps concurrently; be safe).
                    auto [it, inserted] = impl_->session_slots.emplace(sess, std::move(new_slot));
                    slot = it->second;
                }
            }

            if (slot) {
                // Reuse the cached timer: reset expires_at (no allocation).
                slot->timer->expires_at(deadline);
                timer = slot->timer;
            }
        }
    }

    if (!timer) {
        // ── Non-session or arena-null fallback: per-call timer ─────────────
        // Engine-scope callers (close-timeout machinery, seam 10 test harness
        // without a real session, third-party Clock conformance tests) land
        // here. The per-call global-heap allocation is acceptable for
        // non-hot-path engine-scope sleeps ([2d §4.1.1] pattern (b) note;
        // D-8: "non-session callers fall back to per-call path").
        // The per-timer strand is preserved (D-19).
        timer = std::make_shared<asio::steady_timer>(asio::make_strand(impl_->engine_exec));
        timer->expires_at(deadline);
    }

    // ── Register in the in-flight list (for cancel_sleeps()) ──────────────
    std::uint64_t id{0};
    {
        std::scoped_lock g(impl_->m);
        id = impl_->next_id++;
        impl_->inflight.emplace(id, std::weak_ptr<asio::steady_timer>(timer));
    }
    // RAII de-register (covers deadline-reached, cancel, and exception paths).
    struct dereg {
        system_clock_source::state* s;
        std::uint64_t id;
        dereg(system_clock_source::state* ps, std::uint64_t pid) noexcept : s{ps}, id{pid} {}
        dereg(const dereg&) = delete;
        dereg& operator=(const dereg&) = delete;
        dereg(dereg&&) = delete;
        dereg& operator=(dereg&&) = delete;
        ~dereg() {
            std::scoped_lock g(s->m);
            s->inflight.erase(id);
        }
    } guard{impl_.get(), id};

    // Resumes the awaiting coroutine on ITS bound executor (FR-003); throws
    // std::system_error(operation_aborted) on cancellation (per-op slot OR
    // cancel_sleeps()) — E1 / I-09. The cancellable region converts it to
    // the contract return.
    co_await timer->async_wait(asio::use_awaitable);
    co_return;
}

// NOLINTNEXTLINE(bugprone-exception-escape)
void system_clock_source::cancel_sleeps() noexcept {
    std::vector<std::shared_ptr<asio::steady_timer>> live;
    {
        std::scoped_lock g(impl_->m);
        live.reserve(impl_->inflight.size());
        for (auto& [id, w] : impl_->inflight) {
            if (auto sp = w.lock()) {
                live.push_back(std::move(sp));
            }
        }
    }
    // Cancel each on its OWN executor (asio timers are not thread-safe); the
    // shared_ptr keeps it alive across the post. Lock released first, so a
    // completion handler that re-enters cancel_sleeps() is safe (E1).
    for (auto& sp : live) {
        auto ex = sp->get_executor();
        asio::post(ex, [sp] { sp->cancel(); });
    }
}

// D-23: release this session's reusable-timer slot under the impl_->m lock,
// BEFORE the session's arena memory is reclaimed by ~Session. Idempotent —
// a session that never slept has no entry; erase is a no-op. The slot's
// shared_ptr destruction calls the deallocator on the (still-live) session
// arena's polymorphic_allocator; for a monotonic_buffer_resource that's a
// no-op, which is correct (the arena owns the memory).
void system_clock_source::forget_session(fixpp::session::Session* session) noexcept {
    if (session == nullptr) {
        return;
    }
    std::shared_ptr<SessionTimerSlot> released;  // destroyed AFTER lock release
    {
        std::scoped_lock g(impl_->m);
        auto it = impl_->session_slots.find(session);
        if (it != impl_->session_slots.end()) {
            released = std::move(it->second);
            impl_->session_slots.erase(it);
        }
    }
}

}  // namespace fixpp::core
