# Quickstart — validating 096

Run from the library root (`research/G19-fix-fpml-iso20022/library`), preset `linux-clang-debug`
(build tree `build/linux-clang-debug`). Every command's output goes into the evidence file
(tasks.md Execution rules), with the library `HEAD` it ran at.

**One convention, every block.** Each shell block is pasted whole as `bash <<'EOF' … EOF`, and the
one Python block (§5a) as `python3 - <<'PY' … PY`, so the block's exit status is the interpreter's
and an `exit` does not close the reader's shell. Every shell block's first line is `set -o pipefail`,
so a pipeline's status is its first failing command's, not its last filter's. Inside, one accumulator: `bad=0`, every discrepancy prints a line naming it and sets `bad=1`, and the block
ends `exit "$bad"`. An expected-zero or expected-one count is written `test "$(grep -c …)" = N`; a
`diff` keeps its status (`out=$(diff …) || { …; bad=1; }`) and is never piped into a filter that
masks it. A block is GREEN only at exit 0; its evidence entry records the exit status beside what it
printed. A merge-base copy of a tracked file is extracted through Python, because the RTK hook
rewrites a shell `git show <rev>:<path>` built from a variable. Blocks write scratch files under
`${TMPDIR:-/tmp}`.

## 0. Inputs hold

```bash
bash <<'EOF'
set -o pipefail
TAIL=${TAIL:-"DS DT DJ DK DC DR DO DP CF CG CK CX CY CU CV CZ DA DB CL CM CR CS CT DE DH DI DF DG CN CO CW"}
Q=${Q:-../../../reference-engines/quickfixj}
QC=${QC:-../../../reference-engines/quickfix-cpp}
bad=0
cmp "$QC/spec/FIX50SP2.xml" dictionaries/FIX50SP2.xml || { echo "CMP: FIX50SP2.xml differs"; bad=1; }
test "$(git -C "$QC" describe --tags 2>/dev/null)" = v1.16.0 || { echo "QuickFIX-cpp checkout is not v1.16.0"; bad=1; }
for mt in $TAIL; do
  test "$(grep -o "<message [^>]*msgtype='$mt'[^>]*>" dictionaries/FIX50SP2.xml | grep -c "msgcat='app'")" = 1 \
    || { echo "MISSING v50sp2 $mt"; bad=1; }
  test "$(grep -c "<fixr:message [^>]*msgType=\"$mt\"" dictionaries/orchestra/OrchestraFIXLatest.xml)" = 1 \
    || { echo "NOT ONE orchestra $mt"; bad=1; }
done
# QuickFIX/J 3.0.1's base edition defines none of the 31 (FR-011's Verified cells); the msgtype
# attribute's value is matched whole, so `BNX` is not `BN`
QF=${QF:-$Q/quickfixj-messages/quickfixj-messages-fix50sp2/src/main/resources/FIX50SP2.modified.xml}
test -f "$QF" || { echo "MISSING $QF"; bad=1; }
test "$(git -C "$Q" describe --tags 2>/dev/null)" = QFJ_RELEASE_3_0_1 || { echo "QuickFIX/J checkout is not QFJ_RELEASE_3_0_1"; bad=1; }
test "$(grep -Ec "[[:space:]]msgtype=['\"]BN['\"]" "$QF" 2>/dev/null)" = 1 || { echo "POSITIVE CONTROL FAILED: BN not found once"; bad=1; }
for mt in $TAIL; do
  test "$(grep -Ec "[[:space:]]msgtype=['\"]$mt['\"]" "$QF" 2>/dev/null)" = 0 || { echo "QuickFIX/J DEFINES $mt"; bad=1; }
done
exit "$bad"
EOF
```

GREEN prints nothing and exits 0. A missing QuickFIX/J file prints `MISSING`, the failed tag check and
the failed `BN` control, so an absent input cannot read clean. Controls, each recorded with its exit
status: `TAIL="DS BN"` prints `QuickFIX/J DEFINES BN` and exits 1; `Q=/nonexistent` exits 1;
`TAIL="DS ZZ"` prints `MISSING v50sp2 ZZ` and `NOT ONE orchestra ZZ` and exits 1; `QF=` a scratch file
whose only message is `msgtype="BNX"` prints the failed `BN` control and exits 1; `QF=` a scratch file
holding `msgtype="BN"` and `msgtype="DSX"` exits 0 (`DSX` is not `DS`); `QC=` a scratch directory
holding only a copy of `spec/FIX50SP2.xml`, outside any git checkout, prints `QuickFIX-cpp checkout
is not v1.16.0` and exits 1.

## 1. The move is a move (W0)

The header holds the moved code between exactly one `// clang-format off` line and one
`// clang-format on` line, so no formatter rewrites it, and every leg is a plain, byte-exact `diff`:
- **header leg** (`MOVE:`, `CHECK_WITNESS:`): between the markers, exactly `T1(region)`, one empty
  line, the stated signature line, then `T2(method)`. `region`, `method`, `T1` and `T2` are the
  block's own functions over the merge-base copy of 095's TU; contracts W0 states them in words;
- **outside leg** (`OUTSIDE:`): outside the markers, only `#pragma once`, `#include`s, the namespace
  wrapper, comment lines and blank lines;
- **residual leg** (`RESIDUAL:`): 095's head TU, its forwarding method set aside, equals its base
  minus the region, minus the method and minus `using fixpp_test_support::parse_dict;`. Before the
  diff, the head side drops the header's `#include`, `using` lines naming
  `fixpp_test_support::tail_rows` and comment lines naming `tail_rows_witness.hpp`, and both sides
  drop blank lines (deleting the region leaves seams whose blank lines clang-format re-normalises). A
  deleted, edited or commented-out 095 line, and a moved function left behind in 095's TU, each
  print;
- **forwarder leg** (`FORWARDER:`): 095's `check_witness` method, byte for byte as stated.

Conditions, each with its re-check:
- `T1` reaches function heads only while every column-0 line of the region that starts with a letter
  is a `using`, `enum`, `struct` or `class` declaration, an access specifier or a function head.
  Re-check: run the block's `region` function on the base with `in`, then `grep -n '^[A-Za-z]'`. The
  header is built by the same `T1`, so the diff cannot see a wrong `T1`; the build does: a function
  left without `inline` is defined in two TUs of one binary (095's and 096's v50sp2 TU), and the link
  fails with a multiple definition.
- No lint rewraps or flags the region: pre-commit's clang-format honours the markers; clang-tidy's
  `HeaderFilterRegex` does not match `tests/support/`; no workflow runs clang-format or a line-length
  check. Re-check: `grep -n clang-format .pre-commit-config.yaml`, `grep -n HeaderFilterRegex
  .clang-tidy`, `grep -rli clang-format .github/workflows`.
- `using fixpp_test_support::parse_dict;` goes because only moved code uses it; if it were kept,
  clang-tidy's `misc-unused-using-decls` would fail it under `WarningsAsErrors`. Re-check which of
  095's `using fixpp_test_support::…` declarations lose their last user: for each name, count its
  uses in `region <base> out` with the method and the `using` lines removed (the forwarder uses none
  of them).
- The base is the merge-base copy, so a branch that merges `main` past a change to 095's TU re-takes
  it by construction.
- The header is a new file, so every moved line is an added line for pre-commit's line-citation gate
  and for the parent's `check-comment-claims.py`, while W0 forbids changing one byte of it. Both pass
  on the region as it stands; re-check at P1 by running both over the header's commit (a citation or
  a result claim found in a moved comment is brought to the orchestrator, not edited).

```bash
bash <<'EOF'
set -o pipefail
H=${H:-tests/support/tail_rows_witness.hpp}
T095=${T095:-tests/session/test_095_tail_rows_roundtrip.cpp}
BASE=${TMPDIR:-/tmp}/096-095-base.cpp
python3 -c 'import subprocess as s, sys; b = s.check_output(["git", "merge-base", "origin/main", "HEAD"], text=True).strip(); open(sys.argv[1], "wb").write(s.check_output(["git", "show", b + ":tests/session/test_095_tail_rows_roundtrip.cpp"]))' "$BASE" || { echo "NO BASE"; exit 1; }
bad=0
test -s "$BASE" || { echo "NO BASE"; exit 1; }
# R-2's moved region of 095's TU ("in") or the rest of it ("out")
region() { awk -v want="$2" '/^using IndexView/{p=1} /^\/\/ .*The tail table/{p=0}
  /^std::string drop_hyphen\(/{p=1} /^\/\/ .*Fixture:/{p=0}
  { r = p || /^using Intent =/; if ((want == "in") == r) print }' "$1"; }
# the fixture's check_witness method, signature line to closing brace, or everything but it
method()     { awk '/^    void check_witness\(/{m=1} m; m && /^    }$/{exit}' "${1:--}"; }
not_method() { awk '/^    void check_witness\(/{m=1} !m; m && /^    }$/{m=0}' "${1:--}"; }
# W0's transforms: T1 = `inline ` before every column-0 line that starts a function definition;
# T2 = the method body (signature dropped), dedented 4 columns, the expected_mt statement
# deleted, the three substitutions applied
T1() { sed -E '/^(using|enum|struct|class) |^[a-z]+:$/!s/^([A-Za-z])/inline \1/'; }
T2() { sed -e '1d' -e 's/^    //' -e '/^    std::string const expected_mt =$/{N;d;}' \
  -e 's/\btv()/tv/g; s/\bwalk()/walk/g; s/&read_arena_/read_arena/g'; }
SIG='inline void check_witness(WalkMap const& walk, fixpp::dict::table_view const& tv, std::pmr::memory_resource* read_arena, std::string_view expected_mt, std::string const& body, Intent const& intent) {'
test -n "$(region "$BASE" in)" -a -n "$(method "$BASE")" || { echo "BASE MARKERS DID NOT MATCH"; bad=1; }
# header leg: exactly one off/on pair; between them T1(region), one empty line, SIG, T2(method)
test "$(grep -cx '// clang-format off' "$H")" = 1 -a "$(grep -cx '// clang-format on' "$H")" = 1 \
  || { echo "MARKERS: not exactly one clang-format off/on pair in $H"; bad=1; }
inside() { sed -n '\#^// clang-format off$#,\#^// clang-format on$#p' "$H" | sed '1d;$d'; }
out=$(diff <(region "$BASE" in | T1; echo) <(inside | awk '/^inline void check_witness\(/{exit} 1')) \
  || { printf '%s\n' "$out" | sed 's/^/MOVE: /'; bad=1; }
out=$(diff <(printf '%s\n' "$SIG"; method "$BASE" | T2) <(inside | awk '/^inline void check_witness\(/{p=1} p')) \
  || { printf '%s\n' "$out" | sed 's/^/CHECK_WITNESS: /'; bad=1; }
# outside the markers: only #pragma once, #include, the namespace wrapper, comments, blank lines
out=$(sed '\#^// clang-format off$#,\#^// clang-format on$#d' "$H" | grep -vE '^(#pragma once|#include [<"][^>"]*[>"]|namespace fixpp_test_support::tail_rows \{|\}  // namespace fixpp_test_support::tail_rows|//.*|)$')
test -z "$out" || { printf '%s\n' "$out" | sed 's/^/OUTSIDE: /'; bad=1; }
# residual leg: 095's head TU = its base minus the region, minus `using fixpp_test_support::parse_dict;`
# (its only users move), plus the header's #include, using lines of fixpp_test_support::tail_rows
# and comment lines naming tail_rows_witness.hpp; blank lines dropped on both sides
out=$(diff <(region "$BASE" out | not_method | grep -vxF 'using fixpp_test_support::parse_dict;' | grep -v '^$') \
  <(not_method "$T095" | grep -vxF '#include "support/tail_rows_witness.hpp"' \
     | grep -vE '^using (namespace fixpp_test_support::tail_rows|fixpp_test_support::tail_rows::[A-Za-z_]+);$' \
     | grep -vE '^[[:space:]]*//.*tail_rows_witness\.hpp' | grep -v '^$')) \
  || { printf '%s\n' "$out" | sed 's/^/RESIDUAL: /'; bad=1; }
# the forwarding method, exactly
out=$(diff - <(method "$T095") <<'FWD'
    void check_witness(std::string const& body, Intent const& intent) {
        std::string const test_name =
            ::testing::UnitTest::GetInstance()->current_test_info()->name();
        std::string const expected_mt =
            msgtype_for_generated_name(doc(), split_test_name(test_name).second);
        fixpp_test_support::tail_rows::check_witness(walk(), tv(), &read_arena_, expected_mt, body,
                                                     intent);
    }
FWD
) || { printf '%s\n' "$out" | sed 's/^/FORWARDER: /'; bad=1; }
exit "$bad"
EOF
```

GREEN prints nothing and exits 0. Controls, each on scratch copies passed as `H=…` / `T095=…`, each
recorded with what it printed and its exit status:
- **negative:** a header built by the stated transforms around the merge-base region, the whole file
  then run through `clang-format --style=file:.clang-format --assume-filename=x.hpp`, and a 095 TU
  built as the residual leg states, then run through clang-format → prints nothing, exits 0;
- **whitespace inside a literal:** `" at "` → `"  at "` in `required_members_missing`'s status
  return → `MOVE:`, exit 1; the same in a `check_witness` literal (`"W4 value mismatch at "`) →
  `CHECK_WITNESS:`, exit 1;
- **inserted statement / reordered functions:** a statement added to a moved body, or `join` and
  `join_tags` swapped → `MOVE:`, exit 1;
- **deleted assertion in 095's TU:** `EXPECT_EQ(it->second.element, "NoQuoteEntries") << "35=i";`
  deleted, or turned into a comment → `RESIDUAL:`, exit 1;
- **left-behind copy:** a copy of `join` kept in 095's TU → `RESIDUAL:`, exit 1; the `parse_dict`
  using-declaration kept → `RESIDUAL:`, exit 1;
- **forwarder:** one token changed in the forwarding call → `FORWARDER:`, exit 1;
- **outside / markers:** a function appended after `// clang-format on` → `OUTSIDE:`, exit 1; a
  second `// clang-format off`, or none → `MARKERS:`, exit 1.

### 1b. The vlatest TU's copy of 095's deficient frames

`TailRows096LatestCensus.KnownPositive_DeficientFrames` copies 095's frame block byte for byte, from
`std::string const e_head =` to the `};` that closes `frames`, and changes only the `.expected`
lists, which it re-derives from the Orchestra file (data-model §B.2). The copy sits between
`// clang-format off` / `// clang-format on` lines in the vlatest TU, so a longer expected list
cannot rewrap a frame body.

```bash
bash <<'EOF'
set -o pipefail
TV=${TV:-tests/session/test_096_tail_rows_roundtrip_vlatest.cpp}
BASE=${TMPDIR:-/tmp}/096-095-base.cpp
python3 -c 'import subprocess as s, sys; b = s.check_output(["git", "merge-base", "origin/main", "HEAD"], text=True).strip(); open(sys.argv[1], "wb").write(s.check_output(["git", "show", b + ":tests/session/test_095_tail_rows_roundtrip.cpp"]))' "$BASE" || { echo "NO BASE"; exit 1; }
bad=0
# the deficient-frame block: from `std::string const e_head =` to the `};` closing `frames`,
# every `.expected = {…}` emptied (each tier asserts its own lists, data-model §B.2)
frames() { awk '/^    std::string const e_head =$/{p=1} p; p && /^    };$/{exit}' "$1" \
  | sed -E 's/\.expected = \{[^}]*\}/.expected = {}/'; }
test "$(grep -cx '    std::string const e_head =' "$BASE")" = 1 -a "$(grep -cx '    std::string const e_head =' "$TV")" = 1 \
  || { echo "FRAMES: not exactly one e_head block in each TU"; bad=1; }
out=$(diff <(frames "$BASE") <(frames "$TV")) || { printf '%s\n' "$out" | sed 's/^/FRAMES: /'; bad=1; }
exit "$bad"
EOF
```

GREEN prints nothing and exits 0. Controls: a scratch TU holding the block with one expected list
changed → exit 0; the same with `"11=O0\x01"` → `"11=O9\x01"` in one body → `FRAMES:` lines, exit 1;
a TU without the block → `FRAMES: not exactly one e_head block in each TU`, exit 1.

## 2. Witnesses, censuses, differential (US1, US2)

```bash
bash <<'EOF'
set -o pipefail
B=${B:-build/linux-clang-debug/bin}
bad=0
cmake --build build/linux-clang-debug --target test_077_allversions_builder_roundtrip test_077_allversions_builder_roundtrip_vlatest || bad=1
$B/test_077_allversions_builder_roundtrip --gtest_filter='TailRows096*:TailRows095*:AllVersionsRoundtrip077V50SP2.*' || bad=1
$B/test_077_allversions_builder_roundtrip_vlatest --gtest_filter='TailRows096*:AllVersionsRoundtrip077Vlatest.*' || bad=1
# every witness of data-model §A is registered under its exact name, in each suite (a deleted or
# renamed witness fails here; the census tests hold the converse)
full() { "$1" --gtest_list_tests | awk '/^[^ ]/{s=$1} /^  /{print s $1}'; }
full $B/test_077_allversions_builder_roundtrip > "${TMPDIR:-/tmp}/096-v50.lst" || { echo "LIST: v50sp2 binary's --gtest_list_tests failed"; bad=1; }
full $B/test_077_allversions_builder_roundtrip_vlatest > "${TMPDIR:-/tmp}/096-vl.lst" || { echo "LIST: vlatest binary's --gtest_list_tests failed"; bad=1; }
while read -r id mt name; do
  r=${id/-/}
  grep -qxF "TailRows096.${r}_${name}" "${TMPDIR:-/tmp}/096-v50.lst" || { echo "UNLISTED TailRows096.${r}_${name}"; bad=1; }
  grep -qxF "TailRows096Latest.${r}_${name}" "${TMPDIR:-/tmp}/096-vl.lst" || { echo "UNLISTED TailRows096Latest.${r}_${name}"; bad=1; }
done <<'ROWS'
A-035 DS CrossRequest
A-036 DT CrossRequestAck
A-037 DJ MassOrder
A-038 DK MassOrderAck
A-039 DC TradeMatchReport
A-040 DR MarketDataReport
A-041 DO MarketDataStatisticsRequest
A-042 DP MarketDataStatisticsReport
A-043 CF PartyDetailsListRequest
A-044 CG PartyDetailsListReport
A-045 CK PartyDetailsListUpdateReport
A-046 CX PartyDetailsDefinitionRequest
A-047 CY PartyDetailsDefinitionRequestAck
A-048 CU PartyEntitlementsRequest
A-049 CV PartyEntitlementsReport
A-050 CZ PartyEntitlementsUpdateReport
A-051 DA PartyEntitlementsDefinitionRequest
A-052 DB PartyEntitlementsDefinitionRequestAck
A-053 CL PartyRiskLimitsRequest
A-054 CM PartyRiskLimitsReport
A-055 CR PartyRiskLimitsUpdateReport
A-056 CS PartyRiskLimitsDefinitionRequest
A-057 CT PartyRiskLimitsDefinitionRequestAck
A-058 DE PartyRiskLimitsReportAck
A-059 DH PartyActionRequest
A-060 DI PartyActionReport
A-061 DF PartyRiskLimitCheckRequest
A-062 DG PartyRiskLimitCheckRequestAck
A-063 CN SecurityMassStatusRequest
A-064 CO SecurityMassStatus
A-065 CW QuoteAck
ROWS
grep -qxF 'TailRows096Differential.TailMsgTypesAgree' "${TMPDIR:-/tmp}/096-vl.lst" || { echo "UNLISTED TailRows096Differential.TailMsgTypesAgree"; bad=1; }
ctest --test-dir build/linux-clang-debug -L 096 --output-on-failure || bad=1
exit "$bad"
EOF
```

`B=` may point at another bin directory (re-check where the binaries land:
`find build/linux-clang-debug -name 'test_077_allversions_builder_roundtrip*' -type f -executable`).
The name loop matches each witness as a whole line, so `CrossRequest` cannot match `CrossRequestAck`.
Control for the loop: with one witness absent from `--gtest_list_tests` (a scratch rename, or a stub
binary that omits it) the block prints `UNLISTED …` and exits 1. Control for `pipefail`: with `B=` a
stub binary whose `--gtest_list_tests` prints the full list and exits 1 (stub `cmake`/`ctest` first
on `PATH`), the block prints `LIST:` and exits 1.

## 3. Non-vacuity (W6)

Each arm of contracts W6: apply the scratch mutant, rebuild the one binary, run the arm's filter, copy
the RED message into the evidence file, revert, `git diff --exit-code` the touched file. Mutants G,
F, I, J may share one rebuild per suite; O gets its own.

## 4. Label set

```bash
bash <<'EOF'
set -o pipefail
bad=0
out=$(ctest --test-dir build/linux-clang-debug -N -L 096 | grep 'Test *#' | sed 's/.*: //' | sort \
  | diff - specs/096-fix-latest-tail-evidence/expected-ctest-096.txt) || { printf '%s\n' "${out:-ctest -N or its filter failed}" | sed 's/^/LABEL: /'; bad=1; }
exit "$bad"
EOF
```

Control: with one of the two binaries missing from the label set, the block prints a `LABEL:` line and
exits 1. Control for `pipefail`: a stub `ctest` first on `PATH` whose `-N` prints the expected list
and exits 1 → the block exits 1.

## 5. Records (US3, SC-004, SC-005)

### 5a. The records checker (FR-010..FR-014)

One script checks every record family, each against its merge-base copy with the stated edit
applied, so a wrong cell, a missing comment or an extra change prints:
- **A** — each tail row's Spec ref against `[const §VI.2]` and the Orchestra file (data-model §E's
  rules: registry, application-message and category legs);
- **B** — the catalogue: the merge-base file with exactly the 31 rows' FR-011 cells replaced (version
  `5.0SP2, FIX Latest`, the Spec ref of data-model §E's table, Status `done`, `/specify`, `#$PR`, the
  Tests template, the Verified S0 + S1 with `<MT>` substituted), every other line unchanged;
- **C** — the constitution, the whole file: the merge-base file with
  - the v4.2 Sync Impact Report inserted immediately above the v4.0 → v4.1 block: its fixed lines
    byte for byte as data-model §F.3's template, its opening and review spans located by their
    anchors, and each span's lines fullmatched against §F.3's slot patterns for the state the
    header names;
  - the Status line fullmatching the same state's §F.4 template, followed by one space and the
    merge-base Status text after `> **Status:** ` (the "Prior:" remainder); under RATIFIED the
    Status line's two dates and the opening and review spans' sign-off date are one date, and
    `{PR}` is `$PR`;
  - §XVIII.2's v1.2 bullet equal to its merge-base text with `(new MsgTypes A-035..A-065 +
    EP-level` → `(EP-level` and data-model §F.1's annotation appended;
  - §XVIII.7's line equal to its merge-base text with §F.2's sentence inserted after "A-033 and C-003
    on it." (all 31 row IDs, `feature 096`, both namespaces);
  - every other line equal to the merge-base line. A literal `{OPENING}`, `{REVIEW}`, `{DATE}` or
    `{PR}` left in the file is an unfilled slot and prints. The templates are read from
    data-model.md's §F.3 and §F.4 fenced blocks, one per state, so the checker holds no copy of them;
- **D** — the coverage index: the merge-base file with the 31 gap notes and the SUPERSEDED comment
  appended to each of the four sites (the scope line, the 074 link paragraph, the FIX-Latest
  **Conclusion** line, the Gap Registry row), nothing else;
- **E** — the live B&L: the L-069-1 line equals its merge-base text plus data-model §E's widening
  (four namespaces, fixpp#637); every other merge-base line unchanged; lines past the merge-base
  length are the new `B-096-`/`L-096-` section, read at Gate B;
- **F** — Verified S1's "Live: no cell carries 35=\<MT\>": no file under `tests/interop/` carries a
  tail message in any form a cell uses: `35=<MT>`; `msg_type` followed by `<MT>` double-quoted,
  single-quoted or bare (the scripts write `msg_type: "<MT>"`, `witness_evidence.yaml` writes
  `msg_type: D` or `msg_type: '8'`); or the typed builder `build_<Name>`. Row 2's live cell is each
  form's positive: `BN` for the first two, `build_ExecutionAck` for the third.

`CAT`, `CON`, `COV`, `BL` and `INTEROP` may point the head side at scratch copies; `PR` is required
and must be a PR number (`[1-9][0-9]*`).
The checker assumes all 31 rows flip. Under FR-012's no-flip path, the scoped Gate A round that
changes the §XVIII.2 and §XVIII.7 after-texts, the Sync Impact Report and the Status line also narrows the checker's `ROWS` (and §2's row list, and data-model
§F.3/§F.4's row lists, which family C reads) to the rows that flip, and records that edit. Every
other surface that carries the full row set or its count is narrowed in the same edit; find them by
re-derivation, never from a remembered list:
`grep -n -e 'A-035' -e 'A-065' -e '\b31\b' -e '\b35 65\b' specs/096-fix-latest-tail-evidence/*.md specs/096-fix-latest-tail-evidence/contracts/*.md tests/session/test_096_tail_rows_roundtrip*.cpp`
— read each hit's whole block: a MsgType-only list in that block carries no hit line of its own.
`SENT7` and `IDS` are built from `ROWS` and narrow with it; the tests follow FR-012's owner ruling.

```bash
PR=<number> python3 - <<'PY'
import difflib, os, pathlib, re, subprocess, sys

E = os.environ.get
# the tracked paths: the base copies are read from them; CAT/CON/COV/BL may point the head side at scratch copies
TCAT, TCON, TCOV, TBL = "spec/feature-catalogue.md", ".specify/constitution.md", "spec/coverage-index.md", "spec/behaviors-and-limitations.md"
CAT, CON, COV, BL = E("CAT", TCAT), E("CON", TCON), E("COV", TCOV), E("BL", TBL)
INTEROP, ORCH = E("INTEROP", "tests/interop"), "dictionaries/orchestra/OrchestraFIXLatest.xml"
PR = E("PR") or sys.exit("set PR to the feature PR's number")
re.fullmatch(r"[1-9][0-9]*", PR) or sys.exit(f"PR must be a PR number, not {PR!r}")
BASEREV = E("BASEREV") or subprocess.check_output(["git", "merge-base", "origin/main", "HEAD"], text=True).strip()
bad = 0

def fail(family, msg):
    global bad
    bad = 1
    print(f"{family}: {msg}")

def base(path):  # the merge-base copy of a tracked file (Python: the RTK hook rewrites `git show $r:$p`)
    return subprocess.check_output(["git", "show", f"{BASEREV}:{path}"], text=True).splitlines()

def head(path):
    return pathlib.Path(path).read_text().splitlines()

def same(family, want, got):  # exact line-for-line equality, the first differences printed
    if want != got:
        for line in list(difflib.unified_diff(want, got, "expected", "actual", n=0, lineterm=""))[:12]:
            fail(family, line[:220])

# data-model §A, and §E's Spec-ref "After" column (None: A-037/A-038 stay byte-unchanged)
ROWS = """A-035 DS CrossRequest|A-036 DT CrossRequestAck|A-037 DJ MassOrder|A-038 DK MassOrderAck|
A-039 DC TradeMatchReport|A-040 DR MarketDataReport|A-041 DO MarketDataStatisticsRequest|
A-042 DP MarketDataStatisticsReport|A-043 CF PartyDetailsListRequest|A-044 CG PartyDetailsListReport|
A-045 CK PartyDetailsListUpdateReport|A-046 CX PartyDetailsDefinitionRequest|
A-047 CY PartyDetailsDefinitionRequestAck|A-048 CU PartyEntitlementsRequest|
A-049 CV PartyEntitlementsReport|A-050 CZ PartyEntitlementsUpdateReport|
A-051 DA PartyEntitlementsDefinitionRequest|A-052 DB PartyEntitlementsDefinitionRequestAck|
A-053 CL PartyRiskLimitsRequest|A-054 CM PartyRiskLimitsReport|A-055 CR PartyRiskLimitsUpdateReport|
A-056 CS PartyRiskLimitsDefinitionRequest|A-057 CT PartyRiskLimitsDefinitionRequestAck|
A-058 DE PartyRiskLimitsReportAck|A-059 DH PartyActionRequest|A-060 DI PartyActionReport|
A-061 DF PartyRiskLimitCheckRequest|A-062 DG PartyRiskLimitCheckRequestAck|
A-063 CN SecurityMassStatusRequest|A-064 CO SecurityMassStatus|A-065 CW QuoteAck"""
ROWS = [tuple(r.split()) for r in ROWS.replace("\n", "").split("|")]
PT = "[FIX-Latest] Pre Trade — "
AFTER = {"DS": PT + "Indication", "DT": PT + "Indication", "DJ": None, "DK": None,
         "DC": "[FIX-Latest] Post Trade — Trade Capture", "DR": PT + "Market Data",
         "DO": PT + "Market Data", "DP": PT + "Market Data", "DH": PT + "Parties Action",
         "DI": PT + "Parties Action", "DF": PT + "Parties Action", "DG": PT + "Parties Action",
         "CN": PT + "Securities Reference Data", "CO": PT + "Securities Reference Data",
         "CW": PT + "Quotation Negotiation"}
for mt in "CF CG CK CX CY CU CV CZ DA DB CL CM CR CS CT DE".split():
    AFTER[mt] = PT + "Parties Reference Data"

S0 = ("offline bar (owner 2026-10-21) on both typed tiers (owner 2026-10-28): typed builder → wire → "
      "independent dict-driven parse → bidirectional per-field + per-group-instance readback, every "
      "closure-required field and group set; fixpp::v50sp2 checked against a runtime walk of "
      "FIX50SP2.xml, fixpp::vlatest against a runtime walk of OrchestraFIXLatest.xml, and the two walks "
      "agree for 35=<MT> (TailRows096Differential).")
S1 = ("5.0SP2 here is the vendored FIX50SP2.xml (QuickFIX-cpp v1.16.0's file, which carries the 5.0SP2 "
      "Extension Pack that added 35=<MT>); QuickFIX/J 3.0.1's base edition does not define it. Live: no "
      "cell carries 35=<MT>; the typed tail's one live FIXT.1.1 scenario is A-018's (B-095-1). "
      "`validate_<Msg>` checks presence, not enum domains (L-069-1, fixpp#637). FIX Latest goes on the "
      "wire as ApplVerID 9 with no ApplExtID(1156) (L-074-1, REMAINING-WORK row 4b).")
TESTS = ("`tests/session/test_096_tail_rows_roundtrip.cpp`: `TailRows096.<R>_<N>`; "
         "`tests/session/test_096_tail_rows_roundtrip_vlatest.cpp`: `TailRows096Latest.<R>_<N>`, "
         "`TailRows096Differential.TailMsgTypesAgree`")
GAP = "096 witnesses: `TailRows096.<R>_<N>`, `TailRows096Latest.<R>_<N>`"
SUPERSEDED = ("<!-- SUPERSEDED (096-fix-latest-tail-evidence, constitution v4.2): the 31 MsgTypes of "
              "A-035–A-065 are also application messages of the vendored dictionaries/FIX50SP2.xml (5.0SP2 "
              "Extension Packs; absent from the base edition), and their catalogue rows are done, closed on "
              "fixpp::v50sp2 and fixpp::vlatest; the text before this comment is history. -->")
IDS = ", ".join(r[0] for r in ROWS[:-1]) + " and " + ROWS[-1][0]
ANN = ("*(Amended v4.2, feature 096: the rows A-035..A-065 leave this milestone. Their MsgTypes are "
       "application messages of the vendored `FIX50SP2.xml`, which carries the FIX 5.0SP2 Extension Packs "
       "that added them, as well as of the vendored FIX Orchestra file, and feature 096 closes the rows "
       "in v1.0 (§7).)*")
SENT7 = (f"**The rows {IDS}** each name one MsgType that is an application message of the vendored "
         "`FIX50SP2.xml`, which carries the FIX 5.0SP2 Extension Packs that added it, and of the vendored "
         "FIX Orchestra file; feature 096 closes these catalogue rows on both `fixpp::v50sp2` and "
         "`fixpp::vlatest`.")
WIDEN = ("**Widened by 096-fix-latest-tail-evidence (2026-10-28):** the same holds for every builder-bearing "
         "namespace (`fixpp::v42`, `fixpp::v44`, `fixpp::v50sp2`, `fixpp::vlatest`): one emitter "
         "(`tools/codegen/fixpp-codegen/emit_builders.cpp`) writes every `validate_<Msg>`, and its generic "
         "walk (`fixpp::wire::validate_required`, `include/fixpp/wire/builder_validate.hpp`) iterates only "
         "`writer_traits<T>::required_checks` and `writer_traits<T>::group_checks`, so it checks required "
         "presence and group counts and no enum domain; a value's type comes from the C++ member type of "
         "`<Msg>Args`, not from the validator. The rows A-035 … A-065 close with this disclosed. "
         "**Re-derive:** read the loops of `validate_required` in `include/fixpp/wire/builder_validate.hpp`, "
         "and the two arrays in any generated `*.validator.inl`. **Status: follow-up** (fixpp#637).")

def cells(line):
    f = line.split("|")
    return [c.strip() for c in f] if len(f) == 13 else None

# A — Spec refs against [const §VI.2] (data-model §E's rules), on the head catalogue
orch = pathlib.Path(ORCH).read_text()
words = lambda s: re.sub(r"(?<!^)([A-Z])", r" \1", s)
rows_by_id = {}
for line in head(CAT):
    c = cells(line)
    if c and re.fullmatch(r"[A-Z]+-[0-9]+", c[1]):
        rows_by_id.setdefault(c[1], []).append(c)
for rid, _, _ in ROWS:
    got = rows_by_id.get(rid, [])
    if len(got) != 1:
        fail("A", f"{rid}: not exactly one well-formed row"); continue
    ref, title = got[0][6], got[0][4]
    m = re.fullmatch(r"\[([A-Za-z0-9-]*)\] (.*)", ref)
    if not m or m.group(1) != "FIX-Latest":
        fail("A", f"{rid}: token of '{ref}' is not FIX-Latest"); continue
    for mt in re.findall(r"35=([A-Za-z0-9]+)", title) or [None]:
        msgs = re.findall(rf'<fixr:message [^>]*msgType="{mt}"[^>]*>', orch) if mt else []
        if len(msgs) != 1:
            fail("A", f"{rid} {mt}: not one Orchestra message"); continue
        cat = re.search(r'category="([^"]*)"', msgs[0])
        cat = cat.group(1) if cat else ""
        if not cat or cat == "Session":
            fail("A", f"{rid} {mt}: category '{cat}' is not an application category"); continue
        sec = re.search(rf'<fixr:category [^>]*name="{cat}"[^>]*section="([^"]*)"', orch)
        sec = sec.group(1) if sec else ""
        if m.group(2) not in (words(cat), f"{words(sec)} — {words(cat)}"):
            fail("A", f"{rid} {mt}: Orchestra '{sec} — {cat}', ref names '{m.group(2)}'")

# B — the catalogue: the base file with exactly the 31 rows' FR-011 cells replaced
want = []
for line in base(TCAT):
    c = cells(line)
    row = next((r for r in ROWS if c and c[1] == r[0]), None)
    if row:
        rid, mt, name = row
        r, ref = rid.replace("-", ""), AFTER[mt] or c[6]
        line = "| " + " | ".join([rid, c[2], c[3], c[4], "5.0SP2, FIX Latest", ref, "done",
            "096-fix-latest-tail-evidence", "#" + PR, TESTS.replace("<R>", r).replace("<N>", name),
            S0.replace("<MT>", mt) + " " + S1.replace("<MT>", mt)]) + " |"
    want.append(line)
same("B", want, head(CAT))

# C — the constitution, the whole file: the base with the v4.2 Sync Impact Report inserted above the
#     v4.0 → v4.1 block, the Status line in its state's template, §XVIII.2's and §XVIII.7's lines edited
#     (data-model §F); the templates are data-model's §F.3/§F.4 fenced blocks, one per state
def one(lines, pat, fam, what):
    hits = [i for i, l in enumerate(lines) if re.match(pat, l)]
    if len(hits) != 1:
        fail(fam, f"{what}: {len(hits)} lines match"); return None
    return hits[0]
DM = "specs/096-fix-latest-tail-evidence/data-model.md"
dm = pathlib.Path(DM).read_text()
def fenced(sec, state):  # the one fenced block after the `**<state>**` line of data-model's `### <sec>`
    s = re.search(rf"^### {re.escape(sec)} .*?(?=^### |\Z)", dm, re.S | re.M)
    got = re.findall(rf"^\*\*{state}\*\*[^\n]*\n(?:[^\n]*\n)*?```\n(.*?)\n```$", s.group(0) if s else "", re.S | re.M)
    if len(got) != 1 or not got[0].strip():
        sys.exit(f"{DM} §{sec}: not exactly one {state} template")
    return got[0].splitlines()
HDR = "Sync Impact Report — v4.1 → v4.2 (2026-10-28) — "
SIR = {s: fenced("F.3", s) for s in ("PENDING", "RATIFIED")}
def fixed(tpl):  # a §F.3 template's fixed parts: the lines before the opening span, the middle, the tail
    ib = [i for i, l in enumerate(tpl) if l.startswith("  Bump:")]
    ir = [i for i, l in enumerate(tpl) if l.startswith("  Review and sign-off:")]
    if len(ib) != 1 or len(ir) != 1: sys.exit(f"{DM} §F.3: a template lacks one Bump / Review line")
    return tpl[:2], tpl[ib[0]:ir[0]], tpl[-1:]
FIX = {s: fixed(t) for s, t in SIR.items()}
if FIX["PENDING"][1:] != FIX["RATIFIED"][1:] or [l[:-len(s)] for s in FIX for l in FIX[s][0][1:]] != [HDR, HDR]:
    sys.exit(f"{DM} §F.3: the two templates differ outside their slots")
OPEN = {"PENDING": r"  Pending\. Article XX §2 sequence.*",
        "RATIFIED": r"  Ratified\. Article XX §2 sequence observed in order: .*\S"}
REVIEW, CONT = r"  Review and sign-off: .*\S", r"    \S.*"
ST = {s: fenced("F.4", s) for s in ("PENDING", "RATIFIED")}
if any(len(v) != 1 for v in ST.values()): sys.exit(f"{DM} §F.4: a Status template is not one line")
def status_rx(state, prior):  # the state's template, one space, the base remainder, as one regex
    rx = re.escape(ST[state][0]).replace(re.escape("{DATE}"), r"(?P<date>\d{4}-\d{2}-\d{2})", 1)
    rx = rx.replace(re.escape("{DATE}"), "(?P=date)").replace(re.escape("{REVIEW}"), r"(?!.*\[filled)\S.*\bGate A\b.*\bGate B\b.*")
    return rx.replace(re.escape("{PR}"), re.escape(PR)) + re.escape(" " + prior)

b, h = base(TCON), head(CON)
P2, P7 = r"   - \*\*v1\.2 — FIX Latest application messages\*\*", r"7\. \*\*Application-message codegen scope for v1\.0\.\*\*"
PS = r"> \*\*Status:\*\* "
want = list(b)
ib, ih = one(b, P2, "C", "base §XVIII.2 bullet"), one(h, P2, "C", "§XVIII.2 bullet")
if ib is not None:
    want[ib] = b[ib].replace("(new MsgTypes A-035..A-065 + EP-level", "(EP-level", 1) + " " + ANN
    if want[ib] == b[ib] + " " + ANN: fail("C", "base §XVIII.2 bullet lacks '(new MsgTypes A-035..A-065 + EP-level'")
    if ih is not None: same("C §XVIII.2", [want[ib]], [h[ih]])
ib, ih = one(b, P7, "C", "base §XVIII.7"), one(h, P7, "C", "§XVIII.7")
anchor = "A-033 and C-003 on it."
if ib is not None:
    if b[ib].count(anchor) != 1: fail("C", "base §XVIII.7 lacks its anchor sentence once")
    want[ib] = b[ib].replace(anchor, anchor + " " + SENT7)
    if ih is not None: same("C §XVIII.7", [want[ib]], [h[ih]])
# the state: the v4.2 header's last word, on the head's second line
state = h[1][len(HDR):] if len(h) > 1 and h[1].startswith(HDR) else None
if state not in SIR:
    fail("C", "line 2 is not a v4.1 → v4.2 Sync Impact Report header ending PENDING or RATIFIED"); state = None
date = None
ib, ih = one(b, PS, "C", "base Status line"), one(h, PS, "C", "Status line")
if ib is not None:
    m = re.fullmatch(status_rx(state, b[ib][len("> **Status:** "):]), h[ih]) if state and ih is not None else None
    if m and "{REVIEW}" not in h[ih]:
        want[ib], date = h[ih], m.groupdict().get("date")
    else:
        want[ib] = f"<the Status line: data-model §F.4's {state or 'PENDING/RATIFIED'} template + ' ' + the base remainder>"
        fail("C", f"the Status line does not fullmatch data-model §F.4's {state or 'PENDING/RATIFIED'} template + the base remainder")
# the v4.2 block: its fixed lines from the template, its two spans from the head once they fullmatch
block = []
if state:
    fh, fm, ft = FIX[state]
    end = next((i for i in range(2, len(h)) if h[i] == ft[0]), len(h) - 1)
    k = next((i for i in range(2, end) if h[i] == fm[0]), end)
    r = next((i for i in range(k, end) if h[i].startswith("  Review and sign-off:")), end)
    span_o, span_r = h[2:k], h[r:end]
    ok_o = bool(span_o) and re.fullmatch(OPEN[state], span_o[0]) and all(re.fullmatch(CONT, l) for l in span_o[1:])
    ok_r = bool(span_r) and re.fullmatch(REVIEW, span_r[0]) and all(re.fullmatch(CONT, l) for l in span_r[1:])
    if state == "RATIFIED" and ok_r and not span_r[-1].endswith(f" owner sign-off {date}."):
        ok_r = False
    if state == "RATIFIED":  # each span names, in order, Gate A, Gate B and the dated sign-off (data-model §F.3)
        ev = rf"(?s)(?!.*\[filled).*\bGate A\b.*\bGate B\b.*\bowner sign-off {date}\b.*"
        ok_o = ok_o and bool(date) and bool(re.fullmatch(ev, " ".join(span_o)))
        ok_r = ok_r and bool(date) and bool(re.fullmatch(ev, " ".join(span_r)))
    if any(s in l for l in span_o + span_r for s in ("{OPENING}", "{REVIEW}", "{DATE}")):
        ok_o = ok_r = False
    if not ok_o: fail("C", f"v4.2 report: the opening span does not fullmatch data-model §F.3's {state} slot")
    if not ok_r: fail("C", f"v4.2 report: the review span does not fullmatch data-model §F.3's {state} slot"
                           + (f" ending 'owner sign-off {date or '<the Status date>'}.'" if state == "RATIFIED" else ""))
    block = (fh + (span_o if ok_o else [f"<{state} opening span>"]) + fm
             + (span_r if ok_r else [f"<{state} review span>"]) + ft)
same("C", block + want, h)

# D — the coverage index: the base file with the 31 gap notes and the four site comments, nothing else
SITES = [r"Scope: confirm A-035–A-065 ", r"> \*\*074-orchestra-native-reader link ",
         r"\*\*Conclusion:\*\* All 31 FIX Latest new MsgTypes \(A-035–A-065\)",
         r"\| EP-level field additions to existing FIX 4\.x/5\.x messages "]
want, hits = [], [0] * len(SITES)
for line in base(TCOV):
    row = next((r for r in ROWS if line == f"| {r[1]} | {r[2]} | {r[0]} | — |"), None)
    if row:
        line = f"| {row[1]} | {row[2]} | {row[0]} | " + GAP.replace("<R>", row[0].replace("-", "")).replace("<N>", row[2]) + " |"
    for k, p in enumerate(SITES):
        if re.match(p, line):
            hits[k] += 1
            line += " " + SUPERSEDED
    want.append(line)
if hits != [1] * len(SITES): fail("D", f"base site matches {hits}, one each expected")
if sum(l.startswith("| 096 witnesses") or " | 096 witnesses: " in l for l in want) != len(ROWS):
    fail("D", "base gap-note rows did not all match")
same("D", want, head(COV))

# E — the live B&L: L-069-1's line gains the widening; the base's other lines unchanged;
#     lines past the base's length are the new B-096-/L-096- section (read at Gate B)
b, h = base(TBL), head(BL)
i = one(b, r"- \*\*L-069-1 — ", "E", "base L-069-1 bullet")
if i is not None:
    same("E", b[:i] + [b[i] + " " + WIDEN] + b[i + 1:], h[:len(b)])

# F — no interop cell carries a tail message, in any form a cell uses; row 2's BN / ExecutionAck is
#     each form's positive
forms = {"35=": lambda mt, name: rf"35={mt}(?![A-Za-z0-9])",
         "msg_type": lambda mt, name: rf"""msg_type\W{{1,4}}['"]?{mt}(?![A-Za-z0-9])""",
         "build_": lambda mt, name: rf"(?<![A-Za-z0-9_])build_{name}\b"}
texts = []
for p in pathlib.Path(INTEROP).rglob("*"):
    if p.is_file() and "__pycache__" not in p.parts:
        try: texts.append((str(p), p.read_text()))
        except UnicodeDecodeError: pass
for form, rx in forms.items():
    if not any(re.search(rx("BN", "ExecutionAck"), t) for _, t in texts):
        fail("F", f"positive control: no '{form}' match for BN / ExecutionAck under {INTEROP}")
    for _, mt, name in ROWS:
        for path, t in texts:
            if re.search(rx(mt, name), t): fail("F", f"{path} carries {mt} / {name} ({form} form)")
sys.exit(bad)
PY
```

GREEN prints nothing and exits 0. Controls, each recorded with what it printed and its exit status:
- **before the record edits** (the unedited tree): exit 1, with family A flagging every tail row
  except A-037 and A-038 and families B–E firing; family F reads clean;
- **after the edits, PENDING** (scratch copies holding every edit, the constitution with data-model
  §F.3's PENDING block on top and the §F.4 PENDING Status line): exit 0;
- **after the edits, at the RATIFIED flip** (the same, with §F.3's RATIFIED block and §F.4's RATIFIED
  Status line, every slot filled in v4.1's form: a multi-line opening span and a review span ending
  `owner sign-off <date>.`, each naming Gate A, Gate B and `owner sign-off <date>` in that order,
  that date at both places in the Status line, a `{REVIEW}` text naming Gate A then Gate B, and
  `$PR`): exit 0; and a RATIFIED copy whose opening span, review span and `{REVIEW}` are v4.1's
  own texts, read from its v4.0 → v4.1 report and Status line, with v4.1's sign-off date at both
  Status positions and `$PR` kept: exit 0;
- `PR=banana` and `PR=0`: each exits 1 before any family runs;
- **after the edits**, on scratch copies, one perturbation per run, each exiting 1 with its family's
  message: (A) A-065's token changed to `[FIX44]`, its Title given `35=A` (Orchestra's Logon is
  category `Session`), its ref written `Pre-Trade — Quotation Negotiation`; (B) one `35=DR` in
  A-040's Verified cell changed to `35=DS`, and a run with another `PR`; (C) `A-050, ` removed from
  §XVIII.7's sentence; the Status line left at v4.1; one line of the v4.2 report's fixed body
  deleted; the PENDING Status line cut after its date; an unrelated `MUST` → `MAY` (Article XII's
  `insecure_plain_tcp` bullet); in the RATIFIED copy, the Status line cut after its date; the whole
  v4.0 → v4.1 report deleted; the Status line written with `...` in place of the 31 row IDs; three
  RATIFIED copies whose slots carry no ratification content: the opening `Ratified. Article XX §2
  sequence observed in order: x`, the review `Review and sign-off: x owner sign-off <date>.` and
  `{REVIEW}` = `x`; the minimal flip, the PENDING opening with only `Pending. Article XX §2
  sequence:` swapped to `Ratified. Article XX §2 sequence observed in order:` and the review
  `[filled at each step]. owner sign-off <date>.`; an opening of `… observed in order: [filled at
  each step].`;
  (D) the Conclusion line's comment altered; (E) `` `fixpp::v42`, `` removed from the widening;
  (F) a scratch copy of `tests/interop/` with a file holding `msg_type: "CW"`, others with
  `msg_type: CW`, `msg_type: 'CW'`, `build_QuoteAck(` and `"35=DS\x01"`, and `INTEROP` pointed at
  an empty directory (each form's positive fails).

### 5b. The `[const §I.3]` census (SC-004)

The open count drops by 31 and only A-035 … A-065 change Status. A row whose cells hold a literal `|`
does not split; the census reads only rows that split into 13 fields, and the skipped count must be
equal on both sides.

```bash
bash <<'EOF'
set -o pipefail
HEADCAT=${HEADCAT:-spec/feature-catalogue.md}
W=${TMPDIR:-/tmp}
bad=0
census() {  # ID<TAB>Status of every OFFICIAL row that splits cleanly; the skipped count to $2
  awk -F'|' -v sk="$2" '/^\| *[A-Z]+-[0-9]+ *\|/ { if (NF!=13) {skip++; next}
    s=$3; gsub(/ /,"",s); if (s!="OFFICIAL") next
    id=$2; st=$8; gsub(/^ +| +$/,"",id); gsub(/^ +| +$/,"",st); print id "\t" st }
    END { print skip+0 > sk }' "$1"
}
python3 -c 'import subprocess as s, sys; b = s.check_output(["git", "merge-base", "origin/main", "HEAD"], text=True).strip(); open(sys.argv[1], "wb").write(s.check_output(["git", "show", b + ":spec/feature-catalogue.md"]))' "$W/096-base-cat.md" || { echo "NO BASE CATALOGUE"; exit 1; }
test -s "$W/096-base-cat.md" || { echo "NO BASE CATALOGUE"; exit 1; }
census "$W/096-base-cat.md" "$W/096-base.skip" > "$W/096-base.tsv"; census "$HEADCAT" "$W/096-head.skip" > "$W/096-head.tsv"
cmp -s "$W/096-base.skip" "$W/096-head.skip" || { echo "SKIPPED: base $(cat "$W/096-base.skip") head $(cat "$W/096-head.skip")"; bad=1; }
nopen() { awk -F'\t' '$2!="done" && $2 !~ /^dropped/' "$1" | wc -l; }
b=$(nopen "$W/096-base.tsv"); h=$(nopen "$W/096-head.tsv")
test "$h" -eq "$((b - 31))" || { echo "OPEN: base $b head $h (head must be base - 31)"; bad=1; }
changed=$(diff "$W/096-base.tsv" "$W/096-head.tsv" | grep '^>' | cut -f1 | sed 's/^> //' | sort | tr '\n' ' ')
want=$(for n in $(seq 35 65); do printf 'A-%03d ' "$n"; done)
test "$changed" = "$want" || { echo "CHANGED: '$changed'"; bad=1; }
exit "$bad"
EOF
```

Controls: on the unedited tree it prints `OPEN:` and `CHANGED:` and exits 1; with `HEADCAT` a scratch
copy of the edited catalogue in which one tail row is set back to `backlog`, it prints both and exits
1.

### 5c. No production change (SC-005)

```bash
bash <<'EOF'
set -o pipefail
bad=0
if [ -z "${R:-}" ]; then
  MB=$(git merge-base origin/main HEAD) && test -n "$MB" || { echo "NO MERGE-BASE"; exit 1; }
  R=$MB..HEAD
fi
out=$(git diff --name-only "$R" -- src include tools/codegen) || { echo "DIFF FAILED: $R"; bad=1; }
test -z "$out" || { printf 'PRODUCTION: %s\n' $out; bad=1; }
exit "$bad"
EOF
```

Control: `R=2de73f3f~1..2de73f3f` (PR #636's merge, which touched `src/` and `include/`) prints
`PRODUCTION:` lines and exits 1. A failed `git merge-base` prints `NO MERGE-BASE` and exits 1 rather
than diffing the empty range `..HEAD`.

## 6. Full build and ctest (`[const §XVII.7]`)

Last, at the final head (the resource gate in tasks.md Execution rules applies):

```bash
bash <<'EOF'
set -o pipefail
bad=0
cmake --build build/linux-clang-debug || bad=1
ctest --test-dir build/linux-clang-debug --output-on-failure || bad=1
exit "$bad"
EOF
```

Controls, with stubs first on `PATH`: `cmake` exiting 0 and `ctest` exiting 1 → the block exits 1;
`cmake` exiting 1 → the block exits 1.
