# Implementation Plan: FIX 4.0/4.1 dictionary loader legacy-type support (A-5 / D-004)

**Branch**: `064-fix4041-legacy-types` | **Date**: 2026-07-05 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/064-fix4041-legacy-types/spec.md`

## Summary

Add two collapse-table alias rows to the XML dictionary loader's field-type vocabulary
(`src/dictionary/xml_loader.cpp:97-104`) — `TIME → field_data_type::UtcTimestamp` and
`DATE → field_data_type::LocalMktDate` — so the two remaining un-loadable runtime versions, **FIX 4.0**
and **FIX 4.1**, can be read. Vendor `dictionaries/FIX40.xml` and `dictionaries/FIX41.xml` verbatim from
`quickfix/quickfix @ 19ef6a4c` (the `UPSTREAM.txt` pin), add FIX 4.0/4.1 headline `VersionParam` rows to
the dictionary lookup test asserting their **pre-FIXT session** messages are present, and record the
narrow, global AC-L8 relaxation + the QuickFIX `DATE` divergence.

The two mappings are verified from primary sources (research R2/R3): `TIME → UtcTimestamp` agrees with
QuickFIX's `XMLTypeToType`; `DATE → LocalMktDate` is a user-chosen semantic upgrade over QuickFIX (which
returns `TYPE::Unknown`, no validation), and is **behaviorally free** because the Phase-1 validator
collapses both to `field_type::String` (research R4). The `field_data_type` enum is untouched; no
public / C-ABI / wire / error surface changes. This is the loader-touching complement to the merged
**D-005/006** (PR #171) pure-data half of A-5, and it closes the last `[const §I.1]` runtime-XML gap.

Because it relaxes the recorded fail-closed contract **AC-L8** for two type names, it runs as a full
Spec-Kit feature with a Gate-A design review (user decision, 2026-07-05) — the treatment precedent set by
**060** (which amended a Gate-A decision rather than editing quietly).

## Technical Context

**Language/Version**: C++23 (Clang Tier-1 per `[const §II.2]`; GCC Release sanity). No compiler-specific code.
**Primary Dependencies**: none new. `pugixml` (already a dep) parses the XML; the change is two rows in a
  `constexpr` table.
**Storage**: two checked-in XML data files (`dictionaries/FIX40.xml`, `FIX41.xml`); no runtime storage.
**Testing**: GoogleTest (`tests/dictionary/{lookup,xml_loader,negative_paths}_test.cpp`).
**Target Platform**: all tiers (pure data + one table; no platform-specific path). No MSVC-only surface.
**Project Type**: C++ library (single project — `src/dictionary` + `dictionaries/` data + `tests/dictionary`).
**Performance Goals**: N/A — load-time only, off the hot path; two extra `constexpr` rows in a linear scan
  over a small table. No bench, no baseline.
**Constraints**: **zero public/C-ABI/wire/error/layout surface change** (FR-008); `field_data_type` enum
  frozen (`[FIX50SP2 §3.3]`, no variant added); AC-L8 relaxation is exactly two named aliases, global
  (not version-scoped), fail-closed preserved for every other name.
**Scale/Scope**: two table rows in `src/dictionary/xml_loader.cpp`; two vendored XMLs + README; two lookup
  `VersionParam` rows + one focused typing/loadability test + a fail-closed companion. No new module,
  header, dependency, or build-graph target.

## Constitution Check

*GATE: evaluated against `.specify/constitution.md` v0.3 (2026-06-17). Re-checked after Phase 1 (unchanged).*

| Article | Gate | Status |
|---|---|---|
| **I §1** All nine versions' runtime-XML in v1.0 | FIX 4.0/4.1 are named runtime-XML-only versions | **PASS (advances)** — this feature makes the last two named runtime-XML versions loadable; closes the `[const §I.1]` gap. |
| **I §3** Master Feature Catalogue is the coverage tracker; every OFFICIAL row `done`/`dropped` before v1.0 | D-004 rows currently open | **PASS w/ obligation** — flip the D-004 catalogue rows to `done` with this PR (FR-010, close-out step 19a). |
| **VII §3/§4** TDD, no code without a test | red-first loadability + typing + fail-closed tests | **PASS** — the two vendored dicts fail-load *before* the rows (RED), pass *after*; the AC-L8 witness stays green. |
| **VI §5** Normative References in `/specify` artifacts | `## Normative References` in spec.md | **PASS** — present; states no new OFFICIAL FIX rows, lists the AC-L8 anchor + `[const §I.1]`/`[FIX50SP2 §3.3]`. |
| **IX §1** Coverage ≥95/85 on touched modules | Linux/Clang lcov on `xml_loader.cpp` | **PASS** — the two new rows are covered by the FIX40/41 load tests (both files use both types) and the typing assertions; no new uncovered branch. |
| **IX §2** ASan/UBSan/TSan Tier-1 | loader parse path | **PASS** — no new allocation or unsafe op; the added rows are `constexpr` string_view compares. |
| **IX §5 / X §1,§6** C-ABI frozen, abidiff | no C-ABI surface | **PASS** — no public header / C-ABI symbol / error variant touched (FR-008) → abidiff clean, no `/analyze` ABI-surface trigger. |
| **AC-L8** loader-acceptance contract (`specs/002-dictionary-xml-loader/data-model.md:297`) + `[FIX50SP2 §3.3]` enum contract | enum frozen; only alias rows added | **PASS** — the `field_data_type` **enum** is unchanged; the additions are `xml_name → existing-enum` collapse rows, the same mechanism as the existing post-canonical carve-out (research R5). The **relaxed contract is AC-L8** (a loader acceptance rule, not a constitution article — there is no field-type-freeze article), reviewed by Gate A. |
| **XVI §3** `/clarify` mandatory before `/plan` | ran 2026-07-05 (0 user Q; 1 factual resolution) | **PASS** — both material decisions pre-resolved with the user; clarify surfaced + resolved the metadata-only/interop point. |
| **XVII** Codex Gate A/B | AC-L8 relaxation | **PASS w/ obligation** — Gate A MUST review the AC-L8 relaxation + the QuickFIX `DATE` divergence; the bundle leads with the R2/R3 primary-source evidence + the R4 metadata-only proof. |
| **XVIII §6** FIX 4.0/4.1 lowest priority, best-effort | runtime-XML only (no codegen) | **PASS** — scope is loadability + lookup only; no typed-message namespace / codegen (that stays post-v1.0 best-effort per §XVIII.6). |

**No unjustified violations → Complexity Tracking empty.** The two documented obligations (catalogue rows
`done`; Gate-A-reviewed AC-L8 relaxation) are carried into tasks, not constitution deviations.

## Project Structure

### Documentation (this feature)

```text
specs/064-fix4041-legacy-types/
├── plan.md              # This file
├── spec.md              # Feature spec (+ Clarifications 2026-07-05)
├── research.md          # Phase 0 — R1..R5 (type enumeration, TIME/DATE mappings, metadata-only, mechanism)
├── data-model.md        # Phase 1 — E-1 collapse table rows, E-2 frozen enum, E-3 vendored files, E-4 VersionParam rows
├── quickstart.md        # Phase 1 — vendor + build + run + scope-check recipe
├── contracts/
│   └── loader-vocabulary-contract.md   # accept-set before/after + BG-1..5 + amendment checklist
├── checklists/
│   └── requirements.md  # Spec-quality checklist (from /specify)
└── tasks.md             # Phase 2 — /speckit-tasks (NOT created here)
```

### Source Code (repository root = library submodule)

```text
src/dictionary/xml_loader.cpp                # THE change: +2 collapse rows ({"TIME",UtcTimestamp},{"DATE",LocalMktDate}) at :97-104
include/fixpp/dict/field_ref.hpp             # UNCHANGED (enum frozen) — verified untouched at verify
include/fixpp/dict/field_type.hpp            # UNCHANGED — cited only for the metadata-only proof (both → field_type::String)

dictionaries/FIX40.xml                       # NEW — verbatim from quickfix @ 19ef6a4c spec/FIX40.xml
dictionaries/FIX41.xml                       # NEW — verbatim from quickfix @ 19ef6a4c spec/FIX41.xml
dictionaries/README.md                       # UPDATE — list FIX40/41; extend the D-006-scoped refresh recipe
dictionaries/UPSTREAM.txt                    # UNCHANGED SHA — assert no pin drift

tests/dictionary/xml_loader_test.cpp         # EXTEND — load FIX40/41 (no throw) + assert DATE→LocalMktDate, TIME→UtcTimestamp via field_ref
tests/dictionary/lookup_test.cpp             # EXTEND — +2 VersionParam rows (session msgtypes REQUIRED present, pre-FIXT); un-exclude the D-004 note
tests/dictionary/negative_paths_test.cpp     # EXTEND — companion to AC-L8: DATE/TIME now DON'T throw; UNKNOWN_TYPE still throws (unchanged)

spec/behaviors-and-limitations.md            # ADD — B-/L- row: DATE→LocalMktDate diverges from QuickFIX Unknown; metadata-only
spec/feature-catalogue.md                    # UPDATE (close-out) — D-004 rows → done, evidence = this PR
```

**Structure Decision**: single-project C++ library layout, already established. The behavior change is two
`constexpr` rows in `src/dictionary/xml_loader.cpp`; every other touched path is vendored data / test /
documentation / catalogue. Unlike 060, **every** amendment site is inside this submodule — no parent-repo
post-merge contract amendment (close-out is the standard submodule-pointer bump + trackers per step 19).

## Phase 0 — Research

See [research.md](./research.md). Five decisions consolidated with primary-source evidence: R1 (only
`DATE`/`TIME` are out-of-vocabulary), R2 (`TIME → UtcTimestamp`, QuickFIX-confirmed), R3 (`DATE →
LocalMktDate`, user upgrade over QuickFIX `Unknown`), R4 (metadata-only → zero interop-rejection risk),
R5 (extend the existing collapse table, not the enum; global relaxation, AC-L8 preserved). Three
confirm-at-implement obligations (re-enumerate vendored-file types; byte-identical vendoring; README
refresh-recipe now includes FIX40/41).

## Phase 1 — Design & Contracts

- [data-model.md](./data-model.md): E-1 the two collapse rows + preserved AC-L8 invariant, E-2 frozen
  enum, E-3 vendored files (pre-FIXT session-bearing), E-4 the two `VersionParam` rows (session REQUIRED,
  the inverse of D-006).
- [contracts/loader-vocabulary-contract.md](./contracts/loader-vocabulary-contract.md): the field-type
  accept-set before/after table, behavioral guarantees BG-1..5, and the 6-row amendment/recording
  checklist Gate B verifies.
- Agent context: update the `<!-- SPECKIT ... -->` block in `library/CLAUDE.md` to point at this plan.

## Complexity Tracking

> No Constitution Check violations. Table intentionally empty.

## Gate A

- Round 1 applied 2026-07-05: Codex P1=0 P2=1 P3=2; Opus post-judging P1=0 P2=1 P3=3; rewrite addresses RC (Constitution-Check Article XII mis-cite → AC-L8/[FIX50SP2 §3.3] governing source) + 3 P3 doc-consistency fixes (quickstart build targets, requirements.md clarify-past-tense, field_ref.hpp enum-variant line anchors). Reviews: research/reviews/codex_064-fix4041-legacy-types_gate_a_review.md, research/reviews/opus_064-fix4041-legacy-types_gate_a_adversarial_review.md.
