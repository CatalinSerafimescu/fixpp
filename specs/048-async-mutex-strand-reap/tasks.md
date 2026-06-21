# Tasks: async_mutex strand-local drain-reap simplification (048)

**Feature**: `048-async-mutex-strand-reap` | **Branch**: `048-async-mutex-strand-reap`
**Inputs**: plan.md, spec.md, research.md, data-model.md, contracts/async_mutex-contract.md, quickstart.md
**Tests**: REQUIRED (concurrency-contract change; [const §VII.3] + spec Independent Tests / SC witnesses).
**Primary file**: `include/fixpp/core/sync/async_mutex.hpp` (header-mostly). Tests under `tests/sync/`.

**Build caps**: -j2; sanitizer presets ONE AT A TIME (WSL2 ~11 GB OOM). The implementer's local
debug+ASan+TSan-libstdc++ self-run on the sync suite IS the per-phase gate.

---

## Phase 1: Setup & grounding

- [ ] T001 Re-verify the source anchors the bundle cites still hold (read, do not edit): `include/fixpp/core/sync/async_mutex.hpp` — the reaper `cancel_and_drain` (~1061-1233), `drain_latch_state` (~352-388), `drain_latch_ptr_` (:249), `active_acquirers_count_` (:244), `in_flight_resumptions_` wait (~1188-1196), resume runner + E-3 post (~588-615), `release_ref` (~670-690), `on_cancel` (~731-744), `inherited_slot.assign` (:862) + `store_executor` exit (:849-857), reap result `sync_lock_aborted` (:1129-1130); and `include/fixpp/core/error.hpp` `sync_lock_alloc_failed=44`/`sync_lock_drained=46`. Record any line drift in a note; the design is anchored to these.
- [ ] T002 Capture the pre-change `sizeof(async_mutex)`/`alignof(async_mutex)` baseline (a throwaway `static_assert`/`std::cout` probe under `linux-clang-debug`) for the layout-golden re-baseline (FR-008).

## Phase 2: Foundational (member-set change — blocks all stories)

- [ ] T003 In `include/fixpp/core/sync/async_mutex.hpp` remove the cross-thread members: `drain_latch_ptr_` (:249) and the entire nested `detail::drain_latch_state` type (~352-388, its `concurrent_channel`/`notify`/`signal_release`/`signal_abort`/`async_wait`/`released_`/`aborted_`/`in_flight_resumptions_`), and the now-vestigial `active_acquirers_count_` (:244) + all its inc/dec sites (~:776/781/835/850/869/907/1190). (data-model.md member table.)
- [ ] T004 In the same header ADD the strand-local members `std::atomic<std::uint32_t> in_flight_resumers_{0}` and `std::atomic<bool> draining_complete_{false}`; demote `active_holders_count_` + `draining_` + `drain_in_progress_` to plain/relaxed-or-release-acquire per data-model.md. Keep `state_`/`next_drain_head_`/`waiter_record`/`resume_fn_`/`on_cancel`/`phase_` CAS unchanged.
- [ ] T005 Wire `in_flight_resumers_` as the SOLE-owner counter: increment INSIDE `schedule_record_resume()` before the `asio::post` (every path — reap/grant/drained-bypass/on_cancel); decrement in the resume runner AFTER `release_ref` (research W-3c). No other increment site.
- [ ] T006 Update `.specify/2f-async-mutex.md`: add **Erratum E-5 — strand-local reap** (next after E-4; the merged doc has E-1..E-4 only) + the amendment matrix touching §1.1 (size budget: drop drain_latch_ptr_/drain_latch_state/active_acquirers_count_, add in_flight_resumers_/draining_complete_), §3.1 (drain_latch_state row), §4.1 (member set), §4.5 (cancellation-result table — drain now uninterruptible), §4.7.3 (drain invariants I-1..I-8 → strand-local single-pass), §4.7.4 (consumer discipline). (research D-6 / amendment matrix.)

## Phase 3: User Story 1 — reliable strand-local drain (P1)

**Goal**: the synchronous single-pass reap + unified quiescence loop; no orphan, no UAF, no false-success.
**Independent test**: SC-001 standalone stress ≥200 rounds × ≥25 reps, zero hangs; + the 3 new RED witnesses.

- [ ] T007 [US1] Rewrite `cancel_and_drain()` (`async_mutex.hpp` ~1061-1233) to the unified quiescence loop: `co_await reset_cancellation_state(disable_cancellation)`; idempotent reentrant path `while(!draining_complete_) co_await asio::post(...)` then return terminal; else `draining_=true`; `for(;;){ reap_both_lists(); if(active_holders_count_==0 && in_flight_resumers_==0 && both_lists_empty_this_pass) break; co_await asio::post(executor, use_awaitable); }`; finalize `state_=not_locked` THEN `draining_complete_=true` (release, ordered after). Remove the channel/latch/quiescence-park/abort-path/reaper_slot. Reaped waiters keep `result_=unexpected{sync_lock_aborted}`. (research D-2; contract §cancel_and_drain.)
- [ ] T008 [US1] Confirm `unlock()`'s draining branch and the residual-FIFO splice interoperate with the new loop (the loop re-reaps spliced residuals each pass); no behavior change for new acquirers (`sync_lock_drained` at the `draining_` gate :780/:868).
- [ ] T009 [P] [US1] Witness `tests/sync/test_drain_strand_local_reap.cpp` — N waiters parked on ONE strand → `cancel_and_drain()` reaps all `sync_lock_aborted` exactly once, mutex `not_locked`, zero hangs; SC-001 stress harness (≥200 rounds × ≥25 reps, self-deadline). Structural-discrimination note (P2-6): the supported-topology behavior matches shipped; the witness proves the NEW machinery, not a revert-fails-behaviorally claim.
- [ ] T010 [P] [US1] Witness `tests/sync/test_drain_immediate_destroy_after_reap.cpp` (P1-1) — reap N waiters; destroy the mutex immediately on `cancel_and_drain()` return; assert NO resumer outstanding (`in_flight_resumers_==0` held before return) → no UAF; ASan clean. Mutation: reverting the loop to holder-only must RED this.
- [ ] T011 [P] [US1] Witness `tests/sync/test_drain_reentrant_during_active.cpp` (P1-2) — a second `cancel_and_drain()` on the strand while the first is suspended → awaits `draining_complete_`, returns the terminal result; no false-success, no early-destroy.
- [ ] T012 [P] [US1] Witness `tests/sync/test_drain_onstrand_cancel_during_reap.cpp` (P1-N2/FR-004) — a parked waiter cancelled (on the strand) during the reap → resolved exactly once (single-winner CAS); ASan+TSan-libstdc++ clean.
- [ ] T013 [P] [US1] Witness `tests/sync/test_drain_predrain_holder.cpp` — a holder suspended on the strand at drain time → loop yields, holder `unlock()` runs, drain finalizes; no hang, no orphan of a holder-spliced waiter.

## Phase 4: User Story 2 — OOM fail-closed at inherited_slot.assign (P2)

**Goal**: `inherited_slot.assign` fails closed instead of `std::terminate()`; `reaper_slot` gone; resume/yield-post deferred L-048.
**Independent test**: SC-002 injected alloc failure at `inherited_slot.assign` → caller gets `sync_lock_alloc_failed`, process does NOT terminate.

- [ ] T014 [US2] In `async_lock()` wrap `inherited_slot.assign` (:862) with the normative POSTED catch (research D-3): on throw `record->result_=unexpected{sync_lock_alloc_failed}; phase_=cancelled; schedule_record_resume(record); release_ref(record)/*creator*/; return;` — NOT a copy of the `:849-857` store_executor exit, NOT an immediate invoke_handler (E-3). Verify ref-balance (creator+attached+scheduled → 0).
- [ ] T015 [P] [US2] Witness `tests/sync/test_async_lock_inherited_slot_oom_failclosed.cpp` (SC-002) — inject an allocation failure at the `inherited_slot.assign` boundary (a throwing slot allocator / cancellation-slot exhaustion) → caller observes `sync_lock_alloc_failed`, process does NOT terminate; a second unrelated mutex on another session stays operational. RED on the pre-fix terminate.
- [ ] T016 [US2] Record **L-048** in `spec/behaviors-and-limitations.md`: the resume `asio::post` (pre-existing OOM-terminate) + the drain holder-yield post (replaces the shipped channel `async_wait`, same class) remain deferred; the non-allocating-completion redesign is out of scope (same treatment as L-047-2).

## Phase 5: User Story 3 — honest contract for unsupported concurrent drain (P3)

**Goal**: documentation-primary strand-serialized-drain contract + a test-only instrumented-executor misuse witness (no production assertion seam).

- [ ] T017 [US3] Land the narrowed contract docs in the header doc-comment on `cancel_and_drain()` (strand-serialized-drain-only; ordinary cross-thread async_lock/unlock preserved per 2f §1.1; INV-2 escape hatch is UNDEFINED) matching `contracts/async_mutex-contract.md`.
- [ ] T018 [P] [US3] Witness `tests/sync/test_drain_unsupported_overlap_rejected.cpp` — drive genuinely-concurrent drain-overlap via a TEST-ONLY instrumented executor and assert the debug-build assertion fires / the documented-unsupported path is surfaced (NOT barriered-to-green; this is the repurposed 047 cross-thread witness asserting unsupported use is rejected — `[[feedback_coverage_push_enshrines_bugs]]`). Debug-only; `#ifndef NDEBUG`.

## Phase 6: Polish & cross-cutting

- [ ] T019 [P] Retire/rewrite the ~10 cross-thread drain-machinery tests that exercise removed members (`tests/sync/test_drain_latch_holder_lifecycle.cpp`, `test_drain_reaper_abort_subscribers.cpp`, `test_cancel_and_drain_reentrant_after_abort.cpp`, `test_drain_awaitable_cancellation.cpp`, `test_reentrant_drain_uaf_window.cpp`, `test_in_flight_acquirer_coverage.cpp`, `test_cancel_and_drain_concurrent.cpp`, and the abort-path arms of `test_cancel_and_drain.cpp`/`test_residual_cancel_graceful.cpp`/`test_race_cancel_pre_drain.cpp`/`test_unlock_reaper_splice.cpp`). Keep what still applies under the strand-local contract; delete what tests removed machinery; none deleted-to-green without a replacement witness. Update `tests/sync/CMakeLists.txt`.
- [ ] T020 [P] Add the C++ layout golden: a `static_assert` (or a `test_async_mutex_layout_golden.cpp`) pinning `sizeof(async_mutex)`/`alignof(async_mutex)` to the re-baselined post-change value (FR-008); comment the C-ABI-frozen / header-recompile distinction.
- [ ] T021 Update `spec/behaviors-and-limitations.md`: add **B-048** (reliable strand-local drain; the supported contract) + **L-048** (deferred OOM, from T016) + mark **B-047-1 superseded by 048** (the 047 cross-thread-convergence claim is retired — the strand-local design replaces it). Cross-link.
- [ ] T022 Update `spec/feature-catalogue.md` NFR-016 row (append the 048 strand-local-reap amendment; result-code unchanged; no public/C-ABI change; C++ sizeof change) + `spec/coverage-index.md`; assign catalogue row **S-048** if the convention needs one. (Completeness-audit precondition.)
- [ ] T023 [P] Re-run `tools/check_layers.py` + the `tools/check_no_std_mutex_in_awaitable_headers.sh` §XV.9 gate on the edited header (no std::mutex introduced; no new layer edge).
- [ ] T024 Feature-completeness audit (tasks ↔ FR-001..009 ↔ SC-001..005 ↔ catalogue/B&L) — 100% or explicitly waived; this is the `/gate-b` precondition ([const §XVII.8] 4d). Record in the verify decision doc.
- [ ] T025 FR-009 4→3 confirm: after T003, grep the codebase for `std::atomic<std::shared_ptr<` — expect exactly THREE remaining (engine `reader_snapshot_`, transport `cert_source_slot_`, pinset `pin_snapshot`); note for the 046 rebase.

## Dependencies

- Phase 1 → Phase 2 → Phase 3 (US1 is the MVP). Phase 4 (US2) and Phase 5 (US3) depend on Phase 2 but are independent of each other and of US1's witnesses. Phase 6 after the stories.
- T005 (counter wiring) blocks T007 (the loop reads `in_flight_resumers_`). T003/T004 block everything. T019 (retire tests) after T007 lands (so the suite still builds). T020 layout golden after T003/T004 (members final).

## Parallel opportunities

- Witnesses T009–T013 [P] (US1), T015 [P] (US2), T018 [P] (US3), T019/T020/T023 [P] (polish) touch distinct test files — parallelizable once the implementation tasks they test are in.

## Implementation strategy

MVP = Phase 1+2+3 (US1): the strand-local reap + the 5 reliability witnesses green under debug+ASan+TSan-libstdc++. Then US2 (OOM), US3 (contract), Polish. The 6-preset matrix + bench + completeness audit run at `/speckit-verify`.
