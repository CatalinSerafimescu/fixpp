// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/log/test_shutdown_async_flush.cpp
//
// T027 tests: shutdown(drain_timeout) + async_flush().
//
// Contracts tested (contracts/log-core.md FR-014 / SC-007 / [2k §4.3]):
//
//   T027-a: shutdown() on timeout returns unexpected(log_drain_timeout)
//           and bumps timeout_drop_count() — NOT drop_count().
//
//   T027-b: shutdown() on success returns ok (no timeout).
//
//   T027-c: async_flush() posts a flush sentinel; the drain thread, on
//           consuming it, invokes the completion callback.
//
// Anchors:
//   [2k §4.3] / contracts/log-core.md FR-014, SC-007
//   data-model.md §Logger

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory_resource>
#include <thread>
#include <vector>

#include <fixpp/core/error.hpp>
#include <fixpp/log/level.hpp>
#include <fixpp/log/logger.hpp>
#include <fixpp/log/record.hpp>
#include <fixpp/log/sink.hpp>

namespace {

// ── SlowFlushSink ─────────────────────────────────────────────────────────
// A sink whose flush() blocks for a configurable duration.
// Used to force a drain-timeout: the drain thread is blocked in flush(),
// so drain_done_ is never signalled within the short drain_timeout.
class SlowFlushSink final : public fixpp::log::Sink {
public:
    std::chrono::milliseconds flush_delay{200};
    std::atomic<int>          flush_call_count{0};
    std::atomic<int>          emit_count{0};

    [[nodiscard]] fixpp::core::expected_t<void> open() override { return {}; }

    void emit(fixpp::log::Record const&) noexcept override {
        emit_count.fetch_add(1, std::memory_order_relaxed);
    }

    void flush(std::chrono::milliseconds) noexcept override {
        flush_call_count.fetch_add(1, std::memory_order_relaxed);
        std::this_thread::sleep_for(flush_delay);
    }

    void close() noexcept override {}
};

// ── FastSink ──────────────────────────────────────────────────────────────
// A sink whose flush() returns immediately.
class FastSink final : public fixpp::log::Sink {
public:
    std::atomic<int>                  emit_count{0};
    std::atomic<int>                  flush_count{0};
    std::vector<fixpp::log::Record>   records;

    [[nodiscard]] fixpp::core::expected_t<void> open() override { return {}; }
    void emit(fixpp::log::Record const& rec) noexcept override {
        emit_count.fetch_add(1, std::memory_order_relaxed);
        records.push_back(rec);
    }
    void flush(std::chrono::milliseconds) noexcept override {
        flush_count.fetch_add(1, std::memory_order_relaxed);
    }
    void close() noexcept override {}
};

}  // namespace

// ── T027-a: shutdown timeout ──────────────────────────────────────────────
//
// shutdown(50ms) with a sink whose flush() takes 200ms.
// Expected:
//   - Returns unexpected(log_drain_timeout).
//   - timeout_drop_count() == 1.
//   - drop_count() unchanged (still 0).
TEST(LogShutdown, TimeoutBumpsTimeoutDropCountNotDropCount)
{
    auto* slow_sink_raw = new SlowFlushSink{};
    slow_sink_raw->flush_delay = std::chrono::milliseconds{200};

    std::pmr::vector<std::unique_ptr<fixpp::log::Sink>> sinks{};
    sinks.push_back(std::unique_ptr<fixpp::log::Sink>(slow_sink_raw));

    fixpp::log::LoggerConfig cfg;
    cfg.capacity    = 64u;
    cfg.on_overflow = fixpp::log::overflow_policy::drop_newest;
    // config_.drain_timeout is used by the drain thread's own flush call.
    // Set it long so the drain thread doesn't bail early on its own.
    cfg.drain_timeout = std::chrono::seconds{5};

    auto logger = std::make_unique<fixpp::log::Logger>(std::move(cfg), std::move(sinks));

    // Enqueue one record to give the drain something to do.
    std::array<std::uint8_t, 16> zeroed{};
    auto ts = fixpp::core::utc_time_point{
        std::chrono::system_clock::now().time_since_epoch()};
    constexpr auto fmt_id =
        static_cast<std::uint32_t>(fixpp::log::detail::crc32_str("shutdown timeout test {}"));
    logger->enqueue(fixpp::log::Level::warn, fixpp::log::cat::session,
                    fmt_id, zeroed, 0u, ts,
                    {fixpp::log::ArgValue::from_u64(42u)});

    // Sanity: no drops yet.
    EXPECT_EQ(logger->drop_count(), 0u)
        << "No drops before shutdown";
    EXPECT_EQ(logger->timeout_drop_count(), 0u)
        << "No timeout drops before shutdown";

    // shutdown() with a short timeout — should time out.
    auto result = logger->shutdown(std::chrono::milliseconds{50});

    // ── Assertions ──────────────────────────────────────────────────────────

    // Must return unexpected(log_drain_timeout).
    EXPECT_FALSE(result.has_value())
        << "shutdown() must return unexpected on timeout";
    EXPECT_EQ(result.error(), fixpp::core::error::log_drain_timeout)
        << "Error code must be log_drain_timeout (slot 126)";

    // timeout_drop_count must have been incremented by exactly 1.
    EXPECT_EQ(logger->timeout_drop_count(), 1u)
        << "timeout_drop_count() must be 1 after a drain timeout";

    // drop_count must NOT have been incremented (separate counter).
    EXPECT_EQ(logger->drop_count(), 0u)
        << "drop_count() must NOT be incremented on a drain timeout "
           "(it is a separate counter from timeout_drop_count)";

    // Logger dtor will join the drain thread (which finishes after the flush).
    // This blocks ~150ms (200ms flush - 50ms already elapsed).
    // Acceptable for a test; prevents resource leaks.
}

// ── T027-b: shutdown success ──────────────────────────────────────────────
//
// shutdown(5s) with a fast-flush sink.
// Expected:
//   - Returns ok (no timeout).
//   - timeout_drop_count() == 0.
//   - Records were delivered.
TEST(LogShutdown, SuccessReturnsOk)
{
    auto* fast_sink_raw = new FastSink{};

    std::pmr::vector<std::unique_ptr<fixpp::log::Sink>> sinks{};
    sinks.push_back(std::unique_ptr<fixpp::log::Sink>(fast_sink_raw));

    fixpp::log::LoggerConfig cfg;
    cfg.capacity    = 64u;
    cfg.on_overflow = fixpp::log::overflow_policy::drop_newest;

    auto logger = std::make_unique<fixpp::log::Logger>(std::move(cfg), std::move(sinks));

    std::array<std::uint8_t, 16> zeroed{};
    auto ts = fixpp::core::utc_time_point{
        std::chrono::system_clock::now().time_since_epoch()};
    constexpr auto fmt_id =
        static_cast<std::uint32_t>(fixpp::log::detail::crc32_str("shutdown success test {}"));

    // Enqueue a few records.
    for (std::uint64_t i = 0; i < 5u; ++i) {
        logger->enqueue(fixpp::log::Level::warn, fixpp::log::cat::session,
                        fmt_id, zeroed, 0u, ts,
                        {fixpp::log::ArgValue::from_u64(i)});
    }

    // shutdown() with generous timeout — should succeed.
    auto result = logger->shutdown(std::chrono::seconds{5});

    EXPECT_TRUE(result.has_value())
        << "shutdown() must return ok when drain completes within timeout";
    EXPECT_EQ(logger->timeout_drop_count(), 0u)
        << "timeout_drop_count() must be 0 on successful shutdown";
    EXPECT_EQ(logger->drop_count(), 0u)
        << "drop_count() must be 0 (no overflow)";

    // All records should have been delivered.
    EXPECT_EQ(fast_sink_raw->emit_count.load(), 5u)
        << "All 5 records must have been delivered before shutdown returned";
}

// ── T027-c: async_flush posts completion ──────────────────────────────────
//
// async_flush(on_done) enqueues a sentinel; the drain invokes on_done()
// after processing all preceding records.
TEST(LogShutdown, AsyncFlushPostsCompletion)
{
    auto* fast_sink_raw = new FastSink{};

    std::pmr::vector<std::unique_ptr<fixpp::log::Sink>> sinks{};
    sinks.push_back(std::unique_ptr<fixpp::log::Sink>(fast_sink_raw));

    fixpp::log::LoggerConfig cfg;
    cfg.capacity    = 128u;
    cfg.on_overflow = fixpp::log::overflow_policy::drop_newest;

    auto logger = std::make_unique<fixpp::log::Logger>(std::move(cfg), std::move(sinks));

    std::array<std::uint8_t, 16> zeroed{};
    auto ts = fixpp::core::utc_time_point{
        std::chrono::system_clock::now().time_since_epoch()};
    constexpr auto fmt_id =
        static_cast<std::uint32_t>(fixpp::log::detail::crc32_str("async flush test {}"));

    // Enqueue 3 records before the flush sentinel.
    for (std::uint64_t i = 0; i < 3u; ++i) {
        logger->enqueue(fixpp::log::Level::warn, fixpp::log::cat::session,
                        fmt_id, zeroed, 0u, ts,
                        {fixpp::log::ArgValue::from_u64(i)});
    }

    // async_flush: the completion fires AFTER the 3 records are consumed.
    std::atomic<bool> flush_done{false};
    std::atomic<int>  emit_count_at_flush{-1};

    logger->async_flush([&]() {
        // Capture how many records have been emitted at the time of the callback.
        emit_count_at_flush.store(
            fast_sink_raw->emit_count.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        flush_done.store(true, std::memory_order_release);
    });

    // Wait for the completion to fire (drain thread; max 2 seconds).
    {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
        while (!flush_done.load(std::memory_order_acquire)
               && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
    }

    ASSERT_TRUE(flush_done.load(std::memory_order_acquire))
        << "async_flush completion must fire within 2 seconds";

    // All 3 records must have been emitted BEFORE the flush completion fired.
    EXPECT_EQ(emit_count_at_flush.load(), 3)
        << "The async_flush completion must fire AFTER all preceding records "
           "have been emitted (emit_count at flush = "
        << emit_count_at_flush.load() << ", expected 3)";

    (void)logger->shutdown(std::chrono::seconds{5});
}

// ── T027-d: idempotent shutdown ──────────────────────────────────────────
//
// Calling shutdown() twice must be safe (idempotent after first call).
TEST(LogShutdown, ShutdownIsIdempotent)
{
    auto* fast_sink_raw = new FastSink{};

    std::pmr::vector<std::unique_ptr<fixpp::log::Sink>> sinks{};
    sinks.push_back(std::unique_ptr<fixpp::log::Sink>(fast_sink_raw));

    fixpp::log::LoggerConfig cfg;
    cfg.capacity = 64u;

    auto logger = std::make_unique<fixpp::log::Logger>(std::move(cfg), std::move(sinks));

    auto r1 = logger->shutdown(std::chrono::seconds{5});
    EXPECT_TRUE(r1.has_value()) << "First shutdown must succeed";

    auto r2 = logger->shutdown(std::chrono::seconds{5});
    EXPECT_TRUE(r2.has_value()) << "Second shutdown must also succeed (idempotent)";
}
