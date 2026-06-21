# Checklist: Concurrency + ABI requirements quality — 048 async_mutex strand-local reap

**Purpose**: "Unit tests for the requirements" — validate that the 048 spec/plan/research/data-model/contract
state the strand-local-drain, OOM, contract-narrowing, ABI, and test-discipline requirements completely,
clearly, consistently, and measurably. Audience: Gate B reviewers. **Created**: 2026-06-22.
**Feature**: [spec.md](../spec.md) · design [research.md](../research.md) · [contracts/async_mutex-contract.md](../contracts/async_mutex-contract.md)

## Strand-local drain correctness (requirements)

- [ ] CHK001 Is the drain TERMINAL condition specified completely and identically across spec/research/data-model/contract — i.e. `holders==0 ∧ in_flight_resumers_==0 ∧ both lists empty in one pass`? [Consistency, Spec §FR-001 / contract INV-D]
- [ ] CHK002 Is it explicitly stated WHY `active_holders_count_==0` alone is insufficient (the immediate-destroy UAF), so an implementer cannot "simplify" back to a holder-only wait? [Clarity, research D-2 / P1-1]
- [ ] CHK003 Are the reentrant-drain completion semantics defined unambiguously — a second drain AWAITS `draining_complete_` and returns the terminal result, NOT eager-ok? [Completeness, contract §cancel_and_drain.1 / FR-001]
- [ ] CHK004 Is the `in_flight_resumers_` ownership rule single-sourced — `schedule_record_resume()` is the SOLE incrementer on EVERY path (reap/grant/drained-bypass/on_cancel), runner decrements after `release_ref`? [Consistency, research W-3c]
- [ ] CHK005 Is the finalize ordering specified (`state_=not_locked` BEFORE `draining_complete_=true`, release, no co_await between) so a reentrant drainer observing completion also observes not_locked? [Clarity, data-model state diagram]
- [ ] CHK006 Is the single-winner cancel-vs-reap CAS argued (not asserted "by construction") — both on the owning strand, serialized, loser no-ops, winner schedules exactly one counted resume? [Measurability, research W-1 / FR-004]
- [ ] CHK007 Is the always-post (E-3) resumption discipline preserved and stated as a requirement (no inline/synchronous resume)? [Completeness, Spec §FR-005]
- [ ] CHK008 Is the removal of `active_acquirers_count_` justified with the synchronous-initiation interleaving argument (so it is not silently reopening the 006/047 lost-wake window)? [Coverage, research W-3b]
- [ ] CHK009 Is the holder-yield boundedness honestly bounded by a DRAIN PRECONDITION (holders release promptly), with the two callers' satisfaction verified per-callsite — NOT claimed as an O(holder-count) bound? [Clarity, research §DRAIN PRECONDITION / FR-001]

## OOM fail-closed (requirements)

- [ ] CHK010 Is the `inherited_slot.assign` fail-closed recovery specified as a NORMATIVE posted ref-balance (not prose), distinct from the `store_executor` exit, going through the awaiter/stored-handler? [Clarity, research D-3 / FR-003]
- [ ] CHK011 Is the exact ref-balance arithmetic stated (creator + attached + scheduled-resumer → 0, freed once) so a reviewer can check it? [Measurability, research D-3]
- [ ] CHK012 Is the `reaper_slot.assign` elimination stated as a consequence of removing the reaper park (not a separate fix)? [Completeness, research D-3]
- [ ] CHK013 Is the DEFERRED OOM residual (L-048) honest — resume-post pre-existing; holder-yield post NEW but replacing the shipped channel `async_wait` (same class) — and NOT claimed fail-closed? [Consistency, Spec §US2 / contract INV-C]
- [ ] CHK014 Is it specified that 048 introduces NO new error variant (`sync_lock_alloc_failed=44` already exists) and reaps with the SHIPPED `sync_lock_aborted` (not `sync_lock_drained`)? [Consistency, data-model §error / FR-007]

## Contract narrowing (requirements)

- [ ] CHK015 Does the contract narrow ONLY `cancel_and_drain()` overlap, with ordinary cross-thread `async_lock`/`unlock` explicitly PRESERVED (2f §1.1 contention seam)? [Clarity, contract §cancel_and_drain / FR-006]
- [ ] CHK016 Is the reachability proof stated PER INSTANCE PER OPERATION — exactly 2 drain consumers (strand-confined) vs 4 lock consumers (stores never drain)? [Completeness, research D-1 / FR-007]
- [ ] CHK017 Is the unsupported (INV-2 direct_executor) path documented as UNDEFINED, and is the absence of a production assertion seam acknowledged (test-only instrumented executor)? [Coverage, contract §Unsupported / FR-006]
- [ ] CHK018 Is the "drain is uninterruptible" contract change (abort path removed; no `sync_lock_aborted` return from drain) stated, and confirmed no supported caller depends on cancelling a drain? [Completeness, research D-2.2 / contract §6]

## ABI / layout (requirements)

- [ ] CHK019 Is the ABI claim precise — C ABI FROZEN (abidiff-clean, no enum change) but C++ `sizeof(async_mutex)` CHANGES — with NO surviving "no ABI change" wording? [Consistency, plan Constitution Check / FR-008]
- [ ] CHK020 Is the layout-change impact specified (embedded by value in seqnum_manager/memory_store/file_store → header-recompile) and the sizeof/alignof layout golden mandated as a witness? [Completeness, data-model §ABI / FR-008]
- [ ] CHK021 Is the FR-009 4→3 `atomic<shared_ptr>` consumer claim backed by the four named anchors, and flagged as a cross-feature (046) re-confirm? [Traceability, Spec §FR-009 / research New P2-N1]

## Test discipline (requirements)

- [ ] CHK022 Is the no-enshrine rule explicit — the cross-thread witness is REPURPOSED to assert unsupported use is rejected, NOT barriered-to-green to pass? [Coverage, quickstart / `[[feedback_coverage_push_enshrines_bugs]]`]
- [ ] CHK023 Is the member-removal census specified as grep-exhaustive (every `active_acquirers_count_`/`drain_latch_ptr_`/`drain_latch_state` occurrence), not a fixed list? [Completeness, tasks T003 / census discipline]
- [ ] CHK024 Is the discrimination basis stated as STRUCTURAL for removed machinery + BEHAVIORAL for the new contract (not "reverting the reap fails a supported-topology behavioral test")? [Clarity, Spec Independent Test / P2-6]
- [ ] CHK025 Are the three NEW RED witnesses named with their discriminating condition (immediate-destroy-after-reap, reentrant-during-active-drain, on-strand-cancel-during-reap)? [Coverage, quickstart / tasks T010-T012]
- [ ] CHK026 Is the SC-001 stress threshold measurable (≥200 rounds × ≥25 reps, both deadlines, zero hangs)? [Measurability, Spec §SC-001]
- [ ] CHK027 Is the SC-002 OOM witness scope measurable (injected failure at inherited_slot.assign; 0 process terminations; unrelated sessions operational)? [Measurability, Spec §SC-002]
- [ ] CHK028 Is the SC-003 perf gate quantified with a baseline (vs committed main, ±5% per §VIII.2) and assigned a task? [Measurability, Spec §SC-003 / tasks T026]

## Dependencies & assumptions

- [ ] CHK029 Is the assumption "all production drain consumers + cancel-emit are strand-confined" source-anchored (engine.cpp:1255-1281; session strand) rather than asserted? [Assumption, research D-1]
- [ ] CHK030 Is the design-doc amendment matrix (E-5 + the real §-IDs §1.1/§3.1/§4.1/§4.5/§4.7.3/§4.7.4) complete, with no reference to the phantom 047 E-5/I-33? [Traceability, research D-6 / tasks T006]
- [ ] CHK031 Is the supersede-047 + 046-rebase dependency documented (047/PR #143 closed; 046 consumer set 4→3)? [Dependency, Spec Assumptions]
