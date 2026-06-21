# Concurrency-Correctness Requirements Quality Checklist: async_mutex drain-reap (047)

**Purpose**: Unit-test the *requirements* (spec.md / research.md / data-model.md) for a
lock-free drain-protocol fix — are they complete, unambiguous, consistent, and
measurable enough to implement and review at Gate B? This validates the WRITING of the
requirements, not the code.
**Created**: 2026-06-21
**Feature**: [spec.md](../spec.md) · proof [research.md](../research.md) · [data-model.md](../data-model.md)
**Audience**: Gate B reviewer · **Depth**: formal release gate

## Requirement Completeness

- [ ] CHK001 Are requirements defined for ALL four blockers (B1 notify, B2 snapshot, B3 unlock-privatization, B4 double-resume) as named, testable obligations? [Completeness, Spec §FR-006a, §Clarifications]
- [ ] CHK002 Is a requirement stated that EVERY begun party reaches exactly one terminal outcome for each party class — acquirer, holder, in-flight unlocker, resumption? [Completeness, Spec §FR-001/§FR-008, §Key Entities]
- [ ] CHK003 Is the new quiescence counter (`active_unlockers_count_`) documented as a requirement with its lifecycle (when incremented/decremented) rather than only as an implementation note? [Completeness, data-model §Entities-NEW]
- [ ] CHK004 Is the notify-publication protocol (which decrements must wake the reaper, and how the latch is reliably found) specified as a requirement, not left to the implementer? [Completeness, research §Liveness R2-B1]
- [ ] CHK005 Is the terminal-state arbitration requirement (single winner between release/abort) specified, including the reaper's return value on CAS loss? [Completeness, research §Terminal-flag, data-model §terminal_]
- [ ] CHK006 Is there a requirement covering each invariant the fix must preserve (FIFO order, fast path, idempotent/reentrant drain, F-2, F-3/I-5, noexcept, I-1..I-32)? [Completeness, Spec §FR-004]
- [ ] CHK007 Is a witness required for EACH blocker (W-orig, W-B1..W-B4), or only for the original orphan? [Completeness, Spec §FR-006a/§SC-006]
- [ ] CHK008 Are requirements present for the design-doc update (I-33 + amended I-32 soundness) so the proof is versioned, not only in the spec bundle? [Completeness, tasks T020]

## Requirement Clarity & Measurability

- [ ] CHK009 Are the exact memory orders specified for each strengthened operation (entry `fetch_add`, both `draining_` loads, reaper `draining_.store`, feeder decrements, quiesce loads) rather than a vague "use seq_cst where needed"? [Clarity, research §Exact memory-order changes]
- [ ] CHK010 Is "no orphaned waiter" expressed as an objectively checkable post-condition (e.g. count of completed acquirers equals started; no residual in either list at finalize) rather than a qualitative "never hangs"? [Measurability, Spec §SC-002/§FR-002]
- [ ] CHK011 Is the witness stress budget quantified (workers, acquirers/round, rounds) and the RED bar quantified (≥99/100 mutation-revert) rather than "enough to hit the window"? [Measurability, Spec §FR-006/§SC-005]
- [ ] CHK012 Is "exactly once" (handler invocation under cancel+drain) defined precisely enough to witness (one `invoke_handler` per awaiter)? [Clarity, Spec §FR-009]
- [ ] CHK013 Is the feeder-before-sink read order stated as a precise ordering requirement (which counters, in which order, with which memory order) and not just "read counts coherently"? [Clarity, research §Edge #3, data-model §read-order rule]
- [ ] CHK014 Is the convergence termination condition (I-33) stated as a single checkable predicate (all feeders 0 ∧ holders 0 ∧ both lists empty in one pass) rather than prose? [Measurability, data-model §I-33]

## Requirement Consistency

- [ ] CHK015 Is the invariant numbering consistent across plan/data-model/research (I-1..I-31 unchanged, I-32 amended, I-33 new — no "I-1..I-32 unchanged" anywhere)? [Consistency, plan §Constraints, data-model §Invariants]
- [ ] CHK016 Is the terminal-state model named consistently (`drain_terminal`/`terminal_`, not the old `released_`/`aborted_` bools) in every place the latch is described? [Consistency, data-model §Entities/§state-machine]
- [ ] CHK017 Is the serialized-path claim worded consistently as a *functional* no-op (not "semantic no-op") in spec FR-005, US2, and plan? [Consistency, Spec §FR-005/§US2, plan §Scope-clarification]
- [ ] CHK018 Do spec scope, plan scope, and research agree the fix spans `cancel_and_drain` + `async_lock` + `unlock` (not "reaper-only")? [Consistency, Spec §Clarifications, plan §Scope-clarification]
- [ ] CHK019 Are the feeder/sink counter sets identical across research and data-model (feeders = acquirers/unlockers/resumptions; sink = holders)? [Consistency, research §Counter model, data-model §Counter model]

## Acceptance Criteria Quality

- [ ] CHK020 Does each success criterion (SC-001..006) state a measurable threshold and the environment it is measured in (preset matrix, lcov DA/BRDA)? [Acceptance Criteria, Spec §Success Criteria]
- [ ] CHK021 Is the RED-gate criterion defined so a witness cannot pass merely by missing the timing window (mutation-revert + quantified repro rate)? [Acceptance Criteria, Spec §SC-005, research §Verification]
- [ ] CHK022 Is the no-regression acceptance bound to the full existing 006 suite across the named preset matrix, with the libstdc++-TSan basis explicit? [Acceptance Criteria, Spec §SC-003/§SC-004, tasks T017]

## Scenario & Edge-Case Coverage

- [ ] CHK023 Are requirements defined for a waiter that parks AFTER the reaper's initial scan but while still counted (the original orphan)? [Coverage, Spec §Edge-Cases, §FR-001]
- [ ] CHK024 Are requirements defined for the acquirer→holder transition occurring inside the reaper's quiesce window (B2)? [Coverage, Spec §Edge-Cases]
- [ ] CHK025 Are requirements defined for a `draining_==false` unlock that privatizes a chain and re-pushes a residual concurrently with the drain (B3)? [Coverage, Spec §Edge-Cases, §FR-008]
- [ ] CHK026 Are requirements defined for the recursive `unlock()` re-drive (so the unlocker counter never transiently hits 0 mid-walk)? [Coverage, data-model §active_unlockers_count_ recursion]
- [ ] CHK027 Are requirements defined for the reaper's own cancellation mid-loop AND for the terminal-flag race between finalize and a late cancel? [Coverage/Exception, research §Terminal-flag, Spec §Edge-Cases]
- [ ] CHK028 Are requirements defined for a new acquirer/unlocker arriving after the drain flag is set (fast-fail / no-grant, no park)? [Coverage, Spec §Edge-Cases]
- [ ] CHK029 Is the resumption→re-entrant-acquirer handoff covered as a requirement (relying on synchronous `invoke_handler`), or left as an unstated assumption? [Coverage, research §Edge #3]

## Non-Functional Requirements

- [ ] CHK030 Is the fast-path performance budget quantified (Article VIII §2 ±5%) with a named verification method (bench or asm diff)? [NFR, Spec §FR-005, tasks T022]
- [ ] CHK031 Is the coverage requirement expressed on the binding basis (lcov DA/BRDA on touched module) with an explicit waiver path for unreachable branches? [NFR, tasks T021]
- [ ] CHK032 Is `noexcept` preservation stated as a requirement given the new CAS/notify/loop code? [NFR, Spec §FR-004]
- [ ] CHK033 Is the no-public/ABI-surface-change requirement stated AND given a verification obligation (not only asserted at design time)? [NFR, Spec §FR-007, tasks T023]

## Dependencies & Assumptions

- [ ] CHK034 Is the assumption that the 046 witness is moved into / owned by 047 (and 046 rebases) documented? [Assumption, Spec §Assumptions]
- [ ] CHK035 Is the assumption that TSan must run under libstdc++ (libc++ TSan = false `use_future` races, finding 1) stated as a binding test-environment requirement? [Assumption/Dependency, research §Verification, tasks T017]
- [ ] CHK036 Is the load-bearing ordering assumption — `drain_latch_ptr_` published (release) BEFORE `draining_` (seq_cst) — captured as a requirement the implementation must not reorder? [Assumption, research §Liveness R2-B1]
- [ ] CHK037 Is the "not production-reachable / P2 latent" premise documented so reviewers understand why the merge-blocker is the witness, not a live hang? [Assumption, Spec §Context]

## Ambiguities & Conflicts

- [ ] CHK038 Is the term "feeder decrement that must notify" unambiguous about which sites are exempt (the resumption captured-latch path) vs included (acquirer/unlocker)? [Ambiguity, research §Liveness]
- [ ] CHK039 Does any requirement still describe the change as "reaper-only" or "drain-protocol-only" in a way that conflicts with the async_lock/unlock edits? [Conflict, Spec §FR-007 vs §Clarifications]
- [ ] CHK040 Is there any residual conflict between FR-006 (a witness) and FR-006a (per-blocker witnesses) about how many witnesses are mandatory? [Conflict, Spec §FR-006/§FR-006a]

## Notes
- Check items off as the requirements are confirmed adequate: `[x]`.
- This list is the Gate B reviewer's lens on *requirement quality*; the
  `requirements.md` checklist covers spec-template completeness. The
  `/speckit-checklist-audit` (step 9) dispositions each item before `/implement`.
