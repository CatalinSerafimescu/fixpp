# Specification Quality Checklist: C-ABI include isolation, delivered by the installed package

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-08-03
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs) — *requirements are stated as reachability and
      observable build outcomes, not as CMake property assignments. The measured-facts table cites files
      because the spec's job here is to record what was measured before scoping; those citations are evidence,
      not prescriptions.*
- [x] Focused on user value and business needs — *the value is the AGPL/commercial legal-isolation boundary
      (Article IV §2) actually holding for a C-ABI integrator.*
- [x] Written for non-technical stakeholders — *within the limits of a build/packaging feature; every user
      story is stated as what an integrator can and cannot do.*
- [x] All mandatory sections completed

## Requirement Completeness

- [x] **No [NEEDS CLARIFICATION] markers remain** — **closed at `/speckit-clarify`, 2026-08-03.** Three
      questions asked and answered: installed layout (additive, both roots), `fixpp::service` scope (in scope),
      isolation strictness (by-name targets only). FR-011/FR-012 were rewritten from open questions into
      requirements; FR-003a and FR-005a were added to carry the strictness and additivity answers.
- [x] Requirements are testable and unambiguous — *FR-003 is stated as transitive reachability precisely
      because the #218 defect was a direct property reading clean while the transitive one was open.*
- [x] Success criteria are measurable — *SC-001 counts headers **and names the probes plus the C-5 root
      containment that evidence its negative leg** (scoped at Gate A r2, so the "0" is a claim the named
      evidence actually supports); SC-002 requires a red observation; SC-006 requires a re-measurement;
      SC-007 asserts captured `ctest` **exit codes** and per-test statuses rather than a test-name diff, which
      is structurally incapable of observing a failure (Gate A r2).*
- [x] Success criteria are technology-agnostic (no implementation details) — *stated as reachability counts and
      observed pass/fail, not as target properties.*
- [x] All acceptance scenarios are defined — *6 user stories, **17** scenarios.*
- [x] Edge cases are identified — *7, including the `usr/`-prefix asymmetry, the `find_package`-time existence
      check that makes a naive export-set change break every consumer, and the same-header-at-two-paths
      consequence for any exact-set content assertion.*
- [x] Scope is clearly bounded — *closed at clarify: three roots, by-name targets only, strictly additive.*
- [x] Dependencies and assumptions identified — ***9** assumptions, 3 dependencies.*
- [x] **Normative References present** — *added at Gate A r1. `[const §VI.5]` (`.specify/constitution.md:164`)
      is a **presence** obligation; this feature has no FIX-normative content, so the section records that
      explicitly and cites the governing constitutional/architecture authorities instead.*

> **Inventory totals — counted 2026-08-03 (Gate A round 1), by command, against `spec.md`:**
>
> ```bash
> grep -o '\*\*FR-[0-9a-z]*\*\*' spec.md | sort -u | wc -l                       # 30
> grep -o '\*\*SC-[0-9a-z]*\*\*' spec.md | sort -u | wc -l                       # 10
> awk '/^\*\*Acceptance Scenarios\*\*/,/^---/' spec.md | grep -c '^[0-9]\+\. \*\*Given\*\*'   # 17
> awk '/^## Assumptions/,/^## Normative References/' spec.md | grep -c '^- \*\*'  # 9
> awk '/^### Edge Cases/,/^## Requirements/' spec.md | grep -c '^- \*\*'          # 7
> ```
>
> **30 FR · 10 SC · 17 scenarios · 9 assumptions · 7 edge cases** — **re-derived by the block above at Gate A
> round 2, 2026-08-03: unchanged.** FR-009a gained two lettered legs (i)/(ii) rather than a new FR id, and
> R2-5's missing normative cell landed in `contracts/include-interface.md` §4 rather than as a new SC, so no
> total moved. The pre-Gate-A figures (15 scenarios, 10
> assumptions here; "36 FR, 8 SC" in `plan.md`) matched no measurement. **Re-run the block above before
> trusting these numbers** — a hand-maintained total goes stale silently, which is the defect that put this
> box here.

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria
- [x] User scenarios cover primary flows
- [x] Feature meets measurable outcomes defined in Success Criteria
- [x] No implementation details leak into specification

## Notes

- **The issue's stated remedy was rejected during spec authoring**, with evidence: §7.4:503's
  `INTERFACE_INCLUDE_DIRECTORIES = include/fix/` cannot be satisfied without breaking `<fix/c_api.h>`, because
  every C-ABI header is included through the `fix/` component. The spec records this rather than inheriting the
  issue's framing.
- **FR-007 (demonstrated-red) is load-bearing** and is written to the project's standing rule that a gate never
  observed failing proves nothing. FR-008 guards the adjacent trap — a compile-fails assertion that would pass
  for the wrong reason is a proxy, not a witness.
- **Clarify session 2026-08-03 resolved all blockers; 17/17 items pass** *(16/16 at clarify; the Normative
  References item was added at Gate A round 1 and passes — `grep -c '^- \[x\]' checklists/requirements.md`).*
  One answer went against the
  direction preview the user had originally selected (the preview showed `PATTERN fix EXCLUDE`; the clarified
  layout is strictly additive) — recorded in Clarifications so the plan builds against the answer, not the
  preview.
- **`fixpp::service` was pulled into scope on new evidence** gathered after the direction was chosen. Ready for
  `/speckit-plan`, then `/gate-a` before `/speckit-tasks`.
