# Phase 0 Research: Real file_io_executor offload for FileStore

**Feature**: `035-filestore-io-offload` | **Date**: 2026-06-13

All seven decisions below are grounded in (a) the shipped `file_store.cpp` code (file:line from the
implementation map), (b) the approved design `.specify/2e-msgstore.md` §4.3.2/§6.1.4/§6.5 and the amended
`[const §XV.1]` v0.2 §XV.4-offload exemption (`constitution.md:224`), and (c) a standalone asio probe
(sources saved at `research/probes/cospawn_probe{,2,3,4}.cpp`) run against the project's own asio
(`asio6e6c781a0fee4`). The probe verdicts are reproducible.

---

## Decision 1 — Offload mechanism + the per-op allocation (THE Gate-A decision)

**Decision**: Replace each inert `co_await asio::post(file_io_executor, use_awaitable)` (write/reset sites
`file_store.cpp:824` store, `:1015` next_seqnum, `:1068` reset, plus `flush_for_session_close`'s blocking
`datasync` at `:1252` — Decision 7) with the **nested-`co_spawn` shape**:
`co_await asio::co_spawn(impl_->cfg.file_io_executor, raw_syscall_coro(by-value args), use_awaitable)`,
where `raw_syscall_coro` performs **only** the blocking syscall and returns its result. The single
bounded ~48 B coroutine frame this incurs per offloaded op is **compliant by the [const §XV.1] v0.2
exemption** (see disposition), not an accepted violation. (`retrieve()`'s `:942` inert post is **removed**,
not converted — its `pread` stays on the session strand; see Decision 4.)

**Empirical basis** (probe, 5000 warm iterations, global `operator new` counted):

| Mechanism | body-on-pool | resume-on-strand | global `new`/op | arena-routable? |
|---|---|---|---|---|
| inert `post(pool, use_awaitable)` + paired rebind (shipped) | **NO** (runs on strand) | n/a | **4.001** | — |
| nested `co_spawn(pool, fn, use_awaitable)` | **YES** 5000/5000 | **YES** 5000/5000 | **1.001** (~48 B) | **no** |
| `co_spawn` + `bind_allocator(arena, …)` | YES | YES | 1.001 | **no** (frame is PMR-opaque) |
| hand-rolled `async_initiate` (2 posts) | YES | YES | 2.001 | no (in the toy probe) |

**Why the allocation cannot be routed away** (this is the structural §XV.1↔§XV.4 tension the v0.2
amendment was written to resolve): the ~48 B/op is the **child coroutine frame**, allocated by the C++
coroutine machinery via the awaitable promise's `operator new`. asio's `awaitable<>` promise does **not**
route that through the completion token's associated allocator — confirmed by the probe (`bind_allocator`
left global `new` at 1.001, custom allocator never called) **and independently by the project's own
`cancellable_dispatch` header** (`include/fixpp/core/cancellable_dispatch.hpp:33–42`):
> *"When HALO does NOT fire (e.g., cross-thread dispatch …) the coroutine frame is allocated by asio's
> default frame allocator; in that case the PMR arena is used for the DISPATCH NODE and the
> SLOT-OBSERVATION FLAG — not the coroutine frame itself (asio::awaitable<> frame alloc is opaque to the
> session PMR layer)."*

HALO (coroutine-frame elision) is what gives the **in-strand** session hot path its zero-global-alloc
property; it **cannot fire for a genuine cross-thread offload** (the strand→pool→strand control flow is
non-deterministic to the compiler). So *any* real FileStore offload — by `co_spawn`, by the existing
`cancellable_dispatch`, or by a hand-rolled awaiter — incurs at least one PMR-opaque frame allocation per
cross-thread op. That frame is routable to **neither** HALO (cannot fire cross-executor) **nor** a PMR
arena (the asio awaitable frame is opaque to the bound allocator) — the precise condition under which
§XV.4's *mandated* async-journal offload structurally collides with §XV.1's per-message zero-alloc bar.
This collision is why the constitution was **amended** (v0.2, 2026-06-13); it is not a violation the bundle
elects to tolerate.

**`[const §XV.1]` disposition** (COMPLIANT — by the v0.2 §XV.4-offload exemption):

- **§XV.1 v0.2 exemption** (`constitution.md:224`, *"Scope & §XV.4 exemption (amended v0.2,
  2026-06-13)"*): the "hot path" of the per-message alloc ban is the latency-critical **in-memory** path
  (parse → validate → dispatch and `MemoryStore`, which MUST stay zero-allocation); the **durable-store
  async-journal offload mandated by §XV.4** (FileStore offloading `pwrite`/`fdatasync`/`rename` to a
  `file_io_executor`) is **explicitly exempt to a single bounded O(1) coroutine frame per offloaded I/O
  op**, because that completion frame is routable to neither HALO nor a PMR arena (above). The exemption is
  strictly scoped: O(1) frames/op only, no per-field / unbounded / in-memory-path allocation, and **zero**
  on `MemoryStore::store`. The amendment cross-refs §XI.6 (whose "PMR fallback per-awaiter" presumed a
  fallback that does not exist for this cross-executor frame). The nested-`co_spawn` design at exactly
  **1 frame/op** therefore falls squarely inside the exemption — **compliant by the amendment**, not an
  accepted violation.
- **The amendment took the constitutional-authority fork, not a self-grant.** The prior Gate-A round
  rejected the per-op frame's *justifications* (the "no-gate ⇒ excluded", "4→1 vs a live D-18 bug", and
  "TLS-handshake precedent" arguments — none of which a Gate-A rewrite could use to bless a per-message
  global-heap alloc). The resolution was an explicit §XV.1 amendment routed through constitutional
  authority (`/speckit-constitution`, user-signed-off v0.2), which removed the structural conflict at its
  source. This feature is now compliant *because the rule changed*, not because the old carve-out was
  re-argued.
- **Empirical basis** (`research/probes/cospawn_probe4.cpp`, 5000 warm iters, global `new`/op; cited in
  the amendment text): the **currently-shipped** `co_await asio::post(file_io_executor, use_awaitable)` +
  paired rebind that `FileStore::store` uses today costs **4.001 global `new`/op** and its body never
  reaches a pool thread (`on_other = 0/5000` — the inert D-18 bug); the nested-`co_spawn` fix costs
  **1.001 global `new`/op** and runs genuinely on the pool (`on_other = 5000/5000`). The 4→1 reduction is
  a factual improvement, **not** the compliance argument (which is the exemption above).
- **Idiom is project-blessed**: `src/transport/asio_tls_transport.hpp:45–51` records the D-18 rule
  verbatim — *"To pin a coroutine body to a different executor, use nested `co_spawn(other_exec, fn,
  use_awaitable)` … ALL Transport coroutines must be co_spawn'd on `exec_`."* The fix applies the same
  blessed mechanism to FileStore.
- **Alloc gate for this feature** (asserts the exemption bound, not "≤ baseline"): assert
  `FileStore::store` (and the other offloaded ops) allocate **≤ 1 coroutine frame/op — O(1), the §XV.1
  v0.2 exemption ceiling** — on the offload path via the seam-14 harness, and assert **`MemoryStore::store`
  global `new`/op == 0** (unchanged). The 1 frame/op is PMR-opaque (asio awaitable frame; `bind_allocator`
  can't route it — probe v2/v3) and dominated by the `fdatasync` (~150 µs) already on the path.

**Persistent-worker alternative — empirically rejected.** A long-lived per-store I/O worker fed by a
bounded handoff was prototyped; it measured **worse** (≈10 global `new`/op — the handoff request/completion
nodes), not the "zero per-op frame" it was first assumed to give, and adds a worker lifecycle + its own
teardown ordering. The v0.2 amendment removes any need for it. Not pursued.

**Alternatives rejected**: (a) `bind_allocator` on `co_spawn` — empirically does not route the frame.
(b) hand-rolled `async_initiate` two-post — strictly worse (2 allocs) and reimplements what `co_spawn`
gives for one. (c) keeping the inert idiom + documenting — the rejected Option B (PR #118), blocked at
Gate B as a constitutional violation a doc cannot bless.

---

## Decision 2 — Cancellation under a non-interruptible syscall (FR-004 / SC-006)

**Decision**: Per the authoritative §6.1.4 (`:990–1006`) the per-method cancellation contract is **binary**,
with a **single** pre-linearisation observation point — the `async_mutex` acquire. (§4.3.2:690's "abort
pending I/O at the next `file_io_executor` scheduling point" is a descriptive bullet in an argument list,
not a per-method result contract; it is satisfied by treating the mutex-acquire — on the strand, before
any `co_spawn` is issued — as that scheduling point: cancel observed there ⇒ no child issued ⇒ no I/O
pending ⇒ nothing to abort.) The contract is therefore:

1. **At the `async_mutex` acquire** (`file_store.cpp:794` store, `:998` next_seqnum, `:1050`
   reset; `async_lock()` returns `unexpected{sync_lock_aborted}` when the slot is signalled before
   acquisition). Cancellation observed here ⇒ `store_cancelled`, no `co_spawn` issued, no syscall, no
   state change.
2. **After the mutex is held and the child is `co_spawn`'d**, the syscall runs to **durable completion**
   uninterruptibly (a `pwrite`/`fdatasync` on a worker thread is not an asio suspension point);
   cancellation is observed (if at all) only at the outer resume, linearised at the durable transition ⇒
   normal success, durable. This queued-then-run-to-completion direction is the **safe**,
   §XV.15-conformant one: the frame is durable-but-not-yet-transmitted (the FSM has not called
   `transport::async_write`), so the counter and FSM stay consistent and a later peer `ResendRequest` is
   honourable — no silent loss.

A bespoke cross-executor reaper is **not** built: the project's `cancellable_dispatch` is
`session_executor`-bound (it reaps after a single `dispatch(bind_executor(...))` hop and pulls its arena
via `session_arena_of`) and does **not** drop into FileStore's strand→pool→strand round-trip against a bare
`any_io_executor` pool; and the authoritative §6.1.4 contract never asked for a second checkpoint. The
linearisation point itself is unchanged (`fdatasync` return / counter-record `pwrite` + `datasync` /
dir-`fsync`).

If the runtime, on an already-signalled outer slot, runs the child and surfaces `operation_aborted` at the
outer await, the thrown `operation_aborted` MUST be caught and **unconditionally** converted to the durable
result (the `[[feedback_async_mutex_us3_asio_cancel_and_subagent_seams]]` try/catch pattern). Because the
only observation point is the mutex-acquire, any `operation_aborted` seen at the outer await necessarily
post-dates linearisation (the syscall is non-interruptible and has already run to durable completion), so
the catch is **unconditional** — it MUST NOT return `store_cancelled` for a frame that is already on disk
(a false-cancel on durable state is the `[const §XV.15]`-adjacent silent-loss class).

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
so the contract is correctly scoped to **Session/Engine-reachable** store work: **after `Engine::stop()`
returns, no Session/Engine-reachable FileStore offload work is in flight; the application may then join the
pool.** `stop()` cannot drain a **direct** (non-Session) `FileStore` call made by a test-double or a custom
user outside the `send` path — for those, the **caller obligation** is that the `file_io_executor` MUST
outlive all outstanding store awaitables. This feature documents both: the Session-reachable drain
guarantee, **and** a misuse note for direct FileStore use (operator docs / B&L). The positive
shutdown-ordering seam proves `stop()` returns only **after** Session-reachable nested
`co_spawn(use_awaitable)` work completes (in-flight offloaded `store()` + `Engine::stop()` under ASan/TSan,
no UAF, no use-of-joined-pool). *(Gate B r2 correction 2026-06-14: `Engine::stop()` is terminal-only, so the
real drain is verified by code-analysis over `engine.cpp:1184–1333` + the pool-level offload-join invariant
witness `store_shutdown_ordering` (C5a); the graceful-close flush is witnessed separately by the
discriminating `SessionGracefulCloseFlushesFileStore` (C5b). See contracts §C5a/§C5b + quickstart Recipe D1/D2.)* **Verify**: draining the `send` coroutine truly awaits the nested
`co_spawn` to completion (it does, since `store()`'s outer `co_await` does not return until the child
completes) — covered by the seam.

---

## Decision 6 — The leading "pump-break" posts (resolved)

**Decision**: The leading `co_await asio::post(session_ex, use_awaitable)` at `file_store.cpp:787`
(store), `:991` (next_seqnum), `:1043` (reset) are **retained, load-bearing**, with a one-line comment
citing why. They are **not** vestigial compensation for the inert offload: their purpose is to break the
recursive `awaitable_thread` pump chain (RC#4) **and** to guarantee the method body starts on the session
strand before it acquires the `async_mutex` — which is exactly the precondition Decision 2 relies on
(checkpoint (i) is the mutex-acquire **on the strand**; the child `co_spawn` is issued only after the
mutex is held on the strand). Removing them would silently re-point the Decision-2 checkpoint reasoning,
so they stay. This is settled at Gate A (not deferred): they are orthogonal to the offload frame (they
post on the *session* executor, where HALO applies and the pump-break post is not a global-heap frame on
the cross-executor path), so they do not bear on the §XV.1 exemption count. A focused test at implement
asserts the method enters on the strand before mutex-acquire; any deviation is a Gate-B note.

The **paired rebind posts** (`:830/843/851/859/869` store, `:946/953` retrieve, `:1020/1025/1030`
next_seqnum, the many in reset) are **removed** by the restructure — with nested `co_spawn(use_awaitable)`
the resume-to-strand **is** the `co_await` completion; there is no separate rebind post (this corrects any
"post-syscall rebind" phrasing — there is no such separate step).

---

## Decision 7 — `flush_for_session_close()` blocking `datasync` (the missed 5th site)

**Decision**: `FileStore::flush_for_session_close()` performs a **blocking** `impl_->file.datasync()`
(`file_store.cpp:1252`, inside the coroutine at `:1239–1257`) on the **session close strand** — genuine
FileStore disk I/O that 2e §4.3.2 frames as `file_io_executor` work, and which blocks the close strand
under `commit_batched`/`commit_interval` (where the drain has buffered frames to flush). It is **not** a
§XV.4 *every-send* violation (it is a bounded one-shot at graceful close), but it is in-scope for this
feature's stated goal that no FileStore disk syscall blocks a session strand. **Offload its `datasync` via
the same nested-`co_spawn` mechanism** (only the raw `datasync()` runs in the nested lambda; the
`open_ok` check and the `store_io_failure` mapping stay on the strand). So the offloaded write/reset/close
set is **store, next_seqnum, reset, flush_for_session_close (4 sites)**; `retrieve`'s read stays on the
strand (Decision 4).

**It is offloaded but NOT cancellable.** Per 2e §6.2.1:1025 `flush_for_session_close()` is the
graceful-close durability seam: it runs to completion **outside** the cancellable in-flight set, holds
**no writer mutex**, and **never** surfaces `store_cancelled` (only `store_io_failure` on a flush error).
It is therefore **pulled out of the C3 cancellation table entirely** — it gets the genuine offload helper
but is not in the per-method cancellation contract.

Taxonomy is clean: the existing failure mapping already returns `store_io_failure` on a failed
`datasync()` (and `error.hpp:190`'s comment enumerates `flush_for_session_close()` faults), so no new error
variant is introduced — the same variant the mid-walk-reset reuse pins (Decision 4). The shutdown-ordering
seam (Decision 5 / SC-007) is extended to cover **graceful close** (`Session::close(graceful)` →
`flush_for_session_close`) under `commit_batched`, asserting the close-flush is offloaded and drained
before `Engine::stop()` returns.

---

## Cross-cutting: what does NOT change

- The on-disk format (single append-only log, per-record CRC32, sentinel, atomic-rename reset) — byte
  identical (008 SC-002 crash-survival must still pass).
- The `async_mutex` writer contract — all of store/next_seqnum/reset still acquire it; retrieve still
  snapshots-under-mutex-then-releases (FR-017). `[const §XI.5]` / §XV.9 preserved.
- The public `MessageStore` / `retrieve_visitor` / factory signatures, `EngineConfig`/`FileStore::Config`
  fields, the wire, and `MemoryStore` — all unchanged (FR-009).
- The per-method linearisation points (§6.1.4): store@`fdatasync` return,
  next_seqnum@counter-record `pwrite` + `datasync` (`:1024`), reset@parent-dir-`fsync`/`MoveFileExW`. The
  offload moves *where* the syscall runs, not *when* it linearises.
