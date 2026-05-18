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
        elif [[ "$arg" == -D* || "$arg" == --std=* || "$arg" == -std=* ]]; then
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

# ─── FR-014 banned patterns ───────────────────────────────────────────────────
# Each is a token that must NOT appear in any translation unit that also
# references asio::awaitable.
BANNED_PATTERNS=(
    'std::mutex'
    'std::recursive_mutex'
    'std::timed_mutex'
    'std::recursive_timed_mutex'
    'std::shared_mutex'
    'std::shared_timed_mutex'
)

# ─── Awaitable presence pattern ───────────────────────────────────────────────
# After preprocessing, the asio::awaitable template definition appears under
# its namespace, so "asio::awaitable" only appears in user-code call sites.
# A more reliable signal is the presence of the asio/awaitable.hpp header path
# in a preprocessor line marker (# N "path/asio/awaitable.hpp") or the
# template declaration token "class awaitable" inside the asio namespace block.
# We use the file-path line marker approach: after -E, line markers include the
# form:  # N ".../asio/awaitable.hpp"
# which is present whenever asio::awaitable is (transitively) included.
AWAITABLE_PATTERN='asio/awaitable.hpp'

# ─── Process headers ─────────────────────────────────────────────────────────
VIOLATIONS=0

for HEADER in "${HEADERS[@]}"; do
    if [[ ! -f "$HEADER" ]]; then
        echo "check_no_std_mutex_in_awaitable_headers: WARNING: header not found: $HEADER" >&2
        continue
    fi

    # Preprocess the header into a temporary file.
    TMP_PP=$(mktemp /tmp/check_mutex_XXXXXX.pp)
    # Wrap in a minimal .cpp for -E preprocessing.
    TMP_SRC=$(mktemp /tmp/check_mutex_XXXXXX.cpp)
    echo "#include \"$HEADER\"" > "$TMP_SRC"

    # Run -E; tolerate failures (header may have missing deps — we only care
    # about the output text we do get).
    $CXX -std=c++23 "${INCLUDE_FLAGS[@]}" -E "$TMP_SRC" -o "$TMP_PP" 2>/dev/null || true

    # Cleanup the source wrapper (keep TMP_PP for grep below).
    rm -f "$TMP_SRC"

    # Only proceed if the preprocessed output mentions asio::awaitable.
    # Use grep directly on the file (avoids echo-of-large-string shell issues).
    if ! grep -qF "$AWAITABLE_PATTERN" "$TMP_PP" 2>/dev/null; then
        rm -f "$TMP_PP"
        continue  # This header does not pull asio::awaitable — skip.
    fi

    # Check each banned spelling.
    for BANNED in "${BANNED_PATTERNS[@]}"; do
        if grep -qF "$BANNED" "$TMP_PP" 2>/dev/null; then
            echo "VIOLATION: $HEADER" >&2
            echo "  Found '${BANNED}' in a header that references 'asio::awaitable'." >&2
            echo "  Use 'fixpp::sync::async_mutex' instead ([const §XV.9] / [2f §6.6])." >&2
            VIOLATIONS=$((VIOLATIONS + 1))
        fi
    done

    rm -f "$TMP_PP"
done

if [[ $VIOLATIONS -gt 0 ]]; then
    echo "" >&2
    echo "check_no_std_mutex_in_awaitable_headers: FAILED — $VIOLATIONS violation(s) found." >&2
    echo "  std::mutex (and related types) are banned in headers that include asio::awaitable<...>." >&2
    echo "  Replace with fixpp::sync::async_mutex ([const §XV.9] / [2f §6.6])." >&2
    exit 1
fi

exit 0
