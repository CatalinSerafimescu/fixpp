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
//   - Logger with capacity=1 ring + a TimedBlockSink that holds the drain
//     inside emit() until the TEST RELEASES IT — not for a fixed interval.
//   - Main thread enqueues record #0 (fills the ring; drain picks it up and
//     parks in TimedBlockSink::emit(), so read_sequence_ has NOT advanced).
//   - A dedicated raw std::thread enqueues record #1 with block mode; it
//     must block, because the slot is still held.
//   - The test OBSERVES that the producer is still inside enqueue() after a
//     real interval, then releases the drain, joins, and measures.
//
// ⚠️ THE DRAIN USED TO BE HELD BY `sleep_for(50 ms)`, AND THAT MADE THE TEST'S
// PRECONDITION A RACE AGAINST `std::thread` CREATION. The producer thread was
// spawned only after the drain had already entered emit(), so the whole of
// thread construction had to fit inside the sink's 50 ms window. When it did
// not — measured on `windows-msvc-asan` under `ctest --parallel 4`, campaign
// run 33977674899 — the drain finished sleeping, read_sequence_ advanced, the
// slot came free, and the producer's enqueue returned WITHOUT EVER BLOCKING:
// `actual block=0 ms` against a `>= 10` expectation.
//
// That was an honest red over a VACUOUS run: nothing about block mode had been
// exercised, because the state the contract is about (a full ring) no longer
// held by the time the producer arrived. Widening the window would only have
// made the same race rarer and its failures more confusing.
//
// So the window is removed rather than widened. The sink parks until the test
// says otherwise, which makes "the ring is full when the producer enqueues" a
// STATE the test establishes rather than an interval it hopes to win, and the
// block itself is OBSERVED (`producer still inside enqueue()`) rather than
// inferred from a duration. Thread-creation latency is now irrelevant: the
// drain cannot proceed while it is happening.
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

// A sink that parks emit() on the FIRST record until the test releases it
// (simulates slow I/O). Used to hold the drain thread inside emit() so the ring
// stays full for exactly as long as the test needs, with no interval to lose.
class TimedBlockSink final : public fixpp::log::Sink {
public:
    std::vector<fixpp::log::Record>  captured;
    std::atomic<int>                 emit_count{0};
    std::atomic<bool>                first_emit_started{false};
    std::atomic<bool>                release_first_emit{false};

    // ⚠️ A HANG GUARD, NOT PART OF THE MEASUREMENT. The test releases the drain
    // explicitly; this bound only stops a defect elsewhere from parking the
    // drain forever with no ctest timeout verdict to explain it. It is set far
    // above any interval the test waits, so a run that reaches it has already
    // failed for a different reason — `release_timed_out` says which.
    std::chrono::seconds             release_cap{10};
    std::atomic<bool>                release_timed_out{false};

    [[nodiscard]] fixpp::core::expected_t<void> open() override { return {}; }

    void emit(fixpp::log::Record const& rec) noexcept override {
        if (emit_count.fetch_add(1, std::memory_order_relaxed) == 0) {
            first_emit_started.store(true, std::memory_order_release);
            // Park until released. Sleeping rather than spinning: this runs on
            // the drain thread while the test holds a producer blocked, and a
            // yield-loop would burn a core of a 4-vCPU runner for the whole
            // observation window — on the very lane whose wall time is the
            // measurement.
            auto const deadline = std::chrono::steady_clock::now() + release_cap;
            while (!release_first_emit.load(std::memory_order_acquire)) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    release_timed_out.store(true, std::memory_order_release);
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
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
    // Arrange: capacity=1 ring + block mode + a first-emit sink that parks the
    // drain until this test releases it.
    auto* sink_raw = new TimedBlockSink{};

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
    std::atomic<bool> producer_at_the_door{false};

    std::thread producer([&]() {
        enqueue_start = std::chrono::steady_clock::now();
        producer_at_the_door.store(true, std::memory_order_release);
        // This enqueue MUST block until the drain advances read_sequence_.
        logger->enqueue(fixpp::log::Level::info,
                        fixpp::log::cat::session,
                        fmt_second,
                        zeroed_trace_id, 0u, ts,
                        {fixpp::log::ArgValue::from_u64(1u)});
        enqueue_end = std::chrono::steady_clock::now();
        producer_done.store(true, std::memory_order_release);
    });

    // Wait until the producer is about to enqueue. Unbounded-in-principle but
    // capped: this is thread creation, and the drain is parked until we release
    // it, so nothing is racing this wait — that is the whole point of the
    // redesign described in the header.
    {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        while (!producer_at_the_door.load(std::memory_order_acquire)
               && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }
    ASSERT_TRUE(producer_at_the_door.load(std::memory_order_acquire))
        << "Producer thread did not reach its enqueue within 5 seconds";

    // ── The observation this test exists to make ─────────────────────────────
    // The ring is still full (the drain is parked in emit() and has not
    // advanced read_sequence_), so a block-mode producer MUST still be inside
    // enqueue() after a real interval. This is measured directly rather than
    // inferred from a duration, and it is the VACUITY GUARD: if the slot had
    // come free, `producer_done` would already be true and this reads false.
    // ⚠️ THE ORDER BELOW IS THE ASSERTION. The verdict is captured BEFORE the
    // release, because the release is the teardown that would make it true:
    // once the drain returns from emit() the slot frees and the producer
    // finishes, so a `producer_done` read taken after it says nothing. Moving
    // the release above the capture leaves a test that passes unconditionally.
    constexpr auto kObserveBlocked = std::chrono::milliseconds{40};
    std::this_thread::sleep_for(kObserveBlocked);
    bool const still_blocked = !producer_done.load(std::memory_order_acquire);

    // Release the drain: emit() returns, read_sequence_ advances, the blocked
    // producer acquires the slot.
    sink_raw->release_first_emit.store(true, std::memory_order_release);

    // Wait for the producer thread to unblock and finish.
    producer.join();

    // Shut down the logger (drain the remaining record #1).
    (void)logger->shutdown(std::chrono::seconds{5});

    // ── Assertions ─────────────────────────────────────────────────────────────

    EXPECT_FALSE(sink_raw->release_timed_out.load(std::memory_order_acquire))
        << "The sink's hang guard expired — the drain was never released, so "
           "everything below describes a run that did not happen as designed";

    // The contract, observed rather than timed: the producer was STILL inside
    // enqueue() while the ring was full.
    EXPECT_TRUE(still_blocked)
        << "Producer returned from enqueue() while the ring was still full and "
           "the drain was still parked in emit() — block mode did not block. "
           "(If this fires together with a near-zero measured duration below, "
           "the ring was not actually full; that is a broken FIXTURE, not a "
           "broken contract.)";

    // The producer must have blocked for at least 10 ms.
    auto blocked_for = std::chrono::duration_cast<std::chrono::milliseconds>(
        enqueue_end - enqueue_start);

    EXPECT_GE(blocked_for.count(), 10)
        << "Producer thread must have blocked >= 10 ms while the ring was full "
           "(observation window=" << kObserveBlocked.count() << " ms, "
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
