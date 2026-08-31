---
type: Component Decision Map
title: QuickFIX compatibility — a runtime adapter was REJECTED; only config translation ships
description: Path B. The incompatibility is documented rather than bridged, and the rejection is pinned by a file-scope static_assert so the build fails if anyone re-introduces it.
status: stable
refs:
  - include/fixpp/session/quickfix_compat/cfg_loader.hpp
  - src/session/quickfix_compat/cfg_loader.cpp
  - .specify/2e-msgstore.md
  - .specify/architecture.md
  - tests/session/test_quickfix_compat_path_b_guard.cpp
refs_external:
  - research/G19-fix-fpml-iso20022/decisions/2e-msgstore.md
  - research/G19-fix-fpml-iso20022/decisions/architecture.md
codegraph_entry: [cfg_loader]
---

# QuickFIX compatibility

> ## ⚠️ The CODE is authoritative. This page is not.
>
> What it records is a **rejection**, and a rejection does not rot — it is history. The surviving
> surface is small; read the header.

## The decision, and what it rules out

QuickFIX's `MessageStore` is **synchronous**. `fixpp`'s is awaitable. The open question was whether to
ship a shim letting a synchronous QuickFIX-shaped store drop into the engine.

**Answer: no — "Path B".** v1.0 ships:

- a **documented incompatibility** plus a migration recipe, and
- `quickfix_compat::cfg_loader` — a **config-translation** surface,

and deliberately **no runtime adapter**. The disposition is recorded at `[arch §11]` row 3 as CLOSED,
resolved through `[2e §4.8.A]`.

> ⭐ **Why this matters to anyone extending the compat layer:** the boundary is *translation, not
> emulation*. Reading a QuickFIX config file and producing fixpp configuration is in scope. Making a
> QuickFIX object work inside the engine is the thing that was rejected — a synchronous store called
> from an awaitable path is exactly how you block a session strand.

## ⭐ The rejection is enforced by the compiler, not by a comment

`tests/session/test_quickfix_compat_path_b_guard.cpp` is a **file-scope `static_assert`** that a
QuickFIX-shaped synchronous store is **not constructible** into `fixpp::session::MessageStore`. Its own
comment puts it best: *"No runtime assertion is needed — the BUILD IS THE TEST."*

If someone later adds an implicit conversion that would let such a type bind, **compilation fails
before any test binary runs**.

This is the third place in this codebase where a decision is pinned that way — see the frozen error
range in [`errors.md`](./errors.md) and the trace-context size assert in
[`observability.md`](./observability.md). ⭐ **It is the local idiom for "this decision must not be
undone by accident", and it is worth reaching for instead of a comment**: a comment saying *"do not do
X"* is a claim nobody re-checks; a `static_assert` is the same claim checked on every build.

## Re-derive

```bash
ls include/fixpp/session/quickfix_compat/ src/session/quickfix_compat/   # the whole surface
grep -n static_assert tests/session/test_quickfix_compat_path_b_guard.cpp
```

If that directory ever contains more than config translation, this page's central claim is stale —
fix it here rather than working around it.

## Related

- [`message-store-quiescence.md`](./message-store-quiescence.md) — the awaitable store contract this
  rejection protects.
- [`config.md`](./config.md) — the native config loader the translation feeds.
