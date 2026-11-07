# Data model: 101 inbound Reject fidelity

Decisions: [research.md](research.md) D1–D7.

## `FrameHeader` (`src/session/scan_frame_header.hpp`)

| Member | Today | After |
|---|---|---|
| `msg_type` | last 35 | **first** 35 (the third field on every frame a state arm handles; 093 disregards a frame whose third field is not 35). Supersedes 092 E-1 / C-3 I-3 for this member |
| `msg_type_conflict` | — | **new** `bool`: any later 35 whose value differs from `msg_type` (sticky) |
| `msg_seq_num` | last 34 | unchanged (last-wins) |
| `msg_type_is_third`, `fault_ref_msg_type` | unchanged | unchanged |

`kMaxShippedMsgTypeLength` is deleted (D7).

## `table_view` (`include/fixpp/dict/table_view.hpp`)

| Query | Meaning |
|---|---|
| `valid_tags_for(msg_type).found()` | **new** on `valid_tag_set_view`: `valid_` holds this MsgType. The validator's one `valid_.find` per message answers both the MsgType check and the field walk |
| `tag_defined(tag)` | **new**: the tag is in the dictionary's merged field store (dictionary-built), or was named by any population operation (hand-built). A sorted `std::vector<std::uint16_t>`, binary search, sealed with the table; never a fixed tag-space bitset |

## `core::error` (`include/fixpp/core/error.hpp`)

New slots are appended after the last enumerator at implement time; their values are pinned by the 101 completeness test,
not here.

| Slot | 373 | 371 | C-ABI |
|---|---|---|---|
| `wire_invalid_msg_type` (new) | 11 | none | `FIXPP_ERR_WIRE_CONFORMANCE` |
| `wire_undefined_tag` (new) | 0 | the tag | `FIXPP_ERR_WIRE_CONFORMANCE` |
| `wire_unexpected_tag` | 2 | the tag (now: defined, not in this message) | unchanged |
| `wire_invalid_tag_number` | 0 | none (malformed tag, 092) | unchanged |

## Reason text

`session_reject_reason_text(reason)` → QuickFIX C++'s `SessionRejectReason_*_TEXT` for 0, 1, 2, 4, 5, 6, 11, 13, 14;
empty otherwise. Called by the three validation-gate Reject sites only. `Session::RejectDecision` is unchanged.

## Reject build helper (internal, `src/session/session.cpp`)

Build with 372; on `wire_field_value_truncated` with a non-empty MsgType, build once more without 372; otherwise today's
handling. A twin for the BusinessMessageReject. Runs after `peek_outbound()` and the stamp, before `assign_outbound()`.
