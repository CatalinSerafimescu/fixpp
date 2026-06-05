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

There is a **second, co-equal** hazard the per-session boundary does not address:
the engine's *control plane*. The engine keeps engine-global bookkeeping — its
registry of sessions, its "stopping" flag, its listener/endpoint tables, and its
in-flight counters — and today reads and mutates that bookkeeping on the bare
executor with no serialization (it was only ever safe under single-thread
confinement). On a multi-threaded executor, a connection accepted on one thread
writes the registry/listener tables while shutdown on another thread clears them,
and an outbound send issued from any thread reads the registry while shutdown
mutates it — a data race on the engine's own data structures that is *worse* than
the TLS-teardown crash, and that no per-session boundary can fix.

This feature makes a multi-threaded executor a **first-class, supported** way to
run the engine by establishing **two** serialization domains:

1. a **per-session domain** — all work for a single session (establishment,
   handshake, read-pump, callbacks, sends, and teardown closes) is serialized so
   no two operations for that session ever overlap; and
2. an **engine control-plane domain** — all engine-global bookkeeping
   (registry, stopping flag, listener/endpoint tables, counters, and the
   publication of per-session handles the shutdown path reads) is serialized, and
   every cross-thread engine entry point (any-thread send, shutdown) routes
   through it.

Cross-session parallelism is preserved: unrelated sessions still progress on
different threads. The two domains together make multi-threaded operation safe by
construction — in steady state and during shutdown.

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
  forces the failure on **every** run of the pre-change engine (reliable RED) and
  is green post-change. A flaky/probabilistic stress reproducer is NOT acceptable
  as the gate. (Seam target refined in the Gate A round-1 session below.)

### Session 2026-06-05 (Gate A round 1)

Gate A round 1 (Codex + Opus adversarial) found the per-session domain necessary
but **not sufficient** and ruled the design structural; the following resolutions
expand the design to a two-domain model and are folded into the requirements/plan:

- Q: Is per-session serialization enough for multi-threaded support? → A: **No.**
  The engine *control plane* (registry, stopping flag, listener/endpoint tables,
  counters, per-session-handle publication) is a second, co-equal race class.
  A dedicated **engine control strand** is required; every engine-global access
  and every cross-thread entry point (any-thread send, shutdown) runs on it.
- Q: How is a session bound to the engine-created strand? → A: **D3-B** — extend
  the per-session-strand binding to *adopt a pre-created strand* while preserving
  the `per_session_strand` mode (and its strand-wrapped semantics + spin-policy
  legality). Do **not** internally rewrite a user's config to the `direct_executor`
  mode (verified to break a valid `locks = spin` config).
- Q: Is binding the transport's I/O object to the session strand optional? → A:
  **No — mandatory and asserted.** It is auto-satisfied when the role loop runs on
  the strand (the accepted/connected socket inherits the loop's executor), so the
  real obligation is to ensure **no** construction site samples the bare engine
  executor instead of the loop's executor, and to assert the socket's executor
  equals the session strand.
- Q: What does shutdown serialize? → A: **both** teardown closes on the session
  strand — the transport `close()` (before the join, to wake an idle read) **and**
  the terminal `Session::close()` (after join + send-drain, before registry clear,
  preserving the existing liveness-loop-drain) — with the registry iteration/clear
  confined to the control strand.
- Q: What does the deterministic witness target? → A: the **control-plane data
  race** (concurrent shutdown vs accept-loop registry/listener mutation), which a
  thread-sanitizer reports deterministically under a latch-controlled interleave.
  This is the feasible, root-cause-targeting witness; the TLS-teardown crash is a
  downstream symptom covered by the multi-threaded acceptance test. (Supersedes
  the earlier "force close between eof-arrival and read completion" seam, which is
  not reachable from the transport interface.)

### Session 2026-06-05 (Gate A round 2)

Gate A round 2 (Codex + Opus adversarial) confirmed the rev-2 two-domain
architecture sound and closed RC#2 fully; it surfaced one residual that required a
**user decision** — the synchronous public readers `lookup()` /
`acceptor_bound_endpoint()` read control-plane maps off any strand, which a
synchronous accessor cannot route through an async strand without either an API
change, a block (banned), or a published snapshot.

- Q: How are the synchronous public readers made MT-safe — narrow them to
  quiescent-only (preserves signatures, weakens the de-facto contract) or keep
  them callable any-thread (needs a snapshot, and `lookup()`'s raw-pointer return
  must become shared-ownership)? → A: **Full MT-safe (accept the API change).**
  `acceptor_bound_endpoint()` returns its `Endpoint` value from an
  **atomically-published immutable snapshot** (no signature change). `lookup()`
  changes its return type from `Session*` to `std::shared_ptr<Session>` — a
  deliberate, accepted, **safening-only** public API change so the handle is valid
  across `stop()` / `registry_.clear()` **while the `Engine` is alive** (bounded
  handle — see the round-3 note below). The control plane publishes an immutable
  snapshot after each mutation (RCU-style); readers `atomic_load` it (no domain
  entry, no `std::mutex`). FR-008 is amended to permit this one change; FR-011/FR-014
  encode the read discipline; SC-004 records the intended `abidiff`/`nm` delta.

### Session 2026-06-05 (Gate A round 3)

Gate A round 3 (Codex) confirmed the rev-3 two-domain + D-SNAP architecture and
closed RC#B/RC#C/RC#D; it surfaced two D-SNAP residuals and three stale-marker fixes,
applied here (rev-4) without an architecture change. The one item needing a **user
decision** — the lifetime of the `lookup()` keepalive vs `~Engine`:

- Q: A `lookup()` `shared_ptr<Session>` can keep a `Session` alive past `~Engine`,
  but `Session` borrows `const EngineConfig& engine_` (session.hpp:486) → a UAF if a
  handle is dereferenced after `~Engine` (the same hazard the send path documents at
  engine.cpp:726-730). Re-own `Session`'s engine dependencies behind a
  `shared_ptr<const EngineRuntime>`, or keep `Session`'s borrowed back-reference and
  make the keepalive a **bounded handle**? → A: **Bounded handle (accept the
  precondition; do NOT refactor `Session`'s dependency model).** `lookup()`'s
  `shared_ptr<Session>` is valid across a concurrent `stop()` / `registry_.clear()`
  **only while the `Engine` is alive**; the caller MUST NOT let a `lookup()`/snapshot
  handle outlive the `Engine`. This is a documented hard precondition, guarded by a
  **debug `~Engine` assertion** that no outstanding `lookup()`/snapshot handle remains,
  via the debug-only **lease control block** mechanism of FR-014 (NOT a snapshot
  `use_count()` check — that cannot observe a handle copied out then detached).
  FR-008/FR-014, E-7, and C-8 encode the bound; the keepalive is **not** a general
  keepalive past `~Engine`.

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

The shutdown-time concurrency defects (the engine control-plane data race on the
registry/listener tables, and the downstream TLS-teardown crash) no longer occur,
and a regression guard prevents the root-cause race from returning.

**Why this priority**: Reliability and regression protection. The defects are
rare and timing-dependent, so they need a dedicated witness that *deterministically*
reproduces the root-cause race pre-fix and proves the post-fix safety.

**Independent Test**: A regression witness that **deterministically** reproduces
the engine control-plane data race on the pre-change engine (a latch-controlled
interleave of shutdown vs. an accept-loop registry/listener mutation, reported by
the thread-sanitizer) fails every pre-change run and passes 100% of runs on the
post-change engine.

**Acceptance Scenarios**:

1. **Given** the regression witness driving a latch-controlled interleave of
   shutdown against an accept-loop registry/listener mutation, **When** it is run
   against the pre-change engine under the thread-sanitizer, **Then** the race is
   reported on every run (reliable RED).
2. **Given** the same witness, **When** it is run against the changed engine
   repeatedly under sanitizers, **Then** it passes every run with no race, crash,
   or sanitizer finding.

---

### User Story 3 - Existing single-threaded users are unaffected (Priority: P3)

Applications that run the engine on a single-threaded executor (the only
currently-supported mode) see no behavioral change: identical message handling,
ordering, and lifecycle, and a public interface unchanged apart from the single
recorded `lookup()` return-type safening (FR-008/SC-004).

**Why this priority**: Backward-compatibility guard. The fix must not regress
the supported path, and the only permitted public-surface change is the recorded
`lookup()` safening (FR-008).

**Independent Test**: The full existing test suite remains green with no
single-threaded test rewrites, and the public interface is unchanged apart from
the single recorded `lookup()` return-type safening (FR-008/SC-004).

**Acceptance Scenarios**:

1. **Given** the existing single-threaded test suite, **When** it is run against
   the changed engine, **Then** all tests pass unchanged.
2. **Given** the published engine/session interface, **When** it is compared
   before and after the change, **Then** the only difference is the single
   recorded `lookup()` return-type safening (`Session*` → `shared_ptr<Session>`,
   FR-008/SC-004); no other public type, signature, or required configuration
   changes.

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
- **Shutdown vs. connection accept**: shutdown clears the registry/listener
  tables on one thread while an accept loop is publishing a newly accepted
  session into those tables on another. Must be serialized within the
  control-plane domain.
- **Any-thread send vs. shutdown**: an outbound send issued from an external
  thread reads the registry while shutdown mutates/clears it. The send must
  resolve the registry inside the control-plane domain (and fail cleanly if
  shutdown has begun), never read a half-cleared registry.
- **Synchronous public reader vs. shutdown**: a synchronous public introspection
  call (`lookup()` / `acceptor_bound_endpoint()`) issued from an application
  thread while shutdown clears the registry/endpoint tables on another. The reader
  must observe a consistent immutable snapshot (never an in-place-mutating
  structure), and a `lookup()` handle obtained just before `registry_.clear()`
  must keep its session alive (shared ownership) for as long as the `Engine` is
  alive (bounded handle — not valid past `~Engine`; FR-008/FR-014).

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
  continue to complete without deadlock under multi-threaded operation, including
  when the send must traverse both serialization domains
  (session → control plane → session). Every cross-domain handoff MUST be a
  non-blocking handoff (never a synchronous wait that occupies a serialization
  domain), so a callback-issued send cannot deadlock the domain it runs on.
- **FR-007**: The behavior of an engine run on a single-threaded executor MUST
  be unchanged by this feature (message handling, ordering, and lifecycle
  identical to the prior behavior).
- **FR-008**: The public engine and session interface MUST be limited to a
  **minimal, safening-only** change: the sole permitted signature change is
  `Engine::lookup()` returning a **shared-ownership handle**
  (`std::shared_ptr<Session>`, was a raw `Session*`) so the returned handle stays
  valid across `stop()` / `registry_.clear()` under multi-threaded operation. The
  handle is a **bounded handle**: it is valid only **while the `Engine` is alive** —
  because `Session` borrows the engine's runtime config, the caller MUST NOT let a
  `lookup()`/snapshot handle outlive the `Engine` (a documented hard precondition,
  debug-asserted at `~Engine`; see FR-014). It is NOT a general keepalive past
  `~Engine`. No other public signature change, no public type removal, no
  required-configuration change, and no new mandatory step for applications to obtain
  safe multi-threaded behavior under the default configuration. (This single change is
  the user-accepted cost of making the synchronous public readers MT-safe at runtime —
  see FR-014/D-SNAP; it is recorded, not silent — see SC-004.)
- **FR-009**: The feature MUST reuse the engine's existing strand primitive (the
  same mechanism behind the default per-session strand mode) for both
  serialization domains rather than introducing a new serialization mechanism or
  any lock.
- **FR-010**: The previously-documented limitation that a multi-threaded
  executor is unsupported MUST be lifted **only once both serialization domains
  land and the full set of witnesses passes** — the per-session
  teardown/lifecycle witnesses (V-1, V-2), the transport-on-strand identity
  witness (V-10), the control-plane race witness (V-8), the re-entrant-send
  witness (V-9), the snapshot-reader MT witness (V-11), and the
  publish-vs-stop ordering witness (V-12) — **and** a clean
  address-, undefined-behavior-, and thread-sanitizer run. Lifting the limitation
  while any of those is still racy or failing is prohibited: the supported-mode
  documentation MUST NOT claim multi-threaded safety until **all** of V-1, V-2,
  V-8, V-9, V-10, V-11, V-12 pass under a clean sanitizer matrix.
- **FR-011**: All engine control-plane state — the session registry, the
  stopping flag, the listener and endpoint tables, the in-flight counters, and
  the publication of per-session handles that the shutdown path reads — MUST be
  **mutated** only within a single engine control-plane serialization domain, so
  no two threads ever mutate engine-global state concurrently, and no read
  observes a partially-mutated structure, when the executor is serviced by
  multiple threads. Engine-global state is **read** either (a) within the
  control-plane domain, or (b) — for the synchronous public readers `lookup()` /
  `acceptor_bound_endpoint()` — from an **atomically-published immutable
  snapshot** of that state (FR-014). No read path observes a structure that the
  control-plane domain is concurrently mutating in place. The per-session handle
  publication (`session` / `live_transport`) MUST be **unpublished** (reset on
  the control strand) on **every** role-loop exit path — normal return,
  cancellation, AND error — before the entry can be cleared, so that `stop()`
  never reads a stale handle for a loop that has ended.
- **FR-012**: Every cross-thread engine entry point MUST enter through the
  control-plane domain before touching engine-global state: an any-thread
  outbound send MUST resolve the registry/stopping flag inside the control-plane
  domain before handing off to the target session's domain; engine shutdown MUST
  snapshot, signal-cancel, drain, and clear engine-global state inside the
  control-plane domain.
- **FR-013**: The stopping flag that gates the role loops and the send path MUST
  have a defined cross-thread access discipline (read within the control-plane
  domain, or via an atomic with acquire/release ordering) — it MUST NOT remain a
  plain non-atomic flag justified by single-thread confinement.
- **FR-014**: The synchronous public readers `lookup()` and
  `acceptor_bound_endpoint()` MUST be safe to call from any thread while the
  engine runs concurrently. The control-plane domain MUST, after every
  control-plane mutation, **atomically publish an immutable snapshot** of the
  state these readers need (the registry's `SessionId → shared_ptr<Session>` map
  and the bound-endpoint table); each reader **atomically loads** the current
  snapshot (no domain entry, no `std::mutex`, no blocking — consistent with the
  no-`std::mutex` constraint; the standard `std::atomic<std::shared_ptr<…>>`
  primitive, wait-free where the STL makes it lock-free and otherwise STL-internal
  non-`std::mutex` synchronization, not an absolute lock-free guarantee) and returns
  a value / shared-ownership handle drawn from it. `lookup()`'s returned `shared_ptr<Session>` keeps the session alive
  past a concurrent `stop()` / `registry_.clear()` **while the `Engine` is alive**
  (FR-008, bounded handle) — it is NOT valid past `~Engine`, because `Session`
  borrows the engine's runtime config; the caller MUST NOT let a `lookup()`/snapshot
  handle outlive the `Engine`. This precondition MUST be **debug-asserted at
  `~Engine`** via a realizable **lease-control-block** mechanism (NOT per-copy
  hooks — a `std::shared_ptr` control block cannot run logic on every copy, only a
  deleter when the last owner of that control block is destroyed): in debug builds
  `lookup()` returns an **aliasing** `std::shared_ptr<Session>` whose owning control
  block holds a small **lease** object; the lease constructor increments an
  engine-owned `std::atomic<std::uint64_t>` outstanding-lease counter, and the lease
  destructor (run when the **last** copy sharing that control block is destroyed)
  decrements it. `~Engine` asserts the counter is **zero**. This does not count
  every copy individually, but it proves the required property — no outstanding
  returned handle exists. (A bare `use_count()` on the published snapshot is
  insufficient — a caller can copy the `Session` handle out and drop the snapshot,
  so the snapshot's count would never observe it.) In release builds the handle is a
  plain `std::shared_ptr<Session>` with no lease/counter overhead; the bounded-handle
  contract then holds by caller
  obligation. `stopped()` is covered separately by the atomic stopping flag (FR-013).

### Key Entities

- **Engine control-plane domain**: a single engine-wide serialization boundary
  inside which all engine-global bookkeeping (session registry, stopping flag,
  listener/endpoint tables, counters, per-session-handle publication) is read and
  mutated; every cross-thread engine entry point routes through it.
- **Session serialization domain**: the per-session boundary inside which all of
  one session's operations are ordered so none overlap; the unit of
  serialization is a single session, not the whole engine. Distinct from, and
  subordinate to, the control-plane domain.
- **Engine-managed session lifecycle work**: the engine-owned activities for a
  session (establishing the connection, the inbound read loop, and teardown)
  that must run inside that session's serialization domain.
- **Teardown step**: the shutdown action that closes a session's live connection
  (transport close) and the terminal session close; both must occur inside the
  session's serialization domain, while the surrounding registry iteration/clear
  occurs inside the control-plane domain.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: The engine completes a full session lifecycle (establish →
  application message round-trip → shutdown) on an executor serviced by 3 or more
  threads with **zero** address-, undefined-behavior-, and thread-sanitizer
  findings and zero crashes across repeated runs.
- **SC-002**: A regression witness reproduces the **engine control-plane data
  race** on the pre-change engine via a **one-sided park** seam — a test-only
  delay that parks one thread *inside / immediately adjacent to* an unsynchronized
  listener/endpoint-table access (the publication point reachable before any peer
  session exists) while shutdown clears those tables from another thread, with
  **no happens-before edge** between the two racing accesses (a bidirectional
  latch is forbidden — it would synchronize the accesses and suppress the very
  race the thread-sanitizer must report). The park window is wide enough that the
  conflicting access reliably lands inside it, so the thread-sanitizer reports the
  race on **every** pre-change run (reliable RED) and the witness passes **100%**
  of runs on the post-change engine. (The TLS-teardown crash is a downstream
  symptom covered by the multi-threaded acceptance test of SC-001.)
- **SC-003**: The complete existing test suite remains green with **no**
  single-threaded test rewrites required.
- **SC-004**: The public engine/session interface has **no unintended** API/ABI
  change: exactly one intended, recorded signature change — `Engine::lookup()`
  returns `std::shared_ptr<Session>` instead of a raw `Session*` (FR-008) — and no
  other. `abidiff` / `nm` WILL show this one change; it is expected and documented,
  not a silent break. Every other public type, signature, and required
  configuration is identical before vs. after.
- **SC-005**: Operator-facing documentation no longer lists a multi-threaded
  executor as unsupported; multi-threaded operation is described as safe under
  the default configuration.

## Assumptions

- The engine's existing strand machinery (the mechanism behind the default
  per-session threading mode, from the threading/clock feature) is reused for
  **both** domains — the per-session strand and a new engine control strand — so
  no new strand or locking abstraction is introduced.
- "Multi-threaded executor" means several threads concurrently servicing the
  same execution context; the control-plane strand serializes engine-global state
  and per-session strands serialize each session's work on top of it.
- The two domains are distinct strands over the same executor; every handoff
  between them (and into them from an external thread) is a non-blocking post, so
  no thread is ever blocked holding a domain.
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

- Any change to the public engine or session API surface beyond the single
  recorded `lookup()` return-type safening (`Session*` → `shared_ptr<Session>`,
  FR-008/SC-004).
- Making the expert "direct executor" opt-out path safe under multiple threads.
- Reconnect / multi-cycle session changes.
- Cross-session ordering guarantees (only per-session serialization is in scope;
  unrelated sessions remain independently scheduled).
