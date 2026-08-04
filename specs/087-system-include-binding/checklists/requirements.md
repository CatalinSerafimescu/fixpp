# Specification Quality Checklist: System include directories bound at the installed-package consumer

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-08-04
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs)
- [x] Focused on user value and business needs
- [x] Written for non-technical stakeholders
- [x] All mandatory sections completed *(see Notes — this tick was **false** at Gate A round 1, `## Normative
      References` was missing; closed there)*

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain
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

- **All 3 `[NEEDS CLARIFICATION]` markers resolved by `/speckit-clarify`, 2026-08-04** (mandatory for this
  feature under `[const §XVI.3]`, ABI-adjacent — not skipped). Each answer changed the delivered work, not
  just the wording:
  1. **Assertion form → exact set equality** (new **FR-003a**). Containment was rejected because it is
     structurally blind to a *dropped* root, which is half of what C-3 claims; a deny-list only catches leaks
     it was told about. This also settled the toolchain-root question the marker originally posed: the
     expectation is a **closed set**, so compiler-owned roots — *if* the mechanism reports them at all — are
     enumerated under a stated rule rather than tolerated. Whether they appear is a measurement now **owned by
     `/speckit-plan`**; it changes the expectation's contents, not the assertion's shape.
  2. **Both legs in scope** (new **FR-001a**, **FR-007a**). `fixpp::service` gets its own probe, expectation
     and red demonstration. FR-007a carries the directional hazard 086 measured: `fixpp_service` links
     `fixpp_capi`, so reverting the capi leg reds *both* probes and proves nothing about the service leg —
     the service red must revert the service line alone, with same-run evidence that capi stayed isolated.
  3. **MSVC disposition decided up front** (new **FR-010a**): scope out with a recorded reason, and on the
     excluded platform fail loudly or be visibly absent — never silently pass. Deciding before implementing is
     the direct lesson of 086, where a mechanism measured only on Linux/clang cleared six Gate B rounds and
     then failed on `windows-msvc-debug`.

- **SC-003 grew from three red causes to four** as a consequence of answer 1: exact equality makes a
  *removed* entry detectable, so it must be demonstrated red too. This is the kind of downstream change that
  justifies clarify running before `/speckit-plan`.

- **On "no implementation details":** the spec names `$<LINK_ONLY:>`, `fixpp::capi` and the four withheld
  properties. These are retained deliberately — they are the *existing shipped contract* this feature binds
  (086 C-3), not a proposed implementation. The chosen observing mechanism (CMake File API compile groups,
  per the tracking issue) is **not** prescribed here; it belongs to `/speckit-plan` + `research.md`, and
  FR-001/FR-002 are stated as outcomes so an alternative mechanism could satisfy them.

- **Anti-vacuity is a first-class requirement here**, not a review concern: US2 and FR-005 exist because the
  naive form of this gate — a fourth empty-vs-empty comparison — is precisely why 086 declined to write it.

- **"All mandatory sections completed" was FALSE at Gate A round 1, and is now true.** `spec.md` had no
  `## Normative References` section, which `[const §VI.5]` (`.specify/constitution.md:164`) makes a
  **presence** obligation on every `/specify` artifact — so the tick above was unearned. The section was added
  at Gate A round 1, discharged the way 086 discharged it
  (`specs/086-capi-include-isolation/spec.md:655-662`): the FIX-normative set is empty — verified,
  `grep -c "086\|087" spec/feature-catalogue.md` → **0**, so `[const §VI.4]`'s coverage-index obligation is
  not engaged — and the constitutional and architectural authorities that do govern are named instead. The
  tick is retained because the obligation is now met; it is recorded here rather than silently corrected,
  exactly as 085 did when it acquired the same section at this same gate
  (`specs/085-fold-flat-cap-loop/spec.md:223-225`).

- **Two requirements were added at Gate A round 1**, so this checklist's scope grew: **FR-014** (assert the
  `consumer` label's registration count in CI — the last vacuity path, which no demonstration inside the gate
  can reach) and **SC-008** (its outcome). Both are testable and measurable as the boxes above require;
  neither adds a registered test, so FR-013 is unaffected.

- **No requirement was added at Gate A round 2, but two had their scope made explicit** — recorded here
  because "Scope is clearly bounded" and "Requirements are testable and unambiguous" are ticked above, and in
  both cases the round-1 wording was read narrower than it said:
  - **FR-014 / SC-008** were unqualified by tier, yet `plan.md` prescribed an edit to `tier1.yml` only. The
    witness runs on every lane of all three tiers and `FIXPP_BUILD_CODEGEN_TOOL` is overridden nowhere, so
    the requirement now names `tier1.yml`, `tier2.yml` and `tier3-libcxx.yml` outright (contract §6a).
  - **FR-011**'s amendment set grew from five artifacts to **seven**. The two additions are hits of §4a's own
    exhaustiveness grep that the table had omitted — both in files 087 already edits. The requirement itself
    is unchanged: it delegates the list to §4a, which is why extending §4a was sufficient.

  The ticks are retained: neither correction changed what the requirements assert, only where the bundle said
  they reach.
