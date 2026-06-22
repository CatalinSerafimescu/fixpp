# src/core/sync/CMakeLists.txt
# fixpp_sync — 006-async-mutex: fixpp::sync::async_mutex + async_lock_guard +
# detail::async_mutex_awaiter + detail::slot_allocator + detail::drain_latch_state
# + completion_policy.  [arch §3]/[arch §4.1]: fixpp::sync lives physically under
# core/.  Header-dominant (the compiler needs inline visibility of
# await_ready/await_suspend for HALO analysis per [2f §6.4]); the optional
# out-of-line src/core/sync/async_mutex.cpp for cancel_and_drain() is a deferred
# /implement compile-time tradeoff (plan.md Structure decision).  Starts as an
# INTERFACE target so T005 "builds empty before headers land" holds (mirrors the
# fixpp_session INTERFACE precedent); promote to STATIC iff a .cpp is added.
#
# 046-atomic-shared-ptr (NFR-017): atomic_shared_ptr.cpp adds the type-erased
# fallback shard lock, so fixpp_sync is now STATIC. The .cpp is ALWAYS compiled
# on every mode (always-ship-guard, research D-9 / contract CT-4); on the native
# path the alias never constructs a shard_guard so its symbols are
# present-but-never-called (zero overhead, SC-005). The async_mutex headers stay
# header-only for HALO; PUBLIC include dirs + deps preserve the prior INTERFACE
# contract for header-only consumers.

add_library(fixpp_sync STATIC
  atomic_shared_ptr.cpp
)
add_library(fixpp::sync ALIAS fixpp_sync)

target_include_directories(fixpp_sync PUBLIC
  "${CMAKE_SOURCE_DIR}/include"
)

# Allowed dependencies: core only [arch §2.3] (RC#2 — core::async_mutex never
# reaches into session/ or an engine handle).  asio supplies awaitable /
# cancellation slots / experimental::concurrent_channel / bind_allocator.
target_link_libraries(fixpp_sync PUBLIC
  fixpp_core
  asio::asio
)
