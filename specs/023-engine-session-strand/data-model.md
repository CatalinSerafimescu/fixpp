# Data Model: Per-Session Strand Binding for Engine-Managed Sessions

**Feature**: 023-engine-session-strand
**Date**: 2026-06-05

This feature introduces no persisted data and no public types. The "entities" here
are engine-internal runtime bindings. They are documented so `/speckit-tasks` and
Gate A can reason about ownership and lifetime.

## E-1 — Per-session strand (new)

- **What**: One `asio::strand<asio::any_io_executor>` (or the project's
  `session_executor` wrapping it) created per registered session via
  `asio::make_strand(exec_)`.
- **Owner**: the engine, stored on `SessionEntry` (new field; see E-2). Created in
  `Engine::start()` **before** the session's role loop is spawned.
- **Lifetime**: from `start()` (session registration is pre-`start()` and
  single-threaded by contract) until the registry is cleared in `stop()`
  (after join-before-clear). The strand handle is copyable and cheap; the loop, the
  Session, the transport, and the teardown close all hold/use the same instance.
- **Invariant (INV-1)**: exactly one strand per session; the loop's executor, the
  `Session`'s bound executor, and the teardown `close()` dispatch target are all this
  same strand. Two operations for the session never run concurrently.
- **Relationships**: 1:1 with `SessionEntry`; wraps the engine's `exec_`.

## E-2 — `SessionEntry` (amended)

Existing struct (`include/fixpp/session/engine.hpp:119`). Amendment:

- **Add**: a per-session strand handle field (E-1). Naming TBD at implementation
  (e.g. `session_strand`), typed as the resolved serialized executor used to (a)
  `co_spawn` the role loop, (b) bind the `Session`, and (c) post the teardown
  `close()`.
- **Unchanged**: `session` (`shared_ptr<Session>`), `session_role`, `config`,
  `session_cancel`, `live_transport`. The `live_transport` close-to-wake contract is
  preserved; only the *executor it runs on* changes (E-4).

## E-3 — Session ↔ strand binding (amended seam)

- **What**: the mechanism by which the `Session` adopts the engine's per-session
  strand (research D3) instead of minting its own at `open()`.
- **Mechanism (one of, decided at Gate A)**:
  - `SessionConfig::executor_override = <engine strand>` with
    `threading_mode::direct_executor` + `already_serialized_executor = true`
    (attested-direct, reuses slot-48 enforcement), **or**
  - a `per_session_strand` binding extended to accept a pre-wrapped strand.
- **Invariant (INV-2)**: `session->executor().underlying()` equals the
  `SessionEntry` strand (E-1). `Engine::send`'s existing strand hop
  (`kl->executor().underlying()`, engine.cpp:860) then lands on the *same* domain as
  the read-pump and teardown — closing the steady-state race.

## E-4 — Teardown close dispatch (amended behavior)

- **What**: `Engine::stop()`'s per-session `live_transport->close()` (engine.cpp ~714)
  becomes a strand-dispatched, awaited close on the E-1 strand (research D4).
- **State transition (unchanged semantics)**: live → closed; the close still (a)
  cancels the socket to unblock an idle in-flight read and (b) tears down TLS. The
  only change is the **executor** it executes on (the session strand), making it
  mutually exclusive with that session's read completion.
- **Ordering invariant (INV-3)**: close-before-join is preserved — every per-session
  close is dispatched (and awaited) before the outstanding-counter join, so no
  Session/transport is freed while its read-pump is still in flight.

## E-5 — Per-session transport executor (amended construction)

- **What**: the accepted/connected transport's I/O objects are associated with the
  E-1 strand (research D5).
- **Invariant (INV-4, transport FR-007 re-affirmed)**: the transport's
  `read_in_flight_` / `write_in_flight_` strand-confined booleans and all its
  coroutines execute on the session strand — never on bare `exec_`. (Load-bearing
  guarantee is the loop-on-strand of E-1/D2; this construction binding is
  defense-in-depth and may be scoped per research D5.)

## Lifetime / ordering summary

```
start():  for each registered session →
            strand = make_strand(exec_)            [E-1]
            bind Session to strand                 [E-3 / INV-2]
            co_spawn(strand, role_loop)            [E-2/D2]
              └─ accept/connect + handshake + read-pump + transport  ALL on strand  [E-5/INV-4]

stop():   for each live session →
            co_await dispatch(strand, transport.close())   [E-4 / INV-3]  (serialized w/ read completion)
          join outstanding loops
          drain send_counter
          registry_.clear()                                (no Session* deref after free)
```

No data-model state machine beyond the existing session FSM is added; this is a
runtime-binding/ordering change only.
