// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/tls/test_tls_handshake_alloc_guard.cpp
// T018 — [2g §9 seam #7] Zero-alloc on the handshake-hot path.
//
// Dual gate per [[feedback_tracking_pmr_resource_false_pass]]:
//   (a) counting_resource — counts PMR allocations routed through it.
//   (b) mallocnesia LD_PRELOAD — intercepts global new/delete/malloc/free.
//
// Both gates MUST fire. The counting_resource alone misses non-PMR escapes;
// mallocnesia alone misses PMR-routed allocs. Together they cover FR-007.
//
// Hot path under test (after warm-up):
//   - Pinset::find(sha256) — acquire-load + linear scan.
//   - Pinset::snapshot()   — acquire-load only.
//
// Run manually with mallocnesia:
//   python3 tools/check_alloc.py \
//       --binary build/linux-clang-debug/bin/test_tls_handshake_alloc_guard
//
// mallocnesia replaces the weak alloc_guard_{start,end} symbols with its
// interceptor scope markers. Under normal CTest the weak no-ops fire —
// alloc counting via counting_resource still catches PMR escapes.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <fixpp/tls/certificate.hpp>
#include <fixpp/tls/pinset.hpp>
#include <memory_resource>

// mallocnesia weak symbols — replaced by LD_PRELOAD.
extern "C" {
__attribute__((weak)) void alloc_guard_start() {}
__attribute__((weak)) void alloc_guard_end() {}
}

namespace {

using fixpp::tls::Certificate;
using fixpp::tls::pin_fingerprint;
using fixpp::tls::Pinset;

Certificate make_cert(pin_fingerprint const& fp, std::string_view dn = "CN=alloc") {
    Certificate c{};
    c.sha256_ = fp;
    c.subject_dn_ = dn;
    c.x509_version_ = 3;
    return c;
}

// Minimal counting_resource: tracks bytes allocated via PMR channel.
class counting_resource : public std::pmr::memory_resource {
public:
    std::size_t bytes_allocated = 0;

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        bytes_allocated += bytes;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }
    void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override {
        std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }
};

constexpr pin_fingerprint kFp = [] {
    pin_fingerprint f{};
    f[0] = std::byte{0xCC};
    return f;
}();

constexpr pin_fingerprint kMiss = [] {
    pin_fingerprint f{};
    f[0] = std::byte{0xFF};
    return f;
}();

// ── PMRGate: counting_resource ────────────────────────────────────────────────
TEST(TlsHandshakeAllocGuard, FindAndSnapshotZeroPMRAllocAfterWarmup) {
    counting_resource counter;

    Pinset::Config cfg;
    cfg.mr = &counter;

    Pinset ps(cfg);
    // Warm-up: add a pin — this allocates into the PMR (expected).
    ASSERT_TRUE(ps.add(make_cert(kFp, "CN=warmup")).has_value());

    // Reset counter AFTER warm-up.
    std::size_t allocs_before = counter.bytes_allocated;

    // ── Hot path — must allocate zero PMR bytes. ─────────────────────────────
    constexpr int kRounds = 1000;
    for (int i = 0; i < kRounds; ++i) {
        auto v = ps.find(kFp);
        (void)v.found();
        // Also exercise a miss.
        auto v2 = ps.find(kMiss);
        (void)v2.found();
        // And snapshot.
        auto snap = ps.snapshot();
        (void)snap->size();
    }

    std::size_t hot_allocs = counter.bytes_allocated - allocs_before;
    EXPECT_EQ(hot_allocs, 0u) << "find/snapshot must not allocate via PMR after warm-up (FR-007)";
}

// ── GlobalMallocGate: mallocnesia ─────────────────────────────────────────────
// When run under tools/check_alloc.py (LD_PRELOAD mallocnesia), alloc_guard_start
// / alloc_guard_end activate the interceptor. If global malloc fires inside the
// guarded region the binary exits non-zero → check_alloc.py reports FAIL.
//
// When run without LD_PRELOAD the weak symbols are no-ops and the test still
// passes (it merely doesn't intercept global malloc). The CMakeLists registers a
// separate CTest target that invokes check_alloc.py so CI exercises the gate.
TEST(TlsHandshakeAllocGuard, FindAndSnapshotZeroGlobalMallocAfterWarmup) {
    Pinset ps;
    ASSERT_TRUE(ps.add(make_cert(kFp, "CN=global-warmup")).has_value());

    // Warm-up complete. Begin mallocnesia interception.
    alloc_guard_start();

    constexpr int kRounds = 500;
    for (int i = 0; i < kRounds; ++i) {
        auto v = ps.find(kFp);
        (void)v.found();
        auto snap = ps.snapshot();
        (void)snap->size();
    }

    alloc_guard_end();

    // If we reached here under mallocnesia without a crash/abort, zero global
    // malloc occurred inside the guard. Assert structural correctness too.
    EXPECT_TRUE(ps.find(kFp).found()) << "post-guard find must still work";
}

}  // namespace
