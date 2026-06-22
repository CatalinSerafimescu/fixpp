#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# tools/check_no_std_mutex_in_awaitable_headers.sh
#
# CI gate: rejects any header that (post-preprocessing) both:
#   (a) pulls in asio::awaitable<...>, AND
#   (b) names any of the FR-014 six banned std:: mutex types.
#
# Banned spellings (FR-014 six-type set, per [const §XV.9] / [2f §6.6]):
#   std::mutex
#   std::recursive_mutex
#   std::timed_mutex
#   std::recursive_timed_mutex
#   std::shared_mutex
#   std::shared_timed_mutex
#
# Post-preprocessing scope: each header is preprocessed with -E so transitive
# includes are caught (per tasks.md T015 / T066 / [2f §6.6] Codex C-P2-10).
#
# Diagnostic: names fixpp::sync::async_mutex as the correct alternative.
#
# Corpus / CI wiring: finalized in T066 (US5). This scaffold (T015) runs
# correctly on any supplied header list and exits non-zero on any violation.
#
# Usage:
#   # Check a specific set of headers:
#   bash tools/check_no_std_mutex_in_awaitable_headers.sh \
#       -I include \
#       -I /path/to/asio/include \
#       -- include/fixpp/core/sync/async_mutex.hpp ...
#
#   # When no headers are supplied, exits 0 (nothing to check).
#
# Exit codes:
#   0  — no violations found (or no headers to check)
#   1  — at least one violation found
#
# NOTE ([const §XV.9] limitation, per FR-014):
#   `using`/`typedef` aliases are out of grep scope — recorded limitation,
#   not a corpus false-negative. This matches the tasks.md T066 note.
#
# Full corpus registration and CI step wiring is T066 (US5).
# ─────────────────────────────────────────────────────────────────────────────

set -euo pipefail

# ─── Parse arguments ──────────────────────────────────────────────────────────
# Collect -I flags and header files.  Everything before '--' is a compiler
# flag; everything after (or if no '--', non-flag arguments) are headers.

INCLUDE_FLAGS=()
HEADERS=()
PARSING_FLAGS=1
NEXT_IS_IPATH=0
# 046 (NFR-017) CT-1: when set, a supplied awaitable-including header that emits
# NO asio/awaitable.hpp marker is a FAILURE, not a silent skip — so a libc++-leg
# misconfiguration (empty/half output) goes RED instead of false-green.
REQUIRE_AWAITABLE_MARKER=0

for arg in "$@"; do
    if [[ $NEXT_IS_IPATH -eq 1 ]]; then
        INCLUDE_FLAGS+=("-I${arg}")
        NEXT_IS_IPATH=0
        continue
    fi
    if [[ "$arg" == "--" ]]; then
        PARSING_FLAGS=0
        continue
    fi
    if [[ $PARSING_FLAGS -eq 1 ]]; then
        if [[ "$arg" == "-I" ]]; then
            # -I path form: next arg is the path
            NEXT_IS_IPATH=1
        elif [[ "$arg" == -I* ]]; then
            # -Ipath combined form
            INCLUDE_FLAGS+=("$arg")
        elif [[ "$arg" == --require-awaitable-marker ]]; then
            # 046 CT-1: enforce marker presence (libc++-leg falsifiability).
            REQUIRE_AWAITABLE_MARKER=1
        elif [[ "$arg" == -D* || "$arg" == --std=* || "$arg" == -std=* || "$arg" == -stdlib=* ]]; then
            # 046 CT-1: forward -stdlib=* (was previously mis-bucketed as a
            # header → "header not found" WARNING, so the libc++ leg silently
            # preprocessed under the host libstdc++).
            INCLUDE_FLAGS+=("$arg")
        else
            HEADERS+=("$arg")
        fi
    else
        HEADERS+=("$arg")
    fi
done

if [[ ${#HEADERS[@]} -eq 0 ]]; then
    # Nothing to check — exit clean.
    exit 0
fi

# ─── Detect a C++ compiler ────────────────────────────────────────────────────
if command -v clang++ &>/dev/null; then
    CXX=clang++
elif command -v g++ &>/dev/null; then
    CXX=g++
else
    echo "check_no_std_mutex_in_awaitable_headers: ERROR: no C++ compiler found (tried clang++, g++)." >&2
    exit 2
fi

# ─── Awaitable presence pattern ───────────────────────────────────────────────
# After preprocessing, asio::awaitable's template definition lives under its
# namespace; the reliable transitive-include signal is the presence of the
# asio/awaitable.hpp path in a preprocessor line marker (# N ".../asio/
# awaitable.hpp"), present whenever asio::awaitable is (transitively) pulled.
AWAITABLE_PATTERN='asio/awaitable.hpp'

# ─── T066: file-attributed banned-token detection ────────────────────────────
# CRITICAL correctness point: a naive substring grep of the -E output FALSE-
# POSITIVES on every legitimate asio-awaitable header, because asio itself uses
# std::mutex internally (so `std::mutex` is always present post-preprocessing).
# The FR-014 / [const §XV.9] intent is to reject a banned spelling that
# originates in the *header under test or its project-local transitive
# includes* — NOT asio's / libstdc++'s own internal use.
#
# We therefore walk the -E output tracking the active GCC/Clang line marker
# (`# <lineno> "<file>" <flags>`): a banned token counts only when the active
# region is NON-system (flag 3 absent) AND the owning file is not an asio /
# toolchain / Conan-cache path. This catches the project-local transitive case
# (T065) while never firing on asio's internal std::mutex (T064 zero-FP).
#
# `using`/`typedef` aliases remain out of scope (FR-014 recorded limitation).

# awk detector: prints one line per distinct banned spelling found in a
# non-system region; stdout is consumed by the caller (which aggregates).
detect_banned() {
    awk '
    function is_system(fname, flags) {
        if (flags ~ /(^| )3( |$)/)              return 1
        if (fname ~ /\/asio\//)                 return 1
        if (fname ~ /\/(c\+\+|bits|gcc|clang|llvm)\//) return 1
        if (fname ~ /^\/usr\//)                 return 1
        if (fname ~ /\.conan2\//)               return 1
        if (fname == "<built-in>" || fname == "<command-line>" || fname == "<stdin>") return 1
        return 0
    }
    BEGIN { sys = 1 }
    /^# [0-9]+ "/ {
        rest  = substr($0, index($0, "\"") + 1)
        fname = substr(rest, 1, index(rest, "\"") - 1)
        flags = substr(rest, index(rest, "\"") + 1)
        sys   = is_system(fname, flags)
        next
    }
    {
        if (sys) next
        n = split("std::mutex std::recursive_mutex std::timed_mutex std::recursive_timed_mutex std::shared_mutex std::shared_timed_mutex", pats, " ")
        for (i = 1; i <= n; i++) {
            p  = pats[i]
            re = "(^|[^A-Za-z0-9_])" p "([^A-Za-z0-9_]|$)"
            if ($0 ~ re) seen[p] = 1
        }
    }
    END { for (p in seen) print p }
    ' "$1"
}

# ─── Process headers ─────────────────────────────────────────────────────────
VIOLATIONS=0

for HEADER in "${HEADERS[@]}"; do
    if [[ ! -f "$HEADER" ]]; then
        # 046 Gate B #3: a SUPPLIED corpus header that does not exist is an ERROR,
        # not a warning — a deleted/renamed/mistyped entry must not silently
        # shrink the constitutional gate to a no-op.
        echo "check_no_std_mutex_in_awaitable_headers: ERROR: corpus header not found: $HEADER" >&2
        exit 2
    fi

    TMP_PP=$(mktemp /tmp/check_mutex_XXXXXX.pp)
    TMP_SRC=$(mktemp /tmp/check_mutex_XXXXXX.cpp)
    echo "#include \"$HEADER\"" > "$TMP_SRC"

    # Preprocess (-E); KEEP line markers (no -P) so token→file attribution works.
    # 046 (NFR-017) CT-1: FAIL-CLOSED on a preprocess error (previously masked by
    # `|| true`, so a missing libc++ / broken include path silently produced
    # empty output and a false-green libc++ leg).
    if ! $CXX -std=c++23 "${INCLUDE_FLAGS[@]}" -E "$TMP_SRC" -o "$TMP_PP" 2>"${TMP_PP}.err"; then
        echo "check_no_std_mutex_in_awaitable_headers: ERROR: preprocessing failed for $HEADER" >&2
        sed 's/^/    /' "${TMP_PP}.err" >&2 || true
        rm -f "$TMP_SRC" "$TMP_PP" "${TMP_PP}.err"
        exit 2
    fi
    rm -f "$TMP_SRC" "${TMP_PP}.err"

    # Only headers that (transitively) pull asio::awaitable are in scope.
    if ! grep -qF "$AWAITABLE_PATTERN" "$TMP_PP" 2>/dev/null; then
        if [[ $REQUIRE_AWAITABLE_MARKER -eq 1 ]]; then
            echo "check_no_std_mutex_in_awaitable_headers: ERROR: $HEADER emitted no" >&2
            echo "  '$AWAITABLE_PATTERN' marker under --require-awaitable-marker — the" >&2
            echo "  preprocess leg did not run as expected (libc++ misconfig?)." >&2
            rm -f "$TMP_PP"
            exit 1
        fi
        rm -f "$TMP_PP"
        continue
    fi

    while IFS= read -r BANNED; do
        [[ -z "$BANNED" ]] && continue
        echo "VIOLATION: $HEADER" >&2
        echo "  Found '${BANNED}' (project-owned, non-system) in a header that" >&2
        echo "  transitively references 'asio::awaitable'." >&2
        echo "  Use 'fixpp::sync::async_mutex' instead ([const §XV.9] / [2f §6.6])." >&2
        VIOLATIONS=$((VIOLATIONS + 1))
    done < <(detect_banned "$TMP_PP")

    rm -f "$TMP_PP"
done

if [[ $VIOLATIONS -gt 0 ]]; then
    echo "" >&2
    echo "check_no_std_mutex_in_awaitable_headers: FAILED — $VIOLATIONS violation(s) found." >&2
    echo "  std::mutex (and the FR-014 related types) are banned in headers that" >&2
    echo "  include asio::awaitable<...>. Replace with fixpp::sync::async_mutex" >&2
    echo "  ([const §XV.9] / [2f §6.6])." >&2
    exit 1
fi

exit 0
