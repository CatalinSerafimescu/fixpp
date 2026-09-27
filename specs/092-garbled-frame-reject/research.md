# Research: 092-garbled-frame-reject

Decisions for the open design points in `spec.md` (A-1, A-2, A-4) and the ones the code reading
raised. Code facts are cited by symbol, not by line; re-derive them before relying on them.

---

## R-1 — Where a parse failure is detected: the header scan reports its first fault

**Decision.** `scan_frame_header` (`src/session/scan_frame_header.hpp`, private) gains a fault
record: on the **first** field it cannot read, it stops and returns the fields read so far, plus:

- the fault kind: `malformed_tag` (a non-digit or empty tag, a tag above 0xFFFF, or a field with
  no `=`), or `length_data_mismatch` (a counted Data value that runs past the frame or is not
  followed by SOH);
- the Length tag, for `length_data_mismatch`;
- the fault's byte offset (for the log line only).

"MsgSeqNum/MsgType read before the fault" is simply `!msg_seq_num.empty()` / `!msg_type.empty()` on
the returned header. The scan stops at the fault, so nothing after it is populated.

**Rationale.**
- The scan already walks every field of every inbound frame, in every state, **with the same
  dictionary hooks as the full parse** (`session_hooks(inbound_tv_)` at each call site). Recording the
  first fault costs a few branches on the fault path and nothing on the clean path. There is no new
  pass and no new arena.
- The scan already stops at a malformed count (fixpp#426 design §4). Today it skips a malformed
  tag and continues. Stopping there too makes both shapes share one rule, which FR-001 needs:
  fields after a fault are never read.
- The header is private (`src/session/`), so no public API changes.

**Alternatives rejected.**
- *Run the full `OffsetTable` build up front, before the guards, and reuse its result.* This is the
  authoritative oracle, but its status is a bare error code: `entries_` is cleared on failure, so
  neither the fault point nor "was 34 read before it" survives. Adding them means changing the
  public wire API (`include/fixpp/wire/offset_table.hpp`). It also moves a 16 KiB parse arena from a
  synchronous helper into the coroutine frame (`[const §VIII.5]`), and it adds a full parse on paths
  that do not parse today.
- *Keep the scan skipping a malformed tag and only record that one occurred.* Rejected: the fields
  after it would then be populated, which is exactly what FR-001 forbids reading.

## R-2 — The scan and the full parse must agree on what a fault is

**Decision.** The scan's fault set must equal the `OffsetTable` build's encoding-failure set
(everything except resource failures: `wire_offset_table_full`, `out_of_memory`, arena exhaustion).
This is enforced three ways:

1. **A differential unit test** over a mutation corpus: well-formed frames for each admin type and
   one application type, each mutated at every field position into each fault shape (non-digit tag,
   empty tag, missing `=`, tag above 0xFFFF, short count, long count, count past the frame). Each
   case asserts `scan fault ⇔ OffsetTable build failure`, under both the dictionary hooks and
   `dict_hooks::none()`. The corpus must include a dictionary-only Length+Data pair (091 widened the
   population).
2. **A fuzz assertion** in `tests/fuzz/fuzz_session_recovery_admin_parse.cpp`: the same equivalence
   on every input (`__builtin_trap` on disagreement). Note #508: the fuzzers do not yet get coverage
   feedback from library code, so this arm is a backstop, not proof.
3. **A dispatch-site backstop** (R-4) for any residual disagreement.

**Rationale.** Two Length+Data readers exist: `wire::length_data_carry::read_value` (used by the
scan) and the inline carry in `OffsetTable::build`. Two copies of a rule are the defect class #426
removed. Unifying them is out of scope here: `OffsetTable::build` is the wire hot path, and a
rewrite is bench-sensitive (the B13 lesson). So the agreement is pinned by an instrument instead.
The differential test must be shown able to fail: seed a disagreement (e.g. drop the scan's
non-SOH check) and watch it go RED.

**Alternative rejected.** Making the scan call `OffsetTable::build`. That is a second full parse per
frame on the hot path, for information the scan can produce itself.

## R-3 — One disposition step, right after the scan, in every state

**Decision.** A new private coroutine `Session::dispose_unparseable_(hdr, state)` runs immediately
after `scan_frame_header` at each `on_inbound_frame` state arm (NotConnected, LogonSent,
LogonReceived/Active, LogoutSent), **before** the validate gate and Guards 2–5. If the scan
recorded no fault, it returns "continue". Otherwise it decides the frame, following the table in
`contracts/unparseable-frame-disposition.md`, and the state arm returns.

**Rationale.** FR-002 and clarification Q2 require the decision before any guard or handler. One
function keeps the rule in one place; the four state arms today each re-derive their own handling,
which is how #507's gaps arose.

## R-4 — A residual parse failure at a dispatch site is never "success"

**Decision.** `parse_and_dispatch_` keeps skipping the callback on a parse failure, but it now
returns a distinct result (`dispatch_parse_failed`) rather than `expected_t<void>{}`. Each call site
handles it explicitly:

- **Application path** (fromApp): send Reject(35=3) 373=99 (Other) with Text(58) naming the local
  cause. At that point Guard 4 has already accepted the MsgSeqNum, so the number is consumed, which
  matches FR-004 at the expected number. The message is never silently persisted-and-dropped.
- **Admin paths** (fromAdmin, Reset, Logout): the frame passed the scan's fault check, so its scanned
  fields are trustworthy. The observational callback is skipped as today, and a log line records it.
- **`fire_to_admin_`** (our own outbound frame): unchanged.

`validate_inbound_`'s "parse failed → nullopt" arms become unreachable for encoding faults, since
R-3 decides those first. They stay as a resource-failure backstop, now with a log line, and the
comment calling them "validation passes" is corrected.

**Rationale.** Only resource failures, or a scan/parse disagreement R-2 failed to catch, reach
these sites. Neither may read as "dispatch succeeded" (FR-001's last sentence).

## R-5 — Reject Text(58): a new `build_reject` overload

**Decision.** Add an overload of `build_reject` (`include/fixpp/session/admin_messages.hpp`, public)
with a trailing `std::string_view text`, emitted as 58 when non-empty. The existing overload
delegates to it with an empty text. `emit_session_reject_` gains the matching parameter. The Text is
a fixed string per fault kind (e.g. "Invalid tag number at offset N", "Length(95) does not match its
Data"), built in the existing 512-byte stack buffer, with no allocation.

**Rationale.** Additive and source-compatible. Rejects already emitted keep byte-identical output
(no 58), so no existing golden changes. The public-header change is a Gate A trigger in any case.

## R-6 — Reason codes

**Decision.** Per FR-007 and clarification Q1:
- `malformed_tag` → 373=0, 371 omitted;
- `length_data_mismatch` → 373=5, 371 = the Length tag;
- 372 = the MsgType read before the fault;
- the R-4 residual → 373=99.

The session sets these codes directly from the scan's fault kind. `reject_reason_map` gains mappings
only for the validator path (R-7).

## R-7 — `dictionary_driven_validator::validate` (FR-012)

**Decision.** The field iterator (`include/fixpp/wire/parser.hpp`) gains a read-only
`stopped_on_fault()` accessor, plus the fault kind. `validate` checks it after its walk and returns a
new wire error for each kind: `wire_invalid_tag_number` → 373=0, `wire_length_data_mismatch` → 373=5,
both added to `reject_reason_map`. Both the iterator accessor and the error enumerators are
**additive** public-API changes.

**Rationale.** Today the walk just ends, so every field after the stop goes unchecked. That is a
fail-open in a public API, even though the session cannot reach it (R-3 decides first).

**Alternative rejected.** Disclosing it as a limitation only. The fix is small, and a documented
fail-open in a validator is the kind of claim this repo has paid for before.

## R-8 — C-ABI, bindings, versioning (A-4)

**Finding.** No C-ABI or Python surface changes. The receive callback was already not invoked for
such frames. Nothing in the C API exposes rejects, and `validate_inbound_messages` has no C setter.
What changes is **peer-visible**: the counterparty now receives a Reject where it used to get
silence, or a sequence effect. The C-ABI header text that documents the old behaviour ("dropped as a
parse error, silently … no Reject is sent", `include/fix/c_api/session.h`) must be corrected
(FR-014).

**Open for Gate A.** Whether a peer-visible session-behaviour change with no C-surface change needs a
C-ABI minor bump or only a CHANGELOG / B&L entry. The 1.9 precedent (091) bumped for a change in
what the C API *returns*; this feature changes what the peer *receives*. Proposed: a B&L entry and
the header text fix, with no version bump.

## R-9 — Performance

**Finding.** No bench measures the inbound session path. `bench/session/fsm_bench.cpp` says the
inbound-dispatch path needs a fully wired Active session, and it measures only the outbound admin
emit. `bench/baselines/session/` holds only `placeholder.json`.

**Decision.** The instrument is a new micro-bench, `bench/session/scan_frame_header_bench.cpp`. It
runs `scan_frame_header` over clean frames (a Heartbeat, a NewOrderSingle, and a frame carrying a
Length+Data pair) under the dictionary hooks and under `dict_hooks::none()`. It is written and run
against the **unchanged** scan first, and that baseline is committed with its SHA. The memory lesson
applies: a timing baseline cannot be reconstructed after the edit. It is re-run after the change.
The disposition check itself is one branch per frame, and the scan's clean path trades an existing
`continue`/`return` for a fault record on the fault branch only. The B13 lesson is to measure, not
assume.

## R-10 — Tests that pin the old behaviour

The owner ruling revises these, so they are reviewed rather than silently flipped:
- `tests/session/scan_frame_header_overflow_test.cpp` `NonDigitToken_Rejected_NotDispatched`: still
  holds (the field is not surfaced). It gains an assertion on the fault record, and the "conforming
  pair" half moves to a separate frame, since the scan now stops at the fault.
- `tests/session/length_data_session_scanner_test.cpp` `ScanFrameHeaderStopsAt…`: still hold. They
  gain fault-kind and Length-tag assertions.
- `tests/session/session_reject_test.cpp` `NoRejectOnMalformedLogout`: its "malformed" is a CompID
  mismatch on a well-formed frame, not a parse failure, so it is unaffected.
- The C-ABI doc text in `include/fix/c_api/session.h`, and any C-ABI test asserting "no Reject is
  sent" for a malformed pair: re-derive with `grep -rn "no Reject" tests/capi include/fix`.

## R-11 — The Reject needs BOTH MsgSeqNum and MsgType read before the fault

**Finding.** `wire::Framer` checks BeginString, BodyLength and CheckSum. It does **not** check that
MsgType(35) is the third field: grep `src/wire/framer.cpp` for any 35 or order check and none
appears. Header order is checked only by the validator's Step 0 (`wire_header_out_of_order` → 373=14).
So a fault can precede 35 (`8=…␁9=…␁9x9=1␁35=D…`).

**Decision.** FR-003's Reject applies only when **both** 34 and 35 were read before the fault. If
either is missing, the frame is ignored as garbled (FR-006). A frame whose third field is not 35 is
garbled under FIX-SL 2020 §4.5.2, and a Reject with no RefMsgType could not be classified against
the Logon/SequenceReset exclusions of FR-004.

Awaiting a Logon, the refusal (FR-009) needs neither field: any faulty frame is refused.

**Out of scope, disclosed.** Without inbound validation, a *fault-free* frame whose third field is
not 35 is still not ignored as §4.5.2 requires; the Framer does not check it. That is pre-existing,
and it is a separate issue to file at close-out.
