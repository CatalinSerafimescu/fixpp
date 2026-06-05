# Data Model: Per-Session + Control-Plane Strand Binding

**Feature**: 023-engine-session-strand
**Date**: 2026-06-05 (rev 4 — Gate A round-3 D-SNAP fixes: bounded handle + pinned primitive + stop-before-publish)

No persisted data, no public types. The entities are engine-internal runtime
serialization domains and bindings, documented so `/speckit-tasks` and Gate A can
reason about ownership, publication, and ordering. **Two** domains exist (E-0
control-plane, E-1 per-session); everything else hangs off them — including the
public-reader immutable snapshot (E-7) that decouples the synchronous public readers
from the control-plane mutation domain.

## E-0 — Engine control-plane domain (NEW)

- **What**: one engine-owned control strand (`asio::make_strand(exec_)`), created once
  in the `Engine`. The single serialization domain for ALL engine-global state.
- **Owner**: the `Engine` (a new member; e.g. `control_strand_`). Created at construction
  / `start()`; lives for the engine's lifetime.
- **Serializes (INV-0)**: `registry_`, `stopped_` (write), `listeners_`,
  `listener_endpoints_`, `accept_scope_signals_`, the counters' reset/clear ordering,
  and the publication of `entry.session` / `entry.live_transport`. No engine-global
  structure is read or mutated off this strand under MT.
- **Entry points routed through it**: `Engine::start()` (strand creation + loop spawn),
  `Engine::send` (registry/stopped read + counter bump — first hop), `Engine::stop()`
  (snapshot / signal-cancel / per-session-close dispatch / join-drive / clear).
- **Relationship**: 1 per engine; parent domain of every E-1 session strand. Distinct
  strand from every session strand (so they serialize independently, never block each other).

## E-1 — Per-session strand

- **What**: one `asio::strand` per registered session via `asio::make_strand(exec_)`.
- **Owner**: the engine, stored on `SessionEntry` (E-2). Created **on the control strand**
  in `start()` before the role loop is spawned.
- **Lifetime**: from `start()` to control-strand-confined `registry_.clear()` in `stop()`.
- **Invariant (INV-1)**: exactly one strand per session; the loop's executor, the
  `Session`'s bound executor, the transport's socket executor, and both teardown closes
  all use this same strand. Two operations for the session never run concurrently.

## E-2 — `SessionEntry` (amended)

Existing struct (`include/fixpp/session/engine.hpp:119`). Amendments:

- **Add**: a per-session strand handle field (E-1; e.g. `session_strand`), the resolved
  serialized executor used to spawn the loop, bind the `Session`, construct the transport,
  and post both teardown closes.
- **Publication discipline (INV-2 / D-PUB)**: `session` (`shared_ptr<Session>`) and
  `live_transport` (raw pointer) — the fields `stop()` reads — are **published on the control
  strand**, and the role loop **`co_await`s** that publication (`co_await
  co_spawn(control_strand, publish, use_awaitable)`) **before** it enters the read pump, so
  `stop()` can never observe a null `live_transport` and skip the close-to-wake. The publish
  job is part of the **outstanding-loop join accounting** (stop's join does not complete while
  a publish is pending). On loop exit the loop **unpublishes** (resets `session` /
  `live_transport`) on the control strand before the entry can be cleared. The struck
  atomic-`live_transport` alternative is NOT used (it gives read-definedness but not the
  ordering — research D-PUB). No bare-session-strand write that the control strand later reads.
- **Stopped-before-publish disposition (INV-2a / D-PUB / V-12)**: the publish coroutine checks
  `stopped_` **first** on the control strand. If `stop()` is already in progress (`stopped_`
  true), it MUST **NOT** publish a live `live_transport` for pumping — it records a **stopped
  disposition** (leaves `live_transport == nullptr` / closes the freshly-created transport) and
  the session-strand role loop **closes/returns before initiating any read** (never enters
  `async_read_some`). This closes the symmetric *stop-before-publish* hole (Gate A round-3): a
  transport created just before the awaited publish is never pumped once `stop()` has begun.
- **Unchanged**: `session_role`, `config`, `session_cancel`. The `live_transport`
  close-to-wake contract is preserved; only the executor it runs on (the session strand)
  and the publication discipline change.

## E-3 — Session ↔ strand binding (D3-B)

- **What**: the `Session` adopts the engine's per-session strand (E-1) rather than minting
  its own at `open()`.
- **Mechanism (committed — D3-B)**: extend the per-session-strand binding so that under
  `threading_mode::per_session_strand`, an engine-supplied **already-strand** executor is
  stored directly (with `strand_wrapped=true`) instead of being re-wrapped in a second
  `make_strand`. Preserves the mode, `is_strand_wrapped()==true`, and `lock_policy::spin`
  legality. (NOT the rejected D3-A `direct_executor` rewrite — that breaks spin-policy
  configs.)
- **Adoption seam (D3-B / INV-3a)**: adoption is triggered by an **engine-only internal
  parameter/overload** (an `adopt_strand` tag distinct from the public config), **NOT** by
  inferring from the public `already_serialized_executor` flag — a user may set that flag
  under `per_session_strand`, and inferring from it would silently adopt a bare (non-strand)
  executor as a strand (research D3-B). The ordinary user `per_session_strand` path still
  `make_strand`-wraps.
- **Invariant (INV-3)**: `session->executor().underlying()` equals the `SessionEntry`
  strand (E-1) — a **debug assert AND a test**, not just prose. `Engine::send`'s
  session-strand hop (`kl->executor().underlying()`, engine.cpp:860) then lands on the same
  domain as the read-pump and both teardown closes. This catches the D1 anti-pattern (a
  second distinct `make_strand`).

## E-4 — Teardown ordering (amended — BOTH closes)

`Engine::stop()` per live session, with domains explicit:

1. **(control strand)** set `stopped_` true (atomic release), snapshot + total-cancel
   every `session_cancel` / `accept_scope_signals_`. (Because this runs on the control
   strand, any concurrent role-loop's awaited D-PUB publish that arrives after this sees
   `stopped_` true and takes the INV-2a stopped disposition — never publishes a pumped
   transport behind an in-progress `stop()`.)
2. **(session strand, before join)** dispatch transport `close()` — wakes the idle
   in-flight read; serialized with that read's completion (fixes the BIO race). INV-4a.
3. **(control strand)** drive the outstanding-loop **join** + the send-drain.
4. **(session strand, after join + send-drain, before clear)** dispatch terminal
   `Session::close()` — drains the parked `run_liveness_loop` `sleep_until` (the
   documented prior heap-UAF, engine.cpp:741-752). INV-4b.
5. **(control strand)** `outstanding_counter_.reset()` + clear `accept_scope_signals_` /
   `listeners_` / `listener_endpoints_` / `registry_`.

- **Hop semantics (INV-5)**: every per-session dispatch is a non-blocking
  `co_spawn(session_strand, …, use_awaitable)` — `post`, never inline `dispatch` or a
  blocking wait on a strand.
- **Ordering invariant (INV-6)**: transport close **before** join; terminal `Session::close`
  **after** join + send-drain + **before** registry clear; all registry/listener mutation
  on the control strand. Close-before-clear preserved (no Session/transport freed while its
  read-pump is in flight).
- **Registry iterator-stability (INV-6a)**: the control strand iterates a **stable**
  `registry_` across the per-entry `Session::close()` `co_await`s — `stopped_` is already true
  (no new entries) and no other control-plane mutation (insert/erase) interleaves between
  `stopped_=true` (step 1) and the `registry_.clear()` (step 5). Each per-entry close dispatch
  is awaited-but-not-detached, and any pending D-PUB publish/unpublish job is part of the join
  (step 3), so step 5's clear strictly follows all closes and all publication jobs. (Gate A
  round-2 NEW-1.)

## E-5 — Per-session transport executor (mandatory + asserted)

- **What**: the accepted/connected transport's I/O object is associated with the E-1
  strand (D5).
- **Invariant (INV-7, transport FR-007 re-affirmed)**: `transport.socket().get_executor()
  == session_strand`. **Auto-satisfied** when the role loop runs on the strand (the socket
  inherits the loop's `co_await this_coro::executor`); the obligation is that **no**
  construction site samples the engine's bare `exec_` member, **asserted** in debug.
- **Audit set**: engine listener-build (engine.cpp:459), `reconnect_fsm` make (~225), the
  two `asio_tls_transport` ctors (~542/560/579).

## E-6 — `stopped_` access discipline (D-STOP)

- **What**: `stopped_` becomes `std::atomic<bool>` with acquire/release (was a plain bool
  "safe under single-executor confinement").
- **Invariant (INV-8)**: authoritative **write** on the control strand (in `stop()`);
  **reads** (accept-loop gate engine.cpp:478, send fast-fail engine.cpp:822) are atomic
  acquire — never a non-atomic cross-thread read.

## E-7 — Public-reader immutable snapshot (NEW; D-SNAP)

- **What**: an **atomically-published immutable snapshot** of the control-plane state the
  synchronous public readers need — the registry's `SessionId → shared_ptr<Session>` map and
  the bound-endpoint table. Held in a **`std::atomic<std::shared_ptr<const Snapshot>>`** (the
  standard C++20 primitive — header-only, **no `std::mutex` in our headers**, Art. XV-consistent),
  published via `atomic_store` / read via `atomic_load` (the RCU idiom). The read is **wait-free
  on STLs where `std::atomic<shared_ptr>` is lock-free**, otherwise standard-library-internal
  synchronization that is NOT a `std::mutex` in our code — the absolute "lock-free" claim is
  dropped; the read cost is measured by the V-6 perf gate. This deliberately does **NOT** use the
  project's unshipped `fixpp::sync::atomic_shared_ptr` (NFR-017, backlog) — that is only a possible
  later optimization (research D-SNAP).
- **Owner**: the `Engine` (a new member; e.g. `reader_snapshot_`). Republished by the control
  strand after **every** control-plane mutation (registry insert/erase, listener/endpoint
  mutation, D-PUB publish/unpublish).
- **Readers**: `lookup()` `atomic_load`s the snapshot and returns a
  **`std::shared_ptr<Session>`** drawn from it (the accepted FR-008 signature change — the
  handle outlives a concurrent `registry_.clear()` **while the `Engine` is alive**);
  `acceptor_bound_endpoint()` `atomic_load`s the snapshot and returns its `Endpoint`
  **by value** (no signature change). Neither enters a strand, takes a lock, or blocks.
- **Bounded handle (INV-9a / FR-008/FR-014)**: the `lookup()`/snapshot handle is a
  **bounded handle** — valid across a concurrent `stop()` / `registry_.clear()` **only
  while the `Engine` is alive**. `Session` borrows `const EngineConfig& engine_`
  (session.hpp:486) from `Engine::engine_cfg_`, so dereferencing a handle after `~Engine`
  is a UAF (the same hazard the send path documents at engine.cpp:726-730). This is a
  documented hard precondition (NOT a refactor of `Session`'s dependency model): the
  caller MUST NOT let a handle outlive the `Engine`, **debug-asserted at `~Engine`** that
  no outstanding `lookup()`/snapshot handle remains — via a debug-only **lease control
  block** (a `shared_ptr` control block cannot hook every copy, only its deleter at
  last-owner destruction): the aliasing `shared_ptr<Session>` owns a lease whose ctor
  increments an engine-owned `std::atomic<uint64_t>` and whose dtor (last copy destroyed)
  decrements it, asserted zero at `~Engine`. (A bare snapshot `use_count()` cannot see a
  handle copied out then detached from the snapshot.) Release builds carry
  no counter. The keepalive is NOT a general keepalive past `~Engine`.
- **Invariant (INV-9 / FR-014)**: the synchronous public readers NEVER read a control-plane
  structure the control strand is mutating in place — only the published immutable snapshot.
  Mutation is control-strand-confined (FR-011); the snapshot decouples reads from it.
- **Note**: `stopped()` is NOT in this snapshot — it reads the E-6 `atomic<bool>` directly.

## Lifetime / ordering summary (two domains)

```
start():  (on control strand)  for each registered session →
            session_strand = make_strand(exec_)              [E-1]
            bind Session to session_strand (D3-B)            [E-3 / INV-3]
            co_spawn(session_strand, role_loop)              [E-1/D2]
              └─ accept/connect + handshake + transport         on session_strand          [E-5/INV-7]
              └─ co_await publish entry.session/live_transport VIA control strand          [E-2/INV-2]  (awaited, BEFORE read-pump)
              └─ read-pump                                       on session_strand          [E-1/D2]
              └─ listeners_/listener_endpoints_ writes         on control strand          [E-0/INV-0]
              (each control-strand mutation republishes the E-7 reader snapshot)          [E-7/INV-9]

lookup()/acceptor_bound_endpoint():  atomic_load(reader_snapshot_) → handle/value   [E-7]  (any thread, no std::mutex, no strand; std::atomic<shared_ptr<const Snapshot>>)

send():   caller → control_strand (registry/stopped read, counter bump)   [E-0]
                 → session_strand (toApp + Session::send)                  [E-1]   (both non-blocking posts)

stop():   (control strand)  stopped_=true; snapshot + total-cancel               [E-0/E-6]
          (session strand)  dispatch transport.close()   ── before join          [E-4 step 2 / INV-4a]
          (control strand)  join outstanding loops; drain send_counter           [E-4 step 3]
          (session strand)  dispatch Session::close(terminal)                     [E-4 step 4 / INV-4b]
          (control strand)  counter reset + clear listeners_/endpoints_/registry_ [E-4 step 5 / INV-0]
```

No new session-FSM state; this is a runtime serialization-domain + ordering change only.
