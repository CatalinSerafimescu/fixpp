# Implementation Plan: FR-015a-lite — Codegen Writer-Emitter

**Branch**: `067-codegen-writer-emitter` | **Date**: 2026-07-10 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/067-codegen-writer-emitter/spec.md`

## Summary

Add a codegen **write** surface to `tools/codegen/fixpp-codegen/`: a new `emit_builders(ir)` emitter producing `Builders.hpp` per version, with a generated `build_<Msg>(out, const <Msg>Args&)` free function per OFFICIAL application message that serializes through the single `wire::body_builder` core, plus a generated `validate_<Msg>(args)` required-presence routine over emitter-derived level-scoped presence tables. The pivotal correctness pin: for the 5 exemplar MsgTypes (D/8/9/E/AS in v44) the generated body is **byte-identical** to the frozen 061 hand exemplar AND the QuickFIX golden. Generate builders for all **33** OFFICIAL distinct MsgTypes (exact-set gate). Validation is required-presence ONLY (enum/conditional cut), recursive into group entries, as a SEPARATE step off the serialize path. No read-path / wire-semantics / C-ABI / Python change; no new error value.

Technical approach is fully derived in [research.md](./research.md): field-order two-regime rule (R1), IR carries group-child presence (R2), emit new level-scoped header-excluded **per-occurrence** tables not the flat `<Msg>_rules` (R3), free-function+args-struct API (R4), W/X paired + grouped QuickFIX goldens (R5), the exact 33 (R6), **per-message group plan + shared type/name helpers, NOT the read emitter's version-wide `MemberMap`** (R7), constitution triggers (R8).

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
| **VI — 100% FIX rule** | advances OFFICIAL write coverage | `## Normative References` section present in spec.md (added Gate A round 1 — the 33 rows' `[FIX44]`/catalogue refs + 061/063 design authority); /tasks MUST emit coverage-index + feature-catalogue close-out tasks for the 33 write rows BEFORE any row closes (see Open items); Gate A + Gate B before any row closes. |
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
└── emit_messages.cpp        # reuse type/name helpers only (kind_of/to_accessor/to_identifier); the group planner is per-message in emit_builders.cpp, NOT the version-wide MemberMap (RC#1)

include/fixpp/wire/
└── builder_validate.hpp     # NEW — generic runtime required-presence validate() over the emitted level-scoped tables (header-only, template over writer_traits)

build/<preset>/_codegen/include/fixpp/v44/
└── Builders.hpp             # GENERATED — build_<Msg>(out,args) + <Msg>Args + validate_<Msg> + writer_traits + level-scoped required tables

tests/session/
├── test_067_builder_shape_oracle.cpp   # NEW — generated==hand==golden for D/8/9/E/AS (headline)
├── test_067_builder_roundtrip.cpp      # NEW — seed-driven build->parse read-back + byte-structural asserts (all 33)
├── test_067_builder_validate.cpp       # NEW — required-presence top-level + per-occurrence group-entry depth (incl. W vs X 268 required-set divergence)
├── test_067_builder_failclosed.cpp     # NEW (RC#6/G9) — generated-wrapper fail-closed: undersized out untouched, SOH/control byte, Bool Y/N, Length+Data coupling, required-group-zero rejected, W-vs-X per-occurrence delimiter discrimination
├── test_067_completeness.cpp           # NEW — exact-set gate over the 33 MsgTypes (MsgType→builder registry keys, multi-char incl.)
└── golden/
    ├── gen/qf_market_data.cpp          # NEW (R5/RC#1) — QuickFIX golden generator for W + X (the paired 268 discriminator)
    ├── market_data_snapshot.fix        # NEW (R5/RC#1) — checked-in W golden (NoMDEntries delimiter 269)
    ├── market_data_incremental.fix     # NEW (R5/RC#1) — checked-in X golden (NoMDEntries delimiter 279)
    ├── gen/qf_mass_quote.cpp           # NEW (R5) — extra QuickFIX golden generator for grouped insurance
    └── mass_quote.fix                  # NEW (R5) — checked-in grouped golden

tests/codegen/
└── test_067_emit_builders_unit.cpp     # NEW — emitter unit tests: order rule, exclusion set, and the RC#1 pin — ONE no_tag (268) yields DISTINCT per-message delimiter/member-order/required plans in W vs X (a version-wide plan is impossible); codegen source has no covering tests today
```

**Structure Decision**: Single-project library layout. The emitter grows by one `.cpp` (+ one `emit.hpp` decl + one `main.cpp` line); the runtime validate is one header; generated output is one new header per version; tests live beside the 061 harness in `tests/session/` (reusing `exemplar_seeds.hpp`, `shape_oracle_profile()`, golden-diff support) plus emitter unit tests in `tests/codegen/`. The 061 hand exemplars in `src/session/business_messages.cpp` are untouched (frozen oracle).

## Complexity Tracking

*No Constitution Check violations. No entries.*

## Shared-`no_tag` census (RC#1 — /plan work, not deferred)

The write group model is per-occurrence (research R7/R3); a version-wide plan per `no_tag` is unsound. Enumerate every group `no_tag` that appears in ≥2 of the 33 OFFICIAL messages with a **divergent member set / delimiter / required set**, so the per-occurrence pin covers them:

- **CONFIRMED: `NoMDEntries(268)`** — W/`MDFullGrp` (`FIX44.xml:3023`, delimiter `MDEntryType(269)`, required 269) vs X/`MDIncGrp` (`FIX44.xml:3060`, delimiter `MDUpdateAction(279)`, required 279, 269 demoted to optional). This is the RC#1 pin (W/X paired goldens + the emitter unit test).
- Other shared `no_tag`s across the 33 (e.g. `NoPartyIDs(453)`, `NoQuoteEntries`, `NoRelatedSym`) are the same mechanism; the emitter unit test asserts the planner is per-message so any divergent occurrence is handled by construction, not by an enumerated allow-list. /tasks emits a task to dump the per-message group plans and confirm no version-wide collapse.

## Open items carried to /tasks

- **Data-quality spot-check** (from R2): confirm group-child Required populates for a second grouped message beyond NewOrderList (e.g. MassQuote `NoQuoteEntries` members) before relying on the per-occurrence group required tables universally — a one-line generated-Validator inspection task.
- **R5 / RC#1 grouped goldens**: discrete task to add the **W + X** QuickFIX golden generator + checked-in `.fix` pair (the 268 discriminator), plus MassQuote (optionally AllocationInstruction).
- **Shared-helper extraction (DECIDED — research R7)**: shared = `kind_of`/`to_accessor`/`to_identifier` (type/name only), extracted or reused as-is; the **group planner + required tables are rebuilt PER-MESSAGE in `emit_builders.cpp`, NOT the read emitter's version-wide `MemberMap`** (RC#1). /tasks only decides the mechanical extract-to-shared-header vs duplicate-small for the type/name helpers; it does NOT re-open the per-message group decision. Keep the read emitter's determinism golden green as the guard.
- **kind→setter type map**: `kind_cpp_type` is `static` in `emit_messages.cpp`; the write emitter needs the type→`body_builder` overload mapping including **Bool→char `Y`/`N`** and **Length+Data coupling** (FR-007a) — share or re-derive (R7).
- **Art VI close-out tasks (RC#4, `feedback_speckit_tasks_must_emit_closeout_tasks`)**: /tasks MUST emit explicit tasks to (a) update `spec/coverage-index.md` + `spec/feature-catalogue.md` for the 33 write-coverage rows' disposition BEFORE any row closes, and (b) confirm the spec `## Normative References` section cites the coverage-index rows. These are not optional bookkeeping.
- **`check_layers.py` on the new wire header (New #4, advisory)**: /implement runs `tools/check_layers.py` on `include/fixpp/wire/builder_validate.hpp` to confirm no `wire→codegen`/`wire→dict` include edge is introduced (the header is generic over `writer_traits`; delimiter/rule data is baked into generated tables at codegen time — `feedback_gate_b_check_layers_post_fixer`).

## Gate A

### Round 1 — disagreements

- **Codex #3 (args model "optional group absent" vs "present N==0") DOWNGRADED P1 → P2 by the Opus adversarial pass.** Reason (Opus, golden-byte evidence): the plain `std::span` model DOES reproduce all 5 exemplar goldens — the hand exemplars always open the party group (`src/session/business_messages.cpp:250-252`, `entry->group_begin(453,448)` unconditionally; golden `new_order_list.fix` emits present-empty `453=0`), so it is NOT a headline/G2 byte-equality blocker. The genuine defect is narrower: the "entirely absent" case (`spec.md` Edge Cases + 061 C3) is unexpressible with a plain span. Recorded as RC#2 and fixed by `std::optional<std::span>` for optional groups — a localized representational fix, not a headline pin. (Codex framed it P1; the downgrade is evidence-based, not a dismissal — the fix is still applied.)

### Rounds

- **Round 1 applied 2026-07-10: Codex P1=5 P2=5 P3=1; Opus post-judging P1=3 P2=6 P3=3; rewrite addresses root causes RC#1 (per-occurrence group model — overturns R7 union premise), RC#2 (optional-group absent vs N==0), RC#3 (Bool Y/N + Length+Data), RC#4 (Normative References + Art VI close-out), RC#5 (FR/SC/completeness-gate reconcile), RC#6 (generated-wrapper fail-closed home). Reviews: research/reviews/codex_067-codegen-writer-emitter_gate_a_review.md, research/reviews/opus_067-codegen-writer-emitter_gate_a_adversarial_review.md.**
