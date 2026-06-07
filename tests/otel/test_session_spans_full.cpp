// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/otel/test_session_spans_full.cpp
//
// Coverage-remediation: exercises SessionSpans ctor/dtor, make_store_span,
// make_dispatch_span, session_trace_context (via from_span_context), the
// to_span_context non-zero path, StoreSpan (ctor/dtor/set_seq_num/set_error),
// and DispatchSpan (ctor/dtor/set_msg_type/set_error).
//
// Uses the SDK in-memory exporter (same approach as test_session_spans.cpp)
// injected via OtelConfig::tracer_factory_for_test so that TracerProvider
// wraps an in-memory-exporter-backed SDK provider.  This lets us assert
// actual span status (kOk / kError) — not just "no crash".
//
// Anchor: specs/017-log-otel/contracts/otel-surface.md §SessionSpans.
// [const §XIII.3]: no Scope anywhere.

#include <gtest/gtest.h>

#include <fixpp/otel/providers.hpp>
#include <fixpp/otel/session_spans.hpp>

// OTel SDK — in-memory exporter for span capture.
#include <opentelemetry/exporters/memory/in_memory_span_data.h>
#include <opentelemetry/exporters/memory/in_memory_span_exporter_factory.h>
#include <opentelemetry/sdk/trace/simple_processor_factory.h>
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>

// OTel span data inspection.
#include <opentelemetry/sdk/common/attribute_utils.h>
#include <opentelemetry/sdk/trace/span_data.h>
#include <opentelemetry/trace/span_metadata.h>

#include <cstdint>
#include <string>
#include <vector>

namespace sdk_trace = opentelemetry::sdk::trace;

// ── Test fixture ─────────────────────────────────────────────────────────────
//
// Injects an in-memory-exporter-backed SDK TracerProvider into
// fixpp::otel::TracerProvider via tracer_factory_for_test.
// This lets SessionSpans (which takes TracerProvider&) use a provider whose
// spans we can inspect after they are ended.

class SessionSpansFullTest : public ::testing::Test {
protected:
    void SetUp() override {
        data_ = std::make_shared<opentelemetry::exporter::memory::InMemorySpanData>(500);

        // Build a real SDK TracerProvider backed by the in-memory exporter.
        auto make_sdk_provider = [this]()
            -> std::shared_ptr<opentelemetry::trace::TracerProvider>
        {
            auto exporter =
                opentelemetry::exporter::memory::InMemorySpanExporterFactory::Create(data_);
            auto processor = sdk_trace::SimpleSpanProcessorFactory::Create(std::move(exporter));
            auto resource   = opentelemetry::sdk::resource::Resource::GetDefault();
            return sdk_trace::TracerProviderFactory::Create(std::move(processor), resource);
        };

        fixpp::otel::OtelConfig cfg{};
        cfg.resource.service_name    = "test";
        cfg.tracer_factory_for_test  = make_sdk_provider;

        provider_ = std::make_unique<fixpp::otel::TracerProvider>(cfg);
        ASSERT_TRUE(provider_->init_status().has_value())
            << "TracerProvider init must succeed (in-memory factory does not throw)";
    }

    // Return all spans that have been recorded and ended so far.
    std::vector<std::unique_ptr<sdk_trace::SpanData>> get_spans() {
        return data_->GetSpans();
    }

    // Find a span by name in a vector of SpanData.
    static sdk_trace::SpanData* find_span(
        const std::vector<std::unique_ptr<sdk_trace::SpanData>>& spans,
        const std::string& name)
    {
        for (const auto& s : spans) {
            if (s->GetName() == name) return s.get();
        }
        return nullptr;
    }

    // Extract int64_t latency_ns attribute, or -1 if missing / wrong type.
    static int64_t get_latency_ns(const sdk_trace::SpanData& sd) {
        const auto& attrs = sd.GetAttributes();
        auto it = attrs.find("latency_ns");
        if (it == attrs.end()) return -1;
        if (const auto* p = opentelemetry::nostd::get_if<int64_t>(&it->second)) return *p;
        return -1;
    }

    std::shared_ptr<opentelemetry::exporter::memory::InMemorySpanData> data_;
    std::unique_ptr<fixpp::otel::TracerProvider> provider_;
};

// ── SessionSpans ctor/dtor + to_span_context (all-zero → root) ───────────────
//
// Constructs a SessionSpans with a default (all-zero) parent_ctx.
// to_span_context() takes the all-zero branch → invalid SpanContext → root.
// Verifies: lifecycle span exists and has kOk status after dtor.

TEST_F(SessionSpansFullTest, CtorDtorZeroParent) {
    {
        fixpp::otel::SessionSpans ss{*provider_, "SENDER", "TARGET"};
        // session_span() is non-null while alive.
        ASSERT_NE(ss.session_span().get(), nullptr);
    }
    // After dtor, the lifecycle span should be ended (in-memory exporter received it).
    auto spans = get_spans();
    auto* lifecycle = find_span(spans, "fixpp.session.lifecycle");
    ASSERT_NE(lifecycle, nullptr) << "lifecycle span not exported after SessionSpans dtor";
    EXPECT_EQ(lifecycle->GetStatus(), opentelemetry::trace::StatusCode::kOk);
}

// ── session_trace_context() round-trip (from_span_context) ───────────────────
//
// session_trace_context() calls from_span_context(session_ctx_).
// The returned trace_context must be non-zero (the SDK assigns a real trace_id
// to every span started on a real provider).
// Verifies: trace_id bytes are not all zero.

TEST_F(SessionSpansFullTest, SessionTraceContextNonZero) {
    fixpp::otel::SessionSpans ss{*provider_, "SENDER", "TARGET"};

    auto tc = ss.session_trace_context();

    // The SDK assigns a real trace_id — all-zero would mean a noop span.
    bool all_zero = true;
    for (auto b : tc.trace_id) {
        if (b != std::byte{0}) { all_zero = false; break; }
    }
    EXPECT_FALSE(all_zero) << "session_trace_context() must return a non-zero trace_id";
}

// ── to_span_context non-zero path: pass a non-zero parent_ctx ────────────────
//
// Constructs a SessionSpans with a manually populated (non-zero) parent_ctx.
// This exercises the non-zero branch in to_span_context(), which builds a
// real SpanContext and chains the lifecycle span under a remote parent.

TEST_F(SessionSpansFullTest, ToSpanContextNonZeroBranch) {
    // Build a non-zero parent trace_context (any non-zero bytes suffice).
    fixpp::otel::trace_context parent{};
    for (int i = 0; i < 16; ++i) parent.trace_id[i] = static_cast<std::byte>(i + 1);
    for (int i = 0; i < 8;  ++i) parent.span_id[i]  = static_cast<std::byte>(i + 0x10);
    parent.flags = 0x01U;  // sampled

    {
        fixpp::otel::SessionSpans ss{*provider_, "SENDER", "TARGET", parent};
        ASSERT_NE(ss.session_span().get(), nullptr);
    }

    auto spans = get_spans();
    auto* lifecycle = find_span(spans, "fixpp.session.lifecycle");
    ASSERT_NE(lifecycle, nullptr);

    // The lifecycle span must be remote-parented: GetParentSpanId() must equal
    // the supplied parent.span_id bytes byte-for-byte.
    // to_span_context() builds a SpanContext with span_id from parent.span_id;
    // the SDK propagates that as the parent span_id of the lifecycle span.
    const auto& parent_sid = lifecycle->GetParentSpanId();
    uint8_t actual_buf[8]{};
    parent_sid.CopyBytesTo(opentelemetry::nostd::span<uint8_t, 8>{actual_buf, 8});

    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(actual_buf[i], static_cast<uint8_t>(parent.span_id[i]))
            << "GetParentSpanId() byte[" << i << "] does not match parent.span_id["
            << i << "]; to_span_context must preserve span_id bytes";
    }
}

// ── round-trip: fill trace_context → SessionSpans → session_trace_context ────
//
// Exercises to_span_context (non-zero) and from_span_context together:
// the bytes that go in via parent_ctx should survive round-trip through the
// OTel SpanContext layer (trace_id and flags; span_id is the SESSION span's
// own id, not the parent's — so we check trace_id + flags only).

TEST_F(SessionSpansFullTest, TraceContextRoundTrip) {
    fixpp::otel::trace_context parent{};
    for (int i = 0; i < 16; ++i) parent.trace_id[i] = static_cast<std::byte>(0xAB);
    for (int i = 0; i < 8;  ++i) parent.span_id[i]  = static_cast<std::byte>(0xCD);
    parent.flags = 0x01U;  // sampled

    fixpp::otel::SessionSpans ss{*provider_, "SENDER", "TARGET", parent};

    // session_trace_context() returns the LIFECYCLE SPAN's own context (via
    // from_span_context), not the parent's context.  The lifecycle span is started
    // as a child of parent, so:
    //   • trace_id is SHARED across the chain — must equal parent.trace_id.
    //   • flags reflect the sampling decision propagated from parent.flags —
    //     must equal parent.flags (from_span_context copies TraceFlags::flags()).
    auto tc = ss.session_trace_context();
    EXPECT_EQ(tc.trace_id, parent.trace_id)
        << "trace_id must be propagated from parent through the lifecycle span";
    EXPECT_EQ(tc.flags, parent.flags)
        << "flags must be propagated from parent through the lifecycle span; "
           "from_span_context copies sc.trace_flags().flags() into tc.flags";
}

// ── make_store_span: set_seq_num + ok branch ──────────────────────────────────
//
// Constructs a StoreSpan via SessionSpans::make_store_span(), calls set_seq_num,
// lets it destruct with no set_error → dtor sets kOk.

TEST_F(SessionSpansFullTest, StoreSpanOkBranch) {
    fixpp::otel::SessionSpans ss{*provider_, "SENDER", "TARGET"};

    {
        auto store = ss.make_store_span();
        ASSERT_NE(store.span().get(), nullptr);
        store.set_seq_num(42);
        // dtor → record_latency + kOk + End()
    }

    auto spans = get_spans();
    auto* store_data = find_span(spans, "fixpp.session.store");
    ASSERT_NE(store_data, nullptr) << "store span not found after StoreSpan dtor";
    EXPECT_EQ(store_data->GetStatus(), opentelemetry::trace::StatusCode::kOk);

    // latency_ns must be positive.
    EXPECT_GT(get_latency_ns(*store_data), INT64_C(0));

    // fixpp.seq_num attribute must be present.
    const auto& attrs = store_data->GetAttributes();
    EXPECT_NE(attrs.find("fixpp.seq_num"), attrs.end()) << "fixpp.seq_num attribute missing";
}

// ── make_store_span: set_error branch → dtor sets kError ────────────────────

TEST_F(SessionSpansFullTest, StoreSpanErrorBranch) {
    fixpp::otel::SessionSpans ss{*provider_, "SENDER", "TARGET"};

    {
        auto store = ss.make_store_span();
        store.set_error("disk full");
        // dtor → kError + End()
    }

    auto spans = get_spans();
    auto* store_data = find_span(spans, "fixpp.session.store");
    ASSERT_NE(store_data, nullptr);
    EXPECT_EQ(store_data->GetStatus(), opentelemetry::trace::StatusCode::kError)
        << "StoreSpan::set_error must cause dtor to set kError status";
}

// ── make_dispatch_span: set_msg_type + ok branch ──────────────────────────────

TEST_F(SessionSpansFullTest, DispatchSpanOkBranch) {
    fixpp::otel::SessionSpans ss{*provider_, "SENDER", "TARGET"};

    {
        auto dispatch = ss.make_dispatch_span();
        ASSERT_NE(dispatch.span().get(), nullptr);
        dispatch.set_msg_type("D");
        // dtor → kOk + End()
    }

    auto spans = get_spans();
    auto* dispatch_data = find_span(spans, "fixpp.session.dispatch");
    ASSERT_NE(dispatch_data, nullptr) << "dispatch span not found after DispatchSpan dtor";
    EXPECT_EQ(dispatch_data->GetStatus(), opentelemetry::trace::StatusCode::kOk);
    EXPECT_GT(get_latency_ns(*dispatch_data), INT64_C(0));

    // fixpp.msg_type attribute.
    const auto& attrs = dispatch_data->GetAttributes();
    EXPECT_NE(attrs.find("fixpp.msg_type"), attrs.end()) << "fixpp.msg_type attribute missing";
}

// ── make_dispatch_span: set_error branch → dtor sets kError ─────────────────

TEST_F(SessionSpansFullTest, DispatchSpanErrorBranch) {
    fixpp::otel::SessionSpans ss{*provider_, "SENDER", "TARGET"};

    {
        auto dispatch = ss.make_dispatch_span();
        dispatch.set_error("callback threw");
        // dtor → kError + End()
    }

    auto spans = get_spans();
    auto* dispatch_data = find_span(spans, "fixpp.session.dispatch");
    ASSERT_NE(dispatch_data, nullptr);
    EXPECT_EQ(dispatch_data->GetStatus(), opentelemetry::trace::StatusCode::kError)
        << "DispatchSpan::set_error must cause dtor to set kError status";
}

// ── child spans are parented to the session lifecycle span ───────────────────
//
// Assert that make_store_span() and make_dispatch_span() each produce a span
// whose parent_span_id == session lifecycle span's span_id.

TEST_F(SessionSpansFullTest, ChildSpansParentedToSessionSpan) {
    fixpp::otel::SessionSpans ss{*provider_, "SENDER", "TARGET"};

    // Capture the session span_id before creating children.
    auto session_sc = ss.session_span()->GetContext();

    { auto s = ss.make_store_span();    (void)s; }
    { auto d = ss.make_dispatch_span(); (void)d; }

    // End the session span (dtor).
    // Destroy ss to emit the lifecycle span.
    // We need to move ss into a scope so it ends before we read spans.
    // Reset provider after to allow span flush.

    // Capture the session span_id for assertion.
    auto session_span_id = session_sc.span_id();

    // Destroy ss to end the lifecycle span.
    { auto moved_ss = std::move(ss); }

    auto spans = get_spans();
    auto* store_data    = find_span(spans, "fixpp.session.store");
    auto* dispatch_data = find_span(spans, "fixpp.session.dispatch");
    auto* lifecycle     = find_span(spans, "fixpp.session.lifecycle");

    ASSERT_NE(store_data,    nullptr);
    ASSERT_NE(dispatch_data, nullptr);
    ASSERT_NE(lifecycle,     nullptr);

    EXPECT_EQ(store_data->GetParentSpanId(), lifecycle->GetSpanId())
        << "StoreSpan must be parented to the lifecycle span";
    EXPECT_EQ(dispatch_data->GetParentSpanId(), lifecycle->GetSpanId())
        << "DispatchSpan must be parented to the lifecycle span";
}
