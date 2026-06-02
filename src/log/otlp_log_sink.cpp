// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/log/otlp_log_sink.cpp
//
// OtlpLogSink implementation (T043 / OBS-003).
// Compiled into target `fixpp_log_otlp` — NOT fixpp_log — so the OTel SDK
// link stays out of the base log library (architecture invariant).
//
// Anchors:
//   [2k §4.6]  — OtlpLogSink surface
//   contracts/log-sinks.md §OtlpLogSink
//   [const §XIII.4] — single write path (emit → OnEmit once per record)
//   [arch §2.3]   — log → {core}; OTel SDK is external, not fixpp::otel
//
// NOTE: This file must NOT include <fixpp/otel/...>. It uses OTel SDK headers
// directly so that fixpp_log_otlp → OTel SDK, not fixpp_log → OTel SDK.

#include <fixpp/log/otlp_log_sink.hpp>

// OTel SDK headers — direct, no fixpp::otel layer
#include <opentelemetry/exporters/otlp/otlp_http_log_record_exporter.h>
#include <opentelemetry/exporters/otlp/otlp_http_log_record_exporter_options.h>
#include <opentelemetry/exporters/otlp/otlp_http_log_record_exporter_factory.h>
#include <opentelemetry/sdk/logs/batch_log_record_processor.h>
#include <opentelemetry/sdk/logs/batch_log_record_processor_factory.h>
#include <opentelemetry/sdk/logs/batch_log_record_processor_options.h>
#include <opentelemetry/sdk/logs/read_write_log_record.h>
#include <opentelemetry/sdk/logs/exporter.h>
#include <opentelemetry/sdk/logs/processor.h>
#include <opentelemetry/logs/severity.h>
#include <opentelemetry/trace/trace_id.h>
#include <opentelemetry/trace/span_id.h>
#include <opentelemetry/trace/trace_flags.h>
#include <opentelemetry/common/timestamp.h>
#include <opentelemetry/nostd/span.h>

#include <fixpp/core/error.hpp>
#include <fixpp/log/record.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace fixpp::log {

// ── Level → OTel Severity mapping ────────────────────────────────────────────

namespace {

opentelemetry::logs::Severity level_to_otel(Level lvl) noexcept {
    switch (lvl) {
        case Level::trace: return opentelemetry::logs::Severity::kTrace;
        case Level::debug: return opentelemetry::logs::Severity::kDebug;
        case Level::info:  return opentelemetry::logs::Severity::kInfo;
        case Level::warn:  return opentelemetry::logs::Severity::kWarn;
        case Level::error: return opentelemetry::logs::Severity::kError;
        case Level::fatal: return opentelemetry::logs::Severity::kFatal;
    }
    return opentelemetry::logs::Severity::kInfo;
}

// A non-owning LogRecordExporter wrapper that tracks Export() failures.
// Used when OtlpLogSinkConfig::test_exporter is set: the test owns the
// exporter via shared_ptr<void>; the sink borrows via a raw ptr wrapped here.
//
// This wrapper counts each Export() call that returns kFailure. The sink's
// flush() reads export_failure_count() after ForceFlush to detect failures
// that occurred during the background worker's export cycle.
class BorrowedExporter final : public opentelemetry::sdk::logs::LogRecordExporter {
public:
    explicit BorrowedExporter(opentelemetry::sdk::logs::LogRecordExporter* inner) noexcept
        : inner_(inner) {}

    std::unique_ptr<opentelemetry::sdk::logs::Recordable>
    MakeRecordable() noexcept override {
        return inner_->MakeRecordable();
    }

    opentelemetry::sdk::common::ExportResult Export(
        const opentelemetry::nostd::span<
            std::unique_ptr<opentelemetry::sdk::logs::Recordable>>& batch) noexcept override {
        auto result = inner_->Export(batch);
        if (result != opentelemetry::sdk::common::ExportResult::kSuccess) {
            export_failures_.fetch_add(1, std::memory_order_relaxed);
        }
        return result;
    }

    bool ForceFlush(std::chrono::microseconds t) noexcept override {
        return inner_->ForceFlush(t);
    }

    bool Shutdown(std::chrono::microseconds t) noexcept override {
        // Do NOT forward Shutdown to the borrowed exporter — the test owns it.
        (void)t;
        return true;
    }

    [[nodiscard]] std::uint64_t export_failure_count() const noexcept {
        return export_failures_.load(std::memory_order_relaxed);
    }

private:
    opentelemetry::sdk::logs::LogRecordExporter* inner_;  // non-owning borrow
    std::atomic<std::uint64_t> export_failures_{0};
};

}  // namespace

// ── OtlpLogSink::Impl ─────────────────────────────────────────────────────────

struct OtlpLogSink::Impl {
    // Owned processor (BatchLogRecordProcessor wrapping the exporter).
    std::unique_ptr<opentelemetry::sdk::logs::LogRecordProcessor> processor;

    // Non-owning pointer to the BorrowedExporter wrapper (non-null only in
    // test-seam mode). The processor owns the BorrowedExporter; this ptr is
    // stored here so flush() can query export_failure_count() after ForceFlush.
    BorrowedExporter* borrowed_exporter{nullptr};

    // Export-failure counter (incremented when all flush retries exhausted, OR
    // when the borrowed_exporter detected export failures during flush).
    std::atomic<std::uint64_t> export_failures{0};

    // Config copy (for flush deadline conversion).
    std::size_t max_export_retries{3};
    std::size_t max_export_batch{512};
    std::chrono::seconds export_timeout{10};
};

// ── ctor / dtor ───────────────────────────────────────────────────────────────

OtlpLogSink::OtlpLogSink(OtlpLogSinkConfig cfg)
    : impl_(std::make_unique<Impl>())
    , cfg_(std::move(cfg))
{
    impl_->max_export_retries = cfg_.max_export_retries;
    impl_->max_export_batch   = cfg_.max_export_batch;
    impl_->export_timeout     = cfg_.export_timeout;
}

OtlpLogSink::~OtlpLogSink() {
    // close() is a no-op if already called; call defensively on destruction.
    close();
}

// ── open() ────────────────────────────────────────────────────────────────────
//
// Called once by the drain thread before any emit().
// Constructs the BatchLogRecordProcessor (+ real or injected exporter).
//
// Architecture: the processor is owned here; the LoggerProvider and SDK Logger
// are NOT needed — OtlpLogSink drives the processor directly via
//   auto rec = processor_->MakeRecordable();  // fill → OnEmit()
// This is the single write path ([const §XIII.4]).

[[nodiscard]] fixpp::core::expected_t<void> OtlpLogSink::open() {
    if (impl_->processor != nullptr) {
        // Already opened (idempotent).
        return {};
    }

    try {
        std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter> exporter;

        if (cfg_.test_exporter) {
            // Test-seam: cfg_.test_exporter is a shared_ptr<void> that holds
            // a LogRecordExporter*-aliased shared_ptr.  We borrow the raw pointer
            // via a BorrowedExporter wrapper (non-owning; the test's shared_ptr
            // keeps the real exporter alive for the sink's entire lifetime).
            auto* raw = static_cast<opentelemetry::sdk::logs::LogRecordExporter*>(
                cfg_.test_exporter.get());
            auto borrowed = std::make_unique<BorrowedExporter>(raw);
            // Keep a raw ptr so flush() can read failure counts (the processor
            // takes ownership of the unique_ptr below).
            impl_->borrowed_exporter = borrowed.get();
            exporter = std::move(borrowed);
        } else {
            // Production path: construct the real OTLP HTTP log exporter.
            opentelemetry::exporter::otlp::OtlpHttpLogRecordExporterOptions opts{nullptr};
            opts.url = cfg_.endpoint.empty()
                        ? "http://localhost:4318/v1/logs"
                        : cfg_.endpoint;
            opts.timeout = cfg_.export_timeout;
            if (!cfg_.cert_source.empty()) {
                opts.ssl_ca_cert_path = cfg_.cert_source;
            }
            exporter =
                opentelemetry::exporter::otlp::OtlpHttpLogRecordExporterFactory::Create(opts);
        }

        // BatchLogRecordProcessor options.
        opentelemetry::sdk::logs::BatchLogRecordProcessorOptions batch_opts;
        batch_opts.max_export_batch_size = cfg_.max_export_batch;
        // scheduled_delay: 1s periodic background flush (non-blocking emit).
        batch_opts.schedule_delay_millis = std::chrono::milliseconds{1000};
        // max_queue_size should be ≥ max_export_batch_size.
        batch_opts.max_queue_size = std::max(cfg_.max_export_batch * 4, std::size_t{2048});

        impl_->processor =
            opentelemetry::sdk::logs::BatchLogRecordProcessorFactory::Create(
                std::move(exporter), batch_opts);

        return {};
    } catch (...) {
        return std::unexpected(fixpp::core::error::log_sink_open_failed);
    }
}

// ── emit() ────────────────────────────────────────────────────────────────────
//
// Translates fixpp::log::Record → sdk::logs::Recordable and calls OnEmit().
// Single write path ([const §XIII.4]) — each record flows through exactly once.
// Non-blocking: OnEmit() enqueues into the processor's bounded internal ring
// and returns immediately; the background worker thread handles Export.
//
// Called exclusively on the drain thread (Sink contract).

void OtlpLogSink::emit(Record const& rec) noexcept {
    if (impl_->processor == nullptr) return;

    try {
        // Allocate a recordable from the processor.
        auto recordable = impl_->processor->MakeRecordable();
        if (!recordable) return;

        auto* log_rec = dynamic_cast<opentelemetry::sdk::logs::ReadWriteLogRecord*>(
            recordable.get());
        if (!log_rec) {
            // Processor returned an unexpected recordable type; forward as-is.
            impl_->processor->OnEmit(std::move(recordable));
            return;
        }

        // timestamp → SystemTimestamp (system_clock duration)
        opentelemetry::common::SystemTimestamp otel_ts{
            std::chrono::duration_cast<std::chrono::system_clock::duration>(
                rec.timestamp.time_since_epoch())};
        log_rec->SetTimestamp(otel_ts);
        log_rec->SetObservedTimestamp(otel_ts);

        // level → Severity
        log_rec->SetSeverity(level_to_otel(rec.level));

        // trace_id → TraceId (16 bytes via nostd::span)
        opentelemetry::trace::TraceId trace_id{
            opentelemetry::nostd::span<const uint8_t, 16>{rec.trace_id.data(), 16}};
        log_rec->SetTraceId(trace_id);

        // span_id → SpanId (8 bytes, big-endian)
        std::array<uint8_t, 8> span_bytes{};
        auto sid = rec.span_id;
        for (int i = 7; i >= 0; --i) {
            span_bytes[static_cast<std::size_t>(i)] = static_cast<uint8_t>(sid & 0xFFu);
            sid >>= 8u;
        }
        opentelemetry::trace::SpanId span_id{
            opentelemetry::nostd::span<const uint8_t, 8>{span_bytes.data(), 8}};
        log_rec->SetSpanId(span_id);

        // trace flags: sampled when span_id is non-zero
        opentelemetry::trace::TraceFlags flags{
            rec.span_id != 0u
                ? opentelemetry::trace::TraceFlags::kIsSampled
                : static_cast<uint8_t>(0u)};
        log_rec->SetTraceFlags(flags);

        // body: format_id as int64 (the drain-side format registry can map it back)
        log_rec->SetBody(static_cast<std::int64_t>(rec.format_id));

        // category → attribute "fixpp.log.category"
        log_rec->SetAttribute("fixpp.log.category",
                              static_cast<std::int64_t>(rec.category));

        // Single write path: OnEmit enqueues the recordable into the processor.
        impl_->processor->OnEmit(std::move(recordable));

    } catch (...) {
        // Must not propagate (Sink::emit is noexcept). Swallow silently.
    }
}

// ── flush() ───────────────────────────────────────────────────────────────────
//
// Calls ForceFlush on the processor within `deadline`.
// Retries up to max_export_retries on failure; records otel_export_failed on
// give-up (bounded retries, no storm).
//
// Called on the drain thread by Logger::shutdown / async_flush.

void OtlpLogSink::flush(std::chrono::milliseconds deadline) noexcept {
    if (impl_->processor == nullptr) return;

    try {
        auto timeout_us =
            std::chrono::duration_cast<std::chrono::microseconds>(deadline);

        // Snapshot failure count before flush so we can detect new failures.
        std::uint64_t failures_before = impl_->borrowed_exporter
            ? impl_->borrowed_exporter->export_failure_count()
            : 0u;

        for (std::size_t attempt = 0; attempt < impl_->max_export_retries; ++attempt) {
            bool ok = impl_->processor->ForceFlush(timeout_us);

            // Check the exporter-level failure count after ForceFlush completes.
            // BatchLogRecordProcessor::ForceFlush returns true even when Export()
            // returns kFailure — check at the exporter level for accurate detection.
            if (impl_->borrowed_exporter) {
                std::uint64_t failures_after =
                    impl_->borrowed_exporter->export_failure_count();
                if (failures_after > failures_before) {
                    // Export failed — count this as an otel_export_failed event
                    // and stop retrying (the failed batch is gone from the queue;
                    // retrying flush on an empty queue would succeed vacuously).
                    impl_->export_failures.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
            }

            if (ok) {
                return;  // Success: ForceFlush completed + no new export failures.
            }
            // ForceFlush itself returned false (timeout or processor error).
        }
        // All retries exhausted — record the failure.
        impl_->export_failures.fetch_add(1, std::memory_order_relaxed);
    } catch (...) {
        // Must not propagate.
    }
}

// ── close() ───────────────────────────────────────────────────────────────────

void OtlpLogSink::close() noexcept {
    if (impl_->processor == nullptr) return;
    try {
        impl_->processor->Shutdown();
    } catch (...) {}
    impl_->processor.reset();
}

// ── export_failure_count() ────────────────────────────────────────────────────

std::uint64_t OtlpLogSink::export_failure_count() const noexcept {
    return impl_->export_failures.load(std::memory_order_relaxed);
}

// ── OtlpLogSinkFactory ────────────────────────────────────────────────────────

std::unique_ptr<Sink> OtlpLogSinkFactory::make(
    std::pmr::memory_resource* /*resource*/,
    SinkConfig const&          config)
{
    const auto& cfg = dynamic_cast<OtlpLogSinkConfig const&>(config);
    return std::make_unique<OtlpLogSink>(cfg);
}

}  // namespace fixpp::log
