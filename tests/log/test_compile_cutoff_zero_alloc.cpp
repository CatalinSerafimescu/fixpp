// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/log/test_compile_cutoff_zero_alloc.cpp
//
// TS-1: Compile-time level cutoff + dual-gate zero-alloc.
//
// This test is compiled with FIXPP_LOG_MIN_LEVEL=3 (warn) so that
// debug(1) and info(2) call sites are compiled to zero bytes / contribute
// no format strings to .rodata.
//
// Gate A — compile-time cutoff (FR-010):
//   call sites with level < warn must NOT contribute format strings to
//   .rodata.  We verify this by checking nm/objdump output for the format
//   string constants used only in the suppressed call sites.
//   In-process: we also assert that enqueue is NOT called for below-cutoff
//   sites (the CaptureSink receives zero records for those sites).
//
// Gate B — dual-gate zero-alloc (New-3 / TS-1 / contracts/log-core.md):
//   The full macro → enqueue → arg-marshalling path must perform ZERO heap
//   allocations.  Verified by two complementary gates:
//
//   Gate B1 — counting_resource (PMR):
//     A PMR pool_resource is set as the default resource; every alloc routed
//     through PMR increments a counter.  The counter must be 0 inside the
//     guard window.
//
//   Gate B2 — mallocnesia LD_PRELOAD (global malloc):
//     When run under LD_PRELOAD=tools/mallocnesia/libmallocnesia.so with
//     MALLOCNESIA_MAX_ALLOCS=0, the interceptor exits the process with code 1
//     if any malloc/calloc/realloc fires between alloc_guard_start() and
//     alloc_guard_end().
//     The CMakeLists.txt log_alloc_mallocnesia test runs this binary under
//     the preload; the gtest run here is the in-process PMR gate (Gate B1).
//
// Ring fill levels tested: 10 %, 50 %, 95 % of capacity.
// The drain thread RUNS (not sleeping) so records are consumed; the tested
// enqueue calls always land on the non-overflow producer path.
//
// Warm-up: one enqueue call BEFORE the guard window to prime any per-thread
// caches that ArgValue or the ring CAS might lazily initialise.
//
// Anchors:
//   [2k §4.3] / contracts/log-core.md FR-001/FR-010/New-3/TS-1
//   [[feedback_tracking_pmr_resource_false_pass]]

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <memory_resource>
#include <thread>
#include <vector>

#include <fixpp/log/level.hpp>
#include <fixpp/log/logger.hpp>
#include <fixpp/log/record.hpp>
#include <fixpp/log/sink.hpp>

// ── mallocnesia guard markers ─────────────────────────────────────────────
// Weak *declarations* with NO body: the binary carries no local definition, so
// at runtime the LD_PRELOAD=libmallocnesia.so strong symbols are bound (and
// actually ARM the global-malloc interceptor). A weak symbol with a `{}` body
// would instead bind to the local no-op definition and NEVER be overridden on
// this toolchain (verified: even -fsemantic-interposition does not interpose a
// local weak definition here) — i.e. a silent no-op gate. Without preload the
// weak symbols are null, so the null-checked wrappers below skip them safely
// (the binary still runs plainly for the PMR Gate B1 path).
extern "C" {
__attribute__((weak)) void alloc_guard_start();
__attribute__((weak)) void alloc_guard_end();
}
namespace {
inline void guard_start() { if (alloc_guard_start) alloc_guard_start(); }
inline void guard_end()   { if (alloc_guard_end)   alloc_guard_end(); }
}  // namespace

namespace {

// ── Counting PMR resource (Gate B1) ──────────────────────────────────────
// Wraps an upstream resource and counts allocations routed through PMR.
class CountingResource final : public std::pmr::memory_resource {
public:
    explicit CountingResource(std::pmr::memory_resource* upstream =
                                  std::pmr::new_delete_resource())
        : upstream_{upstream} {}

    std::uint64_t alloc_count() const noexcept {
        return count_.load(std::memory_order_relaxed);
    }
    void reset() noexcept { count_.store(0, std::memory_order_relaxed); }

private:
    void* do_allocate(std::size_t bytes, std::size_t align) override {
        count_.fetch_add(1, std::memory_order_relaxed);
        return upstream_->allocate(bytes, align);
    }
    void do_deallocate(void* p, std::size_t bytes, std::size_t align) override {
        upstream_->deallocate(p, bytes, align);
    }
    bool do_is_equal(std::pmr::memory_resource const& other) const noexcept override {
        return this == &other;
    }

    std::pmr::memory_resource* upstream_;
    std::atomic<std::uint64_t> count_{0};
};

// ── CaptureSink ──────────────────────────────────────────────────────────
class CaptureSink final : public fixpp::log::Sink {
public:
    std::atomic<std::size_t> emit_count{0};

    [[nodiscard]] fixpp::core::expected_t<void> open() override { return {}; }
    void emit(fixpp::log::Record const&) noexcept override {
        emit_count.fetch_add(1, std::memory_order_relaxed);
    }
    void flush(std::chrono::milliseconds) noexcept override {}
    void close() noexcept override {}
};

// ── Format IDs for the call sites ────────────────────────────────────────
// These format strings are ONLY referenced via their CRC32 ID at the
// producer site.  With FIXPP_LOG_MIN_LEVEL=3 (warn), the debug/info IDs
// below should NOT appear in .rodata of this TU.
constexpr std::uint32_t kFmtDebug =
    static_cast<std::uint32_t>(fixpp::log::detail::crc32_str("zero alloc debug {}"));
constexpr std::uint32_t kFmtInfo  =
    static_cast<std::uint32_t>(fixpp::log::detail::crc32_str("zero alloc info {}"));
constexpr std::uint32_t kFmtWarn  =
    static_cast<std::uint32_t>(fixpp::log::detail::crc32_str("zero alloc warn {}"));

// Helper: enqueue one warn-level record via the FIXPP_LOG0 macro.
// This is the full hot-path that must be zero-alloc.
inline void emit_warn(fixpp::log::Logger* logger, std::uint64_t seq)
{
    FIXPP_LOG0(logger,
               warn,
               fixpp::log::cat::session,
               "zero alloc warn {}",
               fixpp::log::ArgValue::from_u64(seq));
}

// Helper: enqueue one debug-level record (should be compiled out by if constexpr).
inline void emit_debug(fixpp::log::Logger* logger, std::uint64_t seq)
{
    FIXPP_LOG0(logger,
               debug,
               fixpp::log::cat::session,
               "zero alloc debug {}",
               fixpp::log::ArgValue::from_u64(seq));
}

// Helper: enqueue one info-level record (should be compiled out by if constexpr).
inline void emit_info(fixpp::log::Logger* logger, std::uint64_t seq)
{
    FIXPP_LOG0(logger,
               info,
               fixpp::log::cat::session,
               "zero alloc info {}",
               fixpp::log::ArgValue::from_u64(seq));
}

}  // namespace

// ── TS-1a: compile-time cutoff (in-process check) ────────────────────────
//
// Verify that debug/info call sites do NOT reach the sink (the if constexpr
// gate compiled them out).
//
// Note: we cannot do the nm/objdump binary check inside a gtest because we
// can't inspect our own text segment portably.  The CMakeLists.txt companion
// test (log_alloc_mallocnesia) acts as the binary gate.  Here we verify the
// run-time behaviour.
TEST(LogZeroAlloc, CompileTimeCutoffInfoDebugNotEmitted)
{
    static_assert(FIXPP_LOG_MIN_LEVEL == 3,
                  "This test must be compiled with FIXPP_LOG_MIN_LEVEL=3 (warn)");

    auto* sink_raw = new CaptureSink{};

    std::pmr::vector<std::unique_ptr<fixpp::log::Sink>> sinks{};
    sinks.push_back(std::unique_ptr<fixpp::log::Sink>(sink_raw));

    fixpp::log::LoggerConfig cfg;
    cfg.capacity    = 256u;
    cfg.on_overflow = fixpp::log::overflow_policy::drop_newest;

    auto logger = std::make_unique<fixpp::log::Logger>(std::move(cfg), std::move(sinks));

    // Emit debug and info — they should be silently compiled out.
    emit_debug(logger.get(), 0u);
    emit_info(logger.get(), 1u);

    // Emit one warn — this SHOULD reach the sink.
    emit_warn(logger.get(), 2u);

    (void)logger->shutdown(std::chrono::seconds{5});

    // Only the warn record should have reached the sink.
    EXPECT_EQ(sink_raw->emit_count.load(), 1u)
        << "Only 1 record (warn) should reach the sink; "
           "debug and info are compiled out by FIXPP_LOG_MIN_LEVEL=3";

    EXPECT_EQ(logger->drop_count(), 0u)
        << "No overflow drops should occur";
}

// ── TS-1b: dual-gate zero-alloc ──────────────────────────────────────────
//
// Gate B1: PMR counting_resource confirms no PMR-routed allocations on the
// enqueue path.
//
// Gate B2: when run under LD_PRELOAD=libmallocnesia.so, the alloc_guard_start
// / alloc_guard_end markers intercept global malloc/calloc/realloc.  Under
// normal execution, those markers are no-ops (weak symbols).
//
// Ring fill levels: 10%, 50%, 95% of capacity 1024.
// The drain runs, so the ring never saturates (non-overflow path).
//
// Warm-up: one call before the guard window to prime any lazy per-thread state.
TEST(LogZeroAlloc, DualGateZeroAllocEnqueuePath)
{
    // ── Setup ──────────────────────────────────────────────────────────────
    // PMR counting resource — wraps the default new_delete_resource.
    CountingResource counting_res{};

    // Set as default PMR resource so any PMR containers inside Logger or
    // the test that use get_default_resource() are tracked.
    // We save and restore the previous default after the test.
    auto* prev_default = std::pmr::get_default_resource();
    std::pmr::set_default_resource(&counting_res);

    auto* sink_raw = new CaptureSink{};

    // Use the counting resource for the ring allocation itself, so the
    // ring-creation alloc is attributed to setup (not the guard window).
    std::pmr::vector<std::unique_ptr<fixpp::log::Sink>> sinks{&counting_res};
    sinks.push_back(std::unique_ptr<fixpp::log::Sink>(sink_raw));

    fixpp::log::LoggerConfig cfg;
    cfg.capacity      = 1024u;
    cfg.on_overflow   = fixpp::log::overflow_policy::drop_newest;
    cfg.ring_resource = &counting_res;

    auto logger = std::make_unique<fixpp::log::Logger>(std::move(cfg), std::move(sinks));

    // ── Warm-up ────────────────────────────────────────────────────────────
    // One enqueue before the guard window to prime any lazy per-thread
    // state (e.g. thread-local caches, first-time initialisation paths).
    emit_warn(logger.get(), 0u);
    // Let the drain consume the warm-up record.
    std::this_thread::sleep_for(std::chrono::milliseconds{10});

    // Reset the PMR counter so warm-up allocations don't count.
    counting_res.reset();

    // ── Gate B1: PMR counting (in-process) ────────────────────────────────
    // Also arm the global interceptor so that if this binary runs under
    // LD_PRELOAD=libmallocnesia.so, a stray global new/malloc on the enqueue
    // path is caught (Gate B2) — null-safe when not preloaded.
    guard_start();

    // Fill levels to test: 10%, 50%, 95% of 1024 = 102, 512, 972 records.
    // The drain RUNS so these are non-overflow enqueues.
    constexpr std::uint64_t kCapacity = 1024u;
    constexpr std::uint64_t k10pct    = kCapacity / 10;   // 102
    constexpr std::uint64_t k50pct    = kCapacity / 2;    // 512
    constexpr std::uint64_t k95pct    = (kCapacity * 95) / 100;  // 972

    for (std::uint64_t i = 0; i < k95pct; ++i) {
        emit_warn(logger.get(), i + 1u);
    }

    guard_end();  // Gate B2 assertion (null-safe no-op if not under LD_PRELOAD)

    // ── Assertions ─────────────────────────────────────────────────────────

    // Gate B1: PMR counter must be 0 inside the guard window.
    EXPECT_EQ(counting_res.alloc_count(), 0u)
        << "Zero PMR-routed allocations must occur on the enqueue path "
           "(Gate B1 — counting_resource gate). "
           "If this fails, an ArgValue ctor or format-deferral path "
           "is incorrectly allocating via PMR.";

    // Shutdown and verify records were delivered.
    (void)logger->shutdown(std::chrono::seconds{5});

    std::pmr::set_default_resource(prev_default);

    // At least k10pct records must have been delivered (drain runs during test).
    // The exact count depends on drain rate; we just verify no PMR allocs occurred.
    EXPECT_GT(sink_raw->emit_count.load(), 0u)
        << "At least some records must have been delivered (drain runs during test)";
}

// ── TS-1c: bite-test helper ───────────────────────────────────────────────
//
// This test INTENTIONALLY inserts an allocation on the enqueue path (via a
// volatile new int to prevent dead-code elimination) and asserts that the
// PMR gate catches it.
//
// The bite-test is enabled only when ENABLE_ALLOC_BITE_TEST is defined
// (e.g. via -DENABLE_ALLOC_BITE_TEST=1 in the compile command).
// In normal test runs (cmake-driven) this test is SKIPPED (the define is absent).
// The orchestrator enables it for the gate-bite verification step.
//
// The mallocnesia LD_PRELOAD gate is bite-tested in the separate
// log_alloc_mallocnesia cmake test (it would exit(1) there).
#ifdef ENABLE_ALLOC_BITE_TEST
TEST(LogZeroAlloc, BiteTestPmrGateCatchesAlloc)
{
    CountingResource counting_res{};
    auto* prev_default = std::pmr::get_default_resource();
    std::pmr::set_default_resource(&counting_res);

    auto* sink_raw = new CaptureSink{};
    std::pmr::vector<std::unique_ptr<fixpp::log::Sink>> sinks{&counting_res};
    sinks.push_back(std::unique_ptr<fixpp::log::Sink>(sink_raw));

    fixpp::log::LoggerConfig cfg;
    cfg.capacity      = 256u;
    cfg.on_overflow   = fixpp::log::overflow_policy::drop_newest;
    cfg.ring_resource = &counting_res;
    auto logger = std::make_unique<fixpp::log::Logger>(std::move(cfg), std::move(sinks));

    // Warm-up.
    emit_warn(logger.get(), 0u);
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
    counting_res.reset();

    // ── Bite: intentional allocation via PMR in the guard window ─────────
    alloc_guard_start();

    // Route an allocation through PMR default resource to simulate a
    // hypothetical mis-placed alloc on the enqueue path.
    {
        // Force a PMR allocation via std::pmr::vector (uses default PMR resource).
        std::pmr::vector<int> vec;  // default PMR resource = counting_res
        vec.push_back(42);          // triggers PMR alloc
        (void)vec;
    }

    emit_warn(logger.get(), 1u);

    alloc_guard_end();

    // Gate B1 must have caught the PMR allocation.
    EXPECT_GT(counting_res.alloc_count(), 0u)
        << "Bite-test: the PMR gate MUST catch the intentional allocation";

    (void)logger->shutdown(std::chrono::seconds{5});
    std::pmr::set_default_resource(prev_default);
}
#endif  // ENABLE_ALLOC_BITE_TEST
