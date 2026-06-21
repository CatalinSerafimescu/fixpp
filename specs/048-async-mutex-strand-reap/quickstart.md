# Quickstart — building & verifying 048 (async_mutex strand-local reap)

## Build (Tier-1 libstdc++, the verification basis)

```bash
cd research/G19-fix-fpml-iso20022/library
# caps: -j2; run sanitizer presets ONE AT A TIME (WSL2 ~11 GB OOM risk)
conan install . -of build/linux-clang-debug --profile=conan/profiles/linux-clang-debug --build=missing
cmake --preset linux-clang-debug
cmake --build build/linux-clang-debug -j2 --target fixpp_sync_tests
ctest --test-dir build/linux-clang-debug -R '^sync_' --output-on-failure
```

## The witnesses (what must be green)

- **Strand-local reap (replaces the cross-thread drain suite):** N waiters parked on one strand → `cancel_and_drain()` → all reaped `sync_lock_drained` exactly once, mutex `not_locked`, zero hangs. Stress standalone ≥200 rounds × ≥25 reps (SC-001) — this is the de-flake proof; reverting the synchronous reap must make it fail.
- **Pre-drain holder:** holder suspended on the strand at drain time → drain yields, holder `unlock()` runs, drain finalizes (no hang, no orphan of a holder-spliced waiter).
- **On-strand cancellation during drain:** a parked waiter cancelled (on the strand) while draining → resolved exactly once (no double-resume / no lost-wake); ASan+TSan clean.
- **OOM fail-closed (SC-002):** injected allocation failure at `inherited_slot.assign` and at the grant decision → caller gets `sync_lock_alloc_failed`, process does NOT terminate. (RED on the pre-fix terminate.)
- **Repurposed cross-thread misuse witness:** genuinely-concurrent drive trips the debug assertion (debug build) — NOT barriered-to-green.

## Sanitizer matrix (verify stage, one at a time)

`linux-clang-debug` · `linux-clang-release` · `linux-clang-asan` · `linux-clang-ubsan` · `linux-clang-tsan` (libstdc++ basis) · `linux-gcc-release`. Plus `bench_async_mutex_uncontended` vs the committed baseline (expect recovery of the 047 +16%/+32% regression).

## ABI check

`abidiff` the `fixpp_sync` library vs the baseline — expect **no change** (no new/renumbered symbol; FR-008).

## 046 follow-on (not part of this PR)

After 048 merges, 046 rebases on it; its `atomic<shared_ptr>` fallback consumer set drops 4→3 (drop `drain_latch_ptr_`); update 046's consumer-migration tests.
