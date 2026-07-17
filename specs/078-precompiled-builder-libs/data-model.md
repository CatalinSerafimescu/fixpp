# Phase 1 Data Model: Precompiled per-version builder/validator libraries

**Feature**: 078-precompiled-builder-libs · **Date**: 2026-07-17

This tier is source-generation + build-packaging; the "entities" are the
**emitted artifacts**, the **build targets**, and the **golden set** — not
runtime data. Relationships and invariants below drive `/tasks`.

## Entity 1 — Shared groups + validator-traits header (`groups.hpp`)

- **Per version** `<ns>` ∈ {v44, v50sp2, vlatest}. One file `fixpp/<ns>/groups.hpp`.
- **Fields:** the deduped `fixpp::<ns>::groups::G_<no_tag>[_<ord>]Args` data structs (today's PASS 4a, `emit_builders.cpp:903-912`), **plus** the plan-keyed validator traits/helpers currently `inline` in `fixpp::wire` (`:957-968`) — relocated here as an `inline`, **validator-scoped** surface (R2).
- **Invariants:** `#pragma once`; included exactly once effectively even when pulled from many per-message headers (FR-012); pure data structs + `inline` template specializations (no external-linkage defs); children-before-parents ordering preserved (`:899-902`); the builder surface (Entity 2 `build_`) references **groups data structs only**, never the traits (SC-005 / R2 emitter invariant).
- **Relationships:** included by every `messages/<Msg>.hpp` (Entity 2) and by both lib TUs (Entity 4).

## Entity 2 — Slim per-message declaration header (`messages/<Msg>.hpp`)

- **Per message** (412 total across 3 versions). `fixpp/<ns>/messages/<Msg>.hpp`.
- **Fields:** `#include "../groups.hpp"`; the `<Msg>Args` struct (`emit_args_struct`, `:306`); **non-inline `extern` declarations** of `build_<Msg>(std::span<std::byte>, <Msg>Args const&)` and `validate_<Msg>(<Msg>Args const&)`.
- **Mode branch:** if the inline-mode macro is set for this message (R4), the header pulls `#include "<Msg>.inl"` instead of the declarations.
- **Invariants:** including one message header costs ~(one Args + shared groups), never the monolith (SC-001); declaring `validate_<Msg>` is free (a decl) and does **not** pull validator code — a send-only consumer that never *calls* it links no validator object (SC-003/SC-005).
- **Relationships:** aggregated by `all.hpp` (Entity 5); its declared symbols are defined by Entity 3 (inline) or Entity 4 (linked lib).

## Entity 3 — Per-message inline body (`messages/<Msg>.inl`)

- **Per message.** `fixpp/<ns>/messages/<Msg>.inl`.
- **Fields:** the `inline` `build_<Msg>` body (`emit_build_fn`/`emit_level_body`, `:479-490`) and `inline` `validate_<Msg>` body (thin wrapper, `:980-986`).
- **Invariants:** identical body to Entity 4's external-linkage `.cpp` (same generation, differing only in linkage) → byte-identical wire output in either mode (SC-004, FR-009); ODR-safe when force-inlined alongside a linked lib (FR-007) because the shared traits are the single inline definition in Entity 1.
- **Relationships:** pulled by Entity 2 only under the inline-mode macro; never compiled standalone.

## Entity 4 — Per-message library TU (`messages/<Msg>.cpp`) → the precompiled libs

- **Per message.** `fixpp/<ns>/messages/<Msg>.cpp` — the **external-linkage** definitions of `build_<Msg>` / `validate_<Msg>`.
- **Compiled into:** `fixpp_builders_<ver>` (the `build_` object) and `fixpp_validators_<ver>` (the `validate_` object + the traits TU). One `.o` per message per lib → static-archive object granularity (SC-002).
- **Invariants:** always built in a release library build (FR-004); the builder object references no validator symbol (SC-005); per-TU compile RSS bounded to one message + `groups.hpp` (the SC-006 basis).
- **Relationships:** resolves the `extern` decls of Entity 2; is the compile-proof locus for the completeness census (Entity 6, R7).

## Entity 5 — Aggregator (`all.hpp`) — replaces `Builders.hpp`

- **Per version.** `fixpp/<ns>/all.hpp` — `#include`s every `messages/<Msg>.hpp`.
- **Invariants:** in **default (link) mode**, N slim declaration headers + `groups.hpp` = the full surface at declaration cost, **not** a monolith re-parse (R5 guard); under `FIXPP_BUILDERS_HEADER_ONLY` it transitively pulls every `.inl` (full parse, by choice). The old `fixpp/<ns>/Builders.hpp` path is **removed** (FR-008, Q4). The install step ships the slim layout (`CMakeLists.txt:338-345`).
- **Relationships:** the migration target for the library's own goldens/tests; the "give me everything" entry point for consumers.

## Entity 6 — Builder/validator completeness census (VI / FR-009 / R7)

- **Per version.** The address-of-everything TUs (`&fixpp::<ns>::build_<Msg>` / `validate_<Msg>`, `builder_completeness_common.hpp:32-37`) + the checked-in `tests/codegen/generated/<ver>_builder_completeness_entries.def`.
- **Semantic shift (R7):** taking the address of a **declaration** proves the symbol **links** (from the lib), not that its body **compiles** — compile-proof moves to Entity 4's build target. The census TU MUST `add_dependencies(... fixpp_builders_<ver> fixpp_validators_<ver>)` and link them.
- **Invariants:** exact-set equality (every in-scope message present, none extra); mutation-witness stays red under a dropped entry (`builder_completeness_mutation_witness_test.cpp`).

## Entity 7 — Golden file SET + determinism (FR-010 / R6)

- **Per version.** Replaces the single-file 077 goldens (`{v44_Builders_all,v50sp2_Builders,vlatest_Builders}.golden.hpp`) with a **golden set**: `groups.hpp` + `messages/<Msg>.{hpp,inl,cpp}` + `all.hpp` (+ validator surface). Lives under `specs/078-.../contracts/golden/`.
- **Invariants (rewritten `determinism_test.cpp`):** (1) run tool twice → whole file set byte-identical; (2) stable file **NAME set + COUNT** (a dropped/renamed message is a new failure mode); (3) generated set == golden set. Backstop: `codegen-build-graph-check` git-clean gate.

## Entity 8 — Build targets & options (FR-002/003/004, R3/R8)

- **New STATIC libs (always built):** `fixpp_builders_<ver>` / `fixpp::builders::<ver>` and `fixpp_validators_<ver>` / `fixpp::validators::<ver>`, for each of v44/v50sp2/vlatest (6 targets). Naming per `fixpp_<component>` convention (`src/wire/CMakeLists.txt:11,18`).
- **Removed (CI-gated, R8):** the `FIXPP_BUILD_HEAVY_BUILDER_TESTS` option (`CMakeLists.txt:208-210`), its 3 preset ON overrides (`CMakePresets.json:26,53,198`), and the `heavy_builder_compile` Ninja job pool (`CMakeLists.txt:347-380`).
- **Relationships:** the heavy test TUs (`tests/{codegen,session}/`) drop the `#include`-the-monolith + `-D..._BUILDERS_HPP` wiring and instead `#include` slim `all.hpp` + `target_link_libraries` the two libs.

## Cross-entity invariants (the load-bearing ones)

1. **Builder ⟂ validator (SC-005):** the `build_` include graph and object never reach validator traits/symbols. Emitter invariant + `nm` witness + R2a probe.
2. **One generation, two modes (SC-004/FR-009):** Entity 3 (`.inl`) and Entity 4 (`.cpp`) are the same body at different linkage → byte-identical in link and inline mode.
3. **Object granularity (SC-002):** per-message `.o` in the archive → link only used messages.
4. **Slim by default (R5):** `all.hpp` in link mode ≠ monolith re-parse.
5. **Determinism over a SET (FR-010):** name-set + count + content, not content-only.
