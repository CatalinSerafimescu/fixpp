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
     (today's behavior, byte-identical). *(Covers credential-free Logons and frames without a 554; a
     non-Logon frame that DOES carry a genuine `\x01554=` is excluded by the `MsgType(35) == "A"` gate
     below, not by 554-absence.)*
   - Else confirm `MsgType(35) == "A"`. If not `A` → not maskable, store as-is.
2. **Mask (maskable path only)**:
   - If `frame.size() > kMaxMaskableLogonBytes` → **fail closed**: skip the store write for this frame
     (logged-then-proceed, I-07, consistent with the existing admin store-error model at
     `session.cpp:4417-4420`); never persist cleartext, but still transmit the original frame (Step 3).
     This is **wire-safe**: admin frames fold into a `SequenceReset-GapFill` on resend whether or not stored
     (`session.cpp:4744-4770`), so for a `35=A` Logon a skipped store is wire-identical to a stored one. The
     durable outbound counter is a separate cell (`persist_outbound_advance_` → `store_->next_seqnum`,
     `session.cpp:691`), untouched by skipping a frame store — no durable counter hole. *(`kMaxMaskableLogonBytes`
     is bound to the `build_logon` builder's maximum output capacity; combined with the C3 `open()`
     credential-length guard this branch is **production-unreachable** for any frame that survives the
     MsgType=A gate — covered dead defensive code, **test-coverable via the frame-injection seam**, see the
     fault-injection witness.)* *(Impl correction 2026-06-13: the BRDA is earned by a `FIXPP_TEST_HOOKS`
     `store_then_emit_test_access` accessor that injects a hand-crafted >256-byte `35=A` frame into the
     real branch — NOT the originally-proposed `FIXPP_TEST_LOGON_MASK_BOUND` compile override, which could
     not reach `store_then_emit` in `libfixpp_session`. See `plan.md ## Gate A` deviation #1 / research R3.)*
   - Else copy `frame` into a coroutine-frame `std::array<std::byte, kMaxMaskableLogonBytes>`, call
     `mask_tag554_same_length_inplace(span(buf, frame.size()))`, and `co_await store_->store(stamped_seq,
     span(buf, frame.size()), outbound)`.
3. **Transmit**: Step 2 of `store_then_emit` transmits the **original** `frame` (unmasked) exactly as today.

**Invariants preserved**: I-3 (durable-before-transmit) — masking is synchronous and completes before the
`co_await store`; the store still commits before transmit. The async-mutex and cancellation/`noexcept`
handling of `store_then_emit` are unchanged (masking adds no suspension point).

## C3 — `open()`-time credential-length guard (extends 033 FQ-1)

**Given** a `SessionConfig` with FIXT credentials, `open()` (the FIXT-config validation site) rejects /
fails configuration if the configured `username` + `password` (plus fixed Logon overhead) could produce a
Logon frame exceeding `kMaxMaskableLogonBytes`. The guard validates `cfg.logon_credentials`
**independent of role** — both initiator and acceptor sessions configure their own creds at `open()`,
emitted from `session.cpp:835` (initiator Logon) and `:2248` (acceptor reply Logon) respectively — so the
bound-exactness argument (and therefore the dead-over-bound branch) holds for the acceptor reply Logon too,
not just the initiator (FR-004 both roles; avoids the [[feedback_symmetric_api_claim_unreachable_arm]]
footgun). This makes C2 step-2's over-bound branch production-unreachable for both roles (keeps masking
always on the zero-alloc coroutine-frame path).

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
| `Acceptor_ReplyLogon_PasswordMaskedInStore` | acceptor reply Logon stored masked (both roles); exercises an **at-bound acceptor credential** so the C3 open()-guard / bound-exactness holds for the acceptor arm | US1 AC3 / FR-004 |
| `NonLogon_WithGenuine554_StoredUnchanged` | a `35`≠`A` frame carrying `\x01554=secret\x01` → stored **unchanged** (MsgType=A gate, not 554-absence, excludes it) | INV-034-5 / FR-006 / RC2 |
| `CredentialFreeLogon_And_NonLogon_StoredByteIdentical` | credential-free Logon + a non-Logon frame stored == pre-change bytes | US3 / FR-007 / SC-003 |
| `InMemoryStore_CredentialedLogon_AlsoMasked` | credentialed Logon in a MemoryStore is masked (uniform backend) | clarification / INV-034-4 |
| `OverBound_SmallBoundSeam_SkipStoreButTransmit` (fault-injection, injected small `kMaxMaskableLogonBytes`) | drives the dead over-bound branch via the test-seam bound; asserts (a) **no cleartext persisted**, (b) the **wire frame still carries the real 554**, (c) a resend over that seqnum → **GapFill** (not a masked verbatim replay) | C2 step-2 / I-07 / N1 (earns the over-bound BRDA) |
| `StorePath_NoNewAllocation` (mallocnesia + counting-resource) | persist path allocates the same as baseline | SC-004 / FR-008 |
| `Masker_SameLength_FieldAnchored_unit` | `mask_tag554_same_length_inplace`: same length, only genuine 554 masked, decoy `554=` in free-text untouched; **called twice → asserts byte-stability on the second pass** (idempotence I-E1-2) | C1 / I-E1-* |
