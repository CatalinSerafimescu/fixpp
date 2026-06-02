# Contract: log core (Level / Category / ArgValue / Record / Logger / macros)

**Anchor**: `.specify/2k-log-otel.md` §4.1–§4.3 + LOG-003 three-macro block. Signatures below are **locked**; this contract restates the testable obligations for TDD. FR refs are to `spec.md`.

## Headers
- `include/fixpp/log/level.hpp` — `Level`, `Category`, `cat::*`, `FIXPP_LOG_CATEGORY`.
- `include/fixpp/log/record.hpp` — `ArgValue`, `Record`, `FIXPP_SLIT`.
- `include/fixpp/log/logger.hpp` — `overflow_policy`, `LoggerConfig`, `Logger`, `FIXPP_SLOG`/`FIXPP_ELOG`/`FIXPP_LOG0`, `FIXPP_FORMAT_ID`, `detail::enqueue_record[_notrace]`.

## Compile-time obligations (must fail the build if violated)
- `static_assert(sizeof(ArgValue) == 24)` and `is_trivially_copyable_v<ArgValue>`.
- `static_assert(sizeof(Record) == 256)` and `is_trivially_copyable_v<Record>`; `alignas(64)`.
- `FIXPP_LOG_MIN_LEVEL` `if constexpr` gate ⇒ below-cutoff sites emit **zero** `.rodata` format strings (FR-010 / TS-1).
- `FIXPP_FORMAT_ID(fmt)` is a `constexpr std::uint32_t` (CRC32 of the literal); no string reaches the producer (FR-010, R3).
- CRC32 format-id registry has a build-time/debug duplicate-id check (R3 collision mitigation).

## Runtime obligations
- `Logger::enqueue` (producer): **zero heap alloc, no exception across boundary, no lock, no syscall** (FR-001); ≤ 50 ns mean non-overflow (FR-002 / TS-9); load-check-CAS overflow check **before** claiming a slot (R5; no deadlock).
- `overflow_policy::drop_newest` (default): preserves oldest in-flight, exact `drop_count()` (FR-004 / TS-2, TSan-clean). `block`: prohibited from session-strand coroutine; debug-`FIXPP_ASSERT` if used there (FR-004 / TS-3 on a raw thread).
- `read_sequence_` / `write_sequence_` are `std::atomic<uint64_t>`, each `alignas(64)`; producer loads `read_sequence_` `relaxed` (R5; TSan).
- `set_category_enabled` lock-free; disabled category dropped before enqueue, counted in `filter_count()`, NOT `drop_count()` (FR-011 / TS-8).
- Three counters are **separate** atomics: `drop_count()` (overflow), `timeout_drop_count()` (drain timeout), `filter_count()` (category).
- `Record::timestamp` from the effective clock (`SessionConfig::clock_override ?: EngineConfig::clock`); a mock clock makes timestamps deterministic (FR-006 / R8).
- `[[nodiscard]] shutdown(drain_timeout)`: drains + flushes each sink; on timeout returns `unexpected(log_drain_timeout)` (1004) + bumps `timeout_drop_count()` (FR-014 / SC-007).
- `async_flush()` posts completion to the caller's executor (one alloc, off hot path).

## LOG-003 macro contract (FR-012/FR-013, TS-6/TS-7)
- `FIXPP_SLOG(lvl, tc, cat, fmt, ...)`: caller passes explicit `tc` from `session.get_trace_context()`; record carries `tc.trace_id`/`tc.span_id`. No `co_await`, no `thread_local`.
- `FIXPP_ELOG(lvl, engine, cat, fmt, ...)`: reads `engine.engine_trace_context()` atomic snapshot.
- `FIXPP_LOG0(lvl, cat, fmt, ...)`: zeroed trace_id/span_id (uncorrelated; not a bug).
- **No path may use `thread_local`** (`[const §XIII.3]`; grep-gate in verify).

## Test seams owned here
TS-1 (compile-time cutoff), TS-2 (drop_newest+TSan), TS-3 (block raw thread), TS-8 (filter), TS-6/TS-7 (correlation), TS-9 (latency bench).
