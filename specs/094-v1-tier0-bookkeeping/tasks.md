# Tasks: v1.0 Tier-0 bookkeeping — catalogue reconciliation, DECISION 1, SPDX marking

**Feature**: `094-v1-tier0-bookkeeping` | **Branch**: `094-v1-tier0-bookkeeping` (main checkout,
`research/G19-fix-fpml-iso20022/library`)

**Input**: `specs/094-v1-tier0-bookkeeping/`:
- spec.md (FR-001…FR-023, including FR-003a/b/c, FR-006a, FR-013a/b and FR-015a/b; SC-001…SC-006; User Stories 1–3);
- plan.md (Gate A, Constitution Check, Phases, Risks);
- research.md (R-1…R-15);
- data-model.md (§A…§H, the exact target text of every changed row, clause and surface);
- quickstart.md (§1…§5, the re-verification recipe).

**Gate A**: converged 2026-10-22 (record `decisions/speckit/094-v1-tier0-bookkeeping-gatea.md` (parent repo, private)). The
v4.0 amendment stays **PENDING** until the owner's own Art. XX §2 sign-off (T061).

**Preconditions (pipeline steps 6–9, not tasks of this file)**: `/speckit-analyze` has run over spec,
plan and this file and its CRITICAL/HIGH findings are remediated; `/speckit-checklist-audit` has
dispositioned every checklist under `specs/094-v1-tier0-bookkeeping/checklists/`. The audit blocks
`/speckit-implement`, so it cannot be a task inside the file it gates.

**Tests**: only FR-022's two session-level cells (owner, Gate A round 2). They are regression guards,
green on the base by construction, and each is proven RED by the mutant FR-022 names (T029). Every
other flipped row is evidenced by tests that already exist (FR-015).

**Recorded deviation (093's T125a precedent):** T054–T068 (Gate B, publish, owner sign-off, the
RATIFIED flip, merge, post-merge close-out) are chronologically AFTER the completeness audit, because
`/gate-b` pre-flight 4d needs the audit record first. They are numbered BEFORE the two mandatory
close-out tasks so that T070 stays the FINAL task and pre-flight 4d reads it unchanged. Each of them
carries its own "Open at the audit — waiver" line, which the audit's check (i) accepts.

## Execution rules (apply to every task)

- **Executor ([const §XVI.6]).** Every task names one:
  - `[orchestrator]` writes documentation and governance text only: `spec/feature-catalogue.md`,
    `spec/coverage-index.md`, `spec/behaviors-and-limitations.md` (L-094-1), `.specify/constitution.md`,
    `.specify/architecture.md`, `.specify/api-contract.md`, the `.specify/2*-*.md` status notes,
    `brain/**`, `README.md`, `docs/src/intro.md`, `CHANGELOG.md`, `specs/094-v1-tier0-bookkeeping/*.md`,
    `CLAUDE-history.md` and `CLAUDE.md` (post-merge, T068), and the decision records. It files the
    GitHub issues and runs read-only instruments, builds and tests.
  - `[phase-implementer]` makes EVERY change to `src/**`, `include/**`, `tests/**`, `conanfile.py`,
    `cmake/**` and `tools/capi_freeze.sha256`, including comment-only lines and every scratch mutant.
  - The orchestrator never writes those files by any tool, python or sed through Bash included.
- **The tree.** Work happens in the main checkout on `094-v1-tier0-bookkeeping`. **Never `git checkout`
  or `git switch` there** (the post-merge reset T066 is the one exception, under its own conditions).
  Before each commit batch, run `git -C research/G19-fix-fpml-iso20022/library rev-parse --abbrev-ref HEAD`.
  Commits are serial; never commit concurrently in the same tree. Never `git stash` (the stash is
  shared by every worktree); commit on the branch instead.
- **Text is copied, never paraphrased.** Every replacement text comes from the data-model § the task
  cites, byte for byte. Data-model's rules apply to every row:
  - "Columns not listed under a row stay byte-identical to `origin/main` (`9b176090`)."
  - "A cell holding a backtick span keeps it as written here. No cell contains a bare `|`."
  - "Where the text says 'same-line', the edit replaces or appends text inside an existing line and
    adds no line."
  - Before replacing, `grep -F` the before-text: if it does not match byte for byte, stop and report.
  - If the before-text is not unique in its file (`grep -cF` prints more than 1), a whole-file
    `grep -F` does not identify the line. Anchor it to its line instead: `sed -n '<N>p' <file>` must
    print the line holding the before-text, where `<N>` is the data-model line guide (at `9b176090`;
    re-derived if `git diff 9b176090 HEAD -- <file>` is not empty). The edit then touches that line
    only. To re-derive: `grep -nF '<before-text>' <file>` lists the candidate lines, and the one in the
    section data-model names is the target. Same-line edits never move a line, so only a line-adding
    edit above the target (the SIR, the NFR-019 row and paragraph, Z-9, the L-094-1 section, the
    CHANGELOG entry) shifts a guide.
- **No placeholder number is ever committed.** Before every commit (tasks.md excluded, since it names
  what it replaces), `git grep -nE 'fixpp#<|<N-' -- ':!specs/094-v1-tier0-bookkeeping/tasks.md'` must
  print nothing once T011 has run. The RATIFIED templates of data-model §E.1/§E.2/§E.8 (`#<pr>`,
  `<sign-off date>`, `<date>`, `<tally>`) stay in the bundle as templates. They must never appear in
  `.specify/constitution.md` or `CHANGELOG.md` (T063 checks this).
- **No closing keyword.** No commit message, issue body or PR body puts "close", "fix" or "resolve"
  (in any form) next to #607, #612, #614 or the FR-018, FR-020, FR-021 or FR-023 issues, and that
  includes a negation such as "does not close" (FR-019). This PR closes no issue.
- **Commit plan, in execution order:**
  - the bundle's real issue numbers (T011, `specs/` only; orchestrator);
  - the US1 docs commits (T019, T026; orchestrator);
  - C-TEST (T029: the two FR-022 cells);
  - the US2 docs commit (T039; orchestrator);
  - C-SPDX (T040: the 53 files, the `conanfile.py` licence line, the RPM licence line,
    `tools/capi_freeze.sha256`);
  - C-607 (T041: the `conanfile.py` comment only, `Refs #607`), its own commit per FR-013b;
  - the `tasks.md` ticks (T069, T056, T062);
  - C-RATIFY (T063), the LAST commit before merge.
- **Builds and test runs need an owner ask ([const §XVII.7])**, unless a standing owner grant covers
  the named phase. Record each ruling in the verify record.
  - Run one preset at a time, under the 16 GiB cap.
  - Before a full rebuild of the main checkout's `build/linux-clang-debug`, run `df -h /mnt/e`.
  - Release, sanitizer and packaging trees live on `/mnt/wsl/fixppbuild`.
  - Long builds run under `setsid nohup` with a `.done` marker and ONE background waiter, never a
    foreground poll loop.
- **The MSVC sandbox.** It is not used: the library change is comment-only, and the FR-022 cells are
  platform-independent `session_pure_tests` cells. The verify record says plainly that MSVC was not run
  locally, and that all three tier matrices skip until both gate labels land. Before any full build,
  full `ctest` or packaging run, `cat /mnt/c/temp/fixpp/.sandbox-lock`. A peer holding it (the
  `cloud-1022-batch` session in `~/Work/Programming/fixpp-parallel`) may be in an MSVC window, and the
  packaging entries compile consumer projects. In that case ask the owner first.
- **Scratch tools** (orchestrator scratchpad, not committed):
  - the census script is
    `/tmp/claude-1000/-home-catalin-Work-Programming-Antreprenoriat/192e7371-7d46-465c-8dfc-5e6c3c527009/scratchpad/tools/catalogue_census.py`;
  - the SPDX script is
    `/tmp/claude-1000/-home-catalin-Work-Programming-Antreprenoriat/192e7371-7d46-465c-8dfc-5e6c3c527009/scratchpad/tools/spdx_mark.py`.
  - The census script's full text goes into the verify record (quickstart preamble).
  - If the SPDX script is gone, `phase-implementer` re-authors it to data-model §G's rules and re-runs
    R-6's dry run in a scratch clone before T040.
- **Workspace-root scripts** resolve through `W=$(git rev-parse --show-superproject-working-tree)`, run
  from the library root (quickstart preamble).
- **Gate results are scoped to the commits that existed when they ran.** Re-run the docs gates as the
  last action before every push.

## Format: `[ID] [P?] [Story] [Executor] Description`

- **[P]**: can run in parallel (different files, no dependency on an incomplete task). Anything touching
  `spec/feature-catalogue.md`, `spec/coverage-index.md` or `.specify/constitution.md` is serial with every
  other task on the same file.
- **[Story]**: US1, US2, US3 (spec.md User Stories 1–3).

---

## Phase 1: Setup (baselines, each with its known positive first)

**Purpose**: measure the base, so every later count has a "before" taken by an instrument that has been
shown able to report non-zero.

- [ ] T001 [orchestrator] **Base and tree check.**
  - In `research/G19-fix-fpml-iso20022/library`: `rev-parse --abbrev-ref HEAD` reads
    `094-v1-tier0-bookkeeping`, and `git status --porcelain` is clean apart from this bundle.
  - Run `git fetch origin`. `git merge-base HEAD origin/main` should be `9b176090`. If `origin/main`
    has moved, list `git log --oneline 9b176090..origin/main`. Name any commit that touches a cited
    file, row or test, and take it to the owner before Phase 3: spec Assumptions rests on the readings
    at `9b176090`.
  - `codegraph status --json` with `projectPath` set to this tree.
  - Record all of this under `## Base` in `.specify/decisions/094-v1-tier0-bookkeeping-verify.md`.
- [ ] T002 [P] [orchestrator] **Catalogue census and parser baseline (R-4, R-5; FR-016)** on
  `spec/feature-catalogue.md` at the base.
  - Known positive first: a mutant copy with NFR-001 set to `done` and W-015 set to the drop form
    reads open 92 and postv1 1.
  - Then the base must read: OFFICIAL open 94, done 152 parsed (154 actual), dropped 1, postv1 0.
  - Save `python3 tools/brain_inventory.py`'s output as the "before".
  - Run R-5's `_status_header` one-liner (`python3 -I -c …`, research R-5) on the base. Known positive:
    done 158, backlog 112, one `dropped(duplicate: …)`.
  - Record everything in the verify record, with the census script's text.
- [ ] T003 [P] [orchestrator] **Licence and sweep baselines** on `origin/main`, read-only. Raw outputs
  go to the verify record.
  - quickstart §3's two `git ls-files src include | … | xargs /usr/bin/grep …` censuses list 53 and 16
    files. Use the system `grep`: `xargs` cannot run a shell function.
  - R-8's `git grep -l "SPDX-License-Identifier: Apache-2.0" -- ':!src/' ':!include/' ':!specs/'` lists
    40 (37 under `tests/`, 3 under `bench/`).
  - R-12's recipe, its four `git grep` commands ((a) the dropped-scope terms, (a) the io_uring grep,
    (b) and (c)), with its known positives: (a) reports
    `README.md:15` and `.specify/constitution.md:374`; (b) reports `brain/components/dictionary.md:75`.
    Record the counts beside R-12's.
- [ ] T004 [P] [orchestrator] **Residuals on record (R-10; Edge Cases).**
  - Read the state and labels of #555, #565, #569, #580, #588, #612, #263 and #614 with
    `gh issue view <n> --repo CatalinSerafimescu/fixpp --json state,labels,createdAt`. Snapshot #614 into
    the verify record (FR-015).
  - In the LIVE `spec/behaviors-and-limitations.md` only (never grep across the live/closed pair),
    confirm each of L-003-2, L-022-1, L-497-1, L-002-4, B-043-1, B-005-7, B-495-3, B-544-1, L-019-4 and
    L-005-1 is defined.
  - If an issue is closed or an entry missing, the row citing it does not flip as worded. Record it and
    tell the owner before Phase 4.
- [ ] T005 [P] [orchestrator] **Cited-test existence (R-11).** Re-run R-11's check over `tests/` with
  both negative controls (`InboundPossResendTest.NoSuchCellXyz`, `RecoveryAdminSpanGapfillTest.NoSuchPrefixXyz*`),
  which must report MISSING. Result: 53 distinct entries, 51 exist, and the 2 MISSING are the FR-022
  cells. Record it in the verify record.

---

## Phase 2: Foundational (issues filed before any text cites them)

**Purpose**: FR-018, FR-020, FR-021 and FR-023 each require their issue to exist before the text that
names it. **⚠️ CRITICAL**: T025 (L-094-1), T032 (NFR-002), T041 (C-607) and any research.md / plan.md
record of these numbers wait on this phase.

- [ ] T006 [orchestrator] Run `gh label list --repo CatalinSerafimescu/fixpp --limit 200` and save the
  output. Choose each issue's labels from it at creation (the owner triages by label).
- [ ] T007 [orchestrator] **FR-018 issue** (the silently ignored `EngineConfig::control_plane_factory`).
  - First run `grep -rn control_plane_factory src/`, which must be empty. Its known positive is
    `grep -rn control_plane_factory include/`, which hits `include/fixpp/core/engine_config.hpp`.
  - The body names the field, the re-derive grep and its output, and the two options: refuse a non-null
    factory in v1.0, or document the field as reserved.
  - Labelled at creation. Record the number in the verify record.
- [ ] T008 [orchestrator] **FR-020 issue** (the 544 record's lapse).
  - The body says that `544-hot-path-zero-alloc-verify.md` (parent repo (private)) "reported 81.73 %
    branch and assessed added lines only", short of `[const §IX.1]`'s "every uncovered line/branch".
  - It asks for the untaken branches to be assessed, or each disposition recorded.
  - Labelled at creation. Record the number.
- [ ] T009 [orchestrator] **FR-021 issue** (the 40 `Apache-2.0` files outside `src/`/`include/`). The
  body carries R-8's re-derive grep and T003's output (37 under `tests/`, 3 under `bench/`), and says
  094 does not change them. Labelled at creation. Record the number.
- [ ] T010 [orchestrator] **FR-023 issue** (`[const §III.4]` not met on `main`).
  - First run plan.md's §III.4 re-derive recipe (Constitution Check, row §III.4) and paste the output:
    - `git log -S'tool_requires = []' -- conanfile.py` finds `629f2170`;
    - the `tool_requires|…§III\.4` grep over `.specify/constitution.md`, `CHANGELOG.md`, both B&L files
      and `brain/` finds only §III.4's own line (the known positive);
    - the `gh issue list --state all` search for `tool_requires`, with `swig>=4.2` as its known positive.
  - The body records that CMake and Ninja come from the host toolchain since `629f2170` (2026-05-10). It
    asks the owner to choose between restoring Conan-pinned `cmake`/`ninja` `tool_requires` and
    amending §III.4 to the host-provisioned model with its reproducibility requirement.
  - Labelled at creation. Record the number.
- [ ] T011 [orchestrator] **Fill the real numbers into the bundle** (FR-018, FR-020, FR-021, FR-023).
  - Before editing, run the placeholder grep from the Execution rules. It must list the bundle's
    current sites in `spec.md`, `plan.md` and `data-model.md`, exactly the sites below (one grep line
    each; L-094-1's two tokens share one line); that is its known positive.
  - Replace each `fixpp#<…>` / `<N-…>` token with the real number of the issue it stands for:
    - in `specs/094-v1-tier0-bookkeeping/spec.md`, the FR-023 issue in §Clarifications (round 3,
      the N3-2 answer);
    - in `specs/094-v1-tier0-bookkeeping/plan.md`, the FR-023 issue in the round-3 RC-2 bullet, the
      Constitution Check §III.4 row and the Result line;
    - in `specs/094-v1-tier0-bookkeeping/data-model.md`: the FR-020 issue in §C NFR-002's Verified
      cell; the FR-018 issue twice in L-094-1 (§F.6, one line); the FR-023 issue in §G's comment
      block.
  - The sentences that talk about a placeholder (spec FR-023, the notes under data-model §C NFR-002,
    §F.6 and §G) name it in words, such as "the FR-023 issue-number placeholder", and carry no token,
    so they read correctly after the fill and the grep does not list them.
  - Add the FR-021 issue's number to research R-8 ("Out of scope, on record"), and all four numbers to
    research R-10 ("New, filed at implement") in `specs/094-v1-tier0-bookkeeping/research.md`.
  - Afterwards the grep prints nothing. Commit, `specs/` only, with no closing keyword.

**Checkpoint**: four issues exist with real numbers in the bundle, and the user stories may start.

---

## Phase 3: User Story 1 — the tag gate stops counting rows nobody will build for v1.0 (Priority: P1) 🎯 MVP

**Goal**: the 7 post-v1.0 drops, the legend, the PENDING v4.0 amendment, the CHANGELOG entry, every
surface R-12 dispositions "edit" for the dropped scope, and L-094-1.

**Independent Test**: the census after T014 differs from T002's by exactly the 7 rows (open 87,
postv1 7). quickstart §4's constitution grep leaves no clause stating v1.0 scope for the service
wrapper, the gRPC control plane or the iceoryx2 data plane. `include/fixpp/service/control_plane_factory.hpp`
is byte-unchanged.

### Catalogue (serial: one file)

- [ ] T012 [US1] [orchestrator] `spec/feature-catalogue.md`, data-model §A, Status cell only:
  - W-015 and W-016 → `dropped(post-v1.0, planned, not abandoned: new wire format, not core tag-value FIX, no QuickFIX reference implementation)`;
  - SVC-001, SVC-002, SVC-003, SVC-004 and SVC-005 → `dropped(post-v1.0, planned, not abandoned: service wrapper deferred by owner decision 2026-07-15; src/service/ is a stub)`
    (as data-model §A writes it).
  - `git diff --word-diff origin/main -- spec/feature-catalogue.md` shows only those seven Status cells
    (FR-001: "Every other cell of these rows MUST stay unchanged").
- [ ] T013 [US1] [orchestrator] `spec/feature-catalogue.md`, data-model §B: append the definition to
  the existing `> **Status** ∈ {backlog, planning, implementing, done, dropped(reason)}` line, as the
  exact text of §B. FR-002: "The definition is appended to the existing Status line so no catalogue
  line moves". `wc -l` is the same before and after.
- [ ] T014 [US1] [orchestrator] **Census after the drops (R-4).**
  - The census reads open 87, done 152 parsed (154 actual), dropped 1, postv1 7.
  - The only Status differences from T002 are W-015, W-016 and SVC-001..005.
  - `tools/brain_inventory.py` and R-5's `_status_header` both parse every row; record their output.

### Coverage index

- [ ] T015 [US1] [orchestrator] `spec/coverage-index.md`, data-model §D "SVC-005 supplemental (:652)":
  directly after `**SVC-005 supplemental:**`, insert the exact italic note of §D, same-line.
  - Anchor the edit by its text; the line number is a guide.
  - Make no change to the W-015/W-016 entries (§D: ":112, :634, :701, :705 … already read post-1.0").
  - This is a same-line edit, so it precedes T036's NFR-019 paragraph, which §D places last.

### Constitution and CHANGELOG (the amendment, written PENDING)

- [ ] T016 [P] [US1] [orchestrator] `.specify/constitution.md`, the six clauses, each before-text
  matched with `grep -F` first:
  - §E.3, Article IV §4;
  - §E.3a, Article VI §6: the (a)–(d) list stays byte-identical, and the two sentences are appended;
  - §E.4, Article XIV §1: the lead-in and the control-plane bullet; the Transport, Cert source, Logger
    sinks and MessageStore bullets are unchanged;
  - §E.5, Article XIV §3;
  - §E.6, Article XVIII §1;
  - §E.7, Article XVIII §2: one bullet after `**v1.8 — FIX MMT**`.
  - §I.2, §IV.2, §XIII.3 and §XVIII.5 are read and left unchanged (R-12).
- [ ] T017 [US1] [orchestrator] `.specify/constitution.md`, after T016:
  - insert the PENDING Sync Impact Report of data-model §E.1 as a new block at line 1, above the
    v2.0 → v3.0 block, verbatim;
  - replace the `> **Status:**` line's opening `> **Status:** user-ratified **v3.0** (2026-09-23) — `
    with §E.2's PENDING form; the rest of the line stays unchanged.
  - The SIR already records the MINOR → MAJOR correction at round 1 and the §VI.6 loosening (FR-003a);
    do not reword it.
- [ ] T018 [P] [US1] [orchestrator] `CHANGELOG.md`, data-model §E.8 Pending form: the entry headed
  `## Constitution v4.0 — Unreleased`, inserted directly after the `---` that follows the "Format
  follows" line, newest first.
- [ ] T019 [US1] [orchestrator] **The docs shift-audit dry run (R-6; quickstart §5).** Run it now,
  before US2 and US3 are stacked on top: the new SIR is the known trigger.
  - Commit T012–T018.
  - In a `git clone --shared` of the branch in the scratchpad, squash the docs commits onto
    `origin/main` as one scratch commit. Run `python3 tools/check_line_citations.py --shift-audit HEAD~1..HEAD`.
  - Frozen `specs/<id>/` citers are reported, not charged.
  - If the audit charges a fixture string in `tools/check_line_citations.py` (for example
    `constitution.md:456`, which has no `citation-ok` marker), the only remedy is a `tools/` edit
    outside SC-005's allowlist. **Stop and ask the owner**; do not apply it.

### Other surfaces (R-12 "edit" hits; different files, parallel)

- [ ] T020 [P] [US1] [orchestrator] `.specify/architecture.md`, data-model §F.1.
  - The same-line edits at :32, :64, :161, :343, :357, :450, :611, :617, :642, :643 and :686.
  - The :815–:816 two-line replacement, which keeps the line count.
  - **Z-9** appended at the end of the file, after "Provenance of Z-6..Z-8", verbatim.
  - `git diff --stat` shows lines added only by the Z-9 block. Nothing above Appendix Z moves.
- [ ] T021 [P] [US1] [orchestrator] `.specify/api-contract.md`, data-model §F.2.
  - Append at :60, :61, :67, :113, :248, :272 and :274.
  - :222's last cell becomes `Post-1.0 (constitution v4.0)`.
  - :249's third cell becomes `` 1 (`make`) — post-1.0, on a type derived from the v1.0 anchor (constitution v4.0) ``.
  - :220 is unchanged (§F.2: "no change").
  - Locate every target line with `sed -n '<N>p' .specify/api-contract.md` and confirm the printed
    line is the row §F.2 names before editing it (Execution rules, non-unique before-texts).
    `Stable from v1.0` and `` 1 (`make`) `` each occur more than once in the file, so a whole-file
    `grep -F` cannot tell :222 or :249 from the other hits.
- [ ] T022 [P] [US1] [orchestrator] The design-doc status headers, data-model §F.3, same-line, verbatim:
  `.specify/2j-controlplane.md:3`, `.specify/2l-tap.md:5` and `:54`, `.specify/2m-pybind.md:5`,
  `.specify/2i-capi.md:3`.
- [ ] T023 [P] [US1] [orchestrator] `README.md:15` and `docs/src/intro.md:3`, data-model §F.4, before →
  after, verbatim.
  - `README.md:11` (badge), `README.md:25–26`, `NOTICE:7` and `LICENSE-COMMERCIAL.md:3` are left as they
    are. That default is recorded in spec §Clarifications, Gate A round 2; T053 reports it.
- [ ] T024 [P] [US1] [orchestrator] `brain/components/service.md`, data-model §F.5's first bullet.
  - Inside the `>` blockquote, `see \`architecture.md\` **Appendix Z-3**.` gains `, superseded by **Z-9**`.
  - The "Whether the service rows are deferred …" sentences are replaced by §F.5's text, with every
    wrapped line keeping the `> ` prefix.
  - The "Precisely: …" paragraph stays.
- [ ] T025 [US1] [orchestrator] `spec/behaviors-and-limitations.md`, data-model §F.6 (FR-017; after
  T007): append the new `## 094-v1-tier0-bookkeeping — …` section with **L-094-1** at the END of the
  live file, verbatim, with the FR-018 issue's real number in both places.
  - `git diff origin/main -- spec/behaviors-and-limitations.md` shows appended lines only.
  - `spec/behaviors-and-limitations-closed.md` is unchanged.
  - Then run `python3 "$W/.claude/scripts/check_bl_delta.py" gate --feature 094-v1-tier0-bookkeeping --base origin/main --root "$PWD"`
    and `python3 "$W/.claude/scripts/check_bl_citations.py" gate`. Record both.
- [ ] T026 [US1] [orchestrator] **US1 check, then commit.**
  - quickstart §4's `grep -niE "grpc|iceoryx|service wrapper|service[- ]mode|out-of-process" .specify/constitution.md`:
    every hit is the v4.0 SIR, the Status line (its v4.0 PENDING text names the post-1.0 scope), a
    clause that says post-1.0, an earlier SIR, or §I.2 / §IV.2 / §XIII.3.
  - `git diff --quiet origin/main -- include/fixpp/service/control_plane_factory.hpp` exits 0
    (FR-003b; US1 scenario 4).
  - Commit the US1 docs (no closing keyword), and re-run T019's shift audit over the docs commits if
    T020–T025 changed what it saw.

**Checkpoint**: DECISION 1 is in the catalogue, the constitution (PENDING) and every dropped-scope
surface. L-094-1 is live.

---

## Phase 4: User Story 2 — every delivered row reads `done` and says only what shipped (Priority: P1)

**Goal**: the 18 reconciliation flips, NFR-013's split into the new row NFR-019, the coverage-index
rewrites, the two brain pages, and FR-022's two TC-017 cells.

**Independent Test**: every cited test exists and passes on this branch, read from the binary's own
output (T030). Every residual a Verified cell names is a live B&L row or an open issue (T004). The census
reads open 69, done 170 parsed (172 actual), dropped 1, postv1 8 (T038).

### Tests for User Story 2 (FR-022; write first)

- [ ] T027 [P] [US2] [phase-implementer] **The acceptor cell**, in `tests/session/test_enum_validation_logon.cpp`.
  - Name: `EnumValidationLogonTest.InDomainNonZeroEncryptMethod_DisconnectsAndSendsNothing`, next to
    `InDomainEncryptMethod_EstablishesAsToday`.
  - It feeds `make_fix44_logon_frame(1, "TW", "ISLD", 30, /*encrypt_method=*/2)` and asserts the state
    is `Disconnected` and `transport.sent_frames()` is empty.
  - The file is already a source of `session_pure_tests` (`tests/session/CMakeLists.txt`), so no ctest
    entry is added. Its licence line (`// SPDX-License-Identifier: AGPL-3.0-or-later`) is unchanged.
- [ ] T028 [P] [US2] [phase-implementer] **The initiator cell**, in `tests/session/fsm_matrix_witness_test.cpp`.
  - Name: `FsmMatrixWitness.LS_InboundLogonEncryptMethodNonZero_DisconnectsAndSendsNothing`.
  - At LogonSent it feeds a Logon built with
    `build_frame("A", 1, kTarget, kSender, kBeginStr, "98=2\x01" "108=30\x01")` and asserts the state is
    `Disconnected` and that `captured_frames.size()` is unchanged across the feed.
  - Same `session_pure_tests` binary; no ctest entry; licence line unchanged.
  - **Do not edit** the stale "(with Logout in US4)" comment in `src/session/session.cpp`'s
    `on_inbound_logon_sent_`. It is a P3 note (plan.md §Gate A): editing it would break SC-005.
- [ ] T029 [US2] [phase-implementer] **RED proof for both cells, then GREEN, then commit C-TEST**
  (needs a build ask).
  - In a scratch copy (`git archive HEAD | tar -x -C "$(mktemp -d)"`), delete the
    `if (invalid_encrypt_method_seen)` block that returns `session_invalid_logon` in `interpret_logon`
    (`src/session/admin_messages.cpp`). Build `session_pure_tests`.
  - Each cell must fail on its state assertion (the session establishes), and the acceptor's send-count
    assertion must fail too.
  - For the initiator's send-count assertion, the known positive is the fixture itself:
    `captured_frames` holds the initiator's own outbound Logon before `clear_handshake_captures()`.
  - Revert, show `git diff` of the scratch copy against the PR head is clean, and `strings`-check that
    the binary is fresh.
  - On the branch: `ctest --preset linux-clang-debug -R '^session_pure_tests$' --no-tests=error --output-on-failure`,
    plus a gtest-filtered run showing both cells PASSED in the binary's own output.
  - Quote the RED output and the GREEN counts in the verify record. Commit C-TEST (only these two files).

### Evidence before any flip (Edge Cases: "A row is never flipped on a cell nobody ran")

- [ ] T030 [US2] [orchestrator] **Run every flipped row's evidence on the branch** (needs a build ask;
  sandbox-lock check first).
  - **Test rows.** Run every ctest entry of research R-15's map (re-derive the map first with its
    recipe; a new or renamed entry is added here, never dropped), each selected as
    `ctest --preset linux-clang-debug -R '^<NAME>$' --no-tests=error --output-on-failure`, reading every
    named test's PASSED from the binary's output (the tier1 `linux` `-LE packaging` step runs all of
    them on every leg; `linux-clang-debug` is the local leg):
    - wire and store: `wire_pure_tests`, `store_memory_zero_allocator_calls`, `store_memory_capacity`;
    - dictionary: `dictionary_pmr_allocation_test`, `dictionary_orchestra_tests`,
      `dictionary_pure_tests`, `delimiter_census`, `dictionary_dict_enum_census_test`,
      `dictionary_load_any_tests`;
    - codegen: `codegen_pure_tests`, `codegen_077_v42_vt11_completeness_c4_test`,
      `codegen_vlatest_tests` (compiles under `FIXPP_CODEGEN_FIX_LATEST`, default ON),
      `fixpp::dict::read-tier-byte-diff` (a `cmake -P` script, not gtest), `codegen_determinism_test`;
    - transport: `transport_smoke`, `transport_asio_plain_transport`, `transport_listener_acceptor`,
      `transport_cancellation_propagation`;
    - C ABI: `capi_dictionary_load`, `capi_inbound_frame_dispositions`;
    - session: `session_inbound_frame_dispositions`, `session_tc_reject` (no labels),
      `session_pure_tests`, `session_resend_reply_possdup`, `session_538_store_lookup_after_reset`
      (a gtest filter needs the `Stores/` prefix), `session_inbound_poss_resend`,
      `session_reset_on_lifecycle`, `session_interpret_logon_encrypt_method`;
    - interop: `interop_parity_us3_tests`;
    - `ctest -L mallocnesia --no-tests=error` on `linux-gcc-release` and `linux-clang-release` (the
      `if:` of the #448 steps), including the `alloc_guard_*_positive_control_mallocnesia` controls;
      run both, or record which was skipped and why.
  - **CI rows (FR-015).**
    - `gh run view 39594667883` and `gh run view 39594667975` must read success at `9b176090`.
    - Re-read on this branch the tier1/tier2 job names, step names and `if:` lines the NFR-001, NFR-003,
      NFR-005 and NFR-013 cells cite.
    - Read `gh api repos/CatalinSerafimescu/fixpp/branches/main/protection/required_status_checks --jq .contexts`.
  - **PR SHAs (FR-010: resolved from git, not copied from a note).** For every PR a §C cell cites, run
    `gh pr view <n> --repo CatalinSerafimescu/fixpp --json mergeCommit` and compare with R-9. For
    `a476bd30`, `96dd8aed` and `27f8701d`, use `git log --reverse -S tier1-required` / `-S tier2-required`.
    A mismatch stops the row.
  - **Recipes, each after its seeded positive:** NFR-013's backend grep (data-model §C NFR-013), and
    data-model §H's TC-014 and TC-017 recipes.
  - Draft the data-model §H claim → witness map from the assertions the cells ran (FR-015b).
  - **A row whose evidence fails stays `backlog`**: record it and tell the owner, and that row's flip
    task skips it.

### Catalogue flips (serial: one file; each after T030 and after T012–T014)

- [ ] T031 [US2] [orchestrator] `spec/feature-catalogue.md`, the clean rows **NFR-001, D-001, D-003,
  D-007** (FR-005). Write Status, /specify, PR, Tests and Verified exactly as data-model §C (each row's
  subsection) has them. The Title is unchanged.
- [ ] T032 [US2] [orchestrator] `spec/feature-catalogue.md`, **NFR-002** (FR-007; after T008). Write the
  Title, Spec ref, Status, /specify, PR, Tests and Verified from data-model §C "NFR-002 (reworded)",
  with the FR-020 issue's real number in Verified ("Lapse on record: …").
  - The Tests cell cites only the two records that assess lines and branches, labelled parent repo
    (private) (R-13).
  - This row closes under §VI.6(b) as amended by T016.
- [ ] T033 [US2] [orchestrator] `spec/feature-catalogue.md`, **NFR-003, NFR-005, NFR-011** (FR-006).
  Write the Title, Status, /specify, PR, Tests and Verified from data-model §C, each row's subsection.
  NFR-005's Title is §C's narrowed form, "…in the per-message parse arena and in MemoryStore's
  retrieve() snapshot; …"; NFR-011's drops "C++17".
- [ ] T034 [US2] [orchestrator] `spec/feature-catalogue.md`, **D-008** (FR-006). Write the FIX version(s)
  cell `4.2, 4.4, 5.0SP2, FIXT.1.1 (+ FIX Latest, opt-in build flag); runtime-XML-only versions: L-003-2`,
  and the Status, /specify, PR, Tests and Verified from data-model §C. The existing HTML comment under
  the row stays.
- [ ] T035 [US2] [orchestrator] `spec/feature-catalogue.md`, the TC rows **TC-003, TC-005, TC-006,
  TC-008, TC-012, TC-014, TC-015, TC-017** (FR-006, FR-006a, FR-009, FR-015a).
  - Each FIX version(s) cell becomes research R-3's "Column written" value for that row.
  - TC-005, TC-012, TC-014 and TC-017 take their reworded Titles from data-model §C.
  - Each row's Status, /specify, PR, Tests and Verified come from data-model §C.
  - Every `<TC-SCOPE>` is expanded to data-model §C's exact sentence:
    `Versions not witnessed: #614. FIX-TC §4.1 makes the scenarios applicable to the session layer of 4.2, 4.4 and FIXT, so the other versions the former column named (4.0, 4.1, 4.3, 5.0, 5.0SP1) are not required by FIX-TC.`
  - TC-003's old Verified text, including "Feature 093 does not own this row … stays backlog", is
    replaced (FR-011).
  - TC-017 is written only after T029 is GREEN.
- [ ] T036 [US2] [orchestrator] `spec/coverage-index.md`, data-model §D (FR-011; after T015). Apply the
  same-line edits first, each anchored by its text:
  - §4 row 3 (:405): the opener becomes `**093; TC-003 done (094 reconciliation):**`;
  - §4 rows 8–11 (:410–:413): §D's four replacements;
  - §4 row 19 (:421): §D's replacement sentence;
  - the 005 ledger rows at :435 and :436: §D's replacements.
  - **Then, last,** insert the **NFR-019 supplemental** paragraph after the NFR-016 supplemental (:648),
    verbatim. This is the `[const §VI.4]` entry, and it must exist before T037 adds the row.
- [ ] T037 [US2] [orchestrator] `spec/feature-catalogue.md`, **NFR-013 and NFR-019** (FR-008; after T036).
  - NFR-013: Title `ASIO async I/O transport (Linux and Windows)`, with the Status, /specify, PR, Tests
    and Verified from data-model §C "NFR-013".
  - NFR-019: data-model §C's full row, placed after NFR-014 in "NFRs (additional)".
- [ ] T038 [US2] [orchestrator] **Census after the flips (SC-001; FR-016).**
  - The census reads open 69, done 170 parsed (172 actual), dropped 1, postv1 8.
  - The only rows whose Status differs from T002's are the 25 in data-model §A/§C, plus NFR-019.
  - Run `tools/brain_inventory.py` again and record the diff against T002's output.
  - R-5's `_status_header` shows the three `dropped(post-v1.0, …` strings with counts 2, 5 and 1.
  - FR-012: `git diff origin/main -- spec/feature-catalogue.md` touches none of the rows NFR-010,
    NFR-004, NFR-008, TC-001, TC-002, TC-004, TC-007, TC-009, TC-010, TC-013.
- [ ] T039 [US2] [orchestrator] (after T038) `brain/components/dictionary.md` (frontmatter `description:`, the
  title, and the "Resolved 2026-10-22" line under the kept heading) and
  `brain/components/nfr-and-tooling.md` (the frontmatter `description:` clause and the :57 bullet), from
  data-model §F.5's second to fourth bullets, verbatim. The tables and dated notes stay. Then commit
  the US2 docs (no closing keyword).

**Checkpoint**: 18 rows read `done`, each worded to what shipped, NFR-019 is a post-v1.0 drop, and both
TC-017 cells pass.

---

## Phase 5: User Story 3 — every library source file says which licence it is under (Priority: P2)

**Goal**: AGPL-3.0-or-later on all 53 `src/`/`include/` C/C++ files, package metadata that agrees,
the C-ABI manifest regenerated, and FR-013b's `conanfile.py` comment.

**Independent Test**: quickstart §3. Both censuses print nothing. The diff filter shows only the two
AGPL comment forms added and `// SPDX-License-Identifier: Apache-2.0` removed. `tools/check_capi_freeze.sh`
prints `PASS`.

- [ ] T040 [US3] [phase-implementer] **Commit C-SPDX**, one commit (FR-013, FR-013a, FR-014).
  - Run `spdx_mark.py` (path in the Execution rules) on the branch:
    - `include/fix/**/*.h` get `/* SPDX-License-Identifier: AGPL-3.0-or-later */` as line 1;
    - every other file gets `// SPDX-License-Identifier: AGPL-3.0-or-later`;
    - the 16 `Apache-2.0` files have their identifier replaced and their `Copyright` line unchanged.
    - "No other byte of these files changes."
  - `conanfile.py:8`: `license = "AGPL-3.0-only"` → `license = "AGPL-3.0-or-later"`.
  - `cmake/FixppPackaging.cmake:408`: `set(CPACK_RPM_PACKAGE_LICENSE "AGPL-3.0")` →
    `set(CPACK_RPM_PACKAGE_LICENSE "AGPL-3.0-or-later")`.
  - `tools/capi_freeze.sha256` is regenerated with `sha256sum` over the same 12-file set, in the
    manifest's existing order and two-space format. `tools/check_capi_freeze.sh` prints `PASS`.
  - `include/fixpp/service/control_plane_factory.hpp` is not in the set and stays byte-unchanged
    (FR-003b).
  - Before committing, run quickstart §3's two censuses (with `/usr/bin/grep`; on the branch both print
    nothing) and its `git diff origin/main -- src include | grep -E '^[+-][^+-]' | sort | uniq -c` filter.
- [ ] T041 [US3] [phase-implementer] **Commit C-607**, a separate commit (FR-013b; after T010 and T040).
  - In `conanfile.py`, replace the two comment lines above `tool_requires = []`:
    ```
        # cmake, ninja, swig provided by apt in CI and locally; Conan-pinned tools
        # collide with system versions and add resolution time. Restore in Phase 5.
    ```
  - with data-model §G's four lines, four-space indent, the last line ending
    `Restore: fixpp#NNN.`, where NNN is the FR-023 issue's real number (filled into §G by T011):
    ```
        # cmake and ninja come from the system (apt in CI); swig is installed by each
        # workflow that builds the bindings, at or above the minimum
        # bindings/python/CMakeLists.txt requires. Conan-pinned tools collide with
        # system versions and add resolution time. Restore: fixpp#NNN.
    ```
  - `tool_requires = []` moves from line 99 to line 101. Nothing else in the file changes.
  - The commit message carries `Refs #607` and no closing keyword.
  - The peer B58 batch also edits this comment, so whichever merges second rebases the conflict
    (plan.md Risks).
- [ ] T042 [US3] [orchestrator] **US3 check.**
  - Run every bullet of quickstart §3. That includes `grep -n '^    license' conanfile.py`,
    `grep -n 'CPACK_RPM_PACKAGE_LICENSE' cmake/FixppPackaging.cmake`, and
    `git log --format=%B origin/main..HEAD -- conanfile.py`, which must show `Refs #607` on C-607 and no
    closing keyword anywhere.
  - SC-005's `git diff --stat origin/main -- ':!spec/' ':!.specify/' ':!brain/' ':!specs/' ':!CHANGELOG.md'`
    lists only the SC-005 files.
  - Then `codegraph sync` in this tree, with `codegraph status --json` read back.

**Checkpoint**: all three stories are complete on the branch.

---

## Phase 6: Polish and cross-cutting

### `/simplify` (pipeline step 11, before verify)

- [ ] T043 [orchestrator] Run `/simplify` scoped to the two FR-022 cells (`tests/session/test_enum_validation_logon.cpp`,
  `tests/session/fsm_matrix_witness_test.cpp`; `[const §XVI.7]`). Opus agents review; the orchestrator
  triages; `phase-implementer` applies in-scope fixes. Any change to a cell re-runs T029's RED proof
  for that cell. Record the triage in the verify record.

### Instruments (each with its known positive)

- [ ] T044 [P] [orchestrator] **R-12 sweep re-run (SC-003; FR-004).** Run R-12's four `git grep` commands
  ((a) the dropped-scope terms, (a) the io_uring grep, (b), (c)) on the branch. Every hit falls in a "keep", "header", "other concept" or "pattern FP"
  class of R-12, or is an applied "edit". Record the count per class in the verify record, beside
  T003's base counts.
- [ ] T045 [P] [orchestrator] **Line citations and comment claims (quickstart §5).**
  - `python3 tools/check_line_citations.py --range origin/main..HEAD` and `--shift-audit origin/main..HEAD`
    both pass; frozen `specs/<id>/` citers are reported, not charged.
  - `python3 "$W/.claude/scripts/check-comment-claims.py" --root "$PWD" --base origin/main` passes. Also
    read the claims the lexical check cannot see: the FR-022 cells' comments and the C-607 comment.
- [ ] T046 [P] [orchestrator] **B&L and brain freshness.**
  - `check_bl_delta.py gate` sees the L-094-1 delta; `check_bl_citations.py gate` reports 0 dangling.
  - The in-repo freshness half: run what `.github/workflows/brain-freshness.yml` runs, derived from the
    workflow, not from a copied list.
  - `"$W/research/G19-fix-fpml-iso20022/tools/brain-external-sweep.sh"` passes; record its execution
    count.
- [ ] T047 [orchestrator] **Every tier1 source-audit job, run locally.** Read
  `.github/workflows/tier1.yml` with a YAML parser. For `ci-script-pins`, `check-layers`,
  `co-spawn-closure-audit` and any other ungated audit job, run each `run:` block as Actions would
  (`bash -e`). Record a job → rc table in the verify record. Every step must run and pass.
- [ ] T048 [P] [orchestrator] **Closing-keyword check over every commit message.**
  - Positive controls first: seed `Closes #607` and
    `Resolves https://github.com/CatalinSerafimescu/fixpp/issues/614` into a scratch copy of one
    message and show the grep hits both (the second is the full-URL form GitHub also links).
  - Then `git log origin/main..HEAD --format=%B | grep -inE '\b(close[sd]?|fix(e[sd])?|resolve[sd]?)\b([: ]+([a-z0-9_.-]+/[a-z0-9_.-]+)?#[0-9]+|\s*:?\s*https://github.com/[^ ]+/issues/[0-9]+)'`
    must print nothing.

### `/speckit-verify` (pipeline step 12, `[const §XVII.8]`)

- [ ] T049 [orchestrator] **Run `/speckit-verify 094-v1-tier0-bookkeeping`.** It writes
  `.specify/decisions/094-v1-tier0-bookkeeping-verify.md`, with a GREEN or YELLOW verdict. Builds need
  an owner ask, and the sandbox lock is checked first (Execution rules).
  - Its evidence list is quickstart §1–§5, at the post-simplify head:
    - **§1**, the census: T038's figures re-run, with the known positive.
    - **One full Linux build** (`-k 0`, `df -h /mnt/e` first), proving the comment edits compile.
    - **The full test suites of the modules the 37 SPDX inserts touch.** Derive them; do not hand-list
      them. Run `git diff --name-only --diff-filter=M origin/main..HEAD -- src include`, restricted to
      the C-SPDX inserts; map each path to its module directory, each module to its ctest label(s) in
      the module's `CMakeLists.txt`, and run those labels unfiltered. Record the mapping.
    - **§2**, the witness binaries per row: T030's runs repeated at the final head with
      `--no-tests=error`, on the preset CI runs each on, including the two FR-022 cells and their RED
      proofs (T029, re-run if T043 changed a cell).
    - **The §H claim → witness map, filled in full** (FR-015b): one row per clause, the `file:line` of
      each `EXPECT`/`ASSERT` or the CI step's `if:` and command, each path resolving here or labelled
      parent repo (private). A clause with no assertion does not flip as worded: it is narrowed, and
      the narrowing goes to T053.
    - **FR-015's three evidence forms**, plus the #614 snapshot and the snapshots of the FR-018,
      FR-020, FR-021 and FR-023 issues (state, labels, createdAt).
    - **§3, the licence census** (T042), and the C-ABI checks: `tools/check_capi_freeze.sh` `PASS`;
      `tools/check_capi_occupancy.sh`; a symbol diff of `libfixpp_capi` built at `9b176090` and at the
      head, which must be empty (R-7).
    - **The packaging lane's RPM build** (FR-013a): derive the preset and the `cpack` command from
      `tier1.yml`. `rpm -qip` shows `License: AGPL-3.0-or-later`.
    - **§4 and §5**: T044–T048's results, re-read at the verified head.
    - **One FULL `ctest` cycle, run LAST** (`[const §XVII.7]`, the local pre-PR build gate; plan.md
      Constitution Check, row §XVII.7): `ctest --preset linux-clang-debug --output-on-failure --no-tests=error`,
      on a committed tree, after every selected and witness run above.
      - Why last: a full `ctest` regenerates `_codegen` in the same build tree, and `ninja -n` is blind
        to that. So the selected runs go first and the full `ctest` last; if any build or run follows
        it, rebuild first.
      - Before it, run `df -h /mnt/e`: the main checkout's `build/` lands on E:.
      - Before it, run `cat /mnt/c/temp/fixpp/.sandbox-lock`. This feature does not use the MSVC
        sandbox, but the packaging entries in a full `ctest` compile consumer projects, so during a
        peer's MSVC window a full `ctest` is a build. If a peer holds the lock, ask the owner before
        running.
      - It needs the owner's build ask, like every build here. T052's `local build: green` line cites
        this run's SHA.
    - **Coverage `[const §IX.1]`**: n/a, because no library line is added or made reachable. The record
      says so per touched module.
    - **MSVC**: stated plainly as not run locally (Execution rules).
  - A row whose evidence did not hold is taken out of the flip set (FR-015), its row reverted to
    `backlog` by a docs task, and the owner told.
- [ ] T050 [orchestrator] **R-11 re-run**: 53 of 53 entries exist; both negative controls report
  MISSING. Recorded in the verify record.
- [ ] T051 [orchestrator] `codegraph sync` in this tree after the last code-changing commit; read back
  `codegraph status --json` (`projectPath` is this tree, `fileCount` nonzero).

### Owner report and PR text

- [ ] T052 [orchestrator] **Draft the PR description** in the parent's `research/reviews/` scratch area
  (T057 opens the PR with it; T064 applies its final form).
  - Title, exactly (FR-003c):
    `Constitution: amend §IV.4, §VI.6, §XIV.1, §XIV.3, §XVIII.1, §XVIII.2 — service wrapper post-1.0, process-row closure (v4.0) (+ 094 Tier-0 bookkeeping)`.
  - The body states the change, the rationale and the affected catalogue rows and specs (FR-003c).
  - FR-019's four items:
    - the Conan cache miss on every lane, once;
    - the `PY_RE` python-wheel fleet and the ccache invalidation for every TU including the touched
      headers;
    - that the C-ABI change is comment-only (FR-014);
    - the relicensing basis (R-8: one author; versions already published under Apache-2.0 keep that
      grant).
  - The disclosed RATIFIED-flip re-run cost (plan.md Risks).
  - The gate records are cited in the two outcome sections only; the body has no separate `## Gates`
    section:
    - a `## Gate A outcome` section: Gate A converged (loop instance 2, round 1), linking
      `decisions/speckit/094-v1-tier0-bookkeeping-gatea.md` (parent repo, private);
    - a `## Gate B outcome` section, held in the draft as a placeholder (`Gate B: pending.`) under
      which the verify record `.specify/decisions/094-v1-tier0-bookkeeping-verify.md` (parent repo
      (private)) is linked, because `/speckit-verify` is Gate B's precondition. T064 replaces the
      placeholder line with Gate B's outcome and the link to the Gate B record (`[const §XVII.4]`) and
      keeps the verify-record link.
  - `local build: green on linux-clang-debug @ <sha>`, with the SHA of T049's full `ctest` cycle.
  - Run T048's grep over the body, with its positive control. No closing keyword, so
    `closingIssuesReferences` must come back empty.
- [ ] T053 [orchestrator] **Owner report** (FR-006a, FR-015b; spec §Clarifications defaults). It lists:
  - every narrowing verify made beyond the bundle's;
  - the narrowings the bundle already reported, with where each was reported:
    - the TC version columns (FR-006a, research R-3). The orchestrator told the owner in-session on
      2026-10-22, before Gate A started ("the TC version columns now differ from the wording you
      approved…"), and #614's body carries the eight-row table. Narrower than the owner's
      "4.2, 4.4 witnessed": TC-003, TC-006, TC-008, TC-012 and TC-015 (4.2 only), and TC-014 (4.4
      only). TC-005 and TC-017 are as the owner worded them;
    - NFR-005's PMR clause, narrowed at Gate A round 3 to the parse arena and MemoryStore's retrieve()
      snapshot (FR-006);
  - any row withheld by T030/T049;
  - the defaults applied and reported, not edited: coverage-index :552, the "AGPL-3.0" licence prose,
    the stale `session.cpp` comment (P3);
  - the four new issue numbers.
  Record that it was sent.

### Gates, publish, ratification, merge and post-merge close-out (pipeline steps 14–20)

> Each task below runs AFTER T070's audit, chronologically. It is numbered here so that T070 stays the
> FINAL task (recorded deviation, header). Each is open at the audit, under the waiver written in it.

- [ ] T054 [orchestrator] **Run `/gate-b 094-v1-tier0-bookkeeping`** (pipeline step 14).
  - **First, integrate `origin/main`.** `git fetch origin`. If `origin/main` moved past T001's base,
    merge it (a merge, not a rebase: the verify record cites this branch's SHAs). `phase-implementer`
    resolves any conflict in code or build files, such as the peer B58 batch's `conanfile.py` comment
    (plan.md Risks). Then re-run quickstart §3's licence census, `tools/check_capi_freeze.sh`, the
    catalogue census and T044–T048, and re-derive SC-005's diff-stat against the new merge-base.
    Record the new base under `## Base`.
  - Hard preconditions: T049's record is non-RED, T070's `## Completeness` exists, the B&L delta passes
    `check_bl_delta.py gate` (L-094-1), and Gate A's evidence is present.
  - The loop follows `/gate-b`: the Claude fixer is `phase-implementer` for code and test findings, and
    the orchestrator for docs findings.
  - After any fix commit, re-run the affected T044–T048 gates and, for a cell change, T029.
  - If any Gate B fix (or T054's `origin/main` merge) touches a path outside `spec/`, `.specify/`,
    `brain/`, `specs/`, `CHANGELOG.md`, `README.md` and `docs/src/`, re-run T049's full `ctest` cycle
    as the last run, under the same pre-checks, and update the SHA T052's `local build: green` line
    cites. Otherwise record in the verify record that the delta since T049's run is docs-only
    (`git diff --name-only <T049 SHA>..HEAD` as the evidence).
  - Gate B also reviews the PENDING amendment text, which the owner signs off in T061.
  - In the Gate B record (`.specify/decisions/094-v1-tier0-bookkeeping-gateb.md`), record the feature's
    B&L delta as "B&L delta: one new limitation (L-094-1), no behaviour change" (FR-017).
  - *Open at the audit — waiver: Gate B needs the audit record first ([const §XVII.8], pre-flight 4d).*
- [ ] T055 [orchestrator] **The SecondBrain pass on the branch** (pipeline step 15a; close-out row 3).
  The brain edits are T024 and T039; re-check that they still hold after Gate B's fixes. Run both
  freshness halves locally (T046) and report the external sweep's execution count.
  - *Open at the audit — waiver: it runs after Gate B converges.*
- [ ] T056 [orchestrator] **Re-run the docs gates as the last action before the first push**: T045,
  T046, T047 and T048 at the head about to be pushed. Commit the `tasks.md` ticks of T054, T055 and
  T070 first (T001–T053 and T069 were committed by T069), so the head the gates ran on is the head
  that is pushed.
  - Read `git diff --name-only <T049 full-ctest SHA>..HEAD`. If it lists a non-docs path (the T054
    rule), T049's full `ctest` re-runs last and the cited SHA is updated before the push; otherwise
    the verify record states that the delta is docs-only.
  - *Open at the audit — waiver: after Gate B.*
- [ ] T057 [orchestrator] **Push and open the PR without gate labels** (pipeline step 16).
  - `git push -u origin 094-v1-tier0-bookkeeping`, then `gh pr create` with T052's title and body. The
    tiers skip until both labels land.
  - Read `closingIssuesReferences`; it must be empty. Record the PR number (needed by T063).
  - *Open at the audit — waiver: after Gate B.*
- [ ] T058 [orchestrator] Read the PR's ungated checks (the tier1 source audits); a red one goes back
  through the Gate B fixer.
  - *Open at the audit — waiver: after T057.*
- [ ] T059 [orchestrator] **Prepare the sign-off packet.** It holds:
  - the six amended clauses, the PENDING SIR and the Status line as they stand on the PR head
    (`.specify/constitution.md`);
  - the `CHANGELOG.md` entry;
  - the Gate A tallies from `decisions/speckit/094-v1-tier0-bookkeeping-gatea.md` (parent repo, private) (instance 1 rounds
    1–3; instance 2 round 1, converged);
  - Gate B's rounds;
  - the RATIFIED-form text T063 will write.
  - *Open at the audit — waiver: after Gate B.*
- [ ] T060 [orchestrator] **Take the PR-title and body corrections Gate B asked for into T052's draft**,
  if it asked for any. The body is applied only in T064.
  - *Open at the audit — waiver: after Gate B.*
- [ ] T061 [orchestrator] **ASK THE OWNER for the Art. XX §2 sign-off** on the amendment, with T059's
  packet. Do nothing in T062–T065 until the owner answers explicitly. A request to change the text sends
  the change back through T016–T018 and a Gate B re-check. Record the answer and its date in the verify
  record and the Gate B record.
  - **If the owner changes any amended CLAUSE text** (data-model §E.3–§E.7: §IV.4, §VI.6, §XIV.1,
    §XIV.3, §XVIII.1, §XVIII.2), a scoped Codex Gate A round runs on the changed lines only before
    T063: `"$W/.claude/scripts/codex-tool.sh" --mode review` with a brief naming those lines, plus an
    Opus judge. `[const §XX.2]` requires "Codex Gate A review on every amendment", and a changed clause
    is amendment text Codex has not reviewed. It runs under the owner's own `/gate-a` invocation,
    because no agent starts Codex on its own initiative (`[const §XVII.4]`). Record it in the Gate A
    record and in plan.md `## Gate A`.
  - **After a scoped Gate A round converges, return to T061** and ask the owner again for the
    Art. XX §2 sign-off, on the text that round reviewed. An earlier sign-off covered other text. The
    date of that fresh sign-off is the one T063 writes.
  - **A change to the Status line, the SIR's status or tally wording, or the CHANGELOG status wording**
    is metadata, not clause text. It needs only Gate B's re-check, with no Gate A round (plan.md
    `## Gate A`, the post-convergence metadata rule).
  - *Open at the audit — waiver: Art. XX §2 orders it after Codex Gate A and after the text is final.*
- [ ] T062 [orchestrator] (after owner sign-off) **Commit the `tasks.md` ticks of T056–T061.** The ticks of
  T001–T055, T069 and T070 are already committed (T069, T056); T070's re-assertion after T063 is
  recorded in the verify record's Closure subsection, not as a new tick. The ticks for T063–T068 are
  recorded in the Gate B record's close-out table, not in this file, because T063 must stay the last
  commit.
  - *Open at the audit — waiver: after T061.*
- [ ] T063 [orchestrator] (after owner sign-off) **Commit C-RATIFY, the RATIFIED flip, as the LAST
  commit before merge** (FR-003a).
  - `.specify/constitution.md`: the SIR goes to data-model §E.1's RATIFIED form. The header line reads
    `Sync Impact Report — v3.0 → v4.0 (<sign-off date>) — RATIFIED`. The first paragraph becomes
    §E.1's "Article XX §2 sequence observed in order: …" sentence. Anchor it by position: it is the
    lines directly after the SIR header line, up to the line before `  Bump:`, and it opens
    `Gate A converged 2026-10-22`. This is a multi-line replacement (the PENDING paragraph is wrapped
    over several lines), so a single-line `grep -F` of it cannot match; print the range with
    `sed -n '<header+1>,<Bump-1>p'` and confirm it before replacing it. In the last paragraph, the
    `Gate B: pending, …` and `Owner sign-off: pending, …` clauses take Gate B's round tally, the
    sign-off date and `Amendment PR #<pr>`; the Gate A tally stays. Nothing else in the block changes.
  - The Status line goes to §E.2's RATIFIED form. Its find-string is the PENDING `not ratified: Gate A
    converged 2026-10-22 …` clause, but after T017 the phrase `Gate A converged 2026-10-22` occurs
    twice in the file (the SIR's first paragraph and the Status line), so apply the non-unique rule
    (Execution rules): `grep -n '^> \*\*Status:\*\*'` gives the line, and `sed -n '<N>p'` must print
    the find-string.
  - `CHANGELOG.md` goes to §E.8's flip form: the heading `## Constitution v4.0 — <sign-off date>`, and
    the paragraph that opens "Gate A converged 2026-10-22" replaced by the "Ratified …" sentence.
  - Each find-string is matched with `grep -F` first (Execution rules); a miss stops the flip.
  - **Before writing it**, `git fetch origin`. If `origin/main` moved since T054's integration, repeat
    that integration first, so that C-RATIFY stays the last commit.
  - Fill the templates: `<pr>` from T057 (the PR number); `<tally>` from T059 (the Gate A tally of
    loop instance 2, round 1, and Gate B's rounds); `<sign-off date>` and `<date>` from T061 (the date
    of the last sign-off, the fresh one if a scoped Gate A round ran). The Gate A round is written in
    the templates as the literal "loop instance 2, round 1" and needs no fill.
  - Checks on the flip head:
    - `grep -nE '<n>|<pr>|<sign-off date>|<date>|<tally>' .specify/constitution.md CHANGELOG.md` prints
      nothing new. Its baseline is the same grep over `git show origin/main:<file>` for both files,
      and only hits absent from the baseline count. Its positive control is the same grep run on
      data-model.md first;
    - re-run `check_line_citations.py --shift-audit origin/main..HEAD` and `--range`,
      `check-comment-claims.py`, and the brain freshness halves: the RATIFIED paragraph changes the
      SIR's line count.
  - In the Gate B record, state that C-RATIFY was written after the Gate B review, from the
    pre-specified text of data-model §E.1, §E.2 and §E.8, and that its evidence is the docs gates
    re-run on the flip head. Gate B's result does not claim a commit it never saw.
  - *Open at the audit — waiver: only after the owner's sign-off.*
- [ ] T064 [orchestrator] (after owner sign-off) **Body → labels → push last** (plan.md Risks; memory:
  three events on one SHA give three waves).
  - **Choose the labels from the verify verdict**, re-read in
    `.specify/decisions/094-v1-tier0-bookkeeping-verify.md` at this head, never by default
    (`[const §XVII.8]`, label evidence rule):
    - **GREEN** → `gate-a-done` and `gate-b-done`, each paired with its convergence record
      (`decisions/speckit/094-v1-tier0-bookkeeping-gatea.md` (parent repo, private), `094-v1-tier0-bookkeeping-gateb.md`), unless that record
      carries gate-level waivers.
    - **GREEN, but a gate's convergence record carries gate-level (Codex-side) waivers** → that gate's
      `-waived` label (`[const §XVII.8]`: "`YELLOW` (or `GREEN` with explicit Codex-side waivers)"),
      treated as YELLOW below: copy the waivers into the verify record as well as into the PR body
      under that gate's literal heading (the clause asks for rationales "recorded both in the verify
      record and the PR body"), and run the awk locally. This bundle reads `[const §XVII.8]` as
      forbidding `-done` over recorded waivers.
    - **YELLOW** → `gate-a-waived` and `gate-b-waived`. `[const §XVII.8]` allows `-done` only on GREEN,
      and a `-waived` label needs its waiver rationales "recorded both in the verify record and the PR
      body". Copy every waiver rationale from the verify record into the body, under a literal
      `## Gate A …` heading and a literal `## Gate B …` heading.
    - **RED** → no label. Go back to the Gate B fixer.
  - **First edit the PR body to its final form**: the sign-off date, the verified head's SHA, the T052
    corrections, and for any `-waived` label the waiver rationales. The `## Gate B outcome` section replaces T052's
    placeholder: it states Gate B's outcome (rounds, verdict, waivers) and links
    `.specify/decisions/094-v1-tier0-bookkeeping-gateb.md` (parent repo (private)), as `## Gate A outcome`
    links the Gate A record (`[const §XVII.4]`: "The PR description links to the Gate A outcome and
    the Gate B outcome").
  - **For any `-waived` label (YELLOW, or GREEN with gate-level waivers), run the label-check awk
    locally over the final body before labelling.** The check is
    the one in `.github/workflows/gate-b.yml`, mirrored in `tier1.yml`, `tier2.yml` and
    `tier3-libcxx.yml`: a heading matching `/^##+[[:space:]]*[Gg]ate[[:space:]]*[Bb]/` followed by a
    non-blank line. `.github/workflows/gate-a.yml` runs the same check with `[Aa]`. Copy each awk from
    the workflow at this head, not from here, and run it on the body. Each must exit 0. The known
    positive: the same body with every Gate A and Gate B heading removed (`## Gate A outcome`,
    `## Gate B outcome` and any other heading either awk matches) must make each awk exit 1.
  - **Whatever the labels, assert the placeholder is gone:** `grep -cF 'Gate B: pending.'` over the
    final body prints 0. Its known positive is T052's draft, where it prints 1.
  - Then apply the chosen pair in ONE `"$W/.claude/scripts/gh-pr-meta.sh"` REST call and read them
    back.
  - Then push C-RATIFY.
  - Never cancel a "duplicate" gate-label wave.
  - *Open at the audit — waiver: after T063.*
- [ ] T065 [orchestrator] **Merge (owner-driven, pipeline step 18).**
  - Before merging, re-read `closingIssuesReferences`, which must be empty, and confirm all three tiers
    and both gate checks are green on the C-RATIFY head.
  - If `--admin` is needed, give the owner a one-line `!` command with `--body-file`; it is
    classifier-denied for the agent.
  - *Open at the audit — waiver: owner-driven, post-Gate B.*
- [ ] T066 [orchestrator] **POST-MERGE: the close-out table** (`.specify/close-out.md` §3, pasted into the
  Gate B record `.specify/decisions/094-v1-tier0-bookkeeping-gateb.md`). Every row reads DONE or
  N/A:<reason>, including:
  - row 4, the parent submodule-pointer bump (`git ls-tree HEAD research/G19-fix-fpml-iso20022/library`
    after the commit);
  - row 5, the labels read back on the merged PR;
  - row 9, issues: none closed; #607, #612, #614 and the four new issues confirmed OPEN;
  - row 14, memory;
  - row 16, the workspace reset of the main checkout: `git switch main && git merge --ff-only origin/main`,
    only if this tree's branch is this PR's and `git status --porcelain` is clean;
  - row 17, purge `094-v1-tier0-bookkeeping` locally and on the remote, after row 16;
  - row 18, N/A: the MSVC lock was never taken.
  - Then `codegraph sync` in the main checkout.
  - *Open at the audit — waiver: post-merge.*
- [ ] T067 [orchestrator] **POST-MERGE: parent bookkeeping.**
  - Parent `REMAINING-WORK.md`: the ★ V1.0 FUNCTIONAL TRACK row 1 is marked done, and the Tier-0
    tallies are re-derived from T038's census.
  - Parent `research/G19-fix-fpml-iso20022/phases/phase-4/issue-batches.md`: place #614 and the FR-018,
    FR-020, FR-021 and FR-023 issues, following the placement check in its header.
  - A project memory note, with its `MEMORY.md` index line.
  - Parent commits only; confirm with `ls-tree` after each one.
  - *Open at the audit — waiver: post-merge.*
- [ ] T068 [orchestrator] **POST-MERGE, on `main`: the `CLAUDE-history.md` entry** (newest first)
  and `CLAUDE.md`'s "Last merged FEATURE" pointer. Both are documentation, so they are the
  orchestrator's (Execution rules).
  - The entry records the feature, the PR, the v4.0 amendment, the 18 flips and 8 post-v1.0 drops,
    L-094-1, and the four new issues.
  - It lands after merge because SC-005 has no allowance for either file on the branch.
  - The push touches no build path.
  - *Open at the audit — waiver: post-merge.*

### Mandatory close-out tasks (Gate-B preconditions, Article XVII §8)

- [ ] T069 [orchestrator] **Catalogue close-out, verification only.** Data-model's rule overrides the
  template's "with the PR ref": "The 094 PR itself is not cited, because its number does not exist until
  the PR is opened, and close-out does not re-edit rows".
  - Assert that every one of the 26 feature-owned rows (W-015, W-016, SVC-001..005, NFR-001, NFR-002,
    NFR-003, NFR-005, NFR-011, NFR-013, NFR-019, D-001, D-003, D-007, D-008, and the eight TC rows) reads
    on disk exactly as data-model §A/§C has it. Extract each row and diff it against the data-model
    text, with the real issue numbers substituted.
  - For check (iii), `grep -nw` each of the 26 IDs in `spec/coverage-index.md` and record the result.
    A row with no entry is recorded as a gap and reported to the owner, not filled ad hoc: data-model
    has no text for it.
  - Then commit the `tasks.md` ticks of T001–T053 and T069 (`specs/` only, no closing keyword), before
    T070 runs, so T070's check (i) reads committed state at the branch head.
- [ ] T070 [orchestrator] **Feature-completeness audit (MUST be the FINAL task).** Assert against the
  branch head:
  - **(i)** Every `tasks.md` row through T053 is `[X]` in the committed branch head (T069's tick
    commit) or carries an explicit waiver; check `git diff --quiet HEAD -- specs/094-v1-tier0-bookkeeping/tasks.md`
    first, so an uncommitted tick cannot pass. T054–T068 carry their own waivers. T069's tick is in
    that commit. T070's own completion is evidenced in the verify record (093's T126 precedent), and
    its tick lands with T056's commit.
  - **(ii)** Every FR-001…FR-023 (with FR-003a/b/c, FR-006a, FR-013a/b and FR-015a/b) and every
    SC-001…SC-006 maps to landed text, a landed commit, or a landed and passing test. Items that cannot
    hold yet carry an **explicit waiver**, never "passed" (pre-flight 4d accepts only 100 % or every
    shortfall paired with a waiver rationale). Each reads "waived at audit: satisfied after Gate B by
    T057/T061/T063/T064, re-asserted in the Closure subsection":
    - SC-006 (Gate B convergence and the owner's sign-off);
    - FR-003a's RATIFIED flip;
    - FR-003c's title, and FR-019's body, until T057/T064.
  - **(iii)** Every feature-owned catalogue row is as T069 recorded it.
  - Record the verdict (100 %, or every shortfall paired with its waiver rationale) in
    `.specify/decisions/094-v1-tier0-bookkeeping-verify.md` `## Completeness`. `/gate-b` pre-flight 4d
    hard-blocks without it. After T063, re-assert (i)–(iii) at the C-RATIFY head and append a "Closure
    at the final head" subsection.

---

## Dependencies and execution order

### Phase dependencies

- **Setup (T001–T005)**: T001 first; T002–T005 are read-only and parallel.
- **Foundational (T006–T011)**: T006 → T007, T008, T009, T010 (any order) → T011. **Blocks**:
  - T025 (L-094-1 needs T007);
  - T032 (NFR-002 needs T008);
  - T041 (C-607 needs T010);
  - T011's record of all four numbers.
- **US1 (T012–T026)**:
  - catalogue T012 → T013 → T014;
  - coverage index T015;
  - constitution T016 → T017;
  - T018 runs in parallel with T016;
  - T019 after T012–T018;
  - T020–T024 run in parallel;
  - T025 after T007;
  - T026 last.
- **US2 (T027–T039)**:
  - T027 ‖ T028 → T029;
  - T030 after T029;
  - catalogue T031 → T032 → T033 → T034 → T035 → T037, all after T014 and T030;
  - T035's TC-017 needs T029;
  - coverage index T036 after T015 and before T037;
  - T038 after T037;
  - T039 after T038 (it ends with the US2 docs commit, so it must not run beside a half-written catalogue).
- **US3 (T040–T042)**: T040 → T041 (also after T010) → T042. Its files are disjoint from US1/US2, but
  its commits serialize with theirs in the one tree.
- **Polish**:
  - T043 → T044–T048 (parallel, except T047's runs) → T049 → T050, T051 → T052, T053;
  - T054 → T055 → T056 → T057 → T058, T059, T060 → **T061 (owner)** → T062 → **T063 (last commit)** →
    T064 → T065 → T066, T067, T068;
  - T069 → T070 run BEFORE T054 chronologically (the recorded deviation), and T070 is re-asserted after
    T063.

### Critical path

T001 → T006 → T007/T008/T010 → T011 → (T012…T026) → (T027…T039) → (T040…T042) → T043 → T049 →
T069 → T070 → T054 → T057 → **T061 owner sign-off** → T063 → T064 → T065 → T066/T067/T068.

### Parallel opportunities

- T002–T005 (read-only baselines).
- T016 ‖ T018 (constitution vs CHANGELOG); T020 ‖ T021 ‖ T022 ‖ T023 ‖ T024 (five different files).
- T027 ‖ T028 (two test files).
- T044 ‖ T045 ‖ T046 ‖ T048.
- US3's file edits (T040, T041) alongside US1/US2 docs, provided commits stay serial.

## Parallel example: User Story 1

```text
Task: "T016 [US1] [orchestrator] .specify/constitution.md six clauses (data-model §E.3–§E.7)"
Task: "T018 [US1] [orchestrator] CHANGELOG.md §E.8 pending entry"
Task: "T020 [US1] [orchestrator] .specify/architecture.md §F.1 + Z-9"
Task: "T021 [US1] [orchestrator] .specify/api-contract.md §F.2"
Task: "T022 [US1] [orchestrator] .specify/2{i,j,l,m}-*.md §F.3 headers"
Task: "T023 [US1] [orchestrator] README.md:15 + docs/src/intro.md:3 §F.4"
Task: "T024 [US1] [orchestrator] brain/components/service.md §F.5"
```

## Implementation strategy

- **MVP = US1**: DECISION 1 written into the catalogue and the PENDING v4.0 amendment. It removes 7 of
  the tag gate's open rows on its own (T014).
- **Then US2**: the 18 flips. Their evidence runs (T030) and the FR-022 cells (T027–T029) must be green
  before any row is written.
- **Then US3**: two `phase-implementer` commits, C-SPDX and C-607.
- **Then** verify, Gate B, the owner's sign-off, and the RATIFIED flip as the last commit.
