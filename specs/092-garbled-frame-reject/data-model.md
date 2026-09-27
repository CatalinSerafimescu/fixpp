# Data model: 092-garbled-frame-reject

## E-1 — `FrameHeader` fault record (private, `src/session/scan_frame_header.hpp`)

New members, appended to the existing struct:

| Member | Type | Meaning |
|---|---|---|
| `fault` | `enum class scan_fault : std::uint8_t { none, malformed_tag, length_data_mismatch }` | first field the scan could not read |
| `fault_length_tag` | `std::uint16_t` | the Length tag, when `fault == length_data_mismatch`; else 0 |
| `fault_offset` | `std::uint32_t` | byte offset of the faulty field's first byte (log line only) |

**Fault kinds, and which scan branch sets each:**
- `malformed_tag`: a non-digit tag byte; an empty tag (`=` first); a tag above 0xFFFF; a field with
  no `=` before SOH or before the end of the frame. Today each of these either `continue`s past
  the field or scans as tag 0.
- `length_data_mismatch`: `length_data_carry::read_value` returns nullopt (the counted extent runs
  past the frame, or is not followed by SOH). Today this `return h`s without a record.

**Rules:**
- On the first fault the scan returns immediately. Every member not yet assigned stays empty, so
  "read before the fault" is exactly `!member.empty()`.
- A frame with no fault leaves `fault == none`, and every existing member is populated exactly as
  today (C-3 I-3).
- `sizeof(FrameHeader)` grows. Check for size pins before changing it: memory says size pins live in
  `tests/` too (`grep -rn "sizeof(FrameHeader)\|FrameHeader) ==" src tests`).

## E-2 — Disposition (private, `Session::dispose_unparseable_`)

`enum class unparseable_disposition { continue_processing, handled }`. The state arm returns when the
result is `handled`. The mapping from (state, `hdr`) to action is `contracts/unparseable-frame-disposition.md` C-2.

## E-3 — Dispatch result (private)

`parse_and_dispatch_` returns `core::expected_t<void>` today. It gains a way to say "parse failed,
callback not run" (either a dedicated `core::error` value used only inside the session, or a small
result enum), so each call site handles it (research R-4). Nothing changes on the success path.

## E-4 — Validator fault (public, additive)

- The field iterator (`include/fixpp/wire/parser.hpp`) records why it set `done_`: end of frame, or
  one of the two fault kinds.
- `dictionary_driven_validator::validate` maps a fault to `wire_invalid_tag_number` or
  `wire_length_data_mismatch`, with `*ref_tag_out` = the Length tag for the latter.
