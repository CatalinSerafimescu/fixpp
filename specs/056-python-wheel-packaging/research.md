# Phase 0 Research: Python Wheel Packaging (PY-005)

All decisions resolve the Technical Context unknowns. Empirical witnesses that
require a full container build are flagged **[verify at implement]** — the first
CI wheel build is the witness; nothing here ships unproven.

## D-1 — PEP 517 build backend: `scikit-build-core`

**Decision**: Use `scikit-build-core` as the build backend in a new
`bindings/python/pyproject.toml`, invoking the **existing** CMake
`FIXPP_BUILD_PYTHON` target. No second build system.

**Rationale**: The binding already builds via CMake (`bindings/python/CMakeLists.txt`,
SWIG + static-link). `scikit-build-core` is the current standard CMake-backed PEP
517 backend; it drives `cmake --build`, honours CMake presets/toolchain files,
and packages the produced `_fixpp*.so` + `fixpp.py` + `fixpp_oo.py`. `setuptools`
+ a hand-written `build_ext` would re-implement what CMake already does.

**Alternatives**: `setuptools` with a CMake shim (more glue, duplicates logic);
`meson-python` (would require a second build system — rejected, Article II §1
mandates CMake).

## D-2 — Toolchain inside manylinux_2_28 (LOAD-BEARING)

**Decision**: Build inside `quay.io/pypa/manylinux_2_28_x86_64` using its
**gcc-toolset (≥13)**, with a Conan profile generated **in-container** to match
that compiler (`compiler=gcc`, `compiler.version=<toolset major>`,
`compiler.libcxx=libstdc++11`, `compiler.cppstd=23`). Conan runs
`--build=missing` inside the container. Do **not** fight to install Clang-22 in
the container.

**Rationale**: The library requires C++23 with `std::expected`. The project's
own `conan/profiles/linux-gcc-release` pins **gcc-13 / libstdc++11 / cppstd=23**
and the Tier-1 `linux-gcc-release` leg compiles the whole tree green with
gcc-13 — so a gcc-13+ toolset is a proven-good path. manylinux_2_28 (AlmaLinux 8)
provides gcc-toolset packages; recent images ship gcc-toolset ≥13. The Clang-22
acrobatics in the existing `python-bindings` job exist only because the runner's
default unversioned clang resolves to clang-18 with stale libstdc++ headers —
irrelevant inside the container where we pick the toolset explicitly.

**Mechanism**: `cibuildwheel` `CIBW_BEFORE_ALL` enables the gcc-toolset
(`source /opt/rh/gcc-toolset-NN/enable`) and `conan profile detect` + writes the
matching profile; `CIBW_ENVIRONMENT` exports `CC`/`CXX`. Conan package cache is
mounted/cached so the ×4 Python-version builds don't each rebuild deps.

**[verify at implement]**: confirm the pinned manylinux_2_28 image's
gcc-toolset major ≥ 13 and that a one-file `std::expected` TU compiles in-container
before wiring the full matrix. If the image's toolset is < 13, pin an image tag
that ships ≥13 or install `gcc-toolset-13`.

**Alternatives**: Clang inside the container (extra install, no benefit over the
proven gcc path); building outside manylinux then `auditwheel`-repairing a
non-manylinux build (fragile — glibc baseline not guaranteed).

## D-3 — Python ABI: per-version wheels, abi3 NOT attempted in v1

**Decision**: Ship **per-version** wheels `cp310/cp311/cp312/cp313` (the
`[2m §1.1]` named matrix). **Do not** attempt an abi3/limited-API wheel in this
feature.

**Rationale**: `[2m §1.1]` names per-version manylinux_2_28 wheels and the
mandatory artifact `fixpp-<ver>-cp310-cp310-manylinux_2_28_x86_64.whl` — a
per-version tag. SWIG-generated wrappers historically use non-limited-API calls,
so abi3 is uncertain effort; the spec (FR-010) makes it optional, not required.
Per-version via `cibuildwheel` is cheap (it already iterates interpreters) and is
the normative target. abi3 stays a documented post-v1 optimisation.

**Alternatives**: abi3-primary (rejected — contradicts the `[2m §1.1]` wheel name
and risks a SWIG/limited-API fight for no v1 requirement).

## D-4 — Wheel module layout: flat top-level modules (fixes the install bug)

**Decision**: Ship `_fixpp*.so`, `fixpp.py`, and `fixpp_oo.py` as **top-level
modules** in the wheel (site-packages root), per the clarification ("two modules,
as-is"). **Fix** the existing `install(TARGETS fixpp_py DESTINATION
${Python3_SITEARCH}/fixpp)` + file installs, which place `fixpp.py`/`fixpp_oo.py`
inside a `fixpp/` directory with **no `__init__.py`** — a PEP-420 namespace dir
that breaks `import fixpp` (the module becomes `fixpp.fixpp`). This only "worked"
in CI because tests run with `PYTHONPATH=<build>/lib` (flat).

**Rationale**: The clarification fixes the public surface as two top-level
modules; flat is the layout that makes `import fixpp` / `import fixpp_oo` resolve
correctly. `scikit-build-core` `wheel.install-dir`/`tool.scikit-build` packaging
maps the built `lib/` artifacts to the wheel root.

**Consequence**: the in-tree `install()` rules are corrected to flat (or the
wheel packaging bypasses them via scikit-build-core's own file selection). Either
way the **installed** layout is now tested (FR-006), closing the untested-install
gap. This is packaging only — no change to module contents (FR-012).

**Alternatives**: restructure into a `fixpp/` package with `__init__.py`
re-exporting OO (rejected by the clarification — renames the frozen flat module).

## D-5 — Dictionary data home + locator helper (FR-004 / FR-004a)

**Decision**: Ship the four FIX XMLs inside a small **importable data package**
`_fixpp_data/` (`__init__.py` + the XMLs), and add a pure-Python locator module
`fixpp_dict_data.py` exposing a by-name resolver built on `importlib.resources`,
**re-exported through `fixpp`** (one additive line in the existing `%pythoncode`
glue) so the public names are `fixpp.dictionary_path` / `fixpp.dictionary_bytes`
/ `fixpp.BUNDLED_DICTIONARIES`.

**Rationale**: Top-level modules cannot host package data cleanly; a tiny data
package is the idiomatic `importlib.resources` host. The XMLs are copied (or
configured-in) from the repo `dictionaries/` at build time so there is one source
of truth. The locator returns a filesystem path (via
`importlib.resources.as_file`) suitable for the existing C-ABI
`dict_load_from_xml(path)` entry, with no user-supplied file. **Surfacing it
through `fixpp`** (not as a standalone top-level import) matches the verified
public-surface pattern: `fixpp.py`'s `%pythoncode` glue already re-exports the
flat functions, `Error`, and the OO classes from `fixpp_oo` (confirmed at
`fixpp.i` ~L742 `from fixpp_oo import Engine, Session, …`). `fixpp_dict_data.py`
mirrors `fixpp_oo.py` as an implementation module.

**Locator shape** (contract in `contracts/`):
`fixpp.dictionary_path(name: str) -> contextmanager[str]` and/or
`fixpp.dictionary_bytes(name) -> bytes`, where `name ∈ {"FIX42","FIX44","FIX50SP2","FIXT11"}`.

**Alternatives**: data inside a `fixpp/` package (rejected — needs the package
restructure D-4 declined); absolute build-host paths (rejected — non-portable,
the exact bug being fixed).

## D-6 — Wheel version source

**Decision**: Derive the wheel version from the CMake `project(... VERSION)`
(currently `0.0.1`) via `scikit-build-core`'s `metadata.version` dynamic
provider (`tool.scikit-build.metadata.version.provider`), so the wheel version
tracks the library version with no second source of truth.

**Rationale**: Avoids a drifting hard-coded version in `pyproject.toml`. The
`<ver>` in `fixpp-<ver>-cp310-...` then follows the project version automatically.

**[verify at implement]**: confirm scikit-build-core reads the CMake project
version cleanly; else fall back to a single static `project.version` in
`pyproject.toml` kept in sync by a CI check.

**Version-value note (not a PY-005 decision)**: the project version is `0.0.1`
today, so the wheel would ship as `fixpp-0.0.1-…`. The v1.0 release-gate bump to
`1.0.0` is a separate release-engineering step (it bumps `project(VERSION)` once,
which this wheel then inherits automatically). PY-005 wires the version *source*;
it does not set the release number.

## D-7 — Conan inside the container / build-graph cost

**Decision**: `cibuildwheel` `CIBW_BEFORE_ALL` runs `conan install . --build=missing`
once per container into a cached `~/.conan2`; the per-interpreter wheel builds
reuse the cache (only the SWIG extension + final link differ by Python version).
The CI job caches `~/.conan2/p` keyed on `conanfile.py` + profile, mirroring the
existing `python-bindings` job.

**Rationale**: Conan dep compilation is the dominant cost; building it once and
caching keeps the 4-version matrix tractable. This is the "non-trivial build
graph" risk (`remaining-work` Key Risk 4) — mitigated by cache + single Conan
install.

## D-8 — Functional install-verification subset (FR-006 / FR-007)

**Decision**: Against the installed wheel, run the binding `tests/` **excluding
`test_gil_release_canary.py`** (the only test that requires a deliberate-hang
special build, `FIXPP_PY_GIL_RELEASE_CANARY`). All other tests run in a normal
(non-instrumented) build. Selection is via a pytest marker or an explicit `-k`/
deselect, recorded in the CI job.

**Dictionary-path fix for the installed run**: the existing
`oo_test_support.py` / `test_roundtrip.py` resolve the dict via a **repo-relative
path** (`<repo>/dictionaries/FIX44.xml`) that does not exist in an installed
wheel. Their `dict_path()` / `_dict_path()` helpers are updated to **prefer the
repo-relative path when present, else fall back to the `fixpp_dict_data` locator**
— so the same suite runs both in-tree and against the installed wheel. (Pure test
support change; no production behaviour change.)

**Sanitizer/canary lanes stay put**: the existing `python-bindings` ASan/TSan/none
CTest matrix and the local-only GIL canary are untouched (FR-007). The wheel job
is additive.

**[verify at implement]**: `test_subinterpreter.py` (FR-018) was witnessed on
CPython 3.12 (055 W-2). Confirm it passes or version-guards cleanly on 3.10/3.11/
3.13; add `sys.version_info` skips if sub-interpreter behaviour differs, without
weakening the 3.12 witness.

## D-9 — CI placement (Tier-1 mandatory gate)

**Decision**: Add a **new Tier-1 job `python-wheel`** to `.github/workflows/tier1.yml`
(required to merge, `[const §IX.6]`) that builds the Linux cp310–cp313 wheels via
`cibuildwheel`, installs each into a clean venv, and runs the D-8 subset against
the installed wheel. On a release event a separate step (release workflow) attaches
the wheels as assets (FR-008). The job fails on build/install/test failure (FR-009).

**Rationale**: Matches the clarification (Tier-1 mandatory). Distinct from the
existing `python-bindings` sanitizer job (which tests the in-tree build under
sanitizers); this one tests the **shippable artifact**. Conan cache shared by key.

**Cost note**: a 4-interpreter manylinux build is heavier than the in-tree job;
mitigated by Conan caching and that the deps compile once per container (D-7).

## D-10 — Windows (deferred / best-effort)

**Decision**: **Do not** build the Windows wheel in this feature's required scope
(`[2m §10 Q3]` deferred; `[const §IV.3]` best-effort). Leave a documented
`cibuildwheel` Windows configuration stub + a `windows`-labelled / on-demand lane
as optional follow-up; its absence does not gate Linux (FR-011). The MSVC C-ABI
build is already green (Tier-2), so the residual is `delvewheel` glue — captured
as a deferred limitation, not built here unless trivially cheap.

**Rationale**: Keeps the feature scoped to the mandatory Linux deliverable; avoids
enshrining Windows packaging the normative doc deferred.

---

## Resolved unknowns summary

| Unknown | Resolution |
|---|---|
| Build backend | scikit-build-core over existing CMake (D-1) |
| Compiler in manylinux | gcc-toolset ≥13, in-container Conan profile (D-2) |
| abi3 vs per-version | per-version cp310–cp313; abi3 deferred (D-3) |
| Module layout | flat top-level modules; fix namespace-dir install bug (D-4) |
| Dict data + access | `_fixpp_data/` package + `fixpp_dict_data` locator (D-5) |
| Wheel version | from CMake project version via scikit-build-core (D-6) |
| Build-graph cost | single in-container Conan install + cache (D-7) |
| Test subset | all tests minus `test_gil_release_canary.py`; dict-path fallback (D-8) |
| CI placement | new Tier-1 `python-wheel` job (D-9) |
| Windows | deferred/best-effort, not built here (D-10) |

No NEEDS CLARIFICATION remain. Items marked **[verify at implement]** are empirical
witnesses owned by the first CI build, not open design questions.
