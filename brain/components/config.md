---
type: Component Decision Map
title: Configuration — TOML file loading, and the resolver registry that turns names into objects
description: Config is not just scalars. A resolver registry selects plugin implementations by name, which is why the loader is large.
status: stable
refs:
  - include/fixpp/config/toml_config_loader.hpp
  - include/fixpp/config/config_bundle.hpp
  - include/fixpp/config/load_diagnostic.hpp
  - src/config/selector_resolver.cpp
  - src/config/logger_resolver.cpp
  - specs/099-fixt-appl-version-validation/contracts/surface.md
refs_external:
  - research/G19-fix-fpml-iso20022/decisions/speckit/099-fixt-appl-version-validation-gatea.md
codegraph_entry: [toml_config_loader, config_bundle, load_diagnostic]
---

# Configuration

> ## ⚠️ The CODE is authoritative. This page is not.
>
> It routes and records intent. The key names, the defaults and the supported types are in the headers
> and the loader, and they change per feature.

## Where to look

| You want | Go to |
|---|---|
| The public entry point | `include/fixpp/config/toml_config_loader.hpp` |
| What a load produces | `include/fixpp/config/config_bundle.hpp` |
| What a load reports when it fails | `include/fixpp/config/load_diagnostic.hpp` |
| Scalar key → field mapping | `src/config/scalar_mappers.cpp` |
| **Name → plugin implementation** | `src/config/selector_resolver.cpp`, `src/config/logger_resolver.cpp` |

## Why the loader is so much bigger than "parse some TOML"

Two jobs, not one:

1. **Scalars** — map keys onto configuration fields, with types and diagnostics.
2. **Selection** — resolve a *name in a file* to a *plugin implementation*.

⚠️ **Corrected 2026-08-31: this is NOT a registry.** `selector_resolver.cpp` and `logger_resolver.cpp`
are straight-line `if (kind == "memory") … else if (kind == "file") …` chains that construct the
concrete factory directly. Calling it a registry implies a registration point that does not exist —
and would send someone looking for the wrong thing. Found by the blind agent that *had* this bundle
and checked the claim against source anyway, which is the bundle working as intended.

Job 2 is the interesting half and is where most of the code is. It is what makes a config file able to
choose a store, a logger or a transport without the host writing C++.

> ⭐ **Consequence worth knowing before extending it:** adding a plugin implementation is not finished
> when the class exists — it is finished when the resolver can name it. A plugin the resolver does not
> know is unreachable from a config file, and nothing about that failure looks like a missing
> registration.

## The boundary the config file does NOT cross

Configuration selects **objects**. It does not supply **behaviour**: the `Application` callbacks — and
any bespoke host-owned objects — stay host-injected in C++. That split is deliberate; a config file
that could supply behaviour would be a scripting surface, which is not what this is.

## Diagnostics are a return value, not an exception

Failure flows through the loader's result type, and the header states the contract in one line: *"All
failure flows through LoadResult; never throws (noexcept)."* That is the engine-wide convention (see
[`errors.md`](./errors.md)) applied at the config boundary — a caller is expected to inspect the
result, because **there is no throwing path to catch.**

## `[dictionary] extra_paths` (099)

What it does is `specs/099-fixt-appl-version-validation/contracts/surface.md`'s `## TOML` and `src/config/selector_resolver.cpp`; behaviour a user must know is `B-099-28`. The half the code cannot tell you:

- **Pushed after `path`, so `engine.dictionaries.front()` stays the session default** (research R-12), which is how a session with no `dictionary` of its own is filled. There is no engine fallback at run time: that default is filled by the loader (FR-019).
- **No overlay applies to an extra** (FR-018): `dialect_overlay` still merges onto a session's base.
- **The config loader runs no FIXT transport check.** `load_toml_config` builds no engine, so the refusal is the host's `Engine::register_session` (research R-11).
- ⚠️ **A lead, not in scope:** `recognize_keys` walks top-level keys only, so a misspelt `[dictionary]` sub-key is silently ignored, a misspelt `extra_paths` included (fixpp#659). Read from source when 099 filed it; check the issue before relying on this.

## Re-derive

```bash
ls src/config/                                              # the real shape; sizes tell you where the work is
grep -rn 'register\|resolver' src/config/selector_resolver.cpp | head   # how a name binds to an implementation
```

## Related

- [`quickfix-compat.md`](./quickfix-compat.md) — QuickFIX config files are *translated* into this,
  which is the whole of the compat surface.
- [`plugin-factory-ownership.md`](./plugin-factory-ownership.md) — the C++ side of the same objects.
