# Contract: disposition of an inbound frame that passes framing but fails the parse

Normative for 092. Each row is one test cell (tasks.md derives the cells from this table). "Fault"
means the header scan's first-fault record (data-model.md E-1). "Read" means read from well-formed
bytes before the fault.

## C-1 — Decision order

In every `on_inbound_frame` state arm:

1. The Framer (unchanged). A §4.5.2 framing failure is ignored: no Reject, no advance (FR-008).
2. `scan_frame_header` → `hdr`, including `hdr.fault`.
3. **`dispose_unparseable_`**: if `hdr.fault` is set, decide per C-2 and return. No later step runs.
4. Everything else, unchanged: the validate gate, Guard 2 (identity), Guard 3 (SendingTime), the
   SequenceReset Reset arm, Guard 4 (seqnum class), handlers, dispatch.

## C-2 — Disposition table

| # | State | 34 read | 35 read | 35 value | Disposition | NextNumIn | Outbound |
|---|---|---|---|---|---|---|---|
| D-1 | NotConnected (acceptor, awaiting Logon) | any | any | any | Logon refusal path | unchanged | none (disconnect) |
| D-2 | LogonSent (initiator, awaiting reply) | any | any | any | Logon refusal path | unchanged | none (disconnect) |
| D-3 | LogonReceived / Active | yes | yes | `A` | Logon refusal path | unchanged | none (disconnect) |
| D-4 | LogonReceived / Active | yes | yes | `4` | Reject; NewSeqNo never applied | unchanged | Reject |
| D-5 | LogonReceived / Active | yes = expected | yes | other | Reject | +1 | Reject |
| D-6 | LogonReceived / Active | yes ≠ expected | yes | other | Reject; no ResendRequest, no too-low Logout | unchanged | Reject |
| D-7 | LogonReceived / Active | no | any | any | ignore as garbled (log) | unchanged | none |
| D-8 | LogonReceived / Active | any | no | — | ignore as garbled (log) | unchanged | none |
| D-9 | LogoutSent | any | any | any | ignore (log); not the Logout reply | unchanged | none |

**Reject contents (D-4, D-5, D-6):**
- 45 = the MsgSeqNum read;
- 372 = the MsgType read;
- 373 = 0 for `malformed_tag`, with 371 omitted; 373 = 5 for `length_data_mismatch`, with 371 = the
  Length tag;
- 58 = a fixed text per fault kind.

The Reject is persisted and emitted like any other outbound session Reject (FR-013).

**D-5 is exactly #423's rule**: `consume_rejected_seqnum_(seq, msg_type)` runs before the Reject,
and it already excludes `A` and `4` and gates on `check_inbound`.

**Reject loop.** A malformed inbound Reject (35=3) follows D-5/D-6. The Reject we send in reply
cannot start a loop, because our Reject is well-formed (FR-004 in 041 is unchanged: a *well-formed*
inbound Reject is never rejected).

## C-3 — Invariants (each a test)

- **I-1** On a faulty frame, no handler reads any `hdr` field other than 34 and 35: SequenceReset,
  Logout, TestRequest, ResendRequest, Heartbeat, PossDup, identity or SendingTime. Witnesses: the
  T076 probe (NewSeqNo not applied), no Heartbeat for a faulty TestRequest, no retransmission for a
  faulty ResendRequest, no Logout reply for a faulty Logout.
- **I-2** An application frame with a fault never reaches the application callback, and is never
  persisted as received without a Reject.
- **I-3** A fault-free frame is dispositioned exactly as before this feature (the regression suite
  plus the bench).
- **I-4** Scan fault ⇔ `OffsetTable::build` encoding failure, under the session's hooks and under
  `dict_hooks::none()` (research R-2).
- **I-5** A residual parse failure at a dispatch site is never "success": application path → Reject
  99; admin path → callback skipped and logged (research R-4).
- **I-6** Behaviour is identical with inbound validation on and off, on FIX.4.2, FIX.4.4 and
  FIXT.1.1, as acceptor and initiator (FR-011).

## C-4 — Public API deltas (all additive)

- `build_reject(…, std::string_view text)`: a new overload in `include/fixpp/session/admin_messages.hpp`.
  The existing overload is unchanged in signature and in its output bytes.
- The Parser field iterator gains `stopped_on_fault()` and a fault-kind accessor
  (`include/fixpp/wire/parser.hpp`).
- `core::error` gains `wire_invalid_tag_number` and `wire_length_data_mismatch`;
  `reject_reason_map` maps them to 0 and 5. These are returned only by
  `dictionary_driven_validator::validate` (FR-012). `OffsetTable::build`'s codes are unchanged.
- C-ABI: no signature, struct or enum change. The `include/fix/c_api/session.h` doc text that says a
  malformed pair is dropped "silently … no Reject is sent" is corrected. Whether a version bump is
  needed is a Gate A question (research R-8).
