# Data Model: Per-Session + Control-Plane Strand Binding

**Feature**: 023-engine-session-strand
**Date**: 2026-06-05 (rev 2 — two-domain model)

No persisted data, no public types. The entities are engine-internal runtime
serialization domains and bindings, documented so `/speckit-tasks` and Gate A can
reason about ownership, publication, and ordering. **Two** domains exist (E-0
control-plane, E-1 per-session); everything else hangs off them.

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
  `live_transport` (raw pointer) — the fields `stop()` reads — are **written on the control
  strand** (the role loop posts the publish to the control strand) or held as atomics with
  release/acquire. `stop()` reads them on the control strand. No bare-session-strand write
  that the control strand later reads.
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
- **Invariant (INV-3)**: `session->executor().underlying()` equals the `SessionEntry`
  strand (E-1). `Engine::send`'s session-strand hop (`kl->executor().underlying()`,
  engine.cpp:860) then lands on the same domain as the read-pump and both teardown closes.

## E-4 — Teardown ordering (amended — BOTH closes)

`Engine::stop()` per live session, with domains explicit:

1. **(control strand)** set `stopped_` true (atomic release), snapshot + total-cancel
   every `session_cancel` / `accept_scope_signals_`.
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

## Lifetime / ordering summary (two domains)

```
start():  (on control strand)  for each registered session →
            session_strand = make_strand(exec_)              [E-1]
            bind Session to session_strand (D3-B)            [E-3 / INV-3]
            co_spawn(session_strand, role_loop)              [E-1/D2]
              └─ accept/connect + handshake + read-pump + transport  on session_strand  [E-5/INV-7]
              └─ publish entry.session / entry.live_transport  VIA control strand        [E-2/INV-2]
              └─ listeners_/listener_endpoints_ writes         on control strand          [E-0/INV-0]

send():   caller → control_strand (registry/stopped read, counter bump)   [E-0]
                 → session_strand (toApp + Session::send)                  [E-1]   (both non-blocking posts)

stop():   (control strand)  stopped_=true; snapshot + total-cancel               [E-0/E-6]
          (session strand)  dispatch transport.close()   ── before join          [E-4 step 2 / INV-4a]
          (control strand)  join outstanding loops; drain send_counter           [E-4 step 3]
          (session strand)  dispatch Session::close(terminal)                     [E-4 step 4 / INV-4b]
          (control strand)  counter reset + clear listeners_/endpoints_/registry_ [E-4 step 5 / INV-0]
```

No new session-FSM state; this is a runtime serialization-domain + ordering change only.
