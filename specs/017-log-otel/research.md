# Phase 0 Research: Async Logger + OTel Observability Surface

**Feature**: 017-log-otel | **Date**: 2026-06-02
**Anchor**: `.specify/2k-log-otel.md` v0.5 (Gate-A converged). The anchor closed every *design* decision (API surface, Record/ArgValue layout, overflow policy, three-macro model, error block, PMR placement, all 13 test seams). This document resolves only the **implementation unknowns** that remain after the design is fixed, plus the four areas the anchor's open questions (§10) and the clarify session flagged for this feature.

There are **no `[NEEDS CLARIFICATION]` markers** in the spec; the three scope-boundary ambiguities were resolved at `/speckit-clarify` (2026-06-02) and are recorded in the spec's Clarifications section. The items below are "how", not "what".

---

## R1 — OTel C++ SDK version pin + dual metric export wiring

- **Decision**: Pin **exactly `opentelemetry-cpp/1.16.1`** (a tagged Conan-Center release; ≥ 1.12 for the stable logs API) in `conanfile.py` per `[const §XV.17]`. A scaffold-time re-confirm of the exact patch is a doc-update, not an open pin. Dual export = **one `MeterProvider` with two `MetricReader`s via two `AddMetricReader()` calls**: (1) `PrometheusExporter` (a `MetricReader`, pull model), (2) a `PeriodicExportingMetricReader` wrapping `OtlpMetricExporter` (a `PushMetricExporter`). `OtelDualExportBuilder::build()` performs both registrations; a single `meter.add(counter, 1)` fans out to both readers via the SDK internally.
- **Rationale**: 1.12 is the first SDK with the stable logs API (needed for `OtlpLogSink`/OBS-003). The two-reader pattern is the documented SDK idiom; there is **no** public `MultiMetricExporter` that accepts a `PrometheusExporter` (different base type — `MetricReader` vs `PushMetricExporter`), so the builder must register readers, not wrap exporters (anchor §4.10 mechanics note).
- **Alternatives considered**: (a) `MultiMetricExporter` — rejected: API-incompatible base types. (b) Two separate `MeterProvider`s — rejected: double-counts instruments and breaks the "single `meter.add` propagates to both" contract.
- **Pinned**: `opentelemetry-cpp/1.16.1` (exact). A scaffold-time re-confirm of the Conan-Center patch is a doc-update only (the pin is committed, not open).

## R2 — Embedded Prometheus HTTP server (pull endpoint, port 9464)

- **Decision**: Use the OTel SDK's bundled `PrometheusExporter`, which runs an embedded single-threaded HTTP server exposing `/metrics` on `0.0.0.0:9464`. This server thread is **not** an asio strand thread and does not touch the engine executor. TLS for the endpoint is optional via the `[2g §4.1]` `cert_source` (default plain HTTP for in-pod scraping).
- **Rationale**: lowest-friction, zero external-process dependency, matches the OTel Prometheus receiver convention (9464). The exposition protocol is synchronous pull — a dedicated thread is simpler and correct than retrofitting asio.
- **Alternatives considered**: (a) ASIO-based HTTP server — deferred post-v1.0 (anchor §10 Q4); (b) push-gateway only — rejected as the v1.0 minimum must satisfy OBS-002 scrape.
- **Test approach**: TS-11 does a real `GET http://localhost:9464/metrics` and asserts the counter value in the text exposition, alongside a mock OTLP push exporter receiving the same value.

## R3 — CRC32 format-id registry + deferred formatting

- **Decision**: `FIXPP_FORMAT_ID(fmt)` evaluates to a `constexpr std::uint32_t` = CRC32 of the format literal at the call site (no string crosses the queue boundary). The drain thread resolves `format_id → format string` via a `constexpr`-built registry keyed by CRC32, then formats with `std::vformat` over the `ArgValue` slots. Format strings are interned at compile time into a static table; the producer never touches them.
- **Rationale**: keeps the producer path string-free (zero alloc, deferred formatting per `[const §XIII.2]`). CRC32 of the literal is a stable compile-time key.
- **Risk + mitigation**: CRC32 **collision** between two distinct format literals would mis-resolve a record on the drain side. Mitigation: a build-time/`static_assert`-style collision check over the registered literals (or a debug-build duplicate-id assertion); document the (astronomically low, but non-zero) collision domain. Resolve the exact mechanism in Phase 1 contract (`log-core.md`).
- **Alternatives considered**: (a) pass `const char* fmt` to the producer — rejected: defeats deferred formatting and risks dangling for non-literal fmt; (b) full string hashing (FNV-1a 64-bit) — viable lower-collision alternative; the anchor specifies CRC32, kept for the locked `format_id` (uint32) field. Collision mitigation chosen over a wider hash to preserve the 4-byte `Record::format_id`.

## R4 — Quill-vs-own backend spike (TS-13) — own ring is the v1.0 shipping candidate behind the backend-agnostic facade; disposition PROVISIONAL

- **Decision** (confirmed at clarify, FR-021): implement the **own** Vyukov-style lock-free MPSC ring as the v1.0 **shipping candidate** behind the backend-agnostic `Logger` facade; its disposition stays **PROVISIONAL**. `bench/log_spike.cpp` (TS-13) **executes + records** (does not gate delivery) — a **non-blocking** harness: 4 producer threads, 10M records, capacity 65536, 10/50/95% fill, p99/p999 primary, mallocnesia zero-alloc gate (Criterion A). It records the disposition (own vs quill) in the convergence/verify record but does **not** gate delivery of the own-ring `Logger`. Quill is compiled only under `FIXPP_LOG_SPIKE_QUILL=ON`; a default build never requires quill.
- **Rationale**: the anchor §1.2 provisional recommendation is own-impl; the facade contract is identical regardless of backend, so the spike can run after the core lands without reshaping the API. Gating implementation on the spike would serialize 017 behind quill Conan/infra for no design benefit.
- **Alternatives considered**: gate-on-spike — rejected at clarify (serializes the feature; the facade is backend-agnostic).
- **Quill Conan pin**: `quill/3.9.0` (anchor §10 Q5), pulled only when `FIXPP_LOG_SPIKE_QUILL=ON`.

## R5 — MPSC ring memory-ordering + drain mechanics (TSan-correct)

- **Decision**: `write_sequence_` (`std::atomic<uint64_t>`, producers CAS `acq_rel`) and `read_sequence_` (`std::atomic<uint64_t>`, drain stores `release`, producers load `relaxed`), each `alignas(64)` on its own cache line. Producer = load-check-CAS: load `w`, load `r` (relaxed), if `w-r >= capacity` increment `drop_count_` and return (drop_newest); else CAS `w→w+1`; on success own slot `w%capacity`, `memcpy` the `Record`, publish a per-slot sequence number; drain consumes in `read_sequence_` order and advances with a `release` store.
- **Rationale**: the overflow check **before** claiming the slot guarantees the drain never waits on a claimed-but-unwritten slot (no deadlock, anchor §4.3 key invariant). The relaxed `read_sequence_` load is safe — a stale read only causes an early drop under `drop_newest`. `read_sequence_` MUST be `std::atomic` (not plain `uint64_t`) or TSan fires on the multi-producer fullness read (anchor P1-2 fix).
- **Verified by**: TS-2 (drop_newest, exactly oldest retained, exact drop_count) under TSan; TS-3 (block on a raw thread).
- **Alternatives considered**: Disruptor-style per-slot sequence with no global read counter — equivalent; the anchor pins the dual-counter form.

## R6 — `FileSink` deadline-bounded fsync (no indefinite drain stall)

- **Decision**: `FileSink::flush(deadline)` calls `::fdatasync(fd)` **on the drain thread** with a `deadline` escape (timerfd/`alarm`-based); if `fdatasync` does not return within `deadline`, flush returns silently (records may be lost — recorded). The producer never calls `fsync`. Rotation renames `<base>.log → <base>.<iso8601>.log`, opens a fresh live file, deletes the oldest when count > `max_keep_count`.
- **Rationale**: `fsync(2)` can block arbitrarily on NFS/ext4 under load; the deadline is a mandatory invariant (anchor N-P1-3) to keep `Logger::shutdown(drain_timeout)` bounded.
- **Verified by**: TS-4 (rotation + keep-count), TS-5 (async fsync on drain thread via injected mock `fsync` hook).

## R7 — `OtlpLogSink` non-blocking emit (OBS-003, no double-write)

- **Decision**: `OtlpLogSink::emit()` translates `Record → opentelemetry::logs::LogRecord` (timestamp→TimeUnixNano, level→SeverityNumber/Text, trace_id/span_id→TraceId/SpanId, resolved+formatted body→Body, category→`fixpp.log.category` attribute) and hands it to the SDK `BatchLogRecordProcessor`, which buffers and exports on its own background thread. `emit()` makes **no** gRPC/HTTP call on the drain thread; `flush(deadline)` calls `ForceFlush(deadline)`. The sink IS the single write path (`[const §XIII.4]`, no double-write).
- **Rationale**: keeps the drain thread non-blocking; satisfies "same sink interface backs OTel log export." OTLP retries capped at `max_export_retries` (default 3) with bounded `export_timeout` — no retry storm (anchor §6.5).
- **Verified by**: TS-10 (mock `LogRecordExporter` receives a matching `LogRecord`).

## R8 — Adjacent-module touch surface (confirm what 2d already shipped)

- **Finding** (from the current tree): `EngineConfig` (`include/fixpp/core/engine_config.hpp`) **already** declares `logger`/`tracer`/`meter` `shared_ptr` stubs (lines 127–129) + the `engine_trace_context` seed VALUE (line 157) + the `core::detail::trace_context_snapshot` helper TYPE (a seqlock/atomic `.store()`/`.load()` wrapper, lines 64–114). There is **no** `EngineConfig::engine_trace_context_snapshot` member instance — the identifier appears only in a comment at line 11; 017 adds the publishable snapshot as an `Engine`-held member seeded from the seed value (the `Engine::engine_trace_context()` item of the four-item 2d-surface amendment set; Decision step 5 below). `SessionConfig` **already** has `clock_override` + `initial_trace_context` (value-typed) **and a `log_sink_override` `shared_ptr<log::Sink>`** (`session_config.hpp:182`) that 017 replaces. `Session` **already** holds the `session_local<trace_context> trace_slot_` (line 409) **and a public `trace_context_value()` accessor over it** (`session.hpp:171`). `fixpp::core::trace_context` exists (`core/trace_context.hpp`). `Engine` (`engine.hpp`) has **no** `engine_trace_context()` accessor (verified absent). The `core::error` enum is `std::uint8_t`-backed with slot 121 the current highest — slots 122+ are free.

  **2d surface — consumed vs owned-amendment** (authoritative table also in `contracts/adjacent-amendments.md`):
  | 2d surface | 017 disposition |
  |---|---|
  | `trace_slot_` / `session_local<trace_context>` storage | **consumed** (read-only; no second storage read introduced) |
  | `Session::get_trace_context()` accessor | **owned amendment** — canonical name over `trace_slot_`; reconciles the existing `trace_context_value()` (see #2) |
  | `Engine::engine_trace_context()` accessor | **owned amendment** — ADD (absent today) |
  | `SessionConfig::{logger,tracer}_override` | **owned amendment** — ADD |
  | `SessionConfig::log_sink_override` | **owned amendment** — REMOVE (replaced by `logger_override`) |
  | `EngineConfig::{logger,tracer,meter,engine_trace_context}`, `SessionConfig::{clock_override,initial_trace_context}`, `core::trace_context` | **consumed** (confirm only; do not re-add) |
- **Decision** — 017's surface-completion steps (NOT FSM wiring, per clarified boundary 1; steps 2/3/5 + the step-3 removal are the four-item 2d-surface amendment set, plus type-definition (step 1) and error slots (step 4)):
  1. **Define** the forward-declared types: `fixpp::log::Logger` (+ `fixpp::core::Logger` alias used by `EngineConfig::logger`), `fixpp::otel::TracerProvider`, `fixpp::otel::MeterProvider`, and confirm/alias `fixpp::otel::trace_context` over the existing `fixpp::core::trace_context`.
  2. **Own** the public `Session::get_trace_context() const noexcept` accessor over the existing `trace_slot_` (the storage is **consumed** from 2d, not owned). The live header already has a public `trace_context_value()` over the same `trace_slot_` (`session.hpp:171`); `get_trace_context()` is the anchor-mandated canonical name (anchor §6.4 / App D §D.1) — make it the canonical accessor as a thin alias of (or rename of) `trace_context_value()`, introducing **no** second storage read. Callers of `trace_context_value()` must be checked (`codegraph_callers`) before any removal.
  3. **Add** `SessionConfig::logger_override` + `tracer_override` (`shared_ptr`, nullable; engine-anchor+session-override per `[2d §4.5]`) and **remove** `SessionConfig::log_sink_override` — the new `logger_override` (a whole `Logger`) **replaces** the 2d `log_sink_override` stub (a single `Sink`), per anchor App D §D.1; leaving both would create two competing, undefined-precedence log-override surfaces. `meter_override` intentionally omitted (metrics engine-scoped, anchor §4.8).
  4. **Add** 7 `fixpp::core::error` enumerators at the next free `std::uint8_t` slots 122–128 (the enum is `uint8_t`-backed; slot 121 is the current highest; append-only per `[const §X.4]`). The `[1000,1099]` integers are the future C-ABI `fixpp_error_t` mapping (no C-ABI symbols in v1.0), recorded in `tools/abi_history/error_codes_v1.txt` — NOT enum values (anchor §6.3; see `contracts/error-block.md`).
  5. **Add** `Engine::engine_trace_context() const noexcept` — an owned public accessor on the `Engine` class (the atomic snapshot read used by `FIXPP_ELOG`), plus a NEW `Engine`-held member `fixpp::core::detail::trace_context_snapshot engine_trace_ctx_snapshot_` (the helper TYPE at `engine_config.hpp:64`) seeded at construction from the `EngineConfig::engine_trace_context` seed field (`engine_config.hpp:157`) via `trace_context_snapshot{engine_cfg_.engine_trace_context}`; the accessor returns `.load()`. Verified ABSENT from `include/fixpp/session/engine.hpp`; `EngineConfig` carries only the seed VALUE + the helper TYPE — there is **no** `EngineConfig::engine_trace_context_snapshot` member (that identifier appears only in a comment at `engine_config.hpp:11`). This is a new public C++ surface amendment, not a "confirm".
- **Rationale**: the EngineConfig stubs were placed by 2d expecting 017 to define the types; defining them + the accessor + config fields is surface completion, not FSM behavior. Constructing `SessionSpans` in the FSM open path and emitting parse/store/dispatch spans from the message coroutine remain with the session-module feature (anchor §11).
- **Risk**: `[const §XV.9]` include-edge — `logger.hpp` is widely included; keep `Logger::Impl` pimpl'd and OTel SDK headers out of `log/*.hpp` and `session/*.hpp`. (Watch-item in plan.md.)

---

## Summary of decisions

| # | Topic | Decision | Blocking? |
|---|---|---|---|
| R1 | OTel SDK pin + dual export | `opentelemetry-cpp/1.16.1` pinned exactly; two `AddMetricReader()` calls | no (pin committed) |
| R2 | Prometheus endpoint | SDK embedded HTTP server, `:9464`, dedicated non-asio thread | no |
| R3 | Format-id registry | constexpr CRC32 key + drain-side `std::vformat`; collision check | mechanism finalized in Phase 1 |
| R4 | Backend | own MPSC ring is the v1.0 shipping candidate (disposition PROVISIONAL); TS-13 spike executes+records, non-blocking; quill opt-in | no |
| R5 | Ring ordering | dual atomic seq counters, load-check-CAS, relaxed read load | no |
| R6 | FileSink fsync | deadline-bounded `fdatasync` on drain thread | no |
| R7 | OtlpLogSink | non-blocking `emit` via `BatchLogRecordProcessor`; single write path | no |
| R8 | Adjacent touch | define fwd types + 4-item amendment set (canonical `Session::get_trace_context()` reconciling `trace_context_value()`; add `Engine::engine_trace_context()`; add `SessionConfig::{logger,tracer}_override`; remove `SessionConfig::log_sink_override`) + 7 `core::error` enumerators (slots 122-128); NO FSM wiring | no |

No unresolved `[NEEDS CLARIFICATION]`. Ready for Phase 1 (data-model, contracts, quickstart).
