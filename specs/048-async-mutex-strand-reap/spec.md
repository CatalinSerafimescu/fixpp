# Feature Specification: async_mutex strand-local drain-reap simplification

**Feature Branch**: `048-async-mutex-strand-reap`
**Created**: 2026-06-22
**Status**: Draft
**Input**: User description: "async_mutex strand-local drain-reap simplification (supersedes the 047 converging-loop approach, PR #143)" — narrow `cancel_and_drain()` to the strand-serialized contract its consumers actually use, replace the cross-thread machinery with a synchronous strand-local reap, and make `async_lock()` setup fail closed under memory exhaustion instead of terminating the process.

**Supersedes**: feature 047 (`047-async-mutex-drain-reap`, PR #143). 047's cross-thread converging-loop / Dekker / feeder-count / drain-latch approach left a residual multi-threaded orphan (047 W-B1: 3/25 standalone runs deadlock at both 5 s and 30 s; `entered=6/completed=5/drain_done=0`). This feature removes that machinery rather than patching it further. Full background: `research/findings/000-PLAN-libcxx-consolidation.md` (§1, §3, §5).

## Clarifications

### Session 2026-06-22

- Q: Does the OOM fail-closed path need a NEW `sync_lock_alloc_failed` error variant / new C-ABI enumerator, or does it already exist? → A: It already exists — `error::sync_lock_alloc_failed = 44` is defined (`include/fixpp/core/error.hpp:87`) with a C-ABI mapping (`:848`), currently returned by the explicit `async_lock(mr)` PMR-fallback's `allocate()` failure. The OOM fix REUSES this existing variant for the default-path asio-internal allocation sites. Therefore this feature introduces **zero new C-ABI surface** (no new/renumbered error code) — though `sizeof(async_mutex)` changes (a C++ layout/header-recompile change, FR-008). (Corrects the original FR-003/FR-008/Key-Entities/Assumptions wording, which assumed a new variant.)
- Q: How is unsupported genuinely-concurrent drain-overlap misuse surfaced (FR-006)? → A: Documentation-primary — the published contract states the drain must not overlap another thread's acquire/cancel/unlock. There is **no production assertion seam**: `async_mutex` stores no owning executor, and `this_coro::executor` / `any_io_executor` is type-erased, so `strand::running_in_this_thread()` is not recoverable in production (P2-4). US3's testable runtime behavior is therefore exercised only via a **test-only instrumented executor** that detects off-strand entry; in production the contract is documentation-enforced. Ordinary cross-thread `async_lock`/`unlock` (the §1.1 cross-domain contention seam) stays supported and is NOT narrowed.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Reliable mutex drain on session teardown (Priority: P1)

The FIX engine drains a session's awaitable mutexes (the write-gate and the sequence-number manager) as part of closing a session. Every acquirer or holder begun before the drain must be resolved — resumed-then-released or rejected with a drain error — so that `close()` completes and the session is reclaimed. Today, under the supported single-strand topology this works, but the primitive also carries cross-thread "convergence" machinery that does **not** fully converge: a parked waiter is occasionally orphaned, the drain hangs, and the test that exercises it intermittently deadlocks (flaky CI on the blocking lane). This story makes the drain reliable by narrowing the primitive to the topology its consumers actually use and reaping waiters synchronously on the owning strand, so no waiter can be orphaned.

**Why this priority**: This is the core of the feature and the reason it was chosen (Option 4). It removes the residual lost-wake **by construction** (no concurrency to race when acquire/drain/cancel all run on one strand), de-flakes the blocking Tier-1 lane, and unblocks the 046/047 merge train. It also shrinks the concurrency proof surface (no converging-loop or Dekker invariants to maintain). Since 048 branches off main, the baseline is main (the 047 +16%/+32% regression was never on main).

**Independent Test**: Run the drain stress witness standalone for a sustained number of runs (≥200 rounds × many repetitions) under the supported strand-serialized topology and observe zero hangs and zero orphaned waiters, across debug/sanitizer presets. Discrimination is **structural** for the removed machinery (the cross-thread latch/channel/Dekker are gone) plus **behavioral** for the new contract (immediate-destroy-after-reap, reentrant-during-active-drain, on-strand-cancel-during-reap). A supported-topology behavioral witness is NOT claimed to fail on reverting the reap (the shipped impl already supports that topology — P2-6).

**Acceptance Scenarios**:

1. **Given** a mutex with one or more waiters parked on the owning strand, **When** `cancel_and_drain()` is invoked on that same strand, **Then** every parked waiter is resumed exactly once with `sync_lock_aborted` (the shipped reap code) and the drain completes only after all posted resumers have run (no immediate-destroy UAF).
2. **Given** a mutex that is currently held, **When** the holder unlocks and a drain is in progress, **Then** the drain completes only after the holder releases (under the drain precondition: holders release promptly), leaving the mutex in the not-locked, fully-reaped terminal state.
3. **Given** sustained stress of the supported topology, **When** the drain witness runs standalone many times, **Then** it never hangs (the residual ~0.06%/round orphan is eliminated).

---

### User Story 2 - Graceful failure instead of process termination under memory exhaustion (Priority: P2)

When the operating system is out of memory, acquiring the awaitable mutex must fail with a recoverable error rather than terminating the entire process — for the lock-setup allocation site that 048 cleanly fixes. Today an allocation in `inherited_slot.assign` (cancel-handler storage) can throw on memory exhaustion and escape a `noexcept` boundary, calling `std::terminate()` — which kills **all** healthy sessions in the process. Because the engine's role and liveness loops acquire this mutex with an active cancellation slot, this is reachable on a production path. (The `reaper_slot.assign` site is eliminated by the redesign. The resume `asio::post` and the drain holder-yield post remain a **pre-existing** OOM-terminate class, deferred as L-048 — 048 does not regress them and does not claim a non-allocating resume post.)

**Why this priority**: Real production-reachable availability defect (one OOM event takes down every session), but it fires only under genuine memory exhaustion — at which point the next allocation anywhere is also failing — so the marginal value over the existing fail-fast is lower than the P1 reliability fix. Independent of US1.

**Independent Test**: With an injected allocation failure at `inherited_slot.assign`, attempt to acquire the mutex and assert the caller observes a fail-closed `sync_lock_alloc_failed` and the process does NOT terminate; other (unrelated) sessions remain operational.

**Acceptance Scenarios**:

1. **Given** an allocation failure at `inherited_slot.assign` during lock setup, **When** a caller invokes the lock, **Then** the caller receives a fail-closed `sync_lock_alloc_failed` and the process continues running.
2. **Given** the same failure on a production role/liveness loop's lock acquisition, **When** the failure propagates, **Then** only that acquisition fails; unrelated sessions are unaffected.

---

### User Story 3 - Honest, enforced contract for unsupported concurrent use (Priority: P3)

The awaitable mutex's drain capability is documented as supporting only strand-serialized (single-threaded-per-mutex) access. A consumer that violates this — supplying a genuinely-concurrent executor while attesting serialization — is operating outside the supported contract. This story ensures that misuse is documented as unsupported and, where it can be detected cheaply, surfaced as a diagnosable error rather than a silent hang or silent corruption.

**Why this priority**: Prevents the narrowed contract from becoming a silent trap, but affects only an explicitly-unsupported misuse path (the documented `direct_executor` serialization-violation), so it is the lowest priority.

**Independent Test**: Document review confirms the drain contract states strand-confined-only (drain must not overlap another thread's acquire/cancel/unlock); a **test-only instrumented executor** drives the unsupported topology and detects off-strand entry (there is no production assertion seam — P2-4).

**Acceptance Scenarios**:

1. **Given** the published contract, **When** a consumer reads the drain documentation, **Then** the strand-serialized-only requirement and the consequence of violating it are stated explicitly.
2. **Given** a concurrent-drain-overlap misuse driven under a test-only instrumented executor, **When** it occurs, **Then** the instrumented executor detects the off-strand entry (in production the contract is documentation-enforced; no production assertion seam).

---

### Edge Cases

- A waiter is parked AND a strand-local cancellation for that waiter arrives during the drain (same strand): the waiter must be resumed exactly once — neither double-resumed (use-after-free) nor lost (on-strand-cancel-during-reap).
- Drain invoked when the mutex is (a) free, (b) held with no waiters, (c) held with queued waiters, (d) already draining (reentrant-during-active-drain — the reentrant caller awaits the first drain's terminal completion, does not return ok eagerly) — each must reach the correct terminal state.
- The mutex is destroyed immediately after `cancel_and_drain()` returns while reaped resumers are still queued — the terminal condition (`in_flight_resumers_==0`) must already have held, so no UAF.
- Allocation failure at `inherited_slot.assign` — must fail closed with `sync_lock_alloc_failed`, not terminate, and must not leak a partially-registered waiter, ref, or counter (exact ref-balance via the awaiter/stored-handler path).
- A consumer overlaps a drain with another thread's acquire/cancel/unlock (the unsupported serialization-violation path) — documented unsupported; ordinary cross-thread `async_lock`/`unlock` stays supported.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Under the supported strand-serialized topology, `cancel_and_drain()` MUST resolve every begun acquirer/holder/parked-waiter exactly once (resumed-then-released or drain-rejected) with zero orphaned waiters, and MUST NOT finalize until its terminal condition holds: `active_holders_count_==0` AND `in_flight_resumers_==0` (no posted-but-unrun resumers) AND both lists empty. Prompt completion rests on the drain precondition (holders release promptly), NOT an O(holder-count) bound.
- **FR-002**: The drain MUST reap parked waiters synchronously on the owning strand; it MUST NOT depend on cross-thread convergence machinery (no converging reap+quiesce loop, no seq_cst Dekker handshake, no acquirer/unlocker feeder counters, no cross-thread drain-latch) to reach its terminal state. The retained `in_flight_resumers_`, `active_holders_count_`, and `draining_complete_` are plain strand-local (relaxed) counters/flags, not cross-thread machinery (`active_acquirers_count_` is removed as vestigial).
- **FR-003**: `async_lock()` MUST fail closed with the existing lock-allocation error `error::sync_lock_alloc_failed` (already defined, `error.hpp:87`) at the `inherited_slot.assign` site instead of allowing that allocation failure to escape a `noexcept` boundary and call `std::terminate()`. The `reaper_slot.assign` site is eliminated by the redesign. The resume `asio::post` and the drain holder-yield post remain a pre-existing OOM-terminate class, deferred as **L-048** (not in 048 scope). This reuses the existing variant (no new error code).
- **FR-004**: Strand-local cancellation of a parked waiter MUST resume that waiter exactly once — no double-resume (use-after-free) and no lost wake. The reap's CAS and `on_cancel`'s CAS are serialized on the strand; the single winner schedules exactly one posted resume counted in `in_flight_resumers_`.
- **FR-005**: Waiter resumption MUST remain always-posted (never inline/synchronous-dispatched into the resuming context), preserving the existing reentrancy-safety guarantee.
- **FR-006**: Overlapping a drain with another thread's `async_lock`/`unlock`/`cancel` for the same mutex MUST be documented as unsupported (documentation-primary contract). `async_mutex` stores no owning executor, so there is **no production assertion seam**; the unsupported overlap is exercised only via a test-only instrumented executor. Ordinary cross-thread `async_lock`/`unlock` contention (the §1.1 cross-domain seam) MUST remain supported and MUST NOT be declared undefined.
- **FR-007**: The change MUST NOT alter the observable behavior of the consumers on any supported path. In particular, reaped waiters MUST keep resuming with `sync_lock_aborted` (the shipped code), NOT `sync_lock_drained`; `sync_lock_drained` is returned only to NEW post-draining `async_lock` callers.
- **FR-008**: No **C ABI** change is permitted (no new/renumbered error code; abidiff-clean). The OOM fix reuses the existing `sync_lock_alloc_failed` variant. However removing `drain_latch_ptr_`/`drain_latch_state` and adding `in_flight_resumers_`/`draining_complete_` CHANGES `sizeof(async_mutex)` — a C++ object-layout change requiring **header recompilation** of every type that embeds it by value (header-mostly library; not a runtime `.so` break). A compile-time `sizeof`/`alignof` layout golden MUST be added and re-baselined.
- **FR-009**: Removing the drain's `drain_latch_ptr_` MUST reduce the libc++ portability fallback's `std::atomic<std::shared_ptr<>>` consumer set from four to three. The four sites are: engine reader-snapshot (`engine.hpp` `reader_snapshot_`), transport cert-source (`transport_factory.hpp` `cert_source_slot_`), pinset snapshot (`pinset.hpp` `pin_snapshot`), and `async_mutex.hpp` `drain_latch_ptr_`. This is a cross-feature (046) claim to re-confirm at the 046 rebase.

### Key Entities

- **Awaitable mutex (`fixpp::sync::async_mutex`)**: the NFR-016 coroutine-aware mutex. Holds a lock state, a queue of parked waiter records, and a drain terminal state. This feature removes its cross-thread convergence members.
- **Waiter record**: a per-acquirer record (refcounted, embedded in the caller's coroutine frame) representing a parked or in-flight acquisition. Retained across this change; resumed via a posted continuation.
- **Drain operation (`cancel_and_drain`)**: the owning-side teardown that rejects future acquisitions and reaps current waiters, satisfying the mutex's destruction precondition. Narrowed to strand-local synchronous reap.
- **Lock-allocation error (`sync_lock_alloc_failed`)**: the EXISTING fail-closed error variant (`error.hpp:87`, code 44) returned when lock setup cannot allocate; this feature extends its use to the `inherited_slot.assign` site. Not a new symbol. (Reaped waiters resume with the EXISTING `sync_lock_aborted`, unchanged.)

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: The drain stress witness, run standalone under the supported topology, completes with **zero hangs and zero orphaned waiters across ≥200 rounds × ≥25 repetitions** at both short and long deadlines — eliminating the residual ~0.06%/round orphan (previously 3/25 standalone failures).
- **SC-002**: Under simulated memory exhaustion at `inherited_slot.assign`, the acquiring caller receives `sync_lock_alloc_failed` and the process does not terminate; concurrently-open unrelated sessions remain operational (0 process terminations across the injected-failure suite). (The resume/yield post OOM-terminate is out of scope — L-048.)
- **SC-003**: The supported uncontended and contended lock/unlock paths show **no latency regression versus main (the shipped baseline)**. (048 branches off main, so the 047 +16%/+32% feeder regression is NOT on main; recovering it is mentioned only parenthetically versus the abandoned 047 branch, not a primary claim.)
- **SC-004**: The full sanitizer matrix (debug/ASan/UBSan/TSan/release/gcc-release) over the synchronization suite is green with the drain machinery removed, and the maintained concurrency-invariant set is reduced (the converging-loop/Dekker/feeder invariants no longer exist to maintain).
- **SC-005**: All consumers pass their existing behavioral suites unchanged — the two drain consumers (Session write-gate, SeqnumManager) and the two lock-only consumers (MemoryStore, FileStore) — and the 046 portability work continues to build against the reduced (three-consumer) fallback set.

## Assumptions

- **The two DRAIN consumers are strand-confined** (source-verified): Session write-gate (drain `session.cpp:1524`) and SeqnumManager (drain `seqnum_manager.hpp:146`) each access their mutex only on the per-session strand, and cancellation emission is itself marshaled onto that same strand (`engine.cpp:1255-1281`). Therefore a drain never overlaps an acquire/cancel/unlock on another thread. The two LOCK-ONLY consumers (MemoryStore, FileStore) never drain; direct (non-Session) FileStore use is caller-serialized per `file_store.hpp:116-133`. The only path to drain-overlap concurrency is the documented-unsupported serialization-violation (INV-2); ordinary cross-thread `async_lock`/`unlock` contention stays supported.
- **This feature supersedes 047** (PR #143) and is branched off `main` (the shipped 006 baseline), not off 047. The 047 converging-loop work is abandoned, not extended.
- **046 (atomic_shared_ptr / libc++ portability, PR #142) rebases on this feature.** Removing the drain latch reduces 046's migrated `atomic<shared_ptr>` consumer set 4→3; the other three consumers still require the fallback.
- The retained resumption discipline (always-post) and the anti-double-resume cancellation arbitration are kept; only the cross-thread *convergence* machinery is removed.
- No new wire, codegen, configuration, or **C-ABI** surface is introduced (`sync_lock_alloc_failed` already exists; the OOM fix reuses it). `sizeof(async_mutex)` does change (C++ layout / header-recompile, FR-008). The externally-visible behavioral change is the narrowed drain contract documentation.
- Full gates apply (this narrows an advertised contract): spec → clarify → plan → Gate A → tasks → analyze → checklist → checklist-audit → implement → simplify → verify → Gate B, per constitution Article XVII.
