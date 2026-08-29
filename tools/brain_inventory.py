#!/usr/bin/env python3
"""Derive SecondBrain's coverage inventory. Nothing here is hand-listed.

Two axes, because a component list alone produced a confident document with the FIX
session engine missing from it (see the plan, Step A1):

  COMPONENTS  catalogue family -> feature bundles -> design docs those bundles cite
              -> does a brain/components/ page cover it?
  FLOWS       long-lived coroutines (run_*/drive_* returning awaitable) and public
              entry points -> does a brain page name them?

WHY DERIVED AND NOT WRITTEN DOWN
--------------------------------
A hand-maintained inventory is a SELECTOR, and a selector is an assertion: a subsystem
omitted from it yields a comprehensive-LOOKING description with an invisible hole.
Deriving it means a subsystem that ships without a design doc shows up as a GAP.

No counts live in this file. Counts are results and results rot; this is the procedure.

The family -> design-doc edge is NOT a lookup table.  It is followed:
  catalogue row's `/specify` column -> specs/<bundle>/spec.md -> the `[2x ...]` it cites.
So a new bundle wires itself in with no edit here.
"""
import argparse, glob, os, re, subprocess, sys, tempfile

ROW = re.compile(r"^\|\s*([A-Z]+-\d+)\s*\|")
CITE = re.compile(r"\[(2[a-m]|arch|const)\s*§")
CORO = re.compile(r"awaitable<(?:[^<>]|<[^<>]*>)*>\s+((?:run|drive)_[a-z0-9_]+)\s*\(")


def cells(line):
    return [c.strip() for c in line.split("|")]


HEADER = re.compile(r"^\|\s*ID\s*\|")


def columns(path):
    """Resolve column indices BY NAME from the table header.

    They were hardcoded once and were off by one -- `/specify` was read from the `PR`
    column, so every bundle lookup missed and the tool reported "no design doc" for
    all 12 families.  Worse, the self-test fixture had been written from the buggy
    code rather than from the real table, so it CERTIFIED the bug.  Reading the
    header makes the error inexpressible instead of merely fixed.
    """
    for line in open(path, encoding="utf-8", errors="replace"):
        if HEADER.match(line):
            c = cells(line)
            idx = {name: i for i, name in enumerate(c)}
            need = ("Category", "/specify", "Status")
            missing = [n for n in need if n not in idx]
            if missing:
                raise SystemExit("brain-inventory: catalogue header lacks %s -- the "
                                 "table shape changed; fix the tool, do not guess."
                                 % missing)
            return idx
    raise SystemExit("brain-inventory: no catalogue header row found.")


def catalogue(root):
    """family -> {rows, bundles, statuses}. Derived from the catalogue table itself."""
    fam = {}
    p = os.path.join(root, "spec/feature-catalogue.md")
    if not os.path.isfile(p):
        return fam
    idx = columns(p)
    for line in open(p, encoding="utf-8", errors="replace"):
        if not ROW.match(line):
            continue
        c = cells(line)
        if len(c) <= max(idx["Category"], idx["/specify"], idx["Status"]):
            continue
        family, bundle, status = c[idx["Category"]], c[idx["/specify"]], c[idx["Status"]]
        d = fam.setdefault(family, {"rows": 0, "bundles": set(), "status": {}})
        d["rows"] += 1
        d["status"][status] = d["status"].get(status, 0) + 1
        b = bundle.strip("` ")
        if b and b not in ("-", "—", "n/a"):
            d["bundles"].add(b.split()[0])
    return fam


def docs_for(root, bundles):
    """Design docs a family's feature bundles actually cite -- followed, not mapped."""
    out = set()
    for b in bundles:
        for f in glob.glob(os.path.join(root, "specs", b + "*", "spec.md")):
            out |= {m.group(1) for m in CITE.finditer(
                open(f, encoding="utf-8", errors="replace").read())}
    return {d for d in out if d.startswith("2")}


def brain_pages(root):
    """page -> its text, so coverage is tested by mention rather than by a list."""
    return {os.path.basename(p): open(p, encoding="utf-8", errors="replace").read()
            for p in glob.glob(os.path.join(root, "brain/**/*.md"), recursive=True)}


def flows(root):
    out = {}
    for pat in ("src/**/*.cpp", "include/**/*.hpp"):
        for f in glob.glob(os.path.join(root, pat), recursive=True):
            for m in CORO.finditer(open(f, encoding="utf-8", errors="replace").read()):
                out.setdefault(m.group(1), os.path.relpath(f, root))
    return out


def named_by(name, pages):
    """Pages whose text CONTAINS this name.

    ⚠️ This is MENTION, not coverage, and the distinction matters. A flow listed among
    a page's participants scores identically to one the page actually explains. The
    tool cannot tell them apart and must not pretend to -- so the column says "named
    by", and a name appearing here is a lead to read the page, not proof it is
    documented. Deliberately not "renamed to covered" at some later date.
    """
    return sorted(p for p, t in pages.items() if name in t)


def run(root, gaps_only=False):
    fam, pages = catalogue(root), brain_pages(root)
    if not fam:
        print("brain-inventory: parsed ZERO catalogue families. That is a broken parser, "
              "not an empty catalogue.", file=sys.stderr)
        return 2
    fl = flows(root)
    if not fl:
        print("brain-inventory: found ZERO long-lived coroutines. Broken matcher.",
              file=sys.stderr)
        return 2

    print("== COMPONENTS  (catalogue family -> bundles -> cited design docs -> brain page)")
    print("%-16s %6s  %-26s %-16s %s" % ("family", "rows", "cited design docs",
                                         "bundles", "named by (NOT proof of coverage)"))
    ngap = 0
    for name in sorted(fam, key=lambda k: -fam[k]["rows"]):
        d = fam[name]
        dd = sorted(docs_for(root, d["bundles"]))
        pg = named_by(name, pages)
        gap = not dd and not pg
        ngap += gap
        if gaps_only and not (gap or not dd):
            continue
        print("%-16s %6d  %-26s %-16s %s%s" % (
            name, d["rows"], ",".join(dd) or "-- NONE --", len(d["bundles"]),
            ",".join(pg) or "-- none --", "   <<< GAP" if gap else ""))

    print("\n== FLOWS  (derived from long-lived coroutines; a new one appears here unedited)")
    for n in sorted(fl):
        pg = named_by(n, pages)
        print("  %-30s %-34s %s" % (n, fl[n], ",".join(pg) or "-- no brain page --"))

    print("\nA family with no cited design doc AND no page is where a component page must "
          "carry the load alone -- that is the deliverable, not an error.")
    print("⚠️  'named by' is MENTION, not coverage: a flow listed among a page's participants "
          "scores the same as one the page explains. Read the page before believing the row.")
    return 0


def self_test():
    """Prove each deriver reports non-zero AND can report a gap -- an inventory that
    can only say 'covered' would hide exactly what it exists to find."""
    fails = []
    with tempfile.TemporaryDirectory() as d:
        os.makedirs(os.path.join(d, "spec"))
        os.makedirs(os.path.join(d, "specs/004-wire-codec"))
        os.makedirs(os.path.join(d, "brain/components"))
        os.makedirs(os.path.join(d, "src"))
        # Header copied VERBATIM from spec/feature-catalogue.md. The previous fixture
        # was invented to match the code and so certified an off-by-one column bug.
        open(os.path.join(d, "spec/feature-catalogue.md"), "w").write(
            "| ID | Source | Category | Title | FIX version(s) | Spec ref | Status |"
            " /specify | PR | Tests | Verified |\n"
            "|---|---|---|---|---|---|---|---|---|---|---|\n"
            "| W-001 | OFFICIAL | wire | t | v | r | done | 004-wire-codec | #68 | t | y |\n"
            "| Z-001 | OFFICIAL | orphan | t | v | r | done | 999-nothing | #99 | t | y |\n")
        open(os.path.join(d, "specs/004-wire-codec/spec.md"), "w").write("cites [2b §4.1]\n")
        open(os.path.join(d, "brain/components/wire.md"), "w").write("about wire and run_x\n")
        open(os.path.join(d, "src/a.cpp"), "w").write(
            "asio::awaitable<void> run_x(int a) {}\nawaitable<void> drive_y() {}\n"
            # nested template -- the original [^>]* matcher stopped at the first '>'
            "asio::awaitable<expected_t<void>> run_nested() {}\n")

        fam = catalogue(d)
        if set(fam) != {"wire", "orphan"}:
            fails.append("catalogue parsed %s" % sorted(fam))
        if fam.get("wire", {}).get("rows") != 1:
            fails.append("row count wrong")
        if docs_for(d, {"004-wire-codec"}) != {"2b"}:
            fails.append("design-doc edge not followed: %s" % docs_for(d, {"004-wire-codec"}))
        if docs_for(d, {"999-nothing"}):
            fails.append("orphan bundle invented a design doc")
        f = flows(d)
        if set(f) != {"run_x", "drive_y", "run_nested"}:
            fails.append("flows parsed %s -- nested template dropped?" % sorted(f))
        pages = brain_pages(d)
        if named_by("wire", pages) != ["wire.md"]:
            fails.append("mention false negative")
        if named_by("orphan", pages):
            fails.append("mention FALSE POSITIVE -- would hide a gap")
        if named_by("drive_y", pages):
            fails.append("flow coverage false positive")
        if named_by("run_x", pages) != ["wire.md"]:
            fails.append("flow coverage false negative")
        if columns(os.path.join(d, "spec/feature-catalogue.md"))["/specify"] == 9:
            fails.append("column resolver reproduced the old off-by-one")

    print("self-test: %d/%d passed" % (10 - len(fails), 10))
    for x in fails:
        print("  FAIL", x)
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser()
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--census", action="store_true")
    g.add_argument("--gaps", action="store_true")
    g.add_argument("--self-test", action="store_true")
    ap.add_argument("--root", default=None)
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    root = a.root or subprocess.check_output(
        ["git", "rev-parse", "--show-toplevel"], text=True).strip()
    return run(root, gaps_only=a.gaps)


if __name__ == "__main__":
    sys.exit(main())
