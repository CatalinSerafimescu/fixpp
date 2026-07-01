# Phase 1 Data Model: Python Wheel Packaging (PY-005)

This feature has no runtime domain entities (it ships no new code paths in the
engine). The "data model" here is the **artifact + package structure** the build
produces and the small locator surface it adds. Entities map to FRs/SCs.

## E-1 — Binding wheel artifact

The distributable unit. **One** stable-ABI wheel, Linux x86_64, covering CPython
3.10–3.13+.

| Field | Value / rule | Source |
|---|---|---|
| filename | `fixpp-<ver>-cp310-abi3-manylinux_2_28_x86_64.whl` (exactly ONE wheel) | FR-001, FR-005, `[arch §7.1]` as proposed-amended at Gate A |
| `<ver>` | from CMake `project(VERSION)` (0.0.1 today) via scikit-build-core | research D-6 |
| platform tag | `manylinux_2_28_x86_64` (post `auditwheel repair`) | FR-005, SC-004 |
| python/abi tag | `cp310-abi3` (stable ABI; `cp310` floor, NOT a per-version `cp3XX-cp3XX` tag; per-version is the fallback only) | FR-010, research D-3 |
| contents | `_fixpp*.so`, `fixpp.py`, `fixpp_oo.py` (flat) + `_fixpp_data/` + `fixpp_dict_data.py` | FR-003, FR-004 |
| self-containment | **no external `.so`** beyond libc/libpython: static `fixpp_capi` + `-static-libstdc++/-libgcc`, `with_otel=False`, static OpenSSL (`-o openssl/*:shared=False`); `auditwheel show` external list **empty** | FR-002, SC-001, research D-7 |
| distribution | attached to GitHub release; not uploaded to PyPI | FR-008, `[const §IV.5]` |

**Validation**: `auditwheel show` reports `manylinux_2_28` with an empty external
list; the single wheel `pip install`s into a clean venv on **each** of 3.10–3.13
(same `.whl` file every time) and `import fixpp` / `import fixpp_oo` succeed
(SC-001) — the cross-version import is the abi3 feasibility witness (research D-3).

## E-2 — Installed module surface (flat, top-level)

| Module | Role | Lifecycle |
|---|---|---|
| `_fixpp` (`_fixpp*.so`) | SWIG low-level extension (statically linked) | **rebuilt** for the bounded limited-API adaptation (`-DPy_LIMITED_API` + the `fixpp_py_is_main_interpreter` helper rework); behaviour-preserved per FR-012/SC-007 |
| `fixpp` (`fixpp.py`) | **the public surface** — SWIG proxy whose `%pythoncode` glue re-exports the flat functions, `FixppError`/`Error`, the OO classes (from `fixpp_oo`), and the new locator | generated; glue extended additively (one re-export line) |
| `fixpp_oo` (`fixpp_oo.py`) | OO API implementation module (Engine/Session/Message/Application/Dictionary), imported by `fixpp.py` | frozen content |
| `fixpp_dict_data` (`fixpp_dict_data.py`) | **NEW** locator implementation module (mirrors `fixpp_oo`); surfaced as `fixpp.dictionary_path`/`fixpp.dictionary_bytes`/`fixpp.BUNDLED_DICTIONARIES` | added by this feature |
| `_fixpp_data` (package) | **NEW** bundled FIX XML data package | added by this feature |

**Rule (D-4)**: all `*.py`/`*.so` modules are **top-level** (site-packages root);
no `fixpp/` namespace directory. Fixes the latent install layout. The **public
import name is `fixpp`** — `fixpp_oo` and `fixpp_dict_data` are implementation
modules surfaced through it, not new user-facing import names. `fixpp_oo` content
is **unchanged** (hand-written pure Python, untouched); `_fixpp` is **rebuilt**
only for the bounded limited-API adaptation (the `fixpp_py_is_main_interpreter`
helper + `-DPy_LIMITED_API`), behaviour-preserved; `fixpp.py`'s `%pythoncode` glue
gains only an additive locator re-export, mirroring the 055 OO re-export precedent
(FR-012: no C-ABI change, no existing-binding-*behaviour* change).

## E-3 — Bundled dictionary data package `_fixpp_data/`

| File | Rule |
|---|---|
| `_fixpp_data/__init__.py` | empty marker; makes the dir an importable package for `importlib.resources` |
| `_fixpp_data/FIX42.xml` | copied from `dictionaries/FIX42.xml` at build (one source of truth) |
| `_fixpp_data/FIX44.xml` | copied from `dictionaries/FIX44.xml` |
| `_fixpp_data/FIX50SP2.xml` | copied from `dictionaries/FIX50SP2.xml` |
| `_fixpp_data/FIXT11.xml` | copied from `dictionaries/FIXT11.xml` |

**Validation (FR-004)**: after a clean install, the four resources are present in
the package and resolvable without any user-supplied file (SC-002).

**Build wiring**: `scikit-build-core` packages files that CMake **installs**, not
loose tree copies — so the four XMLs (and `_fixpp_data/__init__.py`) MUST have an
explicit `install(FILES … DESTINATION <wheel-root>/_fixpp_data)` rule (staged from
`dictionaries/` at configure/build so they are never hand-duplicated), or be
force-included via `tool.scikit-build.wheel.packages` / `sdist.include`. The
`unzip -l … | grep _fixpp_data/FIX` witness (wheel-packaging LAY-2) guards that
they actually land in the wheel.

## E-4 — Dictionary locator API — FR-004a

Pure-Python; no native code; no C-ABI change. Implemented in `fixpp_dict_data.py`
and **re-exported through `fixpp`** so the public names are `fixpp.dictionary_path`
/ `fixpp.dictionary_bytes` / `fixpp.BUNDLED_DICTIONARIES` (consistent with the
existing "everything via `import fixpp`" surface; no new top-level public name).

| Public symbol | Signature | Behaviour |
|---|---|---|
| `fixpp.BUNDLED_DICTIONARIES` | `frozenset({"FIX42","FIX44","FIX50SP2","FIXT11"})` | the shipped set |
| `fixpp.dictionary_path(name)` | `(str) -> ContextManager[str]` | yields a real filesystem path to the named XML via `importlib.resources.as_file`; for feeding `dict_load_from_xml(path)` |
| `fixpp.dictionary_bytes(name)` | `(str) -> bytes` | the XML contents, for `dict_load_from_xml_bytes`-style use |

**Rules**:
- `name` not in the bundled set → **`KeyError`** (single decided type — Gate A)
  whose message lists the **sorted** valid set (testable, unambiguous). No silent
  default, no empty return. LOC-4 and the LOC-4 witness agree on `KeyError`.
- Resolution uses `importlib.resources.files("_fixpp_data")` — works from an
  installed wheel and from the build tree.
- The re-export is one additive line in `fixpp.i`'s existing `%pythoncode` glue
  block (the 055 OO re-export precedent); no change to existing wrappers.

**Validation**: `dictionary_path("FIX44")` yields a path that
`fixpp.dict_load_from_xml(...)` accepts and a FIX 4.4 round-trip succeeds against
it (SC-002); an unknown name raises (negative test).

## E-5 — CI wheel gate (`python-wheel` Tier-1 job)

| Field | Rule |
|---|---|
| trigger | every PR (Tier-1, required to merge), `[const §IX.6]` |
| build | `cibuildwheel` → **one** `cp310-abi3` manylinux_2_28 wheel (research D-2/D-3/D-7) |
| install-test | the single wheel installed into a clean venv on **each** of CPython 3.10/3.11/3.12/3.13; D-8 functional subset run against the installed package on every interpreter |
| failure semantics | build/install/test failure → job red (FR-009, SC-006) |
| release | on release event, the wheel attached as an asset; no PyPI (FR-008) |

**Validation**: the named negative witness `tests/wheel/test_broken_wheel_gate.sh`
(a deliberately broken wheel — `_fixpp_data/FIX44.xml` removed — installed into a
throwaway venv, locator/round-trip asserted non-zero, non-publishing) turns the
gate red (SC-006); the green path proves SC-001/002/003/004.

## E-6 — Installed-wheel test suite + dict-path fallback — D-8

The installed-wheel run uses a **dedicated `bindings/python/tests/wheel/` suite**
that imports only installed modules and resolves every dictionary through
`fixpp.dictionary_path(...)` (never a repo-relative path) — the one harness model
shared by CI-4, quickstart §4, and LOC-5. For in-tree tests reused by that suite
whose dict helpers use a **repo-relative path** (`<repo>/dictionaries/FIX44.xml`),
the helper gains a fallback: **repo-relative path if it exists, else
`fixpp_dict_data.dictionary_path(...)`**. This MUST cover **every** selected test's
helper — not only `oo_test_support.py` / `test_roundtrip.py`, but also
`test_lifetime.py`, `_gil_staging.py`, and the sub-interpreter test (each carries
its own repo-relative helper) — or the wheel subset is scoped to locator-using
tests only. Test-only; no production change (FR-012).

---

## Traceability

| Entity | FRs | SCs |
|---|---|---|
| E-1 wheel artifact | FR-001, FR-002, FR-005, FR-008, FR-010 | SC-001, SC-004, SC-005, SC-007 |
| E-2 module surface | FR-003 | SC-001 |
| E-3 dict data package | FR-004 | SC-002 |
| E-4 locator API | FR-004a | SC-002 |
| E-5 CI gate | FR-006, FR-007, FR-009 | SC-003, SC-006 |
| E-6 test fallback | FR-006 | SC-003 |
