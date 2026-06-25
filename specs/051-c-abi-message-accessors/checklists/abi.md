# ABI Requirements Quality Checklist — 051-c-abi-message-accessors

**Purpose**: Validate that the ABI-surface requirements (symbol set, error-code block, versioning, occupancy/audit gates) are complete, unambiguous, and consistent. Audience: Gate B reviewer. Depth: formal release gate (GA-permanent C-ABI surface).
**Created**: 2026-06-25

## Symbol-surface completeness & clarity

- [x] CHK001 - Is the exact new exported-symbol count specified and reconciled across spec/plan/contracts (8 read + 10 outbound + 6 group-read + 8 group-build + 1 register = 33)? [Consistency, Plan §Scale/Scope] — PASS: plan.md §Scale/Scope enumerates 8+10+6+8+1=33 identically; FR-001/002/003/004/005/006/010/011/012/022 map each group; contracts/message-read.md (8+6), contracts/message-write.md (10+8), contracts/toapp-callback.md (1) are consistent.
- [x] CHK002 - Is the nm golden (`tests/abi/golden/fixpp_capi_symbols.txt`) named as the single source of truth for the symbol set, and is it unambiguous which artifacts are NOT exported (the 6 `#define`s, the verdict enum, the callback typedef)? [Clarity, Plan §Scale/Scope] — PASS: plan.md §Scale/Scope states "nm golden `tests/abi/golden/fixpp_capi_symbols.txt` is the single source of truth"; contracts/toapp-callback.md explicitly marks the `fixpp_toapp_verdict` enum and `fixpp_toapp_cb` typedef as non-exported; error-block-amendment.md notes the 6 `#define`s are not symbols.
- [x] CHK003 - Is each of the 33 symbols traceable to exactly one FR and one golden-append task? [Traceability, Tasks T007/T010/T014/T017/T021] — PASS: tasks.md T007 (8 read), T010 (CA-009 lifecycle/set/commit/clone, 10 symbols), T014 (6 group-read), T017 (8 group-build including fixpp_entry_group_begin), T021 (1 register_send_callback) each enumerate their symbol subsets; all 33 trace back to FR-001/002/003/004/005/006/010/011/012/022 respectively.
- [x] CHK004 - Is the rationale for omitting `fixpp_entry_set_bytes` (vs the msg-level `fixpp_msg_set_bytes`) documented so the asymmetry is not read as a gap? [Completeness, contracts/message-write.md] — PASS: contracts/message-write.md explicitly documents the omission: "fixpp_entry_set_bytes is intentionally omitted (only fixpp_msg_set_bytes exists at message level; entry-level raw-bytes set would bypass the dictionary grammar)"; the rationale is load-bearing.

## Error-code block completeness & consistency

- [x] CHK005 - Is the numeric range of the new session/app block fixed unambiguously to `[1400,1499]` everywhere (spec/plan/contracts/error-block-amendment), with no residual `[11,99]`? [Consistency, Spec §FR-013] — PASS: `[1400,1499]` appears consistently in spec FR-013, plan §Gate-A-round-1 ruling, data-model E-5, contracts/error-block-amendment.md §The new block; all `[11,99]` occurrences in spec/plan/contracts explicitly mark it as the REJECTED alternative (RULED over), never the chosen placement; no drift detected.
- [x] CHK006 - Are all six codes named, numbered (1400–1405), and mapped to their source (five C++ ordinals 119/77/129/130/131; 1405 = construction reject, no ordinal)? [Completeness, Spec §FR-014] — PASS: spec FR-014 enumerates all six with names, numerics, and ordinal mapping; contracts/error-block-amendment.md §The new block gives the exact `#define` block with C++ ordinal comments; 1405 explicitly noted as "no C++ ordinal — pure C-ABI construction reject" in both FR-014 and the contract.
- [x] CHK007 - Is the "freezes permanently at the 0→1 GA cut" constraint stated, and is the choice justified against the alternatives (the `[11,99]` reversal rationale)? [Clarity, Spec §FR-013] — PASS: spec FR-013 states "The slot freezes permanently at the 0→1 GA cut"; Assumptions §5 restates the GA-permanence; error-block-amendment.md Gate-A-talking-point 1 provides the `[11,99]` reversal rationale (sentinel contamination vs dedicated domain).
- [x] CHK008 - Is the per-code introducing-minor requirement (existing codes minor 2, the six new minor 4) specified precisely enough to forbid a scalar bump-to-4 regression? [Clarity, Spec §FR-015] — PASS: spec FR-015 explicitly states "replace the scalar kIntroducingMinor=2 with a PER-CODE introducing-minor lookup"; error-block-amendment.md co-update table row 4 names the exact defect the scalar-bump causes ("silently downgrade EVERY existing 0.2/0.3 code at consumer_minor=3"); verified error_codes_v1.txt shows all existing 97 codes at minor 2, none at minor 3 (Feature B bumped MINOR but added no error codes), confirming "existing codes keep minor 2" is accurate.
- [x] CHK009 - Is the complete co-update set enumerated as a single atomic pass (error.h, version.h, error.cpp translate/strerror/minor-table, `[2i §4.3]`, error_codes_v1.txt, occupancy)? [Completeness, Spec §FR-015] — PASS: error-block amendment sites (error.h, error.cpp translate/strerror/kIntroducingMinor, error_codes_v1.txt, check_capi_occupancy.sh, [2i §4.3]) are in contracts/error-block-amendment.md §Co-update set (7 rows); version.h (FR-019) is included in T004's atomic-pass list; together the co-update set is complete. The CHK parenthetical groups FR-015 + FR-019 sites together — all are enumerated across spec + tasks.md T004.
- [x] CHK010 - Is it explicit that 1405 gets a strerror entry but NO `translate()` arm (only the five mapped arms are re-pointed)? [Clarity, Spec §FR-015] — PASS: spec FR-015 states "re-point the five mapped arms off FIXPP_ERR_UNKNOWN; 1405 gets no translate() arm"; error-block-amendment.md co-update table row 2 repeats "1405 gets NO translate() arm (no C++ ordinal; raised only by the set_* reject path)" and row 3 confirms k_strerror_table gets +6 entries including 1405.

## Occupancy / audit / additivity gates

- [x] CHK011 - Are the occupancy-gate effects specified precisely: Check A `EXPECTED` gains six entries; Check B's 8 prior-doc source counts, the `[0,99]` 11/8 count, and the prior-doc `97` total are UNCHANGED? [Clarity, Spec §FR-016] — PASS: spec FR-016 enumerates exactly these invariants (Check A +6 entries 1400–1405; Check B EXPECT_COUNT unchanged; [0,99] 11/8 count unchanged; prior-doc 97 total unchanged); error-block-amendment.md co-update table row 6 confirms the same. Three separate named unchanged quantities prevent partial-sweep confusion.
- [x] CHK012 - Are the swept published-block `[2i §1.1]` sites vs the unchanged prior-doc-count sites (`[2i §3.11]`/`[2i §6.5]`/Appendix D.2) enumerated so a partial sweep is detectable? [Completeness, Spec §FR-016] — PASS: spec FR-016 explicitly lists swept sites ([2i §1.1] magnitude-domain table, final-layout block, reserved-blocks prose, [2i §4.3] inline block) and explicitly states "[2i §3.11] 2d-count prose, [2i §6.5] prior-doc total, Appendix D.2 are UNCHANGED"; error-block-amendment.md co-update table row 7 mirrors this; a partial sweep is detectable because the two sets are named distinctly.
- [x] CHK013 - Is the additivity requirement measurable (abidiff reports additive; no slot re-defined) and tied to an acceptance criterion? [Measurability, Spec §SC-005] — PASS: spec SC-005 states "abidiff vs the merged Feature B baseline reports additive-only additions (no removals, no renamed symbols, no changed signatures)" as the acceptance criterion; tasks.md T026 makes the abidiff run part of the gate-b pre-flight; the append-only invariant in error_codes_v1.txt (verified: no slot reused) is the complementary check.
- [x] CHK014 - Is the append-only nature of `error_codes_v1.txt` (6 new rows, introducing-minor 4) stated as an invariant? [Completeness, Spec §FR-015] — PASS: spec FR-015 states "append-only tools/abi_history/error_codes_v1.txt audit (six rows, introducing-minor 4)"; error-block-amendment.md co-update table row 5 states "append 6 rows (1400–1405, introducing-minor 4)"; file header (verified) states "append-only per [const §X.4]"; the [const §X.4] binding rule makes violation a constitution defect.

## Versioning & amendment process

- [x] CHK015 - Is the additive MINOR bump (0.3.0 → 0.4.0) specified, and is the 0→1 major freeze explicitly deferred to GA? [Clarity, Spec §FR-019] — PASS: spec FR-019 states "additive MINOR bump 0.3.0→0.4.0 (new exported message-surface symbols + new additive session/app error codes); the 0→1 major freeze stays deferred to GA per remaining-work/release-engineering.md Task 2"; data-model E-8 repeats FIXPP_C_ABI_VERSION_MINOR 3→4; version.h confirmed at MINOR=3 (current state pre-feature-C).
- [x] CHK016 - Is the Article XX amendment scope bounded to `[2i §4.3]` only, with `[2i §4.7]` explicitly NOT edited (the stale-send-prose recorded as a local deviation)? [Consistency, Spec §FR-008a] — PASS: spec FR-008a explicitly states "[2i §4.7] is NOT edited and the reconciliation is NOT part of the §4.3 amendment's [2i] co-update set: Article XX authorises reopening [2i §4.3] (the error enum) only"; clarifications Q4 records the same; error-block-amendment.md §Co-update set lists only §1.1 + §4.3 [2i] changes, not §4.7.
- [x] CHK017 - Is the forward-compat downgrade requirement (below-minor consumer sees `FIXPP_ERR_UNKNOWN`, never a wrong-meaning code) specified and witnessed BOTH ways? [Coverage, Spec §FR-017/SC-004] — PASS: spec FR-017 states both witness directions ("a NEW minor-4 code → UNKNOWN AND an EXISTING minor-2 code (FIXPP_ERR_DICT_CONFIG) SURVIVES at consumer_minor=3"); SC-004 makes both explicit acceptance criteria; tasks.md T004 (RED-first) and T022 both mandate the dual-direction witness.

## Per-symbol reentrancy annotation (ABI gate)

- [x] CHK018 - Is the "0 unannotated symbols" requirement stated, and does every one of the 33 symbols have an assigned reentrancy class in the requirements? [Completeness, Spec §FR-018] — PASS: spec FR-018 states "zero unannotated new symbols — every one of the 33 must have an assigned class"; data-model E-7 gives the reentrancy taxonomy table covering all groups (reads/setters/group-cursors → FIXPP_REQUIRES_SESSION_LOCK; fixpp_msg_version/fixpp_msg_destroy → FIXPP_THREAD_SAFE; fixpp_msg_clone source → REQUIRES_SESSION_LOCK); [2i §4.10] is the anchor for this taxonomy.
- [x] CHK019 - Is the single-conservative-class decision (one `FIXPP_REQUIRES_SESSION_LOCK` annotation on the shared read symbols, with the clone THREAD_SAFE property OUTSIDE the static gate) stated so the gate stays unchanged and `[2i §4.6]` is not edited? [Clarity, Spec §FR-018] — PASS: spec FR-018 states "single conservative class — FIXPP_REQUIRES_SESSION_LOCK on reads/setters/group; the clone THREAD_SAFE guarantee is a runtime/handle-state property outside the static annotation gate so [2i §4.6] is not edited"; data-model E-7 repeats "clone THREAD_SAFE is a runtime guarantee, NOT a second static annotation"; the check_capi_reentrancy.sh gate contract stays unmodified.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 19 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | **19** |

### SPEC-FIXED items
(none)

### DD-DECIDED items
(none)

### WAIVED items
(none)

Anchors spot-verified: `[2i §4.3]` (line 473), `[2i §4.6]` (line 723), `[2i §4.7]` (line 887), `[2i §4.8]` (line 1017), `[2i §4.10]` (line 1168), `[2i §1.1]` (line 30), `[2i §3.11]` (line 210), `[2i §6.5]` (line 1397), `[arch §5.2]` (architecture.md §5.2 Allocator policy, line 384), `[arch §5.3]` (architecture.md §5.3 Error model, line 391), `[const] Article X` (constitution.md line 148), `[const] Article XX` (constitution.md line 330) — all resolve in signed-off revision [2i] v0.3 (Gate A round 2 converged).
