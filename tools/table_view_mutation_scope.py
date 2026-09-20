#!/usr/bin/env python3
"""Scope every mutated `table_view` declaration by BRACE DEPTH (fixpp#456).

For each `table_view X;` declaration it computes the enclosing brace scope, then
compares the line of the LAST mutator call on `X` against every non-mutator
mention of `X` inside that same scope. A scope where a read precedes the last
write INTERLEAVES and cannot be migrated by two mechanical lines.

    python3 tools/table_view_mutation_scope.py --root <library>
    python3 tools/table_view_mutation_scope.py --root <library> --emit-sites sites.tsv
    python3 tools/table_view_mutation_scope.py --root <library> --naive-decl-count

⚠️ SCOPING BY BRACE DEPTH IS THE WHOLE INSTRUMENT. The first version of this
script scanned whole FILES. It was wrong: in a file of many
`TEST(...) { table_view tv; ... }` blocks, test 1's reads of `tv` preceded test
2's writes to a DIFFERENT `tv`. Any figure produced by a file-scoped scan is a
fossil.

`--emit-sites` writes one TSV row per mutated declaration:

    <path>\\t<decl line>\\t<var>\\t<last mutator line>\\t<scope end line>\\t<interleaves>

That file — not a regex re-run across the tree — is what a migration should be
driven from. The obvious declaration regex `table_view X;` also rewrites
declarations that are never mutated, in TUs this census reports clean, which is
damage a tool CREATES rather than residue it leaves. `--naive-decl-count` prints
that regex's match count beside this scoper's; the two are printed as raw facts
and NOT compared, because the sets are not nested in either direction (see the
ATTRIBUTION GUARD below — the scoper sees `auto` spellings the naive regex
cannot, while the naive regex sees never-mutated declarations the scoper skips).

⚠️ THE ATTRIBUTION GUARD IS THE OTHER HALF OF THE INSTRUMENT, and it exists
because a DECLARATION-shaped census has a declaration-shaped blind spot. The
site table above is the sole driver of the migration, so a mutator call it does
not attribute is a call nobody rewrites. After every scan, EVERY mutator-call
line in every scanned file must fall inside the scope of a declaration this
script found; an unattributed receiver is printed and exits non-zero unless it
is named on `--known-unscoped path:var`. The known shapes are the ones where the
reference IS the mutation channel and there is no named local to rename (a
`table_view&` parameter or lambda argument) — those are migrated by hand, so
they are allow-listed explicitly rather than by widening the declaration regex.

⚠️ What "attributed" does NOT prove. Attribution is by (variable name, brace
scope), so a call attributed to a same-named declaration in a sibling scope
counts as attributed. The guard catches a receiver NO declaration explains; it
does not adjudicate which declaration explains it. The builder column shares
this same (variable name, brace scope) machinery with the declaration column
above it, not a bare file-wide name set — the two concede the identical
sibling-scope residue.

Self-check: this instrument is proven able to report non-zero (it reports the
interleaves it lists by name, and the unattributed receivers it lists by name)
AND able to report zero (most files come back 0). Both halves are printed by
every run, so neither can be assumed.
"""
import argparse
import collections
import os
import re
import subprocess
import sys

# Shared with check_co_spawn_lambda rather than re-spelled: a second copy of a
# comment/literal blanker must agree with the first forever, and a byte-identical
# copy propagates a claim that is false at the new site the moment one of them is
# fixed. check_co_spawn_lambda is side-effect-free at module scope (constants +
# `if __name__`), so importing it runs nothing. sys.path[0] is tools/ when run as
# `python3 tools/<x>.py`, but that is not guaranteed for any other entry point --
# hence the explicit insert rather than relying on it.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_co_spawn_lambda import strip_noncode  # noqa: E402

# The sixteen public mutators, alternation ordered LONGEST-FIRST so that
# add_valid/add_valid_tag and set_group_first/set_group_first_ctx cannot swallow
# each other.
MUTATORS = [
    "add_group_required_member_ctx", "add_group_required_member",
    "add_group_member_ctx", "add_group_member",
    "add_required_tag", "add_valid_tag", "add_required", "add_valid",
    "add_enum", "add_fixt_framing_tag",
    "set_group_first_ctx", "set_group_first", "set_length_pair_data_tag",
    "set_field_type", "set_multi_value", "set_type",
]
MUTRE = "(?:" + "|".join(MUTATORS) + ")"
ROOTS = ["src", "tests", "bench", "tools", "bindings", "include", "perf"]

# Two declaration spellings, not one. The second exists because a `table_view`
# returned BY VALUE from a factory is routinely bound with `auto` and mutated
# further — `auto t = make_grammar_with_framing("D"); t.add_required(...)` — and
# a regex that requires the type to be SPELLED is blind to every one of them.
# `auto` alone would match most of the tree, so it is safe here only because
# scope_file() discards any declaration with no mutator call in its scope: what
# survives is an `auto` whose variable receives a table_view mutator.
DECL = re.compile(r"^\s*(?:static\s+)?(?:fixpp::)?(?:dict::)?table_view\s+(\w+)\s*[;{=]")

# A receiver that is ALREADY a builder. After fixpp#456's seal every mutator call
# in the tree is on one of these, so without this the attribution guard below is
# SATURATED — without it, every mutator call attributes to nothing and the guard
# exits 4 on a correct, fully-migrated tree. A guard that fires on everything
# detects nothing: it could no longer single out the one receiver spelling nobody
# rewrote, which is its only job. These are counted and reported separately, never
# as leaks. Scoped the same way the declaration census above is (fix-queue item 4):
# an exemption added to stop a guard firing must be at least as precise as the
# guard, or it becomes the guard.
BUILDER_DECL = re.compile(
    r"^\s*(?:static\s+)?(?:fixpp::)?(?:dict::)?table_view_builder\s+(\w+)\s*[;{=]")
AUTO_DECL = re.compile(r"^\s*(?:static\s+)?auto\s+(\w+)\s*=")
NAIVE_DECL = re.compile(r"\btable_view\s+[A-Za-z_][A-Za-z0-9_]*\s*;")


def candidate_files(root, sub):
    r = subprocess.run(
        ["grep", "-rlE", r"\.(" + MUTRE + r")[[:space:]]*\(",
         "--include=*.cpp", "--include=*.hpp"] + ROOTS,
        cwd=root, capture_output=True, text=True)
    # 0 = matches found, 1 = no matches (its own NO CANDIDATE FILE guard below).
    # Anything else — a missing ROOTS directory, a bad pattern — is a grep
    # failure that can still emit partial stdout; treat it as fatal rather than
    # silently census-ing a partial file list.
    if r.returncode not in (0, 1):
        print(f"!! grep exited {r.returncode} scanning for candidate files:\n"
              + (r.stderr or "(no stderr)"), file=sys.stderr)
        sys.exit(r.returncode)
    files = sorted(r.stdout.split())
    return [f for f in files if sub in f] if sub else files


RECEIVER = re.compile(r"\b(\w+)\s*\.\s*" + MUTRE + r"\s*\(")
# A wrapped chain's continuation line: `    .add_valid("D", 12)` with the head
# receiver on an earlier line. It has NO receiver identifier of its own, so it
# is in neither the attributed nor the unattributed column, and this script
# prints its count rather than letting "every mutator call" stand unqualified.
CONTINUATION = re.compile(r"^\s*\.\s*" + MUTRE + r"\s*\(")


def source_lines(root, path):
    """The file with comments and literals blanked — the view both halves scan.

    ⚠️ `re.sub(r"//.*", "", l)` was NOT good enough, even where it happened to
    leave this script's numbers unmoved. Brace depth is the whole instrument, and
    a brace inside a STRING LITERAL is a brace the crude strip hands straight to
    the counter. At least one scanned file carries unbalanced braces inside
    literals, so the two strips agree only about where those braces happen to
    fall — a property of today's corpus, not of either strip. Re-derive it rather
    than trusting a number here: for each candidate file, compare the net
    `{` minus `}` of `re.sub(r"//.*", "", l)` per line against `strip_noncode`'s.

    `strip_noncode` blanks `//`, `/* */`, string and raw-string literals while
    PRESERVING length and newlines, so it is a drop-in for a line-indexed scan.
    """
    raw = open(os.path.join(root, path), errors="replace").read()
    return strip_noncode(raw).split("\n")


def mutator_receivers(lines):
    """Every (line, HEAD receiver-name) a mutator is called on — the guard's population.

    ⚠️ The population is HEAD RECEIVERS, not calls, and the difference is large.
    A chain yields only the call whose receiver is a spelled identifier:

      - same-line (`t.add_required(...).add_required(...)`) — the later calls sit
        on the returned reference, spelled `)`, which `\\w+` cannot match;
      - WRAPPED (`t.add_valid(...)` then `    .add_valid(...)` on the next line)
        — the continuation has no receiver token at all, and is matched by
        CONTINUATION instead. This is the tree's dominant spelling, so the count
        is printed rather than assumed away.

    That is correct for the migration — renaming the head migrates the whole
    chain — but "every mutator call" would be a wider claim than this measures.
    The one shape in NEITHER column is a chain whose head is an EXPRESSION
    rather than a named variable (`get_view().add_valid(...)`): there is nothing
    to rename, and the compile sweep is what finds it.
    """
    return {(j, m.group(1))
            for j, l in enumerate(lines, 1) for m in RECEIVER.finditer(l)}


def line_depths(lines):
    """Brace depth AT THE START of each line (1-indexed).

    Shared by every brace-scope-keyed pass, so the builder-declaration scope
    below agrees with the `table_view`-declaration scope byte-for-byte instead
    of being a second, driftable copy of the same arithmetic.
    """
    depth = [0] * (len(lines) + 1)
    d = 0
    for i, l in enumerate(lines, 1):
        depth[i] = d
        d += l.count("{") - l.count("}")
    return depth


def scope_end(lines, depth, i):
    """The last line still inside the brace scope opened at line `i`."""
    d0 = depth[i]
    for j in range(i + 1, len(lines) + 1):
        if depth[j] < d0:
            return j - 1
    return len(lines)


def builder_scopes(lines):
    """Yield (name, decl_line, scope_end) per `table_view_builder` declaration.

    The same (variable name, brace scope) machinery `scope_file` uses for
    `table_view` declarations (fix-queue item 4) — an exemption added to stop
    the attribution guard firing must be at least as precise as the guard, or
    it becomes the guard. A receiver is `on_builder` only when it falls AFTER
    its matching declaration's line and INSIDE that declaration's scope; a
    same-named builder anywhere else in the file no longer attributes it.
    """
    depth = line_depths(lines)
    for i, l in enumerate(lines, 1):
        m = BUILDER_DECL.match(l)
        if not m:
            continue
        yield m.group(1), i, scope_end(lines, depth, i)


def scope_file(root, path, lines=None):
    """Yield (declline, var, lastmut, scope_end, preceding_uses, mutlines) per decl."""
    lines = source_lines(root, path) if lines is None else lines
    depth = line_depths(lines)
    for i, l in enumerate(lines, 1):
        m = DECL.match(l) or AUTO_DECL.match(l)
        if not m:
            continue
        name = m.group(1)
        end = scope_end(lines, depth, i)
        mre = re.compile(r"\b" + re.escape(name) + r"\s*\.\s*" + MUTRE + r"\s*\(")
        ure = re.compile(r"\b" + re.escape(name) + r"\b")
        mut, use = [], []
        for j in range(i + 1, end + 1):
            if mre.search(lines[j - 1]):
                mut.append(j)
            elif ure.search(lines[j - 1]):
                use.append(j)
        if not mut:
            continue
        yield i, name, max(mut), end, [u for u in use if u < max(mut)], mut


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True, help="the library checkout")
    ap.add_argument("--filter", default="", help="substring match on the file path")
    ap.add_argument("--emit-sites", help="write the machine-readable site table here")
    ap.add_argument("--naive-decl-count", action="store_true",
                    help="also print the naive `table_view X;` regex match count")
    ap.add_argument("--known-unscoped", default="", metavar="path:var[,path:var]",
                    help="receivers the attribution guard may leave unattributed, each "
                         "named exactly. Nothing is allow-listed by default and nothing "
                         "is hardcoded: a shape migrated by hand is named by its caller")
    a = ap.parse_args()
    root = os.path.abspath(a.root)
    allowed = {s.strip() for s in a.known_unscoped.split(",") if s.strip()}

    files = candidate_files(root, a.filter)
    if not files:
        print("!! NO CANDIDATE FILE MATCHED — a census over nothing reports zero.",
              file=sys.stderr)
        return 2

    total = interleaved = 0
    per, perbad, rows, examples = collections.Counter(), collections.Counter(), [], []
    attributed = unattributed = continuation = 0
    unscoped = collections.Counter()
    on_builder = 0
    for f in files:
        lines = source_lines(root, f)
        seen = set()
        # Receivers that are already builders, scoped like the declarations
        # below rather than as a bare file-wide name set. Collected per file,
        # before the attribution guard runs, so a call on one is ATTRIBUTED (to
        # the migrated form) rather than counted as a leak — see BUILDER_DECL.
        builder_scope_list = list(builder_scopes(lines))
        for decl, name, lastmut, end, pre, mutlines in scope_file(root, f, lines):
            total += 1
            per[f] += 1
            seen |= {(j, name) for j in mutlines}
            bad = bool(pre)
            if bad:
                interleaved += 1
                perbad[f] += 1
                examples.append((f, decl, name, lastmut, pre[:4]))
            rows.append((f, decl, name, lastmut, end, int(bad)))
        continuation += sum(1 for l in lines if CONTINUATION.match(l))
        # THE ATTRIBUTION GUARD — the complement of the declaration census
        # against the CALL population, on a different axis from it.
        for j, recv in mutator_receivers(lines):
            if (j, recv) in seen:
                attributed += 1
            elif any(name == recv and decl_line < j <= end
                     for name, decl_line, end in builder_scope_list):
                on_builder += 1
            else:
                unattributed += 1
                unscoped[f"{f}:{recv}"] += 1

    print(f"files scanned: {len(files)}")
    print(f"scoped declarations that are mutated: {total}")
    print(f"  ... with a non-mutator use of the SAME var BEFORE the last mutator call: "
          f"{interleaved}")
    print(f"  ... pure build-then-use: {total - interleaved}")
    zero = sum(1 for f in per if perbad[f] == 0)
    print(f"instrument two-sided: {interleaved} non-zero report(s); "
          f"{zero} of {len(per)} files report zero")

    print("\nper-file (interleaved/total):")
    for f in sorted(per):
        print(f"  {f}: {perbad[f]}/{per[f]}")
    print("\ninterleaved scopes (file, declline, var, lastmut, preceding-use lines):")
    for e in examples:
        print("  ", e)

    if a.naive_decl_count:
        n = 0
        for f in files:
            n += len(NAIVE_DECL.findall(open(os.path.join(root, f), errors="replace").read()))
        # Printed as two raw facts, NOT compared. The sets are not nested in
        # either direction: the naive regex matches never-mutated `table_view X;`
        # declarations this scoper skips, and this scoper matches `auto`
        # declarations the naive regex cannot spell. A count comparison between
        # them tests nothing. Drive the migration from --emit-sites.
        print(f"\nnaive `table_view X;` regex matches: {n}")
        print(f"scoper's mutated declarations (both spellings): {total}")

    # THE GUARD RUNS BEFORE --emit-sites IS WRITTEN, and that ordering is the
    # point. A site table is "the sole driver of the migration", so producing a
    # complete-LOOKING one and relying on the caller to check `$?` is the exact
    # failure mode `--emit-set` withholding was added to the sweep to avoid: a
    # file someone opens is a file someone trusts. An unattributed receiver means
    # NO site table is produced at all.
    print("\nattribution guard (every mutator-call HEAD RECEIVER must fall inside a "
          "scoped decl):")
    print(f"  head receivers attributed to a scoped declaration:    {attributed}")
    print(f"  head receivers already on a `table_view_builder`:     {on_builder}")
    print(f"  head receivers attributed to NOTHING:                 {unattributed}")
    print(f"  chain CONTINUATION lines (`    .add_valid(…)`, no receiver token of\n"
          f"    their own — in neither column above; renaming the head migrates\n"
          f"    them, so this is a scope statement, not a residue):  {continuation}")
    leaks = sorted(k for k in unscoped if k not in allowed)
    stale = sorted(k for k in allowed if k not in unscoped)
    for k in sorted(unscoped):
        print(f"    {k}  {unscoped[k]} call(s)"
              + ("  [--known-unscoped]" if k in allowed else "  << UNATTRIBUTED"))
    if stale:
        # An allow-list entry that no longer names anything is an exemption
        # outliving its subject: it would silently absorb a future receiver of
        # the same name in the same file.
        print("!! --known-unscoped NAMES A RECEIVER THAT NO LONGER EXISTS: "
              + ", ".join(stale) + "\n   The allow-list has outlived its subject; "
              "delete the entry rather than carrying it.", file=sys.stderr)
        return 3
    if leaks:
        print("!! MUTATOR CALLS ATTRIBUTED TO NO DECLARATION THIS SCOPER FOUND:\n   "
              + "\n   ".join(f"{k}  {unscoped[k]} call(s)" for k in leaks)
              + "\n   --emit-sites is the SOLE driver of the migration, so a call it does "
                "not\n   attribute is a call nobody rewrites. Either the declaration "
                "spelling is\n   one this scoper cannot read, or the receiver is a "
                "reference with no local\n   to rename — migrate it by hand and name it on "
                "--known-unscoped.\n   NO SITE TABLE WAS WRITTEN: an incomplete driver is "
                "worse than none.", file=sys.stderr)
        return 4

    if a.emit_sites:
        with open(a.emit_sites, "w", encoding="utf-8") as fh:
            fh.write("# path\tdecl_line\tvar\tlast_mutator_line\tscope_end_line\tinterleaves\n")
            for r in rows:
                fh.write("\t".join(str(x) for x in r) + "\n")
        print(f"\nsites written: {a.emit_sites} ({len(rows)} rows)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
