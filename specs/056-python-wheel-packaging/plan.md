# Implementation Plan: Python Wheel Packaging (PY-005)

**Branch**: `056-python-wheel-packaging` | **Date**: 2026-06-30 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/056-python-wheel-packaging/spec.md`

## Summary

Package the already-frozen Python binding (PY-001..004: `_fixpp` extension +
`fixpp.py` + `fixpp_oo.py`) into per-version **manylinux_2_28** binary wheels for
**CPython 3.10–3.13** (`[2m §1.1]`), built and install-tested in **Tier-1 CI** as
a mandatory merge gate, and attached to GitHub releases (no PyPI, `[const §IV.5]`).
Technical approach: a `scikit-build-core` PEP 517 backend over the existing CMake
`FIXPP_BUILD_PYTHON` target, driven by `cibuildwheel` inside the `manylinux_2_28`
container (Conan `--build=missing` in-container), `auditwheel repair` for the
portable tag; FIX dictionary XMLs shipped via a tiny data package reachable by a
new pure-Python locator helper; the public surface stays the two existing
top-level modules `fixpp` + `fixpp_oo`, shipped **flat** (fixing the latent
`install(... /fixpp)` namespace-dir layout that only worked under the in-tree
`PYTHONPATH=lib`). No C-ABI change; the `0→1` freeze is untouched.

## Technical Context

**Language/Version**: Python packaging (PEP 517/518) over C++23 build; CPython
3.10–3.13 targets. Native extension already built by SWIG 4.x + Clang/GCC.
**Primary Dependencies** (build-time, lower-bound pins; exact versions verified
against PyPI at implement per the dependency rule): `scikit-build-core`,
`cibuildwheel`, `auditwheel` (Linux), `swig>=4.0`, Conan 2.x, CMake ≥ 3.28, Ninja.
Runtime dependency: **none** (extension statically links `fixpp_capi` +
`-static-libstdc++/-libgcc`, already in `bindings/python/CMakeLists.txt`).
**Storage**: bundled FIX dictionary XMLs as package data (FIX42/44/50SP2/FIXT11).
**Testing**: `pytest` functional subset against the installed wheel in a clean
venv; existing CTest sanitizer matrix unchanged.
**Target Platform**: Linux x86_64 `manylinux_2_28` (mandatory); Windows x86_64
best-effort/deferred (`[2m §10 Q3]`).
**Project Type**: single project — release-engineering layer over an existing
library + binding.
**Performance Goals**: N/A (packaging). Constraint: CI wheel job wall-clock kept
reasonable (Conan cache; 4-version matrix).
**Constraints**: self-contained wheel (no external `.so`); manylinux_2_28 tag;
C-ABI-only consumption (`[arch §8]`); no C-ABI/binding-behaviour change (FR-012).
**Scale/Scope**: 4 wheels (cp310–cp313) × Linux; +1 best-effort Windows lane.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

Article-by-article (only triggered articles listed):

- **Article IV §3 (Distribution)** — PASS. Produces the mandatory Linux x86_64
  CPython wheel via SWIG-over-C-ABI; Windows best-effort/deferred.
- **Article IV §5 (built-not-published)** — PASS. Wheels attach to GitHub release
  assets; **no PyPI** upload. Plan adds no publish-to-index step.
- **Article VIII §5 (Normative References)** — PASS. Spec carries the section.
- **Article VIII §2 (canonical Spec ref)** — PASS. Catalogue row PY-005 already
  cites `[2m §1.1, §11]`; no vague refs introduced.
- **Article IX §6 (Two-tier CI / Tier-1 every PR)** — PASS. Wheel build +
  install-test added as a Tier-1 required job; existing pytest/sanitizer jobs
  unchanged.
- **Article X (ABI Policy)** — PASS / not triggered. No C-ABI surface change;
  `abidiff` golden unaffected (FR-012). The `0→1` freeze stays held.
- **Article VII §3 (AGPL boundary) / [arch §8]** — PASS. Packaging consumes only
  `<fix/c_api.h>`; `tools/check_layers.py` already scans `bindings/python/`. The
  wheel statically links `fixpp_capi` only — no engine C++ headers.
- **Article II §3 (Platforms)** — PASS. Linux primary; Windows on-demand Tier-2.

**No violations.** Complexity Tracking table omitted (nothing to justify).

## Project Structure

### Documentation (this feature)

```text
specs/056-python-wheel-packaging/
├── plan.md              # This file
├── research.md          # Phase 0 — toolchain/manylinux/abi3/layout decisions
├── data-model.md        # Phase 1 — wheel artifact + data package + locator
├── quickstart.md        # Phase 1 — build/install/test walkthrough
├── contracts/           # Phase 1 — packaging + CI + locator-API contracts
└── tasks.md             # Phase 2 (/speckit-tasks — NOT created here)
```

### Source Code (repository root = library submodule)

```text
bindings/python/
├── pyproject.toml            # NEW — scikit-build-core backend + project metadata
├── CMakeLists.txt            # EXISTING — reused as the wheel's CMake build
├── fixpp.i                   # EXISTING (frozen) — SWIG interface
├── fixpp_oo.py               # EXISTING (frozen) — OO API, shipped as-is
├── _fixpp_data/              # NEW — tiny data package holding the FIX XMLs
│   ├── __init__.py           # NEW — marks the data package importable
│   ├── FIX42.xml             # NEW (copied/symlinked from dictionaries/ at build)
│   ├── FIX44.xml
│   ├── FIX50SP2.xml
│   └── FIXT11.xml
├── fixpp_dict_data.py        # NEW — pure-Python locator helper (FR-004a)
└── tests/                    # EXISTING — functional subset selected for the wheel

.github/workflows/
└── tier1.yml                 # EXISTING — add a `python-wheel` Tier-1 job

CMakeLists.txt / bindings/python/CMakeLists.txt  # EXISTING — flat-layout install fix
```

**Structure Decision**: Single project. The wheel is built from
`bindings/python/` with `scikit-build-core` invoking the existing CMake target.
The two public modules (`fixpp`, `fixpp_oo`) ship as **top-level modules** (flat),
per the clarification; the dictionaries live in a separate small importable data
package `_fixpp_data/` so package-data + `importlib.resources` work without
restructuring `fixpp`/`fixpp_oo`. The locator helper `fixpp_dict_data.py` resolves
a dict by name from that package. See data-model.md for the exact layout.

## Complexity Tracking

> No Constitution Check violations — section intentionally empty.
