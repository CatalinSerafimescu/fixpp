# Research — Nanosecond-resolution SendingTime (026)

Phase 0 decisions. Grounded in a source sweep of fixpp + the reference engines (QuickFIX-cpp v1.16.0, QuickFIX/J 3.0.1; Fix8 has no per-session timestamp-precision config).

---

## D1 — The core already computes nanoseconds; nanos is an emit/accept extension

**Decision**: extend `fix_time_precision` with `nanos = 3` and add a nanos branch to `utc_time_to_fix_string`; the time source is unchanged.

**Rationale (source-verified)**: `src/core/fix_time.cpp:130` computes `ns_rem = duration_cast<nanoseconds>(since_epoch - sec_total)` and then truncates: millis `/1'000'000` (`:162`), micros `/1'000` (`:167`). The nanos branch emits the full 9-digit `ns_rem.count()` with no truncation. `utc_time_point = time_point<system_clock>` (`clock.hpp:21`); on libstdc++ (all Linux profiles use `compiler.libcxx=libstdc++11`) `system_clock::period` is nanoseconds, so the value carries true ns. No new time type, no new clock.

**Alternatives considered**: a separate `utc_time_to_fix_string_nanos` function — rejected (the precision param already exists; one more enum value is cleaner). Changing the time type — unnecessary (system_clock is already ns on Tier-1).

---

## D2 — Config shape: reuse the `fix_time_precision` enum, default millis

**Decision**: the per-session knob is `SessionConfig::sending_time_precision` of type `fix_time_precision`, default `millis`.

**Rationale**: QFJ uses an enum `TimeStampPrecision{SECONDS,MILLIS,MICROS,NANOS}` default MILLIS (`Session.java:281,284`); QFcpp uses a raw int `TimestampPrecision` (0–9) + a legacy `MillisecondsInTimeStamp` bool. The enum matches QFJ, is type-safe, is already the formatter's parameter type, and maps 1:1 to a future cfg-loader key ("NANOS" → `nanos`). Default `millis` = FIX 4.x parity ⇒ byte-identical no-op (FR-003).

**Alternatives considered**: a raw int (QFcpp-style) — rejected (the enum is safer and already exists). A bool `nanosecond_sending_time` — rejected (can't express micros; the enum covers all four).

---

## D3 — Inbound parse: lenient (any 1–9 fraction digits)

**Decision**: `fix_string_to_utc_time` accepts any sub-second width 1–9 (timestamp length 17–27), parsing the digits present and scaling to nanoseconds; rejects only non-digit fraction chars, an empty fraction after `.`, or >9 digits. [spec Clarifications]

**Rationale**: QFcpp `FieldConvertors.h:convert(string)` accepts length 17–27 (any 0–9 fraction). Postel's law — a peer emitting a non-standard width (e.g. 7 or 8 digits) should parse, not be rejected. It is also simpler than the current strict 17/21/24 check: the parser already composes `ns_sub` from the fraction (`fix_time.cpp:277-287`); lenient parsing scales an N-digit fraction by `10^(9-N)`. Emit stays at the configured standard precision (we never emit a non-standard width).

**Alternatives considered**: strict (only 0/3/6/9) — rejected (brittle for interop; diverges from QFcpp; more code than lenient). Accept >9 digits (truncate) — rejected (>9 is malformed per the grammar; reject it).

---

## D4 — Wiring: thread precision non-defaulted through both stamp helpers + all sites

**Decision**: add a `fix_time_precision` parameter (**non-defaulted**) to (a) `session::stamp_sending_time` (`sending_time.cpp`) and (b) the file-local `stamp_sending_time(Clock&)` (`session.cpp:1295`); update all ~25 call sites to pass `cfg_.sending_time_precision`.

**Rationale**: the dominant outbound-stamp path is the file-local `stamp_sending_time(Clock&)` at `:1295`, used at ~23 sites (admin builders, heartbeat, test-request, logout, resend, app send, etc.); the public `session::stamp_sending_time` is used at 2 more (`:570`, `:1695`). If any site keeps the millis default, that message type silently emits the wrong precision (the half-restructure class, [[feedback_half_restructure_symmetric_api]]). Making the param **non-defaulted** turns "did I update every site?" into a compile error — the compiler is the exhaustiveness gate. The inbound parse paths (MaxLatency `:1826`, `OrigSendingTime(122)` `:2054`, `SendingTime(52)` `:2084`, LogonSent `:2772`) all call the shared `fix_string_to_utc_time`, so they accept ns automatically once D3 lands — no per-site change.

**Alternatives considered**: defaulted param — rejected (a missed site silently misbehaves; the 026 spec's whole risk). Reading `cfg_` inside a non-member helper — not possible (free function); the param is the clean seam.

---

## D5 — OrigSendingTime(122) preserved; version-gating deferred

**Decision**: `OrigSendingTime(122)` is preserved verbatim on resend (it is a copy of the original message's `SendingTime`); only newly-stamped `SendingTime(52)` uses the configured precision. Sub-second precision is always permitted in fixpp's FIX.4.4 scope; the QFcpp/QFJ FIX4.2+/FIXT version-gate defers to G4.

**Rationale**: 122 carries the ORIGINAL sending instant — re-stamping it at a different precision would misrepresent history; QuickFIX echoes the stored original. Version-gating (`Session.h:177 supportsSubSecondTimestamps`: FIXT or >=FIX4.2) is moot for a 4.4-only engine; it becomes relevant when FIXT.1.1/5.0SP2 land (G4), tracked as L-026-1.

---

## D6 — Resolution ceiling (platform-bounded)

**Decision**: document that the achieved sub-second resolution is bounded by `utc_time_point`'s `system_clock::period` — full nanoseconds on libstdc++ (Tier-1 Linux); coarser on platforms whose `system_clock` ticks at ~100 ns (MSVC, Tier-2). The wire FORMAT is always 9 digits when `nanos` is selected; trailing digits reflect the clock's true resolution.

**Rationale**: the format is decoupled from the source resolution; emitting 9 digits from a 100 ns-resolution clock yields valid timestamps with the last two digits `00`. This is a documented platform nuance (L-026-1), not a defect, and matches how every engine behaves on a coarse clock.

---

## Cross-references

- Core: `fix_time.hpp:42 enum`, `:63 utc_time_to_fix_string`, `:83 fix_string_to_utc_time`; `fix_time.cpp:130 ns_rem`, `:162/:167 truncation`, `:181 accepted lengths`, `:277-287 ns_sub compose`.
- Session stamping: `sending_time.cpp:27/31` (public, millis-hardcoded); `session.cpp:1295` (file-local `stamp_sending_time(Clock&)`); ~25 call sites enumerated in plan.md. Inbound parse: `session.cpp:1826/:2054/:2084/:2772`.
- Config: `session_config.hpp:145 struct SessionConfig` (insert near `:223`).
- Clock/type: `clock.hpp:21 utc_time_point = time_point<system_clock>`; libstdc++11 (conan profiles).
- Reference: QFcpp `Session.h:159-185`, `SessionSettings.h:132-133`, `FieldConvertors.h:465`; QFJ `Session.java:281-284,830-837`.
