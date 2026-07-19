# ABI-Invariance & Contract-Widening Requirements Checklist: Orchestra runtime dictionary load

**Purpose**: Gate-B requirements-quality review of the feature's central correctness claim — **no C-ABI change** — and of the contract-widening documentation obligations. Each item tests whether the REQUIREMENTS pin the invariance precisely and whether the widening is documented, not whether the build passes.
**Created**: 2026-07-19
**Feature**: [spec.md](../spec.md) · [contracts/surfaces.md](../contracts/surfaces.md) · [quickstart.md](../quickstart.md)
**Audience / Depth**: Gate B reviewer · formal release gate

## Requirement Completeness (what "no ABI change" must enumerate)

- [x] CHK001 Does the spec enumerate EACH frozen surface that must stay unchanged — no new public C-API function symbol, no new `fixpp_error_t` value, no edit to a byte-frozen header (`error.h`/`version.h`/`include/fix/c_api/dict.h`)? [Completeness, Spec §FR-005] — PASS: FR-005's parenthetical names `error.h`/`version.h` directly; `dict.h` (the header actually declaring the widened entry point) is separately and more specifically pinned by the Frozen-header docs obligation (spec.md line 86, citing `capi_freeze.sha256` line 3) and by tasks.md T016 / quickstart.md ABI gate, both of which list all three headers together. All three frozen headers are enumerated across the audited bundle.
- [x] CHK002 Are the two concrete ABI gates that must pass WITHOUT regeneration named — the `nm` exported-symbol golden (`tests/abi/golden/fixpp_capi_symbols.txt`) and the header byte-freeze (`tools/capi_freeze.sha256`)? [Completeness, Spec §SC-005] — PASS: SC-005 names both files exactly.
- [x] CHK003 Is the required value of `FIXPP_C_ABI_VERSION` (stays `1.5.0`, no bump) stated as an explicit requirement? [Completeness, Spec §FR-005/SC-005] — PASS: SC-005 states "`FIXPP_C_ABI_VERSION` stays `1.5.0`"; verified live value in `include/fix/c_api/version.h` (MAJOR=1, MINOR=5, PATCH=0) matches.
- [x] CHK004 Is the one additive surface this feature DOES add (the new public C++ symbol `dict::load_any`) explicitly identified as the sole new surface, so "no ABI change" is not mistaken for "no new symbols at all"? [Completeness, Spec §SC-005] — PASS: SC-005 states "The only additive surface is one new public C++ `dict::load_any` symbol."; verified `dict::load_any` does not yet exist in the tree (feature unimplemented), consistent with it being the sole planned addition.
- [x] CHK005 Is the pinned frozen-header docs obligation present — a NON-frozen public docs note recording both accepted roots, with `dict.h` Doxygen prose retained verbatim for ABI stability? [Completeness, Spec §Contract & Compatibility Notes / quickstart close-out] — PASS: spec.md line 86 + quickstart.md "Close-out: frozen-header docs obligation" section + tasks.md T019 all pin this identically, including the "MUST NOT edit `dict.h`" constraint.
- [x] CHK006 Is the behaviors-and-limitations close-out obligation (record the C-API/TOML contract-widening as an operator-facing L-row) present? [Completeness, Spec §Contract & Compatibility Notes] — PASS: spec.md line 85 states the obligation; tasks.md T020 operationalizes it as a pinned close-out task.

## Requirement Clarity

- [x] CHK007 Is the widening expressed as a precise input→disposition delta (an Orchestra `<fixr:repository>` input that returned `FIXPP_ERR_CAPI_CONFIG_INVALID` now returns `FIXPP_ERR_OK`) rather than a vague "now supports Orchestra"? [Clarity, contracts/surfaces.md S1] — PASS: contracts/surfaces.md S1 before/after table states this delta by exact error-code names.
- [x] CHK008 Is the distinction between a **behavior** widening (allowed, documented) and an **ABI** change (prohibited) drawn unambiguously, so a reviewer cannot conflate them? [Clarity, Spec §FR-005 / §Contract & Compatibility Notes] — PASS: spec.md Contract & Compatibility Notes "Contract-widening (must be documented, not silent)" bullet + plan.md Summary ("No C-ABI change... behavior widens... but no symbol/signature/`fixpp_error_t` value changes") both draw the line explicitly and consistently.
- [x] CHK009 Is "pass without regeneration" defined clearly (the golden/freeze files are NOT re-baselined as part of this feature)? [Clarity, Spec §SC-005] — PASS: SC-005 states "pass without regeneration"; tasks.md T016 operationalizes it identically ("both pass **without regeneration**").

## Requirement Consistency

- [x] CHK010 Is the "no C-ABI change" claim consistent across spec.md (FR-005/SC-005), plan.md (Constitution Check IX/X, Constraints), contracts/surfaces.md, and quickstart.md — no doc implying a symbol/error-code/header edit? [Consistency, cross-artifact] — PASS: cross-checked all four; spec.md FR-005/SC-005, plan.md Constitution Check rows IX/X + Constraints, contracts/surfaces.md S1 "ABI" bullet, and quickstart.md ABI/hygiene gate section all state the identical no-symbol/no-error-code/no-header-edit claim with no contradiction.
- [x] CHK011 Is the retained `version_registry` `std::abort` backstop described consistently as the direct-C++ fail-loud path (NOT reachable via any config surface) everywhere it appears (spec Key Entities / B&L note / research D-3/D-4)? [Consistency, Spec §Key Entities / §Contract & Compatibility Notes] — PASS: spec.md line 85 (B&L note) and line 94 (Key Entities) use matching wording ("direct-C++ ... bypasses both config surfaces" / "direct-C++ backstop"); research.md D-3 restates the same conclusion ("direct-C++ `version_registry` `std::abort` ... remains the fail-loud backstop"). Verified against live code: `version_registry.cpp:89-98` abort condition unchanged.
- [x] CHK012 Are the superseded collision-leg records (research D-3/D-4) clearly marked as retained-for-audit so they do not read as live ABI requirements (no dangling "add a new error code")? [Consistency, research.md D-3/D-4] — PASS: both D-3 and D-4 open with an explicit "> **SUPERSEDED by Gate A round 1 ... retained for audit (Article XX §1)**" banner; grepped the full bundle for `reason_class`/`conflicting_dictionaries`/collision — every live-code hit is `include/fixpp/config/load_diagnostic.hpp`'s pre-existing enum (no `conflicting_dictionaries` value present), confirming no dangling requirement leaked outside the SUPERSEDED blocks.

## Acceptance Criteria Quality (Measurability)

- [x] CHK013 Is SC-005 objectively verifiable by named tooling (nm symbol golden diff green + `tools/check_capi_freeze.sh` green + version constant == 1.5.0), with a pass/fail bar? [Measurability, Spec §SC-005 / quickstart ABI gate] — PASS: SC-005 + quickstart.md ABI/hygiene gate section name the exact tooling (`nm` golden, `tools/check_capi_freeze.sh`/`capi_freeze.sha256`, `FIXPP_C_ABI_VERSION == 1.5.0`) with a binary pass bar.
- [x] CHK014 Is the additive-symbol claim ("`dict::load_any` is the ONLY new exported surface") given a checkable verification method (diff vs plan Project Structure)? [Measurability, tasks.md T016] — PASS: tasks.md T016 specifies the exact verification method ("diffing the change against plan.md Project Structure §Source Code ... e.g. `git diff --stat` scoped to the feature branch").

## Coverage & Boundary

- [x] CHK015 Are requirements defined for the boundary where the widening MUST NOT extend — i.e. the frozen `dict.h` Doxygen prose must remain byte-identical even though behavior changed? [Boundary / Edge Case, quickstart close-out] — PASS: spec.md line 86 + quickstart.md close-out section both state "MUST NOT be edited" / retained verbatim for `dict.h`'s Doxygen prose; verified `dict.h` is line 3 of `tools/capi_freeze.sha256` (the byte-freeze list), confirming the boundary is enforced by tooling, not just prose.
- [x] CHK016 Is it specified that the descope of the collision leg (no new `reason_class`, no C-ABI error) leaves NO residual requirement for a config-layer pre-check anywhere in the bundle? [Coverage, research.md D-3/D-4 / Spec §Contract & Compatibility Notes] — PASS: grepped the full bundle (spec.md, plan.md, tasks.md, research.md, contracts/, checklists/) for `collision`/`conflicting_dictionaries`/`reason_class` — every hit is either inside a research.md D-3/D-4 SUPERSEDED block, an explicit "not a live requirement" statement (contracts/surfaces.md line 19, tasks.md line 24, spec.md B&L note line 85 "No config-layer collision pre-check is added"), or this checklist's own item text. No dangling live FR/task requires the pre-check; `include/fixpp/config/load_diagnostic.hpp`'s live `reason_class` enum has no `conflicting_dictionaries` member.

## Dependencies & Assumptions

- [x] CHK017 Is the assumption that no codegen/golden/emitter regeneration is triggered (existing read goldens byte-identical) documented as a constraint? [Assumption, Spec §Out of scope / plan Constraints] — PASS: spec.md "Out of scope" bullet ("Any codegen / golden / emitter change...") + plan.md Constraints ("No codegen/golden/emitter change; existing read goldens byte-identical") state this identically.

## Notes

- Check items off as `[x]` with a disposition tag at step-9 audit: `SPEC-FIXED` / `DD-DECIDED §X` / `WAIVED:<reason>`. Completeness/Clarity/Consistency gaps MUST NOT be waived.
- This is the load-bearing checklist for the feature: its whole correctness rests on "no C-ABI change" (SC-005). Verify each frozen-surface and each documentation obligation is actually pinned, not merely asserted.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 17 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | 17 |

### SPEC-FIXED items

None.

### DD-DECIDED items

None.

### WAIVED items

None.

Anchors spot-verified: `include/fix/c_api/dict.h` = line 3 of `tools/capi_freeze.sha256` (matches spec.md L86 citation); `include/fix/c_api/version.h` `FIXPP_C_ABI_VERSION_{MAJOR,MINOR,PATCH}` = 1/5/0 (matches SC-005); `version_registry.cpp:89-98` abort condition (matches D-3/D-4/Key Entities citation); `engine.cpp:108` `build_version_registry` call, `capi/engine.cpp:186-193` `EngineConfig` construction with no `dictionaries` population, `selector_resolver.cpp:359-371` `dictionaries.push_back` (all match research.md D-3's census exactly) — all resolve against the current tree. Signed-off revision: plan.md `## Gate A` (2 rounds converged 2026-07-19); research.md D-1/D-2/D-5 (live), D-3/D-4 (SUPERSEDED-retained-for-audit).
