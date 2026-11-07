# Quickstart: validating 100

Build tree: `build/linux-clang-debug` (this worktree's symlink to F:). Targeted runs only; the full matrix is `/speckit-verify`'s.

```sh
cmake --build build/linux-clang-debug -j8 --target wire_tests capi_tests session_pure_tests
ctest --test-dir build/linux-clang-debug -R 'HostileInputHardening|ZeroCountGroup|GroupReadIntegrity100|GroupCursorShellArenaExhaustionCapi|CapiInboundFrameDispositionsQ33|MessageReadGroup|MessageReadFailloud|UnparseableFrameDisposition.LateSite' --output-on-failure
```

Target names are the existing test binaries; the new cells' file and suite names are fixed in `tasks.md`.

Expected:
- Every new cell named in research D9 GREEN; each RED with its production change reverted (mutation table in the verify record).
- The two flipped cells (`CraftedCollisionSetSkipsTargetUnderMatchingSeed`, `MsgGetGroupAtHeadroomExhaustion`) assert the new endings.
- Controls listed in research.md unchanged.
- `ctest -L alloc_guard` (inbound zero-allocation gates) GREEN.

ASan / UBSan on the C-ABI cells: the cursor-shell exhaustion path is where an escaping exception used to unwind
through `extern "C"`; run `build/linux-clang-asan` for `capi_tests` once.

MSVC: build the touched test targets on the sandbox (`.sandbox-lock` first) before the first labelled push.
