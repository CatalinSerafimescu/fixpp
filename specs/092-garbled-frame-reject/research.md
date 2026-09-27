# Research: 092-garbled-frame-reject

Decisions for the open design points in `spec.md` and the ones the code reading or Gate A round 1
raised. Code facts are cited by symbol, not by line. Re-derive them with the command given before
relying on them. A list below that comes with a command is a snapshot of that command's output at
the plan head, **not an authority**: tasks re-run the command at the implementation head, and a
member the command adds is a planned edit, not an omission.

---

## R-1 — Where a parse failure is detected: the header scan reports its first fault, and identifies the header by position

**Decision.** `scan_frame_header` (`src/session/scan_frame_header.hpp`, private) gains the fault
record and header identification of data-model.md E-1.
- It stops at the **first** field it cannot read.
- It records the kind (`fixpp::wire::field_fault`, E-0), the Length tag for a
  `length_data_mismatch`, and the fault offset (for instruments only).
- It records whether the **third field is a well-formed 35** (`msg_type_is_third`).
- It keeps the **first** 34 and the first 35 instead of the last.

**Rationale.**
- The scan already walks every field of every inbound frame, with **the same dictionary hooks as the
  full parse**. `session_hooks(inbound_tv_)` in the scan and `for_table_view(*inbound_tv_)` in both
  parse sites resolve to the same table. Recording the first fault costs branches on the fault path
  only. There is no new pass and no new arena.
- The scan already stops at a malformed count (fixpp#426 design §4). Today it skips a malformed tag
  and continues. Stopping there too gives both shapes one rule, which FR-001 needs: fields after a
  fault are never read.
- **Positional identification (Gate A round 1, root cause #1).**
  - The owner ruling's framing row names "35 not third" as garbled: disregard, no Reject. The
    Framer does not check it (R-11).
  - Keying the Reject on "a 35 value was seen" would Reject and advance a frame such as
    `8|9|49=P|35=D|34=N|9x9=1|10`, contradicting that row.
  - With the positional bit, the disposition in LogonReceived/Active first sends any faulty frame
    whose field 3 is not 35 to D-8 (disregard). The remaining rows then condition on 34 alone, as
    the ruling words them. Before Active, clarification Q3's refusal applies whatever field 3 is
    (contract C-2).
- **First-wins.** `OffsetTable` keeps the first occurrence of a tag (its overlay insert skips a
  duplicate), while the scan is last-wins today. The disposition addresses the Reject and advances
  on 34, so it must select the same 34 the parse would. Otherwise `34=99|35=D|34=2|9x9=1` could be
  advanced at 2. The change is scan-wide, so it also changes a fault-free frame that carries a
  second 34 or 35. That is I-3's one carve-out, pinned by a cell. It is observable: a frame whose
  first 34 is below the expected number and whose last 34 equals it is now too-low and
  disconnected, where it was processed. R-8 classifies that for the C ABI, and contract C-5 L-5
  discloses it. Both candidate values come from
  the same authenticated peer, so no trust boundary is crossed either way. This aligns a divergence
  that already existed for every duplicated header field.
- **"34 read" is `parse_seqnum(first 34) > 0`.** `34=abc` is non-empty but `parse_seqnum` returns 0
  for it. A Reject with 45=0 would be meaningless, and the fault-free twin is disconnected by
  Guard 4's `seq == 0` check. So a non-numeric or zero 34 is "not read" (D-7).
- **The header is private** (`src/session/`). Its only public dependency is the shared enum E-0.

**Alternatives rejected.**
- *Run the full `OffsetTable` build up front and reuse its result.* It is the authoritative oracle,
  but its status is a bare code: `entries_` is cleared on failure, so neither the fault point nor
  "was 34 read before it" survives. It would move a 16 KiB parse arena into the coroutine frame
  (`[const §VIII.5]`), and it would add a full parse on paths that do not parse today (R-4).
- *Keep the scan skipping a malformed tag and only record that one occurred.* The fields after it
  would then be populated, which FR-001 forbids reading.
- *Keep last-wins and add a separate first-34 member used only by the disposition.* It would give
  two answers for "the frame's MsgSeqNum" inside one struct: the defect class #426 removed.

## R-2 — The scan and the full parse must agree on what a fault is (C-3 I-4)

**Decision.** The scan's fault set equals the `OffsetTable::build` encoding-failure set (data-model
E-0 gives the derivation). Resource failures are excluded: `wire_offset_table_full` and
`out_of_memory`. For a fault-free frame, the scan's first 34 and `msg_type_is_third` also equal what
`OffsetTable` indexes. Three instruments enforce this.

1. **A differential unit test over a mutation corpus.**
   - **Seeds.** Well-formed frames for every admin MsgType the session handles and one application
     type, plus a frame carrying a standard Length+Data pair and one carrying a dictionary-only
     pair (091 widened that population). **Each seed is asserted to build cleanly** under the
     hooks it is used with before it is mutated, so a mutation cannot pass because both sides
     already failed for an unrelated reason.
   - **Mutations.** Each seed is mutated at every field position into each fault shape. The
     shapes are: a non-digit tag, an empty tag, a missing `=`, a tag above 0xFFFF, a short count,
     a long count, a count past the frame, and a count ending exactly at the frame end.
   - **Oracle, per case.** The case asserts:
     - the exact status class: scan `malformed_tag` ⇔ `OffsetTable` failure at a tag site
       (`wire_invalid_field_format` or `wire_tag_out_of_range`); scan `length_data_mismatch` ⇔
       `OffsetTable` `wire_invalid_field_format` at a counted site;
     - that the scan's `fault_offset` and `fault_length_tag` equal the planted mutation's. The
       fault kind cannot come from `OffsetTable`, which maps both non-digit tags and Length+Data
       faults to `wire_invalid_field_format`, so the corpus checks the kind against what it
       planted;
     - that every header member before the planted offset is populated and every one after it is
       empty.
   - **Hooks.** Every case runs under the dictionary hooks and under `dict_hooks::none()`. A
     dictionary-only pair faults only under the hooks that declare it.
   - **Accepted controls,** each asserted clean on both sides:
     - a leading-zero tag;
     - an empty ordinary value;
     - a non-numeric Length value, which counts as 0 on both sides;
     - a Length tag as the last field before the trailer, with no Data after it;
     - an unrelated tag after a Length, which discards the pending pair;
     - the standard static pairs under `dict_hooks::none()`;
     - a duplicate 34 and a duplicate 35 (first-wins agreement);
     - a fault-free frame whose third field is not 35 (`msg_type_is_third` false on both sides).
   - **Proof that it can fail, per mutation family.** In a scratch copy, seed one disagreement per
     family and watch that family go RED. The families are: the non-digit check, the overflow
     check, the empty-tag check, the no-`=` check, the non-SOH check, the end-equals-size check,
     and first-wins. One planted disagreement does not prove the others can fail.
2. **A fuzz assertion** in `tests/fuzz/fuzz_session_recovery_admin_parse.cpp`. The harness today
   builds a whole `Session` over a FIX.4.2 minimal dictionary and has no dictionary-only pair. The
   092 arm:
   - calls `scan_frame_header` and `OffsetTable::build` directly on each input, under both hook
     sets, with a dictionary that declares a dictionary-only pair;
   - traps (`__builtin_trap`) on any encoding disagreement and on a first-34 or third-field
     disagreement for a fault-free input;
   - skips **only** inputs whose `OffsetTable` status is `wire_offset_table_full` or
     `out_of_memory`, and counts them, so a fuzz run that skipped everything is visible;
   - is proven by one planted disagreement.

   Caveat #508: the fuzzers do not yet get coverage feedback from library code, so this arm is a
   backstop, not proof.
3. **The residual path (R-4)** makes any disagreement that survives both a terminal Reject with
   373=3, never "success". It is the last line. It cannot see a disagreement on a path that never
   parses (R-4 table), so the first two instruments are the only guard there.

**Rationale.** Two Length+Data readers exist: `wire::length_data_carry::read_value` (used by the
scan) and the inline carry in `OffsetTable::build`. Two copies of one rule is the defect class #426
removed. Unifying them is out of scope, because `OffsetTable::build` is the wire hot path and a
rewrite is bench-sensitive (the B13 lesson). So an instrument pins the agreement instead. One
remaining difference is unreachable: the carry re-arms after a counted value, and `OffsetTable` does
not. That matters only for a Data tag that is also a Length tag, which
`dict_hooks::data_tag_for_length` rules out, because a standard Data tag returns 0 there and a
field has one type. The corpus does not need a cell for it. A dictionary-declared pair that broke
that rule would be a loader defect, not a scan one.

**Alternative rejected.** Making the scan call `OffsetTable::build`: a second full parse per frame on
the hot path, for information the scan can produce itself.

## R-3 — One disposition step, right after the scan, branched inline

**Decision.** Right after the scan in each `on_inbound_frame` state arm (NotConnected, LogonSent,
LogonReceived/Active, LogoutSent), the arm tests `hdr.fault != field_fault::none` **inline**. Only
on a fault does it `co_await dispose_unparseable_(hdr, state)` and return. That is before the
validate gate and Guards 2–5 (contract C-1).
- **NotConnected** has no unconditional scan ahead of its guards today. It scans inside the
  validation block, and otherwise only after `interpret_logon` succeeds, so a refused Logon is
  never scanned. Its existing scan is hoisted to the top of the arm and reused by both later
  blocks. On the accept path the arm still scans once. On the refusal path it now scans once where
  it scanned zero times. That cost is new, is paid on the pre-Logon path only, and is stated here.

**Rationale.**
- FR-002 and clarification Q2 require the decision before any guard or handler. One function keeps
  the rule in one place, where today each arm re-derives its own handling, which is how #507's
  gaps arose.
- The branch is inline because a coroutine that returned "continue" on the clean path would add a
  coroutine frame to every inbound message. `validate_inbound_` was made synchronous for exactly
  that reason ("the PASS path … is coroutine-frame-free and alloc-free"), and `[const §VIII.5]`
  forbids heap work between parse and callback.

## R-4 — A residual parse failure is never "success" or "no reject" (C-2b; FR-016)

**Decision.** Owner ruling row 6 is read at face value: *any* parse failure, encoding or resource,
is never acted on. A parse can still fail on a frame the scan found fault-free:
- by a resource failure, which is **peer-reachable with a well-formed frame** (below);
- by a Framer re-feed failure;
- by an I-4 disagreement.

Each such failure takes the same contract rows, keyed on the scan's field 3 and 34 (C-2b), with
373=3. It is terminal at every inbound site.

**Resource failures are live traffic, not a backstop.** Both inbound parse sites use a stack arena
whose upstream is `std::pmr::null_memory_resource()` on every non-MSVC-debug build
(`fixpp::detail::arena_upstream`, `include/fixpp/core/pmr_arena_upstream.hpp`). `OffsetTable` grows
its entries in that arena. `tests/session/test_066_arena_fit_test.cpp` states the consequence: "an
overflow would surface as a parse failure, not a silent heap spill". The Framer admits frames far
larger than the arena can index, so a legitimate large message fails the parse. Today such an
application message is persisted, consumed and silently lost: #507's symptom by another trigger.
- **The ceiling is measured, not derived.** Task: find the smallest field count of a well-formed
  NewOrderSingle that fails `Parser<Index>::parse` in `kInboundParseArena`, on the Linux release
  and debug presets. Record it with the SHA in the B&L row (C-5 L-3). The cells use a real
  well-formed frame above it, so they need no test hook.

**Population (derive, do not copy).**
- `grep -n "parse_and_dispatch_(\|validate_inbound_(" src/session/session.cpp` lists every parse
  site. Read each call's enclosing branch and whether `check_inbound` ran before it.
- `parse_and_dispatch_`'s call sites split into three groups:
  - **inbound**: SequenceReset-Reset `fromAdmin`, too-low PossDup redeliver, knob-off deliver,
    knob-off GapFill, Logout `fromAdmin`, generic admin `fromAdmin`, `fromApp`;
  - **outbound**: `fire_to_admin_`, the toApp of a BusinessMessageReject, and the toApp in the send
    path;
  - **validate gate**: `validate_inbound_` in the NotConnected, LogonSent and LogonReceived/Active
    arms.
- The inbound sites and their rows are contract C-2b. The outbound sites parse fixpp's own frames
  and are unchanged.

**Paths that act on scanned fields and never parse** ("guarded by I-4 only"). Derive them by
reading each `hdr.msg_type == "…"` branch of the LogonReceived/Active arm
(`grep -n 'hdr.msg_type == "' src/session/session.cpp`), and each of the other arms, for a
`parse_and_dispatch_` that runs on every path through it. At the plan head these paths are:
- SequenceReset-Reset with no Application;
- GapFill with `validate_sequence_numbers` on, which persists and then applies `hdr.new_seqno`;
- Heartbeat, TestRequest and ResendRequest with no Application, or in LogonReceived, where the
  generic `fromAdmin` dispatch is Active-only;
- Logout with no Application;
- an inbound Reject;
- the LogoutSent arm;
- the Logon paths of NotConnected and LogonSent (`interpret_logon` is its own scanner).

On these paths an encoding fault is caught by the scan, provided I-4 holds. A resource failure
cannot occur, because nothing allocates a parse arena. That is **accepted**: running a full parse
on them to look for a failure would add the arena and the parse this design exists to avoid (R-1).
The consequence is disclosed as C-5 L-3.

**`validate_inbound_`.** Its "parse failed → `nullopt`" arms are literally ruling row 6's "reads as
no reject". It gains the "parse failed" outcome (data-model E-3). In all three arms that outcome
takes C-2b's validate-gate row: refusal pre-Active, and the C-2 row keyed on field 3 and 34 in
LogonReceived/Active. Its comment calling that case "validation passes" is corrected.

**Knob-off GapFill: the one deviation, recorded.** With `validate_sequence_numbers` off, the GapFill
branch has already advanced the in-memory counter through `check_inbound` when its `fromAdmin`
parse runs. Two choices were weighed:
- *roll the advance back* with `seqnum_mgr_.set_next_inbound`;
- *keep it*: persist, Reject, and never apply NewSeqNo.

**Decision: keep it.** Reasons:
1. The knob-off branch already treats a GapFill as a plain in-sequence message and never applies
   NewSeqNo. The "a SequenceReset never advances" rule exists to stop an unparsed SequenceReset from
   moving the counter through NewSeqNo, and that is not at stake here. The +1 comes from 34, which
   the ruling lets the session read.
2. Not persisting would leave memory and store disagreeing, which `persist_inbound_advance_` exists
   to prevent.

A non-advancing SequenceReset Reject under knob-off freezes the counter: every later frame is
out of sequence and is delivered without advancing. That is pre-existing, through #423's
SequenceReset row at the 041 validate gate, and D-4 inherits it with the knob off. It is not a
reason for this deviation. The knob-off D-4 cell (quickstart §1) pins that outcome.

This is reachable only with the knob off, an Application registered and a resource failure on a
well-formed GapFill. It is disclosed in the B&L delta.

**No log line.** `src/session/session.cpp` includes no logger (`grep -n "#include" src/session/session.cpp`),
and its "logged-then-proceed" comments denote `(void)` discards. The only operator-visible channel
is `emit_event(SessionEvent…)`. Adding a `SessionEvent` alternative would break every exhaustive
`std::visit` over the public variant (a C++ source break) for an observability nicety. So FR-006
and this section say "disregard" without a log. Disregarded frames are observable in tests by the
absence of any effect.

**Rationale.** FR-001's last sentence and ruling row 6: a parse failure must never read as
"dispatch succeeded" or "no reject".

## R-5 — Reject Text(58) and RefMsgType(372)

**Decision.**
- **The overload.** A new overload of `build_reject` (`include/fixpp/session/admin_messages.hpp`,
  public) takes a trailing `std::string_view text`, emitted as 58 when non-empty. The existing
  overload delegates to it with an empty text. `emit_session_reject_` gains the matching parameter.
- **The Text** is one of three **compile-time constant strings**: one for `malformed_tag`, one for
  `length_data_mismatch`, and one for the residual failure. It carries no offset and no peer bytes,
  so its length is fixed.
- **372.** The disposition passes `hdr.msg_type` as 372 only when it is no longer than the longest
  MsgType any shipped dictionary defines; otherwise it passes an empty 372, which `build_reject`
  omits. The bound is a named constant, and a unit test recomputes it from `dictionaries/*.xml` so
  it cannot drift. With 58 fixed and 372 bounded, no peer-controlled byte can make `build_reject`
  fail.
  - That matters because `emit_session_reject_` treats a build failure as success, and D-5 has
    already consumed and persisted the number, which would give "consumed, never Rejected": the
    silent-loss class 092 closes.
  - A cell sends a faulty frame with an over-long MsgType and asserts that the Reject is sent
    without 372.

**Rationale.** Additive and source-compatible. Rejects already emitted keep byte-identical output
(no 58), so no existing golden changes. The public-header change is a Gate A trigger in any case.
The pre-existing `emit_session_reject_` silent-success on a build failure caused by *local*
configuration is unchanged, and is out of scope.

## R-6 — Reason codes

**Decision.**
- `malformed_tag` → 373=0 (Invalid tag number), 371 omitted.
- `length_data_mismatch` → 373=5 (Value is incorrect (out of range) for this tag), 371 = the Length
  tag (clarification Q1; the ruling allows 5 or 6).
- The residual failure (R-4) → 373=3, 371 omitted. 3 is valid in the SessionRejectReason domain of
  FIX.4.2, FIX.4.4 and FIXT.1.1 (`dictionaries/FIX42.xml`, `FIX44.xml`, `FIXT11.xml`), and it is the
  existing fail-closed catch-all of `wire_error_to_session_reject_reason`.
  - Rejected: 99, which FIX.4.2 does not define.
  - Rejected: a per-profile code, two codes for one condition.

  The differentiator NEW-P2-F relies on is "3 with no 371", against "0" or "5 with 371".
- 372 = the MsgType read before the fault, bounded as R-5 says.

The session sets these codes directly from the scan's fault kind. `reject_reason_map` gains mappings
for the validator path only (R-7).

**Disclosed deviation: TC2020 Scenario 17d.** It says an invalid SignatureLength(93) value draws
373=8 (Signature problem). 93/89 is a Length+Data pair in the trailer, after 34, so under the owner
ruling's Length+Data code it gets 373=5. The ruling is not reopened. The deviation is a B&L row
(contract C-5 L-4) with a cell that pins 5.

## R-7 — FR-012: the field iterator reports its fault, the validator rejects on it

**Decision** (owner ruling 2026-09-27: kept in 092 and fully specified).
- **The iterator.** It gains the fault record of data-model E-4. Every stop and tolerance in
  `advance()` is enumerated there (S0–S4, T1–T3) from source. **What it yields does not change,**
  because `src/capi/message_read.cpp`'s `scan_slice_for_tag` walks group slices with it, and
  changing the yield would add C-ABI effects (R-8).
- **The validator** (data-model E-5) checks the fault before each field and after the loop.
- **New errors.** `core::error` gains `wire_invalid_tag_number = 132` and
  `wire_length_data_mismatch = 133` (E-6). `wire_error_to_session_reject_reason` maps them to 0 and
  5. `translate` maps both to `FIXPP_ERR_WIRE_INVALID_FRAME`.

**Every surface that enumerates or bounds `core::error` moves in the same change.** Derive the
population:
```
git grep -n "error_message(static_cast<error>(\|kEnumTable.size\|csv.size()" -- tests
git grep -ln "error_completeness\|expected_error_map" -- tests tools
git grep -n "case error::wire_" -- src/capi/error.cpp include/fixpp/wire/reject_reason_map.hpp
```
At the plan head it yields:
- `include/fixpp/core/error.hpp`: the enumerators and `error_message`'s switch;
- `src/capi/error.cpp`: `translate`'s total switch;
- `include/fixpp/wire/reject_reason_map.hpp`: two cases;
- `tests/capi/error_surface_test.cpp`: `kEnumTable` rows and its size pin, and the CSV row-count
  assertion;
- `tests/capi/expected_error_map.csv`: two rows;
- the "slot 132 is unknown" boundary pins in `tests/core/test_017_error_completeness.cpp`,
  `test_019_error_completeness.cpp`, `test_020_error_completeness.cpp` and
  `tests/capi/abi_symbol_golden_test.cpp`. Each moves to the slot after the 092 block, expressed from
  the last 092 enumerator rather than as a literal.

Also:
- a **new** `tests/core/test_092_error_completeness.cpp` asserts the 092 block is exactly
  `{wire_invalid_tag_number, wire_length_data_mismatch}` at `{132, 133}`, modelled on
  `test_020_error_completeness.cpp`, and is registered in `tests/core/CMakeLists.txt`;
- a `reject_reason_map` cell per new enumerator;
- where `include/fix/c_api/error.h` lists the core errors a C code coalesces (`grep -n "WIRE_INVALID_FRAME" include/fix/c_api/error.h`),
  that list is updated, and the freeze manifest is re-pinned with the rest of Phase 0b.

These pins fail loudly when missed (the compiler's `-Wswitch`, static_asserts, exact-set tests), not
silently. **Comments that state a count** are rewritten to state the condition, with no number:
`error.cpp`'s arm count and `reject_reason_map.hpp`'s "five validator-emitted slots" (parent
CLAUDE.md: a comment may record a procedure, not a result).

**Cells** (`tests/wire/`, `wire_dict_tests`-style grouped bucket):
- **Iterator.** One per S1–S4 and T1–T3 over a bare buffer, asserting `fault()`,
  `fault_length_tag()` and that the yielded sequence equals the pre-092 sequence. Each runs under
  the standard, dictionary-only and `none()` hooks.
- **Validator.**
  - One per fault kind over a view built with hooks that differ from the validator's (the only way
    to reach it). Each asserts the returned error and `*ref_tag_out`.
  - A T1 cell asserting `wire_invalid_tag_number`, not `wire_unexpected_tag`.
  - Clean controls over well-formed messages whose view uses the validator's own hooks, one
    carrying a standard Length+Data pair and one a dictionary-only pair. Each asserts the result is
    unchanged, so a spurious fault cannot pass.
- **C mapping.** `translate` of each new enumerator.
- **Fuzz** (`[const §VII.7]`). `tests/fuzz/fuzz_wire_validator.cpp` asserts both directions on
  every input, with a whole-buffer iterator walk under the same hooks as the `OffsetTable` build:
  - an `OffsetTable` encoding failure implies a non-`none` `fault()`;
  - an `OffsetTable` build that succeeds implies `fault() == none`.

  The second direction is the one that would hurt live traffic: a spurious fault would make
  `validate` reject a parseable message at the validate gate. A forced-miss arm cannot catch that
  (parent CLAUDE.md). Each direction is proven by one planted disagreement.

**Rationale.** Today the walk just ends, so every field after the stop goes unchecked: a fail-open
in a public API, although the session cannot reach it (R-3 decides first).

**Alternatives rejected.**
- Disclosing it as a limitation. The owner kept FR-012 in scope.
- Changing what the iterator yields. It adds C-ABI effects through `scan_slice_for_tag`, and a
  slice can legitimately end where a whole frame cannot.
- Splitting FR-012 into its own issue: the owner ruled against it.

## R-8 — C-ABI 1.10, BREAKING (owner ruling 2026-09-27; replaces the round-0 "no bump")

**Finding.** The round-0 position ("no C-ABI change; the receive callback was already not invoked")
was true for application frames and false for the Logon. Today:
- a Logon carrying a malformed tag (`9x9=1`) is **accepted**, because the validate gate's parse
  failure returns `nullopt`, read as "no reject", and `interpret_logon` skips the tag;
- the session establishes, and later application messages reach `cb`.

092 refuses that Logon (D-1/D-2) through the same refusal 091 FR-020 uses, and D-3 disconnects an
established session on a faulty Logon. 091 declared exactly that refusal **BREAKING (C-ABI 1.9)**
under `[const §X.7]` ("a call that used to succeed and now fails"). The owner ruled 1.10 BREAKING on
that precedent.

**Population recipe.** Re-run 091's *C-ABI 1.9 population recipe*
(`specs/091-data-field-bytes/research.md` R-11, steps 1–6) at the implementation head, with 092's
change as the input, and classify every export by its Classification paragraph. At the plan head
the recipe's step 6 (handshake observers) gives `fixpp_session_is_established`,
`fixpp_session_close`, `fixpp_session_send`, `fixpp_session_register_callback` and
`fixpp_session_register_send_callback`. The candidate effects to classify are:

| Effect | Proposed class |
|---|---|
| a Logon with a malformed tag, or any faulty frame, awaiting a Logon, which established a session, is refused (D-1/D-2) | BREAKING, on the five observers (as 091 FR-020) |
| a faulty Logon while Active disconnects the established session (D-3) | BREAKING where an observer's result changes (`is_established` true → false, `send` OK → `FIXPP_ERR_SESSION_INVALID_STATE`); `close` of a once-established, reaped session returns OK either way. The recipe confirms |
| a faulty frame whose fault precedes 34, in Active, used to disconnect; it now stays connected and later messages reach `cb` (D-7) | additive (a failure turned into a success). Recorded in the `version.h` history |
| a faulty Logout in LogoutSent no longer confirms, so `close` waits for the logout timeout | the return code is unchanged and the latency is undocumented, so additive, if the recipe confirms `fixpp_session_close`'s documentation leaves timing unspecified. Otherwise BREAKING on `close` |
| a faulty application message is Rejected instead of silently consumed | not C-visible (`cb` is not invoked either way). But `fixpp_session_register_callback`'s 1.9 clause says "dropped as a parse error, silently … no Reject is sent", so that sentence is rewritten |
| first-wins 34 on a **fault-free** frame with a duplicate 34 (R-1): e.g. `34=1|…|34=5` at expected 5 was in sequence under last-wins and is now too-low, so Guard 4 disconnects it (except a Heartbeat) | a BREAKING candidate where it ends an established session (`is_established` true → false, `send` OK → `FIXPP_ERR_SESSION_INVALID_STATE`); the recipe classifies it. It is also a B&L row (contract C-5 L-5) |
| residual rows (C-2b) through the validate gate | unreachable from C: `git grep -n validate_inbound -- src/capi` shows no C setter for inbound validation |
| residual rows at dispatch sites (e.g. a Logout residual now keeps the session up) | classified by the recipe |
| FR-012's validator errors | unreachable from C (no `src/capi` caller of the validator, R-7), so no C effect |

**What 1.10 touches.** Found from 091's bump (`git show --stat 932dd1cd`) and `[const §X.7]`. The
procedure is plan.md "Phase 0b":
- `include/fix/c_api/version.h`: `FIXPP_C_ABI_VERSION_MINOR` 9 → 10, and the history comment for
  1.10, which names every effect with no carrying declaration and every observer;
- `include/fix/c_api/session.h`: a **BREAKING (C-ABI 1.10; 092)** clause in each affected
  declaration's own documentation, and a rewrite of `fixpp_session_register_callback`'s 1.9
  sentence;
- `tools/capi_freeze.sha256`, re-pinned through `tools/check_capi_freeze.sh` after every header
  edit;
- `tests/capi/version_test.cpp`: the exact-version cell and `CompositeMacroValue` set to 1.10 and
  shown RED against 9 first (`[const §VII.3]`), plus a mutant back to 9 that must turn it RED;
- every in-repo consumer from
  `git grep -ln "VERSION_MINOR\|0x010900\|1_9_0\|(9U << 8U)" -- . ':!specs'`, each hit classified.
  Most compare against the macro and move with it;
- a `tests/capi` witness mirroring `length_data_logon_refusal_test.cpp`'s `CapiLogonMalformedCount`
  cells for a malformed **tag**, on every observer, both roles;
- the B&L delta (BREAKING, `[const §X.7]`); `brain/components/c-api.md`'s section for 1.10; the
  `spec/feature-catalogue.md` CA rows matched by 091's rule; and the PR description's BREAKING
  declaration.

**Not touched, and why.**
- **`CHANGELOG.md` is not a carrier.** Its scope note says it records backwards-incompatible
  constitutional amendments and released library versions, not features. `[const §X.7]` names it
  only for the first-public-release reset. 091's 1.9 bump did not touch it: `932dd1cd`'s
  `--stat` lists no `CHANGELOG.md`.
- **No error code is added,** so `introducing_minor` and `tools/abi_history/error_codes_v1.txt` are
  untouched.

**Preconditions.**
- `gh release list --exclude-drafts` is empty, which is the pre-release regime `[const §X.7]` needs.
- No other branch has already taken 10. After `git fetch --all --prune`, check
  `git grep -h "define FIXPP_C_ABI_VERSION_MINOR" $(git for-each-ref --format='%(refname)' refs/heads refs/remotes) -- include/fix/c_api/version.h | sort | uniq -c`.
  Positive control: `origin/main` shows 9. Re-run it right before the bump commit, because other
  sessions have PRs in flight.

**Python binding.** It exposes no inbound-validation or Reject surface. The version consumers the
grep finds in `bindings/` move with the macro.

## R-9 — Performance: a paired merge-base comparison (`[const §VIII.2]`)

**Finding.** Nothing benches the inbound session path today. `bench/session/fsm_bench.cpp` says the
inbound-dispatch path needs a fully wired Active session, and it measures only the outbound admin
emit. `bench/baselines/session/` holds several session baselines (list them with
`ls bench/baselines/session/`), none of them for the scan or `on_inbound_frame`.

**Decision.**
- **Benches.** Two benches land in a **bench-only commit first**, before any production edit, so
  they can be built in both trees:
  - `bench/session/scan_frame_header_bench.cpp`: clean frames (a Heartbeat, a NewOrderSingle, and
    a frame carrying a Length+Data pair) under the dictionary hooks and under `dict_hooks::none()`;
  - a clean Active `on_inbound_frame` bench: an in-sequence Heartbeat and NewOrderSingle through an
    established session, built on the session test support that already drives `on_inbound_frame`.
    It is the only instrument that can see the disposition's inline branch and any accidental
    coroutine-frame cost (R-3), which a scan-only bench cannot.
- **CI.** Both are added to `bench/ci-suite.txt`, with a tier-3 comparand, so the CI bench gate runs
  them. A bench missing from that list has no execution gate.
- **Local.** A local paired run follows `[const §VIII.2]`: a clean merge-base worktree carrying only
  the bench commit, and the candidate, built on one machine, run A-B-A-B, compared min-per-tree.
  The budget is +5%.
- The clean path adds one inline compare per frame and trades nothing else. The B13 lesson is to
  measure, not assume.

## R-10 — Tests that pin the old behaviour

The owner ruling revises these, so they are reviewed rather than silently flipped. Derive the
inventory:
```
grep -rln scan_frame_header tests
git grep -ln -i "no Reject\|NoReject\|Skipped\b" -- tests/session tests/capi
git grep -ln "VERSION_MINOR" -- tests
```
Classify each hit as "still holds, gains an assertion", "flips (RED first)", "renamed" or "mention
only". Snapshot at the plan head:
- `tests/session/scan_frame_header_overflow_test.cpp`:
  - The `ForgedTag*` / `Token*` cells still hold (the aliased field is not surfaced) and gain a
    fault-kind assertion.
  - `NonDigitToken_Rejected_NotDispatched`'s "conforming pair" half moves to its own frame, because
    the scan now stops at the fault.
  - `ConformingFrame_AllFieldsCorrect` still holds.
- `tests/session/length_data_session_scanner_test.cpp`:
  - `ScanFrameHeaderStopsAt…` still hold and gain fault-kind and Length-tag assertions.
  - The `LogonArmMalformedCount` and `InterpretLogonMalformedCount` cells are unaffected (their
    refusals are 091's).
- `tests/session/coverage_adversarial_test.cpp` `InboundFrameMalformedTagCharSkipped` and
  `InboundFrameTagWithoutEqualsSkipped` exist to cover the skip-and-continue branch 092 deletes.
  Their names become false, and they assert nothing, so they would not go RED. Rename them for the
  fault-record branch and give them assertions (D-8 for `X35=…` in field 3, D-7 or D-8 for the
  no-`=` field).
- `tests/session/test_test_request_id_cross_session_race.cpp`: mention only (a comment).
- `tests/fuzz/fuzz_session_recovery_admin_parse.cpp`: gains the R-2 arm.
- `tests/session/session_reject_test.cpp`:
  - `NoRejectLoopOnInboundReject` feeds a **well-formed** Reject, so it pins nothing about a
    malformed one and still holds.
  - `NoRejectOnMalformedLogout`'s "malformed" is a CompID mismatch on a well-formed frame, not a
    parse failure, so it is unaffected. Both stay, beside a new malformed-Reject cell (C-2) that
    asserts the Reject.
- `tests/capi/version_test.cpp`: flips to 1.10, RED first (R-8).
- The error-boundary pins: R-7.

## R-11 — The Framer does not check that 35 is the third field

**Finding.** `wire::Framer` checks BeginString, BodyLength and CheckSum, but not that MsgType(35) is
the third field. `grep -n "35\|MsgType\|third" src/wire/framer.cpp` finds no such check. Header
order is checked only by the validator's Step 0 (`wire_header_out_of_order` → 373=14), when inbound
validation is on. So FR-008's "keeps its current handling" is untrue for criterion 3 today: a
fault-free frame whose third field is not 35 is processed.

**Decision.**
- For a **faulty** frame, 092 decides criterion 3 itself. In LogonReceived/Active,
  `msg_type_is_third` false → D-8 (disregard), ahead of the 34-keyed rows (R-1). Before Active the
  frame is refused whatever its third field (D-1/D-2), because no pre-Active timer would end a
  disregarded connection (contract C-2 gives the derivation). In LogoutSent it is disregarded
  (D-9).
- A **fault-free** frame whose third field is not 35 is unchanged and out of scope: pre-existing,
  and a separate issue filed at close-out.

## R-12 — The two non-inbound scan callers (masking and replay)

**Finding.** `grep -n "scan_frame_header(" src/session/session.cpp` shows, besides the inbound state
arms and the LogonSent arm's 1137-Reject RefSeqNum read, two callers that are not inbound
disposition:
- the credential-masking gate, which reads only `msg_type == "A"`;
- stored-frame replay classification, which reads only `msg_type` through `is_admin_type`.

Both scan frames fixpp's own builders produced, where 35 is the third field. Stopping at a fault can
drop only fields *after* the fault, so `msg_type` could change only if field 1 or 2 were malformed,
and fixpp writes those. First-wins changes nothing for a frame with one 35.

**Decision.** No code change at these callers. One cell: a stored frame with a fault after its 35
still classifies as admin or application as before. `src/session/reconnect_fsm.cpp` names the scan
only in a comment.

## R-13 — Text that states the old behaviour (FR-014)

Derive the sites:
```
git grep -n -i "no Reject\|parse error\|reject-loop\|no-reject-loop\|reject-of-reject\|garbled" -- include src docs brain spec
```
Rewrite each site that describes a framed but unparseable frame. Known members at the plan head:
- `include/fix/c_api/session.h`: `fixpp_session_register_callback`'s 1.9 sentence (R-8).
- `include/fixpp/session/admin_messages.hpp`: the no-reject-loop sentence, scoped to well-formed frames, with
  a header comment naming the ruling.
- `session.cpp`'s `parse_and_dispatch_` and `validate_inbound_` comments ("skip, not fatal",
  "validation passes").
- The #423 ruling references at the validate gate. Each gains a pointer to the 2026-09-27 revision
  of row 4.
- `brain/components/session.md`, which calls #507's frames "garbled … contrary to §4.5.2". The ruling
  reverses that framing, so the passage is rewritten, not appended to.
- `brain/components/inbound-message-path.md`, `wire.md`, `errors.md` and `c-api.md`: their decision
  sections gain the ruling, the rejected alternatives (ignore-by-default; 99; changing the iterator's
  yield), and the 1.10 declaration.
