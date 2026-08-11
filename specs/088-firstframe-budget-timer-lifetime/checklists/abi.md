# ABI & Public-Surface Checklist: 088-firstframe-budget-timer-lifetime

**Purpose**: Unit-test the *requirements* governing this feature's public surface, install set and
ABI stability — are they written completely, unambiguously and measurably?
**Created**: 2026-08-05
**Feature**: [spec.md](../spec.md) · FR-012, FR-016, SC-010, SC-017
**Audience**: Gate B reviewer

## Requirement Completeness

- [x] CHK001 - Is the *scope* of "public surface" enumerated (headers, error codes, C ABI, wire format) rather than left to the reader? [Completeness, Spec §FR-012] — PASS: FR-012 enumerates "the public C++ API, the C ABI, the wire format, the session FSM, or the error-code enumeration" explicitly (spec.md:532-534) — a superset of the categories this item names.
- [x] CHK002 - Are requirements stated for the **install set** specifically, distinct from the compiled surface? [Completeness, Spec §SC-017] — PASS: SC-017 states the install-set claim ("the installed package's headers are byte-identical to `main`'s") as a criterion distinct from SC-010's compiled-surface-delta claim (spec.md:708-709).
- [x] CHK003 - Is the placement rule for the new internal header — under `src/` rather than `include/` — stated as a *requirement* with its rationale, or only as an implementation note? [Completeness, Spec §FR-016] — PASS: FR-016 states the placement as a MUST with rationale (the `scan_first_frame_ids.hpp` precedent, "outside the install set by construction") — spec.md:494-500.
- [x] CHK004 - Are requirements defined for the test-only mock header's exclusion from the install set, given it is modified by this feature? [Coverage, Spec §SC-010] — DD-DECIDED §plan.md Constitution Check, Article VII row: the mock header's install exclusion is settled project-wide practice (`CMakeLists.txt:446-451`, `PATTERN "fixpp/transport/test" EXCLUDE`), independently re-verified against this feature's four additions at Gate A round 3. spec.md itself states no requirement for this — the disposition lives in the reviewed design layer, not the spec.
- [x] CHK005 - Is a requirement stated for what happens if a future change *does* need to touch the installed surface (escalation path)? [Gap] — WAIVED: tagged `[Gap]`, not Completeness/Clarity/Consistency. A general escalation policy for future installed-surface changes is out of scope for a narrowly-scoped defect-correction feature; no such policy exists in any sibling feature's spec either.

## Requirement Clarity & Measurability

- [x] CHK006 - Is "the public surface delta is empty" expressed as an objectively checkable predicate (e.g. byte-identity against `main`) rather than a qualitative claim? [Measurability, Spec §SC-010] — PASS: SC-017 supplies the objective byte-identity predicate for headers, and FR-012's "no new or removed error code" is directly diffable against the enum — together these back SC-010's qualitative wording with a checkable test (spec.md:708-709, 532-534).
- [x] CHK007 - Is "not installed" defined by a *mechanism* (the `install()` exclusion pattern) rather than by assertion? [Clarity, Spec §SC-017] — PASS: SC-017/FR-016 ground "not installed" in the src/-is-not-an-install-root mechanism, not bare assertion.
- [x] CHK008 - Can the SC-010 and SC-017 criteria each be evaluated by a reviewer without running the build? [Measurability, Spec §SC-010, §SC-017] — PASS: verifiable by source inspection alone — confirm no `include/` file is touched except the excluded mock header (`CMakeLists.txt:446-451` pattern), per plan.md Scale/Scope; no build required.

## Requirement Consistency

- [x] CHK009 - Do the spec, plan and contract agree on where the concrete transport headers live, after the Gate A round-1 correction that they are under `src/transport/` and not `include/fixpp/transport/`? [Consistency, Plan §Project Structure] — PASS: plan.md Project Structure (:206-220), data-model.md §4, and the contract's "Internal surface delta" section all agree the concrete transport headers live under `src/transport/`; spec.md makes no conflicting header-location claim.
- [x] CHK010 - Are the FR-016 internal-header requirement and the SC-017 install-set requirement free of circular justification — does either rest on the other rather than on the install rule? [Consistency, Spec §FR-016, §SC-017] — PASS: both FR-016 and SC-017 independently ground on the same underlying fact (src/ is not an installed include root); neither cites the other as its justification.
- [x] CHK011 - Is the mock-header modification consistent with FR-012's "no public API change", given the header ships in the repository but not in the install set? [Consistency, Spec §FR-012] — DD-DECIDED §plan.md Constitution Check, Article VII row (corrected at Gate A round 3): the reconciliation — `FIXPP_ALLOW_MOCK_TRANSPORT` gating plus the install-pattern exclusion — is stated there, not in FR-012's own text, which does not mention `mock_transport.hpp` at all.

## Edge Cases & Assumptions

- [x] CHK012 - Is the assumption that `src/` is not an installed include root stated and traceable to the install rule that makes it true? [Assumption, Spec §SC-017] — PASS: traceable via the working precedent cited at FR-016/plan.md Structure Decision — `scan_first_frame_ids.hpp` already lives under `src/session/` and is not installed, which is the positive evidence for the absence of a `src/` install rule.
- [x] CHK013 - Are requirements defined for the case where the new header is transitively included by an installed header? [Edge Case, Gap] — WAIVED: tagged `[Gap]`. The new header is included only by `engine.cpp` and test targets (D-5 / plan.md Project Structure), never by another header — verified by the includer census — so transitive inclusion into the installed set cannot occur; no requirement is owed for an unreachable scenario.
- [x] CHK014 - Is the ABI impact of adding a `std::shared_ptr` member to the two transport classes addressed in requirements, given those classes are internal but linked into the shipped library? [Coverage, Gap] — WAIVED: tagged `[Gap]`. `asio_plain_transport`/`asio_tls_transport` are internal types declared only under `src/transport/`, never named in an installed header or the C ABI surface (FR-012), so no external consumer can observe or depend on their layout — a member addition cannot break an ABI boundary the type was never part of.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 9 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 2 |
| WAIVED | 3 |
| **Total** | 14 |

### SPEC-FIXED items
None.

### DD-DECIDED items
- CHK004 — anchor: plan.md Constitution Check, Article VII row (`CMakeLists.txt:446-451` EXCLUDE pattern, re-verified Gate A round 3); rationale: mock header's install exclusion is settled design-layer practice, not stated in spec.md.
- CHK011 — anchor: plan.md Constitution Check, Article VII row; rationale: FR-012/mock-header reconciliation lives in the design layer via the `FIXPP_ALLOW_MOCK_TRANSPORT` gate + install pattern.

### WAIVED items
- CHK005 — rationale: tagged `[Gap]`; no escalation-path requirement is owed by a narrow defect-correction feature. Not Completeness/Clarity/Consistency.
- CHK013 — rationale: tagged `[Gap]`; the new header has no installed includer, verified by census. Not Completeness/Clarity/Consistency.
- CHK014 — rationale: tagged `[Gap]`; internal types never exposed across an ABI boundary. Not Completeness/Clarity/Consistency.

Anchors spot-verified: `CMakeLists.txt:446-451` (install EXCLUDE pattern, resolves as cited) · `plan.md` Constitution Check Article VII row (resolves, Gate A round 3 text confirmed) · `spec.md` FR-012/FR-016/SC-010/SC-017 (all resolve as cited).
