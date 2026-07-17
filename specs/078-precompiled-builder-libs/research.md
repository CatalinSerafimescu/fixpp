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
| `groups.hpp` | `fixpp::<ns>::groups` shared `G_<no_tag>Args` data structs (today's PASS 4a) + the plan-keyed **validation traits** (see R2). `#pragma once`. | shared, included once |
| `messages/<Msg>.hpp` | **slim declaration** header: `#include "../groups.hpp"`, the `<Msg>Args` struct, and **non-inline `extern` declarations** of `build_<Msg>` (+ `validate_<Msg>`). Under the inline-mode macro (R4) it instead pulls `<Msg>.inl`. | cheap (decls) |
| `messages/<Msg>.inl` | the `build_<Msg>` body (+ `validate_<Msg>` body) as **`inline`** definitions, for header-only mode. | opt-in parse |
| `messages/<Msg>.cpp` | the `build_<Msg>` (+ `validate_<Msg>`) definitions with **external linkage** — the per-message TU compiled into the libs (R3). | compiled once |
| `all.hpp` | aggregator that `#include`s every `messages/<Msg>.hpp` (declarations). Replaces `Builders.hpp`. | slim by default (R5) |

**Rationale.** The emitter already has clean seams: it returns a string and the driver already does per-version `write_file`, so the split is "more `emit_*` calls + more `write_file` lines" in the existing 4-pass structure, not a rewrite. The `groups` block is already a self-contained contiguous data-struct region (`:903-912`). `build_`/`validate_` are `inline` at single sites (`:481`,`:980`) trivially split into a declaration (slim header) + a definition (`.inl` inline / `.cpp` external).

**Alternatives considered.** (a) Per-category folders — rejected, message granularity is what the cost analysis calls for (deep-group messages concentrate the cost; a client wants per-message). (b) One `Builders.cpp` per version instead of per-message `.cpp` — rejected for SC-002 (see R3). (c) Keep the monolith and add only per-message headers (issue #198 proposal 2 alone) — superseded by the precompiled-lib decision (A2), which is what actually removes the compile cost from the library's own tests (SC-006).

**Optional structural variant (decide at `/implement` / Gate A) — single body source.** Instead of emitting the body twice (`.inl` inline + `.cpp` external), emit **one** body file that the header includes under a linkage macro (e.g. `#define FIXPP_BUILD_LINKAGE inline` for `.inl`-mode vs. external for the `.cpp` TU). This collapses the two emissions to a **single source of the body**, making SC-004 byte-identity **structural** rather than a regeneration-discipline assertion, and roughly halves the generated file count. Deferred as an implementation refinement — the acceptance bar (byte-identity both modes) is unchanged either way; flagged so Gate A / `/implement` can pick the single-source form if the emitter supports it cleanly.

## R2 — Placement of the validation machinery (`writer_traits`) — the ODR / SC-005 decision

**Problem.** The real per-field validation logic is the plan-keyed `inline writer_traits<T>` specializations + `_required_`/`_count_` helpers, currently `inline` in `fixpp::wire` (`:957-968`). They are **shared** (keyed by interned plan, deduped across messages). Two hard constraints collide:
1. **SC-005 / FR-005** — a builder-only consumer must carry **zero** `validate_`/traits machine code.
2. **FR-007 mixing** — a consumer may force-inline one `validate_<Msg>` (header-only) while linking `libfixpp_validators_<ver>`; if that message shares a plan with a linked validator, the shared traits must have **one** definition (no duplicate-symbol, no divergent definition).

**Decision.** The plan-keyed validation traits are a **validator-side shared surface**, emitted `inline` into a header included by *both* the validator lib TUs and inline-mode validators — proposed home: a `groups`-adjacent `validators/traits.hpp` (or a clearly validator-scoped region of `groups.hpp`), **never** included by the builder surface. Concretely:
- `build_<Msg>` (slim header, `.inl`, `.cpp`) references **no** validator-side symbol — preserve the existing independence (`build_` = `emit_level_body`; `validate_` = traits) as an **emitter invariant**.
- The traits stay `inline` (explicit specializations → one definition across TUs), so linked-lib + force-inlined validators that share a plan collapse to one definition — ODR-safe mixing.
- Because the traits header is only reachable through the validator surface, a builder-only include graph never pulls it → SC-005 holds.

**This decision is confirmed by a probe, not prose (see R2a).**

### R2a — Pre-emitter ODR/SC-005 probe (blocking prerequisite)

Before finalizing the emitter layout, write a ~30-line probe:
- **(i)** A TU that force-inlines one `validate_<Msg>` sharing a plan with a *linked* validator, and also links `libfixpp_validators_<ver>` → assert compiles + links with **no duplicate-symbol** error.
- **(ii)** `nm` on a builder-only binary (links `libfixpp_builders_<ver>` only, calls one `build_`) → assert **zero** `validate_`/`writer_traits` symbols.

The probe settles R2 (traits home) empirically and becomes the seed for the FR-007 mixing test and the SC-005 `nm` witness. If (i) or (ii) fails, the traits-placement is re-decided before the emitter work — this is why it is a Phase-0 prerequisite, not a test task.

## R3 — Precompiled library targets & granularity (SC-002)

**Decision.** Emit **per-message `.cpp`** TUs and compile them into two always-built STATIC libraries per version:
- `fixpp_builders_<ver>` / alias `fixpp::builders::<ver>` — the `build_<Msg>` objects.
- `fixpp_validators_<ver>` / alias `fixpp::validators::<ver>` — the `validate_<Msg>` objects + the traits TU.

Both are **always built** in a release library build (FR-004); the consumer's opt-in is purely link-time (link builders, validators, both, or neither).

**Rationale — per-message `.cpp`, not one `Builders.cpp`.** A static archive is pulled at **object granularity**: one `.o` per message means the linker pulls only the referenced messages' objects → SC-002 ("binary contains only the machine code for the called messages"). One monolithic `Builders.o` would pull the whole ~18–20 MiB `.text`, defeating SC-002. Per-message TUs also bound per-TU compile RSS (one message + `groups.hpp`, ~hundreds of MB, parallelizable) — this is the "compile once, per message" win that makes SC-006 real and retires the #197 stopgap (R8).

**Naming.** Follows the `fixpp_<component>`/`fixpp::<component>` convention (`src/wire/CMakeLists.txt:11,18`); `builders::<ver>`/`validators::<ver>` mirrors the sibling `fixpp::dict::<ver>` codegen tier (`Codegen.cmake:532-535`).

**Install/export.** Today the typed tier ships header-only (`CMakeLists.txt:326-345`); there is no `install(TARGETS)` for `src/` static libs. Whether the new libs need `install(TARGETS)`/`install(EXPORT)` (installed-consumer story) vs. build-tree-only (tests + in-tree consumers) is a **scope call for `/plan` sign-off / Gate A** — default: build-tree targets consumed by tests + in-tree examples; installed-export deferred unless required, to avoid opening the tier's first export surface in this feature.

## R4 — Header-only inline mode + mixing (FR-006 / FR-007)

**Decision.** A documented macro `FIXPP_BUILDERS_HEADER_ONLY` selects header-only mode; a per-message override macro (e.g. `FIXPP_BUILDERS_HEADER_ONLY_<Msg>` or a small opt-in include `messages/<Msg>.inl` directly) lets a client force-inline a chosen subset. `messages/<Msg>.hpp` branches: default → `extern` declarations (link mode); macro set → `#include "<Msg>.inl"` (inline definitions). Mixing (link the bulk, inline a few) is ODR-safe because (a) the `.inl` inline `build_`/`validate_` are the standard "inline definition may appear in multiple TUs, must be identical" and (b) the shared traits are inline in one header (R2). Validated by the R2a probe extended into the FR-007 test.

**Rationale.** Recovers the cross-TU inlining lost at the precompiled-lib boundary, but only where the client asks. The per-message `.inl` is the *same* body emitted for the `.cpp`, just with `inline` linkage — one generation, both modes (A2).

## R5 — `all.hpp` aggregator semantics (US4 / FR-008) — stays slim by default

**Decision.** `all.hpp` `#include`s every `messages/<Msg>.hpp`. In **default (link) mode** that is N slim declaration headers + `groups.hpp` = the full *surface* at declaration cost, **not** a monolith re-parse. Only when the consumer sets `FIXPP_BUILDERS_HEADER_ONLY` does `all.hpp` transitively pull every `.inl` and reintroduce the full parse cost — **by explicit choice**. The old `fixpp/<ver>/Builders.hpp` include path is **removed** (Q4 / FR-008), an accepted breaking change (tier opt-in + not yet consumed in production); the install step (`CMakeLists.txt:338-345`) ships the slim layout, not the monolith.

**Guard.** A witness must confirm `all.hpp` in default mode does **not** resurrect the ~3.6 GiB include cost (else US4 silently re-creates the very problem removed) — routed to the compile-bench record (R9), not a hard ctest gate.

## R6 — Goldens as a file SET + determinism-test rewrite (FR-010)

**Decision.** The 077 single-file builder goldens are replaced by a **golden file set** per version (`groups.hpp` + `messages/<Msg>.{hpp,inl,cpp}` + `all.hpp`, and the validator surface). `codegen_determinism_test` (`determinism_test.cpp`) is rewritten to:
1. run the tool twice → the whole emitted **file set** is byte-identical;
2. assert a **stable file NAME set and stable file COUNT** (the split multiplies files, so a dropped/renamed message is a *new* failure mode a content-only diff would miss);
3. diff the generated set against the checked-in golden set.

**Backstop.** `codegen-build-graph-check` (`git status --porcelain` clean) remains the catch-all for any golden the diff missed. This is deliberately belt-and-braces — prior features were bitten by golden staleness ([[feedback_codegen_golden_exists_narrow_verify_misses_it]], the determinism-hang-on-stale-golden). Golden regeneration is an explicit `/tasks` item, not a side effect.

## R7 — Completeness proof semantic shift (VI / FR-009)

**Decision + note.** The address-of-everything completeness TUs (`&fixpp::<ns>::build_<Msg>` / `validate_<Msg>`, `builder_completeness_common.hpp:32-37`) today prove both **compiles** and **links** because bodies are inline in-header. After the split, taking the address of a **declaration** proves the symbol **links** (resolved from the lib), not that its body **compiles** — the compile-proof moves to the `fixpp_builders_<ver>` / `fixpp_validators_<ver>` **build target**. This is sound (if a body failed to compile the lib would not build and nothing would link), but it is a real shift: the completeness TU MUST explicitly `add_dependencies(... fixpp_builders_<ver> fixpp_validators_<ver>)` and link them. State it so no one reads the (now cheap) completeness pass as weakened coverage — it is exactly why the heavy tests get cheap (SC-006).

## R8 — #197 stopgap removal: CI-gated sequencing (A4)

**Decision.** Sequence, do not blind-delete:
1. Land the split + relink the heavy TUs against the prebuilt libs (they `#include` slim `all.hpp` decls + LINK `fixpp_builders_<ver>`/`fixpp_validators_<ver>`; the giant compile moves to the per-message lib TUs).
2. **CI proves every tier1 leg (incl. gcc-release ~2× RSS) + tier2 MSVC fits under the 16 GB runner limit without the pool.**
3. Then remove `FIXPP_BUILD_HEAVY_BUILDER_TESTS` (`CMakeLists.txt:208-210`), its 3 preset ON overrides (`CMakePresets.json:26,53,198`), and the `heavy_builder_compile` job pool (`CMakeLists.txt:347-380`).

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
