// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/log/sink.hpp
//
// Sink interface (LOG-002): exactly 4 pure-virtual methods per [const §XIV.2].
// SinkConfig base, SinkFactory + FileSinkFactory/OtlpLogSinkFactory (FR-007).
//
// Anchors:
//   [2k §4.4] — Sink pure-virtual set (locked surface; 4-method cap)
//   [const §XIV.2] — ≤5 pure-virtual on plugin interfaces
//   data-model.md §Sink / §FileSink / §OtlpLogSink / §SyslogSink
//   contracts/log-sinks.md
//   [arch §2.3] — log → {core} only
//
// All Sink methods are called on the drain thread only (sole caller; no
// concurrent emit/flush/close). Configuration is constructor-injected;
// there is no open(SinkConfig) downcast.
#pragma once

#include <chrono>
#include <fixpp/core/error.hpp>
#include <fixpp/log/record.hpp>
#include <memory_resource>

namespace fixpp::log {

// ── SinkConfig ────────────────────────────────────────────────────────────────
//
// Base class for per-sink configuration. Concrete sink configs (FileSinkConfig,
// OtlpLogSinkConfig, SyslogSinkConfig) derive from this.
// Configuration is passed to each Sink at construction; Sink::open() receives
// no arguments (no downcast, no virtual dispatch on config).
struct SinkConfig {
    virtual ~SinkConfig() = default;
};

// ── Sink ──────────────────────────────────────────────────────────────────────
//
// The 4-method plugin interface for log record consumers.
// [const §XIV.2]: exactly 4 pure-virtual methods (≤5 cap; no justification
// paragraph needed per §XIV.2 since the count is ≤5).
//
// Lifecycle (drain thread only):
//   1. open()  — called once before any emit; failure disables this sink.
//   2. emit()  — called per record; must return fast; buffer if I/O.
//   3. flush() — called by Logger::shutdown / async_flush; deadline-bounded.
//   4. close() — called after flush on teardown; must not throw.
class Sink {
public:
    virtual ~Sink() = default;

    // open() — initialise the sink before the first emit.
    // Called once by the drain thread before any emit() call.
    // On failure: Logger disables this sink (no further calls) and records
    // log_sink_open_failed (core slot 123).
    // [const §XIV.2], contracts/log-sinks.md.
    [[nodiscard]] virtual fixpp::core::expected_t<void> open() = 0;

    // emit() — receive one log record on the drain thread.
    // Must not throw; must return fast; I/O sinks must buffer internally.
    // Exceptions must NOT propagate (drain wraps each call in try/catch).
    virtual void emit(Record const& rec) noexcept = 0;

    // flush() — bulk drain buffered records within the given deadline.
    // Mandatory deadline escape: must NOT stall indefinitely.
    // Called on the drain thread by Logger::shutdown / async_flush.
    // Must not throw (drain wraps in try/catch; failure → log_sink_flush_failed).
    virtual void flush(std::chrono::milliseconds deadline) noexcept = 0;

    // close() — teardown after flush; release resources.
    // Called once by the drain thread after flush on Logger teardown.
    // Must not throw.
    virtual void close() noexcept = 0;
};

// ── SinkFactory ───────────────────────────────────────────────────────────────
//
// Factory interface for creating Sinks from a SinkConfig.
// Logger callers build sinks via the appropriate factory and pass ownership
// to the Logger constructor; Logger itself never calls make().
//
// The memory_resource* is provided for implementations that want to allocate
// the Sink (and its internal buffers) from a PMR resource.
struct SinkFactory {
    virtual ~SinkFactory() = default;

    [[nodiscard]] virtual std::unique_ptr<Sink> make(std::pmr::memory_resource* resource,
                                                     SinkConfig const& config) = 0;
};

// ── Forward declarations ──────────────────────────────────────────────────────
//
// Concrete factory forward-declarations (FR-007).
// Definitions in include/fixpp/log/file_sink.hpp and
// include/fixpp/log/otlp_log_sink.hpp respectively (slice 3b/3c).
// Forward-declared here so callers that hold a FileSinkFactory* or
// OtlpLogSinkFactory* can include only this header.
struct FileSinkFactory;     // defined in <fixpp/log/file_sink.hpp>
struct OtlpLogSinkFactory;  // defined in <fixpp/log/otlp_log_sink.hpp>

}  // namespace fixpp::log
