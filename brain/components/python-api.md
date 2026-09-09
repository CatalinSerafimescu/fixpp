---
type: Component Decision Map
title: Python bindings — SWIG over the C ABI, an OO layer that cannot outlive its handle, and a deliberately selective surface
description: The narrow SWIG surface is a false-green guard, not laziness. A blanket %include compiles wrappers that do not work.
status: stable
refs:
  - .specify/2m-pybind.md
  - bindings/python/fixpp.i
  - bindings/python/fixpp_oo.py
  - bindings/python/CMakeLists.txt
refs_external:
  - research/G19-fix-fpml-iso20022/decisions/2m-pybind.md
codegraph_entry: []
---

# Python bindings

> ## ⚠️ The CODE is authoritative. This page is not.
>
> The wrapped function set changes per feature; do not take a list from here.

## Shape: three layers, each with a job

| Layer | File | Job |
|---|---|---|
| C ABI | `include/fix/c_api.h` | the licence seam — see [`c-api.md`](./c-api.md) |
| SWIG substrate | `bindings/python/fixpp.i` | flat, mechanical wrappers over that seam |
| OO layer | `bindings/python/fixpp_oo.py` | pure Python — lifetimes, ownership, safety |

SWIG wraps the **C ABI**, never the C++ library. That is not a convenience choice: `check_layers.py`
forbids the bindings from including the C++ umbrella at all, because the seam is what keeps a non-AGPL
consumer at arm's length.

## ⭐ The selective surface is a FALSE-GREEN GUARD, not laziness

The interface re-declares the functions it needs instead of `%include`-ing whole headers. The file says
why, and it is the sharpest thing on this page:

> A blanket `%include` **compiles unusable wrappers**. Only the end-to-end test forces the typemaps to
> actually work.

So a broad surface would have produced a **green build over wrappers that cannot be called** — an
instrument reporting success because it could not report anything else. The narrow surface exists so
that everything wrapped is also exercised.

⚠️ **The rule that follows: widen the surface and the e2e coverage together.** Adding wrappers alone
re-creates exactly the condition the narrowness was chosen to prevent, and the build will not tell you.

## Lifetime safety is enforced in Python, because it cannot be enforced in C

Two mechanisms in the OO layer, both worth knowing before extending it:

- **A liveness sentinel.** Every handle-bearing wrapper carries `(_handle, _dead)` state and checks it.
  A Python object therefore **cannot outlive its native handle** — post-close or post-dispatch-window
  access raises instead of touching freed memory.
- **Pickling is banned** on handle-bearing wrappers. A pickled native handle would deserialize in
  another process as *a meaningless pointer that use-after-frees on first touch*, so it fails loudly at
  serialization time. ⭐ Scoped to handle-bearing wrappers only — **value-typed classes stay
  picklable**, which is the part to preserve if you touch it.

## The wheel is abi3

Built against the limited API (`Py_LIMITED_API`), so one wheel serves many CPython versions. That
constrains what the extension may use — a non-limited API call will build locally and break the wheel
contract, which is the failure mode to watch for.

## ⭐ The wheel takes the CMake install tree VERBATIM — that is the packaging trap

`Root-Is-Purelib: false`, and scikit-build-core stages whatever the configured `install()` rules
produce. So the wheel's contents are **not** a curated list; they are *the C++ install tree*, minus
whatever `wheel.exclude` in `bindings/python/pyproject.toml` removes.

⚠️ **That is a DENY-LIST, and it has already failed once.** Until #255 it excluded only `include/**`,
so every other root install rule shipped inside the wheel — archives, loose objects,
`lib/cmake/fixpp/`, and a second copy of the dictionaries that **no code path can reach** (the
locator resolves through `importlib.resources` against the `_fixpp_data` *package*; `share/` is not
one). Nothing noticed, because the neighbouring CI checks all inspect the **extension module** — tag,
`NEEDED` set, abi3 conformance — and none of them reads the archive's file list.

**The rule that follows: adding an `install()` rule anywhere in the root `CMakeLists.txt` changes what
the wheel ships.** Check `wheel.exclude` when you add one. The guard is
`ci/check-wheel-payload.sh`, which asserts the **permitted top-level set** rather than probing
known-bad roots — so a rule landing under a name nobody predicted is still rejected.

⚠️ **That guard's reach is narrower than its logic.** It runs inside `python-wheel-build`, which on a
PR is path-gated: the filter names the root `CMakeLists.txt` and `bindings/python/`, but **not a
subdirectory `CMakeLists.txt`**, so a rule added only there is first seen on `push:main` — after the
gate, not before it. Verify against the filter in `tier1.yml` rather than assuming this sentence is
still current.

⚠️ **`share/doc/fixpp/**` is kept in the wheel deliberately** and is not dead weight: it is the
wheel's only copy of `LICENSE`, `NOTICE` and `QUICKFIX_LICENSE.txt`, and the wheel redistributes the
QuickFIX-derived XMLs those attach to. One `install(FILES …)` into `CMAKE_INSTALL_DOCDIR` serves the
wheel *and* the `.tar.gz`/`.deb`/`.rpm`, which is why the licences are not wired through a
wheel-specific setting. ⚠️ PEP 639 cannot do it here anyway — the licences sit two levels above the
project dir, and of the three routes, `[project] license-files` errors while **`wheel.license-files`
and `wheel.force-include` build clean and ship nothing**.

## Re-derive (the packaging half)

```bash
ci/check-wheel-payload.sh <wheel.whl>          # contents + the two-sided assertion
python3 -c 'import zipfile,sys;[print(n) for n in sorted(zipfile.ZipFile(sys.argv[1]).namelist())]' <whl>
```

## Re-derive

```bash
grep -c '%rename\|extern' bindings/python/fixpp.i     # rough surface size
grep -n 'Py_LIMITED_API\|SABI' bindings/python/CMakeLists.txt bindings/python/fixpp.i
grep -n 'class ' bindings/python/fixpp_oo.py          # the OO layer's real shape
```

## Related

- [`c-api.md`](./c-api.md) — the seam this wraps, and why its version is `1.5.0`.
- [`errors.md`](./errors.md) — error codes surface through the same ABI, downgraded per consumer minor.
