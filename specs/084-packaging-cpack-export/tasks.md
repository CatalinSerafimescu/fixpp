# Tasks: Installable Packaging (CPack) + CMake Package-Config Export

**Input**: Design documents from `specs/084-packaging-cpack-export/`
**Prerequisites**: `plan.md` (incl. `## Gate A` + Sign-off decisions), `spec.md` (42 FR, 18 SC, 12 assumptions), `research.md` (R1–R14), `data-model.md` (E1–E6), `contracts/{export-set,package-layout}.md`, `quickstart.md`
**Branch**: `084-packaging-cpack-export` | **Repo root** = the library submodule, checked out as the **worktree** `/home/catalin/Work/Programming/fixpp-parallel/`
**Measurement record**: `research/reviews/orchestrator_084-packaging-cpack-export_gate_a_r1_measurements.md` (parent repo, `research/G19-fix-fpml-iso20022/`)

**Tests**: INCLUDED and mandatory. Three witness tiers (minimal / real-client / clean-environment) plus **five** new packaging ctests (contents, provenance, **telemetry-provenance**, real-client, clean-environment). **Four gates must be PROVEN RED before they count as gates**: SC-007a (T025), SC-007b (T059), SC-016's red leg (T042), and T062a's OTel-ON telemetry-provenance gate (FR-011 — not SC-keyed; only SC-007a/b exist).

**No C++ library code is written.** No `.cpp`/`.hpp` under `src/` or `include/` is touched — only CMake files inside those trees, `cmake/*.cmake`, `CMakePresets.json`, `conan/profiles/`, `tests/`, `perf/`, `.github/workflows/`, and a new root `NOTICE`.

## Format: `[ID] [P?] [Story?] Description with exact file path`

- **[P]** = parallelizable (different file, no incomplete dependency).
- **[US#]** appears only in user-story phases (Phase 3–5). Setup / Foundational / Polish carry no story label.
- Paths are **repo-root-relative** to the worktree above unless prefixed `PARENT:`.

---

## ⚠️ Standing constraints — every task below inherits these

**Build & disk** (spec Assumption 5, SC-008, `quickstart.md` §0/§1):

- `source /mnt/wsl/fixppbuild/env.sh` **before the first `cmake --preset`** in any tree. ccache is baked in at first configure only; configure without it and the only fix is deleting the tree.
- Conan invocation is the **tracked-profile + `-of`** form CI uses (`.github/workflows/tier1.yml:411-414`); the `-pr:a=gcc13 -s build_type=…` form in the pre-round-2 quickstart **does not run** (measurement M2):
  ```bash
  conan install . -pr conan/profiles/<preset> --build=missing -of build/<preset>
  ```
- **Serial only**: build one configuration → package → **delete the tree** → next. Peak ~30 GB against a 64 GB vhdx. **Start with `linux-gcc-release`** — 3.4 GB tree, **zero** third-party deps to build (M1).
- **`-j2` maximum.** Wider parallelism OOM-kills the session.
- **MSVC builds in a SEPARATE Windows sandbox on C:.** Do **not** reuse `/mnt/c/temp/fixpp` — it holds unrelated in-flight state from another feature.
- Package artifacts land in `/mnt/wsl/fixppbuild/artifacts/`, which **survives** build-tree deletion (FR-021) and therefore **accumulates** (FR-021a is why).

**Test hygiene** (`quickstart.md` §7b, `feedback_tree_mutating_test_must_run_serial_vs_gitclean_gate`):

- Every new packaging test configures/builds a sub-project ⇒ **`RUN_SERIAL` + an explicit `TIMEOUT`**, mirroring `CMakeLists.txt:308-310` (`TIMEOUT 300`, driven via `cmake -P`).
- Any scratch configure must write **outside** the source tree, or it reds the git-cleanliness gate at `tests/codegen/codegen_build_graph_test.cmake:202-224`.

**Closed sign-off decisions — implement, do NOT revisit** (`plan.md` → Gate A → Sign-off decisions, user 2026-08-01):

- **D1 / FR-012a = EXPORT the backing targets.** Every shipped include subtree gets a library behind it; the **only** exclusion is the two test-support subtrees. `contracts/package-layout.md` §2a is authoritative per row.
- **FR-018e = provider-agnostic CMake package.** `src/` links only imported target names (zero `.conan2` paths, zero `find_library(… NO_DEFAULT_PATH)`), so every `find_dependency` is a plain `find_package` against the **consumer's** `CMAKE_PREFIX_PATH`. Conan is how we build, not how anyone consumes.
- **D3 / FR-026a = the real-client witness is CI-gated on `linux-gcc-release` ONLY**; the other five configurations run the minimal tier.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: the build host, the missing configuration, and the one budget rule that has no owner yet.

- [X] T001 **Build-host preflight on `linux-gcc-release`** — from `/home/catalin/Work/Programming/fixpp-parallel`: `source /mnt/wsl/fixppbuild/env.sh`, then `conan install . -pr conan/profiles/linux-gcc-release --build=missing -of build/linux-gcc-release`, `cmake --preset linux-gcc-release`, `cmake --build --preset linux-gcc-release -j2`. Confirm the M1 result still holds (`opentelemetry-cpp/1.26.0: Already installed! (14 of 14)` — **zero** packages built from source) and that `/mnt/wsl/fixppbuild` is mounted (a missing mount looks exactly like a broken build tree; re-attach via `schtasks /run /tn "WSL Mount fixpp-build"` from Windows). This tree is the working tree for Phases 2–3.
- [X] T002 [P] **New TRACKED Conan profile** `conan/profiles/linux-gcc-debug` — a `build_type=Debug` sibling of `conan/profiles/linux-gcc-release`, preserving `compiler.version=13`, `libcxx=libstdc++11`, `cppstd=23`, `CC/CXX=gcc-13/g++-13`, Ninja (research R5, FR-022, spec Assumption 8). **Never** a `-s build_type=Debug` command-line override — that leaves the configuration unreproducible and outside Article III §2's "declared, pinned" rule. Adding a profile needs **no** Article XX amendment: Article III §3's enumeration is already non-exhaustive (four in-repo libc++ profiles are absent from it).
- [X] T003 [P] **New `linux-gcc-debug` preset** in `CMakePresets.json` — mirror the `linux-gcc-release` preset (`:98-109`) with `CMAKE_BUILD_TYPE=Debug`, keeping `FIXPP_BUILD_OTEL=ON` (spec Assumption 4: an OTel-OFF shipped artifact would make gcc-Debug the sole package missing the OTel targets). FR-022. Note: this configuration needs **9** third-party packages built from source (Assumption 9) — schedule it late in the matrix, not early.
  - Delivered 2026-08-02 with matching **build** and **test** presets as well, not the configure preset alone — the other 14 configurations all carry the triple, and a configure-only entry would leave `--build --preset linux-gcc-debug` and `ctest --preset linux-gcc-debug` failing for this configuration only.
  - `FIXPP_BUILD_OTEL=ON` is set **explicitly** even though `ON` is already the option default (`CMakeLists.txt:73`). That is deliberate rather than redundant: it is the pin spec Assumption 4 relies on, so a future flip of the default cannot silently make gcc-Debug the sole package shipping without the OTel targets. JSON carries no comments, hence this note.
  - **Census correction made while adding this profile**: the Conan-preset-collision comment block carried in all 14 pre-existing profiles said *"10 of the 14 profiles here are Debug"*. The real count was **11 of 14** (now **12 of 15**), and the tracked preset count was 15 (now **16**). Both numbers corrected across all 15 profiles; comment-only, two lines per file.
- [X] T004 [P] **Decide and record the artifact-directory retention rule** (spec Assumption 5, SC-008, `contracts/package-layout.md` §5) — `/mnt/wsl/fixppbuild/artifacts/` grows monotonically across four Linux configurations × **three redundant formats** (DEB/RPM/TGZ), each carrying the same ~4.6 GB payload, on the **same 64 GB volume** as the build tree and a 20 GB ccache. Choose **either** a stated retention rule (with a measured budget) **or** placement on different storage, and record the choice in `specs/084-packaging-cpack-export/quickstart.md` §0. **This is one of the three open implementation choices `plan.md:429` lists** — it must be decided, not inherited.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: `install(EXPORT)` is a **generate-step hard error** today, twice over. Nothing in any user story can be written until the tree generates with the export wired.

**⚠️ CRITICAL — this phase is unusually heavy and it BLOCKS all three stories.** Measurement M3/M4/M5 executed exactly this and captured all three blockers verbatim: the `FILE_SET` fires first, then all eight reachable module targets, then a **three-level** closure cascade. Each blocker masks the next.

### The include-interface rewrite (FR-002a — the largest blocker)

> **Census, measured**: `grep -rn BUILD_INTERFACE src/` → **0 matches**. Every module target declares `PUBLIC "${CMAKE_SOURCE_DIR}/include"` raw. `install(EXPORT)` rejects such a target at generate: *"Target … INTERFACE_INCLUDE_DIRECTORIES property contains path … which is prefixed in the source directory."* **14** files under `src/` carry the raw path; **13** are edited (below), holding **14** targets. `src/core/test/CMakeLists.txt:15` (`fixpp_mock_clock`) is the **one that stays out** — its subtree is the FR-012a `exclude`.

- [X] T005 **Decide and record the include-interface rewrite FORM** (FR-002a; `plan.md:429` open choice 1) — **per-module** (`target_include_directories(<tgt> PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)` in each file) **vs centralised** (`set_property(TARGET … PROPERTY INTERFACE_INCLUDE_DIRECTORIES …)` from the root). The Gate A generate run **explicitly did not settle this** — it is an implementation choice, not a further experiment (research R11, measurement "Not measured" note). Record the choice and its rationale in `specs/084-packaging-cpack-export/research.md` → "Open items added at Gate A round 1". **If the centralised form is chosen, T006–T018 collapse into one root-level task and each per-file row below is marked with a waiver pointing at this decision — do NOT silently drop them.**
- [X] T006 [P] `src/core/CMakeLists.txt:13` — give `fixpp_core` a `$<BUILD_INTERFACE:>`/`$<INSTALL_INTERFACE:>`-discriminated include interface (measured generate error, M4).
- [X] T007 [P] `src/core/sync/CMakeLists.txt:26` — same for `fixpp_sync` (M4).
- [X] T008 [P] `src/wire/CMakeLists.txt:21` — same for `fixpp_wire` (M4).
- [X] T009 [P] `src/dictionary/CMakeLists.txt:35` — same for `fixpp_dictionary` (M4). **Do NOT touch `fixpp_dict_dispatch_bridge`**: its only include directory is `PRIVATE` (`:82-84`), so it has no `INTERFACE_INCLUDE_DIRECTORIES` to reject — which is why it never appeared in M4.
- [X] T010 [P] `src/tls/CMakeLists.txt:20` — same for `fixpp_tls` (M4).
- [X] T011 [P] `src/transport/CMakeLists.txt:22-26` — same for `fixpp_transport` (M4). The `FILE_SET` at `:53-60` is a **separate** blocker with its own task (T020) — do not conflate them.
- [X] T012 [P] `src/log/CMakeLists.txt` — **TWO targets in one file**: `fixpp_log` (`:16-17`, measured at M4) and `fixpp_log_otlp` (`:42-43`, an export-set member **derived** at the sign-off via `fixpp_config_toml`'s PRIVATE edge, `contracts/export-set.md` §2a). Both need the rewrite.
- [X] T013 [P] `src/session/CMakeLists.txt:28` — same for `fixpp_session` (M4).
- [X] T014 [P] `src/otel/CMakeLists.txt:15-16` — same for `fixpp_otel`. Absent from M4 only because it joined the export set at M5; it declares the raw path identically and fails the same check the moment it joins. **`fixpp_otel` is exported UNCONDITIONALLY by this feature** — in an OTel-OFF tree nothing links it (the PUBLIC edge from `fixpp_session` sits inside `if(FIXPP_BUILD_OTEL)`, `src/session/CMakeLists.txt:55-57`), so its membership in every configuration is held by the export, not by the closure. That is the form SC-015 exercises.
- [X] T015 [P] `src/config/CMakeLists.txt:22-23` — same for `fixpp_config_toml` (**derived** member; FR-012a = export). Adds `tomlplusplus` to the `find_dependency` set (T030) and drags `fixpp_log_otlp` in (T012).
- [X] T016 [P] `src/tap/CMakeLists.txt:7-8` — same for `fixpp_tap` (**derived**; FR-012a = export, and forced independently by `fixpp_capi_objects`' PUBLIC edge at `src/capi/CMakeLists.txt:36`).
- [X] T017 [P] `src/service/CMakeLists.txt:10-11` — same for `fixpp_service` (**derived**; follows D1 — its only link edge is `fixpp_capi`, `src/service/CMakeLists.txt:15`).
- [X] T018 [P] `src/capi/CMakeLists.txt:25-26` — same for **`fixpp_capi_objects`**, **not** for `fixpp_capi`, which declares no include directories of its own (`:43-45`). `fixpp_capi_objects` is an **OBJECT** library — its `install(TARGETS)` shape is handled at T023/T024.
- [X] T019 **Census assertion for the rewrite** — after T006–T018: (a) `grep -rn BUILD_INTERFACE src/` now matches **13 files / 14 targets** and **not** `src/core/test/CMakeLists.txt`; (b) `src/core/test/CMakeLists.txt:15` (`fixpp_mock_clock`) is verifiably **unedited** — its subtree `include/fixpp/core/test/` is the FR-012a `exclude`; (c) the three export-set members that correctly need **no** edit are recorded by name with their reason — `fixpp_dict_dispatch_bridge` (PRIVATE include dir, `src/dictionary/CMakeLists.txt:82-84`), `fixpp_dict_dispatch` (already `$<BUILD_INTERFACE:>`, `cmake/Codegen.cmake:589-590`), `fixpp_capi` (no include dirs at all). Record in the T005 decision note.

### The second generate blocker and the export-shape obligations

- [X] T020 **Decide, record, and apply the `fixpp_transport` `FILE_SET HEADERS` disposition** (FR-002b, research R12; `plan.md:429` open choice 2) — `src/transport/CMakeLists.txt:53-60` is the repository's **only** interface `FILE_SET` and is the **first** error the export produces (M3): *"install TARGETS target fixpp_transport is exported but not all of its interface file sets are installed."* Choose among the three dispositions with their footprints: (i) **install the file set** — needs a `FILE_SET HEADERS DESTINATION` chosen so it does not conflict with the directory rule at `CMakeLists.txt:321-324`, and **the chosen `DESTINATION` MUST be recorded**; (ii) **demote to `PRIVATE`** — the file set is already annotated *"Phase 2 public headers (installed via top-level install(DIRECTORY include/))"*, so demotion may be near-free — **verify before relying on it**; (iii) **drop it** — same file, larger blast radius. Record the choice and consequence in `research.md` R12.
- [X] T021 [P] **`fixpp_dict_dispatch` must carry an EMPTY `$<INSTALL_INTERFACE:>`** — standing obligation from the Gate A sign-off (`plan.md:368`, `contracts/export-set.md` §2/B2/B3, research R2). Its sole include directory (`cmake/Codegen.cmake:589-590`) covers `_dispatch/`, which is install-**EXCLUDED** by the denylist at `CMakeLists.txt:349`. It is a **link-closure-only member**: exported solely to satisfy CMake's closure rule for the bridge's `$<LINK_ONLY:>` edge (`src/dictionary/CMakeLists.txt:89`). An empty install interface makes invariant I2 / rule B2 hold **by construction** rather than by luck. Add an assertion that the interface stays empty, so a future non-empty one trips FR-010's standing obligation.
- [X] T022 **Set `EXPORT_NAME` on EVERY exported target** — `install(EXPORT … NAMESPACE fixpp::)` derives imported names from **target** names, and `add_library(fixpp::X ALIAS …)` does **not** carry into an export. Left alone the package publishes `fixpp::fixpp_capi` / `fixpp::fixpp_config_toml` / `fixpp::fixpp_tap` / `fixpp::fixpp_service`, while `.specify/architecture.md:503` (the ground D1 Option A was decided on) names **`fixpp::capi`** and `include/fixpp/config/toml_config_loader.hpp:7-8` tells consumers to link **`fixpp::config_toml`**. Census: `grep -rn EXPORT_NAME src/ cmake/ CMakeLists.txt` → **0** — it is set nowhere in the repository today. Apply `set_target_properties(<tgt> PROPERTIES EXPORT_NAME <short>)` across the **whole** set (`fixpp::core`, `fixpp::session`, `fixpp::wire`, … — `src/core/CMakeLists.txt:10` and thirteen siblings). **Pick ONE convention; a half-applied one is worse than either** (`contracts/export-set.md` §1).
- [X] T023 **Write `install(TARGETS … EXPORT fixppTargets)` over the full member set** in `CMakeLists.txt` — the **11 measured** (`fixpp_core`, `fixpp_sync`, `fixpp_log`, `fixpp_wire`, `fixpp_dictionary`, `fixpp_tls`, `fixpp_transport`, `fixpp_session`, `fixpp_dict_dispatch_bridge`, `fixpp_dict_dispatch`, `fixpp_otel`) **plus the at-least-six derived** (`fixpp_capi`, `fixpp_capi_objects`, `fixpp_config_toml`, `fixpp_log_otlp`, `fixpp_tap`, `fixpp_service`). **`fixpp_capi_objects` is an OBJECT library and needs an `OBJECTS DESTINATION` — an install shape no other member has**; it **cannot** be escaped by demoting `fixpp_capi`'s PUBLIC edge to `PRIVATE`, because `$<LINK_ONLY:>` entries are export requirements too (FR-008a/B1 — exactly how `fixpp_dict_dispatch` was dragged in). If the destination proves unacceptable, the alternative is a **wiring change on `fixpp_capi`**, never a quiet exclusion.
- [X] T024 **RE-RUN THE GENERATE EXPERIMENT — blocking, not optional** (`contracts/export-set.md` §2a; `plan.md` → Next, item 1). The eleven are the output of an **executed** run; the six the sign-off adds are the output of **reading `target_link_libraries`** — the exact method the measurement caught being wrong in **three places across a three-level cascade**, and blind to two mechanism-level blockers. Method, mirroring the round-1 measurement: configure `build/linux-gcc-release`, **peel errors one at a time — each blocker masks the next** — and capture each diagnostic verbatim. **First question it must answer: does `fixpp_capi_objects` need an `OBJECTS DESTINATION`, or a wiring change?** Reconcile the resulting member set against `contracts/export-set.md` §2a. ⚠️ **If the reconciled set differs from §2a, update §2a AND re-open every task whose content is DERIVED from the member set, before any US1 task starts** — the named checklist is: **T023** (the `install(TARGETS … EXPORT)` member list), **T026** (the exact-set membership assertion), **T030** (the `find_dependency` set — a new member can contribute a seventh package), and **a new `src/*/CMakeLists.txt` include-interface task** for any added member that declares a raw `${CMAKE_SOURCE_DIR}/include` path (the pattern of T012/T015/T016/T017/T018). This checklist exists because the reading method missed **three** members across a **three-level** cascade last time; "update the downstream tasks" without an addressee is how that recurs. Record the run as `PARENT: research/G19-fix-fpml-iso20022/research/reviews/orchestrator_084-packaging-cpack-export_implement_export_remeasurement.md`. **Producer-side generate success is NOT evidence the dependency set is complete** (M6 — `Crc32c` produces no producer-side error; it is a `$<LINK_ONLY:>` **consumer**-configure obligation, discharged only by T041/T042).

  **Re-open checklist — additions (analyze F1).** Two further tasks must be re-opened if the reconciled membership differs, and were missing from the original list. **T019**: its census asserts an *exact* count ("13 files / 14 targets"); a new member carrying a raw include path makes that number silently wrong — an exact-match gate checking the wrong value, structurally identical to the round-2 `OPEN`-passes-`non-empty` defect this bundle already fixed once. **T022**: a member added after the `EXPORT_NAME` sweep escapes it entirely and reproduces the `fixpp::fixpp_capi` naming defect T022 exists to prevent. Re-open **T019, T022, T023, T026, T030** and any new include-interface task.
- [X] T025 **SC-007a — export-closure red EVIDENCE (FR-008/FR-008a).** ⚠️ **This is NOT a ctest task, and must not be written as one.** CMake enforces closure at **generate**, so a tree with a deliberately broken export set produces **no build system** and therefore no `ctest` can run inside it — claiming a ctest-shaped assertion for a failure mode that prevents ctest from existing is the exact defect this criterion's split exists to prevent. Deliver **exactly one** of: **(i)** a nested configure of a **scratch copy** of the tree asserting a non-zero exit **and** the expected diagnostic `install(EXPORT) … includes target … which requires target … that is not in any export set` — registered `RUN_SERIAL` with an explicit `TIMEOUT` and writing **outside** the source tree (git-cleanliness); **or (ii)** a **recorded red generate run** captured during T024, cited as the evidence, with CMake's own generate-time enforcement named as the standing gate. Record which option was taken and why.
- [X] T026 **Export-set membership exact-set assertion** (FR-007, FR-008) — assert the export set equals the reconciled T024 list, as **set equality both directions**, and that it excludes by name: `fixpp_builders_<ver>` and `fixpp_validators_<ver>` (FR-007, settled by 078 Gate B P1 — *not open for relitigation*), and the per-version `fixpp::dict::<ver>` INTERFACE targets (**decided out**, research R2 — they add no post-install capability since both install rules share `${CMAKE_INSTALL_INCLUDEDIR}`, `CMakeLists.txt:323` == `:348`, and `fixpp_dict_vt11` would export over denylisted-and-therefore-absent content). **Standing re-open trigger, not a live choice for this task**: if a *future* change adds them, that change must enumerate them by name (never as a class), disposition `vt11` explicitly, and provide a check other than SC-002 for their install interface — SC-002 structurally cannot carry it.

  **Installed-NAME assertion — membership is not naming (FR-003).** Set equality over *members* cannot see a wrong `EXPORT_NAME`: an export publishing `fixpp::fixpp_capi` has exactly the right membership and the wrong contract. T022 **sets** `EXPORT_NAME`; nothing yet **checks** it, so its named failure mode — a half-applied sweep — has no gate. Add the check `contracts/export-set.md` §1 already names: build the **alias census** mechanically (`grep -rn "add_library(fixpp::[A-Za-z_:]* ALIAS" src/ cmake/` — `src/core/CMakeLists.txt:10` and its siblings), extract every imported name from the **installed** `fixppTargets*.cmake` (`add_library(fixpp::<name> STATIC|INTERFACE IMPORTED)`), and assert the two `fixpp::<short>` sets are **equal both directions** — never a subset, and never a spot-check of one member. A member whose installed imported name does not match its existing in-tree alias is a **red**. **Independent to author, not to run** (the T028 pattern): the generated targets file does not exist until `install(EXPORT …)` lands at **T032**, so this leg is written here and first *executes* against a staged install — ordering is self-enforcing, but do not read "Foundational" as licensing a green claim before T032.
- [X] T027 **FR-010 / SC-009 — record the `Args` boundary verification with its evidence.** Re-derive against the **final** T024 export set (not against research R1's pre-export reading) and state **explicitly** whether the export set reaches anything under the typed-builder `messages/` or `groups/` trees. Two independent mechanisms are expected to hold it clean: the 7-pattern denylist (`CMakeLists.txt:349-355`) and `fixpp_dict_dispatch`'s empty install interface (T021). **⚠️ If the final export set DOES reach either tree, WORK STOPS and the deferred "Option 3" `Args` decision is escalated to the user before any export ships** (FR-010, SC-001/L-078-1). Record the verification in `research.md` R1.
- [X] T028 [P] **Compile-definition fidelity across the export** (data-model I5, `contracts/export-set.md` B4, research R4) — assert `FIXPP_LOG_MIN_LEVEL` **does** reach a consumer through the exported `fixpp_log` interface (public headers branch on it via `if constexpr`, and it is build-type-conditional: Debug `0`, Release `2`, `CMakeLists.txt:60-64`), and that `FIXPP_BUILD_OTEL` **does not** (verified zero occurrences under `include/`; it is directory-scoped at `CMakeLists.txt:102` and does not travel with an exported target). **Recorded because it looks like a hazard and is not**: an implementer who "fixes" the apparent propagation gap by adding `FIXPP_BUILD_OTEL` to the exported interface introduces a real ODR mismatch that currently cannot occur.

**Checkpoint**: `cmake --preset linux-gcc-release` **generates** with the export wired, the member set is re-measured and reconciled, the closure gate has recorded red evidence, and the `Args` boundary is confirmed clean. User stories can now begin.

---

## Phase 3: User Story 1 — A downstream C++ project consumes fixpp via `find_package` (Priority: P1) 🎯 MVP

**Goal**: `find_package(fixpp REQUIRED)` + `target_link_libraries(app PRIVATE fixpp::fixpp)` is the whole contract. Include paths, compile features, link ordering and transitive third-party dependencies all arrive through the imported target. The consumer never hand-adds an include directory and never guesses a link line.

**Independent Test**: three tiers, **because they fail differently** — do not collapse them.

1. **Minimal** — `ctest --test-dir build/linux-gcc-release -R "consumer::install-witness"`. Proves `find_package` resolves **and the export closure links** (it goes from four hand-listed archives to `fixpp::fixpp`), and that both header kinds arrive.
2. **Real-client** — `ctest -R "packaging::real-client"`. Proves the export is *sufficient to build a working FIX application*, linked **and run** against a live counterparty. CI-gated on `linux-gcc-release` only (FR-026a).
3. **Clean-environment** — `ctest -R "packaging::clean-env"`. The **only** tier that inherits **nothing** from the producing build. **Mandatory, not optional** — the other two are both handed the producer's Conan toolchain (`tests/consumer/CMakeLists.txt:39-44`), so neither can fail on the dependency-provisioning gap, and without this tier they are green against a package no operator can consume.

### Export surface and package config

- [X] T029 [US1] **Create the `fixpp::fixpp` umbrella** in `CMakeLists.txt` (FR-001, FR-002) — an INTERFACE target linking `fixpp_session` (which transitively pulls the measured rest), carrying the public include directories with `$<BUILD_INTERFACE:>`/`$<INSTALL_INTERFACE:>`, added to `fixppTargets`, with its `EXPORT_NAME` per the T022 convention. **Re-confirm generate after adding it.** ⚠️ **The umbrella is NOT the whole export set**: it does **not** reach `fixpp_capi`, `fixpp_config_toml`, `fixpp_tap`, `fixpp_service` or `fixpp_log_otlp` — `fixpp_session` links none of them. That is **correct, not a gap** (`contracts/export-set.md` §1) — but **the five are not one class**. **Four** — `fixpp_capi`, `fixpp_config_toml`, `fixpp_tap`, `fixpp_service` — are exported so a consumer can link them **by name** (`fixpp::capi` for a C-ABI consumer who deliberately does *not* want the C++ umbrella per Article IV §2 / `architecture.md:509`; `fixpp::config_toml` as `include/fixpp/config/toml_config_loader.hpp:7-8` instructs). The **fifth**, `fixpp_log_otlp`, is **closure-only**: it is in the export set solely as `fixpp_config_toml`'s `$<LINK_ONLY:>` requirement (FR-008a), **no public header names `fixpp::log_otlp`**, and nothing instructs a consumer to link it directly. Do not restate the five as one "available by name" class — that is the conflation the checklist audit corrected in FR-001 and `contracts/export-set.md` §1.
- [X] T030 [P] [US1] **New `cmake/fixppConfig.cmake.in`** with the `find_dependency` set **DERIVED from the configured target graph, never from option state** (FR-004, FR-010c, invariants I3/I6/I7). Derivation rule: *for every export-set member, every imported target in its `$<LINK_ONLY:>`-expanded link interface contributes its `find_package` package name — PRIVATE deps included, because a static library does not link its private deps and the consumer's final link must resolve them.* ⚠️ **The rule is the input; the six names below are its EXPECTED OUTPUT when applied to the T024-reconciled member set — implement the RULE, not the list.** This is the exact shape FR-010c retracted: the original hand-written enumeration was wrong *in both directions* (it named compression and networking, which no target links, and omitted `Crc32c`, which every consumer needs). A derivation rule cannot drift the way an enumeration did; if T024 adds a member, the set may be seven. Applied today, the set is **six**: `OpenSSL`, `asio`, `pugixml`, **`Crc32c`** (`src/session/CMakeLists.txt:91`, PRIVATE — same mechanism as pugixml, and omitted from every enumeration until Gate A round 1), **`opentelemetry-cpp`** (keyed on `if(TARGET opentelemetry-cpp::api)`, `src/otel/CMakeLists.txt:36`, **not** on `FIXPP_BUILD_OTEL`), **`tomlplusplus`** (`src/config/CMakeLists.txt:37`, unconditional since the sign-off exports `fixpp_config_toml`). **`ZLIB` is DELIBERATELY omitted** (research R3, invariant I7): no fixpp target links it; a spurious requirement is as much a packaging defect as a missing one. ⚠️ If a ZLIB link failure appears in the real-client witness, **first check whether the OpenSSL imported target regressed** — adding `find_dependency(ZLIB)` would mask it. `fixpp_log_otlp`'s three extra `opentelemetry-cpp::*` targets add **no new** `find_dependency` (same package). The failure mode for an omission is a **configure-time hard error inside `find_package(fixpp)`**, not a link-time undefined symbol.
- [X] T031 [US1] **Version file** in `CMakeLists.txt` — `write_basic_package_version_file` deriving from `project(VERSION)` (`CMakeLists.txt:5`, currently `0.0.1`), **never** a packaging-local literal (FR-005), so a later GA bump (REMAINING-WORK item 13) propagates with no packaging change. The compatibility mode must make an incompatible request fail at **configure** (FR-006).
- [X] T032 [US1] **`install(EXPORT fixppTargets NAMESPACE fixpp:: …)` + `configure_package_config_file` + install the config, version file and targets files** in `CMakeLists.txt` (consuming `cmake/fixppConfig.cmake.in`), to the standard CMake package directory `${CMAKE_INSTALL_LIBDIR}/cmake/fixpp` (FR-003), such that `find_package(fixpp)` locates them **with no consumer-side hints**.
- [X] T033 [US1] **Dictionary install rule** in `CMakeLists.txt` (FR-018a) — `dictionaries/` has **no install rule at all** today (verified: zero matches), so the package would ship `fixpp::dict::load_any(path, …)` with none of its data. Install the vendored FIX dictionary data files to a data directory inside the prefix.

### The minimal witness (existing, extended)

- [X] T034 [P] [US1] **Convert `tests/consumer/CMakeLists.txt` to `find_package(fixpp)` + `fixpp::fixpp`** (SC-001, FR-010b) — remove the hand-added `${FIXPP_STAGE_PREFIX}/include` (`:50`), the globbed archive list and the four hand-listed archives (`:56`), the hand-rolled `-Wl,--start-group` (`:61-74`), and the standalone `find_package(pugixml CONFIG REQUIRED)`; update the stale header comment at `:14` (*"There is no fixpp CMake package-config / find_package(fixpp) export"*). The surrounding harness — `run_consumer_witness.cmake`, stage-install, build-type inheritance (`CMakeLists.txt:299-305`) — is reused **unchanged**. ⚠️ **This is not a discovery-only change** (spec Assumption 7): the tier acquires the **entire measured closure** — **not** the full export set; the umbrella reaches neither the four by-name members nor the closure-only `fixpp_log_otlp` (`contracts/export-set.md` §1) — plus OpenSSL, asio and Crc32c, so it becomes the **first** place a missing `find_dependency` (FR-010c) or the `FILE_SET` blocker (FR-002b) surfaces — **early failures here are real, not harness noise**. Dropping `--start-group` is **expected to be safe, not a known defect**: the archives contain two deliberate static-archive cycles (`src/wire/CMakeLists.txt:27-30` ↔ `src/dictionary/CMakeLists.txt:46-49`; dictionary→bridge→dictionary at `:88-90`/`:97`), and CMake repeats archives itself when ordering exported **targets** — the hand-rolling existed because raw archive paths carry no dependency graph to repeat (`contracts/export-set.md` B5). **Scope of SC-001: green under the producing build's environment ONLY** — it must not be cited as evidence the package is consumable off the producing host (that is SC-016's). ⚠️ **Add a SECOND, SEPARATE consumer target that links a by-name member directly — `fixpp::capi` (FR-003).** Every witness in this feature otherwise links only the umbrella or the session stack, so **no** witness exercises a by-name member's installed imported name at all and T022's `EXPORT_NAME` sweep is proven only statically (T026). It **must** be a separate target, never the same one: linking `fixpp::capi` and `fixpp::fixpp` together is the combination Article IV §2 / `architecture.md:509` rejects (`tools/check_layers.py`). A one-translation-unit target including a `include/fix/` header and linking `fixpp::capi` is sufficient — the point is that the name resolves and links, not that the C ABI is re-tested. **The harness genuinely stays unchanged**: `CMakeLists.txt:290-307` registers **one** `add_test` that configures, builds and runs the whole consumer sub-project, so a second target inside `tests/consumer/CMakeLists.txt` is built by the existing witness with **no** new `add_test` and no edit to `run_consumer_witness.cmake` — building and linking it is the assertion; it need not be executed.
- [X] T035 [P] [US1] **`tests/consumer/consumer_witness.cpp` must include BOTH header kinds, reaching both through `fixpp::fixpp` alone** (SC-002) — one hand-written public `include/` header **and** one generated per-version typed header (e.g. `<fixpp/v44/Fields.hpp>`). They arrive via two **different** install rules (`CMakeLists.txt:321` and `:346`), the second filtered by the 7-pattern exclusion set. **State the limit in the test's own comment**: because both rules write to the same `${CMAKE_INSTALL_INCLUDEDIR}` (`:323` == `:348`), a generated header resolves through the umbrella's single install include root **regardless** of whether any per-version `fixpp::dict::<ver>` target has an install interface at all — so SC-002 proves the generated headers were **installed and are reachable**, and is structurally incapable of failing on a broken per-version install interface. Do not cite it for the latter.
- [X] T036 [US1] **SC-006 / FR-006 red** — a consumer requesting an incompatible version (`find_package(fixpp 1.0 REQUIRED)` against an installed `0.0.1`) **fails at configure time with a version-specific diagnostic**, not at build or link. Register as a ctest; the failure must be observed, not assumed.
- [X] T037 [US1] **SC-014 — the API/data pairing is usable, not merely co-located.** A consumer loads a **shipped** dictionary through the public runtime-loading API (`fixpp::dict::load_any`, `include/fixpp/dict/load_any.hpp`) using **only paths inside the installed prefix**, with **no** file from the source tree. Depends on T033.

### The real-client witness (existing program, adapted)

> ### ⚠️ AMENDED AT IMPLEMENTATION — 2026-08-02. Delivered as a standalone project, not a `perf/` variant.
>
> **The claim T038–T040 were written to establish is unchanged and is fully delivered**: a real,
> pre-existing client builds and runs against the installed package with no source-tree path on its
> compile or link line. What changed is *where* the packaged-variant lives, and the change makes the
> claim **stronger**, because the driver is now used **byte-for-byte unmodified** (asserted by sha256
> in the runner) instead of adapted.
>
> Delivered as `tests/packaging/real_client/` (a standalone `project()`), configured from a **copy**
> under the witness work dir, registered as `fixpp::packaging::real-client`.
>
> Three consequences for the task text below, all recorded rather than silently diverged:
>
> 1. **T038's "add the path to `perf/CMakeLists.txt`" does not apply.** `perf/` is shared with the
>    interop workstream, and a target defined there inherits the top-level directory scope — so
>    satisfying T040 inside `perf/` would mean proving a negative about inherited state. A separate
>    `project()` boundary makes T040 **structural**: there is no producer source path in scope to leak.
> 2. **T038 subtraction (2) — "drop the `HdrHistogram_c` FetchContent" — is met with a witness-local
>    shim** (`real_client/shim/hdr/hdr_histogram.h`), not by deletion. Deleting it would require
>    editing the driver's `hdr_*` call sites, which destroys the "real, unmodified client" property
>    that is this tier's entire value. The network fetch is gone either way.
> 3. **T038 subtraction (3) — "replace the dictionary helper with a runtime `load_any` of a shipped
>    dictionary" — is met by COPYING `tests/support/minimal_dictionary.hpp` next to the copied `.cpp`.**
>    Same reason: `load_any` would mean editing the driver, and it would also swap the minimal FIX 4.2
>    dictionary for FIX 4.4, changing the client's runtime behaviour inside a packaging witness. The
>    shipped-dictionary load path is already witnessed directly by **T037** in
>    `run_consumer_witness.cmake`, so nothing is lost. Copying (not duplicating into the repo) keeps the
>    helper from drifting out of sync with the tracked one.
>
> T039's `FIXPP_BUILD_INTEROP_PERF` note likewise does not apply — the standalone project does not
> consult that option. **FR-010b is still proven**, and more visibly: the witness restates no static
> link ordering, no `--start-group`, and no OpenSSL/ZLIB `find_library` probes, all of which
> `perf/CMakeLists.txt` hand-rolls when building in-tree.

- [X] T038 [P] [US1] **Add the packaged-variant path to `perf/CMakeLists.txt`** (research R9) with **three subtractions**, none of which changes the program's status as a real client: (1) drop `src/` and `tests/` from the include path (`:51-54`) — SC-012 forbids any source-tree path; (2) drop the `HdrHistogram_c` `FetchContent` (`:43-47`) — a **network fetch**, and latency instrumentation is irrelevant to a link-and-run witness; (3) replace the one non-public include, the test-support dictionary helper `support/minimal_dictionary.hpp`, with a **runtime load of a shipped dictionary** via `fixpp::dict::load_any` (which is what FR-018a makes possible). Also do **not** carry forward the stale comment at `:15-17` claiming `fixpp_transport`/`fixpp_tls` expose asio/OpenSSL *"PRIVATEly"* — `src/transport/CMakeLists.txt:43-49` links OpenSSL **PUBLIC** (research R7).
- [X] T039 [US1] **New `tests/packaging/run_real_client_witness.cmake` + its ctest registration** (SC-011, FR-010a, FR-010b) — build `perf/fixpp_perf_driver.cpp` **out-of-tree** against the installed package, link it, and **run it against a live counterparty**. Register `RUN_SERIAL` with an explicit `TIMEOUT`. It is gated behind `FIXPP_BUILD_INTEROP_PERF` (declared `OFF` at `cmake/ProjectOptions.cmake:10`), so the witness must **enable it explicitly** rather than assume it is present. This is the tier that catches an export which **resolves but cannot link a real program**, and the only one that proves FR-010b — the exported targets must carry the static-link ordering (`libfixpp_tls.a` references cryptography symbols and needs the fixpp archives to precede them; `perf/CMakeLists.txt:56-57` hand-rolls it today) so a consumer never restates it. ⚠️ **This inverts a standing caution**: the driver is documented as needing an in-tree build so it links freshly generated libraries rather than a stale prebuilt one. Building it against an installed package is safe **only** because the package comes from the build under test — which is what FR-021a (T053/T060) enforces. **Treat any relaxation of FR-021a as reintroducing the staleness trap that guidance was written to prevent.**
- [X] T040 [US1] **SC-012 — the NAMED equivalent check, not the impossible one.** The *"source tree unavailable"* form is **impossible by construction and MUST NOT be claimed**: the real client *is* `perf/fixpp_perf_driver.cpp` and its build *is* `perf/CMakeLists.txt`, both inside the source tree. The operative check is: **configure the adapted client from a directory OUTSIDE the source tree with `CMAKE_PREFIX_PATH` set only to the staged prefix, then assert the generated `compile_commands.json` and the link line contain ZERO paths under the fixpp source root.** Assert it mechanically; an "or an equivalent check" branch that nobody writes is the `feedback_ci_gate_observes_not_asserts_witness_skips_into_green` shape.

### The clean-environment witness (SC-016 — the only tier that inherits nothing)

- [X] T041 [US1] **New `tests/packaging/run_clean_env_witness.cmake` — SC-016 PASS leg** (FR-018e obligation 4). Configure a consumer project with `CMAKE_PREFIX_PATH` set to the staged install prefix **plus a dependency prefix the producing build's package manager did NOT fill** — **no** `conan_toolchain.cmake`, **no** producer-supplied `CMAKE_PREFIX_PATH` entry, **no** source-tree path. Assert `find_package(fixpp REQUIRED)` **succeeds**, `fixpp::fixpp` is defined, and the consumer **compiles and links**. Register `RUN_SERIAL` + explicit `TIMEOUT`. **This has a PASS state** — the old "expected to fail under option (c)" reading was withdrawn at the sign-off: the package is provider-agnostic by construction (`src/` links only imported target names; zero `find_library(…)`, zero `.conan2` paths). **Why no other witness can carry this**: the consumer sub-build is handed `-DCMAKE_TOOLCHAIN_FILE=<build-dir>/conan_toolchain.cmake` by the existing harness (`tests/consumer/CMakeLists.txt:39-44`, driven from `CMakeLists.txt:284-306`), so it sees the producer's exact dependency graph and SC-001 is satisfiable without this property ever holding — `feedback_verification_corpus_built_from_the_read_it_checks_is_blind`.
- [X] T042 [US1] **SC-016 RED leg — PROVEN, not assumed** (`feedback_sanitizer_canary_must_be_proven_red`). In the same harness, remove **exactly one NAMED dependency** from that prefix and re-run: `find_package(fixpp)` MUST fail with **that dependency's** `find_dependency` diagnostic **and no other**. Without a discriminating red leg the check cannot distinguish a missing dependency from an uninstalled `fixppConfig.cmake`, a malformed version file, or a `fixppTargets.cmake` naming a nonexistent target. Record the observed diagnostic verbatim. **This is a separate deliverable from T041 and must not collapse into a mention inside it.**
- [X] T043 [US1] **FR-018e obligation 1 — the installed config must stay provider-agnostic.** In the same run, grep the **INSTALLED** `fixppConfig.cmake` and `fixppTargets*.cmake` (never the templates — FR-018d) for any path under the build host's package-manager cache (`~/.conan2`, `$CONAN_HOME`) and for any provider-specific config filename. **A hit is a RED**: a `fixppConfig.cmake` carrying an absolute path under the producing host's package-manager cache configures perfectly on that host and is unusable anywhere else — the provider-agnostic property lost silently, **in generated files nobody reads**.

### Telemetry-disabled resolution

- [X] T044 [US1] **SC-015 — the generated config resolves against a telemetry-DISABLED build, in a FRESH build folder.** ⚠️ **Delete the tree first; never reconfigure the OTel-ON tree in place** (`quickstart.md` §6): `find_package(opentelemetry-cpp CONFIG QUIET)` (`CMakeLists.txt:52`) cached `opentelemetry-cpp_DIR` there, and `src/otel/CMakeLists.txt:36` keys its SDK link on `if(TARGET opentelemetry-cpp::api)` — **not** on `FIXPP_BUILD_OTEL` — so a reused tree exercises a contaminated configure state and passes because the thing under test never happened. Procedure: `rm -rf /mnt/wsl/fixppbuild/build/linux-gcc-release`; `conan install . -pr conan/profiles/linux-gcc-release --build=missing -o "&:with_otel=False" -of build/linux-gcc-release`; `cmake --preset linux-gcc-release -DFIXPP_BUILD_OTEL=OFF`; re-run the minimal witness against **this** build. **It is expected to hold BY CONSTRUCTION, and must still be RUN**: `fixpp_otel` is a mandatory export member in every configuration, so the criterion is not protected by the target being absent — it is protected by the OTel-OFF stub being `add_library(fixpp_otel INTERFACE)` (`CMakeLists.txt:170`) with, per its own comment at `:167-169`, *"no headers, no SDK symbols, no link edges"*, contributing **none** of the seven `opentelemetry-cpp::*` names. The argument holds **only while the config is derived from the built target graph (I3) rather than from a hardcoded list**. This replaces the coverage the descoped clang-libc++ lane provided (spec Assumption 12) — dropping it without this substitute is **not acceptable**.

**Checkpoint US1**: `find_package(fixpp)` + `fixpp::fixpp` works from a staged prefix in three independently-failing tiers; the config is provider-agnostic and proven so; a telemetry-disabled build resolves. **STOP and VALIDATE here — this is the MVP.**

---

## Phase 4: User Story 2 — An operator installs fixpp from a platform-native package (Priority: P2)

**Goal**: a package artifact per (platform, toolchain, configuration, format) carrying headers, static libraries, CMake package config, dictionaries and attribution, with metadata identifying product, version, license and **what the consumer must supply**.

**Independent Test**: run the package step on a configured build tree; assert the produced artifacts exist, carry the expected name/version, and — for at least one generator — that extracting them yields the same file set as a direct staged install. Then `ctest -R "packaging::contents"` and `ctest -R "packaging::provenance"`.

> **What this story does and does not promise.** The package is **provider-agnostic**, but `find_dependency` **locates** rather than **provides** — so the operator must have all six dependencies available, and **two of them (`Crc32c`, `opentelemetry-cpp`) are rarely offered by a platform package manager**. The package description MUST say this plainly (T050) rather than let an operator discover it at `find_package(fixpp)`.

### Attribution — the one obligation classified as legal, not cosmetic

- [X] T045 [P] [US2] **Create the root `NOTICE` file** (FR-018b, `contracts/package-layout.md` §4) carrying the upstream's required acknowledgment sentence **verbatim**. ⚠️ **The single source is `dictionaries/QUICKFIX_LICENSE.txt:19-20` and the content MUST be derived from that anchor, never written from memory** — the sentence spans **two lines**, sits **indented** inside the license's clause 3, and is itself **enclosed in quotation marks**. No `NOTICE` exists in the repository today. **It is a TRACKED file at the repo root and WILL red the git-cleanliness gate** (`tests/codegen/codegen_build_graph_test.cmake:202-224` runs `git status --porcelain` and fails on any output) — unlike the build symlinks, which git never descends into. **Do NOT commit it standalone: commit it in the SAME commit as its install rule (T046), and never leave it uncommitted across a codegen-gate run.**
- [X] T046 [US2] **Attribution install rules** in `CMakeLists.txt` (FR-018b) — install **both** `dictionaries/QUICKFIX_LICENSE.txt` (discharging upstream clauses **1** *and* **2** — the packages ship the dictionary XML, which *is* the redistributed material in source form) **and** the root `NOTICE` (clause **3**) into the package doc directory. Commit together with T045.
- [X] T047 [US2] **Add the FIRST `PATTERN … EXCLUDE` clauses to `CMakeLists.txt:321-324`** (FR-012a sign-off, FR-013) — exclude `include/fixpp/core/test/` and `include/fixpp/transport/test/` (plus their `.gitkeep`s). This rule installs the **entire** `include/` tree unconditionally today and carries **no** exclusions at all; every description of it as "unfiltered" describes the pre-change state. **Recorded as a DELIBERATE CHANGE IN DELIVERED CONTENT, never as a silent omission** — two separate grounds: `mock_clock.hpp`'s backing `fixpp_mock_clock` is `FIXPP_BUILD_TESTS`-only and absent from any packaging build (the FR-012a defect in its purest form), while `mock_transport.hpp` is header-only test infrastructure that User Story 1 acceptance scenario 7 already presumes is outside the consumable surface. `include/fixpp/otel/`, the `detail/` subtrees, `session/quickfix_compat/` and the `.gitkeep`s all **ship** — do not over-exclude (`contracts/package-layout.md` §2a is authoritative per row).

### CPack

- [X] T048 [P] [US2] **New `cmake/FixppPackaging.cmake` — CPack core** (FR-011, FR-015): `include(CPack)` with per-platform generators — **DEB, RPM, TGZ** on Linux; **ZIP** on Windows. **Zero `CPACK_*` / `include(CPack)` matches exist anywhere today.** Factored into `cmake/` rather than growing the ~360-line top-level `CMakeLists.txt`, matching the existing `cmake/*.cmake` convention. One CPack invocation **per configured build tree** (research R10) — single-config presets plus the serial build-and-delete discipline make a multi-config generator worthless, and two coexisting configurations are forbidden by the storage budget.
- [X] T049 [US2] **Package metadata** in `cmake/FixppPackaging.cmake` (FR-018, FR-018c): product name, version (from `project(VERSION)`), description, **license = the project's own `AGPL-3.0`** (constitution Article V §1), maintainer. ⚠️ **FR-018c constrains the DESCRIPTION wording**: state third-party engine compatibility **as fact** and never imply endorsement by, or affiliation with, the upstream project — upstream clause 4 forbids using its names to endorse or promote derived products, and DEB/RPM description fields are otherwise easy to write carelessly. (Clause 5 — derived products may not be called "QuickFIX" nor carry it in their name — is discharged by the product name `fixpp`; recorded so it is not satisfied *by accident*.)
- [X] T050 [US2] **FR-018e obligations 2 and 3 — dependency metadata and honest availability**, in `cmake/FixppPackaging.cmake` and the consumer documentation. **Versions MUST be read from `conanfile.py` and cited, never transcribed from an anchor doc or a prior spec**: `pugixml/1.15` (`conanfile.py:66`), `asio/1.38.0` (`:67`), `crc32c/1.1.2` (`:68`), `openssl/3.6.2` (`:69`), `tomlplusplus/3.4.0` (`:77`), `opentelemetry-cpp/1.26.0` (**`:94`** — inside `requirements()` under `if self.options.with_otel`, **not** in the `:63-78` block). Mark ABI character per dependency: OpenSSL **ABI-stable**; asio and tomlplusplus **no ABI surface** (header-only); pugixml, Crc32c and `opentelemetry-cpp` **ABI-fragile** (compiled C++, no stated guarantee), with `opentelemetry-cpp` the acute case — largest surface (seven imported targets on `fixpp_otel`, `src/otel/CMakeLists.txt:36-45`, plus three on `fixpp_log_otlp`, `src/log/CMakeLists.txt:49-51`) and fastest-moving. **State plainly that `Crc32c` and `opentelemetry-cpp` are rarely distro-packaged and the consumer will likely have to supply them**, and that `opentelemetry-cpp` is required by **every** artifact this feature ships (all six configurations are OTel-ON). ⚠️ **Verify each pin against the registry before writing it** — anchor-doc pins go stale.
- [X] T051 [US2] **Artifact naming** (FR-017, I10) in `cmake/FixppPackaging.cmake` — names encode **product, version, platform, toolchain, configuration, and format**, and are **unique across the whole six-configuration × format matrix**. ⚠️ **Format is a NAMING dimension, not only a uniqueness dimension** (FR-017, corrected by the checklist audit): the three Linux formats of one configuration are distinct artifacts and each MUST be **independently identifiable by name** — including the file extension where the format supplies one — **not merely disambiguated by directory placement**. Encoding only the first five and relying on the extension plus a per-format directory ships three same-named DEB/RPM/TGZ artifacts, which is exactly what FR-017 forbids.
- [X] T052 [US2] **Staging and artifact output** (FR-020, FR-021, I20/I21) — CPack stages into `_CPack_Packages/` **inside the build tree**, so deleting the tree removes the staged files automatically; `CMAKE_INSTALL_PREFIX` must **never** point at a system location (violating this silently pollutes the host and is what makes the automatic cleanup hold). Finished artifacts are copied **outside every build tree**, to `/mnt/wsl/fixppbuild/artifacts/`, per the T004 retention rule.
- [X] T053 [US2] **Provenance stamping** (FR-021a, I24) in `cmake/FixppPackaging.cmake` — each artifact carries the **configuration**, the **source revision**, **AND** either (a) recorded worktree cleanliness or (b) a content hash over the build inputs. **Configuration + revision alone is INSUFFICIENT**: two packages built from the same commit either side of an *uncommitted edit* are indistinguishable under that pair — and an uncommitted edit is the **normal** state of a working branch, so it is the likely staleness case, not the exotic one. Option (a) reuses existing machinery: `git -C <source> status --porcelain` with a non-empty-output failure, exactly as `tests/codegen/codegen_build_graph_test.cmake:202-224` already does. **This is live, not theoretical**: `artifacts/` deliberately outlives the build-tree deletion cycle, so packages from earlier configurations and earlier source states accumulate alongside current ones.
- [X] T054 [US2] **Both Release and Debug of every in-scope toolchain** (FR-014) — confirm `cmake/FixppPackaging.cmake` is configuration-agnostic and that every preset in `CMakePresets.json` reaches the package step. Six configurations total: `linux-clang-{debug,release}`, `linux-gcc-{debug,release}`, `windows-msvc-{debug,release}`. Debug packages are **CI artifacts only**; none is published as a release artifact (spec Assumption 2, constitution Article V §5 — the WIP disclaimer stays in force). Debug archives carry DWARF and are ~17× the Release size on the measured library, so the serial build-and-delete discipline is mandatory here, not advisory.
- [X] T055 [US2] **Windows Debug symbol files** (FR-019) in `cmake/FixppPackaging.cmake` — the Microsoft toolchain emits debug information to **separate symbol files** alongside its static libraries, not inside the archive members, so a Windows Debug package built with Linux-shaped install rules ships **undebuggable** libraries. **There is no Linux counterpart to this rule.** ⚠️ **The exact artifact naming and location MUST be verified against real Microsoft-toolchain output during implementation, not assumed** — build in the **separate** Windows sandbox (never `/mnt/c/temp/fixpp`).
- [X] T056 [US2] **Document the Windows-installer seam** (FR-016) in `cmake/FixppPackaging.cmake` and `specs/084-packaging-cpack-export/quickstart.md` §8 — the generator selection must leave a documented place to add an installer format (WIX/NSIS) later **without requiring that format now**. Windows is ZIP-only for v1.0 by user decision.
- [X] T057 [US2] **Pin `FIXPP_BUILD_CODEGEN_TOOL=ON` for every packaged configuration + assert a generated typed header is present** (FR-011a). It is declared default `ON` at `CMakeLists.txt:184` and verified overridden nowhere in `CMakePresets.json` or `.github/`, so this is **latent today — pinned here so it stays latent**. It silently gates **two** things: the generated-typed-header install rule (`:345`) **and** the `add_test()` registering `fixpp::consumer::install-witness` (`:289-290`) — so a lane that ever set it OFF would ship a package with no typed headers **and** deregister the witness meant to catch it: a skip that reads as green.

### Package-content and provenance gates — enumerate produced artifacts, never read install rules

- [X] T058 [P] [US2] **New `tests/packaging/CMakeLists.txt` — the package-CONTENTS test** (FR-009, FR-012, FR-013, FR-018d, SC-004, SC-013), registered `RUN_SERIAL` with an explicit `TIMEOUT`. **Expected-artifact set is enumerated BY MEMBER KIND, not as a flat "the exported static libraries"** (analyze F2; source: `contracts/export-set.md` §2a). The export set is heterogeneous — roughly 5 of ~18 exported names produce no archive at all: `fixpp_capi_objects` is an **OBJECT** library (no archive; its own `OBJECTS DESTINATION` install shape); `fixpp_tap`, `fixpp_service`, `fixpp_dict_dispatch` and the `fixpp::fixpp` umbrella are **INTERFACE** (no compiled output); and **`fixpp_otel`'s kind is build-option dependent** — STATIC under OTel-ON, an empty INTERFACE stub under OTel-OFF (the same I3 configuration-dependence the bundle applies rigorously to the `find_dependency` derivation). A gate written from the flat phrase either passes trivially ("at least one `.a`" — cannot catch a genuinely missing archive) or fails spuriously (one archive per member — impossible for the INTERFACE/OBJECT ones). Derive the expected set per kind before authoring the assertion. It **enumerates produced package contents**; it MUST NOT inspect install rules — a rule whose pattern matches nothing yields a package missing content while looking entirely correct in CMake, and for the attribution set that is a **legal** deficiency, not a cosmetic one. **MUST BE PRESENT**: public headers · **at least one** generated per-version typed header (FR-011a) · the exported static libraries · `fixppConfig.cmake` + version file + targets files · the FIX dictionaries (FR-018a) · `dictionaries/QUICKFIX_LICENSE.txt` · **`NOTICE`**. **MUST BE ABSENT** — asserted as **SET EQUALITY over the exact 7-pattern denylist at `CMakeLists.txt:349-355`, NOT as a subset**: `_dispatch` · `vt11` · `messages` · `groups` · `validators` · `all.hpp` · `groups.hpp` — plus test executables, build-system scratch, and the two test-support header subtrees (T047). ⚠️ **A check written from the 078 five-pattern tail would pass a package leaking the build-tree-private reify bridge (`_dispatch/`) or the FIXT.1.1 tree (`vt11/`) while looking perfectly coherent** — `feedback_completeness_gate_exact_set_not_subset`. The `NOTICE` content check compares **whitespace-normalised against the pinned anchor `dictionaries/QUICKFIX_LICENSE.txt:19-20`**, never against a string typed into the test — the sentence spans two lines, is indented inside clause 3, and is itself quoted, so a grep written from memory silently never matches and the one obligation classified as legal becomes unfalsifiable. A package missing **any** of dictionaries / license / `NOTICE` **fails** (SC-013, I18).
- [X] T059 [US2] **SC-007b — PROVE THE EXCLUSION ASSERTION RED before accepting it as a gate.** Against the assertion in `tests/packaging/CMakeLists.txt` (T058): drop a file of a **new generated-artifact kind** under the codegen include root (`build/<preset>/_codegen/include/fixpp/<ver>/`), re-run the enumeration, and confirm the set-equality assertion **FAILS**. Record the red run in `.specify/decisions/084-packaging-cpack-export-verify.md`. The exclusion block is an allowlist-by-exclusion — anything new is installed by default, so a future emitter adding a directory type would silently leak unexported symbols into every package. **A gate never observed failing proves nothing** (`feedback_sanitizer_canary_must_be_proven_red`). This is a **different shape** from SC-007a's red evidence (T025) and must not be merged with it.
- [X] T060 [US2] **Package-PROVENANCE test** in `tests/packaging/CMakeLists.txt` (FR-021a, I24), `RUN_SERIAL` + explicit `TIMEOUT` — a witness fed a package from a **different configuration, a different source revision, or a different worktree state** must **FAIL**. Prove the failure, do not assume it. Depends on T053.
- [X] T061 [US2] **SC-005 — debug/release fidelity, each platform by its own check** (I14, `contracts/package-layout.md` §7). Linux: `readelf -S <installed>/libfixpp_core.a | grep -c debug_info` — Release **0** sections (measured 13 KB), Debug **present** (measured 228 KB, 4 sections). Neither the archiver nor the linker strips anything; the compiler simply never emits debug information in Release. Windows: assert the **separate symbol files** are present in the Debug package and absent from Release. Debug packages must yield **usable symbolication**, not merely non-empty sections.
- [X] T062 [US2] **SC-003 — all six in-scope configurations produce artifacts; count and names match the declared matrix EXACTLY, with no silent omissions.** Run the full matrix **one configuration at a time**, deleting each tree before the next, ordered `linux-gcc-release` (first — 3.4 GB, 0 deps) → `linux-clang-debug` (0 deps) → `linux-clang-release` (5 deps) → `linux-gcc-debug` (9 deps) → the two MSVC lanes in the **separate** Windows sandbox. Expected in `/mnt/wsl/fixppbuild/artifacts/` (Windows lanes into the separate sandbox's artifact directory): 4 Linux configurations × {DEB, RPM, TGZ} + 2 Windows × {ZIP} = **14** artifacts. Assert the count *and* the name set; record the observed listing in `.specify/decisions/084-packaging-cpack-export-verify.md`.
- [X] T062a [US2] **FR-011 telemetry-provenance gate — assert EVERY produced artifact was built `FIXPP_BUILD_OTEL=ON`, across all six configurations.** FR-011 explicitly *permits* an OTel-OFF dependency build as a development accelerator while the packaging logic is being written, and forbids one reaching a shipped artifact — but nothing currently enforces the second half. T003 pins OTel-ON only for the **new** `linux-gcc-debug` preset; the five pre-existing presets inherit the option default and are unasserted, so a leftover accelerator configure would ship silently and gcc-Debug-style asymmetry would reappear as the very defect Assumption 4 exists to prevent. Record the telemetry state in each artifact's provenance (FR-021a already carries configuration + source revision + worktree cleanliness — extend it), and **fail the packaging step** on any artifact whose provenance is not OTel-ON. Prove the gate red by packaging one deliberately OTel-OFF configuration and confirming it is rejected (the SC-007a/b rule applied to a non-SC-keyed gate: an assertion never observed failing proves nothing). **It registers a FIFTH packaging ctest — `packaging::telemetry-provenance` — and does NOT extend T060.** The two read the same provenance record but assert different things off **different red fixtures**: T060's is a package from a mismatched configuration / revision / worktree state; this one's is a package from a deliberately OTel-OFF configuration. Merged into one ctest, a failure cannot say which gate fired — the shape T059 refuses against T025 and T042 refuses against T041. Register it `RUN_SERIAL` with an explicit `TIMEOUT` like every other packaging test. Files: `cmake/FixppPackaging.cmake`, `tests/packaging/CMakeLists.txt`. Depends on T053 (it extends the provenance stamp) and runs across the T062 matrix (it is the only place all six configurations exist). *(Gap found by the Gate A checklist audit and escalated to the orchestrator; added 2026-08-01.)*
- [X] T063 [P] [US2] **SC-009a — the shipped-header disposition table check.** Assert that **every** `include/<subtree>` row in `contracts/package-layout.md` §2a carries a disposition drawn from **exactly two allowed values — `export` or `exclude`** — and that every `exclude` row additionally records a *deliberate change in delivered content*. ⚠️ **A row reading `OPEN` is a FAILURE of this criterion, not a pass** — the pre-round-2 form asserted only that each cell was *non-empty*, which `OPEN` satisfies, so the gate passed over a table with seven undecided rows. All seven were closed at the 2026-08-01 sign-off, so the criterion is **satisfiable** — but it stays **live**: a new `include/<subtree>` added later with no disposition, or a nested subtree whose disposition differs from its module's without its own row, still fails it (the granularity rule is module-level with break-out rows for differing dispositions).
- [X] T064 [US2] **FR-023 + SC-008 — measure the WHOLE-VOLUME high-water mark**, not the transient tree's peak. Three things share the one 64 GB volume: the build tree (22–25 GB in Debug), the 20 GB ccache, **and** the artifact directory that FR-021 deliberately preserves *past* the deletion cycle and FR-015 fills with three redundant Linux formats per configuration. Measure with `df /mnt/wsl/fixppbuild` sampled across the T062 matrix run; record the high-water mark in `specs/084-packaging-cpack-export/quickstart.md` §0 and in `.specify/decisions/084-packaging-cpack-export-verify.md`, and confirm it stays under 64 GB with the T004 retention rule applied. **Confirms FR-023**: every in-scope configuration is buildable and packageable one at a time, with the preceding tree deleted.

**Checkpoint US2**: fourteen artifacts across six configurations, each with correct contents, correct exclusions (all 7 patterns, set equality, proven red), attribution, **build provenance (configuration + revision + worktree state, T053/T060) *and* telemetry provenance (every artifact OTel-ON, T062a, gate proven red)**, and platform-correct debug information.

---

## Phase 5: User Story 3 — Every green CI run leaves downloadable package artifacts (Priority: P3)

**Goal**: a maintainer opens a completed CI run for a supported lane and downloads its package artifacts without rebuilding anything locally.

**Independent Test**: inspect a CI run for an in-scope lane and confirm package artifacts are attached with the expected names.

> ### ⚠️ US3 PRE-WORK FINDINGS — measured 2026-08-02, BEFORE any workflow edit. Read before starting T065–T068.
>
> Three facts about how CI actually runs tests change what this phase has to do. All three were found
> by checking T067's premise against the workflows rather than by trusting the task text.
>
> **1. `ctest --preset <p>` runs with NO label filter** (`tier1.yml:453`; the `testPresets` entries
> carry only `output`, no `filter` key). So the six new packaging witnesses do **not** wait to be
> opted into — as of this branch they run on **every tier1 lane**, including the sanitizer and
> coverage lanes, each doing a full stage-install plus a sub-project configure and compile. T067's
> "the other five lanes run the minimal tier only" is therefore **not the current behaviour** and has
> to be *made* true; it is not already true.
>
> **2. T067's lever is stale.** `FIXPP_BUILD_INTEROP_PERF` was the correct knob when T067 was written,
> because the real-client witness was to live in `perf/`. The T038–T040 amendment above made it a
> standalone project registered unconditionally under `tests/packaging/`, so that option no longer
> gates anything relevant. **T067's disposition is unchanged and still correct** — gate the heavy tier
> on `linux-gcc-release` only, exit non-zero on failure — only the lever changes. Note
> `consumer::install-witness` is registered in the ROOT `CMakeLists.txt`, not under
> `tests/packaging/`, so gating the subdirectory leaves it running on every lane; that is the
> intended "minimal tier".
>
> **3. The gating lane MUST `apt-get install -y rpm`.** `packaging::contents` invokes cpack with the
> **default** generator list, which is `DEB;RPM;TGZ` on Linux (`cmake/FixppPackaging.cmake:155`), so
> it needs `rpmbuild` — absent from `ubuntu-24.04` runners. (`provenance` and `telemetry-provenance`
> force `-G TGZ` and are unaffected.) T065's own artifact upload needs it too: FR-015 requires all
> three Linux formats. This is the same dependency that had to be installed locally on 2026-08-02.
>
> **Anti-false-green requirement for T067/T068**: assert the **count** of packaging tests that ran on
> the gating lane (`ctest -L packaging -N`, parse `Total Tests:`, fail if it is not the expected N),
> not merely the exit code. **`ctest -R <pattern>` exits 0 when the pattern matches nothing** —
> verified directly — so an exit-code-only check reports a lane green having run zero witnesses. The
> local matrix script carries exactly this pin for the same reason.

- [X] T065 [P] [US3] **Attach package artifacts on the in-scope Linux lanes** in `.github/workflows/tier1.yml` (FR-026) — run the package step after a green build and upload the DEB/RPM/TGZ artifacts, named per FR-017/T051.
- [X] T066 [P] [US3] **Attach package artifacts on the MSVC lanes** in `.github/workflows/tier2.yml` (FR-026) — same, for ZIP.
- [X] T067 [US3] **FR-026a / D3 — enable `FIXPP_BUILD_INTEROP_PERF` and gate the real-client witness on `linux-gcc-release` ONLY** in `.github/workflows/tier1.yml`. That single lane runs SC-011 and SC-012 as a **gate**; the other five in-scope lanes run the **minimal tier only**. `FIXPP_BUILD_INTEROP_PERF` is declared `OFF` at `cmake/ProjectOptions.cmake:10` and is enabled in **no** preset and **no** workflow today — so without this task SC-011/SC-012 are local-only and a witness that silently never runs **reads as green** (`feedback_ci_gate_observes_not_asserts_witness_skips_into_green`). `linux-gcc-release` is the chosen lane because it builds **zero** third-party dependencies from source (M1, Assumption 9), so gating the heaviest tier is bounded rather than all-or-nothing. **This must exit non-zero on failure — an observing step is not a gate.**
- [X] T068 [US3] **SC-010 — assert CI artifact names are unique and matrix-identifying** across all lanes in one run (platform, toolchain, configuration, **and format** — FR-017) — the `actions/upload-artifact` `name:` values in `.github/workflows/tier1.yml` and `.github/workflows/tier2.yml` must form a set with no duplicates across the whole matrix. A collision silently overwrites, which is a silent omission in SC-003's sense.

**Checkpoint US3**: every green in-scope CI run leaves uniquely-named, downloadable package artifacts, and exactly one lane gates the real-client tier.

---

## Phase 6: Polish & Cross-Cutting Concerns

- [X] T069 **Run every `quickstart.md` scenario end-to-end** (§1–§9) on `linux-gcc-release`, and confirm the full-acceptance table §9 rows 1–16 each have a landed, *passing* check. ⚠️ **The §9 table is SC-keyed and therefore does NOT reach every gate**: §5's `packaging::telemetry-provenance` (FR-011, T062a) has no row because it has no SC, so walking rows 1–16 alone would skip it. Run it explicitly, and confirm **all four** proven-red gates have a recorded red run — SC-007a, SC-007b, SC-016's red leg, and T062a's telemetry-provenance gate (the same four T075 clause (v) audits). Re-verify §1's conan invocation still runs verbatim.
- [X] T070 [P] **FR-024a — reconcile `.specify/architecture.md` §7.4 "CMake target layout"** with the target graph this feature exports, recording **per clause** which the feature *satisfies* and which it *supersedes*. §7.4 is verifiably stale in three respects: `:500` describes the module targets as `OBJECT` libraries combined into a final `fixpp` shared/static (they are all STATIC and no combined `fixpp` target exists); `:504`'s `fixpp::service-iface` does not exist (the target is `fixpp_service`, `src/service/CMakeLists.txt:7`); `:501`'s `fixpp::capi-objects` is `fixpp_capi_objects` with no namespaced alias. Root `CMakeLists.txt:319` names §7.4 as the authority for exactly this work and this feature introduces a **third** target shape (the `fixpp::fixpp` umbrella), so leaving it undone compounds the drift. D1 = Option A **agrees** with `:502`/`:503`, so this is a **correction**, not a contradiction to resolve. ⚠️ **`.specify/architecture.md` lives in THIS submodule and IS committable on this branch — it MUST NOT be swept into the parent-repo staging deferral below** (FR-024a says so explicitly).
- [X] T071 [P] **Record the verification-matrix taxonomy gap** (decision D2, spec Assumption 11) in the `/speckit-verify` decision record `.specify/decisions/084-packaging-cpack-export-verify.md`. The gate's step-0a taxonomy has **no bucket for a build-system-only change**, and this feature certainly produces one: **no `.cpp`/`.hpp` under `src/` or `include/` is touched — only CMake files inside those trees**. Whether the coverage/preset buckets key on a **path prefix** (`src/**`, which now matches) or on **compiled-source file type** (which still does not) **MUST NOT be assumed either way** — derive it from the real diff against the gate's own taxonomy at verify time. **The fallback is to be APPLIED, not re-derived**: state the gap explicitly, derive the matrix from the `tier1.yml` jobs that actually gate the touched paths, and route any skipped preset through the gate's **waiver mechanism with a paired rationale** — an unpaired skip is a failure by design. Note `/speckit-verify` is clang-only locally; gcc-release and MSVC are CI-only jobs.

### 🔴 CLOSE-OUT — PARENT-REPOSITORY edits (FR-024, FR-025). These CANNOT be committed on this branch.

> **Sequencing constraint, stated in FR-025 and the plan's Risks table.** Both tasks below target files in the **parent repository** (`/home/catalin/Work/Programming/Antreprenoriat/research/G19-fix-fpml-iso20022/`), a sibling of the library submodule. They must be **deferred to close-out and staged deliberately as a SINGLE parent-repo commit** — **never bundled with a submodule commit**, and **never swept in alongside another feature's parent-pointer bump**. The parent working tree also currently carries an unrelated in-flight modification, so stage by path, not by `git add -A`. **FR-024a (T070) is explicitly EXCLUDED from this deferral** — it lives in the submodule.

- [X] T072 **FR-024 — correct the verified-stale claims** in `PARENT: research/G19-fix-fpml-iso20022/remaining-work/packaging-cpack.md`: line 38 (*"no install() rules at all"* — **two** exist, at `CMakeLists.txt:321` and `:346`), line 79 (`project(VERSION)` *"unset / 0.0.0"* — it is **`0.0.1`**), and the drifted citations in line 43 (`:328` → `:321`; `:352-364` → `:345-357`). **AND** correct `PARENT: research/G19-fix-fpml-iso20022/REMAINING-WORK.md:44`, whose *"The `0→1` ABI freeze is deliberately HELD (see snapshot)"* is stale against the snapshot it points at: `REMAINING-WORK.md:7` records the freeze **CLOSED**, GA-frozen at `1.5.0` via PR #160 (`61edae6`, merged 2026-07-01), with `tools/capi_freeze.sha256` re-baselined. **That single stale line propagated into this feature's spec, plan and both contracts as a load-bearing rationale (RC-3, seven sites) — correcting it at source is what stops the next feature inheriting it.**
- [X] T073 **FR-025 — record the 2026-07-31 user scope narrowing** in `PARENT: research/G19-fix-fpml-iso20022/remaining-work/packaging-cpack.md`, so the descoped platforms read as a **decision** rather than as an unmet prerequisite: macOS/Tier-4 and its native installer (`:53-55`), Linux clang-libc++/Tier-3 (`:69`), *"OTel-enabled builds must feed the packages"* as a *requirement* (`:54` — note this does **not** mean packages are built OTel-OFF; all in-scope legs stay OTel-ON), and Windows installer formats beyond ZIP (FR-016 preserves the seam). **Land T072 and T073 as ONE parent-repo commit.**

### Mandatory close-out tasks (ALWAYS emit — Gate-B preconditions, Article XVII §8)

> ### T074 disposition — VACUOUS, with rationale (recorded, not skipped)
>
> **This feature owns NO OFFICIAL catalogue row, so there is nothing to flip.** Verified rather than
> assumed: `spec/feature-catalogue.md` and `spec/coverage-index.md` contain **zero** references to
> `084`, the catalogue contains **zero** `in-progress` rows (only `done`/`backlog`), and
> `spec.md` names no catalogue row.
>
> **Why:** the catalogue is organised entirely by *protocol and runtime capability* — Session Layer,
> Wire/Encoding, Data Dictionary, Application Messages, Transport, Logging, C ABI, Python Bindings,
> Service, NFRs. There is no build/packaging/tooling section, because a build-system change delivers
> no protocol behaviour. **078-precompiled-builder-libs set this precedent explicitly** ("no new
> OFFICIAL row, no new coverage entry — pure implementation-layout restructure"), and 084 is the same
> shape: it changes what is *produced and how it is consumed*, not what any message does on the wire.
>
> **⚠️ The counter-argument, recorded so the decision is deliberate rather than default:** `PY-005`
> **is** a packaging row ("pip-installable wheel (Linux x86_64 minimum) via CI"), admitted under
> *Python Bindings* because a wheel is how Python consumers acquire the library. By that reading, "a
> C++ package + `find_package(fixpp)`" is the equivalent acquisition surface for C++ consumers and
> could justify a row. It has no home in the current sections, and **adding one is a curatorial
> decision about the catalogue's scope, not a packaging decision** — so it is surfaced to the user
> rather than taken unilaterally. If accepted, the row belongs beside `PY-005` in shape: OFFICIAL,
> `done`, evidence = this feature's PR + the six packaging witnesses.

- [X] T074 [P] **Catalogue close-out**: flip **every** feature-owned OFFICIAL row in `spec/feature-catalogue.md` from `in-progress`/`backlog` → `done` (with the PR / evidence ref) **AND** add/update its matching `spec/coverage-index.md` entry.
> ### T001 and T065–T068 — dispositions for clause (i), recorded not implied
>
> **T001 (build-host preflight)** — **PERFORMED**, no durable artifact by nature: it is a `source
> env.sh` + `conan install` + `cmake --preset` sequence whose evidence is that the six-configuration
> matrix subsequently ran. Marked done on that basis; there is nothing to point at but the matrix.
>
> **T065 / T066 / T067 / T068 (CI)** — **IMPLEMENTED AND LANDED; CI-UNVERIFIED.** The workflow changes
> are committed (`tier1.yml`, `tier2.yml`) and their YAML parses, but **no CI run has exercised them**,
> because this branch has not been pushed as a PR. Marked done for the *deliverable*, with the
> verification explicitly **owed at Gate B's first CI run** — which is precisely the evidence Gate B
> exists to collect.
>
> This is deliberately the SAME standard applied to T054/T055 earlier ("code landed ≠ measured"), and
> the reason those two were held open until the MSVC legs actually ran. The difference here is that
> the missing evidence is *only obtainable in CI*, so holding the rows open would block T075
> permanently rather than record anything.
>
> **⚠️ What to watch on that first run**, since it is the first time any of it executes: the packaging
> tier runs on exactly ONE lane per tier (`linux-gcc-release`, `windows-msvc-release`) and each asserts
> a registered count of 6 BEFORE running — a lane reporting green having run zero witnesses is the
> failure mode these assertions exist to prevent, and it has never been exercised.

- [X] T075 **Feature-completeness audit — MUST BE THE FINAL TASK.** Assert against the merged tree that: (i) every `tasks.md` row here is `[X]` or carries an explicit waiver rationale — including the four open implementation choices (T005 rewrite form, T020 `FILE_SET` disposition, T004 retention rule, T024's `fixpp_capi_objects` `OBJECTS DESTINATION`-vs-wiring-change question), each of which must be **recorded as decided**, not left implicit; (ii) every spec **FR-001…FR-026a (42)** and **SC-001…SC-016 (18)** maps to a **landed test AND a landed implementation** — walk the traceability table below row by row; (iii) every feature-owned OFFICIAL catalogue row is `done` with a matching `coverage-index.md` entry; (iv) the T024 export re-measurement landed and was reconciled against `contracts/export-set.md` §2a; (v) **all four proven-red gates have a recorded red run** — SC-007a (T025), SC-007b (T059), SC-016's red leg (T042), and **T062a's OTel-ON telemetry-provenance gate** (FR-011; not SC-keyed — only SC-007a/b exist, so it is enumerated here by task, and this clause is the *only* place that would catch it unproven). Record the verdict (**100% or fully-waived**) in `.specify/decisions/084-packaging-cpack-export-verify.md` under a `## Completeness` section (or a sibling `.specify/decisions/084-packaging-cpack-export-completeness.md`). **`/gate-b` pre-flight HARD-BLOCKS without this record** (Article XVII §8 / pre-flight 4d).

---

## Traceability — every FR and SC → owning task(s)

### Functional Requirements (42)

| FR | Task(s) |
|---|---|
| FR-001 | T029 |
| FR-002 | T005, T006–T018, T029 |
| FR-002a | T005, T006–T018, T019 |
| FR-002b | T020 |
| FR-003 | T032, **T022** (`EXPORT_NAME`-matches-alias clause, added by checklist audit), **T026** (installed-imported-name vs alias-census assertion), **T034** (a witness linking `fixpp::capi` **by name**) |
| FR-004 | T030 |
| FR-005 | T031 |
| FR-006 | T031, T036 |
| FR-007 | T026 |
| FR-008 | T023, T024, T025, T026 |
| FR-008a | T023, T024, T025, T026 |
| FR-009 | T058, T059 |
| FR-010 | T027 |
| FR-010a | T038, T039 |
| FR-010b | T034, T039 |
| FR-010c | T030 |
| FR-011 | T048, T062, **T062a** (telemetry-provenance gate — added 2026-08-01 to close the checklist audit's escalation; T003 covers only the new `linux-gcc-debug` preset, T062a covers all six with a red-proof leg) |
| FR-011a | T057, T058 |
| FR-012 | T058 |
| FR-012a | T047, T063 |
| FR-013 | T047, T058 |
| FR-014 | T054, T062 |
| FR-015 | T048, T062 |
| FR-016 | T056 |
| FR-017 | T051, T062, T068 |
| FR-018 | T049 |
| FR-018a | T033, T058 |
| FR-018b | T045, T046, T058 |
| FR-018c | T049 |
| FR-018d | T058 |
| FR-018e | T043 (obl 1), T050 (obl 2+3), T041/T042 (obl 4) |
| FR-019 | T055, T061 |
| FR-020 | T052 |
| FR-021 | T052, **T004** (retention-rule-or-separate-storage clause, added by checklist audit) |
| FR-021a | T053, T060 |
| FR-022 | T002, T003 |
| FR-023 | T062, T064 |
| FR-024 | T072 |
| FR-024a | T070 |
| FR-025 | T073 |
| FR-026 | T065, T066 |
| FR-026a | T067 |

### Success Criteria (18)

| SC | Task(s) |
|---|---|
| SC-001 | T034 |
| SC-002 | T035 |
| SC-003 | T062 |
| SC-004 | T058 |
| SC-005 | T061 |
| SC-006 | T036 |
| SC-007a | T025 (**recorded red — closure**) |
| SC-007b | T059 (**proven red — exclusion set**) |
| SC-008 | T004, T064 |
| SC-009 | T027 |
| SC-009a | T063 |
| SC-010 | T068 |
| SC-011 | T039, T067 |
| SC-012 | T040, T067 |
| SC-013 | T058 |
| SC-014 | T037 |
| SC-015 | T044 |
| SC-016 | T041 (pass), T042 (**proven red**), T043 (obligation-1 grep) |

**Coverage: 42/42 FR, 18/18 SC.** No requirement or criterion is unowned.

---

## Dependencies & Execution Order

### Phase dependencies

- **Setup (T001–T004)** — no dependencies; T001 gates every build-touching task.
- **Foundational (T005–T028)** — depends on T001. **BLOCKS all three user stories.** The export does not *generate* until T005–T023 land, and the member set is not trustworthy until T024 completes.
- **US1 (T029–T044)** — depends on the whole Foundational phase.
- **US2 (T045–T064)** — depends on Foundational; T058/T060/T061/T062 additionally need the install rules from US1 (T032, T033) because packages are produced from them.
- **US3 (T065–T068)** — depends on US2 (there is nothing to upload before packages exist) and, for T067, on US1's T039/T040.
- **Polish (T069–T075)** — after all three stories. T075 is the **FINAL** task.

### Within Foundational

- T005 (rewrite-form decision) → T006–T018 (the 13 per-file edits, mutually **parallel**) → T019 (census assertion).
- T020 (`FILE_SET`) is independent of T005–T019 and can run in parallel with them; it is the **first** error the export produces, so it must land before T023.
- T021 (empty `$<INSTALL_INTERFACE:>`) is independent of the 13 files.
- T022 (`EXPORT_NAME`) touches the **same** files as T006–T018 ⇒ **sequential after them**, never in parallel.
- T023 (`install(TARGETS … EXPORT)`) requires T019 + T020 + T021 + T022.
- **T024 (RE-MEASURE) requires T023 and BLOCKS T025, T026, T027 and everything downstream.** If it changes the member set, T023's target list and every downstream target reference must be updated before proceeding.
- T025 (SC-007a evidence) naturally consumes T024's red output if option (ii) is taken.
- T028 is independent **to author**, not to run (analyze F4). Its assertion is about the *generated* export interface, so whichever mechanism it uses — a static read of the generated `fixppTargets*.cmake`, or an actual consumer compile — it cannot execute before the tree generates (T005–T023) and, for the consumer form, before T024 settles membership. Ordering is self-enforcing (a configure-time assertion simply cannot run in a tree that fails to configure), but T028 must state which mechanism it uses so "independent" is never read as licensing execution ahead of the export.

### Within US1

- T029 → T032. T030 ‖ T031 → T032. T032 + T033 → T034/T035 → T036/T037.
- T038 → T039 → T040.
- T032 → T041 → T042 ‖ T043 (same harness, same run; T042 and T043 are **separate deliverables**).
- T044 needs a **freshly deleted** tree — schedule it so it does not destroy a tree another task still needs.

### Within US2

- T045 → T046 (**committed together**). T047 independent.
- T048 → T049 → T050; T048 → T051, T052, T053, T054, T055, T056.
- T053 → T060. T058 → T059. T057 → T058.
- **T053 → T062a** (it extends the provenance stamp with the telemetry state). T062a ‖ T060 to author — **separate ctests, separate red fixtures**, never merged.
- T062 requires the whole packaging chain and runs the full serial matrix; T064 measures during T062. **T062a runs across that same matrix** — it asserts *every* produced artifact is OTel-ON, and the six configurations only exist together during T062, so it is executed with T062, not before it.

### Within US3

- T065 ‖ T066. T067 edits the same file as T065 ⇒ after it. T068 after T065/T066.

### Within Polish

- T069 after all stories. T070 ‖ T071 ‖ T074. T072 → T073 (**one parent-repo commit**). T075 **last, always**.

---

## Parallel Opportunities

**31 tasks are marked `[P]`.**

- **Setup**: T002 ‖ T003 ‖ T004 (3).
- **Foundational**: the 13 include-interface edits T006 ‖ T007 ‖ … ‖ T018 — different files, no shared dependency — plus T021 ‖ T028 (15). This is the single largest parallel block in the feature.
- **US1**: T030 ‖ T034 ‖ T035 ‖ T038 (4).
- **US2**: T045 ‖ T048 ‖ T058 ‖ T063 (4).
- **US3**: T065 ‖ T066 (2).
- **Polish**: T070 ‖ T071 ‖ T074 (3).

> **`[P]` is about AUTHORING, not about test runtime.** Every packaging ctest — **five**: contents, provenance, telemetry-provenance, real-client, clean-env — plus SC-007a's nested scratch configure if option (i) is taken, must still be registered **`RUN_SERIAL` with an explicit `TIMEOUT`**: they configure and build sub-projects, so concurrent runs collide with each other and with the git-cleanliness gate.

### Parallel example — the Foundational include-interface block

```bash
# After T005 fixes the rewrite form, launch all 13 file edits together:
Task: "src/core/CMakeLists.txt:13 — $<BUILD_INTERFACE:>/$<INSTALL_INTERFACE:> for fixpp_core"
Task: "src/core/sync/CMakeLists.txt:26 — same for fixpp_sync"
Task: "src/wire/CMakeLists.txt:21 — same for fixpp_wire"
Task: "src/dictionary/CMakeLists.txt:35 — same for fixpp_dictionary"
Task: "src/tls/CMakeLists.txt:20 — same for fixpp_tls"
Task: "src/transport/CMakeLists.txt:22-26 — same for fixpp_transport"
Task: "src/log/CMakeLists.txt:16-17,:42-43 — fixpp_log AND fixpp_log_otlp"
Task: "src/session/CMakeLists.txt:28 — same for fixpp_session"
Task: "src/otel/CMakeLists.txt:15-16 — same for fixpp_otel"
Task: "src/config/CMakeLists.txt:22-23 — same for fixpp_config_toml"
Task: "src/tap/CMakeLists.txt:7-8 — same for fixpp_tap"
Task: "src/service/CMakeLists.txt:10-11 — same for fixpp_service"
Task: "src/capi/CMakeLists.txt:25-26 — same for fixpp_capi_objects (NOT fixpp_capi)"
# Then T019 (census), T022 (EXPORT_NAME) — both SEQUENTIAL, they re-touch the same files.
```

---

## Implementation Strategy

### MVP first (Setup + Foundational + US1)

1. **Phase 1 Setup** — get `linux-gcc-release` configured and building with ccache; add the missing gcc-Debug profile+preset; decide the artifact retention rule.
2. **Phase 2 Foundational** — this is the heavy one and it is unavoidable. `install(EXPORT)` is a **generate-step hard error** today for two independent reasons, and the six sign-off export members are **read, not measured**. Do not start US1 until the tree generates *and* T024 has re-measured.
3. **Phase 3 US1** — `find_package(fixpp)` + `fixpp::fixpp`, all three witness tiers.
4. **STOP and VALIDATE**: `ctest -R "consumer::install-witness"`, `ctest -R "packaging::real-client"`, `ctest -R "packaging::clean-env"`, plus the SC-015 fresh-tree run. **This is the MVP** — it is the entire point of a `-dev` package, and the 078 follow-up B-10 explicitly folds in. Everything after it is packaging *around* this capability.

### Incremental delivery

- **+ US2** — CPack, formats, metadata, attribution, provenance, content gates. Story 1 defines the payload; Story 2 distributes it. A package whose contents cannot be consumed via `find_package` is a container with no product in it, which is why this ranks second.
- **+ US3** — CI artifact upload and the FR-026a lane gate. Real but derivative: it automates Story 2's output; deferring it costs convenience, not capability.
- **+ Polish/close-out** — architecture reconciliation (this branch), the verification-matrix taxonomy record, the parent-repo anchor-doc corrections (**separate commit**), catalogue close-out, and the completeness audit.

### Ordering the six-configuration matrix (US2/T062)

`linux-gcc-release` (3.4 GB, **0** deps) → `linux-clang-debug` (22 GB, 0 deps) → `linux-clang-release` (2.4 GB, **5** deps) → `linux-gcc-debug` (~25 GB, **9** deps) → MSVC ×2 in the separate Windows sandbox. **Smallest tree ≠ fastest to first package**: `linux-clang-release` has the smallest tree but five dependency builds. Delete each tree before the next; `artifacts/` is untouched by the deletion.

> **Development accelerator, with a hard limit**: `-o "&:with_otel=False"` cuts `linux-gcc-debug` from 9 dependency builds to 3 while the CMake is being written. **NEVER ship an artifact built that way** — every other leg is telemetry-enabled, and a single telemetry-disabled package would make gcc-Debug the sole one missing the OTel targets (spec Assumption 4).

---

## Notes

- `[P]` = different file, no incomplete dependency. `[US#]` maps a task to its story for traceability and appears only in Phase 3–5.
- Every packaging ctest: **`RUN_SERIAL` + explicit `TIMEOUT`**; any scratch configure writes **outside** the source tree.
- Commit `NOTICE` (T045) **with** its install rule (T046) — never standalone; it is a tracked root file and reds the git-cleanliness gate until committed.
- **Verify every version pin against the registry before writing it.** Anchor-doc and prior-spec pins go stale.
- Stop at any checkpoint to validate the story independently.
