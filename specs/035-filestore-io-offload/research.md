# Phase 0 Research: Real file_io_executor offload for FileStore

**Feature**: `035-filestore-io-offload` | **Date**: 2026-06-13

All six decisions below are grounded in (a) the shipped `file_store.cpp` code (file:line from the
implementation map), (b) the approved design `.specify/2e-msgstore.md` §4.3.2/§6.1.4/§6.5, and (c) a
standalone asio probe (sources saved at `research/probes/cospawn_probe{,2,3}.cpp`) run against the
project's own asio (`asio6e6c781a0fee4`). The probe verdicts are reproducible.

---

## Decision 1 — Offload mechanism + the per-op allocation (THE Gate-A decision)

**Decision**: Replace each inert `co_await asio::post(file_io_executor, use_awaitable)` (sites
`file_store.cpp:824` store, `:1015` next_seqnum, `:1068` reset) with the **nested-`co_spawn` shape**:
`co_await asio::co_spawn(impl_->cfg.file_io_executor, raw_syscall_coro(by-value args), use_awaitable)`,
where `raw_syscall_coro` performs **only** the blocking syscall and returns its result. Accept the
single small per-op coroutine-frame allocation this incurs on the FileStore I/O path; it is **not** a
`[const §XV.1]` violation in context (see disposition).

**Empirical basis** (probe, 5000 warm iterations, global `operator new` counted):

| Mechanism | body-on-pool | resume-on-strand | global `new`/op | arena-routable? |
|---|---|---|---|---|
| inert `post(pool, use_awaitable)` (shipped) | **NO** (runs on strand) | n/a | — | — |
| nested `co_spawn(pool, fn, use_awaitable)` | **YES** 5000/5000 | **YES** 5000/5000 | **1.001** (~48 B) | **no** |
| `co_spawn` + `bind_allocator(arena, …)` | YES | YES | 1.001 | **no** (frame is PMR-opaque) |
| hand-rolled `async_initiate` (2 posts) | YES | YES | 2.001 | no (in the toy probe) |

**Why the allocation cannot be routed away**: the ~48 B/op is the **child coroutine frame**, allocated
by the C++ coroutine machinery via the awaitable promise's `operator new`. asio's `awaitable<>` promise
does **not** route that through the completion token's associated allocator — confirmed by the probe
(`bind_allocator` left global `new` at 1.001, custom allocator never called) **and independently by the
project's own `cancellable_dispatch` header** (`include/fixpp/core/cancellable_dispatch.hpp:33–42`):
> *"When HALO does NOT fire (e.g., cross-thread dispatch …) the coroutine frame is allocated by asio's
> default frame allocator; in that case the PMR arena is used for the DISPATCH NODE and the
> SLOT-OBSERVATION FLAG — not the coroutine frame itself (asio::awaitable<> frame alloc is opaque to the
> session PMR layer)."*

HALO (coroutine-frame elision) is what gives the **in-strand** session hot path its zero-global-alloc
property; it **cannot fire for a genuine cross-thread offload** (the strand→pool→strand control flow is
non-deterministic to the compiler). So *any* real FileStore offload — by `co_spawn`, by the existing
`cancellable_dispatch`, or by a hand-rolled awaiter — incurs at least one PMR-opaque frame allocation per
cross-thread op. **This is the same cost the shipped TLS cert-signing offload already pays and Gate A
already accepted.**

**`[const §XV.1]` disposition** (PASS — the fix is a strict allocation **improvement**, grounded in
verbatim primaries + an apples-to-apples probe, not a gloss):

- **§XV.1 verbatim** (`constitution.md:209`): *"**Heap-allocate per message or per field on the hot
  path.** Use zero-copy views; arena/PMR for the rare materialise cases."* — it is **hot-path-scoped**
  (not categorical), and it prescribes arena/PMR as the remedy. **§XV.4 verbatim** (`:212`):
  *"Synchronous disk I/O on every send … Async journal with background flush; sync-on-failover is
  opt-in."* — a **separate** banned item, the one this feature fixes.
- **Apples-to-apples probe** (`research/probes/cospawn_probe4.cpp`, 5000 warm iters, global `new`/op):
  the **currently-shipped** `co_await asio::post(file_io_executor, use_awaitable)` + paired rebind that
  `FileStore::store` uses today costs **4.001 global `new`/op** (two inert posts), and its body never
  reaches a pool thread (`on_other = 0/5000` — the inert bug). The nested-`co_spawn` fix costs
  **1.001 global `new`/op** and runs genuinely on the pool (`on_other = 5000/5000`).
- **Precedent (concrete, not assumed)**: the merged-and-008-alloc-gate-passing `FileStore::store`
  **already allocates ~4 global `new`/op**. The 008 seam-14 alloc gate (`tests/perf/test_store_alloc_guard.cpp`)
  gates **zero** global heap on **`MemoryStore::store`** (Test 1) and on **`FileStore::retrieve`**
  (Test 2) — there is **no** zero-global-alloc gate on `FileStore::store`, and the merged 4/op store
  path passes. So §XV.1's "hot path" **operationally excludes `FileStore::store`** (else the merged gate
  would already fail). The fix is therefore a **strict 4×→1× reduction**, not a regression — it *cannot*
  be a §XV.1 violation it improves on.
- **Idiom is project-blessed**: `src/transport/asio_tls_transport.hpp:45–51` records the D-18 rule
  verbatim — *"To pin a coroutine body to a different executor, use nested `co_spawn(other_exec, fn,
  use_awaitable)` … ALL Transport coroutines must be co_spawn'd on `exec_`."* The fix applies the same
  blessed pattern to FileStore.
- **Alloc gate for this feature**: assert `FileStore::store` global `new`/op **≤ the shipped baseline
  (i.e., strictly fewer; target 1/op)** via the seam-14 harness, and keep **`MemoryStore` at zero**
  (unchanged). The remaining 1 frame alloc/op is PMR-opaque (asio awaitable frame; `bind_allocator`
  can't route it — probe v2/v3) and dominated by the `fdatasync` (~150 µs) already on the path; routing
  it to `store_arena` would require a custom-promise coroutine or a persistent worker and is **not
  pursued** (the fix already improves the merged baseline; Karpathy "simplest correct thing").

**Fallback if Gate A rejects the per-op frame** (design sketch, not chosen): a **persistent per-store
I/O worker** — spawn one long-lived coroutine on `file_io_executor` at `open_log()`, fed by a bounded
single-producer handoff; store/next_seqnum/reset enqueue a syscall request + completion and await. Zero
per-op frame, at the cost of a worker lifecycle, a handoff primitive, and its own teardown ordering. Held
in reserve; recommend against unless Gate A insists, per the Karpathy "simplest correct thing" rule.

**Alternatives rejected**: (a) `bind_allocator` on `co_spawn` — empirically does not route the frame.
(b) hand-rolled `async_initiate` two-post — strictly worse (2 allocs) and reimplements what `co_spawn`
gives for one. (c) keeping the inert idiom + documenting — the rejected Option B (PR #118), blocked at
Gate B as a constitutional violation a doc cannot bless.

---

## Decision 2 — Cancellation under a non-interruptible syscall (FR-004 / SC-006)

**Decision**: Keep the **single cancellation checkpoint where it already is — the `async_mutex` acquire**
(`file_store.cpp:794` store, `:998` next_seqnum, `:1050` reset; `async_lock()` returns
`unexpected{sync_lock_aborted}` when the slot is signalled before acquisition). Once past the mutex, the
offloaded syscall runs to **durable completion uninterruptibly** (a `pwrite`/`fdatasync` on a worker
thread is not an asio suspension point). Therefore:
- cancellation **before** the linearisation point ⇒ it was observed at mutex-acquire ⇒ `store_cancelled`,
  no state change (no `co_spawn` issued, no syscall). Matches §6.1.4 + FR-020.
- cancellation **after** the offload began ⇒ the syscall completes durably ⇒ normal success with durable
  state. Matches §6.1.4 + FR-020.

**Open item to verify empirically before/at implement (RED test first)**: what
`co_await asio::co_spawn(pool, fn, use_awaitable)` does when the **outer slot is already signalled** at
the suspension point — does it (i) reap before dispatching the child (child never runs) or (ii) run the
child and surface `operation_aborted` on resume? Either is contract-safe **provided** a durable success is
not lost: if (ii) and the syscall already linearised, the thrown `operation_aborted` MUST be caught and
converted to the durable result (the `[[feedback_async_mutex_us3_asio_cancel_and_subagent_seams]]`
try/catch pattern). Because the mutex-acquire is the only checkpoint and the child is issued only after
the mutex is held, case (i) is expected and the try/catch is then defensive, not load-bearing — but the
test decides, and the code follows the test.

**`co_spawn` cancellation-type trap**: `asio::co_spawn` defaults to **terminal-only** cancellation and
silently filters `cancellation_type::total` (`[[feedback_asio_cospawn_total_cancellation_default]]`).
`Engine::stop()` emits `total`. This does **not** create a hang here because teardown drains by **awaiting
completion** of the store-op (Decision 5), not by cancelling the in-flight syscall — but the nested
`co_spawn` MUST NOT install a cancellation path that swallows `total` and wedges. Verified by the
shutdown-ordering seam (SC-007). The linearisation/cancellation taxonomy is otherwise unchanged from the
approved §6.1.4 table.

---

## Decision 3 — Strand-confined `impl_` mutation (Clarifications 2026-06-13)

**Decision**: The nested `co_spawn` lambda captures and touches **only** the raw syscall arguments by
value — the `OsFile` handle/fd, the file offset, and a `std::span`/pointer+len into the already-populated
`store_scratch_`/`retrieve_scratch_`/counter buffer. It performs the syscall and returns the result.
**Every `impl_` mutation stays in the outer coroutine on the session strand**, before or after the
`co_await`:
- `store`: the frame deep-copy into `store_scratch_` (`:820`), the seqnum increment (`:835–839`),
  `write_pos` update (`:846`) — all on the strand; only `write_frame`'s `pwrite` + `datasync` run in the
  nested lambda.
- `next_seqnum`: the in-memory counter increment (`:1012`) and `write_pos` update (`:1023`) on the strand;
  only `write_counter`'s `pwrite` + `datasync` in the lambda.
- `reset`: **the live-handle swap `impl_->file = std::move(new_file)` (`:1149`) and the in-memory index
  clear + counter reset (`:1205–1210`) run on the strand**; only the tmp-open / sentinel-write / `fdatasync`
  / `rename` / parent-dir-`fsync` syscalls run in the lambda (against a local `OsFile`, not `impl_->file`).

**Consequence**: `impl_` is mutated only on the session strand ⇒ single-threaded ⇒ **no data race on
`impl_` by construction**. Since store/next_seqnum/reset are mutually exclusive via the writer mutex, and
retrieve's reads stay on the strand (Decision 4), the only thread touching the pool is the raw syscall
against by-value args. This is the design the Clarifications fixed and it dissolves the I-03 cross-context
race without a separate lock. TSan (SC-005) is the witness.

---

## Decision 4 — Mid-walk `reset()` detection (FR-006 / I-03)

**Decision**: Add a `std::uint64_t generation_` (or reuse an epoch) field to `FileStoreImpl`, **bumped by
`reset()`** on the strand at the live-handle swap. `retrieve()` snapshots `generation_` at index-snapshot
time (inside the mutex, `:908–932`) and **re-checks it before each frame read** in the walk
(`:940–974`); a changed generation ⇒ a `reset()` ran during the walk ⇒ fail the walk cleanly. `retrieve`'s
`pread` stays on the session strand (Clarifications), so the read and the (strand-confined) handle swap
cannot physically overlap; the generation check covers the **logical** hazard that the walk suspends at
each `visitor.on_frame()` `co_await` (`:957`), during which a `reset()` can run and swap/truncate the log,
making the snapshot's offsets stale.

**Clean-failure error**: 008 FR-021 freezes the store error set at **exactly 10** `store_*` variants
(`error.hpp:165–230`), so a new `store_reset_during_retrieve` is **out** (breaching FR-021 is itself a
Gate-A blocker). **Decision: reuse `store_io_failure` (56)** — a `retrieve()` read whose live handle was
swapped/truncated by a concurrent `reset()` is an I/O-state failure of the read. This deliberately does
**NOT** reuse `store_seqnum_gap` (57, "retrieve over a never-persisted gap"): keeping the two distinct
prevents a reset-race from masquerading as a logical gap and masking a real gap bug (advisor note). The
discriminating test asserts the reset-race path yields `store_io_failure`, while the FR-017 logical-gap
path keeps yielding `store_seqnum_gap`. (Gate-A-confirmable; if Gate A prefers, the alternative is to
make the interleave structurally impossible — rejected here because it would force holding the mutex
across the visitor `co_await`, violating FR-017.) (Reachability note: under
the real session FSM a `reset()` mid-resend is not expected to occur — `reset()` happens at logon, not
during a resend walk — so this guard is primarily defensive against the **test-double** driving the two
concurrently; it is witnessed there, per the 008 scripted-test-double seam pattern, avoiding an
unwitnessed guard per `[[feedback_symmetric_api_claim_unreachable_arm]]`.)

---

## Decision 5 — Teardown: drain-before-join (FR-007 / SC-007)

**Decision**: The nested `co_spawn` MUST use `use_awaitable` (the offloaded op is **awaited**, hence in
the join chain) — **never** `co_spawn(..., detached)`
(`[[feedback_detached_cospawn_write_not_in_join_counter]]`: a detached pool write is invisible to the
join counter → UAF when the store is torn down mid-write). Because each store op is awaited by its caller
(the send coroutine / the FSM driver), and `Engine::stop()` (`engine.cpp:1184–1333`) drains all role
loops and in-flight `send` coroutines before returning (steps 3 at `:1303–1329`), every in-flight
FileStore offload completes before `stop()` returns. The `file_io_executor` pool is **application-owned**
(passed via `EngineConfig::file_io_executor`, `engine_config.hpp:160` — the engine does **not** join it),
so the contract is: **after `Engine::stop()` returns, no FileStore pool work is in flight; the application
may then join the pool.** This feature documents that contract (operator docs / B&L) and proves it with a
shutdown-ordering seam: in-flight offloaded `store()` + `Engine::stop()` under ASan/TSan, asserting no UAF
and no use-of-joined-pool. **Verify**: that draining the `send` coroutine truly awaits the nested
`co_spawn` to completion (it does, since `store()`'s outer `co_await` does not return until the child
completes) — covered by the seam.

---

## Decision 6 — The leading "pump-break" posts (vestigial check)

**Decision**: Investigate (do not blindly retain) the leading `co_await asio::post(session_ex,
use_awaitable)` at `file_store.cpp:787` (store), `:991` (next_seqnum), `:1043` (reset). Their stated
purpose is to "break the recursive awaitable_thread pump chain" (RC#4). With the offload restructured to
nested `co_spawn`, determine whether they are still required or are vestigial compensation for the inert
pattern. If vestigial, remove them (a `/simplify` / Gate-A target if left in). If load-bearing (e.g., they
guarantee the method starts on the strand before acquiring the async_mutex), retain with a comment citing
why. Resolved at implement against a focused test; recorded in the Gate-B notes if it turns out to be a
mechanism deviation. The **paired rebind posts** (`:830/843/851/859/869` store, `:946/953` retrieve,
`:1020/1025/1030` next_seqnum, the many in reset) are **removed** by the restructure — with nested
`co_spawn(use_awaitable)` the resume-to-strand **is** the `co_await` completion; there is no separate
rebind post (this corrects the plan's "post-syscall rebind" phrasing — there is no such separate step).

---

## Cross-cutting: what does NOT change

- The on-disk format (single append-only log, per-record CRC32, sentinel, atomic-rename reset) — byte
  identical (008 SC-002 crash-survival must still pass).
- The `async_mutex` writer contract — all of store/next_seqnum/reset still acquire it; retrieve still
  snapshots-under-mutex-then-releases (FR-017). `[const §XI.5]` / §XV.9 preserved.
- The public `MessageStore` / `retrieve_visitor` / factory signatures, `EngineConfig`/`FileStore::Config`
  fields, the wire, and `MemoryStore` — all unchanged (FR-009).
- The per-method linearisation points (§6.1.4): store@`fdatasync` return, next_seqnum@counter-`pwrite`,
  reset@parent-dir-`fsync`/`MoveFileExW`. The offload moves *where* the syscall runs, not *when* it
  linearises.
