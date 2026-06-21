# Phase 0 Research — async_mutex strand-local drain-reap simplification

Decisions, the reachability proof the whole feature rests on, the OOM fail-closed design, and the alternatives. Source-cited against branch `048-async-mutex-strand-reap` (= main = shipped `async_mutex.hpp`).

---

## D-1 — Narrow `cancel_and_drain()` to the strand-serialized contract

**Decision:** The advertised cross-thread drain contract is narrowed to **strand-serialized (single-thread-per-mutex) topology only**. The cross-thread "convergence" machinery is removed.

**Rationale — the reachability proof, stated PER INSTANCE PER OPERATION (the load-bearing fact):**
- The cross-thread machinery (latch/channel + the `draining_`↔counter handshake + the async quiescence park, reaper step (h), `async_mutex.hpp:1164-1199`) exists *only* to let the reaper wait for acquirers/holders/resumptions running on **other threads** to quiesce. With a single strand there are no other threads to wait on.
- **There are exactly TWO drain consumers, both strand-confined** (New P2-5): `Session::write_gate_` (drain `session.cpp:1524`) and `SeqnumManager::mutex_` (drain `seqnum_manager.hpp:146`). Each runs on the per-session strand (`session.cpp:344` `make_strand`); cancellation emission is marshaled onto that same strand. So `cancel_and_drain()` never overlaps an acquire/cancel/unlock on another thread on these two instances.
- **The two stores NEVER drain** — `MemoryStore` and `FileStore` only `async_lock`/`unlock` (`memory_store.hpp:142/262/362/390`; `file_store.cpp:1013/1243/1353/1457`); they embed the mutex by value but do not call `cancel_and_drain()`. Direct (non-Session) `FileStore` use is **caller-serialized per `file_store.hpp:116-133`** (documented caller obligation), not on a session strand. So the consumer sets differ: **two drain consumers (strand-confined); four lock consumers.** The narrowing applies ONLY to `cancel_and_drain()` overlap — ordinary cross-thread `async_lock`/`unlock` (incl. the §1.1 advertised cross-domain O(N) contention seam) stays supported.
- **Cancellation emission is itself marshaled onto the same strand:** `engine.cpp:1255-1281` deliberately `co_spawn(*entry.session_strand, [&]{ entry.session_cancel.emit(total); })` rather than emitting from the control strand, with the explicit comment that a control-strand emit "would race on the slot's handler — TSan: data race in `cancellation_signal::emit` vs `cancellation_slot::prepare_memory`". `root_cancel_.emit` (`session.cpp:1455`) is inside strand-run `close()`.
- ⇒ acquire, drain, AND cancel-emit all execute on the one session strand → **never truly concurrent in production.** The 047 W-B1 residual orphan (a genuine multi-threaded lost-wake, 3/25 standalone) is therefore not production-reachable; it is dead-but-broken capability.
- **Only escape hatch:** a consumer in `direct_executor` mode that *attests* serialization but supplies a genuinely-concurrent executor — a documented **UNDEFINED** contract violation (INV-2), not a supported path.

**Alternatives rejected:**
- *Root-cause-fix the cross-thread orphan (047 option 1):* the 047 converging-loop already implemented "reap→quiesce→confirm" and still leaks ~0.06%/round; the residual is below the invariant model (asio channel/weak-memory). 3 Gate-A rounds already spent. Poor ROI.
- *Keep the machinery, quarantine the witness:* leaves a known lost-wake and the maintenance burden of a Dekker/latch protocol for a capability nothing uses.

---

## D-2 — Synchronous reap + strand-local holder-yield + in-flight-resumer barrier

**Decision:** `cancel_and_drain()` reaps queued waiters **synchronously** (no async park), then yields the strand to let a *pre-drain holder*'s `unlock()` run, then finalizes ONLY when the full terminal condition holds (no holders AND no posted-but-unrun resumers AND both lists empty).

**Why a residual async step remains:** the one thing a strand-local drain still cannot do synchronously is force a *pre-drain holder* to release. A holder is a coroutine that was granted the lock and is suspended at some `co_await` (it yielded the strand); it will resume later on the same strand and call `unlock()`. So the drain must yield the strand to let that continuation run. This is **strand-local** and needs no cross-thread latch — `while (active_holders_count_ > 0) co_await asio::post(executor, asio::use_awaitable)`.

**The terminal condition (fixes P1-1 UAF + New P1-N2):** the drain MUST NOT terminate on `active_holders_count_==0` alone. Terminal = `(active_holders_count_==0)` AND `(in_flight_resumers_==0)` AND `(state_` and `next_drain_head_` both observed empty in one pass)`. `in_flight_resumers_` is a **plain strand-local** `std::atomic<std::uint32_t>` member of `async_mutex` (relaxed) — the same role as the shipped `drain_latch_state::in_flight_resumptions_`, moved onto the mutex so it survives removing the latch. It is `++`'d when the reap/grant schedules a posted resume and `--`'d in the resume runner AFTER `release_ref` (`async_mutex.hpp:670-690`). It is THE barrier that keeps the mutex alive until every posted resumer has dereferenced `record->mutex_`; without it a no-holder drain returns before any reaped resumer runs and the caller may destroy the mutex → UAF.

**New `cancel_and_drain()` shape:**
1. **Reentrancy (fixes P1-2):** a second `cancel_and_drain()` on the strand observing `draining_` MUST NOT return ok eagerly. It yields `while (!draining_complete_) co_await asio::post(executor, use_awaitable)` and then returns the terminal result — it awaits the first drain's terminal completion. `draining_complete_` (a plain `std::atomic<bool>` member) is set at finalize. This replaces the removed concurrent-channel subscriber protocol with a strand-local flag.
2. `co_await reset_cancellation_state(asio::disable_cancellation{})` — teardown must run to completion (matches `close()`'s existing `disable_cancellation`, `session.cpp:1334`). **Contract change:** the drain is no longer interruptible — anchored in E-5 / §4.7.
3. `draining_.store(true, release)` — gates NEW acquirers (they fast-fail `sync_lock_drained` at `async_lock` entry, `:780/:868`). (New acquirers keep `sync_lock_drained`; reaped/parked waiters keep `sync_lock_aborted` — New P1-N1.)
4. **Synchronous reap:** exchange `state_`→`locked_no_waiters` and `next_drain_head_`→`nullptr`; reverse LIFO→FIFO; for each waiter CAS `queued→cancelled`, set `result_ = unexpected{sync_lock_aborted}` (shipped behavior, `:1129-1130` — NOT `sync_lock_drained`), `++in_flight_resumers_`, `schedule_record_resume(record)` (posted, E-3). One pass.
5. **Holder-yield:** `while (active_holders_count_ > 0) co_await asio::post(executor, use_awaitable)`; after each yield, re-exchange+reap any waiters the holder's `unlock()` spliced. This terminates promptly UNDER THE DRAIN PRECONDITION (below), NOT by an O(holder-count) bound.
6. **Finalize:** when the terminal condition holds, CAS `state_ locked_no_waiters→not_locked`; set `draining_complete_=true`. No `signal_release`, no latch clear.

**DRAIN PRECONDITION (replaces the false "O(holder-count) bound" — fixes P1-3/P2-3):** `cancel_and_drain()` requires that no current holder is blocked on an unbounded operation — holders release promptly. The two real drain callers satisfy it, verified per-callsite:
- **write_gate** (`session.cpp:1524`): the FQ-A teardown closes the socket at `~1456-1458` so the in-flight write completes with error, and the liveness loop is joined before the drain. No holder is blocked on unbounded I/O at drain time.
- **seqnum** (`seqnum_manager.hpp:146`): the seqnum mutex is never held across real async I/O, so a holder always releases promptly.
The drain still yields to let pre-drain holders' `unlock()` and posted resumers run; under the precondition this terminates promptly. The holder-yield `asio::post` (and the resume post) is an OOM site of the SAME pre-existing class as the shipped channel `async_wait`/resume post (which also allocate) — see D-3; it is documented as a pre-existing residual (L-048), NOT claimed fail-closed, NOT a 048 regression.

**Gate-A watch-points (flagged for the review):**
- The reap's `queued→cancelled` CAS races on-strand with `on_cancel`'s CAS — interleaving argument (New P1-N2): on the owning strand, `on_cancel` (`:731-744`) and the reap CAS (`:1126`) both run on that strand and are serialized; the `queued→cancelled` CAS is single-winner; the loser no-ops; the winner schedules exactly one posted resume (counted in `in_flight_resumers_`). Witnessed by an on-strand cancel arriving during reap, ASan+TSan clean, resolved exactly once. (W-1.)
- The holder-yield loop must re-reap post-yield to catch waiters the holder spliced; missing that re-reap would orphan a late waiter (W-2 — synchronous and bounded by the precondition, no cross-thread window).
- `active_holders_count_` and `active_acquirers_count_` demoted to relaxed: justified because all inc/dec happen on the one strand. `active_acquirers_count_` is KEPT as cheap insurance against the 006/047 lost-wake shape (New P2-N2), not removed. (W-3.)

---

## D-3 — OOM scope, honest (US2/FR-003)

**Decision:** re-scope US2/FR-003 to ONLY what 048 cleanly delivers. The "pre-reserved non-allocating post allocator" design is **dropped** (it was unproven and needs a compiled probe — out of scope; P2-1). The fail-closed claim covers `inherited_slot.assign` and the `reaper_slot.assign` elimination; the resume post and the holder-yield post remain a **pre-existing** OOM-terminate class, deferred as **L-048**. Existing fail-closed variant: `error::sync_lock_alloc_failed` (`error.hpp:87`, code 44 — already C-ABI-mapped at `:848`; currently used for the explicit-PMR `store_executor` failure, `async_mutex.hpp:849-857`). No new symbol.

| Site | Current (branch) | Resolution |
|---|---|---|
| `reaper_slot.assign` (`:1177`) | reaper's own cancel handler for the async park | **ELIMINATED** — the redesign removes the reaper park, so this site disappears (free win). |
| `inherited_slot.assign` (`:862`) | assigns the parked waiter's cancel handler inside the `noexcept` `async_initiate` completion | **FAIL-CLOSED** → `sync_lock_alloc_failed`, with the EXACT ref-balance below (NOT a copy of the `:849-856` store_executor exit). |
| resume `asio::post` (`:614`, in `noexcept` `resume_fn_`) AND the drain-yield `asio::post` | posts the resumption runner (E-3 always-post) / yields the strand | **PRE-EXISTING OOM-terminate class.** The shipped code already terminates on OOM at the resume post and the channel `async_wait`. 048 does NOT regress it; the non-allocating-completion redesign is DEFERRED and documented as known limitation **L-048** (same class/treatment as 047's L-047-2). 048 makes NO claim that the resume post is non-allocating. |

**`inherited_slot.assign` fail-closed — exact ref-balance (fixes P2-2):** at the assign site (`:862`) the handler has ALREADY been moved into the awaiter (`store_handler` at `:858` precedes the assign at `:862`) and the record holds creator+attached refs (`add_ref(record, 2)` at `:845`). On assign-throw the completion MUST go THROUGH the awaiter/stored-handler path — complete the awaiter result with `unexpected{sync_lock_alloc_failed}`, detach the awaiter, null `awaiter.record_`, and release each owned ref exactly once — NOT a copy of the `:849-856` store_executor exit (that exit precedes `store_handler` and would double-move the handler / double-release against `async_lock`'s normal tail cleanup at `:925-930`). Allocation-failure injection is required at each instruction boundary around the assign.

---

## D-4 — Retain the per-waiter phase CAS arbitration

**Decision:** keep `on_cancel` + `phase_` `{queued,granted,cancelled}` + the `queued→{cancelled,granted}` CAS (`async_mutex.hpp:737-739, 971, 1026, 1126`). Even strand-local, a parked write-gate waiter can be cancelled when `close()` emits `root_cancel`/`session_cancel` on the strand; the CAS guarantees single-winner resume (no double-resume, no lost-wake) between `on_cancel`, the grant path, and the reap. FR-004.

---

## D-5 — Misuse detection (US3/FR-006)

**Decision:** documentation-primary. A debug assertion is only feasible **where a cheap on-owning-executor check is available** — but `async_mutex` stores NO executor (`2f §1.1`/I-1; `unlock()` takes no executor arg; `this_coro::executor` is type-erased `any_io_executor`), so a production-buildable assertion has **no concrete query seam** (P2-4, NOT addressed by an executor-token redesign in 048). Therefore FR-006's runtime leg is demoted to a **test-only instrumented executor**: the witness drives the unsupported topology via an instrumented executor that detects off-strand entry, NOT a production assertion. The cross-thread WB witness from 047 is repurposed to that test-only check — it is NOT deleted-to-green (`[[feedback_coverage_push_enshrines_bugs]]`).

---

## D-6 — Design-doc amendment matrix (`.specify/2f-async-mutex.md`)

**Decision:** the MERGED `.specify/2f-async-mutex.md` has Errata **E-1..E-4 only** (no E-5, no I-33 — those were the unmerged 047 branch). The new erratum is therefore **E-5** (next after E-4), NOT E-6, and there is no E-5/I-33 to "retire" (P2-8). Add **Erratum E-5 — strand-local reap** recording the strand-local-reap narrowing, plus an exact amendment matrix against the REAL section IDs the redesign touches:

| § | Real section | Amendment |
|---|---|---|
| §1.1 | mutex-size budget (enumerates `active_acquirers_count_` + `drain_latch_ptr_`) | drop `drain_latch_ptr_` + the `drain_latch_state` from the size budget; ADD `in_flight_resumers_` + `draining_complete_`; `active_acquirers_count_` STAYS. Note the `sizeof` change. The §1.1 cross-domain O(N) contention seam stays SUPPORTED. |
| §3.1 | `drain_latch_state` row | remove the row (type deleted). |
| §4.1 | member set | reconcile to the post-048 member set (data-model). |
| §4.5 | cancellation-result table | drain is uninterruptible (no `sync_lock_aborted`-from-drain-cancel); reaped waiters resume `sync_lock_aborted`; new acquirers `sync_lock_drained`. |
| §4.7.3 | drain invariants I-1..I-8 | replace the cross-thread-convergence invariants with: drain runs on the owning strand; the reap is a synchronous single pass; the only yield is to let a pre-drain holder unlock (under the drain precondition); terminal = no holders ∧ `in_flight_resumers_==0` ∧ both lists empty. |
| §4.7.4 | consumer discipline | narrow to: `cancel_and_drain()` must be invoked on the owning strand, co-located with all acquire/cancel/unlock of that mutex; two drain consumers (strand-confined). |

E-5 records the strand-local-reap narrowing. This amendment is an implement-phase task (T0xx), but the *design* is fixed here for Gate A.

---

## Open items carried to Gate A (explicit watch-list)
- W-1 on-strand `queued→cancelled` CAS single-winner — interleaving argument + on-strand-cancel-during-reap witness (D-2, New P1-N2).
- W-2 post-yield re-reap completeness (D-2) — the synchronous analogue of the 006 bug; closed by construction (no cross-thread window).
- W-3 `active_holders_count_`/`active_acquirers_count_` relaxed-ordering justification (single strand); `active_acquirers_count_` KEPT (D-2, New P2-N2).
- W-4 drain precondition (holders release promptly) verified per drain callsite (write_gate, seqnum); resume/yield post OOM deferred as L-048 (D-2/D-3).
- W-5 contract change: drain is now uninterruptible; confirm no supported caller depends on cancelling a drain (close() disables cancellation — `session.cpp:1334`).
- W-6 FR-009 4→3 is a cross-feature (046) claim — re-confirm at 046 rebase against 046's four migrated `atomic<shared_ptr>` sites (engine reader-snapshot, transport cert-source, pinset snapshot, `drain_latch_ptr_`) (New P2-N1).
