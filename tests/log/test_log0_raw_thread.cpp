// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/log/test_log0_raw_thread.cpp
//
// T032 / TS-7 — FIXPP_LOG0 from a context-free raw std::thread:
//   - trace_id and span_id are all-zeros (no context available).
//   - No UB: the macro executes safely with no session/engine context.
//
// ASan obligation: ASan would rebuild OTel SDK (out of scope here).
// Run under DEBUG preset; ASan deferral noted per task brief.
//
// Anchors:
//   [2k §4.3]  — FIXPP_LOG0 zero-context Tier 3 macro
//   [2k §6.4]  — zeroed trace_id is documented expected behavior, not a bug
//   FR-013 / TS-7
//   contracts/log-core.md LOG-003

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory_resource>
#include <mutex>
#include <thread>

#include "support/wait_until.hpp"
#include <vector>

#include <fixpp/log/level.hpp>
#include <fixpp/log/logger.hpp>
#include <fixpp/log/record.hpp>
#include <fixpp/log/sink.hpp>

namespace {

using fixpp::log::ArgValue;
using fixpp::log::Level;
using fixpp::log::Logger;
using fixpp::log::LoggerConfig;
using fixpp::log::Record;

// ── CaptureSink ───────────────────────────────────────────────────────────────

class CaptureSink final : public fixpp::log::Sink {
public:
    [[nodiscard]] fixpp::core::expected_t<void> open() override { return {}; }

    void emit(Record const& rec) noexcept override {
        std::lock_guard lk{mu_};
        records_.push_back(rec);
        count_.fetch_add(1, std::memory_order_release);
    }

    void flush(std::chrono::milliseconds) noexcept override {}
    void close() noexcept override {}

    [[nodiscard]] std::size_t count() const noexcept {
        return count_.load(std::memory_order_acquire);
    }

    [[nodiscard]] Record get(std::size_t idx) const {
        std::lock_guard lk{mu_};
        return records_.at(idx);
    }

private:
    mutable std::mutex mu_;
    std::vector<Record> records_;
    std::atomic<std::size_t> count_{0};
};

// ── Test ──────────────────────────────────────────────────────────────────────

TEST(Log0RawThread, ZeroedTraceNoUB) {
    // Construct logger with a CaptureSink.
    auto  sink = std::make_unique<CaptureSink>();
    auto* sink_ptr = sink.get();

    LoggerConfig cfg;
    cfg.capacity = 1024u;
    std::pmr::vector<std::unique_ptr<fixpp::log::Sink>> sinks(
        std::pmr::get_default_resource());
    sinks.push_back(std::move(sink));
    Logger logger(std::move(cfg), std::move(sinks));

    // Emit FIXPP_LOG0 from a raw std::thread with NO session or engine context.
    // Must complete without UB, assert, or crash.
    // ASan obligation DEFERRED to Phase-6 verify (rebuilding OTel under ASan is
    // out of scope for this slice; documented per task brief T032).
    std::thread raw_thread{[&logger]() {
        FIXPP_LOG0(&logger, info, fixpp::log::cat::session,
                   "from raw thread {}", ArgValue::from_u64(42u));
    }};
    raw_thread.join();

    // Wait for the drain thread to deliver the record (max 2 s).
    // Result discarded: the ASSERT_GE below is the oracle.
    (void)fixpp::test_support::wait_until_observed([&sink_ptr] { return sink_ptr->count() >= 1u; }, std::chrono::seconds{2});
    ASSERT_GE(sink_ptr->count(), 1u) << "FIXPP_LOG0 record not delivered within 2 s";

    auto rec = sink_ptr->get(0);

    // trace_id must be all-zeros (no context).
    // Record::trace_id is std::array<std::uint8_t,16> (not std::byte).
    std::array<std::uint8_t, 16> zero_trace{};
    EXPECT_EQ(rec.trace_id, zero_trace)
        << "FIXPP_LOG0 from a raw thread must produce all-zero trace_id";

    // span_id must be zero.
    EXPECT_EQ(rec.span_id, std::uint64_t{0})
        << "FIXPP_LOG0 from a raw thread must produce zero span_id";

    // Shutdown gracefully.
    auto res = logger.shutdown(std::chrono::seconds{5});
    EXPECT_TRUE(res.has_value()) << "Logger::shutdown must complete without timeout";
}

}  // namespace
