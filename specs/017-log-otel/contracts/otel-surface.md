# Contract: otel surface (providers / SessionSpans / exporters / dual export)

**Anchor**: `.specify/2k-log-otel.md` §4.8–§4.10. Locked surface; testable obligations restated for TDD. The `otel` module depends only on `core` + `log` (`[arch §2.3]`); OTLP transport is the OTel SDK's own, never `fixpp::transport`.

## Headers
`include/fixpp/otel/{trace_context,providers,session_spans,exporters}.hpp`; impls in `src/otel/`.

## `TracerProvider` / `MeterProvider` (FR-019, OBS-001/002)
- `OtelResourceAttributes{service_name, service_version, deployment_environment, extra[]}`; `OtelConfig{endpoint, use_grpc=false, resource, cert_source (null⇒no TLS), export_interval=60s, export_timeout=30s}`.
- `TracerProvider(OtelConfig)`: `get_tracer(string_view) const` (lifetimebound, borrowed), `shutdown()` (flush at `Engine::close()`). `MeterProvider(OtelConfig)`: `get_meter(string_view) const` (lifetimebound), `shutdown()`.
- **Thin RAII wrappers** over the official OTel SDK — no re-implementation of the OTel data model (FR-019). On provider-init failure ⇒ `otel_provider_init_failed` (core slot 128; C-ABI map 1011), engine substitutes a **no-op provider** and continues. OTLP export retries capped. SDK internals not under PMR (documented caveat, §8).
- Owned by `EngineConfig` as `shared_ptr` (`fixpp::otel::TracerProvider` / `MeterProvider` — the types `EngineConfig` forward-declares; 017 defines them).

## `SessionSpans` (FR-016, OBS-001, TS-12) — standalone helper; FSM wiring deferred (clarified boundary 1)
- Ctor `(TracerProvider& provider, string_view sender_comp_id, string_view target_comp_id, trace_context const& parent_ctx)`. Lifecycle span starts on construction, ends on destruction; attrs `fixpp.session.sender_comp_id` / `target_comp_id`. `session_trace_context() const noexcept`; `tracer() const` (lifetimebound).
- Inner RAII children: `ParseSpan` (`set_msg_type`, `set_error`), `StoreSpan` (`set_seq_num`, `set_error`), `DispatchSpan` (`set_msg_type`, `set_error`) — start on construction, on destruction set OK/ERROR status + a `*.latency_ns` attribute > 0.
- **Critical invariant**: sub-spans use the **explicit-parent-context API** (`StartSpanOptions{.parent = session_ctx_}`), **NOT** `opentelemetry::trace::Scope` — `Scope` mutates thread-local SDK context, prohibited by `[const §XIII.3]`. Parent span_id is correct even when a child is constructed on a different OS thread (TS-12 asserts parent-child + OK status + positive parse latency).
- **OUT OF SCOPE here**: constructing `SessionSpans` in the live session-FSM open path / emitting parse/store/dispatch from the message coroutine (deferred to the session-module feature, anchor §11). TS-12 runs against a test/mock session.

## Metric exporters + dual export (FR-017, OBS-002, TS-11)
- `PrometheusConfig{host="0.0.0.0", port=9464, metrics_path="/metrics", cert_source?}`. `PrometheusExporter` = SDK `MetricReader` (pull; embedded single-threaded non-asio HTTP server); `sdk_reader()` (lifetimebound) → register via `AddMetricReader()`. **Do NOT** pass to `PeriodicExportingMetricReader`.
- `OtlpMetricExporter(OtelConfig)` = `PushMetricExporter` wrapped in `PeriodicExportingMetricReader`; `sdk_reader()` → `AddMetricReader()`.
- `OtelDualExportBuilder::with_prometheus(cfg).with_otlp(cfg).build() → shared_ptr<MeterProvider>`: **two `AddMetricReader()` calls on one MeterProvider** (no `MultiMetricExporter` — incompatible base types, R1). A single `meter.add(counter, 1)` propagates to both readers (TS-11: counter==3 visible via `GET :9464/metrics` AND received by mock OTLP push).

## `trace_context` (otel/trace_context.hpp)
- Confirm/alias `fixpp::otel::trace_context` over the existing `fixpp::core::trace_context` (16-B trace_id + 8-B span_id + 1-B flags + pad = 32 B, `[2d §1.2]`). Used by `EngineConfig`/`SessionConfig` and the `FIXPP_SLOG`/`FIXPP_ELOG` macros.
