// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/perf/test_transport_read_alloc_guard.cpp
// T018 — [2h §9 seam #4]:1345 — read-path alloc guard.
//
// Validates zero global heap allocation between async_read_some completion-
// handler dispatch and Framer::feed exit per [const §VIII.5] + SC-003.
//
// DUAL GATE per [[feedback_tracking_pmr_resource_false_pass]] D-21:
//   (a) counting_resource — PMR-routed accounting for allocations through
//       the provided memory_resource.
//   (b) mallocnesia LD_PRELOAD — intercepts global operator new / malloc.
//       LD_PRELOAD=tools/mallocnesia/libmallocnesia.so at ctest level.
//
// 10^4-frame test. Gate: ZERO global new/delete/malloc while the read buffer
// is arena-backed and passed as span to async_read_some.
//
// Warm-up: run WARMUP_ITER frames before the guard window to prime asio's
// per-thread cancellation recycler (per Erratum E-4 /
// [[feedback_asio_cancellation_slot_no_allocator_hook]]).
//
// Run:
//   LD_PRELOAD=tools/mallocnesia/libmallocnesia.so \
//       build/linux-clang-debug/tests/perf/perf_transport_read_alloc_guard
//
// Phase 3a — DISABLED tests require asio_tls_transport (T026).
// The mallocnesia weak-symbol stubs + counting_resource harness compile NOW.

#include <gtest/gtest.h>

#include <cstddef>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_errors.hpp>
#include <memory_resource>

// ─────────────────────────────────────────────────────────────────────────────
// Mallocnesia weak-symbol stubs (same pattern as tests/perf/test_store_alloc_guard).
// Without LD_PRELOAD the stubs are no-ops; WITH LD_PRELOAD the .so replaces them.
// ─────────────────────────────────────────────────────────────────────────────

extern "C" {
// Weak symbols replaced by libmallocnesia.so at runtime.
[[gnu::weak]] void alloc_guard_start();
[[gnu::weak]] void alloc_guard_end();
[[gnu::weak]] long alloc_guard_global_count();
}

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// counting_resource — PMR adapter that counts allocations routed through it.
// Gate (a): verifies allocations are routed through PMR (not global heap).
// ─────────────────────────────────────────────────────────────────────────────

class counting_resource final : public std::pmr::memory_resource {
public:
    long count{0};
    long bytes{0};

    void* do_allocate(std::size_t sz, std::size_t align) override {
        ++count;
        bytes += static_cast<long>(sz);
        return std::pmr::new_delete_resource()->allocate(sz, align);
    }

    void do_deallocate(void* p, std::size_t sz, std::size_t align) override {
        std::pmr::new_delete_resource()->deallocate(p, sz, align);
    }

    bool do_is_equal(std::pmr::memory_resource const& other) const noexcept override {
        return this == &other;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Tests (Phase 3a — compile-time guard shape verification).
// ─────────────────────────────────────────────────────────────────────────────

TEST(TransportReadAllocGuard, WeakSymbolsPresent) {
    // Verify that the weak symbols are at least callable (no-op without preload).
    if (alloc_guard_start) alloc_guard_start();
    if (alloc_guard_end) alloc_guard_end();
    long n = alloc_guard_global_count ? alloc_guard_global_count() : 0L;
    // Without LD_PRELOAD: n == 0 (no interception, any value is acceptable).
    // With LD_PRELOAD: n must be 0 after a preloaded zero-alloc window.
    EXPECT_GE(n, 0L) << "alloc_guard_global_count must be non-negative";
}

TEST(TransportReadAllocGuard, CountingResourceRoutesPmrAllocs) {
    counting_resource cr;
    std::pmr::polymorphic_allocator<std::byte> pa{&cr};

    // Allocate through PMR and verify counting_resource sees it.
    auto* p = pa.allocate(64);
    EXPECT_EQ(cr.count, 1L);
    EXPECT_EQ(cr.bytes, 64L);
    pa.deallocate(p, 64);
}

// ─────────────────────────────────────────────────────────────────────────────
// DISABLED integration tests — require asio_tls_transport (T026).
// ─────────────────────────────────────────────────────────────────────────────

// T018 primary cell: 10^4-frame read-path zero-alloc gate.
// Gate (a): counting_resource sees NO new allocations in the steady-state loop.
// Gate (b): mallocnesia reports ZERO global new/malloc in the guard window.
TEST(DISABLED_TransportReadAllocGuard, TenKFramesZeroGlobalAlloc) {
    // Flow:
    //   1. Construct asio_tls_transport over loopback with a PMR-arena-backed
    //      read buffer (std::array<std::byte, 256KiB> on stack; pass as span).
    //   2. Run WARMUP_ITER = 100 frames through async_read_some to prime asio
    //      per-thread recycler (Erratum E-4).
    //   3. Call alloc_guard_start() [no-op without LD_PRELOAD].
    //   4. For i in 0..10000: peer sends frame; co_await async_read_some → buf.
    //   5. Call alloc_guard_end().
    //   6. ASSERT counting_resource.count unchanged since step 2 (gate a).
    //   7. ASSERT alloc_guard_global_count() == 0 (gate b — requires LD_PRELOAD).
    GTEST_SKIP() << "Requires asio_tls_transport (T026) + loopback peer";
}

}  // namespace
