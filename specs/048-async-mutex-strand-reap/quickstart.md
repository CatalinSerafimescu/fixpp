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

- **Strand-local reap (replaces the cross-thread drain suite):** N waiters parked on one strand → `cancel_and_drain()` → all reaped `sync_lock_aborted` exactly once (shipped code, not `sync_lock_drained`), mutex `not_locked`, zero hangs. Stress standalone ≥200 rounds × ≥25 reps (SC-001). **Discrimination is structural** for the removed machinery (latch/channel/Dekker gone) — NOT a claim that reverting the reap fails a supported-topology behavioral witness (P2-6).
- **Immediate-destroy-after-reap (NEW, P1-1):** N waiters are reaped (resumers posted DURING the drain); destroy the mutex immediately on `cancel_and_drain()` return and prove NO resumer remains outstanding (the `in_flight_resumers_==0` terminal condition held before return) → no UAF; ASan clean. (Reverting the unified quiescence loop to a holder-only wait must RED this.)
- **Reentrant-during-active-drain (NEW, P1-2):** a second `cancel_and_drain()` on the strand while the first is suspended → awaits the first's `draining_complete_`, returns the terminal result; no false-success, no early-destroy.
- **On-strand-cancel-during-reap (NEW, P1-N2):** a parked waiter cancelled (on the strand) while draining → resolved exactly once (single-winner CAS, no double-resume / no lost-wake); ASan+TSan clean.
- **Pre-drain holder:** holder suspended on the strand at drain time → drain yields, holder `unlock()` runs, drain finalizes (no hang, no orphan of a holder-spliced waiter).
- **OOM fail-closed (SC-002):** injected allocation failure at `inherited_slot.assign` → caller gets `sync_lock_alloc_failed`, process does NOT terminate, unrelated sessions stay operational. (RED on the pre-fix terminate.) The resume/yield-post OOM-terminate is out of scope (L-048).
- **Repurposed cross-thread misuse witness:** unsupported drain-overlap driven via a **test-only instrumented executor** (no production assertion seam — P2-4) — NOT barriered-to-green.
- **C++ layout golden (FR-008):** compile-time `static_assert` on `sizeof(async_mutex)`/`alignof(async_mutex)` re-baselined vs the shipped value.
- **FR-009 4→3:** assert the `atomic<shared_ptr>` consumer census drops to three (engine reader-snapshot, transport cert-source, pinset snapshot) — re-confirmed at the 046 rebase.
- **SC-003 contended bench:** `bench_async_mutex` contended path vs the committed main baseline with an explicit no-regression threshold.
- **SC-005 consumer suites:** the two drain consumers (write_gate, seqnum) + the two lock consumers (MemoryStore, FileStore) pass their existing behavioral suites unchanged.

## Sanitizer matrix (verify stage, one at a time)

`linux-clang-debug` · `linux-clang-release` · `linux-clang-asan` · `linux-clang-ubsan` · `linux-clang-tsan` (libstdc++ basis) · `linux-gcc-release`. Plus `bench_async_mutex_uncontended` AND the contended path vs the committed **main** baseline (no-regression threshold; 047 +16%/+32% was never on main).

## ABI / layout check

`abidiff` the `fixpp_sync` library vs the baseline — expect **no C-ABI change** (no new/renumbered symbol; FR-008). BUT `sizeof(async_mutex)` CHANGES (C++ layout): re-baseline the compile-time `sizeof`/`alignof` golden and recompile every embedder (header-mostly; not a runtime `.so` break).

## 046 follow-on (not part of this PR)

After 048 merges, 046 rebases on it; its `atomic<shared_ptr>` fallback consumer set drops 4→3 (drop `drain_latch_ptr_`); update 046's consumer-migration tests.
