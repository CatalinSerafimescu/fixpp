// SPDX-License-Identifier: AGPL-3.0-or-later
//
// bench/log/log_spike.cpp
//
// T045 / TS-13: Own MPSC ring spike — 4 producer threads, 10M records total,
// capacity 65536, at 10/50/95% fill, reporting p99/p999.
//
// This file contains TWO sections:
//   1. OWN-RING SPIKE (unconditional): builds on the default (no-quill) preset.
//      Runs the 3 fill-rate scenarios (10%, 50%, 95%), reports p99/p999 per-
//      producer, and runs the Criterion-A mallocnesia zero-alloc gate.
//
//   2. QUILL COMPARISON (gated): compiled only when FIXPP_LOG_SPIKE_QUILL=ON.
//      This section calls into quill 11.x and is NOT required for v1.0
//      delivery. The quill Conan dep is optional and NOT pulled in the default
//      build.
//
// Anchors:
//   [2k §1.2]  — TS-13 spike specification (Criteria A–E)
//   [2k §D.2]  — disposition record obligation
//   [arch §9.3] — PROVISIONAL backend selection
//   [const §XIII.5] — mandatory spike before locking implementation
//
// Fill-rate strategy:
//   The ring capacity is 65536. To measure enqueue at X% fill, we:
//     1. Pause the drain thread (atomic flag).
//     2. Pre-fill the ring to ~X% capacity from the main thread (no timing).
//     3. Arm the mallocnesia guard (Criterion A).
//     4. Run 4 producer threads, each enqueue-ing samples; per-sample latency
//        recorded in a pre-allocated vector (no allocation inside the guard).
//     5. Disarm the guard.
//     6. Resume drain.
//   At 95% fill the ring is nearly full; many enqueues will overflow (drop).
//   This measures the OVERFLOW path (fast drop-newest) at 95%.
//
// Criterion A (zero-alloc):
//   The mallocnesia weak-decl idiom:
//     extern "C" { __attribute__((weak)) void alloc_guard_start(); }
//     if (alloc_guard_start) alloc_guard_start();
//   When run WITHOUT LD_PRELOAD the functions are null — no-op.
//   When run WITH LD_PRELOAD=libmallocnesia.so MALLOCNESIA_MAX_ALLOCS=0 the
//   strong symbols are bound, arming the interceptor; any malloc between
//   start/end exits 1.
//   Per [[feedback_tracking_pmr_resource_false_pass]], a weak symbol with a
//   `{}` body is a silent no-op on this toolchain — declaration-only is REQUIRED.
//
// Criterion B (quill comparison):
//   p99 ≤ 50ns at 50% fill on the reference CI hardware — GATED behind
//   FIXPP_LOG_SPIKE_QUILL=ON (quill dep not in default build).
//
// WSL2 debug note:
//   p99/p999 on WSL2 debug will EXCEED the 50ns Criterion-B ceiling. That is
//   expected — the obligation is execute + record, not meet 50ns locally.

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <memory_resource>
#include <numeric>
#include <thread>
#include <vector>

#include <fixpp/log/level.hpp>
#include <fixpp/log/logger.hpp>
#include <fixpp/log/record.hpp>
#include <fixpp/log/sink.hpp>

// ── mallocnesia guard markers ─────────────────────────────────────────────────
// Weak DECLARATIONS (no body) so the LD_PRELOAD strong symbols take precedence
// at runtime. A weak {} body would bind to the local no-op and never be
// interposed — that is the corrected idiom per [[feedback_tracking_pmr_resource_false_pass]].
extern "C" {
// NOLINTNEXTLINE(misc-use-internal-linkage)
__attribute__((weak)) void alloc_guard_start();
// NOLINTNEXTLINE(misc-use-internal-linkage)
__attribute__((weak)) void alloc_guard_end();
}  // extern "C"

namespace {

inline void guard_start() noexcept
{
    if (alloc_guard_start != nullptr) alloc_guard_start();
}
inline void guard_end() noexcept
{
    if (alloc_guard_end != nullptr) alloc_guard_end();
}

// ── PausingSink ───────────────────────────────────────────────────────────────
//
// A Sink that the drain thread runs through. When paused_, emit() spins
// (yield-loop) rather than returning immediately — this causes the drain
// thread to block, allowing the ring to fill up before measurement.
class PausingSink final : public fixpp::log::Sink
{
public:
    [[nodiscard]] fixpp::core::expected_t<void> open() override { return {}; }

    void emit(fixpp::log::Record const&) noexcept override
    {
        // If paused, spin-yield until unpaused.
        while (paused_.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }

    void flush(std::chrono::milliseconds) noexcept override {}
    void close() noexcept override { paused_.store(false, std::memory_order_release); }

    void pause()   noexcept { paused_.store(true,  std::memory_order_release); }
    void unpause() noexcept { paused_.store(false, std::memory_order_release); }

private:
    std::atomic<bool> paused_{false};
};

// ── Percentile helper ─────────────────────────────────────────────────────────
double percentile(std::vector<double>& samples, double pct)
{
    if (samples.empty()) return 0.0;
    std::sort(samples.begin(), samples.end());
    auto idx = static_cast<std::size_t>(pct * static_cast<double>(samples.size()));
    if (idx >= samples.size()) idx = samples.size() - 1u;
    return samples[idx];
}

// ── Constants ─────────────────────────────────────────────────────────────────
static constexpr std::uint32_t k_capacity   = 65536u;
static constexpr std::uint32_t k_num_threads = 4u;
static constexpr std::uint32_t k_total       = 10'000'000u;
static constexpr std::uint32_t k_per_thread  = k_total / k_num_threads;  // 2.5M

// Fixed format ID (compile-time CRC32 of a string literal).
static constexpr auto k_fmt_id = static_cast<std::uint32_t>(
    fixpp::log::detail::crc32_str("spike {} {} {} {}"));

static constexpr std::array<std::uint8_t, 16> k_zero_trace{};

// ── run_spike ─────────────────────────────────────────────────────────────────
//
// Runs one scenario at a given fill_pct. Returns {p99_ns, p999_ns}.
// fill_pct values: 0.10, 0.50, 0.95.
//
// For Criterion A: the mallocnesia guard is armed during the per-sample
// enqueue loop on each producer thread, AFTER all allocations for the
// measurement infrastructure (vectors, threads) have been done.
std::pair<double, double> run_spike(double fill_pct, bool criterion_a_arm)
{
    // Create the pausing sink (raw ptr captured by Impl).
    auto sink_owned = std::make_unique<PausingSink>();
    PausingSink* sink_ptr = sink_owned.get();

    // Build logger.
    fixpp::log::LoggerConfig cfg;
    cfg.capacity    = k_capacity;
    cfg.on_overflow = fixpp::log::overflow_policy::drop_newest;

    std::pmr::vector<std::unique_ptr<fixpp::log::Sink>> sinks{};
    sinks.push_back(std::move(sink_owned));

    auto logger = std::make_unique<fixpp::log::Logger>(std::move(cfg), std::move(sinks));

    // Pre-allocate per-thread sample vectors BEFORE the guard window
    // (these allocations must NOT happen inside the guard).
    std::vector<std::vector<double>> per_thread_samples(k_num_threads);
    for (auto& v : per_thread_samples) {
        v.reserve(k_per_thread);
    }

    // Pre-fill the ring to fill_pct occupancy by pausing the drain.
    sink_ptr->pause();
    {
        auto target_fill = static_cast<std::uint32_t>(
            static_cast<double>(k_capacity) * fill_pct);
        if (target_fill == 0) target_fill = 1;
        if (target_fill > k_capacity - 1u) target_fill = k_capacity - 1u;

        auto const ts = fixpp::core::utc_time_point{
            std::chrono::system_clock::now().time_since_epoch()};

        for (std::uint32_t i = 0u; i < target_fill; ++i) {
            logger->enqueue(
                fixpp::log::Level::info,
                fixpp::log::cat::session,
                k_fmt_id,
                k_zero_trace,
                0u,
                ts,
                {fixpp::log::ArgValue::from_u64(i),
                 fixpp::log::ArgValue::from_u64(i + 1),
                 fixpp::log::ArgValue::from_u64(i + 2),
                 fixpp::log::ArgValue::from_u64(i + 3)});
        }
        // Reset drop count from pre-fill (those aren't measured).
        logger->reset_drop_count();
    }

    // Synchronise thread start: all 4 producers launch together.
    std::barrier<> start_gate{static_cast<std::ptrdiff_t>(k_num_threads + 1)};
    std::vector<std::thread> threads;
    threads.reserve(k_num_threads);

    for (std::uint32_t t = 0u; t < k_num_threads; ++t) {
        threads.emplace_back([&, t]() {
            auto& samples = per_thread_samples[t];
            auto const ts = fixpp::core::utc_time_point{
                std::chrono::system_clock::now().time_since_epoch()};

            start_gate.arrive_and_wait();

            // Arm mallocnesia guard for Criterion A on the FIRST thread only
            // (the probe verifies the producer path; all threads run the same
            // code, so probing one is sufficient).
            if (criterion_a_arm && t == 0u) {
                guard_start();
            }

            for (std::uint32_t i = 0u; i < k_per_thread; ++i) {
                auto const t0 = std::chrono::steady_clock::now();
                logger->enqueue(
                    fixpp::log::Level::info,
                    fixpp::log::cat::session,
                    k_fmt_id,
                    k_zero_trace,
                    static_cast<std::uint64_t>(t),
                    ts,
                    {fixpp::log::ArgValue::from_u64(static_cast<std::uint64_t>(i)),
                     fixpp::log::ArgValue::from_u64(static_cast<std::uint64_t>(t)),
                     fixpp::log::ArgValue::from_u64(static_cast<std::uint64_t>(i + 1)),
                     fixpp::log::ArgValue::from_u64(static_cast<std::uint64_t>(t + 1))});
                auto const t1 = std::chrono::steady_clock::now();
                samples.push_back(static_cast<double>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
            }

            if (criterion_a_arm && t == 0u) {
                guard_end();
            }
        });
    }

    // Release all producers simultaneously.
    start_gate.arrive_and_wait();

    // Unpause drain while producers are running at 10%/50% fill.
    // At 95% the ring is nearly full — keep drain paused during measurement
    // so the overflow path (drop_newest) is consistently exercised.
    if (fill_pct < 0.90) {
        sink_ptr->unpause();
    }

    for (auto& th : threads) th.join();

    // Always unpause at end so the drain can flush.
    sink_ptr->unpause();

    // Merge all per-thread samples.
    std::vector<double> all_samples;
    all_samples.reserve(k_total);
    for (auto& v : per_thread_samples) {
        all_samples.insert(all_samples.end(), v.begin(), v.end());
    }

    double const p99  = percentile(all_samples, 0.99);
    double const p999 = percentile(all_samples, 0.999);

    // Explicit shutdown before destruction.
    (void)logger->shutdown(std::chrono::seconds{5});
    return {p99, p999};
}

}  // namespace

// ── main ──────────────────────────────────────────────────────────────────────
//
// Runs the three fill-rate scenarios and prints the results in a format
// suitable for copy-paste into the .specify/decisions/017-log-otel-verify.md
// disposition record.
int main()
{
    std::printf("=== TS-13 Own MPSC Ring Spike ===\n");
    std::printf("Config: %u producers × %u records = %u total | capacity=%u\n\n",
                k_num_threads, k_per_thread, k_total, k_capacity);

    static constexpr std::array<double, 3> k_fill_rates{0.10, 0.50, 0.95};
    static constexpr std::array<const char*, 3> k_labels{"10%", "50%", "95%"};

    // Warm-up run (not measured): prime OS thread scheduler + ring atomics.
    {
        auto [_p99, _p999] = run_spike(0.10, false);
        (void)_p99; (void)_p999;
    }

    std::array<double, 3> p99s{};
    std::array<double, 3> p999s{};

    for (std::size_t i = 0; i < k_fill_rates.size(); ++i) {
        // Only arm Criterion A on the 50% fill run (non-overflow path).
        bool const arm_a = (k_fill_rates[i] < 0.90);
        auto [p99, p999] = run_spike(k_fill_rates[i], arm_a);
        p99s[i]  = p99;
        p999s[i] = p999;

        std::printf("Fill %s: p99=%.1f ns  p999=%.1f ns  [Criterion-A armed: %s]\n",
                    k_labels[i], p99, p999,
                    arm_a ? "YES (mallocnesia)" : "NO (overflow path)");
    }

    std::printf("\n--- Criterion A summary ---\n");
    std::printf("  If run WITHOUT LD_PRELOAD: guards are null-checked no-ops; ");
    std::printf("zero-alloc not verified by this run.\n");
    std::printf("  Run: LD_PRELOAD=tools/mallocnesia/libmallocnesia.so "
                "MALLOCNESIA_MAX_ALLOCS=0 ./log_spike\n");
    std::printf("  Expected: process exits 0 (zero allocs on producer path).\n\n");

    std::printf("--- Criterion B note (WSL2 debug) ---\n");
    std::printf("  Reference ceiling: p99 <= 50 ns at 50%% fill (on CI hardware).\n");
    std::printf("  This run: p99=%.1f ns at 50%% fill.\n", p99s[1]);
    std::printf("  WSL2 debug p99 will exceed 50 ns — expected per spec §1.2.\n\n");

    std::printf("--- Disposition ---\n");
    std::printf("  own lock-free MPSC ring is the v1.0 shipping candidate.\n");
    std::printf("  PROVISIONAL per anchor §1.2/§D.2 + [arch §9.3].\n");
    std::printf("  TS-13 executed and recorded.\n");
    std::printf("  Backend-selection validity (Criterion B vs quill) is a\n");
    std::printf("  recorded, non-blocking metric that does NOT gate v1.0 delivery.\n");

    return 0;
}

// ── QUILL COMPARISON (gated behind FIXPP_LOG_SPIKE_QUILL=ON) ─────────────────
//
// Everything below this point is compiled ONLY when FIXPP_LOG_SPIKE_QUILL=ON
// (the quill/11.x Conan dep is not pulled in the default build).
// The default build does NOT need quill — this gate satisfies the requirement
// that "the own-ring spike MUST build + run on the DEFAULT build (no quill)".
#ifdef FIXPP_LOG_SPIKE_QUILL

// quill 11.x includes (available only when the conan dep is present).
#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/Logger.h>
#include <quill/LogMacros.h>
#include <quill/sinks/NullSink.h>

namespace quill_spike {

// Run quill at 50% fill with 4 producers.  Reports p99 for Criterion-B comparison.
void run_quill_comparison()
{
    // Start quill backend.
    quill::BackendOptions backend_opts;
    backend_opts.cpu_affinity = static_cast<uint16_t>(-1);
    quill::Backend::start(backend_opts);

    // Create a null sink.
    auto sink = quill::Frontend::create_or_get_sink<quill::NullSink>("null");

    // Create loggers for each producer thread.
    std::array<quill::Logger*, k_num_threads> loggers{};
    for (std::size_t t = 0; t < k_num_threads; ++t) {
        auto name = std::string{"quill_prod_"} + std::to_string(t);
        loggers[t] = quill::Frontend::create_or_get_logger(name, sink);
        loggers[t]->set_log_level(quill::LogLevel::Info);
    }

    // Pre-allocate sample storage.
    std::vector<std::vector<double>> per_thread(k_num_threads);
    for (auto& v : per_thread) v.reserve(k_per_thread);

    std::barrier<> gate{static_cast<std::ptrdiff_t>(k_num_threads + 1)};
    std::vector<std::thread> threads;
    threads.reserve(k_num_threads);

    for (std::size_t t = 0; t < k_num_threads; ++t) {
        threads.emplace_back([&, t]() {
            auto& samples = per_thread[t];
            gate.arrive_and_wait();
            for (std::uint32_t i = 0u; i < k_per_thread; ++i) {
                auto const t0 = std::chrono::steady_clock::now();
                LOG_INFO(loggers[t], "spike {} {} {} {}", i, t, i + 1, t + 1);
                auto const t1 = std::chrono::steady_clock::now();
                samples.push_back(static_cast<double>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
            }
        });
    }

    guard_start();  // Criterion A check on quill path.
    gate.arrive_and_wait();
    for (auto& th : threads) th.join();
    guard_end();

    // Collect and report.
    std::vector<double> all_samples;
    all_samples.reserve(k_total);
    for (auto& v : per_thread)
        all_samples.insert(all_samples.end(), v.begin(), v.end());

    double const p99  = percentile(all_samples, 0.99);
    double const p999 = percentile(all_samples, 0.999);
    std::printf("Quill 11.x (50%% fill proxy): p99=%.1f ns  p999=%.1f ns\n", p99, p999);
    std::printf("Criterion B: p99 <= 50 ns? %s\n", p99 <= 50.0 ? "PASS" : "FAIL (see WSL2 note)");

    quill::Backend::stop();
}

}  // namespace quill_spike
#endif  // FIXPP_LOG_SPIKE_QUILL
