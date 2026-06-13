---
description: "Task list — 035-filestore-io-offload"
---

# Tasks: Real file_io_executor offload for FileStore

**Input**: Design documents from `specs/035-filestore-io-offload/`
**Prerequisites**: plan.md, spec.md (US1/US2/US3/US4, FR-001..010, SC-001..007), research.md (Decisions 1–7), data-model.md (`generation_` field, offload helper, per-method execution map §3, generation-guard §4, error-freeze §5), contracts/file-offload.md (C1–C6)

**Tests**: INCLUDED — this project is RED-first TDD (Article VII). Each behavioral task lands its witness RED before the production change makes it GREEN. **TSan is the primary gate** here (the inert offload made the concurrency surface dormant — plan Constitution Check §IX.1).

**Gate A**: converged close-out (2026-06-13, user-signed-off). No residual P1/P2 (Codex confirm pass P1=0/P2=0).

## Format: `[ID] [P?] [Story] Description`
- **[P]** = parallelizable (distinct file, no incomplete dep).
- Story labels on user-story phases only.

## Path conventions (from plan.md Project Structure)
- **All production execution change is in one file**: `src/session/file_store.cpp` (4 offload sites + `retrieve` post removal + `FileStoreImpl` `generation_` field + reset bump + retrieve re-check). Same-file edits are **sequential** (never `[P]` with each other).
- Error-set freeze guard: `include/fixpp/core/error.hpp`
- Teardown drain confirm: `src/session/engine.cpp` (`stop()` `1184–1333`)
- NEW tests: `tests/session/test_file_store_offload_thread.cpp`, `tests/session/test_file_store_cancellation.cpp`, `tests/session/test_file_store_concurrent_tsan.cpp` (+ `tests/session/CMakeLists.txt`)
- EXTEND test: `tests/session/test_store_shutdown_ordering.cpp`
- Docs: `include/fixpp/session/message_store.hpp`, `specs/008-message-store/{plan.md,research.md,contracts/file_store.hpp}`, `spec/behaviors-and-limitations.md`, `spec/feature-catalogue.md`, `spec/coverage-index.md`

---

## Phase 1 — Setup

- [X] T001 Create the three NEW test files (`tests/session/test_file_store_offload_thread.cpp`, `test_file_store_cancellation.cpp`, `test_file_store_concurrent_tsan.cpp`) as skeletons (includes + a real `asio::thread_pool` `file_io_executor` fixture mirroring `test_file_store_crash_survival.cpp` / `test_store_shutdown_ordering.cpp`) and register their targets in `tests/session/CMakeLists.txt`. Add the `test_file_store_concurrent_tsan` target to the TSan/ASan preset set (it is the primary gate). No TLS fixture needed (FileStore temp-dir only).

---

## Phase 2 — Foundational (the offload helper + the generation field + the error freeze — blocks all stories)

- [X] T002 [P] Add the `generation_` field to `FileStoreImpl` (`src/session/file_store.cpp:~405–443`): `std::uint64_t generation_{0};` — plain (mutated strand-only, no atomic per Decision 3). Structural only; no behavior yet. (data-model §1)
- [X] T003 Add the file-local offload helper to `file_store.cpp` (no exported surface): `co_await asio::co_spawn(impl_->cfg.file_io_executor, [POD-by-value syscall args]() -> awaitable<R> { /* raw syscall */ }, asio::use_awaitable)` — the **final no-reaper shape** (terminal-only cancellation by design; MUST NOT bolt on a slot/flag reaper — Decision 5 / data-model §2). The lambda captures **only** raw syscall args (the `OsFile` fd/handle, offset, `std::span` into an already-populated buffer); it touches **no** `impl_` field. Not yet wired into any method. (Decision 2, contracts C1; idiom precedent `asio_tls_transport.hpp:45–51`; `[[feedback_asio_post_resume_bounces_to_spawn_executor]]`)
- [X] T004 [P] Add a compile-time freeze guard in `include/fixpp/core/error.hpp` (`:165–230`) asserting the `store_*` variant set stays frozen per FR-021 / data-model §5 (no new variant — the mid-walk-reset clean-failure reuses the existing `store_io_failure`). A `static_assert` on the enumerator count / a `case`-exhaustive guard, no new C-ABI slot. (plan §X; data-model §5 — PINNED)

**Checkpoint**: helper + field + freeze guard exist; no method is offloaded yet; the build is green and behavior is unchanged.

---

## Phase 3 — User Story 1 (P1, MVP): the session strand does not block on durable disk writes

**Goal**: `store()`'s `pwrite`/`fdatasync` runs on the `file_io_executor`, the strand stays free, durability is preserved.
**Independent test**: drive `store()` under `commit_per_message` on a real pool; a thread-id probe inside the syscall window shows a pool thread ≠ strand thread, and strand-side work makes progress before `fdatasync` returns.

- [X] T005 [P] [US1] RED thread-id witnesses in `tests/session/test_file_store_offload_thread.cpp` (Recipe A + A2): `Store_Syscall_OnPoolThread_NotStrand` (4-thread pool; probe captured inside the syscall window → syscall thread ≠ `g_strand_tid`, SC-001), `Store_StrandProgresses_BeforeFdatasyncReturns` (a unit posted to the strand runs before the in-flight `fdatasync` returns, SC-002), `Store_SingleThreadPool_OffloadGenuine_NoDeadlock` (1-thread pool distinct from the strand → syscall off-strand AND no deadlock — spec Edge Case), and `Store_SaturatedPool_Suspends_NotBlocks` (a single-thread pool whose one thread is latched on a blocking task → the `store()` awaitable **suspends** without blocking the strand thread; release the latch → it completes — the FR-008 backpressure/suspend-on-saturation witness, structurally provided by `co_await co_spawn(use_awaitable)`). RED because shipped `store()` runs the syscall on the strand. (C1 witness; quickstart Recipe A/A2; FR-008 + the "queue saturates"/"pool not yet running" spec Edge Cases)
- [X] T006 [US1] Wire `store()` (`file_store.cpp:824`): replace the inert `co_await asio::post(impl_->cfg.file_io_executor, use_awaitable)` (+ its paired resume posts at `:830/:843/:851/:859/:869`) with the T003 nested-`co_spawn` over the raw `write_frame` `pwrite`(+pad) + counter-record `pwrite` + `datasync`. Keep the mutex held by the outer coroutine throughout; keep **all** `impl_` mutation (`store_scratch_` deep-copy, counter incr, `write_pos`) on the strand before/after the `co_await` (data-model §3); retain the load-bearing pump-break post at `:787` (keeps method entry on the strand before mutex-acquire — Decision 6). → GREEN T005. (FR-001, C1)
- [X] T007 [US1] Durable-before-send regression: confirm `store()` completes success only **after** `datasync` returns (`:850` linearisation unchanged) and that the existing crash-survival seam (`test_file_store_crash_survival.cpp`, 008 SC-002 — N stores then SIGKILL → all N recovered, 0% loss) passes **unchanged** against the offloaded `store()`. Add an assertion (or confirm coverage) that the offload moved *where* the flush runs, not *whether* it precedes completion. (FR-003 / SC-003 / C2)

**Checkpoint**: US1 is independently demonstrable — the MVP removes the `[const §XV.4]` violation on the send path.

---

## Phase 4 — User Story 2 (P2): every FR-024 offload point is genuine, and the cancellation contract still holds

**Goal**: `next_seqnum(_, true)`, `reset()`, and `flush_for_session_close()` are also genuinely offloaded; the binary §6.1.4 per-method cancellation contract is preserved.

- [X] T008 [P] [US2] RED per-method thread-id witnesses in `tests/session/test_file_store_offload_thread.cpp`: `NextSeqnum_Syscall_OnPoolThread`, `Reset_Syscall_OnPoolThread`, `FlushForSessionClose_Syscall_OnPoolThread` — each asserts the blocking syscall runs on a pool thread ≠ strand and the completion rebinds to the strand. RED against the shipped inert/synchronous paths. (FR-002, C1)
- [X] T009 [P] [US2] RED FileStore cancellation contract in `tests/session/test_file_store_cancellation.cpp` (the FileStore twin of the MemoryStore-only `test_store_cancellation_contract.cpp:44–52`), Recipe C, for each of `store` / `next_seqnum(_, true)` / `reset`: (a) fire the slot **at/before the `async_mutex` acquire** (no `co_spawn` issued) → `store_cancelled` + **0** state change + no syscall; (b) fire **mid-syscall** (deterministic in-syscall hook) → normal completion, **durable**, **NOT** `store_cancelled` (exercises the unconditional `operation_aborted`→durable catch); plus `CoSpawn_TerminalOnly_DoesNotSwallowTotal_NoWedge` (the terminal-only default must not swallow `cancellation_type::total` and wedge while `Engine::stop()` emits `total` — `[[feedback_asio_cospawn_total_cancellation_default]]`). Mutation-proven discriminating. (SC-004 / C3)
- [X] T010 [US2] Wire `next_seqnum(_, true)` (`:1015`) and `reset()` (`:1068`) to the T003 helper: `next_seqnum`'s counter-record `pwrite`(`:1018`) + `datasync`(`:1024`); `reset()`'s tmp-open / `initialise_fresh` / `rename`(`:1108`) / parent-dir `fsync`(`:1120–1136`) [Win `MoveFileExW`] — all against a **local** `OsFile`, never `impl_->file`. Keep `reset()`'s live-handle swap `impl_->file = std::move(new_file)` (`:1149`) and all counter/index mutation **outer on the strand** (data-model §3). Retain the pump-break posts at `:991`/`:1043`; the paired resume posts are removed (the `co_await` completion IS the resume). → GREEN T008 (next_seqnum/reset). (FR-002, C1)
- [X] T011 [US2] Wire `flush_for_session_close()` (`file_store.cpp:1239–1257`, `datasync` at `:1252`) to the T003 helper: offload the blocking `datasync`/`FlushFileBuffers`. **Carve-out**: no writer mutex, **not cancellable** (never surfaces `store_cancelled`), a flush error maps to the existing `store_io_failure` only (contracts C1 flush-Pre / C3 carve-out; 2e §6.2.1:1025). → GREEN T008 (flush). (FR-002)
- [X] T012 [US2] Implement the **unconditional** `operation_aborted`→durable catch at the outer `co_await` for `store`/`next_seqnum`/`reset`: any `operation_aborted` surfaced at the outer await post-dates linearisation (the syscall is non-interruptible and has run to durable completion) → return the durable success, **never** `store_cancelled` for a frame on disk (the `[const §XV.15]`-adjacent silent-loss class). MUST NOT install a cancellation path that swallows `total`. → GREEN T009 mid-syscall sub-case + the no-wedge cell. (FR-004 / C3; `[[feedback_async_mutex_us3_asio_cancel_and_subagent_seams]]`)

**Checkpoint**: all four FR-024 disk operations run off the strand; no `store_*` disk op blocks the session/close strand.

---

## Phase 5 — User Story 3 (P2): concurrent store/retrieve/reset on a real pool is race-free; teardown is UAF-free

**Goal**: under the real multi-thread pool the live concurrency surface is TSan-clean; a mid-walk `reset()` is detected via the `generation_` guard; `Engine::stop()` drains in-flight offloads before return.

- [X] T013 [P] [US3] RED concurrency witness in `tests/session/test_file_store_concurrent_tsan.cpp` (Recipe B, real 4-thread pool, built under TSan+ASan): `ConcurrentStoreRetrieveReset_TSanClean` (overlapping `store()` stream + `retrieve()` walk + `reset()` → **zero** TSan reports on `impl_`/live log handle, no torn read, FR-017 gap detection intact — SC-005); `MidWalkReset_GenerationGuard_FailsClean` (drive a `reset()` while a `retrieve()` walk is suspended at a `visitor.on_frame()` `co_await` → the walk's `generation_` re-check fails cleanly with `store_io_failure`, never reads the swapped/truncated log — I-03); `ResetRace_vs_LogicalGap_Discriminating` (reset-race → `store_io_failure` 56; FR-017 logical gap → `store_seqnum_gap` 57 — so a reset-race cannot masquerade as a gap, data-model §5). (FR-005/FR-006 / C4)
- [X] T014 [P] [US3] RED shutdown-ordering EXTEND in `tests/session/test_store_shutdown_ordering.cpp` (Recipe D, ASan+TSan): queue an in-flight offloaded `store()` **and** drive a graceful close (`Session::close(graceful)` → `flush_for_session_close` under `commit_batched` with buffered frames to drain), then `Engine::stop()` → assert no UAF, no use-of-joined-pool; `stop()` returns only after every **Session-reachable** store-awaiting / close-flush coroutine completes (the app-owned pool is then safe to join). (SC-007 / C5)
- [X] T015 [US3] Implement the mid-walk-reset guard in `file_store.cpp`: `reset()` bumps `generation_++` on the strand at/after the `:1149` handle swap; `retrieve()` snapshots `generation_` under the mutex with the index snapshot (`:908–932`), and **re-checks `generation_ != g0` before each per-frame `pread`** in the walk (`:940–974`) → return `store_io_failure` on mismatch. The re-check and the `pread` are **atomic on the strand** — no `co_await` between them (data-model §4); the only suspension is the `visitor.on_frame()` `co_await` after the read. **Remove** `retrieve()`'s inert `:942` post (the `pread` stays on the strand — Clarifications); retain the `:953` resume. → GREEN T013. (FR-006 / I-03 / data-model §4)
- [X] T016 [US3] Confirm/strengthen `Engine::stop()` (`src/session/engine.cpp:1184–1333`) drains every Session-reachable store-awaiting coroutine (the offload is `use_awaitable`, joined, never detached) before returning, so the app-owned pool join is safe (no UAF). Add the **misuse note** documenting the caller obligation for *direct* (non-Session) `FileStore` use — the `file_io_executor` MUST outlive all outstanding store awaitables (`stop()` cannot drain a direct call outside Session ownership). → GREEN T014. (FR-007 / C5)

**Checkpoint**: the activated concurrency surface is TSan-clean and teardown is UAF-free.

---

## Phase 6 — User Story 4 (P3): MemoryStore and non-FileStore paths byte-for-byte unchanged

**Goal**: nothing observable outside `FileStore`'s threading changes.

- [X] T017 [P] [US4] Regression witness `MemoryStore_And_Surfaces_Unchanged` (run the full existing 008 store suite — round-trip equality, MemoryStore zero-alloc, latency seams) + assert no public `MessageStore`/`retrieve_visitor`/factory signature, no error variant, no `EngineConfig`/`FileStore::Config` field, no wire byte changed; pair with the T004 freeze guard (the `store_*` set is unchanged). `MemoryStore` runs synchronously on the strand, no offload introduced. (SC-006 / FR-009 / C6) — VERIFIED: full store/memory/session suite GREEN under debug (ctest exit 0); FR-009 surface audit shows only the FR-021 static_assert + 3 FIXPP_TEST_HOOKS-gated test-seam decls (no production signature/error/config/wire change).

---

## Phase 7 — Polish & cross-cutting

- [X] T018 [P] Alloc gate (counting_resource + mallocnesia LD_PRELOAD): the `FileStore` offload path is **≤ 1 bounded O(1) coroutine frame/op** (the `[const §XV.1]` v0.2 §XV.4-offload exemption ceiling, `constitution.md:224`); `MemoryStore::store` is **== 0** (unchanged). (plan Constitution Check §XV.1; Decision 1) — BY-CONSTRUCTION + empirical probe path (034 honest precedent): `perf_store_alloc_guard` (mallocnesia LD_PRELOAD) confirms MemoryStore==0 GREEN (2 tests PASSED 2026-06-14); FileStore `store()` offload carries exactly 1 bounded O(1) `co_spawn` frame/op (~48 B PMR-opaque global heap, probe-confirmed), permitted by the `[const §XV.1]` v0.2 §XV.4-offload exemption. A strict mallocnesia==0 gate on the FileStore offload path would false-trip by design (the exemption exists precisely because this frame IS expected). The `FileStore::retrieve()` path (stays on strand, 0 global-heap escapes) IS gated by the existing `Mallocnesia_ZeroGlobalHeapFileStoreRetrieveSteadyState` cell which passes GREEN under mallocnesia LD_PRELOAD.
- [X] T019 [P] Docs (FR-010) — correct the stale offload-claim docs **without history rewrite** (dated notes): `include/fixpp/session/message_store.hpp:95` threading comment; `specs/008-message-store/plan.md:16` (flip the false "[const §XV.4] passes" / "runs on file_io_executor" claim); `specs/008-message-store/research.md:163/170/440` + `contracts/file_store.hpp:14/86` (stale offload-works comments). In `spec/behaviors-and-limitations.md` record the prior latent §XV.4 violation → **mitigated-by-035**. Add the 035 row to `spec/feature-catalogue.md` + `spec/coverage-index.md`. — DONE 2026-06-14: all 5 doc targets corrected with dated blockquote/inline notes; S-013 amended in `feature-catalogue.md`; B-035-1 added to `behaviors-and-limitations.md` (own 035 section); S-013 gap column amended in `coverage-index.md`; quick build `store_memory_round_trip` GREEN confirming `message_store.hpp` comment edit compiles.
- [X] T020 Feature-completeness audit (`tasks ↔ FR-001..010 ↔ SC-001..007 ↔ C1..C6 ↔ catalogue`, 100% or §IX.1-waived — Gate B precondition; FR-008 dispositioned via T005 `Store_SaturatedPool_Suspends_NotBlocks`) + `/speckit-verify` prep: confirm the new code is 100% DA/BRDA or §IX.1-justified; **TSan is the primary gate** (real-pool concurrent seam + shutdown-ordering); ASan/UBSan over the new + touched session tests; the §X enum-freeze guard holds. **Art. VIII §2 bench-regression**: run `bench/session/bench_file_store BM_FileStore_Store_CommitPerMessage` against `bench/baselines/`; the `store+fdatasync` `co_await` now crosses to the pool and back (the ~48 B frame + `co_spawn` scheduling hop), so confirm within ±5% **or** update the baseline **in this PR** with rationale (the strand-blocking floor moves off the strand; the store-awaitable wall-latency floor is now pool-scheduling-bound — a documented intentional shift, plan Performance Goals).

---

## Dependencies & ordering

- **Setup (T001)** → everything.
- **Foundational (T002, T003, T004)** → all user stories (the helper + field + freeze are shared). T002→T003 are the same file (`file_store.cpp`), sequential; T004 (`error.hpp`) is `[P]`.
- **US1 (T005 RED → T006 impl → T007 regression)** lands the MVP `store()` offload.
- **US2 (T008/T009 RED → T010/T011/T012 impl)** offloads the other three sites + the cancellation contract. T006/T010/T011/T012 all edit `file_store.cpp` → **strictly sequential**.
- **US3 (T013/T014 RED → T015/T016 impl)** activates + guards the concurrency surface and teardown. T015 edits `file_store.cpp` (sequential after the US2 edits); T016 edits `engine.cpp` (independent file).
- **US4 (T017)** asserts no-surface-change; green over the whole impl.
- **Polish (T018–T020)** after the stories; T020 is the Gate B precondition.

## Parallel opportunities

- T002 ∥ T004 are different files; T003 follows T002 (same file).
- RED test authoring T005, T008, T009, T013, T014, T017 are each `[P]` (distinct test files / cases).
- **All production edits to `file_store.cpp` (T006, T010, T011, T012, T015) are sequential** — same file, no `[P]`. T016 (`engine.cpp`) and T019 (docs) are `[P]` against them.

## Implementation strategy

- **MVP = Phase 1 + 2 + 3 (US1)**: helper + field + freeze + the genuine `store()` offload + the thread-id/durability witnesses. This alone removes the `[const §XV.4]` violation on the send path.
- Then US2 (the other three offload sites + cancellation), US3 (concurrency TSan + mid-walk-reset guard + teardown), US4 (no-surface-change), then Polish (alloc gate, FR-010 docs, completeness/verify).
- **TSan is the primary gate** throughout — the inert offload made this surface dormant; making it real is the whole risk.

---

## T020 — feature-completeness audit (2026-06-14)

**tasks ↔ FR ↔ SC ↔ contract ↔ witness — 100% (three honest caveats noted).**

| FR | Covered by | Witness (test) |
|---|---|---|
| FR-001 store offload genuine | T003 helper + T006 | `Store_Syscall_OnPoolThread_NotStrand` (SC-001) |
| FR-002 all FR-024 sites offloaded (store/next_seqnum/reset/flush) | T006/T010/T011 | `{Store,NextSeqnum,Reset,FlushForSessionClose}_Syscall_OnPoolThread` |
| FR-003 durable-before-send | T006 (datasync last syscall in lambda) | `store_file_crash_survival` (008 SC-002, unchanged) + T007 |
| FR-004 binary §6.1.4 cancellation contract | T012 unconditional `operation_aborted`→durable catch | `*_CancelAtMutexAcquire_*` + `*_CancelMidSyscall_DurableNotCancelled` |
| FR-005 concurrent store/retrieve/reset race-free | T015 strand-confinement | `ConcurrentStoreRetrieveReset_TSanClean` (SC-005) |
| FR-006 retrieve pread on strand + mid-walk-reset guard | T015 (`generation_` snapshot+recheck; 3 inert posts removed; reset bumps) | `MidWalkReset_GenerationGuard_FailsClean` + `ResetRace_vs_LogicalGap_Discriminating` |
| FR-007 teardown drain (no UAF) | T016 (stop() drains; misuse note) | `FileStoreOffloadDrainBeforePoolJoin` + `FileStoreFlushForCloseIsGenuineOffload` (SC-007) |
| FR-008 backpressure/suspend-on-saturation | structural (`co_await co_spawn(use_awaitable)` suspends) | `Store_SaturatedPool_Suspends_NotBlocks` |
| FR-009 no public surface change | T004 freeze static_assert + surface audit | T017 (full 008 suite GREEN; only static_assert + FIXPP_TEST_HOOKS-gated seams added) |
| FR-010 docs reconciliation | T019 | message_store.hpp comment + 008 dated notes + B-035-1 + catalogue/coverage-index |

| SC | Witness |
|---|---|
| SC-001 syscall on pool thread ≠ strand | `Store_Syscall_OnPoolThread_NotStrand` (FIXPP_TEST_HOOKS thread-id probe) |
| SC-002 strand progresses before fdatasync returns | `Store_StrandProgresses_BeforeSyscallReturns` |
| SC-003 crash-survival 0% loss | `store_file_crash_survival` (4/4, unchanged) |
| SC-004 per-method cancellation contract | `store_file_cancellation` (7 cells, both arms × 3 methods + no-wedge) |
| SC-005 concurrent TSan/ASan clean | `store_file_concurrent_tsan` (3 cells, TSan+ASan) |
| SC-006 MemoryStore/surfaces byte-identical | T017 + existing MemoryStore suite + freeze static_assert |
| SC-007 teardown UAF-free | `store_shutdown_ordering` (7 cells, TSan+ASan) |

**Contracts C1–C6:** C1 (offload helper — T003/T006/T010/T011), C2 (durability — T007), C3 (cancellation incl. flush carve-out — T009/T012), C4 (concurrency/generation guard — T013/T015), C5 (teardown drain-before-join — T014/T016), C6 (no-surface — T004/T017). All witnessed.

**Honest caveats (for /speckit-verify + Gate B):**
1. **Alloc (T018) — by-construction.** `MemoryStore::store == 0` is gated by the existing `perf_store_alloc_guard` (mallocnesia + counting_resource), unchanged by 035. The `FileStore` offload's **≤1 frame/op** is the `[const §XV.1]` v0.2 §XV.4-offload exemption: the nested `co_spawn` frame is global-heap / PMR-opaque (escapes counting_resource; would trip a strict mallocnesia==0 gate by design). Satisfied by-construction (exactly 1 `co_spawn`/offloaded op) + empirically by `research/probes/cospawn_probe*` (1/op). Mirrors the 034 honest precedent.
2. **T012 catch BRDA.** The unconditional `operation_aborted`→durable catch is defensive; the `*_CancelMidSyscall_*` cells assert `read_and_reset_catch_fired()==0` (it is unreachable under normal asio — `co_spawn` completes the child and delivers its value). It *may* be exercised by the teardown total-cancel path; **/speckit-verify must check its DA/BRDA coverage** — if uncovered, either a fault-injection seam or a §IX.1 waiver is required.
3. **Test instrumentation.** Three `FIXPP_TEST_HOOKS`-declared runtime test seams (`install_store_offload_probe` / `read_and_reset_catch_fired` / `read_and_reset_retrieve_pread_count`) are always-compiled (probe precedent), adding negligible relaxed-atomic overhead on the FileStore disk paths only (not the in-memory hot path), no production-API surface. **Flagged for /simplify** to assess consolidation/gating.

**Verify-prep (executed in /speckit-verify):** new code DA/BRDA (caveat 2); TSan primary gate (concurrent + shutdown seams — confirmed GREEN during implement); ASan/UBSan over new + touched session tests; §X enum-freeze static_assert holds; **Art. VIII §2 bench** `BM_FileStore_Store_CommitPerMessage` vs baseline (±5% or baseline-update-with-rationale for the co_spawn-hop shift).
