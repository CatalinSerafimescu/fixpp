#!/usr/bin/env bash
# ci/check-wheel-payload.sh — assert WHAT IS IN the shipped wheel (#255).
#
# Usage:  ci/check-wheel-payload.sh <wheel.whl>
#
# ── Why this exists ─────────────────────────────────────────────────────────
# Nothing asserted wheel CONTENTS before. The neighbouring tier1 step checks the
# tag, the external-library (NEEDED) set and abi3 conformance — all properties
# of the EXTENSION MODULE — so none of them reads the archive's file list. A
# wheel could therefore carry (and did carry, until #255) the whole C++ install
# tree while every gate stayed green: `wheel.exclude` listed only `include/**`,
# so every other install() rule in the root CMakeLists landed in the wheel.
#
# ⚠️ No sizes or member counts are recorded here on purpose. They are a RESULT,
# they rot the moment a dictionary or an archive changes, and nothing re-runs a
# comment. The CONDITION is what survives: anything under the excluded roots
# below is C++ install-tree content that a wheel consumer cannot use, because
# the wheel ships a fully-linked _fixpp.so and resolves its data through the
# `_fixpp_data` PACKAGE via importlib.resources — never a filesystem path, so
# `share/` is unreachable from Python by construction. To see the current
# numbers, run this script: it prints them.
#
# ── ⚠️ TWO-SIDED ON PURPOSE ─────────────────────────────────────────────────
# The absence half alone is satisfied by an EMPTY wheel, and the way this check
# gets broken is a `wheel.exclude` glob matching more than intended — precisely
# the failure an absence-only check cannot see. So every required member is
# asserted present as well. Neither half is redundant: drop the presence half
# and an over-broad exclude ships a wheel that imports nothing; drop the absence
# half and the leak this was written for returns unobserved.
#
# ── Forcing it RED (do this before believing a PASS) ────────────────────────
#   absence half : remove "lib/**" from wheel.exclude in
#                  bindings/python/pyproject.toml, rebuild -> FAIL_ABSENT fires
#   presence half: add "share/doc/**" to that same list,
#                  rebuild -> FAIL_PRESENT fires on the licence members
# A pass recorded without having seen at least one of those is a pass this
# script could not have withheld.
set -euo pipefail

WHL="${1:-}"
if [ -z "$WHL" ] || [ ! -f "$WHL" ]; then
  echo "usage: $0 <wheel.whl>" >&2
  exit 2
fi

# The licence/attribution set is NOT spelled here: it is shared with
# tests/packaging/run_package_contents_witness.cmake, which asserts the same
# obligation for the C++ packages. See ci/expected-shipped-doc-files.txt for
# why one file rather than two literal lists.
DOCS_LIST="$(dirname "$0")/expected-shipped-doc-files.txt"
if [ ! -f "$DOCS_LIST" ]; then
  echo "FAIL: $DOCS_LIST is missing — the licence set cannot be checked, and a" >&2
  echo "      check that silently skips its own inputs is worse than no check." >&2
  exit 2
fi

# ONE interpreter, one open of the archive: the name list, every assertion and
# the size report all come from the same read.
python3 - "$WHL" "$DOCS_LIST" <<'PY'
import re, sys, zipfile

whl, docs_list = sys.argv[1], sys.argv[2]

with zipfile.ZipFile(whl) as z:
    infos = z.infolist()
names = [i.filename for i in infos]

# A zip with no entries satisfies every "must be absent" test below while
# reporting nothing wrong. Refuse to grade it at all — this is a DIFFERENT
# failure from a missing member and deserves to say so rather than arriving as
# a dozen confusing FAIL_PRESENT lines.
if not names:
    sys.exit(f"FAIL: '{whl}' lists no entries at all — nothing can be concluded from it.")

# DOCDIR-relative paths -> where they land in the wheel. GNUInstallDirs puts
# CMAKE_INSTALL_DOCDIR at share/doc/<project>, and the wheel takes the install
# tree verbatim (Root-Is-Purelib: false), so the prefix is spelled once here.
DOCDIR = "share/doc/fixpp/"

# ⚠️ The grammar is VALIDATED, not used as a filter — the twin of the rule in
# tests/packaging/run_package_contents_witness.cmake, and it must stay
# identical to it. Skipping a malformed row would quietly narrow the licence
# set while leaving this check green; a row that is neither comment nor valid
# filename is therefore fatal. Keep the two grammars in step: if one parser
# accepts a name the other drops, the two artifacts stop asserting the same set
# and nothing says so.
NAME_RE = re.compile(r"^[A-Za-z0-9_.-]+$")
rows, bad = [], []
with open(docs_list, encoding="utf-8") as fh:
    for line in fh:
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        (rows if NAME_RE.match(s) else bad).append(s)
if bad:
    sys.exit("FAIL: {} has row(s) that are neither a comment nor a valid filename:\n  {}\n"
             "Refusing to continue: dropping them would silently narrow the licence set."
             .format(docs_list, "\n  ".join(bad)))
if not rows:
    sys.exit(f"FAIL: {docs_list} yielded no entries — it parsed to nothing, which "
             f"would make the licence half of this check vacuously pass.")
doc_members = [DOCDIR + r for r in rows]

# ── The absence half is an ALLOW-LIST, deliberately ─────────────────────────
# It used to probe three named roots (lib/, include/, share/fixpp/) — the ones
# that had actually leaked. That reproduces the very defect it guards: the
# wheel's contents come from EVERY install() rule in the root CMakeLists, so a
# future rule landing under bin/, etc/, libexec/ or a NEW share/<x>/ would be
# invisible to a check that names only what leaked once, and someone would have
# to remember to extend it. Asserting the permitted set instead makes any new
# top-level arrival loud by construction, whatever it is called.
#
# `share` is permitted at top level but is NOT a free pass — the wheel keeps
# share/doc/fixpp/ for the licences (see wheel.exclude in pyproject.toml), so
# the tree below it is constrained separately.
ALLOWED_TOP = {
    "fixpp.py",
    "fixpp_oo.py",
    "fixpp_dict_data.py",
    "_fixpp_data",
    "share",
}
ALLOWED_SHARE_PREFIX = "share/doc/fixpp/"

REQUIRED = [
    "fixpp.py",
    "fixpp_oo.py",
    "fixpp_dict_data.py",
    "_fixpp_data/__init__.py",
    # The four the locator exposes. Their presence is also what makes the
    # licence members above an obligation rather than a courtesy: these are the
    # redistributed QuickFIX material.
    "_fixpp_data/FIX42.xml",
    "_fixpp_data/FIX44.xml",
    "_fixpp_data/FIX50SP2.xml",
    "_fixpp_data/FIXT11.xml",
] + doc_members

rc = 0
nameset = set(names)

def _report(label, detail, offenders):
    print(f"{label}: {detail}")
    for o in sorted(offenders)[:10]:
        print(f"               {o}")
    if len(offenders) > 10:
        print(f"               ... and {len(offenders) - 10} more")

# ⚠️ THE TWO EXEMPTIONS BELOW ARE NARROW ON PURPOSE — a wider spelling of them
# put a hole straight through this allow-list, and it was caught by review, not
# by the check. Testing `top.endswith(".dist-info")` exempts ANY top-level root
# whose NAME ends that way, and `top.startswith("_fixpp") and endswith(".so")`
# exempts a DIRECTORY so named. Both were reproduced shipping a real leak past
# a clean report:
#     fixpp-0.dist-info/lib/libfixpp_core.a      -> reported OK
#     _fixpp_payload.so/lib/libfixpp_core.a      -> reported OK
# So the extension exemption now requires an actual top-level FILE, and the
# metadata exemption requires the ONE dist-info root this project's wheel has.
DIST_INFO_RE = re.compile(r"^fixpp-[^/]+\.dist-info$")

dist_info_roots = {n.split("/", 1)[0] for n in names
                   if DIST_INFO_RE.match(n.split("/", 1)[0])}
if len(dist_info_roots) != 1:
    print(f"FAIL_ABSENT: expected exactly one 'fixpp-<version>.dist-info' root, "
          f"found {len(dist_info_roots)}: {sorted(dist_info_roots)}")
    rc = 1

unexpected = {}
for n in names:
    top = n.split("/", 1)[0]
    if top in dist_info_roots:
        continue
    # A top-level FILE, not a directory that merely bears the name.
    if "/" not in n and top.startswith("_fixpp") and top.endswith(".so"):
        continue
    if top not in ALLOWED_TOP:
        unexpected.setdefault(top, []).append(n)

if unexpected:
    roots = ", ".join(sorted(unexpected))
    flat = [n for v in unexpected.values() for n in v]
    _report("FAIL_ABSENT",
            f"{len(flat)} entr(ies) under unexpected top-level root(s) [{roots}] — the\n"
            "             wheel carries content outside the Python distribution. "
            "Offenders:",
            flat)
    rc = 1

stray_share = [n for n in names
               if n.split("/", 1)[0] == "share"
               and not n.startswith(ALLOWED_SHARE_PREFIX)
               and n != "share/" and n != "share/doc/"]
if stray_share:
    _report("FAIL_ABSENT",
            f"{len(stray_share)} entr(ies) under share/ outside "
            f"'{ALLOWED_SHARE_PREFIX}' — only the\n             licence set belongs "
            "there. Offenders:",
            stray_share)
    rc = 1

# ── Independent of the roots above, and deliberately so ─────────────────────
# The allow-list reasons about the TOP-LEVEL segment, so any exemption it grants
# covers a whole subtree — which is precisely how the two holes above worked.
# This asks a different question that no root exemption can answer: does any C++
# BUILD OUTPUT appear anywhere in the archive, at any depth? A leak has to be one
# of these shapes to be worth anything to a C++ consumer, so this stays true even
# under a root nobody predicted, inside dist-info included.
BUILD_OUTPUT_RE = re.compile(
    r"(\.(a|o|lib|obj|so\.[0-9]+.*)$)|((^|/)[A-Za-z0-9_]+(Config|Targets)[^/]*\.cmake$)")
strays = [n for n in names
          if BUILD_OUTPUT_RE.search(n)
          and not (n.startswith("_fixpp") and n.endswith(".so") and "/" not in n)]
if strays:
    _report("FAIL_ABSENT",
            f"{len(strays)} C++ build-output file(s) anywhere in the archive — a\n"
            "             wheel needs none of them at any depth. Offenders:",
            strays)
    rc = 1

for member in REQUIRED:
    if member not in nameset:
        print(f"FAIL_PRESENT: '{member}' is missing from the wheel.")
        rc = 1

# The extension carries an interpreter-dependent name, so it is matched by
# shape rather than spelled literally like the members above.
if not any(n.startswith("_fixpp") and n.endswith(".so") and "/" not in n for n in names):
    print("FAIL_PRESENT: no top-level _fixpp*.so in the wheel.")
    rc = 1

# ⚠️ Reported, never asserted. A size threshold is a claim about a moving tree:
# it rots on any legitimate dictionary or engine growth, and it would be a
# WEAKER statement than the membership tests above, which say exactly which
# content is unwanted.
total = sum(i.file_size for i in infos)
print(f"wheel payload: {len(names)} entries, {total:,} bytes uncompressed")

if rc == 0:
    print("OK: wheel payload is the Python distribution plus its licences, and nothing else.")
sys.exit(rc)
PY
