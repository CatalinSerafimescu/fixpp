# Feature Specification: Dictionary index width and the MultiCharValue accessor type

**Feature Branch**: `102-dict-width-multichar-type`

**Created**: 2026-11-07

**Status**: Draft

**Input**: REMAINING-WORK ★ V1.0 FUNCTIONAL TRACK row 7, dictionary and codegen half: batch B84 (fixpp#654, L-098-4)
and batch B67 (fixpp#621, L-621-1). Two places where the library returns the wrong answer for a valid dictionary or
a valid value, with no error.

## Context & Problem

1. **Side-table index width (#654, L-098-4).** A loaded dictionary keeps every component's and every group's member
   fields in one side table each, and each definition records where its members start and how many there are, in
   16-bit fields. On a dictionary whose side table holds more than 65535 entries, a later definition's start wraps and
   a large count is clamped. The vendored `FIX50SP2.xml` already does this in its component table: the component
   `RgstDistInstGrp`'s member list is another definition's (098's wrap probe). `Dictionary::component_fields` and
   `group_fields` then return the wrong members, silently. `group_fields` feeds the table view's context-free group
   store, which the read path falls back to on a context miss. 098 refuses an overlay that would make a fitting table
   stop fitting, and admits a base that already does not fit.

   The same loaders narrow other values to 16 bits in the same functions (for example a component's name offset is
   clamped at 65535). Whether any of those can exceed 16 bits on a vendored base is not established; the fix must cover
   the class, not one field.

2. **MultiCharValue accessor (#621, L-621-1).** FIX's MultipleCharValue is space-separated single characters, e.g.
   `ExecInst(18)=1 2`. The code generator gives such a field a one-character accessor, and the one-character decoder
   refuses any value that is not one byte, so `exec_inst()` on `18=1 2` fails on `fixpp::v50sp2` and `fixpp::vlatest`.
   The typed builders take a single `char` for the same fields, so a multi-valued value cannot be built through them
   either. The runtime validator already treats these fields as strings, and QuickFIX types them as strings.

## Clarifications

### Session 2026-11-07 (orchestrator, owner-delegated)

- Q: Widen, or refuse a table that does not fit? → A: Widen. Refusing would refuse the vendored FIX50SP2 (#654's fix
  shape; the batch entry calls widening the expected answer).
- Q: 098's side-table fit check after widening? → A: Retired: no admitted input can overflow the wider fields, so the
  refusal arm becomes unreachable and is removed with its limit (decided at plan against 098's caps).
- Q: MultiCharValue's typed type? → A: A string view, as for MultipleStringValue and as QuickFIX types it. No new
  multi-character view type (nothing in fixpp or QuickFIX needs one; an application splits on spaces).
- Q: Builders too? → A: Yes. A typed builder for a MultiCharValue field takes the same string type its accessor
  returns; a single-character value is a one-character string.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - A component's or group's members are its own on every vendored dictionary (Priority: P1)

An application, or the library's own table build, asks a loaded dictionary for a component's or a group's member
fields.

**Why this priority**: Wrong members with no error, on a shipped dictionary, reaching the read path's context-free
group store.

**Independent Test**: For every vendored dictionary, compare each component's and each group's members as returned by
the dictionary with the members read independently from the source file.

**Acceptance Scenarios**:

1. **Given** the vendored FIX50SP2 dictionary, **When** `component_fields("RgstDistInstGrp")` is called, **Then** it
   returns that component's own members (today: another definition's).
2. **Given** every vendored base (QuickFIX XML and Orchestra), **When** every component's and every group's members are
   compared with the raw members, **Then** all match.
3. **Given** a base plus an overlay that adds definitions, **When** the merged dictionary is loaded, **Then** the same
   comparison holds and no side-table limit refuses it.

---

### User Story 2 - A multi-valued MultiCharValue field reads and builds (Priority: P1)

An application reads `ExecInst(18)=1 2` from an inbound `fixpp::v50sp2` or `fixpp::vlatest` message through the typed
accessor, or builds one through the typed builder.

**Acceptance Scenarios**:

1. **Given** a v50sp2 NewOrderSingle carrying `18=1 2`, **When** `exec_inst()` is read, **Then** it returns `"1 2"`.
2. **Given** a single-valued `18=1`, **Then** it returns `"1"`.
3. **Given** the v50sp2 and vlatest builders, **When** an `exec_inst` of `"1 2"` is built, **Then** the wire carries
   `18=1 2`, and the frame round-trips through the typed reader.
4. **Given** v42, v44 and FIXT.1.1, **Then** nothing changes (no MultiCharValue fields).

### Edge Cases

- A MultiCharValue field inside a repeating group (e.g. `LegExecInst(1384)`): same type, through the group entry.
- An owning (reified) message class: same string type as the view accessor.
- A dictionary that types a field `MULTIPLECHARVALUE` in one version and `CHAR` in another: each version's generated
  type follows its own dictionary.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001** (#654): Every index, offset and count a loader records for a component, group or field definition MUST
  hold its true value for any dictionary the loaders admit; no value is narrowed or clamped below its source's range.
- **FR-002** (#654): `Dictionary::component_fields` and `Dictionary::group_fields` MUST return each definition's own
  members, for every component and group of every vendored base, with and without an overlay.
- **FR-003** (#654): 098's side-table refusal MUST be removed or kept only if it can still fire at the new widths
  (decided at plan); B-098-1 and L-098-4 are updated to match.
- **FR-004** (#621): Every generated typed accessor of a MultiCharValue field (view and owning classes, top level and
  group entries) MUST return the same string type MultipleStringValue fields return, and decode any value.
- **FR-005** (#621): Every generated typed builder input for a MultiCharValue field MUST take that string type and
  write it verbatim.
- **FR-006**: The generated text of every other field is unchanged; every generated-file pin that moves is re-pinned
  with the reason, and the accessor census's value-type table maps MultiCharValue to the string kind.
- **FR-007**: The C ABI and Python binding are unchanged (neither exposes these types). The C++ changes are declared as
  breaking in the B&L (`ComponentRef`/`GroupRef` layout; MultiCharValue accessor and builder types).

### Key Entities

- **Component/group definition record**: identity, name, start and count of its members in the side table.
- **Typed accessor kind**: the generated value type of a field, derived from its dictionary type.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: The member comparison of US1.2 is RED on `fa778a8e` for `RgstDistInstGrp` on FIX50SP2 and GREEN on the
  feature head for every component and group of every vendored base.
- **SC-002**: `exec_inst()` on `18=1 2` is RED on `fa778a8e` and GREEN on the head, on v50sp2 and vlatest; the builder
  round-trip of US2.3 likewise.
- **SC-003**: Generated files other than those holding MultiCharValue fields are byte-identical; each moved pin names
  this feature.
- **SC-004**: Dictionary load time does not regress beyond the paired gate's band (the wider fields change only the
  records' size).

## Assumptions

- The side tables are built in `finalize` in a known order, so the expected start of every definition can be
  recomputed independently in a test.
- Generated headers are built, not checked in; their goldens and SHA pins are checked in and republished by hand.
- The precompiled builder libraries are C++ only and pre-release, so their ABI change needs no compatibility shim.

## Out of Scope

- B86 (#650, base loader input caps and the component-cycle guard), which follows this feature on the same `finalize`
  paths.
- An enum value-domain check for MultiCharValue fields (L-069-1, fixpp#637).
