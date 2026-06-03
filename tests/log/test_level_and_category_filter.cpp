// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/log/test_level_and_category_filter.cpp
//
// TS-8: compile-time level cutoff + runtime category filter combine.
// Contract (contracts/log-core.md, FR-010/011, SC-003):
//   - A record below the compile-time level cutoff is never enqueued.
//   - A record in a disabled category is dropped before enqueue and counted
//     in filter_count(), NOT drop_count().
//   - Sink gets exactly 1 record (the one that passes both filters).
//   - drop_count() == 0 (no overflow drops).
//   - filter_count() == 1 (exactly one category-filtered record).
//
// Test setup:
//   - We emit two records:
//       A) level = debug  (below FIXPP_LOG_MIN_LEVEL=2/info in debug builds
//          where FIXPP_LOG_MIN_LEVEL defaults to 0=trace, so debug IS emitted;
//          we use FIXPP_LOG_MIN_LEVEL which is 0 in debug — all levels pass
//          the compile-time gate).
//   - Instead, test the RUNTIME category filter:
//       A) cat::session (enabled  by default) → reaches sink.
//       B) cat::wire    (disabled at runtime) → filtered, filter_count++.
//   - We explicitly disable cat::wire before emitting.
//   - Result: sink.size()==1 (record A), drop_count()==0, filter_count()==1.
//
// Anchors:
//   [2k §4.3] / contracts/log-core.md FR-010/011 / TS-8

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

// ── CaptureSink ─────────────────────────────────────────────────────────────
//
// A simple Sink that captures all records passed to it.
class CaptureSink final : public fixpp::log::Sink {
public:
    std::vector<fixpp::log::Record> records;

    [[nodiscard]] fixpp::core::expected_t<void> open() override { return {}; }

    void emit(fixpp::log::Record const& rec) noexcept override {
        records.push_back(rec);
    }

    void flush(std::chrono::milliseconds) noexcept override {}
    void close() noexcept override {}
};

}  // namespace

// ── T019 / TS-8 ─────────────────────────────────────────────────────────────

TEST(LogFilter, LevelAndCategoryFilterCombine)
{
    auto* capture_raw = new CaptureSink{};
    std::pmr::vector<std::unique_ptr<fixpp::log::Sink>> sinks{};
    sinks.push_back(std::unique_ptr<fixpp::log::Sink>(capture_raw));

    fixpp::log::LoggerConfig cfg;
    cfg.capacity    = 64u;  // small ring, large enough for this test
    cfg.on_overflow = fixpp::log::overflow_policy::drop_newest;

    auto logger = std::make_unique<fixpp::log::Logger>(std::move(cfg), std::move(sinks));

    // Disable cat::wire at runtime.
    logger->set_category_enabled(fixpp::log::cat::wire, false);

    // Verify the filter state.
    EXPECT_TRUE(logger->is_category_enabled(fixpp::log::cat::session));
    EXPECT_FALSE(logger->is_category_enabled(fixpp::log::cat::wire));

    std::array<std::uint8_t, 16> zeroed_trace_id{};
    auto ts = fixpp::core::utc_time_point{
        std::chrono::system_clock::now().time_since_epoch()};

    constexpr auto fmt_session =
        static_cast<std::uint32_t>(fixpp::log::detail::crc32_str("level filter test"));
    constexpr auto fmt_wire =
        static_cast<std::uint32_t>(fixpp::log::detail::crc32_str("category filter test"));

    // Record A: cat::session, level=info → should reach the sink.
    logger->enqueue(fixpp::log::Level::info,
                    fixpp::log::cat::session,
                    fmt_session,
                    zeroed_trace_id,
                    0u,
                    ts,
                    {});

    // Record B: cat::wire (disabled) → should be filtered; filter_count++.
    logger->enqueue(fixpp::log::Level::info,
                    fixpp::log::cat::wire,
                    fmt_wire,
                    zeroed_trace_id,
                    0u,
                    ts,
                    {});

    // Shut down the logger so all in-flight records are processed.
    (void)logger->shutdown(std::chrono::seconds{5});

    // ── Assertions ───────────────────────────────────────────────────────────

    // Exactly 1 record reached the sink.
    EXPECT_EQ(capture_raw->records.size(), 1u)
        << "Exactly 1 record should reach the sink (record A via cat::session)";

    // No overflow drops.
    EXPECT_EQ(logger->drop_count(), 0u)
        << "drop_count() must be 0 — no ring overflow occurred";

    // Exactly 1 filtered record (record B via cat::wire).
    EXPECT_EQ(logger->filter_count(), 1u)
        << "filter_count() must be 1 — exactly one record was category-filtered";

    // The record that reached the sink is record A (fmt_session).
    ASSERT_GE(capture_raw->records.size(), 1u);
    EXPECT_EQ(capture_raw->records[0].format_id, fmt_session)
        << "The record that reached the sink must be record A (cat::session)";

    EXPECT_EQ(capture_raw->records[0].category, fixpp::log::cat::session)
        << "Captured record must have category == cat::session";

    // filter_count and drop_count are SEPARATE (contracts/log-core.md).
    EXPECT_EQ(logger->timeout_drop_count(), 0u)
        << "timeout_drop_count() must be 0";
}

// ── Verify category re-enable works ─────────────────────────────────────────

TEST(LogFilter, CategoryReEnable)
{
    auto* capture_raw = new CaptureSink{};
    std::pmr::vector<std::unique_ptr<fixpp::log::Sink>> sinks{};
    sinks.push_back(std::unique_ptr<fixpp::log::Sink>(capture_raw));

    fixpp::log::LoggerConfig cfg;
    cfg.capacity = 64u;

    auto logger = std::make_unique<fixpp::log::Logger>(std::move(cfg), std::move(sinks));

    // Disable cat::tls.
    logger->set_category_enabled(fixpp::log::cat::tls, false);
    EXPECT_FALSE(logger->is_category_enabled(fixpp::log::cat::tls));

    std::array<std::uint8_t, 16> zeroed_trace_id{};
    auto ts = fixpp::core::utc_time_point{
        std::chrono::system_clock::now().time_since_epoch()};

    constexpr auto fmt_id =
        static_cast<std::uint32_t>(fixpp::log::detail::crc32_str("msg {}"));

    // Enqueue with cat::tls disabled → filtered.
    logger->enqueue(fixpp::log::Level::info, fixpp::log::cat::tls,
                    fmt_id, zeroed_trace_id, 0u, ts,
                    {fixpp::log::ArgValue::from_u64(1u)});

    // Re-enable cat::tls.
    logger->set_category_enabled(fixpp::log::cat::tls, true);
    EXPECT_TRUE(logger->is_category_enabled(fixpp::log::cat::tls));

    // Enqueue again with cat::tls enabled → should reach sink.
    logger->enqueue(fixpp::log::Level::info, fixpp::log::cat::tls,
                    fmt_id, zeroed_trace_id, 0u, ts,
                    {fixpp::log::ArgValue::from_u64(2u)});

    (void)logger->shutdown(std::chrono::seconds{5});

    // 1 filtered, 1 captured.
    EXPECT_EQ(logger->filter_count(), 1u);
    EXPECT_EQ(capture_raw->records.size(), 1u);
    EXPECT_EQ(logger->drop_count(), 0u);
}
