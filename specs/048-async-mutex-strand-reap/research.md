# Phase 0 Research — async_mutex strand-local drain-reap simplification

Decisions, the reachability proof the whole feature rests on, the OOM fail-closed design, and the alternatives. Source-cited against branch `048-async-mutex-strand-reap` (= main = shipped `async_mutex.hpp`).

---

## D-1 — Narrow `cancel_and_drain()` to the strand-serialized contract

**Decision:** The advertised cross-thread drain contract is narrowed to **strand-serialized (single-thread-per-mutex) topology only**. The cross-thread "convergence" machinery is removed.

**Rationale — the reachability proof (the load-bearing fact):**
- The cross-thread machinery (latch/channel + `active_acquirers_count_` + the `draining_`↔counter handshake + the async quiescence park, reaper step (h), `async_mutex.hpp:1164-1199`) exists *only* to let the reaper wait for acquirers/holders/resumptions running on **other threads** to quiesce. With a single strand there are no other threads to wait on.
- **All four production consumers are strand-serialized** (Explore inventory §F): `Session::write_gate_` (acquire `session.cpp:504`, drain `session.cpp:1524`), `SeqnumManager::mutex_` (six call sites `seqnum_manager.cpp`, drain `:1542`→`seqnum_manager.hpp:146`), `memory_store::mutex_` (`memory_store.hpp:142/262/362/390`), `file_store::mutex_` (`file_store.cpp:1013/1243/1353/1457`). Each runs on the per-session strand (`session.cpp:344` `make_strand`; `session.hpp:160-167` foreign-thread invocation is UNDEFINED).
- **Cancellation emission is itself marshaled onto the same strand:** `engine.cpp:1255-1281` deliberately `co_spawn(*entry.session_strand, [&]{ entry.session_cancel.emit(total); })` rather than emitting from the control strand, with the explicit comment that a control-strand emit "would race on the slot's handler — TSan: data race in `cancellation_signal::emit` vs `cancellation_slot::prepare_memory`". `root_cancel_.emit` (`session.cpp:1455`) is inside strand-run `close()`.
- ⇒ acquire, drain, AND cancel-emit all execute on the one session strand → **never truly concurrent in production.** The 047 W-B1 residual orphan (a genuine multi-threaded lost-wake, 3/25 standalone) is therefore not production-reachable; it is dead-but-broken capability.
- **Only escape hatch:** a consumer in `direct_executor` mode that *attests* serialization but supplies a genuinely-concurrent executor — a documented **UNDEFINED** contract violation (INV-2), not a supported path.

**Alternatives rejected:**
- *Root-cause-fix the cross-thread orphan (047 option 1):* the 047 converging-loop already implemented "reap→quiesce→confirm" and still leaks ~0.06%/round; the residual is below the invariant model (asio channel/weak-memory). 3 Gate-A rounds already spent. Poor ROI.
- *Keep the machinery, quarantine the witness:* leaves a known lost-wake and the maintenance burden of a Dekker/latch protocol for a capability nothing uses.

---

## D-2 — Synchronous reap + bounded strand-local holder-yield loop

**Decision:** `cancel_and_drain()` reaps queued waiters **synchronously** (no async park), then yields the strand in a **bounded loop** only to let a *pre-drain holder*'s `unlock()` run, then finalizes.

**Why a residual async step remains:** the one thing a strand-local drain still cannot do synchronously is force a *pre-drain holder* to release. A holder is a coroutine that was granted the lock and is suspended at some `co_await` (it yielded the strand); it will resume later on the same strand and call `unlock()`. So the drain must yield the strand to let that continuation run. This is **strand-local** (the holder resumes on the same strand) and needs no cross-thread latch — a simple `while (active_holders_count_ > 0) co_await asio::post(executor, asio::use_awaitable)` suffices.

**New `cancel_and_drain()` shape:**
1. `co_await reset_cancellation_state(asio::disable_cancellation{})` — teardown must run to completion (replaces the removed abort path; matches `close()`'s existing `disable_cancellation`, `session.cpp:1331`). **Contract change:** the drain is no longer interruptible (no `sync_lock_aborted` return) — anchored in E-6 / §4.7.
2. Idempotency: if `draining_` already set, return ok (re-entrant drain on the strand is a no-op). `drain_in_progress_` retained as a cheap guard.
3. `draining_.store(true, release)` — gates new acquirers (they fast-fail `sync_lock_drained` at `async_lock` entry, `:780/:868`).
4. **Synchronous reap:** exchange `state_`→`locked_no_waiters` and `next_drain_head_`→`nullptr`; reverse LIFO→FIFO; for each waiter CAS `queued→cancelled`, set `result_ = unexpected{sync_lock_drained}`, `schedule_record_resume(record)` (posted, E-3). One pass.
5. **Bounded holder-yield:** `while (active_holders_count_ > 0) co_await asio::post(executor, use_awaitable)`; after each yield, re-exchange+reap any waiters the holder's `unlock()` spliced into `state_`/`next_drain_head_` (the holder's `unlock()` sees `draining_` and short-circuits to `state_ = not_locked` + would otherwise leave residuals — re-reap them). Bounded by holder count (O(1) on v1.0 hot paths).
6. **Finalize:** CAS `state_ locked_no_waiters→not_locked`. No `signal_release`, no latch clear.

**Alternative considered — pure-synchronous with a no-holder precondition:** require callers to guarantee no holder at drain time (`close()` already closes the socket so the write completes first, `session.cpp` FQ-A). Rejected as the *primary* design because it pushes a subtle precondition onto every caller and the `seqnum`/store drains do not all pre-close a holder; the bounded yield loop is only a few lines and keeps the holder-wait contract. (The debug assertion in D-5 still flags an *unexpected* concurrent holder.)

**Gate-A watch-points (flagged for the review):**
- The reap's `queued→cancelled` CAS still races on-strand with `on_cancel`'s `queued→cancelled` CAS — but both run on the same strand (serialized), so the CAS is single-winner by construction; retained for the unsupported path + defense. (W-1.)
- The holder-yield loop must re-reap post-yield to catch waiters the holder spliced; missing that re-reap would orphan a late waiter (W-2 — the direct analogue of the 006 reap-before-quiesce bug, but now trivially correct because it is synchronous and bounded, no cross-thread window).
- `active_holders_count_` demoted to plain/relaxed: justified because all inc/dec now happen on the one strand (no cross-thread visibility needed). (W-3.)

---

## D-3 — OOM fail-closed for the three escaping allocation sites (US2/FR-003)

**Decision:** make the three `noexcept`-escaping allocation sites fail closed with the **existing** `error::sync_lock_alloc_failed` (`error.hpp:87`, code 44 — already C-ABI-mapped at `:848`; currently used for the explicit-PMR `store_executor` failure, `async_mutex.hpp:849-857`). No new symbol.

| Site | Current (branch) | Resolution |
|---|---|---|
| `reaper_slot.assign` (`:1177`) | reaper's own cancel handler for the async park | **ELIMINATED** — the redesign removes the reaper park, so this site disappears. |
| `inherited_slot.assign` (`:862`) | assigns the parked waiter's cancel handler inside the `noexcept` `async_initiate` completion | **Wrapped fail-closed:** on throw, take the same exit as the `store_executor` failure (`:849-857`) — release refs, complete the handler with `unexpected{sync_lock_alloc_failed}`, return. The waiter never parks. |
| resume `asio::post` (`:614`, inside `resume_fn_` which is `noexcept`) | posts the resumption runner (E-3 always-post) | **Made non-allocating:** bind a per-waiter **pre-reserved associated allocator** (storage in `waiter_record`) to the runner so `asio::post` draws from owned memory and cannot allocate/throw. Fixes BOTH the drain-resume and the pre-existing normal-`unlock()`-grant-resume terminate (the post-at-614 is reached on every grant, not only on drain). |

**Why the resume post needs the pre-reserved allocator, not a try/catch:** the post is the *only* way to deliver the result to a parked waiter; if it could fail there is no second channel to wake the waiter (circular). E-3 forbids inline resume (re-enters asio frame chaining → UAF, TSan-witnessed `sync_fifo_fairness`/`sync_cancellation_mid_wait`). The correct fix is to make the post *non-allocating* so it cannot fail — the `waiter_record` is already alive and already carries inline buffers (`exec_storage_` 64 B, `slot_storage_` 32 B per §4.2); add a small `post_storage_` buffer and a `slot_allocator`-style allocator bound via `asio::bind_allocator` on the runner. Size budget tracked in data-model.md (the awaiter byte-budget is § the mutex/record side, not the ≤96 B awaiter — `waiter_record` is the heap/pool record, not the embedded awaiter).

**Gate-A watch-point:** the pre-reserved buffer must be sized for asio's posted-op storage for the runner closure on both libstdc++ and libc++; data-model.md pins a measured size + a static_assert. If a platform's posted-op storage exceeds the buffer, fall back to `sync_lock_alloc_failed` at the *grant* decision (synchronous, on-strand — catchable) rather than terminating. (W-4.)

---

## D-4 — Retain the per-waiter phase CAS arbitration

**Decision:** keep `on_cancel` + `phase_` `{queued,granted,cancelled}` + the `queued→{cancelled,granted}` CAS (`async_mutex.hpp:737-739, 971, 1026, 1126`). Even strand-local, a parked write-gate waiter can be cancelled when `close()` emits `root_cancel`/`session_cancel` on the strand; the CAS guarantees single-winner resume (no double-resume, no lost-wake) between `on_cancel`, the grant path, and the reap. FR-004.

---

## D-5 — Misuse detection (US3/FR-006)

**Decision:** documentation-primary + a **debug-build assertion** that `cancel_and_drain()` (and, where cheap, `unlock()`) runs on the bound executor. Asio strands expose `running_in_this_thread()`; where the executor is a strand we assert it, `#ifndef NDEBUG`. No release-build gate (hot-path cost; `direct_executor` attestation cannot always be checked). The cross-thread WB witness from 047 is **repurposed** to assert that genuinely-concurrent drive trips the debug assertion / is rejected — it is NOT deleted-to-green (that would enshrine the removed bug, `[[feedback_coverage_push_enshrines_bugs]]`).

---

## D-6 — Design-doc amendment (`.specify/2f-async-mutex.md`)

**Decision:** add **Erratum E-6 — strand-local reap** and amend §4.7 to state the narrowed contract; **retire the E-5 / I-33 cross-thread-convergence claims** (047) and the `drain_latch_state` (§3.1 / §4.7.2 / §4.7.3) machinery rows as *superseded by E-6*. The invariant set drops I-33 (convergence) and the cross-thread edges; the new invariant is **"the drain runs on the owning strand; the reap is a synchronous single pass; the only yield is to let a pre-drain holder unlock, bounded by the holder count."** This amendment is an implement-phase task (T0xx), but the *design* is fixed here for Gate A.

---

## Open items carried to Gate A (explicit watch-list)
- W-1 on-strand `queued→cancelled` CAS single-winner (D-2).
- W-2 post-yield re-reap completeness (D-2) — the synchronous analogue of the 006 bug; argue it is closed by construction (no cross-thread window).
- W-3 `active_holders_count_` relaxed-ordering justification (single strand) (D-2).
- W-4 pre-reserved post-storage sizing on libstdc++ AND libc++ + the synchronous fallback (D-3).
- W-5 contract change: drain is now uninterruptible (no `sync_lock_aborted`); confirm no supported caller depends on cancelling a drain (close() disables cancellation — `session.cpp:1331`).
