# Specification Quality Checklist: System include directories bound at the installed-package consumer

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-08-04
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs)
- [x] Focused on user value and business needs
- [x] Written for non-technical stakeholders
- [x] All mandatory sections completed

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
