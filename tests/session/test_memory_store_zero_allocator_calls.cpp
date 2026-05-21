// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_memory_store_zero_allocator_calls.cpp
//
// Seam 15 — MemoryStore zero-allocator-calls under bounded policy
// (FR-007 / I-10 / SC-007).
//
// Test plan:
//   1. Instrument a tracking PMR resource that counts every allocate() call.
//   2. Construct a MemoryStore with capacity_policy::bounded, passing the
//      tracking resource as store_resource.
//   3. Record the allocator counter POST-CONSTRUCTION (baseline).
//   4. Execute 10,000 store() calls.
//   5. Assert the tracking counter is UNCHANGED from the baseline — no
//      allocator calls during the store phase (FR-007 / I-10).
//
// TDD: linker-RED until T020 (MemoryStore) ships the bounded pre-allocation.
#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <cstddef>
#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/seqnum.hpp>
#include <memory_resource>
#include <span>

#include "_fixtures_/test_double_fsm.hpp"

namespace {

using fixpp::session::capacity_policy;
using fixpp::session::direction_t;
using fixpp::session::MemoryStore;
using fixpp::session::seqnum_t;
using fixpp::store_test::make_test_frame;

// ── Tracking PMR resource ────────────────────────────────────────────────────
//
// Wraps std::pmr::new_delete_resource() and counts allocate() calls.
// deallocate() is NOT counted (we only care about allocation growth post-ctor).
class counting_resource final : public std::pmr::memory_resource {
public:
    explicit counting_resource(
        std::pmr::memory_resource* upstream = std::pmr::new_delete_resource()) noexcept
        : upstream_(upstream) {}

    // Number of allocate() calls recorded so far.
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

// ── Test ─────────────────────────────────────────────────────────────────────

TEST(MemoryStoreZeroAllocatorCalls, BoundedPolicyZeroAllocsAfterConstruction) {
    counting_resource mr;

    // ── Construct the store (allocation ALLOWED here — pre-allocating index) ─
    constexpr std::size_t kCapacity = 10'000;
    MemoryStore::Config cfg;
    cfg.policy = capacity_policy::bounded;
    cfg.inbound_capacity = kCapacity;
    cfg.outbound_capacity = kCapacity;
    cfg.max_frame_bytes = 128;  // small frames: 128 bytes max
    cfg.store_resource = &mr;

    MemoryStore store{cfg};

    // ── Baseline: record allocator counter AFTER construction ─────────────────
    const long long baseline = mr.allocate_count();
    // Construction is allowed to allocate; we don't assert on how many.
    // We only assert that no FURTHER allocations happen during store() calls.

    // ── Execute 10,000 store() calls ─────────────────────────────────────────
    asio::thread_pool pool{1};
    asio::co_spawn(
        pool.get_executor(),
        [&store]() -> asio::awaitable<void> {
            const auto dir = direction_t::outbound;
            for (std::size_t i = 0; i < 10'000; ++i) {
                auto seq = static_cast<seqnum_t>(i + 1);
                // Use a fixed-size small frame that fits in max_frame_bytes
                const std::byte frame[16] = {
                    static_cast<std::byte>(seq & 0xFF),
                    static_cast<std::byte>((seq >> 8) & 0xFF),
                };
                auto r = co_await store.store(seq, std::span<const std::byte>(frame, 2), dir);
                EXPECT_TRUE(r.has_value())
                    << "store() failed at seq=" << seq << " error=" << static_cast<int>(r.error());
                if (!r) {
                    co_return;
                }
            }
        },
        asio::use_future)
        .get();

    // ── Assert: counter unchanged from baseline ────────────────────────────────
    const long long after = mr.allocate_count();
    EXPECT_EQ(after, baseline) << "MemoryStore::store() performed " << (after - baseline)
                               << " allocator call(s) after construction under bounded policy "
                               << "(FR-007 / I-10 require ZERO allocations post-construction)";
}

// ── Confirm: unbounded policy is EXEMPT from the zero-alloc guarantee ─────────
//
// This is NOT a requirements test (unbounded is documented as test/embedded
// only) but it documents that the zero-alloc contract is bounded-only.
// We don't assert allocate_count() for unbounded — just that store() succeeds.
TEST(MemoryStoreZeroAllocatorCalls, UnboundedPolicySucceedsWithAllocations) {
    counting_resource mr;

    constexpr std::size_t kCount = 100;
    MemoryStore::Config cfg;
    cfg.policy = capacity_policy::unbounded;
    cfg.inbound_capacity = 0;
    cfg.outbound_capacity = 0;
    cfg.max_frame_bytes = 128;
    cfg.store_resource = &mr;

    MemoryStore store{cfg};

    asio::thread_pool pool{1};
    asio::co_spawn(
        pool.get_executor(),
        [&store]() -> asio::awaitable<void> {
            const auto dir = direction_t::inbound;
            for (std::size_t i = 0; i < kCount; ++i) {
                auto seq = static_cast<seqnum_t>(i + 1);
                auto frame = fixpp::store_test::make_test_frame(seq, dir);
                auto r = co_await store.store(seq, std::span<const std::byte>(frame), dir);
                EXPECT_TRUE(r.has_value());
                if (!r) {
                    co_return;
                }
            }
        },
        asio::use_future)
        .get();
    // No assertion on allocator count — just that it succeeded
    SUCCEED();
}

}  // namespace
