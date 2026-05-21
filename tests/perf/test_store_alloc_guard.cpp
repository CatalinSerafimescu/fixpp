// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/perf/test_store_alloc_guard.cpp
//
// 008-message-store seam 14 — MemoryStore alloc-guard.
//
// Anchor: SC-007 / FR-027 / [const §VIII.5] / reference_mallocnesia_path.
//
// Drives 10⁴ messages through a MemoryStore session-shaped harness and asserts
// ZERO global-heap new/delete/malloc calls between alloc_guard_start() and
// alloc_guard_end() (the steady-state store() loop under bounded policy).
//
// mallocnesia (tools/mallocnesia/libmallocnesia.so) provides the interceptor
// via LD_PRELOAD; the weak symbols below are replaced by the shared library at
// load time. Without LD_PRELOAD the test still executes (counting is a no-op)
// so it can be run under regular ctest without the preload.
//
// Warm-up rationale (Erratum E-4 / feedback_asio_cancellation_slot_no_allocator_hook):
// asio's per-thread cancellation recycler may do one global alloc on the FIRST
// slot assignment per thread. We run WARMUP_ITER store() calls BEFORE the
// guard window to prime the recycler. The measured window (inside the guard)
// is steady-state and expected to be ZERO global-heap allocations.
//
// check_alloc.py post-link symbol scan:
//   python3 tools/check_alloc.py \
//       --binary build/linux-clang-debug/tests/perf/perf_store_alloc_guard \
//       --module fixpp::session::MemoryStore::store
//
// Run with mallocnesia:
//   LD_PRELOAD=tools/mallocnesia/libmallocnesia.so \
//       build/linux-clang-debug/tests/perf/perf_store_alloc_guard \
//       --gtest_filter='*Mallocnesia*'

#include <gtest/gtest.h>

#include <algorithm>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/seqnum.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <vector>

// mallocnesia replaces these weak no-ops with its interceptor scope markers.
// Without LD_PRELOAD they remain no-ops and the test exercises the logic
// without the alloc counting (so it passes trivially — the mallocnesia run
// is the real gate).
extern "C" {
__attribute__((weak)) void alloc_guard_start() {}
__attribute__((weak)) void alloc_guard_end() {}
}

namespace {

using fixpp::session::capacity_policy;
using fixpp::session::direction_t;
using fixpp::session::MemoryStore;
using fixpp::session::seqnum_min;
using fixpp::session::seqnum_t;

constexpr int kWarmupIter = 20;        // prime asio's cancellation recycler
constexpr int kMeasuredIter = 10'000;  // the measured steady-state window

// Build a synthetic FIX-like 200-byte frame (opaque to the store).
inline std::vector<std::byte> make_frame(seqnum_t seq) {
    std::string raw;
    raw +=
        "8=FIX.4.4\x01"
        "35=D\x01"
        "34=";
    raw += std::to_string(static_cast<unsigned>(seq));
    raw +=
        "\x01"
        "49=SENDER\x01"
        "10=123\x01";
    if (raw.size() < 200) raw.append(200 - raw.size(), 'X');

    std::vector<std::byte> result(raw.size());
    std::transform(raw.begin(), raw.end(), result.begin(),
                   [](char c) { return static_cast<std::byte>(c); });
    return result;
}

}  // namespace

// ── StoreAllocGuard / Mallocnesia ─────────────────────────────────────────────
//
// Strategy:
//   1. Construct a MemoryStore with bounded policy (so the slab is pre-allocated
//      at ctor) using new_delete_resource(). The guard window starts AFTER ctor.
//   2. Pre-build all frames BEFORE alloc_guard_start() — the frame vectors
//      are heap-allocated, which is fine because they're built outside the guard.
//   3. Warm up kWarmupIter store() calls to prime asio's per-thread recycler.
//   4. Call alloc_guard_start().
//   5. Drive kMeasuredIter store() calls in a BATCH coroutine — a single
//      co_spawn drives all kMeasuredIter store() calls so the coroutine
//      infrastructure is warm. Under bounded policy and after warm-up, store()
//      must make ZERO global-heap allocations (FR-007 / I-10).
//   6. Call alloc_guard_end() — mallocnesia exits(1) if count > 0.
//
TEST(StoreAllocGuard, Mallocnesia_ZeroGlobalHeapStoreSteadyState) {
    // ── Construction (outside guard window) ──────────────────────────────────

    // Use bounded policy so the slab is pre-allocated at ctor; store() must
    // then perform ZERO global-heap allocations (FR-007 / I-10 / SC-007).
    // Capacity: warm-up (20) + measured (10,000) + margin (200) iterations,
    // all outbound. max_frame_bytes = 1024 → slab = 10220 × 1 KiB ≈ 10 MiB,
    // well within the 1 GiB engine cap ([2e §1.2]).
    const std::size_t kTotalCapacity =
        static_cast<std::size_t>(kWarmupIter + kMeasuredIter + 200);
    MemoryStore::Config cfg;
    cfg.policy = capacity_policy::bounded;
    cfg.inbound_capacity = 0;
    cfg.outbound_capacity = kTotalCapacity;
    cfg.max_frame_bytes = 1024;  // 1 KiB slots; frames are ~200 B (fits)
    cfg.store_resource = std::pmr::new_delete_resource();
    MemoryStore store{cfg};

    // Pre-build all frames outside the guard window.
    const int kTotalIter = kWarmupIter + kMeasuredIter;
    std::vector<std::vector<std::byte>> frames;
    frames.reserve(static_cast<std::size_t>(kTotalIter));
    for (int i = 0; i < kTotalIter; ++i) {
        frames.push_back(make_frame(static_cast<seqnum_t>(i + 1)));
    }

    // io_context for running coroutines. Stays alive for the whole test.
    asio::io_context ioc;

    // Helper: run a batch of N store() calls starting at seq `start` using
    // a single co_spawn (all in one coroutine, no repeated co_spawn overhead).
    auto run_batch = [&](int start, int count) {
        bool all_ok = true;
        // Spawn then run: the future is only retrieved AFTER ioc.run() drains.
        auto fut = asio::co_spawn(
            ioc.get_executor(),
            [&]() -> asio::awaitable<void> {
                for (int i = 0; i < count; ++i) {
                    int fi = start + i;
                    auto result = co_await store.store(
                        static_cast<seqnum_t>(fi + 1),
                        std::span<const std::byte>(frames[static_cast<std::size_t>(fi)]),
                        direction_t::outbound);
                    if (!result.has_value()) all_ok = false;
                }
            },
            asio::use_future);
        // Drive the context until all handlers complete.
        ioc.run();
        ioc.restart();
        fut.get();  // propagate any exception; coroutine is done by now
        return all_ok;
    };

    // ── Warm-up (outside guard window) ───────────────────────────────────────
    ASSERT_TRUE(run_batch(0, kWarmupIter)) << "warm-up store() calls failed";

    // ── Measured window ───────────────────────────────────────────────────────
    alloc_guard_start();

    bool measured_ok = run_batch(kWarmupIter, kMeasuredIter);

    alloc_guard_end();
    // If mallocnesia was LD_PRELOADed and detected any global heap allocation,
    // alloc_guard_end() will have already called exit(1) before reaching here.

    EXPECT_TRUE(measured_ok) << "store() failed during measured window";
}
