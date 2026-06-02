// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/otel/session_spans.hpp
//
// SessionSpans — standalone OTel tracing helper for FIX session lifecycle and
// per-message spans (FR-016 / OBS-001).
//
// STANDALONE HELPER: this type is NOT wired into the live session FSM.  FSM
// wiring is deferred to the session-module feature (anchor §11 hand-off).
// TS-12 runs against a test/mock session.
//
// Parent span: constructed on SessionSpans ctor, ended on dtor.
// Child spans: ParseSpan / StoreSpan / DispatchSpan — constructed on their
// own ctors, ended on their own dtors (each records latency_ns + OK/ERROR).
//
// CRITICAL INVARIANT [const §XIII.3]:
//   All spans are started with StartSpanOptions{.parent = <SpanContext>}.
//   opentelemetry::trace::Scope is NEVER used anywhere in this module.
//   Parent span_id is correct even when a child is constructed on a different
//   OS thread (TS-12 cross-thread parenting assertion).
//
// Anchor: .specify/2k-log-otel.md §4.9; contracts/otel-surface.md §SessionSpans.
#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

#include <fixpp/otel/trace_context.hpp>  // fixpp::otel::trace_context

// OTel API types needed for public signatures.
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/trace/tracer.h>
#include <opentelemetry/trace/span.h>
#include <opentelemetry/trace/span_context.h>
#include <opentelemetry/trace/span_startoptions.h>
#include <opentelemetry/trace/span_metadata.h>

namespace fixpp::otel {

// Forward declaration — see providers.hpp.
class TracerProvider;

// ── Child span RAII wrappers ─────────────────────────────────────────────────
//
// Each child span is EXPLICITLY parented to the SessionSpans session span via
// StartSpanOptions{.parent = session_ctx_} — never via Scope.
//
// On destruction: records latency_ns attribute (> 0) + sets OK or ERROR status.

// ParseSpan: wraps the per-message parse phase.
class ParseSpan {
public:
    // Start a parse child span explicitly parented to session_ctx.
    // start_time defaults to now.
    ParseSpan(
        opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> tracer,
        const opentelemetry::trace::SpanContext& session_ctx);

    // End the span: record latency_ns + set status.  Safe to call multiple times
    // (subsequent calls are no-ops — span::End is idempotent per OTel spec).
    ~ParseSpan();

    ParseSpan(const ParseSpan&)            = delete;
    ParseSpan& operator=(const ParseSpan&) = delete;
    ParseSpan(ParseSpan&&)                 = default;
    ParseSpan& operator=(ParseSpan&&)      = default;

    // Set optional message type attribute (e.g. "D", "8", "0").
    void set_msg_type(std::string_view msg_type);
    // Mark the parse as failed (sets ERROR status on End).
    void set_error(std::string_view description = "parse error");

    // Direct access to the underlying span (for testing).
    [[nodiscard]] opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span>
    span() const noexcept { return span_; }

private:
    opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span> span_;
    std::chrono::steady_clock::time_point start_time_;
    bool error_{false};
    std::string error_description_;
};

// StoreSpan: wraps the per-message store phase.
class StoreSpan {
public:
    StoreSpan(
        opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> tracer,
        const opentelemetry::trace::SpanContext& session_ctx);

    ~StoreSpan();

    StoreSpan(const StoreSpan&)            = delete;
    StoreSpan& operator=(const StoreSpan&) = delete;
    StoreSpan(StoreSpan&&)                 = default;
    StoreSpan& operator=(StoreSpan&&)      = default;

    void set_seq_num(std::int64_t seq_num);
    void set_error(std::string_view description = "store error");

    [[nodiscard]] opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span>
    span() const noexcept { return span_; }

private:
    opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span> span_;
    std::chrono::steady_clock::time_point start_time_;
    bool error_{false};
    std::string error_description_;
};

// DispatchSpan: wraps the per-message dispatch phase.
class DispatchSpan {
public:
    DispatchSpan(
        opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> tracer,
        const opentelemetry::trace::SpanContext& session_ctx);

    ~DispatchSpan();

    DispatchSpan(const DispatchSpan&)            = delete;
    DispatchSpan& operator=(const DispatchSpan&) = delete;
    DispatchSpan(DispatchSpan&&)                 = default;
    DispatchSpan& operator=(DispatchSpan&&)      = default;

    void set_msg_type(std::string_view msg_type);
    void set_error(std::string_view description = "dispatch error");

    [[nodiscard]] opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span>
    span() const noexcept { return span_; }

private:
    opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span> span_;
    std::chrono::steady_clock::time_point start_time_;
    bool error_{false};
    std::string error_description_;
};

// ── SessionSpans ─────────────────────────────────────────────────────────────
//
// Lifecycle span wrapping a single FIX session: started on ctor, ended on dtor.
// Attrs: fixpp.session.sender_comp_id, fixpp.session.target_comp_id.
//
// Provides session_trace_context() for callers that need the W3C trace-context
// to correlate log records with this session's span (F4 / T041).
//
// Child spans (ParseSpan/StoreSpan/DispatchSpan) are created via the factory
// methods; all explicitly parent to the session span context.

class SessionSpans {
public:
    // Ctor: start the lifecycle span.
    //   provider         — the TracerProvider to obtain tracers from.
    //   sender_comp_id   — FIX SenderCompID (49).
    //   target_comp_id   — FIX TargetCompID (56).
    //   parent_ctx       — optional parent (e.g. an upstream request context).
    //                      Pass a default-constructed trace_context for a root span.
    SessionSpans(
        TracerProvider& provider,
        std::string_view sender_comp_id,
        std::string_view target_comp_id,
        const fixpp::otel::trace_context& parent_ctx = fixpp::otel::trace_context{});

    // Dtor: End the lifecycle span with OK status.
    ~SessionSpans();

    SessionSpans(const SessionSpans&)            = delete;
    SessionSpans& operator=(const SessionSpans&) = delete;
    // Move: transfers ownership; original's session_span_ is cleared.
    SessionSpans(SessionSpans&&)                 = default;
    SessionSpans& operator=(SessionSpans&&)      = default;

    // Returns the W3C trace context (trace_id + span_id + flags) of the
    // session lifecycle span — used by FIXPP_SLOG macros (F4 / contracts).
    [[nodiscard]] fixpp::otel::trace_context session_trace_context() const noexcept;

    // Borrowed tracer — lifetime bound to *this.
    [[nodiscard]] opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer>
    tracer() const [[clang::lifetimebound]] { return tracer_; }

    // Direct access to the underlying lifecycle span (for testing).
    [[nodiscard]] opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span>
    session_span() const noexcept { return session_span_; }

    // ── Child span factories ─────────────────────────────────────────────────
    // Each factory creates a child span explicitly parented to the session span.
    // [const §XIII.3]: parent is set via StartSpanOptions{.parent = session_ctx_},
    // never via opentelemetry::trace::Scope.

    [[nodiscard]] ParseSpan    make_parse_span()    const;
    [[nodiscard]] StoreSpan    make_store_span()    const;
    [[nodiscard]] DispatchSpan make_dispatch_span() const;

private:
    opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> tracer_;
    opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span>   session_span_;
    // SpanContext has no default constructor; initialize to invalid state.
    // SpanContext(false, false) = sampled_flag=false, is_remote=false.
    opentelemetry::trace::SpanContext session_ctx_{false, false};
};

}  // namespace fixpp::otel
