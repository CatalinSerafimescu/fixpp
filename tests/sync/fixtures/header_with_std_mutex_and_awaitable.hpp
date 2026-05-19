// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/sync/fixtures/header_with_std_mutex_and_awaitable.hpp
//
// T063 — deliberate-violation fixture for seam #14 (grep-gate corpus).
//
// Each of the six FR-014 banned spellings lives in its own preprocessor-guarded
// block so the seam driver can assert per-spelling by passing a single -D macro.
// Exactly one spelling is active per invocation.
//
// The gate fires because:
//   (a) <asio/awaitable.hpp> is directly included → awaitable line-marker present
//   (b) the active banned spelling appears in a non-system, project-owned region
#pragma once
#include <asio/awaitable.hpp>   // brings asio::awaitable into scope
#include <mutex>
#include <shared_mutex>
namespace fixpp::sync::test::fixture {
#if   defined(FX_MUTEX)
inline std::mutex                 g_banned;
#elif defined(FX_RECURSIVE_MUTEX)
inline std::recursive_mutex       g_banned;
#elif defined(FX_TIMED_MUTEX)
inline std::timed_mutex           g_banned;
#elif defined(FX_RECURSIVE_TIMED_MUTEX)
inline std::recursive_timed_mutex g_banned;
#elif defined(FX_SHARED_MUTEX)
inline std::shared_mutex          g_banned;
#elif defined(FX_SHARED_TIMED_MUTEX)
inline std::shared_timed_mutex    g_banned;
#endif
}  // namespace fixpp::sync::test::fixture
