# #613 — export the typed builder/validator libraries and install the slim typed headers

> **Status: draft v0.2, 2026-10-24. Gate A: not yet run.** v0.2 records the owner's 2026-10-24 rulings on v0.1's
> two open questions (Args freeze, cost); §9 now has no open item.
>
> Issue mode, which is the owner's ruling of 2026-10-24: this note plus issue-mode Gate A and Gate B, with no
> `specs/` bundle. Branch `613-builders-export` in `fixpp-parallel`, based on `origin/main` `4e8b3779`. Batch B66.
> Claims about code name functions, targets, files or rules, never line numbers. A measured value is labelled with
> its commit, date and command. Every count in this note is a measurement with a recipe, not a constant to copy into
> code or comments.
>
> **Owner decisions, not open for review:**
> - 2026-10-21: "export all versions for v1.0" (v42, v44, v50sp2, vlatest).
> - 2026-10-24: issue mode.
> - 2026-10-24 (R-1, was OQ-1): "FREEZE span-based Args as the v1.0 public shape (Option 3 stays deferred; a later
>   change would be a 2.0 break)." This is the explicit decision 084 FR-010 requires before an export reaches
>   `messages/` or `groups/`; §9 says where the PR records it.
> - 2026-10-24 (R-2, was OQ-2): keep "all four versions, every platform" at the measured cost (§5).

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

**Out of scope:**
- Any change to the generated code, the emitter, or the `Args` shape. That is "Option 3", deferred by R-1 (§9).
- The per-version `fixpp::dict::<ver>` INTERFACE targets. They stay unexported, as 084 research R2 decided.
- A runtime/dev component split of the package. 084 shipped one dev-shaped package per configuration.
- `research/G19-fix-fpml-iso20022/remaining-work/packaging-cpack.md` item 6. It lives in the parent repository and
  is left to the orchestrator (§6.3).

## 1. Premises checked against source

| # | Issue premise | Finding | Evidence |
|---|---|---|---|
| P-1 | The libs are "under the switch that builds them (`FIXPP_BUILD_CODEGEN_TOOL`)" | **Moot. `FIXPP_BUILD_CODEGEN_TOOL=OFF` does not configure on `origin/main`.** The export block fails closed on `fixpp_dict_dispatch`, which only `cmake/Codegen.cmake` defines. `cmake/FixppPackaging.cmake` also refuses OFF (its FR-011a guard). So an installed package without the builder libs cannot be produced, and no installed config ever has to say "builders not built". This design adds no OFF support. | Probe, 2026-10-24, `4e8b3779`: `cmake -S . -B <scratch> -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=build/linux-gcc-release/conan_toolchain.cmake -DFIXPP_BUILD_TESTS=OFF -DFIXPP_BUILD_PYTHON=OFF -DFIXPP_BUILD_CODEGEN_TOOL=OFF` → rc 1, `084 FR-007: export set names 'fixpp_dict_dispatch', which is not a target` |
| P-2 | One switch gates the libs | **The real per-version switch is `FIXPP_CODEGEN_FIX_LATEST`** (an `option()` in `cmake/Codegen.cmake`, default ON). When it is OFF, the builder/validator loop in `cmake/Codegen.cmake` skips vlatest: no `fixpp_builders_vlatest`, no `fixpp_validators_vlatest`, and the vlatest output directory is removed. `docs/src/dictionary/codegen.md` documents OFF as a developer configure. v42, v44 and v50sp2 have no such switch. | `cmake/Codegen.cmake` (the `foreach(_ver IN ITEMS v42 v44 v50sp2 vlatest)` block and the T015 stale-dir cleanup) |
| P-3 | `EXPORT_NAME`s to be chosen | **The in-tree aliases are nested: `fixpp::builders::<ver>` and `fixpp::validators::<ver>`.** The prefix-strip sweep would give `builders_v44`. T026's alias check then fails at configure, because no `fixpp::builders_v44` alias exists. An explicit nested `EXPORT_NAME` is required. The precedent is `fixpp_dict_dispatch` → `dict::dispatch`. | Probe, 2026-10-24: the planned edit with the override switched off → `084 T026: export member 'fixpp_builders_v42' resolves to imported name …` (rc 1) |
| P-4 | "a consumer of `find_package(fixpp)`, a CPack package **or Conan**" | **fixpp is not consumable through Conan.** `conanfile.py` is a consumer-side recipe: `requires`, `options`, `generators`. It has no `package()` and no `package_info()`. There are no Conan components to add. | `conanfile.py` |
| P-5 | CPack placement | **CPack has no components.** `cmake/FixppPackaging.cmake` has no `cpack_add_component` and no `CPACK_COMPONENT*`. No install rule carries `COMPONENT`. The new files land in the one package per configuration, like everything else. | `cmake/FixppPackaging.cmake`; `bindings/python/pyproject.toml`'s `wheel.exclude` comment says the same |
| P-6 | "about 150 MB uncompressed" | **True of the gcc-release archives, and incomplete as a cost.** The slim headers add about as much again. MSVC release archives are about 4× the gcc size. Debug and sanitizer archives are 5–11 GB per tree, and every lane's consumer witness stage-installs them. Numbers in §5. | §5 |
| P-7 | "lift the five exclusions" | **The rule has eight patterns.** The five are 078's (`messages`, `groups`, `validators`, `all.hpp`, `groups.hpp`). `_dispatch`, `vt11` and `Manifest.txt` stay. One pattern is added, for the `.cpp` translation units (D-7). | the generated-header `install(DIRECTORY "${CMAKE_BINARY_DIR}/_codegen/include/" …)` rule in `CMakeLists.txt` |
| P-8 | — | **Two docs are already wrong about v42.** `specs/078-precompiled-builder-libs/contracts/include-layout.md` says "`v42` emits none (deferred to #196)". `docs/src/dictionary/codegen.md` says v42 "emit[s] NO builders" and lists `<ver>` ∈ {v44, v50sp2, vlatest}. 082 (issue #196) added v42 to the emitter and to the library loop. Both are corrected in this PR (§6). | `cmake/Codegen.cmake`'s 082 T035 comment on the library loop |
| P-9 | — | **The Python wheel is unaffected.** `wheel.exclude = ["include/**", "lib/**", "share/fixpp/**"]` already drops both new file kinds. The wheel already builds the libs, because they are in `all`. | `bindings/python/pyproject.toml` |

## 2. Design decisions

### D-1 — Export membership

The libs are appended to `FIXPP_EXPORT_TARGETS` in the export block, after the umbrella and beside the
`fixpp_log_otlp` append. They follow the same fail-closed discipline:

```cmake
foreach(_fixpp_typed_ver IN ITEMS v42 v44 v50sp2)
  list(APPEND FIXPP_EXPORT_TARGETS fixpp_builders_${_fixpp_typed_ver} fixpp_validators_${_fixpp_typed_ver})
endforeach()
if(FIXPP_CODEGEN_FIX_LATEST)
  list(APPEND FIXPP_EXPORT_TARGETS fixpp_builders_vlatest fixpp_validators_vlatest)
endif()
```

- v42, v44 and v50sp2 are **unconditional**. If one is missing, the existing membership check reports it as a defect
  instead of skipping it.
- vlatest is gated on **the option that decides whether it is built (P-2)**, never on `if(TARGET …)`. With the option
  ON and the target missing, the check still fails closed.
- The append sits **inside** `if(NOT FIXPP_CODEGEN_BOOTSTRAP_RUNNING)`, like the rest of the export block.

**Measured (probe, 2026-10-24, `4e8b3779` plus this edit in a scratch copy, configure only):**
- `fixpp-export-names.txt` gains the eight nested names when the option is ON, and the six non-vlatest names when it
  is OFF.
- The generated `CMakeFiles/Export/*/fixppTargets.cmake` declares each lib as `STATIC IMPORTED`, with
  `INTERFACE_LINK_LIBRARIES "fixpp::wire"` and `INTERFACE_INCLUDE_DIRECTORIES "${_IMPORT_PREFIX}/include"`.
- The derived `find_dependency` set is unchanged: Crc32c, OpenSSL, asio, opentelemetry-cpp, pugixml, tomlplusplus.

### D-2 — `EXPORT_NAME`s

The names are `builders::<ver>` and `validators::<ver>`, so the imported names are `fixpp::builders::<ver>` and
`fixpp::validators::<ver>`. These are the in-tree aliases, the names `docs/src/dictionary/codegen.md` already tells
consumers to link, and what T026's alias check requires (P-3).

They are set by one loop over `FIXPP_EXPORT_TARGETS`, placed after the prefix-strip sweep and the `dict::dispatch`
override. The loop matches `^fixpp_(builders|validators)_(.+)$`, so a version added later is named by the same rule
and is never left out of a hand-written list.

### D-3 — The umbrella does not reach them; consumers link by name

`fixpp::fixpp` stays `INTERFACE fixpp_session` plus its include root. The eight libs join the existing
**by-name class**: today that is `fixpp::capi`, `fixpp::config_toml`, `fixpp::tap` and `fixpp::service`, in the
umbrella comment's own terms. Reasons:

- 078 FR-004 makes the consumer's opt-in "purely link-time — link builders, validators, both, or neither", and
  `codegen.md` documents exactly that link line. Folding the libs into the umbrella would remove the choice for
  installed consumers only, and leave the two consumer kinds with different contracts.
- `fixpp_session` links none of them. The umbrella comment defines the umbrella as the session closure, so reaching
  the libs would change what the umbrella means, not just what it contains.
- The headers are still reachable through the umbrella's include root: both install rules write to
  `${CMAKE_INSTALL_INCLUDEDIR}`. An umbrella-only consumer who calls `build_<Msg>` therefore gets an undefined
  symbol at link time, naming the function. That is the same behaviour an in-tree consumer gets today when it forgets
  the lib. B-613-1 states it (§8).

### D-4 — Link interface and closure

Both libs link `PUBLIC fixpp::wire` and nothing else (`cmake/Codegen.cmake`). `fixpp_wire` is already a member, so
the closure is satisfied without new members. The probe confirmed CMake's generate-time closure check passes.

What the shipped headers include from outside the generated tree was measured on a configured tree. Recipe (run in
`<build>/_codegen/include/fixpp`):

```
for v in v42 v44 v50sp2 vlatest; do
  find $v/messages $v/groups $v/validators $v/all.hpp $v/groups.hpp -type f ! -name '*.cpp' -print0 \
    | xargs -0 grep -h '^#include <fixpp'
done | sort | uniq -c
```

The result names only hand-written public headers under `include/fixpp/core/` and `include/fixpp/wire/`, which
already ship. Nothing reaches `_dispatch/` or `vt11/`, so 084's B2 holds.

### D-5 — Include interface

Each lib gains `"$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"` beside its existing `$<BUILD_INTERFACE:>`, so a
by-name consumer's include root is declared by the target it links. This is 084's B0 ("every member must have an
include interface"), and B2 is satisfied because the path is the filtered installed include directory.

⚠️ **Stated limit, the same shape as 084's SC-002 note in `tests/consumer/consumer_witness.cpp`.** `fixpp::wire`'s
install interface is the same directory, and it reaches every consumer of these libs through the `PUBLIC` edge. So
no installed-consumer test can observe whether this entry is present. The entry is recorded as a design rule, and no
witness may be cited for it.

### D-6 — `FIXPP_CODEGEN_FIX_LATEST=OFF`: no vlatest, and no package

The owner ruled that the v1.0 packages carry all four versions. A tree configured with `FIXPP_CODEGEN_FIX_LATEST=OFF`
therefore **defines no packaging**. Right after `include(cmake/Codegen.cmake)`, the root `CMakeLists.txt` sets
`FIXPP_PACKAGING_ENABLED` OFF, with a STATUS line naming the option. This follows the OTel-OFF precedent in the same
file ("defines NO packaging targets and NO CPack configuration, so it CANNOT produce an artifact").

A FATAL_ERROR is rejected. `FIXPP_PACKAGING_ENABLED` is ON in every OTel-ON configure, so a FATAL_ERROR would break
the developer configure that `codegen.md` documents.

The export and the install rules stay live in that tree, so `cmake --install` of a FIX_LATEST=OFF tree still
produces a coherent prefix. That prefix has no vlatest headers, because the directory is removed, and no vlatest
targets.

**Measured (probe, as in D-1, with `-DFIXPP_CODEGEN_FIX_LATEST=OFF`):** the STATUS line is printed, no
`CPackConfig.cmake` is generated, and the export declares 24 imported targets instead of 26.

### D-7 — Header surface and the installed-file-set rule

The generated-header install rule loses the five 078 patterns and gains **`PATTERN "*.cpp" EXCLUDE`**:

- A translation unit is never a header, so the TUs stay out by construction, whatever directory they are emitted to.
  This replaces 078's "would leak as public headers" protection.
- No other `.cpp` exists under `_codegen/include/`. The dispatch bridge's `.cpp` is generated one level up, under
  `_codegen/`.

**The `.inl` files ship.** Each installed `messages/<Msg>.hpp` includes its `<Msg>.builder.inl` /
`<Msg>.validator.inl` under `FIXPP_BUILDERS_HEADER_ONLY[_<Msg>]` / `FIXPP_VALIDATORS_HEADER_ONLY[_<Msg>]`. That macro
surface is a documented consumer contract (078 `contracts/include-layout.md` "Macro surface"; `codegen.md` "Per-message
header-only inline mode"). Withholding the `.inl` files would make an installed header name a file the package does
not ship.

**The rule.** From `${CMAKE_BINARY_DIR}/_codegen/include/` to `${CMAKE_INSTALL_INCLUDEDIR}`, every file ships except
the following:
- anything under `fixpp/_dispatch/`;
- anything under `fixpp/vt11/`;
- `Manifest.txt`;
- `*.cpp`.

For each `<ver>` in {v42, v44, v50sp2}, plus vlatest when `FIXPP_CODEGEN_FIX_LATEST=ON`, the shipped kinds are:
- the five read-side files that already ship;
- `all.hpp` and `groups.hpp`;
- `groups/*.hpp`;
- `validators/traits.hpp`;
- `messages/<Msg>.hpp`, `messages/<Msg>.builder.inl` and `messages/<Msg>.validator.inl`.

Re-derive the exact file set from any configured tree `B`:

```
cd "$B/_codegen/include" && find fixpp -type f ! -path 'fixpp/_dispatch/*' ! -path 'fixpp/vt11/*' \
  ! -name Manifest.txt ! -name '*.cpp' | sort
```

**Measured equal to what the planned rule installs** (probe, 2026-10-24, `4e8b3779` plus this edit). This used a
partial `cmake --install`: the header rules run before `install(TARGETS)`, which then fails on the unbuilt archives.
Its generated tree under `include/fixpp/v*` was `diff`ed against the recipe's output with no difference. The
contents witness (§4.2) turns this one-time equality into a standing gate.

### D-8 — CPack, Conan, wheel

- **CPack:** no change (P-5). `fixpp-package` depends on every non-INTERFACE member of `FIXPP_EXPORT_TARGETS`, so
  it picks the libs up from the list.
- **Conan:** no change (P-4).
- **Wheel:** no change (P-9).

### D-9 — MSVC / Windows

- **Static only.** There is no shared builder lib, so no `WINDOWS_EXPORT_ALL_SYMBOLS` question arises. The
  contents witness's archive pattern `^lib/fixpp_[A-Za-z0-9_]+\.lib$` already matches `fixpp_builders_v44.lib`.
- **Debug vs Release.** Each tree is single-config and ships one `fixppTargets-<config>.cmake`. Nothing new: the
  consumer must match the configuration, exactly as for every existing member. The witness already passes
  `FIXPP_BUILD_TYPE`.
- **PDBs.** `cmake/FixppPackaging.cmake`'s `if(MSVC)` loop sets `COMPILE_PDB_NAME` / `COMPILE_PDB_OUTPUT_DIRECTORY`
  on every STATIC/OBJECT member of `FIXPP_EXPORT_TARGETS`, so the eight libs get named PDBs in `lib/` with no new
  code. A Debug ZIP grows by their size (§5).
- **`/W4`.** No new compile of fixpp code: the libs are already built on every MSVC lane. Consumers compile the
  shipped headers with their own flags. The witness compiles them under the producer's flags on every MSVC lane
  (§4.5).
- **Path length.** The longest shipped generated path is 76 characters below `include/` (measured on
  `4e8b3779`'s generated tree with the recipe in D-7, piped through `awk '{print length($0)}' | sort -n | tail -1`).
  Under the sandbox's ZIP staging prefix
  `C:\temp\fixpp\build\windows-msvc-release\_packages\_CPack_Packages\win64\ZIP\fixpp-<version>-windows-msvc-release\include\`
  that is 194 characters. MSVC already writes 178-character object paths for the same files under
  `CMakeFiles\fixpp_validators_vlatest.dir\_codegen\include\`, and 260 is the limit. The windows-msvc-release
  packaging lane exercises the real staging path (§4.5). A consumer who extracts the ZIP under a root longer than
  about 180 characters would hit `MAX_PATH`. That is the general Windows constraint, and it is not recorded as a
  fixpp limitation.

### D-10 — The consumer witness removes its stage after a PASS

Every lane runs `fixpp::consumer::install-witness`, which stage-installs the whole package. Debug, sanitizer and
coverage builder archives are 5–11 GB per tree (§5), so this feature turns each stage into a multi-GB copy that
persists in the build tree until the next run.

The driver therefore deletes `${FIXPP_WITNESS_WORK_DIR}` after it prints its OK line. A failing run keeps
everything for diagnosis.

Nothing else reads the stage after the test ends. Checked with:

```
grep -rn '_consumer_witness' --include='*.cmake' --include=CMakeLists.txt --include='*.yml' --include='*.sh' .
```

Every hit is the registration or the witness's own comparator, `compare_system_includes.cmake`, which runs inside the
witness.

⚠️ **Disclosed consequence.** The job-end `Measure disk at job end (#254)` step no longer sees the stage, although
the stage is still the in-test peak. The tier1 comment that names the consumer witness as part of that high-water
mark is amended to say so (§6).

**Rejected alternative: a separate typed export file or install component that the Debug lanes skip.** Shipping
`fixppTypedTargets.cmake` as an optional `include()` would let a package that lacks the typed tier configure
cleanly. That is the silent-omission shape 084's fail-closed membership rule exists to prevent. The copy cost is
bounded by D-10 instead.

## 3. What becomes reachable, and from where

| An installed consumer… | Links | Includes |
|---|---|---|
| builds typed messages of `<ver>` | `fixpp::builders::<ver>` | `<fixpp/<ver>/messages/<Msg>.hpp>` or `<fixpp/<ver>/all.hpp>` |
| validates typed `Args` of `<ver>` | `fixpp::validators::<ver>` | the same headers |
| inlines chosen builders/validators | the remainder's lib, or none | the same, with `FIXPP_{BUILDERS,VALIDATORS}_HEADER_ONLY[_<Msg>]` defined. The ODR rule of 078 `include-layout.md` applies unchanged: never inline a message in one TU and link it in another. |

## 4. Test plan

No new `add_test`. Both changes extend registered tests, so the CI count pins are unchanged: `-L consumer -N` in
`tier1.yml`, `tier2.yml` and `tier3-libcxx.yml`, and `-L packaging -N` in `tier1.yml` and `tier2.yml`. A new
registration would have to bump them.

### 4.1 `fixpp::consumer::install-witness` (tests/consumer) — every lane

New targets go in `tests/consumer/CMakeLists.txt`. Each is added to the driver's `_required_targets`, so deleting one
fails the build by name (the 086 Gate B r1 rule recorded in `run_consumer_witness.cmake`).

**C-1 `consumer_typed_witness` (executable, link mode, RUN by the driver after `consumer_witness`):**
- **One message per version:**
  - `fixpp::v42::NewOrderSingle` and `fixpp::v44::NewOrderSingle` (the issue's message);
  - `fixpp::v50sp2::ListStatus` and `fixpp::vlatest::ListStatus`.
  - **Why not NewOrderSingle on v50sp2 and vlatest:** its default-mode include closure is 233 group-plan headers,
    about 0.88 GiB peak RSS per TU (B&L L-078-1). That is too much for a TU compiled on every lane while ctest runs
    `-j4`. `ListStatus` has a closure of one plan, so it still exercises the installed `groups/` path. Re-derive any
    closure size by walking the message header's `#include "…"` lines, skipping the `.inl` lines.
- **For each version, three calls:**
  - `build_<Msg>` with fixed inputs. Assert the returned span **byte-for-byte equal** to a checked-in literal. The
    literal is written from the emission order of that version's generated `<Msg>.builder.inl`, read as text. This
    is the emitter's order, not the dictionary's member order: v44 `NewOrderSingle` emits 347 before 1 and 11. The
    literal is never captured from a run.
  - `validate_<Msg>` on the same inputs with every required field set. Assert success.
  - `validate_<Msg>` with one named required field cleared. Assert failure. The required set comes from the
    dictionary XML (`required="Y"`); for v44 NewOrderSingle it is {11, 40, 54, 60}.
  - The two validation outcomes are what prove a real `validate_` body ran, rather than a stub.
- **Links exactly the eight imported libs, by name.** No `if(TARGET …)`, and not `fixpp::fixpp`, so the by-name
  targets must carry their own closure.
- **vlatest is two-sided.** `add_test` passes `FIXPP_CODEGEN_FIX_LATEST` to the driver, and the driver passes it to
  the consumer configure.
  - ON: the consumer links the vlatest pair and compiles the vlatest cells.
  - OFF: the consumer FATALs if `fixpp::builders::vlatest` or `fixpp::validators::vlatest` is defined, and the
    driver FATALs if `include/fixpp/vlatest` exists in the stage.
- **The TU `#error`s if any `FIXPP_*_HEADER_ONLY*` macro reaches it.** Without this guard, inline mode would satisfy
  the link without the archive (the spurious-HIT path; see 4.3 M-6).

**C-2: two compile-only OBJECT targets, never linked, both in `_required_targets`.** They must be **separate
targets**. The whole-TU macros carry the same name in every version, and the generated guard is
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

### 4.2 `fixpp::packaging::contents` (tests/packaging) — linux-gcc-release and windows-msvc-release

Changes to `tests/packaging/run_package_contents_witness.cmake`:

- **Must-be-absent.** Drop the `messages|groups|validators` and `(all|groups)\.hpp` alternatives. Keep `_dispatch`,
  `vt11`, `core/test` and `transport/test`. Add `^include/.*\.cpp$`.
- **Per-version immediate children.** The expected set becomes the five read-side files plus `all.hpp`,
  `groups.hpp`, `messages`, `groups` and `validators`.
  - ⚠️ `dpkg-deb -c` and `tar tf` list directory entries, and `rpm -qlp` may too, so the three directory names can
    appear as children. The implementation must make the expected set identical for every generator. Either filter
    directory entries consistently, or list the three names. Measure that on all three Linux generators and the ZIP
    before choosing.
- **Shipped versions are two-sided.** `_versions_seen` is derived from the package today, so a dropped version
  passes. The expected list is passed in instead: v42, v44, v50sp2, and vlatest iff `FIXPP_CODEGEN_FIX_LATEST`.
- **Slim set by set equality, not by census.** For each version, the shipped files under `messages/`, `groups/` and
  `validators/` must equal the D-7 rule applied to `${FIXPP_MAIN_BUILD_DIR}/_codegen/include/fixpp/<ver>`. This is
  the 086 block's method for the isolated roots. It is an install-fidelity check: a dropped `.inl` turns it RED,
  and so does a leaked `.cpp` or a new emitted kind.
- **Archive count.** Already derived from the shipped `fixppTargets.cmake`, so it follows the new membership with no
  edit. Its comment ("13 of the 18") becomes a condition (§6).

`fixpp::packaging::export-names` and `fixpp::packaging::package-target` are derived from `FIXPP_EXPORT_TARGETS` and
need no edit. Their regexes already admit `::` inside a name. `clean-env` and `real-client` link only the umbrella
and gain nothing to check: the libs add no `find_dependency` (D-1).

### 4.3 Mutants

Each mutant is applied on the implementation branch, in a scratch commit that is then dropped. After applying it:
1. Rebuild the tree, so `cmake_install.cmake` and the export files are regenerated. A stale install script fails
   toward clean.
2. Run `ctest --preset <p> -R 'fixpp::consumer::install-witness|fixpp::packaging::contents' --output-on-failure`.
3. Record the failing line in the verify record.

| # | Mutant | Expected RED | Where it shows |
|---|---|---|---|
| M-1 | Delete the D-1 append (no lib exported) | consumer configure: CMP0028 — `consumer_typed_witness` links `fixpp::builders::v42` "but the target was not found". The headers still ship, which is the incoherent package 078 feared. | install-witness ("consumer configure failed") |
| M-2a | Restore `PATTERN "messages" EXCLUDE` | C-1 compile: `fixpp/v44/messages/NewOrderSingle.hpp` not found. Contents: slim set ≠ rule. | both |
| M-2b | Restore `PATTERN "groups" EXCLUDE` | C-1 compile: a `../groups/G_*Args.hpp` not found (v44 NewOrderSingle; v50sp2/vlatest ListStatus). Contents RED. | both |
| M-2c | Restore `PATTERN "validators" EXCLUDE` | C-2a compile: `../validators/traits.hpp` not found. Link-mode C-1 stays green, which is why C-2a exists. Contents RED. | both |
| M-2d | Restore `PATTERN "all.hpp" EXCLUDE` | C-2a compile: `fixpp/v42/all.hpp` not found. Contents RED (immediate children). | both |
| M-2e | Restore `PATTERN "groups.hpp" EXCLUDE` | C-2a compile: `../groups.hpp` (from `traits.hpp`) not found. Contents RED. | both |
| M-2f | Add `PATTERN "*.inl" EXCLUDE` | C-2a and C-2b compile: `<Msg>.builder.inl` not found. Contents RED. | both |
| M-3 | Drop one version from the append: vlatest, then v42 | consumer configure: CMP0028 on that version's targets | install-witness |
| M-3b | Drop only `fixpp_validators_v44` | consumer configure: CMP0028 on `fixpp::validators::v44` | install-witness |
| M-4 | Remove `PATTERN "*.cpp" EXCLUDE` | Contents: must-be-absent lists `include/fixpp/<ver>/messages/*.builder.cpp`. The consumer stays green, correctly: a leaked TU breaks no compile. | contents |
| M-5 | Remove the D-2 loop | configure: T026 FATAL. Already observed by probe (P-3). | configure |
| M-6 | Spurious-HIT arm: with everything exported, delete the `NewOrderSingle.builder.cpp.o` member from the **staged** `libfixpp_builders_v44.a` (`ar d`) between the driver's install and build steps (a one-time hand run) | C-1 link: undefined `fixpp::v44::build_NewOrderSingle`. Proves the symbol resolves from that archive and from nothing else in the closure. | install-witness |
| M-7 | Change one byte of C-1's v44 expected literal | C-1 run: no `PASS` line. Proves the byte comparison is live. | install-witness |
| M-8 | `FIXPP_CODEGEN_FIX_LATEST=OFF` arm | Configure-level evidence: STATUS line, no `CPackConfig.cmake`, 24 imported names. Already observed by probe (D-6). The consumer-witness OFF arm needs a full FIX_LATEST=OFF build. It is demonstrated once and recorded, because no CI lane configures OFF. | configure + one recorded run |

M-1, M-2a…f, M-3, M-3b and M-4 run on `linux-gcc-release`, the packaging lane. M-6 and M-7 can run on any lane.

### 4.4 Lanes

- **`fixpp::consumer::install-witness`** (label `consumer`) runs on all six tier1 lanes, the three tier2 MSVC lanes
  and tier3 libc++. C-1 and C-2 therefore compile, link and run on every toolchain, MSVC included.
- **`fixpp::packaging::contents`** (label `packaging`) runs on `linux-gcc-release` (tier1) and
  `windows-msvc-release` (tier2).

**Implementation obligations, read from the PR's first CI run** (copying a remembered number is not allowed):
- **Elapsed time.** Read `install-witness`'s elapsed time on every lane against its `TIMEOUT 300`. The stage copy
  grows by the archive sizes in §5, the largest on `linux-clang-coverage`. If any lane uses more than half the
  budget, raise `TIMEOUT` to twice the largest measured value.
- **Disk.** Read the `Measure disk at job end (#254)` line on every lane. At `45a620f4` (run 39983316190) the
  smallest free figure among asan, tsan, ubsan, gcc-release and clang-debug was 58 G, on ubsan. Coverage's line was
  not parsed.

## 5. Size: method, baseline, after

**Method.** The package is what CI ships: the `packages-linux-gcc-release` artifact (tier1) and the
`packages-windows-msvc-release` artifact (tier2). Fetch both with
`gh run download <run> -n <artifact> -D <empty dir>`. Record per file: `stat -c %s`, then the unpacked
`du -sb` split into `include/`, `lib/` and `share/`.
- **Before:** `main`'s artifacts.
- **After:** the same artifacts from the implementation PR's run, measured the same way. The PR records both.

**Baseline.** Measured 2026-10-24 from the push:main runs of `45a620f4`. The only commits between `45a620f4` and
`4e8b3779` are two documentation commits that install nothing (`git diff --stat 45a620f4 4e8b3779`).

| Artifact | Bytes | Unpacked | of which `include/` | `lib/` |
|---|---|---|---|---|
| `fixpp-0.0.1-linux-gnu-release.tar.gz` (tier1 run 39983316190) | 19,421,915 | 211,861,983 | 166,356,961 | 34,832,618 |
| `fixpp-0.0.1-linux-gnu-release.deb` | 19,421,478 | — | — | — |
| `fixpp-0.0.1-linux-gnu-release.rpm` | 18,467,978 | — | — | — |
| `fixpp-0.0.1-windows-msvc-release.zip` (tier2 run 39983316200) | 21,166,138 | 296,060,348 | 166,385,774 | 118,951,897 |

Of the Linux `include/`, the read-side `fixpp/v50sp2` and `fixpp/vlatest` trees are already 74,946,457 and 80,999,073
bytes.

**Projected growth (constructed, not cpack; the PR's CI artifacts are the real "after").**

| Added payload | Raw | gzip (`tar czf`) | Source |
|---|---|---|---|
| gcc-release archives, the 8 libs | 153,453,112 | 27,009,241 | `fixpp-parallel` `build/linux-gcc-release/lib`, built 2026-10-20 at `99d499df` |
| slim headers (the D-7 rule minus what already ships) | 157,781,938 | 14,012,098 | the same tree's `_codegen/include`, regenerated at `4e8b3779`; v50sp2 75,990,603, vlatest 78,135,363 |
| MSVC-release archives, the 8 libs | 611,122,156 | 84,704,020 (`gzip -6`) | the shared sandbox's `windows-msvc-release/lib`, built 2026-10-08/14 at unrecorded commits (approximate) |

About the gcc archives: `git diff 99d499df 4e8b3779` over the builder inputs (`tools/codegen`, `dictionaries`,
`include/fixpp/{core,wire}`, `cmake/`, `CMakeLists.txt`, presets, Conan profiles) changes only three things:
SPDX comment lines in core headers, `cmake/Helpers.cmake`'s fuzz-only instrumentation, and two lines of
`cmake/FixppPackaging.cmake`. None of these reaches a builder object in a non-fuzz build.

Expected order of growth:

| Package | Before (compressed) | After, projected | Unpacked growth |
|---|---|---|---|
| Linux TGZ | 19.4 MB | about 60 MB | +0.31 GB |
| Windows release ZIP | 21.2 MB | about 120 MB | +0.77 GB |

**Not shipped, but paid by every lane's consumer-witness stage (D-10).** These are the summed builder/validator
archive sizes in the existing trees of the main checkout, read with `stat`. Each tree's date and commit vary:

| Tree | Summed archives |
|---|---|
| linux-clang-release | 95 MB |
| linux-gcc-release | 153 MB |
| linux-clang-debug | 4.9 GB |
| linux-clang-asan | 5.5 GB |
| linux-clang-ubsan | 6.9 GB |
| linux-clang-coverage | 10.9 GB |
| MSVC Debug (sandbox) | 2.28 GB of `.lib`, before PDBs |

## 6. Amendments, by name, in the same PR

Rule: a superseded decision gets a **header banner that names `.specify/613-builders-export.md`**. The text under
the banner is kept as history. A count is replaced by its condition, never by a new count (PR #325). Every edited
comment is checked with `.claude/scripts/check-comment-claims.py --root <tree> --base origin/main`.

### 6.1 Named by #613 item 5

**(a) `specs/078-precompiled-builder-libs/contracts/cmake-targets.md`, section "Install / export scope — DECIDED:
build-tree + in-tree only (Gate A round 1, R3)".** Insert directly under the heading:

> **⚠️ SUPERSEDED by fixpp#613 (`.specify/613-builders-export.md`).** The blocker named below, "it would open the
> tier's first export surface", was removed by 084, which built that surface. Since #613, `fixpp_builders_<ver>` and
> `fixpp_validators_<ver>` are members of `FIXPP_EXPORT_TARGETS` for v42, v44 and v50sp2, and for vlatest when
> `FIXPP_CODEGEN_FIX_LATEST=ON`. They are exported as `fixpp::builders::<ver>` / `fixpp::validators::<ver>` and
> linked by name; the `fixpp::fixpp` umbrella does not reach them. The slim typed layout is installed, except its
> `.cpp` translation units. The **coherence rule still holds, through the other branch**: every installed
> `build_`/`validate_` declaration now has an exported definition. The rest of this section is the Gate A round 1
> record. Its version list predates 082, which added v42.
>
> **`Args` freeze (owner ruling, 2026-10-24):** "FREEZE span-based Args as the v1.0 public shape (Option 3 stays
> deferred; a later change would be a 2.0 break)." Installing the layout makes `<Msg>Args` and `groups::G_*Args`
> public API in their span-based shape; L-078-1's compile cost is part of that shape (B&L L-613-2).

**(b) `CMakeLists.txt`, the comment above the generated-header `install(DIRECTORY "${CMAKE_BINARY_DIR}/_codegen/include/" …)`
rule.** Replace the paragraph that begins "078-precompiled-builder-libs: the split emits…" with:

```
# 078 → #613 (`.specify/613-builders-export.md`): the 078 split's messages/ + groups/ + validators/ layout and its
# all.hpp / groups.hpp aggregators SHIP. Their build_<Msg>/validate_<Msg> declarations resolve against
# fixpp_builders_<ver>/fixpp_validators_<ver>, which are FIXPP_EXPORT_TARGETS members (see that list), so 078's
# coherence rule (no installed declaration without an exported definition) holds because the definitions are
# exported, not because the declarations are withheld. Superseded: 078 contracts/cmake-targets.md "Install / export
# scope". The .builder.inl/.validator.inl files ship: they are the FIXPP_{BUILDERS,VALIDATORS}_HEADER_ONLY[_<Msg>]
# surface the installed .hpp files include. Translation units never ship (*.cpp below).
# This rule is a DENYLIST: a new emitted kind ships by default. The allowlist that catches one is
# tests/packaging/run_package_contents_witness.cmake's per-version exact-set check.
```

In the `Manifest.txt` block, delete the two sentences that count the patterns ("the denylist is now EIGHT patterns,
not seven. Every 'the 7-pattern denylist' phrase…"). Keep the reason the pattern exists.

### 6.2 Further surfaces that become false (found by the recipe below)

| Surface | Change |
|---|---|
| `CMakeLists.txt`, the `FIXPP_EXPORT_TARGETS` banner ("Membership basis: the ELEVEN measured … plus the SIX derived") | Add: the builder/validator members appended after the umbrella, with their probe in this note |
| `CMakeLists.txt`, the umbrella comment ("⚠️ The umbrella is deliberately NOT the whole export set …") | Name the eight libs in the BY-NAME class, with the D-3 reason, and drop the "FOUR are" count |
| `cmake/Codegen.cmake`, the library-loop comment "Build-tree + in-tree consumers only; NO install() (R3, Gate A round 1)" | Replace with a line saying they are export-set members installed by the top-level `install(TARGETS)` since #613, while `fixpp_dict_<ver>` stays build-tree only (084 R2) |
| `cmake/fixppConfig.cmake.in`, the "Usage" block ("That is the whole contract.") | Add the by-name typed line: `target_link_libraries(app PRIVATE fixpp::builders::v44 fixpp::validators::v44)` |
| `specs/084-packaging-cpack-export/contracts/export-set.md`: the "Never exported" table, B2's pattern list and B3 | Banner naming this note. Under FR-010, B3's "standing obligation" is discharged by R-1, which the banner quotes and points at the 078 banner for |
| `specs/084-packaging-cpack-export/contracts/package-layout.md`, the "7-pattern exclusion set" rows | Banner naming this note |
| `specs/078-precompiled-builder-libs/contracts/include-layout.md`, "Generated file set" ("external … header-export is deferred"; "`v42` emits none") | Banner: installed since #613; v42 emitted since 082 |
| `docs/src/dictionary/codegen.md`, "Precompiled Builder/Validator Libraries (078)" and the typed-builders note | Installed consumers: the same names through `find_package(fixpp)`, not through `fixpp::fixpp`. Correct the v42 statements and the `<ver>` list (P-8) |
| `tests/packaging/run_package_contents_witness.cmake` comments ("exact 7-pattern denylist", "078 five-pattern tail", "one of seven patterns", "13 of the 18 export members") | Conditions instead of counts |
| `tests/consumer/consumer_witness.cpp` header ("seven-pattern exclusion set") | "its exclusion rule" |
| `.github/workflows/tier1.yml`, the `#254` job-end disk comment that lists the consumer install-witness among what lands before the measurement | State D-10: the stage is removed on PASS |

The `specs/084-*/{spec,plan,tasks,research,data-model}.md` and `checklists/` text is a merged feature's record. It
is left as history; the contract banners carry the supersession.

**Recipe that produced this table** (re-run it on the implementation branch and adjudicate every hit):

```
grep -rn -E 'unexported|Never exported|build-tree \+ in-tree|NO install\(\)|five-pattern|7-pattern|seven-pattern|seven patterns|EIGHT patterns|stay unexported|13 of the 18|ELEVEN measured|whole contract' \
  --exclude-dir=build --exclude-dir=.git --exclude-dir=.codegraph .
```

### 6.3 Outside this repository (for the orchestrator)

`research/G19-fix-fpml-iso20022/remaining-work/packaging-cpack.md` item 6 says "builders/validators stay unexported
(078 Gate B P1 — settled, not open)". It also carries the Option 3 ordering constraint. `REMAINING-WORK.md` row 10
says "typed builders not installed (#613)". Both change when this PR merges.

## 7. Acceptance map

| #613 item | Planned change | Planned test / evidence |
|---|---|---|
| 1 export + names + umbrella | D-1, D-2, D-3, D-5, D-6 | C-1 links all eight by name; M-1, M-3, M-3b, M-5; `packaging::export-names` (derived) |
| 2 slim layout, TUs out, `.inl` decided | D-7 (five patterns lifted, `*.cpp` added, `.inl` shipped) | C-1, C-2; `packaging::contents` slim-set equality; M-2a…f, M-4 |
| 3 out-of-tree consumer test with RED mutants | §4.1 C-1/C-2 inside the existing install witness | M-1…M-8, recorded in the verify record |
| 4 size before/after | §5 method | baseline in §5; "after" from the PR's CI artifacts by the same commands |
| 5 amend 078 contract + install comment | §6.1 texts | `check-comment-claims.py`; §6.2 recipe re-run, every hit adjudicated |

## 8. B&L delta (`spec/behaviors-and-limitations.md`)

- **B-613-1.** An installed consumer can build and validate typed messages for v42, v44, v50sp2 and vlatest. It
  links `fixpp::builders::<ver>` and/or `fixpp::validators::<ver>` by name. `fixpp::fixpp` does not reach them, and
  a `build_<Msg>` call from an umbrella-only consumer fails at link time naming the function. The 078 header-only
  macros work in the installed tree, under 078's same-message ODR rule.
- **L-613-1.** A tree configured with `FIXPP_CODEGEN_FIX_LATEST=OFF` exports and installs no vlatest typed tier and
  defines no packaging.
- **L-613-2.** The span-based `Args` shape, L-078-1's compile cost included, is installed public API and frozen for
  v1.0 (R-1). A forward-declared or handle-based `Args` ("Option 3") is a 2.0 break.

## 9. Owner rulings, 2026-10-24 (v0.1's open questions)

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
- **The 084 `contracts/export-set.md` banner (§6.2).** It quotes the ruling as the discharge of B3's standing
  obligation, and points at the 078 banner.
- **B&L L-613-2 (§8).**
- **`remaining-work/packaging-cpack.md` item 6** is in the parent repository. The orchestrator updates it; this PR
  does not touch it.

**R-2 (was OQ-2): cost.** Ruling: keep "all four versions, every platform" at the measured cost (§5). This covers:
- the slim headers, which add about as much again as the gcc archives;
- the Windows release ZIP growing from about 21 MB to about 120 MB, about 0.77 GB unpacked;
- each lane's install witness copying 5–11 GB of Debug/sanitizer archives per run, with local persistence bounded
  by D-10.

The implementation records the measured "after" figures from the PR's CI artifacts (§5).

No open owner question remains. Technical questions are settled in §1–§4 or assigned there as implementation
obligations with their measurement.
