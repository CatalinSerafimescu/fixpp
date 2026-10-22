# Data model: 094 — the target text of every row, clause and surface it changes

Schema (`spec/feature-catalogue.md` header): `ID | Source | Category | Title | FIX version(s) | Spec ref | Status | /specify | PR | Tests | Verified`.

Rules applied to every row below:
- **Columns not listed under a row stay byte-identical** to `origin/main` (`9b176090`).
- **/specify** for a reconciliation flip reads
  `094-v1-tier0-bookkeeping (reconciliation; delivered by <X>)` (FR-010, D-002 form).
- **PR** cites the delivering PR(s) with the merge SHA from research R-9. The 094 PR itself is not
  cited, because its number does not exist until the PR is opened, and close-out does not re-edit rows.
- **Verified** opens with `Reconciled by 094 (2026-10-22) — not a delivery claim.`, then gives the
  evidence and every residual on record.
- **Every cited path resolves in this (public) repository**, or is labelled `parent repo (private)`.
  The library is public and the owner's research repository is private, so an unlabelled pointer into
  the parent is evidence no reader can open.
- A cell holding a backtick span keeps it as written here. No cell contains a bare `|`.
- Where the text says "same-line", the edit replaces or appends text inside an existing line and adds
  no line. `.specify/architecture.md` requires this above its Appendix Z, and the other design docs are
  edited the same way so no line citation moves.

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

`> **Status** ∈ {backlog, planning, implementing, done, dropped(reason)} — **`dropped(post-v1.0, planned, not abandoned: <reason>)`** is an explicit drop for `[const §I.3]` (v1.0 ships when every OFFICIAL row is `done` or explicitly `dropped` with user-signed rationale) **and nothing more**: the row stays planned, keeps its entry and rationale, and returns to `backlog` when the release that builds it opens. User-signed: W-015, W-016, SVC-001..005 by DECISION 1 (owner, 2026-10-21; quoted in `specs/094-v1-tier0-bookkeeping/spec.md` §Context and recorded in the constitution v4.0 Sync Impact Report); NFR-019 by the NFR-013 split ruling (owner, 2026-10-21; `specs/094-v1-tier0-bookkeeping/spec.md` §Context). Recorded by feature 094.`

## C. Reconciliation flips (FR-005 … FR-009)

### NFR-001 (clean)
- Status `done`
- /specify `094-v1-tier0-bookkeeping (reconciliation; delivered by Phase 3 CI infra a476bd30)`
- PR `— (pre-PR infra a476bd30; tier1-required 96dd8aed, tier2-required #166 ff19ae58, PR-context gating 27f8701d)`
- Tests `CMakeLists.txt` `CMAKE_CXX_STANDARD 23` + `CMAKE_CXX_STANDARD_REQUIRED ON`; `.github/workflows/tier1.yml` job `linux` (legs linux-clang-{debug,release,asan,ubsan,tsan}, linux-gcc-release) + job `coverage`; `.github/workflows/tier2.yml` job `windows` (windows-msvc-{debug,release,asan})
- Verified `Reconciled by 094 (2026-10-22) — not a delivery claim. Enforced by main's branch protection, which lists tier1-required, tier2-required and tier3-required as required status checks (gh api repos/CatalinSerafimescu/fixpp/branches/main/protection/required_status_checks, read 2026-10-22). clang-cl is prohibited ([const §II.2]); the GCC leg is a Release sanity leg only. CI evidence: Tier 1 run 39594667883 and Tier 2 run 39594667975 (push:main at 9b176090, both success). QuickFIX interop: not applicable.`

  (`96dd8aed` and `27f8701d` are direct pushes with no PR; `ff19ae58` is PR #166, the oldest commit
  whose diff adds `tier2-required`. Resolved with `git log --reverse -S tier1-required` / `-S
  tier2-required` over the two workflows.)

### NFR-002 (reworded)
- Title `Per-PR coverage obligation of [const §IX.1] — ≥95% line / ≥85% branch on touched modules, or a recorded assessment for every uncovered line/branch the PR adds or newly makes reachable (tested / waived / escalated to an issue); checked in the per-feature verify record, not a CI threshold`
- Spec ref `[const §IX.1] Coverage thresholds`
- Status `done`
- /specify `094-v1-tier0-bookkeeping (reconciliation; delivered as the per-feature verify obligation of [const §IX.1], raised 2026-05-17)`
- PR `— (process obligation; [const §IX.1] raised 2026-05-17)`
- Tests `[const §IX.1] names the record: .specify/decisions/<feature>-verify.md. In this repository that path is a gitignored link into the owner's research repository — parent repo (private): research/G19-fix-fpml-iso20022/decisions/speckit/563-564-callback-send-verify.md section "Coverage (T7.7)" and research/G19-fix-fpml-iso20022/decisions/speckit/536-537-538-recovery-liveness-verify.md section "T6.7", each assessing every uncovered changed line and every untaken branch on the diff`
- Verified `Reconciled by 094 (2026-10-22) — not a delivery claim. The row's former 90/80 figure was superseded by [const §IX.1]; reworded to the obligation that rule states (owner, 2026-10-21). Closed under [const §VI.6](b) as amended in v4.0: a process row, closed on the records [const §IX.1] names in place of verifying tests. Lapse on record: the 544-hot-path-zero-alloc verify record (parent repo (private)) assessed added lines only, fixpp#<N-544>. It is a per-PR assessment the verifying agent records, not a threshold CI enforces: tier1.yml job coverage's merge/LCOV steps are continue-on-error and Codecov is not a required check. The records live in the owner's private repository, so this cell is a pointer, not evidence a reader of this repository can reproduce. The global ≥95% bullet of [const §IX.1] is a separate release question (parent repo (private) REMAINING-WORK row 11), not covered by this row. QuickFIX interop: not applicable.`

  The earlier draft cited `.claude/skills/speckit-verify/SKILL.md`. That file is not in this
  repository (it is tracked in the parent), and its Step 4 treats a metric below threshold as a FAIL,
  which is not `[const §IX.1]`'s rule. It is no longer cited. The earlier draft also cited
  `544-hot-path-zero-alloc-verify.md`, which assesses added lines only; it is no longer cited as
  evidence, and is put on record instead (FR-020, research R-13). `fixpp#<N-544>` is the issue FR-020
  files before this row is written; the cell is written with the real number, never with a
  placeholder.

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
- Verified `Reconciled by 094 (2026-10-22) — not a delivery claim. Zero allocation holds only on the gated windows; outside them see L-497-1; ungated windows #569; TLS per-record allocation inside OpenSSL #565. The mallocnesia steps run on linux-gcc-release and linux-clang-release (the if: of the three #448 steps in tier1.yml; #478). CI evidence: Tier 1 run 39594667883 (push:main at 9b176090, success). QuickFIX interop: not applicable.`

### NFR-011 (reworded)
- Title `PMR allocator support — std::pmr::memory_resource accepted by the dictionary loaders (XmlLoader, OrchestraLoader), the parse arena, MemoryStore and the typed decimal accessors`
- Status `done`
- /specify `094-v1-tier0-bookkeeping (reconciliation; delivered by 002, 004, 008, 003 — dictionary loaders, wire codec, message store, codegen)`
- PR `#66 (merge ae4c9a56, loaders) + #68 (merge 2a6f76c7, parse arena) + #77 (squash 718195f9, MemoryStore) + #67 (merge b8a3b251, decimal accessor)`

  (Resolved from git: each witness file and test was added in the PR named after it, found with
  `git log --diff-filter=A` and `git log -S`.)
- Tests `tests/dictionary/pmr_allocation_test.cpp` (PmrAllocation.EveryByteFromMr); `include/fixpp/dict/orchestra_loader.hpp` (OrchestraLoader::load / load_from_string take a std::pmr::memory_resource*) + `tests/dictionary/orchestra_loader_test.cpp`; `tests/wire/three_arena_pinning_test.cpp` (WireThreeArenaPinning.*); `tests/session/test_memory_store_zero_allocator_calls.cpp` (MemoryStoreZeroAllocatorCalls.RetrieveDescriptorSnapshotUsesPmr), `tests/session/test_memory_store_capacity.cpp` (MemoryStoreCapacity.EngineMrAppliedWhenConfigResourceIsNull); `tests/codegen/typed_accessor_test.cpp` (CodegenTypedAccessor.DecimalAccessorTakesMemoryResource)
- Verified `Reconciled by 094 (2026-10-22) — not a delivery claim. Narrowed from "throughout" and the stale "C++17" label (the project is C++23). Routing through the resource is asserted for XmlLoader (every byte from the resource); OrchestraLoader takes the resource and its tests pass one, but no cell asserts that its allocations route through it. Not on third-party internals or by design: pugixml's transient DOM (L-002-4), OpenSSL's per-record buffers (#565), the C dictionary entry point's new_delete_resource (B-495-3). QuickFIX interop: not applicable.`

### NFR-013 (reworded; io_uring split to NFR-019)
- Title `ASIO async I/O transport (Linux and Windows)`
- Status `done`
- /specify `094-v1-tier0-bookkeeping (reconciliation; delivered by 012-2h-transport)`
- PR `#85 (squash 53e25b1e)`
- Tests `tests/transport/` (`transport_smoke_test.cpp`, `test_asio_plain_transport.cpp`, `test_listener_acceptor.cpp`, `test_cancellation_propagation.cpp`) on the Linux legs of tier1.yml job `linux` and the Windows legs of tier2.yml job `windows`
- Verified `Reconciled by 094 (2026-10-22) — not a delivery claim. Backend, a design fact and not asserted by any test: asio's platform default, epoll on Linux and IOCP on Windows; no build file selects another (re-derive: git grep -n -e ASIO_HAS_IO_URING -e ASIO_DISABLE_ -- '*CMakeLists.txt' cmake/ CMakePresets.json conan/ conanfile.py is empty; the known positive: over every tracked file outside specs/ the same grep finds only tests/support/recycler_driver.hpp and .specify/544-hot-path-zero-alloc.md, neither a build file). The io_uring opt-in formerly in this title is NFR-019 (post-v1.0, owner 2026-10-21). CI evidence: Tier 1 run 39594667883 and Tier 2 run 39594667975 (push:main at 9b176090, both success). QuickFIX interop: not applicable (transport-level; session interop is the S-/TC- rows').`

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
- Verified `Reconciled by 094 (2026-10-22) — not a delivery claim. Emitted as inline constexpr FieldRef arrays per version. The compile-time read is witnessed on vlatest; v42, v44, v50sp2 and vt11 are witnessed by shape asserts and by byte pins (read_tier_byte_diff_test.cmake pins each version's Fields/Messages/Reify/Validator.hpp). The reify round-trip (L-003-1) is OSS-010's scope, not this row's. QuickFIX interop: not applicable (the dictionaries are QuickFIX-verbatim).`

  The existing HTML comment under the row stays.

### TC rows — version columns and the scope clause

- The FIX version(s) cell of every TC row becomes the "Column written" value in research R-3.
- Every TC Verified cell ends with the same scope sentence, written below as `<TC-SCOPE>`:

  `Versions not witnessed: #614. FIX-TC §4.1 makes the scenarios applicable to the session layer of 4.2, 4.4 and FIXT, so the other versions the former column named (4.0, 4.1, 4.3, 5.0, 5.0SP1) are not required by FIX-TC.`

  (Checked against `FIX_Session_Testcases_June_2020.pdf` §4.1, parent repo (private) `research/`:
  "applicable to all supported versions of the FIX session layer (4.2, 4.4, FIXT)".)

### TC-003 (version column)
- Status `done`
- /specify `094-v1-tier0-bookkeeping (reconciliation; delivered by 093-inbound-frame-dispositions)`
- PR `#554 (squash e84991bf)`
- Tests `tests/session/inbound_frame_dispositions_test.cpp` (InboundFrameDispositionsTc.TC003_3b_*, TC003_3c_*, TC003_3e_*; ctest session_inbound_frame_dispositions)
- Verified `Reconciled by 094 (2026-10-22) — not a delivery claim. 3b/3c/3e: each garbled frame on an Active session is disregarded, counted and logged (the "warning"), NextNumIn unchanged, connection open; 3a/3d are the accept arm of the same cells (the following good frame is delivered). Witnessed in-process; the live QuickFIX interop cells skip without a counterparty, so they are not per-PR evidence and are not claimed. <TC-SCOPE>`

  This replaces the old Verified text, which contained "Feature 093 does not own this row … stays
  backlog".

### TC-005 (reworded)
- Title `Session conformance: Reject (35=3) receipt — NextNumIn incremented, session continues, no Reject of a Reject (test 7)`
- Status `done`
- /specify `094-v1-tier0-bookkeeping (reconciliation; delivered by 005-session-establishment-fsm + 093)`
- PR `#82 (squash ba2222d6) + #554 (squash e84991bf, C-ABI Row6 witness)`
- Tests `tests/capi/inbound_frame_dispositions_c7_witness_test.cpp` (CapiC7Witness.Row6_InboundRejectTrafficKeepsTheSessionUp), `tests/session/conformance/tc_reject_test.cpp` (TC005Reject.Fix42_7_ReceiveReject_NoLoop, TC005Reject.Fix44_7_ReceiveReject_NoLoop; ctest -R session_tc_reject), `tests/session/session_reject_test.cpp` (SessionReject.NoRejectLoopOnInboundReject), `tests/session/fsm_matrix_witness_test.cpp` (FsmMatrixWitness.Active_InboundReject_LogOnly_StaysActive)
- Verified `Reconciled by 094 (2026-10-22) — not a delivery claim. NextNumIn incremented: witnessed on FIX.4.4 by CapiC7Witness.Row6_InboundRejectTrafficKeepsTheSessionUp, which sends in-sequence Rejects with rising MsgSeqNum and then an order at the next number, and asserts the order is delivered (expect_kept_up: o.received); a NextNumIn that did not advance would hold that order as too high. No Reject of a Reject and session continues: TC005Reject and SessionReject assert that nothing is sent in reply and the session stays Active; FsmMatrixWitness.Active_InboundReject_LogOnly_StaysActive asserts the Active state only. Narrowed from "log RefSeqNum/SessionRejectReason": an inbound Reject is neither delivered to fromAdmin nor logged, which QuickFIX C++/J/Go all do — fixpp#612 (batch B65). Witnessed in-process; the live QuickFIX interop cells skip without a counterparty, so they are not per-PR evidence and are not claimed. <TC-SCOPE>`

### TC-006 (version column; residuals)
- Status `done`
- /specify `094-v1-tier0-bookkeeping (reconciliation; delivered by 013-session-reconnect-binding + 037 + B28)`
- PR `#86 (squash bd84e086) + #121 (squash 795cde1b) + #609 (squash 9b176090)`
- Tests `tests/session/test_recovery_store_horizon.cpp` (RecoveryStoreHorizonTest.AvailableRangeReplaysCarryPossDupFlag, .ReplayFramesCarryOrigSendingTime, .LargeAppFrameReplayed_NotGapFilled, .PreHorizonGapCollapsedToGapFill), `tests/session/test_recovery_admin_span_gapfill.cpp` (RecoveryAdminSpanGapfillTest.*), `tests/session/test_resend_reply_possdup.cpp` (ResendReplyPossDup.Cell1_GapFill_Carries_43Y_And_122_EqOwn52), `tests/session/test_538_store_lookup_after_reset.cpp` (Stores/StoreLookupAfterReset.*)
- Verified `Reconciled by 094 (2026-10-22) — not a delivery claim. Application messages are resent with PossDupFlag=Y and OrigSendingTime; admin spans collapse to one SequenceReset-GapFill carrying 43=Y/122. Residuals on record: a live send can interleave with a replay (#588, open); BeginSeqNo(7)=0 is answered with a GapFill whose 34=0 (#580); no per-message DoNotSend veto on replay (L-019-4). Witnessed in-process; the live QuickFIX interop cells skip without a counterparty, so they are not per-PR evidence and are not claimed. <TC-SCOPE>`

### TC-008 (version column)
- Status `done`
- /specify `094-v1-tier0-bookkeeping (reconciliation; delivered by fix/inbound-sequencereset-s023)`
- PR `#90 (squash 13c49f88)`
- Tests `tests/session/test_inbound_sequence_reset.cpp` (InboundSequenceResetTest.ResetModeHardResetsAndBypassesOrdering, .NewSeqNoEqualExpectedIsNoOp, .NewSeqNoBelowExpectedRejects), `tests/interop/parity/inbound_sequencereset_arms_test.cpp` (InboundSequenceResetArms.NewSeqNoAboveExpectedAdvances, .NewSeqNoEqualExpectedIsNoOp, .NewSeqNoBelowExpectedRejects)
- Verified `Reconciled by 094 (2026-10-22) — not a delivery claim. 11a NewSeqNo > NextNumIn sets it regardless of the frame's own 34; 11b equal is accepted; 11c lower is rejected with SessionRejectReason 5 (RefTagID 36), NextNumIn unchanged. Witnessed in-process; the live QuickFIX interop cells skip without a counterparty, so they are not per-PR evidence and are not claimed. <TC-SCOPE>`

### TC-012 (reworded)
- Title `Session conformance: PossResend (test 19, sub-cases 19a–19b) — PossResend(97)=Y delivered to the application with the flag intact; the 19a/19b message-ID duplicate check is the application's`
- Status `done`
- /specify `094-v1-tier0-bookkeeping (reconciliation; delivered by 022-possresend-allowpossdup-send)`
- PR `#103 (squash 97e2e3d9)`
- Tests `tests/session/test_inbound_poss_resend.cpp` (InboundPossResendTest.DeliverInSeq_97Y, .Combo_43Y_97Y, .PossResend_No122_NoReject)
- Verified `Reconciled by 094 (2026-10-22) — not a delivery claim. FIX-TC 19 itself calls the check an "application layer check of message-specific ID"; fixpp does no session-level duplicate detection (L-022-1, as QuickFIX C++/J). Witnessed in-process; the live QuickFIX interop cells skip without a counterparty, so they are not per-PR evidence and are not claimed. <TC-SCOPE>`

### TC-014 (reworded)
- Title `Session conformance (optional): Sequence number synchronization after failure (test 9) — via ResetSeqNumFlag(141)=Y (FIX-TC option 2); outbound SequenceReset-Reset (option 1) not offered`
- Status `done`
- /specify `094-v1-tier0-bookkeeping (reconciliation; delivered by 024-reset-refresh-on-logon + 030 + 032)`
- PR `#106 (squash f3159075) + #113 (squash 3c5ee658) + #115 (squash 9417454f)`
- Tests `tests/session/test_reset_on_lifecycle.cpp` (ResetOnLifecycleTest.ResetOnLogon_Initiator_ResetsAndEmits141, .ResetOnLogon_Acceptor_ResetsIdempotentWith141, .ResetOnLogon_BothRoles_Resets, .Initiator_Received141Ack_NextInboundTwo_NoResend)
- Verified `Reconciled by 094 (2026-10-22) — not a delivery claim. FIX-TC 9 is satisfied by either option; fixpp originates no outbound Reset-mode SequenceReset (outbound is GapFill only). Witnessed in-process; the live QuickFIX interop cells skip without a counterparty, so they are not per-PR evidence and are not claimed. <TC-SCOPE>`

### TC-015 (version column)
- Status `done`
- /specify `094-v1-tier0-bookkeeping (reconciliation; delivered by tests/fix-tc-coverage-gaps-2)`
- PR `#139 (squash fb043752)`
- Tests `tests/interop/parity/fix_tc_coverage_gaps_test.cpp` (FixTcCoverageGaps.BodyFieldsArbitraryOrder_Accepted; ctest interop_parity_us3_tests)
- Verified `Reconciled by 094 (2026-10-22) — not a delivery claim. Fields in non-canonical order (body and header after the first three) are accepted and later MsgSeqNums are accepted (B-005-7). Accepting is what scenario 15 asks for; the same leniency is why 14g is not met (TC-010, backlog). Witnessed in-process; the live QuickFIX interop cells skip without a counterparty, so they are not per-PR evidence and are not claimed. <TC-SCOPE>`

### TC-017 (reworded; not supported by design)
- Title `Session conformance (optional): EncryptMethod(98) (test 17) — 0 accepted; ≠0 refused at Logon (B-043-1); no application-layer encryption ([const §XII.7], [const §XV.10]), so 17c–17j do not apply`
- Status `done`
- /specify `094-v1-tier0-bookkeeping (reconciliation; delivered by 043-plaintext-tcp-transport)`
- PR `#134 (squash 95aa1c38)`
- Tests `tests/session/test_interpret_logon_encrypt_method.cpp` (InterpretLogonEncryptMethod.ZeroEncryptMethodAccepted, .AbsentEncryptMethodAccepted, .NonZeroEncryptMethodRejected; ctest session_interpret_logon_encrypt_method), `tests/session/test_enum_validation_logon.cpp` (EnumValidationLogonTest.InDomainEncryptMethod_EstablishesAsToday, .InDomainNonZeroEncryptMethod_DisconnectsAndSendsNothing, .OutOfDomainEncryptMethod_RejectedAndNotEstablished), `tests/session/fsm_matrix_witness_test.cpp` (FsmMatrixWitness.LS_InboundLogonEncryptMethodNonZero_DisconnectsAndSendsNothing; both files in ctest session_pure_tests)
- Verified `Reconciled by 094 (2026-10-22) — not a delivery claim. Not supported by design (owner, 2026-10-21): FIX deprecated EncryptMethod in 4.3 (TLS/FIXS replaces it, and fixpp ships TLS); QuickFIX C++/J/Go each send 98=0 and implement none of 17c–17j; FIX-TC marks scenario 17 optional, so 17b's Reject 373=7 + Logout is not required. 0 is accepted at session level (EnumValidationLogonTest.InDomainEncryptMethod_EstablishesAsToday) and by the interpreter. ≠0 has two refusal endings. (1) In the dictionary's domain (FIX44 declares 0–6), or with validation off: interpret_logon returns session_invalid_logon (InterpretLogonEncryptMethod.NonZeroEncryptMethodRejected, 98=2), and the Logon arm goes to Disconnected and sends nothing. Witnessed at session level with 98=2 on both roles: the acceptor by EnumValidationLogonTest.InDomainNonZeroEncryptMethod_DisconnectsAndSendsNothing (FIX.4.4), the initiator by FsmMatrixWitness.LS_InboundLogonEncryptMethodNonZero_DisconnectsAndSendsNothing (FIX.4.2); each asserts Disconnected and no frame sent. (2) Outside the domain with validate_inbound_messages on: Reject 373=5 RefTagID 98, and the session does not establish (EnumValidationLogonTest.OutOfDomainEncryptMethod_RejectedAndNotEstablished). Witnessed in-process; the live QuickFIX interop cells skip without a counterparty, so they are not per-PR evidence and are not claimed. <TC-SCOPE>`

## D. Coverage index (FR-011, FR-004)

Line numbers are at `9b176090`. Each edit is same-line except the NFR-019 paragraph. Each edit is
anchored by its text, and the line numbers are a guide only. Apply the same-line edits first and insert
the NFR-019 paragraph last: it lands after the NFR-016 supplemental (:648), so once it is in, the
SVC-005 supplemental moves from :652 to :654.

- **§4 row 3 (:405):** the note's opener changes from `**093 (partial; TC-003 stays backlog):**` to
  `**093; TC-003 done (094 reconciliation):**`. The rest of the note stays, including its
  re-derivation grep.
- **§4 rows 8, 9, 10, 11 (:410–:413):** `005-defer: recovery-dependent — …` is replaced with:
  - 8 → `TC-006 done (094 reconciliation): recovery delivered by 013 (#86), 037 (#121), B28 (#609).`
  - 9 → `TC-014 done (094 reconciliation): option 2 (141=Y) delivered by 024/030/032.`
  - 10 → `TC-007 stays backlog: only 10b has a GapFill-mode witness; 10a/10c/10d/10e unwitnessed (094 row-by-row verification, 2026-10-21; parent repo (private)).`
  - 11 → `TC-008 done (094 reconciliation): Reset-mode SequenceReset delivered by S-023 (#90).`
- **§4 row 19 (:421):** the sentence `The formal \`[FIX-TC]\` scenario-19 conformance cell (TC-012)
  remains backlog (conformance-harness scope, see 005 ledger below).` is replaced with
  `TC-012 done (094 reconciliation): delivered by 022 (#103); the 19a/19b message-ID duplicate check is the application's (L-022-1).`
- **005 deferral ledger, row "ResendRequest / SequenceReset-GapFill / SequenceReset-Reset /
  synchronize-seqnums" (:435):** the last cell is replaced with
  `discharged by 013/S-023/024 — TC-006, TC-008, TC-014 done (094); TC-007 backlog`.
- **005 deferral ledger, row "PossDup / PossResend duplicate semantics (S-010)" (:436):** the
  fragment `only the formal \`[FIX-TC]\` scenario-19 conformance cell (TC-012) remains → later
  conformance-harness work` is replaced with
  `TC-012 done (094 reconciliation): delivered by 022 (#103); the 19a/19b message-ID duplicate check is the application's (L-022-1)`.
- **SVC-005 supplemental (:652):** directly after `**SVC-005 supplemental:**` insert
  ` *(Post-1.0 — constitution v4.0, feature 094: SVC-005 is \`dropped(post-v1.0, planned, not abandoned)\`; the design below, its "Stable from v1.0" proto tier and its "v1.0 RPC surface" describe the post-1.0 release that builds it. v1.0 installs only the empty \`ControlPlaneFactory\` anchor and the codes 900/901.)*`
- **NFR-019 supplemental** (new paragraph, after the NFR-016 supplemental):
  `**NFR-019 supplemental:** io_uring async I/O backend (Linux, opt-in) — a design choice split from NFR-013 (owner 2026-10-21), not a FIX spec section ([const §VI.3]). No constitution clause or design doc requires it; to be measured in the row-16 perf pass. Status: dropped(post-v1.0, planned, not abandoned).`
- **No change** to the W-015 / W-016 entries (:112, :634, :701, :705): they already read post-1.0.
  The earlier draft also said SVC-005's entry already read post-1.0; it did not (Gate A round 1),
  hence the :652 edit above.

## E. Constitution and CHANGELOG (FR-003, FR-003a, FR-003c)

Six clauses change: §IV.4, §VI.6, §XIV.1 (its lead-in and its control-plane bullet), §XIV.3, §XVIII.1
and §XVIII.2. The Status line and a new Sync Impact Report change with them. §I.2, §IV.2, §XIII.3 and
§XVIII.5 are read and left as they are (research R-2, R-12). §VI.6 was added at Gate A round 2 (owner).

### E.1 Sync Impact Report — new block at line 1, above the v2.0 → v3.0 block

PENDING form (lands with the amendment):

```
<!--
Sync Impact Report — v3.0 → v4.0 (2026-10-22) — PENDING
  This header reads PENDING until Codex Gate A on feature 094 has converged AND the owner has signed
    off. The flip to RATIFIED, with the review rounds and the sign-off date filled in below, is the
    last commit before merge, not a claim made in advance (the v1.0 precedent).
  Bump: MAJOR, per Article XX §4, with a CHANGELOG.md entry. Under v3.0 a PR that shipped the gRPC
    control plane or the iceoryx2 SHM data plane before the v1.0 tag shipped required scope
    (Article XVIII §1) and conformed. Under v4.0 the same PR does not: Article IV §4 and Article XIV
    §1 place the control plane post-1.0, and the service wrapper is now an Article XVIII §2 roadmap
    item, to which §5's "no early shipping of post-1.0 protocols into v1.0" applies. A previously
    mergeable PR that now fails is a backwards-incompatible tightening even when the same amendment
    also loosens (here, a v1.0 tag with no service wrapper becomes conformant): the reading v1.0,
    v2.0 and v3.0 set. The first draft of this amendment classified itself MINOR (v3.1) by asking
    whether any EXISTING PR stops conforming, the narrower reading those three reports rejected; it
    was corrected at Gate A round 1 (P1), as v1.0's, v2.0's and v3.0's drafts were. The Article VI
    §6 clarification added at Gate A round 2 is a loosening (every PR mergeable under v3.0's §6 stays
    mergeable), so the classification rests on Article XVIII §5 alone.
    NOTE: the constitution's version is independent of the library's and of the C ABI's.
  Modified: Article IV §4 — the service wrapper is opt-in and post-1.0; when built, gRPC is the
    control-plane transport and iceoryx2 SHM the optional data plane.
  Modified: Article VI §6 — two sentences after (d) say how (b) is met for a process row: a row
    whose subject is a process obligation of this constitution, with no library behaviour under test,
    closes on the per-feature records that obligation names, showing it applied in full, cited in the
    row and labelled where a reader of this repository cannot open them. They do not relax (b) for a
    row that describes behaviour.
  Modified: Article XIV §1 — the lead-in excepts the control plane. The control-plane bullet places
    both the pluggable interface and its default gRPC implementation post-1.0 (SVC-005), and states
    that v1.0 installs only the ABI anchor fixpp::service::ControlPlaneFactory
    (include/fixpp/service/control_plane_factory.hpp, an empty polymorphic base that
    EngineConfig::control_plane_factory holds) and reserves the C-ABI codes 900/901, with no
    behavioural commitment for either.
  Modified: Article XIV §3 — iceoryx2 SHM and sidecar mode are post-1.0.
  Modified: Article XVIII §1 — the gRPC service wrapper and the iceoryx2 SHM data plane leave the
    locked v1.0 scope.
  Modified: Article XVIII §2 — adds the service wrapper as an unscheduled post-1.0 item with no
    version slot. v1.1–v1.8 are not re-ordered.
  Read and unchanged: Article I §2 (the C ABI "exists for … the out-of-process service mode", a
    purpose), Article IV §2 ("the foundation for language bindings and the service wrapper") and
    Article XIII §3 ("control-plane handlers", an example) state no v1.0 scope.
    Article XVIII §5 is unchanged; it reaches the service wrapper through §2.
  Rationale: DECISION 1 (owner, 2026-10-21) takes W-015, W-016 and SVC-001..005 out of the v1.0 tag
    gate and abandons none of them: "We don't want to drop them forever, we want them to implement
    post-v1.0". Roadmap placement (owner, 2026-10-22): unscheduled post-1.0, no version slot.
    Interface timing (owner, 2026-10-22): the control-plane interface goes post-1.0 with its default
    implementation and with SVC-005. The installed header holds only an empty anchor with no
    operations, no fixpp::service::ControlPlane type exists, and nothing in src/ reads
    EngineConfig::control_plane_factory (spec/behaviors-and-limitations.md L-094-1), so no v1.0
    interface existed to keep. Process rows (owner, 2026-10-22, Gate A round 2): NFR-002 states
    Article IX §1's per-PR obligation, which no test can verify; §6(b) read literally would keep every
    such row open forever, and a reading settled only in one feature would bind no later row
    (Article XX §1), so §6 says it.
  Affected catalogue rows: W-015, W-016, SVC-001..005 → dropped(post-v1.0, planned, not abandoned);
    NFR-013 split, its io_uring half → new row NFR-019 (dropped(post-v1.0, …)); NFR-002 closes under
    the amended §6.
  Affected feature specs: none (merged specs/<id>/ bundles stay as written). Companion documents
    annotated by feature 094: .specify/architecture.md (Appendix Z-9), .specify/api-contract.md,
    .specify/2i-capi.md, .specify/2j-controlplane.md, .specify/2l-tap.md, .specify/2m-pybind.md,
    spec/coverage-index.md, spec/behaviors-and-limitations.md (L-094-1), CHANGELOG.md.
  Kept as shipped: include/fixpp/service/control_plane_factory.hpp, and FIXPP_ERR_CTRL_CONFIG (900)
    / FIXPP_ERR_CTRL_RUNTIME (901). Removing a frozen code would be a break for nothing.
  Process: rides feature 094 (specs/094-v1-tier0-bookkeeping). Its PR is titled in Article XX §2
    form, "Constitution: amend §IV.4, §VI.6, §XIV.1, §XIV.3, §XVIII.1, §XVIII.2 — service wrapper
    post-1.0, process-row closure (v4.0) (+ 094 Tier-0 bookkeeping)", and its description states the change, the rationale and the
    affected rows and specs, so §2's four steps are met in order.
  Review and sign-off: Codex Gate A on feature 094 (round 1: NOT CONVERGED, 3 P1 / 5 P2 / 1 P3;
    round 2: NOT CONVERGED, 2 P1 / 3 P2 / 0 P3; later rounds recorded at the flip). Owner sign-off: recorded at the flip.
-->
```

RATIFIED form (the last commit before merge): the header line reads
`Sync Impact Report — v3.0 → v4.0 (<sign-off date>) — RATIFIED`; the first paragraph is replaced by
`Article XX §2 sequence observed in order: Codex Gate A on feature 094 converged (round <n>: <tally>), THEN owner sign-off <date>. This header read PENDING until both had happened.`;
and the last paragraph's two clauses carry every round's tally, the sign-off date and
`Amendment PR #<n>`. Nothing else in the block changes.

### E.2 Status line (`> **Status:**`, line 357 at `9b176090`)

PENDING form — the line's current opening `> **Status:** user-ratified **v3.0** (2026-09-23) — ` is
replaced by:

`> **Status:** **v4.0 — PENDING** (2026-10-22) — **backwards-incompatible** (Article XX §4 v-major, with a `CHANGELOG.md` entry); not ratified until Codex Gate A on feature 094 converges and the owner signs off. The service wrapper — the gRPC control plane, the pluggable control-plane interface and the iceoryx2 SHM data plane — leaves v1.0 scope and becomes unscheduled post-1.0 (Articles IV §4, XIV §1, XIV §3, XVIII §1–§2); v1.0 keeps only the `ControlPlaneFactory` ABI anchor and the C-ABI codes 900/901, with no behavioural commitment. Article VI §6 lets a process row close on the records its rule names. Rides feature 094; its PR is titled in Article XX §2 form. Prior: user-ratified **v3.0** (2026-09-23) — `

and the rest of the line (from `**backwards-incompatible** (Article XX §4 v-major, with a
`CHANGELOG.md` entry). The orchestrator does not implement, …`) is unchanged.

RATIFIED form: `**v4.0 — PENDING** (2026-10-22)` becomes `user-ratified **v4.0** (<sign-off date>)`,
and `not ratified until Codex Gate A on feature 094 converges and the owner signs off` becomes
`Codex Gate A on feature 094 converged at round <n>, then owner sign-off <date>; amendment PR #<n>`.

### E.3 Article IV §4

Before:
`4. **Service wrapper** (`service/`) is opt-in. gRPC is the control-plane transport; iceoryx2 SHM is the optional data-plane (Article XIV §3).`

After:
`4. **Service wrapper** (`service/`) is opt-in and **post-1.0** (Article XVIII §2, unscheduled). When it is built, gRPC is the control-plane transport and iceoryx2 SHM the optional data plane (Article XIV §3).`

### E.3a Article VI §6 (added at Gate A round 2, owner)

Before:
`6. **No PR may close a `done` row without:** (a) a matching `/specify` artifact, (b) verifying tests, (c) Codex Gate B pass, (d) Gate A pass for non-trivial designs.`

After:
`6. **No PR may close a `done` row without:** (a) a matching `/specify` artifact, (b) verifying tests, (c) Codex Gate B pass, (d) Gate A pass for non-trivial designs. For a row whose subject is a process obligation of this constitution, with no library behaviour under test, (b) is met by the per-feature records that obligation names, showing it applied in full (for Article IX §1: verify records that assess every uncovered line and every untaken branch), cited in the row and labelled where a reader of this repository cannot open them. This does not relax (b) for any row that describes behaviour.`

The before-text is `.specify/constitution.md:437` at `9b176090`. The after-text keeps the (a)–(d)
list byte-identical and adds two sentences after it. They carry the Opus round-2 draft's exception,
with the two things the owner's ruling requires added: what the evidence is, and that (b) is not
relaxed for a behaviour row.

### E.4 Article XIV §1 — lead-in and control-plane bullet

Before (lead-in):
`1. **The following are pluggable, each with one default impl in v1.0:**`

After (lead-in):
`1. **The following are pluggable, each with one default impl in v1.0, except the control plane, which is post-1.0:**`

Before (bullet):
`   - **Control plane** (default: gRPC over Unix socket / named pipe).`

After (bullet):
`   - **Control plane** — post-1.0: both the pluggable interface and its default implementation (gRPC over Unix socket / named pipe) (Article XVIII §2; SVC-005). v1.0 installs only the ABI anchor `fixpp::service::ControlPlaneFactory` (`include/fixpp/service/control_plane_factory.hpp`, an empty polymorphic base that `EngineConfig::control_plane_factory` holds) and reserves the C-ABI codes 900/901. It makes no behavioural commitment for either.`

The other four bullets (Transport, Cert source, Logger sinks, MessageStore) are unchanged. The bullet
is a scope statement, not a description of current behaviour, so it stays true when 2j lands.

### E.5 Article XIV §3

Before:
`3. **Data-plane SHM via iceoryx2** is opt-in for sidecar mode. The control plane (gRPC) works without it.`

After:
`3. **Data-plane SHM via iceoryx2** is opt-in for sidecar mode, and iceoryx2 SHM and sidecar mode are both post-1.0 (Article XVIII §2). The control plane (gRPC) works without it.`

### E.6 Article XVIII §1

Before:
`1. **v1.0 scope is locked:** FIX 4.0–5.0SP2 + FIXT.1.1 session + application layers, FIXS over TLS, the C ABI, Python bindings, the gRPC service wrapper, iceoryx2 SHM data plane (opt-in).`

After:
`1. **v1.0 scope is locked:** FIX 4.0–5.0SP2 + FIXT.1.1 session + application layers, FIXS over TLS, the C ABI, Python bindings. The gRPC service wrapper and the iceoryx2 SHM data plane are post-1.0 (§2; amended v4.0).`

### E.7 Article XVIII §2 — one bullet added after `**v1.8 — FIX MMT**`

`   - **Unscheduled — service wrapper** (`fixppd`: the gRPC control plane, the pluggable control-plane interface, the iceoryx2 SHM data plane). Planned post-1.0 with no version assigned (owner, 2026-10-22); not abandoned. *(Added v4.0, feature 094.)*`

### E.8 `CHANGELOG.md` — new entry, newest first (directly after the `---` that follows the "Format follows" line)

Pending form:

```
## Constitution v4.0 — Unreleased

**Backwards-incompatible.** Article XX §4 major bump. Under v3.0 a PR that shipped the gRPC control
plane or the iceoryx2 SHM data plane before the v1.0 tag shipped required scope and conformed. Under
v4.0 both are post-1.0 roadmap items, and Article XVIII §5 reads "No early shipping of post-1.0
protocols into v1.0", so the same PR does not conform. This is the constitution's version: it is not a C-ABI or
library version.

Pending Codex Gate A on feature 094 and the owner's sign-off. This heading reads `Unreleased` until
both have happened.

### Changed

- **Article XVIII §1** — the gRPC service wrapper and the iceoryx2 SHM data plane leave the locked
  v1.0 scope.
- **Article XVIII §2** — the service wrapper becomes an unscheduled post-1.0 item with no version
  slot. v1.1–v1.8 are unchanged.
- **Article XIV §1** — the control plane, both its pluggable interface and its default gRPC
  implementation, is post-1.0. v1.0 installs only the `ControlPlaneFactory` ABI anchor and the C-ABI
  codes 900/901, with no behavioural commitment.
- **Article XIV §3** — iceoryx2 SHM and sidecar mode are post-1.0.
- **Article VI §6** — a row whose subject is a process obligation of the constitution closes on the
  per-feature records that obligation names, in place of verifying tests; a row that describes
  behaviour still needs verifying tests.
- **Article IV §4** — the service wrapper is opt-in and post-1.0.

---
```

Flip form (the last commit before merge): the heading becomes
`## Constitution v4.0 — <sign-off date>`, and the "Pending …" paragraph becomes
`Ratified <date> (PR #<n>, feature 094), after Codex Gate A converged at round <n> and the owner signed off.`

## F. Other surfaces (FR-004, FR-017, FR-018)

Every edit here comes from the research R-12 sweep, where each hit has its disposition. Line numbers
are at `9b176090`; every edit is same-line unless it says otherwise.

### F.1 `.specify/architecture.md`

- **:32** — append to the bullet: ` *(The gRPC service is post-1.0: Appendix Z-9.)*`
- **:64** (module table, row 11) — the last cell `Yes (binary + gRPC schema)` becomes
  `Yes (binary + gRPC schema) — post-1.0, Z-9`.
- **:161** (namespace inventory, `fixpp::service`) — append to its third cell:
  ` Post-1.0 except the installed \`ControlPlaneFactory\` anchor (Z-9).`
- **:343** (§4.11 opening paragraph) — append:
  ` ⚠️ **Post-1.0 (Z-9):** the whole module is on the post-1.0 roadmap; v1.0 installs only the \`ControlPlaneFactory\` anchor.`
- **:357** — `**Catalogue rows:** SVC-001, SVC-002, SVC-003, SVC-004, SVC-005.` becomes
  `**Catalogue rows:** SVC-001, SVC-002, SVC-003, SVC-004, SVC-005 (each \`dropped(post-v1.0, …)\`, Z-9).`
- **:450** (§6 plugin table, `ControlPlane` row) — append to its last cell: ` Post-1.0 (Z-9).`
- **:611** — `### 8.1 Control plane (gRPC, default)` becomes
  `### 8.1 Control plane (gRPC, default) — post-1.0 (Z-9)`.
- **:617** — `### 8.2 Data plane (iceoryx2, opt-in)` becomes
  `### 8.2 Data plane (iceoryx2, opt-in) — post-1.0 (Z-9)`.
- **:642** (§9.3) — `…, the C ABI, the gRPC control-plane proto.` becomes
  `…, the C ABI. The gRPC control-plane proto is post-1.0 (Z-9).`
- **:643** (§9.3) — `the iceoryx2 topic shape,` becomes
  `the iceoryx2 topic shape (post-1.0 with the data plane, Z-9),`.
- **:686** (§11 row 5) — append to its last cell: ` ⚠️ **Z-9: post-1.0 (constitution v4.0).**`
- **:815–:816** (Z-3 body; same two lines, the line count is kept). Before:

  ```
  the surrounding scope has been revisited more than once since. ⚠️ **The disposition of the service
  rows is a live decision that is not this file's to make** — do not read Z-3 as "dropped". It records
  ```
  After:
  ```
  the surrounding scope has been revisited more than once since. ⚠️ **Superseded by Z-9: the service
  rows are post-1.0 and not abandoned (constitution v4.0, feature 094).** It records
  ```
- **New entry at the end of the file** (after "Provenance of Z-6..Z-8"; Appendix Z's rule puts
  everything new there):

  ```
  ### Z-9 — the service module, §8.1/§8.2, §9.3's proto and the iceoryx2 tap are post-1.0

  Constitution v4.0 (feature 094) places the service wrapper on the post-1.0 roadmap, unscheduled:
  the `fixppd` daemon, the gRPC control plane, the pluggable `ControlPlane` interface and the iceoryx2
  SHM data plane, with the iceoryx2 tap consumer (`Iox2Tap`, §4.9 / 2l) that rides it (Article IV §4,
  Article XIV §1 and §3, Article XVIII §1–§2). The design in §4.11, §6, §8.1, §8.2 and §10 row 2j
  stands as design; its timing is post-1.0. Catalogue rows SVC-001..005 read
  `dropped(post-v1.0, planned, not abandoned: …)`. This supersedes Z-3's "live decision" sentence.

  What v1.0 installs is the `ControlPlaneFactory` anchor, an empty polymorphic base with no
  operations, and the C-ABI codes 900/901. `EngineConfig::control_plane_factory` holds one, and
  nothing in `src/` reads it (L-094-1 in `spec/behaviors-and-limitations.md`).

  **Re-derive:** `grep -n "service wrapper" .specify/constitution.md` for the timing;
  `grep -rn control_plane_factory src/` (empty means the field is still unread);
  `grep -rn "class ControlPlane\b" include/ src/` (empty means the interface still does not exist).
  ```

### F.2 `.specify/api-contract.md`

- **:60** (§3.1 pluggable-interface bullet) — append:
  ` *(Except \`ControlPlane\`: the control plane is post-1.0 (constitution v4.0). \`ControlPlaneFactory\` is Stable from v1.0 as an empty polymorphic base; the post-1.0 control plane extends it by derivation and adds no member to it.)*`
  (Gate A round 2, N2-2: the tier is stated once, here, and `:220` and `:249` agree with it. It matches
  the shipped header's own comment, "Full control-plane surface owned by 2j (extends this base)".)
- **:61** (gRPC proto bullet) — append: ` *(Post-1.0 with the service wrapper, constitution v4.0; not a v1.0 surface.)*`
- **:67** (iceoryx2 topic shape bullet) — append: ` *(Post-1.0, constitution v4.0.)*`
- **:113** (`fixpp::service` namespace row) — append to its second cell:
  ` v1.0 installs only \`ControlPlaneFactory\`, an empty anchor; \`ControlPlane\`, \`ControlPlaneConfig\` and \`grpc_control_plane\` are post-1.0 (constitution v4.0).`
- **:222** (`fixppd` row) — the last cell `Stable from v1.0` becomes `Post-1.0 (constitution v4.0)`.
- **:248** (`ControlPlane` interface row) — append to its third cell: ` — post-1.0 (constitution v4.0)`.
- **:220** (`fixpp::service` target row, `Stable from v1.0`) — **no change**. The target installs
  exactly the `ControlPlaneFactory` header, and that header is Stable from v1.0 per the `:60` edit.
- **:249** (`ControlPlaneFactory` row) — the third cell `1 (\`make\`)` becomes
  `1 (\`make\`) — post-1.0, on a type derived from the v1.0 anchor (constitution v4.0)`.
- **:272** (2j row) — append to its third cell:
  ` (post-1.0, constitution v4.0; v1.0 ships only \`ControlPlaneFactory\` as an empty anchor, and the codes 900/901)`.
- **:274** (2l row) — append to its third cell:
  ` (\`Iox2Tap\`, \`TapShmRecord\` and the iceoryx2 service name are post-1.0, constitution v4.0)`.

### F.3 Design-doc status headers (one same-line note each)

- **`.specify/2j-controlplane.md:3`** (`**Status:**` line) — append:
  ` **Timing (constitution v4.0, feature 094):** this design — the \`ControlPlane\` interface, its default gRPC implementation, the proto and \`fixppd\` — is post-1.0, unscheduled (Article XIV §1, Article XVIII §2). v1.0 installs only the \`ControlPlaneFactory\` anchor and the C-ABI codes 900/901, so read every "v1.0" statement below as a post-1.0 design statement.`
- **`.specify/2l-tap.md:5`** (Status row) — the cell `Draft v0.4 — Gate A round 3 converged (Phase A)`
  becomes `Draft v0.4 — Gate A round 3 converged (Phase A). **Timing (constitution v4.0, feature 094):** the iceoryx2 data plane (\`Iox2Tap\`, SVC-002), the gRPC-only mode (SVC-003) and every \`fixppd\` path below are post-1.0, unscheduled (Article XIV §3, Article XVIII §2); read each "binding v1.0 \`fixppd\`" statement as a post-1.0 design statement. The in-process tap (S-036, NFR-014) is not affected.`
- **`.specify/2l-tap.md:54`** (non-goal 7) — append:
  ` *(Both routes are post-1.0 — see Status. v1.0 has no cross-process route for a non-C++ tap consumer.)*`
- **`.specify/2m-pybind.md:5`** (Status row) — append to the cell:
  ` **Timing (constitution v4.0, feature 094):** \`fixppd\`, the gRPC control plane and the iceoryx2 tap are post-1.0, unscheduled; every route below that runs through them (§1.2, §7.10, §7.12, Q4, the tap docstring) is a post-1.0 design statement.`
- **`.specify/2i-capi.md:3`** (`**Status:**` line) — append:
  ` **Timing (constitution v4.0, feature 094):** 2j's control plane is post-1.0, unscheduled. The codes 900/901 stay reserved, and every route below that runs through 2j's control plane (cert/pinset rotation via \`ReloadCertSource\`, the gRPC control-plane registration on \`fixpp_engine_create\`) is a post-1.0 design statement.`

### F.4 Public prose (`README.md`, `docs/src/intro.md`)

- **`README.md:15`** — before:
  `A modern C++23 FIX protocol library. Zero-copy parsing, lock-free hot paths, ABI-agnostic C interface, Python bindings (SWIG), and a gRPC+iceoryx2 service wrapper. Targets FIX 4.0 through 5.0SP2/FIXT.1.1 with ≥90% test coverage via TDD.`
  After:
  `A modern C++23 FIX protocol library. Zero-copy parsing, lock-free hot paths, ABI-agnostic C interface and Python bindings (SWIG); a gRPC+iceoryx2 service wrapper is planned after v1.0. Targets FIX 4.0 through 5.0SP2/FIXT.1.1, test-driven, under the per-PR coverage rule of constitution Article IX §1.`
  (The "≥90%" figure is NFR-002's former 90/80, which `[const §IX.1]` superseded.)
- **`docs/src/intro.md:3`** — `…, Python bindings, and a gRPC + iceoryx2 service wrapper. It covers …`
  becomes `…, and Python bindings, with a gRPC + iceoryx2 service wrapper planned after v1.0. It covers …`.

### F.5 Brain pages

- **`brain/components/service.md`** — inside the `>` blockquote that opens "⚠️ **`[arch §11]` row 5
  still dispositions …**":
  - `see \`architecture.md\` **Appendix Z-3**.` becomes
    `see \`architecture.md\` **Appendix Z-3**, superseded by **Z-9**.`
  - the sentences "Whether the service rows are deferred or formally dropped is a **live decision
    recorded outside this repo** — this page deliberately does **not** answer it. It records only that
    nothing is implemented." are replaced with the text below, every wrapped line keeping the `> `
    prefix:
    `The decision is made: SVC-001..005 are \`dropped(post-v1.0, planned, not abandoned)\` (owner, 2026-10-21), and the constitution places the service wrapper and the control-plane interface unscheduled post-1.0 (Article XIV §1, Article XVIII §2; v4.0, feature 094; \`architecture.md\` Z-9). Nothing is implemented. The one v1.0 surface is the \`ControlPlaneFactory\` anchor, and \`EngineConfig::control_plane_factory\` is accepted and ignored (L-094-1).`
  The paragraph above it ("Precisely: the public interface header exists …") stays. The page cites the
  catalogue rows and the constitution clauses, not line numbers.
- **`brain/components/dictionary.md`**:
  - frontmatter `description:` becomes
    `A substantial shipped subsystem whose catalogue status under-reported it until feature 094 reconciled D-001, D-003, D-007 and D-008 to done.`
  - the title `dictionary — two loaders, a version registry, and a catalogue row that disagrees with the tree`
    becomes `dictionary — two loaders, a version registry, and a catalogue that under-reported them`.
  - the section heading `## ⚠️ The catalogue under-reports this family — a LEAD, not a verdict` stays,
    and a line is added directly under it:
    `> **Resolved 2026-10-22 by feature 094:** D-001, D-003, D-007 and D-008 now read \`done\`, each with its delivering PR and witnessing tests (\`spec/feature-catalogue.md\`). The table below is the 2026-08-29 observation that led there, kept as history.`
  - the table and the notes under it stay as written (they are dated observations).
- **`brain/components/nfr-and-tooling.md`** frontmatter — the `description:` clause
  `and so nfr's uniform backlog is not mistaken for fact.` becomes
  `and so a backlog cell in nfr is read as a lead, not as fact (feature 094 reconciled the rows that had shipped).`
  The title stays: the ⭐ caution under it still applies to the rows left at `backlog`.
- **`brain/components/nfr-and-tooling.md:57`** — the bullet "An out-of-repo planning tracker names a
  set of these rows as *delivered practice, never flipped*, and schedules the flip as a catalogue edit
  with **zero code**." is replaced with:
  `- Feature 094 (2026-10-22) re-verified each candidate row against the tree and flipped the rows that shipped to \`done\`, worded to what shipped; the rows it left at \`backlog\` were checked and are not delivered (its \`spec.md\` FR-012 lists them).`

### F.6 `spec/behaviors-and-limitations.md` — new section at the end of the live file (FR-017, FR-018)

`fixpp#<N>` is the issue FR-018 files before this edit is written; the entry is written with the real
number, never with a placeholder.

```
## 094-v1-tier0-bookkeeping — the service wrapper is post-1.0; v1.0 installs only the control-plane anchor (2026-10-22)

### Limitations

- **L-094-1 — `EngineConfig::control_plane_factory` is accepted and ignored: nothing in the library reads it (fixpp#<N>).** The field is a `std::unique_ptr<fixpp::service::ControlPlaneFactory>`, null permitted (`include/fixpp/core/engine_config.hpp`). `ControlPlaneFactory` (`include/fixpp/service/control_plane_factory.hpp`) is an empty polymorphic base with no operations, and no code under `src/` reads the field, so a factory set there has no effect: no control plane starts, and nothing reports that none did. The control plane, its interface and its default gRPC implementation are post-1.0 (constitution v4.0, Article XIV §1; SVC-001..005 are `dropped(post-v1.0, planned, not abandoned)`); v1.0 keeps the type and the field as an ABI anchor with no behavioural commitment. **Re-derive:** `grep -rn control_plane_factory src/` (empty means still unread). **Status: follow-up** (fixpp#<N>: whether v1.0 should refuse a non-null factory, or document the field as reserved).
```

## G. Licence marking and the build files (FR-013, FR-013a, FR-013b, FR-014)

All of §G is the phase-implementer's (`[const §XVI.6]`: build and configuration files).

- **The script:** `spdx_mark.py`, written in the orchestrator's scratchpad and dry-run against a
  scratch clone (research R-6). The phase-implementer copies it into its own working space and runs
  it on the branch. The script is not committed.
- **C headers:** `include/fix/**/*.h` get `/* SPDX-License-Identifier: AGPL-3.0-or-later */` as line 1.
  All other files get `// SPDX-License-Identifier: AGPL-3.0-or-later`.
- **`conanfile.py:8`:** `license = "AGPL-3.0-only"` becomes `license = "AGPL-3.0-or-later"` (in the
  SPDX commit).
- **`cmake/FixppPackaging.cmake:408`:** `set(CPACK_RPM_PACKAGE_LICENSE "AGPL-3.0")` becomes
  `set(CPACK_RPM_PACKAGE_LICENSE "AGPL-3.0-or-later")` (in the SPDX commit). `AGPL-3.0` is the
  deprecated SPDX short id for AGPL-3.0-only and is the `License:` tag of the RPM the packaging lane
  builds. No test, CI script or tool pins either string (research R-12).
- **`conanfile.py:97–98` (owner, 2026-10-22; FR-013b) — a separate commit, message referencing
  `Refs #607` with no closing keyword.** Before:

  ```
      # cmake, ninja, swig provided by apt in CI and locally; Conan-pinned tools
      # collide with system versions and add resolution time. Restore in Phase 5.
  ```
  After (four lines, the owner's text of Gate A round 2, with the file's four-space indent;
  `tool_requires = []` moves from line 99 to line 101):
  ```
      # cmake and ninja come from the system (apt in CI); swig is installed by each
      # workflow that builds the bindings, at or above the minimum
      # bindings/python/CMakeLists.txt requires. Conan-pinned tools collide with
      # system versions and add resolution time. Restore in Phase 5.
  ```
  The round-1 text said swig is provisioned "at the minimum", which is B58's end state and false on
  `main` until B58 lands: the wheel lanes take PyPI's latest through `bindings/python/pyproject.toml`
  (`swig>=4.2`), and the in-tree Linux legs take apt's. "At or above the minimum" is true on both
  sides of B58 (Gate A round 2, N2-1).
  `git grep -n 'conanfile.py:[0-9]'` outside `specs/` finds no line citation of the file, so the
  moved line breaks no citation.
- **`tools/capi_freeze.sha256`:** regenerated with `sha256sum` over the same 12-file set, in the
  manifest's existing order and two-space format.
- `include/fixpp/service/control_plane_factory.hpp` already carries the AGPL identifier, so it is not
  in the 53-file set and stays byte-unchanged (FR-003b).

## H. Claim → witness map (FR-015b)

The verify record carries this table in full, one row per clause of each flipped row's Title or
version column, and fills the last column with the `file:line` of the `EXPECT`/`ASSERT` (or the CI
step's `if:` and command) it ran. A clause with no mapped assertion does not flip as worded: it is
narrowed, and the narrowing goes into the owner report. Rows read during Gate A round 1 are filled in
here; the others carry the witness to read.

Verify splits every flipped Title and version cell at its clauses and maps each one; a clause missing
from this table is not thereby exempt. Since Gate A round 2 the table lists every clause of the
flipped Titles, including those most at risk of having no assertion: NFR-003's MSVC ASan and
carve-out clauses, NFR-005's PMR clause, TC-014's negative clause and TC-017's encryption clause. A
clause witnessed by a recipe (an absence) has its recipe run first against a seeded positive.

| Row | Clause | Witness | Assertion / step |
|---|---|---|---|
| NFR-001 | C++23 | `CMakeLists.txt` | `CMAKE_CXX_STANDARD 23` + `CMAKE_CXX_STANDARD_REQUIRED ON` |
| NFR-001 | CI on the platform matrix, required before merge | tier1/tier2 workflows; branch protection | job/leg names; `required_status_checks` lists tier1/2/3-required |
| NFR-002 | the obligation is recorded per PR (a process row, `[const §VI.6]`(b) as amended in v4.0) | parent repo (private) 563-564 / 536-537-538 verify records; the FR-020 issue for the 544 record | the T7.7 / T6.7 tables (lines and branches); the issue's number and state |
| NFR-003 | ASan/UBSan/TSan legs run `-LE packaging` | tier1.yml `linux` legs | the ctest step's command, read at verify |
| NFR-003 | an unexplained report fails the leg | tier1.yml step "Peak memory + unexplained-sanitizer gate" | `ci/peak-memory-report.sh` exit status, read at verify |
| NFR-003 | the allocation-failure cells in `ci/expected-preset-conditional-tests.txt` are not built there | tier1.yml steps running `tools/check_sanitizer_test_carveouts.py --self-test` and `tools/check_sanitizer_test_carveouts.py` | the steps' commands and the census output, read at verify |
| NFR-003 | MSVC ASan on Tier 2 (required check tier2-required) | tier2.yml job `windows` leg `windows-msvc-asan`; job `tier2-required` (`needs: [filter, windows]`); branch protection | the leg's matrix entry and the `needs:` line, read at verify; `required_status_checks` lists tier2-required |
| NFR-005 | PMR allocator-aware containers and arena allocation on the parse → validate → dispatch path | at risk: the Tests cell names the gates, not a PMR cell. Candidates: `WireThreeArenaPinning.OffsetTableStorageRoutesThroughPerMessageArena` (parse arena), `MemoryStoreZeroAllocatorCalls.RetrieveDescriptorSnapshotUsesPmr` | read at verify; if no assertion covers validate and dispatch, the clause is narrowed (FR-015b) |
| NFR-005 | zero allocation on the gated windows | tier1.yml #448 steps | `if: matrix.preset == 'linux-gcc-release' \|\| matrix.preset == 'linux-clang-release'`; `ctest … -L mallocnesia --no-tests=error` |
| NFR-011 | XmlLoader routes through the resource | PmrAllocation.EveryByteFromMr | read at verify |
| NFR-011 | OrchestraLoader accepts a resource | `include/fixpp/dict/orchestra_loader.hpp` | `load(path, std::pmr::memory_resource* mr, …)` signature |
| NFR-011 | parse arena / MemoryStore / decimal accessor | the cells in the Tests cell | read at verify |
| NFR-013 | ASIO async I/O transport on Linux and Windows | `tests/transport/` on tier1 `linux` and tier2 `windows` | read at verify (the backend is in Verified as a design fact with its grep, not in the Title) |
| D-001 / D-003 / D-007 / D-008 | each Title clause | the cells in each Tests cell | read at verify |
| TC-003 | 3b/3c/3e disregarded, NextNumIn unchanged, connection open | InboundFrameDispositionsTc.TC003_3* | read at verify |
| TC-005 | NextNumIn incremented (FIX.4.4) | CapiC7Witness.Row6_InboundRejectTrafficKeepsTheSessionUp | `row6_cell` writes Rejects at rising `seq`, then `order(seq, …)`; `expect_kept_up`: `EXPECT_TRUE(o.received)` |
| TC-005 | no Reject of a Reject; session continues | TC005Reject.Fix42_7_* / Fix44_7_*; SessionReject.NoRejectLoopOnInboundReject; FsmMatrixWitness.Active_InboundReject_LogOnly_StaysActive | `tc_reject_test.cpp`: "must not trigger outbound Reject (I-5)", "must stay Active"; `session_reject_test.cpp`: `EXPECT_EQ(f.transport.sent_count(), before)`, `EXPECT_EQ(sess.state(), fsm_state::Active)`; `fsm_matrix_witness_test.cpp`: `EXPECT_EQ(sess.state(), fsm_state::Active)` only |
| TC-006 / TC-008 / TC-012 / TC-014 / TC-015 | each Title clause | the cells in each Tests cell | read at verify |
| TC-014 | outbound SequenceReset-Reset (option 1) not offered | recipe: `git grep -n -i "build_sequence_reset" -- src include` | every hit is `build_sequence_reset_gapfill` (seeded positive: that function's own hits show the grep fires) |
| TC-017 | 0 and absent accepted | InterpretLogonEncryptMethod.ZeroEncryptMethodAccepted, .AbsentEncryptMethodAccepted; EnumValidationLogonTest.InDomainEncryptMethod_EstablishesAsToday (98=0 at session level) | read at verify |
| TC-017 | ≠0 refused, in-domain | InterpretLogonEncryptMethod.NonZeroEncryptMethodRejected (98=2); EnumValidationLogonTest.InDomainNonZeroEncryptMethod_DisconnectsAndSendsNothing (acceptor) and FsmMatrixWitness.LS_InboundLogonEncryptMethodNonZero_DisconnectsAndSendsNothing (initiator), both FR-022 | parser: `session_invalid_logon`; session: state `Disconnected` and no frame sent, each cell proven RED (FR-022) |
| TC-017 | ≠0 refused, out of domain | EnumValidationLogonTest.OutOfDomainEncryptMethod_RejectedAndNotEstablished | `EXPECT_TRUE(has_reject_with_reason(5))`, `EXPECT_EQ(reject_ref_tag_id(5), 98)`, `EXPECT_NE(s, fsm_state::Active)` |
| TC-017 | no application-layer encryption | recipe: `git grep -n -i encrypt -- src include`, minus the EncryptMethod hits | the remaining hits are TLS or transport comments and the EncryptedPassword Length/Data pair, none an encryption implementation (seeded positive: the EncryptMethod hits themselves); `[const §XII.7]` states the rule |
