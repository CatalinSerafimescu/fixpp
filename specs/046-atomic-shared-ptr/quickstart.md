# Quickstart: atomic_shared_ptr — libc++ portability fallback integration

**Feature**: `046-atomic-shared-ptr` | **Date**: 2026-06-20

## What changes for a developer

- **On Tier-1 (libstdc++/MSVC-STL)**: nothing. `fixpp::sync::atomic_shared_ptr<T>` is `std::atomic<std::shared_ptr<T>>`; builds, behavior, perf, and all gates are byte-identical.
- **On libc++**: fixpp now compiles (it did not before). The four snapshot-publishing members use a sharded-mutex fallback transparently.

## Build under libc++ (the new lane)

```bash
# from the library submodule root
conan install . -of build/linux-clang-libc++ \
  --profile=linux-clang-libc++ --build=missing        # libc++ profile; rebuilds deps under libc++
cmake --preset linux-clang-libc++                      # -stdlib=libc++ on compile+link
cmake --build build/linux-clang-libc++ -j2             # per build-resource cap (max -j2)
ctest --test-dir build/linux-clang-libc++ -L sync      # concurrency-relevant subset
```

If OTel-cpp fails to build under libc++ this cycle, configure with the OTel-off / non-OTel target set (D-4) and record the OTel-under-libc++ build as a tracked follow-up.

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
bash tools/check_no_std_mutex_in_awaitable_headers.sh -I include -I <asio> -- \
  include/fixpp/core/sync/async_mutex.hpp include/fixpp/session/engine.hpp   # green under libstdc++ AND libc++ (header is mutex-free — lock type-erased into the .cpp)
bash tools/check_no_raw_atomic_shared_ptr.sh                                  # green (no raw std::atomic<std::shared_ptr> outside the primitive)
```

## Acceptance smoke (SC-001/002)

1. libc++ build compiles with zero errors (was impossible before).
2. Full functional suite passes once under libc++ with libstdc++-equivalent dispositions.
3. Fallback path green under ASan/UBSan/TSan.
4. Native build: `atomic_shared_ptr<T>` is the std alias (`static_assert` in the sync test).
