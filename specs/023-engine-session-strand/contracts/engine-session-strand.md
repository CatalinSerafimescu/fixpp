# Internal Contract: Engine Two-Domain Serialization

**Feature**: 023-engine-session-strand
**Type**: Engine-internal contract — ONE safening-only public change (`Engine::lookup()`
return type `Session*` → `std::shared_ptr<Session>`, FR-008/SC-004); no other public change.
**Date**: 2026-06-05 (rev 4 — Gate A round-3 D-SNAP fixes: bounded handle + pinned primitive + stop-before-publish)

No new public type, command schema, or C-ABI surface; **one changed C++ public signature**
(`Engine::lookup()` return type `Session*` → `std::shared_ptr<Session>`, FR-008/SC-004/C-4).
The contract is an internal invariant on how the engine schedules work across **two**
serialization domains.
Recorded so Gate A and `/speckit-tasks` can verify it and future engine changes do not
regress it.

## C-0 — Engine control-plane domain (NEW)

There is exactly one engine control-plane serialization domain (a control strand). The
engine MUST **mutate** all engine-global state only within it, and **read** it either within
it or via the published immutable snapshot (C-8):

- the session `registry_`,
- the `stopped_` flag (authoritative write on the strand; reads are atomic-acquire — see C-5),
- the `listeners_` / `listener_endpoints_` tables,
- the `accept_scope_signals_` table,
- the in-flight counters' reset/clear ordering,
- the publication of `entry.session` / `entry.live_transport` that the shutdown path reads
  (awaited before the read pump — see C-6).

**Guarantee**: no two threads ever mutate engine-global state concurrently, and no read
observes an in-place-mutating structure, on a multi-threaded executor. Every cross-thread
entry point (any-thread `send`, `stop`) enters the control-plane domain before touching
engine-global state; the synchronous public readers (`lookup`, `acceptor_bound_endpoint`)
read the immutable `std::atomic<std::shared_ptr<const Snapshot>>` instead of entering the
domain (C-8).

## C-1 — Single per-session serialization domain

For every engine-managed session there is exactly one per-session domain (a session
strand, subordinate to C-0). The engine MUST schedule all of the following onto that one
domain: connection establishment, TLS handshake, the inbound read-pump
(`async_read_some` + framing + `on_inbound_frame`), application callbacks, outbound
`Engine::send` work for that session, the transport `close()`, and the terminal
`Session::close()`.

**Guarantee**: no two operations for the *same* session execute concurrently, even when
the executor is serviced by multiple threads. **Non-guarantee**: different sessions MAY
run concurrently (cross-session parallelism preserved — FR-004).

## C-2 — Hop semantics (no deadlock)

Every handoff into or between domains is a **non-blocking post**
(`co_spawn(strand, …, use_awaitable)`), never an inline `dispatch`-that-runs or a blocking
wait on a strand. Consequence: a send issued from inside an application callback
(`session strand → control strand → session strand`) cannot deadlock the domain it runs on.

## C-3 — Multi-threaded execution is supported (contingent)

- Multiple threads calling `io_context::run()` on the same context is a supported mode —
  **but only asserted once BOTH domains (C-0 and C-1) land with the full witness set passing
  (V-1, V-2, V-8, V-9, V-10, V-11, V-12) under a clean sanitizer matrix** (V-7 / FR-010 / SC-005).
  The behaviors-and-limitations **L-019-3** lift happens in the same PR and only then.
  Lifting it earlier would publish a false safety claim.
- The `direct_executor` opt-out remains the caller's serialization responsibility and is
  **out of scope** (unchanged; `error::executor_not_serialised` slot 48 still rejects an
  unattested direct executor).

## C-4 — Public surface: one intended, recorded change only

The only public surface change is `Engine::lookup()` returning `std::shared_ptr<Session>`
instead of a raw `Session*` (FR-008/SC-004/C-8) — a deliberate safening so the handle is
valid across a concurrent `stop()` / `registry_.clear()` **while the `Engine` is alive** (a
bounded handle — not valid past `~Engine`; see C-8). `abidiff` / `nm` **will** show
this one change; it is expected and documented, not a silent break. Every other
`Engine`/`Session` public type, signature, and required configuration is identical
before/after (no other new/changed exported symbols; `acceptor_bound_endpoint()` keeps its
`Endpoint`-by-value signature; no `c_api.h` change). No new configuration: the default
`per_session_strand` mode already selects per-session serialization; the control strand and
the reader snapshot are engine-internal.

## C-5 — `stopped_` access discipline

`stopped_` is an `std::atomic<bool>` (acquire/release): authoritative write on the control
strand in `stop()`; the accept-loop gate and the send fast-fail read it with atomic
acquire. It MUST NOT remain a plain non-atomic flag justified by single-thread confinement.

## C-6 — Teardown enumerates BOTH closes; publication is awaited

`stop()` MUST schedule, per live session: transport `close()` on the session strand
**before** the join (wake the idle read), AND terminal `Session::close()` on the session
strand **after** join + send-drain + **before** the control-strand registry clear
(preserving the documented liveness-loop-drain rationale). The send-drain ordering is
preserved explicitly. The control strand iterates a **stable** `registry_` across the
per-entry close `co_await`s (no insert/erase between `stopped_=true` and `clear()`).

Publication is **awaited**: the role loop `co_await`s its control-strand publish of
`entry.session` / `entry.live_transport` **before** entering the read pump, so `stop()`
cannot observe a null `live_transport` and skip the close-to-wake; the loop unpublishes on
exit; pending publish/unpublish jobs are part of the stop join. (The struck atomic
alternative for `live_transport` is not used — it gives read-definedness but not ordering.)

**Stop-before-publish (symmetric ordering — V-12).** The publish coroutine MUST check
`stopped_` **first** on the control strand. If `stop()` is already in progress (`stopped_`
true), the publish MUST **NOT** publish a live transport for pumping — it records a **stopped
disposition** (leaves `live_transport == nullptr` / closes the freshly-created transport) and
the session-strand role loop **closes/returns before initiating any read**. This closes the
case where `stop()` already saw `live_transport == nullptr` and a later queued publish would
otherwise expose a live transport after the stop sequence began.

## C-8 — Synchronous public readers read an immutable snapshot (D-SNAP)

`lookup()` and `acceptor_bound_endpoint()` MUST be safe to call from any thread while the
engine runs. The control strand **atomically publishes an immutable snapshot** held in a
**`std::atomic<std::shared_ptr<const Snapshot>>`** (standard C++20, header-only, **no
`std::mutex` in our headers** — Art. XV; RCU-style `atomic_store`) of the registry
`SessionId → shared_ptr<Session>` map and the bound-endpoint table after **every**
control-plane mutation. NOT the unshipped project `atomic_shared_ptr` (NFR-017, backlog —
only a possible later optimization). Each reader **`atomic_load`s** the current snapshot
(no domain entry, no `std::mutex`, no block — **wait-free where the STL's
`std::atomic<shared_ptr>` is lock-free**, otherwise STL-internal non-`std::mutex`
synchronization; the absolute "lock-free" claim is dropped, V-6 measures it) and
returns: `lookup()` → a `std::shared_ptr<Session>` drawn from it (keepalive across a
concurrent `registry_.clear()` **while the `Engine` is alive** — a **bounded handle**);
`acceptor_bound_endpoint()` → an `Endpoint` by value. No reader observes a control-plane
structure mutated in place. `stopped()` is covered separately by the C-5 atomic flag.

**Bounded handle (FR-008/FR-014).** The `lookup()`/snapshot `shared_ptr<Session>` is valid
across a concurrent `stop()` / `registry_.clear()` **only while the `Engine` is alive** —
`Session` borrows `const EngineConfig& engine_` (session.hpp:486), so dereferencing a handle
after `~Engine` is a UAF (same hazard as the send path at engine.cpp:726-730). The caller
MUST NOT let a `lookup()`/snapshot handle outlive the `Engine` (documented hard precondition,
NOT a `Session` dependency-model refactor); `~Engine` MUST **debug-assert** no outstanding
`lookup()`/snapshot handle remains — via a debug-only **lease control block** (a `shared_ptr`
control block runs no per-copy logic, only a deleter at last-owner destruction): the aliasing
`shared_ptr<Session>` owns a lease whose ctor increments an engine-owned `std::atomic<uint64_t>`
and whose dtor (last copy destroyed) decrements it, asserted zero at `~Engine` (a bare snapshot
`use_count()` cannot observe a handle copied out then detached from the snapshot). Release builds
carry no lease/counter. It is NOT a general keepalive past `~Engine`.

## C-7 — Verification obligations (consumed by /speckit-tasks)

| ID | Obligation | Evidence |
|----|------------|----------|
| V-1 | Per-session teardown: transport close serialized with the in-flight read | session-strand close ordering; MT acceptance test under ASan/UBSan/TSan |
| V-2 | Full lifecycle on a ≥3-thread executor is clean | `business_messages_roundtrip` (MT) + new MT lifecycle cells, ASan/UBSan/TSan |
| V-3 | Cross-session parallelism preserved | a 2-session MT cell shows independent progress; no engine-global serialization of unrelated sessions |
| V-4 | Single-threaded behavior unchanged | existing single-threaded suite green, no rewrites |
| V-5 | **One** intended API/ABI change only (`lookup() : Session* → shared_ptr<Session>`), no other | `abidiff`/`nm` show exactly the recorded `lookup()` diff and no other (SC-004/C-4) |
| V-6 | Perf within ±5% — **two-hop** send path (and acknowledge the per-establishment publish hop) **and the D-SNAP snapshot read/publish cost** (the `std::atomic<std::shared_ptr<const Snapshot>>` read — measure its real cost since "lock-free" is not guaranteed on every STL — and the per-control-mutation snapshot republish) | session-throughput (establish-churn, not just warm steady-state) **and send / send-from-callback** bench vs re-measured baseline (Art. VIII); record `is_lock_free()` of the snapshot atomic on the supported STL matrix |
| V-7 | L-019-3 lifted **only after** V-1 ∧ V-2 ∧ V-8 ∧ V-9 ∧ V-10 ∧ V-11 ∧ V-12 pass **and** a clean ASan/UBSan/TSan run | behaviors-and-limitations + concurrency doc edits, gated on the full set (FR-010/SC-005) |
| **V-8** | **Control-plane race deterministically witnessed via a ONE-SIDED PARK** (no bidirectional latch — an HB edge would suppress the race) | one-sided park on the **listener/endpoint-table** write (reachable pre-peer) while `stop()` clears from another thread, no shared sync object → TSan RED **every** pre-change run, GREEN post-change |
| **V-9** | **Re-entrant send across both domains has no deadlock under MT AND fails cleanly post-`stop()`** | `session→control→session` send-from-callback cell under TSan, ≥3 threads (no deadlock); a re-entrant send issued after `stop()` has begun fast-fails cleanly (stopped/`session_invalid_state_for_send`), never races a half-cleared registry |
| **V-10** | **Transport socket executor == session strand** | debug assert + a test over the four construction sites (engine listener-build, reconnect_fsm make, the two transport ctors) |
| **V-11** | **Snapshot public readers are MT-safe; `lookup()` is a bounded handle** (D-SNAP/C-8) | a TSan cell calling `lookup()` / `acceptor_bound_endpoint()` from a thread while the engine runs (and `stop()` clears) concurrently → TSan-clean; the `lookup()` handle obtained before `clear()` keeps its session alive **while the engine is alive**; `~Engine` debug-asserts no outstanding `lookup()`/snapshot handle remains |
| **V-12** | **Stop racing exactly between transport creation and the awaited publish takes the stopped disposition** (D-PUB/C-6/INV-2a) | a witness that drives `stop()` to set `stopped_` while a role loop sits between transport creation and its awaited control-strand publish → the publish observes `stopped_`, records the stopped disposition (no live publish), and the loop closes/returns **without** initiating a read; TSan-clean, no pumped transport exposed behind the in-progress `stop()` |
