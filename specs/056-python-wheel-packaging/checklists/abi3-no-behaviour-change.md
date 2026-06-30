# abi3 / No-Behaviour-Change Requirements-Quality Checklist: Python Wheel Packaging (PY-005)

**Purpose**: Unit-test the *requirements* for the stable-ABI adaptation, the per-version fallback
trigger, and the "no C-ABI / no binding-behaviour change" boundary — are the scope and triggers
precisely bounded and internally consistent?
**Created**: 2026-06-30
**Feature**: [spec.md](../spec.md) · research D-3 / D-8 · contracts: ci-wheel-gate (NBC)
**Depth**: formal release gate · **Audience**: PR / Gate-B reviewer

## Requirement Clarity

- [x] CHK018 Is the abi3-vs-per-version decision stated unambiguously as **"abi3 primary, per-version fallback-only,"** with no residual per-version-mandatory language anywhere in the bundle? [Consistency, Spec §FR-010] — PASS: FR-001 names "single stable-ABI (abi3) binary wheel" as the deliverable; FR-010 explicitly retires per-version to "documented fallback, produced only if the abi3 wheel's runtime cross-version import proves flaky"; no residual per-version-mandatory language appears anywhere in spec.md, contracts, or data-model. (Live `.specify/architecture.md §7.1` still reads `cp310-cp310` — this is the documented deferred-apply state per plan.md A-3 item 7 / T023, not a bundle defect.)
- [x] CHK019 Is the per-version fallback **trigger** precisely specified as a **RED runtime cross-version import** (explicitly NOT the compile-time `-fsyntax-only` check, which proves only the API surface)? [Clarity, Spec §FR-010 / Edge Cases / D-3] — PASS: FR-010 states "The runtime fallback trigger remains the CI install/import/round-trip witness across CPython 3.10–3.13 and MUST fire red before any per-version fallback is produced"; spec.md Edge Cases states "the per-version fallback trigger is the runtime witness, not the compile" and that "-fsyntax-only... proves the API surface but NOT this runtime round-trip"; the distinction is explicit in both the FR and the edge cases section.
- [x] CHK020 Is the bounded permitted binding-source change scoped exactly (the `fixpp_py_is_main_interpreter` rework + `-DPy_LIMITED_API` flags) and explicitly distinguished from the retired "zero binding-source change" claim? [Clarity, Spec §FR-012] — PASS: FR-012 precisely names the permitted change ("the ~10–30-line limited-API rework of `fixpp_py_is_main_interpreter` in fixpp.i... plus `-DPy_LIMITED_API=0x030A0000` compile flags... and the abi3 wheel tag from scikit-build-core's `wheel.py-api = 'cp310'`"); FR-012 also explicitly notes "(Re-scoped at Gate A — this no longer claims 'zero binding-source change'; it claims zero C-ABI change + zero binding-behaviour change.)"

## Requirement Completeness & Coverage

- [x] CHK021 Is the no-change boundary defined as **"no C-ABI change AND no binding-BEHAVIOUR change,"** naming the sub-interpreter `1201` rejection as the specific behaviour that MUST be preserved through the rework? [Completeness, Spec §FR-012 / SC-007] — PASS: FR-012 states "MUST NOT change the C-ABI surface... and MUST NOT change any binding behaviour delivered by PY-001..004... the fixpp_py_is_main_interpreter rework... MUST preserve the sub-interpreter-rejection behaviour"; SC-007 names "C-ABI surface is byte-frozen" + "binding behaviour is unchanged — the frozen PY-001..004 behavioural suite runs unchanged and stays green, including the sub-interpreter witness after the limited-API helper rework"; NBC-2 names the sub-interpreter witness as the specific regression target.
- [x] CHK022 Is the **concentrated verify band (3.10/3.11)** specified as a requirement, with the stated rationale that — absent the 3.12+ import barrier — the reworked runtime check is the sole, load-bearing, previously-unwitnessed rejection there? [Coverage, Spec §SC-007 / FR-012 / D-3 / D-8] — PASS: FR-012 states "CPython 3.10/3.11 is the concentrated verify band" and explains "3.10/3.11 have no import barrier, so the reworked runtime check is load-bearing and previously-unwitnessed there"; SC-007 reinforces with "with 3.10/3.11 the concentrated verify band — those versions have no import barrier, so the reworked runtime 1201 check... is the sole rejection mechanism and is load-bearing there."
- [x] CHK023 Is the in-tree-behavioural-suite-stays-green obligation (NBC-2) specified **distinctly** from the installed-wheel runtime witness, so both the in-tree and installed checks are covered, not conflated? [Consistency, NBC-2 / SC-007] — PASS: NBC-2 in the contract covers the in-tree CTest matrix ("runs unchanged under the existing python-bindings CTest matrix and stays green — including the sub-interpreter witness after the limited-API rework"); T003 explicitly distinguishes ("T013 is the per-version-runtime check; this is the distinct in-tree NBC-2 obligation"); T013 covers the installed-wheel runtime witness cross-3.10–3.13.

## Acceptance Criteria Quality

- [x] CHK024 Is the C-ABI freeze requirement specified with a **measurable** guard (byte-frozen `c_api*.h`; any diff fails the gate) rather than a prose assertion? [Measurability, NBC-1 / SC-007] — PASS: NBC-1 states "A CI step fails on any diff to include/fix/c_api*.h (the C-ABI surface is byte-frozen — the 0→1 GA freeze) and runs the existing ABI/header-occupancy check"; SC-007 specifies "zero modifications to include/fix/c_api*.h; the existing ABI/header-occupancy check passes"; T017 wires this as a concrete CI step — objectively measurable.
- [x] CHK025 Is the public import-surface preservation requirement specified as a verifiable snapshot (every existing name/class still resolves after the additive `%pythoncode` re-export)? [Completeness, NBC-3 / SC-007] — PASS: NBC-3 states "An import fixpp public-surface snapshot asserts every existing name/class (flat functions, FixppError/Error, the OO classes, the new locator) still resolves"; T018 wires this as a test in `tests/wheel/` asserting every existing name/class by category enumeration; the spec.md Key Entities section names all four import-surface components.

## Dependencies & Assumptions

- [x] CHK026 Are the `[verify at implement]` empirical obligations — specifically that the replacement symbols (`PyInterpreterState_Get` / `GetID`) are actually in the limited API at the `0x030A0000` floor — flagged as **open/unverified**, not asserted as established fact? [Assumption, D-3] — PASS: D-3 (the cited anchor) explicitly says "confirm both symbols are in the limited API at the 0x030A0000 floor" — treating availability as an open question; FR-012 uses "(both limited-API; `[verify at implement]` — research D-3)" which is a conditional forward assertion, not an established fact; T004 wires the empirical `-fsyntax-only` check as its first build step. Realizability sub-check: this is a pure-Python / SWIG runtime assumption — no C++ incomplete-type issue; the `[verify at implement]` tag correctly records it as an open assumption.

## Notes

- Check items off as `[x]`; annotate each disposition (PASS / SPEC-FIXED / DD-DECIDED §X /
  WAIVED:<reason>) inline during the checklist audit.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 9 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | **9** |

### SPEC-FIXED items
(none)

### DD-DECIDED items
(none)

### WAIVED items
(none)

Anchors spot-verified: `[2m §6.1]`, `[2m §10 Q8]`, `[2m §10 Q9]`, `[const §VII.3]`, `[const §XVII.8]` — all resolve in signed-off revisions of `.specify/2m-pybind.md` (§6.1 line 986; §10 Q8 line 1480; §10 Q9 line 1481) and `.specify/constitution.md` (Art VII §3 line 104; Art XVII §8 line 289). Realizability sub-check: no C++ value types in this feature's contracts; CHK026 abi3 symbol-availability correctly flagged `[verify at implement]` in D-3 (open assumption, not established fact).
