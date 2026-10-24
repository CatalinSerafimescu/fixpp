# Quickstart — validating 095

Run from the main checkout (`research/G19-fix-fpml-iso20022/library`), preset `linux-clang-debug`.
`export CCACHE_DIR=/mnt/wsl/fixppbuild/ccache`; `-j2`; check `df -h /mnt/e` first. An agent asks the
owner (`AskUserQuestion`) before the first build it runs (`[const §XVII.7]` resource gate).

Run each block as a script (`bash -e`). A zero-match `--gtest_filter` exits 0, and the vendored
GoogleTest has no `--gtest_fail_if_no_test_selected` (it prints the usage text and exits 0), so every
filtered run is preceded by a count assertion that stops the block; every `ctest` line carries
`--no-tests=error`.

## 1. Offline witnesses (US1)

```bash
cmake --build --preset linux-clang-debug --target test_077_allversions_builder_roundtrip -j2
B=build/linux-clang-debug/bin/test_077_allversions_builder_roundtrip
n=$("$B" --gtest_filter='TailRows095.*' --gtest_list_tests | command grep -c '^  ' || true)
test "$n" -eq 23 || { echo "expected 23 witnesses, listed $n"; exit 1; }
"$B" --gtest_filter='TailRows095.*:TailRows095Census.*'
"$B" --gtest_filter='TailRows095Census.*'                       # the census alone (order-free)
"$B" --gtest_filter='TailRows095.*:TailRows095Census.*' --gtest_shuffle --gtest_repeat=3
```
Expected: every `TailRows095.*` and `TailRows095Census.*` test passes, including the census run
alone and shuffled. RED arms: clause W6 of contracts/witness-contract.md — apply each scratch mutant,
rebuild, rerun the filter, record the failing assertion (for Mutants A and B it must be W5's
message), revert, `git diff --exit-code`.

## 2. N-002 / N-003 (US2)

```bash
cmake --build --preset linux-clang-debug --target codegen_077_v50sp2_builder_completeness_test session_pure_tests -j2
P=build/linux-clang-debug/bin/codegen_077_v50sp2_builder_completeness_test
n=$("$P" --gtest_filter='*.N002_*:*.N003_*' --gtest_list_tests | command grep -c '^  N00' || true)
test "$n" -eq 2 || { echo "expected 2 pins, listed $n"; exit 1; }
"$P" --gtest_filter='*.N002_*:*.N003_*'
S=build/linux-clang-debug/bin/session_pure_tests
n=$("$S" --gtest_filter='TailRouting095.*' --gtest_list_tests | command grep -c '^  ' || true)
test "$n" -eq 1 || { echo "expected 1 routing cell, listed $n"; exit 1; }
"$S" --gtest_filter='TailRouting095.*'
```
RED arms, one per leg: add `"BE"` to `detail::is_admin_msgtype` (scratch), rebuild
`session_pure_tests`, the BE assertions fail; revert; repeat with `"BW"`; revert, `git diff --exit-code`.

## 3. Live-message selection check (FR-014)

```bash
B=build/linux-clang-debug/bin/test_077_allversions_builder_roundtrip
D=$(mktemp -d)
unzip -o -q ~/.m2/repository/org/quickfixj/quickfixj-messages-fix50sp2/3.0.1/quickfixj-messages-fix50sp2-3.0.1.jar \
  FIX50SP2.xml FIX50SP2.modified.xml -d "$D"
FIXPP_095_QFJ_DICT_DIR="$D" "$B" --gtest_also_run_disabled_tests \
  --gtest_filter='TailRows095Selection.*' | tee "$D/selection.log"
command grep -q '^\[  PASSED  \] 1 test' "$D/selection.log" || { echo "selection check did not pass"; exit 1; }
# RED arm: without its input the check must FAIL, not skip
if env -u FIXPP_095_QFJ_DICT_DIR "$B" --gtest_also_run_disabled_tests \
     --gtest_filter='TailRows095Selection.*'; then echo "check passed without its input"; exit 1; fi
```
The log carries both axes for the tail set (spec Clarifications, Gate A round 1); it goes in the
verify record, not in any artifact.

## 4. Feature label set

```bash
ctest --preset linux-clang-debug -N -L '^095$' | sed -n 's/^ *Test *#[0-9]*: //p' | sort \
  | diff - specs/095-tail-evidence-pass/expected-ctest-095.txt
ctest --preset linux-clang-debug -L '^095$' --no-tests=error --output-on-failure
```

## 5. Live cell (US3), locally

Prerequisites (research R-12): QF-cpp counterparty extracted from the published image
(`docker create ghcr.io/catalinserafimescu/fixpp-interop-counterparties:latest`, `docker cp
<id>:/opt/interop/{interop_counterparty,lib,qfcpp-spec} <dir>`), `INTEROP_QFC_COUNTERPARTY_BIN`,
`LD_LIBRARY_PATH`, `INTEROP_QFCPP_SPEC_DIR` set; QuickFIX/J rebuilt from the patched parent source
(`mvn -f ../phase-9-harness/quickfixj/pom.xml package -DskipTests`) and `INTEROP_QFJ_JAR` pointing
at the `-all.jar`.

```bash
cmake --build --preset linux-clang-debug --target interop_hp_fixt50sp2_logon_hb_logout_test interop_golden_check -j2
python3 ../phase-9-harness/tools/run_interop_cell.py HP-QFcpp-init-fixt11-fix50sp2-app-bn --build-root build
python3 ../phase-9-harness/tools/run_interop_cell.py HP-QFj-init-fixt11-fix50sp2-app-bn   --build-root build
python3 -m pytest tests/interop/cell_results_schema_check_test.py -q
python3 -m pytest ../phase-9-harness/tools/test_run_interop_cell.py ../phase-9-harness/tools/test_emit_matrix.py -q
```
Expected: both cells pass with one passing comparison row each; the harness tests cover every L0
branch marked *test*. RED arms: clause L5 of contracts/live-cell-contract.md.
Regression (L7): re-run 089's `CONV-C1..C4` and `CONV-PROBE` cells against the patched QFJ jar.

## 6. CI

After the parent push and the `publish-counterparties` run: push the fixpp branch; the PR's
`interop-live` run must list both new cells as passed. Record the run id and the pulled image digest
(the hello carries the `local-build:unpinned` sentinel; data-model §D).
