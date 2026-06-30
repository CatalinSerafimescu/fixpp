# Contract: wheel packaging (pyproject + layout + tag)

Defines what `pip wheel bindings/python/` (via `scikit-build-core` under
`cibuildwheel`) must produce. Consumed by FR-001..005, FR-010, SC-001/004.

## Build configuration (`bindings/python/pyproject.toml`)

| ID | Rule |
|---|---|
| PKG-1 | `[build-system] requires = ["scikit-build-core>=…", "swig>=4.0"]`, `build-backend = "scikit_build_core.build"`. Lower-bound pins; exact versions verified against PyPI at implement (per the dependency rule). |
| PKG-2 | `[project] name = "fixpp"`; `version` is **dynamic**, sourced from the CMake `project(VERSION)` via `tool.scikit-build.metadata.version` (research D-6). No second version source. |
| PKG-3 | `[project] requires-python = ">=3.10,<3.14"` (the `[2m §1.1]` matrix). |
| PKG-4 | scikit-build-core drives the existing `bindings/python/CMakeLists.txt` with `-DFIXPP_BUILD_PYTHON=ON` and the in-container Conan toolchain file (research D-2/D-7). |
| PKG-5 | No runtime `dependencies` (the extension is self-contained; FR-002). |

## Wheel contents & layout (FR-002/003/004)

| ID | Rule |
|---|---|
| LAY-1 | Top-level (site-packages root), **flat**: `_fixpp*.so`, `fixpp.py`, `fixpp_oo.py`, `fixpp_dict_data.py`. No `fixpp/` namespace directory (fixes the latent `install(... /fixpp)` layout — research D-4). |
| LAY-2 | `_fixpp_data/` package present with `__init__.py` + the four XMLs (data-model E-3), staged from `dictionaries/` at build. |
| LAY-3 | The wheel carries **no** external shared-lib dependency beyond libc/libpython: `_fixpp*.so` statically links `fixpp_capi` + `-static-libstdc++/-libgcc` (already in CMakeLists). `auditwheel show` lists no non-allowed external `.so`. |
| LAY-4 | `import fixpp`, `import fixpp_oo`, `import fixpp_dict_data` all succeed from a clean install (SC-001). |

## Platform / interpreter tag (FR-005/010, SC-004)

| ID | Rule |
|---|---|
| TAG-1 | After `auditwheel repair`, platform tag is `manylinux_2_28_x86_64` (not raw `linux_x86_64`). |
| TAG-2 | Interpreter/ABI tag is per-version `cp310-cp310` … `cp313-cp313` (research D-3). |
| TAG-3 | One wheel per CPython 3.10/3.11/3.12/3.13; the cp310 wheel is the named mandatory deliverable (`[arch §7.1]`). |

## Witnesses

- LAY-1/4 + TAG-1/2: `cibuildwheel` produces the four files; `unzip -l` /
  `auditwheel show` assert layout + tag; `pip install` + `import` per version.
- LAY-3: `auditwheel show` external-lib list is empty/allowed; a clean container
  with no libfixpp present still imports (SC-001).
- LAY-2: install + `importlib.resources.files("_fixpp_data")` lists 4 XMLs.
