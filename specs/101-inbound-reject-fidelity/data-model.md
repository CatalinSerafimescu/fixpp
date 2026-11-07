# Data model: 101 inbound Reject fidelity

## `FrameHeader` (`src/session/scan_frame_header.hpp`)

| Member | Today | After |
|---|---|---|
| `msg_type` | last 35 | **first** 35 (the third field on every frame a state arm handles; 093 disregards a frame whose third field is not 35) |
| `msg_type_conflict` | — | **new** `bool`: a later 35 whose value differs from `msg_type` |
| `msg_type_is_third`, `fault_ref_msg_type` | unchanged | unchanged |

## `table_view` (`include/fixpp/dict/table_view.hpp`)

| Query | Meaning |
|---|---|
| `has_msg_type(msg_type)` | **new**: the table defines this MsgType (`valid_` holds it) |
| `tag_defined(tag)` | **new**: the dictionary's field store defines the tag (bitset over the tag space, built with the table) |

## `core::error` (`include/fixpp/core/error.hpp`)

| Slot | 373 | 371 | C-ABI |
|---|---|---|---|
| `wire_invalid_msg_type` (new) | 11 | none | `FIXPP_ERR_WIRE_CONFORMANCE` |
| `wire_undefined_tag` (new) | 0 | the tag | `FIXPP_ERR_WIRE_CONFORMANCE` |
| `wire_unexpected_tag` | 2 | the tag (now: defined, not in this message) | unchanged |
| `wire_invalid_tag_number` | 0 | none (malformed tag, 092) | unchanged |

## Reason text

`session_reject_reason_text(reason)` → QuickFIX C++'s `SessionRejectReason_*_TEXT` for 0, 1, 2, 4, 5, 6, 11, 13, 14, 16;
empty otherwise. Passed by the three validation-gate Reject sites only.
