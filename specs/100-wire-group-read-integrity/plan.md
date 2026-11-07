# Implementation Plan: Wire group-read integrity

**Branch**: `100-wire-group-read-integrity` | **Date**: 2026-11-07 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/100-wire-group-read-integrity/spec.md`

## Summary

Four inbound read-path defects (B81: fixpp#645, #644, #647; B42: #541) end in a present field or group read as absent or
empty, or in a `std::bad_alloc` leaving `extern "C"`. The plan gives each one observable ending (research D1–D7):

- the index build fails at the probe cap (`wire_offset_table_full`);
- the group read result carries `is_group` and a failure flag on both read paths, and the typed top-level read passes it;
- a zero-count group is a group when the dictionary's delimiter oracle says so (the validator's own question);
- both C-ABI getters catch the cursor-shell allocation and answer from one table of codes (contract
  `contracts/capi-group-getters.md`).

C-ABI 1.15, BREAKING (`[const §X.7]`); no code, symbol or struct added. One finding beyond the four issues: a group read
stopped by `wire_group_too_large` is also read as empty (research D5), folded in pending Gate A.

## Technical Context

**Language/Version**: C++23 (clang, gcc, MSVC per Article II).

**Primary Dependencies**: none new.

**Storage**: N/A.

**Testing**: GoogleTest; RED-first per issue, a mutation per production change (research D9).

**Target Platform**: Linux (clang, gcc) and Windows (MSVC).

**Project Type**: C++ library: wire codec, C ABI.

**Performance Goals**: no measurable change on the read path. D2 adds one count parse plus one delimiter-oracle call, only
on a group read whose next field is not a member; D3 adds a by-value `bool`; D1 changes only the cap-reach arm.

**Constraints**: zero allocation per message on the inbound path (`[const §XV.1]`); every touched wire function stays
`noexcept`; result structs stay trivially copyable; `group_span` stays within its asserted 16 bytes.

**Scale/Scope**: production `src/wire/offset_table.cpp`, `include/fixpp/wire/offset_table.hpp`,
`include/fixpp/wire/parser.hpp`, `include/fixpp/wire/group_view.hpp` (doc), `src/capi/message_read.cpp`,
`include/fix/c_api/message.h` (doc), `include/fix/c_api/version.h`. No codegen or generated-text change. Tests in
`tests/wire/`, `tests/capi/`, `tests/session/`. Records: B&L, catalogue, coverage index.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Article | Gate | Disposition |
|---|---|---|
| I / VI — scope, spec coverage | core tag-value FIX | PASS. #647 brings the C-ABI read in line with FIX's zero count and TC-018 (ruling note, parent repo). |
| VII — testing | RED first, mutation-proven | PASS. Each cell RED on `fa778a8e`, each production change mutated (D9). |
| VIII — performance | no regression | PASS by construction (Technical Context); the read path's bench set runs at verify. |
| IX — coverage, sanitizers, static | touched modules ≥ 95/85 or per-line assessment | PASS planned: `/speckit-verify`; ASan on the cursor-shell path. |
| **X — ABI** | §X.7: a breaking change before the first release bumps MINOR, declared BREAKING | **1.15 BREAKING** (D8). Gate A confirms the classification. |
| XI — concurrency | — | N/A, no concurrency surface. |
| **XV.1** — no per-message heap | inbound path | PASS: no new allocation; the cursor shell keeps its arena allocation, now caught. |
| XV — no silent success | fail-closed endings | PASS: this feature removes four silent endings and one escaping exception. |
| XVII — gates | Gate A on public C-ABI behaviour | Gate A required: §XVII.1's public-API trigger fires. |

**Initial gate: PASS.** **Post-design re-check: PASS**; Article X is a Gate A confirmation item, not a violation.

## Project Structure

### Documentation (this feature)

```text
specs/100-wire-group-read-integrity/
├── plan.md
├── research.md          # survey + decisions D1–D9
├── data-model.md        # result structs, cache row, status
├── quickstart.md        # targeted runs
├── contracts/
│   └── capi-group-getters.md   # C-ABI 1.15 endings, typed endings, parse ending
├── checklists/
│   └── requirements.md
└── tasks.md             # /speckit-tasks
```

### Source Code (library root)

```text
include/fixpp/wire/offset_table.hpp   # group_slices_result / nested_slices_result: + is_group; group_span: + is_group
src/wire/offset_table.cpp             # D1 probe-cap arm; D2 zero-count branch in group(); D3 flag; D5 group_too_large;
                                      # D7 resolve_nested_result mode (c)
include/fixpp/wire/parser.hpp         # D6 MessageView::group<>() passes the flag
include/fixpp/wire/group_view.hpp     # doc: alloc_failed() = the read did not complete
src/capi/message_read.cpp             # D4 both getters; catch around the cursor shell
include/fix/c_api/message.h           # CA-010 doc block + per-function codes
include/fix/c_api/version.h           # 1.15 BREAKING
tests/wire/                           # D1, D2, D3, D5 cells; the flipped hostile-input cell
tests/capi/                           # D4 table cells; b28-repro port; the flipped Q33 cell
tests/session/                        # D1 inbound ending (or the stated substitute)
```

**Structure Decision**: existing layout; no new module or target, except new test files registered in the existing test
binaries.

## Phases (for /speckit-tasks)

1. **RED** — one cell per issue and per D5/D7 on today's tree, plus controls; record each RED reason.
2. **Wire** — D1, D2, D3, D5, D7 in `offset_table`; wire cells GREEN.
3. **Typed** — D6; typed cells GREEN.
4. **C ABI** — D4, D8; C-ABI cells GREEN; flipped cells rewritten with the superseding requirement named.
5. **Mutations** — each production change reverted alone, its cell RED.
6. **Records** — B&L (L-458-1, L-073-1, L-051-3 closed; L-093-17 retired; L-093-5, L-065-2 amended; a B-row for the
   1.15 endings), catalogue/coverage-index entries for the delivering witnesses, C-ABI history comment.
7. **Verify** — targeted ctest, ASan/UBSan on `capi_tests`, MSVC build of the touched targets, `/speckit-verify`.

## Complexity Tracking

| Item | Why | Simpler alternative rejected because |
|---|---|---|
| D5 folds `wire_group_too_large` into the failure flag | same silent-empty class on the same lines | filing it separately leaves a known silent loss in code this feature rewrites; fallback if Gate A narrows scope |
| FR-003 changes the nested getter's scalar answer | one rule for both getters (B42's "consistent across both getters") | keeping `OK`/0 leaves a scalar indistinguishable from a zero-count group on the nested getter |

## Open for Gate A

1. D2's oracle: `group_first_field` with its context-miss fallback (validator agreement) versus `group_first_field_exact`.
2. D5's scope and the widened meaning of `group_view::alloc_failed()`.
3. D8: 1.15 BREAKING versus a non-breaking classification of a fix to a misreport.
4. D9 #645 inbound cell: whether a real collision frame can reach the session rig under the seed hook.
