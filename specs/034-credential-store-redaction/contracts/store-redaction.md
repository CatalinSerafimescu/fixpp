# Contract — store-boundary credential redaction (034)

fixpp is a library; the relevant "contract" is the **internal** behavior of the store-entry seam plus the
new byte-utility. No public/exported API changes (Article X — no ABI surface).

## C1 — `mask_tag554_same_length_inplace(std::span<std::byte> frame) noexcept -> bool`

**Preconditions**
- `frame` is a mutable view over a private copy of a FIX frame's bytes (NOT the caller's wire buffer).

**Postconditions**
- Every genuine SOH-delimited `554` field value in `frame` is overwritten with `'*'`, same length.
- No other byte changes; `frame.size()` unchanged.
- Returns `true` iff ≥1 field was masked.
- No allocation, no throw.

**Negative / no-op**
- No genuine `554` field → returns `false`, `frame` unmodified.
- Already-masked input → returns `true`/`false` per match but bytes are unchanged (idempotent).

## C2 — `Session::store_then_emit` masking behavior

**Given** an outbound frame `frame` (the builder's bytes) and `stamped_seq`:

1. **Maskability gate** (no allocation in the negative path):
   - If `frame` contains no `\x01554=` (and no leading `554=`) → **not maskable**: store `frame` as-is
     (today's behavior, byte-identical). *(Covers every non-Logon frame and credential-free Logon.)*
   - Else confirm `MsgType(35) == "A"`. If not `A` → not maskable, store as-is.
2. **Mask (maskable path only)**:
   - If `frame.size() > kMaxMaskableLogonBytes` → **fail closed**: skip the store write for this frame
     (logged-then-proceed, I-07); never persist cleartext. *(Structurally unreachable given the `open()`
     credential-length guard — defensive.)*
   - Else copy `frame` into a stack `std::array<std::byte, kMaxMaskableLogonBytes>`, call
     `mask_tag554_same_length_inplace(span(buf, frame.size()))`, and `co_await store_->store(stamped_seq,
     span(buf, frame.size()), outbound)`.
3. **Transmit**: Step 2 of `store_then_emit` transmits the **original** `frame` (unmasked) exactly as today.

**Invariants preserved**: I-3 (durable-before-transmit) — masking is synchronous and completes before the
`co_await store`; the store still commits before transmit. The async-mutex and cancellation/`noexcept`
handling of `store_then_emit` are unchanged (masking adds no suspension point).

## C3 — `open()`-time credential-length guard (extends 033 FQ-1)

**Given** a `SessionConfig` with FIXT credentials, `open()` (the FIXT-config validation site) rejects /
fails configuration if the configured `username` + `password` (plus fixed Logon overhead) could produce a
Logon frame exceeding `kMaxMaskableLogonBytes`. This makes C2 step-2's over-bound branch unreachable in
practice (keeps masking always on the zero-alloc stack path).

## C4 — Documentation contract (FR-010)

- `spec/behaviors-and-limitations.md`: **L-033-6** flips limitation → mitigation; add the R7 forward
  constraint as a new limitation (future verbatim admin-replay must re-derive creds from config).
- `specs/033-fixt-fix50sp2-session/tasks.md`: a **dated correction note** under T024/T020 — the "no
  production frame persistence exists" claim was incomplete (overlooked the 008 store). No history rewrite.
- `feature-catalogue.md` + `coverage-index.md`: add the 034 traceability row.

## Test contract (witnesses)

| Witness | Asserts | Maps to |
|---|---|---|
| `Persisted_LogonPassword_AbsentFromStoreFile_MaskPresent` | open FIXT session w/ known password + **FileStore**, drive logon, **read store file bytes from disk** → cleartext absent AND same-length `'*'` run present | US1 / SC-001 / SC-005 |
| `Wire_LogonPassword_UnmaskedOnTransmit` | captured/transmitted Logon's `554` == configured cleartext; session establishes | US2 / SC-002 |
| `Acceptor_ReplyLogon_PasswordMaskedInStore` | acceptor reply Logon stored masked (both roles) | US1 AC3 / FR-004 |
| `CredentialFreeLogon_And_NonLogon_StoredByteIdentical` | credential-free Logon + a non-Logon frame stored == pre-change bytes | US3 / FR-007 / SC-003 |
| `InMemoryStore_CredentialedLogon_AlsoMasked` | credentialed Logon in a MemoryStore is masked (uniform backend) | clarification / INV-034-4 |
| `StorePath_NoNewAllocation` (mallocnesia + counting-resource) | persist path allocates the same as baseline | SC-004 / FR-008 |
| `Masker_SameLength_FieldAnchored_unit` | `mask_tag554_same_length_inplace`: same length, only genuine 554 masked, decoy `554=` in free-text untouched, idempotent | C1 / I-E1-* |
