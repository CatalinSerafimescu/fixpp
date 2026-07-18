# Phase 0 Research: Precompiled per-version builder/validator libraries

**Feature**: 078-precompiled-builder-libs · **Date**: 2026-07-17 · **Spec**: [spec.md](./spec.md)

All `path:line` refs are the library submodule root
(`research/G19-fix-fpml-iso20022/library`). Two `/plan` recon sweeps established
the emitter internals and the build/test wiring; findings drive the decisions
below.

## Baseline (measured / confirmed)

- **The tier is one `inline`-everything header per version.** `emit_builders(VersionIR const&, CoverageMode)` (`tools/codegen/fixpp-codegen/emit_builders.cpp:793`) returns the whole file as a string; the driver writes `Builders.hpp` per version (`main.cpp:118` → build tree `${CMAKE_BINARY_DIR}/_codegen/include/fixpp/<ns>/Builders.hpp`, `cmake/Codegen.cmake:94`). One file per version, **no** `groups.hpp` today.
- **Four emission passes into one writer** (`emit_builders.cpp:805`): PASS 4a the shared `fixpp::<ns>::groups` `G_<no_tag>Args` data structs (`:903-912`); PASS 4b per-message `<Msg>Args` (`emit_args_struct` `:306/:925`) + `inline build_<Msg>` (`emit_build_fn` `:479/:481/:926`); a `fixpp::wire` block of `inline writer_traits<T>` specializations + `_required_`/`_count_` helpers (`emit_writer_traits_for_level` `:662-707/:957-968`); PASS 4c the `inline validate_<Msg>` thin wrappers (`:975-990`, each `return ::fixpp::wire::validate_required(args);`).
- **`build_` and `validate_` are independent today.** `build_` serializes via `emit_level_body` (`:490`); `validate_` depends on the `writer_traits<T>` specializations (`:957-968`) + the generic walk in `include/fixpp/wire/builder_validate.hpp`. They share the `<Msg>Args` struct and nothing else.
- **Scale** (messages / interned shared plans): v44 83/88, v50sp2 156/558, vlatest 173/576 (+~4,500 `validate_`-side specializations/helpers). vt11 0 (self-skips `:878-880`); v42 gated off at the driver (`main.cpp:103`).
- **Determinism** rests on ordered inputs: message order = `Dictionary` bytewise-sorted MsgType (`ir.cpp:598-603`); interned-plan order = append order (children-before-parents, `:899-902`); ordinal naming first-encounter (`:273-282`). `unordered_map` is used only for grouping, never for emission order.
- **Core does not consume the tier** (verified grep): zero `#include` of `Builders.hpp` and zero `vX::build_`/`validate_` calls in `src/`,`include/`,`capi/`,`bindings/`. Only coupling is the generic `validate_required<T>` template in `builder_validate.hpp`, and the dependency points *from* generated code *into* core.
- **#197 stopgap** = `FIXPP_BUILD_HEAVY_BUILDER_TESTS` option (`CMakeLists.txt:208-210`), ON in exactly 3 presets (`CMakePresets.json:26,53,198` = linux-clang-debug, linux-clang-asan, windows-msvc-release), gating the heavy TUs (`tests/codegen/CMakeLists.txt:217-245,280-308`; `tests/session/CMakeLists.txt:2623,2660,2693`); + a Ninja `JOB_POOLS heavy_builder_compile=2` over 11 targets (`CMakeLists.txt:347-380`); + `/bigobj` on the two address-of-everything completeness TUs (`tests/codegen/CMakeLists.txt:236-241,299-304`).
- **Goldens** (single-file monoliths): `specs/077-builder-args-dedup/contracts/golden/{v44_Builders_all,v50sp2_Builders,vlatest_Builders}.golden.hpp` + `specs/069-.../v44_Builders_official.golden.hpp`. Diffed by `codegen_determinism_test` (`tests/codegen/determinism_test.cpp`, wired `CMakeLists.txt:383-388`). Backstop gates: `codegen-build-graph-check` (`git status --porcelain` clean), `codegen-source-staleness-check`.
- **CI tier1 legs** (`.github/workflows/tier1.yml:274-281`): linux-clang-{debug,release,asan,ubsan,tsan}, linux-gcc-release; 16 GB / 4-core runners; exit-143 OOM history at ~3.7–4.6 GiB RSS/TU (~2× under gcc). MSVC in tier2.
- **Target-naming convention:** real `fixpp_<component>` STATIC + alias `fixpp::<component>` (e.g. `src/wire/CMakeLists.txt:11,18`). The codegen INTERFACE tier uses `fixpp_dict_<ver>`/`fixpp::dict::<ver>` (`Codegen.cmake:532-535`).
- **ABI gate is C-ABI only** (`abi-golden.yml` builds `fixpp_capi` only, `nm`-set diff). The typed tier is a mangled C++ surface — it does **not** touch the abidiff/nm gate. C-ABI frozen 1.5.0.

---

## R1 — Emitter output layout (the split)

**Decision.** Per builder-bearing version `<ns>` ∈ {v44, v50sp2, vlatest}, replace the single `Builders.hpp` with this file set under `_codegen/include/fixpp/<ns>/`:

| File | Contents | Cost class |
|---|---|---|
| `groups.hpp` | **DATA-ONLY** — `fixpp::<ns>::groups` shared `G_<no_tag>Args` data structs (today's PASS 4a). **NO validator traits** (they leave the builder-included surface — see R2). `#pragma once`. | shared, included once |
| `validators/traits.hpp` | validator-scoped: the shared **group-plan** `inline writer_traits<T>` specializations + `_required_`/`_count_` helpers (R2, the `emit_builders.cpp:961` `for p : intern.plans` loop). Included **only** by the validator surface (`.validator.inl`/`.validator.cpp`), **never** by the builder slim header. `#pragma once`. | shared, validator-only |
| `messages/<Msg>.hpp` | **slim declaration** header: `#include "../groups.hpp"`, the `<Msg>Args` struct, and **non-inline `extern` declarations** of `build_<Msg>` (+ `validate_<Msg>`). Under a **builder-inline** macro it pulls `<Msg>.builder.inl`; under a **validator-inline** macro it pulls `<Msg>.validator.inl` (R4). | cheap (decls) |
| `messages/<Msg>.builder.inl` | the `build_<Msg>` body as an **`inline`** definition (references **groups data structs only** — no traits), for header-only builder mode. | opt-in parse |
| `messages/<Msg>.validator.inl` | the `validate_<Msg>` body as an **`inline`** definition **plus this message's per-message top-level `<Msg>Args` traits** (`emit_builders.cpp:963-966`); `#include "../validators/traits.hpp"` for the shared group-plan traits. Header-only validator mode. | opt-in parse |
| `messages/<Msg>.builder.cpp` | the `build_<Msg>` definition with **external linkage** — per-message TU compiled **only** into `fixpp_builders_<ver>` (R3). Carries **no** `validate_`/traits symbol. | compiled once |
| `messages/<Msg>.validator.cpp` | the `validate_<Msg>` definition (+ the per-message top-level `<Msg>Args` traits) with **external linkage**; `#include "../validators/traits.hpp"` — per-message TU compiled **only** into `fixpp_validators_<ver>` (R3). | compiled once |
| `all.hpp` | aggregator that `#include`s every `messages/<Msg>.hpp` (declarations) **plus the per-version `builder_registry` aggregate** (`emit_builders.cpp:929`, `inline constexpr`, odr-used by the completeness census). Replaces `Builders.hpp`. | slim by default (R5) |

**Rationale — physically separate builder and validator objects.** The prose invariant (build_ ⟂ validate_) must be **physically realized**, not co-located: each message emits **two** external-linkage objects — `<Msg>.builder.cpp` → the builders lib and `<Msg>.validator.cpp` → the validators lib — with **disjoint symbol sets**. A single `<Msg>.cpp` defining both strong external symbols would (a) drag `validate_` machine code into a builder-only link (SC-003 leak) **and** (b) hard-fail with a duplicate-symbol link error when a consumer links **both** libs (the US1+US2 common case, un-waivable), because each archive's `<Msg>.o` would redefine the other's `build_`/`validate_`. The `.inl` inline surface is split symmetrically (`.builder.inl` / `.validator.inl`) so the *inline* path preserves the same separation — force-inlining `build_<Msg>` never pulls validator traits. The emitter already has clean seams (it returns a string; the driver already does per-version `write_file`), so the split is "more `emit_*` calls + more `write_file` lines" over the existing 4-pass structure, not a rewrite.

**Alternatives considered.** (a) Per-category folders — rejected, message granularity is what the cost analysis calls for (deep-group messages concentrate the cost; a client wants per-message). (b) One `Builders.cpp` per version instead of per-message `.cpp` — rejected for SC-002 (see R3). (c) Keep the monolith and add only per-message headers (issue #198 proposal 2 alone) — superseded by the precompiled-lib decision (A2), which is what actually removes the compile cost from the library's own tests (SC-006).

**Optional structural variant (decide at `/implement` / Gate A) — single body source per side.** Instead of emitting each side's body twice (`.builder.inl` inline + `.builder.cpp` external, and likewise for the validator), emit **one** body file per side that the two wrappers include under a linkage macro (e.g. `#define FIXPP_BUILD_LINKAGE inline` for the `.inl` wrapper vs. external for the `.cpp` TU). This collapses each side's two emissions to a **single source of the body**, making SC-004 byte-identity **structural** rather than a regeneration-discipline assertion. The builder and validator surfaces remain **physically separate** either way; deferred as an implementation refinement — the acceptance bar (byte-identity both modes, disjoint objects) is unchanged. **Drift-watch (Gate A round 2):** if this single-body variant is chosen at `/implement`, the emitted file **NAME set changes** (a shared body file appears per side; the `.inl`/`.cpp` become thin includers), so Entity 7's golden file enumeration (`data-model.md`) and the R6 determinism **name-set + count** assertion (`completeness-and-golden.md`) MUST be regenerated in lockstep to match. The R6 contract stays authoritative over whichever emission form is picked — it self-resolves at regeneration, so this is a drift-watch, not a contradiction.

## R2 — Placement of the validation machinery (`writer_traits`) — the ODR / SC-003 decision

**Problem.** The real per-field validation logic is `writer_traits<T>` specializations + `_required_`/`_count_` helpers, currently `inline` in `fixpp::wire`. The emitter emits them in **two loops** (`emit_builders.cpp:957-968`): the shared **group-plan** traits (`:961`, `for p : intern.plans` — keyed by interned plan, deduped across messages) **and** the **per-message top-level `<Msg>Args` traits** (`:963-966`, `for i : official_msg_ids` — one set per message, not shared). Two hard constraints collide:
1. **SC-003 / FR-005** — a builder-only consumer must carry **zero** `validate_`/traits machine code. *(Note: unreferenced `inline` template specializations are not ODR-used and emit **no machine code**, so merely including a traits header in a builder-only TU does not by itself break SC-003 machine-code — the machine-code leak is the co-located-object problem, R1/root-cause-#1. What placing traits in the builder-included `groups.hpp` breaks is the **R2 invariant + SC-001 parse cost**: every builder slim header would parse the whole validator trait surface.)*
2. **FR-007 mixing** — a consumer may force-inline one `validate_<Msg>` (header-only) while linking `libfixpp_validators_<ver>`; if that message shares a plan with a linked validator, the shared group-plan traits must have **one** definition (no duplicate-symbol, no divergent definition).

**Decision — `groups.hpp` is DATA-ONLY; traits are placed in TWO tiers, both validator-scoped.**
- `groups.hpp` carries **only** the `G_<no_tag>Args` data structs; it carries **no** validator traits (so the builder slim header, which includes `groups.hpp`, never parses trait machinery).
- **Shared group-plan traits** (`:961`) → a validator-only shared header **`validators/traits.hpp`**, emitted `inline`, included by *both* the validator lib TUs and inline-mode validators, **never** by the builder surface.
- **Per-message top-level `<Msg>Args` traits** (`:963-966`) are **not shared** — they go to the **per-message validator surface** (`<Msg>.validator.cpp` / `<Msg>.validator.inl`), which `#include`s `validators/traits.hpp` for the shared group-plan traits it references. A naive whole-hog move of "all traits" into one shared header would re-create the parse-cost leak for the per-message half (every validator TU parsing every other message's top-level traits).
- `build_<Msg>` (slim header, `.builder.inl`, `.builder.cpp`) references **no** validator-side symbol — preserve the existing independence (`build_` = `emit_level_body`; `validate_` = traits) as an **emitter invariant**.
- The shared traits stay `inline` (explicit specializations → one definition across TUs), so a linked-lib validator + a force-inlined **different** validator that share a plan collapse to one definition — ODR-safe trait sharing across different validators (not the same-message case, see R4).
- Because neither `validators/traits.hpp` nor the per-message validator surface is reachable through the builder include graph, a builder-only include never pulls trait machinery → SC-003 + SC-001 hold.

**This decision is confirmed by a probe, not prose (see R2a).**

### R2a — Pre-emitter ODR / builder⟂validator (SC-003) probe (blocking prerequisite)

Before finalizing the emitter layout, write a ~30-line probe covering the **split** layout:
- **(i)** A TU that force-inlines one `validate_<Msg>` sharing a group-plan with a *linked* validator, and also links `libfixpp_validators_<ver>` → assert compiles + links with **no duplicate-symbol** error (the shared group-plan trait has a single `inline` definition in `validators/traits.hpp`).
- **(ii)** `nm` on a builder-only binary (links `libfixpp_builders_<ver>` only, calls one `build_`) → assert **zero** `validate_`/`writer_traits` symbols.
- **(iii)** A consumer that links **both** `libfixpp_builders_<ver>` and `libfixpp_validators_<ver>` (the US1+US2 common case) → assert **no duplicate-symbol** for `build_<Msg>`/`validate_<Msg>` (the disjoint-object property from R1 — this is the hard-failure case the co-located-object layout would have produced).

The probe settles R2 (traits home) and R1 (disjoint objects) empirically and becomes the seed for the FR-007 mixing test and the SC-002/SC-003 `nm` witnesses. If any leg fails, the object/trait placement is re-decided before the emitter work — this is why it is a Phase-0 prerequisite, not a test task.

## R3 — Precompiled library targets & granularity (SC-002)

**Decision.** Emit **per-message, per-side `.cpp`** TUs (`<Msg>.builder.cpp` and `<Msg>.validator.cpp`) and compile the two disjoint sets into two always-built STATIC libraries per version:
- `fixpp_builders_<ver>` / alias `fixpp::builders::<ver>` — the `<Msg>.builder.cpp` objects (`build_<Msg>` only).
- `fixpp_validators_<ver>` / alias `fixpp::validators::<ver>` — the `<Msg>.validator.cpp` objects (`validate_<Msg>` + the per-message top-level traits). The **shared group-plan traits** are `inline` in `validators/traits.hpp` (a header the validator `.cpp`/`.inl` include) — **not** a separate compiled "traits TU"; being `inline` explicit specializations they need no standalone object.

Both are **always built** in a release library build (FR-004); the consumer's opt-in is purely link-time (link builders, validators, both, or neither).

**Rationale — per-message, per-side `.cpp`, not one `Builders.cpp`.** A static archive is pulled at **object granularity**: one `.o` per message per side means the linker pulls only the referenced messages' objects → SC-002 ("binary contains only the machine code for the called messages"). One monolithic `Builders.o` would pull the whole ~18–20 MiB `.text`, defeating SC-002. Splitting `build_` and `validate_` into **separate** objects is what makes a builder-only link carry zero validator code (SC-003) and lets a consumer link **both** libs with no duplicate-symbol clash (R1). Per-message TUs also bound per-TU compile RSS (one message + `groups.hpp`, ~hundreds of MB, parallelizable) — the "compile once, per message" win that makes SC-006 real and retires the #197 stopgap (R8).

**Naming.** Follows the `fixpp_<component>`/`fixpp::<component>` convention (`src/wire/CMakeLists.txt:11,18`); `builders::<ver>`/`validators::<ver>` mirrors the sibling `fixpp::dict::<ver>` codegen tier (`Codegen.cmake:532-535`).

**Install/export — DECIDED (Gate A round 1): build-tree + in-tree consumers only.** Today the typed tier ships header-only (`CMakeLists.txt:326-345`); there is no `install(TARGETS)` for `src/` static libs. The new libs are **build-tree targets consumed by the library's own tests + in-tree examples**; `install(TARGETS)`/`install(EXPORT)` (and Conan/package-config coverage) for an **installed external consumer are deferred** to a follow-up (they would open the tier's first export surface). To stay coherent, the slim generated headers are **not installed for external linking** either while the targets are unexported — installing a declaration surface that names symbols with no exported target to link is the incoherence Gate A flagged (Codex-3). US1/FR-002/FR-004 are narrowed accordingly (spec Clarifications, Gate A round 1).

## R4 — Header-only inline mode + mixing (FR-006 / FR-007)

**Decision.** A documented macro `FIXPP_BUILDERS_HEADER_ONLY` selects builder-inline mode; a separate `FIXPP_VALIDATORS_HEADER_ONLY` selects validator-inline mode; per-message override macros (e.g. `FIXPP_BUILDERS_HEADER_ONLY_<Msg>` / `FIXPP_VALIDATORS_HEADER_ONLY_<Msg>`, or a small opt-in `#include "messages/<Msg>.builder.inl"` directly) let a client force-inline a chosen subset per side. `messages/<Msg>.hpp` branches per side: default → `extern` declaration (link mode); builder-inline set → `#include "<Msg>.builder.inl"`; validator-inline set → `#include "<Msg>.validator.inl"`. Because the inline surfaces are **split**, force-inlining `build_<Msg>` pulls **only** `<Msg>.builder.inl` (+ `groups.hpp`) and no validator trait (SC-003 holds on the inline path too). Mixing (link the bulk, inline a few) across DIFFERENT messages is ODR-safe because (a) the switch is a **program-wide per-message** control — each message is inline in *all* of its referencing TUs or linked in *all* of them, so distinct messages can independently choose either mode without collision — and (b) the shared group-plan traits are inline in one header (`validators/traits.hpp`, R2). The *same* message force-inlined in one TU while linked (strong external) in another TU of the same program is **unsupported**: it produces a weak/COMDAT `inline` definition alongside the archive's strong external definition of the identically-mangled symbol — an ODR violation under [dcl.inline]/4 (IFNDR, no diagnostic required) — and MUST NOT be relied upon. Validated by the R2a probe extended into the FR-007 test (both a builder-side and a validator-side mixing witness); see quickstart.md Scenario 4d for the full contract.

**Rationale.** Recovers the cross-TU inlining lost at the precompiled-lib boundary, but only where the client asks. Each per-message `.inl` is the *same* body emitted for the same side's `.cpp`, just with `inline` linkage — one generation, both modes (A2).

## R5 — `all.hpp` aggregator semantics (US4 / FR-008) — stays slim by default

**Decision.** `all.hpp` `#include`s every `messages/<Msg>.hpp` (and exposes the per-version `builder_registry` aggregate — its documented home post-split, New-1). In **default (link) mode** that is N slim declaration headers + `groups.hpp` = the full *surface* at declaration cost, **not** a monolith re-parse. Only when the consumer sets `FIXPP_BUILDERS_HEADER_ONLY` (and/or `FIXPP_VALIDATORS_HEADER_ONLY`) does `all.hpp` transitively pull every `.builder.inl` (and/or `.validator.inl`) and reintroduce the full parse cost — **by explicit choice**. The old `fixpp/<ver>/Builders.hpp` include path is **removed** (Q4 / FR-008), an accepted breaking change (tier opt-in + not yet consumed in production); the build-tree slim layout under `_codegen/include/` is what tests + in-tree consumers include. External `install(TARGETS)`/header-export is deferred with the targets (R3, build-tree-only decision).

**Guard.** A witness must confirm `all.hpp` in default mode does **not** resurrect the ~3.6 GiB include cost (else US4 silently re-creates the very problem removed) — routed to the compile-bench record (R9), not a hard ctest gate.

## R6 — Goldens as a file SET + determinism-test rewrite (FR-010)

**Decision.** The 077 single-file builder goldens are replaced by a **golden file set** per version — `groups.hpp` + `validators/traits.hpp` + `messages/<Msg>.{hpp,builder.inl,validator.inl,builder.cpp,validator.cpp}` + `all.hpp` (the split multiplies files per message to five plus the two shared headers). `codegen_determinism_test` (`determinism_test.cpp`) is rewritten to:
1. run the tool twice → the whole emitted **file set** is byte-identical;
2. assert a **stable file NAME set and stable file COUNT** (the split multiplies files, so a dropped/renamed message is a *new* failure mode a content-only diff would miss);
3. diff the generated set against the checked-in golden set.

**Backstop.** `codegen-build-graph-check` (`git status --porcelain` clean) remains the catch-all for any golden the diff missed. This is deliberately belt-and-braces — prior features were bitten by golden staleness ([[feedback_codegen_golden_exists_narrow_verify_misses_it]], the determinism-hang-on-stale-golden). Golden regeneration is an explicit `/tasks` item, not a side effect.

## R7 — Completeness proof semantic shift (VI / FR-009)

**Decision + note.** The address-of-everything completeness TUs (`&fixpp::<ns>::build_<Msg>` / `validate_<Msg>`, `builder_completeness_common.hpp:32-37`) today prove both **compiles** and **links** because bodies are inline in-header. After the split, taking the address of a **declaration** proves the symbol **links** (resolved from the lib), not that its body **compiles** — the compile-proof moves to the `fixpp_builders_<ver>` / `fixpp_validators_<ver>` **build target**. This is sound (if a body failed to compile the lib would not build and nothing would link), but it is a real shift: the completeness TU MUST explicitly `add_dependencies(... fixpp_builders_<ver> fixpp_validators_<ver>)` and link them. State it so no one reads the (now cheap) completeness pass as weakened coverage — it is exactly why the heavy tests get cheap (SC-006).

## R8 — #197 stopgap removal: CI-gated sequencing (A4)

**Motivating evidence (main-CI OOM, Gate A round 1).** Two FAILED `main` CI runs (Tier1 `29565095705`, Tier2 `29565095713`, commit `df04e7df`, reproducing on current main HEAD) show the #197 stopgap is **insufficient under sanitizers**: three Tier-1 legs OOM (exit-143, no compile error) building the heavy builder TUs — `linux-clang-asan` **and the python-bindings `asan`/`ubsan`/`tsan` legs**. The python-bindings legs build the heavy TUs because they configure `cmake --preset linux-clang-debug` (`tier1.yml:~746`), and `linux-clang-debug` (`CMakePresets.json:26`) carries `FIXPP_BUILD_HEAVY_BUILDER_TESTS=ON`. The flag-ON build passes uninstrumented but OOMs once **ASan/UBSan** instrumentation lands on the ~78 MB monolith TUs. This is a census gap in the removal gate: the reused `linux-clang-debug` preset **multiplies** the heavy-TU legs beyond the C++ matrix (pattern [[feedback_reachability_built_table_misses_bypassing_surface]] — a leg list built by walking only the named C++ presets misses every leg that reaches the flag via a *reused* preset). The per-message split is what removes the monolith TU and makes these legs fit.

**Decision.** Sequence, do not blind-delete:
1. Land the split + relink the heavy TUs against the prebuilt libs (they `#include` slim `all.hpp` decls + LINK `fixpp_builders_<ver>`/`fixpp_validators_<ver>`; the giant compile moves to the per-message lib TUs).
2. **CI proves the sanitizer-instrumented legs fit the 16 GB runner limit without the pool** — every tier1 C++ leg (incl. `linux-clang-asan`/`ubsan`/`tsan` and gcc-release ~2× RSS), **the python-bindings `asan`/`ubsan`/`tsan` legs** (`tier1.yml:628-631,742-753`; they build the heavy TUs via the reused `linux-clang-debug` preset), and tier2 MSVC. The sanitizer legs — not the uninstrumented build — are the binding constraint.

   **Evidence-path decision (Gate A round 2 — the legs skip on this PR).** The python-bindings sanitizer legs are **path-gated**: on a `pull_request` they run only when `python_touched=='true'` (`tier1.yml:628-631`), computed from `PY_RE='^(bindings/python/|include/fix/c_api|.*\.i$)'` (`tier1.yml:131`). Feature 078 touches **none** of those paths (FR-011: zero `src/`/`capi/`/`bindings/`/`python` change), so on the 078 PR `python_touched=false` and those legs **skip** — a pre-merge gate written against them would pass **vacuously** (skipped ≠ failing), and `push:main` (which sets `python_touched=true` unconditionally, `tier1.yml:141`) would then reintroduce the exit-143 OOM on `main`, post-merge. So the deletion is gated on an **explicit evidence run that forces those legs**, captured on the **post-split** tree: either **(primary)** a `workflow_dispatch` (or feature-branch `push`) run — any non-`pull_request` event sets `python_touched=true` (`tier1.yml:133-141`) and `proceed=true` (`tier1.yml:150-153`), so it exercises the python-bindings sanitizer legs **and** all tier1 C++ sanitizer legs against the split — recorded as the removal's evidence; or **(fallback)** split the deletion into a **follow-up PR** that lands only after a `push:main`/dispatch run has proven those legs on the merged split. Do **not** permanently broaden `PY_RE` (over-couples the ~7-min wheel build + four sanitizer legs to every future CMake change). Local `/speckit-verify` (clang-only + local) **cannot** supply this evidence.
3. Then remove `FIXPP_BUILD_HEAVY_BUILDER_TESTS` (`CMakeLists.txt:208-210`), its 3 preset ON overrides (`CMakePresets.json:26,53,198`), and the `heavy_builder_compile` job pool (`CMakeLists.txt:347-380`) — only after the step-2 evidence run is green.

**Optional check (note, not a hard FR):** confirm the prebuilt-lib compile itself stays bounded under `BUILD_SHARED_LIBS=ON` + ASan (the python-bindings config) — the per-message split is what keeps each lib TU bounded there.

**Fallback (from the spec):** if a leg regresses, **re-scope the minimum still-needed guard — never silently drop it.** Keep `/bigobj` on the completeness TUs as cheap insurance (against declarations the C1128 pressure should vanish, but it costs nothing). Article XVII §7's resource gate means "every leg fits" is CI-verified, not locally provable — the removal is a *gated step*.

## R9 — Success-criterion → witness mapping (so /implement can't mark an unverifiable task done)

| SC | What it measures | Witness (gate) |
|---|---|---|
| SC-001 | one-message-TU compile **RSS/wall** ≪ monolith | **compile-bench decision record** (003/T046 convention — `.specify/decisions/003-dictionary-codegen-verify.md:101` pattern; **NOT** Article VIII, which is runtime-only) |
| SC-002 | link only used messages | `nm` on a subset-linked binary → only called `build_` symbols present (per-message `.o` granularity) |
| SC-003 | send-only binary has zero validator code | `nm` on a builder-only binary → zero `validate_`/`writer_traits` symbols (R2a probe) |
| SC-004 | byte-identical output, link **and** inline | existing round-trip + golden diff, extended to both modes |
| SC-005 | existing full-set = byte-identical via `all.hpp`; old path removed | golden set (R6) + a "no `Builders.hpp`" negative check |
| SC-006 | heavy test TUs fit runner limit | CI RSS on the relinked TUs (R8 step 2) + compile-bench record |
| SC-007 | deterministic regeneration | rewritten `determinism_test` (R6) + `git`-clean backstop |

**Rationale.** SC-001/SC-006 are compile-time RSS/wall — not ctest-assertable; they go to the compile-bench record convention 077 already established, explicitly *not* Article VIII. Making this map explicit prevents an RSS task being marked `[X]` on agent confidence with no real measurement (the exact failure Article XVII §8 / `/speckit-verify` exist to catch).

## R10 — Scope confirmations (from `/clarify`)

- **In scope:** builder + validator tiers for v44 / v50sp2 / vlatest. vt11 (0 builders) and v42 (deferred to #196) unchanged. Read/reify tier split **out** (A5).
- **Core untouched:** zero `src/`/`capi/`/`bindings/`/`python` change; C-ABI frozen 1.5.0; no abidiff-gate impact (mangled C++ surface).
- **ABI boundary (A3):** the precompiled `build_<Msg>(span, Args const&)` boundary is sound because fixpp is built from source in the client toolchain (shared `Args` layout). Cross-toolchain / runtime version selection remains the existing C-ABI runtime builder (`src/capi/message_write.cpp`) — out of scope.
- **Ordering:** #198 before #196 so v42 later emits into the split layout (A6).

**All NEEDS CLARIFICATION resolved.** Ready for Phase 1.
