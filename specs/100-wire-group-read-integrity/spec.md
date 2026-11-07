# Feature Specification: Wire group-read integrity — no silent loss, no exception across the C boundary

**Feature Branch**: `100-wire-group-read-integrity`

**Created**: 2026-11-07

**Status**: Draft

**Input**: REMAINING-WORK ★ V1.0 FUNCTIONAL TRACK row 7, wire half: batch B81 (fixpp#645, #644, #647) and batch B42
(fixpp#541). Four defects on the inbound read path where a present field or group is reported absent or empty, or where
an allocation failure leaves an `extern "C"` function as an exception.

## Context & Problem

An application reads an inbound message in one of two ways: the typed C++ view (`MessageView`, `group_view`), or the C
ABI (`fixpp_msg_get_*`, `fixpp_msg_get_group`, `fixpp_group_get_nested_group`) from a C application or any process
hosting the C ABI. (The Python binding does not expose the group getters: `bindings/python/fixpp.i` re-declares only
`fixpp_msg_get_string` from `message.h`.) Both read from one per-message offset table built at parse time, and group
reads allocate from the same bounded per-message parse arena.

Four conditions give an answer that is wrong, or no answer at all:

1. **Probe cap (#645, L-458-1).** Building the table's hash index caps each insert's probe length (a DoS bound against
   crafted hash collisions). When the cap is hit, the occurrence is left out of the index **and the build still reports
   success**, so the parse succeeds and a field that is on the wire reads as absent. The per-insert cap is also reached
   by frames nobody crafted: a frame of a few hundred or more distinct, non-consecutive tags reaches it for a small
   fraction of process seeds (measured at Gate A round 1; recipe in research D1).
2. **Top-level group, arena exhausted (#644, L-073-1).** A top-level repeating-group read that cannot allocate its
   instance slices returns an empty group with no failure flag. 073 made the **nested** read fail-loud; the top-level
   read on both paths (the typed `group<>()` and the C-ABI `fixpp_msg_get_group`) still cannot tell "no instances" from
   "could not read".
3. **Zero-count group (#647, L-051-3).** A group whose count field says `0` reads as **not a group** on the C ABI. The
   group check looks at the field after the count tag; with no instances that field (a later body field, or
   CheckSum(10), which is always the root table's last entry) is not a member, so the group read reports absent and the
   flat getter reports `TYPE_MISMATCH`. FIX permits a zero count, a validating session accepts it (TC-018, B-004-7),
   and QuickFIX C++ and J read it as a present group with zero instances, deciding group-ness from the dictionary's
   registration of the count tag (`Message::setGroup` → `DataDictionary::getGroup`; `Message.parseGroup` →
   `dd.getGroup`). **Ruled v1.0 BUILD**, parent repo `research/reviews/ruling_B81_647_zero_count_group_v1.md`.
4. **Cursor shell allocation (#541, B42).** Both C-ABI group getters allocate the returned cursor from the parse arena
   with nothing to catch a failure, so `std::bad_alloc` leaves the `extern "C"` function (undefined behaviour for a C
   caller; under the C-ABI application wrapper the process aborts). Reproduced at `d51ce86d` (branch `b28-repro`,
   `e10c8b80`): after the first read the slices come from a cache, so each repeat call allocates only the shell, and
   call 188 throws on a frame at the inbound limit. Still no handler at `fa778a8e`.

Behind #647 and the getters' inconsistency is one root cause: "is this tag a group?" is never asked as a question of
its own. A group read's success
stands in for it, and that success depends on the count field's position (a count field that is the last entry of the
table it is read from reads present, whatever the tag is), on the field after it, and on whether an allocation
succeeded. So today's C-ABI codes are also inconsistent between the two getters: when the first read of a group does
not fit, the flat getter returns `TYPE_MISMATCH` and the nested one `WIRE_LIMIT_EXCEEDED` (pinned by the shipped
`CapiInboundFrameDispositionsQ33.*` cells); a nested tag that is present but not a group returns `OK` with count 0 while
the flat getter returns `TYPE_MISMATCH`; and a scalar that is the last field of a group instance reads as an empty group.

## Clarifications

### Session 2026-11-07 (orchestrator, owner-delegated)

- Q: Is #647 in v1.0? → A: Yes (ruling note above). Group-ness comes from the dictionary's registration of the count tag
  in its context, never from the value `0`.
- Q: Should the arena be sized or grown so these failures cannot happen? → A: No. The bounded, null-upstream arena is the
  DoS property (073's ruling, kept). This feature makes each failure observable; it does not move the bound.
- Q: Which ending does the probe cap take? → A: The parse fails with a wire-limit error, the ending a frame whose offset
  table is over capacity takes today (`wire_offset_table_full`, C-ABI `FIXPP_ERR_WIRE_LIMIT_EXCEEDED`; on an inbound
  session, 092's late-parse-failure terminal close: no Reject, no Logout, no callback). ~~The DoS bound stays: the build
  still stops probing at the cap.~~ *Superseded by Gate A round 1 (below, "What does the probe bound count?"): the
  bound becomes a per-build budget; the ending is kept.* A linear-scan fallback on a miss was rejected: it is the O(n²)
  the cap exists to stop.
- Q: How is the C-ABI change versioned? → A: FR-003 changes an answer a caller can observe for the same input (nested
  scalar `OK` → `TYPE_MISMATCH`), and FR-005/FR-007 change the flat getter's code on exhaustion (`TYPE_MISMATCH` →
  `FIXPP_ERR_WIRE_LIMIT_EXCEEDED`). Before the first public release a breaking C-ABI change bumps MINOR and is declared
  BREAKING (`[const §X.7]`): C-ABI **1.15, BREAKING**. No new error code. *(Population widened at Gate A round 1: see
  "Which declarations does 1.15 BREAKING cover?")*
- Q: Does a zero-count group return a cursor? → A: No: `OK`, count 0, `*group_out` null, as the nested getter already
  answers a nested zero-count group today.

### Session 2026-11-07 (Gate A round 1)

- Q: What decides that a tag is a group? → A: One predicate, *registered*, asked once per read in the table's stored
  context `(msg_type, parent_path)`: true when the dictionary has a **context record** for `(msg_type, parent_path,
  no_tag)` (members or delimiter; the record's delimiter may be 0), or when it has no context record for `no_tag` in
  **any** context and registers `no_tag` in its bare store. The second arm honours bare-only registrations: hand-built
  fixtures, B-384-2's members-only fixture, and 099's NoHops(627) on a route table whose header does not declare it
  (`Dictionary::as_table_view`, the `route_table` arm). A tag with context records elsewhere and none here is not a group
  here (for example NoPartySubIDs(802), registered under `[453]`, read at the top level). The count's value, its
  position and the field after it never decide group-ness. The validator's own group detection is unchanged (FR-009);
  its remaining context-miss fallback is a lead to file, not fixed here. *Amended by Gate A round 2 (below, "Which
  contexts does the predicate cover?"): a header arm is added, and arm 1 is "a context key exists" (superseded by Gate A round 4 hand-edit (owner, 2026-11-07): research D2's key set). The predicate's
  arms are stated once, in research D2.* *The header arm is replaced by the section arm: superseded by Gate A round 3
  hand-edit (owner, 2026-11-07), research D2.*
- Q: What does a registered count tag whose instances do not follow read as? → A: A present group with zero instances
  read (`OK`, count 0), whatever the declared count: declared `0`, a count field that is the last entry of its instance,
  or a declared `n ≥ 1` whose next field is not a member (a lying count). QuickFIX C++ (`Message::setGroup`) and J
  (`Message.parseGroup`) decide from `getGroup` and set the count field, so a lying count reads as a present group with
  0 parsed instances there too. Flagging the mismatch is the validator's job.
- Q: What does the probe bound count? → A: The build's **total** probe work: `kMaxBuildProbe` × occurrences per build,
  the same constant, in place of a per-insert cap. Exceeding it fails the build with `wire_offset_table_full` (the
  ending above). Reason: the per-insert cap is reached by non-crafted frames (Context item 1), where a terminal close
  of the session would repeat on every resend while the process seed lives; the per-build budget is not reached by them
  (measured, research D1) and is still reached by a crafted window cluster under a known seed. The worst-case build
  stays linear in occurrences, so the DoS bound is unchanged.
- Q: Which declarations does 1.15 BREAKING cover? → A: The two group getters, and the session observers whose 1.11
  clause says "a frame the session admits always parses for dispatch": a crafted frame can now fail the parse, so that
  promise is amended in place in `include/fix/c_api/session.h` and in the 1.11 entry of `include/fix/c_api/version.h`,
  with 093 contract C-3 I-1, I-4 and I-5 and B&L B-093-6, B-092-6 and L-092-6.
- Q: Which code does an absent nested tag answer when the arena is exhausted? → A: `TAG_NOT_FOUND`, as on an ample
  arena. 073 FR-007 says the allocation-failure signal is raised "only for a genuinely present group … never for an
  absent group"; 073's placement of the failure check before the presence probe was an implementation step, not a
  ruling. ~~The wire layer decides presence with an allocation-free scan of the instance whenever a nested read fails,
  so a failed read always means a present tag.~~ *Superseded by Gate A round 2 (below, "Which answer does a present
  scalar take when a nested read fails"): registration is asked first, then presence.*
- Q: What does `group_view::alloc_failed()` mean once a group wire limit (`wire_group_too_large`, a nested
  sub-table's `wire_offset_table_full`) is a failed read too? → A: It keeps its meaning (an allocation failed).
  `group_view` gains `read_failed()`: the read did not complete, for an allocation or a wire limit. Typed readers that
  need "complete or not" check `read_failed()`.
- Q: Is a failed read cached? → A: No. A group read caches a row only when it completed: a group with its slices
  materialized, or a not-a-group answer. A failed read caches nothing, so a repeat read recomputes and fails again with
  the same code. ~~A cache insert that fails never changes the answer: the computed result is served uncached.~~
  *Superseded by Gate A round 2 (below, "What does a cache row that does not fit do?").*
- Q: A dict-aware hooks bundle without the registration callback? → A: The read declines as for a dict-free table
  (fixpp#220): no group. `dict_hooks::for_table_view` always installs the callback; only the test seam
  (`dict_hooks_test_access`) can build the other shape, and its callers pass the callback.
- Q: A nested sub-table is built with the default `OffsetTable::Config`, whatever the root's caps. Inherit the root's
  caps? → A: No, not in this feature: it changes the caps a nested read runs under, a separate behaviour. The
  sub-table's `wire_offset_table_full` is a failed read (research D7) and is witnessed; inheriting the root's
  `Config` is recorded as a lead.
- Q: A null pointer argument to a getter? → A: One rule for both getters. If either out-parameter pointer is null, the
  getter returns `NULL_HANDLE` and leaves the outputs untouched. If both are non-null, it writes null/0 to them before
  any other check, including a null or invalid handle and a bad instance index. The flat getter already does this; the
  nested getter today checks `g` together with the out-pointers and returns before writing them, and changes to match.
- Q: A nested read whose instance sub-table build failed for a reason other than an allocation or a wire limit (073's
  malformed-data degradation)? → A: Not failed (073's scope) and not established as a group: `TYPE_MISMATCH` for a
  present tag (contract row 3). Its reachability is determined in the RED phase.

### Session 2026-11-07 (Gate A round 2)

- Q: Which answer does a present scalar take when a nested read fails (no sub-table, a sub-build out of memory, a
  sub-table over its entry cap)? → A: `TYPE_MISMATCH`, as on an ample arena. On every failure the wire layer asks the
  registration predicate first, in the call's context and through the caller's hooks; a tag that is not registered is
  answered `ok`, not a group, and only a registered tag goes on to the presence scan. A failed read therefore means a
  present, registered tag (data-model `nested_slices_result`).
- Q: Which contexts does the predicate cover? → A: Those QuickFIX looks a group up by. A body group is a group only
  where the dictionary declares it for that message type (QuickFIX `DataDictionary::getGroup(msg, field)`), so a body
  group read in a message that does not declare it, or in a message type the dictionary does not declare, reads
  `TYPE_MISMATCH`, where today the bare-store fallback reads it as a group. That is a declared BREAKING change. ~~A header
  group at the top level is a group in every message type, declared or not (QuickFIX C++ `_header_`, QuickFIX/J
  `HEADER_ID`), as it is today.~~ *Superseded by Gate A round 3 hand-edit (owner, 2026-11-07): a header or trailer
  group, at any depth inside its section, is a group in every message type, declared or not (QuickFIX C++ `_header_` /
  `_trailer_`; QuickFIX/J `HEADER_ID` for the header), as it is today.* Arm 1 holds when a context key exists for the tag, whatever its record holds (members,
  delimiter, or required members only). *(Arm 1 reads research D2's key set, which includes per-occurrence delimiter
  records with no `group_ctx_` key: superseded by Gate A round 4 hand-edit (owner, 2026-11-07).)* The arms are stated in research D2.
- ~~Q: Is a trailer arm needed? → A: No shipped dictionary declares a group in its trailer. This is a condition the D2
  sweep re-checks (research D9), not a count.~~ *Superseded by Gate A round 3 hand-edit (owner, 2026-11-07): the
  section arm covers header and trailer groups at every depth, from records the loaders keep (research D2).*
- Q: Is a nested `wire_group_too_large` reachable? → A: Not through a shipped construction. A sub-table's entry cap
  equals the per-instance cap, so the sub-table fails its entry cap first, and the depth of a nested walk is bounded by
  the parent read that completed over the same bundle and context chain. The status mapping is kept and marked
  defensive. The nested limit witness is a sub-table's `wire_offset_table_full` (research D7).
- Q: What does a cache row that does not fit do? → A: For a registered group with instances, the flat read pushes its
  row before it allocates the slices. If the push fails, the read fails (`alloc_failed`); if the slices then fail, the
  row is popped. An answer that needs no slices is served uncached when its push fails, and recomputing it allocates
  nothing. The nested cache's row is pushed before the sub-table is built (research D10).
- Q: Do the `group_view` constructors change? → A: The `bool` constructors are kept as they are. Two status
  constructors are added with no default argument. Renaming the result structs' `alloc_failed` field to `status` is a
  C++ source change, declared in the B&L delta.
- Q: Is the registration answer folded into the delimiter callback? → A: No. It is a separate `noexcept` callback
  (research D2; the reason is in plan.md `### Round 2 — disagreements`).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - A zero-count group reads as a present, empty group (Priority: P1)

A C application receives a message carrying a repeating group with `NoXxx=0`, followed by more fields (for example
`268=0` in a market data message, then further body fields), and asks for the group.

**Why this priority**: Reachable by any peer with ordinary, valid traffic, on a message the session has just accepted.
The application today gets an error for a valid message and cannot read the group's count.

**Independent Test**: Parse a frame with a zero-count group in mid-body, and one with the zero-count field as the last
body field (followed only by CheckSum), and read the group through each read path.

**Acceptance Scenarios**:

1. **Given** a message with a group count tag registered in its context holding `0` followed by more body fields,
   **When** the application calls the flat C-ABI group getter, **Then** it gets `OK` with count 0.
2. **Given** the same message with the zero-count field as the last body field, **When** the flat getter is called,
   **Then** `OK`, count 0.
3. **Given** a group instance containing a nested group whose count is `0` followed by more members, **When** the nested
   getter is called, **Then** `OK`, count 0.
4. **Given** a scalar field (not registered as a group count in that context) whose value is `0`, **When** either getter
   is called for it, **Then** `TYPE_MISMATCH`, as for any scalar, including a scalar that is the last field of its
   group instance.
5. **Given** a message with no such count tag, **When** either getter is called, **Then** `TAG_NOT_FOUND`, unchanged.
6. **Given** any of these frames on the typed C++ path, **When** the group is read, **Then** the result has size 0 and
   is not failed, and the count field itself reads `0`; the fields after it read as ordinary fields.
7. **Given** a registered count tag holding `n ≥ 1` followed by a field that is not a member (a lying count), **When**
   either getter is called, **Then** `OK`, count 0.
8. **Given** a nested group's count tag (registered only under its parent's path) present in a message, **When** the
   flat getter is called for it, **Then** `TYPE_MISMATCH`, whatever its value.
9. **Given** a body group's count tag in a message whose dictionary entry does not declare that group (for example
   NoPartyIDs(453) in a declared message without Parties), or in a message type the dictionary does not declare,
   **When** the flat getter is called, **Then** `TYPE_MISMATCH`, whatever its value.
10. **Given** a header or trailer group, or a group nested in one (for example NoHops(627) in a FIX 4.3, FIX 4.4 or
    FIXT 1.1 frame), in a message type the dictionary does not declare, **When** the flat getter is called, **Then** it
    reads as a group: with n ≥ 1 instances, `OK` with a cursor, as today (control); with count 0, `OK`/0, as
    scenario 1 (research D9 (ii-0), (ii-0h)).
11. **Given** any frame of scenarios 1 to 3, or a scalar of scenario 4, **When** the same read is made a second time on
    either path, **Then** the second answer is the first one.

---

### User Story 2 - A group read that runs out of arena says so (Priority: P1)

An application reads a top-level repeating group from a large inbound message whose parse arena cannot hold the group's
instance slices, or repeatedly fetches a group cursor until the arena is spent.

**Why this priority**: Silent loss (an empty group for a present one) and an exception across `extern "C"` (a process
abort for a C application) are both outcomes the C-ABI contract forbids.

**Independent Test**: Force the top-level slice allocation to fail, and separately exhaust the arena with repeated
getter calls, on both getters and on the typed path.

**Acceptance Scenarios**:

1. **Given** a top-level group whose slice allocation fails, **When** the typed `MessageView::group<>()` read runs,
   **Then** the result reports `alloc_failed() == true` and `read_failed() == true`.
2. **Given** the same frame, **When** the flat C-ABI getter is called, **Then** it returns
   `FIXPP_ERR_WIRE_LIMIT_EXCEEDED`, the code the nested getter returns for the same cause.
3. **Given** an arena that a repeat call exhausts while allocating the cursor, **When** either getter is called, **Then**
   it returns `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` and no exception leaves the function.
4. **Given** an ample arena, **When** a group is read many times, **Then** every call succeeds as today (control).
5. **Given** an exhausted arena, **When** either getter is called for a tag absent from the message (or instance),
   **Then** `TAG_NOT_FOUND`; **and when** called for a present scalar, **Then** `TYPE_MISMATCH`, whatever stopped the
   nested read (no sub-table, a sub-build out of memory, a sub-table over its entry cap). Neither answer needs memory.
6. **Given** a read that failed, **When** the same group is read again, **Then** it fails again with the same code.

---

### User Story 3 - A parse never succeeds with a field missing from its index (Priority: P2)

A peer sends a frame crafted so that the index build's total probe work exceeds its budget.

**Why this priority**: Needs a hostile frame crafted against the process seed; but the result today is a field that is
on the wire and reads absent on a parse that reported success, the silent-loss class this project treats as real.

**Independent Test**: Under a test seed, build a frame whose tags form a window cluster in the index (research D9) and
parse it; separately parse frames of many distinct, non-consecutive tags that today reach the per-insert cap.

**Acceptance Scenarios**:

1. **Given** such a crafted frame, **When** it is parsed, **Then** the parse fails with a wire-limit error and no field
   reads absent on a successful parse.
2. **Given** a frame whose total probe work is within the budget (including one that reaches today's per-insert cap),
   **When** it is parsed, **Then** it parses and every tag is indexed.
3. **Given** such a crafted frame on an inbound session, **When** it arrives, **Then** it takes the disposition a frame
   whose offset table is over capacity takes: 092's late-parse-failure terminal close.

### Edge Cases

- A zero-count group whose count tag is **also** used as a scalar in another context of the same message: group-ness is
  decided per context (message type and parent path) by the registration predicate.
- A declared count of `0` followed by fields that **are** members of the group (a lying count): the read follows the
  declared count, as today for any declared count; flagging the mismatch is the validator's job.
- A nested zero-count group that is the last field of its instance (`NestedGroupEmptyGroupCountLastField`): unchanged,
  `OK` with count 0.
- A count field that is the last entry of the table it is read from occurs only inside an instance slice: the root
  table's last entry is always CheckSum(10). The flat getter for tag 10 stays `TYPE_MISMATCH`.
- Arena exhaustion on the **first** read versus a **repeat** read of the same group: both return the same code.
- The probe budget and a dict-free table: the budget applies to every table; a dict-free table has no group reads
  (fixpp#220 declines them), so only the index arm applies.
- A group read in a context the dictionary does not declare (US1.9, US1.10): a body group is not a group there; a header
  or trailer group, or a group nested in one, is, at its path inside its section.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001** (#647): A tag is a group, for every group read on both paths, exactly when the registration predicate
  (research D2) holds for it in the read's context (message type, parent path). The value of the count, the count
  field's position, the field that follows it and whether an allocation succeeded do not decide it.
- **FR-002** (#647): A registered group count tag whose instances are not on the wire (declared `0`, a count field last
  in its instance, or a lying count) reads as a present group with zero instances: C-ABI `OK` with count 0 on both
  getters, and an empty, not-failed `group_view` on the typed path, wherever the count field sits in the message or
  instance.
- **FR-003** (#647): A tag present in the message (or instance) that is not registered in the read's context reads
  `TYPE_MISMATCH` on **both** C-ABI getters, wherever it sits, whatever its value, and on an ample or an exhausted
  arena. (Today the nested getter answers `OK`, count 0.)
- **FR-004** (#647, 073 FR-007): A tag absent from the message (or from the instance, for the nested getter) reads
  `TAG_NOT_FOUND` on both getters, on an ample or an exhausted arena. A failed read is reported only for a present,
  registered tag.
- **FR-005** (#644): A top-level group read whose slice allocation fails reports the failure:
  `group_view::alloc_failed()` and `read_failed()` on the typed path, and `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` on the flat
  getter. Never an empty group reported as success.
- **FR-006** (#541): Neither C-ABI group getter lets an exception leave it. A failed cursor allocation returns
  `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` with `*group_out` null and `*count_out` 0.
- **FR-007** (#541, #644): The two getters return **one** code, `FIXPP_ERR_WIRE_LIMIT_EXCEEDED`, for a read that could
  not complete, whatever caused it (slices, sub-table, cache row, cursor shell, a group wire limit) and whether it is
  the first or a repeat read. The flat group cache (`group_index_`) never holds a failed read. A cached nested row is
  re-resolved through the same failure rule as a first read (research D11), so a repeat answers as the first read did.
  A completed read's repeat fails only where the getter allocates a cursor shell.
- **FR-008** (#645): The index build's total probe work is bounded by the same constant per occurrence
  (`kMaxBuildProbe` × occurrences). A build that exceeds the budget ends in a failed build status
  (`wire_offset_table_full`), so the parse fails with a wire-limit error; a build within it indexes every distinct tag.
  Build cost stays linear in the number of occurrences.
- **FR-009**: A frame that does not reach any of these conditions reads exactly as today: same fields, same group
  instances, same codes, except the cells named in the contract's "Changes from 1.14". No new allocation on the read
  path; the per-sub-table allocation grows by one function pointer (research D2), and bands tuned to
  `sizeof(OffsetTable)` are re-measured per their headers. No change to the arena's size or upstream. The validator's
  acceptance is unchanged.
- **FR-010**: The B&L rows L-458-1, L-073-1 and L-051-3 move to the closed file with the delivering witnesses named;
  L-093-17 is retired to match FR-006; L-093-5's group bullets (empty span, the flat getter's `TYPE_MISMATCH`
  "misreport") and L-065-2's "top-level reads remain silent" clause are updated to the new endings; B-093-6, B-092-6
  and L-092-6 ("unreachable for a frame the session admits") are amended in place for the crafted-frame case. Every
  changed C-ABI ending is documented in `include/fix/c_api/message.h` and `.specify/2i-capi.md`, and the C-ABI version
  bumps per `[const §X.7]` (1.15, BREAKING, marked on each affected declaration).
- **FR-011** (found at plan, research D5): A group read stopped by a group wire limit reports a failed read:
  `read_failed()` true and `alloc_failed()` false on the typed path, `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` on the getter,
  on the first and every repeat read. At the top level the limit is `wire_group_too_large` (the per-instance entry cap
  or the nesting depth), on the flat getter. At a nested level it is the sub-table's `wire_offset_table_full`
  (research D7), on the nested getter. A nested `wire_group_too_large` is mapped the same way but is defensive: no
  shipped construction reaches it (research D5).

### Key Entities

- **Group registration**: the dictionary's record that a tag is a repeating group's count in a given context (message
  type, parent path). Which records count is research D2's predicate.
- **Group read result**: the instance list, whether the tag is a group (`is_group`), and the read status (`ok`,
  `alloc_failed`, `limit_exceeded`); the status is what separates "zero instances" from "could not read".
- **Arena-exhaustion code**: `FIXPP_ERR_WIRE_LIMIT_EXCEEDED`, the one C-ABI error a group getter returns when its read
  could not complete.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Every cell named in the acceptance scenarios and in the contract's "Changes from 1.14" is RED on
  `fa778a8e` where today's behaviour differs (US1.1, US1.2, US1.4's nested scalar mid-instance and last in instance,
  US1.7, US1.8 and US1.9 with `n ≥ 1`, US1.11's twins of those cells, US2.1, US2.2, US2.3, US2.5, US2.6, US3.1, US3.2's
  per-insert-cap frame, and research D9's section-arm cells (i-a), (ii-0), (ii-0h) and (ii-0-O)) and GREEN on the feature head; the control
  cells (including US1.3, which today's nested getter already answers `OK`/0 through its presence probe, US1.10 with
  n ≥ 1, and research D9's section-arm cells (i-b), (i-c), (ii), (iii), (iv), (v), (ii-O) and (iii-O), each proven live by its mutant) are GREEN on
  both. The verify record names, per cell, the lanes on which it shows RED and those on
  which it skips (the arena-exhaustion cells assert only where the arena's upstream is null; MSVC debug forwards to the
  heap).
- **SC-002**: The B42 repro shape (repeat getter calls on a frame at the inbound limit) completes every call with
  `FIXPP_ERR_OK` or `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` and no exception, on both getters.
- **SC-003**: No existing test changes its expected outcome except the cells that pin today's defects (named in the
  plan with the requirement that supersedes each), and the inbound zero-allocation gates stay green. The checked-in
  generated goldens that carry the nested accessor text (`specs/003-dictionary-codegen/contracts/golden/` and
  `specs/076-fix-latest-typed-codegen/contracts/golden/`, found by the recipe in research.md's census) are live oracles
  of the codegen tests; they are regenerated by the emitter, never hand-edited, and their accessor-text change is the
  named exception.
- **SC-004**: For a frame whose count tag is registered in its exact context, a validating session that accepts a
  zero-count group and the C-ABI read of the same frame agree: the group is present with count 0.

## Assumptions

- FR-001's predicate needs a dictionary structure that does not exist today: a per-tag "has a context key" bitset in
  `table_view`, set wherever a context key is created, and one `dict_hooks` callback that asks it. `table_view::group_bit`
  is set by the bare and the `_ctx` setters alike, so it cannot answer the bare-only arm. The section arm reads a new
  MsgType-free section-group store, recorded by the loaders (research D2). Arm 1 reads a registration-only key set
  filled from the per-message delimiter records (research D2; Gate A round 4 hand-edit (owner, 2026-11-07)). The callback grows `dict_hooks` and every by-value holder of it, including
  `OffsetTable`; the census is in research.md.
- The crafted probe-budget frame can be built deterministically for a known table seed in a test; the production seed
  is not weakened to make the test possible.
- Typed C++ group views have no "present" notion today; for FR-002 on the typed path, present-with-zero and absent both
  read as size 0, as in QuickFIX's `groupCount`. No typed API is added for presence.
- C++ public API: `group_view` gains `read_failed()` and two status constructors; its `bool` constructors are kept.
  The result structs' `alloc_failed` field is renamed to `status`, a C++ source change for any reader of the field
  (including a header generated by an older emitter), declared in the B&L delta (research D5).

## Out of Scope

- Arena sizing or growth (073's ruling).
- The validator's group detection, including its context-miss fallback (a lead to file).
- Inheriting the root's `OffsetTable::Config` in nested sub-tables (a lead to file).
- The session-side Reject and dispatch defects of row 7 (B82, B85, B88, B91, #549) and the dictionary/codegen ones (B84,
  B67): separate features.
- fixpp#540 (`unknown_fields()` over the same arena), unless the plan finds it is the same fix.
