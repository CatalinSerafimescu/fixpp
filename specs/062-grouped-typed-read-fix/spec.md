# Feature Specification: Grouped Typed-Read Path Fix

**Feature Branch**: `062-grouped-typed-read-fix`
**Created**: 2026-07-05
**Status**: Draft
**Input**: Fix the typed read path for repeating-group entries so typed field access on a generated group-entry flyweight compiles and returns correct values. Prerequisite for feature 061 (typed application messages), merged before it (mirrors how 057 unblocked reify).

## Context

Reading a repeating group's **entries** with typed accessors does not compile today. `wire::group_view<GroupT>::operator[]` (and its iterator) construct a group entry from a byte span (`group_view.hpp:34-37,42`), but every codegen-emitted entry class `G_<no_tag>` has only a default constructor and a `MessageView<Index> const&` constructor, and reads each field through that view (`emit_messages.cpp:209-263`). The two contracts are type-incompatible, so `group_view::operator[]` is ill-formed the moment it is instantiated on a generated flyweight. `msg.orders()` and `msg.orders().size()` compile (member functions of a class template instantiate lazily), but `msg.orders()[0].some_field()` does not.

The defect was masked because no test exercises a generated group-entry flyweight through `operator[]`: `tests/wire/repeating_group_equivalence_test.cpp` uses a hand-written span-constructible `TestLeg`, and `tests/codegen/typed_accessor_test.cpp` only calls `size()` / default-constructs an entry.

Consequence: feature 061's discriminating read/round-trip witnesses cannot be written for any grouped message (the majority of the 33 in-scope messages). This feature is the shared prerequisite that unblocks them, analogous to how 057 unblocked the reify read path.

## Clarifications

### Session 2026-07-05

- Q: Cost model for grouped-entry reads — lazy (pay-per-access) vs eager (materialized at parse time)? → A: No spec-level constraint. `/speckit-plan` selects the mechanism and its cost model freely, judged on correctness + FR-004 (zero per-access heap allocation + lifetime safety); neither lazy nor eager is mandated by the spec. If Phase-0 analysis shows one materially regresses the group-heavy hot path, that is a plan-level tradeoff to surface at `/plan` sign-off, not a spec requirement.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Read a repeating-group entry's typed fields (Priority: P1)

A developer parsing a FIX message with a repeating group (e.g. NewOrderList's orders, a MarketData snapshot's entries) obtains an entry via `group_view::operator[]`, a range-for over the group, or `iter()`, and reads the entry's business fields with correctly-typed values (string, char, integer, decimal), getting fail-closed errors for absent fields — exactly as they can already do for top-level message fields.

**Why this priority**: This is the entire feature. Without it, typed reads of any repeating group are a compile error, and the v1.0 typed-message scope (§XVIII.7) cannot ship its grouped messages.

**Independent Test**: Parse a real frame containing a repeating group, take `msg.<group>()[i]`, and assert each entry accessor returns the exact wire value; assert an absent required entry field returns the typed error. Compiles and passes over a **generated** flyweight (not a hand-written stub).

**Acceptance Scenarios**:

1. **Given** a parsed message with a repeating group of N entries, **When** `group.size()` and `group[i].<field>()` are evaluated for each `i`, **Then** `size()` == N and every entry accessor returns the exact typed value present on the wire.
2. **Given** an entry missing an optional/absent field, **When** that accessor is invoked, **Then** it returns the typed not-found error (never a defaulted or garbage value).
3. **Given** the same group, **When** it is enumerated via `operator[]` and via `iter()`/range-for, **Then** both enumerate identical entries in identical order (seam-#8 invariant preserved).

---

### User Story 2 - Read a nested repeating group inside an entry (Priority: P1)

A developer reads a repeating group that is itself nested inside a group entry (e.g. a leg group inside an order entry, party sub-IDs inside a party entry) — the entry exposes the nested group with the same typed accessors, recursively.

**Why this priority**: Several in-scope messages (SecurityDefinition legs, MassQuote quote-sets/quote-entries, party sub-IDs) have nested groups; a fix that only handles one level leaves them blocked.

**Independent Test**: Parse a message whose group entry contains a nested group; assert `entry.<nestedGroup>().size()` and `entry.<nestedGroup>()[j].<field>()` return correct values.

**Acceptance Scenarios**:

1. **Given** an entry containing a nested repeating group, **When** the nested group is accessed and enumerated, **Then** its size and per-nested-entry field values are exactly correct.

---

### User Story 3 - Entry lifetime is safe and non-degrading (Priority: P1)

A developer holds a group entry (by value) and reads from it while the parent parsed message is alive, without dangling references and without a per-access memory-arena cost.

**Why this priority**: A lifetime bug (dangling entry) or a per-access allocation would make the fixed API unsafe or unsuitable for the hot path (FR-003 zero-alloc discipline).

**Independent Test**: Under the sanitizer matrix (ASan/UBSan/TSan) and an allocation-tracking gate, iterate a group, hold and read entries, and observe no use-after-free and no per-access heap allocation while the parent message is alive.

**Acceptance Scenarios**:

1. **Given** a parsed message, **When** entries are obtained, held, and read while the parent message is alive, **Then** no use-after-free/out-of-bounds is reported by the sanitizer matrix and no per-access allocation occurs.
2. **Given** the parent message is destroyed, **When** an outstanding entry is used afterward, **Then** the contract is documented as undefined (entry borrows the parent), consistent with the existing flyweight lifetime model.

### Edge Cases

- Empty group (`NoXXX=0` or absent): `size() == 0`, `begin() == end()`, no entry dereference.
- Single-entry group; last entry (delimiter/extent correctness).
- Entry field that is absent vs present-but-empty.
- Group cap / oversized count (existing `OffsetTable::Config` per-instance cap behaviour preserved — no regression).
- Regression guard: a test that instantiates `operator[]`/`iter()` on a **generated** flyweight so the build/test breaks if this contract regresses (a compile-level or executed assertion, not a silently-skipped path).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: A group entry obtained from `group_view<G>` via `operator[]`, `iter()`, `begin()/end()`, or range-for MUST expose the same typed field accessors as the generated entry class (scalars: string/char/int/decimal; `field_value(tag)`), each returning the exact value for that entry's slice, with the existing fail-closed error semantics for absent fields.
- **FR-002**: Entry accessors for **nested** repeating groups MUST work recursively (an entry's `group<c, G_c>()` returns a usable `group_view` whose entries are themselves readable).
- **FR-003**: `operator[]` and `iter()` MUST enumerate identical entries in identical order (preserve the seam-#8 invariant already asserted in `repeating_group_equivalence_test.cpp`).
- **FR-004**: Entry read access MUST be lifetime-safe (an entry borrows the parent parsed message; no dangling when the parent is alive) and MUST NOT incur a per-access heap allocation (preserve zero-alloc hot-path discipline).
- **FR-005**: The codegen entry-class contract and the `group_view` entry contract MUST be aligned so a generated entry flyweight is directly obtainable from `group_view` — closing the type mismatch. The affected codegen output (entry classes in the shared `fixpp::<ns>::groups` namespace) MUST be regenerated deterministically from the codegen tool.
- **FR-006**: Discriminating witnesses MUST exercise **generated** flyweights (not hand-written stubs), covering at least: a single-level group with per-entry scalar + decimal fields, a nested-group case, an empty group, and the `operator[]`↔`iter()` equivalence — and MUST include a regression guard that fails the build/test if the entry-read contract regresses.
- **FR-007**: The change MUST NOT alter the C-ABI, the error enum, the wire framing/parsing of top-level fields, or the top-level message flyweight read behaviour, beyond what the group-entry read path strictly requires. No typed builders and no writer are added here.

### Out of Scope

- Feature 061's typed-message builders and read/round-trip witnesses (this only unblocks them).
- The generated-header install rule (FR-007 of 061), the codegen write-emitter (FR-015a), and the N/C/R message families.
- Any change to how top-level (non-group) message fields are read.

### Key Entities

- **Group entry flyweight** (`fixpp::<ns>::groups::G_<no_tag>`): the generated typed view over one repeating-group occurrence's byte slice.
- **`group_view<G>`**: the enumerable view over a group's instance slices (borrowed from the parent `OffsetTable` arena), yielding entries.
- **Group-entry slice**: one occurrence's bytes — a bare `tag=value\x01` field sequence with no message envelope (no 8=/9=/35=/10=).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Typed reads of repeating-group entries (single-level and nested) compile and return exact wire values over generated flyweights — demonstrated for at least 2 distinct grouped messages including one with a nested group.
- **SC-002**: The sanitizer matrix (ASan/UBSan/TSan) is clean on the new entry-read witnesses, and the allocation gate shows zero per-access allocations while the parent message is alive.
- **SC-003**: A regression guard exists such that reverting the fix breaks the build or a test (proven by construction — the guard exercises a generated entry through `operator[]`/`iter()`).
- **SC-004**: No change to C-ABI symbols, error enum, or top-level message read behaviour; existing Tier-1 suites remain green after codegen regeneration.

## Assumptions

- **Mechanism is a Phase-0 / plan decision** — not prejudged here. Candidates: (a) a self-contained typed field-reader constructible over a single non-enveloped entry slice, stored by value in the entry; (b) per-entry sub-views materialized into the `OffsetTable` per-message arena at parse time, borrowed by entries; (c) direct linear field-scan over the entry span in generated accessors. Selection is driven by lifetime safety (FR-004), zero-alloc (FR-004), and minimal wire-layer surface. Current analysis indicates a wire-layer capability is needed because entry accessors require a `MessageView`-grade `get<TAG>()` and no public constructor builds an Index-mode reader over a bare entry slice today.
- Group instance slices are already lifetime-stable (owned by the parent `OffsetTable` arena; slice `.data` points into the parent frame buffer), so entries that borrow slice bytes do not dangle while the parent message is alive — the dangling risk exists only on a "build a temporary view and point at it" route, which the chosen mechanism must avoid.
- Prerequisite 057 (reify + multi-char dispatch, PR #161) is merged.
- The codegen force-regen trap applies: after editing `emit_messages.cpp`, the tool must be rebuilt and `_codegen` markers cleared to force regeneration (configure-time `execute_process`, blind to emitter edits).
- This feature trips the Appendix A mandatory triggers **Wire format/parser** and **Codegen layout** → full mandatory controls (`/clarify`, `/analyze`, Codex Gate A, user `/plan` sign-off) and full Gate B before merge.
