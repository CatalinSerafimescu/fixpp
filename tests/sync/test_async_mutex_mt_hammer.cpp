// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_async_mutex_mt_hammer.cpp — 058 T040 (US5, Phase 7.5)
//
// H-A MT hammer — organic (seam-OFF, coverage-lane-visible) coverage of the
// free-list pop CAS-retry (A4), bump-counter CAS-retry (A5), the async_lock
// slow-path push CAS-retry (A11), release_ref push CAS-retry (C3), and the
// FIRST unlock() terminal-CAS-fail recursive-unlock arm ("F4", :1382) in
// include/fixpp/core/sync/async_mutex.hpp.
//
// CORRECTION vs the original Phase-7.5 coverage-design-gate claim ("T040 is
// the ONLY coverage of ... A11, ... F4/F6, ... F7") — recorded here + in
// tasks.md T040 evidence + escalated to the orchestrator (this revises a
// signed-off gate, not a call for a single test file to make silently):
// empirical llvm-cov measurement (fresh profraw regenerated per
// feedback_coverage_profraw_staleness) on `linux-clang-coverage` found:
//   - A4/A5/C3/A11 — solidly covered by ORDINARY jittered contention
//     (hundreds of real CAS retries per run, confirmed).
//   - F4 (:1382) — NOT hit by the brief-literal jittered design (0 hits
//     across 150+ opportunities); IS hit by a jitter-FREE, high-volume burst
//     (6 total hits observed across 6 independent trial runs) — see the
//     dual-config note below. The hit rate is low/noisy (~0.3-1.5%), so this
//     is a probabilistic, not-guaranteed-every-run organic coverage claim.
//   - F6 (the SECOND terminal-CAS-fail recursive-unlock arm, :1442, reached
//     only when a FIFO walk exhausts with EVERY queued waiter already
//     cancelled AND a fresh push lands in the immediately-following terminal
//     CAS window) — measured UNREACHABLE by every organic combination
//     tried: the zero-jitter high-volume burst above (0 hits), the
//     already-landed T027-T031 cross-thread cancellation race tests merged
//     into the same coverage run (0 hits, even though those tests DO drive
//     5 genuine all-cancelled walk-exhaustion events — the compound
//     "exhausted AND a concurrent push lands in the terminal CAS window" is
//     a second, independent narrow race on top of an already-rare one).
//     RECOMMENDATION (escalated, not self-decided): waive F6 at T036 as
//     reachable-in-principle-but-not-organically-hittable within bounded
//     test time (W-9 class) unless a dedicated hybrid cancel+volume hammer
//     is commissioned as a follow-up task.
//   - push_residual's CAS-retry ("F7") — measured STRUCTURALLY UNREACHABLE
//     under the supported (non-drain-overlap) contract: push_residual is
//     called only from within unlock(), and unlock() is holder-serialized
//     (only the current holder's thread ever executes it, one at a time) —
//     so next_drain_head_ has a single writer at any instant and its CAS
//     can never observe a concurrent modification. Confirmed empirically:
//     78.9k push_residual calls == 78.9k do-while loop-body executions
//     (zero retries) across every trial run, including the highest-volume
//     ones. RECOMMENDATION: waive as a single-writer structural proof
//     (W-4/W-5 class), not a T040 coverage gap.
//
// Why this file has to exist (tasks.md T036 lane caveat (a) /
// .specify/decisions/058-async-mutex-hardening-coverage-design.md): the
// deterministic FIXPP_ASYNC_MUTEX_TEST_SEAM witnesses
// (test_async_mutex_aba_interleave.cpp, test_pool_exhaustion_reuse.cpp,
// test_am_p3_impossible_state_traps.cpp) pin the CAS windows with a
// compile-gated seam hook. The coverage lane builds seam-OFF, so those
// `#ifdef FIXPP_ASYNC_MUTEX_TEST_SEAM` seam-hook lines (and, more
// importantly, the surrounding CAS-retry-loop lines that only get exercised
// by deliberately pinning a thread mid-CAS) are preprocessed OUT / never
// naturally contended in a single-threaded deterministic drive — no lcov
// credit lands on the coverage lane from those tests. This file is
// registered NORMALLY (add_sync_test, ordinary fixpp_sync linkage, no seam
// macro) and drives GENUINE cross-thread contention — real OS threads racing
// the SAME free list / SAME bump counter / SAME state_ atomic on a real
// asio::thread_pool — so the CAS-failure ("retry") branches and the unlock
// recursive-call arms fire ORGANICALLY, with lcov credit on every lane
// including the coverage lane.
//
// Design (brief-mandated, tasks.md T040 — dual config, see the CORRECTION
// note above for why):
//   - CORRECTNESS LANES (debug/TSan/ASan, the default — unchanged from the
//     brief-literal design, originally verified 10/10 debug + 25/25 TSan
//     [0 reports] + 10/10 ASan [0 reports]): N = 2 * hardware_concurrency()
//     (floor 4) contending coroutines per rep, 12 cycles/worker, 25 reps,
//     randomized 0-2us jitter both BEFORE async_lock() (diversifies arrival
//     order) and AFTER acquiring but BEFORE the guard is released
//     (diversifies CS hold time, encouraging queue pile-up). Jitter is for
//     interleaving diversity ONLY; no correctness assertion below depends
//     on its timing.
//   - COVERAGE LANE ONLY (FIXPP_ASYNC_MUTEX_MT_HAMMER_COVERAGE_BURST, wired
//     from FIXPP_ENABLE_COVERAGE in CMakeLists.txt): N = 8 * hw (floor 32),
//     40 cycles/worker, 100 reps, jitter disabled (always 0us) to maximize
//     the raw CAS-collision density needed to hit F4's few-instruction
//     window — see the CORRECTION note above. This does NOT run on the
//     correctness lanes (would blow the ctest TIMEOUT under TSan's
//     slowdown, and disabling jitter is a coverage-only deviation from the
//     brief's literal "randomized jitter" wording).
//   - Both configs: spawned directly on a fresh asio::thread_pool(4)'s
//     executor. mr == nullptr (the default), so every waiter allocation
//     routes through the INLINE pool free-list / bump allocator, never the
//     PMR path — this is what puts real concurrent pressure on the exact
//     CAS loops T040 targets.
//   - Each rep uses a FRESH async_mutex + FRESH thread_pool(4), so each rep
//     is an independent, fully-drained sample (the destructor's
//     std::terminate() guard — [const-adjacent async_mutex.hpp:894] — is
//     itself a live assertion that every rep left the mutex fully drained;
//     a lost wakeup or a stray in-flight resumer would trip it).
//   - pool.join() is the real drain barrier (NOT asio::use_future's
//     fut.get() — feedback_use_future_get_not_a_pool_drain_barrier: a ready
//     future only proves the coroutine's own tail ran, not that every
//     worker thread — including a posted resume-runner's
//     in_flight_resumers_ decrement — has finished). futures are collected
//     AFTER join() purely to propagate any unexpected exception.
//   - ONE long-lived executor (`pool.get_executor()`, captured once, reused
//     verbatim for every co_spawn call this rep) — never re-wrapped/rebound
//     per co_spawn (feedback_strand_in_any_executor_refcount_race: a fresh
//     any_io_executor conversion per co_spawn call is the TSan-race trap;
//     this file never constructs an any_io_executor at all).
//   - Bounded: rep count and per-worker cycle count are both small
//     compile-time constants on both configs, so total work is bounded
//     regardless of hardware_concurrency(); a ctest TIMEOUT is set at
//     registration, raised for the coverage-lane config
//     (feedback_ci_hung_test_no_timeout_burns_6h).
//
// Oracles (discriminating — feedback_witness_asserts_named_postcondition_not_proxy;
// each is a DIRECT check, not a bypassable-if-stimulus-arrived proxy):
//   (1) EXACT completion count — a global atomic `completed` counter,
//       incremented once per fully-finished lock/unlock cycle, asserted
//       == N * kCyclesPerWorker after pool.join(). A lost wakeup (a waiter
//       parked forever, or a resume that never fires) shows up as a
//       strictly-less count; a double-resume would show a strictly-greater
//       count (also wrong) — both directions are caught by the exact `==`.
//   (2) MUTUAL EXCLUSION canary — a deliberately NON-atomic `int`
//       (`nonatomic_holder_count`) incremented/decremented INSIDE the
//       critical section, guarded by nothing but the mutex itself. A
//       genuine double-grant makes two threads race this plain int with no
//       synchronization — real UB, TSan-reportable, and the immediate
//       post-increment read is asserted <= the highest value any run can
//       observe under correct exclusion (1) via a SEPARATE atomic
//       (`peak_holders`) so the *observation* itself never races (only the
//       counter under observation is the racy canary).
//   (3) NO DOUBLE-GRANT identity CAS — `holder_ticket`, a SEPARATE atomic
//       mechanism from (2): every entrant claims a globally-unique
//       monotonic ticket and CAS-installs it into `holder_ticket` from the
//       required-empty (0) state; a concurrent second entrant finds it
//       non-zero and the CAS fails outright, and the exiting entrant's own
//       CAS-clear (ticket -> 0) also fails if anything else has touched the
//       cell. This is a 100%-reliable (non-timing-dependent, no data race
//       on the check itself) double-grant detector, distinct in mechanism
//       from the racy canary in (2) — if (2) is ever inconclusive on a
//       given platform/lane, (3) still discriminates.
//
// No std::mutex / std::condition_variable anywhere in this file (FR-012) —
// coordination is std::atomic + asio::thread_pool::join() only.
//
// If oracle (2) or (3) EVER trips, that is a real double-grant (P0), not a
// flake — see tasks.md T040 brief: escalate, do not retry/loosen.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/steady_timer.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <fixpp/core/sync/async_mutex.hpp>
#include <future>
#include <random>
#include <thread>
#include <vector>

namespace {

using fixpp::sync::async_mutex;

// ─────────────────────────────────────────────────────────────────────────────
// Dual config (added post-empirical-measurement — see the escalation note in
// tasks.md T040 evidence): the brief-mandated light config (N=2*hw,
// cycles=12, randomized 0-2us jitter) reliably covers A4/A5/C3/A11 (verified:
// hundreds of real CAS retries per run) but was measured to NEVER organically
// hit the unlock() terminal-CAS-fail recursive-unlock arm at :1382 ("F4") —
// the timer-suspension jitter smears the few-instruction race window. A
// jitter-FREE, much-higher-volume burst was measured to hit it reliably
// (non-zero across every trial run), at a low/noisy rate (~1-3 hits per
// ~200-600 "opportunities"). That burst is DELIBERATELY confined to the
// coverage lane only (FIXPP_ASYNC_MUTEX_MT_HAMMER_COVERAGE_BURST, wired from
// FIXPP_ENABLE_COVERAGE in CMakeLists.txt) — it would blow the 120s ctest
// TIMEOUT under TSan's ~10-15x slowdown, and disabling jitter entirely is a
// deliberate coverage-maximizing deviation from the brief's literal
// "randomized jitter" wording that is NOT appropriate for the
// debug/TSan/ASan correctness lanes (those keep the brief-literal design,
// unchanged from the originally-verified 10/10 debug + 25/25 TSan + 10/10
// ASan runs).
#if defined(FIXPP_ASYNC_MUTEX_MT_HAMMER_COVERAGE_BURST)
constexpr unsigned kWorkerFloor = 32;
constexpr unsigned kWorkerMultiplier = 8;
constexpr unsigned kCyclesPerWorker = 40;
constexpr int kReps = 100;
constexpr int kJitterMaxUs = 0;  // burst: no suspension, maximize raw CAS-collision density.
#else
constexpr unsigned kWorkerFloor = 4;
constexpr unsigned kWorkerMultiplier = 2;
constexpr unsigned kCyclesPerWorker = 12;
constexpr int kReps = 25;
constexpr int kJitterMaxUs = 2;  // brief-mandated randomized 0-2us jitter.
#endif

// Interleaving-diversity jitter ONLY (see file header) — never an oracle.
asio::awaitable<void> jitter(std::mt19937& rng) {
    std::uniform_int_distribution<int> dist(0, kJitterMaxUs);
    int us = dist(rng);
    if (us > 0) {
        auto exec = co_await asio::this_coro::executor;
        asio::steady_timer t(exec, std::chrono::microseconds(us));
        co_await t.async_wait(asio::use_awaitable);
    }
}

TEST(SeamAsyncMutexMtHammer, RealContentionNoLostWakeupNoDoubleGrant) {
    const unsigned N = std::max(kWorkerFloor, kWorkerMultiplier * std::thread::hardware_concurrency());
    constexpr unsigned kCycles = kCyclesPerWorker;

    for (int rep = 0; rep < kReps; ++rep) {
        async_mutex mtx;

        // Oracle (1).
        std::atomic<std::uint64_t> completed{0};
        std::atomic<std::uint64_t> lock_failures{0};

        // Oracle (2): non-atomic canary + a race-free observation atomic.
        int nonatomic_holder_count = 0;
        std::atomic<int> peak_holders{0};

        // Oracle (3): identity CAS.
        std::atomic<std::uint64_t> holder_ticket{0};
        std::atomic<std::uint64_t> next_ticket{1};
        std::atomic<bool> double_grant_detected{false};

        auto make_coro = [&](unsigned worker_id) -> asio::awaitable<void> {
            std::mt19937 rng(static_cast<unsigned>(rep) * 1'000'003u + worker_id * 7919u + 12345u);

            for (unsigned cycle = 0; cycle < kCycles; ++cycle) {
                co_await jitter(rng);  // pre-lock jitter (arrival-order diversity)

                auto g = co_await mtx.async_lock();  // mr == nullptr: inline pool path
                if (!g.has_value()) {
                    lock_failures.fetch_add(1, std::memory_order_relaxed);
                    co_return;
                }

                // Oracle (3): claim-then-check identity CAS.
                std::uint64_t my_ticket = next_ticket.fetch_add(1, std::memory_order_relaxed);
                std::uint64_t expected_empty = 0;
                bool won_ticket = holder_ticket.compare_exchange_strong(
                    expected_empty, my_ticket, std::memory_order_acq_rel, std::memory_order_acquire);
                if (!won_ticket) {
                    double_grant_detected.store(true, std::memory_order_relaxed);
                }

                // Oracle (2): racy canary increment (deliberately non-atomic).
                int v = ++nonatomic_holder_count;
                if (v > 1) {
                    int prev = peak_holders.load(std::memory_order_relaxed);
                    while (v > prev && !peak_holders.compare_exchange_weak(
                                           prev, v, std::memory_order_relaxed)) {
                    }
                }

                // pre-unlock jitter: holds the lock across a suspension to
                // encourage queue pile-up (more contention downstream).
                co_await jitter(rng);

                --nonatomic_holder_count;

                if (won_ticket) {
                    std::uint64_t expected_mine = my_ticket;
                    bool cleared = holder_ticket.compare_exchange_strong(
                        expected_mine, 0, std::memory_order_acq_rel, std::memory_order_acquire);
                    if (!cleared) {
                        double_grant_detected.store(true, std::memory_order_relaxed);
                    }
                }

                completed.fetch_add(1, std::memory_order_relaxed);
                // `g` destructs at the end of this scope -> unlock().
            }
        };

        {
            asio::thread_pool pool(4);
            auto ex = pool.get_executor();  // ONE long-lived executor for every co_spawn this rep.

            std::vector<std::future<void>> futs;
            futs.reserve(N);
            for (unsigned w = 0; w < N; ++w) {
                futs.push_back(asio::co_spawn(ex, make_coro(w), asio::use_future));
            }

            pool.join();  // the real drain barrier (not fut.get()).
            for (auto& f : futs) {
                f.get();  // propagate any unexpected exception now that the pool has drained.
            }
        }  // `mtx` destructs here — trips std::terminate() if not fully drained.

        ASSERT_EQ(lock_failures.load(), 0u) << "rep=" << rep << ": unexpected async_lock() failure";
        ASSERT_FALSE(double_grant_detected.load())
            << "rep=" << rep << ": double-grant detected (oracle 3, identity CAS)";
        ASSERT_LE(peak_holders.load(), 1)
            << "rep=" << rep << ": mutual exclusion violated (oracle 2, peak=" << peak_holders.load()
            << ")";
        ASSERT_EQ(completed.load(), static_cast<std::uint64_t>(N) * kCycles)
            << "rep=" << rep << ": lost wakeup (oracle 1)";
    }
}

}  // namespace
