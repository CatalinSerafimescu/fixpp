# Feature Specification: A Data field can carry any octets — atomic Length+Data emit in the C++ builders

**Feature Branch**: `091-data-field-bytes`

**Created**: 2026-09-24

**Status**: Draft

**Input**: User description: "fixpp #418 (batch B8): atomic Length+Data emit for DATA fields in
`wire::body_builder` / `entry_handle` and the generated builders, so a coupled Length+Data member can
carry arbitrary octets (SOH, C0 controls, 0x80–0xFF), with the Length derived from the octet count.
ONE atomic operation that appends both the Length and Data nodes or neither, validated against the
authoritative pair set; raw per-field byte setters were rejected at scoped Gate A round 1. INV-2 on
`field()`/`set_string` stays unchanged. Both codegen arms (top level and nested group entry) route
through the new operation; the Args member type stays source-compatible. Regenerate the golden
builder fixtures. Stage-1 pins are brought over and flipped. L-067-2 moves to the closed B&L file.
Out of scope: C-ABI, Python, the size caps, STRING fields admitting high-bit bytes."

**Issue**: fixpp#418 (batch B8 in the parent's `phases/phase-4/issue-batches.md`).

---

## Context — what is broken, and what was already decided

A FIX `data` field exists to carry octets a plain string cannot: `EncodedText(355)` holds text in a
`MessageEncoding(347)` other than ASCII, and `RawData(96)`/`XmlData(213)` hold binary or markup. Its
Length partner, which immediately precedes it, gives the octet count, and that count is what lets a
reader find the end of a value that contains SOH (TagValue v1.0 §4.2.4, §4.2.5, §4.3.7.3).

fixpp's C++ outbound builder — `wire::body_builder` and every generated `build_<Msg>` on top of it —
refuses any value with a byte outside `0x20–0x7E`, including a Data value. So **fixpp cannot send a
non-ASCII `EncodedText` at all**, and L-067-2 (live B&L file) records the gap. Feature 089 works
around it by sending one message as a hand-built frame through a test-only hook.

**Already decided, not re-opened here:**

- **Raw per-field byte setters are rejected** (scoped Gate A round 1, #418 comment 2026-09-12). An
  unguarded `field_bytes(tag, span)` lets a caller write `11=X<SOH>49=EVIL`, use the path on a
  non-Data tag, omit the Length, or send a Length that disagrees with the byte count. The replacement
  shape is one **atomic** Length+Data operation.
- **The pair set and the rule are written once** (O-4 of `.specify/426-428-length-data-pairs.md`). The
  standard pair table (`include/fixpp/core/length_data_pairs.hpp`) and the write-side pair checker
  (`wire::length_data_checker`) exist so this feature can reuse them; the C-ABI commit is the
  checker's first caller and `body_builder` is meant to be its second. A fifteenth private copy of
  the pair set is the defect #426 removed.
- **The C-ABI already has this capability** — `fixpp_msg_set_data` / `fixpp_entry_set_data` (#428,
  C-ABI 1.6) take the **Data** tag, derive the Length tag from the pair lookup, derive the Length
  value from the byte count, and refuse an empty value. That is the behavioural precedent for the
  C++ path.
- **The prerequisites are closed**: #426 (inbound scanners know every pair, so a round trip can be
  witnessed), #427 (FIX Latest dictionaries carry their pairs), #428 (C-ABI).

The earlier design note on branch `fix/418-data-field-bytes`
(`.specify/418-data-field-bytes.md`, v1, 2026-09-11) is **superseded input**: its API shape was
rejected, its §4 treats C-ABI setters as precedent that #428 has since replaced, and its "vlatest 0"
census predates #427.

---

## Clarifications

### Session 2026-09-24 (during `/speckit-specify`)

- Q: Should `body_builder::commit` refuse a malformed Length/Data pair written by hand through
  `field()`/`set_string`? → A: **Yes, refuse at commit**, reusing `wire::length_data_checker` as
  the C-ABI commit does (FR-008).
- Q: Should the operation honour pairs a dictionary declares outside the standard table? → A:
  **Yes**, with B-426-3 precedence (FR-009), after research the owner asked for:
  - **FIX standard.** TagValue v1.0 §4.2.5, §4.3.7 and the datatype table require every `data`
    field to be immediately preceded by its associated Length. FIX 4.4 Vol 1 "User Defined Fields"
    and fixtrading.org reserve 5000–9999 and 20000–39999 for user-defined fields. Nothing found
    restricts `data` to standard tags.
  - **QuickFIX/C++** (`Message::extractField` → `DataDictionary::isDataField`) **and QuickFIX/J**
    (`Message.extractField`) take data-ness from the dictionary. Both assume Length = Data − 1,
    with only 89/93 special-cased.
  - **QuickFIX/n** special-cases only XmlData(213).
  - **Length on send.** None of the three derives the Length; the caller writes both fields.
  - **fixpp C-ABI.** `fixpp_msg_set_data` already resolves pairs through
    `dict_hooks::for_table_view`, so a standard-only C++ builder would refuse what the C-ABI accepts
    for the same dictionary.
  - **Usage.** No public venue rules of engagement defining a custom Length+Data pair were found.
    That is "not found", not "absent".

### Session 2026-09-24 (`/speckit-clarify`)

- Q: How much extra build time may the commit-time pair check add to a typical message with no Data
  field? → A: **at most 3 %** on a representative builder benchmark, against a baseline taken on
  unmodified `main` before any edit; if the design cannot meet it, the figure goes back to the owner
  (SC-005).
- Q: Which error does the Data operation and the commit check return on refusal? → A: **existing
  wire errors, no new variant**, so the C-ABI error map is not touched (FR-004a).
- Q: Should generated-builder users get a way to set `MessageEncoding(347)` when they send an
  `Encoded*` field? → A: **Yes: an optional `message_encoding` Args member**, emitted as `347` in
  the payload and moved to the header by `send_impl`, and not enforced (FR-011a). Found during
  clarify: no builder, session or C-ABI surface sets 347 today, and US-1's reference to
  `args.message_encoding` described a member that did not exist.

---

## User Scenarios & Testing *(mandatory)*

The users are C++ application authors who build outbound FIX messages through fixpp — either the
generated `build_<Msg>(Args)` functions or `wire::body_builder` by hand.

### User Story 1 - Send non-ASCII text in an encoded Data field through a generated builder (Priority: P1)

An application author sets `args.encoded_text` and `args.message_encoding` (FR-011a) on a generated
Args struct to a value holding bytes outside printable ASCII — UTF-8, Shift-JIS, or a value containing
SOH — and calls the generated builder. The message is built; `EncodedTextLen(354)` equals the value's
octet count; `EncodedText(355)` carries the bytes verbatim; a reader re-parsing the frame recovers the
same bytes.

**Why this priority**: This is the whole of #418 — the field's purpose is unreachable today. It is
the path application code actually uses.

**Independent Test**: Build a `NewOrderSingle` with `encoded_text` holding SOH, a C0 control byte,
`0x80` and `0xFF` in turn; assert success, the exact bytes of both fields, and a re-parse that yields
identical octets.

**Acceptance Scenarios**:

1. **Given** a generated Args struct whose coupled Data member holds `"A\x01B"`, **When** the builder
   runs, **Then** the frame carries `354=3<SOH>355=A<SOH>B<SOH>` and re-parsing it yields a
   `355` value of exactly those three octets.
2. **Given** a coupled Data member holding `0x80` or `0xFF` octets, **When** the builder runs,
   **Then** the octets are emitted verbatim and the Length equals the octet count.
3. **Given** a coupled Data member inside a repeating-group entry, **When** the builder runs,
   **Then** the same holds inside that entry (the nested path behaves exactly as the top level).
4. **Given** the coupled Data member is unset, **When** the builder runs, **Then** neither the
   Length nor the Data field is emitted (unchanged behaviour).

---

### User Story 2 - The injection guard on every non-Data field stays exactly as strong (Priority: P1)

An author passes a value containing SOH (or any byte outside `0x20–0x7E`) to an ordinary STRING field
— through a generated builder or `body_builder::field()` / `entry_handle::set_string`. The call is
still refused with `wire_field_value_out_of_range` and the output buffer is untouched.

**Why this priority**: Opening a byte path must not open an injection path. The first design failed
review on exactly this.

**Independent Test**: The existing `SohInValue_RejectedBeforeAnyByteReachesOut` test (SOH in
`ClOrdID(11)`) passes **unedited**; new cases show that the Data operation refuses a tag that is not
the Data half of a known pair.

**Acceptance Scenarios**:

1. **Given** SOH in `ClOrdID(11)`, **When** the message is built, **Then** it is refused and `out` is
   untouched (unchanged).
2. **Given** a caller invokes the Data operation with a tag that is not the Data half of any known
   pair (e.g. `ClOrdID(11)`), **When** it runs, **Then** it is refused and nothing is appended.
3. **Given** a caller invokes the Data operation with a framing tag, or a pair whose Length half
   resolves to a framing tag, **When** it runs, **Then** it is refused and nothing is appended.

---

### User Story 3 - Hand-written `body_builder` callers get the same atomic operation (Priority: P2)

An author using `wire::body_builder` directly (top level, or an `entry_handle` inside a group) emits a
Data value by naming the Data tag and passing the octets once. They never write the Length field
themselves and cannot write a Length that disagrees with the value.

**Why this priority**: The generated builders depend on it, and hand-written callers (tests, the
session layer, integrators) need the same guarantee; but most application code goes through User
Story 1.

**Independent Test**: Unit tests on `body_builder` alone — success, each refusal, and all-or-nothing
rollback on arena exhaustion and on the body cap.

**Acceptance Scenarios**:

1. **Given** an empty builder, **When** the Data operation is called for tag 355 with 5 octets and
   the message is committed, **Then** the body holds `354=5<SOH>355=<5 octets><SOH>` in that order.
2. **Given** a Data value that would exceed the body cap or the arena, **When** the operation runs or
   commit runs, **Then** the existing typed error is returned and neither half of the pair survives.
3. **Given** an empty (zero-octet) Data value, **When** the operation runs, **Then** it is refused
   (TagValue §4.2.5: an empty value is malformed) and nothing is appended.

---

### User Story 4 - The limitation ledger and the witnesses tell the truth (Priority: P3)

A maintainer reading the live B&L file no longer finds L-067-2; the closed file carries it with both
stages' evidence (the stage-one pins and this feature's fix). The four stage-one pins that asserted
rejection now assert verbatim emit.

**Why this priority**: Bookkeeping, but a live row describing a fixed gap misleads every later reader.

**Independent Test**: L-067-2 is absent from `spec/behaviors-and-limitations.md` and present in
`spec/behaviors-and-limitations-closed.md` with the fix's PR; the four `_418` tests assert success.

**Acceptance Scenarios**:

1. **Given** the merged feature, **When** the live B&L file is read, **Then** L-067-2 is not in it.
2. **Given** the four stage-one tests, **When** they run on the fixed tree, **Then** they assert
   verbatim emit; **and** reverting the fix turns them RED (they are not vacuous).

---

### Edge Cases

- **SOH as the last octet of the value**: the frame still parses, because the reader uses the Length,
  not the next SOH.
- **A value longer than 9 999 octets / multi-digit Length**: the Length is the decimal octet count
  with no leading zeros; the body cap bounds it in practice.
- **Pairs where the Data tag is numerically below its Length tag** (93→89, 2372→2371) **or not
  adjacent** (1525→1527, 1678→1697): the Length half is still emitted first.
- **A repeating group whose delimiter (first) field is a pair's Length tag**: the Length must remain
  the first field of the entry; the atomic operation must satisfy the delimiter-first rule (INV-5).
  Whether any shipped dictionary has such a group is to be measured in planning.
- **The same pair set twice in one container** (the Data operation called twice for one tag): must
  not produce two Length/Data pairs silently; behaviour must match how the builder treats a repeated
  tag today. (Resolved in planning against current `field()` semantics.)
- **A caller writes the Length half or the Data half by hand through `field()`/`set_string`**
  alongside, or instead of, the Data operation: commit refuses it unless the result is a well-formed
  pair (FR-008).
- **A dictionary declares a pair whose tags are outside the standard table**: honoured (FR-009).
  A dictionary pair that reuses a standard tag is ignored in favour of the standard pair (L-426-2).
- **A dictionary pair whose Length half is a framing tag**: refused (FR-004), as the C-ABI does.
- **Header-class pairs sent through the session** (`SecureDataLen/SecureData` 90/91,
  `XmlDataLen/XmlData` 212/213, and `MessageEncoding(347)` from FR-011a): `send_impl` moves
  header-class tags ahead of the body. A pair MUST stay adjacent, Length first, and a Data value
  containing SOH MUST NOT be split by that reordering scan. A witness drives such a message through
  `send_impl` and re-parses the frame.
- **FIX Latest (`vlatest`) builders**: after #427 they carry their coupled pairs and must route the
  same way.

## Requirements *(mandatory)*

### Functional Requirements

**The operation**

- **FR-001**: `wire::body_builder` MUST provide one operation that, given a **Data** tag and a
  sequence of octets, appends the pair's Length field (value = the decimal octet count) immediately
  followed by the Data field (value = the octets, verbatim), **both or neither**.
- **FR-002**: `entry_handle` MUST provide the same operation for a field inside a repeating-group
  entry, with identical semantics.
- **FR-003**: The operation MUST resolve the Length tag from the Data tag through the authoritative
  pair lookup (the standard table in `include/fixpp/core/length_data_pairs.hpp`, reached through the
  wire layer's existing pair lookup) — never from a list private to the builder.
- **FR-004**: The operation MUST refuse, appending nothing: a tag that is not the Data half of a known
  pair; a framing tag, as either half; and an empty value.
- **FR-004a**: Refusals MUST use existing `core::error` wire variants. No new variant may be added,
  so the C-ABI error map and its expected-error table stay unchanged:
  - a framing tag, as either half → `wire_field_value_out_of_range`, which is what `field()`
    returns for a framing tag today;
  - a tag that is not the Data half of a known pair → `wire_unexpected_tag`;
  - an empty value → `wire_field_value_out_of_range`;
  - a malformed pair found at commit (FR-008) → `wire_invalid_field_format`.
- **FR-005**: The octets MUST NOT be subject to the printable-ASCII content guard. Any octet value
  `0x00–0xFF` is accepted.
- **FR-006**: A failure at any point (arena exhaustion, body cap, refusal) MUST leave the builder as
  it was before the call (INV-4, all-or-nothing), and a failed commit MUST leave `out` untouched.

**What does not change**

- **FR-007**: `field(tag, string_view)` and `entry_handle::set_string` MUST keep both conjuncts of
  INV-2 (framing-tag refusal and printable-content refusal) for every tag, unchanged. The test
  `SohInValue_RejectedBeforeAnyByteReachesOut` MUST stay byte-identical and passing.
- **FR-008**: `body_builder::commit` MUST refuse, leaving `out` untouched, a message in which any
  container (the top level, or any one repeating-group entry) holds a Length or Data field that does
  not form a well-formed pair: a Data field not immediately preceded by its Length, a Length not
  immediately followed by its Data, a Length that is not a positive decimal count, or a Data value
  whose octet count differs from its Length. The rule is the one `wire::length_data_checker` already
  applies at C-ABI commit (#428), applied by that same checker. Well-formed pairs written by hand
  (ASCII values through `field()`) still pass. This is a behaviour change for any hand-written C++
  caller that builds a malformed pair today, and it MUST get a B&L entry.
  *(Owner decision 2026-09-24.)*
- **FR-009**: The operation (FR-001/FR-002) and the commit check (FR-008) MUST also honour a pair
  that the governing dictionary declares with **both** tags outside the standard table, with the same
  precedence the inbound parser and the C-ABI already use (B-426-3: standard table first, and a
  dictionary may never override a standard pair — L-426-2). Where no dictionary is available to the
  builder, the standard table alone applies (as L-426-1 records for dictionary-free scanners), and
  the operation and the commit check MUST use the **same** pair lookup, so that no pair accepted by
  one is refused by the other. A generated builder MUST honour every pair its own dictionary
  declares. *(Owner decision 2026-09-24, after research; see Clarifications.)*
- **FR-009a**: The interop caveat MUST be disclosed in the B&L file, not enforced: QuickFIX/C++ and
  QuickFIX/J peers find a custom pair's Length only when its tag is the Data tag minus one, and
  QuickFIX/n peers split any data value other than XmlData(213) at SOH.

**Generated builders**

- **FR-010**: The code generator MUST route every coupled Length+Data member through the new
  operation, in **both** places it emits one: a top-level field and a field inside a repeating-group
  entry. Neither the generated code nor the generator may emit the Length half separately.
- **FR-011**: The generated Args member for a coupled pair MUST keep its current C++ type
  (`std::optional<std::string_view>`), so existing application code that sets it still compiles.
- **FR-011a**: For every message whose body can carry an `Encoded*` field, the generated Args MUST
  gain an optional `message_encoding` member. An `Encoded*` field is the Data half of a pair whose FIX
  field name begins with `Encoded`; the set is derived from the dictionary, not hand-listed. When the
  member is set, the builder emits it as `MessageEncoding(347)` in the payload. `Session::send_impl`
  already moves header-class tags ahead of the body (fixpp#422), so no session change is needed.
  The builder does **not** enforce FIX 4.4's "required if any Encoded fields are present": the
  caller decides. That this is not enforced MUST be recorded in the B&L file.
  *(Owner decision 2026-09-24.)*
- **FR-012**: The generator MUST never route a non-Data field through the new operation. This MUST be
  checked by a census over generated output that is first shown able to fire (it reports a
  deliberately mis-wired output as a violation).
- **FR-013**: The checked-in golden builder fixtures MUST be regenerated for every dictionary version
  whose output changes, including `vlatest`, and the diff MUST contain only the coupled-pair change.

**Witnesses and bookkeeping**

- **FR-014**: The four stage-one pins (`DataField_EncodedText_{SOH,ControlByte,0x80,0xFF}_…_418`,
  commit `68c8c769` on `fix/418-data-field-bytes`) MUST be brought onto this branch and flipped to
  assert: success, `EncodedTextLen(354)` equal to the octet count, `EncodedText(355)` equal to the
  input octets, and a re-parse through fixpp's inbound parser recovering the same octets.
- **FR-015**: A mutant that re-applies the printable-content guard to the new operation MUST turn the
  flipped pins RED; reverting it MUST turn them GREEN.
- **FR-016**: L-067-2 MUST move from the live B&L file to the closed file, carrying the stage-one pin
  commit and this feature's PR. No other live row may describe the gap as open.

### Key Entities

- **Length+Data pair**: a (Length tag, Data tag) couple. The Length field immediately precedes the
  Data field and holds the Data value's octet count. The authoritative set is the standard table
  (plus, per FR-009, possibly a dictionary's own pairs).
- **Coupled Args member**: one `std::optional<std::string_view>` in a generated `<Msg>Args` struct
  standing for a whole pair; the Length is never a separate member.
- **Stage-one pins**: the four tests that recorded the limitation as it behaved, before the fix.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Every octet value `0x00–0xFF` in a Data field survives a build → parse round trip
  unchanged, through a generated builder at the top level and inside a group entry (256/256 values).
- **SC-002**: 0 new ways to place SOH or a non-printable byte in a non-Data field: every existing
  injection-guard test passes unedited, and the new operation refuses every non-Data tag tried.
- **SC-003**: 100 % of coupled Length+Data emissions in regenerated builder output use the atomic
  operation, and 0 non-Data fields do (census, proven able to fire).
- **SC-004**: Existing application code that sets a coupled Args member compiles and behaves
  identically for ASCII values (no source change needed).
- **SC-005**: Building a representative message with no Data field (a `NewOrderSingle` with and
  without a repeating group) is at most **3 %** slower than the baseline taken on unmodified `main`
  before any code edit, measured on the same builder benchmark. If the design cannot meet 3 %, the
  measured figure goes back to the owner for review. The target is not silently relaxed.
- **SC-006**: L-067-2 is closed, with its evidence in the closed file, and no live limitation row
  claims the gap.

## Assumptions

- **Feature 089's B-05 stays on its test hook.** Moving it to the real builder changes an interop
  cell and needs a counterparty republish; that is a separate follow-up, not part of #418.
- **An empty Data value is refused**, not emitted as `Len=0`, and not silently omitted — TagValue
  §4.2.5 calls it malformed and the C-ABI already refuses it.
- **The size caps (`kBodyCap`, `kArenaCap`) are unchanged.** A large non-ASCII value that was refused
  for its content may now be refused for its size — a different, already-correct failure.
- **No allocation is added on the builder's hot path**; the operation uses the builder's existing
  fixed arena. If planning finds otherwise, B15 (#497, zero-allocation gate coverage) ordering
  applies.
- **Stage-one pins still pass on current `main`** after the 31 commits since their base; they are
  re-run and re-proven RED against a widened guard before being flipped.

## Normative References

- fixpp#418 — the issue and its 2026-09-12 Gate A round-1 comment (the rejected shape).
- `.specify/426-428-length-data-pairs.md` — O-4 (reuse the pair table and checker), §3 (Data→Length
  lookup), §5.3 (`length_data_checker`), L-426-2 (no dictionary override of a standard pair).
- `.specify/418-data-field-bytes.md` on branch `fix/418-data-field-bytes` — superseded input only.
- `brain/components/wire.md` §*Length+Data pairs* — why the pair set is shared.
- FIX TagValue Encoding v1.0 §4.2.4, §4.2.5, §4.3.7.3.
- `spec/behaviors-and-limitations.md` — L-067-2 (live until this feature closes it).
- `.specify/constitution.md` — `[const §XVII.1]` (Gate A for a public C++ API change),
  `[const §XVI.3]` (clarify mandatory for wire/codegen).

## Explicitly out of scope

- **The C-ABI.** It already has `fixpp_msg_set_data` / `fixpp_entry_set_data`; nothing changes there.
- **The Python binding** exposing a Data setter.
- **Changing `kBodyCap` or `kArenaCap`.**
- **Whether STRING fields should admit high-bit bytes** — #418 leaves it open; this feature does not
  touch the STRING guard.
- **Moving feature 089's B-05 off its test hook** (see Assumptions).
