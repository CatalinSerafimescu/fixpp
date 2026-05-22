// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/alloc_guard/test_session_alloc_guard.cpp
//
// 005-session-establishment-fsm T059 / Phase 8 — Seam #12: zero global
// new/delete/malloc on the 005-specific hot paths (SC-009 / I-7 / FR-015).
//
// Run under mallocnesia via tools/check_alloc.py:
//   python3 tools/check_alloc.py \
//       --binary build/linux-clang-debug/bin/test_session_alloc_guard
//
// Coverage scope — the THREE 005-owned hot paths from data-model.md / plan.md
// "Constraints" §1 (inbound-dispatch / timer-fire / seqnum) decompose as:
//
//  (A) SEQNUM PATH                 — SeqnumManager::assign_outbound() +
//                                    ::check_inbound() under async_mutex.
//                                    Asserted HERE (Test #1).
//  (B) ADMIN BUILD PATH            — build_heartbeat() / build_test_request()
//                                    into pre-allocated stack buffers.
//                                    Asserted HERE (Test #2).
//  (C) SESSION DISPATCH (cancellable_dispatch) — covered upstream by 007's
//                                    test_dispatch_alloc_guard.cpp (Seam 7 /
//                                    T051 — same mallocnesia harness). Not
//                                    re-tested here to avoid drift.
//  (D) CLOCK SLEEP / TIMER FIRE    — covered upstream by 007's
//                                    test_clock_sleep_alloc_guard.cpp (Seam 18
//                                    / T052). Not re-tested here.
//
// Together (A)+(B)+(C)+(D) cover the SC-009 / I-7 surface.

#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <cstddef>
#include <fixpp/session/admin_messages.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/seqnum_manager.hpp>
#include <future>

// mallocnesia replaces these weak no-ops with its interceptor scope markers.
extern "C" {
__attribute__((weak)) void alloc_guard_start() {}
__attribute__((weak)) void alloc_guard_end() {}
}

namespace {

using fixpp::session::seqnum_min;
using fixpp::session::seqnum_t;
using fixpp::session::SeqnumManager;

// ─────────────────────────────────────────────────────────────────────────────
// (A) SeqnumPathNoGlobalHeapAlloc — SeqnumManager hot path
//
// Drives 10⁴ rounds of (assign_outbound → check_inbound). All counter access
// goes through fixpp::sync::async_mutex (D-7 defence-in-depth, structurally
// zero-contention on the per-session strand). The mutex uses a small embedded
// waiter slot pool (`[2f §4.5]`); under uncontended fast-path it allocates
// nothing on the heap.
// ─────────────────────────────────────────────────────────────────────────────

// Run a single awaitable synchronously on an io_context (mirrors the helper
// in tests/session/seqnum_manager_test.cpp).
template <class Awaitable>
auto run_sync(asio::io_context& ioc, Awaitable&& aw) {
    auto fut = asio::co_spawn(ioc, std::forward<Awaitable>(aw), asio::use_future);
    ioc.run();
    ioc.restart();
    return fut.get();
}

TEST(SessionAllocGuard, SeqnumPathNoGlobalHeapAlloc) {
    asio::io_context ioc;
    SeqnumManager mgr;

    // Warm-up: prime asio's per-thread caches + the async_mutex slot pool
    // (mirrors test_dispatch_alloc_guard's WARMUP pattern + the
    // sync_alloc_guard_test E-4 prime). Use the same per-iteration run_sync
    // pattern as the existing seqnum_manager_test LongRunZeroDrift, which is
    // known-working — a single-spawn 10⁴-iteration coroutine over the
    // async_mutex's awaitable surface crashes on this build (likely the
    // mutex's awaiter recycling assumes per-call ioc cycles).
    constexpr int kWarmup = 8;
    for (int i = 0; i < kWarmup; ++i) {
        auto a = run_sync(ioc, mgr.assign_outbound());
        ASSERT_TRUE(a.has_value()) << "warmup assign_outbound i=" << i;
        auto c = run_sync(ioc, mgr.check_inbound(static_cast<seqnum_t>(seqnum_min + i)));
        ASSERT_TRUE(c.has_value()) << "warmup check_inbound i=" << i;
    }

    // ── Measured corpus ──────────────────────────────────────────────────────
    // After warm-up: next_outbound == seqnum_min + kWarmup, next_inbound == same.
    constexpr int kCorpus = 10'000;
    int assigned_count = 0;
    int checked_count = 0;

    alloc_guard_start();
    for (int i = 0; i < kCorpus; ++i) {
        auto a = run_sync(ioc, mgr.assign_outbound());
        if (a.has_value()) ++assigned_count;
        auto c = run_sync(ioc, mgr.check_inbound(static_cast<seqnum_t>(seqnum_min + kWarmup + i)));
        if (c.has_value()) ++checked_count;
    }
    alloc_guard_end();

    EXPECT_EQ(assigned_count, kCorpus);
    EXPECT_EQ(checked_count, kCorpus);

    // Drain async_mutex before dtor (terminate-precondition).
    run_sync(ioc, mgr.drain());
}

// ─────────────────────────────────────────────────────────────────────────────
// (B) AdminBuildNoGlobalHeapAlloc — pure-function admin builders.
//
// build_heartbeat / build_test_request write into a caller-owned std::span;
// they MUST NOT allocate. The output buffer lives on the stack BEFORE the
// guard markers fire. 10⁴ rounds of alternating builders covers the FR-006 /
// liveness emit path used by the heartbeat-timer-fire window.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SessionAllocGuard, AdminBuildNoGlobalHeapAlloc) {
    // 512 B buffer per FIX admin message is comfortably larger than the
    // typical 60-90 B Heartbeat/TestRequest frame on FIX.4.4 with short
    // CompIDs (the seam #2 logon_handshake_test uses similar sizing).
    std::array<std::byte, 512> hb_buf{};
    std::array<std::byte, 512> tr_buf{};

    constexpr std::string_view kSender = "SENDER";
    constexpr std::string_view kTarget = "TARGET";
    constexpr std::string_view kTestId = "TR-001";

    // Warm-up — the FIRST call to build_heartbeat / build_test_request can
    // touch lazy-init data in the wire::Writer / dictionary lookup tables;
    // we don't want those one-shots counted.
    {
        auto r = fixpp::session::build_heartbeat(hb_buf, seqnum_t{1}, kSender, kTarget, {},
                                                 "FIX.4.2", "20240101-00:00:00.000");
        ASSERT_TRUE(r.has_value()) << "warmup build_heartbeat failed";
    }
    {
        auto r = fixpp::session::build_test_request(tr_buf, seqnum_t{1}, kSender, kTarget, kTestId,
                                                    "FIX.4.2", "20240101-00:00:00.000");
        ASSERT_TRUE(r.has_value()) << "warmup build_test_request failed";
    }

    constexpr int kCorpus = 10'000;
    int hb_ok = 0;
    int tr_ok = 0;

    alloc_guard_start();
    for (int i = 0; i < kCorpus; ++i) {
        auto hb = fixpp::session::build_heartbeat(hb_buf, static_cast<seqnum_t>(seqnum_min + i),
                                                  kSender, kTarget, {},
                                                  "FIX.4.2", "20240101-00:00:00.000");
        if (hb.has_value()) ++hb_ok;
        auto tr = fixpp::session::build_test_request(tr_buf, static_cast<seqnum_t>(seqnum_min + i),
                                                     kSender, kTarget, kTestId,
                                                     "FIX.4.2", "20240101-00:00:00.000");
        if (tr.has_value()) ++tr_ok;
    }
    alloc_guard_end();

    EXPECT_EQ(hb_ok, kCorpus);
    EXPECT_EQ(tr_ok, kCorpus);
}

}  // namespace
