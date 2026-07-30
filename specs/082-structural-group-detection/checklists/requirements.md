# Specification Quality Checklist: Structural Repeating-Group Detection for Legacy FIX Dictionaries

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-07-29
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

- **RESOLVED at `/speckit-clarify` (2026-07-29).** The one retained marker — the FIX40/41/42
  compat posture — is settled: **ungated**, one detection path, new strictness riding the
  existing `validate_inbound_messages` opt-in, recorded as a named behavior change with an
  operator-facing release note. FR-006 was replaced by FR-006/006a/006b/006c, which also
  capture the discovery that the **parse/field-addressing** half of the change is
  unconditional (`session.cpp:992` builds `inbound_tv_` regardless of the strict flag), so
  it is pinned separately with strict validation **off** (SC-008/SC-008a).
- US4 (the `v42` grouped/nested write exemplar closing L-061-1) was confirmed **in scope at
  P3** rather than split to a follow-up.
- Two candidate questions were resolved from source instead of being asked. **One of the two source
  reads was WRONG and was corrected at Gate A round 1 (2026-07-30):**
  - **`--families` defaults to `all`** — *correct*: `tools/codegen/fixpp-codegen/main.cpp:69` sets
    `CoverageMode::All`, so the `v42` builder tier covers all 39 application messages. Unchanged.
  - **"the zero-member `<group>` concern is moot (both structural sources are already
    member-independent)" — FALSE at the runtime source, and this bullet must not certify it.**
    `xml_loader.cpp:608-649` initialises `first_field_tag = 0` and records the `GroupDef`
    unconditionally, so a member-less `<group>` stores `first_field_tag == 0`; and
    `group_first_field_impl` (`dictionary.cpp:92-99`) returns 0 both for "not a group" and for
    "declared group whose first field is 0" — an ambiguous sentinel that collapses a zero-member
    group into "not a group". The codegen source *is* member-independent
    (`ir.cpp:80-100` appends unconditionally); the runtime source is not. The candidate question was
    therefore closed on a false read, and the audit trail is corrected here rather than left
    asserting it. **Resolution, in two stages.** *(i) Clarifications, Session 2026-07-30 (Gate A
    round 1):* the predicate owes *not derived from a message's own field-run membership*, and the
    zero-member case was recorded as an explicit **non-property with its representational reason**
    (contract P1-NON, research D-1a). *(ii) User decision, 2026-07-30 (OD-1, post-convergence):* the
    non-property half is **superseded** — a member-less `<group>` is now a **load error** in both
    loaders (**FR-023**, **SC-013**), so the state is unreachable rather than tolerated. The "not
    field-run-derived" absolute is unaffected and still binds. Empirically no vendored dictionary
    declares a member-less `<group>` — `predicate_census.py` emits no such warning for any of the ten
    — so the rejection affects **0** shipped dictionaries; that measurement is now FR-023's
    no-regression leg rather than the ground on which a limitation was accepted.
- **No open items — both were resolved by the user 2026-07-30 — plus one Gate A clarification
  session.** The two decisions flagged in spec § Open decisions are both **RESOLVED**: **OD-1**
  (zero-member `<group>`) → the **fail-closed loader rejection**, i.e. the alternative rather than the
  bundle's default, now specified as **FR-023** / **SC-013**; and **OD-2** (the Article XVIII §7 /
  Status-banner v0.11 annotation-only amendment) → **ratified**, with the separate Appendix-A user
  `/plan` sign-off **given** the same day. Neither ever blocked `/speckit-tasks` — the bundle shipped
  a stated default for each — and both entries are retained in `spec.md` as audit trail. Gate A round
  1 also added a Clarifications session (2026-07-30) recording the predicate-contract and
  detection-path corrections; Gate A itself **converged at round 3** and was user-signed-off
  2026-07-30.
- **Checklist re-validation after the post-convergence edits.** No item flips. The 16 items are
  generic spec-quality items, not per-FR items; the two that could plausibly move both still hold:
  "All functional requirements have clear acceptance criteria" — the FR → pin map now covers **28/28**
  FRs (FR-023 added with a pin and a *Location*), and "Scope is clearly bounded" — FR-023 is the one
  scope addition, made on an explicit user decision and bounded to two loader validation sites.
- Every other requirement is settled by the pre-spec predicate-equivalence census recorded
  in spec.md § Context (all 9 XML dictionaries + Orchestra FIX Latest), including the FIX43
  divergence that rules out a union predicate.
- Content-quality note: the spec names specific source files, tags, and line numbers. These
  are **evidence citations** for the census and the defect locations (an operator/reviewer
  must be able to verify the claim), not prescribed implementation. FR-001 states *what*
  must no longer gate on datatype; the *how* (additive accessor vs. IR-local derivation) is
  explicitly deferred to `/speckit-plan` in the Assumptions section.
- `/speckit-clarify` (mandatory here per constitution §XVI.3 — codegen + wire + dictionary
  change) is **complete**. All 16 checklist items pass. `/speckit-plan` and `/gate-a` are both done
  — Gate A converged at round 3 and was user-signed-off 2026-07-30 — so the next pipeline step is
  **`/speckit-tasks`**, then `/speckit-analyze` (step 6).
- **Note on the four Appendix-A mandatory controls.** Appendix A (`.specify/constitution.md:414-424`)
  is the canonical trigger reference and requires **all four** — `/clarify`, `/analyze`, Codex Gate A,
  **user `/plan` sign-off** — for the Codegen-layout (`:424`) and Wire-format/parser (`:423`) rows,
  both of which 082 hits. Status: `/clarify` DONE, `/analyze` **PENDING** (pipeline step 6, the only
  outstanding control), Codex Gate A **CONVERGED at round 3 + user-signed-off 2026-07-30**, user
  `/plan` sign-off **GIVEN 2026-07-30**. Tracked as a row in `plan.md` § Constitution Check; the
  fourth control was untracked before Gate A round 1, which is why surfacing it was what made it a
  decision the user could actually give.
