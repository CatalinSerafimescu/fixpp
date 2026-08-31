---
type: Component Decision Map
title: Test infrastructure — the seams are designed surfaces, not test-only afterthoughts
description: A pluggable Clock, a public mock transport, and an LD_PRELOAD allocation interceptor. The seams exist because the constitution required them, not because tests needed them later.
status: stable
refs:
  - include/fixpp/transport/test/mock_transport.hpp
  - .specify/constitution.md
  - tools/check_alloc.py
codegraph_entry: [mock_transport, Clock, system_clock_source]
constitution: ["§VII", "§VII.4", "§VIII.5"]
---

# Test infrastructure

> ## ⚠️ The CODE is authoritative. This page is not.
>
> It exists because `test` is a catalogue family with **no owning design doc**, and because the
> testing seams here are *architectural decisions* that a reader will otherwise meet only as
> unexplained interfaces.

## The seams are the design, not scaffolding

`[const §VII]` requires that everything touching the outside world be pluggable so the FSM and parser
can be tested without real I/O. That requirement is why these exist at all — they are not conveniences
added afterwards:

| Seam | Replaces | Where |
|---|---|---|
| `fixpp::core::Clock` | wall-clock time | heartbeat, `SendingTime`, reconnect schedules — a mock steps time deterministically instead of sleeping |
| `mock_transport` | a socket | drives the session FSM through a pre-recorded byte stream |
| `MessageStoreFactory` / `TransportFactory` | disk, network | see [`plugin-factory-ownership.md`](./plugin-factory-ownership.md) |

⭐ **Consequence worth holding on to:** if a new subsystem cannot be tested without real I/O, the
missing piece is a **seam in the design**, not a cleverer test. That is the same reasoning that put a
`Clock` interface in a FIX engine.

## ⭐ `mock_transport` is a PUBLIC header that refuses to compile in production

It lives under `include/`, not `tests/` — a deliberate choice, so external consumers can drive the FSM
in *their* tests. The safety comes from a build-system token: production targets do not define it, and
the header `#error`s without it.

> **The pattern is worth copying: gate by a token the build controls, not by directory placement.**
> "It's in `tests/` so it can't ship" is a convention; an `#error` is checked by the compiler. Same
> family as the `static_assert` idiom on [`quickfix-compat.md`](./quickfix-compat.md).

## Allocation discipline is enforced by an interceptor, not by review

`[const §VIII.5]` demands zero allocation between parse and `fromApp`. That is checked by
**`LD_PRELOAD`-ing a malloc interceptor** around dedicated guard binaries and failing if any
`malloc`/`free` is seen between markers.

⚠️ **Two things to know before trusting a green run.** The instrument is Linux-only by construction —
a passing Windows build proves nothing about allocation. And a guard test only covers the window its
markers enclose: *"zero allocations"* means *zero in that window*, never *anywhere*.

**Re-derive what is actually guarded** — the set changes, and a list here would rot:

```bash
ls tests/alloc_guard/ && sed -n '1,12p' tools/check_alloc.py
```

## ⚠️ The catalogue's `test` rows are not a coverage measure

Every `test` row reads `backlog`, and — as with `nfr` — **that is not evidence the work is absent**;
see [`nfr-and-tooling.md`](./nfr-and-tooling.md) for the condition and the derivation recipe. The test
tree is large and the CI tiers are real. **Do not read this family's status column as coverage.**

## Related

- [`nfr-and-tooling.md`](./nfr-and-tooling.md) — the status-column caveat, and where the CI gates live.
- [`transport.md`](./transport.md) — the interface `mock_transport` implements.
