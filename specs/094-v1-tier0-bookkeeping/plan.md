# Implementation Plan: v1.0 Tier-0 bookkeeping

**Branch**: `094-v1-tier0-bookkeeping` | **Date**: 2026-10-22 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/094-v1-tier0-bookkeeping/spec.md`

## Gate A

- Round 1 applied 2026-10-22: Codex P1=3 P2=5 P3=1; Opus post-judging P1=3 P2=8 P3=14; rewrite addresses root causes RC-1, RC-2, RC-3 (+ owner rulings 2026-10-22). Reviews: research/reviews/codex_094-v1-tier0-bookkeeping_gate_a_review.md, research/reviews/opus_094-v1-tier0-bookkeeping_gate_a_adversarial_review.md.
- Round 2 applied 2026-10-22: Codex P1=2 P2=3 P3=0; Opus post-judging P1=2 P2=4 P3=8; rewrite addresses root causes RC-1, RC-2, RC-3 (+ owner rulings 2026-10-22, round 2). Reviews: research/reviews/codex_094-v1-tier0-bookkeeping_gate_a_2_review.md, research/reviews/opus_094-v1-tier0-bookkeeping_gate_a_2_adversarial_review.md.
- Round 3 (final review round of loop instance 1) 2026-10-22: Codex P1=2 P2=0 P3=0; Opus post-judging P1=1 P2=1 P3=3; loop exhausted after 2 rewrites. Owner (2026-10-22) chose "apply the literal fixes, re-run one round": fixes applied as a post-exhaustion apply (not a third rewrite), N3-1 and N3-2 owner-approved. Reviews: research/reviews/codex_094-v1-tier0-bookkeeping_gate_a_3_review.md, research/reviews/opus_094-v1-tier0-bookkeeping_gate_a_3_adversarial_review.md. A fresh Gate A loop instance follows.
- Loop instance 2, round 1, 2026-10-22: Codex P1=0 P2=0 P3=0; Opus post-judging P1=0 P2=0 P3=3, closed in 11c9a007. Converged. Reviews: research/reviews/codex_094-v1-tier0-bookkeeping_gate_a_4_review.md, research/reviews/opus_094-v1-tier0-bookkeeping_gate_a_4_adversarial_review.md. Record: decisions/speckit/094-v1-tier0-bookkeeping-gatea.md (parent repo).

### After convergence — which edits re-open Gate A

- **The rule (orchestrator, 2026-10-22, at `/speckit-analyze`).** `[const §XX.2]` requires "Codex
  Gate A review on every amendment", so Codex must have reviewed the clause text that ratifies.
  - If the owner changes any amended **clause** at the Art. XX §2 sign-off (data-model §E.3–§E.7:
    §IV.4, §VI.6, §XIV.1, §XIV.3, §XVIII.1, §XVIII.2), the changed text is amendment text no Codex
    round has seen. A scoped Codex Gate A round therefore runs on the changed lines only
    (`codex-tool.sh --mode review`, plus an Opus judge) before the RATIFIED flip (tasks.md T061, T063).
    It runs under the owner's own `/gate-a` invocation, because no agent starts Codex on its own
    initiative (`[const §XVII.4]`).
  - The Status line, the Sync Impact Report's status and review-tally wording, and the `CHANGELOG.md`
    status wording are **metadata**: they record where the process stands and amend no rule. A change
    to them needs only Gate B's re-check, with no Gate A round.
- **Applied once, at `/speckit-analyze` (F3), as a status-metadata edit after convergence.** data-model
  §E.1, §E.2 and §E.8 said Gate A was outstanding. They now read "Gate A converged 2026-10-22
  (decisions/speckit/094-v1-tier0-bookkeeping-gatea.md, parent repo (private)); Gate B pending;
  pending the owner's Article XX §2 sign-off", the SIR's review tally names instance 1 rounds 1–3 and
  instance 2 round 1, and the RATIFIED templates are re-anchored on the new PENDING wording. No clause
  text changed (§E.3–§E.7 are byte-identical), so by the rule above Gate A is not re-run.

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

### Round 1 — owner questions (all answered at round 2; none is open)

1. **NFR-002 and `[const §VI.6]`(b).** Answered (owner, 2026-10-22): §VI.6 gains a process-row
   clarification inside the v4.0 amendment (data-model §E.3a), and NFR-002 flips under it.
2. **`544-hot-path-zero-alloc-verify.md` assessed added lines only.** Answered by the Edge Cases rule
   (Opus round 2, N2-3), merged with answer 1: the orchestrator files one labelled issue at implement
   time and NFR-002's Verified cell names it (FR-020).
3. **No v1.0 cross-process tap route or non-C++ rotation route; coverage-index :552.** Informational,
   recorded as a note only: T-039, T-040 and T-041 already read `done`, so the :552 note has no
   tag-gate effect. Reported, not edited.
4. **Licence prose** (`README.md:11` badge, `README.md:25–26`, `NOTICE:7`, `LICENSE-COMMERCIAL.md:3`).
   Default applied: left as it is and reported. It is not package metadata.
5. **40 `Apache-2.0` files outside `src/`/`include/`.** Default applied: the orchestrator files one
   labelled follow-up issue at implement time (FR-021).
6. **TC-017's in-domain ending.** Answered (owner, 2026-10-22): two session-level cells are added
   (FR-022).

Observation, not a question: `tests/conformance/test_transport_interop.cpp` labels its DISABLED cells
`TC-001..TC-017` with a numbering that differs from the catalogue's TC rows (there, TC-012 is a
BodyLength/CheckSum case). No Verified cell cites them.

### Round 2 — what changed, by root cause

- **RC-1 (questions the rules already decide were parked with the owner):** every round-1 owner
  question is answered above, by an owner ruling or by the rule that decides it. NFR-002 closes under
  an amended §VI.6 (data-model §E.3a; six clauses now). The 544 lapse is an issue (FR-020). TC-017
  gets two session-level cells (FR-022).
- **RC-2 (§H was a partial map):** §H now lists every clause of the flipped Titles, including NFR-003's
  MSVC ASan and carve-out clauses, NFR-005's PMR clause, TC-014's and TC-017's negative clauses (as
  recipes with a seeded positive), and verify splits every Title at its clauses. NFR-013's Title is
  narrowed to "ASIO async I/O transport (Linux and Windows)", and the backend moves to Verified with
  its grep.
- **RC-3 (sweeps scoped by a directory or term list):** R-5's sweep is a `git grep` over every tracked
  file and finds `docs/prebuild.py`; R-12's terms gain `service[- ]mode|out-of-process` and §I.2 is
  dispositioned; the `conanfile.py` comment is reworded (owner) so it is true before and after B58;
  `ControlPlaneFactory`'s tier is stated once in `api-contract.md` (`:60`), with `:220` and `:249`
  agreeing.
- **P3s:** script paths resolve from the library root through the workspace root (quickstart §5); the
  NFR-002 parent path, the §E.5 "both" and the `brain/components/service.md` blockquote are fixed;
  "Every other docs edit is same-line" is corrected (Risks); "plan §Census" now reads research R-4;
  the RATIFIED-flip re-run cost is disclosed (Risks); the PR title carries `(v4.0)`.
- **Stale comment, P3, not edited:** `src/session/session.cpp`, `on_inbound_logon_sent_`, the
  `interpret_logon` refusal comment says the refusal is "with Logout in US4", while the code sends
  nothing. FR-022's initiator cell asserts what the code does. The comment stays: editing it would put
  a non-licence line into `src/` and break SC-005 and quickstart §3's licence-only diff filter.

### Round 2 — disagreements

- **The R-11 count (Opus: "50 → 52"): not copied.** The two `FsmMatrixWitness` refused-Logon cells
  leave TC-017's Tests cell, `EnumValidationLogonTest.InDomainEncryptMethod_EstablishesAsToday` joins
  it, and the two FR-022 cells do not exist until implement. So 49 cited tests exist today and 51 are
  cited once FR-022 lands (research R-11). *(Re-derived at Gate A round 3 with a written counting rule
  and the round-3 Tests cells: 51 exist today, 53 once FR-022 lands; research R-11 lists them.)*
- **The RED mutant's file (the round-2 brief said `src/session/session.cpp`):** the sticky 98≠0
  refusal is in `src/session/admin_messages.cpp`, `interpret_logon`, the `if (invalid_encrypt_method_seen)`
  return; `session.cpp`'s Logon arms only act on its result. FR-022 names the right file. This
  matches the Opus review ("delete the `invalid_encrypt_method_seen` return").
- No Opus-confirmed fix is declined.

### Round 3 — what changed, by root cause

- **RC-1 (a conditional left in a target text):** NFR-005's PMR clause is decided now, not "narrowed
  at verify": its Title names only the parse arena and MemoryStore's retrieve() snapshot, the two
  surfaces a cell asserts; both cells join its Tests cell, and Verified states the narrowing and the
  design facts (data-model §C, §H; FR-006). NFR-011's Tests cell names the two enabled
  `WireThreeArenaPinning` cells instead of the glob (N3-3).
- **RC-2 (the Constitution Check walked the articles the feature changes, not the articles its touched
  files implement):** §III.4 gets a row, recorded as a pre-existing deviation with one issue (FR-023);
  the Result line and Complexity Tracking are corrected. The FR-013b comment's "Restore in Phase 5."
  becomes "Restore: fixpp#<N-III4>." (N3-2, owner-approved).
- **N3-1 (owner-approved):** the §VI.6 sentence reads "per-feature records of the kind that obligation
  names, cited in the row, each showing it applied in full" (data-model §E.3a and its quotations).
- **Codex's remedy for C3-2 (fold §III.4 into v4.0, or restore the pins): not applied**, per the Opus
  judge.

## Summary

This is a governance and reconciliation feature with no behavioural code. Its changes are docs,
licence comments and two test cells. It:
- writes the owner's DECISION 1 into the catalogue (7 post-v1.0 drops plus a legend line) and into the
  constitution (**v3.0 → v4.0, MAJOR**: the service wrapper and the control-plane interface leave v1.0
  scope and become unscheduled post-1.0; §VI.6 says how a process row closes), with a `CHANGELOG.md`
  entry;
- records that `EngineConfig::control_plane_factory` is accepted and ignored, as a new live
  limitation L-094-1 and a new issue;
- flips 18 delivered rows to `done` as reconciliations, worded to what shipped, and splits io_uring
  into a new post-v1.0 row NFR-019;
- adds two session-level TC-017 test cells (FR-022), test-only, in `session_pure_tests`;
- marks every library source file `AGPL-3.0-or-later`, including the C-ABI freeze manifest and the
  Conan and RPM licence fields; and, in a separate commit, updates `conanfile.py`'s `tool_requires`
  comment for #607.

Every target cell and clause is written out in [data-model.md](./data-model.md). Every fact behind
them is measured in [research.md](./research.md).

## Technical Context

**Language/Version**: Markdown (catalogue, coverage index, B&L, constitution, CHANGELOG, design docs,
brain, README, docs). The only library source-file change is a comment line (C++23 / C headers), plus
one string and one comment in `conanfile.py` and one string in `cmake/FixppPackaging.cmake`. Two test
cells are added in two existing `tests/session/` files (FR-022).

**Primary Dependencies**: none added or changed.

**Storage**: N/A.

**Testing**: two new test cells for TC-017 (FR-022), in files already compiled into
`session_pure_tests`, so no ctest entry is added; each is proven RED. The other verifying tests of the
18 flipped rows run unchanged (FR-015). Instruments:
- `catalogue_census.py` (R-4)
- the R-12 sweep recipe (four `git grep` commands: (a) the dropped-scope terms and the io_uring
  grep, (b), (c))
- `tools/check_capi_freeze.sh`
- `tools/check_line_citations.py --range/--shift-audit`
- `"$W/.claude/scripts/check-comment-claims.py"`, where `W=$(git rev-parse --show-superproject-working-tree)`
  run from the library root (the workspace root; in a `fixpp-parallel` worktree, name the absolute
  root instead)
- `tools/brain_inventory.py`, and `docs/prebuild.py`'s `_status_header` (R-5)
- `"$W/.claude/scripts/check_bl_delta.py"` (the branch now touches the live B&L file)
- the brain freshness job's in-repo checks, and `"$W/research/G19-fix-fpml-iso20022/tools/brain-external-sweep.sh"`

**Target Platform**: unchanged. Comment-only source changes mean the library build output is identical (the FR-022 cells change only the `session_pure_tests` test binary), apart
from `__LINE__`-derived values shifting by one in the 37 files that gain a line (see Risks).

**Project Type**: C++23 library.

**Performance Goals / Constraints**: none. The bench gate is not engaged (no library code change).

**Scale/Scope**:
- 25 catalogue rows changed plus 1 added (NFR-019), and the legend line;
- coverage index: 9 lines edited (:405, :410–:413, :421, :435, :436, :652) and 1 paragraph added;
- constitution: 6 clauses (§IV.4, §VI.6, §XIV.1, §XIV.3, §XVIII.1, §XVIII.2), the Status line and the
  Sync Impact Report; `CHANGELOG.md`: 1 entry;
- live B&L: 1 new section with L-094-1;
- 11 other surfaces: `.specify/architecture.md`, `.specify/api-contract.md`, `.specify/2i-capi.md`,
  `.specify/2j-controlplane.md`, `.specify/2l-tap.md`, `.specify/2m-pybind.md`,
  `brain/components/{service,dictionary,nfr-and-tooling}.md`, `README.md`, `docs/src/intro.md`;
- 53 source files (one line each), `tools/capi_freeze.sha256`, `conanfile.py` (two commits) and
  `cmake/FixppPackaging.cmake`;
- 2 test cells in 2 existing test files (FR-022);
- 4 new fixpp issues (FR-018, FR-020, FR-021, FR-023).

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-checked after Phase 1 design.*

| Article | Applies | Disposition |
|---|---|---|
| §VI.6 (closing a `done` row) | yes | (a) 094 is the `/specify` artifact, cited in each /specify cell. (b) Every flipped behaviour row's tests run green at verify (FR-015), each clause mapped to an assertion (FR-015b); NFR-002, a process row, closes under (b) as amended in v4.0 (data-model §E.3a), on the two cited records, with the 544 lapse on record (FR-020). (c) Codex Gate B runs. (d) Gate A runs (R-1). |
| §VI.4 (coverage-index entry before a new OFFICIAL row) | yes | The NFR-019 supplemental lands in the same edit (data-model §D). |
| §VI.3 (`[impl]` rows noted as design choices) | yes | The NFR-019 supplemental says so. |
| §XX.1–§XX.3 (amendment) | yes | Amended in the same PR as the conflicting change, with the Sync Impact Report (data-model §E.1). The PR is titled `Constitution: amend §IV.4, §VI.6, §XIV.1, §XIV.3, §XVIII.1, §XVIII.2 — service wrapper post-1.0, process-row closure (v4.0) (+ 094 Tier-0 bookkeeping)` and its description states change, rationale and affected rows (FR-003c), so §XX.2's four steps are met. Codex Gate A runs, then owner sign-off; the SIR reads PENDING until both. |
| §XX.4 (classification) | yes | MAJOR, v3.0 → v4.0, with a `CHANGELOG.md` entry (R-2; owner, 2026-10-22). The §VI.6 clarification is a loosening and does not change it. |
| §XVIII.4 (roadmap change = amendment) | yes | The §XVIII.2 line is part of the amendment. |
| §XVIII.5 (no early shipping) | yes | Unchanged; it is why the amendment is MAJOR (R-2). |
| §XVI.6 (orchestrator does not implement) | yes | The 53 source files, `capi_freeze.sha256`, `conanfile.py`, `cmake/FixppPackaging.cmake` and the two FR-022 test cells go to `phase-implementer`. Docs (`spec/`, `specs/`, `.specify/*.md`, `brain/`, `CHANGELOG.md`, `README.md`, `docs/src/intro.md`) are governance and documentation text the orchestrator may write. |
| §X.7 / C-ABI freeze | yes | Comment-only header change: the manifest is regenerated and the symbol set is unchanged (R-7). No C-ABI version bump, because no ABI changes. |
| §III.4 (`tool_requires` pins `cmake` and `ninja`) | yes — pre-existing deviation, recorded | Not met on `main`, and not changed by 094: `629f2170` (2026-05-10) emptied `tool_requires`, so CMake and Ninja come from the host toolchain; no amendment, decision record, B&L row or issue dispositioned it before 094 (re-derive, against `9b176090` and before FR-023 files its issue: `git log -S'tool_requires = []' -- conanfile.py`; `grep -rn 'tool_requires\|\(^\|[^IVX]\)III §4\|§III\.4'` over `.specify/constitution.md`, `CHANGELOG.md`, both B&L files and `brain/` must find no hit outside §III.4 itself (known positive: §III.4's own line; the `[^IVX]` guard keeps `Article VIII §4` out); `gh issue list --state all --limit 1000 --json number,title,body` searched for `tool_requires`, with `swig>=4.2` as the known positive). 094 changes no tool provisioning; FR-013b rewords the comment above `tool_requires = []` only. fixpp#<N-III4> (FR-023) records it. Restoring the pins, or amending §III.4 to the host-provisioned model, is the owner's call and outside 094. |
| §V.1 / §V.4 (licence; vendored attribution) | yes | AGPL marking only; the relicense basis is R-8. No vendored file is in the set. NFR-010's commercial half is untouched (postponed). |
| §IX.1 (coverage) | n/a | No line of library code is added or made reachable (FR-022 adds test code only). The verify record says so per touched module. |
| §VIII (bench) | n/a | No library code change. |
| §XVII.7 (local pre-PR build gate) | yes | Before the PR opens, one FULL `ctest --preset linux-clang-debug --output-on-failure --no-tests=error` cycle runs on a committed tree, after the conan install, configure and build, as the LAST run of verify (tasks.md T049). The PR body's `local build: green on linux-clang-debug @ <sha>` line cites that run (T052). It runs last because a full `ctest` regenerates `_codegen` in the same build tree (`ninja -n` cannot see it), so the selected and witness runs go first and anything after it rebuilds first. `df -h /mnt/e` is read first, because the main checkout's `build/` lands on E:. `/mnt/c/temp/fixpp/.sandbox-lock` is read first too: the MSVC sandbox itself is not used, but the packaging entries compile consumer projects, so a full `ctest` is a build during a peer's MSVC window, and a lock held by a peer means asking the owner before running. The resource gate applies: every build asks the owner first. `pytest bindings/python/tests/` does not apply, because the change does not touch `bindings/python/`. |
| §XVII.8 (verify before Gate B) | yes | `/speckit-verify` runs. Its plan derives from this diff: docs-and-comment gates, the flipped rows' witness binaries, the two FR-022 cells and their RED proofs, one full build to prove the comment edits compile, and the packaging lane's RPM build (FR-013a). |

**Result:** PASS, with one pre-existing deviation recorded (§III.4, fixpp#<N-III4>); none is introduced by this feature.

## Project Structure

### Documentation (this feature)

```text
specs/094-v1-tier0-bookkeeping/
├── spec.md
├── plan.md              # this file
├── research.md          # R-1 … R-15
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
.specify/constitution.md             # §E.1–§E.7 (incl. §E.3a, Article VI §6)
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
tests/session/test_enum_validation_logon.cpp   # FR-022 acceptor cell — phase-implementer
tests/session/fsm_matrix_witness_test.cpp      # FR-022 initiator cell — phase-implementer
```

Parent repo (private), after merge, at close-out: `REMAINING-WORK.md` row 1 and the Tier-0 tallies;
`phases/phase-4/issue-batches.md` places #614 and the FR-018, FR-020, FR-021 and FR-023 issues.

## Phases

The order is the canonical pipeline (`.specify/pipeline.md`).

1. **Gate A** on the bundle (`/gate-a 094-v1-tier0-bookkeeping`). data-model.md §E holds the exact
   amended clause text and §A–§G the exact row and surface text, so Codex reviews the words that will
   land, not a description of them.
2. **Tasks / analyze / checklist + audit.**
3. **Implement**:
   - Orchestrator: file the FR-018 issue (labels from `gh label list`) before writing L-094-1, the
     FR-020 issue before writing NFR-002, the FR-021 issue, and the FR-023 issue before the FR-013b commit.
   - Docs (orchestrator): catalogue, coverage index, B&L, constitution, CHANGELOG, the §F surfaces.
     Run the census before and after (R-4), `brain_inventory.py` (R-5), and the R-12 recipe after.
   - Licence and build files (phase-implementer): run `spdx_mark.py` on the branch, change the
     `conanfile.py` and RPM licence fields, regenerate `tools/capi_freeze.sha256`, then run
     `tools/check_capi_freeze.sh` and the SPDX census. Then, as its own commit (`Refs #607`), the
     `conanfile.py` comment. The implementer gets the script, the dry-run output (R-6) and the expected
     diff shape (SC-005).
   - Test cells (phase-implementer): the two FR-022 cells, each proven RED in a scratch copy by
     deleting the `if (invalid_encrypt_method_seen)` return in `interpret_logon`
     (`src/session/admin_messages.cpp`).
4. **Verify**:
   - one full Linux build (the comment edits compile), and the packaging lane's RPM build;
   - the full test suites of the modules the 37 inserts touch, and the witness binaries of the 18 rows
     (the ctest entries of R-15's map, the filters from R-3's census), including the two FR-022 cells and their RED proofs, with the §H map
     filled from the assertions;
   - `docs/prebuild.py`'s `_status_header` on the branch's catalogue (R-5);
   - the C-ABI freeze, symbol diff and occupancy checks;
   - line-citation `--range` and `--shift-audit` (first on a scratch commit of the docs edits, R-6),
     and the comment-claims check;
   - the R-12 recipe re-run, with the count per disposition class;
   - `check_bl_delta.py` (the branch touches the live B&L file), brain freshness, the external sweep;
   - the #614 snapshot;
   - last, one full `ctest --preset linux-clang-debug --output-on-failure --no-tests=error` cycle on a
     committed tree (Constitution Check, row §XVII.7).
5. **Gate B**, then the PR (titled per FR-003c, body per FR-019), labels and merge (owner), then
   close-out. The last commit before merge flips the SIR, Status line and CHANGELOG heading; the
   owner's amendment sign-off is taken before the gate labels go on, so that flip is pushed once,
   last (Risks).

## Risks

- **`__LINE__` motion.**
  - The 37 inserted lines shift `__LINE__` in those files.
  - Measured 2026-10-22: none of them uses `__LINE__`, `std::source_location` or a `FIXPP_LOG_` macro;
    7 contain plain `assert(`.
  - No test pins `<basename>:<digits>` for any of the 37 files.
  - So no observable output moves. The full-module test run in verify is the backstop.
- **Line motion in the docs.** The new SIR shifts every line of `constitution.md`, and
  `tools/check_line_citations.py` carries fixture strings naming constitution lines. The shift audit
  runs on a scratch commit first (R-6). The data-model §F edits are same-line; the edits that add lines
  are the NFR-019 catalogue row, the coverage-index NFR-019 paragraph (data-model §D orders it after
  the same-line edits, and anchors every edit by text), the B&L section, Z-9 and the CHANGELOG entry.
  No tracked file outside `specs/` cites `feature-catalogue.md:<n>` or `coverage-index.md:<n>`
  (R-6), so this is harmless once stated.
- **Conan cold run, python-wheel fleet, ccache.** Accepted by the owner (FR-013a). The PR body says so
  (FR-019).
- **A closing keyword in a negation.** None may stand next to #607, #612, #614 or the four new issues
  (FR-018, FR-020, FR-021, FR-023), in the body or in commit messages (FR-019). `closingIssuesReferences` is read before merge.
- **Peer collation PR.** The 2026-10-22 cloud batch may touch `conanfile.py` (B58/SWIG). The FR-013b
  comment is the owner's text (reworded at Gate A round 2 so it is true before and after B58), so
  whichever merges second rebases a small conflict in the same comment. The peer was told this feature
  adds no ctest entries, which stays true: the FR-022 cells join the existing `session_pure_tests`.
- **The RATIFIED flip re-runs the whole matrix.** FR-003a makes the flip the last commit before merge.
  The three MAJOR precedents were docs-only PRs, so their flip push was cheap. 094's diff contains
  `include/fix/c_api/*` and `conanfile.py`, which match tier1's `PY_RE`, and that filter is matched
  against `git diff --name-only <base.sha> <head.sha>`, the whole PR and not the pushed delta. So a
  docs-only flip pushed after the gate labels re-runs every tier and the python-wheel fleet. To avoid
  paying it twice, take the owner's amendment sign-off before the gate labels go on, so the order is
  flip → body → labels → push last.

## Complexity Tracking

No constitutional violation is introduced. One pre-existing deviation is recorded, not cured: §III.4 (Constitution Check; FR-023). The first draft recorded a §XX.2 deviation (the amendment
riding a feature branch); FR-003c's PR title removes it.
