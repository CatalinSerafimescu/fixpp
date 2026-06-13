# Data Model — Credential redaction at the message-store boundary (034)

This feature adds **no new entity, type, field, or stored schema**. It defines one byte-utility and
one behavioral invariant over an existing data flow. "Data model" here = the masker's input/output
contract and the stored-frame invariant.

## E1 — `mask_tag554_same_length_inplace` (new byte utility)

- **Location**: `include/fixpp/session/logon_credentials.hpp` (sibling of `redact_tag554`).
- **Signature (intended)**: `inline bool mask_tag554_same_length_inplace(std::span<std::byte> frame) noexcept;`
  - Input: a mutable view over a **copy** of the frame bytes (never the caller's original/wire buffer).
  - Returns: `true` if at least one genuine `554` field value was masked, else `false`.
- **Field detection** (shared rule with `redact_tag554`): a genuine `554` field is `\x01554=` mid-frame,
  or `554=` at offset 0. A `554=` substring inside another field's free-text value is NOT a match.
- **Rewrite**: for each matched field, overwrite every value byte (from just past `554=` up to the next
  `\x01` or end-of-frame) with `'*'`. **Byte count is unchanged** (same-length).
- **Invariants**:
  - I-E1-1 (length): `frame.size()` is unchanged; no byte outside a 554 value is modified.
  - I-E1-2 (idempotent): masking an already-masked frame is a no-op (`'*'` runs stay `'*'`).
  - I-E1-3 (zero-alloc): no heap allocation; no exceptions (`noexcept`).
  - I-E1-4 (delimiter-safe): a 554 value cannot contain `\x01` or `=` (enforced at Logon build, 033 FQ-3),
    so the value extent is unambiguous and the mask cannot corrupt framing.

## E2 — Masked stored Logon record (invariant over the existing store)

The `MessageStore` schema is **unchanged**. The new invariant on the *content* of a stored outbound
`35=A` record carrying credentials:

- INV-034-1 **(masked-at-rest)**: the persisted bytes of an outbound `35=A` Logon contain no `554` value
  byte equal to the configured cleartext; the 554 value is a `'*'` run.
- INV-034-2 **(same-length / well-formed)**: the stored record has the same length as the unmasked frame;
  `9=` BodyLength and the store's per-record CRC are valid (FIX `10=` is intentionally stale — never
  re-validated; see research R5).
- INV-034-3 **(wire untouched)**: the bytes transmitted on the wire equal the original unmasked frame.
- INV-034-4 **(uniform across backends)**: holds for every store backend (FileStore, MemoryStore, null);
  masking is applied before the store call, not inside a backend (research R1/clarification).
- INV-034-5 **(no-op when nothing to protect)**: a frame with no genuine `554` field — i.e. every non-Logon
  frame and every credential-free Logon — is passed to the store **byte-identical** to today (no copy, no
  mask). FR-007 / SC-003.

## State / lifecycle

No new state machine. The masking is a stateless, synchronous transform on the persist path of a single
frame, inside `store_then_emit` (before the `co_await store`). No interaction with the session FSM, the
seqnum manager, hydrate/refresh, or reset logic.

## Relationships

- Depends on: the existing `store_then_emit` single store seam; the 033 FQ-3 injection floor (guarantees
  I-E1-4); the existing resend admin→GapFill fold (guarantees INV-034-3 is never violated on replay).
- Affects: `spec/behaviors-and-limitations.md` (L-033-6 limitation→mitigation + the R7 forward-constraint
  limitation); `feature-catalogue.md`/`coverage-index.md` (034 row).
