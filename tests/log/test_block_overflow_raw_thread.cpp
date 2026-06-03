// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/log/test_block_overflow_raw_thread.cpp
//
// TS-3: overflow_policy::block on a raw std::thread (NOT a session strand).
//
// Contract (contracts/log-core.md, FR-004):
//   - With overflow_policy::block and a full ring, the producer thread BLOCKS
//     (spins/yields) for at least 10 ms while the ring is full.
//   - When the drain resumes and frees a slot, the producer unblocks and the
//     record is eventually delivered to the sink.
//
// Test design:
//   - Logger with capacity=1 ring + a PausingSink that holds the drain blocked
//     inside emit() for 50 ms.
//   - Main thread enqueues record #0 (fills the ring; drain picks it up and
//     blocks in PausingSink::emit()).
//   - A dedicated raw std::thread enqueues record #1 with block mode; it
//     must block while record #0 is being "emitted" (drain hasn't advanced
//     read_sequence_ yet — emit() returns AFTER the pause, then read advances).
//   - We measure the actual block duration; it must be >= 10 ms.
//   - After the sink's pause, the drain advances read_sequence_, the blocked
//     producer acquires the slot, and record #1 is eventually delivered.
//
// Anchors:
//   [2k §4.3] / contracts/log-core.md FR-004, TS-3
//   data-model.md §overflow_policy::block

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory_resource>
#include <thread>
#include <vector>

#include <fixpp/log/level.hpp>
#include <fixpp/log/logger.hpp>
#include <fixpp/log/record.hpp>
#include <fixpp/log/sink.hpp>

namespace {

// A sink that blocks emit() for a configurable duration (simulates slow I/O).
// Used to hold the drain thread inside emit() so the ring stays full.
class TimedBlockSink final : public fixpp::log::Sink {
public:
    // block_duration: how long emit() blocks for the FIRST record only.
    std::chrono::milliseconds        block_duration{50};
    std::vector<fixpp::log::Record>  captured;
    std::atomic<int>                 emit_count{0};
    std::atomic<bool>                first_emit_started{false};

    [[nodiscard]] fixpp::core::expected_t<void> open() override { return {}; }

    void emit(fixpp::log::Record const& rec) noexcept override {
        if (emit_count.fetch_add(1, std::memory_order_relaxed) == 0) {
            first_emit_started.store(true, std::memory_order_release);
            // Block for block_duration on the first record.
            std::this_thread::sleep_for(block_duration);
        }
        captured.push_back(rec);
    }

    void flush(std::chrono::milliseconds) noexcept override {}
    void close() noexcept override {}
};

}  // namespace

// ── T016 / TS-3 ─────────────────────────────────────────────────────────────

TEST(LogBlockOverflow, BlockModeRawThreadBlocks10ms)
{
    // Arrange: capacity=1 ring + block mode + a 50 ms blocking first-emit sink.
    auto* sink_raw = new TimedBlockSink{};
    sink_raw->block_duration = std::chrono::milliseconds{50};

    std::pmr::vector<std::unique_ptr<fixpp::log::Sink>> sinks{};
    sinks.push_back(std::unique_ptr<fixpp::log::Sink>(sink_raw));

    fixpp::log::LoggerConfig cfg;
    cfg.capacity    = 1u;  // smallest possible ring — will fill after 1 enqueue
    cfg.on_overflow = fixpp::log::overflow_policy::block;

    auto logger = std::make_unique<fixpp::log::Logger>(std::move(cfg), std::move(sinks));

    std::array<std::uint8_t, 16> zeroed_trace_id{};
    auto ts = fixpp::core::utc_time_point{
        std::chrono::system_clock::now().time_since_epoch()};

    constexpr auto fmt_first  =
        static_cast<std::uint32_t>(fixpp::log::detail::crc32_str("block test first"));
    constexpr auto fmt_second =
        static_cast<std::uint32_t>(fixpp::log::detail::crc32_str("block test second"));

    // Enqueue record #0 — fills the ring; the drain picks it up and blocks in
    // TimedBlockSink::emit() for 50 ms (read_sequence_ advances AFTER emit()).
    logger->enqueue(fixpp::log::Level::info,
                    fixpp::log::cat::session,
                    fmt_first,
                    zeroed_trace_id, 0u, ts,
                    {fixpp::log::ArgValue::from_u64(0u)});

    // Wait until the drain has entered emit() for record #0.
    // This ensures the ring slot is NOT yet freed (read_sequence_ not yet advanced).
    {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{1};
        while (!sink_raw->first_emit_started.load(std::memory_order_acquire)
               && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
    }
    ASSERT_TRUE(sink_raw->first_emit_started.load(std::memory_order_acquire))
        << "Drain should have entered emit() for record #0 within 1 second";

    // ── Producer on a raw std::thread ─────────────────────────────────────────
    // Record the time before the blocking enqueue.
    std::chrono::steady_clock::time_point enqueue_start;
    std::chrono::steady_clock::time_point enqueue_end;
    std::atomic<bool> producer_done{false};

    std::thread producer([&]() {
        enqueue_start = std::chrono::steady_clock::now();
        // This enqueue MUST block until the drain advances read_sequence_.
        logger->enqueue(fixpp::log::Level::info,
                        fixpp::log::cat::session,
                        fmt_second,
                        zeroed_trace_id, 0u, ts,
                        {fixpp::log::ArgValue::from_u64(1u)});
        enqueue_end = std::chrono::steady_clock::now();
        producer_done.store(true, std::memory_order_release);
    });

    // Wait for the producer thread to unblock and finish.
    producer.join();

    // Shut down the logger (drain the remaining record #1).
    (void)logger->shutdown(std::chrono::seconds{5});

    // ── Assertions ─────────────────────────────────────────────────────────────

    // The producer must have blocked for at least 10 ms.
    auto blocked_for = std::chrono::duration_cast<std::chrono::milliseconds>(
        enqueue_end - enqueue_start);

    EXPECT_GE(blocked_for.count(), 10)
        << "Producer thread must have blocked >= 10 ms while the ring was full "
           "(block_duration=" << sink_raw->block_duration.count() << " ms, "
           "actual block=" << blocked_for.count() << " ms)";

    // Both records were eventually delivered (no drops in block mode).
    EXPECT_EQ(logger->drop_count(), 0u)
        << "drop_count must be 0 — block mode must not drop records";

    EXPECT_EQ(sink_raw->captured.size(), 2u)
        << "Both records must have been delivered to the sink";

    // Record ordering: record #0 (fmt_first) before record #1 (fmt_second).
    ASSERT_GE(sink_raw->captured.size(), 2u);
    EXPECT_EQ(sink_raw->captured[0].format_id, fmt_first)
        << "First delivered record must be record #0";
    EXPECT_EQ(sink_raw->captured[1].format_id, fmt_second)
        << "Second delivered record must be record #1";
}
