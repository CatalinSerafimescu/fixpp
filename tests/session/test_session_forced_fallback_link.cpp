// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_session_forced_fallback_link.cpp
// T019 (046-atomic-shared-ptr, NFR-017) — forced-fallback link test for fixpp_session.
//
// Design anchor: .specify/046-atomic-shared-ptr.md (plan row +B)
//
// This test verifies that the shard_guard out-of-line symbols from
// src/core/sync/atomic_shared_ptr.cpp resolve transitively through the
// fixpp_session consumer target (fixpp_session links fixpp_transport which
// links fixpp_sync — the chain that carries atomic_shared_ptr.cpp).
//
// The test constructs fixpp::sync::atomic_shared_ptr<int> directly (in a
// session test binary that links fixpp_session) and calls store/load/exchange,
// proving the linker resolves shard_guard through the consumer target's
// transitive link chain.
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

TEST(SessionForcedFallbackLink, AtomicSharedPtrSymbolsResolveViaSession) {
    // Construct an atomic_shared_ptr<int> in a binary that links fixpp_session.
    // The linker must find the shard_guard symbols through fixpp_session's
    // transitive dependency chain: fixpp_session → fixpp_transport → fixpp_sync,
    // where fixpp_sync contains src/core/sync/atomic_shared_ptr.cpp.
    fixpp::sync::atomic_shared_ptr<int> ptr;

    // Write: release-store (on the fallback path, acquires a shard mutex).
    auto v1 = std::make_shared<int>(46);  // 046 feature marker
    ptr.store(v1, std::memory_order_release);

    // Read: acquire-load.
    auto loaded = ptr.load(std::memory_order_acquire);
    ASSERT_NE(loaded, nullptr) << "acquire-load must return the stored pointer";
    EXPECT_EQ(*loaded, 46) << "loaded value must match the stored value";

    // Exchange: exercises both acquire-load and release-store paths of shard_guard.
    auto v2 = std::make_shared<int>(0);
    auto old = ptr.exchange(v2, std::memory_order_acq_rel);
    ASSERT_NE(old, nullptr);
    EXPECT_EQ(*old, 46) << "exchange must return the previously stored value";

    auto final_load = ptr.load(std::memory_order_acquire);
    ASSERT_NE(final_load, nullptr);
    EXPECT_EQ(*final_load, 0);
}

}  // namespace
