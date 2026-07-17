# Implementation Plan: Precompiled per-version builder/validator libraries

**Branch**: `078-precompiled-builder-libs` | **Date**: 2026-07-17 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/078-precompiled-builder-libs/spec.md`

## Summary

Restructure the client-facing typed builder/validator codegen tier (delivered by
077) from one monolithic `inline`-everything `fixpp/<ns>/Builders.hpp` per version
into **precompiled per-version libraries + a slim declaration surface + a
per-message header-only inline mode**, so a consumer pays compile time and binary
size only for what it links. Organizing principle (user-confirmed): *everything is
always emitted and always compiled once, per version; all opt-in is purely
link-time.*

Per builder-bearing version `<ns>` ∈ {v44, v50sp2, vlatest} the emitter emits a
file set — **data-only** `groups.hpp` (shared `G_<no_tag>Args`), validator-scoped
`validators/traits.hpp` (shared group-plan traits), slim `messages/<Msg>.hpp`
(Args + `extern` decls), per-side inline bodies `messages/<Msg>.builder.inl` /
`<Msg>.validator.inl`, per-side external-linkage TUs `messages/<Msg>.builder.cpp` /
`<Msg>.validator.cpp`, and `all.hpp` (aggregator + `builder_registry`, replaces
`Builders.hpp`) — with the **disjoint** `.builder.cpp` / `.validator.cpp` sets
compiled into two always-built STATIC libraries `fixpp_builders_<ver>` and a
**separate** `fixpp_validators_<ver>` (no `.cpp` shared → builder-only link carries
zero validator code and both-libs link has no duplicate symbol).
Consumers `#include` the slim header and link whichever libs they want; a macro
(`FIXPP_BUILDERS_HEADER_ONLY`, per-message override) flips chosen messages to
zero-overhead inline. The library's own heavy builder tests link the prebuilt
libs (the giant compile happens once, at the lib target), which **retires the
#197 CI heavy-test stopgap** (config-gating + Ninja job pool). Approach and
seams are established in [research.md](./research.md) (R1–R10).

**Zero core change** (verified): the tier is 100% client-facing — no `src/`,
`capi/`, `bindings/`, or `python` edits; C-ABI frozen 1.5.0; mangled C++ surface,
so no abidiff/nm gate impact.

## Technical Context

**Language/Version**: C++23 (Article II §1).

**Primary Dependencies**: the in-tree codegen host tool `fixpp-codegen`
(`tools/codegen/fixpp-codegen/`, links `fixpp::dictionary` + `pugixml`); CMake ≥3.28
+ Ninja; Conan. Codegen runs at **configure time** via `execute_process`
(`cmake/Codegen.cmake:442-472`). GoogleTest for tests. No new external deps.

**Storage**: N/A (source-generation feature).

**Testing**: GoogleTest (round-trip, completeness census, mixing/inline, `nm`
symbol witnesses); `cmake -P` determinism + git-cleanliness gates; a compile-bench
decision record for compile-RSS/wall SCs (003/T046 convention).

**Target Platform**: Linux/Clang (primary), Linux/GCC (sanity), Windows/MSVC
(tier2). All three must build the new lib targets.

**Project Type**: C++23 library — codegen-tool + build-system + tests change only.

**Performance Goals**: **build-time / binary-size**, not runtime. Targets are the
measured deltas vs the 077 monolith baseline (SC-001 one-message-TU compile RSS
≪ ~3.6 GiB; SC-002/SC-003 link-only / zero-validator binary; SC-006 heavy test
TUs under the 16 GB runner limit). No library **runtime** hot-path change → no
Google Benchmark / Article VIII surface (see Constitution Check).

**Constraints**: byte-identical typed output vs 077 for every message, both link
and inline mode (SC-004); deterministic multi-file regeneration + regenerated
golden set (FR-010); ODR-safe builder/validator/inline mixing (FR-007, gated by
the R2a probe); `all.hpp` stays slim by default (R5); #197 removal CI-gated (R8).

**Scale/Scope**: 3 versions; 83+156+173 = 412 messages × {slim `.hpp`,
`.builder.inl`, `.validator.inl`, `.builder.cpp`, `.validator.cpp`} + per-version
`groups.hpp` (data-only) + `validators/traits.hpp` + `all.hpp`; 6 new STATIC lib
targets (3×{builders,validators}); ~4,500 vlatest validator specializations.

## Constitution Check

*GATE: re-checked after Phase 1 design — PASS with a Gate-A-folded annotation. No unjustified violations.*

| Gate (Article) | Status | Notes |
|---|---|---|
| **I §1 — FIX-Latest builder-tier description** | **AMEND (Gate A)** | I §1 (`constitution.md:88`) states 077 delivers "576 plans / ~78 MB **single-TU header**". 078 makes "single-TU header" **false going forward** (precompiled libs + slim headers). Per Article XX §1 a document↔spec conflict is resolved by **amending the article**, and the 074–077 codegen re-narrations landed as **versioned clause edits** (v0.6→v0.9), mirrored by XVIII §2's inline "(Annotation, 076 v0.8 / 077 v0.9: …)" form (`constitution.md:364`). **Disposition (Gate A round 1):** minimal inline clause edit to `constitution.md:88` (append "(Annotation, 078 …: layout restructured to precompiled per-version libs + slim headers)") + constitution **version bump v0.9→v0.10**, folded at merge per precedent — **not** edited now (do not touch constitution.md at Gate A; record the AMEND disposition + locus + bump here). |
| **XVIII §7 — per-version builder tier** | **AMEND (Gate A)** | §7 (`constitution.md:375`) states the v44/v50sp2/vlatest builder tier "flow through feature 077's **single** … emitter". 078 restructures the packaging (per-message libs + slim headers) so "single … emitter"/single-header is stale going forward; no message-set change (v42 still deferred to #196; vt11 none). **Disposition (Gate A round 1):** minimal inline clause edit to `constitution.md:375` (append "(Annotation, 078 …: packaged as precompiled per-version builder/validator libs)") folded into the same v0.9→v0.10 bump at merge — recorded here, not edited now. |
| Appendix A — **Codegen layout** trigger | **Satisfied** | Codegen-layout change ⇒ all four mandatory controls: `/clarify` ✓ (5 Q, done), `/analyze` (pipeline step 6, before `/implement`), Codex **Gate A** (after this `/plan`), user `/plan` sign-off. |
| X — ABI Policy | **PASS** | Mangled C++ surface only; zero `c_api.h` change, C-ABI frozen 1.5.0; new libs absent from `fixpp_capi_symbols.txt`, untouched by `abi-golden.yml`. The C++ `Args`-ABI boundary is sound because fixpp is built-from-source in the client toolchain (A3); cross-toolchain = existing C-ABI runtime builder (out of scope). |
| VI — 100% FIX / no silent omissions | **PASS+** | The completeness census is preserved (address-of-everything now proves **link**, with compile-proof at the lib target — R7); determinism now also asserts stable file **name-set + count** (R6), *strengthening* the no-silent-omission guarantee across the file split. **Normative References:** `spec.md` now carries a `## Normative References` section (per Article VI §5, which requires the section in every `/specify` artifact unconditionally). Its content is "None — pure layout restructure of 077's already-delivered tier; no new FIX coverage / no new `OFFICIAL` catalogue row; governing prior artifact = feature 077 + issue #198." (Codex-4: the section's presence is mandatory even when the content is "None" — a prior feature's omission does not amend the article, Article XX.) |
| VII — Testing (TDD, grouping) | **PASS** | Red-green per FR; new `nm` witnesses (SC-002/003), mixing/inline test (FR-007), regenerated golden set. Heavy TUs relink the libs; grouping/label rules (VII §8) preserved. |
| VIII — Perf budgets | **PASS (N/A)** | Article VIII is **runtime-only** (Google Benchmark, ±5% regression, hot-path allocator, latency) and the runtime hot path is untouched → trivially PASS. The build-time/compile-RSS deltas (SC-001/006) are **not** an Article VIII surface; they go to the **003 compile-bench / decision-record convention** (R9), the same mechanism 077 used (`.specify/decisions/003-...-verify.md:101`, T046). Article VIII has no compile-time ceiling. |
| IX — Coverage / sanitizers / static analysis | **PASS** | New emitter/host-tool code carries unit coverage; generated headers + new lib targets build under the existing sanitizer tiers. **Coverage scope note:** the generated per-message `.cpp` compile into the libs but live in `${CMAKE_BINARY_DIR}/_codegen/` (build tree), **outside** Article IX's touched-module scope (`include/fixpp/<mod>*`+`src/<mod>*`) — like the existing generated headers — so deep-group `build_`/`validate_` bodies do **not** fall under the 95/85 touched-module gate (verify at `/speckit-verify`). No `src/`/`include/` change expected (like 077). |
| XV — Banned patterns | **PASS** | No hot-path alloc, no runtime-only validation (§6: typed constexpr metadata retained), runtime dictionary path still exists (§13). No new banned pattern. |
| XVI — Spec Kit workflow | **PASS** | Pipeline order honored — Gate A after `/plan`, before `/tasks`; `/clarify` done; `/analyze` next. |
| III — Build toolchain | **PASS** | CMake≥3.28+Ninja+Conan; `tools/` stays build-only (§5) — the codegen tool is not a user-link-time dependency; the new libs are ordinary compiled artifacts consumers link, not tooling. |

**Gate A round 1 disposition:** I §1 (`constitution.md:88`) and XVIII §7
(`constitution.md:375`) are **AMEND**, not annotate — the stale "single-TU header"
/ "single … emitter" text is load-bearing factual text made false by 078, and the
074–077 precedent is a **versioned clause edit** (v0.6→v0.9), so a minimal inline
clause edit at each locus + a constitution **version bump v0.9→v0.10** is folded at
merge (per precedent; not edited at Gate A). This is the expected, precedented
codegen re-narration — no separate amendment vote beyond the Gate-A-folded edit.

## Project Structure

### Documentation (this feature)

```text
specs/078-precompiled-builder-libs/
├── plan.md              # this file
├── research.md          # Phase 0 — R1–R10 (layout, ODR/SC-003 probe, granularity, golden set, #197)
├── data-model.md        # Phase 1 — emitted-artifact + target + golden-set entities
├── quickstart.md        # Phase 1 — validation scenarios (link, send-only, inline-mix, aggregator)
├── contracts/
│   ├── include-layout.md         # generated file-set + include/macro contract
│   ├── cmake-targets.md          # lib target names, always-built, link-time opt-in
│   ├── completeness-and-golden.md# completeness (link-proof) + golden-set/determinism contract
│   └── golden/                   # regenerated golden SET (created at /implement)
├── checklists/requirements.md    # spec quality checklist (done)
└── tasks.md             # /speckit-tasks output (NOT this command)
```

### Source Code (repository root = the library submodule)

```text
tools/codegen/fixpp-codegen/
├── emit_builders.cpp   # PRIMARY CHANGE — split the 4-pass string into a file set:
│                       #   data-only groups.hpp, validators/traits.hpp (shared group-plan
│                       #   traits :961, R2), messages/<Msg>.{hpp slim decl, builder.inl,
│                       #   validator.inl (+ per-msg top-level traits :963-966), builder.cpp,
│                       #   validator.cpp}, all.hpp (+ builder_registry :929); keep the
│                       #   interned-plan/message ordering for determinism (:824-912)
├── main.cpp            # driver — replace per-version write_file("Builders.hpp") (:118) with
│                       #   multi-file write_file over the emitted set; remove monolith path
├── emit.hpp / ir.*     # emitter surface / IR — extend emit signatures; IR unchanged (order intact)
└── (emit_messages.cpp) # read tier — REFERENCE ONLY (unchanged; read/reify split out of scope, A5)

cmake/Codegen.cmake     # emit the file set into _codegen/include; add STATIC targets
                        #   fixpp_builders_<ver> (disjoint <Msg>.builder.cpp set) /
                        #   fixpp_validators_<ver> (disjoint <Msg>.validator.cpp set); wire slim
                        #   headers into determinism/git-clean gates (build-tree only — no
                        #   external install/export, R3); keep determinism golden dir defs
                        #   updated to the golden SET
CMakeLists.txt          # DELETE FIXPP_BUILD_HEAVY_BUILDER_TESTS option (:208-210) + the
                        #   heavy_builder_compile job pool (:347-380) AFTER CI proves the
                        #   SANITIZER legs fit — incl. python-bindings asan/ubsan/tsan (reused
                        #   linux-clang-debug preset) + gcc-release + MSVC (R8); do NOT install
                        #   the typed slim layout for external linking while targets unexported
                        #   (:338-345, R3)
CMakePresets.json       # remove the 3 FIXPP_BUILD_HEAVY_BUILDER_TESTS=ON overrides (:26,53,198)

tests/codegen/          # rewrite determinism_test.cpp → multi-file set (name-set + count + content);
                        #   completeness TUs (test_077_<ver>_builder_completeness.cpp) relink the libs
                        #   + add_dependencies on the lib targets; regenerate the *_entries.def census;
                        #   NEW nm symbol-witness test (SC-002 link-only / SC-003 zero-validator);
                        #   NEW ODR/mixing probe→test (R2a / FR-007); keep /bigobj (cheap insurance)
tests/session/          # un-gate + relink the heavy roundtrip TUs (test_077_*_builder_roundtrip.cpp,
                        #   :2609-2700); ALSO relink the 067 v44 tier (test_067_*.cpp ×5) AND the 069
                        #   v44 tier (test_069_{all_families_roundtrip,family_exemplar_golden,mode_count,
                        #   us1_smoke}) — all #include <fixpp/v44/Builders.hpp> today. FULL migration
                        #   surface = every TU that #includes <fixpp/<ns>/Builders.hpp>: 067×5, 069×4,
                        #   077×6 — exact set + count RE-MEASURED at /tasks (census this session: ZERO
                        #   in examples/, so no CI-exercised example break). ALSO a FAMILY of
                        #   NON-#include monolith-name gates (build-graph markers, existence
                        #   asserts, text-parse/size gates, determinism OFF-path/golden-diff, -D
                        #   path defs) — enumerated COMPLETELY with per-file dispositions in the
                        #   "Round 2 — applied 2026-07-17" migration census below (New-C; the
                        #   round-1 #include-only framing under-counted them)
bench/codegen/vlatest_builders_compile_bench/  # compile_bench.sh #includes fixpp/vlatest/Builders.hpp
                        #   (:66) — repoint to the slim header; this harness BECOMES the SC-001
                        #   slim-vs-monolith compile-RSS witness (R9 compile-bench record)

specs/078-.../contracts/golden/   # regenerated golden SET per version (created at /implement)
docs/src/dictionary/codegen.md    # document the split layout + link-time opt-in + inline mode
spec/behaviors-and-limitations.md # record the include-path break + the #197-stopgap retirement
```

**Structure Decision**: Single-project layout. The change is concentrated in the
codegen host tool (`emit_builders.cpp` + `main.cpp`), CMake wiring (new lib
targets + stopgap removal), the golden set, and tests — the same surface 077
touched, plus the new library targets. No `src/`/`include/`/`capi/`/`python`
change.

## Complexity Tracking

| Item | Why | Simpler alternative rejected because |
|---|---|---|
| Per-message, **per-side** `.cpp` (`<Msg>.builder.cpp` / `<Msg>.validator.cpp`) instead of one `Builders.cpp` | SC-002 needs static-archive **object granularity** so the linker pulls only used messages; **disjoint** builder/validator objects are what keep a builder-only link validator-free (SC-003) and let both libs link with no duplicate symbol (R1); also bounds per-TU compile RSS (SC-006) | one `Builders.o` pulls the whole ~18–20 MiB `.text` → SC-002 fails; a single `<Msg>.cpp` defining both `build_`+`validate_` → validator code leaks into a builder-only link AND both-libs link hard-fails on duplicate symbol |
| Five-file per message (`.hpp` decl / `.builder.inl` / `.validator.inl` / `.builder.cpp` / `.validator.cpp`) | FR-004+FR-006 require **both** link mode (default) and header-only inline **from one generation**, and the builder/validator surfaces must be **physically separate on the inline path too** (force-inlining `build_` must not pull validator traits) | header-only-only (issue proposal 2) can't remove the test compile cost (SC-006); lib-only drops FR-006; a unified `.inl` re-creates root-cause-#1 on the inline path |
| Two-tier validator traits — shared **group-plan** traits in `validators/traits.hpp` (inline) + per-message **top-level** traits in the per-message validator surface (R2) | group-plan traits are shared and must be one ODR-safe definition across linked-lib + force-inlined validators (FR-007); per-message top-level traits are **not** shared (one set per message); both must be **invisible to the builder graph** (SC-003/SC-001) | traits in the builder-included `groups.hpp` → parses into every builder TU (SC-001) + tri-doc contradiction; a whole-hog move of *all* traits into one shared header re-creates the parse-cost leak for the per-message half |
| Golden as a file **set** + name/count determinism (R6) | the split multiplies files → a dropped/renamed message is a new failure mode a content-only diff misses | content-only diff on a single golden → silent omission across the split ([[feedback_codegen_golden_exists_narrow_verify_misses_it]]) |
| #197 removal is **CI-gated**, not a blind delete (R8) | Article XVII §8 resource gate — "every leg fits without the pool" is CI-verified, not locally provable; gcc ~2× RSS + MSVC | delete-then-hope re-introduces the exit-143 OOM that motivated #197 |

**Compile-time overage note (not an Article VIII item).** Any residual
compile-RSS/wall figures (e.g. the per-message lib TUs, `all.hpp` header-only
mode) are recorded via the **003 compile-bench / decision-record convention**
(`.specify/decisions/003-dictionary-codegen-verify.md` pattern, 077 T046), the
same non-Article-VIII mechanism 077 used — captured in this feature's
`/speckit-verify` record, not gated by a perf budget.

## Gate A

Codegen-layout change ⇒ Gate A **mandatory** (Appendix A). Run `/gate-a
078-precompiled-builder-libs` after this `/plan`, before `/tasks`. Gate A must
confirm: (1) the I §1 / XVIII §7 annotations vs. an edited clause; (2) the R2
validator-traits placement + the R2a ODR / builder⟂validator (SC-003) probe as a blocking prerequisite;
(3) the install-scope call (build-tree targets vs. `install(TARGETS)`/export, R3);
(4) the #197-removal sequencing (R8). User `/plan` sign-off is one of the four
mandatory controls.

**User `/plan` sign-off: GRANTED 2026-07-17.** Proceeding to Gate A.

### Round 1 — applied 2026-07-17

- Round 1 applied 2026-07-17: Codex P1=2 P2=2 P3=1; Opus post-judging P1=1 P2=4 P3=3; rewrite addresses root cause #1 (research↔data-model split-object + data-only groups.hpp + two-tier traits), install/export scope decision, Normative References, constitution AMEND (I §1/XVIII §7 clause edit + v0.9→v0.10), builder_registry+staleness-gate migration, plus main-CI OOM census gap (python-bindings sanitizer legs). Reviews: research/reviews/codex_078-precompiled-builder-libs_gate_a_review.md, research/reviews/opus_078-precompiled-builder-libs_gate_a_adversarial_review.md.

**Confirm-item resolutions (the four Gate-A calls):**
1. **I §1 / XVIII §7 → AMEND** (not annotate): minimal inline clause edits at `constitution.md:88` and `:375` + version bump **v0.9→v0.10**, folded at merge (Constitution Check table; New-3).
2. **R2 validator-traits placement → data-only `groups.hpp` + two-tier traits** (shared group-plan → `validators/traits.hpp`; per-message top-level → per-message validator surface); **R2a probe** widened to 3 legs (builder-only zero-validator `nm`, both-libs no-dup, force-inline+linked shared-plan single-def) and stands as the Phase-0 blocking prerequisite (root cause #1).
3. **Install scope → build-tree + in-tree consumers only** (installed-external export deferred; slim headers not installed for external linking while targets unexported); US1/FR-002/FR-004 narrowed; spec Clarifications (Gate A round 1) (Codex-3, R3).
4. **#197-removal sequencing → CI-gated on the SANITIZER legs** including the python-bindings `asan`/`ubsan`/`tsan` matrix (reused `linux-clang-debug` preset), gcc-release, MSVC — not on clang-only local `/speckit-verify` (A4, R8; main-CI OOM findings).

**Also folded (P3 + New):** aggregator "backwards-compatible" → "replacement whole-version surface" (Codex-5); builder-side mixing witness added (New-4); `--gc-sections` fallback flagged as CI-RSS-measure-before-use, not equivalent (New-5); `builder_registry` home = `all.hpp` + `codegen_source_staleness_test.cmake` added to the migration surface (New-1).

### Round 2 — applied 2026-07-17

- Round 2 applied 2026-07-17: Codex P1=0 P2=1 P3=1; Opus post-judging P1=0 P2=2 P3=3; rewrite completes the non-#include Builders.hpp migration census (re-point markers/assertions → all.hpp), fixes the #197-removal CI-evidence path (workflow_dispatch/split-PR gate on the python-bindings sanitizer legs), corrects the SC-004 validator byte-identical→result-identical category error, and adds the validators/traits.hpp groups.hpp include edge. Reviews: research/reviews/codex_078-precompiled-builder-libs_gate_a_2_review.md, research/reviews/opus_078-precompiled-builder-libs_gate_a_2_adversarial_review.md.

**Confirm-item resolutions (round 2):**
1. **#197-removal CI evidence path (Codex P2 / Opus CONFIRM) → RESOLVED.** The python-bindings sanitizer legs are path-gated (`python_touched`, `tier1.yml:131,628-631`) and **skip on the 078 PR** (078 touches none of `PY_RE`), so a PR gate on them passes vacuously and `push:main` (`python_touched=true` unconditionally, `:141`) would OOM `main` post-merge. **Decision:** gate the deletion on an explicit **`workflow_dispatch` (or feature-branch `push`) evidence run** on the post-split tree (primary) **or split the deletion into a follow-up PR** after a `push:main`/dispatch run proved the legs (fallback). Do **not** broaden `PY_RE`. Recorded in spec A4 + Clarifications (Gate A round 2), research.md R8 step 2, cmake-targets.md "#197 removal", quickstart.md Scenario 7.
2. **Complete non-#include Builders.hpp migration census (Opus New-C) → RESOLVED.** See the census table below — the round-1 `#include`-only framing under-counted the class ([[feedback_reachability_built_table_misses_bypassing_surface]]).
3. **SC-004/FR-009 validator "byte-identical" category error (Codex P3, locus-escalated) → RESOLVED.** Builder = byte-identical wire bytes; validator = result-identical (same success/error + offending tag for the same `Args`). Swept across spec FR-009/SC-004, data-model.md Entity 3, completeness-and-golden.md, quickstart.md Scenario 4a.
4. **`validators/traits.hpp` → `groups.hpp` include edge (Opus New-A) → RESOLVED.** Traits specialize over the group `Args` structs (`emit_builders.cpp:959-960`; type must be complete, `:954-955`), so `validators/traits.hpp` MUST `#include "../groups.hpp"`. Stated in data-model.md Entity 1b + include-layout.md.
5. **Single-body-source variant vs golden NAME/COUNT (Opus New-B, drift-watch) → NOTED.** If the deferred single-body variant is chosen at `/implement`, the file name-set changes → Entity 7 golden enumeration + the R6 name-set/count assertion regenerate together; R6 stays authoritative. Noted in research.md R1 + completeness-and-golden.md R6.

**Complete `Builders.hpp` migration census (non-`#include` gates — New-C).** This is the **COMPLETE** set of `#include`-independent artifacts that hardcode the monolith path/marker or text-parse a whole-version aggregate symbol; each false-reds or inverts once FR-008 deletes `Builders.hpp`. Built from a tree-wide `grep -rn 'Builders\.hpp' tests/ cmake/ bench/` + a `grep -rn 'builder_registry'` symbol-leg sweep (which confirmed every `builder_registry` symbol consumer also `#include`s its defining header — those are the separately-enumerated `#include`-ers: 067×5, 069×4, 077×6, relinked to `all.hpp`). The round-1 `#include`-only framing under-counted this class ([[feedback_reachability_built_table_misses_bypassing_surface]]). **`/tasks` MUST carry a disposition task per row.** Two re-point targets: `builder_registry` → `all.hpp` (Entity 5); the `G_<no_tag>Args` group structs → `groups.hpp` (Entity 1, data-only) — the group-struct text-parse gates re-point to `groups.hpp`, **not** `all.hpp`.

| File:line | What it does (verified at source) | Disposition |
|---|---|---|
| `cmake/Codegen.cmake:267,273,285` | `_v{44,50sp2,vlatest}_builders_marker = .../<ver>/Builders.hpp` — build-graph existence/dependency sentinels (consumed at `:362` clean-loop, `:380,411,495-514` staleness/existence checks) | re-point each marker to `.../<ver>/all.hpp` (the new per-version emission sentinel) |
| `cmake/Codegen.cmake:623` | `FIXPP_CODEGEN_BUILDERS_MARKER_v44 = .../v44/Builders.hpp` written into `graph_props.cmake`, feeds `codegen_build_graph_test.cmake` | re-point to `.../v44/all.hpp` |
| `tests/codegen/codegen_build_graph_test.cmake:119-125` | `if(EXISTS marker)` → `_assert(TRUE "v44/Builders.hpp exists")` else `_assert(FALSE …must exist)` — **hard-RED** post-split | assert `v44/all.hpp` exists (via the re-pointed marker) |
| `tests/codegen/codegen_source_staleness_test.cmake:96-108` | greps `FIXPP_BUILDERS_HEADER` (`= v44/Builders.hpp`) for `inline constexpr ::std::array<builder_registry_entry, 83> builder_registry` | re-point `FIXPP_BUILDERS_HEADER` (`CMakeLists.txt:437`) to `v44/all.hpp` (`builder_registry`'s new home) — round-1 New-1 |
| `tests/codegen/builder_completeness_common.hpp:168-177` | shared `parse_builder_registry` helper — finds `builder_registry = {{` in the `-D`-fed builders path (used by all completeness + mutation TUs) | re-point the fed path → `all.hpp` for the `builder_registry` array text; `parse_build_fn_identifiers` → the per-message `.builder.{inl,cpp}` set (`all.hpp` holds only `#include`s + the registry array, no literal `build_<Msg>(` bodies) |
| `tests/codegen/builder_completeness_mutation_witness_test.cpp:146-147` (+ text-parse) | `ASSERT_TRUE(fs::exists(out/v44/Builders.hpp))` then registry/build-fn text-parse of the dropped file | existence → `v44/all.hpp`; registry text-parse → `all.hpp`; build-fn identifiers → per-message set |
| `tests/codegen/determinism_test.cpp:363-368,559-568` (existence + byte-identity), `763-774,784` (FR-012 OFF-path presence/absence; `:784` = `MainTreeVlatestBuildersUnaffectedByOffPathWitness` monolith-existence assertion), `807-879` (FR-013 per-version golden-diff) | asserts `<ver>/Builders.hpp` exists + byte-identical run-to-run; OFF-path vlatest-absent / v44+v50sp2-present / v42-absent; per-version golden-diff of the monolith | rewrite to the file-**SET** (name-set + count + content) per R6: existence → the `all.hpp`/messages set; OFF-path → `<ver>/all.hpp` (or `<ver>/` dir) presence/absence; golden-diff → the golden **SET** |
| `tests/codegen/test_077_builder_no_emit.cpp:51-72` | `EXPECT_FALSE(exists(vt11/v42 Builders.hpp))` + `EXPECT_TRUE(exists(v44/v50sp2/vlatest Builders.hpp))` via `-D..._BUILDERS_HPP` | present versions → `<ver>/all.hpp` exists; absent versions (vt11/v42) → assert no `<ver>/all.hpp`/messages dir emitted (no-emit semantics preserved) |
| `tests/codegen/vlatest_compile_smoke_test.cpp:82-91` | `ASSERT_TRUE(exists(vlatest/Builders.hpp))` + `EXPECT_LT(size, 100MB)` monolith size gate | existence → `vlatest/all.hpp` + the per-message set; the ~monolith size band is **obsolete** (no monolith to bound) — retire or retarget to the split artifact set |
| `tests/codegen/test_077_builder_dedup_count.cpp:61,89-110` | text-parse of `vlatest/Builders.hpp`: counts `struct G_…Args` (==576) + `namespace fixpp::vlatest::groups` + size band | re-point to **`vlatest/groups.hpp`** (the `G_…Args` structs' new data-only home — **NOT** `all.hpp`); size band retargets to `groups.hpp` |
| `tests/codegen/test_077_v42_vt11_completeness_and_c4.cpp:72,94,102` | `EXPECT_FALSE(exists(v42/vt11 Builders.hpp))` (still holds) + C4 reads `v50sp2/Builders.hpp` text for `struct G_…Args` bodies | v42/vt11 existence → `<ver>/all.hpp` absence; C4 structural-key text → **`v50sp2/groups.hpp`** (group structs' new home) |
| `tests/codegen/CMakeLists.txt:158,161-164,234,269,297,324-326,400,437` | `-DFIXPP_CODEGEN_*_BUILDERS_HPP="…/Builders.hpp"` + `FIXPP_BUILDERS_HEADER` (`:437`) — feed the monolith path into the heavy/text-parse/golden TUs | change each `-D` header-path def to the new header (`all.hpp` for registry/build-fn/existence; `groups.hpp` for `G_`-struct text-parse) or drop it as the TU relinks the libs |

*Separate (already-enumerated) `#include`-er leg:* the completeness TUs `test_077_{v44,v50sp2,vlatest}_builder_completeness.cpp` are in **both** categories — `#include <fixpp/<ns>/Builders.hpp>` (relink → `all.hpp`) **and** `-D`-fed registry/build-fn text-parse (re-point per the table). Disposition both legs at `/tasks`.

- Round 3 (2026-07-17): CONVERGED — Codex P1=0 P2=0 P3=1; Opus post-judging P1=0 P2=0 P3=1. Residual P3 validator "byte-identical"→"result-identical" wording swept (data-model.md, completeness-and-golden.md, include-layout.md) + determinism_test.cpp:784 census line-cite added. Reviews: research/reviews/codex_078-precompiled-builder-libs_gate_a_3_review.md, research/reviews/opus_078-precompiled-builder-libs_gate_a_3_adversarial_review.md.
