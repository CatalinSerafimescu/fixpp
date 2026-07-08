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
- **Byte-exact pin**: because both the external golden diff and the read-back parse compare decimals *by
  value*, neither can catch a wrong canonical *format*. Each exemplar's round-trip witness MUST therefore
  assert, for ≥1 decimal field, that the emitted `<tag>=<ascii>\x01` **bytes** equal the canonical expected
  bytes exactly (a direct byte compare, independent of the by-value golden diff and the parse round-trip).

## C3 — Repeating-group grammar (INV-5, enforced)

- A group emits `No<Group>=<N>\x01` (the exact instance count) FIRST, then `N` instances back-to-back.
- Each instance leads with the group's **delimiter field** and lists its fields in dictionary order.
- **Mechanism (not merely asserted):** `group_begin(no_tag, delimiter_tag)` carries the delimiter (author-
  supplied, no dictionary lookup). At `commit()` `body_builder` **fail-closed rejects** (a) any empty group
  instance and (b) any instance whose first field ≠ `delimiter_tag` — mirroring the C-ABI
  `validate_group_grammar` empty-instance + delimiter-first legs (`src/capi/message_write.cpp:682-701`,
  called from `fixpp_msg_commit:727`). `body_builder` remains a wire→core serializer with no
  `wire→dictionary` edge (the author, not a dictionary, supplies `delimiter_tag`).
- Nested groups appear inside their parent entry at the correct depth (E: `73`→`453`→`802`; the delimiters
  are `11`, `448`, `523` respectively).
- `N == 0`: `No<Group>=0\x01` is emitted (present-but-empty), consistent with the read-side accept
  behavior (TC-018 / B-004-7). The count-0 witness targets an **optional** group (`NoPartyIDs(453)`,
  `required='N'`), not the required `NoOrders(73)`. [Groups that are entirely optional-and-absent emit
  nothing — a builder never asked to open the group omits its `No<Group>` tag entirely; this is distinct
  from the `N == 0` present-but-empty case.]

## C4 — Fail-closed atomicity (INV-4) + grammar (INV-5)

- Invalid input (empty required string, control byte / SOH in a value, out-of-range char, unformattable
  decimal, malformed UTCTimestamp) OR insufficient output span → a typed error (`wire_*`) and the caller
  span is left untouched (no partial write).
- `commit()` with any group still open → typed error (LIFO not balanced).
- `commit()` on an empty group instance, or an instance whose first field ≠ `delimiter_tag` → typed error
  (INV-5, before any bytes reach `out`).
- **Test seam (direct pin, AC-2):** these negative cases have a named home —
  `tests/session/test_exemplar_build_failclosed.cpp` — with a discriminating witness per case: empty required
  string, control-byte/SOH in a value, out-of-range char/side, unformattable decimal, malformed
  UTCTimestamp, undersized buffer → **buffer untouched**, `commit()` with a group still open, empty group
  instance, wrong delimiter-first field. INV-4/INV-5 are marketed invariants and must be pinned directly,
  not only transitively.

## C5 — Round-trip + external-golden agreement

- **Round-trip**: builder output → framed → dict-backed `Parser<Index>::parse` → typed flyweight → every
  seeded field (incl. nested group entries) reads back at its exact input value.
- **External golden**: the builder body byte-matches the checked-in QuickFIX-authored body-only golden via
  the 061-specific `shape_oracle_profile()` — which excludes **no business tags** (only framing
  `{8,9,10,34,52}`, absent from a body-only golden), so `TransactTime(60)` and every seeded field are matched
  verbatim (decimal comparison stays by-value). This is NOT the interop `default_normalization_tags()`
  (`{9,10,34,52,60,112,122}`), which drops business tag `60`. This is the independent oracle — round-trip
  alone is insufficient (non-tautological requirement).
- **Byte-exact decimal (C2)**: complemented by the direct byte compare of ≥1 decimal field per exemplar, so
  canonical *format* (INV-3) is pinned independently of both by-value legs.

## C6 — No new public surface beyond the declared builders + `body_builder`

- No new `fixpp_error_t` value, no C-ABI change, no wire-format-semantics change, no codegen-layout change
  (FR-009). The read path is the already-generated 062/063 flyweight surface, unchanged.

## FR-015a acceptance (future, informational)

When FR-015a generates a builder for any of these 5 MsgTypes in v44, its output MUST satisfy C1–C5 against
the SAME seed — i.e. the generated builder body equals the exemplar builder body (and both equal the
golden). That equality is the shape-oracle's reason to exist.
