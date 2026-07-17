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
file set — `groups.hpp` (shared `G_<no_tag>Args` + validator traits), slim
`messages/<Msg>.hpp` (Args + `extern` decls), `messages/<Msg>.inl` (inline
bodies), per-message `messages/<Msg>.cpp` (external-linkage defs), and `all.hpp`
(aggregator, replaces `Builders.hpp`) — compiled into two always-built STATIC
libraries `fixpp_builders_<ver>` and a **separate** `fixpp_validators_<ver>`.
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

**Scale/Scope**: 3 versions; 83+156+173 = 412 messages × {slim `.hpp`, `.inl`,
`.cpp`} + per-version `groups.hpp` + `all.hpp` + validator surface; 6 new STATIC
lib targets (3×{builders,validators}); ~4,500 vlatest validator specializations.

## Constitution Check

*GATE: re-checked after Phase 1 design — PASS with a Gate-A-folded annotation. No unjustified violations.*

| Gate (Article) | Status | Notes |
|---|---|---|
| **I §1 — FIX-Latest builder-tier description** | **ANNOTATE (Gate A)** | I §1 describes 077's output as "576 plans / ~78 MB **single-TU header**". 078 supersedes the *layout* going forward (precompiled libs + slim headers); the annotation records the layout change. Descriptive/historical text about 077's delivery, not a scope change — mirrors the 074/075/076/077 codegen re-narrations folded into Gate A. |
| **XVIII §7 — per-version builder tier** | **ANNOTATE (Gate A)** | §7 records the v44/v50sp2/vlatest builder tier "flow through 077's single … emitter". 078 restructures the *packaging* (libs, not a header); no message-set change (v42 still deferred to #196; vt11 none). Annotation only. |
| Appendix A — **Codegen layout** trigger | **Satisfied** | Codegen-layout change ⇒ all four mandatory controls: `/clarify` ✓ (5 Q, done), `/analyze` (pipeline step 6, before `/implement`), Codex **Gate A** (after this `/plan`), user `/plan` sign-off. |
| X — ABI Policy | **PASS** | Mangled C++ surface only; zero `c_api.h` change, C-ABI frozen 1.5.0; new libs absent from `fixpp_capi_symbols.txt`, untouched by `abi-golden.yml`. The C++ `Args`-ABI boundary is sound because fixpp is built-from-source in the client toolchain (A3); cross-toolchain = existing C-ABI runtime builder (out of scope). |
| VI — 100% FIX / no silent omissions | **PASS+** | The completeness census is preserved (address-of-everything now proves **link**, with compile-proof at the lib target — R7); determinism now also asserts stable file **name-set + count** (R6), *strengthening* the no-silent-omission guarantee across the file split. **Normative References:** none — this is a pure layout restructure of 077's already-delivered tier with **no new FIX coverage** (adds no `OFFICIAL` catalogue row); the spec omits a Normative-References section, matching 077's spec (also a codegen restructure, likewise omitted). `/analyze` confirms. |
| VII — Testing (TDD, grouping) | **PASS** | Red-green per FR; new `nm` witnesses (SC-002/003), mixing/inline test (FR-007), regenerated golden set. Heavy TUs relink the libs; grouping/label rules (VII §8) preserved. |
| VIII — Perf budgets | **PASS (N/A)** | Article VIII is **runtime-only** (Google Benchmark, ±5% regression, hot-path allocator, latency) and the runtime hot path is untouched → trivially PASS. The build-time/compile-RSS deltas (SC-001/006) are **not** an Article VIII surface; they go to the **003 compile-bench / decision-record convention** (R9), the same mechanism 077 used (`.specify/decisions/003-...-verify.md:101`, T046). Article VIII has no compile-time ceiling. |
| IX — Coverage / sanitizers / static analysis | **PASS** | New emitter/host-tool code carries unit coverage; generated headers + new lib targets build under the existing sanitizer tiers. **Coverage scope note:** the generated per-message `.cpp` compile into the libs but live in `${CMAKE_BINARY_DIR}/_codegen/` (build tree), **outside** Article IX's touched-module scope (`include/fixpp/<mod>*`+`src/<mod>*`) — like the existing generated headers — so deep-group `build_`/`validate_` bodies do **not** fall under the 95/85 touched-module gate (verify at `/speckit-verify`). No `src/`/`include/` change expected (like 077). |
| XV — Banned patterns | **PASS** | No hot-path alloc, no runtime-only validation (§6: typed constexpr metadata retained), runtime dictionary path still exists (§13). No new banned pattern. |
| XVI — Spec Kit workflow | **PASS** | Pipeline order honored — Gate A after `/plan`, before `/tasks`; `/clarify` done; `/analyze` next. |
| III — Build toolchain | **PASS** | CMake≥3.28+Ninja+Conan; `tools/` stays build-only (§5) — the codegen tool is not a user-link-time dependency; the new libs are ordinary compiled artifacts consumers link, not tooling. |

The two annotations are the expected, precedented (074–077) codegen
re-narration folded into Gate A. No amendment vote required beyond the Gate-A
annotation; **Gate A confirms** whether I §1 / XVIII §7 need an edited clause vs.
a per-feature annotation line.

## Project Structure

### Documentation (this feature)

```text
specs/078-precompiled-builder-libs/
├── plan.md              # this file
├── research.md          # Phase 0 — R1–R10 (layout, ODR/SC-005 probe, granularity, golden set, #197)
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
│                       #   groups.hpp (+validator traits, R2), messages/<Msg>.{hpp slim decl,
│                       #   .inl inline body, .cpp external-linkage def}, all.hpp; keep the
│                       #   interned-plan/message ordering for determinism (:824-912)
├── main.cpp            # driver — replace per-version write_file("Builders.hpp") (:118) with
│                       #   multi-file write_file over the emitted set; remove monolith path
├── emit.hpp / ir.*     # emitter surface / IR — extend emit signatures; IR unchanged (order intact)
└── (emit_messages.cpp) # read tier — REFERENCE ONLY (unchanged; read/reify split out of scope, A5)

cmake/Codegen.cmake     # emit the file set into _codegen/include; add STATIC targets
                        #   fixpp_builders_<ver> / fixpp_validators_<ver> over the per-message .cpp;
                        #   wire slim headers into determinism/git-clean gates + install; keep
                        #   determinism golden dir defs updated to the golden SET
CMakeLists.txt          # DELETE FIXPP_BUILD_HEAVY_BUILDER_TESTS option (:208-210) + the
                        #   heavy_builder_compile job pool (:347-380) AFTER CI proves legs fit (R8);
                        #   install slim layout not the monolith (:338-345)
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
                        #   surface = every TU that #includes <fixpp/<ns>/Builders.hpp>: 067×5, 069×5,
                        #   077×6 — exact set + count RE-MEASURED at /tasks (census this session: ZERO
                        #   in examples/, so no CI-exercised example break)
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
| Per-message `.cpp` (N TUs/version) instead of one `Builders.cpp` | SC-002 needs static-archive **object granularity** so the linker pulls only used messages; also bounds per-TU compile RSS (the SC-006 win) | one `Builders.o` pulls the whole ~18–20 MiB `.text` → SC-002 fails, and re-creates a single giant TU → SC-006 fails |
| Tri-file per message (`.hpp` decl / `.inl` inline / `.cpp` def) | FR-004+FR-006 require **both** link mode (default) and header-only inline **from one generation**; the decl/def/inline split is the minimal way | header-only-only (issue proposal 2) can't remove the library's own test compile cost (SC-006); lib-only drops FR-006 inline mode |
| Validator traits as a shared `inline` validator-scoped header (R2) | plan-keyed traits are shared; must be one ODR-safe definition across linked-lib + force-inlined validators (FR-007) **and** invisible to the builder graph (SC-005) | traits in the builder surface → SC-005 fails; per-message non-shared traits → duplicate definitions / dedup lost |
| Golden as a file **set** + name/count determinism (R6) | the split multiplies files → a dropped/renamed message is a new failure mode a content-only diff misses | content-only diff on a single golden → silent omission across the split ([[feedback_codegen_golden_exists_narrow_verify_misses_it]]) |
| #197 removal is **CI-gated**, not a blind delete (R8) | Article XVII §7 resource gate — "every leg fits without the pool" is CI-verified, not locally provable; gcc ~2× RSS + MSVC | delete-then-hope re-introduces the exit-143 OOM that motivated #197 |

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
validator-traits placement + the R2a ODR/SC-005 probe as a blocking prerequisite;
(3) the install-scope call (build-tree targets vs. `install(TARGETS)`/export, R3);
(4) the #197-removal sequencing (R8). User `/plan` sign-off is one of the four
mandatory controls.

**User `/plan` sign-off: GRANTED 2026-07-17.** Proceeding to Gate A.
