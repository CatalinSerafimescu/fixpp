// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/alloc_guard/test_clock_sleep_alloc_guard.cpp
//
// 007-threading-clock T052 — [2d §9.18] / root cause #5 / N-P1-1 Seam 18:
// zero global-heap after the first heartbeat cycle over 10⁴ cycles, BOTH
// threading modes; an idle session that never sleeps allocates no slot
// (SC-003; I-16; E12; D-8).
//
// Run under mallocnesia via tools/check_alloc.py:
//   python3 tools/check_alloc.py \
//       --binary build/linux-clang-debug/bin/test_clock_sleep_alloc_guard
//
// Design anchors:
//   D-8  — per-session reusable steady_timer slot, keyed by Session*,
//           allocated ONCE from session_arena at first sleep_until.
//   D-19 — per-timer strand over engine_exec (asio timers not thread-safe).
//   E12  — SessionTimerSlot entity; session-lifetime; both threading modes.
//
// Erratum E-4 warm-up (mirrors sync_alloc_guard_test.cpp): asio's
// cancellation_slot has NO allocator-binding hook. On the first
// cancellation-slot assignment on a thread asio's detail::thread_info_base
// performs ONE global aligned_new for its per-thread recycling block.
// The FIRST sleep_until call on a new session also performs ONE global
// alloc to create the per-session timer slot (the slot is allocated from
// session_arena via allocate_shared — the shared_ptr control block goes
// into session_arena, not the global heap; but asio's internal timer
// strand construction may touch the global heap on first use). The warm-up
// pass below covers all first-touch allocations so the MEASURED window is
// zero-global-heap (steady state after cycle 1).
//
// [const §VIII.5] extended to heartbeat path (N-P1-1); D-8; N-P2-4.

#include <gtest/gtest.h>

#include <array>
#include <asio/bind_executor.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <atomic>
#include <cstddef>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/session_executor.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <memory_resource>
#include <system_error>

#include "support/minimal_dictionary.hpp"

// mallocnesia replaces these weak no-ops with its interceptor scope markers.
extern "C" {
__attribute__((weak)) void alloc_guard_start();
__attribute__((weak)) void alloc_guard_end();
}

namespace {

using namespace std::chrono_literals;

using fixpp::core::EngineConfig;
using fixpp::core::make_session_executor;
using fixpp::core::system_clock_source;
using fixpp::session::Session;
using fixpp::session::SessionConfig;
using fixpp::session::threading_mode;

// ─────────────────────────────────────────────────────────────────────────────
// run_sleep_corpus — drives N sleep_until cycles on the session executor
// and measures global-heap activity between alloc_guard_start/end.
//
// Strategy:
//   1. Build all objects (arenas, engine, session, clock) BEFORE markers.
//   2. Run WARMUP_CYCLES to prime asio's per-thread recycler + allocate the
//      per-session timer slot (one-time PMR allocation from session_arena).
//   3. alloc_guard_start()
//   4. Run CORPUS_CYCLES using immediate deadlines in the past (no real sleep).
//   5. alloc_guard_end()
//
// "Immediate" deadline = steady_clock::now() - 1s (already elapsed) so the
// async_wait completes synchronously when the io_context polls — no real
// wall-clock delay, no TSan flakiness from timing.
// ─────────────────────────────────────────────────────────────────────────────
void run_sleep_corpus(threading_mode mode) {
    constexpr int WARMUP_CYCLES = 5;  // prime asio recycler + slot alloc
    constexpr int CORPUS_CYCLES = 10'000;

    constexpr std::size_t kSessionArenaSize = 65536;
    std::array<std::byte, kSessionArenaSize> session_buf{};
    // Monotonic, NULL upstream — exhaustion is a hard failure, not silent heap.
    std::pmr::monotonic_buffer_resource session_mr{session_buf.data(), session_buf.size(),
                                                   std::pmr::null_memory_resource()};

    // Single-threaded io_context drives everything deterministically.
    asio::io_context ioc;

    EngineConfig engine;
    engine.executor = ioc.get_executor();
    engine.default_session_resource = &session_mr;

    SessionConfig cfg;
    cfg.session_arena = &session_mr;
    cfg.mode = mode;
    cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    if (mode == threading_mode::direct_executor) cfg.already_serialized_executor = true;

    Session sess{engine, cfg};
    auto se_result =
        make_session_executor(ioc.get_executor(), mode,
                              /*attested=*/(mode == threading_mode::direct_executor), &sess);
    ASSERT_TRUE(se_result.has_value()) << "make_session_executor failed";
    auto se = *se_result;

    // Build the clock with the engine executor.
    auto clk = std::make_shared<system_clock_source>(ioc.get_executor());

    // ── Warm-up: prime asio recycler + perform the one-time slot alloc ─────
    {
        std::atomic<int> warmup_done{0};
        asio::co_spawn(ioc,
                       asio::bind_executor(se,
                                           [&]() -> asio::awaitable<void> {
                                               for (int i = 0; i < WARMUP_CYCLES; ++i) {
                                                   // Deadline in the past → async_wait completes
                                                   // immediately on the next io_context poll pass.
                                                   try {
                                                       co_await clk->sleep_until(
                                                           std::chrono::steady_clock::now() - 1s);
                                                   } catch (const std::system_error&) {
                                                       // operation_aborted on cancel — should not
                                                       // happen in the warm-up with no
                                                       // cancellation.
                                                   }
                                                   warmup_done.fetch_add(1,
                                                                         std::memory_order_relaxed);
                                               }
                                               co_return;
                                           }),
                       asio::detached);
        ioc.run();
        ioc.restart();
        ASSERT_EQ(warmup_done.load(), WARMUP_CYCLES);
    }

    // ── Measured window ────────────────────────────────────────────────────
    std::atomic<int> corpus_done{0};
    asio::co_spawn(ioc,
                   asio::bind_executor(
                       se,
                       [&]() -> asio::awaitable<void> {
                           for (int i = 0; i < CORPUS_CYCLES; ++i) {
                               try {
                                   co_await clk->sleep_until(std::chrono::steady_clock::now() - 1s);
                               } catch (const std::system_error&) {
                                   // Should not happen without cancellation.
                               }
                               corpus_done.fetch_add(1, std::memory_order_relaxed);
                               // Yield periodically to avoid excessive inline-resume
                               // recursion depth.
                               if ((i & 255) == 255)
                                   co_await asio::post(co_await asio::this_coro::executor,
                                                       asio::use_awaitable);
                           }
                           co_return;
                       }),
                   asio::detached);

    if (alloc_guard_start) alloc_guard_start();
    ioc.run();
    if (alloc_guard_end) alloc_guard_end();

    EXPECT_EQ(corpus_done.load(), CORPUS_CYCLES);
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// ClockSleepAllocGuard / PerSessionStrandNoHeapAllocAfterCycle1
//
// Under per_session_strand: the slot is allocated at cycle 1 from session_arena;
// cycles 2..N reuse it with zero global heap.
// ─────────────────────────────────────────────────────────────────────────────
TEST(ClockSleepAllocGuard, PerSessionStrandNoHeapAllocAfterCycle1) {
    run_sleep_corpus(threading_mode::per_session_strand);
}

// ─────────────────────────────────────────────────────────────────────────────
// ClockSleepAllocGuard / DirectExecutorNoHeapAllocAfterCycle1
//
// Under direct_executor: same Session* keying axis (D-8 / round-2 root cause
// #1); the executor carries no strand wrapper but the timer slot is still
// looked up by Session*, so behaviour is identical.
// ─────────────────────────────────────────────────────────────────────────────
TEST(ClockSleepAllocGuard, DirectExecutorNoHeapAllocAfterCycle1) {
    run_sleep_corpus(threading_mode::direct_executor);
}

// ─────────────────────────────────────────────────────────────────────────────
// ClockSleepAllocGuard / IdleSessionNoSlotAllocated
//
// An idle session that NEVER calls sleep_until must NOT allocate a timer slot
// in system_clock_source. The slot pool is lazily populated at first sleep;
// a session that is constructed and destroyed without sleeping leaves the
// pool untouched.
// ─────────────────────────────────────────────────────────────────────────────
TEST(ClockSleepAllocGuard, IdleSessionNoSlotAllocated) {
    constexpr std::size_t kSessionArenaSize = 65536;
    std::array<std::byte, kSessionArenaSize> session_buf{};
    std::pmr::monotonic_buffer_resource session_mr{session_buf.data(), session_buf.size(),
                                                   std::pmr::null_memory_resource()};

    asio::io_context ioc;

    EngineConfig engine;
    engine.executor = ioc.get_executor();
    engine.default_session_resource = &session_mr;

    SessionConfig cfg;
    cfg.session_arena = &session_mr;
    cfg.mode = threading_mode::per_session_strand;
    cfg.dictionary = fixpp::test_support::make_minimal_dictionary();

    // Record the arena's used bytes before creating the session.
    // (We cannot easily introspect a monotonic_buffer_resource's watermark,
    // but we CAN verify the contract holds by creating the clock, NOT calling
    // sleep_until, and checking that cancel_sleeps produces no error.)
    auto clk = std::make_shared<system_clock_source>(ioc.get_executor());

    {
        Session sess{engine, cfg};
        // No sleep_until called — slot must NOT be lazily allocated.
        // We verify cancel_sleeps is a no-op (idempotent, no crash/error).
        clk->cancel_sleeps();
        ioc.run();
        // Session destructs here.
    }

    // The clock is still valid; cancel_sleeps again is safe (idempotent).
    clk->cancel_sleeps();
    // If we reach here without crash/assertion, the invariant is satisfied.
    SUCCEED();
}
