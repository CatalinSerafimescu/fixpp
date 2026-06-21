---
description: "Task list — 047 async_mutex cancel_and_drain late-waiter reap (lost-wake fix)"
---

# Tasks: async_mutex cancel_and_drain late-waiter reap (lost-wake fix)

**Input**: Design documents from `specs/047-async-mutex-drain-reap/`
**Prerequisites**: plan.md, spec.md, research.md (the multi-edge proof), data-model.md (I-33, counter model)
**Gate A**: CONVERGED (3 rounds) — binding implementation constraints in
`../../research/reviews/opus_047_gate_a_FINAL_judge.md`. Threading trigger → `/analyze` mandatory after this.

**Tests**: REQUIRED (Article VII §3 TDD). The witnesses are first-class RED→GREEN gates;
each blocker fix has a discriminating, mutation-tested witness (FR-006a / SC-006).

## Format: `[ID] [P?] [Story] Description`
- **[P]**: parallelizable (different file, no dependency on an incomplete task).
- All production code is in the single header `include/fixpp/core/sync/async_mutex.hpp` →
  those tasks are **sequential** (same file, interlocking lock-free edits), NOT [P].

## ⚠️ Coordinated-increment note (read before implementing)

The five fixes interlock and ship as ONE increment — they cannot be delivered as five
isolated MVPs: B-orig's converging loop **deadlocks** without B1 (notify), B1's witness
only manifests *with* the converging loop, B2/B3 add new ordering the loop depends on.
TDD discipline is preserved via **mutation-revert** witnesses (revert one fix → its
witness goes RED; SC-006), not via shipping each fix alone. US1 below is therefore the
core increment; US2/US3 are verification + durability around it.

---

## Phase 1: Setup

- [ ] T001 [P] Move `test_async_mutex_drain_latch_publish_acquire.cpp` from the 046 branch into `tests/sync/` on this branch and register it in `tests/sync/CMakeLists.txt` (046 will drop its copy on rebase).
- [ ] T002 Harden the moved witness per FR-006: real `asio::thread_pool` with **≥4 workers, ≥32 racing acquirers/round, ≥100 rounds/invocation**, an **internal self-deadline** (fast attributable FAIL, never a lane hang), a **fresh mutex per round**, and **orphan-at-teardown safety** (isolated child-process or leak-safe timeout — the mutex dtor rejects a non-empty state). Prefer polling a done-flag over `use_future` for cross-thread joins (removes the libc++ teardown noise of finding 1).
- [ ] T003 [P] Capture the RED baseline harness in `research/findings/` notes: `taskset -c 0,1` release loop reproducing the hang on `main` (per `quickstart.md`); record the pre-fix repro rate.

---

## Phase 2: Foundational (Blocking Prerequisites — shared scaffolding)

**Purpose**: data structures every blocker fix depends on. MUST land before Phase 3.

- [ ] T004 Add private member `std::atomic<std::uint32_t> active_unlockers_count_{0}` to `async_mutex` in `include/fixpp/core/sync/async_mutex.hpp` (the new feeder counter; B3 / edge #2′).
- [ ] T005 Refactor `drain_latch_state` (async_mutex.hpp ~L347-385): replace the two bools `released_`/`aborted_` with one `std::atomic<drain_terminal>{pending}` (`enum class drain_terminal { pending, released, aborted }`). `signal_release()`/`signal_abort()` **CAS from `pending`**, return whether the CAS won, and **close the channel only on a win**. Update `subscribe()` (~L1069-1080) and the idempotent fast paths steps (a)/(b) (~L1083-1103) to read the single terminal state. Preserve F-2 (abort epoch keeps the latch published). (R2-B2 + binding finalize rules.)
- [ ] T006 Add the audited notify helper (free fn or private member) implementing the **seq_cst publication protocol** (R2-B1): `feeder.fetch_sub(1, seq_cst); if (draining_.load(seq_cst)) { if (auto l = drain_latch_ptr_.load(acquire)) l->notify(); }`. All acquirer + unlocker terminal decrements will route through it; the resumption path (L597-599) stays unchanged (captured-latch unconditional notify).

---

## Phase 3: User Story 1 — Concurrent drain never orphans a waiter (P1) 🎯 MVP

**Goal**: `cancel_and_drain()` drives every begun acquirer/holder/unlocker/resumption to
exactly one terminal outcome under genuine multi-threaded concurrency; never hangs,
never double-resumes, never finalizes with the lock held. (FR-001/002/003/008/009.)

**Independent test**: the hardened multi-threaded witness + W-B1..W-B4 all GREEN; each
fix's mutation-revert goes RED.

### Tests first (author RED)

- [ ] T007 [P] [US1] Author `tests/sync/test_async_mutex_drain_reap_blockers.cpp` with **W-B3** (a holder with a residual `W1→W2` chain in `next_drain_head_` unlocks concurrently with a drain; assert W2 is reaped, drain does not finalize early) — RED vs `main`.
- [ ] T008 [P] [US1] Add **W-B4** to the blockers file (cancellation hits the second draining gate concurrently with the drained fast-fail; assert the awaiter's handler is invoked exactly once) — RED vs `main`.
- [ ] T009 [US1] Confirm **W-orig** (the moved hardened witness) is RED vs `main` (self-deadline trips on the orphan) and record it.

### Implementation (sequential — all in `include/fixpp/core/sync/async_mutex.hpp`)

- [ ] T010 [US1] **B3** — `unlock()` (~L947-1057): wrap the whole body in an RAII guard that increments `active_unlockers_count_` at the very top **before** `holders--` (L951) and the `draining_` read, and decrements (**seq_cst**, via the T006 helper) at every return **after** any `push_residual` (L978/L1033)/grant. Recursion-safe (L1004/L1055 are not returns — never `unlockers--; unlock(); return`). Make the L953 `draining_` load **seq_cst** (edge #2′).
- [ ] T011 [US1] **B4** — `async_lock()` second draining gate (~L868-875): replace the unconditional `phase_.store(cancelled)` with a `phase_.compare_exchange(queued→cancelled)`; **set result + schedule only on CAS win**; run **common cleanup (acquirer dec via helper + creator-ref release) for BOTH outcomes**.
- [ ] T012 [US1] **B-orig + B2** — `cancel_and_drain()` (~L1142-1233): replace the linear (g)→(h)→finalize with the **converging reap+quiesce loop** — drain both lists, read feeders (`acquirers`/`unlockers` **seq_cst**, `resumptions` acquire) **before** the sink (`holders` acquire), wait on any nonzero, then a confirming list-exchange; finalize only when all feeders 0 ∧ holders 0 ∧ confirming scan empty in one pass. `draining_.store(true)` → **seq_cst** (L1110). Finalize uses the `signal_release()` CAS-win result (clear ptr + success only on win; else keep latch + return aborted).
- [ ] T013 [US1] **edge #2 + B1 (acquire side)** — `async_lock()`: make the entry `active_acquirers_count_.fetch_add` (L776) and **both** `draining_` loads (L780, L868) **seq_cst**; route **all** acquirer terminal decrements (L781/793/835/850/869/887/907) through the T006 notify helper (seq_cst).
- [ ] T014 [US1] **B1 (unlock side)** — ensure every `unlock()` terminal decrement (including the no-grant draining branch and the grant/normal paths) notifies via the T006 helper (the old L958 notify is not a substitute for the new unlocker decrement).

### Tests after (author + verify GREEN, then mutation-revert RED)

- [ ] T015 [P] [US1] Add **W-B1** (reaper parks while exactly one acquirer is between increment and fast-fail decrement; assert drain completes — RED on a converging-loop-without-notify) and **W-B2** (an acquirer wins the fast-path CAS inside the reaper's quiesce window; assert `cancel_and_drain` never reports success with the lock held) to the blockers file. Verify all five witnesses (W-orig, W-B1..W-B4) GREEN.
- [ ] T016 [US1] **Mutation-revert matrix (SC-005/SC-006)**: individually revert each of {B-orig converging scan, B1 notify, B2 read order, B3 unlocker bracket, B4 CAS} → confirm the matching witness goes RED (≥99/100 pinned-release for W-orig) → restore. Record results in `.specify/decisions/047-async-mutex-drain-reap-verify.md`.

**Checkpoint**: US1 complete = the lost-wake/UAF set is fixed and independently witnessed.

---

## Phase 4: User Story 2 — No regression to existing drain semantics & invariants (P2)

**Goal**: every existing `async_mutex` behavior identical; serialized path a functional
no-op. (FR-004/005.)

- [ ] T017 [US2] Run the full existing 006 suite (`tests/sync/test_cancel_and_drain.cpp` + the whole sync suite) GREEN across **debug, release, ASan, UBSan, TSan (libstdc++ — NOT libc++, finding 1), gcc-release**, ONE preset at a time (WSL2 -j2 cap).
- [ ] T018 [US2] Confirm FIFO-fair grant order, the uncontended fast path (no new suspension/alloc), idempotent/concurrent + reentrant-drain (F-2), reaper own-cancellation (F-3/I-5), and `noexcept` are unchanged; I-1..I-32 (incl. I-32 reclamation under the amended soundness note) preserved.

---

## Phase 5: User Story 3 — Permanent multi-threaded regression gate (P3)

**Goal**: a continuously-run witness that catches this lost-wake class again. (US3.)

- [ ] T019 [US3] Confirm the hardened witness + blockers file are registered in the standard CI sync lanes and pass reliably (no hang/flake) on repeated runs under each preset.
- [ ] T020 [US3] Update the 006 design doc `.specify/2f-async-mutex.md`: append **I-33** (drain convergence), the **amended I-32 single-walker soundness note**, the `active_unlockers_count_` Dekker handshake (edge #2′), and the single-atomic `drain_terminal` arbitration, so the proof is versioned alongside I-1..I-32.

---

## Phase 6: Polish & Cross-Cutting

- [ ] T021 [P] Coverage (Article IX §1, lcov DA/BRDA): the converging loop's new branches (confirming-empty vs late-waiter; each feeder-wait arm; the B4 CAS-win/loss arms; the terminal-state CAS-win/loss arms) hit by a witness or carry a recorded Opus waiver in `.specify/decisions/047-async-mutex-drain-reap-verify.md`.
- [ ] T022 [P] Perf (FR-005 / Article VIII §2): confirm the uncontended `async_lock` fast path stays within ±5% (bench or asm diff on a supported arch) despite the seq_cst entry increment + both `draining_` loads.
- [ ] T023 [P] Static analysis: clang-tidy / clang-format / cppcheck / IWYU clean on `async_mutex.hpp` + the new/moved witnesses.
- [ ] T024 Completeness audit (tasks ↔ FR-001..009 / SC-001..006 ↔ catalogue, 100% or waived — `/gate-b` precondition) + update `spec/feature-catalogue.md` + `spec/coverage-index.md` + `spec/behaviors-and-limitations.md` (any B-047/L-047 rows; note the latent-defect-now-fixed disposition).
- [ ] T025 `codegraph sync` (submodule cwd) after the code change so the index reflects the new member + reaper structure.

---

## Dependencies & order

- **Phase 1 → Phase 2 → Phase 3** strictly (scaffolding before the interlocking fix).
- Within Phase 3: tests T007-T009 first; then T010→T011→T012→T013→T014 **sequential** (one header, interlocking); then T015-T016.
- **Phase 4 & 5** after Phase 3 (verification + durability). **Phase 6** last.
- T020 (design doc) and T024 (catalogue) are independent of each other.

## Parallel opportunities

- T001 ∥ T003 (Setup).
- T007 ∥ T008 (witness authoring, distinct test cases) — both before the implementation tasks.
- T021 ∥ T022 ∥ T023 (Polish, distinct artifacts).

## MVP scope

**US1 (Phase 1+2+3)** is the MVP — it fixes and witnesses the lost-wake/UAF set. US2
(no-regression) and US3 (permanent gate + design doc) harden and durably guard it.
