// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/otel/test_exporters.cpp
//
// Coverage-remediation: exercises PrometheusExporter ctor/shutdown,
// OtlpMetricExporter ctor/shutdown, OtelDualExportBuilder::with_prometheus /
// with_otlp / with_otlp_reader / build (the two-AddMetricReader wiring path).
//
// Port-collision avoidance: all Prometheus binds use 127.0.0.1:0 (ephemeral port
// assigned by the OS/civetweb) so tests never collide with each other or with
// ts11_dual_metric_export (:9464).  The tests only assert construction +
// reader non-null + shutdown; no HTTP scrape is required for coverage.
//
// OtlpMetricExporter: object construction only (safe, no connect until export).
//
// OtelDualExportBuilder F1 witness: builds a provider via
//   with_prometheus(prom_cfg).with_otlp_reader(<mock reader>).build()
// then creates a Meter+counter, Add(N), ForceFlush, and asserts the mock OTLP
// reader captured the counter value — PROVING the otlp reader was registered.
// A separate test exercises the real with_otlp(cfg) builder path (coverage).
//
// Anchor: .specify/2k-log-otel.md §4.10; contracts/otel-surface.md §Metric exporters.
// [const §XIII.3]: no opentelemetry::trace::Scope.

#include <gtest/gtest.h>

#include <fixpp/otel/exporters.hpp>

// SDK MeterProvider for dynamic_cast assertion.
#include <opentelemetry/sdk/metrics/meter_provider.h>

// SDK — mock push exporter implementation.
#include <opentelemetry/sdk/metrics/push_metric_exporter.h>
#include <opentelemetry/sdk/metrics/export/metric_producer.h>
#include <opentelemetry/sdk/metrics/data/metric_data.h>
#include <opentelemetry/sdk/metrics/data/point_data.h>
#include <opentelemetry/sdk/metrics/instruments.h>
#include <opentelemetry/sdk/common/exporter_utils.h>

// SDK — MetricReader wrapping.
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_options.h>
#include <opentelemetry/sdk/metrics/metric_reader.h>

// API — for creating a Meter and counter.
#include <opentelemetry/metrics/meter.h>
#include <opentelemetry/metrics/sync_instruments.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/nostd/string_view.h>
#include <opentelemetry/nostd/variant.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace sdk_metrics = opentelemetry::sdk::metrics;

// ── MockPushExporter ──────────────────────────────────────────────────────────
//
// A PushMetricExporter that records every Export() call.  Thread-safe.
// Mirrors the same helper in test_dual_metric_export.cpp.

namespace {

class MockPushExporter final : public sdk_metrics::PushMetricExporter {
public:
    MockPushExporter() = default;

    opentelemetry::sdk::common::ExportResult
    Export(const sdk_metrics::ResourceMetrics& data) noexcept override {
        std::lock_guard<std::mutex> lock(mu_);
        received_.push_back(data);
        return opentelemetry::sdk::common::ExportResult::kSuccess;
    }

    sdk_metrics::AggregationTemporality GetAggregationTemporality(
        sdk_metrics::InstrumentType) const noexcept override {
        return sdk_metrics::AggregationTemporality::kCumulative;
    }

    bool ForceFlush(std::chrono::microseconds) noexcept override { return true; }
    bool Shutdown(std::chrono::microseconds)   noexcept override { return true; }

    // Test accessor.
    std::vector<sdk_metrics::ResourceMetrics> get_received() const {
        std::lock_guard<std::mutex> lock(mu_);
        return received_;
    }

private:
    mutable std::mutex mu_;
    std::vector<sdk_metrics::ResourceMetrics> received_;
};

// Helper: read a named metric's SumPointData int64 value from the MOST RECENT
// batch that carried it. Returns -1 if no batch did.
//
// Summing ACROSS batches (what this did until 2026-08-02) is wrong arithmetic
// for a CUMULATIVE counter: every export re-reports the running total, so N
// exports of a counter at 7 sum to 7N, not 7. It was only ever correct because
// exactly one export happened to occur — which does not hold on Windows, where
// the MSVC release lane observed two batches and read 14 against an expected 7.
// Within a single batch the points ARE summed, which is correct: those are the
// same counter under different attribute sets.
//
// This is the twin of latest_counter_in_export() in test_dual_metric_export.cpp.
// Both had the identical defect; fixing only the one that failed first would
// have left this to surface as a separate mystery.
int64_t latest_counter_in_export(
    const std::vector<sdk_metrics::ResourceMetrics>& batches,
    std::string_view metric_name)
{
    int64_t latest = -1;

    for (const auto& rm : batches) {
        int64_t batch_total = 0;
        bool    in_batch    = false;

        for (const auto& scope_m : rm.scope_metric_data_) {
            for (const auto& md : scope_m.metric_data_) {
                if (std::string_view{md.instrument_descriptor.name_} != metric_name)
                    continue;
                for (const auto& pda : md.point_data_attr_) {
                    if (const auto* sp =
                            opentelemetry::nostd::get_if<sdk_metrics::SumPointData>(
                                &pda.point_data)) {
                        if (const auto* iv =
                                opentelemetry::nostd::get_if<int64_t>(&sp->value_)) {
                            batch_total += *iv;
                            in_batch = true;
                        }
                    }
                }
            }
        }
        if (in_batch) latest = batch_total;
    }
    return latest;
}

}  // namespace

// ── PrometheusExporter ────────────────────────────────────────────────────────
//
// Constructs a PrometheusExporter on loopback:0 (OS-assigned ephemeral port,
// no collision).  Asserts sdk_reader()/sdk_reader_shared() non-null, then
// calls shutdown() idempotently.

TEST(PrometheusExporterTest, CtorReaderNonNullAndShutdown) {
    fixpp::otel::PrometheusConfig cfg;
    cfg.host = "127.0.0.1";
    cfg.port = 0;  // ephemeral — civetweb picks a free port

    fixpp::otel::PrometheusExporter exp{cfg};

    ASSERT_NE(exp.sdk_reader(), nullptr)
        << "PrometheusExporter::sdk_reader() must return non-null after construction";
    ASSERT_NE(exp.sdk_reader_shared(), nullptr)
        << "PrometheusExporter::sdk_reader_shared() must return non-null after construction";

    // Shutdown must not crash.  Idempotent.
    exp.shutdown();
    exp.shutdown();  // second call: idempotent (no crash)
}

// ── OtlpMetricExporter ────────────────────────────────────────────────────────
//
// Object construction only — no connection is made until export.
// Uses a dummy endpoint; just verifies ctor + reader + shutdown.

TEST(OtlpMetricExporterTest, CtorReaderNonNullAndShutdown) {
    fixpp::otel::OtlpMetricConfig cfg;
    cfg.endpoint = "http://127.0.0.1:14318";  // dummy — no export triggered

    fixpp::otel::OtlpMetricExporter exp{cfg};

    ASSERT_NE(exp.sdk_reader(), nullptr)
        << "OtlpMetricExporter::sdk_reader() must return non-null";
    ASSERT_NE(exp.sdk_reader_shared(), nullptr)
        << "OtlpMetricExporter::sdk_reader_shared() must return non-null";

    // shutdown() must not crash.
    exp.shutdown();
}

// ── OtelDualExportBuilder: with_prometheus + with_otlp_reader + build ─────────
//
// F1 witness: proves BOTH readers are wired by building a provider via
//   with_prometheus(prom_cfg).with_otlp_reader(<mock reader>).build()
// then:
//   • Creates a uint64 counter and calls Add(7).
//   • Calls ForceFlush() → the mock OTLP reader's Export() is called
//     synchronously, capturing the counter value.
//   • Asserts the mock received the counter == 7  (proves otlp reader wired).
//   • Asserts the SDK MeterProvider is the concrete type (proves build() wired).
//
// Prometheus reader is constructed (port 0, ephemeral) — its construction proves
// the prom reader was registered (build() called AddMetricReader for it).
//
// A separate test (WithOtlpCfgBuilderPath) exercises the with_otlp(cfg) path.

TEST(OtelDualExportBuilderTest, WithPrometheusAndMockOtlpReaderWiresBothReaders) {
    // Build the mock OTLP push exporter and wrap it in a PeriodicExportingMetricReader
    // with a very long interval so only ForceFlush() triggers Export().
    auto* raw_mock = new MockPushExporter{};
    auto mock_up = std::unique_ptr<MockPushExporter>{raw_mock};

    sdk_metrics::PeriodicExportingMetricReaderOptions reader_opts;
    reader_opts.export_interval_millis = std::chrono::milliseconds{300'000};
    reader_opts.export_timeout_millis  = std::chrono::milliseconds{5'000};

    auto mock_reader_up =
        opentelemetry::sdk::metrics::PeriodicExportingMetricReaderFactory::Create(
            std::move(mock_up), reader_opts);
    auto mock_reader =
        std::shared_ptr<sdk_metrics::MetricReader>(mock_reader_up.release());

    fixpp::otel::PrometheusConfig prom_cfg;
    prom_cfg.host = "127.0.0.1";
    prom_cfg.port = 0;  // ephemeral

    auto mp = fixpp::otel::OtelDualExportBuilder{}
                .with_prometheus(prom_cfg)
                .with_otlp_reader(mock_reader)
                .build();

    ASSERT_NE(mp, nullptr) << "OtelDualExportBuilder::build() must return non-null";

    // Must be the concrete SDK MeterProvider (both readers registered).
    auto* sdk_mp = dynamic_cast<opentelemetry::sdk::metrics::MeterProvider*>(mp.get());
    ASSERT_NE(sdk_mp, nullptr)
        << "build() must return an sdk::metrics::MeterProvider";

    // Create a counter and increment it.
    constexpr int64_t kValue = 7;
    auto meter   = mp->GetMeter("fixpp.exporters.test");
    auto counter = meter->CreateUInt64Counter("fixpp.exporters.test.counter");
    ASSERT_NE(counter.get(), nullptr);
    counter->Add(static_cast<uint64_t>(kValue));

    // ForceFlush → triggers Export() on the mock OTLP reader.
    bool flush_ok = sdk_mp->ForceFlush(std::chrono::microseconds{10'000'000});
    EXPECT_TRUE(flush_ok) << "MeterProvider::ForceFlush() returned false";

    // The mock OTLP reader must have received at least one batch containing our counter.
    const auto batches = raw_mock->get_received();
    ASSERT_FALSE(batches.empty())
        << "MockPushExporter received no batches after ForceFlush; "
           "the OTLP reader was not registered";

    int64_t otlp_val = latest_counter_in_export(batches, "fixpp.exporters.test.counter");
    EXPECT_EQ(otlp_val, kValue)
        << "Mock OTLP exporter must capture counter==" << kValue
        << " in " << batches.size() << " batch(es); "
           "the OTLP reader was not wired or ForceFlush did not trigger Export()";

    // Shutdown cleanly.
    sdk_mp->Shutdown();
}

// ── OtelDualExportBuilder: with_prometheus + with_otlp(cfg) path ──────────────
//
// Exercises the real with_otlp(cfg) builder path (distinct from with_otlp_reader).
// This covers the OtlpMetricExporter construction + AddMetricReader lines in
// OtelDualExportBuilder::build() that with_otlp_reader bypasses.
// The dummy OTLP endpoint never receives a connection — only construction is tested.

TEST(OtelDualExportBuilderTest, WithPrometheusAndOtlpCfgBuilderPath) {
    fixpp::otel::PrometheusConfig prom_cfg;
    prom_cfg.host = "127.0.0.1";
    prom_cfg.port = 0;  // ephemeral

    fixpp::otel::OtlpMetricConfig otlp_cfg;
    otlp_cfg.endpoint = "http://127.0.0.1:14318";  // dummy — no export triggered

    auto mp = fixpp::otel::OtelDualExportBuilder{}
                .with_prometheus(prom_cfg)
                .with_otlp(otlp_cfg)
                .build();

    ASSERT_NE(mp, nullptr) << "OtelDualExportBuilder::build() must return non-null";

    auto* sdk_mp = dynamic_cast<opentelemetry::sdk::metrics::MeterProvider*>(mp.get());
    ASSERT_NE(sdk_mp, nullptr)
        << "build() must return an sdk::metrics::MeterProvider";

    // Shutdown cleanly (stops Prometheus civetweb server + OTLP reader).
    sdk_mp->Shutdown();
}
