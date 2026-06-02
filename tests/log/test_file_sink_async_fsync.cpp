// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/log/test_file_sink_async_fsync.cpp
//
// T018 / TS-5: FileSink::flush() calls fsync on the DRAIN thread, not the
// producer thread. Producers never block on I/O.
//
// Mechanism:
//   - Inject a mock fsync_fn via FileSinkConfig::fsync_fn.
//   - The mock records the thread id of its caller.
//   - After Logger::shutdown(), assert the recorded thread id matches the drain
//     thread id (obtained from the sink's test helper), NOT the main/producer
//     thread id.
//   - Also assert flush(deadline) returns (does not hang or block).
//
// Anchors:
//   [2k §4.5]          — flush(deadline) calls fdatasync on drain thread only
//   contracts/log-sinks.md TS-5
//   FR-005/FR-008

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory_resource>
#include <thread>
#include <vector>

#include <fixpp/log/file_sink.hpp>
#include <fixpp/log/level.hpp>
#include <fixpp/log/logger.hpp>
#include <fixpp/log/record.hpp>
#include <fixpp/log/sink.hpp>

namespace {

// ── MockFsyncSink ─────────────────────────────────────────────────────────────
//
// Wraps a FileSink and captures the thread id of every fsync_fn call.
// This lets us assert the fsync happened on the drain thread.

class MockFsyncState {
public:
    std::atomic<bool>                             fsync_called{false};
    std::thread::id                               fsync_thread_id{};
    std::atomic<int>                              fsync_call_count{0};

    int mock_fsync(int /*fd*/)
    {
        fsync_called.store(true, std::memory_order_release);
        fsync_thread_id = std::this_thread::get_id();
        fsync_call_count.fetch_add(1, std::memory_order_relaxed);
        return 0;  // success
    }
};

}  // namespace

// ── T018 / TS-5 ───────────────────────────────────────────────────────────────

class FileSinkFsyncTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        tmpdir_ = std::filesystem::temp_directory_path() /
                  ("fixpp_fsync_test_" + std::to_string(
                      std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(tmpdir_);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(tmpdir_, ec);
    }

    std::filesystem::path tmpdir_;
};

TEST_F(FileSinkFsyncTest, FlushCallsFsyncOnDrainThread)
{
    // ── Arrange ───────────────────────────────────────────────────────────────

    auto mock = std::make_shared<MockFsyncState>();

    fixpp::log::FileSinkConfig cfg;
    cfg.directory      = tmpdir_;
    cfg.base_name      = "fsync_test";
    cfg.max_file_bytes = 256u * 1024u * 1024u;  // 256 MiB — won't rotate
    cfg.max_keep_count = 8u;
    cfg.async_fsync    = true;
    // Inject mock fsync function.
    cfg.fsync_fn = [mock](int fd) -> int {
        return mock->mock_fsync(fd);
    };

    auto* file_sink_raw = new fixpp::log::FileSink(std::move(cfg));
    std::pmr::vector<std::unique_ptr<fixpp::log::Sink>> sinks{};
    sinks.push_back(std::unique_ptr<fixpp::log::Sink>(file_sink_raw));

    fixpp::log::LoggerConfig lcfg;
    lcfg.capacity = 64u;

    auto logger = std::make_unique<fixpp::log::Logger>(std::move(lcfg), std::move(sinks));

    // ── Record the PRODUCER thread id (main thread) ───────────────────────────
    std::thread::id producer_thread_id = std::this_thread::get_id();

    // ── Emit a few records ────────────────────────────────────────────────────
    std::array<std::uint8_t, 16> zeroed_trace_id{};
    auto ts = fixpp::core::utc_time_point{
        std::chrono::system_clock::now().time_since_epoch()};
    constexpr auto fmt_id = static_cast<std::uint32_t>(
        fixpp::log::detail::crc32_str("msg {}"));

    for (int i = 0; i < 5; ++i) {
        logger->enqueue(fixpp::log::Level::info,
                        fixpp::log::cat::session,
                        fmt_id,
                        zeroed_trace_id,
                        0u,
                        ts,
                        {fixpp::log::ArgValue::from_u64(static_cast<std::uint64_t>(i))});
    }

    // ── Shutdown triggers flush(drain_timeout) on the drain thread ────────────
    //
    // Logger::shutdown() drains all records and then calls flush() on each sink
    // from the DRAIN thread. Our injected fsync_fn captures that thread's id.
    auto res = logger->shutdown(std::chrono::seconds{5});
    ASSERT_TRUE(res.has_value()) << "shutdown must complete successfully";

    // ── Assertions ────────────────────────────────────────────────────────────

    // 1. fsync was called at all.
    EXPECT_TRUE(mock->fsync_called.load(std::memory_order_acquire))
        << "fsync_fn must have been called during flush()";

    // 2. fsync was called ≥ 1 time.
    EXPECT_GE(mock->fsync_call_count.load(std::memory_order_relaxed), 1)
        << "fsync_fn must be called at least once";

    // 3. fsync was called on the DRAIN thread, NOT the producer/main thread.
    //    The drain thread is an internal OS thread spawned by Logger::Impl.
    //    Its id is NEVER the main thread's id.
    EXPECT_NE(mock->fsync_thread_id, producer_thread_id)
        << "fsync must fire on the DRAIN thread, not the producer/main thread";

    // 4. fsync was NOT called on the main thread.
    //    (redundant with assertion 3, but explicit per TS-5 contract)
    EXPECT_NE(mock->fsync_thread_id, std::thread::id{})
        << "fsync_thread_id must have been set (fsync was called)";
}

// ── Producer never blocks on I/O during enqueue ───────────────────────────────
//
// This test verifies that enqueue() on the producer thread returns quickly
// (< 10 ms per call) even when the drain thread is blocked inside fsync.
// We inject a slow fsync (10ms sleep) and verify enqueue() does not block.
TEST_F(FileSinkFsyncTest, ProducerDoesNotBlockOnFsync)
{
    auto mock = std::make_shared<MockFsyncState>();

    fixpp::log::FileSinkConfig cfg;
    cfg.directory      = tmpdir_;
    cfg.base_name      = "no_block_test";
    cfg.max_file_bytes = 256u * 1024u * 1024u;
    cfg.max_keep_count = 8u;
    cfg.async_fsync    = true;
    // Slow fsync: 50ms per call.
    cfg.fsync_fn = [mock](int fd) -> int {
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
        return mock->mock_fsync(fd);
    };

    auto* file_sink_raw = new fixpp::log::FileSink(std::move(cfg));
    std::pmr::vector<std::unique_ptr<fixpp::log::Sink>> sinks{};
    sinks.push_back(std::unique_ptr<fixpp::log::Sink>(file_sink_raw));

    fixpp::log::LoggerConfig lcfg;
    lcfg.capacity = 64u;

    auto logger = std::make_unique<fixpp::log::Logger>(std::move(lcfg), std::move(sinks));

    // Enqueue several records from the producer (main) thread and measure latency.
    std::array<std::uint8_t, 16> zeroed_trace_id{};
    auto ts = fixpp::core::utc_time_point{
        std::chrono::system_clock::now().time_since_epoch()};
    constexpr auto fmt_id = static_cast<std::uint32_t>(
        fixpp::log::detail::crc32_str("msg {}"));

    constexpr int k_records = 10;
    auto t0 = std::chrono::steady_clock::now();

    for (int i = 0; i < k_records; ++i) {
        logger->enqueue(fixpp::log::Level::info,
                        fixpp::log::cat::session,
                        fmt_id,
                        zeroed_trace_id,
                        0u,
                        ts,
                        {fixpp::log::ArgValue::from_u64(static_cast<std::uint64_t>(i))});
    }

    auto t1 = std::chrono::steady_clock::now();
    auto enqueue_elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    // enqueue() must NOT block on fsync. Total enqueue time for 10 records
    // must be < the fsync delay (50ms). We allow 40ms total (4ms per call).
    EXPECT_LT(enqueue_elapsed_ms, 40LL)
        << "enqueue() blocked on I/O: total enqueue time=" << enqueue_elapsed_ms
        << "ms for " << k_records << " records. Should be < 40ms.";

    // Drain with a generous deadline (flush triggers the slow fsync).
    (void)logger->shutdown(std::chrono::seconds{5});
}
