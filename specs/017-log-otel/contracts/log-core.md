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
- **Category → mask-bit mapping (resolves anchor §4.1 silence; no 2k amendment).** The 64-bit runtime-filter bit index = `category & 63u` (low 6 bits) — defined for any uint16 CRC32 (no `1ull << n` with `n ≥ 64` UB). Built-ins (0x0001..0x0008) occupy bits 1–8. `FIXPP_LOG_CATEGORY("name")` carries a build-time `static_assert` collision check against the built-in low-6-bits (default: **reject** a user category that collides with a built-in bit). A test asserts a user category and a built-in with colliding low-6-bits are either independently controllable or the collision is rejected at compile time.

## Runtime obligations
- `Logger::enqueue` (producer): **zero heap alloc, no exception across boundary, no lock, no syscall** (FR-001); ≤ 50 ns mean non-overflow (FR-002 / TS-9); load-check-CAS overflow check **before** claiming a slot (R5; no deadlock).
- **Arg-marshalling zero-alloc obligation (New 3).** `enqueue(..., std::initializer_list<ArgValue> args)` copies **at most `k_max_args = 6`** `ArgValue`s **by value** into `Record::args`, with **no** dynamic container; excess args are truncated (or a `static_assert`/debug-assert bounds the count) — there is no `args.size() > 6` heap fallback. The `initializer_list` backing array is stack-allocated (`ArgValue` is trivially copyable). The zero-alloc gate (TS-1 / the alloc-guard test) MUST wrap the **full macro → `enqueue` arg-marshalling path** (not just the ring CAS) under the **dual gate** (counting_resource + mallocnesia LD_PRELOAD, `[[feedback_tracking_pmr_resource_false_pass]]`) at 10/50/95% fill — so a stray `ArgValue` ctor or `std::vformat` deferral mis-placed onto the producer side is caught.
- `overflow_policy::drop_newest` (default): preserves oldest in-flight, exact `drop_count()` (FR-004 / TS-2, TSan-clean). `block`: prohibited from session-strand coroutine; debug-`FIXPP_ASSERT` if used there (FR-004 / TS-3 on a raw thread).
- `read_sequence_` / `write_sequence_` are `std::atomic<uint64_t>`, each `alignas(64)`; producer loads `read_sequence_` `relaxed` (R5; TSan).
- `set_category_enabled` lock-free; disabled category dropped before enqueue, counted in `filter_count()`, NOT `drop_count()` (FR-011 / TS-8).
- Three counters are **separate** atomics: `drop_count()` (overflow), `timeout_drop_count()` (drain timeout), `filter_count()` (category).
- `Record::timestamp` from the effective clock (`SessionConfig::clock_override ?: EngineConfig::clock`); a mock clock makes timestamps deterministic (FR-006 / R8).
- `[[nodiscard]] shutdown(drain_timeout)`: drains + flushes each sink; on timeout returns `unexpected(log_drain_timeout)` (core slot 126; C-ABI map 1004) + bumps `timeout_drop_count()` (FR-014 / SC-007).
- `async_flush()` posts completion to the caller's executor (one alloc, off hot path). **`async_flush()` and `shutdown()` are off-hot-path control/shutdown operations, explicitly EXCLUDED from the FR-001 zero-alloc producer gate** — their bounded allocation (e.g. the `std::function` completion handler, the flush sentinel enqueue) is not an FR-001 violation (New 4).

## LOG-003 macro contract (FR-012/FR-013, TS-6/TS-7)
- `FIXPP_SLOG(lvl, tc, cat, fmt, ...)`: caller passes explicit `tc` from `session.get_trace_context()`; record carries `tc.trace_id`/`tc.span_id`. No `co_await`, no `thread_local`.
- `FIXPP_ELOG(lvl, engine, cat, fmt, ...)`: reads `engine.engine_trace_context()` atomic snapshot.
- `FIXPP_LOG0(lvl, cat, fmt, ...)`: zeroed trace_id/span_id (uncorrelated; not a bug).
- **No path may use `thread_local`** (`[const §XIII.3]`; grep-gate in verify).

## Test seams owned here
TS-1 (compile-time cutoff), TS-2 (drop_newest+TSan), TS-3 (block raw thread), TS-8 (filter), TS-6/TS-7 (correlation), TS-9 (latency bench).
