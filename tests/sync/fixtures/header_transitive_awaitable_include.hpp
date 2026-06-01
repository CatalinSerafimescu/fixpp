// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/sync/fixtures/header_transitive_awaitable_include.hpp
//
// T065 — transitive-awaitable violation fixture for seam #14 (grep-gate corpus).
//
// This fixture contains a fixture-OWNED std::mutex (non-system region) while
// pulling asio::awaitable TRANSITIVELY (not a direct #include <asio/awaitable.hpp>
// — pulled via the project impl header fixpp/core/sync/async_mutex.hpp).
//
// The gate MUST fire because:
//   (a) async_mutex.hpp transitively pulls asio::awaitable → line-marker present
//   (b) std::mutex appears in this fixture file itself — a non-system, project-
//       owned region — so the token IS attributed to project code by the awk filter
#pragma once
#include <fixpp/core/sync/async_mutex.hpp>  // transitively pulls asio::awaitable
#include <mutex>
namespace fixpp::sync::test::fixture {
inline std::mutex g_transitive_banned;
}  // namespace fixpp::sync::test::fixture
