# fixpp#624 — a `FileStore` offload's completion and the caller's execution context (B57)

> **Status: v0.1, implementation note (2026-10-27).** It records decision D-624-1, built in
> `offload_to` (`src/session/file_store.cpp`), and the alternatives it rejects. It is a sibling of
> `.specify/536-537-538-recovery-liveness.md` §11 (fixpp#598), whose decisions D-598-1 and D-598-2 it
> keeps. That note belongs to a merged batch (B28) with its own Gate A history, so this decision is
> written here rather than as a new subsection there. Batch B57 also carries fixpp#611 and fixpp#599;
> their decisions are not in this version.
>
> *Read* means read from source: fixpp at this branch's head, and asio at the version the build compiles
> against (`asio/version.hpp`). *Run* names a cell or command a reader can re-run. Neither records a
> count.

## 1. The condition

A `FileStore` call that writes (`store`, `next_seqnum` with `increment` set, `reset`, `reset_to`,
`flush_for_session_close`) hands its blocking system calls to the file I/O executor through
`offload_to`, and is resumed on its own (the caller's) executor when they are done. The hand-back runs
on the file I/O thread.

The host is allowed to destroy the caller's execution context once every store call on it has
returned, while the file I/O pool lives on. `FileStore::Config::file_io_executor`'s precondition
(`include/fixpp/session/file_store.hpp`) asks only that the file I/O pool keep running its jobs until
every store call has returned; it never asks the host to join that pool before its own context goes.
A file I/O pool shared by several engines is the plain case.

## 2. What happened before D-624-1 (read; run)

`offload_to` was `co_await asio::co_spawn(pool_ex, body, bind_cancellation_slot({}, use_awaitable))`.
In asio's `co_spawn_entry_point` (`asio/impl/co_spawn.hpp`), after the body has returned, the file
I/O thread calls `(dispatch)(s.handler_work.get_executor(), …)`. `s.handler_work` is a work-tracking
copy of the caller's executor, taken by `co_spawn_state`. The dispatch makes the completion runnable
on the caller's context; the caller can then resume, return from the store call, and its context can
be destroyed. Only after that does the file I/O thread destroy the dispatch's temporaries
(`initiate_dispatch_with_executor`, `asio/detail/initiate_dispatch.hpp`) and then the coroutine frame
that owns `s`. Each tracked copy's destructor calls the caller scheduler's `work_finished()`, and the
last reference to a strand runs `strand_executor_service::strand_impl`'s destructor
(`asio/detail/impl/strand_executor_service.ipp`), which uses the caller's strand service. Both were
reported by ASan as a heap-use-after-free on the file I/O thread.

Run: `tests/session/test_624_offload_completion_lifetime.cpp`, the cells `CallerPool_*` and
`CallerStrand_*`, against the tree before D-624-1 (the RED cell's own commit carries the form of the
cell that ran there).

## 3. D-624-1 — the caller waits for the job's last touch

**The statement.** `offload_to` hands `pool_ex` a job of its own, initiated with
`asio::async_initiate` under `bind_cancellation_slot(cancellation_slot{}, use_awaitable)` and posted
to `pool_ex`:

1. On the caller's side the initiation converts the caller's executor, there and nowhere else:
   `prefer(require(get_associated_executor(handler), blocking.never), outstanding_work.tracked)`.
   `blocking.never` keeps the hand-back from running inline; the tracked work keeps the caller's
   context running while the job does.
2. The job runs the body under `store_offload_exit_guard`, keeping its result or the exception it
   threw (`std::exception_ptr`).
3. In a scope that closes before the job's final action, the job moves the handler and the
   converted executor out of itself into locals, and hands the completion (which owns the handler)
   back through that executor's `execute()`; it never converts the executor again. The local
   executor, and with it the job's tracked work, is destroyed when that scope closes, on the pool
   thread, before the flag. That is safe because the caller is still waiting for the flag, so its
   context is alive, and because the queued completion is itself outstanding work of the caller's
   scheduler (or of the strand that queued it), so releasing the job's work first cannot let the
   caller's context run out of work. Every other reference to the caller the job held (the
   moved-from handler and executor, the hand-back's temporaries) is destroyed in the same scope.
4. The job's final action is a release-store to a `std::atomic<bool>` that lives in `offload_to`'s
   own frame, reached through a raw pointer. After it the job touches nothing: no notify, no member.
   The job object asio destroys after the call operator returns then owns nothing of the caller's.
5. `offload_to`, once resumed on the caller's executor, waits for that flag (acquire load,
   `std::this_thread::yield()`) before it returns; then it returns the body's result or rethrows what
   the body threw.

**The wait's bound.** The wait covers the pool thread's unwind of the job after its hand-back: the
`execute()`'s return and the scope's destructors. It does not cover any I/O, which the job finished
before handing back.

**Why the caller must be the one that waits.** asio's scheduler makes a posted operation runnable and
only then signals its waiters: `asio/detail/posix_event.hpp`, `maybe_unlock_and_signal_one`, releases
the lock and then calls `pthread_cond_signal` on the scheduler's own event. So even an untracked post
from the file I/O thread touches the caller's scheduler after the completion is runnable, and the
caller can by then have resumed. The same holds from the other side for a strand: when the file I/O
thread holds the last reference to a shared executor target, it destroys it. No arrangement of copies
on the pool side removes the last touch; only the caller can make sure it is over.

**Never inline (`blocking.never`, never `dispatch`).** With the caller's own executor as the file I/O
executor, the job runs on the caller's thread (or the caller's strand). A hand-back that could run the
completion inline from there, a dispatch or an `execute()` without `blocking.never`, would resume the
caller inside the job, before the flag is stored, and the caller's wait would never end.

**Kept from fixpp#598.** D-598-1: the await's token is bound to an empty cancellation slot and the
initiation connects nothing, so no cancellation reaches the job. D-598-2: the call returns the body's
own result, or rethrows what the body threw; nothing is caught and turned into a success.

**Where the flag lives.** In the awaiting frame. So the caller's execution context must not be
destroyed while a store call is in flight — the direct caller's existing obligation at
`FileStore::Config::file_io_executor` ("co_await every store/flush call to completion").

**Allocation.** Stated as conditions; re-measure rather than cite a figure.
- The offload makes no per-op coroutine frame. The job and the completion are posted operations
  through asio's recycling allocator for `std::allocator` handlers.
- On a caller whose executor `asio::any_io_executor` holds as a shared target (a
  `fixpp::core::session_executor`, or a strand inside an `any_io_executor`), each property conversion
  of that executor builds a new target, which allocates. The offload converts that executor a bounded
  number of times per op, all on the caller's side (step 1), and the hand-back converts it no further.
- That was already so before D-624-1: `co_spawn_state`'s work guard converted the caller's executor
  per op, besides the per-op frame.
- To measure, run the arms of `StoreAllocGuard.FileStoreStoreWindow_*` (among them
  `_SessionExecutorCaller`, the shared-target shape) under the interceptor as
  `tests/perf/CMakeLists.txt` shows, on the tree before a change and on the tree after it, and compare.

**Constitution.** `[const §XV.1]`'s §XV.4-offload exemption (amended v0.2) permits a single bounded
O(1) coroutine frame per offloaded I/O op. It is a ceiling, and D-624-1 allocates no per-op coroutine
frame, so the constitution is not amended. `spec/behaviors-and-limitations.md` `B-035-1` states the
mechanism and the allocation as conditions, and `B-624-1` states what a host may now do.

**Not handled: an allocation failure in the job's hand-back.** Read, not run. If the job's
`asio::post` to the caller's executor throws (`std::bad_alloc` from the posted operation, or from a
property conversion on an executor stored as a shared target), the exception leaves the job's call
operator before the flag is stored, so the job touches no caller frame afterwards. Unwinding destroys
the completion object, and with it the handler, on the file I/O thread; the handler's
`awaitable_thread` destructor (`asio/impl/awaitable.hpp`) then posts the destruction of the caller's
coroutine stack to the caller's executor, and that post can fail the same way. The exception then
leaves asio's operation: on an `asio::thread_pool`, `thread_pool::thread_function`
(`asio/impl/thread_pool.ipp`) catches it and calls `std::terminate`; on an `io_context` the host
runs, it leaves the host's `run()`. The awaiting store call is never resumed. Before D-624-1 the
same failure in `co_spawn_entry_point`'s dispatch had the same reach.

## 4. Rejected

1. **Narrowing the host contract: the file I/O pool must be joined before the caller's context is
   destroyed.** It makes a shared file I/O pool, outliving several engines or sessions, a contract
   violation, which nothing a host reads forbids today (§1). It also moves a library defect into every
   host's teardown order.
2. **A tracking-only fix: hand back through an untracked copy of the caller's executor.** It removes
   `work_finished()` on a destroyed scheduler and leaves the strand case: the file I/O thread can still
   drop the last reference to a strand and run `strand_impl`'s destructor against the destroyed
   strand service. The cell `CallerStrand_*` stays RED under it by construction (its hold covers every
   copy the file I/O thread destroys, tracked or not).
3. **No copies on the pool side: an untracked `post` and nothing else kept.** The post itself touches
   the caller's scheduler after the completion is runnable (`posix_event.hpp`,
   `maybe_unlock_and_signal_one`: the signal follows the unlock), so there is still a window in which
   the caller has resumed, returned and destroyed its context while the file I/O thread is inside the
   post.

## 5. The cells (run)

`tests/session/test_624_offload_completion_lifetime.cpp`, target
`session_624_offload_completion_lifetime` (labels `session;624`, `FIXPP_TEST_HOOKS`):

- `CallerPool_DestroyedBeforeTheFileIoPool`, `CallerStrand_DestroyedBeforeTheFileIoPool`,
  `ThrowingJob_CallerPool_DestroyedBeforeTheFileIoPool`: the caller's context is destroyed while the
  file I/O pool lives. A test-local executor wrapper holds the file I/O thread inside the hand-off's
  `execute()` until the caller starts its wait (the probe `install_store_offload_caller_wait_probe`)
  or the caller's context is gone. Each asserts that the hold engaged, that the caller started waiting
  while the hold was engaged, and that the call's result (or the job's exception, through the seam
  `arm_force_store_offload_throw_once`) reached the awaiter.
- `SameExecutor_SingleThreadPool_Completes`, `SameExecutor_IoContext_Completes`,
  `SameExecutor_Strand_Completes`: the caller's own executor is the file I/O executor; the call must
  complete within a bound.

**Mutants to re-run** (not committed): delete the caller's wait in `offload_to` (the probe call and
the loop) — the three hazard cells must go RED; replace the job's `asio::post` with `asio::dispatch` —
the three `SameExecutor_*` cells must go RED.

## 6. Documents that describe the superseded idiom

`spec/behaviors-and-limitations.md` `B-035-1` is a live row and is amended in this change. The others
describe `offload_to` as a nested `co_spawn`. They are not edited; each is flagged in place in
`brain/components/message-store-quiescence.md`, "The offload's completion hand-off (fixpp#624)":
`specs/035-filestore-io-offload/research.md` Decision 1, `data-model.md` §2, `plan.md`,
`checklists/api-abi-nfr.md` CHK010; `.specify/563-564-callback-send.md`
§2.6, "How a cancellation reaches the write"; `include/fixpp/session/message_store.hpp`, `store`'s
comment ("before the nested co_spawn that offloads …").

## 7. D-599-1 — an exception after a reset's rename poisons the store (fixpp#599)

**The condition.** `reset_store_to`'s job sets `rename_done` as soon as the rename has replaced the
live log. A failure the job RETURNS after that takes Region 3's poison branch. Before D-599-1, a failure
the job THREW did not: the exception left `offload_to`'s await and `reset_store_to` without reaching
Region 3. The store kept the replaced log's descriptor, later writes reported success into an unlinked
inode, and the next open read the fresh log without them. The job built the directory's path for the
directory fsync after the rename, and each of those constructions can throw `std::bad_alloc`.

**The statement.** Both of these:
- (a) Every path and string the steps after the rename need is built before the rename (the POSIX
  branch's directory path). Nothing after the rename allocates. On the Windows branch this already
  held: every path its post-rename steps use is built before the rename.
- (b) `offload_reset_job`, through which `reset_store_to` awaits the job, catches any exception leaving
  the offload and returns false. Region 3 then reads `rename_done` exactly as for a returned failure:
  after the rename the store is poisoned, before it the old log stands, and either way the reset
  returns `store_io_failure`. It does not rethrow. One condition, one observable ending.

(a) removes the known throw; (b) is the guard for the class, so a step added after the rename that can
throw cannot reopen the hole. (b) is a helper rather than a `try` written inside `reset_store_to` so that
the job's body keeps its indentation; `reset_store_to` is its only caller.

**The cell** (run): `ResetThrowAfterRename.PoisonsTheStoreAndReturnsTheResetsError` in
`tests/session/test_611_store_offload_error_arms.cpp`, through the reset-step fault seam
`arm_force_reset_step_fault_once(kResetStepFaultThrowAfterRename)` (FIXPP_TEST_HOOKS), which throws
`std::bad_alloc` right after `rename_done` is set on both branches. The same seam fails the reset's
other steps (initialise_fresh on the temporary log, the rename, the reopen's open and its lock), each
with a cell of its own under `ResetStepArms` (fixpp#611). It asserts that the seam fired, that the reset returns
`store_io_failure` and does not throw, that a later `store()` is refused, and that the reopened log is
the reset's own (no frame, next outbound 1). The Windows branch compiles only under MSVC; its witness
is this cell on POSIX plus the shared structure: the same seam at its `rename_done` site, and the same
`offload_reset_job` and Region 3.

**Mutant to re-run** (not committed): remove (b)'s catch, so that `offload_reset_job` only returns what
`offload_to` returns. The cell must go RED, with the reset passing the exception on and the later
`store()` reporting success.

