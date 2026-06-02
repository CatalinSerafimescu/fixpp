// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/otel/providers.hpp
//
// Thin RAII wrappers over the OpenTelemetry C++ SDK TracerProvider and
// MeterProvider (FR-019 / OBS-001 / OBS-002).  These are the types that
// EngineConfig forward-declares in include/fixpp/core/engine_config.hpp.
//
// On provider-init failure the ctor falls back to the SDK no-op provider and
// records otel_provider_init_failed in init_status() — the engine is never
// aborted by OTel setup failures (FR-019).
//
// Anchor: .specify/2k-log-otel.md §4.8; contracts/otel-surface.md §TracerProvider.
// [const §XIII.3]: opentelemetry::trace::Scope is NEVER used anywhere in this module.
#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <fixpp/core/error.hpp>  // expected_t, error::otel_provider_init_failed

// Pull in the OTel API types for Tracer/Meter return values.
// These are lightweight API-only headers (no SDK internals).
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/trace/tracer.h>
#include <opentelemetry/trace/tracer_provider.h>
#include <opentelemetry/metrics/meter.h>
#include <opentelemetry/metrics/meter_provider.h>

namespace fixpp::otel {

// ── Resource attributes for a service (maps to sdk::resource::Resource) ──────

struct OtelResourceAttributes {
    std::string service_name;
    std::string service_version;
    std::string deployment_environment;
    // Additional arbitrary key=value pairs.
    std::vector<std::pair<std::string, std::string>> extra;
};

// ── Shared OTel config (provider endpoint + resource) ────────────────────────

struct OtelConfig {
    std::string endpoint;               // e.g. "http://localhost:4318"
    bool        use_grpc{false};        // gRPC OTLP (deferred to Phase 5)
    OtelResourceAttributes resource;
    std::string cert_path;              // optional PEM; empty ⇒ plain HTTP
    std::chrono::seconds export_interval{60};
    std::chrono::seconds export_timeout{30};

    // ── Test-seam injection (null in production) ──────────────────────────────
    // When set, the TracerProvider / MeterProvider ctor calls this factory
    // INSTEAD of the real SDK factory, but INSIDE the same try block — so a
    // factory that throws drives the identical catch(...)→Noop fallback path.
    // Zero-cost and zero-behavior-change when null (the default).
    // Usage: set to a lambda that throws to exercise the FR-019 fallback.
    std::function<std::shared_ptr<opentelemetry::trace::TracerProvider>()>
        tracer_factory_for_test;
    std::function<std::shared_ptr<opentelemetry::metrics::MeterProvider>()>
        meter_factory_for_test;
};

// ── TracerProvider ────────────────────────────────────────────────────────────
//
// Thin RAII owner of an sdk::trace::TracerProvider.
//   • get_tracer() — borrowed pointer; callers MUST NOT outlive *this.
//   • shutdown()   — flush in-flight spans; idempotent.
//   • init_status() — otel_provider_init_failed if SDK init failed (FR-019).
//
// On construction failure transparently falls back to NoopTracerProvider so
// callers need not handle null (FR-019).
//
// [const §XIII.3]: no opentelemetry::trace::Scope anywhere in this module.

class TracerProvider {
public:
    explicit TracerProvider(const OtelConfig& cfg);
    ~TracerProvider();

    TracerProvider(const TracerProvider&)            = delete;
    TracerProvider& operator=(const TracerProvider&) = delete;
    // Move constructor/assignment defined in .cpp (Impl is complete there).
    TracerProvider(TracerProvider&&) noexcept;
    TracerProvider& operator=(TracerProvider&&) noexcept;

    // Returns a borrowed tracer for `name`.  Lifetime is tied to *this.
    [[nodiscard]] opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer>
    get_tracer(std::string_view name) const [[clang::lifetimebound]];

    // Flush + shut down the underlying SDK provider.  Idempotent.
    void shutdown();

    // Returns a void expected — unexpected(otel_provider_init_failed) if the
    // SDK TracerProvider could not be constructed.
    [[nodiscard]] fixpp::core::expected_t<void> init_status() const noexcept {
        return init_status_;
    }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    fixpp::core::expected_t<void> init_status_;
};

// ── MeterProvider ─────────────────────────────────────────────────────────────
//
// Same contract as TracerProvider; wraps sdk::metrics::MeterProvider.

class MeterProvider {
public:
    explicit MeterProvider(const OtelConfig& cfg);
    ~MeterProvider();

    MeterProvider(const MeterProvider&)            = delete;
    MeterProvider& operator=(const MeterProvider&) = delete;
    // Move constructor/assignment defined in .cpp (Impl is complete there).
    MeterProvider(MeterProvider&&) noexcept;
    MeterProvider& operator=(MeterProvider&&) noexcept;

    // Returns a borrowed meter for `name`.  Lifetime is tied to *this.
    [[nodiscard]] opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Meter>
    get_meter(std::string_view name) const [[clang::lifetimebound]];

    // Flush + shut down the underlying SDK provider.  Idempotent.
    void shutdown();

    [[nodiscard]] fixpp::core::expected_t<void> init_status() const noexcept {
        return init_status_;
    }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    fixpp::core::expected_t<void> init_status_;
};

}  // namespace fixpp::otel
