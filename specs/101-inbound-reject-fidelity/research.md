# Research: 101 inbound Reject fidelity

Code read at `fa778a8e` (main checkout). Symbols, not lines; re-locate by name. This file owns the decisions and the
supersessions; the spec, plan, contract and data model point here instead of restating them.

## Survey

| Surface | Today | Where |
|---|---|---|
| Header scan | `FrameHeader::msg_type` is the **last** 35; a 35 in field 3 also sets `msg_type_is_third` and `fault_ref_msg_type`; a value keeps any `=` after the first | `src/session/scan_frame_header.hpp` (`scan_frame_header`, `case 35`) |
| Header-scan readers | NotConnected, LogonReceived/Active, LogoutSent (`on_inbound_logout_sent_`: `hdr.msg_type == "5"`) and LogonSent arms; outbound store masking (`== "A"`); resend replay (`stored_msg_type`) | `src/session/session.cpp` |
| Logon interpretation | own scan; `msg_type_found` last-wins; `!= "A"` → `session_invalid_logon`. Public C++ (`include/fixpp/session/admin_messages.hpp`) | `src/session/admin_messages.cpp`, `interpret_logon` |
| Message view | `MessageView::msg_type()` = first 35 (the offset table indexes first occurrences) | `include/fixpp/wire/parser.hpp`; `OffsetTable::build` |
| 099 FR-025 | `refuse_active_frame_`: `hdr.msg_type != hdr.fault_ref_msg_type` → 13 if `fixt_header_repeats_msg_type` else 14, 371=35, `names_third_field`; called only `if (fixt_routing_)` inside the validating gate, ahead of the 3/5 exemption | `session.cpp`; `src/session/peek_fixt_route.hpp` (`walk_fixt_header`, which stops at the first non-header tag past field 3) |
| 099 FR-029 | `establishment_frame_refused_` = `fixt_routing_ && validate && (route.application \|\| msg_type != fault_ref_msg_type)` → Disconnected, nothing sent; NotConnected and LogonSent | `session.cpp` |
| Validator order | 092 failed-build fault scan → Step 0 header order (`wire_header_out_of_order`, no ref) → Step 1 per-field `valid_tags_for(msg_type)` (`wire_unexpected_tag`, ref = tag; FIXT framing and envelope arms) → Step 2 required → Step 3 groups | `include/fixpp/wire/validator.hpp` (`dictionary_driven_validator::validate`, `check_fields`) |
| MsgType defined? | no query; `valid_tags_for` does one `valid_.find` and returns an empty `valid_tag_set_view` on a miss | `include/fixpp/dict/table_view.hpp` |
| Tag defined? | no query on `table_view`; `types_` holds message-reachable tags only; `field_type_of` defaults to String; `Dictionary::field_name(tag)` is the only whole-dictionary query | `table_view.hpp`, `src/dictionary/dictionary.cpp` (`Dictionary::build_table_view`, which every `as_table_view` form calls), `include/fixpp/dict/dictionary.hpp` |
| Per-tag stores | `group_bits_`, `enum_bits_`, `header_bits_` are word vectors sized to the highest populated tag; a comment there rejects a fixed tag-space array | `table_view.hpp` |
| Reason map | required 1, unexpected 2, out_of_range 5, truncated 6, header_order 14, invalid_tag_number 0, length_data_mismatch 5; default 3 is a fail-closed arm its own comment calls dead on the in-contract path | `include/fixpp/wire/reject_reason_map.hpp` |
| Gate Rejects | NotConnected, LogonSent and Active (`on_inbound_active_validate_failed_`) all pass an empty text | `session.cpp`; `RejectDecision{reason, ref_tag_id, names_third_field}` in `include/fixpp/session/session.hpp` |
| Reject builder | `ref_tag_id > 0` writes 371; non-empty 372; non-empty text writes 58. A Writer overflow, including `commit`'s CheckSum room, returns `core::error::wire_field_value_truncated` | `build_reject_with_text`, `build_business_message_reject` (`src/session/admin_messages.cpp`); `Writer::append_raw`, `Writer::commit` (`src/wire/writer.cpp`) |
| Reject buffers | fixed-size stack buffers in both `emit_session_reject_` overloads, `inbound_reject_leaf_`, `inbound_reject_best_effort_leaf_` and `inbound_business_reject_leaf_`; a failed build sends nothing and returns success. The BMR leaf's `if (bmr_r)` has no `else`: no BMR, no `toApp`, no outbound number (`peek_outbound` only), no record, and the caller (`on_inbound_active_from_app_failed_`) persists the inbound advance | `session.cpp` |
| Outbound frame ceiling | `Session::kSendFrameBytes` (a private static of `Session`): `Session::send` returns `wire_frame_too_large` above it; `send_fallback_leaf_` owns a buffer of that size in its own coroutine frame, "over asio's recycler limit", so it allocates (`.specify/544-hot-path-zero-alloc.md` R-10, §1(b)). The resend replay captures a stored frame with `CaptureVisitor` (`session.cpp`'s anonymous namespace), whose buffer is `kCapBufSize`; a larger stored frame sets `truncated` and `replay_outbound_range_` returns `dispatch_aborted`. 35=j is not in the replay's `is_admin_type` set, so a stored BMR is resent, never gap-filled | `include/fixpp/session/session.hpp`, `session.cpp` |
| 092 echo bound | `dispose_unparseable_` passes `fault_ref_msg_type` only when no longer than `kMaxShippedMsgTypeLength` (092 research R-5: "the longest MsgType a shipped dictionary defines") | `scan_frame_header.hpp`, `session.cpp` |
| Error slots | `wire_*` slots end at `wire_length_data_mismatch`; C mapping: unexpected/header-order/required/out-of-range → `FIXPP_ERR_WIRE_CONFORMANCE`, invalid_tag_number/length_data_mismatch → `FIXPP_ERR_WIRE_INVALID_FRAME`; `translate` is a total switch with no `default` | `include/fixpp/core/error.hpp`, `src/capi/error.cpp`; `tests/capi/expected_error_map.csv`, `tests/capi/error_surface_test.cpp`, the `test_0NN_error_completeness.cpp` files |
| Benches | `scan_frame_header_bench`, `on_inbound_frame_bench` (in the CI suite, not paired); `validator_bench` (paired); `table_view_footprint_bench` | `bench/session/`, `bench/wire/`, `bench/dictionary/`, `bench/ci-suite.txt` |

QuickFIX (read for this feature; facts in the spec's Context): C++ `2ce8a60`, QuickFIX/J `4969564`. Both write the
BusinessMessageReject's 372 from the inbound MsgType with no frame bound: C++ `Session::generateBusinessReject`
(`reject.setField(RefMsgType(message.getHeader().getField<MsgType>()))`), J `Session.generateBusinessReject`
(`reject.setString(RefMsgType.FIELD, msgType)`). RefMsgType(372) is `required='Y'` in the BusinessMessageReject of the
vendored `dictionaries/FIX42.xml`, `FIX44.xml` and `FIX50SP2.xml`. QuickFIX/Go: no local
checkout. The B85 ruling note records Go `eff945d` for 373=0/2/11; **Go was not read for #646** (the 372 echo), although
that issue's "Done when" asks for all three engines. The C++ and J reads agree, and the ruling follows them.

QuickFIX C++ reason texts (`src/C++/Values.h`, `SessionRejectReason_*_TEXT`), for the reasons the validation gate sends.
The strings quoted here, in the spec, the contract and the data model are informative; the authority is `Values.h` at
`2ce8a60`, which tasks T004 transcribes, and a test literal is copied from that transcription:
0 `Invalid tag number`, 1 `Required tag missing`, 2 `Tag not defined for this message type`, 4 `Tag specified without a
value`, 5 `Value is incorrect (out of range) for this tag`, 6 `Incorrect data format for value`, 11 `Invalid MsgType`,
13 `Tag appears more than once`, 14 `Tag specified out of required order`.

QuickFIX C++ behaviour this feature is judged against (read at the commit above):
- `Message::setString` classifies a field as header by `Message::isHeaderField(field, pSessionDD)`: a **static** list
  (1128, 1129, 627, 369, 370, the Sub/Location IDs, …) **or** the session dictionary's header, on every BeginString.
- `DataDictionary::validate` runs `hasValidStructure` (a header field after a body field → 373=14) **before**
  `iterate` (`RepeatedTag` → 373=13), then `checkMsgType`, `checkHasRequired` (373=1) and the tag-ordered field walk.
- `Message::setGroup` ends a group at the first non-member field and rewinds, so that field lands in the body.
- `Session::verify` → `validLogonState` reads the first 35 and disconnects a non-Logon first frame.
- `Session::populateRejectReason` writes `<text> (<tag>)` into 58 below FIX.4.2, where 371 and 373 do not exist;
  `Session::generateReject` writes 373 on FIX.4.2 only for reasons up to 11.

## Decisions

### D1 — A defined-tag store on `table_view`, built once (FR-002)

**Decision:**
- `table_view` gains `tag_defined(std::uint16_t) const noexcept`, backed by a **sorted `std::vector<std::uint16_t>` read
  by binary search**. The lookup is cold (D2 calls it only after `valid_tags.contains` has already failed), so the
  smaller store wins over a word vector on FIX Latest and on high-tag overlays. A fixed tag-space bitset is rejected for
  the reason the existing per-tag stores' comment gives.
- **Dictionary-built tables:** `Dictionary::build_table_view` fills the store from the merged field store: the
  handle's field list after any 098 overlay merge, i.e. the QuickFIX-XML `<fields>` set or Orchestra's fields. Every
  `as_table_view` form (session, FIXT route, route plus `extra`) goes through that function, so none is missed.
  - *Which store (checklist audit).* "The handle's field list" is the set of field definitions the loader keeps for the
    whole dictionary, the one `Dictionary::field_name` and `field_by_name` read (`dict_metadata_handle::field_by_name_`,
    filled from every QuickFIX-XML `<field number=…>` under `<fields>`, or every Orchestra `<fixr:field id=…>`). It is
    **not** `dict_metadata_handle::fields_`, which holds per-message field runs, i.e. message reach, the meaning this
    decision rejects. So a field declared under `<fields>` that no message references is defined (373=2, not 373=0),
    and so is a field a 098 overlay adds without adding a message that reaches it: a peer can see 373=2 where the base
    alone answers 373=0, depending on the overlay the operator loaded.
  - The fill's own population operations call `mark_defined` too (they are the same operations as on a hand-built
    table), so a dictionary-built table also defines every tag those operations name: its header tags, the
    `fixt_framing` tags and NoHops metadata of a FIXT form, and `extra`'s pairs. A route's store is the routed
    dictionary's field definitions plus those, not the transport dictionary's: an S header tag in an application frame
    is judged by the validator's envelope arm, and a framing tag passes Step 1(a), before the 0/2 split runs (D2).
  - A wire tag above 65535 never reaches `tag_defined(std::uint16_t)`: the scanners reject it as a malformed tag
    (`wire::accumulate_tag_digit`), which is the 092 disposer's case, so no narrowing can alias it to a defined tag.
  - A `table_view` is copied when a table is built or adopted (`Session::open()`, `fixt_route_set::make`, whose
    validators hold their tables by value, a dictionary snapshot), never per message; the store's copy cost is
    therefore an open-time cost, which D8's `BM_TableView_Copy*` rows time.
- **Hand-built tables:** one private `mark_defined(tag)`, called by **every population operation that names a tag**.
  The census at round 1, re-derived by reading the private mutators `table_view_builder` forwards to:
  `add_valid_tag`/`add_valid`, `add_required_tag`/`add_required`, `set_field_type`/`set_type`, `add_enum`,
  `set_multi_value`, `add_group_member`, `add_group_required_member`, `set_group_first` and their `_ctx` forms,
  `add_header_tag`, `add_fixt_framing_tag`, and both tags of `set_length_pair_data_tag`. Re-derive it at tasks by
  listing the mutators; a mutation cell (drop `mark_defined` from one op) must go RED. The rule binds a population
  operation added later too: the comment at `mark_defined` states it as a procedure (every population operation that
  names a tag calls this; re-derive the set by listing the private mutators `table_view_builder` forwards to), and the
  census, re-run at T068, finds an operation that does not call it.
- The store is sealed (sorted, deduplicated) once, when the table is finished; no per-message cost.

**Rationale:** "Defined" in QuickFIX is the dictionary's field list (`checkValidTagNumber` → `m_fields`), not message
reach. A hand-built table answers "defined" for every tag it was told anything about, so a test table that gives a tag a
type cannot answer 373=0 for it.

**Alternatives:** `field_type_of` (cannot tell unknown from String); query `Dictionary` at validate time (the validator
holds a `table_view`, not a `Dictionary`); a fixed tag-space bitset (footprint, copied per validator).

### D2 — Validator: an undefined MsgType, then an undefined tag (FR-001, FR-002)

**Decision:**
- After Step 0, the validator takes `valid_tags_for(msg_type)` **once** and tests whether the key was found:
  `valid_tag_set_view` gains `found() const noexcept` (true when the view refers to a `valid_` entry). A miss returns
  `wire_invalid_msg_type`, no ref tag. Step 1 reuses the same view, so the hot path keeps one `valid_.find` per message,
  as today. "MsgType defined" means `valid_` holds the key, so a MsgType is defined exactly when some
  `add_valid_tag`/`add_valid` or `add_required_tag`/`add_required` named it (`add_required_tag` writes both `required_`
  and `valid_`). The group-context, per-tag, header and framing operations do not define a MsgType. On a
  dictionary-built table every declared message gets its header tags as valid, so every declared MsgType is defined.
  (That comes from `Dictionary::build_table_view` calling `add_valid` for each declared message's header tags, not from
  the header operation itself, which defines no MsgType.)
  Re-derive the writers with `grep -n "valid_\[" include/fixpp/dict/table_view.hpp` (Gate A round 3).
  Consequence on a hand-built table: one that names application messages but no admin message Rejects every admin
  frame its validating session receives (other than 3 and 5, which the gate skips), as it does today (373=2, 371=8
  today; 373=11 after). A hand-built validating table names every MsgType it accepts, admin included.
- **Position (checklist audit).** The MsgType check sits after 092's failed-build fault scan and after Step 0, and
  before Step 1, whose FIXT envelope arm is per field. 099's 1128 refusals and the repeat refusal (D5) are not the
  validator's: `refuse_active_frame_` answers them at the gate, ahead of `validate_inbound_`. So an unknown MsgType on
  a frame whose 1128 099 refuses answers the 1128 refusal (373=4 or 5, 371=1128); a frame whose 1128 resolves, or that
  carries none, is validated with the resolved version's table, which answers 11 when it does not define the MsgType.
  Step 0 tests only that the first field after 8 and 9 is 35, so an undefined tag among the header fields (9999
  between 52 and 56) passes it and answers Step 1's 373=0.
- Step 1(a): a tag outside the view (and not a FIXT framing tag) answers
  `dict_.tag_defined(tag) ? wire_unexpected_tag : wire_undefined_tag`, ref = the tag. The envelope arm is unchanged.
- Two new `core::error` slots, `wire_invalid_msg_type` → 373=11 and `wire_undefined_tag` → 373=0, appended after the
  last enumerator at implement time; the values are pinned by the 101 completeness test, not written here. C mapping:
  both `FIXPP_ERR_WIRE_CONFORMANCE` (dictionary conformance results, as `wire_unexpected_tag` is). Error-slot test plan:
  D9.
- An undefined tag inside a group instance: Step 1 walks every field of the message before Step 3, against a set that
  includes group members, so it answers 373=0 there too, with no group-specific code. The walk is flat (one
  `field_iterator` over the whole frame), so this holds at any nesting depth. A tag the dictionary defines that sits
  inside a group instance it is not a member of is told apart the same way as outside a group: if the message's set
  holds it elsewhere, Step 1 passes it and Step 3 gives the group-structure result; if the message's set does not
  hold it, Step 1 answers 373=2. Traced against QuickFIX C++ on
  098's AS-2 frame (one Parties instance, the undefined tag last in it): `setGroup` ends the group there, the tag lands in
  the body, `iterate` reaches `checkValidTagNumber` → 373=0, 371 = the tag. The same answer. When the tag sits
  mid-instance, C++ answers 373=2 on the first later member by tag order, and with several instances it may answer 373=16
  on the count first; that divergence is a B-row (FR-009).

**Rationale:** the owner's ruling (new slots, this order). The C mapping keeps the C-ABI code set unchanged, so no
version step is owed (Codex and Opus agree).

### D3 — Text(58) on every validation-gate Reject (FR-003)

**Decision:** one function `session_reject_reason_text(int reason) noexcept -> std::string_view` returns QuickFIX C++'s
text for 0, 1, 2, 4, 5, 6, 11, 13 and 14 (static literals) and an empty view otherwise. The three gate Reject sites
(NotConnected, LogonSent, Active) pass `session_reject_reason_text(reason)` as `text`. This covers the validator's
reasons and 099's FR-025/FR-026/1128 refusals, which share these sites. The map's fail-closed default (3) is dead on the
in-contract path and gets no text. Other Reject sites keep their texts. The text is a function of the reason, derived at
the emit site; `RejectDecision` does not grow.

**The list is derived, not kept (checklist audit).** Re-derive it at tasks (T025) and after the last fix commit: every
reason `wire_error_to_session_reject_reason` returns from a named case (read its `case`/`return` pairs, after T021 adds
the two new slots), plus every reason `refuse_active_frame_` and its 1128 arms set, plus 099's other gate refusals at
the three sites. Every reason so found other than the fail-closed 3 must be in the list; a reason missing from it
stops T025. The validator's results all reach a named case (its group-structure results are `wire_required_field_missing`
or `wire_field_value_out_of_range`, and a decimal-precision loss is re-mapped to `wire_field_value_truncated`), which is
why 3 is unreachable on the in-contract path. A gate Reject for a reason outside the list (only 3, if the fail-closed arm
ever ran) is sent with no 58, not refused.

**Scope of "QuickFIX C++'s text":** the FIX.4.2-and-later form (58 = the text alone). Below FIX.4.2 QuickFIX C++ writes
`<text> (<tag>)` and no 371/373; on FIX.4.2 it writes 373 only for reasons up to 11. fixpp writes 371/373 on every
BeginString today and keeps doing so; the difference is a B-row (FR-009).

### D4 — One MsgType per frame (FR-004)

**Decision:** a frame's MsgType is its **first 35**. On every frame a state arm handles that is its third field, because
093 disregards a frame whose third field is not 35 before any arm runs. Every reader reads that one value:
- `scan_frame_header` keeps the first 35 in `msg_type` and sets a new `bool msg_type_conflict` when **any** later 35's
  value differs from it (sticky: `D…A…D` is a conflict). It is the only new `FrameHeader` member; the hot path adds one
  comparison per later 35. "Differs" is byte inequality of the two values as the scan slices them (the bytes after the
  field's first `=`), with no case folding, trimming or other normalisation. The scan records no fault for an empty
  value, so a later `35=` with an empty value differs from a non-empty first 35 and is a conflict.
- `interpret_logon` (public C++) reads the first 35 likewise. Inside the session this is unobservable (D6 refuses every
  conflicting frame before either establishment arm calls it, and field 3 is read identically under any hooks), so it
  gets a direct unit cell and a B-row. A debug assertion at both arms that `interpret_logon`'s MsgType equals
  `hdr.msg_type` is optional. A direct caller of `interpret_logon` gets no conflict signal from it: `35=A … 35=D` with
  Logon fields is accepted as a Logon. The conflict is `FrameHeader::msg_type_conflict`'s, which only the Session's
  arms read; the B-row says so.
- A frame that reaches the session in Disconnected is drained without a scan (`on_inbound_cold_`'s Disconnected row),
  so no reader reads its MsgType; every other state's arm scans it.
- **Readers, by recipe.** The reader list of FR-004 is re-derived, not kept: `command git grep -nE '\bmsg_type\b' --
  src/session` (known positive: LogoutSent's `hdr.msg_type == "5"` in `on_inbound_logout_sent_`), each hit read in
  its function, plus the callers of `MessageView::msg_type`; a reader the list does not name is classified at T039.
- 099's `hdr.msg_type != hdr.fault_ref_msg_type` tests become `hdr.msg_type_conflict`.
- LogoutSent's `hdr.msg_type == "5"` now reads the first 35, on every session kind: `D…5` no longer confirms the Logout,
  `5…D` does.
- The resend replay and the outbound store masking read the first 35; stored outbound frames carry one MsgType, so their
  behaviour does not change (controls in D9).

**Rationale:** the message view and the validator already read the first 35; the third field is what 099 made the
RefMsgType and the consume key; the header scan, the Logon interpreter and LogoutSent's reply test are the readers that
disagree.

**Alternatives:** keep last-wins and move the first-wins readers (reverses the message view's index rule and 099's
consume key).

### D5 — The repeat refusal on every validating session, with QuickFIX's 13/14 precedence (FR-005)

**Decision:**
- In the LogonReceived/Active gate, the repeat arm of `refuse_active_frame_` runs whether or not `fixt_routing_`; its
  1128 arm stays FIXT-only (a non-FIXT route never carries a refusal).
- **13 or 14.** `fixt_header_repeats_msg_type` becomes one cold walk that does **not** end at the header's stop; it
  reports whether a later 35 stands in the header and whether one stands past it. It is its own walk (or a parameter of
  `peek_detail::walk_fixt_header` that only it passes): `peek_fixt_route`'s walk and its stop stay as they are, on S's
  hooks. **Every field, header and body, is read with the selected route's hooks** (`session_hooks(*route.entry.owner)`,
  the hooks the inbound arm's `scan_frame_header` call used, and the table the frame is parsed and validated with; S's
  own on a non-FIXT session, where `select_route_` returns `inbound_tv_`). **S's header union classifies** the fields
  (`*inbound_tv_`): 099 judges the header by S. Owner ruling 2026-11-07, option (b) (below); it supersedes Gate A
  round 2's "S's hooks before the stop, the route's from the stopping field". **The walk, field by field:**
  1. It is one loop with one `length_data_carry`, never reset or replaced.
  2. For each field in frame order, parse the tag.
  3. Classify the tag with S's header union (`table_view::is_header_tag` ∪ `dict::is_fixt_standard_header_tag`). Once
     a field is not a header field, `past_stop` stays true for it and every later field. The ruling changes no part of
     this step: the classifying table is S's, as in the converged D5 and in today's `walk_fixt_header` (`s_table`).
  4. Read its value with `carry.read_value(frame, vstart, tag, route_hooks)`, the same hooks for every field.
  5. A 35 after field 3 is in the header when `past_stop` was false for it, else past the header.

  Why one carry across the stop: `length_data_carry::read_value` decides whether **this** field is counted from what
  the **previous** call armed. A Length in the header whose Data is the stopping field counts that Data only if the
  carry survives the classification stop; a reset there splits the Data at its first SOH, and an embedded `SOH 35=`
  reads as a phantom past-header 35 that the scan, which never resets, does not see.

  **Scan and walk agree by construction.** The walk and `scan_frame_header` read with the same hooks, through the same
  `length_data_carry::read_value` and the same tag-digit rule (`accumulate_tag_digit`, an `=` required, an empty tag a
  fault), and `refuse_active_frame_` receives the `route` whose owner's hooks built `hdr` (each arm binds one `const`
  `route` from `select_route_` and scans with it). So the two split every frame on the same boundaries: a later 35 the
  scan's `msg_type_conflict` saw is a later 35 the walk sees, and the reverse.

  **The answer.** `refuse_active_frame_` answers **14 when any later 35 lies past the header, else 13 when one lies in
  it, else 14** (the three-clause rule; the last clause is the answer for a walk that sees no later 35). **When the
  walk faults** (`read_value` returns `nullopt` on a malformed count, or a tag is malformed), the walk returns false,
  the 35s it saw before the fault are discarded, and the answer is the default clause, 14. That fault rule is a chosen
  ending, kept so the function is total: through a real `Session` it cannot be reached, because the walk faults only
  where the route-hooked scan faults at the same field, and a frame whose scan records a fault goes to the 092 disposer
  (`dispose_unparseable_`, ahead of `refuse_active_frame_` and `establishment_frame_refused_` in every arm) and never
  reaches the gate. Its witness is therefore a unit cell on `fixt_header_repeats_msg_type` (D9). For the same reason
  the default clause ("else 14" for a walk that sees no later 35) is reached only through a fault: the unit cell
  asserts the walk's report, not `refuse_active_frame_`'s mapping of it; the mapping's default clause is witnessed by a
  direct `refuse_active_frame_` call through the test-side friend `session_test_access` on a faulting frame (tasks
  W101-51, M101-41).

  This is QuickFIX C++'s `hasValidStructure`-before-`iterate` precedence. With exactly two 35s
  it equals 099's answer; with three or more it can differ (`0,D(hdr)…0(past)` and `0,0(hdr)…D(past)` answer 14, where
  099's rule answers nothing and 13 respectively). The reason is decided from the walk alone: a count from
  `scan_frame_header` is never combined with the walk's; they agree by construction, but the walk is the one that
  classifies.
- **Owner ruling (2026-11-07), checklist `msgtype-and-framing.md` CHK006, CHK008, CHK010: option (b), the walk reads
  every field with the route's hooks.**
  - *Measured* (the reachability bullet below and that checklist's Audit Result): under Gate A's D5 (S's hooks for the
    header) the scan and the walk disagreed on no shipped pairing, and on no dictionary where S's own `<header>` holds
    the pair's Length or Data tag. They disagreed on a custom S that pairs a non-standard Length on a tag of
    `dict::is_fixt_standard_header_tag` its own `<header>` omits: `dict::header_length_pairs` tests S's own
    `header_tags_`, not the union the walk classifies by, so `fixt_route_set::make` does not merge that pair. The
    route-hooked scan then split the Data and could flag a conflict the S-hooked walk did not see (a conformant frame,
    by S's reading, drew 14 or a disconnect); the reverse could turn a real conflict's 13 into 14.
  - *Why (b).* (a), widening `header_length_pairs` to S's header union, changes 099's merge rule and closes only the
    direction that misfires. (c), keeping D5 and disclosing the residual, leaves two readings of one frame. (b) makes the
    walk read as the scan, parser, validator and message view already do ("a route is one table", 099 research R-20),
    so they agree in both directions on every dictionary, by construction rather than by measurement.
  - *The residual's ruled ending.* On that custom S the frame is refused as the route reads it, like every other reader
    of it: the split Data yields a later 35, 14 when the split lands past the stop (the Data tag outside S's header
    union), 13 when it lands in the header, and a disconnect in an establishment arm, when every piece of the split Data is a well-formed field (e.g. `1=a<SOH>35=X<SOH>2=b`); otherwise the scan faults and the 092 disposer takes the frame. This is not a fault and
    not a B-row:
    the route's parse sees two MsgTypes.
  - *No shipped behaviour changes against D5 as Gate A converged it.* On every shipped pairing the route's hooks answer
    every tag of S's header union as S's hooks do: a standard pair is answered from the standard table
    (`dict_hooks::data_tag_for_length`); a pair S's own `<header>` holds is merged into every route
    (`fixt_route_set::make` over `dict::header_length_pairs`, 099 R-20); no vendored file declares a header pair outside
    the standard table (`W17Overload.VendoredFileDeclaresNoHeaderPair`); and the only non-standard pairs (FIX50SP2's and
    FIX Latest's body pairs) are read with the route's hooks under both rules. On a non-FIXT session the two hook sets
    are one. Re-derive by the next bullet's recipe.
  - Retired with Gate A's rule: the "stopping field takes the route's hooks" boundary cell and its mutant (D9 re-aims
    them); the old fault-rule witness through a real `Session` (D9 moves it to a unit cell).
- The walk's header test stays `table_view::is_header_tag` ∪ `dict::is_fixt_standard_header_tag` on non-FIXT sessions
  too. That union is the closer model of QuickFIX C++'s `isHeaderField` (static list or session dictionary, every
  BeginString). The residual — the tags `is_fixt_standard_header_tag` holds and QuickFIX C++'s static list lacks (at
  round 1: HopCompID 628, HopSendingTime 629, HopRefID 630, ApplExtID 1156; re-derive by comparing the two lists) — is
  disclosed (FR-009), not excluded: a FIX.4.x frame carrying them in the header is already non-conformant.
- `establishment_frame_refused_` becomes `hdr.msg_type_conflict || (fixt_routing_ && validate && route.application)`;
  the conflict clause holds whatever `validate_inbound_messages` is (D6).
- **Where the scan's hooks and Gate A's walk could disagree (checklist audit; the measured basis of the 2026-11-07
  ruling above).** The inbound arms scan the whole frame with the route's hooks (`session_hooks(*route.entry.owner)`),
  and `msg_type_conflict` comes from that scan; Gate A's walk read the header with S's hooks. The two split a field
  differently only where one hook set counts a Length+Data pair the other does not:
  - A standard pair is answered from the standard table whatever the dictionary (`dict_hooks::data_tag_for_length`),
    so only a pair outside it can differ.
  - S's header pairs are on every route's table: `fixt_route_set::make` registers each listed S's
    `dict::header_length_pairs` into every other dictionary's table, S winning a collision (099 research R-20), every
    FIXT session's dictionary is listed (`Engine::start`, or `Session::open()` for a direct session), and
    `bind_fixt_routes_` picks the `{d, S}` entries. `dict::header_length_pairs` takes a pair when S's own `<header>`
    holds its Length or its Data tag, so a pair on a tag only `dict::is_fixt_standard_header_tag` puts in the union is
    not merged: that is the custom residual the ruling closes.
  - On the shipped dictionaries no file declares a header pair outside the standard table (pinned by
    `W17Overload.VendoredFileDeclaresNoHeaderPair`), and FIXT11 declares no pair outside it at all; the only such pairs
    are FIX50SP2's and FIX Latest's body pairs, which both rules read with the route's hooks, as the scan does, since a
    body tag ends the header. So on every shipped pairing the two agree.
  - Re-derive: `dict::header_length_pairs` over each dictionary a session lists, and the non-standard DATA fields of
    each (a QuickFIX-XML `type='DATA'` or Orchestra `type="data"` field whose tag the standard table lacks); known
    positive: a FIXT11 overlay adding a header pair (`W17HeaderLengthPairs.AnOverlaysHeaderPairIsAnswered`).

### D6 — Validation off: refuse in establishment, handle by the first 35 after (FR-006)

**Decision:** `establishment_frame_refused_`'s conflict clause applies whatever `validate_inbound_messages` is (both
QuickFIX acceptors close on a non-Logon first frame; neither establishes). In LogonReceived, Active and LogoutSent a
validation-off session sends no Reject for the conflict; D4 makes it handled as its first 35.

**"Handled as X" is defined by a differential oracle**, not a hand table: the ending of a conflicting frame equals the
ending of the same frame with every later 35 removed and the frame re-framed (BodyLength, CheckSum). This covers X whose
own path can Reject (`4…D` is handled as a SequenceReset; `A…0` in Active is a 35=A, which reaches `fromApp` with an
Application and Reject 373=3 without one). "No Reject" means no Reject **for the repeat**.

**Terms (checklist audit).**
- An *ending* is the value tasks T005's `ending_of` records: every outbound frame, each with the fields T005 decodes
  (35, 34, 45, 58, 371, 372, 373); the callbacks fired, with their counts; the state; and NextNumIn. Two endings are
  equal when each of those is, outbound frames compared field for field, so a Reject the stripped twin draws for
  another cause must match the conflicting frame's Reject field for field.
- A Reject *for the repeat* is one the conflicting frame draws and its stripped twin does not. A reader therefore
  tells the two apart by building the twin, not from the frame alone, and the oracle is that test.
- Every first frame reaches the conflict check: the Engine's first-frame path reads only the CompIDs
  (`scan_first_frame_ids`) and hands the frame to `on_inbound_frame`, so the acceptor's NotConnected arm runs on it.

**Rationale:** the validation-off ending after Logon splits the engines (C++ rejects, QuickFIX/J delivers the first
value); "validation off sends no validation Reject" keeps 099's owner ruling.

### D7 — The RefMsgType echo has one owner and one rule (FR-007)

**The rule (Gate A round 2, orchestrator ruling; one rule for 372 on both messages).** A session Reject and a
BusinessMessageReject echo the inbound MsgType in 372 verbatim (`=` included) whenever the message fits the engine's
outbound frame ceiling, `Session::kSendFrameBytes`. Two edges of "verbatim" (checklist audit): a value that begins with
`=` (`35==D`) is echoed like any other (`372==D`); an empty MsgType has nothing to echo, since the builders write 372
only when it is non-empty (Survey), so such a Reject carries no 372 at any size and step 3 does not apply to it. That is the bound `Session::send` already enforces
(`wire_frame_too_large` above it) and the bound the resend replay's capture depends on (`CaptureVisitor::kCapBufSize`).
Above the ceiling, the Reject is sent without 372 (372 is optional in 35=3, `Req'd N`; the #646 ruling), and the BMR is
not sent (372 is required in 35=j). On a later ResendRequest a stored Reject, rebuilt in the slot or sent without 372,
is admin, so the replay answers it with a GapFill as it does every Reject today (the replay's `is_admin_type` set holds
3); only the BMR is resent, with its 372 (US4 scenario 3).

**Decision:** one internal helper in `session.cpp` owns the echo policy for a session Reject, and a twin owns it for the
BusinessMessageReject (`build_business_message_reject`):
1. Build into the site's own fixed buffer with 372 = the inbound MsgType, verbatim. This is today's build, and the
   path a fitting message takes is unchanged.
2. If that build fails with `core::error::wire_field_value_truncated`, rebuild the message with 372 into the
   session's **inbound reply slot**, `inbound_reply_slot_`: a `std::array<std::byte, kSendFrameBytes>` member of
   `Session`, allocated with the Session as `send_frame_slot_` is. The rebuild is a non-coroutine helper (a class-1
   build in #544 §2.2's terms: it neither suspends nor calls back). The peeked sequence number and the SendingTime
   value the site already holds are passed in, so nothing is re-peeked or re-stamped.
3. Reject only: if the rebuild also fails with `wire_field_value_truncated` and the MsgType is non-empty, the helper
   builds once more into the slot with an empty RefMsgType (372 omitted). The BMR has no such step.
4. Whatever still fails is not sent: the outbound-ceiling ending below. Text(58) is never dropped to make room
   (FR-003).

The trigger is "out of room", whatever took the room. The slot rebuild therefore also recovers a Reject built through
the helper that overflowed its site's buffer because of long CompIDs or the Text(58) D3 adds, not only because of 372.
Outside the helper, `apply_inbound_sequence_reset`'s Reject (a literal 372, not the inbound MsgType) is built in its
own fixed buffer; long CompIDs that overflow it drop it with no record. That is the CompID-length residual, disclosed
with the outbound-ceiling B-row and closed only by the follow-up CompID length bound at `open()`. Other codes
cannot fire:
- `wire_frame_too_large` needs a body beyond BodyLength's placeholder width. That width is the digits `Writer`
  reserves for 9 (`src/wire/writer.cpp`), and no `kSendFrameBytes` buffer reaches it. Above the ceiling, the rebuild
  therefore fails with `wire_field_value_truncated`.
- The builders' own `render_u32` failures cannot fire either.

**One tail, in the site's own coroutine.** The site selects the span that holds the frame: its own buffer when step 1
fit, the slot after step 2 or 3. It then runs its existing tail over that span. No tail is copied, and no coroutine
is added on either path.
- A Reject's tail is `assign_outbound()`, then `fire_to_admin_`, then `store_then_emit`. Each site keeps its own
  failure handling unchanged:
  - Disconnected on a failed assign, in both `emit_session_reject_` overloads and in `inbound_reject_leaf_`;
  - the discarded emit error, in `inbound_reject_leaf_`;
  - the best-effort assign, in `inbound_reject_best_effort_leaf_`.
- The BMR's tail is `toApp` through `parse_and_dispatch_(…, kSendParseArena, …)`, with its veto and throw arms, then
  `assign_outbound()`, then `store_then_emit`.
- The site's frame keeps today's buffer and gains only the span and the slot holder below. It must still fit #544's
  recycler limit; D9's allocation cells check it on the fitting twin.
- A frame the built-frame parse cannot read returns `parse_failed`, and the built-frame sites ignore it. So a callback
  that silently never fired on a rebuilt frame would show nothing, and D9 witnesses the callback at the ceiling.

**Placement:** every build attempt, the slot rebuild included, runs strictly after `seqnum_mgr_.peek_outbound()` and
the SendingTime stamp. It runs strictly before the site's first callback and before `assign_outbound()`:
- for a Reject, `fire_to_admin_`, which every Reject site runs after `assign_outbound()`;
- for the BMR, `toApp`, which fires before `assign_outbound()` (036 C2).

So no attempt re-stamps, re-numbers or fires a callback twice.

**Outbound-ceiling ending.** What step 4 leaves is not sent: a Reject that exceeds `kSendFrameBytes` even without 372
(FR-007 (a)) and a BMR that exceeds it with 372 (FR-007 (b)). (a): no callback, no outbound number, the record.
(b): no callback, no outbound number, the inbound consumed as today, the session in its state, the record. For (a)
the inbound frame is disposed of as for (b): consumed as today at that site (the gate consumes in LogonReceived and
Active and not in an establishment arm), the session in its state. A Reject sent without 372 (step 3) is a sent
message and writes no record; only the not-sent endings do.
- **Rate.** Each not-sent ending of (a) and (b) writes at most one session log record per max(HeartBtInt, 1 s) on
  `effective_clock_`, the rule of 093's garbled-frame record (`Session::note_garbles_`), with HeartBtInt resolved as
  `run_liveness_loop` resolves it. A record carries the count of not-sent messages suppressed since the previous
  record. That HeartBtInt is the session's configured `SessionConfig::heartbeat_interval` (its default,
  `kDefaultHeartBtInt`, when unset), never a value negotiated at Logon, so a not-sent ending before Logon (the
  NotConnected and LogonSent gate arms, a 092 disposer reply before establishment) uses the same window as after it;
  the 1 s floor bounds a HeartBtInt of 0. Like the garble record's members, the rate state is never reset: it carries
  across connections of one Session.
- **Bounded record.** A record does not echo the inbound MsgType, a peer-controlled value up to the inbound limit; it
  records the MsgType's length and the message type not sent, plus the suppressed count. It is a `FIXPP_SLOG`
  enqueue, as 093's garble record is: asynchronous (`[const §XIII.2]`, `[const §XV.5]`), with integer arguments and a
  fixed name, so it formats nothing on the strand.
- **One rate state for both.** The two endings are one cause, an inbound frame whose reply cannot fit the outbound
  ceiling, so they share **one** rate state and **one** suppressed count. Each record names the message type that was
  not sent and the count suppressed since the previous record. The state is a set of `Session` members beside the
  garble record's (`garble_logged_`, `garble_log_next_`, `garbles_unlogged_`), not those members: the garble record
  counts a different cause.
- **Format string.** Registered in `src/log/format_registry.cpp`, where 093's records are; its CRC-collision
  `static_assert` stays green.

**The inbound reply slot: who uses it, and for how long.**
- **Users.** Only the inbound-fed reply sites listed under "Sites" below. Each is called from an `on_inbound_*` arm, or
  from `dispose_unparseable_`, which only inbound arms call. Re-derive this with the census recipe plus each caller's
  enclosing function. Tasks pin it by reading every caller, not by counting.
  - No other chain touches the slot. `run_liveness_loop`, `Session::send`, `close()` and the resend replay build in
    their own storage.
  - That restriction answers #544 §2.2's rejected "`Session`-member scratch buffer for the inbound reply blocks". That
    rejection was of a buffer shared with the strand's other chains (its example: `run_liveness_loop`'s emits while a
    reply is suspended in its write), for which no per-chain exclusivity argument existed. Here the only chain is the
    inbound one, and its exclusivity is the precondition below.
- **Held.** From the start of the slot rebuild until the site's tail returns, which is after `store_then_emit` returns.
  The bytes must outlive the tail's `co_await`s:
  - `store_then_emit` stores the frame, then transmits the caller's span after the store suspends (the reply-leaf
    comment above `inbound_reject_leaf_`);
  - the transmit goes through `live_write_serialized_`, which holds that span until its `async_write` completes;
  - the BMR's `toApp` re-parse reads the slot synchronously before both.
- **Released.** By an RAII holder's destructor on every exit of the tail: success, the Disconnected and error returns,
  the throw arm, and destruction of the site's coroutine frame by a close.
- **Nothing else can take the slot before release.**
  1. Within one inbound frame, an arm `co_await`s each reply site to completion before its next statement, so the
     reply sites of one frame are sequential.
  2. A callback the tail fires (`toApp`, `toAdmin`) cannot begin an inbound frame: `FIXPP_ASSERT_NO_CALLBACK_AT_PARSE_SITE`
     asserts at every inbound parse site. A `send()` or `close()` started from a callback begins after the callback
     returns and uses its own storage (`on_inbound_frame`'s reentrancy contract in `session.hpp`).
  3. Across frames, the next `on_inbound_frame` begins only after the current one returns: the serialized-caller
     precondition.
  4. The slot is a member of one Session, so sessions that share a strand or an executor never share a slot: another
     session's frame, pumped while this one's reply is suspended, rebuilds into its own.
- **Release before the next frame (checklist audit).** The holder is released on the tail's return, so before the
  site, and so `on_inbound_frame`, returns; a reconnect's first frame comes through a new `on_inbound_frame` call,
  which the precondition orders after the previous one completes. A close that destroys the site's coroutine frame
  without resuming it releases the slot in the holder's destructor, as part of that destruction, before any later
  call on that Session can begin. W101-46b's exits include a close during the store's suspension, followed by a next
  frame whose overflowed reply is sent.
- **Who must honour the precondition.** Only a direct C++ caller of `Session::on_inbound_frame`. Engine-managed
  sessions meet it by construction (below), and no C-ABI or binding entry point calls `on_inbound_frame`: the C and
  Python surfaces drive sessions through the Engine.

**The serialized-caller precondition.**
- **Source: overlapping calls are already outside what the session supports.**
  - **The Engine never overlaps them.** `run_read_pump` (`src/session/engine.cpp`) `co_await`s
    `session.on_inbound_frame(...)` for each frame before it handles the next. `run_accept_loop` `co_await`s the
    first Logon's delivery, then runs the pump inline. `run_connect_loop` runs the pump inline. So each Session has
    one pump.
  - **The arms mutate state across `co_await`s and are unsound under overlap.** `on_inbound_cold_` picks the arm from
    `fsm_state_` at entry. `on_inbound_not_connected_` writes `fsm_state::Active` only after its own `co_await`s
    (`ensure_hydrated_`, `seqnum_mgr_.check_inbound`, `run_reset_unit_`, `honor_peer_next_expected_`, the reply-Logon
    leaf). So two overlapping Logons would both take the NotConnected arm and both answer. The inbound advance
    (`persist_inbound_advance_`) likewise runs after the callback, across `co_await`s.
  - **The public header does not say so.** `on_inbound_frame`'s reentrancy contract in
    `include/fixpp/session/session.hpp` says "session-strand", nothing more.
- **The implementation adds the sentence to that contract.** A caller `co_await`s each `on_inbound_frame` to
  completion before it makes the next call; overlapping calls on one Session are outside the contract, and builds with
  assertions assert on the route this feature adds. A B-row discloses it (FR-009).
- **A violation is loud, mirroring the re-entry rule.** The re-entry rule is a debug `assert` macro at the site
  (`FIXPP_ASSERT_NO_CALLBACK_AT_PARSE_SITE`), whose text no other assertion contains, over a plain member kept in every
  build type (`callback_depth_`). This rule copies that form:
  - a member `bool inbound_reply_slot_held_`, set and cleared by the holder, in every build type;
  - `FIXPP_ASSERT_INBOUND_REPLY_SLOT_FREE()`, asserting it false where the holder takes the slot, with its own unique
    text so a death test can match it.

  Release builds compile it out, as they do the existing rule. The normal path keeps one ending.
  - The build condition is the existing rule's: a plain `assert`, so it fires wherever `NDEBUG` is not defined, and the
    death test's preprocessor condition is `NDEBUG` likewise. The lanes that check it are the presets whose build
    type leaves `NDEBUG` unset (read `CMakePresets.json`), recorded by the death test's row in
    `ci/expected-preset-conditional-tests.txt` (tasks T045).
  - In a release build a violation is not caught: two overlapping calls that both overflow share the slot, so one
    reply's bytes can be overwritten while its write is in flight, and a wrong or corrupted frame can reach the wire.
    B-row (h) states that consequence; nothing tests a release build under a violation.

**The capture invariant.** `static_assert(kSendFrameBytes <= CaptureVisitor::kCapBufSize)` sits in the slot-rebuild
helper's body. That relation is what makes every rebuilt frame capturable by the resend replay.
- It cannot be written at namespace scope. `kSendFrameBytes` is a private member of `Session`, and `CaptureVisitor`
  is in `session.cpp`'s anonymous namespace.
- So the helper, a `Session` member function, is defined in `session.cpp` after `CaptureVisitor`.
- **Replay growth (orchestrator ruling R8, implement).** The ceiling bounds what is built and stored. A resent frame
  grows by PossDupFlag(43), OrigSendingTime(122) and a restamped or inserted SendingTime(52), each timestamp at most the
  widest UTCTimestamp `utc_time_to_fix_string` writes (its nanos form); that growth must fit the replay rebuild
  buffer's headroom, `kRpBufSize` − `CaptureVisitor::kCapBufSize`, and `replay_outbound_range_` asserts it
  (`static_assert`, derived from those field widths). So a resent inbound-fed reply built at the ceiling may exceed
  `kSendFrameBytes` by that growth, as every resent message already may (a `Session::send` frame replays through the
  same path); the B-row states it (tasks T060).

**Allocation (`[const §XV.1]`): none added.**
- The slot is allocated once per Session, with the Session. `Session` grows by `kSendFrameBytes`, beside its three
  send-path arrays of that size.
- The rebuild is a non-coroutine.
- The tail runs in the site's existing coroutine.
- D9's allocation cells witness it, including a cell that must go RED with a coroutine-owned `kSendFrameBytes`
  buffer.

**Sites — every inbound-fed builder goes through it** (at round 1; the census recipe below decides): both `emit_session_reject_` overloads, `inbound_reject_leaf_`,
`inbound_reject_best_effort_leaf_` and `inbound_business_reject_leaf_`. Reachability with an unbounded inbound MsgType:
- `inbound_reject_leaf_`: `on_inbound_active_sending_time_reject_` (Reject 10/52, then Logout), PossDup Arms C and D and
  the malformed-122 arm (the session survives, so the loss is silent); all run on a validation-off session too;
- the zero-argument `emit_session_reject_`: `on_inbound_active_unsupported_` (no Application);
- `inbound_business_reject_leaf_`: `fromApp` rejects;
- the text overload: the validation gate and `dispose_unparseable_`;
- `inbound_reject_best_effort_leaf_`: only the fromAdmin-reject paths of SequenceReset and Logout, where the MsgType is
  admin; it goes through the helper for one rule and is witnessed by a unit cell.

**092's echo bound is retired.** `dispose_unparseable_` passes `fault_ref_msg_type` verbatim; `kMaxShippedMsgTypeLength`
and its recompute cell in `tests/session/session_reject_test.cpp` are deleted. Its premise ("the longest MsgType a
shipped dictionary defines") stopped being the population at 098, when an overlay may define a longer MsgType, so a
faulty frame with a fitting 3-byte MsgType draws a Reject without 372 today. **Supersedes 092 research R-5's
RefMsgType bound** (the bound and the sentence that the build cannot fail). 092's
`UnparseableFrameDisposition.RefMsgTypeBound_LongMsgType_RejectWithout372_{MalformedTag,LengthDataMismatch}` **flip**:
their MsgType overflows the disposer's buffer but fits `kSendFrameBytes`, so under the round-2 rule the Reject carries
it in 372 (D9 lists them). The omit-372 ending keeps its own witness, a MsgType whose Reject exceeds the ceiling (D9).

**Census recipe for the sites** (re-run at tasks and after the last fix commit):
`git grep -nE 'build_reject(_with_text)?\(|build_business_message_reject\(' -- src/session`. Every hit outside
`admin_messages.cpp` is the helper or carries a literal 372 (`apply_inbound_sequence_reset`'s `"4"`).
`git grep -n kMaxShippedMsgTypeLength -- src tests` returns nothing after the change.

**Residual (B-row, FR-009), restated at round 2.** `open()` bounds CompIDs for forbidden bytes, not for length. D3's 58
takes room too. At round 1, CompIDs long enough to overflow a gate Reject's own buffer lost the Reject even with a
one-byte MsgType. The slot rebuild now recovers that case up to `kSendFrameBytes`. What remains is a Reject whose
fixed fields, CompIDs and 58 exceed the ceiling even without 372. That is the outbound-ceiling ending above, the same
ceiling every outbound frame obeys. The session's other admin frames are built in smaller fixed buffers, so they fail
at shorter CompIDs than that (re-derive with `git grep -n 'std::array<std::byte, ' -- src/session/session.cpp`).
`apply_inbound_sequence_reset`'s literal-372 Reject, outside the helper, is part of this residual (above). A CompID
length bound at `open()` stays a follow-up issue (quickstart).

**Rationale:** QuickFIX C++ and J echo 372 in both messages with no frame bound. fixpp matches them up to its own
outbound ceiling, the one bound every frame it sends already obeys and the one its resend replay depends on. Above it:
- In Reject (35=3), 372 is optional (`Req'd N`). The Reject's reason and 45 are what the peer needs, so the Reject is
  sent without 372.
- In BusinessMessageReject (35=j), 372 is required (`required='Y'` in the vendored FIX 4.2, 4.4 and 5.0SP2
  dictionaries), so a BMR without it would be one a validating peer refuses. The BMR is therefore not sent, as no
  outbound frame above the ceiling is.

The work is on the failing path only and costs nothing on the normal path.

**Follow-up (b-L), for the owner.** Full parity echoes 372 up to the inbound limit L (`inbound_limit_for(cfg)`) instead
of up to `kSendFrameBytes`. It needs lifting the outbound ceiling the replay depends on: a stored frame larger than
`CaptureVisitor::kCapBufSize` aborts a later resend answer (`dispatch_aborted`). It also needs a buffer sized from L
and the CompIDs, plus re-parse and store support above the ceiling. That is a scope and cost question for the owner, so
it is a follow-up issue (quickstart), not part of this feature.

**Alternatives:**
- **Enlarge each site's own buffer.** It grows every reply leaf's frame on the normal path, past #544's recycler limit
  at `kSendFrameBytes`, and a smaller size still leaves a threshold of its own. Round 1's "a bigger buffer moves the
  threshold" holds against that form. The adopted slot rebuild moves the threshold to the engine's outbound ceiling,
  which every outbound frame shares, and leaves the normal path as it is.
- **A cold leaf coroutine that owns a `kSendFrameBytes` buffer**, shaped like `send_fallback_leaf_` (rewrite 2's first
  shape). Its frame is over asio's recycler limit (#544 §1(b)), so it allocates one frame per overflowed reply, on a
  path a peer can repeat. `[const §XV.1]` bans per-message heap on parse → validate → dispatch, and a reply to an
  inbound frame is on that path. #544 R-10's disposition for `send_fallback_leaf_` does not carry over: that fallback
  is caused by the engine's own call pattern (a send inside `toApp`, or one overlapping a suspended send), not by a
  peer. Rejected by orchestrator ruling.
- **Use the session-owned `send_frame_slot_`.** An in-flight `Session::send` may hold it.
- **One buffer shared with the strand's other chains** (#544 §2.2's rejected form). No per-chain exclusivity argument
  exists for it.
- **Send the BMR without 372.** A validating peer answers that BMR with Reject 373=1, 371=372, so the business
  information is lost anyway.
- **Keep the BMR's silent no-send below the ceiling.** It splits one condition at a leaf's private buffer.
- **Refuse the inbound frame.** It sends nothing, which is the defect itself.
- **Keep 092's bound and carve the disposer out of FR-007.** That gives one field two rules.

### D8 — Benchmarks (FR-008, SC-004), `[const §VIII.2]`

- **Protocol:** Article VIII §2's: a local same-runner run, A-B-A-B, compared **min-per-tree**, a slowdown above **+5%**
  fails. Base = the merge-base of the candidate and `origin/main` **at measurement time**, built in a worktree whose path
  has the candidate's length. Record the merge-base, four observations per tree, the minima and the ratios.
- **Binaries:** `scan_frame_header_bench`, `on_inbound_frame_bench` **and** `validator_bench`. `validator_bench`'s CI
  pairing is supplementary only (Article VIII §2: a green sentinel is not evidence of +5% compliance).
- **Table-view build and copy (Gate A round 2).** D1 adds work to exactly what `table_view_footprint_bench`'s existing
  timed rows measure: `Dictionary::build_table_view` populates, sorts and deduplicates the new store, and every
  `table_view` copy copies it. So its `BM_TableView_Build*` and `BM_TableView_Copy*` rows (list them with
  `--benchmark_list_tests`) join the same local A-B-A-B min-per-tree run, with the same +5% rule and the same Article
  VIII §2 approval fallback. `bench/ci-suite.txt` keeps the binary's `none:…` comparand and its `no` pairing: making
  it `paired` is irreversible (§2a) and out of scope.
- **Footprint:** `table_view_footprint_bench` gains the defined-tag store's bytes for FIX44, a FIXT route, FIX Latest and
  a high-tag overlay. No new timed rows: the existing Build/Copy rows cover the changed functions.
- **Expected cost:** one comparison per later 35 in the scan; one hot `valid_.find` per validated message, as today (D2
  shares it); one cold `tag_defined` only on a field that has already failed; nothing on a validation-off frame with one
  MsgType.
- **Which row measures which change (checklist audit).** The scan's comparison: `scan_frame_header_bench`. The MsgType
  check on the shared lookup and the tag split: `validator_bench`. The gate's work on a passing frame:
  `on_inbound_frame_bench`'s validated rows (`BM_Session_OnInboundFrame_InSequence_Validated`,
  `BM_Session_OnInboundFrame_Fixt_Validated`), which run a validating session. D1's fill, sort and copy:
  `table_view_footprint_bench`'s `BM_TableView_Build*` and `BM_TableView_Copy*`. No named bench feeds a frame with a
  repeated 35: the conflict path (the refusal, D5's walk) runs only on a refused frame, which is cold, so it is not
  measured; on a frame with one 35 the scan's added work is the branch that finds no later 35.
- **Footprint rows are report-only.** They carry no acceptance threshold; `[const §VIII.2]`'s budget is on time. The
  Build rows' cost from D1 (a fill linear in the field store, one sort and one dedup per build) is not predicted here:
  T055 measures it, and a slowdown above +5% fails unless it carries `[const §VIII.2]`'s approval by someone other
  than the author.
- **Re-run on a peer's merge.** When a peer's merge into `origin/main` moves a measured function before 101 merges
  (peer 100 edits `table_view.hpp` and `dictionary.cpp`), the paired runs are repeated at the new merge-base (tasks
  T078).

### D9 — Cells that change (SC-003): a recipe, plus the cells known at round 1

The census is **derived by command at the head being accepted and re-derived after the last fix commit**. Each manifest
row gives file::cell, today's ending, the new ending, the superseding FR (and the superseded clause, from the list
below), and for a tag cell whether the tag is dictionary-defined or wholly undefined. The manifest lives in `tasks.md`.

**Grep recipes** (each hit classified, not counted):
- `git grep -ln wire_unexpected_tag -- tests` — an undefined tag moves to `wire_undefined_tag`; a defined tag not in the
  MsgType stays; a hand-built table follows D1's population rule.
- `git grep -n "wire_length_data_mismatch) + 1\|static_cast<error>(134" -- tests` — the "last enumerator + 1 is unknown"
  sentinels (re-spell the second pattern to whatever value is one past the last slot at implement time).
- `git grep -ln rep35 -- tests` **and** `git grep -lE 'kNotConnectedOff|kLogonSentOff|kLogoutSent' -- tests` — the 099
  tables (the first grep misses `tests/session/test_099_establishment.cpp`'s W5 tables, so it is not enough alone).
- `git grep -n -i "last-wins\|LastWins" -- tests/session` — 092's scan pins.
- `is_valid_wire_error` in `tests/fuzz/fuzz_wire_validator.cpp` (it traps on any code outside its allowlist).
- the `kMaxShippedMsgTypeLength` hits (D7).
- `git grep -n fixt_header_repeats_msg_type -- tests` — the D5 walk's direct callers, whose report changes (D5); known
  positive: `W21PeekColdPath` in tests/session/test_099_peek_fixt_route.cpp.

**Mutate-and-run recipes** (grep is the wrong instrument where `58=` or a MsgType string matches nearly every Reject
cell). The cells live in many binaries (`session_pure_tests`, `wire_dict_tests`, the per-feature
`error_0NN_completeness` executables, `capi_pure_tests`, the interop executables, the fuzz harness). In `$M` (tasks.md
Execution rules) on a scratch commit at H (never a stash: it is shared by every worktree): build every test target
unmutated and record each test executable's hash. Apply the recipe's change alone with its 101-MUT- marker. Rebuild
every test target (-k 0; rc ≠ 0 voids the recipe). The affected binaries are the executables whose hash changed; each
must pass `strings … | command grep -c 101-MUT-<id>`, and a changed binary without the marker stops the recipe. Run
each affected binary whole and unfiltered under the mutant protocol's timeout. Where the ctest entry wraps a binary
(interop golden checks), run the entry's registered command from `ctest -N -V`. No ctest run here; the one full ctest
is T075. The RED set is the union of [  FAILED  ] names and must contain the recipe's known positive (recipe 1: the
locally reachable HP-QFj-*-reject-invalid-admin golden checks), or the recipe is broken. Recipe 1's positive is also
W101-12's cells, which the D3 change always turns RED (T024); when no golden check can run locally (a counterparty
binary absent), W101-12 is the positive alone, the absence is recorded, and the goldens are diffed only at T052. The
RED set is the census for that change:
1. D3 alone (or a mutant passing a fixed non-empty text at the three gate sites) → the cells pinning no 58, plus the
   `HP-QFj-*-reject-invalid-admin` goldens.
2. D4 alone (first-wins scan + `interpret_logon` + LogoutSent) → the 092 and 099 cells pinning last-wins endings.
3. D6 alone (conflict clause without the `validate` term) → the validation-off establishment cells.
4. D2 alone → the validator and session cells pinning 373=2 for an undefined tag or an undefined MsgType.
5. Two placeholder enumerators appended to `core::error` → the RED set must contain the sentinel grep's hits (its
   known positive), as every other recipe's must contain its own; every other RED name (a completeness gate or size
   pin the placeholders' switch cases trip, such as `kEnumTable`, `expected_error_map.csv` or `error_surface_test`'s
   size pins) is classified in the manifest's §A, and an empty RED set means the recipe is broken. `-Werror`
   stays on (never disabled for a mutant). The change includes a case for each placeholder in every total switch the
   build names: build the mutant once, read its `-Wswitch` errors, and map each placeholder exactly as its neighbouring
   existing enumerator is mapped, so the mutant compiles. The recipe states that procedure, not a list of switches;
   rc ≠ 0 still voids it.

**Known at round 1, not exhaustive** (the recipes above decide; a cell missing here is not exempt):
- 098: `DialectOverlayInboundBase098.AS3_*` (→ 11, no 371, 58) and `AS1`/`AS2` (undefined tags → 0, 58).
- `UnparseableFrameDisposition.LateSite_Control_BelowCeiling_ValidateGate_ParsedAndValidated` (undefined tag → 0, 58).
- `ValidateGateInbound.ManyFieldsBypassArena_Rejected_NotBypassed` (undefined tags → 0).
- Wire: `ValidatorProductionTableView.UnknownMsgTypeRejectsLikePreHoistFieldValidFor` (→ `wire_invalid_msg_type`),
  `ValidatorTypeCheck.ValidateUnexpectedTagReturnsError`, `ValidatorPerVersion.UnexpectedTagRejected`,
  `ValidatorDomain.UnexpectedTagRejected`, `W3Unit.TagSDoesNotDeclare_TheRouteAnswers` — each classified by D1.
- W-014 (`tests/wire/conformance/w014_validate.csv`, judged by `make_heartbeat_grammar`, which names `"0"` only
  through `add_required`): row `unexpected-tag-42` has one new ending, `wire_undefined_tag`, 371=9999 (no operation
  in that grammar names 9999). Rows `conforming-heartbeat-42`, `conforming-heartbeat-50sp2` and
  `conforming-heartbeat-t11` stay accept: they are the existing guard that `add_required` defines a MsgType (D2).
- Fuzz: `is_valid_wire_error` gains both new slots.
- Error slots: the sentinels in `test_017_error_completeness`, `test_019_error_completeness`,
  `test_020_error_completeness` and `AbiSymbolGolden.ErrorEnumUnchanged` move to one past `wire_undefined_tag`.
  `test_092_error_completeness` keeps its two 092-slot checks; its "nothing added beyond the 092 block" check moves into
  a new `test_101_error_completeness.cpp`, which pins the two new slots, their messages, their `translate` targets and
  the slot after them as unknown. `kEnumTable` and `expected_error_map.csv` gain both rows, and `error_surface_test`'s
  size pins follow. `core::error_message`/`to_string`, `capi::translate` and `kEnumTable` gain both cases.
- 092 scan pins: `ScanFrameHeaderFault.CleanFrame_Duplicate35_KeepsLastWins` (→ first 35);
  `expect_clean_and_agree`'s `last_35` oracle; `expect_planted_fault`'s per-tag last-wins loop for tag 35;
  `MsgTypeIsThird_FalseWhenField3IsNot35`'s message. `msg_seq_num` keeps last-wins, and
  `UnparseableFrameDisposition.Dup34_FaultFree_LastWins_Pin` stays.
- 099 validating: `kLogoutSent` rows `LO-a` `rep35 D..5 past` (→ stays) and `rep35 5..D past` (→ `Disc`).
- 099 validation off: `kLogoutSentOff` (the same two rows flip); `kNotConnectedOff` `NC-a` and `kLogonSentOff` `LS-a`
  rows `rep35 D..A logon-fields` / `rep35 0..A logon-fields` (→ `Disc`), in `test_099_establishment_admission.cpp`;
  `W6PerShape.DThenAWithLogonFields_Establishes` (→ disconnect); the `rep35` rows of `test_099_validation_off.cpp`
  (→ the differential oracle of D6). `test_099_establishment.cpp`'s W5 off tables are checked by recipe 2/3.
- 099: tag 3115 cells in `test_099_toml_live.cpp` / `capi_099_fixt_live_test.cpp` (3115 defined only in FIX Latest).
- 092 echo: `UnparseableFrameDisposition.RefMsgTypeBound_LongMsgType_RejectWithout372_{MalformedTag,LengthDataMismatch}`
  (→ 372 present and byte-equal to the inbound MsgType, which overflows the disposer's buffer but fits
  `kSendFrameBytes`; renamed, citing FR-007).
- Interop goldens (58 added, 9 and 10 move; regenerated, never hand-edited): the library's
  `tests/interop/happy/golden/HP-QFj-{acc,init}-fix44-reject-invalid-admin.fix` and their `-capture.fix` twins, and
  the PARENT's `research/G19-fix-fpml-iso20022/phase-9-harness/golden/HP-QF{j,cpp}-{acc,init}-fix44-reject-invalid-admin.fix`
  copies. Tasks T052 regenerates them; the parent copies land in the post-merge parent commit, T080.

Each rewritten cell names its superseding requirement in the test.

**New witnesses owed** (each RED on `fa778a8e` where today's behaviour differs, and RED with its production change
reverted):
- D4 sticky: `D…A…D` through Active (validating: a last-vs-third mutant reads no conflict; must go RED), and
  `A…D…A` carrying Logon fields through both establishment roles on a non-FIXT and on a validation-off session (the
  last-vs-third mutant establishes; D4 disconnects). `D…A…D` alone does not discriminate in establishment.
- D5: `0,D(hdr)…0(past)` → 14 and `0,0(hdr)…D(past)` → 14; validating non-FIXT 13 and 14; a FIXT-only header tag at the
  non-FIXT boundary (1128 before a later 35 → 13, as QuickFIX C++).
- D5's hooks and carry (real FIXT `Session`, validating, Active; Gate A round 2, re-aimed by the 2026-11-07 owner
  ruling, D5):
  - a Length only the route declares (no shipped FIX50SP2 or FIX Latest dictionary has such a body pair, by D5's re-derive
    recipe, so the routed dictionary is 099's `fix50sp2_with_body_pair` overlay; orchestrator ruling S4), as the first body field, whose Data
    embeds `SOH 35=X`, plus a real in-header repeat → 13, not 14. A mutant whose call site passes S's hooks
    (`session_hooks(inbound_tv_)`) must go RED;
  - an S header Length whose paired Data tag is outside S's header union, so the Data is the stopping field, built
    through a `Dictionary` and the route set: through a FIXT11 overlay that puts the Length alone in `<header>`, the Data in a message, and declares the pair adjacent in `<fields>` (the mirror of `W17HeaderLengthPairs.OnlyTheDataTagInTheHeader_IsAHeaderPair`), so `dict::header_length_pairs` merges it into every route. The Data embeds `SOH 35=X` → the Data counted across
    the stop, and the 13/14 answer as the frame's real 35s give it. A mutant that resets the carry when `past_stop`
    latches must go RED. If that dictionary cannot be loaded through `fixt_route_set::make`, record why; (b) then becomes
    a unit cell at T040, on `fixt_header_repeats_msg_type` with that S table and route hooks that carry the pair. Its
    RED source is M101-3, named RED 'a past-header 35 reported';
  - the residual the ruling closes: a custom S that pairs a non-standard Length on a tag of
    `dict::is_fixt_standard_header_tag` its own `<header>` omits, with a Data tag outside S's header union and a Data
    value embedding `SOH 35=X`, which the route's hooks split, plus a real in-header repeat → 14 (the split puts the
    embedded 35 past the stop), not 13, when every piece of the split Data is a well-formed field (e.g. `1=a<SOH>35=X<SOH>2=b`); otherwise the scan faults and the 092 disposer takes the frame. A mutant that reads the
    header-classified fields with S's hooks (Gate A's rule) must go RED. Through a real `Session` if such an S loads
    through `fixt_route_set::make`; else a unit cell on `fixt_header_repeats_msg_type` with S's table and a route
    table lacking the pair, written with the new walk (tasks T040). The mutant is an edit inside the walk either way.
- D5's fault rule (a unit cell on `fixt_header_repeats_msg_type`; a real `Session` cannot reach it, D5): a real
  in-header repeat before a malformed count → the walk reports neither an in-header nor a past-header 35, so the
  answer is 14. A mutant that keeps the 35s a faulting walk saw must go RED.
- LogoutSent on validating and off, FIXT and non-FIXT: `D..5` stays, `5..D` confirms.
- D6's differential oracle for every post-establishment validation-off row and every LogoutSent row, including X = 4
  and X = A.
- Controls: a single MsgType in every affected arm; a same-value repeat; the 3/5 exemption ordering on validation off;
  the outbound Logon masking; the resend replay.
- `interpret_logon` direct cell: `35=D … 35=A` → `session_invalid_logon`.
- D7. **Thresholds are derived at test time, never written as a MsgType length.** Call the site's builder, with the
  session's CompIDs, SendingTime and (for a gate Reject) its 58, into a buffer of the site's size and into one of
  `kSendFrameBytes`. The longest MsgType for which each build succeeds is that boundary. The derivation uses the exact
  values the frame under test will carry: the MsgSeqNum (34) the session will stamp and the 45 it will echo, besides
  the CompIDs, SendingTime and 58, because their digit counts move the boundary; so the one-byte controls are exact.
  - **Reject sites:** a long undefined MsgType through the validation gate; `inbound_reject_leaf_` via PossDup Arm C
    on validation off (the session survives); the zero-argument emit (no Application); a unit cell for
    `inbound_reject_best_effort_leaf_`. Each has three cells:
    - a MsgType that overflows the site's buffer but fits the ceiling → 372 present, byte-equal to the inbound one,
      every other field as for a short MsgType;
    - a MsgType whose Reject exceeds the ceiling → the Reject sent without 372;
    - boundary controls one byte under and one byte over the site's buffer, and one byte under and one byte over the
      ceiling.
  - **The BMR leaf** (validation-off acceptor; `fromApp` rejects):
    - a MsgType that overflows the leaf's buffer but fits the ceiling → the BMR delivered with 372 byte-equal to the
      inbound MsgType. It must be RED on `fa778a8e` (no BMR there) and RED with the slot rebuild removed;
    - a ResendRequest covering that stored BMR, and one covering a BMR one byte under the ceiling → resent with 372;
      not a disconnect, not a GapFill;
    - a MsgType whose BMR exceeds the ceiling → no BMR, no `toApp`, no outbound number consumed, the inbound consumed,
      the session Active, and the log record, captured through `SessionConfig::logger_override`. The no-BMR half is
      GREEN on `fa778a8e`, which sends none; the record is the discriminating assertion. A second such message inside
      the rate window writes no second record, and the next record carries the count;
    - boundary controls one byte under and one byte over the leaf's buffer and the ceiling.
  - **Callbacks at the ceiling:** a Reject one byte under the ceiling reaches `toAdmin`, and a BMR one byte under it
    reaches `toApp`, each with 372 byte-equal. A built-frame parse failure skips the callback silently (D7), so this is
    what shows the rebuilt frame parses.
  - **Long CompIDs:** CompIDs and 58 that overflow a gate Reject's buffer with a one-byte MsgType → the Reject sent,
    with 372. It must be RED with the slot rebuild removed. The session's own admin frames are built in smaller
    buffers, so drive the cell in a gate arm such CompIDs can reach, found by running it.
  - **Placement:** in every overflow cell, one outbound number per sent message and one callback. A mutant that
    re-peeks or re-stamps in the slot rebuild must go RED.
  - **Allocation (the zero, measured).** New cells in #544's run-thread rig (`tests/alloc_guard/test_544_run_thread_windows.cpp`
    over `tests/support/run_thread_engine_rig.hpp`). That rig drives the real Engine read pump and counts allocations
    two ways: the mallocnesia interceptor (Release, Linux, one gtest case per registration) and the operator-new counter
    build (`FIXPP_B35_TU_COUNTER`). It is the closest existing harness that runs a Session's inbound path. #544's own
    windows take no reply, so these cells are the first to gate a reply chain.
    - A W-B-shaped window whose application rejects in `fromApp`, with a MsgType that overflows the BMR leaf's buffer
      (each BMR rebuilt in the slot). Its twin uses a MsgType that fits.
    - A Reject window: a validating rig and a long undefined MsgType, so each gate Reject is rebuilt in the slot. Its
      twin uses a short undefined MsgType.
    - Each asserts zero interceptions and a zero counter, plus its post-conditions: one reply per window frame, 372
      byte-equal, the session still Active with the expected NextNumIn.
    - **RED arm:** a mutant restoring rewrite 2's first shape, a coroutine that owns a `kSendFrameBytes` buffer, must
      be intercepted, as #544's arm (e) is for `send_fallback_leaf_`. That arm is the mallocnesia registration's only:
      an over-limit coroutine frame is a class-(b) request (`aligned_alloc`, `_aligned_malloc` on MSVC), which the
      `operator new` counter cannot see on any platform (#544 §3, "Why MSVC is class (a) only"). So the counter build is
      class-(a) evidence only, and on MSVC, where mallocnesia is not registered, W101-45 has no class-(b) evidence, as
      #544's own windows have none there (#544 §3).
    - A zero counts only after that registration's positive has read non-zero on the same build: M101-5 intercepted for
      the mallocnesia registration, and #544's arm (a) (the configuration arm whose TU counter must read > 0) for the
      counter build.
    - If a **fitting** twin is not zero, the cell is not weakened. #544 leaves reply chains ungated (§2.2 "The leaf's
      own frame", §3), so that would be a pre-existing gap: tasks stop and report it. An unweakened cell that reads
      non-zero stays RED, so the feature does not merge until the orchestrator rules (with the owner): fix the gap in
      101, or file it as an issue and waive the cell with a recorded reason.
    - `ctest -L alloc_guard` stays GREEN as a regression control.
  - **Slot discipline:**
    - A death test, on a build with assertions, drives two overlapping `on_inbound_frame` calls on one Session. Each
      draws an overflowed reply, with a store double that suspends the first call's store. It expects
      `FIXPP_ASSERT_INBOUND_REPLY_SLOT_FREE()`'s text. A mutant with the assertion removed must go RED. This test is the
      one sanctioned out-of-contract call of `on_inbound_frame`; nothing tests a release build under a violation (the
      assertion is compiled out there; B-row (h) states the consequence).
    - Release on every exit: an overflowed BMR whose `toApp` vetoes, and one whose `toApp` throws; an overflowed
      Reject whose `assign_outbound` fails. After each exit, `session_test_access` reads `inbound_reply_slot_held_`
      as false. After the veto, whose session stays Active, a second overflowed BMR in the next frame is sent. A
      mutant that releases only on success must go RED.
    - The users census: every caller of the slot rebuild is an inbound-fed reply site (read each caller's enclosing
      function at tasks).
  - **092 disposer:** a fault with a fitting 3-byte overlay MsgType → 372 present (none on `fa778a8e`).
  - **`35=D=X`:** echoed as `372=D=X` and read back by fixpp's parser as one field.
- D1: one mutation per population path (dictionary-built; each hand-built op class).
- D2's MsgType definition on a hand-built table (Gate A round 3):
  - required-only: a table that names `"X"` only through `add_required("X", …)`; an `"X"` frame carrying only those
    tags validates OK, not `wire_invalid_msg_type`;
  - ctx-only: a table that names `"Y"` only through `table_view_builder::add_group_member_ctx("Y", …)`; a `"Y"` frame
    returns `wire_invalid_msg_type` with no ref tag;
  - mutant: drop the `valid_` insert from `add_required_tag` only. The required-only cell and the W-014
    `conforming-heartbeat-*` rows must go RED.

### D10 — Comments the feature makes false

These state results D2/D4/D7 make false (the comment rule: a comment records a procedure, not a result). Tasks list them
and `check-comment-claims.py` runs on the diff:
- `refuse_active_frame_`'s header ("because hdr is last-wins on 35");
- the NotConnected arm ("interpret_logon reads the last 35, so it could not refuse `D…A`") and 099's data-model E-7 S1
  wording it mirrors;
- the 099 comments in the Active gate;
- `FrameHeader`'s "The members above keep last-wins";
- `session.hpp`'s `names_third_field` rationale;
- `validator.hpp`'s Step 1 hoist comment ("an unknown msg_type yields a view whose `contains()` is false …"), unreachable
  behind the MsgType check;
- `reject_reason_map.hpp`'s `wire_invalid_tag_number` comment, which must not suggest it serves the undefined tag;
- `dispose_unparseable_`'s RefMsgType-bound comment;
- `emit_session_reject_`'s header ("success or build-failure (not fatal if build fails)"), and any other reply-leaf
  comment describing what a failed build does, where the slot rebuild now runs first;
- `on_inbound_frame`'s reentrancy contract in `session.hpp`, which gains the serialized-caller sentence (D7);
- `fixt_header_repeats_msg_type`'s comment ("by the same walk and the same stop"), and `peek_fixt_route.hpp`'s
  header ("The walk uses S's hooks"), which after D5 holds for `peek_fixt_route`'s walk only;
- the 098 test file's header recipe (B85 note);
- the pre-fix note above `FixtHeaderValidateTest.WellFormedApplicationFrameAccepted` in
  `tests/wire/fixt_header_validate_test.cpp` ("rejected wire_unexpected_tag, ref_tag==8 … identical 'empty valid-tag
  view -> first field is 8' proof"), which cites `ValidatorProductionTableView.UnknownMsgTypeRejectsLikePreHoistFieldValidFor`
  as the mechanism D2 replaces.

Each file whose decision this feature supersedes names the superseding 101 decision in a header comment (CLAUDE.md,
#310); tasks T066 applies this list:
- `scan_frame_header.hpp`'s header names **101 D4** as superseding 092 data-model E-1 / contract C-3 I-3 for
  `msg_type`;
- `session.cpp`'s `dispose_unparseable_` names **101 D7** as superseding 092 research R-5's bound;
- `session.cpp`'s `refuse_active_frame_` names **101 D4/D5** as superseding 099 FR-025/FR-029's comparison and 13/14
  rule;
- `peek_fixt_route.hpp` names **101 D5** for `fixt_header_repeats_msg_type`;
- `admin_messages.cpp`'s `interpret_logon` names **101 D4**;
- `validator.hpp` names **101 D2** for the step order (the MsgType check, the undefined-tag split).

## Supersessions (by name)

This feature changes clauses of merged features. Each is superseded for the part named, nothing more. The list is a
**recipe plus its classified hits** (Gate A round 2), as D9's census is. It is re-run at tasks and after the last fix
commit, and every hit is classified as superseded (a row below) or kept (with its reason). It is not counted.

One line per decision. Each hit is classified, not counted.

```sh
# D4/D5/D6/D7 — MsgType comparison, the 13/14 rule, the 372 bound (the round-2 pattern, plus first-wins and two paths: 098, brain)
git grep -nE 'last MsgType|last 35|keeps? (the|its) (first-wins, last-wins )?(MsgType )?split|keeps both|kMaxShippedMsgTypeLength|longest MsgType|shipped dictionary|second 35|(D|0)(…|\.\.)A|(D|5)(…|\.\.)(5|D)|leaves it false|gains no step|last-wins|first-wins' \
  -- specs/092-garbled-frame-reject specs/098-dialect-overlay specs/099-fixt-appl-version-validation spec/behaviors-and-limitations.md brain/components
# D2 / 373 — an undefined tag moves 2 → 0. For each hit that names a tag, check whether the judging table's
# dictionary defines it: ``number=['"]N['"]`` per dictionaries/*.xml (prove the regex on 370 → FIX42, FIX43),
# the frozen FIXT11 at tests/fuzz/corpus/dict_xml_loader/seed_fixt11_real_dictionary.xml for an operator's copy,
# `<fixr:field id=…>` for Orchestra. Undefined → superseded to 373=0; defined → kept.
# A hit whose judged MsgType the judging table does not define (R 2/8, 371=8) → superseded to 373=11, no 371 (FR-001);
# e.g. 099 data-model MT-f ("FIX Latest does not declare ABCDEFGH") and the MT-f cells in tests/session/test_099_msg_type_default.cpp.
git grep -nE '373 ?= ?2\b|R 2/|\b2 unexpected|unexpected[- ]tag|wire_unexpected_tag|Rejects?/2\b|reason 2\b|SessionRejectReason\(373\) ?= ?2\b' \
  -- specs/092-garbled-frame-reject specs/098-dialect-overlay specs/099-fixt-appl-version-validation spec/behaviors-and-limitations.md brain/components
# D2 / 371 — an undefined MsgType carries no 371. Only a universal claim ("now carry", "names the field") is superseded.
git grep -nE 'RefTagID|\b371\b' -- spec/behaviors-and-limitations.md brain/components
```

The vendored XML quotes attributes with single quotes, so a `number="N"` grep reports nothing for every tag; the
quote-agnostic form above is the one to use. The patterns were widened at round 2 until every clause both reviewers
named is a hit, and at round 3 by the D2 and 371 lines, which the round-2 vocabulary could not reach. A clause the
recipe still misses, once found, widens the recipe; a hand addition to the table alone does not close it.

**A hit that fits nothing (checklist audit).** The task whose run finds it (T003 before Phase 2 ends, T064 after the
records, T068 after the last fix commit) classifies it: a row below when a decision of this feature changes the
clause, else a kept class with its reason. A hit whose clause would change in a way D1 to D7 do not reach stops that
task for the orchestrator.

**The path from a merged clause to its supersession.** Merged bundles are history and are not edited. A later reader
gets from a superseded clause to 101 through two pointers only: the in-place flag on the brain component page
(CLAUDE.md, #334) and the header comment naming the 101 decision in the source file (D10, CLAUDE.md, #310). The live
B&L rows carry the operator-facing ending.

**The live B&L file and `R 2/8`.** The D2 line's MsgType rule applies to the live file too: a live row that states
373=2 with 371=8 as the current ending of an undefined MsgType is superseded to 373=11 with no 371 (FR-001). A row that
records the behaviour before its own feature (B-081-1's "previously rejected on tag 8") is history and is kept. The
check: `command grep -nE '371 ?= ?8\b|R 2/8|RefTagID\(371\) ?= ?8|tag 8' spec/behaviors-and-limitations.md` (known
positive: B-081-1), re-run at T064.

**Kept, by class:**
- 092's, 098's and 099's `tasks.md`, `plan.md`, `research.md`, `quickstart.md` and `checklists/` are history. They
  record what each feature measured and planned when it merged, and are not rewritten (099's quickstart
  issues-to-file list is the plan that filed #657/#658, which this feature closes). 098 `spec.md`'s out-of-scope entry
  "fixpp's single `wire_unexpected_tag` against QuickFIX's 373=0 … / 373=2 … distinction (filed)" is the same class:
  it records the filing of the divergence D2 closes.
- A merged spec's description of behaviour before that feature (its Context "today" table), e.g. 099 `spec.md`'s
  Context table and 099 `data-model.md`'s "T rejects every application frame … on tag 8, 373=2".
- Clauses that pass `fault_ref_msg_type` rather than the scan's last-wins `msg_type` (092 `spec.md` FR-003's 45/372
  source, 092 contract C-2's "35 value"). The rule they state, "the third field's value", is the one D4 makes
  universal, so they stay.
- Unrelated hits (e.g. B-493-1's "keeps both … caps", the 083 splitter note).
- 099's mutant rows (`witnesses.md` M-50, M-51, M-59, M-109, M-93) describe 099's mutants. They stay; where a mutated
  line no longer exists after D4, tasks re-target or retire the mutant with its witness.
- Hits on the D2 line whose tag the judging table's dictionary defines (373=2 stays), and the outbound builder rows
  (B-091's `wire_unexpected_tag` from `body_builder`, 098 `spec.md`'s "Sending a custom Data value" edge case), which
  are not the validator. Hits on the 371 line that state a per-site 371 (B-009-3, B-021-1's OrigSendingTime arms,
  B-021-2, B-075-1, B-075-2, B-509-2, B-038-1, B-092-3's fault Reject, B-097-1, B-099-*, B-420-1, B-422-1), not a universal one.
- Every 099 `witnesses.md` M-row on the D2 line describes a 099 mutant, as the class above already says.
- B-092-8 (the fault results), which D2 does not change.
- Hits that are not a MsgType ending: B-099-22 and 099 `contracts/surface.md`'s "Two 1128s in the header read
  first-wins" (1128, unaffected), 099 `data-model.md`'s `scan_logon_msg_type_defaults`, and the "keeps the split"
  hits in `brain/components/session.md` and `brain/components/message-store-quiescence.md` (pool-thread ordering).
- SecondBrain history and rejected alternatives: `brain/components/wire.md`'s "unexpected tag 8" (a history of T062a)
  and its "shipped dictionary" lines (FR-018's drift arms); `brain/components/inbound-message-path.md`'s rejected
  "making the whole scan first-wins", which stays true because D4 makes `msg_type` alone first-wins.
  `brain/components/session.md`'s "`interpret_logon` reads the last 35" lines are superseded clauses, flagged in place
  by the SecondBrain edit below.

**Superseded:**

| Superseded clause | Part superseded | By |
|---|---|---|
| 092 `contracts/unparseable-frame-disposition.md` C-3 **I-3** ("the scan keeps last-wins for every existing member (E-1)") | `msg_type` only; `msg_seq_num` stays last-wins | D4, FR-004 |
| 092 `data-model.md` **E-1** ("`msg_seq_num` and `msg_type` stay last-wins") | `msg_type` only | D4, FR-004 |
| 092 `research.md` **R-5**, the RefMsgType bound (`kMaxShippedMsgTypeLength`) | the bound and its "cannot fail" premise | D7, FR-007 |
| 092 `spec.md` **FR-003**, the RefMsgType sub-bullet ("omitted when longer than any MsgType a shipped dictionary defines") | the omission rule | D7, FR-007 |
| 092 `contracts/unparseable-frame-disposition.md` **"Reject contents"**, the 372 bullet ("omitted when it is longer than the longest MsgType any shipped dictionary defines … cannot overflow the Reject's 512-byte buffer") | the omission rule and its no-overflow premise | D7, FR-007 |
| 099 `spec.md` **FR-025**: "whose last MsgType differs from its third field's"; "373=13 when the second 35 is in the header, 373=14 when past it"; the **Limits** sentences "a last MsgType of 5 confirms the Logout" and "A non-validating session and a non-FIXT session keep the split" | the comparison (now any later differing 35), the 13/14 rule (D5's precedence), LogoutSent's reply test, the non-FIXT and validation-off carve-outs | D4, D5, D6; FR-004..FR-006 |
| 099 `spec.md` **FR-029**: "whose last MsgType differs from its third field's"; "a validating FIXT session" for that clause | the comparison and its scope (every session, validating or not) | D5, D6; FR-005, FR-006 |
| 099 `spec.md` **Clarifications**: the Gate A round-1 Q on a last MsgType that differs ("A non-validating session stays as today"), the round-2 Qs on establishment ("`interpret_logon` reads the last 35"), on LogoutSent ("a frame whose last MsgType is 5 confirms the Logout"), and the after-loop-1 Q "Does a validating FIXT session reject a frame whose last MsgType differs" | the comparison, the non-validating carve-out, the `interpret_logon` rationale, LogoutSent's reply test | D4, D5, D6 |
| 099 `spec.md` **US3** acceptance scenarios 7 ("last MsgType differs", 13 "with the second 35 in the header") and 8 (validating FIXT establishment, "last MsgType differs") | the comparison, the 13/14 rule, scenario 8's validating-FIXT scope for the conflict clause | D4, D5, D6 |
| 099 `spec.md` **Edge Cases**, "MsgType repeated with a different value": "A non-FIXT validating session keeps the split …" | that sentence | D5, FR-005 |
| 099 `data-model.md` **E-7**: steps **S1** and **A1**; the non-validating paragraph ("`D…A+L` is scanned with S and interpreted with the transport's table", as a path to establishment); "A non-FIXT session leaves it [`names_third_field`] false"; "LogoutSent gains no step: it drains every frame as today"; the decisive-cells list's "`D…A+L` and `0…A+L` establish", "`D…3` in Active is consumed in silence. `D…5` logs the session out"; the per-arm table's `LO` row "`D…5`: … as today"; the "Columns" note "last 35=0" naming | S1's condition and its "interpret_logon reads the last 35" rationale; A1's condition and 13/14 rule; the validation-off establishment path; the non-FIXT flag (D5 sets it there); LogoutSent's reply test (D4); the validation-off Active endings (D6's oracle) | D4, D5, D6 |
| 099 `contracts/surface.md`, the validation-off bullet ("establishes a session of either role, because `interpret_logon` reads the last 35 … LogoutSent takes a last MsgType of 5 as the reply") and the bullet "A non-FIXT validating session keeps the first-wins, last-wins MsgType split" | both bullets | D4, D5, D6 |
| 099 `contracts/witnesses.md` **W-6**'s "**`D…A` with Logon fields establishes,** pinned as disclosed" | that clause | D6, FR-006 |
| 099 `contracts/witnesses.md` **W-9**: the named cells "`D…A` and `0…A` with Logon fields: Disconnected, not Active" (kept on a validating FIXT session, widened to every session) and "`D…5` in LogoutSent: confirms the Logout"; its walk of `LO-a` | the `D…5` cell, `LO-a`'s repeated-35 inputs, the scope of the establishment cells | D4, D6 |
| 099 test tables: `kLogoutSent`/`kLogoutSentOff` `LO-a` `D..5`/`5..D`; `kNotConnectedOff` `NC-a` and `kLogonSentOff` `LS-a` `logon-fields` rows; `W6PerShape.DThenAWithLogonFields_Establishes` | their expected endings | D4, D6 |
| B&L **B-092-3**, the RefMsgType(372) bullet (`kMaxShippedMsgTypeLength`, its recompute cell, "cannot overflow") | the whole bullet, rewritten to D7's rule | D7, FR-009 |
| B&L **B-099-3**, "last MsgType differing from it", "a validating FIXT session" for the conflict clause | the comparison and its scope | D4, D6, FR-009 |
| B&L **B-099-4**: its headline ("… once a validating FIXT session is past establishment; a non-validating session keeps both"), its "Repeated MsgType" bullet ("373=13 when the second 35 is in the header"), its "Not validating" bullet | the headline's scope, the 13/14 rule, the whole "Not validating" bullet | D4, D5, D6, FR-009 |
| B&L **B-099-5**, **B-099-11** | each whole row, rewritten to the new endings | FR-009 |
| B&L **L-452-2** | closed: the echo stays verbatim by ruling, and the silent loss is fixed | D7, FR-009 |
| B&L **B-092-1**, "the scan keeps last-wins for the fields it already read" | `msg_type` only; MsgSeqNum(34) stays last-wins | D4, FR-004, FR-009 |
| B&L **B-075-6**, its headline and "a strict-validating session's Reject now names the field that failed"; the 075 section's intro sentence "a validate-driven `Reject(35=3)` now carries `RefTagID(371)`" | the universal 371 claim | D2, FR-001, FR-009 |
| B&L **B-041-1**, its `SessionRejectReason ∈ {…}` enumeration | the enumeration, replaced by a pointer to `wire_error_to_session_reject_reason` | D2, FR-001, FR-002, FR-009 |
| B&L **B-099-2**, Double-fault limit, "an undeclared tag is 373=2" | 373=2 → 373=0 for a tag the transport dictionary does not define | D2, FR-002, FR-009 |
| B&L **B-099-8**, "373=2, 371=370 on any other" | 373=0 where the route's dictionary does not define 370 | D2, FR-002, FR-009 |
| B&L **B-099-25**, "`R 2/384 A`" | `R 0/384 A` for a dictionary that does not define 384 | D2, FR-002, FR-009 |
| B&L **L-099-1**, "Such a trailer tag draws 373=2" | 373=0 where the answering table does not define it | D2, FR-002, FR-009 |
| 099 `spec.md` **FR-010** Limits, `data-model.md` **E-7** `NC L` 5999 cell and §FIXT11's `R 2/384 A`, and every other merged-spec clause on the D2 recipe line whose tag the judging table does not define (the per-hit list: tasks.md `## Census at tasks time`, then T064's run in the evidence file) | 373=2 → 373=0 | D2, FR-002 |

**B&L row rewrites (close-out row 3).** Each uses the file's "*(Amended <date>, 101:)*" convention and carries no
count. The rows rewritten to the new endings with no planned text here (B-092-3's 372 bullet, B-099-3, B-099-4, B-099-5,
B-099-11) take their text from contracts/reject-frames.md's rows ("Repeated MsgType", "RefMsgType echo") and D4 to D7,
and each rewritten clause is checked against that contract row. B-075-6's amendment states 371 and 373 only; the new
row that every validation-gate Reject carries 58 (FR-009, tasks T060 (k)) owns that statement, and B-075-6 does not
repeat it.
- **B-092-1:** replace "the scan keeps last-wins for the fields it already read." with:
  > the scan keeps last-wins for the fields it already read, MsgSeqNum(34) among them. *(Amended <date>, 101:)*
  > MsgType(35) is first-wins: `FrameHeader::msg_type` is the first 35, and a later 35 with a different value sets
  > `msg_type_conflict` (B-101-n).
- **B-075-6:**
  - Headline becomes: "Reject(35=3) frames driven by dictionary validation now carry `RefTagID(371)` when the failure
    has a field — including the pre-existing type-arm rejects, for free."
  - Append: "*(Amended <date>, 101:)* An undefined MsgType draws 373=11 with no 371 (`wire_invalid_msg_type`,
    B-101-n), as a malformed tag draws 373=0 with none (B-092-8). An undefined tag draws 373=0 with 371 = the tag."
  - The 075 section's intro sentence gains "when the failure has a field (B-075-6)".
- **B-041-1:** replace the `SessionRejectReason ∈ {…}` enumeration with "the `SessionRejectReason` that
  `wire_error_to_session_reject_reason` maps the validator's result to" and append "*(Amended <date>, 101:)* an
  undefined tag answers 0 and an undefined MsgType 11 (B-101-n); 2 is a tag defined but not for this message." The
  pointer does not rot; `brain/components/wire.md`'s session-layer seam withholds the mapping values for that reason.
- **B-099-2**, Double-fault limit: replace "An absent 1137 with an undeclared tag is 373=2 naming that tag" with:
  > An absent 1137 with a tag the transport dictionary does not define is 373=0 naming that tag (373=2 for one it
  > defines elsewhere) *(amended <date>, 101)*
- **B-099-8:** replace "373=2, 371=370 on any other" with:
  > 373=0, 371=370 on a route whose dictionary does not define 370 (373=2 on one that defines it; which dictionaries
  > define 370 is read from the dictionary, not listed here) *(amended <date>, 101)*
- **B-099-25:** replace "answers such a Logon `R 2/384 A`" with:
  > answers such a Logon `R 0/384 A` when it does not define 384, as QuickFIX C++'s copy does not, and `R 2/384 A` when
  > it defines 384 without the group *(amended <date>, 101)*
- **L-099-1:** replace "Such a trailer tag draws 373=2." with:
  > Such a trailer tag draws 373=0 when the answering table's dictionary does not define it, 373=2 when it does
  > *(amended <date>, 101)*.

SecondBrain: `brain/components/session.md` and `brain/components/inbound-message-path.md` flag each superseded clause
above in place (CLAUDE.md, #334), and `brain/components/wire.md` is checked too, because the validator's Step 1 lives
there. Close-out row 3 makes that edit, re-running the D2 and 371 recipe lines over `brain/components`; plan.md Phase 7
points it at this list.

**What 099 keeps:** its two-occurrence FIXT endings in LogonReceived and Active (Reject 373=13/14, 371=35, 372 = the
third field, the consume key), its establishment disconnect on a validating FIXT session, and every 1128 refusal. What
changes for a FIXT session: frames with three or more 35s (D4's sticky conflict, D5's 13/14 precedence), LogoutSent's
reply test, and validation-off establishment.
