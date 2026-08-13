#!/usr/bin/env bash
# check_dictionary_snapshot_exclusivity.sh — fixpp#215 item 1 (Option C),
# `.specify/215-dictionary-view.md` §6 seam 7, v0.4.
#
# Two singularity claims are load-bearing for Option C's C1 closure and
# nothing else can pin them (not a static_assert — see the design doc):
#
#   G1 — `snapshot_key` (the passkey) is minted by exactly ONE production
#        function: fixpp::dict::make_dictionary_snapshot.
#   G2 — the aliasing shared_ptr<const table_view> construction happens at
#        exactly ONE production site: fixpp::dict::shared_dictionary_view.
#        AND both Session::open() and fixpp_session_open route through it.
#
# This is a source gate that runs NOWHERE in CI — it is invoked from
# `/speckit-verify` Step 1 (`[const §XVII.1]`), same as check_capi_freeze.sh
# et al. Every grep is guarded with `|| true`: under `set -e` + `pipefail` an
# unguarded grep that matches nothing aborts the WHOLE gate on its own
# PASSING branch (this project's recorded `[ cond ] && fail` trap, pipeline
# form) — see the design doc §6 seam 7 for the worked example that shipped
# broken this way on first draft.
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

HDR='include/fixpp/dict/dictionary_snapshot.hpp'
FACTORY='src/dictionary/dictionary_snapshot.cpp'
A5TU='tests/dictionary/dictionary_snapshot_test.cpp'

# `grep -c .` counts NON-EMPTY lines, so an empty capture counts 0 rather than 1.
n_of() { printf '%s' "${1-}" | { grep -c . || true; }; }
# Exact first-field match on a `path:line:text` hit list. awk, not
# `grep -F "$f:"`, because the latter also matches the path quoted inside a
# comment, and awk needs no regex escaping of the `/` and `.` in a path.
# awk exits 0, so no `|| true`.
in_file() { printf '%s\n' "${2-}" | awk -F: -v f="$1" '$1==f{n++} END{print n+0}'; }
# Drop FULL-LINE comments (`//...` or ` * ...` block-comment continuation,
# after leading whitespace) before a CALL/CONSTRUCTION census. A gate that
# asserts "X is CALLED" (not "X is NAMED") must not count a prose mention as
# the call — measured: the code's own prose sentence
# "shared_dictionary_view() is the sole production alias-formation site"
# satisfies `\<shared_dictionary_view[[:space:]]*\(` even after the REAL call
# is replaced by a copy, so the required-call assertion below stayed GREEN
# under exactly the mutation it exists to catch (case H, comment-shielded
# variant) until this filter was added. Does not handle a trailing same-line
# comment after real code, or a `/* */` block whose FIRST line carries code —
# neither occurs in this tree today; a stricter AST check would close that
# residual (mirrors G2's own documented spelling-coverage limitation below).
strip_comment_lines() { grep -vE '^[[:space:]]*(//|\*)' "$1" || true; }

# ── G1 — SOLE MINTER ──────────────────────────────────────────────────────────
# `snapshot_key` may be NAMED only where it is declared, defined, and asserted
# on. A file boundary alone is green for a second friend or a second minter
# added INSIDE an allowlisted file, so assertions (b) and (c) below count
# occurrences rather than trusting the boundary.
G1_ALLOW='^(include/fixpp/dict/dictionary_snapshot\.hpp|src/dictionary/dictionary_snapshot\.cpp|tests/dictionary/dictionary_snapshot_test\.cpp):'
# SELF-EXCLUSION: this gate script lives under tools/ (in scope) and its own
# prose/regex literals necessarily spell out "snapshot_key" — that is the
# instrument describing its own subject, not a production/test site naming
# the type, so it must not count as an "outside allowlist" hit. Excluded by
# path, not folded into G1_ALLOW: G1_ALLOW is the three-file design-doc
# allowlist and stays exactly that.
SELF="tools/$(basename "${BASH_SOURCE[0]}")"
g1_hits_raw=$(grep -rn 'snapshot_key' src/ include/ bindings/ tools/ tests/ || true)
g1_hits=$(printf '%s\n' "$g1_hits_raw" | { grep -v "^${SELF}:" || true; })
g1_bad=$(printf '%s\n' "$g1_hits" | { grep -vE "$G1_ALLOW" || true; })
g1_bad_n=$(n_of "$g1_bad")

# LIVENESS hits, CODE ONLY: same class of hole as the G2 required-call fix
# above, and measured to be a REAL false-green here (not merely theoretical)
# — deleting ALL FIVE A1-A5 static_asserts from $A5TU while leaving one
# prose comment mentioning "snapshot_key" left this file's liveness at 1 and
# the WHOLE gate green, because (unlike HDR's friend-count assertion, which
# independently re-derives from $HDR and would have caught a header-only
# version of this) no other assertion re-checks that $A5TU's assertions
# still exist. `path:line:` kept literal (a `:` cannot appear in these repo
# paths) so only the CONTENT after it is tested for a comment-line prefix.
g1_hits_code=$(printf '%s\n' "$g1_hits" | { grep -vE '^[^:]+:[0-9]+:[[:space:]]*(//|\*)' || true; })

# LIVENESS — PER ALLOWLISTED FILE, never a union total, and CODE ONLY, never
# a comment mention. A union bound (e.g. `>= 3`) is met by the header alone
# (it declares, comments on, and befriends around the key), so an entire
# assertion TU could be deleted with the gate green; a comment-only mention
# has the identical failure shape one level down (measured, see above).
for f in "$HDR" "$FACTORY" "$A5TU"; do
    n=$(in_file "$f" "$g1_hits_code")
    echo "G1 liveness: $f = $n"
    [ "$n" -ge 1 ] || { echo "G1 DEAD: no CODE reference to 'snapshot_key' in $f — file deleted, type renamed, scanner broken, or only a comment mention remains"; exit 1; }
done

# ASSERTION (a) — nothing outside the allowlist may name the key.
[ "$g1_bad_n" -eq 0 ] || { echo "G1 FAIL: $g1_bad_n site(s) outside the allowlist:"; echo "$g1_bad"; exit 1; }

# ASSERTION (b) — the friend list is a LIST OF ONE, and its one entry is the
# factory. Selector: `friend` at STATEMENT POSITION. A bare `\<friend\>` also
# matches the header's own prose ("a qualified friend declaration cannot
# introduce a name", "The friend list below IS the boundary"), which would
# FAIL a conforming tree (measured — 2 hits on this file's own header prose).
# ("befriends" is excluded by \<.)
g1_friend=$(grep -nE '^[[:space:]]*friend\>' "$HDR" || true)
g1_friend_n=$(n_of "$g1_friend")
g1_friend_fact_n=$(n_of "$(printf '%s\n' "$g1_friend" | { grep -E '\<make_dictionary_snapshot\>' || true; })")
echo "G1 friend decls in $HDR = $g1_friend_n (naming make_dictionary_snapshot: $g1_friend_fact_n)"
[ "$g1_friend_n" -eq 1 ] || { echo "G1 FAIL: $g1_friend_n friend declaration(s) in $HDR, expected exactly 1:"; echo "$g1_friend"; exit 1; }
[ "$g1_friend_fact_n" -eq 1 ] || { echo "G1 FAIL: the sole friend declaration does not name make_dictionary_snapshot:"; echo "$g1_friend"; exit 1; }

# ASSERTION (c) — exactly ONE production construction of a key, and it is the
# factory's. Scope excludes `tests/`: a test may NAME the key without minting
# one (A5 does exactly that). `snapshot_key() = default;` is a DECLARATION,
# not a construction, and is filtered.
G1_MINT='snapshot_key[[:space:]]*(\{[[:space:]]*\}|\([[:space:]]*\))'
g1_mint_raw=$(grep -rnE "$G1_MINT" src/ include/ bindings/ tools/ || true)
g1_mint=$(printf '%s\n' "$g1_mint_raw" | { grep -vE '=[[:space:]]*default' || true; })
g1_mint_n=$(n_of "$g1_mint")
echo "G1 production key constructions = $g1_mint_n"
[ "$g1_mint_n" -eq 1 ] || { echo "G1 FAIL: $g1_mint_n production snapshot_key construction(s), expected exactly 1:"; echo "$g1_mint"; exit 1; }
case "$g1_mint" in
    "$FACTORY":*) ;;
    *) echo "G1 FAIL: the sole key construction is not in $FACTORY:"; echo "$g1_mint"; exit 1 ;;
esac

# ── G2 — SOLE ALIAS-FORMER, AND THE TWO REQUIRED CALLS ────────────────────────
# A TWO-ARGUMENT shared_ptr<const table_view> construction IS the aliasing
# ctor. Two spellings are matched: `(std::move(...)` and `(<identifier>,`. The
# pattern's coverage is the LISTED SPELLINGS ONLY (see the design doc §6 seam
# 7 "east const" / "brace-init" / "deduced return" / "type alias" limitation —
# a clang-query/AST check would be strictly stronger; this exact-count grep is
# the minimum repair, not an exhaustive census).
G2='shared_ptr<[[:space:]]*const[^>]*table_view[[:space:]]*>[[:space:]]*\([[:space:]]*(std::move\(|[A-Za-z_][A-Za-z0-9_]*[[:space:]]*,)'
g2_hits=$(grep -rnE "$G2" src/ include/ bindings/ tools/ tests/ || true)
g2_all_n=$(n_of "$g2_hits")
g2_helper_n=$(in_file "$FACTORY" "$g2_hits")
g2_bad_n=$(( g2_all_n - g2_helper_n ))
echo "G2 alias-formation sites = $g2_all_n (in $FACTORY: $g2_helper_n, elsewhere: $g2_bad_n)"

# LIVENESS — the helper's OWN aliasing ctor must match, or the pattern is dead
# and the allowlist is filtering an empty set.
[ "$g2_all_n" -ge 1 ] || { echo "G2 DEAD: pattern matches nothing, not even the helper"; exit 1; }
# ASSERTION (a) — EXACTLY ONE aliasing-ctor expression in the whole tree. An
# occurrence count, not a file boundary: a second one inside the helper's own
# TU is a second alias-formation site and is caught here.
[ "$g2_all_n" -eq 1 ] || { echo "G2 FAIL: $g2_all_n aliasing-ctor expression(s), expected exactly 1:"; echo "$g2_hits"; exit 1; }
# ASSERTION (b) — and that one is the helper's.
[ "$g2_bad_n" -eq 0 ] || { echo "G2 FAIL: $g2_bad_n hand-rolled alias site(s):"; echo "$g2_hits"; exit 1; }

# ASSERTION (c) — the REQUIRED calls, the census G2 never had. Without this
# both production consumers can COPY instead of aliasing while (a), (b), seam
# 3 and seam 4 all stay green. `>= 1` per consumer, not `== 1`: the claim the
# design makes is "each consumer routes through the helper"; singularity is
# already carried by (a).
G2_CALL='\<shared_dictionary_view[[:space:]]*\('
for f in src/session/session.cpp src/capi/session.cpp; do
    n=$(n_of "$(strip_comment_lines "$f" | { grep -nE "$G2_CALL" || true; })")
    echo "G2 required call: $f = $n"
    [ "$n" -ge 1 ] || { echo "G2 FAIL: $f never calls shared_dictionary_view — the view is copied, not aliased (comment mentions do not count)"; exit 1; }
done

echo "PASS: dictionary_snapshot exclusivity gates (G1 sole minter, G2 sole alias-former + required calls)."
