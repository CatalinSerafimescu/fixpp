# Contract: CMake library targets + #197 stopgap removal

**Feature**: 078-precompiled-builder-libs · Phase 1

## New library targets (always built)

Per version `<ver>` ∈ {v44, v50sp2, vlatest}, two STATIC libraries, following the
`fixpp_<component>` real-target + `fixpp::<component>` alias convention
(`src/wire/CMakeLists.txt:11,18`; sibling `fixpp::dict::<ver>` at
`Codegen.cmake:532-535`):

| Target | Alias | Sources | Contents |
|---|---|---|---|
| `fixpp_builders_<ver>` | `fixpp::builders::<ver>` | `messages/<Msg>.cpp` (build side) | `build_<Msg>` external-linkage objects (one `.o`/message) |
| `fixpp_validators_<ver>` | `fixpp::validators::<ver>` | `messages/<Msg>.cpp` (validate side) + traits TU | `validate_<Msg>` objects + plan-keyed traits |

- **Always built** in a release library build (FR-004). The consumer's opt-in is
  purely **link-time** — link builders, validators, both, or neither.
- **Object granularity (SC-002):** one `.o` per message per lib so a static-archive
  link pulls only referenced messages. (If per-message TUs prove impractical, the
  fallback is `-ffunction-sections` + `--gc-sections`; per-message `.cpp` is the
  primary — decided at `/implement`, but SC-002 is the acceptance bar either way.)
- **Builder ⟂ validator (SC-005):** `fixpp_builders_<ver>` objects reference **no**
  validator symbol; enforced by the emitter invariant + `nm` witness + R2a probe.
- **Generated at configure time** (existing `execute_process`, `Codegen.cmake:458`);
  the per-message `.cpp` files are added as target sources after generation.

## Install / export scope (Gate-A confirm, R3)

Today the tier is header-only-installed (`CMakeLists.txt:326-345`); there is no
`install(TARGETS)` for `src/` static libs. **Default:** build-tree targets consumed
by tests + in-tree examples; `install(TARGETS)`/`install(EXPORT)` for an installed
consumer is **deferred** unless required (avoids opening the tier's first export
surface in this feature). Gate A confirms.

## #197 stopgap removal (CI-gated — R8)

**Sequence (do not blind-delete):**
1. Land the split + relink the heavy TUs against the prebuilt libs.
2. **CI proves** every tier1 leg (incl. `linux-gcc-release` ~2× RSS) + tier2 MSVC
   fits under the 16 GB / 4-core runner limit **without** the job pool.
3. Then remove:
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
status --porcelain` clean) and `codegen-source-staleness-check` remain the
backstops.
