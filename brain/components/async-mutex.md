---
type: Component Decision Map
title: async_mutex — coroutine-aware lock, cancellation and drain
description: Every document describing fixpp's async mutex, with the pre-048 design flagged as historical.
status: stable
refs:
  - include/fixpp/core/sync/async_mutex.hpp
refs_external:
  - research/G19-fix-fpml-iso20022/phases/phase-4/core/048-async-mutex-strand-reap.md
  - research/G19-fix-fpml-iso20022/phases/phase-4/core/058-async-mutex-hardening.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/048-async-mutex-strand-reap-gateb.md
codegraph_entry: [async_mutex, async_lock_guard, cancel_and_drain]
constitution: ["§XV.9"]
---

# `async_mutex`

## Current state

`fixpp::sync::async_mutex` (`include/fixpp/core/sync/async_mutex.hpp`) is the only coroutine-aware
mutual-exclusion primitive — an awaitable lock that never blocks an OS thread. It exists because
`std::mutex` is banned in any header transitively included by an `asio::awaitable` coroutine
(`[const §XV.9]`), enforced by a CI grep gate.

**Cancellation is a single-winner CAS race** on a per-waiter `phase_` field: `queued→cancelled` from
the cancel handler, `queued→granted` from unlock's/drain's walk. Whichever side wins is the outcome;
the loser is a no-op. **A cancellation arriving after a grant never revokes it** — pinned by test.

`cancel_and_drain()` reaps every still-`queued` waiter, sets a flag that fast-fails new acquires, is
**uninterruptible** (`disable_cancellation`), and is **strand-local by contract** — overlap from
another thread or strand is explicitly UNDEFINED (`L-048-2`).

The destructor calls `std::terminate()` if the mutex still has a holder, queued waiter or in-flight
resumer (`B-006-2`) — a deliberate hard precondition, not an error return.

## ⚠️ Documents describing a design that no longer exists

| Document | Status |
|---|---|
| `decisions/2f-async-mutex.md` (the Phase-2 design doc) | **HISTORICAL.** Describes the pre-048 cross-thread drain: a `shared_ptr`-owned `drain_latch_state` over an `asio::experimental::concurrent_channel`, a Dekker-style publication handshake, `active_acquirers_count_` epoch counters |
| `specs/006-async-mutex/` — `spec.md`, `research.md`, `data-model.md`, `contracts/` | **HISTORICAL** for drain/cancellation. Frozen at the original feature; never updated after 048 |

Feature **048** removed that machinery entirely. Reading 006-only docs gives a **materially wrong
mental model of `cancel_and_drain()` today**. They remain valuable for *why the original shape was
chosen* and for the alternatives rejected then — see below.

Note also: `specs/006-async-mutex/contracts/*.hpp` are a **shape oracle, not build headers** (they say
so). Everything ships consolidated in the single header.

## Why this component did NOT fail the blind-agent test

`async_mutex.hpp` names its own supersession in a header comment — *"Erratum E-5 (048 — strand-local
reap): `cancel_and_drain()` is narrowed to the strand-serialised contract its two consumers actually
use…"*. A blind agent followed that pointer to 048, then read the 006 docs, and **caught the
contradiction unaided**. The [engine accept path](./engine-accept-path.md) has no such pointer and an
equally careful agent reported "no disagreements found" over a repo with two.

**This is the convention worth copying.** It is a pointer, not a result, so it does not rot like a
line-number citation (#310).

## Lineage

| Feature / PR | What changed |
|---|---|
| **006** (PR #73) | First shipped version, per `2f-async-mutex.md` (6 review rounds). **Rejected then:** a `steady_timer`-backed latch (needs an executor; the mutex must stay `constexpr`-default-constructible); a 4-state waiter phase (collapsed to 3); a public `try_lock()` (removed to `detail::` — admitted a same-mutex-aliasing bug) |
| **048** (PR #144) | **The pivotal cancellation/drain redesign.** Removed the cross-thread convergence machinery; replaced with a strand-local quiescence loop. A deliberate **narrowing**: cross-thread drain overlap went from handled to explicitly undefined, because production consumers only ever drain strand-locally |
| **058** (PR #162) | Hardening — ABA on the free list, in-flight-resumer happens-before, destructor-guard widening, bounded-CAS exhaustion, chain-walk CAS loss, acquire livelock. **Did not change the cancellation model** |

## Consumers, and the teardown contract

`Session::write_gate_` (the one that actually exercises contention and cancellation),
`SeqnumManager::mutex_`, `MemoryStore::mutex_`, `FileStoreImpl::mutex_`.

The stores are **never explicitly drained**, and that is correct — see
[`message-store-quiescence.md`](./message-store-quiescence.md). The contract is discharged
structurally, not by a call.
