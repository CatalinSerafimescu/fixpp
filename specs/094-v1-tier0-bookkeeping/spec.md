# Feature Specification: v1.0 Tier-0 bookkeeping — catalogue reconciliation, DECISION 1, SPDX marking

**Feature Branch**: `094-v1-tier0-bookkeeping`

**Created**: 2026-10-22

**Status**: Draft

**Input**: User description: "v1.0 Tier-0 bookkeeping (070-style, [const §VI.6]): a docs-only feature
that (1) moves W-015, W-016, SVC-001..005 to `dropped(post-v1.0, planned, not abandoned: <reason>)`
per owner DECISION 1, adds the catalogue legend line, and amends the constitution so it no longer
requires a v1.0 service wrapper; (2) flips 18 catalogue rows to `done` as a reconciliation citing the
delivering PRs and verifying tests; (3) marks every `src/`/`include/` C/C++ file AGPL-3.0-or-later."

## Terminology

- **Reconciliation flip** — a catalogue row whose behaviour already shipped in earlier PRs, moved to
  `done` by this feature with its cells citing those PRs and tests. It is not a delivery claim. The
  precedents are D-002 (070's bookkeeping flip) and D-010 (083: "stated as a reconciliation, not as a
  delivery claim").
- **Post-v1.0 drop** — the status form `dropped(post-v1.0, planned, not abandoned: <reason>)`. It
  takes a row out of the v1.0 tag gate (`[const §I.3]`'s done-or-dropped rule) and nothing more: the
  row stays planned and returns to `backlog` when its release opens.
- **Residual on record** — a part of a row's original wording that the reworded row no longer
  claims, and that is named in its Verified cell as a live B&L row or an open issue.

## Context: why this feature exists

The ★ V1.0 FUNCTIONAL TRACK (parent `REMAINING-WORK.md`, owner 2026-10-21) makes this PR its step 1:
the cheapest movement towards the v1.0 tag, with no behavioural code. Its inputs are all decided:

- **DECISION 1 (owner, 2026-10-21):** the seven rows W-015 (FIXP), W-016 (SOFH) and SVC-001..005
  (gRPC control plane, iceoryx2 data plane, data-plane opt-in, health, pluggable `ControlPlane`) leave
  the v1.0 tag gate, and **none is abandoned**. The owner's words: *"We don't want to drop them
  forever, we want them to implement post-v1.0 … as long we don't permanently drop them and we don't
  get back to a debate about saying that we 'dropped' them today."* This ruling is settled.
- **Row-by-row verification at fixpp `9b176090`**
  (`research/G19-fix-fpml-iso20022/research/findings/v1-tier0-flip-verification-2026-10-21.md`): 4 rows
  flip as written, 11 flip once reworded to what shipped, 4 needed an owner call, and 9 are NOT
  delivered.
- **Owner rulings (2026-10-21):** all 11 rewordings approved. NFR-002 is reworded and flipped. NFR-013
  is split, with io_uring becoming a new post-v1.0 row. TC-017 is not supported by design and is
  flipped. NFR-010 is postponed and stays `backlog`.
- **Owner rulings (2026-10-22):** the service wrapper goes on the post-1.0 roadmap as **unscheduled**,
  with no version slot. The 16 `Apache-2.0` transport files are **relicensed to AGPL-3.0-or-later** in
  this PR. Their Apache identifier was inherited from 012's contract templates, not chosen.

`main` at `9b176090` still states the dropped scope as v1.0 in the constitution:
- `[const §XVIII.1]` "v1.0 scope is locked: … the gRPC service wrapper, iceoryx2 SHM data plane (opt-in)".
- `[const §XIV.1]` "each with one default impl in v1.0: … Control plane (default: gRPC …)".
- `[const §XIV.3]` "The control plane (gRPC) works without it".
- `[const §IV.4]` "Service wrapper (`service/`) is opt-in. gRPC is the control-plane transport".

As long as these clauses stand, dropping the rows would leave the constitution requiring something
the catalogue no longer schedules. `[const §XX.1]` says to amend the article first, in the same PR.

## Clarifications

### Session 2026-10-22 (owner)

- Q: Where does the service wrapper sit on `[const §XVIII.2]`'s locked post-1.0 roadmap? → A:
  **Unscheduled post-1.0.** One line under §2. No version slot, and v1.1–v1.8 are not re-ordered.
- Q: The 16 `Apache-2.0` transport files? → A: **Relicense to AGPL-3.0-or-later in this PR.**
- Q: `conanfile.py` says `AGPL-3.0-only`, while the marked files and `pyproject.toml` say `-or-later`.
  Which is intended? → A: **`AGPL-3.0-or-later`; fix `conanfile.py` in this PR.** The owner was told
  that the edit misses the Conan cache on every CI lane once, and accepted that cost.
- Q (resolved without the owner, by `[const §XX.2]`): the owner's brief said "short spec + Gate B".
  Article XX §2 requires a Codex Gate A review on **every** amendment, so this feature runs `/gate-a`
  on the bundle too. It is a governance requirement, not a choice of scope.

## User Scenarios & Testing *(mandatory)*

The "users" are the owner tagging v1.0 and any contributor who reads the catalogue or the
constitution to decide what v1.0 owes.

### User Story 1 — The tag gate stops counting rows nobody will build for v1.0 (Priority: P1)

The owner counts the catalogue's open OFFICIAL rows to see what blocks the v1.0 tag. The seven
post-v1.0 rows no longer count as open. Each one still says it is planned and why it was deferred,
and the constitution no longer requires a v1.0 service wrapper.

**Why this priority**: DECISION 1 is the only owner-signed scope change on the track. Until it is
written down, the rows block the tag although nobody intends to build them for v1.0.

**Independent Test**: Read the seven rows, the catalogue legend and the amended articles. Run the
open-row census (plan §Census) before and after the change: the after-count drops by exactly seven
for these rows, and no other row changes.

**Acceptance Scenarios**:

1. **Given** the catalogue on this branch, **When** a reader looks up W-015, W-016 or SVC-001..005,
   **Then** each row's Status reads `dropped(post-v1.0, planned, not abandoned: <reason>)` with the
   owner's reason, and its other cells are unchanged.
2. **Given** the catalogue header, **When** a reader looks up the Status legend, **Then** a line
   defines the post-v1.0 drop form: excluded from the v1.0 tag only, still planned, back to `backlog`
   when its release opens.
3. **Given** the amended constitution, **When** a reader looks for a v1.0 obligation to ship the gRPC
   control plane, the iceoryx2 data plane or the service wrapper, **Then** none remains. §XVIII.2
   lists the service wrapper as unscheduled post-1.0, and the pluggable control-plane interface stays.
4. **Given** the installed `include/fixpp/service/control_plane_factory.hpp` and the C-ABI codes
   `FIXPP_ERR_CTRL_CONFIG` (900) / `FIXPP_ERR_CTRL_RUNTIME` (901), **When** this feature merges,
   **Then** both are byte-unchanged apart from FR-013's licence line.

---

### User Story 2 — Every delivered row reads `done` and says only what shipped (Priority: P1)

A contributor reads an NFR, D or TC row to learn whether fixpp provides it. Each of the 18 rows reads
`done`. Its cells cite the PRs that delivered it and the tests that witness it. Anything the original
wording promised but did not ship is named as a residual on record.

**Why this priority**: every row is a §I.3 open row today. A `done` that claims more than shipped
would be worse than `backlog`, so the rewording is the half that matters.

**Independent Test**: For each row, open its Tests cell and confirm every cited test exists and
passes on this branch (the verify record lists them). Confirm each residual its Verified cell names
is a live B&L row or an open issue.

**Acceptance Scenarios**:

1. **Given** one of the 4 clean rows (NFR-001, D-001, D-003, D-007), **When** read, **Then** its
   Title is unchanged and Status is `done`. Its /specify, PR, Tests and Verified cells cite the
   delivery, and Verified says it is a reconciliation.
2. **Given** one of the 11 reworded rows, **When** read, **Then** its Title and FIX-version cells
   carry the owner-approved wording (FR-006), and Verified names each residual on record.
3. **Given** TC-005, **When** read, **Then** it no longer claims that an inbound Reject is logged, and
   Verified names fixpp#612 (no `fromAdmin` delivery or log of an inbound Reject).
4. **Given** NFR-013, **When** read, **Then** it claims ASIO async I/O (epoll on Linux, IOCP on
   Windows) only, and a new row NFR-019 carries the io_uring opt-in as a post-v1.0 drop.

---

### User Story 3 — Every library source file says which licence it is under (Priority: P2)

A reader or a licence scanner opens any C/C++ file under `src/` or `include/` and finds
`SPDX-License-Identifier: AGPL-3.0-or-later` at the top.

**Why this priority**: NFR-010's AGPL half is independent of the postponed commercial half, and the
owner put the marking in this PR. It does not block the other stories.

**Independent Test**: A census over the tracked `src/` and `include/` C/C++ files finds every file
carrying the AGPL identifier and none carrying another. A diff filter confirms the only lines changed
in those files are licence comment lines.

**Acceptance Scenarios**:

1. **Given** the 37 files with no SPDX line at `9b176090`, **When** this feature merges, **Then** each
   starts with an AGPL-3.0-or-later SPDX comment in the comment form its language uses.
2. **Given** the 16 files marked `Apache-2.0` at `9b176090`, **When** this feature merges, **Then**
   each says `AGPL-3.0-or-later`, and its copyright line is unchanged.
3. **Given** the C-ABI freeze gate, **When** it runs on this branch, **Then** it passes against a
   manifest regenerated in this PR, and the exported C-ABI symbol set is unchanged.

---

### Edge Cases

- **A row's evidence fails at run time.** A cited test that fails or is missing on this branch means
  the row does not flip. The row stays `backlog`, the finding is recorded, and the owner is told. A
  row is never flipped on a cell nobody ran.
- **A residual is not on record.** If a residual is neither a live B&L row nor an open issue, the row
  does not flip until an issue is filed. The rule is the owner's (2026-10-21).
- **A Status string breaks a parser.** The drop form contains commas and a colon. Every tool that
  reads the catalogue's Status column must still parse all rows (FR-016).
- **A file is outside the census.** Generated files under the build tree are not tracked and are out
  of scope. A tracked file with a non-C/C++ extension under `src/`/`include/` is listed in the verify
  record, not silently skipped.
- **The stale coverage-index notes.** The `005-defer: recovery-dependent` notes on FIX-TC scenarios 8–11
  are false since 013 (PR #86) and S-023 (PR #90), and the "093 (partial; TC-003 stays backlog)" note
  on scenario 3 becomes false when TC-003 flips. They are rewritten in the same edit (FR-011).

## Requirements *(mandatory)*

### Functional Requirements

**Post-v1.0 drops (DECISION 1)**

- **FR-001**: W-015, W-016 and SVC-001..005 MUST carry the Status
  `dropped(post-v1.0, planned, not abandoned: <reason>)`. W-015 and W-016 take the reason "new wire
  format, not core tag-value FIX, no QuickFIX reference implementation". SVC-001..005 take "service
  wrapper deferred by owner decision 2026-07-15; `src/service/` is a stub". Every other cell of these
  rows MUST stay unchanged.
- **FR-002**: The catalogue's Status legend MUST define the post-v1.0 drop form. It excludes the row
  from the v1.0 tag only, the row stays planned, and it returns to `backlog` when its release opens.
  The wording MUST NOT read as abandonment. The legend MUST name the user-signed rationale `[const
  §I.3]` requires: DECISION 1, and the NFR-013 split ruling. The definition is appended to the
  existing Status line so no catalogue line moves.
- **FR-003**: The constitution MUST be amended so no clause requires a v1.0 service wrapper, gRPC
  control plane or iceoryx2 data plane:
  - `[const §XVIII.1]` drops them from the locked v1.0 scope.
  - `[const §XVIII.2]` gains one line: service wrapper (gRPC control plane, iceoryx2 data plane),
    unscheduled post-1.0.
  - `[const §XIV.1]` keeps the control plane as a pluggable interface; its default gRPC
    implementation moves to post-1.0.
  - `[const §XIV.3]` and `[const §IV.4]` get the same post-1.0 note.
  - Any further clause the plan's sweep finds naming the same scope as v1.0 is amended too (FR-004).
- **FR-003a**: The amendment MUST carry a Sync Impact Report (`[const §XX.3]`) with: the date, the
  version bump and an argued `[const §XX.4]` classification, the clauses changed, the rationale, the
  affected rows and specs, the review and sign-off, and the feature it rode. If the classification is
  backwards-incompatible, `CHANGELOG.md` MUST gain an entry. It MUST record that it rides a feature
  branch instead of a standalone `Constitution: amend …` PR, as a recorded deviation from
  `[const §XX.2]` (precedents 035, 043, 069).
- **FR-003b**: The installed `include/fixpp/service/control_plane_factory.hpp` and the C-ABI codes 900
  and 901 MUST be kept as shipped. Removing a frozen code would be a break for nothing.
- **FR-004**: Every other in-repo text that states the dropped scope as v1.0 MUST be updated, or
  annotated with a pointer to this feature. This includes `.specify/architecture.md` ("Stable from
  v1.0: … the gRPC control-plane proto"), `.specify/2j-controlplane.md`'s status header and
  `brain/components/service.md` (which still calls the disposition "a live decision recorded outside
  this repo"). The plan enumerates the full set by a recorded grep.

**Reconciliation flips (18 rows)**

- **FR-005**: NFR-001, D-001, D-003 and D-007 MUST move to `done`, Title unchanged. Their /specify,
  PR, Tests and Verified cells follow the verification file, and Verified states that the flip is a
  reconciliation, not a delivery claim.
- **FR-006**: The 11 reworded rows MUST move to `done` with the owner-approved wording (REMAINING-WORK
  ★ functional track, rulings of 2026-10-21; text in the verification file):
  - **Version column only:** TC-003, TC-008 and TC-015 → "4.2, 4.4 witnessed; FIXT.1.1 shares the code
    path, not witnessed". D-008 → "4.2, 4.4, 5.0SP2, FIXT.1.1 (+ FIX Latest opt-in); runtime-XML-only
    versions L-003-2".
  - **Narrowed, residual on record:**
    - NFR-003: names #555 and the allowlisted allocation-failure cells.
    - NFR-005: zero allocation on the gated windows only, L-497-1.
    - NFR-011: library surfaces only, naming L-002-4, #565 and B-495-3; "C++17" dropped from the Title.
    - TC-006: names #588 and #580.
    - TC-012: the duplicate check belongs to the application, per FIX-TC.
    - TC-014: ResetSeqNumFlag only; FIX-TC accepts either option.
  - **TC-005:** "NextNumIn advances, session continues, no Reject of a Reject"; Verified names fixpp#612.
  - Each TC row also takes the version-column narrowing that the verification file applies to every
    TC flip.
- **FR-007**: NFR-002 MUST be reworded to `[const §IX.1]`'s per-PR obligation: ≥95/85 on touched
  modules, or a recorded assessment per uncovered line, checked in `/speckit-verify`, not a CI
  threshold. It then moves to `done`. The global ≥95 % stays a separate release question (row 11).
- **FR-008**: NFR-013 MUST be reworded to "ASIO async I/O transport (epoll on Linux, IOCP on Windows)"
  and moved to `done`. A new row **NFR-019** (`OFFICIAL`, nfr) MUST carry the io_uring opt-in with
  Status `dropped(post-v1.0, planned, not abandoned: io_uring opt-in, to be measured in the row-16 perf
  pass)`. Before it lands, NFR-019 gets a coverage-index entry (`[const §VI.4]`).
- **FR-009**: TC-017 MUST be reworded to "EncryptMethod(98): 0 accepted; ≠0 refused at Logon
  (B-043-1); no application-layer encryption (`[const §XII.7]`, §XV.10), so 17c–17j do not apply" and
  moved to `done`.
- **FR-006a**: A TC row's version column MUST name only the BeginStrings its cited witnesses actually
  drive, determined per test (fixture, helper default or instantiation list, not a file-level grep).
  The owner's "4.2, 4.4 witnessed" wording applies where both are driven. Where only one is, the
  column narrows further, because the owner's rule (narrow to what shipped) governs the wording, not
  the other way round. Each such deviation is listed in research.md and reported to the owner before
  Gate A. The versions left unwitnessed are required by FIX-TC §4.1, so the owner's rule (narrow only
  where the removed part is not required, or is on record and named) needs them on record. They are,
  as **fixpp#614** (filed 2026-10-22), which every affected Verified cell names.
- **FR-010**: Every flipped row's cells MUST cite tests that exist literally in the tree. Its PR cell
  MUST cite the PR and squash SHA, resolved from git, not copied from a note. Its /specify cell MUST
  read `094-v1-tier0-bookkeeping (reconciliation; delivered by <feature or PR>)`, following D-002's
  `070-… (bookkeeping; …)` form, so that 094 is the `/specify` artifact `[const §VI.6]`(a) requires.
- **FR-011**: `spec/coverage-index.md` MUST be updated in the same edit:
  - the stale `005-defer` notes on FIX-TC scenarios 8, 9, 10 and 11 are rewritten to their current
    owners (TC-007 stays `backlog`, and its note says why);
  - scenario 3's "TC-003 stays backlog" note is replaced;
  - the 005 deferral ledger rows for scenarios 8–11 are annotated.
  TC-003's "Feature 093 does not own this row … stays backlog" note in the catalogue is removed.
- **FR-012**: These rows MUST NOT change, because they are not delivered: NFR-010 (postponed), NFR-004,
  NFR-008, TC-001, TC-002, TC-004, TC-007, TC-009, TC-010 and TC-013.

**Licence marking**

- **FR-013**: Every tracked C/C++ file under `src/` and `include/` MUST carry
  `SPDX-License-Identifier: AGPL-3.0-or-later` as its first comment line: `//` in C++ files, and
  `/* … */` in C headers, matching `include/fix/c_api/log.h`. The 37 unmarked files gain the line.
  The 16 `Apache-2.0` files have their identifier replaced, and their `Copyright` line stays. No other
  byte of these files changes.
- **FR-013a**: `conanfile.py`'s `license` field MUST change from `AGPL-3.0-only` to
  `AGPL-3.0-or-later`, so the package metadata agrees with the files and with
  `bindings/python/pyproject.toml` (owner, 2026-10-22). The owner accepted the known cost:
  `ci/conan-cache-key.sh` hashes the whole of `conanfile.py`, so this PR's CI misses the Conan cache on
  every lane once, and its `push:main` reseeds the cache. The PR body MUST say so, so a slow first run
  is not misread as a regression.
- **FR-014**: `tools/capi_freeze.sha256` MUST be regenerated for the C-ABI headers FR-013 touches, and
  `tools/check_capi_freeze.sh` MUST pass. The PR body MUST state that the C-ABI change is comment-only.

**Evidence and gates**

- **FR-015**: The verify record (`.specify/decisions/094-v1-tier0-bookkeeping-verify.md`) MUST give
  each flipped row its evidence in one of three forms:
  - **Test rows** (D-*, TC-*, NFR-011): the binaries run, on the preset CI runs them on, with the
    filter, `--no-tests=error`, and the pass count read from the binary's output.
  - **CI rows** (NFR-001, NFR-003, NFR-005, NFR-013): the required jobs green on the cited `push:main`
    runs at `9b176090` (Tier 1 `39594667883`, Tier 2 `39594667975`), plus the workflow job and step
    names re-read on this branch. NFR-005 and NFR-013 also run their witnesses locally.
  - **Process row** (NFR-002): the three cited verify records, each showing a per-line coverage
    assessment.

  A row whose evidence did not hold MUST stay out of the flip set (Edge Cases).
- **FR-015a**: Every Verified cell MUST state its QuickFIX-interop disposition, because the catalogue
  legend's Verified rule names interop "where applicable". The disposition is "not applicable" for the
  D/NFR rows. For the TC rows it is "witnessed in-process; the live QuickFIX cells skip without a
  counterparty, so they are not per-PR evidence and are not claimed".
- **FR-016**: Every tool that parses the catalogue (`tools/brain_inventory.py` and any other found by
  the plan's sweep) MUST still parse every row. The open-row census MUST be recorded before and after
  the change, with a delta of −25 open rows (7 drops + 18 flips) and +1 dropped row (NFR-019). The
  census is first proven against a known positive.
- **FR-017**: The B&L files MUST NOT change. This feature has no functional delta, and the Gate B
  record carries that explicit no-delta disposition.

### Key Entities

- **Catalogue row**: ID, Source, Category, Title, FIX version(s), Spec ref, Status, /specify, PR,
  Tests, Verified (`spec/feature-catalogue.md` schema line).
- **Residual on record**: a live B&L row ID (`L-…`/`B-…`) or an open fixpp issue number.
- **Amendment**: the clauses changed, the Sync Impact Report and the version bump.

## Success Criteria *(mandatory)*

- **SC-001**: The open OFFICIAL row count falls by exactly 25 (7 drops, 18 flips), and no other row
  changes Status. Both counts come from the census output.
- **SC-002**: 100 % of the tests cited in the 18 flipped rows exist literally and pass on this branch.
- **SC-003**: 0 constitution clauses state the service wrapper, gRPC control plane or iceoryx2 data
  plane as v1.0 scope, by the recorded sweep.
- **SC-004**: 100 % of tracked C/C++ files under `src/` and `include/` carry the AGPL-3.0-or-later
  identifier, and 0 carry another identifier.
- **SC-005**: The diff outside `spec/`, `.specify/`, `brain/`, `specs/` and `CHANGELOG.md` contains
  only licence comment lines, `tools/capi_freeze.sha256` and `conanfile.py`'s `license` line.
- **SC-006**: Gate A (on the amendment and bundle) and Gate B converge, and the owner signs off on the
  amendment.

## Assumptions

- The verification file's readings hold at the branch base, because it was read at `9b176090` and that
  is still `origin/main`. Every citation is still re-checked (FR-010, FR-015).
- The 47 test names the flips cite were grep-checked on 2026-10-22, with a negative control: all
  exist. Passing is checked at verify.
- The atomic-shared-ptr spike (parent `research/G19-fix-fpml-iso20022/atomic-shared-ptr/`) carries no
  licence or copyright. The 37 unmarked files carry no foreign copyright notice (provenance grep,
  2026-10-22). So marking them AGPL relabels the owner's own work.
- Licence comments are outside Doxygen output, and no golden test pins those bytes. The plan checks
  the second point with a byte-pin grep.

## Out of scope

- NFR-010's commercial half (postponed by the owner) and counsel review (row 15d, L-2).
- The 9 not-delivered rows (FR-012), which stay `backlog` after the typed-message track.
- SPDX marking outside `src/`/`include/` (tests, tools, bench, bindings, examples).
- Any code or behaviour change, including the fixpp#612 fix (batch B65).

## Normative References

- `[const §VI.6]` — closing a `done` row; `[const §VI.4]` — coverage-index entry before a new OFFICIAL
  row; `[const §XX.1]`–`[const §XX.4]` — the amendment process and classification; `[const §I.3]` — the
  tag gate; `[const §IX.1]` — NFR-002's new wording; `[const §XII.7]`, `[const §XV.10]` — TC-017.
- `[FIX-TC §4 scenario 3]`, `[FIX-TC §4 scenario 7]`, `[FIX-TC §4 scenario 8]`,
  `[FIX-TC §4 scenario 9]`, `[FIX-TC §4 scenario 11]`, `[FIX-TC §4 scenario 15]`,
  `[FIX-TC §4 scenario 17]`, `[FIX-TC §4 scenario 19]` — the coverage-index entries for the TC rows
  flipped.
- `[FIX42]`, `[FIX50SP2]` — D-001, D-003.
- `research/G19-fix-fpml-iso20022/research/findings/v1-tier0-flip-verification-2026-10-21.md` — the
  per-row evidence and proposed cells.
- Parent `REMAINING-WORK.md` — ★ V1.0 FUNCTIONAL TRACK row 1, DECISION 1, the Tier-0 re-verification
  note and the owner rulings of 2026-10-21.
- `spec/behaviors-and-limitations.md` — L-003-2, L-022-1, L-497-1, L-002-4, B-043-1, B-005-7,
  B-495-3, B-544-1 (the residuals named in Verified cells).
