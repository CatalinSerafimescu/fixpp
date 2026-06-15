# Contract: bounded-tag accumulate helper

`include/fixpp/wire/tag_scan.hpp` — the single source of truth for the 16-bit FIX tag bound, shared
by all five live-inbound tag scanners.

## Signature

```cpp
namespace fixpp::wire {
[[nodiscard]] constexpr bool accumulate_tag_digit(std::uint32_t& tag, unsigned char c) noexcept;
}
```

- **Precondition**: `c` is an ASCII decimal digit (`'0'..'9'`). Callers already validate this before
  the accumulate step (each site rejects non-digits via its own path); the helper does not re-validate
  the character class.
- **Postcondition (returns `true`)**: `tag` updated to `old_tag*10 + (c-'0')`, which is `≤ 0xFFFF`.
- **Postcondition (returns `false`)**: appending `c` would make `tag` exceed `0xFFFF`; `tag` is NOT
  advanced past `0xFFFF` (no wrap occurs). The caller MUST dispose (reject/skip the field) per its own
  control flow and MUST NOT use `tag` as a valid tag.
- `constexpr`, `noexcept`, no allocation, no I/O, no global state.

## Invariants

- The helper NEVER lets the accumulator wrap: the bound is checked **before** the multiply, using the
  non-overflowing form `tag > (0xFFFF - (c-'0')) / 10`.
- Bound is exactly the 16-bit FIX tag space (`0xFFFF`). Tags above it are invalid by field width, so
  no legitimate tag is rejected.
- It is the ONLY place the tag bound is expressed (SC-004) — the five call sites carry no independent
  bound arithmetic after this feature.

## Consumers (all five live-inbound scanners)

`OffsetTable::build` (Index), `field_iterator::advance` (Scan), `interpret_logon`,
`scan_first_frame_ids`, `scan_frame_header`. Each calls it once per tag digit and disposes on `false`
per `data-model.md`.

## Reference shapes (already correct in-tree)

- `src/wire/framer.cpp:120` — `BodyLength` bound `if (body_length > ((max_frame_bytes - digit)/10))`.
- `src/session/session.cpp:1588` — seqnum bound with the boundary clause.
The helper generalizes these to the `0xFFFF` tag bound.

## Witness (normative)

- Helper unit test: `65535`→ok; `65536`→reject; wrap-and-continue (`429496729649`) → reject;
  zero-padded (`000000000034`) → ok (parses 34).
- Per-site: a wrap-and-continue token aliasing to a security-relevant tag for that site is rejected
  (forged field not surfaced under the aliased tag); conforming tags unchanged.
