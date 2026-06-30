# Pickle-Ban & Teardown-Ergonomics Requirements Checklist: Python bindings ownership / lifetime layer (PY-004)

**Purpose**: Validate that the cross-process-safety (pickle-ban) and teardown-ergonomics (context-manager, GC DeprecationWarning) REQUIREMENTS are complete, unambiguous, consistent, and measurable — with special attention to the value-type scope boundary that caused an SC-003 contradiction at Gate A round 1. Tests requirements writing, not code.
**Created**: 2026-06-27
**Feature**: [spec.md](../spec.md) · [contracts/python-lifetime-api.md](../contracts/python-lifetime-api.md) · [quickstart.md](../quickstart.md)

## Requirement Completeness

- [x] CHK001 Is the pickle-ban required for ALL FIVE handle-bearing wrappers (Engine/Session/Message/Application/Dictionary)? [Completeness, Spec §FR-013, contracts C-5] — PASS: FR-013 enumerates all five by name: "Handle-bearing wrappers (`Engine`, `Session`, `Message`, `Application`, `Dictionary`) MUST refuse to be pickled"; contracts C-5 repeats the same five-class enumeration; US3 acceptance scenario AC1 also enumerates all five.
- [x] CHK002 Is the content of the pickle-ban error specified (a `TypeError` whose message explains native handles cannot cross process boundaries)? [Completeness, Spec §FR-013, contracts C-5] — PASS: FR-013 specifies "`TypeError` with a message explaining native handles cannot cross process boundaries"; contracts C-5 gives the exact message template `"fixpp.<ClassName> objects are not pickleable; native handles cannot cross process boundaries"` including the class-name insertion; SC-003 accepts "documented 'not pickleable; native handles cannot cross process boundaries' message."
- [x] CHK003 Are context-manager requirements specified for BOTH `Engine` and `Session` (`__enter__`/`__exit__` invoking `close()`)? [Completeness, Spec §FR-009] — PASS: FR-009 states "Engine and Session MUST be usable as context managers, invoking `close()` deterministically on block exit"; contracts C-1 shows `__enter__`/`__exit__` for both Engine and Session; C-3 closing note "with Engine(config) as engine: calls close() on __exit__ deterministically."
- [x] CHK004 Is the GC-only teardown behavior fully specified (emit `DeprecationWarning`, attempt best-effort native cleanup, and state explicit close is the only guaranteed-correct path)? [Completeness, Spec §FR-010] — PASS: FR-010 specifies all three elements: "MUST emit a `DeprecationWarning` stating explicit close is the supported teardown path, and MUST still attempt best-effort native cleanup. (Finalisation order within a GC cycle is unspecified; explicit close is the only guaranteed-correct path.)"

## Requirement Clarity

- [x] CHK005 Is the pickle-ban scope boundary stated unambiguously — handle-bearing wrappers ONLY, with value-types explicitly NOT introduced — so no SC-003 round-trip contradiction is reintroduced? [Clarity, Spec §FR-014, SC-003] — PASS: FR-014 explicitly states "PY-004 does NOT introduce value-typed Python classes"; SC-003 scopes its 100% guarantee to "handle-bearing wrappers" and notes "Value-typed round-trip is out of scope"; US3 AC2 ("Given a value-typed object that holds no native handle … it serialises and round-trips successfully") includes the note "scoped to value-types that exist in the binding; see Assumptions" — the scope boundary is unambiguous.
- [x] CHK006 Is "best-effort cleanup" defined (what is and is NOT guaranteed) for the `__del__` path, given finalisation order within a GC cycle is unspecified? [Clarity, Ambiguity, Spec §FR-010, contracts C-3] — PASS: FR-010 states "best-effort native cleanup" and parenthetically clarifies "Finalisation order within a GC cycle is unspecified; explicit close is the only guaranteed-correct path"; C-3 closing note states "cross-module `__del__` order at interpreter shutdown is NOT relied upon"; "best-effort" is bounded: attempt native cleanup, no ordering guarantee, may silently succeed or fail depending on GC order.
- [x] CHK007 Is it clear WHICH reduce hooks the pickle-ban must override (`__reduce_ex__` and/or `__reduce__`) to be effective? [Clarity, contracts C-5] — PASS: contracts C-5 explicitly names both hooks: "raise `TypeError(…)` from `__reduce_ex__`/`__reduce__`"; both are named so the implementer covers both Python pickle-dispatch paths (Python's pickle machinery calls `__reduce_ex__` first then falls back to `__reduce__`).

## Requirement Consistency

- [x] CHK008 Is the value-type deferral consistent across Spec §FR-014, §SC-003, the US3 Independent Test, and the Assumptions section — with NO residual claim of a value-type pickle round-trip? [Consistency, Spec §FR-014/SC-003/US3, requirements.md Gate-A-r1 note] — PASS: FR-014 defers value-types; SC-003 scopes to handle-bearing wrappers and notes value-type round-trip is out of scope; US3 AC2 adds "scoped to value-types that exist in the binding; see Assumptions"; Assumptions section says "PY-004 introduces no value-typed Python classes"; no residual round-trip claim found — the Gate A round-1 SC-003 contradiction was correctly closed.
- [x] CHK009 Does the pickle `TypeError` message requirement in contracts C-5 match the FR-013 wording (no drift between spec and contract)? [Consistency, Spec §FR-013 ↔ contracts C-5] — PASS: FR-013 wording "a message explaining native handles cannot cross process boundaries"; C-5 message template "fixpp.<ClassName> objects are not pickleable; native handles cannot cross process boundaries" — C-5 is a concretization of FR-013, not a contradiction; the phrase "native handles cannot cross process boundaries" appears verbatim in both; no drift.
- [x] CHK010 Is the documented close-on-exit ORDER (each session closed, its messages invalidated first, engine handle destroyed last) consistent between US2 AC3, FR-007, and contracts C-3? [Consistency, Spec §US2 ↔ FR-007 ↔ contracts C-3] — PASS: US2 AC3 states "`engine.close()` runs deterministically, closing each session (marking its derived messages dead before the native close) and destroying the engine handle last"; FR-007 states "invalidate derived child wrappers FIRST … THEN call the native close/destroy; the engine destroys its native handle only after all its sessions are closed"; C-3 steps 2-4 execute in exactly that order — no contradiction.

## Acceptance Criteria Quality & Measurability

- [x] CHK011 Is SC-003 ("100% of pickle attempts on handle-bearing wrappers raise `TypeError`") objectively measurable AND scoped to exclude out-of-scope value-types? [Measurability, Spec §SC-003] — PASS: SC-003 states "100% of pickle attempts on handle-bearing wrappers … raise `TypeError`" and adds "any existing non-handle values, if present, are not regressed"; the five-class enumeration makes the test table deterministic; value-type scope exclusion is explicit ("value-typed round-trip is out of scope").
- [x] CHK012 Can the documented teardown ORDER on context-manager exit be objectively verified (an observable ordering assertion), not just asserted as "deterministic"? [Measurability, Spec §US2 Independent Test, FR-009] — PASS: US2 Independent Test specifies: "open an engine + session inside a `with` block and verify `close()` ran on exit in the documented order"; the ordering observable is the sequence of `_dead` flags set (session `_dead` before engine native destroy) — deterministically verifiable by checking the `_was_explicitly_closed` flag and the message-dead state at the time `engine.close()` completes.
- [x] CHK013 Is the DeprecationWarning condition measurable (emitted on GC-only teardown, i.e. when not previously explicitly closed)? [Measurability, Spec §FR-010] — PASS: FR-010 ties the warning to "reclaimed by garbage collection without a prior explicit close"; data-model E-1/E-2 `_was_explicitly_closed` field drives `__del__` — the condition is a boolean flag; the US2 AC4 acceptance scenario makes it observable: "Given an Engine/Session that is never explicitly closed and is reclaimed by GC … Then a DeprecationWarning is emitted."

## Scenario & Edge-Case Coverage

- [x] CHK014 Are requirements defined for a context-manager exit AFTER an explicit `close()` (idempotent no-op, no second teardown, no warning)? [Coverage, Edge Case, Spec §FR-008/FR-009] — PASS: FR-008 states "a second close (including a context-manager exit after an explicit close) is a no-op, never a double-free or error"; C-3 closing note "close() is idempotent (a second call, including a with-exit after an explicit close, is a no-op — guarded by `_dead`/`_was_explicitly_closed`)"; the "no warning on idempotent path" is implied by `_was_explicitly_closed=True` blocking the DeprecationWarning gate.
- [x] CHK015 Is cross-module `__del__` ordering at interpreter shutdown explicitly stated as NOT relied upon (a documented boundary)? [Coverage, contracts C-3, Spec §FR-010] — PASS: FR-010 states "Finalisation order within a GC cycle is unspecified"; C-3 closing note "cross-module `__del__` order at interpreter shutdown is NOT relied upon"; both the lifetime checklist (CHK022 disposition) and the ergonomics checklist document this boundary.
- [x] CHK016 Is the `multiprocessing.Pool(...).map(...)` footgun (the motivating cross-process scenario) covered by the pickle-ban requirement for the objects that would actually be pickled (incl. inbound `Message`)? [Coverage, Spec §US3, quickstart] — PASS: US3 names `multiprocessing.Pool(...).map(...)` as the motivating scenario; FR-013 enumerates `Message` in the pickle-ban list; SC-003 accepts 100% of pickle attempts on handle-bearing wrappers; the inbound `Message` (the most likely accidentally-pickled object) is explicitly covered.

## Ambiguities & Gaps

- [x] CHK017 Does the spec define whether `__del__`'s best-effort teardown emits the `DeprecationWarning` even when the handle is ALREADY dead/closed, to avoid a spurious warning on the idempotent path? [Gap, Spec §FR-010, FR-008] — WAIVED: the `_was_explicitly_closed` flag (data-model E-1/E-2) is the intended gate for the DeprecationWarning — `__del__` emits the warning only when `_was_explicitly_closed` is False. FR-010 reads "without a prior explicit close" which implies `_was_explicitly_closed=False` as the precondition. The disambiguation is present as a structural implication of the data model and is sufficient for implementation. Specifying it verbatim in FR-010 would add no new information. Item is tagged [Gap] only, not Completeness/Clarity/Consistency; WAIVED.
- [x] CHK018 Is the future-direction note ("v1.x makes GC-only teardown an error") scoped clearly as non-normative for v1.0, so it is not mistaken for a current requirement? [Ambiguity, quickstart, Spec §FR-010] — PASS: FR-010 states the future-direction note parenthetically as an aspirational comment following the v1.0 requirement; the quickstart marks it as a non-normative note; FR-010's normative text says "MUST emit a `DeprecationWarning`…", not "MUST raise an error" — the current behavior is unambiguously DeprecationWarning + best-effort, not an error.

## Notes

- This checklist tests REQUIREMENTS quality, not pickle/teardown behavior at runtime.
- CHK005/CHK008 are the highest-value items: the SC-003/US3 value-type round-trip contradiction was already caught and fixed at Gate A round 1 (see requirements.md notes); these items guard against its reintroduction.
- Traceability: 18/18 items carry a spec/contracts/quickstart reference or a `[Gap]`/`[Ambiguity]` marker.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 17 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 1 |
| **Total** | **18** |

### SPEC-FIXED items
*(none)*

### DD-DECIDED items
*(none)*

### WAIVED items
- CHK017 — rationale: the `_was_explicitly_closed` flag structurally implies the no-spurious-warning behaviour; item tagged [Gap] only, not Completeness/Clarity/Consistency.

Anchors spot-verified: `[2m §6.2]` (line 1035), `[2m §9 seam #3]` (line 1428 context, seam #3 within §9) — all cited anchors in scope for this checklist resolve in signed-off revision `.specify/2m-pybind.md v0.3`.
