#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# tests/codegen/golden_091_residual_diff.py
#
# 091-data-field-bytes T038 [US1]: structural validation of the regenerated
# 078 builder goldens (specs/091-data-field-bytes/contracts/codegen-builders.md
# C-2.4; spec FR-013). Not registered in ctest: it compares two git revisions
# of specs/078-precompiled-builder-libs/contracts/golden/ (by default
# origin/main, the goldens before 091, and HEAD, the regenerated ones).
#
# The goldens may differ only by FR-013's three transformations:
#   (a) a coupled Length+Data site rerouted from the two-call form
#       (`r_len`/`r_data`) to one `field_data`/`set_data` call;
#   (b) the `message_encoding` Args member (last) and its emit (first body
#       statement);
#   (c) on v50sp2 only, the pairs research.md R-11 names become coupled: the
#       Length member is deleted and the two single-field emits become one
#       coupled call.
#
# Checks, in order:
#   1. Positive control. A stray `message_encoding` emit is injected inside a
#      group body of one regenerated golden (a temp copy) and the residual below
#      is computed for it. It must be non-empty, else the instrument cannot fire
#      and the script stops with exit 2.
#   2. Residual. Each transformation is normalised out of both sides: (a) the
#      old two-call site is rewritten to the coupled form, (c) the old pair of
#      single-field blocks is rewritten to the coupled block and the old Length
#      member deleted, (b) the new member is deleted only where it is the last
#      member and the new emit only where it is the first body statement.
#      Old and new must then be byte-identical, file by file; a file present
#      on one side only is residual too.
#   3. Correspondence for (a) and (c): per file, the new coupled call sites
#      equal the old two-call sites plus the (c) sites, each exactly once; the
#      (c) pairs seen are exactly R-11's, and only on v50sp2. For (b): the
#      messages whose member was deleted are exactly those whose emit was
#      deleted from both the .builder.cpp and the .builder.inl.
#   4. The C-2.2 census (codegen_091_data_census_test) over the new goldens,
#      (a), (c) and (b)'s message set. A missing census binary is exit 2.
#
# Usage:
#   python3 tests/codegen/golden_091_residual_diff.py \
#       [--base origin/main] [--head HEAD] \
#       [--census-bin build/linux-clang-debug/bin/codegen_091_data_census_test]
# Run from the library root. Exit 0: every check passed. Exit 1: a check
# failed (the residual is printed as a unified diff). Exit 2: the positive
# control did not fire, or the script could not run.

import argparse
import collections
import difflib
import os
import re
import shutil
import subprocess
import sys
import tempfile

GOLDEN = "specs/078-precompiled-builder-libs/contracts/golden"
PAIRS_HEADER = "include/fixpp/core/length_data_pairs.hpp"

# research.md R-11: the standard pairs FIX 5.0 SP2 declares adjacently only
# inside components or groups, (Length, Data). Written out here rather than
# derived from either golden, so a change to the set must be made on purpose.
R11_PAIRS = {(2494, 2493), (2815, 2814), (43109, 42684), (43110, 42486), (43111, 42982)}
R11_VERSION = "v50sp2"

STATIC_ASSERT = "{ind}static_assert(::fixpp::wire::dict_hooks::none().length_tag_for_data({d}) == {l});"
COUPLED_CALL = "{ind}auto r_pair = {recv}.{call}({d}, ::std::as_bytes(::std::span{{*{member}}}));"
COUPLED_GUARD = "{ind}if (!r_pair) return ::std::unexpected(r_pair.error());"

TWO_CALL = re.compile(
    r"^(?P<ind>[ ]*)auto r_len = (?P<recv>\w+)\.(?P<lcall>field|set_int)\((?P<l>\d+), "
    r"static_cast<::std::int64_t>\((?P<member>[\w.]+)->size\(\)\)\);\n"
    r"(?P=ind)if \(!r_len\) return ::std::unexpected\(r_len\.error\(\)\);\n"
    r"(?P=ind)auto r_data = (?P=recv)\.(?P<dcall>field|set_string)\((?P<d>\d+), \*(?P=member)\);\n"
    r"(?P=ind)if \(!r_data\) return ::std::unexpected\(r_data\.error\(\)\);\n",
    re.M,
)
SINGLE_BLOCK = re.compile(
    r"^(?P<ind>[ ]*)if \((?P<obj>\w+)\.(?P<m>\w+)\) \{\n"
    r"(?P=ind)    auto r = (?P<recv>\w+)\.(?P<call>\w+)\((?P<tag>\d+), \*(?P=obj)\.(?P=m)\);\n"
    r"(?P=ind)    if \(!r\) return ::std::unexpected\(r\.error\(\)\);\n"
    r"(?P=ind)\}\n",
    re.M,
)
COUPLED_SITE = re.compile(
    r"^(?P<ind>[ ]*)static_assert\(::fixpp::wire::dict_hooks::none\(\)\.length_tag_for_data"
    r"\((?P<d>\d+)\) == (?P<l>\d+)\);\n"
    r"(?P=ind)auto r_pair = (?P<recv>\w+)\.(?P<call>field_data|set_data)\((?P=d), "
    r"::std::as_bytes\(::std::span\{\*(?P<member>[\w.]+)\}\)\);\n",
    re.M,
)
BB_DECL = re.compile(r"^[ ]*::fixpp::wire::body_builder bb\{[^\n]*\};\n", re.M)
ENC_EMIT = (
    "{ind}if (args.message_encoding) {{\n"
    "{ind}    auto r = bb.field(347, *args.message_encoding);\n"
    "{ind}    if (!r) return ::std::unexpected(r.error());\n"
    "{ind}}}\n"
)
ENC_MEMBER = re.compile(r"^[ ]*::std::optional<::std::string_view> message_encoding\{\};\n(?=\};)",
                        re.M)


def die(msg, code=2):
    print(f"golden_091_residual_diff: {msg}", file=sys.stderr)
    sys.exit(code)


def git(*args):
    return subprocess.run(["git", *args], check=True, capture_output=True).stdout


def extract(rev, dest):
    os.makedirs(dest)
    tar = git("archive", "--format=tar", rev, GOLDEN)
    subprocess.run(["tar", "-x", "-C", dest], input=tar, check=True)
    return os.path.join(dest, GOLDEN)


def standard_pairs(rev):
    text = git("show", f"{rev}:{PAIRS_HEADER}").decode()
    decl = re.search(r"std::array<length_data_pair, (\d+)> standard_length_data_pairs\{\{", text)
    if not decl:
        die(f"no standard_length_data_pairs declaration in {PAIRS_HEADER} at {rev}")
    end = text.index("}};", decl.end())
    rows = re.findall(r"\{\.length_tag = (\d+),\s*\.data_tag = (\d+)\}", text[decl.end():end])
    # The array extent is the compiler-checked row count; a parse that finds
    # fewer rows than it is a parser defect, not a smaller table.
    if len(rows) != int(decl.group(1)):
        die(f"parsed {len(rows)} rows from {PAIRS_HEADER} at {rev}, "
            f"its extent is {decl.group(1)}")
    return {(int(l), int(d)) for l, d in rows}


def list_files(root):
    out = set()
    for dirpath, _, files in os.walk(root):
        for f in files:
            out.add(os.path.relpath(os.path.join(dirpath, f), root))
    return out


def read(root, rel):
    with open(os.path.join(root, rel), "rb") as fh:
        return fh.read().decode("utf-8")


def is_builder(rel):
    return rel.endswith(".builder.cpp") or rel.endswith(".builder.inl")


class Record:
    def __init__(self):
        self.two_call = collections.Counter()   # (rel, recv, d, l, member)
        self.c_sites = collections.Counter()    # (rel, recv, d, l, member)
        self.c_len_members = collections.defaultdict(set)  # version -> Length member names
        self.enc_member = set()                 # (version, message)
        self.enc_emit = collections.defaultdict(set)  # (version, message) -> {".builder.cpp", ...}
        self.problems = []


def rewrite_two_call(rel, text, pairs, rec):
    def sub(m):
        l, d = int(m["l"]), int(m["d"])
        top = m["recv"] == "bb"
        want = ("field", "field") if top else ("set_int", "set_string")
        if (l, d) not in pairs or (m["lcall"], m["dcall"]) != want:
            rec.problems.append(f"{rel}: two-call site L={l} D={d} is not a standard pair "
                                "in its arm's form")
            return m.group(0)
        rec.two_call[(rel, m["recv"], d, l, m["member"])] += 1
        ind = m["ind"]
        return "\n".join([
            STATIC_ASSERT.format(ind=ind, d=d, l=l),
            COUPLED_CALL.format(ind=ind, recv=m["recv"], call="field_data" if top else "set_data",
                                d=d, member=m["member"]),
            COUPLED_GUARD.format(ind=ind),
        ]) + "\n"
    return TWO_CALL.sub(sub, text)


def rewrite_r11_blocks(rel, version, text, rec):
    if version != R11_VERSION:
        return text
    blocks = list(SINGLE_BLOCK.finditer(text))
    out, pos, i = [], 0, 0
    while i < len(blocks):
        a = blocks[i]
        b = blocks[i + 1] if i + 1 < len(blocks) else None
        if b is not None and a.end() == b.start() and a["ind"] == b["ind"] and \
                a["obj"] == b["obj"] and a["recv"] == b["recv"]:
            ta, tb = int(a["tag"]), int(b["tag"])
            pair = next((p for p in ((ta, tb), (tb, ta)) if p in R11_PAIRS), None)
            if pair is not None:
                l, d = pair
                lb, db = (a, b) if ta == l else (b, a)
                top = a["recv"] == "bb"
                if (lb["call"], db["call"]) != (("field", "field") if top else ("set_int",
                                                                                 "set_string")):
                    rec.problems.append(f"{rel}: R-11 pair L={l} D={d} not in its arm's form")
                else:
                    member = f"{db['obj']}.{db['m']}"
                    ind = a["ind"]
                    inner = ind + "    "
                    out.append(text[pos:a.start()])
                    out.append(
                        f"{ind}if ({member}) {{\n"
                        + STATIC_ASSERT.format(ind=inner, d=d, l=l) + "\n"
                        + COUPLED_CALL.format(ind=inner, recv=a["recv"],
                                              call="field_data" if top else "set_data", d=d,
                                              member=member) + "\n"
                        + COUPLED_GUARD.format(ind=inner) + "\n"
                        + f"{ind}}}\n")
                    pos = b.end()
                    rec.c_sites[(rel, a["recv"], d, l, member)] += 1
                    rec.c_len_members[version].add(lb["m"])
                    i += 2
                    continue
        i += 1
    out.append(text[pos:])
    return "".join(out)


def delete_r11_length_members(rel, version, text, rec):
    names = rec.c_len_members.get(version, set())
    if not names:
        return text
    pat = re.compile(r"^[ ]*::std::optional<::std::int64_t> (" + "|".join(map(re.escape, names))
                     + r")\{\};\n", re.M)
    return pat.sub("", text)


def delete_enc_emit(rel, version, text, rec):
    m = BB_DECL.search(text)
    if not m:
        return text
    nxt = text[m.end():]
    ind = re.match(r"[ ]*", nxt).group(0)
    emit = ENC_EMIT.format(ind=ind)
    if nxt.startswith(emit):
        msg = os.path.basename(rel).split(".builder.")[0]
        rec.enc_emit[(version, msg)].add(rel[rel.index(".builder."):])
        return text[:m.end()] + nxt[len(emit):]
    return text


def delete_enc_member(rel, version, text, rec):
    new, n = ENC_MEMBER.subn("", text)
    if n:
        if not rel.startswith("messages/") and "/messages/" not in rel:
            rec.problems.append(f"{version}/{rel}: message_encoding member outside a message Args")
        rec.enc_member.add((version, os.path.basename(rel)[:-len(".hpp")]))
    return new


def coupled_sites(rel, text):
    c = collections.Counter()
    for m in COUPLED_SITE.finditer(text):
        c[(rel, m["recv"], int(m["d"]), int(m["l"]), m["member"])] += 1
    return c


def residual(old_root, new_root, pairs):
    """Returns (diff_lines, Record, new_coupled Counter)."""
    rec = Record()
    new_coupled = collections.Counter()
    old_files, new_files = list_files(old_root), list_files(new_root)
    diff = []
    for f in sorted(old_files ^ new_files):
        diff.append(f"only in {'old' if f in old_files else 'new'}: {f}\n")
    # Pass 1 over the old builders records the R-11 Length members, which the
    # header pass then deletes.
    old_norm = {}
    for f in sorted(old_files & new_files):
        version, _, rel = f.rpartition("/") if "/" not in f else f.partition("/")
        text = read(old_root, f)
        if is_builder(f):
            text = rewrite_two_call(f, text, pairs, rec)
            text = rewrite_r11_blocks(f, version, text, rec)
        old_norm[f] = text
    for f in sorted(old_files & new_files):
        version, _, rel = f.rpartition("/") if "/" not in f else f.partition("/")
        old = old_norm[f]
        new = read(new_root, f)
        if f.endswith(".hpp"):
            old = delete_r11_length_members(f, version, old, rec)
            new = delete_enc_member(rel, version, new, rec)
        if is_builder(f):
            new_coupled += coupled_sites(f, new)
            new = delete_enc_emit(f, version, new, rec)
        if old != new:
            diff.extend(difflib.unified_diff(old.splitlines(True), new.splitlines(True),
                                             f"old/{f}", f"new/{f}"))
    return diff, rec, new_coupled


def inject_control(root):
    """Inserts a stray message_encoding emit inside the first group-entry body of
    v44/messages/NewOrderSingle.builder.cpp; returns a description."""
    rel = "v44/messages/NewOrderSingle.builder.cpp"
    path = os.path.join(root, rel)
    text = read(root, rel)
    m = re.search(r"^([ ]*)auto& eh\d+ = \*en\d+;\n", text, re.M)
    if not m:
        die(f"control: no group-entry body in {rel}")
    nxt = text[m.end():]
    ind = re.match(r"[ ]*", nxt).group(0)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text[:m.end()] + ENC_EMIT.format(ind=ind) + nxt)
    line = text[:m.end()].count("\n") + 1
    return f"{rel}, after line {line} ({m.group(0).strip()})"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", default="origin/main")
    ap.add_argument("--head", default="HEAD")
    ap.add_argument("--census-bin", default="build/linux-clang-debug/bin/codegen_091_data_census_test")
    args = ap.parse_args()
    if not os.path.isfile(PAIRS_HEADER):
        die("run from the library root")

    pairs = standard_pairs(args.head)
    if not R11_PAIRS <= pairs:
        die("an R-11 pair is not a row of the standard table")
    tmp = tempfile.mkdtemp(prefix="golden091.")
    try:
        old_root = extract(args.base, os.path.join(tmp, "old"))
        new_root = extract(args.head, os.path.join(tmp, "new"))
        ctl_root = os.path.join(tmp, "control", "golden")
        shutil.copytree(new_root, ctl_root)

        print(f"base {args.base} = {git('rev-parse', args.base).decode().strip()}")
        print(f"head {args.head} = {git('rev-parse', args.head).decode().strip()}")

        # 1. Positive control.
        where = inject_control(ctl_root)
        ctl_diff, _, _ = residual(old_root, ctl_root, pairs)
        print(f"[control] stray message_encoding emit injected into {where}")
        print(f"[control] residual lines: {len(ctl_diff)}")
        sys.stdout.writelines("  " + l for l in ctl_diff[:12])
        if not ctl_diff:
            die("positive control: the residual is empty, the instrument cannot fire")

        # 2. Residual.
        diff, rec, new_coupled = residual(old_root, new_root, pairs)
        failed = False
        print(f"[residual] files compared: {len(list_files(new_root))}; "
              f"residual lines: {len(diff)}")
        if diff:
            failed = True
            sys.stdout.writelines(diff)
        for p in rec.problems:
            print(f"[residual] problem: {p}")
            failed = True

        # 3. Correspondence.
        expected_coupled = rec.two_call + rec.c_sites
        if new_coupled != expected_coupled:
            failed = True
            for k in sorted(set(new_coupled) | set(expected_coupled)):
                if new_coupled[k] != expected_coupled[k]:
                    print(f"[correspondence] {k}: new {new_coupled[k]}, "
                          f"old two-call + R-11 {expected_coupled[k]}")
        dup = [k for k, n in expected_coupled.items() if n != 1]
        for k in dup:
            failed = True
            print(f"[correspondence] site not unique: {k} x{expected_coupled[k]}")
        c_pairs = {(l, d) for (_, _, d, l, _) in rec.c_sites}
        c_versions = {k[0].split("/", 1)[0] for k in rec.c_sites}
        print(f"[correspondence] two-call sites: {sum(rec.two_call.values())}; "
              f"R-11 sites: {sum(rec.c_sites.values())}; new coupled sites: "
              f"{sum(new_coupled.values())}")
        print(f"[correspondence] R-11 pairs rewritten: {sorted(c_pairs)} in {sorted(c_versions)}; "
              f"Length members deleted: {sorted(rec.c_len_members.get(R11_VERSION, set()))}")
        if c_pairs != R11_PAIRS or c_versions != {R11_VERSION}:
            failed = True
            print("[correspondence] the R-11 rewrites are not exactly R-11's pairs on "
                  f"{R11_VERSION}")
        emit_both = {k for k, v in rec.enc_emit.items() if v == {".builder.cpp", ".builder.inl"}}
        if rec.enc_member != emit_both or set(rec.enc_emit) != emit_both:
            failed = True
            print("[message_encoding] member and emit sets differ: member-only "
                  f"{sorted(rec.enc_member - emit_both)}, emit-only "
                  f"{sorted(set(rec.enc_emit) - rec.enc_member)}")
        by_ver = collections.Counter(v for v, _ in rec.enc_member)
        print(f"[message_encoding] messages with member + first emit, per version: "
              f"{dict(sorted(by_ver.items()))}")

        # 4. Census over the new goldens.
        if os.path.isfile(args.census_bin):
            root = os.path.join(tmp, "census-root")
            os.makedirs(root)
            os.symlink(new_root, os.path.join(root, "fixpp"))
            r = subprocess.run([args.census_bin, root], capture_output=True, text=True)
            summary = [l for l in r.stdout.splitlines()
                       if l.startswith(("[census]", "[  PASSED", "[  FAILED", "[==="))]
            print("[census] " + args.census_bin)
            for l in summary:
                print("  " + l)
            if r.returncode != 0:
                failed = True
                print(r.stdout[-4000:])
        else:
            # (a) and (c) are defined as the census plus the correspondence
            # check, so a run without the census could not check them.
            die(f"no census binary at {args.census_bin}; build "
                "codegen_091_data_census_test or pass --census-bin")

        print("RESULT: " + ("FAIL" if failed else "PASS"))
        return 1 if failed else 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
