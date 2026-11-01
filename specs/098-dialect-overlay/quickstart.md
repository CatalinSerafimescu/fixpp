# Quickstart: validating 098 dialect overlay

The witnesses and their RED arms are in `contracts/witnesses.md`; this file is the run order.

## 0. Trees

| Tree | Role | Build directories |
|---|---|---|
| `~/Work/Programming/fixpp-parallel` | the PR's tree (branch `098-dialect-overlay`) | `build/linux-clang-{debug,asan}` → `/mnt/wsl/fixppbuild/build/parallel-clang-*` |
| `/mnt/wsl/fixppbuild/fixpp-098-mutants` | mutants only, detached at the head under test (contracts preamble) | its own `build/` on F: |
| main checkout | **not used**; never switched | — |

One build owner per build directory; never a Windows and a Linux build at once. Take
`/mnt/c/temp/fixpp/.sandbox-lock` before any MSVC rsync and release it at close-out.

**Parallelism.** Every build below uses `-j"$J"`, with `J=${J:-8}` at the top of its block: `J` is `8` for
every 098 build (owner, 2026-11-01: "yes, you can run skip asking me before each build, and you can use -j8 for all of them"; tasks T002). An empty `J` would give a bare `-j`, which is unbounded.

**Build approval.** The owner's standing instruction (tasks T002) approves the builds below; the
pre-PR build gate of `[const §XVII.7]` is §8.

## 1. Before any production edit — witnesses that compile on the unfixed tree

Write and run first exactly the arms the contracts arm table marks **fails** on the unfixed tree: W-6's
two edited-dictionary arms and the replay arm's 5020 half, W-8's rows (all but
`T020_MissingRequired_Dictionary`), W-9's version pin and symbol golden (phase 1 appends `098` to
`capi_pure_tests`'s labels, so `-L 098` runs them), and W-10's import-surface row. W-5 and W-7 have no
arm here: W-5's base and edited sessions pass on the unfixed tree (a negative control and a pin), and
both W-7 halves need code that lands in plan phase 5.

```
cd ~/Work/Programming/fixpp-parallel
J=${J:-8}                                                       # never empty: a bare -j is unbounded
cmake --preset linux-clang-debug -DFIXPP_BUILD_PYTHON=ON &&    # the preset leaves the bindings OFF
  cmake --build --preset linux-clang-debug -j"$J"               # a failed configure must stop here
ctest --preset linux-clang-debug -L 098 --no-tests=error --output-on-failure
L="$PWD/build/linux-clang-debug/lib"
PYTHONPATH="$L" python3 -c 'import fixpp; print(fixpp.__file__)'  # must print a path under $L
W=$(mktemp -d) && cp bindings/python/tests/wheel/test_import_surface.py "$W"/
PYTHONPATH="$L" python3 -m pytest "$W/test_import_surface.py"    # W-10 import surface
```

The wheel test is run from a copy, as CI's wheel job runs the wheel suite: the in-tree `conftest.py`
ignores `wheel/*`, and the wheel suite shares basenames with the in-tree one. The configure must return
0: if it fails (SWIG not on `PATH`, say), a `lib/` left by an earlier Python build still imports from
`$L`, and the `__file__` check cannot tell that it is stale.

Expected: each listed arm RED for its stated reason (W-6: 5020, or the 5030/5031 pair, found after the
first body field; W-8 AS-1: `open()` returns `invalid_session_config`; the other W-8 rows:
`recognized_not_yet_supported_step2` instead of the stated outcome; W-9: the version reads 1.12, and the
golden reports `fixpp_dict_load_from_xml_with_overlay` missing from the library; W-10: the exact-set
assertion, whose only difference is the missing `dict_load_from_xml_with_overlay`). An ImportError, a
`fixpp` imported from outside `$L`, or any other set difference is RED for the wrong reason: stop.
**Stop rule:** if a listed arm passes on the unfixed tree, its premise is false. Stop and decide; do not
adjust the test.

## 2. Comparator first

W-0 GREEN on its identical pair and every seeded row's reported set equal to its declared expected set,
before W-1/W-2 are read.

## 3. After each implementation phase

```
J=${J:-8}
cmake --build --preset linux-clang-debug -j"$J"
ctest --preset linux-clang-debug -L 098 --no-tests=error --output-on-failure
ctest --preset linux-clang-debug -L "dictionary|config|capi|session|091|field-order" --no-tests=error --output-on-failure
```

Check each selection instead of assuming it. `ctest --preset linux-clang-debug -N -L 098` must list
every ctest entry that runs a 098 witness file, including the W-7 census and, from phase 6, the W-9 live
arm. `ctest --preset linux-clang-debug -N -L "<module regex>"` must list the entries that run every file
W-6's byte identity names. An entry missing from a list gets the label, or joins the regex, before the
run is trusted.

From phase 6 on, also run W-10, which no ctest label reaches:

```
L="$PWD/build/linux-clang-debug/lib"
PYTHONPATH="$L" python3 -m pytest bindings/python/tests/test_098_dialect_overlay.py -v
W=$(mktemp -d) && cp bindings/python/tests/wheel/test_import_surface.py "$W"/
PYTHONPATH="$L" python3 -m pytest "$W/test_import_surface.py"
```

Run the full suite at the end of phases 3, 5 and 7 (plan). The existing header-order witnesses named in
W-6 must stay green with unchanged expected bytes.

## 4. Mutants

In the mutants tree, per contracts: apply one mutant, rebuild (rc 0 or the row proves nothing), run the
named binary **unfiltered**, record witness | mutant | build rc | test rc | message | collateral, revert.
A mutant whose W-3 rows exist in both formats runs once per format (contracts W-3). M-7 runs in
`build/linux-clang-asan` so that an overflow is reported; its RED is the cycle row's cycle-error
assertion, and the record states how the run ended (contracts W-3). M-12 runs in the Debug tree: its RED
is each bomb row's origin assertion, and the origin it reports differs per row (contracts W-3: step 6's
side-table fit refuses the doubling rows, the counter refuses the empty-chain rows, and the counter's
arming bounds the run, research R-5). Both run again against the W-11 harness, M-7 with each format's
cycle seed and M-12 with each format's empty-chain seed. M-11 and M-19
also run against W-10: the mutants tree is configured with `-DFIXPP_BUILD_PYTHON=ON` (contracts
preamble), and pytest uses that tree's `build/linux-clang-debug/lib`, as in §3. `_fixpp.so` links
`fixpp_capi` statically, so check that it was relinked after the mutant (its timestamp is newer than the
mutant's edit) before a GREEN is read as "the mutant survived".

## 5. Fuzz

```
J=${J:-8}
cmake --preset linux-clang-asan -DFIXPP_BUILD_FUZZ=ON && cmake --build --preset linux-clang-asan -j"$J"
ctest --preset linux-clang-asan -L fuzz --no-tests=error --output-on-failure
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
python3 .claude/scripts/check_bl_delta.py --self-test             # the known positive first
python3 .claude/scripts/check_bl_delta.py gate --feature 098-dialect-overlay --root "$T" --base origin/main
python3 .claude/scripts/check-comment-claims.py --root "$T" --base origin/main
research/G19-fix-fpml-iso20022/tools/brain-external-sweep.sh     # report the execution count
```

plus the steps of `$T/.github/workflows/brain-freshness.yml` (derive them from the workflow), the
line-citation gate over `specs/098-dialect-overlay/`, data-model §G's inventory recipe run in `$T` with
its seeded positive and every hit dispositioned, and the note presence check with its seeded negative.

## 8. Final head

`[const §XVII.7]`'s pre-PR build gate: one Conan install + full configure (with
`-DFIXPP_BUILD_PYTHON=ON`) + build + `ctest` of `linux-clang-debug`, plus §3's W-10 pytest runs and
`pytest bindings/python/tests/` (098 touches `bindings/python/`); the PR body carries
`local build: green on linux-clang-debug @ <sha>` from this run. Then `/speckit-verify`; then `/gate-b`.
