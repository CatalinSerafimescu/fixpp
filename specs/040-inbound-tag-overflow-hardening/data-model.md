# Phase 1 Data Model: Inbound tag-overflow hardening

**No new entities or persisted structures.** The only new artifact is one stateless helper function
and the invariant it enforces.

## The bounded-tag accumulate helper

`accumulate_tag_digit(uint32_t& tag, unsigned char c) -> bool` (`include/fixpp/wire/tag_scan.hpp`):

| Input state | Action | Returns |
|-------------|--------|---------|
| `tag ≤ (0xFFFF - (c-'0'))/10` | `tag = tag*10 + (c-'0')` | `true` |
| `tag > (0xFFFF - (c-'0'))/10` (would exceed 16-bit tag space) | no overflow of the accumulator; `tag` left at its in-progress value | `false` |

Properties: `constexpr`, `noexcept`, no allocation, no side effects beyond `tag`. The pre-multiply
form never itself overflows `uint32`.

## Per-site disposition on `false` (unchanged from today)

| # | Site | Disposition on overflow |
|---|------|-------------------------|
| 1 | `OffsetTable::build` (Index) | `status_ = err_tag_out_of_range(); entries_.clear(); return;` → whole message all-fields-absent |
| 2 | `field_iterator::advance` (Scan) | `done_ = true; return;` → iteration terminates; field never yielded |
| 3 | `interpret_logon` | `goto next_field;` → skip the forged field, continue |
| 4 | `scan_first_frame_ids` | `tag_ok = false;` → field not used for resolution |
| 5 | `scan_frame_header` | `tag_ok = false;` → field skipped; required-field-absence handled downstream |

## Security invariant (SC-001)

For every tag token `T` arriving at sites 1–5: if `T > 0xFFFF` (including any value that would wrap a
`uint32` accumulator), the field is disposed per the table above and is **never surfaced/queryable
under any tag** `t ≤ 0xFFFF`. In particular the verified vectors `429496729634` (→34) and
`429496729649` (→49) are rejected at all five sites.

## Out of scope (no model change)

- Site 6 `build_replay_frame` — stored own-outbound; documented exclusion (FR-008).
- Non-tag value/length/seqnum/checksum accumulators — unchanged.
