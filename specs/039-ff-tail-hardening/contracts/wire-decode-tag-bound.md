# Contract: inbound field-decode tag bound (US1)

This is the only behavioral contract changed by feature 039. US2–US5 add no contract (US2 pins an
existing one; US3/US4 are test/build-gate; US5 is doc).

## Scope

The inbound field decoder (both access modes) MUST NOT admit a field whose tag token's decimal value
exceeds the 16-bit FIX tag space, and MUST detect this before any fixed-width accumulator can wrap.

## Surfaces

- `fixpp::wire::OffsetTable::build` — `src/wire/offset_table.cpp` (Index/eager).
- `fixpp::wire::MessageView<Mode>::field_iterator::advance` — `include/fixpp/wire/parser.hpp`
  (Scan/lazy).

Both are internal (not public C++/C-ABI). No signature changes; no new symbols; no ABI impact.

Reference shape: the framer's `BodyLength` digit-accumulation guard (`src/wire/framer.cpp:120`) — a
pre-multiply bound check inside the digit loop — already implements this pattern (bound =
`max_frame_bytes`); the two tag-decode twins mirror it with bound `0xFFFF`.

## Pre / Post

**Pre**: an inbound frame's body bytes, scanned field by field; each field is `tag '=' value SOH`
(or Length+Data fixed-byte form).

**Post**:
- If every tag token in the frame is `0 < T ≤ 0xFFFF`: decode is **byte-identical** to pre-039
  behavior (SC-002). No conforming message changes.
- If any tag token `T > 0xFFFF` (including a value that would overflow the uint32 accumulator):
  - Index mode: `status() == wire_tag_out_of_range`, `entries` empty → every field reports absent.
  - Scan mode: iteration terminates at that field (`done_`); the field is never yielded.
  - In **neither** mode is the offending field queryable under any aliased small tag (SC-001/FR-005).

## Error code

Reuses `error::wire_tag_out_of_range` (`include/fixpp/wire/errors.hpp:55`,
`err_tag_out_of_range()`). **No new error code** (FR-003/FR-013).

## Witness (normative)

An adversarial test constructs a frame containing a forged tag token whose uint32 wrap aliases to a
chosen small, security-relevant tag (e.g. `4294967330` → 34), decodes it in **both** modes, and
asserts:
1. the decode fails closed per the mode's disposition above, and
2. no field is queryable / yielded under the aliased tag (34).
Plus a non-aliasing out-of-range token (e.g. `70000`) still rejects (regression of the pre-existing
`> 0xFFFF` path), and `65535` still decodes (boundary).
