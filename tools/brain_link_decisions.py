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
        if os.path.basename(f) == "log.md":
            continue                      # a chronological log names bundles in passing, not as its subject
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


def self_test():
    """Both directions, on a FIXED fixture -- neither the real brain nor the private
    parent. A proposer only ever run against under-linked pages proves it can speak;
    it does not prove it can stay quiet. Live freshness is the no-flag run's job."""
    import contextlib, subprocess, tempfile, shutil
    d = tempfile.mkdtemp()
    root, parent = os.path.join(d, "root"), os.path.join(d, "parent")
    b, rec = "042-fixture-bundle", f"{D}/speckit/042-fixture-bundle-gatea.md"

    def w(p, s):
        os.makedirs(os.path.dirname(p), exist_ok=True)
        io.open(p, "w", encoding="utf-8").write(s)

    def page(name, refs_ext=""):
        w(os.path.join(root, "brain", name), "---\ntype: T\nrefs:\n  - x\n"
          f"{refs_ext}---\n\n# S\n\nThis page discusses `{b}` and `2c-fixture.md`.\n")

    os.makedirs(os.path.join(root, "specs", b))
    page("components/linked.md",
         f"refs_external:\n  - {rec}\n  - {D}/2c-fixture-convergence.md\n")
    page("components/seed.md")
    page("log.md")
    with contextlib.redirect_stderr(io.StringIO()):
        none = propose(root, parent)      # parent has no records dir yet
    cli = subprocess.run([sys.executable, os.path.abspath(__file__), "--root", root,
                          "--parent", parent], capture_output=True).returncode
    w(os.path.join(parent, rec), "x\n")
    w(os.path.join(parent, D, "2c-fixture-convergence.md"), "x\n")
    r = propose(root, parent)
    shutil.rmtree(d)
    if r is None:
        print("self-test: could not see the fixture records -- inconclusive, not clean",
              file=sys.stderr)
        return 2
    want = [rec, f"{D}/2c-fixture-convergence.md"]
    arms = [("already-linked page -> no proposals", "brain/components/linked.md" not in r),
            ("seeded page naming bundle + design doc -> both proposed",
             r.get("brain/components/seed.md") == want),
            ("log.md naming the bundle -> not proposed", "brain/log.md" not in r),
            ("records dir missing -> None (inconclusive), not {}", none is None),
            ("records dir missing -> CLI exits 2, not 0", cli == 2)]
    for name, ok in arms:
        print(f"  {'ok  ' if ok else 'FAIL'}  {name}")
    ok = all(a[1] for a in arms)
    print("self-test:", f"{len(arms)}/{len(arms)} pass" if ok else "FAILED")
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
        sys.exit(self_test())
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
