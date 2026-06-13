# Security Requirements Quality Checklist: Credential redaction at the message-store boundary

**Purpose**: Validate that the credential-handling requirements are complete, clear, consistent, and measurable — BEFORE implementation. Audience: Gate B reviewer.
**Created**: 2026-06-13
**Feature**: [spec.md](../spec.md)
**Focus**: secret-at-rest handling, wire-side non-regression, scope boundary.

## Threat / asset coverage

- [x] CHK001 - Is the protected asset (the session's OWN `Password(554)`) and the exposure (cleartext at rest in a persistent store) explicitly stated, distinct from the peer's credential? [Completeness, Spec §US1/Context] — PASS: spec.md §Context names the session's own outbound Logon `Password(554)` as the exposed asset; FR-006 explicitly scopes to outbound direction only, excluding peer/inbound frames.
- [x] CHK002 - Are ALL credential-bearing persistence surfaces enumerated and dispositioned (message store IN scope; logs / OTEL / metrics stated already-clean)? [Coverage, Spec §Context] — PASS: spec.md §Context states "Application logs, telemetry spans, and metrics already carry no credential material — this feature concerns **only** the message-store persistence boundary." All surfaces enumerated and dispositioned.
- [x] CHK003 - Is it specified that inbound/peer frames are out of scope (never persisted), so the requirement is bounded to the session's own outbound credential? [Clarity, Spec §FR-006] — PASS: FR-006 explicitly states "Scope is restricted to the **outbound direction** and the **Logon (`35=A`)** message type. Inbound/peer frames and all non-Logon outbound frames MUST be persisted unchanged."

## Secret-handling correctness

- [x] CHK004 - Is "masked" defined precisely enough to be verifiable (the 554 VALUE replaced, same length, not the tag/`=`; the literal password absent)? [Measurability, Spec §FR-001/SC-001] — PASS: FR-001 defines masking as replacing the 554 **value** with a placeholder (tag/`=` unchanged); FR-003 mandates same byte length; SC-001 requires the literal password appears zero times; data-model.md E1 specifies the exact byte overwrite semantics (value extent from past `=` to next SOH).
- [x] CHK005 - Is the requirement that the WIRE frame remains the unmasked credential stated as a hard invariant separate from the at-rest requirement (no conflation of store vs wire)? [Consistency, Spec §FR-002/US2] — PASS: FR-002 ("Persistence masking MUST NOT alter the wire frame"), US2, and INV-034-3 each independently state the wire/store separation as distinct hard invariants; SC-002 provides a measurable test.
- [x] CHK006 - Is `Username(553)` explicitly excluded from masking, with a rationale (identity, not secret), so the scope is unambiguous? [Clarity, Spec §FR-005] — PASS: FR-005 states "`Username(553)` MUST NOT be masked (it is an identity, not a secret)"; spec.md §Assumptions confirms this with the same rationale.
- [x] CHK007 - Is the safety argument that a masked stored frame is never replayed verbatim (admin→GapFill) stated as a load-bearing premise, with the forward-constraint if that ever changes? [Completeness, Spec §Assumptions / research R7] — PASS: spec.md §Assumptions explicitly states admin→GapFill as a "relied-upon invariant" and records the forward constraint ("if a future feature ever introduces verbatim admin-frame replay, it MUST re-derive credentials from configuration"); research R7 and data-model.md INV-034-5 also state this; FR-010 requires it in the B&L catalogue.

## Failure / edge handling

- [x] CHK008 - Is the over-bound disposition specified to be fail-closed (never persist cleartext) rather than fail-open? [Edge Case, contracts §C2] — DD-DECIDED §C2/R3: contracts §C2 step-2 specifies "fail closed: skip the store write for this frame … never persist cleartext, but still transmit the original frame." Research R3 justifies this as wire-safe (admin→GapFill). The C3 open()-guard makes the branch production-unreachable; T010 earns the BRDA via fault injection. Settled in the Gate A round 2 bundle.
- [x] CHK009 - Are the framing-safety preconditions of the secret value (no SOH/`=` in 554, enforced at build) stated so same-length masking provably cannot corrupt the frame? [Assumption, Spec §Edge Cases] — PASS: spec.md §Edge Cases states "Password value at the framing boundary: the 554 value cannot contain a field delimiter (SOH) or `=` (already enforced when the Logon is built); same-length masking therefore cannot corrupt framing." Data-model.md I-E1-4 records this as an invariant citing 033 FQ-3 as the enforcement site.
- [x] CHK010 - Is the behavior on a Logon carrying a username but no password (and an empty-password) covered by the requirements? [Coverage, Gap] — PASS: username-only case is covered by FR-005 ("A Logon without a 554 field MUST be persisted unchanged") and SC-003 (credential-free → byte-identical). The empty-password case (`\x01554=\x01`: value extent = zero bytes) is covered by C1/E1's masker contract: the value overwrite loop writes zero bytes, frame is byte-unchanged — no framing corruption (I-E1-4 still holds, I-E1-1 length invariant trivially holds). T002 witness (c) and (e) cover no-genuine-554 / no-op returns; the empty-value sub-case is deterministically safe by the same algorithm.
- [x] CHK011 - Is the deliberate divergence from QuickFIX parity (which persists cleartext) stated, so a reviewer doesn't read it as a conformance regression? [Consistency, Spec §Assumptions] — PASS: spec.md §Assumptions states "QuickFIX-cpp / QuickFIX-J persist the sent Logon password identically (verified: their FileStore writes the raw Logon string with no redaction); this feature is deliberate hardening **beyond** reference-engine parity, not a conformance fix."
- [x] CHK012 - Is it stated that NO application-layer encryption is introduced (TLS-only posture preserved, `[const §XV.10]`)? [Consistency, plan §Constitution Check] — PASS: plan.md §Constitution Check Article XII entry states "No app-layer crypto introduced (`[const §XV.10]` untouched)."

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 10 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 1 |
| WAIVED | 0 |
| **Total** | **12** |

### SPEC-FIXED items
_(none)_

### DD-DECIDED items
- CHK008 — anchor `contracts §C2 / research R3 / Gate A round 2`; rationale: over-bound fail-closed disposition, wire-safety proof, and production-unreachability via C3 open()-guard are all settled in the Gate A bundle; production branch is dead-defensive code covered by fault-injection seam.

### WAIVED items
_(none)_

Anchors spot-verified: `spec.md §US1/Context`, `spec.md §FR-001..007`, `spec.md §Assumptions`, `spec.md §Edge Cases`, `data-model.md E1/INV-034-*`, `contracts/store-redaction.md C1/C2/C3`, `research.md R7`, `plan.md §Constitution Check` — all resolve in the Gate A round 2 converged bundle (2026-06-13).

### Realizability sub-check
E1 `mask_tag554_same_length_inplace` is a free function with signature `inline bool mask_tag554_same_length_inplace(std::span<std::byte>) noexcept`. The implementation uses a `std::array<std::byte, kMaxMaskableLogonBytes>` buffer allocated in the coroutine frame — both complete, non-deferred types. There is no struct/class held by value with a `unique_ptr`-to-incomplete member, no by-value member of a forward-declared deferred-spec type, no base class requiring completeness. **Clean — no latent incomplete-type defect.**
