# Contract — `fixpp::sync::async_mutex` after 048 (narrowed drain)

The public C++ signatures are **unchanged**; this documents the narrowed behavioral contract. The C ABI is unchanged (no new/renumbered error code).

## `async_lock(std::pmr::memory_resource* mr = nullptr) noexcept` → `awaitable<expected_t<async_lock_guard>>`

- Unchanged grant/queue/cancel semantics. `cancellation_type::total` → `unexpected{sync_lock_aborted}`.
- **STRENGTHENED (FR-003):** lock setup now **fails closed** under allocation failure instead of terminating:
  - `store_executor` failure → `unexpected{sync_lock_alloc_failed}` (already shipped).
  - `inherited_slot.assign` failure (cancel-handler storage) → `unexpected{sync_lock_alloc_failed}` (NEW).
  - The resumption post is **non-allocating** (pre-reserved per-waiter storage) → cannot fail; under a platform storage-overflow it degrades to `unexpected{sync_lock_alloc_failed}` at the synchronous grant decision (NEW).
- Called while `draining_` → `unexpected{sync_lock_drained}` (unchanged).

## `unlock() noexcept`

- Unchanged walk (residual FIFO + LIFO grant). The grant resumption post is now non-allocating (no OOM-terminate on the grant path).
- **CONTRACT (FR-006/FR-007):** MUST be called on the owning strand (the strand `async_lock` was awaited on). Debug-asserted where the executor exposes `running_in_this_thread()`.

## `cancel_and_drain() noexcept` → `awaitable<expected_t<void>>` — **NARROWED**

**Supported topology:** the calling coroutine and ALL acquirers/holders of this mutex run on a single shared executor (the per-session strand). This is the only supported topology.

**Behavior (supported topology):**
1. Idempotent: a second concurrent/re-entrant call returns `ok` (the first drains).
2. Rejects all future acquisitions (`sync_lock_drained`).
3. Reaps every currently-parked waiter exactly once with `sync_lock_drained` (synchronous single pass).
4. Waits — by yielding the strand, bounded by the live-holder count — for any pre-drain holder to `unlock()`, then reaps anything that holder spliced.
5. Leaves the mutex in the `not_locked`, fully-reaped terminal state (satisfies the destructor precondition).
6. **Uninterruptible:** the drain disables its own cancellation and always runs to completion. It no longer returns `sync_lock_aborted` (the cancellable-drain path is removed). Callers needing teardown already run with cancellation disabled (`Session::close`, `session.cpp:1331`).

**Unsupported (UNDEFINED) topology:** driving `async_lock`/`unlock`/`cancel_and_drain` for one mutex from genuinely-concurrent executors (e.g., a `direct_executor` that attests serialization but is concurrent — INV-2). Behavior is undefined; in debug builds the on-strand assertion fires where detectable. This MUST NOT be relied upon and is not made "safe" — it is documented unsupported.

**Removed vs prior (047/006):** no cross-thread convergence (`drain_latch_state`/`concurrent_channel`/`async_wait`), no `active_acquirers_count_` epoch, no Dekker handshake, no cancellable-drain abort path. The residual multi-threaded lost-wake (047 W-B1) is eliminated by construction.

## Destructor

- Unchanged precondition: `~async_mutex` requires `not_locked` and empty lists (`std::terminate()` otherwise). `cancel_and_drain()` is the way to satisfy it before destruction.

## Invariants (post-048)

- **INV-A:** the reap is a synchronous single pass on the owning strand; the only suspension is the holder-yield, bounded by `active_holders_count_`.
- **INV-B:** a parked waiter is resolved exactly once — winner of the `phase_ queued→{granted,cancelled}` CAS (grant / on_cancel / reap).
- **INV-C:** no allocation on the lock-setup or resume path can call `std::terminate()`; all fail closed with `sync_lock_alloc_failed`.
- **INV-D (supersedes I-33):** convergence under genuine multi-threaded concurrency is NOT claimed; the contract is strand-serialized-only.
