# Research: 101 inbound Reject fidelity

Code read at `fa778a8e` (main checkout). Symbols, not lines; re-locate by name.

## Survey

| Surface | Today | Where |
|---|---|---|
| Header scan | `FrameHeader::msg_type` is the **last** 35; a 35 in field 3 also sets `msg_type_is_third` and `fault_ref_msg_type`; a value keeps any `=` after the first | `src/session/scan_frame_header.hpp` (`scan_frame_header`, `case 35`) |
| Header-scan readers | NotConnected, LogonReceived/Active, LogoutSent and LogonSent arms; outbound store masking (`== "A"`); resend replay (`stored_msg_type`) | `src/session/session.cpp` |
| Logon interpretation | own scan; `msg_type_found` last-wins; `!= "A"` → `session_invalid_logon` | `src/session/admin_messages.cpp`, `interpret_logon` |
| Message view | `MessageView::msg_type()` = first 35 (the offset table indexes first occurrences) | `include/fixpp/wire/parser.hpp`; `OffsetTable::build` |
| 099 FR-025 | `refuse_active_frame_`: `hdr.msg_type != hdr.fault_ref_msg_type` → 13 if `fixt_header_repeats_msg_type` else 14, 371=35, `names_third_field`; called only `if (fixt_routing_)` inside the validating gate, ahead of the 3/5 exemption | `session.cpp`; `src/session/peek_fixt_route.hpp` |
| 099 FR-029 | `establishment_frame_refused_` = `fixt_routing_ && validate && (route.application \|\| msg_type != fault_ref_msg_type)` → Disconnected, nothing sent; NotConnected and LogonSent | `session.cpp` |
| Validation-off pins | `W6PerShape.DThenAWithLogonFields_Establishes` and the `rep35` rows | `tests/session/test_099_validation_off.cpp` |
| Validator order | 092 failed-build fault scan → Step 0 header order (`wire_header_out_of_order`, no ref) → Step 1 per-field `valid_tags_for(msg_type)` (`wire_unexpected_tag`, ref = tag; FIXT framing and envelope arms) → Step 2 required → Step 3 groups | `include/fixpp/wire/validator.hpp` (`dictionary_driven_validator::validate`, `check_fields`) |
| MsgType defined? | no query; `valid_tags_for` returns an empty view on a `valid_` miss | `include/fixpp/dict/table_view.hpp` |
| Tag defined? | no query on `table_view`; `types_` holds message-reachable tags only; `field_type_of` defaults to String; `Dictionary::field_name(tag)` is the only whole-dictionary query | `table_view.hpp`, `src/dictionary/dictionary.cpp` (`as_table_view`), `include/fixpp/dict/dictionary.hpp` |
| Reason map | required 1, unexpected 2, out_of_range 5, truncated 6, header_order 14, invalid_tag_number 0, length_data_mismatch 5, default 3 | `include/fixpp/wire/reject_reason_map.hpp` |
| Gate Rejects | NotConnected, LogonSent and Active (`on_inbound_active_validate_failed_`) all pass an empty text | `session.cpp`; `RejectDecision{reason, ref_tag_id, names_third_field}` in `include/fixpp/session/session.hpp` |
| Reject builder | `ref_tag_id > 0` writes 371; non-empty 372; non-empty text writes 58 | `build_reject_with_text`, `src/session/admin_messages.cpp` |
| Reject buffers | 512-byte stack buffers in both `emit_session_reject_` overloads, the reject leaves, and the BMR site; a failed build sends nothing and returns success | `session.cpp` |
| Error slots | `wire_*` 30..42, 132, 133 (last); C mapping: unexpected/header-order/required/out-of-range → `FIXPP_ERR_WIRE_CONFORMANCE`, invalid_tag_number/length_data_mismatch → `FIXPP_ERR_WIRE_INVALID_FRAME` | `include/fixpp/core/error.hpp`, `src/capi/error.cpp`; `tests/capi/expected_error_map.csv` (118 rows), `tests/capi/error_surface_test.cpp` (118 pinned twice), `tests/core/test_092_error_completeness.cpp` |
| Benches | `scan_frame_header_bench`, `on_inbound_frame_bench` (in the CI suite, not paired); `validator_bench` (paired) | `bench/session/`, `bench/wire/`, `bench/ci-suite.txt` |

QuickFIX (read for this feature; facts in the spec's Context): C++ `2ce8a60`, QuickFIX/J `4969564`. QuickFIX/Go: no local
checkout; the B85 ruling note records Go `eff945d` for 373=0/2/11.

QuickFIX C++ reason texts (`src/C++/Values.h`, `SessionRejectReason_*_TEXT`): 0 `Invalid tag number`, 1 `Required tag
missing`, 2 `Tag not defined for this message type`, 4 `Tag specified without a value`, 5 `Value is incorrect (out of
range) for this tag`, 6 `Incorrect data format for value`, 11 `Invalid MsgType`, 13 `Tag appears more than once`, 14
`Tag specified out of required order`, 16 `Incorrect NumInGroup count for repeating group`.

## Decisions

### D1 — Two dictionary queries on `table_view`, built once (FR-001, FR-002)

**Decision:** `table_view` gains `has_msg_type(std::string_view) const noexcept` (a `valid_` lookup) and
`tag_defined(std::uint16_t) const noexcept` (a bitset over the tag space). `Dictionary::as_table_view` sets a bit for
every field in the dictionary's field store (base plus 098 overlay), and the table builder also sets it for every tag it
is given in a message, header, trailer, group or FIXT framing set, so a hand-built table answers "defined" for every tag
it knows.

**Rationale:** "Defined" in QuickFIX is the dictionary's field list (`checkValidTagNumber` → `m_fields`), not message
reach. A bitset read is O(1) with no per-message cost; built once with the table (099 builds tables once per engine).

**Alternatives:** use `field_type_of` (cannot tell unknown from String); query `Dictionary` at validate time (the
validator holds a `table_view`, not a `Dictionary`).

### D2 — Validator: an undefined MsgType, then an undefined tag (FR-001, FR-002)

**Decision:**
- After Step 0: `if (!dict_.has_msg_type(msg_type)) return wire_invalid_msg_type;` with no ref tag.
- Step 1(a): a tag outside `valid_tags_for(msg_type)` (and not a FIXT framing tag) answers
  `dict_.tag_defined(tag) ? wire_unexpected_tag : wire_undefined_tag`, ref = the tag. The envelope arm is unchanged.
- Two new `core::error` slots, appended after `wire_length_data_mismatch`: `wire_invalid_msg_type` → 373=11,
  `wire_undefined_tag` → 373=0. C mapping: both `FIXPP_ERR_WIRE_CONFORMANCE` (they are dictionary conformance results,
  as `wire_unexpected_tag` is). The CSV gains two rows; both 118 pins become 120.
- An undefined tag inside a group instance: Step 1 walks every field of the message before Step 3, against a set that
  includes group members, so it answers 373=0 there too, with no group-specific code.

**Rationale:** the owner's ruling (new slots, this order). The C mapping keeps the C-ABI code set unchanged.

### D3 — Text(58) on every validation-gate Reject (FR-003)

**Decision:** one function `session_reject_reason_text(int reason) noexcept -> std::string_view` returns QuickFIX C++'s
text for 0, 1, 2, 4, 5, 6, 11, 13, 14, 16 (static literals) and an empty view otherwise. The three gate Reject sites
(NotConnected, LogonSent, Active) pass `session_reject_reason_text(reason)` as `text`. This covers the validator's
reasons and 099's FR-025/FR-026/1128 refusals, which share these sites. Other Reject sites keep their texts.

**Rationale:** one table, three callers; `RejectDecision` does not grow (the text is a function of the reason).

### D4 — One MsgType per frame (FR-004)

**Decision:** `scan_frame_header` keeps the **first** 35 in `msg_type` and sets a new `bool msg_type_conflict` when a
later 35's value differs from it. `interpret_logon`'s scan is first-wins likewise. 099's two tests
`hdr.msg_type != hdr.fault_ref_msg_type` become `hdr.msg_type_conflict`.

**Rationale:** the message view and the validator already read the first 35; the third field is what 099 made the
RefMsgType and the consume key; the header scan and the Logon interpreter are the two readers that disagree.

**Consequence, recorded:** 099's check compared the **last** 35 with the third; a frame `D…A…D` passed it. Under D4 it
is a conflict. This widens 099's FR-025/FR-029 to every differing later value; no 099 cell is known to pin `D…A…D`
(to be checked at tasks).

**Alternatives:** keep last-wins and fix the two first-wins readers (reverses the message view's index rule and 099's
consume key).

### D5 — The repeat refusal for every validating session (FR-005)

**Decision:** in the Active/LogonReceived gate, call the repeat arm of `refuse_active_frame_` whether or not
`fixt_routing_`; its 1128 arm stays FIXT-only (a non-FIXT route never carries a refusal). `fixt_header_repeats_msg_type`
walks the header with the session's table, which a non-FIXT validating session has. `establishment_frame_refused_`
becomes `hdr.msg_type_conflict || (fixt_routing_ && validate && route.application)`, with D6's validation-off clause.

### D6 — Validation off: refuse in establishment, handle by the third field after (FR-006)

**Decision:** `establishment_frame_refused_`'s conflict clause applies whatever `validate_inbound_messages` is (both
QuickFIX acceptors close on a non-Logon first frame; neither establishes). In LogonReceived, Active and LogoutSent a
validation-off session sends no Reject for the conflict; D4 makes it handled as its third field's MsgType.

**Rationale:** the validation-off ending after Logon splits the engines (C++ rejects, QuickFIX/J delivers the first
value); "validation off sends no validation Reject" keeps 099's owner ruling.

### D7 — A Reject is sent when its echo does not fit (FR-007)

**Decision:** in both `emit_session_reject_` overloads and the BusinessMessageReject site: if the build fails because
the buffer is full and `ref_msg_type` is non-empty, build again with an empty `ref_msg_type` (372 omitted). Only the
buffer-full error takes this path; other build failures keep today's handling. The `=` in an echoed value stays.

**Rationale:** 372 is optional (`Req'd N`); the Reject's reason and 45 are what the peer needs. A retry on the failing
path only costs nothing on the normal path; a length bound would need a number that is a function of the other fields.

**Alternatives:** a bigger buffer (moves the threshold, does not remove it); refusing the inbound frame (sends nothing,
the defect itself).

### D8 — Benchmarks (FR-008, SC-004)

`scan_frame_header_bench` and `on_inbound_frame_bench` paired by hand (base `fa778a8e`, A-B-A-B, base worktree at a
path of the same length); `validator_bench` through its CI pairing. The changes add one comparison per 35 occurrence in
the scan, two O(1) lookups per validated message, and none on a validation-off frame with one MsgType.

### D9 — Cells that change (SC-003)

- 098: `DialectOverlayInboundBase098.AS3_*` (→ 11, no 371, 58) and `AS1`/`AS2` (5001/5002 undefined → 0, 58).
- `UnparseableFrameDisposition.LateSite_Control_BelowCeiling_ValidateGate_ParsedAndValidated` (5000 → 0, 58).
- `ValidateGateInbound.ManyFieldsBypassArena_Rejected_NotBypassed` (9000.. → 0).
- 099: `test_099_validation_off.cpp` rep35 rows and `W6PerShape.DThenAWithLogonFields_Establishes` (→ disconnect in
  establishment; dispatch by the third field after); the validating rep35 tables gain non-FIXT rows; tag 3115 cells in
  `test_099_toml_live.cpp` / `capi_099_fixt_live_test.cpp` checked (3115 defined only in FIX Latest).
- Cells asserting no 58 on a gate Reject; interop goldens `HP-QFj-{acc,init}-fix44-reject-invalid-admin.fix` (58 added,
  9 and 10 move; regenerated).
- Each is rewritten with the requirement that supersedes it named in the test.
