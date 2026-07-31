# Implementation Plan: Installable Packaging (CPack) + CMake Package-Config Export

**Branch**: `084-packaging-cpack-export` | **Date**: 2026-07-31 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/084-packaging-cpack-export/spec.md`

---

## Summary

Make fixpp consumable and distributable. Today the library installs headers only — there is no `install(TARGETS)`, no `install(EXPORT)`, and no CPack anywhere — so a downstream CMake project cannot `find_package(fixpp)` and an operator has no artifact to install.

This feature adds a `fixpp::` export set covering the static library closure a real FIX application links, generates the package-config files that make `find_package(fixpp)` work, and produces per-configuration packages (DEB/RPM/TGZ on Linux, ZIP on Windows) carrying the headers, libraries, config files, the FIX dictionaries, and their required upstream attribution.

The design is anchored on one decision (spec FR-010a): a headers-only umbrella is provably insufficient, because the project's own benchmark client links `fixpp_session`, `fixpp_transport`, and `fixpp_tls` directly. That drives everything downstream — the export must carry static targets, their link ordering, and their third-party dependencies, and only a **real client built out-of-tree against an installed package** can prove it works.

---

## Technical Context

**Language/Version**: C++23 (`CMAKE_CXX_STANDARD 23`, `CMakeLists.txt:11`). This feature writes **no C++ library code** — it is build-system and packaging work plus a witness adaptation.

**Primary Dependencies**: CMake ≥ 3.28 + Ninja (Article III §1); Conan 2 (Article III §2); CPack; `CMakePackageConfigHelpers`. Runtime third-party surface reaching consumers: OpenSSL, asio, pugixml, and conditionally opentelemetry-cpp (research R3).

**Storage**: N/A — no persistent state. Package artifacts are build outputs.

**Testing**: ctest. Two witness tiers — the existing `fixpp::consumer::install-witness` (`CMakeLists.txt:289-311`) extended to `find_package`, and a new real-client tier built from `perf/fixpp_perf_driver.cpp` against an installed package.

**Target Platform**: Linux (gcc, clang) and Windows (MSVC). Six configurations: `linux-clang-{debug,release}`, `linux-gcc-{debug,release}`, `windows-msvc-{debug,release}`. macOS and clang-libc++ are descoped by user decision 2026-07-31.

**Project Type**: C++ library — packaging and CMake package-config export.

**Performance Goals**: N/A. No runtime code path changes; Article VIII budgets are untouched.

**Constraints**:
- **Storage**: one build tree at a time; peak ~30 GB against a 64 GB budget (spec Assumption 5, SC-008). Build → package → delete → next.
- **Ordering**: start with `linux-gcc-release` — the only configuration cheap on *both* axes (3.4 GB tree, zero third-party dependencies to build). `linux-clang-release` has the smallest tree but 5 dependencies to build from source; `linux-gcc-debug` needs 9 (research R5, spec Assumption 9).
- **Isolation**: Windows builds in a **separate** sandbox; the existing one holds unrelated in-flight state and must not be reused.
- **ccache**: the environment must be sourced *before* the first configure — the compiler launcher is baked in at first configure only.

**Scale/Scope**: 6 configurations × {DEB, RPM, TGZ} or {ZIP}. Export set ≈ 8 static targets plus per-version INTERFACE dict targets. No new library code.

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
| **IV §5** | v1.0 artifacts are **built but not published**; no registry upload | **PASS** | Packages are produced and archived as CI artifacts only. Deliberately *narrower* than the article permits — it also allows attaching to GitHub releases, which this feature does not set up (research R6). |
| **V §1** | AGPL-3.0 + commercial dual | **PASS** | Declared as the package license (FR-018). |
| **V §5** | WIP disclaimer stays until publishing is unblocked | **PASS** | Nothing published; disclaimer untouched. |
| **VII** | Testing requirements | **PASS** | Two witness tiers; every success criterion has a mechanical check. |
| **VIII** | Performance budgets & benchmarks | **N/A** | No runtime code path modified; §3 applies to hot-path changes. |
| **IX** | Coverage, sanitizers, static analysis | **N/A — confirm at verify** | Coverage is scoped to `src`/`include`; this feature touches neither, so the gate is expected N/A. Must be derived from the real diff, not assumed (spec Assumption 11). |
| **X** | ABI policy — C ABI is a versioned contract | **PASS — explicitly out of scope** | `include/fix/c_api*` untouched; the empty diff there is itself the freeze-invariant evidence. No shared-library variants introduced (spec explicit non-goal; A-1 holds the `0→1` freeze). |
| **XV** | Banned patterns | **PASS** | No library code written. |
| **XVI** | Spec Kit workflow rules | **PASS** | specify → clarify (3 answered) → plan. Gate A precedes `/speckit-tasks`. |
| **XIX** | Documentation | **PASS** | FR-024/FR-025 correct verified-stale anchor-doc claims; consumer usage documented in `quickstart.md`. |

**Initial result: PASS.** No violations, so `Complexity Tracking` is empty and omitted.

**Post-Phase-1 re-evaluation: PASS, unchanged.** The Phase 1 design adds no dependency, no generator, no library code, and no C-ABI surface. The one design element that could have moved a gate — exporting static targets (FR-010a) — was checked against Article X and does not touch the C ABI or introduce shared libraries.

**One item carried, not cleared**: REMAINING-WORK item 15d (whether the upstream dictionary license's acknowledgment clause is compatible with AGPL-3.0) remains open pending counsel review. This feature discharges the *mechanical* attribution obligations only and must not be read as legal clearance (spec Assumption 10). Because 15d gates publishing and nothing here is published, it does not block delivery.

---

## Project Structure

### Documentation (this feature)

```text
specs/084-packaging-cpack-export/
├── plan.md               # This file
├── spec.md               # Feature specification (34 FR, 15 SC, 12 assumptions)
├── research.md           # Phase 0 — R1..R10, all decisions evidenced
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
                                     #   dictionary + attribution install rules
cmake/
├── Codegen.cmake                    # add $<INSTALL_INTERFACE:> to fixpp::dict::<ver>
├── FixppPackaging.cmake             # NEW — CPack config, generators, metadata, provenance
└── fixppConfig.cmake.in             # NEW — find_dependency set + include of fixppTargets

conan/profiles/
└── linux-gcc-debug                  # NEW — Debug sibling of linux-gcc-release

CMakePresets.json                    # NEW preset: linux-gcc-debug

NOTICE                               # NEW — upstream clause-3 acknowledgment (verbatim)

tests/consumer/
├── CMakeLists.txt                   # hand-rolled discovery -> find_package(fixpp)
├── consumer_witness.cpp             # include BOTH a core header AND a generated typed header
└── run_consumer_witness.cmake       # harness reused unchanged

tests/packaging/                     # NEW
├── CMakeLists.txt                   # package-content enumeration + provenance checks
└── run_real_client_witness.cmake    # builds the real client out-of-tree vs installed pkg

perf/
└── CMakeLists.txt                   # packaged-variant path: no src//tests/ includes,
                                     #   no network fetch, shipped-dictionary load

.github/workflows/tier1.yml          # artifact upload for in-scope Linux lanes
.github/workflows/tier2.yml          # artifact upload for the MSVC lanes
```

**Structure Decision**: no new source tree. Packaging logic is factored into `cmake/FixppPackaging.cmake` rather than growing the ~360-line top-level `CMakeLists.txt`, matching the existing `cmake/*.cmake` convention. The only new test directory is `tests/packaging/`, holding checks that operate on **produced artifacts** rather than on compiled code — a genuinely different subject from every existing test directory.

---

## Phase 1 Design Summary

Full detail in `contracts/`. The load-bearing decisions:

1. **Export set = the transitive closure a real client links** (research R2): `fixpp_core`, `fixpp_sync`, `fixpp_log`, `fixpp_wire`, `fixpp_dictionary`, `fixpp_tls`, `fixpp_transport`, `fixpp_session`, plus per-version `fixpp::dict::<ver>` INTERFACE targets, plus conditionally-built telemetry targets. `fixpp::fixpp` is the umbrella. Builders and validators stay **out** (FR-007).

2. **The export set is configuration-dependent, so the config file is generated from what was built** — never a hardcoded list. Telemetry targets exist only in telemetry-enabled builds, which is exactly why SC-015 requires exercising the config against a telemetry-disabled build.

3. **Link ordering is the export's responsibility** (research R7). Declared target dependencies let CMake produce a correct link line; consumers never restate the ordering the existing harnesses hand-roll.

4. **Two witness tiers, because they fail differently** (research R8, R9). The minimal tier catches config-package regressions cheaply in every configuration; the real-client tier is the only thing that can catch an export that *resolves but cannot link*.

5. **Attribution is two obligations, not one** (FR-018b): reproduce the upstream license text **and** carry the acknowledgment sentence in `NOTICE`. The license file states that requirement but does not satisfy it.

6. **Every content guarantee is verified by enumerating produced artifacts**, never by reading install rules (FR-018d, SC-004, SC-013). An install rule matching nothing yields a deficient package that looks correct in CMake.

---

## Decisions routed to Gate A

**D1 — C-ABI packaging scope. OPEN; must be decided, not defaulted.** `include/fix/c_api.h` + `include/fix/c_api/` ship today because `CMakeLists.txt:321` installs the whole `include/` tree, but no C-ABI library is exported — a shipped header with nothing behind it. Article IV §2 makes this surface the legal isolation boundary for dual licensing. Options and implications are in [`spec.md` → Open Questions](./spec.md#open-questions). Option B (exporting the shared C-ABI library) touches the deliberately-held `0→1` ABI freeze, so it is not this feature's call. **Raise this at `/gate-a` explicitly** — it is a design-scope decision, and `/speckit-analyze` (step 6) runs after `/speckit-tasks`, far too late to reshape the export surface.

**D2 — Verification-matrix fallback, in writing.** Assumption 11 expects the full preset matrix and coverage to be N/A because no `src/**` or `include/**` file is touched. That is an *expectation*, not a disposition, and the verify gate's step-0a taxonomy has **no bucket for a build-system-only change** — it is neither a source change nor strictly documentation-only. **Fallback, to be applied rather than re-derived**: if the diff lands outside every bucket, the verification record states that taxonomy gap explicitly and the matrix is derived from the `tier1.yml` jobs that actually gate the touched paths. Any preset skipped goes through the gate's waiver mechanism with a paired rationale — an unpaired skip is a failure by design, not a shortcut.

**D3 — Is the real-client witness a CI gate or local-only?** `FIXPP_BUILD_INTEROP_PERF` defaults **OFF** and no CI lane enables it, so SC-011/SC-012 are local-only unless a lane turns it on. A witness that silently does not run reads as green — the exact failure mode recorded in `feedback_ci_gate_observes_not_asserts_witness_skips_into_green`. **Decide explicitly**: either a lane enables it, or SC-011/SC-012 are declared local-verify obligations that MUST appear in the verification record. Leaving it implicit is the one unacceptable outcome.

---

## Risks

| Risk | Mitigation |
|---|---|
| A witness passes against a **stale** package from an earlier configuration — `artifacts/` deliberately outlives build trees | FR-021a: provenance (configuration + source revision) stamped on each artifact; witness fails on mismatch |
| The generated config unconditionally requires the telemetry dependency, breaking every telemetry-disabled consumer | SC-015 — exercise the config against a telemetry-disabled build. The one defect class the descoped libc++ lane would have caught |
| A future emitter adds a generated-artifact kind that escapes the denylist, leaking unexported symbols into packages | FR-009 machine-checkable coherence assertion; SC-007 requires it be **proven to fail** on a deliberately broken input before counting as a gate |
| An implementer "fixes" the apparent `FIXPP_BUILD_OTEL` propagation gap and introduces a real ODR mismatch | Research R4 records that the current non-propagation is **correct** — the definition never reaches public headers |
| Anchor-doc corrections (FR-024/FR-025) get bundled into a submodule commit | They target the **parent** repo and cannot be committed on this branch; deferred to close-out and staged deliberately |
| Windows work disturbs unrelated in-flight state in the shared sandbox | Use a distinct sandbox path; never reuse the existing one |
| **The new root `NOTICE` file reds the hidden git-cleanliness gate.** `tests/codegen/codegen_build_graph_test.cmake:198-221` runs `git status --porcelain` and fails on any output. `NOTICE` is a new **tracked** file at the repo root — unlike the build symlinks (invisible because git never descends into an ignored directory), it *will* appear until committed | Commit `NOTICE` in the same change that adds its install rule; never leave it uncommitted across a codegen-gate run |
| **The three new packaging tests configure and build sub-projects** — concurrent runs collide with each other and with the git-cleanliness gate | Mark all three `RUN_SERIAL` with an explicit `TIMEOUT`, mirroring the existing consumer witness (`TIMEOUT 300`, driven via `cmake -P`), per `feedback_tree_mutating_test_must_run_serial_vs_gitclean_gate` |
| A ZLIB link failure in the real-client witness gets "fixed" by adding `find_dependency(ZLIB)` | R3's reasoning is that the Conan-provided OpenSSL imported target carries compression transitively — true for these presets, not a general truth. If that link fails, first check whether the **imported target regressed**; adding the dependency would mask it |

---

## Next

**`/gate-a 084-packaging-cpack-export`** — pipeline step 4, immediately after `/speckit-plan`. Per `.specify/pipeline.md`, Gate A's blockers are resolved **before** `/speckit-tasks` (step 5); `/speckit-analyze` is step 6 and runs *after* tasks, since `tasks.md` is one of the artifacts its consistency pass checks.

Carry D1, D2, and D3 into Gate A as explicit agenda items. D1 in particular is a design-scope decision about the export surface — deferring it to the step-6 consistency pass would be too late to reshape anything.
