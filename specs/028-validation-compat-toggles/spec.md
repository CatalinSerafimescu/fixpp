# Feature Specification: Validation-compat toggles — CheckCompID & ValidateSequenceNumbers

**Feature Branch**: `028-validation-compat-toggles`
**Created**: 2026-06-07
**Status**: Draft
**Input**: User description: "G3 validation-compat toggles — CheckCompID and validateSequenceNumbers session knobs. Two additive SessionConfig bool fields (QuickFIX-compat relaxations); DEFAULT true = current strict behaviour = byte-identical no-op (the 024 reset-knob pattern). CheckCompID=N skips the sender/target-CompID match; ValidateSequenceNumbers=N accepts out-of-order without the gap dance. MaxLatency OUT OF SCOPE (already via sending_time_threshold). Own Gate A (session FSM touch)."

## Clarifications

### Session 2026-06-07

Reference sweep grounding (QuickFIX-cpp v1.16.0 + QuickFIX/J 3.0.1): QFcpp `isCorrectCompID` returns `true` when `!m_checkCompId` (skips the 49/56 equality match only; header fields still parsed) and has **no** `ValidateSequenceNumbers` knob. QFJ has both (default `Y`); `verify(msg, checkTooHigh, checkTooLow)` skips the too-high/too-low blocks when the flags are false; the inbound counter advances via the uniform `if (getExpectedTargetNum() == msgSeqNum) incrNextTargetMsgSeqNum()` rule across all handlers; the PossDup check (`isPossibleDuplicate && !validatePossDup`) runs regardless of the seqnum flags; CheckCompID is enforced inside `verify()` which QFJ also calls for Logon.

- Q: When `check_comp_id` is relaxed (=N), does it also bypass fixpp's separate 013 Logon-time CompID **authorization** allow-list (`compid_authorization_policy`)? → A: **No — keep authz enforced.** The knob relaxes only the QuickFIX per-message `49`/`56` equality match; the 013 default-deny, mTLS-bound authorization allow-list is an independent security control and still applies. (QFJ has no analogue of the 013 allow-list.)
- Q: With `validate_sequence_numbers` relaxed (=N), how does the next-expected **inbound** counter advance for an out-of-order message? → A: **QFJ-parity — advance on exact match only.** Every frame is delivered; the next-expected counter advances ONLY when `MsgSeqNum == expected`. An out-of-order (too-high or too-low) frame is delivered but does **not** move the counter (exact QuickFIX-J `if (getExpectedTargetNum() == msgSeqNum) incr...` behaviour).
- Q: Do the two relaxations apply at the Logon/establishment check, or only to post-Logon steady-state messages? → A: **Steady-state only** (deliberate divergence from QFJ, which evaluates both inside `verify()` for Logon too). Logon establishment keeps the FULL strict CompID match AND the full strict sequence/establishment handling (including the 013/024 `ResetSeqNumFlag(141)` reset path); the knobs relax only post-Logon (Active steady-state) application/admin traffic. This keeps establishment safe and leaves the Logon FSM + 013/024 reset interaction entirely untouched.
- Q: With `validate_sequence_numbers` relaxed, does the explicit PossDup(43)-flagged duplicate handling (021 `redeliver_poss_dup`) still run? → A: **Keep PossDup handling.** Validation-off skips only the too-high/too-low gap checks; PossDup(43)-flagged frames still flow through the existing 021 disposition (matches QFJ, whose PossDup check is independent of the seqnum flags).

### Session 2026-06-07 (Gate A round 1)

- Q: What is the interaction of `validate_sequence_numbers=false` with an inbound `SequenceReset(35=4)` (both reset-mode and gapfill-mode)? → A: **QFJ-parity — knob-off `SequenceReset` never applies its `NewSeqNo(36)`; the frame is delivered to `fromAdmin` (QFJ `Session.java:1543`/`:1550` gate BOTH the GapFill detection and the `NewSeqNo` application on `validateSequenceNumbers`).** Both the reset-mode intercept (`session.cpp:~1966`) and the gapfill-mode intercept (`:~2294`) are gated on the knob so they do NOT call `apply_inbound_sequence_reset` when validation is off; the shared `apply_inbound_sequence_reset` itself is unchanged (still used by the strict 013/024/027 paths) — only whether it is CALLED is gated. **Counter advance then follows the ordinary per-mode/ordering rule:** reset-mode (intercepted *before* the seqnum gate) and any out-of-order `35=4` ⇒ counter **unchanged** (deliver-without-advance); an **exact-match gapfill `35=4`** passes `check_inbound` first and so **advances by one via the ordinary exact-match path**, after which S7 is bypassed so `NewSeqNo` is still not applied. The "+1" is a fixpp ordering artifact (S7 sits after `check_inbound`/S5), NOT a QFJ-parity claim — QFJ never `+1`s a reset. (The "`NewSeqNo` never applied" part IS QFJ-parity.)

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Relax CompID matching for a compat counterparty (Priority: P1)

An operator must connect to a counterparty whose FIX engine populates `SenderCompID(49)`/`TargetCompID(56)` in a way that does not match the operator's configured `target_comp_id`/`sender_comp_id` on every inbound message (a common QuickFIX-compat scenario — e.g. a venue that varies CompIDs per message class, or a test harness). The operator enables a per-session knob that **skips the per-message sender/target CompID equality check** so these messages are processed instead of rejected. With the knob at its default the session validates CompIDs exactly as today.

**Why this priority**: This is one of the two requested relaxations and is independently valuable — it lets fixpp interoperate with counterparties that fixpp would otherwise reject on CompID mismatch, matching QuickFIX's `CheckCompID=N`.

**Independent Test**: Configure a session with the CompID knob disabling the check, feed an inbound message whose `49`/`56` do **not** match the configured CompIDs, and assert the message is accepted and delivered (not rejected/disconnected). With the knob at default, assert the same message is rejected exactly as today.

**Acceptance Scenarios**:

1. **Given** the CompID-check knob set to relaxed, **When** an inbound application message arrives whose `SenderCompID(49)`/`TargetCompID(56)` do not match the configured CompIDs, **Then** the message is accepted and delivered to the application (no CompID-mismatch reject, no disconnect).
2. **Given** the CompID-check knob at its default (strict), **When** the same mismatching message arrives, **Then** the session rejects/disconnects exactly as it does today.
3. **Given** the CompID-check knob set to relaxed, **When** an inbound message arrives whose CompIDs **do** match, **Then** processing is unchanged (the relaxation never changes the matching-CompID path).

---

### User Story 2 - Tolerate out-of-order sequence numbers for a compat counterparty (Priority: P1)

An operator connects to a counterparty (or test harness) that does not guarantee strictly increasing in-order sequence numbers and does not participate in the standard gap-fill recovery dance. The operator enables a per-session knob that **disables inbound sequence-number validation** so out-of-order / gapped / too-low messages are accepted and processed instead of triggering a `ResendRequest` (too-high) or a fatal disconnect (too-low). With the knob at its default the session performs the full sequence validation and recovery exactly as today.

**Why this priority**: This is the second requested relaxation, independently valuable, and matches QuickFIX's `ValidateSequenceNumbers=N`. It enables interop with counterparties whose sequencing fixpp would otherwise treat as an unrecoverable gap or a fatal too-low error.

**Independent Test**: Configure a session with sequence validation disabled, feed inbound messages out of order (a too-high gap and a too-low replay), and assert each is accepted/processed with **no `ResendRequest` emitted** and **no fatal too-low disconnect**. With the knob at default, assert the same frames produce the existing recovery/disconnect behaviour.

**Acceptance Scenarios**:

1. **Given** sequence validation disabled, **When** an inbound message arrives with a `MsgSeqNum` higher than expected (a forward gap), **Then** it is accepted and processed and **no `ResendRequest` is sent**.
2. **Given** sequence validation disabled, **When** an inbound message arrives with a `MsgSeqNum` lower than expected, **Then** it is accepted and processed and the session is **not** disconnected for a too-low sequence number.
3. **Given** sequence validation at its default (strict), **When** the same out-of-order frames arrive, **Then** the existing behaviour holds: too-high enters recovery and emits a `ResendRequest`; too-low disconnects as today.

---

### User Story 3 - Default strict, byte-identical no-op (Priority: P1)

An existing operator who does not opt in must see **no change whatsoever**. With both knobs at their default (strict), every inbound CompID match and every sequence-number validation behaves byte-for-byte as it does today, and the outbound wire is unchanged.

**Why this priority**: Zero regression for every existing session is co-equal with the relaxations themselves — both knobs are opt-in and the established validation/recovery paths must be untouched when the knobs are at default.

**Independent Test**: With the default configuration, run the full existing session/establishment/recovery/CompID regression suite and assert every witness remains green; assert the two new fields default to the strict value.

**Acceptance Scenarios**:

1. **Given** the default configuration, **When** the full existing session/recovery/CompID/sequence regression suite runs, **Then** every witness remains green and behaviour is byte-identical to the pre-feature baseline.
2. **Given** a freshly default-constructed session configuration, **When** the two new fields are read, **Then** both hold the strict value (CompID checking on, sequence validation on).

---

### Edge Cases

- **CompID relaxation is the match only, not field presence**: `CheckCompID=N` skips the *equality comparison* of `49`/`56` against the configured CompIDs; it does NOT make the header fields optional. A message still carries `SenderCompID`/`TargetCompID` as today; only the match against the session's configured values is skipped (QuickFIX parity — `isCorrectCompID` returns `true`, fields still parsed).
- **CompID relaxation does NOT bypass the security allow-list** (clarified): fixpp has a *separate* Logon-time CompID **authorization** allow-list (013 `compid_authorization_policy` / FR-023, default-deny, an mTLS-bound security control). `CheckCompID=N` relaxes only the per-message `49`/`56` equality match (the QuickFIX `CheckCompID` semantic); it does NOT bypass the authorization allow-list. The two mechanisms are independent (QFJ has no analogue of the 013 allow-list).
- **CompID relaxation does NOT relax BeginString**: the existing combined "CompID/BeginString gate" also validates `BeginString(8)`. `CheckCompID=N` relaxes only the CompID portion; `BeginString` validation is unchanged.
- **CompID relaxation scope — steady-state only** (clarified): the relaxation applies ONLY to post-Logon messages. The Logon/establishment CompID match stays strict — an acceptor still refuses a Logon whose `49` ≠ configured `target_comp_id` even with the knob relaxed. (Deliberate divergence from QFJ; FR-012.)
- **Sequence-validation-off counter mechanics — exact-match advance** (clarified): with validation disabled, every frame is delivered, but the next-expected inbound counter advances ONLY when `MsgSeqNum` equals the currently-expected value (QFJ-parity). An out-of-order (too-high or too-low) frame is delivered but does NOT move the counter.
- **Sequence-validation-off vs inbound `SequenceReset(35=4)` — NewSeqNo not applied** (clarified, FR-013): with validation disabled, an inbound `SequenceReset(35=4)` (reset-mode `GapFillFlag≠Y` AND gapfill-mode `123=Y`) NEVER applies its `NewSeqNo(36)` (QFJ-parity, `Session.java:1543`/`:1550`); it is delivered to `fromAdmin`. The counter advance then follows the ordinary per-mode/ordering rule: reset-mode (intercepted before the seqnum gate) and any out-of-order `35=4` ⇒ counter **unchanged** (deliver-without-advance); an **exact-match gapfill `35=4`** passes `check_inbound` first and so **advances by one** via the ordinary exact-match path, after which the gapfill intercept is bypassed so `NewSeqNo` is still not applied (the +1 is a fixpp ordering artifact, not QFJ-parity). At default (strict) the `35=4` applies `NewSeqNo` as today. The shared sequence-reset application is unchanged — only whether it is invoked is gated on the knob.
- **Sequence-validation-off vs PossDup/duplicate handling (021) — handling retained** (clarified): validation-off skips only the too-high/too-low gap checks. The PossDup(43)-flagged duplicate handling (021 `redeliver_poss_dup`) still runs on the relaxed path (matches QFJ, whose PossDup check is independent of the seqnum flags).
- **Sequence-validation-off vs Logon seqnum / reset (013/024) — Logon untouched** (clarified): disabling sequence validation does NOT affect the Logon's own sequence handling or the `ResetSeqNumFlag(141)` reset knobs (024). Logon establishment sequencing keeps the full strict path (steady-state-only scope, FR-012).
- **Knob independence**: the two knobs are independent; either may be enabled without the other, and any combination (both default, one relaxed, both relaxed) is valid.
- **Outbound is unaffected**: both knobs govern only *inbound* validation. Outbound message construction (our own CompIDs, our own outgoing sequence numbers) is unchanged by either knob.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST expose a **per-session configuration knob** controlling inbound sender/target CompID matching, **defaulting to strict (checking enabled)** — matching QuickFIX `CheckCompID` (default `Y`).
- **FR-002**: When the CompID-check knob is at its default (strict), inbound messages MUST be validated such that the peer's `SenderCompID(49)` equals the configured `target_comp_id` AND the peer's `TargetCompID(56)` equals the configured `sender_comp_id`, with a mismatch rejected/disconnected exactly as today.
- **FR-003**: When the CompID-check knob is relaxed, the per-message sender/target CompID **equality check MUST be skipped for post-Logon (steady-state) messages** so mismatching-CompID messages are accepted and processed. The relaxation MUST NOT make `49`/`56` optional, MUST NOT relax `BeginString(8)` validation, MUST NOT apply at the **Logon/establishment** CompID check (which stays strict — see FR-012), and MUST NOT bypass the separate Logon-time CompID **authorization** allow-list (013 `compid_authorization_policy`).
- **FR-004**: The system MUST expose a **per-session configuration knob** controlling inbound sequence-number validation, **defaulting to strict (validation enabled)** — matching QuickFIX `ValidateSequenceNumbers` (default `Y`).
- **FR-005**: When the sequence-validation knob is at its default (strict), inbound sequence numbers MUST be validated exactly as today: a too-high `MsgSeqNum` enters recovery and emits a `ResendRequest`; a too-low `MsgSeqNum` is handled by the existing too-low disposition (fatal disconnect / 021 possible-duplicate handling).
- **FR-006**: When the sequence-validation knob is relaxed, inbound sequence-number validation MUST be skipped **for post-Logon (steady-state) messages** so out-of-order messages are accepted and processed: a too-high `MsgSeqNum` MUST NOT trigger a `ResendRequest` / gap-fill recovery, and a too-low `MsgSeqNum` MUST NOT cause a fatal disconnect — the message is accepted and delivered. The next-expected inbound counter MUST advance **only when `MsgSeqNum` equals the currently-expected value** (QFJ-parity); an out-of-order frame is delivered but does NOT advance the counter. The PossDup(43)-flagged duplicate handling (021 `redeliver_poss_dup`) MUST still run — only the too-high/too-low gap checks are skipped.
- **FR-007**: The two knobs MUST be **independent** — each may be enabled or disabled without the other, and all four combinations MUST behave per FR-002/003/005/006 for the respective axis.
- **FR-008**: Both knobs MUST govern **inbound validation only**; outbound message construction (our CompIDs, our outgoing sequence numbers) MUST be unchanged by either knob.
- **FR-009**: When both knobs are at their default (strict), the session MUST be byte-for-byte identical to current behaviour on every path — zero regression for existing sessions.
- **FR-010**: The feature MUST NOT introduce a new error slot, a new wire field, a codegen-emitter change, or a C-ABI surface change; it adds two additive `SessionConfig` fields (C++-only value type; struct-layout change requiring a normal source rebuild).
- **FR-011**: Each knob MUST surface a **net-new feature-catalogue row** (the toggle behaviours do not exist as configurable knobs today).
- **FR-012**: Both relaxations MUST apply to **post-Logon (steady-state) traffic only**. The Logon/establishment path MUST be unchanged by either knob: the Logon-time CompID match stays strict, and Logon sequence/establishment handling — including the 013/024 `ResetSeqNumFlag(141)` reset interaction — is untouched. (Deliberate divergence from QFJ, which evaluates both checks for Logon; chosen for safe establishment and to leave the Logon FSM unmodified.)
- **FR-013**: When the sequence-validation knob is relaxed, an inbound `SequenceReset(35=4)` — in BOTH reset-mode (GapFillFlag≠Y) and gapfill-mode (`123=Y`) — MUST NEVER apply its `NewSeqNo(36)` to the inbound counter (QFJ-parity, `Session.java:1543`/`:1550`). The frame MUST be delivered to the application via `fromAdmin` (it is an admin message type). The counter advance MUST then follow the ordinary per-mode/ordering rule: reset-mode (intercepted before the seqnum gate) and any out-of-order `35=4` leave the counter **unchanged** (deliver-without-advance); an **exact-match gapfill `35=4`** passes `check_inbound` first and so **advances the counter by one** via the ordinary exact-match path, after which the gapfill intercept is bypassed so `NewSeqNo` is still not applied. (The +1 is a fixpp ordering artifact — the gapfill intercept sits after `check_inbound` — NOT a QFJ-parity claim; QFJ never `+1`s a reset.) The shared sequence-reset application MUST be unchanged for the strict default and the Logon-establishment paths (013/024/027); only whether it is invoked is gated on the knob. With the knob at default (strict), an inbound `SequenceReset(35=4)` applies `NewSeqNo` exactly as today.

### Key Entities *(include if feature involves data)*

- **CheckCompID config knob**: additive per-session boolean (default strict/on) selecting whether the inbound `49`/`56` equality match is enforced.
- **ValidateSequenceNumbers config knob**: additive per-session boolean (default strict/on) selecting whether inbound sequence-number validation (gap-fill recovery + too-low handling) is enforced.
- **Inbound CompID match**: the existing per-message check that the peer's `SenderCompID`/`TargetCompID` equal the session's configured `target_comp_id`/`sender_comp_id`.
- **Inbound sequence validation**: the existing `check_inbound` path (too-high → recovery/`ResendRequest`; too-low → disconnect / possible-duplicate handling).
- **CompID authorization allow-list (013)**: the *separate* security control (`compid_authorization_policy`) that the CompID-check knob MUST NOT bypass.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: With the CompID-check knob relaxed, an inbound message whose `49`/`56` do not match the configured CompIDs is accepted and delivered (verified by an automated test); with the knob at default, the identical message is rejected/disconnected (verified by a paired test).
- **SC-002**: With the sequence-validation knob relaxed, both a too-high and a too-low inbound message are accepted and processed with **zero `ResendRequest` messages on the wire** and **no too-low disconnect** (verified by an automated test that counts wire messages and asserts session liveness); with the knob at default, the identical frames produce the existing `ResendRequest`/disconnect behaviour (verified by a paired test).
- **SC-003**: With both knobs at default, 100% of existing session/establishment/recovery/CompID regression witnesses remain green and behaviour is byte-identical to the pre-feature baseline.
- **SC-004**: The CompID-check relaxation does not bypass the 013 CompID authorization allow-list — a Logon from a principal not on the allow-list is still refused even with the CompID-check knob relaxed (verified by a negative test).
- **SC-005**: All four knob combinations (both default, CompID-only relaxed, sequence-only relaxed, both relaxed) behave per their requirements, verified by a combination matrix test.
- **SC-006**: With the sequence-validation knob relaxed, the next-expected inbound counter advances only on an exact-expected match (an out-of-order frame is delivered but does not move the counter), verified by an automated test asserting counter state after an out-of-order then in-order frame.
- **SC-007**: With either knob relaxed, the Logon/establishment path is unchanged — a Logon with a mismatching CompID is still refused and Logon-time sequence/reset handling is byte-identical to the strict baseline, verified by a Logon-path negative test.
- **SC-008**: With the sequence-validation knob relaxed, an inbound `SequenceReset(35=4)` (both reset-mode and gapfill-mode; exact-match, too-high, and too-low) does NOT apply its `NewSeqNo` and is delivered to `fromAdmin` — verified by automated tests. The counter-state assertion is **per mode/ordering**: reset-mode and any out-of-order `35=4` ⇒ counter **unchanged**; an **exact-match gapfill `35=4`** ⇒ counter **advances by one** (it passes `check_inbound` before the gapfill intercept is bypassed). With the knob at default, the paired test asserts `NewSeqNo` IS applied exactly as today.

## Assumptions

- **Default strict** ⇒ both fields default to the value that preserves today's behaviour (CompID checking on, sequence validation on); the feature is a byte-identical no-op when unset. Note the polarity **inverts** versus the false-default additive knobs (021/022/024/026/027): here strict IS the current default, so the bool defaults `true` and `false` is the opt-in relaxation. This is an EXPLICIT per-field default (no-implicit-default, [const §XII.5]).
- **QuickFIX-compat semantics (settled — see Clarifications)** — the relaxations mirror QuickFIX-cpp / QuickFIX-J `CheckCompID` and `ValidateSequenceNumbers` for the match/skip behaviour and the exact-match counter advance, with **two deliberate divergences from QFJ**: (a) scope is **steady-state only** (Logon establishment stays strict — FR-012), and (b) the CompID-check knob does NOT bypass fixpp's 013 authorization allow-list (QFJ has no analogue).
- **CompID-check ≠ CompID-authorization** — the new CompID-check knob is the QuickFIX per-message `49`/`56` equality match; it is a *distinct* mechanism from fixpp's 013 `compid_authorization_policy` security allow-list and MUST NOT bypass it. (Key clarify question; high impact.)
- **Inbound-only** — both knobs govern inbound validation; outbound construction is unchanged.
- **Net-new catalogue rows** — neither relaxation exists as a configurable knob today; each gets a new feature-catalogue row + B&L entry.
- **MaxLatency is OUT OF SCOPE** — message-latency tolerance is already configurable via the existing `sending_time_threshold` field; this slice does NOT add a latency knob.
- **No new wire field, no new error slot, no codegen change, no C-ABI change** — two additive C++-only `SessionConfig` value-type fields; struct-layout change requiring a normal source rebuild.
- **Own Gate A** — this slice touches the session FSM / inbound validation path and therefore runs its own Gate A design review.

## Normative References

- **QuickFIX `CheckCompID` setting** — the parity authority for FR-001/002/003: when off, the engine does not validate the inbound `SenderCompID`/`TargetCompID` against the session's configured values.
- **QuickFIX `ValidateSequenceNumbers` setting** — the parity authority for FR-004/005/006: when off, the engine does not validate inbound sequence numbers (no gap-fill recovery, no too-low rejection).
- **`[FIX-SL §4.2.2] Identification of FIX session peers (CompID)`** — the standard CompID identification the strict default enforces.
- **`[FIX-SL §4.8] Message recovery`** — the standard recovery the sequence-validation default drives and the relaxation suppresses; specifically `[FIX-SL §4.8.2] Request retransmission of messages (ResendRequest)`, `[FIX-SL §4.8.5] Gap fill process (SequenceReset-GapFill)`, and `[FIX-SL §4.8.6] Sequence reset (hard reset, GapFillFlag=N)`.
