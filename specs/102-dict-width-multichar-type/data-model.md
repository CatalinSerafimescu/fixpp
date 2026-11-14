# Data model: 102

Layouts are asserted with `static_assert` in each header and again in `tests/dictionary/ref_shape_test.cpp`
(`sizeof`, `alignof`, `offsetof` of every member, standard layout, trivially copyable).

## `FieldRef` (after)

Declared order unchanged; `component_index` widened in place; `_reserved` removed.

| Offset | Member | Type | Note |
|---|---|---|---|
| 0 | `tag` | `std::uint16_t` | FIX tag |
| 2 | `type` | `field_data_type` (`std::uint8_t`) | |
| 3 | `rule` | `field_presence` (`std::uint8_t`) | |
| 4 | `condition_index` | `std::uint16_t` | never written (0) |
| 6 | `group_no_tag` | `std::uint16_t` | FIX tag |
| 8 | `component_index` | `std::uint32_t` | was `std::uint16_t`; 0 = not in a component, else the component's index + 1 |
| 12 | `enum_table_index` | `std::uint16_t` | never written (0) |
| 14 | `length_pair_data_tag` | `std::uint16_t` | FIX tag |

`sizeof == 16` (unchanged), `alignof == 4` (was 2). Every generated constexpr `FieldRef` table therefore keeps its size.

## `ComponentRef` (after)

Declared order unchanged; every member `std::uint32_t`; `_reserved` removed.

| Offset | Member | Type | Note |
|---|---|---|---|
| 0 | `component_id` | `std::uint32_t` | 0-based declaration index in both formats (not an Orchestra document's `fixr:component id`); was 16-bit and wrapped past 65,535 components |
| 4 | `name_offset` | `std::uint32_t` | was clamped at 65535 |
| 8 | `first_field_index` | `std::uint32_t` | was wrapped modulo 65,536 |
| 12 | `field_count` | `std::uint32_t` | was clamped at 65535 |
| 16 | `parent_component_id` | `std::uint32_t` | 0 = top level; otherwise the enclosing component's `component_id` + 1 |

`sizeof == 20` (was 12), `alignof == 4`.

## `GroupRef` (after)

Declared order unchanged; the two side-table values widened.

| Offset | Member | Type |
|---|---|---|
| 0 | `no_tag` | `std::uint16_t` |
| 2 | `first_field_tag` | `std::uint16_t` |
| 4 | `first_field_index` | `std::uint32_t` |
| 8 | `field_count` | `std::uint32_t` |
| 12 | `parent_group_no_tag` | `std::uint16_t` |
| 14 | `_reserved` | `std::uint16_t` (0) |

`sizeof == 16` (was 12), `alignof == 4`. `_reserved` stays as the explicit tail pad (the record is 16 bytes at
`alignof == 4` either way), under 2c §4.2's discipline: zero on emit, ignored on read. The group index is not stored
in a record; inside the loaders it stays 16-bit, bounded by the NumInGroup tag domain (research Survey).

## Loader behaviour

- No refusal is added. A QuickFIX XML or Orchestra base of any number of components loads (no count is refused); each
  component's id, parent id, members and name are its own while every table stays below 2^32 entries (FR-001's
  condition: components, side-table entries, name-pool bytes; L-102-1 (a)–(c)).
- Orchestra component and group ids (and their refs) are read in 0..2147483647 (the schema's `id_t` domain, with 0
  admitted as today), by today's lexical rule with only its upper bound raised (spec FR-001b); above it, or
  malformed, `orchestra_parse_error` as today. Field ids, `lengthId` and `numInGroup` ids stay FIX tags, 1..65535.
- 098's side-table fit refusal is removed (research D2).

## Generated type of a MultiCharValue field

| Surface | Before | After |
|---|---|---|
| view / owning accessor | `expected_t<char>` | `expected_t<std::string_view>` |
| builder `Args` member | `std::optional<char>` | `std::optional<std::string_view>` |
| group entry setter | `set_char` | `set_string` |

## Generated `Fields.hpp` row

Before: `{ tag, type, rule, condition_index, group_no_tag, component_index, enum_table_index, length_pair_data_tag, 0 },`
After: the same values without the trailing `0` (eight initializers). No value changes on a vendored base.

## Appendix A — C-ABI population (contract §6; spec FR-007)

Owner ruling, 2026-11-13: "All BREAKING, bump (Recommended)". `[const §X.7]` requires the BREAKING mark "in the
documentation of each affected declaration", so the affected declarations are derived here, in C-ABI 1.9's form
(`specs/091-data-field-bytes/data-model.md` Appendix A), not listed by example.

**Condition:** the Symbol column equals `tests/abi/golden/fixpp_capi_symbols.txt` as a set. No total is recorded.
Re-derive at the implementation head:

- **Row set**, run from the library root under bash:
  ```sh
  diff <(command grep -oE '^\| `fixpp_[a-z0-9_]+` \|' specs/102-dict-width-multichar-type/data-model.md \
           | command grep -oE 'fixpp_[a-z0-9_]+' | sort) \
       <(sort tests/abi/golden/fixpp_capi_symbols.txt)
  ```
  The extractor reads only the first cell of each row, so a symbol named in another row's reason cannot mask a
  deleted row. It must print nothing. Positive controls: deleting a row from a scratch copy, or appending a fake
  symbol to a scratch copy of the golden file, makes it print that symbol.
- **Classes:** each row is re-derived with research D8's C-ABI population recipe (tasks T067). The diff proves the row
  set only, not the classification.

**The predicate.** A call is BREAKING (**B**) when both hold:

1. its result depends on a `Dictionary` or a `table_view` built from a `fixpp_dict_t`: read at call time, or through
   the session, the validator, the parser or the send path that observe it;
2. that read consumes a part of the dictionary 102 corrects:
   - component identity, and so `message_fields` (message expansion through a componentRef past the wrap) and every
     store `Dictionary::build_table_view` builds from it (the valid-tag and required sets, the group contexts, the
     Length+Data pairs registered from every message's fields), the header store and the section records;
   - the group side table, and so `group_fields` and the bare member store `build_table_view` fills from it;
   - or the load's own result (the surrogate-id parse; the retired side-table refusal): the two load calls only.

Every other row is UNCHANGED (**U**), with the predicate as its reason. Condition 1's read reaches the call's own result, or is performed in the work the call
starts: a setter or carrier whose stored state another call reads later, and whose own result carries no such read,
is U (research D8 step 3, "Where arm O hits"). A B row changes only over a dictionary in
contract §6's regions: a load input of classes (a) to (c) and their twins, or a dictionary of class (f) or (g). No
vendored base is in region (f) or (g) (research Survey; spec US1).

**Carriers** (1.9's three tiers): **own note**, a BREAKING note on the declaration in `dict.h` (the root cause);
**message.h paragraph**, one BREAKING family paragraph in `message.h` beside its 1.9 reader paragraph, covering every
`message.h` row marked so; **session.h paragraph**, one BREAKING family paragraph in `session.h` beside its other
BREAKING session paragraphs; **`version.h`**, the version comment, which also carries the effects no declaration
carries: what the counterparty receives (a Reject the session now sends or no longer sends; the header placement and
the Length+Data carry of a frame the session sends).

**UNCHANGED by construction, whatever the row** (contract §6 "Unchanged"): the component side-table start, so
FIX50SP2's `RgstDistInstGrp` correction (read only through `Dictionary::component_fields`, which nothing in `src/`
calls); the component name offset (nothing in `src/` reads `ComponentRef::name_offset`); FR-008's run-reader guard
(no loader writes an overrunning record below 2^32 entries); the MultiCharValue kind (no `src/capi` file includes a
generated typed-tier header, and the runtime validator does not read the kind, spec FR-004).

| Symbol | Class | Reason / path | Carrier |
|---|---|---|---|
| `fixpp_decimal_compare` | U | no dictionary read | none |
| `fixpp_decimal_compare_checked` | U | no dictionary read | none |
| `fixpp_decimal_equal` | U | no dictionary read | none |
| `fixpp_decimal_equal_checked` | U | no dictionary read | none |
| `fixpp_decimal_format` | U | no dictionary read | none |
| `fixpp_decimal_init` | U | no dictionary read | none |
| `fixpp_decimal_parse` | U | no dictionary read | none |
| `fixpp_dict_destroy` | U | releases the handle; reads no content | none |
| `fixpp_dict_load_from_xml` | B | root cause: (c) a failure turned into a success; (f) and (g) the same code, and the dictionary the call yields is corrected | own note + `version.h` |
| `fixpp_dict_load_from_xml_with_overlay` | B | root cause: (a) and (b) failures turned into a success; (a′) and (b′) failures turned into a different failure, or into a success; (f) and (g) as for `fixpp_dict_load_from_xml` | own note + `version.h` |
| `fixpp_engine_config_add_dictionary` | U | stores the handle after `check_dict_handle`'s tag check; reads no content; arm O records the corrected read at the exports whose result carries it (the `fixpp_session_*` B rows) or whose started work performs it (`fixpp_engine_start`); this call's own result carries none, since its stored state is read later (`[const §X.7]` judges a call's own result) | none |
| `fixpp_engine_config_create` | U | no dictionary read | none |
| `fixpp_engine_config_destroy` | U | destroys; reads no dictionary content (an implicit destructor is not a read) | none |
| `fixpp_engine_config_set_realtime_clock` | U | no dictionary read | none |
| `fixpp_engine_config_set_worker_threads` | U | no dictionary read | none |
| `fixpp_engine_create` | U | builds the version registry from the configured dictionaries (`build_version_registry`, which always holds a value); its one dictionary read, `Dictionary::which_session_version`, reads no corrected part; arm O records the corrected read at the exports whose result carries it (the `fixpp_session_*` B rows) or whose started work performs it (`fixpp_engine_start`); this call's own result carries none, since its stored state is read later (`[const §X.7]` judges a call's own result) | none |
| `fixpp_engine_destroy` | U | destroys; reads no dictionary content (an implicit destructor is not a read) | none |
| `fixpp_engine_start` | B | with a FIXT session registered, `Engine::start` builds the route tables in the call (`fixt_route_set::build` → `Dictionary::as_table_view`, every store `build_table_view` builds from `message_fields`), and the role loops it starts run `Session::open` → `Dictionary::as_table_view` (the session table) and `bind_fixt_routes_`; the sessions it starts validate and parse over those stores. Its return code is unchanged: the route build still fails only on an allocation failure | session.h paragraph |
| `fixpp_entry_group_begin` | B | `Dictionary::group_first_field(group_tag) == 0` refuses with `FIXPP_ERR_TYPE_MISMATCH`; `GroupRef::first_field_tag` is the first-seen delimiter of the group contexts the message expansions build, which (f) moves | message.h paragraph |
| `fixpp_entry_set_data` | B | `pair_hooks` (the session table's Length+Data pairs, registered from every message's `message_fields`), `session_tv_->group_first_field_exact` and `is_group_collision` | message.h paragraph |
| `fixpp_entry_set_decimal` | B | `entry_set_bytes_impl`'s `is_group_collision` (`Dictionary::group_first_field`), as `fixpp_entry_group_begin` | message.h paragraph |
| `fixpp_entry_set_double` | B | as `fixpp_entry_set_decimal` | message.h paragraph |
| `fixpp_entry_set_int` | B | as `fixpp_entry_set_decimal` | message.h paragraph |
| `fixpp_entry_set_string` | B | `soh_outside_data(pair_hooks(…))` and `entry_set_bytes_impl`'s `is_group_collision` | message.h paragraph |
| `fixpp_group_builder_add_entry` | U | `check_builder` and `resolve_group` only; no dictionary read | none |
| `fixpp_group_get_field_decimal` | B | `scan_slice_for_tag` over an instance slice of a view parsed over the session table (`hooks_for`): the slices come from the group contexts built from `message_fields` (the bare member store on a context miss, (g)), the scan's carry from its pairs | message.h paragraph |
| `fixpp_group_get_field_double` | B | as `fixpp_group_get_field_decimal` | message.h paragraph |
| `fixpp_group_get_field_int` | B | as `fixpp_group_get_field_decimal` | message.h paragraph |
| `fixpp_group_get_field_string` | B | as `fixpp_group_get_field_decimal` | message.h paragraph |
| `fixpp_group_get_nested_group` | B | as `fixpp_group_get_field_decimal`, through `nested_group_slices` | message.h paragraph |
| `fixpp_library_version` | U | no dictionary read | none |
| `fixpp_msg_clone` | U | the return code is unchanged: it re-parses the source's bytes over the source's own table under its caps, which succeeded once; an outbound source returns `FIXPP_ERR_INVALID_HANDLE` first; the clone's view is the reader rows' (091's row) | message.h paragraph (view only) |
| `fixpp_msg_commit` | B | `validate_group_grammar` over `dict_` and `session_tv_` (group contexts and delimiters: (f), (g)) and `check_length_data(…, pair_hooks(…))`: `FIXPP_ERR_OK` and a group-grammar or pair refusal can trade places | message.h paragraph |
| `fixpp_msg_create_outbound` | U | reads only `Dictionary::messages()`'s MsgType list, which no 102 correction changes | none |
| `fixpp_msg_destroy` | U | destroys; reads no dictionary content (an implicit destructor is not a read) | none |
| `fixpp_msg_field_at` | B | the view's `OffsetTable` entries, whose counted carry reads the session table's pairs | message.h paragraph |
| `fixpp_msg_field_count` | B | as `fixpp_msg_field_at` | message.h paragraph |
| `fixpp_msg_get_bytes` | B | `view->get(tag)` over that `OffsetTable`: `FIXPP_ERR_OK` and `FIXPP_ERR_TAG_NOT_FOUND` can trade places | message.h paragraph |
| `fixpp_msg_get_decimal` | B | as `fixpp_msg_get_bytes` | message.h paragraph |
| `fixpp_msg_get_double` | B | as `fixpp_msg_get_bytes` | message.h paragraph |
| `fixpp_msg_get_group` | B | `offsets().group_context_for` and the group slices of the parse over the session table | message.h paragraph |
| `fixpp_msg_get_int` | B | as `fixpp_msg_get_bytes` | message.h paragraph |
| `fixpp_msg_get_msg_type` | U | `MessageView::msg_type` reads MsgType(35), the third field: since 093 every inbound state disregards as garbled a frame whose third field is not 35, before validation (`session.cpp`, `!hdr.msg_type_is_third` in each inbound arm), the toApp view reads a frame the session built with 35 third, and a clone re-parses a view's own bytes, so no pair precedes 35 | none |
| `fixpp_msg_get_string` | B | as `fixpp_msg_get_bytes` | message.h paragraph |
| `fixpp_msg_group_begin` | B | as `fixpp_entry_group_begin` | message.h paragraph |
| `fixpp_msg_group_end` | U | no dictionary read | none |
| `fixpp_msg_has_tag` | B | as `fixpp_msg_get_bytes`; the value can flip | message.h paragraph |
| `fixpp_msg_remove_tag` | U | no dictionary read | none |
| `fixpp_msg_set_bytes` | B | `is_group_collision` (`Dictionary::group_first_field`) | message.h paragraph |
| `fixpp_msg_set_data` | B | `Dictionary::field_ref(msg_type, tag).rule` (message expansion), `pair_hooks` and `is_group_collision` | message.h paragraph |
| `fixpp_msg_set_decimal` | B | `check_dict`: `Dictionary::field_ref(msg_type, tag)` (message expansion: `FIXPP_ERR_DICT_CONFIG`, `FIXPP_ERR_TYPE_MISMATCH` and `FIXPP_ERR_OK` can trade places) and `is_group_collision` | message.h paragraph |
| `fixpp_msg_set_double` | B | as `fixpp_msg_set_decimal` | message.h paragraph |
| `fixpp_msg_set_int` | B | as `fixpp_msg_set_decimal` | message.h paragraph |
| `fixpp_msg_set_string` | B | `check_dict` and `soh_outside_data(pair_hooks(…))` | message.h paragraph |
| `fixpp_msg_version` | B | `MessageView::resolved_application_version`, the version the session stamped from the frame's route (`Session::select_route_` → `peek_fixt_route`, which reads the header store, `table_view::is_header_tag`, and the pair store); `get(8)` and `msg_type()` read fixed header fields, so `kind` and `session` cannot move | message.h paragraph |
| `fixpp_session_acceptor_bound_endpoint` | U | no dictionary read | none |
| `fixpp_session_close` | B | handshake observer: a validating session runs the validator over the Logon before `interpret_logon` (the validate-first gate), over the corrected stores, so a Logon accepted before can be Rejected, or the reverse; its return then moves as its 1.9 clause states for a refused Logon | session.h paragraph |
| `fixpp_session_config_create` | U | no dictionary read | none |
| `fixpp_session_config_destroy` | U | destroys; reads no dictionary content (an implicit destructor is not a read) | none |
| `fixpp_session_config_set_begin_string` | U | stores a value; arm O records the corrected read at the exports whose result carries it (the `fixpp_session_*` B rows) or whose started work performs it (`fixpp_engine_start`); this call's own result carries none, since its stored state is read later (`[const §X.7]` judges a call's own result) | none |
| `fixpp_session_config_set_comp_ids` | U | stores a value | none |
| `fixpp_session_config_set_default_appl_ver_id` | U | `resolve_application_version` only; no dictionary read; arm O records the corrected read at the exports whose result carries it (the `fixpp_session_*` B rows) or whose started work performs it (`fixpp_engine_start`); this call's own result carries none, since its stored state is read later (`[const §X.7]` judges a call's own result) | none |
| `fixpp_session_config_set_dictionary` | U | stores the handle; the effect is the load call's and the session rows'; arm O records the corrected read at the exports whose result carries it (the `fixpp_session_*` B rows) or whose started work performs it (`fixpp_engine_start`); this call's own result carries none, since its stored state is read later (`[const §X.7]` judges a call's own result) | none |
| `fixpp_session_config_set_heartbeat_seconds` | U | stores a value | none |
| `fixpp_session_config_set_logon_timeout_ms` | U | stores a value | none |
| `fixpp_session_config_set_reset_on_logon` | U | stores a value | none |
| `fixpp_session_config_set_reset_seqnum_policy` | U | stores a value | none |
| `fixpp_session_config_set_role` | U | stores a value | none |
| `fixpp_session_config_set_security` | U | stores a value | none |
| `fixpp_session_config_set_tcp_endpoint` | U | stores a value | none |
| `fixpp_session_config_set_validate_inbound_messages` | U | stores the flag; its effect is the session rows'; arm O records the corrected read at the exports whose result carries it (the `fixpp_session_*` B rows) or whose started work performs it (`fixpp_engine_start`); this call's own result carries none, since its stored state is read later (`[const §X.7]` judges a call's own result) | none |
| `fixpp_session_garbled_frame_count` | U | counts the Framer's garbles and the frames whose third field is not MsgType(35) (`header_out_of_order_garble`); `scan_frame_header` runs with the session hooks, but fields 1–3 (BeginString, BodyLength, MsgType) cannot move: no Length+Data pair, group context or header-store test reaches them | none |
| `fixpp_session_is_established` | B | handshake observer, as `fixpp_session_close`: `true` and `false` can trade places | session.h paragraph |
| `fixpp_session_open` | B | `Engine::register_session` runs the FIXT check on a validating FIXT.1.1 config: `fixt_session_config_valid` reads `defines_fixt_admin`, which asks `Dictionary::field_valid_for("A", 1137)` (message expansion), so `FIXPP_ERR_OK` and the config refusal can trade places; `Session::bind_fixt_routes_` chooses the transport route by the same predicate | session.h paragraph |
| `fixpp_session_register_callback` | B | delivery: on a validating session `validate_inbound_` runs the validator over the corrected stores (Step 1's valid-tag set; Step 3's group walk, whose context miss falls back to the bare store, (g)), so a frame can go from delivered to Rejected or the reverse; on every dictionary-backed session the parse's pair carry moves; and the handshake observer of `fixpp_session_close` | session.h paragraph |
| `fixpp_session_register_send_callback` | B | the toApp view is the reader rows' view; the send path's header placement and carry (`fixpp_session_send`); and the handshake observer | session.h paragraph |
| `fixpp_session_send` | B | the send path places header fields by the session dictionary's header store (`table_view::is_header_tag`, `send_header_tags.hpp`) and carries Length+Data pairs over `session_hooks`, so the bytes sent, and `FIXPP_ERR_OK` against a payload refusal, can change; and the handshake observer | session.h paragraph |
| `fixpp_strerror` | U | no error code is added | none |
| `fixpp_version` | U | (value bumps) returns the new MINOR by design: the declaration mechanism, not a 102 effect | `version.h` |
| `fixpp_version_string` | U | no dictionary read | none |

The Class cell holds exactly `B` or `U`; qualifiers go in the Reason column.

### Python

The Python binding inherits its results from the C rows, by construction.

- `fixpp.i`'s type-scoped `%typemap(out) fixpp_error_t` raises the exception class of the code's block, with
  `.code`. Its argout typemaps convert the out-values. No binding code reads the dictionary.
- So a flat wrapper's result changes exactly where its C row's does. `msg_get_string`'s UTF-8 decode can turn a
  changed value into `FixppError`, and that is inside its row.
- A `fixpp_oo` method calls flat wrappers behind liveness, reentrancy and pickle checks that make no C call. So its
  result changes only where a row it composes is `B`.
- Python's BREAKING set is therefore every flat wrapper whose C row is `B`, plus every `fixpp_oo` method that calls
  one. Class (d) is the loaders' part of that set. Elsewhere, the result of each call is its C row's.
- **Derive the flat surface from `fixpp.i`:**
  - the declarations of the `%include`d `fix/c_api` headers, minus the `%ignore`s;
  - plus the redeclared `msg_*` functions;
  - plus the hand wrappers, each mapped to the export it calls: `session_register_callback` and
    `_register_application_oo` map to `fixpp_session_register_callback`; `engine_create` maps to
    `fixpp_engine_create`.
- **Derive the OO methods** from `fixpp_oo.py`'s import list and the method bodies that call those imports.
- **Positive:** the hand-written `session_register_callback` maps to `fixpp_session_register_callback`, which is
  `B`. A derivation that drops it with its `%ignore`d raw declaration is blind and stops.
- **No Python cell besides tasks T021b.** The binding is unchanged (T067's surface check), the existing pytest suite
  witnesses the mapping, and the composed rows' witnesses are the requirement map's.
