# Quickstart: validating the cancel_and_drain late-waiter fix

How to reproduce the lost wake (RED), apply the fix, and verify GREEN across the
sanitizer matrix. All commands run from the library submodule root
(`research/G19-fix-fpml-iso20022/library`).

## The witness

`tests/sync/test_async_mutex_drain_latch_publish_acquire.cpp` — moved from feature
046. Races `cancel_and_drain()` against N concurrent `async_lock()` attempts on a
real `asio::thread_pool{4}` over multiple rounds; asserts
`aborted + granted == kWaitersPerRound` every round and `drain_ok_count == 1`,
**bounded by its own internal completion deadline** so a lost wake fails fast and
attributably instead of hanging the lane.

CTest name: `sync_async_mutex_drain_latch_publish_acquire`.

## RED — reproduce the lost wake on current `main`

The race is timing-dependent; contention (fewer cores) widens the window.

```bash
# Build the release preset (libstdc++, Tier 1), then pin to 2 cores and loop.
cmake --build build/linux-clang-release --target sync_async_mutex_drain_latch_publish_acquire -j2
for i in $(seq 1 30); do
  taskset -c 0,1 ctest --test-dir build/linux-clang-release \
    -R '^sync_async_mutex_drain_latch_publish_acquire$' --output-on-failure || break
done
```

Expected on `main`: ~1 in 3 runs trips the witness's internal deadline → FAIL
(pre-fix). Evidence baseline: `research/findings/046-libcxx-tier3-and-006-lostwake.md`.

## GREEN — after the fix

```bash
cmake --build build/linux-clang-release --target sync_async_mutex_drain_latch_publish_acquire -j2
for i in $(seq 1 50); do
  taskset -c 0,1 ctest --test-dir build/linux-clang-release \
    -R '^sync_async_mutex_drain_latch_publish_acquire$' --output-on-failure || exit 1
done
echo "50/50 GREEN"
```

## Mutation-revert (proves the witness is a genuine RED gate, SC-005)

Revert **only Part 1** (the converging confirming-scan) while keeping the seq_cst
handshake, rebuild, and re-run the loop: the witness must return to RED. Restore
the fix afterward. (Reverting Part 2 instead leaves edge #2 exposed — a rarer RED.)

## Sanitizer matrix (run ONE preset at a time — WSL2 -j2 cap)

Validate TSan under **libstdc++**, not libc++ (libc++ TSan throws false
`std::promise` teardown races on the `use_future` join — finding 1, not this bug).

```bash
for P in linux-clang-debug linux-clang-release linux-clang-asan \
         linux-clang-ubsan linux-clang-tsan linux-gcc-release; do
  cmake --build build/$P --target sync_async_mutex_drain_latch_publish_acquire \
        test_cancel_and_drain -j2
  ctest --test-dir build/$P -R 'sync_async_mutex|cancel_and_drain' --output-on-failure
done
```

Pass bar (Success Criteria): zero hangs (SC-001), 100% acquirers terminal
(SC-002), full 006 suite no behavior change (SC-003), zero new sanitizer findings
(SC-004).

## Coverage (Article IX §1 — lcov DA/BRDA basis)

The converging loop's new branches must be hit or waived:
- confirming-scan-empty (converged) **and** confirming-scan-nonempty (late waiter
  caught) — the witness's stress should hit both; if BRDA shows the late-waiter
  branch not-taken, record an Opus risk assessment / waiver in
  `.specify/decisions/047-async-mutex-drain-reap-verify.md`.
- quiesced-immediately vs wait-again (`co_await latch->async_wait()`).
