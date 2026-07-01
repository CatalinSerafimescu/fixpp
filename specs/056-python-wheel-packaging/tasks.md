---
description: "Task list — Python Wheel Packaging (PY-005)"
---

# Tasks: Python Wheel Packaging (PY-005)

**Input**: Design documents from `specs/056-python-wheel-packaging/`
**Prerequisites**: plan.md, spec.md, research.md (D-1..D-10), data-model.md (E-1..E-6),
contracts/ (wheel-packaging PKG/LAY/TAG, ci-wheel-gate CI/REL/WIN/NBC, locator-api LOC), quickstart.md

**Tests**: Verification tasks ARE included. The FRs/SCs name the wheel-suite, the
broken-wheel gate, the NBC snapshots, and the cross-version/sub-interpreter witnesses as
**acceptance witnesses** (FR-006/007/009, SC-001..007). TDD ordering (Article VII §3 — red
test first) **applies where it can**: the pure-Python locator (T008) is unit-testable
in-tree, so its LOC-1..6 tests (in T012) are written failing-first before T008 is implemented.
The artifact-dependent witnesses (wheel build, install-test, CI, `auditwheel`/tag checks) can
only run **against a built wheel**, so they are verified post-build rather than pre-written —
a **bounded packaging-ordering deviation**, NOT a blanket TDD exemption, recorded as an
explicit waiver in `.specify/decisions/056-python-wheel-packaging-verify.md` at
`/speckit-verify` (precedent: 039 gate-a-waived, Article XVII §6). The canonical name for the
post-install test set is the **functional install-verification subset** (spec Key Entities) —
the dedicated `tests/wheel/` suite (T012), used consistently here, in D-8, and in CI-4.

**[verify at implement]**: tasks T004, T010, T013, T014 are the empirical witnesses owned
by the first build (research flags them); each carries its fallback. **Nothing here runs
the manylinux build now — that is executed at `/speckit-implement`.**

**Branch**: `056-python-wheel-packaging` · all paths are relative to the library submodule
root (`research/G19-fix-fpml-iso20022/library`).

## Format: `[ID] [P?] [Story] Description`

- **[P]**: parallelizable (different files, no incomplete-task dependency)
- **[Story]**: US1 / US2 / US3 (Setup, Foundational, Polish carry no story label)

---

## Phase 1: Setup

**Purpose**: Pre-flight the toolchain pins (no build artifacts yet).

- [X] T001 Verify the build-tool versions against PyPI/registries **at implement** (per the
  CLAUDE.md dependency rule — anchor-doc pins go stale): `scikit-build-core`, `cibuildwheel`,
  `auditwheel`, **`swig>=4.2` (PINNED — a 4.0 runner silently regresses the limited-API mode,
  PKG-1/D-3)**, Conan 2.x, CMake ≥ 3.28, Ninja. Record the exact resolved lower-bound pins to
  reuse in T002/T016; flag any value behind the current release.
  **RESOLVED 2026-06-30** (PyPI / local): scikit-build-core 0.12.2 (latest), cibuildwheel 4.1.0
  (latest), auditwheel 6.7.0 (latest), swig 4.4.1 latest / 4.2.0 local (pin `>=4.2` ✓), Conan
  2.27.0, CMake 3.30.0 (≥3.28 ✓), Ninja 1.11.1. No anchor-doc pin is behind; pyproject uses
  correct lower bounds. (The pyproject comment's "swig 4.2.0 published" is the local build copy —
  latest published is 4.4.1; pin is a lower bound so unaffected.)

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: The PEP 517 build backbone every story consumes (US1 builds the wheel from it;
US2's CI invokes it; US3 reuses it for Windows). **No user-story work begins until this exists.**

**⚠️ CRITICAL**: blocks US1/US2/US3.

- [X] T002 Create `bindings/python/pyproject.toml` — `scikit-build-core` PEP 517 backend
  (PKG-1..6, research D-1/D-6): `[build-system] requires = ["scikit-build-core>=…", "swig>=4.2"]`,
  `build-backend = "scikit_build_core.build"` (PKG-1); `[project] name = "fixpp"`, `version`
  **dynamic** from CMake `project(VERSION)` via `tool.scikit-build.metadata.version` (PKG-2/D-6);
  `requires-python = ">=3.10"` (PKG-3, the abi3 `cp310` floor, no upper cap); **no runtime
  `dependencies`** (PKG-5, self-contained); `[tool.scikit-build] wheel.py-api = "cp310"` — the
  **authoritative abi3 tag driver** (PKG-6; NOT the inert setuptools `py_limited_api` kwarg);
  drives the existing `bindings/python/CMakeLists.txt` with `-DFIXPP_BUILD_PYTHON=ON` (PKG-4).

**Checkpoint**: build backend in place — user stories can begin.

---

## Phase 3: User Story 1 - Clean-machine pip install (Priority: P1) 🎯 MVP

**Goal**: Produce ONE self-contained, importable stable-ABI (abi3) Linux wheel
`fixpp-<ver>-cp310-abi3-manylinux_2_28_x86_64.whl` that `pip install`s on a stock
CPython-3.10–3.13 machine (no compiler/SWIG/Conan/system fixpp) and supports the full
dictionary-load → round-trip flow from bundled data.

**Independent Test**: build locally via cibuildwheel (quickstart §1), `pip install` into a clean
venv on each of 3.10/3.11/3.12/3.13, `import fixpp`, run the FIX 4.4 round-trip resolving the
dictionary through `fixpp.dictionary_path("FIX44")` — succeeds with no repo present.

### abi3 feasibility gate (front-loaded — the tightest constraint; everything below assumes it holds)

- [X] T003 [US1] Rework `fixpp_py_is_main_interpreter` in `bindings/python/fixpp.i` (~L596) for
  the limited API (FR-012 / SC-007 / D-3): remove `PyInterpreterState_Main()` (not in the limited
  API); capture the main interpreter's id at module init and compare it against
  `PyInterpreterState_GetID(PyInterpreterState_Get())` (both limited-API). **MUST preserve the
  sub-interpreter-rejection behaviour (→ code 1201)** — the 055 witness stays green. No C-ABI change.
  **NBC-2 in-tree regression check**: after the rework, run the existing `python-bindings` CTest
  matrix (none/asan/tsan) **in-tree** to confirm the frozen PY-001..004 behavioural suite — incl.
  the in-tree sub-interpreter witness — stays green BEFORE proceeding to T004 (the installed-wheel
  witness T013 is the per-version-runtime check; this is the distinct in-tree NBC-2 obligation).
- [X] T004 [US1] **abi3 feasibility GATE [verify at implement]** (D-3): regenerate the SWIG wrapper
  and compile it `-fsyntax-only -DPy_LIMITED_API=0x030A0000` against 3.10 headers; assert **ZERO**
  limited-API violations (research proved only the single `PyInterpreterState_Main` violation — the
  `PyInterpreterState_Get`/`GetID` replacement is itself unverified at the `0x030A0000` floor).
  A non-zero count here is the **compile-time** trigger for the per-version fallback (T015).

### Build/tag adaptation (CMake) — depends on T004 passing

- [X] T005 [US1] `bindings/python/CMakeLists.txt`: raise `find_package(SWIG 4.0 → 4.2 REQUIRED)`
  (PKG-1/D-3); add `-DPy_LIMITED_API=0x030A0000` **compile-only** + the CMake SABI wiring
  (`USE_SABI 3.10` / `SKBUILD_SABI_COMPONENT` / `Development.SABIModule`). **Compile-only — does
  NOT set the wheel tag** (the tag is PKG-6's `wheel.py-api`).
- [X] T006 [US1] `bindings/python/CMakeLists.txt` **flat-layout install fix** (D-4 / LAY-1):
  change the install `DESTINATION` for the `fixpp_py` target + `fixpp.py` + `fixpp_oo.py` from
  `"${Python3_SITEARCH}/fixpp"` (the latent PEP-420 namespace-dir that makes the module
  `fixpp.fixpp`) to the **flat** site-packages root. Fixes the in-tree `cmake --install` path too,
  not just the wheel. Packaging only — no module-content change (FR-012).

### Bundled dictionary data + locator

- [X] T007 [P] [US1] Create the `_fixpp_data/` data package (E-3 / LAY-2): add
  `bindings/python/_fixpp_data/__init__.py` (empty importable marker) and a CMake
  `install(FILES … DESTINATION <wheel-root>/_fixpp_data)` rule staging
  `dictionaries/{FIX42,FIX44,FIX50SP2,FIXT11}.xml` (single source of truth — copied/configured
  at build, never hand-duplicated). `unzip -l … | grep _fixpp_data/FIX` is the landing witness.
- [X] T008 [P] [US1] Create `bindings/python/fixpp_dict_data.py` locator (E-4 / contracts LOC-*):
  `BUNDLED_DICTIONARIES = frozenset({"FIX42","FIX44","FIX50SP2","FIXT11"})` (LOC-1, set-equality);
  `@contextmanager dictionary_path(name) -> Iterator[str]` via `importlib.resources.files("_fixpp_data")`
  + `as_file` (LOC-2/5/6); `dictionary_bytes(name) -> bytes` (LOC-3); unknown name → **`KeyError`**
  whose message lists the **sorted** valid set (LOC-4 — single decided type, no silent default). Add
  its CMake flat install rule (T006 destination).
- [X] T009 [US1] `bindings/python/fixpp.i` `%pythoncode` glue (the OO-re-export block at
  fixpp.i L742–L747): add an **additive `try/except ImportError`-guarded re-export block** surfacing
  `dictionary_path` / `dictionary_bytes` / `BUNDLED_DICTIONARIES` from `fixpp_dict_data` into `fixpp`
  (LOC-0 / FR-004a) — mirroring the 055 `try: from fixpp_oo import … except ImportError: pass`
  precedent (a guarded block, NOT a bare line — a bare import breaks any deployment where the module
  is absent). Use a **separate guard** from the `fixpp_oo` block so each import fails independently.
  No change to any existing wrapper (FR-012). Depends on T008.

### Build the artifact + witnesses ([verify at implement])

- [X] T010 [US1] **DONE 2026-06-30** — built `fixpp-0.0.1-cp310-abi3-manylinux_2_26_x86_64.manylinux_2_28_x86_64.whl`
  (4.1 MB) via `build-wheel.sh` in `manylinux_2_28` (gcc-toolset-14). D-2 gate PASSED (gcc 14.2.1 ≥13,
  `std::expected` TU compiles). Config now in `[tool.cibuildwheel]` + `cibw-before-all.sh`; proven via an
  isolated configure-only probe. Surfaced + fixed: perl modules for openssl, OTel/TESTS paired CMake toggles,
  the Conan→cmake toolchain handoff (config-settings, not env), and a SWIG 4.4 conflicting-linkage bug
  (commit d869735). **Build the single abi3 wheel** locally via `bindings/python/build-wheel.sh`
  (quickstart §1, D-2/D-7) — the wrapper runs `cibuildwheel` from a pristine `git worktree` of
  committed HEAD so the multi-GB `build/`/`.codegraph/` trees are never swept into cibuildwheel's
  blind `tar -c .` of cwd (commit before running). Env knobs:
  `CIBW_BUILD="cp310-manylinux_x86_64"` + `CIBW_ARCHS_LINUX="x86_64"` +
  `CIBW_MANYLINUX_X86_64_IMAGE="manylinux_2_28"` (the architecture-only build identifier — `abi3`/`2_28`
  are NOT selector components); `CIBW_BEFORE_ALL` enables gcc-toolset (≥13), generates the matching
  in-container Conan profile, runs
  `conan install . --build=missing -o fixpp/*:with_otel=False -o openssl/*:shared=False`
  (self-contained `.so`), then scikit-build-core drives the CMake target + `auditwheel repair`.
  **[verify at implement] D-2**: confirm the pinned image's gcc-toolset major ≥ 13 (a one-file
  `std::expected` TU compiles in-container) before wiring the full matrix; else pin an image / install
  `gcc-toolset-13`.
- [X] T011 [US1] **DONE 2026-06-30** — `auditwheel show`: only baseline glibc libs (libc/libdl/libpthread/libm),
  **NO external third-party libs** (libssl/libcrypto/libstdc++ all static) → self-contained (LAY-3). `abi3audit
  --strict`: `is_abi3:true, baseline 3.10, non_abi3_symbols:[]`. `zipfile -l`: flat `_fixpp.so`/`fixpp.py`/
  `fixpp_oo.py`/`fixpp_dict_data.py` + `_fixpp_data/FIX{42,44,50SP2,T11}.xml`; exactly ONE wheel; tag `cp310-abi3`.
  NOTE: auditwheel tagged `manylinux_2_26.manylinux_2_28` (binary needs only glibc 2.26 — MORE compatible than
  the 2_28 floor, and 2_28 is still in the tag). Verify tag + self-containment on the produced wheel (LAY-1/2/3, TAG-1/2/3, SC-004):
  `auditwheel show` → `manylinux_2_28_x86_64` + `abi3` + **external-library list EMPTY** (LAY-3 static-
  everything witness); `unzip -l` → flat top-level `_fixpp*.so`/`fixpp.py`/`fixpp_oo.py`/`fixpp_dict_data.py`
  + `_fixpp_data/FIX*.xml`; assert **exactly ONE** wheel, ABI tag `cp310-abi3` (not `cp3XX-cp3XX`).
- [X] T012 [US1] **DONE 2026-06-30** — created `bindings/python/tests/wheel/` (commit 61c1493):
  16 modules = ports of the enumerated categories (round-trip, smoke, exception, lifetime,
  OO-behaviour, sub-interpreter) + `test_locator.py` (LOC-0..6/LAY-4) + `test_installed_only.py`
  (sys.prefix guard) + 4 support modules (`_wheeldict` locator resolver, `oo_test_support`,
  `_gil_staging`, `_oo_reentrancy_staging`). Each port's only divergence from source is its dict
  helper → `_wheeldict.resolve("FIX44")`. **Two documented exclusions** (README): the spec'd
  `test_gil_release_canary.py` (FR-007 canary build) AND `test_error_coverage.py` (reads the repo
  source header `include/fix/c_api/error.h` — not install-verifiable; stays in-tree; its additive
  guard is the separate NBC-3 T018). Fixed the committed `test_locator` LOC-3 bug (`<?xml`→`<fix`,
  the bundled dicts carry no XML declaration). **Validated GREEN against the SWIG-4.2 build-dir .so:
  87 passed** (sole "fail" = `test_installed_only` correctly firing off-prefix). **This run caught a
  ship-blocking SWIG-4.3 wheel defect** (`SWIG_Python_AppendOutput` gained an is_void arg → every
  factory returned `[None,handle]`). Fixed **version-agnostically** (commit follows 35c82d2): the
  `%typemap(out) fixpp_error_t` leaves `$result` NULL on OK + a new `%typemap(ret)` defaults None,
  relying only on AppendOutput's stable `!result` branch → correct on 4.2 AND 4.3+ with no #if guard.
  Pin **dropped** (`swig>=4.2`, no upper bound) so the wheel builds with the **latest SWIG (4.4.1)**
  per USER DECISION (drive the future). Re-verified — see T013.
- [X] T013 [US1] **DONE 2026-06-30** — full cross-version witness GREEN on the shipped **SWIG-4.4.1**
  wheel (`aaa5842…`), installed into clean `uv` venvs (PYTHONPATH scrubbed, fixpp under sys.prefix):
  **3.10 → 88 passed, 3.11 → 88 passed, 3.12 → 88 passed, 3.13 → 87 passed + 1 skipped**
  (`test_subinterpreter` skips on 3.13 only because `_xxsubinterpreters` was renamed `_interpreters`
  in 3.13; 3.13 is covered by the same 3.12+ single-phase import barrier). **The load-bearing
  3.10/3.11 band initially went RED** — `fixpp.Engine()` was NOT rejected in a sub-interpreter (no
  import barrier <3.12, so the runtime 1201 check is the sole guard, and it was never actually
  witnessed by 055/054 which only tested 3.12). Root cause = process-global `main_interp_id` captured
  in `%init` overwritten by the sub-interp's re-init; **fixed** to the limited-API `interp id == 0`
  is-main check (commit on fixpp.i). Standalone `test_subinterpreter` now PASSES on 3.10 AND 3.11
  (the discriminator). NOT RED on any version → per-version fallback (T015) stays untriggered.
  **The abi3 proof + the SWIG-4.4.1 toolchain are both validated end-to-end.**
  **[verify at implement] obligation**: T013 + the typemap fix + the sub-interp fix all materially
  changed 055's runtime binding behaviour → /speckit-verify owes the in-tree sanitizer matrix
  (none/asan/tsan) revalidation + Gate B owes a sub-interp-contract re-check (not inherited-frozen).
- [X] T014 [US1] **DONE 2026-06-30** — scikit-build-core resolved the version from CMake `project(VERSION)`
  cleanly; both the local and container wheels are named `fixpp-0.0.1-cp310-abi3-…`. No static fallback needed.
  **Version-source check [verify at implement]** (D-6 / PKG-2): confirm scikit-build-core
  reads CMake `project(VERSION)` (`0.0.1` today) cleanly so the wheel ships `fixpp-<ver>-cp310-abi3-…`;
  else fall back to a single static `project.version` in `pyproject.toml` kept in sync by a CI check.
  (The v1.0 release-number bump is a separate release step, not this feature.)

### Per-version fallback contingency

- [X] T015 [US1] **NOT TRIGGERED 2026-06-30** — abi3 shipped. T004 abi3 feasibility GATE passed (zero
  limited-API violations) and the produced **container** wheel passed `abi3audit --strict` (non_abi3_symbols:[],
  baseline 3.10) AND imports + runs the locator round-trip on fresh CPython 3.12. No RED witness → the
  per-version `cp3XX-cp3XX` fallback is NOT executed (closed as not-triggered). Full 3.10/3.11/3.13 install
  confirmation is T013 (CI matrix T016).
  ~~Per-version fallback (FR-010 — CONTINGENCY, execute ONLY if T004 compile OR T013 runtime import fires RED).~~

**Checkpoint**: a locally-built, install-tested abi3 wheel — the MVP deliverable.

---

## Phase 4: User Story 2 - CI builds, install-tests, publishes the wheel (Priority: P2)

**Goal**: Wire wheel production + clean-env install-verification into a Tier-1 mandatory merge gate,
attach the wheel to GitHub releases, and make a broken wheel fail the gate.

**Independent Test**: trigger the `python-wheel` Tier-1 job — it emits the single `.whl`, installs it
on each of 3.10–3.13, runs the functional subset green against the installed package, and (on a release
event) attaches the asset; a deliberately broken wheel turns it red.

- [X] T016 [US2] **DONE 2026-07-01** — added three additive jobs to `.github/workflows/tier1.yml`:
  `python-wheel-build` (build the ONE cp310-abi3 wheel in `manylinux_2_28` via `cibuildwheel` — run
  directly from repo root, not `build-wheel.sh`: a fresh CI checkout has no `build/` so the
  blind-tar hazard is absent; NBC-1 freeze + occupancy + REL-2 grep run first/fast; `abi3audit
  --strict` + `auditwheel show` + exactly-ONE-wheel on the artifact), `python-wheel-test` (CI-4
  install-test **matrix 3.10/3.11/3.12/3.13 against the single downloaded artifact** — clean venv,
  out-of-repo + `PYTHONPATH`-scrubbed `tests/wheel/`, then the T019 broken-wheel gate),
  `python-wheel-release` (T020). All `gate-precheck`-guarded (CI-1), per-leg `concurrency` (CI-7),
  `timeout-minutes` backstops (CI-6), no `continue-on-error` (CI-5), **additive — the existing
  `python-bindings` sanitizer matrix + GIL canary are untouched (CI-8)**. CI-3 Conan cache mounted
  into the container as `CONAN_HOME` (non-load-bearing — a miss just rebuilds). YAML validated;
  REL-2 grep self-match avoided (anchored patterns, proven clean + detects an injected publish).
  **Authored + structurally verified; the real execution is on the PR (no GH Actions runner / Docker
  manylinux available locally).** Original spec follows:
  Add the **`python-wheel` Tier-1 job** to `.github/workflows/tier1.yml` (CI-1..8, D-9):
  runs every PR behind the `gate-precheck` `proceed` guard (CI-1); builds the single `cp310-abi3`
  wheel via `cibuildwheel` in `manylinux_2_28_x86_64` with gcc-toolset ≥13 + in-container Conan +
  `-DPy_LIMITED_API` (compile-only) + `wheel.py-api="cp310"` + SWIG ≥4.2 + `with_otel=False` + static
  OpenSSL (CI-2); caches `~/.conan2/p` keyed on `conanfile.py` + profile (CI-3); install-test **matrix**
  over 3.10/3.11/3.12/3.13 against **the one wheel** running `tests/wheel/` against the installed package
  (CI-4); **red** on any build/install/test failure, **no** `continue-on-error` (CI-5); `timeout-minutes`
  backstop (CI-6); per-leg `concurrency` group (CI-7); **additive only — does NOT remove/weaken the
  existing `python-bindings` sanitizer matrix or the GIL canary** (CI-8 / FR-007). The install-test
  runs the canonical **functional install-verification subset** (`tests/wheel/`, T012). **This job also
  hosts the negative-gate step authored in T019** (`test_broken_wheel_gate.sh`) and the NBC guards
  (T017 c_api-diff, T018 import-surface) as steps within it — T016 creates the job skeleton; T017/T018/T019
  add steps to it.
- [X] T017 [P] [US2] **DONE 2026-07-01** — `tools/check_capi_freeze.sh` + the committed manifest
  `tools/capi_freeze.sha256` (SHA-256 of `include/fix/c_api.h` **+ all of `include/fix/c_api/`** — the
  literal `c_api*.h` glob would miss the subdir where `message.h`/`session.h`/`error.h` live). An
  **absolute** checksum baseline (not a diff-vs-base) so it works on `push:main` too; `sha256sum -c`
  catches content tamper, an exact-set check catches an added/removed header. Wired as the first step of
  `python-wheel-build` (fail-fast) alongside the existing occupancy gate. **Mutation-verified locally:
  PASS green; RED on a tampered header AND on an added header; restored green.**
  **NBC-1** C-ABI freeze guard: a CI step that **fails on any diff to
  `include/fix/c_api*.h`** (the `0→1` GA freeze is byte-frozen) and runs the existing ABI/header-occupancy
  check (SC-007).
- [X] T018 [P] [US2] **DONE 2026-07-01** — `bindings/python/tests/wheel/test_import_surface.py`: an
  **exact-set** (`==`, not subset — `feedback_completeness_gate_exact_set_not_subset`) snapshot of the
  full 79-name public surface of `import fixpp`, generated from the installed SWIG-4.4.1 wheel. Catches
  a dropped symbol AND a silent addition → any surface change must be a reviewed snapshot edit. Plus a
  named-categories belt-and-suspenders (Error===FixppError, OO classes, locator). **Verified GREEN
  (2 passed) against the INSTALLED wheel in a scrubbed venv.** Joins the `tests/wheel/` suite that CI-4
  runs every PR. **NBC-3** import-surface snapshot test (in `tests/wheel/`): assert `import fixpp`
  still resolves **every** existing name/class (flat functions, `FixppError`/`Error`, the OO classes, the
  new locator) — guards the additive `%pythoncode` re-export (T009) from dropping a symbol.
- [X] T019 [US2] **DONE 2026-07-01** — `bindings/python/tests/wheel/test_broken_wheel_gate.sh`:
  rebuilds a wheel COPY with `_fixpp_data/FIX44.xml` (and its RECORD line) stripped so the mutated
  wheel still **installs cleanly** and the failure surfaces at locate-time (a *data* witness, not a
  corrupt-zip witness), installs into a throwaway venv, runs the locator round-trip, asserts **non-zero**.
  **Self-discriminating**: proves the INTACT wheel round-trip PASSES first (positive control) so a red
  stripped arm is attributable to the missing data. Non-publishing (throwaway copies only). Wired as a
  `python-wheel-test` step (CI-5, no `continue-on-error`). **Verified locally BOTH arms: intact →
  ROUNDTRIP_OK; stripped → `CapiError` (dict load fails) → gate RED.**
- [X] T020 [US2] **DONE 2026-07-01** — `python-wheel-release` job (`if: github.event_name == 'release'`,
  `needs: [python-wheel-build, python-wheel-test]`, `contents: write`): on a published release the full
  Tier-1 gate runs on the release commit first, then `gh release upload <tag> <wheel> --clobber` attaches
  the single `cp310-abi3` wheel as an **asset** (REL-1/3). Added `release: types: [published]` to the
  workflow `on:`. REL-2/SC-005 enforced by the `python-wheel-build` grep step (anchored to real
  publish syntax — `gh release upload` is an asset attach, not an index publish; **verified the grep
  finds no publish step and DETECTS an injected `pypa/gh-action-pypi-publish`**).

**Checkpoint**: the wheel is produced + install-verified + released by CI; broken artifacts fail.

---

## Phase 5: User Story 3 - Best-effort Windows wheel (Priority: P3)

**Goal**: Provide the Windows packaging glue as a separable, non-gating lane (deferred/best-effort).

**Independent Test**: on the on-demand Windows lane, build the wheel, install into a clean Windows Python
env, run the functional subset; its absence/failure does not affect the Linux gate.

- [X] T021 [US3] **DONE 2026-07-01 (DEFERRED disposition)** — added `.github/workflows/wheel-windows.yml`:
  a SEPARABLE, on-demand (`workflow_dispatch` / `windows-wheel` label one-shot) `cibuildwheel`-windows
  lane, **`continue-on-error` + never a Linux merge-gate dependency** (FR-011 / WIN-1). Not trivially
  cheap to finish (needs an MSVC before-all: Conan `compiler=msvc` + static OpenSSL, `delvewheel`
  instead of auditwheel, SABI-link validation under MSVC) so the disposition is **DEFERRED** — the file
  is an honest scaffold documenting the remaining work; recorded as **L-056-1** (T024). YAML validated.
  Document the Windows deferral + add an **on-demand** `cibuildwheel` Windows stub
  (D-10 / FR-011 / WIN-1): a `windows`-labelled / nightly lane reusing the same source + the existing
  green MSVC C-ABI build, with `delvewheel` glue. **MUST be separable — never a Linux merge-gate
  dependency**; build it only if trivially cheap, otherwise capture as a deferred limitation. Record the
  disposition (built vs deferred) as an `L-056-*` row in `spec/behaviors-and-limitations.md` (T024).

---

## Phase 6: Polish & Cross-Cutting Concerns

- [X] T022 **DONE 2026-07-01** — ran quickstart §1–§5 end-to-end against the built SWIG-4.4.1 wheel in a
  clean `uv` 3.12 venv: §1 artifact present; §2 install + `fixpp.__file__` under sys.prefix + public-surface
  smoke OK; §3 FIX44 round-trip via `fixpp.dictionary_path` OK; §4 out-of-repo PYTHONPATH-scrubbed
  `tests/wheel/` = **90 passed** (the +2 over T013's 88 are the new T018 snapshot tests); §5 tag
  `cp310-abi3` + flat `_fixpp.so`/`fixpp.py`/`fixpp_oo.py`/`fixpp_dict_data.py` + `_fixpp_data/FIX*.xml`.
  Run the quickstart.md validation end-to-end (§1 build → §2 install → §3 round-trip → §4 subset
  → §5 tag/self-containment) as the manual mirror of the CI gate.
- [X] T023 **DONE 2026-07-01** — applied all 8 A-3 from→to sites: `spec/coverage-index.md:618`
  (wheel name → `cp310-abi3`), `.specify/api-contract.md` §10 row 2m (single-abi3 prose), the §3.2
  mirror + §1 goal-5 (name flip + **mimalloc clause dropped** + static-link surface recorded) + §1.1
  prose + non-goal #2 in `.specify/2m-pybind.md`, `.specify/architecture.md:468` table row, and the
  library `CLAUDE.md` Active-work pointer. **A-3 item 2b: ANNOTATED (not rewrote)** the two historical
  audit quotes in `api-contract.md` (Root-cause-#3 + N-P2-2) with forward-pointers — audit trail
  intact. Verified: `grep -rn "cp310-cp310" .specify/ spec/` now hits ONLY the gate-A decision log
  (historical, expected); no live normative source names the per-version mandate.
  **Apply the inherited-design amendments A-1 / A-2 / A-3** (deferred-apply at `/implement`;
  **user-ratified at Gate A**) per the exact from→to table in `plan.md` (*Proposed inherited-design
  amendments*). Sites: `spec/coverage-index.md:618` (PY-005 wheel name, normative index), `.specify/api-contract.md`
  (§10 row 2m), `.specify/2m-pybind.md` (§3.2 mirror :149/151, §1 goal-5 :23 incl. **dropping the stale
  "mimalloc" clause**, §1.1 :30, non-goal #2 :110), `.specify/architecture.md:468` (build-output table row),
  and the library `CLAUDE.md` "Active work" line (refresh the stale "per-version" pointer to the abi3 wheel).
  **CRITICAL — A-3 item 2b (`api-contract.md:316`/`:334`): ANNOTATE each historical resolution quote with a
  forward-pointer ("superseded by the abi3 pivot — see PY-005 / `[arch §7.1]` amendment"); do NOT rewrite the
  quote — the audit trail must not be falsified.** Verify with `grep -rn "cp310-cp310" .specify/ spec/` → no
  live normative source names the per-version mandate afterward.
- [X] T024 **DONE 2026-07-01** — added the `### 056` section to `spec/behaviors-and-limitations.md`:
  B-056-1 (the one self-contained abi3 wheel + bundled-dict locator), B-056-2 (typed-1201 sub-interp
  guard now WITNESSED on 3.10/3.11 + the is-main `interp id==0` fix; cf. L-055-1), L-056-1 (Windows
  deferred/best-effort, separable lane), L-056-2 (per-version fallback not triggered — abi3 shipped),
  L-056-3 (3.14+ covered-by-abi3-but-untested-in-v1, PKG-3).
  Append `spec/behaviors-and-limitations.md` `L-056-*` rows: Windows wheel deferred/best-effort
  (T021), the per-version fallback contingency (T015, abi3-untriggered), and 3.14+ covered-by-abi3-but-
  untested-in-v1 (PKG-3).

### Mandatory close-out tasks (ALWAYS emit — Gate-B preconditions, Article XVII §8)

- [X] T025 **DONE 2026-07-01** — flipped the PY-005 OFFICIAL row in `spec/feature-catalogue.md`
  `backlog → done` (specify=056, PR="— (verify + Gate B pending)", Tests/Verified columns populated with
  the wheel suite + NBC guards + the 2 fixed ship-blockers). The matching `spec/coverage-index.md:618`
  PY-005 entry was updated by T023 (wheel-name flip to `cp310-abi3`). PR number lands at merge.
  **Catalogue close-out** (depends on T023 — shares `spec/coverage-index.md`; NOT `[P]` with it):
  flip every feature-owned OFFICIAL row in `spec/feature-catalogue.md` (PY-005) from
  `in-progress`/`backlog` → `done` with the PR / evidence ref, AND add/update its matching
  `spec/coverage-index.md` entry (the wheel-name update of T023 is part of this).
- [X] T026 **DONE 2026-07-01** — verdict recorded in
  `.specify/decisions/056-python-wheel-packaging-completeness.md`: **100% of implement-scope complete**.
  (i) all T001–T026 `[X]` (T015 not-triggered + T021 deferred count; vestigial `[ ] T012` removed);
  (ii) every FR-001..012 + SC-001..007 maps to a landed test AND implementation (traceability table in
  the verdict); (iii) PY-005 catalogue row `done` + `coverage-index.md:618` entry updated. **ONE tracked
  verify-stage obligation** (not an implement gap): `/speckit-verify` owes the in-tree none/asan/tsan
  matrix (NBC-2 leg of FR-012/SC-007) + Gate B owes a sub-interp-contract re-check, because the SWIG
  is-main + argout fixes changed 055's runtime binding (validated on the 4.4.1 wheel, not yet on the
  in-tree 4.2 sanitizer build).
  **Feature-completeness audit (FINAL task)**: assert against the merged tree that (i) every
  `tasks.md` row is `[X]` or carries an explicit waiver rationale (T015 "not triggered" counts); (ii) every
  spec FR-001..012 and SC-001..007 maps to a landed test AND a landed implementation (use the traceability
  map below); (iii) the PY-005 OFFICIAL catalogue row is `done` with a matching `coverage-index.md` entry.
  Record the verdict (100% or fully-waived) in `.specify/decisions/056-python-wheel-packaging-verify.md`
  (`## Completeness`) or a sibling `…-completeness.md`. **Hard `/gate-b` precondition (Article XVII §8 /
  pre-flight 4d).**

---

## Requirement → Task Traceability

> Written so `/speckit-analyze`'s coverage pass and the T026 audit come back clean. Every FR / SC and
> every contract rule maps to ≥1 task.

| Req | Tasks |
|---|---|
| FR-001 single abi3 wheel | T002, T004, T005, T010, T011 |
| FR-002 self-contained (empty external list) | T010 (with_otel=False + static OpenSSL), T011 (auditwheel show) |
| FR-003 flat module surface | T006 |
| FR-004 bundled dict data | T007 |
| FR-004a pure-Python locator (via `fixpp`) | T008, T009 |
| FR-005 manylinux_2_28 tag | T010 (auditwheel repair), T011 |
| FR-006 Tier-1 build + per-interpreter install-test | T013, T016 |
| FR-007 functional subset excludes canary; sanitizer lanes intact | T012, T016 (CI-8) |
| FR-008 release attach, no PyPI | T020 |
| FR-009 gate fails on broken artifact | T016 (CI-5), T019 |
| FR-010 abi3 primary; per-version fallback | T004, T013, T015 |
| FR-011 Windows deferred/best-effort, separable | T021 |
| FR-012 no C-ABI / no binding-behaviour change | T003 (preserve 1201), T017 (NBC-1), T013 (NBC-2 suite green), T018 (NBC-3) |
| SC-001 clean-env install+import on each 3.10–3.13 | T013 |
| SC-002 round-trip via `dictionary_path` | T012 |
| SC-003 functional subset green vs installed wheel, every PR | T012, T016 |
| SC-004 tags cp310-abi3 + manylinux_2_28, exactly one wheel | T011 |
| SC-005 release asset present, no PyPI | T020 |
| SC-006 broken wheel → gate red | T019 |
| SC-007 C-ABI byte-frozen + behaviour unchanged + sub-interp witness | T003, T013, T017, T018 |
| PKG-1..6 | T001, T002, T005; **PKG-2** (dynamic version) also T014 |
| LAY-1..4 | T006, T007, T011, T012 |
| TAG-1..3 | T010, T011 |
| LOC-0..6 | T008, T009, T012 |
| CI-1..8 | T016 |
| REL-1..3 | T020 |
| WIN-1 | T021 |
| NBC-1..3 | T017, T013, T018 |

> **T022** (quickstart validation) is the manual mirror of **SC-001/002/003/004** (build → install →
> round-trip → tag check); **T015** is the FR-010 per-version fallback contingency (closed "not triggered"
> when T004 + T013 pass). Both are intentionally process/contingency tasks, not new-requirement coverage.

---

## Dependencies & Execution Order

- **Setup (T001)** → no deps.
- **Foundational (T002)** → after T001; **blocks all stories**.
- **US1 (T003–T015)** → after T002. Internal order: **T003 → T004 (abi3 gate) → T005/T006 → T010 → T011/T013**;
  T007/T008 are `[P]` (independent files), T009 after T008, T012 before T013, T015 only on a red T004/T013.
- **US2 (T016–T020)** → after US1 produces a buildable wheel (CI invokes the US1 build). T017/T018 `[P]`.
- **US3 (T021)** → after US1; independent of US2; never gates Linux.
- **Polish (T022–T026)** → after the desired stories. T023 (amendments) + T024 (B&L) + T025 (catalogue) →
  then **T026 the FINAL audit**.

### Parallel Opportunities

- T007 ‖ T008 (data package ‖ locator module — different files).
- T017 ‖ T018 (CI freeze guard ‖ import-surface snapshot).
- T025 may overlap T022/T024 (quickstart run / B&L append), but runs **after T023** (both write
  `spec/coverage-index.md`) — so T025 is not `[P]`.

---

## Implementation Strategy

**MVP = US1**: Setup → Foundational → US1 (the abi3 gate T004 is the make-or-break; if it or T013 fires red,
fall to T015 per-version — same downstream CI). Stop and validate: build locally, install on each of
3.10–3.13, round-trip. Then layer US2 (CI gate + release), then the deferred US3 (Windows). Polish closes the
inherited-doc amendments, B&L, catalogue, and the mandatory completeness audit last.
