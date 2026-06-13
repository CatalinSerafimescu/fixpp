# API / ABI / NFR Requirements Quality Checklist: Credential redaction at the message-store boundary

**Purpose**: Validate the surface-stability and non-functional requirements quality before implementation. Audience: Gate B reviewer.
**Created**: 2026-06-13
**Feature**: [spec.md](../spec.md)
**Focus**: "no new surface" claim, zero-alloc / same-length NFRs, coverage.

## API / ABI surface

- [x] CHK001 - Is the "no new wire field / error slot / config knob / codegen / C-ABI / store-interface" claim stated as an explicit requirement that Gate B can check against the diff? [Completeness, plan §Constitution Check] — PASS: plan.md §Summary and §Constitution Check explicitly state "No new surface: no wire field, no error-taxonomy slot, no codegen, no C-ABI, no `SessionConfig` field, no `MessageStore` pure-virtual. Gate-clean." Article X disposition records "No new exported symbol; `store_then_emit` is internal."
- [x] CHK002 - Is the test-seam bound (`kMaxMaskableLogonBytes`) requirement-bounded to a NON-public mechanism (`FIXPP_TEST_HOOKS`-gated accessor, no public `SessionConfig`/ctor/template), so Art. X is provably preserved? [Clarity, research §R3 / contracts §C2] — DD-DECIDED §R3/Gate-A-rd2: `kMaxMaskableLogonBytes` is a **fixed `static constexpr 256`**; the test seam is the `FIXPP_TEST_HOOKS`-gated `store_then_emit_test_access()` frame-injection **accessor**, not a bound override — NOT a public `SessionConfig` knob, public constructor parameter, or template parameter; precedent is `kRpBufSize` in the same function (confirmed in session.cpp:4731). Gate A round 2 judged this NOT a contradiction ("Session is non-template; store_then_emit private; FIXPP_TEST_HOOKS + kRpBufSize precedent"). plan.md Project Structure and T004 both re-state this constraint.
- [x] CHK003 - Is the masking required at a SINGLE store-entry seam (not per-backend), so the surface footprint and the "every backend inherits it" guarantee are both unambiguous? [Consistency, Spec §FR-009] — PASS: FR-009 states masking "applied **unconditionally at the single point where outbound frames enter the store**" and research R1 identifies that single point as `Session::store_then_emit`, confirmed by the grep showing it is the only site calling `store_->store()` for outbound frames.
- [x] CHK004 - Is it specified that the `open()`-time credential-length guard changes config-validation behavior (a rejection path), and is that the ONLY observable behavioral change at configuration time? [Completeness, contracts §C3] — PASS: contracts §C3 specifies the guard rejects when `username.size() + password.size() >= kMaxMaskableLogonBytes` — a one-directional **cred_len-only** sufficient-condition floor, **no Logon-overhead accounting** (matches `session.cpp:987-991` and corrected C3 `contracts/store-redaction.md:58-60`); plan.md §Summary confirms "No new … `SessionConfig` field" — the guard is a validation tightening of an existing `open()` path (extends 033 FQ-1), not a new API surface.

## Performance / allocation NFRs

- [x] CHK005 - Is "zero-alloc on the persist path" quantified as a measurable requirement (same allocation COUNT vs baseline) with a named witness, not a vague "fast"? [Measurability, Spec §SC-004/FR-008] — PASS: SC-004 quantifies it as "same number of heap allocations as the pre-change baseline (no new allocation), verified under the allocation-tracking gate"; FR-008 states MUST NOT allocate; named witness `StorePath_NoNewAllocation` (mallocnesia + counting-resource) is in contracts §Test contract / T011.
- [x] CHK006 - Is the same-length-mask requirement stated with its downstream invariants (preserves `9=` BodyLength, store offsets, record CRC) so it is objectively verifiable? [Clarity, Spec §FR-003/SC-005] — PASS: FR-003 mandates same byte length; data-model.md INV-034-2 states "same length … `9=` BodyLength and the store's per-record CRC are valid"; research R5 details the CRC-over-masked-frame consistency; SC-005 requires the stored record be same length and retrievable as structurally valid.
- [x] CHK007 - Is the masked-copy storage location specified honestly (coroutine-frame-resident fixed-size, enlarges an existing allocation, adds zero new ones) rather than as "stack-local"? [Clarity, research §R3] — DD-DECIDED §R3/plan-§XV.1: research R3 explicitly states the buffer lives in "the existing `store_then_emit` coroutine frame — it enlarges that frame by ≤`kMaxMaskableLogonBytes`, adding **zero new allocations**"; plan.md §Constitution Check XV §1 and §Summary repeat "coroutine-frame-resident fixed-size storage." The claim is honest and precise; Gate A round 2 accepted it (the §VIII.5→§XV.1 cite sweep from RC3 corrected an earlier misleading cite).
- [x] CHK008 - Is the no-op requirement for the credential-absent / non-Logon path quantified as byte-identical to baseline, with the boundary (credentialed Logon is NOT byte-identical) stated? [Consistency, Spec §FR-007/SC-003] — PASS: FR-007 and SC-003 both state "byte-identical to current behavior" for credential-free / default / non-Logon paths; spec.md §US3 note (lines 72–76) and §Clarifications explicitly state the boundary — "The 'no observable change' guarantee therefore scopes to the **credential-free / default** cases above, not to a credentialed Logon on a non-persistent store." No inconsistency; the boundary is deliberate and documented.

## Coverage / testability

- [x] CHK009 - Is each Success Criterion (SC-001..005) tied to a named, objectively-checkable witness rather than a subjective outcome? [Measurability, contracts §Test contract] — PASS: contracts §Test contract provides a 9-row witness table mapping every SC to a named witness with a checkable assertion (SC-001/005 → `Persisted_LogonPassword_AbsentFromStoreFile_MaskPresent`; SC-002 → `Wire_LogonPassword_UnmaskedOnTransmit`; SC-003 → `CredentialFreeLogon_And_NonLogon_StoredByteIdentical`; SC-004 → `StorePath_NoNewAllocation`).
- [x] CHK010 - Is the dead defensive (over-bound) branch's coverage requirement specified (fault-injection seam earns BRDA, or a §IX.1 waiver), so it doesn't silently fail the coverage gate? [Coverage, Spec §quickstart / N1] — PASS: quickstart.md §Sanitizer/coverage explicitly states "The over-bound fail-closed branch … is covered by the `OverBound_SmallBoundSeam_SkipStoreButTransmit` fault-injection cell driving the frame-injection test seam — so it earns BRDA rather than needing a §IX.1 waiver." T010 in tasks.md implements this. The `FIXPP_TEST_HOOKS`-gated **frame-injection** accessor (`store_then_emit_test_access()`) is already an established project pattern (confirmed: `kRpBufSize` precedent in session.hpp / session.cpp:4758).
- [x] CHK011 - Is the FIX `10=` staleness in the stored masked frame documented as an accepted, bounded consequence (never re-validated/replayed) rather than left as an ambiguity? [Edge Case, research §R5] — DD-DECIDED §R5: research R5 states "The embedded FIX `10=` checksum of the stored frame is **not** recomputed and therefore no longer matches the masked body; this is **accepted and documented**: stored frames are never re-validated as FIX and never replayed (admin → `SequenceReset-GapFill`)." data-model.md INV-034-2 also records this ("FIX `10=` is intentionally stale — never re-validated; see research R5").

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 8 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 3 |
| WAIVED | 0 |
| **Total** | **11** |

### SPEC-FIXED items
_(none)_

### DD-DECIDED items
- CHK002 — anchor `research §R3 / Gate A round 2`; rationale: test **accessor** (`store_then_emit_test_access`) scoped to FIXPP_TEST_HOOKS-only; the **bound** is a fixed `static constexpr 256` (unconditional, not under any `#if`); non-public surface with `kRpBufSize` precedent; Gate A round 2 explicitly judged this not a contradiction.
- CHK007 — anchor `research §R3 / plan §Constitution Check XV.1`; rationale: coroutine-frame-resident storage (not stack-local) is accurately stated in both research and plan; the §VIII.5→§XV.1 cite correction was applied in Gate A round 1 RC3.
- CHK011 — anchor `research §R5 / data-model.md INV-034-2`; rationale: FIX `10=` staleness in masked stored frame is accepted and bounded (never re-validated, never replayed); documented in both research and data-model.

### WAIVED items
_(none)_

Anchors spot-verified: `plan.md §Constitution Check/§Summary`, `spec.md §FR-009/FR-007/FR-003/SC-001..005`, `research.md R3/R5`, `contracts/store-redaction.md C3/§Test contract`, `data-model.md INV-034-2`, `quickstart.md §Sanitizer/coverage` — all resolve in the Gate A round 2 converged bundle (2026-06-13).

### Realizability sub-check
The only entity introduced is the free function `mask_tag554_same_length_inplace(std::span<std::byte>) noexcept` in `logon_credentials.hpp`. The `std::array<std::byte, kMaxMaskableLogonBytes>` buffer lives in the `store_then_emit` coroutine frame — a complete type with a constexpr size. No struct/class is added with `unique_ptr`-to-incomplete or by-value incomplete members. **Clean — no latent incomplete-type defect.**
