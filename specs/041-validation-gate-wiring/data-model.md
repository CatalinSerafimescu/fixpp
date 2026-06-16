# Phase 1 Data Model: Validation Gate Wiring

No new persisted/wire entities. The "entities" here are configuration and in-memory adapter structures.

## E-1 — Inbound validation toggle (SessionConfig)

- **Field**: `bool validate_inbound_messages` on `SessionConfig` (`session_config.hpp`), placed after `validate_sequence_numbers`.
- **Default**: `false` (FR-001 — opt-in; preserves the byte-identical default path).
- **Validation rule**: if `true`, `SessionConfig::dictionary` MUST be non-null (R-6 / FR-011); else the session config is rejected at registration.
- **Polarity note**: unlike `check_comp_id` / `validate_sequence_numbers` (default **true**, "false relaxes"), this flag defaults **false** ("true tightens") — it adds a new strictness rather than relaxing an existing one.

## E-2 — `dict::table_view` (production realization)

- **Type**: a move-only value type owning its tables; the validator binds it by value (`validator.hpp:86`).
- **Surface** (the 6 methods the validator calls):
  - `bool field_valid_for(string_view msg_type, uint16_t tag) const noexcept`
  - `span<uint16_t const> required_fields(string_view msg_type) const noexcept`
  - `uint16_t group_first_field(uint16_t no_tag) const noexcept`
  - `span<uint16_t const> group_member_tags(uint16_t no_tag) const noexcept`
  - `field_type field_type_of(uint16_t tag) const noexcept`  (`field_type` = 7-value enum)
  - `bool enum_valid(uint16_t tag, span<const byte> value) const noexcept` → **always `true`** (Phase-1)
- **Backing storage** (built once by `as_table_view()`):
  - valid-tag set per msg_type, required-tag list per msg_type, group-first per no_tag — copied/forwarded from `Dictionary`.
  - group-member `uint16_t` arrays per no_tag — projected from `Dictionary::group_fields` `FieldRef` spans.
  - global tag→`field_type` map — `field_data_type`(29) mapped to `field_type`(7).
- **Lifetime**: constructed at validator/session setup; immutable thereafter; no per-message mutation/allocation.

## E-3 — `Dictionary::as_table_view()` builder

- **Signature**: `[[nodiscard]] table_view Dictionary::as_table_view() const;` (realizes the deferred method noted at `dictionary.hpp:18`).
- **Behaviour**: populates an `E-2` `table_view` from `this` Dictionary, with `enum_valid` stubbed `true`.

## E-4 — Validation-error → SessionRejectReason mapping

- **Form**: a pure `constexpr` function `wire_error → SessionRejectReason` (`reject_reason_map.hpp` or inline in `session.cpp`).
- **Mapping** (R-3): 39→14, 42→2, 38→1, 40→5, 41→6.
- **Consumers**: the inbound validate gate, when `validate()` returns a `wire_*` error, maps it and calls the (extended) `emit_session_reject_`.

## E-5 — Engine clock-config gate

- **Input**: `EngineConfig::clock` (`std::shared_ptr<Clock>`, `engine_config.hpp:127`).
- **Rule**: `validate_engine_config(cfg)` returns `clock_not_set` iff `clock == nullptr`.
- **Enforcement point**: top of `Engine::start()` (now `expected_t<void>`); error returned before any `co_spawn`.

## State / flow (enabled path)

```
on_inbound_frame(frame)
  → frame / scan_frame_header
  → if validate_inbound_messages:
        build MessageView<Index>
        r = validator.validate(mv, scratch)
        if !r:  emit_session_reject_(seq, msg_type, map(r.error()));  return  // no seqnum advance
  → [existing] CompID / SendingTime / seqnum gate / msg-type-for-state
  → parse_and_dispatch_(...)  // dispatch (re-parses in MVP)
```

Default path (`validate_inbound_messages == false`): identical to current — the validate block is skipped entirely.
