# Generated Typed API Surface Checklist: FIX Latest Typed Codegen (`fixpp::vlatest`)

**Purpose**: Validate that the requirements defining the generated `fixpp::vlatest` typed surface (scope, shape, boundaries) are complete, clear, consistent, and measurable — Gate-B review audience.
**Created**: 2026-07-15
**Feature**: [spec.md](../spec.md) · [contracts/generated-api.md](../contracts/generated-api.md)

## Requirement Completeness

- [x] CHK001 Are the per-message artifact kinds emitted for the universal surface (typed args, readback accessors, `dict::reify()` participation) enumerated so a missing kind is detectable? [Completeness, data-model Entity 2] — PASS: data-model Entity 2 enumerates exactly 4 universal kinds (typed args struct, readback accessors, `dict::reify()` participation, `version_v` constant) plus 2 app-subset kinds (`build_<Msg>`, `validate_<Msg>`); contract C-1 mirrors the same enumeration.
- [x] CHK002 Is the `version_v` constant value and its derivation specified, including the special-case that avoids referencing a non-existent `application_version::vlatest`? [Completeness, Spec §FR-005] — PASS: FR-005 + research R3 specify `app_version_enum("vlatest") → application_version::v50sp2`; code-read confirms `application_version` (`include/fixpp/dict/version_profile.hpp:51-61`) caps at `v50sp2` (no `vlatest` member) and the existing `vt11→Unknown` special-case precedent (`gen_util.hpp:248-253`) the new case mirrors.
- [x] CHK003 Are the field-membership requirements (header + body + repeating-group members at all depths, resolved components, flattened codeset enums, dropped `unionDataType` second arm) specified? [Completeness, data-model Entity 2 / Spec §Edge Cases] — PASS: data-model Entity 2 "Field membership" paragraph + spec.md Edge Cases both state this identically.
- [x] CHK004 Is the namespace disjointness requirement (`fixpp::vlatest` collision-free with `fixpp::v50sp2` and all tiers) stated with a concrete uniqueness rule (the `ns` key)? [Completeness, Spec §FR-001 / C-1] — PASS: data-model Entity 1 "Uniqueness rule: `ns` MUST be unique across rows"; code-read confirms `kCodegenVersions` (`ir.cpp:212-227`) has no `vlatest` row today (adding one with `ns="vlatest"` is collision-free by construction against existing `v42/v44/v50sp2/vt11` rows).

## Requirement Clarity

- [x] CHK005 Is the Option-A surface split — read/reify/args/readback for **all 181** vs `build_<Msg>`/`validate_<Msg>` for the **application subset** — stated unambiguously (not the earlier "typed builder for all 181" wording)? [Clarity, Spec §FR-001 / Clarifications] — PASS: spec.md Clarifications explicitly names and flags the precision correction; FR-001/C-1/data-model Entity 2 state the split unambiguously; converged and user-confirmed through Gate A rounds 2-3 (plan.md Gate A log, RC-B).
- [x] CHK006 Are the application-subset selection criteria defined precisely enough to derive the subset from the Orchestra `category→is_application` mapping without a pinned count? [Clarity, Spec §FR-001 / data-model Entity 5] — PASS: data-model Entity 5 + research R2b state the verified single-category rule (8 `category="Session"` frames = admin, all else = app), explicitly not pinned to a count, fail-closed on unmapped category.
- [x] CHK007 Is the scope of the generated `validate_<Msg>` (required/group-presence only, dict-free) clearly distinguished from the separate runtime enum/type/domain validator? [Clarity, Spec §generated-api C-2] — PASS: contract C-2 draws the distinction explicitly (thin wrapper over `wire::validate_required<T>` vs. `dictionary_driven_validator`/`table_view` ctor arg/`wire_field_value_out_of_range`), matching data-model Entity 2 and spec AC-2.

## Requirement Consistency

- [x] CHK008 Is the "no `build_<Msg>` for admin/session frames" behavior documented as by-design (Option A) consistently across spec, data-model, and contracts — never as a coverage gap? [Consistency, Spec §FR-001 / FR-007 / data-model Entity 2] — PASS: FR-001, FR-007, contract V-2, and spec Edge Cases all use identical "by design (Option A), NOT a skip" language; no artifact frames it as a gap.
- [x] CHK009 Is the dispatch-exclusion boundary (`vlatest` not wired into `dispatch_application`) stated as an intentional requirement with the injective-wire-ApplVerID-map rationale, consistent with the deferred ApplExtID scope? [Consistency, Spec §FR-009 / C-3] — PASS: FR-009, contract C-3, Out-of-Scope section, and research R7 state the same rationale consistently; code-read confirms `kAppVersions` (`emit_dispatch.cpp:61-65`) lists only `v42/v44/v50sp2` today, matching the "would collide" premise.
- [x] CHK010 Do the contract (generated-api.md) and spec agree on the single generated application-dispatch surface (`dispatch_application`; no separate validator-dispatch switch)? [Consistency, Spec §FR-009 / contract C-3] — PASS: FR-009 and contract C-3 use verbatim-matching language ("exactly one such surface... no separate validator dispatch"); code-read confirms the generated validator emits only `constexpr` tables (no dispatch switch).

## Acceptance Criteria Quality

- [x] CHK011 Is behavioral parity with legacy tiers (build → serialize → read-back field-for-field) expressed as an objectively measurable acceptance criterion? [Measurability, Spec §C-2 / SC-002] — PASS: SC-002 + contract V-2 state a binary, objectively-checkable field-for-field equality / zero-skips assertion.
- [x] CHK012 Can the "reachable only via the direct typed API with a caller-supplied dictionary" boundary be objectively verified (World-A independence from the deferred `version_registry` re-keying)? [Measurability, Spec §C-3 / FR-009] — PASS: research R7 records the "World A" verified precondition (code-read grade-1) plus contract V-3's two concrete witnesses ((a) no vlatest-typed owner via `dispatch_application`, (b) exactly one `v50sp2` case), both objectively checkable.

## Notes

- Audience: Gate B reviewers. Items test whether the **requirements** for the generated surface are well-written — not whether the emitter works (that is V-1..V-7).
- Disposition each item at `/speckit-checklist-audit` (SPEC-FIXED / DD-DECIDED §X / WAIVED:<reason>).

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 12 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | 12 |

### SPEC-FIXED items

None.

### DD-DECIDED items

None.

### WAIVED items

None.

Anchors spot-verified (code-read, this audit): `include/fixpp/dict/version_profile.hpp:51-61` (`application_version` caps at `v50sp2`, no `vlatest` member); `tools/codegen/fixpp-codegen/gen_util.hpp:248-253` (`vt11→Unknown` special-case precedent); `tools/codegen/fixpp-codegen/ir.cpp:212-227` (`kCodegenVersions`, no `vlatest` row yet); `tools/codegen/fixpp-codegen/emit_dispatch.cpp:61-65` (`kAppVersions` = v42/v44/v50sp2 only) — all resolve as cited against the current `main` tree (pre-implementation state, as expected for a Gate-A-converged, not-yet-implemented feature).
