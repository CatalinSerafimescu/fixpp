# Implementation Plan: v1.0 Tier-0 bookkeeping

**Branch**: `094-v1-tier0-bookkeeping` | **Date**: 2026-10-22 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/094-v1-tier0-bookkeeping/spec.md`

## Gate A

- Round 1 applied 2026-10-22: Codex P1=3 P2=5 P3=1; Opus post-judging P1=3 P2=8 P3=14; rewrite addresses root causes RC-1, RC-2, RC-3 (+ owner rulings 2026-10-22). Reviews: research/reviews/codex_094-v1-tier0-bookkeeping_gate_a_review.md, research/reviews/opus_094-v1-tier0-bookkeeping_gate_a_adversarial_review.md.

### Round 1 — what changed, by root cause

- **RC-1 (the amendment drafted from the ruling's words):** data-model §E now holds the literal text:
  the v3.0 → v4.0 Sync Impact Report (PENDING form and its flip), the Status line in both forms, and
  before → after for §IV.4, §XIV.1 (lead-in and bullet), §XIV.3, §XVIII.1 and §XVIII.2, plus the
  `CHANGELOG.md` entry in both forms. MAJOR per R-2. §XIV.1 places the control-plane interface
  post-1.0 with its default implementation (owner). The PR is titled in §XX.2 form (FR-003c), so no
  deviation is recorded.
- **RC-2 (the sweep scoped by guesswork):** research R-12 is a repository-wide recipe over tracked
  files outside `specs/`, for dropped-scope terms, all 26 row IDs and the licence strings. It is
  proven on known positives, and every hit has a disposition. Its "edit" hits have exact text in
  data-model §D, §F and §G (architecture incl. Z-9, api-contract, 2i/2j/2l/2m headers, coverage-index
  :421/:436/:652, README, intro, three brain pages, the RPM licence, L-094-1).
- **RC-3 (witnesses cited by name):** data-model §H's claim → witness map, required in full in the
  verify record (FR-015b). NFR-002, TC-017, TC-005, NFR-011, D-008, NFR-005 and NFR-001 cells were
  corrected against what each witness asserts.

### Round 1 — disagreements

- **C-7 (Codex P2, "#614 is unverified"): not applied.** The Opus judge disagreed, and the state was
  re-read: `gh issue view 614 --repo CatalinSerafimescu/fixpp` gives OPEN, created
  2026-10-22T08:46:26Z, labels `enhancement`, `follow-up`, with the eight-row table and both acceptance
  routes in its body. Codex had no `gh`, so it could not see the issue. The owner's rule accepts an
  open issue as "on record". The two residuals the judge kept as P3 are applied instead: the verify
  record snapshots the issue (FR-015), and close-out places it in `issue-batches.md` (below).
- **C-8 (Codex P2, NFR-001 provenance): applied at the judge's P3.** The claim was true (branch
  protection lists the three required checks); only the citation was vague. The PR cell now names
  `96dd8aed`, `#166 ff19ae58` and `27f8701d`.
- **The judge's "add a new Z-6 entry" (C-4):** Appendix Z already runs to Z-8, so the entry is
  **Z-9**.

### Round 1 — owner questions (none blocks this bundle)

1. **NFR-002 and `[const §VI.6]`(b).** NFR-002 is a process obligation with no in-repo verifying test;
   its evidence is two verify records in the private parent repository. Does a record count as the
   "verifying tests" §VI.6(b) asks for? The owner ruled the flip on 2026-10-21; this asks only for the
   §VI.6 reading to be confirmed.
2. **`544-hot-path-zero-alloc-verify.md` assessed added lines only** (81.73 % branch, no per-branch
   table). It is no longer cited as NFR-002 evidence. Does that record need an issue?
3. **A consequence of dropping SVC-001/SVC-002:** v1.0 has no cross-process route for a non-C++ tap
   consumer (2l non-goal 7, now annotated), and no non-C++ route for runtime cert/pinset rotation
   (2i §7.7). The coverage-index note on T-039/T-041 (:552) names 2j's `ReloadCertSource` as a
   dependency, which is now post-1.0 work. S-036 and NFR-014 (the in-process tap) are not affected.
4. **Licence prose.** `README.md:11` (badge), `README.md:25–26`, `NOTICE:7` and
   `LICENSE-COMMERCIAL.md:3` say "AGPL-3.0" without "or later". This bundle changes package metadata
   only. Edit the prose too, or leave it?
5. **40 Apache-2.0 files remain outside `src/`/`include/`** (37 under `tests/`, 3 under `bench/`). Out
   of scope by the spec. File a follow-up issue?
6. **TC-017's in-domain ending** (98=1..6 → Disconnected, nothing sent) is witnessed by composition
   (the parser cell, plus the session arm driven with another refusal cause), not by a session-level
   98=2 cell. Adding one is a test-only change, outside SC-005. Accept the composition, or add the
   cell in this PR?

Observation, not a question: `tests/conformance/test_transport_interop.cpp` labels its DISABLED cells
`TC-001..TC-017` with a numbering that differs from the catalogue's TC rows (there, TC-012 is a
BodyLength/CheckSum case). No Verified cell cites them.

## Summary

This is a governance and reconciliation feature with no behavioural code. It:
- writes the owner's DECISION 1 into the catalogue (7 post-v1.0 drops plus a legend line) and into the
  constitution (**v3.0 → v4.0, MAJOR**: the service wrapper and the control-plane interface leave v1.0
  scope and become unscheduled post-1.0), with a `CHANGELOG.md` entry;
- records that `EngineConfig::control_plane_factory` is accepted and ignored, as a new live
  limitation L-094-1 and a new issue;
- flips 18 delivered rows to `done` as reconciliations, worded to what shipped, and splits io_uring
  into a new post-v1.0 row NFR-019;
- marks every library source file `AGPL-3.0-or-later`, including the C-ABI freeze manifest and the
  Conan and RPM licence fields; and, in a separate commit, updates `conanfile.py`'s `tool_requires`
  comment for #607.

Every target cell and clause is written out in [data-model.md](./data-model.md). Every fact behind
them is measured in [research.md](./research.md).

## Technical Context

**Language/Version**: Markdown (catalogue, coverage index, B&L, constitution, CHANGELOG, design docs,
brain, README, docs). The only source-file change is a comment line (C++23 / C headers), plus one
string and one comment in `conanfile.py` and one string in `cmake/FixppPackaging.cmake`.

**Primary Dependencies**: none added or changed.

**Storage**: N/A.

**Testing**: no new tests. The verifying tests of the 18 flipped rows run unchanged (FR-015).
Instruments:
- `catalogue_census.py` (R-4)
- the R-12 sweep recipe (three `git grep`s)
- `tools/check_capi_freeze.sh`
- `tools/check_line_citations.py --range/--shift-audit`
- `.claude/scripts/check-comment-claims.py` (parent repo)
- `tools/brain_inventory.py`
- `.claude/scripts/check_bl_delta.py` (parent repo; the branch now touches the live B&L file)
- the brain freshness job's in-repo checks, and the parent's `tools/brain-external-sweep.sh`

**Target Platform**: unchanged. Comment-only source changes mean the build output is identical, apart
from `__LINE__`-derived values shifting by one in the 37 files that gain a line (see Risks).

**Project Type**: C++23 library.

**Performance Goals / Constraints**: none. The bench gate is not engaged (no code change).

**Scale/Scope**:
- 25 catalogue rows changed plus 1 added (NFR-019), and the legend line;
- coverage index: 9 lines edited (:405, :410–:413, :421, :435, :436, :652) and 1 paragraph added;
- constitution: 5 clauses (§IV.4, §XIV.1, §XIV.3, §XVIII.1, §XVIII.2), the Status line and the Sync
  Impact Report; `CHANGELOG.md`: 1 entry;
- live B&L: 1 new section with L-094-1;
- 11 other surfaces: `.specify/architecture.md`, `.specify/api-contract.md`, `.specify/2i-capi.md`,
  `.specify/2j-controlplane.md`, `.specify/2l-tap.md`, `.specify/2m-pybind.md`,
  `brain/components/{service,dictionary,nfr-and-tooling}.md`, `README.md`, `docs/src/intro.md`;
- 53 source files (one line each), `tools/capi_freeze.sha256`, `conanfile.py` (two commits) and
  `cmake/FixppPackaging.cmake`;
- 1 new fixpp issue (FR-018).

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-checked after Phase 1 design.*

| Article | Applies | Disposition |
|---|---|---|
| §VI.6 (closing a `done` row) | yes | (a) 094 is the `/specify` artifact, cited in each /specify cell. (b) Every flipped row's tests run green at verify (FR-015), each clause mapped to an assertion (FR-015b); NFR-002's records are owner question 1. (c) Codex Gate B runs. (d) Gate A runs (R-1). |
| §VI.4 (coverage-index entry before a new OFFICIAL row) | yes | The NFR-019 supplemental lands in the same edit (data-model §D). |
| §VI.3 (`[impl]` rows noted as design choices) | yes | The NFR-019 supplemental says so. |
| §XX.1–§XX.3 (amendment) | yes | Amended in the same PR as the conflicting change, with the Sync Impact Report (data-model §E.1). The PR is titled `Constitution: amend §IV.4, §XIV.1, §XIV.3, §XVIII.1, §XVIII.2 — service wrapper post-1.0 (+ 094 Tier-0 bookkeeping)` and its description states change, rationale and affected rows (FR-003c), so §XX.2's four steps are met. Codex Gate A runs, then owner sign-off; the SIR reads PENDING until both. |
| §XX.4 (classification) | yes | MAJOR, v3.0 → v4.0, with a `CHANGELOG.md` entry (R-2; owner, 2026-10-22). |
| §XVIII.4 (roadmap change = amendment) | yes | The §XVIII.2 line is part of the amendment. |
| §XVIII.5 (no early shipping) | yes | Unchanged; it is why the amendment is MAJOR (R-2). |
| §XVI.6 (orchestrator does not implement) | yes | The 53 source files, `capi_freeze.sha256`, `conanfile.py` and `cmake/FixppPackaging.cmake` go to `phase-implementer`. Docs (`spec/`, `specs/`, `.specify/*.md`, `brain/`, `CHANGELOG.md`, `README.md`, `docs/src/intro.md`) are governance and documentation text the orchestrator may write. |
| §X.7 / C-ABI freeze | yes | Comment-only header change: the manifest is regenerated and the symbol set is unchanged (R-7). No C-ABI version bump, because no ABI changes. |
| §V.1 / §V.4 (licence; vendored attribution) | yes | AGPL marking only; the relicense basis is R-8. No vendored file is in the set. NFR-010's commercial half is untouched (postponed). |
| §IX.1 (coverage) | n/a | No line of code is added or made reachable. The verify record says so per touched module. |
| §VIII (bench) | n/a | No code change. |
| §XVII.8 (verify before Gate B) | yes | `/speckit-verify` runs. Its plan derives from this diff: docs-and-comment gates, the flipped rows' witness binaries, one full build to prove the comment edits compile, and the packaging lane's RPM build (FR-013a). |

**Result:** PASS, with no recorded deviation.

## Project Structure

### Documentation (this feature)

```text
specs/094-v1-tier0-bookkeeping/
├── spec.md
├── plan.md              # this file
├── research.md          # R-1 … R-14
├── data-model.md        # exact target text of every changed row / clause / surface (§A–§H)
├── quickstart.md        # how to re-verify the feature end to end
├── checklists/requirements.md
└── tasks.md             # /speckit-tasks
```

No `contracts/` directory: the feature changes no interface. The C-ABI headers gain a comment line,
and R-7 covers that.

### Files changed (repository root = the library)

```text
spec/feature-catalogue.md            # §A–§C of data-model
spec/coverage-index.md               # §D
spec/behaviors-and-limitations.md    # §F.6 — L-094-1, after FR-018 files the issue
.specify/constitution.md             # §E.1–§E.7
CHANGELOG.md                         # §E.8
.specify/architecture.md             # §F.1 (same-line edits + Z-9 at the end)
.specify/api-contract.md             # §F.2
.specify/2i-capi.md                  # §F.3
.specify/2j-controlplane.md          # §F.3
.specify/2l-tap.md                   # §F.3
.specify/2m-pybind.md                # §F.3
README.md, docs/src/intro.md         # §F.4
brain/components/service.md          # §F.5
brain/components/dictionary.md       # §F.5
brain/components/nfr-and-tooling.md  # §F.5
src/**, include/** (53 files)        # §G — phase-implementer
tools/capi_freeze.sha256             # §G — phase-implementer
conanfile.py                         # §G — phase-implementer (licence in the SPDX commit; comment in a separate Refs #607 commit)
cmake/FixppPackaging.cmake           # §G — phase-implementer
```

Parent repo (private), after merge, at close-out: `REMAINING-WORK.md` row 1 and the Tier-0 tallies;
`phases/phase-4/issue-batches.md` places #614 and the FR-018 issue.

## Phases

The order is the canonical pipeline (`.specify/pipeline.md`).

1. **Gate A** on the bundle (`/gate-a 094-v1-tier0-bookkeeping`). data-model.md §E holds the exact
   amended clause text and §A–§G the exact row and surface text, so Codex reviews the words that will
   land, not a description of them.
2. **Tasks / analyze / checklist + audit.**
3. **Implement**:
   - Orchestrator: file the FR-018 issue (labels from `gh label list`) before writing L-094-1.
   - Docs (orchestrator): catalogue, coverage index, B&L, constitution, CHANGELOG, the §F surfaces.
     Run the census before and after (R-4), `brain_inventory.py` (R-5), and the R-12 recipe after.
   - Licence and build files (phase-implementer): run `spdx_mark.py` on the branch, change the
     `conanfile.py` and RPM licence fields, regenerate `tools/capi_freeze.sha256`, then run
     `tools/check_capi_freeze.sh` and the SPDX census. Then, as its own commit (`Refs #607`), the
     `conanfile.py` comment. The implementer gets the script, the dry-run output (R-6) and the expected
     diff shape (SC-005).
4. **Verify**:
   - one full Linux build (the comment edits compile), and the packaging lane's RPM build;
   - the full test suites of the modules the 37 inserts touch, and the witness binaries of the 18 rows
     (filters from R-3's census), with the §H map filled from the assertions;
   - the C-ABI freeze, symbol diff and occupancy checks;
   - line-citation `--range` and `--shift-audit` (first on a scratch commit of the docs edits, R-6),
     and the comment-claims check;
   - the R-12 recipe re-run, with the count per disposition class;
   - `check_bl_delta.py` (the branch touches the live B&L file), brain freshness, the external sweep;
   - the #614 snapshot.
5. **Gate B**, then the PR (titled per FR-003c, body per FR-019), labels and merge (owner), then
   close-out. The last commit before merge flips the SIR, Status line and CHANGELOG heading.

## Risks

- **`__LINE__` motion.**
  - The 37 inserted lines shift `__LINE__` in those files.
  - Measured 2026-10-22: none of them uses `__LINE__`, `std::source_location` or a `FIXPP_LOG_` macro;
    7 contain plain `assert(`.
  - No test pins `<basename>:<digits>` for any of the 37 files.
  - So no observable output moves. The full-module test run in verify is the backstop.
- **Line motion in the docs.** The new SIR shifts every line of `constitution.md`, and
  `tools/check_line_citations.py` carries fixture strings naming constitution lines. The shift audit
  runs on a scratch commit first (R-6). Every other docs edit is same-line.
- **Conan cold run, python-wheel fleet, ccache.** Accepted by the owner (FR-013a). The PR body says so
  (FR-019).
- **A closing keyword in a negation.** FR-019 bans it next to #607, #614 and the FR-018 issue, in the
  body and in commit messages. `closingIssuesReferences` is read before merge.
- **Peer collation PR.** The 2026-10-22 cloud batch may touch `conanfile.py` (B58/SWIG). The FR-013b
  comment is the owner's agreed text for that change, so whichever merges second rebases a small
  conflict in the same comment. The peer was told this feature adds no ctest entries.

## Complexity Tracking

No constitutional violation needs tracking. The first draft recorded a §XX.2 deviation (the amendment
riding a feature branch); FR-003c's PR title removes it.
