# Implementation Plan: Real file_io_executor offload for FileStore

**Branch**: `035-filestore-io-offload` | **Date**: 2026-06-13 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/035-filestore-io-offload/spec.md`

## Summary

Make `fixpp::session::FileStore`'s blocking disk syscalls genuinely run on the `file_io_executor`
instead of the session strand, fixing a live `[const §XV.4]` violation. The shipped code offloads with the
inert `co_await asio::post(impl_->cfg.file_io_executor, asio::use_awaitable)` idiom at three write/reset
sites (`file_store.cpp:824` store, `:1015` next_seqnum, `:1068` reset), at `retrieve`'s read (`:942`), and
`flush_for_session_close`'s blocking `datasync` (`:1252`) runs on the close strand with no offload at all.
The post idiom is **inert** (012 D-18 / `[[feedback_asio_post_resume_bounces_to_spawn_executor]]`):
`post(other_exec, use_awaitable)` schedules the *completion handler* on `other_exec`, but `use_awaitable`
honours the coroutine's associated executor and re-dispatches the **resumption back to the session
strand**, so the syscall on the very next line runs on the strand. The session strand therefore blocks on
`pwrite`/`fdatasync` on **every send**.

The fix replaces each inert write/reset post with the **nested-`co_spawn` shape**: `co_await asio::co_spawn(
impl_->cfg.file_io_executor, [captured-by-value syscall args]() -> awaitable<R> { /* raw syscall */ },
asio::use_awaitable)`. The nested coroutine's body is pinned to `file_io_executor` (so the syscall truly
runs on the pool); the outer `co_await … use_awaitable` resumes on the session strand. **The offloaded
write/reset/close set is store, next_seqnum, reset, and `flush_for_session_close` (4 sites); `retrieve`'s
`:942` inert post is *removed* (not converted) and its `pread` stays on the session strand** (Clarifications
2026-06-13 — only FR-024 write/reset ops + the close-flush are offloaded; `retrieve`'s read is not). Per
the Clarifications, **all `impl_` state mutation stays on the session strand** — the nested lambda captures
only the raw syscall arguments (the `OsFile` fd/handle value, offset, buffer span) by value and returns the
syscall result; `write_pos`, the counters, the index vectors, and `reset()`'s live-handle swap
(`impl_->file = std::move(new_file)`, `file_store.cpp:1149`) are assigned in the outer coroutine on the
strand. This keeps `impl_` single-threaded (strand-only) and dissolves the cross-context data race by
construction.

The one residual hazard is **mid-walk `reset()`**: `retrieve()` snapshots the index under the mutex
(`file_store.cpp:908–932`), releases it (`:933`), then walks frames, suspending at each
`visitor.on_frame()` `co_await` (`:957`); a `reset()` can run during that suspension and swap/truncate the
live log. This feature adds a **store-generation counter** to `FileStoreImpl`, bumped by `reset()`,
snapshotted by `retrieve()` at index-snapshot time and re-checked before each frame read — a mid-walk
`reset()` fails the walk cleanly with **`store_io_failure`** (the existing variant; *no new
`store_*` variant is introduced* — FR-021's 10-variant freeze is preserved) rather than reading a swapped
handle (008 invariant **I-03**).

This is a **conformance / defect-fix** feature: it realizes the already-Gate-A-approved design
(`.specify/2e-msgstore.md` §4.3.2 / §6.5; 008 **FR-024** / **I-13** / **FR-020** / **SC-006**) as correct
code. It does **not** amend the design. FileStore-only; `MemoryStore` and all public surfaces are
unchanged.

## Technical Context

**Language/Version**: C++23 (Clang 22 local == CI per `[const Art.II §2]`)
**Primary Dependencies**: standalone Asio (coroutines, `co_spawn`, `any_io_executor`, native cancellation), existing `async_mutex` (`include/fixpp/core/sync/async_mutex.hpp`), the `OsFile` RAII wrapper + `FileStoreImpl` pimpl (`src/session/file_store.cpp:405–754`)
**Storage**: the production `FileStore` append-only single-log-per-session on-disk format — on-disk layout **unchanged**; only *where* its syscalls run changes
**Testing**: GoogleTest; TSan + ASan over a real multi-thread `asio::thread_pool` `file_io_executor`; thread-id probe seams; the existing 008 store suite as regression
**Target Platform**: Linux (Tier-1, `pwrite`/`fdatasync`/`rename`/dir-`fsync`); Windows path (`MoveFileExW`) structurally mirrored, Tier-2
**Project Type**: single library (`fixpp`)
**Performance Goals**: session strand performs **zero** disk syscalls on the send path; FileStore `store` p-latency unchanged vs the design's §6.6 ≤250 µs (the flush floor moves off the strand, it does not grow)
**Constraints**: `impl_` mutated strand-only (TSan-clean); durable-before-send preserved (FR-003); per-method cancellation contract preserved (FR-004); no public signature/error/config/wire change (FR-009)
**Scale/Scope**: in-place rework of 4 offloaded methods (store, next_seqnum, reset, flush_for_session_close) in one `.cpp` + removal of `retrieve`'s inert `:942` post (pread stays on the strand) + 1 new `FileStoreImpl` field (store-generation) + **no new error variant** (the mid-walk-reset clean-failure reuses the existing `store_io_failure`; FR-021's 10-variant freeze preserved) + new TSan/thread-id test cells. Est. 150–250 LoC production, behavior-preserving on disk.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-checked after Phase 1 design.*

| Article | Relevance | Disposition |
|---|---|---|
| **XV §4 — no synchronous disk I/O on every send** (banned QuickFIX FileStore pattern) | This feature **fixes** a live violation of it | The inert offload meant `store()`'s `pwrite`+`fdatasync` ran on the session strand on every send — exactly the banned pattern. Genuine offload to `file_io_executor` restores compliance. This is the feature's *raison d'être*. PASS-by-construction (witnessed: thread-id probe SC-001). |
| **XI — Concurrency** (§XI.1 coroutines, §XI.2 native cancellation, §XI.4 callbacks on session strand, §XI.5 store-write path always under mutex) | Core of the change | Nested `co_spawn(file_io_executor)` per §XI.1/§4.3.2; outer resumes on session strand (§XI.4 — visitor/completion on strand, `file_store.cpp:953/957`); writer mutex still held across store/next_seqnum/reset (§XI.5); cancellation kept native (§XI.2) — see FR-004 design. PASS. |
| **XV §9 — no `std::mutex` in coroutine context** | We touch the locked region | No new lock; the existing `async_mutex impl_->mutex_` is retained. The nested lambda holds **no** lock (raw syscall only). PASS. |
| **XV §1 — no per-message heap alloc on hot path** | `store` is on the send path | **COMPLIANT by the §XV.1 v0.2 §XV.4-offload exemption** (`constitution.md:224`, amended 2026-06-13; cross-ref §XI.6). The exemption explicitly permits **a single bounded O(1) coroutine frame per offloaded I/O op** on the §XV.4 FileStore async-journal path, because that completion frame routes to neither HALO (cannot fire cross-executor) nor a PMR arena (the asio awaitable frame is opaque to the bound allocator) — the structural §XV.1↔§XV.4 tension the amendment was written to resolve. The nested-`co_spawn` design at exactly **1 frame/op** falls inside the exemption. The "hot path" of the ban is the **in-memory** path (parse→validate→dispatch + `MemoryStore`), which stays zero-alloc. (Factual, not the compliance argument: the probe shows the shipped inert idiom costs 4/op and never reaches the pool — `cospawn_probe4` `on_other=0/5000` — vs 1/op genuinely on the pool for the fix.) Idiom is project-blessed (`asio_tls_transport.hpp:45–51`). Gate: `FileStore` offload path **≤ 1 frame/op (O(1) exemption ceiling)**; `MemoryStore::store` **== 0** (unchanged). |
| **IX §1 — coverage / sanitizers** | New concurrency surface | TSan is the **primary** gate here (the inert offload made the surface dormant). New code 100% DA/BRDA; TSan+ASan over the real-pool concurrent seam (SC-005); shutdown-ordering ASan/TSan seam (SC-007). |
| **X — ABI** | Public surface | **No** public type/signature change, and **no new error variant**: the mid-walk-reset clean-failure reuses the existing `store_io_failure` (Phase 0 decision #4, against the FR-021 10-variant freeze, `error.hpp:165–230`). A compile/enum guard asserts the `store_*` set stays at exactly 10 (no 11th variant). PASS. |
| **VII — testing (≥ seams)** | New behavior | New seams: thread-id offload witness (per method), FileStore cancellation-contract (the existing seam is MemoryStore-only — `test_store_cancellation_contract.cpp:44–52`), real-pool concurrent store/retrieve/reset TSan, mid-walk-reset detection, shutdown-ordering. |
| **VI — 100% FIX rule / catalogue** | No new FIX message/field | No catalogue *row* add; updates B&L (FR-010) + corrects the stale 008 offload-claim docs. |
| **Dependencies / Version Management** | None added | No new third-party dependency; Asio already in-tree. |

**Surface delta**: no wire field, no `SessionConfig`/`EngineConfig` field (the `file_io_executor` field already exists, 008 FR-024a), no codegen, no C-ABI signature, no `MessageStore` pure-virtual, **no new error variant** (mid-walk-reset reuses `store_io_failure`). The only addition is **internal**: one `FileStoreImpl` field (store-generation). Gate-clean; the §X error-enum guard asserts the 10-variant `store_*` freeze holds.

## Project Structure

### Documentation (this feature)

```text
specs/035-filestore-io-offload/
├── plan.md              # This file
├── research.md          # Phase 0 — the 7 design decisions
├── data-model.md        # Phase 1 — FileStoreImpl delta + the offload-helper + generation-guard contract
├── quickstart.md        # Phase 1 — the thread-id offload witness + TSan concurrent-pool recipe
├── contracts/
│   └── file-offload.md  # internal contract: the nested-co_spawn offload helper + per-method linearisation/cancellation/teardown
├── checklists/
│   └── requirements.md  # spec-quality checklist (done)
└── tasks.md             # /speckit-tasks output (NOT created here)
```

### Source Code (repository root)

```text
src/session/
└── file_store.cpp       # 3 write/reset inert sites (824 store, 1015 next_seqnum, 1068 reset) → nested co_spawn;
                         #   + flush_for_session_close datasync (:1252) → nested co_spawn (Decision 7);
                         #   retrieve's :942 inert post REMOVED (pread stays on the session strand — Clarifications);
                         #   strand-confine impl_ mutation; + FileStoreImpl store-generation field; reset() bumps it;
                         #   retrieve() snapshots+rechecks it (the re-check and the pread are atomic on the strand —
                         #   no co_await between them); mid-walk-reset clean-fails with store_io_failure (no new variant)

include/fixpp/core/
└── error.hpp            # NO new variant — mid-walk-reset reuses store_io_failure; add a compile/enum guard
                         #   asserting the store_* set stays at exactly 10 (FR-021 freeze, :165–230)

src/session/
└── engine.cpp           # FR-007: confirm/strengthen that Engine::stop() drains every Session-reachable store-awaiting
                         #   coroutine before returning, so the app-owned pool join is safe (stop() at engine.cpp:1184–1333)

tests/session/
├── test_file_store_offload_thread.cpp   # NEW — thread-id probe: syscall on pool thread ≠ strand thread (SC-001/002), per method;
│                                        #   + single-thread-pool variant: syscall thread ≠ strand thread, no deadlock (spec edge case)
├── test_file_store_cancellation.cpp     # NEW — FileStore per-method cancellation contract (SC-004), the FileStore twin of the
│                                        #   MemoryStore-only test_store_cancellation_contract.cpp; covers the TWO sub-cases (binary §6.1.4):
│                                        #   (a) cancel at mutex-acquire (before the offload is issued) → store_cancelled, no syscall, 0 state change;
│                                        #   (b) cancel mid-syscall (in-syscall hook) → runs to durable completion, NOT store_cancelled (unconditional catch);
│                                        #   plus: co_spawn terminal-only default while Engine::stop() emits total — must not swallow total + wedge
├── test_file_store_concurrent_tsan.cpp  # NEW — real 4-thread pool: store/retrieve/reset overlap, TSan/ASan clean, mid-walk-reset detection (SC-005)
└── test_store_shutdown_ordering.cpp     # EXTEND — in-flight offloaded FileStore store() AND flush_for_session_close (graceful
                                         #   close under commit_batched) at Engine::stop() (SC-007)

spec/
├── behaviors-and-limitations.md         # L-* the prior latent §XV.4 violation → mitigated-by-035 (FR-010)
└── feature-catalogue.md / coverage-index.md  # traceability row for 035

specs/008-message-store/
├── plan.md (:16)        # flip the false "[const §XV.4] passes" / "runs on file_io_executor" claim (dated note, no history rewrite)
├── research.md (:163/170/440), contracts/file_store.hpp (:14/86)  # correct the stale offload-works comments
include/fixpp/session/message_store.hpp (:95)                      # correct the stale threading comment
```

**Structure Decision**: Single-library, in-place. The whole change is internal to `file_store.cpp`'s 4
offloaded methods (store, next_seqnum, reset, flush_for_session_close) plus the removal of `retrieve`'s
inert `:942` post and one pimpl field; no new module, no header-graph change (`tools/check_layers.py` unaffected).
The offload helper is a file-local `static` coroutine in `file_store.cpp` (not a public header) so it adds
no exported surface (Art. X). Doc corrections to the 008 bundle are dated notes, never history rewrites.

## Phase 0 — Research

See [research.md](./research.md) — all seven decisions resolved, the allocation one with a reproducible probe verdict (`research/probes/cospawn_probe{,2,3,4}.cpp`):

1. **`co_spawn` allocation discipline (§XV.1) — THE Gate-A decision, resolved by the v0.2 amendment.** Per-call nested `co_spawn` costs **one ~48 B PMR-opaque frame/op** (cannot be arena-routed; HALO can't fire cross-thread) — the structural §XV.1↔§XV.4 tension that **forced the v0.2 constitution amendment** (`constitution.md:224`). The design at **1 frame/op** is **compliant by the §XV.1 v0.2 §XV.4-offload exemption** (≤1 bounded O(1) frame/op on the FileStore offload path), with `MemoryStore::store` zero-alloc preserved. The persistent-worker alternative was **empirically rejected** (measured ≈10/op — *worse*, not the assumed zero; the amendment removes any need for it).
2. **The offload idiom** — nested `co_spawn(file_io_executor, fn, use_awaitable)`; **probe-confirmed** body-on-pool + resume-on-strand (5000/5000). The file-local helper captures only POD syscall args.
3. **Strand-confined mutation boundary** — only the `OsFile` syscall runs in the nested lambda; every `impl_->…` mutation (incl. reset's `file = std::move`, `:1149`) stays outer on the strand.
4. **Cancellation** (FR-004 / SC-006) — the **binary** §6.1.4 contract: a **single** pre-linearisation observation point, the `async_mutex` acquire (cancel there → `store_cancelled`, no syscall, no state change). Once the mutex is held and the child is `co_spawn`'d, the syscall runs to durable completion uninterruptibly (linearisation unchanged); a post-linearisation `operation_aborted` at the outer await is caught and **unconditionally** returned as the durable result (no false-cancel on a persisted frame). No second checkpoint / no bespoke reaper.
5. **Mid-walk-`reset()` detection** — a `FileStoreImpl` generation counter bumped by `reset()`, snapshotted + re-checked by `retrieve()`; clean-failure **reuses `store_io_failure`** (no new variant — FR-021 10-variant freeze preserved).
6. **Teardown ordering** (FR-007) — nested `co_spawn` is `use_awaitable` (joined, NOT detached); `Engine::stop()` drain proves Session-reachable pool work completes before return; the pool is app-owned (caller obligation: executor outlives outstanding store awaitables; misuse note for direct non-Session FileStore use). Also: the paired rebind posts are **removed** by the restructure (the `co_await` completion IS the resume-to-strand — no separate rebind); the leading pump-break posts (`:787/991/1043`) are **retained, load-bearing** (they keep method entry on the strand before mutex-acquire — Decision 2's precondition; orthogonal to the offload frame).
7. **`flush_for_session_close` datasync** (Decision 7) — the blocking `datasync` (`:1252`) on the close strand is offloaded via the same nested-`co_spawn`; failure still maps to `store_io_failure`. The shutdown-ordering seam is extended to graceful close under `commit_batched`.

## Phase 1 — Design & Contracts

- [data-model.md](./data-model.md) — the `FileStoreImpl` store-generation field; the offload-helper signature; the per-method linearisation points (unchanged from §6.1.4: store@`fdatasync`, next_seqnum@counter-record `pwrite` + `datasync` (`:1024`), reset@dir-`fsync`/`MoveFileExW`); the retrieve generation-guard invariant.
- [contracts/file-offload.md](./contracts/file-offload.md) — the nested-`co_spawn` offload contract (pre/post: syscall-on-pool, mutation-on-strand, mutex-held-by-outer), the cancellation-result contract per method, and the teardown drain-before-join contract.
- [quickstart.md](./quickstart.md) — the thread-id witness recipe (real pool, probe captured inside the syscall window) + the TSan concurrent-pool recipe.

## Complexity Tracking

No constitution violations to justify: the feature *removes* a §XV.4 violation, and its one per-op frame
is **compliant by the §XV.1 v0.2 §XV.4-offload exemption** (the structural §XV.1↔§XV.4 tension was resolved
at the constitutional layer, not carved out in this bundle). The two non-trivial choices — (a) the
unconditional `operation_aborted`→durable try/catch to preserve durable success on a post-linearisation
abort, and (b) the store-generation guard
for mid-walk-reset — are the minimum needed for correctness under a *real* offload; both are forced by
making the dormant concurrency surface live, not added speculatively. The `co_spawn`-allocation question
(Phase-0 decision 1) is settled against the §XV.1 v0.2 exemption ceiling (≤1 O(1) frame/op).

## Gate A

| Round | Outcome |
|---|---|
| Round 1 (2026-06-13) | Codex P1=3/P2=4/P3=1; Opus post-judging P1=1/P2=7/P3=2; structural §XV.1 P1 resolved by constitution v0.2 amendment (§XV.1 §XV.4-offload exemption); P2s addressed in this convergence rewrite. Reviews: `research/reviews/{codex,opus}_035-filestore-io-offload_gate_a*.md`. |
| Round 2 (2026-06-13) | Codex P1=0/P2=3/P3=2; Opus post-judging P1=0/P2=4/P3=3 (§XV.1 P1 confirmed resolved). Rewrite #2 applies the **binary-cancellation simplification** (§6.1.4 single mutex-acquire checkpoint; drop the two-checkpoint model; retain + make the `operation_aborted`→durable try/catch unconditional; pin `co_spawn` as final no-reaper shape) + the §6.1.4/§6.2.1 per-method re-derivation (`next_seqnum` linearisation = counter-record `pwrite` + `datasync` `:1024`; `flush_for_session_close` pulled out of C1's mutex precondition and C3's cancellation table — non-cancellable, no-mutex, `store_io_failure`-only) + US3 prose → logical stale-snapshot. Reviews: `research/reviews/{codex,opus}_035-filestore-io-offload_gate_a_2*.md`. |
| Round 3 (2026-06-13) | Codex P1=0/P2=1/P3=0; Opus post-judging P1=0/P2=1/P3=0. Lone residual P2: `spec.md` US2 independent test + `reset()` scenario 4 + SC-004 still carried the unrealizable "cancel before the `rename` linearisation point → `store_cancelled`" phrasing (rewrite #2 fixed the `store` analogue but missed the `reset` sibling). Rewrite cap reached → user authorized a single targeted close-out fix. Reviews: `research/reviews/{codex,opus}_035-filestore-io-offload_gate_a_3*.md`. |
| Close-out (2026-06-13) | Targeted `spec.md`-only fix aligned the `reset`/US2-test/SC-004 cancellation prose to the binary boundary (mutex-acquire-before-offload → `store_cancelled`; after-offload/mid-syscall → durable, never `store_cancelled`). **Codex confirm pass: CONVERGED — P1=0, P2=0.** Reviews: `research/reviews/codex_035-filestore-io-offload_gate_a_4_review.md`. **Gate A converged; user-signed-off 2026-06-13.** |
