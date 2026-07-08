# Feature Specification: Dictionary-backed inbound receive parse

**Feature Branch**: `066-dict-backed-inbound-parse`  
**Created**: 2026-07-09  
**Status**: Draft  
**Input**: Prerequisite for issue #179 / feature 065. Gate A on 065 (+ a Fable investigation, 2026-07-09) established that the shipped inbound receive parse is dictionary-free, so every `MessageView` delivered to an application callback (C-ABI and C++ typed) has no group membership — making all inbound repeating-group reads positional/membership-free on the real path. This feature threads the session's (already-required) dictionary into the inbound parse so inbound messages carry membership, which is the precondition for any correct grouped read.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Membership-correct inbound repeating-group reads (Priority: P1)

An application receiving a FIX message through a live session reads a repeating group (via the C-ABI `fixpp_group_*` accessors or the C++ typed flyweights). The group's instances and per-instance fields resolve by **dictionary membership** — each instance is bounded at the first tag that is not a member of that group in its context — exactly as the (dictionary-backed) validator and typed-read tests already do.

**Why this priority**: Today the inbound parse is dictionary-free (`Session::parse_and_dispatch_` uses a default `Parser`), so `OffsetTable::group()` takes its membership-free fallback and a group's extent runs to **end of message**. The last instance of any inbound group absorbs every subsequent body field; a field queried on that instance returns `OK` + a wrong value instead of `TAG_NOT_FOUND`. This is a reachable **silent wrong value** on every real inbound message that carries a group followed by more fields — the root cause behind issue #179, and broader than #179 stated (it affects top-level groups and the C++ typed path too, not only the C-ABI nested read).

**Independent Test**: Drive a group-bearing frame (e.g. a FIX44 `ExecutionReport` with `NoLegs(555)` followed by a trailing outer field) through **real `Session` dispatch** and assert in the application callback that a trailing outer field queried on the last group instance returns `TAG_NOT_FOUND`, and each instance's own members read correctly. This must be RED on today's dict-free parse and GREEN after.

**Acceptance Scenarios**:

1. **Given** a live session with its configured dictionary, **When** an inbound message carrying a repeating group followed by trailing fields is dispatched, **Then** the group's last instance is bounded by membership (trailing non-member fields are NOT part of it), for both the C-ABI and C++ typed read paths.
2. **Given** the same, **When** the application reads each instance's declared members, **Then** every value is correct and the instance count matches the wire.

---

### User Story 2 - Scalar-as-group query returns the documented result (Priority: P1)

An application queries a scalar tag as if it were a repeating group on an inbound message.

**Why this priority**: Dictionary-free, the "is this count field really a group" delimiter-membership check is skipped, so querying a scalar tag as a group returns a bogus instance spanning the rest of the message instead of the documented `FIXPP_ERR_TYPE_MISMATCH` (C-ABI E-2 / CA-010 contract). Restoring membership restores this contract.

**Independent Test**: Through real session dispatch, `fixpp_msg_get_group(msg, <a scalar tag>, …)` returns `FIXPP_ERR_TYPE_MISMATCH` (not a spurious group).

**Acceptance Scenarios**:

1. **Given** a dispatched inbound message, **When** a non-group scalar tag is queried as a group, **Then** the result is `FIXPP_ERR_TYPE_MISMATCH` (present-but-not-a-group), matching the dictionary-backed contract.

---

### User Story 3 - No regression for non-group traffic, admin, or performance (Priority: P1)

Every message that reads correctly today continues to; admin/session messages (Heartbeat/Logon/…) are unaffected; the inbound parse remains free of new global-heap allocation and within its stack parse-arena budget.

**Why this priority**: The change is on the inbound hot path. It must not regress existing session/interop behavior, must not introduce per-message global-heap allocation, and must not overflow the parse arena.

**Independent Test**: The full session + interop + C-ABI test suites pass; the allocation-discipline gate shows no new global-heap allocation on the parse+read path; a representative group-bearing message parses+reads within the existing stack arena.

**Acceptance Scenarios**:

1. **Given** the existing session/interop/C-ABI suites, **When** the inbound parse is dictionary-backed, **Then** all pass (with any intended behavior changes updated as explicit, reviewed test edits — not silent breakage).
2. **Given** an inbound message with no repeating groups (incl. all admin messages), **When** dispatched, **Then** behavior and cost are effectively unchanged (membership is consulted lazily, only on a group read).

---

### Edge Cases

- **Counterparty field inside a group not present in the loaded dictionary** — INTENDED BEHAVIOR CHANGE (permissive → strict): membership bounding terminates the group instance at that unknown field (`consume_group_extent` breaks on the first non-member). This matches QuickFIX's `DataDictionary` behavior but is a change from today's permissive dict-free read. It MUST be surfaced (a Behaviors & Limitations row) and is the main decision to confirm during `/speckit-clarify`. Scope note: the loaded dictionary should declare all groups the counterparty actually sends; a session speaking a superset dialect needs a matching dictionary.
- **Cloned inbound message (`fixpp_msg_clone`) and `reify` handles** — these currently build deliberately dictionary-free views, so a clone would read a group DIFFERENTLY from its source once the source is dictionary-backed. Policy (propagate membership into the clone/reify view, or document the divergence) is decided in this feature (see FR-007).
- **Parse arena sizing** — dictionary-backed nested reads build sub-`OffsetTable`s lazily from the per-message stack arena during the callback. The existing `kInboundParseArena` (16 KiB) / `kAdminParseArena` (8 KiB) must still suffice for representative nested reads; a pathological deeply-nested message must fail closed (defined error), never over-read or corrupt.
- **Opt-in strict validator's own parse** (`vg_parser`, dictionary-free today) — assess whether it must also be dictionary-backed for its group-validation to be correct, or whether the validator's own `table_view` walk already covers it (FR-006).
- **No dictionary / directly-constructed session** — `open()` already requires a dictionary; a session without one is a config error before dispatch. So there is no optional-dictionary path to handle on the read side.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The inbound receive parse that produces the `MessageView` delivered to application callbacks (`Session::parse_and_dispatch_`) MUST construct its `Parser` dictionary-backed, using the session's configured dictionary, so the resulting `MessageView`'s `OffsetTable` carries the group-membership predicate and context.
- **FR-002**: The session MUST build the dictionary's `table_view` **once** (at `open()`, when the dictionary is known) and hold it in a member with a **stable address** for the lifetime of all inbound parses (the dictionary-backed `Parser` ctor stores a pointer to it). It MUST NOT rebuild the `table_view` per message.
- **FR-003**: Both the C-ABI grouped read (`fixpp_msg_get_group` / `fixpp_group_*`) and the C++ typed grouped read MUST produce membership-correct results on inbound-dispatched messages (group extents membership-bounded; scalar-as-group → `TYPE_MISMATCH`), because both derive membership from the same dictionary-backed `OffsetTable`.
- **FR-004**: The inbound parse+read path MUST remain free of new **global-heap** allocation: the `table_view` is built once at `open()`; per-message membership lookups and lazily-built nested sub-views allocate only from the existing per-message stack parse arena (as the dictionary-backed typed-read path already does).
- **FR-005**: Non-group traffic and admin/session messages MUST be behaviorally unchanged (membership is consulted lazily, only on a group read); the change MUST NOT alter session-protocol handling.
- **FR-006**: The feature MUST determine and record whether the opt-in strict validator's own inbound parse (`vg_parser`) also requires dictionary-backing for correct group validation, and either apply the same treatment or document why the validator's own `table_view` walk already suffices.
- **FR-007**: The feature MUST decide and record the dictionary policy for cloned (`fixpp_msg_clone`) and `reify` views — either propagate membership so a clone reads identically to its source, or document the divergence as a limitation — so a clone does not silently read a group differently from the message it was cloned from.
- **FR-008**: The intended behavior change — a counterparty field inside a group that is absent from the loaded dictionary now terminates the group instance (permissive → strict, matching QuickFIX) — MUST be documented as a Behaviors & Limitations row and surfaced for explicit sign-off.
- **FR-009**: A pathological inbound message (deeply nested / oversized group) MUST fail closed within the existing parse-arena and group-depth/entry caps — never over-read, corrupt, or silently truncate.
- **FR-010**: Issue #179 and the L-063-2 Behaviors & Limitations row MUST be amended to correct the now-known-false claim that "the C++ typed read path is unaffected" (it is membership-free on the shipped path until this feature lands).

### Key Entities *(include if data involved)*

- **Session `table_view` member**: the once-built, stable-address dictionary metadata (membership predicate + field classification) the inbound `Parser` binds to. Analogous to the copy the strict validator already holds; this feature gives the parse its own stable instance (or a shared one).
- **Inbound `MessageView` / `OffsetTable`**: the parsed view handed to callbacks; gains a non-null membership predicate + dictionary pointer + root group context, enabling membership-bounded group extents.
- **Dictionary (`cfg_.dictionary`)**: the required, already-configured `shared_ptr<const Dictionary>`; source of `as_table_view()`.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A group-bearing inbound frame driven through **real `Session` dispatch** reads membership-correctly — a trailing outer field queried on the last group instance returns `TAG_NOT_FOUND` (0 silent wrong values) — for both the C-ABI and C++ typed read paths; RED on the pre-change dict-free parse, GREEN after.
- **SC-002**: Querying a scalar tag as a group on an inbound-dispatched message returns `FIXPP_ERR_TYPE_MISMATCH` in 100% of cases (documented contract restored).
- **SC-003**: 100% of the existing session + interop + C-ABI test suites pass after the change; every behavior delta is an explicit, reviewed test update (no silent breakage), and each intended change is pinned by a discriminating test.
- **SC-004**: Zero new global-heap allocations on the inbound parse+read path (allocation-discipline gate), and the per-message stack parse arena is not overflowed by representative group-bearing messages.
- **SC-005**: After this feature, issue #179's C-ABI nested-read fix (065) reads membership-correctly through real session dispatch — the prerequisite is satisfied (verified by 065's real-dispatch witness, which is RED before 066+065 and GREEN after).

## Assumptions

- The dictionary is **required** at `open()` (`session.cpp` null-dict guard), so there is no optional-dictionary path to handle on the inbound read side; every live session already owns the dictionary this feature threads into the parse.
- The dictionary-backed `Parser` ctor (`parser.hpp`) and the 062/063 membership/extent machinery are correct and reusable; this feature routes the already-supported dictionary-backed parse into the session inbound path — it does not modify the parse/membership algorithms.
- Building `as_table_view()` once at `open()` is acceptable cost (open is not the hot path; the strict validator already pays it when enabled); per-Dictionary caching across sessions is a possible later optimization, out of scope here.
- Membership is consulted lazily (only on a group read), so admin/no-group traffic incurs negligible per-message overhead.
- The dict-free parse was an oversight, not a deliberate design choice (the 019 app-callbacks design doc describes the callback view as "dict-backed / dict-aware"), so restoring dictionary-backing aligns the implementation with its original design intent.
