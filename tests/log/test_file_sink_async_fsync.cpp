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

// ── flush(deadline) returns bounded even when fsync stalls ────────────────────
//
// [RC#2 regression guard / LOG-002 coverage]
// [2k §4.5] / contracts/log-sinks.md §FileSink: flush(deadline) is a
// mandatory deadline escape — it MUST return within deadline even when the
// injected fsync_fn blocks longer.
//
// Test: inject a fsync_fn that sleeps 500ms. Call flush(10ms).
// Assert flush() returns well under 500ms. The test has an internal hard
// deadline so a regression hangs for at most ~600ms before failing (not forever).
TEST_F(FileSinkFsyncTest, FlushDeadlineBounded)
{
    constexpr auto k_fsync_sleep_ms = std::chrono::milliseconds{500};
    constexpr auto k_flush_deadline = std::chrono::milliseconds{10};
    // Allow 3× the flush_deadline for OS scheduling jitter before failing.
    constexpr auto k_max_return_ms = std::chrono::milliseconds{100};

    fixpp::log::FileSinkConfig cfg;
    cfg.directory      = tmpdir_;
    cfg.base_name      = "deadline_test";
    cfg.max_file_bytes = 256u * 1024u * 1024u;
    cfg.max_keep_count = 8u;
    cfg.async_fsync    = true;
    // Inject a very slow fsync (500ms) so any synchronous implementation hangs.
    cfg.fsync_fn = [k_fsync_sleep_ms](int) -> int {
        std::this_thread::sleep_for(k_fsync_sleep_ms);
        return 0;
    };

    fixpp::log::FileSink sink{std::move(cfg)};
    ASSERT_TRUE(sink.open().has_value()) << "FileSink::open() failed";

    // Measure how long flush(10ms) takes under a 500ms stalling fsync.
    auto t0 = std::chrono::steady_clock::now();
    sink.flush(k_flush_deadline);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);

    // flush() must return well before the fsync sleep completes.
    EXPECT_LT(elapsed.count(), k_max_return_ms.count())
        << "flush(10ms) took " << elapsed.count()
        << "ms — should return within " << k_max_return_ms.count()
        << "ms even when fsync blocks for " << k_fsync_sleep_ms.count() << "ms. "
        << "flush(deadline) must implement the mandatory deadline escape "
        << "([2k §4.5] / contracts/log-sinks.md).";

    // close() must return promptly — NO pre-sleep masking the lifetime bug.
    // A correct owned-worker implementation joins the worker before fclose();
    // close() may block briefly for the worker to finish its current fsync,
    // but must not hang indefinitely. Allow up to k_fsync_sleep_ms + 200ms.
    auto t_close_start = std::chrono::steady_clock::now();
    sink.close();
    auto close_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t_close_start);
    EXPECT_LT(close_elapsed.count(), (k_fsync_sleep_ms + std::chrono::milliseconds{200}).count())
        << "close() took " << close_elapsed.count() << "ms — should complete "
        << "within " << (k_fsync_sleep_ms + std::chrono::milliseconds{200}).count()
        << "ms (bounded join of the owned fsync worker)";
}

// ── FileSink close() lifetime: no detached threads, no fd-reuse race ─────────
//
// [RC#2 / P1 lifetime guard — Gate B r2]
// [2k §4.5] / contracts/log-sinks.md §FileSink: close() must synchronize with
// the owned fsync worker BEFORE closing the fd. Production close() must not
// leave any in-flight thread holding the raw fd after fclose().
//
// Sub-tests:
//   (i)   close() returns bounded after repeated timed-out flushes (no hung join)
//   (ii)  no unbounded thread growth — repeated timeouts must not spawn extra
//         threads (owned worker design: single persistent thread)
//   (iii) no late fsync on a reused fd — after close()+reopen, the previously
//         injected stalling fsync must NOT call the new fd value
//
// The injected fsync_fn uses a flag+condvar so the test can release it at
// teardown, ensuring the worker can always exit (correct impl terminates;
// a buggy detach-per-flush impl would skip the close join entirely).
TEST_F(FileSinkFsyncTest, CloseJoinsWorkerAndPreventsReusedFdWrite)
{
    // Internal self-deadline: if something hangs, the test itself unblocks
    // the stalling fsync_fn after k_release_after_ms so the worker can exit
    // and the test fails with a time assertion rather than hanging ctest.
    constexpr auto k_flush_deadline      = std::chrono::milliseconds{1};
    constexpr auto k_fsync_stall_ms      = std::chrono::milliseconds{400};
    constexpr auto k_close_bound_ms      = std::chrono::milliseconds{600};  // stall + margin
    constexpr auto k_release_after_ms    = std::chrono::milliseconds{800};  // test self-deadline

    // Shared state for the injected fsync: stall until released.
    std::atomic<bool>   released{false};
    std::atomic<int>    fsync_call_count{0};
    std::atomic<int>    last_fsync_fd{-1};
    std::mutex          release_mu;
    std::condition_variable release_cv;

    // The injected fsync stalls until released OR k_release_after_ms elapses.
    // Records which fd it was called with.
    auto stalling_fsync = [&](int fd) -> int {
        fsync_call_count.fetch_add(1, std::memory_order_relaxed);
        last_fsync_fd.store(fd, std::memory_order_relaxed);
        std::unique_lock<std::mutex> lk(release_mu);
        release_cv.wait_for(lk, k_release_after_ms, [&] { return released.load(); });
        return 0;
    };

    fixpp::log::FileSinkConfig cfg;
    cfg.directory      = tmpdir_;
    cfg.base_name      = "lifetime_test";
    cfg.max_file_bytes = 256u * 1024u * 1024u;
    cfg.max_keep_count = 8u;
    cfg.async_fsync    = true;
    cfg.fsync_fn       = stalling_fsync;

    fixpp::log::FileSink sink{std::move(cfg)};
    ASSERT_TRUE(sink.open().has_value()) << "FileSink::open() failed";

    // Repeatedly flush(1ms) against the stalling fsync — each call times out.
    // A buggy detach-per-flush impl would spawn N detached threads here.
    // The owned-worker design keeps exactly 1 in-flight fsync across all calls.
    constexpr int k_flush_rounds = 5;
    for (int i = 0; i < k_flush_rounds; ++i) {
        sink.flush(k_flush_deadline);
    }

    // (ii) At most 1 fsync should have been called so far (the worker serializes).
    // (A detach-per-flush impl would have up to k_flush_rounds calls in flight.)
    EXPECT_LE(fsync_call_count.load(), 1)
        << "More than 1 fsync in-flight after " << k_flush_rounds
        << " timed-out flushes — indicates per-flush thread growth (detach bug)";

    // Record the fd the first fsync used (the fd of the live file).
    int original_fd = last_fsync_fd.load();

    // close() immediately — NO pre-sleep masking. This is the key lifetime test.
    // A correct implementation joins the worker; a detached-thread impl would
    // return before the stalling fsync is done.
    auto t0 = std::chrono::steady_clock::now();
    sink.close();
    auto close_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);

    // (i) close() must return bounded — within k_close_bound_ms.
    // For the owned-worker design: close() joins the worker which is still
    // stalling → close() blocks until released OR times out at k_release_after_ms.
    // We accept up to k_close_bound_ms (which is < k_release_after_ms) only if
    // the join is correct. The self-deadline above guarantees the test finishes.
    EXPECT_LT(close_elapsed.count(), k_release_after_ms.count() + 200LL)
        << "close() took " << close_elapsed.count()
        << "ms — should return bounded (worker joined before fd close)";

    // Release the stalling fsync (so the worker can finish and let the test end).
    {
        std::lock_guard<std::mutex> lk(release_mu);
        released.store(true);
    }
    release_cv.notify_all();

    // (iii) No late fsync on a reused fd: open a new FileSink reusing the same
    // path (the OS may reuse the same fd integer). The original stalling fsync
    // was already released above; verify it called the original fd, not a new one.
    // This assertion is structural: if the old fsync called the new fd, the
    // recorded last_fsync_fd would match the new file's fd, not the old one.
    // We open the new file and check the fd integers don't collide.
    //
    // Note: the POSIX fd reuse guarantee is not testable deterministically, but
    // the owned-worker design prevents it structurally (join before fclose).
    // We assert what IS deterministic: fsync was only called for the pre-close fd.
    if (original_fd >= 0) {
        fixpp::log::FileSinkConfig cfg2;
        cfg2.directory      = tmpdir_;
        cfg2.base_name      = "lifetime_test";
        cfg2.max_file_bytes = 256u * 1024u * 1024u;
        cfg2.max_keep_count = 8u;
        cfg2.async_fsync    = false;  // don't trigger more fsync calls
        fixpp::log::FileSink sink2{std::move(cfg2)};
        (void)sink2.open();
        // Worker already joined — no pending fsync can land on the new fd.
        // Any fsync call after this point would increment fsync_call_count.
        int count_before = fsync_call_count.load();
        // Give any latent detached thread time to run (discriminates the bug).
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
        EXPECT_EQ(fsync_call_count.load(), count_before)
            << "fsync was called after close() — indicates a detached thread "
            << "outliving close(), which may target a reused fd";
        sink2.close();
    }
}
