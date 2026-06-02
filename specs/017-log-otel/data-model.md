# Phase 1 Data Model: Async Logger + OTel Observability Surface

**Feature**: 017-log-otel | **Date**: 2026-06-02
**Source of truth**: `.specify/2k-log-otel.md` §4 (exact layouts) — reproduced here in entity form for traceability. Where a field layout or invariant is given, it is the anchor's locked value; do not change without a design-doc amendment.

## Entity catalogue

### `Level` (enum, `log/level.hpp`) — LOG-004
- `std::uint8_t`: `trace=0, debug=1, info=2, warn=3, error=4, fatal=5`. Numeric values **stable** (never reorder; extend only at the high end).
- Compile-time cutoff: `FIXPP_LOG_MIN_LEVEL` (CMake int 0..5); call sites below it compile to zero bytes.

### `Category` (`log/level.hpp`) — LOG-004
- `using Category = std::uint16_t`; CRC32-interned from a string literal at static init (no heap).
- Built-ins (`cat::`): `session=0x0001, wire=0x0002, transport=0x0003, tls=0x0004, store=0x0005, otel=0x0006, control=0x0007, user=0x0008`. User categories via `FIXPP_LOG_CATEGORY("name")` (compile-time CRC32).
- Runtime filter: `Logger::enabled_categories_mask_` (`std::atomic<uint64_t>`), bit clear ⇒ dropped before enqueue, counted in `filter_count()`.

### `ArgValue` (struct, `log/record.hpp`) — **`sizeof == 24`, trivially copyable**
- Tagged union. `Kind` (`uint8_t`): `empty=0, u64, i64, f64, bool_val, inline_str=5, static_str=6`.
- Layout: `Kind kind` (1) + `uint8_t _pad[7]` (7) + union (16) = 24.
- Union members: `uint64_t`, `int64_t`, `double`, `bool`, `InlineStr` (`char data[15]` + `uint8_t len`; NOT null-terminated, drain reads `len` bytes; truncates at 15), `const char* static_ptr` (caller-asserted static lifetime).
- Constructors: `from_u64/from_i64/from_f64/from_bool/from_inline(string_view)/from_static(const char*)`. `FIXPP_SLIT("lit")` → `from_static`.
- **Invariants**: `static_assert(sizeof(ArgValue)==24)` (`==`, not `<=` — Record sizing depends on it); `static_assert(is_trivially_copyable_v)`. No `std::string_view` field (dangling across the queue boundary is the failure this design prevents).

### `Record` (struct, `log/record.hpp`) — **`alignas(64)`, `sizeof == 256`, trivially copyable**
- `k_max_args = 6`.
- Header (48 B): `utc_time_point timestamp` (8) + `trace_id[16]` (16) + `span_id` (8) + `Level level` (1) + `flags` (1, reserved) + `Category category` (2) + `format_id` (4, CRC32 of fmt) + `arg_count` (1) + `_pad[5]` (5).
- `std::array<ArgValue,6> args` (144 B) + `_cache_pad[64]` (64 B) = 256 B (4 cache lines).
- **Invariants**: `static_assert(sizeof(Record)==256)`, `is_trivially_copyable_v`. `trace_id`/`span_id` zeroed for context-free records (FIXPP_LOG0; not a bug, §6.4). Placed on the ring via one CAS + one 256-B memcpy (zero alloc).

### `overflow_policy` (enum, `log/logger.hpp`) — LOG-001
- `drop_newest=0` (default; FIFO ring ⇒ dropping newest preserves oldest in-flight ⇒ satisfies `[const §XIII.2]` drop-oldest allowance; increments `drop_count_`). `block=1` (spins `yield()`; **prohibited from session-strand coroutines** per `[const §XI.3]`; debug-`FIXPP_ASSERT` if used from a session executor thread).

### `LoggerConfig` (struct, `log/logger.hpp`)
- `uint32_t capacity=65536` (**power of 2**), `overflow_policy on_overflow=drop_newest`, `std::pmr::memory_resource* ring_resource=get_default_resource()` (must outlive `Logger`), `int drain_cpu_affinity=-1`, `std::chrono::milliseconds drain_timeout=5s`.

### `Logger` (class, `log/logger.hpp`) — LOG-001 / LOG-004 — **pimpl**
- Ctor: `Logger(LoggerConfig, std::pmr::vector<std::unique_ptr<Sink>> sinks)`. Sinks fixed at construction (not swappable mid-flight). Dtor joins drain thread.
- Producer: `void enqueue(Level, Category, uint32_t format_id, trace_id const&, span_id, utc_time_point, initializer_list<ArgValue>) noexcept` (called by the macros; zero alloc, lock-free).
- Config: `set_category_enabled(Category,bool) noexcept`, `is_category_enabled(Category) const noexcept`.
- Accounting: `[[nodiscard]]` `drop_count()` / `timeout_drop_count()` / `filter_count()` (all `uint64_t`, separate atomics) + `reset_*`; `sink_error_count(size_t) const noexcept`.
- Flush/shutdown: `[[nodiscard]] asio::awaitable<void> async_flush()` (posts completion to caller executor; one alloc, off hot path); `[[nodiscard]] expected_t<void> shutdown(std::chrono::milliseconds drain_timeout)` (drains, flushes each sink, returns `unexpected(log_drain_timeout)` + bumps `timeout_drop_count()` on timeout).
- **State**: `write_sequence_`/`read_sequence_` (`std::atomic<uint64_t>`, each `alignas(64)`), `enabled_categories_mask_`, the three counters, the per-sink error counters, the ring buffer (PMR), the drain `std::thread`.
- **Aliased** as `fixpp::core::Logger` (forward-declared by `EngineConfig::logger`).

### Trace-correlation macros (`log/logger.hpp`) — LOG-003 (no `thread_local`)
| Macro | Tier | Trace source |
|---|---|---|
| `FIXPP_SLOG(lvl, tc, cat, fmt, ...)` | session strand | explicit `fixpp::otel::trace_context const& tc = session.get_trace_context()` |
| `FIXPP_ELOG(lvl, engine, cat, fmt, ...)` | engine scope | `engine.engine_trace_context()` (atomic snapshot) |
| `FIXPP_LOG0(lvl, cat, fmt, ...)` | context-free | zeroed trace_id/span_id |
- All gate on `if constexpr (Level::lvl >= FIXPP_LOG_MIN_LEVEL)`. `FIXPP_FORMAT_ID(fmt)` → `constexpr uint32_t` CRC32 (no string to producer).

### `Sink` (interface, `log/sink.hpp`) — LOG-002 — **exactly 4 pure-virtual** (`[const §XIV.2]`)
- `[[nodiscard]] expected_t<void> open()` (once, before emit; failure ⇒ sink disabled + `log_sink_open_failed`); `void emit(Record const&) noexcept` (per-record, drain thread, must be fast, buffer if I/O); `void flush(std::chrono::milliseconds deadline) noexcept` (bulk drain within deadline; mandatory deadline escape); `void close() noexcept` (teardown, two-phase).
- All methods called on the **drain thread only** (sole caller; no concurrency). Configuration is constructor-injected (no `open(SinkConfig const&)` downcast).
- `SinkConfig` base (per-sink configs derive). `SinkFactory` (`make(memory_resource*, SinkConfig const&)`) + `FileSinkFactory`/`OtlpLogSinkFactory`. Callers build sinks and pass to `Logger`; `Logger` never calls `make()`.

### `FileSink` (`log/file_sink.hpp`) — LOG-002
- `FileSinkConfig`: `directory`, `base_name="fixpp"`, `max_file_bytes=256MiB`, `max_keep_count=8`, `async_fsync=true`.
- Rotation when `bytes_written() > max_file_bytes`: rename live `<base>.log → <base>.<iso8601>.log`, open fresh, delete oldest when count > keep. **Disk bound = `max_file_bytes × max_keep_count`** (DoS bound, §6.5). `flush` = deadline-bounded `fdatasync` on drain thread.
- Accessors: `current_path()` (lifetimebound), `bytes_written()`, `rotation_count()`.

### `OtlpLogSink` (`log/otlp_log_sink.hpp`) — LOG-002 / OBS-003
- `OtlpLogSinkConfig`: `endpoint`, `use_grpc=false`, `cert_source` (`[2g §4.1]`, null ⇒ plain HTTP), `export_timeout=10s`, `max_export_batch=512`, `max_export_retries=3`.
- `emit()` → `opentelemetry::logs::LogRecord` (timestamp→TimeUnixNano, level→Severity, trace/span→TraceId/SpanId, formatted body→Body, category→attr) handed to `BatchLogRecordProcessor` (non-blocking). Single write path, no double-write (`[const §XIII.4]`).

### `SyslogSink` (`log/syslog_sink.hpp`) — LOG-002
- `SyslogSinkConfig`: `ident="fixpp"`, `facility=LOG_DAEMON`. Level map: trace/debug→LOG_DEBUG, info→LOG_INFO, warn→LOG_WARNING, error→LOG_ERR, fatal→LOG_CRIT. `emit`=`syslog(3)`, `flush`=no-op, `close`=`closelog(3)`. POSIX-only (Windows: `#ifdef`-guarded out).

### `trace_context` (`otel/trace_context.hpp`) — consumed, not owned
- 16-B `trace_id` + 8-B `span_id` + 1-B flags + pad = 32 B (`[2d §1.2]`). Already exists as `fixpp::core::trace_context`; 017 confirms/aliases `fixpp::otel::trace_context` (used by `EngineConfig`/`SessionConfig`).

### `TracerProvider` / `MeterProvider` (`otel/providers.hpp`) — OBS-001/002 — thin SDK RAII wrappers
- `OtelResourceAttributes` (service name/version/env + extra kv). `OtelConfig` (endpoint, use_grpc, resource, cert_source, export_interval=60s, export_timeout=30s).
- `TracerProvider(OtelConfig)`: `get_tracer(string_view) const` (lifetimebound), `shutdown()`. `MeterProvider(OtelConfig)`: `get_meter(string_view) const` (lifetimebound), `shutdown()`. On provider-init failure ⇒ no-op provider (`otel_provider_init_failed`), engine continues. Owned by `EngineConfig` as `shared_ptr`. SDK internals not under PMR (documented caveat).

### `SessionSpans` (`otel/session_spans.hpp`) — OBS-001 — RAII, standalone (FSM wiring deferred)
- Ctor `(TracerProvider&, sender_comp_id, target_comp_id, trace_context const& parent_ctx)`; lifecycle span started on construction, ended on destruction; attrs `fixpp.session.{sender,target}_comp_id`. `session_trace_context() const noexcept`. `tracer() const` (lifetimebound).
- Inner RAII children **using explicit-parent-context API** (`StartSpanOptions{.parent = session_ctx_}`, **never** `Scope` — `Scope` mutates thread-local SDK context, prohibited by `[const §XIII.3]`): `ParseSpan` (`set_msg_type`, `set_error`), `StoreSpan` (`set_seq_num`, `set_error`), `DispatchSpan` (`set_msg_type`, `set_error`) — each records `latency_ns` + OK/ERROR status on destruction.

### Metric exporters (`otel/exporters.hpp`) — OBS-002
- `PrometheusConfig` (host=`0.0.0.0`, port=9464, metrics_path=`/metrics`, optional cert_source). `PrometheusExporter` = SDK `MetricReader` (pull, embedded HTTP); `sdk_reader()` (lifetimebound) registered via `AddMetricReader()`.
- `OtlpMetricExporter(OtelConfig)` = `PushMetricExporter` wrapped in `PeriodicExportingMetricReader`; `sdk_reader()` registered via `AddMetricReader()`.
- `OtelDualExportBuilder`: `with_prometheus(cfg).with_otlp(cfg).build() → shared_ptr<MeterProvider>` (two `AddMetricReader()` calls; single `meter.add` fans out to both).

### Error block `[1000,1099]` (`core/error.hpp`) — §6.3
| Variant | Value | Surface |
|---|---|---|
| `log_queue_overflow` | 1000 | internal C++ only (macros are `void`; reserved slot; counter via `drop_count()`) |
| `log_sink_open_failed` | 1001 | sink disabled, Logger continues |
| `log_sink_write_failed` | 1002 | `emit` threw (caught by drain) |
| `log_sink_flush_failed` | 1003 | `flush` threw |
| `log_drain_timeout` | 1004 | returned from `shutdown()`; bumps `timeout_drop_count()` |
| `otel_export_failed` | 1010 | OTLP batch export failure counter; engine continues |
| `otel_provider_init_failed` | 1011 | provider ctor failure; no-op fallback |
- No other slot in the block consumed in v1.0 (1005–1009, 1012–1099 reserved).

### Adjacent-module amendments (minimal; NO FSM wiring — clarified boundary 1)
- `Session::get_trace_context() const noexcept` — **new** read-only accessor over the existing `trace_slot_` (`session_local<trace_context>`).
- `SessionConfig::logger_override`, `SessionConfig::tracer_override` — **new** nullable `shared_ptr` (engine-anchor + session-override; `meter_override` omitted).
- `fixpp::core::Logger` alias = `fixpp::log::Logger` (satisfies `EngineConfig::logger`'s forward decl).
- `Engine::engine_trace_context()` accessor confirmed/added (atomic snapshot read for `FIXPP_ELOG`).
- **Already present (confirm only)**: `EngineConfig::{logger,tracer,meter,engine_trace_context}`, `SessionConfig::{clock_override,initial_trace_context}`, `Session::trace_slot_`, `fixpp::core::trace_context`.

## Relationships

```
EngineConfig ─owns(shared_ptr)─> Logger, TracerProvider, MeterProvider
            └─ engine_trace_context (FIXPP_ELOG source)
SessionConfig ─override(shared_ptr)─> logger_override, tracer_override
             └─ initial_trace_context ─stored at open()─> Session::trace_slot_ ─read by─> get_trace_context() ─> FIXPP_SLOG
Logger ─owns─> MPSC ring(Record[]), drain thread, pmr::vector<unique_ptr<Sink>>
Sink <|── FileSink | OtlpLogSink | SyslogSink           (drain-thread-only callers)
OtlpLogSink ─uses─> OTel SDK BatchLogRecordProcessor     (OBS-003, single write path)
MeterProvider ─AddMetricReader×2─> PrometheusExporter (pull) + OtlpMetricExporter (push)  (OBS-002)
SessionSpans ─parent ctx─> ParseSpan | StoreSpan | DispatchSpan  (OBS-001, explicit parent, no Scope)
```
