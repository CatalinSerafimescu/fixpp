# Contract: disposition of an inbound frame that is framed but unparseable

Normative for 092. Each row is at least one test cell (tasks.md derives the cells from this table
and from quickstart §1's cell list). Definitions:
- **Fault** is the header scan's first-fault record (data-model.md E-1).
- **Read** means read from well-formed bytes before the fault.
- **Field 3 is 35** is `hdr.msg_type_is_third`.
- **34 read** is `parse_seqnum(hdr.fault_ref_seq_num) > 0` (E-1).
- **35 value** is `hdr.fault_ref_msg_type` (E-1), never the last-wins `hdr.msg_type`.
- **Disregard** means no Reject, no advance, no disconnect. 092 applies it only to scan-faulty frames
  (owner ruling row 1 for criterion 3, row 5), never to a Framer failure (C-1 step 1).

## C-1 — Decision order

In every `on_inbound_frame` path:

1. The Framer (unchanged). A §4.5.2 framing failure it detects ends the session, as today
   (`L-004-4`; FR-008). Its disregard is fixpp#514.
   - **1b.** In Active only, the negotiated MaxMessageSize(383) guard runs **before** the state
     switch (unchanged). It reads only `frame.size()`, which the Framer validated through
     BodyLength, and no field of the frame, so it does not violate FR-001. An oversized frame is
     disconnected whether or not it is also unparseable: size precedes disposition.
2. `scan_frame_header` → `hdr`, including `hdr.fault` and `hdr.msg_type_is_third`. In NotConnected
   this is the arm's hoisted scan (E-2).
3. **If `hdr.fault` is set**, the arm `co_await`s `dispose_unparseable_`, which decides per C-2, and
   the arm returns. No later step runs. The fault-free path takes one inline compare (E-2).
4. Everything else is unchanged: the validate gate, Guard 2 (identity), Guard 3 (SendingTime), the
   SequenceReset Reset arm, Guard 4 (seqnum class), handlers and dispatch.
   - A parse inside step 4 can still fail on a frame the scan found fault-free (a **late parse
     failure**). That is C-6: the session closes.

## C-2 — Disposition of a faulty frame (the rows are evaluated top to bottom)

| # | State | Field 3 is 35 | 34 read | 35 value | Disposition | NextNumIn | Outbound |
|---|---|---|---|---|---|---|---|
| D-1 | NotConnected (acceptor, awaiting Logon) | any | any | any | Logon refusal: `record_state_transition_(Disconnected)`, the action the arm takes when `interpret_logon` refuses | unchanged | none |
| D-2 | LogonSent (initiator, awaiting the reply) | any | any | any | Logon refusal, as D-1 | unchanged | none |
| D-9 | LogoutSent | any | any | any | disregard; not taken as the Logout reply; the logout timeout runs as usual | unchanged | none |
| D-8 | LogonReceived / Active | **no** | any | any | disregard as garbled (§4.5.2 criterion 3; ruling row 1) | unchanged | none |
| D-7 | LogonReceived / Active | yes | **no** | any | disregard (ruling row 5, TC2020 17g) | unchanged | none |
| D-3 | LogonReceived / Active | yes | yes | `A` | Logon refusal: `record_state_transition_(Disconnected)` (silent; not `refuse_logon_with_logout_`, not Logout-with-error) | unchanged | none |
| D-4 | LogonReceived / Active | yes | yes | `4` | Reject; NewSeqNo(36) never applied | unchanged | Reject |
| D-5 | LogonReceived / Active | yes | yes, = expected | any other | `consume_rejected_seqnum_`, then Reject | +1 (persisted before the Reject) | Reject |
| D-6 | LogonReceived / Active | yes | yes, ≠ expected | any other | Reject; no ResendRequest, no too-low Logout, PossDup not consulted | unchanged | Reject |

- **When field 3 is itself the faulting field**, field 3 is not 35, so D-8 applies in
  LogonReceived/Active.
- **Pre-Active, D-1/D-2 refuse whatever field 3 is: today's handling, kept** (owner ruling O-1).
  Both pre-Active arms already refuse every input that is not a valid Logon:
  - the NotConnected arm of `Session::on_inbound_frame`: its comment "No MsgType discrimination:
    every refusal on this row lands in Disconnected";
  - the LogonSent arm: its refusal of "Heartbeat/TestRequest/Reject/out-of-scope admin / invalid
    MsgType" inbound.

  Re-derive with `grep -n "No MsgType discrimination\|out-of-scope admin" src/session/session.cpp`.
  Every Framer failure pre-Active is session-fatal too (`L-004-4`). A pre-Active disregard of a
  frame whose field 3 is not 35 is **fixpp#514**. It needs an establishment timeout: the liveness
  loop starts only on the first transition to Active, `kFirstFrameDeadline` bounds only the first
  read, and LogonSent has no logon-reply timer (re-derive with
  `grep -n -i "timeout\|deadline" src/session/engine.cpp` and
  `grep -n "run_liveness_loop" src/session/session.cpp`). A fault-free frame whose field 3 is not
  35 is also #514 (research R-11). LogoutSent disregards (D-9) because its logout timeout ends the
  session.
- **"Any other" includes 3 (Reject) and 5 (Logout).** For a faulty frame this supersedes the
  published no-reject-loop exemption (below).
- **D-5 is exactly #423's rule.** The disposer calls
  `consume_rejected_seqnum_(parse_seqnum(hdr.fault_ref_seq_num), hdr.fault_ref_msg_type)`, never
  `hdr.msg_type` (FR-004), so its `A` and `4` exclusions and the 372 value both come from
  `fault_ref_msg_type`. It gates on `check_inbound`, closes a filled resend gap, and persists. It
  runs before the Reject, as at the 041 validate gate, and a failed persist disconnects with no
  Reject (FR-013). Both are pinned by the `092 disposer (D-5)` case in #423's two persistence
  tables (quickstart §1 "D-5 persistence").
- **D-5 at the expected number = seqnum_max** does not advance and does not Reject. The advance
  fails with `store_seqnum_overflow`, and `consume_rejected_seqnum_` takes the silent Disconnected
  transition (FR-019, I-7). The same holds at every #423 Reject site.
- **D-4 during AwaitingResend** leaves the gap open. The disclosed outcome is in C-5 L-2.
- **Liveness.** In Active, D-4, D-5 and D-6 refresh the inbound-liveness timestamp that any received
  message refreshes today. These frames are received and are not garbled (SL2020's heartbeat-timer
  row: "ANY inbound message (non-garbled)"). The disregard rows do not refresh it. LogonReceived
  refreshes nothing today and still does not.

**Reject contents (D-4, D-5, D-6):**
- 45 = the MsgSeqNum read (`fault_ref_seq_num`).
- 372 = the MsgType read (`fault_ref_msg_type`), omitted when it is longer than the longest MsgType any shipped dictionary
  defines. That bound is derived from `dictionaries/*.xml` by a test, not written as a number
  (research R-5). A peer-controlled 372 therefore cannot overflow the Reject's 512-byte buffer and
  leave the number consumed with no Reject sent.
- 373 = 0 for `malformed_tag`, with 371 omitted.
- 373 = 5 for `length_data_mismatch`, with 371 = `hdr.fault_length_tag`.
- 58 = a fixed string per fault kind (R-5). It carries no offset and no peer bytes.
- The Reject is persisted and emitted like any other outbound session Reject (FR-013).

**No-reject-loop, superseded for faulty frames (FR-003).** The header comment of `build_reject` in
`include/fixpp/session/admin_messages.hpp` states the published rule "a malformed Reject/Logout is
never itself rejected (I-5)". That "I-5" is the header's own label, not C-3 I-5 below. The session applies it through the `msg_type != "3" && msg_type != "5"`
exemptions at the validate gate and at Guard 3. **It still holds for a well-formed frame that fails
validation or SendingTime.** For a faulty frame, the ruling's rows apply to every MsgType, so a
malformed Reject (35=3) or Logout (35=5) is Rejected under D-5/D-6.

**A loop is bounded.** fixpp never originates a Reject in reply to a well-formed Reject. It needs a
peer that both garbles every Reject it sends and rejects Rejects, and even then each fixpp Reject
answers one peer frame. The bound cell is quickstart §1 "reject-loop bound".

## C-3 — Invariants (each a test)

- **I-1** On a faulty frame, no handler reads any `hdr` field other than 34, 35 and
  `msg_type_is_third`. That excludes SequenceReset, Logout, TestRequest, ResendRequest, Heartbeat,
  PossDup, identity and SendingTime. Witnesses:
  - the T076 probe (NewSeqNo not applied);
  - no Heartbeat for a faulty TestRequest;
  - no retransmission for a faulty ResendRequest;
  - no Logout reply for a faulty Logout.
- **I-2** An application frame with a fault never reaches the application callback, and is never
  persisted as received without a Reject.
- **I-3** A fault-free frame is dispositioned exactly as before this feature, byte for byte: the
  scan keeps last-wins for every existing member (E-1). The regression suite, a duplicate-34 cell and
  the paired bench cover it.
- **I-4** Scan fault ⇔ `OffsetTable::build` encoding failure, under the session's hooks and under
  `dict_hooks::none()`. For a fault-free frame, `fault_ref_seq_num` equals the value of the first
  `entries()` element with tag 34, and `msg_type_is_third` equals "the third `entries()` element has
  tag 35", and `fault_ref_msg_type` equals the value of the third `entries()` element when that
  element has tag 35, else empty (research R-2). The oracle is `entries()`, never `find(34)`: the overlay can leave an
  occurrence unindexed while the build succeeds.
- **I-5** A late parse failure is never "success" or "no reject": the session closes (C-6).
- **I-6** The C-2 disposition is identical with inbound validation on and off, with
  `validate_sequence_numbers` on and off, on FIX.4.2, FIX.4.4 and FIXT.1.1, and as acceptor and
  initiator (FR-011).
- **I-7** NextNumIn never wraps (FR-019; the inbound side of `seqnum_manager.hpp`'s I-8).
  `SeqnumManager::check_inbound` refuses to advance from seqnum_max with `store_seqnum_overflow` and
  leaves the counter unchanged. Every caller ends the session on that error with the silent
  Disconnected transition, before any Reject, delivery or drop. This holds for Guard 4 (every
  MsgType, PossDupFlag and `validate_sequence_numbers` value), for `consume_rejected_seqnum_` (every
  #423 site and D-5) and for the pre-Active Logon arms, with a persistent and a non-persistent store.
  The caller population and its derivation are in research R-14.

## C-4 — Public surface deltas

**C++ (additive, source-compatible):**
- `build_reject_with_text(…, std::string_view text)` is a new function in
  `include/fixpp/session/admin_messages.hpp`. `build_reject` stays a single declaration, unchanged
  in signature and in its output bytes; it delegates to `build_reject_with_text` with an empty text.
  No name gains a second declaration, so every existing call and every use of `build_reject`'s
  name compiles as before.
- `admin_messages.hpp`'s no-reject-loop sentence is rewritten to the scoped rule above. The rewrite carries a
  header comment naming the ruling that supersedes it.
- `fixpp::wire::field_fault` (E-0) is new in `include/fixpp/wire/tag_scan.hpp`.
- `field_iterator::fault()` and `fault_length_tag()` are new (E-4). What the iterator yields is
  unchanged. `sizeof(field_iterator)` grows, so the change is source-compatible, not layout-neutral.
- `core::error::wire_invalid_tag_number = 132` and `wire_length_data_mismatch = 133` are new (E-6).
  `reject_reason_map` maps them to 0 and 5.
- `dictionary_driven_validator::validate` returns them where it used to end its walk silently (E-5).
- No `SessionEvent` alternative is added: disregarded frames are not logged (research R-4).
- `SeqnumManager::check_inbound` (public `include/fixpp/session/seqnum_manager.hpp`) returns
  `store_seqnum_overflow` when asked to advance from seqnum_max, where it used to wrap to 0 (FR-019).
  That is the error `assign_outbound` already returns. It is source-compatible, and its header comment
  names 092/FR-019. `set_next_inbound` is unchanged.

**C-ABI 1.10, BREAKING (`[const §X.7]`; owner ruling 2026-09-27, FR-017):**
- No symbol, signature or error code is added.
- What changes is which inbound Logons are accepted, plus D-3's disconnect of an established
  session, plus C-6's close on a late parse failure at a dispatch site, plus FR-019's close at NextNumIn = seqnum_max (research R-14). Each is observed through the calls FR-020 of 091 named: `fixpp_session_is_established`,
  `fixpp_session_close`, `fixpp_session_send`, `fixpp_session_register_callback` and
  `fixpp_session_register_send_callback`.
- The population is re-derived with 091's recipe (research R-8). The carriers and the procedure are
  in plan.md "Phase 0b".

## C-5 — Disclosed outcomes (behaviours-and-limitations rows, each pinned by a cell)

- **L-1: the disregard rows re-create the resend loop.** A deterministically malformed frame whose
  fault precedes 34 (D-7), or whose field 3 is not 35 (D-8), is disregarded. The next message opens
  a gap and fixpp sends a ResendRequest. The peer replays the same bytes, and they are disregarded
  again. SL2020 §4.5.2 names this loop, and fixpp has no resend-loop guard. The cell pins the
  observed outcome.
- **L-2: a faulty GapFill during AwaitingResend ends in a disconnect.** The sequence:
  1. A faulty GapFill at the expected N is Rejected and not advanced (D-4).
  2. While AwaitingResend, the too-high arm is skipped.
  3. The peer's later PossDup resends fail `check_inbound` and are dropped.
  4. Its next new message is too-high without PossDup and disconnects.
  5. After reconnect the same GapFill is replayed.

  The cell pins the outcome.
- **L-4: TC2020 Scenario 17d is not followed.** A malformed SignatureLength(93)/Signature(89) pair
  gets 373=5 (the owner ruling's Length+Data code), not 17d's 8 (Signature problem).
- **L-6: a late parse failure closes the session (C-6).** A well-formed frame that exhausts the
  parse arena, silently consumed today, now ends the session. The cell pins it per late site,
  including whether a reconnect's NextNumIn includes the closed-on frame (resend requested, or the
  number consumed).
- **L-7: a session whose NextNumIn reaches seqnum_max ends at the next message that would consume
  NextNumIn** (FR-019): Guard 4, a #423 site, D-5 or a pre-Active Logon. A Reset-mode
  SequenceReset, D-4, D-6 and the disregard rows do not consume it, and do not end the session
  through the bound. At a #423 Reject site, a Logon or SequenceReset does not consume either,
  because `consume_rejected_seqnum_` returns before `check_inbound` for `A` and `4`. It used to wrap NextNumIn to 0 and continue. The session now goes silently to
  Disconnected, with no Reject and no Logout. Where a reconnect resumes depends on the store, because
  the SequenceReset jump is not persisted (research R-14). 092 does not change that. The SC-010
  cells pin the close on both store kinds.

(L-3 and L-5 were deleted in Gate A round 2, and the numbers are not reused.)

## C-6 — Late parse failure: fail-closed (owner ruling O-2)

A **late** parse site is an inbound parse that runs after step 3 (C-1), so it only ever sees a frame
the scan found fault-free. A parse failure there is a resource failure (the arena or the
offset-table cap, reachable with a large well-formed frame) or an I-4 breach. A Framer re-feed
failure is not a cause: each re-feed is one exact frame into an empty default-constructed Framer.
Re-derive with `grep -n "wire::Framer " src/session/session.cpp src/session/engine.cpp` and read
each construction and its single `feed`.

**Population.** `grep -n "parse_and_dispatch_(\|validate_inbound_(" src/session/session.cpp`. Read
each call and classify it by the provenance of the bytes it parses: bytes received from the peer
are a late inbound site; a frame fixpp built (the outbound admin and toApp sites, whatever arena they
use) is not. The validate gate in every state arm is in the population.

**One action at every late inbound site:** `close(close_mode::terminal)`, no Reject, the parse
target's receive callback (`fromAdmin`/`fromApp`) not invoked, and the frame never read as success
or "no reject". `onLogout` from the close, and callbacks already fired earlier at the site (e.g.
`toAdmin` for the Logout handler's confirming Logout), are out of scope of that clause. Where the call's result is not bound
today, it is bound. There is no per-site table.
- At a pre-Active validate gate the close ends the connection as a refusal does, so SC-006's
  deletion proof also runs with this close deleted. `Session::close` is keyed on the session's
  lifecycle, not on its FSM state, so it acts pre-Active: `Session::open` already calls it before
  any Logon, on an `onCreate` throw (re-derive with
  `grep -n "close(fixpp::session::close_mode::terminal)" src/session/session.cpp`).
- **Effects already taken are not undone.** Depending on the site, Guards 2 and 3, the Guard 4
  advance, the resend-gap close, the liveness refresh, or the Logout handler's confirming Logout
  (sent before its `fromAdmin` parse) have run. The close guarantees only that the frame is not
  treated as delivered and the session does not continue. Moving the decision ahead of them is
  fixpp#515.
- **Disclosed cost** (C-5 L-6): a well-formed frame that exhausts the arena, silently consumed
  today, now ends the session. Whether a reconnect re-requests the frame depends on whether the
  site's advance was persisted before the close. Each late-site cell reconnects after the close,
  over a persistent store that survives it, and asserts whether NextNumIn includes the closed-on
  frame: the peer's resend is requested, or the number was consumed. That pins today's per-site
  outcome, which C-5 L-6 records; any change is fixpp#515's.
  It is a fail-closed change, classified under research R-8's recipe.
