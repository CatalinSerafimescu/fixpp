# #613 — export the typed builder/validator libraries and install the slim typed headers

> **Status: draft v0.8, 2026-10-24.** **v0.8 (records only, after implementation): the note now states four things
> the implementation does where v0.7 prescribed otherwise or said nothing. (1) §4's install-witness registration passes
> `-DFIXPP_IS_MSVC=$<BOOL:${MSVC}>`, because CMake refuses `$<CXX_COMPILER_ID:MSVC>` in an `add_test()` command
> ("may only be used with binary targets"). (2) C-1's literals have a third source, `.specify/2a-decimal.md` §6.2,
> for the decimal fields vlatest ListStatus's required NoOrders entry carries; only exponent-0 values are used.
> (3) D-0's cpack-time guard refuses a `release` package unless the effective value is exactly ON, and has no domain
> refusal of its own: `cmake/FixppPackaging.cmake` writes the root's resolved value, which is only ever ON or OFF, so
> such a refusal would sit on a branch nothing reaches (cpack with the value OFF or empty both exit 1). (4) The
> tests' copy of the rule (D-0, §4) lives in one test-only module, `cmake/FixppTypedTierRule.cmake` (guard 0's
> domain check, guard 1's re-derivation, the expected per-version children), shared by both `cmake -P` witnesses.
> The root CMakeLists never includes it. Evidence: parent `decisions/speckit/613-builders-export-evidence.md`.**
> **v0.7 (records only, before implementation): the seven P3s of the final
> Opus post-exhaustion review applied (research/reviews/opus_613_4_builders-export_postexhaustion_review.md, P1=0
> P2=0 P3=7, converged); where each landed is in `## Gate A`.** Round 1 applied 2026-10-24: Codex P1=0 P2=6 P3=2; Opus post-judging P1=0 P2=8
> P3=10; rewrite addresses root causes 1–5 (named in `## Gate A`); owner ruling R-2a applied. Reviews:
> research/reviews/codex_613_1_builders-export_review.md, research/reviews/opus_613_1_builders-export_triage.md.
> **R-2b applied (owner amendment, between rounds 1 and 2):** the typed tier sits behind the cache setting
> `FIXPP_INSTALL_TYPED_TIER`, in effect by default for Release trees only and switchable ON by anyone who builds fixpp
> themselves, in any configuration (D-0).
> **Round 2 applied 2026-10-24: Codex P1=0 P2=3 P3=2; Opus post-judging P1=0 P2=3 P3=9; rewrite addresses root causes
> 1 (mechanism written without the configure order and the per-lane flags it runs under), 2 (guards that do not check
> their own input's domain), 3 (a settled ruling applied to the prose but not to every mechanism encoding it) and 4
> (precision defects). Reviews: research/reviews/codex_613_2_builders-export_review.md,
> research/reviews/opus_613_2_builders-export_triage.md.**
> **Post-exhaustion pass applied 2026-10-24 (owner: hand-edit + one Opus round): round-3 Codex P1=0 P2=1 P3=2; Opus
> post-judging P1=0 P2=1 P3=6 (research/reviews/codex_613_3_builders-export_review.md,
> research/reviews/opus_613_3_builders-export_triage.md); P2 four-step fix + six P3s applied.**
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
>   defines.
> - **2026-10-24 (R-2b, amends R-2a), after the L-613-3 disclosure (a Debug consumer of the typed API had no supported
>   package):** "Can we put it behind an option and activate it for Release packages, and allow a consumer to build
>   its own Debug and switch it on? More - I don't think we should even provide Debug packages at all, I think we
>   ruled we will never release Debug configs, which means that a consumer will have to build fixpp itself."
>   The earlier ruling it names is `tier1.yml`'s "RELEASE-ONLY IN CI (user decision 2026-08-03)" comment: no Debug
>   configuration uploads packages, and Debug packaging works only locally (P-11). D-0 realises R-2a and R-2b
>   together.

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

R-2a and R-2b put items 1 and 2 behind `FIXPP_INSTALL_TYPED_TIER`: in effect by default in Release trees, which are
the only released packages, and available ON in any self-built configuration. Item 3's test is two-sided
accordingly (§4).

**Out of scope:**
- Any change to the generated code, the emitter, or the `Args` shape. That is "Option 3", deferred by R-1 (§9).
- The per-version `fixpp::dict::<ver>` INTERFACE targets. They stay unexported, as 084 research R2 decided.
- A runtime/dev component split of the package. 084 shipped one dev-shaped package per configuration.
- Multi-config generators as a packaging configuration (Article III: Ninja only; D-0 says what such a tree does).
- Removing the local Debug packaging capability. R-2b restates that Debug configurations are never released; CI
  already uploads none. Whether `fixpp-package` should also refuse a Debug tree is not asked here and is not changed.
- Parent-repository surfaces (§6.3). They are left to the orchestrator.

## 1. Premises checked against source

| # | Issue premise | Finding | Evidence |
|---|---|---|---|
| P-1 | The libs are "under the switch that builds them (`FIXPP_BUILD_CODEGEN_TOOL`)" | **Moot. `FIXPP_BUILD_CODEGEN_TOOL=OFF` does not configure on `origin/main`.** The export block fails closed on `fixpp_dict_dispatch`, which only `cmake/Codegen.cmake` defines. `cmake/FixppPackaging.cmake` also refuses OFF (its FR-011a guard). So an installed package without the builder libs *because they were not built* cannot be produced. This design adds no OFF support. | Probe, 2026-10-24, `4e8b3779`: `cmake -S . -B <scratch> -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=build/linux-gcc-release/conan_toolchain.cmake -DFIXPP_BUILD_TESTS=OFF -DFIXPP_BUILD_PYTHON=OFF -DFIXPP_BUILD_CODEGEN_TOOL=OFF` → rc 1, `084 FR-007: export set names 'fixpp_dict_dispatch', which is not a target` |
| P-2 | One switch gates the libs | **The real per-version switch is `FIXPP_CODEGEN_FIX_LATEST`** (an `option()` in `cmake/Codegen.cmake`, default ON). When it is OFF, the builder/validator loop in `cmake/Codegen.cmake` skips vlatest: no `fixpp_builders_vlatest`, no `fixpp_validators_vlatest`, and the vlatest output directory is removed. `docs/src/dictionary/codegen.md` documents OFF as a developer configure. v42, v44 and v50sp2 have no such switch. **R-2a/R-2b add a second, independent condition: the `FIXPP_INSTALL_TYPED_TIER` setting, whose default follows the build type (D-0).** | `cmake/Codegen.cmake` (the `foreach(_ver IN ITEMS v42 v44 v50sp2 vlatest)` block and the T015 stale-dir cleanup) |
| P-3 | `EXPORT_NAME`s to be chosen | **The in-tree aliases are nested: `fixpp::builders::<ver>` and `fixpp::validators::<ver>`.** The prefix-strip sweep would give `builders_v44`. T026's alias check then fails at configure, because no `fixpp::builders_v44` alias exists. An explicit nested `EXPORT_NAME` is required. The precedent is `fixpp_dict_dispatch` → `dict::dispatch`. | Probe, 2026-10-24: the planned edit with the override switched off → `084 T026: export member 'fixpp_builders_v42' resolves to imported name …` (rc 1) |
| P-4 | "a consumer of `find_package(fixpp)`, a CPack package **or Conan**" | **This repository defines no Conan package, so there is no Conan component metadata to amend.** `conanfile.py` is a consumer/build recipe (`name`, `requirements()`, `options`, `generators = "CMakeToolchain", "CMakeDeps"`) with no `package()` and no `package_info()`. | `conanfile.py` |
| P-5 | CPack placement | **CPack has no components.** `cmake/FixppPackaging.cmake` has no `cpack_add_component` and no `CPACK_COMPONENT*`. No install rule carries `COMPONENT`. The new files land in the one package per configuration, like everything else. | `cmake/FixppPackaging.cmake`; `bindings/python/pyproject.toml`'s `wheel.exclude` comment says the same |
| P-6 | "about 150 MB uncompressed" | **True of the gcc-release archives, and incomplete as a cost.** The slim headers add about as much again; MSVC release archives are about 4× the gcc size; Debug archives are GB-scale, which is why R-2a keeps them out of the default. Numbers in §5. | §5 |
| P-7 | "lift the five exclusions" | **The rule has eight patterns.** The five are 078's (`messages`, `groups`, `validators`, `all.hpp`, `groups.hpp`). `_dispatch`, `vt11` and `Manifest.txt` stay. One pattern is added, for the `.cpp` translation units (D-7). The five are lifted only where the typed tier is in effect (D-0). | the generated-header `install(DIRECTORY "${CMAKE_BINARY_DIR}/_codegen/include/" …)` rule in `CMakeLists.txt` |
| P-8 | — | **Two docs are already wrong about v42.** `specs/078-precompiled-builder-libs/contracts/include-layout.md` says "`v42` emits none (deferred to #196)". `docs/src/dictionary/codegen.md` says v42 is "descoped" and "emit[s] NO builders", tells readers to "construct v42 messages via the runtime `wire::body_builder`", and lists `<ver>` ∈ {v44, v50sp2, vlatest}. 082 (issue #196) added v42 to the emitter and to the library loop. Both are corrected in this PR (§6). | `cmake/Codegen.cmake`'s 082 T035 comment on the library loop |
| P-9 | — | **The Python wheel's content is unaffected; without an opt-out its build would stage more.** `wheel.exclude = ["include/**", "lib/**", "share/fixpp/**"]` already drops both new file kinds. The wheel already builds the libs, because they are in `all`. Its build sets `cmake.build-type = "Release"` and `FIXPP_BUILD_OTEL = "OFF"`, so `AUTO` would resolve ON there while packaging is disabled: scikit-build-core would stage the full install, typed archives and slim headers included, before `wheel.exclude` drops them. **This PR sets `FIXPP_INSTALL_TYPED_TIER = "OFF"` in the wheel's `[tool.scikit-build.cmake.define]`** (D-8). It is compatible with every guard: OTel OFF with no `FIXPP_ALLOW_OTEL_OFF_PACKAGE` leaves `FIXPP_PACKAGING_ENABLED` OFF, so there is no CPack configuration and the cpack-time guard is never read, and `FIXPP_BUILD_TESTS = "OFF"` registers no witness. | `bindings/python/pyproject.toml` (`cmake.build-type`, `[tool.scikit-build.cmake.define]`, `wheel.exclude`) |
| P-10 | — | **The packaging presets use exactly two build types, Release and Debug.** Every sanitizer, coverage and libc++ preset is Debug. 084 `contracts/package-layout.md` §1 ships six configurations: Linux clang and gcc, Windows MSVC, each Release and Debug. | Recipe: resolve `CMAKE_BUILD_TYPE` through `inherits` for every non-hidden `configurePresets` entry of `CMakePresets.json` (a short `json` walk), 2026-10-24, `4e8b3779`. No configure preset inherits another (each inherits only `_base`), so a cache variable set on one preset reaches no other |
| P-11 | — | **Only Release packages are released.** 084 §1 defines Debug configurations, but `tier1.yml`'s `Build packages` comment records "RELEASE-ONLY IN CI (user decision 2026-08-03) … No -Debug configuration uploads packages", and says Debug packaging works only locally. R-2b reaffirms it: a Debug consumer builds fixpp itself. | `.github/workflows/tier1.yml`, the `Build packages` / `Upload packages` steps' comment |

## 2. Design decisions

### D-0 — R-2a + R-2b: the typed tier behind `FIXPP_INSTALL_TYPED_TIER`

**The setting.** One cache entry, declared and resolved in the root `CMakeLists.txt` directly after the
`if(FIXPP_BUILD_CODEGEN_TOOL) include(cmake/Codegen.cmake) endif()` block, **outside** that `if`, beside D-6's root
decision, and before the `if(FIXPP_BUILD_TESTS)` block.

**Why there (round 2).** `add_test()` expands `${…}` when it executes. Both witness registrations read the setting:
`fixpp::consumer::install-witness` inside the root's tests block, and `fixpp::packaging::contents` inside
`add_subdirectory(tests/packaging)`, which that block enters first. Both precede the generated-header install rule
and the export block, so a declaration beside the header rule (v0.4) would hand them empty values: the effective
value always (a plain variable), the raw setting on a fresh tree at `AUTO`. `tests/packaging/CMakeLists.txt` records
the same trap for `FIXPP_EXPORT_TARGETS` ("set in the root CMakeLists AFTER this subdirectory is added, so reading it
here yields an empty list"). The setting needs nothing from `cmake/Codegen.cmake`, so it can sit after it; it must
precede every reader. Only the conditional pattern list stays beside the header rule (D-7). Re-derive the order with
`grep -n -E 'include\(cmake/Codegen.cmake\)|^if\(FIXPP_BUILD_TESTS|add_subdirectory\(tests/packaging\)|install-witness|_codegen/include/"|APPEND FIXPP_EXPORT_TARGETS' CMakeLists.txt`.
An ordering slip is also caught by the instrument: each witness refuses an empty or out-of-domain value (§4,
guard 0). M-21 (§4.3) re-creates v0.4's slip to show that refusal RED.

```cmake
set(FIXPP_INSTALL_TYPED_TIER "AUTO" CACHE STRING
    "Export and install the typed builder/validator tier (#613): AUTO = only for a single-config Release tree; ON/OFF force it")
set_property(CACHE FIXPP_INSTALL_TYPED_TIER PROPERTY STRINGS AUTO ON OFF)
string(TOUPPER "${FIXPP_INSTALL_TYPED_TIER}" _fixpp_typed_setting_uc)
get_property(_fixpp_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
string(TOUPPER "${CMAKE_BUILD_TYPE}" _fixpp_build_type_uc)
if(_fixpp_typed_setting_uc STREQUAL "AUTO")
  if(NOT _fixpp_multi_config AND _fixpp_build_type_uc STREQUAL "RELEASE")
    set(FIXPP_INSTALL_TYPED_TIER_EFFECTIVE ON)
  else()
    set(FIXPP_INSTALL_TYPED_TIER_EFFECTIVE OFF)
  endif()
elseif(_fixpp_typed_setting_uc MATCHES "^(ON|YES|TRUE|Y|1|OFF|NO|FALSE|N|0)$")
  if(FIXPP_INSTALL_TYPED_TIER)
    set(FIXPP_INSTALL_TYPED_TIER_EFFECTIVE ON)
  else()
    set(FIXPP_INSTALL_TYPED_TIER_EFFECTIVE OFF)
  endif()
else()
  message(FATAL_ERROR "#613: FIXPP_INSTALL_TYPED_TIER must be AUTO or a boolean (ON/YES/TRUE/Y/1, OFF/NO/FALSE/N/0; case-insensitive), got '${FIXPP_INSTALL_TYPED_TIER}'")
endif()
message(STATUS "#613: typed builder/validator tier "
               "${FIXPP_INSTALL_TYPED_TIER_EFFECTIVE} (FIXPP_INSTALL_TYPED_TIER=${FIXPP_INSTALL_TYPED_TIER}, "
               "CMAKE_BUILD_TYPE='${CMAKE_BUILD_TYPE}')")
```

Every surface whose content depends on the tier keys on `FIXPP_INSTALL_TYPED_TIER_EFFECTIVE`, never on the build
type, and is placed after its resolution. The witnesses receive it together with the raw setting (§4).

**Why a tri-state STRING and not a BOOL `option()`.** For the user the two are the same: `-DFIXPP_INSTALL_TYPED_TIER=ON`
forces it, and AUTO or a boolean (ON/YES/TRUE/Y/1, OFF/NO/FALSE/N/0; case-insensitive) is accepted. The difference is the default:
- a BOOL whose default is computed from `CMAKE_BUILD_TYPE` is frozen into the cache at the first configure, so a tree
  reconfigured from Debug to Release (or back) keeps the other build type's value. `AUTO` is re-evaluated at every
  configure;
- a BOOL cannot tell "defaulted" from "set by the user", so a test cannot tell a defaulting bug from a choice, and a
  wrong default turns both sides green. With `AUTO` the tests check the default against their own copy of the rule
  (§4). That copy is one test-only module, `cmake/FixppTypedTierRule.cmake`, which both `cmake -P` witnesses include;
  the root CMakeLists never includes it (v0.8).

**The default (`AUTO`).** ON for a single-config tree whose `CMAKE_BUILD_TYPE` is `Release`, compared
case-insensitively (CMake treats configuration names case-insensitively, and `cmake/FixppPackaging.cmake`
lowercases the build type for the artifact name). OFF for `Debug`, `RelWithDebInfo`, `MinSizeRel`, any custom type,
and every multi-config generator. No preset produces anything but Release or Debug (P-10). RelWithDebInfo stays out
of the default on R-2a's own reason: it carries the debug information that made the Debug archives GB-scale.

**Released packages cannot end up OFF: the refusal fires at package generation, never at configure.** A FATAL in
`cmake/FixppPackaging.cmake` itself would run on every OTel-ON configure, because the root includes the module
whenever `FIXPP_PACKAGING_ENABLED` is ON, and would refuse a Release self-build configured OFF before anyone asked for
a package. The module's own T062a comment records why that shape was removed once for OTel ("The first cut refused
to CONFIGURE an OTel-OFF build, which was wrong because FR-011 explicitly permits such development builds"). So:
- **Mechanism.** The module sets `CPACK_FIXPP_PKG_CONFIG` (`_fixpp_pkg_config`) and
  `CPACK_FIXPP_TYPED_TIER_EFFECTIVE` before its `include(CPack)`, so both are written into `CPackConfig.cmake` (only
  `CPACK_*` variables are), and sets `CPACK_PROJECT_CONFIG_FILE` to a new script, `cmake/FixppCPackTypedTierGuard.cmake`.
  cpack reads that script when it initialises each generator; it FATALs when the configuration is `release` and the
  effective value is anything but exactly ON, naming the setting and R-2 (v0.8: keyed on "not ON", so an empty or
  out-of-domain value is refused by the same branch; there is no separate domain refusal). No code sets `CPACK_PROJECT_CONFIG_FILE` today:
  `git grep -n CPACK_PROJECT_CONFIG_FILE -- CMakeLists.txt cmake tests .github bindings` was empty on 2026-10-24 on
  this branch, whose code is `4e8b3779`'s (scoped to code because this note now names the variable).
- **Reach.** Every package is generated by `cpack --config "${…}/CPackConfig.cmake"`: the `fixpp-package` target's
  command, CI's `Build packages` steps through it, and the packaging witnesses that execute cpack themselves. List
  those with `git grep -n 'COMMAND "${CMAKE_CPACK_COMMAND}"' -- tests/packaging` and read each hit; a bare
  `CPackConfig.cmake` grep overcounts, because `run_package_target_witness.cmake` only looks for that string in the
  build graph and runs no cpack. One cpack-time guard covers all of them; a guard on the wrapper target alone would
  miss the witnesses.
- **Not in the provenance `install(CODE)` block.** That block runs on every `cmake --install`, the consumer witness's
  stage install included, so it would refuse a permitted install.
- **Toy observation** (CMake/CPack 3.30.0, 2026-10-24, a one-file project in the session scratch directory, since
  deleted): with the guard's input OFF, `cpack --config CPackConfig.cmake` printed the FATAL, then "CPack Error: Cannot
  initialize the generator TGZ", exited 1, and its `install(CODE)` message never printed, so the refusal precedes
  staging; with the input ON it packaged and exited 0. The implementation repeats the exit-code check once on a real
  tree (M-20).

What follows:
- a Release tree with OTel ON may configure, build, install and run its consumer witness with the tier OFF; it cannot
  generate a package, and on such a tree each `packaging`-labelled witness that executes cpack is RED (M-20 records
  which);
- a Release tree with packaging disabled (an OTel-OFF development build) has no CPack configuration at all;
- a local Debug package (P-11: never released) may be generated with either value; the contents witness follows the
  effective value there (§4.2).

**Single-config generators (every preset, Ninja).**
The **expected pairs** below mean the builder/validator pair (`fixpp_builders_<ver>`, `fixpp_validators_<ver>`) for
every expected version: v42, v44 and v50sp2, plus vlatest iff `FIXPP_CODEGEN_FIX_LATEST` (P-2). That is §4.1's
expected-list form.
- **Effective OFF** (the default in a Debug tree): the expected pairs are not appended to `FIXPP_EXPORT_TARGETS`, so
  `fixppTargets.cmake` and `fixppTargets-<config>.cmake` never name `fixpp::builders::*` or `fixpp::validators::*`;
  the generated-header rule keeps 078's five exclusions, so the include tree is today's; the libs are still built
  (they are in `all`, for the in-tree tests) and none is installed.
- **Effective ON in a Debug tree** (a consumer's own build, R-2b): exactly the Release shape, with that tree's Debug
  archives. The export is unfiltered, so `fixppTargets-debug.cmake` gives each typed target its
  `IMPORTED_LOCATION_DEBUG`, and the slim headers are installed beside it: a coherent Debug typed tier. Its debug
  information follows the tree's MSVC debug-information format and, for separate PDBs, whether packaging is enabled
  (D-9): the PDB plumbing lives only in `cmake/FixppPackaging.cmake`, which is pre-existing for every export member.
  The `windows-msvc-debug` CI lane runs this case (§4.4).

**Multi-config generators.** `AUTO` is OFF in every configuration, which gives a coherent prefix with no typed tier.
Set ON, the libs join the export set unfiltered and the header rule lifts the five patterns for every configuration.
That is the ordinary multi-config export: `cmake --install --config <C>` writes `fixppTargets-<c>.cmake` with that
configuration's own locations. A second toy probe settled what that yields (CMake 3.30.0, 2026-10-24, Ninja
Multi-Config, Debug and Release, a `typed` library that returns a different value per configuration, since deleted):
- **One configuration per prefix:** a consumer of the matching configuration links that configuration's archive
  (Debug prefix, Debug consumer: the Debug value; Release prefix, Release consumer: the Release value).
- **A consumer of the other configuration** silently maps to the only installed one (CMake's
  `IMPORTED_CONFIGURATIONS` fallback). That is CMake's behaviour for every fixpp member today, not the typed tier's.
  On MSVC the mix is a link error rather than a silent one (the `_ITERATOR_DEBUG_LEVEL` / `RuntimeLibrary`
  `/failifmismatch` records, LNK2038).
- **Both configurations into one prefix:** `IMPORTED_CONFIGURATIONS=DEBUG;RELEASE`, with both locations naming the
  same `lib/libtyped.a`; the Release consumer ran the Debug code. fixpp sets no `DEBUG_POSTFIX` anywhere
  (`grep -rn _POSTFIX CMakeLists.txt cmake CMakePresets.json` is empty), so this collision already applies to every
  fixpp archive. It is a pre-existing property of an unsupported generator (Article III), stated in L-613-3, and not
  something this PR changes or fixes.

Packaging a multi-config tree FATALs unless `CMAKE_BUILD_TYPE` is also set (FR-017's `CMAKE_BUILD_TYPE is empty`).

**Rejected: `install(... CONFIGURATIONS Release)` on the libs and on the header rule.** That was the v0.3 question:
whether one tree could carry the typed tier in Release and not in Debug by filtering the install rules per
configuration. It cannot be made coherent. A first toy probe settled what that generates (CMake 3.30.0, 2026-10-24,
a two-library project in the session scratch directory, since deleted: library `typed` installed
`CONFIGURATIONS Release` into the same export set as an unconditional `core`):
- **Single-config Debug tree.** The config-independent `pTargets.cmake` still declares
  `add_library(p::typed STATIC IMPORTED)`; `pTargets-debug.cmake` gives it no location. A consumer sees
  `if(TARGET p::typed)` true, and linking it fails at generate time with "IMPORTED_LOCATION not set for imported
  target". The target is declared, not absent.
- **Multi-config tree (Ninja Multi-Config, Debug and Release installed into one prefix).** `p::typed` has
  `IMPORTED_CONFIGURATIONS=RELEASE` only. A Debug consumer configured, linked `libtyped.a` (the Release archive) and
  ran with rc 0. That is the silent Release-into-Debug link the ruling must not allow. The shared `include/` would
  also hold the Release slim headers in the Debug view of the prefix.

So "present in Release, absent in Debug" cannot be expressed inside one multi-config prefix at all: one
`fixppTargets.cmake` and one `include/` serve every configuration. The setting is therefore per tree, never per
configuration, and the export is never configuration-filtered.

**Coherence in both states.** Effective ON ships the declarations and exports the definitions of that tree's
configuration. Effective OFF ships neither. 078's coherence rule holds in each.

### D-1 — Export membership

Inside `if(FIXPP_INSTALL_TYPED_TIER_EFFECTIVE)`, the libs are appended to `FIXPP_EXPORT_TARGETS` in the export block,
after the umbrella and beside the `fixpp_log_otlp` append. They follow the same fail-closed discipline:

```cmake
if(FIXPP_INSTALL_TYPED_TIER_EFFECTIVE)
  foreach(_fixpp_typed_ver IN ITEMS v42 v44 v50sp2)
    list(APPEND FIXPP_EXPORT_TARGETS fixpp_builders_${_fixpp_typed_ver} fixpp_validators_${_fixpp_typed_ver})
  endforeach()
  if(FIXPP_CODEGEN_FIX_LATEST)
    list(APPEND FIXPP_EXPORT_TARGETS fixpp_builders_vlatest fixpp_validators_vlatest)
  endif()
endif()
```

- v42, v44 and v50sp2 are **unconditional while the tier is in effect**. If one is missing, the existing membership check reports
  it as a defect instead of skipping it.
- vlatest is gated on **the option that decides whether it is built (P-2)**, never on `if(TARGET …)`. With the option
  ON and the target missing, the check still fails closed.
- The append sits **inside** `if(NOT FIXPP_CODEGEN_BOOTSTRAP_RUNNING)`, like the rest of the export block.
- **What follows from the list, with no new code:** the T026 export-name manifest (`fixpp-export-names.txt`), the
  `fixpp-package` target inputs, the derived `find_dependency` set, and `cmake/FixppPackaging.cmake`'s MSVC PDB loop
  all iterate `FIXPP_EXPORT_TARGETS`. With the tier in effect they gain the expected pairs (D-0); otherwise they are
  unchanged from today.

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
- The implementation adds the Debug side of this probe, configure only, twice: a Debug tree with `AUTO` whose
  `fixpp-export-names.txt` and generated `fixppTargets*.cmake` contain no `fixpp::builders::` or
  `fixpp::validators::` name, and the same tree with `-DFIXPP_INSTALL_TYPED_TIER=ON` whose manifest holds the
  expected pairs' nested names and whose `fixppTargets-debug.cmake` gives each an `IMPORTED_LOCATION_DEBUG`.

### D-2 — `EXPORT_NAME`s

The names are `builders::<ver>` and `validators::<ver>`, so the imported names are `fixpp::builders::<ver>` and
`fixpp::validators::<ver>`. These are the in-tree aliases, the names `docs/src/dictionary/codegen.md` already tells
consumers to link, and what T026's alias check requires (P-3).

They are set by one loop over `FIXPP_EXPORT_TARGETS`, placed after the prefix-strip sweep and the `dict::dispatch`
override. The loop matches `^fixpp_(builders|validators)_(.+)$`, so a version added later is named by the same rule
and is never left out of a hand-written list. With the tier not in effect the loop matches nothing.

### D-3 — The umbrella does not reach them; consumers link by name

`fixpp::fixpp` stays `INTERFACE fixpp_session` plus its include root. The expected pairs (D-0) join the existing
**by-name class**: today that is `fixpp::capi`, `fixpp::config_toml`, `fixpp::tap` and `fixpp::service`, in the
umbrella comment's own terms. Reasons:

- 078 FR-004 makes the consumer's opt-in "purely link-time — link builders, validators, both, or neither", and
  `codegen.md` documents exactly that link line. Folding the libs into the umbrella would remove the choice for
  installed consumers only, and leave the two consumer kinds with different contracts.
- `fixpp_session` links none of them. The umbrella comment defines the umbrella as the session closure, so reaching
  the libs would change what the umbrella means, not just what it contains.
- The headers are still reachable through the umbrella's include root in an install with the tier: both install rules write
  to `${CMAKE_INSTALL_INCLUDEDIR}`. An umbrella-only consumer who calls `build_<Msg>` therefore gets an undefined
  symbol at link time, naming the function. That is the same behaviour an in-tree consumer gets today when it forgets
  the lib. B-613-1 states it (§8).
- The umbrella's meaning is independent of the setting: it is the same target with the tier ON or OFF, and only the
  by-name class differs.

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
entry is unconditional in `cmake/Codegen.cmake`: it is read only when the target is exported, so with the tier not
in effect it is inert.

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

No CI lane configures OFF (no preset or workflow sets the option), so the root-decision deletion is recorded once as
a RED mutant (M-8a, whose observed RED is the module guard's FATAL, so M-8a is also the guard's mutation proof).
Deleting the module guard alone cannot go RED while the root decision holds; that arm is an expected-green
independence check (§4.3, outside the RED table), not a mutation kill, and the guard is belt-and-braces by design.
The contents witness's expected version list is **unconditional** (all four, as R-2 rules for every package), so it
cannot track the defect it should catch (§4.2).

A FATAL_ERROR at the root is rejected. `FIXPP_PACKAGING_ENABLED` is ON in every OTel-ON configure, so a root FATAL
would break the documented developer configure.

The export and the install rules stay live in that tree, so `cmake --install` of a FIX_LATEST=OFF tree still
produces a coherent prefix: no vlatest headers, because the directory is removed, and no vlatest targets.

**Measured (probe, as in D-1, with `-DFIXPP_CODEGEN_FIX_LATEST=OFF`, Release):** the STATUS line is printed, no
`CPackConfig.cmake` is generated, and the export declares 24 imported targets instead of 26.

### D-7 — Header surface and the installed-file-set rule

Where `FIXPP_INSTALL_TYPED_TIER_EFFECTIVE` is ON, the generated-header install rule loses the five 078 patterns. In
every tree it gains **`PATTERN "*.cpp" EXCLUDE`**. One `install(DIRECTORY)` rule serves both states: its OFF branch
keeps the five 078 patterns. The rule keys on the effective value, never on the build type, so a Debug tree with the
tier ON installs the slim headers its exported Debug libs define.

- A translation unit is never a header, so the TUs stay out by construction, whatever directory they are emitted to.
  This replaces 078's "would leak as public headers" protection.
- No other `.cpp` exists under `_codegen/include/`. The dispatch bridge's `.cpp` is generated one level up, under
  `_codegen/`.

**The `.inl` files ship (tier ON).** Each installed `messages/<Msg>.hpp` includes its `<Msg>.builder.inl` /
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
- **with the tier OFF only:** anything under `fixpp/<ver>/messages/`, `fixpp/<ver>/groups/` or
  `fixpp/<ver>/validators/`, and `fixpp/<ver>/all.hpp` and `fixpp/<ver>/groups.hpp`.

The shipped kinds per `<ver>` in {v42, v44, v50sp2}, plus vlatest when `FIXPP_CODEGEN_FIX_LATEST=ON`:

| Kind | tier ON (default: Release) | tier OFF (default: Debug) |
|---|---|---|
| the five read-side files (`Fields.hpp`, `Messages.hpp`, `NormativeReferences.md`, `Reify.hpp`, `Validator.hpp`) | ✅ | ✅ |
| `all.hpp`, `groups.hpp` | ✅ | ❌ |
| `groups/*.hpp` | ✅ | ❌ |
| `validators/traits.hpp` | ✅ | ❌ |
| `messages/<Msg>.hpp`, `messages/<Msg>.builder.inl`, `messages/<Msg>.validator.inl` | ✅ | ❌ |

Re-derive the exact tier-ON file set from any configured tree `B`:

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

- **CPack:** no component change (P-5). `fixpp-package` depends on every non-INTERFACE member of
  `FIXPP_EXPORT_TARGETS`, so it picks the libs up from the list with the tier ON, and not with it OFF. The one CPack
  addition is D-0's cpack-time guard (`CPACK_PROJECT_CONFIG_FILE` and two `CPACK_FIXPP_*` inputs).
- **Conan:** no change. This repository defines no Conan package (P-4).
- **Wheel:** `FIXPP_INSTALL_TYPED_TIER = "OFF"` is added to `bindings/python/pyproject.toml`'s
  `[tool.scikit-build.cmake.define]`, with a comment naming this note, so the wheel build stages no typed payload
  that `wheel.exclude` would then drop (P-9). The wheel's content does not change. **Known positive:** D-0's STATUS
  line reading `#613: typed builder/validator tier OFF (FIXPP_INSTALL_TYPED_TIER=OFF, …)` in the wheel build's
  configure output, read once on the PR's run in the `python-wheel` job's log. A misspelled define would fail toward
  the old behaviour silently (`AUTO`, ON), so a log that does not show the line is **no evidence**, not a pass. That
  such a line can appear there at the default verbosity is shown by the existing
  `-- 084 FR-011: packaging DISABLED for this OTel-OFF build …` line in the same job's log (`main` tier1 run
  39983316190, job `python-wheel (build cp312-abi3)`). The wheel build tree is inside the manylinux container and is
  not readable from the run, so there is no cache-file fallback.

### D-9 — MSVC / Windows

- **Static only.** There is no shared builder lib, so no `WINDOWS_EXPORT_ALL_SYMBOLS` question arises. The
  contents witness's archive pattern `^lib/fixpp_[A-Za-z0-9_]+\.lib$` already matches `fixpp_builders_v44.lib`.
- **Debug vs Release (R-2a, R-2b).** Each tree is single-config and ships one `fixppTargets-<config>.cmake`; the
  consumer must match the configuration, as for every existing member. By default Release exports the expected pairs (D-0)
  and Debug exports none and installs none of their files; a Debug tree set ON exports its Debug libs (D-0). A Debug
  consumer that resolved to Release archives would be refused at link (LNK2038, `_ITERATOR_DEBUG_LEVEL` /
  `RuntimeLibrary`), which makes the `windows-msvc-debug` ON lane a discriminating witness that the Debug archives are
  the ones linked (§4.4).
- **PDBs.** `cmake/FixppPackaging.cmake`'s `if(MSVC)` loop sets `COMPILE_PDB_NAME` / `COMPILE_PDB_OUTPUT_DIRECTORY`
  on every STATIC/OBJECT member of `FIXPP_EXPORT_TARGETS`, and its `install(DIRECTORY lib/ FILES_MATCHING PATTERN
  "*.pdb")` ships every PDB in `lib/`. Two conditions decide whether a typed PDB exists to ship, and the note keys
  every PDB expectation on both:
  - **Packaging enabled.** The loop and the rule exist only where the root includes the module, i.e.
    `FIXPP_PACKAGING_ENABLED` (not in an OTel-OFF build, and not, under D-6, in a `FIXPP_CODEGEN_FIX_LATEST=OFF` one).
    This is pre-existing for every export member: an OTel-OFF MSVC Debug install ships no PDB for `fixpp_core`
    either. The typed tier inherits it; this PR does not move the plumbing (that would change every member's
    behaviour, out of scope).
  - **The debug-information format.** CI's MSVC legs compile Debug with `/Z7`: `tier2.yml`'s configure passes
    `"-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>"` because sccache cannot cache
    `/Zi`, and its own comment records "Under /Z7 there is no compiler PDB at all … It WOULD bite the day packaging
    is enabled on a Debug leg." Debug records are then embedded in each object of each `.lib`, which meets FR-019's
    "debuggable" without a PDB. A local tree configured from the preset alone (the shared sandbox among them: its
    `CMakeCache.txt` has no `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT` entry, and its `lib/` holds `.pdb` files) uses
    CMake's default, which under CMP0141 (NEW at `cmake_minimum_required(VERSION 3.28)`) is documented as
    `$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>`, i.e. `/Zi` and a compiler PDB. That default is CMake's
    documentation, not a probe of this repository; the sandbox's state is consistent with it.
  - Release: the libs are members, but Release emits no debug information, so no PDB is written. The contents witness
    already FATALs on a Release ZIP carrying `lib/*.pdb` (its SC-005/T061 Release arm).
  - Debug, tier OFF: the libs are not members, so where a compiler PDB exists it stays under its own object directory
    and never reaches `lib/`. That glob is still the one leak path, so the absence check (§4.1) includes `.pdb` files
    by name.
  - Debug, tier ON, packaging enabled, a PDB format (`/Zi`): the libs are members, so the loop names
    `fixpp_builders_<ver>.pdb` / `fixpp_validators_<ver>.pdb` into `lib/` and the rule installs them beside their
    `.lib`. The presence check (§4.1) requires each typed `.lib` to have its `.pdb` there. That case runs in the
    sandbox, not on CI (M-18).
  - Debug, tier ON, `/Z7` (CI's `windows-msvc-debug`): no typed PDB exists, and the check requires none. It requires
    the reverse instead, that no typed `.pdb` is staged, so a wrong reading of the format is not silent.
- **`/W4`.** No new compile of fixpp code: the libs are already built on every MSVC lane. Their own
  `.builder.cpp` / `.validator.cpp` TUs include the shipped headers and compile in-tree under
  `fixpp_apply_common_flags` (`cmake/Helpers.cmake`), which gives MSVC `/W4` (and `/WX` under `FIXPP_WERROR`) and
  GCC/Clang `-Wall -Wextra -Wpedantic`. That is the evidence that the shipped headers are warning-clean. The
  standalone consumer gets only the Conan toolchain, not the producer's flags, so C-1 is **not** a `/W4` witness.
- **Path length.** The bound is a recipe, not a figure: the longest shipped generated path (D-7's `find` recipe,
  piped through `awk '{print length($0)}' | sort -n | tail -1`) plus the length of the **deepest** stager's include
  prefix on the lane, which must stay under 260 (`MAX_PATH`). The deepest stager on windows-msvc-release is the
  export-names witness's ZIP stage,
  `<build>\_packaging_tests\export-names\packages\_CPack_Packages\win64\ZIP\<package-file-name>\include\`, not the
  `fixpp-package` target's shallower `<build>\_packages\_CPack_Packages\win64\ZIP\…\include\`. `<build>` is the
  sandbox's `C:\temp\fixpp\build\windows-msvc-release` locally, and the runner workspace's
  `build\windows-msvc-release` on CI, whose absolute prefix is the runner's workspace path. `MAX_PATH` is about the
  absolute path, so the implementation computes the recipe **once**, with the absolute CI workspace path read from a
  real tier2 run's log (not assumed), and records the inputs and the sum in the evidence file (§4.4). Nothing on the
  lane can fail on the bound: the only typed compiles are the consumer witness's, under a shallower root, and a
  CMake copy past 260 characters says nothing about a consumer's extractor or `cl.exe` include lookup. The
  `windows-msvc-debug` ON lane stages the same paths under its
  shallower `build\windows-msvc-debug\_consumer_witness\stage\include\` root. A consumer who extracts the Release ZIP
  under a root whose length plus the longest shipped path reaches 260 would hit `MAX_PATH`. That is the general
  Windows constraint, and it is not recorded as a fixpp limitation.

### D-10 — after a PASS the driver removes `stage/lib/`, and keeps the staged headers and the sub-build

v0.2 deleted all of `${FIXPP_WITNESS_WORK_DIR}` after a PASS, because every Debug, sanitizer and coverage lane would
have copied 5–11 GB of builder archives into it. v0.3 dropped the deletion: under R-2a those lanes default to no
typed tier, and the whole-directory deletion broke a reader (below). The consumer stage is one of several stagers
that copy the typed payload, and §5 aggregates them per lane; this decision covers the consumer stage only. Its
reason: on every PRESENT lane the stage holds a full copy of that tree's typed archives, which the build tree already
holds once in `lib/`, and on the Debug lane set ON (`windows-msvc-debug`, §4.4) that copy is GB-scale. So the
deletion returns, narrowed to the part that has no reader. The packaging witnesses' copies are §4.4's conditional
cleanup, not this one.

**Readers of the stage after a PASS** (re-derive with the unfiltered `git grep -n _consumer_witness`; v0.2's
"nothing else reads the stage" came from a grep restricted to build-file globs, which could not see a reader in a
`plan.md`; run 2026-10-24 on `97748d1e`, its hits include `specs/086-capi-include-isolation/plan.md`'s `clang-tidy -p`
line):
- 086 `plan.md`'s verifier step, `clang-tidy -p build/linux-clang-release/_consumer_witness/build tests/consumer/probe_*.cpp`,
  reads the compile database that 086 T053 added to the sub-build (`${FIXPP_WITNESS_WORK_DIR}/build`, the driver's
  `_sub_build`) and the staged `stage/include` it points at. It reads no archive.
- M-6 (§4.3) edits a staged archive with `ar d` and relinks C-1 by hand.
- §4.4's peak-RSS obligation rebuilds C-1, C-2a and C-2b by hand; C-1 links the staged archives.

**Rule.** As its last step, after every check and every execution has passed, the driver removes `stage/lib/`, and
nothing else: `stage/include` and the sub-build with its compile database stay. A FATAL anywhere earlier leaves the
whole stage in place for diagnosis. With the environment variable `FIXPP_WITNESS_KEEP_STAGE_LIB` set to a true value
the driver skips the removal and prints that it did; M-6 and the peak-RSS rebuild run with it. An environment
variable rather than a cache option: the hand procedures then need no reconfigure, and `ctest` passes its environment
to the test. After the removal the sub-build can neither rebuild nor reconfigure (the package config under
`stage/lib/cmake/` is gone, and CMake's generated `fixppTargets*.cmake` would refuse missing imported files anyway),
which is why those procedures set the variable first; 086's `clang-tidy -p` needs neither.

The stage is also overwritten at the start of every run (the driver's existing `file(REMOVE_RECURSE)`). The
`tier1.yml` `#254` job-end disk comment is **not** amended; its step is the Linux lane-wide instrument §4.4 reads.
The in-test disk instrument of §4.4 reads the stage before the removal.

## 3. What becomes reachable, and from where

| A consumer of an install with the tier ON (every released Release package; any self-built tree set ON)… | Links | Includes |
|---|---|---|
| builds typed messages of `<ver>` | `fixpp::builders::<ver>` | `<fixpp/<ver>/messages/<Msg>.hpp>` or `<fixpp/<ver>/all.hpp>` |
| validates typed `Args` of `<ver>` | `fixpp::validators::<ver>` | the same headers |
| inlines chosen builders/validators | the remainder's lib, or none | the same, with `FIXPP_{BUILDERS,VALIDATORS}_HEADER_ONLY[_<Msg>]` defined. The ODR rule of 078 `include-layout.md` applies unchanged: never inline a message in one TU and link it in another. |

A consumer of an install with the tier OFF (by default every Debug tree) has none of these rows: no typed target,
and no slim header to inline from. Debug packages are not released (P-11), so a Debug consumer of the typed API
builds fixpp itself with `-DFIXPP_INSTALL_TYPED_TIER=ON` (B&L L-613-3).

## 4. Test plan

No new `add_test`. Both changes extend registered tests, so the CI count pins are unchanged: `-L consumer -N` in
`tier1.yml`, `tier2.yml` and `tier3-libcxx.yml`, and `-L packaging -N` in `tier1.yml` and `tier2.yml`. A new
registration would have to bump them.

**What the expectation is keyed on (R-2b).** The two witnesses' `add_test` registrations pass both the raw setting
(`-DFIXPP_TYPED_TIER_SETTING=${FIXPP_INSTALL_TYPED_TIER}`) and the effective value
(`-DFIXPP_TYPED_TIER_EFFECTIVE=${FIXPP_INSTALL_TYPED_TIER_EFFECTIVE}`). PRESENT/ABSENT is the **effective** value,
so a self-built Debug tree set ON is checked as PRESENT. The effective value is never trusted alone; the guards below
check it against sources the mechanism does not control, so a defaulting bug cannot turn both sides green. Both
registrations are resolved after the setting (D-0); the guards do not rely on that, they check their own inputs
first. The install driver runs guards 0 and 1 **before** its `file(REMOVE_RECURSE)` and `cmake --install`, so a
defaulting bug on a Debug lane (M-10) fails before the multi-GB copy rather than after it.

0. **Domain first.** Each witness FATALs, naming the value, unless the raw setting, uppercased, is `AUTO` or one of
   D-0's boolean spellings (`ON|YES|TRUE|Y|1|OFF|NO|FALSE|N|0`), and the effective value is exactly `ON` or `OFF`.
   Empty is out of domain on both; the test is `STREQUAL ""`, not `NOT DEFINED`, because `-DX=` defines `X` as empty.
   This is what keeps the next ordering slip loud: with v0.4's placement both values were empty on a fresh Debug tree,
   guard 1 took its explicit branch, compared `''` with `''`, and the lanes whose ABSENT branch is the standing gate
   passed (round 2, Codex #1). M-21 (the slip itself) and M-22 (a value the contents gate does not forward) show
   guard 0 RED (§4.3).
1. **The default, re-derived.** If the setting is `AUTO`, the witness computes its own default (case-insensitive
   `Release` build type ⇒ ON; anything else ⇒ OFF; the install driver takes the build type from `FIXPP_BUILD_TYPE`,
   whose empty value it already treats as Debug; the contents witness from the configuration component of the
   artifact's own name, §4.2) and FATALs if it differs from the effective value. If the setting is explicit, the
   witness normalises it to a truth value (`ON|YES|TRUE|Y|1` ⇒ ON, `OFF|NO|FALSE|N|0` ⇒ OFF) and the effective value
   must equal that, so `-DFIXPP_INSTALL_TYPED_TIER=1`, `yes` or `on` is not a false RED. A wrong default in either
   direction is RED on every lane (M-10, M-13).
2. **Released packages are ON.** The refusal is at package generation (D-0): the cpack-time guard
   `cmake/FixppCPackTypedTierGuard.cmake` refuses to generate a `release` package with the tier OFF. Independently of
   it, the contents witness, which runs cpack, FATALs on any artifact whose configuration component is `release` and
   whose effective value is OFF. Deleting the cpack-time guard leaves that (M-13b). The install driver has **no**
   guard 2: a Release tree configured OFF is permitted to install, and its consumer witness must run ABSENT and pass
   (M-20); at `AUTO`, "Release resolved OFF" is already guard 1's "re-derived ON ≠ effective OFF".
3. **The ON-in-Debug lane is pinned.** `windows-msvc-debug` sets the tier ON in its preset (§4.4). A tier2 step pins
   the CMakeCache value on every MSVC leg, two-sided: `ON` on `windows-msvc-debug`, `AUTO` on the others. Without it,
   dropping the preset entry would silently turn that lane ABSENT, and green (M-16).

**What else the install driver's registration passes**, for the stage check's PDB expectation (§4.1, D-9):
`-DFIXPP_PACKAGING_ENABLED=${FIXPP_PACKAGING_ENABLED}`, `-DFIXPP_IS_MSVC=$<BOOL:${MSVC}>` (v0.8: CMake refuses
`$<CXX_COMPILER_ID:MSVC>` in an `add_test()` command, "may only be used with binary targets"), and the typed
libs' evaluated debug-information format with whether the property is set at all:
- `-DFIXPP_TYPED_DEBUG_INFO=$<TARGET_GENEX_EVAL:fixpp_builders_v44,$<TARGET_PROPERTY:fixpp_builders_v44,MSVC_DEBUG_INFORMATION_FORMAT>>`.
  A bare `$<TARGET_PROPERTY:…>` is **not** enough: it passes a genex-valued property through unevaluated, and
  tier2 sets the format as a genex. Toy probe (CMake 3.30.0, 2026-10-24, Ninja, a one-library project in the session
  scratch directory, since deleted; property evaluation does not depend on the generator or compiler): with
  `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT=$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>`, the bare form gave the genex text
  in both Debug and Release, and `TARGET_GENEX_EVAL` (and `GENEX_EVAL`) gave `Embedded` in Debug and empty in
  Release; with the variable unset, every form gave empty.
- `-DFIXPP_TYPED_DEBUG_INFO_SET=0|1`, from `get_target_property` at configure (`-NOTFOUND` ⇒ 0). An evaluated empty
  value is ambiguous without it: unset means CMake's default (a PDB format in Debug), set-but-empty means no debug
  information.

### 4.1 `fixpp::consumer::install-witness` (tests/consumer) — every lane, two-sided

The driver applies the guards above, prints one line naming the outcome (`#613 typed tier: expected
PRESENT|ABSENT (setting=<s>, effective=<e>, FIXPP_BUILD_TYPE=<t>)`), and passes `-DFIXPP_TYPED_EXPECTED=ON|OFF` to
the consumer configure. **Both branches add
required targets** to the driver's `_required_targets`, so neither branch can pass by registering nothing, and
deleting a target fails the build by name (the 086 Gate B r1 rule recorded in `run_consumer_witness.cmake`).

**Stage check, both branches (driver, after the install).** The versions are an **expected list**, never the
directories found in the stage: v42, v44 and v50sp2, plus vlatest iff `FIXPP_CODEGEN_FIX_LATEST` (passed to the
driver; keying vlatest on the option is right here, unlike in §4.2, because the consumer witness legitimately runs in
FIX_LATEST=OFF trees). This extends the driver's existing FR-008 sub-assertion, which already requires
`include/fixpp/<ver>/Messages.hpp` for v42, v44 and v50sp2. Each expected version directory must exist, and its set of
immediate children (first path component, deduplicated) must equal exactly:
- ABSENT: the five read-side files;
- PRESENT: the five read-side files plus `all.hpp`, `groups.hpp`, `messages`, `groups` and `validators`.

In the ABSENT branch, the driver also FATALs on any file under the stage's `lib/` whose name contains
`fixpp_builders_` or `fixpp_validators_` (archives and `.pdb` alike), and on any staged `lib/cmake/fixpp/fixppTargets*.cmake`
containing `fixpp::builders::` or `fixpp::validators::`.

**These checks run first.** They extend step 1 of `run_consumer_witness.cmake`, which runs right after
`cmake --install` and before "2. Configure the standalone consumer project". So a mutant that changes the staged
children, or stages a typed file in the ABSENT branch, fails on the driver's line before any consumer configure or
compile. The downstream guards (C-1's and C-2a's compiles, C-3's `#error`s, the consumer configure's ABSENT
`IMPORTED_TARGETS` regex) are each shown RED by a bypass arm that also removes the pre-empting step-1 check (§4.3,
the primed arms).

**PDB expectation, PRESENT branch (D-9).** The driver first FATALs unless `FIXPP_TYPED_DEBUG_INFO` is empty,
`ProgramDatabase`, `EditAndContinue` or `Embedded`. It then resolves the format: set ⇒ the value; unset ⇒ CMake's
documented default (`ProgramDatabase` for a Debug or RelWithDebInfo build type, none otherwise). A typed PDB is
**required** iff `FIXPP_IS_MSVC`, `FIXPP_PACKAGING_ENABLED`, and the resolved format is `ProgramDatabase` or
`EditAndContinue`: then every staged `lib/fixpp_(builders|validators)_<ver>.lib` must have its
`lib/fixpp_(builders|validators)_<ver>.pdb`. Where the resolved format is `Embedded`, no typed `.pdb` may be staged.
Otherwise nothing is checked. On CI's `windows-msvc-debug` (`/Z7`) that makes the requirement arm vacuous by design;
it is exercised in the sandbox (`/Zi`, OTel ON), once (M-18). The line the driver prints names the resolved format and
which arm applied.

#### PRESENT branch (Release lanes, and `windows-msvc-debug` set ON)

On `windows-msvc-debug` C-1 and C-2 compile in a Debug consumer against the Debug archives; MSVC refuses a
Debug-into-Release link (D-9), so a green link there is evidence that the Debug archives were the ones resolved.

**C-1 `consumer_typed_witness` (executable, link mode, RUN by the driver after `consumer_witness`):**
- **One message per version:**
  - `fixpp::v42::NewOrderSingle` and `fixpp::v44::NewOrderSingle` (the issue's message);
  - `fixpp::v50sp2::ListStatus` and `fixpp::vlatest::ListStatus` (vlatest iff `FIXPP_CODEGEN_FIX_LATEST`, below).
  - **Why not NewOrderSingle on v50sp2 and vlatest:** its default-mode include closure is 233 group-plan headers,
    about 0.88 GiB peak RSS per TU (B&L L-078-1). `ListStatus` has a closure of one plan, so it still exercises the
    installed `groups/` path. Re-derive any closure size by walking the message header's `#include "…"` lines,
    skipping the `.inl` lines.
- **For each version, three calls:**
  - `build_<Msg>` with fixed inputs. Assert the returned span **byte-for-byte equal** to a checked-in literal. The
    literal is written from these sources, read as text, never captured from a run (v0.8 adds the third):
    - the **tag order**: the emission order of that version's generated `<Msg>.builder.inl` (the emitter's order,
      not the dictionary's member order: v44 `NewOrderSingle` emits 347 before 1 and 11);
    - the **framing**: `wire::body_builder`'s output contract (`include/fixpp/wire/body_builder.hpp`'s design block:
      body only, `35=<msg_type>\x01` emitted at `commit()`, `<tag>=<value>\x01` per field, count before instances),
      as pinned by `tests/wire/test_body_builder.cpp`;
    - the **decimal text**, where a message's required fields include a decimal (vlatest ListStatus's required
      NoOrders entry does): `.specify/2a-decimal.md` §6.2's formatting rule. C-1 uses only exponent-0 values.
  - `validate_<Msg>` on the same inputs with every required field set. Assert success.
  - `validate_<Msg>` with one named required field cleared. Assert failure. The required set comes from the
    dictionary XML (`required="Y"`); for v44 NewOrderSingle it is {11, 40, 54, 60}.
  - The two validation outcomes are what prove a real `validate_` body ran, rather than a stub.
- **Links exactly the imported targets of the expected pairs (D-0), by name**: v42, v44 and v50sp2, plus vlatest iff
  `FIXPP_CODEGEN_FIX_LATEST`. No `if(TARGET …)`, and not `fixpp::fixpp`, so the by-name targets must carry their own
  closure.
- **Own include interface (D-5).** Before `add_executable`, the consumer configure checks each typed target it will
  link: first `if(NOT TARGET <t>)` ⇒ FATAL `#613: <t> is not exported`, so a missing export fails on that line rather
  than on `get_target_property`'s error for a non-existent target; then it reads the target's own
  `INTERFACE_INCLUDE_DIRECTORIES` and FATALs unless it is exactly the staged include root (both sides through
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
  and (iff `FIXPP_CODEGEN_FIX_LATEST`) vlatest `messages/ListStatus.hpp`. That reaches a large version's `.builder.inl`. The builder `.inl` includes
  only its message header and `<fixpp/wire/body_builder.hpp>`, so no `groups.hpp` is pulled.
- Neither target is linked, so 078's same-message ODR rule cannot be violated.

#### ABSENT branch (Debug lanes at the default)

**C-3 `probe_typed_absent` (compile-only OBJECT, linked to `fixpp::fixpp`, required).** It follows 086's ❌-cell
form (rule 2 in `tests/consumer/CMakeLists.txt`'s 086 block: `__has_include` plus a unique-token `#error`, asserted by
compiling):
- **A positive control per version it asserts absence for:** `#error` unless
  `__has_include(<fixpp/<ver>/Messages.hpp>)`, for v42, v44, v50sp2, and vlatest under the same
  `FIXPP_CODEGEN_FIX_LATEST` condition the consumer configure passes as a compile definition. Without it, a version
  tree that was not staged at all would make that version's absences true for the wrong reason
  (`consumer_witness.cpp` includes only `<fixpp/v44/Messages.hpp>`, so nothing else requires the other versions).
- Then, for each of those versions, `#error` naming the path if any of these resolves: `all.hpp`, `groups.hpp`,
  `validators/traits.hpp`, and one message header (`messages/NewOrderSingle.hpp` for v42/v44,
  `messages/ListStatus.hpp` for v50sp2/vlatest).

**Consumer configure, ABSENT branch.** It reads the directory's `IMPORTED_TARGETS` after `find_package(fixpp)`:
- positive control: FATAL unless `fixpp::fixpp` is in the list (a blind read reports no typed target);
- FATAL naming every entry matching `^fixpp::(builders|validators)::`. A regex rather than a list, so a version added
  later is caught too.

### 4.2 `fixpp::packaging::contents` (tests/packaging) — linux-gcc-release, windows-msvc-release and linux-clang-coverage

Changes to `tests/packaging/run_package_contents_witness.cmake`. The `fixpp::packaging::contents` registration runs
`run_package_contents_gate.cmake`, which `execute_process`es the witness with a fixed list of `-D`s, so the
registration's two new values (`FIXPP_TYPED_TIER_SETTING`, `FIXPP_TYPED_TIER_EFFECTIVE`) must also be required and
forwarded by the gate; a value the gate does not forward reaches the witness empty, which guard 0 refuses.
**PRESENT/ABSENT is the effective value passed by the build; the artifact's own name is the independent check on it**, as the FR-019 PDB block uses the name. The name
is `CPACK_PACKAGE_FILE_NAME`'s `fixpp-<version>-<platform>-<toolchain>-<config>`, optionally followed by
`-otel-off-fixture`, then the extension (`cmake/FixppPackaging.cmake`, T051). The witness **parses** the
configuration component (`^fixpp-[^-]+-[^-]+-[^-]+-([^-.]+)`) rather than matching a suffix, and FATALs if the name
does not parse. Then, in this order:
- guard 0: the raw setting and the effective value in domain (§4);
- component `release` with the effective value OFF → FATAL (guard 2: a released configuration without the tier;
  reachable only when the cpack-time guard is gone, M-13b);
- guard 1: setting `AUTO` → the default re-derived from the component (`release` ⇒ ON, anything else, `debug`,
  `relwithdebinfo` or `minsizerel` included, ⇒ OFF) must equal the effective value; an explicit setting, normalised,
  must equal it;
- effective ON → PRESENT: the slim set is expected; effective OFF → ABSENT: today's read-side-only set.

Below, "PRESENT artifacts" and "ABSENT artifacts" mean these two outcomes.

- **Must-be-absent, every artifact.** Keep `_dispatch`, `vt11`, `core/test` and `transport/test`. Add
  `^include/.*\.cpp$`. ABSENT artifacts also keep the `messages|groups|validators` and `(all|groups)\.hpp`
  alternatives, and add `^lib/.*fixpp_(builders|validators)_`.
- **Per-version immediate children, derived the same way under every generator.** A version's children are the first
  path component under `include/fixpp/<ver>/` (`^include/fixpp/<ver>/([^/]+)(/|$)`, deduplicated). The listings are
  already normalised with `string(REGEX REPLACE "/$" "" …)`, so a listed directory entry and a file path below it
  give the same component, whether or not the generator lists directories. Expected set: as in §4.1's stage check,
  PRESENT or ABSENT.
- **Shipped versions: an unconditional expected list.** `_versions_seen` is derived from the package today, so a
  dropped version passes. The expected list becomes v42, v44, v50sp2 and vlatest, **unconditionally**: R-2 rules all
  four into every package, and D-6 makes a FIX_LATEST=OFF tree unpackageable. Keying it on `FIXPP_CODEGEN_FIX_LATEST`
  would track the very regression it should catch.
- **PRESENT artifacts: two independent checks on `messages/`, `groups/` and `validators/`.**
  - **Schema allowlist** (catches a new emitted kind). Written from 078 `contracts/include-layout.md`'s generated file
    set, not from the install rule: `messages/` holds only `<Msg>.hpp`, `<Msg>.builder.inl` and
    `<Msg>.validator.inl`, and each `<Msg>.hpp` has both `.inl` siblings and vice versa; `groups/` holds only
    `[A-Za-z0-9_]+\.hpp`; `validators/` holds exactly `traits.hpp`. Any other file fails, by name.
  - **Build-tree equality** (install fidelity). The shipped files must equal the D-7 tier-ON rule applied to
    `${FIXPP_MAIN_BUILD_DIR}/_codegen/include/fixpp/<ver>`. This is the 086 block's method for the isolated roots.
    It catches a dropped file and a leaked `.cpp`. It **cannot** catch a new emitted kind, because its expectation is
    the same denylist as the rule; the schema check exists for that.
- **Archive count.** Already derived from the shipped `fixppTargets.cmake`, so it follows the new membership in
  either configuration with no edit. Its comment ("13 of the 18") becomes a condition (§6).

**Check order (v0.7).** The witness stops at its first `FATAL_ERROR` (its own comment insists on `FATAL_ERROR`, not
an appended list), so only the first failing check of a run prints. The typed-tier checks run in this order:
1. guard 0, before cpack runs (the two values need no artifact);
2. after cpack, per artifact name: the name parse, guard 2, guard 1;
3. per artifact, after the existing 084/086 checks: must-be-absent, then the unconditional version list, then the
   per-version children, then (PRESENT only) the schema allowlist, then (PRESENT only) build-tree equality.

So a mutant's contents half names the first of these that fails, and an arm whose defect only equality can see
must keep must-be-absent, the versions, the children and the schema intact (M-23, §4.3).

**The other packaging witnesses, and the Python install witness, need no edit.** Each was read for a closed
expectation on the staged or extracted tree that the typed payload would turn RED; none has one. List them with
`ls tests/packaging/run_*_witness.cmake` plus `bindings/python/run_python_install_witness.cmake`:
- **export-names** and **package-target**: derived from `FIXPP_EXPORT_TARGETS`; their regexes already admit `::`
  inside a name.
- **clean-env** and **real-client**: link only the umbrella and gain nothing to check, because the libs add no
  `find_dependency` (D-1); clean-env's `lib/cmake/fixpp/*.cmake` glob is derived.
- **provenance**: its payload digest walks every file under the extracted prefix and compares it with the stamp,
  which `cmake/FixppStampProvenance.cmake` computes over the same staged tree from the provenance `install(CODE)`
  block. Both sides are derived; nothing is checked in.
- **telemetry-provenance**: reads one stamp field.
- **version-mismatch**: `find_package` version legs only.
- **Python install witness**: matches only `fixpp.py`, `fixpp_oo.py`, `fixpp_dict_data.py`, `^_fixpp`,
  `.py`/`.pyi`/`.pyc` and SOABI `.so`/`.pyd` names; no typed file matches.

⚠️ **No CI lane uploads a Debug package, and none is released** (P-11). **linux-clang-coverage does build one on
every run:** its `ctest --preset linux-clang-coverage` has no `-LE packaging` (its own comment: "it runs the FULL
suite"), and its configure leaves `FIXPP_BUILD_OTEL` at its `ON` default, so `FIXPP_PACKAGING_ENABLED` is ON,
`tests/packaging` is registered, and this witness cpacks a `…-debug` artifact and runs its ABSENT arm. The standing
gates for the tier-OFF state are therefore this ABSENT contents arm on linux-clang-coverage and §4.1's ABSENT branch
on every Debug CI lane at the default; for tier ON in Debug, §4.1's PRESENT branch on `windows-msvc-debug`. M-14's
unmutated half is read on the PR's linux-clang-coverage run (and once locally); its M-11 half is recorded once.

### 4.3 Mutants

Each mutant is applied on the implementation branch, in a scratch commit that is then dropped. After applying it:
1. Rebuild the tree, so `cmake_install.cmake` and the export files are regenerated. A stale install script fails
   toward clean.
2. Run `ctest --preset <p> -R 'fixpp::consumer::install-witness|fixpp::packaging::contents' --output-on-failure`.
3. Record the failing line in the verify record.

Each Expected-RED cell names the **first** line that fires, in the driver's execution order (§4.1: guards 0 and 1,
then the install, then the step-1 stage checks, then the consumer configure, build and run) and, for a contents
half, in the contents witness's check order (§4.2). Where one `cmake --build` holds several failing targets, the
line that prints is whichever Ninja scheduled first; such a cell records the driver's "consumer build failed" and
proves each target's own line by a hand build of that target on the kept stage (M-2a′, M-2f). A guard pre-empted by an
earlier one is shown RED by its own primed bypass arm (M-2a′, M-2c′, M-11′, M-12′), which removes the pre-empting
check in the same scratch commit; moving the checks after the build would only make the driver's checks the unproven
ones.

| # | Mutant | Lane | Expected RED | Where it shows |
|---|---|---|---|---|
| M-1 | Delete the D-1 append (no lib exported) | gcc-release | consumer configure: `#613: fixpp::builders::v42 is not exported` (the own-include-interface check's `if(NOT TARGET)` line, which runs before any link is generated). The headers still ship, which is the incoherent package 078 feared. | install-witness ("consumer configure failed") |
| M-2a | Restore `PATTERN "messages" EXCLUDE` in the tier-ON branch | gcc-release | driver (step 1, before any consumer configure): a version's children ≠ PRESENT set, `messages` missing. Contents (first firing, §4.2's order): the per-version children check, `messages` missing. C-1's compile is pre-empted; M-2a′ shows it | both |
| M-2b | Restore `PATTERN "groups" EXCLUDE` (Release) | gcc-release | driver (step 1): children ≠ PRESENT set, `groups` missing. Contents: the children check, `groups` missing. | both |
| M-2c | Restore `PATTERN "validators" EXCLUDE` (Release) | gcc-release | driver (step 1): children ≠ PRESENT set, `validators` missing. Contents: the children check, `validators` missing. C-2a's compile, the only consumer target that reaches `validators/` (link-mode C-1 does not), is pre-empted; M-2c′ shows it | both |
| M-2d | Restore `PATTERN "all.hpp" EXCLUDE` (Release) | gcc-release | driver (step 1): children ≠ PRESENT set, `all.hpp` missing. Contents: the children check, `all.hpp` missing. | both |
| M-2e | Restore `PATTERN "groups.hpp" EXCLUDE` (Release) | gcc-release | driver (step 1): children ≠ PRESENT set, `groups.hpp` missing. Contents: the children check, `groups.hpp` missing. | both |
| M-2f | Add `PATTERN "*.inl" EXCLUDE` | gcc-release | driver: "consumer build failed", carrying a missing `<Msg>.builder.inl` (or `.validator.inl`) from C-2a or C-2b, whichever Ninja scheduled first (the driver builds every required target in one `cmake --build` without `-k 0`). Then, by hand on the kept stage, `cmake --build <work>/build --target probe_typed_inline_small` and `--target probe_typed_inline_large`, recording each target's own missing-`.inl` line. Contents (first firing): the schema allowlist, a `messages/<Msg>.hpp` without its `.inl` siblings. | both |
| M-2a′ | Bypass arm: M-2a plus, in the same scratch commit, the driver's step-1 children check removed | gcc-release | driver: "consumer build failed" (C-1, C-2a and C-2b all miss `messages/` headers, and the one `cmake --build` stops at Ninja's first failing edge, so the driver's output need not show C-1's line). Then, by hand on the kept stage (a FATAL keeps it, D-10), `cmake --build <work>/build --target consumer_typed_witness`, which prints C-1's line: `fixpp/v44/messages/NewOrderSingle.hpp` (or v42's) not found. Only the hand-build line is recorded as C-1's proof. Proves C-1 depends on the staged `messages/`. Recorded once | install-witness, then the hand build |
| M-2c′ | Bypass arm: M-2c plus the driver's step-1 children check removed | gcc-release | C-2a compile: `../validators/traits.hpp` not found; C-1 alone would stay green. Proves C-2a's sensitivity to the validators path. Recorded once | install-witness |
| M-3 | Drop one version from the append: vlatest, then v42 | gcc-release | consumer configure: `#613: fixpp::builders::<ver> is not exported` for that version | install-witness |
| M-3b | Drop only `fixpp_validators_v44` | gcc-release | consumer configure: `#613: fixpp::validators::v44 is not exported` | install-witness |
| M-4 | Remove `PATTERN "*.cpp" EXCLUDE` | gcc-release | Contents (first firing): must-be-absent lists `include/fixpp/<ver>/messages/*.builder.cpp`. The consumer stays green, correctly: a leaked TU breaks no compile. | contents |
| M-5 | Remove the D-2 loop | gcc-release | configure: T026 FATAL. Already observed by probe (P-3). | configure |
| M-6 | Spurious-HIT arm: with everything exported, delete the `NewOrderSingle.builder.cpp.o` member from the **staged** `libfixpp_builders_v44.a`. Procedure (GNU `ar`, so Linux): run the witness once to PASS with `FIXPP_WITNESS_KEEP_STAGE_LIB=1` in the environment, so the staged archives survive the PASS (D-10); `ar d <work>/stage/lib/libfixpp_builders_v44.a NewOrderSingle.builder.cpp.o`; then `cmake --build <work>/build --target consumer_typed_witness` by hand (`<work>/build` is the driver's `_sub_build`) | gcc-release | C-1 link: undefined `fixpp::v44::build_NewOrderSingle`. Proves the symbol resolves from that archive and from nothing else in the closure. | the hand build |
| M-7 | Change one byte of C-1's v44 expected literal | any Release | C-1 run: no `PASS` line. Proves the byte comparison is live. | install-witness |
| M-8a | FIX_LATEST=OFF, root D-6 decision deleted | configure only | the `cmake/FixppPackaging.cmake` guard FATALs naming `FIXPP_CODEGEN_FIX_LATEST` | configure, recorded once |
| M-8c | FIX_LATEST=OFF, unmutated | one full OFF build | D-6 configure evidence (STATUS line, no `CPackConfig.cmake`, 24 imported names, already observed by probe) plus the consumer witness's vlatest-OFF arm green | recorded once; no CI lane configures OFF |
| M-9 | Remove the `INSTALL_INTERFACE` entry from `fixpp_validators_v50sp2` only | gcc-release | consumer configure: the own-include-interface check FATALs naming `fixpp::validators::v50sp2`, although its headers would still resolve through `fixpp::wire` | install-witness |
| M-10 | Defaulting bug, Debug side: make `AUTO` resolve ON for every build type | a Debug tree at `AUTO` (linux-clang-debug) | driver guard 1, before the install: `AUTO` resolved ON but the re-derived default for `FIXPP_BUILD_TYPE=Debug` is OFF. Without guard 1 the lane would run PRESENT and pass; that is the both-sides-green case R-2b names | install-witness |
| M-11 | In the tier-OFF branch, drop only the restored `PATTERN "groups.hpp" EXCLUDE` | a Debug tree at `AUTO` | driver (step 1): `groups.hpp` among a version's children. C-3's `#error` is pre-empted; M-11′ shows it | install-witness |
| M-11′ | Bypass arm: M-11 plus the driver's step-1 children check removed | a Debug tree at `AUTO` | C-3 compile: the `#error` naming `fixpp/<ver>/groups.hpp`. Recorded once | install-witness |
| M-12 | Append `fixpp_validators_v44` to `FIXPP_EXPORT_TARGETS` outside the `if(FIXPP_INSTALL_TYPED_TIER_EFFECTIVE)` | a Debug tree at `AUTO` | driver (step 1): the ABSENT `lib/` check naming `lib/libfixpp_validators_v44.a` (or `.lib`), or its `fixppTargets*.cmake` check naming `fixpp::validators::v44`, whichever the implementation runs first. T026 stays green (the D-2 loop names it). The consumer's `IMPORTED_TARGETS` regex is pre-empted; M-12′ shows it | install-witness |
| M-12′ | Bypass arm: M-12 plus **both** ABSENT step-1 checks removed (the `lib/` name check and the `fixppTargets*.cmake` check; removing one leaves the other pre-empting) | a Debug tree at `AUTO` | consumer configure: `IMPORTED_TARGETS` lists `fixpp::validators::v44`. Recorded once | install-witness |
| M-13 | Defaulting bug, Release side: make `AUTO` resolve OFF for every build type | gcc-release | configure succeeds (the refusal is not at configure). Driver guard 1, before the install: `AUTO` resolved OFF but the re-derived default for `FIXPP_BUILD_TYPE=Release` is ON. The cpack-time guard: `cpack` refuses the `release` package with the tier OFF, in each `packaging` witness that executes cpack (D-0's recipe) on linux-gcc-release and windows-msvc-release, and in the `Build packages` step on every Release lane | install-witness, packaging tests, `Build packages` |
| M-13b | M-13 with `cmake/FixppCPackTypedTierGuard.cmake`'s FATAL also deleted | gcc-release | driver guard 1 as in M-13; contents guard 2 (`release` artifact, effective OFF). linux-clang-release, which runs no contents witness, is RED through the driver alone | install-witness, contents |
| M-16 | Delete `FIXPP_INSTALL_TYPED_TIER: ON` from the `windows-msvc-debug` preset | windows-msvc-debug: tier2 CI (fresh tree), or locally only on a **fresh** build directory or after `cmake -U FIXPP_INSTALL_TYPED_TIER`: deleting a preset entry does not remove the value an existing `CMakeCache.txt` already holds, so a reconfigured tree keeps `ON` and the arm fails toward clean | the tier2 pin step: CMakeCache holds `AUTO`, expected `ON`. The witness itself would go ABSENT and green, which is why the pin exists | tier2 pin step |
| M-16b | The other side of the pin: add `"FIXPP_INSTALL_TYPED_TIER": "ON"` to `windows-msvc-release`'s preset | windows-msvc-release: tier2 CI (fresh tree), or locally under M-16's fresh-tree / `cmake -U` precondition | the tier2 pin step: CMakeCache holds `ON`, expected `AUTO`. The witness stays PRESENT and green, which is why the pin exists | tier2 pin step |
| M-17 | Key the generated-header rule's OFF branch on the build type instead of the effective value | windows-msvc-debug (ON) | driver (step 1): a version's children ≠ PRESENT set (Debug libs exported, slim headers withheld: the incoherent install) | install-witness |
| M-18 | Remove `fixpp_validators_v44` from the PDB loop's reach (skip it in the `if(MSVC)` loop) | **the sandbox only**: `windows-msvc-debug` set ON, configured from the preset (so `/Zi`, no `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT` entry) with OTel ON (so packaging is enabled), under the sandbox lock procedure. CI's leg is `/Z7`, where no typed PDB exists to misplace | driver: `lib/fixpp_validators_v44.lib` staged without its `.pdb`, the resolved format `ProgramDatabase` named in the line | install-witness, recorded once |
| M-19 | `-DFIXPP_INSTALL_TYPED_TIER=maybe` | configure only, in a **scratch** build directory, never a preset tree and least of all the shared sandbox: the value stays in that cache, so every later configure of the tree FATALs until the entry is cleared | D-0's FATAL naming the accepted values | configure, recorded once |
| M-20 | Unmutated: the ruling as a test. A Release tree, OTel ON, configured with explicit `-DFIXPP_INSTALL_TYPED_TIER=OFF` | linux-gcc-release's settings in a scratch build directory (or the preset tree, restored with `-DFIXPP_INSTALL_TYPED_TIER=AUTO` afterwards) | configure GREEN; `fixpp::consumer::install-witness` GREEN on its ABSENT branch; `cpack --config <tree>/CPackConfig.cmake` RED with the guard's line and a non-zero exit, before any staging (D-0's toy observation, here on the real tree); then `ctest -L packaging`, recording which tests went RED on the guard's line (expected: those D-0's recipe lists as executing cpack) and which stayed green | configure, install-witness, cpack, `-L packaging`; recorded once |
| M-14 | Debug package contents: run `packaging::contents` once unmutated, then with M-11 applied | a Debug tree at `AUTO` with packaging enabled (and, unmutated, linux-clang-coverage on CI, §4.2) | unmutated GREEN on the ABSENT arm; with M-11, must-be-absent (first firing, §4.2's order) naming `include/fixpp/<ver>/groups.hpp` | contents; the M-11 half recorded once |
| M-15 | New emitted kind: after the build and before `ctest`, create `_codegen/include/fixpp/v44/messages/X.json` in the build tree | gcc-release | contents (first firing): the schema allowlist naming `include/fixpp/v44/messages/X.json`; build-tree equality, which runs after it, would stay green, correctly | contents |
| M-21 | v0.4's ordering slip: move D-0's block below `if(FIXPP_BUILD_TESTS)` | any tree; on a **fresh** build directory the raw setting is empty too | driver guard 0, before the install: `FIXPP_TYPED_TIER_EFFECTIVE` empty (a plain variable read by `add_test` before it is set), and on a fresh tree `FIXPP_TYPED_TIER_SETTING` empty as well. Proves D-0's "an ordering slip is also caught by the instrument". Recorded once | install-witness |
| M-22 | Drop `FIXPP_TYPED_TIER_SETTING` from `run_package_contents_gate.cmake`'s forwarded `-D` list (keep it required) | gcc-release | contents guard 0 naming `FIXPP_TYPED_TIER_SETTING` (not forwarded, so empty or undefined; the domain regex refuses both). Recorded once | contents |
| M-23 | Equality-only arm (v0.7): in the tier-ON header rule, exclude one v50sp2 message's three files with a REGEX scoped to that version, `REGEX "fixpp/v50sp2/messages/<Msg>\\.(hpp\|builder\\.inl\|validator\\.inl)$" EXCLUDE`, for a `<Msg>` outside every consumer closure (not `ListStatus`; v42 and v44 are reached whole by C-2a's `all.hpp`, so the message is a v50sp2 one). A `PATTERN` on the basename would match that message in every version | gcc-release | contents (first firing): build-tree equality naming the three dropped files; must-be-absent, the versions, the children and the schema pairing all hold. The driver stays green, correctly: its stage check is the children set, and no consumer target reaches the message | contents, recorded once |

**Expected-green independence check, not a mutant (was M-8b).** FIX_LATEST=OFF with only the module's D-6 guard
deleted, configure only: still no `CPackConfig.cmake` and the D-6 STATUS line, because the root decision holds. It
has no failing line to record and is not cited as a mutation kill; M-8a is the guard's behavioural proof (D-6).
Recorded once beside M-8a.

### 4.4 Lanes and first-run obligations

**Which lanes take which branch.** Re-derive from the presets and the three workflow matrices: a lane is PRESENT iff
its configure preset sets `FIXPP_INSTALL_TYPED_TIER` ON, or leaves it `AUTO` and resolves `CMAKE_BUILD_TYPE` to
`Release` (P-10's recipe); it runs the consumer witness iff its workflow runs `ctest -L consumer` or an unfiltered
`ctest`. With this PR's preset change, read against `4e8b3779`'s matrices:
- **PRESENT, default:** linux-clang-release and linux-gcc-release (tier1), windows-msvc-release (tier2).
- **PRESENT, set ON in Debug (R-2b's self-build case):** windows-msvc-debug (tier2), by a
  `"FIXPP_INSTALL_TYPED_TIER": "ON"` entry in its `CMakePresets.json` `cacheVariables`. No other preset inherits it
  (P-10). Chosen over a Linux Debug lane because it is the only lane where a Debug consumer resolving to the wrong
  configuration's archives fails the link instead of passing silently (LNK2038, D-9). Two reasons v0.4 gave are
  withdrawn, because they were read from the sandbox (`/Zi`) while the CI leg is `/Z7` (D-9):
  - "its typed archives are the smallest Debug set measured": §5's MSVC Debug figure is a `/Zi` archive set; CI's
    `/Z7` archives embed the debug records in every object and are a different quantity, **unmeasured**. The first
    run measures it (below), and the lane stays chosen on the LNK2038 reason alone;
  - "it exercises the PDB path": on CI it exercises the `Embedded` arm (no typed PDB, and none staged); the PDB arm is
    the sandbox's (M-18).
  The lane still runs ON in Debug end to end on CI: export, slim headers, a Debug consumer's compile, link and run.
- **ABSENT, default:** linux-clang-debug, linux-clang-asan, linux-clang-ubsan, linux-clang-tsan and
  linux-clang-coverage (tier1), windows-msvc-asan (tier2), the four libc++ legs (tier3).
- **The pin (guard 3).** A tier2 step after configure, on every MSVC leg, reads `FIXPP_INSTALL_TYPED_TIER` from
  `build/<preset>/CMakeCache.txt` and FATALs unless it is `ON` on windows-msvc-debug and `AUTO` on the other legs.
  It follows `tier1.yml`'s "Verify full v44 coverage preset (FR-008)" step, which reads a cache value the same way.
- ⚠️ **Disclosed narrowing.** C-1 and C-2 compile and run on gcc, clang and MSVC Release and on MSVC Debug. The
  installed typed tier is never exercised under a sanitizer, coverage or libc++. The libs themselves still are,
  in-tree, on every lane.
- **`fixpp::packaging::contents`** (label `packaging`) runs on linux-gcc-release (tier1) and windows-msvc-release
  (tier2), both PRESENT, and on linux-clang-coverage (tier1), a Debug artifact, ABSENT (§4.2).

**Stagers of the typed payload, by kind.** Enumerate with
`git grep -n -E -- '--install|CMAKE_CPACK_COMMAND' -- tests bindings` and read each hit for the copies it leaves
after a PASS (one per `cmake --install`; for a cpack run, one stage per generator under
`_CPack_Packages/<platform>/<generator>/` plus one per extraction). Kinds, at `4e8b3779`:
- **`cmake --install` stages:** `tests/consumer/run_consumer_witness.cmake` (every lane; D-10 removes its `lib/`
  after a PASS); `tests/packaging/run_clean_env_witness.cmake`, `run_real_client_witness.cmake` and
  `run_version_mismatch_witness.cmake` (one stage each, kept); `bindings/python/run_python_install_witness.cmake`
  (registered `if(FIXPP_BUILD_TESTS AND UNIX AND NOT DEFINED SKBUILD)`, `TIMEOUT 300`, on the Linux legs that build
  the bindings; its driver removes its stage on PASS).
- **cpack runs:** `run_package_contents_witness.cmake` and `run_export_names_witness.cmake` (one stage per configured
  generator plus one extraction), `run_provenance_witness.cmake` and `run_telemetry_provenance_witness.cmake` (a TGZ
  stage plus one extraction; telemetry keeps only its last). Each witness has its own `FIXPP_WORK_DIR` under
  `<build>/_packaging_tests/` and runs `file(REMOVE_RECURSE "${FIXPP_WORK_DIR}")` only as its first step, so every
  copy persists to job end. `RUN_SERIAL` bounds the concurrent peak, not the total.
- **The `fixpp-package` target** (CI's `Build packages`): one stage per generator under `<build>/_packages/`, kept.

Which lanes run which: the packaging tier runs on every leg whose `ctest` carries no `-LE packaging` and whose tree
has `FIXPP_PACKAGING_ENABLED` (re-derive: each workflow's `ctest` lines, and each lane's `FIXPP_BUILD_OTEL`), and
`fixpp-package` where the `Build packages` steps' `if:` admits the preset. Read against `4e8b3779`: the packaging
tier on linux-gcc-release and windows-msvc-release (their `Test (full tier incl. packaging)` steps) and on
linux-clang-coverage (the `coverage` job's unfiltered `ctest`, OTel at its default ON; Debug, so ABSENT, and no typed
growth); the tier3 libc++ legs also run `ctest` unfiltered, but their presets set `FIXPP_BUILD_OTEL=OFF`, so no
packaging test is registered there. `fixpp-package` on linux-gcc-release, linux-clang-release and
windows-msvc-release. windows-msvc-debug runs neither, so its only typed stager is the consumer witness. Only the
PRESENT lanes copy anything new. On the Linux Release legs
the consumer and Python stages can coexist under `ctest` parallelism.

**Install-time payload hash.** The provenance `install(CODE)` block, which `cmake/FixppPackaging.cmake` keeps as its
last install rule, includes `cmake/FixppStampProvenance.cmake`, which SHA256s every file of the staged payload. It
runs on **every** `cmake --install` and every cpack staging in a packaging-enabled tree, so each stager above pays a
full hash of the typed payload as well as its copy. Every MSVC leg configures `-DFIXPP_BUILD_OTEL=ON` (tier2's
`Configure (OTel-ON)` step has no `if:`), so on windows-msvc-debug set ON the consumer witness's stage install hashes
that tree's GB-scale `/Z7` typed archives inside `TIMEOUT 300`. A `TIMEOUT` raise below is attributed to copy plus
hash.

**In-test disk instrument (driver).** After the install and before configuring the consumer (so before D-10's
removal), the driver prints
one line: the stage's total bytes (summed with `file(GLOB_RECURSE)` + `file(SIZE)`, so it works on every platform; a
zero sum is a FATAL, because the glob then saw nothing), and the free bytes of the work directory before and after
the install where `df` is found (`df -B1 --output=avail`); elsewhere it prints `free: unavailable`. It reports; it
gates nothing.

**Implementation obligations, read from the PR's first CI run** (copying a remembered number is not allowed):
- **Disk, lane-wide.** The driver's line counts one consumer stage; on the packaging lanes that is a small part of
  the lane's new bytes (§5). The lane-wide reading is the job-end instrument:
  - **Linux:** tier1's existing `Measure disk at job end (#254)` step (`df -h` of the workspace plus `du -sh` of
    `build/<preset>`, under which every `_packaging_tests` and `_packages` directory lies). Read it on
    linux-gcc-release and linux-clang-release against `main`'s run.
  - **Windows:** tier2 has no disk step (`grep -n -i -E 'disk|df |free' .github/workflows/tier2.yml` finds none). This
    PR adds one: `Measure disk at job end (#613)`, `if: always()`, `shell: bash`, on every MSVC leg, printing the free
    space of the workspace drive, the size of `build/<preset>`, and `du` of each stager directory that exists
    (`build/<preset>/_packaging_tests/*`, `build/<preset>/_packages` and `build/<preset>/_consumer_witness/stage`),
    with #254's split (the measurement fails the leg if it cannot run; only the job-summary write is
    `continue-on-error`). `main` has no figure on any MSVC leg, because the step is new, so the growth is computed
    from one run: each copy's typed share is its size minus the same copy's size without the typed tier (§5's
    baseline: the `main` Release ZIP unpacks to 296,060,348 B). A leg whose step reports "instrument unavailable" has
    no reading, and the obligation is open.
  - Record both against §5's projection.
- **Driver line.** Read it on every lane, coverage included. ABSENT lanes: stage bytes should match a run of `main`
  taken the same way. PRESENT lanes: the consumer stage's growth should match that configuration's row in §5.
- **windows-msvc-release and windows-msvc-debug first.** windows-msvc-release carries the largest projected growth
  (§5) and had no disk instrument before this PR. windows-msvc-debug's `/Z7` archive size is unmeasured (§5): read its
  stage bytes (they replace §5's `/Zi` estimate as the lane's cost; the driver prints `free: unavailable` there, so
  free space comes from the new job-end step) and the `install-witness` elapsed time against `TIMEOUT 300`, before
  anything else.
- **Conditional cleanup, stated in advance (and disclosed in the PR body).** If any lane's job-end free space reads
  below tier1's own #254 re-open rule ("Re-open if job-end free space ever drops below ~2x the tree on any leg"), the
  PR adds post-PASS removal of the extraction directories (`extract/`, and telemetry-provenance's `x/`) and of
  `_CPack_Packages/` to the packaging witnesses. A FATAL anywhere keeps everything, and an environment keep switch in
  D-10's form skips the removal; the Python witness's remove-on-PASS is the precedent. Otherwise the reading is
  recorded and 084's witnesses are left alone.
- **Elapsed time.** Read the elapsed time of `install-witness` on every PRESENT lane, of the Python install witness on
  the two Linux Release legs (both `TIMEOUT 300`), and of each `tests/packaging` witness against its own `TIMEOUT`.
  If any uses more than half its budget, raise its `TIMEOUT` to twice the largest measured value; the cost is copy
  plus the install-time payload hash (above).
- **Compile cost of C-1, C-2a, C-2b.** Wall time from the sub-build's `.ninja_log` on each PRESENT lane, the Debug
  one included (C-1 links Debug archives there, the largest of them GB-scale). Peak RSS
  once, locally on linux-gcc-release: run the witness with `FIXPP_WITNESS_KEEP_STAGE_LIB=1` so the staged archives
  survive (D-10), then clean and rebuild each target under
  `/usr/bin/time -v cmake --build <work>/build --target <t>`, and record `Maximum resident set size`.
- **Configure probe, both build types and both Debug settings** (D-1's last bullet), recorded in the verify record.
- **Path bound, once (D-9).** The longest shipped generated path (D-7's `find` recipe through
  `awk '{print length($0)}' | sort -n | tail -1`), plus the deepest stager's include prefix on windows-msvc-release
  (the export-names ZIP stage, D-9), with `<build>` written as the **absolute** CI workspace path read from a real
  tier2 run's log. The inputs and the sum go into the evidence file; it is a reading, not a gate.

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
| `packages-linux-clang-release` | not taken for this note; read by the method above at implementation | | | |

Of the Linux `include/`, the read-side `fixpp/v50sp2` and `fixpp/vlatest` trees are already 74,946,457 and 80,999,073
bytes.

**Cost per configuration of 084 `package-layout.md` §1, after R-2a and R-2b.** Only the Release rows are released
packages (P-11). The Debug rows are what a consumer's own build set ON installs, and what a local Debug package at
`AUTO` does not.

| Configuration | Typed tier | Added archives (raw) | Added slim headers (raw) | Source / label |
|---|---|---|---|---|
| Linux gcc Release | released, ON by default | 153,453,112 (gzip `tar czf` 27,009,241) | 157,781,938 (gzip 14,012,098) | archives: `fixpp-parallel` `build/linux-gcc-release/lib`, built 2026-10-20 at `99d499df`; headers: the same tree's `_codegen/include`, regenerated at `4e8b3779` (v50sp2 75,990,603, vlatest 78,135,363) |
| Linux clang Release | released, ON by default | 95,381,890 | the generated tree is toolchain-independent; read from the artifact | **estimate**: main checkout `build/linux-clang-release/lib`, newest archive 2026-10-23, commit unrecorded |
| Windows MSVC Release | released, ON by default | 611,122,156 (`gzip -6` 84,704,020); no PDBs (Release has no `/Zi`) | read from the artifact | **estimate**: the shared sandbox's `windows-msvc-release/lib`, built 2026-10-08/14 at unrecorded commits |
| Linux clang Debug | **not released**; OFF by default, ON in a self-build | 0 at `AUTO`; ON: 4,861,863,620 summed, largest single archive (`libfixpp_validators_vlatest.a`) 1,355,449,906 | 0 at `AUTO`; ON: as Release | **estimate**: main checkout `build/linux-clang-debug/lib`, newest archive 2026-10-23, commit unrecorded |
| Linux gcc Debug | **not released**; OFF by default, ON in a self-build | 0 at `AUTO`; ON: unmeasured (no local gcc-debug tree holds builder archives) | 0 at `AUTO`; ON: as Release | — |
| Windows MSVC Debug | **not released**; OFF by default; **ON on the `windows-msvc-debug` CI lane** | 0 at `AUTO`; ON, **`/Zi` (local default)**: 2,281,281,176 of `.lib` plus 61,931,520 of compiler PDBs; ON, **`/Z7` (CI)**: unmeasured, a different quantity (debug records embedded in each object, no PDB) | 0 at `AUTO`; ON: as Release | **estimate**, `/Zi` row only: the shared sandbox's `build/windows-msvc-debug` (no `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT` in its cache, so CMake's `/Zi` default), read 2026-10-24 (the `stat` recipe above over `lib/fixpp_{builders,validators}_*.lib`, and over the eight `CMakeFiles/fixpp_{builders,validators}_*.dir/*.pdb` where the PDBs sit while the libs are not export members), newest file 2026-10-20, commit unrecorded; the lane's first run measures it (§4.4) |

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
| every Debug package (local only, never released) | unchanged at `AUTO` | unchanged at `AUTO` | 0 at `AUTO` |

**Stage costs paid by tests and package builds (§4.4), per lane.** A recipe, not a constant:
- copies per stager come from §4.4's stager enumeration, read per script (one per `cmake --install`; one per
  generator plus one per extraction for a cpack run), on the lanes that run that stager (§4.4);
- per-lane persistent growth = Σ over the `tests/packaging` stagers (copies × that configuration's typed row above:
  archives plus slim headers), plus the `fixpp-package` stages on the three lanes that build packages, plus the
  consumer stage's `include/` (D-10 removes its `lib/` after a PASS); the Python install witness contributes
  nothing (its driver removes its stage on PASS);
- the compressed artifacts come on top.

**Measured baseline, an estimate** (commit unrecorded): `du -sb` over `fixpp-parallel`'s
`/mnt/wsl/fixppbuild/build/parallel-gcc-release/_packaging_tests/*`, written 2026-10-15, gave 3,575,579,456 bytes in
15 payload copies (contents and export-names 4 each: one extraction plus the TGZ, DEB and RPM stages; provenance and
telemetry-provenance 2 each; clean-env, real-client and version-mismatch 1 each); `main-gcc-release` has the same
seven directories and shape. The shared sandbox's Windows Release tree has never run the packaging tier, so no
Windows aggregate is measured.

**Projection, an estimate** (the baseline's copy counts × §5's per-copy rows):
- **linux-gcc-release:** packaging tier 15 × 311,235,050 ≈ 4.67 GB; `fixpp-package` 3 × 311,235,050 ≈ 0.93 GB; the
  consumer stage's `include/` 157,781,938 B; ≈ **+5.8 GB** persistent. The Python witness's stage is transient.
- **windows-msvc-release** (ZIP only): packaging tier 11 copies (contents, export-names, provenance and
  telemetry-provenance 2 each; three install stages) × (611,122,156 + 157,781,938) ≈ 8.46 GB; `fixpp-package`'s one
  ZIP stage ≈ 0.77 GB; the consumer stage's `include/` 157,781,938 B; ≈ **+9.4 GB**.
- **linux-clang-release:** `fixpp-package` 3 × (95,381,890 + 157,781,938) ≈ 0.76 GB plus the consumer stage's
  `include/` 157,781,938 B; ≈ **+0.92 GB**.
- **windows-msvc-debug** (set ON): the consumer stage only. **Peak** during the witness: the unmeasured `/Z7` typed
  archives plus the slim headers (the about 2.3 GB plus PDBs figure is the sandbox's `/Zi` set and describes a local
  run of that preset). **Persistent** after a PASS: the stage's `include/` only, 157,781,938 B, because D-10 removes
  `stage/lib/`.
- **ABSENT lanes** (every other Debug, sanitizer, coverage and libc++ lane): no typed stager grows.

The PR's first run replaces the projection with the lane-wide readings (§4.4), and §4.4's conditional cleanup
applies if a reading falls below the #254 re-open rule.

Large-payload behaviour of the DEB, RPM and ZIP generators is exercised only on the Release payloads above, which the
packaging lanes build on every run.

## 6. Amendments, by name, in the same PR

Rule: a superseded decision gets a **header banner that names `.specify/613-builders-export.md`**. The text under
the banner is kept as history. A count is replaced by its condition, never by a new count (PR #325). Every edited
comment is checked with `.claude/scripts/check-comment-claims.py --root <tree> --base origin/main`. The PR also runs
`tools/check_line_citations.py --shift-audit origin/main..HEAD` (`brain-freshness.yml` gates on it): banners are
inserted lines, and the cross-bundle citer of `package-layout.md` (`specs/086…/spec.md`) is archival under the
tool's frozen-citer rule, so the run is expected to pass; any hit is repaired, not waived.

Every banner and comment that says "installed" names the condition, `FIXPP_INSTALL_TYPED_TIER` (ON by default for
Release, which are the only released packages): R-2a and R-2b are part of the fact.

### 6.1 Named by #613 item 5

**(a) `specs/078-precompiled-builder-libs/contracts/cmake-targets.md`, section "Install / export scope — DECIDED:
build-tree + in-tree only (Gate A round 1, R3)".** Insert directly under the heading:

> **⚠️ SUPERSEDED by fixpp#613 (`.specify/613-builders-export.md`).** The blocker named below, "it would open the
> tier's first export surface", was removed by 084, which built that surface. Since #613, wherever
> `FIXPP_INSTALL_TYPED_TIER` is in effect, `fixpp_builders_<ver>` and `fixpp_validators_<ver>` are members of
> `FIXPP_EXPORT_TARGETS` for v42, v44 and v50sp2, and for vlatest when `FIXPP_CODEGEN_FIX_LATEST=ON`. They are
> exported as `fixpp::builders::<ver>` / `fixpp::validators::<ver>` and linked by name; the `fixpp::fixpp` umbrella
> does not reach them. The slim typed layout is installed, except its `.cpp` translation units. The setting defaults
> to `AUTO`: in effect for a single-config Release tree, which every released package is, and not otherwise; anyone
> who builds fixpp may set it ON in any configuration (owner rulings R-2a, 2026-10-24: "Release packages only.", and
> R-2b, 2026-10-24, which put it behind the setting; Debug configurations are never released, 2026-08-03). The
> **coherence rule still holds in both states**: ON installs every `build_`/`validate_` declaration with an exported
> definition, and OFF installs neither. The rest of this section is the Gate A round 1 record. Its version list
> predates 082, which added v42.
>
> **`Args` freeze (owner ruling, 2026-10-24):** "FREEZE span-based Args as the v1.0 public shape (Option 3 stays
> deferred; a later change would be a 2.0 break)." Installing the layout makes `<Msg>Args` and `groups::G_*Args`
> public API in their span-based shape; L-078-1's compile cost is part of that shape (B&L L-613-2).

**(b) `CMakeLists.txt`, the comment above the generated-header `install(DIRECTORY "${CMAKE_BINARY_DIR}/_codegen/include/" …)`
rule.** Replace the paragraph that begins "078-precompiled-builder-libs: the split emits…" with:

```
# 078 → #613 (`.specify/613-builders-export.md`): where FIXPP_INSTALL_TYPED_TIER is in effect (AUTO = single-config
# Release, the only released packages; ON in any configuration a consumer builds itself; owner rulings R-2a/R-2b)
# the 078 split's messages/ + groups/ + validators/ layout and its all.hpp / groups.hpp aggregators SHIP. Their
# build_<Msg>/validate_<Msg> declarations resolve against fixpp_builders_<ver>/fixpp_validators_<ver>, which are
# FIXPP_EXPORT_TARGETS members on exactly the same condition, so 078's coherence rule (no installed declaration
# without an exported definition) holds because the definitions are exported. With it OFF the five 078 patterns
# stay, and the rule holds because the declarations are withheld. Superseded: 078
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
F1 export membership (typed tier exported where the setting is in effect); F2 shipped layout (slim set installed on
the same condition); F3 the imported-name rule; F4 the include interface; F5 packaging enablement (D-6 adds
`FIXPP_CODEGEN_FIX_LATEST`) and the released-package guard (D-0); F6 export-member counts; F7 v42 emission (a fact
082 changed, still false in two live docs, P-8); F8 the configuration dependence R-2a/R-2b introduce (a default
that follows the build type, and a user setting).

**Recipe** (re-run it on the implementation branch and adjudicate every hit):

```
git grep -n -i -E 'unexported|never exported|stay unexported|NO install\(\)|build-tree \+ in-tree|absent from the export set|no typed-builder API|header-export is deferred|whole contract|five-pattern|7-pattern|seven-pattern|seven patterns|EIGHT patterns|8th pattern|one of seven|must NOT be installed|diverges from the prefix-strip|14 targets|tracks FIXPP_BUILD_OTEL|13 of the 18|of the 18 export|ELEVEN measured|FOUR are|v42.{0,20}(emits none|descoped)|emits? NO builders|NO builder/validator tier' -- . ':!build'
```

**Known positive:** run on `97748d1e` (2026-10-24), it reports `tests/packaging/run_package_contents_witness.cmake`'s
"of the 18 export members, 13 are STATIC", the respelling v0.2's recipe (`13 of the 18` only) could not match. The
recipe is lexical, so it is backed by `check-comment-claims.py` and by reading each changed region.

**F6 has a respelling the recipe above misses** (Gate B round 1, Codex #1 and N-1). The count is written as a number
followed by "members" or "STATIC", in the binding `.specify/` documents as well as in comments:

```
git grep -n -i -E '[0-9]+ members|[0-9]+ STATIC|(eighteen|thirteen) (export )?members' \
  -- .specify/architecture.md .specify/api-contract.md CMakeLists.txt cmake docs tests/consumer tests/packaging \
     specs/084-packaging-cpack-export/contracts specs/078-precompiled-builder-libs/contracts
```

Known positive: run on `0ed8b197` (2026-10-24), it reports `.specify/architecture.md`'s §7.4 banner ("Delivered export
set: 18 members — 13 STATIC …") and its "The delivered export set is 18 members" list, and `.specify/api-contract.md`'s
target-kind row ("13 STATIC"). Every hit is adjudicated at the Gate B round-1 head; the adjudication is in the
parent's `613-builders-export-evidence.md`.

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
  their membership depends on the setting, and wherever it is in effect they are fail-closed like the rest.
- `specs/084-packaging-cpack-export/contracts/package-layout.md` §1, "One dev-shaped package per configuration. Each
  carries headers + static libraries + …" → still true as a model, but the typed content now differs by
  configuration: covered by that contract's banner, which also names §1.
- `specs/084-packaging-cpack-export/contracts/export-set.md`, "`fixpp_otel` is exported in every configuration" and
  the other `fixpp_otel` / `fixpp_log_otlp` rows → about OTel membership, unchanged.
- `.github/workflows/tier1.yml`, "test in EVERY configuration" → the Python witness's modes, unrelated.

**Live surfaces — amended:**

| Surface | Fact | Change |
|---|---|---|
| `CMakeLists.txt`, the `FIXPP_EXPORT_TARGETS` banner ("Membership basis: the ELEVEN measured … plus the SIX derived") | F1, F6 | Add: the builder/validator members appended after the umbrella where `FIXPP_INSTALL_TYPED_TIER` is in effect, with their probe in this note. Leave the dated 084 measurement as history. |
| `CMakeLists.txt`, the umbrella comment ("is the whole contract"; "⚠️ The umbrella is deliberately NOT the whole export set …"; "FOUR are exported so a consumer can link them BY NAME") | F1, F6, F8 | Name the builder/validator pair for every expected version (v42, v44 and v50sp2, plus vlatest iff `FIXPP_CODEGEN_FIX_LATEST`) in the BY-NAME class, under `FIXPP_INSTALL_TYPED_TIER`, with the D-3 reason; replace "FOUR are" with the condition (the by-name members are the ones the umbrella does not reach) |
| `CMakeLists.txt`, "fixpp_log_otlp is the one genuinely configuration-dependent member" | F8 | State the condition instead of "the one": `fixpp_log_otlp` depends on the OTel SDK, and the typed tier on `FIXPP_INSTALL_TYPED_TIER` (D-0) |
| `CMakeLists.txt`, the D-0 setting itself and the two `add_test` registrations (`fixpp::consumer::install-witness` here, `fixpp::packaging::contents` in `tests/packaging/CMakeLists.txt`) | F8 | The setting's comment quotes R-2a/R-2b, says why it sits before the tests block (D-0), and names the cpack-time guard; the registrations pass the raw setting and the effective value (§4), and the install witness also `FIXPP_PACKAGING_ENABLED`, `FIXPP_IS_MSVC` and the typed debug-information format with its set flag |
| `tests/packaging/run_package_contents_gate.cmake`, its required-variable list and its `execute_process` of the witness | F8 | Require and forward `FIXPP_TYPED_TIER_SETTING` and `FIXPP_TYPED_TIER_EFFECTIVE` (§4.2) |
| `CMakePresets.json`, `windows-msvc-debug` | F8 | `"FIXPP_INSTALL_TYPED_TIER": "ON"` in its `cacheVariables` (§4.4) |
| `.github/workflows/tier2.yml` | F8 | The two-sided pin step (guard 3), with a comment naming this note and the mutants that prove it (M-16, M-16b); and the job-end `Measure disk at job end (#613)` step on every MSVC leg (§4.4), whose comment names this note and mirrors #254's measure-asserts / report-does-not split |
| `bindings/python/pyproject.toml`, `[tool.scikit-build.cmake.define]` | F8 | `FIXPP_INSTALL_TYPED_TIER = "OFF"`, with a comment naming this note: the wheel stages no typed payload that `wheel.exclude` would drop, and packaging is disabled there, so no guard reads it (P-9, D-8) |
| `CMakeLists.txt`, "One target diverges from the prefix-strip rule" | F3 | State the rule: nested in-tree aliases get a nested `EXPORT_NAME`; `fixpp_dict_dispatch` by override, the typed libs by the D-2 loop |
| `CMakeLists.txt`, the `INCLUDES DESTINATION` comment ("on the 14 targets that already declare `$<INSTALL_INTERFACE:…>`") | F4 | A dated 2026-08-02 measurement: keep it as dated history and add that the typed libs declare their own since #613, rather than re-counting |
| `CMakeLists.txt`, the 084 FR-011 block ("is PACKAGING available in this configuration?") | F5 | Name the second condition (D-6) next to the OTel one |
| `CMakeLists.txt`, the generated-header rule and the `Manifest.txt` block | F2, F8 | §6.1(b) |
| `cmake/Codegen.cmake`, the library-loop comment "Build-tree + in-tree consumers only; NO install() (R3, Gate A round 1)" | F1, F8 | Replace with: export-set members installed by the top-level `install(TARGETS)` where `FIXPP_INSTALL_TYPED_TIER` is in effect since #613, build-tree only otherwise; `fixpp_dict_<ver>` stays build-tree only (084 R2) |
| `cmake/FixppPackaging.cmake` | F5 | The D-6 guard with its reason; `CPACK_FIXPP_PKG_CONFIG`, `CPACK_FIXPP_TYPED_TIER_EFFECTIVE` and `CPACK_PROJECT_CONFIG_FILE` set before `include(CPack)`, with a comment saying the released-package refusal is at generation, not configure, and citing the T062a precedent |
| `cmake/FixppCPackTypedTierGuard.cmake` (**new file**) | F5, F8 | The cpack-time released-package guard (D-0). Its header comment names `.specify/613-builders-export.md`, quotes R-2/R-2b, and says why it is a project config file (it covers every `cpack --config` run, the wrapper target's and the witnesses') and not an `install(CODE)` block (which every stage install runs) |
| `cmake/fixppConfig.cmake.in`, the "Usage" block ("That is the whole contract.") | F1, F8 | Add the by-name typed line, `target_link_libraries(app PRIVATE fixpp::builders::v44 fixpp::validators::v44)`, with "present in Release packages, and in a self-built tree configured with `FIXPP_INSTALL_TYPED_TIER=ON`" |
| `docs/src/dictionary/codegen.md`, the generated-tree listing ("NO builder/validator tier — v42 descoped"), "Precompiled Builder/Validator Libraries (078)", the typed-builders note ("descoped", "emit NO builders", "construct v42 messages via the runtime `wire::body_builder`") and the `<ver>` ∈ {v44, v50sp2, vlatest} rows | F1, F7, F8 | Installed consumers: the same names through `find_package(fixpp)`, not through `fixpp::fixpp`. Document `FIXPP_INSTALL_TYPED_TIER` (`AUTO`/`ON`/`OFF`, the default, the released-package guard) and the Debug recipe: Debug packages are not released, so build fixpp with `-DFIXPP_INSTALL_TYPED_TIER=ON`. Correct every v42 statement and the `<ver>` list (P-8) |
| `tests/consumer/CMakeLists.txt` header ("The whole contract is") | F1 | Say it is the umbrella's contract, and that the by-name members, including the typed tier where it is installed, are linked below |
| `tests/consumer/consumer_witness.cpp` header ("seven-pattern exclusion set") | F2 | "its exclusion rule" |
| `tests/packaging/run_package_contents_witness.cmake` comments ("of the 18 export members, 13 are STATIC", "13 of the 18 export members", "exact 7-pattern denylist", "078 five-pattern tail", "one of seven patterns") | F2, F6 | Conditions instead of counts |
| `.github/workflows/tier2.yml`, "FIXPP_PACKAGING_ENABLED tracks FIXPP_BUILD_OTEL" | F5 | Name both conditions |
| `spec/behaviors-and-limitations.md` | F1, F8 | The §8 delta |

**Normative entry points of merged features — forward banners** (the `brain/index.md` rule that a supersession
pointer must run forward, and #334: a reader who opens the stale file directly must be redirected there):

| Surface | Banner |
|---|---|
| `specs/084-packaging-cpack-export/spec.md`, **FR-010** ("If the final export set does reach either tree, work MUST STOP") and the edge case saying the libs "are absent from the export set by design" | Discharged by R-1 and superseded by #613 wherever `FIXPP_INSTALL_TYPED_TIER` is in effect (by default: Release); names this note and the 078 banner |
| `specs/084-packaging-cpack-export/spec.md`, **AS-5** ("the header is absent"), **FR-007** ("MUST remain outside the export set") and the "Do not export the builder/validator libraries" non-goal | An in-place clause on each line (same line count, the r1 `238378ab` form): superseded by #613 wherever `FIXPP_INSTALL_TYPED_TIER` is in effect; AS-5's clause says the slim headers are present on that condition and absent elsewhere |
| `specs/084-packaging-cpack-export/contracts/export-set.md`: the "Never exported" table, B2's pattern list, B3, and the "No typed-builder API" bullet | Banner naming this note. Under FR-010, B3's "standing obligation" is discharged by R-1, which the banner quotes and points at the 078 banner for |
| `specs/084-packaging-cpack-export/contracts/package-layout.md`, §1 "Package model" and the "7-pattern exclusion set" rows | Banner naming this note, with R-2a/R-2b: the Release and (local, never released, P-11) Debug packages of one toolchain differ in typed content at the default |
| `specs/078-precompiled-builder-libs/spec.md`, the Clarifications Q on "Install/export scope" and the "Scope (Gate A round 1): … build-tree + in-tree consumers only" paragraph | Banner naming this note and the 078 contract banner |
| `specs/078-precompiled-builder-libs/contracts/cmake-targets.md` | §6.1(a) |
| `specs/078-precompiled-builder-libs/contracts/include-layout.md`, "Generated file set" ("external … header-export is deferred"; "`v42` emits none") | Banner: installed under `FIXPP_INSTALL_TYPED_TIER` since #613 (by default: Release); v42 emitted since 082 |

**Adjudicated and left unchanged:**
- **Merged-feature records under the bannered bundles:** 084 `plan.md`, `tasks.md`, `research.md`, `data-model.md`
  and `checklists/`; 078 `plan.md`, `research.md`, `tasks.md` and `checklists/codegen-linkage.md`. The bundles' `spec.md`
  and contracts carry banners or clauses at the statements in the table above, found by the subject-keyed census in
  the evidence file (E-N4; its known positive is r1's head). Banners on tasks and checklists are optional (P3) and not added.
- **v42 "descoped" in 069, 077, 079 and 082 records**, `.specify/constitution.md`'s 082 rationale, and
  `tests/codegen/determinism_test.cpp` (whose comment already records the inversion): 082 superseded that fact, not
  this PR.
- **`.github/workflows/tier2.yml`'s `/Z7` risk comment** ("Under /Z7 there is no compiler PDB at all … It WOULD bite
  the day packaging is enabled on a Debug leg."): still true. This PR enables no `Build packages` on a Debug leg, and
  its own PDB expectation keys on the evaluated format (§4.1), so the `/Z7` Debug leg is not the case the comment warns
  about.
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

All three change when this PR merges. Each should say that the released (Release) packages carry the typed tier,
and that a Debug consumer builds fixpp with `FIXPP_INSTALL_TYPED_TIER=ON` (R-2a, R-2b).

## 7. Acceptance map

| #613 item | Planned change | Planned test / evidence |
|---|---|---|
| 1 export + names + umbrella | D-0, D-1, D-2, D-3, D-5, D-6, D-8 (wheel) | C-1 links the expected pairs (v42, v44 and v50sp2, plus vlatest iff `FIXPP_CODEGEN_FIX_LATEST`) by name with its own-include-interface check, on the Release lanes and on windows-msvc-debug set ON; C-3 and the ABSENT checks on the Debug lanes at `AUTO`; the guards of §4 (domain, default, cpack-time released-package refusal, pin); M-1, M-3, M-3b, M-5, M-9, M-10, M-12, M-12′, M-13, M-13b, M-16, M-16b, M-18 (sandbox), M-19, M-20, M-21, M-22; `packaging::export-names` (derived); the wheel build's D-0 STATUS line reading OFF, read once (D-8's known positive; a log without it is no evidence) |
| 2 slim layout, TUs out, `.inl` decided | D-0, D-7 (five patterns lifted where the tier is in effect, `*.cpp` added, `.inl` shipped) | C-1, C-2, C-3, the stage children check; `packaging::contents` schema allowlist and build-tree equality; M-2a…f, M-2a′, M-2c′, M-4, M-11, M-11′, M-14, M-15, M-17, M-23 |
| 3 out-of-tree consumer test with RED mutants | §4.1 inside the existing install witness, two-sided | the §4.3 table (M-1…M-23, with M-16b and the primed bypass arms M-2a′, M-2c′, M-11′, M-12′), each first-firing line recorded in the verify record; the M-8b independence check recorded beside M-8a, not as a mutant |
| 4 size before/after | §5 method | baseline in §5; "after" from the PR's CI artifacts by the same commands; §4.4's in-test stage line; the lane-wide job-end disk readings (tier1's #254 step, the new tier2 step) against §5's per-lane projection |
| 5 amend 078 contract + install comment | §6.1 texts | `check-comment-claims.py`; `check_line_citations.py --shift-audit`; §6.2 recipe re-run, every hit adjudicated |

## 8. B&L delta (`spec/behaviors-and-limitations.md`)

- **B-613-1.** An installed consumer of a **Release** package, or of a fixpp it built itself with the tier in
  effect (B-613-2), can build and validate typed messages for v42, v44 and v50sp2, plus vlatest iff that tree was configured with `FIXPP_CODEGEN_FIX_LATEST=ON` (every package requires it, L-613-1). It links `fixpp::builders::<ver>` and/or `fixpp::validators::<ver>` by name. `fixpp::fixpp`
  does not reach them, and a `build_<Msg>` call from an umbrella-only consumer fails at link time naming the
  function. The 078 header-only macros work in the installed tree, under 078's same-message ODR rule.
- **L-613-1.** A tree configured with `FIXPP_CODEGEN_FIX_LATEST=OFF` exports and installs no vlatest typed tier and
  defines no packaging.
- **L-613-2.** The span-based `Args` shape, L-078-1's compile cost included, is installed public API and frozen for
  v1.0 (R-1). A forward-declared or handle-based `Args` ("Option 3") is a 2.0 break.
- **L-613-3 (R-2a, R-2b).** "Debug packages are not released (2026-08-03); a Debug consumer of the typed API builds
  fixpp with FIXPP_INSTALL_TYPED_TIER=ON". Details a user needs:
  - `FIXPP_INSTALL_TYPED_TIER` is `AUTO` by default: the typed tier (the `fixpp::builders::*` /
    `fixpp::validators::*` targets and the slim headers, and with them header-only inline mode) is installed from a
    single-config Release tree only. `ON` installs it from any configuration; `OFF` withholds it. A Release tree may
    be configured, built and installed with `OFF`, but its package generation is refused (cpack exits non-zero naming
    the setting), so on such a tree the `packaging`-labelled tests that generate a package fail by design; run
    `ctest -LE packaging` there.
  - On MSVC the installed typed tier's debug information follows the build's debug-information format: with `/Zi`
    (CMake's default) and packaging enabled, each typed `.lib` ships with its `.pdb`; with `/Z7` the records are in
    the `.lib` and no PDB ships. A tree with packaging disabled (OTel OFF) installs no compiler PDB for any fixpp
    library, the typed tier included; that is pre-existing, not new.
  - A consumer must match the installed configuration, as for every fixpp library (084). On MSVC a Debug consumer
    cannot link Release libraries at all (runtime-library and iterator-debug-level mismatch, LNK2038).
  - With a multi-config generator (outside the supported Ninja-only build), install each configuration into its own
    prefix: fixpp archives carry no per-configuration suffix, so with two configurations installed into one prefix a
    single file per archive name serves both configurations' imported locations, and a consumer can link the other
    configuration's code (D-0's probe), for every fixpp library, not only the typed tier.
  - In-tree builds are unaffected: the libs are built and linkable in every configuration.
- **B-613-2.** `FIXPP_INSTALL_TYPED_TIER` (`AUTO` / `ON` / `OFF`) is a documented configure setting
  (`docs/src/dictionary/codegen.md`); an invalid value fails the configure naming the accepted ones.

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
package; Debug packages carry none of the typed tier (D-0, L-613-3). What the tests' stages cost per lane is §5's
recipe, and the consumer stage's post-PASS removal is D-10.

**R-2b (amends R-2a), quoted in the header.** Put to the owner as the L-613-3 disclosure: under R-2a a Debug consumer
of the typed API had no supported package, and on MSVC no fallback. The ruling keeps R-2a's released-package scope and
removes the dead end:
- the typed tier sits behind `FIXPP_INSTALL_TYPED_TIER`, in effect by default for Release (`AUTO`), which a released
  package cannot turn off (the cpack-time guard, `cmake/FixppCPackTypedTierGuard.cmake`, which refuses package
  generation and leaves configure, build and install of an OFF Release tree alone);
- a consumer who builds fixpp itself may set it ON in any configuration and gets a coherent install of that
  configuration's typed tier (D-0);
- Debug configurations are never released, which restates the 2026-08-03 decision `tier1.yml` records (P-11).
  Removing local Debug packaging is not part of it (§0).

The tri-state `AUTO` default instead of a BOOL is this note's design choice inside R-2b, for the two reasons D-0
gives. It does not change what the owner asked for: `-DFIXPP_INSTALL_TYPED_TIER=ON` works as a BOOL would.

The implementation records the measured "after" figures from the PR's CI artifacts (§5).

No open owner question remains. Technical questions are settled in §1–§4 or assigned there as implementation
obligations with their measurement.

## Gate A

**Round 1 applied 2026-10-24: Codex P1=0 P2=6 P3=2; Opus post-judging P1=0 P2=8 P3=10; rewrite addresses root causes
1 (instruments sharing a source with the thing they check), 2 (a rule declared untestable), 3 (cost record covering
a subset of what pays), 4 (amendments chosen by policy and one lexical sweep) and 5 (precision defects); owner ruling
R-2a applied. Reviews: research/reviews/codex_613_1_builders-export_review.md,
research/reviews/opus_613_1_builders-export_triage.md.**

**R-2b applied (owner amendment, between rounds 1 and 2), 2026-10-24, v0.4.** What it changed:
- D-0: the plain variable became the `FIXPP_INSTALL_TYPED_TIER` cache setting (`AUTO`/`ON`/`OFF`) with an effective
  value; ON in Debug and in multi-config settled by reading CMake's export behaviour and a second toy probe; a
  released-package guard in `cmake/FixppPackaging.cmake`.
- §4: PRESENT/ABSENT keys on the effective value, behind three guards (the default re-derived, released packages ON,
  the ON-in-Debug lane pinned); windows-msvc-debug runs the ON-in-Debug case; mutants M-10 and M-13 redefined as
  defaulting bugs, and M-13b and M-16 to M-19 added.
- §5's cost table, §6's banners and rows, §7, §8 (L-613-3 reworded as ruled, B-613-2 added) and §9.
- Round 1's resolutions that keyed on "Release" now key on the setting; none was reverted. The v0.3 rule that "nothing
  under `tests/` reads it" is replaced by §4's guards: the tests read the effective value and check it against
  sources the mechanism does not control.

Where each finding landed:
- **Root cause 1.** Codex #1 → §4.2 schema allowlist, M-15, §6.1(b) comment text. Codex #3 → D-6 module guard,
  §4.2 unconditional version list, M-8a/b/c. N-1 → D-10 withdrawn (R-2a removed its reason) and the unfiltered
  recipe recorded. N-9 → D-4's widened recipe with its known positive. Since R-2b the two-sided checks take their
  expectation from the effective value, and never trust it alone: §4's three guards check it against the build type,
  the artifact name and the pinned preset (M-10, M-13, M-13b, M-16).
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

The triage disagreed with no Codex finding outright. Where v0.3/v0.4 do not apply Codex's own counter-proposal, the
reason is recorded here:
- **Codex #3, nested-configure arm.** Not applied. It pays the codegen bootstrap and five generations on a packaging
  lane every run to guard one deleted line. The triage's shape costs nothing at runtime and makes the regression loud
  by construction: the `cmake/FixppPackaging.cmake` guard plus the unconditional expected version list (D-6, M-8a/b).
- **Codex #5, a new disk step in the `linux-clang-coverage` job.** Not applied. The driver's in-test line (§4.4) is
  read on every lane, coverage included, and sees the in-test peak, which a job-end step cannot. Coverage is an ABSENT
  lane at the default, so its stage does not grow.
- **Codex #6, "name every directly false document" with a marker in each.** Narrowed. Forward banners go at each
  merged bundle's normative entry points (`spec.md` and contracts); the bundles' plan, tasks, research, data-model
  and checklists stay as unmarked history, adjudicated by name in §6.2. A reader entering a bundle meets the banner at
  its spec.
- **Codex #8, a `cmake --install --config Debug/Release` coherence probe for multi-config trees.** Not applied as a
  standing test. A multi-config tree is outside Article III and no lane configures one. At `AUTO` it has no typed tier;
  set ON it is the ordinary unfiltered multi-config export, whose per-configuration behaviour D-0's second toy probe
  recorded, including the pre-existing one-prefix archive collision that applies to every fixpp library (L-613-3).

**Round 2 applied 2026-10-24: Codex P1=0 P2=3 P3=2; Opus post-judging P1=0 P2=3 P3=9; rewrite addresses root causes
1 (the mechanism was written without the configure order and the per-lane build flags it runs under), 2 (a guard
that does not check its own input's domain fails toward clean), 3 (a settled ruling applied to the prose but not to
every mechanism that encodes it) and 4 (precision defects). Reviews: research/reviews/codex_613_2_builders-export_review.md,
research/reviews/opus_613_2_builders-export_triage.md.** v0.5.

Where each finding landed:
- **Root cause 1.**
  - Codex #1 (P2) → D-0: the setting is declared after the `cmake/Codegen.cmake` include and before the tests block,
    with the reason and a re-derivation recipe.
  - N-1 (P2) → D-9's PDB bullets (two conditions, `/Z7` on CI, `/Zi` locally); §4's registration passes the
    `TARGET_GENEX_EVAL`-evaluated format plus a set flag (a toy probe showed the triage's bare `$<TARGET_PROPERTY:…>`
    passes the genex text unevaluated); §4.1's PDB expectation keyed on the resolved format, two-sided under
    `Embedded`; M-18 sandbox-only with its preconditions; §4.4's lane reasons reduced to LNK2038; §5's MSVC Debug
    row and stage-cost paragraph labelled `/Zi`, with the CI `/Z7` quantity unmeasured and read on the first run.
  - Codex #4 (P3) → D-0, D-9 and L-613-3 qualified to packaging-enabled trees, stated as pre-existing for every
    member; the PDB expectation also keys on `FIXPP_PACKAGING_ENABLED`.
- **Root cause 2.** Codex #1's empty-value bypass → §4 guard 0 (domain first, `STREQUAL ""`). N-2 → guard 1
  normalises an explicit setting; guards 0 and 1 run before the install. N-5 → §4.1's expected version list and
  C-3's per-version positive controls. N-4 → M-16's fresh-tree / `-U` precondition and M-19's scratch-directory
  precondition.
- **Root cause 3.** Codex #2 (P2) → D-0's cpack-time guard (`CPACK_PROJECT_CONFIG_FILE`,
  `cmake/FixppCPackTypedTierGuard.cmake`), with a toy observation of cpack's exit code and the real-tree check as
  M-20; driver guard 2 dropped; M-13 and M-13b re-expressed; M-20 added (the explicit-OFF Release arm); L-613-3,
  §9's R-2b bullet, D-8 and the §6.2 rows moved to the generation-time shape.
- **Root cause 4.** Codex #3 → M-8b moved out of the RED table as an expected-green independence check (D-6,
  §4.3, §7). Codex #5 → D-10 rewritten: after a PASS the driver removes `stage/lib/` and keeps `stage/include` and
  the sub-build, with `FIXPP_WITNESS_KEEP_STAGE_LIB` for M-6 and the peak-RSS rebuild. N-3 → D-0's reader count replaced by its condition. N-6 → the `if(NOT TARGET)` line
  and M-1/M-3/M-3b's expected line. N-7 → P-9's wheel-build disclosure and owner option. Also fixed in passing: M-6
  and §4.4 named the sub-build directory by its variable (`_sub_build`) instead of its path (`<work>/build`); §4.2
  and §6.2 now name `run_package_contents_gate.cmake`, which forwards a fixed `-D` list to the witness; D-0's
  cpack-reach recipe executes-cpack-scoped (a bare `CPackConfig.cmake` grep overcounts) and its
  `CPACK_PROJECT_CONFIG_FILE` recipe scoped to code.

### Round 2 — disagreements

The triage disagreed with no Codex finding. Where v0.5 does not apply a counter-proposal as written, the reason is:
- **Codex #1, a configure-time assertion before each `add_test` registration.** Not added. The witnesses' guard 0
  checks the same domain on every lane where the value is consumed, so a second check at the producer would guard
  nothing more.
- **Codex #2, a separate guard on the `fixpp-package` wrapper target.** Not added. The wrapper runs
  `cpack --config`, so the one project-config guard already covers it.
- **Codex #3, a structural assertion for the module's D-6 guard.** Not added. M-8a proves it behaviourally; the
  guard is belt-and-braces by design.
- **Codex #4, moving the PDB plumbing out of `cmake/FixppPackaging.cmake` onto the general install surface.** Not
  applied. It would change every export member's install behaviour, which is out of scope; the gap is disclosed
  as pre-existing instead.
- **Triage N-1's registration form, a bare `$<TARGET_PROPERTY:fixpp_builders_v44,MSVC_DEBUG_INFORMATION_FORMAT>`.**
  Replaced by `TARGET_GENEX_EVAL` around it, on the toy probe recorded in §4.
- **Codex #5's keep switch as a `-D` (the triage's "one `-D`").** An environment variable instead, so the hand
  procedures need no reconfigure (D-10).

**Post-exhaustion pass applied 2026-10-24 (owner: hand-edit + one Opus round): round-3 Codex P1=0 P2=1 P3=2; Opus
post-judging P1=0 P2=1 P3=6 (research/reviews/codex_613_3_builders-export_review.md,
research/reviews/opus_613_3_builders-export_triage.md); P2 four-step fix + six P3s applied.** v0.6.

Where each finding landed:
- **Root cause 1 (the cost model scoped to the mechanism the PR edits, not to every stager of the payload; recurs
  from round 1's root cause 3).**
  - Codex #1 (P2), the triage's four steps. §5's windows-msvc-release and linux-clang-release totals are larger than
    the triage's by the persistent consumer `include/` term (D-10 keeps it), which the triage counted on
    linux-gcc-release only; windows-msvc-debug is stated as peak and persistent separately:
    1. deleted, not re-stated: §5's "the one GB-scale stage this PR adds" and "v0.2's 5–11 GB … withdrawn", §4.4's
       "It is the one GB-scale stage this PR adds", D-10's "R-2b brought the reason back for one lane" (rewritten to
       scope D-10 to the consumer stage; mechanism unchanged), and §9 R-2a's "v0.2's per-lane 5–11 GB stage copies no
       longer arise, and D-10 is withdrawn with them" (same claim, and it contradicted the live D-10);
    2. §5: a per-lane aggregate as a recipe (copies per stager × the configuration's row, plus `fixpp-package`), the
       triage's `_packaging_tests` baseline labelled an estimate, and the projection labelled one; §4.4 enumerates the
       stagers by kind and derives which lanes run each from the workflows' `if:`s;
    3. §4.4: the lane-wide job-end instrument — tier1's existing #254 step on Linux, a new tier2 job-end step on every
       MSVC leg (§6.2 row) — with windows-msvc-release read first beside windows-msvc-debug;
    4. §4.4: the conditional cleanup, keyed on tier1's own "~2x the tree" re-open rule, disclosed in the PR body.
  - N-2 → D-9's path bound names the deepest stager (export-names' ZIP stage) and keeps the recipe; every figure
    derived from the dropped one went with it.
  - N-3 → §4.2 names all eight packaging witnesses and the Python witness, each with its reason; §4.4 adds the
    provenance block's install-time payload SHA256 to the install cost.
- **Root cause 2 (a settled ruling applied in the prose but not in the mechanism).** Codex #2 → the wheel's
  `FIXPP_INSTALL_TYPED_TIER = "OFF"` is made, not offered: P-9, D-8 (with its known positive and the "no line is no
  evidence" rule), a §6.2 row, §7 item 1.
- **Root cause 3 (guards whose ability to report non-zero is asserted but never shown).** N-1 → §4.1 states that the
  step-1 stage checks run first; §4.3's Expected-RED cells name the first-firing line (M-2a…e, M-11, M-12, M-17);
  bypass arms M-2a′, M-2c′, M-11′, M-12′ (M-12′ removes both ABSENT step-1 checks). N-4 → M-21, M-22, M-16b,
  cross-referenced from D-0's ordering-slip sentence and guard 0.
- **Root cause 4 (a conditional fact stated unconditionally).** Codex #3 → D-0 defines the expected pairs (the
  expected-list form of §4.1); every unconditional "eight" (D-0, D-1, D-3, D-9, C-1, the §6.2 umbrella row, §7)
  now uses it. The pattern counts (P-7, §6.1(b)) and the D-1 probe's already-conditional line are a different
  subject and stay.

### Post-exhaustion pass — disagreements

The triage narrowed Codex #1 in two places; v0.6 follows the triage:
- **Codex's mandatory post-PASS cleanup in every packaging witness.** Narrowed to the conditional fallback of §4.4.
  Those are 084's witnesses; the Linux margin is recorded large in tier1 (114 G free after the reclaim, 2026-08-08),
  Windows is unmeasured, and the fallback fires on tier1's existing re-open rule rather than an invented threshold.
- **Codex's "do not withdraw the 5–11 GB concern until measured".** Became "delete, do not re-state": the withdrawn
  sentence was true of its literal subject (Debug-lane consumer stages) but read as a lane-wide claim, so it is
  deleted and §5's per-lane recipe and projection replace it, pending the first-run readings.

**Final Opus post-exhaustion review (one round, owner-authorized), 2026-10-24: P1=0 P2=0 P3=7, converged
(research/reviews/opus_613_4_builders-export_postexhaustion_review.md).** v0.7 applies the seven P3s as records only,
before implementation. Where each landed:
- P3-1 (coverage runs the packaging tier on a Debug tree) → §4.2's title and ⚠️ paragraph, §4.4's "Which lanes run
  which" (derived from `-LE packaging` and `FIXPP_PACKAGING_ENABLED`) and its contents bullet, M-14's lane cell.
- P3-2 (contents stops at its first FATAL; equality never shown RED) → §4.2's check order; the contents halves of
  M-2a…e, M-2f, M-4, M-14 and M-15 name one first-firing check; M-23, the equality-only arm.
- P3-3 (M-2a′ may not print C-1's line) → M-2a′ and M-2f record the driver's build failure, then each target's line
  from a hand build on the kept stage; §4.3's preamble states the rule.
- P3-4 (D-9's "standing witness for the bound") → deleted; §4.4 obligation to compute the bound once with the
  absolute CI workspace path, recorded in the evidence file.
- P3-5 (no Windows baseline) → the tier2 job-end step also prints `du` of each stager directory; growth computed from
  one run.
- P3-6 (§5's formula double-counts) → Σ over the `tests/packaging` stagers only.
- P3-7 (D-8's unreachable fallback) → deleted; the known positive is the STATUS line in the `python-wheel` job's log,
  beside the existing 084 FR-011 line.
