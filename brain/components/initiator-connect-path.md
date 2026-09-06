---
type: Flow Decision Map
title: Initiator connect path — lazy connect and the strand lynchpin
description: run_connect_loop's invariants. The acceptor's mirror; both converge on one handshake_result.
status: stable
refs:
  - src/session/engine.cpp
  - include/fixpp/session/reconnect_fsm.hpp
codegraph_entry: [run_connect_loop, drive_reconnect, drive_reconnect_attempt, ReconnectFsm]
constitution: ["§XI.2"]
---

# Initiator connect path

> ## ⚠️ The CODE is authoritative. This page is not.
>
> SecondBrain is a **consultant**, not a source of truth. It points you at the right files and explains
> **why** a decision was taken and what was **rejected** — that half is historical and does not change
> retroactively. It does **not** establish what the code does today.
>
> **Anything here describing current behaviour is a LEAD TO CHECK, not a fact to cite.** Verify against
> source before you rely on it, and cite the source, not this page.
>
> This page exists because signed-off design documents rotted. **It has no immunity from that** — a page
> trusted instead of read becomes the next fossil, and it would be a worse one, because it is the page
> people come to for the fossil list.

## Invariants, and where each is enforced

| Invariant | Why | Enforced at |
|---|---|---|
| **`open()` emits NO Logon on the engine-managed initiator arm — the Logon is emitted POST-connect** | *lazy connect*: a Logon written before the transport exists has nowhere to go, and at-open emission would couple session construction to network availability | `run_connect_loop` steps 2–3 (T016(d)); the open-arm is a deliberate no-op |
| **On attempt exhaustion, cancellation, or Logon-emit failure the session is left Disconnected, closed terminally, and unwound** | one terminal state for every failure mode, so callers need no failure taxonomy; the close is idempotent because `stop()` may have run concurrently | `run_connect_loop` unwind path [E-1a; FR-003] |
| **The connect-path transport socket is bound to the session strand — and this is AUTO-satisfied, not asserted into truth** | the property follows structurally: the loop runs on the session strand, so `this_coro::executor` inside `drive_reconnect_attempt` *is* that strand, and the factory stores it as the socket's executor | ⭐ **the assert exists to catch a REGRESSION**: it fires if `reconnect_fsm.cpp` reverts to a bare `exec_`. The code calls this the **R8 lynchpin** (T011/INV-7, D5/E-5) |

> ⭐ **That last row is the shape worth copying.** The assert is not a belt-and-braces check on
> something already true — it is a **tripwire on a specific known regression**, and the comment names
> the file that would cause it. Compare a bare `assert(x)` that says nothing about what would break it.

## Relationship to the acceptor and to reconnect

`run_connect_loop` delegates to `drive_reconnect` → `drive_reconnect_attempt`, **the same path
reconnect uses** — which is why connect and reconnect cannot drift, and why both the acceptor and
initiator arms converge on a single `handshake_result`. The acceptor mirror is
[`engine-accept-path`](./engine-accept-path.md); the retry/backoff invariants are in
[`session-liveness-and-reconnect`](./session-liveness-and-reconnect.md).

⚠️ **`connect_timeout` bounds the attempt AS A WHOLE, resolution included (#361) — and the
mechanism is not cancellation.** asio's resolve op takes no cancellation slot, so the lookup is
ABANDONED at a shared deadline rather than aborted, which also means the io_context drain is NOT
bounded by it. The decision, the falsified alternatives, and the head-of-line regression this
introduced and then capped are on [`transport`](./transport.md); the residual is `L-361-2`. Do not
re-derive either here.
