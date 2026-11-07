# Implementation Plan: Dictionary index width and the MultiCharValue accessor type

**Branch**: `102-dict-width-multichar-type` | **Date**: 2026-11-07 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/102-dict-width-multichar-type/spec.md`

## Summary

Widen the dictionary records' side-table starts, counts and name offset to 32 bits and refuse a dictionary of more than
65535 components, so no loader value wraps or clamps (B84, #654); retire 098's 16-bit side-table refusal. Map
MultiCharValue to the generator's string kind, so its accessors and builder inputs are `std::string_view`, as for
MultipleStringValue and as QuickFIX types it (B67, #621). Decisions D1–D6 in [research.md](research.md); the C++ breaks
in [contracts/cpp-api-breaks.md](contracts/cpp-api-breaks.md).

## Technical Context

**Language/Version**: C++23 (clang, gcc, MSVC).

**Primary Dependencies**: none new.

**Storage**: N/A.

**Testing**: GoogleTest; the dictionary equivalence fixture without its wrap exclusions as the #654 oracle; decode and
builder round-trip cells for #621; goldens and SHA pins re-published by recipe.

**Target Platform**: Linux and Windows.

**Project Type**: C++ library: dictionary loaders, code generator, generated typed tiers.

**Performance Goals**: no load-time regression beyond the paired band (D6).

**Constraints**: C ABI and Python unchanged; every generated file without a MultiCharValue field byte-identical.

**Scale/Scope**: production `include/fixpp/dict/component_ref.hpp`, `group_ref.hpp`, `src/dictionary/xml_loader.cpp`,
`src/dictionary/orchestra_loader.cpp`, `src/dictionary/overlay_merge.{hpp,cpp}`, `tools/codegen/fixpp-codegen/gen_util.hpp`.
Republished: 003 v50sp2 golden, 076 vlatest golden, 078 v50sp2 and vlatest trees, two read-tier hashes, the census
table. Tests in `tests/dictionary/`, `tests/codegen/`, `tests/session/` (077 round-trips).

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Article | Gate | Disposition |
|---|---|---|
| I / VI | dictionaries and codegen fidelity | PASS: every vendored base's members correct; FIX's MultipleCharValue representable. |
| VII | RED first, mutation-proven | PASS planned (D4). |
| VIII | no regression | D6 paired load measurement. |
| IX | coverage, static | `/speckit-verify`; `tools/codegen/` is lint scope since #265. |
| X | ABI | PASS: C ABI unchanged (`fixpp_dict_t` opaque; SWIG exposes neither). The C++ breaks are pre-release and declared (contract). |
| XV.13 | hybrid codegen + runtime dictionary | unchanged. |
| XVII.1 | Gate A on public API | Gate A required (public C++ layout and generated types). |

**Initial gate: PASS. Post-design re-check: PASS.**

## Project Structure

### Documentation (this feature)

```text
specs/102-dict-width-multichar-type/
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── contracts/
│   └── cpp-api-breaks.md
├── checklists/
│   └── requirements.md
└── tasks.md            # /speckit-tasks
```

### Source Code (library root)

```text
include/fixpp/dict/component_ref.hpp      # D1 layout
include/fixpp/dict/group_ref.hpp          # D1 layout
src/dictionary/xml_loader.cpp             # D1 32-bit writes; component cap; counter
src/dictionary/orchestra_loader.cpp       # D1 same
src/dictionary/overlay_merge.{hpp,cpp}    # D2 retire the side-table check
tools/codegen/fixpp-codegen/gen_util.hpp  # D3 kind_of
specs/003-…/golden, specs/076-…/golden, specs/078-…/golden   # D5 republished
tests/dictionary/                         # ref_shape, equivalence fixture, overlay refusal/equivalence, cap cell
tests/codegen/                            # census mapped_kind, read-tier pins, decode cells
tests/session/                            # 077 round-trip assignments; builder round-trip cells
```

**Structure Decision**: existing layout.

## Phases (for /speckit-tasks)

1. **RED** — equivalence fixture with exclusions removed (RED at `RgstDistInstGrp`); name-offset comparison (RED on
   SP1/SP2/Latest); MultiCharValue decode and builder cells (RED: decode failure / compile-level shape check).
2. **#654** — D1 layout and loader writes; component cap with cell and control; D2 retirement.
3. **#621** — D3; regenerate and republish per D5; update the census and the 077 sources.
4. **Mutations** — each change reverted alone, its cell RED.
5. **Records** — B&L: L-098-4 and L-621-1 closed; B-098-1 amended (side-table refusal gone, component cap); a B-row
   for each C++ break; catalogue / coverage index.
6. **Verify** — targeted ctest, D6 paired run, MSVC build of the touched targets, `/speckit-verify`.

## Open for Gate A

1. D1's component cap versus widening the component index too.
2. D3's `std::string_view` versus a dedicated multi-character view type.
3. Whether retiring 098's side-table refusal (D2) leaves any overlay input whose merged size the loader should still
   bound.
