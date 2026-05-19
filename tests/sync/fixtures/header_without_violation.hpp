// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/sync/fixtures/header_without_violation.hpp
//
// T064 — zero-FP fixture for seam #14 (grep-gate corpus).
//
// This header is legitimate: it pulls asio::awaitable (transitively, via the
// real async_mutex.hpp impl which includes <asio/awaitable.hpp>).  Asio
// internally uses std::mutex, so std::mutex IS present post-preprocessing —
// but it lives in an asio-owned / system region, not in project-owned code.
// The gate MUST NOT fire (zero false-positive guarantee per T064).
//
// The fixture's own code uses only fixpp::sync::async_mutex.
#pragma once
#include <fixpp/core/sync/async_mutex.hpp>
namespace fixpp::sync::test::fixture {
inline fixpp::sync::async_mutex g_ok;
}  // namespace fixpp::sync::test::fixture
