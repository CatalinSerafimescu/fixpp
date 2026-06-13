---
description: "Task list — 034-credential-store-redaction"
---

# Tasks: Credential redaction at the message-store boundary

**Input**: Design documents from `specs/034-credential-store-redaction/`
**Prerequisites**: plan.md, spec.md (US1/US2/US3, FR-001..010, SC-001..005), research.md (R1–R8), data-model.md (E1, INV-034-*), contracts/store-redaction.md (C1–C4, 9 witnesses)

**Tests**: INCLUDED — this project is RED-first TDD (Article VII). Each behavioral task lands its witness RED before the production change makes it GREEN.

**Gate A**: converged round 2 (2026-06-13, user-signed-off). No residual P1/P2.

## Format: `[ID] [P?] [Story] Description`
- **[P]** = parallelizable (distinct file, no incomplete dep).
- Story labels on user-story phases only.

## Path conventions (from plan.md Project Structure)
- Masker utility: `include/fixpp/session/logon_credentials.hpp`
- Store-boundary branch + open()-guard: `src/session/session.cpp`
- Test-seam bound: `include/fixpp/session/session.hpp` (under `FIXPP_TEST_HOOKS`)
- New unit/integration tests: `tests/session/test_credential_store_redaction.cpp` (+ `tests/session/CMakeLists.txt`)
- Docs: `spec/behaviors-and-limitations.md`, `spec/feature-catalogue.md`, `spec/coverage-index.md`, `specs/033-fixt-fix50sp2-session/tasks.md`

---

## Phase 1 — Setup

- [ ] T001 Create the test file `tests/session/test_credential_store_redaction.cpp` (skeleton + includes mirroring `test_store_reset_crash_cut.cpp` for FileStore fixtures) and register the `credential_store_redaction_test` target in `tests/session/CMakeLists.txt` (wire `FIXPP_TLS_FIXTURE_DIR` only if a live-logon witness needs it; the disk-byte witnesses use a FileStore temp dir, no TLS).

---

## Phase 2 — Foundational (the standalone same-length masker — blocks all stories)

- [ ] T002 [P] RED unit `Masker_SameLength_FieldAnchored_unit` in `tests/session/test_credential_store_redaction.cpp`: asserts `mask_tag554_same_length_inplace` (a) masks a genuine `\x01554=secret\x01` value to an equal-length `'*'` run, (b) leaves `frame.size()` unchanged, (c) does NOT touch a decoy `554=` inside a `58=` free-text value, (d) is idempotent (call twice → byte-stable second pass, I-E1-2), (e) returns `false` + no change when no genuine 554. (C1 / E1 / I-E1-*)
- [ ] T003 Implement `inline bool mask_tag554_same_length_inplace(std::span<std::byte> frame) noexcept` in `include/fixpp/session/logon_credentials.hpp` (sibling of `redact_tag554`; same `\x01554=` / offset-0 field anchoring; in-place same-length `'*'` overwrite of the value extent; zero alloc; noexcept) → GREEN T002. (R2 / FR-003 / FR-008)
- [ ] T004 Add the `kMaxMaskableLogonBytes` bound in `include/fixpp/session/session.hpp` as a `static constexpr` set to the `build_logon` max output capacity, overridable ONLY through the existing `FIXPP_TEST_HOOKS` compile-gated seam (no public `SessionConfig`/ctor/template surface — Art. X; precedent: `kRpBufSize`). (R3 / N1)

---

## Phase 3 — User Story 1 (P1, MVP): session password not recoverable from the store

**Goal**: a credentialed Logon persisted to any store carries no cleartext password.
**Independent test**: open a FIXT session w/ a known password + FileStore, drive logon, read the store file bytes from disk → password absent, same-length mask present.

- [ ] T005 [P] [US1] RED witnesses in `tests/session/test_credential_store_redaction.cpp`: `Persisted_LogonPassword_AbsentFromStoreFile_MaskPresent` (FileStore temp dir; **read raw store-file bytes** → literal password absent AND same-length `'*'` run present; not via `retrieve()`), `Acceptor_ReplyLogon_PasswordMaskedInStore` (acceptor reply Logon stored masked; exercises an **at-bound** acceptor credential), `InMemoryStore_CredentialedLogon_AlsoMasked` (MemoryStore copy masked too — uniform backend). (SC-001/005, FR-004, INV-034-4)
- [ ] T006 [US1] Implement the masking branch in `Session::store_then_emit` (`src/session/session.cpp`): maskability gate (frame contains `\x01554=`/leading `554=` → confirm `MsgType(35)=="A"`), copy into a coroutine-frame `std::array<std::byte, kMaxMaskableLogonBytes>`, `mask_tag554_same_length_inplace`, `co_await store_->store(stamped_seq, masked_span, outbound)`; **transmit the original `frame` unchanged** (Step 2). Over-bound → I-07 skip-store-but-transmit (dead-defensive). → GREEN T005. (R1/R3/R4, FR-001/002/006/009, INV-034-1/2/3)
- [ ] T007 [US1] Implement the role-independent `open()`-time credential-length guard (extend the 033 FQ-1 FIXT-config validation in `src/session/session.cpp`): reject config whose `cfg.logon_credentials` username+password+Logon overhead could exceed `kMaxMaskableLogonBytes` — fires for BOTH initiator and acceptor open() (FR-004 both roles; makes the over-bound branch production-unreachable). (C3 / N2)

**Checkpoint**: US1 is independently demonstrable — the MVP delivers the at-rest protection.

---

## Phase 4 — User Story 2 (P2): the counterparty still authenticates

**Goal**: masking never touches the wire frame.

- [ ] T008 [P] [US2] RED witness `Wire_LogonPassword_UnmaskedOnTransmit` in `tests/session/test_credential_store_redaction.cpp`: capture the transmitted Logon (test transport send hook) → `Password(554)` equals the configured cleartext; session reaches established. (Green over the US1 impl — guards FR-002/SC-002 against regression.)

---

## Phase 5 — User Story 3 (P3): zero observable change when there is no secret

**Goal**: credential-free / non-Logon frames byte-identical; the MsgType=A gate (not 554-absence) is what excludes non-Logon frames.

- [ ] T009 [P] [US3] RED witnesses in `tests/session/test_credential_store_redaction.cpp`: `CredentialFreeLogon_And_NonLogon_StoredByteIdentical` (credential-free Logon + a non-Logon frame → stored bytes == pre-change baseline) and `NonLogon_WithGenuine554_StoredUnchanged` (a `35`≠`A` frame carrying `\x01554=secret\x01` → stored **unchanged**, excluded by the MsgType=A gate). (US3, FR-005/006/007, SC-003, INV-034-5)

---

## Phase 6 — Polish & cross-cutting

- [ ] T010 [P] Fault-injection witness `OverBound_SmallBoundSeam_SkipStoreButTransmit` in `tests/session/test_credential_store_redaction.cpp`: via the `FIXPP_TEST_HOOKS` small-`kMaxMaskableLogonBytes` seam, drive the dead over-bound branch and assert (a) no cleartext persisted, (b) the wire frame still carries the real 554, (c) a resend over that seqnum → SequenceReset-GapFill (not a masked verbatim replay). Earns the over-bound BRDA (no §IX.1 waiver). (N1 / C2-step-2 / I-07)
- [ ] T011 [P] Alloc gate `StorePath_NoNewAllocation` (counting-resource + mallocnesia LD_PRELOAD): the masked-Logon persist path allocates the same as baseline (the coroutine-frame `std::array` adds zero allocations). (SC-004 / FR-008)
- [ ] T012 [P] Docs (FR-010): in `spec/behaviors-and-limitations.md` flip **L-033-6** limitation → mitigation + add the R7 forward-constraint limitation (future verbatim admin-replay must re-derive creds from config); add a **dated correction note** under `specs/033-fixt-fix50sp2-session/tasks.md` T024/T020 ("no production frame persistence exists" was incomplete — overlooked the 008 store; no history rewrite); add the 034 row to `spec/feature-catalogue.md` + `spec/coverage-index.md`.
- [ ] T013 Feature-completeness audit (`tasks ↔ FR-001..010 ↔ SC-001..005 ↔ 9 witnesses ↔ catalogue`, 100% or waived — Gate B precondition) + `/speckit-verify` prep: confirm the new code is 100% DA/BRDA (incl. the over-bound branch via T010) or §IX.1-justified; ASan/UBSan/TSan over the new unit + touched session tests.

---

## Dependencies & ordering

- **Setup (T001)** → everything.
- **Foundational (T002→T003, T004)** → all user stories (the masker + bound are shared).
- **US1 (T005 RED → T006/T007 impl)** lands the integration; it is the MVP and unblocks US2/US3 witnesses (which are green over the US1 impl).
- **US2 (T008)** and **US3 (T009)** are independent of each other; both depend only on the US1 impl.
- **Polish (T010–T013)** after the stories; T013 is the Gate B precondition.

## Parallel opportunities

- T002 ∥ (nothing — leads Foundational).
- T005, T008, T009, T010, T011, T012 are each `[P]` (distinct test cases / distinct doc files), but T006/T007 (same `session.cpp`) are sequential and must precede the green state of T005/T008/T009.

## Implementation strategy

- **MVP = Phase 1 + 2 + 3 (US1)**: the masker + the store-boundary branch + the open()-guard + the at-rest witnesses. This alone delivers and proves the security fix.
- Then US2 (wire no-regression) and US3 (no-op) witnesses, then Polish (fault-injection BRDA, alloc gate, docs, completeness/verify).
