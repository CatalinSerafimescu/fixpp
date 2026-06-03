// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/otel/exporters.cpp
//
// Implementation of PrometheusExporter, OtlpMetricExporter, and
// OtelDualExportBuilder (FR-017 / OBS-002 / TS-11).
//
// Dual-export pattern: ONE sdk::metrics::MeterProvider receives TWO
// AddMetricReader() calls — one Prometheus (pull) reader and one OTLP
// (PeriodicExportingMetricReader push) reader.  A single counter.Add(N)
// propagates to both.
//
// The PrometheusExporter IS the MetricReader (embedded civetweb HTTP server).
// The OtlpMetricExporter wraps a PushMetricExporter inside a
// PeriodicExportingMetricReader.
//
// Anchor: .specify/2k-log-otel.md §4.10; contracts/otel-surface.md.
// [const §XIII.3]: no opentelemetry::trace::Scope here.

#include <fixpp/otel/exporters.hpp>

// Prometheus exporter (MetricReader via civetweb embedded server).
#include <opentelemetry/exporters/prometheus/exporter_factory.h>
#include <opentelemetry/exporters/prometheus/exporter_options.h>

// OTLP HTTP metric exporter.
#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter_options.h>

// PeriodicExportingMetricReader (wraps a PushMetricExporter into a MetricReader).
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_options.h>

// SDK MeterProvider (concrete type for AddMetricReader).
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/sdk/metrics/meter_provider_factory.h>
#include <opentelemetry/sdk/metrics/metric_reader.h>
#include <opentelemetry/sdk/metrics/view/view_registry.h>
#include <opentelemetry/sdk/resource/resource.h>

namespace fixpp::otel {

// ── PrometheusExporter ────────────────────────────────────────────────────────

struct PrometheusExporter::Impl {
    // SDK PrometheusExporter IS a MetricReader.  We hold it as shared_ptr
    // because AddMetricReader() requires shared ownership.
    std::shared_ptr<opentelemetry::sdk::metrics::MetricReader> reader;
};

PrometheusExporter::PrometheusExporter(const PrometheusConfig& cfg)
    : impl_(std::make_unique<Impl>()) {
    opentelemetry::exporter::metrics::PrometheusExporterOptions opts{nullptr};
    opts.url = cfg.host + ":" + std::to_string(cfg.port);
    // metrics_path is set via the options struct in older SDK versions;
    // in 1.26.0 the civetweb exposer always serves /metrics — no override needed.
    auto reader_up = opentelemetry::exporter::metrics::PrometheusExporterFactory::Create(opts);
    impl_->reader = std::shared_ptr<opentelemetry::sdk::metrics::MetricReader>(reader_up.release());
}

PrometheusExporter::~PrometheusExporter() = default;
PrometheusExporter::PrometheusExporter(PrometheusExporter&&) noexcept = default;
PrometheusExporter& PrometheusExporter::operator=(PrometheusExporter&&) noexcept = default;

opentelemetry::sdk::metrics::MetricReader* PrometheusExporter::sdk_reader() const noexcept {
    return impl_->reader.get();
}

std::shared_ptr<opentelemetry::sdk::metrics::MetricReader> PrometheusExporter::sdk_reader_shared()
    const noexcept {
    return impl_->reader;
}

void PrometheusExporter::shutdown() {
    if (impl_ && impl_->reader) {
        impl_->reader->Shutdown();
    }
}

// ── OtlpMetricExporter ────────────────────────────────────────────────────────

struct OtlpMetricExporter::Impl {
    std::shared_ptr<opentelemetry::sdk::metrics::MetricReader> reader;
};

OtlpMetricExporter::OtlpMetricExporter(const OtlpMetricConfig& cfg)
    : impl_(std::make_unique<Impl>()) {
    opentelemetry::exporter::otlp::OtlpHttpMetricExporterOptions exporter_opts;
    exporter_opts.url = cfg.endpoint + "/v1/metrics";

    auto push_exporter =
        opentelemetry::exporter::otlp::OtlpHttpMetricExporterFactory::Create(exporter_opts);

    opentelemetry::sdk::metrics::PeriodicExportingMetricReaderOptions reader_opts;
    reader_opts.export_interval_millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(cfg.export_interval);
    reader_opts.export_timeout_millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(cfg.export_timeout);

    auto reader_up = opentelemetry::sdk::metrics::PeriodicExportingMetricReaderFactory::Create(
        std::move(push_exporter), reader_opts);
    impl_->reader = std::shared_ptr<opentelemetry::sdk::metrics::MetricReader>(reader_up.release());
}

OtlpMetricExporter::~OtlpMetricExporter() = default;
OtlpMetricExporter::OtlpMetricExporter(OtlpMetricExporter&&) noexcept = default;
OtlpMetricExporter& OtlpMetricExporter::operator=(OtlpMetricExporter&&) noexcept = default;

opentelemetry::sdk::metrics::MetricReader* OtlpMetricExporter::sdk_reader() const noexcept {
    return impl_->reader.get();
}

std::shared_ptr<opentelemetry::sdk::metrics::MetricReader> OtlpMetricExporter::sdk_reader_shared()
    const noexcept {
    return impl_->reader;
}

void OtlpMetricExporter::shutdown() {
    if (impl_ && impl_->reader) {
        impl_->reader->Shutdown();
    }
}

// ── OtlpMetricExporterFromReader ──────────────────────────────────────────────

OtlpMetricExporterFromReader::OtlpMetricExporterFromReader(
    std::shared_ptr<opentelemetry::sdk::metrics::MetricReader> reader)
    : reader_(std::move(reader)) {}

std::shared_ptr<opentelemetry::sdk::metrics::MetricReader>
OtlpMetricExporterFromReader::sdk_reader_shared() const noexcept {
    return reader_;
}

// ── OtelDualExportBuilder ─────────────────────────────────────────────────────

OtelDualExportBuilder& OtelDualExportBuilder::with_prometheus(const PrometheusConfig& cfg) {
    PrometheusExporter exp{cfg};
    prom_reader_ = exp.sdk_reader_shared();
    return *this;
}

OtelDualExportBuilder& OtelDualExportBuilder::with_otlp(const OtlpMetricConfig& cfg) {
    OtlpMetricExporter exp{cfg};
    otlp_reader_ = exp.sdk_reader_shared();
    return *this;
}

OtelDualExportBuilder& OtelDualExportBuilder::with_otlp_reader(
    std::shared_ptr<opentelemetry::sdk::metrics::MetricReader> reader) {
    otlp_reader_ = std::move(reader);
    return *this;
}

std::unique_ptr<opentelemetry::sdk::metrics::MeterProvider> OtelDualExportBuilder::build() {
    // Create an SDK MeterProvider with empty views + default resource.
    auto mp = opentelemetry::sdk::metrics::MeterProviderFactory::Create(
        std::make_unique<opentelemetry::sdk::metrics::ViewRegistry>());

    // Wire the two readers — two AddMetricReader() calls on ONE MeterProvider.
    // This is the FR-017 dual-export contract (R1: no MultiMetricExporter).
    if (prom_reader_) {
        mp->AddMetricReader(prom_reader_);
    }
    if (otlp_reader_) {
        mp->AddMetricReader(otlp_reader_);
    }

    return mp;
}

}  // namespace fixpp::otel
