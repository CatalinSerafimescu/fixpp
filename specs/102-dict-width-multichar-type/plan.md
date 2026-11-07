# Implementation Plan: Dictionary index width and the MultiCharValue accessor type

**Branch**: `102-dict-width-multichar-type` | **Date**: 2026-11-07 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/102-dict-width-multichar-type/spec.md`

## Summary

Widen the dictionary records' side-table starts, counts and name offset, and the whole component identity, to 32 bits,
with no refusal; read Orchestra component and group ids in the schema's domain; retire 098's 16-bit side-table refusal;
do every run reader's bounds arithmetic so it cannot wrap (B84, #654). Map MultiCharValue to the generator's string
kind, so its accessors and builder inputs are `std::string_view`, as for MultipleStringValue and as QuickFIX types it
(B67, #621). Decisions D1–D9 in [research.md](research.md); the C++ breaks in
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
| I / VI | dictionaries and codegen fidelity | PASS: every vendored base's members, names and identities correct; no component count is refused (QuickFIX parity); identities exact below 2^32 components (L-102-1 (c)); FIX's MultipleCharValue representable. |
| VI §5 | Normative References | PASS: spec §Normative References. |
| VII | RED first, mutation-proven | PASS planned (D4). |
| VIII §2 | no regression | `xml_loader_bench` paired locally against `fa778a8e`, +5% budget, independent approval above it (D6). |
| IX | coverage, static | `/speckit-verify`; `tools/codegen/` is lint scope since #265. |
| X | C ABI | C ABI unchanged: `fixpp_dict_t` opaque, `src/capi` reads `FieldRef::rule` and `type` (not widened), SWIG exposes none of the records. Article X does not govern the C++ break. |
| XV.13 | hybrid codegen + runtime dictionary | unchanged. |
| XVII.1 | Gate A on public API | Gate A required and assesses the C++ break: the corrected contract, B-102-1..3, the 2c §1, §4.1, §4.1.3 and §4.2 amendments (D8). |

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
include/fixpp/dict/error.hpp, xml_loader.hpp   # D8 comments (side-table cause and guard order)
include/fixpp/dict/field_traits.hpp       # D8 comment (no MultiCharValue / MultiStringValue split)
src/dictionary/xml_loader.cpp             # D1 32-bit writes; component identity
src/dictionary/orchestra_loader.cpp       # D1 same; D1b surrogate-id parse split
src/dictionary/dictionary_internal.hpp    # D1 component_fields_impl parameter; D1b id parse split, surrogate-id type
src/dictionary/dictionary.cpp             # D7 reader arithmetic
src/dictionary/overlay_merge.{hpp,cpp}    # D2 retire the side-table check and what only it reads; D1b surrogate ids
tools/codegen/fixpp-codegen/gen_util.hpp  # D3 kind_of
tools/codegen/fixpp-codegen/emit_fields.cpp  # D5 FieldRef row without _reserved
tools/codegen/fixpp-codegen/ir.cpp        # D1b surrogate-id parse and its own surrogate-id type
specs/003-…/golden, specs/076-…/golden, specs/078-…/golden   # D5 republished
tests/dictionary/                         # ref_shape, equivalence fixture repair, name-offset oracle, identity cells,
                                          # rows (a)-(g), reader cell, overlay refusal/equivalence, orchestra_loader
tests/fuzz/                               # D2 duplicate seeds, harness comments
tests/codegen/                            # census mapped_kind + Fields parser, validator_shape, read-tier pins,
                                          # decode cells, Orchestra IR id cell
tests/session/                            # 077 round-trip assignments; builder round-trip cells
```

Records and design documents (D8): `spec/behaviors-and-limitations.md` (+ closed file), `spec/feature-catalogue.md`,
`spec/coverage-index.md`, `.specify/2c-codegen.md` §1, §4.1, §4.1.3 and §4.2, `brain/components/dictionary.md` (a 102
section flagging the superseded 098, 003 and 002 clauses; no merged bundle is edited).

**Structure Decision**: existing layout.

## Phases (for /speckit-tasks)

1. **RED** — equivalence fixture repaired (32-bit `check` operands, Orchestra ids 32-bit) and with exclusions removed
   (RED at `RgstDistInstGrp`); name-offset oracle (RED on SP1/SP2/Latest); component-identity cells for both formats,
   the Orchestra id-domain pair, the overlay-crossing rows, the Orchestra overlay and IR id cells, rows (a)–(f), each
   fixture under the alias-pair rule (RED on `fa778a8e`); row (g), a characterization pin (GREEN on `fa778a8e` and
   the head, RED only under its mutant); the re-pointed side-table rows (D2; RED: refused); reader
   cell; MultiCharValue decode, group-entry, reified, builder and vlatest shape cells (RED: decode failure /
   compile-level shape check).
2. **#654** — classify the hits of the D1, D1b (parse and holder), D2 and D7 recipes; D1 layouts and loader writes; D1b
   id parse split and the surrogate-id type in the loader, `overlay_merge` and `ir.cpp`; the re-pointed
   `StructuralIdOutsideTheUint16RangeThrows`; D2 retirement with every surface D2 decides; D7 readers.
3. **#621** — D3; `emit_fields` row; regenerate and republish per D5; update the census (kind and parser), the
   `validator_shape` check and the 077 sources.
4. **Differential** — D5's base-vs-head codegen diff, hunk classification and planted positive.
5. **Mutations** — D4's three classes, one mutant per independent helper: parse width (each named helper, by kind),
   container width (a compile check per container, plus a cast-through mutant per alias pair), kind collapse (row
   (g)); and each other change reverted alone. Each mutant from a copy, its named row RED.
6. **Records** — D8: B&L (L-098-4, L-621-1 closed; B-098-1 amended; B-102-1..3; L-102-1 (a)–(c); L-098-3 pointer);
   the live-text amendments (2c §1, §4.1, §4.1.3, §4.2; the installed headers; `overlay_merge`'s comments and its
   superseding header comment; the parse comments); the brain section's flags; catalogue / coverage index. Every D8
   recipe hit classified. Hand the orchestrator D2's §B86 text.
7. **Integrate** (D9) — after 100 and then 101 merge: rebase onto the integrated head; re-run the D1, D1b, D2, D7 and
   D8 recipes; republish the goldens, 078 trees and read-tier hashes from the integrated tree; re-run D5's
   differential with the integrated merge-base as its base; re-run the size pins, the accessor census and D6 against
   the merge-base at measurement time; record which base each RED was shown on.
8. **Verify** — targeted ctest, D6 paired run and footprint, MSVC build of the touched targets, `/speckit-verify`.

## Open for Gate A

1. *(Resolved in round 1.)* The component cap is dropped; the component identity is widened end to end, per the B86
   owner ruling (spec Clarifications, Gate A round 1; research D1).
2. *(Resolved in round 1.)* `std::string_view`, not a dedicated type (both reviewers; research D3).
3. *(Resolved in round 1.)* D2's retirement stands, with its premise restated as a condition and the 32-bit run
   obligation handed to B86 (research D2).
4. *(Resolved in round 2.)* D1b's source scope outside the loaders is accepted (both reviewers); its proof plan takes
   the alias-pair rule, rows (a)–(g), the holder recipe and per-helper mutants (research D1b, D4).
5. New in rewrite 2, for round 3 to check: the distinct surrogate-id type (research D1b), adopted from the judge's
   optional suggestion. It stays internal (no installed header names a surrogate id) and turns the container-width
   class into a compile error; the alias-pair witnesses stay.

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

### Round 2 → rewrite 2 (2026-11-07)

Inputs: Codex round 2 (P1=0 P2=2 P3=1) and the Opus judgement (P1=0 P2=1 P3=3), in
`research/reviews/{codex,opus}_102-dict-width-multichar-type_gate_a_2*_review.md`. The judge's fix list was applied in
full; each source claim it rests on was re-read at `fa778a8e` for this rewrite.

| Finding | Judged | Disposition in rewrite 2 |
|---|---|---|
| Codex 1, the 2^32 one-based identity boundary | P3 | FR-001a qualified with the judge's text; L-102-1 (c) in spec §Out of Scope and D8; data-model §Loader behaviour, contract §2 and the Constitution row I/VI qualified; the §B86 hand-off gains the sentence that the budget does not bound the component count; D2 records why (an empty definition costs no step; one step per definition would be the rejected entry cap). The reachability argument was re-derived from `collect_components` (both loaders) and `intern_name_in_pool`. US1.4 and FR-002 unchanged (65,537 is inside the exact range). Codex's "count every definition" alternative rejected (the ruling). |
| Codex 2, silent-skip paths without witnesses | P2 | D4: the alias-pair rule over every surrogate-keyed container; rows (a) parent map, (b) header store, (c) codegen header classification, (d) overlay group definition key, (e) overlay child key, (f) graph build and a high-id cycle, (g) tag kinds stay 16-bit; mutants per independent helper in three classes (parse width, container width, kind collapse). D1b: the holder recipe beside the parse recipe; the kind split named at `id_of` and `records_of`'s `has_id`; the distinct surrogate-id type adopted (Open item 5). |
| Codex 3, superseded 098 and 003 contracts | P3 | D8 rewritten: merged bundles flagged in a 102 section of `brain/components/dictionary.md`, not edited; the 002 in-place note dropped; the live texts added (`field_traits.hpp`, 2c §1 and §4.1.3, `overlay_merge`'s superseding header comment, both `xml_loader.hpp` sentences); the amendment set enumerated by recipe with a four-way classification. |
| N-1, D2's test retirement by hand list | P3 | D2: recipes over symbols, `side.\?table` text in tests, corpus file names; per-surface decisions: the side-table refusal rows re-pointed as loads with their own members (the retirement's witness), their just-inside controls and the order rows deleted, `expect_base_fits` removed, the duplicate seeds deleted, the enum value and every member only the check reads removed. |
| Merge order (orchestrator) | — | D9 and plan phase 7: 102 rebases after 100 and 101 and re-runs its recipes, republish, differential (integrated merge-base), size pins, census and bench at the integrated head. |

Where source contradicted or extended the judge:

- N-1's recipe misses `guard_outcome`'s `base_components_fit` / `base_groups_fit`, the walk results' own
  `components_fit` / `groups_fit`, and `expect_base_fits`, which the doubling and empty-chain rows of both formats
  call; `def_info::entries` feeds only the fit. D2's symbol recipe is widened to them.
- The order rows probe the same input as the `DoublingBombRefusedByTheBudget` rows, and their fuzz seeds are
  byte-identical to the doubling seeds, so they are deleted, not renamed. The judge's `':!tests/fuzz/corpus'` recipe
  cannot see file names; a `git ls-files` line is added.
- The judge's D8 recipe misses the `xml_loader.hpp` guard-order sentence, which breaks between "side-table" and "fit";
  an end-of-line alternative is added and recorded in D8. `orchestra_loader.hpp` has no side-table text of its own, so
  rewrite 1's D8 entry for it is dropped. `field_traits.hpp` is caught by the first split alternative, not the second.
- An existing row pins the 16-bit surrogate parse: `OrchestraFailClosed.StructuralIdOutsideTheUint16RangeThrows`
  refuses component id `65536` and would go RED on the head. D1b re-points it, and names the parse comments the split
  makes false.
- Row (c) needs the header component to carry both id 1024 (the loader's key) and the name `StandardHeader` (the code
  generator's), and the header group must be unreachable from any message, because `walk_orchestra_level` walks the
  header inline and `group_tags` unions every message's `group_order`; otherwise the `collect_orchestra_group_tags`
  mutant cannot go RED.
- `collect_header_tags`' `id_of` also reads each group's `numInGroup` id, a tag, besides the three ref kinds.
- The kind-collapse mutant is defined as a raised range check that still truncates into 16 bits: with the distinct
  surrogate-id type, swapping in the surrogate parser would fail to compile, not turn row (g) RED.
- The judge's row 8 (here (g)) asserts only `orchestra_parse_error` at 65536. Under the kind-collapse mutant 65536
  truncates to 0, which every tag site refuses for another reason with the same exception type
  (`parse_orchestra_field_tag`'s zero message, "not declared", the length pair's declaration check), so the row would
  stay GREEN with the mutant live. (g) now asserts the range refusal's message per kind, at ids whose truncation
  reaches a load (65536 + a declared tag; 65536 + an undeclared tag for `<fixr:field id>`), and is scoped as a
  characterization pin, GREEN on `fa778a8e`, not one of the rows RED there.
- Row (f)'s cycle assertion distinguishes the pre-walk: only `overlay_merge.cpp`'s pre-walk emits "component reference
  cycle" (re-derive: `git grep -n 'reference cycle' -- src`).
- 2c's new notes shift line-anchored 2c citations outside 2c (`field_traits.hpp`, 003's data-model); D8 adds the
  shift audit and the recipe.
