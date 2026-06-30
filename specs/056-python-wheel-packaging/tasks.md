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
- [ ] T012 [US1] Create the dedicated `bindings/python/tests/wheel/` suite — **the canonical
  "functional install-verification subset"** (D-8 / E-6, LOC witnesses): imports **only installed
  modules** and resolves every dictionary through `fixpp.dictionary_path(...)` (never a repo-relative
  path). **Enumerated membership** = the locator-using ports of the round-trip, smoke, exception,
  lifetime, and OO-behaviour tests + the sub-interpreter test, **excluding `test_gil_release_canary.py`**
  (the only test needing the deliberate-hang `FIXPP_PY_GIL_RELEASE_CANARY` build — stays in the in-tree
  sanitizer matrix, FR-007). Cover: FIX 4.4 round-trip from the bundled dict (SC-002 / LOC-2),
  `BUNDLED_DICTIONARIES` set-equality (LOC-1), `dictionary_bytes` XML-prolog (LOC-3),
  `pytest.raises(KeyError)` on an unknown name asserting the sorted set is named (LOC-4),
  `import fixpp`/`fixpp_oo`/`fixpp_dict_data` (LAY-4). **Sub-interpreter test note (F1)**: the 055
  `test_subinterpreter.py` is **locator-independent** — it loads no dictionary (only `import fixpp` →
  `Engine(cfg)` → assert code 1201), so it ports into this suite **as-is** with no dict-helper fallback
  (E-6 approach (a) is moot for it). Out-of-repo harness scrubs `PYTHONPATH` and asserts
  `os.path.realpath(fixpp.__file__).startswith(sys.prefix)` (quickstart §4) so no source tree shadows.
- [ ] T013 [US1] **Runtime cross-version witness [verify at implement]** (the abi3 proof, D-3/D-8,
  SC-001/007): for EACH of CPython 3.10/3.11/3.12/3.13, `pip install` **the one wheel** into a clean
  venv, `import fixpp`, run the T012 wheel suite + the sub-interpreter test against the installed
  package. **3.10/3.11 is the concentrated verify band** — no import barrier there, so the reworked
  runtime 1201 check (T003) is the sole, previously-unwitnessed rejection mechanism (NBC-2/SC-007).
  A **red** runtime import on any version is the trigger for the per-version fallback (T015).
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

- [ ] T016 [US2] Add the **`python-wheel` Tier-1 job** to `.github/workflows/tier1.yml` (CI-1..8, D-9):
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
- [ ] T017 [P] [US2] **NBC-1** C-ABI freeze guard: a CI step that **fails on any diff to
  `include/fix/c_api*.h`** (the `0→1` GA freeze is byte-frozen) and runs the existing ABI/header-occupancy
  check (SC-007).
- [ ] T018 [P] [US2] **NBC-3** import-surface snapshot test (in `tests/wheel/`): assert `import fixpp`
  still resolves **every** existing name/class (flat functions, `FixppError`/`Error`, the OO classes, the
  new locator) — guards the additive `%pythoncode` re-export (T009) from dropping a symbol.
- [ ] T019 [US2] **Broken-wheel negative gate** `bindings/python/tests/wheel/test_broken_wheel_gate.sh`
  (SC-006 named witness): copy a built wheel, remove `_fixpp_data/FIX44.xml` from the copy, install the
  mutated copy into a throwaway venv, run the locator/round-trip, **assert non-zero**. Non-publishing
  (never uploads or pollutes the real artifact); wired as a CI step that flips the gate red (FR-009).
- [ ] T020 [US2] **Release-attach** step (REL-1/3, FR-008): on a GitHub **release** event, upload the
  single `cp310-abi3` Linux wheel as a release **asset**. Assert **no** `twine` / `pypa/gh-action-pypi-publish`
  / index-upload step exists anywhere in the workflow (REL-2 / SC-005 / `[const §IV.5]`).

**Checkpoint**: the wheel is produced + install-verified + released by CI; broken artifacts fail.

---

## Phase 5: User Story 3 - Best-effort Windows wheel (Priority: P3)

**Goal**: Provide the Windows packaging glue as a separable, non-gating lane (deferred/best-effort).

**Independent Test**: on the on-demand Windows lane, build the wheel, install into a clean Windows Python
env, run the functional subset; its absence/failure does not affect the Linux gate.

- [ ] T021 [US3] Document the Windows deferral + add an **on-demand** `cibuildwheel` Windows stub
  (D-10 / FR-011 / WIN-1): a `windows`-labelled / nightly lane reusing the same source + the existing
  green MSVC C-ABI build, with `delvewheel` glue. **MUST be separable — never a Linux merge-gate
  dependency**; build it only if trivially cheap, otherwise capture as a deferred limitation. Record the
  disposition (built vs deferred) as an `L-056-*` row in `spec/behaviors-and-limitations.md` (T024).

---

## Phase 6: Polish & Cross-Cutting Concerns

- [ ] T022 Run the quickstart.md validation end-to-end (§1 build → §2 install → §3 round-trip → §4 subset
  → §5 tag/self-containment) as the manual mirror of the CI gate.
- [ ] T023 **Apply the inherited-design amendments A-1 / A-2 / A-3** (deferred-apply at `/implement`;
  **user-ratified at Gate A**) per the exact from→to table in `plan.md` (*Proposed inherited-design
  amendments*). Sites: `spec/coverage-index.md:618` (PY-005 wheel name, normative index), `.specify/api-contract.md`
  (§10 row 2m), `.specify/2m-pybind.md` (§3.2 mirror :149/151, §1 goal-5 :23 incl. **dropping the stale
  "mimalloc" clause**, §1.1 :30, non-goal #2 :110), `.specify/architecture.md:468` (build-output table row),
  and the library `CLAUDE.md` "Active work" line (refresh the stale "per-version" pointer to the abi3 wheel).
  **CRITICAL — A-3 item 2b (`api-contract.md:316`/`:334`): ANNOTATE each historical resolution quote with a
  forward-pointer ("superseded by the abi3 pivot — see PY-005 / `[arch §7.1]` amendment"); do NOT rewrite the
  quote — the audit trail must not be falsified.** Verify with `grep -rn "cp310-cp310" .specify/ spec/` → no
  live normative source names the per-version mandate afterward.
- [ ] T024 Append `spec/behaviors-and-limitations.md` `L-056-*` rows: Windows wheel deferred/best-effort
  (T021), the per-version fallback contingency (T015, abi3-untriggered), and 3.14+ covered-by-abi3-but-
  untested-in-v1 (PKG-3).

### Mandatory close-out tasks (ALWAYS emit — Gate-B preconditions, Article XVII §8)

- [ ] T025 **Catalogue close-out** (depends on T023 — shares `spec/coverage-index.md`; NOT `[P]` with it):
  flip every feature-owned OFFICIAL row in `spec/feature-catalogue.md` (PY-005) from
  `in-progress`/`backlog` → `done` with the PR / evidence ref, AND add/update its matching
  `spec/coverage-index.md` entry (the wheel-name update of T023 is part of this).
- [ ] T026 **Feature-completeness audit (FINAL task)**: assert against the merged tree that (i) every
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
