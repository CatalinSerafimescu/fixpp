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
  - include/fixpp/session/message_store.hpp
  - include/fixpp/session/engine.hpp
  - src/session/session.cpp
  - src/session/send_header_tags.hpp
  - src/session/engine.cpp
  - src/session/session_strand.cpp
  - src/session/fixt_route_set.hpp
  - src/session/peek_fixt_route.hpp
  - src/session/fixt_session_check.hpp
  - src/session/logon_msg_type_defaults.hpp
  - .specify/447-458-452-capi-refusals.md
  - .specify/544-hot-path-zero-alloc.md
  - .specify/536-537-538-recovery-liveness.md
  - .specify/563-564-callback-send.md
  - specs/059-outbound-store-fail-closed/research.md
  - specs/059-outbound-store-fail-closed/contracts/store-then-emit-disposition.md
  - specs/005-session-establishment-fsm/spec.md
  - specs/093-inbound-frame-dispositions/spec.md
  - specs/093-inbound-frame-dispositions/plan.md
  - specs/093-inbound-frame-dispositions/research.md
  - specs/093-inbound-frame-dispositions/contracts/inbound-frame-dispositions.md
  - specs/099-fixt-appl-version-validation/spec.md
  - specs/099-fixt-appl-version-validation/plan.md
  - specs/099-fixt-appl-version-validation/research.md
  - specs/099-fixt-appl-version-validation/data-model.md
  - specs/099-fixt-appl-version-validation/contracts/surface.md
  - specs/101-inbound-reject-fidelity/spec.md
  - specs/101-inbound-reject-fidelity/research.md
  - specs/101-inbound-reject-fidelity/data-model.md
  - specs/101-inbound-reject-fidelity/contracts/reject-frames.md
  - spec/behaviors-and-limitations.md
refs_external:
  - research/G19-fix-fpml-iso20022/decisions/2d-threading.md
  - research/G19-fix-fpml-iso20022/decisions/2e-msgstore.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/005-session-establishment-fsm-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/013-session-reconnect-binding-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/019-app-callbacks-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/023-engine-session-strand-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/024-reset-refresh-on-logon-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/025-refresh-on-logon-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/027-next-expected-msgseqnum-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/029-persistent-seqnum-hydrate-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/033-fixt-fix50sp2-session-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/037-resend-reply-possdup-tags-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/042-fixt-version-serviceability-guard-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/090-capi-refusals-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/092-garbled-frame-reject-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/093-inbound-frame-dispositions-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/093-inbound-frame-dispositions-evidence.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/511-test-hooks-odr-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/544-hot-path-zero-alloc-tasks.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/544-hot-path-zero-alloc-evidence.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/536-537-538-recovery-liveness-tasks.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/536-537-538-recovery-liveness-verify.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/b41-563-564-measurement.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/563-564-callback-send-tasks.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/563-564-callback-send-verify.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/098-dialect-overlay-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/099-fixt-appl-version-validation-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/099-fixt-appl-version-validation-evidence.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/101-inbound-reject-fidelity-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/101-inbound-reject-fidelity-evidence.md
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
| FIXT / FIX50SP2, version serviceability | `specs/033-fixt-fix50sp2-session/`, `specs/042-fixt-version-serviceability-guard/`. ⚠️ 033's "1128 tolerated, not routed" and 042's "config-load" are narrowed by 099: [below](#a-fixt-session-reads-each-message-with-that-messages-dictionary-099) |
| Which dictionary answers a FIXT session's inbound frame, what each state does with it | `Session::select_route_`, `Session::bind_fixt_routes_` and `specs/099-fixt-appl-version-validation/data-model.md` E-6, E-7, E-10; the live B&L file's section `## 099`; why and what was rejected: [below](#a-fixt-session-reads-each-message-with-that-messages-dictionary-099) |
| A second `close()` while one is in flight | `Session::close` and its comment in `session.hpp`; `specs/093-inbound-frame-dispositions/plan.md` OD-29. ⚠️ **`.specify/2d-threading.md` §4.7's and §6.5's "Idempotency" bullets and §4.7's code-block comment ("functionally a no-op for the second caller", "without side effects", "returns immediately with no error") are SUPERSEDED for terminal-on-graceful by 093 plan OD-29**: a terminal close that arrives while a graceful one is in flight escalates it, ending its grace wait or keeping its Logout exchange from starting. Each carries a dated erratum in place. ⚠️ **OD-29 is itself narrowed by fixpp#590** (`.specify/563-564-callback-send.md` §2.6, rulings R-8 and R-9; B&L `B-563-5`): a terminal close started inside the `toAdmin` of the graceful close's Logout cuts that Logout and joins without ending the grace wait. `.specify/2d-threading.md` has a document-level amendment for it at its end, and the frozen 093 bundle is flagged in the section on fixpp#563 below. The code-block comment's "returns immediately" did not hold before 093 either: the join awaits the in-flight close. Graceful-on-graceful still joins with no side effect. |
| Recovery and liveness: a frame above the expected number while a resend is outstanding, the deadline of an unfilled gap, a too-high Heartbeat and a pending TestRequest, a resend answer whose store read fails, `retrieve` after a reset, one session's close and the other sessions' liveness | `.specify/536-537-538-recovery-liveness.md` §2 to §4 (fixpp#536, #537, #538; issue mode, no `specs/` bundle); the live B&L file's section `## fixpp#536 / #537 / #538 / #598`; why and what was rejected: [below](#recovery-liveness-and-a-cancellation-inside-the-session-fixpp536--537--538--598). ⚠️ **013's `spec.md` FR-009, a queue of inbound frames above the expected number during recovery, was never built and is now rejected** (the note's §4.4): a frame above NextNumIn while a resend is outstanding is ignored, and the peer's replay carries it (`B-537-1`). `ResendState::inbound_held` has no writer, and `L-013-2`, which said frames are held there, is corrected. 013 is frozen and not edited. |
| A cancellation met inside the session: while an inbound frame is processed, at an outbound store call, at the graceful close's Logout, inside a `FileStore` call's offload | the note's §9, §10 and §11 (rulings R-14 to R-17, R-24 to R-27); B&L `B-538-4` to `B-538-7`, `B-598-1`, `B-598-2`, `L-598-1`; [below](#recovery-liveness-and-a-cancellation-inside-the-session-fixpp536--537--538--598) |
| The outbound store-then-transmit step (`store_then_emit`): what a store call that did not store the frame ends as | `Session::store_then_emit` and its header comment; `specs/059-outbound-store-fail-closed/` for a failed write on a persistent store (B&L `B-059-1`); the note's §10 for a cancelled store call (`B-538-5`). ⚠️ **059's disposition of a cancelled store call is SUPERSEDED by the note's §10 (rulings R-15, R-16)**: on both timings and on any store the frame is not transmitted, its number is taken back, and `store_then_emit` records `Disconnected` and leaves by `operation_aborted`, which `Session::send` turns into `dispatch_aborted`. Superseded, the returned timing (059's D7, "absorb → proceed", "shutdown-only"): `research.md`'s grounded fact "`store_cancelled` is shutdown-only", its D7, and D3 for this route; `contracts/store-then-emit-disposition.md` items 2 and 3 ("except `store_cancelled`") and its error-channel row for a cancelled or drained mutex; `data-model.md`'s disposition table, the row for `store_cancelled`; `plan.md`'s summary, step 1; `tasks.md` T006 (1) and T013 (a); `checklists/disposition.md` CHK002 and CHK012; `quickstart.md` ("only reachable on shutdown drain"). Superseded as mechanism, the thrown timing (059 kept the old catch, which returned `dispatch_aborted` from `store_then_emit` with no state change of its own): `research.md`'s grounded fact on the thrown `operation_aborted` and its D5; the contract's item 5; `data-model.md`'s disposition row for a thrown `operation_aborted` and its invariant INV-059-5 ("no new `Disconnected`/`hydrated_` side effects"); `plan.md`'s "existing `operation_aborted` cancellation handling … preserved verbatim"; the end of `tasks.md` T006. The caller of `Session::send` still gets `dispatch_aborted`. **Not superseded:** `spec.md`'s edge case "Cancellation is not a store failure" and FR-005, which say a cancelled send already disconnects; that now holds on both timings. The bundle is frozen and not edited. |
| Runtime flows | the flow pages below — no design doc carries them |

## Invariants that span features, and why they exist

| Invariant | Why | Disclosed as |
|---|---|---|
| **Outbound sequence numbers are committed to the `MessageStore` BEFORE the frame hits transport** | ⭐ **the ordering is the durability guarantee.** Reverse it and a crash between write and commit yields a peer that saw a message the store never recorded — unrecoverable by resend | `B-005-5` |
| **`seqnum_t` overflow is session-fatal and requires operator intervention** | it **never silently wraps**; a wrap would silently corrupt the resend contract | `B-005-4` |
| **Acceptor sessions stay in `NotConnected` at `open()` and emit no Logon; only initiators do** | the asymmetry is deliberate — an acceptor has no peer to greet yet | `B-009-1`; pairs with the lazy-connect invariant in [`initiator-connect-path`](./initiator-connect-path.md) |
| **A refused first Logon transitions to `Disconnected`, not back to `NotConnected`** | the two states are not interchangeable: `Disconnected` records that an attempt happened and failed | `B-009-2` |
| **The live inbound path accepts header/body fields after the first three in any order** | real counterparties emit them; strictness here buys conformance-theatre and loses interop. ⚠️ *Narrowed by 093 (fixpp#514):* the row used to include "MsgType not first". A frame whose third field is not MsgType(35) is garbled under FIX-SL 2020 §4.5.2 criterion 3 and is disregarded in every state but Disconnected, in both validation modes, before any other check in the arm (contract C-2 step 1) | `B-005-7`, `B-093-3` |
| **Every inbound frame that is neither garbled nor faulty refreshes inbound liveness, at one point** | the owner's ruling R-1 on #516: any such frame proves the peer is alive. One writer, right after the fault and third-field checks and before every early return, so no early return can skip it; a refresh in `LogonReceived` is harmless because both roles seed the value on entering `Active` | `B-093-9`; garbled and faulty frames still do not refresh (`B-092-5`) |
| **Once `close()` has begun, a frame in `NotConnected` or `LogonSent` is acted on no further** (fixpp#523) | those arms run no Logout exchange, so nothing in them needs a frame after close began. `LogonReceived` is deliberately left alone: a graceful close from there runs phase 1's Logout exchange, which must process inbound frames | `B-093-10`, `L-518-1` |
| **`open()` refuses a configured string that an admin builder would copy verbatim, if it holds a byte `< 0x20` or `'='`** — CompIDs, BeginString, each `supported_msg_types[].msg_type`, and the credentials | those values reach the wire through `append_raw` unvalidated, so one SOH injects a field. Every admin builder emits CompIDs and BeginString; `build_logon` adds RefMsgType and the credentials when they are configured. ⭐ **ONE predicate, `fixpp::session::contains_forbidden_config_byte`** (`config_byte_floor.hpp`), also called by the C-ABI setters before any `Session` exists. ⚠️ **It is a POLICY floor, not FIX grammar**: fixpp's scanner splits at the *first* `'='`, so `'='` is legal in a value as fixpp parses it. Do not cite it as the grammar | `B-452-1`; residual `L-452-2` |
| **A Logon arm that resumes after `close()` began, or after `Engine::stop()`'s step 1 ran on its strand, stops where it is** — no further frame, no Active, no `onLogon`, no liveness loop. A 141=Y reset unit always completes, and `close()` waits for it before its teardown reset (*since 093, fixpp#524; before, the unit stopped mid-way once that reset had been issued*) | a Logon arm suspends, and `close()` can run in that window. The guarded suspensions: on the acceptor the hydrate and the `reset_on_logon` reset; in a peer-requested (141=Y) reset the store reset and each restore's persist; the reply write; the NextExpectedMsgSeqNum(789) handling; after `Active`, the persist of the Logon's inbound advance. `close()` can be posted from the application's `toAdmin` or `onLogon` via `Engine::lookup`, or from another thread. A graceful close leaves the FSM in `LogonReceived` while its Logout waits on the write gate the reply holds, so the check is "close began" (`state_ == lifecycle::closing`), not only "the FSM moved". Before fixpp#518 the arm overwrote close's `Disconnected` with `Active`, fired `onLogon` after close began, and spawned a liveness loop that nothing cancels | `B-518-1`; `L-518-1`; `B-093-11` |

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
  ⚠️ **Superseded in part by 098 (`098-dialect-overlay`, FR-022):** the send path now also asks the
  session dictionary. After a miss in the fixed set (`kSendHeaderTags` / `is_send_header_tag`, moved
  unchanged into `src/session/send_header_tags.hpp`), a tag the dictionary declares in its header
  (`table_view::is_header_tag`) is header-class too; a counted Data value still follows its Length.
  The two-table split stands: `kReplayHeaderTags` is unchanged. L-422-1 sits in the closed B&L file;
  the live row is B-098-5.
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
    - *One atomic durable unit* (a store reset-to-values) needs a new `MessageStore` operation, outside the owner's rulings for #518. *(093 shipped exactly this for #524, under the owner's ruling R-3; see the #524 bullet below.)*
    - **What shipped (owner-approved, PR #522 Gate B round 2):** the unit stops mid-way only once `teardown_reset_done_` is set. `close()` sets it just before it awaits the teardown reset and never clears it; no Logon arm runs on a `Session` after its `close()` (L-518-1). Premise: the store applies operations in issue order (L-518-1). ⚠️ **Superseded by 093 (fixpp#524):** the in-unit stops are removed; the unit is one store operation that always completes, and `close()` waits for it. `teardown_reset_done_` survives only as `close()`'s single-fire latch.
  - **A check straight after `onLogon`, before the post-`Active` persist,** was rejected (PR #522, Gate B round 2). By then the peer's Logon has been consumed, answered and made `Active`, so skipping its persist leaves the store one behind the peer, and a peer that next logs on at the following sequence number is too high. The check follows the persist instead, before the liveness loop is spawned.
  - **A check after the 141=Y reset event** was rejected (PR #522, Gate B round 2). On the initiator it let the reset event, the CompID authorization and the FIXT version record run after `close()` began; the acceptor's reset event had the same order. The check follows the last restore, before the event.
  - ⚠️ **Rule: a `co_await` in a Logon arm that can yield is followed by the check before the next frame, callback or state write**, including the suspensions inside the coroutines the arm awaits (093 plan OD-25, OD-26: the 789 helpers, `store_then_emit` before its transmit, and every `Disconnected` write after a store suspension, through `disconnect_unless_superseded_`). *(The rule's second sentence, on `teardown_reset_done_` inside a 141=Y reset unit, is superseded by 093: the unit no longer stops mid-way.)*
- **The 141=Y reset unit, made one store operation** (fixpp#524, 093; owner ruling R-3: a new `MessageStore` virtual with a default body, overridden atomically by `MemoryStore` and `FileStore`; owner ruling "`close()` waits, with a timeout").
  - **`reset_to(next_in, next_out)` takes targets in {1, 2} only** (research R-6, G93-A-16): `next_seqnum` advances by one, so a general target would need an O(n) loop in the default body.
  - **The manager is set first, inline, then the store's one `reset_to`.** That closes the drain residual (L-518-1) on the uncontended-grant condition. The true targets go to volatile stores too (OD-9); that was expected to fix #538 as a side effect, and measured not to (T089): #538 reproduces on the base and at 093's head, and stays open in B28.
  - **The store operation runs on its own awaitable thread, bound to an empty cancellation slot** (OD-25, OD-26). OD-14's in-place `reset_cancellation_state(disable)` shield was **wrong, and was replaced**: `async_mutex::async_lock()` resets the awaitable thread's state to terminal-only after every acquisition (and enables total cancellation during a contended wait), so after the manager set's first lock the disable had already been replaced, and a deleting mutant stayed GREEN on every cell. The separate thread is what keeps `Engine::stop()`'s total emission out (mutant "run it inline" is RED); the empty slot extends that to every emission kind (mutant "drop the binding" survives, priced). Rejected: an `async_mutex` mode that respects the caller's policy (asio exposes no filter getter, user and default-body stores would not get it, and it changes a core primitive); spawning the whole unit (`co_spawn`'s entry dispatch would make the manager set's no-suspension property depend on the launch path); disclosing the cancellation window instead.
  - **An engine-stop flag on the Session** (OD-15), set by `Engine::stop()`'s step 1 on the session's strand before it emits. It has two readers, both on that strand: `logon_arm_superseded_`, and since fixpp#563 (its OD-3) the body that `Engine::send` starts for a published session, which refuses a send that starts after step 1. Rejected: calling `close()` from stop's step 1 (it reorders the teardown that the join and the registry steps depend on); an atomic latch set on the control strand and read with acquire (same check-then-act gap; only running on the session strand gives the mutual exclusion). The guarantee is strand-ordered, not real-time.
  - **Every suspension-then-effect window in the Logon arms is checked** (OD-26), found by re-deriving the predicate sites transitively. Rejected: disclosing the error-path `Disconnected` writes as an L-row; the guarantee is a MUST and one helper covers them.
  - **`close()`'s wait is bounded by `logon_timeout_ms`, event-driven** (OD-1). Rejected: `logout_disconnect_timeout_ms` (nothing validates it and TOML accepts 0, which would expire the wait at once); a `co_await asio::post` poll loop, which spins the strand for the whole bound. After expiry a default-body store may miss (1, 1) (`L-093-4`).
  - Not BREAKING, under B-518-1's owner ruling (#523 and #524 are its follow-ups).
  - ⚠️ **Passages in 093's own bundle that still describe the replaced shield**, flagged here and not
    edited (each is a point-in-time record; the code follows the C-6 erratum and plan OD-25/OD-26):
    contract C-6's unit steps 2 and 5 and its "What the shield means" paragraph (the erratum at the top
    of C-6 says to read "the shield" as "the empty-slot store operation"; step 2's disable is removed,
    step 5's restore stays); research R-9's "Decision" and "The `co_spawn` alternative, re-judged"
    bullets, which chose the in-place shield and rejected the empty-slot `co_spawn` that shipped (R-9's
    erratum says so); plan OD-14, marked SUPERSEDED there; and spec FR-042's "With the shield". Do not
    put the disable back to match them: a deleting mutant stayed GREEN on every cell, because
    `async_lock()` replaces it.

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
  [`inbound-message-path`](./inbound-message-path.md). **Not 092's, and since settled by 093:**
  - a true §4.5.2 failure detected by the Framer ended the session (`L-004-4`); since 093 (fixpp#514) it
    is disregarded and counted (`B-093-1`), and `L-004-4` is in the closed B&L file;
  - a late parse failure closed the session, and effects taken before the close stand; since 093
    (fixpp#515) no admitted frame reaches it, and the close is a defence (`L-092-6`).

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
  - **CI rejects it** (fixpp#530): `ci/odr-hooks-census.py` runs on Tier 1's `linux-clang-release`
    leg, and `ci/test-odr-hooks-census.sh` pins its rules in `ci-script-pins`. It fails a class,
    struct, union or enum definition whose head it recognises and whose tokens differ between the two
    states, in `*.hpp`, `*.h`, `*.inl` and `*.ipp` files; the owner ruled enums and the fragment
    suffixes in scope at Gate B (2026-10-02). What it does not fail, a gated statement in a function
    body outside a class among them, is `L-530-1`. Rejected homes: a ctest, which would run
    on every leg including MSVC's `cl` flags; a `ci-script-pins` step, since that job has no configured
    tree; and `co-spawn-closure-audit`, which is not in `tier1-required`.
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

## Allocation on the hot path — the session strand and the send slot (fixpp#544)

The design is `.specify/544-hot-path-zero-alloc.md` (issue mode: that note is the plan; §2.1 and §2.5,
plus its "As built" section). The behaviour a user must know is B&L `B-544-1`…`B-544-5`, `L-544-1`
and `L-544-2`; what stays open is `L-497-1`. `L-544-3` and `L-544-4` are in the closed B&L file
since fixpp#563 and fixpp#564 (the next section). Re-derive the shape from source, not from here.

- **The session strand keeps the concrete executor type.** `make_session_strand`
  (`src/session/session_strand.cpp`) is the one construction point. When the Engine's executor's target
  type is exactly `io_context::executor_type`, it stores `strand<session_inner_executor_t>` in an
  `any_io_executor`; otherwise it falls back to a strand over the type-erased executor, as before. The
  test is exact target type, so a work-tracked or custom-allocator `io_context` executor takes the
  fallback. ⚠️ `.specify/2d-threading.md`'s "the engine never picks a concrete executor" is annotated by
  a document-level amendment at that file's END; read it before citing the sentence.
  ⚠️ Also written before this and not updated: `specs/007-threading-clock/contracts/session_executor.hpp`
  ("Holds the resolved inner executor — `asio::strand<asio::any_io_executor>`") and
  `specs/023-engine-session-strand/data-model.md` (`session_strand = make_strand(exec_)`). On the fast path
  the strand's target is `strand<session_inner_executor_t>`, and `SessionEntry::session_strand`'s type
  changed (B&L `B-544-2`).
  Why a per-session strand exists at all is in two parent-repo convergence records, both **current**:
  `decisions/2d-threading.md` (the default per-session strand and the `session_executor` wrapper over
  `any_io_executor`; the strand's inner type is not in it) and
  `decisions/speckit/023-engine-session-strand-gatea.md` (the engine control strand beside the
  per-session strand, and the rejected one-domain design). The 2d record's `close()` summary predates
  093's OD-29; the erratum is in `.specify/2d-threading.md` (the close row above).
- **`send_impl` no longer carries its buffers in its frame.** The `Session` owns two build-scratch
  buffers, a frame slot and a slot flag (`send_strip_scratch_`, `send_body_scratch_`,
  `send_frame_slot_`, `send_slot_in_use_` in `session.hpp`). The build is a non-coroutine helper,
  `build_send_frame_`, which uses the scratch without the flag because it neither suspends nor calls an
  Application callback. `Session::send` tests the flag through an RAII holder declared inside its `try`;
  a send that finds it held takes `send_fallback_leaf_`, whose frame owns its own buffer and so
  allocates.
- **Why the existing serialisation could not carry one session-owned buffer.** The strand serialises
  handlers only between suspensions, and a send suspends after its frame is built wherever its store
  or its write yields (the store's leading post, the seqnum lock, the write gate, the write). On a
  session with no store and a synchronous sink it does not suspend at all. So two sends can overlap:
  concurrent `Engine::send` callers, a consumer `co_spawn`ing `Session::send` without awaiting, and a
  send started from inside `toApp`. That last one ran inline when this was decided (the note's probe
  r3); since fixpp#563 it starts after `toApp` has returned, while the outer send may still be
  suspended and holding the slot (the next section). `write_gate_` covers only the write and is taken
  after the store.
- **Rejected, and why** (owner ruling R-10; §2.5):
  - **a session send gate** (an `async_mutex` held from before the build until `store_then_emit`
    returns): sends would queue behind each other's store and write, the gate would have to join
    `close()`'s and `Engine::stop`'s drain, the `app_callback_threw` path awaits `close()` inside
    `send_impl`, and a pending cancellation would throw earlier than today, turning a validation error
    or a `toApp` veto into a disconnect;
  - **`write_gate_` taken early**: `async_mutex` records no owner, so `store_then_emit` locking it again
    would wait on itself;
  - **a per-send leaf that owns the buffer**: that is the over-limit frame itself;
  - **a pool of K slots**: the number of concurrent sends has no bound, so a pool still needs the
    fallback.
- **A new caller condition (`L-544-2`).** The slot holder writes `Session` memory when the send's frame
  ends, so a direct `Session::send` caller must keep the `Session` alive until that frame has completed
  or been destroyed. `Engine::send` does.
- **Two pre-existing defects this work surfaced and did not fix:** a send nested in `toApp` could
  reuse the outer send's MsgSeqNum (`L-544-3`, fixpp#563), and a send from inside an application
  callback tripped `callback_dispatch_scope`'s `assert` without `NDEBUG` (`L-544-4`, fixpp#564). Both
  are fixed by the decision of the next section, and both rows are in the closed B&L file.
- The inbound side of the same work, the dispatcher split and its leaves, is on
  [`inbound-message-path`](./inbound-message-path.md).

## A send or a close started inside an application callback (fixpp#563, fixpp#564)

The design is `.specify/563-564-callback-send.md` (issue mode: that note is the plan; its §0 has the
owner's rulings, §5 what was rejected, §7 what it does not decide). The behaviour a user must know is
B&L `B-563-1`…`B-563-5`, `B-564-1`, `L-563-1`…`L-563-5` and `L-563-7`, with `L-007-2` widened. The
measurements
are in the parent repository, `decisions/speckit/b41-563-564-measurement.md`. Re-derive the shape from
source, not from here.

**The one condition** (note §1): no emit body on a session may begin while that session has `toApp` or
`toAdmin` on the stack. An emit is one synchronous stretch of a strand handler, and the only way into
it through an `Application` callback is the callback the stretch itself calls.

**The rulings, each with where it lives.**

- **R-1, `Session::send`.** It waits, by one post onto the session's executor, only while the session
  has `toApp` or `toAdmin` on the stack; in `fromApp`, `fromAdmin` and `onLogon` it runs inline, as in
  the three QuickFIX engines. The test is a strand-confined depth written in one place, the stack-arena
  overload of `parse_and_dispatch_`, which is the one route to `toApp` and `toAdmin`. The deferred path
  is its own coroutine, `send_deferred_`: it copies the payload, posts, applies asio's cancellation
  test by hand, and refuses the send once a `close()` has begun.
- **R-2, `Engine::send`.** A published session is reached through the reader snapshot, with no hop
  through the control strand, so a call from a callback of that session nests by construction and no
  longer depends on the control strand being idle. The control-strand route stays as the miss path.
  The control-strand read of the stop flag is replaced on the hit path by the session's engine-stop
  flag, read on the session's strand (OD-3). ⚠️ This supersedes 023's two-hop send; the flags below
  name the frozen passages that still describe it.
- **R-3.** The session closed from inside one of its own callbacks was to be measured under a memory
  checker; note §2.5 has why that decides nothing about lifetime, and what the sanitizer runs are kept
  for.
- **R-4, `Session::close`.** A close in either mode, started while any callback of the session is on
  the stack, begins after the callback returns: one post in `close()` itself, after its cancellation
  reset and before its three-state test. The owner fixed its reading after Gate A round 1: the rule is
  "after the callback returns", not "after the operation that called the callback has finished", and
  `L-563-1` is what that costs.
- **R-5.** `Engine::stop()` started from a callback is out of this decision; `L-563-4`, follow-up
  fixpp#579.
- **R-6.** "Calls to `store()` complete in the order they were made" is a requirement of
  `MessageStore::store`, not a disclosed limitation of send ordering (`B-563-4`).
  ⚠️ **Narrowed by B28** (`.specify/536-537-538-recovery-liveness.md` §10, D-OUT-7; ruling R-21;
  B&L `B-538-3`), and with it `.specify/563-564-callback-send.md` §0 (R-6), §2.1 and §4, and
  `.specify/2e-msgstore.md` Appendix Z2 §1 (B28's erratum at that file's end), which state it
  unconditionally:
  calls to `store()` that a cancellation does not end complete in the order they were made;
  a call that a cancellation ended may return earlier. On `FileStore`, a call queued at the
  store's lock that a cancellation ends returns `store_cancelled` while the call ahead of it
  still holds the lock.
- **R-7.** A callback must not run the session's own executor. The waits of R-1 and R-4 are one post
  and do not hold there; the case is outside the contract, as a false `direct_executor` attestation
  already was (`L-007-2`).
- **OD-5.** The route has a bench in the same change, `bench/session/engine_send_bench.cpp`; the note's
  §0 and §3 ("Cost") have the ruling on its manifest row and what it replaces.
- **OD-6, withdrawn.** No C-ABI change is declared. The first ruling ("declared breaking") rested on a
  sentence that reading and then a measurement showed false: outside R-5's route, the new stop check
  refuses no send that the control-strand read admitted (note §2.4).
- **R-8, one ending for a terminal close started inside the `toAdmin` of a graceful close's
  Logout** (note §2.6; fixpp#590; `B-563-5`). The Logout is cut: numbered, stored where the
  session has a store, and not transmitted, which is QuickFIX's ending. The test is a count of
  the terminal closes that have taken R-4's wait, read by `run_logout_phase1` before the
  Logout's `toAdmin` and again when it has returned. Where the count moved, the step writes
  `Disconnected` at once, so `onLogout` runs before the Logout takes its number, stores the
  Logout through `store_then_emit` with its transmit switched off, and returns without
  `LogoutSent` and without a wait.
- **R-9, the bound.** ⚠️ This narrows 093's plan OD-29 on that route. A terminal close that joins
  a graceful close whose Logout was cut sets the latch and does not call the grace timer's wake,
  so nothing cancels the cut Logout's store call before the grace timer expires. Where that call
  is still waiting for the store's lock then, the timer ends the join and nothing is stored for
  the Logout. Note §2.6, "The bound", is the one statement of it, with what the timer does not
  end (`L-563-7` is one of those).
- **R-10.** With `reset_on_disconnect` set the route keeps fixpp's teardown reset, which runs
  after the Logout step and empties the store. QuickFIX C++'s order differs; the difference is
  stated in `B-563-5` and not removed.

**The debug guard** (note §2.2) changed with it, because the old one aborted on the nesting R-1 makes
legal. `in_dispatch_` is gone. `callback_dispatch_scope` now carries an atomic owner token (a second
thread still asserts; a nested entry on the owner's thread passes) and maintains the depth of all
callbacks in every build type. What the old assertion also protected is asserted where it begins: at
each parse site over the session's parse buffer, in the stack-arena overload, at the `onLogon` and
`onLogout` sites, at the deferred send's resumption and at the waiting close's. `B-564-1` has the
nestings that are no longer detected.

**What was rejected** (note §5 has each with its reason; these are the ones a later change is most
likely to propose again).

- **Defer a send from every callback.** One rule, and sound. Rejected by R-1: in `fromApp` it moves the
  reply's `toApp` and its numbering out of the callback, which a ported application observes.
- **Let the send nest in `toApp` and repair each emit site.** Restamping sends a number the callback
  did not inspect; assigning first makes a veto consume a number; neither restores order at the
  assign-first sites.
- **A session send gate, or ordered emission inside `Session`.** fixpp#544's ruling R-10 already
  rejected the gate (the section above); tickets or a completion queue are the same queuing.
- **Reuse the debug flag as R-1's predicate.** It was set for inbound callbacks too.
- **Leave a terminal `close()` inline**, with a check after each inbound callback, or with the arm
  left to carry on as QuickFIX C++ and J do. Rejected by R-4.
- **R-4 read as "after the operation that called the callback has completed".** It needs a mechanism
  at every operation entry, and a terminal close would wait on the I/O it exists to interrupt.
- **A "close requested" mark tested at the next frame.** Not taken; the note keeps it available if
  `L-563-1`'s fourth condition proves unacceptable in use.
- **`Engine::send`: document "nested or deferred", or always defer by a post.** Rejected by R-2.
- **Put every registered id in the snapshot**, to drop the miss path: a republish per registration.
- **A callback-exit latch, or a run-time refusal,** for a callback that runs the session's executor.
  Rejected by R-7.
- **An atomic depth read by the guard and by `close()`.** An atomic read-modify-write on every
  callback of every build type, for a reader that is already on the strand.
- **A lifetime rule for the payload instead of the copy.** The same application code would be correct
  in `fromApp` and undefined in `toApp`.
- **Refuse a send started in the confirming Logout's `toAdmin`** of a logout the peer started. Ruled,
  then not implemented: the source reading showed that no QuickFIX engine refuses it (`L-563-2`).

For R-8 and R-9 (note §5, its last block, which names the prototype of each):

- **Merge with the split stated** (on a `FileStore` with a live transport, timing decided whether
  the Logout was transmitted). Rejected by R-8.
- **Skip the Logout's transmit and change nothing else.** The step would record a Logout as sent
  that never left, and with `reset_on_logout` set the teardown reset would fire against a peer
  that saw no Logout.
- **Keep the cut step in its wait and let `close()`'s join end it.** The join cancels the step
  once, and the stores' lock sequence clears a cancellation that arrives while the Logout's
  store call is queued at the lock; the step then waits out the logout timeout.
- **Write `Disconnected` after the cut Logout's store call.** The session stays Active during
  that call, and what arrives then is acted on after the application's terminal close.
- **A separate state or gate that keeps arrivals out once the cut is decided.** Not needed: the
  state the session already has, `Disconnected`, is written at the decision point.
- **Let the joining terminal close cancel the grace timer for a cut Logout as well.** The join
  would then cancel the Logout's store call, and "stored" would depend on whether the store's
  lock is free at that instant.
- **Never cancel the cut Logout's store call, not even at the grace timer's expiry.** Rejected by
  R-9: a terminal close and `Engine::stop()` would wait for the store however long it takes.
- **No direct close of the transport on the cut route,** for a clean TLS shutdown. Not taken
  here: it is one question for every escalation, tracked in fixpp#596.
- **A yield after `toAdmin` and then a read of the escalation's flag, or that flag set inside the
  callback.** The first makes the decision an order between two posts; the second makes `close()`
  act on another close before its wait, against R-4.
- **Transmit on every arm,** by letting the Logout's store call and write finish before the
  escalation takes effect. It is the opposite of QuickFIX's ending, and a write that hangs would
  hold a terminal close.
- **Match QuickFIX C++'s order of a reset on disconnect.** Rejected by R-10.

**⚠️ Frozen bundles that still describe the superseded behaviour.** `specs/<id>/` bundles are
point-in-time records and are not edited. Each flag is on the bundle and covers its plan, tasks,
checklists and quickstart, which repeat the same statements. Re-derive the list with the last search
of the note's §4 ("The searches", the families over `specs/`).

- **`specs/019-app-callbacks`:** FR-006, its "Re-entrant send from within a callback" edge case, and
  research D6 say that a send from inside an on-strand callback is enqueued behind the current
  dispatch. It is not: R-1 and R-2 above. The owner's principle (note §0) makes that clause history,
  not a constraint.
- **`specs/023-engine-session-strand`:** FR-012, contract C-0's "every cross-thread entry point enters
  the control-plane domain", C-2's "never an inline `dispatch`-that-runs", data-model E-0 and research
  D0's send route describe the two-hop send. For a published session `Engine::send` no longer enters
  the control-plane domain, and its body starts inline when the caller is already on the session's
  strand. 023's research weighed "Lock-free registry snapshot for `send`" and deferred it as a
  possible optimisation, to be revisited only if the bench gate failed; R-2 adopts it for a different
  reason, the timing dependence of a send started inside a callback.
- **`specs/093-inbound-frame-dispositions`:** contract C-3 I-3, and the research line that "the
  callback scope asserts against nesting". I-3 still holds: no inbound parse nests inside another.
  The carrier of its assertion changed, from the callback scope to the depth assertion at the parse
  sites. Its plan's OD-29 and contract C-6's note on `close()` say that a terminal close that
  arrives while a graceful one is in flight ends its grace wait. For a Logout that was cut it
  does not (R-8 and R-9 above). Re-derive the passages with
  `command grep -rn 'OD-29' specs/093-inbound-frame-dispositions`.

Also superseded in place, by an erratum at the END of each file: `.specify/2d-threading.md`
("Non-reentrant from inside a callback", and the `direct_executor` attestation), `.specify/2e-msgstore.md`
(the order requirement, and "contention is zero") and `.specify/544-hot-path-zero-alloc.md` (a send
started in `toApp` "runs inline"; `Engine::send`'s two hops).

**Compatibility** (note §4, "Compatibility").

- `Session`'s layout changes: two depths, and the owner token in place of `in_dispatch_`. A C++ layout
  change before the first release. `Session::send` keeps its signature.
- `MessageStore::store` gains a documented requirement (R-6). No signature changes.
- The threading contract gains a sentence (R-7). It states, for a case no sentence named, what
  `L-007-2` already made undefined for a false attestation.
- The C ABI's symbols, types and error codes do not change, and no call that succeeds before this
  change fails after it. A send that races `fixpp_engine_destroy` from another thread had an outcome
  that depended on timing and still has; the window in which it is admitted is wider (`B-563-3`).
- No version bump and no BREAKING clause.
- One change of behaviour that follows from R-4's wait: a terminal close started in the `toAdmin`
  or the `onLogout` of a graceful close now ends it without the wait for the peer's Logout, where
  it could run out the logout timeout (`B-563-2`). From `onLogout` it escalates the close. From
  `toAdmin` it cuts the close's Logout, on every store and transport: the Logout is numbered,
  stored where there is a store, and not transmitted, where before it was transmitted (R-8;
  `B-563-5`).
- `Session`'s layout grows by two more members, the count and the record of the cut.

**Not decided here** (note §7): a live send that interleaves with a resend replay (fixpp#588);
`Engine::stop()` started from a callback (fixpp#579); one ending for an allocation failure while a
send starts (fixpp#587); the census matcher's blind spot found while the cells were written (fixpp#586).
Beside R-8's route: a terminal close that begins after the Logout's `toAdmin` has returned, where
timing still decides whether the Logout is transmitted (fixpp#594); a clean TLS shutdown for every
escalation (fixpp#596); what a completed `close()` promises about another coroutine's store job
(fixpp#597). A `FileStore::store` call that reported success for a frame it did not write
(fixpp#598) is decided by B28 (the next section; `B-598-1`).

## Recovery, liveness, and a cancellation inside the session (fixpp#536 / #537 / #538 / #598)

The design record is `.specify/536-537-538-recovery-liveness.md` (issue mode: no `specs/` bundle; the
owner's rulings R-1 to R-27 are its §0). What the code does is in the live B&L file's section
`## fixpp#536 / #537 / #538 / #598`; this section keeps what that file does not: why, and what was not
taken. Each item names the note's section that has the whole list.

- **`retrieve` after a reset (#538, §2.3).** Both stores look a number up relative to the first frame
  the direction retains (`B-538-1`). Not taken: a stored base (it can disagree with the entries, and it
  would change `MemoryStore`'s layout or `FileStore`'s format version, which old and new builds would
  then refuse); a search by sequence number (it would also serve a hole the session cannot make);
  keeping (1, 1) and storing the Logon again (it changes 093's reset unit to work around a lookup).
- **A store read that fails in a resend answer (#538, §2.3, rulings R-7, R-10, R-13).** The answer is
  aborted with the store's own error (`B-538-2`). Not taken: gap-filling the unreadable slot and
  recording an event (a read error can be transient; a GapFill is permanent on the wire); aborting only
  on a persistent store; aborting on slot 0 (no frame can exist there, and any peer could end a
  session with one frame); keeping the GapFill for a cancelled read (measured: the peer read a GapFill
  over stored messages); clamping the walk's start to 1 (a wire change no issue asked for; fixpp#580).
- **One session's close and the others' liveness (#536, §3.3).** No session sweeps the clock
  (`B-536-1`). Not taken: `close` emitting terminal or `all` (it would also cancel a liveness emit in
  flight); every sleeper re-arming on a sweep that is not its own (the issue's second option: it would
  have to hold at three sites, breaks the test teardown helper, and a clock whose sweep does nothing
  would never deliver the logout wake); a per-session cancellation on the `Clock` interface (a pure
  virtual on a roster interface is a listed breaking change); session identity from the clock's slot
  map. Why the loop resets its own filter: the lock sequence leaves every coroutine thread with a
  terminal filter, so a loop that had sent a frame no longer accepted `close`'s total cancellation;
  whether the lock should restore its caller's filter is fixpp#583.
- **A second too-high frame and an unfilled gap (#537, §4.4, rulings R-2, R-6, R-9, R-11, R-12).** The
  frame is ignored and the gap has a deadline (`B-537-1`, `B-537-2`). Not taken: holding the frame,
  as 013 FR-009 and the three QuickFIX engines do (it needs a capacity policy, an order for held
  administrative frames and a delivery path, and gains nothing on the wire while the request runs to
  0); a second ResendRequest per extra frame; no longer refreshing liveness on a too-high frame (it
  reverses ruling R-1 on fixpp#516); no deadline; a timer for the deadline; two endings for a live
  peer; the grace check reading "any frame since the TestRequest" (it edits the liveness loop, B27's
  surface); a Logout on the dead-peer ending; the check ahead of the BeginString, CompID and
  SendingTime guards; the check without its own too-high test (it would end a healthy slow replay); the
  check on every frame that does not advance (it would close `L-537-1`'s pattern; not chosen, R-11);
  a Logout with no text where the text does not fit (one more branch; kept by R-12).
- **A cancellation met while an inbound frame is processed (§9.4, ruling R-14).** One catch at the
  one coroutine every frame enters (`B-538-4`). Not taken: making `on_inbound_frame` a coroutine with
  the `try` in it (one frame deeper on every inbound frame than fixpp#544's target, and no gate counts
  that depth); a `try` at each production caller (every other caller would still get the throw); a
  handler for `operation_aborted` only; the catch without the reset of the caller's state (measured:
  the pump still ended by exception); `throw_if_cancelled(false)` in place of the reset; a conversion
  at each store call; `dispatch_aborted` for a cancellation.
- **A cancelled outbound store call (§10.3, rulings R-15, R-16, R-19 to R-23).** Not transmitted, the
  number taken back, the session ended (`B-538-5`). Not taken: a returned code from `store_then_emit`
  with each discarding caller changed to return it (a list of sites; the exception needs no caller to
  cooperate); a returned code with the caller's state reset inside `store_then_emit` (a later reply of
  the same arm could go out); 059's reconcile read for the number; leaving the number consumed; the
  session staying up, as in QuickFIX (R-16); keeping 059's absorb for `Engine::stop`. The two
  boundaries: the transmit step is fixpp#584 (R-18), and a cancellation that reaches `drive_reconnect`
  before the Logon emission is fixpp#585 (R-22).
- **The store's offload (#598, §11.4, rulings R-25 to R-27).** Once a `FileStore` call holds the
  store's lock, no cancellation reaches its job (`B-598-1`). Not taken: withdrawing the job so the
  call returns at the cancellation (asio cannot remove a posted job, and the job holds a descriptor and
  a span into the store's buffer); the line at the moment a pool thread starts the job (R-26's other
  option: it keeps the split decided by which pool thread runs first, and the data race); the same
  line drawn with a flag of the store's own; a handler with a true meaning in place of the deleted ones
  (no input reaches it, so nothing can hold it true); the handlers left as they were; making the whole
  store call uncancellable (it reverses R-15 for a call queued at the lock); undoing a completed write;
  repairing a damaged log at open (R-27; `L-598-1`).

## A FIXT session reads each message with that message's dictionary (099)

The design is `specs/099-fixt-appl-version-validation/`: research R-1, R-3, R-20 and R-5 give the half kept here, plan.md's `## Owner sign-off` the rulings, data-model E-7 and E-10 the endings per state and input. The behaviour a user must know is B&L `B-099-1` to `B-099-28`, `L-099-1` to `L-099-4`. Read `Session::select_route_` rather than this page. The dictionary half is on [`dictionary.md`](./dictionary.md), the surfaces on [`c-api.md`](./c-api.md), [`python-api.md`](./python-api.md) and [`config.md`](./config.md).

**What was decided.**

- **Two dictionaries, chosen per frame** (R-1, owner ruling 2), as QuickFIX C++ and J split them: a transport dictionary for admin messages, an application dictionary for application messages. The session dictionary answers its own version, so an overlay survives.
- **A route is one table** (R-20): the frame scan, the validator and the parse that builds the view all take it, because a clone or `reify` re-parses from the owner's table and must split as the view did (fixpp#426).
- **One shared set per Engine, built in `Engine::start()`** (R-3): `fixt_route_set`, in the session library, held through a `shared_ptr` by the Engine and each FIXT session.
- **The peek walks with the session dictionary's hooks and stops at the header's end** (R-5), because the scan's own hooks are what the route decides. It is `peek_fixt_route`.
- **A refusal, never an unchecked delivery** (owner ruling 1). This departs from QuickFIX C++ on purpose.

**What was rejected, and why.**

- **Replacing the session dictionary with the registry's for the same version** (R-1): it loses the overlay. This is the rule L-098-1 carried for this feature.
- **Choosing "own version" by comparing to `default_appl_ver_id` or to the negotiated version** (R-1): a FIXT11 session dictionary has no version, and the negotiated version is not the session dictionary's.
- **The registry owning validators, as Gate A's first review proposed** (R-3): `[arch §2.3]` lets `dictionary` include `core` only, and `tools/check_layers.py` enforces that on `src/dictionary/`. The CMake targets would not have stopped it, since the two link each other as a permitted archive cycle.
- **The registry owning only tables, published by compare-exchange** (R-3): a heap cache in a `noexcept` constructor, null-cache answers, deleted move assignment and racing walks, all for a case a build in `Engine::start()` removes, and each table held twice.
- **A `shared_ptr`-holding validator** (R-3): an indirection on every FIX 4.x validation, where 098 needed an owner ruling for a 24-byte layout change.
- **Building in the Engine constructor** (R-3): a walk for engines that never use it, and no way to return an error. **A warm step in `register_session`** (R-3): one walk per call against entries still arriving, and no answer for a directly constructed session.
- **`std::once_flag` or a mutex in `open()`** (R-3): a strand thread blocks inside a coroutine on another's walk. **Per-session tables** (R-3): sessions times dictionaries, the shape NFR-003 forbids.
- **Composing two dictionaries into one `dict_hooks`** (R-20): `offset_table.hpp`'s nested cache keys on `dict_hooks::opaque_dict()`, a function of one dictionary pointer, and a clone or `reify` re-parses from the owner's table, so it would lose the second dictionary and split differently from the view. The overlay's header pair is registered into the route's table instead, before the seal.
- **Selecting the table from the frame scan** (R-5): `scan_frame_header`'s hooks are chosen by the table, so a table-specific pair changes how it splits. **Peeking with `dict_hooks::none()`** (R-5, measured): a 1128 inside a header pair's Data is read as the header's.
- **Leaving an application frame in an establishment arm to `interpret_logon`** (R-8, as that section first recommended): `interpret_logon` reads the last 35, so a frame `35=D … 35=A` with Logon fields would establish a validating session. Neither review caught it; a measurement did. A non-validating session still does this (`B-099-4`, fixpp#657). ⚠️ **Superseded by 101 (research D4, D6):** `interpret_logon` reads the first 35, and every session, validating or not, refuses a frame whose MsgTypes differ in an establishment arm (`B-099-3`, `B-101-8`). The rejection itself stands.
- **A session lookup from `fromApp`** (R-10): blocking in C, and no handle. **Reading the table's version** (R-10): with validation off and no dictionary the table is the session dictionary's, which is not the resolved version. The answer is one byte on the view, `resolved_application_version()`.

**Records this supersedes, flagged here and not edited** (frozen `specs/` are not rewritten; each is a point-in-time record).

- ⚠️ **Superseded by 099:** `specs/033-fixt-fix50sp2-session/spec.md`'s clarification that per-message 1128 routing is deferred, and `plan.md`'s "tolerated, not routed". L-033-1 sits in the closed B&L file. Its tolerance now holds only without validation (`W8` in `tests/session/test_fixt_logon_establishment.cpp`), and L-033-3's "never refuses on any value" is narrowed in place to a non-validating initiator.
- ⚠️ **Superseded by 099:** `specs/041-validation-gate-wiring/spec.md`'s "Phase-1 validates against the session-held dictionary only" and its out-of-scope entry for FIXT two-dictionary validation. L-041-2 sits in the closed B&L file; `B-041-1` carries the condition.
- ⚠️ **Narrowed by 099:** `specs/042-fixt-version-serviceability-guard/` and catalogue row S-043 say a misconfigured default "fails closed at config-load". That holds for a directly constructed session and for a validating one at `Engine::register_session`; a non-validating session on an Engine route is logged, not told (`L-099-4`). The closed `L-033-5` row is corrected in place.
- ⚠️ **Superseded by 099 (fixpp#651):** `specs/007-threading-clock/`'s FR-016, I-13 and `contracts/session_config.hpp`, and `.specify/2d-threading.md`'s "Engine-anchor + session-override pattern", all claim `resolved = override.value_or(engine_anchor)` for a dictionary axis. It never held for the dictionary: it has no engine anchor and is required on the session. Re-derive with `git grep -n 'value_or(engine_anchor)' -- .specify specs`.
- ⚠️ **Superseded by 099:** `specs/015-runtime-engine/`'s "`start()` non-blocking" (spec, `contracts/engine_api.md`, `contracts/realized-behavior.md`, `data-model.md`, `research.md`, `plan.md`, `quickstart.md`). On an engine with a FIXT session it blocks for the walks (`B-099-21`). Re-derive with `git grep -n -i -e 'non-blocking' -e 'does NOT block' -- specs/015-runtime-engine`.
- ⚠️ **Superseded by 099:** `specs/095-tail-evidence-pass/contracts/live-cell-contract.md` and `spec.md`: the live cell's `arm="validation-off"`. The two cells now run validation on, with a peer-to-fixpp step (`B-095-1`).
- ⚠️ **Superseded by 099:** `specs/097-fix-latest-applverid-10/spec.md`'s "validation still uses the session's own dictionary" and "decided by the session's own dictionary, as for every tag (L-041-2)". `B-097-1`, `B-097-3` and `B-097-4` are corrected; L-097-1 sits in the closed B&L file.
- `.specify/2b-wire.md`, `.specify/2c-codegen.md`, `.specify/2i-capi.md`, `.specify/api-contract.md` and `.specify/architecture.md` carry in-place `Updated (099-fixt-appl-version-validation)` or `Since 099` notes; the text before each note is history. Re-derive with `git grep -n '099-fixt-appl-version-validation' -- .specify`.
- **Not superseded:** `specs/098-dialect-overlay/spec.md`'s rule that the session dictionary keeps its own version. 099 keeps it, and `L-098-1` now says how. `specs/081-strict-validation-residuals/`'s framing surface stands and is extended to FIX 4.x, and its accept-only divergence stays as `B-081-1`.

**Decided elsewhere, left open by 099.** `interpret_logon` reading the last 35 without validation (fixpp#657); a non-FIXT validating session's MsgType split (fixpp#658, byte-identical here by SC-005) ⚠️ *(both since decided by 101, the next section)*; a misspelt `[dictionary]` sub-key (fixpp#659); a failed allocation inside `make_strand` while creating an engine (fixpp#660, `L-660-1`).

## One MsgType per frame, QuickFIX's reject reasons, and a reply that is never lost to its echo (101)

The design is `specs/101-inbound-reject-fidelity/`: research D4 to D7 decide the session half, contracts/reject-frames.md gives the endings, and the behaviour a user must know is B&L `B-101-1` to `B-101-15` with the amended `B-099-3`, `B-099-4`, `B-099-5`, `B-099-11` and `B-092-3`. Read `Session::refuse_active_frame_`, `Session::establishment_frame_refused_` and the two reply helpers in `src/session/session.cpp` rather than this page. The validator half is on [`wire.md`](./wire.md), the defined-tag store on [`dictionary.md`](./dictionary.md), the inbound-chain parts on [`inbound-message-path.md`](./inbound-message-path.md).

**What was decided.**

- **A frame's MsgType is its first 35, for every reader** (D4). `FrameHeader::msg_type` is the first 35 and `msg_type_conflict` records any later 35 with other bytes, sticky. The message view and the validator already read the first; the header scan, `interpret_logon` and LogoutSent's reply test were the readers that disagreed. Re-derive the readers with D4's recipe, `git grep -nE '\bmsg_type\b' -- src/session`.
- **The repeat refusal runs on every validating session** (D5), with QuickFIX C++'s structure-before-iterate precedence: 14 when any later 35 lies past the header, else 13 when one lies in it, else 14. The 13/14 walk is its own walk, which does not stop at the header; `peek_fixt_route`'s walk and its stop are unchanged. The header is S's header ∪ the FIXT standard header set, and **every field is read with the selected route's hooks** (owner ruling 2026-11-07, option (b)), so the walk splits a frame exactly as the scan that set `msg_type_conflict` did.
- **Validation off: refused in establishment, handled as the first 35 after** (D6). "Handled as" is a differential oracle, not a table: the ending equals that of the frame with every later 35 removed and re-framed.
- **One owner of the RefMsgType echo, one rule for 372** (D7): verbatim up to `Session::kSendFrameBytes`. A reply that overflows its site's buffer is rebuilt, before any callback and before `assign_outbound`, in the session's inbound reply slot, a `kSendFrameBytes` member; above the ceiling a Reject goes without 372 and a BusinessMessageReject is not sent, with one rate-bounded record.
- **The slot is the inbound chain's alone**, which is what lets it exist (#544 §2.2 rejected a scratch buffer shared with the strand's other chains). Its exclusivity rests on `on_inbound_frame`'s serialized-caller precondition, now stated in `session.hpp` and asserted in builds with assertions (`B-101-10`).

**What was rejected, and why.**

- **Keeping last-wins and moving the first-wins readers** (D4): it reverses the message view's index rule and 099's consume key.
- **The repeat walk on S's hooks before the header's stop and the route's after** (Gate A round 2's rule, superseded by the owner ruling of 2026-11-07). On a custom S that pairs a non-standard Length on a standard-header tag its own `<header>` omits, the scan and the walk split the frame differently. **(a) widening `dict::header_length_pairs` to S's header union**: it changes 099's merge rule and closes only the direction that misfires. **(c) keeping the rule and disclosing the residual**: two readings of one frame.
- **A dictionary-only header boundary on a non-FIXT session** (Gate A round 1, Codex 4, not applied): QuickFIX C++'s `isHeaderField` is its static list or the session dictionary, so the union is the closer model; the residual is `B-101-5`.
- **Enlarging each site's own buffer** (D7): it grows every reply leaf's frame on the normal path past #544's recycler limit and leaves a threshold of its own. **A cold leaf coroutine owning a `kSendFrameBytes` buffer**: its frame is over asio's recycler limit, so a peer could make it allocate per message (`[const §XV.1]`); orchestrator ruling. **`send_frame_slot_`**: an in-flight `Session::send` may hold it. **A BMR without 372**: a validating peer Rejects it (373=1, 371=372). **Refusing the frame**: it sends nothing, which was the defect. **Keeping 092's bound for the disposer**: one field, two rules.

**Records this supersedes, flagged here and not edited** (frozen `specs/` are not rewritten; research's Supersessions table is the full list and its recipe re-derives it).

- ⚠️ **Superseded by 101:** `specs/099-fixt-appl-version-validation/spec.md`'s FR-025 ("whose last MsgType differs from its third field's", the 13/14 rule, the Limits sentences on LogoutSent and on non-validating and non-FIXT sessions keeping the split), FR-029's comparison and scope, the Clarifications on `interpret_logon` reading the last 35 and on LogoutSent, US3 scenarios 7 and 8, and the Edge Cases sentence on a non-FIXT validating session.
- ⚠️ **Superseded by 101:** `specs/099-fixt-appl-version-validation/data-model.md` E-7's S1 and A1, its non-validating establishment paragraph, "A non-FIXT session leaves it false", "LogoutSent gains no step", and the decisive cells and `LO` row that follow from them; `contracts/surface.md`'s validation-off bullet and "A non-FIXT validating session keeps the … split"; `contracts/witnesses.md` W-6's "`D…A` with Logon fields establishes" and W-9's `D…5` cell.
- ⚠️ **Superseded by 101:** `specs/092-garbled-frame-reject/research.md` R-5's RefMsgType bound (`kMaxShippedMsgTypeLength`, deleted) and its "cannot fail" premise, `spec.md` FR-003's omission sub-bullet, and `contracts/unparseable-frame-disposition.md`'s 372 bullet under "Reject contents".
- **Not superseded:** 099's two-occurrence FIXT endings in LogonReceived and Active, its validating FIXT establishment disconnect, and every 1128 refusal.

**Left open by 101.** Echoing 372 beyond the outbound ceiling, up to the inbound limit (fixpp#669, for the owner); a CompID length bound at `open()`, and `apply_inbound_sequence_reset`'s literal-372 Reject dropped unlogged when long CompIDs overflow its buffer (fixpp#670).

## Runtime flows

[`engine-accept-path`](./engine-accept-path.md) · [`initiator-connect-path`](./initiator-connect-path.md) ·
[`inbound-message-path`](./inbound-message-path.md) ·
[`session-liveness-and-reconnect`](./session-liveness-and-reconnect.md) ·
[`graceful-logout`](./graceful-logout.md) · [`message-store-quiescence`](./message-store-quiescence.md)
