# Data model: 094 — the target text of every row it changes

Schema (`spec/feature-catalogue.md` header): `ID | Source | Category | Title | FIX version(s) | Spec ref | Status | /specify | PR | Tests | Verified`.

Rules applied to every row below:
- **Columns not listed under a row stay byte-identical** to `origin/main` (`9b176090`).
- **/specify** for a reconciliation flip reads
  `094-v1-tier0-bookkeeping (reconciliation; delivered by <X>)` (FR-010, D-002 form).
- **PR** cites the delivering PR(s) with the merge SHA from research R-9. The 094 PR itself is not
  cited, because its number does not exist until the PR is opened, and close-out does not re-edit rows.
- **Verified** opens with `Reconciled by 094 (2026-10-22) — not a delivery claim.`, then gives the
  evidence and every residual on record.
- A cell holding a backtick span keeps it as written here. No cell contains a bare `|`.

## A. Post-v1.0 drops (FR-001)

Only the Status cell changes.

| Row | Status (new) |
|---|---|
| W-015 | `dropped(post-v1.0, planned, not abandoned: new wire format, not core tag-value FIX, no QuickFIX reference implementation)` |
| W-016 | same as W-015 |
| SVC-001 … SVC-005 | `dropped(post-v1.0, planned, not abandoned: service wrapper deferred by owner decision 2026-07-15; src/service/ is a stub)` |

## B. Legend (FR-002)

The definition is **appended to the existing `> **Status** ∈ {…}` line**, not added as a new line, so
no catalogue line moves:

`> **Status** ∈ {backlog, planning, implementing, done, dropped(reason)} — **`dropped(post-v1.0, planned, not abandoned: <reason>)`** is an explicit drop for `[const §I.3]` (v1.0 ships when every OFFICIAL row is `done` or explicitly `dropped` with user-signed rationale) **and nothing more**: the row stays planned, keeps its entry and rationale, and returns to `backlog` when the release that builds it opens. User-signed: W-015, W-016, SVC-001..005 by DECISION 1 (owner, 2026-10-21); NFR-019 by the NFR-013 split ruling (owner, 2026-10-21). Recorded by feature 094.`

## C. Reconciliation flips (FR-005 … FR-009)

### NFR-001 (clean)
- Status `done`
- /specify `094-v1-tier0-bookkeeping (reconciliation; delivered by Phase 3 CI infra a476bd30)`
- PR `— (pre-PR infra a476bd30; Tier 2 later made a required merge gate)`
- Tests `CMakeLists.txt` `CMAKE_CXX_STANDARD 23` + `CMAKE_CXX_STANDARD_REQUIRED ON`; `.github/workflows/tier1.yml` job `linux` (legs linux-clang-{debug,release,asan,ubsan,tsan}, linux-gcc-release) + job `coverage`; `.github/workflows/tier2.yml` job `windows` (windows-msvc-{debug,release,asan})
- Verified `Reconciled by 094 (2026-10-22) — not a delivery claim. Enforced by the required checks tier1-required and tier2-required on main. clang-cl is prohibited ([const §II.2]); the GCC leg is a Release sanity leg only. CI evidence: Tier 1 run 39594667883 and Tier 2 run 39594667975 (push:main at 9b176090, both success). QuickFIX interop: not applicable.`

### NFR-002 (reworded)
- Title `Per-PR coverage obligation of [const §IX.1] — ≥95% line / ≥85% branch on touched modules, or a recorded assessment for every uncovered line (tested / waived / escalated to an issue); checked in /speckit-verify, not a CI threshold`
- Spec ref `[const §IX.1] Coverage thresholds`
- Status `done`
- /specify `094-v1-tier0-bookkeeping (reconciliation; delivered by the /speckit-verify coverage step)`
- PR `— (process gate; [const §IX.1] raised 2026-05-17)`
- Tests `.claude/skills/speckit-verify/SKILL.md` coverage step; per-feature records `.specify/decisions/<feature>-verify.md` (e.g. 544-hot-path-zero-alloc-verify.md, 563-564-callback-send-verify.md, 536-537-538-recovery-liveness-verify.md)
- Verified `Reconciled by 094 (2026-10-22) — not a delivery claim. The row's former 90/80 figure was superseded by [const §IX.1]; reworded to the obligation that rule states (owner, 2026-10-21). It is a recorded per-PR assessment, not a threshold CI enforces: tier1.yml job coverage's merge/LCOV steps are continue-on-error and Codecov is not a required check. The global ≥95% bullet of [const §IX.1] is a separate release question (parent REMAINING-WORK row 11), not covered by this row. Evidence form: the three cited verify records. QuickFIX interop: not applicable.`

### NFR-003 (reworded)
- Title `Sanitizers: the ASan, UBSan and TSan legs run the non-packaging ctest suite (ctest -LE packaging) before every merge (required check tier1-required), and any sanitizer report not on the allowlist fails the leg; the allocation-failure cells listed in ci/expected-preset-conditional-tests.txt are not built there; MSVC ASan on Tier 2 (required check tier2-required)`
- Status `done`
- /specify `094-v1-tier0-bookkeeping (reconciliation; delivered by Phase 3 CI infra a476bd30 + #282)`
- PR `— (pre-PR infra a476bd30) + #282 (squash a776d0af, sanitizer reports fail closed)`
- Tests `.github/workflows/tier1.yml` job `linux` legs linux-clang-{asan,ubsan,tsan} + step "Peak memory + unexplained-sanitizer gate (#266 / #229 / #267)"; `.github/workflows/tier2.yml` leg windows-msvc-asan; allowlists `ci/expected-sanitizer-reports.txt`, `ci/expected-preset-conditional-tests.txt` (censused by `tools/check_sanitizer_test_carveouts.py`)
- Verified `Reconciled by 094 (2026-10-22) — not a delivery claim. Narrowed from "sanitizers clean on hot-path code": a clean run says no exercised path reported. Known unexercised UB on record: #555 (deadline arithmetic overflow, liveness loop). Allowlisted report: the deliberate capi_send_recv_uaf_negative canary in ci/expected-sanitizer-reports.txt. CI evidence: Tier 1 run 39594667883 and Tier 2 run 39594667975 (push:main at 9b176090, both success). QuickFIX interop: not applicable.`

### NFR-005 (reworded)
- Title `PMR allocator-aware containers and arena allocation on the parse → validate → dispatch path; zero global-heap allocation enforced on the windows the mallocnesia gates list (B-544-1)`
- Status `done`
- /specify `094-v1-tier0-bookkeeping (reconciliation; delivered by 008-message-store, #448 gates, 544-hot-path-zero-alloc)`
- PR `#77 (squash 718195f9, slab-PMR MemoryStore) + #478 (squash a8fe9f79, mallocnesia gates enforced) + #572 (squash 2e4d15c6, B-544-1)`
- Tests `ctest -L mallocnesia` (tier1.yml steps "Assert the mallocnesia gate population (#448)" + "Run the allocation-discipline gates (#448)"); `tests/alloc_guard/CMakeLists.txt` positive controls `alloc_guard_*_positive_control_mallocnesia`
- Verified `Reconciled by 094 (2026-10-22) — not a delivery claim. Zero allocation holds only on the gated windows; outside them see L-497-1; ungated windows #569; TLS per-record allocation inside OpenSSL #565. The mallocnesia gates run on linux-clang-release (#478); CI evidence: Tier 1 run 39594667883 (push:main at 9b176090, success). QuickFIX interop: not applicable.`

### NFR-011 (reworded)
- Title `PMR allocator support — std::pmr::memory_resource accepted by the dictionary loaders, the parse arena, MemoryStore and the typed decimal accessors`
- Status `done`
- /specify `094-v1-tier0-bookkeeping (reconciliation; delivered by 002, 004, 008, 003 — dictionary loaders, wire codec, message store, codegen)`
- PR `#66 (merge ae4c9a56, loaders) + #68 (merge 2a6f76c7, parse arena) + #77 (squash 718195f9, MemoryStore) + #67 (merge b8a3b251, decimal accessor)`

  (Resolved from git: each witness file and test was added in the PR named after it, found with
  `git log --diff-filter=A` and `git log -S`.)
- Tests `tests/dictionary/pmr_allocation_test.cpp` (PmrAllocation.EveryByteFromMr); `tests/wire/three_arena_pinning_test.cpp` (WireThreeArenaPinning.*); `tests/session/test_memory_store_zero_allocator_calls.cpp` (MemoryStoreZeroAllocatorCalls.RetrieveDescriptorSnapshotUsesPmr), `tests/session/test_memory_store_capacity.cpp` (MemoryStoreCapacity.EngineMrAppliedWhenConfigResourceIsNull); `tests/codegen/typed_accessor_test.cpp` (CodegenTypedAccessor.DecimalAccessorTakesMemoryResource)
- Verified `Reconciled by 094 (2026-10-22) — not a delivery claim. Narrowed from "throughout" and the stale "C++17" label (the project is C++23). Not on third-party internals or by design: pugixml's transient DOM (L-002-4), OpenSSL's per-record buffers (#565), the C dictionary entry point's new_delete_resource (B-495-3). QuickFIX interop: not applicable.`

### NFR-013 (reworded; io_uring split to NFR-019)
- Title `ASIO async I/O transport (epoll on Linux, IOCP on Windows)`
- Status `done`
- /specify `094-v1-tier0-bookkeeping (reconciliation; delivered by 012-2h-transport)`
- PR `#85 (squash 53e25b1e)`
- Tests `tests/transport/` (`transport_smoke_test.cpp`, `test_asio_plain_transport.cpp`, `test_listener_acceptor.cpp`, `test_cancellation_propagation.cpp`) on the Linux legs of tier1.yml job `linux` and the Windows legs of tier2.yml job `windows`
- Verified `Reconciled by 094 (2026-10-22) — not a delivery claim. asio's default reactor on each platform (no ASIO_HAS_IO_URING in the build). The io_uring opt-in formerly in this title is NFR-019 (post-v1.0, owner 2026-10-21). CI evidence: Tier 1 run 39594667883 and Tier 2 run 39594667975 (push:main at 9b176090, both success). QuickFIX interop: not applicable (transport-level; session interop is the S-/TC- rows').`

### NFR-019 (new row, FR-008) — placed after NFR-014 in "NFRs (additional)"

`| NFR-019 | OFFICIAL | nfr | io_uring async I/O backend on Linux (opt-in) | all | [impl] split from NFR-013 (owner 2026-10-21) | dropped(post-v1.0, planned, not abandoned: io_uring opt-in, to be measured in the row-16 perf pass) | — | — | — | Split from NFR-013 by 094. Only the catalogue ever promised io_uring; the constitution and the 2h design do not. Absent today: no CMake option, no liburing dependency, no CI leg. |`

### D-001 (clean)
- Status `done`
- /specify `094-v1-tier0-bookkeeping (reconciliation; delivered by 002-dictionary-xml-loader)`
- PR `#66 (merge ae4c9a56)`
- Tests `tests/dictionary/lookup_test.cpp` (DictionaryLookupFixture, FIX42 instantiation), `tests/dictionary/delimiter_census_test.cpp` (DelimiterCensus.RedCountsReconcileWithSpecBaseline), `tests/dictionary/dict_enum_census_test.cpp` (DictEnumCensus.SC011_*), `tests/codegen/test_077_v42_vt11_completeness_and_c4.cpp` (BuilderCompleteness077Vt11V42.V42RegistryExactSetEqualsRawXmlWalk)
- Verified `Reconciled by 094 (2026-10-22) — not a delivery claim. dictionaries/FIX42.xml is QuickFIX-verbatim (dictionaries/UPSTREAM.txt), loaded by XmlLoader. FIX 4.2 declares no <component> blocks, so the "components" clause is vacuous. QuickFIX interop: not applicable (the dictionaries are QuickFIX-verbatim).`

### D-003 (clean)
- Status `done`
- /specify `094-v1-tier0-bookkeeping (reconciliation; delivered by 002-dictionary-xml-loader)`
- PR `#66 (merge ae4c9a56)`
- Tests `tests/dictionary/lookup_test.cpp` (DictionaryLookupFixture FIX50SP2 + FIXT11 instantiations, incl. CrossVersionIsolation), `tests/dictionary/delimiter_census_test.cpp` (DelimiterCensus.RegisteredGroupCountMatchesCodegenFix50Sp2, DelimiterCensus.NoChangeDictionariesUnchanged), `tests/dictionary/dict_enum_census_test.cpp` (DictEnumCensus.SC010_EveryMessageTypeCoveredByMsgTypeCodesetOrCodesetEmpty), `tests/codegen/test_077_v42_vt11_completeness_and_c4.cpp` (BuilderCompleteness077Vt11V42.Vt11ExpectedEmptyNoFileEmitted)
- Verified `Reconciled by 094 (2026-10-22) — not a delivery claim. The FIXT.1.1 transport / FIX50SP2 application split is asserted by CrossVersionIsolation; session-level use of the pair is S-025. QuickFIX interop: not applicable (the dictionaries are QuickFIX-verbatim).`

### D-007 (clean)
- Status `done`
- /specify `094-v1-tier0-bookkeeping (reconciliation; delivered by 002-dictionary-xml-loader)`
- PR `#66 (merge ae4c9a56)`
- Tests `tests/dictionary/xml_loader_test.cpp`, `tests/dictionary/negative_paths_test.cpp`, `tests/dictionary/lookup_test.cpp` (DictionaryLookupFixture/AllRuntimeVersions), `tests/fuzz/fuzz_dict_xml_loader.cpp`, `tests/dictionary/load_any_test.cpp`, `tests/capi/dictionary_load_test.cpp`
- Verified `Reconciled by 094 (2026-10-22) — not a delivery claim. XmlLoader accepts QuickFIX XML for all nine runtime versions ([const §I.1]); reachable from C and TOML through dict::load_any (080). Load-time performance is tracked separately (#263). QuickFIX interop: not applicable (the dictionaries are QuickFIX-verbatim).`

  (#263 was confirmed OPEN on 2026-10-22.)

### D-008 (version column reworded)
- FIX version(s) `4.2, 4.4, 5.0SP2, FIXT.1.1 (+ FIX Latest, opt-in build flag); runtime-XML-only versions: L-003-2`
- Status `done`
- /specify `094-v1-tier0-bookkeeping (reconciliation; delivered by 003-dictionary-codegen)`
- PR `#67 (merge b8a3b251)`
- Tests `tests/codegen/vlatest_compile_smoke_test.cpp` (static_assert over Fields.hpp), `tests/codegen/typed_accessor_test.cpp` (CodegenTypedAccessor.MsgTypeAndVersionAreConstexpr), `tests/dictionary/ref_shape_test.cpp`, `tests/codegen/read_tier_byte_diff_test.cmake`, `tests/codegen/determinism_test.cpp` (DeterminismTest.GeneratedMatchesGolden)
- Verified `Reconciled by 094 (2026-10-22) — not a delivery claim. Emitted as inline constexpr FieldRef arrays per version. The compile-time read is witnessed on vlatest; v42/v44/v50sp2 are witnessed by shape asserts and byte pins. The reify round-trip (L-003-1) is OSS-010's scope, not this row's. QuickFIX interop: not applicable (the dictionaries are QuickFIX-verbatim).`

  The existing HTML comment under the row stays.

### TC rows — version columns from research R-3

The FIX version(s) cell of every TC row becomes the "Column written" value in research R-3.

### TC-003 (version column)
- Status `done`
- /specify `094-v1-tier0-bookkeeping (reconciliation; delivered by 093-inbound-frame-dispositions)`
- PR `#554 (squash e84991bf)`
- Tests `tests/session/inbound_frame_dispositions_test.cpp` (InboundFrameDispositionsTc.TC003_3b_*, TC003_3c_*, TC003_3e_*; ctest session_inbound_frame_dispositions)
- Verified `Reconciled by 094 (2026-10-22) — not a delivery claim. 3b/3c/3e: each garbled frame on an Active session is disregarded, counted and logged (the "warning"), NextNumIn unchanged, connection open; 3a/3d are the accept arm of the same cells (the following good frame is delivered). Witnessed in-process; the live QuickFIX interop cells skip without a counterparty, so they are not per-PR evidence and are not claimed. Versions not witnessed: #614.`

  This replaces the old Verified text, which contained "Feature 093 does not own this row … stays
  backlog".

### TC-005 (reworded)
- Title `Session conformance: Reject (35=3) receipt — NextNumIn incremented, session continues, no Reject of a Reject (test 7)`
- Status `done`
- /specify `094-v1-tier0-bookkeeping (reconciliation; delivered by 005-session-establishment-fsm + 093)`
- PR `#82 (squash ba2222d6) + #554 (squash e84991bf, C-ABI Row6 witness)`
- Tests `tests/capi/inbound_frame_dispositions_c7_witness_test.cpp` (CapiC7Witness.Row6_InboundRejectTrafficKeepsTheSessionUp), `tests/session/conformance/tc_reject_test.cpp` (TC005Reject.Fix42_7_ReceiveReject_NoLoop, TC005Reject.Fix44_7_ReceiveReject_NoLoop; ctest -R session_tc_reject), `tests/session/session_reject_test.cpp` (SessionReject.NoRejectLoopOnInboundReject), `tests/session/fsm_matrix_witness_test.cpp` (FsmMatrixWitness.Active_InboundReject_LogOnly_StaysActive)
- Verified `Reconciled by 094 (2026-10-22) — not a delivery claim. Narrowed from "log RefSeqNum/SessionRejectReason": an inbound Reject is neither delivered to fromAdmin nor logged, which QuickFIX C++/J/Go both do — fixpp#612 (batch B65). Witnessed in-process; the live QuickFIX interop cells skip without a counterparty, so they are not per-PR evidence and are not claimed. Versions not witnessed: #614.`

### TC-006 (version column; residuals)
- Status `done`
- /specify `094-v1-tier0-bookkeeping (reconciliation; delivered by 013-session-reconnect-binding + 037 + B28)`
- PR `#86 (squash bd84e086) + #121 (squash 795cde1b) + #609 (squash 9b176090)`
- Tests `tests/session/test_recovery_store_horizon.cpp` (RecoveryStoreHorizonTest.AvailableRangeReplaysCarryPossDupFlag, .ReplayFramesCarryOrigSendingTime, .LargeAppFrameReplayed_NotGapFilled, .PreHorizonGapCollapsedToGapFill), `tests/session/test_recovery_admin_span_gapfill.cpp` (RecoveryAdminSpanGapfillTest.*), `tests/session/test_resend_reply_possdup.cpp` (ResendReplyPossDup.Cell1_GapFill_Carries_43Y_And_122_EqOwn52), `tests/session/test_538_store_lookup_after_reset.cpp` (Stores/StoreLookupAfterReset.*)
- Verified `Reconciled by 094 (2026-10-22) — not a delivery claim. Application messages are resent with PossDupFlag=Y and OrigSendingTime; admin spans collapse to one SequenceReset-GapFill carrying 43=Y/122. Residuals on record: a live send can interleave with a replay (#588, open); BeginSeqNo(7)=0 is answered with a GapFill whose 34=0 (#580); no per-message DoNotSend veto on replay (L-019-4). Witnessed in-process; the live QuickFIX interop cells skip without a counterparty, so they are not per-PR evidence and are not claimed. Versions not witnessed: #614.`

### TC-008 (version column)
- Status `done`
- /specify `094-v1-tier0-bookkeeping (reconciliation; delivered by fix/inbound-sequencereset-s023)`
- PR `#90 (squash 13c49f88)`
- Tests `tests/session/test_inbound_sequence_reset.cpp` (InboundSequenceResetTest.ResetModeHardResetsAndBypassesOrdering, .NewSeqNoEqualExpectedIsNoOp, .NewSeqNoBelowExpectedRejects), `tests/interop/parity/inbound_sequencereset_arms_test.cpp` (InboundSequenceResetArms.NewSeqNoAboveExpectedAdvances, .NewSeqNoEqualExpectedIsNoOp, .NewSeqNoBelowExpectedRejects)
- Verified `Reconciled by 094 (2026-10-22) — not a delivery claim. 11a NewSeqNo > NextNumIn sets it regardless of the frame's own 34; 11b equal is accepted; 11c lower is rejected with SessionRejectReason 5 (RefTagID 36), NextNumIn unchanged. Witnessed in-process; the live QuickFIX interop cells skip without a counterparty, so they are not per-PR evidence and are not claimed. Versions not witnessed: #614.`

### TC-012 (reworded)
- Title `Session conformance: PossResend (test 19, sub-cases 19a–19b) — PossResend(97)=Y delivered to the application with the flag intact; the 19a/19b message-ID duplicate check is the application's`
- Status `done`
- /specify `094-v1-tier0-bookkeeping (reconciliation; delivered by 022-possresend-allowpossdup-send)`
- PR `#103 (squash 97e2e3d9)`
- Tests `tests/session/test_inbound_poss_resend.cpp` (InboundPossResendTest.DeliverInSeq_97Y, .Combo_43Y_97Y, .PossResend_No122_NoReject)
- Verified `Reconciled by 094 (2026-10-22) — not a delivery claim. FIX-TC 19 itself calls the check an "application layer check of message-specific ID"; fixpp does no session-level duplicate detection (L-022-1, as QuickFIX C++/J). Witnessed in-process; the live QuickFIX interop cells skip without a counterparty, so they are not per-PR evidence and are not claimed. Versions not witnessed: #614.`

### TC-014 (reworded)
- Title `Session conformance (optional): Sequence number synchronization after failure (test 9) — via ResetSeqNumFlag(141)=Y (FIX-TC option 2); outbound SequenceReset-Reset (option 1) not offered`
- Status `done`
- /specify `094-v1-tier0-bookkeeping (reconciliation; delivered by 024-reset-refresh-on-logon + 030 + 032)`
- PR `#106 (squash f3159075) + #113 (squash 3c5ee658) + #115 (squash 9417454f)`
- Tests `tests/session/test_reset_on_lifecycle.cpp` (ResetOnLifecycleTest.ResetOnLogon_Initiator_ResetsAndEmits141, .ResetOnLogon_Acceptor_ResetsIdempotentWith141, .ResetOnLogon_BothRoles_Resets, .Initiator_Received141Ack_NextInboundTwo_NoResend)
- Verified `Reconciled by 094 (2026-10-22) — not a delivery claim. FIX-TC 9 is satisfied by either option; fixpp originates no outbound Reset-mode SequenceReset (outbound is GapFill only). Witnessed in-process; the live QuickFIX interop cells skip without a counterparty, so they are not per-PR evidence and are not claimed. Versions not witnessed: #614.`

### TC-015 (version column)
- Status `done`
- /specify `094-v1-tier0-bookkeeping (reconciliation; delivered by tests/fix-tc-coverage-gaps-2)`
- PR `#139 (squash fb043752)`
- Tests `tests/interop/parity/fix_tc_coverage_gaps_test.cpp` (FixTcCoverageGaps.BodyFieldsArbitraryOrder_Accepted; ctest interop_parity_us3_tests)
- Verified `Reconciled by 094 (2026-10-22) — not a delivery claim. Fields in non-canonical order (body and header after the first three) are accepted and later MsgSeqNums are accepted (B-005-7). Accepting is what scenario 15 asks for; the same leniency is why 14g is not met (TC-010, backlog). Witnessed in-process; the live QuickFIX interop cells skip without a counterparty, so they are not per-PR evidence and are not claimed. Versions not witnessed: #614.`

### TC-017 (reworded; not supported by design)
- Title `Session conformance (optional): EncryptMethod(98) (test 17) — 0 accepted; ≠0 refused at Logon (B-043-1); no application-layer encryption ([const §XII.7], [const §XV.10]), so 17c–17j do not apply`
- Status `done`
- /specify `094-v1-tier0-bookkeeping (reconciliation; delivered by 043-plaintext-tcp-transport)`
- PR `#134 (squash 95aa1c38)`
- Tests `tests/session/test_interpret_logon_encrypt_method.cpp` (InterpretLogonEncryptMethod.ZeroEncryptMethodAccepted, .AbsentEncryptMethodAccepted, .NonZeroEncryptMethodRejected)
- Verified `Reconciled by 094 (2026-10-22) — not a delivery claim. Not supported by design (owner, 2026-10-21): FIX deprecated EncryptMethod in 4.3 (TLS/FIXS replaces it, and fixpp ships TLS); QuickFIX C++/J/Go each send 98=0 and implement none of 17c–17j. 98≠0 is a Logon refusal, not FIX-TC 17b's Reject 373=7 + Logout. Witnessed in-process; the live QuickFIX interop cells skip without a counterparty, so they are not per-PR evidence and are not claimed. Versions not witnessed: #614.`

## D. Coverage index (FR-011)

- **§4 row 3:** the note's opener changes from `**093 (partial; TC-003 stays backlog):**` to
  `**093; TC-003 done (094 reconciliation):**`. The rest of the note stays, including its
  re-derivation grep.
- **§4 rows 8, 9, 10, 11:** `005-defer: recovery-dependent — …` is replaced with:
  - 8 → `TC-006 done (094 reconciliation): recovery delivered by 013 (#86), 037 (#121), B28 (#609).`
  - 9 → `TC-014 done (094 reconciliation): option 2 (141=Y) delivered by 024/030/032.`
  - 10 → `TC-007 stays backlog: only 10b has a GapFill-mode witness; 10a/10c/10d/10e unwitnessed (findings v1-tier0-flip-verification-2026-10-21).`
  - 11 → `TC-008 done (094 reconciliation): Reset-mode SequenceReset delivered by S-023 (#90).`
- **The 005 deferral ledger** (§FIX-TC, row "ResendRequest / SequenceReset-GapFill / SequenceReset-Reset
  / synchronize-seqnums"): the last cell is replaced with
  `discharged by 013/S-023/024 — TC-006, TC-008, TC-014 done (094); TC-007 backlog`.
- **NFR-019 supplemental** (new paragraph, after the NFR-016 supplemental):
  `**NFR-019 supplemental:** io_uring async I/O backend (Linux, opt-in) — a design choice split from NFR-013 (owner 2026-10-21), not a FIX spec section ([const §VI.3]). No constitution clause or design doc requires it; to be measured in the row-16 perf pass. Status: dropped(post-v1.0, planned, not abandoned).`
- **No change** to the existing W-015/W-016/SVC-005 entries: they already read post-1.0.

## E. Constitution (FR-003, FR-003a)

Each item is in the same edit.

- **Sync Impact Report:** a new block at the top, `v3.0 → v3.1 (2026-10-22)`. It records the bump
  and the R-2 argument, the clauses modified, the rationale (DECISION 1, owner 2026-10-21; roadmap
  placement, owner 2026-10-22), the affected rows (W-015, W-016, SVC-001..005; NFR-013 → NFR-019),
  the kept-as-shipped surfaces (FR-003b), the process (rides feature 094, a recorded deviation from
  §XX.2's standalone-PR form, precedents 035/043/069), and the review (Gate A on 094) and sign-off
  (owner).
- **Status line:** the `> **Status:**` line gains a `v3.1` lead clause, and the v3.0 text moves into
  its "Prior:" chain.
- **§IV.4:** `**Service wrapper** (\`service/\`) is opt-in and **post-1.0** (Article XVIII §2). When
  built, gRPC is the control-plane transport and iceoryx2 SHM the optional data plane (Article XIV
  §3).`
- **§XIV.1:** the control-plane bullet becomes `**Control plane** (interface in v1.0 —
  \`include/fixpp/service/control_plane_factory.hpp\`; its default gRPC-over-Unix-socket / named-pipe
  implementation is post-1.0, Article XVIII §2).` The lead-in stays "each with one default impl in
  v1.0", plus an exception clause naming the control plane.
- **§XIV.3:** `**Data-plane SHM via iceoryx2** is opt-in for sidecar mode, which is post-1.0 (Article
  XVIII §2). The control plane works without it.`
- **§XVIII.1:** `**v1.0 scope is locked:** FIX 4.0–5.0SP2 + FIXT.1.1 session + application layers,
  FIXS over TLS, the C ABI, Python bindings. The gRPC service wrapper and the iceoryx2 SHM data plane
  are post-1.0 (§2; amended v3.1).`
- **§XVIII.2:** a final bullet `**Unscheduled — service wrapper** (\`fixppd\`: gRPC control plane,
  iceoryx2 SHM data plane). Planned post-1.0; no version assigned (owner, 2026-10-22). Not
  abandoned.`

## F. Other surfaces (FR-004)

- **`.specify/architecture.md`:** the "Stable from v1.0: … the gRPC control-plane proto" bullet gains
  `(post-1.0 — constitution v3.1, feature 094)` after the proto item. §8.1's heading gains
  `— post-1.0 (constitution v3.1)`.
- **`.specify/2j-controlplane.md`:** a status-line note: `**Timing (constitution v3.1, feature 094):**
  the default gRPC implementation designed here is post-1.0 (Article XVIII §2, unscheduled); the
  interface header ships in v1.0.`
- **`brain/components/service.md`:** the sentence "Whether the service rows are deferred or formally
  dropped is a **live decision recorded outside this repo** — this page deliberately does **not**
  answer it." is replaced with the decision: SVC-001..005 are
  `dropped(post-v1.0, planned, not abandoned)` (owner, 2026-10-21), and the constitution names the
  wrapper unscheduled post-1.0 (v3.1, feature 094). The page cites the catalogue rows and the
  constitution clause, not line numbers.

## G. Licence marking (FR-013, FR-013a, FR-014)

- **The script:** `spdx_mark.py`, written in the orchestrator's scratchpad and dry-run against a
  scratch clone (research R-6). The phase-implementer copies it into its own working space and runs
  it on the branch. The script is not committed.
- **C headers:** `include/fix/**/*.h` get `/* SPDX-License-Identifier: AGPL-3.0-or-later */` as line 1.
  All other files get `// SPDX-License-Identifier: AGPL-3.0-or-later`.
- **`conanfile.py`:** `license = "AGPL-3.0-only"` becomes `license = "AGPL-3.0-or-later"`.
- **`tools/capi_freeze.sha256`:** regenerated with `sha256sum` over the same 12-file set, in the
  manifest's existing order and two-space format.
