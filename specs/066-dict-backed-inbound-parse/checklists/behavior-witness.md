# Checklist: Strict-Membership Behavior Change & RED-First Real-Dispatch Witnesses (Requirements Quality)

**Purpose**: Validate that the accepted permissive→strict behavior change and the mandatory RED-first real-dispatch witness discipline are specified completely, unambiguously, and with measurable acceptance — so the shipped-path behavior is proven, not inferred. (Unit tests for the requirements.)
**Created**: 2026-07-09
**Feature**: [spec.md](../spec.md) · **Focus**: strict-membership behavior change, TDD RED-first real-dispatch
**Audited**: 2026-07-09 (see [audit-2026-07-09.md](./audit-2026-07-09.md)) — verdict PASS.

## Strict-Membership Behavior Change — Completeness & Clarity

- [x] CHK023 - Is the permissive→strict change specified for BOTH manifestations — (a) trailing non-member field not absorbed by the last instance, and (b) an unknown field INTERIOR to an instance terminating it — rather than only the trailing case? [Completeness, Spec §FR-008/Edge-Cases, Task T004] **PASS:** FR-008/Edge-Cases + T004 assertion (b) interior-truncation.
- [x] CHK024 - Is the scope of "membership-correct" unambiguously limited to group-registering dictionaries (FIX43/44/50/50SP1/50SP2/FIXT), with FIX40/41/42 explicitly carved out? [Clarity, Spec §FR-001/FR-003, Contract §C1] **PASS:** FR-001/003 + Contract C1 (group-registering dicts; FIX4x carve-out).
- [x] CHK025 - Is the FIX4x limitation (INT-typed counts → zero group registration → group reads become TYPE_MISMATCH) documented as a stated, traceable limitation (L-066-x tied to L-063-1), not an unstated surprise? [Completeness, Task T018, `behaviors-and-limitations.md` L-063-1] **PASS:** T018 L-066-x + `behaviors-and-limitations.md` L-063-1.
- [x] CHK026 - Is the extension story precise about what is shipped now (dictionary currency) vs planned (`dialect_overlay`/D-009, backlog) so no reader assumes an unshipped escape hatch? [Consistency, Spec §FR-008, Contract §C3] **PASS:** FR-008 + Contract C3 (dialect_overlay backlog qualifier).
- [x] CHK027 - Is the top-level-unknown-tag tolerance requirement (only group EXTENTS become strict; top-level unknown tags stay indexed/readable) stated so the behavior change is bounded? [Clarity, Spec §FR-008/Edge-Cases] **PASS:** FR-008/Edge-Cases (only group extents strict; top-level tolerated).
- [x] CHK028 - Is the scalar-as-group → `TYPE_MISMATCH` restoration specified as a distinct contract from the extent change (different code path, different acceptance)? [Consistency, Spec §US2/SC-002, Contract §C2] **PASS:** US2/SC-002 + Contract C2 (scalar-as-group distinct).
- [x] CHK029 - Is a release-note / Behaviors-&-Limitations requirement present so the interop-visible behavior change is communicated to counterparties? [Completeness, Spec §FR-008] **PASS:** FR-008 + T018 (B&L row + release note).

## RED-First Real-Dispatch Witnesses — Measurability & Coverage

- [x] CHK030 - Is it a stated requirement that every correctness witness drives a frame through REAL `Session` dispatch (and a C-ABI engine loopback), NOT a `Parser<Index>{dict}` unit parse? [Clarity, Research §Decision-6, Tasks T004/T005/T010] **PASS:** Research Decision 6 + T004/T005/T010 (real dispatch, not `Parser{dict}`).
- [x] CHK031 - Is the RED-first obligation explicit — each witness must be demonstrated failing on the pre-change dict-free parse before the fix — with an objective pass/fail signal? [Measurability, Spec §SC-001, Constitution Art VII §3] **PASS:** SC-001 + Constitution Art VII §3 + quickstart §1 (RED-first).
- [x] CHK032 - Are witness requirements present for each named behavior: trailing-field absent, interior-truncation, scalar-as-group TYPE_MISMATCH, clone identity, reify identity, validator-ON parity? [Coverage, Tasks T004–T010] **PASS:** T004(trailing+interior)/T005(C-ABI)/T010(scalar)/T007(clone)/T008(reify)/T009(validator-ON).
- [x] CHK033 - Is the "no silent regression" requirement measurable — every intended behavior delta in existing suites is an explicit, reviewed, discriminating test edit (not a silently updated assertion)? [Measurability, Spec §SC-003] **PASS:** SC-003 + T011 (explicit reviewed deltas).
- [x] CHK034 - Is the SC-005 prerequisite relationship specified as discharged by 065's own real-dispatch witness (RED before 066+065, GREEN after), i.e. a cross-reference not a 066 test? [Consistency, Spec §SC-005, Task T022] **PASS:** SC-005 + T022 note (065 discharges it).
- [x] CHK035 - Are the required witness message shapes concrete enough (e.g. FIX44 ExecutionReport with `NoLegs(555)`×2 + trailing/interior tags) that the witness cannot degenerate into a weaker proxy? [Clarity, Task T001] **PASS:** T001 (FIX44 ExecutionReport `NoLegs(555)`×2 + trailing/interior).
