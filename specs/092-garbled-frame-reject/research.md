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
- It records, in the fault record only, the **first** well-formed 34 (`fault_ref_seq_num`) and the
  third field's 35 value (`fault_ref_msg_type`). The existing `msg_seq_num` and `msg_type` stay
  last-wins, unchanged.

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
- **The Reject's 34 comes from the fault record, not from `msg_seq_num` (Gate A round 2, root
  cause C).** `OffsetTable` keeps the first occurrence of a tag (its overlay insert skips a
  duplicate), and the Reject must not be addressed from a later 34, or `35=D|34=99|…|34=2|…|9x9=1` could
  be advanced at 2 (literal corrected at implementation, T034: the earlier `34=99|35=D|…` form
  puts 34 in field 3, which is D-8). But a faulty frame has no parse, so there is no "parser's 34" to agree with; the
  need exists only on faulty frames. So the first 34 is recorded in `fault_ref_seq_num`, read only
  when `fault != none`, and every clean frame is scanned exactly as today.
- **"34 read" is `parse_seqnum(fault_ref_seq_num) > 0`.** `34=abc` is non-empty but `parse_seqnum` returns 0
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
- *Make the whole scan first-wins* (the round-1 decision, reverted in round 2). It changed fault-free
  frames: `34=1|…|34=5` at expected 5 went from delivered to too-low and disconnected. That is an
  unruled BREAKING candidate for a need that exists only on faulty frames.
- *Detect a duplicate 34 as a fault, or send it to the disregard row.* The first changes clean
  frames again; the second reinterprets ruling row 5 ("34 was NOT scanned").
- (Round 1 rejected the adopted shape, a separate first-34 member, as "two answers for the frame's
  MsgSeqNum". That objection does not apply: the fault-record member answers a different question,
  and nothing reads it on a clean frame.)

## R-2 — The scan and the full parse must agree on what a fault is (C-3 I-4)

**Decision.** The scan's fault set equals the `OffsetTable::build` encoding-failure set (data-model
E-0 gives the derivation). Resource failures are excluded: `wire_offset_table_full` and
`out_of_memory`. For a fault-free frame, `fault_ref_seq_num` equals the value of the first `entries()`
element with tag 34, and `msg_type_is_third` equals "the third `entries()` element has tag 35". The
oracle is `entries()`, not `find(34)`: once an insert reaches `kMaxBuildProbe`
(`src/wire/offset_table.cpp`), the overlay leaves the occurrence unindexed while the build still
succeeds. Three instruments enforce this.

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
     - a duplicate 34 and a duplicate 35 (`fault_ref_seq_num` equals the first `entries()` 34;
       `fault_ref_msg_type` equals the third element's value; `msg_seq_num` and `msg_type` still
       equal today's last-wins values);
     - a fault-free frame whose third field is not 35 (`msg_type_is_third` false on both sides).
   - **Proof that it can fail, per mutation family.** In a scratch copy, seed one disagreement per
     family and watch that family go RED. The families are: the non-digit check, the overflow
     check, the empty-tag check, the no-`=` check, the non-SOH check, the end-equals-size check,
     the `fault_ref_seq_num` first-34 selection, and the `fault_ref_msg_type` third-field
     selection. One planted disagreement does not prove the others can fail.
2. **A fuzz assertion** in `tests/fuzz/fuzz_session_recovery_admin_parse.cpp`. The harness today
   builds a whole `Session` over a FIX.4.2 minimal dictionary and has no dictionary-only pair. The
   092 arm:
   - calls `scan_frame_header` and `OffsetTable::build` directly on each input, under both hook
     sets, with a dictionary that declares a dictionary-only pair;
   - traps (`__builtin_trap`) on any encoding disagreement and, for a fault-free input, on a
     `fault_ref_seq_num`, `msg_type_is_third` or `fault_ref_msg_type` disagreement with
     `entries()`;
   - skips **only** inputs whose `OffsetTable` status is `wire_offset_table_full` or
     `out_of_memory`, and counts them, so a fuzz run that skipped everything is visible;
   - is proven by one planted disagreement.

   Caveat #508: the fuzzers do not yet get coverage feedback from library code, so this arm is a
   backstop, not proof.
3. **The late-site close (R-4, contract C-6)** turns any disagreement that survives both into a
   terminal close, never "success". It is the last line. It cannot see a disagreement on a path
   that never parses (R-4), so the first two instruments are the only guard there.

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

## R-4 — A late parse failure is never "success": the session closes (contract C-6; FR-016)

**Decision (owner ruling O-2).** After step 3 (contract C-1), an inbound parse only ever sees a frame
the scan found fault-free. A failure there is a resource failure or an I-4 breach. Every late
inbound site takes one action: `close(close_mode::terminal)`, no Reject, and the parse target's
receive callback (`fromAdmin`/`fromApp`) not invoked (contract C-6 scopes it). There is no
per-site Reject table and no residual reason code. The proper disposition of a resource failure is
fixpp#515.

**Resource failures are live traffic, not a backstop.** Both inbound parse sites use a stack arena
whose upstream is `std::pmr::null_memory_resource()` on every non-MSVC-debug build
(`fixpp::detail::arena_upstream`, `include/fixpp/core/pmr_arena_upstream.hpp`). `OffsetTable` grows
its entries in that arena. `tests/session/test_066_arena_fit_test.cpp` states the consequence: "an
overflow would surface as a parse failure, not a silent heap spill". The Framer admits frames far
larger than the arena can index, so a legitimate large message fails the parse. Today such an
application message is persisted, consumed and silently lost: #507's symptom by another trigger.
After 092 it closes the session: fail-closed, disclosed (contract C-5 L-6) and classified for the C
ABI (R-8).
- **The ceiling is measured, not derived**, only to build the late-site cells: the smallest field
  count of a well-formed frame that fails `Parser<Index>::parse` in `kInboundParseArena` on the Linux
  presets the cells run on, recorded with the SHA. The per-lane measurement and the cap belong to
  #515.

**Population (derive, do not copy).** Contract C-6 gives the command and the classification rule:
every `parse_and_dispatch_` or `validate_inbound_` call whose bytes came from the peer is a late
inbound site, whatever arena it names; a call that parses a frame fixpp built is not. The validate
gate in every state arm is a late site; pre-Active its close amounts to a refusal.

**What a late close does not do.** It cannot undo what ran before the parse: Guards 2 and 3, the
Guard 4 advance, the resend-gap close, the liveness refresh, or the Logout handler's confirming
Logout, which is sent before its `fromAdmin` parse. No row or invariant claims otherwise. Moving the
decision ahead of those effects needs detection at step 3, which is #515's design.

**Paths that act on scanned fields and never parse** ("guarded by I-4 only"). Derive them by
reading each `hdr.msg_type == "…"` branch of the LogonReceived/Active arm
(`grep -n 'hdr.msg_type == "' src/session/session.cpp`), and each of the other arms, for a
`parse_and_dispatch_` that runs on every path through it. On these paths an encoding fault is caught
by the scan, provided I-4 holds. A resource failure cannot occur, because nothing allocates a parse
arena. Whether a large well-formed frame fails at all therefore depends on the path; that dependence
is #515's, not a disposition of this feature.

**`validate_inbound_`.** Its "parse failed → `nullopt`" arms are literally ruling row 6's "reads as
no reject". It gains the "parse failed" outcome (data-model E-3), which takes the C-6 close in all
three arms. Its comment calling that case "validation passes" is corrected.

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
- **The builder.** A new function, `build_reject_with_text(…, std::string_view text)`
  (`include/fixpp/session/admin_messages.hpp`, public), takes `build_reject`'s parameters plus a
  trailing `text`, emitted as 58 when non-empty. `build_reject` stays a single declaration and
  delegates to it with an empty text. `emit_session_reject_` gains the matching parameter.
- **The Text** is one of two **compile-time constant strings**: one for `malformed_tag` and one for
  `length_data_mismatch`. It carries no offset and no peer bytes,
  so its length is fixed.
- **372.** The disposition passes `hdr.fault_ref_msg_type` as 372 only when it is no longer than the longest
  MsgType any shipped dictionary defines; otherwise it passes an empty 372, which
  `build_reject_with_text` omits. The bound is a named constant, and a unit test recomputes it from `dictionaries/*.xml` so
  it cannot drift. With 58 fixed and 372 bounded, no peer-controlled byte can make
  `build_reject_with_text` fail.
  - That matters because `emit_session_reject_` treats a build failure as success, and D-5 has
    already consumed and persisted the number, which would give "consumed, never Rejected": the
    silent-loss class 092 closes.
  - A cell sends a faulty frame with an over-long MsgType and asserts that the Reject is sent
    without 372.

**Rationale.** Additive and source-compatible: a new name, and `build_reject` keeps its single
declaration, so no existing call or use of its name changes meaning. Rejects already emitted keep byte-identical output
(no 58), so no existing golden changes. The public-header change is a Gate A trigger in any case.
The pre-existing `emit_session_reject_` silent-success on a build failure caused by *local*
configuration is unchanged, and is out of scope.

## R-6 — Reason codes

**Decision.**
- `malformed_tag` → 373=0 (Invalid tag number), 371 omitted.
- `length_data_mismatch` → 373=5 (Value is incorrect (out of range) for this tag), 371 = the Length
  tag (clarification Q1; the ruling allows 5 or 6).
- A late parse failure (R-4) sends no Reject, so it has no reason code. (Round 1's 373=3 residual
  Reject is deleted: 3 means "Undefined tag", which a local resource failure is not.)
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
git grep -n "case error::wire_" -- src/capi/error.cpp include/fixpp/wire/reject_reason_map.hpp tests
```

*Widened at implementation (2026-09-28, T077):* the third command now covers `tests/`. The
narrower form missed `tests/fuzz/fuzz_wire_validator.cpp`'s `is_valid_wire_error` allowlist (a
`switch` of `case error::wire_…` labels), which was fixed in `ae787a3a`.

At the plan head, before that widening, it yields:
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
  - One per fault kind, reached through the public API. The malformed-tag cells construct
    `MessageView<Index>` directly and assert its failed `build_status()` before validating (a
    malformed tag fails `OffsetTable::build` under the view's own hooks. *At implementation, T062b:*
    differing hooks do reach it when the view's hooks pair a Length/Data whose counted value
    swallows the malformed field, so the build succeeds and the validator's walk meets it). The Length/Data cells build under one hook set and validate under a differing set. Each
    asserts the returned error and `*ref_tag_out`.
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
| a faulty application message is Rejected instead of silently consumed | not C-visible (`cb` is not invoked either way). But `fixpp_session_register_callback`'s 1.9 clause says "dropped as a parse error, silently … no Reject is sent", so that sentence is rewritten. The rewrite keeps the 1.9 record intact (the delivered-then-dropped change for component/group-only pairs, and the 091 FR-020 Logon half) and replaces only the "silently … no Reject is sent" outcome: `cb` is still not invoked, and the session answers with a session Reject under contract C-2 (D-5/D-6), or disregards the frame (D-7/D-8/D-9), with a pointer to the 1.10 clause |
| a late parse failure (contract C-6) through the validate gate | unreachable from C: `git grep -n validate_inbound -- src/capi` shows no C setter for inbound validation |
| a late parse failure at a dispatch site with a registered callback: a well-formed frame that exhausts the arena, silently consumed today, now ends the session (fail-closed, contract C-5 L-6) | a BREAKING candidate where it ends an established session (`is_established` true → false, `send` OK → `FIXPP_ERR_SESSION_INVALID_STATE`); the recipe classifies it |
| FR-012's validator errors | unreachable from C (no `src/capi` caller of the validator, R-7), so no C effect |
| an in-sequence inbound message at NextNumIn = seqnum_max ends the session (silent Disconnected) where NextNumIn used to wrap and the session continued (FR-019, R-14) | a BREAKING candidate where it ends an established session (`is_established` true → false, `send` OK → `FIXPP_ERR_SESSION_INVALID_STATE`); the recipe classifies it, and the `version.h` history names it |

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
- **Local.** A local paired run follows `[const §VIII.2]`: a clean merge-base worktree with only
  the bench commit cherry-picked onto it, and the candidate, built on one machine, run A-B-A-B,
  compared min-per-tree. Both SHAs and the bench commit's patch-id are recorded. The budget is +5%.
- FR-019 adds one more compare, in `SeqnumManager::check_inbound`'s in-sequence branch, which every
  in-sequence inbound message takes (R-14). The `on_inbound_frame` bench covers it; the scan bench
  does not.
- The clean path adds one inline compare per frame and trades nothing else. The B13 lesson is to
  measure, not assume.

## R-10 — Tests that pin the old behaviour

The owner ruling revises these, so they are reviewed rather than silently flipped. Derive the
inventory:
```
grep -rln scan_frame_header tests
git grep -ln -i "no Reject\|NoReject\|Skipped\b" -- tests/session tests/capi
git grep -ln "VERSION_MINOR" -- tests
git grep -ln "check_inbound\|set_next_inbound" -- tests    # FR-019: any cell that advances NextNumIn at seqnum_max
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
  frame is refused whatever its third field (D-1/D-2): both arms already refuse every input that is
  not a valid Logon (contract C-2 cites the source). The pre-Active disregard needs an establishment
  timeout and is fixpp#514 (owner ruling O-1). In LogoutSent it is disregarded (D-9).
- A **fault-free** frame whose third field is not 35 is unchanged and out of scope: pre-existing,
  and owned by fixpp#514.

## R-12 — The two non-inbound scan callers (masking and replay)

**Finding.** `grep -n "scan_frame_header(" src/session/session.cpp` shows, besides the inbound state
arms and the LogonSent arm's 1137-Reject RefSeqNum read, two callers that are not inbound
disposition:
- the credential-masking gate, which reads only `msg_type == "A"` on an **outbound** frame fixpp
  built. fixpp writes fields 1 and 2 of every outbound frame, so stopping at a fault cannot change
  its `msg_type`. No change.
- stored-frame replay classification (`app_present` in the resend store walk), which reads
  `msg_type` through `is_admin_type`. It classifies whatever bytes `MessageStore::retrieve` returns,
  and the store is a public interface: `build_replay_frame`'s own comment allows for a frame that
  "an older build or a custom MessageStore wrote". After stop-first, a stored **admin** frame with a
  fault in field 1 or 2 scans with an empty `msg_type`, `is_admin_type("")` is false, and it would
  be rebuilt and **resent** as application data, where today it is gap-filled. Resending an admin
  message breaks FIX-SL §4.8.3.

**Decision.** At the replay site, a stored frame whose scan read no 35 (`msg_type` empty) is not
`app_present`, so it is gap-filled: fail-safe, the same outcome as fixpp#424's unbuildable slot.
One cell stores an **admin** frame with a fault in field 2 through a custom store and asserts a
gap-fill and no resend. It is RED against stop-first without the guard. `src/session/reconnect_fsm.cpp`
names the scan only in a comment.

## R-13 — Text that states the old behaviour (FR-014)

Derive the sites:
```
git grep -n -i "no Reject\|parse error\|reject-loop\|no-reject-loop\|reject-of-reject\|garbled" -- include src tests docs brain spec
```
*Widened at implementation (2026-09-28, T072):* the pathspec now includes `tests`. The narrower
form missed test-file headers such as `session_reject_test.cpp`'s no-reject-loop scenario.

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

## R-14 — The inbound seqnum_max bound (FR-019; owner ruling, Gate A loop 2, round 2)

**Finding.** `SeqnumManager::check_inbound`'s in-sequence branch increments `next_inbound_` with no
bound. Only `assign_outbound` tests `seqnum_max`. Re-derive both with
`grep -n "seqnum_max\|++next_inbound_" src/session/seqnum_manager.cpp`. The header's "I-8: seqnum_max
overflow is session-fatal; no wrap" is therefore enforced for the outbound counter only.
`parse_seqnum` accepts 4294967295. `apply_inbound_sequence_reset` calls `set_next_inbound` for any
NewSeqNo(36) above the expected number. So a SequenceReset to 4294967295, then any in-sequence
message at 4294967295, wraps NextNumIn to 0 today. No 092 frame is needed.

**The wrap happens on a persistent store too.** The Opus loop-2 round-2 review assumed a persistent
store stops it: it read the store's `seqnum_max` check (`MemoryStore::next_seqnum`,
`FileStore::next_seqnum`) as firing first. It cannot fire here:
- `MessageStore` has no method that sets a counter. Re-derive with
  `grep -n "virtual.*awaitable" include/fixpp/session/message_store.hpp`: store, retrieve,
  next_seqnum, reset.
- So the SequenceReset jump is never persisted. `session.cpp`'s validate-on GapFill persist comment
  says so ("set_next_inbound, NOT +1 … is NOT persisted (INV-H1/D-5)").
- The durable counter stays at the value before the reset. `persist_inbound_advance_`'s increment
  after the wrap is an ordinary +1, and the session stays Active.

The store-kind row does not change the outcome today. Both store kinds get cells.

**Decision: one bound check, in `SeqnumManager::check_inbound`.**
- In the in-sequence branch, before `++next_inbound_`: if `next_inbound_ == seqnum_max`, return
  `core::error::store_seqnum_overflow` and leave the counter unchanged. This mirrors
  `assign_outbound`, which returns the same error for the same condition, and names it the same way
  (`[2e §6.7]`, I-8). Codex's counter-proposal is adopted: detect before mutating.
- **`set_next_inbound` gets no check, and it cannot wrap.** Its argument is a `seqnum_t` (32 bits,
  `include/fixpp/session/seqnum.hpp`), `parse_seqnum` caps at the same width, and it stores the value
  without arithmetic. seqnum_max is a representable NextNumIn, just as the outbound counter can hold
  seqnum_max. The bound fires at the next in-sequence advance. The other writers of `next_inbound_`
  also store values, not sums: `hydrate` (from the store), `reset_to_one` and `set_next_inbound`.
  Re-derive with `grep -n "next_inbound_ =\|++next_inbound_" src/session/seqnum_manager.cpp`. The
  only increment is the one this check guards.
- **SequenceReset and Logon never reach the advance through a Reject.** `consume_rejected_seqnum_`
  returns early for `A` and `4`. A GapFill in sequence does reach Guard 4's `check_inbound`, which
  holds the check.

**Disposition.** It is the silent transition to Disconnected, `record_state_transition_(fsm_state::Disconnected)`,
with no Reject and no Logout. The precedents:
- the outbound I-8 callers of `assign_outbound` (for example the initiator Logon in `Session::emit_initiator_logon_`,
  "Seqnum overflow — same disposition as build_logon failure");
- `persist_inbound_advance_`, when the store refuses the advance, including its own `seqnum_max`
  check.

A Logout was considered and not taken. FIX-SL 2020 defines no maximum sequence number. §4.1 says
each message received "consumes the next inbound sequence number, incrementing NextNumIn by 1", and
that resetting both numbers to 1 "shall constitute the beginning of a new FIX session". A wrap to 0
(not a valid MsgSeqNum) or to 1 (an unannounced new session) is neither. The session cannot
continue, so it ends. §4.6 says that termination by means other than the Logout exchange "should
be considered an abnormal condition and dealt with as an error". A NextNumIn that cannot advance is
such an error condition. The silent transition is the existing overflow disposition in both
directions, so 092 adds no new one.

**Callers: every `check_inbound` call must dispose of the new error.** Population:
`grep -n "check_inbound(" src/session/session.cpp`. Read each call's error branch. The members at
the plan head are below. This is a snapshot of the command, not an authority. Each needs one of two
dispositions:
- **`consume_rejected_seqnum_`** discards every `check_inbound` error as "not consumed", so its
  caller would go on to emit the Reject. It gains one branch: on `store_seqnum_overflow` it performs
  the Disconnected transition itself and returns the error. Its callers need no edit: each
  `co_return`s a failed result before its Reject, as it does for a failed persist today (re-derive
  with `grep -n "consume_rejected_seqnum_(" src/session/session.cpp` and read each `!c` branch). That
  covers every #423 Reject site and D-5, and keeps FR-013's ordering.
- **Guard 4 in LogonReceived/Active** routes a failed `check_inbound` by the frame, not by the error:
  - a Heartbeat is dropped silently;
  - a PossDupFlag=Y admin frame is ignored and an application frame redelivered (the two arms);
  - with `validate_sequence_numbers` off, the frame is delivered without advance;
  - only then comes the too-low fatal arm.

  Each of those would keep a session alive whose next number does not exist. So an overflow test
  comes first in that error branch, takes the Disconnected transition, and returns
  `store_seqnum_overflow` from `on_inbound_frame`, as `consume_rejected_seqnum_`'s branch and the
  outbound I-8 callers do (so the read pump stops; the too-low arm's `ok` return is not the model).
- **The acceptor Logon arm (NotConnected) and the initiator Logon-reply arm (LogonSent)** tolerate
  only `session_seqnum_too_high` with the next-expected knob on. Every other error, the overflow
  included, is already fatal. They need no edit, and they need a reading at the implementation head
  to confirm it.

A member the command adds at the implementation head is classified the same way: either it already
ends the session on any error, or it gains the overflow branch.

**Why not a check per call site.** The bound is one compare in the one place the counter is
incremented. The call-site edits only make sure the error is not misread. Without them, Guard 4
would drop or deliver the frame at seqnum_max, and `consume_rejected_seqnum_` would Reject it and
keep the session open.

**Public surface.** `SeqnumManager` is declared in the public `include/fixpp/session/seqnum_manager.hpp`.
- `check_inbound` gains a returnable error, which is source-compatible (contract C-4).
- Its header comment gains the overflow line and names 092/FR-019, per the parent rule on
  superseding a decision. The comment's "Caller is responsible for the session-fatal disposition
  (emitting Logout-with-text + disconnect) on any unexpected return" is **replaced**, not appended
  to: it is already false (the too-low fatal arm and the pre-Active arms disconnect with no Logout).
  The new text states per result: too-low and too-high keep their context-dependent handling at the
  caller; `store_seqnum_overflow` requires FR-019's silent Disconnected.
- The stale sentence in `seqnum_manager.cpp` that counts the callers is rewritten as the
  re-derivation command above, without a count.
- The C-ABI effect is classified in R-8's table.
- The behaviour is disclosed as contract C-5 L-7.

**Cost.** One compare on every in-sequence inbound message, measured by the `on_inbound_frame`
bench (R-9).

**Tests.** Each is RED first. For a D-5 cell, the RED baseline is a scratch copy of the 092 tree
with the bound deleted, because D-5 does not exist on today's tree. The Guard 4 and #423-site cells
are also RED on today's tree.
- **Unit** (`tests/session/seqnum_manager_test.cpp`, target `session_store_tests`; it needs no hook,
  because `set_next_inbound` is public): `set_next_inbound(seqnum_max)`, then `check_inbound(seqnum_max)`.
  It returns `store_seqnum_overflow` and `next_inbound_unsafe()` stays `seqnum_max`. A second cell
  asserts that `set_next_inbound(seqnum_max)` itself succeeds.
- **Session, per store kind** (quickstart §1 "Inbound seqnum_max bound"):
  - a non-persistent memory store, in `session_validation_compat_toggles`;
  - a FileStore, in `store_fail_reconcile`, whose fixture already drives a Session over a
    `FileStoreFactory`.

  Both are existing targets that already define `FIXPP_TEST_HOOKS`, because each cell reads the
  counter through `seqnum_mgr_test_access()`. There is no new hooked target (fixpp#511). Each cell
  sends a SequenceReset (Reset mode, 36=4294967295). It then asserts Active and NextNumIn ==
  4294967295, so that a disconnect from any other cause cannot satisfy the cell. Then it sends the
  frame at 4294967295 and asserts:
  - Disconnected;
  - NextNumIn still 4294967295;
  - no outbound frame after it, so no Reject;
  - no receive callback;
  - on the FileStore, a durable inbound counter the frame did not move. `Session` exposes no store
    accessor, so the cell reads it by reopening a `FileStore` over the fixture's `dir_` after the
    session closes and calling `next_seqnum(inbound, false)`. This sub-assertion adds no RED power
    on its own (with `consume_rejected_seqnum_` reverted the durable counter also stays); the
    Disconnected and no-Reject assertions are the discriminating ones.

  The frame at the maximum is, in turn:
  - an application message;
  - a Heartbeat;
  - a PossDupFlag=Y admin frame with a valid OrigSendingTime(122);
  - a #423 Reject site: a PossDupFlag=Y application frame with no OrigSendingTime(122), which the
    PossDup Stage-1 Arm C Rejects through `consume_rejected_seqnum_`. It needs no dictionary, so it
    runs in both fixtures, and today the session survives it, so Disconnected discriminates. The
    SendingTime site is not used, because it disconnects without the fix;
  - a faulty application frame (D-5).

  A `validate_sequence_numbers`-off arm seeds NextNumIn with `set_counters_for_test`, because with
  the knob off the Reset-mode SequenceReset is not applied. The seed passes the current
  `peek_outbound()` as the outbound value, because `set_counters_for_test` sets both counters.
- **Pre-Active** (memory store, `session_validation_compat_toggles`): after `open()` and before the
  Logon, seed NextNumIn at seqnum_max with `set_counters_for_test(seqnum_max, peek_outbound())`.
  Then send a Logon at 34=4294967295, as acceptor, and as the initiator's Logon reply where the
  fixture drives one. Assert Disconnected and NextNumIn still seqnum_max. Today the counter wraps
  and the session establishes, so the cell is RED on today's tree. It witnesses the "no edit needed"
  reading of the two pre-Active arms.
- **Deletion proofs**, each in a scratch copy:
  - delete the bound in `check_inbound`: every cell goes RED, the pre-Active cell included;
  - delete Guard 4's overflow branch: the Heartbeat, PossDup and knob-off cells go RED. The plain
    application cell stays green, because the too-low fatal arm catches it, so it is not the witness
    for that branch;
  - revert `consume_rejected_seqnum_`'s branch: the #423 and D-5 cells go RED (a Reject is sent and
    the session stays Active).
