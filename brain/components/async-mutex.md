---
type: Component Decision Map
title: async_mutex — coroutine-aware lock, cancellation and drain
description: Every document describing fixpp's async mutex, with the pre-048 design flagged as historical.
status: stable
refs:
  - include/fixpp/core/sync/async_mutex.hpp
  - include/fixpp/session/memory_store.hpp
  - src/session/seqnum_manager.cpp
  - .specify/2f-async-mutex.md
  - .specify/544-hot-path-zero-alloc.md
refs_external:
  - research/G19-fix-fpml-iso20022/phases/phase-4/core/048-async-mutex-strand-reap.md
  - research/G19-fix-fpml-iso20022/phases/phase-4/core/058-async-mutex-hardening.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/048-async-mutex-strand-reap-gateb.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/006-async-mutex-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/2f-async-mutex.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/544-hot-path-zero-alloc-tasks.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/544-hot-path-zero-alloc-evidence.md
codegraph_entry: [async_mutex, async_lock_guard, cancel_and_drain, lock_frame, async_lock_op, finish_lock]
constitution: ["§XV.9"]
---

# `async_mutex`

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
| ⚠️ `decisions/2f-async-mutex.md` — **the 9.6 KB decision record in the PRIVATE parent**, *not* the 309 KB design doc of the same basename at `.specify/2f-async-mutex.md`. Two different artifacts, one name. Check the size or the path before concluding anything | **HISTORICAL.** Describes the pre-048 cross-thread drain: a `shared_ptr`-owned `drain_latch_state` over an `asio::experimental::concurrent_channel`, a Dekker-style publication handshake, `active_acquirers_count_` epoch counters |
| `specs/006-async-mutex/` — `spec.md`, `research.md`, `data-model.md`, `contracts/` | **HISTORICAL** for drain/cancellation. Frozen at the original feature; never updated after 048. ⚠️ Also stale, since fixpp#544, where its contracts place the frame-local awaiter in `async_lock`'s own frame: for callers of `FIXPP_DETAIL_CO_AWAIT_LOCK` it lives in a `detail::lock_frame` in the caller's frame (2f Erratum E-6, at the end of `.specify/2f-async-mutex.md`) |

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
| **fixpp#544** (B35) | **Erratum E-6, the frameless lock op.** For the internal callers that lock through `FIXPP_DETAIL_CO_AWAIT_LOCK` (`SeqnumManager::check_inbound`, `SeqnumManager::hydrate`, `MemoryStore::store`) the awaiter lives in a caller-owned `detail::lock_frame` in the **caller's** frame, and the lock is a `deferred` operation awaited directly: no coroutine frame of its own and no `use_awaitable` adapter, which is what E-1 intended. Public `async_lock` is unchanged, a thin coroutine over the same macro. The macro and `detail::lock_frame` / `async_lock_op` / `finish_lock` are not supported API (B&L `B-544-5`). E-6 also corrects E-4's "per-thread" recycler to the **scheduler-call** scope, disposition unchanged, and records that the recycler's slot count is fixpp's exported value (B&L `L-544-1`). **Rejected:** an inline function plus an RAII restore of the cancellation filter (`reset_cancellation_state` exists only as a promise `await_transform`, so a destructor cannot restore it). ⚠️ **A design premise that failed (F-2):** "removing a frame is only headroom" is false at asio's default slot count, where the first-fit, evict-first-occupied recycler can settle into an allocating cycle on the smaller pattern; an existing zero gate on the public `async_lock` (`sync_alloc_guard_test_mallocnesia`) did so until the exported count landed. Record: `.specify/544-hot-path-zero-alloc.md` §2.3 and its "As built" section |

## Consumers, and the teardown contract

`Session::write_gate_` (the one that actually exercises contention and cancellation),
`SeqnumManager::mutex_`, `MemoryStore::mutex_`, `FileStoreImpl::mutex_`.
*(fixpp#544:)* `SeqnumManager::check_inbound`/`hydrate` and `MemoryStore::store` reach their mutex through
`FIXPP_DETAIL_CO_AWAIT_LOCK`; every other call site, `write_gate_` and `assign_outbound` included, uses the
public `async_lock`. Re-derive with `git grep -n "FIXPP_DETAIL_CO_AWAIT_LOCK" -- src include`.

The stores are **never explicitly drained**, and that is correct — see
[`message-store-quiescence.md`](./message-store-quiescence.md). The contract is discharged
structurally, not by a call.

## ✅ `.specify/2f-async-mutex.md` is the POSITIVE CONTROL for post-sign-off amendment

Verified 2026-08-29 during the Step-R sweep, and worth stating because every other entry on this page
is a failure: **this document did the thing correctly.**

- It carries **in-document errata `E-1` … `E-6`**, each dated, each naming the feature and the
  authority that approved it — appended rather than rewritten, so the original design and its
  corrections both survive. *(E-6, fixpp#544, is at the END of the file rather than beside E-5, so
  that no line-number citation into the file moves; the status line says so.)*
- Its Status header names what shipped it (`006-async-mutex`), instead of leaving a bare *"Draft"*.
- The shipped header **points back**: `include/fixpp/core/sync/async_mutex.hpp` says
  *"Design anchor: `.specify/2f-async-mutex.md` v1.6 (errata E-1..E-6)"* — so the code and the doc
  name each other, and neither can drift without the other becoming visibly wrong.

That closed loop is why a blind agent found the 048 supersession here unaided, and did not find the
engine accept-path one. **This is the shape to copy** — not a rule anyone wrote down, just a document
whose author kept the pointer current.

