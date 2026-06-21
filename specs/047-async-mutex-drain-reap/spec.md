# Feature Specification: async_mutex cancel_and_drain late-waiter reap (lost-wake fix)

**Feature Branch**: `047-async-mutex-drain-reap`
**Created**: 2026-06-21
**Status**: Draft
**Input**: Fix a lost-wake defect in `fixpp::sync::async_mutex::cancel_and_drain()` (core 006 primitive) where a waiter that begins acquisition concurrently with a drain can be orphaned (never resumed), hanging its caller. Discovered when the 046 multi-threaded witness hung the blocking Tier-1 `linux-clang-release` lane (~9 min); reproduced locally (libstdc++ release, 2-core pinned, ~1/3 runs); confirmed by an all-thread backtrace (4 idle pool workers, main blocked in `std::future::get`, one orphaned never-reaped waiter). Evidence: `research/findings/046-libcxx-tier3-and-006-lostwake.md`.

## Context & Background

`async_mutex` is the project's FIFO-fair asynchronous mutex (feature 006). Its
`cancel_and_drain()` operation cancels all current and future acquirers so the
mutex can be destroyed safely (e.g. at session close). The primitive **advertises
cross-thread use** — its `drain_latch_ptr_` is published via an atomic
shared-pointer specifically so a release-store on one thread pairs with an
acquire-load on another. A concurrency witness exercising that advertised
contract on a real multi-threaded executor exposed a lost wake.

**Not production-reachable today.** All four production consumers
(`Session::write_gate_`, `SeqnumManager::mutex_`, `memory_store::mutex_`,
`file_store::mutex_`) run on a per-session strand or an attested-serialized
`direct_executor`, so `async_lock()` and `cancel_and_drain()` on a given mutex
never execute truly concurrently. The defect is nonetheless real: it violates the
primitive's documented cross-thread contract, and a future multi-threaded
consumer (or a mis-attested `direct_executor`) would hit it. Severity: P2 (latent
correctness defect in a core primitive).

## Clarifications

### Session 2026-06-21

- Q: How should the multi-threaded witness reliably prove RED (catch the lost-wake), given it is a timing-dependent ~1/3 race? → A: Stress + self-deadline, **no production seam** — high thread-count × many rounds; a lost wake trips the witness's OWN internal deadline so it fails fast as a test failure (never a multi-minute CI hang). RED is proven by the documented pre-fix reproduction plus a mutation-revert (reverting the fix returns the witness to RED). No test-only scheduling hook is added to the production primitive.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Concurrent drain never orphans a waiter (Priority: P1)

A component built on `async_mutex` shuts the mutex down via `cancel_and_drain()`
while, on a different thread, another task is in the middle of an `async_lock()`
acquisition. The shutting-down task must observe the drain complete, and the
acquiring task must observe a definite outcome — never hang.

**Why this priority**: This is the defect. A single orphaned waiter hangs its
caller forever (its awaitable never resumes), which in turn can wedge any code
that joins on it. It is the reason the feature exists.

**Independent Test**: Run a multi-threaded scenario (a real multi-worker
executor) that repeatedly races `cancel_and_drain()` against N concurrent
`async_lock()` attempts and asserts every attempt finishes exactly once with a
definite result and the run never hangs. RED on current code, GREEN after the fix.

**Acceptance Scenarios**:

1. **Given** a held `async_mutex` with N acquirers attempting to lock on worker
   threads, **When** `cancel_and_drain()` runs concurrently on another thread,
   **Then** every acquirer completes exactly once (each either granted then
   released, or aborted), the count of completed acquirers equals N, and the
   scenario terminates with no hang.
2. **Given** an acquirer that parks its waiter record at the exact moment the
   reaper has finished its initial scan, **When** the drain proceeds, **Then**
   that late waiter is still reaped (resumed with an abort result), not left
   suspended.
3. **Given** `cancel_and_drain()` returns success, **When** the call completes,
   **Then** no acquirer that had begun before/around the drain remains suspended.

---

### User Story 2 - No regression to existing drain semantics & invariants (Priority: P2)

Every existing `async_mutex` behavior must remain identical: FIFO-fair grant
order, the uncontended no-suspension fast path, idempotent/concurrent
`cancel_and_drain()` re-entry, the reentrant-drain use-after-free closure, the
reaper's own-cancellation handling, `noexcept` operation, and the documented
acquisition/ordering invariants. The serialized (single-strand) execution path —
the only path used in production — must be a semantic no-op change.

**Why this priority**: The drain protocol is delicate (lock-free, with a body of
binding invariants and two prior hardening fixes). A fix that regresses any of
them trades one defect for another. Behavior preservation gates the fix.

**Independent Test**: The full existing 006 `async_mutex` test suite passes
unchanged across all sanitizer presets; the single-threaded drain tests behave
identically; idempotent/reentrant/own-cancellation drain tests stay green.

**Acceptance Scenarios**:

1. **Given** the existing 006 test suite, **When** the fix is applied, **Then**
   all tests pass with no behavior change on every preset.
2. **Given** a drain on an idle (never-locked or quiescent) mutex, **When**
   `cancel_and_drain()` is called, **Then** it remains a no-op that returns
   success.
3. **Given** concurrent/duplicate `cancel_and_drain()` callers, **When** they
   race, **Then** exactly one becomes the reaper and the rest subscribe and
   observe the same terminal outcome (idempotence preserved).

---

### User Story 3 - Permanent multi-threaded regression gate (Priority: P3)

The project must retain a continuously-run regression test that would catch this
class of lost-wake again. The multi-threaded drain-vs-lock witness (authored
under 046) is owned by this feature and runs in the standard CI lanes.

**Why this priority**: Without a permanent multi-thread witness, a future change
to the drain protocol could silently reintroduce the lost wake; the single-thread
tests cannot detect it. The witness is the durable guard.

**Independent Test**: The witness is present in the sync test suite, fails
deterministically enough on the pre-fix protocol to have caught the defect, and
passes reliably (no hang/flake) post-fix across repeated rounds on every lane.

**Acceptance Scenarios**:

1. **Given** the pre-fix protocol, **When** the witness runs, **Then** the lost
   wake is observable as a fast, attributable FAILURE via the witness's internal
   deadline (not a lane-wide hang) — establishing it as a true RED witness; a
   mutation-revert of the fix returns the witness to RED.
2. **Given** the fixed protocol, **When** the witness runs repeatedly under each
   sanitizer preset, **Then** it passes every time with no hang.

---

### Edge Cases

- A waiter increments the in-flight acquirer count, then parks its record *after*
  the reaper's initial list scan but while still counted — must be reaped.
- Multiple late waiters park concurrently during the same drain — all must be
  reaped.
- A late waiter whose own cancellation fires concurrently with the reaper — the
  CAS race resolves to exactly one terminal outcome (no double-resume, no lost
  wake).
- The reaper's own cancellation fires mid-drain — the documented abort-propagation
  outcome is preserved (subscribers woken, drain reports aborted).
- A new `async_lock()` that begins after the drain flag is set — must fast-fail
  with the drained result, never park.
- Drain on an idle mutex — no-op success.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: `cancel_and_drain()` MUST drive every acquirer that has begun an
  `async_lock()` acquisition to a single terminal outcome (granted-then-released,
  or aborted/drained), including an acquirer that parks its waiter record
  concurrently after the reaper's initial waiter-list scan. No begun acquirer may
  be left permanently suspended.
- **FR-002**: `cancel_and_drain()` MUST NOT report completion while any
  begun-but-not-yet-terminated acquirer remains unreaped; completion implies the
  waiter list is empty and all in-flight acquisition/holder/resumption activity
  has quiesced together (no ordering gap between "list scanned empty" and
  "acquirers quiesced" that could strand a late waiter).
- **FR-003**: Every `async_lock()` call that races a `cancel_and_drain()` MUST
  complete exactly once with exactly one result — never zero (hang) and never
  more than once (double-resume).
- **FR-004**: The fix MUST preserve all existing `async_mutex` guarantees:
  FIFO-fair grant ordering, the uncontended fast path (no suspension), idempotent
  and concurrent-caller `cancel_and_drain()` semantics, the reentrant-drain
  use-after-free closure, reaper own-cancellation propagation, `noexcept`
  operation, and the documented acquisition/ordering invariants.
- **FR-005**: On a single strand or attested-serialized executor (the production
  execution model), observable behavior MUST be unchanged — the fix is a semantic
  no-op for the serialized path.
- **FR-006**: A multi-threaded regression witness MUST exist that races
  concurrent drain vs. N lock attempts over repeated rounds on a real
  multi-worker executor, asserts every attempt completes exactly once (sum of
  aborted + granted equals N) with no hang, and runs under the standard CI
  sanitizer lanes. The witness MUST establish RED via stress (sufficient thread
  count × rounds to hit the late-park window with near-certainty) and MUST impose
  its OWN internal completion deadline so a lost wake surfaces as a fast,
  attributable test FAILURE — never a multi-minute lane-wide CI timeout. RED is
  evidenced by the documented pre-fix reproduction and a mutation-revert (removing
  the fix returns the witness to RED). It MUST NOT add any test-only scheduling
  hook or seam to the production primitive.
- **FR-007**: The change MUST NOT add or alter any public API, error code,
  configuration, wire format, codegen output, or C-ABI surface — it is an
  internal correctness fix to the drain protocol only.

### Key Entities

- **Acquirer / waiter record**: an in-flight `async_lock()` attempt; must reach
  exactly one terminal state (granted or aborted) under any drain timing.
- **Reaper (drain)**: the single `cancel_and_drain()` invocation that cancels
  waiters and quiesces the mutex; must not finalize until every begun acquirer is
  accounted for.
- **In-flight acquirer/holder/resumption counters**: the quiescence signals the
  reaper uses to know all concurrent activity has settled.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: The multi-threaded witness (real multi-worker executor, N
  concurrent acquirers, repeated rounds) completes with **zero hangs** across the
  full sanitizer preset matrix.
- **SC-002**: In every round, **100%** of acquirers reach a terminal state — the
  count of completed acquirers equals the number started (no lost wake, no
  double-resume).
- **SC-003**: The entire existing 006 `async_mutex` test suite passes with **no
  behavior change** on every preset (debug, release, ASan, UBSan, TSan, gcc).
- **SC-004**: **Zero** new sanitizer findings (TSan / ASan / UBSan) are introduced
  by the change.
- **SC-005**: The pre-fix protocol, run against the new witness, is shown to
  exhibit the lost wake (the witness is a genuine RED gate, not a tautology).

## Assumptions

- The witness file authored under the 046 feature
  (`test_async_mutex_drain_latch_publish_acquire`) is moved into / owned by this
  feature as its RED→GREEN gate; 046 rebases on top once this merges and no longer
  carries its own copy.
- The base for this work is `main` (the defect is present on `main`); the fix is
  orthogonal to 046's change of the `drain_latch_ptr_` storage type and merges
  with it cleanly.
- "Genuine multi-threaded concurrency" is exercised via a real multi-worker
  executor with no serializing strand — matching the primitive's advertised
  cross-thread contract, not the production single-strand usage.
- No production behavior change is expected or required; production consumers are
  strand-confined and already safe.
