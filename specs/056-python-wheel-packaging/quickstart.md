# Quickstart: Python Wheel Packaging (PY-005)

How the wheel is built, installed, and verified — the manual mirror of the CI
gate (contracts/ci-wheel-gate.md). All commands run from the library submodule
root (`research/G19-fix-fpml-iso20022/library`).

## 1. Build the wheels (local, via cibuildwheel)

```bash
pipx install cibuildwheel        # + Docker for the manylinux container
# Linux cp310–cp313 manylinux_2_28 wheels:
CIBW_BUILD="cp310-* cp311-* cp312-* cp313-*" \
CIBW_ARCHS_LINUX="x86_64" \
cibuildwheel --platform linux bindings/python
# -> wheelhouse/fixpp-<ver>-cp3XX-cp3XX-manylinux_2_28_x86_64.whl  (×4)
```

`cibuildwheel` runs inside `manylinux_2_28_x86_64`, enables the gcc-toolset (≥13),
generates a matching Conan profile, `conan install --build=missing`, then
`scikit-build-core` drives the existing `FIXPP_BUILD_PYTHON` CMake target and
`auditwheel repair` normalises the tag. (Build-graph + toolchain detail:
research D-2/D-7.)

## 2. Install into a clean environment (the FR-002/SC-001 check)

```bash
python3.12 -m venv /tmp/wheeltest && . /tmp/wheeltest/bin/activate
pip install wheelhouse/fixpp-*-cp312-cp312-manylinux_2_28_x86_64.whl
python -c "import fixpp, fixpp_oo, fixpp_dict_data; print('ok')"
```

No compiler, SWIG, Conan, or system `libfixpp` is present in the venv — the
extension is self-contained.

## 3. End-to-end round-trip from the bundled dictionary (SC-002)

```python
import fixpp, fixpp_dict_data
# Resolve the bundled FIX44 dictionary — no user-supplied file:
with fixpp_dict_data.dictionary_path("FIX44") as p:
    d = fixpp.dict_load_from_xml(p)
    # ... stand up two engines, open a session, send, read back the field ...
    # (the existing test_roundtrip.py flow, dict resolved via the locator)
```

`fixpp_oo` gives the OO API (`Engine`/`Session`/`Message`/...) over the same
substrate.

## 4. Run the functional subset against the installed wheel (SC-003)

```bash
# From a dir WITHOUT a repo-relative dictionaries/ shadowing the locator:
pytest --pyargs -q \
  bindings/python/tests \
  --deselect bindings/python/tests/test_gil_release_canary.py
```

`test_gil_release_canary.py` is excluded — it needs the deliberate-hang
`FIXPP_PY_GIL_RELEASE_CANARY` build and is gated by the existing CTest sanitizer
matrix, not by the installed wheel (research D-8, FR-007). The dict-path helpers
fall back to `fixpp_dict_data` when no repo-relative `dictionaries/` exists, so
the suite runs unchanged against the installed package.

## 5. Verify tag + self-containment

```bash
auditwheel show wheelhouse/fixpp-*-cp312-*.whl   # -> manylinux_2_28_x86_64, no external libs
unzip -l wheelhouse/fixpp-*-cp312-*.whl | grep -E '_fixpp|fixpp_oo|_fixpp_data/FIX'
```

## 6. CI / release

The above is the `python-wheel` Tier-1 job on every PR (merge gate). On a release
event the four wheels are attached as GitHub release assets — **no PyPI**
(`[const §IV.5]`). See contracts/ci-wheel-gate.md.

## Out of scope here

Windows wheel (deferred, `[2m §10 Q3]`); macOS/aarch64; abi3; wheel signing — see
spec Out of Scope.
