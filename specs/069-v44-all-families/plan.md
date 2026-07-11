# Implementation Plan: v44 all-families typed codegen coverage

**Branch**: `069-v44-all-families` | **Date**: 2026-07-11 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/069-v44-all-families/spec.md`

## Summary

Widen the existing `067` codegen writer-emitter so it generates typed `build_<Msg>` / `validate_<Msg>` / `<Msg>Args` for **all FIX44 application messages** (measured spike upper bound 86; in-scope target **81** = all `msgcat='app'` messages minus N-002/N-003), not just the 33 OFFICIAL MsgTypes. Emission is mechanical from the existing `VersionIR` (spike-verified); the read side (`Messages.hpp` + `dict::reify`) is **already** generated for every message, so this feature is **write-side + verification only**. Coverage is selected by a new CMake option `FIXPP_CODEGEN_V44_FAMILIES=all|official` (**default `all`**); OFFICIAL-only regenerates today's 33 byte-identically. Every generated builder is verified by a **differential round-trip** against the independent runtime-XML parse path, anchored by a small exemplar-per-family external-golden set. No runtime / C-ABI / Python surface change. v44 only.

## Technical Context

**Language/Version**: C++23 (Clang 22 local == CI, per Article II/III). The codegen host tool `fixpp-codegen` and the generated headers are C++23.
**Primary Dependencies**: existing `fixpp-codegen` emitter (`tools/codegen/fixpp-codegen/`), `wire::body_builder` (061), `dict::reify` (057), the runtime-XML parse path (`dict::Dictionary::as_table_view`), CMake codegen driver (`cmake/Codegen.cmake`). No new third-party dependency.
**Storage**: N/A (generated headers under `build/<preset>/_codegen/include/fixpp/v44/`; source-tree unaffected).
**Testing**: GoogleTest + ctest; a new table-driven differential round-trip harness over the emitted set; the 067 shape-oracle/roundtrip/validate/failclosed/completeness suite extended; Tier-1 sanitizer matrix via `/speckit-verify`.
**Target Platform**: Linux (clang) primary; MSVC + gcc-release exercised in CI (codegen is host-tool + headers, platform-portable).
**Project Type**: C++ library — codegen tool + generated headers + tests. No application/service surface.
**Performance Goals**: No runtime performance surface (FR-012, no runtime change). Build-cost is the governed axis: measured +19 s/TU (2.57×) for the full header, bounded by the CMake option (default `all`; opt-down `official` restores baseline).
**Constraints**: 33 OFFICIAL messages byte-identical under either mode (FR-005/SC-003); no runtime/C-ABI/Python change (FR-012); v44 namespace only (FR-004); enum-domain validation out of scope (FR-013); differential verification must be non-tautological (FR-009/FR-010).
**Scale/Scope**: 81 in-scope v44 application builders (33 existing + ~48 new). Spike measured 86 (its upper bound included N-002/N-003); feature excludes those 5, so real cost ≤ measured. Blast radius = the ~5 test TUs that include `v44/Builders.hpp` (no public header pulls it).

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-checked after Phase 1 design.*

**Appendix A trigger: Codegen layout** → this feature triggers **all four mandatory controls**: `/clarify` (DONE — Session 2026-07-11, 2 Qs), `/analyze` (pending, after `/tasks`), **Codex Gate A** (pending, after this plan), **user `/plan` sign-off** (pending). Also Article XVI §3 (`/clarify` mandatory before `/plan` for codegen) — satisfied.

| Gate | Article | Status |
|---|---|---|
| TDD red-green (VII §3/§4) | VII | PLANNED — differential harness + new-family witnesses written failing-first; completeness pin moved with the set. |
| Forced-regen + golden discipline (codegen) | VI/XVII | PLANNED — regenerate all 4 namespaces; `codegen-build-graph-check` git-cleanliness gate; 33-OFFICIAL byte-identical diff asserted. |
| Sanitizer matrix (IX) | IX | PLANNED — `/speckit-verify` Tier-1 (ASan/UBSan/TSan + debug) on the codegen consumers + differential harness. |
| No runtime/ABI/Python surface (X/FR-012) | X | PASS by construction — codegen + tests only; C-ABI stays GA-frozen 1.5.0; no `c_api.h` touch. Confirm via `capi_freeze.sha256` unchanged. |
| Fuzzing (VII §8) | VII | N/A — no new parser-touching runtime code (write emitter is a build-time host tool; the runtime parse path is unchanged). |
| Coverage (IX) | IX | PLANNED — generated builders exercised by the differential harness; lcov measured at `/implement`, not waived ex-ante. |

**⚠️ Constitution conflict — RESOLVED by folded amendment (user-approved 2026-07-11).**
Article XVIII §7 defers application-message rows **A-014..A-034 to v1.x**, and §5 forbids early-shipping post-1.0 scope into v1.0. Feature 069's new messages ARE that deferred set (A-014/015/019/025/034 + C/R/P-004..008 families); since 067 already shipped the v1.0 OFFICIAL set, the widening inherently lands v1.x-scoped codegen, and the v1.0 tag is not yet cut. **Resolution (user decision 2026-07-11): proceed now, folding an Article XVIII §7 amendment into this feature** (Article XX §1 amend-then-proceed; precedent: Gate-A-folded amendments in features 035/043). The amendment reclassifies all-v44-application typed codegen as delivered here, absorbing the already-pending D7 §XVIII.7 staleness reconciliation. Gate A reviews the amendment; user signs off. Tracked in Complexity Tracking + `research.md` R7. **This is the one Constitution Check item requiring justification; all other gates pass or are planned.**

## Project Structure

### Documentation (this feature)

```text
specs/069-v44-all-families/
├── plan.md              # This file
├── research.md          # Phase 0 — R1..R8 decisions
├── data-model.md        # Phase 1 — IR msgcat field, coverage-mode, emitted-set
├── quickstart.md        # Phase 1 — build with families=all|official; run the harness
├── contracts/
│   └── coverage-and-completeness.md   # coverage-mode + emitted-set completeness contract
├── checklists/
│   └── requirements.md  # spec quality (from /specify)
└── tasks.md             # Phase 2 (/speckit-tasks — NOT created here)
```

### Source Code (repository root of the library submodule)

```text
tools/codegen/fixpp-codegen/
├── ir.hpp                # + MessageIR::is_application (or msgcat enum)
├── ir.cpp                # parse msgcat='app'|'admin' from <message> into the IR
├── emit_builders.cpp     # replace kOfficial33-only gate with coverage-mode predicate
│                         #   (official = kOfficial33 allowlist [unchanged output];
│                         #    all = is_application && !is_n002_n003_excluded)
└── main.cpp              # thread the coverage mode (CLI flag --families all|official)

cmake/
└── Codegen.cmake         # + FIXPP_CODEGEN_V44_FAMILIES cache option (default all) → --families

tests/
├── codegen/
│   └── test_067_emit_builders_unit.cpp   # move kOfficial33 completeness pin → intended-set pin
└── session/
    ├── test_069_all_families_roundtrip.cpp   # NEW — differential round-trip over the full set
    ├── test_069_family_exemplar_golden.cpp   # NEW — external-golden anchor, exemplar-per-family
    └── test_067_*                            # extended where the set widens

spec/
├── coverage-index.md         # flip the newly-covered app-message rows (write column)
├── feature-catalogue.md      # 069 close-out rows
└── behaviors-and-limitations.md  # L-069-* (enum-domain unbacked; any nested-family limitation)

.specify/constitution.md      # Article XVIII §7 amendment (folded; Gate A + user sign-off)
```

**Structure Decision**: Reuse the 067 emitter and its test module verbatim; the only emitter change is the **selection predicate** (allowlist → msgcat-driven coverage mode) plus a **one-field IR addition** (msgcat) and a **CMake option**. No new module, no new public header, no new runtime code. Verification adds two test TUs (differential harness + exemplar golden) alongside the existing 067 builder suite.

## Complexity Tracking

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| Article XVIII §7 amendment (folded into 069) | 069's coverage goal lands the §7-deferred A-014..A-034 + C/R/P families as v1.x-scoped codegen before the v1.0 tag; the constitution must be amended before proceeding (Article XX §1). User-approved 2026-07-11. | Deferring 069 until after the v1.0 tag was offered and declined — it blocks "close legacy before Orchestra" behind the whole (not-imminent) v1.0-tag gate. Narrowing scope to avoid the deferred set was offered and declined — 067 already shipped the non-deferred set, so nothing coherent remains to widen. |
| New IR field (`is_application`/msgcat) | The emitter currently selects via a hardcoded 33-allowlist; "all app-messages minus N-002/N-003" needs the app/admin category, which the IR does not yet carry. | Hardcoding a second ~81-element allowlist was rejected — it duplicates dictionary knowledge, goes stale on dictionary revisions, and re-creates the exact drift the completeness pin exists to prevent. msgcat is authoritative and already in the XML. |
