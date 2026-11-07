# Feature Specification: Wire group-read integrity — no silent loss, no exception across the C boundary

**Feature Branch**: `100-wire-group-read-integrity`

**Created**: 2026-11-07

**Status**: Draft

**Input**: REMAINING-WORK ★ V1.0 FUNCTIONAL TRACK row 7, wire half: batch B81 (fixpp#645, #644, #647) and batch B42
(fixpp#541). Four defects on the inbound read path where a present field or group is reported absent or empty, or where
an allocation failure leaves an `extern "C"` function as an exception.

## Context & Problem

An application reads an inbound message in one of two ways: the typed C++ view (`MessageView`, `group_view`), or the C
ABI (`fixpp_msg_get_*`, `fixpp_msg_get_group`, `fixpp_group_get_nested_group`), which the Python binding wraps. Both
read from one per-message offset table built at parse time, and group reads allocate from the same bounded per-message
parse arena.

Four conditions give an answer that is wrong, or no answer at all:

1. **Probe cap (#645, L-458-1).** Building the table's hash index caps each insert's probe length (a DoS bound against
   crafted hash collisions). When the cap is hit, the occurrence is left out of the index **and the build still reports
   success**, so the parse succeeds and a field that is on the wire reads as absent.
2. **Top-level group, arena exhausted (#644, L-073-1).** A top-level repeating-group read that cannot allocate its
   instance slices returns an empty group with no failure flag. 073 made the **nested** read fail-loud; the top-level
   read on both paths (the typed `group<>()` and the C-ABI `fixpp_msg_get_group`) still cannot tell "no instances" from
   "could not read".
3. **Zero-count group (#647, L-051-3).** A group whose count field says `0` reads as **not a group** on the C ABI. The
   group check looks at the field after the count tag; with no instances that field is not a member, so the flat getter
   reports `TYPE_MISMATCH` (also when the count field is last, since the flat getter treats an empty slice list as "not
   a group"). FIX permits a zero count, a validating session accepts it (TC-018, B-004-7), and QuickFIX C++ and J read
   it as a present group with zero instances, deciding group-ness from the dictionary's registration of the count tag
   (`Message::setGroup` → `DataDictionary::getGroup`; `Message.parseGroup` → `dd.getGroup`). **Ruled v1.0 BUILD**,
   parent repo `research/reviews/ruling_B81_647_zero_count_group_v1.md`.
4. **Cursor shell allocation (#541, B42).** Both C-ABI group getters allocate the returned cursor from the parse arena
   with nothing to catch a failure, so `std::bad_alloc` leaves the `extern "C"` function (undefined behaviour for a C
   caller; under the C-ABI application wrapper the process aborts). Reproduced at `d51ce86d` (branch `b28-repro`,
   `e10c8b80`): after the first read the slices come from a cache, so each repeat call allocates only the shell, and
   call 188 throws on a frame at the inbound limit. Still no handler at `fa778a8e`.

Today's C-ABI codes for these endings are also inconsistent between the two getters: when the first read of a group
does not fit, the flat getter returns `TYPE_MISMATCH` and the nested one `WIRE_LIMIT_EXCEEDED` (pinned by the shipped
`CapiInboundFrameDispositionsQ33.*` cells); and a nested tag that is present but not a group returns `OK` with count 0
while the flat getter returns `TYPE_MISMATCH`.

## Clarifications

### Session 2026-11-07 (orchestrator, owner-delegated)

- Q: Is #647 in v1.0? → A: Yes (ruling note above). Group-ness comes from the dictionary's registration of the count tag
  in its context, never from the value `0`.
- Q: Should the arena be sized or grown so these failures cannot happen? → A: No. The bounded, null-upstream arena is the
  DoS property (073's ruling, kept). This feature makes each failure observable; it does not move the bound.
- Q: Which ending does the probe cap take? → A: The parse fails with a wire-limit error, the ending a frame whose offset
  table is over capacity takes today. The DoS bound stays: the build still stops probing at the cap.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - A zero-count group reads as a present, empty group (Priority: P1)

A C or Python application receives a message carrying a repeating group with `NoXxx=0`, followed by more fields (for
example `268=0` in a market data message, then further body fields), and asks for the group.

**Why this priority**: Reachable by any peer with ordinary, valid traffic, on a message the session has just accepted.
The application today gets an error for a valid message and cannot read the group's count.

**Independent Test**: Parse a frame with a zero-count group in mid-body, and one with the zero-count field last, and read
the group through each read path.

**Acceptance Scenarios**:

1. **Given** a message with a dictionary-registered group count tag holding `0` followed by more fields, **When** the
   application calls the flat C-ABI group getter, **Then** it gets `OK` with count 0.
2. **Given** the same message with the zero-count field last, **When** the flat getter is called, **Then** `OK`, count 0.
3. **Given** a group instance containing a nested group whose count is `0` followed by more members, **When** the nested
   getter is called, **Then** `OK`, count 0.
4. **Given** a scalar field (not registered as a group count in that context) whose value is `0`, **When** either getter
   is called for it, **Then** `TYPE_MISMATCH`, as for any scalar.
5. **Given** a message with no such count tag, **When** either getter is called, **Then** `TAG_NOT_FOUND`, unchanged.
6. **Given** any of these frames on the typed C++ path, **When** the group is read, **Then** the result has size 0 and
   the count field itself reads `0`; the fields after it read as ordinary fields.

---

### User Story 2 - A group read that runs out of arena says so (Priority: P1)

An application reads a top-level repeating group from a large inbound message whose parse arena cannot hold the group's
instance slices, or repeatedly fetches a group cursor until the arena is spent.

**Why this priority**: Silent loss (an empty group for a present one) and an exception across `extern "C"` (a process
abort for a C or Python application) are both outcomes the C-ABI contract forbids.

**Independent Test**: Force the top-level slice allocation to fail, and separately exhaust the arena with repeated
getter calls, on both getters and on the typed path.

**Acceptance Scenarios**:

1. **Given** a top-level group whose slice allocation fails, **When** the typed `group<>()` read runs, **Then** the
   result reports `alloc_failed() == true`.
2. **Given** the same frame, **When** the flat C-ABI getter is called, **Then** it returns the one documented
   arena-exhaustion code, the same code the nested getter returns for the same cause.
3. **Given** an arena that a repeat call exhausts while allocating the cursor, **When** either getter is called, **Then**
   it returns that same code and no exception leaves the function.
4. **Given** an ample arena, **When** a group is read many times, **Then** every call succeeds as today (control).

---

### User Story 3 - A parse never succeeds with a field missing from its index (Priority: P2)

A peer sends a frame crafted so that one tag's index insert reaches the probe cap.

**Why this priority**: Needs a hostile, crafted frame; but the result is a field that is on the wire and reads absent on
a parse that reported success, the silent-loss class this project treats as real.

**Independent Test**: Build a frame whose tags collide under the index's hash for the table's seed until a later tag
reaches the cap; parse it.

**Acceptance Scenarios**:

1. **Given** such a frame, **When** it is parsed, **Then** the parse fails with a wire-limit error and no field reads
   absent on a successful parse.
2. **Given** a frame below the cap, **When** it is parsed, **Then** it parses and indexes exactly as today.
3. **Given** such a frame on an inbound session, **When** it arrives, **Then** it takes the disposition a frame whose
   offset table is over capacity takes today.

### Edge Cases

- A zero-count group whose count tag is **also** used as a scalar in another context of the same message: group-ness is
  decided per context (message type and parent path), as the read path does today for membership.
- A declared count of `0` followed by fields that **are** members of the group (a lying count): the read follows the
  declared count, as today for any declared count; flagging the mismatch is the validator's job.
- A nested zero-count group that is the last field of its instance (`NestedGroupEmptyGroupCountLastField`): unchanged,
  `OK` with count 0.
- Arena exhaustion on the **first** read versus a **repeat** read of the same group: both return the same code.
- The probe cap and a dict-free table: the cap applies to every table; a dict-free table has no group reads (fixpp#220
  declines them), so only the index arm applies.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001** (#647): A group count tag is a group, for every group read on both paths, when the dictionary registers it
  as a group count in the read's context (message type, parent path). The value of the count does not decide it, and
  neither does the field that follows it.
- **FR-002** (#647): A registered group count tag whose declared count is `0` reads as a present group with zero
  instances: C-ABI `OK` with count 0 on both getters, and an empty, not-failed `group_view` on the typed path, wherever
  the count field sits in the message or instance.
- **FR-003** (#647): A tag present in the message that is not a registered group count in the read's context reads
  `TYPE_MISMATCH` on **both** C-ABI getters. (Today the nested getter answers `OK`, count 0.)
- **FR-004** (#647): A tag absent from the message (or from the instance, for the nested getter) reads `TAG_NOT_FOUND`
  on both getters, unchanged.
- **FR-005** (#644): A top-level group read whose slice allocation fails reports the failure: `group_view::alloc_failed()`
  on the typed path, and the C-ABI arena-exhaustion code on the flat getter. Never an empty group reported as success.
- **FR-006** (#541): Neither C-ABI group getter lets an exception leave it. A failed cursor allocation returns the C-ABI
  arena-exhaustion code with `*group_out` null and `*count_out` 0.
- **FR-007** (#541, #644): The two getters return **one** code for arena exhaustion, whatever allocation failed (slices,
  sub-table, cursor shell) and whether it is the first or a repeat read. `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` is the expected
  code (the nested getter's today), to be confirmed at plan against the C-ABI error contract.
- **FR-008** (#645): A table build that reaches the probe cap ends in a failed build status, so the parse fails with a
  wire-limit error. The probe stays capped at the same constant; build cost stays linear in the number of occurrences.
- **FR-009**: A frame that does not reach any of these conditions reads exactly as today: same fields, same group
  instances, same codes. No new allocation on the read path; no change to the arena's size or upstream.
- **FR-010**: The B&L rows L-458-1, L-073-1 and L-051-3 move to the closed file with the delivering witnesses named, and
  L-093-17's group-getter half is updated (or retired) to match FR-006. Any new or changed C-ABI code is documented in
  the C-ABI error contract and its completeness test, and the C-ABI version bumps per `[const §X.1]` if the observable
  contract changes.

### Key Entities

- **Group registration**: the dictionary's record that a tag is a repeating group's count in a given context (message
  type, parent path), with its delimiter and members.
- **Group read result**: the instance list plus an allocation-failure flag; the flag is what separates "zero instances"
  from "could not read".
- **Arena-exhaustion code**: the one C-ABI error a group getter returns when any allocation it needs fails.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Every cell named in the acceptance scenarios is RED on `fa778a8e` where today's behaviour differs (US1.1,
  US1.2, US1.3, FR-003's nested scalar, US2.1, US2.2, US2.3, US3.1) and GREEN on the feature head; the control cells are
  GREEN on both.
- **SC-002**: The B42 repro shape (repeat getter calls on a frame at the inbound limit) completes every call with a
  return code and no exception, on both getters.
- **SC-003**: No existing test changes its expected outcome except the cells that pin today's defects (named in the plan
  with the requirement that supersedes each), and the inbound zero-allocation gates stay green.
- **SC-004**: A validating session that accepts a zero-count group frame and the C-ABI read of the same frame agree: the
  group is present with count 0.

## Assumptions

- The read path's group membership query already carries the context (message type, parent path); FR-001 asks it a
  registration question instead of a membership question, without a new dictionary structure (to confirm at plan).
- The probe-cap frame can be crafted deterministically for a known table seed in a test; the production seed is not
  weakened to make the test possible.
- Typed C++ group views have no "present" notion today; for FR-002 on the typed path, present-with-zero and absent both
  read as size 0, as in QuickFIX's `groupCount`. No typed API is added for presence.
- C++ public API changes, if any, are additive (`group_view` already carries `alloc_failed`).

## Out of Scope

- Arena sizing or growth (073's ruling).
- The session-side Reject and dispatch defects of row 7 (B82, B85, B88, B91, #549) and the dictionary/codegen ones (B84,
  B67): separate features.
- fixpp#540 (`unknown_fields()` over the same arena), unless the plan finds it is the same fix.
