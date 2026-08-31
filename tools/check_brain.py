#!/usr/bin/env python3
# check_brain.py — freshness/integrity instrument for the SecondBrain bundle.
#
# The bundle exists because signed-off design documents rotted. It has no
# immunity from that, and it is now load-bearing: a blind agent went from 30 tool
# calls and 0 of 3 fossils found to 9 calls and 3 of 3 by reading a component
# page. A page that rots is therefore WORSE than no page -- it is where people go
# for the fossil list.
#
# THREE CHECKS, and one of them is not about rot at all:
#
#   1 refs           an in-repo path a page names must exist. FAIL CLOSED: a path
#                    that no longer resolves is an error a human dispositions,
#                    never an auto-evict and never a warning that passes.
#   2 links          a relative markdown link must resolve.
#   3 deprecated     a concept marked `status: deprecated` must NOT be reachable
#                    from any index.md. Progressive disclosure is the whole
#                    filter: a stale claim still in reach produces confident
#                    wrongness, which is strictly worse than a missing one
#                    producing honest uncertainty.
#
# refs vs refs_external -- and why the split is NOT cosmetic. `refs` are in-repo
# and CI can see them. `refs_external` point at the PRIVATE parent repo
# (decision records, phases/), which is gitignored on the public side and simply
# ABSENT from a CI checkout. Checking them in CI would fail on every hub concept
# on the first run, and the "fix" would be to weaken rule 1 -- which is exactly
# how a fail-closed gate becomes fail-open. So they get their own subcommand,
# run locally, with its own seeded-positive proof.
#
# Pure Python `re`, never a shelled-out grep -- this machine's `rg` is a shim
# that silently returns 0 matches for any alternation pattern.
#
# WHERE THIS LIVES, and why it matters: `gate`/`census` touch only in-repo paths,
# so this file lives IN the library and CI can run it. `sweep` touches the private
# parent and is local-only -- it is a subcommand rather than a separate tool so the
# refs/refs_external split stays visible in one place.
#
# Exit codes: 0 pass · 1 finding · 2 instrument error.

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

DEFAULT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUNDLE = "brain"
PARENT_OF_ROOT = "../../.."  # refs_external are relative to the parent repo root

RE_FM = re.compile(r"\A---\n(.*?)\n---", re.S)
RE_LIST_KEY = r"^{key}:\n((?:[ \t]+-[ \t]*\S.*\n)+)"
RE_STATUS = re.compile(r"^status:[ \t]*(\S+)", re.M)
RE_MDLINK = re.compile(r"\[[^\]]*\]\((\.{1,2}/[^)#]+)")


def die(code, msg):
    print(msg, file=sys.stderr)
    sys.exit(code)


def frontmatter(text):
    m = RE_FM.match(text)
    if not m:
        return None
    # Trailing newline restored deliberately: the capture stops BEFORE the closing
    # `---`, so the last list item would have no `\n` and a line-anchored list
    # regex would silently drop it. That is a false CLEAN -- a page whose only ref
    # is its last line would be reported as having none. Caught by the self-test.
    return m.group(1) + "\n"


def list_key(fm, key):
    m = re.search(RE_LIST_KEY.format(key=key), fm, re.M)
    if not m:
        return []
    return [ln.strip().lstrip("-").strip() for ln in m.group(1).strip().split("\n")]


def load_pages(root):
    """[(relpath, frontmatter, body)] for every .md in the bundle."""
    base = os.path.join(root, BUNDLE)
    if not os.path.isdir(base):
        die(2, f"check-brain: no bundle at {base} -- refusing to report a missing "
               "bundle as clean. Wrong --root, or the bundle moved.")
    pages = []
    for dirpath, _, names in os.walk(base):
        for n in sorted(names):
            if not n.endswith(".md"):
                continue
            p = os.path.join(dirpath, n)
            try:
                text = open(p, encoding="utf-8").read()
            except OSError as e:
                die(2, f"check-brain: cannot read {p}: {e}")
            fm = frontmatter(text)
            if fm is None:
                die(2, f"check-brain: {os.path.relpath(p, root)} has no YAML "
                       "frontmatter. Every concept needs one; refusing to skip it "
                       "silently, which would exempt it from every check below.")
            pages.append((os.path.relpath(p, root), fm, text))
    if not pages:
        die(2, f"check-brain: parsed ZERO pages under {base}. A scan that finds "
               "nothing reports 'no findings' and reads as clean -- that is an "
               "instrument error, not a pass.")
    return pages


# ── rule 4: refs must be in SOURCE-PRIORITY order ────────────────────────────
#
# A concept page routes; the order it routes IN is the routing. `refs` is read
# top-down by a human in a hurry, so the most authoritative source has to be
# first. The tiers, per plan step A2:
#
#   0  include/ , src/          the code. Authoritative -- everything else is a
#                               claim ABOUT this.
#   1  .specify/               design docs + constitution: the WHY, and what was
#                               rejected. The half that does not rot.
#   2  specs/<id>/             the per-feature bundle that shipped it.
#   3  spec/behaviors-*        shipped behaviour a user must know.
#   4  anything else in-repo.
#
# Decision records are `refs_external` and CodeGraph symbols are `codegraph_entry`
# -- separate keys, so their position is fixed by construction and needs no rule.
#
# Enforced rather than documented because an ordering convention that is merely
# written down is one nobody can tell has been broken.

def ref_tier(path):
    if path.startswith(("include/", "src/")):
        return 0
    if path.startswith(".specify/"):
        return 1
    if path.startswith("specs/"):
        return 2
    if path.startswith("spec/behaviors"):
        return 3
    return 4


def check(root, pages):
    """(findings, counts). A finding is (kind, page, detail)."""
    findings = []
    n_refs = n_links = 0

    deprecated = {p for p, fm, _ in pages
                  if (m := RE_STATUS.search(fm)) and m.group(1) == "deprecated"}

    for rel, fm, body in pages:
        for r in list_key(fm, "refs"):
            n_refs += 1
            if not os.path.exists(os.path.join(root, r)):
                findings.append(("refs", rel, r))

        tiers = [ref_tier(r) for r in list_key(fm, "refs")]
        if tiers != sorted(tiers):
            findings.append(("ref-order", rel,
                             " -> ".join(f"{r}(t{ref_tier(r)})"
                                         for r in list_key(fm, "refs"))))

        for link in RE_MDLINK.findall(body):
            n_links += 1
            tgt = os.path.normpath(os.path.join(os.path.dirname(os.path.join(root, rel)), link))
            if not os.path.exists(tgt):
                findings.append(("link", rel, link))

        # rule 3 -- an index must not reach a deprecated concept
        if os.path.basename(rel) == "index.md":
            for link in RE_MDLINK.findall(body):
                tgt = os.path.normpath(os.path.join(os.path.dirname(os.path.join(root, rel)), link))
                trel = os.path.relpath(tgt, root)
                if trel in deprecated:
                    findings.append(("deprecated-reachable", rel, trel))

    return findings, dict(pages=len(pages), refs=n_refs, links=n_links,
                          deprecated=len(deprecated))


def sweep(root, parent):
    """refs_external -- LOCAL ONLY. Never run in CI: these paths are absent there."""
    pages = load_pages(root)
    findings, n = [], 0
    for rel, fm, _ in pages:
        for r in list_key(fm, "refs_external"):
            n += 1
            if not os.path.exists(os.path.join(parent, r)):
                findings.append(("refs_external", rel, r))
    if n == 0:
        print("check-brain sweep: no refs_external declared.")
        return 0
    if findings:
        print(f"check-brain sweep: FAIL -- {len(findings)} unresolvable external ref(s):",
              file=sys.stderr)
        for kind, page, d in findings:
            print(f"  {page}: {d}", file=sys.stderr)
        print("\nThese point at the PRIVATE parent repo. Unresolvable here means the "
              "record moved or was deleted -- fix the page; do not delete the ref to "
              "make this quiet.", file=sys.stderr)
        return 1
    print(f"check-brain sweep: OK -- {n} external ref(s) resolve.")
    return 0


def report(findings, counts, quiet=False):
    if not quiet:
        print("── SecondBrain bundle ─────────────────────────────────────────")
        for k in ("pages", "refs", "links", "deprecated"):
            print(f"{k:<26}: {counts[k]}")
    if not findings:
        return 0
    print(f"\ncheck-brain: FAIL -- {len(findings)} finding(s):", file=sys.stderr)
    for kind, page, d in findings:
        if kind == "refs":
            print(f"  [refs]  {page}: '{d}' does not exist", file=sys.stderr)
        elif kind == "link":
            print(f"  [link]  {page}: '{d}' does not resolve", file=sys.stderr)
        elif kind == "ref-order":
            print(f"  [ref-order] {page}: refs are not in source-priority order.\n"
                  f"              {d}\n"
                  "              Order: code(0) -> .specify(1) -> specs(2) -> "
                  "B&L(3) -> other(4).", file=sys.stderr)
        else:
            print(f"  [deprecated-reachable] {page} links '{d}', which is "
                  "status: deprecated", file=sys.stderr)
    print("\nA deprecated concept reachable from an index defeats progressive "
          "disclosure: a stale claim in reach produces CONFIDENT WRONGNESS, which "
          "is worse than a missing one producing honest uncertainty. Unlink it; "
          "do not delete it -- deletion is what makes a stale link fail silently.",
          file=sys.stderr)
    return 1


# ── self-test ────────────────────────────────────────────────────────────────

def _w(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "w", encoding="utf-8").write(text)


def _tree(d, extra_page=None, index_links="", dep_status="stable"):
    _w(os.path.join(d, "src", "real.cpp"), "int main(){}\n")
    _w(os.path.join(d, BUNDLE, "index.md"),
       f"---\ntype: Routing Index\nstatus: stable\n---\n\n# I\n\n"
       f"- [c](./components/c.md)\n{index_links}\n")
    _w(os.path.join(d, BUNDLE, "components", "c.md"),
       "---\ntype: Component Decision Map\nstatus: stable\nrefs:\n  - src/real.cpp\n---\n\n# C\n")
    if extra_page:
        _w(os.path.join(d, BUNDLE, "components", "old.md"),
           f"---\ntype: Component Decision Map\nstatus: {dep_status}\n---\n\n# Old\n")


def self_test():
    ok = fail = 0

    def expect(label, got, want):
        nonlocal ok, fail
        good = got == want
        print(f"  {'ok  ' if good else 'FAIL'} want={want} got={got}  {label}")
        ok, fail = ok + good, fail + (not good)

    print("gate() on throwaway bundles -- each rule, positive AND negative:\n")

    # clean
    d = tempfile.mkdtemp()
    _tree(d)
    f, c = check(d, load_pages(d))
    expect("clean tree -> 0 findings", len(f), 0)
    shutil.rmtree(d)

    # rule 1 -- a refs path that stopped existing
    d = tempfile.mkdtemp()
    _tree(d)
    os.remove(os.path.join(d, "src", "real.cpp"))
    f, _ = check(d, load_pages(d))
    expect("deleted refs target -> exactly 1 [refs]", [x[0] for x in f], ["refs"])
    shutil.rmtree(d)

    # rule 2 -- a link that resolves nowhere
    d = tempfile.mkdtemp()
    _tree(d, index_links="- [gone](./components/gone.md)\n")
    f, _ = check(d, load_pages(d))
    expect("broken link -> exactly 1 [link]", [x[0] for x in f], ["link"])
    shutil.rmtree(d)

    # rule 3 -- deprecated reachable from an index, and the same page NOT linked
    d = tempfile.mkdtemp()
    _tree(d, extra_page=True, dep_status="deprecated",
          index_links="- [old](./components/old.md)\n")
    f, _ = check(d, load_pages(d))
    expect("deprecated linked from index -> 1 finding",
           [x[0] for x in f], ["deprecated-reachable"])
    shutil.rmtree(d)

    d = tempfile.mkdtemp()
    _tree(d, extra_page=True, dep_status="deprecated")   # present but unlinked
    f, _ = check(d, load_pages(d))
    expect("deprecated present but UNLINKED -> 0 findings (the intended state)",
           len(f), 0)
    shutil.rmtree(d)

    # rule 4 -- refs out of source-priority order.
    #
    # Both directions are asserted. A tier check that only ever sees a WRONG
    # order proves it can fire; it does not prove it can stay quiet, and a rule
    # that fires on correct input gets disabled within a week.
    d = tempfile.mkdtemp()
    _tree(d)
    page = os.path.join(d, "brain", "components", "c.md")
    good = ("---\ntype: Component Decision Map\nstatus: stable\nrefs:\n"
            "  - src/real.cpp\n  - .specify/2d-threading.md\n"
            "  - specs/010-x/spec.md\n---\n\n# C\n")
    bad = ("---\ntype: Component Decision Map\nstatus: stable\nrefs:\n"
           "  - .specify/2d-threading.md\n  - src/real.cpp\n"
           "  - specs/010-x/spec.md\n---\n\n# C\n")
    for rel in ("src/real.cpp", ".specify/2d-threading.md", "specs/010-x/spec.md"):
        _w(os.path.join(d, rel), "x")
    _w(page, good)
    f, _ = check(d, load_pages(d))
    expect("refs in tier order -> 0 findings", len(f), 0)
    _w(page, bad)
    f, _ = check(d, load_pages(d))
    expect("design doc listed BEFORE the code -> exactly 1 [ref-order]",
           [x[0] for x in f], ["ref-order"])
    shutil.rmtree(d)

    print("\nfail-closed paths -- ambiguity is exit 2, never a silent pass:\n")

    for label, mutate in (
        ("no bundle directory at all", lambda d: shutil.rmtree(os.path.join(d, BUNDLE))),
        ("a page with NO frontmatter",
         lambda d: _w(os.path.join(d, BUNDLE, "bare.md"), "# no frontmatter\n")),
    ):
        d = tempfile.mkdtemp()
        _tree(d)
        mutate(d)
        r = subprocess.run([sys.executable, os.path.abspath(__file__), "gate", "--root", d],
                           capture_output=True)
        expect(label, r.returncode, 2)
        shutil.rmtree(d)

    print("\nrefs_external -- its OWN seeded positive (rule 1's proof does not cover it):\n")
    d = tempfile.mkdtemp()
    par = tempfile.mkdtemp()
    _tree(d)
    _w(os.path.join(d, BUNDLE, "components", "x.md"),
       "---\ntype: Component Decision Map\nstatus: stable\nrefs_external:\n"
       "  - decisions/rec.md\n---\n\n# X\n")
    _w(os.path.join(par, "decisions", "rec.md"), "rec\n")
    expect("external ref resolves -> 0", sweep(d, par), 0)
    os.remove(os.path.join(par, "decisions", "rec.md"))
    expect("external ref deleted -> 1", sweep(d, par), 1)
    shutil.rmtree(d); shutil.rmtree(par)

    print(f"\nself-test: {ok}/{ok+fail} pass")
    return 0 if fail == 0 else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--self-test", action="store_true")
    sub = ap.add_subparsers(dest="cmd")
    for name in ("census", "gate"):
        s = sub.add_parser(name)
        s.add_argument("--root", default=DEFAULT_ROOT)
    s = sub.add_parser("sweep")
    s.add_argument("--root", default=DEFAULT_ROOT)
    s.add_argument("--parent", default=None)
    a = ap.parse_args()

    if a.self_test:
        sys.exit(self_test())
    if not a.cmd:
        ap.print_help()
        sys.exit(2)

    if a.cmd == "sweep":
        parent = a.parent or os.path.normpath(os.path.join(a.root, PARENT_OF_ROOT))
        sys.exit(sweep(a.root, parent))

    pages = load_pages(a.root)
    findings, counts = check(a.root, pages)
    sys.exit(report(findings, counts, quiet=(a.cmd == "gate")))


if __name__ == "__main__":
    main()
