// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/otel/providers.cpp
//
// Implementation of TracerProvider / MeterProvider thin RAII wrappers over
// the OpenTelemetry C++ SDK (FR-019 / OBS-001 / OBS-002).
//
// On SDK init failure each wrapper falls back to the SDK noop provider so
// that callers never receive a null provider — the engine continues running
// with silently-dropped telemetry and otel_provider_init_failed recorded in
// init_status() (FR-019).
//
// Anchor: .specify/2k-log-otel.md §4.8; contracts/otel-surface.md.
// [const §XIII.3]: opentelemetry::trace::Scope is NEVER used here.

#include <fixpp/otel/providers.hpp>

// SDK trace
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>
#include <opentelemetry/sdk/trace/simple_processor_factory.h>
#include <opentelemetry/sdk/trace/exporter.h>
#include <opentelemetry/sdk/trace/span_data.h>
#include <opentelemetry/sdk/resource/resource.h>

// SDK metrics
#include <opentelemetry/sdk/metrics/meter_provider_factory.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/sdk/metrics/view/view_registry.h>

// API noop fallbacks
#include <opentelemetry/trace/noop.h>
#include <opentelemetry/metrics/noop.h>

// API TracerProvider / MeterProvider base types
#include <opentelemetry/trace/tracer_provider.h>
#include <opentelemetry/metrics/meter_provider.h>

#include <map>
#include <stdexcept>

namespace fixpp::otel {

// ── helpers ──────────────────────────────────────────────────────────────────

namespace {

// Build an OTel SDK resource from OtelResourceAttributes.
opentelemetry::sdk::resource::Resource
make_resource(const OtelResourceAttributes& ra) {
    opentelemetry::sdk::common::AttributeMap attrs;
    if (!ra.service_name.empty())
        attrs["service.name"]        = ra.service_name;
    if (!ra.service_version.empty())
        attrs["service.version"]     = ra.service_version;
    if (!ra.deployment_environment.empty())
        attrs["deployment.environment"] = ra.deployment_environment;
    for (const auto& [k, v] : ra.extra)
        attrs[k] = v;
    return opentelemetry::sdk::resource::Resource::Create(attrs);
}

// A no-op SpanExporter so we can construct a real TracerProvider when no
// OTLP endpoint is configured — spans are silently discarded.
// MakeRecordable() MUST return a valid (non-null) recordable because the
// SimpleSpanProcessor dereferences it unconditionally.
class NullSpanExporter final : public opentelemetry::sdk::trace::SpanExporter {
public:
    std::unique_ptr<opentelemetry::sdk::trace::Recordable>
    MakeRecordable() noexcept override {
        return std::make_unique<opentelemetry::sdk::trace::SpanData>();
    }

    opentelemetry::sdk::common::ExportResult Export(
        const opentelemetry::nostd::span<
            std::unique_ptr<opentelemetry::sdk::trace::Recordable>>&) noexcept override {
        return opentelemetry::sdk::common::ExportResult::kSuccess;
    }

    bool ForceFlush(std::chrono::microseconds) noexcept override { return true; }
    bool Shutdown(std::chrono::microseconds)   noexcept override { return true; }
};

}  // namespace

// ── TracerProvider ────────────────────────────────────────────────────────────

struct TracerProvider::Impl {
    // Holds a reference-counted pointer to the OTel API TracerProvider.
    // May be the real SDK provider or a NoopTracerProvider.
    std::shared_ptr<opentelemetry::trace::TracerProvider> provider;
};

TracerProvider::TracerProvider(const OtelConfig& cfg)
    : impl_(std::make_unique<Impl>())
    , init_status_(fixpp::core::expected_t<void>{})
{
    try {
        if (cfg.tracer_factory_for_test) {
            // Test-seam path: call the injected factory (may throw to exercise
            // the FR-019 catch arm without touching the real SDK).
            impl_->provider = cfg.tracer_factory_for_test();
        } else {
            // Production path.
            auto resource = make_resource(cfg.resource);
            auto exporter = std::make_unique<NullSpanExporter>();
            auto processor = opentelemetry::sdk::trace::SimpleSpanProcessorFactory::Create(
                std::move(exporter));
            auto sdk_provider = opentelemetry::sdk::trace::TracerProviderFactory::Create(
                std::move(processor), resource);
            impl_->provider = std::shared_ptr<opentelemetry::trace::TracerProvider>(
                sdk_provider.release());
        }
    } catch (...) {
        // Fall back to no-op provider; record the failure (FR-019).
        impl_->provider = std::make_shared<opentelemetry::trace::NoopTracerProvider>();
        init_status_ = std::unexpected(fixpp::core::error::otel_provider_init_failed);
    }
}

TracerProvider::~TracerProvider() = default;
TracerProvider::TracerProvider(TracerProvider&&) noexcept = default;
TracerProvider& TracerProvider::operator=(TracerProvider&&) noexcept = default;

opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer>
TracerProvider::get_tracer(std::string_view name) const {
    return impl_->provider->GetTracer(
        opentelemetry::nostd::string_view{name.data(), name.size()});
}

void TracerProvider::shutdown() {
    if (auto* sdk = dynamic_cast<opentelemetry::sdk::trace::TracerProvider*>(
            impl_->provider.get())) {
        sdk->Shutdown();
    }
    // NoopTracerProvider has no Shutdown; nothing to do.
}

// ── MeterProvider ─────────────────────────────────────────────────────────────

struct MeterProvider::Impl {
    std::shared_ptr<opentelemetry::metrics::MeterProvider> provider;
};

MeterProvider::MeterProvider(const OtelConfig& cfg)
    : impl_(std::make_unique<Impl>())
    , init_status_(fixpp::core::expected_t<void>{})
{
    try {
        if (cfg.meter_factory_for_test) {
            // Test-seam path: call the injected factory (may throw to exercise
            // the FR-019 catch arm without touching the real SDK).
            impl_->provider = cfg.meter_factory_for_test();
        } else {
            // Production path.
            auto resource = make_resource(cfg.resource);
            auto sdk_provider = opentelemetry::sdk::metrics::MeterProviderFactory::Create(
                /*views=*/std::make_unique<opentelemetry::sdk::metrics::ViewRegistry>(),
                resource);
            impl_->provider = std::shared_ptr<opentelemetry::metrics::MeterProvider>(
                sdk_provider.release());
        }
    } catch (...) {
        impl_->provider = std::make_shared<opentelemetry::metrics::NoopMeterProvider>();
        init_status_ = std::unexpected(fixpp::core::error::otel_provider_init_failed);
    }
}

MeterProvider::~MeterProvider() = default;
MeterProvider::MeterProvider(MeterProvider&&) noexcept = default;
MeterProvider& MeterProvider::operator=(MeterProvider&&) noexcept = default;

opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Meter>
MeterProvider::get_meter(std::string_view name) const {
    return impl_->provider->GetMeter(
        opentelemetry::nostd::string_view{name.data(), name.size()});
}

void MeterProvider::shutdown() {
    if (auto* sdk = dynamic_cast<opentelemetry::sdk::metrics::MeterProvider*>(
            impl_->provider.get())) {
        sdk->Shutdown();
    }
}

}  // namespace fixpp::otel
