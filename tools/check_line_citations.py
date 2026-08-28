#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# tools/check_line_citations.py
#
# Instrument for issue #310: line-number citations in comments rot silently.
#
# A citation naming a line number is a claim about a file that keeps moving.
# Nobody has to touch the CITING file for it to become false -- the TARGET
# drifts and the citation rots in place. Nothing ever fails; the reader is sent
# to the wrong line and usually lands on an unrelated comment, which reads as
# plausible.
#
# Three spellings exist, and a sweep keyed on any one of them reports clean over
# the other two (#310 records both halves of that failure):
#
#   A  `file.cpp:NNN`      -- has a filename, so the target is RESOLVABLE
#   B  `at line 2234`      -- NO filename: undetectable by any filename-keyed
#                             sweep, AND ambiguous about which file it means
#   C  `(:64)`             -- bare self-citation, same blindness as B
#
# TWO MODES, and the distinction is the point:
#
#   --census  reports CANDIDATES with the cited line's ACTUAL content beside the
#             claim, for a human to adjudicate. It does NOT emit a rot verdict:
#             whether a comment's prose still describes the code at line N is not
#             mechanically decidable. The ONE exception is `out-of-range`, which
#             is decided here because it cannot be anything but a defect.
#
#   --staged / --range  GATE newly ADDED lines only. Gating the whole tree would
#             be noise; gating additions stops the bleeding without demanding a
#             tree-wide migration first.
#
# THE FIX FOR A FLAGGED CITATION IS TO DELETE THE NUMBER, NOT TO CORRECT IT.
# Re-pointing `session.cpp:1258` at `session.cpp:1265` re-arms the same defect
# with a fresh half-life. Cite a function/struct name plus a short quoted phrase
# instead; that survives arbitrary line motion, and grep finds it if the quoted
# text is ever changed. CONTRIBUTING.md carries the worked example.

import argparse
import collections
import json
import os
import re
import subprocess
import sys
import tempfile

# Citations cannot live in a fuzz seed or an ABI baseline, and those directories
# hold binary blobs that git emits raw into a diff when they contain no NUL in
# the first 8000 bytes -- decoding that as strict UTF-8 throws, which would
# abort a commit that merely added a corpus seed.
SCAN_DIRS = [
    "tests/", "src/", "include/",
    ":(exclude)tests/fuzz/corpus/",
    ":(exclude)tests/abi/baseline/",
]

# Form A. The negative lookbehind keeps `a/b/c.cpp:12` from also matching as
# `b/c.cpp:12` and `c.cpp:12`.
RE_A = re.compile(
    r"(?<![\w/.-])([A-Za-z0-9_][A-Za-z0-9_/.-]*\.(?:hpp|cpp|ipp|hh|hxx|cc|h)):(\d+)"
)
# Form B. {2,} digits: a deliberate recall/precision trade -- one-digit forms
# are not gated, since they collide with prose about test data (`line 5`) far
# more often than a real citation would.
RE_B = re.compile(r"~?\blines?\s+\d{2,}", re.IGNORECASE)
# Form C.
RE_C = re.compile(r"\(:\d+")

# Forms B and C name no target, so nothing can be resolved for them and the only
# available signal is the citing line. Restricted to tokens that name a foreign
# PROJECT or a vendored include root. Deliberately NOT `gtest` or `/usr/`: those
# appear on ordinary in-tree lines, and a line-scoped exemption keyed on them
# silently swallows real citations that happen to share the line.
RE_FOREIGN_LINE = re.compile(r"QuickFIX|QFJ|QFcpp|reference-engines|asio/|boost/",
                             re.IGNORECASE)

# Deliberate, reviewed exception. `--census` reports these as their own bucket:
# the marker is itself a claim (that this number will not rot), so it has to stay
# countable rather than vanish from the instrument built to audit it.
RE_PRAGMA = re.compile(r"citation-ok")

SELF = "tools/check_line_citations.py"


def git(args, root, what):
    """Run git, FAILING LOUDLY. A swallowed git error would make every mode
    report clean, which is the exact defect this tool exists to catch."""
    r = subprocess.run(["git"] + args, cwd=root, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(
            f"check-line-citations: git {what} failed (exit {r.returncode}): "
            f"{r.stderr.strip()[:300]}\n"
            "Refusing to report a clean tree from a failed measurement.")
    return r.stdout


def tracked(root):
    files = git(["ls-files", "--"] + SCAN_DIRS, root, "ls-files").split()
    if not files:
        raise SystemExit(
            "check-line-citations: git ls-files matched ZERO files under "
            f"{SCAN_DIRS[:3]}. Refusing to interpret an empty measurement as a "
            "clean tree -- check the repo root.")
    return files


def basename_map(files):
    by_base = collections.defaultdict(list)
    for p in files:
        by_base[os.path.basename(p)].append(p)
    return by_base


def resolve(target, files, by_base):
    """Candidate in-tree paths for a form-A target. Empty => foreign."""
    if "/" in target:
        hits = [t for t in files if t == target or t.endswith("/" + target)]
        if hits:
            return hits
        # A path-shaped target that does not exist is a spec/contract artifact
        # or a foreign source file. Resolving it by BASENAME would point at an
        # unrelated tree file and manufacture a finding: `005 contracts/
        # session.hpp:46` is not include/fixpp/session/session.hpp.
        return []
    return by_base.get(target, [])


def forms_on(line, files=None, by_base=None):
    """Citation forms present on `line`, minus exemptions.

    Form A is decided PER MATCH, by whether its target resolves in-tree -- not by
    a token on the line. A line-scoped test here swallowed every citation sharing
    a line with an `#include <asio/...>` or the word gtest, including this tool's
    own worked example.
    """
    if RE_PRAGMA.search(line):
        return []
    found = []
    for m in RE_A.finditer(line):
        if files is None or resolve(m.group(1), files, by_base):
            found.append("A")
            break
    foreign_line = RE_FOREIGN_LINE.search(line)
    if RE_B.search(line) and not foreign_line:
        found.append("B")
    if RE_C.search(line) and not foreign_line:
        found.append("C")
    return found


def read_lines(root, path, cache):
    if path not in cache:
        try:
            with open(os.path.join(root, path), encoding="utf-8", errors="replace") as f:
                cache[path] = f.read().splitlines()
        except OSError:
            cache[path] = None
    return cache[path]


def census(root, json_out, quiet=False):
    files = tracked(root)
    by_base = basename_map(files)
    cache = {}
    resolved, foreign, ambiguous = [], [], []
    b_hits, c_hits, pragma_hits = [], [], []

    for p in files:
        src = read_lines(root, p, cache)
        if src is None:
            continue
        for i, ln in enumerate(src):
            if RE_PRAGMA.search(ln):
                pragma_hits.append({"cf": p, "cl": i + 1, "text": ln.strip()})
                continue
            forms = forms_on(ln, files, by_base)
            if "B" in forms:
                b_hits.append({"cf": p, "cl": i + 1, "text": ln.strip()})
            if "C" in forms:
                c_hits.append({"cf": p, "cl": i + 1, "text": ln.strip()})
            for m in RE_A.finditer(ln):
                target, num = m.group(1), int(m.group(2))
                rec = {"cf": p, "cl": i + 1, "text": ln.strip(),
                       "target": target, "n": num}
                cands = resolve(target, files, by_base)
                if not cands:
                    foreign.append(rec)
                    continue
                if len(cands) > 1:
                    rec["cands"] = cands
                    ambiguous.append(rec)
                    continue
                tl = read_lines(root, cands[0], cache)
                rec["tp"] = cands[0]
                rec["tn"] = len(tl)
                rec["inrange"] = num <= len(tl)
                rec["at"] = tl[num - 1].strip() if num <= len(tl) else "<OUT OF RANGE>"
                resolved.append(rec)

    oor = [r for r in resolved if not r["inrange"]]
    if not quiet:
        total_a = len(resolved) + len(foreign) + len(ambiguous)
        print("── #310 citation census " + "─" * 50)
        print(f"form A  `file.ext:NNN`   candidates : {total_a}")
        print(f"          foreign / unresolvable     : {len(foreign)}   [out of scope]")
        print(f"          ambiguous basename         : {len(ambiguous)}   [needs a path]")
        print(f"          RESOLVED in-tree           : {len(resolved)}")
        print(f"            of which OUT OF RANGE    : {len(oor)}   <-- mechanically certain")
        print(f"form B  prose `line NNN` candidates : {len(b_hits)}   [no filename: unresolvable]")
        print(f"form C  bare `(:NNN)`     candidates : {len(c_hits)}   [no filename: unresolvable]")
        print(f"`citation-ok` exemptions in force   : {len(pragma_hits)}")
        print()
        for r in pragma_hits:
            print(f"  exempt: {r['cf']}:{r['cl']}  {r['text'][:100]}")
        if pragma_hits:
            print()
        if oor:
            print("OUT-OF-RANGE citations (each one is a defect):")
            for r in oor:
                print(f"  {r['cf']}:{r['cl']}  ->  {r['target']}:{r['n']}  "
                      f"(target {r['tp']} has {r['tn']} lines)")
                print(f"      {r['text'][:140]}")
            print()
        print("A resolved citation is NOT thereby correct -- only out-of-range is")
        print("decided here. Adjudicate the rest by reading claim vs ACTUAL (--json).")

    if json_out:
        with open(json_out, "w") as f:
            json.dump({"resolved": resolved, "foreign": foreign,
                       "ambiguous": ambiguous, "form_b": b_hits,
                       "form_c": c_hits, "exempt": pragma_hits}, f, indent=1)
        if not quiet:
            print(f"\nadjudication table -> {json_out}")
    return {"resolved": resolved, "oor": oor, "foreign": foreign,
            "ambiguous": ambiguous, "b": b_hits, "c": c_hits, "exempt": pragma_hits}


def added_lines(root, args):
    """(path, text) for lines ADDED by the staged diff or a rev range."""
    cmd = ["diff", "-U0"]
    if args.staged:
        cmd.append("--cached")
    if args.range:
        cmd.append(args.range)
    diff = git(cmd + ["--"] + SCAN_DIRS, root, "diff")
    path = None
    for ln in diff.splitlines():
        if ln.startswith("+++ b/"):
            path = ln[6:]
        elif ln.startswith("+") and not ln.startswith("+++") and path:
            yield path, ln[1:]


def gate(root, args):
    files = tracked(root)
    by_base = basename_map(files)
    findings = []
    for path, text in added_lines(root, args):
        if path == SELF:
            continue
        for form in forms_on(text, files, by_base):
            findings.append((path, form, text.strip()))
    if not findings:
        print("check-line-citations: no new line-number citations in added lines. OK")
        return 0
    print("check-line-citations: NEW line-number citations were added.", file=sys.stderr)
    print("", file=sys.stderr)
    for path, form, text in findings:
        print(f"  [{form}] {path}", file=sys.stderr)
        print(f"      {text[:150]}", file=sys.stderr)
    print("", file=sys.stderr)
    print("A line number is a claim about a file that keeps moving; it rots without",
          file=sys.stderr)
    print("anyone touching this file, and nothing ever fails. (issue #310)", file=sys.stderr)
    print("", file=sys.stderr)
    print("DELETE the number -- do NOT re-point it at a fresh one. Cite a function or",
          file=sys.stderr)
    print("struct name plus a short quoted phrase from the target instead.", file=sys.stderr)
    print("Worked example and the `citation-ok` escape: CONTRIBUTING.md,", file=sys.stderr)
    print("section 'The line-number citation gate'.", file=sys.stderr)
    return 1


# ── Self-test ────────────────────────────────────────────────────────────────
#
# Before believing any zero, prove the instrument can report non-zero. Both
# decision paths are covered: `forms_on` (which the gate uses) and `census`
# end-to-end on a throwaway git repo (whose out-of-range arm is the only
# mechanical verdict this tool emits, and so the only zero that must be earned).

FORM_CASES = [
    ("// see session.cpp:1258 for the thunk",                        ["A"]),
    ("// (src/wire/offset_table.cpp:440-443) differs",               ["A"]),
    ("// silent-drop at line 2234, then Stage-2",                    ["B"]),
    ("// initiator honor block (~line 3232)",                        ["B"]),
    ("// returns the status error (:532-534)",                       ["C"]),
    ("// both session.cpp:12 and at line 99",                        ["A", "B"]),
    # A citation sharing a line with a vendored include or gtest is STILL a
    # citation. A line-scoped foreign test got all four of these wrong.
    ("// the branch (session.cpp:225-232) uses gtest's ADD_FAILURE", ["A"]),
    ("#include <asio/co_spawn.hpp>  // spawned at session.cpp:916",  ["A"]),
    # Genuinely foreign: no such file in this tree.
    ("// mirroring QuickFIX's DataDictionary.cpp:271-273",           []),
    ("// asio/impl/io_context.hpp:88 tests now < abs",               []),
    # Exemptions and near-misses that must NOT fire.
    ("// session.cpp:1258 citation-ok reviewed 2026-08-28",          []),
    ("// the ratio is 3:2 across the board",                         []),
    ("// see line 9 of the fixture",                                 []),
    ("// std::array<std::uint16_t, 16> group_view",                  []),
    ("// timeout is 120s and the port is 8080",                      []),
]


def self_test():
    files = ["src/session/session.cpp", "src/wire/offset_table.cpp"]
    by_base = basename_map(files)
    bad = 0
    print("forms_on() -- the decision the GATE makes:")
    for text, want in FORM_CASES:
        got = forms_on(text, files, by_base)
        ok = got == want
        bad += not ok
        print(f"  {'ok  ' if ok else 'FAIL'} want={want!s:11} got={got!s:11}  {text[:52]}")

    print("\ncensus() end-to-end on a throwaway repo -- the OUT-OF-RANGE verdict:")
    with tempfile.TemporaryDirectory() as d:
        os.makedirs(os.path.join(d, "src"))
        os.makedirs(os.path.join(d, "include"))
        # 3 lines long, so :99 cannot resolve and :2 can.
        open(os.path.join(d, "include", "target.hpp"), "w").write("a\nb\nc\n")
        open(os.path.join(d, "src", "citer.cpp"), "w").write(
            "// rotted past EOF: target.hpp:99\n"
            "// resolves fine: target.hpp:2\n"
            "// foreign: DataDictionary.cpp:271 via QuickFIX\n"
            "// exempted: target.hpp:99 citation-ok\n")
        for a in (["init", "-q"], ["add", "-A"]):
            subprocess.run(["git"] + a, cwd=d, capture_output=True, check=True)
        r = census(d, None, quiet=True)
        checks = [
            ("out-of-range found",   len(r["oor"]) == 1),
            ("in-range not flagged", len(r["resolved"]) == 2),
            ("foreign excluded",     len(r["foreign"]) == 1),
            ("citation-ok bucketed", len(r["exempt"]) == 1),
        ]
        for label, ok in checks:
            bad += not ok
            print(f"  {'ok  ' if ok else 'FAIL'} {label}")

    total = len(FORM_CASES) + 4
    print(f"\nself-test: {total - bad}/{total} pass")
    if bad:
        print("SELF-TEST FAILED -- the instrument does not behave as documented.")
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--census", action="store_true",
                   help="sweep the tree, report candidates for human adjudication")
    g.add_argument("--staged", action="store_true",
                   help="gate: fail if the staged diff ADDS a line-number citation")
    g.add_argument("--range", metavar="A..B",
                   help="gate: fail if the rev range ADDS a line-number citation")
    g.add_argument("--self-test", action="store_true",
                   help="prove the detector reports non-zero on known positives")
    ap.add_argument("--json", metavar="OUT", help="write the adjudication table")
    ap.add_argument("--root", default=None, help="repo root (default: git toplevel)")
    args = ap.parse_args()

    if args.self_test:
        return self_test()
    root = args.root or git(["rev-parse", "--show-toplevel"], None, "rev-parse").strip()
    if args.census:
        census(root, args.json)
        return 0
    return gate(root, args)


if __name__ == "__main__":
    sys.exit(main())
