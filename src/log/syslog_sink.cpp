// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/log/syslog_sink.cpp
//
// SyslogSink implementation — POSIX syslog(3) (T029).
// Compiled only on POSIX platforms where FIXPP_HAS_SYSLOG is defined.
//
// Anchors:
//   [2k §4.7]          — SyslogSink level map + POSIX-only guard
//   contracts/log-sinks.md §SyslogSink
//   [arch §2.3]        — log → {core} only

#include <fixpp/log/syslog_sink.hpp>

#ifdef FIXPP_HAS_SYSLOG

#include <fixpp/log/format_registry.hpp>

namespace fixpp::log {

SyslogSink::SyslogSink(SyslogSinkConfig config) : config_{std::move(config)} {}

SyslogSink::~SyslogSink() { close(); }

fixpp::core::expected_t<void> SyslogSink::open() {
    // openlog(3): sets up the syslog connection.
    // ident must be a stable C-string; SyslogSinkConfig::ident is a std::string
    // whose lifetime equals the SyslogSink (constructed before open(), alive
    // until close()/dtor). c_str() is valid for the whole lifetime.
    ::openlog(config_.ident.c_str(), LOG_PID | LOG_NDELAY, config_.facility);
    open_ = true;
    return {};
}

void SyslogSink::emit(Record const& rec) noexcept {
    if (!open_) return;

    // Map fixpp::log::Level → syslog priority.
    // trace and debug both map to LOG_DEBUG by design (syslog has no TRACE level).
    int priority = LOG_DEBUG;
    // NOLINTBEGIN(bugprone-branch-clone) — intentional identical trace/debug arms
    switch (rec.level) {
        case Level::trace:
            priority = LOG_DEBUG;
            break;
        case Level::debug:
            priority = LOG_DEBUG;
            break;
        case Level::info:
            priority = LOG_INFO;
            break;
        case Level::warn:
            priority = LOG_WARNING;
            break;
        case Level::error:
            priority = LOG_ERR;
            break;
        case Level::fatal:
            priority = LOG_CRIT;
            break;
    }
    // NOLINTEND(bugprone-branch-clone)

    // Format the body on the drain thread using the registry.
    // Exceptions are swallowed: emit() must not propagate.
    try {
        std::string body = detail::format_record(rec);
        ::syslog(priority, "%s", body.c_str());
    } catch (...) {
        ::syslog(priority, "[fixpp log format error]");
    }
}

void SyslogSink::flush(std::chrono::milliseconds /*deadline*/) noexcept {
    // syslog(3) is handled by the syslog daemon; no client-side flush needed.
}

void SyslogSink::close() noexcept {
    if (open_) {
        ::closelog();
        open_ = false;
    }
}

}  // namespace fixpp::log

#endif  // FIXPP_HAS_SYSLOG
