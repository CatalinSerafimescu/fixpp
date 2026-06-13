# Implementation Plan: Real file_io_executor offload for FileStore

**Branch**: `035-filestore-io-offload` | **Date**: 2026-06-13 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/035-filestore-io-offload/spec.md`

## Summary

Make `fixpp::session::FileStore`'s blocking disk syscalls genuinely run on the `file_io_executor`
instead of the session strand, fixing a live `[const §XV.4]` violation. The shipped code offloads with
`co_await asio::post(impl_->cfg.file_io_executor, asio::use_awaitable)` (4 sites:
`file_store.cpp:824` store, `:942` retrieve, `:1015` next_seqnum, `:1068` reset). That idiom is **inert**
(012 D-18 / `[[feedback_asio_post_resume_bounces_to_spawn_executor]]`): `post(other_exec, use_awaitable)`
schedules the *completion handler* on `other_exec`, but `use_awaitable` honours the coroutine's associated
executor and re-dispatches the **resumption back to the session strand**, so the syscall on the very next
line runs on the strand. The session strand therefore blocks on `pwrite`/`fdatasync` on **every send**.

The fix replaces each inert post with the **nested-`co_spawn` shape**: `co_await asio::co_spawn(
impl_->cfg.file_io_executor, [captured-by-value syscall args]() -> awaitable<R> { /* raw syscall */ },
asio::use_awaitable)`. The nested coroutine's body is pinned to `file_io_executor` (so the syscall truly
runs on the pool); the outer `co_await … use_awaitable` resumes on the session strand. Per the
Clarifications (2026-06-13), **all `impl_` state mutation stays on the session strand** — the nested
lambda captures only the raw syscall arguments (the `OsFile` fd/handle value, offset, buffer span) by
value and returns the syscall result; `write_pos`, the counters, the index vectors, and `reset()`'s
live-handle swap (`impl_->file = std::move(new_file)`, `file_store.cpp:1149`) are assigned in the outer
coroutine on the strand. This keeps `impl_` single-threaded (strand-only) and dissolves the cross-context
data race by construction.

`retrieve()`'s `pread` is **not** offloaded (Clarifications) — only the FR-024 write/reset ops are. The
one residual hazard is **mid-walk `reset()`**: `retrieve()` snapshots the index under the mutex
(`file_store.cpp:908–932`), releases it (`:933`), then walks frames, suspending at each
`visitor.on_frame()` `co_await` (`:957`); a `reset()` can run during that suspension and swap/truncate the
live log. This feature adds a **store-generation counter** to `FileStoreImpl`, bumped by `reset()`,
snapshotted by `retrieve()` at index-snapshot time and re-checked before each frame read — a mid-walk
`reset()` fails the walk cleanly with `store_reset_during_retrieve` rather than reading a swapped handle
(008 invariant **I-03**).

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
**Scale/Scope**: in-place rework of 4 methods in one `.cpp` + 1 new `FileStoreImpl` field + 1 new internal error variant (`store_reset_during_retrieve`) + new TSan/thread-id test cells. Est. 150–250 LoC production, behavior-preserving on disk.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-checked after Phase 1 design.*

| Article | Relevance | Disposition |
|---|---|---|
| **XV §4 — no synchronous disk I/O on every send** (banned QuickFIX FileStore pattern) | This feature **fixes** a live violation of it | The inert offload meant `store()`'s `pwrite`+`fdatasync` ran on the session strand on every send — exactly the banned pattern. Genuine offload to `file_io_executor` restores compliance. This is the feature's *raison d'être*. PASS-by-construction (witnessed: thread-id probe SC-001). |
| **XI — Concurrency** (§XI.1 coroutines, §XI.2 native cancellation, §XI.4 callbacks on session strand, §XI.5 store-write path always under mutex) | Core of the change | Nested `co_spawn(file_io_executor)` per §XI.1/§4.3.2; outer resumes on session strand (§XI.4 — visitor/completion on strand, `file_store.cpp:953/957`); writer mutex still held across store/next_seqnum/reset (§XI.5); cancellation kept native (§XI.2) — see FR-004 design. PASS. |
| **XV §9 — no `std::mutex` in coroutine context** | We touch the locked region | No new lock; the existing `async_mutex impl_->mutex_` is retained. The nested lambda holds **no** lock (raw syscall only). PASS. |
| **XV §1 — no per-message heap alloc on hot path** | `store` is the hot path | **RESOLVED in Phase 0 (decision #1, with an empirical verdict).** A genuine cross-thread offload incurs **one ~48 B PMR-opaque coroutine-frame allocation per op** (probe-measured; asio awaitable frames don't route through the bound allocator and HALO can't fire cross-thread — the **same** cost the shipped TLS offload already accepts, per `cancellable_dispatch.hpp:33–42`). Disposition: PASS — §XV.1's zero-alloc bar is witnessed on `MemoryStore`/the strand chain, not `FileStore`'s disk path (dominated by `fdatasync`); the alloc gate asserts **bounded O(1) per op** for `FileStore::store` and **zero** for `MemoryStore`. Gate-A blesses. Persistent-worker fallback held in reserve. |
| **IX §1 — coverage / sanitizers** | New concurrency surface | TSan is the **primary** gate here (the inert offload made the surface dormant). New code 100% DA/BRDA; TSan+ASan over the real-pool concurrent seam (SC-005); shutdown-ordering ASan/TSan seam (SC-007). |
| **X — ABI** | Public surface | **No** public type/signature change. The new `store_reset_during_retrieve` is an addition to the **internal** `fixpp::core::error` enum (check: is the error enum part of the frozen C-ABI taxonomy? — verified in Phase 0 against the FR-021 10-variant freeze; if frozen, reuse an existing variant or gate behind the visitor-abort group). PASS pending that check. |
| **VII — testing (≥ seams)** | New behavior | New seams: thread-id offload witness (per method), FileStore cancellation-contract (the existing seam is MemoryStore-only — `test_store_cancellation_contract.cpp:44–52`), real-pool concurrent store/retrieve/reset TSan, mid-walk-reset detection, shutdown-ordering. |
| **VI — 100% FIX rule / catalogue** | No new FIX message/field | No catalogue *row* add; updates B&L (FR-010) + corrects the stale 008 offload-claim docs. |
| **Dependencies / Version Management** | None added | No new third-party dependency; Asio already in-tree. |

**Surface delta**: no wire field, no `SessionConfig`/`EngineConfig` field (the `file_io_executor` field already exists, 008 FR-024a), no codegen, no C-ABI signature, no `MessageStore` pure-virtual. The only additions are **internal**: one `FileStoreImpl` field (store-generation) and possibly one internal error variant (Phase-0 ABI check). Gate-clean pending the §X error-enum check.

## Project Structure

### Documentation (this feature)

```text
specs/035-filestore-io-offload/
├── plan.md              # This file
├── research.md          # Phase 0 — the 6 design decisions
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
└── file_store.cpp       # the 4 inert sites (824/942/1015/1068) → nested co_spawn; strand-confine impl_ mutation;
                         #   + FileStoreImpl store-generation field; reset() bumps it; retrieve() snapshots+rechecks it

include/fixpp/core/
└── error.hpp (or wherever fixpp::core::error lives)  # IFF Phase-0 ABI check allows: + store_reset_during_retrieve (internal)

src/core/ | src/session/
└── engine.cpp           # FR-007: confirm/strengthen that Engine::stop() drains every store-awaiting coroutine
                         #   before returning, so app-owned pool join is safe (stop() sequence at engine.cpp:1184–1333)

tests/session/
├── test_file_store_offload_thread.cpp   # NEW — thread-id probe: syscall on pool thread ≠ strand thread (SC-001/002), per method
├── test_file_store_cancellation.cpp     # NEW — FileStore per-method cancellation contract (SC-004) — the FileStore twin of the MemoryStore-only test_store_cancellation_contract.cpp
├── test_file_store_concurrent_tsan.cpp  # NEW — real 4-thread pool: store/retrieve/reset overlap, TSan/ASan clean, mid-walk-reset detection (SC-005)
└── test_store_shutdown_ordering.cpp     # EXTEND — in-flight offloaded FileStore I/O at Engine::stop() (SC-007)

spec/
├── behaviors-and-limitations.md         # L-* the prior latent §XV.4 violation → mitigated-by-035 (FR-010)
└── feature-catalogue.md / coverage-index.md  # traceability row for 035

specs/008-message-store/
├── plan.md (:16)        # flip the false "[const §XV.4] passes" / "runs on file_io_executor" claim (dated note, no history rewrite)
├── research.md (:163/170/440), contracts/file_store.hpp (:14/86)  # correct the stale offload-works comments
include/fixpp/session/message_store.hpp (:95)                      # correct the stale threading comment
```

**Structure Decision**: Single-library, in-place. The whole change is internal to `file_store.cpp`'s 4
methods plus one pimpl field; no new module, no header-graph change (`tools/check_layers.py` unaffected).
The offload helper is a file-local `static` coroutine in `file_store.cpp` (not a public header) so it adds
no exported surface (Art. X). Doc corrections to the 008 bundle are dated notes, never history rewrites.

## Phase 0 — Research

See [research.md](./research.md) — all six decisions resolved, the allocation one with a reproducible probe verdict (`research/probes/cospawn_probe{,2,3}.cpp`):

1. **`co_spawn` allocation discipline (§XV.1) — THE Gate-A decision, with a concrete verdict.** Per-call nested `co_spawn` costs **one ~48 B PMR-opaque frame/op** (cannot be arena-routed; HALO can't fire cross-thread); accepted as bounded-O(1) on the FileStore disk path (precedent: the TLS offload), with `MemoryStore` zero-alloc preserved. Persistent-worker fallback documented.
2. **The offload idiom** — nested `co_spawn(file_io_executor, fn, use_awaitable)`; **probe-confirmed** body-on-pool + resume-on-strand (5000/5000). The file-local helper captures only POD syscall args.
3. **Strand-confined mutation boundary** — only the `OsFile` syscall runs in the nested lambda; every `impl_->…` mutation (incl. reset's `file = std::move`, `:1149`) stays outer on the strand.
4. **Cancellation** (FR-004 / SC-006) — single checkpoint stays at the `async_mutex` acquire; past it the syscall is durable-uninterruptible; defensive `operation_aborted` try/catch only if the slot-already-signalled probe shows the child can run post-linearisation. Linearisation unchanged.
5. **Mid-walk-`reset()` detection** — a `FileStoreImpl` generation counter bumped by `reset()`, snapshotted + re-checked by `retrieve()`; clean-failure error reuses an existing variant (FR-021 10-variant freeze check).
6. **Teardown ordering** (FR-007) — nested `co_spawn` is `use_awaitable` (joined, NOT detached); `Engine::stop()` drain proves pool work completes before return; app-owned-pool join contract documented. Also: the paired rebind posts are **removed** by the restructure (the `co_await` completion IS the resume-to-strand — no separate rebind), and the leading pump-break posts are investigated for vestigiality.

## Phase 1 — Design & Contracts

- [data-model.md](./data-model.md) — the `FileStoreImpl` store-generation field; the offload-helper signature; the per-method linearisation points (unchanged from §6.1.4: store@`fdatasync`, next_seqnum@counter-pwrite, reset@dir-`fsync`/`MoveFileExW`); the retrieve generation-guard invariant.
- [contracts/file-offload.md](./contracts/file-offload.md) — the nested-`co_spawn` offload contract (pre/post: syscall-on-pool, mutation-on-strand, mutex-held-by-outer), the cancellation-result contract per method, and the teardown drain-before-join contract.
- [quickstart.md](./quickstart.md) — the thread-id witness recipe (real pool, probe captured inside the syscall window) + the TSan concurrent-pool recipe.

## Complexity Tracking

No constitution violations to justify (the feature *removes* a violation). The two non-trivial choices —
(a) the cancellation try/catch to preserve durable success, and (b) the store-generation guard for
mid-walk-reset — are the minimum needed for correctness under a *real* offload; both are forced by making
the dormant concurrency surface live, not added speculatively. The `co_spawn`-allocation question (Phase-0
decision 6) is the one place a simpler-vs-safer tradeoff is evaluated against the §XV.1 alloc gate.
