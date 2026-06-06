# Contract — SendingTime precision (026)

Phase 1 interface contract. Surfaces touched + pre/post-conditions + the behavior oracle.

## C1 — `fix_time_precision::nanos` (core enum)

- **Surface**: `include/fixpp/core/fix_time.hpp`, `nanos = 3`.
- **Compatibility**: additive enum value; existing `seconds/millis/micros` unchanged. Source rebuild.

## C2 — `core::utc_time_to_fix_string(tp, prec, out)`

- **Pre**: `out` ≥ 27 bytes for `nanos` (≥24 micros, ≥21 millis, ≥17 seconds).
- **Post (nanos)**: writes `YYYYMMDD-HH:MM:SS.sssssssss` (27 chars), the 9 digits = `ns_rem.count()` zero-padded; returns the 27-char sub-span. Other precisions unchanged.
- **Post (buffer too small)**: `unexpected(decimal_buffer_too_small)` (existing slot).
- **Oracle**: `format(t, nanos)` last-9-digits == `(t.time_since_epoch() % 1s)` in ns; `format(t, millis)` byte-identical to pre-feature.

## C3 — `core::fix_string_to_utc_time(s)` (lenient)

- **Pre**: `s` is a candidate UTCTimestamp.
- **Post (accept)**: base `YYYYMMDD-HH:MM:SS` optionally followed by `.` + 1–9 digits → `utc_time_point` with the fraction scaled to nanoseconds (`×10^(9−N)`).
- **Post (reject → `wire_invalid_field_format`)**: empty fraction after `.`, non-digit fraction char, >9 fraction digits, or malformed base / out-of-range field.
- **Oracle**: `parse("…SS.1234")` == base + 123 400 000 ns; `parse("…SS.")`, `parse("…SS.12a")`, `parse("…SS.1234567890")` all reject; `parse(format(t,P)) == time_point_cast<period(P)>(t)`.

## C4 — `SessionConfig::sending_time_precision`

- **Surface**: `include/fixpp/session/session_config.hpp`, `fix_time_precision sending_time_precision = fix_time_precision::millis;` + `#include <fixpp/core/fix_time.hpp>`.
- **Compatibility**: additive field, default millis ⇒ byte-identical. Source rebuild; no C-ABI.

## C5 — Stamping helpers (precision-threaded, non-defaulted)

- `session::stamp_sending_time(now, prec, buf)` and the file-local `stamp_sending_time(Clock&, prec)` both take a **non-defaulted** `fix_time_precision prec`.
- **Contract**: every outbound `SendingTime(52)` is formatted at `prec`; callers pass `cfg_.sending_time_precision`. The non-defaulted param means a build failure if any call site is not updated (compile-time exhaustiveness — I-NST-6).
- **Oracle**: with `prec = nanos`, the produced `52=` field is 27 chars; with the default millis, 21 chars and byte-identical.

## C6 — Non-goals (explicit)

- **No new wire field / error slot / codegen / C-ABI surface.** (Formatter reuses `decimal_buffer_too_small`; parser reuses `wire_invalid_field_format`.)
- **No re-stamping of `OrigSendingTime(122)`** — it is preserved verbatim on resend (I-NST-4).
- **No FIXT/version-gating** of sub-second precision (fixpp is FIX.4.4; gating → G4, L-026-1).
- **No change to the time source / clock** — resolution is `system_clock`-bounded (L-026-1).
- **Emit never uses a non-standard width** — only seconds/millis/micros/nanos are emitted; the 1–9 leniency is parse-only.

## C7 — Interop (live, both roles)

- **C7.1 — fixpp emits nanos**: a fixpp session with `sending_time_precision = nanos` sends `52=` as a 27-char timestamp; a live QFcpp/QFJ peer accepts it (no Reject).
- **C7.2 — fixpp accepts nanos**: a live QFcpp/QFJ peer configured for NANOS sends a 27-char `52=`; fixpp parses and processes it (MaxLatency computed correctly).
- Skip-without-counterparty in-tree; goldens captured at the first paired run in the parent harness.
