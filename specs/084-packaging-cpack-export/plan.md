# Implementation Plan: Installable Packaging (CPack) + CMake Package-Config Export

**Branch**: `084-packaging-cpack-export` | **Date**: 2026-07-31 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/084-packaging-cpack-export/spec.md`

---

## Summary

Make fixpp consumable and distributable. Today the library installs headers only — there is no `install(TARGETS)`, no `install(EXPORT)`, and no CPack anywhere — so a downstream CMake project cannot `find_package(fixpp)` and an operator has no artifact to install.

This feature adds a `fixpp::` export set covering the static library closure a real FIX application links, generates the package-config files that make `find_package(fixpp)` work, and produces per-configuration packages (DEB/RPM/TGZ on Linux, ZIP on Windows) carrying the headers, libraries, config files, the FIX dictionaries, and their required upstream attribution.

The design is anchored on one decision (spec FR-010a): a headers-only umbrella is provably insufficient, because the project's own benchmark client links `fixpp_session`, `fixpp_transport`, and `fixpp_tls` directly. That drives everything downstream — the export must carry static targets, their link ordering, and their third-party dependencies, and only a **real client built out-of-tree against an installed package** can prove it works.

**Two prerequisites established at Gate A round 1, both of which enlarge the work.**

1. **The export cannot be written against the targets as they stand.** No module target has a build-tree/install-tree-discriminated include interface (`grep -rn BUILD_INTERFACE src/` → 0 matches), which makes `install(EXPORT)` a **generate-step hard error**; and `fixpp_transport` carries the repository's only `PUBLIC FILE_SET HEADERS`, a second independent blocker. Both are fixed in `src/*/CMakeLists.txt`, so this feature **does** touch build files under `src/` — see Project Structure and the Article IX row. Research R11, R12; FR-002a, FR-002b.
2. **A resolvable package is not an installable one — but it IS provider-agnostic.** *(Half of this prerequisite was retracted at Gate A sign-off, 2026-08-01.)* `src/` links only imported target names — **zero** `find_library(…)`, **zero** `.conan2` paths — so `install(EXPORT)` writes target names and every `find_dependency` resolves against the **consumer's** `CMAKE_PREFIX_PATH`, from any provider. What survives is the blindness: every existing witness inherits the producing build's Conan toolchain, so none can fail on a dependency the consumer would have to supply, nor on a generated config that baked in a build-host path. Research R13; FR-018e (four obligations), SC-016.

**The export set is now MEASURED, not read** *(Gate A round 2)*. Round 1 marked `research.md` R2's membership provisional and named an executed `install(TARGETS … EXPORT …)` generate run as a blocker on `/speckit-tasks`. **That run was executed** against a real configured `linux-gcc-release` tree (`research/reviews/orchestrator_084-packaging-cpack-export_gate_a_r1_measurements.md`). The reading missed **three** members across a **three-level** cascade; the measured minimum is:

`fixpp_core` · `fixpp_sync` · `fixpp_log` · `fixpp_wire` · `fixpp_dictionary` · `fixpp_tls` · `fixpp_transport` · `fixpp_session` · `fixpp_dict_dispatch_bridge` · `fixpp_dict_dispatch` · **`fixpp_otel`**

**⚠️ Scope qualifier, mandatory wherever this set is quoted**: it is the minimum for the closure that **excludes** the four then-open shipped-header subtrees (`fixpp_capi*`, `fixpp_config_toml`, `fixpp_tap`, `fixpp_service` — D1 / FR-012a). **All four became `export` at the 2026-08-01 sign-off**, and the **at least six** members they bring — `fixpp_capi`, `fixpp_capi_objects`, `fixpp_config_toml`, `fixpp_log_otlp`, `fixpp_tap`, `fixpp_service` — are **DERIVED BY READING, NOT MEASURED**. That is the method the measurement caught being wrong in three places across a three-level cascade, so **re-running the generate experiment once they are wired is an implementation obligation** (`contracts/export-set.md` §2a).

The run settled **membership for its scope**. It deliberately did **not** settle the include-interface **rewrite form** or the `FILE_SET` disposition: both are implementation choices (FR-002a, FR-002b), not further Gate-A experiments. What it *did* fix is the edit list, which the sign-off then widened — **13 of the 14** `src/*/CMakeLists.txt` files carrying the raw include path, holding **14** targets, enumerated in Project Structure below.

---

## Technical Context

**Language/Version**: C++23 (`CMAKE_CXX_STANDARD 23`, `CMakeLists.txt:11`). This feature writes **no C++ library code** — it is build-system and packaging work plus a witness adaptation. *(Precision added at Gate A round 1: "no library code" remains true, but the feature **does** edit CMake files inside `src/` — FR-002a/FR-002b. No `.cpp` or `.hpp` under `src/` or `include/` is touched.)*

**Primary Dependencies**: CMake ≥ 3.28 + Ninja (Article III §1); Conan 2 (Article III §2); CPack; `CMakePackageConfigHelpers`. Runtime third-party surface reaching consumers — **six**: OpenSSL, asio, pugixml, **Crc32c**, **opentelemetry-cpp**, and **tomlplusplus** (research R3). *(opentelemetry-cpp was corrected from conditional at round 2 — `fixpp_otel` is exported in every configuration and all six in-scope configurations are OTel-ON, so **every artifact this feature ships** requires it at consumer configure; it is absent only in the OTel-OFF build SC-015 exercises. tomlplusplus became unconditional at the 2026-08-01 sign-off, which exports `fixpp_config_toml`.)* **All six are resolved provider-agnostically** — every link edge names an imported target, with zero `find_library(…)` and zero `.conan2` paths under `src/`, `cmake/` or the root `CMakeLists.txt` — so Conan is how fixpp is built, not how anyone must consume it. Two of the six (`Crc32c`, `opentelemetry-cpp`) are rarely distro-packaged and a consumer should expect to supply them: FR-018e, research R13.

**Storage**: N/A — no persistent state. Package artifacts are build outputs.

**Testing**: ctest. **Three** witness tiers *(corrected at Gate A round 2 — this said "two", which survived the round-1 repair that added the third)*: the existing `fixpp::consumer::install-witness` (`CMakeLists.txt:289-311`) extended to `find_package`; a new real-client tier built from `perf/fixpp_perf_driver.cpp` against an installed package; and a new **clean-environment** tier (**SC-016**) that inherits nothing from the producing build. The third is mandatory — the first two are both handed the producer's Conan toolchain, so neither can fail on the dependency-provisioning gap (research R13).

**Target Platform**: Linux (gcc, clang) and Windows (MSVC). Six configurations: `linux-clang-{debug,release}`, `linux-gcc-{debug,release}`, `windows-msvc-{debug,release}`. macOS and clang-libc++ are descoped by user decision 2026-07-31.

**Project Type**: C++ library — packaging and CMake package-config export.

**Performance Goals**: N/A. No runtime code path changes; Article VIII budgets are untouched.

**Constraints**:
- **Storage**: one build tree at a time; peak ~30 GB against a 64 GB budget (spec Assumption 5, SC-008). Build → package → delete → next. **The budget is whole-volume**: the build tree shares the 64 GB with a 20 GB ccache *and* an artifact directory that FR-021 deliberately preserves across the deletion cycle and FR-015 fills with three redundant Linux formats per configuration. A retention rule or a different volume for `artifacts/` is required.
- **Ordering**: start with `linux-gcc-release` — the only configuration cheap on *both* axes (3.4 GB tree, zero third-party dependencies to build). `linux-clang-release` has the smallest tree but 5 dependencies to build from source; `linux-gcc-debug` needs 9 (research R5, spec Assumption 9).
- **Isolation**: Windows builds in a **separate** sandbox; the existing one holds unrelated in-flight state and must not be reused.
- **ccache**: the environment must be sourced *before* the first configure — the compiler launcher is baked in at first configure only.

**Scale/Scope**: 6 configurations × {DEB, RPM, TGZ} or {ZIP}. Export set = **11 measured members** (research R2) **+ at least 6 derived** by the 2026-08-01 D1/FR-012a sign-off (`contracts/export-set.md` §2a) + the `fixpp::fixpp` umbrella. The per-version dict INTERFACE targets are excluded (decided, research R2). Include-interface rewrites across **13** `src/*/CMakeLists.txt` files carrying **14** targets — every file that declares the raw include path except `src/core/test/CMakeLists.txt`, whose subtree is the one FR-012a `exclude` (FR-002a). No new library code.

---

## Constitution Check

*GATE: evaluated before Phase 0 and re-evaluated after Phase 1 design. Re-evaluation result is recorded at the end of this section.*

| Article | Requirement | Status | Basis |
|---|---|---|---|
| **III §1** | CMake ≥ 3.28 + Ninja; no alternative generators | **PASS** | Uses existing preset infrastructure; adds no generator. |
| **III §2** | Conan; every dep declared + pinned in `conanfile.py` | **PASS** | Adds no new third-party dependency. The `find_dependency` set is drawn from already-declared deps (research R3). |
| **III §3** | Conan profiles under `conan/profiles/` | **PASS — noted deviation** | Adds `conan/profiles/linux-gcc-debug`, absent from the article's list. **Not an amendment trigger**: the enumeration is already non-exhaustive — 4 libc++ profiles exist in-repo and are absent from it (research R5). |
| **III §5** | `tools/` is build-only; no runtime dependency at user-link time | **PASS** | Nothing under `tools/` enters the export set or any package. |
| **IV §1** | The C++ library is the primary public surface, consumed in-process via Conan | **PASS — advances it** | `find_package(fixpp)` is the missing half of this contract. |
| **IV §5** | v1.0 artifacts are **built but not published**; no registry upload | **PASS — with a stated gap** | On the *publication* axis: packages are produced and archived as CI artifacts only, a subset of what the article permits (it also allows attaching to GitHub releases, which this feature does not set up). On the *artifact-class* axis the earlier "deliberately narrower" wording was wrong: Article IV §5 names **Conan packages and Python wheels** (`.specify/constitution.md:144`); DEB/RPM/TGZ/ZIP appear nowhere in Article IV, so they are a **different class**, not a narrower slice. The article enumerates what is published and forbids no build output, so there is no conflict — but the gap is stated rather than implied (research R6). |
| **V §1** | AGPL-3.0 + commercial dual | **PASS** | Declared as the package license (FR-018). |
| **V §5** | WIP disclaimer stays until publishing is unblocked | **PASS** | Nothing published; disclaimer untouched. |
| **VII** | Testing requirements | **PASS** | **Three** witness tiers — minimal, real-client, and clean-environment — and every success criterion has a mechanical check. *(Corrected at Gate A round 2: this basis cell read "Two witness tiers", which is the constitutional testing justification a task list is derived from — so a testing task set derived from this Check could omit the third tier while the Check still read PASS.)* **The clean-environment tier (SC-016) is MANDATORY for this row's PASS**, not an optional extra: it is the only gate that observes FR-018e, and the only witness that inherits nothing from the producing build. Without it the other two are green against a package no operator can consume (research R13). SC-009a and SC-016 additionally each name the input that makes them **red** — a criterion with no failing state is not a gate. |
| **VIII** | Performance budgets & benchmarks | **N/A** | No runtime code path modified; §3 applies to hot-path changes. |
| **IX** | Coverage, sanitizers, static analysis | **UNRESOLVED — premise changed; must be derived at verify** | *Was* `N/A` on the basis that "this feature touches neither `src` nor `include`". **That basis is false**: FR-002a and FR-002b place edits in `src/*/CMakeLists.txt` (research R11, R12). No `.cpp`/`.hpp` under `src/` or `include/` is touched — only CMake files inside those trees. Whether the coverage bucket keys on a **path prefix** (`src/**`, which now matches) or on **compiled-source file type** (which still does not) is **not asserted here in either direction**; it is derived from the real diff against the gate's own taxonomy at `/speckit-verify` (spec Assumption 11, decision D2). **One of this row's two unknowns is now closed** *(Gate A round 2; finalised at the 2026-08-01 sign-off)*: the touched-file list is no longer open-ended or conditional. The edit set is **13** `src/*/CMakeLists.txt` files carrying **14** targets (Project Structure below) — every file declaring the raw include path except `src/core/test/CMakeLists.txt` — independent of which include-interface rewrite form is chosen. The row stays **UNRESOLVED** because the *other* unknown — path-prefix vs file-type keying — is genuinely open and is derived at verify, not because the file list is unknown. |
| **X** | ABI policy — C ABI is a versioned contract | **PASS** *(unconditional as of the 2026-08-01 sign-off — D1 = Option A)* | *Surface*: `include/fix/c_api*` is untouched — no declaration and no exported symbol changes — and the empty diff there is the freeze-invariant evidence. The C ABI is **GA-frozen at `1.5.0`** (`REMAINING-WORK.md:7`, PR #160), so packaging it is not an ABI commitment; it is a packaging decision under existing freeze controls. *Packaging*: **D1 = Option A** — export the **static** `fixpp_capi` only. **No shared-library variant is introduced**, so this row's original basis holds, and it now holds as a *consequence* of a made decision rather than as a claim that pre-decided one. Two hazards are therefore **not** live and are recorded so they are not re-opened by mistake: `fixpp_capi_shared` is gated on `FIXPP_BUILD_TESTS` (`src/capi/CMakeLists.txt:47-48`) so it does not exist in a packaging build, and its `WINDOWS_EXPORT_ALL_SYMBOLS ON` (`:70`) — deliberately not the "only `fixpp_*`" surface the POSIX version script enforces (`:73`), a gap the header-hash freeze does not cover — attaches only to that unexported artifact. Un-gating it is a decision for a feature that also builds the mechanism to hold it. Separately, the explicit non-goal on shared **C++ core** variants rests on those targets having *no* freeze mechanism, not on any held freeze. |
| **XIX §5 / XX §3** | Architecture-document conformance — `.specify/architecture.md` §7.4 "CMake target layout" | **PARTIAL — reconciliation owned by this feature** | *(Row added at Gate A round 1; the check previously had none, and `.specify/architecture.md` was cited nowhere in the bundle. Basis: Article XIX §5 — pages tied to public API surfaces must be regenerated when the surface changes, and §7.4 *is* the page describing the surface this feature ships; Article XX §3 — locked decisions record which architecture revision they rest on.)* Root `CMakeLists.txt:319` names **arch §7.4** as the authority for this exact work, and §7.4 is verifiably stale: `:500` describes the module targets as OBJECT libraries combined into a final `fixpp` shared/static (all are STATIC; no combined `fixpp` target exists), `:504`'s `fixpp::service-iface` does not exist, `:501`'s `fixpp::capi-objects` has no namespaced alias. This feature introduces a **third** target shape (`fixpp::fixpp`). FR-024a requires stating per clause what is satisfied and what is superseded, and filing the §7.4 correction — **in this submodule, on this branch**, unlike FR-024/FR-025's parent-repo targets. **D1 = Option A (2026-08-01) agrees with `:502`/`:503`**, so the reconciliation is a §7.4 *correction*, not a contradiction to resolve: this feature ships `fixpp::capi` as `:503` describes, exports `fixpp_service` (the target `:504` calls `fixpp::service-iface`), and supersedes `:500`/`:501`'s OBJECT-combined-into-one-`fixpp` model with STATIC modules plus a `fixpp::fixpp` umbrella. Option C, which would have contradicted `:502`/`:503` and deleted the structural half of the §8 boundary, was not taken. |
| **XV** | Banned patterns | **PASS** | No library code written. |
| **XVI** | Spec Kit workflow rules | **PASS** | specify → clarify (3 answered) → plan. Gate A precedes `/speckit-tasks`. |
| **XIX** | Documentation | **PASS** | FR-024/FR-025 correct verified-stale anchor-doc claims (parent repo, deferred to close-out); FR-024a reconciles `.specify/architecture.md` §7.4 (this submodule, this branch); consumer usage documented in `quickstart.md`. |

**Initial result: PASS.** No violations, so `Complexity Tracking` is empty and omitted.

**Post-Phase-1 re-evaluation: PASS, unchanged.** The Phase 1 design adds no dependency, no generator, no library code, and no C-ABI surface. The one design element that could have moved a gate — exporting static targets (FR-010a) — was checked against Article X and does not touch the C ABI or introduce shared libraries.

**Gate A round-1 re-evaluation: NOT a clean PASS.** Three rows moved and one was added:

- **IX** — from `N/A` to **UNRESOLVED**. Its stated basis ("touches neither `src` nor `include`") is false once FR-002a/FR-002b land in `src/*/CMakeLists.txt`. The disposition is not re-asserted in the other direction either; it is derived from the real diff at verify.
- **X** — from unconditional `PASS` to **PASS-for-the-surface, CONDITIONAL-on-D1-for-packaging**. The previous basis cell contained a claim ("no shared-library variants introduced") that is only true under two of D1's three options, which pre-decided an open decision inside a neutral table.
- **IV §5** — the "deliberately narrower" characterisation was wrong in kind and is replaced by a stated artifact-class gap.
- **XIX §5 / XX §3** — new row; architecture-document conformance had no coverage at all.

No article is *violated*, so `Complexity Tracking` remains empty. But this section can no longer be read as "all gates clear before `/speckit-tasks`": Article IX's disposition is genuinely open and Article X's is conditional on a decision that is deliberately not being made yet.

**One item carried, not cleared**: REMAINING-WORK item 15d (whether the upstream dictionary license's acknowledgment clause is compatible with AGPL-3.0) remains open pending counsel review. This feature discharges the *mechanical* attribution obligations only and must not be read as legal clearance (spec Assumption 10). Because 15d gates publishing and nothing here is published, it does not block delivery.

---

## Project Structure

### Documentation (this feature)

```text
specs/084-packaging-cpack-export/
├── plan.md               # This file
├── spec.md               # Feature specification (42 FR, 18 SC, 12 assumptions)
├── research.md           # Phase 0 — R1..R14, all decisions evidenced
├── data-model.md         # Phase 1 — entities and their invariants
├── quickstart.md         # Phase 1 — runnable validation guide
├── contracts/
│   ├── export-set.md     # What `fixpp::` exports, and the boundary rules
│   └── package-layout.md # What ships, where, and the attribution set
├── checklists/
│   └── requirements.md   # Spec quality checklist (16/16)
└── tasks.md              # Phase 2 — /speckit-tasks, NOT created here
```

### Source Code (repository root)

```text
CMakeLists.txt                       # export set, install(TARGETS/EXPORT), CPack include,
                                     #   dictionary + attribution install rules, AND the
                                     #   first PATTERN exclusions on :321-324 (the two
                                     #   test-support subtrees -- FR-012a/FR-013 sign-off)

# ── src/*/CMakeLists.txt — REQUIRED by FR-002a/FR-002b ──
#   Every export-set member needs $<BUILD_INTERFACE:>/$<INSTALL_INTERFACE:>; today
#   NONE has either (`grep -rn BUILD_INTERFACE src/` -> 0). install(EXPORT) is a
#   generate-step hard error without it. Research R11/R12.
#   14 files under src/ declare the raw path; after the 2026-08-01 D1/FR-012a
#   sign-off THIRTEEN are DEFINITE, carrying FOURTEEN targets. The one file that
#   stays out is src/core/test/CMakeLists.txt (its subtree is the FR-012a exclude).
#   Whether the rewrite is per-module (these files) or centralised from the root
#   is an OPEN IMPLEMENTATION CHOICE (FR-002a) -- NOT awaiting a generate run;
#   that run happened and explicitly did not settle it. BOTH forms edit here.
src/core/CMakeLists.txt              # :13  fixpp_core        -- definite
src/core/sync/CMakeLists.txt         # :26  fixpp_sync        -- definite
src/wire/CMakeLists.txt              # :21  fixpp_wire        -- definite
src/dictionary/CMakeLists.txt        # :35  fixpp_dictionary  -- definite; also the
                                     #   dispatch-bridge/dispatch export disposition (R2).
                                     #   NOTE fixpp_dict_dispatch_bridge itself needs NO
                                     #   edit: its only include dir is PRIVATE (:82-84)
src/tls/CMakeLists.txt               # :20  fixpp_tls         -- definite
src/transport/CMakeLists.txt         # :22-26 fixpp_transport -- definite; ALSO the
                                     #   FILE_SET HEADERS disposition, :53-60 (FR-002b)
src/log/CMakeLists.txt               # TWO targets: :16-17 fixpp_log, and :42-43
                                     #   fixpp_log_otlp -- which the sign-off pulls INTO
                                     #   the export set via fixpp_config_toml's PRIVATE
                                     #   edge (src/config/CMakeLists.txt:43-45). Derived,
                                     #   not measured -- export-set.md §2a
src/session/CMakeLists.txt           # :28  fixpp_session     -- definite
src/otel/CMakeLists.txt              # :15-16 fixpp_otel      -- definite. Exported
                                     #   UNCONDITIONALLY by this feature; note the PUBLIC
                                     #   edge from fixpp_session sits inside
                                     #   if(FIXPP_BUILD_OTEL) (:55-57), so membership is
                                     #   held by the export, not by the closure
src/config/CMakeLists.txt            # :22-23 fixpp_config_toml -- DEFINITE (FR-012a =
                                     #   export). Adds tomlplusplus to the find_dependency
                                     #   set and drags fixpp_log_otlp in
src/tap/CMakeLists.txt               # :7-8  fixpp_tap        -- DEFINITE (FR-012a = export;
                                     #   also forced by fixpp_capi_objects PUBLIC, capi:36)
src/service/CMakeLists.txt           # :10-11 fixpp_service   -- DEFINITE (follows D1)
src/capi/CMakeLists.txt              # :25-26 fixpp_capi_objects -- DEFINITE (D1 = Option A).
                                     #   The edit is on fixpp_capi_OBJECTS: fixpp_capi
                                     #   (:43-45) declares NO include dirs of its own.
                                     #   fixpp_capi_objects is an OBJECT library, so its
                                     #   install(TARGETS) needs an OBJECTS DESTINATION --
                                     #   a shape no other member has (export-set.md §2a).
                                     #   fixpp_capi_shared (:47-48) stays test-gated and
                                     #   UNEXPORTED -- recorded limitation, not omission
src/core/test/CMakeLists.txt         # :15 fixpp_mock_clock -- NOT EDITED. Its subtree
                                     #   include/fixpp/core/test/ is the FR-012a exclude
#   cmake/Codegen.cmake:589-590 already gives fixpp_dict_dispatch $<BUILD_INTERFACE:>,
#   so that member needs no edit either -- which is why neither it nor the bridge
#   appeared in M4.

.specify/architecture.md             # FR-024a -- reconcile §7.4 "CMake target layout"
                                     #   COMMITTABLE ON THIS BRANCH (submodule), unlike
                                     #   FR-024/FR-025's parent-repo targets

cmake/
├── Codegen.cmake                    # dict-target include interfaces; the per-version
│                                    #   fixpp::dict::<ver> targets are EXCLUDED from
│                                    #   the export set (decided, R2) -- no edit needed here
├── FixppPackaging.cmake             # NEW — CPack config, generators, metadata, provenance
│                                    #   incl. FR-018e dependency metadata
└── fixppConfig.cmake.in             # NEW — find_dependency set (derived per FR-010c,
                                     #   incl. Crc32c) + include of fixppTargets

conan/profiles/
└── linux-gcc-debug                  # NEW — Debug sibling of linux-gcc-release

CMakePresets.json                    # NEW preset: linux-gcc-debug

NOTICE                               # NEW — upstream clause-3 acknowledgment (verbatim)

tests/consumer/
├── CMakeLists.txt                   # hand-rolled discovery -> find_package(fixpp)
├── consumer_witness.cpp             # include BOTH a core header AND a generated typed header
└── run_consumer_witness.cmake       # harness reused unchanged

tests/packaging/                     # NEW
├── CMakeLists.txt                   # package-content enumeration (7-pattern exclusion SET
│                                    #   equality) + provenance checks (incl. worktree
│                                    #   cleanliness, FR-021a)
├── run_real_client_witness.cmake    # builds the real client out-of-tree vs installed pkg
└── run_clean_env_witness.cmake      # NEW (SC-016) — consumer configured against a prefix
                                     #   carrying NOTHING from the producing build:
                                     #   no conan_toolchain.cmake, no source-tree path

perf/
└── CMakeLists.txt                   # packaged-variant path: no src//tests/ includes,
                                     #   no network fetch, shipped-dictionary load

.github/workflows/tier1.yml          # artifact upload for in-scope Linux lanes; ALSO
                                     #   FIXPP_BUILD_INTEROP_PERF=ON + the real-client
                                     #   witness on linux-gcc-release ONLY (FR-026a / D3)
.github/workflows/tier2.yml          # artifact upload for the MSVC lanes
```

**Structure Decision**: no new source tree, and **no C++ source or header file is edited** — but the file list above now includes `src/*/CMakeLists.txt`, which the pre-Gate-A version did not. That correction is the single most consequential one from Gate A round 1, because Article IX's `N/A`, spec Assumption 11, and decision D2 were all built on the claim that this feature touches nothing under `src/`. Packaging logic is factored into `cmake/FixppPackaging.cmake` rather than growing the ~360-line top-level `CMakeLists.txt`, matching the existing `cmake/*.cmake` convention. The only new test directory is `tests/packaging/`, holding checks that operate on **produced artifacts** rather than on compiled code — a genuinely different subject from every existing test directory.

> **What is now DEFINITE, and what is still open** *(restated at Gate A round 2 — this previously read "still not final … settled by research R2's blocking generate run", which pointed a now-discharged event at questions it never answered).*
>
> **Definite — 13 files, 14 targets** *(nine at round 2 from the measured export set; the four conditional files became definite at the 2026-08-01 sign-off)*: `src/core`, `src/core/sync`, `src/log` (**two** targets), `src/wire`, `src/dictionary`, `src/tls`, `src/transport`, `src/session`, `src/otel`, `src/config`, `src/tap`, `src/service`, `src/capi`. **Independent of which rewrite form is chosen** — per-module or centralised, these are the targets whose include interface must change. Three members need **nothing**: `fixpp_dict_dispatch_bridge` (include dir is PRIVATE, `src/dictionary/CMakeLists.txt:82-84`), `fixpp_dict_dispatch` (already has `$<BUILD_INTERFACE:>`, `cmake/Codegen.cmake:589-590`), and `fixpp_capi` (declares no include dirs at all, `:43-45` — the edit is on `fixpp_capi_objects`). One file **stays out**: `src/core/test/CMakeLists.txt`, whose subtree is the FR-012a `exclude`. **Article IX and D2 re-derive from this list.**
>
> **Still open, and not by measurement**: the **rewrite form** (per-module vs centralised) and the `FILE_SET` **disposition** are implementation choices under FR-002a / FR-002b, to be recorded when made — and `fixpp_capi_objects`' `OBJECTS DESTINATION` is a third, new at sign-off. **What IS waiting on a run**: the six export members the sign-off derives are derived **by reading**, and the generate experiment must be re-run once they are wired (`contracts/export-set.md` §2a). That is not a Gate-A blocker — it is an implementation obligation, and it is the same discipline that caught RC-1.

---

## Phase 1 Design Summary

Full detail in `contracts/`. The load-bearing decisions:

0. **Two generate-blocking preconditions come first** (research R11, R12; FR-002a, FR-002b). No module target has an include interface `install(EXPORT)` will accept, and `fixpp_transport` carries an uninstalled `PUBLIC FILE_SET`. Neither is polish: each is a hard error at generate, so nothing below can be written until both are dispositioned. Both are fixed in `src/*/CMakeLists.txt`.

1. **Export set = the `$<LINK_ONLY:>`-expanded closure a real client links** (research R2, FR-008a) — **MEASURED, eleven members**: `fixpp_core`, `fixpp_sync`, `fixpp_log`, `fixpp_wire`, `fixpp_dictionary`, `fixpp_tls`, `fixpp_transport`, `fixpp_session`, `fixpp_dict_dispatch_bridge` (unconditional, PUBLIC edge from `fixpp_dictionary`), `fixpp_dict_dispatch` (link-closure-only, **empty** install interface), and **`fixpp_otel`** (exported in every configuration — held by this feature exporting it unconditionally, since the PUBLIC edge from `fixpp_session` sits inside `if(FIXPP_BUILD_OTEL)`, `src/session/CMakeLists.txt:55-57`; what varies is its content). The per-version `fixpp::dict::<ver>` INTERFACE targets are **excluded (decided, research R2)** — they add no post-install capability (both install rules share `${CMAKE_INSTALL_INCLUDEDIR}`) and `fixpp_dict_vt11` would export over denylisted content. `fixpp::fixpp` is the umbrella. Builders and validators stay **out** (FR-007). **⚠️ This is the measured floor, not the export set.** The 2026-08-01 sign-off exports all four then-open shipped-header subtrees, adding **at least six more members** — `fixpp_capi`, `fixpp_capi_objects` (OBJECT, different install shape), `fixpp_config_toml`, `fixpp_log_otlp`, `fixpp_tap`, `fixpp_service` — **derived by reading, not measured** (`contracts/export-set.md` §2a), with a standing obligation to re-run the generate experiment once they are wired. Round 1's "must be replaced by an executed generate run" is **discharged for the eleven**; it now applies afresh to the six.

2. **The export set is configuration-dependent, so the config file is generated from what was built** — never a hardcoded list. What varies is not *whether* `fixpp_otel` is a member — it always is — but whether the OTel SDK's **seven** imported targets are inside it, keyed on `if(TARGET opentelemetry-cpp::api)` (`src/otel/CMakeLists.txt:36`) rather than on `FIXPP_BUILD_OTEL`. Option state alone therefore does not determine the `find_dependency` set, which is why SC-015 exercises the config against a telemetry-disabled build — and why it holds there **by construction**: the OTel-OFF stub is an empty INTERFACE library with no link edges (`CMakeLists.txt:167-170`), so it contributes none of the seven names.

3. **Link ordering is the export's responsibility** (research R7). Declared target dependencies let CMake produce a correct link line; consumers never restate the ordering the existing harnesses hand-roll.

4. **Three witness tiers, because they fail differently** (research R8, R9, R13). The **minimal** tier now exercises the link interface too — switching it from four hand-listed archives to `fixpp::fixpp` gives it the whole closure — so it catches config-package regressions, a missing `find_dependency`, and the `FILE_SET` blocker. The **real-client** tier is the only one that links and *runs* a working FIX application. The **clean-environment** tier (SC-016) is the only one that inherits nothing from the producing build; without it, the other two are green against a package no operator can consume.

5. **Attribution spans five upstream clauses, and this feature discharges all five** (FR-018b, contract §4): the shipped license text covers clauses 1 and 2, `NOTICE` covers clause 3, FR-018c covers clause 4, and the product name covers clause 5. Clause 3's verbatim sentence is pinned to `dictionaries/QUICKFIX_LICENSE.txt:19-20` so its check is falsifiable.

6. **Every content guarantee is verified by enumerating produced artifacts**, never by reading install rules (FR-018d, SC-004, SC-013). An install rule matching nothing yields a deficient package that looks correct in CMake. Content checks assert the exclusion set as **set equality over all 7 patterns** (`CMakeLists.txt:349-355`), not as a subset of the 078 five.

7. **The package is provider-agnostic by construction, and must be kept that way** (research R13, FR-018e — decided 2026-08-01). `src/` links only imported target names, so `install(EXPORT)` writes target names and every `find_dependency` resolves against the consumer's `CMAKE_PREFIX_PATH`. There is no provisioning *model* to choose. There are four obligations: keep build-host paths out of the **installed** config; declare a tested-against version and ABI character per dependency, read from `conanfile.py`; say honestly which dependencies a consumer will have to supply (`Crc32c`, `opentelemetry-cpp`); and prove it with **SC-016**, which now has a pass state and an explicit red leg.

8. **Every shipped header has a library behind it** (FR-012a / D1 = Option A, decided 2026-08-01). The export set gains `fixpp_capi` (+ the forced `fixpp_capi_objects`), `fixpp_config_toml` (+ the forced `fixpp_log_otlp`), `fixpp_tap` and `fixpp_service`. The single deliberate subtraction is the two **test-support** header subtrees, which acquire the first `PATTERN` exclusions `CMakeLists.txt:321-324` has ever carried.

---

## Decisions routed to Gate A

> **✅ ALL THREE CLOSED at sign-off, 2026-08-01.** D1 = **Option A** (export the static `fixpp_capi`; the FR-012a class resolves to `export`). FR-018e = **provider-agnostic package** (the three-option table is withdrawn as a mis-framing). D3 = **option (iii)**, gate the real-client witness on `linux-gcc-release` only (now **FR-026a**). D2's framing was already correct and its premise already resolved. Rationale is recorded under [Gate A → Sign-off decisions](#sign-off-decisions-user-2026-08-01); the option analyses below are retained as the record the decisions were made against.

**D1 — C-ABI packaging scope. ✅ CLOSED 2026-08-01 → Option A.** *(Framing below retained; it is what the decision was made against.)* `include/fix/c_api.h` + `include/fix/c_api/` ship today because `CMakeLists.txt:321-324` installs the whole `include/` tree, but no C-ABI library is exported — a shipped header with nothing behind it. Article IV §2 makes this surface the legal isolation boundary for dual licensing. Options and implications are in [`spec.md` → Open Questions](./spec.md#open-questions).

**Framing corrected at Gate A round 1 in three ways**, all of which change how the options rank:

- **The evidence base was missing its binding document.** `.specify/architecture.md` §7.4 already specifies this exact surface — `:502` puts `<fix/c_api.h>` in the umbrella's exposed surface, `:503` names `fixpp::capi` the target *"C-ABI consumers link"*, `:504` adds a service-interface target, and §8 `:523`/`:526` makes the AGPL isolation boundary structural — and root `CMakeLists.txt:319` cites §7.4 as the authority for this work. **Option C would contradict `:502`/`:503` and delete the structural half of the §8 boundary**, so it requires an Article XX-visible architecture amendment, not a spec non-goal. (§7.4 is itself partly stale — FR-024a — so it is evidence to weigh, not an oracle. But deciding without it risks landing on the one option that contradicts it.)
- **Option B's risk statement was a month stale.** It read "sits closer to an ABI commitment … REMAINING-WORK A-1 deliberately holds the `0→1` freeze". The freeze is **CLOSED**, GA-frozen at `1.5.0` (`REMAINING-WORK.md:7`, PR #160, merged 2026-07-01). B is therefore *"ship an already-frozen, already-gated C ABI"* — materially **lower** risk than stated.
- **B's real constraints are different from the stated one, and they are standing.** `fixpp_capi_shared` exists **only under `FIXPP_BUILD_TESTS`** (`src/capi/CMakeLists.txt:47-48`) and is built `WINDOWS_EXPORT_ALL_SYMBOLS ON` on Windows (`:70`) — deliberately not the `[const §X.2]` "only `fixpp_*`" surface the POSIX version script enforces (`:73`), because the artifact is documented as test-only with no shipped Windows consumer (`:64-68`). Choosing B means un-gating a test-only target **and** resolving an Article X §2 symbol-surface problem the header-hash freeze does not cover.

**D1 is one row of a class** — `include/fixpp/config/`, `tap/` and `service/` have the same shipped-header-without-library defect (FR-012a, `contracts/package-layout.md` §2a), and `include/fixpp/service/` is **bound** to D1 because `fixpp_service` links `fixpp_capi`. They were decided together, as required. **Outcome**: the class resolves to `export`; the only `exclude` in the exhaustive table is the pair of test-support subtrees, which are not a "backing target" case at all.

**D2 — Verification-matrix fallback, in writing. Its triggering premise is now known-false, and the fallback fires by default.** Assumption 11 *expected* the full preset matrix and coverage to be N/A because no `src/**` or `include/**` file is touched. **That premise no longer holds**: FR-002a and FR-002b place edits in `src/*/CMakeLists.txt` (research R11, R12). The framing of D2 needs no change — writing the fallback in advance rather than re-deriving it at verify time is right, and routing skips through the gate's paired-rationale waiver is right. What changes is its conditionality: **the taxonomy gap is expected, not hypothetical.** The verify gate's step-0a taxonomy has no bucket for a build-system-only change, and this feature will certainly produce one; whether the coverage bucket keys on the `src/**` *path prefix* (now matching) or on *compiled-source file type* (still not matching) is unresolved and must not be assumed either way. **Fallback, to be applied rather than re-derived**: the verification record states the taxonomy gap explicitly, and the matrix is derived from the `tier1.yml` jobs that actually gate the touched paths. Any preset skipped goes through the waiver mechanism with a paired rationale — an unpaired skip is a failure by design, not a shortcut.

**D3 — Is the real-client witness a CI gate or local-only? ✅ CLOSED 2026-08-01 → option (iii): gate it on `linux-gcc-release` only.** That lane enables `FIXPP_BUILD_INTEROP_PERF` and runs SC-011/SC-012; the other five run the minimal tier. Recorded as **FR-026a** so a derived task list carries it rather than leaving it in plan prose — which is the `feedback_ci_gate_observes_not_asserts_witness_skips_into_green` shape this decision exists to avoid. *(Framing below retained.)* `FIXPP_BUILD_INTEROP_PERF` is declared `OFF` by default at `cmake/ProjectOptions.cmake:10` and is enabled in **no** `CMakePresets.json` preset and **no** `.github/workflows/` lane, so SC-011/SC-012 are local-only unless something turns it on. A witness that silently does not run reads as green — `feedback_ci_gate_observes_not_asserts_witness_skips_into_green`. The binary, the hazard, and the unacceptable outcome are all correctly posed.

**Three options, not two** *(third added at Gate A round 1)*:

| | Option | Note |
|---|---|---|
| (i) | Enable it in every in-scope CI lane | Highest coverage, highest cost — the real-client tier is the heavy one |
| (ii) | Declare SC-011/SC-012 local-verify obligations that MUST appear in the verification record | Cheapest; carries the skip-reads-as-green hazard unless the record is genuinely enforced |
| (iii) | **Gate it in CI on the single cheapest lane** — `linux-gcc-release`, which per Assumption 9 builds **zero** third-party dependencies | Middle path; converts an all-or-nothing choice into a bounded one. **✅ CHOSEN, 2026-08-01 — FR-026a.** |

**Rider that applies whichever way it goes** (P2-I): SC-012's "succeeds with the source tree unavailable" form is impossible by construction — the real client and its `CMakeLists.txt` both live in the source tree. So a "local-verify obligation" answer must **name the equivalent check**, not merely restate the criterion. SC-012 now names it: configure from outside the tree with `CMAKE_PREFIX_PATH` set only to the staged prefix, then assert zero source-root paths in `compile_commands.json` and the link line.

---

## Risks

| Risk | Mitigation |
|---|---|
| A witness passes against a **stale** package from an earlier configuration — `artifacts/` deliberately outlives build trees | FR-021a: provenance (configuration + source revision **+ worktree cleanliness or a build-input hash**) stamped on each artifact; witness fails on mismatch. Configuration+revision alone cannot separate two packages built either side of an uncommitted edit |
| **The package is green in every witness and still unusable by an operator.** The consumer sub-build inherits the producer's Conan toolchain, so no pre-existing witness can observe either a dependency the consumer must supply or a build-host path baked into the installed config | FR-018e's four obligations + **SC-016**, a witness that inherits nothing from the producing build, with a **pass** state and a proven **red** leg. *(The stronger form of this risk — "all deps are Conan-only, so the package is Conan-consumption-only" — was **retracted** at the 2026-08-01 sign-off: the exported graph names only imported targets, so the package is provider-agnostic. Research R13.)* |
| **The six sign-off export members are wrong, because they were READ rather than measured.** The identical method missed three members across a three-level cascade at round 1, and `fixpp_capi_objects` / `fixpp_log_otlp` were both second-pass discoveries | Re-run the `install(TARGETS … EXPORT …)` + generate experiment once the additions are wired, peeling errors one at a time as the round-1 measurement did, and reconcile against `contracts/export-set.md` §2a. This is an implementation obligation with a named artifact, not a review note |
| **`fixpp_capi_objects` is an OBJECT library and needs an install shape no other member has** — `install(TARGETS)` on it requires an `OBJECTS DESTINATION`, and it cannot be dropped from the export set by demoting the edge (`$<LINK_ONLY:>` entries are export requirements too) | Named as the first question the re-measurement must answer (`export-set.md` §2a). If the destination is unacceptable, the alternative is a wiring change on `fixpp_capi`, not a quiet exclusion |
| **`install(EXPORT)` fails at generate on day one** — no module target has an acceptable include interface; `fixpp_transport` has an uninstalled `FILE_SET` | **No longer a risk — an executed fact, and now a scoped work item.** The Gate A round-2 measurement ran exactly this and captured both errors verbatim: the `FILE_SET` fires first (M3), then all eight reachable module targets (M4). FR-002a / FR-002b are the fixes. The edit list was **nine** files at Gate A round 2 (the measured export set); the 2026-08-01 D1/FR-012a sign-off widened it to **13 of the 14** `src/*/CMakeLists.txt` files carrying the raw include path, holding **14** targets — see Project Structure, which is authoritative for the current count. The residual risk is only that the *chosen* rewrite form is wrong, which the first real generate after the edits will show |
| The artifact directory grows unbounded on the same 64 GB volume as the build tree and a 20 GB ccache | Assumption 5 / SC-008 now measure whole-volume high-water mark; a retention rule or a different volume for `artifacts/` is required |
| The generated config unconditionally requires the telemetry dependency, breaking every telemetry-disabled consumer | SC-015 — exercise the config against a telemetry-disabled build, **in a fresh or deleted build folder** (`quickstart.md` §6): reconfiguring an OTel-ON tree leaves `opentelemetry-cpp_DIR` cached and `src/otel/CMakeLists.txt:36` keys on `if(TARGET opentelemetry-cpp::api)`, so a reused tree can exercise a contaminated state. The one defect class the descoped libc++ lane would have caught. *(Round 2: `fixpp_otel` is a **mandatory** member, so this holds by the stub having no link edges — not by the target being absent)* |
| A future emitter adds a generated-artifact kind that escapes the exclusion set, leaking unexported symbols into packages | FR-009 machine-checkable coherence assertion, keyed to the exact **7-pattern** set as set equality; **SC-007b** requires it be proven to fail on a deliberately broken input before counting as a gate |
| The export-closure gate is written as a ctest assertion that can never fire — CMake enforces closure at **generate**, so a broken tree produces no build system for ctest to run in | **SC-007a** splits that leg out and requires either a nested scratch configure asserting the exit code and diagnostic, or a recorded red generate run as the evidence |
| An implementer "fixes" the apparent `FIXPP_BUILD_OTEL` propagation gap and introduces a real ODR mismatch | Research R4 records that the current non-propagation is **correct** — the definition never reaches public headers |
| Anchor-doc corrections (FR-024/FR-025) get bundled into a submodule commit | They target the **parent** repo and cannot be committed on this branch; deferred to close-out and staged deliberately |
| Windows work disturbs unrelated in-flight state in the shared sandbox | Use a distinct sandbox path; never reuse the existing one |
| **The new root `NOTICE` file reds the hidden git-cleanliness gate.** `tests/codegen/codegen_build_graph_test.cmake:202-224` runs `git status --porcelain` and fails on any output. `NOTICE` is a new **tracked** file at the repo root — unlike the build symlinks (invisible because git never descends into an ignored directory), it *will* appear until committed | Commit `NOTICE` in the same change that adds its install rule; never leave it uncommitted across a codegen-gate run |
| **The new packaging tests configure and build sub-projects** — concurrent runs collide with each other and with the git-cleanliness gate. This now covers **five**: contents, provenance, **telemetry-provenance** (T062a — its own ctest, its own red fixture; never merged into provenance), real-client, and the SC-016 clean-environment witness — plus SC-007a's nested scratch configure if that option is taken | Mark every one `RUN_SERIAL` with an explicit `TIMEOUT`, mirroring the existing consumer witness (`TIMEOUT 300` at `CMakeLists.txt:308-310`, driven via `cmake -P`), per `feedback_tree_mutating_test_must_run_serial_vs_gitclean_gate`. Any scratch configure must write **outside** the source tree |
| **A packaging lane sets `FIXPP_BUILD_CODEGEN_TOOL=OFF`** — it silently gates both the generated-typed-header install (`CMakeLists.txt:345`) *and* the `add_test()` registering the consumer witness (`:289-290`), so the package loses its typed headers and the witness that would catch it deregisters | FR-011a pins it ON for any packaged configuration and requires a package-content assertion that at least one generated typed header is present. Latent today — it is `ON` by default (`:184`) and overridden nowhere |
| A ZLIB link failure in the real-client witness gets "fixed" by adding `find_dependency(ZLIB)` | R3's reasoning is that the Conan-provided OpenSSL imported target carries compression transitively — true for these presets, not a general truth. If that link fails, first check whether the **imported target regressed**; adding the dependency would mask it |

---

## Gate A

- Round 1 applied 2026-07-31: Codex P1=4 P2=4 P3=1; Opus post-judging P1=8 P2=12 P3=4; rewrite addresses root causes RC-1 (the export set was derived from the link graph and never from the export *mechanics* — no `INTERFACE_INCLUDE_DIRECTORIES`, no `FILE_SET`s, no `$<LINK_ONLY:>` expansion), RC-2 ("what ships" and "what is exported" were scoped from two different sources and never intersected), RC-3 (one stale anchor premise — the C-ABI `0→1` freeze — propagated to seven sites), RC-4 (every witness inherits the producer's environment, so none can fail on an environment defect), RC-5 (`plan.md`'s Project Structure listed the edits the author intended, not the edits the design forces, and Article IX hung off that list). Reviews: research/reviews/codex_084-packaging-cpack-export_gate_a_review.md, research/reviews/opus_084-packaging-cpack-export_gate_a_adversarial_review.md.

### Round 1 — what each root cause changed

| Root cause | Change |
|---|---|
| **RC-1** | `research.md` R2 re-headed as **PROVISIONAL** with a blocking prerequisite (an executed `install(TARGETS … EXPORT …)` + generate run whose output replaces the reading-derived table); new R11 (no include interface anywhere in `src/` — `grep -rn BUILD_INTERFACE src/` → 0) and R12 (`fixpp_transport`'s `FILE_SET`); FR-002a, FR-002b, FR-008a; `data-model.md` I1/I4/I4a; `export-set.md` B0/B1 and a provisional membership banner. `fixpp_dict_dispatch_bridge` corrected from "conditional" to unconditional, and the bridge/dispatch pair given an explicit disposition (export both, dispatch with an **empty** install interface). Per-version `fixpp::dict::<ver>` targets **decided out**. |
| **RC-2** | New `package-layout.md` **§2a** — one row per `include/<subtree>` with `backing target / in export set / disposition`, every row non-empty; FR-012a and SC-009a make it a requirement; research R14 records the census. D1 reframed as one row of that class rather than a standalone question. |
| **RC-3** | Freeze premise corrected at all seven sites (`spec.md` Clarifications Q2, explicit non-goal, Assumption 3, D1 Option B; `plan.md` Article X row; `export-set.md` §6; `package-layout.md` §1) plus FR-024 now carries the `REMAINING-WORK.md:44` correction at source. The **no-SHARED-core-variants** non-goal re-derived on a ground that survives: the C ABI has freeze machinery (version script, header-hash baseline, symbol golden) and the core C++ targets have none. Q2's *conclusion* (keep `0.0.1`) unchanged — only its rationale. |
| **RC-4** | New third witness tier: FR-018e (dependency-provisioning decision + package metadata) and **SC-016** (a consumer check inheriting nothing from the producing build); research R13; `data-model.md` I6a / I23a and a three-column E6 table; `export-set.md` §1 precondition. SC-002 and SC-012 restated to claim only what they can prove. |
| **RC-5** | `plan.md` **Project Structure** now lists the `src/*/CMakeLists.txt` files (definite vs conditional-on-D1/FR-012a), plus `.specify/architecture.md`; Article IX moved `N/A` → **UNRESOLVED**; Article X made conditional on D1; new **XIX §5 / XX §3** architecture-conformance row; spec Assumption 11 and decision D2 restated ("the taxonomy gap is expected, not hypothetical"). |

### Round 1 — decisions

- **D1 (C-ABI packaging scope)** — remains **OPEN and deliberately undecided**, per the review's verdict. Framing improved only: `architecture.md` §7.4/§8 added to the evidence, Option C flagged as requiring an Article XX-visible amendment, Option B's stale risk model corrected and its two real constraints (`FIXPP_BUILD_TESTS` gating; `WINDOWS_EXPORT_ALL_SYMBOLS`) made standing.
- **D2 (verification-matrix fallback)** — framing unchanged and correct; its premise resolved by RC-5. Restated as firing by default rather than conditionally.
- **D3 (real-client witness: CI gate or local-only)** — confirmed **decidable at this gate without further evidence**. A third option (gate on `linux-gcc-release` only) added and recommended; the SC-012 rider absorbed. **Not closed by the rewriter** — the decision belongs to the gate.
- **Bridge/dispatch export disposition** — settled *in the bundle* (research R2), not routed. The review asked for a disposition, not a routed decision.

### Round 1 — disagreements

- **Codex finding #9 (P3, the "34 FR" counting convention) — OVERRULED. Not a defect; nothing renumbered on its strength.** Codex reported the `plan.md` "34 FR" parenthetical as an inconsistency. The Opus judge recounted and confirmed it was arithmetically correct at review time: FR-001…FR-026 = 26, plus FR-010a/b/c (3), FR-018a/b/c/d (4), FR-021a (1) = **34**; likewise "15 SC" (SC-001…SC-015) and "12 assumptions", and the checklist's "16/16" (4 + 8 + 4 boxes). Suffixed FRs are established convention across this repository's spec corpus, so reporting a correct count as a finding is noise. **The suffixed FRs were therefore NOT renumbered in this rewrite**, and every requirement added here follows the same convention (FR-002a, FR-002b, FR-008a, FR-011a, FR-012a, FR-018e, FR-024a; SC-007 split into SC-007a/SC-007b; SC-009a; SC-016). The parenthetical was updated to **41 FR, 18 SC, 12 assumptions** to reflect the additions — an arithmetic refresh, not an adoption of the finding.

### Round 1 — carried forward → ✅ CLOSED at round 2 by measurement

**`research.md` R2's export-set membership could not be verified by a documentation pass**, so round 1 corrected R2's demonstrably false claims (the "conditional" bridge, the missing `$<LINK_ONLY:>` rule, the missing `Crc32c`, the untouched `FILE_SET`), marked the table **provisional with a named blocking prerequisite**, and refused to write a fresh membership list — which would have repeated the exact defect the review identified.

**The orchestrator executed that prerequisite between rounds 1 and 2** — `install(TARGETS … EXPORT fixppTargets)` + `install(EXPORT …)` appended as scratch to `CMakeLists.txt`, `cmake -S . -B build/linux-gcc-release` re-run, errors peeled one at a time (each blocker masks the next), scratch reverted, tree left byte-identical. Record: `research/reviews/orchestrator_084-packaging-cpack-export_gate_a_r1_measurements.md`.

**Round 1's refusal is vindicated, not merely defensible.** The reading was wrong in three places (`fixpp_dict_dispatch_bridge`, `fixpp_otel`, `fixpp_dict_dispatch` — a three-level cascade) and blind to two mechanism-level blockers (`FILE_SET`, absent include interface). **This item no longer blocks `/speckit-tasks`.** What the run did *not* settle — the include-interface rewrite form, the `FILE_SET` disposition — are implementation choices, not evidence gaps.

### Round 2 — what was applied

- Round 2 applied 2026-07-31: Codex P1=1 P2=4 P3=1; Opus post-judging P1=1 P2=7 P3=1; rewrite addresses root causes **RC-A** (a round-1 correction applied where the review pointed and never swept to the sibling documents restating the same claim — R7's retracted link-order cause surviving verbatim in `export-set.md` B5, and the two-tier→three-tier repair missing `spec.md`'s primary site and both `plan.md` sites) and **RC-B** (a criterion written to check the *shape* of a table rather than its *content* — SC-009a passing on `OPEN`, SC-016 having no failing state under FR-018e (c)); and absorbs the executed export measurement across the bundle, replacing round 1's PROVISIONAL/candidate framing with the measured eleven-member set **plus its scope qualifier**, separating the discharged generate run from the still-open include-interface rewrite, fixing the definite `src/*/CMakeLists.txt` edit list at **nine**, and stating `fixpp_otel`'s consequence — mandatory `find_dependency(opentelemetry-cpp)` for every shipped artifact, keyed on SDK presence rather than option state, with SC-015's by-construction argument made explicit. Reviews: research/reviews/codex_084-packaging-cpack-export_gate_a_2_review.md, research/reviews/opus_084-packaging-cpack-export_gate_a_2_adversarial_review.md. Measurements: research/reviews/orchestrator_084-packaging-cpack-export_gate_a_r1_measurements.md.

### Round 2 — what each root cause changed

| Root cause | Change |
|---|---|
| **P1 — absorb the measurement** | R2 re-headed **MEASURED** with the executed eleven-member set, the three-level cascade, and a **mandatory scope qualifier** (the four open subtrees are unmeasured and each adds its own closure level). Swept every PROVISIONAL/candidate site across `plan.md`, `research.md`, `data-model.md`, `export-set.md`, `package-layout.md`, `checklists/requirements.md`. **(a)** The four sites pointing the discharged generate run at the *still-open* include-interface rewrite (`research.md` R11 and its open-items row, `plan.md` Project Structure header and its "not final" note) now separate settled membership from open implementation work. **(b)** The definite edit list is fixed at **nine** files — `src/otel` added, absent from M4 only because `fixpp_otel` joined the set at M5 — and the two members needing *no* edit are named. **(c)** The scope qualifier travels with the set at every site. |
| **RC-A — unswept siblings** | `export-set.md` B5 no longer carries the causal explanation R7 retracted at round 1, and now records the second (static-archive-cycle) link hazard where an implementer will look for it. "Two witness tiers" → **three** at `spec.md` User Story 1 (the primary site, which Codex missed), `plan.md` Technical Context, and the Article VII basis cell — which now names **SC-016 as mandatory** for its PASS. |
| **RC-B — gates that observe rather than assert** | **SC-009a** now requires `disposition ∈ {export, exclude}` and **fails** on any `OPEN` row; `package-layout.md` §2a declares `OPEN` a routed gate-blocking decision that must name its decision and options. **SC-016** now names the *specific* expected failure and expected successes under FR-018e (c), so it cannot swallow an uninstalled config, a malformed version file, or a dangling target. |
| **N2 — `fixpp_otel`'s dependency consequence** | `opentelemetry-cpp` restated as **unconditional for every shipped artifact** (all six in-scope configurations are OTel-ON) across `research.md` R2/R3, `export-set.md` §2/§4, `data-model.md` E2/I3/I8, `plan.md`, and FR-018e — keyed on `if(TARGET opentelemetry-cpp::api)` rather than the option, which is what makes I3 load-bearing. SC-015's survival is now argued **by construction** (the OTel-OFF stub is an empty INTERFACE with no link edges) instead of assumed. |
| **P2s — quickstart, Assumption 8** | `quickstart.md` §1 and §6 now use the tracked-profile/`-of` invocation CI uses (`.github/workflows/tier1.yml:411-414`); §6 additionally mandates a **deleted** build folder so SC-015 cannot run against a cached OTel-ON configure state. `spec.md` Assumption 8 — the last site saying the gcc Debug configuration reuses the release profile with an override — now matches research R5, `plan.md`, and the measurement: a **tracked** `conan/profiles/linux-gcc-debug`. |

### Round 2 — decisions

- **D1, D2, D3, and FR-018e** — all four remain exactly where round 1 left them. **Nothing was decided by this rewrite.** Framing and evidence improved only: the FR-018e table now carries the `opentelemetry-cpp` weight (14 Conan packages, mandatory for every artifact), and each `OPEN` row of the FR-012a class now names the decision it is routed to and that decision's options.

- Round 3 (final) 2026-08-01: Codex P1=0 P2=0 P3=2; Opus post-judging **P1=0 P2=0 P3=4** — **CONVERGED**, no third rewrite needed (budget 2/2 spent at round 2). All nine round-2 findings verified CLOSED at the file rather than against the claim; the §2a repair was confirmed **not** to have pre-decided D1; ~90 newly-introduced `file:line` cites re-read and exact; FR-010's `Args` gate re-derived from source — `fixpp_dict_dispatch`'s empty install interface is *forced* by the denylist at `CMakeLists.txt:349`, not merely asserted. The 4 residual P3s were applied as sign-off edits (D1 routing framing at `plan.md`/`spec.md`; `data-model.md` I11a's stale "four subtrees" → the seven `OPEN` rows; `research.md`'s present-tense restatement of SC-009a's retracted "no row is empty" form; `spec.md`'s raw-include-path file count 12 → 14, of which 9 are definite). Reviews: research/reviews/codex_084-packaging-cpack-export_gate_a_3_review.md, research/reviews/opus_084-packaging-cpack-export_gate_a_3_adversarial_review.md.

### Round 3 — carried into sign-off

Converged **with decisions still owed by the gate** — these were not rewrite failures. **All three were decided by the user on 2026-08-01; see [Sign-off decisions](#sign-off-decisions-user-2026-08-01) above.**

- ✅ **D1 / the FR-012a class** — the seven `OPEN` rows in `contracts/package-layout.md` §2a → six `export`, one `exclude`.
- ✅ **FR-018e** — rewritten: the package is provider-agnostic; the three-option table is withdrawn.
- ✅ **D3** — option (iii), gate on `linux-gcc-release` only (FR-026a).

Standing obligations for the implementer: `fixpp_dict_dispatch` must carry an **empty** `$<INSTALL_INTERFACE:>`; the `find_dependency` set is **derived from the target graph**, never from option state; SC-015 runs in a **fresh** build folder; **and — added at sign-off — the generate experiment must be RE-RUN once the six derived export members are wired** (`contracts/export-set.md` §2a), with `fixpp_capi_objects`' `OBJECTS DESTINATION` as the first question it answers.

### Round 2 — disagreements

**None.** The judge overruled nothing this round and moved no severity; every finding on the round-2 list was applied. This subsection is stated rather than omitted so its emptiness is a recorded result, not an oversight.

### Sign-off decisions (user, 2026-08-01)

Gate A converged at round 3 with **P1=0 P2=0** and three decisions still owed. All three are decided here. This is **not** a fourth rewrite round: nothing was re-reviewed, no finding was applied, and the bundle's evidence base is unchanged except where a decision forced a consequence.

**D1 / the FR-012a class — EXPORT the backing targets (D1 = Option A).**

Export `fixpp_capi` (**static**), `fixpp_config_toml`, `fixpp_tap`, `fixpp_service`, so **every shipped header has a library behind it**. `include/fixpp/otel/` ships; the `detail/` subtrees ship; the two **test-support** subtrees are excluded. Rationale:

- It is the disposition the architecture already specifies. `.specify/architecture.md:503` names `fixpp::capi` *"the C-ABI consumer target … C-ABI consumers link this"*, and §8 `:523` puts `include/fixpp/service/` in *"the public C++ plugin surface"*. Option C would have contradicted `:502`/`:503` and required an Article XX-visible amendment; Option A needs none.
- It is the smallest coherent fix on the C-ABI axis: the frozen surface is untouched, no shared variant is introduced, and Article X's row becomes an unconditional PASS.
- The `otel` row resolves on a ground that is independent of the OTel question entirely: `include/fixpp/session/engine.hpp:32` includes `<fixpp/otel/trace_context.hpp>` **unguarded**, so excluding the subtree would break a public session header in every configuration. The stub case is latent for what ships (Assumption 4) and is what SC-015 exercises.
- The `detail/` rows resolve per file: three of the four headers are reached from public headers (`transport_factory.hpp:33`, `tls/pinset.hpp:22`, `session/engine.hpp:31`, `message_store.hpp:28`, and `atomic_shared_ptr.hpp:20`) and therefore **must** ship; the fourth (`session/detail/validate_compid_filesystem_safety.hpp`) is reached only from `src/`, and ships on the separate ground that excluding one file would be a delivered-content regression for zero benefit.
- The **one exclusion** is the test-support pair, and it is a deliberate change in delivered content, not an omission: `mock_clock.hpp`'s backing `fixpp_mock_clock` is `FIXPP_BUILD_TESTS`-only and absent from any packaging build, and `mock_transport.hpp` is header-only test infrastructure that User Story 1 scenario 7 already presumes is outside the consumable surface.

> **⚠️ The export members this adds are DERIVED, NOT MEASURED — and that must not be quietly forgotten.** The eleven in research R2 are the output of an executed generate run. These are the output of *reading* `target_link_libraries`, which is the exact method the measurement caught being wrong in three places across a three-level cascade. The reading yields **at least six**: `fixpp_capi`, **`fixpp_capi_objects`** (forced — `fixpp_capi` has no sources or include dirs and links it PUBLIC, `src/capi/CMakeLists.txt:43-45`; it is an **OBJECT** library needing an `OBJECTS DESTINATION`), `fixpp_config_toml`, **`fixpp_log_otlp`** (forced — `src/config/CMakeLists.txt:43-45` under a guard live in every OTel-ON configuration), `fixpp_tap`, `fixpp_service`. Two of the six were second-pass discoveries, which is itself the evidence that the reading is not to be trusted as final.
>
> **Standing implementation obligation**: once these are wired, **re-run the `install(TARGETS … EXPORT …)` + generate experiment** — peeling errors one at a time, as the round-1 measurement did — and reconcile the result against `contracts/export-set.md` §2a before `tasks.md` is treated as closed. The same discipline that caught RC-1.

**Consequences propagated**: `tomlplusplus` becomes an unconditional `find_dependency` (research R3, `export-set.md` §4, FR-010c); the definite `src/*/CMakeLists.txt` edit list grows from **9 to 13 of the 14** files carrying the raw include path — `src/core/test/CMakeLists.txt` is the one that stays out — holding **14 targets**, because `src/log/CMakeLists.txt` now carries two and the `src/capi` edit is on `fixpp_capi_objects`, not on `fixpp_capi`; `CMakeLists.txt:321-324` acquires its first `PATTERN` exclusions; SC-009a becomes satisfiable and no §2a row reads `OPEN`.

**Known limitation, recorded rather than omitted**: `fixpp_capi_shared` is gated on `FIXPP_BUILD_TESTS` (`src/capi/CMakeLists.txt:47-48`), so a packaging build with tests off has **no** shared C-ABI library and none is exported. Consumers wanting *dynamic* AGPL isolation under Article IV §2 remain unserved, and the `WINDOWS_EXPORT_ALL_SYMBOLS ON` symbol-surface problem (`:70`) attaches only to that unexported artifact.

**FR-018e — the package is PROVIDER-AGNOSTIC; the three-option table is withdrawn.**

The round-1 framing (vendor / distro `Depends:` / Conan-consumption-only) inferred a **consumption** constraint from a **build** convention. Verified: `src/` links **only imported target names** — `OpenSSL::*`, `asio::asio`, `pugixml::pugixml`, `Crc32c::crc32c`, `tomlplusplus::tomlplusplus`, `opentelemetry-cpp::*` — with **zero** `find_library(…)` calls and **zero** `.conan2` paths across `src/`, `cmake/` and the root `CMakeLists.txt`. So `install(EXPORT)` writes target names, and each `find_dependency(X)` is a plain `find_package(X)` against the **consumer's** `CMAKE_PREFIX_PATH`. Conan is how fixpp is built, not how anyone must consume it. FR-018e is rewritten to four obligations: (1) keep build-host paths and provider-specific config names out of the **installed** config; (2) declare a tested-against version and ABI character per dependency, **read from `conanfile.py` and cited** (`:66`–`:69`, `:77`, and `:94` for `opentelemetry-cpp/1.26.0` inside `requirements()`); (3) state honestly that `Crc32c` and `opentelemetry-cpp` are rarely distro-packaged and a consumer will likely have to supply them; (4) prove it via SC-016.

**SC-016 is rewritten with a pass state**: `find_package(fixpp)` must **succeed** against a prefix the producing build's package manager did not fill, and its **red** leg is explicit — remove one **named** dependency and it must fail with that dependency's `find_dependency` diagnostic and no other. The round-2 "assert *which* failure" refinement survives as the red leg rather than as the expected outcome. What survives entirely from R13 is the blindness: every pre-existing witness inherits the producer's toolchain and can observe none of this.

**D3 — gate the real-client witness on `linux-gcc-release` only (option (iii)).**

One lane enables `FIXPP_BUILD_INTEROP_PERF` (`cmake/ProjectOptions.cmake:10`, `OFF` by default and enabled nowhere today) and gates SC-011/SC-012; the other five run the minimal tier. `linux-gcc-release` because it builds **zero** third-party dependencies from source (Assumption 9), so the heaviest tier is gated at the lowest cost. Recorded as **FR-026a** rather than left in plan prose — a decision that lives only in a plan is a witness that silently never runs.

**Counts after sign-off**: **42 FR** (FR-026a added), **18 SC**, 12 assumptions.

---

## Next

**`/speckit-tasks 084-packaging-cpack-export`** — pipeline step 5. **Nothing blocks it.**

Gate A ran at pipeline step 4 (`/gate-a`, immediately after `/speckit-plan`) and converged at round 3 with P1=0 P2=0. Its three owed decisions — D1 / the FR-012a class, FR-018e, and D3 — were **all closed by the user on 2026-08-01** and are recorded under [Gate A → Sign-off decisions](#sign-off-decisions-user-2026-08-01). D2's framing was already correct and its premise already resolved at round 1. `/speckit-analyze` is step 6 and runs *after* tasks, since `tasks.md` is one of the artifacts its consistency pass checks.

**What is closed:**

- ✅ **The executed export generate run** (round 1's first blocker) — discharged by measurement between rounds 1 and 2: `research/reviews/orchestrator_084-packaging-cpack-export_gate_a_r1_measurements.md`.
- ✅ **The D1 / FR-012a class decision** — all seven `OPEN` rows of `contracts/package-layout.md` §2a now read `export` or `exclude`, so SC-009a is satisfiable.
- ✅ **FR-018e** — the package is provider-agnostic by construction; four obligations replace the withdrawn three-option table, and SC-016 has a pass state.
- ✅ **D3** — gated on `linux-gcc-release` only, carried as FR-026a.

**What `/speckit-tasks` MUST carry into the task list** (these are obligations, not open questions):

1. **Re-run the export generate experiment** once the six derived members are wired — they are read, not measured, and the identical method was wrong before (`contracts/export-set.md` §2a). First question it answers: whether `fixpp_capi_objects` needs an `OBJECTS DESTINATION` or a wiring change.
2. **Set `EXPORT_NAME` on every aliased export-set member.** `install(EXPORT … NAMESPACE fixpp::)` derives imported names from **target** names and `add_library(fixpp::X ALIAS …)` does **not** carry into an export — so left alone the package publishes `fixpp::fixpp_capi`, not the `fixpp::capi` that `architecture.md:503` names and that D1 Option A was decided on. `EXPORT_NAME` is set nowhere in the repository today. Pick one convention and apply it across the whole set (`contracts/export-set.md` §1).
3. **FR-026a** — enable `FIXPP_BUILD_INTEROP_PERF` and gate SC-011/SC-012 on `linux-gcc-release`, and only there.
4. **The FR-018e obligation-1 check** — assert the **installed** `fixppConfig.cmake` / `fixppTargets*.cmake` carry no build-host package-manager path; and SC-016's red leg must be **demonstrated**, not assumed.
5. The four implementation choices still open by design: the include-interface **rewrite form** (FR-002a), the `fixpp_transport` **`FILE_SET` disposition** (FR-002b), the artifact-directory **retention rule** (Assumption 5 / SC-008), and **`fixpp_capi_objects`' `OBJECTS DESTINATION` vs. a wiring change on `fixpp_capi`** — the first question T024's re-measurement must answer (`contracts/export-set.md` §2a, plan.md → Risks).

Everything else from rounds 1, 2 and 3 converged in the rewrites.
