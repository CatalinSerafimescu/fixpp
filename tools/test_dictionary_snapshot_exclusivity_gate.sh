#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# tools/test_dictionary_snapshot_exclusivity_gate.sh
#
# Positive/negative test for tools/check_dictionary_snapshot_exclusivity.sh.
# Requires the stateful comment stripper to handle the lexer corpus, the gate to
# exit 0 on a clean copy of the tree printing G1's liveness counts and G2's zero-match line,
# and the gate to exit 1 when either:
#   * every static_assert block in tests/dictionary/dictionary_snapshot_test.cpp that
#     carries the comment-out marker (MARKER below) is removed, under three comment
#     spellings (G1). The marker selects the blocks that name the key, so the count
#     follows the file: the rewrite must remove as many blocks as there are markers,
#     at least one (W1, `.specify/688-707-559-wire-headroom-token.md` §2.4);
#   * one of G2's enumerated spellings is seeded into the snapshot TU (fixpp#495
#     R-C, `.specify/495-493-486-dict-reify-copy.md` §6.4 / T-18). G1 runs first,
#     so a seeded case must show `G2 FAIL` in the log, not just a non-zero exit; or
#   * an extra key construction is seeded into the header or into the factory (W1
#     D-707-2): G1's filter admits the header's one constructor declaration and
#     nothing wider, so each seed must show `G1 FAIL`.
# The clean case also requires one production key construction, which shows that the
# header's user-provided default constructor declaration is filtered.
#
# The G2 seeds and the G1 mint seeds are ASSEMBLED FROM FRAGMENTS at run time: G2
# and G1's mint count scan tools/ and do not strip comments, so a literal seed
# spelled in this file would itself be a match on the clean tree.
#
# Every case runs the gate with `--root` on ONE temp copy of the directories it
# scans; seeds are written into that copy, never into the source tree. The clean
# case runs on the same copy, so it is also what shows the copy is a complete
# corpus: a copy the scanner cannot read fails G1 liveness there, and the RED
# cases' expected exit 1 would otherwise be met by that failure alone.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
gate="${repo_root}/tools/check_dictionary_snapshot_exclusivity.sh"
a5tu_rel="tests/dictionary/dictionary_snapshot_test.cpp"
factory_rel="src/dictionary/dictionary_snapshot.cpp"
a5tu_src="${repo_root}/${a5tu_rel}"
factory_src="${repo_root}/${factory_rel}"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
root="${tmp}/root"
mkdir "$root"
for d in src include bindings tools tests; do
  cp -R "${repo_root}/${d}" "${root}/${d}"
done
a5tu="${root}/${a5tu_rel}"
factory_tu="${root}/${factory_rel}"
hdr_rel="include/fixpp/dict/dictionary_snapshot.hpp"
hdr_src="${repo_root}/${hdr_rel}"
hdr_tu="${root}/${hdr_rel}"
MARKER='exclusivity-self-test: comment out'

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
  awk -v mode="$style" -v marker="$MARKER" '
    BEGIN { in_block = 0; count = 0; markers = 0; armed = 0 }
    index($0, marker) { markers++; armed = 1; print; next }
    armed && /^[[:space:]]*static_assert\(/ { in_block = 1; armed = 0 }
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
      if (markers < 1 || count != markers) {
        printf("expected to rewrite one static_assert block per marker (%d markers), rewrote %d\n", markers, count) > "/dev/stderr"
        exit 1
      }
    }
  ' "$a5tu_src" > "$a5tu"
}

run_gate_expect() {
  local label="$1"
  local expected_rc="$2"
  local log="${tmp}/${label}.log"
  set +e
  bash "$gate" --root "$root" >"$log" 2>&1
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

# Like run_gate_expect, and additionally requires `needle` (fixed string) in the log.
run_gate_expect_log() {
  local label="$1"
  local expected_rc="$2"
  local needle="$3"
  run_gate_expect "$label" "$expected_rc"
  if grep -qF -- "$needle" "${tmp}/${label}.log"; then
    printf 'PASS %s: log has %q\n' "$label" "$needle"
  else
    printf 'FAIL %s: log lacks %q\n' "$label" "$needle" >&2
    rc=1
  fi
}

# G2 seeds, from fragments (see the header).
g2_type='shared_ptr<const table_view>'
g2_seed_move="static auto g2_seed_move = std::${g2_type}(std::move(g2_seed_src));"
g2_seed_ident="static auto g2_seed_ident = std::${g2_type}(g2_seed_src, g2_seed_ptr);"

# G1 mint seeds, from fragments (see the header).
g1_key='snapshot''_key'
g1_seed_hdr="inline void g1_seed_hdr() { (void)detail::${g1_key}{}; }"
g1_seed_factory="void g1_seed_factory() { (void)detail::${g1_key}{}; }"
g1_seed_hdr_decl="    ${g1_key}() noexcept {}"

run_g1_mint_seed_case() {
  local label="$1"
  local src="$2"
  local dst="$3"
  local seed="$4"
  cp "$src" "$dst"
  printf '%s\n' "$seed" >> "$dst"
  run_gate_expect_log "$label" 1 "G1 FAIL"
  cp "$src" "$dst"
}

run_g2_seed_case() {
  local label="$1"
  local seed="$2"
  cp "$factory_src" "$factory_tu"
  printf '%s\n' "$seed" >> "$factory_tu"
  run_gate_expect_log "$label" 1 "G2 FAIL"
  cp "$factory_src" "$factory_tu"
}

run_red_case() {
  local style="$1"
  cp "$a5tu_src" "$a5tu"
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

cp "$a5tu_src" "$a5tu"
run_gate_expect_log "whole_script_clean" 0 "G2 matches of the enumerated spellings = 0"
run_gate_expect_log "whole_script_clean_one_mint" 0 "G1 production key constructions = 1"
run_g1_mint_seed_case "g1_seed_extra_mint_in_header" "$hdr_src" "$hdr_tu" "$g1_seed_hdr"
run_g1_mint_seed_case "g1_seed_extra_mint_in_factory" "$factory_src" "$factory_tu" "$g1_seed_factory"
run_g1_mint_seed_case "g1_seed_second_decl_form_in_header" "$hdr_src" "$hdr_tu" "$g1_seed_hdr_decl"
run_g2_seed_case "g2_seed_std_move" "$g2_seed_move"
run_g2_seed_case "g2_seed_identifier_comma" "$g2_seed_ident"
run_red_case line
run_red_case block-line
run_red_case block-unstarred

if [[ "$rc" -eq 0 ]]; then
  echo "test_dictionary_snapshot_exclusivity_gate: OK"
else
  echo "test_dictionary_snapshot_exclusivity_gate: FAILED" >&2
fi
exit "$rc"
