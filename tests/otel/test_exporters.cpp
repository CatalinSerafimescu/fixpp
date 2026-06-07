// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/otel/test_exporters.cpp
//
// Coverage-remediation: exercises PrometheusExporter ctor/shutdown,
// OtlpMetricExporter ctor/shutdown, OtelDualExportBuilder::with_prometheus /
// with_otlp / build (the two-AddMetricReader wiring path).
//
// Port-collision avoidance: we pick a high ephemeral port (19464) for the
// PrometheusExporter test; different from the :9464 used by ts11_dual_metric_export
// to allow both tests to coexist.  The PrometheusExporter test only asserts
// construction + reader non-null + shutdown — no HTTP scrape needed for coverage.
//
// OtlpMetricExporter: object construction only (safe, no connect until export).
//
// OtelDualExportBuilder: exercises with_prometheus + with_otlp + build, then
// asserts a non-null SDK MeterProvider is returned (dual-AddMetricReader path).
//
// Anchor: .specify/2k-log-otel.md §4.10; contracts/otel-surface.md §Metric exporters.
// [const §XIII.3]: no opentelemetry::trace::Scope.

#include <gtest/gtest.h>

#include <fixpp/otel/exporters.hpp>

// SDK MeterProvider for dynamic_cast assertion.
#include <opentelemetry/sdk/metrics/meter_provider.h>

// ── PrometheusExporter ────────────────────────────────────────────────────────
//
// Constructs a PrometheusExporter on loopback:19464 (high port, avoids collision
// with the TS-11 test on :9464).  Asserts sdk_reader()/sdk_reader_shared()
// non-null then calls shutdown().

TEST(PrometheusExporterTest, CtorReaderNonNullAndShutdown) {
    fixpp::otel::PrometheusConfig cfg;
    cfg.host = "0.0.0.0";
    cfg.port = 19464;

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

// ── OtelDualExportBuilder::with_prometheus + with_otlp + build ───────────────
//
// Exercises the two-AddMetricReader wiring in build().
// Uses a high port (19465) for Prometheus; OTLP uses a dummy endpoint.
// Asserts the returned MeterProvider is a non-null SDK MeterProvider.

TEST(OtelDualExportBuilderTest, WithPrometheusAndOtlpBuildReturnsNonNull) {
    fixpp::otel::PrometheusConfig prom_cfg;
    prom_cfg.host = "0.0.0.0";
    prom_cfg.port = 19465;

    fixpp::otel::OtlpMetricConfig otlp_cfg;
    otlp_cfg.endpoint = "http://127.0.0.1:14318";  // dummy — no export triggered

    auto mp = fixpp::otel::OtelDualExportBuilder{}
                .with_prometheus(prom_cfg)
                .with_otlp(otlp_cfg)
                .build();

    ASSERT_NE(mp, nullptr) << "OtelDualExportBuilder::build() must return non-null";

    // Must be the concrete SDK MeterProvider (with both readers registered).
    auto* sdk_mp = dynamic_cast<opentelemetry::sdk::metrics::MeterProvider*>(mp.get());
    ASSERT_NE(sdk_mp, nullptr)
        << "build() must return an sdk::metrics::MeterProvider";

    // Shutdown cleanly (stops the Prometheus civetweb server + OTLP reader).
    sdk_mp->Shutdown();
}
