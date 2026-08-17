#!/usr/bin/env bash
# SINGLE SOURCE for the wheel lane's ccache identity: its lane name and the
# digest-pinned manylinux image it builds in (#259).
#
# Prints two lines:
#   lane=<lane name>
#   image_ref=<image>@sha256:<64 hex>
#
# ── WHY THIS EXISTS AT ALL ───────────────────────────────────────────────────
#
# Three places need these two strings: the ccache RESTORE, the ccache SEED, and
# the assertion that cibuildwheel really used the pinned image. Spelling them
# out three times in tier1.yml is exactly the drift ci/ccache-cache-key.sh's
# opening note warns about — a lane string the publish side and the pull side
# compute DIFFERENTLY is a permanent MISS, and a permanent miss on a compiler
# cache is indistinguishable from "ccache didn't help", which is the conclusion
# #259 exists to change. One step runs this once and exports both.
#
# ── WHY THE IMAGE REF IS READ FROM pyproject.toml AND NOT RESTATED HERE ──────
#
# pyproject.toml is what cibuildwheel actually obeys. A copy here could drift
# from the value driving the build, and the failure would be silent in the worst
# way: the cache would be keyed to a toolchain that is not the one compiling.
# Reading the real file makes that impossible rather than merely unlikely.
#
# ⚠️ `set -uo pipefail` WITHOUT `-e`, matching every other script in this
# directory — see restore-ccache.sh's header for why.
set -uo pipefail

PYPROJECT="${1:-bindings/python/pyproject.toml}"

# The lane name. Bound to the container-lane enumeration in
# ci/ccache-cache-key.sh (`ccache_lane_is_container`) — the two must agree, or
# the pruner classifies this lane's tags as somebody else's and silently skips
# them. ci/test-ccache-scripts.sh asserts the agreement.
LANE="wheel-manylinux228"

if [ ! -f "$PYPROJECT" ]; then
  echo "wheel-ccache-ident: $PYPROJECT not found; run this from the library root." >&2
  exit 1
fi

# tomllib is stdlib from 3.11; the wheel job sets up 3.12. Parsing the real TOML
# rather than grepping the line means a reformat, a comment mentioning the key,
# or a moved table cannot silently yield the wrong string.
# ⚠️ STDERR GOES TO A FILE, NOT INTO THE VALUE. An earlier draft used `2>&1`
# for the sake of the error path, which contaminates the SUCCESS path: any
# Python warning on stderr would land INSIDE $IMAGE_REF. The `*@sha256:*` guard
# below would still match (the ref is in there somewhere), so it would pass —
# and then a multi-line value written to $GITHUB_OUTPUT as `key=value` truncates
# at the first newline, handing restore and seed a mangled reference. The minter
# then refuses it and the lane takes a permanent MISS: precisely the failure
# ci/ccache-cache-key.sh's header is written about.
ERR="$(mktemp)"
trap 'rm -f "$ERR"' EXIT

IMAGE_REF="$(
  python3 -c '
import sys, tomllib
with open(sys.argv[1], "rb") as f:
    cfg = tomllib.load(f)
try:
    print(cfg["tool"]["cibuildwheel"]["manylinux-x86_64-image"])
except KeyError:
    raise SystemExit("manylinux-x86_64-image is not set in [tool.cibuildwheel]")
' "$PYPROJECT" 2>"$ERR"
)" || { echo "wheel-ccache-ident: $(cat "$ERR")" >&2; exit 1; }

# Exactly ONE line, and exactly the shape `$GITHUB_OUTPUT` can carry as a plain
# `key=value`. Asserting the shape here means a malformed value can never reach
# three consumers; a bare `key=value` cannot express a multi-line string, so a
# second line would be silently dropped rather than reported.
if [ "$(printf '%s' "$IMAGE_REF" | wc -l)" -ne 0 ] \
   || ! printf '%s' "$IMAGE_REF" | grep -qE '^[^[:space:]]+@sha256:[0-9a-f]{64}$'; then
  echo "wheel-ccache-ident: manylinux-x86_64-image did not resolve to a single, well-formed digest-pinned reference. Got: $(printf '%s' "$IMAGE_REF" | head -3)" >&2
  exit 1
fi

# Fail closed on a floating reference HERE as well as in the minter. The minter
# is the authority, but catching it at the source gives the error next to the
# file the reader has to edit, and stops a non-pinned value from reaching three
# separate consumers before anything complains.
case "$IMAGE_REF" in
  *@sha256:*) ;;
  *)
    echo "wheel-ccache-ident: manylinux-x86_64-image is not digest-pinned ('$IMAGE_REF'). A floating tag keys a MOVING toolchain to a STABLE ccache tag — internal misses forever, reported as a healthy HIT. Pin it to <image>@sha256:<64 hex>." >&2
    exit 1 ;;
esac

echo "lane=$LANE"
echo "image_ref=$IMAGE_REF"
