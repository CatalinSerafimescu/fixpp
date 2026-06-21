# Contract — `fixpp::sync::async_mutex` after 048 (narrowed drain)

The public C++ signatures are **unchanged**; this documents the narrowed behavioral contract. The C ABI is unchanged (no new/renumbered error code).

## `async_lock(std::pmr::memory_resource* mr = nullptr) noexcept` → `awaitable<expected_t<async_lock_guard>>`

- Unchanged grant/queue/cancel semantics. `cancellation_type::total` → `unexpected{sync_lock_aborted}`.
- **STRENGTHENED (FR-003, narrowed scope):** lock setup `inherited_slot.assign` now **fails closed** instead of terminating:
  - `store_executor` failure → `unexpected{sync_lock_alloc_failed}` (already shipped).
  - `inherited_slot.assign` failure (cancel-handler storage) → `unexpected{sync_lock_alloc_failed}` (NEW), via the awaiter/stored-handler path with the exact ref-balance (research D-3).
  - The resumption `asio::post` (`:614`) is **NOT** made non-allocating by 048; it remains a pre-existing OOM-terminate site, deferred as **L-048** (same class as 047's L-047-2). No fail-closed claim on the resume post.
- Called while `draining_` (a NEW acquirer) → `unexpected{sync_lock_drained}` (unchanged). (A reaped/parked waiter, by contrast, resumes `sync_lock_aborted` — see `cancel_and_drain` below.)

## `unlock() noexcept`

- Unchanged walk (residual FIFO + LIFO grant). The grant resumption post is unchanged — it remains a pre-existing OOM-terminate site (L-048); 048 does not alter it.
- **CONTRACT (FR-006/FR-007):** ordinary cross-thread `async_lock`/`unlock` with no drain in flight remains SUPPORTED (the §1.1 cross-domain contention seam). `unlock()` is strand-bound ONLY when a `cancel_and_drain()` may run concurrently on the owning strand — that overlap is the narrowed, unsupported case. `async_mutex` stores no executor, so the strand-overlap rule is **documentation-enforced**; the unsupported overlap is exercised only via a test-only instrumented executor (no production assertion seam — P2-4).

## `cancel_and_drain() noexcept` → `awaitable<expected_t<void>>` — **NARROWED**

**Supported topology (DRAIN ONLY):** `cancel_and_drain()` must be invoked on the owning strand, co-located with all acquire/cancel/unlock of that mutex; a drain MUST NOT overlap an acquire/unlock/cancel running on another thread. There are exactly TWO drain consumers (Session write-gate `session.cpp:1524`; SeqnumManager `seqnum_manager.hpp:146`), both strand-confined. (Ordinary cross-thread `async_lock`/`unlock` is NOT narrowed — see below.)

**DRAIN PRECONDITION:** no current holder is blocked on an unbounded operation (holders release promptly). The two drain callers satisfy it (write_gate: socket closed first so the in-flight write completes; seqnum: never held across real async I/O). The drain is NOT claimed O(holder-count)-bounded; promptness rests on this precondition.

**Behavior (supported topology):**
1. **Reentrancy:** a second `cancel_and_drain()` on the strand does NOT return `ok` eagerly — it awaits the first drain's terminal completion (`while (!draining_complete_) co_await asio::post(executor, use_awaitable)`) then returns the terminal result.
2. Rejects all future (NEW) acquisitions with `sync_lock_drained`.
3. Reaps every currently-parked waiter exactly once with `sync_lock_aborted` (synchronous single pass; shipped result code, FR-007 — NOT `sync_lock_drained`).
4. Yields the strand (under the drain precondition) for any pre-drain holder to `unlock()`, then reaps anything that holder spliced.
5. **Terminal condition:** finalizes ONLY when `active_holders_count_==0` AND `in_flight_resumers_==0` AND both lists (`state_`, `next_drain_head_`) are observed empty in one pass — `in_flight_resumers_` keeps the mutex alive until every posted resumer has dereferenced `record->mutex_` (prevents the immediate-destroy UAF).
6. Leaves the mutex in the `not_locked`, fully-reaped terminal state; sets `draining_complete_`.
7. **Uninterruptible:** the drain disables its own cancellation and always runs to completion. Callers needing teardown already run with cancellation disabled (`Session::close`, `session.cpp:1334`).

**Unsupported (UNDEFINED) — DRAIN OVERLAP ONLY:** overlapping `cancel_and_drain()` with an `async_lock`/`unlock`/`cancel` for the same mutex on a genuinely-concurrent executor (e.g., a `direct_executor` that attests serialization but is concurrent — INV-2). Behavior is undefined. **PRESERVED:** ordinary cross-thread `async_lock`/`unlock` contention remains SUPPORTED — the `.specify/2f-async-mutex.md §1.1` cross-domain O(N) contention seam is not narrowed. There is no production debug assertion (no executor seam, P2-4); the unsupported overlap is exercised only by a test-only instrumented executor.

**Removed vs prior (047/006):** no cross-thread convergence (`drain_latch_state`/`concurrent_channel`/`async_wait`), no Dekker handshake, no cancellable-drain abort path. `active_acquirers_count_` is KEPT as a plain strand-local counter (insurance against the 006/047 lost-wake shape). `in_flight_resumers_` (strand-local) replaces the latch's `in_flight_resumptions_`; `draining_complete_` replaces the channel subscriber protocol. The residual multi-threaded lost-wake (047 W-B1) is eliminated by construction.

## Destructor

- Unchanged precondition: `~async_mutex` requires `not_locked` and empty lists (`std::terminate()` otherwise). `cancel_and_drain()` is the way to satisfy it before destruction.

## Invariants (post-048)

- **INV-A:** the reap is a synchronous single pass on the owning strand; the only suspension is the holder-yield, which terminates under the drain precondition (holders release promptly) — NOT an O(holder-count) bound.
- **INV-B:** a parked waiter is resolved exactly once — winner of the `phase_ queued→{granted,cancelled}` CAS (grant / on_cancel / reap); the winner schedules exactly one posted resume counted in `in_flight_resumers_`.
- **INV-C (narrowed):** the `inherited_slot.assign` lock-setup site fails closed with `sync_lock_alloc_failed`; the resume `asio::post` + the holder-yield post remain pre-existing OOM-terminate sites (deferred L-048). 048 does NOT claim a fully terminate-free lock path.
- **INV-D:** the drain terminal condition is (no holders) ∧ (`in_flight_resumers_==0`) ∧ (both lists empty); the mutex stays alive until every posted resumer has dereferenced it.
- **INV-E (new E-5):** `cancel_and_drain()` overlap with another thread's acquire/cancel/unlock is undefined (strand-confined drain); ordinary cross-thread `async_lock`/`unlock` contention stays supported.
