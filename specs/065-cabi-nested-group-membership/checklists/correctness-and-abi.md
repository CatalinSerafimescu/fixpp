# Checklist: Nested-Extent Correctness & C-ABI Freeze (Requirements Quality)

**Purpose**: Validate that the requirements around the membership-bounded nested-extent fix, the absent/empty distinction, the scope limitations, and the GA-frozen C-ABI + zero-alloc invariants are complete, unambiguous, measurable, and consistent — BEFORE implementation. (Unit tests for the requirements, not the code.)
**Created**: 2026-07-10
**Feature**: [spec.md](../spec.md) · **Focus**: nested-extent correctness, C-ABI freeze, zero-alloc, scope boundaries

## Nested-extent correctness — Completeness & Measurability

- [x] CHK001 - Is the defect stated with an objectively verifiable acceptance signal — a read of the trailing outer member against the LAST nested instance returns `FIXPP_ERR_TAG_NOT_FOUND` (not `OK`+value) — rather than prose alone? [Measurability, Spec §FR-002/SC-001, Contract §C1] — **PASS:** FR-002 states the exact TAG_NOT_FOUND signal; SC-001 makes it measurable ("100% of cases (0 silent wrong values)"); Contract §C1 states the before/after byte-precise expectation.
- [x] CHK002 - Is it a stated requirement that the fix bounds EVERY nested instance (including the last) by dictionary MEMBERSHIP via the shared primitive, not a positional heuristic, so C-ABI and C++ agree by construction? [Clarity, Spec §FR-001/FR-005, Research §Decision-1] — **PASS:** FR-001 ("bound each nested-group instance — including the last — by the nested group's dictionary membership") + FR-005 (reuse mandate); Research Decision-1 names the delegated primitive `nested_group_slices`.
- [x] CHK003 - Is the "reuse the existing machinery, not a new membership implementation" constraint (FR-005) specified as a testable no-divergence requirement, with the untouched primitives named (`nested_group_slices` 7-arg / `build_nested_subview` / `consume_group_extent` + cache keying)? [Clarity, Spec §FR-005, Plan §Untouched-by-contract] — **PASS:** plan.md "Untouched by contract" line names all three primitives + cache keying verbatim; FR-005 states the no-divergence rationale ("so the C-ABI and C++ paths agree by construction").
- [x] CHK004 - Is the reported nested instance count + per-instance genuine member values required to be correct for MULTI-entry nested groups (not just the trailing-member edge)? [Coverage, Spec §FR-004, Contract §C1] — **PASS:** FR-004 states this directly; Contract §C1 asserts it for every nested index `j` in `[0, nc)`, not just the last.
- [x] CHK005 - Is the outer-index read of the trailing member required to STAY `FIXPP_ERR_OK`+correct-value (the member genuinely belongs to the outer entry), so the fix is a strict narrowing of the nested extent, not a relocation of the member? [Consistency, Spec §FR-003, Contract §C2] — **PASS:** FR-003 + US1 Acceptance Scenario 2 + Contract §C2 all state the outer-index read is unchanged/OK.

## Absent vs empty distinction — Preservation

- [x] CHK006 - Is the absent-vs-empty-count distinction specified as an explicit preserved contract (nested_tag absent → `TAG_NOT_FOUND`; present-with-count-0/count-is-last → `OK`, nc=0), with the pinning tests named? [Completeness, Spec §FR-002, Contract §C3, Research §Decision-6] — **PASS:** Contract §C3 states the distinction and names both pinning tests verbatim (`NestedGroupAbsentTag` / `NestedGroupEmptyGroupCountLastField`); Research Decision-6 gives the mechanism; Spec US2 Acceptance Scenario 2 states the behavioral requirement. (Note: the checklist's "FR-002" cite is a loose pointer — FR-002 covers the field-level TAG_NOT_FOUND signal, not this absent-vs-empty group-descent distinction specifically; the substantive coverage is in Contract §C3 / Research Decision-6 / US2-AS2, which is sufficient.)
- [x] CHK007 - Is the presence-probe's empty→OK mapping specified as MEMBERSHIP-FREE and safe ONLY because the collapsed-`err_group_too_large` empty span is unreachable here (no error is silently mapped to OK)? [Clarity, Research §Decision-6 scope, Spec §FR-009] — **PASS:** Research Decision-6 "Scope of the empty → OK, nc=0 mapping" states this exactly ("membership-free slice scan for the successful empty cases only... maps no error to OK... safe only because of that unreachability argument"); FR-009 states the corrected unreachability argument.

## Scope boundaries — Clarity & Traceability

- [x] CHK008 - Is the membership-collision case (a trailing tag equal by VALUE to a non-delimiter nested member) specified as a stated SCOPE LIMITATION with a traceable owner (L-062-3 / L-063-4 / issue #180), not silently claimed as solved? [Completeness, Spec §Edge-Cases, Contract §C5] — **PASS:** Spec Edge-Cases (first bullet) and Contract §C5 both state this explicitly with the exact tracking rows; verified live in `spec/behaviors-and-limitations.md:1693` (L-062-3) / `:1701` (L-063-4), both citing issue #180.
- [x] CHK009 - Is the depth-1 scope boundary unambiguous — depth-≥2 nested-in-nested read correctness/equivalence is OUT of scope and unwitnessed, and the pre-existing typed-path depth-2 gap is recorded (candidate L-065-1), not fixed? [Clarity, Spec §Edge-Cases, Research §Decision-7, Contract §C5a] — **PASS:** Spec Edge-Cases (second bullet), Research Decision-7 (full mechanism + rationale for NOT fixing), and Contract §C5a all state the depth-1 scope boundary and candidate `L-065-1` consistently.
- [x] CHK010 - Is the dropped coded `WIRE_LIMIT_EXCEEDED` guard's acceptability tied to a SOURCE-anchored unreachability argument (outer-first `group_slices()` pre-walk fails first + default `Config` caps equal), not to "regresses nothing"? [Measurability, Spec §FR-009, Contract §C6, Research §Decision-7] — **PASS:** FR-009's corrected note and Contract §C6 both give the full source-anchored unreachability argument (outer-first pre-walk recursion at `offset_table.cpp:476-482` + always-default `Config`). Citation note: the unreachability argument's fullest form lives in research.md's "Cross-cutting facts verified during research" section (the paragraph immediately following Decision-7, not literally under the "Decision 7" heading) — a loose section pointer, not a missing requirement; the substance is present and verified live.

## C-ABI freeze & zero-alloc — Measurability

- [x] CHK011 - Is the "no exported C symbol / public header / error-enum / version change" requirement stated with an objective witness (`tests/abi/golden/fixpp_capi_symbols.txt` + `tools/capi_freeze.sha256` byte-identical to `1.5.0`), not prose? [Measurability, Spec §FR-006/SC-003, Contract §C7] — **PASS:** Contract §C7 names the two objective witness files verbatim and byte-identical; FR-006/SC-003 state the requirement; T012 operationalizes the check (`sha256sum -c tools/capi_freeze.sha256`).
- [x] CHK012 - Are the three surface additions (internal `fixpp_group::group_ctx`, `OffsetTable::group_context_for`, `nested_group_slices` 4-arg overload) each specified as NON-C-ABI with the boundary explicit (internal struct forward-declared only; public-C++ recompiled), so none is mistaken for an ABI event? [Clarity, Plan §Post-design-re-check, Data-model §Entities] — **PASS:** plan.md "Post-design re-check" names all three and states the boundary (internal struct only forward-declared in the public header; the two `OffsetTable` seams are public C++ recompiled with the library); data-model.md §Entities restates the same for each. Complexity Tracking table also justifies each individually.
- [x] CHK013 - Is the zero-global-heap requirement measurable (nested sub-table + slices from the per-message arena; alloc-discipline gate), unchanged from today? [Measurability, Spec §FR-007/SC-004, Plan §Constitution-Check] — **PASS:** FR-007/SC-004 state the requirement; Plan Constitution-Check + T013 name the alloc-discipline gate as the measurable instrument, unchanged from `build_nested_subview`'s existing arena-only allocation.

## Notes

- Requirements-quality checklist (probes the SPEC, not the implementation). Dispositioned by the pipeline step-9 checklist audit before `/speckit-implement`.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 13 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | 13 |

### SPEC-FIXED items
None in this checklist (the one bundle-wide SPEC-FIXED edit — the dangling `research New #3` anchor — is recorded against CHK024 in `witness-discipline.md`).

### DD-DECIDED items
None.

### WAIVED items
None.

Anchors spot-verified: `struct fixpp_group` = `capi_internal.hpp:~310` (live, no `group_ctx` field yet — pre-implementation, expected); `fixpp_msg_get_group` = `message_read.cpp:336`; `stored_group_context()` private = `offset_table.hpp:257`; `gen_` `#ifndef NDEBUG` = confirmed live; `nested_group_slices` 7-arg = confirmed live; `L-062-3`/`L-063-4`/issue #180 = `spec/behaviors-and-limitations.md:1693,1701` — all resolve in the current bundle/source tree (2026-07-10). CHK010's research.md pointer was a loose section reference (content present, not literally under "Decision 7" heading) — noted, not a defect.
