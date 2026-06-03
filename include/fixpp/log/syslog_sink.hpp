// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/log/syslog_sink.hpp
//
// SyslogSink — POSIX syslog(3) sink (FR-008, LOG-002, T029).
// POSIX-only: the entire class is #ifdef-guarded so a Windows build does not
// see any syslog symbols (Windows has no syslog.h).
//
// Anchors:
//   [2k §4.7]          — SyslogSink config + level map
//   contracts/log-sinks.md §SyslogSink
//   [arch §2.3]        — log → {core} only
//   [const §XIV.2]     — Sink has exactly 4 pure-virtual (inherited; 0 added here)
//
// Level map (FR-008):
//   trace/debug → LOG_DEBUG
//   info        → LOG_INFO
//   warn        → LOG_WARNING
//   error       → LOG_ERR
//   fatal       → LOG_CRIT
//
// Windows behaviour:
//   On Windows, SyslogSink and SyslogSinkConfig are not compiled.
//   A SyslogSinkFactory is not provided either. Code that wants to conditionally
//   construct a SyslogSink must guard with #if defined(FIXPP_HAS_SYSLOG).
#pragma once

// FIXPP_HAS_SYSLOG is defined on any POSIX platform that has <syslog.h>.
#if defined(__unix__) || defined(__APPLE__) || defined(__linux__)
#define FIXPP_HAS_SYSLOG 1
#endif

#ifdef FIXPP_HAS_SYSLOG

#include <syslog.h>

#include <fixpp/log/sink.hpp>
#include <string>

namespace fixpp::log {

// ── SyslogSinkConfig ──────────────────────────────────────────────────────────

struct SyslogSinkConfig : SinkConfig {
    // ident passed to openlog(3). The pointed-to string must remain valid for
    // the lifetime of the SyslogSink (typically a string literal or a
    // std::string member whose lifetime outlives the sink).
    std::string ident{"fixpp"};

    // syslog facility (e.g. LOG_DAEMON, LOG_LOCAL0..LOG_LOCAL7).
    // Default: LOG_DAEMON.
    int facility{LOG_DAEMON};
};

// ── SyslogSink ────────────────────────────────────────────────────────────────

class SyslogSink final : public Sink {
public:
    explicit SyslogSink(SyslogSinkConfig config);
    ~SyslogSink() override;

    // Non-copyable, non-movable.
    SyslogSink(SyslogSink const&) = delete;
    SyslogSink& operator=(SyslogSink const&) = delete;
    SyslogSink(SyslogSink&&) = delete;
    SyslogSink& operator=(SyslogSink&&) = delete;

    // ── Sink interface ────────────────────────────────────────────────────────

    // open() — calls openlog(3) to initialise the syslog connection.
    [[nodiscard]] fixpp::core::expected_t<void> open() override;

    // emit() — calls syslog(3) with the mapped priority + formatted body.
    // Must not throw; must return fast.
    void emit(Record const& rec) noexcept override;

    // flush() — no-op for syslog (kernel handles buffering).
    void flush(std::chrono::milliseconds deadline) noexcept override;

    // close() — calls closelog(3).
    void close() noexcept override;

private:
    SyslogSinkConfig config_;
    bool open_{false};
};

// ── SyslogSinkFactory ─────────────────────────────────────────────────────────

struct SyslogSinkFactory final : SinkFactory {
    [[nodiscard]] std::unique_ptr<Sink> make(std::pmr::memory_resource* /*resource*/,
                                             SinkConfig const& config) override {
        return std::make_unique<SyslogSink>(static_cast<SyslogSinkConfig const&>(config));
    }
};

}  // namespace fixpp::log

#endif  // FIXPP_HAS_SYSLOG
