# Implementation Plan: Fold the Flat Per-Instance Cap Loop into the Nesting-Aware Traversal

**Branch**: `085-fold-flat-cap-loop` | **Date**: 2026-08-03 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/085-fold-flat-cap-loop/spec.md`

**Closes**: fixpp#214 (L-063-4 leg 2). **Files**: fixpp#220 (dict-free trailing-field cap false positive — deliberately not repaired here).

## Summary

`OffsetTable::group()` answers two questions about a repeating group — where its extent ends, and whether any instance breaches `max_group_entries_per_instance` — using **two walks over the same entries**. The first, `consume_group_extent`, is nesting-aware and *already applies the cap* (`:521-524`). The second (`:584-594`) re-walks the same range with a flat, wire-derived boundary test and applies the same cap again.

Phase 0 re-verified (per A-001) that on the dictionary path the second walk is **unreachable as an error return**: both walks derive `delim` from the same entry, the flat partition strictly refines the nesting-aware one, and the nesting-aware walk returns first on breach. Removal is therefore a no-op on observable behaviour.

It cannot simply be deleted: the **dict-free** branch never calls `consume_group_extent`, so the loop is that path's only cap enforcement. Per `/clarify` Q1 the loop is relocated **verbatim** into the `else` branch. `consume_group_extent` is not touched.

Net change: one loop deleted from the dictionary branch, the same lines moved into the dict-free branch, one justification comment added, one new test (plus its bracketing companion and a mutation transcript), one limitation row, one stale coverage waiver repaired.

## Technical Context

**Language/Version**: C++23 (per Article II)

**Primary Dependencies**: none added. Change is confined to `src/wire/offset_table.cpp`.

**Storage**: N/A

**Testing**: GoogleTest; new cases join the existing `offset_table_test.cpp` grouped bucket per Article VII §8 (no new executable). Selected via `ctest -L wire`.

**Target Platform**: Linux/Clang + Linux/GCC (Tier 1), Windows/MSVC (Tier 2). No platform-specific construct is introduced.

**Project Type**: C++ library (FIX engine), wire layer.

**Performance Goals**: no regression within Article VIII §2's ±5% budget on `BM_TypedReadGroup_{Flat2,ModeC2,ModeC8}`. The change removes an `O(extent)` walk from a path reached on every group materialization (`group_slices_status:667`); a null delta is a pass (A-004).

**Constraints**: zero allocation (Article VIII §5, FR-009); no public/exported surface change (FR-008); byte-identical dict-free behaviour (FR-003).

**Scale/Scope**: ~12 source lines relocated, ~10 added (comment + test scaffolding), 0 header changes. Ten shipped dictionaries in the regression surface.

**No NEEDS CLARIFICATION remain.** The five that existed were resolved in the `/speckit-clarify` session of 2026-08-03; A-001's re-verification and the R-2 severity correction closed the remainder during Phase 0.

## Constitution Check

*GATE: evaluated before Phase 0, re-evaluated after Phase 1. Both PASS.*

| Article | Gate | Status | Basis |
|---|---|---|---|
| VII §3 | TDD — failing test before implementation | **PASS (planned)** | The dict-free cap pin is written and observed RED before the relocation; FR-005a(i) |
| VII §4 | No code without a test | **PASS** | The only behaviour-bearing line that moves is the cap check, pinned by the new test |
| VII §7 | Parser-touching ⇒ fuzz harness | **PASS** | No new parser code; existing `tests/fuzz/` harnesses cover `OffsetTable`. No new harness required |
| VII §8 | Grouped buckets, label selection | **PASS** | New cases join `offset_table_test.cpp`; `ctest -L wire` |
| VIII §2 | ±5% vs `bench/baselines/` | **PASS (planned)** | SC-006; no baseline update — this is not an intentional perf change |
| VIII §3 | No perf change without a bench in the same PR | **PASS (planned)** | Three existing `BM_TypedReadGroup_*` cases, numbers in the PR body |
| VIII §5 | Zero hot-path allocation | **PASS** | Removal only; FR-009 |
| IX §1 | ≥95% line / ≥85% branch; no silent uncovered error path | **PASS — improves** | The previously dead `:592` becomes covered for the first time (research.md R-3) |
| IX §2/§4 | Sanitizers + static analysis | **PASS (planned)** | SC-008; no new constructs |
| X | C-ABI contract | **N/A** | Untouched; FR-008/SC-007 |
| XVI §3 | `/clarify` mandatory for wire-layer | **PASS** | 5/5 session, 2026-08-03 |
| XVII | Gate A after `/plan` before `/tasks`; Gate B before merge | **PENDING** | Pipeline steps 4 and post-implement |

**Complexity Tracking: no entries.** This feature adds no abstraction, dependency, configuration surface or indirection. The one candidate — a shared cap helper — was considered and rejected at `/clarify` Q1 as an abstraction over a two-line comparison that would also make FR-003's byte-identical claim un-inspectable.

### Post-Phase-1 re-evaluation

Re-checked after `research.md`, `data-model.md`, `contracts/` and `quickstart.md` were written. **No gate moved.** Two Phase-1 findings *add scope without adding complexity*:

1. **R-2** corrected FR-003a's severity — the dict-free breach is unreachable under default `Config`. Affects the wording of fixpp#220 and the limitation row, not the design.
2. **R-3** found the coverage waiver at `tests/wire/offset_table_error_path_test.cpp:10-14` stale in two independent ways; it must be repaired rather than re-pointed. Adds one task; touches no `src/` file.

## Project Structure

### Documentation (this feature)

```text
specs/085-fold-flat-cap-loop/
├── plan.md                            # This file
├── spec.md                            # /speckit-specify + /speckit-clarify
├── research.md                        # Phase 0 — A-001 re-verification (R-1..R-7)
├── data-model.md                      # Phase 1 — entities reasoned over (E-1..E-4)
├── quickstart.md                      # Phase 1 — validation procedure
├── contracts/
│   └── group_cap_accounting.md        # Phase 1 — C-1..C-4
├── checklists/
│   └── requirements.md                # spec quality checklist
└── tasks.md                           # Phase 2 — NOT created by /speckit-plan
```

### Source Code (repository root)

```text
src/wire/
└── offset_table.cpp                   # THE ONLY src/ FILE TOUCHED
                                       #   group():553-582  — relocate the loop into the else branch
                                       #   group():~575     — add the C-1/FR-002 justification comment
                                       #   consume_group_extent:442-528 — UNTOUCHED (FR-001a)
                                       #   group_slices():712-733       — UNTOUCHED (leg 1, descoped)

include/fixpp/wire/
└── offset_table.hpp                   # UNTOUCHED — no signature, no Config, no default changes

tests/wire/
├── offset_table_test.cpp              # + dict-free cap pin, + bracketing companion
└── offset_table_error_path_test.cpp   # repair the stale coverage waiver (:10-14)

spec/
└── behaviors-and-limitations.md       # L-063-4 leg 2 DELIVERED; new row citing fixpp#220

bench/wire/
└── typed_read_group_bench.cpp         # UNTOUCHED — existing cases re-run, no new case
```

**Structure Decision**: Single-file source change in the existing `wire` module. No new files, directories, targets or test executables. Tests join the established `offset_table_test.cpp` bucket per Article VII §8; documentation lands in the two artifacts that already own these claims (`spec/behaviors-and-limitations.md` for the limitation, the test header for the coverage waiver).

## Phase 0 — Outline & Research

**Output**: [research.md](./research.md) — complete.

| ID | Finding |
|---|---|
| R-1 | **A-001 discharged.** All four steps of the redundancy argument re-derived at `c1564dd2`; all five degenerate exits checked. Argument holds |
| R-2 | **Spec correction.** The dict-free breach is unreachable under default `Config` (both bounds 4096, table clamped at `:326`). Config-dependent, not a default-path defect |
| R-3 | **The removed line is dead in the entire suite today**, and this feature makes its successor live. The waiver at `offset_table_error_path_test.cpp:10-14` is stale in two ways |
| R-4 | Delivered code shape: verbatim relocation; `consume_group_extent` untouched |
| R-5 | Test inventory — what stays green (2 cap pins, ~16 dict-free tests, 8 census/split pins) and what is new |
| R-6 | Gate obligations mapped to discharges |
| R-7 | Residual risk — C-1 is a standing invariant no test can guard |

## Phase 1 — Design & Contracts

**Outputs**: [data-model.md](./data-model.md), [contracts/group_cap_accounting.md](./contracts/group_cap_accounting.md), [quickstart.md](./quickstart.md) — complete.

**Entities** (E-1 group extent, E-2 the cap, E-3 the instance boundary rule, E-4 membership context): **none added, modified or removed.** The document exists so FR-008 can be confirmed without reading the diff.

**Contracts**:

| | Statement | Notable |
|---|---|---|
| **C-1** | `group()` and `consume_group_extent` MUST resolve `delim` from the same entry | **STANDING** invariant; no test can guard it — the failure mode is a correctly-never-firing cap ceasing to be correct. Carried as a source comment per FR-002 |
| **C-2** | Dictionary path performs exactly one traversal | Post-conditions all unchanged in value |
| **C-3** | Dict-free cap preserved **byte-identically** | Verbatim-move obligation; reduces FR-003 to a diff inspection |
| **C-4** | Explicit non-scope: `consume_group_extent`, the `group_slices()` splitter (leg 1), the reserve bound, delimiter resolution | Named so the boundary is visible, not inferred |

**Validation**: [quickstart.md](./quickstart.md) — three claims in order (nothing changed → cap still fires → no regression), with the mutation step called out as mandatory and non-skippable.

## Risks and mitigations

| Risk | Severity | Mitigation |
|---|---|---|
| A future feature re-points one delimiter source and silently breaks the redundancy premise | **High impact, low likelihood** | C-1 as a standing contract + source comment naming both sites and the consequence. Explicitly *not* mitigated by a test — see C-1's rationale |
| The relocation is not truly verbatim, changing dict-free behaviour | Medium | FR-001a + C-3's diff-reads-as-a-move obligation; ~16 existing dict-free tests |
| The new cap pin false-passes (never reaches the branch) | Medium | FR-005a(i) mutation transcript is mandatory; quickstart §2c states the stop condition |
| fixpp#220's row and the issue drift apart | Low | SC-010 pins the citation in both directions |
| Gate A challenges why one flat loop is removed and the adjacent one is not | Low | C-4 + A-002 carry 083's leg-1 descope evidence explicitly |
| Bench delta within noise read as "no evidence" | Low | A-004 and SC-006 pre-commit to a null result being a pass |

## Next step

**`/gate-a 085-fold-flat-cap-loop`** — per `.specify/pipeline.md` step 4, Gate A runs **after `/plan` and before `/tasks`**, with blockers resolved before task generation.
