// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/log/logger.hpp
//
// Logger facade, overflow_policy, LoggerConfig, FIXPP_LOG0 macro,
// FIXPP_FORMAT_ID, and detail::enqueue_record_notrace.
//
// Anchors:
//   [2k §4.3]   — Logger/ring/overflow/filter/macros (locked surface)
//   data-model.md §overflow_policy / §LoggerConfig / §Logger
//   contracts/log-core.md (runtime + compile-time obligations)
//   [const §XV.9] — no std::mutex / std::shared_mutex on this include edge
//   [const §XIV.2] — Logger::Impl pimpl; OTel SDK stays off this header
//   [arch §2.3]  — log → {core} only
//
// IMPORTANT: Keep this header free of:
//   - asio headers (no awaitable, no executor)
//   - OpenTelemetry SDK headers
//   - std::mutex / std::shared_mutex
// All of those are confined to Logger::Impl in logger.cpp.
//
// async_flush() and shutdown() are declared here but the async_flush()
// signature is forward-declared with a simple void return for now (the asio
// awaitable<void> variant requires the asio include which we must keep off
// this header; the full body is in slice 3b-ii / T027).
// shutdown() is DECLARED only in 3b-i; the body is in logger.cpp (T027 scope).
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <memory_resource>

#include <fixpp/core/error.hpp>
#include <fixpp/log/level.hpp>
#include <fixpp/log/record.hpp>
#include <fixpp/log/sink.hpp>

namespace fixpp::log {

// ── overflow_policy ────────────────────────────────────────────────────────
//
// Controls what the producer does when the ring is full.
//
// [2k §4.3] / data-model.md §overflow_policy.
enum class overflow_policy : std::uint8_t {
    // drop_newest (default): the producer detects a full ring and drops the
    // record it is about to enqueue. Increments drop_count_. In a FIFO ring,
    // dropping the newest-produced record preserves all older in-flight records
    // — satisfying [const §XIII.2]'s "drop-oldest permitted" allowance.
    // read_sequence_ is std::atomic; producers load with relaxed ordering (a
    // stale read can only cause an early drop — safe under drop_newest).
    drop_newest = 0,

    // block: producer spins with std::this_thread::yield() until a ring slot
    // is available.
    //
    // WARNING — PROHIBITED FROM SESSION-STRAND COROUTINES: using block inside
    // a session coroutine pins the executor OS thread at the call site, stalling
    // all sessions on that thread. This is equivalent to holding std::mutex in
    // a coroutine context, which is prohibited by [const §XI.3].
    //
    // block is safe only for dedicated non-coroutine producer threads (e.g.
    // background control-plane threads that do NOT run on the session executor).
    // A debug FIXPP_ASSERT fires in debug builds if block is used from a
    // detected session-executor thread.
    block = 1,
};

// ── LoggerConfig ───────────────────────────────────────────────────────────
//
// Construction-time configuration for the Logger.
// [2k §4.3] / data-model.md §LoggerConfig.
struct LoggerConfig {
    // Ring capacity (number of Record slots). MUST be a power of 2.
    // Default: 65536 records × 256 bytes = 16 MiB ring.
    std::uint32_t           capacity        = 65536u;

    overflow_policy         on_overflow     = overflow_policy::drop_newest;

    // PMR resource for ring allocation. Lifetime MUST outlive Logger.
    std::pmr::memory_resource* ring_resource =
        std::pmr::get_default_resource();

    // Drain thread CPU affinity. Negative = no hint.
    int                     drain_cpu_affinity = -1;

    // Default drain timeout for shutdown(). After this deadline in-flight
    // records are abandoned and log_drain_timeout is returned.
    std::chrono::milliseconds drain_timeout = std::chrono::seconds{5};
};

// ── FIXPP_FORMAT_ID ────────────────────────────────────────────────────────
//
// Computes a constexpr uint32_t CRC32 of a string literal at compile time.
// No string crosses the producer→ring boundary; only the 4-byte ID is stored
// in Record::format_id.  The drain thread resolves it back to the format
// string via the compile-time registry in format_registry.cpp.
//
// Usage: FIXPP_FORMAT_ID("msg {} arrived")
//
// [2k §4.3] / contracts/log-core.md FIXPP_FORMAT_ID obligation.
#define FIXPP_FORMAT_ID(fmt)                                                  \
    (static_cast<std::uint32_t>(::fixpp::log::detail::crc32_str(fmt)))

// ── Logger ─────────────────────────────────────────────────────────────────
//
// The main async-logger facade. Pimpl pattern: all implementation details
// (ring buffers, drain thread, atomics, std::mutex) live in Logger::Impl
// defined in logger.cpp — this header carries NO std::mutex or asio includes
// so it is safe to include from awaitable-bearing headers. ([const §XV.9])
//
// Ownership:
//   - Sinks are passed at construction and not swappable mid-flight.
//   - Logger dtor joins the drain thread (blocks until in-flight records are
//     processed or the drain_timeout elapses).
//
// [2k §4.3] / data-model.md §Logger / contracts/log-core.md.
class Logger {
public:
    explicit Logger(LoggerConfig                            config,
                    std::pmr::vector<std::unique_ptr<Sink>> sinks);

    // Dtor joins the drain thread. Behaviour under concurrent calls to
    // enqueue() during destruction is unspecified — callers must stop
    // producing before the Logger is destroyed (or call shutdown() first).
    ~Logger();

    // Non-copyable, non-movable (owns OS thread + PMR ring).
    Logger(Logger const&)            = delete;
    Logger& operator=(Logger const&) = delete;
    Logger(Logger&&)                 = delete;
    Logger& operator=(Logger&&)      = delete;

    // ── Producer API (called from any thread) ──────────────────────────────

    // Enqueue one log record onto the MPSC ring.
    //
    // Zero heap allocation. Lock-free MPSC CAS on write_sequence_.
    // Never throws (noexcept). If the ring is full and on_overflow==drop_newest,
    // the record is silently dropped and drop_count() is incremented.
    //
    // The initializer_list<ArgValue> backing array is stack-allocated by the
    // compiler; enqueue() copies at most k_max_args=6 ArgValues by value into
    // Record::args and ignores excess (contracts/log-core.md New-3 obligation).
    //
    // trace_id / span_id are the OTel correlation fields for LOG-003; pass
    // zeroed values for context-free call sites (FIXPP_LOG0).
    void enqueue(Level                                level,
                 Category                             category,
                 std::uint32_t                        format_id,
                 std::array<std::uint8_t, 16> const&  trace_id,
                 std::uint64_t                        span_id,
                 fixpp::core::utc_time_point          timestamp,
                 std::initializer_list<ArgValue>      args) noexcept;

    // ── Runtime category filter ────────────────────────────────────────────

    // Enable / disable a category at runtime (lock-free compare-exchange on
    // enabled_categories_mask_). Records whose category bit (category & 63u)
    // is clear are dropped before enqueue; counted in filter_count(), NOT
    // drop_count() (contracts/log-core.md FR-011 / TS-8).
    void set_category_enabled(Category cat, bool enabled) noexcept;
    bool is_category_enabled(Category cat) const noexcept;

    // ── Drop / filter accounting ───────────────────────────────────────────

    // Three SEPARATE counters (contracts/log-core.md §Runtime obligations).
    [[nodiscard]] std::uint64_t drop_count()         const noexcept;
    [[nodiscard]] std::uint64_t timeout_drop_count() const noexcept;
    [[nodiscard]] std::uint64_t filter_count()       const noexcept;

    void reset_drop_count()         noexcept;
    void reset_timeout_drop_count() noexcept;
    void reset_filter_count()       noexcept;

    // Per-sink exception counter (drain thread catches each Sink::emit/flush
    // exception and increments this per-sink counter — T026 FR-005).
    [[nodiscard]] std::uint64_t sink_error_count(std::size_t sink_index) const noexcept;

    // ── Flush / shutdown ───────────────────────────────────────────────────
    //
    // Both are OFF-HOT-PATH control/shutdown operations, explicitly EXCLUDED
    // from the FR-001 zero-alloc producer gate (contracts/log-core.md New-4).

    // Synchronous flush + shutdown. Drains in-flight records and calls
    // flush(drain_timeout) on each Sink. On timeout returns
    // unexpected(log_drain_timeout) and bumps timeout_drop_count() (the
    // SEPARATE timeout counter — does NOT inflate drop_count()).
    // Callable from any thread; blocks until drain completes or times out.
    // Safe to call multiple times (idempotent after first call).
    // [2k §4.3] / contracts/log-core.md FR-014 / SC-007.
    [[nodiscard]] fixpp::core::expected_t<void> shutdown(
        std::chrono::milliseconds drain_timeout);

    // Async flush — enqueues a flush sentinel into the ring; the drain thread
    // processes all pending records before this sentinel and then invokes
    // on_done() on the drain thread.
    // Off-hot-path; one allocation per call (completion handler).
    // The on_done callback MUST NOT call back into Logger (no enqueue, no
    // shutdown) — it is called from the drain thread with no re-entrancy.
    // [T027 scope]
    //
    // NOTE: the full asio::awaitable<void> signature (executor-posting variant)
    // is in T033/T034 scope. The std::function<void()> variant ships here.
    //
    // This method is EXCLUDED from the FR-001 zero-alloc gate (New-4 contract).
    // [const §XV.9]: std::function does NOT drag asio/std::mutex into this
    // header — the allocation happens inside the .cpp implementation only.
    void async_flush(std::function<void()> on_done);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ── detail helpers ─────────────────────────────────────────────────────────

namespace detail {

// enqueue_record_notrace: called by FIXPP_LOG0.
// Looks up the effective logger from a global/thread context (for 3b-i we
// use a module-local pointer; in production the engine wires this via the
// Logger passed at construction).
// Zeroed trace_id / span_id (context-free; not a bug per [2k §6.4]).
//
// NOTE: For 3b-i the function takes an explicit Logger* so tests can pass
// a concrete Logger directly.  The macro is defined to use a clock for
// the timestamp (std::chrono::system_clock::now()).
inline void enqueue_record_notrace(
    Logger*                          logger,
    Level                            level,
    Category                         category,
    std::uint32_t                    format_id,
    fixpp::core::utc_time_point      timestamp,
    std::initializer_list<ArgValue>  args) noexcept
{
    if (logger == nullptr) return;
    static constexpr std::array<std::uint8_t, 16> zeroed_trace_id{};
    logger->enqueue(level, category, format_id,
                    zeroed_trace_id, 0u, timestamp, args);
}

}  // namespace detail

}  // namespace fixpp::log

// ── FIXPP_LOG0 ──────────────────────────────────────────────────────────────
//
// Context-free log macro (Tier 3 per LOG-003).
// Zeroed trace_id / span_id — not a bug; used from non-session code paths.
// No thread_local. No co_await.
//
// Usage:
//   FIXPP_LOG0(logger_ptr, info, cat::session, FIXPP_FORMAT_ID("msg {}"), args...)
//
// The compile-time level cutoff is checked via if constexpr so that sites
// below FIXPP_LOG_MIN_LEVEL compile to zero bytes (contracts/log-core.md FR-010).
//
// [2k §4.3] / contracts/log-core.md / data-model.md §Trace-correlation-macros.
#define FIXPP_LOG0(logger_ptr, lvl, cat, fmt, ...)                            \
    do {                                                                      \
        if constexpr (static_cast<int>(::fixpp::log::Level::lvl)             \
                      >= FIXPP_LOG_MIN_LEVEL) {                               \
            ::fixpp::log::detail::enqueue_record_notrace(                    \
                (logger_ptr),                                                 \
                ::fixpp::log::Level::lvl,                                     \
                (cat),                                                        \
                FIXPP_FORMAT_ID(fmt),                                         \
                ::fixpp::core::utc_time_point{                               \
                    std::chrono::system_clock::now().time_since_epoch()},    \
                {__VA_ARGS__});                                               \
        }                                                                     \
    } while (false)
