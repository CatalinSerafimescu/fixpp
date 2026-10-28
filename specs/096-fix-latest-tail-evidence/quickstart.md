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
# QuickFIX/J 3.0.1's base edition defines none of the 31 (FR-011's Verified cells); fail-closed
Q=../../../reference-engines/quickfixj
QF=$Q/quickfixj-messages/quickfixj-messages-fix50sp2/src/main/resources/FIX50SP2.modified.xml
test -f "$QF" || echo "MISSING $QF"
test "$(git -C "$Q" describe --tags)" = QFJ_RELEASE_3_0_1 || echo "QuickFIX/J checkout is not QFJ_RELEASE_3_0_1"
test "$(grep -c "msgtype=.BN." "$QF")" = 1 || echo "POSITIVE CONTROL FAILED: BN not found once"
for mt in DS DT DJ DK DC DR DO DP CF CG CK CX CY CU CV CZ DA DB CL CM CR CS CT DE DH DI DF DG CN CO CW; do
  test "$(grep -c "msgtype=.$mt." "$QF")" = 0 || echo "QuickFIX/J DEFINES $mt"
done                                                                                       # prints nothing
```

A missing file prints `MISSING` and a `DEFINES` line per MsgType (an empty count is not `0`), so the
loop cannot read clean on an absent input. Positive control for the loop body: run it with `BN` in
the list; it prints `QuickFIX/J DEFINES BN`.

## 1. The move is a move (W0)

Two-way and formatting-robust: both sides of the moved region are normalised the same way (comments
and a line-leading `inline ` stripped, lines joined, whitespace collapsed, one statement or brace per
line), then diffed, so clang-format's reflow and realignment vanish while an added, dropped, edited
or reordered statement prints. `check_witness` is diffed separately, with contracts W0's three
substitutions applied to 095's side; the rest of the header may hold only `#pragma once`, `#include`s
and the namespace wrapper. Conditions:
- no string literal in the region contains `//` (re-check: `sed -n '/^using IndexView/,/^using
  Intent =/p' <095 TU> | grep -n '"[^"]*//'` prints nothing);
- the whitespace collapse also hides a whitespace-only edit inside a string literal; a literal is
  compared token by token only;
- the header uses `//` comments; a `/* */` comment outside the regions prints as `OUTSIDE:` (a
  false RED, never a false clean);
- the base is `origin/main`'s copy of 095's TU: if `origin/main` moves past a change to that TU,
  this check reads RED, not clean, and the base is re-taken before a `MOVE:` line is read as a
  defect.

```bash
H=${H:-tests/support/tail_rows_witness.hpp}
BASE=/tmp/096-095-base.cpp
git show origin/main:tests/session/test_095_tail_rows_roundtrip.cpp > "$BASE"; test -s "$BASE"
norm() { sed 's://.*$::; s/^[[:space:]]*inline //' | tr '\n' ' ' | tr -s '[:space:]' ' ' \
  | sed 's/\([;{}]\) */\1\n/g' | sed 's/( /(/g; s/ )/)/g; s/^ //; s/ $//' | grep -v '^$'; }
moved_base() { awk '/^using IndexView/{p=1} /^\/\/ .*The tail table/{p=0} /^std::string drop_hyphen\(/{p=1}
  /^\/\/ .*Fixture:/{p=0} p; /^using Intent =/' "$1"; }
moved_hdr() { awk '/^[[:space:]]*using IndexView/{p=1} /^[[:space:]]*inline void check_witness\(/{p=0} p' "$1"; }
cw_base() { awk '/^    void check_witness\(/,/^    }$/' "$1" | norm | sed '1d' \
  | sed 's/\btv()/tv/g; s/\bwalk()/walk/g; s/&read_arena_/read_arena/g' | grep -vF 'std::string const expected_mt ='; }
cw_hdr() { awk '/^[[:space:]]*inline void check_witness\(/{p=1} p; p&&/^}$/{exit}' "$1" | norm | sed '1d'; }
diff <(moved_base "$BASE" | norm) <(moved_hdr "$H" | norm) | sed 's/^/MOVE: /'
diff <(cw_base "$BASE") <(cw_hdr "$H") | sed 's/^/CHECK_WITNESS: /'
test "$(moved_hdr "$H" | norm | wc -l)" -gt 100 || echo "EMPTY REGION: the header's markers did not match"
test "$(cw_hdr "$H" | wc -l)" -gt 10 || echo "EMPTY REGION: no free check_witness in the header"
# nothing else in the header: outside the two regions only #pragma once, #include, the namespace
awk '/^[[:space:]]*using IndexView/{p=1} /^[[:space:]]*inline void check_witness\(/{p=0; q=1} !p&&!q; q&&/^}$/{q=0}' "$H" \
  | sed 's://.*$::; s/^[[:space:]]*//; s/[[:space:]]*$//' | grep -v '^$' \
  | grep -vE '^(#pragma once|#include .*|namespace [A-Za-z_:]+ \{|\})$' | sed 's/^/OUTSIDE: /'
# 095's TU no longer defines the moved items (on origin/main's copy this prints 4)
grep -cE '^(enum class walk_status|std::string join_tags|std::string drop_hyphen|IndexView parse_noting_refusal)' \
  tests/session/test_095_tail_rows_roundtrip.cpp                                         # 0
```

Expected: nothing before the last line, which prints `0`. Controls, each on a scratch copy of the
header passed as `H=…` and each recorded with what it printed:
- **negative:** 095's region copied into a scratch header with `inline ` before each function, a
  free `check_witness` written per W0, then `clang-format --style=file:.clang-format
  --assume-filename=x.hpp` → prints nothing (a correct, reflowed move reads clean);
- **positive, inserted statement:** one statement added to a moved body (e.g. `if (gi == 0)
  continue;` at the top of leg 6 of `required_members_missing`) → prints a `MOVE: >` line;
- **positive, edited token:** one literal or token changed in a moved body → prints `MOVE:` lines;
- **positive, `check_witness`:** one token changed in the free function → prints `CHECK_WITNESS:`;
- **marker:** `using IndexView` renamed → prints `EMPTY REGION`;
- **outside:** a free function appended after `check_witness` → prints `OUTSIDE:` lines.

Every added line in 095's TU is an `#include`, a `using` declaration, or a line of the forwarding
method's body (read `git diff origin/main -- tests/session/test_095_tail_rows_roundtrip.cpp | grep
'^+[^+]'`).

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
- The constitution's Sync Impact Report and Status line read PENDING until the RATIFIED flip.
- §XVIII.2 and §XVIII.7 (US3 AS-2), each check reading differently before and after the amendment:

  ```bash
  # the v1.2 bullet alone (the new SIR quotes the phrase, so the grep is scoped to the bullet)
  grep '^   - \*\*v1\.2 — FIX Latest' .specify/constitution.md | grep -c '(new MsgTypes A-035..A-065'
  # 1 before the amendment, 0 after
  L=$(grep '^7\. \*\*Application-message codegen scope for v1\.0\.\*\*' .specify/constitution.md)
  test -n "$L" || echo "NO §XVIII.7 LINE"
  for n in $(seq 35 65); do id=$(printf 'A-%03d' "$n")
    case "$L" in *"$id"*) ;; *) echo "NOT NAMED $id" ;; esac; done
  # before the amendment: a NOT NAMED line for each of the 31 (its own positive control); after: nothing
  ```

## 6. Full build and ctest (`[const §XVII.7]`)

Last, at the final head: full `cmake --build build/linux-clang-debug` and full `ctest` (the
resource gate in tasks.md Execution rules applies).
