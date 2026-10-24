# #613 — export the typed builder/validator libraries and install the slim typed headers

> **Status: draft v0.3, 2026-10-24.** Round 1 applied 2026-10-24: Codex P1=0 P2=6 P3=2; Opus post-judging P1=0 P2=8
> P3=10; rewrite addresses root causes 1–5 (named in `## Gate A`); owner ruling R-2a applied. Reviews:
> research/reviews/codex_613_1_builders-export_review.md, research/reviews/opus_613_1_builders-export_triage.md.
> R-2a ("Release packages only") changes the design: the typed tier is exported and installed by Release
> configurations only (D-0).
>
> Issue mode, which is the owner's ruling of 2026-10-24: this note plus issue-mode Gate A and Gate B, with no
> `specs/` bundle. Branch `613-builders-export` in `fixpp-parallel`, based on `origin/main` `4e8b3779`. Batch B66.
> Claims about code name functions, targets, files or rules, never line numbers. A measured value is labelled with
> its commit, date and command; a value whose commit is unrecorded is labelled **estimate** and is not the basis of
> any gate. Every count in this note is a measurement with a recipe, not a constant to copy into code or comments.
>
> **Owner decisions, not open for review:**
> - 2026-10-21: "export all versions for v1.0" (v42, v44, v50sp2, vlatest).
> - 2026-10-24: issue mode.
> - 2026-10-24 (R-1, was OQ-1): "FREEZE span-based Args as the v1.0 public shape (Option 3 stays deferred; a later
>   change would be a 2.0 break)." This is the explicit decision 084 FR-010 requires before an export reaches
>   `messages/` or `groups/`; §9 says where the PR records it.
> - 2026-10-24 (R-2, was OQ-2): keep "all four versions, every platform" at the measured cost (§5).
> - **2026-10-24 (R-2a, amends R-2): "Release packages only."** Put to the owner after the triage's N-3 showed that
>   R-2's cost basis covered Release packages only. Told that Debug packages would grow by about 4.9 GB raw
>   (linux-clang-debug; its largest single archive about 1.36 GB) and about 2.3 GB (MSVC Debug, `.lib` only, before
>   PDBs) — both **estimates**, §5 — the owner ruled that only Release configurations export and install the
>   builder/validator libraries and their slim typed headers. Debug configurations install neither, so a Debug
>   install stays coherent: no installed header declares a `build_`/`validate_` symbol that no installed library
>   defines. D-0 realises it.

## 0. Scope

The five items of fixpp#613, quoted:

1. "Add `fixpp_builders_<ver>` and `fixpp_validators_<ver>` to `FIXPP_EXPORT_TARGETS`, under the switch that builds
   them (`FIXPP_BUILD_CODEGEN_TOOL`), with their `EXPORT_NAME`s. Decide whether the `fixpp::fixpp` umbrella reaches
   them or a consumer links them by name."
2. "Install the slim layout: lift the five exclusions, and keep the generated `.builder.cpp`/`.validator.cpp` out
   (078's 'would leak as public headers'). Check whether an `.inl` belongs to the header surface."
3. "Add an out-of-tree consumer packaging test: `find_package(fixpp)`, call `build_NewOrderSingle` (and one
   v50sp2/vlatest message), link, run. A mutant that removes the export, or restores an exclusion, must turn it RED."
4. "Measure package size before and after, and record it. The gcc release static libraries total about 150 MB
   uncompressed, most of it v50sp2 and vlatest."
5. "Amend 078's contract section and the install comment in `CMakeLists.txt` in the same PR, by name, so the two
   stay consistent."

R-2a narrows items 1 and 2 to Release configurations. Item 3's test is two-sided accordingly (§4).

**Out of scope:**
- Any change to the generated code, the emitter, or the `Args` shape. That is "Option 3", deferred by R-1 (§9).
- The per-version `fixpp::dict::<ver>` INTERFACE targets. They stay unexported, as 084 research R2 decided.
- A runtime/dev component split of the package. 084 shipped one dev-shaped package per configuration.
- Multi-config generators as a packaging configuration (Article III: Ninja only; D-0 says what such a tree does).
- Parent-repository surfaces (§6.3). They are left to the orchestrator.

## 1. Premises checked against source

| # | Issue premise | Finding | Evidence |
|---|---|---|---|
| P-1 | The libs are "under the switch that builds them (`FIXPP_BUILD_CODEGEN_TOOL`)" | **Moot. `FIXPP_BUILD_CODEGEN_TOOL=OFF` does not configure on `origin/main`.** The export block fails closed on `fixpp_dict_dispatch`, which only `cmake/Codegen.cmake` defines. `cmake/FixppPackaging.cmake` also refuses OFF (its FR-011a guard). So an installed package without the builder libs *because they were not built* cannot be produced. This design adds no OFF support. | Probe, 2026-10-24, `4e8b3779`: `cmake -S . -B <scratch> -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=build/linux-gcc-release/conan_toolchain.cmake -DFIXPP_BUILD_TESTS=OFF -DFIXPP_BUILD_PYTHON=OFF -DFIXPP_BUILD_CODEGEN_TOOL=OFF` → rc 1, `084 FR-007: export set names 'fixpp_dict_dispatch', which is not a target` |
| P-2 | One switch gates the libs | **The real per-version switch is `FIXPP_CODEGEN_FIX_LATEST`** (an `option()` in `cmake/Codegen.cmake`, default ON). When it is OFF, the builder/validator loop in `cmake/Codegen.cmake` skips vlatest: no `fixpp_builders_vlatest`, no `fixpp_validators_vlatest`, and the vlatest output directory is removed. `docs/src/dictionary/codegen.md` documents OFF as a developer configure. v42, v44 and v50sp2 have no such switch. **R-2a adds a second, independent condition: the build type (D-0).** | `cmake/Codegen.cmake` (the `foreach(_ver IN ITEMS v42 v44 v50sp2 vlatest)` block and the T015 stale-dir cleanup) |
| P-3 | `EXPORT_NAME`s to be chosen | **The in-tree aliases are nested: `fixpp::builders::<ver>` and `fixpp::validators::<ver>`.** The prefix-strip sweep would give `builders_v44`. T026's alias check then fails at configure, because no `fixpp::builders_v44` alias exists. An explicit nested `EXPORT_NAME` is required. The precedent is `fixpp_dict_dispatch` → `dict::dispatch`. | Probe, 2026-10-24: the planned edit with the override switched off → `084 T026: export member 'fixpp_builders_v42' resolves to imported name …` (rc 1) |
| P-4 | "a consumer of `find_package(fixpp)`, a CPack package **or Conan**" | **This repository defines no Conan package, so there is no Conan component metadata to amend.** `conanfile.py` is a consumer/build recipe (`name`, `requirements()`, `options`, `generators = "CMakeToolchain", "CMakeDeps"`) with no `package()` and no `package_info()`. | `conanfile.py` |
| P-5 | CPack placement | **CPack has no components.** `cmake/FixppPackaging.cmake` has no `cpack_add_component` and no `CPACK_COMPONENT*`. No install rule carries `COMPONENT`. The new files land in the one package per configuration, like everything else. | `cmake/FixppPackaging.cmake`; `bindings/python/pyproject.toml`'s `wheel.exclude` comment says the same |
| P-6 | "about 150 MB uncompressed" | **True of the gcc-release archives, and incomplete as a cost.** The slim headers add about as much again; MSVC release archives are about 4× the gcc size; Debug archives are GB-scale, which is what R-2a rules out. Numbers in §5. | §5 |
| P-7 | "lift the five exclusions" | **The rule has eight patterns.** The five are 078's (`messages`, `groups`, `validators`, `all.hpp`, `groups.hpp`). `_dispatch`, `vt11` and `Manifest.txt` stay. One pattern is added, for the `.cpp` translation units (D-7). Under R-2a the five are lifted in Release only. | the generated-header `install(DIRECTORY "${CMAKE_BINARY_DIR}/_codegen/include/" …)` rule in `CMakeLists.txt` |
| P-8 | — | **Two docs are already wrong about v42.** `specs/078-precompiled-builder-libs/contracts/include-layout.md` says "`v42` emits none (deferred to #196)". `docs/src/dictionary/codegen.md` says v42 is "descoped" and "emit[s] NO builders", tells readers to "construct v42 messages via the runtime `wire::body_builder`", and lists `<ver>` ∈ {v44, v50sp2, vlatest}. 082 (issue #196) added v42 to the emitter and to the library loop. Both are corrected in this PR (§6). | `cmake/Codegen.cmake`'s 082 T035 comment on the library loop |
| P-9 | — | **The Python wheel is unaffected.** `wheel.exclude = ["include/**", "lib/**", "share/fixpp/**"]` already drops both new file kinds. The wheel already builds the libs, because they are in `all`. | `bindings/python/pyproject.toml` |
| P-10 | — | **The packaging presets use exactly two build types, Release and Debug.** Every sanitizer, coverage and libc++ preset is Debug. 084 `contracts/package-layout.md` §1 ships six configurations: Linux clang and gcc, Windows MSVC, each Release and Debug. | Recipe: resolve `CMAKE_BUILD_TYPE` through `inherits` for every non-hidden `configurePresets` entry of `CMakePresets.json` (a short `json` walk), 2026-10-24, `4e8b3779` |

## 2. Design decisions

### D-0 — R-2a: the typed tier ships from single-config Release trees only

**The condition.** One configure-time variable, `FIXPP_INSTALL_TYPED_TIER`, is computed once in the root
`CMakeLists.txt`, immediately before the generated-header install rule. It is a plain variable, not an `option()`
or cache entry: R-2a is a ruling, not a knob.

```cmake
get_property(_fixpp_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
string(TOUPPER "${CMAKE_BUILD_TYPE}" _fixpp_build_type_uc)
if(NOT _fixpp_multi_config AND _fixpp_build_type_uc STREQUAL "RELEASE")
  set(FIXPP_INSTALL_TYPED_TIER ON)
else()
  set(FIXPP_INSTALL_TYPED_TIER OFF)
  message(STATUS "#613 R-2a: typed builder/validator tier NOT exported or installed "
                 "(CMAKE_BUILD_TYPE='${CMAKE_BUILD_TYPE}'; Release single-config trees only)")
endif()
```

It has exactly two readers: the generated-header install rule (D-7) and the D-1 append. Nothing under `tests/`
reads it; the tests derive their expectation independently (§4).

**Which build types count as Release.** `Release` only, compared case-insensitively (CMake treats configuration
names case-insensitively, and `cmake/FixppPackaging.cmake` lowercases the build type for the artifact name).
`RelWithDebInfo`, `MinSizeRel`, `Debug` and any custom type get no typed tier. No preset produces anything but
Release or Debug (P-10). RelWithDebInfo is excluded on the ruling's own reason: it carries the debug information that
made the Debug archives GB-scale. B&L L-613-3 states it.

**Single-config generators (every preset, Ninja).** A Debug tree:
- does not append the eight libs to `FIXPP_EXPORT_TARGETS`, so `fixppTargets.cmake` and
  `fixppTargets-debug.cmake` never name `fixpp::builders::*` or `fixpp::validators::*`;
- keeps 078's five exclusions on the generated-header rule, so its include tree is today's;
- still builds the libs (they are in `all`, for the in-tree tests), and installs none of them.

**Rejected: `install(... CONFIGURATIONS Release)` on the libs and on the header rule.** A toy probe settled what that
generates (CMake 3.30.0, 2026-10-24, a two-library project in the session scratch directory, since deleted: library
`typed` installed `CONFIGURATIONS Release` into the same export set as an unconditional `core`):
- **Single-config Debug tree.** The config-independent `pTargets.cmake` still declares
  `add_library(p::typed STATIC IMPORTED)`; `pTargets-debug.cmake` gives it no location. A consumer sees
  `if(TARGET p::typed)` true, and linking it fails at generate time with "IMPORTED_LOCATION not set for imported
  target". The target is declared, not absent.
- **Multi-config tree (Ninja Multi-Config, Debug and Release installed into one prefix).** `p::typed` has
  `IMPORTED_CONFIGURATIONS=RELEASE` only. A Debug consumer configured, linked `libtyped.a` (the Release archive) and
  ran with rc 0. That is the silent Release-into-Debug link the ruling must not allow. The shared `include/` would
  also hold the Release slim headers in the Debug view of the prefix.

So "present in Release, absent in Debug" cannot be expressed inside one multi-config prefix at all: one
`fixppTargets.cmake` and one `include/` serve every configuration.

**Multi-config generators.** `FIXPP_INSTALL_TYPED_TIER` is OFF for every configuration, whatever
`CMAKE_BUILD_TYPE` holds. The prefix is coherent, with no typed tier in any configuration. Such a tree can only
reach packaging if it also sets `CMAKE_BUILD_TYPE` (FR-017's `CMAKE_BUILD_TYPE is empty` FATAL otherwise). It is
outside Article III's Ninja-only boundary either way, and its artifact would fail `packaging::contents` loudly,
because that witness keys its expectation off the artifact's name (§4.2). No `--config` coherence probe is added.

**Coherence in both branches.** Release ships the declarations and exports the definitions. Debug ships neither.
078's coherence rule holds in each.

### D-1 — Export membership

Inside `if(FIXPP_INSTALL_TYPED_TIER)`, the libs are appended to `FIXPP_EXPORT_TARGETS` in the export block, after
the umbrella and beside the `fixpp_log_otlp` append. They follow the same fail-closed discipline:

```cmake
if(FIXPP_INSTALL_TYPED_TIER)
  foreach(_fixpp_typed_ver IN ITEMS v42 v44 v50sp2)
    list(APPEND FIXPP_EXPORT_TARGETS fixpp_builders_${_fixpp_typed_ver} fixpp_validators_${_fixpp_typed_ver})
  endforeach()
  if(FIXPP_CODEGEN_FIX_LATEST)
    list(APPEND FIXPP_EXPORT_TARGETS fixpp_builders_vlatest fixpp_validators_vlatest)
  endif()
endif()
```

- v42, v44 and v50sp2 are **unconditional within Release**. If one is missing, the existing membership check reports
  it as a defect instead of skipping it.
- vlatest is gated on **the option that decides whether it is built (P-2)**, never on `if(TARGET …)`. With the option
  ON and the target missing, the check still fails closed.
- The append sits **inside** `if(NOT FIXPP_CODEGEN_BOOTSTRAP_RUNNING)`, like the rest of the export block.
- **What follows from the list, with no new code:** the T026 export-name manifest (`fixpp-export-names.txt`), the
  `fixpp-package` target inputs, the derived `find_dependency` set, and `cmake/FixppPackaging.cmake`'s MSVC PDB loop
  all iterate `FIXPP_EXPORT_TARGETS`. In Release they gain the eight; in Debug they are unchanged from today.

**Measured (probe, 2026-10-24, `4e8b3779` plus this edit in a scratch copy, configure only, `CMAKE_BUILD_TYPE=Release`):**
- `fixpp-export-names.txt` gains the eight nested names when `FIXPP_CODEGEN_FIX_LATEST` is ON, and the six
  non-vlatest names when it is OFF.
- The generated `CMakeFiles/Export/*/fixppTargets.cmake` declares each lib as `STATIC IMPORTED`, with
  `INTERFACE_LINK_LIBRARIES "fixpp::wire"` and `INTERFACE_INCLUDE_DIRECTORIES "${_IMPORT_PREFIX}/include"`.
  ⚠️ v0.2 did not record the probe's edit set. The include-directory reading is impossible without D-5, because the
  library loop's include directory is `$<BUILD_INTERFACE:>` only today; so the probe must have carried D-5. The
  implementation re-takes this reading with D-1, D-2 and D-5 applied and says so, and the consumer property check
  (§4.1, M-9) replaces it as the standing evidence.
- The derived `find_dependency` set is unchanged: Crc32c, OpenSSL, asio, opentelemetry-cpp, pugixml, tomlplusplus.
- The implementation adds the Debug side of this probe: a configure-only Debug tree whose `fixpp-export-names.txt`
  and generated `fixppTargets*.cmake` contain no `fixpp::builders::` or `fixpp::validators::` name.

### D-2 — `EXPORT_NAME`s

The names are `builders::<ver>` and `validators::<ver>`, so the imported names are `fixpp::builders::<ver>` and
`fixpp::validators::<ver>`. These are the in-tree aliases, the names `docs/src/dictionary/codegen.md` already tells
consumers to link, and what T026's alias check requires (P-3).

They are set by one loop over `FIXPP_EXPORT_TARGETS`, placed after the prefix-strip sweep and the `dict::dispatch`
override. The loop matches `^fixpp_(builders|validators)_(.+)$`, so a version added later is named by the same rule
and is never left out of a hand-written list. In a Debug tree the loop matches nothing.

### D-3 — The umbrella does not reach them; consumers link by name

`fixpp::fixpp` stays `INTERFACE fixpp_session` plus its include root. The eight libs join the existing
**by-name class**: today that is `fixpp::capi`, `fixpp::config_toml`, `fixpp::tap` and `fixpp::service`, in the
umbrella comment's own terms. Reasons:

- 078 FR-004 makes the consumer's opt-in "purely link-time — link builders, validators, both, or neither", and
  `codegen.md` documents exactly that link line. Folding the libs into the umbrella would remove the choice for
  installed consumers only, and leave the two consumer kinds with different contracts.
- `fixpp_session` links none of them. The umbrella comment defines the umbrella as the session closure, so reaching
  the libs would change what the umbrella means, not just what it contains.
- The headers are still reachable through the umbrella's include root in a Release install: both install rules write
  to `${CMAKE_INSTALL_INCLUDEDIR}`. An umbrella-only consumer who calls `build_<Msg>` therefore gets an undefined
  symbol at link time, naming the function. That is the same behaviour an in-tree consumer gets today when it forgets
  the lib. B-613-1 states it (§8).
- R-2a makes the umbrella's meaning independent of the build type: it is the same target in Release and Debug, and
  only the by-name class differs.

### D-4 — Link interface and closure

Both libs link `PUBLIC fixpp::wire` and nothing else (`cmake/Codegen.cmake`). `fixpp_wire` is already a member, so
the closure is satisfied without new members. The probe confirmed CMake's generate-time closure check passes.

What the shipped slim headers include was measured on a configured tree, over **every** `#include` line, angle and
quoted. The recipe prints each include that is not a standard header, not `<fixpp/core/…>` or `<fixpp/wire/…>`, and
not inside the version's own tree (a sibling, `"../groups.hpp"`, or `"[../](groups|messages|validators)/…"`). Run it in
`<build>/_codegen/include/fixpp`:

```
for v in v42 v44 v50sp2 vlatest; do
  find $v/messages $v/groups $v/validators $v/all.hpp $v/groups.hpp -type f ! -name '*.cpp' -print0 \
    | xargs -0 grep -h -E '^[[:space:]]*#[[:space:]]*include'
done | sed -E 's/^[[:space:]]*#[[:space:]]*include[[:space:]]*//' \
  | grep -v -E '^<[a-z_]+>$|^<fixpp/(core|wire)/|^"((\.\./)?(groups|messages|validators)/)?[A-Za-z0-9_]+(\.builder|\.validator)?\.(hpp|inl)"$|^"\.\./groups\.hpp"$' \
  | sort | uniq -c
```

- **Result, 2026-10-24,** on `fixpp-parallel`'s `build/linux-gcc-release`, generated tree regenerated at `4e8b3779`:
  no output. Every include outside the version tree names a hand-written public header under `include/fixpp/core/`
  or `include/fixpp/wire/`, which already ship, so nothing reaches `_dispatch/` or `vt11/`, and 084's B2 holds.
- **Known positive, same date:** on a scratch copy of `v42` with `#include "../../_dispatch/x.hpp"` and
  `#include <fixpp/dict/x.hpp>` appended to `groups.hpp`, the recipe printed both lines. The scratch copy is deleted.

### D-5 — Include interface

Each lib gains `"$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"` beside its existing `$<BUILD_INTERFACE:>`, so a
by-name consumer's include root is declared by the target it links. This is 084's B0 ("every member must have an
include interface at all"), and B2 is satisfied because the path is the filtered installed include directory. The
entry is unconditional in `cmake/Codegen.cmake`: it is read only when the target is exported, so in a Debug tree it
is inert.

**It is observable, and it is witnessed.** `fixpp::wire`'s install interface is the same directory, so a compile
cannot tell whether this entry is present: the header resolves through the `PUBLIC fixpp::wire` edge either way. The
imported target's **own** `INTERFACE_INCLUDE_DIRECTORIES`, read with `get_target_property` after `find_package`, does
not depend on that edge. C-1's configure asserts it for each typed target it links (§4.1), and M-9 deletes the entry
from one lib to prove the assertion RED while the compile would still pass.

### D-6 — `FIXPP_CODEGEN_FIX_LATEST=OFF`: no vlatest, and no package

The owner ruled that the v1.0 packages carry all four versions. A tree configured with `FIXPP_CODEGEN_FIX_LATEST=OFF`
therefore **defines no packaging**, whatever its build type. Right after `include(cmake/Codegen.cmake)`, the root
`CMakeLists.txt` sets `FIXPP_PACKAGING_ENABLED` OFF, with a STATUS line naming the option. This follows the OTel-OFF
precedent in the same file ("defines NO packaging targets and NO CPack configuration, so it CANNOT produce an
artifact").

**Made loud by construction.** `cmake/FixppPackaging.cmake` also gains a `FATAL_ERROR` on
`NOT FIXPP_CODEGEN_FIX_LATEST`, in the style of its FR-011a guard for `FIXPP_BUILD_CODEGEN_TOOL`. The module is
included only when packaging is enabled, so the guard is latent while the root decision holds:
- deleting the root decision makes an OFF configure FATAL, which is loud rather than a silently packaged OFF tree;
- deleting the module guard leaves the root decision in force;
- the developer OFF configure that `codegen.md` documents still works.

No CI lane configures OFF (no preset or workflow sets the option), so these two deletion arms are recorded once
(M-8a, M-8b), and the contents witness's expected version list is **unconditional** (all four, as R-2 rules for
every package), so it cannot track the defect it should catch (§4.2).

A FATAL_ERROR at the root is rejected. `FIXPP_PACKAGING_ENABLED` is ON in every OTel-ON configure, so a root FATAL
would break the documented developer configure.

The export and the install rules stay live in that tree, so `cmake --install` of a FIX_LATEST=OFF tree still
produces a coherent prefix: no vlatest headers, because the directory is removed, and no vlatest targets.

**Measured (probe, as in D-1, with `-DFIXPP_CODEGEN_FIX_LATEST=OFF`, Release):** the STATUS line is printed, no
`CPackConfig.cmake` is generated, and the export declares 24 imported targets instead of 26.

### D-7 — Header surface and the installed-file-set rule

In a Release tree the generated-header install rule loses the five 078 patterns. In every tree it gains
**`PATTERN "*.cpp" EXCLUDE`**. One `install(DIRECTORY)` rule serves both branches: its non-Release branch keeps the
five 078 patterns.

- A translation unit is never a header, so the TUs stay out by construction, whatever directory they are emitted to.
  This replaces 078's "would leak as public headers" protection.
- No other `.cpp` exists under `_codegen/include/`. The dispatch bridge's `.cpp` is generated one level up, under
  `_codegen/`.

**The `.inl` files ship (Release).** Each installed `messages/<Msg>.hpp` includes its `<Msg>.builder.inl` /
`<Msg>.validator.inl` under `FIXPP_BUILDERS_HEADER_ONLY[_<Msg>]` / `FIXPP_VALIDATORS_HEADER_ONLY[_<Msg>]`. That macro
surface is a documented consumer contract (078 `contracts/include-layout.md` "Macro surface"; `codegen.md` "Per-message
header-only inline mode"). Withholding the `.inl` files would make an installed header name a file the package does
not ship.

**The rule.** From `${CMAKE_BINARY_DIR}/_codegen/include/` to `${CMAKE_INSTALL_INCLUDEDIR}`, every file ships except
the following:
- anything under `fixpp/_dispatch/`;
- anything under `fixpp/vt11/`;
- `Manifest.txt`;
- `*.cpp`;
- **in a non-Release tree only:** anything under `fixpp/<ver>/messages/`, `fixpp/<ver>/groups/` or
  `fixpp/<ver>/validators/`, and `fixpp/<ver>/all.hpp` and `fixpp/<ver>/groups.hpp`.

The shipped kinds per `<ver>` in {v42, v44, v50sp2}, plus vlatest when `FIXPP_CODEGEN_FIX_LATEST=ON`:

| Kind | Release | Debug |
|---|---|---|
| the five read-side files (`Fields.hpp`, `Messages.hpp`, `NormativeReferences.md`, `Reify.hpp`, `Validator.hpp`) | ✅ | ✅ |
| `all.hpp`, `groups.hpp` | ✅ | ❌ |
| `groups/*.hpp` | ✅ | ❌ |
| `validators/traits.hpp` | ✅ | ❌ |
| `messages/<Msg>.hpp`, `messages/<Msg>.builder.inl`, `messages/<Msg>.validator.inl` | ✅ | ❌ |

Re-derive the exact Release file set from any configured tree `B`:

```
cd "$B/_codegen/include" && find fixpp -type f ! -path 'fixpp/_dispatch/*' ! -path 'fixpp/vt11/*' \
  ! -name Manifest.txt ! -name '*.cpp' | sort
```

**Measured equal to what the planned Release rule installs** (probe, 2026-10-24, `4e8b3779` plus this edit, Release).
This used a partial `cmake --install`: the header rules run before `install(TARGETS)`, which then fails on the unbuilt
archives. Its generated tree under `include/fixpp/v*` was `diff`ed against the recipe's output with no difference.
This is a one-time observation; the contents witness's build-tree equality (§4.2) is the standing gate.

**The rule is a denylist, so a new emitted kind ships by default.** What catches one is the contents witness's
**schema allowlist** (§4.2), whose expectation is written from the 078 file-set contract, not from this rule.

### D-8 — CPack, Conan, wheel

- **CPack:** no change (P-5). `fixpp-package` depends on every non-INTERFACE member of `FIXPP_EXPORT_TARGETS`, so
  it picks the libs up from the list in Release, and not in Debug.
- **Conan:** no change. This repository defines no Conan package (P-4).
- **Wheel:** no change (P-9).

### D-9 — MSVC / Windows

- **Static only.** There is no shared builder lib, so no `WINDOWS_EXPORT_ALL_SYMBOLS` question arises. The
  contents witness's archive pattern `^lib/fixpp_[A-Za-z0-9_]+\.lib$` already matches `fixpp_builders_v44.lib`.
- **Debug vs Release (R-2a).** Each tree is single-config and ships one `fixppTargets-<config>.cmake`; the consumer
  must match the configuration, as for every existing member. Release exports the eight libs; Debug exports none
  and installs none of their files (D-0).
- **PDBs.** `cmake/FixppPackaging.cmake`'s `if(MSVC)` loop sets `COMPILE_PDB_NAME` / `COMPILE_PDB_OUTPUT_DIRECTORY`
  on every STATIC/OBJECT member of `FIXPP_EXPORT_TARGETS`, and its `install(DIRECTORY lib/ FILES_MATCHING PATTERN
  "*.pdb")` ships every PDB in `lib/`.
  - Release: the libs are members, but Release compiles without `/Zi`, so no PDB is written. The contents witness
    already FATALs on a Release ZIP carrying `lib/*.pdb` (its SC-005/T061 Release arm).
  - Debug: the libs are not members, so their compiler PDBs stay under their own object directories and never reach
    `lib/`. That glob is still the one leak path, so the Debug absence check (§4.1) includes `.pdb` files by name.
- **`/W4`.** No new compile of fixpp code: the libs are already built on every MSVC lane. Their own
  `.builder.cpp` / `.validator.cpp` TUs include the shipped headers and compile in-tree under
  `fixpp_apply_common_flags` (`cmake/Helpers.cmake`), which gives MSVC `/W4` (and `/WX` under `FIXPP_WERROR`) and
  GCC/Clang `-Wall -Wextra -Wpedantic`. That is the evidence that the shipped headers are warning-clean. The
  standalone consumer gets only the Conan toolchain, not the producer's flags, so C-1 is **not** a `/W4` witness.
- **Path length.** The longest shipped generated path is 76 characters below `include/` (measured on
  `4e8b3779`'s generated tree with the recipe in D-7, piped through `awk '{print length($0)}' | sort -n | tail -1`).
  Under the sandbox's ZIP staging prefix
  `C:\temp\fixpp\build\windows-msvc-release\_packages\_CPack_Packages\win64\ZIP\fixpp-<version>-windows-msvc-release\include\`
  that is 194 characters. MSVC already writes 178-character object paths for the same files under
  `CMakeFiles\fixpp_validators_vlatest.dir\_codegen\include\`, and 260 is the limit. The windows-msvc-release
  packaging lane exercises the real staging path (§4.4). The Debug ZIP stages none of these paths. A consumer who
  extracts the Release ZIP under a root longer than about 180 characters would hit `MAX_PATH`. That is the general
  Windows constraint, and it is not recorded as a fixpp limitation.

### D-10 — withdrawn by R-2a: the consumer witness keeps its stage

v0.2 deleted `${FIXPP_WITNESS_WORK_DIR}` after a PASS, because every Debug, sanitizer and coverage lane would have
copied 5–11 GB of builder archives into it. Under R-2a those lanes install no typed tier, so their stage is
unchanged from today. The Release stages grow by the Release archives and slim headers (§5), hundreds of MB, not GB.

The deletion is therefore dropped. That also removes the defect the triage found in it (N-1): 086 `plan.md`'s
verifier step runs `clang-tidy -p build/linux-clang-release/_consumer_witness/build tests/consumer/probe_*.cpp`
against the compile database that 086 T053 added to the sub-build, and that procedure must keep working for this
PR's new TUs. v0.2's "nothing else reads the stage" came from a grep restricted to build-file globs, which could not
see a reader in a `plan.md`. The unfiltered recipe, `git grep -n _consumer_witness`, finds it (run 2026-10-24 on
`97748d1e`: its hits include `specs/086-capi-include-isolation/plan.md`'s `clang-tidy -p` line).

The `tier1.yml` `#254` job-end disk comment is therefore **not** amended. The in-test disk instrument of §4.4 is kept.

## 3. What becomes reachable, and from where

| An installed **Release** consumer… | Links | Includes |
|---|---|---|
| builds typed messages of `<ver>` | `fixpp::builders::<ver>` | `<fixpp/<ver>/messages/<Msg>.hpp>` or `<fixpp/<ver>/all.hpp>` |
| validates typed `Args` of `<ver>` | `fixpp::validators::<ver>` | the same headers |
| inlines chosen builders/validators | the remainder's lib, or none | the same, with `FIXPP_{BUILDERS,VALIDATORS}_HEADER_ONLY[_<Msg>]` defined. The ODR rule of 078 `include-layout.md` applies unchanged: never inline a message in one TU and link it in another. |

An installed **Debug** consumer has none of these rows: no typed target, and no slim header to inline from
(B&L L-613-3).

## 4. Test plan

No new `add_test`. Both changes extend registered tests, so the CI count pins are unchanged: `-L consumer -N` in
`tier1.yml`, `tier2.yml` and `tier3-libcxx.yml`, and `-L packaging -N` in `tier1.yml` and `tier2.yml`. A new
registration would have to bump them.

**The expectation is independent of the mechanism.** Every two-sided check below derives "typed tier expected" from
the build type itself, not from `FIXPP_INSTALL_TYPED_TIER`:
- the install witness's driver from `FIXPP_BUILD_TYPE` (already passed by the `add_test`; case-insensitive `Release`
  means present, anything else, and the driver's own empty-means-Debug default, means absent);
- the contents witness from the configuration component of the artifact's own name (§4.2), the method its FR-019
  PDB block already uses.

So a mutant that flips `FIXPP_INSTALL_TYPED_TIER` either way meets an oracle that did not move (M-10, M-13).

### 4.1 `fixpp::consumer::install-witness` (tests/consumer) — every lane, two-sided

The driver computes the expectation, prints one line naming it (`#613 typed tier: expected PRESENT|ABSENT
(FIXPP_BUILD_TYPE=<t>)`), and passes `-DFIXPP_TYPED_EXPECTED=ON|OFF` to the consumer configure. **Both branches add
required targets** to the driver's `_required_targets`, so neither branch can pass by registering nothing, and
deleting a target fails the build by name (the 086 Gate B r1 rule recorded in `run_consumer_witness.cmake`).

**Stage check, both branches (driver, after the install).** For each version directory present under
`include/fixpp/`, the set of immediate children (first path component, deduplicated) must equal exactly:
- ABSENT: the five read-side files;
- PRESENT: the five read-side files plus `all.hpp`, `groups.hpp`, `messages`, `groups` and `validators`.

In the ABSENT branch, the driver also FATALs on any file under the stage's `lib/` whose name contains
`fixpp_builders_` or `fixpp_validators_` (archives and `.pdb` alike), and on any staged `lib/cmake/fixpp/fixppTargets*.cmake`
containing `fixpp::builders::` or `fixpp::validators::`.

#### PRESENT branch (Release lanes)

**C-1 `consumer_typed_witness` (executable, link mode, RUN by the driver after `consumer_witness`):**
- **One message per version:**
  - `fixpp::v42::NewOrderSingle` and `fixpp::v44::NewOrderSingle` (the issue's message);
  - `fixpp::v50sp2::ListStatus` and `fixpp::vlatest::ListStatus`.
  - **Why not NewOrderSingle on v50sp2 and vlatest:** its default-mode include closure is 233 group-plan headers,
    about 0.88 GiB peak RSS per TU (B&L L-078-1). `ListStatus` has a closure of one plan, so it still exercises the
    installed `groups/` path. Re-derive any closure size by walking the message header's `#include "…"` lines,
    skipping the `.inl` lines.
- **For each version, three calls:**
  - `build_<Msg>` with fixed inputs. Assert the returned span **byte-for-byte equal** to a checked-in literal. The
    literal is written from two sources, read as text, never captured from a run:
    - the **tag order**: the emission order of that version's generated `<Msg>.builder.inl` (the emitter's order,
      not the dictionary's member order: v44 `NewOrderSingle` emits 347 before 1 and 11);
    - the **framing**: `wire::body_builder`'s output contract (`include/fixpp/wire/body_builder.hpp`'s design block:
      body only, `35=<msg_type>\x01` emitted at `commit()`, `<tag>=<value>\x01` per field, count before instances),
      as pinned by `tests/wire/test_body_builder.cpp`.
  - `validate_<Msg>` on the same inputs with every required field set. Assert success.
  - `validate_<Msg>` with one named required field cleared. Assert failure. The required set comes from the
    dictionary XML (`required="Y"`); for v44 NewOrderSingle it is {11, 40, 54, 60}.
  - The two validation outcomes are what prove a real `validate_` body ran, rather than a stub.
- **Links exactly the eight imported libs, by name.** No `if(TARGET …)`, and not `fixpp::fixpp`, so the by-name
  targets must carry their own closure.
- **Own include interface (D-5).** Before `add_executable`, the consumer configure reads each linked typed target's
  own `INTERFACE_INCLUDE_DIRECTORIES` and FATALs unless it is exactly the staged include root (both sides through
  `file(REAL_PATH)`).
- **vlatest is two-sided** on `FIXPP_CODEGEN_FIX_LATEST`, which `add_test` passes to the driver and the driver to the
  consumer configure.
  - ON: the consumer links the vlatest pair and compiles the vlatest cells.
  - OFF: the consumer FATALs if `fixpp::builders::vlatest` or `fixpp::validators::vlatest` is defined, and the
    driver FATALs if `include/fixpp/vlatest` exists in the stage.
- **The TU `#error`s if any `FIXPP_*_HEADER_ONLY*` macro reaches it.** Without this guard, inline mode would satisfy
  the link without the archive (the spurious-HIT path; see 4.3 M-6).

**C-2: two compile-only OBJECT targets, never linked.** They must be **separate targets**. The whole-TU macros carry
the same name in every version, and the generated guard is
`defined(FIXPP_VALIDATORS_HEADER_ONLY) || defined(FIXPP_VALIDATORS_HEADER_ONLY_<Msg>)`. In a shared TU they would
pull the large versions' `ListStatus.validator.inl`, then `validators/traits.hpp`, then the full v50sp2 and vlatest
`groups.hpp`, at about 1.5 GiB each (L-078-1).

- **C-2a `probe_typed_inline_small`** defines `FIXPP_BUILDERS_HEADER_ONLY` and `FIXPP_VALIDATORS_HEADER_ONLY`, then
  includes `<fixpp/v42/all.hpp>` and `<fixpp/v44/all.hpp>`. That reaches each small version's `all.hpp`,
  `groups.hpp` (through `validators/traits.hpp`), `validators/traits.hpp`, and every `.builder.inl` and
  `.validator.inl`.
- **C-2b `probe_typed_inline_large`** defines only `FIXPP_BUILDERS_HEADER_ONLY_ListStatus`, then includes the v50sp2
  and vlatest `messages/ListStatus.hpp`. That reaches a large version's `.builder.inl`. The builder `.inl` includes
  only its message header and `<fixpp/wire/body_builder.hpp>`, so no `groups.hpp` is pulled.
- Neither target is linked, so 078's same-message ODR rule cannot be violated.

#### ABSENT branch (Debug lanes)

**C-3 `probe_typed_absent` (compile-only OBJECT, linked to `fixpp::fixpp`, required).** It follows 086's ❌-cell
form (rule 2 in `tests/consumer/CMakeLists.txt`'s 086 block: `__has_include` plus a unique-token `#error`, asserted by
compiling):
- **Positive control first:** `#error` unless `__has_include(<fixpp/v44/Messages.hpp>)`. Without it, a missing
  include root would make every absence below true for the wrong reason.
- Then, for each of the four versions, `#error` naming the path if any of these resolves: `all.hpp`, `groups.hpp`,
  `validators/traits.hpp`, and one message header (`messages/NewOrderSingle.hpp` for v42/v44,
  `messages/ListStatus.hpp` for v50sp2/vlatest).

**Consumer configure, ABSENT branch.** It reads the directory's `IMPORTED_TARGETS` after `find_package(fixpp)`:
- positive control: FATAL unless `fixpp::fixpp` is in the list (a blind read reports no typed target);
- FATAL naming every entry matching `^fixpp::(builders|validators)::`. A regex rather than a list, so a version added
  later is caught too.

### 4.2 `fixpp::packaging::contents` (tests/packaging) — linux-gcc-release and windows-msvc-release

Changes to `tests/packaging/run_package_contents_witness.cmake`. **The typed-tier expectation is keyed off the
artifact's own name**, as the FR-019 PDB block is. The name is `CPACK_PACKAGE_FILE_NAME`'s
`fixpp-<version>-<platform>-<toolchain>-<config>`, optionally followed by `-otel-off-fixture`, then the extension
(`cmake/FixppPackaging.cmake`, T051). The witness **parses** the configuration component
(`^fixpp-[^-]+-[^-]+-[^-]+-([^-.]+)`) rather than matching a suffix, and FATALs if the name does not parse:
- component equal to `release` → PRESENT: the slim set is expected;
- any other component (`debug`, and equally `relwithdebinfo` or `minsizerel`, mirroring D-0) → ABSENT: today's
  read-side-only set is expected.

Below, "Release artifacts" and "Debug artifacts" mean these two outcomes.

- **Must-be-absent, every artifact.** Keep `_dispatch`, `vt11`, `core/test` and `transport/test`. Add
  `^include/.*\.cpp$`. Debug artifacts also keep the `messages|groups|validators` and `(all|groups)\.hpp`
  alternatives, and add `^lib/.*fixpp_(builders|validators)_`.
- **Per-version immediate children, derived the same way under every generator.** A version's children are the first
  path component under `include/fixpp/<ver>/` (`^include/fixpp/<ver>/([^/]+)(/|$)`, deduplicated). The listings are
  already normalised with `string(REGEX REPLACE "/$" "" …)`, so a listed directory entry and a file path below it
  give the same component, whether or not the generator lists directories. Expected set: as in §4.1's stage check,
  by artifact configuration.
- **Shipped versions: an unconditional expected list.** `_versions_seen` is derived from the package today, so a
  dropped version passes. The expected list becomes v42, v44, v50sp2 and vlatest, **unconditionally**: R-2 rules all
  four into every package, and D-6 makes a FIX_LATEST=OFF tree unpackageable. Keying it on `FIXPP_CODEGEN_FIX_LATEST`
  would track the very regression it should catch.
- **Release artifacts: two independent checks on `messages/`, `groups/` and `validators/`.**
  - **Schema allowlist** (catches a new emitted kind). Written from 078 `contracts/include-layout.md`'s generated file
    set, not from the install rule: `messages/` holds only `<Msg>.hpp`, `<Msg>.builder.inl` and
    `<Msg>.validator.inl`, and each `<Msg>.hpp` has both `.inl` siblings and vice versa; `groups/` holds only
    `[A-Za-z0-9_]+\.hpp`; `validators/` holds exactly `traits.hpp`. Any other file fails, by name.
  - **Build-tree equality** (install fidelity). The shipped files must equal the D-7 Release rule applied to
    `${FIXPP_MAIN_BUILD_DIR}/_codegen/include/fixpp/<ver>`. This is the 086 block's method for the isolated roots.
    It catches a dropped file and a leaked `.cpp`. It **cannot** catch a new emitted kind, because its expectation is
    the same denylist as the rule; the schema check exists for that.
- **Archive count.** Already derived from the shipped `fixppTargets.cmake`, so it follows the new membership in
  either configuration with no edit. Its comment ("13 of the 18") becomes a condition (§6).

`fixpp::packaging::export-names` and `fixpp::packaging::package-target` are derived from `FIXPP_EXPORT_TARGETS` and
need no edit. Their regexes already admit `::` inside a name. `clean-env` and `real-client` link only the umbrella
and gain nothing to check: the libs add no `find_dependency` (D-1).

⚠️ **No CI lane builds a Debug package** (`tier1.yml`'s `Build packages` comment: "No -Debug configuration uploads
packages"), so this witness's `-debug` arm runs only when someone runs the packaging tier on a Debug tree. **The only
standing Debug gate is §4.1's ABSENT branch**, on every Debug CI lane. The `-debug` arm is demonstrated once (M-14).

### 4.3 Mutants

Each mutant is applied on the implementation branch, in a scratch commit that is then dropped. After applying it:
1. Rebuild the tree, so `cmake_install.cmake` and the export files are regenerated. A stale install script fails
   toward clean.
2. Run `ctest --preset <p> -R 'fixpp::consumer::install-witness|fixpp::packaging::contents' --output-on-failure`.
3. Record the failing line in the verify record.

| # | Mutant | Lane | Expected RED | Where it shows |
|---|---|---|---|---|
| M-1 | Delete the D-1 append (no lib exported) | gcc-release | consumer configure: CMP0028 — `consumer_typed_witness` links `fixpp::builders::v42` "but the target was not found". The headers still ship, which is the incoherent package 078 feared. | install-witness ("consumer configure failed") |
| M-2a | Restore `PATTERN "messages" EXCLUDE` in the Release branch | gcc-release | C-1 compile: `fixpp/v44/messages/NewOrderSingle.hpp` not found. Stage children ≠ PRESENT set. Contents: children and schema/equality RED. | both |
| M-2b | Restore `PATTERN "groups" EXCLUDE` (Release) | gcc-release | C-1 compile: a `../groups/G_*Args.hpp` not found (v44 NewOrderSingle; v50sp2/vlatest ListStatus). Contents RED. | both |
| M-2c | Restore `PATTERN "validators" EXCLUDE` (Release) | gcc-release | C-2a compile: `../validators/traits.hpp` not found. Link-mode C-1 stays green, which is why C-2a exists. Contents RED. | both |
| M-2d | Restore `PATTERN "all.hpp" EXCLUDE` (Release) | gcc-release | C-2a compile: `fixpp/v42/all.hpp` not found. Stage children and contents RED. | both |
| M-2e | Restore `PATTERN "groups.hpp" EXCLUDE` (Release) | gcc-release | C-2a compile: `../groups.hpp` (from `traits.hpp`) not found. Contents RED. | both |
| M-2f | Add `PATTERN "*.inl" EXCLUDE` | gcc-release | C-2a and C-2b compile: `<Msg>.builder.inl` not found. Contents: schema (a `.hpp` without its `.inl`) and equality RED. | both |
| M-3 | Drop one version from the append: vlatest, then v42 | gcc-release | consumer configure: CMP0028 on that version's targets | install-witness |
| M-3b | Drop only `fixpp_validators_v44` | gcc-release | consumer configure: CMP0028 on `fixpp::validators::v44` | install-witness |
| M-4 | Remove `PATTERN "*.cpp" EXCLUDE` | gcc-release | Contents: must-be-absent lists `include/fixpp/<ver>/messages/*.builder.cpp`; schema RED too. The consumer stays green, correctly: a leaked TU breaks no compile. | contents |
| M-5 | Remove the D-2 loop | gcc-release | configure: T026 FATAL. Already observed by probe (P-3). | configure |
| M-6 | Spurious-HIT arm: with everything exported, delete the `NewOrderSingle.builder.cpp.o` member from the **staged** `libfixpp_builders_v44.a`. Procedure (GNU `ar`, so Linux): run the witness once to PASS (the stage persists, D-10); `ar d <work>/stage/lib/libfixpp_builders_v44.a NewOrderSingle.builder.cpp.o`; then `cmake --build <work>/_sub_build --target consumer_typed_witness` by hand | gcc-release | C-1 link: undefined `fixpp::v44::build_NewOrderSingle`. Proves the symbol resolves from that archive and from nothing else in the closure. | the hand build |
| M-7 | Change one byte of C-1's v44 expected literal | any Release | C-1 run: no `PASS` line. Proves the byte comparison is live. | install-witness |
| M-8a | FIX_LATEST=OFF, root D-6 decision deleted | configure only | the `cmake/FixppPackaging.cmake` guard FATALs naming `FIXPP_CODEGEN_FIX_LATEST` | configure, recorded once |
| M-8b | FIX_LATEST=OFF, module guard deleted | configure only | still no `CPackConfig.cmake` and the D-6 STATUS line (the root decision holds) | configure, recorded once |
| M-8c | FIX_LATEST=OFF, unmutated | one full OFF build | D-6 configure evidence (STATUS line, no `CPackConfig.cmake`, 24 imported names, already observed by probe) plus the consumer witness's vlatest-OFF arm green | recorded once; no CI lane configures OFF |
| M-9 | Remove the `INSTALL_INTERFACE` entry from `fixpp_validators_v50sp2` only | gcc-release | consumer configure: the own-include-interface check FATALs naming `fixpp::validators::v50sp2`, although its headers would still resolve through `fixpp::wire` | install-witness |
| M-10 | Force `FIXPP_INSTALL_TYPED_TIER` ON regardless of build type | a Debug tree (linux-clang-debug) | driver: stage children ≠ ABSENT set and typed archives in `lib/`; consumer: `IMPORTED_TARGETS` lists `fixpp::builders::v42` …; C-3 `#error` on `fixpp/v42/all.hpp`. Pays the multi-GB Debug stage once. | install-witness |
| M-11 | In the non-Release branch, drop only the restored `PATTERN "groups.hpp" EXCLUDE` | a Debug tree | driver: `groups.hpp` among a version's children; C-3 `#error` on `fixpp/<ver>/groups.hpp`. | install-witness |
| M-12 | Append `fixpp_validators_v44` to `FIXPP_EXPORT_TARGETS` outside the `if(FIXPP_INSTALL_TYPED_TIER)` | a Debug tree | consumer: `IMPORTED_TARGETS` lists `fixpp::validators::v44`; driver: `lib/libfixpp_validators_v44.a` (or `.lib`) staged. T026 stays green (the D-2 loop names it), which is why the consumer check exists. | install-witness |
| M-13 | Force `FIXPP_INSTALL_TYPED_TIER` OFF regardless of build type | gcc-release | consumer configure: CMP0028 on `fixpp::builders::v42`, because the driver's expectation comes from `FIXPP_BUILD_TYPE`, not from the variable | install-witness |
| M-14 | Debug package contents: run `packaging::contents` once unmutated, then with M-11 applied | a Debug tree with packaging enabled | unmutated GREEN on the `-debug` arm; with M-11, children set and must-be-absent RED naming `include/fixpp/<ver>/groups.hpp` | contents, recorded once |
| M-15 | New emitted kind: after the build and before `ctest`, create `_codegen/include/fixpp/v44/messages/X.json` in the build tree | gcc-release | contents: schema allowlist RED naming `include/fixpp/v44/messages/X.json`; build-tree equality stays green, correctly | contents |

### 4.4 Lanes and first-run obligations

**Which lanes take which branch.** Re-derive from the presets and the three workflow matrices: a lane is PRESENT iff
its configure preset resolves `CMAKE_BUILD_TYPE` to `Release` (P-10's recipe), and runs the consumer witness iff its
workflow runs `ctest -L consumer` or an unfiltered `ctest`. Read on `4e8b3779`:
- **PRESENT:** linux-clang-release and linux-gcc-release (tier1), windows-msvc-release (tier2).
- **ABSENT:** linux-clang-debug, linux-clang-asan, linux-clang-ubsan, linux-clang-tsan and linux-clang-coverage
  (tier1), windows-msvc-debug and windows-msvc-asan (tier2), the four libc++ legs (tier3).
- ⚠️ **Disclosed narrowing.** C-1 and C-2 compile and run on gcc, clang and MSVC Release only. The installed typed
  tier is never exercised under a sanitizer, coverage or libc++. The libs themselves still are, in-tree, on every
  lane.
- **`fixpp::packaging::contents`** (label `packaging`) runs on linux-gcc-release (tier1) and windows-msvc-release
  (tier2): Release artifacts only (§4.2).

**Tests that pay the install copy.** Derived with `git grep -n -- '--install' -- tests bindings` (2026-10-24,
`4e8b3779`): `tests/consumer/run_consumer_witness.cmake` (every lane), `bindings/python/run_python_install_witness.cmake`
(registered `if(FIXPP_BUILD_TESTS AND UNIX AND NOT DEFINED SKBUILD)`, `TIMEOUT 300`, on the Linux legs that build
the bindings), and the `tests/packaging` witnesses (their cpack staging, on the two packaging lanes). Under R-2a only
the PRESENT lanes copy anything new. On the Linux Release legs the consumer and Python stages can coexist under
`ctest` parallelism, so their growth adds.

**In-test disk instrument (driver).** Before deleting nothing and before configuring the consumer, the driver prints
one line: the stage's total bytes (summed with `file(GLOB_RECURSE)` + `file(SIZE)`, so it works on every platform; a
zero sum is a FATAL, because the glob then saw nothing), and the free bytes of the work directory before and after
the install where `df` is found (`df -B1 --output=avail`); elsewhere it prints `free: unavailable`. It reports; it
gates nothing.

**Implementation obligations, read from the PR's first CI run** (copying a remembered number is not allowed):
- **Disk.** Read the driver's line on every lane, coverage included. ABSENT lanes: stage bytes should match a run of
  `main` taken the same way. PRESENT lanes: the growth should match §5's projection; record it.
- **Elapsed time.** Read the elapsed time of `install-witness` on every PRESENT lane, of the Python install witness on
  the two Linux Release legs (both `TIMEOUT 300`), and of each `tests/packaging` witness against its own `TIMEOUT`.
  If any uses more than half its budget, raise its `TIMEOUT` to twice the largest measured value.
- **Compile cost of C-1, C-2a, C-2b.** Wall time from the sub-build's `.ninja_log` on each PRESENT lane. Peak RSS
  once, locally on linux-gcc-release: with the stage kept (D-10), clean and rebuild each target under
  `/usr/bin/time -v cmake --build <work>/_sub_build --target <t>`, and record `Maximum resident set size`.
- **Configure probe, both build types** (D-1's last bullet), recorded in the verify record.

## 5. Size: method, baseline, after

**Method.** The package is what CI ships: `packages-linux-gcc-release` and `packages-linux-clang-release` (tier1) and
`packages-windows-msvc-release` (tier2). Fetch each with `gh run download <run> -n <artifact> -D <empty dir>`. Record
per file: `stat -c %s`, then the unpacked `du -sb` split into `include/`, `lib/` and `share/`.
- **Before:** `main`'s artifacts.
- **After:** the same artifacts from the implementation PR's run, measured the same way. The PR records both.

Archive sums below were read with
`stat -c '%s %y' <tree>/lib/libfixpp_builders_*.a <tree>/lib/libfixpp_validators_*.a | awk '{s+=$1; if($2>m)m=$2} END{print s, m}'`
(the second field is the newest archive's date).

**Baseline.** Measured 2026-10-24 from the push:main runs of `45a620f4`. The only commits between `45a620f4` and
`4e8b3779` are two documentation commits that install nothing (`git diff --stat 45a620f4 4e8b3779`).

| Artifact | Bytes | Unpacked | of which `include/` | `lib/` |
|---|---|---|---|---|
| `fixpp-0.0.1-linux-gnu-release.tar.gz` (tier1 run 39983316190) | 19,421,915 | 211,861,983 | 166,356,961 | 34,832,618 |
| `fixpp-0.0.1-linux-gnu-release.deb` | 19,421,478 | — | — | — |
| `fixpp-0.0.1-linux-gnu-release.rpm` | 18,467,978 | — | — | — |
| `fixpp-0.0.1-windows-msvc-release.zip` (tier2 run 39983316200) | 21,166,138 | 296,060,348 | 166,385,774 | 118,951,897 |
| `packages-linux-clang-release` | not taken for v0.3; read by the method above at implementation | | | |

Of the Linux `include/`, the read-side `fixpp/v50sp2` and `fixpp/vlatest` trees are already 74,946,457 and 80,999,073
bytes.

**Cost per shipped configuration (084 `package-layout.md` §1), after R-2a.**

| Configuration | Typed tier | Added archives (raw) | Added slim headers (raw) | Source / label |
|---|---|---|---|---|
| Linux gcc Release | shipped | 153,453,112 (gzip `tar czf` 27,009,241) | 157,781,938 (gzip 14,012,098) | archives: `fixpp-parallel` `build/linux-gcc-release/lib`, built 2026-10-20 at `99d499df`; headers: the same tree's `_codegen/include`, regenerated at `4e8b3779` (v50sp2 75,990,603, vlatest 78,135,363) |
| Linux clang Release | shipped | 95,381,890 | the generated tree is toolchain-independent; read from the artifact | **estimate**: main checkout `build/linux-clang-release/lib`, newest archive 2026-10-23, commit unrecorded |
| Windows MSVC Release | shipped | 611,122,156 (`gzip -6` 84,704,020); no PDBs (Release has no `/Zi`) | read from the artifact | **estimate**: the shared sandbox's `windows-msvc-release/lib`, built 2026-10-08/14 at unrecorded commits |
| Linux clang Debug | **not shipped (R-2a)** | 0 | 0 | what R-2a avoided: 4,861,863,620 summed, largest single archive (`libfixpp_validators_vlatest.a`) 1,355,449,906 — **estimate**: main checkout `build/linux-clang-debug/lib`, newest archive 2026-10-23, commit unrecorded |
| Linux gcc Debug | **not shipped** | 0 | 0 | what R-2a avoided: unmeasured (no local gcc-debug tree holds builder archives) |
| Windows MSVC Debug | **not shipped** | 0 | 0 | what R-2a avoided: about 2.28 GB of `.lib`, PDBs excluded — **estimate**, sandbox tree, commit unrecorded |

About the gcc archives: `git diff 99d499df 4e8b3779` over the builder inputs (`tools/codegen`, `dictionaries`,
`include/fixpp/{core,wire}`, `cmake/`, `CMakeLists.txt`, presets, Conan profiles) changes only three things:
SPDX comment lines in core headers, `cmake/Helpers.cmake`'s fuzz-only instrumentation, and two lines of
`cmake/FixppPackaging.cmake`. None of these reaches a builder object in a non-fuzz build.

Expected order of growth (projected, constructed rather than cpack'd; the PR's CI artifacts are the real "after"):

| Package | Before (compressed) | After, projected | Unpacked growth |
|---|---|---|---|
| Linux gcc TGZ | 19.4 MB | about 60 MB | +0.31 GB |
| Linux clang TGZ | not taken | — | about +0.25 GB (estimate) |
| Windows release ZIP | 21.2 MB | about 120 MB | +0.77 GB (estimate) |
| every Debug package | unchanged | unchanged | 0 |

**Stage costs paid by tests (§4.4).** On PRESENT lanes each stage-installing test grows by that configuration's row
above. On ABSENT lanes, which are every Debug, sanitizer, coverage and libc++ lane, the stage does not grow. v0.2's
5–11 GB per-lane stage table no longer describes any lane and is withdrawn.

Large-payload behaviour of the DEB, RPM and ZIP generators is exercised only on the Release payloads above, which the
packaging lanes build on every run.

## 6. Amendments, by name, in the same PR

Rule: a superseded decision gets a **header banner that names `.specify/613-builders-export.md`**. The text under
the banner is kept as history. A count is replaced by its condition, never by a new count (PR #325). Every edited
comment is checked with `.claude/scripts/check-comment-claims.py --root <tree> --base origin/main`. The PR also runs
`tools/check_line_citations.py --shift-audit origin/main..HEAD` (`brain-freshness.yml` gates on it): banners are
inserted lines, and the cross-bundle citer of `package-layout.md` (`specs/086…/spec.md`) is archival under the
tool's frozen-citer rule, so the run is expected to pass; any hit is repaired, not waived.

Every banner and comment says **Release** where it says "installed": R-2a is part of the fact.

### 6.1 Named by #613 item 5

**(a) `specs/078-precompiled-builder-libs/contracts/cmake-targets.md`, section "Install / export scope — DECIDED:
build-tree + in-tree only (Gate A round 1, R3)".** Insert directly under the heading:

> **⚠️ SUPERSEDED by fixpp#613 (`.specify/613-builders-export.md`).** The blocker named below, "it would open the
> tier's first export surface", was removed by 084, which built that surface. Since #613, in **Release**
> configurations, `fixpp_builders_<ver>` and `fixpp_validators_<ver>` are members of `FIXPP_EXPORT_TARGETS` for v42,
> v44 and v50sp2, and for vlatest when `FIXPP_CODEGEN_FIX_LATEST=ON`. They are exported as `fixpp::builders::<ver>` /
> `fixpp::validators::<ver>` and linked by name; the `fixpp::fixpp` umbrella does not reach them. The slim typed
> layout is installed, except its `.cpp` translation units. **Debug and every other non-Release configuration export
> and install neither** (owner ruling R-2a, 2026-10-24: "Release packages only."). The **coherence rule still holds
> in both**: Release installs every `build_`/`validate_` declaration with an exported definition, and non-Release
> installs neither. The rest of this section is the Gate A round 1 record. Its version list predates 082, which added
> v42.
>
> **`Args` freeze (owner ruling, 2026-10-24):** "FREEZE span-based Args as the v1.0 public shape (Option 3 stays
> deferred; a later change would be a 2.0 break)." Installing the layout makes `<Msg>Args` and `groups::G_*Args`
> public API in their span-based shape; L-078-1's compile cost is part of that shape (B&L L-613-2).

**(b) `CMakeLists.txt`, the comment above the generated-header `install(DIRECTORY "${CMAKE_BINARY_DIR}/_codegen/include/" …)`
rule.** Replace the paragraph that begins "078-precompiled-builder-libs: the split emits…" with:

```
# 078 → #613 (`.specify/613-builders-export.md`): in a Release tree (FIXPP_INSTALL_TYPED_TIER, owner ruling R-2a
# "Release packages only.") the 078 split's messages/ + groups/ + validators/ layout and its all.hpp / groups.hpp
# aggregators SHIP. Their build_<Msg>/validate_<Msg> declarations resolve against fixpp_builders_<ver>/
# fixpp_validators_<ver>, which are FIXPP_EXPORT_TARGETS members in exactly the same trees, so 078's coherence rule
# (no installed declaration without an exported definition) holds because the definitions are exported. In every
# other tree the five 078 patterns stay, and the rule holds because the declarations are withheld. Superseded: 078
# contracts/cmake-targets.md "Install / export scope". The .builder.inl/.validator.inl files ship with the layout:
# they are the FIXPP_{BUILDERS,VALIDATORS}_HEADER_ONLY[_<Msg>] surface the installed .hpp files include.
# Translation units never ship (*.cpp below).
# This rule is a DENYLIST: a new emitted kind ships by default. What catches one is the schema allowlist in
# tests/packaging/run_package_contents_witness.cmake, written from 078 contracts/include-layout.md, not from this rule.
```

In the `Manifest.txt` block, delete the two sentences that count the patterns ("the denylist is now EIGHT patterns,
not seven. Every 'the 7-pattern denylist' phrase…") and the `8th pattern` banner's ordinal. Keep the reason the
pattern exists.

### 6.2 Further surfaces, per changed fact

The table is built per **fact** this PR changes, with each fact's spellings, not from one lexical sweep. The facts:
F1 export membership (typed tier exported, Release only); F2 shipped layout (slim set installed, Release only); F3
the imported-name rule; F4 the include interface; F5 packaging enablement (D-6 adds `FIXPP_CODEGEN_FIX_LATEST`);
F6 export-member counts; F7 v42 emission (a fact 082 changed, still false in two live docs, P-8); F8 the
Debug/Release asymmetry R-2a introduces.

**Recipe** (re-run it on the implementation branch and adjudicate every hit):

```
git grep -n -i -E 'unexported|never exported|stay unexported|NO install\(\)|build-tree \+ in-tree|absent from the export set|no typed-builder API|header-export is deferred|whole contract|five-pattern|7-pattern|seven-pattern|seven patterns|EIGHT patterns|8th pattern|one of seven|must NOT be installed|diverges from the prefix-strip|14 targets|tracks FIXPP_BUILD_OTEL|13 of the 18|of the 18 export|ELEVEN measured|FOUR are|v42.{0,20}(emits none|descoped)|emits? NO builders|NO builder/validator tier' -- . ':!build'
```

**Known positive:** run on `97748d1e` (2026-10-24), it reports `tests/packaging/run_package_contents_witness.cmake`'s
"of the 18 export members, 13 are STATIC", the respelling v0.2's recipe (`13 of the 18` only) could not match. The
recipe is lexical, so it is backed by `check-comment-claims.py` and by reading each changed region.

**F8 has spellings of its own** (the facts above cannot find it, because before R-2a nothing in the export or layout
varied with the build type):

```
git grep -n -i -E 'configuration-dependent|every (in-scope )?configuration|each configuration|same (export set|file set|payload)|Debug and Release|Release and Debug|per configuration' \
  -- CMakeLists.txt cmake tests/consumer tests/packaging docs .github bindings/python/CMakeLists.txt \
     specs/084-packaging-cpack-export/contracts specs/078-precompiled-builder-libs/contracts
```

Known positive (2026-10-24, `e46ee9a0`): `CMakeLists.txt`'s "fixpp_log_otlp is the one genuinely
configuration-dependent member", which D-0 makes false and which sits beside the D-1 append. The recipe matches one
line at a time, so a phrase wrapped across comment lines escapes it: the fail-closed membership comment's "expected
to exist in EVERY in-scope configuration" was found by reading the region instead. Adjudication of its hits:
- `CMakeLists.txt`, the `fixpp_log_otlp` comment → **amended** (row below).
- `CMakeLists.txt`, the fail-closed loop's "Every other name above is expected to exist in EVERY in-scope
  configuration" → **still true**, left: the typed libs exist in every configuration (they are built in `all`); only
  their membership depends on the build type, and inside a Release tree they are fail-closed like the rest.
- `specs/084-packaging-cpack-export/contracts/package-layout.md` §1, "One dev-shaped package per configuration. Each
  carries headers + static libraries + …" → still true as a model, but the typed content now differs by
  configuration: covered by that contract's banner, which also names §1.
- `specs/084-packaging-cpack-export/contracts/export-set.md`, "`fixpp_otel` is exported in every configuration" and
  the other `fixpp_otel` / `fixpp_log_otlp` rows → about OTel membership, unchanged.
- `.github/workflows/tier1.yml`, "test in EVERY configuration" → the Python witness's modes, unrelated.

**Live surfaces — amended:**

| Surface | Fact | Change |
|---|---|---|
| `CMakeLists.txt`, the `FIXPP_EXPORT_TARGETS` banner ("Membership basis: the ELEVEN measured … plus the SIX derived") | F1, F6 | Add: the builder/validator members appended after the umbrella in Release trees, with their probe in this note. Leave the dated 084 measurement as history. |
| `CMakeLists.txt`, the umbrella comment ("is the whole contract"; "⚠️ The umbrella is deliberately NOT the whole export set …"; "FOUR are exported so a consumer can link them BY NAME") | F1, F6, F8 | Name the eight libs in the BY-NAME class, Release only, with the D-3 reason; replace "FOUR are" with the condition (the by-name members are the ones the umbrella does not reach) |
| `CMakeLists.txt`, "fixpp_log_otlp is the one genuinely configuration-dependent member" | F8 | State the condition instead of "the one": `fixpp_log_otlp` depends on the OTel SDK, and the typed tier on the build type (D-0) |
| `CMakeLists.txt`, "One target diverges from the prefix-strip rule" | F3 | State the rule: nested in-tree aliases get a nested `EXPORT_NAME`; `fixpp_dict_dispatch` by override, the typed libs by the D-2 loop |
| `CMakeLists.txt`, the `INCLUDES DESTINATION` comment ("on the 14 targets that already declare `$<INSTALL_INTERFACE:…>`") | F4 | A dated 2026-08-02 measurement: keep it as dated history and add that the typed libs declare their own since #613, rather than re-counting |
| `CMakeLists.txt`, the 084 FR-011 block ("is PACKAGING available in this configuration?") | F5 | Name the second condition (D-6) next to the OTel one |
| `CMakeLists.txt`, the generated-header rule and the `Manifest.txt` block | F2, F8 | §6.1(b) |
| `cmake/Codegen.cmake`, the library-loop comment "Build-tree + in-tree consumers only; NO install() (R3, Gate A round 1)" | F1, F8 | Replace with: export-set members installed by the top-level `install(TARGETS)` in Release trees since #613, build-tree only otherwise; `fixpp_dict_<ver>` stays build-tree only (084 R2) |
| `cmake/FixppPackaging.cmake` | F5 | The D-6 guard, with its reason |
| `cmake/fixppConfig.cmake.in`, the "Usage" block ("That is the whole contract.") | F1, F8 | Add the by-name typed line, `target_link_libraries(app PRIVATE fixpp::builders::v44 fixpp::validators::v44)`, marked "Release packages only" |
| `docs/src/dictionary/codegen.md`, the generated-tree listing ("NO builder/validator tier — v42 descoped"), "Precompiled Builder/Validator Libraries (078)", the typed-builders note ("descoped", "emit NO builders", "construct v42 messages via the runtime `wire::body_builder`") and the `<ver>` ∈ {v44, v50sp2, vlatest} rows | F1, F7, F8 | Installed consumers: the same names through `find_package(fixpp)`, not through `fixpp::fixpp`, from Release packages only. Correct every v42 statement and the `<ver>` list (P-8) |
| `tests/consumer/CMakeLists.txt` header ("The whole contract is") | F1 | Say it is the umbrella's contract, and that the by-name members, including the typed tier in Release, are linked below |
| `tests/consumer/consumer_witness.cpp` header ("seven-pattern exclusion set") | F2 | "its exclusion rule" |
| `tests/packaging/run_package_contents_witness.cmake` comments ("of the 18 export members, 13 are STATIC", "13 of the 18 export members", "exact 7-pattern denylist", "078 five-pattern tail", "one of seven patterns") | F2, F6 | Conditions instead of counts |
| `.github/workflows/tier2.yml`, "FIXPP_PACKAGING_ENABLED tracks FIXPP_BUILD_OTEL" | F5 | Name both conditions |
| `spec/behaviors-and-limitations.md` | F1, F8 | The §8 delta |

**Normative entry points of merged features — forward banners** (the `brain/index.md` rule that a supersession
pointer must run forward, and #334: a reader who opens the stale file directly must be redirected there):

| Surface | Banner |
|---|---|
| `specs/084-packaging-cpack-export/spec.md`, **FR-010** ("If the final export set does reach either tree, work MUST STOP") and the edge case saying the libs "are absent from the export set by design" | Discharged by R-1 and superseded by #613 for Release configurations; names this note and the 078 banner |
| `specs/084-packaging-cpack-export/contracts/export-set.md`: the "Never exported" table, B2's pattern list, B3, and the "No typed-builder API" bullet | Banner naming this note. Under FR-010, B3's "standing obligation" is discharged by R-1, which the banner quotes and points at the 078 banner for |
| `specs/084-packaging-cpack-export/contracts/package-layout.md`, §1 "Package model" and the "7-pattern exclusion set" rows | Banner naming this note, with R-2a: the Release and Debug packages of one toolchain now differ in typed content |
| `specs/078-precompiled-builder-libs/spec.md`, the Clarifications Q on "Install/export scope" and the "Scope (Gate A round 1): … build-tree + in-tree consumers only" paragraph | Banner naming this note and the 078 contract banner |
| `specs/078-precompiled-builder-libs/contracts/cmake-targets.md` | §6.1(a) |
| `specs/078-precompiled-builder-libs/contracts/include-layout.md`, "Generated file set" ("external … header-export is deferred"; "`v42` emits none") | Banner: installed in Release since #613; v42 emitted since 082 |

**Adjudicated and left unchanged:**
- **Merged-feature records under the bannered bundles:** 084 `plan.md`, `tasks.md`, `research.md`, `data-model.md`
  and `checklists/`; 078 `plan.md`, `research.md`, `tasks.md` and `checklists/codegen-linkage.md`. Each bundle's
  `spec.md` and contracts carry the forward banner. Banners on tasks and checklists are optional (P3) and not added.
- **v42 "descoped" in 069, 077, 079 and 082 records**, `.specify/constitution.md`'s 082 rationale, and
  `tests/codegen/determinism_test.cpp` (whose comment already records the inversion): 082 superseded that fact, not
  this PR.
- **Other merged notes and history:** `.specify/ci254-python-fold.md` ("7-pattern denylist", a dated description of
  the witness at the time) and `CLAUDE-history.md`.
- **Same words, different subject:** every "four are" hit outside the umbrella comment; `specs/035…` "14 targets";
  `specs/049…` "unexported" (a C-ABI symbol); `specs/085…` "whole contract"; `.specify/architecture.md`'s "Absent from
  the export set" (the codegen host tool, still true); `src/session/*.hpp` "no install() rule names src/" (still true);
  `tools/codegen/fixpp-codegen/main.cpp` "emits no builders this run" (vt11).

### 6.3 Outside this repository (for the orchestrator)

- `research/G19-fix-fpml-iso20022/remaining-work/packaging-cpack.md` item 6 says "builders/validators stay
  unexported (078 Gate B P1 — settled, not open)". It also carries the Option 3 ordering constraint and the deferred
  **wall-time bench** ("informs the above"), which R-1 makes moot.
- `REMAINING-WORK.md` row 10 says "typed builders not installed (#613)".
- `fable-assessments/6.2-option3-forward-declared-args.md` says "External consumers are structurally impossible
  today", and makes "before export/API-freeze, either do Option 3 or consciously freeze" its first ordering
  constraint. R-1 is that conscious freeze; the assessment needs an outcome marker.

All three change when this PR merges, and each should say **Release packages only** (R-2a).

## 7. Acceptance map

| #613 item | Planned change | Planned test / evidence |
|---|---|---|
| 1 export + names + umbrella | D-0, D-1, D-2, D-3, D-5, D-6 | C-1 links all eight by name with its own-include-interface check; C-3 and the ABSENT checks on Debug lanes; M-1, M-3, M-3b, M-5, M-9, M-10, M-12, M-13; `packaging::export-names` (derived) |
| 2 slim layout, TUs out, `.inl` decided | D-0, D-7 (five patterns lifted in Release, `*.cpp` added, `.inl` shipped) | C-1, C-2, C-3, the stage children check; `packaging::contents` schema allowlist and build-tree equality; M-2a…f, M-4, M-11, M-14, M-15 |
| 3 out-of-tree consumer test with RED mutants | §4.1 inside the existing install witness, two-sided | M-1…M-15, recorded in the verify record |
| 4 size before/after | §5 method | baseline in §5; "after" from the PR's CI artifacts by the same commands; §4.4's in-test stage line |
| 5 amend 078 contract + install comment | §6.1 texts | `check-comment-claims.py`; `check_line_citations.py --shift-audit`; §6.2 recipe re-run, every hit adjudicated |

## 8. B&L delta (`spec/behaviors-and-limitations.md`)

- **B-613-1.** An installed consumer of a **Release** package can build and validate typed messages for v42, v44,
  v50sp2 and vlatest. It links `fixpp::builders::<ver>` and/or `fixpp::validators::<ver>` by name. `fixpp::fixpp`
  does not reach them, and a `build_<Msg>` call from an umbrella-only consumer fails at link time naming the
  function. The 078 header-only macros work in the installed tree, under 078's same-message ODR rule.
- **L-613-1.** A tree configured with `FIXPP_CODEGEN_FIX_LATEST=OFF` exports and installs no vlatest typed tier and
  defines no packaging.
- **L-613-2.** The span-based `Args` shape, L-078-1's compile cost included, is installed public API and frozen for
  v1.0 (R-1). A forward-declared or handle-based `Args` ("Option 3") is a 2.0 break.
- **L-613-3 (R-2a).** Debug packages, and every installation from a non-Release build type (`RelWithDebInfo` and
  `MinSizeRel` included) or from a multi-config generator, carry **no typed builder/validator tier**: no
  `fixpp::builders::*` / `fixpp::validators::*` target, and no slim header, so header-only inline mode is unavailable
  too. A Debug-configured consumer that uses the typed tier has no supported package to take it from: 084 requires a
  consumer to match the package's configuration, and on MSVC a Debug consumer cannot link Release libraries at all
  (runtime-library and iterator-debug-level mismatch). In-tree Debug builds are unaffected: the libs are still built
  and linkable there.

## 9. Owner rulings, 2026-10-24

**R-1 (was OQ-1): `Args` shape.** Ruling, quoted: "FREEZE span-based Args as the v1.0 public shape (Option 3 stays
deferred; a later change would be a 2.0 break)."

The question was raised because three documents require an explicit decision before `install(EXPORT)` ships `Args`:
- 084 FR-010 and B3's standing obligation in `contracts/export-set.md`;
- `remaining-work/packaging-cpack.md` item 6's ordering constraint;
- `fable-assessments/6.2-option3-forward-declared-args.md`.

This ruling is that decision, so FR-010's "work MUST STOP" no longer blocks the implementation.

**Where the PR records it:**
- **The 078 contract amendment, §6.1(a).** The banner on `contracts/cmake-targets.md` "Install / export scope" quotes
  the ruling. This is the authoritative in-repo record.
- **The 084 `spec.md` FR-010 banner and the `contracts/export-set.md` banner (§6.2).** They quote the ruling as the
  discharge of B3's standing obligation, and point at the 078 banner.
- **B&L L-613-2 (§8).**
- **The parent-repository surfaces of §6.3.** The orchestrator updates them; this PR does not touch them.

**R-2 (was OQ-2): cost.** Ruling: keep "all four versions, every platform" at the measured cost (§5). As ruled, it
covered the Release packages: the gcc archives, the slim headers about as large again, and the Windows release ZIP
growing from about 21 MB to about 120 MB (about 0.77 GB unpacked).

**R-2a (amends R-2): "Release packages only."** The triage (N-3) showed that §5 v0.2 put the Debug configurations
that 084 ships under "Not shipped", so R-2 had been ruled without their cost. Told the Debug growth (about 4.9 GB raw
for linux-clang-debug, one archive about 1.36 GB; about 2.3 GB of MSVC Debug `.lib` before PDBs; both estimates),
the owner ruled that only Release configurations carry the typed tier. "All four versions" stands for every Release
package; Debug packages carry none of the typed tier (D-0, L-613-3). v0.2's per-lane 5–11 GB stage copies no longer
arise, and D-10 is withdrawn with them.

The implementation records the measured "after" figures from the PR's CI artifacts (§5).

No open owner question remains. Technical questions are settled in §1–§4 or assigned there as implementation
obligations with their measurement.

## Gate A

**Round 1 applied 2026-10-24: Codex P1=0 P2=6 P3=2; Opus post-judging P1=0 P2=8 P3=10; rewrite addresses root causes
1 (instruments sharing a source with the thing they check), 2 (a rule declared untestable), 3 (cost record covering
a subset of what pays), 4 (amendments chosen by policy and one lexical sweep) and 5 (precision defects); owner ruling
R-2a applied. Reviews: research/reviews/codex_613_1_builders-export_review.md,
research/reviews/opus_613_1_builders-export_triage.md.**

Where each finding landed:
- **Root cause 1.** Codex #1 → §4.2 schema allowlist, M-15, §6.1(b) comment text. Codex #3 → D-6 module guard,
  §4.2 unconditional version list, M-8a/b/c. N-1 → D-10 withdrawn (R-2a removed its reason) and the unfiltered
  recipe recorded. N-9 → D-4's widened recipe with its known positive. R-2a's two-sided checks take their expectation
  from the build type, never from `FIXPP_INSTALL_TYPED_TIER` (§4, M-10, M-13).
- **Root cause 2.** Codex #2 → D-5 and §4.1's own-include-interface check, M-9; the D-1 probe's missing edit set is
  disclosed.
- **Root cause 3.** Codex #4 → §5 labels (estimate, commit, date, the `stat` recipe; no PDBs in Release). Codex #5 →
  §4.4's in-test driver line on every lane, coverage included. N-2 → §4.4's derived list of stage-installing tests,
  Python witness included. N-3 → §5's per-configuration table and owner ruling R-2a. N-4 → §4.4's compile-cost
  obligation.
- **Root cause 4.** Codex #6 → §6.2's normative-entry-point banners (084 FR-010 and edge case, 078 spec scope and
  clarification), §6.3's Fable assessment and wall-time bench, `--shift-audit`. N-5 → §6.2 rebuilt per fact, with
  the recipe's known positive and every hit adjudicated; R-2a's own fact (F8) has its own recipe, known positive and
  adjudication.
- **Root cause 5.** Codex #7 → P-4. Codex #8 → D-0's multi-config section. N-6 → D-9 `/W4` reason; the dangling §4.5
  references now point at §4.4. N-7 → C-1's two oracle sources. N-8 → M-6's hand procedure and lane. N-10 → §4.2's
  first-component derivation.

### Round 1 — disagreements

The triage disagreed with no Codex finding outright. Where v0.3 does not apply Codex's own counter-proposal, the
reason is recorded here:
- **Codex #3, nested-configure arm.** Not applied. It pays the codegen bootstrap and five generations on a packaging
  lane every run to guard one deleted line. The triage's shape costs nothing at runtime and makes the regression loud
  by construction: the `cmake/FixppPackaging.cmake` guard plus the unconditional expected version list (D-6, M-8a/b).
- **Codex #5, a new disk step in the `linux-clang-coverage` job.** Not applied. The driver's in-test line (§4.4) is
  read on every lane, coverage included, and sees the in-test peak, which a job-end step cannot. Under R-2a coverage
  is an ABSENT lane, so its stage does not grow.
- **Codex #6, "name every directly false document" with a marker in each.** Narrowed. Forward banners go at each
  merged bundle's normative entry points (`spec.md` and contracts); the bundles' plan, tasks, research, data-model
  and checklists stay as unmarked history, adjudicated by name in §6.2. A reader entering a bundle meets the banner at
  its spec.
- **Codex #8, a `cmake --install --config Debug/Release` coherence probe for multi-config trees.** Not applied.
  Under D-0 a multi-config tree has no typed tier in any configuration, so there is no per-config difference to
  probe, and the toy probe in D-0 already recorded why `CONFIGURATIONS` could not deliver one.
