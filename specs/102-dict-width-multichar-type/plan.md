# Implementation Plan: Dictionary index width and the MultiCharValue accessor type

**Branch**: `102-dict-width-multichar-type` | **Date**: 2026-11-07 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/102-dict-width-multichar-type/spec.md`

## Summary

Widen the dictionary records' side-table starts, counts and name offset, and the whole component identity, to 32 bits,
with no refusal; read Orchestra component and group ids in the schema's domain; retire 098's 16-bit side-table refusal;
do every run reader's bounds arithmetic so it cannot wrap (B84, #654). Map MultiCharValue to the generator's string
kind, so its accessors and builder inputs are `std::string_view`, as for MultipleStringValue and as QuickFIX types it
(B67, #621). Decisions D1–D8 in [research.md](research.md); the C++ breaks in
[contracts/cpp-api-breaks.md](contracts/cpp-api-breaks.md).

## Technical Context

**Language/Version**: C++23 (clang, gcc, MSVC).

**Primary Dependencies**: none new.

**Storage**: N/A.

**Testing**: GoogleTest; the dictionary equivalence fixture, repaired and without its wrap exclusions, as the #654
oracle, plus a name-offset oracle and generated large-dictionary identity cells; decode and builder round-trip cells for
#621; goldens and SHA pins re-published by recipe; a base-vs-head codegen differential (D5).

**Target Platform**: Linux and Windows.

**Project Type**: C++ library: dictionary loaders, code generator, generated typed tiers.

**Performance Goals**: `xml_loader_bench` within Article VIII §2's +5%, paired against the merge-base (D6).

**Constraints**: C ABI and Python unchanged; every generated file without a MultiCharValue field byte-identical, except
the `FieldRef` row trailer in `Fields.hpp`.

**Scale/Scope**: the files below. Republished: 003 v50sp2 golden, 076 vlatest golden, 078 v50sp2 and vlatest trees,
the read-tier hashes of every `Fields.hpp` and of v50sp2 `Messages.hpp` and `Reify.hpp`, the census table and Fields
parser.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Article | Gate | Disposition |
|---|---|---|
| I / VI | dictionaries and codegen fidelity | PASS: every vendored base's members, names and identities correct; any component count loads (QuickFIX parity); FIX's MultipleCharValue representable. |
| VI §5 | Normative References | PASS: spec §Normative References. |
| VII | RED first, mutation-proven | PASS planned (D4). |
| VIII §2 | no regression | `xml_loader_bench` paired locally against `fa778a8e`, +5% budget, independent approval above it (D6). |
| IX | coverage, static | `/speckit-verify`; `tools/codegen/` is lint scope since #265. |
| X | C ABI | C ABI unchanged: `fixpp_dict_t` opaque, `src/capi` reads `FieldRef::rule` and `type` (not widened), SWIG exposes none of the records. Article X does not govern the C++ break. |
| XV.13 | hybrid codegen + runtime dictionary | unchanged. |
| XVII.1 | Gate A on public API | Gate A required and assesses the C++ break: the corrected contract, B-102-1..3, the 2c §4.1/§4.2 amendment (D8). |

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
include/fixpp/dict/field_ref.hpp          # D1 layout (component_index 32-bit, _reserved gone)
include/fixpp/dict/component_ref.hpp      # D1 layout
include/fixpp/dict/group_ref.hpp          # D1 layout
include/fixpp/dict/error.hpp, xml_loader.hpp, orchestra_loader.hpp   # D8 comments (side-table cause gone)
src/dictionary/xml_loader.cpp             # D1 32-bit writes; component identity
src/dictionary/orchestra_loader.cpp       # D1 same; D1b surrogate-id parse split
src/dictionary/dictionary_internal.hpp    # D1 component_fields_impl parameter; D1b id parse helper
src/dictionary/dictionary.cpp             # D7 reader arithmetic
src/dictionary/overlay_merge.{hpp,cpp}    # D2 retire the side-table check; D1b surrogate-id parse
tools/codegen/fixpp-codegen/gen_util.hpp  # D3 kind_of
tools/codegen/fixpp-codegen/emit_fields.cpp  # D5 FieldRef row without _reserved
tools/codegen/fixpp-codegen/ir.cpp        # D1b surrogate-id parse
specs/003-…/golden, specs/076-…/golden, specs/078-…/golden   # D5 republished
tests/dictionary/                         # ref_shape, equivalence fixture repair, name-offset oracle, identity cells,
                                          # reader cell, overlay refusal/equivalence
tests/codegen/                            # census mapped_kind + Fields parser, validator_shape, read-tier pins,
                                          # decode cells, Orchestra IR id cell
tests/session/                            # 077 round-trip assignments; builder round-trip cells
```

Records and design documents (D8): `spec/behaviors-and-limitations.md` (+ closed file), `spec/feature-catalogue.md`,
`spec/coverage-index.md`, `.specify/2c-codegen.md` §4.1/§4.2, `specs/002-dictionary-xml-loader/` extracts,
`brain/components/dictionary.md`.

**Structure Decision**: existing layout.

## Phases (for /speckit-tasks)

1. **RED** — equivalence fixture repaired (32-bit `check` operands, Orchestra ids 32-bit) and with exclusions removed
   (RED at `RgstDistInstGrp`); name-offset oracle (RED on SP1/SP2/Latest); component-identity cells for both formats,
   the Orchestra id-domain pair, the overlay-crossing rows, the Orchestra overlay and IR id cells (RED on `fa778a8e`);
   reader cell; MultiCharValue decode, group-entry, reified, builder and vlatest shape cells (RED: decode failure /
   compile-level shape check).
2. **#654** — classify the D1, D1b and D7 recipes' hits; D1 layouts and loader writes; D1b id parse in the loader,
   `overlay_merge` and `ir.cpp`; D2 retirement; D7 readers.
3. **#621** — D3; `emit_fields` row; regenerate and republish per D5; update the census (kind and parser), the
   `validator_shape` check and the 077 sources.
4. **Differential** — D5's base-vs-head codegen diff, hunk classification and planted positive.
5. **Mutations** — each change reverted alone, its cell RED (D4's list).
6. **Records** — D8: B&L (L-098-4, L-621-1 closed; B-098-1 amended; B-102-1..3; L-102-1; L-098-3 pointer); 2c, 002,
   brain and header-comment amendments; catalogue / coverage index. Hand the orchestrator D2's §B86 text.
7. **Verify** — targeted ctest, D6 paired run and footprint, MSVC build of the touched targets, `/speckit-verify`.

## Open for Gate A

1. *(Resolved in round 1.)* The component cap is dropped; the component identity is widened end to end, per the B86
   owner ruling (spec Clarifications, Gate A round 1; research D1).
2. *(Resolved in round 1.)* `std::string_view`, not a dedicated type (both reviewers; research D3).
3. *(Resolved in round 1.)* D2's retirement stands, with its premise restated as a condition and the 32-bit run
   obligation handed to B86 (research D2).
4. New in rewrite 1, for round 2 to check: D1b widens the Orchestra surrogate-id parse in three places outside the
   loaders' `finalize` (`overlay_merge.cpp`, the code generator's `ir.cpp`, the equivalence fixture). It follows from
   the ruling (the schema's `id_t` makes the 16-bit parse a cap, not a format rule), but it grows the source list.

## Gate A

### Round 1 → rewrite 1 (2026-11-07)

Inputs: Codex round 1 (P1=2 P2=4 P3=2) and the Opus judgement (P1=0 P2=6 P3=5), in
`research/reviews/{codex,opus}_102-dict-width-multichar-type_gate_a*_review.md`; the orchestrator's ruling replacing
the judge's item 3.

| Finding | Judged | Disposition in rewrite 1 |
|---|---|---|
| C1 32-bit narrowings / D2 retirement | P2 | FR-001 restated as the 16-bit class closed; D2's premise restated as the 2^32 condition bounded by B86's budget; §B86 text for the orchestrator in research D2; L-102-1 discloses (a) the pre-B86 run window and (b) the name pool, no guard. `name_at` out-of-bounds claim rejected (judge), recorded in D2. |
| C2 Normative References | P2 | Section added in 099's form; titles copied from `spec/coverage-index.md`; checklist re-ticked after. |
| C3 component cap | P2, ruling | **Replaced by the orchestrator ruling**: cap dropped, component identity widened end to end (D1, FR-001a), enumerated by recipe; Orchestra surrogate ids widened to the schema domain (D1b, FR-001b; the 16-bit parse verified not to be a format rule against `repositorytypes.xsd`); Open item 1 removed; Clarification recorded. Group identity stays 16-bit, bounded by the tag domain (stated as such). |
| C4 migration list / contract | P2 | Contract rewritten: what compiles, what fails, what silently changes; declared order kept in all three records (no reorder, so positional initializers keep meaning); amendment list in D8 (2c §4.1/§4.2, 002 extracts, brain page, header banners, `error.hpp` / loader headers); B-rows named. |
| C5 D5 cannot prove SC-003 | P2 | D5's base-vs-head differential over every tier with hunk classification and a planted positive; the `FieldRef` row-trailer transform folded in; moved pins carry 102 banners. |
| C6 D4 witnesses | P3 | vlatest compile-shape cells; v50sp2 and vlatest `LegExecInst(1384)` group-entry readback; one reified decode. Lifetime cells rejected (judge). |
| C7 D6 benches | P3 | `xml_loader_bench` paired against `fa778a8e` under VIII §2 (release, A-B-A-B, min-per-tree, +5%, approval); footprint bench bytes recorded, not timed. |
| C8 Article X | P3 | X row is "C ABI unchanged"; the C++ break is assessed under XVII.1. |
| N-1 fixture lambdas / name_offset oracle | P2 | Both `check` lambdas widened; `_reserved` checks removed; the name-offset oracle named (internal access, pool slice equals name; RED on SP1/SP2/Latest). |
| N-2 reader `uint32_t + uint32_t` | P3 | FR-008 and D7: every run reader in `std::size_t` (or subtraction form), enumerated by recipe; internal-access reader cell. |
| N-3 US1 reach | P3 | US1's priority split into the `component_fields` half (shipped dictionary, no `src/` caller) and the group-store half (a group table past 65,535 entries, no vendored base), plus the identity half. |

Where source contradicted the judge: none on the judged facts. The judge's ComponentRef reorder and silent positional
reassignment no longer apply, because under the ruling every `ComponentRef` member is 32-bit and the order is kept;
`ComponentRef` is 20 bytes, not 16.
