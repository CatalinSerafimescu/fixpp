# Checklist: installed layout, package contents & export set — requirements quality

**Purpose**: Unit tests for the *requirements* governing what the package ships, where, and what the export
declares. Validates requirement quality, not package correctness.
**Created**: 2026-08-04 · **Audience**: Gate B reviewer · **Depth**: formal gate
**Feature**: [spec.md](../spec.md) · **Inherits**: `specs/084-packaging-cpack-export/contracts/package-layout.md`

## Requirement Completeness

- [x] CHK031 - Is every installed root this feature adds specified with its exact destination and contents? [Completeness, Data-model §E1] — PASS: data-model E1's table gives exact destination + contents + literal `install(DIRECTORY …)` command for each of the two new roots.
- [x] CHK032 - Are requirements defined for what the isolated roots must **not** contain, or only for what they must? [Completeness, Spec §FR-010a] — PASS: FR-010a's exact-regex containment assertion ("every installed path under `include/capi/` matches `^include/capi/fix/`") is a positive-form bound that equally excludes everything outside it — the only assertion tracing FR-001, per its own text and C-5.
- [x] CHK033 - Is there a requirement that the package-contents gate assert the C-ABI headers positively, given nothing asserted them before this feature? [Completeness, Spec §FR-010] — PASS: FR-010 states the MUST-assert-present, MUST-fail-if-absent requirement explicitly.
- [x] CHK034 - Are requirements stated for the export set's membership and its shipped object files, or is their stability assumed? [Completeness, Spec §FR-016] — PASS: FR-016 requires re-measurement of both the member count and (per Assumptions) the shipped `lib/objects-<CONFIG>/**` files' validity, framed as needing proof rather than assumed.
- [x] CHK035 - Is there a requirement covering the configure-time existence check that makes a missing shipped object file fatal for every consumer? [Coverage, Spec §Edge Cases] — PASS: Edge Cases states the `_cmake_import_check_files_for_fixpp::capi_objects` `FATAL_ERROR` behaviour and the consequence of dropping the export member without dropping the check.

## Requirement Clarity

- [x] CHK036 - Is "purely additive" defined in terms of an observable comparison, and is the thing compared specified (produced manifests vs. install rules)? [Clarity, Spec §FR-005a, §SC-003a] — PASS: SC-003a is explicit — "Verified by comparing **produced artifacts** … never by reading the install rules."
- [x] CHK037 - Is the scope of additivity — file layout only, explicitly not the target graph — stated unambiguously? [Clarity, Spec §FR-005b] — PASS: FR-005b states this in as many words (see abi.md CHK014).
- [x] CHK038 - Where the requirements reference the existing header install rule, is it clear whether it may acquire *new* exclusions versus already carrying some? [Clarity, Contracts §2a] — PASS: contracts §2a distinguishes "unchanged — acquires no new `PATTERN … EXCLUDE`" from "it already carries two, for `fixpp/core/test` and `fixpp/transport/test`, `:449-450`." Anchor spot-verified: `CMakeLists.txt:446-451` is exactly this `install(DIRECTORY …)` block, with the two pre-existing `EXCLUDE` patterns at lines 449–450 verbatim.
- [x] CHK039 - Is the baseline that "additive" is measured against pinned to a named commit rather than left as an ambient artifact? [Clarity, Measurability, Quickstart §2] — PASS: quickstart §2 pins `BASE=$(git merge-base HEAD origin/main)`, records it to `baseline-commit.txt`, and SC-007 states the same requirement ("a durable artifact captured from a named pre-feature commit… not an ambient file").

## Requirement Consistency

- [x] CHK040 - Do the installed-root descriptions agree across spec, data-model, contract and quickstart? [Consistency] — PASS: `include/capi` and `include/service-iface`, with identical contents, appear identically in spec Clarifications, data-model E1, contracts §2 and quickstart §§2–3.
- [x] CHK041 - Is the export-member count stated consistently everywhere it appears, and is each occurrence either a measurement or explicitly marked a prediction? [Consistency, Spec §FR-016] — PASS: spec.md, data-model I5, research.md D-5/R2 all mark 18 as *predicted, to be re-measured*; quickstart §7 derives it explicitly (16 in `FIXPP_EXPORT_TARGETS` + `fixpp` appended + `fixpp_log_otlp` appended). Anchor spot-verified against the real `CMakeLists.txt`: the `FIXPP_EXPORT_TARGETS` list at `:547-566` contains exactly 16 entries (11 "measured" + 5 "derived," not "six" as the file's own stale comment at `:575` claims — a pre-existing, out-of-086-scope comment/count mismatch that does not affect the 16/18 total), `list(APPEND … fixpp)` is at `:593`, `list(APPEND … fixpp_log_otlp)` is at `:601` — the quickstart §7 derivation is exactly right.
- [x] CHK042 - Does any requirement describe the export membership basis in a way that conflicts with how membership is actually determined? [Consistency, Conflict, Spec §FR-003a] — PASS: no conflict found. FR-003a, contracts §1a and data-model E2 all state membership is by explicit enumeration at `CMakeLists.txt:596`, and all explicitly retire the superseded `:575-585` comment-based classification (corrected at Gate A r1, "Codex #10"). Anchor spot-verified: `CMakeLists.txt:596` is exactly `fixpp_capi_objects` inside the `FIXPP_EXPORT_TARGETS` list.
- [x] CHK043 - Are the packaging requirements consistent with the inherited D1 decision, or is the divergence recorded where they differ? [Consistency, Dependency] — PASS: FR-015 explicitly reconciles `package-layout.md` §2a's D1 Option A citations rather than silently diverging; research R2/R7 record that no part of the inherited arrangement changes.

## Acceptance Criteria Quality

- [x] CHK044 - Can the additivity criterion fail? Is the comparison specified so that a removed path produces a non-zero result rather than a printed line nobody checks? [Measurability, Spec §SC-003a] — PASS: quickstart §2's `comm -23 … | tee removed.txt; [ ! -s removed.txt ] || { … exit 1; }` is an explicit non-zero-exit assertion, not a printed-and-ignored line.
- [x] CHK045 - Is the content assertion required to be demonstrated failing, or only to exist? [Measurability, Spec §SC-005] — PASS: SC-005 requires the assertion be "observed failing when their install rule is removed," and T036 operationalizes exactly that demonstration.
- [x] CHK046 - Is the export re-measurement required from a real generate run, with reading the build files explicitly excluded as a method? [Measurability, Spec §FR-016] — PASS: FR-016 explicitly excludes "derived by reading `target_link_libraries`" as a method.
- [x] CHK047 - Is the criterion for "no production source edited" expressed so it can fail, given that the natural command reports success either way? [Measurability, Spec §SC-007] — DD-DECIDED §Gate A carry-forward (T050): the SC-007 requirement text itself is sound, but its quickstart §9 operationalization currently reads `git diff --stat … # MUST be empty: no PRODUCTION source edited` with no exit-code check — `--stat` exits 0 whether or not it prints, so as written the comment asserts nothing. This exact gap is already recorded as Gate A round-3 carry-forward #7 (`quickstart.md:434-435`, T050: replace with `git diff --quiet … || { echo "SC-007 FAIL…"; exit 1; }`), owned by `/speckit-implement`, not re-opened here.

## Scenario & Edge Case Coverage

- [x] CHK048 - Are requirements stated for the platform asymmetry in the installed prefix, so a content assertion cannot pass vacuously on one platform? [Edge Case, Coverage, Spec §Edge Cases] — PASS: Edge Cases "The `usr/` prefix asymmetry" plus US4 Acceptance Scenario 3 both require prefix-normalised assertions on both Windows ZIP and Linux DEB/RPM.
- [x] CHK049 - Are requirements defined for the archive/object naming differences across toolchains, where content assertions must match? [Coverage, Gap] — WAIVED: out of this feature's scope — 086 adds no new archives or objects (it adds only header-tree install rules); the existing toolchain-dependent archive-naming handling (`libfixpp_core.a` vs `fixpp_core.lib`, spot-checked in `tests/packaging/run_package_contents_witness.cmake` around `:380-385`) is inherited unmodified from 084 and is not a concern this feature's requirements need to restate. Tagged `[Coverage, Gap]` only — no Completeness/Clarity/Consistency dimension, so waivable.
- [x] CHK050 - Is the consequence of shipping the same header at two paths addressed for any assertion that counts or exact-matches installed paths? [Edge Case, Spec §Edge Cases] — PASS: Edge Cases states it directly — "a content assertion that counts headers, or asserts an exact set of installed paths, will see both and must be written knowing that."
- [x] CHK051 - Are requirements stated for a stale staging prefix, given that installing does not remove files left by a previous install? [Edge Case, Gap] — PASS: quickstart §§1–2 state the requirement procedurally and explain the consequence in both directions (a stale BEFORE prefix over-reports removals; a stale AFTER prefix masks a genuine one), tied to SC-003a's evidentiary integrity. Treated as covered rather than waived since it is affirmatively required, not merely permissible to skip.

## Dependencies & Assumptions

- [x] CHK052 - Is the dependency on the predecessor feature's packaging machinery documented, including which parts must not change? [Dependency, Spec §Dependencies] — PASS: Dependencies names 084-packaging-cpack-export and what it supplies; FR-005a/contracts §2a name `CMakeLists.txt:446-451` as unchanged.
- [x] CHK053 - Is the assumption that no part of the inherited packaging arrangement needs modification stated and traced to evidence? [Assumption, Research §R2] — PASS: research R2 traces the claim to a measured export-closure analysis, not an assumption stated bare.
- [x] CHK054 - Are the citation corrections owed to the inherited contract scoped by claim, with the non-uniform nature of the drift recorded? [Traceability, Spec §FR-015] — PASS, strong: FR-015 explicitly states "The drift is not a constant, which is exactly why an offset cannot be applied blind" and enumerates both the +1-shifted citations and the one exception. Anchor spot-verified against the real `src/capi/CMakeLists.txt`: `add_library(fixpp_capi STATIC)` is at `:44` (cited as `:43→:44`), the `PUBLIC` link at `:46` (`:45→:46`), the `fixpp_capi_shared` gate at `:48-49` (`:47-48→:48-49`), `WINDOWS_EXPORT_ALL_SYMBOLS` at `:71` (`:70→:71`) — all confirmed +1-shifted as claimed — and the `fixpp_tap` PUBLIC edge is at `:37`, not `:36`, confirming FR-015's claim that this one citation is *not* explained by the uniform +1 offset.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 22 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 1 |
| WAIVED | 1 |
| **Total** | 24 |

### SPEC-FIXED items

None.

### DD-DECIDED items

- CHK047 — Gate A round-3 carry-forward #7, T050 (`.specify/decisions/086-capi-include-isolation-gatea.md` → "Carry-forward into `/speckit-implement`"); rationale: `quickstart.md:434-435`'s `git diff --stat` MUST-be-empty comment is a non-asserting command (`--stat` exits 0 regardless) — already scheduled as a one-line `git diff --quiet` fix at `/speckit-implement`, not re-opened here.

### WAIVED items

- CHK049 — rationale: 086 adds no new archives/objects; the existing toolchain-dependent archive-naming handling is inherited unmodified from 084's witness. Tagged `[Coverage, Gap]` only — confirmed NOT Completeness/Clarity/Consistency. Verified rigorously (not just by absence of new archives): no count-based assertion in `run_package_contents_witness.cmake` ranges over all of `include/**` in a way the two new header roots would perturb — the whole-package floor check (`:311-313`, `_nf LESS 10`) is a total-package sanity count, unrelated to header-tree scoping, and the exact-set/denylist checks (`:484-487`, `:508`) are anchored on `^include/fixpp/…` only, which the new `include/capi/` and `include/service-iface/` roots never match.

Anchors spot-verified: `CMakeLists.txt:446-451,449-450,547-566,562,575,593,601`; `src/capi/CMakeLists.txt:37,44,46,48-49,71`; `tests/packaging/run_package_contents_witness.cmake:365-385` — all resolve and say what the bundle claims, against the signed-off Gate A revision (`086-capi-include-isolation-gatea.md`, round 3, user-signed-off 2026-08-03).
