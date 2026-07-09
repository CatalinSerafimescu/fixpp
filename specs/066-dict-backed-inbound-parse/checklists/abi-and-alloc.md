# Checklist: C-ABI Freeze & Zero-Alloc Hot Path (Requirements Quality)

**Purpose**: Validate that the requirements around the GA-frozen C-ABI and the zero-global-heap inbound hot path are complete, unambiguous, measurable, and consistent — BEFORE implementation. (Unit tests for the requirements, not the code.)
**Created**: 2026-07-09
**Feature**: [spec.md](../spec.md) · **Focus**: C-ABI freeze, zero-alloc hot path

## C-ABI Freeze — Requirement Completeness & Measurability

- [ ] CHK001 - Is the "no exported C symbol / header / error-enum / version change" requirement stated with an objectively verifiable acceptance signal (a named golden/`capi_freeze.sha256` witness), not just prose? [Measurability, Spec §FR-003/SC-003, Contract §C5]
- [ ] CHK002 - Is it explicitly specified that the behavioral change (membership-bounded inbound reads) is NOT an ABI event, and is the boundary between "behavioral" and "ABI" changes unambiguous? [Clarity, Plan §C-ABI]
- [ ] CHK003 - Are the requirements clear that the clone-owned `table_view` and the reify owned copy are INTERNAL additions with no public C-ABI/`reify()` signature change? [Consistency, Data-model §Reused-unchanged, Spec §FR-007]
- [ ] CHK004 - Is there a requirement covering the case where a new internal `MessageView` accessor could accidentally alter a public/inline header's ABI surface (header-only leakage)? [Gap]

## Zero-Alloc Hot Path — Requirement Completeness & Clarity

- [ ] CHK005 - Is "no new global-heap allocation on the inbound parse+read path" defined with a measurable gate (which alloc-guard target, what counts as the path)? [Measurability, Spec §FR-004/SC-004]
- [ ] CHK006 - Is the once-at-`open()` `table_view` build explicitly excluded from the hot-path budget, and is "hot path" vs "setup path" delineated unambiguously? [Clarity, Spec §FR-002, Plan §Performance]
- [ ] CHK007 - Are the requirements specific that per-message membership lookups + lazily-built nested sub-views draw ONLY from the existing per-message stack arena (not global heap)? [Completeness, Spec §FR-004, Contract §C5]
- [ ] CHK008 - Is the arena-fit requirement quantified with the actual budgets (`kInboundParseArena=16384`, `kAdminParseArena=8192`) AND a near-cap/headroom criterion, rather than "should suffice"? [Measurability, Spec §FR-009/SC-004]
- [ ] CHK009 - Does a requirement address the clone/reify path allocation posture (owning handle uses `new_delete`) so the owned `table_view` build is not mistaken for a hot-path violation? [Consistency, Data-model §Clone/§Reify]
- [ ] CHK010 - Is the admin path's allocation posture separately covered (same single parse site, `kAdminParseArena`), or only the app path? [Coverage, Gap, Spec §FR-005]

## Cross-cutting

- [ ] CHK011 - Are the C-ABI-freeze and zero-alloc requirements internally consistent with the "algorithms unchanged, only internal surfaces added" claim (no hidden new allocating code path implied)? [Consistency, Data-model §Reused-unchanged]
- [ ] CHK012 - Is the fail-closed requirement for a pathological/oversized inbound group tied to a concrete bound (depth `kMaxGroupDepth=16` / entry caps / arena), not just "never over-read"? [Clarity, Spec §FR-009, Contract §C6]
