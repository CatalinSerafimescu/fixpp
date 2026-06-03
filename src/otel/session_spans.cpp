// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/otel/session_spans.cpp
//
// Implementation of SessionSpans and its RAII child-span wrappers.
//
// CRITICAL [const §XIII.3]:
//   All child spans use StartSpanOptions{.parent = session_ctx_} — the
//   explicit SpanContext form.  opentelemetry::trace::Scope is NEVER used
//   here.  This guarantees correct parent span_id even when a child is
//   constructed on a different OS thread than the parent.
//
// Anchor: .specify/2k-log-otel.md §4.9; contracts/otel-surface.md §SessionSpans.

#include <fixpp/otel/providers.hpp>
#include <fixpp/otel/session_spans.hpp>

// OTel API
#include <opentelemetry/trace/span_id.h>
#include <opentelemetry/trace/trace_flags.h>
#include <opentelemetry/trace/trace_id.h>

namespace trace_api = opentelemetry::trace;

namespace fixpp::otel {

// ── helpers ──────────────────────────────────────────────────────────────────

namespace {

// Build a SpanContext from our fixpp::otel::trace_context 32-byte POD.
// Returns an invalid SpanContext for a default-constructed (all-zero) parent.
// Note: trace_context stores bytes as std::byte; cast to uint8_t* for OTel APIs.
trace_api::SpanContext to_span_context(const fixpp::otel::trace_context& tc) {
    // All-zero trace_id ⇒ invalid context (root span).
    bool all_zero = true;
    for (auto b : tc.trace_id) {
        if (b != std::byte{0}) {
            all_zero = false;
            break;
        }
    }
    if (all_zero) return trace_api::SpanContext::GetInvalid();

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto* tid_u8 = reinterpret_cast<const uint8_t*>(tc.trace_id.data());
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto* sid_u8 = reinterpret_cast<const uint8_t*>(tc.span_id.data());

    trace_api::TraceId trace_id{opentelemetry::nostd::span<const uint8_t, 16>{tid_u8, 16}};
    trace_api::SpanId span_id{opentelemetry::nostd::span<const uint8_t, 8>{sid_u8, 8}};
    // Treat flags bit-0 as the sampled flag (W3C trace-flags).
    bool sampled = (static_cast<uint8_t>(tc.flags) & 0x01U) != 0U;
    trace_api::TraceFlags flags{sampled ? trace_api::TraceFlags::kIsSampled
                                        : static_cast<uint8_t>(0)};
    return trace_api::SpanContext{trace_id, span_id, flags, /*remote=*/true};
}

// Extract our fixpp::otel::trace_context from a live OTel SpanContext.
fixpp::otel::trace_context from_span_context(const trace_api::SpanContext& sc) {
    fixpp::otel::trace_context tc{};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    auto* tid_u8 = reinterpret_cast<uint8_t*>(tc.trace_id.data());
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    auto* sid_u8 = reinterpret_cast<uint8_t*>(tc.span_id.data());
    sc.trace_id().CopyBytesTo(opentelemetry::nostd::span<uint8_t, 16>{tid_u8, 16});
    sc.span_id().CopyBytesTo(opentelemetry::nostd::span<uint8_t, 8>{sid_u8, 8});
    tc.flags = static_cast<std::uint8_t>(sc.trace_flags().flags());
    return tc;
}

// Build StartSpanOptions that explicitly sets parent to a SpanContext.
// [const §XIII.3]: never use Scope.
trace_api::StartSpanOptions make_child_opts(const trace_api::SpanContext& parent_ctx) {
    trace_api::StartSpanOptions opts;
    opts.parent = parent_ctx;  // SpanContext variant — explicit parent
    return opts;
}

// Record latency_ns on a span (positive by design — at least 1 ns).
void record_latency(opentelemetry::nostd::shared_ptr<trace_api::Span>& span,
                    std::chrono::steady_clock::time_point start) {
    const auto now = std::chrono::steady_clock::now();
    const auto ns = std::max(
        INT64_C(1), std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count());
    span->SetAttribute("latency_ns", static_cast<int64_t>(ns));
}

}  // namespace

// ── ParseSpan ────────────────────────────────────────────────────────────────

ParseSpan::ParseSpan(opentelemetry::nostd::shared_ptr<trace_api::Tracer> tracer,
                     const trace_api::SpanContext& session_ctx)
    : start_time_(std::chrono::steady_clock::now()) {
    auto opts = make_child_opts(session_ctx);
    span_ = tracer->StartSpan("fixpp.session.parse", opts);
}

ParseSpan::~ParseSpan() {
    if (!span_) return;
    record_latency(span_, start_time_);
    if (error_) {
        span_->SetStatus(trace_api::StatusCode::kError, error_description_);
    } else {
        span_->SetStatus(trace_api::StatusCode::kOk);
    }
    span_->End();
}

void ParseSpan::set_msg_type(std::string_view msg_type) {
    if (span_)
        span_->SetAttribute("fixpp.msg_type",
                            opentelemetry::nostd::string_view{msg_type.data(), msg_type.size()});
}

void ParseSpan::set_error(std::string_view description) {
    error_ = true;
    error_description_ = std::string{description};
}

// ── StoreSpan ─────────────────────────────────────────────────────────────────

StoreSpan::StoreSpan(opentelemetry::nostd::shared_ptr<trace_api::Tracer> tracer,
                     const trace_api::SpanContext& session_ctx)
    : start_time_(std::chrono::steady_clock::now()) {
    auto opts = make_child_opts(session_ctx);
    span_ = tracer->StartSpan("fixpp.session.store", opts);
}

StoreSpan::~StoreSpan() {
    if (!span_) return;
    record_latency(span_, start_time_);
    if (error_) {
        span_->SetStatus(trace_api::StatusCode::kError, error_description_);
    } else {
        span_->SetStatus(trace_api::StatusCode::kOk);
    }
    span_->End();
}

void StoreSpan::set_seq_num(std::int64_t seq_num) {
    if (span_) span_->SetAttribute("fixpp.seq_num", seq_num);
}

void StoreSpan::set_error(std::string_view description) {
    error_ = true;
    error_description_ = std::string{description};
}

// ── DispatchSpan ──────────────────────────────────────────────────────────────

DispatchSpan::DispatchSpan(opentelemetry::nostd::shared_ptr<trace_api::Tracer> tracer,
                           const trace_api::SpanContext& session_ctx)
    : start_time_(std::chrono::steady_clock::now()) {
    auto opts = make_child_opts(session_ctx);
    span_ = tracer->StartSpan("fixpp.session.dispatch", opts);
}

DispatchSpan::~DispatchSpan() {
    if (!span_) return;
    record_latency(span_, start_time_);
    if (error_) {
        span_->SetStatus(trace_api::StatusCode::kError, error_description_);
    } else {
        span_->SetStatus(trace_api::StatusCode::kOk);
    }
    span_->End();
}

void DispatchSpan::set_msg_type(std::string_view msg_type) {
    if (span_)
        span_->SetAttribute("fixpp.msg_type",
                            opentelemetry::nostd::string_view{msg_type.data(), msg_type.size()});
}

void DispatchSpan::set_error(std::string_view description) {
    error_ = true;
    error_description_ = std::string{description};
}

// ── SessionSpans ──────────────────────────────────────────────────────────────

SessionSpans::SessionSpans(TracerProvider& provider, std::string_view sender_comp_id,
                           std::string_view target_comp_id,
                           const fixpp::otel::trace_context& parent_ctx) {
    tracer_ = provider.get_tracer("fixpp.session");

    trace_api::StartSpanOptions opts;
    opts.parent = to_span_context(parent_ctx);  // explicit parent [const §XIII.3]

    session_span_ = tracer_->StartSpan("fixpp.session.lifecycle", opts);
    session_span_->SetAttribute(
        "fixpp.session.sender_comp_id",
        opentelemetry::nostd::string_view{sender_comp_id.data(), sender_comp_id.size()});
    session_span_->SetAttribute(
        "fixpp.session.target_comp_id",
        opentelemetry::nostd::string_view{target_comp_id.data(), target_comp_id.size()});

    session_ctx_ = session_span_->GetContext();
}

SessionSpans::~SessionSpans() {
    if (session_span_) {
        session_span_->SetStatus(trace_api::StatusCode::kOk);
        session_span_->End();
    }
}

fixpp::otel::trace_context SessionSpans::session_trace_context() const noexcept {
    return from_span_context(session_ctx_);
}

ParseSpan SessionSpans::make_parse_span() const { return ParseSpan{tracer_, session_ctx_}; }

StoreSpan SessionSpans::make_store_span() const { return StoreSpan{tracer_, session_ctx_}; }

DispatchSpan SessionSpans::make_dispatch_span() const {
    return DispatchSpan{tracer_, session_ctx_};
}

}  // namespace fixpp::otel
