---
type: Component Decision Map
title: session — the FIX engine, which no design doc owns
description: Second-largest catalogue family, ~20 feature bundles, nine cited design docs, and no 2* doc that owns it. This page is the routing layer that absence leaves missing.
status: stable
refs:
  - include/fixpp/session/session.hpp
  - include/fixpp/session/session_fsm.hpp
  - include/fixpp/session/seqnum_manager.hpp
  - include/fixpp/session/config_byte_floor.hpp
  - .specify/447-458-452-capi-refusals.md
  - specs/005-session-establishment-fsm/spec.md
  - spec/behaviors-and-limitations.md
refs_external:
  - research/G19-fix-fpml-iso20022/decisions/speckit/005-session-establishment-fsm-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/013-session-reconnect-binding-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/024-reset-refresh-on-logon-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/025-refresh-on-logon-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/027-next-expected-msgseqnum-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/029-persistent-seqnum-hydrate-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/033-fixt-fix50sp2-session-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/037-resend-reply-possdup-tags-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/042-fixt-version-serviceability-guard-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/090-capi-refusals-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/092-garbled-frame-reject-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/511-test-hooks-odr-gatea.md
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
| Sequence numbers, persistence, hydration | `SeqnumManager`; `specs/029-persistent-seqnum-hydrate/` ⚠️ **029 `tasks.md` T014 (`[X]`, "Live restart-resume interop cell … both roles") is FALSE as a result claim: `hp_fix44_restart_resume_test.cpp` does not run live.** Flagged here and not edited (frozen `specs/` are not rewritten): no harness cell selects the interop suite named in this row — its ids are in the `unregistered-tracked` group of `tests/interop/live-cells-excluded.txt`, and fixpp#503 owns making it live. Re-derive the bundles' live-cell claims with `git grep -n -i -E -e 'live [a-z -]*cell' -e 'bidirectional-gap cell' -- specs/025-refresh-on-logon specs/027-next-expected-msgseqnum specs/029-persistent-seqnum-hydrate`; prospective `plan.md` / `spec.md` lines are design intent, not result claims. |
| PossDup / OrigSendingTime / PossResend | `specs/021-…`, `specs/022-…`. ⚠️ **021 FR-004's at-expected "MUST NOT advance … matches QuickFIX" is SUPERSEDED by fixpp#423** (B-423-1); see the rejected alternative below. |
| Resend answers (replay + GapFill) — how `build_replay_frame` / `build_sequence_reset_gapfill` build the wire frame | `specs/013-session-reconnect-binding/spec.md` FR-010; `specs/037-resend-reply-possdup-tags/` (43/122 emission). ⚠️ **037's `spec.md` Assumptions section, `research.md` D-3, `plan.md`, `data-model.md`, `contracts/resend-reply-wire.md`, and `checklists/wire-conformance.md` CHK010 all describe placing `43`/`122` AFTER the body as "order-safe" / "field order is unconstrained for interop" — FALSE, superseded by fixpp#419: a strict peer (QuickFIX-J `UseDataDictionary=Y`) rejects it (373=14). The bundle is a point-in-time record, left as-is; do not trust its field-order claims. See `spec/behaviors-and-limitations.md` `## fixpp#419` for current behaviour.** ⚠️ **037 FR-006 / SC-003 ("replayed application frames MUST be byte-identical to prior behavior") are also SUPERSEDED, by fixpp#420: a replay restamps `SendingTime(52)`. 013 FR-010's "byte-for-byte" clause governs `122` only and still holds.** See `## fixpp#420 / fixpp#424` in the same file. |
| Reset & refresh on Logon | `specs/024-reset-refresh-on-logon/`, `specs/025-refresh-on-logon/` ⚠️ **025 `tasks.md` T041 (`[X]`, "the live standby-re-hydrate interop cell … capture the golden") and T002's "standby-re-hydrate live cell" are FALSE as result claims: the `StandbyRehydrate_Initiator` suite does not run live.** Flagged here and not edited (frozen `specs/` are not rewritten): no harness cell selects the interop suite named in this row — its ids are in the `unregistered-tracked` group of `tests/interop/live-cells-excluded.txt`, and fixpp#503 owns making it live. Re-derive the bundles' live-cell claims with `git grep -n -i -E -e 'live [a-z -]*cell' -e 'bidirectional-gap cell' -- specs/025-refresh-on-logon specs/027-next-expected-msgseqnum specs/029-persistent-seqnum-hydrate`; prospective `plan.md` / `spec.md` lines are design intent, not result claims. |
| NextExpectedMsgSeqNum | `specs/027-next-expected-msgseqnum/` ⚠️ **027 `tasks.md` T022's "+ a bidirectional-gap cell" and its `## Notes` line "T022 the live cell" are FALSE as result claims for the bidirectional suite: `NextExpectedBidirectional` does not run live. T022's "recover an at-logon gap with zero `ResendRequest`" and contract C10's "recover an at-logon gap … all missed messages delivered in order" are ALSO false as result claims for the initiator and acceptor NE cells: those cells run live but seed no gap (parent `run_interop_cell.py` NE block, "no resend"), so they witness only 789 negotiation and in-sync honour, and fixpp#503 owns the gap cells.** Flagged here and not edited (frozen `specs/` are not rewritten): no harness cell selects the interop suite named in this row — its ids are in the `unregistered-tracked` group of `tests/interop/live-cells-excluded.txt`, and fixpp#503 owns making it live. Re-derive the bundles' live-cell claims with `git grep -n -i -E -e 'live [a-z -]*cell' -e 'bidirectional-gap cell' -e 'recover an at-logon gap' -- specs/025-refresh-on-logon specs/027-next-expected-msgseqnum specs/029-persistent-seqnum-hydrate`; prospective `plan.md` / `spec.md` lines are design intent, not result claims. |
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
| **`open()` refuses a configured string that an admin builder would copy verbatim, if it holds a byte `< 0x20` or `'='`** — CompIDs, BeginString, each `supported_msg_types[].msg_type`, and the credentials | those values reach the wire through `append_raw` unvalidated, so one SOH injects a field. Every admin builder emits CompIDs and BeginString; `build_logon` adds RefMsgType and the credentials when they are configured. ⭐ **ONE predicate, `fixpp::session::contains_forbidden_config_byte`** (`config_byte_floor.hpp`), also called by the C-ABI setters before any `Session` exists. ⚠️ **It is a POLICY floor, not FIX grammar**: fixpp's scanner splits at the *first* `'='`, so `'='` is legal in a value as fixpp parses it. Do not cite it as the grammar | `B-452-1`; residual `L-452-2` |
| **A Logon arm that resumes after `close()` began stops where it is** — no further frame, no Active, no `onLogon`, no liveness loop, and inside an interrupted 141=Y reset no counter write once `close()`'s teardown reset has been issued (otherwise the unit completes, keeping the consumed Logon's advance) | a Logon arm suspends, and `close()` can run in that window. The guarded suspensions: on the acceptor the hydrate and the `reset_on_logon` reset; in a peer-requested (141=Y) reset the store reset and each restore's persist; the reply write; the NextExpectedMsgSeqNum(789) handling; after `Active`, the persist of the Logon's inbound advance. `close()` can be posted from the application's `toAdmin` or `onLogon` via `Engine::lookup`, or from another thread. A graceful close leaves the FSM in `LogonReceived` while its Logout waits on the write gate the reply holds, so the check is "close began" (`state_ == lifecycle::closing`), not only "the FSM moved". Before fixpp#518 the arm overwrote close's `Disconnected` with `Active`, fired `onLogon` after close began, and spawned a liveness loop that nothing cancels | `B-518-1`; `L-518-1` |

## What was rejected — the half the code cannot tell you

- **A `RecoveryPending` half-state.** `B-005-2` states there is none: the FSM is *exactly* the
  `[FIX-SL §4.10]` set. An intermediate recovery state is the obvious design and was **not** taken.
- **Receipt of a deferred admin type is a defined, bounded transition** (`B-005-3`) rather than an
  error or an unbounded wait.
- **A bare outbound-sequence field.** Deleted in favour of `SeqnumManager` so two writers could not
  diverge — see [`graceful-logout`](./graceful-logout.md).
- **Keeping the stored `SendingTime(52)` on a replay** (fixpp#420, owner ruling 2026-09-14). The
  obvious reading of "the store is the authority" (013 FR-010), but that clause governs `122`. FIX-SL
  2020 §4.8.4 wants `52` restamped. A peer also rejects an unrestamped replay once it is older than
  the peer's MaxLatency, and that check covers PossDup frames in both QuickFIX engines.
- **Four answers to a stored message whose replay cannot be built** (fixpp#424, owner ruling
  2026-09-14). The chosen one gap-fills the slot and records a
  `session_event_resend_slot_gap_filled` (D4a). Rejected:
  - skipping the slot silently, the pre-#424 behaviour, which leaves the peer's gap open;
  - failing the whole resend (option b), which also leaves the gap open;
  - QuickFIX-cpp's behaviour of abandoning the rest of the range;
  - both QuickFIX engines' `FieldNotFound` on a stored frame with no `52`. fixpp instead replays it with
    `122` := the new `52`, which is what the StandardHeader says to do when the data is unavailable.
- **Rebuilding a replay after the GapFill flush that precedes it** (fixpp#420 review, 2026-09-14).
  The replay is stamped and built before an open gap run is flushed, so that an unbuildable slot can
  join that run. A flush that blocks on the transport therefore leaves the replay's `52` older than
  its send time. Rebuilding after the flush was declined for #420: it costs a second build per replay,
  and the stamp is late only by as long as that one write blocks.
- **Two homes for the configured-byte floor** (090 D-5b, fixpp#452). The first was the function-local
  lambda that `Session::open` used for the credentials. It had no linkage, so
  `src/capi/config.cpp` could not call it. The second was `src/session/file_store_factory.cpp`'s
  comp-id check, which is the wrong layer with the wrong charset: SOH and `'='` pass it, and the
  C-ABI default `MemoryStoreFactory` runs no comp-id check at all. Widening the charset (all C0,
  printable-only) was **not** taken either. It is a separate decision with its own blast radius.
- **Gap-filling a frame too large to capture** (fixpp#424 D5). Rejected *for now*: it stays a loud
  disconnect (`L-424-1`), deferred, and may be reopened.
- **Rejecting a `send()` payload whose header-class field follows a body field** (fixpp#422, owner
  decision 2026-09-14). The field is moved into the header instead; a reject would also have broken
  callers that append `43`/`122` under `allow_pos_dup=true`.
- **One header-tag table for both the send partition and the replay's insertion point** (fixpp#422).
  They pull in opposite directions: the send table (`is_send_header_tag`) must be a SUPERSET of any
  real header, or a header field stays behind the body; the replay's `kReplayHeaderTags` only needs to
  be a SUBSET (B-419-1). Reading the set from the session's dictionary was not taken either (L-422-1).
- **Putting the canonical outbound-tag rule in `wire/tag_scan.hpp`** (fixpp#421). That header serves
  the inbound scanners, which accept zero padding by design; the outbound rule
  (`parse_outbound_tag`) stays in `session.cpp`.
- **Widening `Writer::append_raw` to a 32-bit tag that rejects values above 65535** (fixpp#421). It
  would add an error arm to every literal-tag caller to protect the one runtime-tag caller, the replay,
  which now narrows by type instead.
- **Leaving an in-sequence rejected message's MsgSeqNum unconsumed, "for QuickFIX parity"** (fixpp#423,
  owner ruling 2026-09-14).
  - **Who chose it:** 041 (contract C-3) and 021 (FR-004, at the expected number). 016's thorny
    C-102 witness chose the opposite for the Rejects after the seqnum gate.
  - **Why it was wrong:** the parity claim was false, since both QuickFIX engines' `generateReject`
    increment at the expected number. FIX-SL 2020 §4.5.4 also says NextNumIn *must* be incremented.
  - **What it did live:** the session stalled.
  - **Still not consumed:** a Logon or SequenceReset, a message at any other number, and the
    establishment arms (`consume_rejected_seqnum_`).
  - ⚠️ **041's `contracts/validation-gate.md` C-3 and `spec.md` (Clarifications, FR-003, edge cases),
    and 021's `spec.md` FR-004, still say "does not advance". They are point-in-time records, left
    as-is. See B-423-1.**
- **Guarding a Logon arm against a concurrent `close()` in one central place** (fixpp#518).
  - **Refusing the Active write inside `record_state_transition_`** was rejected. Refusing the write does not stop the arm: it would still build and emit frames, fire `toAdmin` and spawn liveness. It would also need an error return across every caller.
  - **Failing `store_then_emit` once closing** was rejected. `close()`'s own graceful Logout goes through that function. It also does not cover the hydrate and reset suspensions, which come before any emit.
  - **What shipped instead:** each guarded suspension in the Logon arms (B-518-1 lists them) is followed by one predicate, `logon_arm_superseded` in `Session::on_inbound_frame`.
  - **A guard after every `co_await`** was rejected where the await cannot yield: the seqnum-mutex awaits (`check_inbound`, `assign_outbound`). An uncontended `async_lock` grants inline, so there is no window, and no cell could go RED on such a guard (L-518-1 names the premise and what runs after those awaits).
  - **Either unconditional form inside a 141=Y reset unit that `close()` interrupted** was rejected.
    - *Always completing the unit* (PR #522, Gate B round 1). Measured on the unguarded source, with `reset_on_disconnect` and a terminal close posted from the arm's peer reset: the arm's inbound persist landed after `close()`'s teardown reset, so the store ended at next-inbound 2 instead of 1, on both roles.
    - *Always stopping mid-unit* (PR #522, Gate B round 2). With no teardown reset configured nothing repairs the store, which is left expecting seq 1 after the consumed reset Logon, so a peer that next logs on at seq 2 without 141=Y is too high — fatal with the NextExpectedMsgSeqNum(789) tolerance off.
    - *Keying the stop on `cfg_.reset_on_disconnect`* misses `reset_on_logout` (a graceful phase 1 sets `logout_seen_`) and stops needlessly when a teardown reset is configured but not yet issued.
    - *One atomic durable unit* (a store reset-to-values) needs a new `MessageStore` operation, outside the owner's rulings for #518.
    - **What shipped (owner-approved, PR #522 Gate B round 2):** the unit stops mid-way only once `teardown_reset_done_` is set. `close()` sets it just before it awaits the teardown reset and never clears it; no Logon arm runs on a `Session` after its `close()` (L-518-1). Premise: the store applies operations in issue order (L-518-1).
  - **A check straight after `onLogon`, before the post-`Active` persist,** was rejected (PR #522, Gate B round 2). By then the peer's Logon has been consumed, answered and made `Active`, so skipping its persist leaves the store one behind the peer, and a peer that next logs on at the following sequence number is too high. The check follows the persist instead, before the liveness loop is spawned.
  - **A check after the 141=Y reset event** was rejected (PR #522, Gate B round 2). On the initiator it let the reset event, the CompID authorization and the FIXT version record run after `close()` began; the acceptor's reset event had the same order. The check follows the last restore, before the event.
  - ⚠️ **Rule: a `co_await` in a Logon arm that can yield is followed by the check before the next frame, callback or state write. Inside a 141=Y reset unit, a counter write that completes the consumed Logon's accounting is skipped only once `teardown_reset_done_` is set.**

## ⚠️ Limitations an integrator must know before trusting this family

⚠️ **This is a SELECTED pair, not the full set** — the family carries roughly a dozen open
`L-` rows across 005 / 009 / 013. **Derive the full list from the live B&L file**; a blind
agent correctly treated this as a shortlist and re-derived, which is the intended use.

- **`L-005-1` — the full `[FIX-TC]` conformance corpus is NOT satisfied.** Only a
  capability-partitioned subset ships. Do not read the size of this family as completeness.
- **`L-005-5` — `OnBehalfOfCompID(115)` / `DeliverToCompID(128)` third-party addressing is not
  implemented.**
- **A framed but unparseable inbound frame (fixpp#507, feature 092).** ⚠️ **"Garbled" is the wrong word
  for these frames, and this bullet used to use it.** The owner ruling of 2026-09-27 reserves "garbled" for
  the four FIX-SL 2020 §4.5.2 framing criteria. A frame that passes the Framer and then has a malformed tag
  or a Length+Data mismatch is a TagValue encoding violation, which §9.4 routes to a Reject. It is not a
  §4.5.2 case. The old bullet's defect, the session acting on header fields it scanned from such a frame,
  is the one 091's T076 found. 091 fixed only the Logon case (FR-020), and 092 ships the rest: a disposition
  by state, decided before any handler reads the frame. The behaviour and its limitations are the live B&L
  file's `B-092-*` / `L-092-*` rows, and the decision and its rejected alternatives are on
  [`inbound-message-path`](./inbound-message-path.md). **Still open, and not 092's:**
  - a true §4.5.2 failure detected by the Framer still ends the session (`L-004-4`), and its disregard is
    fixpp#514;
  - a late parse failure closes the session, and effects taken before the close stand; that is fixpp#515.

  Read the code, not this bullet: `dispose_unparseable_` in `src/session/session.cpp`.

⚠️ **A limitation is open only if it is in the LIVE B&L file.** Resolved rows move to
`spec/behaviors-and-limitations-closed.md`, so a repo-wide `grep L-0NN-` reports closed ones as open.

## Test access to private `Session` state — read before giving a test one

`Session` lives in a PUBLIC header (`include/fixpp/session/session.hpp`). Since fixpp#511 (B21) there is
one way for a test to reach its private state: the unconditional friend `session_test_access`. Define it
only in `tests/support/session_test_access.hpp`. `SeqnumManager`, `MemoryStore` and `Engine`
follow the same pattern, each with its own `*_test_access` header there. Verify against `session.hpp`.

- **Adding an accessor:** add a static function to the existing struct in `tests/support/`. Do not touch
  the class.
- **Wrong: anything inside the class gated on `FIXPP_TEST_HOOKS`.** That covers members, friends and
  access specifiers. The libraries are compiled without the macro, so every hooked test program would
  hold two definitions of the class: an ODR violation, ill-formed with no diagnostic required. That was
  #511.
  - A gated NAMESPACE-scope declaration of a function the library defines unconditionally is fine. The
    seams in `file_store.hpp` and `src/capi/capi_internal.hpp` are examples.
- **Wrong: a second, *differing* definition of a `*_test_access` struct**, e.g. a TU-local one. Two
  TUs of one test program would then disagree about the struct: an ODR violation inside the test
  program.
- **Still first:** look for a public observable before reaching for private state.

**Decision history:**
- Before #511, this page listed the public-header friend as "Wrong: needs a Gate A ruling". It also told
  tests to reuse an already-hooked target, because PR #510 (091) Gate B rounds 2 and 3 had each moved the
  defect instead of removing it (parent repo, `decisions/speckit/091-data-field-bytes-gateb.md`).
- The owner then ruled for the named friend, on 2026-10-01. Before that, any consumer could already
  `#define FIXPP_TEST_HOOKS` for the same access, with UB. Rejected: public seqnum getters, hooked library
  twins, and the explicit-instantiation access loophole. Record: parent repo,
  `decisions/speckit/511-test-hooks-odr-gatea.md`.

## Runtime flows

[`engine-accept-path`](./engine-accept-path.md) · [`initiator-connect-path`](./initiator-connect-path.md) ·
[`inbound-message-path`](./inbound-message-path.md) ·
[`session-liveness-and-reconnect`](./session-liveness-and-reconnect.md) ·
[`graceful-logout`](./graceful-logout.md) · [`message-store-quiescence`](./message-store-quiescence.md)
