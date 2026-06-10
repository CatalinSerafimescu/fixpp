# Compatibility / No-op-Paths Requirements Checklist: RefreshOnLogon

**Purpose**: Validate that the requirements governing the feature's **zero-regression** surface — the default-off byte-identity, the non-persistent-store no-op, the `bilateral_strict` suppression (the 025 re-hydrate delta vs the deferred inherited cold-open gap), and the store-read-failure disposition — are complete, clear, consistent, and measurable. This is co-equal with the feature itself: the knob is opt-in and every existing session must be untouched.
**Created**: 2026-06-09
**Feature**: [spec.md](../spec.md) · [contracts/refresh-knob.md](../contracts/refresh-knob.md) · [data-model.md](../data-model.md)

## Default-off byte-identity

- [x] CHK001 Is the explicit default specified as **off** (no implicit default, [const §XII.5]) and tied to the polarity rationale (matches the other false-default additive knobs 021/022/024/026/027)? [Completeness, FR-001 / Assumptions / data-model Entities] — PASS: FR-001 states "defaulting to off"; Assumptions states "EXPLICIT per-field default (no-implicit-default, [const §XII.5])" and "Polarity matches the other false-default additive knobs 021/022/024/026/027"; data-model Entities states "explicit default `false` ([const §XII.5])."
- [x] CHK002 Is "no-op" quantified as **byte-for-byte identical on every path** (not merely "behaviour unchanged"), so the default-off requirement is objectively verifiable against the pre-feature baseline? [Measurability, FR-004 / FR-010 / INV-RoL-1] — PASS: FR-010 says "byte-for-byte identical to current behaviour on every path"; FR-004 says "byte-identical to current 029 behaviour"; INV-RoL-1 says "every path is byte-identical to 029."
- [x] CHK003 Is the requirement that the established 029 one-shot cold-open hydrate (INV-H3, latched, never re-runs) is **entirely unmodified** under the default stated, rather than implied by the absence of a change? [Clarity, FR-004 / Edge Cases / INV-RoL-1] — PASS: FR-004 says "a single cold-open one-shot hydrate that never re-runs on a subsequent logon/reconnect (INV-H3 unchanged)"; Edge Cases states "Default-off ⇒ 029 one-shot unchanged: with the knob off, the existing 029 cold-open one-shot hydrate (latched, never re-hydrated — INV-H3) is entirely unmodified."
- [x] CHK004 Is the verification basis for byte-identity specified as the **full existing session/recovery/029-hydrate regression suite remaining green** plus a default-value assertion, so SC-003 is measurable and not a subjective "looks unchanged"? [Measurability, SC-003 / W3 / US2 AS1–AS2] — PASS: SC-003 says "100% of existing session/recovery/029-hydrate regression witnesses remain green and behaviour is byte-identical to the pre-feature baseline"; US2 AS1 asserts the full regression suite green; US2 AS2 asserts default-value reading returns off; W3 in data-model covers both direct read-count and regression assertions.
- [x] CHK005 Does W3's "no re-read" requirement mandate checking the **store-read call-count directly** (a counting store), not a bypassable proxy, per the witness-asserts-the-named-postcondition discipline? [Measurability, W3 note / [[feedback_witness_asserts_named_postcondition_not_proxy]]] — PASS: data-model W3 footnote states "W3's 'no re-read' assertion must check the **store read call-count** directly (a counting test store), not a proxy — per [[feedback_witness_asserts_named_postcondition_not_proxy]]."

## Non-persistent-store no-op

- [x] CHK006 Is the non-persistent-store no-op specified as **zero store reads even with the knob enabled** (carrying over 029 INV-H4), with the discriminator named (`store_is_persistent_` / `yields_persistent_store()` captured at `open()`)? [Completeness, FR-005 / INV-RoL-2 / data-model Entities] — PASS: FR-005 says "no store read even with the knob enabled — carrying over 029 INV-H4"; INV-RoL-2 names the discriminator "`store_is_persistent_ == false` ⇒ no store read on refresh"; data-model Entities names "`store_is_persistent_` — the 029 discriminator (`MessageStoreFactory::yields_persistent_store()` captured at `open()`)".
- [x] CHK007 Is it stated that the non-persistent skip is enforced **inside `ensure_hydrated_`** (the `force` flag does NOT bypass the persistent-store skip), so a forced call on a non-persistent store is still a no-op? [Consistency, C2.3 / data-model truth table row 4] — PASS: C2.3 says "`force` does **not** alter the `store_is_persistent_` skip (`:576`): a non-persistent store is a no-op even under `force`"; data-model truth table row 4 confirms "non-persistent: no-op (INV-H4)" regardless of knob-on + lenient/unilateral; `:576` skip confirmed in live session.cpp.

## `bilateral_strict` suppression — the 025 re-hydrate delta

- [x] CHK008 Is the suppression scoped to the **2nd-and-subsequent re-hydrate** (the 025 delta) and explicitly NOT claimed for the cold-open seed, so "suppressed re-hydrate" is never conflated with "strict cold-open is safe"? [Clarity, FR-008 / INV-RoL-3 / SC-005] — PASS: FR-008 says "the per-logon **re-hydrate** (the 025 2nd-and-subsequent-logon delta) MUST apply only under a non-reset-announcing policy"; INV-RoL-3 says "This invariant covers the **re-hydrate**, not the cold-open seed: the strict + non-1 cold-open path is the inherited L-029-3 gap, NOT a 025 guarantee"; SC-005 explicitly scopes to "the 2nd-and-subsequent logon."
- [x] CHK009 Is the *reason* for suppression stated as a hard wire-correctness floor — a store-wins hydrate to a non-1 outbound under `bilateral_strict`'s unconditional `141=Y` would emit a malformed `141=Y`+non-1-body Logon, violating the FIX `141=Y` ⟹ `MsgSeqNum=1` rule — rather than presented as an arbitrary config restriction? [Clarity, FR-008 / Clarifications / C5.2] — PASS: FR-008 states the full rationale explicitly; Clarifications states "the only choice that keeps fixpp's wire behaviour matching all three reference engines and the FIX `141=Y` ⟹ `MsgSeqNum=1` rule"; C5.2 confirms the `bilateral_strict` unconditional `141=Y` is a fixpp-024 construct with no reference-engine analogue.
- [x] CHK010 Is the suppression's observable contract stated as **zero EXTRA reads beyond the 029 cold one-shot** AND **no NEW malformed Logon attributable to the knob** (establishment proceeds exactly as the knob-off strict path), giving W5a two distinct, measurable postconditions? [Measurability, SC-005 / W5a / INV-RoL-3] — PASS: SC-005 lists both postconditions explicitly as "(a) zero extra store reads beyond the 029 cold one-shot" and "(b) no NEW malformed Logon attributable to the knob"; data-model W5a row mirrors them; INV-RoL-3 states both in one sentence.
- [x] CHK011 Is the inherited **L-029-3 cold-open gap** stated as a property of the **policy, not the knob** — the 029 cold-open seed under `bilateral_strict` + non-1 store can itself emit a `141=Y`+non-1 Logon — and explicitly **deferred** (OUT OF 025 scope, routed to a 029/024 follow-up; folding the one-line outbound-seed guard would break FR-010 byte-identity)? [Consistency, FR-008 / Assumptions / Edge Cases / data-model D-RoL-6] — PASS: FR-008 says "Inherited gap (OUT OF 025's scope, tracked L-029-3)...a property of the **policy**, not of the refresh knob"; Edge Cases and Assumptions both state the fold-in rejection rationale (breaks FR-010 byte-identity, half-fix, unnecessary since 025's gate never reaches it); research D-RoL-6 records Gate A RESOLVED → DEFER.
- [x] CHK012 Is W5b labeled a **gap witness** with its "what it asserts / what it does NOT assert" boundary stated (it documents the inherited cold-open behaviour as-is and does NOT assert the cold Logon is well-formed), so it cannot be mistaken for a correctness witness? [Coverage, W5b / data-model note / [[feedback_witness_asserts_named_postcondition_not_proxy]]] — PASS: data-model W5b row explicitly says "clearly labeled 'inherited 029 gap, NOT a 025 guarantee' — must not be mistaken for a correctness witness"; the W matrix note says W5b "asserts ONLY what holds — it does NOT assert cold-open validity"; tasks T031 instructs "Clearly labeled: inherited-029 gap, NOT a 025 guarantee."
- [x] CHK013 Is the operator consequence documented — because `bilateral_strict` is the **default** policy, enabling `refresh_on_logon` is a no-op out of the box until a non-strict policy is selected — so the field doc and B&L do not over-promise? [Completeness, FR-008 / Clarifications / Assumptions] — PASS: Clarifications states "enabling `refresh_on_logon` out of the box is a no-op until the operator also selects a non-strict policy — documented in the config-field doc + B&L"; FR-008 says "Consequence: since `bilateral_strict` is the default, enabling `refresh_on_logon` is a no-op until a non-strict policy is selected"; tasks T003 mandates the field doc comment include "no-op under `bilateral_strict` (the default)."

## Store-read-failure disposition

- [x] CHK014 Is the refresh read-failure disposition specified as **reusing the existing 029 fatal-disconnect path with no partial seed**, and explicitly forbidding a new error slot or a different disposition? [Completeness, FR-006 / INV-RoL-6 / C2.5] — PASS: FR-006 says "handled by the existing 029 hydrate read-failure disposition (fatal disconnect, no partial seed); the refresh path MUST NOT introduce a new error slot or a different failure disposition"; INV-RoL-6 says "transitions to `Disconnected` with no partial seed (reuses the 029 disposition)"; C2.5 mirrors this precisely; `:585-593` fatal path confirmed in live session.cpp.
- [x] CHK015 Is the read-failure outcome measurable (transitions to `Disconnected`, no partial seed, no new error slot — fault-injection test), rather than a vague "handles errors gracefully"? [Measurability, SC-007 / W7] — PASS: SC-007 says "existing 029 fatal-disconnect disposition with no partial seed (verified by a fault-injection test), introducing no new error slot"; W7 specifies "assert `Disconnected`, no partial seed (manager unchanged from pre-read), no new error slot (reuses the 029 store-failure disposition)"; tasks T033 mandates a fault-injecting store that fails the read on the 2nd logon.

## Notes

- This checklist tests the **zero-regression / no-op** requirements; the re-hydrate mechanics live in `refresh-semantics.md` and the wire/parity invariance in `interop-wire.md`.
- The pivotal items are **CHK008/CHK011/CHK012** — the boundary between the *suppressed* 025 re-hydrate (INV-RoL-3) and the *deferred* inherited 029 cold-open gap (L-029-3). This is the feature's RC-1: the place a reviewer is most likely to conflate "suppressed re-hydrate" with "strict cold-open is now safe." An unresolvable anchor on these three is a real gap.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 15 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | **15** |

### SPEC-FIXED items
None.

### DD-DECIDED items
None.

### WAIVED items
None.

Anchors spot-verified: FR-001, FR-004, FR-005, FR-006, FR-008, FR-010, INV-RoL-1, INV-RoL-2, INV-RoL-3, INV-RoL-6, C2.3, C2.5, SC-003, SC-005, SC-007, W3, W5a, W5b, W7, data-model truth table (all 4 rows), Edge Cases (Default-off, Non-persistent, bilateral_strict suppression), D-RoL-6 — all resolve in Gate-A-converged spec/plan/data-model/contracts/research (submodule head `fb33a65`, session.cpp unchanged from 029 merge `0b9c8b8`). Live source anchors `:576` (store_is_persistent_ skip), `:564-566` (hydrated_ latch), `:585-593` (fatal disconnect path) confirmed.
