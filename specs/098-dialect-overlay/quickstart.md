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

**Resource gate (`[const §XVII.7]`).** Every build or `ctest` below (§1, §3, §4, §5, §6, §8) is a local
build. An agent surfaces an `AskUserQuestion` before it and runs it only once the user approves.

## 1. Before any production edit — witnesses that compile on the unfixed tree

Write and run first exactly the arms the contracts arm table marks **fails** on the unfixed tree: W-6's
two edited-dictionary arms and the replay arm's 5020 half, W-8's rows (all but
`T020_MissingRequired_Dictionary`), and W-10's import-surface row. W-5 and W-7 have no arm here: W-5's
base and edited sessions pass on the unfixed tree (a negative control and a pin), and both W-7 halves
need code that lands in plan phase 5.

```
cd ~/Work/Programming/fixpp-parallel
cmake --build --preset linux-clang-debug -j2
ctest --preset linux-clang-debug -L 098 --output-on-failure
pytest bindings/python/tests/wheel/test_import_surface.py      # W-10, against the built bindings
```

Expected: each listed arm RED for its stated reason (W-6: 5020, or the 5030/5031 pair, found after the
first body field; W-8 AS-1: `open()` returns `invalid_session_config`; the other W-8 rows:
`recognized_not_yet_supported_step2` instead of the stated outcome; W-10: the name is absent). **Stop
rule:** if a listed arm passes on the unfixed tree, its premise is false. Stop and decide; do not adjust
the test.

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
M-7 and M-12 run in `build/linux-clang-asan` (the sanitizer's report is the RED), and again against the
W-11 harness.

## 5. Fuzz

```
cmake --preset linux-clang-asan -DFIXPP_BUILD_FUZZ=ON && cmake --build --preset linux-clang-asan -j2
ctest --preset linux-clang-asan -R fuzz_replay_dict_overlay --output-on-failure
```

The ten-minute campaign is `/speckit-verify` Step 7 (`--fuzz-duration=600`).

## 6. Performance (W-12)

Release build (`linux-clang-release`): time `XmlLoader::load(FIX50SP2.xml)` and
`load_with_overlay(FIX50SP2.xml, <the named at-cap fixture overlay>)`, median of ≥ 5 runs each, ratio
≤ 1.5, recorded in the verify record with the machine, the commit and the fixture's expansion count. The send path's paired bench runs under
`/speckit-verify`'s bench step.

## 7. Records (W-13)

From the parent repo, with `T=~/Work/Programming/fixpp-parallel`:

```
python3 .claude/scripts/check_bl_delta.py --root "$T"
python3 .claude/scripts/check-comment-claims.py --root "$T" --base origin/main
research/G19-fix-fpml-iso20022/tools/brain-external-sweep.sh     # report the execution count
```

plus the steps of `$T/.github/workflows/brain-freshness.yml` (derive them from the workflow), the
line-citation gate over `specs/098-dialect-overlay/`, data-model §G's inventory recipe run in `$T` with
its seeded positive and every hit dispositioned, and the note presence check with its seeded negative.

## 8. Final head

After the §XVII.7 ask: one full configure + build + `ctest` of `linux-clang-debug`; `/speckit-verify`;
then `/gate-b`.
