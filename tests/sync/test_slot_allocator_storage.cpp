// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_slot_allocator_storage.cpp — Seam #21, T057
//
// slot_allocator isolation unit test (post-Erratum E-4 re-anchor).
//
// Erratum E-4 (2026-05-19): asio 1.36.0's cancellation_slot has NO
// allocator-binding hook. slot_allocator is NOT bound to the cancellation slot.
// It is unit-verified IN ISOLATION as a storage-policy type.
//
// Three cases (re-anchored to waiter_record / inline buffer storage):
//   case 1 (mr == nullptr): inline slot_storage_ buffer path.
//     - allocate(≤ 32) returns non-null ptr into awaiter's slot_storage_.
//     - A second call to allocate (without deallocating) throws std::bad_alloc.
//     - allocate(33) also throws std::bad_alloc (> 32-byte capacity).
//   case 2: N/A — the waiter_record is never coroutine-frame-resident
//     (post-E-2/E-4 split: waiter_record lives in the mutex's per-mutex pool
//     or a pmr_waiter_block, never the coroutine frame). Not tested.
//   case 3 (mr != nullptr): pmr path (std::pmr::polymorphic_allocator-shaped).
//     - allocate returns a ptr inside the backing buffer.
//     - deallocate forwards to mr (does not crash).
//     - Exhausting the resource (tiny buffer, null upstream) → std::bad_alloc.
//
// NOTE: this test does NOT assert that slot_allocator is wired into async_lock
// or the cancellation slot. Post-E-4 it is NOT so wired. The test covers the
// storage-policy semantics of slot_allocator in isolation.
//
// Oracle: [2f §9 #21] — "slot_allocator storage policy (seam #21, post-E-4)".

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <memory_resource>
#include <new>
#include <stdexcept>

#include <fixpp/core/sync/async_mutex.hpp>

namespace {

using fixpp::sync::detail::async_mutex_awaiter;
using fixpp::sync::detail::slot_allocator;

// ─────────────────────────────────────────────────────────────────────────────
// Test 1 (case 1): mr == nullptr — inline slot_storage_ path.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SyncSlotAllocatorStorage, Case1InlineBufferAllocate) {
    async_mutex_awaiter awaiter{};
    slot_allocator alloc{&awaiter, nullptr};

    // allocate(32) must return a non-null pointer into awaiter.slot_storage_.
    std::byte* p = alloc.allocate(32);
    ASSERT_NE(p, nullptr);

    // The pointer must lie within the awaiter's slot_storage_ array.
    // slot_storage_ is 32 bytes aligned to 8 (see async_mutex_awaiter layout).
    auto* begin = awaiter.slot_storage_.data();
    auto* end   = begin + awaiter.slot_storage_.size();
    EXPECT_GE(p, begin) << "allocate() must return a ptr inside slot_storage_";
    EXPECT_LT(p, end)   << "allocate() must return a ptr inside slot_storage_";

    // A second allocate (without intervening deallocate) must throw bad_alloc
    // (used_inline_ == true; only one inline allocation at a time).
    EXPECT_THROW(alloc.allocate(1), std::bad_alloc);

    // deallocate resets used_inline_ → next allocate succeeds.
    alloc.deallocate(p, 32);
    EXPECT_NO_THROW({
        std::byte* p2 = alloc.allocate(16);
        EXPECT_NE(p2, nullptr);
        alloc.deallocate(p2, 16);
    });
}

TEST(SyncSlotAllocatorStorage, Case1InlineBufferOverSizeThrows) {
    async_mutex_awaiter awaiter{};
    slot_allocator alloc{&awaiter, nullptr};

    // allocate(33) exceeds the 32-byte inline capacity → std::bad_alloc.
    EXPECT_THROW(alloc.allocate(33), std::bad_alloc);

    // allocate(0) edge case: 0 <= 32, should return the inline buffer.
    // (zero-size allocations are valid in the Allocator concept.)
    EXPECT_NO_THROW({
        std::byte* p = alloc.allocate(0);
        EXPECT_NE(p, nullptr);
        alloc.deallocate(p, 0);
    });
}

// case 2: N/A — waiter_record is never frame-resident (post-E-2/E-4).
// No test; documented here per the seam oracle.

// ─────────────────────────────────────────────────────────────────────────────
// Test 2 (case 3): mr != nullptr — PMR path.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SyncSlotAllocatorStorage, Case3PmrAllocateDeallocate) {
    async_mutex_awaiter awaiter{};

    // Stack buffer for the PMR resource.
    std::array<std::byte, 256> buf{};
    std::pmr::monotonic_buffer_resource mbr{
        buf.data(), buf.size(),
        std::pmr::null_memory_resource()};

    slot_allocator alloc{&awaiter, &mbr};

    // allocate(32) returns a pointer inside the backing buffer.
    std::byte* p = alloc.allocate(32);
    ASSERT_NE(p, nullptr);

    auto* buf_begin = buf.data();
    auto* buf_end   = buf.data() + buf.size();
    EXPECT_GE(p, buf_begin) << "PMR allocate() ptr must be in the backing buffer";
    EXPECT_LT(p, buf_end)   << "PMR allocate() ptr must be in the backing buffer";

    // deallocate forwards to mr — must not crash.
    EXPECT_NO_THROW(alloc.deallocate(p, 32));
}

TEST(SyncSlotAllocatorStorage, Case3PmrExhaustionThrows) {
    async_mutex_awaiter awaiter{};

    // Tiny buffer: 8 bytes, null upstream → first large allocation must fail.
    std::array<std::byte, 8> tiny_buf{};
    std::pmr::monotonic_buffer_resource tiny_mr{
        tiny_buf.data(), tiny_buf.size(),
        std::pmr::null_memory_resource()};

    slot_allocator alloc{&awaiter, &tiny_mr};

    // Requesting 64 bytes from an 8-byte resource → std::bad_alloc.
    EXPECT_THROW(alloc.allocate(64), std::bad_alloc);
}

TEST(SyncSlotAllocatorStorage, EqualityOperator) {
    async_mutex_awaiter aw1{};
    async_mutex_awaiter aw2{};
    std::array<std::byte, 64> buf{};
    std::pmr::monotonic_buffer_resource mbr{
        buf.data(), buf.size(), std::pmr::null_memory_resource()};

    slot_allocator a1{&aw1, nullptr};
    slot_allocator a2{&aw1, nullptr};
    slot_allocator a3{&aw2, nullptr};
    slot_allocator a4{&aw1, &mbr};

    EXPECT_EQ(a1, a2)   << "same awaiter + same mr → equal";
    EXPECT_NE(a1, a3)   << "different awaiter → not equal";
    EXPECT_NE(a1, a4)   << "different mr → not equal";
}

}  // namespace
