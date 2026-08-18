# Locally-patched `civetweb/1.16`

This directory exists for **one reason**: civetweb 1.16 has a data race on
`ctx->stop_flag` that upstream has already fixed in `master` but has never
shipped in a release.

## The race

`linux-clang-tsan` fails with reports at `civetweb.c:2315` in
`STOP_FLAG_ASSIGN`. The atomic implementation (selected by
`STOP_FLAG_NEEDS_LOCK`, set in `conan/profiles/linux-clang-tsan`) writes the flag
with a compare-and-swap but **seeds that CAS with a plain, non-atomic read**:

```c
sf = mg_atomic_compare_and_swap(f, *f, v);   /* *f is a PLAIN read */
```

The worker threads read the same flag atomically (`mg_atomic_add(f, 0)` in
`STOP_FLAG_IS_ZERO`) while holding `ctx->thread_mutex`. TSan flags the
non-atomic participant. `volatile` provides neither atomicity nor ordering, so
this is a real race, not a sanitizer artifact.

It is reachable from the **shipped** Prometheus exporter path, not only tests:
`MeterProvider` dtor → `PrometheusExporter` → `Exposer` → `CivetServer::close()`
→ `mg_stop`.

## Why a patch and not an upgrade

| | |
|---|---|
| Known upstream? | Yes — [civetweb#861 "Data race during shutdown"](https://github.com/civetweb/civetweb/issues/861), open 2020-04-16, closed 2021-08-30 after 39 comments. Also [#432](https://github.com/civetweb/civetweb/issues/432) via Valgrind. |
| Fixed upstream? | Yes, in `master`: `__atomic_load_n(f, __ATOMIC_SEQ_CST)` replaces `*f`. |
| In a release? | **No.** v1.16 (2023-04-10) is the newest release and still carries `*f`. |
| Upgrade path? | **None** — we are already on the latest release. |

So the patch is a **backport of an existing upstream fix**, not a local
invention. It is one line, taken verbatim from upstream `master`.

## What is here

- `conanfile.py` — a **verbatim, unmodified copy** of the Conan Center recipe.
  Deliberately not edited, so it can be `diff`ed against CCI to detect drift. It
  already calls `export_conandata_patches()` / `apply_conandata_patches()`, which
  is why no recipe change is needed at all.
- `conandata.yml` — the upstream `sources` entry plus one `patches` entry.
- `patches/1.16-0001-stop-flag-atomic-cas-seed.patch`

## How it is wired

`conan export conan/recipes/civetweb --version 1.16` runs inside the **existing**
`Conan install` step of `tier1.yml` — deliberately not as a separate step, which
would trip `ci/test-tier1-python-policy.sh`'s brittle step count for the `linux`
job. `conan install` runs without `--update`, so the exported cache revision is
the one resolved; no remote lookup can override it.

The export changes the **recipe revision** (`civetweb/1.16#3ddba6fb…`). Note the
**`package_id` does NOT change** — a binary is identified by
`recipe_revision:package_id`, so the patched recipe gets its own binary slot and
rebuilds, but any cache keyed on `package_id` alone would be wrong here.

## Verifying it is actually in the binary

Symbol-level, and **discriminating** — `__tsan_atomic64_load` comes from
`__atomic_load_n` and is absent from an unpatched build:

```
$ nm libcivetweb.a | grep -oE '__tsan_atomic[0-9]*_[a-z_]+' | sort | uniq -c
      1 __tsan_atomic64_compare_exchange_val
      1 __tsan_atomic64_fetch_add
      1 __tsan_atomic64_fetch_sub
      1 __tsan_atomic64_load        <-- ONLY present when the patch applied
```

Checking merely that `mg_atomic_compare_and_swap` exists is **not** sufficient:
that is true of the unpatched atomic branch too. That mistake was made once
during this work and is recorded so it is not repeated.

## Retirement condition

Delete this directory, and the `conan export` line in `tier1.yml`, as soon as a
civetweb release **after v1.16** contains the fix, and bump the version pin.

The patch is **self-retiring by failure**: `apply_conandata_patches` fails loudly
on an already-patched tree (verified — `patch` reports "Reversed (or previously
applied) patch detected"). So if a future version ships the fix, the build breaks
rather than silently applying nothing.
