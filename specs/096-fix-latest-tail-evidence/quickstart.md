# Quickstart — validating 096

Run from the library root (`research/G19-fix-fpml-iso20022/library`), preset `linux-clang-debug`
(build tree `build/linux-clang-debug`). Every command's output goes into the evidence file
(tasks.md Execution rules), with the library `HEAD` it ran at.

## 0. Inputs hold

```bash
cmp ../../../reference-engines/quickfix-cpp/spec/FIX50SP2.xml dictionaries/FIX50SP2.xml   # exit 0
for mt in DS DT DJ DK DC DR DO DP CF CG CK CX CY CU CV CZ DA DB CL CM CR CS CT DE DH DI DF DG CN CO CW; do
  test "$(grep -o "<message [^>]*msgtype='$mt'[^>]*>" dictionaries/FIX50SP2.xml | grep -c "msgcat='app'")" = 1 || echo "MISSING v50sp2 $mt"
  grep -q "<fixr:message [^>]*msgType=\"$mt\"" dictionaries/orchestra/OrchestraFIXLatest.xml || echo "MISSING orchestra $mt"
done                                                                                       # prints nothing
```

## 1. The move is a move (W0)

```bash
git diff origin/main -- tests/session/test_095_tail_rows_roundtrip.cpp | grep '^[-+]' | grep -v '^[-+][-+]' > /tmp/096-move.diff
```

Every removed line of a moved item must appear verbatim in `tests/support/tail_rows_witness.hpp`
(the checker below prints each one that does not); every added line in 095's TU is an `#include`, a
`using` declaration, or the one-line forward of `check_witness`.

```bash
grep '^-' /tmp/096-move.diff | sed 's/^-//' | grep -v '^\s*$' | while IFS= read -r l; do
  grep -qxF -- "$l" tests/support/tail_rows_witness.hpp || printf 'NOT MOVED: %s\n' "$l"; done
```

Positive control: run the checker against a header with one moved line edited (scratch copy); it
must print that line.

## 2. Witnesses, censuses, differential (US1, US2)

```bash
cmake --build build/linux-clang-debug --target test_077_allversions_builder_roundtrip test_077_allversions_builder_roundtrip_vlatest
B=build/linux-clang-debug/bin
$B/test_077_allversions_builder_roundtrip --gtest_filter='TailRows096*:TailRows095*:AllVersionsRoundtrip077V50SP2.*'
$B/test_077_allversions_builder_roundtrip_vlatest --gtest_filter='TailRows096*:AllVersionsRoundtrip077Vlatest.*'
# the listed witness counts, which a deleted witness would change
test "$($B/test_077_allversions_builder_roundtrip --gtest_list_tests --gtest_filter='TailRows096.*' | grep -c '^  ')" = 31
test "$($B/test_077_allversions_builder_roundtrip_vlatest --gtest_list_tests --gtest_filter='TailRows096Latest.*' | grep -c '^  ')" = 31
ctest --test-dir build/linux-clang-debug -L 096 --output-on-failure
```

Both binaries land in `build/linux-clang-debug/bin` (re-check: `find build/linux-clang-debug -name 'test_077_allversions_builder_roundtrip*' -type f -executable`).

## 3. Non-vacuity (W6)

Each arm of contracts W6: apply the scratch mutant, rebuild the one binary, run the arm's filter, copy
the RED message into the evidence file, revert, `git diff --exit-code` the touched file. Mutants G,
F, I, J may share one rebuild per suite; O gets its own.

## 4. Label set

```bash
ctest --test-dir build/linux-clang-debug -N -L 096 | grep 'Test *#' | sed 's/.*: //' | sort \
  | diff - specs/096-fix-latest-tail-evidence/expected-ctest-096.txt
```

## 5. Records (US3, SC-004)

- Spec refs: data-model §E's script — exit 1 with the expected rows before the catalogue edits,
  exit 0 after, plus its three perturbation arms on a scratch copy.
- Every witness ID in each tail row's Tests cell and coverage-index gap note resolves in
  `--gtest_list_tests` of its binary (match each ID with its backticks: `CrossRequest` is a prefix of
  `CrossRequestAck`).
- The `[const §I.3]` census on the merge-base and on the head: the open count drops by 31 and only
  A-035 … A-065 change Status. A row whose cells hold a literal `|` does not split; the census reads
  only rows that split into 13 fields and prints the count it skipped, which must be equal on both
  sides:

  ```bash
  census() {  # ID<TAB>Status of every OFFICIAL row that splits cleanly; skipped count on stderr
    awk -F'|' '/^\| *[A-Z]+-[0-9]+ *\|/ { if (NF!=13) {skip++; next}
      s=$3; gsub(/ /,"",s); if (s!="OFFICIAL") next
      id=$2; st=$8; gsub(/^ +| +$/,"",id); gsub(/^ +| +$/,"",st); print id "\t" st }
      END { print "skipped " skip+0 > "/dev/stderr" }' "$1"
  }
  python3 -c 'import subprocess as s; b=s.check_output(["git","merge-base","origin/main","HEAD"],text=True).strip(); open("/tmp/096-base-cat.md","wb").write(s.check_output(["git","show",b+":spec/feature-catalogue.md"]))'
  test -s /tmp/096-base-cat.md
  census /tmp/096-base-cat.md > /tmp/096-base.tsv; census spec/feature-catalogue.md > /tmp/096-head.tsv
  open() { awk -F'\t' '$2!="done" && $2 !~ /^dropped/' "$1" | wc -l; }
  echo "open: base $(open /tmp/096-base.tsv) head $(open /tmp/096-head.tsv)"   # head = base - 31
  diff /tmp/096-base.tsv /tmp/096-head.tsv | grep '^>' | cut -f1 | sed 's/^> //' | sort | tr '\n' ' '
  # prints exactly A-035 … A-065
  ```

  Positive control: on a scratch copy of the head catalogue with one tail row set back to
  `backlog`, the open count rises by one and that row drops out of the diff. (The base copy is
  extracted through Python because a shell `git show <rev>:<path>` built from a variable is
  rewritten by the RTK hook.)
- `git diff --name-only origin/main...HEAD -- src include tools/codegen` prints nothing (SC-005).
- The constitution's Sync Impact Report and Status line read PENDING until the RATIFIED flip, and
  `grep -n 'A-035..A-065' .specify/constitution.md` shows the v1.2 bullet no longer listing them as
  milestone content.

## 6. Full build and ctest (`[const §XVII.7]`)

Last, at the final head: full `cmake --build build/linux-clang-debug` and full `ctest` (the
resource gate in tasks.md Execution rules applies).
