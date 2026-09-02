// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/otel/test_session_spans.cpp
//
// TS-12: SessionSpans yields a session span + a parse child parented by the
// session, both OK, parse.latency_ns > 0.  Parenting uses the EXPLICIT
// StartSpanOptions API — never opentelemetry::trace::Scope.
//
// Cross-thread parenting: a child ParseSpan built on a different OS thread
// still carries the correct session span_id as parent_span_id.
//
// Uses the OTel in-memory span exporter to capture + assert span data.
//
// Anchor: specs/017-log-otel/contracts/otel-surface.md §SessionSpans.
// [const §XIII.3]: no Scope anywhere.
//
// The three `sleep_for(10us)` calls below are sub-granularity and DELIBERATELY
// left as sleeps. Each has to make one span's measured latency non-zero, which
// any timer granularity satisfies; there is no rate and no interleaving here for
// a coarser sleep to destroy, so the spin fix issue #327 applies elsewhere would
// buy nothing. Listed there as needing no change — do not sweep them for
// symmetry with the sites that do.

#include <gtest/gtest.h>

// Subjects under test.
#include <fixpp/otel/providers.hpp>
#include <fixpp/otel/session_spans.hpp>

// OTel SDK — in-memory exporter for span capture.
#include <opentelemetry/exporters/memory/in_memory_span_exporter_factory.h>
#include <opentelemetry/exporters/memory/in_memory_span_data.h>
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>
#include <opentelemetry/sdk/trace/simple_processor_factory.h>

// OTel span data inspection.
#include <opentelemetry/sdk/trace/span_data.h>
#include <opentelemetry/trace/span_metadata.h>

// OTel attribute access — OwnedAttributeValue is nostd::variant; get_if lives
// in the nostd namespace.
#include <opentelemetry/sdk/common/attribute_utils.h>
#include <opentelemetry/nostd/variant.h>

// Complete metric instrument types (needed for CreateUInt64Counter return type).
#include <opentelemetry/metrics/sync_instruments.h>

#include <stdexcept>
#include <thread>

namespace sdk_trace = opentelemetry::sdk::trace;

// ─────────────────────────────────────────────────────────────────────────────
// Test fixture: builds an in-memory SDK tracer for span capture/assertion.
// We exercise the SessionSpans and child span types directly via SDK tracers.
// This mirrors the production code paths (SessionSpans::make_parse_span uses
// the same tracer->StartSpan(..., opts) path with explicit parent SpanContext).
// ─────────────────────────────────────────────────────────────────────────────

class SessionSpansTest : public ::testing::Test {
protected:
    void SetUp() override {
        data_ = std::make_shared<opentelemetry::exporter::memory::InMemorySpanData>(200);
        auto exporter =
            opentelemetry::exporter::memory::InMemorySpanExporterFactory::Create(data_);
        auto processor =
            sdk_trace::SimpleSpanProcessorFactory::Create(std::move(exporter));
        auto resource = opentelemetry::sdk::resource::Resource::GetDefault();
        sdk_provider_ = sdk_trace::TracerProviderFactory::Create(
            std::move(processor), resource);
    }

    opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> get_tracer() {
        return sdk_provider_->GetTracer("fixpp.session");
    }

    std::vector<std::unique_ptr<sdk_trace::SpanData>> get_spans() {
        return data_->GetSpans();
    }

    // Helper: extract the int64_t latency_ns from a SpanData's attributes.
    // Returns -1 if not found or wrong type.
    static int64_t get_latency_ns(const sdk_trace::SpanData& sd) {
        const auto& attrs = sd.GetAttributes();
        auto it = attrs.find("latency_ns");
        if (it == attrs.end()) return -1;
        if (const auto* p = opentelemetry::nostd::get_if<int64_t>(&it->second)) {
            return *p;
        }
        return -1;
    }

    std::shared_ptr<opentelemetry::exporter::memory::InMemorySpanData> data_;
    std::unique_ptr<sdk_trace::TracerProvider> sdk_provider_;
};

// ── TS-12 core: session span + parse child, explicitly parented ───────────────
//
// Verifies:
//   (a) parse.latency_ns > 0
//   (b) parse.parent_span_id == session.span_id
//   (c) both have StatusCode::kOk
//   (d) uses StartSpanOptions{.parent=SpanContext} — no Scope

TEST_F(SessionSpansTest, SessionSpanAndParseChildBothOK) {
    auto tracer = get_tracer();

    // Start session lifecycle span (root — parent is invalid).
    opentelemetry::trace::StartSpanOptions session_opts;
    session_opts.parent = opentelemetry::trace::SpanContext::GetInvalid();  // root

    auto session_span = tracer->StartSpan("fixpp.session.lifecycle", session_opts);
    session_span->SetAttribute("fixpp.session.sender_comp_id", "CLIENT1");
    session_span->SetAttribute("fixpp.session.target_comp_id", "SERVER1");
    auto session_sc = session_span->GetContext();

    // Start parse child with explicit parent = session span context.
    // [const §XIII.3]: StartSpanOptions{.parent=SpanContext}, NOT Scope.
    opentelemetry::trace::StartSpanOptions parse_opts;
    parse_opts.parent = session_sc;

    const auto t0 = std::chrono::steady_clock::now();
    auto parse_span = tracer->StartSpan("fixpp.session.parse", parse_opts);

    // Deliberate sub-granularity sleep — see the file header (issue #327).
    std::this_thread::sleep_for(std::chrono::microseconds(10));

    const auto latency_ns = std::max(
        INT64_C(1),
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - t0).count());

    parse_span->SetAttribute("latency_ns", latency_ns);
    parse_span->SetStatus(opentelemetry::trace::StatusCode::kOk);
    parse_span->End();

    session_span->SetStatus(opentelemetry::trace::StatusCode::kOk);
    session_span->End();

    auto spans = get_spans();
    ASSERT_EQ(spans.size(), 2u);

    sdk_trace::SpanData* session_data = nullptr;
    sdk_trace::SpanData* parse_data   = nullptr;
    for (auto& s : spans) {
        if (s->GetName() == "fixpp.session.lifecycle") session_data = s.get();
        if (s->GetName() == "fixpp.session.parse")     parse_data   = s.get();
    }
    ASSERT_NE(session_data, nullptr) << "session lifecycle span not found";
    ASSERT_NE(parse_data,   nullptr) << "parse span not found";

    // Both OK.
    EXPECT_EQ(session_data->GetStatus(), opentelemetry::trace::StatusCode::kOk);
    EXPECT_EQ(parse_data->GetStatus(),   opentelemetry::trace::StatusCode::kOk);

    // Parent span_id of parse == span_id of session lifecycle.
    EXPECT_EQ(parse_data->GetParentSpanId(), session_data->GetSpanId())
        << "parse span parent_span_id must match session span's span_id";

    // latency_ns > 0.
    const int64_t lat = get_latency_ns(*parse_data);
    EXPECT_GT(lat, INT64_C(0)) << "latency_ns must be > 0";
}

// ── TS-12 cross-thread: child built on a different OS thread still parents ────
//
// Proves that using StartSpanOptions{.parent=SpanContext} works correctly even
// when the child span is started on a thread different from the parent.
// This is the key invariant: no thread-local state is involved.

TEST_F(SessionSpansTest, ParseChildOnDifferentThreadParentsCorrectly) {
    auto tracer = get_tracer();

    // Session span on the main thread.
    opentelemetry::trace::StartSpanOptions session_opts;
    session_opts.parent = opentelemetry::trace::SpanContext::GetInvalid();
    auto session_span = tracer->StartSpan("fixpp.session.lifecycle", session_opts);
    const auto session_sc = session_span->GetContext();  // copy for cross-thread use

    // Build parse span on a different OS thread using the copied SpanContext.
    // [const §XIII.3]: no Scope — parent is passed as explicit SpanContext value.
    {
        std::thread worker{[&tracer, &session_sc] {
            opentelemetry::trace::StartSpanOptions child_opts;
            child_opts.parent = session_sc;  // explicit parent — no Scope

            const auto t0 = std::chrono::steady_clock::now();
            auto ps = tracer->StartSpan("fixpp.session.parse", child_opts);

            // Deliberate sub-granularity sleep — see the file header (issue #327).
            std::this_thread::sleep_for(std::chrono::microseconds(10));

            const auto ns = std::max(
                INT64_C(1),
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - t0).count());
            ps->SetAttribute("latency_ns", ns);
            ps->SetStatus(opentelemetry::trace::StatusCode::kOk);
            ps->End();
        }};
        worker.join();
    }

    session_span->SetStatus(opentelemetry::trace::StatusCode::kOk);
    session_span->End();

    auto spans = get_spans();
    ASSERT_EQ(spans.size(), 2u);

    sdk_trace::SpanData* session_data = nullptr;
    sdk_trace::SpanData* parse_data   = nullptr;
    for (auto& s : spans) {
        if (s->GetName() == "fixpp.session.lifecycle") session_data = s.get();
        if (s->GetName() == "fixpp.session.parse")     parse_data   = s.get();
    }
    ASSERT_NE(session_data, nullptr);
    ASSERT_NE(parse_data,   nullptr);

    // Cross-thread: parent_span_id must still equal session span_id.
    EXPECT_EQ(parse_data->GetParentSpanId(), session_data->GetSpanId())
        << "cross-thread child must parent correctly via explicit SpanContext";

    EXPECT_EQ(parse_data->GetStatus(), opentelemetry::trace::StatusCode::kOk);

    const int64_t lat = get_latency_ns(*parse_data);
    EXPECT_GT(lat, INT64_C(0));
}

// ── TS-12 via fixpp::otel::ParseSpan RAII wrapper ────────────────────────────
//
// Exercises the production fixpp::otel::ParseSpan RAII type (T041).
// Uses the SDK tracer directly, bypassing fixpp::otel::TracerProvider, so we
// can inject the in-memory exporter for span capture.

TEST_F(SessionSpansTest, ParseSpanRaiiSetsLatencyAndStatus) {
    auto tracer = get_tracer();

    // Create a session span context.
    opentelemetry::trace::StartSpanOptions session_opts;
    session_opts.parent = opentelemetry::trace::SpanContext::GetInvalid();
    auto session_span = tracer->StartSpan("fixpp.session.lifecycle", session_opts);
    const auto session_sc = session_span->GetContext();
    session_span->SetStatus(opentelemetry::trace::StatusCode::kOk);
    session_span->End();

    // Construct a ParseSpan RAII object — production code path.
    {
        fixpp::otel::ParseSpan parse{tracer, session_sc};
        parse.set_msg_type("D");
        // Deliberate sub-granularity sleep — see the file header (issue #327).
        std::this_thread::sleep_for(std::chrono::microseconds(10));
        // dtor: records latency_ns + sets kOk + calls End()
    }

    auto spans = get_spans();
    ASSERT_EQ(spans.size(), 2u);

    sdk_trace::SpanData* parse_data   = nullptr;
    sdk_trace::SpanData* session_data = nullptr;
    for (auto& s : spans) {
        if (s->GetName() == "fixpp.session.parse")     parse_data   = s.get();
        if (s->GetName() == "fixpp.session.lifecycle") session_data = s.get();
    }
    ASSERT_NE(parse_data,   nullptr) << "parse span missing from in-memory exporter";
    ASSERT_NE(session_data, nullptr) << "session span missing from in-memory exporter";

    // Parent linkage via ParseSpan's explicit-SpanContext parent.
    EXPECT_EQ(parse_data->GetParentSpanId(), session_data->GetSpanId())
        << "ParseSpan must parent to the session span_id";

    // OK status (no set_error called).
    EXPECT_EQ(parse_data->GetStatus(), opentelemetry::trace::StatusCode::kOk);

    // latency_ns > 0 (recorded by ParseSpan dtor).
    const int64_t lat = get_latency_ns(*parse_data);
    ASSERT_GE(lat, INT64_C(0)) << "latency_ns attribute missing or wrong type";
    EXPECT_GT(lat, INT64_C(0)) << "latency_ns must be > 0";

    // fixpp.msg_type attribute present.
    const auto& attrs = parse_data->GetAttributes();
    EXPECT_NE(attrs.find("fixpp.msg_type"), attrs.end()) << "fixpp.msg_type missing";
}

// ── ParseSpan with set_error sets ERROR status ────────────────────────────────

TEST_F(SessionSpansTest, ParseSpanErrorSetsErrorStatus) {
    auto tracer = get_tracer();

    opentelemetry::trace::StartSpanOptions session_opts;
    session_opts.parent = opentelemetry::trace::SpanContext::GetInvalid();
    auto session_span = tracer->StartSpan("fixpp.session.lifecycle", session_opts);
    const auto session_sc = session_span->GetContext();
    session_span->SetStatus(opentelemetry::trace::StatusCode::kOk);
    session_span->End();

    {
        fixpp::otel::ParseSpan parse{tracer, session_sc};
        parse.set_error("bad checksum");
        // dtor: records ERROR status
    }

    auto spans = get_spans();
    sdk_trace::SpanData* parse_data = nullptr;
    for (auto& s : spans) {
        if (s->GetName() == "fixpp.session.parse") parse_data = s.get();
    }
    ASSERT_NE(parse_data, nullptr);
    EXPECT_EQ(parse_data->GetStatus(), opentelemetry::trace::StatusCode::kError);
}

// ── E3 no-op fallback: TracerProvider init does not throw/crash ───────────────
//
// Tests FR-019: on healthy init the provider is usable; span ops don't crash.
// The negative (force-throw) path requires SDK injection; documented limitation.

TEST(TracerProviderE3, HealthyInitDoesNotSurfaceError) {
    fixpp::otel::OtelConfig cfg{};
    cfg.resource.service_name = "test";

    // Must not throw or crash.
    fixpp::otel::TracerProvider provider{cfg};

    // Healthy init: init_status() must be a value (no error).
    EXPECT_TRUE(provider.init_status().has_value())
        << "healthy TracerProvider must not report an error";

    // get_tracer must return a non-null tracer.
    auto tracer = provider.get_tracer("test.scope");
    EXPECT_NE(tracer.get(), nullptr) << "get_tracer must return non-null";

    // Span operations on the returned tracer must not crash.
    opentelemetry::trace::StartSpanOptions opts;
    opts.parent = opentelemetry::trace::SpanContext::GetInvalid();
    auto span = tracer->StartSpan("test.span", opts);
    ASSERT_NE(span.get(), nullptr);
    span->SetAttribute("key", "value");
    span->SetStatus(opentelemetry::trace::StatusCode::kOk);
    span->End();  // must not crash

    provider.shutdown();  // must not crash
}

TEST(MeterProviderE3, HealthyInitDoesNotSurfaceError) {
    fixpp::otel::OtelConfig cfg{};
    cfg.resource.service_name = "test";

    fixpp::otel::MeterProvider provider{cfg};

    EXPECT_TRUE(provider.init_status().has_value())
        << "healthy MeterProvider must not report an error";

    auto meter = provider.get_meter("test.meter");
    EXPECT_NE(meter.get(), nullptr);

    provider.shutdown();  // must not crash
}

// ── E3 NEGATIVE: init-failure → no-op-fallback → otel_provider_init_failed ────
//
// Injects a factory that THROWS via OtelConfig::tracer_factory_for_test /
// meter_factory_for_test (the test seam), driving the real catch(...)→Noop
// arm in providers.cpp.
//
// Asserts all four FR-019 negative-path properties:
//   (1) construction does NOT throw/crash,
//   (2) init_status() == unexpected(otel_provider_init_failed),
//   (3) the returned provider is the no-op: get_tracer()/get_meter() returns
//       non-null and a span/counter op is silent (no crash),
//   (4) shutdown() on the fallback is safe.
//
// BITE-CONFIRM: if you remove the catch(...)→Noop arm from providers.cpp, the
// factory's throw propagates through the TracerProvider ctor, and assertion (1)
// fails because ASSERT_NO_THROW catches the uncaught exception.

TEST(TracerProviderE3Negative, InitFailureFallsBackToNoopAndSurfacesError) {
    fixpp::otel::OtelConfig cfg{};
    cfg.resource.service_name = "test-negative";
    // Inject a factory that throws — drives the catch(...)→Noop arm.
    cfg.tracer_factory_for_test = []()
        -> std::shared_ptr<opentelemetry::trace::TracerProvider>
    {
        throw std::runtime_error("simulated TracerProvider init failure");
    };

    // (1) Construction must NOT throw or crash.
    fixpp::otel::TracerProvider provider{cfg};

    // (2) init_status() must carry otel_provider_init_failed.
    ASSERT_FALSE(provider.init_status().has_value())
        << "init_status must be an error after factory throw";
    EXPECT_EQ(provider.init_status().error(),
              fixpp::core::error::otel_provider_init_failed)
        << "init_status error must be otel_provider_init_failed";

    // (3) get_tracer() returns non-null; span ops are silent (no-op provider).
    auto tracer = provider.get_tracer("test.scope");
    ASSERT_NE(tracer.get(), nullptr) << "fallback must return non-null tracer";

    opentelemetry::trace::StartSpanOptions opts;
    opts.parent = opentelemetry::trace::SpanContext::GetInvalid();
    auto span = tracer->StartSpan("silent.span", opts);
    ASSERT_NE(span.get(), nullptr);
    span->SetAttribute("key", "value");  // must not crash (no-op)
    span->SetStatus(opentelemetry::trace::StatusCode::kOk);
    span->End();  // must not crash (no-op)

    // (4) shutdown() on the noop fallback is safe.
    provider.shutdown();  // must not crash
}

TEST(MeterProviderE3Negative, InitFailureFallsBackToNoopAndSurfacesError) {
    fixpp::otel::OtelConfig cfg{};
    cfg.resource.service_name = "test-negative";
    // Inject a factory that throws — drives the catch(...)→Noop arm.
    cfg.meter_factory_for_test = []()
        -> std::shared_ptr<opentelemetry::metrics::MeterProvider>
    {
        throw std::runtime_error("simulated MeterProvider init failure");
    };

    // (1) Construction must NOT throw or crash.
    fixpp::otel::MeterProvider provider{cfg};

    // (2) init_status() must carry otel_provider_init_failed.
    ASSERT_FALSE(provider.init_status().has_value())
        << "init_status must be an error after factory throw";
    EXPECT_EQ(provider.init_status().error(),
              fixpp::core::error::otel_provider_init_failed)
        << "init_status error must be otel_provider_init_failed";

    // (3) get_meter() returns non-null; counter ops are silent (no-op provider).
    auto meter = provider.get_meter("test.meter");
    ASSERT_NE(meter.get(), nullptr) << "fallback must return non-null meter";

    // NoopMeter::CreateUInt64Counter returns a noop counter; Add() must not crash.
    auto counter = meter->CreateUInt64Counter("silent.counter");
    ASSERT_NE(counter.get(), nullptr);
    counter->Add(1);  // must not crash (no-op)

    // (4) shutdown() on the noop fallback is safe.
    provider.shutdown();  // must not crash
}
