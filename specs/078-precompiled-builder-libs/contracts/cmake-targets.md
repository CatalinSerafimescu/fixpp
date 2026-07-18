# Contract: CMake library targets + #197 stopgap removal

**Feature**: 078-precompiled-builder-libs · Phase 1

## New library targets (always built)

Per version `<ver>` ∈ {v44, v50sp2, vlatest}, two STATIC libraries, following the
`fixpp_<component>` real-target + `fixpp::<component>` alias convention
(`src/wire/CMakeLists.txt:11,18`; sibling `fixpp::dict::<ver>` at
`Codegen.cmake:532-535`):

| Target | Alias | Sources | Contents |
|---|---|---|---|
| `fixpp_builders_<ver>` | `fixpp::builders::<ver>` | the `messages/<Msg>.builder.cpp` set | `build_<Msg>` external-linkage objects (one `.o`/message) |
| `fixpp_validators_<ver>` | `fixpp::validators::<ver>` | the **disjoint** `messages/<Msg>.validator.cpp` set | `validate_<Msg>` objects + per-message top-level traits |

The two source sets are **physically disjoint** — no `.cpp` is shared between the
libs — so a builder-only link carries zero validator code (SC-003) and
linking both libs raises no duplicate-symbol clash (R1, R2a leg iii). The **shared
group-plan traits** are `inline` in `validators/traits.hpp` (a header the
`<Msg>.validator.{inl,cpp}` include), **not** a separately compiled "traits TU" —
being `inline` explicit specializations they contribute no standalone object.

- **Always built** in a release library build (FR-004). The consumer's opt-in is
  purely **link-time** — link builders, validators, both, or neither.
- **Object granularity (SC-002):** one `.o` per message per side so a static-archive
  link pulls only referenced messages. **Per-message `.cpp` is the primary path.**
  The `-ffunction-sections` + `--gc-sections` alternative can satisfy SC-002
  (link-only granularity) from a *monolithic* per-lib TU, but it does **not**
  reproduce the per-message-`.o` **compile-RSS** basis that SC-006 / the #197
  removal depend on (it re-creates one giant lib-compile TU per version). Treat it
  strictly as a fallback whose peak compile RSS must be **CI-measured, not
  assumed**, before use — it is not an equivalent path (New-5).
- **Builder ⟂ validator (SC-003):** `fixpp_builders_<ver>` objects reference **no**
  validator symbol; enforced by the emitter invariant + `nm` witness + R2a probe.
- **Generated at configure time** (existing `execute_process`, `Codegen.cmake:458`);
  the per-message `.cpp` files are added as target sources after generation.

## Install / export scope — DECIDED: build-tree + in-tree only (Gate A round 1, R3)

Today the tier is header-only-installed (`CMakeLists.txt:326-345`); there is no
`install(TARGETS)` for `src/` static libs. **Decision (Gate A round 1):** the six
new libs are **build-tree targets consumed by the library's own tests + in-tree
examples**. `install(TARGETS)`/`install(EXPORT)` (plus Conan/package-config
coverage) for an **installed external consumer is deferred** to a follow-up — it
would open the tier's first export surface.

**Coherence rule:** while the targets are unexported, the slim generated headers
are **not installed for external linking** either. Installing a declaration surface
that names `build_<Msg>`/`validate_<Msg>` symbols whose only definitions live in
unexported libs would ship unresolvable link symbols (the incoherence Gate A
flagged, Codex-3). The install step therefore does **not** ship the typed slim
layout in this feature; US1/FR-002/FR-004 are narrowed to build-tree + in-tree
consumers accordingly (spec Clarifications, Gate A round 1).

## #197 stopgap removal (CI-gated — R8)

**Sequence (do not blind-delete):**
1. Land the split + relink the heavy TUs against the prebuilt libs.
2. **CI proves the sanitizer-instrumented legs fit** the 16 GB / 4-core runner limit
   **without** the job pool — every tier1 C++ leg (incl. `linux-clang-asan`/`ubsan`/
   `tsan` and `linux-gcc-release` ~2× RSS), **the python-bindings `asan`/`ubsan`/
   `tsan` legs** (`tier1.yml:628-631,742-753` — they build the heavy TUs because they
   configure `cmake --preset linux-clang-debug`, which carries
   `FIXPP_BUILD_HEAVY_BUILDER_TESTS=ON` at `CMakePresets.json:26`), and tier2 MSVC.
   The main-CI OOM findings (Tier1 `29565095705` / Tier2 `29565095713`, commit
   `df04e7df`) show the flag-ON build passes uninstrumented but OOMs (exit-143)
   once ASan/UBSan lands on the ~78 MB monolith TUs, so the **sanitizer** legs —
   not the uninstrumented build — are the binding constraint. The reused preset
   multiplies the heavy-TU legs beyond the named C++ presets
   ([[feedback_reachability_built_table_misses_bypassing_surface]]).

   **Evidence run (Gate A round 2 — the legs skip on this PR).** The python-bindings
   sanitizer legs are **path-gated**: on a `pull_request` they run only when
   `python_touched=='true'` (`tier1.yml:628-631`), keyed by
   `PY_RE='^(bindings/python/|include/fix/c_api|.*\.i$)'` (`tier1.yml:131`). Feature
   078 touches **none** of those paths (FR-011), so on the 078 PR they **skip** — a
   PR gate on them would pass **vacuously** and `push:main` (which sets
   `python_touched=true` unconditionally, `tier1.yml:141`) would then OOM `main`
   post-merge. Gate the deletion on an explicit evidence run that forces those legs
   on the **post-split** tree: **(primary)** a `workflow_dispatch` (or feature-branch
   `push`) run — any non-`pull_request` event sets `python_touched=true` (`:133-141`)
   and `proceed=true` (`:150-153`), so it exercises the python-bindings + all tier1
   C++ sanitizer legs — captured as the removal's evidence; **or (fallback)** split
   the deletion into a follow-up PR landing only after a `push:main`/dispatch run has
   proven those legs. Do **not** permanently broaden `PY_RE` (over-couples the wheel
   build + four sanitizer legs to every future CMake change). Local `/speckit-verify`
   cannot supply this evidence.
3. Then remove (only after the step-2 evidence run is green):
   - option `FIXPP_BUILD_HEAVY_BUILDER_TESTS` (`CMakeLists.txt:208-210`)
   - the 3 preset ON overrides (`CMakePresets.json:26` linux-clang-debug, `:53`
     linux-clang-asan, `:198` windows-msvc-release)
   - the Ninja `JOB_POOLS heavy_builder_compile=2` block (`CMakeLists.txt:347-380`)

**Fallback:** if a leg regresses, **re-scope the minimum still-needed guard — never
silently drop it** (spec / A4). Keep `/bigobj` on the completeness TUs
(`tests/codegen/CMakeLists.txt:236-241,299-304`) as cheap insurance.

## Test relinking

The heavy TUs (`tests/codegen/CMakeLists.txt:217-308`,
`tests/session/CMakeLists.txt:2609-2700`) and the 067 v44 tier drop the
`#include`-monolith + `-D..._BUILDERS_HPP` / `target_include_directories(...
FIXPP_CODEGEN_OUT)` wiring and instead `#include` slim `all.hpp` +
`target_link_libraries(<tgt> PRIVATE fixpp::builders::<ver> fixpp::validators::<ver>)`;
completeness TUs add `add_dependencies` on the lib targets (R7).

## Determinism / git-clean gates

`codegen_determinism_test` golden-dir defs (`CMakeLists.txt:383-388`) update to the
golden **set** (contracts/golden/); `codegen-build-graph-check` (`git
status --porcelain` clean) remains a backstop.

`codegen-source-staleness-check` must be **re-pointed, not just kept**: it
path-greps `v44/Builders.hpp` for `builder_registry.size() == 83`
(`tests/codegen/codegen_source_staleness_test.cmake:96-108`, via `FIXPP_BUILDERS_HEADER`
at `tests/codegen/CMakeLists.txt:437`). FR-008 deletes `Builders.hpp`, so re-point
`FIXPP_BUILDERS_HEADER` at the new home of `builder_registry` (`all.hpp`, data-model.md
Entity 5).

This is **one member of a family** of `#include`-independent monolith-name gates
(New-C) — the build-graph markers in `cmake/Codegen.cmake` (`:267,273,285,623`), the
build-graph existence assertion (`codegen_build_graph_test.cmake:119-125`), the
`no_emit` / `vlatest_compile_smoke` / `builder_dedup_count` / `mutation_witness`
existence + text-parse gates, the `determinism_test.cpp` existence/OFF-path/golden-diff
logic, and the `tests/codegen/CMakeLists.txt` `-D..._BUILDERS_HPP` path defs. The
**complete** set with per-file dispositions lives in the **plan.md migration census
(Gate A round 2)** — the earlier `#include`-only framing under-counted them
([[feedback_reachability_built_table_misses_bypassing_surface]]). Note two re-point
targets: `builder_registry` → `all.hpp`, but the `G_<no_tag>Args` structs → the
per-plan `groups/<Plan>.hpp` headers (so the group-struct text-parse gates re-point
to the `groups/` dir, not the umbrella `groups.hpp` and not `all.hpp`).
