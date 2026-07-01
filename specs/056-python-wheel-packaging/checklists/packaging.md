# Packaging & Artifact Requirements-Quality Checklist: Python Wheel Packaging (PY-005)

**Purpose**: Unit-test the *requirements* for the wheel artifact, its tags, layout, dictionary
bundling, and locator — are they complete, unambiguous, consistent, and measurable?
**Created**: 2026-06-30
**Feature**: [spec.md](../spec.md) · contracts: wheel-packaging (PKG/LAY/TAG), locator-api (LOC)
**Depth**: formal release gate · **Audience**: PR / Gate-B reviewer

## Requirement Completeness

- [x] CHK001 Is the bundled-dictionary requirement specified as an **exact set** (FIX42/FIX44/FIX50SP2/FIXT11), not a representative subset, so set-equality is the acceptance? [Completeness, Spec §FR-004 / LOC-1 / E-3] — PASS: FR-004 names all four dictionaries; LOC-1 mandates set-equality (`BUNDLED_DICTIONARIES == {"FIX42","FIX44","FIX50SP2","FIXT11"}`); E-3 lists exactly the four XML members; T012 wires the LOC-1 set-equality assertion.
- [x] CHK002 Is the flat top-level layout requirement specified as a **fix** of the latent `install(.../fixpp)` namespace-dir rule (not a scikit-build-core bypass), covering both the wheel path and the in-tree `cmake --install` path? [Completeness, Spec §FR-003 / LAY-1 / D-4] — PASS: LAY-1 and D-4 both explicitly call it a "fix" of the latent `install(... /fixpp)` namespace-dir rule; T006 names the in-tree cmake --install path fix alongside the wheel path (changes DESTINATION from `${Python3_SITEARCH}/fixpp` to flat root).
- [x] CHK003 Are the build-backend/toolchain dependency pins specified with a lower bound **and** a verify-against-registry obligation (the `swig>=4.2` pin + its regression rationale)? [Completeness, PKG-1 / D-3] — PASS: PKG-1 states "swig>=4.2 is PINNED — a 4.0 runner silently regresses the limited-API (abi3) mode (research D-3)" with the lower-bound + regression rationale; T001 mandates explicit PyPI/registry verification at implement.

## Requirement Clarity & Measurability

- [x] CHK004 Is "self-contained" (FR-002) quantified with an objectively verifiable witness (`auditwheel show` external-library list empty), rather than left as a vague adjective? [Measurability, Spec §FR-002 / LAY-3] — PASS: FR-002 names the witness explicitly ("the self-containment witness is that `auditwheel show`'s external-library list is empty (LAY-3)"); LAY-3 confirms: "auditwheel show's external-library list is empty (the static-everything self-containment witness)."
- [x] CHK005 Is "exactly ONE wheel" stated unambiguously, so a per-version `cp3XX-cp3XX` matrix cannot accidentally satisfy the deliverable? [Clarity, Spec §SC-004 / TAG-3] — PASS: SC-004 states "exactly ONE wheel, not four"; TAG-3 names it "Exactly ONE wheel fixpp-<ver>-cp310-abi3-manylinux_2_28_x86_64.whl"; FR-010 explicitly retires per-version to "documented fallback, produced only if the abi3 wheel's runtime cross-version import proves flaky."
- [x] CHK006 Is the locator's error contract specified with a **single decided** exception type (`KeyError` listing the sorted valid set) — no silent default, no empty return? [Clarity, Spec §FR-004a / LOC-4 / E-4] — PASS: LOC-4 states "n ∉ BUNDLED_DICTIONARIES → raises KeyError (single decided type — Gate A) whose message lists the sorted valid set. No silent default, no empty return."; E-4 mirrors this verbatim; both explicitly prohibit silent default and empty return.
- [x] CHK007 Is the wheel version **source** requirement (derived from the CMake `project(VERSION)`, single source of truth) specified distinctly from the version **value** (the separate v1.0 bump)? [Clarity, PKG-2 / D-6] — PASS: PKG-2 specifies "version is dynamic, sourced from the CMake project(VERSION) via tool.scikit-build.metadata.version"; D-6 explicitly separates "Version-source (a PY-005 decision)" from "Version-value note (not a PY-005 decision): the project version is 0.0.1 today... The v1.0 release-gate bump to 1.0.0 is a separate release-engineering step."

## Requirement Consistency

- [x] CHK008 Are the mandatory platform and ABI tags specified **identically** (`manylinux_2_28_x86_64` + `cp310-abi3`) across spec, wheel-packaging contract, and data-model E-1 — with no residual raw `linux_x86_64` / per-version tag? [Consistency, Spec §FR-005 / §FR-010 / TAG-1..3 / E-1] — PASS: spec.md FR-001 names `cp310-abi3-manylinux_2_28_x86_64`; TAG-1/2/3 use identical tags; E-1 names "fixpp-<ver>-cp310-abi3-manylinux_2_28_x86_64.whl (exactly ONE wheel)"; no residual per-version or raw linux tag in the spec bundle. (Note: live `.specify/architecture.md §7.1` still reads `cp310-cp310` — this is the documented deferred-apply state per plan.md A-3 / T023, not a bundle defect.)
- [x] CHK009 Is the locator's reachability requirement (surfaced **through** `import fixpp`, not as a new top-level public name) consistent with the "ship the two modules as-is" clarification? [Consistency, Spec §FR-004a / LOC-0] — PASS: FR-004a states the locator "MUST be reachable through `import fixpp`... re-exported via the same additive %pythoncode glue... not as a new separate top-level public import name"; LOC-0 mirrors this ("reachable as fixpp.dictionary_path / fixpp.dictionary_bytes / fixpp.BUNDLED_DICTIONARIES from `import fixpp` (re-export glue)"); consistent with "ship as-is — two separate top-level modules; no restructure."

## Notes

- Check items off as `[x]` during the checklist audit; annotate each disposition
  (PASS / SPEC-FIXED / DD-DECIDED §X / WAIVED:<reason>) inline.

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

Anchors spot-verified: `[2m §1.1]`, `[2m §11]`, `[arch §7.1]`, `[arch §4.12]`, `[const §IV.3]`, `[const §IV.5]` — all resolve in signed-off revisions of `.specify/2m-pybind.md`, `.specify/architecture.md`, `.specify/constitution.md`. Live `[arch §7.1]` still reads `cp310-cp310-manylinux_2_28_x86_64.whl`; this is the documented deferred-apply state (plan.md A-3 item 7 / T023) — not a dangling anchor.
