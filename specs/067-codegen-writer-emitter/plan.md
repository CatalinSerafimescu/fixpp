# Implementation Plan: FR-015a-lite — Codegen Writer-Emitter

**Branch**: `067-codegen-writer-emitter` | **Date**: 2026-07-10 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/067-codegen-writer-emitter/spec.md`

## Summary

Add a codegen **write** surface to `tools/codegen/fixpp-codegen/`: a new `emit_builders(ir)` emitter producing `Builders.hpp` per version, with a generated `build_<Msg>(out, const <Msg>Args&)` free function per OFFICIAL application message that serializes through the single `wire::body_builder` core, plus a generated `validate_<Msg>(args)` required-presence routine over emitter-derived level-scoped presence tables. The pivotal correctness pin: for the 5 exemplar MsgTypes (D/8/9/E/AS in v44) the generated body is **byte-identical** to the frozen 061 hand exemplar AND the QuickFIX golden. Generate builders for all **33** OFFICIAL distinct MsgTypes (exact-set gate). Validation is required-presence ONLY (enum/conditional cut), recursive into group entries, as a SEPARATE step off the serialize path. No read-path / wire-semantics / C-ABI / Python change; no new error value.

Technical approach is fully derived in [research.md](./research.md): field-order two-regime rule (R1), IR carries group-child presence (R2), emit new level-scoped header-excluded tables not the flat `<Msg>_rules` (R3), free-function+args-struct API (R4), extra QuickFIX goldens for grouped insurance (R5), the exact 33 (R6), emitter reuse map (R7), constitution triggers (R8).

## Technical Context

**Language/Version**: C++23 (Clang 22 local == CI, Article II §2). The codegen tool `fixpp-codegen` is C++; generated output is C++23 headers.
**Primary Dependencies**: existing `fixpp-codegen` IR + emitters (`emit_messages`, `emit_validator`, `gen_util`, `ir`); `wire::body_builder` (061, `include/fixpp/wire/body_builder.hpp`); `dict::reify()` / typed read flyweights (057/062/063) for round-trip read-back; QuickFIX-cpp offline golden generators (`tests/session/golden/gen/`, MIT, not in main build).
**Storage**: N/A (codegen emits headers into the build tree `build/<preset>/_codegen/include/fixpp/<ns>/`).
**Testing**: GoogleTest (`tests/session/`, `tests/wire/`, `tests/codegen/`); ctest; the sanitizer/coverage/static-analysis Tier-1 matrix; `/speckit-verify`.
**Target Platform**: Linux/Clang Tier 1 (gate); MSVC/libc++ Tier 2/3 CI.
**Project Type**: C++ library + codegen tool (single project; the library submodule).
**Performance Goals**: no new budget; outbound builder inherits `body_builder`'s zero-global-heap fixed arena. No regression to existing codegen/build benches.
**Constraints**: body-only output (INV-2); canonical decimals (INV-3); fail-closed atomic commit (INV-4); group grammar (INV-5); byte-identical shape-oracle; C-ABI 1.5.0 frozen (FR-009); Emitter-Lite (no `emit_enums`, no IR addition — R2/R3).
**Scale/Scope**: 33 OFFICIAL MsgTypes × 1 representative namespace (v44). Emitter delta ≈ one new `emit_builders.cpp` + a runtime `validate` header + witness harness; generated header count +1 per version (`Builders.hpp`).

## Constitution Check

*GATE: evaluated pre-Phase-0 and re-checked post-design. PASS (no violations; no Complexity Tracking entries needed).*

| Article / Appendix | Applicability | Disposition |
|---|---|---|
| **Appendix A — triggers** | Wire format (validator changes) + Codegen layout | **All four controls required.** `/clarify` DONE; `/analyze` at step 6; **Codex Gate A after /plan**; user `/plan` sign-off pending. |
| **XVI §3 — /clarify mandatory** | wire + codegen touched | DONE (2 questions; `## Clarifications` recorded). |
| **X — ABI policy** | must stay unchanged | FR-009: no C-ABI symbol/signature/error-code change. Verify `nm`, abidiff, `check_capi_occupancy.sh`, `error_codes_v1.txt` unchanged; C-ABI 1.5.0 byte-identical. |
| **Error semantics (Appendix A)** | reuse existing wire error | `wire_required_field_missing`(=38) pre-exists; NO new `fixpp_error_t`. Not a trigger; verify no addition. |
| **VI — 100% FIX rule** | advances OFFICIAL write coverage | coverage-index + feature-catalogue updated for the 33 rows' write disposition BEFORE close; Normative References section present; Gate A + Gate B before any row closes. |
| **VII — testing (TDD)** | new emitter + runtime + generated code | Tests-first: shape-oracle byte-equality, round-trip, fail-closed, exact-set completeness, validate-required (top-level + group depth). |
| **IX — coverage / sanitizers / static analysis** | emitter source + runtime harness | Full Tier-1 matrix. Generated headers coverage-excluded (`analyze_coverage.py`); emitter `.cpp` + runtime validate + harness are NOT excluded — must be covered. |
| **VIII — perf budgets** | outbound builder | inherits `body_builder` arena; no new budget; confirm no bench regression. |
| **XV — banned patterns** | no silent loss / no sync-hot-path-log | body_builder fail-closed (INV-4); no app-message drop; no new banned pattern. |
| **XVII §8 — verify gate** | before Gate B | `/speckit-verify` + feature-completeness audit non-failing precede Gate B; evidence in `.specify/decisions/067-*-verify.md`. |
| **XVIII — roadmap** | Emitter-Lite v1.0 slice | in-roadmap (typed-message v1.0); FR-015b/families/Orchestra explicitly out of scope (spec Out of Scope). |

**Codegen-specific mandatory controls (from prior codegen features 003/057/062/063):** forced codegen regeneration + the codegen build-graph cleanliness gate (`git`-clean after regen; `feedback_codegen_build_graph_cleanliness_gate`); re-index CodeGraph after code-changing phases; watch codegen emitter staleness in non-debug build dirs (`project_codegen_emitter_staleness`) — sanitizer/coverage builds compile a fresh `_codegen`, so regenerate before those runs.

## Project Structure

### Documentation (this feature)

```text
specs/067-codegen-writer-emitter/
├── plan.md              # this file
├── research.md          # Phase 0 (R1–R8)
├── data-model.md        # Phase 1 — generated builder / args / tables / validate model
├── quickstart.md        # Phase 1 — how to add/regenerate/verify a builder
├── contracts/
│   └── generated-builder.md   # Phase 1 — the emitter's output contract (extends 061 C1–C6)
├── checklists/
│   └── requirements.md  # spec quality checklist (from /specify)
└── tasks.md             # Phase 2 (/speckit-tasks — NOT created here)
```

### Source Code (repository root = library submodule)

```text
tools/codegen/fixpp-codegen/
├── emit_builders.cpp        # NEW — the write emitter (emit_builders(ir) -> Builders.hpp)
├── emit.hpp                 # + declare emit_builders(const VersionIR&)
├── main.cpp                 # + write_file(base/"Builders.hpp", emit_builders(ir))
├── gen_util.hpp             # reuse kind_of / to_accessor / to_identifier (+ maybe a shared kind→setter map)
└── emit_messages.cpp        # reuse MemberMap / GroupPlan / plan_dfs (extract shared helpers if needed)

include/fixpp/wire/
└── builder_validate.hpp     # NEW — generic runtime required-presence validate() over the emitted level-scoped tables (header-only, template over writer_traits)

build/<preset>/_codegen/include/fixpp/v44/
└── Builders.hpp             # GENERATED — build_<Msg>(out,args) + <Msg>Args + validate_<Msg> + writer_traits + level-scoped required tables

tests/session/
├── test_067_builder_shape_oracle.cpp   # NEW — generated==hand==golden for D/8/9/E/AS (headline)
├── test_067_builder_roundtrip.cpp      # NEW — seed-driven build->parse read-back + byte-structural asserts (all 33)
├── test_067_builder_validate.cpp       # NEW — required-presence top-level + group-entry depth
├── test_067_completeness.cpp           # NEW — exact-set gate over the 33 MsgTypes
└── golden/
    ├── gen/qf_mass_quote.cpp           # NEW (R5) — extra QuickFIX golden generator(s) for grouped insurance
    └── mass_quote.fix                  # NEW (R5) — checked-in grouped golden

tests/codegen/
└── test_067_emit_builders_unit.cpp     # NEW — emitter unit tests (order rule, exclusion set, group tables) — codegen source has no covering tests today
```

**Structure Decision**: Single-project library layout. The emitter grows by one `.cpp` (+ one `emit.hpp` decl + one `main.cpp` line); the runtime validate is one header; generated output is one new header per version; tests live beside the 061 harness in `tests/session/` (reusing `exemplar_seeds.hpp`, `shape_oracle_profile()`, golden-diff support) plus emitter unit tests in `tests/codegen/`. The 061 hand exemplars in `src/session/business_messages.cpp` are untouched (frozen oracle).

## Complexity Tracking

*No Constitution Check violations. No entries.*

## Open items carried to /tasks

- **Data-quality spot-check** (from R2): confirm group-child Required populates for a second grouped message beyond NewOrderList (e.g. MassQuote `NoQuoteEntries` members) before relying on `<Group>_rules` universally — a one-line generated-Validator inspection task.
- **R5 grouped goldens**: discrete task to add the MassQuote (and optionally AllocationInstruction) QuickFIX golden generator + checked-in `.fix`.
- **Shared-helper extraction**: `MemberMap`/`GroupPlan`/`plan_dfs` currently live `static` in `emit_messages.cpp`; /tasks decides extract-to-shared-header vs duplicate-small (prefer extract; keep the read emitter's behavior byte-identical — determinism golden must stay green).
- **kind→setter type map**: `kind_cpp_type` is `static` in `emit_messages.cpp`; the write emitter needs the type→`body_builder` overload mapping — share or re-derive (R7).
