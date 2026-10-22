# Research: 094 v1.0 Tier-0 bookkeeping

Every decision below is either an owner ruling (dated) or a measurement with its command. Where a
measurement contradicts the verification file or an owner wording, the measurement is stated, and the
deviation goes into the owner report (R-3).

## R-1 — Gate A runs, although the brief said "short spec + Gate B"

- **Decision:** run `/gate-a` on the bundle, which includes the amendment.
- **Rationale:** `[const §XX.2]` requires "Codex Gate A review on every amendment". The 068 waiver
  precedent covered only an additive amendment outside every Appendix A trigger. This one narrows a
  *locked* scope (`[const §XVIII.1]`, `[const §XVIII.4]`: "Roadmap changes are constitution
  amendments"), so Gate A is not optional.
- **Alternatives:** a standalone `Constitution: amend …` PR. Rejected: the owner wanted one docs PR,
  and the 035/043/069 precedents rode feature branches with the deviation recorded (FR-003a).

## R-2 — Amendment classification (`[const §XX.4]`): MINOR, v3.0 → v3.1 (proposed; Gate A decides)

- **Test applied:** the reading recorded in v1.0's and v3.0's Sync Impact Reports. An amendment is
  backwards-incompatible when a PR that conformed under the old text does not conform under the new
  one (a tightening). §4 names two such classes: banned-pattern additions and perf-budget tightening.
- **Argument:**
  - The amendment removes items from v1.0 scope and adds one unscheduled roadmap line. No banned
    pattern is added and no budget changes.
  - No PR that conformed under v3.0 stops conforming: nothing in the tree implements the service
    wrapper (`src/service/` is an INTERFACE stub), and the installed interface header and C-ABI codes
    900/901 are kept (FR-003b).
  - The change loosens in one direction only: a v1.0 tag without a gRPC service, non-conformant under
    v3.0, becomes conformant.
- **Counter-argument recorded, for Gate A to weigh:** a reader of v3.0 was promised a v1.0 service
  wrapper, and withdrawing a promise could be called incompatible for that reader. It is not a §4
  class. No downstream consumer exists before the first public release (`[const §X.7]`'s pre-release
  posture), so nothing outside the project depended on the promise.
- **Consequence:** MINOR needs no `CHANGELOG.md` entry. If Gate A reclassifies it MAJOR, the bump
  becomes v4.0 and an entry is added. Either outcome is mechanical.

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
| TC-017 | 4.4 (`interpret_logon` called with `expected_begin = "FIX.4.4"`; pure parser test) | `4.4 witnessed; 4.2, FIXT.1.1 not witnessed` (FIX-TC 17 is optional) | narrower |

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
- **Base (`9b176090`):** 276 rows, no duplicate IDs. OFFICIAL: done 152, dropped 1, open **94**, which
  matches the tracker's re-derived figure. Two rows (C-002, T-042) have a literal `|` inside a cell
  and do not split. Both are `done` and outside this feature, so the open count is complete. The
  verify record states this rather than hiding the two SHAPE lines.
- **Known positive:** a mutant copy with NFR-001 set to `done` and W-015 set to the drop form reads
  open 92 and postv1 1. Each class moves by exactly the injected change.
- **Expected after 094:** OFFICIAL open **69** (−25), postv1 **8** (7 + NFR-019), dropped 1, done
  **170** (+18).

## R-5 — Catalogue parsers survive the drop form

- **Sweep:** `grep -rln feature-catalogue tools .github ci .claude`. It finds `tools/brain_inventory.py`
  (and only a prose reference in the speckit-tasks skill).
- **`brain_inventory.py` behaviour:**
  - It resolves columns by header name and counts the Status *string* per family. It does not parse
    the Status grammar, so commas and colons inside `dropped(...)` cannot break it.
  - Its /specify read takes `bundle.split()[0]`, so `094-v1-tier0-bookkeeping (reconciliation; …)`
    yields the bundle `094-v1-tier0-bookkeeping`.
  - Verify runs it before and after.

## R-6 — Line motion from the SPDX inserts (dry run, scratch clone)

- **Method:** `git clone --shared` of the branch into the scratchpad, apply `spdx_mark.py`, commit, run
  `tools/check_line_citations.py --shift-audit HEAD~1..HEAD`.
- **Result:**
  - 53 files change: 37 inserts (10 `/* … */` C headers, 27 `//`) and 16 identifier replacements.
  - Post-state census: 0 files missing AGPL, 0 carrying another identifier.
  - Shift audit: **GATE PASSED**, 175 rotted citations reported and **0 charged**. All 175 are cited
    from archival `specs/<id>/` bundles, which are point-in-time records and stay as written.
  - No `brain/`, `spec/`, `.specify/` or source-comment citation moves.
- **Still to run at verify:** the parent's `tools/brain-external-sweep.sh` (refs_external), and the
  shift audit again over the real branch range. The constitution edit moves `.md` lines, which check
  [1] does cover.

## R-7 — C-ABI freeze

- `tools/check_capi_freeze.sh` hashes `include/fix/c_api.h` and every `include/fix/c_api/*.h` against
  `tools/capi_freeze.sha256`. 10 of the 37 inserts are in that set, so the manifest is regenerated
  (`sha256sum` over the same sorted set). The change is comment-only.
- The exported symbol set is unchanged by construction. Verify confirms it with the repo's C-ABI
  occupancy check and a symbol diff of `libfixpp_capi` built before and after.

## R-8 — Provenance of the 37 unmarked files

- The grep for `copyright|derived from|adapted from|ported from|licen[cs]e|BSL|boost software` across
  the 37 files gives one hit, `peer_identity.hpp` ("value type derived from a verified peer cert").
  That is prose, not provenance.
- The `atomic_shared_ptr` spike in the parent (`research/G19-fix-fpml-iso20022/atomic-shared-ptr/`)
  carries no licence or copyright text.
- `[const §V.4]` vendored code (`async_mutex`, BSL-1.0 attribution) is already SPDX-marked and is not
  in the set.

## R-9 — Delivering PRs resolved from GitHub (`gh pr view <n> --json mergeCommit`)

| PR | Commit on main (squash unless marked) | Branch |
|---|---|---|
| #66 | `ae4c9a56` (merge commit) | 002-dictionary-xml-loader |
| #67 | `b8a3b251` (merge commit) | 003-dictionary-codegen |
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
| #282 | `a776d0af` | ci/sanitizer-counter-fail-closed |
| #478 | `a8fe9f79` | fix/448-mallocnesia-gates-in-ci |
| #554 | `e84991bf` | 093-inbound-frame-dispositions |
| #572 | `2e4d15c6` | 544-hot-path-zero-alloc |
| #609 | `9b176090` | 536-537-538-recovery-liveness |
| — | `a476bd30` | Phase 3 Step 3 CI matrix (pre-PR infra) |

## R-10 — Residuals on record (checked 2026-10-22)

- **Open issues:** #555, #565, #569, #580, #588, #612 (`gh issue view`).
- **Live B&L entries** (defined in `spec/behaviors-and-limitations.md`): L-003-2, L-022-1, L-497-1 (the
  open half; its resolved half moved to the closed file), L-002-4, B-043-1, B-005-7, B-495-3, B-544-1,
  L-019-4.

## R-11 — The 47 cited tests exist

- The grep required `TEST*(Suite, Name` with the name half. A negative control
  (`InboundPossResendTest.NoSuchCellXyz`) reported MISSING; the first version of the grep, which only
  matched the suite, had passed it.
- 46 matched directly. `TC003_3c_GarbledBeginStringBeforeAGoodFrame_DisregardedAndContinues` exists
  but its `TEST(` declaration is line-wrapped.
- **NFR-011 witnesses:**
  - `PmrAllocation.EveryByteFromMr` (dictionary loader)
  - `WireThreeArenaPinning.OffsetTableStorageRoutesThroughPerMessageArena` (parse arena)
  - `MemoryStoreZeroAllocatorCalls.RetrieveDescriptorSnapshotUsesPmr`
  - `MemoryStoreCapacity.EngineMrAppliedWhenConfigResourceIsNull` (MemoryStore)
  - `CodegenTypedAccessor.DecimalAccessorTakesMemoryResource` (typed decimal accessor)

## R-12 — Surfaces naming the dropped scope (FR-004 sweep)

Command: `grep -niE "grpc|iceoryx|service wrapper|control plane|sidecar|io_uring"` over
`.specify/constitution.md`, `.specify/architecture.md`, `.specify/2j-controlplane.md`,
`.specify/2h-transport.md`, `README.md`, `brain/`.

- **Constitution (amended, FR-003):**
  - §IV.4
  - §XIV.1 (control-plane bullet)
  - §XIV.3
  - §XVIII.1
  - §XVIII.2 (new line)
  - §IV.2 ("foundation for … the service wrapper") and §XIII.3 ("control-plane handlers" as an example
    of out-of-session code paths) are kept. Neither states v1.0 scope.
- **`.specify/architecture.md`:**
  - "Stable from v1.0: … the gRPC control-plane proto" gets an annotation.
  - §8.1/§8.2 (design, not timing) get a one-line pointer.
  - Row 11 of the module table and the §11 row-5 disposition are left as design records. The
    appendix erratum pattern (Appendix Z) carries the pointer.
- **`.specify/2j-controlplane.md`:** status header gets a pointer. The design stands, and its default
  implementation is post-1.0.
- **`brain/components/service.md`:** "a live decision recorded outside this repo" is replaced by the
  decision.
- **`.specify/2h-transport.md`:** its io_uring line is an example ("e.g., epoll-driven or
  io_uring-driven"), not a v1.0 claim, so it is kept.
- **`README.md`:** 0 hits.
