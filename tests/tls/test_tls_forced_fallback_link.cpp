// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/tls/test_tls_forced_fallback_link.cpp
// T019 (046-atomic-shared-ptr, NFR-017) — forced-fallback link test for fixpp::tls.
//
// Design anchor: .specify/046-atomic-shared-ptr.md (plan row +B)
//
// This test verifies that the shard_guard out-of-line symbols from
// src/core/sync/atomic_shared_ptr.cpp resolve transitively through the
// fixpp::tls consumer target (fixpp::tls links fixpp::core which contains the .cpp).
//
// The test constructs fixpp::sync::atomic_shared_ptr<int> directly (in the TLS
// test binary which links fixpp::tls) and calls store/load, proving the linker
// can find the shard_guard symbols through the consumer target's transitive link.
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

TEST(TlsForcedFallbackLink, AtomicSharedPtrSymbolsResolveViaTls) {
    // Construct an atomic_shared_ptr<int> in a binary that links fixpp::tls.
    // The linker must find the shard_guard symbols through fixpp::tls's
    // transitive dependency on fixpp::core (which contains atomic_shared_ptr.cpp).
    fixpp::sync::atomic_shared_ptr<int> ptr;

    // Write: release-store (on the fallback path, acquires a shard mutex).
    auto v1 = std::make_shared<int>(42);
    ptr.store(v1, std::memory_order_release);

    // Read: acquire-load.
    auto loaded = ptr.load(std::memory_order_acquire);
    ASSERT_NE(loaded, nullptr) << "acquire-load must return the stored pointer";
    EXPECT_EQ(*loaded, 42) << "loaded value must match the stored value";

    // Second store with a new instance, then verify.
    // Note: 46 (decimal) is the feature number; use decimal to avoid the octal pitfall.
    auto v2 = std::make_shared<int>(46);  // feature 046 marker
    ptr.store(v2, std::memory_order_release);
    auto loaded2 = ptr.load(std::memory_order_acquire);
    ASSERT_NE(loaded2, nullptr);
    EXPECT_EQ(*loaded2, 46);
}

}  // namespace
