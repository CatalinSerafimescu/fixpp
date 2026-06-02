---
description: "Task list — 017-log-otel: Async Logger + OTel Observability Surface"
---

# Tasks: Async Logger + OTel Observability Surface

**Input**: Design documents from `specs/017-log-otel/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/ (log-core, log-sinks, otel-surface, error-block, adjacent-amendments)
**Design anchor**: `.specify/2k-log-otel.md` v0.5 (Gate-A converged) — locked surface; tasks carve the build, they do not re-litigate.
**Branch**: `017-log-otel` (run all commands with cwd = library submodule root).

**Tests**: INCLUDED. This feature is TDD by construction — the 13 test seams (TS-1…TS-13) are the SC-008 completeness gate (quickstart.md). Each seam is written RED, then made GREEN.

**Catalogue rows owned**: LOG-001..004, OBS-001..003.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: parallelizable (different file, no dependency on an incomplete task)
- **[Story]**: US1 / US2 / US3 (Setup, Foundational, Polish carry no story label)
- File paths are repository-root-relative (repo root = the library submodule).

## Story → test-seam ownership (from quickstart.md)

| Story | Seams |
|---|---|
| US1 (P1) — logger core + sinks + filtering (MVP) | TS-1, TS-2, TS-3, TS-4, TS-5, TS-8, TS-9 |
| US2 (P2) — trace correlation | TS-6, TS-7 |
| US3 (P3) — OTel export | TS-10, TS-11, TS-12 |
| Polish — non-blocking spike | TS-13 |

---

## Phase 1: Setup (build scaffold — FR-023)

**Purpose**: 017 owns its own build wiring; a default build (`FIXPP_LOG_SPIKE_QUILL=OFF`) MUST NOT require quill.

- [X] T001 [P] Pin the OpenTelemetry C++ SDK **exactly** to `opentelemetry-cpp/1.26.0` in `conanfile.py` (latest on conancenter, revision 2026-04-23; ≥1.12 stable logs API; `[const §XV.17]`) with the option set required by 017's FRs: **`with_abi_v2=True`** (API V2 — `OPENTELEMETRY_ABI_VERSION_NO=2`; user directive), `with_no_deprecated_code=True` (pairs with abi_v2), **`with_prometheus=True`** (REQUIRED by FR-017 / TS-11 `:9464` pull endpoint — the recipe default is `False`), `with_otlp_http=True` (default; covers `OtlpLogSink`/`OtlpMetricExporter` HTTP, our `use_grpc=false` default). Keep `with_otlp_grpc=False` in v1.0 — gRPC OTLP transport is deferred to Phase 5 and aligned with the `grpc` Conan pin then (research R1 marker; `conanfile.py` Phase-5 note). Add `quill/11.1.0` as an OPTIONAL requirement pulled only under `FIXPP_LOG_SPIKE_QUILL=ON` (FR-023, R1/R4).
- [X] T002 In `CMakeLists.txt` / `CMakePresets.json`, add the `FIXPP_LOG_MIN_LEVEL` (int 0..5, default debug/trace; release default info) and `FIXPP_LOG_SPIKE_QUILL` (default OFF) options, plus the link wiring of the OTel SDK targets to the new `log`/`otel` library targets and their test/bench targets (FR-023).
- [X] T003 [P] Create the empty module skeletons: `include/fixpp/log/`, `include/fixpp/otel/`, `src/log/`, `src/otel/`, `tests/log/`, `tests/otel/`, and the bench placeholders under `bench/`.
- [X] T004 [P] Confirm/assert the layer edges `log → {core}`, `otel → {core, log}`, and the negative edge `otel ↛ transport` in `tools/check_layers.py` (the positive edges are already present at lines ~26-27 per plan §Constitution-Check `[arch §2.3]` — this task verifies, and makes the `otel ↛ transport` negative edge **explicit** (a FORBIDDEN entry or an inline assertion comment), so it cannot be silently lost by a future edit that adds `transport` to `otel`'s ALLOWED set; today the constraint holds only implicitly via `transport`'s absence from the whitelist).
- [X] T005 Run `conan install . -of build/<preset> --profile=conan/profiles/<preset> --build=missing` then `cmake --preset linux-clang-debug-py` with `FIXPP_LOG_SPIKE_QUILL=OFF` and confirm the scaffold configures + the default build does **not** require quill.

**Checkpoint**: OTel SDK resolves, CMake options exist, layer edges asserted, default build is quill-free.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Shared surface — error block, forward-declared type completion, C-ABI placeholders, the SessionConfig amendment — that the user stories build on. **No session-FSM transition is edited** (clarified scope boundary 1).

**⚠️ CRITICAL**: No user-story work begins until this phase completes.

- [X] T006 Add **exactly 7** new `fixpp::core::error` enumerators at the next free `std::uint8_t` slots **122–128** (append-only / non-renumbering per `[const §X.4]`; slot 121 `session_unknown_acceptor_session` is the current highest) in `include/fixpp/core/error.hpp`: `log_queue_overflow`(122), `log_sink_open_failed`(123), `log_sink_write_failed`(124), `log_sink_flush_failed`(125), `log_drain_timeout`(126), `otel_export_failed`(127), `otel_provider_init_failed`(128); add matching `error_message`/`to_string` entries (FR-015, contracts/error-block.md). Do **NOT** add variants "at 1000–1011" — the enum is `uint8_t`-backed (non-compile).
- [X] T007 [P] Record the C-ABI `fixpp_error_t` `[1000,1099]` occupancy mapping (1000/1001/1002/1003/1004/1010/1011; 1005–1009 + 1012–1099 reserved) in `tools/abi_history/error_codes_v1.txt` (append-only) as the **future v1.x** mapping table — NOT `core::error` enum values (contracts/error-block.md, `[const §X.4]`). The 2i C-ABI audit infra has **not yet landed**, so **create `tools/abi_history/` + `error_codes_v1.txt` if absent** here. Do **NOT** create or run `tools/check_capi_occupancy.sh` — that is a 2i deliverable (it counts rows in the 2X spec-doc error tables) and is irrelevant to 017, which ships **no** live C-ABI symbols (FR-020); this is a forward reservation only.
- [X] T008 [P] Completeness test in `tests/core/` asserting `fixpp::core::error` gained **exactly** the 7 new enumerators (exact-SET equality on the set, missing/unexpected diff — per `[[feedback_completeness_gate_exact_set_not_subset]]`), at slots 122–128. Also smoke-assert `error_message`/`to_string` returns a **non-empty** string for each of the 7 enumerators (the FR-015 / contracts/error-block.md error-string-table obligation).
- [X] T009 [P] Create the C-ABI placeholders `include/fix/c_api/log.h` and `include/fix/c_api/otel.h` containing only `FIXPP_LOG_API_VERSION` / `FIXPP_OTEL_API_VERSION` macros + include guards — **no** `extern "C"` symbols (FR-020).
- [X] T010 Create `include/fixpp/core/logger_fwd.hpp` aliasing `fixpp::core::Logger = fixpp::log::Logger` (forward-decl only; `EngineConfig::logger` resolves through it). Keep the OTel SDK and `std::mutex`/`std::shared_mutex` **off** this include-edge — `Logger::Impl` is pimpl'd (`[const §XV.9]`, `[[feedback_awaitable_header_mutex_include_edge]]`).
- [X] T011 [P] Create `include/fixpp/otel/trace_context.hpp` confirming/aliasing `fixpp::otel::trace_context` over the existing `fixpp::core::trace_context` (16-B trace_id + 8-B span_id + 1-B flags + pad = 32 B, `[2d §1.2]`); used by config + the LOG-003 macros.
- [X] T012 Amend `include/fixpp/session/session_config.hpp`: ADD nullable `std::shared_ptr<...> logger_override` and `tracer_override` (engine-anchor + session-override per `[2d §4.5]`; `meter_override` intentionally omitted — metrics engine-scoped), and REMOVE `log_sink_override` (`session_config.hpp:182`, replaced by `logger_override`). Check `codegraph_callers` of `log_sink_override` before removal (contracts/adjacent-amendments.md item 3/4).
- [X] T013 [P] Regression test in `tests/session/` asserting `SessionConfig::log_sink_override` is **gone** (grep/compile-fail) and `logger_override`/`tracer_override` are present and default-null.

**Checkpoint**: Error block + completeness gate green; fwd types complete; SessionConfig amendment landed; C-ABI placeholders symbol-free.

---

## Phase 3: User Story 1 — Zero-overhead structured async logging with pluggable sinks (Priority: P1) 🎯 MVP

**Goal**: The producer/consumer logger core (LOG-001), the 4-method `Sink` interface + `FileSink`/`SyslogSink` (LOG-002), and compile-time + runtime filtering (LOG-004). Producer path: zero alloc, no lock, no syscall, no exception across the queue boundary.

**Independent Test**: Construct a `Logger` with a capturing sink; emit at varying levels/categories from one and several producer threads; confirm (a) producer performs no alloc and never blocks, (b) records are delivered off the producer thread, (c) below-threshold/filtered records never reach the sink, (d) overflow drops the newest while preserving the oldest, with an exact `drop_count()`.

### Tests for User Story 1 (write RED first) ⚠️

- [X] T014 [P] [US1] TS-1 in `tests/log/test_compile_cutoff_zero_alloc.cpp`: `FIXPP_LOG_MIN_LEVEL=warn` ⇒ no debug/info format strings in `.rodata` (`nm`/`objdump`); **dual-gate** zero-alloc (counting_resource + mallocnesia LD_PRELOAD, `[[feedback_tracking_pmr_resource_false_pass]]`) over the **full macro → `enqueue` arg-marshalling path** (initializer_list backing array stack-allocated, ≤6 ArgValue by value, no dynamic container) at 10/50/95% fill (FR-001/FR-010/SC-003).
- [X] T015 [P] [US1] TS-2 in `tests/log/test_overflow_drop_newest.cpp`: capacity=1, 100 emits, drain paused ⇒ `drop_count()==99`, the **oldest** record retained, drain processes exactly 1; **TSan**-clean on the ring sequence (FR-003/004, SC-002).
- [X] T016 [P] [US1] TS-3 in `tests/log/test_block_overflow_raw_thread.cpp`: `block` mode on a **raw `std::thread`** blocks ≥10 ms while the ring is full, unblocks on drain resume (never on a session strand) (FR-004).
- [X] T017 [P] [US1] TS-4 in `tests/log/test_file_sink_rotation.cpp`: `FileSink` rotates past `max_file_bytes`, oldest **archived** file deleted; assert the byte-bound `max_file_bytes × max_keep_count` (archived only) + one live file overshooting by ≤1 record before the `>`-triggered rotation — not merely file-count ≤ keep; **TSan** (FR-009).
- [X] T018 [P] [US1] TS-5 in `tests/log/test_file_sink_async_fsync.cpp`: injected mock `fsync`/`fdatasync` hook fires on the **drain thread**; `flush(deadline)` returns after it; the producer never blocks on I/O (FR-005/008).
- [X] T019 [P] [US1] TS-8 in `tests/log/test_level_and_category_filter.cpp`: compile-time level cutoff + runtime category filter combine ⇒ sink gets 1 record, `drop_count()==0`, `filter_count()==1` (FR-010/011, SC-003).

### Implementation for User Story 1

- [X] T020 [US1] `include/fixpp/log/level.hpp`: `Level` (`uint8_t` trace=0..fatal=5, stable values), `using Category = uint16_t` with `cat::` built-ins (session=0x0001..user=0x0008), `FIXPP_LOG_CATEGORY("name")` (compile-time CRC32). Encode the **category → 64-bit mask-bit mapping** (`index = category & 63u`) with a build-time `static_assert` collision check rejecting a user category colliding with a built-in low-6-bits (data-model §Category, contracts/log-core.md).
- [X] T021 [US1] `include/fixpp/log/record.hpp`: `ArgValue` (tagged union, `Kind` + 7-pad + 16-union, `from_u64/i64/f64/bool/inline/static`, `FIXPP_SLIT`) with `static_assert(sizeof(ArgValue)==24)` + `is_trivially_copyable_v`; `Record` (`alignas(64)`, 48-B header incl. `trace_id[16]`/`span_id[8]`, `std::array<ArgValue,6> args`, cache pad) with `static_assert(sizeof(Record)==256)` + `is_trivially_copyable_v` (data-model §ArgValue/§Record).
- [X] T022 [US1] `include/fixpp/log/sink.hpp`: `Sink` with **exactly 4** pure-virtual (`[[nodiscard]] expected_t<void> open()`, `void emit(Record const&) noexcept`, `void flush(std::chrono::milliseconds) noexcept`, `void close() noexcept`); `SinkConfig` base; `SinkFactory::make(memory_resource*, SinkConfig const&)` + `FileSinkFactory`/`OtlpLogSinkFactory` (FR-007, `[const §XIV.2]`). Constructor-injected config (no `open(SinkConfig)` downcast).
- [X] T023 [US1] `include/fixpp/log/logger.hpp`: `overflow_policy` (`drop_newest=0` default, `block=1` with debug-`FIXPP_ASSERT` if used from a session-strand thread), `LoggerConfig` (capacity=65536 power-of-2, on_overflow, `pmr::memory_resource*`, drain affinity, drain_timeout=5s), the **pimpl** `Logger` class surface (ctor `(LoggerConfig, pmr::vector<unique_ptr<Sink>>)`, `enqueue(...) noexcept`, `set_category_enabled`/`is_category_enabled`, `drop_count`/`timeout_drop_count`/`filter_count`/`sink_error_count` + resets, `async_flush`, `[[nodiscard]] shutdown(drain_timeout)`), `FIXPP_FORMAT_ID`, `FIXPP_LOG0` macro, and `detail::enqueue_record_notrace`. Keep OTel SDK + `std::mutex` out of this header (pimpl; `[const §XV.9]`).
- [X] T024 [US1] `src/log/format_registry.cpp`: `constexpr` CRC32 `format_id → format-string` registry (drain-side `std::vformat`), with the **build-time/debug duplicate-id collision check** (R3). No string crosses the producer boundary.
- [X] T025 [US1] `src/log/logger.cpp` (`Logger::Impl`): the bounded power-of-2 **MPSC ring** with dual `alignas(64)` atomics `write_sequence_`(producer CAS `acq_rel`)/`read_sequence_`(drain `release` store, producer `relaxed` load), **load-check-CAS overflow check before claiming the slot** (R5; no deadlock), `drop_newest` accounting, the runtime category bitmask filter (`filter_count()`, dropped before enqueue), record timestamp from the effective clock (`SessionConfig::clock_override ?: EngineConfig::clock`, FR-006), and the **dedicated drain OS thread** (NOT an asio strand; holds no session/engine refs) formatting + fanning out to sinks.
- [X] T026 [US1] In `src/log/logger.cpp`, the drain-thread **fan-out catch-all**: wrap each `Sink::emit`/`flush` in `try/catch(...)`, increment `sink_error_count(i)` (`log_sink_write_failed`/`log_sink_flush_failed` semantics), and continue — one failing sink never stalls the drain or the others (FR-005).
- [X] T027 [US1] In `src/log/logger.cpp`, `shutdown(drain_timeout)` (`[[nodiscard]] expected_t<void>`): drain in-flight + `flush` each sink within the deadline; on timeout return `unexpected(log_drain_timeout)` and bump `timeout_drop_count()` (separate atomic from overflow `drop_count()`); plus `async_flush()` posting completion to the caller executor (off-hot-path, excluded from the FR-001 zero-alloc gate) (FR-014, SC-007).
- [X] T028 [P] [US1] `include/fixpp/log/file_sink.hpp` + `src/log/file_sink.cpp`: `FileSinkConfig` (directory, base_name="fixpp", max_file_bytes=256MiB, max_keep_count=8, async_fsync=true); rotation when `bytes_written() > max_file_bytes` (rename live `<base>.log → <base>.<iso8601>.log`, open fresh, delete oldest archived when count > keep — archived-only bound, live additional); `flush` = deadline-bounded `::fdatasync(fd)` on the drain thread (timerfd/alarm escape); accessors `current_path()`/`bytes_written()`/`rotation_count()` (FR-008/009).
- [X] T029 [P] [US1] `include/fixpp/log/syslog_sink.hpp` + `src/log/syslog_sink.cpp`: `SyslogSinkConfig` (ident="fixpp", facility=LOG_DAEMON), level map (trace/debug→LOG_DEBUG…fatal→LOG_CRIT), `emit`=`syslog(3)`, `flush`=no-op, `close`=`closelog(3)`; POSIX-only, `#ifdef`-guarded out on Windows (FR-008).
- [X] T030 [US1] TS-9 bench `bench/log_enqueue.cpp` + baseline `bench/baselines/log_enqueue.json`: enqueue **mean ≤ 50 ns** (binding gate) on the **non-overflow producer path** over 10M iters; record p99/p999/max as reported metrics. **Resolve the fill-rate explicitly**: with the drain sleeping and the default 65536 ring, only the first ~65536 enqueues are non-overflow — the rest measure the overflow/drop path, not the gated producer cost. Either (a) measure the 50 ns gate in windows of ≤ `capacity−1` enqueues (reset between windows), or (b) let the drain **run** (not sleep) so slots free and the ring never saturates; report overflow-path cost separately, ungated (FR-002, SC-001).

**Checkpoint**: MVP — the logger core, `FileSink`/`SyslogSink`, filtering, and overflow all work and are independently testable with **no** OTel dependency. TS-1,2,3,4,5,8 GREEN; TS-9 bench established.

---

## Phase 4: User Story 2 — Trace-correlated log records (Priority: P2)

**Goal**: Every `Record` carries the originating `trace_id`/`span_id`, acquired explicitly (never `thread_local`), via the three-tier macros. `Record` already carries the fields (T021); US2 adds the macros + the two accessor amendments that populate them.

**Independent Test**: Emit from each of the three context tiers — session strand (known tc), engine scope (known engine tc), context-free — and assert each delivered record carries the expected `trace_id`/`span_id` (or all-zeros for the context-free site).

### Tests for User Story 2 (write RED first) ⚠️

- [ ] T031 [P] [US2] TS-6 in `tests/log/test_trace_correlation.cpp`: (a) `FIXPP_SLOG` carries the session trace/span (`0xAA…/0xBB…`); (b) `FIXPP_ELOG` carries the engine root (`0xCC…/0xDD…`); (c) `FIXPP_LOG0` all-zeros; (d) with a **mock clock injected via `EngineConfig::clock`**, a delivered record's `timestamp` equals `mock.now()` — exercising the effective-clock routing `SessionConfig::clock_override ?: EngineConfig::clock` (FR-006, otherwise impl-only in T025) (FR-012/013, SC-004).
- [ ] T032 [P] [US2] TS-7 in `tests/log/test_log0_raw_thread.cpp`: `FIXPP_LOG0` from a context-free raw thread ⇒ zeroed trace/span, **no UB**; **ASan** (FR-013).

### Implementation for User Story 2

- [ ] T033 [US2] In `include/fixpp/log/logger.hpp`, add `FIXPP_SLOG(lvl, tc, cat, fmt, ...)` (caller passes explicit `tc = session.get_trace_context()`) and `FIXPP_ELOG(lvl, engine, cat, fmt, ...)` (reads `engine.engine_trace_context()` atomic snapshot) + `detail::enqueue_record` (with-trace). All three macros gate on `if constexpr (Level::lvl >= FIXPP_LOG_MIN_LEVEL)`; **no `thread_local`** on any path (`[const §XIII.3]`; grep-gate in verify) (FR-013, contracts/log-core.md).
- [ ] T034 [US2] Amend `include/fixpp/session/session.hpp`: add the canonical `[[nodiscard]] Session::get_trace_context() const noexcept` over the existing `trace_slot_`, reconciling the existing `trace_context_value()` (`session.hpp:171`) — make `get_trace_context()` the **single canonical** accessor (thin alias / rename, **no** second storage read). Check `codegraph_callers` of `trace_context_value()` before any removal (contracts/adjacent-amendments.md item 1). **T034 owns migrating BOTH existing callers** in the same commit as the rename: the **production** caller `src/session/session_executor.cpp:80` **and** the test caller `tests/core/test_session_local_lifetime.cpp:106` → `get_trace_context()`, leaving no lingering `trace_context_value()`.
- [ ] T035 [US2] Amend `include/fixpp/session/engine.hpp` + `engine.cpp`: add the `Engine`-held member `fixpp::core::detail::trace_context_snapshot engine_trace_ctx_snapshot_` (helper TYPE at `engine_config.hpp:64`), **seeded at `Engine` construction** from `EngineConfig::engine_trace_context` (`engine_config.hpp:157`) via `trace_context_snapshot{engine_cfg_.engine_trace_context}`, plus the public `[[nodiscard]] fixpp::otel::trace_context Engine::engine_trace_context() const noexcept { return engine_trace_ctx_snapshot_.load(); }` (verified ABSENT today; there is **no** `EngineConfig::engine_trace_context_snapshot` member). No session-FSM edit (contracts/adjacent-amendments.md item 2).
- [ ] T036 [P] [US2] Regression test in `tests/session/test_trace_context_accessors.cpp`: `get_trace_context()` returns the value stored from `initial_trace_context` at `open()` (feeds TS-6a); exactly **one** canonical session trace-context accessor exists (grep-regression scoped over `src/` + `include/` + `tests/` — **not `tests/` alone**, so the migrated `session_executor.cpp:80` caller is covered; no lingering `trace_context_value()` duplicate); `Engine::engine_trace_context()` returns the seeded snapshot (TS-6b).

**Checkpoint**: All three macros route the correct context into `Record`; US1 + US2 both independently testable. TS-6, TS-7 GREEN.

---

## Phase 5: User Story 3 — OpenTelemetry observability export: traces, metrics, logs (Priority: P3)

**Goal**: `SessionSpans` (standalone helper, FSM wiring deferred), dual metric export (Prometheus pull :9464 + OTLP push), and `OtlpLogSink` (OTLP log export through the same `Sink` interface, single write path). All thin wrappers over the official OTel C++ SDK.

**Independent Test**: With mock OTel exporters, assert (a) a session span with correctly-parented parse/store/dispatch child spans, (b) a counter simultaneously readable via the Prometheus scrape endpoint and received by the OTLP push exporter in one cycle, (c) a record exported once as a matching OTLP `LogRecord`.

### Tests for User Story 3 (write RED first) ⚠️

- [ ] T037 [P] [US3] TS-10 in `tests/log/test_otlp_log_sink.cpp`: a mock `LogRecordExporter` receives a `LogRecord` with matching TimeUnixNano/Severity/TraceId/SpanId/Body ("msg 42"); single write path, no double-write (FR-018, SC-005).
- [ ] T038 [P] [US3] TS-11 in `tests/otel/test_dual_metric_export.cpp`: counter==3 readable via real `GET http://localhost:9464/metrics` **and** received by a mock OTLP push exporter in one export cycle (FR-017, SC-005).
- [ ] T039 [P] [US3] TS-12 in `tests/otel/test_session_spans.cpp`: `SessionSpans` yields a session span + a parse child parented by the session, both OK, `parse.latency_ns > 0`, using the **explicit-parent-context** API (no `Scope`); parent span_id correct even when a child is built on a different OS thread (FR-016, SC-006).

### Implementation for User Story 3

- [ ] T040 [US3] `include/fixpp/otel/providers.hpp` + `src/otel/providers.cpp`: `OtelResourceAttributes`/`OtelConfig`; `TracerProvider(OtelConfig)` (`get_tracer`, `shutdown`) + `MeterProvider(OtelConfig)` (`get_meter`, `shutdown`) as **thin RAII wrappers** over the OTel SDK; on provider-init failure ⇒ `otel_provider_init_failed`, substitute a **no-op provider** and continue (FR-019). Defines the types `EngineConfig` forward-declares. Add a **negative test** injecting an init-failing `OtelConfig` asserting the engine does **not** throw/crash, the returned provider is a **no-op** (metric add is silent), and `otel_provider_init_failed` is surfaced (the FR-019 fallback path is otherwise untested).
- [ ] T041 [US3] `include/fixpp/otel/session_spans.hpp` + `src/otel/session_spans.cpp`: `SessionSpans` (lifecycle span on ctor/dtor, CompID attrs, `session_trace_context()`, `tracer()`) + `ParseSpan`/`StoreSpan`/`DispatchSpan` children using `StartSpanOptions{.parent = session_ctx_}` (**never** `opentelemetry::trace::Scope` — `[const §XIII.3]`), each recording `*.latency_ns > 0` + OK/ERROR on destruction. Standalone helper — **no** live session-FSM wiring (clarified boundary 1; anchor §11 hand-off) (FR-016).
- [ ] T042 [US3] `include/fixpp/otel/exporters.hpp` + `src/otel/exporters.cpp`: `PrometheusExporter` (SDK `MetricReader`, embedded single-threaded non-asio HTTP server on `:9464`, `sdk_reader()`), `OtlpMetricExporter` (`PushMetricExporter` wrapped in `PeriodicExportingMetricReader`, `sdk_reader()`), and `OtelDualExportBuilder::with_prometheus(cfg).with_otlp(cfg).build()` performing **two `AddMetricReader()` calls** on one `MeterProvider` (no `MultiMetricExporter` — incompatible base types, R1) (FR-017).
- [ ] T043 [US3] `include/fixpp/log/otlp_log_sink.hpp` + `src/log/otlp_log_sink.cpp`: `OtlpLogSinkConfig` (endpoint, use_grpc=false, cert_source, export_timeout=10s, max_export_batch=512, max_export_retries=3); `emit()` translates `Record → opentelemetry::logs::LogRecord` and hands it to a `BatchLogRecordProcessor` (**non-blocking on the drain thread**; single write path, no double-write — `[const §XIII.4]`); `flush(deadline)`=`ForceFlush(deadline)`; retries capped (`otel_export_failed` on give-up, no storm) (FR-008/018, OBS-003).
- [ ] T044 [US3] Wire `Engine::close()` to **flush sinks and shut down the providers** (`TracerProvider`/`MeterProvider::shutdown()`) on the existing engine teardown path — lifecycle only, **no** session-FSM transition edit (FR-014). Add a **regression test** in `tests/otel/` with **spy** Tracer/Meter providers asserting `Engine::close()` invokes both `shutdown()` calls **and** flushes sinks before returning (the FR-014 engine-close teardown is otherwise implementation-only).

**Checkpoint**: Traces, dual metrics, and OTLP log export all work against mock exporters; all three default sinks ship. TS-10, TS-11, TS-12 GREEN. All user stories independently functional.

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: The non-blocking spike, completeness/ABI/layer gates, catalogue + done-marking.

- [ ] T045 TS-13 spike `bench/log_spike.cpp` (`FIXPP_LOG_SPIKE_QUILL`-gated): 4 producer threads, 10M records, capacity 65536, 10/50/95% fill, p99/p999 primary, mallocnesia Criterion A. It MUST **execute and record its disposition** (own-ring is the v1.0 shipping candidate, disposition PROVISIONAL) in `.specify/decisions/017-log-otel-verify.md` — discharging `[const §XIII.5]`'s mandatory-spike obligation. Backend-selection validity (p99 vs Criterion B) is a **recorded, non-blocking** metric; it does NOT gate delivery (FR-021, SC-008).
- [ ] T046 [P] Append the catalogue rows — LOG-001..004 + OBS-001..003 marked **done** with coverage-index entries — and any B-*/L-* behaviors/limitations rows to `spec/behaviors-and-limitations.md` (per `[[project_behaviors_limitations_catalogue]]`).
- [ ] T047 [P] Verify the cross-cutting gates: `tools/check_layers.py` green (`log→{core}`, `otel→{core,log}`, `otel↛transport`); `nm -D libfixpp_capi.so` shows **no** new symbols (c_api log/otel placeholders); grep-gate confirms **zero** `thread_local` on any log/macro path (`[const §XIII.3]`).
- [ ] T048 Run the quickstart.md validation gates end-to-end and an **UNFILTERED** Tier-1 build (or `-L sync`) to catch the `[const §XV.9]` awaitable include-edge from the widely-included `logger.hpp` (NOT a name-scoped `-R`; `[[feedback_awaitable_header_mutex_include_edge]]`).

**Note**: The full `/speckit-verify` matrix (sanitizers, coverage 95/85 on touched `log/`+`otel/`, alloc dual-gate, abidiff) runs at pipeline step after `/simplify`; these polish tasks stage its inputs.

---

## Dependencies & Execution Order

### Phase dependencies

- **Setup (P1)** → no deps; start immediately.
- **Foundational (P2)** → depends on Setup; **blocks all user stories**.
- **US1 (P3 phase, P1 priority)** → depends on Foundational. The MVP.
- **US2 (P2)** → depends on Foundational; consumes US1's `Record` (trace fields) + `logger.hpp` macro host. Build after US1.
- **US3 (P3)** → depends on Foundational; `OtlpLogSink` consumes US1's `Record`/`Sink`; providers consume the Foundational fwd-type completion. Build after US1.
- **Polish (P6)** → depends on all desired stories complete.

### Cross-story notes (layered library — honest dependencies)

- US2 and US3 both build on US1's `Record`/`Logger`/`Sink` machinery (this is a layered infra feature, not three orthogonal stories). Each story's **tests** are self-contained within its phase, but US2/US3 cannot be implemented before US1's core lands.
- US2 ↔ US3 are independent of each other (macros vs OTel export) and may proceed in parallel once US1 is done.

### Within each story

- Tests (TS-*) written RED before implementation; headers before impls; `level → record → sink → logger.{hpp,cpp}`; sinks ([P]) after the `Sink` interface.

### Parallel opportunities

- Setup: T001, T003, T004 in parallel.
- Foundational: T007, T008, T009, T011, T013 in parallel (different files; T008 after T006, T013 after T012).
- US1 tests T014–T019 all [P]. US1 sink impls T028, T029 [P] after T022.
- US2 tests T031, T032 [P]; T036 [P] after T034/T035.
- US3 tests T037, T038, T039 [P]; impls T040/T041/T042 largely independent.

---

## Parallel Example: User Story 1 tests

```bash
# Launch the US1 seam tests together (all different files, all RED first):
Task: "TS-1 compile-cutoff + zero-alloc dual gate — tests/log/test_compile_cutoff_zero_alloc.cpp"
Task: "TS-2 drop_newest + TSan — tests/log/test_overflow_drop_newest.cpp"
Task: "TS-3 block raw thread — tests/log/test_block_overflow_raw_thread.cpp"
Task: "TS-4 FileSink rotation + TSan — tests/log/test_file_sink_rotation.cpp"
Task: "TS-5 async fsync on drain — tests/log/test_file_sink_async_fsync.cpp"
Task: "TS-8 level + category filter — tests/log/test_level_and_category_filter.cpp"
```

---

## Implementation Strategy

### MVP first (US1 only)

1. Phase 1 Setup → 2. Phase 2 Foundational → 3. Phase 3 US1 → **STOP & VALIDATE** the MVP: a zero-alloc async logger with file/syslog sinks + filtering, no OTel dependency.

### Incremental delivery

- US1 (MVP) → US2 (trace correlation) → US3 (OTel export) → Polish. Each adds value without breaking the prior; US2/US3 reuse the identical `Logger` facade regardless of backend.

### Notes

- [P] = different file, no incomplete-task dependency.
- TS-13 is **non-blocking** (execute + record disposition); it does NOT gate v1.0 delivery — own MPSC ring is the shipping candidate behind the backend-agnostic facade.
- Producer-path constitution gates (zero alloc / no lock / no syscall / no exception) are enforced by TS-1 (dual gate) + TS-9 (latency) + TSan on TS-2/TS-4.
- Commit after each task or logical group; full sanitizer ctest + `/speckit-verify` at the dedicated pipeline step, never a name-scoped `-R` for the awaitable include-edge.
