// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/alloc_guard/test_dispatch_alloc_guard.cpp
//
// 007-threading-clock T051 — [2d §9.7] / §6.2 Seam 7: zero GLOBAL-heap
// new/delete/malloc between parse and fromApp over a 10⁴-message corpus
// (PMR-arena allocations expected, NOT flagged — N-P2-4; SC-003; I-16).
//
// Run under mallocnesia via tools/check_alloc.py:
//   python3 tools/check_alloc.py \
//       --binary build/linux-clang-debug/bin/test_dispatch_alloc_guard
//
// The critical path: parse (MessageView from frame_view) → cancellable_dispatch
// → handler. All dynamic storage is either:
//   a) HALO-elided (compiler elides the coroutine frame entirely when the
//      dispatch chain stays in-strand), or
//   b) PMR-arena allocated (the cancellable_dispatch node from session_arena,
//      the per-awaiter promise from message_arena).
//
// Erratum E-4 warm-up (mirrors sync_alloc_guard_test.cpp): asio's
// cancellation_slot has NO allocator-binding hook. On the first
// cancellation-slot assignment on a thread asio's detail::thread_info_base
// performs ONE global aligned_new for its per-thread recycling block.
// The warm-up pass before alloc_guard_start() primes this recycler so the
// MEASURED window is zero-global-heap (steady state).
//
// [const §VIII.5] / [const §XI.6]; D-6; N-P2-4.

#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <atomic>
#include <cstddef>
#include <fixpp/core/cancellable_dispatch.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/session_executor.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <memory_resource>

#include "support/minimal_dictionary.hpp"

// mallocnesia replaces these weak no-ops with its interceptor scope markers.
extern "C" {
__attribute__((weak)) void alloc_guard_start();
__attribute__((weak)) void alloc_guard_end();
}

namespace {

using fixpp::core::cancellable_dispatch;
using fixpp::core::EngineConfig;
using fixpp::core::error;
using fixpp::core::expected_t;
using fixpp::core::make_session_executor;
using fixpp::session::Session;
using fixpp::session::SessionConfig;
using fixpp::session::threading_mode;

// ─────────────────────────────────────────────────────────────────────────────
// Backing arenas for the entire corpus — allocated BEFORE the guard markers.
// Sized to match the full 10⁴-message corpus pass.
//
// session_arena: cancellable_dispatch allocates ~64 B per call (shared_ptr
// control block for the atomic<bool> flag, via allocate_shared with a PMR
// allocator). 10003 calls × 80 B = ~800 KB → round up to 1 MB.
// Monotonic, using get_default_resource() as upstream so overflow falls back
// to the global heap gracefully OUTSIDE the guard window (outside the guard
// the test itself may touch global heap — only the ioc.run() window matters).
//
// For the guard assertion: under mallocnesia, allocations from the monotonic
// buffer are invisible (they're pointer-bumps into pre-allocated std::array
// memory) so the guard measures ONLY asio/STL global-heap activity that
// cannot be PMR-redirected.
// ─────────────────────────────────────────────────────────────────────────────

// 1 MB for session_arena — enough for 10003 cancellable_dispatch calls.
constexpr std::size_t kSessionArenaSize = 1024 * 1024;
// 64 KB for message_arena (per-awaiter promise frames on PMR fallback).
constexpr std::size_t kMessageArenaSize = 65536;

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// DispatchAllocGuard / HotPathNoGlobalHeapAlloc
//
// Simulates the parse→fromApp hot path over 10⁴ messages.
//
// Strategy: on a SINGLE io_context (single-threaded, so there is no real
// strand/cross-thread hop for the dispatch) we co_spawn a loop that does
// 10⁴ rounds of:
//   1. The "parse" step is elided (wire parsing is covered by T051-wire).
//      We simulate a parsed-message handler directly.
//   2. cancellable_dispatch(se, slot, handler) — the actual dispatch seam.
//   3. The handler runs (no-op body = the "fromApp").
//
// Because the session strand = the same single-threaded io_context, ASIO's
// dispatch is immediate (same-thread fast path), making this HALO-elision-
// friendly per [const §XI.6] / [2d §6.2].
//
// The global-heap window is between alloc_guard_start() and alloc_guard_end().
// All steady-state dispatch node allocations come from session_arena (via the
// PMR polymorphic_allocator in cancellable_dispatch) — no global heap in
// steady state after the warm-up round.
// ─────────────────────────────────────────────────────────────────────────────

TEST(DispatchAllocGuard, HotPathNoGlobalHeapAlloc) {
    // Build all objects BEFORE the guard markers.
    // Use static storage to avoid a 1 MB stack frame (some platforms have
    // 8 MB default stack; 1 MB is borderline with test harness overhead).
    static std::array<std::byte, kSessionArenaSize> session_buf;
    static std::array<std::byte, kMessageArenaSize> message_buf;
    session_buf.fill(std::byte{0});
    message_buf.fill(std::byte{0});

    // Session arena: monotonic. Uses get_default_resource() as upstream so
    // overflow outside the guard window doesn't abort; inside the guard we
    // assert the hot path never overflows (the 1 MB buffer accommodates all
    // 10003 calls; any overflow would be a global-heap hit caught by mallocnesia).
    std::pmr::monotonic_buffer_resource session_mr{session_buf.data(), session_buf.size(),
                                                   std::pmr::get_default_resource()};
    // Message arena: per-awaiter promise frames (PMR fallback).
    std::pmr::monotonic_buffer_resource message_mr{message_buf.data(), message_buf.size(),
                                                   std::pmr::get_default_resource()};

    asio::io_context ioc;

    // Engine + session config wired up with the PMR arenas.
    EngineConfig engine;
    engine.executor = ioc.get_executor();
    engine.default_session_resource = &session_mr;

    SessionConfig cfg;
    cfg.session_arena = &session_mr;
    cfg.message_arena = &message_mr;
    cfg.mode = threading_mode::per_session_strand;
    cfg.dictionary = fixpp::test_support::make_minimal_dictionary();

    Session sess{engine, cfg};
    auto se_result = make_session_executor(ioc.get_executor(), threading_mode::per_session_strand,
                                           /*attested=*/false, &sess);
    ASSERT_TRUE(se_result.has_value()) << "make_session_executor failed";
    auto se = *se_result;

    // ── Erratum E-4 warm-up pass ─────────────────────────────────────────
    // Prime asio's per-thread cancellation recycler before the guard window.
    // Run 3 warm-up cancellable_dispatch rounds (prime recycler + caches).
    constexpr int WARMUP = 3;
    std::atomic<int> warmup_count{0};
    asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            for (int i = 0; i < WARMUP; ++i) {
                asio::cancellation_signal sig;
                auto rc = co_await cancellable_dispatch(se, sig.slot(), [&warmup_count] {
                    warmup_count.fetch_add(1, std::memory_order_relaxed);
                });
                (void)rc;
                co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
            }
            co_return;
        },
        asio::detached);
    ioc.run();
    ioc.restart();
    ASSERT_EQ(warmup_count.load(), WARMUP);

    // Reset the message arena (warm-up may have used it for PMR fallback).
    message_mr.release();
    new (&message_mr) std::pmr::monotonic_buffer_resource{message_buf.data(), message_buf.size(),
                                                          std::pmr::null_memory_resource()};

    // ── Measured window ───────────────────────────────────────────────────
    constexpr int CORPUS_SIZE = 10'000;
    std::atomic<int> handler_count{0};
    bool corpus_done = false;

    // Spawn the corpus coroutine BEFORE the guard markers so the coroutine
    // frame allocation (if any) is outside the measured window.
    asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            for (int i = 0; i < CORPUS_SIZE; ++i) {
                asio::cancellation_signal sig;
                auto rc = co_await cancellable_dispatch(se, sig.slot(), [&handler_count] {
                    handler_count.fetch_add(1, std::memory_order_relaxed);
                });
                EXPECT_TRUE(rc.has_value());
                // Yield periodically to unwind the strand's inline-resume
                // recursion depth (mirrors bench warm-up trampoline pattern).
                if ((i & 255) == 255)
                    co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
            }
            corpus_done = true;
            co_return;
        },
        asio::detached);

    if (alloc_guard_start) alloc_guard_start();
    ioc.run();
    if (alloc_guard_end) alloc_guard_end();

    EXPECT_TRUE(corpus_done);
    EXPECT_EQ(handler_count.load(), CORPUS_SIZE);
}
