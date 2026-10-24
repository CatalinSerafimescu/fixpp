# Quickstart — validating 095

Run from the main checkout (`research/G19-fix-fpml-iso20022/library`), preset `linux-clang-debug`.
`export CCACHE_DIR=/mnt/wsl/fixppbuild/ccache`; `-j2`; check `df -h /mnt/e` first.

## 1. Offline witnesses (US1)

```bash
cmake --build --preset linux-clang-debug --target test_077_allversions_builder_roundtrip -j2
build/linux-clang-debug/bin/test_077_allversions_builder_roundtrip --gtest_filter='TailRows095*'
ctest --preset linux-clang-debug -R '^test_077_allversions_builder_roundtrip$' --output-on-failure
```
Expected: 23 `TailRows095.*` + the `TailRows095Census.*` tests pass; the 077 tests in the same binary
still pass. RED arms: clause W6 of the witness contract (contracts/witness-contract.md) — apply each scratch mutant, rebuild, rerun
the filter, record the failing assertion, revert, `git diff --exit-code`.

## 2. N-002 / N-003 (US2)

```bash
cmake --build --preset linux-clang-debug --target test_077_v50sp2_builder_completeness session_pure_tests -j2
build/linux-clang-debug/bin/test_077_v50sp2_builder_completeness --gtest_filter='*N00[23]_*'
build/linux-clang-debug/bin/session_pure_tests --gtest_filter='TailRouting095.*'
```
RED arm: add `"BE"` to `detail::is_admin_msgtype` (scratch), rebuild `session_pure_tests`, the
routing cell fails; revert.

## 3. Required-set selection check for the live message (FR-014)

```bash
D=$(mktemp -d); unzip -o -q ~/.m2/repository/org/quickfixj/quickfixj-messages-fix50sp2/3.0.1/*.jar 'FIX50SP2*.xml' -d $D
# the R-1 walk (the census helper's logic, or a script implementing the same rule) over
#   dictionaries/FIX50SP2.xml and $D/FIX50SP2.modified.xml for 35=BN must print the same required set
```

## 4. Live cell (US3), locally

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
```
Expected: both cells pass with one passing comparison row each. RED arms: clause L5 of the live-cell contract (contracts/live-cell-contract.md).
Regression (L7): re-run 089's `CONV-C1..C4` and `CONV-PROBE` cells against the patched QFJ jar.

## 5. CI

After the parent push and the `publish-counterparties` run: push the fixpp branch; the PR's
`interop-live` run must list both new cells as passed. Record the run id and the pulled image digest.
