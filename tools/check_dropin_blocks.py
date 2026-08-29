#!/usr/bin/env python3
"""Audit Appendix-D style **Before**/**After** drop-in blocks in .specify/ design docs.

A drop-in block proposes an edit to a SIBLING document: it quotes the target's current
text ("Before") and the replacement ("After"), and an orchestrator is supposed to apply
it at sign-off.  Nothing ever re-checks whether that happened, so a block can sit in a
signed-off document for months in any of four states.  This classifies them.

WHAT THIS TOOL IS FOR, AND WHAT IT IS NOT
-----------------------------------------
It reports a STATE, not a verdict.  `NOT-APPLIED` is perfectly legitimate for a document
whose sign-off has not happened yet; `APPLIED` with a stale "Before" is normal and is what
the word "Before" means.  Only two combinations are defects on their own:

  * APPLIED  + the prose calls the stale half the *current* text of the target
  * NEITHER  -- neither half is found, so the target moved to a THIRD state that no
               document records.  This is the one that hides a shipped design change.

Even those are LEADS.  The matcher is substring-based, so whitespace reflow in the target
is indistinguishable from a real edit.  Confirm against the target and the shipped headers
before acting -- and note the target may be right while the DOC is wrong, or the reverse.

Deliberately NOT recorded anywhere: how many blocks are in which state.  That is a RESULT
and it rots; this file is the PROCEDURE, which does not.
"""
import argparse, glob, os, re, sys, tempfile

PAIR = re.compile(
    r"\*\*Before\*\*\s*\((?P<bmeta>[^\n]*)\)\s*:?\s*\n+```(?:\w*)\n(?P<before>.*?)\n```"
    r".*?\*\*After\*\*\s*\((?P<ameta>[^\n]*)\)\s*:?\s*\n+```(?:\w*)\n(?P<after>.*?)\n```",
    re.S)
FILE = re.compile(
    r"`?(?:library/)?((?:\.specify/|specs/|spec/|include/|src/)?[A-Za-z0-9_./-]+"
    r"\.(?:md|hpp|cpp|py|yaml|yml))`?")
CURRENT = re.compile(r"\bcurrent\b", re.I)


def resolve(name, root):
    for c in (name, os.path.join(".specify", os.path.basename(name)),
              name.replace("library/", "")):
        p = os.path.join(root, c)
        if os.path.isfile(p):
            return c
    g = glob.glob(os.path.join(root, "**", os.path.basename(name)), recursive=True)
    return os.path.relpath(g[0], root) if len(g) == 1 else None


def audit(root, pattern=".specify/2*.md"):
    rows = []
    for doc in sorted(glob.glob(os.path.join(root, pattern))):
        txt = open(doc, encoding="utf-8", errors="replace").read()
        for m in PAIR.finditer(txt):
            fm = FILE.search(m.group("bmeta"))
            if not fm:
                continue
            tgt = resolve(fm.group(1), root)
            line = txt[:m.start()].count("\n") + 1
            rel = os.path.relpath(doc, root)
            if tgt is None:
                rows.append((rel, line, fm.group(1), "UNRESOLVED", False))
                continue
            blob = open(os.path.join(root, tgt), encoding="utf-8",
                        errors="replace").read()
            b = m.group("before").strip() in blob
            a = m.group("after").strip() in blob
            state = ("BOTH" if (a and b) else "APPLIED" if a else
                     "NOT-APPLIED" if b else "NEITHER")
            rows.append((rel, line, tgt, state, bool(CURRENT.search(m.group("bmeta")))))
    return rows


def verdict(state, claims_current):
    if state == "UNRESOLVED":
        return "target path does not resolve"
    if state == "APPLIED" and claims_current:
        return "prose calls the stale half the CURRENT target text"
    if state == "NEITHER":
        return "neither half matches -- target is in a THIRD state"
    return ""


def report(rows, only_suspect=False):
    n = 0
    print("%-24s %6s %-26s %-12s %s" % ("doc", "line", "target", "state", "verdict"))
    for rel, line, tgt, state, cur in rows:
        v = verdict(state, cur)
        if only_suspect and not v:
            continue
        n += v != ""
        print("%-24s %6d %-26s %-12s %s" % (os.path.basename(rel), line,
                                            os.path.basename(tgt), state, v))
    return n


SELF_TEST_TARGET = "ORIGINAL LINE\nSHARED LINE\n"
SELF_TEST_DOC = """# fixture

**Before** (current `target.md` text, lines 1-2):

```
ORIGINAL LINE
SHARED LINE
```

**After** (drop-in):

```
REPLACED LINE
SHARED LINE
```
"""


def self_test():
    """Prove the classifier reports every state -- a checker that can only say one
    thing is the failure mode this repository keeps re-encountering."""
    fails = []
    with tempfile.TemporaryDirectory() as d:
        os.makedirs(os.path.join(d, ".specify"))
        doc = os.path.join(d, ".specify", "2x-fixture.md")
        tgt = os.path.join(d, ".specify", "target.md")

        cases = [
            ("NOT-APPLIED", "ORIGINAL LINE\nSHARED LINE\n"),
            ("APPLIED",     "REPLACED LINE\nSHARED LINE\n"),
            ("NEITHER",     "SOMETHING ELSE ENTIRELY\n"),
            ("BOTH",        "ORIGINAL LINE\nSHARED LINE\nREPLACED LINE\nSHARED LINE\n"),
        ]
        for want, content in cases:
            open(doc, "w").write(SELF_TEST_DOC)
            open(tgt, "w").write(content)
            rows = audit(d)
            if len(rows) != 1:
                fails.append("%s: expected 1 block, parsed %d" % (want, len(rows)))
                continue
            got = rows[0][3]
            if got != want:
                fails.append("%s: classifier said %s" % (want, got))

        # the two defect verdicts must actually fire, and must NOT fire otherwise
        if not verdict("APPLIED", True):
            fails.append("APPLIED+current produced no verdict")
        if verdict("APPLIED", False):
            fails.append("APPLIED without a currency claim wrongly flagged")
        if not verdict("NEITHER", False):
            fails.append("NEITHER produced no verdict")
        if verdict("NOT-APPLIED", True):
            fails.append("NOT-APPLIED wrongly flagged -- it is legitimate pre-sign-off")

        # a doc with no drop-in blocks must yield nothing, not crash
        open(doc, "w").write("# nothing here\n")
        if audit(d):
            fails.append("a doc with no blocks produced rows")

    total = 9
    print("self-test: %d/%d passed" % (total - len(fails), total))
    for f in fails:
        print("  FAIL", f)
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--census", action="store_true", help="classify every block")
    g.add_argument("--suspect", action="store_true", help="only blocks with a verdict")
    g.add_argument("--self-test", action="store_true")
    ap.add_argument("--root", default=None)
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    root = a.root or os.popen("git rev-parse --show-toplevel").read().strip() or "."
    rows = audit(root)
    if not rows:
        print("check-dropin-blocks: parsed ZERO Before/After blocks under .specify/.",
              file=sys.stderr)
        print("That is a broken matcher, not a clean tree -- these docs have them.",
              file=sys.stderr)
        return 2
    n = report(rows, only_suspect=a.suspect)
    print("\n%d block(s) parsed, %d carry a verdict. A verdict is a LEAD: confirm against "
          "the target and the shipped header before acting." % (len(rows), n))
    return 0


if __name__ == "__main__":
    sys.exit(main())
