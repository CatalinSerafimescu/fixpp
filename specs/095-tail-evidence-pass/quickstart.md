# Quickstart — validating 095

Run from the main checkout (`research/G19-fix-fpml-iso20022/library`), preset `linux-clang-debug`.
`export CCACHE_DIR=/mnt/wsl/fixppbuild/ccache`; `-j2`; check `df -h /mnt/e` first. An agent asks the
owner (`AskUserQuestion`) before the first build it runs (`[const §XVII.7]` resource gate).

Before `/speckit-verify` runs, "the verify record" below means the staging evidence file
`.specify/decisions/095-tail-evidence-pass-evidence.md` (tasks.md Execution rules): verify's own write
replaces the verify record, and tasks T085 copies the evidence file's entries into it.

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
alone and shuffled. RED arms: the table of clause W6 of contracts/witness-contract.md, one row per
output. For each mutant row: apply the scratch mutant, rebuild, rerun the filter, record the message
that fired (it must be the one the row names: W5's for Mutants A and B, W4's for D and H, the
census's for F, G, I and J, the deficient-frame assertion's for C, L, M and N), revert, `git diff --exit-code`. A census mutant that adds or deletes a
witness also changes the listed count, so run it with the count assertion above skipped, and record
that it was.

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
verify record, not in any artifact. The check hard-asserts only BN, and inside the same test it
asserts that its equality predicate is false for BM (research R-7), so a passing run has shown the
predicate able to fail on real inputs. Record a mutant RED for that half too: make the predicate
return `true` unconditionally (scratch), rerun, and record that the BM assertion fired; revert.

## 4. Feature label set

```bash
ctest --preset linux-clang-debug -N -L '^095$' | sed -n 's/^ *Test *#[0-9]*: //p' | sort \
  | diff - specs/095-tail-evidence-pass/expected-ctest-095.txt
ctest --preset linux-clang-debug -L '^095$' --no-tests=error --output-on-failure
```

## 5. Live cells (US3), locally, before the fixpp PR is pushed

The parent harness changes are uncommitted edits in the parent working copy (research R-10); the
main checkout is on the 095 branch, so `../phase-9-harness` sees this tree's template rows.

```bash
IMG=ghcr.io/catalinserafimescu/fixpp-interop-counterparties:latest
P=$(mktemp -d)
docker pull "$IMG"
docker inspect --format '{{index .RepoDigests 0}}' "$IMG" | tee "$P/image-digest.txt"
cid=$(docker create "$IMG")
for x in interop_counterparty lib qfcpp-spec; do docker cp "$cid:/opt/interop/$x" "$P/$x"; done
docker rm "$cid"
cmp dictionaries/FIX50SP2.xml "$P/qfcpp-spec/FIX50SP2.xml"   # FR-014 reads the vendored file in its place
mvn -f ../phase-9-harness/quickfixj/pom.xml package -DskipTests
export INTEROP_QFC_COUNTERPARTY_BIN="$P/interop_counterparty" LD_LIBRARY_PATH="$P/lib" \
       INTEROP_QFCPP_SPEC_DIR="$P/qfcpp-spec" \
       INTEROP_QFJ_JAR="$PWD/../phase-9-harness/quickfixj/target/phase9-harness-quickfixj-0.1.0-SNAPSHOT-all.jar"
test -f "$INTEROP_QFJ_JAR"
bash ../phase-9-harness/tls/gen-interop-certs.sh
# The whole-population reconcile against the local harness; runs nothing. It is the check the
# post-merge interop-live run makes first.
python3 ci/run-interop-live.py --harness ../phase-9-harness --list
targets=$(python3 ci/run-interop-live.py --harness ../phase-9-harness --list-binaries)
test -n "$targets"
cmake --build --preset linux-clang-debug --target $targets interop_golden_check -j2
# First capture only (R-9 item 5), then review the two goldens by hand:
#   python3 ../phase-9-harness/tools/run_interop_cell.py <cell> --build-root build --update-goldens
# Every live cell, the two new ones included: what the PR's interop-live cannot run (L7).
python3 ci/run-interop-live.py --harness ../phase-9-harness --build-root build
python3 -m pytest tests/interop/cell_results_schema_check_test.py -q
cd ../phase-9-harness/tools
python3 -m pytest -q -p no:cacheprovider test_run_interop_cell.py test_emit_matrix.py
python3 -m pytest -q -p no:cacheprovider test_emit_matrix.py -k RegistryAgreesWithTheRealTemplate
```
Expected:
- `cmp` prints nothing.
- The reconcile lists every cell.
- The full run ends "all … live interop cell(s) passed", and each new cell has exactly one passing
  comparison row.
- The harness tests cover every L0 row marked *test*.
- The real-template test passes.

Record the image digest, the jar's source state (`git -C .. status --short -- phase-9-harness`) and
every output in the verify record.

RED arms:
- clause L5 of contracts/live-cell-contract.md;
- the real-template test's RED, recorded once in P5 before the template rows are added: it fails
  naming the two cells;
- the skip-list gate's RED, also in P5 before the two skip lines land: `ci/assert-interop-skips.py`
  over the whole `ctest -L interop` population with no counterparty exits 1 naming the two new
  gtest ids as unexpected skips, then 0 once the lines are in (tasks T055a, T055b).

## 6. CI

- **The fixpp PR's `interop-live`** runs against the pre-095 image. Expected: the build step
  compiles the image's targets, then `ci/run-interop-live.py` fails its coverage reconcile on the
  two new skip-list ids and prints "population reconciliation FAILED -- not running any cell".
  This is advisory, disclosed in the PR body, and not SC-003 evidence (research R-10).
- **After the merge**, in this order:
  1. The one parent commit (harness changes + goldens + gitlink bump) is made, and the orchestrator
     pushes parent `main`.
  2. `harness-tests` passes on that commit.
  3. Wait for `publish-counterparties` and record the image digest. Re-dispatch the parent
     `interop-matrix` if its push run started before the publish finished.
  4. Dispatch the fixpp lane: `gh workflow run interop-live.yml --ref main` (it has no `push`
     trigger).
  5. That run must list both new cells as passed. Record its run id and the pulled digest at
     close-out; the hello carries the `local-build:unpinned` sentinel (data-model §D). If it does
     not pass, the close-out stops and goes to the owner.

## 7. Records (US4, SC-004)

Run on the head after P7; it must pass. Its RED arms, one per check, each recorded with the message
that fired:
- **once before P7:** the first Status check fails, because no tail row is `done` yet;
- **after P7, on scratch copies** (`CAT`/`CI`/`CONST` pointed at a copy under `mktemp -d`, the tree
  untouched), one perturbation per run:
  - one narrowed version cell reverted;
  - `fixpp#625` dropped from N-002's Verified cell;
  - one witness name removed from its row's Tests cell;
  - one witness name removed from its coverage-index gap note;
  - one pin or live-cell ID removed from its row's Tests cell;
  - one corrected Spec ref of each kind reverted (A-033 or C-003 to its old category; A-030 or
    N-003 to its old heading spelling);
  - the amended §VI.2 sentence removed from the constitution copy.

```bash
CAT=${CAT:-spec/feature-catalogue.md}
CI=${CI:-spec/coverage-index.md}
CONST=${CONST:-.specify/constitution.md}
B=build/linux-clang-debug/bin/test_077_allversions_builder_roundtrip
P=build/linux-clang-debug/bin/codegen_077_v50sp2_builder_completeness_test
S=build/linux-clang-debug/bin/session_pure_tests
gtests() { "$1" --gtest_list_tests | awk '/^[^ ]/{s=$1} /^  /{print s $1}'; }
# column $2 of the catalogue row with ID $1; fails unless exactly one row has that ID and it splits
# into its 11 columns (13 fields with the empty edges), so a literal '|' in a cell cannot shift it
col() {
  awk -F'|' -v want="$1" -v c="$2" '{id=$2; gsub(/ /,"",id)} id==want {n++; nf=NF; v=$c}
    END {gsub(/^ +| +$/,"",v); if (n!=1 || nf!=13) exit 1; print v}' "$CAT"
}
for id in A-018 A-023 A-027 A-028 A-029 A-030 A-031 A-032 A-033 C-003 N-002 N-003; do
  v=$(col "$id" 8) || { echo "$id: not exactly one well-formed row"; exit 1; }
  test "$v" = done || { echo "$id: Status is '$v', not done"; exit 1; }
done
for id in A-018 A-029 A-030 N-002; do
  v=$(col "$id" 6) || { echo "$id: not exactly one well-formed row"; exit 1; }
  test "$v" = 5.0SP2 || { echo "$id: version is '$v', not 5.0SP2"; exit 1; }
done
col N-002 12 | command grep -q 'fixpp#625' || { echo "N-002: Verified does not name fixpp#625"; exit 1; }
# every witness: in its row's Tests cell, and named in its MsgType's coverage-index gap note
n=0
for t in $(gtests "$B" | command grep '^TailRows095\.'); do
  c=${t#TailRows095.}; rid=$(echo "${c%%_*}" | sed 's/^\([A-Z]\)/\1-/'); name=${c#*_}
  col "$rid" 11 | command grep -qF "$t" || { echo "$t: not in $rid's Tests cell"; exit 1; }
  mt=$(command grep -o "<message name='$name' msgtype='[^']*'" dictionaries/FIX50SP2.xml | sed "s/.*msgtype='//; s/'//")
  test -n "$mt" || { echo "$t: no dictionary message $name"; exit 1; }
  test "$(awk -F'|' -v m="$mt" '{v=$2; gsub(/ /,"",v)} v==m' "$CI" | wc -l)" -eq 1 || { echo "$mt: not one coverage-index row"; exit 1; }
  awk -F'|' -v m="$mt" '{v=$2; gsub(/ /,"",v)} v==m {print $5}' "$CI" | command grep -qF "$t" \
    || { echo "$mt: gap note does not name $t"; exit 1; }
  n=$((n+1))
done
test "$n" -eq 23 || { echo "checked $n witnesses"; exit 1; }
# N-002 / N-003 pins and the routing cell resolve in their binaries and sit in the right rows
for pair in "N-002 BuilderCompleteness077V50sp2.N002_UserRequestUserResponseRegistered $P" \
            "N-003 BuilderCompleteness077V50sp2.N003_ApplicationMessageRequestAckReportRegistered $P" \
            "N-002 TailRouting095.InboundBeAndBwReachFromApp $S" \
            "N-003 TailRouting095.InboundBeAndBwReachFromApp $S"; do
  set -- $pair
  gtests "$3" | command grep -qxF "$2" || { echo "$2: not in $3"; exit 1; }
  col "$1" 11 | command grep -qF "$2" || { echo "$2: not in $1's Tests cell"; exit 1; }
done
# A-018's live cells resolve in the (local) harness registry and sit in A-018's Tests cell
for cell in HP-QFcpp-init-fixt11-fix50sp2-app-bn HP-QFj-init-fixt11-fix50sp2-app-bn; do
  (cd ../phase-9-harness/tools && python3 -c "import sys, run_interop_cell as r; sys.exit(0 if sys.argv[1] in r.CELLS else 1)" "$cell") \
    || { echo "$cell: not registered"; exit 1; }
  col A-018 11 | command grep -qF "$cell" || { echo "$cell: not in A-018's Tests cell"; exit 1; }
done
# FR-018's Spec ref corrections, and FR-021's amendment, which they need (data-model §E, §F.1)
for pair in 'A-030|[FIX50SP1] Settlement Instruction' 'A-033|[FIX50SP2] Market Data' \
            'C-003|[FIX50SP2] Account Reporting' 'N-003|[FIX50SP2] Application'; do
  id=${pair%%|*}; want=${pair#*|}
  v=$(col "$id" 7) || { echo "$id: not exactly one well-formed row"; exit 1; }
  test "$v" = "$want" || { echo "$id: Spec ref is '$v', not '$want'"; exit 1; }
done
command grep -qF 'may instead use the message-level form `[DocAbbrev] Category`' "$CONST" \
  || { echo "$CONST: §VI.2 is not amended"; exit 1; }
```

SC-004: run 094's `catalogue_census.py` (its text is in `.specify/decisions/094-v1-tier0-bookkeeping-verify.md`)
on the merge-base's catalogue (`git show "$(git merge-base origin/main HEAD)":spec/feature-catalogue.md`)
and on the head's. OFFICIAL open must fall by 12 and done rise by 12. Its `--list` output must differ
only in the 12 tail rows. Every output goes in the verify record.
