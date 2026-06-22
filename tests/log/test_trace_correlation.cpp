// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/log/test_trace_correlation.cpp
//
// T031 / TS-6 — LOG-003 three-macro trace correlation:
//   (a) FIXPP_SLOG carries the session trace_id / span_id from get_trace_context().
//   (b) FIXPP_ELOG carries the engine root trace_id / span_id from engine_trace_context().
//   (c) FIXPP_LOG0 produces all-zeros trace_id / zero span_id.
//   (d) [E1 remediation] With a mock clock injected via EngineConfig::clock,
//       a delivered record's timestamp equals mock.now() — exercising the
//       FR-006 effective-clock routing (FIXPP_ELOG reads engine.clock()->now()).
//
// Anchors:
//   [2k §4.3]  — LOG-003 three-macro contract
//   [2k §6.4]  — three-tier trace acquisition
//   contracts/log-core.md LOG-003
//   contracts/adjacent-amendments.md §1/§2
//   T031 brief / FR-012/013 / SC-004

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory_resource>
#include <mutex>
#include <thread>
#include <vector>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/core/trace_context.hpp>
#include <fixpp/log/level.hpp>
#include <fixpp/log/logger.hpp>
#include <fixpp/log/record.hpp>
#include <fixpp/log/sink.hpp>
#include <fixpp/session/engine.hpp>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

namespace {

using fixpp::log::ArgValue;
using fixpp::log::Level;
using fixpp::log::Logger;
using fixpp::log::LoggerConfig;
using fixpp::log::Record;
using fixpp::log::cat::session;

// ── CaptureSink: collects emitted records in order ────────────────────────────

class CaptureSink final : public fixpp::log::Sink {
public:
    [[nodiscard]] fixpp::core::expected_t<void> open() override { return {}; }

    void emit(Record const& rec) noexcept override {
        std::lock_guard lk{mu_};
        records_.push_back(rec);
    }

    void flush(std::chrono::milliseconds) noexcept override {}
    void close() noexcept override {}

    // Copy-out the captured records (under lock).
    [[nodiscard]] std::vector<Record> drain() {
        std::lock_guard lk{mu_};
        auto out = records_;
        records_.clear();
        return out;
    }

    [[nodiscard]] std::size_t count() const {
        std::lock_guard lk{mu_};
        return records_.size();
    }

private:
    mutable std::mutex mu_;
    std::vector<Record> records_;
};

// ── Helpers ───────────────────────────────────────────────────────────────────

// Build a minimal Logger with a CaptureSink.
// Returns {Logger, raw-CaptureSink*}.
struct LoggerBundle {
    CaptureSink* sink_ptr = nullptr;  // non-owning; owned by Logger
    std::unique_ptr<Logger> logger;
};

LoggerBundle make_logger(std::uint32_t capacity = 4096) {
    LoggerBundle b;
    auto  sink = std::make_unique<CaptureSink>();
    b.sink_ptr = sink.get();

    LoggerConfig cfg;
    cfg.capacity = capacity;

    std::pmr::vector<std::unique_ptr<fixpp::log::Sink>> sinks(
        std::pmr::get_default_resource());
    sinks.push_back(std::move(sink));

    b.logger = std::make_unique<Logger>(std::move(cfg), std::move(sinks));
    return b;
}

// Wait (busy-poll max 2 s) for at least `n` records in the sink.
bool wait_for_records(CaptureSink* sink, std::size_t n,
                      std::chrono::milliseconds timeout = std::chrono::seconds{2}) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (sink->count() >= n) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    return false;
}

// Helper: convert std::array<std::byte,16> to std::array<std::uint8_t,16>
// for comparison with Record::trace_id (same bit pattern, different element type).
static std::array<std::uint8_t, 16> to_u8(std::array<std::byte, 16> const& b) {
    return reinterpret_cast<std::array<std::uint8_t, 16> const&>(b);
}

// ── (a) FIXPP_SLOG carries the session trace_id / span_id ────────────────────

TEST(TraceCorrelation, SlogCarriesSessionTrace) {
    auto [sink_ptr, logger] = make_logger();

    // Build a trace_context with known trace_id and span_id.
    fixpp::otel::trace_context tc{};
    tc.trace_id.fill(std::byte{0xAA});
    // span_id: std::array<std::byte,8> — fill with 0xBB.
    tc.span_id.fill(std::byte{0xBB});
    const std::uint64_t expected_span_id =
        std::bit_cast<std::uint64_t>(tc.span_id);

    // (a) Emit via FIXPP_SLOG — caller passes tc explicitly, no co_await.
    FIXPP_SLOG(logger.get(), info, tc, fixpp::log::cat::session,
               "slog test {}", ArgValue::from_u64(1u));

    // Wait for the drain thread to deliver the record.
    ASSERT_TRUE(wait_for_records(sink_ptr, 1)) << "record not delivered within 2 s";

    auto records = sink_ptr->drain();
    ASSERT_EQ(records.size(), 1u);

    // Assert trace_id == 0xAA...
    // Record::trace_id is std::array<std::uint8_t,16>; convert for comparison.
    EXPECT_EQ(records[0].trace_id, to_u8(tc.trace_id))
        << "FIXPP_SLOG must carry the session trace_id into Record::trace_id";

    // Assert span_id == bit_cast of 0xBB...BB.
    EXPECT_EQ(records[0].span_id, expected_span_id)
        << "FIXPP_SLOG must carry the session span_id into Record::span_id";
}

// ── (b) FIXPP_ELOG carries the engine root trace_id / span_id ────────────────

TEST(TraceCorrelation, ElogCarriesEngineTrace) {
    auto [sink_ptr, logger] = make_logger();

    // Build an Engine with a known engine_trace_context.
    asio::io_context ioc;
    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();
    eng_cfg.clock    = std::make_shared<fixpp::core::mock_clock>(
        fixpp::core::utc_time_point{}, fixpp::core::steady_time_point{},
        ioc.get_executor());

    eng_cfg.engine_trace_context.trace_id.fill(std::byte{0xCC});
    eng_cfg.engine_trace_context.span_id.fill(std::byte{0xDD});
    const std::uint64_t expected_span_id =
        std::bit_cast<std::uint64_t>(eng_cfg.engine_trace_context.span_id);

    fixpp::session::Engine engine{ioc.get_executor(), std::move(eng_cfg)};

    // (b) Emit via FIXPP_ELOG — reads engine.engine_trace_context() internally.
    FIXPP_ELOG(logger.get(), info, engine, fixpp::log::cat::control,
               "elog test {}", ArgValue::from_u64(2u));

    ASSERT_TRUE(wait_for_records(sink_ptr, 1)) << "record not delivered within 2 s";

    auto records = sink_ptr->drain();
    ASSERT_EQ(records.size(), 1u);

    // Record::trace_id is std::array<std::uint8_t,16>; convert std::byte for cmp.
    std::array<std::byte, 16> expected_trace_bytes;
    expected_trace_bytes.fill(std::byte{0xCC});
    EXPECT_EQ(records[0].trace_id, to_u8(expected_trace_bytes))
        << "FIXPP_ELOG must carry the engine trace_id into Record::trace_id";

    EXPECT_EQ(records[0].span_id, expected_span_id)
        << "FIXPP_ELOG must carry the engine span_id into Record::span_id";

    // Engine teardown.
    auto stop_future = asio::co_spawn(ioc, engine.stop(), asio::use_future);
    ioc.run();
    stop_future.get();
}

// ── (c) FIXPP_LOG0 all-zeros ─────────────────────────────────────────────────

TEST(TraceCorrelation, Log0AllZeros) {
    auto [sink_ptr, logger] = make_logger();

    // (c) FIXPP_LOG0 — context-free; trace_id and span_id must be all-zeros.
    FIXPP_LOG0(logger.get(), info, fixpp::log::cat::session,
               "log0 test {}", ArgValue::from_u64(3u));

    ASSERT_TRUE(wait_for_records(sink_ptr, 1)) << "record not delivered within 2 s";

    auto records = sink_ptr->drain();
    ASSERT_EQ(records.size(), 1u);

    // trace_id: all-zeros (Record::trace_id is std::array<std::uint8_t,16>)
    std::array<std::uint8_t, 16> zero_trace{};
    EXPECT_EQ(records[0].trace_id, zero_trace)
        << "FIXPP_LOG0 must produce all-zero trace_id";

    // span_id: zero (Record::span_id is uint64_t)
    EXPECT_EQ(records[0].span_id, std::uint64_t{0})
        << "FIXPP_LOG0 must produce zero span_id";
}

// ── (d-slog) [RC#3] SLOG timestamp is wall-clock (system_clock::now()) ───────
//
// FR-006 effective-clock scope clarification (option b): FIXPP_SLOG uses
// system_clock::now() for its timestamp because trace_context carries no clock.
// This test asserts the DOCUMENTED behavior: SLOG produces a wall-clock timestamp
// reasonably close to system_clock::now(), NOT a zero or mock-clock timestamp.
// See contracts/log-core.md LOG-003 macro contract + L-017-6.

TEST(TraceCorrelation, SlogTimestampIsWallClock) {
    auto [sink_ptr, logger] = make_logger();

    // Record wall-clock bounds before and after the SLOG call.
    auto before = fixpp::core::utc_time_point{
        std::chrono::system_clock::now().time_since_epoch()};

    fixpp::otel::trace_context tc{};
    tc.trace_id.fill(std::byte{0x11});
    tc.span_id.fill(std::byte{0x22});

    FIXPP_SLOG(logger.get(), info, tc, fixpp::log::cat::session,
               "test message");

    auto after = fixpp::core::utc_time_point{
        std::chrono::system_clock::now().time_since_epoch()};

    ASSERT_TRUE(wait_for_records(sink_ptr, 1)) << "record not delivered within 2 s";

    auto records = sink_ptr->drain();
    ASSERT_EQ(records.size(), 1u);

    // SLOG timestamp must be a real wall-clock time (between before and after),
    // not zero and not a future/past sentinel.
    // This asserts the documented behavior: SLOG is wall-clock, not mock-clock.
    EXPECT_GE(records[0].timestamp.time_since_epoch().count(),
              before.time_since_epoch().count())
        << "SLOG timestamp must be >= system_clock::now() captured before the call";

    EXPECT_LE(records[0].timestamp.time_since_epoch().count(),
              after.time_since_epoch().count())
        << "SLOG timestamp must be <= system_clock::now() captured after the call";

    // Confirm the timestamp is non-zero (it is a real wall-clock sample).
    EXPECT_NE(records[0].timestamp.time_since_epoch().count(), 0)
        << "SLOG timestamp must not be zero (must be a real system_clock sample)";
}

// ── (d) [E1 remediation] Mock clock timestamp via FIXPP_ELOG ─────────────────
//
// Inject a mock clock via EngineConfig::clock. Call FIXPP_ELOG.
// Assert the delivered record's timestamp equals mock.now().
// This exercises the FR-006 effective-clock routing: FIXPP_ELOG reads
// engine.clock()->now() for the record timestamp.

TEST(TraceCorrelation, ElogTimestampFromMockClock) {
    auto [sink_ptr, logger] = make_logger();

    // Build mock clock with a known initial UTC time (100 hours from epoch —
    // distinct enough from system_clock::now() that no system_clock call could
    // accidentally match).
    using namespace std::chrono_literals;
    auto const known_utc = fixpp::core::utc_time_point{
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::hours{100})};

    asio::io_context ioc;
    auto mock_clk = std::make_shared<fixpp::core::mock_clock>(
        known_utc, fixpp::core::steady_time_point{}, ioc.get_executor());

    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();
    eng_cfg.clock    = mock_clk;

    fixpp::session::Engine engine{ioc.get_executor(), std::move(eng_cfg)};

    // Record mock.now() BEFORE the emit (deterministic — mock clock doesn't advance).
    auto const expected_ts = mock_clk->now();

    FIXPP_ELOG(logger.get(), info, engine, fixpp::log::cat::control,
               "clock test {}", ArgValue::from_u64(4u));

    ASSERT_TRUE(wait_for_records(sink_ptr, 1)) << "record not delivered within 2 s";

    auto records = sink_ptr->drain();
    ASSERT_EQ(records.size(), 1u);

    // The record timestamp must equal mock.now() — not system_clock::now().
    // [E1 remediation]: FIXPP_ELOG reads engine.clock()->now() for the timestamp.
    EXPECT_EQ(records[0].timestamp, expected_ts)
        << "FIXPP_ELOG must use the engine's injected clock (FR-006 routing). "
           "expected (mock.now())=" << expected_ts.time_since_epoch().count()
        << " got=" << records[0].timestamp.time_since_epoch().count()
        << " (if got != expected, FIXPP_ELOG is using system_clock instead)";

    // Engine teardown.
    auto stop_future = asio::co_spawn(ioc, engine.stop(), asio::use_future);
    ioc.run();
    stop_future.get();
}

}  // namespace
