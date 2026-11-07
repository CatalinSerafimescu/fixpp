# Quickstart: validating 101

Main checkout, build tree `build/linux-clang-debug`. Targeted runs; the matrix is `/speckit-verify`'s.

```sh
cmake --build build/linux-clang-debug -j8 --target session_pure_tests wire_tests capi_tests interop_happy_tests
ctest --test-dir build/linux-clang-debug -R 'RejectFidelity101|DialectOverlayInboundBase098|ValidateGateInbound|UnparseableFrameDisposition.LateSite|099|ErrorSurface|HpFix44RejectInvalidAdmin' --output-on-failure
```

Binary and suite names of the new cells are fixed in `tasks.md`.

Expected: the new cells GREEN, each RED on `fa778a8e` and RED with its production change reverted; the cells in
research D9 rewritten with their superseding requirement; `ctest -L alloc_guard` GREEN.

Benchmarks (D8): build `scan_frame_header_bench` and `on_inbound_frame_bench` at `fa778a8e` in a worktree whose path has
the same length as the candidate's, run A-B-A-B on the same machine, compare medians.

MSVC: build the touched test targets in the sandbox (`.sandbox-lock` first) before the first labelled push.
