# Implementation Plan: FIX Latest is ApplVerID 10 — its own application version, both directions

**Branch**: `097-fix-latest-applverid-10` | **Date**: 2026-10-29 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/097-fix-latest-applverid-10/spec.md`

## Gate A

(Filled by `/gate-a`.)

## Summary

FIX Latest gets the wire identity the FIX documents give it, ApplVerID `"10"`, in place of 074's
mapping onto FIX50SP2:
- a new `application_version::vlatest` enumerator, parsed from and rendered to `"10"`, and accepted
  as a config token (research R-1, R-2, R-7);
- its own `version_registry` slot, which retires the FR-010 abort, L-074-1 and L-080-1. Lookup is a
  one-way fallback per the owner's second Q1 ruling: FIX Latest answers FIX50SP2 when that dictionary is
  absent, never the reverse (R-3). L-097-1 records that only direct C++ can register both;
- a FIX Latest arm in the generated reify dispatch, with the vlatest typed classes reporting FIX
  Latest (R-4, R-5);
- inbound 1407/1156 accepted and ignored, and nothing new emitted (R-6).

The session code is unchanged, because its three version checks already go through the parse and the
registry. Records: a MINOR constitution amendment, B&L, the catalogue and the coverage index (data-model
§C–§F).

## Technical Context

**Language/Version**: C++23 (library, codegen tool, tests); Python 3 and Bash for the records checks
**Primary Dependencies**: GoogleTest, pugixml (both already linked by the touched binaries), `fixpp_dictionary`,
`fixpp_session`, `fixpp_config`, `fixpp-codegen`
**Storage**: N/A. Inputs are the vendored `dictionaries/FIX50SP2.xml`, `dictionaries/FIXT11.xml` and
`dictionaries/orchestra/OrchestraFIXLatest.xml`
**Testing**: existing gtest binaries for dictionary, config, session and codegen, each gaining ctest label `097`
**Target Platform**: every CI platform. The FIX Latest dispatch arm's MsgType switch exists only under
`FIXPP_CODEGEN_FIX_LATEST` (default ON); under OFF the arm fails loud (R-5)
**Project Type**: library — production change in `dict`, `config` and `tools/codegen`, plus tests and records
**Performance Goals**: no hot path changes (the Logon-time parse gains one length check). No bench
row is expected to move; verify runs the paired gate as usual
**Constraints**: the orchestrator writes no library code, so `phase-implementer` writes every source,
test and CMake line. Additive public enum. No C-ABI change. No `src/session/` change
**Scale/Scope**: about 10 source files, about 8 test files, 1 regenerated golden, 6 record files

## Constitution Check

| Clause | Status |
|---|---|
| `[const §I.1]` | Amended (FR-012(a); data-model §E). Without the amendment, Article I §1 would contradict the shipped mapping. |
| `[const §I.3]` | No `backlog` row closes. L-074-1 and L-080-1 retire. |
| `[const §VI.4]`, `§VI.5` | Coverage-index rows updated (data-model §F); spec Normative References lists the sources. |
| `[const §VII.3]`/`§VII.4` | TDD order: every witness shown RED (unfixed tree or named mutant) before GREEN (contracts). |
| `[const §VII.8]` | No new executable. Witnesses join existing binaries; selected by `ctest -L 097`. |
| `[const §IX.1]` | Touched modules ≥ 95/85 or a per-line assessment, at verify. |
| `[const §IX.4]` | clang-tidy/clang-format on changed files, `tools/codegen/` included. |
| `[const §X.1]`, `§X.7` | C ABI unchanged; nothing to bump. |
| `[const §XVII.1]` | Gate A runs: public C++ API change, behaviour change and amendment. |
| `[const §XVII.7]` | Local full build + ctest of `linux-clang-debug` at the final head. |
| `[const §XVIII.2]` | Annotated (data-model §E.3). |
| `[const §XX.1]`–`§XX.4` | Amendment first in commit order; MINOR v4.3; PR titled in §XX.2 form; owner sign-off before RATIFIED. |

No violation to justify.

## Project Structure

### Documentation (this feature)

```text
specs/097-fix-latest-applverid-10/
├── spec.md
├── plan.md            # this file
├── research.md        # R-1..R-10
├── data-model.md      # §A identities, §B surfaces, §C superseded text, §D B&L, §E amendment, §F catalogue
├── quickstart.md      # §0..§5 validation
├── contracts/
│   └── witnesses.md   # W-1..W-10, mutants M-1..M-9b
└── checklists/
    └── requirements.md
```

### Files changed

- `include/fixpp/dict/version_profile.hpp`, `src/dictionary/version_profile.cpp`
- `include/fixpp/dict/version_registry.hpp`, `src/dictionary/version_registry.cpp`
- `src/config/scalar_mappers.cpp`
- `tools/codegen/fixpp-codegen/emit_dispatch.cpp`, `ir.cpp`, `gen_util.hpp`
- `specs/076-fix-latest-typed-codegen/contracts/golden/vlatest_Messages.golden.hpp` (regenerated)
- tests: `tests/dictionary/{version_profile_test,orchestra_loader_test,reify_dispatch_test}.cpp`,
  `tests/config/` (one case + fixture), `tests/session/test_fixt_logon_establishment.cpp`,
  `tests/codegen/{vlatest_dispatch_exclusion_test,vlatest_compile_smoke_test}.cpp`, the CMake label lines
- records: `.specify/constitution.md`, `spec/behaviors-and-limitations{,-closed}.md`,
  `spec/feature-catalogue.md`, `spec/coverage-index.md`, `brain/components/dictionary.md`

## Phases

1. **Amendment** (the first commit): data-model §E text and the Sync Impact Report with PENDING
   sign-off slots.
2. **Unmodified-code witnesses first** (quickstart §1.1). Before any production edit, write the witnesses
   that compile against unmodified code:
   - W-1's string half;
   - W-3's text arm;
   - W-4 construction-only (the death test, as it stands);
   - W-5's FIX-Latest-alone arm;
   - W-6 (a)/(b)/(c);
   - W-7 (a)/(b)/(c)/(d), with `validate_inbound_messages` ON.

   Build and run them in `build/linux-clang-debug`; that run is the RED record and the
   characterization record. **Stop rule:** if W-7 (a) or (d) fails on unmodified code, FR-008 needs
   session code and the plan's "no `src/session/` change" is wrong. Stop and decide, and do not adjust
   the test.
3. **Core**: enumerator, parse, render, registry with the one-way fallback, config. W-1..W-5 green.
   Mutants M-1..M-5c.
4. **Session witnesses**: W-6..W-8 (no session source change). Mutants M-7a..c.
5. **Codegen**: dispatch row, `ir.cpp`, `gen_util.hpp`, golden regenerated (quickstart §3), W-9.
   Mutants M-9a/b.
6. **Comments and records**: data-model §C–§F; quickstart §4 checks. A rewritten comment states the
   rule and the derivation, never a count: `kTableSize` is derived from the enum with a `static_assert`,
   and the `"2".."9"` tables in `version_profile.hpp` and `scalar_mappers.cpp` point at the code instead of
   re-listing values (CLAUDE.md: a comment may record a procedure, not a result).
7. **Final head**: one full configure + build + ctest (`-j8`), `/speckit-verify`, then Gate B.

## Risks

- **A generated-code blast radius larger than the golden.** Other checked-in generated output may embed
  the vlatest classes' version. Mitigation: after phase 5, `git status` must list only the files named
  above. Any other changed generated file stops the phase for a decision.
- **The fallback hides a mis-registration.** A C++ integrator who meant to register FIX50SP2 but did not
  gets FIX Latest answering `1137=9`. This is today's behaviour, kept by ruling. Inbound validation uses
  the session's own dictionary in either case.
- **`-Wswitch` in a consumer** sees the new enumerator. Disclosed in B-097-2.
- **A witness that cannot fail.** Every row of contracts carries its RED arm, and quickstart §1.2 gates
  each mutant row on build rc 0.

## Complexity Tracking

None.
