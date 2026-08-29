---
type: Flow Decision Map
title: Inbound message path — socket bytes to fromApp
description: The read pump's invariants and where each is enforced. Written because no design doc owns a runtime flow; only 2 of ~30 do.
status: stable
refs:
  - src/session/engine.cpp
  - include/fixpp/session/session.hpp
  - specs/015-runtime-engine/research.md
codegraph_entry: [run_read_pump, Framer, on_inbound_frame, Session]
constitution: ["§VIII.5", "§XI.2"]
---

# Inbound message path

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

## Why this page exists

**No design doc owns a runtime flow.** `arch §5` is 8 subsections of cross-cutting *policy*, and the
`2*` docs each own a subsystem while explicitly disclaiming the spine. Only **2 of ~30** design and
spec documents contain anything flow-shaped. The flow knowledge is in **code comments and Phase-4 spec
bundles** — so this page routes there and records the **invariants**, not the steps.

⛔ **There is deliberately no step-by-step narrative here.** That is the code, it rots on the next
edit, and it would read as authoritative while doing so.

## Participants

`Transport::async_read_some` → `wire::Framer::feed` → `Session::on_inbound_frame` → the FSM →
`fromApp`. Query the graph index for structure; it is always current and this page is not.

## Invariants, and where each is enforced

| Invariant | Why it exists | Enforced at |
|---|---|---|
| **No inbound queue. One frame is processed at a time** | backpressure is *structural*, not a policy — the pump does not read the next chunk until the session has consumed the current frame, so there is nothing to bound and nothing to drop | the pump `co_await`s `on_inbound_frame` before the next read (`run_read_pump`, `src/session/engine.cpp`) |
| **The carry buffer is allocated once per session and never reallocated**; overflow is an error, not a growth | `[const §VIII.5]` zero-allocation between parse and `fromApp` | carry buffer construction in `run_read_pump`; overflow → `wire_frame_too_large` |
| **Surplus bytes from the bounded first-frame read drain through the SAME framing path BEFORE the first socket read** | ⚠️ **this is a bug class, not a detail.** A peer may coalesce `Logon`‖next-frame in one segment; reading the socket first would silently drop the second frame | the `initial_bytes` drain block preceding the read loop (F-015-002) |
| **Error or EOF closes the session terminally, and the close is idempotent** | `stop()` may already have closed it; a second close must be a no-op, not a fault | the pump's stop helper → `Session::close(terminal)`; `session_already_closed` is deliberately ignored |
| **Total cancellation must be re-enabled explicitly** | `co_spawn` defaults to **terminal-only**, so `stop()`'s total-cancel is otherwise swallowed **silently** | `reset_cancellation_state(enable_total_cancellation())` as the coroutine's first step `[const §XI.2]` |

## What was rejected

From `specs/015-runtime-engine/research.md` R3 — the half that does not rot:

- **One shared read-pump multiplexing all sessions** — rejected: breaks per-session strand isolation
  and serialises unrelated sessions.
- **Callback-style `async_read_some` with completion handlers** — rejected: the tree is
  coroutine-native `[const §XI.1]`.

⚠️ That same R3 `Decision` is **superseded in part** — it places the pump on the engine executor and
as a separately `co_spawn`ed coroutine; neither is true. See
[`engine-accept-path`](./engine-accept-path.md).
