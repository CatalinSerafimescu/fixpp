#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# tools/test_dictionary_snapshot_exclusivity_gate.sh
#
# Positive/negative test for tools/check_dictionary_snapshot_exclusivity.sh.
# Proves the stateful comment stripper handles the lexer corpus, the clean tree
# stays green with printed liveness counts, and removing all five static_asserts
# in tests/dictionary/dictionary_snapshot_test.cpp goes red under three comment
# spellings.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
gate="${repo_root}/tools/check_dictionary_snapshot_exclusivity.sh"
a5tu="${repo_root}/tests/dictionary/dictionary_snapshot_test.cpp"

tmp="$(mktemp -d)"
backup="${tmp}/dictionary_snapshot_test.cpp.orig"
cp "$a5tu" "$backup"
trap 'cp "$backup" "$a5tu"; rm -rf "$tmp"' EXIT

rc=0

run_strip_case() {
  local case_id="$1"
  local expected="$2"
  local fixture="${tmp}/case_${case_id}.txt"
  shift 2
  printf '%s\n' "$@" > "$fixture"
  local actual
  actual="$(bash "$gate" --strip-comments "$fixture")"
  if [[ "$actual" == "$expected" ]]; then
    printf 'PASS row %s: %q\n' "$case_id" "$actual"
  else
    printf 'FAIL row %s: expected %q got %q\n' "$case_id" "$expected" "$actual" >&2
    rc=1
  fi
}

comment_out_static_asserts() {
  local style="$1"
  awk -v mode="$style" '
    BEGIN { in_block = 0; count = 0 }
    /^[[:space:]]*static_assert\(/ { in_block = 1 }
    {
      if (in_block) {
        if (mode == "line") {
          print "// " $0
        } else if (mode == "block-line") {
          print "/* " $0 " */"
        } else if (mode == "block-unstarred") {
          if ($0 ~ /^[[:space:]]*static_assert\(/) print "/*"
          print $0
          if ($0 ~ /^[[:space:]]*\);/) print "*/"
        }
        if ($0 ~ /\);[[:space:]]*$/) {
          in_block = 0
          count++
        }
        next
      }
      print
    }
    END {
      if (count != 5) {
        printf("expected to rewrite 5 static_assert blocks, rewrote %d\n", count) > "/dev/stderr"
        exit 1
      }
    }
  ' "$backup" > "$a5tu"
}

run_gate_expect() {
  local label="$1"
  local expected_rc="$2"
  local log="${tmp}/${label}.log"
  set +e
  bash "$gate" >"$log" 2>&1
  local gate_rc=$?
  set -e
  cat "$log"
  if [[ "$gate_rc" -ne "$expected_rc" ]]; then
    printf 'FAIL %s: expected exit %s got %s\n' "$label" "$expected_rc" "$gate_rc" >&2
    rc=1
  else
    printf 'PASS %s: exit %s\n' "$label" "$gate_rc"
  fi
}

run_red_case() {
  local style="$1"
  cp "$backup" "$a5tu"
  comment_out_static_asserts "$style"
  run_gate_expect "whole_script_${style}" 1
}

run_strip_case 1 "" "// snapshot_key"
run_strip_case 2 "" " * snapshot_key"
run_strip_case 3 "" "/* snapshot_key */"
run_strip_case 4 "" "/*" "snapshot_key" "*/"
run_strip_case 5 " snapshot_key" "/* comment */ snapshot_key"
run_strip_case 6 "int x; " "int x; /* snapshot_key"
run_strip_case 7 "snapshot_key; " "snapshot_key; // trailing"
run_strip_case 8 "const char* s = \"/* snapshot_key */\";" "const char* s = \"/* snapshot_key */\";"
run_strip_case 9 "const char* s = \"// snapshot_key\";" "const char* s = \"// snapshot_key\";"
run_strip_case 10 " code  snapshot_key" "/* a */ code /* b */ snapshot_key"
run_strip_case 11 "const char* s = R\"(/* snapshot_key */)\";" "const char* s = R\"(/* snapshot_key */)\";"

cp "$backup" "$a5tu"
run_gate_expect "whole_script_clean" 0
run_red_case line
run_red_case block-line
run_red_case block-unstarred

cp "$backup" "$a5tu"

if [[ "$rc" -eq 0 ]]; then
  echo "test_dictionary_snapshot_exclusivity_gate: OK"
else
  echo "test_dictionary_snapshot_exclusivity_gate: FAILED" >&2
fi
exit "$rc"
