# Contract: `python-wheel` Tier-1 CI gate + release attach

Defines the new `.github/workflows/tier1.yml` job and the release-attach step.
Consumed by FR-006..009, SC-003/005/006.

## Tier-1 job `python-wheel`

| ID | Rule |
|---|---|
| CI-1 | Runs on **every PR** as a required-to-merge Tier-1 job (`[const §IX.6]`); gated by the same `gate-precheck` `proceed` guard as the other Tier-1 jobs. |
| CI-2 | Builds the Linux wheels for cp310–cp313 via `cibuildwheel` inside `manylinux_2_28_x86_64`, with the gcc-toolset ≥13 + in-container Conan profile (research D-2/D-7). |
| CI-3 | Caches `~/.conan2/p` keyed on `conanfile.py` + profile (mirrors the existing `python-bindings` job) to keep the ×4 matrix tractable. |
| CI-4 | For each produced wheel: create a clean venv on the matching CPython, `pip install <wheel>`, then run the **functional subset** (research D-8) **against the installed package** — `pytest bindings/python/tests/ --deselect …test_gil_release_canary.py` (or marker-based), from a working dir where no repo-relative `dictionaries/` shadows the locator. |
| CI-5 | The job is **red** if any wheel fails to build, fails to install, or the subset fails (FR-009, SC-006). No `continue-on-error`. |
| CI-6 | Has a `timeout-minutes` backstop (as the other Tier-1 jobs do) so a hung build cannot wedge the runner. |
| CI-7 | Per-matrix-leg `concurrency` group (mirrors the repo convention) so a skip-run cannot cancel a real run. |
| CI-8 | Does **not** remove, weaken, or replace the existing `python-bindings` sanitizer matrix or the local-only GIL canary (FR-007). Additive only. |

## Release attach (FR-008)

| ID | Rule |
|---|---|
| REL-1 | On a GitHub **release** event, the cp310–cp313 Linux wheels are uploaded as release **assets**. |
| REL-2 | **No** PyPI / index upload step exists anywhere in the workflow (`[const §IV.5]`, SC-005). |
| REL-3 | The mandatory `fixpp-<ver>-cp310-cp310-manylinux_2_28_x86_64.whl` is among the attached assets. |

## Windows (deferred — research D-10)

| ID | Rule |
|---|---|
| WIN-1 | Any Windows wheel lane is **separate** and on-demand (`windows` label / nightly), never a Linux merge-gate dependency (FR-011). Its absence/failure does not affect CI-1..CI-5. |

## Witnesses

- CI-4/CI-5: green path proves SC-001/002/003 against the installed artifact; a
  deliberately broken wheel (drop `_fixpp_data`) flips the gate red (SC-006).
- REL-1/3: a release dry-run lists the four `.whl` assets including the cp310 one.
- REL-2: a grep of the workflow shows no `twine`/`pypa/gh-action-pypi-publish`/
  index-upload step.
