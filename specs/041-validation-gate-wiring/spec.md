# Feature Specification: Validation Gate Wiring

**Feature Branch**: `041-validation-gate-wiring`
**Created**: 2026-06-16
**Status**: Draft
**Input**: User description: "Wire the two specified-but-unwired validation gates into production (REMAINING-WORK.md item 3c): (A) configurable inbound message validation via the dictionary-driven `wire::Validator`, default OFF; (B) the Engine clock-config gate that rejects a null time source."

## Context & Problem

The library already contains two fully-implemented validation mechanisms that are **never invoked on any production path**:

1. **`wire::dictionary_driven_validator`** implements the full session-level inbound suite (header-field order, unexpected tags, required fields, field-value types, repeating-group structure, enum values) — but `Session`'s inbound path only performs an order-independent header scan, so out-of-order header/body fields and other dictionary violations are silently **accepted** on the wire. This diverges from QuickFIX, which rejects them (e.g. `Reject` with `SessionRejectReason=14`). Recorded as behaviour/limitation rows B-004-1 / B-005-7 / L-003-3.

2. **The engine clock-config validator** detects an unset time source and returns a `clock_not_set` error — but no engine lifecycle step ever calls it, so a misconfigured engine with no time source proceeds silently instead of failing fast (limitation B-007-2).

This feature wires both gates into production. It is a v1.0-release-gate `[RATIFY]` item: it closes a real QuickFIX-conformance gap and two specified-but-unwired-gate defects.

## Clarifications

### Session 2026-06-16

- Q: When strict inbound validation is enabled, which inbound messages does it apply to? → A: **All inbound messages — session/admin and application — including the session-establishing Logon** (QuickFIX parity; QuickFIX `DataDictionary::validate` runs unconditionally on every inbound message at `Session.cpp:1218-1229`). A malformed Logon is rejected and blocks establishment.
- Q: Where does the validation gate run relative to the sequence-number gate? → A: **Before the sequence-number gate** (QuickFIX parity; `validate()` precedes `verify()` so a structurally-invalid message is rejected regardless of its seqnum and does not advance seqnum state).
- Q: If validation is enabled but the session has no data dictionary configured? → A: **Config error / fail-closed** — reject the configuration/session setup rather than silently disabling validation.
- Q: Which chokepoint enforces the clock-config gate? → A: **`Engine::start()` returning `expected_t<void>`** (the spec's "Engine::open" semantics); callers check the result. Accepts the small public-API change from `void`.

### Session 2026-06-16 (Gate A round 1)

- D: **FSM scope of the validate gate.** `Session::on_inbound_frame` is a `switch (fsm_state_)` with separate per-state bodies and no shared post-framing point; `LogoutSent`/`Disconnected` drain all non-Logout inbound. → Decision: the validate gate is inserted **per-arm, before each state's seqnum gate, only in the states that process inbound** (`NotConnected`, `LogonSent`, `LogonReceived`, `Active`). The `LogoutSent`/`Disconnected` drain semantics are **preserved unchanged**, and the existing no-reject-loop exemption for inbound `Reject(35=3)`/`Logout(35=5)` is **preserved** (a malformed `35=3`/`35=5` is still not Reject-looped). FR-003/FR-004 below are scoped accordingly; "validate every inbound message" means every inbound message *in the processing states*, not the drain states.
- D: **FIXT two-dictionary validation (app messages).** QuickFIX validates a FIXT **application** message against BOTH the session dictionary AND a separate application dictionary resolved from `DefaultApplVerID` (`Session.cpp:1218-1229`). Phase-1 validates against the **session-held `cfg.dictionary` only**. → Decision: full FIXT two-dictionary (app-dict-by-`DefaultApplVerID`) resolution is a **bounded Phase-1 limitation, deferred** (parallel to the FR-005 enum deferral). For a FIXT session the session dictionary is FIXT.1.1 (transport/admin); validating an app message against it would over-reject, so app-message validation parity for FIXT is out of scope this feature. Recorded in Out-of-Scope + a B&L row.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Opt-in strict inbound validation (QuickFIX parity, except enum-value checks) (Priority: P1)

An operator integrating against a strict counterparty enables full dictionary-driven inbound validation on a session. Inbound messages that violate the data dictionary — header fields out of standard order, tags not defined for the message type, missing required fields, fields whose value is not type-conformant, or malformed repeating groups — are rejected with a FIX session-level Reject carrying the correct reason code, instead of being silently accepted. This makes fixpp's strict-mode behaviour match QuickFIX for the same inputs.

**Why this priority**: This is the core value of the feature — it turns a dead, fully-built validator into a usable conformance mode and resolves the headline QuickFIX divergence. Without it the feature delivers nothing.

**Independent Test**: Configure a session with validation enabled; feed inbound messages exhibiting each violation class; assert each is rejected with the expected `SessionRejectReason` and that a well-formed message is still accepted and dispatched.

**Acceptance Scenarios**:

1. **Given** a session with inbound validation enabled, **When** an inbound message arrives with a body field appearing before `MsgType(35)` (header out of order), **Then** the message is rejected with a session Reject (MsgType 3) carrying `SessionRejectReason=14` and is not dispatched to the application.
2. **Given** a session with validation enabled, **When** an inbound message of a known type carries a tag not defined for that type, **Then** it is rejected with `SessionRejectReason=2` (unexpected tag).
3. **Given** a session with validation enabled, **When** an inbound message is missing a field the dictionary marks required for its type, **Then** it is rejected with `SessionRejectReason=1` (required tag missing).
4. **Given** a session with validation enabled, **When** an inbound message carries a field whose value is not conformant to its declared type, **Then** it is rejected with `SessionRejectReason=5` (value is incorrect / out of range for this tag).
5. **Given** a session with validation enabled, **When** a fully dictionary-conformant inbound message arrives, **Then** it passes validation and is dispatched normally.

---

### User Story 2 - Lenient-by-default preservation (Priority: P1)

An existing integrator upgrades to the release carrying this feature without changing any configuration. Their sessions behave exactly as before: out-of-order fields, extra tags, and other dictionary deviations that were previously accepted continue to be accepted, with no new rejects and no added per-message cost.

**Why this priority**: Equal-priority to US1. The new validation MUST be strictly opt-in; a default-on validator would be a breaking wire-behaviour change and an interop/perf regression. This guarantee is what makes US1 safe to ship.

**Independent Test**: Run the existing session/interop test corpus with default configuration (validation not enabled); assert behaviour and emitted bytes are identical to the prior release — no new rejects, same dispatch outcomes.

**Acceptance Scenarios**:

1. **Given** a session with default configuration (validation not enabled), **When** an inbound message arrives with header fields out of order or with an undefined tag, **Then** it is accepted and processed exactly as in the prior release (no Reject emitted on account of validation).
2. **Given** default configuration, **When** the full existing inbound-handling test corpus runs, **Then** every outcome is unchanged from the prior release (byte-identical no-op).

---

### User Story 3 - Engine fails fast on a missing time source (Priority: P2)

An operator misconfigures an engine without a time source. Instead of starting and silently producing incorrect timestamp behaviour, the engine refuses to come up (or refuses to accept the session) and surfaces a clear, distinct error so the operator can fix the configuration.

**Why this priority**: High-value correctness guard but smaller and independent of the validator work; a genuinely-unset clock is a configuration error no correct caller hits, so it is lower urgency than the conformance gate but still a real latent defect (B-007-2).

**Independent Test**: Construct an engine configuration with no time source and drive the lifecycle entry point; assert it returns the `clock_not_set` error and does not become operational. Repeat with a valid time source and assert normal startup.

**Acceptance Scenarios**:

1. **Given** an engine configuration whose time source is unset, **When** the engine lifecycle gate runs, **Then** it returns the `clock_not_set` error and the engine does not become operational.
2. **Given** an engine configuration with a valid time source, **When** the lifecycle gate runs, **Then** it succeeds and the engine operates normally — no behaviour change versus the prior release.

---

### Edge Cases

- **Validation enabled but no data dictionary available to the session**: treated as a configuration error (fail-closed) per FR-011 — the system rejects the setup rather than silently skipping validation.
- **Enum-value violations under validation**: enum-value checking is deliberately out of scope this feature (the dictionary carries no enum-value tables yet). A field whose value is a wrong enum constant but a correct type is **accepted** even with validation enabled. This is a documented Phase-1 limitation, deferred to the 2c enum-table work.
- **Interaction with existing session checks**: validation runs **before** the sequence-number gate (FR-003). The existing targeted session checks (CompID, sequence numbers, BeginString, PossDup handling) remain authoritative for the messages that pass validation and are not weakened (FR-010).
- **A message that fails both sequence-number and dictionary validation**: dictionary validation runs first; the message is rejected for the validation failure and its sequence number is not processed/advanced.
- **A malformed establishing Logon under validation**: validation applies to the Logon too (FR-003) and runs **before `interpret_logon()`** (validate-first), so a dictionary-invalid Logon is rejected with `Reject(35=3)` rather than hitting `interpret_logon`'s silent Disconnect, and the session is not established (QuickFIX parity). A dict-clean Logon that nonetheless fails CompID/BeginString keeps its existing silent-Disconnect disposition (FR-010) — see the data-model Logon-arm overlap-precedence rows.
- **Drain states (`LogoutSent`/`Disconnected`)**: these states drain all non-Logout inbound with no seqnum advance and no dispatch; the validate gate does NOT run in them (FR-003 scope), so a malformed frame received while draining is still silently dropped, not Reject-looped.
- **Inbound `Reject(35=3)` / `Logout(35=5)`**: the existing no-reject-loop guard exempts these from triggering a session Reject; that exemption is preserved even with validation enabled (a malformed `35=3`/`35=5` is not Reject-looped — FR-004).
- **FIXT application messages**: Phase-1 validates against the session-held dictionary only; full FIXT two-dictionary (application dictionary resolved from `DefaultApplVerID`) validation is a documented Phase-1 limitation, deferred (see Clarifications 2026-06-16 + Out-of-Scope).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST expose an opt-in, per-session configuration setting that enables dictionary-driven inbound message validation. The setting MUST default to **disabled**, alongside the existing inbound-validation toggles.
- **FR-002**: When validation is disabled (the default), inbound message handling MUST be behaviourally identical to the prior release — no new rejects, no change to dispatch outcomes, and no construction or invocation of the validator on the message path.
- **FR-003**: When validation is enabled, the system MUST validate every inbound message that is **processed** — in the FSM states that process inbound (`NotConnected`, `LogonSent`, `LogonReceived`, `Active`), including the establishing Logon — against the session's data dictionary for: (a) standard-header field order, (b) tags not defined for the message type, (c) presence of required fields, (d) field-value type conformance, and (e) repeating-group structure. Validation MUST run **per-arm, before that state's sequence-number gate** — and, in the Logon-bearing arms (`NotConnected`/`LogonSent`), **before `interpret_logon()` and the establishment checks (BeginString/CompID/MsgType, SendingTime, FIXT `1137`)** that today run as that arm's lead statements (validate-first). Otherwise a dictionary-invalid Logon that also fails `interpret_logon` (wrong CompID/BeginString, non-Logon first message) hits the existing **silent Disconnect (no `Reject`)** instead of `Reject(35=3)`. This way a structurally-invalid message is rejected regardless of its sequence number and does not advance sequence-number state (QuickFIX parity). The overlap-precedence rule for a dict-clean message is **FR-010 unchanged** (a dict-clean message keeps its existing disposition); the per-row witness list is in data-model "Logon-arm overlap precedence" + contracts C-2/C-3. The `LogoutSent`/`Disconnected` **drain** states are NOT in scope — they continue to drain non-Logout inbound with no validation, no seqnum advance, and no dispatch (FR-010 preservation). Enum-value conformance is NOT checked (FR-005).
- **FR-004**: On a validation failure (validation enabled, in a processing state), the system MUST reject the offending message with a FIX session-level Reject (MsgType 3) and MUST NOT dispatch it to the application, EXCEPT that the existing **no-reject-loop exemption for inbound `Reject(35=3)` and `Logout(35=5)` is preserved** (a malformed `35=3`/`35=5` is not Reject-looped — FR-010). The `SessionRejectReason` MUST match what the validator actually surfaces: header out of order → **14**; undefined/unexpected tag → **2**; required field missing **and all repeating-group structure failures** (delimiter misplacement, NumInGroup count mismatch) → **1** (the validator surfaces group-structure failures as `wire_required_field_missing`; there is no distinct group reason in Phase-1); value not type-conformant / out of range → **5** (emitted by the type arm — Int/Char/Float-format — not the Phase-1-dead enum arm); Float/decimal precision-loss → **6** (this is the ONLY case that yields reason 6 — `wire_field_value_truncated` fires only on the Float decimal-precision-loss remap, never as a generic "bad format value"). **Float parse-error remapping (audit finding — SPEC-FIXED)**: `decimal_t::parse` on a badly-formatted Float value can return `decimal_invalid_input` (slot 10) or `decimal_overflow` (slot 11) — non-`wire_*` errors. The validator's Float path (`validator.hpp:307-313`) currently passes these through directly, leaking non-wire errors out of `validate()`. The implementation MUST remap any Float parse error other than `decimal_precision_loss` to `wire_field_value_out_of_range` (→ reason **5** — type non-conformant), so that every error surfaced by `validate()` is a `wire_*` slot. Consequently: a badly-formatted Float (e.g., `"abc"`, value overflow) → reason **5**; Float with precision-loss only → reason **6**. See data-model E-4 + tasks T009a.
- **FR-005**: Enum-value conformance checking is OUT OF SCOPE for this feature; enum-value checks MUST be treated as passing (no rejects on enum value alone). This limitation MUST be documented in the behaviours-and-limitations catalogue and deferred to the 2c enum-table work.
- **FR-006**: Validation MUST be driven by the data dictionary already configured for the session; this feature MUST NOT introduce a separate validation-only dictionary configuration surface.
- **FR-007**: `Engine::start()` MUST return a result type (`expected_t<void>`) and MUST reject a configuration whose time source is unset, surfacing the existing `clock_not_set` error, instead of proceeding to an operational state. Callers check the returned result. (Accepts the `void`→`expected_t<void>` public-API change.)
- **FR-011**: When validation is enabled but the session has no data dictionary configured, the system MUST treat this as a configuration error (fail-closed) rather than silently disabling validation.
- **FR-008**: The clock-config gate MUST NOT be configurable — an unset time source is unconditionally invalid.
- **FR-009**: At default configuration the feature MUST introduce no production behaviour change: inbound validation is off by default, and the clock gate triggers only on a genuinely-unset time source that no correct caller produces.
- **FR-010**: The feature MUST NOT weaken, bypass, or reorder the existing session-establishment and steady-state checks (CompID, sequence-number, BeginString, PossDup, Logon handling) in a way that changes their current outcomes.

### Key Entities

- **Inbound validation setting**: a per-session opt-in flag governing whether dictionary-driven validation runs; default disabled. Sits beside the existing inbound-validation toggles.
- **Data dictionary**: the per-session description of valid message types, their valid/required fields, field types, and repeating-group structure; the authoritative input to validation. Already configured for sessions today.
- **Validation outcome → reject reason mapping**: the correspondence between each dictionary-violation class and the FIX `SessionRejectReason` emitted (14 / 2 / 1 / 5 / 6), where required-field-missing **and repeating-group structure failures** both map to **1** and reason **6** is Float/decimal precision-loss only (FR-004).
- **Engine time-source configuration**: the engine's configured clock; an unset value is the condition the clock gate rejects.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: With default configuration (validation disabled), 100% of the existing inbound-handling and interop test corpus produces outcomes identical to the prior release — zero new rejects, zero dispatch-outcome changes.
- **SC-002**: With validation enabled, a message with header fields in non-standard order is rejected with `SessionRejectReason=14`, matching QuickFIX for the same input (enum-value checks excepted — FR-005).
- **SC-003**: With validation enabled, messages exhibiting an undefined tag, a missing required field (or a malformed repeating group), a type-nonconformant value, and a Float/decimal precision-loss value are rejected with reasons **2**, **1**, **5**, and **6** respectively; a fully conformant message is accepted and dispatched. The reason-**5** witness MUST exercise the **type arm** (e.g. a non-numeric `Int`/multi-byte `Char` value), not the Phase-1-dead enum arm (which always passes — FR-005). Wrong enum *values* of a correct type are NOT rejected (FR-005), so this matches QuickFIX except for enum-value checks.
- **SC-004**: An engine configured with no time source is refused at its lifecycle gate with the distinct `clock_not_set` error and never becomes operational; an engine with a valid time source starts and operates unchanged.
- **SC-005**: The default (validation-disabled) inbound path performs no validator construction or invocation — verifiable by inspection/instrumentation — so there is no added per-message cost at default.

## Assumptions

- **Validation applies to every inbound message processed in the inbound-processing FSM states** (`NotConnected`/`LogonSent`/`LogonReceived`/`Active` — session/admin and application, including the establishing Logon) and runs per-arm before that state's sequence-number gate — and, in the `NotConnected`/`LogonSent` Logon arms, before `interpret_logon()` and the establishment checks (validate-first) — per QuickFIX parity (resolved in Clarifications; scoped in FR-003). The `LogoutSent`/`Disconnected` drain states are excluded and the `35=3`/`35=5` no-reject-loop exemption is preserved; the existing session checks remain authoritative for messages that pass validation (FR-010).
- **The engine clock-config gate is enforced at `Engine::start()`**, which changes from `void` to `expected_t<void>` (resolved in Clarifications).
- **The production dictionary can feed five of the six validator inputs today** (field validity, required fields, repeating-group structure, field type, header order); only enum-value tables are missing, which is why FR-005 scopes enum checks out.
- **No new public configuration dictionary or external API surface** beyond the single per-session opt-in flag and (at most) one engine-lifecycle signature adjustment.

## Out of Scope

- Enum-value conformance checking and the enum-value dictionary tables (deferred to the 2c work).
- **FIXT two-dictionary validation for application messages**: Phase-1 validates against the session-held `cfg.dictionary` only; full FIXT resolution of a separate application dictionary via `DefaultApplVerID` (QuickFIX `Session.cpp:1218-1229`) is deferred. For FIXT sessions, application-message validation parity is out of scope this feature (B&L row + Clarifications 2026-06-16).
- Making the clock-config gate configurable.
- Any change to default wire behaviour or to the existing session checks.
- Validation in the `LogoutSent`/`Disconnected` drain states (those states keep their current drain semantics — FR-003).
- A general-purpose outbound message validator (this feature is inbound-only).

## Normative References

- `[2b §6.5.1]` standard-header field order; `[2b §6.5.3]` field-value range/type; `[2b §6.5.4]` required fields; `[2b §6.5.5]` unexpected (undefined) tags — the dictionary-validator's rule basis.
- `[FIX50SP2 §2.1]` (Session-level error processing — the FIX 5.0 SP2 / FIX-SL section enumerating the `SessionRejectReason(373)` values; the project anchor for tag 373, per `[2b §6.5]` rule 5): 1 (required tag missing), 2 (tag not defined for this message type), 5 (value is incorrect / out of range for this tag), 6 (incorrect data format for value), 14 (tag specified out of required order).
- `[2d §4.4]` engine clock configuration; `validate_engine_config` → `clock_not_set` (`core::error` slot 54).
- `[2d §4.5] / [2d §6.1]` `invalid_session_config` (`core::error` slot 53) — the config fail-closed error for FR-011.
- QuickFIX-cpp parity oracle. The file lives at the **parent workspace root, OUTSIDE this submodule subtree** (`research/G19-fix-fpml-iso20022/` is the submodule), so the path is **not reachable from the submodule cwd**: `<parent-workspace-root>/reference-engines/quickfix-cpp/src/C++/Session.cpp:1218-1229` (verified to match the excerpt below). The minimal load-bearing excerpt is quoted here as the reviewable oracle (validate precedes `nextLogon`/`verify`), so Gate A reasoning does not depend on the external checkout being reachable from the submodule:

  ```cpp
  if (m_sessionID.isFIXT() && message.isApp()) {
    // ... resolve applVerID, getApplicationDataDictionary(applVerID) ...
    DataDictionary::validate(message, &sessionDataDictionary, &applicationDataDictionary);
  } else {
    sessionDataDictionary.validate(message);   // <- validate BEFORE nextLogon/verify
  }
  if (msgType == MsgType_Logon) { nextLogon(message, now); ... }
  ```

  This confirms both (a) validate-before-seqnum/verify ordering and (b) the FIXT two-dictionary path Phase-1 defers (see Out-of-Scope).
