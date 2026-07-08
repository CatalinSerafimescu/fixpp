# Contract: Builder Shape-Oracle (what FR-015a must reproduce)

The 5 exemplar builders + `wire::body_builder` are the **write shape-oracle**: the frozen reference the
follow-on codegen writer-emitter (FR-015a) must regenerate byte-for-byte against. This contract states the
observable guarantees, mirroring how the read side is pinned by `contracts/generated_message.hpp`.

## C1 — Body-only output framing (INV-2)

For any exemplar builder invoked with valid inputs and a sufficient span, the returned byte span:
- begins with `35=<MsgType>\x01` (MsgType may be multi-char, e.g. `AS`);
- contains ONLY business-body fields after that, each `tag=value\x01`;
- contains NONE of the session/framing tags `8, 9, 34, 49, 52, 56, 10`.

## C2 — Canonical field encoding (INV-3)

- Decimals via `decimal_t::format`: locale-independent, no scientific notation, trailing-zero canonical
  (`190.50` and `190.5` encode identically).
- Chars are single bytes; ints are ASCII with no leading zeros/sign padding beyond the value.

## C3 — Repeating-group grammar

- A group emits `No<Group>=<N>\x01` (the exact instance count) FIRST, then `N` instances back-to-back.
- Each instance leads with the group's **delimiter field** (first field of the entry) and lists its fields
  in dictionary order (INV-4 delimiter-first).
- Nested groups appear inside their parent entry at the correct depth (E: `73` → `453` → `802`).
- `N == 0`: `No<Group>=0\x01` is emitted (present-but-empty), consistent with the read-side accept
  behavior (TC-018 / B-004-7). [Groups that are entirely optional-and-absent emit nothing — a builder that
  is never asked to open the group omits it.]

## C4 — Fail-closed atomicity (INV-4)

- Invalid input (empty required string, control byte / SOH in a value, out-of-range char, unformattable
  decimal, malformed UTCTimestamp) OR insufficient output span → a typed error (`wire_*`) and the caller
  span is left untouched (no partial write).
- `commit()` with any group still open → typed error (LIFO not balanced).

## C5 — Round-trip + external-golden agreement

- **Round-trip**: builder output → framed → dict-backed `Parser<Index>::parse` → typed flyweight → every
  seeded field (incl. nested group entries) reads back at its exact input value.
- **External golden**: the builder body byte-matches the checked-in QuickFIX-authored body-only golden
  (decimal-by-value; non-deterministic tags normalized/excluded). This is the independent oracle — round-
  trip alone is insufficient (non-tautological requirement).

## C6 — No new public surface beyond the declared builders + `body_builder`

- No new `fixpp_error_t` value, no C-ABI change, no wire-format-semantics change, no codegen-layout change
  (FR-009). The read path is the already-generated 062/063 flyweight surface, unchanged.

## FR-015a acceptance (future, informational)

When FR-015a generates a builder for any of these 5 MsgTypes in v44, its output MUST satisfy C1–C5 against
the SAME seed — i.e. the generated builder body equals the exemplar builder body (and both equal the
golden). That equality is the shape-oracle's reason to exist.
