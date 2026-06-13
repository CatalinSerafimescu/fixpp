# Feature Specification: Real file_io_executor offload for FileStore

**Feature Branch**: `035-filestore-io-offload`
**Created**: 2026-06-13
**Status**: Draft
**Input**: User description: "Real file_io_executor offload for FileStore — make FileStore's blocking disk I/O genuinely run off the session strand on the file_io_executor, fixing a live [const §XV.4] violation."

## User Scenarios & Testing *(mandatory)*

Context: the engine persists every outbound frame to the configured message store **before**
transmitting it (durable-before-send). When that store is the production `FileStore` under the
default `commit_per_message` policy, persisting a frame performs a blocking `pwrite` followed by a
blocking `fdatasync` on the log file. The signed-off design (`.specify/2e-msgstore.md` §4.3.2; 008
**FR-024**, **I-13**) requires that blocking work to run on a dedicated **`file_io_executor`**
(*"typically a 4-thread `asio::thread_pool` shared across all FileStores"*) so the **session strand
never blocks on `pwrite`/`fdatasync`** — that is the entire basis on which 008 claims compliance with
`[const §XV.4]` (the banned QuickFIX "synchronous disk I/O on every send" pattern).

The shipped implementation attempts that offload with `co_await asio::post(file_io_executor,
asio::use_awaitable)`. That idiom is **inert** (the D-18 anti-pattern banned in 012): `asio::post(other,
use_awaitable)` moves only the *post-completion handler* to the other executor; the coroutine body —
the actual blocking syscall — resumes on the spawning (session) executor. So the session strand
blocks on `pwrite`/`fdatasync` on **every send**, and shipped `FileStore` under `commit_per_message`
(the default) has always violated `[const §XV.4]`. This feature makes the offload **real** so the
implementation matches the approved design. It is a **conformance fix**: it realizes FR-024 / §4.3.2
/ SC-006 as already specified — it does **not** amend them.

This feature is **FileStore-only**. `MemoryStore` does no blocking I/O and is unchanged.

### User Story 1 - The session strand does not block on durable disk writes (Priority: P1)

When a session persists an outbound frame to a `FileStore` under `commit_per_message`, the blocking
`pwrite`/`fdatasync` runs on the `file_io_executor`, not on the session strand. While that durable
write is in flight, the session strand remains free to make progress on other work (other sessions
sharing the engine's executor, heartbeat timers, inbound parsing).

**Why this priority**: This is the constitutional violation and the entire point of the feature.
A single `fdatasync` floor is ~150 µs on commodity NVMe; blocking the strand for that on every send
freezes the FSM (heartbeat, parser, app callbacks) and is precisely the `[const §XV.4]` banned
pattern. Without this, the feature delivers nothing.

**Independent Test**: Drive `FileStore::store(...)` under `commit_per_message` from a session strand
while an independent unit of work is also queued on that strand; instrument so the disk syscall is
observably executing on a `file_io_executor` pool thread (not the strand's thread) and assert the
strand-side work makes progress concurrently with the in-flight write. Equivalently: point the
`file_io_executor` at a real pool and assert, via a thread-id probe captured inside the syscall
window, that the syscall thread is a pool thread and differs from the session-strand thread.

**Acceptance Scenarios**:

1. **Given** a `FileStore` under `commit_per_message` whose `file_io_executor` is a multi-thread pool, **When** `store()` is invoked on the session strand, **Then** the `pwrite`/`fdatasync` executes on a pool thread and the awaitable's completion resumes back on the session strand.
2. **Given** the same store, **When** a durable write is in flight, **Then** the session strand can dequeue and run other ready work before that write completes (the strand is not blocked for the duration of `fdatasync`).
3. **Given** the same store, **When** the write completes, **Then** `store()` still returns success only **after** `fdatasync` has returned (durable-before-send is preserved; the offload does not weaken durability).

### User Story 2 - Every FR-024 offload point is genuine, and the cancellation contract still holds (Priority: P2)

Every blocking disk operation FR-024 enumerates — `store`'s `pwrite`+`fdatasync`, `next_seqnum(_,
increment=true)`'s counter-record `pwrite`+flush, and `reset()`'s `rename`+durability primitive —
runs on the `file_io_executor` and rebinds to the session strand on completion. The per-method
cancellation-result contract (008 **FR-020** / **SC-006**) is unchanged: cancellation that wins
**before** the method's linearisation point completes with `store_cancelled` and no state change;
cancellation that wins **after** completes normally and the new state is durable.

**Why this priority**: A partial fix that offloaded only `store()` would leave `next_seqnum` and
`reset` blocking the strand, and a naive offload could break the cancellation linearisation (a
blocking syscall on a worker thread is not an asio suspension point and cannot be interrupted
mid-call). Both are required for the implementation to actually match the approved design.

**Independent Test**: For each of `store`, `next_seqnum(_, true)`, `reset`: (a) assert the blocking
syscall runs on a pool thread; (b) fire the cancellation slot **at the `async_mutex` acquire, before the
offload is issued** and assert the awaitable completes with `store_cancelled`, no syscall runs, and zero
state change; (c) fire **after the offload is issued / mid-syscall** and assert normal completion with the
state durable, never `store_cancelled`. These are the FileStore specialisations of the existing per-method
cancellation seam (which today exercises `MemoryStore`).

**Acceptance Scenarios**:

1. **Given** a `FileStore`, **When** `next_seqnum(outbound, true)` or `reset()` is invoked, **Then** its blocking disk work runs on the `file_io_executor` and the completion rebinds to the session strand.
2. **Given** a `store()` cancelled **before the offload is issued** (at the `async_mutex` acquire, no `co_spawn` issued), **Then** the awaitable completes with `store_cancelled`, no syscall runs, and there is no durable record.
3. **Given** a `store()` whose offload has been issued (the mutex is held and the syscall is running on the pool), **When** cancellation fires, **Then** the syscall runs to durable completion uninterruptibly and the awaitable completes with normal success and a durable record (cancellation observed at resume is converted to the durable result, never `store_cancelled`).
4. **Given** `reset()` cancelled **at the `async_mutex` acquire (before the offload is issued, no `co_spawn`)**, **Then** no syscall runs, there is no state change, and the awaitable completes with `store_cancelled`. **Given** `reset()`'s offload has been issued (the mutex is held and the `rename`/dir-`fsync` chain is running on the pool), **When** cancellation fires, **Then** the chain runs to durable completion, the reset is durable, and the awaitable completes with normal success — never `store_cancelled`.

### User Story 3 - Concurrent store / retrieve / reset on a real pool cannot corrupt the log or tear the file handle (Priority: P2)

Under the design's multi-thread `file_io_executor`, the offloaded disk operations of one `FileStore`
genuinely run on pool threads concurrently with the session strand's other store calls. Concurrent
access to the store's live log handle and on-disk state MUST be free of data races and MUST NOT
corrupt the log or yield a torn read.

**Why this priority**: The inert offload serialised everything on the session strand, so this entire
concurrency surface was dormant and untested. Making the offload real activates it. Under the final design
`retrieve()`'s `pread` stays **on the session strand** and pool work is raw syscalls only, so a `reset()`'s
live-handle swap (also strand-confined) can never **physically** overlap a read — there is no fd race. The
residual hazard is **logical**: `retrieve()` snapshots the index under the writer mutex but releases it
before walking frames (**FR-017**), and the walk suspends at each `visitor.on_frame()` `co_await`; a
`reset()` can run during that suspension and replace/truncate the log, leaving the snapshot's offsets
**stale**. Reading against a stale snapshot is the defect to guard (008 invariant **I-03**), detected via
the `generation_` guard (FR-006 / data-model §4).

**Independent Test**: Under TSan + ASan, run a `retrieve()` walk concurrently with `store()` calls
(already covered for the in-memory index by the FIFO-fair writer seam) **and** with a `reset()`, on a
real multi-thread `file_io_executor`; assert no TSan report on the live log handle / on-disk state,
no torn read, and that `retrieve()` either reads a coherent pre-reset frame or fails cleanly via the
`generation_` guard — never reads against a stale snapshot whose log a mid-walk `reset()` replaced/truncated.

**Acceptance Scenarios**:

1. **Given** a `FileStore` on a multi-thread pool, **When** multiple `store()` calls and a `retrieve()` walk overlap, **Then** TSan reports no race on the store's internal state and `retrieve()` returns coherent frames (the existing mid-traversal mutation-detection contract of FR-017 still holds).
2. **Given** a `retrieve()` walk suspended at a `visitor.on_frame()` `co_await`, **When** a `reset()` runs on the strand and replaces/truncates the log, **Then** the walk's `generation_` re-check on resume detects the stale snapshot and the walk fails cleanly with `store_io_failure` — it never reads against the replaced/truncated log (the strand-confined `pread` means there is no physical handle race to begin with).
3. **Given** the engine is shutting down, **When** `Engine::stop()` tears the session down, **Then** `stop()` returns only **after** every Session-reachable in-flight FileStore offload (store and the graceful-close flush) has completed, so the application may then safely join its app-owned `file_io_executor` pool with no offloaded disk operation still touching a torn-down store (no UAF).

### User Story 4 - MemoryStore and non-FileStore paths are byte-for-byte unchanged (Priority: P3)

This feature touches only `FileStore`. `MemoryStore` (which does its work synchronously on the
session strand and has no blocking I/O), the public store interface, the wire, the error set, and the
configuration surface are unchanged.

**Why this priority**: Guards against scope creep and accidental regressions in the common
test/embedded (`MemoryStore`) path and on the wire. The fix is internal to `FileStore`'s execution;
nothing observable outside `FileStore`'s threading should change.

**Independent Test**: Run the full existing 008 store suite (round-trip equality, crash-survival,
cancellation contract, latency seams) and assert `MemoryStore` behaviour and all wire/error/config
surfaces are unchanged; assert no public header signature in the store interface changes.

**Acceptance Scenarios**:

1. **Given** a `MemoryStore`, **When** any method is invoked, **Then** behaviour is identical to before this feature (no offload introduced, synchronous on the session strand).
2. **Given** any consumer of the `MessageStore` interface, **When** built against this feature, **Then** no public signature, error variant, or config field has changed (the change is confined to `FileStore`'s internal execution).

### Edge Cases

- **`file_io_executor` is a single-thread context.** The contract is `asio::any_io_executor`; a caller may supply a single-thread executor. The offload MUST still be genuine (syscall off the session strand) and correct (no deadlock) in that configuration.
- **`file_io_executor` queue saturates.** Per §4.3.2 the `store()` awaitable suspends (backpressure propagates to the wire receive); the offload MUST preserve that suspend-on-saturation behaviour rather than blocking the strand or dropping work.
- **Cancellation lands while the blocking syscall is mid-flight.** A `pwrite`/`fdatasync` on a worker thread is not an asio suspension point and cannot be interrupted mid-syscall; the cancellation is observed only when the outer await resumes on the strand, linearised at the durable transition (never a torn/partial durable state).
- **`retrieve()` walk overlapping a `reset()` fd-swap.** The one concurrency hazard the inert offload hid; resolved per Clarifications 2026-06-13 + FR-006 (read stays on the strand; mid-walk-`reset()` detection required).
- **Pool not yet running / not started.** If the supplied `file_io_executor`'s underlying threads are not running, offloaded work cannot complete; behaviour MUST be the documented backpressure/suspend, not silent loss or a strand block. (Engine wiring guarantees a running pool in production; this is a misconfiguration-robustness note.)
- **Engine teardown with in-flight offloaded I/O.** The app-owned pool may be joined only after `Engine::stop()` returns, which it does only once every Session-reachable in-flight offload has completed (User Story 3 scenario 3 / FR-007). Direct (non-Session) FileStore use carries the caller obligation that the executor outlives all outstanding store awaitables.

## Clarifications

### Session 2026-06-13

- Q: Is `FileStore::retrieve()`'s per-frame disk read (`pread`) also offloaded to the `file_io_executor`, or does it stay on the session strand? → A: **Keep it on the session strand.** Offload only the write/reset operations FR-024 enumerates (`store`'s `pwrite`+`fdatasync`, `next_seqnum(_, true)`'s counter write, `reset()`'s `rename`). This is the minimal-scope option faithful to FR-024 (which omits `pread`) and `[const §XV.4]` (which bans I/O only on *every send* = `store`, not the rare resend-path read), and matches reference-engine behaviour (QuickFIX-cpp/J read resends synchronously on the session thread). All `FileStore` `impl_` state mutation is confined to the session strand on completion-rebind, so pool threads execute only raw syscalls and never mutate `impl_` concurrently — which dissolves the cross-context retrieve↔reset/store data race. The orthogonal mid-walk-`reset()` hazard (a `retrieve()` walk suspends at each visitor `co_await`, during which a `reset()` may swap/truncate the live log) is still a required correctness guard (store-generation detection), independent of the read-offload decision.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: `FileStore::store` under `commit_per_message` MUST execute its blocking `pwrite` and `fdatasync` on the configured `file_io_executor`, NOT on the session strand, and MUST resume (rebind) on the session strand on completion. The inert `co_await asio::post(file_io_executor, asio::use_awaitable)` idiom MUST be replaced with a mechanism that genuinely runs the blocking syscall on the `file_io_executor` (realizing 008 **FR-024** / §4.3.2 / **I-13**).
- **FR-002**: The same genuine-offload-and-rebind MUST apply to **every** blocking disk operation FR-024 enumerates: `store`'s append+flush, `next_seqnum(_, increment=true)`'s counter-record `pwrite`+flush, and `reset()`'s `rename`+durability primitive. After this feature, no FR-024 disk operation blocks the session strand. **`FileStore::flush_for_session_close()`'s blocking `fdatasync`/`FlushFileBuffers`** (today synchronous on the session **close** strand) MUST likewise be offloaded via the same mechanism: 2e §4.3.2 frames FileStore disk I/O as `file_io_executor` work, and under `commit_batched`/`commit_interval` the close-flush drains buffered frames and otherwise blocks the close strand. The offloaded write/reset/close set is therefore `store`, `next_seqnum`, `reset`, and `flush_for_session_close` (4 sites); `retrieve()`'s read stays on the strand (FR-006). No new error variant is introduced — a failed close-flush still maps to the existing `store_io_failure`.
- **FR-003**: Durable-before-send MUST be preserved: under `commit_per_message`, `store()` MUST still complete the awaitable with success only **after** `fdatasync`/`FlushFileBuffers` has returned success (the offload changes *where* the flush runs, never *whether* it runs before completion).
- **FR-004**: The per-method cancellation-result contract (008 **FR-020** / **SC-006**) MUST be preserved exactly per the **binary** §6.1.4 contract: cancellation winning **before** a method's linearisation point completes the awaitable with `store_cancelled` and **no** state change; cancellation winning **after** completes normally with the operation's value and the new state durable. There is a **single** pre-linearisation observation point — the `async_mutex` acquire (on the strand, before any `co_spawn` is issued; cancel observed here ⇒ `store_cancelled`, no syscall, no state change). Once the mutex is held and the child is `co_spawn`'d, the blocking syscall is not interruptible mid-call, so it runs to **durable completion**; cancellation arriving after that point is observed (if at all) only when the outer await resumes on the strand, linearised at the durable transition — never a torn or partial durable state, and a durable success is never lost. (§4.3.2's "abort pending I/O at the next `file_io_executor` scheduling point" is satisfied by the mutex-acquire as that scheduling point.) If the outer await surfaces `operation_aborted` (terminal cancellation during teardown) after the syscall has run durably, the implementation MUST **unconditionally** return the durable success, never `store_cancelled`, for a frame already on disk.
- **FR-005**: Concurrent access to a single `FileStore`'s live log handle and on-disk state from `file_io_executor` pool threads MUST be free of data races (TSan-clean) and MUST NOT corrupt the log or produce a torn read, under the design's multi-thread pool. The existing FR-017 mid-traversal mutation-detection contract for `retrieve` MUST continue to hold.
- **FR-006**: `retrieve()`'s per-frame disk reads stay on the **session strand** (they are NOT offloaded to the `file_io_executor` — see Clarifications 2026-06-13). Only the write/reset operations FR-024 enumerates are offloaded. All `FileStore` `impl_` state mutation (index growth, counter update, `reset()`'s live-handle swap) MUST be confined to the session strand on completion-rebind, so the offloaded pool threads execute only raw syscalls against locally-captured arguments and `impl_` is never mutated concurrently from a pool thread. Independently, a `retrieve()` walk spans suspension points (each visitor `co_await`), during which a `reset()` may run on the strand and swap/truncate the live log; `retrieve()` MUST detect a mid-walk `reset()` (e.g., a store-generation guard checked on each resume) and either continue reading a coherent pre-reset frame or fail cleanly with a defined error — it MUST NOT read through a swapped-out/freed handle or a truncated region (008 invariant **I-03**). This mid-walk-reset detection is required regardless of where the read runs.
- **FR-007**: The `file_io_executor` is **application-owned**; the engine does not own or join it. The contract is scoped to **Session/Engine-reachable** store work: after `Engine::stop()` returns, no Session/Engine-reachable FileStore offload operation is in flight — `stop()` drains its role loops and in-flight sends (`engine.cpp:1184–1333`), and each store op (incl. the close-flush) is awaited inside that drained work, so no Session-reachable offload outlives the drain or touches a torn-down `FileStore` (no UAF). The **caller obligation** for *direct* (non-Session) `FileStore` use is that the supplied `file_io_executor` MUST outlive all outstanding store awaitables; `stop()` cannot drain a direct call made outside Session ownership (a misuse note documents this). The positive shutdown-ordering seam proves `stop()` returns only **after** Session-reachable nested `co_spawn(use_awaitable)` work completes.
- **FR-008**: The offload MUST preserve the §4.3.2 saturation/backpressure behaviour: when the `file_io_executor` queue is saturated, the `store()` awaitable suspends (backpressure to the wire receive), rather than blocking the strand or dropping work.
- **FR-009**: `MemoryStore` MUST be unchanged — it has no blocking I/O and continues to run synchronously on the session strand. No public `MessageStore`/`retrieve_visitor`/factory signature, no error variant, no configuration field, and no wire byte MUST change as a result of this feature.
- **FR-010** *(documentation)*: The implementation-side documentation that currently asserts or implies the offload already works MUST be corrected to match reality, **without** rewriting merged Gate-A/Gate-B history: the `message_store.hpp` threading comment, the `specs/008-message-store/contracts/file_store.hpp` offload comments, the `specs/008-message-store/research.md` offload notes, and the false `[const §XV.4]`-passes claim in `specs/008-message-store/plan.md`. The behaviors-and-limitations catalogue MUST record the prior latent violation and its remediation by this feature. This feature **fulfills** the existing FR-024 contract; it does not amend it.

### Key Entities

- **`FileStore`**: the production message store; the only component whose internal execution changes. Its blocking disk operations must run on the `file_io_executor`.
- **`file_io_executor`** (`asio::any_io_executor`): the engine-resolved executor (typically a 4-thread `asio::thread_pool` shared across all FileStores) on which FileStore's blocking disk syscalls must actually run.
- **Session strand**: the per-session serialisation domain that issues store operations; it must remain free to make progress while a FileStore disk operation is in flight.
- **Live log handle / on-disk state**: the `FileStore`'s open file descriptor and append-only log; the shared mutable state that concurrent pool-thread operations and `reset()`'s handle-swap must access race-free.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: For `FileStore::store` under `commit_per_message`, the blocking `pwrite`/`fdatasync` is observed executing on a `file_io_executor` pool thread distinct from the session-strand thread (thread-id probe captured inside the syscall window), in **100%** of stores — i.e., the session strand performs **zero** disk syscalls on the send path.
- **SC-002**: With a durable write in flight on the pool, an independent unit of work queued on the session strand completes **before** the in-flight `fdatasync` returns (the strand is demonstrably not blocked for the flush duration), in a deterministic instrumented test.
- **SC-003**: Durability is preserved — a `FileStore` host SIGKILL'd after N successful `commit_per_message` `store()` calls returns all N frames on restart with **0%** loss (008 SC-002 continues to pass unchanged).
- **SC-004**: The per-method cancellation-result contract holds for `FileStore`: for `store`, `next_seqnum(_, true)`, and `reset`, cancellation **at the `async_mutex` acquire (before the offload is issued)** yields `store_cancelled` with **0** state change, and cancellation **after the offload is issued (mid-syscall)** yields normal completion with durable state, never `store_cancelled` (008 SC-006 extended to FileStore, not only MemoryStore).
- **SC-005**: Under TSan + ASan, concurrent `store` / `retrieve` / `reset` on one `FileStore` over a real multi-thread `file_io_executor` produces **zero** TSan reports on the store's internal state and the live log handle, and **zero** torn reads, across the full seam run.
- **SC-006**: `MemoryStore` behaviour and all wire/error/config/public-signature surfaces are **byte-for-byte / signature-for-signature identical** to pre-feature (the existing 008 MemoryStore and interface seams pass unchanged; no public store signature diff).
- **SC-007**: Engine teardown with in-flight offloaded I/O (a `store()` and a graceful-close `flush_for_session_close` under `commit_batched`) produces **no** UAF and **no** use-of-joined-pool under ASan/TSan: `Engine::stop()` returns only after every Session-reachable offload completes, verified by a shutdown-ordering seam (the app-owned pool is then safe to join).

## Assumptions

- **The design is authoritative and already approved.** `.specify/2e-msgstore.md` (Gate-A-converged) and 008 FR-024 / §4.3.2 / SC-006 fix the offload contract, the multi-thread `file_io_executor` model, and the cancellation linearisation points. This feature realizes them as correct code; where this spec and the design doc disagree, the design doc wins.
- **The execution model is a multi-thread pool, not a free choice.** §4.3.2 documents the `file_io_executor` as *"typically a 4-thread `asio::thread_pool` shared across all FileStores."* The implementation must be correct for that shared multi-thread pool; it may not assume a single-thread executor serialises its operations.
- **The correct offload idiom is the nested-`co_spawn` shape**, not the inert `post(use_awaitable)` hop (012 D-18 / `[[feedback_asio_post_resume_bounces_to_spawn_executor]]`). The exact mechanism is an implementation detail for `/speckit-plan`; the requirement here is that the blocking syscall genuinely runs on the `file_io_executor`.
- **Cancellation cannot interrupt a syscall mid-flight.** A blocking `pwrite`/`fdatasync` on a worker thread is not an asio suspension point; the FileStore cancellation contract is therefore **binary** with a **single** pre-linearisation observation point — the `async_mutex` acquire (FR-004) — consistent with the linearisation-at-durable-transition rule in §6.1.4. Once the syscall is issued it runs to durable completion; a post-linearisation `operation_aborted` is unconditionally returned as the durable result.
- **Industry parity is not the bar.** QuickFIX-cpp / QuickFIX-J perform synchronous FileStore flushes on the calling thread; this feature is the project's deliberate `[const §XV.4]` posture (async journal, no strand block), not a conformance fix to reference engines.
- **No new configuration knob and no wire change.** The `file_io_executor` field already exists on the engine/store config (008 FR-024a); this feature only makes the existing field actually take effect. Nothing observable on the wire changes.
