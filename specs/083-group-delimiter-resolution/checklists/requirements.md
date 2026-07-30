# Specification Quality Checklist: Group Delimiter Resolution

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-07-30
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs)
- [x] Focused on user value and business needs
- [x] Written for non-technical stakeholders
- [x] All mandatory sections completed

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain — all 3 resolved in the 2026-07-30 clarification session
- [x] Requirements are testable and unambiguous
- [x] Success criteria are measurable
- [x] Success criteria are technology-agnostic (no implementation details)
- [x] All acceptance scenarios are defined
- [x] Edge cases are identified
- [x] Scope is clearly bounded
- [x] Dependencies and assumptions identified

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria
- [x] User scenarios cover primary flows
- [x] Feature meets measurable outcomes defined in Success Criteria
- [x] No implementation details leak into specification

## Notes

### RESOLVED 2026-07-30 — all three markers settled in `/speckit-clarify` (4 questions asked)

| Was | Decision | Effect on scope |
|---|---|---|
| FR-018 | **Thread context into the construction path.** The construction-side check already recurses through nested instances and already has the message type, so it carries the ancestor path through that walk. | Neutral. No exported ABI signature changes (FR-018a). |
| FR-020 | **Declaration order wins unconditionally.** Interop gate is observational — it records divergence, never arbitrates it. No compatibility mode, no config surface, no hot-path branch (FR-020a). | **Narrows.** Removes a possible lenient-mode branch and keeps FR-012's pin carve-out-free. Also unblocks the feature despite `reference-engines/` being absent. |
| FR-021 | **Fully in scope** — the typed-read instance splitter is investigated and fixed here. | **Widens.** Added User Story 5 (P3), FR-021/021a/021b, SC-010. |
| *(4th question, not a pre-existing marker)* FR-006 | **Reject the load by default, plus an explicit opt-in tolerant mode.** | **Widens slightly.** Added FR-006a/006b and SC-011: a new loader option surface, both dispositions tested, and a precondition that all ten shipped dictionaries still load under the default. |

Net: 21 → 28 functional requirements, 9 → 12 success criteria, 4 → 5 user stories.

### Original rationale for deferring the three markers to `/speckit-clarify` (retained for the record)

`/speckit-clarify` is the **mandatory next pipeline step** for this feature and must not be skipped
(project rule: always invoke it, never skip on "spec complete"). Resolving these three here would
make that step a no-op and would ask the user the same three questions twice. They are all genuine
scope decisions with no reasonable default, which is exactly the category `/speckit-clarify` exists
to settle:

| Marker | Question | Why no default |
|---|---|---|
| FR-018 | How is the construction-path delimiter reconciled with the validation-path delimiter? | The construction commit point may not know the message type and ancestor path at all. Threading context in, narrowing the check, and deferring are all defensible and have different blast radii on a GA-frozen ABI. |
| FR-020 | If an external reference engine's delimiter disagrees with declaration order, which wins? | Every member of the affected groups is schema-optional, so *any* choice rejects some schema-legal shape. This is an interop-policy call that cannot be settled from inside the repository. |
| FR-021 | Is the typed-read instance splitter in scope? | It is adjacent, reachable today, and was explicitly not investigated during triage. Pulling it in or leaving it out both change the feature's size materially. |

### Measurement-quality notes carried into the spec

- Baseline figures are measured on `main` @ `0539b56d`, not inherited from the issue text. Three of
  the issue's figures were corrected: the polluted-context count (42 → 52 across ten dictionaries,
  after removing 10 context-miss artefacts and adding FIX42 and Orchestra, which the issue omitted),
  the framing of pollution as the primary symptom (it is secondary to 335 wrong delimiters), and the
  scope of the defect as cross-loader rather than XML-loader-only.
- One premise is explicitly recorded as **inference, not measurement** (Assumptions): that a wrong
  delimiter causes mis-parsing. It is a reading of the receiver's logic; no wire reproduction exists
  yet. The spec requires the first test to close this before any fix is made.
- The root-cause split between the two causes is recorded as **corroborated but not proven**.

### Corrections applied during validation (iteration 2)

- **Fabricated message names removed.** The first draft glossed measured msgType codes with message
  names that were not derivable from the probe output (it attributed `BidResponse(l)` to
  `NoOrders(73)`, where `l` was measured under `NoBidComponents(420)`). All message names in the
  spec are now resolved from the dictionary's own `<messages>` block and every one traces to a line
  of probe output.
- **SC-001's denominator corrected.** The probe skipped the 30 unregistered contexts *before* the
  delimiter check, so 335 is measured over a population that excludes them. Once FR-006 registers
  their parents, the post-fix population is 365. The spec now labels this the one projected rather
  than measured figure, and requires it to be measured before the fix.
- **New coupling surfaced.** Those three groups' declared delimiters (453, 1529, 1920) are
  themselves nested-group count tags, so they are *additional* Story-2 cases — 30 on top of the 232.
  Story 2 is now sized against both.
- **SC-004 descoped from an impossible enumeration.** Was "all 232 affected contexts" as wire
  witnesses. Now: the delimiter pin covers all of them by construction, and wire acceptance is
  witnessed on a named per-count-tag subset. A sibling feature already hit the underlying constraint
  — some contexts are unconstructable because every member is schema-optional.
- **FR-022 added** requiring the benchmark in the same change. SC-009 stated the budget but nothing
  required the bench, and this is a hot-path change (delimiter resolution is queried per received
  field on the inbound validation path).
- **Dependency tightened**: the census oracle extension must be additive, because the existing
  member sets are consumed by pins on a parked branch that cannot currently be built.

### Deliberate anti-pattern guards written into the requirements

- FR-016 forbids citing the existing 78 collision-membership cases as delimiter coverage — their
  discriminator is derived independently of the delimiter, so their green is a proxy gap.
- FR-013 forbids a circular pin (expected values must not mirror the implementation).
- FR-014 requires the pin to be *observed* failing; a gate never proven red proves nothing.
- FR-015 forbids emitting member-set exactness as a second half-pin.
- FR-005 forbids a half-restructure across the two loaders.
- FR-017 requires the registered-count delta to be justified by construction, not by a bare number.
