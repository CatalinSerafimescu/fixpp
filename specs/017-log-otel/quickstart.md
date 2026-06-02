# Quickstart: Async Logger + OTel Observability Surface

**Feature**: 017-log-otel | **Date**: 2026-06-02
This is the integration/validation map: each row is a test seam from the anchor (§9) tied to the user story, FR/SC, and the harness it runs under. TDD order = write the seam RED, implement, GREEN. The 13 seams are the SC-008 completeness set.

## Build prerequisites (FR-023; do first)
```bash
cd research/G19-fix-fpml-iso20022/library
# conanfile.py pins opentelemetry-cpp >=1.12 ; CMake exposes:
#   -DFIXPP_LOG_MIN_LEVEL=<0..5>      (compile-time level cutoff; default trace(debug)/info(release))
#   -DFIXPP_LOG_SPIKE_QUILL=ON|OFF    (OFF default; ON pulls quill/3.x for TS-13 only)
cmake --preset linux-clang-debug-py     # default build must NOT require quill
```

## Test-seam map

| Seam | Story / FR / SC | What it proves | Harness |
|---|---|---|---|
| **TS-1** | US1 / FR-010 / SC-003 | `FIXPP_LOG_MIN_LEVEL=warn` ⇒ no debug/info format strings in `.rodata` (`nm`/`objdump`); zero alloc at call site | build + `nm`, mallocnesia |
| **TS-2** | US1 / FR-003/004 / SC-002 | capacity=1, 100 emits, drain paused ⇒ `drop_count()==99`, the **oldest** record retained, drain processes exactly 1 | GoogleTest + **TSan** |
| **TS-3** | US1 / FR-004 | `block` mode on a **raw `std::thread`** blocks ≥10 ms while ring full, unblocks on drain resume (never a session strand) | GoogleTest |
| **TS-4** | US1 / FR-009 | `FileSink` rotates past `max_file_bytes`, file count ≤ `max_keep_count`, oldest deleted | GoogleTest + **TSan** |
| **TS-5** | US1 / FR-005/008 | injected mock `fsync` fires on the **drain thread**; `flush()` returns after it; producer never blocks on I/O | GoogleTest |
| **TS-6** | US2 / FR-012/013 / SC-004 | (a) `FIXPP_SLOG` carries session trace/span (`0xAA…/0xBB…`); (b) `FIXPP_ELOG` carries engine root (`0xCC…/0xDD…`); (c) `FIXPP_LOG0` all-zeros | GoogleTest |
| **TS-7** | US2 / FR-013 / SC-004 | `FIXPP_LOG0` from a context-free raw thread ⇒ zeros, no UB | GoogleTest + **ASan** |
| **TS-8** | US1 / FR-010/011 / SC-003 | level cutoff + runtime category filter combine: sink gets 1 record, `drop_count()==0`, `filter_count()==1` | GoogleTest |
| **TS-9** | US1 / FR-002 / SC-001 | enqueue **p99 ≤ 50 ns** over 10M iters, drain sleeping; baseline `bench/baselines/log_enqueue.json` | Google Benchmark (Tier-2 nightly) |
| **TS-10** | US3 / FR-018 / SC-005 | mock `LogRecordExporter` receives a `LogRecord` with matching TimeUnixNano/Severity/TraceId/SpanId/Body ("msg 42") | GoogleTest + mock OTel |
| **TS-11** | US3 / FR-017 / SC-005 | dual export: counter==3 via `GET :9464/metrics` **and** received by mock OTLP push, one cycle | GoogleTest + mock OTLP + live HTTP |
| **TS-12** | US3 / FR-016 / SC-006 | `SessionSpans`: session + parse spans, parse parented by session, both OK, `parse.latency_ns>0` (explicit parent ctx, no `Scope`) | GoogleTest + mock OTel |
| **TS-13** | US1 / FR-021 / SC-008 | quill-vs-own spike harness (4 threads, 10M, 10/50/95% fill, p99/p999, mallocnesia Criterion A) — **records** disposition, non-blocking | Google Benchmark (`FIXPP_LOG_SPIKE_QUILL`) |

## Minimal producer usage (illustrative; not new API)
```cpp
// Session strand:
auto const& tc = session.get_trace_context();
FIXPP_SLOG(info, tc, fixpp::log::cat::session, "logon {}", fixpp::log::ArgValue::from_u64(seq));

// Control-plane (Engine& in scope, no Session&):
FIXPP_ELOG(warn, engine, fixpp::log::cat::control, "reconnect {}", fixpp::log::ArgValue::from_inline(comp_id));

// Destructor / static init (no context):
FIXPP_LOG0(error, fixpp::log::cat::store, "store close failed");
```

## Validation gates (mirrors `/speckit-verify`)
- **Zero-alloc**: mallocnesia LD_PRELOAD over the alloc-guard test + TS-9 bench at 10/50/95% fill (FR-001 / SC-001) — dual gate (counting_resource + mallocnesia), `[[feedback_tracking_pmr_resource_false_pass]]`.
- **Data race**: TS-2 + TS-4 under TSan; full UNFILTERED Tier-1 to catch the `[const §XV.9]` awaitable include-edge (not a name-scoped `-R`).
- **Coverage**: ≥90% line / ≥80% branch on touched `log/` + `otel/` files (`[const §IX.1]`).
- **ABI**: `nm -D` on `libfixpp_capi.so` shows no new symbols (c_api log/otel are placeholders).
- **Layers**: `tools/check_layers.py` green (`log→{core}`, `otel→{core,log}`).
- **Completeness**: error block contains exactly the 7 slots (exact-SET); catalogue rows LOG-001..004 + OBS-001..003 marked done with coverage-index entries.

## Out of scope (do not implement — keep as placeholders)
`GrpcStreamSink`; C-ABI log/otel symbols; W3C TraceContext injection into FIX; sync-logging shim; log aggregation/routing/sampling; custom OTel SDK; `dlopen` sink discovery; **live session-FSM wiring of `SessionSpans`** (deferred to the session-module feature).
