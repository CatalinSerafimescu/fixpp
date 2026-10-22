# Research: 094 v1.0 Tier-0 bookkeeping

Every decision below is either an owner ruling (dated) or a measurement with its command. Where a
measurement contradicts the verification file or an owner wording, the measurement is stated, and the
deviation goes into the owner report (R-3).

Paths into the owner's research repository are labelled **parent repo (private)**: the library is
public, so a reader of it cannot open them.

## R-1 — Gate A runs, although the brief said "short spec + Gate B"

- **Decision:** run `/gate-a` on the bundle, which includes the amendment.
- **Rationale:** `[const §XX.2]` requires "Codex Gate A review on every amendment". The 068 waiver
  precedent covered only an additive amendment outside every Appendix A trigger. This one narrows a
  *locked* scope (`[const §XVIII.1]`, `[const §XVIII.4]`: "Roadmap changes are constitution
  amendments"), so Gate A is not optional.
- **PR form (owner, 2026-10-22; §VI.6 and `(v4.0)` added at Gate A round 2):** the 094 PR is titled in
  `[const §XX.2]` form,
  `Constitution: amend §IV.4, §VI.6, §XIV.1, §XIV.3, §XVIII.1, §XVIII.2 — service wrapper post-1.0, process-row closure (v4.0) (+ 094 Tier-0 bookkeeping)`,
  and its description states the change, the rationale and the affected rows and specs. §XX.2's four
  steps are then met literally, so there is no deviation to record (FR-003c).
- **Rejected:** a standalone `Constitution: amend …` PR, because the owner wanted one PR carrying
  DECISION 1 and the flips. Also rejected: riding a feature branch as a recorded §XX.2 deviation, the
  first draft's form. Its precedents (035, 043, 069, and 068, 074–082) were all MINOR, while every
  MAJOR amendment so far was a standalone PR (v1.0 #285, v2.0 #451, v3.0 #500), so they did not cover
  a MAJOR amendment (Gate A round 1, N-3).

## R-2 — Amendment classification (`[const §XX.4]`): MAJOR, v3.0 → v4.0

- **Test applied:** the reading the v1.0, v2.0 and v3.0 Sync Impact Reports set. It asks whether a
  *hypothetical* PR that was mergeable under the old text fails under the new one, not whether an
  existing PR stops conforming. Making a previously mergeable change fail is a tightening even when
  the same amendment also loosens.
- **The hypothetical PR:** under v3.0, a PR that ships SVC-001's gRPC control plane before the tag
  ships required scope (`[const §XVIII.1]`) and conforms. Under v4.0 it fails twice:
  - §IV.4 and §XIV.1 place the control plane post-1.0 outright;
  - the wrapper becomes a §XVIII.2 roadmap item, and `[const §XVIII.5]` reads "**No early
    shipping** of post-1.0 protocols into v1.0 to 'get them done.'"
- **Is there a MINOR wording?** Only a carve-out such as "may ship in any release, §5 does not
  apply". That contradicts the owner's ruling, which places the wrapper post-1.0. So no faithful MINOR
  wording exists.
- **The §VI.6 clarification (owner, Gate A round 2) changes nothing here.** It lets a process row close
  on the records its rule names in place of verifying tests (data-model §E.3a). Every PR mergeable
  under v3.0's §VI.6 stays mergeable, so it is a loosening; the MAJOR classification still rests on
  `[const §XVIII.5]` above.
- **Consequence:** v4.0, with a `CHANGELOG.md` entry (data-model §E.8). The Sync Impact Report reads
  PENDING until Gate A converges and the owner signs off, and the flip to RATIFIED is the last commit
  before merge (the v1.0 precedent).
- **History, recorded in the SIR:** the first draft classified the amendment MINOR (v3.1) by the
  existing-PR reading. Codex and the Opus judge both raised it at Gate A round 1 (P1). This is the
  fourth consecutive amendment whose draft was corrected from MINOR to MAJOR at round 1.

## R-3 — TC version columns follow what is witnessed (FR-006a)

- **Measured:**
  - A file-level BeginString count over the witness files (`grep -c FIX.4.2/FIX.4.4/FIXT.1.1`) shows
    0 FIXT.1.1 everywhere.
  - Several files carry only one version: `test_inbound_sequence_reset.cpp`,
    `inbound_sequencereset_arms_test.cpp` (TC-008) and `fix_tc_coverage_gaps_test.cpp` (TC-015) are
    FIX.4.2 only, and `test_interpret_logon_encrypt_method.cpp` (TC-017) is FIX.4.4 only.
  - Some files have no literal at all.
  - The per-test census (fixture defaults followed) is in the table below. It fills the data-model's
    version cells.
- **Decision:** the version column states the per-test union. Where that is narrower than the owner's
  "4.2, 4.4 witnessed" wording, the narrower text is used and listed here for the owner report. The
  owner's rule (narrow to what shipped) governs the wording.

The census traced each test from its fixture or helper to the place the BeginString is set
(2026-10-22, read-only Explore pass). No witness drives FIXT.1.1, and none is parameterised over
versions.

| Row | Witness BeginStrings (per test) | Column written | vs owner wording |
|---|---|---|---|
| TC-003 | 4.2 (`plain_rig::Rig::begin_string` default, via `run_q1`) | `4.2 witnessed; 4.4, FIXT.1.1 not witnessed` | **narrower** (owner: "4.2, 4.4 witnessed") |
| TC-005 | 4.2 (`TC005Reject.Fix42_7_*`, `SessionReject`, `FsmMatrixWitness`) + 4.4 (`TC005Reject.Fix44_7_*`, C-ABI `CapiC7Witness.Row6_*`) | `4.2, 4.4 witnessed; FIXT.1.1 not witnessed` | as owner |
| TC-006 | 4.2 (every session-driving witness; the `Stores/StoreLookupAfterReset.*` cells open no Session, and their `8=FIX.4.4` bytes are an opaque payload) | `4.2 witnessed; 4.4, FIXT.1.1 not witnessed` | narrower |
| TC-008 | 4.2 (`InboundSequenceResetTest` fixture; `ParityAcceptorFixture`) | `4.2 witnessed; 4.4, FIXT.1.1 not witnessed` | **narrower** (owner: "4.2, 4.4 witnessed") |
| TC-012 | 4.2 (`PossDupTestBase::make_cfg`) | `4.2 witnessed; 4.4, FIXT.1.1 not witnessed` | narrower |
| TC-014 | 4.4 (`ResetOnLifecycleTest` fixture) | `4.4 witnessed; 4.2, FIXT.1.1 not witnessed` | narrower |
| TC-015 | 4.2 (`ParityAcceptorFixture`) | `4.2 witnessed; 4.4, FIXT.1.1 not witnessed` | **narrower** (owner: "4.2, 4.4 witnessed") |
| TC-017 | 4.4 (`interpret_logon` called with `expected_begin = "FIX.4.4"`; the enum-validation cells, including FR-022's acceptor cell, feed a FIX44 Logon) + 4.2 (FR-022's initiator cell runs on the `FsmMatrixWitness` fixture, `kBeginStr = "FIX.4.2"`, and drives 98=2). The two `FsmMatrixWitness` refused-Logon cells refuse for a non-98 cause and are no longer cited | `4.2, 4.4 witnessed; FIXT.1.1 not witnessed` (FIX-TC 17 is optional) | as owner, once FR-022's cells land |

**On record:** the unwitnessed versions are required by FIX-TC §4.1. They are filed as **fixpp#614**,
and every TC Verified cell names it (R-10 has the issue's state). That satisfies the owner's narrowing
rule (owner 2026-10-21: "on record … and named in the row's Verified cell").

**Versions the old column named and FIX-TC does not require.** The old columns read "4.0–5.0SP2,
FIXT.1.1". FIX-TC §4.1 says the document "is applicable to all supported versions of the FIX session
layer (4.2, 4.4, FIXT)" (`FIX_Session_Testcases_June_2020.pdf`, parent repo (private) `research/`,
read with `pdftotext` 2026-10-22). So 4.0, 4.1, 4.3, 5.0 and 5.0SP1 are not required by FIX-TC, and
each TC Verified cell says so (data-model `<TC-SCOPE>`).

The owner's text added "FIXT.1.1 shares the code path". That is a claim about the code, and this
feature has measured it nowhere, so the columns say "not witnessed" and nothing more. A reader who
needs the code-path argument finds it in the verification file (TC-003: the Framer checks are
BeginString-independent).

Census side-findings, recorded for the verify record:
- `session_tc_reject` is registered with no LABELS, so only `ctest -R session_tc_reject` selects it.
- The `StoreLookupAfterReset` cells are gtest-named `Stores/StoreLookupAfterReset.<Name>/<Kind>`, so a
  filter needs the `Stores/` prefix.

## R-4 — Open-row census instrument (FR-016)

- **Instrument:** `catalogue_census.py` (scratchpad, used read-only; the verify record reproduces it).
  It parses every catalogue table row by the header's column names, and splits on unescaped `|`.
  Classes: `done`; `postv1` (Status starts `dropped(post-v1.0, planned, not abandoned`); `dropped`;
  `open` (anything else).
- **Base (`9b176090`):** 276 rows, no duplicate IDs. OFFICIAL: done **152 parsed / 154 actual**,
  dropped 1, open **94**, which matches the tracker's re-derived figure. Two rows (C-002, T-042) have
  a literal `|` inside a cell and do not split, so the parser skips them. Both are `done` and outside
  this feature, which is why the actual done count is two higher and the open count is complete. The
  verify record states both figures rather than hiding the two SHAPE lines (Gate A round 1, N-9:
  reproduced independently by a header-free split over `git show 9b176090:spec/feature-catalogue.md`).
- **Known positive:** a mutant copy with NFR-001 set to `done` and W-015 set to the drop form reads
  open 92 and postv1 1. Each class moves by exactly the injected change.
- **Expected after 094:** OFFICIAL open **69** (−25), postv1 **8** (7 + NFR-019), dropped 1, done
  **170 parsed / 172 actual** (+18).

## R-5 — Catalogue parsers survive the drop form

- **Sweep:** `git grep -l feature-catalogue -- ':!specs/' ':!*.md'` over every tracked file. It finds
  `docs/prebuild.py` and `tools/brain_inventory.py`, which parse the catalogue, and five files that
  only mention it in comments or data (`docs/.gitignore`, `tests/interop/live-cells-excluded.txt`,
  `tests/session/test_interpret_logon_encrypt_method.cpp`, `tests/session/test_session_plaintext_authz.cpp`,
  `tests/session/test_session_plaintext_roundtrip.cpp`). The round-1 sweep listed directories
  (`tools .github ci .claude`) and so missed `docs/prebuild.py` (Gate A round 2, C2-4).
- **`brain_inventory.py` behaviour:**
  - It resolves columns by header name and counts the Status *string* per family. It does not parse
    the Status grammar, so commas and colons inside `dropped(...)` cannot break it.
  - Its /specify read takes `bundle.split()[0]`, so `094-v1-tier0-bookkeeping (reconciliation; …)`
    yields the bundle `094-v1-tier0-bookkeeping`.
  - Verify runs it before and after.
- **`docs/prebuild.py` behaviour:**
  - `_status_header` reads every Markdown table with a `Status` header cell, splits each row on `|`
    and counts each raw Status string. The drop strings hold no `|`, and commas and colons are inert,
    so it cannot break. The published "Status at a glance" table gains three rows: the W-015/W-016
    string, the SVC string and NFR-019's.
  - It is not a PR check. `.github/workflows/docs.yml` runs `python3 docs/prebuild.py` on `push: main`
    (paths include `spec/**`) and on `workflow_dispatch`, so a fault would show after merge, on the
    Pages deploy.
  - Verify exercises it without writing the tree (the full script writes `docs/src/`, part of which is
    tracked): load the file by path, since `python3 -I` drops the script directory from `sys.path`,
    and call `_status_header` on the branch's catalogue:
    `python3 -I -c 'import importlib.util as u; s=u.spec_from_file_location("p","docs/prebuild.py"); m=u.module_from_spec(s); s.loader.exec_module(m); print(m._status_header(open("spec/feature-catalogue.md").read()))'`.
    Known positive, run 2026-10-22 on the base: it prints the table (done 158, backlog 112, one
    `dropped(duplicate: …)`). On the branch the three `dropped(post-v1.0, …` strings must appear, with
    counts 2, 5 and 1.

## R-6 — Line motion (dry run, scratch clone)

- **Method:** `git clone --shared` of the branch into the scratchpad, apply `spdx_mark.py`, commit, run
  `tools/check_line_citations.py --shift-audit HEAD~1..HEAD`.
- **Result for the SPDX commit:**
  - 53 files change: 37 inserts (10 `/* … */` C headers, 27 `//`) and 16 identifier replacements.
  - Post-state census: 0 files missing AGPL, 0 carrying another identifier.
  - Shift audit: **GATE PASSED**, 175 rotted citations reported and **0 charged**. All 175 are cited
    from archival `specs/<id>/` bundles, which are point-in-time records and stay as written.
- **Not yet run, and the known trigger (Gate A round 1, N-10):** the docs edits.
  - The new Sync Impact Report at the top of `constitution.md` shifts every line of that file.
  - `tools/check_line_citations.py` carries fixture strings that name `constitution.md:456` (one
    without a `citation-ok` marker), `constitution.md:335` and `2j-controlplane.md:902` / `:22`.
  - The data-model §F edits are same-line, so `2j-controlplane.md` and `architecture.md` lines above
    Appendix Z do not move. Some other edits add lines: the NFR-019 catalogue row, the coverage-index
    NFR-019 paragraph, the B&L section, Z-9 at the end of `architecture.md` and the CHANGELOG entry
    (data-model §D orders its edits for this). No tracked file outside `specs/` cites
    `feature-catalogue.md:<n>` or `coverage-index.md:<n>`
    (`git grep -n -E "(feature-catalogue|coverage-index)\.md:[0-9]+" -- ':!specs/'` is empty). The
    `conanfile.py` comment gains two lines; `git grep -n 'conanfile.py:[0-9]'` outside `specs/` finds no
    citation of it.
  - So the docs commits get their own `--shift-audit` on a scratch commit before the PR (quickstart
    §5), and again over the real branch range at verify.

## R-7 — C-ABI freeze

- `tools/check_capi_freeze.sh` hashes `include/fix/c_api.h` and every `include/fix/c_api/*.h` against
  `tools/capi_freeze.sha256`. 10 of the 37 inserts are in that set, so the manifest is regenerated
  (`sha256sum` over the same sorted set). The change is comment-only.
- The exported symbol set is unchanged by construction. Verify confirms it with the repo's C-ABI
  occupancy check and a symbol diff of `libfixpp_capi` built before and after.

## R-8 — Provenance and relicensing basis

- The grep for `copyright|derived from|adapted from|ported from|licen[cs]e|BSL|boost software` across
  the 37 unmarked files gives one hit, `peer_identity.hpp` ("value type derived from a verified peer
  cert"). That is prose, not provenance.
- The `atomic_shared_ptr` spike in the parent (parent repo (private)
  `research/G19-fix-fpml-iso20022/atomic-shared-ptr/`) carries no licence or copyright text.
- `[const §V.4]` vendored code (`async_mutex`, BSL-1.0 attribution) is already SPDX-marked and is not
  in the set.
- **The 16 Apache-2.0 files have one author.** `git log --format='%an <%ae>'` over them gives only
  Catalin Serafimescu, under two addresses (123 per-file commit entries). The owner holds the copyright, so the
  relicense is the owner's to make.
- **The relicense applies from this commit on.** fixpp is public, so versions already published
  under Apache-2.0 keep that grant for anyone who has them. The PR body says so (FR-019).
- **Out of scope, on record:** 40 files outside `src/`/`include/` still carry
  `SPDX-License-Identifier: Apache-2.0` (37 under `tests/`, 3 under `bench/`;
  `git grep -l "SPDX-License-Identifier: Apache-2.0" -- ':!src/' ':!include/' ':!specs/'`). The
  orchestrator files one labelled follow-up issue for them at implement time (FR-021), and its real
  number is recorded here.

## R-9 — Delivering PRs resolved from GitHub (`gh pr view <n> --json mergeCommit`)

| PR | Commit on main (squash unless marked) | Branch |
|---|---|---|
| #66 | `ae4c9a56` (merge commit) | 002-dictionary-xml-loader |
| #67 | `b8a3b251` (merge commit) | 003-dictionary-codegen |
| #68 | `2a6f76c7` (merge commit) | 004-wire-codec |
| #77 | `718195f9` | 008-message-store |
| #82 | `ba2222d6` | 009-session-fsm-finalize (005 FSM) |
| #85 | `53e25b1e` | 012-2h-transport |
| #86 | `bd84e086` | 013-session-reconnect-binding |
| #90 | `13c49f88` | fix/inbound-sequencereset-s023 |
| #103 | `97e2e3d9` | 022-possresend-allowpossdup-send |
| #106 | `f3159075` | 024-reset-refresh-on-logon |
| #113 | `3c5ee658` | 030-received-reset-inbound-advance |
| #115 | `9417454f` | 032-initiator-reset-outbound-advance |
| #121 | `795cde1b` | 037-resend-reply-possdup-tags |
| #134 | `95aa1c38` | 043-plaintext-tcp-transport |
| #139 | `fb043752` | tests/fix-tc-coverage-gaps-2 |
| #166 | `ff19ae58` | the oldest commit adding `tier2-required` (`git log --reverse -S tier2-required -- .github/workflows/tier2.yml`) |
| #282 | `a776d0af` | ci/sanitizer-counter-fail-closed |
| #478 | `a8fe9f79` | fix/448-mallocnesia-gates-in-ci |
| #554 | `e84991bf` | 093-inbound-frame-dispositions |
| #572 | `2e4d15c6` | 544-hot-path-zero-alloc |
| #609 | `9b176090` | 536-537-538-recovery-liveness |
| — | `a476bd30` | Phase 3 Step 3 CI matrix (pre-PR infra) |
| — | `96dd8aed` | `ci(tier1): add tier1-required gate` (direct push; oldest `-S tier1-required` hit) |
| — | `27f8701d` | `ci: restore pull_request trigger + uniform Gate A∧B gate (all 3 tiers)` (direct push) |

Enforcement read 2026-10-22: `gh api repos/CatalinSerafimescu/fixpp/branches/main/protection/required_status_checks --jq .contexts`
lists `tier1-required`, `tier2-required`, `tier3-required` and the two gate-label checks.

## R-10 — Residuals on record (checked 2026-10-22)

- **Open issues:** #555, #565, #569, #580, #588, #612, #263 (`gh issue view`).
- **#614 snapshot** (`gh issue view 614 --repo CatalinSerafimescu/fixpp`, 2026-10-22): **OPEN**,
  created 2026-10-22T08:46:26Z, labels `enhancement`, `follow-up`, title "FIX-TC witnesses drive one
  BeginString per row: no FIX 4.4/4.2 or FIXT.1.1 cell for TC-003/005/006/008/012/014/015/017". The
  verify record repeats this snapshot, since the library cannot show an issue's state. #614 is not yet
  placed in parent repo (private) `phases/phase-4/issue-batches.md`; close-out places it (plan.md).
- **Live B&L entries** (defined in `spec/behaviors-and-limitations.md`, the live file): L-003-2,
  L-022-1, L-497-1 (the open half; its resolved half moved to the closed file), L-002-4, B-043-1,
  B-005-7, B-495-3, B-544-1, L-019-4.
- **New, filed at implement:** the issue behind L-094-1 (`EngineConfig::control_plane_factory` is
  accepted and ignored; FR-018); the 544 record's lapse, named in NFR-002's Verified cell (FR-020);
  the 40 `Apache-2.0` files outside `src/`/`include/` (FR-021); `[const §III.4]` not met on `main`,
  named in plan.md's Constitution Check and the FR-013b comment (FR-023). Each is written here with
  its real number. Close-out places all four, and #614, in parent repo (private)
  `phases/phase-4/issue-batches.md`.

## R-11 — The cited tests exist (51 now, 2 added by FR-022)

- The grep required `TEST*(Suite, Name` with the name half. A negative control
  (`InboundPossResendTest.NoSuchCellXyz`) reported MISSING; the first version of the grep, which only
  matched the suite, had passed it.
- 46 of the first 47 matched directly. `TC003_3c_GarbledBeginStringBeforeAGoodFrame_DisregardedAndContinues`
  exists but its `TEST(` declaration is line-wrapped.
- Added at Gate A round 1 for TC-017, each read in the file: `FsmMatrixWitness.NC_InboundRefusedLogon_BeginStringMismatch_TransitionsToDisconnected`
  (`tests/session/fsm_matrix_witness_test.cpp`), `FsmMatrixWitness.LS_InboundRefusedLogon_TransitionsToDisconnected`
  (same file), `EnumValidationLogonTest.OutOfDomainEncryptMethod_RejectedAndNotEstablished`
  (`tests/session/test_enum_validation_logon.cpp`). Both files are sources of `session_pure_tests`.
  That made 50.
- Changed at Gate A round 2 for TC-017 (FR-022): the two `FsmMatrixWitness` refused-Logon cells leave
  the Tests cell, because they refuse for another cause; `EnumValidationLogonTest.InDomainEncryptMethod_EstablishesAsToday`
  joins it (98=0 at session level; it exists, read in the file); and the two FR-022 cells join it.
  So 50 − 2 + 1 = 49 cited tests existed by that round's count, and 51 once FR-022 lands. That count
  is superseded by the round-3 re-derivation below.
- **Re-derived at Gate A round 3 (2026-10-22), from data-model.md's Tests cells as they now read.**
  - The population is every gtest entry a Tests cell names: each `Suite.Name`, each pattern
    (`Suite.Prefix*`, `Suite.*`) counted once, and each parameterised instantiation counted once.
    `DictionaryLookupFixture`'s "FIX42 instantiation", "FIX50SP2 + FIXT11 instantiations" and
    `/AllRuntimeVersions` are all the one `INSTANTIATE_TEST_SUITE_P(AllRuntimeVersions, DictionaryLookupFixture`.
    A test cited by two rows counts once. Files, ctest entries and signatures are not entries.
  - Existence check, over every `.cpp`/`.hpp`/`.h`/`.cc` under `tests/`, matching across line breaks:
    `\bTEST(_F|_P)?\(\s*Suite\s*,\s*Name\s*\)` for a named cell (the name half required); for a
    pattern, at least one `TEST*(Suite, Prefix…`; for an instantiation, its `INSTANTIATE_TEST_SUITE_P(Inst, Suite`
    and at least one cell of the suite.
  - Negative controls, both reported MISSING: `InboundPossResendTest.NoSuchCellXyz` (named cell) and
    `RecoveryAdminSpanGapfillTest.NoSuchPrefixXyz*` (pattern).
  - Result: **53 distinct entries; 51 exist today; the 2 MISSING are the FR-022 cells**, added at
    implement, for **53** cited in all. The difference from round 2's 49/51 is partly N3-3 (NFR-011 now
    names `WireThreeArenaPinning.PerMessageArenasAreIsolated` in place of the glob, +1) and partly the
    counting rule, which round 2 did not write down; the list below is the one to re-run.
  - The 53, by the first row citing each:
    - NFR-005: `WireThreeArenaPinning.OffsetTableStorageRoutesThroughPerMessageArena`, `MemoryStoreZeroAllocatorCalls.RetrieveDescriptorSnapshotUsesPmr`
    - NFR-011: `PmrAllocation.EveryByteFromMr`, `WireThreeArenaPinning.PerMessageArenasAreIsolated`, `MemoryStoreCapacity.EngineMrAppliedWhenConfigResourceIsNull`, `CodegenTypedAccessor.DecimalAccessorTakesMemoryResource`
    - D-001: `AllRuntimeVersions/DictionaryLookupFixture.*`, `DelimiterCensus.RedCountsReconcileWithSpecBaseline`, `DictEnumCensus.SC011_*`, `BuilderCompleteness077Vt11V42.V42RegistryExactSetEqualsRawXmlWalk`
    - D-003: `DictionaryLookupFixture.CrossVersionIsolation`, `DelimiterCensus.RegisteredGroupCountMatchesCodegenFix50Sp2`, `DelimiterCensus.NoChangeDictionariesUnchanged`, `DictEnumCensus.SC010_EveryMessageTypeCoveredByMsgTypeCodesetOrCodesetEmpty`, `BuilderCompleteness077Vt11V42.Vt11ExpectedEmptyNoFileEmitted`
    - D-008: `CodegenTypedAccessor.MsgTypeAndVersionAreConstexpr`, `DeterminismTest.GeneratedMatchesGolden`
    - TC-003: `InboundFrameDispositionsTc.TC003_3b_*`, `.TC003_3c_*`, `.TC003_3e_*`
    - TC-005: `CapiC7Witness.Row6_InboundRejectTrafficKeepsTheSessionUp`, `TC005Reject.Fix42_7_ReceiveReject_NoLoop`, `TC005Reject.Fix44_7_ReceiveReject_NoLoop`, `SessionReject.NoRejectLoopOnInboundReject`, `FsmMatrixWitness.Active_InboundReject_LogOnly_StaysActive`
    - TC-006: `RecoveryStoreHorizonTest.AvailableRangeReplaysCarryPossDupFlag`, `.ReplayFramesCarryOrigSendingTime`, `.LargeAppFrameReplayed_NotGapFilled`, `.PreHorizonGapCollapsedToGapFill`, `RecoveryAdminSpanGapfillTest.*`, `ResendReplyPossDup.Cell1_GapFill_Carries_43Y_And_122_EqOwn52`, `Stores/StoreLookupAfterReset.*`
    - TC-008: `InboundSequenceResetTest.ResetModeHardResetsAndBypassesOrdering`, `.NewSeqNoEqualExpectedIsNoOp`, `.NewSeqNoBelowExpectedRejects`, `InboundSequenceResetArms.NewSeqNoAboveExpectedAdvances`, `.NewSeqNoEqualExpectedIsNoOp`, `.NewSeqNoBelowExpectedRejects`
    - TC-012: `InboundPossResendTest.DeliverInSeq_97Y`, `.Combo_43Y_97Y`, `.PossResend_No122_NoReject`
    - TC-014: `ResetOnLifecycleTest.ResetOnLogon_Initiator_ResetsAndEmits141`, `.ResetOnLogon_Acceptor_ResetsIdempotentWith141`, `.ResetOnLogon_BothRoles_Resets`, `.Initiator_Received141Ack_NextInboundTwo_NoResend`
    - TC-015: `FixTcCoverageGaps.BodyFieldsArbitraryOrder_Accepted`
    - TC-017: `InterpretLogonEncryptMethod.ZeroEncryptMethodAccepted`, `.AbsentEncryptMethodAccepted`, `.NonZeroEncryptMethodRejected`, `EnumValidationLogonTest.InDomainEncryptMethod_EstablishesAsToday`, `.OutOfDomainEncryptMethod_RejectedAndNotEstablished`, and the FR-022 cells `EnumValidationLogonTest.InDomainNonZeroEncryptMethod_DisconnectsAndSendsNothing`, `FsmMatrixWitness.LS_InboundLogonEncryptMethodNonZero_DisconnectsAndSendsNothing`
  - Verify re-runs this check, with both negative controls, after implement; it must then report 53 of 53.
  - `/speckit-analyze` C2 (2026-10-22) removed `tests/fuzz/fuzz_dict_xml_loader.cpp` from D-007's Tests
    cell. It was cited as a file and names no gtest entry, so by the counting rule above it was never
    one of the 53, and the counts do not change: 53 distinct, 51 exist today, 2 MISSING (the FR-022
    cells).
- **NFR-011 witnesses:**
  - `PmrAllocation.EveryByteFromMr` (XmlLoader)
  - `OrchestraLoader::load(path, std::pmr::memory_resource*, …)` (signature,
    `include/fixpp/dict/orchestra_loader.hpp`). Its tests pass a `std::pmr::monotonic_buffer_resource`;
    none asserts where the allocations land.
  - `WireThreeArenaPinning.OffsetTableStorageRoutesThroughPerMessageArena` (parse arena)
  - `WireThreeArenaPinning.PerMessageArenasAreIsolated` (parse arena; named in place of the glob at Gate A
    round 3, N3-3)
  - `MemoryStoreZeroAllocatorCalls.RetrieveDescriptorSnapshotUsesPmr`
  - `MemoryStoreCapacity.EngineMrAppliedWhenConfigResourceIsNull` (MemoryStore)
  - `CodegenTypedAccessor.DecimalAccessorTakesMemoryResource` (typed decimal accessor)

## R-12 — The cross-surface sweep (FR-004)

> **Gate A instance 2 disposition (2026-10-22): L-005-1 is "keep: still true".** The live B&L entry
> `L-005-1` says the recovery-dependent FIX-TC cases are deferred. 094 flips TC-006, TC-008 and TC-014,
> but TC-007 (scenario 10, GapFill mode) stays `backlog`, so the entry still describes a real deferral.
> FR-017 keeps every live entry other than the new L-094-1 unchanged.

### Recipe

Over every tracked file outside `specs/`, four `git grep` commands: (a) is two of them (the
dropped-scope terms and the io_uring grep), then (b) and (c):

```
# (a) dropped-scope terms (service[- ]mode|out-of-process added at Gate A round 2, N2-4)
git grep -n -i -I -E "grpc|iceoryx|service wrapper|fixppd|sidecar|control[- ]plane|service[- ]mode|out-of-process" -- ':!specs/'
git grep -n -i -I -E "io_uring|liburing" -- ':!specs/'
# (b) every changed or added row ID (25 changed + NFR-019)
git grep -n -w -I -E "W-015|W-016|SVC-00[1-5]|NFR-00[1235]|NFR-01[139]|D-00[1378]|TC-00[3568]|TC-01[2457]" -- ':!specs/'
# (c) licence metadata
git grep -n -I -E "AGPL-3\.0-only|CPACK_RPM_PACKAGE_LICENSE|license *=" -- ':!specs/'
```

`git grep` is used because it searches tracked files only and needs no `xargs`. ⚠️ The first attempt
piped `git ls-files` into `xargs command grep`, which returned **0 lines**: `command` is a shell
builtin, so `xargs` could not run it (`xargs: command: No such file or directory`, exit 127). A zero
from that pipeline would have been read as "no surfaces". It is recorded here because it is this
repository's most common instrument defect.

### Proof the instrument fires

- (a): `README.md:15` ("… and a gRPC+iceoryx2 service wrapper"). The first draft's sweep reported
  "`README.md`: 0 hits" for the same file; this one reports it.
- (b): `brain/components/dictionary.md:75` (`D-007` … `backlog`).
- (a)'s widened terms: `.specify/constitution.md:374` (§I.2, "the out-of-process service mode"). The
  round-1 terms did not reach it.
- Counts at `9b176090` (the branch carries only `specs/` commits): (a) **664** lines in 79 files (the
  round-1 terms gave 647 in 77; the 17 lines only the widened terms add are dispositioned below),
  plus **4** io_uring lines; (b) **330** lines in 64 files; (c) the two licence lines below.

### Disposition classes

- **edit** — exact target text in data-model §D, §E, §F or §G.
- **header** — covered by one same-line timing note in the document's status header (data-model
  §F.3); the hit is a design statement about the post-1.0 release.
- **keep** — states no v1.0 scope and no old status of a changed row; the reason is given.
- **other concept** — the term means something else here (the engine's control strand, the OTLP gRPC
  exporter, a capture "sidecar" file, a `.sha256` sidecar).
- **pattern FP** — the regex matched an unrelated token (`_FixppDecimalT` for `fixppd`,
  `kGrpClassStart` for `grpc`, base64 in a `.pem`; `-w` matching `NFR-003` in `NFR-003-3`).

### Hits that state scope or status, one per row

| Hit | Says | Disposition |
|---|---|---|
| `.specify/constitution.md:415` §IV.4 | gRPC is the control-plane transport | edit (§E.3) |
| `.specify/constitution.md:587`, `:589` §XIV.1 | control plane, default gRPC, v1.0 | edit (§E.4) |
| `.specify/constitution.md:594` §XIV.3 | iceoryx2 sidecar | edit (§E.5) |
| `.specify/constitution.md:680` §XVIII.1 | the gRPC service wrapper is locked v1.0 scope | edit (§E.6) |
| `.specify/constitution.md` §XVIII.2 | the roadmap has no wrapper line | edit (§E.7) |
| `.specify/constitution.md:374` §I.2 | the C ABI "exists for non-C++ consumers, language bindings, and the out-of-process service mode" | keep: a purpose, no v1.0 timing |
| `.specify/constitution.md:413` §IV.2 | the C ABI is "the foundation for … the service wrapper" | keep: a dependency direction, no timing |
| `.specify/constitution.md:579` §XIII.3 | "control-plane handlers" as an example | keep: an example of out-of-session code |
| `.specify/constitution.md` §XVIII.5 | no early shipping of post-1.0 protocols | keep: unchanged, reaches the wrapper through §2 (R-2) |
| `.specify/architecture.md:32` | "the gRPC service" among the public surfaces | edit (§F.1) |
| `.specify/architecture.md:64` | module row 11, `fixppd` gRPC + iceoryx2 | edit (§F.1) |
| `.specify/architecture.md:161` | `fixpp::service` interfaces | edit (§F.1) |
| `.specify/architecture.md:343`, `:357` | §4.11 module and its rows | edit (§F.1) |
| `.specify/architecture.md:450` | §6 plugin table, `ControlPlane` default gRPC | edit (§F.1) |
| `.specify/architecture.md:611`, `:617` | §8.1, §8.2 headings | edit (§F.1) |
| `.specify/architecture.md:642` | §9.3 "Stable from v1.0 … the gRPC control-plane proto" | edit (§F.1) |
| `.specify/architecture.md:643` | §9.3 Provisional "the iceoryx2 topic shape" | edit (§F.1) |
| `.specify/architecture.md:686` | §11 row 5, ControlPlane shape "Phase 2" | edit (§F.1) |
| `.specify/architecture.md:815–816` | Z-3 "a live decision … do not read Z-3 as dropped" | edit (§F.1); new Z-9 at the end |
| `.specify/api-contract.md:60`, `:61`, `:67` | §3.1 Stable from v1.0: `ControlPlane`, `ControlPlaneFactory`, the proto; §3.2 iceoryx2 topic | edit (§F.2) |
| `.specify/api-contract.md:113`, `:222`, `:248`, `:249`, `:272`, `:274` | `fixpp::service` row, `fixppd` "Stable from v1.0", interface rows, 2j/2l rows | edit (§F.2) |
| `.specify/2j-controlplane.md` (225 lines) | the control-plane design, "v1.0 RPC surface" (§1 goal 4) | header (§F.3) |
| `.specify/2l-tap.md:54` | non-goal 7: non-C++ tools use iceoryx2 or gRPC in v1.0 | edit (§F.3) |
| `.specify/2l-tap.md:568`, `:806`, `:813`, `:977`, `:1151` | "binding v1.0 `fixppd` path" | header (§F.3) |
| `.specify/2m-pybind.md:53`, `:917–918`, `:1394–1396`, `:1406`, `:1476` | Python reaches control plane / tap through `fixppd` in v1.0 | header (§F.3) |
| `.specify/2i-capi.md:89`, `:1544`, `:1574`, `:1674` | rotation and engine registration through 2j's control plane | header (§F.3) |
| `spec/coverage-index.md:652` | SVC-005 "Stable from v1.0" proto, "v1.0 RPC surface" | edit (§D) |
| `spec/coverage-index.md:405` | "TC-003 stays backlog" | edit (§D) |
| `spec/coverage-index.md:410–413` | `005-defer` on scenarios 8–11 | edit (§D) |
| `spec/coverage-index.md:421`, `:436` | TC-012 "remains backlog" | edit (§D) |
| `spec/coverage-index.md:435` | 005 ledger, scenarios 8–11 | edit (§D) |
| `spec/feature-catalogue.md:285–289` | SVC-001..005 at `backlog` | edit (§A) |
| `spec/feature-catalogue.md` rows of the 18 flips, NFR-013 | old Status/Title | edit (§C) |
| `README.md:15` | "and a gRPC+iceoryx2 service wrapper"; "≥90% test coverage" (NFR-002's old figure) | edit (§F.4) |
| `docs/src/intro.md:3` | "and a gRPC + iceoryx2 service wrapper" | edit (§F.4) |
| `brain/components/service.md` | "a live decision recorded outside this repo" | edit (§F.5) |
| `brain/components/dictionary.md:3–4`, `:75–77` | D-007 "says backlog"; D-003/D-008 `backlog` | edit (§F.5) |
| `brain/components/nfr-and-tooling.md:4` (frontmatter) | "so nfr's uniform backlog is not mistaken for fact" | edit (§F.5) |
| `brain/components/nfr-and-tooling.md:57` | "an out-of-repo planning tracker … schedules the flip" (no dropped-scope term and no row ID, so neither (a) nor (b) reaches it: found by the Opus review) | edit (§F.5) |
| `cmake/FixppPackaging.cmake:408` | `CPACK_RPM_PACKAGE_LICENSE "AGPL-3.0"` (AGPL-3.0-only) | edit (§G) |
| `conanfile.py:8` | `license = "AGPL-3.0-only"` | edit (§G) |
| `include/fixpp/core/engine_config.hpp:171` | `control_plane_factory` member, read by nothing in `src/` | new limitation L-094-1 (§F.6) + issue (FR-018) |

### Hits kept, by file group

| Group | Lines | Why kept |
|---|---|---|
| `.specify/2j-controlplane.md:1452–1469`, `:1682`; `.specify/2k-log-otel.md:1695`, `:1703`, `:1729`, `:1737` | SVC rows at `backlog` inside the docs' Appendix D drop-in blocks | historical proposals; 2j's header note covers 2j, and 2k's copies propose nothing about timing |
| `.specify/2k-log-otel.md` (rest of 39) | OTLP gRPC exporter; `StreamLogs` deferred to v1.x | other concept / already deferral |
| `.specify/2d-threading.md`, `2f-async-mutex*.md`, `563-564-callback-send.md`, `2e-msgstore.md:1833` | "control-plane handlers", engine control strand | other concept |
| `.specify/2e-msgstore.md:1132`, `:1284`; `544-hot-path-zero-alloc.md:1875` | "sidecar" audit entry / private sidecar | other concept |
| `.specify/2g-tls.md:59`, `:1056–1058`, `:1152` | runtime cert reload owned by 2j | keep: ownership, no v1.0 timing; in-process rotation (T-040) is done, and Z-9 / 2j's header give the timing |
| `.specify/2h-transport.md` (9), `.specify/2c-codegen.md:1795–1903`, `2a-decimal.md`, `2b-wire.md:724` | 2j cross-references, control-plane examples | keep: ownership or examples |
| `.specify/2b-wire.md:51` | W-015, W-016 post-v1 | keep: already post-1.0 |
| `.specify/architecture.md:6`, `:62`, `:79`, `:347–355`, `:451`, `:469`, `:518`, `:580–603`, `:614–621`, `:663`, `:665` | the service design, the boundary rule, `fixppd` "not yet built" | keep: design, covered by the edited headings and Z-9; the AGPL boundary rule still governs the installed `fixpp::service` |
| `.specify/api-contract.md:300` | how a Provisional iceoryx2 shape may change | keep: a change rule, no timing |
| Widened terms only (14 of the 17 lines): `.specify/2i-capi.md:1789`; `.specify/2j-controlplane.md:107`, `:1110`, `:1231`, `:1371`, `:1615`; `.specify/2l-tap.md:1271`; `.specify/2m-pybind.md:159`, `:1552`; `.specify/2k-log-otel.md:1328`; `.specify/architecture.md:19`, `:593`, `:605`, `:716` | the `[arch §8]` service-mode boundary rule: its heading, TOC entry, lint note and cross-references | keep: the AGPL boundary rule, no timing; it still governs the installed `fixpp::service` (2i/2j/2l/2m also carry their §F.3 timing header) |
| `.specify/constitution.md:374` (widened terms only) | §I.2, above | keep |
| `.specify/codex-review.md:72` (widened terms only) | "an out-of-process Codex CLI call" | other concept |
| `CLAUDE-history.md:12` (widened terms only) | 086's changelog entry on the service-mode boundary | keep: history |
| `spec/feature-catalogue.md:226` (T-040), `:281` (section heading) | "gRPC trigger remains v1.x"; "Service (Daemon/Sidecar)" | keep: already deferral / a heading |
| `spec/coverage-index.md:552` | T-039/T-041 full rows wait on "2j `ReloadCertSource` control-plane" | keep, and reported as informational (spec §Clarifications, Gate A round 2): the dependency now waits on post-1.0 work, and T-039/T-040/T-041 already read `done` |
| `spec/behaviors-and-limitations.md:388–390` (L-015-3) | "Bounded below the Phase-5 service wrapper" | keep: already later |
| `spec/behaviors-and-limitations.md:470`, `:807–833`; `-closed.md:70`, `:77` | engine control strand | other concept |
| `spec/behaviors-and-limitations.md:75`, `:78`, `:85`, `:102`, `:1542`, `:3347` | row IDs as anchors; spec-local `NFR-003-3` | keep / pattern FP |
| `brain/components/session.md:493–495`, `engine-accept-path.md:75–76`, `test.md:881` | control-plane domain; capture sidecar | other concept |
| `brain/index.md:64`, `brain/render-architecture.md:102` | "a stub by decision" | keep: still true |
| `docs/src/tls-quickstart.md:83`, `docs/src/transport-quickstart.md:88` | "owned by 2j" in a not-covered list | keep: ownership, no timing |
| `conanfile.py:29–42`, `:61`, `:118` | grpc as a Phase-5 dependency; OTel `with_otlp_grpc=False` | keep: already later |
| `cmake/ProjectOptions.cmake:9` | `FIXPP_BUILD_SERVICE` OFF | keep: the option exists, off |
| `src/service/CMakeLists.txt:2–3`, `src/tap/CMakeLists.txt:2` | module description comments | keep: design, no timing |
| `include/fix/c_api/error.h:20`, `:145–150`; `src/capi/error.cpp:360–364`; `tools/abi_history/error_codes_v1.txt:73` | codes 900/901 | keep: FR-003b |
| `include/fixpp/service/control_plane_factory.hpp:9`, `:23` | the anchor's own comment | keep: FR-003b |
| `include/fixpp/log/*`, `include/fixpp/otel/providers.hpp`, `src/config/logger_resolver.cpp`, `tests/config/*`, `tools/otel_smoke/collector.yaml` | OTLP gRPC exporter / `use_grpc` | other concept |
| `include/fixpp/session/engine.hpp`, `src/session/engine.cpp`, `src/capi/engine.cpp:237`, `tests/session/test_engine_session_strand.cpp`, `tests/session/CMakeLists.txt:2127` | engine control strand | other concept |
| `tests/interop/**`, `tests/codegen/CMakeLists.txt:58` | capture / selection "sidecar" | other concept |
| `.github/workflows/{tier2,parallelism-measure}.yml`, `ci/assert-ci-lane-policy.py:235` | `.sha256` sidecar | other concept |
| `tests/oracle/conftest.py`, `tests/codegen/test_082_*`, `tests/codegen/vlatest_manifest_*`, `tests/tls/fixtures/ca-db/newcerts/1004.pem` | `_FixppDecimalT`, `kGrpClassStart`, base64 | pattern FP |
| `tests/session/test_interpret_logon_encrypt_method.cpp:10` | "(TC-017 backlog)" | keep: history ("S-021 documented as …"), and a test file outside SC-005 |
| `tests/conformance/test_transport_interop.cpp`, `tests/conformance/CMakeLists.txt:44` | `TC-001..TC-017` as DISABLED transport cells | keep: a different numbering (there TC-012 is a BodyLength/CheckSum case); the cells are still disabled, and no Verified cell claims them |
| `tests/session/conformance/tc_reject_test.cpp`, `tests/session/conformance/CMakeLists.txt:73` | TC-005 cells | keep: the witnesses themselves |
| `.specify/2c-codegen.md:7–8`, `:2117`; `.specify/constitution.md:447`, `:616`; `.specify/architecture.md:138`, `:189`, `:210`, `:324`, `:763`; `.specify/2h-transport.md:1393`, `:1518`; `.specify/2m-pybind.md:1806`, `:1818` | rows named as owned, cited or in rule text | keep: no status claim |
| `src/`, `include/`, `tools/codegen/`, `cmake/templates/`, `bench/`, `tests/` other ID hits | a row ID cited as an anchor in a comment | keep: no status claim (the status-word filter `backlog|not … done|pending|deferred|remains|stays` over them finds only the two kept lines above) |
| `bench/**`, `docs/src/dictionary/codegen.md`, `.specify/495-493-486-*.md`, `.specify/ci209-bench-gate.md` | `NFR-002-1`, `NFR-003-1..7` | pattern FP (spec-local IDs) |
| `tools/brain_link_decisions.py:19–20` | `D-007`, `NFR-001` as examples of ids not to match | keep |
| `.specify/2h-transport.md:837`, `.specify/544-hot-path-zero-alloc.md:170`, `tests/support/recycler_driver.hpp:54` | io_uring as an example / `ASIO_HAS_IO_URING` guard | keep: no v1.0 claim |

### Licence metadata (c)

`conanfile.py:8` and `cmake/FixppPackaging.cmake:408` are the only two non-or-later licence fields;
`bindings/python/pyproject.toml:78` already reads `AGPL-3.0-or-later`. No test, CI script or tool pins
either string (`git grep -n -E "RPM_PACKAGE_LICENSE|AGPL-3\.0\"" -- tests ci .github tools cmake`
returns only the definition). `README.md:11` (badge), `README.md:25–26`, `NOTICE:7` and
`LICENSE-COMMERCIAL.md:3` say "AGPL-3.0" in prose. They are not package metadata, so they are kept and
reported (the default recorded in spec §Clarifications, Gate A round 2).

### Re-run after the edits

At verify the four `git grep` commands run again. Every remaining hit must be in a "keep", "header", "other
concept" or "pattern FP" class above, or have its "edit" applied. The count per class goes into the
verify record (SC-003).

## R-13 — NFR-002's evidence (Gate A round 1, C-3; round 2, C2-1 and N2-3)

- `[const §IX.1]`'s binding rule: "only if every uncovered **line/branch** carries a recorded Opus risk
  assessment in `.specify/decisions/<feature>-verify.md`". The first draft's Title said "every
  uncovered line" and so stated less than the clause it cites. The Title now says "line/branch".
- `.specify/decisions` is a gitignored symlink into parent repo (private)
  `research/G19-fix-fpml-iso20022/decisions/speckit/` (library `.gitignore`, the `decisions/` entry
  and the symlink note beside it). The records are tracked there.
- What each candidate record assesses (read 2026-10-22):
  - `563-564-callback-send-verify.md` §"Coverage (T7.7): the Article IX §1 assessment, line by line":
    lines and branches ("Seven branches on added lines were never taken").
  - `536-537-538-recovery-liveness-verify.md` §"T6.7 — coverage per changed line": lines and branches
    ("26 untaken branches on 25 changed lines").
  - `544-hot-path-zero-alloc-verify.md` Step 4: reports 81.73 % branch and dispositions added lines
    only, short of the rule's "every uncovered line/branch". PR #572 merged on it. It is not cited as
    evidence, and it is not dropped silently either: by the Edge Cases rule, a residual is on record
    before the row flips, so the orchestrator files one labelled issue for it (FR-020) and NFR-002's
    Verified cell names that issue.
- **How the row closes (owner, Gate A round 2):** `[const §VI.6]`(b) asks for "verifying tests", and
  NFR-002 has none: it is a process obligation. v4.0 amends §VI.6 (data-model §E.3a) so that a row whose
  subject is a process obligation of the constitution closes on per-feature records of the kind that
  obligation names, cited in the row, each showing it applied in full; for `[const §IX.1]`, verify
  records that assess every uncovered line and every untaken branch. The exception does not relax (b) for any row that describes
  behaviour. NFR-002's Verified cell cites §VI.6 as amended. The two cited records meet that
  definition; the 544 record does not, and is the FR-020 issue.
- `.claude/skills/speckit-verify/SKILL.md` is tracked in the parent, not here, and its Step 4 reads a
  metric below threshold as a FAIL. It is no longer cited.

## R-14 — TC-017, read in source (Gate A round 1, N-1)

- `interpret_logon` sets a sticky flag on any `98` whose value is not exactly `0` and returns
  `session_invalid_logon` (`src/session/admin_messages.cpp`, the `case 98:` scan and the
  `invalid_encrypt_method_seen` check).
- On the acceptor (NotConnected arm) and the initiator (LogonSent arm), `src/session/session.cpp`
  handles any refusal with `record_state_transition_(fsm_state::Disconnected); co_return`, with no
  send and no branch on the cause. The disposer's comment states the same ending ("the Logon refusal,
  as when interpret_logon refuses: Disconnected, nothing sent").
- The validate-first gate runs before `interpret_logon` when `validate_inbound_messages` is on. An
  out-of-domain `98` (FIX44 declares 0–6) is then rejected with `emit_session_reject_` (373=5,
  371=98), witnessed by `EnumValidationLogonTest.OutOfDomainEncryptMethod_RejectedAndNotEstablished`.
- At the base no test drives `98=1..6` through a Session (`git grep -n -E "98=[1-9]"` over `tests/`
  finds only the parser cells and a fuzz seed). FR-015b's own rule says a clause with no mapped
  assertion does not flip as worded, so FR-022 adds the two session-level cells (owner, Gate A round
  2): an acceptor cell in `tests/session/test_enum_validation_logon.cpp` and an initiator cell in
  `tests/session/fsm_matrix_witness_test.cpp`, both sources of `session_pure_tests`
  (`tests/session/CMakeLists.txt`, `add_executable(session_pure_tests …)`), so no ctest entry is added.
  Each asserts Disconnected and no frame sent. The mutant that proves each RED deletes the
  `if (invalid_encrypt_method_seen)` return in `interpret_logon` (`src/session/admin_messages.cpp`);
  the refusal is in the interpreter, not in `session.cpp`, whose arms only act on its result.
- **A stale comment, P3, not edited.** The initiator arm's comment in `on_inbound_logon_sent_` says
  every refusal "transitions to Disconnected (with Logout in US4)", while the code sends nothing.
  FR-022's initiator cell asserts the code's behaviour. Editing the comment would put a non-licence
  line into `src/` and break SC-005, so it is noted here and in plan.md §Gate A instead.
- FIX-TC 17 is "Optional" (§4.5.16), and 17b expects Reject 373=7, NextNumIn incremented, Logout and
  disconnect. Neither ending sends that, which the owner's "not supported by design" ruling accepts.
- So the source supports the owner's wording "≠0 refused at Logon": both endings refuse the Logon and
  the session does not establish. No owner question is needed for the Title.

## R-15 — Test file → CMake target → ctest entry → CI preset (C1 of `/speckit-analyze`, 2026-10-22)

The ctest entries T030, T049 and quickstart §2 run are derived here from data-model §C's Tests cells,
not hand-listed. fixpp groups test sources into whole-binary executables (`[const §VII.8]`, the 068
grouping), so a source file names a **binary**, and one ctest entry often covers several cited files.

**Recipe (re-run it; do not copy the table forward):**
1. List every path under `tests/` that a data-model §C Tests cell names.
2. For each, `git grep -n -F <basename> -- 'tests/**/CMakeLists.txt'`, ignoring comment lines, and walk
   back to the enclosing call (`add_executable`, `add_wire_test`, `add_threading_test`,
   `fixpp_add_store_test`, `fixpp_add_interop_test`, …). That call's first argument is the target.
3. Find the target's `add_test(NAME …)`: it is inside the helper for the helpers above, and explicit
   otherwise. The NAME is not always the target name.
4. Read the `if(…)` guards enclosing the registration.
5. Confirm each NAME with `ctest --test-dir build/<preset> -N`, matched as a whole line, with a
   negative control (a made-up NAME must not match).

Three traps the recipe hit, each of which a plain basename grep gets wrong:
- `tests/dictionary/pmr_allocation_test.cpp` is built by the `FIXPP_DICT_STANDALONE` `foreach` in
  `tests/dictionary/CMakeLists.txt`, which derives the source name from the target name
  (`dictionary_pmr_allocation_test` → `pmr_allocation_test.cpp`). The basename appears nowhere in a
  CMake file.
- Two files are named `determinism_test.cpp`. D-008 cites `tests/codegen/determinism_test.cpp`
  (`codegen_determinism_test`). `tests/dictionary/determinism_test.cpp` is a source of
  `dictionary_pure_tests` and holds a different suite.
- Several ctest NAMEs differ from their target: `transport_smoke` (target `transport_smoke_test`),
  `delimiter_census` (`dictionary_delimiter_census_test`), `capi_dictionary_load`
  (`capi_dictionary_load_test`), `capi_inbound_frame_dispositions`
  (`capi_inbound_frame_dispositions_test`), `session_tc_reject` (`tc_reject_test`).

**CI preset.** Every entry below is in the population of tier1.yml job `linux`'s
`ctest --preset <preset> --output-on-failure -LE packaging --no-tests=error` step, which runs on every
leg (`linux-clang-{debug,release,asan,ubsan,tsan}`, `linux-gcc-release`); tier2.yml job `windows`
runs the same `-LE packaging` selection. Verify runs each entry locally on `linux-clang-debug`, selected
as `-R '^<NAME>$'` (anchored: several NAMEs are prefixes of others). `[const §VII.8]` prefers label
selection, but several of these entries carry no LABELS and the rest share labels with entries no row
cites, so a label would run a different set. Verify also reads which of them
register on the Windows presets from tier2's run. The one exception is NFR-005's mallocnesia gates, below.

| Source file (data-model §C) | Rows | Target (registration) | ctest NAME | Notes |
|---|---|---|---|---|
| `tests/wire/three_arena_pinning_test.cpp` | NFR-005, NFR-011 | `wire_pure_tests` (`add_wire_test`, `tests/wire/CMakeLists.txt`) | `wire_pure_tests` | grouped binary |
| `tests/session/test_memory_store_zero_allocator_calls.cpp` | NFR-005, NFR-011 | `store_memory_zero_allocator_calls` (`fixpp_add_store_test`, `tests/session/CMakeLists.txt`) | `store_memory_zero_allocator_calls` | the helper sets no LABELS |
| `tests/dictionary/pmr_allocation_test.cpp` | NFR-011 | `dictionary_pmr_allocation_test` (`FIXPP_DICT_STANDALONE` foreach, `tests/dictionary/CMakeLists.txt`) | `dictionary_pmr_allocation_test` | standalone: in-TU global-new counting |
| `tests/dictionary/orchestra_loader_test.cpp` | NFR-011 | `dictionary_orchestra_tests` (`tests/dictionary/CMakeLists.txt`) | `dictionary_orchestra_tests` | |
| `tests/session/test_memory_store_capacity.cpp` | NFR-011 | `store_memory_capacity` (`fixpp_add_store_test`) | `store_memory_capacity` | the helper sets no LABELS |
| `tests/codegen/typed_accessor_test.cpp` | NFR-011, D-008 | `codegen_pure_tests` (`tests/codegen/CMakeLists.txt`) | `codegen_pure_tests` | grouped binary |
| `tests/transport/transport_smoke_test.cpp` | NFR-013 | `transport_smoke_test` (`tests/transport/CMakeLists.txt`) | `transport_smoke` | NAME ≠ target; no LABELS |
| `tests/transport/test_asio_plain_transport.cpp` | NFR-013 | `transport_asio_plain_transport` | `transport_asio_plain_transport` | |
| `tests/transport/test_listener_acceptor.cpp` | NFR-013 | `transport_listener_acceptor` | `transport_listener_acceptor` | |
| `tests/transport/test_cancellation_propagation.cpp` | NFR-013 | `transport_cancellation_propagation` | `transport_cancellation_propagation` | |
| `tests/dictionary/lookup_test.cpp` | D-001, D-003, D-007 | `dictionary_pure_tests` (`tests/dictionary/CMakeLists.txt`) | `dictionary_pure_tests` | grouped binary |
| `tests/dictionary/delimiter_census_test.cpp` | D-001, D-003 | `dictionary_delimiter_census_test` | `delimiter_census` | NAME ≠ target |
| `tests/dictionary/dict_enum_census_test.cpp` | D-001, D-003 | `dictionary_dict_enum_census_test` | `dictionary_dict_enum_census_test` | |
| `tests/codegen/test_077_v42_vt11_completeness_and_c4.cpp` | D-001, D-003 | `codegen_077_v42_vt11_completeness_c4_test` | `codegen_077_v42_vt11_completeness_c4_test` | |
| `tests/dictionary/xml_loader_test.cpp` | D-007 | `dictionary_pure_tests` | `dictionary_pure_tests` | |
| `tests/dictionary/negative_paths_test.cpp` | D-007 | `dictionary_pure_tests` | `dictionary_pure_tests` | |
| `tests/dictionary/load_any_test.cpp` | D-007 | `dictionary_load_any_tests` | `dictionary_load_any_tests` | |
| `tests/capi/dictionary_load_test.cpp` | D-007 | `capi_dictionary_load_test` (`tests/capi/CMakeLists.txt`) | `capi_dictionary_load` | NAME ≠ target |
| `tests/codegen/vlatest_compile_smoke_test.cpp` | D-008 | `codegen_vlatest_tests` | `codegen_vlatest_tests` | inside `if(FIXPP_CODEGEN_FIX_LATEST)`, an option that defaults ON (`cmake/Codegen.cmake`); its evidence is `static_assert`, so building the binary is the witness |
| `tests/dictionary/ref_shape_test.cpp` | D-008 | `dictionary_pure_tests` | `dictionary_pure_tests` | |
| `tests/codegen/read_tier_byte_diff_test.cmake` | D-008 | none: a `cmake -P` script registered by `add_test` in `tests/codegen/CMakeLists.txt` | `fixpp::dict::read-tier-byte-diff` | not gtest: the pass is ctest's result plus the script's own output |
| `tests/codegen/determinism_test.cpp` | D-008 | `codegen_determinism_test` | `codegen_determinism_test` | not `tests/dictionary/determinism_test.cpp` |
| `tests/session/inbound_frame_dispositions_test.cpp` | TC-003 | `session_inbound_frame_dispositions` (`add_threading_test`) | `session_inbound_frame_dispositions` | |
| `tests/capi/inbound_frame_dispositions_c7_witness_test.cpp` | TC-005 | `capi_inbound_frame_dispositions_test` | `capi_inbound_frame_dispositions` | NAME ≠ target |
| `tests/session/conformance/tc_reject_test.cpp` | TC-005 | `tc_reject_test` (`tests/session/conformance/CMakeLists.txt`) | `session_tc_reject` | NAME ≠ target; no LABELS (R-3) |
| `tests/session/session_reject_test.cpp` | TC-005 | `session_pure_tests` (`tests/session/CMakeLists.txt`) | `session_pure_tests` | grouped binary |
| `tests/session/fsm_matrix_witness_test.cpp` | TC-005, TC-017 | `session_pure_tests` | `session_pure_tests` | holds FR-022's initiator cell |
| `tests/session/test_recovery_store_horizon.cpp` | TC-006 | `session_pure_tests` | `session_pure_tests` | |
| `tests/session/test_recovery_admin_span_gapfill.cpp` | TC-006 | `session_pure_tests` | `session_pure_tests` | |
| `tests/session/test_resend_reply_possdup.cpp` | TC-006 | `session_resend_reply_possdup` (`add_threading_test`) | `session_resend_reply_possdup` | |
| `tests/session/test_538_store_lookup_after_reset.cpp` | TC-006 | `session_538_store_lookup_after_reset` (`add_threading_test`) | `session_538_store_lookup_after_reset` | gtest filter needs the `Stores/` prefix (R-3) |
| `tests/session/test_inbound_sequence_reset.cpp` | TC-008 | `session_pure_tests` | `session_pure_tests` | |
| `tests/interop/parity/inbound_sequencereset_arms_test.cpp` | TC-008 | `interop_parity_us3_tests` (`fixpp_add_interop_test`, `tests/interop/CMakeLists.txt`) | `interop_parity_us3_tests` | also selected by tier1's interop gate step (`-L interop`) |
| `tests/session/test_inbound_poss_resend.cpp` | TC-012 | `session_inbound_poss_resend` | `session_inbound_poss_resend` | |
| `tests/session/test_reset_on_lifecycle.cpp` | TC-014 | `session_reset_on_lifecycle` | `session_reset_on_lifecycle` | |
| `tests/interop/parity/fix_tc_coverage_gaps_test.cpp` | TC-015 | `interop_parity_us3_tests` | `interop_parity_us3_tests` | |
| `tests/session/test_interpret_logon_encrypt_method.cpp` | TC-017 | `session_interpret_logon_encrypt_method` (`add_threading_test`) | `session_interpret_logon_encrypt_method` | |
| `tests/session/test_enum_validation_logon.cpp` | TC-017 | `session_pure_tests` | `session_pure_tests` | holds FR-022's acceptor cell |

**The distinct ctest entries**, in the order T030 names them: `wire_pure_tests`,
`store_memory_zero_allocator_calls`, `store_memory_capacity`, `dictionary_pmr_allocation_test`,
`dictionary_orchestra_tests`, `codegen_pure_tests`, `transport_smoke`,
`transport_asio_plain_transport`, `transport_listener_acceptor`, `transport_cancellation_propagation`,
`dictionary_pure_tests`, `delimiter_census`, `dictionary_dict_enum_census_test`,
`codegen_077_v42_vt11_completeness_c4_test`, `dictionary_load_any_tests`, `capi_dictionary_load`,
`codegen_vlatest_tests`, `fixpp::dict::read-tier-byte-diff`, `codegen_determinism_test`,
`session_inbound_frame_dispositions`, `capi_inbound_frame_dispositions`, `session_tc_reject`,
`session_pure_tests`, `session_resend_reply_possdup`, `session_538_store_lookup_after_reset`,
`interop_parity_us3_tests`, `session_inbound_poss_resend`, `session_reset_on_lifecycle`,
`session_interpret_logon_encrypt_method`. Each matched exactly one whole line of
`ctest --test-dir build/linux-clang-debug -N` (2026-10-22; the negative control `NoSuchEntryXyz`
matched none). Before this table, T030 and quickstart §2 named only some of them and covered the rest
with "the codegen and transport entries"; that phrase also left out the dictionary entries
`delimiter_census` and `dictionary_dict_enum_census_test`.

**NFR-005's mallocnesia gates.** `ctest --preset <preset> -L mallocnesia --no-tests=error` on
`linux-gcc-release` and `linux-clang-release`, the presets the `if:` of tier1.yml's #448 steps names.
`tests/alloc_guard/CMakeLists.txt` registers the positive controls in that label:
`alloc_guard_positive_control_mallocnesia` unconditionally, and the per-entry-point
`alloc_guard_*_positive_control_mallocnesia` controls inside `if(FIXPP_MALLOCNESIA_SUPPORTED)`.

**Preset carve-outs.** No cited gtest suite appears in `ci/expected-preset-conditional-tests.txt`
(searched for each suite name of R-11's list; the known positive: the same search finds the
`WireLifetimeTrap*` cells the file lists).

**Cited, and not ctest entries** (their evidence is read, not run): `include/fixpp/dict/orchestra_loader.hpp`
(NFR-011, a signature); `CMakeLists.txt` (NFR-001, the C++ standard); the tier1/tier2 jobs, steps and
`if:` lines, `ci/expected-sanitizer-reports.txt`, `ci/expected-preset-conditional-tests.txt` and
`tools/check_sanitizer_test_carveouts.py` (NFR-001, NFR-003, NFR-005, NFR-013: CI rows, FR-015);
NFR-002's two verify records (parent repo (private)). `tests/fuzz/fuzz_dict_xml_loader.cpp` is no
longer cited by D-007 (data-model §C).
