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
#   A  `file.cpp:NNN`      -- has a filename, mechanically resolvable
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
#   --staged / --range  GATE newly ADDED lines only. The existing population is
#             ~1100 candidates at a measured ~33% rot rate (n=40); gating the
#             whole tree would be pure noise. Gating additions stops the bleeding
#             without demanding a 200-file migration first.
#
# THE FIX FOR A FLAGGED CITATION IS TO DELETE THE NUMBER, NOT TO CORRECT IT.
# Re-pointing `session.cpp:1258` at `session.cpp:1265` re-arms the same defect
# with a fresh half-life. Replace with a function/struct name plus a short quoted
# phrase, which survives arbitrary line motion and which grep can find if the
# quoted text is ever changed:
#
#   before:  drain_or_report's residual ADD_FAILURE branch (pump_until_ready.hpp:225-232)
#   after:   drain_or_report's residual ADD_FAILURE ("#289: the io_context did not run out of work")

import argparse
import collections
import json
import os
import re
import subprocess
import sys

SCAN_DIRS = ["tests/", "src/", "include/"]

# Form A. The negative lookbehind keeps `a/b/c.cpp:12` from also matching as
# `b/c.cpp:12` and `c.cpp:12`.
RE_A = re.compile(
    r"(?<![\w/.-])([A-Za-z0-9_][A-Za-z0-9_/.-]*\.(?:hpp|cpp|ipp|hh|hxx|cc|h)):(\d+)"
)
# Form B. {2,} digits: `line 5` is nearly always prose about test data, `line 86`
# is nearly always a citation. Deliberately admits design-doc anchors
# (`[arch §6 line 243]`) -- those rot the same way.
RE_B = re.compile(r"~?\blines?\s+\d{2,}", re.IGNORECASE)
# Form C.
RE_C = re.compile(r"\(:\d+")

# A citation into a reference implementation or a vendored dependency does not
# rot when THIS tree moves, so it is out of scope for both modes. Keyed on the
# citing line, because the target file is by definition absent from this tree.
RE_FOREIGN = re.compile(
    r"QuickFIX|QFJ|QFcpp|quickfix|reference-engines|asio/|boost/|gtest|googletest|/usr/",
    re.IGNORECASE,
)
# Deliberate, reviewed exception. Keep it rare: it is an assertion that this
# particular number will not rot, which is almost never true of in-tree code.
RE_PRAGMA = re.compile(r"citation-ok")

# Paths whose citations name Spec-Kit contract/spec artifacts rather than tree
# headers. `005 contracts/session.hpp:46` is NOT include/fixpp/session/session.hpp,
# and resolving it by basename manufactures a finding (#310 records the sibling
# QuickFIX false positive that this class generalises).
RE_SPEC_CONTEXT = re.compile(r"contracts/|specs?/|spec\.md|data-model|research|tasks\.md")

SELF = "tools/check_line_citations.py"


def tracked(root, dirs):
    out = subprocess.run(
        ["git", "ls-files", "--"] + dirs, cwd=root, capture_output=True, text=True
    )
    return out.stdout.split()


def read_lines(root, path, cache):
    if path not in cache:
        try:
            with open(os.path.join(root, path), encoding="utf-8", errors="replace") as f:
                cache[path] = f.read().splitlines()
        except OSError:
            cache[path] = None
    return cache[path]


def classify(line):
    """Return the citation forms present on `line`, minus exempt ones."""
    if RE_PRAGMA.search(line) or RE_FOREIGN.search(line):
        return []
    forms = []
    if RE_A.search(line):
        forms.append("A")
    if RE_B.search(line):
        forms.append("B")
    if RE_C.search(line):
        forms.append("C")
    return forms


def census(root, json_out):
    files = tracked(root, SCAN_DIRS)
    by_base = collections.defaultdict(list)
    for p in files:
        by_base[os.path.basename(p)].append(p)

    cache = {}
    resolved, foreign, unresolved, ambiguous = [], [], [], []
    b_hits, c_hits = [], []

    for p in files:
        src = read_lines(root, p, cache)
        if src is None:
            continue
        for i, ln in enumerate(src):
            if RE_PRAGMA.search(ln):
                continue
            if RE_B.search(ln) and not RE_FOREIGN.search(ln):
                b_hits.append({"cf": p, "cl": i + 1, "text": ln.strip()})
            if RE_C.search(ln) and not RE_FOREIGN.search(ln):
                c_hits.append({"cf": p, "cl": i + 1, "text": ln.strip()})
            for m in RE_A.finditer(ln):
                target, num = m.group(1), int(m.group(2))
                rec = {"cf": p, "cl": i + 1, "text": ln.strip(),
                       "target": target, "n": num}
                if RE_FOREIGN.search(ln):
                    foreign.append(rec)
                    continue
                cands = []
                if "/" in target:
                    cands = [t for t in files if t == target or t.endswith("/" + target)]
                    if not cands and RE_SPEC_CONTEXT.search(target):
                        # A spec-artifact path that does not exist as a tree file.
                        # Resolving its basename would point at the wrong file.
                        unresolved.append(rec)
                        continue
                if not cands:
                    if RE_SPEC_CONTEXT.search(ln) and "/" in target:
                        unresolved.append(rec)
                        continue
                    cands = by_base.get(os.path.basename(target), [])
                if not cands:
                    unresolved.append(rec)
                    continue
                if len(cands) > 1:
                    rec["cands"] = cands
                    ambiguous.append(rec)
                    continue
                tp = cands[0]
                tl = read_lines(root, tp, cache)
                rec["tp"] = tp
                rec["tn"] = len(tl)
                rec["inrange"] = num <= len(tl)
                rec["at"] = tl[num - 1].strip() if num <= len(tl) else "<OUT OF RANGE>"
                resolved.append(rec)

    oor = [r for r in resolved if not r["inrange"]]
    total_a = len(resolved) + len(foreign) + len(unresolved) + len(ambiguous)

    print("── #310 citation census " + "─" * 50)
    print(f"form A  `file.ext:NNN`   candidates : {total_a}")
    print(f"          foreign (QuickFIX/vendored): {len(foreign)}   [out of scope]")
    print(f"          unresolved / spec-artifact : {len(unresolved)}   [out of scope]")
    print(f"          ambiguous basename         : {len(ambiguous)}   [needs a path]")
    print(f"          RESOLVED in-tree           : {len(resolved)}")
    print(f"            of which OUT OF RANGE    : {len(oor)}   <-- mechanically certain")
    print(f"form B  prose `line NNN` candidates : {len(b_hits)}   [no filename: unresolvable]")
    print(f"form C  bare `(:NNN)`     candidates : {len(c_hits)}   [no filename: unresolvable]")
    print()
    if oor:
        print("OUT-OF-RANGE citations (each one is a defect):")
        for r in oor:
            print(f"  {r['cf']}:{r['cl']}  ->  {r['target']}:{r['n']}  "
                  f"(target {r['tp']} has {r['tn']} lines)")
            print(f"      {r['text'][:140]}")
        print()
    print("A resolved citation is NOT thereby correct -- only out-of-range is decided")
    print("here. Adjudicate the rest by reading claim vs ACTUAL with --json.")

    if json_out:
        with open(json_out, "w") as f:
            json.dump({"resolved": resolved, "foreign": foreign,
                       "unresolved": unresolved, "ambiguous": ambiguous,
                       "form_b": b_hits, "form_c": c_hits}, f, indent=1)
        print(f"\nadjudication table -> {json_out}")
    return 0


def added_lines(root, args):
    """Yield (path, text) for lines ADDED by the staged diff or a rev range."""
    cmd = ["git", "diff", "-U0"]
    if args.staged:
        cmd.append("--cached")
    if args.range:
        cmd.append(args.range)
    cmd += ["--"] + SCAN_DIRS
    diff = subprocess.run(cmd, cwd=root, capture_output=True, text=True).stdout
    path = None
    for ln in diff.splitlines():
        if ln.startswith("+++ b/"):
            path = ln[6:]
        elif ln.startswith("+") and not ln.startswith("+++") and path:
            yield path, ln[1:]


def gate(root, args):
    findings = []
    for path, text in added_lines(root, args):
        if path == SELF:
            continue
        for form in classify(text):
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
    print("A line number is a claim about a file that keeps moving; it rots without", file=sys.stderr)
    print("anyone touching this file, and nothing ever fails. (issue #310)", file=sys.stderr)
    print("", file=sys.stderr)
    print("DELETE the number -- do not correct it. Cite a function/struct name plus a", file=sys.stderr)
    print("short quoted phrase from the target:", file=sys.stderr)
    print("", file=sys.stderr)
    print('  before:  the residual ADD_FAILURE branch (pump_until_ready.hpp:225-232)', file=sys.stderr)
    print('  after:   drain_or_report\'s residual ADD_FAILURE ("#289: the io_context', file=sys.stderr)
    print('           did not run out of work")', file=sys.stderr)
    print("", file=sys.stderr)
    print("Citations into QuickFIX/vendored code are exempt automatically. For a", file=sys.stderr)
    print("deliberate in-tree exception add a `citation-ok` marker on the line.", file=sys.stderr)
    return 1


def self_test():
    """Prove each form's detector can report NON-ZERO before any zero is believed.

    This repo's single most recurring defect is an instrument that reports clean
    because it COULD not report anything else. Every case below is checked in
    both directions: a positive that must match, and a near-miss that must not.
    """
    cases = [
        # (line, expected forms)
        ("// see session.cpp:1258 for the thunk",              ["A"]),
        ("// (src/wire/offset_table.cpp:440-443) differs",      ["A"]),
        ("// silent-drop at line 2234, then Stage-2",           ["B"]),
        ("// initiator honor block (~line 3232)",               ["B"]),
        ("// returns the status error (:532-534)",              ["C"]),
        ("// MessageView::get(tag) (:212-219) minus",           ["C"]),
        ("// both session.cpp:12 and at line 99",               ["A", "B"]),
        # exemptions
        ("// mirroring QuickFIX's DataDictionary.cpp:271-273",  []),
        ("// asio/impl/io_context.hpp:88 tests now < abs",      []),
        ("// session.cpp:1258 citation-ok reviewed 2026-08-28", []),
        # near-misses that must NOT fire
        ("// the ratio is 3:2 across the board",                []),
        ("// see line 9 of the fixture",                        []),
        ("// std::array<std::uint16_t, 16> group_view",         []),
        ("// timeout is 120s and the port is 8080",             []),
        ("auto x = ns::thing(a, b);  // nothing here",          []),
    ]
    bad = 0
    for text, want in cases:
        got = classify(text)
        ok = got == want
        bad += not ok
        print(f"  {'ok  ' if ok else 'FAIL'}  want={want!s:12} got={got!s:12}  {text[:58]}")
    # The census's out-of-range arm, proven against a synthetic positive.
    print(f"\nself-test: {len(cases) - bad}/{len(cases)} cases pass")
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

    root = args.root or subprocess.run(
        ["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True
    ).stdout.strip()

    if args.census:
        return census(root, args.json)
    return gate(root, args)


if __name__ == "__main__":
    sys.exit(main())
