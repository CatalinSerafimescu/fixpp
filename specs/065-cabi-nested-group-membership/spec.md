# Feature Specification: Membership-aware C-ABI nested repeating-group read

**Feature Branch**: `065-cabi-nested-group-membership`  
**Created**: 2026-07-08  
**Status**: Draft  
**Input**: Fix issue #179 / L-063-2 — the C-ABI nested-group read absorbs a trailing outer-group member into the last nested-group instance, yielding a silent wrong value on the GA-frozen C-ABI.

## Clarifications

### Session 2026-07-09

- Q: For the FR-011 / SC-005 real-dictionary nested witness, which harness asserts C-ABI≡C++ membership equivalence — the new 066 C-ABI engine-loopback (production dispatch path), a directly-built `Dictionary::as_table_view()` view, or both? → A: **Both** — a direct `as_table_view()` unit witness for the extent arithmetic PLUS one engine-loopback witness (066 `GroupMembershipCapiRed`-style, via `capi_dict066_loopback_support.hpp`) for dispatch-path context-threading fidelity.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Correct nested-group field read when a trailing outer member follows the nested group (Priority: P1)

A C-ABI consumer parses a well-formed FIX message whose outer repeating-group entry carries a **multi-entry nested group** followed by a **declared trailing outer-group member** (a layout that is ubiquitous in the shipped dictionaries — e.g. a FIX44 `ExecutionReport` `NoLegs(555)` entry with a multi-entry `NoLegSecurityAltID(604)` nested group followed by `LegQty(687)`). The consumer descends into the nested group via `fixpp_group_get_nested_group` and reads a field from the **last** nested-group instance by tag.

**Why this priority**: This is the defect. Today the last nested instance's byte extent runs to the end of the outer entry's slice, so the trailing outer member's bytes fall inside it. A read of the trailing member's tag against the last nested instance returns `FIXPP_ERR_OK` + the trailing member's value, where a correct reader must return `FIXPP_ERR_TAG_NOT_FOUND`. This is a **reachable, silent wrong-value** result on a GA-frozen (`1.5.0`) C-ABI, triggerable by any conforming counterparty. Silent wrong values are the worst failure class for a wire library — the consumer cannot detect them.

**Independent Test**: Un-skip and run the pre-existing witness `tests/capi/message_read_test.cpp::MessageReadGroup.NestedGroupLastInstanceExtentDoesNotAbsorbTrailingOuterMember`, which builds exactly this layout and asserts that a read of the trailing outer tag against the last nested instance returns `FIXPP_ERR_TAG_NOT_FOUND`.

**Acceptance Scenarios**:

1. **Given** an outer group entry containing a multi-entry nested group followed by a trailing outer-level member tag, **When** the consumer reads the trailing tag from the **last** nested-group instance (`fixpp_group_get_field_*(nested, last_index, trailing_tag, …)`), **Then** the call returns `FIXPP_ERR_TAG_NOT_FOUND` and writes no value.
2. **Given** the same message, **When** the consumer reads the trailing outer member tag at the **outer** entry index (`fixpp_group_get_field_*(outer, i, trailing_tag, …)`), **Then** the call still returns `FIXPP_ERR_OK` with the correct trailing value (the member genuinely belongs to the outer entry).
3. **Given** the same message, **When** the consumer reads each genuine member of each nested instance by tag, **Then** every nested instance returns its own correct member values and `nested_count` equals the true number of nested instances.

---

### User Story 2 - No regression on the common case and no C-ABI freeze change (Priority: P1)

Every message layout that reads correctly today MUST continue to read identically, and no exported C symbol, header, error enum, or version constant may change (the C-ABI is GA-frozen at `1.5.0`).

**Why this priority**: The fix touches a GA-frozen read path. Any behavioral or symbol change beyond eliminating the wrong value is a breaking change. Equally load-bearing as US1 — a fix that regresses the common case is not shippable.

**Independent Test**: The full existing C-ABI read test suite (`tests/capi/message_read_test.cpp`) passes unchanged except for the one newly-unskipped witness; the exported-symbol / ABI hygiene gate reports no delta.

**Acceptance Scenarios**:

1. **Given** a nested group with **no** trailing outer member after it, **When** the consumer reads any nested instance field, **Then** results are byte-identical to current behavior.
2. **Given** a single-entry nested group, an empty nested group (`No<Group>=0`), or a `nested_tag` that is absent, **When** the consumer descends, **Then** the returned count and per-field errors match current behavior.
3. **Given** the built shared/static library, **When** its exported C symbols and public headers are compared to the `1.5.0` baseline, **Then** there is no addition, removal, or signature change.

---

### Edge Cases

- **Trailing member equals a nested member tag by value (SCOPE LIMITATION — not solved here)**: the nested-instance bound is by dictionary **membership**, but that predicate compares by tag **value** (`consume_group_extent` breaks on the first non-member tag, `offset_table.cpp:469`), so a trailing outer tag whose value equals a non-delimiter member of the nested group would be **absorbed** into the last nested instance. This feature therefore delivers correctness only where the parent and nested **scalar member tags are DISJOINT** — which holds for every shipped dictionary (mechanically verified for the 6 vendored group-bearing dicts). The same-value collision on a user/dialect dictionary is a **documented, already-tracked limitation** (L-062-3 / L-063-4, issue #180 — "harden dictionary census: parent/child scalar-member disjointness"), **NOT** solved by 065.
- **Deeply nested groups (group within nested group, depth ≥ 2)**: OUT OF SCOPE for this feature. Issue #179 is a depth-1 case — a trailing outer member after a *single* nested group. The fix threads the arithmetically-correct membership path onto each cursor (so a depth-2 C-ABI descent is membership-bounded rather than positional), and stays bounded by the same depth cap (`kMaxGroupDepth`) the C++ parser enforces (fails closed, never silently truncates). But depth-≥2 nested-in-nested *read correctness* is not asserted or witnessed here: research surfaced a **pre-existing** typed-path context-threading gap at depth ≥ 2 (the generated nested accessor threads the parent group's *unpushed* context — `emit_messages.cpp:263-268` — so a doubly-nested group's members resolve under a too-short path), which is a separate follow-up (research Decision 7). The represented (depth-1) layout is the delivered scope.
- **Dictionary-free / membership-unavailable cursor**: if a message was parsed without dictionary membership (no `group_member_fn`), the read path MUST NOT crash or silently produce a worse result than today; it degrades to the current positional behavior (documented limitation), never UB.
- **Lying / malformed nested count**: a declared count larger or smaller than the on-wire instance count MUST remain fail-closed (bounded scan), never over-read past the outer entry slice.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The C-ABI nested-group read (`fixpp_group_get_nested_group` and the subsequent `fixpp_group_get_field_*` on the returned nested cursor) MUST bound each nested-group instance — including the **last** instance — by the nested group's dictionary **membership**, so that a declared outer-group member positioned after the nested group is NOT included in any nested instance's field-lookup extent.
- **FR-002**: A field read against a nested-group instance for a tag that is not a member of that nested group (e.g. a trailing outer-level member) MUST return `FIXPP_ERR_TAG_NOT_FOUND`.
- **FR-003**: A field read against the **outer** group entry for a genuine outer-level member (including one positioned after the nested group) MUST continue to return `FIXPP_ERR_OK` with the correct value.
- **FR-004**: The reported nested instance count (`nested_count_out`) and every genuine nested-instance member value MUST be correct for multi-entry nested groups.
- **FR-005**: The fix MUST reuse the existing membership-aware repeating-group extent machinery (the same `group_member_fn` + `group_context` mechanism that bounds the C++ typed read path and `OffsetTable::nested_group_slices`), NOT a new independent membership implementation, so the C-ABI and C++ paths agree by construction.
- **FR-006**: The fix MUST NOT change any exported C symbol, public C header, C error enum value, or C-ABI version constant. The C-ABI stays GA-frozen at `1.5.0` (SC-005 of prior features).
- **FR-007**: The fix MUST preserve the zero-global-heap property of the C-ABI read path (allocations for the nested cursor and its slices come from the per-message parse arena, as today).
- **FR-008**: ~~For a cursor that lacks dictionary membership (parsed without a `group_member_fn`), the read path MUST degrade safely (no crash, no UB, no regression versus today's positional behavior) rather than fault.~~ **[SUPERSEDED 2026-09-06 by fixpp#220.** The "no regression versus positional behaviour" clause treated the pre-065 positional result as a floor, which was right for 065 — it was fixing the dict-aware path and deliberately not touching the other. #220 establishes that the positional result was itself the defect: the trailing outer member absorbed into the last nested instance is a wrong value returned under `FIXPP_ERR_OK`, indistinguishable to the caller from a real one. `OffsetTable::group()` is now dictionary-only, so a dict-free cursor is never handed out and `fixpp_msg_get_group` reports `FIXPP_ERR_TYPE_MISMATCH`. The "no crash, no UB" half of FR-008 still holds and is still witnessed. See **B-220-1** in `spec/behaviors-and-limitations.md`; the pin `DictFreeNestedReadDegradesToPositional` is rewritten as `DictFreeGroupReadReportsTypeMismatch`.]**
- **FR-009**: Nested descent MUST remain bounded by the existing group-depth and per-instance entry caps and MUST NOT over-read past the outer entry slice. **Limit-error channel (corrected):** the span-returning `nested_group_slices` collapses an `err_group_too_large` from `consume_group_extent` to an empty span (`group_slices()`, `offset_table.cpp:583/621/625`), so the nested read does **not** itself surface a distinct limit error — the C-ABI does **not** "inherit fail-closed by reusing `consume_group_extent`". This is acceptable only because the over-limit condition is **unreachable** on the public C-ABI nested read: the only way to obtain a nested cursor is `fixpp_msg_get_group`, whose mandatory outer-first `group_slices()` pre-walk already recurses through the nested group during the outer extent computation (`consume_group_extent` recurses at `offset_table.cpp:476-482`), and the C-ABI always parses with the default `Config` (no tuning surface for `max_group_entries_per_instance`), so the sub-table's caps equal the root's — an over-limit nested group would already have failed the **OUTER** read (`fixpp_msg_get_group` → `FIXPP_ERR_TYPE_MISMATCH`, no cursor). The old scanner's coded `WIRE_LIMIT_EXCEEDED` guard (untested / `LCOV_EXCL`) is thereby **dropped** — acceptable only on this unreachability, not because it regresses a tested behavior.
- **FR-010**: The pre-existing `GTEST_SKIP`'d witness `MessageReadGroup.NestedGroupLastInstanceExtentDoesNotAbsorbTrailingOuterMember` MUST be un-skipped and pass with all of its positive assertions (nested counts, per-instance member values, trailing member reachable at the outer index) intact, mirroring the `:353` un-skip lifecycle precedent. It MUST be mutation-proven RED on the pre-fix code.
- **FR-011**: The trailing-member layout MUST be pinned on the issue's concrete FIX44 shape (`NoLegs(555)` entry → multi-entry `NoLegSecurityAltID(604)` → trailing `LegQty(687)`) by **two** real-dictionary witnesses [Clarification 2026-07-09]: **(a) a direct witness** built via `Dictionary::as_table_view()` from **inline XML** (`XmlLoader{}.load_from_string(<FIX44-shaped XML>, &arena)` then `as_table_view()`, following the existing `TopLevelCollidingGroup296CAbiReadsFullMassQuoteExtent` precedent — NOT a hand-built single-`msg_type` `table_view`, and NOT requiring the shipped `dictionaries/FIX44.xml` / `FIXPP_DICT_DATA_DIR`) that asserts the trailing member is `TAG_NOT_FOUND` on the last nested instance AND that the C-ABI result matches the C++ typed read (SC-005) — this isolates the nested-extent arithmetic. It lives in `tests/capi/message_read_test.cpp` (mallocnesia-safe, no CMake compile-def change); and **(b) an engine-loopback witness** that drives the same frame through the 066 C-ABI dispatch path (the `GroupMembershipCapiRed`-style harness via `capi_dict066_loopback_support.hpp`), so membership reaches the nested cursor exactly as in production and the trailing tag is `TAG_NOT_FOUND` on the last nested instance. Witness (b) lives in a **dict066-style loopback target** (a new target mirroring `capi_dict066_group_membership_red_test`, or a new case on the existing one) that already carries `FIXPP_DICT_DATA_DIR` + `FIXPP_CAPI_FEATURE_B_INCLUDES` — it CANNOT live in `message_read_test.cpp` (no loopback scaffold / no `FIXPP_DICT_DATA_DIR`); it may use the shipped `FIX44.xml`. Together they pin that real msg_type/parent-path context is threaded correctly on the C-ABI path — the exact class (empty-`msg_type` context on the C-ABI read) that regressed in 063 Gate-B RC#1. **Writability caveat:** the generated typed flyweight has **no accessor for a non-member tag** (`687` is not a member of the nested group), so the C-ABI≡typed equivalence MUST be expressed via genuine member values + nested count/extent agreement AND the trailing tag's **absence** from the typed nested entry's corrected extent (via the `field_value()` escape hatch — post-fix the entry slice excludes `687`, matching the C-ABI `TAG_NOT_FOUND`), NOT by asking the typed nested accessor for the trailing tag directly.

### Key Entities *(include if feature involves data)*

- **Nested-group cursor (`fixpp_group`, internal)**: the opaque C-ABI group handle returned to consumers. Internally holds the slices of an outer or nested group plus a back-pointer to the owning parsed view. Only forward-declared in the public header, so its internal shape may carry additional membership context (the nested group's context path) with **zero public-ABI impact**.
- **Group membership context**: the `(msg_type, parent-group path)` context under which a repeating group's members are resolved, already produced by dictionary load (`as_table_view`) and carried on the parsed view's offset table. It is the input that lets the extent scan distinguish a nested member from a trailing outer member.
- **Nested-group instance slice**: a `(pointer, length)` sub-span of the wire buffer delimiting one instance of a nested group. The last instance's length is the quantity the defect gets wrong.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A C-ABI consumer reading a trailing outer-level member tag from the last instance of a preceding multi-entry nested group receives `FIXPP_ERR_TAG_NOT_FOUND` in 100% of cases (0 silent wrong values), verified by the un-skipped witness across the represented layout.
- **SC-002**: 100% of currently-passing C-ABI read tests continue to pass; the only test-status change is the one newly-unskipped witness moving from skipped to passing.
- **SC-003**: Zero change in exported C symbols and public C headers versus the `1.5.0` baseline (ABI hygiene gate reports no delta).
- **SC-004**: Zero global-heap allocations on the nested C-ABI read path (verified by the existing allocation-discipline gate), unchanged from today.
- **SC-005**: For the represented **depth-1** layout, C-ABI and C++ typed nested reads produce membership-equivalent results (both exclude the trailing member from the nested extent), verified by **two** `as_table_view()`-backed witnesses on the FIX44 `NoLegs(555)` → `NoLegSecurityAltID(604)` → `LegQty(687)` shape — a direct `as_table_view()` witness (inline-XML `load_from_string`, in `message_read_test.cpp`, no `FIXPP_DICT_DATA_DIR`) and an engine-loopback (066 C-ABI dispatch-path) witness (dict066-loopback target, may use shipped `FIX44.xml`) [Clarification 2026-07-09]. Because the typed path has no accessor for the non-member trailing tag, equivalence is asserted via member values/extent + the trailing tag's absence from the typed nested entry's corrected extent (`field_value()` escape hatch), not via a typed accessor for the trailing tag. (Depth-≥2 equivalence is out of scope — see Edge Cases.)

## Assumptions

- The message was parsed with a dictionary that supplies group membership (`group_member_fn` + `group_context`); this is the case for every read path that reaches a nested group in the shipped/typical usage. Dictionary-free cursors are handled by FR-008 as a documented degradation, not a target of the fix.
- The membership-aware extent machinery delivered by 062 (`OffsetTable::nested_group_slices`, `build_nested_subview`, `consume_group_extent`) and the context-scoped membership from 063 are correct and reusable as-is; this feature plumbs them to the C-ABI cursor rather than re-deriving membership.
- The parent-group's own context path is derivable at the C-ABI cursor (the top-level group cursor is created from the parsed view, which carries the root context; nested descent pushes the parent group's count tag onto the path).
- No new dictionary data, no new wire framing, and no writer/emitter change is in scope; this is a read-path correctness fix only.
- The `:353` witness un-skip precedent (a prior escalated-then-fixed witness) is the model for FR-010's lifecycle.
