// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/transport/test_transport_forced_fallback_link.cpp
// T019 (046-atomic-shared-ptr, NFR-017) — forced-fallback link test for fixpp_transport.
//
// Design anchor: .specify/046-atomic-shared-ptr.md (plan row +B)
//
// This test verifies that the shard_guard out-of-line symbols from
// src/core/sync/atomic_shared_ptr.cpp resolve transitively through the
// fixpp_transport consumer target (fixpp_transport links fixpp_sync which
// contains the .cpp).
//
// The test constructs fixpp::sync::atomic_shared_ptr<int> directly (in the
// transport test binary which links fixpp_transport) and calls store/load,
// proving the linker can find the shard_guard symbols through the consumer.
//
// On -DFIXPP_FORCE_ATOMIC_SHARED_PTR_FALLBACK=ON (linux-clang-ff preset):
//   FIXPP_ATOMIC_SHARED_PTR_NATIVE_ACTIVE == 0 → shard_guard symbols are USED.
//
// On native presets (linux-clang-debug):
//   FIXPP_ATOMIC_SHARED_PTR_NATIVE_ACTIVE == 1 → shard_guard present-but-unused;
//   the link still succeeds (link test) and the store/load are inlined.

#include <gtest/gtest.h>

#include <fixpp/core/sync/detail/atomic_shared_ptr.hpp>

namespace {

TEST(TransportForcedFallbackLink, AtomicSharedPtrSymbolsResolveViaTransport) {
    // Construct an atomic_shared_ptr<int> in a binary that links fixpp_transport.
    // The linker must find the shard_guard symbols through fixpp_transport's
    // transitive dependency on fixpp_sync (which contains atomic_shared_ptr.cpp).
    fixpp::sync::atomic_shared_ptr<int> ptr;

    // Write: release-store (on the fallback path, acquires a shard mutex).
    auto v1 = std::make_shared<int>(100);
    ptr.store(v1, std::memory_order_release);

    // Read: acquire-load.
    auto loaded = ptr.load(std::memory_order_acquire);
    ASSERT_NE(loaded, nullptr) << "acquire-load must return the stored pointer";
    EXPECT_EQ(*loaded, 100) << "loaded value must match the stored value";

    // Exchange: compare-exchange (exercises shard_guard on both read and write).
    auto v2 = std::make_shared<int>(200);
    auto old = ptr.exchange(v2, std::memory_order_acq_rel);
    ASSERT_NE(old, nullptr);
    EXPECT_EQ(*old, 100) << "exchange must return the previously stored value";

    auto final_load = ptr.load(std::memory_order_acquire);
    ASSERT_NE(final_load, nullptr);
    EXPECT_EQ(*final_load, 200);
}

}  // namespace
