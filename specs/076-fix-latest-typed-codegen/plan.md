# Implementation Plan: FIX Latest Typed Message Classes via Native Orchestra Codegen (`fixpp::vlatest` tier)

**Branch**: `076-fix-latest-typed-codegen` | **Date**: 2026-07-15 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/076-fix-latest-typed-codegen/spec.md`

## Summary

Generate the typed message layer for FIX Latest (EP303) into a new, distinct `fixpp::vlatest` namespace, using the existing 067/069 codegen emitter fed by 074's native `OrchestraLoader`. **Scope (Option A, per 067/069 precedent):** typed **read/reify/args classes for all 181** messages (universal, `ir.ns`-keyed), and typed **`build_<Msg>`/`validate_<Msg>` builders for the application subset** of the 181 (app-scoped, exactly as 069's emitter scopes builders). Only `emit_messages`/`emit_validator`/reify namespacing is free (the emitter partitions on `session_version`, kept distinct for `vlatest` by 074). The **dominant logic change is an Orchestra-native IR projection in `ir.cpp`** (RC-A): the existing `populate_group_order` (`ir.cpp:145-197`) is `<fix>`-schema-hardcoded (`doc.child("fix")` `:158`, `msgcat` `:186`) and NO-OPS on an Orchestra root, so `group_order`, `header_trailer_tags`, and `is_application` are all empty/false for the 181 unless a sibling `fixr:`-schema projection produces them. That projection (research R2b) yields FOUR outputs — declaration-order `group_order`, `header_trailer_tags`, a **category→`is_application`** mapping (Orchestra has `category=`, NOT `msgcat`; verified single-category rule: the 8 `category="Session"` frames {0,1,2,3,4,5,A,n} = admin, every other category = app — NB `category="Testing"` is application algo-cert/test-suite messages, not session admin; fail-closed on an unmapped category, exact-set-pinned admin complement), and the **lossless occurrence list** feeding the census manifest. The remaining work: (a) the `OrchestraLoader` load branch, (b) a `kCodegenVersions` `vlatest` row + `app_version_enum` special-case, (c) widening the `emit_builders.cpp:646` gate with a **vlatest coverage predicate** that does NOT reuse the FIX44-specific `kOfficial33`/`kN002N003Excluded` sets, (d) a fifth per-version codegen invocation behind a build option that **defaults ON**, and (e) proving completeness with a **non-circular** raw-XML census (message + full field-set, all depths) whose walker shares no code with the projection (N-1 double-entry) since FIX Latest has no QuickFIX peer. Application-version auto-dispatch reachability (the single generated surface `dispatch_application`, emitted into `_dispatch/reify_dispatch_application.hpp` — the reify application switch called by `dict::reify`) is out of scope — wiring it forces the injective wire-ApplVerID break that is the deferred ApplExtID(1156)=303 feature's problem. **Verified (World A, code-read):** the typed validate/reify/round-trip path is INDEPENDENT of that deferred re-keying — the generated validator is `constexpr` tables, the runtime validator takes its `table_view` as a caller-supplied ctor argument, and reify is dict-free (`version_v = v50sp2` is inert). So this feature does not need `version_registry` re-keying; it stops cleanly at the caller-supplied-dict typed API.

## Technical Context

**Language/Version**: C++23 (library); the codegen tool is C++23 host code under `tools/codegen/fixpp-codegen/`.

**Primary Dependencies**: existing in-tree only — the 067/069 codegen emitter (`ir.cpp`, `emit_messages.cpp`, `emit_validator.cpp`, `emit_dispatch.cpp`, `gen_util.hpp`, `main.cpp`), 074's `dict::OrchestraLoader` (pugixml), the runtime `Dictionary`/`table_view`, the `wire::body_builder` / typed-args machinery. No new third-party dependency.

**Storage**: N/A. Input is the already-vendored, pinned `dictionaries/orchestra/OrchestraFIXLatest.xml` (EP303, 181 messages). Output is generated headers under the build tree (`${CMAKE_BINARY_DIR}/_codegen/include/fixpp/vlatest/`) plus a checked-in codegen golden.

**Testing**: gtest + ctest, whole-binary grouped per Article VII §8, selected via `ctest -L`. New surfaces: the non-circular completeness census, the 181-message typed round-trip, the build-option ON/OFF matrix behavior, and the codegen determinism golden for the `vlatest` tier.

**Target Platform**: Linux (clang/gcc) Tier-1/Tier-3 + Windows MSVC Tier-2, per the standard CI matrix. Codegen runs at CMake configure time (host), identical to the existing four-version invocation.

**Project Type**: C++23 FIX engine library + its offline codegen tool. Single project (Option 1).

**Performance Goals**: N/A for runtime hot path — this feature adds no runtime code to `parse`/`validate`/session send-recv beyond the generated per-message typed builders/validators, which mirror the existing legacy tiers. The **relevant cost is build-time / binary-size** (compile of 181 additional classes across the sanitizer/preset matrix). **Clarification disposition (build-cost measurement):** the spec's "`/plan` MUST measure the compile/binary delta" is satisfied here as an **ex-ante estimate** (≈ one `v50sp2`-sized tier; 181 vs 156 messages — research.md R6) with the **precise measurement deferred to implement task T1** — the code being measured does not exist until implementation, so a real number is unobtainable at `/plan`. The ON default holds unless T1's measurement is surprising, in which case it is re-raised with the user before merge. No new hot-path perf budget (Article VIII §3) is engaged.

**Constraints**: (1) **Zero C-ABI / Python / link-ABI change** — C-ABI GA-frozen at 1.5.0 (FR-008, SC-004). (2) **Strictly additive** — v42/v44/v50sp2/vt11 generated output byte-identical with the option ON or OFF (FR-004, SC-003). (3) **Build option defaults ON**, full matrix on the `vlatest` tier, both ON+OFF paths in CI (FR-003). (4) **Injective wire-ApplVerID map preserved** — no `vlatest` wiring into the single generated application-version auto-dispatch surface `dispatch_application` (emitted into `_dispatch/reify_dispatch_application.hpp`; the generated validator is `constexpr` tables with no dispatch switch), which would collide `vlatest` onto v50sp2 (FR-009, SC-005). (5) **Non-circular exact-set census mandatory** (FR-006).

**Scale/Scope**: 181 generated read/reify/args classes + app-subset builders in one new namespace; ~6 codegen-tool touch points, of which the **`ir.cpp` Orchestra-native IR projection (RC-A) is the dominant, highest-risk one** (a `fixr:`-schema sibling to `populate_group_order` producing group_order + header_trailer_tags + category→is_application + lossless occurrence list) — the rest are `ir.cpp` load branch + `kCodegenVersions` row, `gen_util.hpp` `app_version_enum` special-case, `emit_builders.cpp` gate-widening with a vlatest coverage predicate (app-subset), a new `emit_manifest` census-manifest emitter + `main.cpp` write line, `cmake/Codegen.cmake` fifth invocation + build option + OFF-path stale-dir cleanup; 1 new census test (independent walker), 1 round-trip test, determinism/legacy-golden extension (incl. the manifest, under `specs/076-fix-latest-typed-codegen/contracts/golden/`).

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

### Amendment required (rides this feature's Gate A)

- **Article I §1 — FIX Latest carve-out MUST be narrowed (MINOR bump v0.7 → v0.8).** The current text (v0.7) reads: *"FIX Latest (typed-codegen / session-negotiation tiers) … are post-1.0 milestones"* and *"only its typed-codegen, ApplExtID(1156)=303 differentiation, and session-negotiation tiers remain post-1.0."* This feature **delivers the FIX Latest typed-codegen tier for v1.0**, which contradicts that carve-out. The amendment reclassifies **"typed codegen"** from post-1.0 to **v1.0-delivered-by-076**, leaving only **ApplExtID(1156)=303 differentiation + session negotiation** as post-1.0.
  - **Bump class**: MINOR (additive version-tier reclassification; no banned-pattern / perf-budget / config change → not v-major per Article XX §4). Identical in kind to v0.6 (074) and v0.7 (075).
  - **§XVIII.5 disposition**: NO residual conflict — §5 bars early-shipping deferred **protocols** (SOFH/SBE/FIXP/FAST/JSON/GPB/MMT); typed codegen over an already-supported dictionary is not a protocol. The amendment is what makes the scope cease to be deferred, not a §5 waiver.
  - **§XVIII.6/§7 disposition**: §6 defers per-version codegen for the *runtime-XML-only* legacy versions (4.0/4.1/4.3/5.0/5.0SP1); FIX Latest is Orchestra-native, not runtime-XML-only, so §6 does not bar it. §7 (v0.5) governs the v44 app-message *family* set; FIX Latest is a distinct version namespace governed by Article I §1, not §7 — no §7 change needed.
  - **Process**: this feature is an Appendix-A codegen-trigger feature → **Codex Gate A required**. Per the established Gate-A-fold deviation from Article XX §2's standalone-PR letter (precedents 035/043/068/069/074/075), the mechanical amendment text + Sync Impact Report **rides this feature's branch/Gate A** — do NOT run `/speckit-constitution` standalone (constitution is symlink-single-sourced). User sign-off at Gate A. This is the amendment the 2026-07-15 v1.0 SCOPE DECISION pre-authorized for the first catalogue-closure feature.

- **Article XVIII §2 — locked v1.2 roadmap line MUST be reconciled (same v0.7 → v0.8 MINOR bump, folded into the same amendment).** The locked roadmap entry (constitution `.specify/constitution.md:339`) reads: *"**v1.2 — FIX Latest application messages** (new MsgTypes **A-035..A-065** + EP-level field additions to existing messages, per coverage-index Post-1.0 Gap Registry)."* Feature 076 delivers the typed classes for **A-035..A-065** (and the EP-augmented existing FIX Latest messages, all in the `fixpp::vlatest` namespace) **in v1.0** — directly moving those MsgTypes off the v1.2 line. Article XVIII §4 makes roadmap changes Article-XX amendments (*"Re-ordering, additions, removals all require Article XX."*), and leaving §2 unamended makes the constitution **internally contradictory** post-merge (Article I §1 would say typed codegen is v1.0 while §2 still schedules the messages for v1.2).
  - **Annotation text** (append to line 339): *"— NOTE: the new-MsgType typed classes A-035..A-065 are delivered in the `fixpp::vlatest` tier in v1.0 by feature 076; only (a) EP-level field additions back-ported into the legacy `v44`/`v50sp2` dictionaries (coverage-index Post-1.0 Gap Registry, line 699) and (b) ApplExtID(1156)=303 on-wire differentiation remain post-1.0."* **Scope precision**: 076 does NOT back-port EP fields into the legacy dicts — that is the distinct Post-1.0 Gap at `coverage-index.md:699`, which stays intact and deferred. Only the FIX-Latest-namespace typed-class delivery moves to v1.0.
  - **Sync Impact Report**: MUST list **BOTH** Article I §1 (lines 63/65) **AND** Article XVIII §2 (line 339) as amended in the same v0.8 bump, per Article XVIII §4. A report citing only Article I §1 leaves the contradiction.

### Compliant (no change)

- **Article I §3/§4 (Master Feature Catalogue / no silent omissions)** — PASS, advanced. This feature moves catalogue rows A-035..A-065 (FIX Latest typed) toward `done`; the exact-set census enforces "no silent omissions" at field granularity.
- **C-ABI freeze (1.5.0)** — PASS. FR-008/SC-004: zero C-ABI / Python / link-ABI change; verified by the existing ABI-golden gate.
- **Article VIII §3 (hot-path perf needs a bench)** — NOT ENGAGED. Additive codegen; no hot-path change. The build-cost measurement owed by the spec is a scope-cost input (research.md R6), not a runtime perf budget.
- **Article VII §8 (test grouping)** — PASS by construction: new tests are whole-binary grouped, `ctest -L`-selected, no `gtest_discover_tests` buckets.
- **Testing rigor (mutation-discriminating witnesses, exact-set completeness, no false-green)** — PASS: FR-006 mandates exact-set (not subset), the census is built from an *independent* source (raw XML), and the determinism golden guards non-deterministic emit.
- **Article XVIII §5 (no early-ship of deferred protocols)** — PASS: no protocol shipped; see §XVIII.5 disposition above.

**Gate result**: PASS with one required amendment spanning **two constitution loci in a single v0.7 → v0.8 MINOR bump** — Article I §1 (lines 63/65, narrow the FIX Latest typed-codegen carve-out) **and** Article XVIII §2 (line 339, reconcile the locked v1.2 roadmap line for A-035..A-065); Sync Impact Report lists both. Folded into Gate A per precedent. No unjustified complexity — see Complexity Tracking (empty).

## Project Structure

### Documentation (this feature)

```text
specs/076-fix-latest-typed-codegen/
├── plan.md              # This file
├── spec.md              # Feature spec (+ Clarifications 2026-07-15)
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output (generated-API + build-option contracts)
├── checklists/
│   └── requirements.md  # Spec quality checklist (16/16)
└── tasks.md             # /speckit-tasks output (NOT created here)
```

### Source Code (repository root = library submodule)

```text
tools/codegen/fixpp-codegen/
├── ir.cpp               # build_ir: OrchestraLoader load branch + kCodegenVersions vlatest row + DOMINANT: Orchestra-native IR projection (research R2b) — fixr:-schema sibling to populate_group_order (which is <fix>-only, :158/:186) producing group_order + header_trailer_tags + category→is_application + lossless occurrence list
├── gen_util.hpp         # app_version_enum: "vlatest" special-case (mirror vt11→Unknown)
├── emit_builders.cpp    # REQUIRED: widen the ir.ns=="v44" early-return gate (:646) to emit build_<Msg>+validate_<Msg> for the vlatest APP-SUBSET via a vlatest coverage predicate (NOT reusing FIX44 kOfficial33/kN002N003Excluded, :58-76); read/reify/args stay all-181 (does NOT generate for free — see research.md R-summary row 2b/2c)
├── emit_messages.cpp    # unchanged (keys on ir.ns → fixpp::vlatest for free)
├── emit_validator.cpp   # unchanged (keys on ir.ns → constexpr rule tables for free)
├── emit_dispatch.cpp    # unchanged — vlatest deliberately excluded from dispatch_application (FR-009)
├── emit_manifest.*      # NEW: emits the per-message census manifest (occurrence-path field list from the IR) the census reads (research.md R5/row 2c)
└── main.cpp             # driver: accepts the fifth --xml job + one write_file line for the census manifest (mirrors the existing NormativeReferences.md emit, main.cpp:91)

cmake/
└── Codegen.cmake        # add build option (default ON) + fifth --xml OrchestraFIXLatest.xml invocation

dictionaries/orchestra/
└── OrchestraFIXLatest.xml   # existing pinned input (074) — unchanged

<build tree>/_codegen/include/fixpp/vlatest/   # generated: Fields/Messages/Validator/Reify/Builders.hpp + census Manifest
specs/076-fix-latest-typed-codegen/contracts/golden/   # extend the checked-in codegen golden with vlatest + census manifest AND the legacy tiers + _dispatch/ inventory V-7 diffs against (goldens live under specs/<id>/contracts/golden/, e.g. specs/003-.../, specs/069-...; NOT tools/codegen/golden/, which does not exist)

tests/
├── codegen/             # completeness census (raw-XML exact-set) + determinism assertion
└── wire/  (or tests/codegen/)  # 181-message typed round-trip + build-option ON/OFF behavior witness
```

**Structure Decision**: Single project. The change is confined to the offline codegen tool (`tools/codegen/fixpp-codegen/`), its CMake wiring (`cmake/Codegen.cmake`), and new tests. No `src/` runtime code, no `capi/`, no `bindings/python/` changes. Generated output lands in the existing per-version build-tree layout, adding a `vlatest/` sibling to `v42/v44/v50sp2/vt11/`.

## Complexity Tracking

> No Constitution Check violations requiring justification. The Article I §1 + Article XVIII §2 amendment is a governance reclassification handled through the Article XX process (folded into Gate A), not an architectural complexity deviation. Table intentionally empty.

## Gate A

- Round 1 applied 2026-07-15: Codex P1=3 P2=3 P3=0; Opus post-judging P1=4 P2=4 P3=0; rewrite addresses root causes RC#1 (generated-API surface accuracy), RC#2 (amendment payload — Article I §1 + XVIII §2 + Normative References), RC#3 (census occurrence-path key + per-message source), plus P2-5 (V-7 OFF-path gate) and P2-6 (ctest -L). Reviews: research/reviews/codex_076-fix-latest-typed-codegen_gate_a_review.md, research/reviews/opus_076-fix-latest-typed-codegen_gate_a_adversarial_review.md. Rewrite also surfaced a NEW finding beyond both reviews: `emit_builders.cpp:646` hard-gates `if (ir.ns != "v44") return {};`, so the vlatest `build_<Msg>`/`validate_<Msg>` surface does NOT generate "for free" — the gate MUST be widened (recorded in research.md R-summary row 2b + Project Structure).
- Round 2 applied 2026-07-15: Codex P1=3 P2=2 P3=0; Opus post-judging P1=4 P2=2 P3=0; rewrite addresses RC-A (Orchestra-native IR projection — group_order/header_trailer/category/occurrence, the dominant change), RC-B (Option A: read/reify/args all 181, build_/validate_ app-subset via a vlatest coverage predicate — flagged for user sign-off confirmation), RC-C (golden path/inventory correction specs/<id>/contracts/golden + CMake OFF-path cleanup), and N-1 (FR-006 non-circularity re-argued as two independent declaration-order walkers). Reviews: research/reviews/codex_076-fix-latest-typed-codegen_gate_a_2_review.md, research/reviews/opus_076-fix-latest-typed-codegen_gate_a_2_adversarial_review.md.
- Round 3 (verify) 2026-07-15: Codex P1=0 P2=0 P3=1; Opus post-judging P1=0 P2=1 P3=1 → loop exhausted; user chose a targeted rewrite. Targeted fix closes the P2 (census asserted on the projection-manifest, not the shipped universal read/reify/args classes) via closure option (b): added a manifest↔emitted-class consistency gate (V-1b) so census∘consistency non-circularly pins class≡raw-XML at class-reachable-field granularity for the read surface, while V-1 pins the projection/builder surface at occurrence-path granularity; + P3 cleanup of the stale Dictionary/loader provenance sentences (research.md:84/88). Reviews: research/reviews/{codex,opus}_076-fix-latest-typed-codegen_gate_a_3_*review.md.
