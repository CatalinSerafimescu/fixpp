# Feature Specification: Per-Session Strand Binding for Engine-Managed Sessions

**Feature Branch**: `023-engine-session-strand`
**Created**: 2026-06-05
**Status**: Draft
**Input**: User description: "Per-session strand binding for engine-managed sessions so a multi-threaded io_context is genuinely supported (fixes the flaky OpenSSL BIO_ctrl SEGV/UAF in business_messages_roundtrip TLS teardown)."

## Overview

An application that embeds the fixpp engine drives it with an executor (an
`io_context` or thread pool). Today the engine is only safe when that executor
is serviced by a **single** thread ("single-executor confinement"): every piece
of per-session work — establishing the connection, reading and framing inbound
bytes, dispatching application callbacks, sending outbound messages, and tearing
the session down — is correct only because one thread runs it all in sequence.

The engine already carries a per-session serialization primitive (the
per-session strand, the default threading mode) that *callbacks and sends* use,
but the engine's own connection/read loops and its teardown step do **not** run
inside that serialization domain. When an application runs the engine on a
multi-threaded executor, two threads can therefore touch the *same* session at
the same time. The most damaging instance is during shutdown: the teardown step
closes a session's live network connection on one thread while that session's
in-flight inbound read completes on another, corrupting the shared TLS state and
crashing the process.

This feature makes a multi-threaded executor a **first-class, supported** way to
run the engine, by ensuring that **all** work for a single session — including
the engine's read loop and the teardown close — happens inside that session's
serialization domain. Two operations for the same session never run
concurrently, even on a many-threaded executor, and even during shutdown.

## Clarifications

### Session 2026-06-05

- Q: Which engine-owned operations must be re-bound to the session's existing
  (callback/send) strand? → A: The **whole role loop** — connection
  establishment, TLS handshake, and the inbound read-pump — **and** the shutdown
  teardown close, all on the session's single existing strand. This is the
  simplest race-free invariant ("all work for a session on its one strand") and
  satisfies the transport's own strand-confinement contract.
- Q: What reproduction standard must the regression witness (US2 / SC-002) meet?
  → A: **Deterministic** — a controlled interleaving (a test-only barrier/seam)
  forces the teardown close to run between an in-flight read's eof-arrival and
  its completion, so the witness reproduces the failure on **every** run of the
  pre-change engine (reliable RED) and is green post-change. A flaky/probabilistic
  stress reproducer is NOT acceptable as the gate.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Run the engine on a multi-threaded executor safely (Priority: P1)

An application embeds the engine and services its executor with a pool of
threads (to parallelize work across many sessions). The application establishes
sessions, exchanges application messages, and later shuts the engine down. The
engine operates and tears down cleanly: no crash, no memory corruption, no data
race — regardless of how many threads service the executor.

**Why this priority**: This is the entire value of the feature. Multi-threaded
operation is currently unsupported (a documented limitation); steady-state and
teardown races make it unsafe. Making it safe is the deliverable.

**Independent Test**: Drive a full session lifecycle (establish → application
message round-trip → shut down) with the executor serviced by multiple worker
threads, under address/undefined-behavior/thread sanitizers, and observe zero
findings and zero crashes across repeated runs.

**Acceptance Scenarios**:

1. **Given** the engine running on an executor serviced by 3+ threads with at
   least one established session, **When** the application shuts the engine down
   while an inbound read is in flight, **Then** the session's connection is torn
   down without any concurrent access to that session's network/TLS state and
   without a crash or sanitizer finding.
2. **Given** the engine running on an executor serviced by 3+ threads, **When**
   an application message round-trip occurs concurrently with normal engine
   activity, **Then** message handling and delivery ordering are correct and no
   two operations for the same session run simultaneously.
3. **Given** an application that sends an outbound message from inside an
   application callback while the engine runs multi-threaded, **When** the send
   is issued, **Then** it completes without deadlock and without racing the
   session's other work.

---

### User Story 2 - The teardown crash is eliminated and stays eliminated (Priority: P2)

The specific intermittent shutdown crash (a use-after-free / data race in the
TLS layer when teardown races an in-flight inbound read) no longer occurs, and a
regression guard prevents it from returning.

**Why this priority**: Reliability and regression protection. The crash is rare
and timing-dependent, so it needs a dedicated witness that reliably reproduces
the pre-fix failure and proves the post-fix safety.

**Independent Test**: A regression witness that **deterministically** reproduces
the teardown race/crash on the pre-change engine (via a controlled interleaving
seam) fails every pre-change run and passes 100% of runs on the post-change
engine under the sanitizer matrix.

**Acceptance Scenarios**:

1. **Given** the regression witness for the teardown race driven by a controlled
   interleaving seam, **When** it is run against the pre-change engine, **Then**
   it reproduces the failure on every run (reliable RED).
2. **Given** the same witness, **When** it is run against the changed engine
   repeatedly under sanitizers, **Then** it passes every run with no crash or
   sanitizer finding.

---

### User Story 3 - Existing single-threaded users are unaffected (Priority: P3)

Applications that run the engine on a single-threaded executor (the only
currently-supported mode) see no behavioral change: identical message handling,
ordering, lifecycle, and public interface.

**Why this priority**: Backward-compatibility guard. The fix must not regress
the supported path or change the public surface.

**Independent Test**: The full existing test suite remains green with no
single-threaded test rewrites, and the public interface is unchanged.

**Acceptance Scenarios**:

1. **Given** the existing single-threaded test suite, **When** it is run against
   the changed engine, **Then** all tests pass unchanged.
2. **Given** the published engine/session interface, **When** it is compared
   before and after the change, **Then** there is no change to public types,
   signatures, or required configuration.

---

### Edge Cases

- **Teardown vs. in-flight read**: shutdown closes a session's connection at the
  same moment that session's inbound read is completing (the original crash).
  Must be serialized within the session's domain.
- **Teardown vs. in-flight send**: shutdown overlaps an outbound send still in
  progress for the same session. Must be serialized within the session's domain.
- **Re-entrant send**: an application callback triggers an outbound send while
  the engine is multi-threaded. Must not deadlock and must not race.
- **Many concurrent sessions**: distinct sessions may legitimately run in
  parallel on different threads; the feature serializes *within* a session, not
  across unrelated sessions (cross-session parallelism is preserved).
- **Idle established session at shutdown**: a session blocked on an inbound read
  with no peer traffic must still be unblocked and torn down (the close-to-wake
  behavior is preserved, now performed inside the session's domain).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: All work for a single engine-managed session — connection
  establishment, TLS handshake, inbound read/framing, inbound message dispatch,
  outbound send, and teardown (i.e. the entire engine-owned per-session role
  loop plus its shutdown close) — MUST execute within that session's single
  serialization domain, such that no two operations for the same session run
  concurrently, even when the engine's executor is serviced by multiple threads.
- **FR-002**: Engine shutdown MUST perform each session's connection teardown
  (the close that both releases resources and unblocks an idle inbound read)
  inside that session's serialization domain, so teardown never runs
  concurrently with that session's in-flight read or write completion.
- **FR-003**: Running the engine on a multi-threaded executor MUST NOT produce
  data races or memory-safety violations during steady-state operation or during
  shutdown, as verified under address-, undefined-behavior-, and
  thread-sanitizers.
- **FR-004**: Cross-session parallelism MUST be preserved — unrelated sessions
  MAY continue to make progress on different threads concurrently; the
  serialization guarantee is per-session, not engine-global.
- **FR-005**: Inbound message ordering and exactly-once in-order delivery
  guarantees MUST be preserved under multi-threaded operation.
- **FR-006**: Outbound send issued from within an application callback MUST
  continue to complete without deadlock under multi-threaded operation.
- **FR-007**: The behavior of an engine run on a single-threaded executor MUST
  be unchanged by this feature (message handling, ordering, and lifecycle
  identical to the prior behavior).
- **FR-008**: The public engine and session interface MUST remain unchanged — no
  new or altered public types, function signatures, or required configuration,
  and no new mandatory step for applications to obtain safe multi-threaded
  behavior under the default configuration.
- **FR-009**: The feature MUST reuse the engine's existing per-session
  serialization primitive (the default per-session strand mode) rather than
  introducing a new serialization mechanism.
- **FR-010**: The previously-documented limitation that a multi-threaded
  executor is unsupported MUST be lifted, and the supported-mode documentation
  updated to reflect that multi-threaded operation is now safe under the default
  configuration.

### Key Entities

- **Session serialization domain**: the per-session boundary inside which all of
  one session's operations are ordered so none overlap; the unit of
  serialization is a single session, not the whole engine.
- **Engine-managed session lifecycle work**: the engine-owned activities for a
  session (establishing the connection, the inbound read loop, and teardown)
  that must run inside that session's serialization domain.
- **Teardown step**: the shutdown action that closes a session's live connection;
  it both frees resources and unblocks an otherwise-idle inbound read, and must
  occur inside the session's serialization domain.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: The engine completes a full session lifecycle (establish →
  application message round-trip → shutdown) on an executor serviced by 3 or more
  threads with **zero** address-, undefined-behavior-, and thread-sanitizer
  findings and zero crashes across repeated runs.
- **SC-002**: A regression witness that **deterministically** reproduces the
  teardown race/crash on the pre-change engine (via a controlled interleaving
  seam — failing every pre-change run) passes **100%** of runs on the post-change
  engine under the sanitizer matrix.
- **SC-003**: The complete existing test suite remains green with **no**
  single-threaded test rewrites required.
- **SC-004**: The public engine/session interface is **unchanged** (no public
  type, signature, or required-configuration differences before vs. after).
- **SC-005**: Operator-facing documentation no longer lists a multi-threaded
  executor as unsupported; multi-threaded operation is described as safe under
  the default configuration.

## Assumptions

- The engine's existing per-session strand machinery (the default threading
  mode, introduced by the threading/clock feature) is sufficient as the
  serialization primitive and is reused — no new strand or locking abstraction is
  introduced.
- "Multi-threaded executor" means several threads concurrently servicing the
  same execution context; per-session strands provide the required serialization
  on top of it.
- Applications that explicitly opt out of per-session strands (the expert
  "direct executor" mode) remain responsible for their own serialization; making
  that opt-out path safe under multiple threads is **out of scope** for this
  feature.
- Network-transport operations for a session are issued from within that
  session's serialization domain once this feature lands, so the transport's own
  in-flight-operation state is only ever touched by one thread at a time.
- Reconnect/multi-cycle behavior is unchanged and out of scope.
- The default configuration already selects the per-session strand mode, so
  applications obtain safe multi-threaded behavior without any new configuration.

## Out of Scope

- Any change to the public engine or session API surface.
- Making the expert "direct executor" opt-out path safe under multiple threads.
- Reconnect / multi-cycle session changes.
- Cross-session ordering guarantees (only per-session serialization is in scope;
  unrelated sessions remain independently scheduled).
