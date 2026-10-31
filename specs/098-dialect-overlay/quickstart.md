# Quickstart: validating 098 dialect overlay

The witnesses and their RED arms are in `contracts/witnesses.md`; this file is the run order.

## 0. Trees

| Tree | Role | Build directories |
|---|---|---|
| `~/Work/Programming/fixpp-parallel` | the PR's tree (branch `098-dialect-overlay`) | `build/linux-clang-{debug,asan}` → `/mnt/wsl/fixppbuild/build/parallel-clang-*` |
| `/mnt/wsl/fixppbuild/fixpp-098-mutants` | mutants only, detached at the head under test (contracts preamble) | its own `build/` on F: |
| main checkout | **not used**; never switched | — |

One build owner per build directory; `-j2` at most; never a Windows and a Linux build at once. Take
`/mnt/c/temp/fixpp/.sandbox-lock` before any MSVC rsync and release it at close-out.

## 1. Before any production edit — witnesses that compile on the unfixed tree

Write and run the **U** arms first (contracts): W-5's base-session arm, W-6's edited-dictionary arm,
W-7's per-dictionary header walk (its `is_header_tag` half lands in phase 3), W-8's AS-1 cell, W-10's
import-surface row.

```
cd ~/Work/Programming/fixpp-parallel
cmake --build --preset linux-clang-debug -j2
ctest --preset linux-clang-debug -L 098 --output-on-failure
```

Expected: each **U** arm RED for its stated reason (W-6: 5020 found after the first body field; W-8 AS-1:
`open()` returns `invalid_session_config`; W-10: the name is absent). **Stop rule:** if a **U** arm
passes on the unfixed tree, its premise is false. Stop and decide; do not adjust the test.

## 2. Comparator first

W-0 GREEN on its identical pair and RED (a reported difference naming the store) on every seeded row,
before W-1/W-2 are read.

## 3. After each implementation phase

```
cmake --build --preset linux-clang-debug -j2
ctest --preset linux-clang-debug -L 098 --output-on-failure
ctest --preset linux-clang-debug -R "dict|config|capi|session_send|resend" --output-on-failure
```

and the full suite at the end of phases 3, 5 and 7 (plan). The existing header-order witnesses named in
W-6 must stay green with unchanged expected bytes.

## 4. Mutants

In the mutants tree, per contracts: apply one mutant, rebuild (rc 0 or the row proves nothing), run the
named binary **unfiltered**, record witness | mutant | build rc | test rc | message | collateral, revert.
M-7 runs in `build/linux-clang-asan` (the stack-overflow report is the RED), and again against the W-11
harness.

## 5. Fuzz

```
cmake --preset linux-clang-asan -DFIXPP_BUILD_FUZZ=ON && cmake --build --preset linux-clang-asan -j2
ctest --preset linux-clang-asan -R fuzz_replay_dict_overlay --output-on-failure
```

The ten-minute campaign is `/speckit-verify` Step 7 (`--fuzz-duration=600`).

## 6. Performance (W-12)

Release build (`linux-clang-release`): time `XmlLoader::load(FIX50SP2.xml)` and
`load_with_overlay(FIX50SP2.xml, <overlay at the entry caps>)`, median of ≥ 5 runs each, ratio ≤ 1.5,
recorded in the verify record with the machine and commit. The send path's paired bench runs under
`/speckit-verify`'s bench step.

## 7. Records (W-13)

From the parent repo, with `T=~/Work/Programming/fixpp-parallel`:

```
python3 .claude/scripts/check_bl_delta.py --root "$T"
python3 .claude/scripts/check-comment-claims.py --root "$T" --base origin/main
research/G19-fix-fpml-iso20022/tools/brain-external-sweep.sh     # report the execution count
```

plus the steps of `$T/.github/workflows/brain-freshness.yml` (derive them from the workflow), the
line-citation gate over `specs/098-dialect-overlay/`, and the 2c-note presence check with its seeded
negative.

## 8. Final head

One full configure + build + `ctest` of `linux-clang-debug`; `/speckit-verify`; then `/gate-b`.
