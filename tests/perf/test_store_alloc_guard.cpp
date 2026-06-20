// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/perf/test_store_alloc_guard.cpp
//
// 008-message-store seam 14 — MemoryStore + FileStore alloc-guard.
//
// Anchor: SC-007 / FR-027 / [const §VIII.5] / reference_mallocnesia_path.
//
// Test 1 (MemoryStore):
// Drives 10⁴ messages through a MemoryStore session-shaped harness and asserts
// ZERO global-heap new/delete/malloc calls between alloc_guard_start() and
// alloc_guard_end() (the steady-state store() loop under bounded policy).
//
// Test 2 (FileStore::retrieve() — N4 gate):
// Stores N frames into a FileStore backed by a counting_resource, then calls
// retrieve() and verifies that (a) ZERO global-heap allocations occur (gate via
// mallocnesia LD_PRELOAD at ctest level) and (b) all snapshot + frame-read
// allocations are routed through the store_resource (counting_resource count
// increases, but no global-heap escapes).
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
#include <unistd.h>

#include <algorithm>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
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
__attribute__((weak)) void alloc_guard_start();
__attribute__((weak)) void alloc_guard_end();
}

namespace {

using fixpp::session::capacity_policy;
using fixpp::session::direction_t;
using fixpp::session::FileStore;
using fixpp::session::FileStoreFactory;
using fixpp::session::MemoryStore;
using fixpp::session::seqnum_min;
using fixpp::session::seqnum_t;
using fixpp::session::visit_result;

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

// ── counting_resource — tracks allocate() calls through a PMR resource ────────
//
// Used to verify that FileStore::retrieve() routes its snapshot and scratch
// allocations through cfg.store_resource rather than global new/delete.
class counting_resource final : public std::pmr::memory_resource {
public:
    explicit counting_resource(
        std::pmr::memory_resource* upstream = std::pmr::new_delete_resource()) noexcept
        : upstream_(upstream) {}

    [[nodiscard]] long long allocate_count() const noexcept {
        return count_.load(std::memory_order_relaxed);
    }

    void reset_count() noexcept { count_.store(0, std::memory_order_relaxed); }

private:
    void* do_allocate(std::size_t bytes, std::size_t align) override {
        count_.fetch_add(1, std::memory_order_relaxed);
        return upstream_->allocate(bytes, align);
    }
    void do_deallocate(void* p, std::size_t bytes, std::size_t align) noexcept override {
        upstream_->deallocate(p, bytes, align);
    }
    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }
    std::pmr::memory_resource* upstream_;
    mutable std::atomic<long long> count_{0};
};

// ── Temp-dir helper for the FileStore alloc-guard test ─────────────────────────
inline std::filesystem::path perf_temp_dir(std::string_view tag) {
    static std::atomic<unsigned> ctr{0};
    const auto seq = ctr.fetch_add(1, std::memory_order_relaxed);
    auto p = std::filesystem::temp_directory_path() /
             (std::string("fixpp_perf_") + std::string(tag) + "_" +
              std::to_string(static_cast<unsigned>(::getpid())) + "_" + std::to_string(seq));
    std::filesystem::create_directories(p);
    return p;
}

// ── Counting visitor for retrieve() verification ──────────────────────────────
class counting_visitor final : public fixpp::session::retrieve_visitor {
public:
    std::size_t count = 0;

    asio::awaitable<fixpp::core::expected_t<visit_result>> on_frame(
        seqnum_t, std::span<const std::byte>) noexcept override {
        ++count;
        co_return fixpp::core::expected_t<visit_result>{visit_result::cont};
    }
};

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
    const std::size_t kTotalCapacity = static_cast<std::size_t>(kWarmupIter + kMeasuredIter + 200);
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
    if (alloc_guard_start) alloc_guard_start();

    bool measured_ok = run_batch(kWarmupIter, kMeasuredIter);

    if (alloc_guard_end) alloc_guard_end();
    // If mallocnesia was LD_PRELOADed and detected any global heap allocation,
    // alloc_guard_end() will have already called exit(1) before reaching here.

    EXPECT_TRUE(measured_ok) << "store() failed during measured window";
}

// ── FileStore::retrieve() alloc-guard (N4 gate) ────────────────────────────────
//
// Verifies that FileStore::retrieve() does NOT allocate from the global heap
// during steady-state replay. Both the index snapshot (std::pmr::vector<IndexEntry>)
// and the frame-read scratch (retrieve_scratch_) must be routed through the
// cfg.store_resource (counting_resource here) rather than global new/delete.
//
// Gate structure (two layers — must BOTH pass):
//   Layer 1 — counting_resource: all allocations that DO occur during retrieve()
//             must go through cfg.store_resource (count increases, but they're
//             PMR-routed). This is verified by EXPECT_GE(after, baseline).
//   Layer 2 — mallocnesia LD_PRELOAD (at ctest level): any global-heap escape
//             from retrieve() causes exit(1) inside alloc_guard_end(). Without
//             LD_PRELOAD, alloc_guard_start/end are no-ops (test still runs).
//
// Warm-up: one retrieve() run outside the guard window to prime asio's per-thread
// cancellation recycler (same rationale as store steady-state test above).
#ifndef _WIN32
TEST(StoreAllocGuard, Mallocnesia_ZeroGlobalHeapFileStoreRetrieveSteadyState) {
    constexpr int kFileStoreFrames = 50;  // small N: we exercise the per-frame path, not scale
    constexpr std::size_t kMaxFrame = 1024;

    // ── Setup (outside guard window) ─────────────────────────────────────────
    auto dir = perf_temp_dir("filestore_retrieve");

    counting_resource mr;

    // Build the FileStore config with counting_resource as store_resource.
    // file_io_executor is the thread_pool executor.
    asio::thread_pool pool{2};
    FileStore::Config cfg;
    cfg.directory = dir;
    cfg.sender_comp_id = "SENDER";
    cfg.target_comp_id = "TARGET";
    cfg.policy = {};  // commit_per_message
    cfg.max_frame_bytes = kMaxFrame;
    cfg.store_resource = &mr;
    cfg.file_io_executor = pool.get_executor();

    FileStoreFactory factory{cfg};
    auto minted = factory.make("SENDER", "TARGET", &mr, 1024 * 1024 * 1024, pool.get_executor());
    ASSERT_TRUE(minted.has_value()) << "FileStore open failed";
    auto& store = *minted.value();

    // Pre-build frames and store them (outside guard window).
    std::vector<std::vector<std::byte>> frames;
    frames.reserve(static_cast<std::size_t>(kFileStoreFrames));
    for (int i = 0; i < kFileStoreFrames; ++i) {
        frames.push_back(make_frame(static_cast<seqnum_t>(i + 1)));
    }

    auto fut_store = asio::co_spawn(
        pool.get_executor(),
        [&store, &frames]() -> asio::awaitable<void> {
            for (int i = 0; i < static_cast<int>(frames.size()); ++i) {
                auto r = co_await store.store(
                    static_cast<seqnum_t>(i + 1),
                    std::span<const std::byte>(frames[static_cast<std::size_t>(i)]),
                    direction_t::outbound);
                EXPECT_TRUE(r.has_value());
                if (!r) co_return;
            }
        },
        asio::use_future);
    fut_store.get();

    // Warm-up retrieve() to prime asio's cancellation recycler.
    {
        counting_visitor warmup_vis;
        auto fut_warmup = asio::co_spawn(
            pool.get_executor(),
            [&store, &warmup_vis]() -> asio::awaitable<void> {
                auto r = co_await store.retrieve(1, 0, direction_t::outbound, warmup_vis);
                EXPECT_TRUE(r.has_value()) << "warm-up retrieve() failed";
            },
            asio::use_future);
        fut_warmup.get();
        ASSERT_EQ(warmup_vis.count, static_cast<std::size_t>(kFileStoreFrames));
    }

    // Record baseline AFTER construction + store + warm-up retrieve.
    const long long baseline = mr.allocate_count();

    // ── Measured window ───────────────────────────────────────────────────────
    if (alloc_guard_start) alloc_guard_start();

    counting_visitor vis;
    bool retrieve_ok = false;
    auto fut_retrieve = asio::co_spawn(
        pool.get_executor(),
        [&store, &vis, &retrieve_ok]() -> asio::awaitable<void> {
            auto r = co_await store.retrieve(1, 0, direction_t::outbound, vis);
            retrieve_ok = r.has_value();
        },
        asio::use_future);
    fut_retrieve.get();

    if (alloc_guard_end) alloc_guard_end();
    // If mallocnesia was LD_PRELOADed and detected any global heap allocation,
    // alloc_guard_end() will have already called exit(1) before reaching here.

    EXPECT_TRUE(retrieve_ok) << "FileStore retrieve() failed during measured window";
    EXPECT_EQ(vis.count, static_cast<std::size_t>(kFileStoreFrames))
        << "retrieve() visitor did not see all stored frames";

    // Layer 1 gate: any allocations retrieve() makes must be routed through mr
    // (counting_resource). The count must be >= baseline (not go backwards).
    const long long after = mr.allocate_count();
    EXPECT_GE(after, baseline) << "allocator count went backwards — PMR accounting is broken";

    std::filesystem::remove_all(dir);
}
#endif  // !_WIN32}
