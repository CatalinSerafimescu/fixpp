# Feature Specification: Async Logger + OTel Observability Surface

**Feature Branch**: `017-log-otel`
**Created**: 2026-06-02
**Status**: Draft
**Design anchor**: `.specify/2k-log-otel.md` v0.5 (Gate-A converged 2026-05-09) — authoritative; this spec carves its surface and MUST NOT re-litigate decisions closed there.
**Catalogue rows owned**: LOG-001, LOG-002, LOG-003, LOG-004, OBS-001, OBS-002, OBS-003.

## User Scenarios & Testing *(mandatory)*

This is a library-infrastructure feature; the "users" are (a) the **engine/session code** that emits diagnostics on the hot path, and (b) the **operator** running a fixpp engine who needs production observability. Value: structured diagnostics and OpenTelemetry traces/metrics/logs **without compromising the zero-allocation, bounded-latency hot path** that the rest of fixpp guarantees.

### User Story 1 - Zero-overhead structured async logging with pluggable sinks (Priority: P1)

Engine and session code emits leveled, categorized log records from any thread — including a session-strand coroutine on the message hot path — and those records are formatted and written off the hot path by a dedicated drain thread to one or more sinks (file, syslog, OTLP), with overflow bounded and never allocating or blocking the producer.

**Why this priority**: This is the MVP — the producer/consumer logger core (LOG-001), the sink plugin interface + three default sinks (LOG-002), and compile-time + runtime filtering (LOG-004). Without it there is no logging surface at all, and every other story builds on the `Record`/`Logger`/`Sink` machinery.

**Independent Test**: Construct a `Logger` with a capturing sink, emit records at varying levels/categories from one and from several producer threads, and confirm (a) the producer path performs no allocation and does not block, (b) records are delivered to the sink off the producer thread, (c) below-threshold and filtered-out records never reach the sink, and (d) overflow drops the newest records while preserving the oldest in-flight ones with an exact drop count.

**Acceptance Scenarios**:

1. **Given** a `Logger` built with `FIXPP_LOG_MIN_LEVEL=warn`, **When** the source contains `debug`/`info` call sites, **Then** those call sites contribute zero instructions and zero format strings to the binary and never allocate.
2. **Given** a `Logger` with a bounded ring at capacity and a paused drain, **When** more records are emitted than fit, **Then** the newest records are dropped, the oldest in-flight records are preserved, and `drop_count()` reflects the exact number dropped.
3. **Given** a category disabled at runtime, **When** a record in that category is emitted at an enabled level, **Then** it is filtered (counted in `filter_count()`), not delivered, and does not affect `drop_count()`.
4. **Given** a `FileSink` configured with a size bound and keep-count, **When** records exceed the size bound, **Then** the file rotates, the oldest file is deleted, and total files never exceed the keep-count.
5. **Given** a sink whose `emit`/`flush` throws, **When** the drain thread processes records, **Then** the exception is caught, a per-sink error counter increments, and the drain thread and other sinks continue.

### User Story 2 - Trace-correlated log records (Priority: P2)

Every log record carries the OpenTelemetry `trace_id`/`span_id` of its originating context so that operators can correlate a log line with the distributed trace of the session/operation that produced it, with the context acquired explicitly (never via `thread_local`).

**Why this priority**: Correlation (LOG-003) is what makes the logs useful in a traced deployment, but the logger is functional without it; depends on US1's `Record` carrying the trace fields.

**Independent Test**: Emit from each of the three context tiers — a session strand with a known trace context, an engine scope with a known engine trace context, and a context-free site — and assert each delivered record carries the expected `trace_id`/`span_id` (or all-zeros for the context-free site).

**Acceptance Scenarios**:

1. **Given** a session opened with a known trace context, **When** code logs via the session-strand macro passing `session.get_trace_context()`, **Then** the delivered record carries that session's `trace_id`/`span_id`.
2. **Given** an engine with a known engine trace context, **When** control-plane code logs via the engine-scope macro, **Then** the record carries the engine's root span IDs.
3. **Given** a context-free site (destructor/static init), **When** code logs via the zero-context macro, **Then** the record carries all-zero `trace_id`/`span_id` with no panic or undefined behavior.

### User Story 3 - OpenTelemetry observability export: traces, metrics, logs (Priority: P3)

An operator configures OTel exporters so the engine emits session/parse/store/dispatch **traces**, dual-exports **metrics** (a Prometheus pull endpoint and an OTLP push), and exports **logs** over OTLP through the same sink interface — all wrapping the official OpenTelemetry C++ SDK.

**Why this priority**: Production observability (OBS-001/002/003) is high-value but layered on the logging core and the trace fields; an operator with only file/syslog logging still has a working engine.

**Scope boundary**: 017 delivers the `SessionSpans` helper and its parse/store/dispatch child-span types as standalone `otel`-module components, verified against a test/mock session (TS-12). Constructing `SessionSpans` in the live session-FSM open path and emitting these spans from the real message-processing coroutine is **deferred to the future session-module feature** (anchor §11 hand-off) — 017 does not edit the session FSM.

**Independent Test**: With mock OTel exporters, run a session lifecycle and assert (a) a session-lifecycle span with child parse/store/dispatch spans correctly parented, (b) a counter metric is simultaneously readable via the Prometheus scrape endpoint and received by the OTLP push exporter, and (c) a log record is exported as an OTLP `LogRecord` with matching severity/trace/body.

**Acceptance Scenarios**:

1. **Given** a `SessionSpans` helper over an open session, **When** a parse span is created and destroyed within the session, **Then** the exporter receives a session span and a parse child span whose parent is the session span, both with OK status and a positive latency attribute.
2. **Given** a `MeterProvider` configured with both a Prometheus reader and an OTLP push reader, **When** a counter is incremented and an export cycle runs, **Then** the value is visible via the Prometheus endpoint **and** received by the OTLP exporter.
3. **Given** an `OtlpLogSink` registered as a sink, **When** a record is emitted, **Then** it is exported once as an OTLP `LogRecord` (no double-write path) with matching timestamp, severity, trace/span IDs, and formatted body.

### Edge Cases

- **Queue overflow** under a slow/stalled drain → `drop_newest`, exact `drop_count()`, no data race on the ring sequence (TSan-clean).
- **`block` overflow mode** used from a session-strand coroutine → prohibited (equivalent to holding a mutex in a coroutine); allowed only from dedicated non-coroutine producer threads.
- **Drain timeout** on shutdown → `shutdown(drain_timeout)` returns the timeout error and increments `timeout_drop_count()` (tracked separately from overflow `drop_count()`).
- **Sink `open()` failure** at startup → that sink is disabled, the logger continues with the remaining sinks.
- **OTLP export failure** → bounded retries (cap), then drop with an export-failure counter; no retry storm; engine continues with a no-op provider on provider-init failure.
- **Context-free log site** → all-zero trace/span, treated as uncorrelated.
- **Clock injection** → record timestamps come from the effective clock, so a mock clock makes time-sensitive log output deterministic in tests.

## Clarifications

### Session 2026-06-02

- Q: Does 017 wire SessionSpans into the real session FSM, or only deliver the helper type tested standalone? → A: Helper only — 017 builds `SessionSpans` + the parse/store/dispatch child-span types in the `otel` module, verified by TS-12 against a test/mock session; the real session-FSM open-path + message-coroutine wiring is deferred to the future session-module feature (anchor §11 hand-off).
- Q: Does 017 own adding the OpenTelemetry C++ SDK build dependency (Conan + CMake + the two CMake options), or assume the scaffold already exists? → A: 017 owns its build wiring — pin OTel C++ SDK ≥ 1.12 in `conanfile.py`, add CMake link targets and the `FIXPP_LOG_MIN_LEVEL` / `FIXPP_LOG_SPIKE_QUILL` options (quill recipe optional/spike-only); 017 must be independently buildable and testable.
- Q: Is the shippable v1.0 backend the own MPSC ring now (spike validates), or does 017 gate the backend on the spike outcome? → A: Own ring now — implement the own Vyukov lock-free MPSC ring as the v1.0 backend (anchor §1.2 provisional rec + §D.2); TS-13 is a non-blocking validation/benchmark harness that records the decision and could justify a later swap behind the identical `Logger` facade.

## Requirements *(mandatory)*

### Functional Requirements

**Producer / async core (LOG-001)**

- **FR-001**: The producer log path MUST perform zero heap allocation per record, emit no exceptions across the queue boundary, take no lock/`std::mutex`, and make no system call — on every supported fill rate.
- **FR-002**: The producer enqueue path MUST meet a mean/p99 latency ceiling on the reference CI hardware (anchor §6.2: ≤ 50 ns mean non-overflow; TS-9 asserts p99 ≤ 50 ns).
- **FR-003**: The queue MUST be a bounded N-producer/1-consumer ring with a configurable capacity; records beyond capacity are handled by the overflow policy and never grow memory unboundedly.
- **FR-004**: Overflow policy MUST default to `drop_newest` (preserving the oldest in-flight records), incrementing an atomic `drop_count()`. A `block` policy MUST exist but MUST be documented as prohibited from session-strand coroutines.
- **FR-005**: A dedicated drain OS thread (NOT an ASIO strand thread, holding no session/engine references) MUST format records and fan them out to sinks; it MUST wrap each sink call in a catch-all, increment a per-sink error counter on exception, and continue.
- **FR-006**: Record timestamps MUST be sourced from the effective clock (`SessionConfig::clock_override ?: EngineConfig::clock`), so a mock clock deterministically controls log timestamps in tests.

**Sink interface + default sinks (LOG-002)**

- **FR-007**: The `Sink` interface MUST expose exactly 4 pure-virtual methods (`open`, `emit`, `flush(deadline)`, `close`), within the `[const §XIV.2]` ≤ 5 cap.
- **FR-008**: Three default sinks MUST ship: `FileSink` (size-bounded rotation with keep-count + deadline-bounded async fsync on the drain thread), `OtlpLogSink` (OTLP log export), `SyslogSink` (POSIX syslog).
- **FR-009**: `FileSink` disk usage MUST be bounded by `max_file_bytes × max_keep_count`; on rotation the oldest file is deleted before exceeding the keep-count.

**Filtering (LOG-004)**

- **FR-010**: A compile-time minimum level (`FIXPP_LOG_MIN_LEVEL` + `if constexpr`) MUST eliminate below-threshold call sites entirely (zero instructions, zero `.rodata` format strings, zero allocation).
- **FR-011**: A runtime per-category filter (atomic bitmask) MUST drop disabled-category records before sink delivery, counted in a `filter_count()` separate from `drop_count()`.

**Trace correlation (LOG-003)**

- **FR-012**: Every `Record` MUST carry `trace_id` (16 bytes) and `span_id` (8 bytes) as plain fields, with no additional allocation for correlation.
- **FR-013**: Three macros MUST provide the three context tiers without any `thread_local`: `FIXPP_SLOG(lvl, tc, cat, fmt, ...)` (session strand; caller passes an explicit `tc = session.get_trace_context()`), `FIXPP_ELOG(lvl, engine, cat, fmt, ...)` (engine scope; reads an atomic engine trace-context snapshot), `FIXPP_LOG0(lvl, cat, fmt, ...)` (zeroed context).

**Shutdown / errors**

- **FR-014**: `Logger::shutdown(drain_timeout)` MUST be `[[nodiscard]] expected_t<void>`; on timeout it returns `log_drain_timeout` and increments a `timeout_drop_count()` tracked separately from overflow `drop_count()`. `Engine::close()` MUST flush sinks and shut down the providers.
- **FR-015**: The feature MUST introduce exactly the 7 error variants in the reserved `[1000, 1099]` block: `log_queue_overflow` (1000), `log_sink_open_failed` (1001), `log_sink_write_failed` (1002), `log_sink_flush_failed` (1003), `log_drain_timeout` (1004), `otel_export_failed` (1010), `otel_provider_init_failed` (1011). No other slot in the block is consumed in v1.0.

**OTel observability (OBS-001/002/003)**

- **FR-016**: `SessionSpans` MUST provide a session-lifecycle span with `ParseSpan`/`StoreSpan`/`DispatchSpan` children parented by **explicit** context (no thread-local scope), carrying CompID/latency/msg-type/status attributes. 017 ships `SessionSpans` as a standalone `otel`-module helper verified against a test/mock session (TS-12); wiring it into the live session FSM open path and message coroutine is deferred to the future session-module feature (anchor §11) and is OUT OF SCOPE here.
- **FR-017**: Metrics MUST dual-export from one `MeterProvider`: a `PrometheusExporter` pull reader (embedded server, port 9464) **and** an `OtlpMetricExporter` push reader, registered via two `AddMetricReader()` calls.
- **FR-018**: `OtlpLogSink` MUST be the LOG-002 `Sink` combined with an OTel batch log processor over a single write path (no double-write), satisfying `[const §XIII.4]`.
- **FR-019**: OTel wrappers MUST wrap the official OpenTelemetry C++ SDK (no re-implementation); on provider-init failure the engine substitutes a no-op provider and continues; OTLP export retries MUST be capped.

**Boundaries / deferrals (keep as placeholders; DO NOT implement)**

- **FR-020**: v1.0 MUST expose log/OTel access via the C++ API only; `c_api/log.h` and `c_api/otel.h` contain version-macro/include-guard placeholders with **no** `extern "C"` symbols.
- **FR-021**: `GrpcStreamSink` MUST remain deferred (anchor §10 Q1; AGPL boundary). The v1.0 shippable backend MUST be the **own** Vyukov lock-free MPSC ring (anchor §1.2 provisional recommendation + §D.2); the quill-vs-own disposition stays PROVISIONAL but TS-13 is a **non-blocking** validation/benchmark harness — it records the decision and could justify a later swap, but MUST NOT gate delivery of the own-ring `Logger`. The public `Logger` facade contract is identical regardless of backend.
- **FR-022**: The following are explicit non-goals and MUST NOT be built: synchronous logging shim, log aggregation/routing/sampling, custom OTel SDK, W3C TraceContext injection into outbound FIX messages, structured-logging query language, `dlopen` sink discovery.

**Build integration (this feature owns its scaffold)**

- **FR-023**: 017 MUST be independently buildable and testable on its own branch: it MUST add the OpenTelemetry C++ SDK (pinned ≥ 1.12, the first stable logs API; exact version pinned in `conanfile.py` per `[const §XV.17]`) as a build dependency with the CMake link wiring for the `otel`/`log` targets and tests, and MUST introduce the `FIXPP_LOG_MIN_LEVEL` and `FIXPP_LOG_SPIKE_QUILL` CMake options. The `quill` Conan recipe is OPTIONAL and gated behind `FIXPP_LOG_SPIKE_QUILL=ON` (spike-only); a default build (`FIXPP_LOG_SPIKE_QUILL=OFF`) MUST NOT require quill.

### Key Entities *(include if feature involves data)*

- **Logger** — owns the MPSC ring, drain thread, overflow/drop/filter/timeout counters, and `shutdown()`; constructed from `LoggerConfig` (capacity, overflow policy, drain timeout, drain CPU affinity, optional PMR resource).
- **Record** — the fixed 256-byte slot: timestamp, `Level`, `Category`, format id, packed `ArgValue` slots, and `trace_id`/`span_id`.
- **ArgValue** — tagged union (`InlineStr<15>` no-null + `StaticStr` + numeric variants), `sizeof == 24`.
- **Level / Category** — leveled severity + categorized channel for compile-time and runtime filtering.
- **Sink** (+ **FileSink**, **OtlpLogSink**, **SyslogSink**) — the 4-method plugin interface and its three default implementations.
- **TracerProvider / MeterProvider** — thin wrappers over the OTel SDK providers.
- **SessionSpans** — RAII helper producing the session-lifecycle span and parse/store/dispatch children.
- **PrometheusExporter / OtlpExporter** — the dual metric export readers.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: On the reference CI hardware, producer enqueue p99 latency is ≤ 50 ns (TS-9), with **zero** allocations on the producer path at 10%/50%/95% fill rates under the malloc interceptor.
- **SC-002**: Under overflow with a stalled drain, exactly the oldest in-flight records are retained and `drop_count()` equals the number of records beyond capacity (TS-2), TSan-clean.
- **SC-003**: With `FIXPP_LOG_MIN_LEVEL` above a call site's level, that site contributes zero format strings to the binary and zero runtime cost (TS-1); a disabled runtime category is filtered and counted without touching `drop_count()` (TS-8).
- **SC-004**: Every record emitted from a session-strand site carries that session's `trace_id`/`span_id`; engine-scope sites carry the engine root IDs; context-free sites carry all-zeros — verified across the three macros (TS-6/TS-7).
- **SC-005**: A counter metric is simultaneously observable via the Prometheus scrape endpoint (port 9464) and received by the OTLP push exporter in one export cycle (TS-11); a log record exports once as a matching OTLP `LogRecord` (TS-10).
- **SC-006**: A `SessionSpans` run yields a session span with a correctly parented parse child span, both OK, with a positive parse-latency attribute (TS-12).
- **SC-007**: `shutdown(drain_timeout)` drains all in-flight records within the timeout, or returns `log_drain_timeout` with an accurate `timeout_drop_count()` and a `drop_count()` that still reflects only overflow drops.
- **SC-008**: All 13 test seams (TS-1…TS-13) are present and pass (TS-13 is the quill-vs-own spike harness; its execution records the backend decision but does not gate the facade).

## Assumptions

- The "reference CI hardware" and latency baselines are those defined in `bench/baselines/` (TS-9 baseline `bench/baselines/log_enqueue.json`); SC-001's 50 ns is relative to that baseline.
- The OpenTelemetry C++ SDK is available at ≥ 1.12 (first stable logs API); 017 owns adding and pinning it (FR-023) rather than depending on an external build-scaffold step — the anchor's "Phase 3 build scaffold" label (§10 Q3/Q5, §11) predates the current phasing.
- This feature **consumes** (does not own) `Session::get_trace_context()` / `session_local<trace_context>` and the clock from 2d (`[2d §4.6]`/`[2d §7.9]`); the `EngineConfig`/`SessionConfig` observability fields follow the 2d engine-anchor + session-override pattern (anchor App D §D.1).
- The v1.0 backend is the own lock-free MPSC ring (FR-021); the backend is an implementation detail behind the `Logger` facade, the spec's contract holds for either, and TS-13 validates (does not gate) the PROVISIONAL disposition.
- C-ABI log/OTel exposure and `StreamLogs` log-stream integration are v1.x cross-doc amendment targets (anchor §10 Q1), not part of this feature.
