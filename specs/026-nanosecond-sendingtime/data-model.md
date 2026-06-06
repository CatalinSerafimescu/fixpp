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

**Required doc-comment correction (the existing comments are wrong — do NOT append the nanos line to a rotted table).** The same edit MUST fix the three stale length doc-comments in `fix_time.hpp`, which currently disagree with the parser/formatter (real lengths 17/21/24):
- `fix_time.hpp:43-45` enum comments say `seconds (15 chars)`, `millis (19 chars)`, `micros (23 chars)` → correct to **17 / 21 / 24** and add `nanos (27 chars)`.
- `fix_time.hpp:55` format-buffer doc says `micros: 25 bytes` → correct to **24**; add `nanos: 27 bytes`.
- `fix_time.hpp:73-76` accepted-length doc says `25 — microseconds` → correct to **24** (the code accepts 24 at `fix_time.cpp:184`); add the lenient 19–27 nanos range.

## E2 — `core::utc_time_to_fix_string` nanos branch

- Existing: computes `ns_rem` (sub-second nanoseconds), then `millis → /1'000'000`, `micros → /1'000`.
- **Required edit (a) — resize the size table (UB today).** `fix_time.cpp:119` declares `constexpr std::size_t min_size[3] = {17, 21, 24};` and indexes it `min_size[static_cast<std::uint8_t>(prec)]` (`:120`). With `nanos = 3` this reads `min_size[3]` on a 3-element array — an **out-of-bounds read** (UBSan `array-bounds` / `-fsanitize=bounds`). The edit MUST extend it to a 4-entry `constexpr std::size_t min_size[4] = {17, 21, 24, 27};` (or switch to a `switch`/computed size covering nanos). Without this the headline nanos path is UB before any formatting happens.
- **Required edit (b) — add the `nanos` arm (silently 17-char without it).** The format if/else at `fix_time.cpp:160-171` handles `millis` then `else if micros`, then falls through (seconds = no suffix). There is **no `nanos` arm**, so `prec == nanos` currently emits the **17-char seconds form**. The edit MUST add an explicit `else if (prec == fix_time_precision::nanos)` arm that writes `'.'` then `ns_rem.count()` as 9 zero-padded digits (`write_digits(p, ns_rem.count(), 9)`) into `result+18`; total length `17 + 1 + 9 = 27`.
- Buffer-too-small → existing `decimal_buffer_too_small` (no new slot).
- **RED witness (UBSan-run):** `format(t, nanos)` returns a span of length **== 27** (NOT 17), run under `-fsanitize=undefined,bounds` so the `min_size[3]` OOB and the missing-arm 17-char regression both fail RED before the edits land.

## E3 — `core::fix_string_to_utc_time` lenient parse

- Existing: accepts lengths 17 / 21 / 24 (seconds/millis/micros) strictly.
- **New**: accept length 17 (no fraction) OR 17 + `.` + N digits, **1 ≤ N ≤ 9** (length 19–27 with the dot at index 17). Parse the N fraction digits, scale to nanoseconds by `10^(9−N)`, compose via the existing `ns_sub` path.
- Reject (→ existing `wire_invalid_field_format`): a `.` with no digits (empty fraction), any non-digit fraction char, N > 9, or a malformed base.
- Round-trip (uniform for ALL P **including nanos**): `parse(format(t, P)) == time_point_cast<period_for(P)>(t)` for P ∈ {seconds,millis,micros,nanos}. For nanos this is `time_point_cast<nanoseconds>(t)` (identity on a ns-resolution `system_clock`; on a coarser clock the low digits are already zero, so the cast form still holds — a bare `== t` is a cross-platform flake, do NOT use it). And `parse` of any 1–9-digit fraction yields the ns-scaled instant (e.g. `parse("…SS.1234") == base + 123'400'000 ns`).

## E4 — `SessionConfig::sending_time_precision` (additive field)

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `sending_time_precision` | `fix_time_precision` | `millis` | Precision used to stamp **newly-stamped outbound `SendingTime(52)` only**. `OrigSendingTime(122)` on resend is the stored original `52` preserved verbatim — never stamped at this precision (I-NST-4). Default millis ⇒ byte-identical. |

Requires `#include <fixpp/core/fix_time.hpp>` in `session_config.hpp` (mutex-free chrono header; §XV.9 watch-item).

## E5 — Stamping seam (precision threaded, non-defaulted)

The **actual** public declaration is the 2-arg `stamp_sending_time(utc_time_point now, std::span<char> buf)` (`sending_time.hpp:41-42`) whose body hardcodes millis (`sending_time.cpp:31`). The edit inserts `prec` as the **middle** parameter: `stamp_sending_time(utc_time_point now, fix_time_precision prec, std::span<char> buf)` — a public-header source break (callers recompile; see plan §X / ABI row). Its doc comment (`sending_time.hpp:37-39`, "millis … ≥ 19 bytes") must be corrected (precision is now caller-supplied; buffer ≥ 27 for nanos).

| Helper | Signature change | Callers (verified) |
|--------|------------------|--------------------|
| `session::stamp_sending_time` (`sending_time.hpp/.cpp`) | 2-arg `(now, buf)` → 3-arg `(now, fix_time_precision prec, buf)` (prec middle, non-defaulted) | **2 public callers**: `session.cpp:570`, `:1695` |
| file-local `stamp_sending_time(Clock&)` (`session.cpp:1295`) | `+ fix_time_precision prec` (non-defaulted) | **19 file-local callers**: `:1420,:1844,:1929,:1976,:2017,:2061,:2089,:2241,:2296,:2390,:2425,:2470,:2682,:2792,:3304,:3585,:3639,:3822,:3872` |

**Verified 21-site set** = 19 file-local + 2 public (definition `:1295` and the in-body call in `sending_time.cpp:31` are not counted as caller sites). All call sites pass `cfg_.sending_time_precision`. **Non-defaulted ⇒ compiler enforces exhaustiveness** (a missed site is a build error, not a silent wrong-precision frame).

**Test callers the non-defaulted param also breaks** (must be updated in the same change): `tests/session/logon_handshake_test.cpp:667` calls the public 2-arg `stamp_sending_time(now, buf)` and must pass `prec`. (Note: `tests/session/admin_builder_distinct_now_test.cpp` only *mentions* `stamp_sending_time` in a comment at `:199` — it does not call it, so it does NOT need a signature update; verify with the rg below before editing.)

**Mechanically checkable** — re-run before and after the edit:
```bash
rg -n "stamp_sending_time\(" src/session/*.cpp include/fixpp/session/sending_time.hpp tests/session/*.cpp
```

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
- **I-NST-2** (lossless round-trip): `parse(format(t, P)) == time_point_cast<period_for(P)>(t)` for all P incl. nanos (for nanos = `time_point_cast<nanoseconds>(t)`; never a bare `== t`, which is a cross-platform flake on a coarser `system_clock`). [FR-005]
- **I-NST-3** (lenient parse): any 1–9-digit fraction parses (scaled to ns); only non-digit / empty / >9 reject. [FR-004/FR-008]
- **I-NST-4** (122 preserved): `OrigSendingTime(122)` on resend echoes the original instant, not re-stamped at the configured precision. [FR-006]
- **I-NST-5** (no-heap): format + parse are stack-buffer noexcept; no allocation on any precision. [VIII.5/SC of no-heap witness]
- **I-NST-6** (exhaustive precision): every outbound `52=` stamping site uses `cfg_.sending_time_precision` (compiler-enforced by the non-defaulted param). [FR-006]
