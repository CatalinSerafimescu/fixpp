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
#
# This is a source gate mirrored by the Tier-1 workflow and recorded manually
# in the 215 `/speckit-verify` evidence doc; it is NOT wired through the same
# path as `check_capi_freeze.sh`. The text layer now runs a small stateful
# comment lexer: it strips `//...` and `/* ... */` comments across lines while
# preserving code before/after a comment and ignoring comment markers inside
# ordinary and raw string literals. It is still a text gate, not a C++ parser:
# this script counts literal spellings and the G2 pattern remains limited to
# the enumerated shared_ptr spellings below. Every grep is guarded with
# `|| true`: under `set -e` + `pipefail` an unguarded grep that matches nothing
# aborts the WHOLE gate on its own PASSING branch (this project's recorded
# `[ cond ] && fail` trap, pipeline form) — see the design doc §6 seam 7 for
# the worked example that shipped broken this way on first draft.
set -euo pipefail

# `grep -c .` counts NON-EMPTY lines, so an empty capture counts 0 rather than 1.
n_of() { printf '%s' "${1-}" | { grep -c . || true; }; }
# Exact first-field match on a `path:line:text` hit list. awk, not
# `grep -F "$f:"`, because the latter also matches the path quoted inside a
# comment, and awk needs no regex escaping of the `/` and `.` in a path.
# awk exits 0, so no `|| true`.
in_file() { printf '%s\n' "${2-}" | awk -F: -v f="$1" '$1==f{n++} END{print n+0}'; }
# Strip C++ line/block comments while preserving code, line count, and comment
# markers inside string/raw-string literals.
strip_comments_preserve_code() {
    awk '
        function starts_raw_string(text, pos,    j, delim, ch) {
            if (substr(text, pos, 2) != "R\"") return 0
            delim = ""
            for (j = pos + 2; j <= length(text); ++j) {
                ch = substr(text, j, 1)
                if (ch == "(") {
                    raw_end = ")" delim "\""
                    raw_len = length(raw_end)
                    return j
                }
                if (ch ~ /[[:space:]()\\]/) return 0
                delim = delim ch
            }
            return 0
        }
        {
            out = ""
            i = 1
            if (!in_block && $0 ~ /^[[:space:]]*\*([[:space:]].*)?$/) {
                print ""
                next
            }
            while (i <= length($0)) {
                c = substr($0, i, 1)
                n = (i < length($0) ? substr($0, i + 1, 1) : "")
                if (in_block) {
                    if (c == "*" && n == "/") {
                        in_block = 0
                        i += 2
                    } else {
                        i++
                    }
                    continue
                }
                if (in_raw) {
                    if (substr($0, i, raw_len) == raw_end) {
                        out = out raw_end
                        i += raw_len
                        in_raw = 0
                    } else {
                        out = out c
                        i++
                    }
                    continue
                }
                if (in_string) {
                    out = out c
                    if (escape) {
                        escape = 0
                    } else if (c == "\\") {
                        escape = 1
                    } else if (c == string_quote) {
                        in_string = 0
                    }
                    i++
                    continue
                }
                raw_start = starts_raw_string($0, i)
                if (raw_start) {
                    out = out substr($0, i, raw_start - i + 1)
                    i = raw_start + 1
                    in_raw = 1
                    continue
                }
                if (c == "/" && n == "/") {
                    break
                }
                if (c == "/" && n == "*") {
                    in_block = 1
                    i += 2
                    continue
                }
                if (c == "\"" || c == "'\''") {
                    in_string = 1
                    string_quote = c
                    escape = 0
                    out = out c
                    i++
                    continue
                }
                out = out c
                i++
            }
            print out
        }
    ' "$1"
}
# Keep only hits whose matched source line survives strip_comments_preserve_code().
# The input is the usual `path:line:text` grep format.
code_hits_only() {
    while IFS=: read -r file line rest; do
        [ -n "${file-}" ] || continue
        stripped_line=$(strip_comments_preserve_code "$file" | sed -n "${line}p")
        if printf '%s\n' "$stripped_line" | grep -q .; then
            printf '%s:%s:%s\n' "$file" "$line" "$rest"
        fi
    done
}

main() {
    cd "$(git rev-parse --show-toplevel)"

    HDR='include/fixpp/dict/dictionary_snapshot.hpp'
    FACTORY='src/dictionary/dictionary_snapshot.cpp'
    A5TU='tests/dictionary/dictionary_snapshot_test.cpp'

    # ── G1 — SOLE MINTER ──────────────────────────────────────────────────────
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
    SELF_TEST='tools/test_dictionary_snapshot_exclusivity_gate.sh'
    g1_hits_raw=$(grep -rn 'snapshot_key' src/ include/ bindings/ tools/ tests/ || true)
    g1_hits=$(printf '%s\n' "$g1_hits_raw" | { grep -v "^${SELF}:" || true; })
    g1_hits=$(printf '%s\n' "$g1_hits" | { grep -v "^${SELF_TEST}:" || true; })
    g1_bad=$(printf '%s\n' "$g1_hits" | { grep -vE "$G1_ALLOW" || true; })
    g1_bad_n=$(n_of "$g1_bad")

    # LIVENESS hits, CODE ONLY: the stateful comment stripper must remove comment-
    # only evidence even when a block spans lines, while preserving code before or
    # after a comment on the same physical line.
    g1_hits_code=$(printf '%s\n' "$g1_hits" | code_hits_only)

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

    # ── G2 — SOLE ALIAS-FORMER ────────────────────────────────────────────────
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
    echo "PASS: dictionary_snapshot exclusivity gates (G1 sole minter, G2 sole alias-former)."
}

if [[ "${1-}" == "--strip-comments" ]]; then
    [ $# -eq 2 ] || { echo "usage: $0 --strip-comments <file>" >&2; exit 2; }
    strip_comments_preserve_code "$2"
    exit 0
fi

main "$@"
