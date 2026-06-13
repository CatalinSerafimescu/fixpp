# Quickstart — verifying credential store redaction (034)

The single most important check is the **on-disk witness**: a real persistent store must not contain the
cleartext password after a credentialed logon.

## The P1 witness (US1 / SC-001)

1. Configure a FIXT session with a known password and a `FileStoreFactory` pointed at a temp directory:
   - `cfg.default_appl_ver_id = <a FIXT version>` (so it's a FIXT Logon carrying 553/554)
   - `cfg.logon_credentials.password = "S3cr3t-PaSSw0rd"` (a distinctive literal)
   - `cfg.store_factory = FileStoreFactory(temp_dir)`
2. Drive the session to emit + persist its outbound Logon (the 029/025 unit pattern: self-seed seqnums /
   reuse `test_store_reset_crash_cut.cpp` fixture shape; no live peer needed — `store_then_emit` persists
   before transmit).
3. **Read the store file bytes from disk** (`std::ifstream` over the FileStore log file — NOT `retrieve()`):
   - Assert the literal `"S3cr3t-PaSSw0rd"` byte sequence appears **zero** times.
   - Assert a same-length `'*'` run is present where the 554 value was (`554=***************\x01`, length
     equal to the original password).
4. Cross-check the record is still well-formed: same total length as the unmasked frame; the store can
   `retrieve()` the record without a CRC/length error (SC-005).

## The wire no-regression check (US2 / SC-002)

- Capture the transmitted Logon bytes (the test transport's send hook) and assert its `554` value equals
  `"S3cr3t-PaSSw0rd"` (unmasked). Optionally run a live logon against a reference counterparty and assert
  establishment succeeds.

## The no-op checks (US3 / SC-003)

- A **credential-free** Logon and any **non-Logon** outbound frame: persist with and without the change and
  assert the stored bytes are byte-identical.

## The alloc gate (SC-004 / FR-008)

- Run the persist path under the counting-resource + mallocnesia LD_PRELOAD gate; assert the masked-Logon
  store path allocates the same as baseline (the stack copy adds no heap).

## Sanitizer / coverage

- New unit (`test_credential_store_redaction.cpp`) + touched session tests under ASan/UBSan/TSan green.
- New byte-utility + the `store_then_emit` branch at 100% DA/BRDA (Article IX §1), or §IX.1-justified.

## What "done" looks like

- All seven witnesses in `contracts/store-redaction.md` pass.
- `/speckit-verify` matrix green; feature-completeness audit (tasks ↔ FR/SC ↔ catalogue) 100%.
- B&L L-033-6 flipped to mitigation + forward-constraint limitation added; 033 tasks.md dated correction
  note added; 034 catalogue/coverage-index rows added.
