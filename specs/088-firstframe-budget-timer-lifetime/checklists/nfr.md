# NFR Checklist: 088-firstframe-budget-timer-lifetime

**Purpose**: Unit-test the *requirements* governing cancellation, object lifetime, allocation and
determinism — the non-functional dimensions this feature actually moves.
**Created**: 2026-08-05
**Feature**: [spec.md](../spec.md) · FR-004/005/006/013/014/015/017/018, SC-005/006/009/013/014/015/016/018
**Audience**: Gate B reviewer

## Cancellation Requirements — Completeness

- [x] CHK001 - Is the cancellation *type* that `Engine::stop()` emits named in the requirements, rather than left as "cancellation"? [Clarity, Spec §FR-015] — PASS: FR-015 names `cancellation_type::total` explicitly (spec.md:452-464).
- [x] CHK002 - Are requirements stated for what each arm of the join must do on receiving that type, individually rather than collectively? [Completeness, Spec §FR-005, §FR-018] — DD-DECIDED §research.md D-2 (plan.md Constitution Check XI§2 row): the deadline arm's individual reset-to-`enable_total_cancellation()` mechanism is a settled, Gate-A-reviewed design decision; FR-005/FR-006/FR-017/FR-018 state the resulting spec-level obligations, and the per-arm mechanism is properly a research-layer decision, not duplicated at spec level.
- [x] CHK003 - Is the obligation on the TLS transport's read path stated as a requirement on the *transport*, given the fix is not expressible at the call site? [Completeness, Spec §FR-018] — PASS: FR-018 states this explicitly — "discharged in the transport, not at the call site... an engine-side arm wrapper is clobbered... an engine-local remedy is structurally impossible" (spec.md:475-479).
- [x] CHK004 - Are requirements defined for the plain transport's read path, including an explicit statement that it is *out* of scope and why? [Coverage, Spec §FR-018] — PASS: FR-018's final paragraph states the plain transport is explicitly out of scope and why (native `total` support) — spec.md:489-493.
- [x] CHK005 - Are the handshake and write paths' cancellation behaviour addressed in requirements, or explicitly deferred with a recorded reason? [Gap] — PASS: spec.md's census addendum ("The same latent property exists elsewhere...") explicitly defers both with a recorded reason (spec.md:160-169).
- [x] CHK006 - Is a requirement stated for the *bound* on `stop()` — prompt, deadline-bounded, or unbounded — rather than only that it "aborts"? [Measurability, Spec §FR-015] — PASS: FR-015 plus SC-015 clause (e) bind explicit numeric promptness thresholds (100 ms / 500 ms), not only "aborts" (spec.md:452-464, SC-015(e)).

## Lifetime & Memory-Safety Requirements

- [x] CHK007 - Is "no handler executes against state owned by a returned frame" expressed so it can be evaluated without appeal to timing? [Measurability, Spec §FR-005] — PASS: FR-005/SC-006 evaluate structurally (drain-to-completion, zero `cancel()` calls observed) — not by appeal to timing or a race.
- [x] CHK008 - Are the requirements for handler *survival* (none outstanding) distinguished from requirements for handler *effect* (no `cancel()` issued)? [Clarity, Spec §FR-006, §SC-006] — PASS: FR-005 (survival) and FR-006 (effect on a live `Session`) are stated as two distinct FRs; SC-006/contract S5 and data-model.md's INV-L1/INV-L2 keep the distinction explicit throughout.
- [x] CHK009 - Is the requirement that the epoch live in shared state the handler owns by value stated with its rationale (a plain member would be read through a dangling `this`)? [Completeness, Plan §D-4.1] — PASS: D-4.1 states this rationale in full; FR-014 also states "the handler MUST decide staleness from state it owns by value, before touching any member" (spec.md:438-439).
- [x] CHK010 - Are per-timer counter requirements justified against a caller-sequencing property the transport cannot enforce, rather than against today's callers only? [Clarity, Plan §D-4.1] — PASS: D-4.1 states this explicitly — "that correctness rests on a sequencing property of two callers that nothing in the transport enforces... a future interleaving [would] silently reintroduce this exact defect class."

## Determinism Requirements

- [x] CHK011 - Is "deterministic" defined for the witness criteria — does it mean no timing margin, no scheduler-ordering dependence, or both? [Clarity, Spec §SC-016] — PASS: SC-016 defines it as ordering-construction — "the ordering under test is constructed by the test, not awaited... does not depend on winning a timing race." (Its tension with SC-015's own normative wall-clock thresholds is a separate, tracked item — see CHK012.)
- [x] CHK012 - Do the SC-016 determinism requirement and SC-015's normative wall-clock thresholds conflict, and is the conflict resolved in the requirements rather than in the quickstart? [Conflict, Spec §SC-015, §SC-016] — DD-DECIDED — carried Gate A round-4 obligation (plan.md "Carried out of round 4, as `/tasks` obligations" item 1; bound to `tasks.md` T045): the conflict is real, known and explicitly deferred — T045 requires replacing T2a/T2b's wall-clock bounds with a test-owned ordering construction **and** reconciling/narrowing SC-016 in the same edit, before Gate B. This is a deliberate, tracked deferral, not an accidental gap.
- [x] CHK013 - Are requirements stated distinguishing orderings the design *may* depend on (timer-vs-timer, expiry-ordered) from those it may not (socket-vs-timer, unspecified)? [Clarity, Spec §SC-014] — PASS: SC-014/SC-016/clarification G-1 explicitly distinguish timer-vs-timer (relied on, session layer) from socket-vs-timer (disclaimed as unspecified, transport layer).
- [x] CHK014 - Is the arm-once requirement for the deadline timer stated as a requirement, given a per-iteration re-arm silently removes the deadline with every existing test green? [Completeness, Spec §FR-017] — PASS: FR-017 states this near-verbatim, including the "every existing test still green" silent-failure mode (spec.md:407-412).

## Allocation & Performance Requirements

- [x] CHK015 - Is the allocation delta quantified as a *count* rather than characterised qualitatively? [Measurability, Plan §D-7] — PASS: D-7's allocation table quantifies the delta as an explicit count (~3-4 global-heap coroutine frames per join, itemised 1-7), not qualitatively.
- [x] CHK016 - Is the Article XI §6 deviation recorded as an explicit, reviewable waiver rather than an omission? [Completeness, Plan §Complexity Tracking] — PASS: plan.md's Complexity Tracking table records the deviation with a Violation/Why-Needed/Simpler-Alternative-Rejected-Because structure and an explicit "Disposition sought: waiver, not a claim of compliance."
- [x] CHK017 - Is the Article VIII §3 disposition's *ground* stated, and does it carry a stated void-condition under which a benchmark becomes owed? [Clarity, Plan §D-7] — PASS: plan.md's VIII §3 row / D-7 states an explicit void-condition ("void if the delivered filter captures state or moves off the per-call entry point").
- [x] CHK018 - Are requirements defined for the per-iteration cost of the join, given it recurs per loop iteration rather than once per connection? [Coverage, Plan §D-7] — PASS: D-7 states the per-iteration recurrence explicitly and ties its bound to FR-013's iteration cap ("bounded by the same constant FR-013 bounds the bytes with").

## Bounds & Edge Cases

- [x] CHK019 - Is the byte bound stated as an exact value with its derivation, rather than as "bounded"? [Measurability, Spec §FR-013] — PASS: FR-013 states the exact value (`max_bytes + 1`) with its derivation (the C1 clamp).
- [x] CHK020 - Are requirements defined for the boundary case where cumulative size equals the budget with *no* complete frame present, distinct from the case where one is present? [Edge Case, Spec §Edge Cases] — PASS: spec.md's Edge Cases list both boundary cases as distinct bullets ("Cumulative size exactly `max_bytes`, complete frame present" / "...no complete frame present").
- [x] CHK021 - Is a requirement stated for the pre-session DoS surface, given the join recurs per iteration and the iteration count is bounded only by the byte budget? [Coverage, Gap] — PASS: FR-013's byte bound plus D-7's explicit "that is a pre-session DoS surface worth stating explicitly, and it is bounded by the same constant FR-013 bounds the bytes with" connect the DoS surface to a stated, numeric requirement.
- [x] CHK022 - Are sanitizer requirements scoped to a named matrix (ASan/UBSan/TSan) with a stated pass condition? [Measurability, Spec §SC-009] — PASS: SC-009 names the matrix (ASan/UBSan/TSan) and the pass condition (0 findings) explicitly.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 20 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 2 |
| WAIVED | 0 |
| **Total** | 22 |

### SPEC-FIXED items
None.

### DD-DECIDED items
- CHK002 — anchor: research.md D-2, plan.md Constitution Check XI§2 row; rationale: the deadline arm's per-arm cancellation-reset mechanism is a Gate-A-reviewed design decision, not duplicated as a spec-level FR (FR-005/006/017/018 carry the resulting obligations).
- CHK012 — anchor: plan.md "Carried out of round 4" item 1; bound to `tasks.md` T045; rationale: the SC-015/SC-016 wall-clock-vs-determinism tension is real, known, and deliberately deferred to a mandatory pre-Gate-B task, not an accidental gap.

### WAIVED items
None.

Anchors spot-verified: `spec.md` FR-005/FR-006/FR-013/FR-014/FR-015/FR-017/FR-018, SC-009/SC-014/SC-015/SC-016 (all resolve as cited) · `plan.md` Constitution Check XI§2 and VIII§3 rows, Complexity Tracking table (all resolve) · `research.md` D-2, D-4.1, D-7 (all resolve) · `tasks.md` T045 (resolves, matches cited obligation).
