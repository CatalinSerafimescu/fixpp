---
type: Flow Decision Map
title: Liveness, heartbeat and reconnect — the timeout family
description: Three coroutines that decide a peer is gone. Their invariants and enforcement sites; the constants are deliberately not reproduced.
status: stable
refs:
  - include/fixpp/session/reconnect_fsm.hpp
  - include/fixpp/session/session.hpp
  - spec/behaviors-and-limitations.md
codegraph_entry: [run_liveness_loop, drive_reconnect_attempt, drive_reconnect, ReconnectFsm]
constitution: ["§XI.2"]
---

# Liveness, heartbeat and reconnect

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

## Participants

`run_liveness_loop`, `drive_reconnect`, `drive_reconnect_attempt`, `run_logout_phase1` — derive the
current list with `python3 tools/brain_inventory.py --census`, never from a list written here.

> ## ⛔ CORRECTION 2026-08-31 — three names this page used to list are DEAD STUBS
>
> `run_heartbeat_cadence`, `run_inbound_liveness_watch` and `drive_logout` in
> `src/session/reconnect_fsm.cpp` have **one-line bodies** (`co_return expected_t<void>{};`). Their own
> comments say the logic is *"deferred to Phase 4"* and *"lives in session.cpp run_liveness_loop() for
> Phase 3."* **The live implementation is `Session::run_liveness_loop()`.**
>
> This page previously cited two of those stubs as the enforcement site for its two headline
> invariants. **It pointed readers at empty functions** — the exact fossil class this bundle exists to
> prevent, committed by the bundle. Found by a blind agent during the A4 measurement, not by review.
>
> ⭐ **Root cause, and it is the transferable part:** `brain_inventory.py` derives "long-lived
> coroutines" from a **signature**, and a stub has a signature. A declaration-shaped instrument cannot
> tell a flow from a placeholder. **Re-derive before trusting any name here** — measure the body, do
> not match the signature:
>
> ```bash
> python3 tools/brain_inventory.py --census    # now flags stubs; see the STUB marker
> ```

## Invariants, and where each is enforced

| Invariant | Why | Enforced at |
|---|---|---|
| **Liveness escalates in two stages — probe, then terminate** | a silent peer and a dead peer are indistinguishable from one missed interval; the probe disambiguates before the session is torn down | **`Session::run_liveness_loop()`** in `src/session/session.cpp` — ⚠️ **not** `run_inbound_liveness_watch`, which is a stub (see the correction above). FR-004 / FR-007. **The two thresholds are NOT reproduced here** — read the source. A copied constant is what rots |
| **The outbound timer is armed on Active entry, rearmed on every outbound, cancelled on Disconnected entry** | the cadence is *"nothing sent for HeartBtInt"*, not *"every HeartBtInt"* — so any outbound resets it and an idle session sends the minimum | **`Session::run_liveness_loop()`** — ⚠️ **not** `run_heartbeat_cadence`, which is a stub |
| **An inbound Heartbeat's `TestReqID(112)` must match the most recent outbound TestRequest** | an unmatched reply proves liveness of *something*, not of the exchange being probed | FR-006 → `session_testreqid_mismatch` |
| **Reconnect mints a FRESH `Transport` per attempt; the dead instance is destroyed first** | a `Transport` is never reused across attempts | `drive_reconnect_attempt`; disclosed as **`B-012-2`** |
| **All elapsed/threshold measurements use `steady_now()`, not `now()`** | `now()` is not promised monotonic, so a wall-clock step would fire or suppress timeouts spuriously | disclosed as **`B-007-3`** |

## Surprising shipped behaviour — read the B&L rows, not this page

- **`B-005-6`** — `HeartBtInt=0` disables heartbeating **entirely**: no timers run at all. An
  operator expecting a default cadence gets none.
- **`L-009-2`** — TestRequest ID uniqueness is **per-session-lifetime only** and wraps at
  `UINT32_MAX`; not unique across sessions or restarts.

⚠️ **A limitation is open only if it is in the LIVE B&L file.** Resolved rows move to
`spec/behaviors-and-limitations-closed.md`, so a repo-wide grep reports closed ones as open.

## Where the reconnect *decisions* live

`drive_reconnect_attempt` is also the initiator's connect+handshake path, reused by
`run_connect_loop` — which is why the acceptor and initiator paths converge on one
`handshake_result`. See [`engine-accept-path`](./engine-accept-path.md) for the acceptor half.
