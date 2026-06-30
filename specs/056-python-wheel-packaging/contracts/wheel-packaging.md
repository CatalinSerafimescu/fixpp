# Contract: wheel packaging (pyproject + layout + tag)

Defines what `pip wheel bindings/python/` (via `scikit-build-core` under
`cibuildwheel`) must produce. Consumed by FR-001..005, FR-010, SC-001/004.

## Build configuration (`bindings/python/pyproject.toml`)

| ID | Rule |
|---|---|
| PKG-1 | `[build-system] requires = ["scikit-build-core>=…", "swig>=4.2"]`, `build-backend = "scikit_build_core.build"`. **`swig>=4.2` is PINNED** — a 4.0 runner silently regresses the limited-API (abi3) mode (research D-3). Lower-bound pins; exact versions verified against PyPI at implement (per the dependency rule). |
| PKG-2 | `[project] name = "fixpp"`; `version` is **dynamic**, sourced from the CMake `project(VERSION)` via `tool.scikit-build.metadata.version` (research D-6). No second version source. |
| PKG-3 | `[project] requires-python = ">=3.10"` (the abi3 `cp310` floor; the stable-ABI wheel covers 3.10–3.13 and future 3.14+ — no upper cap, since abi3 forward-compatibility is the point; 3.14+ is covered-by-abi3 but untested in v1). |
| PKG-4 | scikit-build-core drives the existing `bindings/python/CMakeLists.txt` with `-DFIXPP_BUILD_PYTHON=ON` and the in-container Conan toolchain file (research D-2/D-7). |
| PKG-5 | No runtime `dependencies` (the extension is self-contained; FR-002). |
| PKG-6 | **abi3 build**: the wheel tag is driven by scikit-build-core's `[tool.scikit-build] wheel.py-api = "cp310"` (→ `cp310-abi3`) — the authoritative tag mechanism. Separately, CMake compiles the SWIG wrapper against the limited C API with `-DPy_LIMITED_API=0x030A0000` + the CMake SABI wiring (`SKBUILD_SABI_COMPONENT` / `Development.SABIModule` / `USE_SABI 3.10`); this is **compile-only — it does NOT set the wheel tag**. scikit-build-core does not auto-detect the limited ABI and does not consume the setuptools `Extension(py_limited_api=True)` kwarg (inert under this backend). Requires the bounded limited-API rework of `fixpp_py_is_main_interpreter` in `fixpp.i` (FR-012, preserves sub-interpreter rejection — research D-3). No C-ABI change. |

## Wheel contents & layout (FR-002/003/004)

| ID | Rule |
|---|---|
| LAY-1 | Top-level (site-packages root), **flat**: `_fixpp*.so`, `fixpp.py`, `fixpp_oo.py`, `fixpp_dict_data.py`. No `fixpp/` namespace directory (fixes the latent `install(... /fixpp)` layout — research D-4). The public import name is `fixpp`; `fixpp_oo`/`fixpp_dict_data` are implementation modules re-exported through it. |
| LAY-2 | `_fixpp_data/` package present with `__init__.py` + the four XMLs (data-model E-3), staged from `dictionaries/` at build. |
| LAY-3 | The wheel carries **no** external shared-lib dependency beyond libc/libpython: `_fixpp*.so` statically links `fixpp_capi` + `-static-libstdc++/-libgcc` (already in CMakeLists), and the build sets `-o fixpp/*:with_otel=False` (drops the OTel/gRPC/protobuf subtree) + static OpenSSL (`-o openssl/*:shared=False`) (research D-7). `auditwheel show`'s external-library list is **empty** (the static-everything self-containment witness — not a vendored-deps wheel). |
| LAY-4 | `import fixpp`, `import fixpp_oo`, `import fixpp_dict_data` all succeed from a clean install (SC-001). |

## Platform / interpreter tag (FR-005/010, SC-004)

| ID | Rule |
|---|---|
| TAG-1 | After `auditwheel repair`, platform tag is `manylinux_2_28_x86_64` (not raw `linux_x86_64`). |
| TAG-2 | Interpreter/ABI tag is `cp310-abi3` (stable ABI, `cp310` floor; **not** a per-version `cp3XX-cp3XX` tag) (research D-3, PKG-6). |
| TAG-3 | **Exactly ONE** wheel `fixpp-<ver>-cp310-abi3-manylinux_2_28_x86_64.whl` — the named mandatory deliverable (`[arch §7.1]` as proposed-amended at Gate A). It installs on each of CPython 3.10/3.11/3.12/3.13 (install-test matrix — ci-wheel-gate CI-4); per-version wheels are the fallback only. |

## Witnesses

- LAY-1/4 + TAG-1/2/3: `cibuildwheel` produces the single `cp310-abi3` wheel;
  `unzip -l` / `auditwheel show` assert flat layout + `cp310-abi3` +
  `manylinux_2_28` tag; `pip install` + `import fixpp` on each of 3.10–3.13.
- LAY-3: `auditwheel show` external-lib list is **empty**; a clean container with
  no libfixpp present still imports (SC-001).
- LAY-2: install + `importlib.resources.files("_fixpp_data")` lists 4 XMLs.
