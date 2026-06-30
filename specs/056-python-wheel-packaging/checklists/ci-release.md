# CI Gate & Release Requirements-Quality Checklist: Python Wheel Packaging (PY-005)

**Purpose**: Unit-test the *requirements* for the Tier-1 wheel gate, the install-test matrix,
failure semantics, release attachment, and the Windows lane — are the pass/fail criteria
complete, measurable, and unambiguous?
**Created**: 2026-06-30
**Feature**: [spec.md](../spec.md) · contracts: ci-wheel-gate (CI/REL/WIN)
**Depth**: formal release gate · **Audience**: PR / Gate-B reviewer

## Requirement Completeness

- [x] CHK010 Is the CI gate's tier and blocking semantics fully specified (Tier-1, required-to-merge, runs on every PR, behind the same `gate-precheck` guard)? [Completeness, Spec §FR-006 / CI-1] — PASS: FR-006 states "CI MUST build the single Linux x86_64 abi3 wheel from source as a Tier-1 mandatory merge gate (runs on every PR)"; CI-1 confirms "Runs on every PR as a required-to-merge Tier-1 job ([const §IX.6]); gated by the same gate-precheck proceed guard as the other Tier-1 jobs."
- [x] CHK011 Are the release-attach requirements specified including the explicit **negative** requirement (no PyPI / index / `twine` upload anywhere in the workflow)? [Completeness, Spec §FR-008 / REL-1..3 / SC-005] — PASS: FR-008 states "MUST NOT upload to PyPI in v1"; REL-2 states "No PyPI / index upload step exists anywhere in the workflow ([const §IV.5], SC-005)"; SC-005 names "no artifact is uploaded to PyPI" as a success criterion; REL-3 names the exact wheel asset.

## Requirement Clarity

- [x] CHK012 Is the install-test matrix scope specified exactly — the **one** wheel installed on **each** of CPython 3.10/3.11/3.12/3.13, run against the **installed package** (not the build tree)? [Clarity, Spec §FR-006 / CI-4] — PASS: FR-006 names the one wheel, four-interpreter matrix (CPython 3.10/3.11/3.12/3.13) and "installed package (not against the build tree)"; CI-4 adds PYTHONPATH-scrubbed harness + `fixpp.__file__` under venv site-packages assertion.
- [x] CHK013 Are the boundaries of the "functional install-verification subset" specified so its membership is **enumerable** (the `tests/wheel/` suite; canary excluded), not merely implied by a single negative exclusion? [Ambiguity, Spec §FR-007 / D-8] — PASS: spec.md Key Entities (ll.334–336) positively lists "the round-trip, smoke, exception, lifetime, and OO behaviour tests" as the member categories; FR-007 names the excluded test class (GIL-release canary); contract CI-4 names the dedicated `bindings/python/tests/wheel/` suite. Together the membership is enumerable from the spec bundle — not a bare negative exclusion.
- [x] CHK014 Are the Windows lane's **separability** requirements specified precisely enough that its absence or failure provably cannot gate the mandatory Linux deliverable? [Clarity, Spec §FR-011 / WIN-1] — PASS: FR-011 states the lane "MUST be separable such that its absence or failure does not gate the mandatory Linux deliverable"; WIN-1 confirms "never a Linux merge-gate dependency" and "Its absence/failure does not affect CI-1..CI-5"; `[2m §2 non-goal #4]` resolves (line 112: "No Windows mandatory wheel."); US-3 scenario 2 adds an acceptance-criterion witness.

## Acceptance Criteria Quality & Coverage

- [x] CHK015 Are the gate FAILURE conditions specified for **every** failure mode (build OR install OR functional-subset failure → red, no silent pass, no `continue-on-error`)? [Coverage, Spec §FR-009 / CI-5 / SC-006] — PASS: FR-009 enumerates all three failure modes ("cannot be built, cannot be installed into a clean environment, or fails the functional subset"); CI-5 confirms "No continue-on-error" and names all three modes; SC-006 confirms "CI wheel gate to report failure rather than pass."
- [x] CHK016 Is the negative-gate requirement (a deliberately broken wheel MUST turn the gate red) specified with a concrete, **non-publishing** witness rather than asserted abstractly? [Measurability, Spec §SC-006] — PASS: E-5 and the contract "SC-006 named negative witness" section both name the concrete script `tests/wheel/test_broken_wheel_gate.sh` with the specific mutation (remove `_fixpp_data/FIX44.xml`), install into a throwaway venv, assert non-zero, and explicitly state "Non-publishing (never uploads or pollutes the real artifact)."

## Requirement Consistency

- [x] CHK017 Is the "additive only" constraint specified so the new job provably cannot remove, weaken, or replace the existing `python-bindings` sanitizer matrix or the GIL-release canary? [Consistency, Spec §FR-007 / CI-8] — PASS: FR-007 mandates "this feature MUST NOT remove or weaken" the existing CTest sanitizer lanes; CI-8 states "Does not remove, weaken, or replace the existing python-bindings sanitizer matrix or the local-only GIL canary (FR-007). Additive only." — the prohibition is explicit and bidirectional in spec + contract.

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

Anchors spot-verified: `[const §IX.6]`, `[const §IV.5]`, `[2m §2 non-goal #4]` — all resolve in signed-off revisions of `.specify/constitution.md` (§IX.6 line 142; §IV.5 line 75) and `.specify/2m-pybind.md` (non-goal #4 line 112: "No Windows mandatory wheel.").
