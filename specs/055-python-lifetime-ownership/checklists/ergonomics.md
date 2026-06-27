# Pickle-Ban & Teardown-Ergonomics Requirements Checklist: Python bindings ownership / lifetime layer (PY-004)

**Purpose**: Validate that the cross-process-safety (pickle-ban) and teardown-ergonomics (context-manager, GC DeprecationWarning) REQUIREMENTS are complete, unambiguous, consistent, and measurable — with special attention to the value-type scope boundary that caused an SC-003 contradiction at Gate A round 1. Tests requirements writing, not code.
**Created**: 2026-06-27
**Feature**: [spec.md](../spec.md) · [contracts/python-lifetime-api.md](../contracts/python-lifetime-api.md) · [quickstart.md](../quickstart.md)

## Requirement Completeness

- [ ] CHK001 Is the pickle-ban required for ALL FIVE handle-bearing wrappers (Engine/Session/Message/Application/Dictionary)? [Completeness, Spec §FR-013, contracts C-5]
- [ ] CHK002 Is the content of the pickle-ban error specified (a `TypeError` whose message explains native handles cannot cross process boundaries)? [Completeness, Spec §FR-013, contracts C-5]
- [ ] CHK003 Are context-manager requirements specified for BOTH `Engine` and `Session` (`__enter__`/`__exit__` invoking `close()`)? [Completeness, Spec §FR-009]
- [ ] CHK004 Is the GC-only teardown behavior fully specified (emit `DeprecationWarning`, attempt best-effort native cleanup, and state explicit close is the only guaranteed-correct path)? [Completeness, Spec §FR-010]

## Requirement Clarity

- [ ] CHK005 Is the pickle-ban scope boundary stated unambiguously — handle-bearing wrappers ONLY, with value-types explicitly NOT introduced — so no SC-003 round-trip contradiction is reintroduced? [Clarity, Spec §FR-014, SC-003]
- [ ] CHK006 Is "best-effort cleanup" defined (what is and is NOT guaranteed) for the `__del__` path, given finalisation order within a GC cycle is unspecified? [Clarity, Ambiguity, Spec §FR-010, contracts C-3]
- [ ] CHK007 Is it clear WHICH reduce hooks the pickle-ban must override (`__reduce_ex__` and/or `__reduce__`) to be effective? [Clarity, contracts C-5]

## Requirement Consistency

- [ ] CHK008 Is the value-type deferral consistent across Spec §FR-014, §SC-003, the US3 Independent Test, and the Assumptions section — with NO residual claim of a value-type pickle round-trip? [Consistency, Spec §FR-014/SC-003/US3, requirements.md Gate-A-r1 note]
- [ ] CHK009 Does the pickle `TypeError` message requirement in contracts C-5 match the FR-013 wording (no drift between spec and contract)? [Consistency, Spec §FR-013 ↔ contracts C-5]
- [ ] CHK010 Is the documented close-on-exit ORDER (each session closed, its messages invalidated first, engine handle destroyed last) consistent between US2 AC3, FR-007, and contracts C-3? [Consistency, Spec §US2 ↔ FR-007 ↔ contracts C-3]

## Acceptance Criteria Quality & Measurability

- [ ] CHK011 Is SC-003 ("100% of pickle attempts on handle-bearing wrappers raise `TypeError`") objectively measurable AND scoped to exclude out-of-scope value-types? [Measurability, Spec §SC-003]
- [ ] CHK012 Can the documented teardown ORDER on context-manager exit be objectively verified (an observable ordering assertion), not just asserted as "deterministic"? [Measurability, Spec §US2 Independent Test, FR-009]
- [ ] CHK013 Is the DeprecationWarning condition measurable (emitted on GC-only teardown, i.e. when not previously explicitly closed)? [Measurability, Spec §FR-010]

## Scenario & Edge-Case Coverage

- [ ] CHK014 Are requirements defined for a context-manager exit AFTER an explicit `close()` (idempotent no-op, no second teardown, no warning)? [Coverage, Edge Case, Spec §FR-008/FR-009]
- [ ] CHK015 Is cross-module `__del__` ordering at interpreter shutdown explicitly stated as NOT relied upon (a documented boundary)? [Coverage, contracts C-3, Spec §FR-010]
- [ ] CHK016 Is the `multiprocessing.Pool(...).map(...)` footgun (the motivating cross-process scenario) covered by the pickle-ban requirement for the objects that would actually be pickled (incl. inbound `Message`)? [Coverage, Spec §US3, quickstart]

## Ambiguities & Gaps

- [ ] CHK017 Does the spec define whether `__del__`'s best-effort teardown emits the `DeprecationWarning` even when the handle is ALREADY dead/closed, to avoid a spurious warning on the idempotent path? [Gap, Spec §FR-010, FR-008]
- [ ] CHK018 Is the future-direction note ("v1.x makes GC-only teardown an error") scoped clearly as non-normative for v1.0, so it is not mistaken for a current requirement? [Ambiguity, quickstart, Spec §FR-010]

## Notes

- This checklist tests REQUIREMENTS quality, not pickle/teardown behavior at runtime.
- CHK005/CHK008 are the highest-value items: the SC-003/US3 value-type round-trip contradiction was already caught and fixed at Gate A round 1 (see requirements.md notes); these items guard against its reintroduction.
- Traceability: 18/18 items carry a spec/contracts/quickstart reference or a `[Gap]`/`[Ambiguity]` marker.
