// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/perf/test_store_alloc_guard.cpp
//
// 008-message-store seam 14 — MemoryStore + FileStore alloc-guard.
//
// Anchor: SC-007 / FR-027 / [const §VIII.5] / reference_mallocnesia_path.
//
// Test 1 (MemoryStore): drives 10⁴ messages through a MemoryStore session-shaped
// harness, the steady-state store() loop under bounded policy, inside an
// alloc_guard window.
//
// Test 2 (FileStore::retrieve() — N4 gate): stores N frames into a FileStore backed by
// a counting_resource, then calls retrieve() inside an alloc_guard window and checks
// that the snapshot + frame-read allocations are routed through the store_resource.
//
// The global-heap half. SC-007 / FR-027 ask these windows for zero global-heap
// allocation. Test 1's window is fixpp#544's W-D, and its interceptor registration is
// Release-only (tests/perf/CMakeLists.txt). ⚠️ Test 2's global-heap half is NOT CHECKED:
// FileStore `retrieve` is disclosed, not gated (fixpp#544, L-497-1). Its alloc_guard
// markers stay so the window can be run by hand, as tests/perf/CMakeLists.txt shows at
// this binary's registration.
//
// Warm-up rationale (Erratum E-4 / feedback_asio_cancellation_slot_no_allocator_hook):
// asio's cancellation recycler may do one global alloc on the FIRST slot
// assignment in a scheduler call: its cache lives for one io_context run-family
// call, not for the thread (Erratum E-6). We run WARMUP_ITER store() calls BEFORE
// the guard window, in the same scheduler call, to prime the recycler.

#include <gtest/gtest.h>

#include <algorithm>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <cstddef>
#include <cstdint>
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
#include <utility>
#include <vector>

// mallocnesia replaces these weak no-ops with its interceptor scope markers when it
// is preloaded: its Release registration preloads it for the MemoryStore steady-state
// cell (tests/perf/CMakeLists.txt); the FileStore cell's window is run by hand (see the
// file header).
#include "support/alloc_guard_markers.hpp"
#include "support/temp_dir.hpp"  // replaced a local copy of this helper (#404).
// NOT byte-identical, and the difference is on disk: the local one prefixed
// "fixpp_perf_", the shared one prefixes "fixpp_test_" -- only current_pid()
// was identical. The tag below carries "perf_" so the name stays greppable.
#include "support/recycler_driver.hpp"

namespace {

using fixpp::session::capacity_policy;
using fixpp::session::direction_t;
using fixpp::session::FileStore;
using fixpp::session::FileStoreFactory;
using fixpp::session::MemoryStore;
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
    std::ranges::transform(raw, result.begin(), [](char c) { return static_cast<std::byte>(c); });
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
// fixpp#544 (B35; `.specify/544-hot-path-zero-alloc.md` §3, W-D): this cell is W-D, and its
// window is the driver-template shape (tests/support/recycler_driver.hpp). One driver
// coroutine, spawned before the window, runs the warm-up and the windowed store() calls
// under one `ioc.run()`, through exactly one test-owned wrapper frame, which models
// `run_liveness_loop` (long-lived) -> `store_then_emit` -> `MemoryStore::store`, the
// shallowest production chain to store(). A window must not spawn with `use_future`
// (its handler allocates its promise) or span several `ioc.run()` calls (each is a fresh
// scheduler call with an empty frame cache). Its interceptor registration
// (tests/perf/CMakeLists.txt) is Release-only.
//
// Strategy:
//   1. Construct a MemoryStore with bounded policy (so the slab is pre-allocated
//      at ctor) using new_delete_resource(). The guard window starts AFTER ctor.
//   2. Pre-build all frames BEFORE the driver starts — the frame vectors are
//      heap-allocated, which is fine because they're built outside the guard.
//   3. The driver warms up kWarmupIter store() calls, calls alloc_guard_start(), drives
//      kMeasuredIter store() calls (FR-007 / I-10 ask store() for zero global-heap
//      allocations here) and calls alloc_guard_end() — mallocnesia exits(1) if count > 0.
//
TEST(StoreAllocGuard, Mallocnesia_ZeroGlobalHeapStoreSteadyState) {
    // ── Construction (outside guard window) ──────────────────────────────────

    // Use bounded policy so the slab is pre-allocated at ctor; store() must
    // need no further slab allocation (FR-007 / I-10 / SC-007).
    // Capacity: every warm-up and window iteration, plus a margin, all outbound, in
    // 1 KiB slots.
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

    // The callee, a non-coroutine forwarder: one store() per call, in seq order. The
    // wrapper frame above it is the template's.
    std::size_t next = 0;
    auto store_next = [&] {
        auto const i = next++;
        return store.store(static_cast<seqnum_t>(i + 1), std::span<const std::byte>(frames[i]),
                           direction_t::outbound);
    };

    asio::io_context ioc;
    auto const out = fixpp::test_support::recycler::run_driver_window<1>(
        ioc, ioc.get_executor(), store_next,
        fixpp::test_support::recycler::window_spec{.warm = kWarmupIter, .measured = kMeasuredIter});
    // If mallocnesia was LD_PRELOADed and detected any global heap allocation,
    // alloc_guard_end() will have already called exit(1) before reaching here.

    EXPECT_TRUE(out.closed) << "the driver never reached the end of its window";
    EXPECT_EQ(out.iterations, kTotalIter);
    EXPECT_EQ(out.ok, kTotalIter) << "store() failed during the warm-up or the measured window";
}

// ── FileStore::retrieve() alloc-guard (N4 gate) ────────────────────────────────
//
// Verifies that FileStore::retrieve() does NOT allocate from the global heap
// during steady-state replay. Both the index snapshot (std::pmr::vector<IndexEntry>)
// and the frame-read scratch (retrieve_scratch_) must be routed through the
// cfg.store_resource (counting_resource here) rather than global new/delete.
//
// Gate structure (two layers; only Layer 1 is checked):
//   Layer 1 — counting_resource: all allocations that DO occur during retrieve()
//             must go through cfg.store_resource (count increases, but they're
//             PMR-routed). This is verified by EXPECT_GE(after, baseline).
//   Layer 2 — the global-heap half: NOT CHECKED (see the file header). Under a
//             hand run with the interceptor, an escape exits(1) in alloc_guard_end().
//
// Warm-up: one retrieve() run outside the guard window to prime asio's
// cancellation recycler (same rationale as store steady-state test above).
#ifndef _WIN32
TEST(StoreAllocGuard, Mallocnesia_ZeroGlobalHeapFileStoreRetrieveSteadyState) {
    constexpr int kFileStoreFrames = 50;  // small N: we exercise the per-frame path, not scale
    constexpr std::size_t kMaxFrame = 1024;

    // ── Setup (outside guard window) ─────────────────────────────────────────
    auto dir = fixpp::test_support::unique_temp_dir("perf_filestore_retrieve");

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
            for (int i = 0; std::cmp_less(i, frames.size()); ++i) {
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

    // ⚠️ RELEASE THE STORE FIRST. `minted` owns the FileStore and is not destroyed
    // until this function returns, so removing the directory here would violate the
    // contract stated in support/temp_dir.hpp -- the one this change is about. It is
    // invisible on this test (it is POSIX-only, and POSIX permits unlink-while-open),
    // which is exactly why the contract has to be honoured rather than observed.
    minted.value() = nullptr;
    fixpp::test_support::remove_temp_dir(dir);
}
#endif  // !_WIN32}
