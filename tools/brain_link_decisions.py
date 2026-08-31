#!/usr/bin/env python3
"""Propose `refs_external` decision-record links for brain pages.

WHY THIS IS A TOOL AND NOT A ONE-OFF.  The `refs_external` tier -- the private
parent's Gate A / convergence records, i.e. WHY a decision was taken and what was
REJECTED -- is the half of this bundle that the code cannot supply, and it is the
half most likely to sit empty: a new page starts with zero, and nothing complains.
It sat at 5 refs across 2 of 18 pages for exactly that reason.  Re-run this
whenever pages are added.

DERIVATION, not judgement.  A record is proposed only when the page ALREADY names
its subject:
  * the full feature-bundle slug appears in the body  (`023-engine-session-strand`)
  * or the body says "feature 023"
  * or the body names a design doc file (`2d-threading.md`) -> its convergence story

PRECISION OVER RECALL, deliberately.  A refs_external path that exists but is
IRRELEVANT is a silent defect: the freshness gate proves paths RESOLVE, it cannot
prove they BELONG.  So bare three-digit ids are NOT matched -- `D-007`, `B-005-3`
and `NFR-001` all contain one, and matching them proposed a dozen unrelated
records on the first attempt.

`gatea` only.  Gate A is the design review: why this shape, what was rejected.
Gate B is what a hostile reviewer found in the implementation -- valuable, but it
is about code, and the code owns that.  Add a `gateb` by hand when a page's point
IS the review finding.
"""
import argparse, glob, io, os, re, sys

D = "research/G19-fix-fpml-iso20022/decisions"


def fm_list(text, key):
    m = re.search(rf"(?m)^{key}:\n((?:  - .*\n)+)", text)
    return [l[4:] for l in m.group(1).rstrip("\n").split("\n")] if m else []


def propose(root, parent):
    dec = os.path.join(parent, D)
    if not os.path.isdir(dec):
        print(f"brain-link: {dec} missing -- refusing to report 'nothing to add' "
              "when the records are simply not visible from here.", file=sys.stderr)
        return None
    bundles = [n for n in os.listdir(os.path.join(root, "specs")) if re.match(r"\d{3}-", n)]
    speck = set(os.listdir(os.path.join(dec, "speckit")))
    conv = sorted(os.listdir(dec))
    out = {}
    for f in sorted(glob.glob(os.path.join(root, "brain", "**", "*.md"), recursive=True)):
        if os.path.basename(f) == "index.md":
            continue                      # a routing index must stay one hop, not accumulate
        t = io.open(f, encoding="utf-8").read()
        if not t.startswith("---"):
            continue
        body, have = t.split("---", 2)[2], fm_list(t, "refs_external")
        ids = {b for b in bundles if b in body}
        for n in re.findall(r"(?:feature|Feature)s?\s+`?(\d{3})`?", body):
            ids |= {b for b in bundles if b.startswith(n + "-")}
        p = [f"{D}/speckit/{b}-gatea.md" for b in sorted(ids) if f"{b}-gatea.md" in speck]
        for d in sorted(set(re.findall(r"\b(2[a-m])-[\w-]+\.md", body))):
            p += [f"{D}/{n}" for n in conv if n.startswith(d + "-")]
        new = [x for x in dict.fromkeys(p)
               if x not in have and os.path.exists(os.path.join(parent, x))]
        if new:
            out[os.path.relpath(f, root)] = new
    return out


def self_test(root, parent):
    """Both directions. A proposer only ever run against under-linked pages proves
    it can speak; it does not prove it can stay quiet."""
    r = propose(root, parent)
    if r is None:
        print("self-test: could not see the records -- inconclusive, not clean", file=sys.stderr)
        return 2
    ok = True
    # the tree is currently linked, so a second run must propose nothing
    if r:
        ok = False
        print(f"  FAIL  want=0 got={len(r)}  already-linked tree -> no proposals")
    else:
        print("  ok    want=0 got=0  already-linked tree -> no proposals")
    # seeded control: a page naming a bundle with no refs_external must propose
    import tempfile, shutil
    d = tempfile.mkdtemp()
    shutil.copytree(os.path.join(root, "specs"), os.path.join(d, "specs"),
                    ignore=lambda *a: [x for x in a[1] if x != "."] if False else [])
    os.makedirs(os.path.join(d, "brain", "components"), exist_ok=True)
    bundle = sorted(n for n in os.listdir(os.path.join(root, "specs"))
                    if re.match(r"\d{3}-", n) and
                    os.path.exists(os.path.join(parent, D, "speckit", n + "-gatea.md")))[0]
    io.open(os.path.join(d, "brain", "components", "seed.md"), "w", encoding="utf-8").write(
        f"---\ntype: Component Decision Map\nstatus: stable\nrefs:\n  - x\n---\n\n"
        f"# S\n\nThis page discusses `{bundle}` and nothing else.\n")
    s = propose(d, parent)
    if s and any(bundle in x for v in s.values() for x in v):
        print(f"  ok    seeded page naming {bundle} -> proposed")
    else:
        ok = False
        print(f"  FAIL  seeded page naming {bundle} -> NOTHING proposed")
    shutil.rmtree(d)
    print("self-test:", "2/2 pass" if ok else "FAILED")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default=".")
    ap.add_argument("--parent", default=None)
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    parent = a.parent or os.path.normpath(os.path.join(a.root, "../../.."))
    if a.self_test:
        sys.exit(self_test(a.root, parent))
    r = propose(a.root, parent)
    if r is None:
        sys.exit(2)
    if not r:
        print("brain-link: nothing to propose -- every page's named records are linked.")
        sys.exit(0)
    for page, new in r.items():
        print(f"\n{page}")
        for n in new:
            print(f"  + {n}")
    print(f"\n{sum(len(v) for v in r.values())} proposal(s). Review each -- the gate can "
          "prove a path RESOLVES, never that it BELONGS. Then add by hand.")


if __name__ == "__main__":
    main()
