# Feature Specification: async_mutex strand-local drain-reap simplification

**Feature Branch**: `048-async-mutex-strand-reap`
**Created**: 2026-06-22
**Status**: Draft
**Input**: User description: "async_mutex strand-local drain-reap simplification (supersedes the 047 converging-loop approach, PR #143)" — narrow `cancel_and_drain()` to the strand-serialized contract its consumers actually use, replace the cross-thread machinery with a synchronous strand-local reap, and make `async_lock()` setup fail closed under memory exhaustion instead of terminating the process.

**Supersedes**: feature 047 (`047-async-mutex-drain-reap`, PR #143). 047's cross-thread converging-loop / Dekker / feeder-count / drain-latch approach left a residual multi-threaded orphan (047 W-B1: 3/25 standalone runs deadlock at both 5 s and 30 s; `entered=6/completed=5/drain_done=0`). This feature removes that machinery rather than patching it further. Full background: `research/findings/000-PLAN-libcxx-consolidation.md` (§1, §3, §5).

## Clarifications

### Session 2026-06-22

- Q: Does the OOM fail-closed path need a NEW `sync_lock_alloc_failed` error variant / new C-ABI enumerator, or does it already exist? → A: It already exists — `error::sync_lock_alloc_failed = 44` is defined (`include/fixpp/core/error.hpp:87`) with a C-ABI mapping (`:848`), currently returned by the explicit `async_lock(mr)` PMR-fallback's `allocate()` failure. The OOM fix REUSES this existing variant for the default-path asio-internal allocation sites. Therefore this feature introduces **zero new public/ABI surface** — the only surface change is the narrowed drain-contract *documentation*. (Corrects the original FR-003/FR-008/Key-Entities/Assumptions wording, which assumed a new variant.)
- Q: How is unsupported genuinely-concurrent (non-strand-serialized) drain misuse surfaced (FR-006)? → A: Documentation-primary — the published contract states strand-serialized-only — PLUS a debug-build assertion where a cheap on-owning-executor check is available. NO release-build runtime gate (it would cost on the hot path, and the `direct_executor` serialization-attestation path cannot always be detected). US3's testable runtime behavior is the debug assertion firing under cheaply-detectable misuse; otherwise the contract is documentation-enforced.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Reliable mutex drain on session teardown (Priority: P1)

The FIX engine drains a session's awaitable mutexes (the write-gate and the sequence-number manager) as part of closing a session. Every acquirer or holder begun before the drain must be resolved — resumed-then-released or rejected with a drain error — so that `close()` completes and the session is reclaimed. Today, under the supported single-strand topology this works, but the primitive also carries cross-thread "convergence" machinery that does **not** fully converge: a parked waiter is occasionally orphaned, the drain hangs, and the test that exercises it intermittently deadlocks (flaky CI on the blocking lane). This story makes the drain reliable by narrowing the primitive to the topology its consumers actually use and reaping waiters synchronously on the owning strand, so no waiter can be orphaned.

**Why this priority**: This is the core of the feature and the reason it was chosen (Option 4). It removes the residual lost-wake **by construction** (no concurrency to race when acquire/drain/cancel all run on one strand), de-flakes the blocking Tier-1 lane, and unblocks the 046/047 merge train. It also shrinks the concurrency proof surface (no converging-loop or Dekker invariants to maintain) and is expected to recover the uncontended latency regression 047 introduced.

**Independent Test**: Run the drain stress witness standalone for a sustained number of runs (≥200 rounds × many repetitions) under the supported strand-serialized topology and observe zero hangs and zero orphaned waiters, across debug/sanitizer presets. Reverting the synchronous-reap change must make a faithful witness fail (mutation-discriminated).

**Acceptance Scenarios**:

1. **Given** a mutex with one or more waiters parked on the owning strand, **When** `cancel_and_drain()` is invoked on that same strand, **Then** every parked waiter is resumed exactly once with a drain rejection and the drain completes deterministically (bounded, no unbounded wait).
2. **Given** a mutex that is currently held, **When** the holder unlocks and a drain is in progress, **Then** the drain completes only after the holder releases, leaving the mutex in the not-locked, fully-reaped terminal state.
3. **Given** sustained stress of the supported topology, **When** the drain witness runs standalone many times, **Then** it never hangs (the residual ~0.06%/round orphan is eliminated).

---

### User Story 2 - Graceful failure instead of process termination under memory exhaustion (Priority: P2)

When the operating system is out of memory, acquiring the awaitable mutex must fail with a recoverable error rather than terminating the entire process. Today, three allocations internal to the asynchronous lock setup can throw on memory exhaustion and escape a `noexcept` boundary, calling `std::terminate()` — which kills **all** healthy sessions in the process, not just the one under memory pressure. Because the engine's role and liveness loops acquire this mutex with an active cancellation slot, this is reachable on a production path.

**Why this priority**: Real production-reachable availability defect (one OOM event takes down every session), but it fires only under genuine memory exhaustion — at which point the next allocation anywhere is also failing — so the marginal value over the existing fail-fast is lower than the P1 reliability fix. Independent of US1.

**Independent Test**: With an injected allocation failure at each of the lock-setup allocation sites, attempt to acquire the mutex and assert the caller observes a fail-closed lock-acquisition error and the process does NOT terminate; other sessions remain operational.

**Acceptance Scenarios**:

1. **Given** an allocation failure during asynchronous lock setup, **When** a caller invokes the lock, **Then** the caller receives a fail-closed lock-allocation error and the process continues running.
2. **Given** the same failure occurring on a production role/liveness loop's lock acquisition, **When** the failure propagates, **Then** only that acquisition fails; unrelated sessions are unaffected.

---

### User Story 3 - Honest, enforced contract for unsupported concurrent use (Priority: P3)

The awaitable mutex's drain capability is documented as supporting only strand-serialized (single-threaded-per-mutex) access. A consumer that violates this — supplying a genuinely-concurrent executor while attesting serialization — is operating outside the supported contract. This story ensures that misuse is documented as unsupported and, where it can be detected cheaply, surfaced as a diagnosable error rather than a silent hang or silent corruption.

**Why this priority**: Prevents the narrowed contract from becoming a silent trap, but affects only an explicitly-unsupported misuse path (the documented `direct_executor` serialization-violation), so it is the lowest priority.

**Independent Test**: Document review confirms the drain contract states strand-serialized-only; where a cheap runtime detection exists, a test drives the unsupported topology and asserts a diagnostic/rejection rather than a hang.

**Acceptance Scenarios**:

1. **Given** the published contract, **When** a consumer reads the drain documentation, **Then** the strand-serialized-only requirement and the consequence of violating it are stated explicitly.
2. **Given** a cheaply-detectable concurrent-drain misuse, **When** it occurs, **Then** the system surfaces it (diagnostic/rejection) rather than silently hanging or corrupting state.

---

### Edge Cases

- A waiter is parked AND a strand-local cancellation for that waiter arrives during the drain (same strand): the waiter must be resumed exactly once — neither double-resumed (use-after-free) nor lost.
- Drain invoked when the mutex is (a) free, (b) held with no waiters, (c) held with queued waiters, (d) already draining (re-entrant) — each must reach the correct terminal state.
- Drain invoked after a prior drain has aborted (re-entrant-after-abort) must observe the terminal outcome, not re-drain incoherently.
- Allocation failure at each of the three lock-setup allocation sites independently — each must fail closed, not terminate, and must not leak a partially-registered waiter or counter.
- A consumer supplies a genuinely-concurrent executor (the unsupported serialization-violation path) — documented unsupported; surfaced where cheaply detectable.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Under the supported strand-serialized topology, `cancel_and_drain()` MUST resolve every begun acquirer/holder/parked-waiter exactly once (resumed-then-released or drain-rejected) with zero orphaned waiters, and MUST complete in bounded time (no unbounded wait).
- **FR-002**: The drain MUST reap parked waiters synchronously on the owning strand; it MUST NOT depend on cross-thread convergence machinery (no converging reap+quiesce loop, no seq_cst Dekker handshake, no acquirer/unlocker feeder counters, no cross-thread drain-latch) to reach its terminal state.
- **FR-003**: `async_lock()` MUST fail closed with the existing lock-allocation error `error::sync_lock_alloc_failed` (already defined, `error.hpp:87`) instead of allowing an allocation failure during lock setup to escape a `noexcept` boundary and call `std::terminate()`. This reuses the existing variant (no new error code) and extends its use from the explicit-PMR path to the default-path asio-internal allocation sites.
- **FR-004**: Strand-local cancellation of a parked waiter MUST resume that waiter exactly once — no double-resume (use-after-free) and no lost wake.
- **FR-005**: Waiter resumption MUST remain always-posted (never inline/synchronous-dispatched into the resuming context), preserving the existing reentrancy-safety guarantee.
- **FR-006**: Genuinely-concurrent (non-strand-serialized) drain MUST be documented as unsupported (documentation-primary contract). Where a cheap on-owning-executor check is available, a **debug-build assertion** MUST surface the misuse rather than letting it silently hang or corrupt state; no release-build runtime gate is required (the `direct_executor` attestation path cannot always be detected, and a hot-path gate is not warranted). It MUST NOT be "resolved" by making the witness deterministic in a way that hides the unsupported nature.
- **FR-007**: The change MUST NOT alter the observable behavior of the four production consumers (Session write-gate, SeqnumManager, MemoryStore, FileStore) on any supported path.
- **FR-008**: No public API or ABI surface change is permitted. The OOM fix reuses the existing `sync_lock_alloc_failed` variant (no new error code, no C-ABI enumerator change). The only externally-visible change is the narrowed/clarified drain-contract documentation. (FR-007 ABI-no-change, as in 047.)
- **FR-009**: Removing the drain's cross-thread shared-pointer slot MUST reduce the libc++ portability fallback's `std::atomic<std::shared_ptr<>>` consumer set from four to three, without affecting the remaining three consumers (engine reader-snapshot, transport cert-source slot, pinset snapshot).

### Key Entities

- **Awaitable mutex (`fixpp::sync::async_mutex`)**: the NFR-016 coroutine-aware mutex. Holds a lock state, a queue of parked waiter records, and a drain terminal state. This feature removes its cross-thread convergence members.
- **Waiter record**: a per-acquirer record (refcounted, embedded in the caller's coroutine frame) representing a parked or in-flight acquisition. Retained across this change; resumed via a posted continuation.
- **Drain operation (`cancel_and_drain`)**: the owning-side teardown that rejects future acquisitions and reaps current waiters, satisfying the mutex's destruction precondition. Narrowed to strand-local synchronous reap.
- **Lock-allocation error (`sync_lock_alloc_failed`)**: the EXISTING fail-closed error variant (`error.hpp:87`, code 44) returned when lock setup cannot allocate; this feature extends its use from the explicit-PMR path to the default-path asio-internal allocation sites. Not a new symbol.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: The drain stress witness, run standalone under the supported topology, completes with **zero hangs and zero orphaned waiters across ≥200 rounds × ≥25 repetitions** at both short and long deadlines — eliminating the residual ~0.06%/round orphan (previously 3/25 standalone failures).
- **SC-002**: Under simulated memory exhaustion at each lock-setup allocation site, the acquiring caller receives a recoverable lock-allocation error and the process does not terminate; concurrently-open sessions remain operational (0 process terminations across the injected-failure suite).
- **SC-003**: The supported uncontended and contended lock/unlock paths show **no latency regression versus the shipped baseline**, and recover the +16% acquire / +32% unlock regression that the superseded 047 feeder machinery introduced.
- **SC-004**: The full sanitizer matrix (debug/ASan/UBSan/TSan/release/gcc-release) over the synchronization suite is green with the drain machinery removed, and the maintained concurrency-invariant set is reduced (the converging-loop/Dekker/feeder invariants no longer exist to maintain).
- **SC-005**: All four production consumers pass their existing behavioral suites unchanged, and the 046 portability work continues to build against the reduced (three-consumer) fallback set.

## Assumptions

- **All production `async_mutex` consumers are strand-serialized** (source-verified): Session write-gate, SeqnumManager, MemoryStore, and FileStore each access their mutex only on the per-session strand, and cancellation emission for those sessions is itself marshaled onto that same strand (`engine.cpp:1255-1281`). Therefore acquire, drain, and cancel never run truly concurrently in production. The only path to concurrency is the documented-unsupported `direct_executor` serialization-violation (INV-2).
- **This feature supersedes 047** (PR #143) and is branched off `main` (the shipped 006 baseline), not off 047. The 047 converging-loop work is abandoned, not extended.
- **046 (atomic_shared_ptr / libc++ portability, PR #142) rebases on this feature.** Removing the drain latch reduces 046's migrated `atomic<shared_ptr>` consumer set 4→3; the other three consumers still require the fallback.
- The retained resumption discipline (always-post) and the anti-double-resume cancellation arbitration are kept; only the cross-thread *convergence* machinery is removed.
- No new wire, codegen, configuration, or error/ABI surface is introduced (`sync_lock_alloc_failed` already exists; the OOM fix reuses it). The only externally-visible change is documentation of the narrowed drain contract.
- Full gates apply (this narrows an advertised contract): spec → clarify → plan → Gate A → tasks → analyze → checklist → checklist-audit → implement → simplify → verify → Gate B, per constitution Article XVII.
