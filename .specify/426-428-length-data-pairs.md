# #426 / #428 — one Length+Data pair table for every scanner, and a coupled C-ABI Data setter

> **Status: v4, 2026-09-15.** Revised after Codex scoped Gate A:
> - **Round 1:** 1 P1 / 5 P2 / 1 P3. Triage `opus_426_428_1_…_triage.md`.
> - **Round 2:** 3 P1 / 2 P2. Triage `opus_426_428_2_…_triage.md`.
> - **Round 3:** 2 P1; every round-2 finding MET, and the 14-scanner census independently confirmed.
>   Triage `opus_426_428_3_…_triage.md`.
> - **Round 4:** 1 P2 (R4-1, the §6 RED/regression-pin split). Triage `opus_426_428_4_…_triage.md`.
> - **Round 5: CONVERGED** (`codex_426_428_5_length-data-pairs_review.md`): no P1, P2 or P3, and
>   the 14-scanner census independently confirmed a third time.
> - All triages confirmed every finding; all files are in `research/reviews/`.
>
> **Scoped Gate A converged at round 5 (2026-09-15).** `[const §X.1]` makes a Codex Gate A mandatory for every C-ABI
> change.
>
> Batch branch `fix/426-427-428-length-data-pairs`, which also carries #427 (Orchestra `lengthId`).
> #427 is implemented and not re-decided here. Claims about code name functions or files, never
> line numbers.
>
> **Owner decisions (2026-09-15), not open for review:**
> - **O-1** #426 covers every field scanner with the standard pair table, plus the dictionary's pairs
>   wherever a `table_view` is reachable. The remaining residual is documented.
> - **O-2** C-ABI **MINOR 1.6.0**:
>   - add `fixpp_msg_set_data` / `fixpp_entry_set_data`;
>   - SOH is refused only where it cannot be well-formed;
>   - `fixpp_msg_commit` refuses malformed Length+Data pairs;
>   - no other existing-symbol behaviour changes.
>
>   This is a **breaking change** to three existing symbols. It ships as MINOR 1.6.0 under proposed
>   constitution Article X §7: before the first public release a breaking C-ABI change bumps MINOR
>   (§5.5). r2 R2-1 moved the SOH rule from "string setters reject SOH" to the condition stated in
>   §5.1, so no well-formed call sequence is refused.
> - **O-3** The loose dictionary callbacks are **bundled now** into one value type.
> - **O-4** #418 (arbitrary bytes through C++ `body_builder`) stays out, but it depends on #427 (landed
>   first) and must use the same pair table. Two things here exist so it can: the Data→Length lookup
>   (§3) and the pair checker `wire::length_data_checker` (§5.3), both in the wire layer. D-4's
>   C-ABI commit is the first caller of the checker; `body_builder` is meant to be the second, so the
>   rule is written once.
> - **Rejected:**
>   - a MAJOR 2.0.0;
>   - setter-level refusal of pair halves, which breaks callers building valid pairs by hand;
>   - a purely additive change, which leaves the injection open;
>   - rejecting all C0 control bytes, which refuses well-formed output such as a newline in Text(58).

---

## 0. What the standard says, and what the reference engines do

**Standard.** FIX TagValue Encoding v1.0 (June 2020):
- §4.2.4: *"There must be no embedded <SOH> characters within field values except for those of
  datatype data."*
- §4.2.5 lists what makes a field malformed, including:
  - *"the value is empty"*;
  - *"the value contains an <SOH> character and the datatype of the field is not data or XMLdata"*;
  - *"the datatype of the field is data and the field is not immediately preceded by its associated
    Length field"*.
- §4.3.7 and §4.3.7.3: parsers *"must use the Length field"*, which gives the octet count.
- §4.1: non-data fields use a single-byte character set, ISO 8859-1 by default.

FIX 4.4:
- Vol 1, `data` datatype: *"Data fields are always immediately preceded by a length field."*
- Vol 2, EncodedTextLen: *"must immediately precede it."*

**QuickFIX C++ v1.16.0 and QuickFIX/J 3.0.1**, the pinned interop counterparties:
- **Send:** no coupling, no derived Length, and no SOH check. `StringConvertor` is
  `EmptyConvertor`.
- **Parse:** pairs are honoured only with a dictionary, and only for type `DATA`, so **XmlData is
  split at SOH**. The Length tag is assumed to be `tag - 1`, except 89→93. Adjacency is not checked
  (`Message::extractField`).
- **Consequence for fixpp's pairs:** QuickFIX mis-parses non-adjacent or inverted pairs. QuickFIX
  C++ sorts body fields by tag (`message_order::normal`), so it sends inverted pairs Data-first.

**Fix8 1.4.3** (local source, read 2026-09-15):
- **Parsing** (`MessageBase::decode`):
  - A `LENGTH`-typed field reads the next field by fixed width. That only works when the Data tag is
    exactly `Length + 1`, so Signature 93/89 and every other non-adjacent or inverted pair is **not**
    counted.
  - Only `DATA` is paired; `XMLDATA` is not.
  - The byte after a counted value is not checked for SOH, so a lying Length silently throws the
    scan out of step.
  - `MessageBase::decode_group` has **no** Length+Data handling, so encoded pairs inside repeating
    groups are split at SOH.
  - `Session::process` finds MsgSeqNum with a raw `find("34=")` before decoding, so a Data value
    holding `34=` forges it. That is the class of fixpp's rows 6–8.
- **Sending:** Length and Data are set separately, with no derived Length and no SOH check. Schema
  order emits Length first, including 93 before 89.

**Other engines:** §0.1 will be added when the public-documentation survey returns.

fixpp is deliberately stricter than QuickFIX on send and follows the standard on parse. §7 records
where that differs from QuickFIX.

## 1. The defect: fourteen field scanners

A Data value may contain SOH. A scanner that does not know the pair splits the value there and reads
the rest as fields.

**How the census was built.** Every non-comment SOH reference in `src/`, `include/` and
`tools/codegen/fixpp-codegen/` was mapped to its enclosing function, and the alternate spellings
(`\001`, `'\1'`, `std::byte{1}`, ``) were checked for additional hits. It was not derived by a
detector.
- **Not scanners:** SOH writers (`append_field`, `Writer`, `body_builder::commit`,
  `fixpp_msg_commit` serialisation) and `Framer`, which is BodyLength-driven.

| # | Scanner | Pair knowledge today | Consumer | Pair source after the fix |
|---|---|---|---|---|
| 1 | `MessageView<Iter>::field_iterator::advance` (`parser.hpp`) | 6-pair static table | Iter API; rows 3–5 | hooks it is given (default: standard) |
| 2 | `OffsetTable::build` (`offset_table.cpp`) | own copy of the 6 | Index `get`, groups, `unknown_fields`, C-ABI enumeration | table's hooks |
| 3 | `Validator::validate` field walk (`validator.hpp`) | via Iter | inbound validation | validator's `table_view` |
| 4 | `wire::get(span, tag, gen)` (`parser.hpp`) | via Iter | generated group-entry readers | `entry_context` hooks |
| 5 | `scan_slice_for_tag` (`src/capi/message_read.cpp`) | via Iter | C-ABI group-entry reads | minting view's hooks |
| 6 | `scan_frame_header` (`src/session/scan_frame_header.hpp`) | **none** | inbound dispatch; `store_then_emit` MsgType | session hooks |
| 7 | `scan_first_frame_ids` (`src/session/scan_first_frame_ids.hpp`) | **none** | acceptor CompID routing, pre-session | **standard only** |
| 8 | `interpret_logon` (`src/session/admin_messages.cpp`) | **none** | Logon 98, 108, 553, 554, 1137 | session hooks |
| 9 | `build_replay_frame` (`src/session/session.cpp`) | **none** | resend of a stored outbound frame | session hooks |
| 10 | `redact_tag554` (`include/fixpp/session/logon_credentials.hpp`) | **none** | persistence redaction (logger/tap, transcript, golden writer) | **standard only** |
| 11 | `frame_has_genuine_tag554` (same) | **none** | `Session::store_then_emit` | session hooks |
| 12 | `mask_tag554_same_length_inplace` (same) | **none** | `Session::store_then_emit`, before persistence | session hooks |
| 13 | `Session::send_impl` opaque-payload walk, including `has_boundary_token` (`session.cpp`) | **none** | C++ `Session::send` and `fixpp_session_send`: framing-tag refusal, duplicate-35 scan, per-field validation, header partition (`is_send_header_tag`), 43/122 excision | session hooks |
| 14 | `fixpp_msg_clone`'s `mk_fv` (`src/capi/message_write.cpp`) | **none** | inbound message clone `frame_view` bounds | none needed (§4) |

- **"Session hooks"** means `dict_hooks::for_table_view(*inbound_tv_)` when the session has a
  dictionary, else `none()` (standard table). The free functions gain a `dict_hooks` parameter
  defaulting to `none()`.
- **The 6-pair tables are mislabelled:** 348/349 and 350/351 are EncodedIssuer and
  EncodedSecurityDesc, not "EncodedHeader/EncodedMsg".

**What goes wrong in each scanner today:**
- **Rows 6–8 (forge fields).** They keep the last value per tag. A counted value holding
  `␁554=forged` forges a Password, and a header SecureData(91) or XmlData(213) can override 34, 43
  or 141.
- **Row 9 (breaks replayed values).** Replaying a stored frame whose Data value holds SOH writes the
  value's pieces as separate fields.
- **Rows 10–12 (corrupt stored bytes).** They never miss a genuine Password, because a real 554 is
  SOH-anchored. They do mask a `␁554=` inside a Data value, corrupting stored bytes that a resend
  replays.
- **Row 13 (outbound refusal or corruption).** It refuses a legitimate Data value holding `␁34=`,
  and silently cuts a `␁43=` or `␁122=` out of the value.
- **Row 14 (truncated clone).** A Data value holding `␁10=` ends the cloned body inside that value.

**Nothing guards these by default:**
- Inbound validation is opt-in and defaults to OFF (`SessionConfig::validate_inbound_messages`,
  B-041-1).
- When validation is on, `Session::validate_inbound_` parses without a dictionary.
- `scan_first_frame_ids` runs in `engine.cpp` before any Session exists.

**Ordering matrix.** Each cell gets a stage-one witness proving that a forged tag inside a counted
value changes nothing there:

| State / path | Scanners | Forged tags that matter | Validation |
|---|---|---|---|
| Acceptor pre-session routing (`engine.cpp`) | 7 | 8, 49, 56 | cannot run |
| NotConnected / LogonReceived establishment | 6, 8 | 34, 43, 49, 52, 56, 98, 108, 141, 553, 554, 789, 1137 | on / off |
| LogonSent / Active / LogoutSent dispatch | 6 | 7, 16, 34, 35, 36, 43, 112, 122, 123, 141, 383, 464, 789 | on / off |
| Reject reference (RefSeqNum) | 6 | 34 | on / off |
| Outbound send | 13 | 8, 9, 10, 34, 35, 43, 49, 52, 56, 122, any header-class tag | n/a |
| Outbound persistence / replay | 9, 11, 12, 10 | 43, 52, 122, 554 | n/a |
| C-ABI clone | 14 | 10 | n/a |

## 2. D-1 — the standard pair table: one checked-in header

**The header.** `include/fixpp/core/length_data_pairs.hpp` (new) holds a constexpr array sorted by
Length tag and `detail::standard_data_tag_for_length(std::uint16_t) noexcept`, a binary search.
- Both hand copies of the 6-pair table are deleted.
- So is the comment claiming `offset_table.cpp` cannot include a shared table; that is wrong for a
  leaf header.

**How the header is derived and kept honest.** A wire test checks the header directly:
1. Load all ten shipped dictionaries with the real loaders.
2. Take the union of `length_pair_data_tag` over every Length-typed tag of every message expansion.
3. Assert that no Length tag maps to two different Data tags.
4. Assert that the union equals the header. On mismatch, print the replacement array body.

Facts behind the test:
- **Measured:** 84 pairs, no conflicts, and no ambiguous Data→Length inverse.
- **Proof it can fail:** deleting one header row turns the test RED.
- **Pins:** `static_assert` for 93→89, 95→96, 354→355, 2372→2371, and 7→0.
- **Why a union across versions is sound:** FIX never reuses a tag number for a different field.

## 3. D-2 — dictionary pairs through one bundled hook value (O-3)

**`wire::dict_hooks`** (new, `include/fixpp/wire/dict_hooks.hpp`) is trivially copyable, checked by
`static_assert`. It holds:
- `void const* opaque_dict`
- `classify_fn_t classify`
- `group_member_fn_t group_member`
- `group_delim_fn_t group_delim`
- `length_pair_fn_t length_pair`, where
  `length_pair_fn_t = std::uint16_t (*)(void const*, std::uint16_t, pair_side) noexcept`
  (`pair_side::length` or `pair_side::data` selects the lookup direction)

**Construction.** The constructor is private, so production code cannot build a partly filled
bundle. There are three factories:
- `none()`: all fields null.
- `for_table_view(fixpp::dict::table_view const&)`: every callback set, using captureless lambdas
  moved from `Parser(TV&)` — except `length_pair`, which is installed only when
  `table_view::has_nonstandard_pair()` is true, i.e. the dictionary declares a pair whose two tags
  the standard table both leave unnamed. Those are the only pairs the lookup rule below can
  honour, so for every other dictionary the callback could only ever answer 0 — at one lookup per
  field, on a bundle that `Validator::validate`, `Session`'s scanners and the C-ABI setters build
  per message. Every shipped dictionary is of that kind. `table_view` computes the flag in
  `set_length_pair_data_tag`, which is why the standard table is a core header (§2): the
  dictionary layer may not include wire ([arch §2.3]).

  ⚠️ **A bundle is a snapshot.** `for_table_view` reads the flag once, so a bundle built before a
  pair is registered keeps a null callback. No shipped path can observe that: a `table_view` is
  built once at config time (`Dictionary::as_table_view`, immutable afterwards by its own contract),
  and a bundle is built only after it is populated: `Validator::validate`, `Session`'s scanners and the
  C-ABI setters build one per operation, while `Parser` builds one in its constructor and reuses it for
  its own lifetime against a view that must stay stable for at least that long (Gate B r7 N-4 —
  "rebuilt per message" was too strong; `Parser` retains its bundle). ⚠️ **SUPERSEDED — see
  `.specify/456-table-view-seal.md`.** At the time of writing the type did not *enforce* the order
  (`set_length_pair_data_tag` and assignment were public) and the rule was pinned by
  `DictHooksCustomPair.ABundleIsASnapshotOfTheDictionaryItWasBuiltFrom`. fixpp#456 sealed the
  published view's population surface, which makes the mutate-after-publish case this test built
  unconstructible; its population half is now covered by the compile-time seal witness
  (`tests/dictionary/table_view_seal_compile_test.cpp`), and its identity half — the same latch
  observed through a destroy-and-reconstruct at the same address, e.g.
  `std::optional<table_view>::emplace` — by
  `DictHooksCustomPair.ABundleKeepsItsNullPairCallbackAcrossAReSeatThatAddsThePair`. Pair mutation
  still carries the strong exception guarantee, so the
  maps and the flag cannot disagree after a failed allocation (r6 M-2) — copy-assignment, which
  carried the other half of that claim, no longer exists.
- A test-only factory under the existing test-hooks seam, for stub dictionaries such as the uint16
  token in `fuzz_wire_nested_slice.cpp` and the offset-table tests that pin a null member function.

**One lookup rule, used by every scanner in §1 and by C-ABI commit (r3 R3-1).**
`hooks.data_tag_for_length(tag)` works in this order:
1. If `tag` is a Length tag in the **standard pair table**, it returns the standard Data tag.
2. Otherwise, if the dictionary pairs `tag` with a Data tag that is **not** in the standard table
   (on either side), it returns that Data tag.
3. Otherwise it returns 0.

The inverse, `data → length`, follows the same order. With `none()`, step 2 never applies. It is
spelled `detail::standard_length_tag_for_data` in `length_data_pairs.hpp` and
`dict_hooks::length_tag_for_data`; the dictionary side is a second map filled beside the first. A
compile-time check pins that the two directions agree on every standard pair (the table has no
ambiguous inverse, §2).

- **Why the standard comes first:** FIX fixes standard tag numbers and their datatypes. A dictionary
  that repurposes a standard pair tag contradicts the standard, and is not honoured for pairing
  (§7).
- **What this buys:** commit and fixpp's own parsers split every message on the same boundaries, and
  a session whose dictionary does not declare a standard pair at all (e.g. a FIX44 session receiving
  SecurityXML(1185)) still counts it.

**Dictionary side.** `table_view::length_pair_data_tag(std::uint16_t) const noexcept` is new. It is
backed by an `unordered_map` that `Dictionary::as_table_view()` fills from each message's
`message_fields`, which include header and trailer. The defaulted copy constructor carries it.

**Every site where the bundle replaces the loose pointers:**
- The two dict-aware `OffsetTable` constructors take one no-default `dict_hooks`, and the table
  stores it as its `hooks_` member.
- The `MessageView` dict constructors and `Parser` do the same (both `parse` overloads).
- `build_nested_subview` and **both** `nested_group_slices` overloads take `dict_hooks` from their
  caller. This closes the mismatched-pairing sibling recorded in `brain/components/wire.md`.
- `entry_context` carries `dict_hooks hooks` in place of its `opaque_dict` and `group_member_fn`,
  and stays trivially copyable.
- Generated readers (`emit_messages.cpp`) call `wire::get(ctx_.span, tag, ctx_.hooks, ctx_.gen)` and
  `nested_group_slices(…, ctx_.hooks, …)`. Every golden carrying `emit_messages` output regenerates;
  the regeneration diff enumerates the set.
- `Validator::validate` walks with `dict_hooks::for_table_view(dict_)`.
- `Session::validate_inbound_` parses with `Parser{validator's table_view}` instead of a
  default-constructed one.
- `scan_slice_for_tag` takes the minting view's hooks.
- The `Session` call sites of rows 6, 8, 9, 11, 12 and 13 pass the session hooks.
- `fixpp_msg_clone` and `reify.cpp` build their parser over their **owned** `table_view` copy, so
  the hooks point at the copy.

**Iter and Index views.** `MessageView<Iter>::field_iterator` gains a `dict_hooks`, defaulting to
`none()`. `MessageView<Index>` exposes the hooks it was built with.

**Lifetime.** A `dict_hooks` aliases a `table_view`. Every holder is already bounded by that view's
owner:
- the session's `inbound_tv_`;
- the validator's `dict_`;
- a clone's or reify's owned copy.

**Witnesses per path:**
- **Paths:** clone, `membership_copy`, reify, nested subview (both overloads), generated reader,
  C-ABI slice, validator walk, validator parse, and each session call site.
- **Setup:** a test dictionary declaring the custom pair 5001/5002, with a value that carries
  `␁<tag>=`.
- **Assertion:** exactly one field boundary.

## 4. D-3 — one Length→Data carry; the policy on a bad count varies by scanner

The shared helpers live in `include/fixpp/wire/length_data_carry.hpp`. They are kept out of `tag_scan.hpp`, a std-only
leaf, because they need `dict_hooks`.
- **`counted_value_end(buf, vstart, count)`** returns the index of the SOH that must follow a
  counted value. It returns nullopt when the count runs past the buffer, reaches exactly its end, or
  lands on a non-SOH byte. It uses a subtraction bound that cannot wrap (W-P2-1a preserved).
- **`length_data_carry`.** `read_value(buf, vstart, tag, hooks)` reads one field's value and arms
  for the next field.
  - The value is read by the previous Length's count when `tag` is that Length's Data partner, and
    to the next SOH otherwise. The carry always disarms (W-P2-1b preserved).
  - The count is parsed with `parse_bounded_u32`, which saturates (W-P2-1c preserved).
  - `reset()` disarms for a field the scanner skips.

  The /simplify pass merged the drafted `take` / `arm` / `counted_end` into this one path.

| Scanners | Response to a malformed counted value | Why |
|---|---|---|
| 2 `OffsetTable::build` | unchanged: `wire_invalid_field_format`, table cleared | has an error channel |
| 1 `field_iterator` (3–5 via it) | unchanged: an overrun clamps to the span end, an in-span non-SOH boundary stops, an exact span end is accepted (group slices exclude the terminal SOH) | no error channel |
| 6, 7, 8 | **stop scanning**; nothing after the value is read | fail toward *absent*, never *forged* |
| 9 `build_replay_frame` | `wire_invalid_field_format`, so the slot is gap-filled | same disposition as its `bad_tag` arm |
| 10, 11, 12 | skip a correctly counted value; on a malformed count, fall back to today's rule (every SOH-anchored `554=` matches) | fail toward over-masking, never disclosure |
| 13 `send_impl` | `app_payload_malformed`: no seqnum consumed, nothing sent | the outbound payload is the caller's, so fail closed |
| 14 `mk_fv` | none needed: locate CheckSum by searching **backwards** for `␁10=` | CheckSum is structurally the last field of a Framer-validated frame |

**Row 13 detail.** `send_impl` replaces `has_boundary_token` and its separate duplicate-35 scan with
the per-field walk it already performs. It checks each field's tag, skips counted values as units,
and applies the framing-tag refusal, the duplicate-35 refusal, the empty-value rule, the header
partition and the 43/122 excision **only to real fields**. A counted Data value always moves with
its Length: they are copied as one unit into whichever partition the Length's tag belongs to.
- If the two tags classify differently (Length in the header set, Data not, or vice versa), the pair
  goes with its Length.
- The standard pairs 90/91 and 212/213 are both in the header set, so they stay together.

**Row 9 detail.** `build_replay_frame` re-emits a counted value with `Writer::append_raw`. The
Length is copied just before it, so the pair stays adjacent.

## 5. D-4 — #428, the C-ABI (O-2)

### 5.1 The pair source, and SOH outside a Data value

**Pair source.** A handle's pair source is the §3 lookup rule, taken over the session's `table_view`
when the handle has one and over `none()` otherwise. "Well-formed" in §5 is judged against that
source:
- a standard pair per the FIX standard;
- a non-standard pair per the dictionary in force;
- for a handle with no dictionary, a custom tag is not a data field.

**The SOH rule:**
- **Setter check.** `fixpp_msg_set_string` and `fixpp_entry_set_string` refuse a value containing
  0x01 **only when `tag` is not the Data half of a pair** in the source. The error is
  `FIXPP_ERR_WIRE_CONFORMANCE` and nothing is written.
  - `WIRE_CONFORMANCE` is the existing minor-2 code that `translate()` gives
    `wire_field_value_out_of_range`, so no consumer sees `UNKNOWN`.
  - The entry check sits before delegation, not in `entry_set_bytes_impl`.
- **Commit check.** `fixpp_msg_commit` refuses (§5.3) any scalar entry, at any depth and written by
  any setter, whose value contains 0x01 and whose tag is not a Data half.
- **Still accepted:** `set_int(354, 3)` + `set_string(355, "A\x01B", 3)` and its entry twin. That is
  the r2 R2-1 witness.
- **Unchanged:** other control bytes and 0x80–0xFF are still accepted (§7).

### 5.2 `fixpp_msg_set_data` / `fixpp_entry_set_data`

`fixpp_error_t fixpp_msg_set_data(fixpp_msg_t*, uint16_t data_tag, const uint8_t* bytes, size_t len)`,
plus the entry twin. The Length tag is the unique Length whose partner is `data_tag` in the source.

**Refusals, all checked before anything is touched:**
- A framing tag → `MSG_FRAMING_TAG_FORBIDDEN`.
- `data_tag` is not the Data half of a pair → `TYPE_MISMATCH`.
- `len == 0` → `WIRE_CONFORMANCE`, because an empty value is malformed (§4.2.5; r2 R2-4).
- A group collision → `TYPE_MISMATCH`.
- For the message-level setter only, when a dictionary is present: the tag is absent from the
  MsgType grammar → `DICT_CONFIG`.
- The entry twin runs no `check_dict`, like every existing entry setter (r1 G428-C).

**No mid-vector erase or insert (r2 R2-3).** `fixpp_group_builder::group_field_index` and
`fixpp_entry::instance_index` are vector indices, so anything that shifts entries re-points a live
builder. In the target container (the top-level entries, or the current `GroupInstance::fields`):
- **Neither half present:** append the Length (`len` in decimal), then the Data (bytes verbatim).
  An append never shifts an existing index.
- **Both present, Length immediately followed by Data:** overwrite both values in place.
- **Any other state** (one half only, the halves not adjacent, or Data first): refuse with
  `TYPE_MISMATCH` and write nothing. The caller removes the stray half first.

Serialisation follows insertion order, so the pair goes out adjacent with the Length first. If the
arena runs out, the existing steady-state abort thunk applies.

**Groups.** A census over all ten shipped dictionaries found:
- No group has a Data field as its delimiter.
- Three groups have a **Length** as delimiter: the PaymentStreamFormula groups in FIX50SP2 and FIX
  Latest (43109/42684, 43110/42486, 43111/42982).
  - Starting such an instance with `set_data` appends the Length first, which satisfies
    `validate_group_grammar`.
- If a custom dictionary's group delimiter is a Data field, `set_data` refuses with `TYPE_MISMATCH`.

### 5.3 Commit-time conformance

`fixpp_msg_commit` gains one recursive pass. It runs after `validate_group_grammar` and before
sizing, uses the §5.1 source, and covers the top-level entries and every `GroupInstance`. It returns
`FIXPP_ERR_WIRE_CONFORMANCE`, with nothing serialised, when a container holds:
1. a scalar whose value contains 0x01 and whose tag is not a Data half;
2. a Data-half entry not immediately preceded by its Length entry;
3. a Length that is not one or more ASCII digits (so a sign, a space, or any other byte is refused),
   or whose numeric value is zero, overflows, or differs from its Data value's byte count; or an
   empty Data value. **Leading zeros are accepted**: TagValue v1.0 Table 1 says `int` *"may contain
   leading zeros"*, and `Length` is *"Sequence of character digits … Value must be positive"*. Only
   `TagNum` bans them (r3 R3-2). Witnesses: `354="003"` with a 3-byte `355` is accepted;
   `"+3"`, `"-3"`, `"0"` and `"3 "` are refused;
4. a Length-half entry not immediately followed by its Data entry.

**Where the rule lives.** Rules 2–4 are one wire-layer type, `wire::length_data_checker`
(`include/fixpp/wire/length_data_check.hpp`), not code inside `src/capi/`. It is fed one field at a
time — `observe(tag, value)` then `finish()` at the end of a container — and answers with the first
violation. The C-ABI commit pass feeds it each container's entries, resetting per container. #418's
`body_builder` emit path can feed it each field it appends, so both writers refuse exactly the same
pairs. Rule 1 (SOH outside a Data half) is a one-line test over the same `dict_hooks`, kept at the
caller because only the C-ABI accepts SOH through a string setter.

Each of these is malformed under TagValue §4.2.4/§4.2.5 or FIX 4.4 Vol 1 `data`. So no well-formed
message is refused. Every existing caller that builds a correct pair by hand keeps working, and
`set_bytes` and `remove_tag` are unchanged. A caller who wrote the Data before the Length has been
sending Data-first bytes, which are malformed and are now refused.

### 5.4 Not changed, and why

- **`fixpp_msg_set_bytes`** keeps its type-agnostic contract. Malformed output it can produce is
  refused at commit.
- **`fixpp_msg_remove_tag`** is unchanged. ⚠️ It erases from a vector that open builders index; that
  is a suspected pre-existing defect, to be verified and filed separately. This design does not rely
  on it.

### 5.5 Versioning and surface bookkeeping

**These refusals are a breaking change.** `fixpp_msg_set_string`, `fixpp_entry_set_string` and
`fixpp_msg_commit` refuse call sequences that used to succeed. An earlier revision of this section
proposed an Article X ruling that would have classified them as a MINOR "conformance fix", because
every refused call produced malformed FIX. The project owner rejected that framing on 2026-09-15: a
call that used to succeed and now fails is breaking, whatever it produced.

fixpp has no public release and no consumers, so the change ships under constitution v2.0
Article X §7, ratified 2026-09-15 on PR #451. Before the first
public release a breaking change bumps MINOR and is declared **BREAKING** on each affected symbol, in
the PR, and in the B&L delta, with every in-repository consumer updated in the same PR. The
compatibility promise, and a reset of the C-ABI version to 1.0.0, start at the first public release.
**This work merges after PR #451.**

The dictionary-precedence scope is unchanged: a dictionary that retypes a standard FIX tag does not
change how that tag pairs (r3 R3-1, §3).

**Bookkeeping:**
- `include/fix/c_api/message.h`: two new prototypes, plus reworded docs for `set_string`,
  `set_bytes`, and `commit`. Gate B r1 G-1 adds a refusal to `fixpp_msg_create_outbound`: an
  empty MsgType, or one holding SOH, returns `FIXPP_ERR_WIRE_CONFORMANCE` (B-428-4).
- `version.h`: MINOR 5→6.
- `tools/capi_freeze.sha256`: this manifest edit is the visible review point.
- `tests/abi/golden/fixpp_capi_symbols.txt`: +2.
- No new error code, so neither `error_codes_v1.txt` nor `introducing_minor` changes.
- The Python binding exposes neither new symbol (out of scope).

## 6. Witness plan

**Stage one**, run against the unfixed tree; each witness is expected RED unless marked as a regression pin. **Regression pins**, expected GREEN
both before and after (r4 R4-1): EncodedIssuer 348/349 and XmlData 212/213 carrying an embedded SOH,
through Index and Iter. Both unfixed 6-pair scanners already count these, so they guard against the
table swap losing a pair rather than proving the fix.
- **Per scanner in §1:** a counted value carrying `␁<tag>=<forged>` for a tag that scanner reads.
  - The forged value is not observed.
  - Where fields are yielded, the Data bytes come back exact and the next real field parses.
- **Per ordering-matrix cell:** the observable outcome is unchanged. That covers routing, seq
  handling, reject/disconnect disposition, stored bytes, sent bytes and clone body.
- **Specific pairs:** EncodedText 354/355, non-adjacent 1678/1697 and inverted 2372/2371 (expected RED: absent from both unfixed 6-pair tables), each through
  Index and Iter.
- **Custom pair 5001/5002:** through each dict-aware path, and through rows 7 and 10 to pin the
  standard-only residual.
- **Outbound, through both C++ `Session::send` and `fixpp_session_send` (r2 R2-5):** a Data value
  containing `␁34=`, `␁35=`, `␁10=`, `␁43=`, `␁122=` and a benign `␁58=`.
  - Each arrives at a loopback counterparty byte-exact.
  - Allow-pos-dup on and off.
- **Clone:** a Data value containing `␁10=`; the clone's body equals the source's.

**Then:**
- the §2 drift test, proven RED by deleting a header row;
- a mutant for each malformed-count arm in §4, and for each of the four §5.3 rules, each RED on its
  own;
- `set_data` witnesses:
  - append;
  - overwrite in place;
  - refusal for one half present, non-adjacent halves, Data-first, empty value and Data-as-delimiter;
  - an open top-level group and an open nested group while `set_data` appends before `group_end`,
    with the builder resolving the right entry afterwards;
  - nothing written on any refusal;
- the setter-level SOH refusal on a non-data tag, and R2-1's accepted sequence plus its entry twin.

**Pure-C conversational test** (standing ask): `EncodedText` holding an embedded SOH through
`fixpp_msg_set_data` → `fixpp_session_send` → counterparty reads the bytes exactly.

**Bench** (r1 G426-D):
- `parser_bench` and `offset_table_bench` against the baseline, both dict-free and dict-backed.
- Frames with no Length tags, and frames with a counted value full of SOH.
- The `send_impl` walk, before and after.
- Budget: no cell may exceed the bench gate's existing hard threshold.

## 7. Limitations to record (B&L)

- Custom dictionary pairs are not honoured by rows 7 (pre-session routing) and 10 (redaction at
  logger, tap and transcript sites): no dictionary is reachable there.
- **QuickFIX interop:**
  - QuickFIX locates the Length as `tag - 1`, and QuickFIX C++ sends inverted pairs Data-first. For
    those FIX50SP2 and FIX Latest pairs, fixpp splits the value at SOH, as the standard requires.
  - Both QuickFIX engines split XmlData at SOH.
- String setters and commit accept C0 control bytes other than SOH, and 0x80–0xFF. That is looser
  than TagValue §4.1 and the C++ builder; it is a compatibility choice.
- **A dictionary that repurposes a standard Length+Data tag is not honoured for pairing.** An example
  is one that types RawData(96) as STRING, or pairs SignatureLength(93) with a custom Data tag. The
  standard pair governs both parsing and commit (§3, r3 R3-1).
- #418 is unchanged: C++ `body_builder` still cannot emit a non-ASCII Data value (L-067-2). The
  generated builders keep reading pairs from the dictionary (`FieldRef::length_pair_data_tag`), not
  from the header; for the shipped dictionaries the §2 drift test is what keeps the two in step.
