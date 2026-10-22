# Implementation Plan: v1.0 Tier-0 bookkeeping

**Branch**: `094-v1-tier0-bookkeeping` | **Date**: 2026-10-22 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/094-v1-tier0-bookkeeping/spec.md`

## Summary

This is a governance and reconciliation feature with no behavioural code. It:
- writes the owner's DECISION 1 into the catalogue (7 post-v1.0 drops plus a legend line) and into the
  constitution (v3.0 → v3.1: the service wrapper leaves v1.0 scope and becomes unscheduled post-1.0);
- flips 18 delivered rows to `done` as reconciliations, worded to what shipped, and splits io_uring
  into a new post-v1.0 row NFR-019;
- marks every library source file `AGPL-3.0-or-later`, including the C-ABI freeze manifest and the
  `conanfile.py` licence field.

Every target cell is written out in [data-model.md](./data-model.md). Every fact behind them is
measured in [research.md](./research.md).

## Technical Context

**Language/Version**: Markdown (catalogue, coverage index, constitution, design docs, brain). The only
source-file change is a comment line (C++23 / C headers), plus one string in `conanfile.py`.

**Primary Dependencies**: none added or changed.

**Storage**: N/A.

**Testing**: no new tests. The verifying tests of the 18 flipped rows run unchanged (FR-015).
Instruments:
- `catalogue_census.py` (R-4)
- `tools/check_capi_freeze.sh`
- `tools/check_line_citations.py --range/--shift-audit`
- `.claude/scripts/check-comment-claims.py`
- `tools/brain_inventory.py`
- the brain freshness job's in-repo checks, and the parent's `tools/brain-external-sweep.sh`

**Target Platform**: unchanged. Comment-only source changes mean the build output is identical, apart
from `__LINE__`-derived values shifting by one in the 37 files that gain a line (see Risks).

**Project Type**: C++23 library.

**Performance Goals / Constraints**: none. The bench gate is not engaged (no code change).

**Scale/Scope**:
- 25 catalogue rows changed plus 1 added, and the coverage index;
- 6 constitution clauses plus the Sync Impact Report;
- 3 design/brain surfaces;
- 53 source files (one line each), `tools/capi_freeze.sha256` and `conanfile.py`.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-checked after Phase 1 design.*

| Article | Applies | Disposition |
|---|---|---|
| §VI.6 (closing a `done` row) | yes | (a) 094 is the `/specify` artifact, cited in each /specify cell. (b) Every flipped row's tests run green at verify (FR-015). (c) Codex Gate B runs. (d) Gate A runs (R-1). |
| §VI.4 (coverage-index entry before a new OFFICIAL row) | yes | The NFR-019 supplemental lands in the same edit (data-model §D). |
| §VI.3 (`[impl]` rows noted as design choices) | yes | The NFR-019 supplemental says so. |
| §XX.1–§XX.3 (amendment) | yes | Amended in the same PR as the conflicting change, with the Sync Impact Report. Codex Gate A runs, and the owner signs off. **Deviation recorded:** the amendment rides a feature branch, not a standalone `Constitution: amend …` PR (precedents 035/043/069). |
| §XX.4 (classification) | yes | Proposed MINOR, argued in R-2. Gate A decides. If it lands MAJOR: v4.0 plus a `CHANGELOG.md` entry. |
| §XVIII.4 (roadmap change = amendment) | yes | The §XVIII.2 line is part of the amendment. |
| §XVI.6 (orchestrator does not implement) | yes | The 53 source files, `capi_freeze.sha256` and `conanfile.py` go to `phase-implementer`. Docs (`spec/`, `specs/`, `.specify/*.md`, `brain/`) are governance text the orchestrator may write. |
| §X.7 / C-ABI freeze | yes | Comment-only header change: the manifest is regenerated and the symbol set is unchanged (R-7). No C-ABI version bump, because no ABI changes. |
| §V.1 / §V.4 (licence; vendored attribution) | yes | AGPL marking only. No vendored file is in the set (R-8). NFR-010's commercial half is untouched (postponed). |
| §IX.1 (coverage) | n/a | No line of code is added or made reachable. The verify record says so per touched module. |
| §VIII (bench) | n/a | No code change. |
| §XVII.8 (verify before Gate B) | yes | `/speckit-verify` runs. Its plan derives from this diff: docs-and-comment gates, plus the flipped rows' witness binaries and one full build to prove the comment edits compile. |

**Result:** PASS. The one recorded deviation is the §XX.2 PR form.

## Project Structure

### Documentation (this feature)

```text
specs/094-v1-tier0-bookkeeping/
├── spec.md
├── plan.md              # this file
├── research.md          # R-1 … R-12
├── data-model.md        # exact target text of every changed row / clause
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
.specify/constitution.md             # §E
.specify/architecture.md             # §F
.specify/2j-controlplane.md          # §F
brain/components/service.md          # §F
src/**, include/** (53 files)        # §G — phase-implementer
tools/capi_freeze.sha256             # §G — phase-implementer
conanfile.py                         # §G — phase-implementer
CHANGELOG.md                         # only if Gate A reclassifies the amendment MAJOR
```

Parent repo (after merge, close-out): `REMAINING-WORK.md` row 1 and the Tier-0 tallies, plus
`phases/phase-4/issue-batches.md` if any follow-up is filed.

## Phases

The order is the canonical pipeline (`.specify/pipeline.md`).

1. **Gate A** on the bundle (`/gate-a 094-v1-tier0-bookkeeping`). data-model.md §E holds the exact
   amended clause text and §A–§D the exact row text, so Codex reviews the words that will land, not a
   description of them.
2. **Tasks / analyze / checklist + audit.**
3. **Implement**:
   - Docs (orchestrator): catalogue, coverage index, constitution, the three surfaces. Run the census
     before and after (R-4) and `brain_inventory.py` (R-5).
   - Licence marking (phase-implementer): run `spdx_mark.py` on the branch, flip the `conanfile.py`
     licence line, regenerate `tools/capi_freeze.sha256`, then run `tools/check_capi_freeze.sh` and the
     SPDX census. The implementer gets the script, the dry-run output (R-6) and the expected diff shape
     (SC-005).
4. **Verify**:
   - one full Linux build (the comment edits compile);
   - the full test suites of the modules the 37 inserts touch, and the witness binaries of the 18 rows
     (filters from R-3's census);
   - the C-ABI freeze, symbol diff and occupancy checks;
   - line-citation `--range` and `--shift-audit`, and the comment-claims check;
   - brain freshness, plus the external sweep.
5. **Gate B**, then PR, labels and merge (owner), then close-out.

## Risks

- **`__LINE__` motion.**
  - The 37 inserted lines shift `__LINE__` in those files.
  - Measured 2026-10-22: none of them uses `__LINE__`, `std::source_location` or a `FIXPP_LOG_` macro;
    7 contain plain `assert(`.
  - No test pins `<basename>:<digits>` for any of the 37 files.
  - So no observable output moves. The full-module test run in verify is the backstop.
- **Conan cold run.** Accepted by the owner (FR-013a). The PR body says so.
- **Gate A reclassifies MAJOR.** Mechanical: v4.0 plus a `CHANGELOG.md` entry, no design change.
- **Peer collation PR.** The 2026-10-22 cloud batch may touch `conanfile.py` (B58/SWIG). Whichever
  merges second rebases a one-line conflict. The peer was told this feature adds no ctest entries.

## Complexity Tracking

| Violation | Why needed | Simpler alternative rejected because |
|---|---|---|
| Amendment rides a feature branch (§XX.2 form) | The owner asked for one docs PR carrying DECISION 1 and the flips together. | A standalone amendment PR would leave the catalogue drops waiting on it, or contradicting the constitution in between (§XX.1). |
