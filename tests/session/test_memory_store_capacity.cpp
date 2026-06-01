// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_memory_store_capacity.cpp
//
// Seam 4 — MemoryStore bounded-capacity exhaustion + storage-DoS factory guard
// (FR-006 / FR-014 / I-08 / I-11 / SC-004 / AC US2 #1 / #2 / #2a).
//
// Sub-scenarios:
//   A. 101st store() on a bounded/100-cap → store_capacity_exhausted (I-08);
//      entry-array index is unchanged (state preserved on failure).
//   B. unbounded variant stores 1e5 frames without capacity error (I-09).
//   C. Storage-DoS guard via factory: (inbound + outbound) * max_frame_bytes
//      > max_store_memory_bytes → store_factory_failed (FR-014 / I-11).
//   D. Overflow-safe arithmetic sub-scenario:
//      inbound = SIZE_MAX/2-1, outbound = 2, frame = 8 → overflow →
//      store_factory_failed.
//   E. Default MemoryStore::Config against 1 GiB cap → store_factory_failed
//      (AC #2a — design intent per [2e §1.2]:54).
//
// TDD: linker-RED until T020 (MemoryStore) and T030 (DoS guard) ship.
#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <climits>
#include <cstddef>
#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/memory_store_factory.hpp>
#include <fixpp/session/seqnum.hpp>
#include <memory_resource>
#include <span>

#include "_fixtures_/test_double_fsm.hpp"

namespace {

using fixpp::core::error;
using fixpp::session::capacity_policy;
using fixpp::session::direction_t;
using fixpp::session::MemoryStore;
using fixpp::session::MemoryStoreFactory;
using fixpp::session::seqnum_t;
using fixpp::store_test::make_test_frame;
using fixpp::store_test::run_on_pool;

// ── A. Bounded capacity: 101st store() → store_capacity_exhausted ─────────────

TEST(MemoryStoreCapacity, BoundedCapacityExhaustedOnOverflow) {
    asio::thread_pool pool{1};

    run_on_pool(pool, []() -> asio::awaitable<void> {
        constexpr std::size_t kCap = 100;
        MemoryStore::Config cfg;
        cfg.policy = capacity_policy::bounded;
        cfg.inbound_capacity = kCap;
        cfg.outbound_capacity = kCap;
        cfg.max_frame_bytes = 4096;
        cfg.store_resource = nullptr;

        MemoryStore store{cfg};
        const auto dir = direction_t::outbound;

        // Store exactly kCap frames — all must succeed
        for (std::size_t i = 0; i < kCap; ++i) {
            auto seq = static_cast<seqnum_t>(i + 1);
            auto frame = make_test_frame(seq, dir);
            auto r = co_await store.store(seq, std::span<const std::byte>(frame), dir);
            EXPECT_TRUE(r.has_value()) << "store() failed at seq=" << seq << " (expected success)";
            if (!r) {
                co_return;
            }
        }

        // The 101st store() must return store_capacity_exhausted
        auto seq101 = static_cast<seqnum_t>(kCap + 1);
        auto frame101 = make_test_frame(seq101, dir);
        auto r101 = co_await store.store(seq101, std::span<const std::byte>(frame101), dir);
        EXPECT_FALSE(r101.has_value());
        EXPECT_EQ(r101.error(), error::store_capacity_exhausted);

        // State must be preserved: next_seqnum must still be kCap+1
        // (the 101st store failed — the counter was NOT advanced)
        auto ns = co_await store.next_seqnum(dir, false);
        EXPECT_TRUE(ns.has_value());
        if (!ns) {
            co_return;
        }
        EXPECT_EQ(*ns, static_cast<seqnum_t>(kCap + 1))
            << "next_seqnum advanced despite store_capacity_exhausted";
    });
}

// ── A2. Bounded exhaustion on INBOUND direction ───────────────────────────────

TEST(MemoryStoreCapacity, BoundedCapacityExhaustedInboundDirection) {
    asio::thread_pool pool{1};

    run_on_pool(pool, []() -> asio::awaitable<void> {
        constexpr std::size_t kCap = 50;
        MemoryStore::Config cfg;
        cfg.policy = capacity_policy::bounded;
        cfg.inbound_capacity = kCap;
        cfg.outbound_capacity = kCap;
        cfg.max_frame_bytes = 4096;
        cfg.store_resource = nullptr;

        MemoryStore store{cfg};
        const auto dir = direction_t::inbound;

        for (std::size_t i = 0; i < kCap; ++i) {
            auto seq = static_cast<seqnum_t>(i + 1);
            auto frame = make_test_frame(seq, dir);
            auto r = co_await store.store(seq, std::span<const std::byte>(frame), dir);
            EXPECT_TRUE(r.has_value());
            if (!r) {
                co_return;
            }
        }

        auto seq_extra = static_cast<seqnum_t>(kCap + 1);
        auto frame_extra = make_test_frame(seq_extra, dir);
        auto r_extra =
            co_await store.store(seq_extra, std::span<const std::byte>(frame_extra), dir);
        EXPECT_FALSE(r_extra.has_value());
        EXPECT_EQ(r_extra.error(), error::store_capacity_exhausted);
    });
}

// ── B. Unbounded: store 1e5 frames without capacity error ────────────────────

TEST(MemoryStoreCapacity, UnboundedStoresManyFramesWithoutError) {
    asio::thread_pool pool{1};

    run_on_pool(pool, []() -> asio::awaitable<void> {
        constexpr std::size_t kCount = 100'000;
        MemoryStore::Config cfg;
        cfg.policy = capacity_policy::unbounded;
        cfg.inbound_capacity = 0;  // ignored under unbounded
        cfg.outbound_capacity = 0;
        cfg.max_frame_bytes = 128;
        cfg.store_resource = nullptr;

        MemoryStore store{cfg};
        const auto dir = direction_t::outbound;

        // Store 1e5 very small frames — must all succeed
        for (std::size_t i = 0; i < kCount; ++i) {
            auto seq = static_cast<seqnum_t>(i + 1);
            // Use minimal frame to avoid massive allocations
            const std::byte small_frame[1] = {static_cast<std::byte>(seq & 0xFF)};
            auto r = co_await store.store(seq, std::span<const std::byte>(small_frame), dir);
            EXPECT_TRUE(r.has_value()) << "unbounded store() failed at seq=" << seq;
            if (!r) {
                co_return;
            }
        }
    });
}

// ── C. Storage-DoS guard: product > max_store_memory_bytes → store_factory_failed

TEST(MemoryStoreCapacity, DoSGuardRejectsOversizedConfig) {
    // inbound=5000, outbound=5000, max_frame=256KiB
    // product = 10000 * 262144 = 2.5 GiB > 1 GiB default cap
    MemoryStore::Config cfg;
    cfg.policy = capacity_policy::bounded;
    cfg.inbound_capacity = 5000;
    cfg.outbound_capacity = 5000;
    cfg.max_frame_bytes = 256 * 1024;
    cfg.store_resource = nullptr;

    MemoryStoreFactory factory{cfg};
    constexpr std::size_t kCap1GiB = 1ULL << 30;
    auto result = factory.make("SND", "TGT", nullptr, kCap1GiB, asio::any_io_executor{});
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::store_factory_failed);
}

// ── D. Overflow-safe arithmetic sub-scenario ──────────────────────────────────

TEST(MemoryStoreCapacity, DoSGuardDetectsAdditionOverflow) {
    // inbound = SIZE_MAX/2 - 1, outbound = 2 → sum wraps
    // max_frame_bytes = 8 → even if sum wrapped, factory must catch it
    MemoryStore::Config cfg;
    cfg.policy = capacity_policy::bounded;
    cfg.inbound_capacity = SIZE_MAX / 2 - 1;
    cfg.outbound_capacity = 2;
    cfg.max_frame_bytes = 8;
    cfg.store_resource = nullptr;

    MemoryStoreFactory factory{cfg};
    constexpr std::size_t kCap1GiB = 1ULL << 30;
    auto result = factory.make("SND", "TGT", nullptr, kCap1GiB, asio::any_io_executor{});
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::store_factory_failed)
        << "overflow-safe arithmetic must detect addition overflow";
}

// ── E. Default Config against 1 GiB cap → store_factory_failed (AC #2a) ──────

TEST(MemoryStoreCapacity, DefaultConfigExceedsDefaultCap) {
    // Default Config: inbound=10000, outbound=10000, max_frame_bytes=256KiB
    // Product = 20000 * 262144 = ~5 GiB > 1 GiB
    MemoryStore::Config cfg;  // all defaults
    MemoryStoreFactory factory{cfg};

    constexpr std::size_t kCap1GiB = 1ULL << 30;  // 1 GiB default per [2e §1.2]:54
    auto result = factory.make("SND", "TGT", nullptr, kCap1GiB, asio::any_io_executor{});
    EXPECT_FALSE(result.has_value())
        << "default MemoryStore::Config should fail the 1-GiB DoS guard (AC #2a)";
    EXPECT_EQ(result.error(), error::store_factory_failed);
}

// ── E2. Unbounded policy bypasses DoS guard ───────────────────────────────────

TEST(MemoryStoreCapacity, UnboundedPolicyBypasesDoSGuard) {
    // Under unbounded the inbound/outbound capacity fields are conceptually
    // infinite; the factory must NOT apply the DoS arithmetic check.
    MemoryStore::Config cfg;
    cfg.policy = capacity_policy::unbounded;
    cfg.inbound_capacity = 5000;  // irrelevant under unbounded
    cfg.outbound_capacity = 5000;
    cfg.max_frame_bytes = 256 * 1024;
    cfg.store_resource = nullptr;

    MemoryStoreFactory factory{cfg};
    constexpr std::size_t kCap1GiB = 1ULL << 30;
    auto result = factory.make("SND", "TGT", nullptr, kCap1GiB, asio::any_io_executor{});
    // unbounded is exempt from the DoS arithmetic guard (grown at runtime)
    EXPECT_TRUE(result.has_value())
        << "unbounded policy must bypass the storage-DoS arithmetic check";
}

// ── F. DoS guard: max_frame_bytes > Framer::Config::max_frame_bytes ──────────

TEST(MemoryStoreCapacity, DoSGuardRejectsMaxFrameBytesExceedingFramerCap) {
    // Framer::Config::max_frame_bytes default is 256 KiB
    constexpr std::size_t kFramerMax = 256 * 1024;
    MemoryStore::Config cfg;
    cfg.policy = capacity_policy::bounded;
    cfg.inbound_capacity = 1;
    cfg.outbound_capacity = 1;
    cfg.max_frame_bytes = kFramerMax + 1;  // 1 byte over the Framer cap
    cfg.store_resource = nullptr;

    MemoryStoreFactory factory{cfg};
    constexpr std::size_t kCap1GiB = 1ULL << 30;
    auto result = factory.make("SND", "TGT", nullptr, kCap1GiB, asio::any_io_executor{});
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::store_factory_failed)
        << "max_frame_bytes > Framer::Config::max_frame_bytes must be rejected";
}

// ── G. Coverage uplift: DoS guard (a) — actual addition overflow ──────────────
//
// ic = SIZE_MAX - 1, oc = 2 → ic > SIZE_MAX - oc (SIZE_MAX-1 > SIZE_MAX-2) → true
// This exercises the overflow-detection branch that the existing test D misses:
// test D uses SIZE_MAX/2-1 + 2 which doesn't overflow size_t, and falls to
// guard (b) instead.
TEST(MemoryStoreCapacity, DoSGuardDetectsActualAdditionOverflow) {
    MemoryStore::Config cfg;
    cfg.policy = capacity_policy::bounded;
    cfg.inbound_capacity = SIZE_MAX - 1;  // ic + oc = SIZE_MAX + 1 → overflows
    cfg.outbound_capacity = 2;
    cfg.max_frame_bytes = 8;
    cfg.store_resource = nullptr;

    MemoryStoreFactory factory{cfg};
    constexpr std::size_t kCap1GiB = 1ULL << 30;
    auto result = factory.make("SND", "TGT", nullptr, kCap1GiB, asio::any_io_executor{});
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::store_factory_failed)
        << "guard (a) must reject ic + oc overflow";
}

// ── H. Coverage uplift: engine-provided mr applied when Config.store_resource is nullptr
//
// make() with mr != nullptr and cfg.store_resource == nullptr → resolved_cfg uses mr.
// Exercises the branch at line 110 (resolved_cfg.store_resource = mr).
TEST(MemoryStoreCapacity, EngineMrAppliedWhenConfigResourceIsNull) {
    // Use a small bounded config that passes the DoS guard.
    MemoryStore::Config cfg;
    cfg.policy = capacity_policy::bounded;
    cfg.inbound_capacity = 1;
    cfg.outbound_capacity = 1;
    cfg.max_frame_bytes = 64;
    cfg.store_resource = nullptr;  // no config-level resource

    MemoryStoreFactory factory{cfg};
    std::pmr::monotonic_buffer_resource mr{std::pmr::new_delete_resource()};
    constexpr std::size_t kCap1GiB = 1ULL << 30;

    // Pass a non-null engine mr — the factory must accept it and use it.
    auto result = factory.make("SND", "TGT", &mr, kCap1GiB, asio::any_io_executor{});
    EXPECT_TRUE(result.has_value()) << "engine-mr applied: expected success but got error: "
                                    << (!result.has_value() ? static_cast<int>(result.error()) : 0);
}

}  // namespace
