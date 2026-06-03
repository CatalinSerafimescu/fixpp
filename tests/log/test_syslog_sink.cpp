// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/log/test_syslog_sink.cpp
//
// Minimal smoke test for SyslogSink (T029):
//   - Construct SyslogSink
//   - open() succeeds
//   - emit() a record at each level (no crash)
//   - close() cleans up
//
// POSIX-only: the entire test is #ifdef-guarded to match syslog_sink.hpp.
// On Windows the test compiles to an empty file with no tests registered.
//
// Anchors:
//   [2k §4.7]          — SyslogSink POSIX-only, level map
//   contracts/log-sinks.md §SyslogSink

#include <gtest/gtest.h>

#include <fixpp/log/syslog_sink.hpp>

#ifdef FIXPP_HAS_SYSLOG

#include <chrono>

#include <fixpp/log/level.hpp>
#include <fixpp/log/record.hpp>

namespace {

fixpp::log::Record make_record(fixpp::log::Level level, std::uint32_t fmt_id)
{
    fixpp::log::Record rec{};
    rec.level      = level;
    rec.category   = fixpp::log::cat::session;
    rec.format_id  = fmt_id;
    rec.arg_count  = 0u;
    rec.timestamp  = fixpp::core::utc_time_point{
        std::chrono::system_clock::now().time_since_epoch()};
    return rec;
}

}  // namespace

TEST(SyslogSinkTest, ConstructOpenEmitClose)
{
    fixpp::log::SyslogSinkConfig cfg;
    cfg.ident    = "fixpp_test";
    cfg.facility = LOG_LOCAL7;  // use LOCAL7 to avoid clobbering daemon log

    fixpp::log::SyslogSink sink{std::move(cfg)};

    // open() must succeed.
    auto result = sink.open();
    EXPECT_TRUE(result.has_value()) << "SyslogSink::open() must succeed on POSIX";

    // emit() at each level must not crash.
    constexpr auto fmt_id = static_cast<std::uint32_t>(
        fixpp::log::detail::crc32_str("test message"));

    sink.emit(make_record(fixpp::log::Level::trace, fmt_id));
    sink.emit(make_record(fixpp::log::Level::debug, fmt_id));
    sink.emit(make_record(fixpp::log::Level::info,  fmt_id));
    sink.emit(make_record(fixpp::log::Level::warn,  fmt_id));
    sink.emit(make_record(fixpp::log::Level::error, fmt_id));
    sink.emit(make_record(fixpp::log::Level::fatal, fmt_id));

    // flush() must not crash.
    sink.flush(std::chrono::milliseconds{100});

    // close() must not crash.
    sink.close();

    // Double-close must also not crash (idempotent).
    sink.close();
}

#else  // FIXPP_HAS_SYSLOG not defined (Windows)

TEST(SyslogSinkTest, NotAvailableOnPlatform)
{
    // On non-POSIX platforms, SyslogSink is not compiled.
    // This test documents the expected behaviour.
    SUCCEED() << "SyslogSink is POSIX-only; not compiled on this platform.";
}

#endif  // FIXPP_HAS_SYSLOG
