# Quickstart: validating 103

Run from the library root of the tree that holds the branch. One build or test process at a time (machine rule).
Contracts: [session-wire-endings.md](contracts/session-wire-endings.md) (E1–E14), [c-abi-1.17.md](contracts/c-abi-1.17.md).

## 1. RED on the base (before each fix)

For each new cell in research "RED cells", build the feature's test target at the commit that adds the cell and
before the fix, and record the failing assertion in the evidence file:

```bash
cmake --build --preset linux-clang-debug --target <the cell's test target>   # e.g. session_pure_tests; new files may add their own target
ctest --preset linux-clang-debug -R '<cell filter>' --output-on-failure
```

#577 and #578: if the reproduction is GREEN on the base, stop for that issue and record not-a-bug with the evidence.

## 2. GREEN after each fix, and the mutant

Re-run the same filter after the fix (GREEN), then revert only the fix's decisive line in a scratch copy and re-run
(RED again). Each mutant is named in research "RED cells".

## 3. The FR-020 and Arm-B censuses (measured)

In a scratch copy (never committed): Pass P (poison default) and Pass F (default flipped) per research D8, and the
Arm-B Logout applied alone per D9; run the session, capi and config labels and the standalone interop labels:

```bash
ctest --preset linux-clang-debug -L session
ctest --preset linux-clang-debug -L capi
ctest --preset linux-clang-debug -L config
```

Known positives: `VariantC_BilateralStrictDefault_RegressionGuardNotClean` (Pass P),
`PossDupToleranceTest.ArmB_Regression_Pin_NoLogout` (Arm B). A census without its known positive is broken.

## 4. Whole-feature checks

- `pytest bindings/python/tests` (the binding's default-policy comment and cells).
- The sanitizer repeat of R-588-a/b (SC-005) on `linux-clang-tsan` and `linux-clang-asan`.
- The paired send-path bench (SC-006) against the base, with the base worktree path padded to equal length.
- The send-path allocation witness (`test_session_alloc_guard`) stays at zero.
- MSVC: build the new test targets (`-k 0`) before the first labelled push.

## 5. Live interop (SC-002)

With the interop helpers no longer forcing a policy, run the restart-and-resume cells against QuickFIX C++ v1.16.0
and QuickFIX/J 3.0.2 (counterparty image `sha256:5c9928840b6f…`, pinned). Expected: every cell passes with the
default configuration; E1 and E3 visible in the transcripts.

## 6. Records

B&L delta (research "B&L delta" + pass-2 amendments), `spec/feature-catalogue.md` and `spec/coverage-index.md`
rows, brain supersession flags; then `/speckit-verify` and Gate B.
