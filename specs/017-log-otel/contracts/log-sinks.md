# Contract: log sinks (Sink interface + File / Otlp / Syslog)

**Anchor**: `.specify/2k-log-otel.md` §4.4–§4.7. Locked surface; testable obligations restated for TDD.

## Headers
`include/fixpp/log/{sink,file_sink,otlp_log_sink,syslog_sink}.hpp`; impls in `src/log/`.

## `Sink` — exactly 4 pure-virtual (`[const §XIV.2]`, FR-007)
- `[[nodiscard]] expected_t<void> open()` — once, before any emit; failure ⇒ Logger disables this sink (no-op thereafter) + records `log_sink_open_failed` (1001).
- `void emit(Record const&) noexcept` — per-record, **drain thread only**; must return fast; I/O sinks buffer internally; exceptions must NOT propagate.
- `void flush(std::chrono::milliseconds deadline) noexcept` — bulk drain within `deadline`; **mandatory deadline escape** (no indefinite stall).
- `void close() noexcept` — teardown after flush; must not throw.
- **Invariant**: drain thread is the sole caller (no concurrent emit/flush/close). Config is constructor-injected (no `open(SinkConfig const&)` downcast). `SinkFactory::make(memory_resource*, SinkConfig const&)`; `Logger` takes ownership and never calls `make()`.

## `FileSink` (FR-008/FR-009, TS-4/TS-5)
- `FileSinkConfig{directory, base_name="fixpp", max_file_bytes=256MiB, max_keep_count=8, async_fsync=true}`.
- Rotation when `bytes_written() > max_file_bytes`: rename live `<base>.log → <base>.<iso8601>.log`, open fresh, delete oldest when file count > `max_keep_count`. **Disk usage bounded by `max_file_bytes × max_keep_count`** (TS-4).
- `flush(deadline)` = `::fdatasync(fd)` on the drain thread with a deadline escape (timerfd/alarm); producer never calls fsync (TS-5: mock fsync hook fires on drain thread, producer returns after it).
- Accessors: `current_path()` (lifetimebound), `bytes_written()`, `rotation_count()`.

## `OtlpLogSink` (FR-008/FR-018, OBS-003, TS-10)
- `OtlpLogSinkConfig{endpoint, use_grpc=false, cert_source (null⇒plain HTTP), export_timeout=10s, max_export_batch=512, max_export_retries=3}`.
- `emit()` translates `Record → opentelemetry::logs::LogRecord` (timestamp→TimeUnixNano, level→SeverityNumber/Text, trace_id/span_id→TraceId/SpanId, formatted body→Body, category→`fixpp.log.category` attr) → `BatchLogRecordProcessor`. **Non-blocking on drain thread; single write path, no double-write** (`[const §XIII.4]`). `flush(deadline)`=`ForceFlush(deadline)`. Retries capped (no storm; `otel_export_failed` 1010 on give-up).

## `SyslogSink` (FR-008)
- `SyslogSinkConfig{ident="fixpp", facility=LOG_DAEMON}`. Level map: trace/debug→LOG_DEBUG, info→LOG_INFO, warn→LOG_WARNING, error→LOG_ERR, fatal→LOG_CRIT. `emit`=`syslog(3)`, `flush`=no-op, `close`=`closelog(3)`. POSIX-only (Windows `#ifdef`-guarded out).

## Drain-thread fan-out obligation (FR-005)
- The drain thread wraps each `Sink::emit`/`flush` in `try/catch(...)`, increments `sink_error_count(i)` (→ `log_sink_write_failed` 1002 / `log_sink_flush_failed` 1003 semantics) and continues; one failing sink never stalls the drain or the others (TS-5 covers the drain-thread/producer split).
