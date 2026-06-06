# Feature Specification: Nanosecond-resolution SendingTime / OrigSendingTime

**Feature Branch**: `026-nanosecond-sendingtime`
**Created**: 2026-06-06
**Status**: Draft
**Input**: User description: "Nanosecond-resolution SendingTime(52) — emit and parse nanosecond (9-decimal) UTCTimestamp precision for SendingTime and OrigSendingTime, configurable precision, QuickFIX parity."

## Clarifications

### Session 2026-06-06

- Q: How lenient should fixpp be about the number of sub-second fraction digits a peer sends? → A: **Lenient — accept any 1–9 fraction digits** (bare length-17 seconds form, OR a `.` at index 17 followed by 1–9 ASCII digits ⇒ total length 19–27), padding internally to nanoseconds; still **emit** only the configured standard precision. (Postel's law / interop-robust; matches QuickFIX-cpp which accepts 0–9; also simpler since the parser already composes nanoseconds from whatever digits are present.)
- Reference sweep (QFcpp + QFJ; Fix8 n/a) settled the rest:
  - **Config shape**: reuse fixpp's existing `fix_time_precision` enum extended with `nanos` (matches QFJ's `TimeStampPrecision` enum {SECONDS,MILLIS,MICROS,NANOS}; cleaner than QFcpp's raw int and already wired into the core formatter). **Default = MILLIS** (both engines).
  - **Version-gating**: both engines gate sub-second precision to FIX4.2+/FIXT.1.1 (FIX4.0/4.1 → seconds). fixpp targets FIX.4.4, so sub-second is always permitted in current scope; FIXT/5.0SP2 gating defers to G4.
  - **OrigSendingTime(122)**: it carries the *original* message's SendingTime → preserved verbatim on resend; the configured precision applies only to newly-stamped `SendingTime(52)`.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Emit nanosecond-precision SendingTime (Priority: P1)

An operator connecting to a venue or counterparty that requires (or benefits from) nanosecond-resolution timestamps needs fixpp to stamp `SendingTime(52)` on outbound messages with 9 sub-second decimal digits (`YYYYMMDD-HH:MM:SS.sssssssss`), rather than the FIX 4.x millisecond default. The precision must be selectable per session so existing millisecond behaviour is preserved by default.

**Why this priority**: This is the core deliverable — outbound timestamp precision. FIX 5.0SP2 and many modern venues expect nanosecond timestamps; emitting them is the feature's reason to exist. Selecting the precision and observing a 27-character `SendingTime` field delivers the whole primary value.

**Independent Test**: Configure a session with `sending_time_precision = nanos`, emit any admin/app message through the send path, and assert the `52=` field is a 27-character `YYYYMMDD-HH:MM:SS.sssssssss` value whose nanosecond digits round-trip losslessly through the parser.

**Acceptance Scenarios**:

1. **Given** a session configured with `sending_time_precision = nanos`, **When** it emits an outbound message, **Then** `SendingTime(52)` carries 9 sub-second decimal digits (27-char timestamp) sourced from the session clock.
2. **Given** a session configured with `sending_time_precision = micros`, **When** it emits an outbound message, **Then** `SendingTime(52)` carries 6 sub-second digits (24-char).
3. **Given** a session with the **default** configuration (no precision set), **When** it emits an outbound message, **Then** `SendingTime(52)` is the millisecond form (21-char) — byte-for-byte identical to current behaviour.

---

### User Story 2 - Accept inbound nanosecond-precision timestamps (Priority: P1)

When a counterparty sends `SendingTime(52)` (or `OrigSendingTime(122)` on a resend) with nanosecond precision, fixpp must parse it correctly rather than rejecting the message as malformed — regardless of the local session's own emit precision — so interop with nanosecond-emitting engines works in both directions.

**Why this priority**: Emitting nanos is useless if fixpp rejects a peer's nanos timestamps. The inbound parse path must accept any standard precision (seconds / millis / micros / nanos). This is co-equal P1 with US1 for real interop.

**Independent Test**: Feed an inbound message whose `SendingTime(52)` is a 27-char nanosecond timestamp and assert it parses successfully (no Reject), the parsed instant matches the source to nanosecond resolution, and the existing MaxLatency check operates on it correctly.

**Acceptance Scenarios**:

1. **Given** an inbound message with a 27-char nanosecond `SendingTime(52)`, **When** fixpp parses it, **Then** parsing succeeds and the message is processed (not rejected as a malformed field).
2. **Given** an inbound resend carrying a nanosecond `OrigSendingTime(122)`, **When** fixpp processes it, **Then** the 122 value parses and the existing 021 PossDup/OrigSendingTime handling operates on the nanosecond value.
3. **Given** an inbound nanosecond `SendingTime(52)`, **When** the MaxLatency guard runs, **Then** the latency comparison is computed correctly against the effective clock.

---

### Edge Cases

- **Clock resolution ceiling**: the session clock (`utc_time_point = std::chrono::time_point<std::chrono::system_clock>`) provides nanosecond resolution on the Tier-1 platform (libstdc++); on platforms whose `system_clock` is coarser (e.g., MSVC ~100 ns ticks), the trailing digits reflect the clock's true resolution (a documented platform nuance, not a defect).
- **Round-trip losslessness**: a nanosecond timestamp emitted by fixpp must parse back to the identical instant (the existing I-6 lossless invariant, extended to nanos).
- **Mixed precision**: the local emit precision and the peer's emit precision are independent; the parser accepts any standard precision regardless of the local setting.
- **Default unchanged**: with no precision configured, every outbound `52=` is the millisecond form — byte-identical, no behaviour change for existing sessions.
- **Non-standard fraction width (accepted)**: a peer sending 4, 5, 7, or 8 fraction digits is parsed (lenient, padded to nanoseconds) — not rejected. Only non-digit characters, an empty fraction, or >9 digits are malformed.
- **OrigSendingTime echo on resend**: when fixpp resends a stored message, the `OrigSendingTime(122)` it emits is the stored original `SendingTime(52)` bytes copied verbatim (existing behaviour — never re-formatted at the configured precision); this feature does not change resend semantics, only the precision the formatter can produce for newly-stamped `SendingTime(52)`.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST support emitting `SendingTime(52)` at nanosecond precision (9 sub-second decimal digits, `YYYYMMDD-HH:MM:SS.sssssssss`, 27 characters).
- **FR-002**: The system MUST expose a per-session configuration selecting the outbound SendingTime precision via the `fix_time_precision` enum extended with `nanos` (values {seconds, millis, micros, nanos}), defaulting to **millis** (FIX 4.x parity; matches QFJ's enum-style `TimeStampPrecision`).
- **FR-003**: When the precision is the default (millis), outbound `SendingTime(52)` MUST be byte-for-byte identical to current behaviour (no regression for existing sessions).
- **FR-004**: The inbound timestamp parser MUST leniently accept the FIX UTCTimestamp grammar: **bare length-17** (`YYYYMMDD-HH:MM:SS`, no dot) **OR** a `.` at index 17 followed by **1–9 ASCII digits** (total length 19–27, no other length valid), padding internally to nanoseconds (an N-digit fraction scales by `10^(9−N)`), so a peer's nanosecond (or any non-standard-width) `SendingTime(52)` / `OrigSendingTime(122)` is parsed, not rejected. [Clarifications: lenient parse]
- **FR-005**: A nanosecond timestamp emitted by the system MUST round-trip losslessly through the parser (`parse(format(t)) == time_point_cast<nanoseconds>(t)` — lossless at nanosecond resolution, subject to the clock's resolution ceiling; the precise per-precision oracle is `parse(format(t,P)) == time_point_cast<period_for(P)>(t)`, data-model I-NST-2 — never a bare `== t`).
- **FR-006**: The configured precision MUST apply to **newly-stamped `SendingTime(52)` only**. A replayed `OrigSendingTime(122)` is **never** stamped at the configured precision: on resend the resender preserves the **stored original** `SendingTime(52)` bytes/instant verbatim and re-emits them as `122` unchanged (no reformatting at the current config). [matches `build_replay_frame`, `session.cpp:1341-1358`; Clarifications / research D5 / contract C6 / I-NST-4]
- **FR-007**: The existing MaxLatency (SendingTime freshness) check MUST operate correctly on nanosecond-precision inbound timestamps.
- **FR-008**: A timestamp that is not the bare length-17 form and not `.`-at-index-17 + 1–9 digits MUST be rejected as malformed: specifically length 18 (`YYYYMMDD-HH:MM:SS.` — empty fraction), a `.` anywhere other than index 17, trailing spaces, an embedded SOH or any non-digit fraction character, or **more than 9 fraction digits**. The >9-digit case MUST be rejected by an explicit **width/length gate** (reject total length > 27, equivalently fraction width > 9) **before** any digit parse — a 10-digit fraction (max 9,999,999,999) fits in `int64`, so it does NOT trip an arithmetic-overflow trap and must be caught by the width check. (Fraction widths 1–9 are all accepted per FR-004; only genuinely malformed sub-seconds are rejected.)
- **FR-009**: The feature MUST NOT introduce a new wire field, a new error slot, or a C-ABI surface change; it extends the existing time formatter/parser and adds one additive `SessionConfig` field.

### Key Entities *(include if feature involves data)*

- **`fix_time_precision`** (core enum): the existing precision selector (`seconds`/`millis`/`micros`) extended with **`nanos`**.
- **SendingTime precision config (`sending_time_precision`)**: additive per-session setting selecting the outbound emit precision; default millis.
- **UTCTimestamp formatter/parser** (`core::utc_time_to_fix_string` / `fix_string_to_utc_time`): the existing functions; the formatter gains a 9-digit branch, the parser gains the 27-char length.
- **`utc_time_point`** (`time_point<system_clock>`): the in-memory instant; nanosecond-resolution on the Tier-1 platform.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: With `sending_time_precision = nanos`, 100% of outbound messages carry a 27-char `SendingTime(52)` whose nanosecond digits match the session clock and round-trip losslessly.
- **SC-002**: With the default configuration, 100% of existing time/session/wire regression witnesses remain green and outbound `52=` fields are byte-identical to the pre-feature baseline.
- **SC-003**: An inbound 27-char nanosecond `SendingTime(52)` (and `OrigSendingTime(122)`) is parsed and processed without a malformed-field rejection, verified by automated tests.
- **SC-004**: The MaxLatency check produces the correct accept/reject decision for nanosecond-precision inbound timestamps, verified by automated tests at the boundary.
- **SC-005**: Genuinely malformed sub-seconds (non-digit chars, empty fraction after `.`, >9 digits) are rejected, while non-standard widths (4/5/7/8 digits) are accepted — both verified by tests.

## Assumptions

- **Default precision = millis** (FIX 4.x default) ⇒ the feature is a byte-identical no-op when unset; nanos/micros are opt-in.
- **Clock resolution**: `utc_time_point` is `time_point<system_clock>`, nanosecond-resolution on libstdc++ (all Linux Tier-1 profiles); the achieved trailing-digit resolution is clock-bounded (documented for non-ns-resolution platforms).
- **The core formatter already computes sub-second in nanoseconds** and truncates to the chosen precision; adding nanos is emitting the full value (no new time source needed). The parser already composes the sub-second as nanoseconds; accepting the 27-char length is the parse-side extension.
- **Scope is timestamp precision only** — SendingTime(52) and OrigSendingTime(122). No change to resend selection, seqnum handling, or any other field. (Confirmed orthogonal to the seqnum/persistence/handshake area.)
- **No new wire field / error slot / codegen / C-ABI**; reuses the existing time formatter/parser + an additive `SessionConfig` field.
- **Clarify decisions (reference-grounded, recorded above)**: per-session `fix_time_precision` enum (not a build option), default millis; sub-second always permitted in fixpp's FIX.4.4 scope (FIXT/version-gating defers to G4); `OrigSendingTime(122)` preserved verbatim on resend (configured precision applies only to new `SendingTime(52)`); inbound parse is lenient (any 1–9 fraction digits).

## Normative References

- **`[FIX50SP2 §3.3] Field data types`** — the UTCTimestamp field datatype `YYYYMMDD-HH:MM:SS[.sss[sss[sss]]]` in UTC, with optional 3- (millisecond), 6- (microsecond), or 9-digit (nanosecond) sub-second precision. Authority for the 27-character nanosecond emit form (FR-001) and the accepted-width grammar (FR-004/FR-008). FIX 5.0 SP2 venues commonly expect nanosecond `SendingTime`; FIXT.1.1/5.0SP2 per-version precision gating is out of scope here (deferred to G4, L-026-1).
- **`[FIX-SL §4.2.3] Validation of SendingTime(52)`** (the section S-019 cites): the MaxLatency freshness check that operates on the parsed instant (FR-007/SC-004; the precision extension changes only what parses, not the check arithmetic).
- **`[FIX-SL §4.8.4] Possible duplicates (PossDupFlag semantics)`**: `OrigSendingTime(122)` carries the *original* message's `SendingTime` on a `PossDupFlag=Y` resend → preserved verbatim, never re-stamped at the configured precision (FR-006/I-NST-4).
