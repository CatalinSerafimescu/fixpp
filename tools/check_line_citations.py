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
# THREE MODES, and the distinctions are the point:
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
#   --shift-audit  the OPPOSITE direction (#336). The three modes above are all
#             aimed at GROWTH, so an edit that INVALIDATES existing citations --
#             which adds none -- passes all three. This one asks whether a range
#             MOVED the lines other files already cite.
#
# THE FIX FOR A FLAGGED CITATION IS TO DELETE THE NUMBER, NOT TO CORRECT IT.
# Re-pointing `session.cpp:1258` at `session.cpp:1265` re-arms the same defect
# with a fresh half-life. Cite a function/struct name plus a short quoted phrase
# instead; that survives arbitrary line motion, and grep finds it if the quoted
# text is ever changed. CONTRIBUTING.md carries the worked example.

import argparse
import collections
import contextlib
import io
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
    "tools/", "bench/", "bindings/", "cmake/", ":(glob,top)*.md",
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

# Form A's target pattern, WIDENED with `md` -- used ONLY by --shift-audit, to
# decide which changed files are cited by line number. RE_A is deliberately left
# alone: widening the ADDITION gate to `.md` would start failing every future
# spec commit that writes a `constitution.md:456`-style ref, of which the tree
# already holds hundreds. Rot-detection and addition-gating want different
# populations, so they get different patterns rather than one compromise.
# The leading `.` is not cosmetic: RE_A's first-character class excludes it, so
# `.specify/2j-controlplane.md:902` matches NOTHING -- not at the dot (wrong
# class) and not one character in (the lookbehind rejects it). That silently
# hides 26 targets, every one of them a `.specify/` design doc, which is exactly
# the surface this mode exists to protect.
RE_TARGET = re.compile(
    # `[0-9]`, NOT `\d`: Python's `\d` matches Unicode decimal digits, the ERE
    # prefilter's `[0-9]` does not, and `int()` accepts them -- so `doc.md:١٢`
    # would be decided by a matcher the prefilter had already dropped. A decider
    # that out-matches its own prefilter is a silent drop.
    r"(?<![\w/.-])([A-Za-z0-9_.][A-Za-z0-9_/.-]*\.(?:hpp|cpp|ipp|hh|hxx|cc|h|md)):([0-9]+)"
)

# `@@ -a[,b] +c[,d] @@`. git OMITS the count when it is 1, which is the common
# single-line-replacement shape -- defaulting a missing count to 1 is where the
# off-by-one in a shift predicate hides.
RE_HUNK = re.compile(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@")

# Citations cannot live in these, and they are binary.
CITE_SCAN_SKIP = ("tests/fuzz/corpus/", "tests/abi/baseline/")

# --shift-audit's `git grep` PREFILTER. It only decides which lines reach
# RE_TARGET / RE_B / RE_C; those then decide. So its recall MUST be a superset of
# all three, or citations vanish before anything can judge them and the audit
# reports clean -- this repo's most repeated defect, one layer up. Deliberately
# looser than the deciders (no `\b`, no `{2,}`, and `-i` at the call site);
# over-supply costs nothing because Python re-decides. `--self-test` pins the
# superset relation, so editing a decider without editing this fails loudly.
CITE_PREFILTER = [
    r"\.(hpp|cpp|ipp|hh|hxx|cc|h|md):[0-9]",
    r"lines?[[:space:]]+[0-9][0-9]",
    r"\(:[0-9]",
]

# A `#line` preprocessor directive is not a citation -- form B's {2,}-digit
# pattern matches its line number.
RE_LINE_DIRECTIVE = re.compile(r"^\s*#\s*line\s")

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


def all_tracked(root):
    """Every tracked file in the repo -- the universe for RESOLVING a form-A
    target, kept separate from SCAN_DIRS (which bounds only what gets
    SCANNED/GATED). A legitimate citation target can live in a directory this
    tool deliberately does not scan; resolving against the SCAN_DIRS-only file
    list would bucket it 'foreign / unresolvable', a false clean for an
    in-tree file."""
    files = git(["ls-files"], root, "ls-files").split()
    if not files:
        raise SystemExit(
            "check-line-citations: git ls-files matched ZERO files. Refusing "
            "to interpret an empty measurement as a clean tree -- check the "
            "repo root.")
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
    if RE_LINE_DIRECTIVE.search(line):
        return []
    found = []
    for m in RE_A.finditer(line):
        if files is None or resolve(m.group(1), files, by_base):
            found.append("A")
            break
    if RE_B.search(line):
        found.append("B")
    if RE_C.search(line):
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
    resolve_files = all_tracked(root)
    resolve_by_base = basename_map(resolve_files)
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
            forms = forms_on(ln, resolve_files, resolve_by_base)
            if "B" in forms:
                b_hits.append({"cf": p, "cl": i + 1, "text": ln.strip()})
            if "C" in forms:
                c_hits.append({"cf": p, "cl": i + 1, "text": ln.strip()})
            for m in RE_A.finditer(ln):
                target, num = m.group(1), int(m.group(2))
                rec = {"cf": p, "cl": i + 1, "text": ln.strip(),
                       "target": target, "n": num}
                cands = resolve(target, resolve_files, resolve_by_base)
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
                rec["inrange"] = 1 <= num <= len(tl)
                rec["at"] = tl[num - 1].strip() if rec["inrange"] else "<OUT OF RANGE>"
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
    if args.range is not None:
        if not args.range.strip():
            raise SystemExit(
                "check-line-citations: --range was given an EMPTY value. Without "
                "it this would silently diff the working tree against the index "
                "and report clean; refusing to gate the wrong thing.")
        cmd.append(args.range)
    diff = git(cmd + ["--"] + SCAN_DIRS, root, "diff")
    path = None
    for ln in diff.splitlines():
        if ln.startswith("+++ b/"):
            path = ln[6:]
        elif ln.startswith("+") and not ln.startswith("+++") and path:
            yield path, ln[1:]


def gate(root, args):
    resolve_files = all_tracked(root)
    resolve_by_base = basename_map(resolve_files)
    findings = []
    for path, text in added_lines(root, args):
        if path == SELF:
            continue
        for form in forms_on(text, resolve_files, resolve_by_base):
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


# ── --shift-audit: the ROT direction ─────────────────────────────────────────
#
# Issue #336. The three modes above are all aimed at GROWTH -- they fail a diff
# that ADDS a line-number citation. An edit that INVALIDATES existing citations
# adds none, so all three pass clean. Insert a paragraph near the top of a
# document that is cited by line number and every citation into it below the
# insertion point silently re-points at the wrong content: no diff hunk anywhere
# near the citing files, no error, no warning.
#
# That is not hypothetical. Commit 9e0b332d inserted a 47-line note ~12 lines
# from the top of `.specify/2d-threading.md`, a file cited by line number from
# more than a dozen others; 4afaed04 restored them by making every amendment
# line-shift-free. The citation gate ran and passed throughout.
#
# TWO CHECKS, and they see different things:
#
#   [1] LINE-SHIFT  -- did a changed document move its own line numbers? Needs
#       no citation to resolve, so it is the ONLY check that can cover form B
#       (`at line 448`, which names no file and is unattributable by any tool).
#       The motivating citation was form B, so this check carries that case
#       alone. Scoped to `.md` citation targets: the append-at-the-end / edit-
#       in-place discipline is a DOCUMENT discipline, and applying it to source
#       would demand every code edit preserve its line count.
#
#   [2] CITED-LINE CONTENT -- for each resolvable `file:NNN` citation into a
#       changed file, is the content of line NNN the same before and after? A
#       line number that still exists is not a line number that still means what
#       it meant. Precise where it applies; blind to form B and to ambiguous
#       basenames, which is why [1] is not redundant with it.
#
# TWO KNOWN FALSE-POSITIVE DIRECTIONS, documented rather than fixed, because both
# are rare under the "delete the number, do not renumber it" discipline and both
# over-report (the lucky direction):
#
#   * check [2] scores citations found at `head` against content at `base`. A
#     citation ADDED or renumbered INSIDE the range therefore reads as rotted:
#     its line means something new because the citation is new, not because the
#     target moved. `--staged` / `--range` already gate that direction.
#   * `resolve_cited` strips a `library/` prefix. If a genuinely foreign path
#     happens to have an in-tree twin under the stripped name, it resolves to the
#     twin and check [2] compares against a file the citation never meant.
#
# WHAT A CLEAN RUN DOES NOT MEAN. Every report prints its own denominator, so a
# zero here is readable as "no rot among the citations this mode could RESOLVE"
# rather than "no rot". Ambiguous-basename citations are listed and deliberately
# do NOT affect the exit code -- over-reporting them would train the reader to
# ignore the section, and they cannot be adjudicated mechanically.


def range_endpoints(root, spec):
    """(base_sha, head_sha) for `A..B` / `A...B` / `A..` / `..B`."""
    if "..." in spec:
        a, b = spec.split("...", 1)
        a, b = a or "HEAD", b or "HEAD"
        a = git(["merge-base", a, b], root, f"merge-base {a} {b}").strip()
    elif ".." in spec:
        a, b = spec.split("..", 1)
        a, b = a or "HEAD", b or "HEAD"
    else:
        raise SystemExit(
            f"check-line-citations: --shift-audit wants a RANGE (A..B), got {spec!r}. "
            "A single rev is ambiguous about which side is the baseline; refusing "
            "to guess and report a clean tree from the wrong comparison.")
    return (git(["rev-parse", a], root, f"rev-parse {a}").strip(),
            git(["rev-parse", b], root, f"rev-parse {b}").strip())


def changed_in_range(root, base, head):
    """[(status, old_path, new_path)] -- M/A/D/R/C across the range."""
    # --no-renames on purpose (and explicitly, since diff.renames defaults ON).
    # A rename handled as one row would need check [1] to diff a path that does
    # not exist at `base`, yielding a whole-file "INSERTION after line 0" with an
    # impossible line number. As D+A it is already correct: citations into the old
    # name no longer resolve, which is exactly what rot means here.
    out = git(["diff", "--name-status", "--no-renames", base, head],
              root, "diff --name-status")
    rows = []
    for ln in out.splitlines():
        parts = ln.split("\t")
        if len(parts) < 2:
            continue
        st = parts[0]
        if st[0] in ("R", "C") and len(parts) >= 3:
            rows.append((st, parts[1], parts[2]))
        else:
            rows.append((st, parts[1], parts[1]))
    return rows


def blob_lines(root, rev, path, cache):
    """Lines of `rev:path`, or None if the blob does not exist at that rev.

    Absence is a legitimate answer here (an ADDED file has no base blob), so this
    cannot route through git() -- but the CALLER must know which absences it
    expected, or a broken invocation reads as `no citations to check`.
    """
    key = (rev, path)
    if key not in cache:
        r = subprocess.run(["git", "show", f"{rev}:{path}"],
                           cwd=root, capture_output=True)
        cache[key] = (None if r.returncode != 0
                      else r.stdout.decode("utf-8", "replace").splitlines())
    return cache[key]


def hunks_for(root, base, head, path):
    """[(old_start, old_count, new_start, new_count)] from a -U0 diff."""
    diff = git(["diff", "-U0", base, head, "--", path], root, f"diff -U0 {path}")
    out = []
    for ln in diff.splitlines():
        m = RE_HUNK.match(ln)
        if m:
            out.append((int(m.group(1)),
                        1 if m.group(2) is None else int(m.group(2)),
                        int(m.group(3)),
                        1 if m.group(4) is None else int(m.group(4))))
    return out


def hunk_shifts(old_start, old_count, new_count, old_total):
    """Does this hunk move the line numbers below it?

    Shift-free means the hunk is either an in-place same-line-count replacement
    (`NcN`) or an append at the ORIGINAL last line. `old_count == 0` is an
    INSERTION after `old_start`; that only fails to shift anything when there is
    nothing below it, i.e. `old_start == old_total`.
    """
    if old_count == 0:
        return old_start != old_total
    return old_count != new_count


def mapped_base_line(hunks, n):
    """Which BASE line does head line `n` come from? None if `n` is inside a hunk.

    Content equality is not enough to say a citation still means what it meant.
    Insert a line above two byte-identical `}` lines and head line 10 still reads
    `}` -- but it is the OTHER brace; the one the author cited is now line 11.
    Check [2] compares content and sees no change, and check [1] does not look at
    source files at all, so the citation repoints in silence. This is the only
    thing that catches it.
    """
    delta = 0
    for a, b, c, d in hunks:
        if d > 0 and c <= n <= c + d - 1:
            return None           # inside changed content; the content check owns it
        new_end = c + d - 1 if d > 0 else c
        if new_end < n:
            delta += d - b
    return n - delta


def resolve_cited(target, universe, by_base):
    """resolve(), plus the PARENT-REPO spelling of a path inside this submodule.

    The submodule is mounted at `.../library/` in the parent, and citations
    written from up there spell the same file `library/.specify/2j-...md`. Run
    from inside the submodule that path resolves to nothing, so the citation is
    bucketed 'foreign / out of scope' -- a false clean on an in-tree file.
    """
    hits = resolve(target, universe, by_base)
    if not hits and target.startswith("library/"):
        return resolve(target[len("library/"):], universe, by_base)
    return hits


def files_at(root, rev):
    """Every tracked path at `rev`. -z, because paths may contain spaces."""
    out = git(["ls-tree", "-r", "-z", "--name-only", rev], root, f"ls-tree {rev}")
    files = [p for p in out.split("\0") if p]
    if not files:
        raise SystemExit(
            f"check-line-citations: `git ls-tree {rev}` listed ZERO files. "
            "Refusing to audit against an empty universe.")
    return files


def build_citation_index(root, wanted, rev):
    """target path -> [citation records], for targets in `wanted` only.

    Reads the citing files AT `rev`, not from the working tree: a range that does
    not end at HEAD would otherwise be scored with today's citing files against a
    historical target, mixing eras and manufacturing findings.

    The population is EVERY tracked file, not SCAN_DIRS -- the motivating rot was
    `.specify/` citing `.specify/`, and neither end is inside the population the
    addition gate scans.

    `wanted` is folded into the RESOLUTION universe, not merely used to filter it.
    A file DELETED or RENAMED in the range is gone from the tree at `rev`, so
    every citation into it would resolve to nothing and the audit would report
    clean on the one case where every citation is certainly rotted.
    """
    universe = sorted(set(files_at(root, rev)) | set(wanted))
    by_base = basename_map(universe)
    at_rev = set(universe)
    memo = {}

    def rz(target):
        if target not in memo:
            memo[target] = resolve_cited(target, universe, by_base)
        return memo[target]

    # One rev-scoped grep instead of 6k blob reads. The patterns are a PREFILTER
    # only -- deliberately looser than RE_TARGET/RE_B/RE_C, which then decide.
    # `-i` widens it further; that costs nothing, since Python re-decides.
    grep_cmd = ["git", "grep", "-nI", "--no-color", "-i", "-E"]
    for pat in CITE_PREFILTER:
        grep_cmd += ["-e", pat]
    grep_cmd += [rev, "--", "."] + [f":(exclude){x}" for x in CITE_SCAN_SKIP]
    r = subprocess.run(grep_cmd, cwd=root, capture_output=True)
    if r.returncode > 1:
        raise SystemExit(
            "check-line-citations: git grep failed "
            f"(exit {r.returncode}): {r.stderr.decode('utf-8', 'replace')[:300]}\n"
            "Refusing to report a clean audit from a failed measurement.")
    hits = r.stdout.decode("utf-8", "replace").splitlines()
    if not hits:
        raise SystemExit(
            f"check-line-citations: the citation prefilter matched ZERO lines at "
            f"{rev[:12]}. This repo holds thousands; refusing to interpret an "
            "empty measurement as a clean audit.")

    index = collections.defaultdict(list)
    ambiguous, form_bc = [], 0
    seen_files = set()
    prefix = rev + ":"
    for h in hits:
        if not h.startswith(prefix):
            raise SystemExit(
                f"check-line-citations: unparseable git grep row {h[:120]!r}. "
                "Refusing to audit from a measurement I cannot read.")
        rest = h[len(prefix):]
        try:
            cf, cl, text = rest.split(":", 2)
        except ValueError:
            continue
        if cf not in at_rev:
            raise SystemExit(
                f"check-line-citations: git grep named {cf!r}, absent from "
                f"`git ls-tree {rev[:12]}` -- a path containing a colon would do "
                "this. Refusing to audit from a misparsed measurement.")
        seen_files.add(cf)
        if RE_PRAGMA.search(text) or RE_LINE_DIRECTIVE.search(text):
            continue
        if RE_B.search(text) or RE_C.search(text):
            form_bc += 1
        for m in RE_TARGET.finditer(text):
            target, num = m.group(1), int(m.group(2))
            cands = rz(target)
            rec = {"cf": cf, "cl": int(cl), "target": target, "n": num,
                   "text": text.strip()}
            if len(cands) == 1:
                if cands[0] in wanted:
                    index[cands[0]].append(rec)
            elif len(cands) > 1 and any(c in wanted for c in cands):
                rec["cands"] = cands
                ambiguous.append(rec)
    return index, ambiguous, len(seen_files), form_bc


def shift_audit(root, spec, json_out=None):
    base, head = range_endpoints(root, spec)
    rows = changed_in_range(root, base, head)
    if not rows:
        raise SystemExit(
            f"check-line-citations: --shift-audit {spec} names a range with ZERO "
            "changed files. Refusing to interpret an empty measurement as a "
            "clean audit -- check the range.")

    # BOTH sides of every row: a citation into a path that a rename or a delete
    # removed is exactly the citation most certainly rotted.
    wanted = {p for _st, old, new in rows for p in (old, new)}
    index, ambiguous, scanned, form_bc = build_citation_index(root, wanted, head)

    blobs = {}
    shift_findings, content_findings = [], []
    md_targets_checked, skipped_non_target = [], []

    # For check [1]'s predicate ONLY, an AMBIGUOUS citation still counts as
    # evidence that a file is cited by line number: `spec.md:88` names some
    # spec.md, and if this one is a candidate it may well be the one. Over-
    # reporting is the lucky direction for "should this document be edited
    # shift-free?". Check [2] must NOT do this -- comparing a line against a file
    # the citation may not mean would manufacture rot.
    amb_targets = collections.Counter()
    for r in ambiguous:
        for c in r["cands"]:
            if c in wanted:
                amb_targets[c] += 1

    for st, old_path, new_path in rows:
        cites = list(index.get(new_path, []))
        if old_path != new_path:
            cites += index.get(old_path, [])
        n_amb = amb_targets.get(new_path, 0)

        # ── [1] line-shift, for EVERY changed `.md` ──────────────────────────
        # Not "every .md that a form-A citation resolves to". Gating on that made
        # the check unable to cover the very case it is here for: a document cited
        # only as `see the rule at line 18` names no file, so it resolves to
        # nothing, gets bucketed "cited by NOTHING", and its shift goes unreported
        # -- while the header claims form B is exactly what check [1] covers. The
        # append-at-the-end discipline is document-wide, so the check is too.
        if new_path.endswith(".md") and st[0] not in ("A", "D"):
            old = blob_lines(root, base, old_path, blobs)
            if old is None:
                raise SystemExit(
                    f"check-line-citations: {old_path} is status {st} in {spec} but "
                    f"has no blob at {base[:8]}. Refusing to audit from a "
                    "measurement that failed.")
            md_targets_checked.append(new_path)
            amb_lines = [r["n"] for r in ambiguous if new_path in r["cands"]]
            md_hunks = hunks_for(root, base, head, new_path)
            # C. A modified file that yields NO parsed hunk is a failed
            # measurement, not a shift-free edit: `git diff -U0` prints "Binary
            # files ... differ" with no `@@` line for anything git calls binary
            # (a `binary` attribute, or a NUL byte), and this loop would then
            # declare it clean having compared nothing.
            if not md_hunks:
                raise SystemExit(
                    f"check-line-citations: {new_path} is status {st} in {spec} but "
                    "`git diff -U0` yielded NO hunk header -- git is treating it as "
                    "binary. Refusing to call it shift-free from a measurement that "
                    "compared nothing.")
            for a, b, _c, d in md_hunks:
                if hunk_shifts(a, b, d, len(old)):
                    # Only citations AT OR BELOW the hunk actually move. This is
                    # a triage aid, NOT a filter: it counts the citations that
                    # can be POSITIONED, and form B/C name no file, so they
                    # cannot be. A hunk with `below == 0` is therefore "no known
                    # citation moved", never "nothing moved" -- which is why it
                    # is still reported and still fails.
                    below = sum(1 for r in cites if r["n"] >= a)
                    shift_findings.append({
                        "file": new_path, "old_start": a, "old_count": b,
                        "new_count": d, "old_total": len(old),
                        "cited_by": len(cites), "cited_ambiguously_by": n_amb,
                        "resolved_citations_below": below,
                        "ambiguous_citations_below": sum(1 for n in amb_lines if n >= a)})
            if not cites and not n_amb:
                skipped_non_target.append(new_path)

        if not cites:
            continue

        # ── [2] cited-line content ───────────────────────────────────────────
        if st[0] == "D":
            for r in cites:
                content_findings.append(dict(r, why="target DELETED in range",
                                             before=None, after=None))
            continue
        old = blob_lines(root, base, old_path, blobs)
        new = blob_lines(root, head, new_path, blobs)
        if new is None:
            raise SystemExit(
                f"check-line-citations: {new_path} is status {st} in {spec} but has "
                f"no blob at {head[:8]}. Refusing to audit from a failed measurement.")
        if old is None:
            # The target did not exist at `base`, so "did the cited line change"
            # has no before-value and cannot be violated -- the citation was
            # dangling before and resolves now, which is an improvement, not rot.
            # Only OUT OF RANGE stays a finding here: like census's out-of-range
            # arm, it cannot be anything but a defect.
            for r in cites:
                if not 1 <= r["n"] <= len(new):
                    content_findings.append(dict(
                        r, why=f"target ADDED in range and line {r['n']} is OUT "
                               f"OF RANGE ({len(new)} lines)",
                        before=None, after=None))
            continue
        cite_hunks = hunks_for(root, base, head, new_path)
        for r in cites:
            n = r["n"]
            b_ok, a_ok = 1 <= n <= len(old), 1 <= n <= len(new)
            before = old[n - 1] if b_ok else None
            after = new[n - 1] if a_ok else None
            if not a_ok:
                content_findings.append(dict(r, why=f"line {n} is now OUT OF RANGE "
                                             f"({len(new)} lines)",
                                             before=before, after=None))
            elif not b_ok:
                content_findings.append(dict(r, why="line was out of range BEFORE "
                                             "(already rotted)",
                                             before=None, after=after))
            elif before != after:
                content_findings.append(dict(r, why="cited line CONTENT CHANGED",
                                             before=before, after=after))
            else:
                # Byte-identical, which is NOT the same as unmoved. If head line n
                # came from a different base line, the citation now lands on a
                # coincidental twin -- duplicate `}`, `#endif`, or a blank line --
                # and the content check cannot see it. Applies to SOURCE files
                # too, which check [1] deliberately does not cover.
                m = mapped_base_line(cite_hunks, n)
                if m is not None and m != n:
                    content_findings.append(dict(
                        r, why=f"SILENTLY REPOINTED: head line {n} was line {m} "
                               "before; the content matches only by coincidence",
                        before=old[m - 1] if 1 <= m <= len(old) else None,
                        after=after))

    # ── report ───────────────────────────────────────────────────────────────
    print("── #336 shift audit " + "─" * 54)
    print(f"range                    : {base[:12]}..{head[:12]}  ({spec})")
    print(f"files changed in range   : {len(rows)}")
    print(f"citation index           : {scanned} file(s) hold a citation-shaped "
          f"line at {head[:8]}")
    print("                           (population: EVERY tracked file, not SCAN_DIRS)")
    print(f"  citations INTO changed files, resolved : "
          f"{sum(len(v) for v in index.values())}")
    print(f"  ambiguous basename, NOT adjudicated    : {len(ambiguous)}")
    print(f"  form B/C lines tree-wide, INVISIBLE    : {form_bc}   "
          "(name no file; check [1] is their only cover)")
    n_non_md = sum(1 for _st, _o, n in rows if not n.endswith(".md"))
    print(f"[1] .md citation targets shift-checked   : {len(md_targets_checked)}")
    print(f"    .md changed but cited by NOTHING     : {len(skipped_non_target)}"
          "   [not checked]")
    print(f"    non-.md changed files                : {n_non_md}   [check [1] "
          "does not apply: the append-at-the-end")
    print("                                             discipline is a DOCUMENT "
          "discipline, not a source one]")
    print()

    print(f"[1] LINE-SHIFT AUDIT -- {len(shift_findings)} shifting hunk(s)")
    for f in shift_findings:
        kind = ("INSERTION of %d line(s) after line %d" % (f["new_count"], f["old_start"])
                if f["old_count"] == 0
                else "REPLACEMENT of %d line(s) by %d at line %d"
                     % (f["old_count"], f["new_count"], f["old_start"]))
        amb = (f", + {f['cited_ambiguously_by']} ambiguous"
               if f.get("cited_ambiguously_by") else "")
        print(f"  {f['file']}  ({f['cited_by']} resolved citation(s){amb} into it, "
              f"{f['old_total']} lines before)")
        below = f["resolved_citations_below"] + f["ambiguous_citations_below"]
        print(f"      {kind} -- every line below it moved")
        print(f"      {below} citation(s) sit at or below it and MOVED"
              + ("   [none that can be POSITIONED -- form B/C cannot be]"
                 if below == 0 else ""))
    if not shift_findings:
        print("  none -- every hunk is an in-place same-line-count replacement or an")
        print("  append at the original last line.")
    print()

    print(f"[2] CITED-LINE CONTENT CHECK -- {len(content_findings)} rotted citation(s)")
    for f in content_findings:
        print(f"  {f['cf']}:{f['cl']}  ->  {f['target']}:{f['n']}   {f['why']}")
        if f.get("before") is not None:
            print(f"      before: {f['before'].strip()[:110]}")
        if f.get("after") is not None:
            print(f"      after : {f['after'].strip()[:110]}")
    if not content_findings:
        print("  none -- every resolvable cited line holds byte-identical content.")
    print()

    if ambiguous:
        print(f"AMBIGUOUS ({len(ambiguous)}) -- basename resolves to several tracked "
              "files, so the")
        print("target is undecidable. Listed, NOT counted against the exit code.")
        for r in ambiguous[:40]:
            print(f"  {r['cf']}:{r['cl']}  ->  {r['target']}:{r['n']}  "
                  f"({len(r['cands'])} candidates)")
        if len(ambiguous) > 40:
            print(f"  ... and {len(ambiguous) - 40} more (--json for all)")
        print()

    if json_out:
        with open(json_out, "w") as f:
            json.dump({"base": base, "head": head, "shift": shift_findings,
                       "content": content_findings, "ambiguous": ambiguous,
                       "md_checked": md_targets_checked,
                       "md_skipped_non_target": skipped_non_target,
                       "form_bc_tree_wide": form_bc}, f, indent=1)
        print(f"audit table -> {json_out}")

    if shift_findings or content_findings:
        print("A line number is a claim about a file that keeps moving. Do NOT "
              "renumber the", file=sys.stderr)
        print("citations -- that produces N fresh claims that rot on the next edit. "
              "Reshape", file=sys.stderr)
        print("the EDIT instead: append narrative at the END of the file, make every "
              "in-body", file=sys.stderr)
        print("edit an in-place same-line-count replacement, fold or pad a comment "
              "block back", file=sys.stderr)
        print("to its original count. (issue #336; brain/index.md, 'Amending a "
              "document that", file=sys.stderr)
        print("is cited BY LINE NUMBER')", file=sys.stderr)
        return 1
    print("check-line-citations: shift audit clean for the citations it could "
          "RESOLVE. See")
    print("the denominators above -- form B/C citations name no file and cannot be "
          "checked.")
    return 0


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
    # Forms B/C name no target, so a foreign token elsewhere on the line must
    # NOT exempt them -- a line-scoped exemption here previously swallowed the
    # `at line 99` citation because the unrelated `boost/asio/` token shared it.
    ("// our workaround at line 99 handles boost/asio/",             ["B"]),
    # A `#line` directive is not a citation -- form B's {2,}-digit rule would
    # otherwise match its line number.
    ('#line 86 "generated.cpp"',                                     []),
    # Exemptions and near-misses that must NOT fire.
    ("// session.cpp:1258 citation-ok reviewed 2026-08-28",          []),
    ("// the ratio is 3:2 across the board",                         []),
    ("// see line 9 of the fixture",                                 []),
    ("// std::array<std::uint16_t, 16> group_view",                  []),
    ("// timeout is 120s and the port is 8080",                      []),
]


# `hunk_shifts` cases, derived from the RULE stated in #336 -- "each diff hunk is
# NcN (same line count in and out) or an append at the original last line" -- and
# NOT from what the parser happens to do. The two entries with `old_count == 0`
# are the whole point: an insertion shifts everything below it, and an append at
# EOF has nothing below it.
HUNK_CASES = [
    # (old_start, old_count, new_count, old_total) -> shifts?
    ((1515, 0, 41, 1515), False, "append at the original last line"),
    ((12,   0, 47, 1620), True,  "47-line note inserted near the top (9e0b332d)"),
    ((0,    0,  5,   20), True,  "insertion before line 1"),
    ((0,    0,  5,    0), False, "first content into an EMPTY file: nothing below"),
    ((500,  0,  3, 1515), True,  "insertion mid-document"),
    ((3,    1,  1,  100), False, "NcN, the omitted-count `@@ -3 +3 @@` form"),
    ((121,  3,  3, 1515), False, "NcN over three lines"),
    ((50,   2,  5,  100), True,  "2 lines replaced by 5"),
    ((80,   5,  2,  100), True,  "5 lines replaced by 2"),
    ((96,   5,  0,  100), True,  "trailing deletion still changes the count"),
]


# RE_TARGET / resolve_cited cases. Both entries with a leading dot and both with
# a `library/` prefix are REGRESSION pins: each was silently invisible, and each
# hid a `.specify/` design doc -- the exact surface --shift-audit protects.
TARGET_CASES = [
    ("(`.specify/2j-controlplane.md:902`)", [(".specify/2j-controlplane.md", 902)]),
    ("per `library/.specify/2j-controlplane.md:22` the lint extends",
     [("library/.specify/2j-controlplane.md", 22)]),
    ("cites 2d-threading.md:448 for the block", [("2d-threading.md", 448)]),
    ("// see session.cpp:1258 for the thunk", [("session.cpp", 1258)]),
    # The lookbehind must still stop one path matching three times.
    ("// (src/wire/offset_table.cpp:440-443)", [("src/wire/offset_table.cpp", 440)]),
    ("// the ratio is 3:2 across the board", []),
]

RESOLVE_CASES = [
    (".specify/2d.md",          [".specify/2d.md"], "leading-dot path"),
    ("library/.specify/2d.md",  [".specify/2d.md"], "parent-repo `library/` spelling"),
    ("2d.md",                   [".specify/2d.md"], "bare basename"),
    ("src/a.cpp",               ["src/a.cpp"],      "ordinary path"),
    ("library/src/a.cpp",       ["src/a.cpp"],      "`library/` prefix on a source path"),
    ("QuickFIX/DataDictionary.cpp", [],             "genuinely foreign, stays foreign"),
    ("library/QuickFIX/Foo.cpp",    [],             "`library/` rewrite must not invent a hit"),
]


def _sh_run(d, *args):
    subprocess.run(["git"] + list(args), cwd=d, capture_output=True, check=True)


def _sh_commit(d, msg):
    _sh_run(d, "add", "-A")
    _sh_run(d, "commit", "-q", "-m", msg)


def _sh_audit(d):
    """shift_audit over HEAD~1..HEAD, silenced -> (exit code, json payload).

    The audit table is written OUTSIDE the fixture repo on purpose: it embeds the
    citation text it found, so writing it inside would make the next `git add -A`
    commit a file that cites the fixture and quietly inflate every later count.
    """
    fd, out = tempfile.mkstemp(suffix=".json")
    os.close(fd)
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(buf):
        code = shift_audit(d, "HEAD~1..HEAD", out)
    with open(out) as f:
        payload = json.load(f)
    os.unlink(out)
    return code, payload


# Citation spellings that must survive the PREFILTER and reach the deciders. Each
# is an edge of a DECIDER, so if the prefilter is ever narrowed the pin fails
# rather than the audit quietly going blind. Symlinks are excluded on both sides:
# `git grep` does not follow them, and following one would double-count every
# citation in the file it points at (found while validating -- the comparison
# harness followed `.specify/memory/constitution.md` and reported 19 phantom
# misses against a tool that was correct).
PREFILTER_LINES = [
    "// see session.cpp:1258 for the thunk",
    "// (`.specify/2j-controlplane.md:902`) per goal 6",
    "// per `library/.specify/2d-threading.md:22` the lint extends",
    "// tab before the number: at\tline 448 exactly",
    "// tilde form (~line 3232) honoured",
    "// bare self-citation (:532-534) returns status",
    "// two digits is the floor: line 99 counts",
    "// doc.md:10 and target.hpp:2 on one line",
]


def prefilter_recall_check(d):
    """The prefilter must pass through EVERY line the deciders would match.

    It decides what reaches RE_TARGET/RE_B/RE_C, so a prefilter narrower than any
    of them drops citations before anything can judge them -- and the audit then
    reports clean because it could not report otherwise.
    """
    os.makedirs(os.path.join(d, "pf"), exist_ok=True)
    open(os.path.join(d, "pf", "spellings.cpp"), "w").write(
        "\n".join(PREFILTER_LINES) + "\n")
    _sh_commit(d, "prefilter spellings")
    rev = git(["rev-parse", "HEAD"], d, "rev-parse").strip()

    cmd = ["git", "grep", "-nI", "--no-color", "-i", "-E"]
    for pat in CITE_PREFILTER:
        cmd += ["-e", pat]
    cmd += [rev, "--", "."]
    r = subprocess.run(cmd, cwd=d, capture_output=True)
    passed = set()
    for h in r.stdout.decode("utf-8", "replace").splitlines():
        rest = h[len(rev) + 1:]
        try:
            cf, cl, _ = rest.split(":", 2)
        except ValueError:
            continue
        passed.add((cf, int(cl)))

    # The deciders, run over every REGULAR tracked file (no symlinks).
    ls = git(["ls-tree", "-r", "-z", rev], d, "ls-tree").split("\0")
    decided = set()
    for e in ls:
        if not e:
            continue
        meta, path = e.split("\t", 1)
        if meta.split()[0] == "120000":
            continue
        try:
            with open(os.path.join(d, path), encoding="utf-8", errors="replace") as f:
                src = f.read().splitlines()
        except OSError:
            continue
        for i, ln in enumerate(src):
            if RE_TARGET.search(ln) or RE_B.search(ln) or RE_C.search(ln):
                decided.add((path, i + 1))

    missed = decided - passed
    return [
        ("prefilter is proven NON-VACUOUS (it passes the spellings through)",
         len({c for c in passed if c[0] == "pf/spellings.cpp"}) == len(PREFILTER_LINES)),
        ("every decider-matching line survives the prefilter", not missed),
    ]


def shift_self_test():
    """Both halves of #336's requirement: the audit must fire on a real shift AND
    on a same-line-count mutation of a cited line, and must stay silent on the
    two edit shapes the discipline permits."""
    bad = 0
    print("RE_TARGET -- which `file:NNN` spellings are SEEN at all:")
    for text, want in TARGET_CASES:
        got = [(m.group(1), int(m.group(2))) for m in RE_TARGET.finditer(text)]
        ok = got == want
        bad += not ok
        print(f"  {'ok  ' if ok else 'FAIL'} {len(got)} match(es)  {text[:58]}")

    print("\nresolve_cited() -- which spellings reach the file they name:")
    _u = [".specify/2d.md", "src/a.cpp"]
    _b = basename_map(_u)
    for target, want, label in RESOLVE_CASES:
        got = resolve_cited(target, _u, _b)
        ok = got == want
        bad += not ok
        print(f"  {'ok  ' if ok else 'FAIL'} {label}")

    print("\nhunk_shifts() -- the shift predicate, per #336's stated rule:")
    for (a, b, d_, total), want, label in HUNK_CASES:
        got = hunk_shifts(a, b, d_, total)
        ok = got == want
        bad += not ok
        print(f"  {'ok  ' if ok else 'FAIL'} shifts={got!s:5} want={want!s:5}  {label}")

    print("\nshift_audit() end-to-end on a throwaway repo:")
    checks = []
    with tempfile.TemporaryDirectory() as d:
        _sh_run(d, "init", "-q")
        _sh_run(d, "config", "user.email", "t@t")
        _sh_run(d, "config", "user.name", "t")
        os.makedirs(os.path.join(d, "src"))
        doc = os.path.join(d, "doc.md")
        lines = [f"L{i:02d}" for i in range(1, 21)]

        def write_doc():
            open(doc, "w").write("\n".join(lines) + "\n")

        write_doc()
        # Two citations INTO doc.md, at lines 10 and 18.
        open(os.path.join(d, "src", "citer.cpp"), "w").write(
            "// the widget is at doc.md:10\n"
            "// the gadget is at doc.md:18\n")
        # A .md nothing cites -- must be reported as skipped, never as clean-checked.
        open(os.path.join(d, "other.md"), "w").write("uncited\n")
        _sh_commit(d, "base")

        # 1. Insertion at the top: the 9e0b332d shape. Both checks must fire.
        lines = ["NEW"] * 5 + lines
        write_doc(); _sh_commit(d, "insert at top")
        code, j = _sh_audit(d)
        checks.append(("top insertion: exit non-zero", code == 1))
        checks.append(("top insertion: [1] reports a shifting hunk", len(j["shift"]) >= 1))
        checks.append(("top insertion: [2] reports BOTH citations rotted",
                       len(j["content"]) == 2))
        checks.append(("top insertion: rot is a CONTENT change, not out-of-range",
                       all("CONTENT CHANGED" in c["why"] for c in j["content"])))

        # 2. Append at EOF: the permitted shape. Both checks must stay silent.
        lines = lines + ["APPENDIX", "Z", "text"]
        write_doc(); _sh_commit(d, "append at EOF")
        code, j = _sh_audit(d)
        checks.append(("EOF append: exit zero", code == 0))
        checks.append(("EOF append: no shift, no rot",
                       not j["shift"] and not j["content"]))
        checks.append(("EOF append: doc.md WAS shift-checked, not skipped",
                       j["md_checked"] == ["doc.md"]))

        # 3. In-place replacement of an UNCITED line -- exercises the hunk form
        #    where git omits both counts (`@@ -3 +3 @@`).
        lines[2] = "L03-edited"
        write_doc(); _sh_commit(d, "in-place edit, uncited line")
        code, j = _sh_audit(d)
        checks.append(("in-place edit of an uncited line: clean",
                       code == 0 and not j["shift"] and not j["content"]))

        # 4. #336's mandated control: mutate ONE cited line, same line count. The
        #    shift check CANNOT see this -- only the content check can.
        lines[9] = "L10-mutated"
        write_doc(); _sh_commit(d, "mutate the cited line")
        code, j = _sh_audit(d)
        checks.append(("mutated cited line: exit non-zero", code == 1))
        checks.append(("mutated cited line: [1] sees NOTHING (line count kept)",
                       not j["shift"]))
        checks.append(("mutated cited line: [2] reports exactly the :10 citation",
                       len(j["content"]) == 1 and j["content"][0]["n"] == 10))

        # 5. Mid-document insertion -- must NOT be confused with an EOF append.
        lines = lines[:5] + ["MID"] * 4 + lines[5:]
        write_doc(); _sh_commit(d, "insert mid-document")
        code, j = _sh_audit(d)
        checks.append(("mid-document insertion: [1] fires", code == 1 and j["shift"]))
        checks.append(("mid-document insertion: reported as an INSERTION",
                       any(f["old_count"] == 0 for f in j["shift"])))

        # 5b. A shift BELOW every citation still fails -- but must say that zero
        #     POSITIONABLE citations moved, so a reviewer can triage it. Form B/C
        #     citations cannot be positioned, which is why it is not filtered out.
        lines = lines[:25] + ["LATE"] * 2 + lines[25:]
        write_doc(); _sh_commit(d, "insert below every citation")
        code, j = _sh_audit(d)
        checks.append(("shift below every citation: still fails",
                       code == 1 and len(j["shift"]) == 1))
        checks.append(("...annotated as 0 positionable citations moved",
                       j["shift"][0]["resolved_citations_below"] == 0
                       and not j["content"]))

        # 5c. ...and the same shift ABOVE the citations reports them as moved.
        lines = ["EARLY"] * 2 + lines
        write_doc(); _sh_commit(d, "insert above every citation")
        code, j = _sh_audit(d)
        checks.append(("shift above every citation: both reported as moved",
                       code == 1 and j["shift"][0]["resolved_citations_below"] == 2))

        # 6. Truncation below a cited line: the citation survives as a number and
        #    stops existing as a location.
        lines = lines[:8]
        write_doc(); _sh_commit(d, "truncate")
        code, j = _sh_audit(d)
        checks.append(("truncation: out-of-range is reported as such",
                       code == 1 and any("OUT OF RANGE" in c["why"]
                                         for c in j["content"])))

        # 7. Deleting the target rots every citation into it.
        os.remove(doc); _sh_commit(d, "delete the target")
        code, j = _sh_audit(d)
        checks.append(("deleted target: every citation into it is a finding",
                       code == 1 and len(j["content"]) == 2
                       and all("DELETED" in c["why"] for c in j["content"])))

        # 8b. A doc cited only by an AMBIGUOUS basename must still be shift-
        #     checked: `sub/doc.md:10` where two doc.md exist names one of them,
        #     and skipping both is a false clean on whichever it meant.
        os.makedirs(os.path.join(d, "sub"))
        twin = os.path.join(d, "sub", "twin.md")
        open(twin, "w").write("\n".join(f"T{i:02d}" for i in range(1, 16)) + "\n")
        os.makedirs(os.path.join(d, "other"))
        open(os.path.join(d, "other", "twin.md"), "w").write("decoy\n")
        open(os.path.join(d, "src", "amb.cpp"), "w").write("// cited: twin.md:9\n")
        _sh_commit(d, "add an ambiguously-cited doc")
        _prev = open(twin).read()
        open(twin, "w").write("PREPENDED\n" + _prev)
        _sh_commit(d, "prepend to the ambiguously-cited doc")
        code, j = _sh_audit(d)
        checks.append(("ambiguously-cited .md is still shift-checked",
                       code == 1 and any(f["file"] == "sub/twin.md"
                                         for f in j["shift"])))
        checks.append(("...and the ambiguity is disclosed, not silently resolved",
                       any(f.get("cited_ambiguously_by", 0) > 0 for f in j["shift"])
                       and not j["content"]))

        # 8c. A citation into a file ADDED in the range has no before-value, so it
        #     is not rot -- but an out-of-range one into it still is.
        os.makedirs(os.path.join(d, "fresh"), exist_ok=True)
        open(os.path.join(d, "src", "newcite.cpp"), "w").write(
            "// in range:  fresh/new.md:2\n"
            "// past EOF:  fresh/new.md:99\n")
        _sh_commit(d, "cite a file that does not exist yet")
        open(os.path.join(d, "fresh", "new.md"), "w").write("A\nB\nC\n")
        _sh_commit(d, "add the cited file")
        code, j = _sh_audit(d)
        checks.append(("added target: an IN-RANGE citation into it is not rot",
                       not any(c["n"] == 2 for c in j["content"])))
        checks.append(("added target: an OUT-OF-RANGE citation into it still is",
                       code == 1 and any(c["n"] == 99 and "OUT OF RANGE" in c["why"]
                                         for c in j["content"])))

        # 8d. A RENAMED cited target must not produce a whole-file "INSERTION
        #     after line 0". Citations into the old name stop resolving, which is
        #     rot; the new name is an add, which is not.
        os.makedirs(os.path.join(d, "mv"), exist_ok=True)
        open(os.path.join(d, "mv", "before.md"), "w").write(
            "\n".join(f"R{i:02d}" for i in range(1, 13)) + "\n")
        open(os.path.join(d, "src", "mvcite.cpp"), "w").write("// see mv/before.md:5\n")
        _sh_commit(d, "add a renamable cited doc")
        _sh_run(d, "mv", os.path.join(d, "mv", "before.md"),
                os.path.join(d, "mv", "after.md"))
        _sh_commit(d, "rename the cited doc")
        code, j = _sh_audit(d)
        checks.append(("rename: no impossible 'after line 0' shift finding",
                       not any(f["old_start"] == 0 for f in j["shift"])))
        checks.append(("rename: the citation into the OLD name is reported rotted",
                       code == 1 and any("DELETED" in c["why"] for c in j["content"])))

        # 8e. The dispatch in main() is its own failure surface: an empty range is
        #     FALSY, so `if args.shift_audit:` fell through to gate(), which with
        #     neither --staged nor --range diffs the working tree against the
        #     index and prints "no new line-number citations ... OK". A clean
        #     verdict FROM ANOTHER MODE. Pinned through the CLI, because calling
        #     shift_audit() directly cannot see a dispatch bug.
        for flag in ("--shift-audit", "--range"):
            r = subprocess.run([sys.executable, os.path.abspath(__file__),
                                flag, "", "--root", d],
                               capture_output=True, text=True)
            blob = r.stdout + r.stderr
            checks.append((f"CLI: an empty {flag} value never reports clean",
                           r.returncode != 0
                           and "no new line-number citations" not in blob))

        # 8f. Codex P1-1: a document cited ONLY by form B. `see the rule at line
        #     18` names no file, so nothing resolves to doc2.md -- yet form B is
        #     the reason check [1] exists, and the motivating incident was written
        #     in it. Gating check [1] on a resolvable form-A citation made the
        #     check blind to its own justification.
        d2 = os.path.join(d, "doc2.md")
        open(d2, "w").write("\n".join(f"D{i:02d}" for i in range(1, 21)) + "\n")
        open(os.path.join(d, "src", "formb.cpp"), "w").write(
            "// the rule is described at line 18 of the design doc\n")
        _sh_commit(d, "add a form-B-only cited doc")
        # Read BEFORE opening for write: `open(x,"w").write(... + open(x).read())`
        # truncates x first, so the read returns "" and the fixture silently tests
        # a whole-file replacement instead of the insertion it claims to test.
        _prev = open(d2).read()
        open(d2, "w").write("PRE\n" * 5 + _prev)
        _sh_commit(d, "insert at the top of the form-B-only doc")
        code, j = _sh_audit(d)
        checks.append(("form-B-only doc IS shift-checked (not 'cited by NOTHING')",
                       "doc2.md" in j["md_checked"]))
        checks.append(("form-B-only doc's shift is REPORTED",
                       code == 1 and any(f["file"] == "doc2.md" for f in j["shift"])))

        # 8g. Codex P1-2: a SOURCE citation that repoints while the content stays
        #     byte-identical. Two identical `}` lines; inserting one line above
        #     makes head line 10 read `}` exactly as base line 10 did -- but it is
        #     base line 9's brace. Check [1] does not cover .cpp and check [2]'s
        #     content comparison sees nothing, so only the line map catches it.
        tc = os.path.join(d, "src", "target.cpp")
        open(tc, "w").write("\n".join(["A"] * 8 + ["}", "}"]) + "\n")
        open(os.path.join(d, "src", "twincite.cpp"), "w").write(
            "// the closing brace is at target.cpp:10\n")
        _sh_commit(d, "add a target with two identical braces")
        _prev = open(tc).read()
        open(tc, "w").write("int f() {\n" + _prev)
        _sh_commit(d, "insert a line above the identical braces")
        code, j = _sh_audit(d)
        rp = [c for c in j["content"] if "SILENTLY REPOINTED" in c["why"]]
        checks.append(("byte-identical repoint in a SOURCE file is caught",
                       code == 1 and len(rp) == 1 and rp[0]["n"] == 10))
        checks.append(("...and it names the base line it actually came from",
                       bool(rp) and "was line 9" in rp[0]["why"]))

        checks += prefilter_recall_check(d)

        # 8. A .md that nothing cites must be SKIPPED and said to be skipped --
        #    not silently folded into the clean verdict.
        open(os.path.join(d, "other.md"), "a").write("more\n")
        _sh_commit(d, "edit an uncited doc")
        code, j = _sh_audit(d)
        checks.append(("uncited .md: clean AND declared skipped",
                       code == 0 and j["md_skipped_non_target"] == ["other.md"]))

        # 9/10. A failed or empty measurement must RAISE, never report clean.
        # Each case asserts WHICH diagnostic fired, not merely that something did.
        # "any SystemExit" would be satisfied by the wrong one -- swallow a bad
        # revision into an empty range and the later "ZERO changed files" guard
        # still raises, so the revision bug reads as correctly handled.
        for label, spec, want in (
                ("an invalid range raises, naming the failed git call",
                 "not-a-ref..HEAD", "rev-parse"),
                ("an EMPTY --shift-audit value raises rather than falling "
                 "through to the ADDITION gate", "", "wants a RANGE"),
                ("an EMPTY range raises rather than reporting clean",
                 "HEAD..HEAD", "ZERO changed files"),
                ("a single rev (no `..`) is refused, not guessed",
                 "HEAD", "wants a RANGE")):
            try:
                buf = io.StringIO()
                with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(buf):
                    shift_audit(d, spec, None)
                ok = False
            except SystemExit as e:
                ok = want in str(e)
            checks.append((label, ok))

    for label, ok in checks:
        bad += not ok
        print(f"  {'ok  ' if ok else 'FAIL'} {label}")
    return bad, len(TARGET_CASES) + len(RESOLVE_CASES) + len(HUNK_CASES) + len(checks)


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
        # 0 lines long: any citation into it is out of range, including :0.
        open(os.path.join(d, "include", "empty.hpp"), "w").write("")
        open(os.path.join(d, "src", "citer.cpp"), "w").write(
            "// rotted past EOF: target.hpp:99\n"
            "// resolves fine: target.hpp:2\n"
            "// foreign: DataDictionary.cpp:271 via QuickFIX\n"
            "// exempted: target.hpp:99 citation-ok\n"
            "// line zero is not a line: target.hpp:0\n"
            "// empty target file: empty.hpp:1\n")
        for a in (["init", "-q"], ["add", "-A"]):
            subprocess.run(["git"] + a, cwd=d, capture_output=True, check=True)
        r = census(d, None, quiet=True)
        checks = [
            ("out-of-range found",     len(r["oor"]) == 3),
            ("in-range not flagged",   len(r["resolved"]) == 4),
            ("foreign excluded",       len(r["foreign"]) == 1),
            ("citation-ok bucketed",   len(r["exempt"]) == 1),
            (":0 is out of range",     any(x["target"] == "target.hpp" and x["n"] == 0
                                            for x in r["oor"])),
            ("empty target is out of range",
             any(x["target"] == "empty.hpp" and x["n"] == 1 for x in r["oor"])),
        ]
        for label, ok in checks:
            bad += not ok
            print(f"  {'ok  ' if ok else 'FAIL'} {label}")

    print("\ngate() via --staged and --range on a throwaway repo -- the ENFORCEMENT path:")
    gate_checks = 0
    with tempfile.TemporaryDirectory() as d:
        subprocess.run(["git", "init", "-q"], cwd=d, capture_output=True, check=True)
        subprocess.run(["git", "config", "user.email", "t@t"], cwd=d, capture_output=True, check=True)
        subprocess.run(["git", "config", "user.name", "t"], cwd=d, capture_output=True, check=True)
        os.makedirs(os.path.join(d, "tests"))
        sample = os.path.join(d, "tests", "sample.cpp")
        open(sample, "w").write("// baseline, no citations\n")
        subprocess.run(["git", "add", "-A"], cwd=d, capture_output=True, check=True)
        subprocess.run(["git", "commit", "-q", "-m", "base"], cwd=d, capture_output=True, check=True)

        class Args:
            def __init__(self, staged=False, range=None):
                self.staged = staged
                self.range = range

        positives = (
            "// one per form, self-citing this file:\n"
            "// see sample.cpp:1\n"                 # A -- resolves in-tree
            "// silent-drop at line 2234\n"          # B
            "// returns the status error (:532)\n"  # C
        )

        # --staged: dirty the index, gate() must fire; clean it, gate() must not.
        open(sample, "a").write(positives)
        subprocess.run(["git", "add", "-A"], cwd=d, capture_output=True, check=True)
        gate_checks += 1
        ok = gate(d, Args(staged=True)) == 1
        bad += not ok
        print(f"  {'ok  ' if ok else 'FAIL'} --staged fires on a staged A/B/C positive")

        subprocess.run(["git", "reset", "-q", "--hard", "HEAD"], cwd=d, capture_output=True, check=True)
        gate_checks += 1
        ok = gate(d, Args(staged=True)) == 0
        bad += not ok
        print(f"  {'ok  ' if ok else 'FAIL'} --staged is clean once the positive is gone")

        # --range: same shape across a commit range instead of the index.
        open(sample, "a").write(positives)
        subprocess.run(["git", "add", "-A"], cwd=d, capture_output=True, check=True)
        subprocess.run(["git", "commit", "-q", "-m", "add citations"], cwd=d, capture_output=True, check=True)
        gate_checks += 1
        ok = gate(d, Args(range="HEAD~1..HEAD")) == 1
        bad += not ok
        print(f"  {'ok  ' if ok else 'FAIL'} --range fires on a committed A/B/C positive")

        open(sample, "w").write("// baseline, no citations\n")
        subprocess.run(["git", "add", "-A"], cwd=d, capture_output=True, check=True)
        subprocess.run(["git", "commit", "-q", "-m", "remove citations"], cwd=d, capture_output=True, check=True)
        gate_checks += 1
        ok = gate(d, Args(range="HEAD~1..HEAD")) == 0
        bad += not ok
        print(f"  {'ok  ' if ok else 'FAIL'} --range is clean once the positive is removed")

        # An invalid ref must RAISE, not report a clean tree -- a swallowed git
        # failure here would make the gate pass on a broken invocation.
        gate_checks += 1
        try:
            gate(d, Args(range="not-a-real-ref..HEAD"))
            ok = False
        except SystemExit:
            ok = True
        bad += not ok
        print(f"  {'ok  ' if ok else 'FAIL'} an invalid --range raises rather than reporting clean")

    print("\n── #336 shift audit ──────────────────────────────────────────────")
    shift_bad, shift_total = shift_self_test()
    bad += shift_bad

    total = len(FORM_CASES) + 6 + gate_checks + shift_total
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
    g.add_argument("--shift-audit", metavar="A..B",
                   help="audit: fail if the rev range INVALIDATES existing "
                        "line-number citations (#336)")
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
    # `is not None`, not truthiness: `--shift-audit ""` is falsy and would fall
    # through to gate(), which with neither --staged nor --range diffs the working
    # tree against the index and prints "no new line-number citations ... OK". A
    # mistyped invocation would report clean FROM ANOTHER MODE. `--range ""` had
    # the same shape and is fixed with it.
    if args.shift_audit is not None:
        return shift_audit(root, args.shift_audit, args.json)
    return gate(root, args)


if __name__ == "__main__":
    sys.exit(main())
