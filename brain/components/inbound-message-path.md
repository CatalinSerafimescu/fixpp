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
  - include/fixpp/wire/framer.hpp
  - src/session/read_pump.hpp
  - src/session/inbound_limit.hpp
  - src/session/parse_capacity.hpp
  - src/session/read_first_frame_bounded.hpp
  - include/fixpp/core/clock.hpp
  - src/core/fix_time.cpp
  - src/session/sending_time.cpp
  - specs/015-runtime-engine/research.md
  - specs/092-garbled-frame-reject/spec.md
  - specs/092-garbled-frame-reject/research.md
  - specs/092-garbled-frame-reject/contracts/unparseable-frame-disposition.md
  - specs/093-inbound-frame-dispositions/spec.md
  - specs/093-inbound-frame-dispositions/plan.md
  - specs/093-inbound-frame-dispositions/research.md
  - specs/093-inbound-frame-dispositions/contracts/inbound-frame-dispositions.md
  - spec/behaviors-and-limitations.md
  - tests/session/read_first_frame_bounded_test.cpp
  - .specify/544-hot-path-zero-alloc.md
  - src/session/session_strand.cpp
  - tests/alloc_guard/test_544_run_thread_windows.cpp
refs_external:
  - research/G19-fix-fpml-iso20022/decisions/speckit/015-runtime-engine-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/092-garbled-frame-reject-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/092-garbled-frame-reject-evidence.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/093-inbound-frame-dispositions-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/093-inbound-frame-dispositions-evidence.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/544-hot-path-zero-alloc-tasks.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/544-hot-path-zero-alloc-evidence.md
codegraph_entry: [run_read_pump, Framer, on_inbound_frame, on_inbound_active_, make_session_strand, Session, scan_frame_header, dispose_unparseable_, inbound_framer_config, note_garbles_, read_first_frame_bounded]
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
| **The carry buffer is allocated once per session and never reallocated** | `[const §VIII.5]` zero-allocation between parse and `fromApp`. *(Since 093 the carry is allocated at `open()`, L plus one read, and the pump borrows it; before 093 the pump built a 64 KiB carry itself. A frame over L is refused at its BodyLength, so the carry cannot overflow in the pump: 093 plan OD-23)* | `Session::open()` allocates it; the pump reads it through `session_engine_access::carry` (`run_read_pump`) |
| **Surplus bytes from the bounded first-frame read drain through the SAME framing path BEFORE the first socket read** | ⚠️ **this is a bug class, not a detail.** A peer may coalesce `Logon`‖next-frame in one segment; reading the socket first would silently drop the second frame | the `initial_bytes` drain block preceding the read loop (F-015-002) |
| **EOF, a read error, a frame over L, an `on_inbound_frame` error, or the establishment deadline closes the session terminally, and the close is idempotent** | `stop()` may already have closed it; a second close must be a no-op, not a fault. *(Before 093 every Framer error closed it too; since 093 a garbled frame is disregarded and only `wire_frame_too_large` ends the pump: see the 093 section below)* | the pump's stop helper → `Session::close(terminal)`; `session_already_closed` is deliberately ignored |
| **A feed's garbles are accounted before any frame that feed produced is delivered** | a garble the Framer reports precedes every frame the same call produces (contract C-1 "Ordering"), so the count, the event and the log record never trail an effect of a later frame | the pump's `note_garbles` after every feed, in both drains (`run_read_pump`) |
| **Until the first Active, expiry is decided at a loop head, never by a race** | asio does not order two ready completions, so a race that decided would let a peer that keeps the socket readable outrun the bound | the `expired()` tests before each read and before each frame's delivery in `run_read_pump`; the read is raced against `await_deadline` only so that it wakes |
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
- ⚠️ **Superseded in part by 093** (fixpp#514): a frame whose third field is not MsgType(35) is now
  disregarded in every state but Disconnected, faulty or not, by a check that runs before this
  ruling's rows; so 092's D-8 is unreachable, and a pre-Active frame of that shape is no longer
  refused. The rest of the ruling stands. See the 093 section below.
- No field of a faulty frame is ever acted on, and no parse failure ever reads as "no reject".

**Where the decision is taken.** It is one inline check in each state arm of `on_inbound_frame`,
right after `scan_frame_header` and ahead of every guard. The disposition itself is
`dispose_unparseable_`. The scan now stops at the **first** fault and records it; the fault-free
path is unchanged. Read contract C-1/C-2 for the order and the rows, and B&L `B-092-*` for what
ships. ⚠️ The invariant worth keeping is **decide before any handler reads a field**. The row
table is not reproduced here; read the contract.

**A late parse failure closes the session** (owner ruling O-2). A parse that runs after the check,
on a frame the scan found fault-free, could still fail for a resource reason. At every such inbound
site the session closes terminally, through `close_on_late_parse_failure_`. There is no Reject and
the callback is not invoked. Effects already taken at the site stand. ⚠️ **Since 093 (fixpp#515)
the failure is unreachable for an admitted frame**, because every inbound parse is sized for the
densest frame of L bytes; the close stays as a defence (093 FR-014). #515 was settled by removing
the failure, not by moving the decision ahead of the effects (B&L `L-092-6`).

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
  (D-7, D-8) carry exactly that cost, disclosed as `L-092-1`. *(D-8 is now 093's third-field
  disregard; the cost is the same, and 093's garbles carry it too, `L-093-7`.)*
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
table, row 4 ("garbled … no Reject, no advance"). Row 1's "Ignore … Unchanged" described a
disregard fixpp did not do until 093 shipped it (fixpp#514; `L-004-4` is now in the closed B&L
file). 092's `spec.md`, contract C-2 (rows D-1/D-2 for a third field that is not 35, and D-8) and
contract C-6's "a late parse can still fail" are point-in-time records of 092, superseded by 093's
contract C-2 step 1 and C-3; they are not edited.

## Garbled frames, one inbound limit, and a bounded establishment (093, fixpp#514, #515, #516)

**The defects.** A frame the Framer could not frame ended the session, where FIX-SL 2020 §4.5.2 says
"disregard" (#514). A well-formed frame could be lost to a parse arena nothing derived (16 KiB on the
stack), or refused by the 64 KiB carry depending on how reads split it (#515). And a frame that took
an early return in the Active arm did not count as inbound traffic, so a busy peer drew TestRequests
(#516).

**The owner's rulings** (spec.md Clarifications, 2026-10-02):
- **R-1 (#516):** every inbound frame that is neither garbled nor faulty refreshes inbound liveness;
  092's "a faulty frame does not refresh" stands.
- **R-2 (#515):** a frame the read pump admits must always parse. One limit L, from the advertised
  MaxMessageSize(383) or else 64 KiB, sizes the carry and the parse capacity, and a frame over it is
  refused at framing with a loud close. The parse index moves to a per-session buffer allocated once.
- **R-3 (#524):** a new `MessageStore` virtual with a default body; recorded on
  [`session`](./session.md) and [`message-store-quiescence`](./message-store-quiescence.md).
- **R-4 (#514):** an establishment timeout, `logon_timeout_ms`, with a C-ABI setter; on expiry the
  transport is closed.
- From specify and clarify: a frame whose third field is not MsgType(35) is disregarded in both
  validation modes and every state (after research R-1 checked the specs and four QuickFIX engines);
  a garbled frame before Logon is disregarded, bounded by the timeout; 10 s default and zero refused;
  a C-ABI counter; 383 above 256 KiB refused.

**Where the decisions are taken.** The Framer's opt-in resync is on [`wire`](./wire.md). On this
path: the pump and the acceptor's first-frame read build their Framer from one config,
`detail::inbound_framer_config` (`src/session/read_pump.hpp`); L comes from `inbound_limit_for`
(`src/session/inbound_limit.hpp`) and the parse buffer from `parse_capacity`
(`src/session/parse_capacity.hpp`); the third-field disregard is the first check in each arm of
`on_inbound_frame` (contract C-2 step 1); the deadline is the `expired()` loop-head test in
`run_read_pump`. Read contract C-1 to C-5 for the rules and B&L `B-093-*` / `L-093-*` for what
ships. ⚠️ The invariant worth keeping: **the pump decides expiry at a loop head, and accounts a
feed's garbles before it delivers that feed's frames.**

### Orchestrator decisions, and what each rejected

These are plan.md's ODs and research R-2 to R-4, R-9, open to review then and settled at Gate A:
- **The deadline lives in the pump** (OD-6), with the acceptor's first-frame read as its phase (a).
  Rejected: a detached Session timer, which needs a join counter and deadlocks `close()`'s join when
  it calls `close()` while counted in `liveness_counter_`; arming per reconnect attempt (there is no
  per-attempt Logon); one connection-scoped watchdog spanning TLS, the store and the Logon write
  (a peer cannot hold those open, so it would test the store, not establishment); and uncharging
  discarded bytes from the first-frame budget (it contradicts the unchanged bounded read).
- **Expiry is a loop-head check, and the race only wakes a blocked read** (Gate A round 2).
  Rejected: letting the race decide, because nothing orders two ready completions.
- **On TLS, phase (a) lasts `max(T, the handshake bound)`.** Rejected: clamping the listener-wide
  handshake bound per connection.
- **Garbles are counted, evented and logged even after `close()` began** (OD-7): a transport
  observation, not an arm effect, so the third-field disregard runs before FR-030's closing check.
- **The first-frame garbles reach the Session as one summary** (OD-8). Rejected: a per-region record
  list (about 1365 records per accept at 3 bytes a region) and the counter on `SessionEntry` (no
  observable gain under one Session per entry per `start()`, and a reader path that survives
  `registry_.clear()` would be new).
- **The first production log site is `FIXPP_SLOG`, rate-bounded to `max(HeartBtInt, 1 s)`** (OD-5;
  the 1 s floor because HeartBtInt 0 is legal). A record's suppressed count is in regions, the
  counter's unit, so the log reconciles with `garbled_frame_count()` (OD-21). Rejected: counting
  only suppressed summaries, which breaks the reconciliation for a logged summary of several
  regions.
- **One limit L for everything** (R-2's ruling), checked at `register_session` and at `open()`,
  with a 4096 floor (OD-2: fail at configuration, not at every Logon). Rejected: no floor.
- **070's pre-establishment exemption is reversed** (OD-3). Rejected: keeping it for frames over L
  but within 64 KiB, at the cost of a second, larger buffer before Active.
- **An over-L frame records a log line and no `SessionEvent`** (OD-24). Rejected: a new public
  `session_event_frame_too_large`, unreviewed API that duplicates the close the application sees.
- **The carry is allocated at `open()`, L plus one read** (OD-13). Rejected: carry = L, which makes a
  frame of exactly L depend on segmentation; and building it in the pump, where an allocation
  failure is a `std::terminate` inside a `noexcept` constructor rather than an `open()` error.
- **The parse buffer is per session, allocated once** (R-2's ruling; research R-3). Rejected: a
  per-thread buffer, lazy growth (it allocates on the inbound path), reserving from the header
  scan's field count (no size gain, and two scanners that must agree forever), and a reserve held in
  `OffsetTable::Config` (clones and reifies copy it into a `frame_len + 4096` arena).
- **The deadline race may allocate before the first Active; it is measured, not asserted** (R-9,
  L-093-13). No pmr counter can see it, because asio's recycling allocators do not draw on a pmr
  resource; the instrument is a global `operator new` counter with a positive control.

⚠️ **Spec drift, recorded and not edited:** contract C-8 L-1 says the stall "closes when the carry
overflows", which plan OD-23 makes unreachable in the pump (`L-093-1` has the re-derivation), and
spec FR-013 says an over-L close is recorded "with an event and a log", which OD-24 narrowed to a
log record.

## A timestamp the time type cannot hold (fixpp#509)

A peer's `SendingTime(52)`, and its `OrigSendingTime(122)` on a PossDup message, are parsed by
`core::fix_string_to_utc_time`. The grammar's four-digit year reaches past both ends of
`utc_time_point`'s signed 64-bit nanosecond count. Before #509 such a year overflowed the composition,
the parse succeeded, and `check_sending_time`'s subtraction overflowed a second time. Both are UB on
input a peer controls (`[const §XII]`).

**Decision:** refuse at the parse, and make the accuracy check exact for any two representable time
points. The parse returns the grammar's existing error. Every caller already dispositions an unparseable
timestamp, so no caller changed; B-509-2 lists what each site now does. The accuracy check still needs
its own fix after the parse fix: a representable time far from the clock can overflow the difference.

### What was rejected, and why

- **Patching the callers** (a range test at each `fix_string_to_utc_time` site). Five sites, and any
  new caller would repeat the hole. The parse is the one place the wrong value is created.
- **Year literals for the bound.** A comment or constant naming the edge years is a result, and it
  goes stale if the rep or period of `utc_time_point` changes. The bound is derived from `min()` and
  `max()`.
- **`__builtin_mul_overflow` / `__builtin_add_overflow`.** The MSVC lane has no such builtin.
- **A cold out-of-line edge path.** It measured the same as the inline shape within noise in a paired
  run, and it added compiler-specific attributes. The owner kept the simpler shape and waived
  `[const §VIII.2]` for the cost of about 1 ns per parse (fixpp#509 owner rulings, 2026-09-29).
- **Adding a Reject for an out-of-range `52` on a PossDup Reject(35=3) or Logout(35=5).** That site
  already falls through for an unparseable `52`, so fixing it is a change to that rule, not part of
  #509. It is disclosed as `L-509-1`.

## Deadline arithmetic under an arbitrary `Clock` (093 Gate B)

`Clock::steady_now()` (`include/fixpp/core/clock.hpp`) promises monotonicity and nothing about range.
An embedder's clock (`EngineConfig::clock`, or a session's `SessionConfig::clock_override`) may
read negative or close to `steady_time_point::max()`, and two readings may lie further apart than
`duration::max()`. A plain `now + d` or `deadline - now` is then signed overflow, which is UB. So
the deadlines 093 added go through two helpers in `src/session/read_first_frame_bounded.hpp`:

- **`detail::deadline_after(now, d)`**: exactly `now + d` when that instant is representable, else
  `max()`, for any `d >= 0` its `static_assert`s admit. The headroom is checked before `d` is
  converted to the time point's units, because the conversion can overflow by itself (a
  `heartbeat_interval` in seconds, converted to nanoseconds). `max()` is the right saturation value
  because the clock reaches it only at its end, so the deadline never fires early. As a
  consequence, a `Clock`'s `sleep_until` may be handed `max()` (B&L `B-093-4`).
- **`detail::duration_until(now, deadline)`**: total. It returns zero once `now` has reached the
  deadline, the exact difference when that fits, and `duration::max()` otherwise. The acceptor's
  time left after the handshake uses it on a single clock read.

Witnesses: the `DeadlineAfter` and `DurationUntil` suites in
`tests/session/read_first_frame_bounded_test.cpp`; the engine-level cells are listed in `B-093-4`.

- **Why helpers, not site-by-site fixes.** A site-by-site fix covered the sums, and the review of
  that fix found more members of the class: a difference, and the sum helper's own inexact branch
  for a negative `now`. Closure came from enumerating every time-point and duration expression in
  the PR's `src/` and `include/` diff, and following each saturated value to its sink
  (`Clock::sleep_until` or an asio timer). That enumeration is the re-derivation recipe; rerun it
  rather than trusting any list of sites.
- **Rejected for this PR: a range precondition on `Clock`** (`steady_now()` non-negative, or two
  readings at most `duration::max()` apart). It changes `clock.hpp`, a public contract ratified at
  Gate A. It cannot be enforced inside a `const noexcept` virtual, so it would be a claim that rots.
  And it does not cover the duration side: an oversized `heartbeat_interval` overflows even with
  the real clock. It is deferred as an owner question on fixpp#555, for the older sites.
- **Older sites are tracked in fixpp#555**: the session's graceful-logout wait and liveness loop
  still form raw sums. Check the issue's state, and grep `src/session/session.cpp` for
  `steady_now() +` and `+ heartbt_int`, before relying on either.

## Zero allocation on the Active read (fixpp#544)

The design is `.specify/544-hot-path-zero-alloc.md` (§1, §2.2; its "As built" section records where the
implementation departs). The user-facing statement is B&L `B-544-1`; what stays open is `L-497-1`.

- **The invariant.** On an Active session, a read up to `fromApp` makes no global-heap allocation in
  steady state, when the Engine's executor's target type is exactly `io_context::executor_type` and the
  thread that runs the session's handlers stays inside one scheduler call. **Its gates are W-A (an
  Active Heartbeat) and W-B (an Active application message through `fromApp`)**: mallocnesia
  registrations on the two Linux Release presets (`ctest -N -L 544` lists them), and the TU-local
  `operator new` counter of `alloc_guard_544_run_thread`, which also runs on `windows-msvc-release`.
  ⚠️ The recycler's cache belongs to the **scheduler call**, not to the thread, so a pump driven by
  bounded `run_for` / `run_one*` / `poll*` calls allocates on every message (arm (s) in
  `tests/alloc_guard/test_544_run_thread_windows.cpp` pins that it does).
- **The dispatcher and the arms.** `Session::on_inbound_frame` is a **non-coroutine** that returns an
  awaitable, so it adds no frame. As built (orchestrator ruling A → a2) it always returns the Active
  arm, `on_inbound_active_`, which reads the FSM state on the strand at resume and hands a cold state to
  its own arm (one frame more, on ungated paths). Selecting the arm in the dispatcher was rejected: it
  would read the FSM state on the caller's thread. Every arm takes its parameters by value, because an
  awaitable starts suspended and runs after the dispatcher has returned.
- **Two lifetime classes for the scratch buffers, decided by data flow, not lexical scope.** Class 1: a
  buffer last read, with every view into it, before the arm's next `co_await` moves to a non-coroutine
  helper's stack. Class 2: a buffer a later `co_await` reads moves into a **reply leaf** coroutine that
  owns it and performs the emit, because `store_then_emit` transmits the caller's original span after
  suspending in the store. **Rejected: v0.4's hoist of every buffer into a non-coroutine helper** — a
  use-after-return for every class-2 buffer, measured as differing emitted bytes at `-O2` and as
  `stack-use-after-return` under ASan with default options. **Also rejected:** keeping the buffers in the
  arm (GCC 13 does not overlap disjoint-scope locals, so the arm is over the limit on
  `linux-gcc-release`), and a `Session`-member reply buffer (other chains emit while a reply is suspended,
  so it would need a per-chain exclusivity argument the leaf does not).
- **The cancellation check at a new boundary (ruling B → b3).** Each `co_await` boundary the split
  introduces goes through `FIXPP_INBOUND_SPLIT_AWAIT`, and each callee starts with
  `FIXPP_INBOUND_SPLIT_ENTRY` (`src/session/session.cpp`): the first cancellation check stays at the moved
  block's first original `co_await`, so a callback that ran before an await under a cancelled chain still
  runs. `session_544_inbound_split_cancel` holds the differential cells.
- **The other two changes on this path.** The session strand keeps the concrete executor type
  (`make_session_strand`; see [`session`](./session.md)), and `SeqnumManager::check_inbound` locks
  through the frameless lock op (see [`async-mutex`](./async-mutex.md)). The plain and TLS transport
  reads are awaited as `deferred` operations (see [`transport`](./transport.md)).
