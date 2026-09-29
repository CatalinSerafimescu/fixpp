---
type: Flow Decision Map
title: Inbound message path — socket bytes to fromApp
description: The read pump's invariants and where each is enforced. Written because no design doc owns a runtime flow; only 2 of ~30 do.
status: stable
refs:
  - src/session/engine.cpp
  - include/fixpp/session/session.hpp
  - src/session/session.cpp
  - src/session/scan_frame_header.hpp
  - specs/015-runtime-engine/research.md
  - specs/092-garbled-frame-reject/spec.md
  - specs/092-garbled-frame-reject/research.md
  - specs/092-garbled-frame-reject/contracts/unparseable-frame-disposition.md
  - spec/behaviors-and-limitations.md
refs_external:
  - research/G19-fix-fpml-iso20022/decisions/speckit/015-runtime-engine-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/092-garbled-frame-reject-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/092-garbled-frame-reject-evidence.md
codegraph_entry: [run_read_pump, Framer, on_inbound_frame, Session, scan_frame_header, dispose_unparseable_]
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

⚠️ **A rejected message is not "unprocessed".** Before fixpp#423 the Rejects issued ahead of the
sequence-number check (041's validate gate, SendingTime accuracy, 021's PossDup arms) left the
MsgSeqNum unconsumed. The peer's next message then looked like a gap, and the session stalled. An
in-sequence rejected message now consumes its number (FIX-SL 2020 §4.5.4). The decision and the
rejected "QuickFIX parity" alternative are on [`session`](./session.md); the behaviour is B-423-1.

## A frame that passes the Framer but not the parse (092, fixpp#507)

**The defect.** The session drove its state machine from a header scan that silently skipped a
malformed tag and silently stopped at a malformed Length count. Every consumer of the full parse
read a failure as "nothing to do". So a SequenceReset was applied, a TestRequest answered and a
Logout honoured from a frame that never parsed, and an application message was consumed and lost
with neither side told (#507, found by 091's T076).

**The ruling** (owner, 2026-09-27, on #507; it revises #423's ruling table, row 4):
- "Garbled" means only FIX-SL 2020 §4.5.2's four framing criteria. A frame that passes framing and
  fails the parse is a TagValue encoding violation, and §9.4 routes it to a Reject.
- That Reject follows #423's seqnum rows: it advances NextNumIn only at the expected number, and a
  Logon or SequenceReset never advances through it.
- A frame whose MsgSeqNum(34) was not read before the fault is disregarded (TC2020 17g). So is one
  whose third field is not MsgType(35), in LogonReceived/Active (§4.5.2 criterion 3).
- Before Active the frame is refused like any non-Logon, and in LogoutSent it is disregarded.
- No field of a faulty frame is ever acted on, and no parse failure ever reads as "no reject".

**Where the decision is taken.** It is one inline check in each state arm of `on_inbound_frame`,
right after `scan_frame_header` and ahead of every guard. The disposition itself is
`dispose_unparseable_`. The scan now stops at the **first** fault and records it; the fault-free
path is unchanged. Read contract C-1/C-2 for the order and the rows, and B&L `B-092-*` for what
ships. ⚠️ The invariant worth keeping is **decide before any handler reads a field**. The row
table is not reproduced here; read the contract.

**A late parse failure closes the session** (owner ruling O-2). A parse that runs after the check,
on a frame the scan found fault-free, can still fail for a resource reason. At every such inbound
site the session closes terminally, through `close_on_late_parse_failure_`. There is no Reject and
the callback is not invoked. Effects already taken at the site stand; moving the decision ahead
of them is fixpp#515.

**NextNumIn never wraps** (FR-019). `SeqnumManager::check_inbound` refuses to advance from
`seqnum_max` with `store_seqnum_overflow`, and every consuming caller then ends the session
silently. Before 092 it wrapped to 0 and the session continued. This is the inbound twin of the
outbound invariant on [`session`](./session.md) (`B-005-4`); the caller population is research R-14.

**C-ABI 1.10, BREAKING** (`[const §X.7]`). It changes which inbound Logons are accepted and which
inbound frames end an established session. No symbol or code changes. The carriers are on
[`c-api`](./c-api.md).

### What was rejected, and why

- **Ignore by default** (disregard every framed-but-unparseable frame). A deterministically
  malformed frame is resent identically on every ResendRequest, and fixpp has no resend-loop guard.
  That is the stall B-423-1 measured live against QuickFIX-cpp. The disregard rows that remain
  (D-7, D-8) carry exactly that cost, disclosed as `L-092-1`.
- **373 = 99 ("Other").** 99 is not a valid SessionRejectReason on FIX.4.2 (plan.md, the Gate A
  round 1 fixes; check the 373 enum in `dictionaries/FIX42.xml`). The fault kinds map to defined codes, 0 for a malformed tag and 5 for a
  Length+Data mismatch (research R-6). Gate A round 2 deleted the residual late-site Reject that once
  carried a fallback code (research R-6).
- **Changing what `field_iterator` yields.** It would add C-ABI effects through `scan_slice_for_tag`,
  and a slice can legitimately end where a whole frame cannot (research R-7). The iterator reports
  its fault instead; see [`wire`](./wire.md).
- **A per-site Reject table for late parse failures.** O-2 ruled that a resource failure of a
  well-formed frame is not a peer encoding error. Within 092 every late site does one thing: it
  closes (contract C-6).
- **Keying the Reject on "a 35 was seen"**, or making the whole scan first-wins. The first would
  Reject and advance a frame whose MsgType is not third, against ruling row 1. The second changed
  fault-free frames: a duplicate 34 went from delivered to too-low (research R-1). The scan records
  the first 34 and the positional 35 in fault-only members instead.
- **C-ABI witnesses for every BREAKING effect.** The T020 ruling relies on the C++ session cells for
  the effects without C-ABI cells, because the C observers read the session state through the thin
  `src/capi` layer. This follows 091's 1.9 precedent. Which effects have C-ABI cells is stated in
  `L-092-12`.

⚠️ **Frozen records that now say the wrong thing**, flagged here and not edited: #423's ruling
table, row 4 ("garbled … no Reject, no advance"). Row 1's "Ignore … Unchanged" also describes a
disregard fixpp never did (`L-004-4`; fixpp#514).
