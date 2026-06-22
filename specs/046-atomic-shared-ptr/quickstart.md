# Quickstart: atomic_shared_ptr — libc++ portability fallback integration

**Feature**: `046-atomic-shared-ptr` | **Date**: 2026-06-20

> **REBASE CORRECTION (2026-06-22) — consumer set 4→3.** This file was authored for the original **four-consumer** design. 046 was later rebased onto merged **048** (PR #144), whose strand-local `cancel_and_drain` reap **removed** `core/sync/async_mutex.hpp drain_latch_ptr_`. The as-built migrated consumer set is **three** (`pinset` `snapshot_`, `transport_factory` `cert_source_slot_`, `engine` `reader_snapshot_`); "four" below is the **historical design record**. Authoritative as-built: `spec/feature-catalogue.md` NFR-017.

## What changes for a developer

- **On Tier-1 (libstdc++/MSVC-STL)**: nothing. `fixpp::sync::atomic_shared_ptr<T>` is `std::atomic<std::shared_ptr<T>>`; consumer operation codegen, runtime behavior, and performance remain unchanged; the build graph gains the always-shipped guard archive (`fixpp_sync` promotes INTERFACE→STATIC and always compiles the guard TU, whose symbols are present-but-never-called on the alias path).
- **On libc++**: fixpp now compiles (it did not before). The four snapshot-publishing members use a sharded-mutex fallback transparently.

## Build under libc++ (the new lane)

```bash
# from the library submodule root
conan install . -of build/linux-clang-libc++ \
  --profile=linux-clang-libc++ --build=missing        # libc++ profile; rebuilds deps under libc++ (OTel ENABLED)
cmake --preset linux-clang-libc++                      # -stdlib=libc++ on compile+link
cmake --build build/linux-clang-libc++ -j2             # per build-resource cap (max -j2)

# ── ACCEPTANCE (run once): full functional suite under libc++ (FR-007 / SC-002) ──
ctest --test-dir build/linux-clang-libc++              # UNFILTERED — the whole suite, exactly once

# ── ONGOING Tier-2 lane: concurrency-relevant subset under sanitizers (FR-011) ──
ctest --test-dir build/linux-clang-libc++ -L sync      # the per-change regression subset
```

**Primary acceptance = OTel ENABLED under libc++** (research D-4): the command above builds the full dep set incl. `opentelemetry-cpp` under libc++ and runs the full functional suite once with OTel ON (no public-API change — `engine_trace_context()`'s `fixpp::otel::trace_context` return is a dependency-free fixpp POD).

**Fallback ONLY if `opentelemetry-cpp` proves unbuildable under libc++ this cycle**: this feature adds a **paired** Conan `fixpp:with_otel` option + a matching CMake `FIXPP_BUILD_OTEL` option (both default ON; CMake fails-fast if the two disagree — spec FR-011 / research D-4; no OTel-off configuration exists today). The two MUST be set together — `conan install` resolves `opentelemetry-cpp` out of the dep graph, and the CMake flag drops `src/otel` + the `fixpp_otel` link edges + the OTel tests and `#if FIXPP_BUILD_OTEL`-gates `engine.cpp`'s `#include <fixpp/otel/providers.hpp>` + the `tracer/meter->shutdown()` calls:

```bash
# OTel-OFF fallback — set BOTH halves of the paired option (else configure fails the disagreement check)
conan install . -of build/linux-clang-libc++ \
  --profile=linux-clang-libc++ --build=missing \
  -o fixpp/*:with_otel=False                          # drops opentelemetry-cpp from the Conan dep graph
cmake --preset linux-clang-libc++ -DFIXPP_BUILD_OTEL=OFF   # matching CMake half; gates src/otel + fixpp_otel edges + engine.cpp OTel coupling
cmake --build build/linux-clang-libc++ -j2
ctest --test-dir build/linux-clang-libc++             # "full suite" = the full OTel-OFF-configuration suite (SC-002)
```

Record the OTel-under-libc++ build as a tracked follow-up. "Full suite" under this config means the full OTel-OFF-configuration suite (SC-002).

## Exercise the fallback on a native toolchain (no libc++ host needed)

```bash
# force the sharded-mutex path under the default libstdc++ toolchain
cmake --preset linux-clang-asan -DFIXPP_FORCE_ATOMIC_SHARED_PTR_FALLBACK=ON
cmake --build build/linux-clang-asan -j2
ctest --test-dir build/linux-clang-asan -L sync        # forced-fallback acceptance (SC-006)
```

`FIXPP_FORCE_ATOMIC_SHARED_PTR_NATIVE` is the (untested) escape hatch; the two force macros are mutually exclusive (compile `#error` if both set).

## Verify no Tier-1 regression

```bash
# Tier-1 leg (libstdc++) — the existing check_no_std_mutex_corpus ctest, membership unchanged (engine.hpp already registered).
ctest --test-dir build/linux-clang-debug -R check_no_std_mutex_corpus           # green

# libc++ leg (the actual no-amendment proof, CT-1c) — runs ONLY in the libc++ preset.
# The script is EDITED (CT-1a): fails closed on -E error AND now recognizes/forwards -stdlib=*
# (previously -stdlib=libc++ fell through to the header list → a "header not found" WARNING, NOT an -E
#  error, so the leg silently ran under host libstdc++); it uses the preset's configured clang.
bash tools/check_no_std_mutex_in_awaitable_headers.sh -stdlib=libc++ \
  -I include -I <libc++-include> -I <asio> -- \
  include/fixpp/core/sync/async_mutex.hpp include/fixpp/session/engine.hpp     # green; asserts each header emitted the awaitable marker (else RED, never silent-pass)
# Plus the libc++-active probe (CT-1c): RED-fail if _LIBCPP_VERSION is not defined under the same invocation.
echo '#include <version>' | <preset-clang> -stdlib=libc++ -std=c++23 -x c++ -dM -E - | grep -q _LIBCPP_VERSION   # proves the leg actually ran under libc++
bash tools/check_no_raw_atomic_shared_ptr.sh                                    # green (no raw std::atomic<std::shared_ptr> outside the primitive header under core/sync/detail/)
```

## Acceptance smoke (SC-001/002)

1. libc++ build compiles with zero errors (was impossible before).
2. Full functional suite passes once under libc++ with libstdc++-equivalent dispositions.
3. Fallback path green under ASan/UBSan/TSan.
4. Native build: `atomic_shared_ptr<T>` is the std alias (`static_assert` in the sync test).
