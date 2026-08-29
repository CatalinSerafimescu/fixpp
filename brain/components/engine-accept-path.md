---
type: Component Decision Map
title: Engine accept path — listener, accept loop, Session handoff
description: Every document that claims to describe how the Engine hands an accepted connection to a Session, with the superseded ones flagged.
status: stable
refs:
  - src/session/engine.cpp
  - src/transport/asio_listener.hpp
  - include/fixpp/transport/listener.hpp
refs_external:
  - research/G19-fix-fpml-iso20022/decisions/speckit/pr331-330-asio-listener-executor-gateb.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/pr326-310-315-gateb.md
codegraph_entry: [Engine, run_accept_loop, asio_listener, assert_transport_on_session_strand]
constitution: ["§XI"]
---

# Engine accept path

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


## Current state — a LEAD, verify against source before citing

`Engine::start()` `co_spawn`s `run_accept_loop` **on `*entry.session_strand`** — a **per-session**
strand, one per acceptor `SessionEntry`. The loop reads `co_await this_coro::executor` at the top,
builds the `asio_listener` with **that** executor, and asserts every accepted transport is on the same
strand (**INV-7**, `assert_transport_on_session_strand`).

The structural fact underneath: `asio_listener::async_accept()` is an ordinary coroutine that never
dispatches onto its own stored executor, so **it resumes on whatever executor its awaiter runs on**.
There is no listener-owned executor contract at all — the executor comes entirely from the `co_spawn`
target.

Also true, and easy to assume otherwise: **each acceptor session's loop serves exactly one peer and
then returns.** There is no re-spin to `async_accept` after a successful handoff.

## ⚠️ Documents that describe this component and are WRONG

Listed because omitting them is the defect this page exists to prevent. **Do not fix code to match
them.** Tracked by **issue #334**; the verdict there is that the docs are what should move.

| Document | Claim | Status |
|---|---|---|
| `.specify/2j-controlplane.md` §6.5 | *"The engine executor is shared with the engine's listener accept and engine-bootstrap coroutines … it is not shared with session strands"* — stated as a **threading invariant**, in a **signed-off** design doc, which is what makes it re-seed the model for new work | ✅ **AMENDED 2026-08-29 (v0.3 → v0.4)** — sentence **deleted**, not refreshed; replaced by a re-derivation recipe. ⚠️ Its §6.3 cross-strand handoff budget (`≤ 100 µs`/`≤ 5 ms`) is now flagged **UNVERIFIED** — derived under the falsified premise, never re-measured |
| `.specify/2d-threading.md` | The **upstream** of the 2j claim. Uses *"(listener accept, control-plane handlers, engine bootstrap)"* at ~10 sites as a proxy for "outside any session serialisation domain" | ✅ **AMENDED 2026-08-29 (v0.4 → v0.5)** — **one document-level note**, not 10 site edits. Behavioural conclusion survives (the loop runs on a *bare* strand, not a `session_executor` wrapper, so the trace-context fallback still fires); the stated *reason* does not. `clock_scope = engine` claims not re-verified |
| `.specify/constitution.md` `[const §XIII.3]` | Same parenthetical — *"(e.g., listener accept, control-plane handlers)"* | ⚠️ **NOT amended, deliberately.** Illustrative inside a `thread_local` prohibition whose rule is unaffected; amending it needs its own Gate A pass. **Escalated to the user** |
| `specs/015-runtime-engine/research.md` | *"Per registered acceptor session, `co_spawn` an accept loop on the engine executor that **repeatedly** `co_await listener.async_accept()`"* — recorded as a **Decision** | **SUPERSEDED / FALSE on two axes**: the executor *and* the "repeatedly" loop shape |
| `specs/012-2h-transport/spec.md` | user-story narrative describing **one** `async_accept` loop on a service-strand executor for all counterparties | **STALE** — the engine ships one listener per session. Rewriting it is a spec change, not a citation fix; decide deliberately |

⚠️ **Re-derive these before quoting them** — they are claims about a moving tree, and #334 says so in
its own words.

## Documents that are current

| Document | Carries |
|---|---|
| `specs/023-engine-session-strand/research.md` | The governing decision set: **D0** control strand (rejected: a global mutex — banned by `[const §XI]`); **D1** one strand per session, created once; **D2** the entire role on that one strand (verified against asio's own `ssl/detail/io.hpp` — SSL BIO processing dispatches on whatever executor runs the coroutine); **D3-B** adopt the pre-made strand (**D3-A rejected as verified broken** — collides with a user's legitimate `lock_policy::spin`); **D4** teardown ordering; **D5/INV-7** accepted socket on the session strand; **D-PUB** publish `co_await`ed on the control strand *before* the read pump (**rejected**: an atomic `live_transport` pointer — makes the read well-defined but not the ordering); **D6/D-SNAP** atomic immutable snapshot for public synchronous readers |
| `src/transport/asio_listener.hpp` header comment | The corrected statement of the no-executor-contract fact, after PR #331 deleted the "service strand" claim from ~10 files |
| `pr331-330-asio-listener-executor-gateb.md` (private) | How that deletion converged — 4 rounds, every round replacing a false claim with a *new* false one until deletion closed it |

## Why it is this way

Two race classes on a multi-threaded `io_context`: an in-flight SSL read racing teardown `close()` (a
real observed `BIO_ctrl` use-after-free), and engine-global maps read/written from uncoordinated
frames. Single-strand placement is what serializes the first; the control strand serializes the second.

## What breaks if the accept loop's executor changes

INV-7's debug assert fires; D2's SSL-BIO serialization is lost, reopening the `BIO_ctrl` UAF on the
acceptor path; D4's teardown dispatch no longer lands on the executor the loop actually runs on; and
`asio_listener.hpp`'s corrected header comment becomes false again — the exact class of stale claim
that cost PR #331 four Gate B rounds.
