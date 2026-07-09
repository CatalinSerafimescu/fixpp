# Checklist: C-ABI Freeze & Zero-Alloc Hot Path (Requirements Quality)

**Purpose**: Validate that the requirements around the GA-frozen C-ABI and the zero-global-heap inbound hot path are complete, unambiguous, measurable, and consistent — BEFORE implementation. (Unit tests for the requirements, not the code.)
**Created**: 2026-07-09
**Feature**: [spec.md](../spec.md) · **Focus**: C-ABI freeze, zero-alloc hot path
**Audited**: 2026-07-09 (see [audit-2026-07-09.md](./audit-2026-07-09.md)) — verdict PASS.

## C-ABI Freeze — Requirement Completeness & Measurability

- [x] CHK001 - Is the "no exported C symbol / header / error-enum / version change" requirement stated with an objectively verifiable acceptance signal (a named golden/`capi_freeze.sha256` witness), not just prose? [Measurability, Spec §FR-003/SC-003, Contract §C5] **PASS:** SC-003 + T016 (`tests/abi` golden + `capi_freeze.sha256`); Contract C5.
- [x] CHK002 - Is it explicitly specified that the behavioral change (membership-bounded inbound reads) is NOT an ABI event, and is the boundary between "behavioral" and "ABI" changes unambiguous? [Clarity, Plan §C-ABI] **PASS:** plan §C-ABI ("behavioral … not an ABI event"); FR-003.
- [x] CHK003 - Are the requirements clear that the clone-owned `table_view` and the reify owned copy are INTERNAL additions with no public C-ABI/`reify()` signature change? [Consistency, Data-model §Reused-unchanged, Spec §FR-007] **PASS:** data-model §Reused-unchanged ("no public reify()/factory/C-ABI signature change").
- [x] CHK004 - Is there a requirement covering the case where a new internal `MessageView` accessor could accidentally alter a public/inline header's ABI surface (header-only leakage)? [Gap] **SPEC-FIXED:** plan §C-ABI +1 line — the new accessor is a non-virtual `MessageView` member fn, no data member → no vtable/layout/size change → not an ABI concern; T016 covers the frozen C surface.

## Zero-Alloc Hot Path — Requirement Completeness & Clarity

- [x] CHK005 - Is "no new global-heap allocation on the inbound parse+read path" defined with a measurable gate (which alloc-guard target, what counts as the path)? [Measurability, Spec §FR-004/SC-004] **PASS:** FR-004/SC-004 + T013 (alloc_guard).
- [x] CHK006 - Is the once-at-`open()` `table_view` build explicitly excluded from the hot-path budget, and is "hot path" vs "setup path" delineated unambiguously? [Clarity, Spec §FR-002, Plan §Performance] **PASS:** FR-002 + plan §Performance (built once at open(), not hot path).
- [x] CHK007 - Are the requirements specific that per-message membership lookups + lazily-built nested sub-views draw ONLY from the existing per-message stack arena (not global heap)? [Completeness, Spec §FR-004, Contract §C5] **PASS:** FR-004 + Contract C5 (per-message from stack arena).
- [x] CHK008 - Is the arena-fit requirement quantified with the actual budgets (`kInboundParseArena=16384`, `kAdminParseArena=8192`) AND a near-cap/headroom criterion, rather than "should suffice"? [Measurability, Spec §FR-009/SC-004] **PASS:** FR-009/SC-004 + T014 (16384 + 8192 + near-cap + pathological).
- [x] CHK009 - Does a requirement address the clone/reify path allocation posture (owning handle uses `new_delete`) so the owned `table_view` build is not mistaken for a hot-path violation? [Consistency, Data-model §Clone/§Reify] **PASS:** data-model §Clone/§Reify (owning arena new_delete).
- [x] CHK010 - Is the admin path's allocation posture separately covered (same single parse site, `kAdminParseArena`), or only the app path? [Coverage, Gap, Spec §FR-005] **PASS:** T012/T014 admin path (`kAdminParseArena`); FR-005.

## Cross-cutting

- [x] CHK011 - Are the C-ABI-freeze and zero-alloc requirements internally consistent with the "algorithms unchanged, only internal surfaces added" claim (no hidden new allocating code path implied)? [Consistency, Data-model §Reused-unchanged] **PASS:** data-model §Reused-unchanged (algorithms unchanged).
- [x] CHK012 - Is the fail-closed requirement for a pathological/oversized inbound group tied to a concrete bound (depth `kMaxGroupDepth=16` / entry caps / arena), not just "never over-read"? [Clarity, Spec §FR-009, Contract §C6] **PASS:** FR-009 + Contract C6 (`kMaxGroupDepth=16`/entry/arena).
