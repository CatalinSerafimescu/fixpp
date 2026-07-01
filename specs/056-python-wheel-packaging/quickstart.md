# Quickstart: Python Wheel Packaging (PY-005)

How the wheel is built, installed, and verified — the manual mirror of the CI
gate (contracts/ci-wheel-gate.md). All commands run from the library submodule
root (`research/G19-fix-fpml-iso20022/library`).

## 1. Build the wheels (local, via cibuildwheel)

```bash
pipx install cibuildwheel        # + Docker for the manylinux container
# Build via the wrapper, which runs cibuildwheel from a PRISTINE git worktree:
git add -A && git commit ...     # commit first — the worktree builds HEAD
bindings/python/build-wheel.sh
# -> wheelhouse/fixpp-<ver>-cp310-abi3-manylinux_2_28_x86_64.whl  (×1)
```

**Why the wrapper (not raw `cibuildwheel` from the root).** cibuildwheel copies
the *entire current working directory* into the container with a blind
`tar -c .` that honours no .gitignore/.dockerignore and cannot be told to
exclude anything; it also requires `package_dir` to live under cwd. Because
pyproject's `cmake.source-dir = "../.."` forces cwd to be the submodule root,
`cibuildwheel --platform linux bindings/python` from the root sweeps the
multi-GB `build/` + `.codegraph/` trees into the container (once ~100G in
seconds). `build-wheel.sh` builds from a detached `git worktree` of committed
HEAD, where those gitignored trees are physically absent (~tens of MB tarred),
then copies the wheel back to `wheelhouse/`. This is a LOCAL-dev hazard only —
CI builds on a fresh checkout where `build/` never exists. The build identifier
is overridable via the same `CIBW_BUILD` / `CIBW_ARCHS_LINUX` /
`CIBW_MANYLINUX_X86_64_IMAGE` env vars:

```bash
CIBW_BUILD="cp310-manylinux_x86_64" \
CIBW_ARCHS_LINUX="x86_64" \
CIBW_MANYLINUX_X86_64_IMAGE="manylinux_2_28" \
bindings/python/build-wheel.sh
```

A cibuildwheel build identifier is `{python_tag}-{platform_tag}`, where the linux
platform tag is architecture-only (`manylinux_x86_64`) — there is **no** `abi3`
or `2_28` component in the selector (the original `cp310-abi3-*` selected zero
builds). `cibuildwheel` runs inside the pinned `manylinux_2_28_x86_64` image
(`CIBW_MANYLINUX_X86_64_IMAGE`), enables the gcc-toolset (≥13), generates a matching
Conan profile, runs
`conan install --build=missing -o fixpp/*:with_otel=False -o openssl/*:shared=False`
(self-contained `.so`, no external libs), then `scikit-build-core` drives the
existing `FIXPP_BUILD_PYTHON` CMake target with `-DPy_LIMITED_API=0x030A0000`
(compile-only) under scikit-build-core's `wheel.py-api = "cp310"` abi3 tag
(SWIG ≥ 4.2), and `auditwheel repair` normalises the
tag. **One build** produces the single `cp310-abi3` wheel; the **four** CPython
3.10/3.11/3.12/3.13 install-tests of §2 run against that one artifact.
(Build-graph + toolchain + abi3 detail: research D-2/D-3/D-7.)

## 2. Install into a clean environment (the FR-002/SC-001 check)

```bash
# The SAME single abi3 wheel installs on each of 3.10/3.11/3.12/3.13:
python3.12 -m venv /tmp/wheeltest && . /tmp/wheeltest/bin/activate
pip install wheelhouse/fixpp-*-cp310-abi3-manylinux_2_28_x86_64.whl
# Public-surface smoke (the locator is reached THROUGH import fixpp):
python -c "import fixpp; assert fixpp.dictionary_path; print('ok')"
```

No compiler, SWIG, Conan, or system `libfixpp` is present in the venv — the
extension is self-contained. (`fixpp_oo` / `fixpp_dict_data` are implementation
modules surfaced through `import fixpp`; direct imports of them belong to the
internal layout tests — LAY-4 — not the user-facing smoke.)

## 3. End-to-end round-trip from the bundled dictionary (SC-002)

```python
import fixpp
# Resolve the bundled FIX44 dictionary — no user-supplied file:
with fixpp.dictionary_path("FIX44") as p:
    d = fixpp.dict_load_from_xml(p)
    # ... stand up two engines, open a session, send, read back the field ...
    # (the existing test_roundtrip.py flow, dict resolved via the locator)
```

`import fixpp` is the whole public surface: flat functions, the `Error`
hierarchy, the OO classes (`fixpp.Engine`/`Session`/...), and the dictionary
locator — all re-exported via the proxy's `%pythoncode` glue.

## 4. Run the functional subset against the installed wheel (SC-003)

```bash
# Out-of-repo harness — prove the tests run against the INSTALLED wheel, not the tree:
WORK=$(mktemp -d)                      # temp dir outside the repo
cp -r bindings/python/tests/wheel "$WORK/wheel_tests"   # dedicated installed-only suite
cd "$WORK"
env -u PYTHONPATH \
  python -c "import fixpp, os, sys; \
    assert os.path.realpath(fixpp.__file__).startswith(sys.prefix), fixpp.__file__"  # under venv site-packages
env -u PYTHONPATH pytest -q "$WORK/wheel_tests"
```

The dedicated `tests/wheel/` suite imports only installed modules and resolves
dictionaries through `fixpp.dictionary_path(...)` — never a repo-relative
`dictionaries/` directory — so it is valid with the repo absent (research D-8).
`PYTHONPATH` is scrubbed and `fixpp.__file__` is asserted under the venv
site-packages so no source-tree state leaks. `test_gil_release_canary.py` is not in
the wheel suite — it needs the deliberate-hang `FIXPP_PY_GIL_RELEASE_CANARY` build
and stays gated by the existing CTest sanitizer matrix (FR-007).

## 5. Verify tag + self-containment

```bash
auditwheel show wheelhouse/fixpp-*-cp310-abi3-*.whl  # -> manylinux_2_28_x86_64, abi3, external list EMPTY
unzip -l wheelhouse/fixpp-*-cp310-abi3-*.whl | grep -E '_fixpp|fixpp_oo|_fixpp_data/FIX'
```

## 6. CI / release

The above is the `python-wheel` Tier-1 job on every PR (merge gate). On a release
event the single abi3 wheel is attached as a GitHub release asset — **no PyPI**
(`[const §IV.5]`). See contracts/ci-wheel-gate.md.

## Out of scope here

Windows wheel (deferred, `[2m §2 non-goal #4]`); macOS/aarch64 (`[2m §10 Q9]`);
per-version fallback wheels (only if the abi3 import proves flaky); wheel signing
(`[2m §10 Q2]`) — see spec Out of Scope.
