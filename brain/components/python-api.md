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

## Re-derive

```bash
grep -c '%rename\|extern' bindings/python/fixpp.i     # rough surface size
grep -n 'Py_LIMITED_API\|SABI' bindings/python/CMakeLists.txt bindings/python/fixpp.i
grep -n 'class ' bindings/python/fixpp_oo.py          # the OO layer's real shape
```

## Related

- [`c-api.md`](./c-api.md) — the seam this wraps, and why its version is `1.5.0`.
- [`errors.md`](./errors.md) — error codes surface through the same ABI, downgraded per consumer minor.
