# Feature Specification: Nanosecond-resolution SendingTime / OrigSendingTime

**Feature Branch**: `026-nanosecond-sendingtime`
**Created**: 2026-06-06
**Status**: Draft
**Input**: User description: "Nanosecond-resolution SendingTime(52) — emit and parse nanosecond (9-decimal) UTCTimestamp precision for SendingTime and OrigSendingTime, configurable precision, QuickFIX parity."

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
- **Malformed sub-second**: a timestamp with a non-standard sub-second length (e.g., 4, 5, 7, 8 digits) or non-digit characters is rejected as malformed, exactly as today (only seconds/millis/micros/nanos lengths are accepted).
- **OrigSendingTime echo on resend**: when fixpp resends a stored message, the `OrigSendingTime(122)` it stamps reflects the original message's `SendingTime` (existing behaviour); this feature does not change resend semantics, only the precision the formatter can produce.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST support emitting `SendingTime(52)` at nanosecond precision (9 sub-second decimal digits, `YYYYMMDD-HH:MM:SS.sssssssss`, 27 characters).
- **FR-002**: The system MUST expose a per-session configuration selecting the outbound SendingTime precision among at least {millis, micros, nanos}, defaulting to **millis** (FIX 4.x parity).
- **FR-003**: When the precision is the default (millis), outbound `SendingTime(52)` MUST be byte-for-byte identical to current behaviour (no regression for existing sessions).
- **FR-004**: The inbound timestamp parser MUST accept seconds, millisecond, microsecond, AND nanosecond precision (lengths 17 / 21 / 24 / 27), so a peer's nanosecond `SendingTime(52)` / `OrigSendingTime(122)` is parsed, not rejected.
- **FR-005**: A nanosecond timestamp emitted by the system MUST round-trip losslessly through the parser (parse(format(t)) == t at nanosecond resolution, subject to the clock's resolution ceiling).
- **FR-006**: The nanosecond precision MUST apply to both `SendingTime(52)` and any newly-stamped `OrigSendingTime(122)` produced by the outbound/resend path, consistent with the configured precision.
- **FR-007**: The existing MaxLatency (SendingTime freshness) check MUST operate correctly on nanosecond-precision inbound timestamps.
- **FR-008**: A timestamp with a non-standard sub-second length or non-digit sub-second characters MUST continue to be rejected as malformed (no new tolerance beyond the four standard precisions).
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
- **SC-005**: Malformed sub-second lengths (4/5/7/8 digits) and non-digit sub-seconds are still rejected, verified by negative tests.

## Assumptions

- **Default precision = millis** (FIX 4.x default) ⇒ the feature is a byte-identical no-op when unset; nanos/micros are opt-in.
- **Clock resolution**: `utc_time_point` is `time_point<system_clock>`, nanosecond-resolution on libstdc++ (all Linux Tier-1 profiles); the achieved trailing-digit resolution is clock-bounded (documented for non-ns-resolution platforms).
- **The core formatter already computes sub-second in nanoseconds** and truncates to the chosen precision; adding nanos is emitting the full value (no new time source needed). The parser already composes the sub-second as nanoseconds; accepting the 27-char length is the parse-side extension.
- **Scope is timestamp precision only** — SendingTime(52) and OrigSendingTime(122). No change to resend selection, seqnum handling, or any other field. (Confirmed orthogonal to the seqnum/persistence/handshake area.)
- **No new wire field / error slot / codegen / C-ABI**; reuses the existing time formatter/parser + an additive `SessionConfig` field.
- **`/speckit-clarify` will sweep** QuickFIX-cpp/J/Fix8 for: whether the precision should be a per-session enum vs a global build option; the exact `SendingTime` precision each FIX version permits (4.2 millis-only vs 5.0SP2 nanos); and whether OrigSendingTime echo should preserve the original's precision or re-stamp at the configured precision.
