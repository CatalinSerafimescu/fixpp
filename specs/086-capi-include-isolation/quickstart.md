# Quickstart — validating 086 C-ABI include isolation

**Feature**: 086-capi-include-isolation · **Date**: 2026-08-03

How to prove the feature works end to end. Every step below produces evidence for a specific requirement; the
requirement id is named so the `/speckit-verify` record can cite it.

> **Worktree note.** This feature lives in the parallel worktree `~/Work/Programming/fixpp-parallel`, not in
> `research/G19-fix-fpml-iso20022/library` (which another session holds on `085-fold-flat-cap-loop`).
> `/speckit-verify` and `/gate-b` hardcode the main checkout — substitute this path. See
> `phases/phase-4/parallel-worktrees.md` §4.

## 0. Prerequisites

```bash
cd ~/Work/Programming/fixpp-parallel
export CCACHE_DIR=/mnt/wsl/fixppbuild/ccache      # unset by default; silently uses ~/.cache/ccache otherwise
df -h /mnt/wsl/fixppbuild                          # Debug trees are 22-31 GB; check headroom first
```

Build with **`-j2` maximum** — wider parallelism OOM-kills the host. One build owner per build directory.

## 1. Stage an install

The isolation is a property of the **installed** package; a build tree cannot show it, because
`include_directories()` at `CMakeLists.txt:234` makes everything reachable in-tree by design.

```bash
cmake --preset linux-clang-release
cmake --build build/linux-clang-release -j2
cmake --install build/linux-clang-release --prefix /tmp/fixpp-stage-086
```

## 2. Confirm the layout is additive — FR-005a / SC-003a

Compare **produced manifests**, not install rules. Requires a pre-change manifest captured from `main`.

```bash
(cd /tmp/fixpp-stage-086 && find include -type f | sort) > /tmp/after.txt
comm -23 /tmp/before.txt /tmp/after.txt        # MUST be empty: nothing that shipped before is missing
```

Expect exactly two additions: `include/capi/fix/**` and `include/service-iface/fixpp/service/**`.

## 3. Read the delivered interface — FR-003 / FR-011b

```bash
grep -A4 'Create imported target fixpp::capi$'    /tmp/fixpp-stage-086/lib/cmake/fixpp/fixppTargets.cmake
grep -A4 'Create imported target fixpp::service$' /tmp/fixpp-stage-086/lib/cmake/fixpp/fixppTargets.cmake
```

Expected:

```cmake
set_target_properties(fixpp::capi PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES "${_IMPORT_PREFIX}/include/capi"
  INTERFACE_LINK_LIBRARIES "\$<LINK_ONLY:fixpp::capi_objects>"
)
set_target_properties(fixpp::service PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES "${_IMPORT_PREFIX}/include/service-iface"
  INTERFACE_LINK_LIBRARIES "fixpp::capi"
)
```

> ⚠️ **This step is necessary but NOT sufficient.** It reads the target properties — the same method that
> reported `fixpp::capi` "clean" in 084 while the transitive path was wide open. The reachability tests in §4
> are what actually establish the contract.

## 4. Run the reachability tests — FR-003 / FR-006 / FR-011a / FR-011b

```bash
ctest --test-dir build/linux-clang-release -L consumer --output-on-failure
```

Asserts the full `contracts/include-interface.md` §1 matrix. Two rules govern how (§4 of that contract):
the "MUST NOT resolve" cells are **compile-only** (a link stage fails for unrelated reasons — measured in
`research.md` R5), and a passing positive assertion **never** establishes a negative one, because
`<fix/c_api.h>` resolves from either root under the additive layout.

## 5. Demonstrate the witness RED — FR-007 / SC-002

**Mandatory.** A gate never observed failing proves nothing. Revert the isolation locally and confirm the
witness fails; restore and confirm it passes. Record both commands and both outcomes in the verify record.

```bash
# make PRIVATE -> PUBLIC in src/capi/CMakeLists.txt, then:
cmake --build build/linux-clang-release -j2
cmake --install build/linux-clang-release --prefix /tmp/fixpp-stage-086-red
ctest --test-dir build/linux-clang-release -L consumer --output-on-failure   # MUST fail
git checkout src/capi/CMakeLists.txt                                          # restore
```

Repeat independently for `src/service/CMakeLists.txt`. Two isolations, two red demonstrations — one revert
cannot stand in for the other.

## 6. Confirm symbols still resolve — FR-009 / C-3

Narrowing an *include* interface must not narrow the *link* interface.

```bash
ctest --test-dir build/linux-clang-release -R consumer_witness --output-on-failure
```

`consumer_capi_witness` links `fixpp::capi` and calls `fixpp_library_version()` + `fixpp_strerror()`, forcing
more than one object out of the installed archive.

## 7. Re-measure the export set — FR-016 / SC-006 / C-2

Predicted unchanged at 18 members (`research.md` R2). **Predicted is not measured** — §2a records that reading
`target_link_libraries` was wrong in three places across a three-level cascade.

```bash
grep -c '^add_library(fixpp::' /tmp/fixpp-stage-086/lib/cmake/fixpp/fixppTargets.cmake   # expect 18
ls /tmp/fixpp-stage-086/lib/objects-*/fixpp_capi_objects/ | wc -l                        # expect 11
```

If the count moved, `architecture.md` §7.4's reconciliation table **and** the T024 membership assertion both
move with it.

## 8. Package contents — FR-010 / SC-005

```bash
ctest --test-dir build/linux-clang-release -L packaging --output-on-failure
```

Assertions must be **prefix-normalised**: Linux DEB/RPM/TGZ carry a `usr/` component, the Windows ZIP does not.
A `usr/`-anchored glob finds nothing on Windows and reports "the package carries no C-ABI headers" — a defect
claim about the product manufactured by the test (`package-layout.md` §2).

## 9. In-tree regression — FR-005 / SC-007 / C-4

```bash
ctest --test-dir build/linux-clang-release --output-on-failure
git diff --stat main -- '*.cpp' '*.hpp' '*.h'      # MUST be empty: no source edited for the layout
```

## 10. What local runs cannot cover

- **MSVC** — CI-only. The `usr/`-prefix asymmetry and archive naming (`libfixpp_capi.a`/`.o` vs
  `fixpp_capi.lib`/`.obj`) both differ there.
- **gcc-release** — `/speckit-verify` is clang-only locally; gcc-release is a CI job.
- **Off-host consumability** — the consumer tier is green under the *producing build's* environment only
  (`tests/consumer/CMakeLists.txt:33-38`). It cannot fail on a dependency a third-party consumer would have to
  supply; that property belongs to `tests/packaging/`.
