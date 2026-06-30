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
`meson-python` (would require a second build system — rejected: `[const §III.1]`
mandates CMake + Ninja as the build system; Article II §1 is the C++23 language
standard).

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
mounted/cached so the single abi3 build (and any per-version fallback) does not
rebuild deps.

**[verify at implement]**: confirm the pinned manylinux_2_28 image's
gcc-toolset major ≥ 13 and that a one-file `std::expected` TU compiles in-container
before wiring the full matrix. If the image's toolset is < 13, pin an image tag
that ships ≥13 or install `gcc-toolset-13`.

**Alternatives**: Clang inside the container (extra install, no benefit over the
proven gcc path); building outside manylinux then `auditwheel`-repairing a
non-manylinux build (fragile — glibc baseline not guaranteed).

## D-3 — Python ABI: single abi3 (stable-ABI) wheel — PRIMARY (abi3 pivot, Gate A)

**Decision**: Ship **one stable-ABI (abi3 / `Py_LIMITED_API=0x030A0000`) wheel**
`fixpp-<ver>-cp310-abi3-manylinux_2_28_x86_64.whl` covering CPython 3.10–3.13+.
Per-version `cp3XX-cp3XX` wheels are retired to a **documented fallback**, used
only if the abi3 wheel's runtime cross-version import (D-8 / FR-006) proves flaky.

**Rationale (empirically established — abi3-first restored)**: The original intent
was abi3-first (`remaining-work/python-bindings.md:61`: *"TRY abi3 → one wheel
covers 3.x+; fall back to per-version if SWIG fights us"*). The **compile-time**
SWIG limited-API feasibility fallback did **not** fire — abi3 is feasible against
the installed toolchain at the API-surface level (below). The **runtime**
import/round-trip fallback trigger remains the CI install/import/round-trip witness
across CPython 3.10–3.13 and must fire **red** before any per-version fallback is
produced (`[verify at implement]`, see below):

- The installed **SWIG is 4.2.0**. Its limited-API mode emits heap types via
  `PyType_FromSpec` (NOT the static `PyTypeObject` that blocks older SWIG); the
  hard floor is **Python 3.10** because `PyUnicode_AsUTF8AndSize` entered the
  limited API in 3.10 (hence the `cp310` floor / `0x030A0000`).
- **Empirical**: the generated 6127-line `fixpp_wrap.cxx` compiles
  `-fsyntax-only -DPy_LIMITED_API=0x030A0000` against Python 3.12 headers with
  **exactly one** limited-API violation: `PyInterpreterState_Main()` inside
  `fixpp_py_is_main_interpreter` (`bindings/python/fixpp.i:596`), which backs the
  sub-interpreter-rejection behaviour (the 055 sub-interpreter witness).
- So the old rationale ("SWIG-generated wrappers historically use non-limited-API
  calls, so abi3 is uncertain") is **overturned for SWIG 4.2.0**: the only blocker
  is one helper.

**Build change set (bounded — not a redesign)**: (a) a ~10–30-line limited-API
rework of `fixpp_py_is_main_interpreter` that **preserves** the
sub-interpreter-rejection behaviour (the 055 witness must stay green).
`PyInterpreterState_Main()` (`fixpp.i:597`) is **not** in the limited API and **is**
the runtime sub-interpreter-rejection mechanism (→ code 1201). The **proposed**
limited-API replacement (`[verify at implement]` — confirm both symbols are in the
limited API at the `0x030A0000` floor): capture the main interpreter's id at module
init and, in the helper, compare it against
`PyInterpreterState_GetID(PyInterpreterState_Get())` — both `PyInterpreterState_Get`
and `PyInterpreterState_GetID` are in the limited API, and this realigns the impl
with the mechanism `[2m §6.1]` (`2m-pybind.md:1043`) already describes
(`Engine.__init__` records main-interpreter state via `PyInterpreterState_Get()`).
**Concentrated verify band — CPython 3.10/3.11**: the 055 `test_subinterpreter.py`
witness passed on 3.12 via CPython's single-phase-init **import barrier** (3.12+
only), which fails `import fixpp` inside a sub-interpreter **before** the runtime
1201 check is ever reached — so the reworked check was **never behaviorally
witnessed there**. On 3.10/3.11 there is **no** import barrier, so rejection falls
solely to this reworked runtime check; it is load-bearing and previously-unwitnessed
on exactly those two versions. D-8's per-version sub-interpreter test on 3.10/3.11
is the realizing witness (the import barrier is absent there, so the 1201 path is
actually exercised). (b)
`-DPy_LIMITED_API=0x030A0000` (compile-only) + the abi3 wheel tag from
scikit-build-core's `wheel.py-api = "cp310"`, wired through
scikit-build-core/cibuildwheel; (c) **raise the SWIG floor 4.0 → 4.2 and PIN it**
(a 4.0 runner silently regresses the limited-API mode — PKG-1, Technical Context).
None of this touches the C-ABI surface or binding *behaviour* (FR-012).

**[verify at implement]**: `-fsyntax-only` proves the **API surface**, not the
**runtime import round-trip** (the limited-API `__name__` string-compare type
checks). Before shipping: build the abi3 wheel, `import fixpp`, run the loopback
round-trip + the sub-interpreter witness on each of 3.10/3.11/3.12/3.13. A runtime
import failure on any targeted version is the trigger to fall back to per-version.

**Alternatives**: per-version cp310–cp313 (now the documented **fallback** only,
not the primary path — retained for the import-flaky contingency).

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

**Consequence (fix, not bypass — decided at Gate A)**: the in-tree
`install(TARGETS fixpp_py DESTINATION ${Python3_SITEARCH}/fixpp)` + file-install
rules (`bindings/python/CMakeLists.txt:122–133`) are **corrected to a flat
top-level destination** — we do NOT leave the namespace-dir bug live in the CMake
rule and merely sidestep it with scikit-build-core file selection. Fixing the rule
keeps the in-tree `cmake --install` path correct too (not just the wheel path),
and a witness covers whichever path ships (the installed-wheel `import fixpp` in
FR-006 + an in-tree install-layout check). This is packaging only — no change to
module contents (FR-012).

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

## D-7 — Conan inside the container / build-graph cost + self-containment options

**Decision**: `cibuildwheel` `CIBW_BEFORE_ALL` runs
`conan install . --build=missing -o fixpp/*:with_otel=False -o openssl/*:shared=False`
once per container into a cached `~/.conan2`. The **`with_otel=False`** option is
load-bearing for self-containment: the default `with_otel=True`
(`conanfile.py:109`) drags `fixpp_otel` → `opentelemetry-cpp` → gRPC/protobuf into
the link; disabling it (the binding consumes only the C-ABI, which needs no OTel)
keeps the static tree small. **Static OpenSSL** (`-o openssl/*:shared=False`, the
Conan default but pinned explicitly) ensures `fixpp_tls` → OpenSSL 3.6.2
(`conanfile.py:69`) is statically absorbed into `_fixpp.so` rather than leaving a
shared `libssl`/`libcrypto` that `auditwheel` would vendor. The result is a
**static-everything** wheel with **no external `.so`** — `auditwheel show`'s
external-library list is empty (the LAY-3 / FR-002 witness). This is the recorded
static decision (Assumptions), not a vendored-deps wheel.

Since the build is now a single abi3 wheel, the wheel link itself runs **once**;
only the install-test iterates the four interpreters against that one `.so`.

**Rationale**: Conan dep compilation is the dominant cost; building it once and
caching (`~/.conan2/p` keyed on `conanfile.py` + profile, mirroring the existing
`python-bindings` job) keeps the build tractable. This is the "non-trivial build
graph" risk (`remaining-work` Key Risk 4) — mitigated by cache + single Conan
install + `with_otel=False` pruning the OTel/gRPC/protobuf subtree.

## D-8 — Functional install-verification subset (FR-006 / FR-007)

**Decision**: Against the installed wheel, run the binding `tests/` **excluding
`test_gil_release_canary.py`** (the only test that requires a deliberate-hang
special build, `FIXPP_PY_GIL_RELEASE_CANARY`). All other tests run in a normal
(non-instrumented) build. Selection is via a pytest marker or an explicit `-k`/
deselect, recorded in the CI job.

**Install harness model (decided — dedicated wheel suite over absolute paths)**:
the installed-wheel run uses a **dedicated `bindings/python/tests/wheel/` suite**
that imports **only installed modules** (`import fixpp`, never a repo-relative path
or a `dictionaries/` directory) and resolves every dictionary through
`fixpp.dictionary_path(...)`. This sidesteps the "every selected test carries its
own repo-relative dict helper" problem cleanly: rather than chase every helper, the
wheel subset is **scoped to tests that use the locator**. This one harness model is
applied consistently across D-8, data-model E-6, ci-wheel-gate CI-4, quickstart §4,
and locator-api LOC-5.

**Dictionary-path fallback (for the shared in-tree tests reused by the wheel
suite)**: tests reused from the in-tree suite that resolve the dict via a
**repo-relative path** (`<repo>/dictionaries/FIX44.xml`) — which does not exist in
an installed wheel — get a helper that **prefers the repo-relative path when
present, else falls back to the `fixpp_dict_data` locator**. This MUST cover
**every** selected test's dict helper, not just `oo_test_support.py` /
`test_roundtrip.py` — `test_lifetime.py`, `_gil_staging.py`, and the
sub-interpreter test carry their own repo-relative helpers; either broaden the
fallback to all of them or scope the wheel subset to locator-using tests only
(the dedicated-suite model above does the latter). (Pure test support change; no
production behaviour change.)

**Sanitizer/canary lanes stay put**: the existing `python-bindings` ASan/TSan/none
CTest matrix and the local-only GIL canary are untouched (FR-007). The wheel job
is additive.

**[verify at implement] — runtime import round-trip (the abi3 witness)**: build the
abi3 wheel, `pip install` it into a clean venv on **each** of CPython
3.10/3.11/3.12/3.13, `import fixpp`, run the loopback round-trip and the
sub-interpreter test (the 055 sub-interpreter witness — witnessed on CPython 3.12
under 055 W-2) on every interpreter. `-fsyntax-only` (D-3) proves the API surface
but NOT this runtime round-trip; add `sys.version_info` skips only if
sub-interpreter behaviour genuinely differs across versions, without weakening the
3.12 witness. A runtime import failure on any targeted version is the
per-version-fallback trigger (D-3).

## D-9 — CI placement (Tier-1 mandatory gate)

**Decision**: Add a **new Tier-1 job `python-wheel`** to `.github/workflows/tier1.yml`
(required to merge, `[const §IX.6]`) that builds the **single** Linux abi3 wheel via
`cibuildwheel`, installs **that one wheel** into a clean venv on **each** of CPython
3.10/3.11/3.12/3.13, and runs the D-8 subset against the installed wheel on every
interpreter. On a release event a separate step (release workflow) attaches the
wheel as an asset (FR-008). The job fails on build/install/test failure (FR-009).

**Rationale**: Matches the clarification (Tier-1 mandatory). Distinct from the
existing `python-bindings` sanitizer job (which tests the in-tree build under
sanitizers); this one tests the **shippable artifact**. Conan cache shared by key.

**Cost note**: a 4-interpreter manylinux build is heavier than the in-tree job;
mitigated by Conan caching and that the deps compile once per container (D-7).

## D-10 — Windows (deferred / best-effort)

**Decision**: **Do not** build the Windows wheel in this feature's required scope
(`[2m §2 non-goal #4]` / `[2m §1.1]` deferred; `[const §IV.3]` best-effort). Leave a documented
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
| abi3 vs per-version | **single abi3 wheel PRIMARY** (SWIG 4.2.0 feasible); per-version is the documented fallback (D-3) |
| Module layout | flat top-level modules; **fix** (not bypass) namespace-dir install bug (D-4) |
| Dict data + access | `_fixpp_data/` package + `fixpp_dict_data` locator (D-5) |
| Wheel version | from CMake project version via scikit-build-core (D-6) |
| Build-graph cost + self-containment | single in-container Conan install + cache; `with_otel=False` + static OpenSSL → empty external list (D-7) |
| Test subset | dedicated `tests/wheel/` locator-using suite vs installed wheel; canary excluded (D-8) |
| CI placement | new Tier-1 `python-wheel` job, one wheel × 4-interpreter install-test (D-9) |
| Windows | deferred/best-effort, not built here (D-10) |

No NEEDS CLARIFICATION remain. Items marked **[verify at implement]** are empirical
witnesses owned by the first CI build, not open design questions.
