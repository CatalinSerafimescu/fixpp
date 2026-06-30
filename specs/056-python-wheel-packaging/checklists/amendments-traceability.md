# Amendments & Traceability Requirements-Quality Checklist: Python Wheel Packaging (PY-005)

**Purpose**: Unit-test the *requirements* for the deferred-apply inherited-design amendments
(A-1/A-2/A-3), the catalogue/coverage-index close-out, and the completeness audit — are the
amendment scope, audit-trail rules, and Gate-B close-out obligations unambiguous and complete?
**Created**: 2026-06-30
**Feature**: [spec.md](../spec.md) · plan.md *Proposed inherited-design amendments* · constitution Art XVII §8
**Depth**: formal release gate · **Audience**: PR / Gate-B reviewer

## Requirement Clarity & Completeness

- [x] CHK027 Are the inherited-design amendments (A-1/A-2/A-3) scoped with **exact from→to text and exact site locations** (file:line), so they can be applied unambiguously at `/implement`? [Clarity, plan.md A-3 table] — PASS: plan.md A-3 table provides 8 enumerated amendment sites, each with exact file:line references (e.g., "spec/coverage-index.md:618", ".specify/api-contract.md:268", ".specify/2m-pybind.md:149,151") and exact From (live cp310-cp310 / per-version) → To (cp310-abi3 / abi3 stable ABI) text for unambiguous machine-applicable apply at T023.
- [x] CHK028 Is the deferred-apply boundary specified (the live `.specify/` + `architecture.md` docs are **not** edited by this bundle; amendments apply at `/implement`)? [Consistency, plan.md amendments] — PASS: plan.md explicitly states "The live .specify/2m-pybind.md / .specify/architecture.md are not edited by this bundle"; T023 is the named apply task at /implement; this deferred-apply boundary is the documented intentional state — confirmed via the live stale anchor at `[arch §7.1]` line 468 (still reads `cp310-cp310`).
- [x] CHK029 Is the audit-trail-preservation rule for A-3 item 2b specified explicitly — **annotate with a forward-pointer, do NOT rewrite** the historical resolution quote? [Completeness, plan.md A-3 #2b] — PASS: plan.md A-3 item 2b includes "CRITICAL — A-3 item 2b (api-contract.md:316/334): ANNOTATE each historical resolution quote with a forward-pointer ('superseded by the abi3 pivot — see PY-005 / [arch §7.1] amendment'); do NOT rewrite the quote — the audit trail must not be falsified"; T023 mirrors this CRITICAL note verbatim with exact line refs.

## Acceptance Criteria Quality & Measurability

- [x] CHK030 Is the census claim ("no live normative source names `cp310-cp310` after apply") specified as a **verifiable** post-condition (a grep-verify), not an unprovable assertion? [Measurability, plan.md A-3] — PASS: plan.md A-3 closing section names the exact verification command: "Verify with `grep -rn 'cp310-cp310' .specify/ spec/ → no live normative source names the per-version mandate afterward`"; T023 mirrors this grep-verify post-condition — a machine-executable check, not a prose assertion.
- [x] CHK031 Are the catalogue close-out obligations specified completely (every feature-owned OFFICIAL row → `done` with evidence ref **AND** a matching `coverage-index.md` entry)? [Completeness, Art XVII §8] — PASS: T025 specifies "flip every feature-owned OFFICIAL row in spec/feature-catalogue.md (PY-005) from in-progress/backlog → done with the PR / evidence ref, AND add/update its matching spec/coverage-index.md entry"; T026 pre-flight check (iii) verifies "the PY-005 OFFICIAL catalogue row is done with a matching coverage-index.md entry"; both obligations explicit and paired.
- [x] CHK032 Is the feature-completeness audit specified as a **hard, recorded** Gate-B precondition with a named decision-doc location (`.specify/decisions/<feature>-verify.md` `## Completeness` or `…-completeness.md`)? [Traceability, Art XVII §8] — PASS: T026 specifies "Record the verdict (100% or fully-waived) in `.specify/decisions/056-python-wheel-packaging-verify.md` (## Completeness) or a sibling …-completeness.md. Hard /gate-b precondition (Article XVII §8 / pre-flight 4d)." — both the "hard precondition" classification and the exact named doc path are specified.

## Coverage & Consistency

- [x] CHK033 Is there a complete FR/SC → task traceability mapping such that **every** requirement maps to ≥1 task and **every** task maps to a requirement or a declared process/contingency role? [Coverage, tasks.md traceability] — PASS: tasks.md "Requirement → Task Traceability" table covers FR-001..012 (all mapped), SC-001..007 (all mapped), and all contract rule classes (PKG/LAY/TAG/LOC/CI/REL/WIN/NBC); T022 (quickstart docs) declared as process/documentation role; T015 (per-version fallback) declared as contingency task for FR-010 — no orphaned requirements or unmapped tasks.
- [x] CHK034 Are the TDD-ordering expectations for the packaging tasks specified **consistently with Article VII §3** (test-first where feasible; the artifact-dependent deviation recorded as an explicit waiver, NOT a blanket exemption)? [Consistency, Art VII §3 / Art XVII §6] — PASS: tasks.md preamble applies TDD where feasible (locator T008 is unit-testable in-tree; LOC-1..6 tests in T012 written failing-first before T008 is implemented); the artifact-dependent deviation (wheel build, install-test, CI checks) is explicitly bounded ("NOT a blanket TDD exemption") and to be recorded as an explicit waiver per Art XVII §6 at /speckit-verify (precedent: 039 gate-a-waived); consistent with Art VII §3.

## Notes

- Check items off as `[x]`; annotate each disposition (PASS / SPEC-FIXED / DD-DECIDED §X /
  WAIVED:<reason>) inline during the checklist audit.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 8 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | **8** |

### SPEC-FIXED items
(none)

### DD-DECIDED items
(none)

### WAIVED items
(none)

Anchors spot-verified: `[const §XVII.8]`, `[const §VII.3]`, `[const §IV.5]`, `[arch §7.1]`, `[2m §1.1]`, `.specify/api-contract.md:268`, `spec/coverage-index.md:618` — all resolve in signed-off revisions. The live stale reads at `[arch §7.1]` (`cp310-cp310`), `[2m §1.1]` (per-version form), `api-contract.md:268` (`CPython 3.10 mandatory`), and `coverage-index.md:618` (`cp310-cp310`) are the documented deferred-apply state (plan.md A-1/A-2/A-3 / T023) — not dangling anchors or defects. Post-T023 grep-verify command in plan.md A-3 closing will confirm all four sites update at /implement.
