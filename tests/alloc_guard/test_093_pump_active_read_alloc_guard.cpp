// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/alloc_guard/test_093_pump_active_read_alloc_guard.cpp
//
// 093-inbound-frame-dispositions T044 (quickstart Q-19; FR-052; contract C-4; plan OD-22):
// after the session first reaches Active the read pump reads without the establishment
// deadline's race, so an Active read makes no NEW global operator new call: every Active
// read the cell drives makes the same number of calls, and that number is at most
// kBaseActiveReadAllocs, the count the merge-base's pump makes with this cell's rig.
//
// The real pump is driven past Active over loopback TCP (tests/session/plain_engine_rig.hpp),
// a warm-up Heartbeat is processed, then each later Heartbeat is written by the peer
// synchronously, before the counter is armed, and the io_context is run one handler at a
// time until the session's NextNumIn shows the frame was processed. So the armed window
// holds the pump's read completion, its feed, the frame's delivery and the next read's
// initiation, and nothing the peer does.
//
// A regression witness, not a disarm witness: asio serves handler and coroutine-frame
// storage from a per-thread recycling cache when a cached block fits, without calling
// operator new, so a race left armed after Active need not show here. A frame larger than
// the cache holds is allocated on every use instead (L-497-1). Quickstart Q-36 witnesses
// the disarm by behaviour.
//
// This cell counts global operator new only. asio allocates those frames through
// std::aligned_alloc, which the counter cannot see.
//
// Standalone ([const §VII.8]): it replaces global operator new for the whole binary.
// No *_mallocnesia twin: the window wraps io_context::run_one_for(), which the
// "DELIBERATELY NOT GATED" note in tests/alloc_guard/CMakeLists.txt keeps out of the
// LD_PRELOAD gate.

#include <gtest/gtest.h>

#include <asio/buffer.hpp>
#include <asio/write.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <new>
#include <string>
#include <system_error>

#include "session/plain_engine_rig.hpp"
#include "support/session_test_access.hpp"

// ── Sanitizer-detection guard (tests/alloc_guard/test_validate_gate_alloc_guard.cpp) ──
// ASan, TSan and MSan own operator new/delete; a TU-local replacement conflicts with
// theirs, so under them the counter is compiled out and the count arms skip.
#ifdef __has_feature
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || \
    __has_feature(memory_sanitizer)
#define FIXPP_SANITIZER_REPLACES_NEW 1
#endif
#endif
#if !defined(FIXPP_SANITIZER_REPLACES_NEW) && \
    (defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__))
#define FIXPP_SANITIZER_REPLACES_NEW 1
#endif
#ifndef FIXPP_SANITIZER_REPLACES_NEW
#define FIXPP_SANITIZER_REPLACES_NEW 0
#endif

#if !FIXPP_SANITIZER_REPLACES_NEW

// Counts global operator new calls while armed. Constant-initialised, so safe to read
// from operator new during other TUs' dynamic initialisation.
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) — the counter's state
static std::atomic<std::size_t> g_new_count{0};
static std::atomic<bool> g_arming{false};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

// The replacement obtains and releases raw storage with malloc/free by design: it is the
// interceptor the counter needs, and it must not re-enter operator new.
// NOLINTBEGIN(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,hicpp-no-malloc)
// NOLINTNEXTLINE(cert-dcl58-cpp) — replacing global operator new/delete is intentional
void* operator new(std::size_t n) {
    if (g_arming.load(std::memory_order_relaxed)) {
        g_new_count.fetch_add(1, std::memory_order_relaxed);
    }
    void* p = std::malloc(n == 0 ? 1 : n);
    if (!p) throw std::bad_alloc{};
    return p;
}

// NOLINTNEXTLINE(cert-dcl58-cpp)
void* operator new[](std::size_t n) {
    if (g_arming.load(std::memory_order_relaxed)) {
        g_new_count.fetch_add(1, std::memory_order_relaxed);
    }
    void* p = std::malloc(n == 0 ? 1 : n);
    if (!p) throw std::bad_alloc{};
    return p;
}

// NOLINTNEXTLINE(cert-dcl58-cpp)
void operator delete(void* p) noexcept { std::free(p); }
// NOLINTNEXTLINE(cert-dcl58-cpp)
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
// NOLINTNEXTLINE(cert-dcl58-cpp)
void operator delete[](void* p) noexcept { std::free(p); }
// NOLINTNEXTLINE(cert-dcl58-cpp)
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
// NOLINTEND(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,hicpp-no-malloc)

namespace {
void arm() noexcept {
    g_new_count.store(0, std::memory_order_relaxed);
    g_arming.store(true, std::memory_order_relaxed);
}
std::size_t disarm() noexcept {
    g_arming.store(false, std::memory_order_relaxed);
    return g_new_count.load(std::memory_order_relaxed);
}
}  // namespace

#endif  // !FIXPP_SANITIZER_REPLACES_NEW

namespace {

namespace pr = fixpp::test_support::plain_rig;
using fixpp::session::fsm_state;

// The global operator new calls one Active read makes on the merge-base's pump, with this
// cell's rig (plan OD-22). Not zero: asio's type-erased executor allocates, at
// `any_executor_base::query_fn_non_void<..., prefer_only<blocking::possibly_t>>` when the
// session strand dispatches the socket's recv completion, and at that query and at
// `shared_target_executor::shared_target_executor<strand<any_io_executor>>` when the pump
// initiates its next read (`handler_work_base`'s tracked-work `prefer`). A change of asio
// that changes those frames changes the count. To re-derive:
// build this cell against the merge-base, run it, read the per-read counts it reports on
// failure (or set this to 0), and attribute each call with gdb, breaking in this file's
// operator new on `g_arming` and printing a backtrace.
constexpr std::size_t kBaseActiveReadAllocs = 5;

std::uint32_t next_inbound(fixpp::session::Session& s) {
    return fixpp::session::session_test_access::seqnum_mgr(s).next_inbound_unsafe();
}

// The counter counts: an allocation in an armed window is seen. Without this a zero
// below could mean only that the replacement is not linked.
TEST(PumpActiveReadAllocGuard, TheCounterCountsAKnownAllocation) {
#if FIXPP_SANITIZER_REPLACES_NEW
    GTEST_SKIP() << "operator-new replacement disabled under sanitizers";
#else
    arm();
    // A raw new/delete pair on purpose: the known operator new the counter must see.
    // NOLINTBEGIN(cppcoreguidelines-owning-memory)
    int* volatile p = new int(7);
    delete p;
    // NOLINTEND(cppcoreguidelines-owning-memory)
    std::size_t const n = disarm();
    EXPECT_GE(n, 1U) << "the armed counter did not see a known operator new";
#endif
}

TEST(PumpActiveReadAllocGuard, AnActiveReadAfterAWarmUpReadAllocatesNothing) {
    pr::Rig rig;
    bool const up = rig.start(rig.cfg()) && rig.to_active();
    auto const sess = rig.session();
    bool warmed = false;
    if (up && sess) {
        warmed = rig.deliver(rig.heartbeat(2)) &&
                 rig.run_until([&] { return next_inbound(*sess) == 3U; });
    }

    constexpr std::uint32_t kFirst = 3;
    constexpr std::uint32_t kLast = 6;
    std::size_t processed = 0;
    std::size_t counts[kLast - kFirst + 1] = {};
    std::error_code write_ec;
    if (warmed) {
        for (std::uint32_t seq = kFirst; seq <= kLast; ++seq) {
            std::string const hb = rig.heartbeat(seq);
            asio::write(rig.peer.sock, asio::buffer(hb), write_ec);
            if (write_ec) break;
            auto const limit = std::chrono::steady_clock::now() + std::chrono::seconds{10};
#if !FIXPP_SANITIZER_REPLACES_NEW
            arm();
#endif
            while (next_inbound(*sess) != seq + 1U && std::chrono::steady_clock::now() < limit) {
                rig.ioc.run_one_for(std::chrono::milliseconds{1});
            }
#if !FIXPP_SANITIZER_REPLACES_NEW
            counts[seq - kFirst] = disarm();
#endif
            if (next_inbound(*sess) != seq + 1U) break;
            ++processed;
        }
    }
    fsm_state const state = sess ? sess->state() : fsm_state::NotConnected;
    rig.stop();

    ASSERT_TRUE(up && sess) << "the session did not reach Active";
    ASSERT_TRUE(warmed) << "the warm-up Heartbeat was not processed";
    ASSERT_FALSE(write_ec) << write_ec.message();
    ASSERT_EQ(processed, kLast - kFirst + 1) << "a measured Heartbeat was not processed, so "
                                                "its window did not reach the pump";
    EXPECT_EQ(state, fsm_state::Active);
#if FIXPP_SANITIZER_REPLACES_NEW
    GTEST_SKIP() << "operator-new replacement disabled under sanitizers; the reads above "
                    "were processed";
#else
    for (std::size_t i = 0; i < processed; ++i) {
        EXPECT_EQ(counts[i], counts[0]) << "the Active read of 34=" << (kFirst + i)
                                        << " made a different number of operator new calls "
                                           "from the read of 34="
                                        << kFirst << ": a per-read growth";
        EXPECT_LE(counts[i], kBaseActiveReadAllocs)
            << "global operator new calls during the Active read of 34=" << (kFirst + i)
            << ": more than the merge-base's pump makes";
    }
#endif
}

}  // namespace
