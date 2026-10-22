#!/usr/bin/env python3
"""tools/bench_seed.py — write a bench baseline that records its seeding commit (#276).

  bench_seed.py [--repo DIR] [--allow-dirty] <current.json> <baseline.json>

Copies Google Benchmark JSON (`--benchmark_format=json`) to <baseline.json>
with `context.git_sha` added: the full SHA of HEAD in DIR (default: the
current directory). Every other byte is what Google Benchmark wrote, except
`context.git_dirty` under --allow-dirty (below).

WHY. A baseline seeded mid-PR and then invalidated by a later commit of the same
PR describes code that never shipped (#276). With the seeding commit recorded,
"did anything after it touch what this baseline measures?" is one command:

    git log --name-only <git_sha>..HEAD -- <paths the baseline covers> ':!bench/baselines'

That range means "after seeding" only while <git_sha> is an ancestor of HEAD
(`git merge-base --is-ancestor <git_sha> HEAD`): ask it on the PR branch. After a
squash merge the seeding commit is off `main`, so end the range at the PR's head
(`git fetch origin pull/<N>/head`) instead of at `main`.

A baseline WITHOUT the field has an unknown seeding commit. Do not backfill one:
a SHA written after the fact is a guess presented as a record.

DIRTY TREE — refused. A SHA that does not describe the code that produced the
numbers is the defect this helper exists to remove, so a tree with tracked
modifications or untracked non-ignored files (an untracked source can enter the
build through a `file(GLOB ...)`) is refused and nothing is written.
<current.json> and <baseline.json> themselves are exempt, since seeding
necessarily creates them.
`--allow-dirty` seeds anyway and records that it did, as `context.git_dirty:
true` next to `git_sha`.

LIMIT, stated as a condition: the helper sees the tree when it runs, not when the
benchmark binary was built. The SHA is only true of the measurement when the
helper is run in the checkout the binary was built from, before anything else is
committed or edited there.

HOW the bytes are kept. Textual insertion, never a json.load/json.dump round
trip (which would reformat numbers, escapes and indentation). Raw Google
Benchmark output always opens with the `context` object, so the key is inserted
as its first member, reusing the whitespace that follows its `{`. Before writing,
the result is checked two ways: deleting the inserted span reproduces the input
exactly, and it parses to the input plus only the added key(s). Any input not
shaped like raw Google Benchmark output is refused rather than guessed at.

Exit status: 0 written; 1 refused (reason on stderr, nothing written); 2 usage.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile

_ANCHOR = re.compile(rb'\A(\s*\{\s*"context"\s*:\s*\{)(\s*)')


class Refused(Exception):
    pass


def _git(repo: str, *args: str) -> bytes:
    try:
        return subprocess.run(["git", "-C", repo, *args], check=True,
                              capture_output=True).stdout
    except FileNotFoundError:
        raise Refused("git is not installed")
    except subprocess.CalledProcessError as exc:
        msg = exc.stderr.decode(errors="replace").strip()
        raise Refused(f"git {' '.join(args)} failed in {repo!r}: {msg}")


def head_sha(repo: str) -> str:
    sha = _git(repo, "rev-parse", "--verify", "HEAD^{commit}").decode().strip()
    if not re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", sha):
        raise Refused(f"git rev-parse returned an unexpected SHA: {sha!r}")
    return sha


def dirty_paths(repo: str, exempt: list[str]) -> list[str]:
    """Tracked modifications and untracked non-ignored files, minus `exempt`."""
    top = os.path.realpath(_git(repo, "rev-parse", "--show-toplevel").decode().strip())
    exempt_real = {os.path.realpath(p) for p in exempt}
    # --no-renames: a rename is then a deletion record for its source plus an
    # addition record for its destination, in either status column, so every -z
    # entry is one record naming one path and no source can hide behind an
    # exempt destination.
    out = _git(repo, "status", "--porcelain=v1", "-z", "--no-renames",
               "--untracked-files=all")
    dirty: list[str] = []
    for e in out.split(b"\0"):
        if not e:
            continue
        status, path = e[:2].decode(), e[3:].decode(errors="surrogateescape")
        if os.path.realpath(os.path.join(top, path)) in exempt_real:
            continue
        dirty.append(f"{status} {path}")
    return dirty


def seed(raw: bytes, sha: str, dirty: bool) -> bytes:
    m = _ANCHOR.match(raw)
    if not m:
        raise Refused("input does not open with a `context` object, so it is not raw "
                      "Google Benchmark --benchmark_format=json output")
    try:
        before = json.loads(raw)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise Refused(f"input is not valid JSON: {exc}")
    ctx = before.get("context") if isinstance(before, dict) else None
    if not isinstance(ctx, dict) or not ctx:
        raise Refused("`context` is not a non-empty object")
    for key in ("git_sha", "git_dirty"):
        if key in ctx:
            raise Refused(f"input already carries context.{key}; refusing to overwrite it")
    ws = m.group(2)
    span = ws + b'"git_sha": "' + sha.encode() + b'",'
    if dirty:
        span += ws + b'"git_dirty": true,'
    out = raw[:m.end(1)] + span + raw[m.end(1):]

    # Post-conditions: the bytes are the input's, and the document is the input's
    # plus only the added key(s). Either failing is a defect here, not in the input.
    if out[:m.end(1)] + out[m.end(1) + len(span):] != raw:
        raise Refused("internal: removing the inserted span does not reproduce the input")
    after = json.loads(out)
    expect = dict(before)
    expect["context"] = dict(ctx, git_sha=sha, **({"git_dirty": True} if dirty else {}))
    if after != expect:
        raise Refused("internal: the written document differs from the input by more "
                      "than the added key(s)")
    return out


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description="Seed a bench baseline recording context.git_sha (#276)")
    p.add_argument("current", help="Google Benchmark JSON output (--benchmark_format=json)")
    p.add_argument("baseline", help="baseline path to write")
    p.add_argument("--repo", default=".", help="checkout the benchmark was built from (default: .)")
    p.add_argument("--allow-dirty", action="store_true",
                   help="seed from a dirty tree, recording context.git_dirty: true")
    a = p.parse_args(argv)

    try:
        sha = head_sha(a.repo)
        dirty = dirty_paths(a.repo, [a.current, a.baseline])
        if dirty and not a.allow_dirty:
            listing = "\n".join(f"  {d}" for d in dirty)
            raise Refused("working tree is dirty, so HEAD does not describe the measured "
                          f"code:\n{listing}\ncommit or stash first, or pass --allow-dirty "
                          "to record context.git_dirty: true")
        try:
            with open(a.current, "rb") as f:
                raw = f.read()
        except OSError as exc:
            raise Refused(f"cannot read {a.current!r}: {exc}")
        out = seed(raw, sha, bool(dirty))
        d = os.path.dirname(os.path.abspath(a.baseline))
        try:
            fd, tmp = tempfile.mkstemp(dir=d, prefix=".bench_seed.")
        except OSError as exc:
            raise Refused(f"cannot write into {d!r}: {exc}")
        try:
            with os.fdopen(fd, "wb") as f:
                f.write(out)
            os.replace(tmp, a.baseline)
        except BaseException:
            os.unlink(tmp)
            raise
    except Refused as exc:
        print(f"bench_seed: REFUSED — {exc}", file=sys.stderr)
        return 1
    print(f"bench_seed: wrote {a.baseline} (context.git_sha={sha}"
          f"{', git_dirty=true' if dirty else ''})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
