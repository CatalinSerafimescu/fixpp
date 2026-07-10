# Implementation Plan: FR-015a-lite — Codegen Writer-Emitter

**Branch**: `067-codegen-writer-emitter` | **Date**: 2026-07-10 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/067-codegen-writer-emitter/spec.md`

## Summary

Add a codegen **write** surface to `tools/codegen/fixpp-codegen/`: a new `emit_builders(ir)` emitter producing `Builders.hpp` per version, with a generated `build_<Msg>(out, const <Msg>Args&)` free function per OFFICIAL application message that serializes through the single `wire::body_builder` core, plus a generated `validate_<Msg>(args)` required-presence routine over emitter-derived level-scoped presence tables. The pivotal correctness pin: for the 5 exemplar MsgTypes (D/8/9/E/AS in v44) the generated body is **byte-identical** to the frozen 061 hand exemplar AND the QuickFIX golden. Generate builders for all **33** OFFICIAL distinct MsgTypes (exact-set gate). Validation is required-presence ONLY (enum/conditional cut), recursive into group entries, as a SEPARATE step off the serialize path. No read-path / wire-semantics / C-ABI / Python change; no new error value.

Technical approach is fully derived in [research.md](./research.md): field-order two-regime rule (R1), required-presence tables need no IR add — order-independent from `m.fields` (R2), emit new level-scoped header-excluded **per-occurrence** tables not the flat `<Msg>_rules` (R3), free-function+args-struct API (R4), W/X paired + grouped QuickFIX goldens (R5), the exact 33 (R6), **per-message group plan + shared type/name helpers, NOT the read emitter's version-wide `MemberMap`** (R7), constitution triggers (R8), and the **codegen-local `MessageIR.group_order` declaration-order walk that supplies the group delimiter + member order (RC#7 — `m.fields` is tag-sorted so it cannot)** (R9).

## Technical Context

**Language/Version**: C++23 (Clang 22 local == CI, Article II §2). The codegen tool `fixpp-codegen` is C++; generated output is C++23 headers.
**Primary Dependencies**: existing `fixpp-codegen` IR + emitters (`emit_messages`, `emit_validator`, `gen_util`, `ir`); `wire::body_builder` (061, `include/fixpp/wire/body_builder.hpp`); `dict::reify()` / typed read flyweights (057/062/063) for round-trip read-back; QuickFIX-cpp offline golden generators (`tests/session/golden/gen/`, MIT, not in main build).
**Storage**: N/A (codegen emits headers into the build tree `build/<preset>/_codegen/include/fixpp/<ns>/`).
**Testing**: GoogleTest (`tests/session/`, `tests/wire/`, `tests/codegen/`); ctest; the sanitizer/coverage/static-analysis Tier-1 matrix; `/speckit-verify`.
**Target Platform**: Linux/Clang Tier 1 (gate); MSVC/libc++ Tier 2/3 CI.
**Project Type**: C++ library + codegen tool (single project; the library submodule).
**Performance Goals**: no new budget; outbound builder inherits `body_builder`'s zero-global-heap fixed arena. No regression to existing codegen/build benches.
**Constraints**: body-only output (INV-2); canonical decimals (INV-3); fail-closed atomic commit (INV-4); group grammar (INV-5); byte-identical shape-oracle; C-ABI 1.5.0 frozen (FR-009); Emitter-Lite (no `emit_enums`). **IR addition (RC#7/R9)**: a codegen-tool-local `MessageIR.group_order` (declaration-order XML walk) IS added — required for the group delimiter/member order because `m.fields` is tag-sorted; the required-presence tables still need no IR add (order-independent, R2/R3). This addition is entirely inside the codegen tool — **NO runtime `Dictionary`/`GroupRef`/C-ABI/Python change** (FR-009 intact).
**Scale/Scope**: 33 OFFICIAL MsgTypes × 1 representative namespace (v44). Emitter delta ≈ one new `emit_builders.cpp` + a runtime `validate` header + witness harness; generated header count +1 per version (`Builders.hpp`).

## Constitution Check

*GATE: evaluated pre-Phase-0 and re-checked post-design. PASS (no violations; no Complexity Tracking entries needed).*

| Article / Appendix | Applicability | Disposition |
|---|---|---|
| **Appendix A — triggers** | Wire format (validator changes) + Codegen layout | **All four controls required.** `/clarify` DONE; `/analyze` at step 6; **Codex Gate A after /plan**; user `/plan` sign-off pending. |
| **XVI §3 — /clarify mandatory** | wire + codegen touched | DONE (2 questions; `## Clarifications` recorded). |
| **X — ABI policy** | must stay unchanged | FR-009: no C-ABI symbol/signature/error-code change. The RC#7 `MessageIR.group_order` addition is **codegen-tool-local** (`ir.hpp`/`ir.cpp` + `emit_builders.cpp`) — it touches NO runtime `Dictionary`/`GroupRef`/C-ABI/Python symbol. Verify `nm`, abidiff, `check_capi_occupancy.sh`, `error_codes_v1.txt` unchanged; C-ABI 1.5.0 byte-identical. Art X clean. |
| **Error semantics (Appendix A)** | reuse existing wire error | `wire_required_field_missing`(=38) pre-exists; NO new `fixpp_error_t`. Not a trigger; verify no addition. |
| **VI — 100% FIX rule** | advances OFFICIAL write coverage | `## Normative References` section present in spec.md (added Gate A round 1 — the 33 rows' `[FIX44]`/catalogue refs + 061/063 design authority); /tasks MUST emit coverage-index + feature-catalogue close-out tasks for the 33 write rows BEFORE any row closes (see Open items); Gate A + Gate B before any row closes. |
| **VII — testing (TDD)** | new emitter + runtime + generated code | Tests-first: shape-oracle byte-equality, round-trip, fail-closed, exact-set completeness, validate-required (top-level + group depth). |
| **IX — coverage / sanitizers / static analysis** | emitter source + runtime harness | Full Tier-1 matrix. Generated headers coverage-excluded (`analyze_coverage.py`); emitter `.cpp` + runtime validate + harness are NOT excluded — must be covered. |
| **VIII — perf budgets** | outbound builder | inherits `body_builder` arena; no new budget; confirm no bench regression. |
| **XV — banned patterns** | no silent loss / no sync-hot-path-log | body_builder fail-closed (INV-4); no app-message drop; no new banned pattern. |
| **XVII §8 — verify gate** | before Gate B | `/speckit-verify` + feature-completeness audit non-failing precede Gate B; evidence in `.specify/decisions/067-*-verify.md`. |
| **XVIII — roadmap** | Emitter-Lite v1.0 slice | in-roadmap (typed-message v1.0); FR-015b/families/Orchestra explicitly out of scope (spec Out of Scope). |

**RC#7 IR-addition note (Gate A round 2):** the round-1 "no IR addition" premise is CORRECTED — a codegen-tool-local `MessageIR.group_order` (declaration-order XML walk in `ir.cpp`) IS added, because `MessageIR.fields` is loader-tag-sorted (`xml_loader.cpp:695-702`) and so cannot supply the group delimiter/member order (R9). This is NOT a Constitution violation: it is host-build-tool-only, adds no runtime type/symbol, and leaves FR-009 (C-ABI 1.5.0 freeze, read path, Python) byte-identical. The required-presence tables are unaffected (still order-independent from `m.fields`, R2/R3). No Complexity Tracking entry needed (no new abstraction, no cross-layer edge — the walk stays inside the codegen tool).

**Codegen-specific mandatory controls (from prior codegen features 003/057/062/063):** forced codegen regeneration + the codegen build-graph cleanliness gate (`git`-clean after regen; `feedback_codegen_build_graph_cleanliness_gate`); re-index CodeGraph after code-changing phases; watch codegen emitter staleness in non-debug build dirs (`project_codegen_emitter_staleness`) — sanitizer/coverage builds compile a fresh `_codegen`, so regenerate before those runs.

## Project Structure

### Documentation (this feature)

```text
specs/067-codegen-writer-emitter/
├── plan.md              # this file
├── research.md          # Phase 0 (R1–R9)
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
├── ir.hpp                   # + MessageIR.group_order (NEW codegen-local field: per-occurrence delimiter + declaration-ordered members, recursive) — RC#7/R9
├── ir.cpp                   # + codegen-tool-local pugixml RE-PARSE of xml_path populating group_order (NEW pugixml dep in this TU — build_ir owns xml_path ir.cpp:67-69 but NOT the parsed tree; pugixml is TU-local to xml_loader.cpp/D-15, so this is NOT the runtime loader tree/accessor); does NOT tag-sort/dedup — RC#7/R9
├── main.cpp                 # + write_file(base/"Builders.hpp", emit_builders(ir))
├── gen_util.hpp             # reuse kind_of / to_accessor / to_identifier (+ maybe a shared kind→setter map)
├── emit_messages.cpp        # reuse type/name helpers only (kind_of/to_accessor/to_identifier); the group planner is per-message in emit_builders.cpp, delimiter/order from group_order (R9), NOT m.fields, NOT the version-wide MemberMap (RC#1)
└── CMakeLists.txt           # + find_package(pugixml CONFIG REQUIRED) + target_link_libraries(fixpp-codegen PRIVATE pugixml::pugixml) — build home for ir.cpp's NEW re-parse dep (fixpp-codegen currently links only fixpp::dictionary at :30, and fixpp_dictionary's pugixml link is PRIVATE so it does NOT propagate); build-only host-tool dep, PRIVATE, never installed (mirrors this file's own never-installed framing, CMakeLists:36-38), zero ABI surface — RC-B/RC#7

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
└── test_067_emit_builders_unit.cpp     # NEW — emitter unit tests: order rule, exclusion set, the RC#1 pin (ONE no_tag 268 → DISTINCT per-message plans in W vs X), AND the RC#7 group_order pin — the ir.cpp declaration-order XML walk captures W delimiter=269 vs X delimiter=279 (NOT tag-sorted 269 for both) and E NoOrders member order 55-before-54 (NOT tag-sorted 54 55); codegen source has no covering tests today
```

**Structure Decision**: Single-project library layout. The emitter grows by one `.cpp` (+ one `emit.hpp` decl + one `main.cpp` line); the runtime validate is one header; generated output is one new header per version; tests live beside the 061 harness in `tests/session/` (reusing `exemplar_seeds.hpp`, `shape_oracle_profile()`, golden-diff support) plus emitter unit tests in `tests/codegen/`. The 061 hand exemplars in `src/session/business_messages.cpp` are untouched (frozen oracle).

## Complexity Tracking

*No Constitution Check violations. No entries.*

## Shared-`no_tag` census (RC#1 — /plan work, not deferred)

The write group model is per-occurrence (research R7/R3); a version-wide plan per `no_tag` is unsound. Enumerate every group `no_tag` that appears in ≥2 of the 33 OFFICIAL messages with a **divergent member set / delimiter / required set**, so the per-occurrence pin covers them:

- **CONFIRMED: `NoMDEntries(268)`** — W/`MDFullGrp` (`FIX44.xml:3023`, delimiter `MDEntryType(269)`, required 269) vs X/`MDIncGrp` (`FIX44.xml:3060`, delimiter `MDUpdateAction(279)`, required 279, 269 demoted to optional). This is the RC#1 pin (W/X paired goldens + the emitter unit test).
- Other shared `no_tag`s across the 33 (e.g. `NoPartyIDs(453)`, `NoQuoteEntries`, `NoRelatedSym`) are the same mechanism; the emitter unit test asserts the planner is per-message so any divergent occurrence is handled by construction, not by an enumerated allow-list. /tasks emits a task to dump the per-message group plans and confirm no version-wide collapse.

### N3 — `append_run` tag-dedup collapse census (RC#7-adjacent, /tasks must run)

`append_run` (`xml_loader.cpp:695-702`) sorts the WHOLE per-message run by tag and then `unique`-dedups by tag (first-seen). If one tag appears in **two contexts within a single message** — e.g. the same tag both top-level and inside a group, or in two different groups of one message — the dedup collapses them into ONE `FieldRef`, keeping a single `group_no_tag`. Two exposures:
- **Group membership/order view**: side-stepped by design — the RC#7 `group_order` XML walk does NOT tag-dedup, so it captures every member at its true level/order. `group_order` is the authority for group delimiter + membership + order.
- **The required SET derived from the deduped `m.fields`** (top-level body set AND per-occurrence group set) COULD be missing a field the dedup dropped to the other level. This is NOT solved here — it is a mandatory /tasks census: enumerate the 33 OFFICIAL messages for any tag appearing at ≥2 levels within one message and confirm the `m.fields`-derived required sets miss nothing (if any collapse is found, source the affected required set from **the codegen-local declaration walk, extended to carry per-member `rule` (and a top-level declaration set)** — NOT from `group_order`, which carries member ORDER not `rule` and cannot recover a collapsed *top-level* field). This census is a HARD /tasks task — flag, don't hand-wave.

## Open items carried to /tasks

- **Data-quality spot-check** (from R2): confirm group-child Required populates for a second grouped message beyond NewOrderList (e.g. MassQuote `NoQuoteEntries` members) before relying on the per-occurrence group required tables universally — a one-line generated-Validator inspection task.
- **R5 / RC#1 grouped goldens**: discrete task to add the **W + X** QuickFIX golden generator + checked-in `.fix` pair (the 268 discriminator), plus MassQuote (optionally AllocationInstruction).
- **Shared-helper extraction (DECIDED — research R7)**: shared = `kind_of`/`to_accessor`/`to_identifier` (type/name only), extracted or reused as-is; the **group planner + required tables are rebuilt PER-MESSAGE in `emit_builders.cpp`, NOT the read emitter's version-wide `MemberMap`** (RC#1). /tasks only decides the mechanical extract-to-shared-header vs duplicate-small for the type/name helpers; it does NOT re-open the per-message group decision. Keep the read emitter's determinism golden green as the guard.
- **kind→setter type map**: `kind_cpp_type` is `static` in `emit_messages.cpp`; the write emitter needs the type→`body_builder` overload mapping including **Bool→char `Y`/`N`** and **Length+Data coupling** (FR-007a) — share or re-derive (R7).
- **Art VI close-out tasks (RC#4, `feedback_speckit_tasks_must_emit_closeout_tasks`)**: /tasks MUST emit explicit tasks to (a) update `spec/coverage-index.md` + `spec/feature-catalogue.md` for the 33 write-coverage rows' disposition BEFORE any row closes, (b) confirm the spec `## Normative References` section cites the coverage-index rows, and (c) resolve the Art VI §2 canonical-format question for these application-message rows — either add `[DocAbbrev §X.Y.Z]` section-granular refs OR record them as `[impl]`/design-authority rows per Art VI §3 — before any row closes (the coverage-index carries only message-level refs across the whole FIX44 application-message domain today, a pre-existing project-wide convention; see Gate A Round 3 disagreement). These are not optional bookkeeping.
- **pugixml codegen build home (RC-B)**: /tasks emits a task to add `find_package(pugixml CONFIG REQUIRED)` + `target_link_libraries(fixpp-codegen PRIVATE pugixml::pugixml)` to `tools/codegen/fixpp-codegen/CMakeLists.txt` (build-only host-tool dep — PRIVATE, never installed, zero ABI surface; `fixpp_dictionary`'s pugixml link is PRIVATE and does NOT propagate to `fixpp-codegen`), and to VERIFY the bootstrap codegen build still configures/links cleanly with the new dep (`ir.cpp`'s `#include <pugixml.hpp>` compiles and `fixpp-codegen` links) before any generated row closes.
- **`check_layers.py` on the new wire header (New #4, advisory)**: /implement runs `tools/check_layers.py` on `include/fixpp/wire/builder_validate.hpp` to confirm no `wire→codegen`/`wire→dict` include edge is introduced (the header is generic over `writer_traits`; delimiter/rule data is baked into generated tables at codegen time — `feedback_gate_b_check_layers_post_fixer`).

## Gate A

### Round 1 — disagreements

- **Codex #3 (args model "optional group absent" vs "present N==0") DOWNGRADED P1 → P2 by the Opus adversarial pass.** Reason (Opus, golden-byte evidence): the plain `std::span` model DOES reproduce all 5 exemplar goldens — the hand exemplars always open the party group (`src/session/business_messages.cpp:250-252`, `entry->group_begin(453,448)` unconditionally; golden `new_order_list.fix` emits present-empty `453=0`), so it is NOT a headline/G2 byte-equality blocker. The genuine defect is narrower: the "entirely absent" case (`spec.md` Edge Cases + 061 C3) is unexpressible with a plain span. Recorded as RC#2 and fixed by `std::optional<std::span>` for optional groups — a localized representational fix, not a headline pin. (Codex framed it P1; the downgrade is evidence-based, not a dismissal — the fix is still applied.)

### Rounds

- **Round 1 applied 2026-07-10: Codex P1=5 P2=5 P3=1; Opus post-judging P1=3 P2=6 P3=3; rewrite addresses root causes RC#1 (per-occurrence group model — overturns R7 union premise), RC#2 (optional-group absent vs N==0), RC#3 (Bool Y/N + Length+Data), RC#4 (Normative References + Art VI close-out), RC#5 (FR/SC/completeness-gate reconcile), RC#6 (generated-wrapper fail-closed home). Reviews: research/reviews/codex_067-codegen-writer-emitter_gate_a_review.md, research/reviews/opus_067-codegen-writer-emitter_gate_a_adversarial_review.md.**
- Round 3 applied 2026-07-10: Codex P1=2 P2=1 P3=2; Opus post-judging P1=1 P2=2 P3=3; doc-only convergence rewrite addresses root causes RC#1 (stale <Msg>_rules-reuse fragments), RC#2 (group_order access = codegen-tool-local pugixml re-parse of xml_path, not "raw XML in hand"), RC#3 (required-set m.fields MUST softened + fallback authority + N3 census as hard /tasks task), RC#4 (cosmetic: R1–R9, failclosed quickstart line, Art VI coverage-index close-out note). Reviews: research/reviews/codex_067-codegen-writer-emitter_gate_a_3_review.md, research/reviews/opus_067-codegen-writer-emitter_gate_a_3_adversarial_review.md.
- Round 4 applied 2026-07-10: Codex P1=1 P2=1 P3=0; Opus post-judging P1=0 P2=2 P3=1; doc/build-graph-only convergence rewrite addresses RC-A (plan.md:141 stale XmlLoader-mechanism fragment → pugixml re-parse), RC-B (add tools/codegen/fixpp-codegen/CMakeLists.txt pugixml build home to Project Structure + /tasks configure-verify item), P3 (FR-007 N3 cross-reference). Reviews: research/reviews/codex_067-codegen-writer-emitter_gate_a_4_review.md, research/reviews/opus_067-codegen-writer-emitter_gate_a_4_adversarial_review.md.
- **Round 5 — CONVERGED 2026-07-10 (user-signed-off; converged commit `a4cb5624`): Codex P1=0 P2=0 P3=1; Opus post-judging P1=0 P2=0 P3=1.** No rewrite (convergence-confirmation pass). The lone P3 (stale `tools/codegen/fixpp-codegen/CMakeLists.txt:3-5` "no second QuickFIX-XML parser" banner comment) rides to `/tasks` — folded into T003 (RC-B build-home task). Convergence bar (P1==0 ∧ P2==0) met. Reviews: research/reviews/codex_067-codegen-writer-emitter_gate_a_5_review.md, research/reviews/opus_067-codegen-writer-emitter_gate_a_5_adversarial_review.md.

### Round 2 — RC#7 (re-/plan)

- **Round 2 (2026-07-10): Codex P1=1 P2=0 P3=1; Opus adversarial pass concurring.** The confirmed P1 (RC#7) is a **structural defect in the round-1 rewrite itself**: R1/R7 had the write emitter derive each group's delimiter + member order from `MessageIR.fields`, but the loader **tag-sorts + tag-dedups** the per-message run before it reaches the IR (`xml_loader.cpp:695-702`, copied `ir.cpp:98-100`) — so `m.fields` is tag-sorted and declaration order is LOST. This breaks (a) the group **delimiter** (X `NoMDEntries` delimiter must be `MDUpdateAction(279)` but tag-sort surfaces the optional `MDEntryType(269)` first since 269 < 279 — `FIX44.xml:3060-3061` vs W's genuine 269 delimiter at `:3023-3024`), and (b) group-entry **ORDER** on an exemplar (golden E `new_order_list.fix` `NoOrders` is `55` before `54`; tag-sort inverts to `54 55`), failing the headline FR-003/G2/SC-002 byte-equality pin.
- **Loop exited to re-/plan (user 2026-07-10).** The round-1 RC#1–6 fixes are CORRECT and preserved; only the group-order mechanism (RC#7) + two residuals (the RC#5 P3 US1-Independent-Test "typed field setters" wording; the N3 `append_run` dedup census) are re-derived.
- **Fix = codegen-local `MessageIR.group_order`** (research R9): a codegen-tool-local **pugixml re-parse** of `xml_path` in `ir.cpp` (`build_ir` owns `xml_path` at `ir.cpp:67-69`, but `XmlLoader` yields only the tag-sorted `Dictionary`, not the raw parsed tree — pugixml is a NEW codegen-tool dep, NOT a runtime loader/tree accessor; Round 2 first framed this as a raw-XML walk owning its own `XmlLoader`, corrected in Round 3/4 to the re-parse — aligns with `plan.md:70` / `research.md:142` / `data-model.md:72`) captures, per (message, group-occurrence), the delimiter (first declared member) + members in declaration order, recursively, resolving THIS message's own `<component>` refs. The global `Dictionary`/`GroupRef`/`group_fields` cannot serve this — they are `no_tag`-deduped first-seen (`xml_loader.cpp:485-486`; L-063-3) and cannot distinguish W's vs X's `NoMDEntries`; 063 deferred the delimiter (`specs/063-nested-group-parse-correctness/tasks.md:62`). The required-presence tables are UNAFFECTED (order-independent, still from `m.fields`, R2/R3). The addition is codegen-tool-local: NO runtime `Dictionary`/`GroupRef`/C-ABI/Python change (FR-009 intact). This FALSIFIES the round-1 "no IR addition" premise for the delimiter/order — corrected across research R2/R7/R9, data-model §3, this plan, spec FR-006a/FR-007.

### Round 3 — disagreements

- **Codex #2 (Art VI traceability, `spec.md:150`) DOWNGRADED P1 → P3 by the Opus adversarial pass.** Codex framed the message-level (non-`[DocAbbrev §X.Y.Z]`) Normative References as a P1 Gate-A blocker. The Opus pass downgraded to P3 because the coverage-index carries zero §-granular refs for the entire FIX44 application-message domain (a pre-existing project-wide convention, not a 067 regression), `plan.md:34` already hedges (it does not claim "clean" — it schedules the close-out), and the pre-close reconciliation is already a scheduled /tasks task (Art VI close-out, Open items). Do NOT apply Codex's heavier Normative-References rewrite; the light-touch close-out note (Art VI close-out (c) above) — which now requires /tasks to resolve the §2 `[DocAbbrev §X.Y.Z]`-vs-`[impl]` canonical-format question before any row closes — suffices.
