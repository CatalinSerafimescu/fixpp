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
| Reject buffers | fixed-size stack buffers in both `emit_session_reject_` overloads, `inbound_reject_leaf_`, `inbound_reject_best_effort_leaf_` and `inbound_business_reject_leaf_`; a failed build sends nothing and returns success | `session.cpp` |
| 092 echo bound | `dispose_unparseable_` passes `fault_ref_msg_type` only when no longer than `kMaxShippedMsgTypeLength` (092 research R-5: "the longest MsgType a shipped dictionary defines") | `scan_frame_header.hpp`, `session.cpp` |
| Error slots | `wire_*` slots end at `wire_length_data_mismatch`; C mapping: unexpected/header-order/required/out-of-range → `FIXPP_ERR_WIRE_CONFORMANCE`, invalid_tag_number/length_data_mismatch → `FIXPP_ERR_WIRE_INVALID_FRAME`; `translate` is a total switch with no `default` | `include/fixpp/core/error.hpp`, `src/capi/error.cpp`; `tests/capi/expected_error_map.csv`, `tests/capi/error_surface_test.cpp`, the `test_0NN_error_completeness.cpp` files |
| Benches | `scan_frame_header_bench`, `on_inbound_frame_bench` (in the CI suite, not paired); `validator_bench` (paired); `table_view_footprint_bench` | `bench/session/`, `bench/wire/`, `bench/dictionary/`, `bench/ci-suite.txt` |

QuickFIX (read for this feature; facts in the spec's Context): C++ `2ce8a60`, QuickFIX/J `4969564`. QuickFIX/Go: no local
checkout. The B85 ruling note records Go `eff945d` for 373=0/2/11; **Go was not read for #646** (the 372 echo), although
that issue's "Done when" asks for all three engines. The C++ and J reads agree, and the ruling follows them.

QuickFIX C++ reason texts (`src/C++/Values.h`, `SessionRejectReason_*_TEXT`), for the reasons the validation gate sends:
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
- **Hand-built tables:** one private `mark_defined(tag)`, called by **every population operation that names a tag**.
  The census at round 1, re-derived by reading the private mutators `table_view_builder` forwards to:
  `add_valid_tag`/`add_valid`, `add_required_tag`/`add_required`, `set_field_type`/`set_type`, `add_enum`,
  `set_multi_value`, `add_group_member`, `add_group_required_member`, `set_group_first` and their `_ctx` forms,
  `add_header_tag`, `add_fixt_framing_tag`, and both tags of `set_length_pair_data_tag`. Re-derive it at tasks by
  listing the mutators; a mutation cell (drop `mark_defined` from one op) must go RED.
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
  as today. "MsgType defined" means `valid_` holds it; on a hand-built table, `add_required_tag` alone does not define a
  MsgType (no dictionary-built table is affected: every declared message gets its header tags as valid).
- Step 1(a): a tag outside the view (and not a FIXT framing tag) answers
  `dict_.tag_defined(tag) ? wire_unexpected_tag : wire_undefined_tag`, ref = the tag. The envelope arm is unchanged.
- Two new `core::error` slots, `wire_invalid_msg_type` → 373=11 and `wire_undefined_tag` → 373=0, appended after the
  last enumerator at implement time; the values are pinned by the 101 completeness test, not written here. C mapping:
  both `FIXPP_ERR_WIRE_CONFORMANCE` (dictionary conformance results, as `wire_unexpected_tag` is). Error-slot test plan:
  D9.
- An undefined tag inside a group instance: Step 1 walks every field of the message before Step 3, against a set that
  includes group members, so it answers 373=0 there too, with no group-specific code. Traced against QuickFIX C++ on
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

**Scope of "QuickFIX C++'s text":** the FIX.4.2-and-later form (58 = the text alone). Below FIX.4.2 QuickFIX C++ writes
`<text> (<tag>)` and no 371/373; on FIX.4.2 it writes 373 only for reasons up to 11. fixpp writes 371/373 on every
BeginString today and keeps doing so; the difference is a B-row (FR-009).

### D4 — One MsgType per frame (FR-004)

**Decision:** a frame's MsgType is its **first 35**. On every frame a state arm handles that is its third field, because
093 disregards a frame whose third field is not 35 before any arm runs. Every reader reads that one value:
- `scan_frame_header` keeps the first 35 in `msg_type` and sets a new `bool msg_type_conflict` when **any** later 35's
  value differs from it (sticky: `D…A…D` is a conflict). It is the only new `FrameHeader` member; the hot path adds one
  comparison per later 35.
- `interpret_logon` (public C++) reads the first 35 likewise. Inside the session this is unobservable (D6 refuses every
  conflicting frame before either establishment arm calls it, and field 3 is read identically under any hooks), so it
  gets a direct unit cell and a B-row. A debug assertion at both arms that `interpret_logon`'s MsgType equals
  `hdr.msg_type` is optional.
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
  reports whether a later 35 stands in the header and whether one stands past it. Up to the stop it reads with S's hooks
  and S's table (`session_hooks(inbound_tv_)`, `*inbound_tv_`, as today: 099 judges the header by S). **Past the stop it
  reads values with the route's hooks** (`*route.entry.owner`, the table the frame is parsed and validated with), which
  are S's on a non-FIXT session. Reading the body with S's hooks would split a Data value the route declares, and one
  embedding `SOH 35=` would show a phantom past-header 35 (refinement of Opus's fix shape, recorded in plan.md
  `## Gate A`). `refuse_active_frame_` answers **14 when any
  later 35 lies past the header, else 13 when one lies in it, else 14** (the last clause keeps today's answer when the
  walk sees no later 35). This is QuickFIX C++'s `hasValidStructure`-before-`iterate` precedence. With exactly two 35s
  it equals 099's answer; with three or more it can differ (`0,D(hdr)…0(past)` and `0,0(hdr)…D(past)` answer 14, where
  099's rule answers nothing and 13 respectively). The reason is decided from the walk alone: a count from
  `scan_frame_header` is never combined with the walk's: they are two walks, and nothing pins that they agree.
- The walk's header test stays `table_view::is_header_tag` ∪ `dict::is_fixt_standard_header_tag` on non-FIXT sessions
  too. That union is the closer model of QuickFIX C++'s `isHeaderField` (static list or session dictionary, every
  BeginString). The residual — the tags `is_fixt_standard_header_tag` holds and QuickFIX C++'s static list lacks (at
  round 1: HopCompID 628, HopSendingTime 629, HopRefID 630, ApplExtID 1156; re-derive by comparing the two lists) — is
  disclosed (FR-009), not excluded: a FIX.4.x frame carrying them in the header is already non-conformant.
- `establishment_frame_refused_` becomes `hdr.msg_type_conflict || (fixt_routing_ && validate && route.application)`;
  the conflict clause holds whatever `validate_inbound_messages` is (D6).

### D6 — Validation off: refuse in establishment, handle by the first 35 after (FR-006)

**Decision:** `establishment_frame_refused_`'s conflict clause applies whatever `validate_inbound_messages` is (both
QuickFIX acceptors close on a non-Logon first frame; neither establishes). In LogonReceived, Active and LogoutSent a
validation-off session sends no Reject for the conflict; D4 makes it handled as its first 35.

**"Handled as X" is defined by a differential oracle**, not a hand table: the ending of a conflicting frame equals the
ending of the same frame with every later 35 removed and the frame re-framed (BodyLength, CheckSum). This covers X whose
own path can Reject (`4…D` is handled as a SequenceReset; `A…0` in Active is a 35=A, which reaches `fromApp` with an
Application and Reject 373=3 without one). "No Reject" means no Reject **for the repeat**.

**Rationale:** the validation-off ending after Logon splits the engines (C++ rejects, QuickFIX/J delivers the first
value); "validation off sends no validation Reject" keeps 099's owner ruling.

### D7 — The RefMsgType echo has one owner (FR-007)

**Decision:** one internal helper in `session.cpp` owns the echo policy for a session Reject, and a twin owns it for the
BusinessMessageReject (`build_business_message_reject`):
1. Build with 372 = the inbound MsgType, verbatim (`=` included).
2. If the build fails with `core::error::wire_field_value_truncated` and the MsgType is non-empty, build again with an
   empty RefMsgType (372 omitted).
3. Any other failure, or a second failure, keeps today's handling. Text(58) is never dropped to make room (FR-003).

The code means "out of room", not "out of room because of 372"; retrying without 372 is still right, because it either
fits or fails again into today's handling. `wire_frame_too_large` cannot fire at these buffer sizes (it needs a body
beyond BodyLength's placeholder width), and the builders' own `render_u32` failures cannot fire either.

**Placement:** the helper runs strictly after `seqnum_mgr_.peek_outbound()` and the SendingTime stamp and strictly before
`assign_outbound()`, `fire_to_admin_` and `store_then_emit`, so a retry re-stamps nothing, re-assigns nothing and fires no
callback twice. Every site already orders these so; the helper must sit between its build and its `if (rj_r)`.

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
`UnparseableFrameDisposition.RefMsgTypeBound_LongMsgType_RejectWithout372_{MalformedTag,LengthDataMismatch}` still hold
(their MsgType cannot fit), but their name and comment cite the retired mechanism and are renamed to cite FR-007.

**Census recipe for the sites** (re-run at tasks and after the last fix commit):
`git grep -nE 'build_reject(_with_text)?\(|build_business_message_reject\(' -- src/session`. Every hit outside
`admin_messages.cpp` is the helper or carries a literal 372 (`apply_inbound_sequence_reset`'s `"4"`).
`git grep -n kMaxShippedMsgTypeLength -- src tests` returns nothing after the change.

**Residual (B-row, FR-009):** D3's 58 takes room in the same fixed buffer, and `open()` bounds CompIDs for forbidden bytes,
not for length. With CompIDs long enough that a gate Reject's fixed fields plus 58 exceed the build buffer, the Reject is
not sent even with a one-byte MsgType, and the retry does not recover it (it drops only 372). This is the local-config
loss 092 R-5 put out of scope. Bounding CompID length at `open()` would close it at the source; that is a new config
refusal and belongs to a follow-up issue.

**Rationale:** in Reject (35=3), 372 is optional (`Req'd N`); the Reject's reason and 45 are what the peer needs. A
retry on the failing path only costs nothing on the normal path; a length bound needs a number that is a function of the
other fields.

**BusinessMessageReject — provisional, open for a ruling.** In BusinessMessageReject (35=j) RefMsgType(372) is
**required** (`required='Y'` in the vendored FIX 4.2, 4.4 and 5.0SP2 dictionaries), so the optional-field rationale does
not carry over. QuickFIX writes 372 from the inbound MsgType with no length limit, so it has no such case. Provisional
ending, one per condition: a BMR whose echo does not fit is sent **without 372** (a non-conformant BMR, which a validating
peer may answer with Reject 373=1, 371=372), rather than not at all, and a B-row discloses it. The alternative is to keep
today's silent loss for the BMR only. Spec Clarifications (Gate A round 1) records the question.

**Alternatives:** a bigger buffer (moves the threshold, does not remove it); refusing the inbound frame (sends nothing,
the defect itself); keeping 092's bound and carving the disposer out of FR-007 (two rules for one field).

### D8 — Benchmarks (FR-008, SC-004), `[const §VIII.2]`

- **Protocol:** Article VIII §2's: a local same-runner run, A-B-A-B, compared **min-per-tree**, a slowdown above **+5%**
  fails. Base = the merge-base of the candidate and `origin/main` **at measurement time**, built in a worktree whose path
  has the candidate's length. Record the merge-base, four observations per tree, the minima and the ratios.
- **Binaries:** `scan_frame_header_bench`, `on_inbound_frame_bench` **and** `validator_bench`. `validator_bench`'s CI
  pairing is supplementary only (Article VIII §2: a green sentinel is not evidence of +5% compliance).
- **Footprint:** `table_view_footprint_bench` gains the defined-tag store's bytes for FIX44, a FIXT route, FIX Latest and
  a high-tag overlay.
- **Expected cost:** one comparison per later 35 in the scan; one hot `valid_.find` per validated message, as today (D2
  shares it); one cold `tag_defined` only on a field that has already failed; nothing on a validation-off frame with one
  MsgType.

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

**Mutate-and-run recipes** (grep is the wrong instrument where `58=` or a MsgType string matches nearly every Reject
cell). On a scratch commit (never a stash: it is shared by every worktree), apply one change alone; rebuild every test
target; check with `strings` that the binaries are fresh; run the whole `ctest` suite **unfiltered** (the cells live in
many binaries: `session_pure_tests`, `wire_dict_tests`, the per-feature `error_0NN_completeness` executables,
`capi_pure_tests`, the interop executables, the fuzz harness); the RED set is the census for that change:
1. D3 alone (or a mutant passing a fixed non-empty text at the three gate sites) → the cells pinning no 58, plus the
   `HP-QFj-*-reject-invalid-admin` goldens.
2. D4 alone (first-wins scan + `interpret_logon` + LogoutSent) → the 092 and 099 cells pinning last-wins endings.
3. D6 alone (conflict clause without the `validate` term) → the validation-off establishment cells.
4. D2 alone → the validator and session cells pinning 373=2 for an undefined tag or an undefined MsgType.
5. Two placeholder enumerators appended to `core::error` → the RED set must equal the sentinel grep's hits.

**Known at round 1, not exhaustive** (the recipes above decide; a cell missing here is not exempt):
- 098: `DialectOverlayInboundBase098.AS3_*` (→ 11, no 371, 58) and `AS1`/`AS2` (undefined tags → 0, 58).
- `UnparseableFrameDisposition.LateSite_Control_BelowCeiling_ValidateGate_ParsedAndValidated` (undefined tag → 0, 58).
- `ValidateGateInbound.ManyFieldsBypassArena_Rejected_NotBypassed` (undefined tags → 0).
- Wire: `ValidatorProductionTableView.UnknownMsgTypeRejectsLikePreHoistFieldValidFor` (→ `wire_invalid_msg_type`),
  `ValidatorTypeCheck.ValidateUnexpectedTagReturnsError`, `ValidatorPerVersion.UnexpectedTagRejected`,
  `ValidatorDomain.UnexpectedTagRejected`, `tests/wire/conformance/w014_validate.csv` row `unexpected-tag-42`,
  `W3Unit.TagSDoesNotDeclare_TheRouteAnswers` — each classified by D1.
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
- Interop goldens `HP-QFj-{acc,init}-fix44-reject-invalid-admin.fix` (58 added, 9 and 10 move; regenerated).

Each rewritten cell names its superseding requirement in the test.

**New witnesses owed** (each RED on `fa778a8e` where today's behaviour differs, and RED with its production change
reverted):
- D4 sticky: `D…A…D` through Active (validating: a last-vs-third mutant reads no conflict; must go RED), and
  `A…D…A` carrying Logon fields through both establishment roles on a non-FIXT and on a validation-off session (the
  last-vs-third mutant establishes; D4 disconnects). `D…A…D` alone does not discriminate in establishment.
- D5: `0,D(hdr)…0(past)` → 14 and `0,0(hdr)…D(past)` → 14; validating non-FIXT 13 and 14; a FIXT-only header tag at the
  non-FIXT boundary (1128 before a later 35 → 13, as QuickFIX C++).
- LogoutSent on validating and off, FIXT and non-FIXT: `D..5` stays, `5..D` confirms.
- D6's differential oracle for every post-establishment validation-off row and every LogoutSent row, including X = 4
  and X = A.
- Controls: a single MsgType in every affected arm; a same-value repeat; the 3/5 exemption ordering on validation off;
  the outbound Logon masking; the resend replay.
- `interpret_logon` direct cell: `35=D … 35=A` → `session_invalid_logon`.
- D7: a long undefined MsgType through the validation gate (373=11, no 372, 58 present); `inbound_reject_leaf_` via
  PossDup Arm C on validation off (the session survives); the zero-argument emit (no Application); the BMR leaf
  (`fromApp` rejects); the disposer with a fitting 3-byte overlay MsgType and a fault (372 present; none on
  `fa778a8e`); a unit cell for `inbound_reject_best_effort_leaf_`; `35=D=X` echoed as `372=D=X` and read back by fixpp's
  parser as one field.
- D1: one mutation per population path (dictionary-built; each hand-built op class).

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
- the 098 test file's header recipe (B85 note).

`scan_frame_header.hpp`'s header names **101 D4** as superseding 092 data-model E-1 / contract C-3 I-3 for `msg_type`,
and `session.cpp`'s `dispose_unparseable_` names **101 D7** as superseding 092 research R-5's bound.

## Supersessions (by name)

This feature changes clauses of merged features. Each is superseded for the part named, nothing more:

| Superseded clause | Part superseded | By |
|---|---|---|
| 092 `contracts/unparseable-frame-disposition.md` C-3 **I-3** ("the scan keeps last-wins for every existing member (E-1)") | `msg_type` only; `msg_seq_num` stays last-wins | D4, FR-004 |
| 092 `data-model.md` **E-1** ("`msg_seq_num` and `msg_type` stay last-wins") | `msg_type` only | D4, FR-004 |
| 092 `research.md` **R-5**, the RefMsgType bound (`kMaxShippedMsgTypeLength`) | the bound and its "cannot fail" premise | D7, FR-007 |
| 099 `spec.md` **FR-025**: "whose last MsgType differs from its third field's"; "373=13 when the second 35 is in the header, 373=14 when past it"; the **Limits** sentences "a last MsgType of 5 confirms the Logout" and "A non-validating session and a non-FIXT session keep the split" | the comparison (now any later differing 35), the 13/14 rule (D5's precedence), LogoutSent's reply test, the non-FIXT and validation-off carve-outs | D4, D5, D6; FR-004..FR-006 |
| 099 `spec.md` **FR-029**: "whose last MsgType differs from its third field's"; "a validating FIXT session" for that clause | the comparison and its scope (every session, validating or not) | D5, D6; FR-005, FR-006 |
| 099 `data-model.md` **E-7**, steps **S1** and **A1** | S1's condition and its "interpret_logon reads the last 35" rationale; A1's condition and 13/14 rule | D4, D5 |
| 099 `contracts/surface.md`, the validation-off bullet ("establishes a session of either role, because `interpret_logon` reads the last 35 … LogoutSent takes a last MsgType of 5 as the reply") | the whole bullet | D4, D6 |
| 099 `contracts/witnesses.md` **W-9**'s "`D…5` in LogoutSent: confirms the Logout" | that named cell | D4 |
| 099 test tables: `kLogoutSent`/`kLogoutSentOff` `LO-a` `D..5`/`5..D`; `kNotConnectedOff` `NC-a` and `kLogonSentOff` `LS-a` `logon-fields` rows; `W6PerShape.DThenAWithLogonFields_Establishes` | their expected endings | D4, D6 |
| B&L **B-099-4**'s "Not validating" bullet, **B-099-5**, **B-099-11** | each whole bullet/row, rewritten to the new endings | FR-009 |
| B&L **L-452-2** | closed: the echo stays verbatim by ruling, and the silent loss is fixed | D7, FR-009 |

**What 099 keeps:** its two-occurrence FIXT endings in LogonReceived and Active (Reject 373=13/14, 371=35, 372 = the
third field, the consume key), its establishment disconnect on a validating FIXT session, and every 1128 refusal. What
changes for a FIXT session: frames with three or more 35s (D4's sticky conflict, D5's 13/14 precedence), LogoutSent's
reply test, and validation-off establishment.
