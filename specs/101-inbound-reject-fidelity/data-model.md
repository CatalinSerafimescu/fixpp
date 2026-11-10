# Data model: 101 inbound Reject fidelity

Decisions: [research.md](research.md) D1–D7.

## `FrameHeader` (`src/session/scan_frame_header.hpp`)

| Member | Today | After |
|---|---|---|
| `msg_type` | last 35 | **first** 35 (the third field on every frame a state arm handles; 093 disregards a frame whose third field is not 35). Supersedes 092 E-1 / C-3 I-3 for this member |
| `msg_type_conflict` | — | **new** `bool`: any later 35 whose value differs from `msg_type` (sticky) |
| `msg_seq_num` | last 34 | unchanged (last-wins). So a validator Reject (373=0, 2, 11 and the rest) on a frame with two 34s carries 45 = the last 34, as today, while a frame-level refusal (`names_third_field`: the conflict Reject, 099's 1128 refusals) carries 45 = the first 34 (`fault_ref_seq_num`). The two rules differ on purpose: a refusal names the frame by its third field, as 099 does |
| `msg_type_is_third`, `fault_ref_msg_type` | unchanged | unchanged |

`kMaxShippedMsgTypeLength` is deleted (D7).

## `table_view` (`include/fixpp/dict/table_view.hpp`)

| Query | Meaning |
|---|---|
| `valid_tags_for(msg_type).found()` | **new** on `valid_tag_set_view`: `valid_` holds this MsgType, i.e. `add_valid(_tag)` or `add_required(_tag)` named it (a group-context operation does not). The validator's one `valid_.find` per message answers both the MsgType check and the field walk |
| `tag_defined(tag)` | **new**: the tag is in the dictionary's merged field store (dictionary-built), or was named by any population operation (hand-built). A sorted `std::vector<std::uint16_t>`, binary search, sealed with the table; never a fixed tag-space bitset as the stored form (*Updated (101 T067):* the seal writes it from a build-time bitset, which it releases) |

## `core::error` (`include/fixpp/core/error.hpp`)

New slots are appended after the last enumerator at implement time; their values are pinned by the 101 completeness test,
not here.
Their message strings (`core::error_message`, and `to_string`, which returns the same view) follow the rule of the neighbouring `wire_*` slots: `wire: ` and a lower-case phrase naming the result, as `wire_unexpected_tag`'s does; the 101 completeness test pins the strings T021 writes, read from the code, not from here.

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

The full sequence is research D7's; this table summarises it.

| Step | Session Reject | BusinessMessageReject (twin) |
|---|---|---|
| 1 | build with 372 into the site's buffer | the same |
| 2 | on `wire_field_value_truncated`: a non-coroutine helper rebuilds with 372 into `inbound_reply_slot_` (a `std::array<std::byte, Session::kSendFrameBytes>` member of `Session`), taking the peeked number and the SendingTime value as arguments | the same |
| 3 | on `wire_field_value_truncated` again, MsgType non-empty: rebuild in the slot without 372 | none: 372 is required |
| 4 | still failing: not sent (no callback, no outbound number); one `FIXPP_SLOG` record at most per max(HeartBtInt, 1 s), HeartBtInt being the configured value (the same window before Logon), naming the type not sent, the inbound MsgType's length (never the value) and the count suppressed since the previous record (research D7, "Outbound-ceiling ending") | not sent, the same record; **one** rate state and suppressed count shared with the Reject's |
| tail | the site's own tail (`assign_outbound`, `fire_to_admin_`, `store_then_emit`, its own failure handling), unchanged, in the site's own coroutine, over the span that holds the frame | `toApp` (veto, throw), `assign_outbound`, `store_then_emit`; the same |

Every attempt runs after `peek_outbound()` and the stamp and before the first callback and `assign_outbound()`. The
rebuild helper's body holds `static_assert(kSendFrameBytes <= CaptureVisitor::kCapBufSize)`, so it is defined after
`CaptureVisitor` in `session.cpp`.

**Slot state.** An RAII holder holds the slot from the rebuild until the tail returns (after `store_then_emit`), and
releases it on every exit. `bool inbound_reply_slot_held_` is kept in every build type, and
`FIXPP_ASSERT_INBOUND_REPLY_SLOT_FREE()` asserts it false where the holder takes the slot. Precondition: each
`on_inbound_frame` completes before the next on that Session (research D7). No allocation is added.
