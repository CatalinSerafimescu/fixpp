# Quickstart — Nanosecond-resolution SendingTime (026)

How to exercise and verify. cwd = the library submodule.

## Enable

```cpp
fixpp::session::SessionConfig cfg{ /* ...existing... */ };
cfg.sending_time_precision = fixpp::core::fix_time_precision::nanos;   // or micros / millis (default)
```

Default (`millis`) ⇒ outbound `SendingTime(52)` is the 21-char form, byte-identical to today.

## Behavior at a glance

| Config | Outbound `52=` | Length |
|--------|----------------|--------|
| `millis` (default) | `YYYYMMDD-HH:MM:SS.sss` | 21 |
| `micros` | `YYYYMMDD-HH:MM:SS.ssssss` | 24 |
| `nanos` | `YYYYMMDD-HH:MM:SS.sssssssss` | 27 |

Inbound: any 1–9-digit fraction is accepted (padded to ns); only non-digit / empty / >9-digit fractions are rejected.

## Core witnesses (`tests/session/fix_time_roundtrip_test.cpp`, target `session_fix_time_roundtrip`, extend, RED-first)

1. **Nanos format** — `format(t, nanos)` returns a span of length **== 27** (assert == 27, NOT 17 — guards both the missing-`else if` 17-char regression and the `min_size[3]` OOB); the 9 digits equal the sub-second ns of `t`. **Run under UBSan** (`-fsanitize=undefined,bounds`) so the `min_size[3]` out-of-bounds read fails RED.
2. **Nanos round-trip** — `parse(format(t, nanos)) == time_point_cast<nanoseconds>(t)` (ns-lossless; use the cast form, NOT a bare `== t`, which is a cross-platform flake on a coarser `system_clock`).
3. **Lenient parse** — `parse("YYYYMMDD-HH:MM:SS.D")` for each width D = 1..9 yields the ns-scaled instant (e.g. `.1234` → 123 400 000 ns).
4. **Reject malformed** — `.` (empty), `.12a` (non-digit), `.1234567890` (10 digits) → `wire_invalid_field_format`.
5. **No regression** — `format(t, millis)` / `micros` byte-identical to pre-feature; seconds path unchanged.

## Session witnesses (`tests/session/test_sending_time_precision.cpp`, NEW, RED-first)

6. **Emit nanos** — session with `sending_time_precision = nanos` → every outbound message's `52=` is 27 chars.
7. **Default millis byte-identity** — default config → `52=` is 21 chars, byte-identical to the pre-feature golden.
8. **Inbound ns accepted** — feed an inbound message with a 27-char `52=` → parsed, processed (no Reject).
9. **MaxLatency on ns** — inbound ns `52=` at the latency boundary → correct accept/reject.
10. **OrigSendingTime(122) preserved** — on a PossDup resend, `122` echoes the original instant (not re-stamped at the configured precision).
11. **No-heap** — under mallocnesia, format + parse allocate nothing at any precision.

Run:

```bash
cmake --build build/linux-clang-debug-py --target session_fix_time_roundtrip session_sending_time_precision
cd build/linux-clang-debug-py && ctest -R '^(session_fix_time|session_sending_time_precision)' --output-on-failure
```

## Sanitizers + coverage

```bash
for P in linux-clang-asan linux-clang-ubsan linux-clang-tsan; do
  cmake --build build/$P --target session_fix_time_roundtrip session_sending_time_precision
  ( cd build/$P && ctest -R '^(session|wire)' --output-on-failure )
done
cmake --build build/linux-clang-coverage --target session_fix_time_roundtrip session_sending_time_precision
( cd build/linux-clang-coverage && ctest -R '^(session_fix_time|session_sending_time_precision)' )   # ≥95/85 new branches
```

## §XV.9 watch-item (new include into session_config.hpp)

```bash
cd build/linux-clang-debug-py && ctest -L sync --output-on-failure   # or unfiltered Tier-1
```

## Live interop (skip-without-counterparty)

```bash
cd build/linux-clang-debug-py && ctest -R 'interop.*sending_time|interop.*nanos' --output-on-failure
```

- fixpp emits nanos `52=` → accepted by a live QFcpp/QFJ peer (C7.1).
- fixpp accepts a live peer's nanos `52=` (C7.2).
