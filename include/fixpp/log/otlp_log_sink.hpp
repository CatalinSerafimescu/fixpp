// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/log/otlp_log_sink.hpp
//
// OtlpLogSink — the OTLP log-export Sink implementation (T043 / OBS-003).
// Compiled in a SEPARATE target `fixpp_log_otlp` that links fixpp_log + the
// OTel SDK — base fixpp_log MUST stay OTel-free.
//
// Anchors:
//   [2k §4.6]  — OtlpLogSink surface (locked)
//   contracts/log-sinks.md §OtlpLogSink
//   [const §XIII.4] — single write path, no double-write
//   [const §XIV.2] — 4 pure-virtual Sink methods
//   [arch §2.3]   — log: {core} only; OTel SDK is external (not fixpp::otel)
//
// ARCHITECTURE NOTE:
//   OtlpLogSink MUST NOT #include <fixpp/otel/...>.
//   It accesses the OTel SDK headers directly + fixpp/log/sink.hpp.
//   This preserves the `log → {core}` layer: the fixpp_log_otlp target adds the
//   OTel SDK dependency on top of fixpp_log; fixpp_log itself has no OTel link.
#pragma once

#include <chrono>
#include <fixpp/log/sink.hpp>  // Sink, SinkConfig, SinkFactory
#include <memory>
#include <string>

namespace fixpp::log {

// ── OtlpLogSinkConfig ─────────────────────────────────────────────────────────
//
// Configuration for OtlpLogSink (FR-008 / FR-018 / OBS-003).
// [2k §4.6] / contracts/log-sinks.md §OtlpLogSink.
struct OtlpLogSinkConfig : SinkConfig {
    // OTLP HTTP endpoint (e.g. "http://localhost:4318/v1/logs").
    // Empty → noop exporter (records buffered + silently dropped on flush).
    std::string endpoint;

    // gRPC OTLP transport deferred to Phase 5; always false in v1.0.
    bool use_grpc{false};

    // Optional PEM CA-cert path for TLS. Empty → plain HTTP.
    std::string cert_source;

    // Per-export RPC timeout.
    std::chrono::seconds export_timeout{10};

    // BatchLogRecordProcessor: maximum export batch size.
    std::size_t max_export_batch{512};

    // Maximum export retry attempts before giving up and recording
    // otel_export_failed (core slot 127).
    std::size_t max_export_retries{3};

    // ── Test-seam injection ───────────────────────────────────────────────────
    // When set, OtlpLogSink::open() uses this exporter instead of the real
    // OtlpHttpLogRecordExporter.  Null in production (zero-cost).
    // Lifetime: the sink holds a raw-pointer borrow — the pointed-to object
    // must outlive the sink.  Tests inject a mock that counts Export() calls.
    //
    // The shared_ptr<void> stores ownership of a sdk::logs::LogRecordExporter
    // derived object. The raw pointer is cast in .cpp (OTel SDK kept off this
    // header to preserve the log → {core} layer boundary).
    std::shared_ptr<void> test_exporter;  // cast to LogRecordExporter* in .cpp
};

// ── OtlpLogSink ───────────────────────────────────────────────────────────────
//
// Sink that exports log records via OTLP to an OTel collector / backend.
//
// Internal design (non-blocking, single write path):
//   open()  — construct the BatchLogRecordProcessor + logger provider.
//   emit()  — MakeRecordable() + fill fields + OnEmit(): enqueues into the
//             processor's internal ring — returns immediately (non-blocking on
//             the drain thread as required by [const §VIII.5]).
//   flush() — ForceFlush(deadline): waits up to `deadline` for the batch
//             processor to export all pending records.
//   close() — Shutdown() the processor.
//
// [const §XIII.4]: single write path — each Record is translated and handed to
//   the BatchLogRecordProcessor ONCE; no double-write.
//
// Retry policy: the BatchLogRecordProcessor retries at the exporter level.
//   On give-up (after max_export_retries), otel_export_failed is incremented.
//   No retry storm: the exporter reports kFailure; the processor drops the
//   batch; processing continues.
class OtlpLogSink final : public Sink {
public:
    explicit OtlpLogSink(OtlpLogSinkConfig cfg);
    ~OtlpLogSink() override;

    OtlpLogSink(const OtlpLogSink&) = delete;
    OtlpLogSink& operator=(const OtlpLogSink&) = delete;
    OtlpLogSink(OtlpLogSink&&) = delete;
    OtlpLogSink& operator=(OtlpLogSink&&) = delete;

    // ── Sink interface (4 pure-virtual) ───────────────────────────────────────

    [[nodiscard]] fixpp::core::expected_t<void> open() override;

    // Translates Record → sdk::logs::Recordable and hands it to the
    // BatchLogRecordProcessor via OnEmit(). Non-blocking; always returns fast.
    // Single write path — each record flows through exactly once.
    void emit(Record const& rec) noexcept override;

    // ForceFlush the batch processor within `deadline`.
    void flush(std::chrono::milliseconds deadline) noexcept override;

    void close() noexcept override;

    // ── Export-failure accounting ─────────────────────────────────────────────

    // Returns the number of times the exporter reported kFailure, causing
    // otel_export_failed to be counted.  Used by the E10 negative test.
    [[nodiscard]] std::uint64_t export_failure_count() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    OtlpLogSinkConfig cfg_;
};

// ── OtlpLogSinkFactory ────────────────────────────────────────────────────────
//
// Creates OtlpLogSink from an OtlpLogSinkConfig (FR-007).
struct OtlpLogSinkFactory final : SinkFactory {
    [[nodiscard]] std::unique_ptr<Sink> make(std::pmr::memory_resource* /*resource*/,
                                             SinkConfig const& config) override;
};

}  // namespace fixpp::log
