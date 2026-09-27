# Contract: disposition of an inbound frame that is framed but unparseable

Normative for 092. Each row is at least one test cell (tasks.md derives the cells from this table
and from quickstart §1's cell list). Definitions:
- **Fault** is the header scan's first-fault record (data-model.md E-1).
- **Read** means read from well-formed bytes before the fault.
- **Field 3 is 35** is `hdr.msg_type_is_third`.
- **34 read** is `parse_seqnum(first 34) > 0` (E-1).
- **Disregard** means no Reject, no advance, no disconnect (owner ruling rows 1 and 5).

## C-1 — Decision order

In every `on_inbound_frame` path:

1. The Framer (unchanged). A §4.5.2 framing failure it detects is disregarded (FR-008).
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
   - A parse inside step 4 can still fail on a frame the scan found fault-free (a **residual**
     failure). That is C-2b.

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
- **Pre-Active, D-1/D-2 refuse whatever field 3 is.** Clarification Q3 ("whatever its MsgType")
  governs there, and disregarding would leave the connection open with nothing to end it:
  - `run_read_pump` (`src/session/engine.cpp`) reads until EOF;
  - the liveness loop starts only on the first transition to Active;
  - the acceptor's only deadline, `kFirstFrameDeadline`, bounds reading the first frame before
    `on_inbound_frame` runs;
  - no logon-reply timer exists for LogonSent.

  Re-derive with `grep -n -i "timeout\|deadline" src/session/engine.cpp` and
  `grep -n "run_liveness_loop" src/session/session.cpp`. A fault-free frame whose field 3 is not 35
  is out of scope (research R-11). LogoutSent disregards (D-9) because its logout timeout ends the
  session.
- **"Any other" includes 3 (Reject) and 5 (Logout).** For a faulty frame this supersedes the
  published no-reject-loop exemption (below).
- **D-5 is exactly #423's rule.** `consume_rejected_seqnum_(seq, msg_type)` excludes `A` and `4`,
  gates on `check_inbound`, closes a filled resend gap, and persists. It runs before the Reject, as
  at the 041 validate gate.
- **D-4 during AwaitingResend** leaves the gap open. The disclosed outcome is in C-5 L-2.
- **Liveness.** In Active, D-4, D-5 and D-6 refresh the inbound-liveness timestamp that any received
  message refreshes today. These frames are received and are not garbled (SL2020's heartbeat-timer
  row: "ANY inbound message (non-garbled)"). The disregard rows do not refresh it. LogonReceived
  refreshes nothing today and still does not.

**Reject contents (D-4, D-5, D-6):**
- 45 = the MsgSeqNum read.
- 372 = the MsgType read, omitted when it is longer than the longest MsgType any shipped dictionary
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

## C-2b — Residual parse failure (the scan found no fault, a later parse failed)

Causes: a resource failure (`wire_offset_table_full`; arena exhaustion surfacing as `out_of_memory`
from the 16 KiB stack arena, whose upstream is `null_memory_resource()` on every non-MSVC-debug
build; a Framer re-feed failure) or an I-4 disagreement. The frame is fault-free per the scan, so
field 3 and 34 are as the scan read them.

The same rows apply, with a single residual reason:
- **373 = 3, 371 omitted**, and 58 names the local cause.
- 3 is inside every supported profile's SessionRejectReason domain. It is the existing fail-closed
  catch-all of `wire_error_to_session_reject_reason`, and it differs from 0 and 5, so a residual
  Reject is distinguishable from a C-2 Reject.
- 99 is rejected: it is outside FIX.4.2's domain.

Each site is terminal (E-3). The per-site action depends on whether `check_inbound` already ran:

| Site (enclosing branch) | `check_inbound` already ran? | Action |
|---|---|---|
| validate gate, NotConnected arm | no | Logon refusal (as D-1) |
| validate gate, LogonSent arm | no | Logon refusal (as D-2) |
| validate gate, LogonReceived/Active arm | no | the C-2 row for (field 3, 34, 35): D-3, D-4, D-5 (`consume_rejected_seqnum_` then Reject), D-6, or D-7/D-8 |
| SequenceReset-Reset `fromAdmin` (Application registered) | no (before Guard 4) | Reject; no advance; NewSeqNo not applied |
| too-low PossDup redeliver (`redeliver_poss_dup` and an Application) | yes, refused | Reject; no advance |
| `validate_sequence_numbers` off, out-of-sequence deliver | yes, refused | Reject; no advance |
| `validate_sequence_numbers` off, GapFill `fromAdmin` | yes, **advanced** | `persist_inbound_advance_`, then Reject; NewSeqNo not applied (knob-off never applies it). The +1 is kept: research R-4 records this deviation and why |
| Logout `fromAdmin` (Application registered) | yes, advanced | `persist_inbound_advance_`, then Reject; the Logout handler does not run (no reply, no disconnect) |
| generic admin `fromAdmin` (Active, Application registered) | yes, advanced | `persist_inbound_advance_`, then Reject; the Heartbeat, TestRequest and ResendRequest handlers do not run |
| `fromApp` | yes, advanced | `persist_inbound_advance_`, then Reject; no BusinessMessageReject; the fall-through persist does not run twice |

Where `check_inbound` already advanced the counter, `consume_rejected_seqnum_` would call it again,
be refused, and persist nothing. Those sites therefore persist directly, as the existing
callback-reject path already does.

Frames that never parse have no parse to fail, so C-2b cannot fire on them. Only I-4 guards them.
The population is in research R-4, and whether it parses depends on the Application registration
and the validation knob (C-5 L-3).

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
- **I-3** A fault-free frame is dispositioned exactly as before this feature, except a frame
  carrying a second 34 or 35, whose scan now selects the first occurrence (E-1). The regression
  suite, a duplicate-34 cell and the paired bench cover it.
- **I-4** Scan fault ⇔ `OffsetTable::build` encoding failure, under the session's hooks and under
  `dict_hooks::none()`. For a fault-free frame, the scan's first 34 equals `OffsetTable::find(34)`,
  and `msg_type_is_third` equals "the third `entries()` element has tag 35" (research R-2).
- **I-5** A residual parse failure is never "success" or "no reject": it takes its C-2b row and is
  terminal.
- **I-6** Behaviour is identical with inbound validation on and off, with `validate_sequence_numbers`
  on and off, on FIX.4.2, FIX.4.4 and FIXT.1.1, and as acceptor and initiator (FR-011). This is
  true for C-2. C-2b's reachability depends on the Application registration and the validation
  knob (C-5 L-3).

## C-4 — Public surface deltas

**C++ (additive, source-compatible):**
- `build_reject(…, std::string_view text)` is a new overload in
  `include/fixpp/session/admin_messages.hpp`. The existing overload is unchanged in signature and in
  its output bytes.
- `admin_messages.hpp`'s no-reject-loop sentence is rewritten to the scoped rule above. The rewrite carries a
  header comment naming the ruling that supersedes it.
- `fixpp::wire::field_fault` (E-0) is new in `include/fixpp/wire/tag_scan.hpp`.
- `field_iterator::fault()` and `fault_length_tag()` are new (E-4). What the iterator yields is
  unchanged. `sizeof(field_iterator)` grows, so the change is source-compatible, not layout-neutral.
- `core::error::wire_invalid_tag_number = 132` and `wire_length_data_mismatch = 133` are new (E-6).
  `reject_reason_map` maps them to 0 and 5.
- `dictionary_driven_validator::validate` returns them where it used to end its walk silently (E-5).
- No `SessionEvent` alternative is added: disregarded frames are not logged (research R-4).

**C-ABI 1.10, BREAKING (`[const §X.7]`; owner ruling 2026-09-27, FR-017):**
- No symbol, signature or error code is added.
- What changes is which inbound Logons are accepted, plus D-3's disconnect of an established
  session. Both are observed through the calls FR-020 of 091 named: `fixpp_session_is_established`,
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
- **L-3: the residual path depends on which paths parse.** A well-formed frame beyond the parse
  arena's field ceiling fails the parse only on a path that parses. It is Rejected with 373=3
  (C-2b) where it is parsed. It is processed normally where it is not: for example a Heartbeat with
  no Application registered, or in LogonReceived, where the generic `fromAdmin` dispatch is
  Active-only. The ceiling is measured (research R-4), not written down from a derivation.
- **L-4: TC2020 Scenario 17d is not followed.** A malformed SignatureLength(93)/Signature(89) pair
  gets 373=5 (the owner ruling's Length+Data code), not 17d's 8 (Signature problem).
- **L-5: a duplicate MsgSeqNum is read first-wins, on every frame.** A fault-free frame carrying two
  34s is sequenced on the first, as the full parse indexes it, where it was sequenced on the last.
  A frame whose first 34 is below the expected number and whose last 34 equals it is now too-low
  and disconnected (a too-low Heartbeat is still ignored). The cell pins that outcome.
