#!/usr/bin/env bash
# ci/test-check-wheel-payload.sh — regression pin for ci/check-wheel-payload.sh (#255).
#
# ── Why this exists, and why the recipe it replaces was useless ─────────────
# check-wheel-payload.sh gates what the shipped wheel contains. Its first draft
# documented only ONE way to prove it could report non-zero: "remove lib/** from
# wheel.exclude and rebuild". That rebuild is a manylinux container build of the
# whole engine — tens of minutes — so in practice nobody would ever run it, and
# an instrument whose only proof-of-life is unaffordable is an instrument nobody
# has checked. Meanwhile the defects that actually got past that draft were
# reproduced in SECONDS with synthetic zips.
#
# So the arms live here, as zip fixtures. No build, no network, no Conan, no
# toolchain — python3 + bash, which the ci-script-pins job already has.
#
# ⚠️ EVERY RED CELL ASSERTS ITS OWN FAILURE TOKEN, not merely a non-zero exit.
# A mutant that reddens for the wrong reason certifies nothing: it would keep
# passing this pin after the check it was written for had been deleted. Each
# cell below therefore names the string the check must emit.
#
# ⚠️ TWO OF THESE CELLS ARE HISTORY, NOT HYPOTHESIS. `rogue_dist_info` and
# `dir_named_so` are the two subtree escapes a hostile review reproduced against
# a draft that had already been rewritten once to close this very class:
#   fixpp-0.dist-info/lib/libfixpp_core.a   -> reported OK
#   _fixpp_payload.so/lib/libfixpp_core.a   -> reported OK
# They are pinned so that closing them cannot be silently undone.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SCRIPT="$HERE/check-wheel-payload.sh"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

pass=0; fail=0

# Build the harness copy so $0-relative resolution of the doc list is exercised
# exactly as in production, but against a fixture list we control.
mk_harness() {   # $1 = dir, $2 = doc-list contents ("" => omit the file entirely)
  mkdir -p "$1"
  cp "$SCRIPT" "$1/check-wheel-payload.sh"
  chmod +x "$1/check-wheel-payload.sh"
  if [ -n "${2-}" ]; then printf '%s' "$2" > "$1/expected-shipped-doc-files.txt"; fi
}

DEFAULT_LIST='LICENSE
LICENSE-COMMERCIAL.md
NOTICE
QUICKFIX_LICENSE.txt
'

# A wheel that must PASS. Every RED fixture below is this, plus one defect.
mk_wheel() {   # $1 = path, $2.. = extra "name" entries to add, "-name" to drop
  python3 - "$@" <<'PY'
import sys, zipfile
dst, *ops = sys.argv[1:]
base = [
    "_fixpp.cpython-312-x86_64-linux-gnu.so",
    "fixpp.py", "fixpp_oo.py", "fixpp_dict_data.py",
    "_fixpp_data/__init__.py",
    "_fixpp_data/FIX42.xml", "_fixpp_data/FIX44.xml",
    "_fixpp_data/FIX50SP2.xml", "_fixpp_data/FIXT11.xml",
    "share/doc/fixpp/LICENSE", "share/doc/fixpp/LICENSE-COMMERCIAL.md",
    "share/doc/fixpp/NOTICE", "share/doc/fixpp/QUICKFIX_LICENSE.txt",
    "fixpp-0.0.1.dist-info/METADATA", "fixpp-0.0.1.dist-info/WHEEL",
    "fixpp-0.0.1.dist-info/RECORD",
]
for op in ops:
    if op.startswith("-"):
        base.remove(op[1:])
    else:
        base.append(op)
with zipfile.ZipFile(dst, "w") as z:
    for n in base:
        z.writestr(n, b"x")
PY
}

# $1 label, $2 expected rc, $3 expected token ("" when rc==0), $4 wheel, $5 harness dir
cell() {
  local label="$1" want_rc="$2" want_tok="$3" whl="$4" dir="$5"
  local out rc
  out="$("$dir/check-wheel-payload.sh" "$whl" 2>&1)"; rc=$?
  if [ "$rc" -ne "$want_rc" ]; then
    printf '  FAIL %-22s want rc=%s got rc=%s\n' "$label" "$want_rc" "$rc"
    printf '%s\n' "$out" | sed 's/^/         /' | head -4
    fail=$((fail+1)); return
  fi
  if [ -n "$want_tok" ] && ! printf '%s\n' "$out" | grep -qF -- "$want_tok"; then
    printf '  FAIL %-22s rc ok but did not report %s\n' "$label" "$want_tok"
    printf '%s\n' "$out" | sed 's/^/         /' | head -4
    fail=$((fail+1)); return
  fi
  printf '  ok   %-22s rc=%s%s\n' "$label" "$rc" \
         "$( [ -n "$want_tok" ] && echo "  [$want_tok]" )"
  pass=$((pass+1))
}

H="$WORK/h"; mk_harness "$H" "$DEFAULT_LIST"

# ── the GREEN control ───────────────────────────────────────────────────────
# Without this every RED below is unfalsifiable: a check that fails on
# EVERYTHING would satisfy all of them and be worthless.
mk_wheel "$WORK/good.whl"
cell "green-control" 0 "" "$WORK/good.whl" "$H"

# ── the leak classes the excludes exist for ─────────────────────────────────
mk_wheel "$WORK/lib.whl"      "lib/libfixpp_core.a"
cell "leak-lib"      1 "FAIL_ABSENT" "$WORK/lib.whl" "$H"

mk_wheel "$WORK/share.whl"    "share/fixpp/dictionaries/FIX40.xml"
cell "leak-share-fixpp" 1 "FAIL_ABSENT" "$WORK/share.whl" "$H"

mk_wheel "$WORK/inc.whl"      "include/fix/c_api.h"
cell "leak-include"  1 "FAIL_ABSENT" "$WORK/inc.whl" "$H"

# A root the allow-list was never told about — the reason the absence half is an
# allow-list and not a list of known-bad prefixes.
mk_wheel "$WORK/bin.whl"      "bin/fixpp-tool"
cell "leak-unnamed-root" 1 "FAIL_ABSENT" "$WORK/bin.whl" "$H"

# ── the two reproduced subtree escapes ──────────────────────────────────────
# ⚠️ THE PAYLOAD HIDDEN IN THESE TWO IS DELIBERATELY *NOT* A BUILD ARTEFACT, and
# that detail is the difference between pinning the rule and only appearing to.
# The first draft hid `lib/libfixpp_core.a` in them — which the shape-keyed check
# further down catches on its own, so both cells stayed RED even after the root
# rules they exist for were reverted. Measured: re-introducing the loose
# `top.endswith(".dist-info")` exemption left this pin at 16/16. A dictionary XML
# matches no build-output shape, so ONLY the root rules can reject it, and the
# cells now fail when those rules regress. Each also asserts the specific
# diagnostic rather than the generic FAIL_ABSENT.
mk_wheel "$WORK/rogue.whl"    "fixpp-0.dist-info/share/fixpp/dictionaries/FIX40.xml"
cell "rogue-dist-info" 1 "expected exactly one" "$WORK/rogue.whl" "$H"

mk_wheel "$WORK/dirso.whl"    "_fixpp_payload.so/share/fixpp/dictionaries/FIX40.xml"
cell "dir-named-so"  1 "unexpected top-level root" "$WORK/dirso.whl" "$H"

# ⚠️ A SECOND dist-info cell, and it is not a duplicate of the one above. That
# one uses `fixpp-0.dist-info`, which MATCHES the `fixpp-<version>` pattern — so
# it is rejected by the "exactly one dist-info root" rule and would stay RED even
# if the per-entry exemption were reverted to the loose `endswith('.dist-info')`.
# Measured: reverting it left this pin at 16/16, i.e. that cell pins the
# uniqueness rule and NOT the exemption. A root which does not match the pattern
# leaves the count at one, so only the exemption can reject it — which is what
# this cell holds, and it is the shape a real escape would take.
mk_wheel "$WORK/rogue2.whl"   "evil.dist-info/share/fixpp/dictionaries/FIX40.xml"
cell "rogue-dist-info-alt" 1 "unexpected top-level root" "$WORK/rogue2.whl" "$H"

# Shape-keyed, so no ROOT exemption can answer for it: a build artefact hidden
# inside the one dist-info root that is legitimately allowed.
mk_wheel "$WORK/indist.whl"   "fixpp-0.0.1.dist-info/lib/libfixpp_core.a"
cell "build-output-in-dist" 1 "FAIL_ABSENT" "$WORK/indist.whl" "$H"

# ── the presence half ───────────────────────────────────────────────────────
# Without these an over-broad exclude ships a wheel that imports nothing.
mk_wheel "$WORK/nolic.whl"    "-share/doc/fixpp/LICENSE"
cell "missing-licence" 1 "FAIL_PRESENT" "$WORK/nolic.whl" "$H"

mk_wheel "$WORK/noso.whl"     "-_fixpp.cpython-312-x86_64-linux-gnu.so"
cell "missing-extension" 1 "FAIL_PRESENT" "$WORK/noso.whl" "$H"

mk_wheel "$WORK/noxml.whl"    "-_fixpp_data/FIX44.xml"
cell "missing-dictionary" 1 "FAIL_PRESENT" "$WORK/noxml.whl" "$H"

# ── an empty archive must be REFUSED, not graded ────────────────────────────
python3 -c "import zipfile,sys; zipfile.ZipFile(sys.argv[1],'w').close()" "$WORK/empty.whl"
cell "empty-archive" 1 "lists no entries" "$WORK/empty.whl" "$H"

# ── the shared doc list is VALIDATED, not filtered ──────────────────────────
# A malformed row must be fatal. Were it merely skipped, the licence set would
# narrow silently and this check would stay green — the defect a hostile review
# reproduced against the pre-filter version.
HB="$WORK/hbad"; mk_harness "$HB" 'LICENSE
LICENSE-COMMERCIAL.md
NOTICE
QUICKFIX LICENSE.txt
'
cell "doclist-malformed" 1 "neither a comment nor a valid filename" "$WORK/good.whl" "$HB"

HE="$WORK/hempty"; mk_harness "$HE" '# only comments here
'
cell "doclist-empty" 1 "parsed to nothing" "$WORK/good.whl" "$HE"

HM="$WORK/hmissing"; mk_harness "$HM" ""
cell "doclist-missing" 2 "is missing" "$WORK/good.whl" "$HM"

# A non-ASCII row must not slip through as a member.
HN="$WORK/hnonascii"; mk_harness "$HN" 'LICENSE
LICENSE-COMMERCIAL.md
NOTICE
QUICKFIX_LICENSE.txt
LICENCE—EM-DASH
'
cell "doclist-non-ascii" 1 "neither a comment nor a valid filename" "$WORK/good.whl" "$HN"

echo
echo "check-wheel-payload pin: ${pass} pass, ${fail} fail"
[ "$fail" -eq 0 ] || exit 1
