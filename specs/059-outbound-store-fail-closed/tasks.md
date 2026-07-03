---
description: "Task list — 059-outbound-store-fail-closed"
---

# Tasks: Outbound store-failure disposition — fail-closed on a persistent store

**Input**: Design documents from `specs/059-outbound-store-fail-closed/`
**Prerequisites**: plan.md, spec.md (US1/US2/US3), research.md (D1–D7), data-model.md (INV-059-1..6), contracts/store-then-emit-disposition.md, quickstart.md (W1/W2/W3)

**Tests**: REQUIRED — Article VII §3 (TDD mandatory) + the spec's discriminating witnesses (SC-001..006). The W1 cascade witness is proven **RED on the pre-fix tree** (seam present, disposition fix absent) before the fix makes it GREEN.

**Gate-A carry-ins (must be addressed here):** 2 non-blocking P3s (stale `data-model.md:9` `hydrate()` row; the reconcile-read-inside-`try` throw arm) + 2 hard-to-witness coverage arms + a `Session::send` guard app-veto witness. All folded into the tasks below.

**Scope:** S-P1-1 + S-P2-1 only. S-P2-2 / S-P3 / other clusters are OUT (separate tracker rows).

## Format: `[ID] [P?] [Story] Description`
- **[P]**: can run in parallel (different files, no incomplete-task dependency)
- Paths are repo-relative to the library submodule (`research/G19-fix-fpml-iso20022/library/`).

---

## Phase 1: Setup & grounding

**Purpose**: refresh anchors (line numbers drift) and clear the Gate-A doc P3s before writing code.

- [ ] T001 Re-verify the design's source anchors against the current tree and record any line drift in `research.md` "Grounded facts": `store_then_emit` def + the store-swallow `(void)store_r;` + the store-before-transmit split + the transport-write-failure return (`co_return dispatch_aborted`, no internal `record_state_transition_`); `Session::send` `== dispatch_aborted` guard; `send_impl` app-veto return (`app_do_not_send`/`app_payload_malformed`) pre-store; `record_state_transition_` onLogout fire; the durable-reset gate on `cfg_.reset_on_logon`; `store_cancelled` return sites — all in `src/session/session.cpp` / `include/fixpp/session/{memory_store.hpp,file_store.cpp,session_config.hpp,seqnum_manager.hpp}`.
- [ ] T002 [P] Gate-A P3 cleanup — **the `data-model.md:9` stale `hydrate()` row was already fixed during `/speckit-analyze` triage** (now reads "reconciled … via `set_next_outbound(durable_k)` … NOT `hydrate()`"). Remaining scope: grep the whole bundle (`hydrate\|re-hydrat\|rehydrate`) to confirm no other file re-introduces the rejected framing, and keep the "Touched?" wording consistent with the `hydrated_` "NOT touched" row directly below.

---

## Phase 2: Foundational (BLOCKING) — FileStore fault-injection seam

**Purpose**: the durable-path witnesses (W1, W3) cannot exist without a way to force one `store()` pwrite to fail. Test-only infra; must land before US1/US3 tests. **⚠️ No US work begins until this phase is complete.**

- [ ] T003 Add a `FIXPP_TEST_HOOKS`-gated `store()`-pwrite fault-injection seam in `src/session/file_store.cpp`: an `std::atomic<bool>` "fail next store pwrite once" arm forcing one `raw_pwrite_all` in `store()` to return `false` → `io_ok=false` → `store_io_failure`. Mirror the existing seam idiom (`g_force_abort_after_reset_lambda` / `g_post_rename_reopen_fail_hook`): counter/arm compiled UNCONDITIONALLY, declaration gated in `include/fixpp/session/file_store.hpp` behind `FIXPP_TEST_HOOKS` so production callers cannot reach it. Include a same-name `#ifdef` twin if any part is OS-specific (`feedback_crossplatform_test_same_name_ifdef`).
- [ ] T004 Add a `FIXPP_TEST_HOOKS` probe counter (read-and-reset) that increments when the injected store-pwrite failure fires, so tests can assert the seam actually fired (V5 RED-for-the-right-reason; `feedback_fail_placeholder_red_test`). Declaration gated in `file_store.hpp`.

**Checkpoint**: durable fault injection available + witnessable.

---

## Phase 3: User Story 1 — persistent fail-closed + no silent freeze (Priority: P1) 🎯 MVP

**Goal**: one durable retain failure fails the session closed (Disconnected, un-retained frame NOT transmitted); the silent-freeze cascade + restart-desync become unreachable.

**Independent Test**: W1 (quickstart) — inject one FileStore `store_io_failure`, continue sending, peer `ResendRequest[k,…]`; RED on pre-fix (post-k loss + stuck durable counter + restart-below), GREEN post-fix (fail-closed before transmit; restart durable==peer-last-seen).

### Tests for US1 (write FIRST, prove RED on the pre-fix tree) ⚠️

- [ ] T005 [US1] Write the W1 cascade witness `tests/session/test_store_fail_closed_persistent.cpp` (GoogleTest, `thread_pool(≥2)` harness per `feedback_single_threaded_harness_masks_strand_races`): step 0 assert the seam fired (probe); RED assertions (pre-fix): replay of `k..k+n` folds to `SequenceReset-GapFill` (app msgs lost), durable outbound counter stuck at `k`, simulated restart recovers `next_outbound==k` below peer-last-seen; GREEN assertions (post-fix, currently failing): at the failing send the session → `Disconnected` and frame `k` is NOT transmitted (transport saw nothing for `k`), and restart durable==peer-last-seen. **Run against the current (pre-fix) tree and capture the RED evidence** before T006.
- [ ] T005a [US1] **FR-004 breadth witness (classified by durability, not error code):** the FileStore pwrite seam (T003) produces `store_io_failure` only. Add a discriminating witness that a **persistent** store *also* fails closed on `store_seqnum_out_of_order` AND `store_capacity_exhausted` (a plausible mutation is a fix that special-cases on the wrong error value). Use a **minimal fake persistent store** (`yields_persistent_store()==true`, arm-to-return-a-specific-error) per research.md D6's fallback, or `tests/support/store_double.hpp` — a table-driven/parametrized assertion over the three store-fatal codes suffices (no need for three full FileStore-seam scenarios). Guards FR-004 / SC-006.

### Implementation for US1

- [ ] T006 [US1] The core disposition fix in `src/session/session.cpp` `store_then_emit`: replace `(void)store_r;` with — (1) `store_cancelled` → today's absorb→proceed (cancellation-class, D7); (2) genuine failure (`store_io_failure`/`store_seqnum_out_of_order`/`store_capacity_exhausted`) + `store_is_persistent_` → **capture `err = store_r.error()` FIRST** (NEW-P3), best-effort reconcile (T011), then `co_return std::unexpected(err)` **before** Step 2 transmit, with **no internal `record_state_transition_`** (mirror the existing transport-write-failure return; `feedback_mirror_existing_failclosed_disposition`); (3) genuine failure + volatile → today's `(void)` proceed (FR-003). Preserve the existing `operation_aborted` throw→`dispatch_aborted` catch verbatim (FR-005).
- [ ] T007 [US1] Broaden the one narrow-guard caller in `src/session/session.cpp` `Session::send` (`~:4046`): its `Disconnected` transition currently gates on `== dispatch_aborted`; widen to also fire on the store-fatal class `{store_io_failure, store_seqnum_out_of_order, store_capacity_exhausted}`, while keeping app-veto (`app_do_not_send`/`app_payload_malformed`, returned pre-store) NON-fatal. (The only genuinely-new-in-059 call-site edit; every other caller uses a broad `if(!emit_r)` and needs no change — confirmed by the Gate-A 26-site census.)
- [ ] T008 [US1] Run T005 → GREEN: fail-closed + no-transmit-of-`k` + restart consistency all pass; capture the GREEN evidence. Confirm no pre-existing session/store test regressed.

**Checkpoint**: US1 (the P1 flagship) functional and independently testable.

---

## Phase 4: User Story 2 — volatile store unchanged (Priority: P2)

**Goal**: a volatile-store retain failure behaves byte-for-byte as `main` (logged-then-proceed; no new disconnect). L-008-2 stands for the volatile leg.

**Independent Test**: W2 — bounded `MemoryStore` filled to capacity → next `store()` returns `store_capacity_exhausted` → session stays `Active`, transmit proceeds.

- [ ] T009 [US2] Write `tests/session/test_store_fail_open_volatile.cpp`: bounded `MemoryStore` at outbound capacity (no mock needed); assert the capacity failure does NOT disconnect and behaves identically to `main` (FR-003 / SC-003). Must pass after T006 (volatile arm untouched) and guard against regression. (Optionally use `tests/support/store_double.hpp` to isolate the disposition branch.)

**Checkpoint**: US1 + US2 both independently pass.

---

## Phase 5: User Story 3 — reconcile-from-durable on reconnect (Priority: P2)

**Goal**: after the transient durable fault clears, an in-process reconnect resumes from the durable counter (plain-persistent); reset policies own their post-reconnect state.

**Independent Test**: W3 (three HONEST variants) — plain (clean resume at `k`), `reset_on_logon` (durable reset overrides to 1), `bilateral_strict` DEFAULT (bounded by pre-existing L-029-3 — regression-guard, NOT clean).

### Implementation for US3

- [ ] T010 [US3] Implement the best-effort outbound-only reconcile inside `store_then_emit`'s fatal branch (`src/session/session.cpp`, part of T006's branch): `dk = co_await store_->next_seqnum(outbound, false)`; if `dk`, `co_await seqnum_mgr_.set_next_outbound(*dk)`; reconcile failure is non-fatal (still disconnect). Do **NOT** touch `hydrated_`. (`err` already captured before this read per T006.)

### Tests for US3

- [ ] T011 [US3] Write `tests/session/test_store_fail_reconcile.cpp`: at disconnect assert `peek_outbound()==durable_k` AND inbound counter untouched. **Variant A** plain persistent → in-process reconnect resumes at `k` (clean, no repeating disconnect). **Variant B** `reset_on_logon` → reconnect Logon `34=1`+`141=Y` well-formed (durable reset overrides; reconcile neutral). **Variant C** `bilateral_strict` (default) → reconnect Logon `34=k(>1)`+`141=Y` = pre-existing L-029-3; assert **regression-guard** (059 does not worsen it: reconciled `durable_k` and un-reconciled `k+1` both non-1) — do NOT assert clean recovery.
- [ ] T012 [US3] Acceptor-path coverage: either add an acceptor-side reconcile/reconnect witness (acceptor reset reaches via received-`141=Y`/inbound-Logon, distinct from the initiator `reset_on_logon` path) OR record a sourced exclusion in `research.md` explaining why the acceptor in-process reconnect is covered by the shared `store_then_emit` reconcile.

**Checkpoint**: all three user stories independently pass.

---

## Phase 6: Polish & Cross-Cutting Concerns

- [ ] T013a **Post-implementation census re-stamp (F2):** after T006/T007 land (line numbers shift), re-grep `store_then_emit(` call sites in `src/session/session.cpp`, confirm the count is still 26 with the 9-broad-guard / 1-narrow-guard / 16-swallow classification intact, and refresh the line-number citations in `research.md` (D3/D4 census), `data-model.md` (INV-059-6), and `contracts/store-then-emit-disposition.md` so the recorded anchors match the merged tree.
- [ ] T013 Coverage-design gate (Article IX §1): provide a discriminating witness OR a recorded risk assessment for the two hard-to-witness arms — (a) `store_cancelled`-on-a-persistent-store (disposition row 2, shutdown-drain-only); (b) the reconcile "durable read FAILED → skip reconcile, still fail-closed" else-arm (T010). Also add the `Session::send` guard witness: an app-veto (`app_do_not_send`) must leave the session `Active` (NOT caught by the widened predicate, T007). Record uncovered-branch assessments in the eventual `.specify/decisions/059-outbound-store-fail-closed-verify.md`.
- [ ] T014 Documentation (FR-009): amend `spec/behaviors-and-limitations.md` L-008-2 — the silent-resend-loss limitation now applies ONLY to the volatile MemoryStore; add a row that the **persistent** FileStore leg fails closed. Note the US3 bound under `bilateral_strict` = pre-existing deferred L-029-3 (cross-reference, do NOT fix L-029-3 here).
- [ ] T015 Run `quickstart.md` validation (W1/W2/W3) across the Tier-1 sanitizer matrix (ASan/UBSan/TSan, Linux/Clang; `-DFIXPP_TEST_HOOKS=ON`); confirm conformance corpus TC-001..017 stay green (no session-FSM regression); state the no-hot-path-perf-delta rationale in the verify doc (happy path byte-identical; no bench required).
- [ ] T016 [P] **Catalogue close-out** (Gate-B precondition, Article XVII §8): add the 059 OFFICIAL `session` row to `spec/feature-catalogue.md` (status `done` with PR/evidence ref, tests = the three W-witnesses) AND add the matching `spec/coverage-index.md` entry (touched modules `src/session/session.cpp`, `src/session/file_store.cpp` seam).
- [ ] T017 **Feature-completeness audit (FINAL task)** (hard `/gate-b` precondition, Article XVII §8 / pre-flight 4d): assert against the merged tree that (i) every `tasks.md` row is `[X]` or explicitly waived; (ii) every spec FR-001..009 and SC-001..006 maps to a landed test AND a landed implementation; (iii) the 059 catalogue row is `done` with a matching `coverage-index.md` entry. Record the 100%-or-waived verdict in `.specify/decisions/059-outbound-store-fail-closed-verify.md` `## Completeness` (or a sibling `-completeness.md`).

---

## Dependencies & Execution Order

- **Phase 1 (Setup)**: T001 grounding + T002 doc P3 — no code deps; do first.
- **Phase 2 (Foundational, BLOCKING)**: T003 seam → T004 probe. **Blocks US1 + US3 tests.**
- **Phase 3 (US1, P1 — MVP)**: T005 (RED, needs T003/T004) → T006 core fix → T007 Session::send guard → T008 GREEN. T006 is the linchpin; T007 depends on T006's error codes.
- **Phase 4 (US2, P2)**: T009 — depends on T006 (volatile arm), independent of US3.
- **Phase 5 (US3, P2)**: T010 reconcile (part of T006's branch) → T011 tests (needs T003) → T012 acceptor. Depends on T006.
- **Phase 6 (Polish)**: T013–T015 after US1–US3; T016 then T017 LAST.

### Within-story order
Tests written & RED before implementation (T005 before T006/T008; T011 after T010 impl but the RED cascade T005 gates the fix). Core fix (T006) before the dependent guard (T007) and reconcile (T010).

### Parallel opportunities
- T002 [P] (docs) parallel with T001.
- T016 [P] (catalogue) parallel with T013/T014 once impl is landed.
- US2 (T009) and US3 (T010–T012) can proceed in parallel after T006, but both depend on T006.

---

## Implementation Strategy

**MVP = US1** (the only true P1). Land Phase 1 → Phase 2 (seam) → Phase 3 (RED T005 → fix T006/T007 → GREEN T008) and STOP to validate the flagship data-integrity fix independently. Then US2 (regression pin), US3 (recovery + honest policy scoping), then Polish + the two mandatory close-out tasks.

**Per Article XV §1 / build caps:** the fix is a cold-path disposition (happy path byte-identical, no new alloc/lock); build parallelism max `-j2` (`feedback_build_resource_cap_oom`); one task per phase-implementer invocation (`feedback_phase_implementer_sonnet_runaway_scope`).

## Notes
- Sanitizer findings are real-until-disproven (TSan/ASan/UBSan) — the store path is `async_mutex`-guarded + strand-confined; use `thread_pool(≥2)`.
- Prove the W1 RED for the CASCADE reason (assert the seam fired), not a miswired seam.
- Do not remove run-tier labels post-merge; squash-merge auto-runs the tiers.
