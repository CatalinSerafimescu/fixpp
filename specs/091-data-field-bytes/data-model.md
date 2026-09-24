# Data model — 091 data-field-bytes

No persistent data. The "entities" are the builder's accumulator nodes and the generated Args
shapes. Decisions are cited as `R-n` (research.md).

## `wire::body_builder` (changed)

| Member | Change | Notes |
|---|---|---|
| ctor `body_builder(std::string_view msg_type, dict_hooks hooks = dict_hooks::none())` | **new defaulted parameter** | Existing one-argument callers compile unchanged (R-2). Precondition: the dictionary behind `hooks` outlives the builder (C-1). |
| `dict_hooks hooks_` | **new private member**, by value | Trivially copyable. Read by the commit check only (FR-009); the set-time operation uses the standard table. |
| `field_data(std::uint16_t data_tag, std::span<const std::byte> value)` | **new public** | Atomic Length+Data append, standard pairs only (R-1, R-2, R-3). |
| `field(tag, string_view)` and the other `field` overloads | unchanged | INV-2 intact (FR-007). |
| `commit(out)` | **adds the pair check** inside the INV-5 walk | R-4, FR-008. |
| `validate_group_grammar` | becomes the combined INV-5 + pair walk (a private static, which gains a `dict_hooks const&` parameter) | R-4. |

## `wire::entry_handle` (changed)

| Member | Change |
|---|---|
| `set_data(std::uint16_t data_tag, std::span<const std::byte> value)` | **new public**. The same checks as `set_string` for handle validity, then R-3 (standard table only). Preferred implementation: `field_data` and `set_data` forward to one shared static `append_data_field(into, data_tag, value)`, in the file's `append_*_field` style (R-3). The two-surface tests (C-1.4, C-1.5 nested twin, C-1.9) are required either way. |

## Accumulator node shapes (unchanged)

`entry_node` and `group_instance` gain no field. A pair is simply two consecutive scalar
`entry_node`s: the Length (value = ASCII decimal count) and the Data (value = the verbatim octets).
Keeping them as ordinary nodes is what lets `compute_size` / `serialize_entries` stay untouched, and
lets the commit check treat pairs from `field_data` and pairs written by hand identically.

## Invariants

| ID | Statement | Enforced where |
|---|---|---|
| INV-2 (existing, unchanged) | `field`/`set_string`: no framing tag, every byte in `0x20–0x7E` | `append_string_field` |
| INV-4 (existing, extended) | Any failed append leaves the container at its pre-call size, so a later commit serializes exactly what it would have without the call; a failed commit leaves `out` and the accumulator untouched. Arena capacity consumed by a failed call is not reclaimed (as for every existing append) | `field_data`/`set_data` roll back **both** nodes (R-3); observed by commit-and-byte-compare (C-1) |
| INV-5 (existing) | Each group instance is non-empty and delimiter-first | commit walk. A Length-delimited entry passes because the Length node is appended first (R-6) |
| **INV-6 (new)** | In every container, each pair tag (per `hooks_`: standard pairs, plus the dictionary's own pairs when the hooks carry one) forms a well-formed pair: Length immediately followed by its Data, Length a positive decimal equal to the Data's octet count, Data non-empty | commit walk, through `wire::length_data_checker` (R-4) |
| **INV-7 (new)** | `field_data`/`set_data` never append a node for a tag that the **standard table** does not name as a Data tag (whatever `hooks_` holds), and never for a framing tag on either half | refusal order, R-3; the Length half by a `static_assert` over the standard table |
| **INV-8 (new)** | Set-time pairs ⊆ commit-time pairs, so nothing the set-time operation appends is refused at commit | every `dict_hooks` answers standard tags from the standard table first (`dict_hooks.hpp`) |

## Refusal → error (FR-004a)

| Condition | Error | Stage |
|---|---|---|
| handle has no owner / is not the innermost open entry (`set_data`; checked before any resolution, C-1.4b) | `wire_invalid_field_format` | set |
| `data_tag` is a framing tag | `wire_field_value_out_of_range` | set |
| `dict_hooks::none().length_tag_for_data(data_tag) == 0` (includes a dictionary-only Data tag) | `wire_unexpected_tag` | set |
| `value.empty()` | `wire_field_value_out_of_range` | set |
| arena exhaustion during either append | `wire_frame_too_large` (both nodes rolled back) | set |
| INV-6 violated in any container | `wire_invalid_field_format` | commit |
| serialized body over `kBodyCap` | `wire_frame_too_large` (existing) | commit |

## Generated Args (changed)

| Shape | Change |
|---|---|
| Coupled Length+Data member `std::optional<std::string_view> <data_accessor>` | **type unchanged** (FR-011). Emitted through `field_data`/`set_data` (R-7). |
| v50sp2 caller-set Length member `std::optional<std::int64_t>` of the five pairs R-11 newly couples | **deleted, no alias** (FR-011 carve-out, owner-ratified). Its Data member becomes the coupled member. Owners listed (dated) in C-2.1. |
| `std::optional<std::string_view> message_encoding` | **new**, top level only, **appended last**, only on messages that can carry an `Encoded*` field at any depth (R-8, FR-011a). Emitted first in the body as `347`. |

## Limitation ledger (FR-016, FR-008, FR-009, FR-009a, FR-011, FR-011a, FR-017, FR-019)

| Row | Action |
|---|---|
| L-067-2 | **moves** to `spec/behaviors-and-limitations-closed.md`, citing stage-one `68c8c769` + this PR |
| "L-067-2 is unchanged … (fixpp#418)" bullet in the live #426/#428 limitations block | **deleted** (the gap it restates is closed) |
| `tests/interop/conversation/support/conv_wire.hpp` hand-built-frame comment; `tests/interop/conversation/conv_cell_test.cpp` B-05 comment | reworded: B-05 stays on its hook because moving it needs a counterparty republish (spec Assumptions), not because #418 is open |
| `include/fixpp/wire/length_data_check.hpp` header comment; `brain/components/wire.md` Length+Data line | "#418 is meant to be / must reuse" → past tense (the builder is the checker's second caller) |
| W-008 (`spec/feature-catalogue.md`) | evidence column gains this feature |
| B-091-1 (new, behaviour) | `body_builder::commit` refuses a malformed hand-written Length/Data pair (`wire_invalid_field_format`) |
| B-091-2 (new, behaviour) | `MessageEncoding(347)` is available on generated Args but is not required when an `Encoded*` field is set |
| B-091-3 (new, behaviour) | C++ set-time vs C-ABI divergences: `field_data`/`set_data` refuse a dictionary-only Data tag that `fixpp_msg_set_data` accepts (follow-up *verifiable session binding for dictionary pairs, option (b)*, fixpp#505); a repeated call appends where the C-ABI upserts |
| B-091-4 (new, behaviour, source break; **BREAKING — C-ABI 1.9**, `[const §X.7]`, FR-019) | FIX 5.0 SP2: the QuickFIX XML loader now pairs 2494→2493, 2815→2814, 43109→42684, 43110→42486, 43111→42982, so `Dictionary`/`table_view`/`FieldRef` report them and the generated v50sp2 builders couple them; the caller-set Length members are deleted — a designated-initializer or member-access caller gets a compile error, a positional aggregate-initializer caller may silently shift into the next member. **User-loaded dictionaries:** a non-standard pair declared adjacently only inside a component or group becomes a dictionary pair, so `table_view::has_nonstandard_pair()` can flip, inbound scanners read its Data by count, `fixpp_msg_set_data` accepts it, and a malformed hand-written pair of that kind, which committed before, is now refused at commit through both the C-ABI and `body_builder`. The **BREAKING (1.9)** effects, each a success turned into a failure or a failure turned into a different failure, are FR-019's recipe-derived population (`specs/091-data-field-bytes/research.md` R-11), classified row by row in `specs/091-data-field-bytes/data-model.md` Appendix A: `fixpp_dict_load_from_xml` (root cause: the dictionary it yields now carries the pair); `fixpp_msg_commit` returned `FIXPP_ERR_OK` for that malformed pair and now returns `FIXPP_ERR_WIRE_CONFORMANCE`; `fixpp_session_send` returned `FIXPP_ERR_OK` for it and now returns `FIXPP_ERR_APP_PAYLOAD_MALFORMED`, and a count that covers a following field is now transmitted verbatim as Data; for a send it refuses, the toApp callback (`fixpp_session_register_send_callback`) is not invoked; inbound scanners now read such a Data by count, so an inbound message carrying a malformed pair of that kind, which parsed before as two plain fields, is dropped as a parse error with no Reject (`fixpp_session_register_callback`'s `fixpp_recv_cb` is not invoked); the inbound reader family (one shared `message.h` paragraph: inbound, clone and toApp views), including `fixpp_msg_version` (the `appl_ver_id` member can become null) and `fixpp_msg_get_msg_type` (`FIXPP_ERR_OK` can become `FIXPP_ERR_TAG_NOT_FOUND` when such a pair precedes 35, reachable while the session does not validate inbound header order), can see a counted Data absorb fields it used to return; `fixpp_msg_set_data`/`fixpp_entry_set_data` for such a Data tag no longer stop at `FIXPP_ERR_TYPE_MISMATCH` but reach their later documented refusals (e.g. `FIXPP_ERR_WIRE_CONFORMANCE` for `len == 0`; `FIXPP_ERR_DICT_CONFIG` from `fixpp_msg_set_data` for a half not declared for the MsgType); and, with no carrying declaration (the `version.h` comment), a stored pre-1.9 frame with such a malformed pair is gap-filled on replay and the header/Logon scans read such a Data by count, so a Logon in which a required field (HeartBtInt(108), or 35/49/56 when out of order) follows such a malformed count is refused, and on the initiator path `fixpp_session_is_established` stays `false` and `fixpp_session_close` returns `FIXPP_ERR_THREAD_SESSION_LIFECYCLE`, not `FIXPP_ERR_OK`. Every other export is unchanged (FR-019's condition: no pair lookup at call time and no read of a view the pair-aware parse built; `specs/091-data-field-bytes/data-model.md` Appendix A). The **additive** widenings that ride with them, each a failure turned into a success: `fixpp_msg_set_data`/`fixpp_entry_set_data` now accept a well-formed such pair (their BREAKING failure → different failure case above stays); `fixpp_msg_set_string`/`fixpp_entry_set_string` no longer refuse an SOH-bearing value for its Data tag; and a correctly Length-prefixed, SOH-bearing Data of that pair, written through any setter, which failed commit with `FIXPP_ERR_WIRE_CONFORMANCE` before, now commits, and sent through `fixpp_session_send`, which returned `FIXPP_ERR_APP_PAYLOAD_MALFORMED` before, now returns `FIXPP_ERR_OK`. For the shipped dictionaries inbound parsing is unaffected (C-2.5a) |
| `brain/components/c-api.md` | gains a C-ABI 1.9 entry beside the C-ABI 1.8 one (FR-019), naming FR-019's BREAKING population (the marked declarations, including `fixpp_msg_set_data`/`fixpp_entry_set_data`; the reader-family paragraph, including `fixpp_msg_version` and `fixpp_msg_get_msg_type`; the `version.h` no-carrier effects) and the recipe that derives it (`specs/091-data-field-bytes/research.md` R-11; `specs/091-data-field-bytes/data-model.md` Appendix A) |
| Every CA row of `spec/feature-catalogue.md` that lists a declaration FR-019 marks, a reader-paragraph member included (e.g. `fixpp_msg_version`, `fixpp_msg_get_msg_type`) (re-derive by matching every `specs/091-data-field-bytes/data-model.md` Appendix A row of class B whose carrier is a declaration note or the reader paragraph, not the `version.h` history alone, against each row's listed functions; the root cause is the `fixpp_dict_load_from_xml` row, CA-011) | gains a C-ABI 1.9 BREAKING note in the style of CA-011's 1.8 one, naming that row's effect: a custom pair declared only inside a component or group is now a dictionary pair (FR-017, FR-019) |
| L-426-3 (existing) | **extended** with the send-side consequence (FR-009a); no L-091-1 near-copy |

The row IDs are provisional; `/speckit-implement` assigns the final ones against the live file.

## Appendix A — C-ABI 1.9 export classification (FR-019)

**Condition:** the Symbol column equals `tests/abi/golden/fixpp_capi_symbols.txt` as a set (CI
already holds that file equal to the `nm` export list of `libfixpp_capi.a`). No total is recorded.
Re-derive at the implementation head:
- **Row set**, run from the library root:
  ```sh
  diff <(grep -oE '^\| `fixpp_[a-z0-9_]+` \|' specs/091-data-field-bytes/data-model.md \
           | grep -oE 'fixpp_[a-z0-9_]+' | sort) \
       <(sort tests/abi/golden/fixpp_capi_symbols.txt)
  ```
  The extractor reads only the
  **first cell** of each row, so a symbol named in another row's reason cannot mask a deleted row.
  It must print nothing. Positive controls (plan Phase 0b): deleting a row, or appending a fake
  symbol to a copy of the golden file, makes it print that symbol.
- **Classes:** each row is re-verified against source with research.md R-11's recipe. The diff
  proves only the row set, not the classification.

Classes (`[const §X.7]`, research.md R-11 *Classification*): **B** BREAKING (a success turned into a
failure, a documented result changed, or a failure turned into a different failure); **A** ADDITIVE
(a failure turned into a success); **U** UNCHANGED (R-11 step 5: no path to steps 1–4). The path
tags are `pair_hooks` (`message_write.cpp`'s `pair_hooks()` / `soh_outside_data` /
`check_length_data`), `session_hooks` (`session.cpp`'s `session_hooks()` →
`dict_hooks::for_table_view`), `OffsetTable` (`offset_table.cpp`'s counted carry through
`hooks_.data_tag_for_length`) and `carry` (`length_data_carry` in `send_impl` / `scan_frame_header`
/ `interpret_logon` / `build_replay_frame`). "Reader paragraph" is FR-019's one shared BREAKING
paragraph in `message.h`.

| Symbol | Class | Reason / path | Carrier |
|---|---|---|---|
| `fixpp_decimal_compare` | U | no path (pure decimal arithmetic) | none |
| `fixpp_decimal_compare_checked` | U | no path | none |
| `fixpp_decimal_equal` | U | no path | none |
| `fixpp_decimal_equal_checked` | U | no path | none |
| `fixpp_decimal_format` | U | no path | none |
| `fixpp_decimal_init` | U | no path | none |
| `fixpp_decimal_parse` | U | no path | none |
| `fixpp_dict_load_from_xml` | B | root cause: the loader walk (R-11) now couples a component/group-only pair; same return code, and the yielded dictionary carries the pair | own note |
| `fixpp_dict_destroy` | U | no path | none |
| `fixpp_engine_config_create` | U | no path | none |
| `fixpp_engine_config_set_worker_threads` | U | no path | none |
| `fixpp_engine_config_set_realtime_clock` | U | no path | none |
| `fixpp_engine_config_destroy` | U | no path | none |
| `fixpp_engine_create` | U | no path (no dictionary or pair lookup at create) | none |
| `fixpp_engine_start` | U | no pair lookup at call time; the sessions it runs are classified in the `fixpp_session_*` rows | none |
| `fixpp_engine_destroy` | U | no path | none |
| `fixpp_strerror` | U | no path; no error code added (FR-004a) | none |
| `fixpp_msg_get_string` | B | `view->get(tag)` on the `OffsetTable` view: a counted Data can absorb `tag`, so `FIXPP_ERR_OK` can become `FIXPP_ERR_TAG_NOT_FOUND` | reader paragraph |
| `fixpp_msg_get_bytes` | B | as `fixpp_msg_get_string` | reader paragraph |
| `fixpp_msg_get_int` | B | as `fixpp_msg_get_string` | reader paragraph |
| `fixpp_msg_get_double` | B | as `fixpp_msg_get_string` | reader paragraph |
| `fixpp_msg_get_decimal` | B | as `fixpp_msg_get_string` | reader paragraph |
| `fixpp_msg_has_tag` | B | as `fixpp_msg_get_string`, so the value can flip to `false` | reader paragraph |
| `fixpp_msg_version` | B | `view->get(1137)` can be absorbed, so the `appl_ver_id` member can go from a value to null with `FIXPP_ERR_OK`; tag 8 is fixed first by the Framer and cannot be absorbed | reader paragraph |
| `fixpp_msg_get_msg_type` | B | `view->msg_type()` reads tag 35 through the same view; a pair before 35 is reachable while `validate_inbound_messages` is unset on the session (the header-order check is the only rule placing 35 third), so `FIXPP_ERR_OK` can become `FIXPP_ERR_TAG_NOT_FOUND` | reader paragraph |
| `fixpp_msg_get_group` | B | `offsets().find` / `group_slices` over the `OffsetTable`: a counted Data can absorb a NoXxx or a delimiter, giving `FIXPP_ERR_TAG_NOT_FOUND` or a changed count | reader paragraph |
| `fixpp_group_get_field_string` | B | instance-slice walk over the same `OffsetTable` | reader paragraph |
| `fixpp_group_get_field_int` | B | as `fixpp_group_get_field_string` | reader paragraph |
| `fixpp_group_get_field_double` | B | as `fixpp_group_get_field_string` | reader paragraph |
| `fixpp_group_get_field_decimal` | B | as `fixpp_group_get_field_string` | reader paragraph |
| `fixpp_group_get_nested_group` | B | as `fixpp_group_get_field_string` | reader paragraph |
| `fixpp_msg_field_count` | B | the `OffsetTable`'s entry count changes | reader paragraph |
| `fixpp_msg_field_at` | B | as `fixpp_msg_field_count`; an index can go out of range | reader paragraph |
| `fixpp_msg_create_outbound` | U | captures the session dictionary; no pair lookup at call time | none |
| `fixpp_msg_destroy` | U | no path | none |
| `fixpp_msg_clone` | U | re-parses a dict-backed source with `Parser<Index>` over the source's own table; the same bytes and table succeed exactly when the source parse did, so the return code is unchanged. The clone's contents are the reader rows ("clone view") | reader paragraph (view only) |
| `fixpp_msg_set_string` | A | `soh_outside_data(pair_hooks)`: an SOH-bearing value for the new Data tag goes from `FIXPP_ERR_WIRE_CONFORMANCE` to `FIXPP_ERR_OK` | B-091-4 additive |
| `fixpp_msg_set_bytes` | U | no pair lookup at set time; the pair's effect on its value surfaces at commit (`fixpp_msg_commit`) | none |
| `fixpp_msg_set_data` | B + A | `pair_hooks().length_tag_for_data`. A: `FIXPP_ERR_TYPE_MISMATCH` → `FIXPP_ERR_OK`. B (failure → different failure): the pair-resolution `FIXPP_ERR_TYPE_MISMATCH` no longer fires, so the call reaches `message.h`'s later documented refusals, e.g. `FIXPP_ERR_WIRE_CONFORMANCE` (`len == 0`) or `FIXPP_ERR_DICT_CONFIG` (a half not declared for this MsgType) | own note + B-091-4 additive |
| `fixpp_msg_set_int` | U | no pair lookup | none |
| `fixpp_msg_set_double` | U | no pair lookup | none |
| `fixpp_msg_set_decimal` | U | no pair lookup | none |
| `fixpp_msg_remove_tag` | U | no pair lookup; it removes one tag, never the paired half | none |
| `fixpp_msg_group_begin` | U | no pair lookup | none |
| `fixpp_msg_commit` | B + A | `check_length_data(acc.entries, pair_hooks(h))`: a malformed pair goes from `FIXPP_ERR_OK` to `FIXPP_ERR_WIRE_CONFORMANCE`; a well-formed SOH-bearing Data goes from `FIXPP_ERR_WIRE_CONFORMANCE` to `FIXPP_ERR_OK` | own note + B-091-4 additive |
| `fixpp_group_builder_add_entry` | U | no pair lookup | none |
| `fixpp_entry_set_string` | A | `soh_outside_data(pair_hooks)`, as `fixpp_msg_set_string` | B-091-4 additive |
| `fixpp_entry_set_data` | B + A | `pair_hooks().length_tag_for_data`. A: `FIXPP_ERR_TYPE_MISMATCH` → `FIXPP_ERR_OK`. B (failure → different failure): as `fixpp_msg_set_data`, e.g. `FIXPP_ERR_WIRE_CONFORMANCE` (`len == 0`) | own note + B-091-4 additive |
| `fixpp_entry_set_int` | U | no pair lookup | none |
| `fixpp_entry_set_double` | U | no pair lookup | none |
| `fixpp_entry_set_decimal` | U | no pair lookup | none |
| `fixpp_entry_group_begin` | U | no pair lookup | none |
| `fixpp_msg_group_end` | U | no pair lookup; `check_length_data`'s instance recursion runs only from commit | none |
| `fixpp_session_config_create` | U | no path | none |
| `fixpp_session_config_set_comp_ids` | U | no path | none |
| `fixpp_session_config_set_begin_string` | U | no path | none |
| `fixpp_session_config_set_role` | U | no path | none |
| `fixpp_session_config_set_heartbeat_seconds` | U | no path | none |
| `fixpp_session_config_set_security` | U | no path | none |
| `fixpp_session_config_set_dictionary` | U | stores the handle; no pair lookup at call time (the effect is `fixpp_dict_load_from_xml`'s) | none |
| `fixpp_session_config_set_reset_on_logon` | U | no path | none |
| `fixpp_session_config_set_reset_seqnum_policy` | U | no path | none |
| `fixpp_session_config_set_tcp_endpoint` | U | no path | none |
| `fixpp_session_config_destroy` | U | no path | none |
| `fixpp_session_open` | U | builds the inbound table view; no pair lookup at open ("open != connected") | none |
| `fixpp_session_close` | B (no carrying declaration) | its return code depends on "established at least once"; `interpret_logon` stops at a malformed count, so a Logon in which a required field (HeartBtInt(108), or 35/49/56 when out of order) follows it is refused, and on the initiator path close returns `FIXPP_ERR_THREAD_SESSION_LIFECYCLE`, not `FIXPP_ERR_OK` (the change is in the handshake, not the call) | `version.h` history |
| `fixpp_session_is_established` | B (no carrying declaration) | as `fixpp_session_close`: for that refused Logon, on the initiator path, `true` becomes `false` | `version.h` history |
| `fixpp_session_acceptor_bound_endpoint` | U | no path | none |
| `fixpp_session_send` | B + A | `engine_->send` → `send_impl`'s `length_data_carry` over `session_hooks`: a malformed pair goes from `FIXPP_ERR_OK` to `FIXPP_ERR_APP_PAYLOAD_MALFORMED`, a count covering a following field is sent verbatim, and a well-formed SOH-bearing Data goes from `FIXPP_ERR_APP_PAYLOAD_MALFORMED` to `FIXPP_ERR_OK` | own note + B-091-4 additive |
| `fixpp_session_register_callback` | B | delivery: `parse_and_dispatch_`'s `Parser<Index>` + `scan_frame_header(session_hooks)`, so a malformed-pair frame goes from delivered to silently dropped | own note |
| `fixpp_session_register_send_callback` | B (covered) | the toApp view is built by `parse_and_dispatch_` (reader paragraph). `send_impl` returns the carry refusal before its toApp `parse_and_dispatch_`, so for a refused send the callback is not invoked | reader paragraph + `fixpp_session_send`'s note |
| `fixpp_version` | U (value bumps) | returns 1.9 by design: the declaration mechanism, not a pair effect | `version.h` |
| `fixpp_library_version` | U | no path | none |
| `fixpp_version_string` | U | no path | none |
