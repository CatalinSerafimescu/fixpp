# Implementation Plan: Async Logger + OTel Observability Surface

**Branch**: `017-log-otel` | **Date**: 2026-06-02 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/017-log-otel/spec.md`
**Design anchor**: `.specify/2k-log-otel.md` v0.5 (Gate-A converged 2026-05-09) — authoritative; this plan carves the build from its locked surface and MUST NOT re-litigate decisions closed there.

## Summary

Deliver the `fixpp::log` async logging core (zero-alloc producer, bounded lock-free MPSC ring, dedicated drain thread, deferred formatting, three context-tier macros, compile-time + runtime filtering, three default sinks) and the `fixpp::otel` observability surface (TracerProvider/MeterProvider SDK wrappers, `SessionSpans` RAII helper, Prometheus+OTLP dual metric export, OTLP log export through the same `Sink` interface), wired into the existing `EngineConfig`/`SessionConfig`/`Session` observability stubs already placed by 2d.

The complete public C++ API, behavioral contract, error block, PMR placement, and all 13 test seams are **already locked in the anchor** (§4–§9). The plan's job is to define the build order, confirm the constitution gates, resolve the handful of genuine implementation unknowns (OTel C++ SDK dual-export wiring, embedded Prometheus server, CRC32 format-id registry, the quill-vs-own spike harness), and pin the scope boundaries fixed at `/speckit-clarify` (2026-06-02 session).

**Clarified scope boundaries (this feature):**
1. **SessionSpans = standalone helper.** 017 ships `SessionSpans` + parse/store/dispatch span types in the `otel` module, verified against a test/mock session (TS-12). Constructing them in the live session-FSM open path and emitting from the message coroutine is deferred to the future session-module feature (anchor §11). 017 does **not** edit FSM transitions.
2. **017 owns its build scaffold.** Pin OTel C++ SDK ≥ 1.12 in `conanfile.py`, add CMake link wiring + the `FIXPP_LOG_MIN_LEVEL` / `FIXPP_LOG_SPIKE_QUILL` options; quill recipe is optional/spike-only (FR-023).
3. **Own MPSC ring is the v1.0 backend.** TS-13 (quill-vs-own) is a non-blocking validation harness; it records the disposition but does not gate delivery (FR-021).

## Technical Context

**Language/Version**: C++23 (clang, `-std=c++2b`), matching the rest of fixpp.
**Primary Dependencies**: OpenTelemetry C++ SDK ≥ 1.12 (first stable logs API; pinned exactly in `conanfile.py` per `[const §XV.17]`), `asio` (standalone; only for `Logger::async_flush` completion post + existing executor types), `std::format`/`std::vformat` (drain-thread formatting), POSIX `<syslog.h>` (SyslogSink), `quill/3.x` (OPTIONAL, `FIXPP_LOG_SPIKE_QUILL=ON` only, TS-13).
**Storage**: `FileSink` rotating files on local FS (bounded by `max_file_bytes × max_keep_count`); no database.
**Testing**: GoogleTest (unit/integration), Google Benchmark (TS-9 latency, TS-13 spike), `mallocnesia` LD_PRELOAD interceptor (zero-alloc gate), TSan (ring data-race), ASan/UBSan, mock OTel exporters (in-memory `LogRecordExporter`/`SpanExporter`/`PushMetricExporter` stubs) for TS-10/11/12.
**Target Platform**: Linux (clang primary); the embedded Prometheus HTTP server + syslog are POSIX. Windows MSVC builds the logger core + OTLP/file sinks; `SyslogSink` is `#ifdef`-guarded out on Windows (no behavior change to the cross-platform surface).
**Project Type**: Single C++ library (the fixpp engine), two new modules: `log` and `otel`.
**Performance Goals**: Producer enqueue **p99 ≤ 50 ns** mean on the non-overflow path (reference CI hardware, anchor §6.2 / SC-001 / TS-9); drain throughput ≥ 5M records/s (soft, off hot path). Zero heap allocation per record on the producer path at 10%/50%/95% fill (mallocnesia).
**Constraints**: Producer path: zero alloc, no exceptions across the queue boundary, no lock/`std::mutex`, no syscall (FR-001); no `thread_local` for trace context (`[const §XIII.3]`); drain thread is a dedicated OS thread, NOT an asio strand thread, holding no session/engine references; `block` overflow mode prohibited from session-strand coroutines (`[const §XI.3]`).
**Scale/Scope**: Two modules (~10 headers, ~8 TUs), 7 new error slots, two minimal 2d surface amendments (Session accessor + 2 SessionConfig fields), 2 benches, 13 test seams. C-ABI surface is placeholder-only (no symbols) in v1.0.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

The anchor converged Gate A (round 4) against the full constitution; this section confirms the gates bind to the plan. All PASS — **no violations, Complexity Tracking empty.**

| Article | Requirement | How this plan satisfies it | Verified by |
|---|---|---|---|
| `[const §VIII.5]` | Zero `new`/`delete` on the hot path | Producer path = `if constexpr` gate → one atomic mask load → load-check-CAS on the ring → 256-byte `memcpy` of a trivially-copyable `Record`. No heap, no syscall, no virtual dispatch. Args captured by value (`ArgValue`, no `string_view`). | mallocnesia gate (TS-1, alloc-guard test); bench (TS-9) |
| `[const §XI.3]` | No `std::mutex`/spin in coroutine context | Enqueue uses only `std::atomic`. `block` overflow mode is documented + debug-`FIXPP_ASSERT`-guarded as prohibited from session-strand threads; `drop_newest` is the default. | TS-3 (block on raw thread only); code review |
| `[const §XIII.1-5]` | OTel from v1.0; async logging mandatory; trace_id in every record; same sink for OTel+file; bench spike mandatory | Dual Prometheus+OTLP metric export (OBS-002); MPSC async logger (LOG-001); `Record` carries `trace_id`/`span_id` (LOG-003); `OtlpLogSink` IS a `Sink` (OBS-003, no double-write); TS-13 spike. | TS-2/6/7/10/11/12/13 |
| `[const §XIII.3]` | `thread_local` prohibited for trace context | Three-macro explicit-context model: `FIXPP_SLOG(tc)` passes `session.get_trace_context()` explicitly; `FIXPP_ELOG` reads `engine.engine_trace_context()` atomic snapshot; `FIXPP_LOG0` zeroes. `SessionSpans` sub-spans use explicit-parent-context API, never `Scope`. | TS-6/TS-7; grep-gate for `thread_local` |
| `[const §XIV.2]` | ≤5 pure-virtual per pluggable interface | `Sink` has exactly 4 (`open`/`emit`/`flush`/`close`). | `check_layers`/code review |
| `[const §XV.5]` / `[§XV.15]` | No sync logging on hot path; `drop-oldest` only on telemetry/log path | Async by construction; `drop_newest` (semantically drop-oldest in a FIFO ring) only on the log path, with a recorded counter. | TS-2 |
| `[const §XV.17]` | Third-party version pinning | OTel SDK + quill pinned to tagged releases in `conanfile.py` (FR-023). | build review |
| `[const §IX.1]` | ≥90% line / ≥80% branch on touched files | TDD per test seam; coverage preset gate. | `/speckit-verify` Step 4 |
| `[const §X.2]` | No C++ symbol leak through C ABI | `c_api/log.h` + `c_api/otel.h` contain **no** `extern "C"` symbols in v1.0 (version macro + include guard only). | nm audit (trivially clean) |
| `[arch §2.3]` | Module include whitelist | `log → {core}`; `otel → {core, log}` (confirmed in `tools/check_layers.py`). OTLP transport is the OTel SDK's own (not `fixpp::transport`); the `otel` module does not include `transport/`. | `tools/check_layers.py` |

**Watch item (not a violation):** `[const §XV.9]` awaitable-header mutex include-edge (`[[feedback_awaitable_header_mutex_include_edge]]`). `logger.hpp` will be included widely (session, transport, control-plane). Its public header MUST NOT drag `std::mutex`/`std::shared_mutex` or heavy OTel SDK headers into the awaitable corpus — the `Logger::Impl` is pimpl'd, and OTel SDK types appear only in `otel/*.hpp` + `.cpp`. Verified by an UNFILTERED Tier-1 build (or `-L sync`) at `/speckit-verify`, not a name-scoped `-R`.

## Project Structure

### Documentation (this feature)

```text
specs/017-log-otel/
├── plan.md              # This file
├── research.md          # Phase 0 — implementation unknowns resolved
├── data-model.md        # Phase 1 — entity catalogue (Record/ArgValue/Logger/Sink/otel)
├── quickstart.md        # Phase 1 — test-seam-driven integration scenarios
├── contracts/           # Phase 1 — locked header contracts (extracted from anchor §4–§5)
│   ├── log-core.md          # Level/Category/ArgValue/Record/Logger/macros (§4.1–§4.3, LOG-003)
│   ├── log-sinks.md         # Sink/SinkFactory + File/Otlp/Syslog sinks (§4.4–§4.7)
│   ├── otel-surface.md      # providers/SessionSpans/exporters/dual-export (§4.8–§4.10)
│   ├── error-block.md       # 7 slots in [1000,1099] (§6.3)
│   └── adjacent-amendments.md  # minimal 2d surface touch (Session accessor + SessionConfig fields)
└── tasks.md             # Phase 2 (/speckit-tasks — NOT created here)
```

### Source Code (repository root = library submodule)

```text
include/fixpp/log/
├── level.hpp             # Level enum, Category + cat:: constants, FIXPP_LOG_CATEGORY (§4.1)
├── record.hpp            # ArgValue (sizeof==24), Record (sizeof==256), FIXPP_SLIT (§4.2)
├── logger.hpp            # overflow_policy, LoggerConfig, Logger (pimpl), 3 macros, FIXPP_FORMAT_ID (§4.3)
├── sink.hpp              # SinkConfig, Sink (4 pure-virtual), SinkFactory (§4.4)
├── file_sink.hpp         # FileSinkConfig, FileSink (§4.5)
├── otlp_log_sink.hpp     # OtlpLogSinkConfig, OtlpLogSink (§4.6)
└── syslog_sink.hpp       # SyslogSinkConfig, SyslogSink (§4.7)

include/fixpp/otel/
├── trace_context.hpp     # confirm/alias fixpp::otel::trace_context (over the existing core type)
├── providers.hpp         # OtelConfig, OtelResourceAttributes, TracerProvider, MeterProvider (§4.8)
├── session_spans.hpp     # SessionSpans + ParseSpan/StoreSpan/DispatchSpan (§4.9)
└── exporters.hpp         # PrometheusExporter, OtlpMetricExporter, OtelDualExportBuilder (§4.10)

include/fix/c_api/
├── log.h                 # placeholder: FIXPP_LOG_API_VERSION, include guard, no symbols (§5)
└── otel.h                # placeholder: FIXPP_OTEL_API_VERSION, include guard, no symbols (§5)

include/fixpp/core/
├── error.hpp             # AMEND: add 7 variants [1000..1011] (§6.3)
└── logger_fwd.hpp        # alias fixpp::core::Logger = fixpp::log::Logger (used by EngineConfig)

src/log/
├── logger.cpp            # Logger::Impl: MPSC ring, drain thread, format registry, filtering, shutdown
├── format_registry.cpp   # constexpr CRC32 format-id → format-string resolution (drain side)
├── file_sink.cpp
├── otlp_log_sink.cpp     # Record → opentelemetry::logs::LogRecord; BatchLogRecordProcessor
└── syslog_sink.cpp

src/otel/
├── providers.cpp         # TracerProvider/MeterProvider over OTel SDK; no-op fallback
├── session_spans.cpp     # explicit-parent-context spans (no Scope for sub-spans)
└── exporters.cpp         # PrometheusExporter (embedded HTTP :9464), OtlpMetricExporter, dual builder

include/fixpp/session/      # MINIMAL 2d surface amendment (no FSM wiring)
├── session.hpp            # AMEND: add Session::get_trace_context() const noexcept (reads trace_slot_)
└── session_config.hpp     # AMEND: add logger_override, tracer_override shared_ptrs

tests/log/   tests/otel/    # 13 test seams (see quickstart.md)
bench/log_enqueue.cpp       # TS-9 (latency)
bench/log_spike.cpp         # TS-13 (quill-vs-own; FIXPP_LOG_SPIKE_QUILL-gated)
bench/baselines/log_enqueue.json
conanfile.py                # AMEND: OTel SDK ≥1.12 pin (+ optional quill)
CMakePresets.json / CMakeLists # AMEND: FIXPP_LOG_MIN_LEVEL, FIXPP_LOG_SPIKE_QUILL options
```

**Structure Decision**: Two new sibling modules under the existing layered layout. `log` depends only on `core`; `otel` depends on `core` + `log` (it reuses `Record` in `OtlpLogSink`, which lives in the `log` module per the anchor). The OTel C++ SDK is an external dependency reached directly by `otel/*.cpp` and `log/otlp_log_sink.cpp`; it never crosses into `fixpp::transport`. The C-ABI headers are placeholders (no symbols) so there is no ABI-drift surface this feature. The only edits outside the two new modules are the 7 error slots (`core/error.hpp`), the `fixpp::core::Logger` alias, and the two minimal 2d amendments — none of which touch the session FSM (per clarified scope boundary 1).

## Complexity Tracking

> No constitution violations. Section intentionally empty.
