// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/otel/exporters.hpp
//
// Thin RAII wrappers over the OpenTelemetry C++ SDK metric exporters
// (FR-017 / OBS-002 / TS-11).
//
// PrometheusExporter   — wraps sdk::metrics::MetricReader (pull; the SDK's
//                        embedded single-threaded non-asio HTTP server on
//                        the configured port, exposing /metrics).
// OtlpMetricExporter  — wraps PushMetricExporter inside a
//                        PeriodicExportingMetricReader.
// OtelDualExportBuilder — fluent builder that wires BOTH readers onto one
//                         sdk::metrics::MeterProvider via two AddMetricReader()
//                         calls (no MultiMetricExporter; incompatible base
//                         types, R1).
//
// Anchor: .specify/2k-log-otel.md §4.10; contracts/otel-surface.md §Metric
//         exporters + dual export.
// [const §XIII.3]: no opentelemetry::trace::Scope anywhere in this module.
#pragma once

#include <chrono>
#include <memory>
#include <string>

// MetricReader is the base type for both Prometheus and OTLP readers.
// Include the lightweight SDK MetricReader header (no SDK internals in .hpp).
#include <opentelemetry/sdk/metrics/metric_reader.h>
// MeterProvider forward declaration via the SDK header.
// (Used only in build()'s return type — the full definition lives in .cpp.)
#include <opentelemetry/sdk/metrics/meter_provider.h>

namespace fixpp::otel {

// ── PrometheusConfig ──────────────────────────────────────────────────────────
//
// Configuration for the Prometheus pull endpoint.
// The SDK PrometheusExporter starts an embedded civetweb HTTP server bound to
// `host:port` and exposes `metrics_path` — no asio involvement.

struct PrometheusConfig {
    std::string host = "0.0.0.0";
    uint16_t port = 9464;
    std::string metrics_path = "/metrics";
    // cert_source is intentionally omitted in v1.0 (TLS on prom endpoint deferred).
};

// ── OtlpMetricConfig ──────────────────────────────────────────────────────────

struct OtlpMetricConfig {
    std::string endpoint = "http://localhost:4318";
    std::chrono::milliseconds export_interval = std::chrono::milliseconds{60000};
    std::chrono::milliseconds export_timeout = std::chrono::milliseconds{30000};
};

// ── PrometheusExporter ────────────────────────────────────────────────────────
//
// Thin RAII owner of the SDK PrometheusExporter (which IS a MetricReader).
// The embedded HTTP server starts when the reader is added to a MeterProvider
// and is shut down on shutdown() / destruction.
//
//   sdk_reader() — borrowed pointer to the MetricReader; register via
//                  sdk::metrics::MeterProvider::AddMetricReader().
//
// Lifetime: sdk_reader() is valid for the lifetime of *this.

class PrometheusExporter {
public:
    explicit PrometheusExporter(const PrometheusConfig& cfg);
    ~PrometheusExporter();

    PrometheusExporter(const PrometheusExporter&) = delete;
    PrometheusExporter& operator=(const PrometheusExporter&) = delete;
    PrometheusExporter(PrometheusExporter&&) noexcept;
    PrometheusExporter& operator=(PrometheusExporter&&) noexcept;

    // Returns the underlying MetricReader.  Lifetime bound to *this.
    [[nodiscard]] opentelemetry::sdk::metrics::MetricReader* sdk_reader() const noexcept
        [[clang::lifetimebound]];

    // Returns shared ownership (safe to pass to AddMetricReader).
    [[nodiscard]] std::shared_ptr<opentelemetry::sdk::metrics::MetricReader> sdk_reader_shared()
        const noexcept;

    void shutdown();

private:
    // unique_ptr to avoid pulling heavy SDK headers into users of this header.
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ── OtlpMetricExporter ────────────────────────────────────────────────────────
//
// Thin RAII owner of an OtlpHttpMetricExporter wrapped in a
// PeriodicExportingMetricReader.  sdk_reader() returns the MetricReader to
// register via AddMetricReader().

class OtlpMetricExporter {
public:
    explicit OtlpMetricExporter(const OtlpMetricConfig& cfg);
    ~OtlpMetricExporter();

    OtlpMetricExporter(const OtlpMetricExporter&) = delete;
    OtlpMetricExporter& operator=(const OtlpMetricExporter&) = delete;
    OtlpMetricExporter(OtlpMetricExporter&&) noexcept;
    OtlpMetricExporter& operator=(OtlpMetricExporter&&) noexcept;

    [[nodiscard]] opentelemetry::sdk::metrics::MetricReader* sdk_reader() const noexcept
        [[clang::lifetimebound]];

    [[nodiscard]] std::shared_ptr<opentelemetry::sdk::metrics::MetricReader> sdk_reader_shared()
        const noexcept;

    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ── OtlpMetricExporterFromReader ──────────────────────────────────────────────
//
// Variant that accepts a pre-built MetricReader (shared_ptr) — used by
// OtelDualExportBuilder to inject a mock reader for testing (TS-11).

class OtlpMetricExporterFromReader {
public:
    explicit OtlpMetricExporterFromReader(
        std::shared_ptr<opentelemetry::sdk::metrics::MetricReader> reader);

    [[nodiscard]] std::shared_ptr<opentelemetry::sdk::metrics::MetricReader> sdk_reader_shared()
        const noexcept;

private:
    std::shared_ptr<opentelemetry::sdk::metrics::MetricReader> reader_;
};

// ── OtelDualExportBuilder ─────────────────────────────────────────────────────
//
// Fluent builder that wires a Prometheus reader AND an OTLP reader onto ONE
// sdk::metrics::MeterProvider via two AddMetricReader() calls.  This is the
// FR-017 dual-export contract (no MultiMetricExporter — R1).
//
// Usage:
//   auto mp = OtelDualExportBuilder{}
//               .with_prometheus(prom_cfg)
//               .with_otlp(otlp_cfg)
//               .build();
//
// Either reader is optional; build() with no readers returns a plain
// MeterProvider with no export (useful for unit tests / no-op).
//
// For testing: use with_otlp_reader(shared_ptr<MetricReader>) to inject a
// mock push reader instead of the real OTLP exporter.

class OtelDualExportBuilder {
public:
    OtelDualExportBuilder() = default;

    OtelDualExportBuilder& with_prometheus(const PrometheusConfig& cfg);
    OtelDualExportBuilder& with_otlp(const OtlpMetricConfig& cfg);

    // Inject a pre-built reader (for testing — mock OTLP push exporter).
    OtelDualExportBuilder& with_otlp_reader(
        std::shared_ptr<opentelemetry::sdk::metrics::MetricReader> reader);

    // Builds and returns the MeterProvider with both readers registered.
    // Ownership is transferred to the caller.
    [[nodiscard]] std::unique_ptr<opentelemetry::sdk::metrics::MeterProvider> build();

private:
    std::shared_ptr<opentelemetry::sdk::metrics::MetricReader> prom_reader_;
    std::shared_ptr<opentelemetry::sdk::metrics::MetricReader> otlp_reader_;
};

}  // namespace fixpp::otel
