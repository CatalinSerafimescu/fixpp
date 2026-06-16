# Checklist: API / Contract Requirements Quality — 041 Validation Gate Wiring

**Purpose**: Unit-test the requirements for the public/contract surface (opt-in validation setting, `Engine::start()` return-type change, reject-reason mapping) before implementation.
**Created**: 2026-06-16
**Audience**: Gate B reviewer
**Feature**: [spec.md](../spec.md)

## Requirement Completeness

- [x] CHK001 Is the opt-in validation setting fully specified — name intent, type, default value, and its co-location with the existing inbound-validation toggles? [Completeness, Spec §FR-001, Key Entities] — PASS: data-model E-1 names the field (`bool validate_inbound_messages`), type (`bool`), default (`false`), and placement (`session_config.hpp` after `validate_sequence_numbers`); FR-001 states intent and default-disabled; quickstart.md §Enable shows usage. Complete.
- [x] CHK002 Is the `Engine::start()` post-change return type AND the distinct error it surfaces on an unset clock both specified? [Completeness, Spec §FR-007] — PASS: FR-007 states return type (`expected_t<void>`), error value (`clock_not_set`), and the `[[nodiscard]]` obligation; contracts C-4 provides the full signature; data-model E-5 + research R-5 detail the enforcement point. `core::error::clock_not_set` (slot 54) verified in `error.hpp:155`. Complete.
- [x] CHK003 Is the complete violation-class → `SessionRejectReason` mapping enumerated for every class the validator can surface (14/2/1/5/6) with no class left unmapped? [Completeness, Spec §FR-004] — SPEC-FIXED: FR-004 and data-model E-4 enumerate five reasons (14/2/1/5/6) covering `wire_*` slots 38–42, but this was INCOMPLETE: `decimal_t::parse` (invoked in the validator's Float type arm at `validator.hpp:301-313`) can also return `decimal_invalid_input` (slot 10) or `decimal_overflow` (slot 11) for a badly-formatted Float value; the current Float arm passes these through directly as `inner_err` when `inner_err != decimal_precision_loss`, leaking non-`wire_*` errors out of `validate()`. These are outside {38–42} and not mapped to any `SessionRejectReason`. Fixed by adding a normative sentence to FR-004 (spec.md §FR-004) and a corresponding note to data-model E-4 requiring the implementation to remap any non-`decimal_precision_loss` Float parse error to `wire_field_value_out_of_range` (40) → reason 5. New task T009a added to tasks.md to implement this remap and test it. Affected: `spec.md §FR-004`, `data-model.md §E-4`, `tasks.md §T009a`.
- [x] CHK004 Is the fail-closed config-error path (validation enabled + no dictionary) specified with a distinct, named error value rather than a generic failure? [Completeness, Spec §FR-011] — PASS: FR-011 states the fail-closed requirement; contracts C-5 names `core::error::invalid_session_config` (slot 53, `error.hpp:148`) as the exact error returned from `register_session`; research R-6 distinguishes it from `session_invalid_argument(119)`. Distinct named error, not generic. Complete.
- [x] CHK005 Is the structure of the emitted validation Reject specified (MsgType 3, reason code, and RefTagID presence/absence)? [Completeness, Spec §FR-004] — PASS: FR-004 specifies `Reject (MsgType 3)` with `SessionRejectReason(373)` matching the validator output; contracts C-2 spells out the Reject per violation class; data-model E-4 notes that `build_reject` already emits `371` when RefTagID > 0 (reused unchanged, RC-C). The presence/absence rule for RefTagID (present when a tag is identified, 0 otherwise) is covered by the existing `build_reject` behaviour documented in research R-3. Complete.

## Requirement Clarity

- [x] CHK006 Are caller obligations for the new `expected_t<void>` result unambiguous (result MUST be checked; non-operational on error)? [Clarity, Spec §FR-007, SC-004] — PASS: contracts C-4 states `[[nodiscard]]` on `Engine::start()`, specifying that on error the engine does not become operational and no session loops are spawned; SC-004 phrases the check obligation measurably; quickstart.md shows the check pattern. Obligations are unambiguous. Complete.
- [x] CHK007 Is "before the sequence-number gate" and "validate-first before `interpret_logon()`" defined precisely enough to place the gate per FSM arm without interpretation? [Clarity, Spec §FR-003, Clarifications Gate A round 1] — PASS: FR-003 specifies per-arm insertion in the four processing states and the validate-first ordering for `NotConnected`/`LogonSent` (before `interpret_logon()` and before establishment checks); data-model pseudocode gives the three arm-groups with line references (`session.cpp:1712`, `session.cpp:3341`, etc.); contracts C-3 restates the per-arm ordering. Gate A rounds 1–3 converged on this exact wording. No interpretation required. Complete.
- [x] CHK008 Is the requirement that no new public validation-only dictionary/config surface is introduced stated unambiguously (single flag + at most one lifecycle signature change)? [Clarity, Spec §FR-006, Assumptions] — PASS: FR-006 states "this feature MUST NOT introduce a separate validation-only dictionary configuration surface"; Assumptions paragraph restates "no new public configuration dictionary or external API surface beyond the single per-session opt-in flag and (at most) one engine-lifecycle signature adjustment". Unambiguous. Complete.

## Requirement Consistency

- [x] CHK009 Are the no-reject-loop exemptions for inbound `Reject(35=3)`/`Logout(35=5)` stated consistently between FR-004, FR-010, and Edge Cases (preserved even with validation enabled)? [Consistency, Spec §FR-004, FR-010, Edge Cases] — PASS: FR-004 explicitly states the exemption in-line ("EXCEPT that the existing no-reject-loop exemption for inbound `Reject(35=3)` and `Logout(35=5)` is preserved"); FR-010 preserves the exemption as part of "not weaken…existing session checks"; Edge Cases section names it directly; contracts C-2 and data-model row (f) both repeat it. Consistent across all four locations. The FR-010 cross-reference in FR-004 is an umbrella reference that suffices. Complete.
- [x] CHK010 Is the reason-code basis consistent between FR-004 and the Normative References (each of 14/2/1/5/6 traced to `[FIX50SP2 §2.1]` / `[2b §6.5]`)? [Traceability, Spec §FR-004, Normative References] — PASS: spec Normative References cites `[FIX50SP2 §2.1]` (Session-level error processing, the `SessionRejectReason(373)` taxonomy) and `[2b §6.5.1/6.5.3/6.5.4/6.5.5]` for the validator's rule basis; these match the five reason codes (14/2/1/5/6) enumerated in FR-004. Note: `[2b §6.5.1]` notation refers to rule-numbered items within the single `### 6.5 Validation` section in `2b-wire.md` (no literal `§6.5.1` heading exists; the notation matches the `[2b §6.5] rule 1/3/4/5` usage in research.md's Normative References). The anchor is resolvable and the mapping is complete. Verified [2b §6.5] resolves to `### 6.5 Validation`; [FIX50SP2 §2.1] is the external FIX standard, not a local file. Complete.
- [x] CHK011 Does the spec consistently scope reason **1** to cover required-field-missing AND all repeating-group structure failures, and reason **6** to Float/decimal precision-loss only? [Consistency, Spec §FR-004, SC-003, Key Entities] — PASS: FR-004 states reason 1 covers "required field missing and all repeating-group structure failures" and reason 6 is "Float/decimal precision-loss → 6 (this is the ONLY case that yields reason 6)"; SC-003 repeats reason 1 covers "malformed repeating group" and reason 6 is Float/decimal; Key Entities paragraph on the mapping echoes the same; contracts C-2 and data-model E-4 are consistent. No inconsistency found. Complete.

## Acceptance Criteria Quality

- [x] CHK012 Does each US1 acceptance scenario map one violation class to exactly one observable reason code (objectively checkable)? [Measurability, Spec §US1, SC-002, SC-003] — PASS: US1 scenarios 1–5 each map a single violation to a single reason code or to "accepted and dispatched"; SC-002 specifies `SessionRejectReason=14` for out-of-order; SC-003 specifies 2/1/5/6 for the remaining four violation classes and "accepted" for the conformant case; tasks T012 witnesses one per class. Each is objectively checkable by asserting the emitted tag `373` value. Complete.
- [x] CHK013 Is the reason-5 witness requirement specified to exercise the **type arm** (not the Phase-1-dead enum arm) so the acceptance test is non-degenerate? [Clarity, Spec §SC-003, FR-004] — PASS: SC-003 explicitly states "The reason-5 witness MUST exercise the type arm (e.g. a non-numeric Int/multi-byte Char value), not the Phase-1-dead enum arm (which always passes — FR-005)"; FR-004 notes the enum arm "also yields slot 40 but is dead in Phase-1"; contracts C-2 specifies "type arm — e.g. non-numeric Int, multi-byte Char" for the reason-5 cell; tasks T012 mirrors this. Non-degenerate test is mandated. Complete.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 12 |
| SPEC-FIXED | 1 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | **13** |

### SPEC-FIXED items
- CHK003 — Float parse-error completeness gap: `decimal_invalid_input`/`decimal_overflow` leaked as non-`wire_*` errors from the validator's Float arm; added Float parse-error remap requirement to FR-004 + data-model E-4 + new task T009a; affected: `spec.md §FR-004`, `data-model.md §E-4`, `tasks.md §T009a`.

### DD-DECIDED items
None.

### WAIVED items
None.

Anchors spot-verified:
- `[2b §6.5]` (`### 6.5 Validation`) — resolves in `.specify/2b-wire.md` (no literal `§6.5.1` subheading; rule-number notation matches rule-indexed items within §6.5).
- `[FIX50SP2 §2.1]` — external FIX 5.0 SP2 standard (Session-level error processing / `SessionRejectReason(373)` taxonomy); not a local file; cited consistently across spec Normative References and research Normative References.
- `[2d §4.4]` (`### 4.4 EngineConfig`) — resolves at line 412 of `.specify/2d-threading.md`; `clock_not_set` + `validate_engine_config` references confirmed.
- `[2d §4.5]` (`### 4.5 SessionConfig`) — resolves at line 496 of `.specify/2d-threading.md`; `invalid_session_config` confirmed.
- `[2d §6.1]` (`### 6.1 Strand semantics`) — resolves at line 1069 of `.specify/2d-threading.md`; error-table entry confirms `invalid_session_config = §4.5 / §6.1`.
- All resolve in the design doc as used at the signed-off version.
