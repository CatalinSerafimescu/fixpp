# Contract: `python-wheel` Tier-1 CI gate + release attach

Defines the new `.github/workflows/tier1.yml` job and the release-attach step.
Consumed by FR-006..009, SC-003/005/006.

## Tier-1 job `python-wheel`

| ID | Rule |
|---|---|
| CI-1 | Runs on **every PR** as a required-to-merge Tier-1 job (`[const §IX.6]`); gated by the same `gate-precheck` `proceed` guard as the other Tier-1 jobs. |
| CI-2 | Builds the **single** Linux `cp310-abi3` wheel via `cibuildwheel` inside `manylinux_2_28_x86_64`, with the gcc-toolset ≥13 + in-container Conan profile + `-DPy_LIMITED_API=0x030A0000` (compile-only) + the `wheel.py-api = "cp310"` abi3 tag + SWIG ≥ 4.2 + `with_otel=False` + static OpenSSL (research D-2/D-3/D-7). |
| CI-3 | Caches `~/.conan2/p` keyed on `conanfile.py` + profile (mirrors the existing `python-bindings` job) to keep the build tractable. |
| CI-4 | Install-test matrix over CPython 3.10/3.11/3.12/3.13 against **the one wheel**: for each interpreter create a clean venv, `pip install <the-abi3-wheel>`, then run the **functional subset** (research D-8) **against the installed package** via the dedicated `bindings/python/tests/wheel/` suite (imports only installed modules; resolves dicts through `fixpp.dictionary_path`). The harness runs out-of-repo with `PYTHONPATH` scrubbed and asserts `fixpp.__file__` is under the venv site-packages (quickstart §4) — so no source-tree `dictionaries/` can shadow the locator. The cross-version import is the abi3 feasibility witness. |
| CI-5 | The job is **red** if the wheel fails to build, fails to install on any targeted interpreter, or the subset fails (FR-009, SC-006). No `continue-on-error`. |
| CI-6 | Has a `timeout-minutes` backstop (as the other Tier-1 jobs do) so a hung build cannot wedge the runner. |
| CI-7 | Per-matrix-leg `concurrency` group (mirrors the repo convention) so a skip-run cannot cancel a real run. |
| CI-8 | Does **not** remove, weaken, or replace the existing `python-bindings` sanitizer matrix or the local-only GIL canary (FR-007). Additive only. |

## Release attach (FR-008)

| ID | Rule |
|---|---|
| REL-1 | On a GitHub **release** event, the single `cp310-abi3` Linux wheel is uploaded as a release **asset**. |
| REL-2 | **No** PyPI / index upload step exists anywhere in the workflow (`[const §IV.5]`, SC-005). |
| REL-3 | The mandatory `fixpp-<ver>-cp310-abi3-manylinux_2_28_x86_64.whl` is the attached asset. |

## Windows (deferred — research D-10)

| ID | Rule |
|---|---|
| WIN-1 | Any Windows wheel lane is **separate** and on-demand (`windows` label / nightly), never a Linux merge-gate dependency (FR-011). Its absence/failure does not affect CI-1..CI-5. |

## No-behaviour-change verification (FR-012 / SC-007)

| ID | Rule |
|---|---|
| NBC-1 | A CI step **fails on any diff to `include/fix/c_api*.h`** (the C-ABI surface is byte-frozen — the `0→1` GA freeze) and runs the existing ABI/header-occupancy check. |
| NBC-2 | The frozen PY-001..004 in-tree behavioural suite runs **unchanged** under the existing `python-bindings` CTest matrix and stays green — **including the sub-interpreter witness** after the limited-API rework of `fixpp_py_is_main_interpreter` (the one behaviour the abi3 adaptation could regress). |
| NBC-3 | An `import fixpp` **public-surface snapshot** asserts every existing name/class (flat functions, `FixppError`/`Error`, the OO classes, the new locator) still resolves — guards the additive `%pythoncode` re-export from dropping a symbol. |

## Witnesses

- CI-4/CI-5: green path proves SC-001/002/003 against the installed artifact on
  each of 3.10–3.13.
- SC-006 (named negative witness): **`tests/wheel/test_broken_wheel_gate.sh`** —
  copy a built wheel, remove `_fixpp_data/FIX44.xml` from the copy, install the
  mutated copy into a throwaway venv, run the locator/round-trip, **assert
  non-zero**. Non-publishing (it never uploads or pollutes the real artifact);
  wired as a CI verification step that flips the gate red.
- NBC-1/2/3: the c_api header-diff guard fails on any `c_api*.h` change; the
  frozen behavioural suite + sub-interpreter witness stay green; the import-surface
  snapshot resolves every existing name.
- REL-1/3: a release dry-run lists the single `cp310-abi3` `.whl` asset.
- REL-2: a grep of the workflow shows no `twine`/`pypa/gh-action-pypi-publish`/
  index-upload step.
