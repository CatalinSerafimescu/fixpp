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

> **Superseded in part by the Gate A rulings** (Clarifications):
> - the seven v50sp2 Length members are deleted (the FR-011 carve-out);
> - FR-017's C-ABI behaviour change is in scope as C-ABI 1.9 BREAKING (FR-019);
> - `interpret_logon` refuses a Logon in which a Length is immediately followed by its paired Data
>   whose counted extent reaches or passes the end of the whole framed message, or whose following
>   byte is not SOH (FR-020, owner ruling during `/speckit-implement`);
> - `fixpp_msg_commit`'s Length+Data check feeds a group node an empty value, as `body_builder`
>   does, so a group whose count tag is a pair half is never read as that half (FR-021, owner ruling
>   2026-09-25, during `/speckit-implement`; it folds in fixpp#506).
>
> Only new C-ABI surface stays out of scope. The quote above is the original input, kept verbatim.

**Issue**: fixpp#418 (batch B8 in the parent's `phases/phase-4/issue-batches.md`).

---

## Context — what is broken, and what was already decided

A FIX `data` field exists to carry octets a plain string cannot: `EncodedText(355)` holds text in a
`MessageEncoding(347)` other than ASCII, and `RawData(96)`/`XmlData(213)` hold binary or markup. Its
Length partner, which immediately precedes it, gives the octet count, and that count is what lets a
reader find the end of a value that contains SOH (`[FIX50SP2 §3.3] Field data types`, catalogue row
W-008; informatively TagValue v1.0 §4.2.4, §4.2.5, §4.3.7.3).

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
  - *Superseded for the set-time operation by Session 2026-09-24 (Gate A round 1), ruling 1: the
    commit check keeps honouring dictionary pairs; `field_data`/`set_data` do not.*

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

### Session 2026-09-24 (Gate A round 1)

- Q: A caller-supplied `dict_hooks` makes SOH legal in any tag those hooks call Data, and nothing binds
  the hooks to the dictionary of the session that sends the payload (`send_impl` scans with its own
  dictionary), so SOH could reach a tag the sender and the peer treat as plain. Which pair set may
  the **set-time** operation use? → A: **Option (a): the standard table only.** `field_data` /
  `set_data` accept standard-table pairs only (`include/fixpp/core/length_data_pairs.hpp`, through
  the standard precedence), so SOH can never be emitted in a tag the sending session could treat as
  plain. Dictionary-declared (non-standard) pairs stay accepted by the **commit-time check**
  (FR-008, `wire::length_data_checker` with the builder's hooks): a hand-written custom pair written
  through `field()`/`set_string` is validated at commit. This diverges from the C-ABI, whose setter
  honours dictionary pairs because its hooks come from the handle's own session dictionary; the
  divergence is disclosed in the B&L file. **Follow-up** (fixpp#505): *verifiable session binding
  for dictionary pairs, option (b)* — C-ABI parity for custom pairs at set time.
  (FR-009.)
- Q: The QuickFIX XML loader misses five FIX 5.0 SP2 standard pairs (2494→2493, 2815→2814,
  43109→42684, 43110→42486, 43111→42982) because they are adjacent only inside components and
  groups, which its secondary walk never visits; the generated v50sp2 builders therefore leave them
  uncoupled. Which fix? → A: **Fix the loader, structurally, inside 091.** The secondary walk
  descends into `<component>` and `<group>` containers (FR-017), and a per-dictionary drift arm keeps
  each dictionary in step with the standard table (FR-018). (R-11.)
- Q: The fix couples the five pairs in the v50sp2 generated builders, which removes their separate
  caller-set Length members. Alias or delete? → A: **Delete, no alias.** The caller-set
  `std::optional<std::int64_t>` Length members of those pairs are removed from the v50sp2 Args; this
  is an owner-ratified source break, carved out of SC-004/FR-011 and disclosed in the B&L file.
- Q: The fix moves two read-tier SHA-256 pins. Accept? → A: **Yes, rebaseline** `v50sp2/Fields.hpp`
  and `v50sp2/Validator.hpp` in `tests/codegen/read_tier_byte_diff_test.cmake`, with a
  re-derivation recipe in that file's fixpp#427 style. Every other pin stays gated.

### Session 2026-09-24 (Gate A round 2)

- Q: The owner fixed SC-005's comparand as "a baseline taken on unmodified `main` before any edit";
  Article VIII §2 fixes the procedure as a paired merge-base run in one session. Does rebuilding the
  base in the measurement session conflict with the owner's wording? → A: **No** (reading recorded by
  the round-2 Opus judging, not a new owner ruling). The owner's wording fixes the **source** of the
  comparand: production code equal to unmodified `main`, never a post-change re-baseline. The base is
  therefore built in the measurement session from the current merge-base plus this branch's final
  bench source only, and `git diff --stat <merge-base> <base tree> -- src include tools cmake` MUST
  be empty — that empty diff is the "unmodified main" guarantee (quickstart §6).
- Q: FR-017 changes what `XmlLoader` reports for a user-loaded dictionary, not only for the shipped
  ones. Is that inside ruling 2? → A: **Yes, as a disclosed consequence** (reading recorded by the
  round-2 Opus judging, not a new owner ruling; the owner may overrule it): the
  ruling extends the loader walk, and the loader is one runtime API. The consequence is disclosed
  in B-091-4 and witnessed by C-2.5a.

### Session 2026-09-24 (Gate A round 3)

- Q: FR-017 turns a C-ABI commit that succeeded into a refusal. The case is a user-loaded dictionary
  whose custom Length+Data pair is declared adjacently only inside a `<component>` or `<group>`:
  `fixpp_msg_commit` returned `FIXPP_ERR_OK` and now returns `FIXPP_ERR_WIRE_CONFORMANCE`.
  `[const §X.7]` calls any call that used to succeed and now fails breaking. Does 091 accept a
  breaking C-ABI change, or scope the user-dictionary effect out? → A: **Accept: C-ABI 1.9
  BREAKING** (owner ruling). The loader keeps FR-017 as written. 091 bumps
  `FIXPP_C_ABI_VERSION_MINOR` to 9 in `include/fix/c_api/version.h`, with a history comment naming
  091/fixpp#418. It marks the behaviour change BREAKING (1.9) on `fixpp_msg_commit`
  (`include/fix/c_api/message.h`) and on `fixpp_dict_load_from_xml` (`include/fix/c_api/dict.h`),
  marks B-091-4 BREAKING, and adds a C-ABI test that pins the refusal (FR-019). The alternative,
  a component/group walk that marks only standard-table pairs, is rejected: it would leave the
  loader's answer wrong for the dictionary the user wrote. (Population since derived: FR-019.)

### Session 2026-09-24 (`/speckit-implement`, after T021)

- Q: `interpret_logon` stops scanning at a malformed Length count and validates only the fields it
  saw, so an `EncryptMethod(98)` other than `0` placed after the count is never seen and the Logon is
  accepted — contrary to `[const §XII.7]`. It is reachable on `main` through the standard pair
  RawDataLength(95)/RawData(96), and FR-017 widens it to component/group-only pairs of a user-loaded
  dictionary. Measured, not only read: the T070 witnesses show the Logon accepted on the function,
  the acceptor arm and the initiator arm. How does 091 handle it? → A: **Fold the fix into 091**
  (owner ruling). `interpret_logon` refuses a Logon carrying any malformed count (FR-020). The other
  `length_data_carry` scan sites were enumerated (evidence file §*Malformed-count scan sites*); each
  fails closed or is equivalent to the peer omitting the later fields, so the fix is scoped to
  `interpret_logon`.

### Session 2026-09-25 (`/speckit-implement`, after T053)

- Q: The C-ABI commit check `check_length_data` (`src/capi/message_write.cpp`) feeds a group node
  to `wire::length_data_checker::observe` with the group's instance-count digits as its value,
  where `body_builder::commit` feeds an empty value (R-4 and C-1.7 make the empty value normative,
  because count digits can complete a pair). Measured, not only read (evidence file §*/simplify
  (T053)*, M1): on a session with no dictionary, `fixpp_msg_group_begin(354)` with one instance,
  then `fixpp_msg_set_bytes(355, "x")`, commits `FIXPP_ERR_OK` and emits
  `35=D␁354=1␁79=A1␁355=x␁`. R-4 recorded this as the pre-existing follow-up fixpp#506, outside
  091. How does 091 handle it? → A: **Fold the fix into 091** (owner ruling 2026-09-25): FR-021.
  The delta (FR-021 and its plan, tasks, data-model, quickstart, research and contract edits) goes
  through a **scoped Gate A round**, as FR-020's did.

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
3. **Given** a caller invokes the Data operation with a framing tag, **When** it runs, **Then** it
   is refused and nothing is appended.
4. **Given** a builder constructed with hooks from a dictionary that declares a custom pair
   (5001, 5002), **When** the Data operation is called for 5002 with a value holding SOH, **Then** it
   is refused (`wire_unexpected_tag`) and nothing is appended: the set-time operation knows the
   standard pairs only (FR-009).

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
2. **Given** a Data value that would exhaust the arena, **When** the operation runs, **Then**
   `wire_frame_too_large` is returned, and committing afterwards yields a body byte-identical to the
   body built without that call (neither half is serialized; INV-4).
3. **Given** a Data value that fits the arena but pushes the serialized body over the body cap,
   **When** commit runs, **Then** `wire_frame_too_large` is returned, `out` is untouched, and the
   builder's accumulated contents are preserved (commit never mutates them).
4. **Given** an empty (zero-octet) Data value, **When** the operation runs, **Then** it is refused
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

- A Logon in which a Length is immediately followed by its paired Data whose counted extent reaches
  or passes the end of the whole framed message, or whose following byte is not SOH, is refused,
  whatever follows it (FR-020). An orphan Length, one whose
  next field is not its paired Data, is read as a plain value; its count is never applied, so a
  later `EncryptMethod(98)` is still seen.
- **A C-ABI group whose count tag is a pair half** (a Length or a Data under the handle's pairs;
  reachable where `fixpp_msg_group_begin` / `fixpp_entry_group_begin` accept such a tag, FR-021):
  once the handle, open-builder and group-grammar checks pass (an empty instance or, on a
  dictionary session, a delimiter or context miss is `FIXPP_ERR_TYPE_MISMATCH` first, as before),
  `fixpp_msg_commit` refuses it with `FIXPP_ERR_WIRE_CONFORMANCE`, whatever its instance count and
  whatever sits next to it, exactly as `body_builder::commit` refuses the C++ twin (C-1.7). Before
  FR-021 it was refused unless its count digits completed the pair: a group tagged 354 with one
  instance followed by a one-byte 355 (the Length shape), or a `354=1` followed by a group tagged
  355 with a one-digit instance count (the Data shape), committed.

- **SOH as the last octet of the value**: the frame still parses, because the reader uses the Length,
  not the next SOH.
- **A value longer than 9 999 octets / multi-digit Length**: the Length is the decimal octet count
  with no leading zeros; the body cap bounds it in practice.
- **Pairs where the Data tag is numerically below its Length tag** (93→89, 2372→2371) **or not
  adjacent** (1525→1527, 1678→1697): the Length half is still emitted first.
- **A repeating group whose delimiter (first) field is a pair's Length tag**: the Length must remain
  the first field of the entry; the atomic operation must satisfy the delimiter-first rule (INV-5).
  Measured in R-6 (recipe there): the `*PaymentStreamFormula` groups are Length-delimited; coupled
  after FR-017.
- **The same pair set twice in one container** (the Data operation called twice for one tag): must
  not produce two Length/Data pairs silently; behaviour must match how the builder treats a repeated
  tag today. Resolved: appends, as `field()` does (C-1.11).
- **A caller writes the Length half or the Data half by hand through `field()`/`set_string`**
  alongside, or instead of, the Data operation: commit refuses it unless the result is a well-formed
  pair (FR-008).
- **A dictionary declares a pair whose tags are outside the standard table**: refused by the set-time
  operation; a well-formed hand-written ASCII pair through `field()`/`set_string` is accepted at
  commit, and a malformed one refused (FR-009). A dictionary pair that reuses a standard tag is
  ignored in favour of the standard pair (L-426-2).
- **A dictionary pair whose Length half is a framing tag**: unreachable at set time (no standard pair
  tag is a framing tag, asserted at compile time — R-3); a hand-written one is refused by `field()`'s
  INV-2 framing conjunct before it reaches commit.
- **FIX 5.0 SP2 standard pairs declared adjacently only inside a component or group** (2494→2493,
  2815→2814, 43109→42684, 43110→42486, 43111→42982): coupled after the loader fix (FR-017), so the
  generated builder routes them like every other pair. Without that fix FR-008 would refuse v50sp2
  `PayManagementRequest`/`PayManagementReport` whenever the pair is set, because their top level
  emits 2814 before 2815.
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
- **FR-004**: The operation MUST refuse, appending nothing: for `set_data`, a handle with no owner or
  one that is not the innermost open entry; a framing Data tag; a tag that is not the Data half of a
  standard pair (FR-009); and an empty value. (A framing Length half cannot arise at
  set time: no standard pair tag is a framing tag, asserted at compile time, R-3.)
- **FR-004a**: Refusals MUST use existing `core::error` wire variants. No new variant may be added,
  so the C-ABI error map and its expected-error table stay unchanged:
  - a `set_data` handle with no owner, or one that is not the innermost open entry →
    `wire_invalid_field_format` (as `set_string`), checked before any resolution (C-1.4b);
  - a framing Data tag → `wire_field_value_out_of_range`, which is what `field()` returns for a
    framing tag today;
  - a tag that is not the Data half of a standard pair → `wire_unexpected_tag`;
  - an empty value → `wire_field_value_out_of_range`;
  - a malformed pair found at commit (FR-008) → `wire_invalid_field_format`.
- **FR-005**: The octets MUST NOT be subject to the printable-ASCII content guard. Any octet value
  `0x00–0xFF` is accepted.
- **FR-006**: A refusal or an arena exhaustion in the operation MUST leave the builder's contents and
  its serialized output as they were before the call (INV-4: the container is rolled back to its
  pre-call size, so a later commit serializes exactly what it would have without the call). A failed
  commit MUST leave `out` untouched and the accumulated contents unchanged. Arena capacity consumed
  by a failed call is **not** reclaimed, as for every existing append (the arena is a null-upstream
  monotonic resource); this MUST be stated in the API comment.

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
  caller that builds a malformed pair today, and it MUST get a B&L entry. For generated builders it
  refuses nothing, provided every standard pair whose two halves appear at one level is coupled
  (FR-010, FR-017); without FR-017, v50sp2 `PayManagementRequest`/`PayManagementReport` would be
  refused whenever `EncodedPostTradePaymentDesc` is set (Data emitted before its Length).
  *(Owner decision 2026-09-24.)*
- **FR-009**: The pair sets differ by stage, and the set-time one MUST be a subset of the commit-time
  one, so nothing `field_data`/`set_data` appends is refused at commit:
  - **Set time** (FR-001/FR-002): the **standard table only**, reached through the standard
    precedence (`dict_hooks::none().length_tag_for_data`). A Data tag that only a dictionary pairs is
    refused (`wire_unexpected_tag`) even when the builder holds that dictionary's hooks.
  - **Commit time** (FR-008): the builder's `dict_hooks` (the constructor argument, `none()` by
    default), with B-426-3 precedence: standard table first, and a dictionary pair only when neither
    tag is standard (L-426-2). So a hand-written custom pair is checked for well-formedness.
  - **Why this closes the injection path.** A custom pair can only be written by hand through
    `field()`/`set_string`, which keep INV-2, so its Data value is printable ASCII and holds no SOH.
    SOH can reach the wire only in a standard Data tag, which every fixpp scanner — the sending
    session's included — reads by count, whatever dictionary it holds.
  - **Divergences from the C-ABI**, disclosed in the B&L file: the C-ABI setter also honours
    dictionary pairs (its hooks come from the handle's own session dictionary); and a repeated call
    appends a second pair here, where `fixpp_msg_set_data` upserts (C-1.11).
  - **Generated builders** couple every standard pair whose two halves appear at one level of a
    message (FR-010), which the loader fix makes equal to each dictionary's own coupling (FR-017).
    Codegen over a custom dictionary is unsupported: `fixpp-codegen` is not installed and runs over
    the shipped dictionaries only.
  *(Owner decisions 2026-09-24 — specify-time, then Gate A round 1 ruling 1; see Clarifications.)*
- **FR-009a**: The interop caveat MUST be disclosed in the B&L file, not enforced, by **extending
  L-426-3** (which already records QuickFIX's `tag - 1` Length lookup and its XmlData split) with the
  send-side consequence: fixpp can now send a Data value holding SOH, which QuickFIX/C++ and
  QuickFIX/J split unless the Length tag is Data − 1, and QuickFIX/n splits unless the tag is
  XmlData(213). No near-copy row is added.

**Generated builders**

- **FR-010**: The code generator MUST route every coupled Length+Data member through the new
  operation, in **both** places it emits one: a top-level field and a field inside a repeating-group
  entry. Neither the generated code nor the generator may emit the Length half separately. "Coupled"
  MUST cover every **standard** pair whose two halves appear at one level of a message, for every
  shipped dictionary — checked against the standard table, not against the loader's own answer
  (C-2.2).
- **FR-011**: The generated Args member for a coupled pair MUST keep its current C++ type
  (`std::optional<std::string_view>`), so existing application code that sets it still compiles.
  **Carve-out (owner-ratified source break):** for the five FIX 5.0 SP2 pairs FR-017 newly couples,
  the separate caller-set `std::optional<std::int64_t>` Length member is **deleted, with no alias**,
  and the Data member becomes the coupled member. The B&L row MUST state both break shapes: a
  designated-initializer or member-access caller gets a compile error; a positional
  aggregate-initializer caller may silently shift into the next member.
- **FR-011a**: For every message whose body can carry an `Encoded*` field, the generated Args MUST
  gain an optional `message_encoding` member. An `Encoded*` field is the Data half of a standard pair (the standard table) whose FIX
  field name **contains** `Encoded`, so FIX 5.0 SP2's `DerivativeEncodedIssuer(1278)`,
  `DerivativeEncodedSecurityDesc(1281)` and `InstrumentScopeEncodedSecurityDesc(1621)` are included,
  though a "begins with" rule misses them. The set is derived from the dictionary, not hand-listed. When the
  member is set, the builder emits it as `MessageEncoding(347)` in the payload. `Session::send_impl`
  already moves header-class tags ahead of the body (fixpp#422), so no session change is needed.
  The builder does **not** enforce FIX 4.4's "required if any Encoded fields are present": the
  caller decides. That this is not enforced MUST be recorded in the B&L file.
  *(Owner decision 2026-09-24.)*
- **FR-012**: The generator MUST never route a non-Data field through the new operation. This MUST be
  checked by a census over generated output that is first shown able to fire (it reports a
  deliberately mis-wired output as a violation).
- **FR-013**: The checked-in golden builder fixtures MUST be regenerated for every dictionary version
  whose output changes, including `vlatest`. The diff MUST contain only these transformations, and
  it MUST be validated structurally against an expected manifest derived independently of the
  generator (C-2.2's census), not by a token filter
  (C-2.4):
  - (a) every coupled call site rerouted from the two-call form to one `field_data`/`set_data` call;
  - (b) the `message_encoding` member and its emit (FR-011a) on exactly the messages FR-011a's rule selects;
  - (c) on v50sp2 only, the five newly coupled pairs (FR-017): their Length member deleted, their Data
    member coupled, and their two separate emits replaced by one coupled call at the Length's
    position.

**Witnesses and bookkeeping**

- **FR-014**: The four stage-one pins (`DataField_EncodedText_{SOH,ControlByte,0x80,0xFF}_…_418`,
  commit `68c8c769` on `fix/418-data-field-bytes`) MUST be brought onto this branch and flipped to
  assert: success, `EncodedTextLen(354)` equal to the octet count, `EncodedText(355)` equal to the
  input octets, and a re-parse through fixpp's inbound parser recovering the same octets.
- **FR-015**: A mutant that re-applies the printable-content guard to the new operation MUST turn the
  flipped pins RED; reverting it MUST turn them GREEN.
- **FR-016**: L-067-2 MUST move from the live B&L file to the closed file, carrying the stage-one pin
  commit and this feature's PR. No other live text may describe the gap as open. Every live mention
  MUST be updated in the same change: the separate "L-067-2 is unchanged … (fixpp#418)" bullet in the
  B&L #426/#428 limitations block; the "#418 … tracked open" comments in
  `tests/interop/conversation/support/conv_wire.hpp` (the hand-built-frame route) and
  `tests/interop/conversation/conv_cell_test.cpp` (B-05), which stay on their hook per the
  Assumptions and MUST say why rather than that #418 is open; the `include/fixpp/wire/
  length_data_check.hpp` header comment ("#418 is meant to be its second caller") and
  `brain/components/wire.md`'s "#418's `body_builder` must reuse" line become past tense. Re-derive
  the set with `git grep -nE '#418|fixpp ?#418' -- . ':!specs/091-data-field-bytes'` before closing
  (a bare `418` matches FIX tag 418). The W-008 row in `spec/feature-catalogue.md`
  gains this feature's evidence.

**Dictionary loader (owner decision, Gate A round 1)**

- **FR-017**: The QuickFIX XML loader (`LoaderState::detect_length_pairs`) MUST pair a LENGTH field
  with the DATA/XMLDATA field that immediately follows it inside **every** container that lists
  fields — header, trailer, each message, each `<component>` definition and each `<group>` at any
  depth — in addition to its `<fields>`-order rule (in the newly visited component and group
  containers a non-`<field>` child breaks adjacency; the existing direct-`<field>` walk of header,
  trailer and messages is unchanged, and the groups inside them belong to the new group walk, R-11). For FIX 5.0 SP2 this adds exactly the five
  standard pairs named in Clarifications; it MUST add no non-standard pair for any shipped dictionary
  (the union drift test fails otherwise). For the shipped dictionaries inbound parsing is
  unaffected (every scanner resolves standard tags from the standard table alone; FR-020 separately
  changes the Logon verdict when a count is malformed). For a
  **user-loaded** dictionary (`XmlLoader` is a public runtime API), a non-standard pair declared
  adjacently only inside a component or group becomes a dictionary pair, so `has_nonstandard_pair()`
  can flip, scanners read its Data by count, `fixpp_msg_set_data` and both commit checks honour
  it, and the SOH-in-value refusal no longer applies to its Data tag (`fixpp_msg_set_string` / `fixpp_entry_set_string` at set time; any setter at commit when the value is correctly Length-prefixed; `fixpp_session_send` of such a well-formed Data) (B-091-4). Through the C-ABI this is a breaking change, C-ABI 1.9 (FR-019). The new containers are visited in the order R-11 states, and `mark_pair`'s existing
  first-writer rule settles a Length adjacent to two Data fields; a synthetic-dictionary test pins
  the component, group-in-component, group-under-message, group-in-group, group-under-header,
  group-under-trailer, break-on-non-field, first-writer and visit-order cases (C-2.5a). What moves on
  the shipped set is the dictionary's own answer
  (`Dictionary::length_pair_data_tag`, `table_view::length_pair_data_tag`, `FieldRef`) on FIX 5.0
  SP2, and the two read-tier pins `v50sp2/Fields.hpp` and `v50sp2/Validator.hpp`, which are
  rebaselined with a re-derivation recipe (C-2.5).
- **FR-018**: A per-dictionary drift arm MUST check, for **each** shipped dictionary, that every
  standard pair whose two tags both appear in some message expansion of that dictionary is paired by
  that dictionary's loader. It MUST be shown RED on the unfixed loader (FIX 5.0 SP2, the five pairs)
  before FR-017 lands. Each arm also asserts that its probed standard-pair set is non-empty, and
  prints it, so a leg whose probe collects nothing fails rather than passes; Orchestra FIX Latest is
  one of the arms. That non-empty assertion is shown RED on a non-FIX50SP2 leg by a mutant
  (quickstart §3).
- **FR-019** (owner decision, Gate A round 3; widened by FR-020 and FR-021): FR-017's, FR-020's
  and FR-021's effects on the C-ABI MUST be shipped as
  **C-ABI 1.9 BREAKING** under `[const §X.7]`:
  - `FIXPP_C_ABI_VERSION_MINOR` becomes 9 in `include/fix/c_api/version.h`, with a history comment
    naming 091/fixpp#418.
  - The affected population is **derived**, not listed by example: research.md R-11's *C-ABI 1.9
    population recipe* is re-run and every export is classified. It is carried out under the
    round-3 ruling and needs no further owner decision. Every FR-017 note below says that a
    Length+Data pair a loaded dictionary declares only inside a component or group is now a
    dictionary pair, then names that declaration's own effect; the FR-020 and FR-021 clauses state
    their own cause. §X.7 classes a success turned into a failure, and a
    failure turned into a different failure, as BREAKING whatever the documentation said. The
    per-export classification is data-model.md Appendix A, whose Symbol column equals
    `tests/abi/golden/fixpp_capi_symbols.txt` (plan Phase 0b checks it).
    - **BREAKING (1.9) notes on declarations:**
      - `fixpp_dict_load_from_xml` (`include/fix/c_api/dict.h`), the root cause: same return code,
        but the dictionary it yields now carries the pair;
      - `fixpp_msg_commit` (`include/fix/c_api/message.h`): a malformed pair of that kind returned
        `FIXPP_ERR_OK` and now returns `FIXPP_ERR_WIRE_CONFORMANCE`; and, as its own FR-021 clause,
        a group whose count tag is a pair half, which committed when its instance-count digits
        completed the pair, now returns `FIXPP_ERR_WIRE_CONFORMANCE` (FR-021);
      - `fixpp_session_send` (`include/fix/c_api/session.h`), through `Session::send_impl`'s
        `length_data_carry` scan over the session dictionary's hooks: a malformed pair of that kind
        returned `FIXPP_ERR_OK` and now returns `FIXPP_ERR_APP_PAYLOAD_MALFORMED`; and a count that
        covers a following field (e.g. 43, 122 or a header-class tag) is now transmitted verbatim
        as Data, not excised or reordered; for a send it refuses, the toApp callback
        (`fixpp_session_register_send_callback`) is not invoked, since `send_impl` returns the
        refusal before its toApp parse;
      - `fixpp_msg_set_data` and `fixpp_entry_set_data` (`include/fix/c_api/message.h`), failure →
        different failure: for the Data tag of such a pair, the unfixed loader left it unpaired and
        the call returned `FIXPP_ERR_TYPE_MISMATCH`; now that refusal no longer fires and the call
        reaches the later refusals its declaration documents, e.g. `FIXPP_ERR_WIRE_CONFORMANCE` for
        `len == 0` (both), or `FIXPP_ERR_DICT_CONFIG` for a half not declared for the MsgType
        (`fixpp_msg_set_data`). Their success widening stays additive (the B-091-4 bullet below);
      - `fixpp_session_register_callback` (`include/fix/c_api/session.h`), the `fixpp_recv_cb`
        delivery contract: an inbound message carrying a malformed pair of that kind, delivered
        before, is now dropped as a parse error by `Session::parse_and_dispatch_` (the count's end
        byte is not SOH, `OffsetTable::build` refuses), silently and with no Reject;
      - `fixpp_session_register_send_callback` (`include/fix/c_api/session.h`): for a send
        `fixpp_session_send` refuses because of such a malformed pair, the toApp callback is not
        invoked (its own note, task T075; the view's content effect stays in the reader paragraph);
      - the **inbound reader family**, as **one** BREAKING (1.9) paragraph in `message.h`'s shared
        accessor preamble ("Return codes common to all accessors"), not a marker per reader. It
        names `fixpp_msg_get_{string,bytes,int,double,decimal}`, `fixpp_msg_has_tag`,
        `fixpp_msg_version`, `fixpp_msg_get_msg_type`,
        `fixpp_msg_field_count`, `fixpp_msg_field_at`, `fixpp_msg_get_group`,
        `fixpp_group_get_field_{string,int,double,decimal}` and `fixpp_group_get_nested_group`,
        says it covers the inbound, clone and toApp views (the last is what
        `fixpp_session_register_send_callback` exposes), and says a counted Data of such a pair can
        absorb fields these readers used to return (a getter that returned `FIXPP_ERR_OK` can now
        return `FIXPP_ERR_TAG_NOT_FOUND`; `field_at` can go out of range; `has_tag` and
        `field_count` change value; `fixpp_msg_version`'s `appl_ver_id` member can become null,
        tag 8 being fixed first by the Framer; `fixpp_msg_get_msg_type` can return
        `FIXPP_ERR_TAG_NOT_FOUND` when such a pair precedes 35, which is reachable while the
        session's `validate_inbound_messages` is unset, since that header-order check is the only
        rule placing 35 third; re-derive by grepping `validate_inbound_messages` in `src/capi` —
        an empty result means no C-ABI setter exists, so read its default in
        `include/fixpp/session/session_config.hpp`).
    - **BREAKING with no carrying declaration**, recorded in the `version.h` history comment
      (§X.7): a frame a pre-1.9 engine stored with a malformed pair of that kind now fails replay
      (`build_replay_frame`) and is gap-filled rather than resent; the session's header and Logon
      scans (`scan_frame_header`, `interpret_logon` in `admin_messages.cpp`, the store's
      `frame_has_genuine_tag554` masking) read such a Data by count.
    - **BREAKING, FR-020's Logon refusal:** `interpret_logon` refuses a Logon in which a Length is
      immediately followed by its paired Data whose counted extent reaches or passes the end of the
      whole framed message, or whose following byte is not SOH, whether the pair is such a pair or
      a standard pair (RawDataLength(95) and RawData(96), so shipped dictionaries are affected too);
      a Logon of that shape that was accepted is now refused, on either role (the C-ABI's
      `established` / `ever_established` latches are set by `onLogon` whatever the role). The
      observers are every C-ABI call whose result depends on the session having logged on (FR-020
      lists them; research.md R-11 step 6 derives them). Each observer has a declaration in
      `session.h`, so each carries its own BREAKING (1.9) FR-020 clause (`[const §X.7]`: "each
      affected declaration"), and `fixpp_session_register_send_callback`'s note also carries its
      FR-019 carry-refusal clause; the `version.h` history names every one of them too.
    - **Additive** (failure turned into success; no §X.7 marker, listed in B-091-4): the widenings
      named in the next bullet.
    - **UNCHANGED:** every other export, classified row by row in data-model.md Appendix A. The
      condition is the recipe's step 5: no path to its steps 1–4, that is, no pair lookup at call
      time and no read of a view the pair-aware parse built, and not a step-6 handshake observer
      (FR-020); any pair effect of the values such an export writes surfaces at `fixpp_msg_commit`.
  - B-091-4 is marked BREAKING and names the BREAKING effects above. The **additive** widenings,
    each a failure turned into a success, are listed next to them: `fixpp_msg_set_data` and
    `fixpp_entry_set_data` now accept a well-formed such pair (their failure → different failure
    case is BREAKING, above); `fixpp_msg_set_string` and `fixpp_entry_set_string` no longer refuse an
    SOH-bearing value for its Data tag (both gate that refusal on `length_tag_for_data(tag) == 0`);
    and a correctly Length-prefixed, SOH-bearing Data of that pair, written through any setter,
    which failed commit with `FIXPP_ERR_WIRE_CONFORMANCE` before, now commits; and the same
    well-formed, SOH-bearing Data sent through `fixpp_session_send`, which returned
    `FIXPP_ERR_APP_PAYLOAD_MALFORMED` before, now returns `FIXPP_ERR_OK`.
  - The PR description carries the BREAKING declaration.
  - No symbol, signature or error code changes. The refusals use the existing
    `FIXPP_ERR_WIRE_CONFORMANCE` and `FIXPP_ERR_APP_PAYLOAD_MALFORMED` (FR-004a).
  - A C-ABI test loads a synthetic dictionary whose custom pair is declared adjacently only inside a
    component, under C-2.5a's non-adjacency conditions (plan Phase 0b names the load path and the
    setters). It commits a malformed instance of that pair through `fixpp_msg_commit` and asserts
    `FIXPP_ERR_WIRE_CONFORMANCE`. A focused assertion in the same fixture pins the additive
    widening: a well-formed instance whose Data holds SOH, written with `fixpp_msg_set_string`, is
    accepted at set time and commits `FIXPP_ERR_OK` after FR-017 (plan Phase 0b). A second focused
    assertion pins the `set_data` failure → different failure: `fixpp_msg_set_data(5002, len = 0)`
    returns `FIXPP_ERR_TYPE_MISMATCH` on the unfixed loader (RED) and `FIXPP_ERR_WIRE_CONFORMANCE`
    after FR-017 (GREEN).
  - The test asserts only that post-change value. It is written first and shown RED on the unfixed
    loader, where the result is `FIXPP_ERR_OK`; that pre-change form is recorded in the commit, so
    the RED → GREEN step witnesses the break rather than only the new state.
  - More witnesses, each written first and RED on the unfixed loader (plan Phase 0b):
    - **send:** on a loopback session (`tests/capi/capi_loopback_support.hpp`) whose session
      dictionary is a full shipped dictionary with one injected component-only custom pair,
      `fixpp_session_send` of a malformed instance (`35=D␁5001=2␁5002=abc␁`) asserts
      `FIXPP_ERR_APP_PAYLOAD_MALFORMED`; the unfixed loader returns `FIXPP_ERR_OK`;
    - **inbound drop:** a fixpp 1.9 peer cannot send the malformed frame, so this witness is at the
      session/wire layer: `Parser<Index>` over the synthetic dictionary's `table_view` refuses the
      malformed frame; the unfixed loader parses it as two plain fields. It proves the effect the
      `fixpp_session_register_callback` note documents;
    - **readers:** at the same layer and over the same synthetic table, two frames whose
      `parse()` each asserts a value (so a parse refusal cannot pass as an absorbed field):
      `…5001=8␁5002=a␁1137=9␁…`, where tag 1137 reads `"9"` on the unfixed loader (RED) and is
      absent after FR-017 (GREEN), proving the `fixpp_msg_version` effect; and
      `8=…␁9=…␁5001=6␁5002=a␁35=D␁…`, where `msg_type()` is `"D"` on the unfixed loader (RED) and
      empty after FR-017 (GREEN), proving the `fixpp_msg_get_msg_type` effect.
  - The `[const §X.6]` controls for a breaking C-ABI change apply.
- **FR-020** (owner ruling 2026-09-24, during `/speckit-implement`): `session::interpret_logon` MUST
  refuse with the existing `core::error::session_invalid_logon` a Logon that carries a **malformed
  paired count**. That is exactly the case in which `length_data_carry::read_value`
  (`include/fixpp/wire/length_data_carry.hpp`) returns no value, which needs all three of:
  1. **Armed:** the field being read comes immediately after a Length field, with nothing between
     them (every field the scanner skips as unparseable, whatever the reason, takes its
     `next_field` path, which disarms the carry through `length_data_carry::reset`), and its tag
     is that Length's paired Data tag in the table in use: the hooks passed to `interpret_logon`,
     that is the session dictionary's, or the standard table alone when none is passed.
  2. **Count:** the Length value's leading ASCII decimal digits, saturating (`parse_bounded_u32`,
     as `OffsetTable::build` reads it), so a Length with no leading digit counts 0.
  3. **Malformed:** `counted_value_end` finds that the counted extent reaches or passes the end of
     the frame (the whole framed message, trailer included), or that the byte after it is not SOH.

  A restatement MUST use one short form: *a Length immediately followed by its paired Data whose
  counted extent reaches or passes the end of the whole framed message, or whose following byte is
  not SOH*; the comments in `version.h`, `session.h`, `admin_messages.hpp` and
  `admin_messages.cpp` are aligned to it by task T075. It MUST NOT
  stop and validate a prefix: nothing after a malformed paired count can be trusted, so the only
  sound verdict is refusal. This closes a `[const §XII.7]` fail-open in which an
  `EncryptMethod(98)` other than `0` placed after the count was never seen.
  - **Orphan Length:** a Length whose next field is not its paired Data is read as a
    plain value; its count is never applied, the next field is read up to its SOH, and the scan
    continues, so a later `98` is still seen and refused unless it is `0`. A well-formed applied
    count that covers a `98=` makes it part of the Data value, which `OffsetTable::build` reads the
    same way; that is equivalent to the peer omitting 98 (evidence file §*Malformed-count scan
    sites*).
  - **C++ refusal code:** the refusal comes before the field checks, so its code is always
    `session_invalid_logon`. The old scan stopped at the count, so a SenderCompID(49) or
    TargetCompID(56) after it was never read and was compared as empty against the expected
    CompID passed in. Where that expected CompID is non-empty, the public C++
    `session::interpret_logon` returned `session_compid_mismatch` and now returns
    `session_invalid_logon` (a failure turned into a different failure; B-426-2 records it). That
    code change has no C-ABI effect: both `Session` Logon arms (NotConnected and LogonSent) discard
    the code and record Disconnected, and `translate` (`src/capi/error.cpp`) maps both codes to the
    same C code. Where it is empty, that compare passed, so the Logon could be accepted; it is now
    refused, which is the accept → refuse case of the C-ABI effect below.
  - No error code, symbol or signature changes (FR-004a).
  - **C-ABI effect** (FR-019, `[const §X.7]`): a Logon of that shape that was accepted before (the
    old scan stopped at the count, and every field it had seen passed) is now refused on either
    role, shipped dictionaries included. The observers are **every C-ABI call whose result depends
    on the session having logged on**; research.md R-11 step 6 derives them, and data-model.md
    Appendix A classifies each:
    - `fixpp_session_is_established`: `true` becomes `false`;
    - `fixpp_session_close`, once the refused session has drained: `FIXPP_ERR_OK` becomes
      `FIXPP_ERR_THREAD_SESSION_LIFECYCLE`;
    - `fixpp_session_send` on that session, issued after the Logon was processed: `FIXPP_ERR_OK`
      becomes `FIXPP_ERR_SESSION_INVALID_STATE` (`Engine::send` refuses with
      `session_invalid_state_for_send` at its Active check on the session strand, before it calls
      `Session::send`, whose own Active precondition is the backstop; returned as translated for
      the consumer's minor);
    - `fixpp_session_register_callback`'s `cb`: an inbound application message on that session,
      delivered before, is never delivered (the `fromApp` dispatch runs only in the
      LogonReceived/Active arm of `Session`);
    - `fixpp_session_register_send_callback`'s toApp callback: invoked before for each send on that
      session, now never invoked, since `Engine::send` refuses at its session-strand Active check
      before it calls `Session::send`, so `send_impl` never builds the toApp view.

    Each of the five has a declaration in `include/fix/c_api/session.h`, so each declaration's
    documentation gains a BREAKING (C-ABI 1.9) FR-020 clause (`[const §X.7]`: "each affected
    declaration"; `fixpp_session_close`'s clause states the drained precondition), and FR-019's
    `version.h` history names every observer too (task T075).
  - Witnesses (tasks T070, written first and RED on the unfixed code): the function directly, over a
    standard pair (count ending on a non-SOH byte; count running past the frame; a malformed count
    followed only by `98=0`) and over a component-only custom pair through the session dictionary's
    hooks; and the acceptor (NotConnected) and initiator (LogonSent) arms, where the Logon reaches
    Active today and must not after the fix. Twins that hold before and after: a well-formed count
    with `98=2` is refused, with `98=0` accepted; `98=2` with no count is refused; the custom-pair
    frame without the dictionary is refused.
  - More witnesses (task T074), added after T071 landed:
    - **orphan-Length pins, GREEN on arrival (not RED witnesses):** `…95=999␁98=2␁…` with no `96`
      is refused, and `…95=999␁98=0␁…` with no `96` is accepted. Each is proven by its own mutant
      (quickstart §3): the first kills a scan that stops at an orphan Length whose count would
      overrun, and the second kills an implementation of an unarmed predicate;
    - **arm cells, on each role:** a count running past the frame (then `98=2`), and a malformed
      count followed by `98=0`. Their RED on the unfixed code is shown by T026's `break` mutant,
      since the fix has already landed;
    - **equality-boundary pair, function level:** a RawDataLength(95) count equal to the bytes
      from RawData(96)'s first value byte to the end of the framed message is refused; with one
      less, the byte after the counted extent is the trailer's final SOH and the Logon is
      accepted. The first pins
      item 3's "reaches" through `interpret_logon`; the second pins "the whole framed message,
      trailer included", since a scan over a narrower buffer would refuse it. A `>=` → `>` mutant
      in `counted_value_end` is killed at compile time by the static assertion in
      `include/fixpp/wire/length_data_carry.hpp` that a count reaching the end of the buffer is
      malformed, so this pair is the runtime witness at the session function.
  - The pre-existing test that pins the old acceptance
    (`LengthDataSessionScanner.InterpretLogonStopsAtAMalformedCount`, renamed
    `InterpretLogonRefusesAMalformedCount` when inverted; the #426 "every later field
    stays absent" case) is inverted to assert the refusal; the ruling above is its authority.
  - No other `length_data_carry` scan site changes (Clarifications, this session). The scope rests
    on the whole population of count-reading sites, not only the `length_data_carry` users:
    re-derive with `grep -rn 'read_value(' src include` together with its complement,
    `grep -rn 'data_tag_for_length\|counted_value_end' src include`, and classify each site as a
    refusal gate or a reader (task T076 records the recipe and the classification).
- **FR-021** (owner ruling 2026-09-25, during `/speckit-implement`; folds in fixpp#506):
  `fixpp_msg_commit`'s Length+Data check, `check_length_data` (`src/capi/message_write.cpp`), MUST
  feed a group node to `wire::length_data_checker::observe` with an **empty** value, as
  `body_builder::commit` does (R-4, C-1.7), at every depth. It MUST NOT feed the group's
  instance-count digits.
  - **Effect, as a condition.** With an empty value, `observe` refuses a group node whose count tag
    is a pair half under the handle's pairs (`pair_hooks`: the session dictionary's `table_view`,
    or the standard table alone when the session has none): a Length tag fails on its empty count,
    and a Data tag fails either as a Data not immediately preceded by its Length or, when its Length
    is immediately before it, on its empty value. So a group whose count tag is a pair half is never
    read as that pair's half, and, once the handle, open-builder and group-grammar checks pass (an
    empty instance or, on a dictionary session, a delimiter or context miss is
    `FIXPP_ERR_TYPE_MISMATCH` first, as before), `fixpp_msg_commit` returns
    `FIXPP_ERR_WIRE_CONFORMANCE` for it whatever its instance count and neighbours. Before FR-021 the same group was refused unless its
    count digits completed the pair; those are the commits that change, from `FIXPP_ERR_OK` to
    `FIXPP_ERR_WIRE_CONFORMANCE`. These shapes witness it (they are witnesses, not the population):
    - **Length shape** (measured, evidence file §*/simplify (T053)*, M1): a group tagged 354 with
      one instance, then a one-byte 355: `35=D␁354=1␁79=A1␁355=x␁` was emitted;
    - **Data shape** (from `observe`'s source): `354=1`, then a group tagged 355 whose
      instance-count digits number one: `observe(355, "1")` after `354=1` satisfied the awaited
      Data's byte count.
    A group node whose count tag is not a pair half is unaffected: it passes `observe` with either
    value, and a Length right before it is still a Length not followed by its Data.
  - **Reachability, as a condition** (`src/capi/message_write.cpp`). `fixpp_msg_group_begin` and
    `fixpp_entry_group_begin` refuse a count tag only when it is a framing tag, or when the handle
    carries a dictionary (`h->dict_`) and that dictionary's `group_first_field(tag)` is 0. So the
    effect is reachable:
    - on **every session with no dictionary**, where any non-framing tag, pair halves included,
      opens a group, at the top level and nested (`fixpp_entry_group_begin`, with the sibling half
      written by an entry setter, since `check_length_data` checks each group instance as its own
      container with the same group-node rule);
    - on a **dictionary session** only where that dictionary's `group_first_field` is nonzero for a
      tag its own pairs treat as a pair half (the handle's `dict_` and its `table_view` come from
      the same session dictionary, null together). *Dated 2026-09-25:* no shipped QuickFIX
      dictionary declares such a group. Re-derive by intersecting, for each `dictionaries/*.xml`,
      the fields named by `<group name=…>` with the **union** of the standard-pair tags of
      `include/fixpp/core/length_data_pairs.hpp` and the fields typed LENGTH, DATA or XMLDATA;
      positive control, per file: add that file's first `<group name=…>` field to the probe set and
      see it reported. The QuickFIX XML loader does not check a `<group>` count field's type
      (`expand_field_list`'s `<group>` arm in `src/dictionary/xml_loader.cpp` copies the field's
      declared type without requiring NUMINGROUP), so a user dictionary can declare such a group;
      T078's dictionary cell (`EmptyGroupTaggedAsADictionaryDataDoesNotCompleteThePair`) measures
      it. The condition, not a population, is what this requirement states. Where it
      holds, a group with **zero instances** is the shape most easily reached there: at commit,
      `validate_group_grammar` runs its `group_first_field_exact` context lookup only for a group
      with at least one instance (`dict && !e.instances.empty()`), so a zero-instance group meets
      no delimiter or context check before `check_length_data`, and its count digits (`0`) could
      complete a one-byte Data before FR-021.
  - **C-ABI effect** (FR-019, `[const §X.7]`: a success turned into a failure is BREAKING). The
    population follows research.md R-11's recipe: step 2 maps `check_length_data` to its only
    exported caller, `fixpp_msg_commit`, whose documented result changes, so its declaration in
    `include/fix/c_api/message.h` gains its own BREAKING (C-ABI 1.9) FR-021 clause beside the
    existing FR-019 one, and its `FIXPP_ERR_WIRE_CONFORMANCE` return-code line gains the group case;
    the `version.h` 1.9 history gains an FR-021 sentence. Every other export is unchanged for
    FR-021, classified in data-model.md Appendix A:
    - `fixpp_msg_group_begin`, `fixpp_entry_group_begin` and the setters that write the sibling
      half return what they returned before; the effect of what they wrote surfaces at
      `fixpp_msg_commit` (R-11 step 5);
    - `fixpp_session_send` takes the caller's bytes and never runs `check_length_data`, and FR-021
      changes nothing on its path, so for any given payload its result is unchanged. Separately,
      why such a payload is accepted today: its `send_impl` scan (`length_data_carry`) disarms a
      pending count when the next field is not the Length's Data and reads an unarmed Data tag as a
      plain field. So a payload of either shape that a pre-FR-021 commit produced, or that a caller
      assembles by hand, is sent as before (disclosed in B-091-4, not changed here).
  - No error code, symbol or signature changes (FR-004a).
  - `.specify/426-428-length-data-pairs.md` §5.3 prescribes feeding "each container's entries" and
    says both writers "refuse exactly the same pairs"; it does not say how a group node is fed.
    FR-021 is what makes that sentence true, so it supersedes no recorded decision of that note;
    it supersedes research.md R-4's "not 091 scope" for the C-ABI group path.
  - Witnesses (task T078, written first and RED on the unfixed code, in
    `tests/capi/length_data_setters_test.cpp`, `capi_length_data`, on the no-dictionary fixture
    except where named): the Length shape and the Data shape at the top level, the Data shape with a
    zero-instance group (`EmptyGroupTaggedAsTheDataDoesNotCompleteThePair`), the same zero-instance
    Data shape on a dictionary session whose custom pair's Data tag is declared a group
    (`EmptyGroupTaggedAsADictionaryDataDoesNotCompleteThePair`, its own fixture XML), and the Length
    shape nested in a group instance, each asserting `FIXPP_ERR_WIRE_CONFORMANCE` (today `FIXPP_ERR_OK`). Twins that hold
    before and after: the same group with a non-pair count tag and the same bare sibling Data is
    refused (an orphan Data); that group with no sibling commits; a well-formed pair next to a
    group commits; and a lone pair-half group with no neighbour is refused, for the Length
    (`TwinLonePairLengthGroupIsRefused`) and the Data (`TwinLonePairDataGroupIsRefused`), which is
    what separates feeding the group's own tag from feeding a non-pair tag; and a Length before a
    zero-instance non-pair group, then its Data, is refused
    (`TwinLengthBeforeAnEmptyNonPairGroupIsRefused`), which is what shows a zero-instance group node
    is still fed.

### Key Entities

- **Length+Data pair**: a (Length tag, Data tag) couple. The Length field immediately precedes the
  Data field and holds the Data value's octet count. The authoritative set is the standard table; the
  commit check also knows a dictionary's own pairs when the builder holds its hooks (FR-009).
- **Coupled Args member**: one `std::optional<std::string_view>` in a generated `<Msg>Args` struct
  standing for a whole pair; the Length is never a separate member.
- **Stage-one pins**: the four tests that recorded the limitation as it behaved, before the fix.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Every octet value `0x00–0xFF` in a Data field survives a build → parse round trip
  unchanged, through a generated builder at the top level and inside a group entry (256/256 values;
  witnesses C-2.6).
- **SC-002**: 0 new ways to place SOH or a non-printable byte in a non-Data field: every existing
  injection-guard test passes unedited, and the new operation, through both `field_data` and
  `set_data`, refuses every non-Data tag tried (C-1.4).
- **SC-003**: The coupled call sites in regenerated builder output equal, exactly, the expected set
  derived from the dictionary sources, independently of the generator (C-2.2), against the standard
  table — every (message, structural path, Length tag, Data
  tag, arm) — and 0 non-Data fields use the atomic operation (census C-2.2, proven able to fire on the
  output of the new emitter over the unfixed loader, which must be reported missing exactly the five
  R-11 pairs).
- **SC-004**: Existing application code that sets a coupled Args member compiles and behaves
  identically for ASCII values (no source change needed). **Exception (owner-ratified):** code that
  sets the deleted v50sp2 Length members of the five newly coupled pairs (FR-011 carve-out).
- **SC-005**: Building a representative message with no Data field (a `NewOrderSingle` with and
  without a repeating group) is at most **3 %** slower than the baseline taken on unmodified `main`
  before any code edit, measured on the same builder benchmark. If the design cannot meet 3 %, the
  measured figure goes back to the owner for review. The target is not silently relaxed. The method
  is quickstart §6 / R-10: base and candidate built in one session with one toolchain, the base
  from the current merge-base with no production-code change (only the final bench source), A-B-A-B;
  if the noise-floor precondition fails, the verdict is inconclusive and goes to the owner.
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
- **Stage-one pins still pass on current `main`** after the commits since their base (measured
  2026-09-24: 31; dated); they are re-run and re-proven RED against a widened guard before being
  flipped.
- **Read-tier movement is confined to FIX 5.0 SP2** `Fields.hpp` and `Validator.hpp`. If any other
  pinned read-tier artifact moves — in particular `v50sp2/Messages.hpp`, whose pin also corroborates a
  checked-in golden — work stops and it goes back to the owner.

## Normative References

- fixpp#418 — the issue and its 2026-09-12 Gate A round-1 comment (the rejected shape).
- `.specify/426-428-length-data-pairs.md` — O-4 (reuse the pair table and checker), §3 (Data→Length
  lookup), §5.3 (`length_data_checker`), L-426-2 (no dictionary override of a standard pair).
- `.specify/418-data-field-bytes.md` on branch `fix/418-data-field-bytes` — superseded input only.
- `brain/components/wire.md` §*Length+Data pairs* — why the pair set is shared.
- `[FIX50SP2 §3.3] Field data types` — coverage-index entry for W-008 (Length+Data).
- Informative (no coverage-index entry): FIX TagValue Encoding v1.0 §4.2.4, §4.2.5, §4.3.7.3.
- `spec/behaviors-and-limitations.md` — L-067-2 (live until this feature closes it).
- `.specify/constitution.md` — `[const §XVII.1]` (Gate A for a public C++ API change),
  `[const §XVI.3]` (clarify mandatory for wire/codegen), `[const §X.7]` (a call that used to
  succeed and now fails is BREAKING — FR-019), `[const §X.6]` (the controls for a breaking C-ABI
  change), `[const §XII.7]` (application-layer encryption is refused: `EncryptMethod(98)` other than
  `0` — FR-020).

## Explicitly out of scope

- **New C-ABI surface.** The C-ABI already has `fixpp_msg_set_data` / `fixpp_entry_set_data`, so
  091 adds no C-ABI symbol, signature or error code. Its only C-ABI effect is FR-019's behaviour
  change (C-ABI 1.9 BREAKING), which follows from the loader fix (FR-017), the `interpret_logon`
  fix (FR-020) and the C-ABI commit's group-node fix (FR-021) and is in scope.
- **Set-time C++ support for dictionary-declared pairs** — follow-up *verifiable session binding for
  dictionary pairs, option (b)* (fixpp#505), for C-ABI parity on custom pairs.
- **Codegen over a custom dictionary** (FR-009): unsupported.
- **The Orchestra loader.** FIX Latest already carries every pair through `lengthId` (#427).
- **The Python binding** exposing a Data setter.
- **Changing `kBodyCap` or `kArenaCap`.**
- **Whether STRING fields should admit high-bit bytes** — #418 leaves it open; this feature does not
  touch the STRING guard.
- **Moving feature 089's B-05 off its test hook** (see Assumptions).
