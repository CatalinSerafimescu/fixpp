---
type: Component Decision Map
title: session — the FIX engine, which no design doc owns
description: Second-largest catalogue family, ~20 feature bundles, nine cited design docs, and no 2* doc that owns it. This page is the routing layer that absence leaves missing.
status: stable
refs:
  - include/fixpp/session/session.hpp
  - include/fixpp/session/session_fsm.hpp
  - include/fixpp/session/seqnum_manager.hpp
  - specs/005-session-establishment-fsm/spec.md
  - spec/behaviors-and-limitations.md
refs_external:
  - research/G19-fix-fpml-iso20022/decisions/speckit/005-session-establishment-fsm-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/024-reset-refresh-on-logon-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/025-refresh-on-logon-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/027-next-expected-msgseqnum-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/029-persistent-seqnum-hydrate-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/033-fixt-fix50sp2-session-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/042-fixt-version-serviceability-guard-gatea.md
codegraph_entry: [Session, fsm_state, SeqnumManager, Engine, on_inbound_frame]
constitution: ["§XI.4", "§XV.4"]
---

# `session` — the engine nobody's design doc owns

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

## ⭐ Why this page exists, and why it is different from the others

Every other component page **supplements** a design doc. This one **substitutes** for the missing one.

`session` is the **second-largest catalogue family** and spans roughly twenty feature bundles — yet
**no `2*` design doc owns it.** `[arch §10]`'s hand-off table, the natural place to look, lists
`2a`–`2m` and **has no session row at all**. The family cites nine design docs
(`2a,2b,2c,2d,2e,2f,2g,2h,2j`) and **not one of them owns it**; each contributes a slice and
explicitly disclaims the spine.

> ⚠️ **This is exactly the hole that a hand-written component inventory would have hidden.** Deriving
> the inventory from `[arch §10]` looked obviously right and would have omitted the FIX engine — the
> heart of the product. Re-derive the current standing, never trust the shape described here:
>
> ```bash
> python3 tools/brain_inventory.py --census
> ```

The authority is therefore split three ways, and knowing the split is most of the value:
**the headers** (what it does), **the Phase-4 `specs/<id>/` bundles** (why, per feature), and
**`spec/behaviors-and-limitations.md`** (what will surprise you).

## Route by question

| You want | Go to |
|---|---|
| The state machine | `include/fixpp/session/session_fsm.hpp`. ⚠️ **The state set is not reproduced here** — `B-005-2` pins it to the `[FIX-SL §4.10]` set, and a copied enum is what rots |
| Establishment, Logon, the FSM's origin | `specs/005-session-establishment-fsm/` |
| Sequence numbers, persistence, hydration | `SeqnumManager`; `specs/029-persistent-seqnum-hydrate/` |
| PossDup / OrigSendingTime / PossResend | `specs/021-…`, `specs/022-…` |
| Reset & refresh on Logon | `specs/024-reset-refresh-on-logon/`, `specs/025-refresh-on-logon/` |
| NextExpectedMsgSeqNum | `specs/027-next-expected-msgseqnum/` |
| FIXT / FIX50SP2, version serviceability | `specs/033-fixt-fix50sp2-session/`, `specs/042-fixt-version-serviceability-guard/` |
| Runtime flows | the flow pages below — no design doc carries them |

## Invariants that span features, and why they exist

| Invariant | Why | Disclosed as |
|---|---|---|
| **Outbound sequence numbers are committed to the `MessageStore` BEFORE the frame hits transport** | ⭐ **the ordering is the durability guarantee.** Reverse it and a crash between write and commit yields a peer that saw a message the store never recorded — unrecoverable by resend | `B-005-5` |
| **`seqnum_t` overflow is session-fatal and requires operator intervention** | it **never silently wraps**; a wrap would silently corrupt the resend contract | `B-005-4` |
| **Acceptor sessions stay in `NotConnected` at `open()` and emit no Logon; only initiators do** | the asymmetry is deliberate — an acceptor has no peer to greet yet | `B-009-1`; pairs with the lazy-connect invariant in [`initiator-connect-path`](./initiator-connect-path.md) |
| **A refused first Logon transitions to `Disconnected`, not back to `NotConnected`** | the two states are not interchangeable: `Disconnected` records that an attempt happened and failed | `B-009-2` |
| **The live inbound path accepts out-of-order header/body fields, including `MsgType` not first** | real counterparties emit them; strictness here buys conformance-theatre and loses interop | `B-005-7` |

## What was rejected — the half the code cannot tell you

- **A `RecoveryPending` half-state.** `B-005-2` states there is none: the FSM is *exactly* the
  `[FIX-SL §4.10]` set. An intermediate recovery state is the obvious design and was **not** taken.
- **Receipt of a deferred admin type is a defined, bounded transition** (`B-005-3`) rather than an
  error or an unbounded wait.
- **A bare outbound-sequence field.** Deleted in favour of `SeqnumManager` so two writers could not
  diverge — see [`graceful-logout`](./graceful-logout.md).

## ⚠️ Limitations an integrator must know before trusting this family

- **`L-005-1` — the full `[FIX-TC]` conformance corpus is NOT satisfied.** Only a
  capability-partitioned subset ships. Do not read the size of this family as completeness.
- **`L-005-5` — `OnBehalfOfCompID(115)` / `DeliverToCompID(128)` third-party addressing is not
  implemented.**

⚠️ **A limitation is open only if it is in the LIVE B&L file.** Resolved rows move to
`spec/behaviors-and-limitations-closed.md`, so a repo-wide `grep L-0NN-` reports closed ones as open.

## Runtime flows

[`engine-accept-path`](./engine-accept-path.md) · [`initiator-connect-path`](./initiator-connect-path.md) ·
[`inbound-message-path`](./inbound-message-path.md) ·
[`session-liveness-and-reconnect`](./session-liveness-and-reconnect.md) ·
[`graceful-logout`](./graceful-logout.md) · [`message-store-quiescence`](./message-store-quiescence.md)
