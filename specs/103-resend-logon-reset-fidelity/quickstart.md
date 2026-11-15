# Quickstart: validating 103

Run from the library root of the tree that holds the branch. One build or test process at a time (machine rule).
Contracts: [session-wire-endings.md](contracts/session-wire-endings.md) (E1–E14), [c-abi-1.17.md](contracts/c-abi-1.17.md),
[constitution-xi3-amendment.md](contracts/constitution-xi3-amendment.md) (applied first, plan phase 0).

## 1. RED on the base (before each fix)

For each new cell in research "RED cells", build the feature's test target at the commit that adds the cell and
before the fix, and record the failing assertion in the evidence file:

Article VII §8 groups isolation-safe tests into whole-binary buckets (one CTest registration each, for example
`session_pure_tests`), so a GTest cell name passed to `ctest -R` matches nothing and ctest exits 0 with "No tests
were found": an instrument that can only report clean. Select instead (loop 2 round 1, Codex P3-1):

```bash
cmake --build --preset linux-clang-debug --target <the cell's test target>   # e.g. session_pure_tests
# one cell inside a bucket: run the binary directly, and record the reported test count (0 fails the step)
<build>/tests/session/session_pure_tests --gtest_filter='Suite.Cell'
# constitutional runs: by label, refusing an empty selection (label 103 is added to every bucket 103 touches)
ctest --preset linux-clang-debug -L 103 --no-tests=error --output-on-failure
```

`ctest -R <target>` is used only for §8's standalone "live `-R <target>` selection" exception.

#577 and #578: if the reproduction is GREEN on the base, stop for that issue and record not-a-bug with the evidence.

## 2. GREEN after each fix, and the mutant

Re-run the same selection after the fix (GREEN, with the same non-zero test count), then revert only the fix's decisive line in a scratch copy and re-run
(RED again). Each mutant is named in research "RED cells".

## 3. The FR-020 and Arm-B censuses (measured)

In a scratch copy (never committed), with the seeded sentinel `tests/session/test_103_census_sentinel.cpp` added:
Pass P (poison default; non-fatal, one flushed `CENSUS-HIT` line on stdout per hit, the run continuing with the base
value) and Pass F (default flipped) per research D8, and the Arm-B Logout applied alone per D9. Run the session,
capi and config labels and the standalone interop labels with each binary's output captured, and attribute each
`CENSUS-HIT` to the `[ RUN      ]` line before it (grouped binaries would hide every later cell if the poison
aborted):

```bash
ctest --preset linux-clang-debug -L session -V
ctest --preset linux-clang-debug -L capi -V
ctest --preset linux-clang-debug -L config -V
```

Known positives: the sentinel (static matcher, Pass P and Pass F; its `CENSUS-HIT` must land under its own
`[ RUN ]` line), `PossDupToleranceTest.ArmB_Regression_Pin_NoLogout` (Arm B). Negative control:
`StoreFailReconcileTest.VariantC_BilateralStrictDefault_RegressionGuardNotClean` (it names the policy through
`make_initiator_cfg`) must not appear in any of the three. A census without its known positive, or with its negative
control, is broken.

## 4. Whole-feature checks

- `pytest bindings/python/tests` (the binding's default-policy comment and cells).
- SC-005: R-588-a/b once on each of `linux-clang-asan`, `linux-clang-ubsan` and `linux-clang-tsan` (the hold makes
  them deterministic) with their mutants; R-588-h on
  `linux-clang-tsan` and `linux-clang-asan` repeated n times, n derived from its mutant's measured miss rate
  (research R-588-h), recorded in the evidence file.
- The paired benches (SC-006) against the base, with the base worktree path padded to equal length: the send path,
  and the inbound-frame path (an Active Heartbeat, an Active application message).
- The send-path allocation witness: the Release, Linux-only registrations
  `alloc_guard_544_we_sends_interleaved_mallocnesia` and its MemoryStore twin
  `alloc_guard_103_we_sends_memorystore_mallocnesia` read zero; in a scratch copy, an allocation planted in the
  gate's shared fast path turns both RED (research D5′ "Allocation"). `test_session_alloc_guard` is not this
  witness: it never runs `Session::send`.
- MSVC: build the new test targets (`-k 0`) before the first labelled push.

## 5. SC-002 (re-scoped by owner ruling, Gate A round 1)

With the interop helpers no longer forcing a policy, run the live default-config Logon cells against QuickFIX C++
v1.16.0 and QuickFIX/J 3.0.2 (the counterparty image re-derived from the harness at run time, its digest recorded
in the evidence file, tasks T176; no digest is quoted here to copy): `Fix44/HappyLogonHbLogout`,
`Fixt/HappyFixtLogonHbLogout`, `Fix44/ResetOnLogonInitiator`, `Fix44/ResetOnLogonAcceptor` and
`Fix44/HappyDisconnectReconnectNoReset` (re-derive the live set: `tests/interop/expected-skips-without-counterparty.txt`
minus `tests/interop/live-cells-excluded.txt`). Expected: every cell passes with the default configuration, and
`HappyDisconnectReconnectNoReset` goes RED with the strict default restored (its reconnect logs on at a stored number
above 1). Then the in-process FileStore restart-and-resume cell, both roles, against the peer fixture: GREEN with the
default, RED with the strict default restored. The live restart-and-resume suites stay fixpp#503's.

## 6. Records

B&L delta (research "B&L delta" + pass-2 amendments), `spec/feature-catalogue.md` and `spec/coverage-index.md`
rows, brain supersession flags; then `/speckit-verify` and Gate B.
