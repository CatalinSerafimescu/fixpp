# Feature Specification: async_mutex hardening (Cluster-4)

**Feature Branch**: `058-async-mutex-hardening`
**Created**: 2026-07-02
**Status**: Draft
**Input**: User description: "Harden the async_mutex coroutine sync primitive against a set of Phase-0-verified concurrency defects (AM-P1..P3) plus the test-validity gaps."

## Context & Provenance

`async_mutex` (`include/fixpp/core/sync/async_mutex.hpp`) is the core coroutine synchronization
primitive, embedded in MemoryStore, FileStore, SeqnumManager, and Session `write_gate_`. A defect
here is pervasive and sanitizer-elusive. An adversarial review (Fable) found 1 P1 / 3 P2 / 3 P3
defects plus test-validity gaps; a pre-spec Phase-0 pass (Opus + independent Codex, full
concurrence) confirmed all seven are REAL. Evidence:
`research/G19-fix-fpml-iso20022/phases/phase-9/perf-investigation/findings/async-mutex-phase0-verification.md`
(+ `async-mutex-review.md`).

**Load-bearing contract fact:** the primitive SUPPORTS cross-thread `async_lock`/`unlock`
contention (`async_mutex.hpp:190`); only `cancel_and_drain()` is strand-local. Every defect is
REAL in that supported cross-thread envelope. They are **latent in production today** only because
all four shipped consumers are strand-confined — this feature protects the primitive's *published*
contract, not a live production bug. The goal is to make the primitive bullet-proof across its full
supported envelope.

## Clarifications

### Session 2026-07-02

- Q: AM-P2-1 disposition — strengthen ordering & keep cross-executor teardown supported, narrow the
  contract, or both? → A: Fix the ordering (release/acquire) AND tighten the drain/destructor docs
  to declare the drain strand-local by design, guard-enforced (belt-and-suspenders: safe AND
  explicitly scoped).
- Q: How broadly to close the test-validity gaps T-1..T-7? → A: Full closure — convert every
  race-named test that runs single-threaded-in-disguise (`test_race_*`, `test_result_write_race`,
  `test_cancellation_mid_wait`) to genuinely multi-threaded, AND add the missing exhaustion /
  free-list-reuse (T-3) and FIFO-across-cycles (T-7) witnesses; all new fix witnesses are genuinely
  MT.
- Q: Binding branch-coverage Definition-of-Done for `async_mutex.hpp`? → A: 100% of REACHABLE
  branches (lcov BRDA/DA basis on the coverage lane); every unreachable branch waived with a
  written source-level proof; re-asserted at `/speckit-verify` + Gate B.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Cross-thread waiter-pool integrity (Priority: P1)

A consumer contends the mutex from multiple threads (the supported envelope). Concurrent
allocation and release of waiter records from the internal 512-slot pool must never corrupt the
pool, lose a waiter, or grant the lock to two records aliasing one slot — even under the exact
pop/pop/push interleaving that today produces an ABA on the free-list and an unsynchronized read of
a slot being reused.

**Why this priority**: This is the one true safety defect in the pool machinery (AM-P1). It is
TSan-invisible (all-atomic cycle), so it can silently corrupt state on real hardware; it is the
reason the session exists.

**Independent Test**: A genuinely multi-threaded stress harness that drives concurrent contended
acquire/release across the free-list reuse path, with a discriminating oracle (exact completion
count + no double-grant + no lost waiter) that FAILS against the current (pre-fix) code and PASSES
after — verified by mutation (revert the fix → red).

**Acceptance Scenarios**:

1. **Given** N threads contending one mutex with waiters cycling through the inline pool, **When** a
   pop reads a free-list head whose successor is concurrently popped, reused, and pushed back
   (the ABA window), **Then** no allocation ever returns a slot holding a live parked record, and
   every acquire completes exactly once with a correct grant.
2. **Given** the same contention, **When** running under TSan, **Then** the plain-field read of a
   slot being concurrently reused is no longer a data race (the primitive exposes no non-atomic
   conflicting access on the reuse path).

---

### User Story 2 - Safe teardown across the supported envelope (Priority: P1)

A consumer tears the mutex down (drain-then-destroy, or the destructor's contract-violation guard)
without any thread performing a write-after-free into the mutex's own storage, and the destructor
guard loudly rejects — in both debug and release — every state in which an **in-flight resumer**
could still touch the mutex. (The residual cross-executor granted-holder-vs-drain hazard is handled
by keeping the drain strand-local per the contract — FR-002/FR-003 — not by the guard, since the
holder count is not a valid teardown barrier.)

**Why this priority**: Two teardown defects (AM-P2-1 relaxed happens-before; AM-P2-2 destructor
guard blind spot) can each produce a heap-use-after-free. AM-P2-2 is worse: the guard exists
precisely to catch contract violations, yet silently approves one that UAFs.

**Independent Test**: (a) a drain-then-destroy witness with an in-flight resumer that asserts the
teardown observes all outstanding pool writes; (b) a death test reproducing the
cancel-delivered-then-destroy-before-runner-tail shape (ASan-deterministic) that the current guard
wrongly passes and the hardened guard terminates.

**Acceptance Scenarios**:

1. **Given** a drain that observes zero in-flight resumers, **When** the caller destroys the mutex,
   **Then** no resumer's write into the mutex-owned pool storage is still outstanding (the teardown
   synchronizes-with every resumer's final release).
2. **Given** a cancellation has been delivered to a waiter's owner but the resume runner's tail has
   not yet run, **When** the caller destroys the mutex without draining, **Then** the destructor
   terminates loudly (guard rejects) rather than passing silently into a UAF.
3. **Given** a correctly drained-then-destroyed mutex, **When** destroyed, **Then** the destructor
   does NOT terminate (no false positive on the legitimate teardown path).

---

### User Story 3 - Bounded behavior under pool exhaustion (Priority: P2)

Under sustained inline-pool exhaustion (512 parked waiters, no PMR fallback), the primitive fails
closed deterministically and never re-issues a slot that holds a live record — even across an
unbounded number of failing allocation attempts.

**Why this priority**: AM-P2-3 — the exhaustion counter is an unclamped `uint32_t` that wraps after
2³² failing attempts and re-issues live slots (blast radius equal to AM-P1). A naive client that
tight-retries on the allocation-failure error reaches the wrap in minutes.

**Independent Test**: A witness that exhausts the inline pool and drives many failing allocation
attempts, asserting the failure error is returned deterministically and no live slot is ever
re-handed-out (counter is bounded, not wrapping).

**Acceptance Scenarios**:

1. **Given** 512 parked waiters and no PMR, **When** a 513th acquire is attempted, **Then** it fails
   closed with the allocation-failure error.
2. **Given** repeated failing acquisitions during exhaustion, **When** the attempt count exceeds any
   32-bit boundary, **Then** the allocation counter never wraps into re-issuing an occupied slot.

---

### User Story 4 - Impossible-state hardening (assert, don't degrade) (Priority: P3)

Internal invariants that are true today but merely *tolerated* (stepped past, or left to a
defensive arm) instead of asserted are converted into loud traps, so any future edit that violates
them fails a test/assert rather than degrading into an undiagnosable hang, phantom unlock, or
process terminate.

**Why this priority**: AM-P3-1 (chain-walk steps past a `granted` record → latent lost-waiter),
AM-P3-2 (null-awaiter arm can leave `result_` engaged → latent phantom unlock), AM-P3-3
(`asio::post` bad_alloc escapes the `noexcept` resume path → terminate, asymmetric with the trapped
slot-assign). All are currently unreachable; this is robustness/assert hardening and an explicit
disposition for the OOM asymmetry.

**Independent Test**: For AM-P3-1/2, an assertion/trap that fires if the impossible state is ever
constructed (verified by a fault-injection or mutation that forces the state). For AM-P3-3, a
recorded disposition (accepted terminate documented, OR the escape closed) with a test or documented
rationale.

**Acceptance Scenarios**:

1. **Given** a future change that leaves a `granted` record in a chain walk, **When** unlock runs,
   **Then** an assert/trap fires (no silent lost-waiter).
2. **Given** the null-awaiter resume arm, **When** a granted record is released, **Then** no phantom
   `unlock()` occurs (result disarmed or non-null asserted).
3. **Given** the OOM behavior of the noexcept resume path, **When** the fix ships, **Then** the
   disposition is explicit and consistent with the trapped slot-assign counterpart.

---

### User Story 5 - Trustworthy multi-threaded test coverage (Priority: P2)

The async_mutex test suite genuinely exercises its concurrent contract: cancel × drain × destroy
interleavings run on real threads (not a single-threaded `io_context` in disguise), inline-pool
exhaustion and free-list reuse are covered, and FIFO order is asserted across acquire/release
cycles — with every added witness being a discriminating, mutation-tested assertion so the coverage
push cannot enshrine a bug.

**Why this priority**: Test-validity gaps T-1..T-7 — today only `test_arm64_weak_memory` is
genuinely MT and it never cancels/drains/destroys; every test named for a race runs
single-threaded; no test exercises pool exhaustion or free-list reuse at all (exactly where AM-P1/
AM-P2-3 live). Without this, the fixes above cannot be trusted to hold.

**Independent Test**: Branch-coverage measurement of `async_mutex.hpp` on the coverage lane (lcov
BRDA/DA) shows the target met or every uncovered branch waived with a written source-level
unreachability proof; each new witness is shown to fail against a deliberately reverted fix.

**Acceptance Scenarios**:

1. **Given** the hardened suite, **When** measured on the coverage lane, **Then** branch coverage of
   `async_mutex.hpp` meets the target or each gap carries a written unreachability proof.
2. **Given** each added concurrency witness, **When** the corresponding fix is reverted (mutation),
   **Then** the witness turns red (it discriminates, not merely executes).
3. **Given** the MT witnesses, **When** run repeatedly under stress, **Then** cancel/drain/destroy
   interleavings actually occur across ≥2 threads (not deterministic single-strand scheduling).

### Edge Cases

- The exact ABA window: free-list `A→B→C`, one thread mid-pop while another pops A, pops B, and
  pushes A back with a stale successor.
- A waiter parked from a different executor/strand *before* a drain begins, then reaped by that
  drain (the cross-executor teardown seam behind AM-P2-1).
- Cancellation delivered inline followed by immediate destroy, before the resume runner's tail
  releases the record and decrements the in-flight count (AM-P2-2).
- Sustained exhaustion: 512 parked + a tight retry loop on the allocation-failure error (AM-P2-3).
- The load-bearing layout golden (`sizeof == 131120`, `alignof == 16`) must remain intact unless a
  fix strictly requires a change — in which case the change is called out explicitly.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The waiter-pool free-list MUST be safe against ABA under the supported cross-thread
  contention envelope: no allocation may return a slot that holds a live parked record, and no
  concurrent read of a slot's link field may conflict with that slot's reuse (closes AM-P1).
- **FR-002**: Teardown MUST establish a happens-before relationship such that when the drain's
  terminal condition observes zero in-flight resumers, every resumer's writes into the mutex-owned
  pool storage are already visible; destroying the mutex after that point MUST NOT race an
  outstanding resumer write (closes AM-P2-1). *Disposition (clarified 2026-07-02):* strengthen the
  ordering to a release/acquire pairing so cross-executor drain-then-destroy is memory-safe, AND
  tighten the drain/destructor documentation to state the drain is strand-local by design —
  belt-and-suspenders: the ordering fix makes the parked-then-reaped case safe, the contract text
  makes the intended envelope explicit. (The destructor guard enforces the *in-flight-resumer*
  barrier; strand-locality of the granted-holder case is documented-contract-only, NOT guard-enforced
  — see FR-003.)
- **FR-003**: The destructor's contract-violation guard MUST reject (terminate, in debug AND
  release) any state in which an **in-flight resumer** could still dereference the mutex — not only a
  non-free `state_` or a non-empty residual chain (closes AM-P2-2). The load-bearing signal is the
  in-flight-resumer count (decremented as the runner's last statement); the holder count is NOT a
  valid teardown barrier and MUST NOT be relied on for the guard (it decrements before the holder
  finishes touching the mutex — Gate-A). The residual cross-executor granted-holder-vs-drain hazard is
  handled by the drain CONTRACT (FR-002 / contract-delta), not the guard. It MUST NOT terminate on a
  correctly drained-then-destroyed mutex.
- **FR-004**: The inline-pool allocation counter MUST be bounded: sustained failing allocation
  attempts during exhaustion MUST NOT wrap and re-issue an occupied slot; exhaustion MUST fail
  closed with the existing allocation-failure error (closes AM-P2-3).
- **FR-005**: Chain-walk arms that today step past a structurally-impossible `granted` record MUST
  trap **loudly in release AND debug** (the `std::terminate()` idiom, mirroring the destructor guard —
  NOT a debug-only `assert` and NOT `std::unreachable()`) instead of silently abandoning the record
  (closes AM-P3-1). The state is provably impossible today, so the trap has no false-fire path.
- **FR-006**: The resume runner's null-awaiter arm MUST NOT leave a granted record's result engaged
  in a way that triggers a phantom `unlock()` on record destruction (closes AM-P3-2).
- **FR-007**: The OOM behavior of the `noexcept` resume/post path MUST have an explicit, documented
  disposition consistent with the already-trapped slot-assign path. *Disposition (settled, research.md
  D-8 / contract-delta):* the escape is NOT closed — the resume runs post-grant with no error channel,
  so the primitive documents terminate-on-OOM as an **accepted fail-stop**, consistent with the
  destructor guard's terminate-on-contract-violation posture; this asymmetry with the pre-grant
  slot-assign path (which CAN fail closed) is inherent to grant-ordering, not a defect (closes AM-P3-3).
- **FR-008**: Every behavioral fix MUST ship with a discriminating, mutation-tested witness that is
  RED against the pre-fix code and GREEN after; witnesses for the cross-thread defects MUST be
  genuinely multi-threaded. Additionally (full-closure decision, clarified 2026-07-02) every
  race-named test that currently runs single-threaded-in-disguise — `test_race_cancel_during_resume`,
  `test_race_multi_cancel`, `test_race_cancel_pre_drain`, `test_result_write_race`,
  `test_cancellation_mid_wait` — MUST be converted to genuinely multi-threaded so its arbitration is
  actually contended (closes T-1, T-2, T-4, T-5).
- **FR-009**: The suite MUST add witnesses for inline-pool exhaustion (513th waiter), free-list
  reuse, and FIFO-order-across-cycles, none of which are exercised today (closes T-3, T-7).
- **FR-010**: Branch coverage of `async_mutex.hpp` MUST be measured on the coverage lane (lcov
  BRDA/DA basis) and MUST reach **100% of reachable branches**; every unreachable branch MUST carry
  a written source-level unreachability proof logged as a coverage waiver (no silently-uncovered
  branch). The target is re-asserted at `/speckit-verify` and Gate B. Coverage witnesses MUST assert
  behavior, not merely execute the branch (no bug enshrinement).
- **FR-011**: The public `async_mutex` API surface and the `[2f]` layout golden
  (`sizeof == 131120`, `alignof == 16`) MUST remain unchanged unless a fix strictly requires
  otherwise; any required change MUST be explicitly called out and justified.
- **FR-012**: Strand-confined production consumers MUST be behaviorally unaffected (the fixes are
  additive safety for the supported cross-thread envelope). The `no-std-mutex` CI gate and the
  load-bearing `test_async_mutex_layout_golden` MUST be preserved.
- **FR-013**: Because AM-P1 is TSan-invisible (all-atomic cycle), its closure MUST be argued by a
  targeted stress/model-check witness plus the regression test that would have caught it — branch
  coverage alone is NOT accepted as proof of correctness for the atomic cycle.

### Key Entities

- **Waiter-pool free-list**: the intrusive LIFO of released waiter-record slots inside the mutex's
  512-slot inline storage; the locus of AM-P1 and AM-P2-3.
- **In-flight-resumer count**: the barrier counter that keeps the mutex alive until every posted
  resumer has finished dereferencing it; the locus of AM-P2-1 and the AM-P2-2 guard gap.
- **Resume runner**: the posted continuation that resumes a waiter's owner and then releases its
  record; its ordering and OOM behavior drive AM-P2-1/AM-P2-2/AM-P3-3.
- **Chain walks**: unlock's residual and reversed-LIFO grant walks; the locus of AM-P3-1.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: The AM-P1 multi-threaded stress witness passes a sustained run (target ≥ the project's
  established stress threshold for lost-wake/race findings) with zero lost waiters, zero
  double-grants, and zero pool corruption, and is demonstrably RED against the pre-fix code.
- **SC-002**: The AM-P2-2 death test terminates on the contract-violating destroy shape and does NOT
  terminate on the legitimate drained-then-destroyed shape (both directions asserted).
- **SC-003**: The full sanitizer matrix (ASan/UBSan/TSan) is green across the async_mutex suite,
  and the TSan run shows no data race on the free-list reuse path.
- **SC-004**: Branch coverage of `async_mutex.hpp` on the coverage lane reaches 100% of reachable
  branches, with every unreachable branch carrying a written unreachability proof (zero
  silently-uncovered branches).
- **SC-005**: The exhaustion witness demonstrates deterministic fail-closed behavior and no live-slot
  re-issue across a boundary that would have wrapped the previous 32-bit counter.
- **SC-006**: The layout golden and public API are unchanged (or any change is explicitly justified),
  and the `no-std-mutex` gate remains green.
- **SC-007**: Every added witness is mutation-verified (reverting its target fix turns it red).

## Assumptions

- **AM-P2-1 disposition (RESOLVED at `/clarify` 2026-07-02)**: strengthen the memory ordering
  (release/acquire) so cross-executor drain-then-destroy is memory-safe, AND tighten the
  drain/destructor documentation to state the drain is strand-local by design (guard-enforced). See
  FR-002/FR-003.
- **Coverage target (RESOLVED at `/clarify` 2026-07-02)**: 100% of reachable branches of
  `async_mutex.hpp` (lcov BRDA/DA), every unreachable branch waived with a written proof. The exact
  per-branch inventory + any waivers are enumerated at the coverage-design gate (after `/tasks`); the
  100%-reachable bar itself is fixed now. See FR-010/SC-004.
- Fixes are additive to the primitive; no consumer (MemoryStore/FileStore/SeqnumManager/Session)
  requires an API or call-site change.
- Native ARM64 weak-memory hardware remains unavailable in CI (per `test_arm64_weak_memory`); the
  AM-P2-1 ordering argument is therefore carried by reasoning + the release/acquire pairing, not a
  native weak-memory execution.
- The independence/role model from the parked plan holds: Fable = finder + Gate-A/B reviewer only;
  Opus/Codex = verification + design + implementation + hostile review; dual reviewers at both gates.
