# Phase 1 Data Model: Validation Gate Wiring

No new persisted/wire entities. The "entities" here are configuration and in-memory adapter structures.

## E-1 — Inbound validation toggle (SessionConfig)

- **Field**: `bool validate_inbound_messages` on `SessionConfig` (`session_config.hpp`), placed after `validate_sequence_numbers`.
- **Default**: `false` (FR-001 — opt-in; preserves the byte-identical default path).
- **Validation rule**: if `true`, `SessionConfig::dictionary` MUST be non-null (R-6 / FR-011); else `register_session` rejects the config with `core::error::invalid_session_config` (slot 53, `error.hpp:148`).
- **Polarity note**: unlike `check_comp_id` / `validate_sequence_numbers` (default **true**, "false relaxes"), this flag defaults **false** ("true tightens") — it adds a new strictness rather than relaxing an existing one.

## E-2a — `dict::field_type` (production enum — NEW)

- **Type**: `enum class field_type : std::uint8_t { String, Int, Float, Char, Boolean, Data, Length };` (the 7-value enum the validator switches on at `validator.hpp:295`).
- **Buildability blocker (RC-A)**: this enum currently exists ONLY in the test mock (`tests/support/mock_dict_table.hpp:30`); a grep for `enum class field_type` across `include/` returns nothing. It MUST be given a **production home** in the dict layer (`include/fixpp/dict/field_type.hpp`), distinct from the 29-value `field_data_type` (`field_ref.hpp:29`). `validator.hpp` must include it for a complete type (not rely on the mock being included first), with a **compile-witness TU** proving the production validator instantiates without the mock.

## E-2 — `dict::table_view` (production realization — NEW)

- **Type**: a move-only value type owning its tables; the validator binds it by value (`validator.hpp:86`) and the class is header-only by design. **Like `field_type`, no production `table_view` exists today** (only the test mock) — this is the second half of RC-A. Define it in `include/fixpp/dict/table_view.hpp` and include it from `validator.hpp` for a complete type.
- **Surface** (the 6 methods the validator calls):
  - `bool field_valid_for(string_view msg_type, uint16_t tag) const noexcept`
  - `span<uint16_t const> required_fields(string_view msg_type) const noexcept`
  - `uint16_t group_first_field(uint16_t no_tag) const noexcept`
  - `span<uint16_t const> group_member_tags(uint16_t no_tag) const noexcept`
  - `field_type field_type_of(uint16_t tag) const noexcept`  (`field_type` = the E-2a 7-value enum)
  - `bool enum_valid(uint16_t tag, span<const byte> value) const noexcept` → **always `true`** (Phase-1)
- **Backing storage** (built once by `as_table_view()`):
  - valid-tag set per msg_type, required-tag list per msg_type, group-first per no_tag — copied/forwarded from `Dictionary`.
  - group-member `uint16_t` arrays per no_tag — projected from `Dictionary::group_fields` `FieldRef` spans.
  - global tag→`field_type` map — `field_data_type`(29) mapped to `field_type`(7). **Assumes** a tag's `field_data_type` is invariant across msg_types (`Dictionary` has only `field(msg_type, tag)`, no global `field(tag)`); confirm/derive at implementation (R-1a).
- **Lifetime**: constructed at validator/session setup; immutable thereafter; no per-message mutation/allocation.

## E-3 — `Dictionary::as_table_view()` builder

- **Signature**: `[[nodiscard]] table_view Dictionary::as_table_view() const;` (realizes the deferred method noted at `dictionary.hpp:18`).
- **Behaviour**: populates an `E-2` `table_view` from `this` Dictionary, with `enum_valid` stubbed `true`.

## E-4 — Validation-error → SessionRejectReason mapping

- **Form**: a pure `constexpr` function `wire_error → SessionRejectReason` (`reject_reason_map.hpp` or inline in `session.cpp`).
- **Mapping** (R-3, faithful to what the validator emits):
  - `wire_header_out_of_order`(39) → **14** (header field out of standard order)
  - `wire_unexpected_tag`(42) → **2** (tag not defined for msg type)
  - `wire_required_field_missing`(38) → **1** (required field missing **and all repeating-group structure failures** — the validator surfaces group failures as `wire_required_field_missing`; no distinct group reason in Phase-1)
  - `wire_field_value_out_of_range`(40) → **5** (value not type-conformant — emitted by the type arm; the enum arm is dead Phase-1)
  - `wire_field_value_truncated`(41) → **6** (Float/decimal precision-loss ONLY — never a generic bad-format value)
- **Reject emission (RC-C correction)**: `build_reject` (`admin_messages.cpp:613`) ALREADY accepts `ref_tag_id` + `session_reject_reason` and emits `371`/`373` — reuse it UNCHANGED. The work is to extend/overload the **caller** `emit_session_reject_` (`session.cpp:1597`, today hardwired `RefTagID=0, reason=3`) to thread the mapped reason (and optional offending `RefTagID`) through.
- **Consumers**: the inbound validate gate, when `validate()` returns a `wire_*` error, maps it and calls the (extended) `emit_session_reject_`.

## E-5 — Engine clock-config gate

- **Input**: `EngineConfig::clock` (`std::shared_ptr<Clock>`, `engine_config.hpp:127`).
- **Rule**: `validate_engine_config(cfg)` returns `clock_not_set` iff `clock == nullptr`.
- **Enforcement point**: top of `Engine::start()` (now `expected_t<void>`); error returned before any `co_spawn`.

## State / flow (enabled path)

`on_inbound_frame` is a `switch (fsm_state_)` with no shared post-framing point (R-2/R-4). The gate is inserted **per-arm** in each inbound-processing state; the `LogoutSent`/`Disconnected` drain arms are NOT touched. The MessageView build is therefore duplicated into each validating arm (New-6).

The Logon-bearing arms (`NotConnected` `session.cpp:1712`, `LogonSent` `session.cpp:3341`) run `interpret_logon()` (`admin_messages.cpp:217` — the CompID/BeginString/MsgType establishment check, silent-Disconnect on failure with NO `Reject`) as the **lead statement**, structurally BEFORE `scan_frame_header`, the SendingTime guard (NotConnected `Reject(reason=10)` ~1825), the `1137`/hydrate block, and `check_inbound` (the seqnum gate ~1890 / ~3440). So in those arms the gate must run **before `interpret_logon()`** — validate-first — or a dict-invalid + CompID-failing Logon hits the existing silent Disconnect instead of `Reject(35=3)` (FR-003). The `LogonReceived`/`Active` arms have no `interpret_logon()` lead; there the gate runs after `scan_frame_header`, before `check_inbound`.

```
on_inbound_frame(frame)
  switch (fsm_state_):

    case NotConnected | LogonSent:                            // LOGON-BEARING arms
        if validate_inbound_messages:                         // VALIDATE FIRST
            build MessageView<Index>                          // per-arm
            r = validator.validate(mv, scratch)
            if !r and msg_type ∉ {35=3, 35=5}:                // preserve no-reject-loop exemption
                emit_session_reject_(seq, msg_type, map(r.error()) [, refTag]);  return  // no seqnum advance, not established
        → interpret_logon(...)        // [existing] CompID/BeginString/MsgType — silent Disconnect on refusal
        → [existing] scan_frame_header / SendingTime guard / 1137 / check_inbound (seqnum gate)
        → parse_and_dispatch_(...)    // dispatch (re-parses in MVP)

    case LogonReceived | Active:                              // STEADY-STATE processing arms
        scan_frame_header
        if validate_inbound_messages:
            build MessageView<Index>                          // per-arm
            r = validator.validate(mv, scratch)
            if !r and msg_type ∉ {35=3, 35=5}:                // preserve no-reject-loop exemption
                emit_session_reject_(seq, msg_type, map(r.error()) [, refTag]);  return  // no seqnum advance
        → [existing] CompID / check_inbound (seqnum gate) / msg-type-for-state
        → parse_and_dispatch_(...)    // dispatch (re-parses in MVP)

    case LogoutSent | Disconnected:                           // DRAIN arms — UNCHANGED
        drain non-Logout inbound (no validation, no seqnum advance, no dispatch)
```

Default path (`validate_inbound_messages == false`): identical to current in every arm — the validate block is skipped entirely.

### Logon-arm overlap precedence (C-2/C-3 witness rows)

The governing principle: **validation rejects iff the message violates the dictionary; a dict-clean message keeps its existing disposition unchanged** (FR-010). Each overlap row is an application of that rule (validate runs first in the Logon arms):

| inbound first/Logon message | disposition | why |
|---|---|---|
| (a) dict-invalid + BeginString mismatch | `Reject(35=3, 373=…)`, not established | validate-first; dict violation preempts `interpret_logon`'s silent Disconnect |
| (b) missing/invalid FIXT `1137` (dict-valid frame) | existing `1137`/establishment disposition | `1137` semantics are not a dict-structure violation → validation passes → existing check runs |
| (c-i) **absent** `52` SendingTime (dict marks `52` required for Logon) | validate-first `Reject(35=3, 373=1)` (required-field-missing), not established | absence of a required field IS a dict-structure violation → validator catches it (`wire_required_field_missing`) |
| (c-ii) **present-but-malformed** `52` value (e.g. `52=NOTATIME`), or a **dict-valid-but-stale** `52` | passes validation → falls to the existing SendingTime semantic path (038 guard): existing reason=10 / Logout-only — NOT a validate-first `Reject(35=3)` | `field_type_of(52)` collapses to `String` (the 7-value `field_type` enum has no timestamp type), and the validator's type arm does NO format check on `String`; a malformed *or* stale timestamp value is undetectable to the Phase-1 validator and stays semantic |
| (d) CompID-authz failure (dict-valid frame) | existing authz reject/disconnect | authz is semantic, not a dict-structure check → validation passes → existing disposition |
| (e) well-formed non-Logon first message | dict-valid → passes validation → `interpret_logon` refuses → silent Disconnect (unchanged) | FR-010: a dict-clean message keeps its existing disposition |
| (f) inbound `35=3`/`35=5`, malformed | no-reject-loop exemption skips the validate-reject; falls through to existing handling | FR-004 exemption preserved |

For a dict-invalid Logon the reject uses the establishment-appropriate reject/disconnect path (R-4 TASKS note); it is rejected and the session is not established.
