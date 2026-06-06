# Data Model — Nanosecond-resolution SendingTime (026)

Phase 1. One core enum value, one formatter branch + lenient parser, one additive config field. No new entity, no persistent schema, no new wire field.

## E1 — `fix_time_precision::nanos` (core enum extension)

```cpp
enum class fix_time_precision : std::uint8_t {
    seconds = 0,  // YYYYMMDD-HH:MM:SS               (17 chars)
    millis  = 1,  // YYYYMMDD-HH:MM:SS.sss           (21 chars, FIX 4.x default)
    micros  = 2,  // YYYYMMDD-HH:MM:SS.ssssss        (24 chars)
    nanos   = 3,  // YYYYMMDD-HH:MM:SS.sssssssss     (27 chars)  ← NEW
};
```

Buffer sizes (caller-supplied): seconds 17, millis 21, micros 24, **nanos 27**. A 32-byte buffer still covers all.

## E2 — `core::utc_time_to_fix_string` nanos branch

- Existing: computes `ns_rem` (sub-second nanoseconds), then `millis → /1'000'000`, `micros → /1'000`.
- **New `nanos` branch**: emit `ns_rem.count()` as 9 zero-padded digits (no truncation) into `result+18`; total length `17 + 1 + 9 = 27`.
- Buffer-too-small → existing `decimal_buffer_too_small` (no new slot).

## E3 — `core::fix_string_to_utc_time` lenient parse

- Existing: accepts lengths 17 / 21 / 24 (seconds/millis/micros) strictly.
- **New**: accept length 17 (no fraction) OR 17 + `.` + N digits, **1 ≤ N ≤ 9** (length 19–27 with the dot at index 17). Parse the N fraction digits, scale to nanoseconds by `10^(9−N)`, compose via the existing `ns_sub` path.
- Reject (→ existing `wire_invalid_field_format`): a `.` with no digits (empty fraction), any non-digit fraction char, N > 9, or a malformed base.
- Round-trip: `parse(format(t, P)) == time_point_cast<period_for(P)>(t)` for P ∈ {seconds,millis,micros,nanos}; and `parse` of any 1–9-digit fraction yields the ns-scaled instant.

## E4 — `SessionConfig::sending_time_precision` (additive field)

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `sending_time_precision` | `fix_time_precision` | `millis` | Precision used to stamp outbound `SendingTime(52)` (and newly-stamped `OrigSendingTime` where applicable). Default millis ⇒ byte-identical. |

Requires `#include <fixpp/core/fix_time.hpp>` in `session_config.hpp` (mutex-free chrono header; §XV.9 watch-item).

## E5 — Stamping seam (precision threaded, non-defaulted)

| Helper | Signature change | Callers |
|--------|------------------|---------|
| `session::stamp_sending_time` (`sending_time.hpp/.cpp`) | `+ fix_time_precision prec` (non-defaulted) | `session.cpp:570`, `:1695` |
| file-local `stamp_sending_time(Clock&)` (`session.cpp:1295`) | `+ fix_time_precision prec` (non-defaulted) | ~23 sites: `:1420,:1844,:1929,:1976,:2017,:2061,:2089,:2241,:2296,:2390,:2425,:2470,:2682,:2792,:3304,:3585,:3639,:3822,:3872,…` |

All call sites pass `cfg_.sending_time_precision`. **Non-defaulted ⇒ compiler enforces exhaustiveness** (a missed site is a build error, not a silent wrong-precision frame).

## E6 — Truth table (outbound emit)

| `sending_time_precision` | outbound `52=` form | length | default? |
|--------------------------|---------------------|--------|----------|
| `millis` | `YYYYMMDD-HH:MM:SS.sss` | 21 | ✅ (byte-identical to today) |
| `micros` | `YYYYMMDD-HH:MM:SS.ssssss` | 24 | |
| `nanos` | `YYYYMMDD-HH:MM:SS.sssssssss` | 27 | |
| `seconds` | `YYYYMMDD-HH:MM:SS` | 17 | |

Inbound accepts any of the above **plus** any non-standard width 1–9 (lenient).

## Invariants

- **I-NST-1** (default byte-identical): `sending_time_precision == millis` ⇒ every outbound `52=` is the 21-char millis form, byte-identical to pre-feature. [FR-003/SC-002]
- **I-NST-2** (lossless round-trip): `parse(format(t, P)) == t` truncated to P's period, for all P incl. nanos. [FR-005]
- **I-NST-3** (lenient parse): any 1–9-digit fraction parses (scaled to ns); only non-digit / empty / >9 reject. [FR-004/FR-008]
- **I-NST-4** (122 preserved): `OrigSendingTime(122)` on resend echoes the original instant, not re-stamped at the configured precision. [FR-006]
- **I-NST-5** (no-heap): format + parse are stack-buffer noexcept; no allocation on any precision. [VIII.5/SC of no-heap witness]
- **I-NST-6** (exhaustive precision): every outbound `52=` stamping site uses `cfg_.sending_time_precision` (compiler-enforced by the non-defaulted param). [FR-006]
