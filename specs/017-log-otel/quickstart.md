# Quickstart: Async Logger + OTel Observability Surface

**Feature**: 017-log-otel | **Date**: 2026-06-02
This is the integration/validation map: each row is a test seam from the anchor (§9) tied to the user story, FR/SC, and the harness it runs under. TDD order = write the seam RED, implement, GREEN. The 13 seams are the SC-008 completeness set.

## Build prerequisites (FR-023; do first)
```bash
cd research/G19-fix-fpml-iso20022/library
# conanfile.py pins opentelemetry-cpp/1.16.1 (a tagged release >=1.12, the first stable logs API) ; CMake exposes:
#   -DFIXPP_LOG_MIN_LEVEL=<0..5>      (compile-time level cutoff; default trace(debug)/info(release))
#   -DFIXPP_LOG_SPIKE_QUILL=ON|OFF    (OFF default; ON pulls quill/3.9.0 for TS-13 only)
cmake --preset linux-clang-debug-py     # default build must NOT require quill
```

## Test-seam map

| Seam | Story / FR / SC | What it proves | Harness |
|---|---|---|---|
| **TS-1** | US1 / FR-010 / FR-001 / SC-003 | `FIXPP_LOG_MIN_LEVEL=warn` ⇒ no debug/info format strings in `.rodata` (`nm`/`objdump`); zero alloc over the **full macro → `enqueue` arg-marshalling path** (initializer_list backing array stack-allocated; ≤6 ArgValue by value; no dynamic container) at 10/50/95% fill — dual gate (counting_resource + mallocnesia) | build + `nm`, mallocnesia + counting_resource |
| **TS-2** | US1 / FR-003/004 / SC-002 | capacity=1, 100 emits, drain paused ⇒ `drop_count()==99`, the **oldest** record retained, drain processes exactly 1 | GoogleTest + **TSan** |
| **TS-3** | US1 / FR-004 | `block` mode on a **raw `std::thread`** blocks ≥10 ms while ring full, unblocks on drain resume (never a session strand) | GoogleTest |
| **TS-4** | US1 / FR-009 | `FileSink` rotates past `max_file_bytes`, oldest archived file deleted; asserts the byte-bound `max_file_bytes × max_keep_count` (archived files) + one live file overshooting by at most one record before the `>`-triggered rotation — not merely file count ≤ `max_keep_count` | GoogleTest + **TSan** |
| **TS-5** | US1 / FR-005/008 | injected mock `fsync` fires on the **drain thread**; `flush()` returns after it; producer never blocks on I/O | GoogleTest |
| **TS-6** | US2 / FR-012/013 / SC-004 | (a) `FIXPP_SLOG` carries session trace/span (`0xAA…/0xBB…`); (b) `FIXPP_ELOG` carries engine root (`0xCC…/0xDD…`); (c) `FIXPP_LOG0` all-zeros | GoogleTest |
| **TS-7** | US2 / FR-013 / SC-004 | `FIXPP_LOG0` from a context-free raw thread ⇒ zeros, no UB | GoogleTest + **ASan** |
| **TS-8** | US1 / FR-010/011 / SC-003 | level cutoff + runtime category filter combine: sink gets 1 record, `drop_count()==0`, `filter_count()==1` | GoogleTest |
| **TS-9** | US1 / FR-002 / SC-001 | enqueue **mean ≤ 50 ns** (binding gate) over 10M iters, drain sleeping; p99/p999/max recorded as metrics; baseline `bench/baselines/log_enqueue.json` | Google Benchmark (Tier-2 nightly) |
| **TS-10** | US3 / FR-018 / SC-005 | mock `LogRecordExporter` receives a `LogRecord` with matching TimeUnixNano/Severity/TraceId/SpanId/Body ("msg 42") | GoogleTest + mock OTel |
| **TS-11** | US3 / FR-017 / SC-005 | dual export: counter==3 via `GET :9464/metrics` **and** received by mock OTLP push, one cycle | GoogleTest + mock OTLP + live HTTP |
| **TS-12** | US3 / FR-016 / SC-006 | `SessionSpans`: session + parse spans, parse parented by session, both OK, `parse.latency_ns>0` (explicit parent ctx, no `Scope`) | GoogleTest + mock OTel |
| **TS-13** | US1 / FR-021 / SC-008 | quill-vs-own spike harness (4 threads, 10M, 10/50/95% fill, p99/p999, mallocnesia Criterion A) — MUST **execute + record** its disposition (discharges `[const §XIII.5]`); backend-selection validity is a non-blocking recorded metric | Google Benchmark (`FIXPP_LOG_SPIKE_QUILL`) |

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
- **Zero-alloc**: mallocnesia LD_PRELOAD over the alloc-guard test + TS-9 bench at 10/50/95% fill (FR-001 / SC-001) — dual gate (counting_resource + mallocnesia), `[[feedback_tracking_pmr_resource_false_pass]]`; the gate wraps the **full macro → `enqueue` arg-marshalling path** (initializer_list + ≤6 ArgValue copy), not just the ring CAS (New 3).
- **Data race**: TS-2 + TS-4 under TSan; full UNFILTERED Tier-1 to catch the `[const §XV.9]` awaitable include-edge (not a name-scoped `-R`).
- **Coverage**: ≥95% line / ≥85% branch on touched `log/` + `otel/` files (`[const §IX.1]`); binding rule — no uncovered error/edge path lands without a recorded Opus risk assessment in `.specify/decisions/017-log-otel-verify.md` (genuine error/edge path must be tested; defensive/unreachable/trivial-accessor waived with a one-line rationale).
- **ABI**: `nm -D` on `libfixpp_capi.so` shows no new symbols (c_api log/otel are placeholders).
- **Layers**: `tools/check_layers.py` green (`log→{core}`, `otel→{core,log}`).
- **Completeness**: `fixpp::core::error` gains exactly the 7 new enumerators at slots 122–128 (exact-SET equality on the enumerator set, not the C-ABI `[1000,1099]` integers); the C-ABI occupancy is recorded in `tools/abi_history/error_codes_v1.txt`; catalogue rows LOG-001..004 + OBS-001..003 marked done with coverage-index entries.

## Out of scope (do not implement — keep as placeholders)
`GrpcStreamSink`; C-ABI log/otel symbols; W3C TraceContext injection into FIX; sync-logging shim; log aggregation/routing/sampling; custom OTel SDK; `dlopen` sink discovery; **live session-FSM wiring of `SessionSpans`** (deferred to the session-module feature).
