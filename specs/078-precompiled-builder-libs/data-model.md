# Phase 1 Data Model: Precompiled per-version builder/validator libraries

**Feature**: 078-precompiled-builder-libs · **Date**: 2026-07-17

This tier is source-generation + build-packaging; the "entities" are the
**emitted artifacts**, the **build targets**, and the **golden set** — not
runtime data. Relationships and invariants below drive `/tasks`.

## Entity 1 — Shared groups data header (`groups.hpp`) — DATA-ONLY

- **Per version** `<ns>` ∈ {v44, v50sp2, vlatest}. One file `fixpp/<ns>/groups.hpp`.
- **Fields:** the deduped `fixpp::<ns>::groups::G_<no_tag>[_<ord>]Args` **data structs only** (today's PASS 4a, `emit_builders.cpp:903-912`). **No validator traits** — they move out of the builder-included surface entirely (Entity 1b, R2).
- **Invariants:** `#pragma once`; included exactly once effectively even when pulled from many per-message headers (FR-012); **pure data structs** (no `inline` trait specializations, no external-linkage defs); children-before-parents ordering preserved (`:899-902`); the builder surface (Entity 2 `build_`) references **groups data structs only** (SC-003 / R2 emitter invariant).
- **Relationships:** included by every `messages/<Msg>.hpp` (Entity 2) and by both lib TU sides (Entity 4). Because it is trait-free, a builder-only include graph parses no validator machinery (SC-001).

## Entity 1b — Shared validator traits header (`validators/traits.hpp`)

- **Per version.** One file `fixpp/<ns>/validators/traits.hpp`.
- **Fields:** the **shared group-plan** `inline writer_traits<T>` specializations + `_required_`/`_count_` helpers (`emit_builders.cpp:959-961`, the `for p : intern.plans` loop) — relocated out of `fixpp::wire`-in-`groups.hpp` into a validator-scoped header. Per-message top-level traits do **not** live here (they are per-message — Entities 3/4).
- **Include edge (Gate A round 2):** `validators/traits.hpp` MUST `#include "../groups.hpp"`. Each shared trait is a specialization over the fully-qualified group `Args` struct (`emit_builders.cpp:959-960` emits `writer_traits<::fixpp::<ns>::groups::<plan>>`, and the specialized type must already be complete — emitter comment `:954-955`), so the header does not compile standalone without `groups.hpp`. Harmless to the builder⟂validator split: pulling data-only `groups.hpp` into a validator-scoped header creates no cycle and no builder→validator leak (the builder surface still never includes `traits.hpp`).
- **Invariants:** `#pragma once`; `inline` explicit specializations → one definition across TUs (ODR-safe when a force-inlined validator shares a plan with a linked one, FR-007); **never** `#include`d by the builder surface (slim `<Msg>.hpp`, `<Msg>.builder.inl`, `<Msg>.builder.cpp`) — reachable only through the validator surface (SC-003). Being `inline`, it needs no standalone compiled TU.
- **Relationships:** `#include`d by `<Msg>.validator.inl` (Entity 3) and `<Msg>.validator.cpp` (Entity 4, validator side) only.

## Entity 2 — Slim per-message declaration header (`messages/<Msg>.hpp`)

- **Per message** (412 total across 3 versions). `fixpp/<ns>/messages/<Msg>.hpp`.
- **Fields:** `#include "../groups.hpp"`; the `<Msg>Args` struct (`emit_args_struct`, `:306`); **non-inline `extern` declarations** of `build_<Msg>(std::span<std::byte>, <Msg>Args const&)` and `validate_<Msg>(<Msg>Args const&)`.
- **Mode branch (per side):** if the builder-inline macro is set for this message (R4), the header pulls `#include "<Msg>.builder.inl"` instead of the `build_` declaration; independently, if the validator-inline macro is set, it pulls `#include "<Msg>.validator.inl"` instead of the `validate_` declaration. The two sides are independently selectable, so builder-inline never pulls validator traits.
- **Invariants:** including one message header costs ~(one Args + shared groups), never the monolith (SC-001); declaring `validate_<Msg>` is free (a decl) and does **not** pull validator code — a send-only consumer that never *calls* it links no validator object (SC-003).
- **Relationships:** aggregated by `all.hpp` (Entity 5); its declared symbols are defined by Entity 3 (inline) or Entity 4 (linked lib).

## Entity 3 — Per-message inline bodies (`messages/<Msg>.builder.inl` / `<Msg>.validator.inl`)

- **Per message, two files.** `fixpp/<ns>/messages/<Msg>.builder.inl` and `<Msg>.validator.inl`.
- **Fields:** `.builder.inl` = the `inline` `build_<Msg>` body (`emit_build_fn`/`emit_level_body`, `:479-490`), referencing **groups data structs only**. `.validator.inl` = the `inline` `validate_<Msg>` body (thin wrapper, `:980-986`) **plus this message's per-message top-level traits** (`:963-966`); it `#include`s `../validators/traits.hpp` (Entity 1b) for the shared group-plan traits.
- **Invariants:** each side's `.inl` body is identical to the same side's external-linkage `.cpp` (Entity 4) — same generation, differing only in linkage → the **builder** produces byte-identical wire output and the **validator** is result-identical (same success/error and same offending tag for the same `Args`; it returns a result, not wire bytes) in either mode (SC-004, FR-009); ODR-safe when force-inlined alongside a linked lib (FR-007) because the shared group-plan traits are the single inline definition in Entity 1b; the **builder** `.inl` references no validator symbol, so force-inlining `build_<Msg>` pulls no validator machine code (SC-003 on the inline path).
- **Relationships:** `.builder.inl` pulled by Entity 2 only under the builder-inline macro; `.validator.inl` only under the validator-inline macro; never compiled standalone.

## Entity 4 — Per-message, per-side library TUs (`<Msg>.builder.cpp` / `<Msg>.validator.cpp`) → the precompiled libs

- **Per message, two files.** `fixpp/<ns>/messages/<Msg>.builder.cpp` — the **external-linkage** `build_<Msg>` def; `fixpp/<ns>/messages/<Msg>.validator.cpp` — the **external-linkage** `validate_<Msg>` def (+ the per-message top-level traits; `#include "../validators/traits.hpp"`).
- **Compiled into:** `<Msg>.builder.cpp` → `fixpp_builders_<ver>` **only**; `<Msg>.validator.cpp` → `fixpp_validators_<ver>` **only**. **Disjoint symbol sets** — one `.o` per message per side → static-archive object granularity (SC-002), a builder-only link carries zero validator code (SC-003), and linking **both** libs raises no duplicate-symbol clash (R1).
- **Invariants:** always built in a release library build (FR-004); the builder object references no validator symbol (SC-003); per-TU compile RSS bounded to one message + `groups.hpp` (the SC-006 basis).
- **Relationships:** resolves the `extern` decls of Entity 2; is the compile-proof locus for the completeness census (Entity 6, R7).

## Entity 5 — Aggregator (`all.hpp`) — replaces `Builders.hpp`

- **Per version.** `fixpp/<ns>/all.hpp` — `#include`s every `messages/<Msg>.hpp` **and hosts the per-version `builder_registry` aggregate** (`emit_builders.cpp:929`, `inline constexpr ::std::array<builder_registry_entry, N>`; a whole-version symbol odr-used by the completeness census — its documented home post-split, New-1).
- **Invariants:** in **default (link) mode**, N slim declaration headers + `groups.hpp` = the full surface at declaration cost, **not** a monolith re-parse (R5 guard); under `FIXPP_BUILDERS_HEADER_ONLY` (and/or `FIXPP_VALIDATORS_HEADER_ONLY`) it transitively pulls every `.builder.inl` (and/or `.validator.inl`) (full parse, by choice). The old `fixpp/<ns>/Builders.hpp` path is **removed** (FR-008, Q4). This is the build-tree slim layout tests + in-tree consumers include; external install/export is deferred with the targets (R3, build-tree-only decision).
- **Relationships:** the migration target for the library's own goldens/tests; the "give me everything" entry point for in-tree consumers.

## Entity 6 — Builder/validator completeness census (VI / FR-009 / R7)

- **Per version.** The address-of-everything TUs (`&fixpp::<ns>::build_<Msg>` / `validate_<Msg>`, `builder_completeness_common.hpp:32-37`) + the checked-in `tests/codegen/generated/<ver>_builder_completeness_entries.def`.
- **Semantic shift (R7):** taking the address of a **declaration** proves the symbol **links** (from the lib), not that its body **compiles** — compile-proof moves to Entity 4's build target. The census TU MUST `add_dependencies(... fixpp_builders_<ver> fixpp_validators_<ver>)` and link them.
- **Invariants:** exact-set equality (every in-scope message present, none extra); mutation-witness stays red under a dropped entry (`builder_completeness_mutation_witness_test.cpp`).

## Entity 7 — Golden file SET + determinism (FR-010 / R6)

- **Per version.** Replaces the single-file 077 goldens (`{v44_Builders_all,v50sp2_Builders,vlatest_Builders}.golden.hpp`) with a **golden set**: `groups.hpp` + `validators/traits.hpp` + `messages/<Msg>.{hpp,builder.inl,validator.inl,builder.cpp,validator.cpp}` + `all.hpp`. Lives under `specs/078-.../contracts/golden/`.
- **Invariants (rewritten `determinism_test.cpp`):** (1) run tool twice → whole file set byte-identical; (2) stable file **NAME set + COUNT** (a dropped/renamed message is a new failure mode); (3) generated set == golden set. Backstop: `codegen-build-graph-check` git-clean gate.

## Entity 8 — Build targets & options (FR-002/003/004, R3/R8)

- **New STATIC libs (always built):** `fixpp_builders_<ver>` / `fixpp::builders::<ver>` (sources = the `<Msg>.builder.cpp` set) and `fixpp_validators_<ver>` / `fixpp::validators::<ver>` (sources = the **disjoint** `<Msg>.validator.cpp` set), for each of v44/v50sp2/vlatest (6 targets). Naming per `fixpp_<component>` convention (`src/wire/CMakeLists.txt:11,18`).
- **Removed (CI-gated, R8):** the `FIXPP_BUILD_HEAVY_BUILDER_TESTS` option (`CMakeLists.txt:208-210`), its 3 preset ON overrides (`CMakePresets.json:26,53,198`), and the `heavy_builder_compile` Ninja job pool (`CMakeLists.txt:347-380`). Removal is gated on the **sanitizer-instrumented** CI legs — including the **python-bindings `asan`/`ubsan`/`tsan`** legs that reach the flag via the reused `linux-clang-debug` preset (R8) — fitting the runner without the pool.
- **Migration-surface gates (New-C — the COMPLETE non-`#include` class):** FR-008 deletes `Builders.hpp`, so **every** artifact that hardcodes the monolith path/marker or text-parses a whole-version aggregate symbol false-reds or inverts post-split — not just the one `#include`-independent gate round 1 named. The full set (build-graph markers in `cmake/Codegen.cmake`, the build-graph existence assertion, the staleness registry-size grep, the `no_emit`/compile-smoke/dedup-count/mutation-witness existence + text-parse gates, the determinism existence/OFF-path/golden-diff logic, and the `-D..._BUILDERS_HPP` path defs) is enumerated with a per-file disposition in the **plan.md migration census (Gate A round 2)** — cite `[[feedback_reachability_built_table_misses_bypassing_surface]]`. Note the two distinct re-point targets: `builder_registry` moves to **`all.hpp`** (Entity 5), but the `G_<no_tag>Args` group structs move to **`groups.hpp`** (Entity 1) — the group-struct text-parse gates (`test_077_builder_dedup_count.cpp`, `test_077_v42_vt11_completeness_and_c4.cpp` C4) re-point to `groups.hpp`, not `all.hpp`.
- **Relationships:** the heavy test TUs (`tests/{codegen,session}/`) drop the `#include`-the-monolith + `-D..._BUILDERS_HPP` wiring and instead `#include` slim `all.hpp` + `target_link_libraries` the two libs.

## Cross-entity invariants (the load-bearing ones)

1. **Builder ⟂ validator (SC-003):** the `build_` include graph **and object** never reach validator traits/symbols — the builder `.hpp`/`.builder.inl`/`.builder.cpp` include only trait-free `groups.hpp`. Emitter invariant + `nm` witness + R2a probe.
2. **Disjoint objects, both-libs-safe (R1):** `<Msg>.builder.cpp` and `<Msg>.validator.cpp` are physically separate objects with disjoint strong symbols → a builder-only link carries zero validator code (SC-003) **and** linking both libs raises no duplicate-symbol clash (the US1+US2 common case). R2a leg (iii).
3. **One generation, two modes (SC-004/FR-009):** each side's Entity 3 (`.inl`) and Entity 4 (`.cpp`) are the same body at different linkage → the **builder** side is byte-identical wire output, the **validator** side is result-identical (same success/error + offending tag), each in both link and inline mode.
4. **Object granularity (SC-002):** per-message per-side `.o` in the archive → link only used messages.
5. **Slim by default (R5):** `all.hpp` in link mode ≠ monolith re-parse.
6. **Determinism over a SET (FR-010):** name-set + count + content, not content-only.
