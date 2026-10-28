# Implementation Plan: Typed app-message tail — evidence pass for the 31 FIX Latest rows

**Branch**: `096-fix-latest-tail-evidence` | **Date**: 2026-10-28 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/096-fix-latest-tail-evidence/spec.md`

## Gate A

(Filled by `/gate-a`.)

## Summary

Close A-035..A-065 on 095's offline bar on both typed tiers. Tests only, plus records:
- move 095's dictionary-independent witness machinery into `tests/support/tail_rows_witness.hpp`
  (byte-identical bodies) and add one walk status, `wrapped_group` (research R-2);
- 31 v50sp2 witnesses, their census and a golden field-leg check in a new TU of the existing
  `test_077_allversions_builder_roundtrip` binary (R-3);
- an independent Orchestra required-walk, 31 vlatest witnesses, their census, its known positives
  (golden field leg, 095's deficient frames under the Orchestra dictionary, 35=i, synthetic
  fragments) and the cross-dictionary differential (35=W as its measured positive) in a new TU of the
  existing `test_077_allversions_builder_roundtrip_vlatest` binary (R-4..R-6);
- the constitution v4.2 amendment (§XVIII.2, §XVIII.7), then the catalogue, coverage-index and B&L
  edits (data-model §E, §F).

No production code, no codegen change, no new executable, no live cell.

## Technical Context

**Language/Version**: C++23 (tests), Bash/awk (records checks)
**Primary Dependencies**: GoogleTest, pugixml (both already linked by the v50sp2 binary; pugixml added
to the vlatest binary), `fixpp_wire`, `fixpp_dictionary` (`XmlLoader`, `OrchestraLoader`),
`fixpp::builders::v50sp2`, `fixpp::builders::vlatest`
**Storage**: N/A — inputs are the vendored dictionaries and `tools/quickfix_required_golden/golden.csv`
**Testing**: two existing standalone gtest binaries; ctest label `096` added to both
**Target Platform**: every CI platform that builds the two binaries (the vlatest one only under
`FIXPP_CODEGEN_FIX_LATEST`, default ON)
**Project Type**: library — test and documentation change
**Performance Goals**: none; the vlatest TU's compile peak RSS is measured (R-9, stop above 8 GiB)
**Constraints**: FR-006 (no recorded results), FR-012 (no production change), the orchestrator
writes no library code (the implementer writes every test and CMake line)
**Scale/Scope**: 31 rows, 62 witnesses, two TUs, one shared header, one CMake file, four record files

## Constitution Check

| Clause | Status |
|---|---|
| `[const §I.3]` | The feature removes 31 rows from the open set; SC-004 re-derives it. |
| `[const §VI.2]` | 29 Spec refs corrected to the v4.1 message-level form; data-model §E's script checks all 31. |
| `[const §VI.4]` | Each tail MsgType keeps its existing coverage-index row and gains its witnesses; the superseded scope text is commented in place (FR-013). |
| `[const §VI.5]` | spec Normative References lists section entries and the message-level refs. |
| `[const §VI.6]` | (a) this bundle; (b) the witnesses; (c) Gate B; (d) Gate A runs (non-trivial: a new walker and an amendment). |
| `[const §VII.3]`/`§VII.4` | Tests-only feature; each witness and check is shown RED by its W6 arm before it is trusted. |
| `[const §VII.8]` | No new executable: both TUs join existing standalone binaries; selected by `ctest -L 096`. |
| `[const §IX.4]` | clang-tidy/clang-format on the changed test files at verify. |
| `[const §XVII.7]` | Local full build + ctest of `linux-clang-debug` at the final head. |
| `[const §XVIII.2]`, `§XVIII.4`, `§XVIII.7` | Amended (FR-010; data-model §F). Without it the flips contradict §XVIII.2. |
| `[const §XX.1]`–`§XX.4` | Amendment first in commit order; MINOR (R-8); PR titled in §XX.2 form; owner sign-off before the RATIFIED flip. |

No violation to justify.

## Project Structure

### Documentation (this feature)

```text
specs/096-fix-latest-tail-evidence/
├── spec.md
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── contracts/witness-contract.md
├── checklists/requirements.md
├── expected-ctest-096.txt     # written by the implementer (P0)
└── tasks.md                   # /speckit-tasks
```

### Files changed

```text
tests/support/tail_rows_witness.hpp                    NEW  — moved 095 machinery (W0)
tests/session/test_095_tail_rows_roundtrip.cpp         MOD  — the move only
tests/session/test_096_tail_rows_roundtrip.cpp         NEW  — TailRows096, TailRows096Census
tests/session/test_096_tail_rows_roundtrip_vlatest.cpp NEW  — OrchestraWalker, TailRows096Latest,
                                                              TailRows096LatestCensus, TailRows096Differential
tests/session/CMakeLists.txt                           MOD  — two sources added, pugixml + golden
                                                              define on the vlatest target, label 096
.specify/constitution.md                               MOD  — v4.2 (data-model §F)
spec/feature-catalogue.md                              MOD  — 31 rows (data-model §E)
spec/coverage-index.md                                 MOD  — 31 gap notes + 3 comments
spec/behaviors-and-limitations.md                      MOD  — L-069-1 widened (+ any exposed row)
```

## Phases

- **P0 — setup.** `expected-ctest-096.txt`; baseline: both binaries build and pass at the branch's
  base; record the vlatest TU's compile RSS method (R-9).
- **P1 — the move (W0).** Header created, 095's TU forwards; build; 095's suites pass; quickstart §1
  diff empty. Commit alone.
- **P2 — v50sp2 leg.** The 096 v50sp2 TU: literal table, census, golden field-leg check, then the 31
  witnesses. Census and golden check first, RED against an empty witness set (the census reports 31
  multiplicity-0 findings), then witnesses until GREEN. W6 v50sp2 arms.
- **P3 — vlatest leg.** `OrchestraWalker` with its synthetic-fragment and golden checks first, then the
  deficient frames and 35=i, then the census (RED on an empty set), then the 31 witnesses. W6 vlatest
  arms.
- **P4 — differential.** `KnownDifference_MarketDataSnapshotFullRefresh` and `SyntheticPairs` first,
  then `TailMsgTypesAgree`. A tail difference stops the implementer (FR-012).
- **P5 — amendment.** Constitution v4.2, PENDING (orchestrator; `.specify/*.md` is documentation).
  Scoped Codex Gate A round on §XVIII.2/§XVIII.7.
- **P6 — records.** Catalogue, coverage index, B&L (orchestrator), after P5; data-model §E's script
  RED before, GREEN after; quickstart §5.
- **P7 — verify, Gate B, owner sign-off, RATIFIED flip last, PR, labels, merge.**

The orchestrator writes P5/P6 (Markdown records). Every `.cpp`, `.hpp`, CMake and `.txt` change in
P0–P4 is the implementer's.

## Risks

- **The real walk disagrees with the scratch measurement** (a tail MsgType reaches a fail-loud
  status, or the differential finds a tail difference): stop rule, owner ruling, FR-012's procedure.
- **095's frames parse differently under the Orchestra dictionary** (another delimiter, another split):
  the implementer stops at P3; the frame is not adapted silently.
- **vlatest TU compile cost**: measured alone at P3 before the full build; above 8 GiB peak RSS the
  implementer stops (split the TU by row range as the fallback, orchestrator's call).
- **The move changes behaviour**: guarded by quickstart §1's checker (with its positive control) and
  095's suites.

## Complexity Tracking

None.
