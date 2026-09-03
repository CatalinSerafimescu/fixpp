#!/usr/bin/env python3
r"""AST audit of the NAMED-CLOSURE `co_spawn` form (issue #354).

#291 fixed the immediately-invoked TEMPORARY form and shipped
`tools/check_co_spawn_lambda.py` — a buildless lexer — to stop it recurring. A
second form carries the same closure-lifetime requirement and is out of that
guard's reach BY CONSTRUCTION:

    auto lam = [&]() -> asio::awaitable<void> { ... };
    asio::co_spawn(ioc, lam(), asio::detached);   // not flagged by the lexer

The coroutine frame reaches captures THROUGH the closure object, so `lam` must
outlive the coroutine. Whether it does is a LIFETIME question, not a lexical one.

WHY THE CRITERION IS "FIND THE DRIVING CALL" AND NOT SOMETHING CHEAPER
---------------------------------------------------------------------
It is tempting to ask instead whether the closure outlives the `io_context` —
that is structural and cheap. It is also the WRONG question for the common shape.
Destruction runs in reverse declaration order, so in the overwhelmingly common

    asio::io_context ioc;
    auto lam = [&]() -> awaitable<void> { ... };
    co_spawn(ioc, lam(), detached);
    ioc.run();

the closure dies BEFORE the io_context, and the site is nevertheless safe —
because `ioc.run()` drives the coroutine to completion while the closure is still
alive. Any criterion that does not look for that call reports this shape as a
defect, i.e. ~every site in the tree. So the driving call is the criterion; there
is no way around it, and this tool does not pretend otherwise.

CLASSIFICATION
--------------
  SAFE-OUTLIVES  the closure is declared BEFORE the io_context in the same scope,
                 or in a strictly enclosing scope. Then it outlives the context
                 itself, so it is safe whether or not the coroutine ever runs.
                 (A frame destroyed at its initial suspend point runs frame-local
                 destructors but never the body, so it never reads a capture.)

  SAFE-DRIVEN    a driving call (`run`, `run_for`, `run_one`, `run_until`, `poll`,
                 `poll_one`, or a future `.get()`/`.wait()`) appears lexically
                 AFTER the co_spawn and while the closure is still in scope.

  FLAG           the closure's scope closes before any driving call this tool can
                 see. This is the shape the issue describes as the unsafe variant.

  MANUAL         everything else: the closure is a member or a parameter, or the
                 driving call is in a helper, a fixture destructor, or another TU.
                 NOT a verdict — an instruction to read the site.

⚠️ MANUAL IS NOT "PROBABLY FINE". Reporting a site as MANUAL and then counting it
with the safe ones is exactly how the issue's own five-site sample turned into a
bill of health for ~190. Every MANUAL must be dispositioned by a human.

⚠️ THIS TOOL'S ZEROS ARE NOT SELF-PROVING. Before believing a clean sweep, seed a
FLAG: take a real safe site, wrap its closure declaration in a bare `{ ... }` so
the scope closes before the driving call, and confirm the walker emits FLAG.
`--self-test` does this against synthetic fixtures; the in-tree seeded arm is what
proves the walker works against the REAL compile flags and headers.

KNOWN FALSE-SAFE PATHS — read these before trusting a SAFE verdict
------------------------------------------------------------------
Every one of these makes the tool report SAFE where a human might not. They are
listed because a tool whose limits are undocumented gets trusted past them.

  1. A driving call that is written but never REACHED — inside an uncalled
     lambda, an untaken branch, or after an early return. The walker is lexical
     about ordering (source offset) and structural about scope; it does not do
     reachability. `auto f = [&]{ ioc.run(); };` declared after the co_spawn
     counts as a drive.
  2. A driving call in a DIFFERENT file from the closure (a .cpp driving a
     harness header's co_spawn). Scope keys are cursor identities and do not
     compare across files, so this lands as FLAG or MANUAL — the loud direction,
     but it means such sites need reading rather than dismissing.
  3. `SAFE-OUTLIVES` assumes a coroutine frame destroyed at its initial suspend
     point never reads a capture. That is true for a frame that never started —
     it runs frame-local destructors, not the body — and it is the basis for
     treating "closure outlives the io_context" as safe.
  4. A closure whose driving call is a member function of a fixture (e.g. a
     destructor) is MANUAL, not SAFE. That is deliberate: it is exactly the
     shape where lifetime and lexical order diverge.

⚠️ POPULATION: DO NOT TRUST #354's "~190". That figure came from

    callExpr(callee(functionDecl(hasName("co_spawn"))),
             hasAnyArgument(cxxOperatorCallExpr(hasOverloadedOperatorName("()"))))

run through clang-query. In clang-query's DEFAULT traversal mode (`AsIs`) that
matcher reports **0 matches on a file containing 3 real sites** — measured on
tests/sync/test_fifo_fairness.cpp — because implicit nodes (materialize-temporary,
construct-expr, casts) sit between the co_spawn argument and the operator call.
It only reports correctly under `set traversal IgnoreUnlessSpelledInSource`. So
the census that produced ~190 was run through an instrument that CAN report a
false low, and the number must be re-derived rather than reconciled against.
`--reconcile` re-derives it a second way and diffs the site SETS.

RUN IT LOCALLY, ON DEMAND. IT IS DELIBERATELY NOT WIRED INTO CI.
---------------------------------------------------------------
A full sweep is ~242 TUs at roughly 31 s each (a real clang parse of the actual
compile command — asio, gtest, C++23, no PCH), i.e. ~35 min at 4 cores and hours
on a 2-vCPU runner. A diff-scoped variant would be affordable but strictly
weaker: it cannot see a site whose SAFETY changed because a driving call moved in
a file the diff did not touch. Either way it needs a configured build, so it
could only live in a GATED job — emitting nothing during the review rounds that
are the only thing between a fresh unsafe site and merge, which is exactly the
window #291's buildless lexer was placed to cover.

Weigh that against what it guards. The immediately-invoked form is already caught
by tools/check_co_spawn_lambda.py in the ungated job. What is left to this tool
is the NAMED-closure form, which needs a closure declared in a narrower scope
than its driving call — and the population is safe today. Installing a two-hour
job with a pip libclang dependency to watch that is a worse trade than running
this when there is a reason to.

WHEN THERE IS A REASON TO:
  * before a release, or after a wave of new co_spawn sites;
  * when touching the pump/drain helpers, since DRIVING_FREE_FUNCTIONS decides
    what counts as driven and a rename there silently turns callers into FLAGs;
  * when #291's lexer fires, as the AST-level follow-up to whatever it caught.

Usage:  python3 tools/audit_co_spawn_named_closure.py --jobs 4
        python3 tools/audit_co_spawn_named_closure.py --filter <path-substring>

⚠️ Do not record this tool's RESULT here. A count in a comment rots silently and
nothing re-runs it. The measured outcome of a run belongs in the issue or PR that
commissioned it, dated and tied to a SHA.
"""

from __future__ import annotations

import argparse
import concurrent.futures as cf
import json
import os
import shlex
import subprocess
import sys

import clang.cindex as ci

# ⚠️ A DRIVING CALL IS IDENTIFIED BY ITS OWNER, NEVER BY ITS NAME ALONE, and this
# is the single easiest way to make this tool report a false clean. An earlier
# version matched bare method NAMES, which meant `unique_ptr::get()` — one of the
# most common calls in the tree — registered as "the coroutine was driven here".
# That would have marked essentially every site SAFE-DRIVEN and produced a
# confident, worthless sweep. Every entry below therefore names the OWNING TYPE
# the method must resolve to.
DRIVING_METHODS: dict[str, tuple[str, ...]] = {
    # asio execution contexts: these actually run handlers.
    "run": ("io_context", "thread_pool", "execution_context"),
    "run_for": ("io_context", "thread_pool"),
    "run_one": ("io_context", "thread_pool"),
    "run_one_for": ("io_context", "thread_pool"),
    "run_one_until": ("io_context", "thread_pool"),
    "run_until": ("io_context", "thread_pool"),
    "poll": ("io_context", "thread_pool"),
    "poll_one": ("io_context", "thread_pool"),
    # A future handed back by co_spawn(..., use_future): blocking on it means the
    # coroutine has completed. Restricted to future/shared_future so that
    # unique_ptr::get, optional::value, span::data and friends cannot qualify.
    "get": ("future", "shared_future"),
    "wait": ("future", "shared_future"),
    "wait_for": ("future", "shared_future"),
    "wait_until": ("future", "shared_future"),
}

# Free-function pump helpers in this tree. They take the io_context by reference
# and drive it; they have no owning class, so they are listed by name and must be
# re-derived rather than trusted — a helper renamed or added here without
# updating this set makes its callers read as FLAG (loud), never as SAFE (quiet),
# which is the direction this list is allowed to be wrong in.
# DERIVED, not guessed: every free helper in tests/support/pump_until_ready.hpp
# that takes `asio::io_context&` and drives it. Re-derive with
#   git grep -n "io_context& *ioc" -- tests/support/
# rather than extending this list from memory — the first version of it omitted
# `pump_until`, which is the most-used helper in the tree, and that produced two
# FALSE FLAGs in tests/sync/test_drain_onstrand_cancel_during_reap.cpp.
#
# ⚠️ The failure was LOUD, and that is the property to preserve when editing this:
# a helper missing from this set makes its callers read as FLAG (a finding someone
# reads), never as SAFE (a silence nobody checks). Keep it biased that way — if in
# doubt about whether something drives, leave it OUT.
DRIVING_FREE_FUNCTIONS = frozenset(
    {
        "pump_until",
        "pump_until_ready",
        "run_window_then_ready",
        "drain_or_report",
        "cancel_and_drain_or_report",
        "run_until",
        "pump_or_report_throw",
    }
)

IOC_TYPE_MARKERS = ("io_context", "thread_pool")


# A free function's semantic parent is a NAMESPACE or the translation unit, not
# "" — which is what an earlier version tested for. `pump_until` lives in
# `fixpp::test_support`, so its parent spelled "test_support" and it was never
# recognised as a driving helper, keeping the two false FLAGs alive even after it
# was added to the list. Distinguish by KIND, not by an empty spelling.
_FREE_FUNCTION_PARENTS = frozenset(
    {
        ci.CursorKind.NAMESPACE,
        ci.CursorKind.TRANSLATION_UNIT,
        ci.CursorKind.UNEXPOSED_DECL,
    }
)


def owner_of(cursor) -> str:
    """Spelling of the CLASS declaring the callee; "" when it is a free function."""
    ref = cursor.referenced
    if ref is None:
        return ""
    parent = ref.semantic_parent
    if parent is None:
        return ""
    if kind_of(parent) in _FREE_FUNCTION_PARENTS:
        return ""
    return parent.spelling or ""


def is_lambda_type(t: str) -> bool:
    return "(lambda at " in t


# ⚠️ VERSION SKEW IS STRUCTURAL HERE, AND IT IS HANDLED RATHER THAN IGNORED.
# The tree is compiled by clang 22, so the compilation database's flags need
# LLVM 22's libclang to parse (C++23). But PyPI's `libclang` bindings top out at
# 18.1.1, and LLVM 22 returns cursor-kind ids that enum does not know — every one
# raises `ValueError: Unknown template argument kind N` out of `cursor.kind`.
#
# The resolution is NOT to pin an older libclang.so (it cannot parse the tree).
# It is to depend only on cursor kinds that have been stable since early LLVM —
# COMPOUND_STMT, VAR_DECL, CALL_EXPR, DECL_REF_EXPR — and to RECURSE THROUGH a
# node whose kind the bindings cannot name rather than pruning it. Structure is
# preserved: an unknown parent never hides a known child.
#
# The residual risk is a node that IS one of the four AND has been renumbered,
# which would make this tool under-report. `unknown_kinds` is printed for exactly
# that reason: a sweep with a large unknown count and zero sites is not clean, and
# the zero-sites denominator below fails closed on it.
UNKNOWN_KINDS: dict[int, int] = {}


def kind_of(cursor):
    """cursor.kind, or None when the bindings cannot name it (see above)."""
    try:
        return cursor.kind
    except ValueError:
        try:
            UNKNOWN_KINDS[cursor._kind_id] = UNKNOWN_KINDS.get(cursor._kind_id, 0) + 1
        except Exception:
            pass
        return None


class SiteWalker:
    """One pass over a TU, tracking the enclosing compound-statement stack."""

    def __init__(self, tu_file: str, repo_root: str):
        self.tu_file = os.path.realpath(tu_file)
        self.repo_root = os.path.realpath(repo_root)
        self.sites: list[dict] = []
        # scope_id -> list of (offset, kind, name) events, in source order
        self.events: list[dict] = []

    # ── scope identity: the stack of COMPOUND_STMT cursor hashes ──
    @staticmethod
    def scope_key(stack: list[int]) -> tuple[int, ...]:
        return tuple(stack)

    def walk(self, cursor, stack: list[int]) -> None:
        k = kind_of(cursor)
        pushed = False
        if k is not None and k == ci.CursorKind.COMPOUND_STMT:
            stack.append(cursor.hash)
            pushed = True

        # ⚠️ IN-REPO, NOT IN-TU. Restricting to the TU's own .cpp would silently
        # drop every site that lives in a test HEADER — 18 headers in this tree
        # contain co_spawn, including engine_loopback_harness.hpp and several
        # fixtures, i.e. exactly the shared scaffolding a lifetime defect would
        # be worst in. Headers are seen once per including TU, so the caller
        # dedupes by (file, line, col).
        try:
            loc = cursor.location
            fn = loc.file.name if loc.file is not None else None
            in_tu = fn is not None and os.path.realpath(fn).startswith(self.repo_root + os.sep)
            if in_tu and (os.sep + "build" + os.sep) in os.path.realpath(fn):
                in_tu = False
        except Exception:
            in_tu = False

        if in_tu and k is not None:
            self._record(cursor, stack)

        for child in cursor.get_children():
            self.walk(child, stack)

        if pushed:
            stack.pop()

    def _record(self, cursor, stack: list[int]) -> None:
        k = kind_of(cursor)
        if k is None:
            return
        off = cursor.extent.start.offset

        # (a) VarDecls: closures and io_contexts
        if k == ci.CursorKind.VAR_DECL:
            tspell = cursor.type.spelling
            if is_lambda_type(tspell):
                self.events.append(
                    {
                        "kind": "closure_decl",
                        "name": cursor.spelling,
                        "off": off,
                        "end": cursor.extent.end.offset,
                        "scope": self.scope_key(stack),
                        "line": cursor.location.line,
                    }
                )
            elif any(m in tspell for m in IOC_TYPE_MARKERS):
                self.events.append(
                    {
                        "kind": "ioc_decl",
                        "name": cursor.spelling,
                        "off": off,
                        "scope": self.scope_key(stack),
                        "line": cursor.location.line,
                    }
                )
            return

        if k != ci.CursorKind.CALL_EXPR:
            return

        spelling = cursor.spelling

        # (b) driving calls — owner-checked, see DRIVING_METHODS' header
        owners = DRIVING_METHODS.get(spelling)
        own = owner_of(cursor) if owners is not None else ""
        is_method_drive = owners is not None and any(o in own for o in owners)
        is_free_drive = spelling in DRIVING_FREE_FUNCTIONS and owner_of(cursor) == ""
        if is_method_drive or is_free_drive:
            self.events.append(
                {
                    "kind": "drive",
                    "name": spelling,
                    "off": off,
                    "scope": self.scope_key(stack),
                    "line": cursor.location.line,
                }
            )
            return

        # (c) co_spawn sites with a named-closure invocation argument
        if spelling != "co_spawn":
            return

        closure_var = self._named_closure_arg(cursor)
        if closure_var is None:
            return
        self.sites.append(
            {
                "file": os.path.realpath(cursor.location.file.name),
                "line": cursor.location.line,
                "col": cursor.location.column,
                "off": off,
                "scope": self.scope_key(stack),
                "closure": closure_var,
            }
        )

    @staticmethod
    def _named_closure_arg(call) -> str | None:
        """Return the closure VARIABLE's spelling if an argument is `var()`.

        Only a call through a NAMED closure object counts. An immediately-invoked
        temporary is #291's form and belongs to the lexer, not here; a call to a
        plain awaitable-returning function (`run_liveness_loop()`) is not a
        closure at all and is exactly what the issue warns the crude grep
        over-counts.
        """
        for arg in call.get_arguments():
            # ⚠️ PRUNE AT LAMBDA BODIES. An unpruned descent walks INTO the spawned
            # lambda's own body and mistakes a closure invoked in there for the
            # spawned argument itself. Measured: it reported
            # `co_spawn(..., read_bufs(), ...)` at
            # tests/transport/test_asio_plain_transport_config.cpp:238, where the
            # argument is a correctly-passed uninvoked `[&]() -> awaitable<void>`
            # and `read_bufs` is a plain non-coroutine helper called inside it.
            # This is the same confusion tools/check_co_spawn_lambda.py's docstring
            # calls out — telling the argument-level invocation from an inner
            # invoked lambda nested inside a correctly-passed outer one.
            for node in [arg] + list(_descend(arg, prune_lambda_bodies=True)):
                if kind_of(node) != ci.CursorKind.CALL_EXPR:
                    continue
                ref = node.referenced
                if ref is None or ref.spelling != "operator()":
                    continue
                # The object being called: a DeclRefExpr to a VarDecl of lambda type.
                for sub in _descend(node):
                    if kind_of(sub) != ci.CursorKind.DECL_REF_EXPR:
                        continue
                    d = sub.referenced
                    if d is None or kind_of(d) != ci.CursorKind.VAR_DECL:
                        continue
                    if is_lambda_type(d.type.spelling):
                        return d.spelling
        return None


def _descend(cursor, prune_lambda_bodies: bool = False):
    """Depth-first walk. With prune_lambda_bodies, do not enter a LAMBDA_EXPR.

    The lambda node itself is still yielded — the caller may need to see that the
    argument IS a lambda — but nothing inside its body is, so a closure invoked
    within the spawned coroutine cannot be mistaken for the spawned argument.
    """
    for c in cursor.get_children():
        yield c
        if prune_lambda_bodies and kind_of(c) == ci.CursorKind.LAMBDA_EXPR:
            continue
        yield from _descend(c, prune_lambda_bodies)


def encloses(outer: tuple, inner: tuple) -> bool:
    """True if scope `outer` is `inner` or an ancestor of it."""
    return len(outer) <= len(inner) and inner[: len(outer)] == outer


def classify(site: dict, events: list[dict]) -> tuple[str, str]:
    closures = [
        e
        for e in events
        if e["kind"] == "closure_decl"
        and e["name"] == site["closure"]
        and e["off"] < site["off"]
        and encloses(e["scope"], site["scope"])
    ]
    if not closures:
        return ("MANUAL", "closure declaration not found in an enclosing block scope "
                          "(member, parameter, or captured from another TU)")
    decl = max(closures, key=lambda e: e["off"])
    cscope = decl["scope"]

    # SAFE-OUTLIVES: an io_context declared in the SAME scope AFTER the closure,
    # or in a scope the closure's scope encloses, dies first.
    for e in events:
        if e["kind"] != "ioc_decl":
            continue
        if e["scope"] == cscope and e["off"] > decl["off"]:
            return ("SAFE-OUTLIVES", f"io_context `{e['name']}` (line {e['line']}) is declared "
                                     f"after the closure in the same scope, so the closure outlives it")
        if len(e["scope"]) > len(cscope) and encloses(cscope, e["scope"]):
            return ("SAFE-OUTLIVES", f"io_context `{e['name']}` (line {e['line']}) lives in a scope "
                                     f"nested inside the closure's, so the closure outlives it")

    # SAFE-DRIVEN: a driving call after the co_spawn, while the closure is in scope.
    for e in events:
        if e["kind"] != "drive" or e["off"] <= site["off"]:
            continue
        if encloses(cscope, e["scope"]):
            return ("SAFE-DRIVEN", f"`{e['name']}()` at line {e['line']} runs after the co_spawn "
                                   f"and inside the closure's scope")

    return ("FLAG", f"closure `{site['closure']}` declared at line {decl['line']} goes out of "
                    f"scope with no driving call after the co_spawn inside that scope")


def parse_tu(entry: dict, extra_args: list[str]) -> tuple[list[dict], list[dict], str | None]:
    idx = ci.Index.create()
    args = [a for a in entry["arguments"][1:] if a not in ("-c",)]
    # Drop the output and input file args; keep flags/includes/defines.
    cleaned: list[str] = []
    skip_next = False
    for a in args:
        if skip_next:
            skip_next = False
            continue
        if a == "-o":
            skip_next = True
            continue
        if a.endswith((".cpp", ".cc", ".cxx", ".o", ".obj")):
            continue
        cleaned.append(a)
    cleaned += extra_args

    path = os.path.join(entry.get("directory", "."), entry["file"])
    try:
        tu = idx.parse(path, args=cleaned, options=0)
    except ci.TranslationUnitLoadError as exc:
        return [], [], f"parse failed: {exc}"

    # ⚠️ ERROR, NOT JUST FATAL. A non-fatal semantic error (severity Error) does
    # NOT stop libclang producing an AST — it produces a TRUNCATED one, and the
    # statement that failed to type-check simply is not there. This tool then
    # reports FEWER sites for that TU and looks clean.
    #
    # Measured, not theorised: while seeding a deliberate FLAG into
    # tests/sync/test_fifo_fairness.cpp, the seed used `asio::detached` without
    # its include. The only symptom was `no member named 'detached'` at severity
    # Error — the site count stayed at 3 instead of rising to 4, with no warning.
    # An earlier version of this function bailed only on Fatal and would have
    # reported that tree clean. A TU that does not type-check is UNSEEN, not safe.
    bad = [d for d in tu.diagnostics if d.severity >= ci.Diagnostic.Error]
    if bad:
        loc = bad[0].location
        where = f"{os.path.basename(loc.file.name)}:{loc.line}" if loc.file else "?"
        return [], [], f"{len(bad)} error diagnostic(s), first at {where}: {bad[0].spelling}"

    w = SiteWalker(path, os.environ.get("FIXPP_REPO_ROOT", os.getcwd()))
    w.walk(tu.cursor, [])
    return w.sites, w.events, None


_WORKER_CFG: dict = {}


def _worker_init(libclang: str, repo_root: str) -> None:
    if libclang and os.path.exists(libclang):
        ci.Config.set_library_file(libclang)
    _WORKER_CFG["repo_root"] = repo_root


def _worker(job: tuple) -> tuple:
    """Parse ONE TU and classify its sites. Runs in a child process.

    Classification happens here rather than in the parent because a site's
    verdict depends on the EVENTS of the TU it was seen in, and shipping every
    TU's event list back would dominate the transfer.
    """
    entry, extra, repo_root = job
    os.environ["FIXPP_REPO_ROOT"] = repo_root
    sites, events, err = parse_tu(entry, extra)
    if err:
        return (entry["file"], [], err)
    out = []
    for s in sites:
        verdict, why = classify(s, events)
        s["verdict"] = verdict
        s["why"] = why
        out.append(s)
    return (entry["file"], out, None)


# Worst-first, so a dedupe across TUs can never quietly downgrade a header site.
VERDICT_RANK = {"FLAG": 0, "MANUAL": 1, "SAFE-DRIVEN": 2, "SAFE-OUTLIVES": 3}


def load_db(build_dir: str, only_with_cospawn: bool) -> list[dict]:
    with open(os.path.join(build_dir, "compile_commands.json"), encoding="utf-8") as fh:
        db = json.load(fh)
    out = []
    seen = set()
    for e in db:
        f = os.path.realpath(os.path.join(e.get("directory", "."), e["file"]))
        if f in seen:
            continue
        seen.add(f)
        if "arguments" not in e:
            # ⚠️ shlex, NOT str.split(). CMake emits `command` as a SHELL string, and
            # a string-valued macro appears in it shell-escaped:
            #     -DFIXPP_TLS_FIXTURE_DIR=\\"/path/to/fixtures\\"
            # A naive .split() keeps the backslashes, so libclang is handed a macro
            # whose expansion is `\"..."\` — not a valid expression. Every TU that
            # USES that macro then dies with "expected expression".
            #
            # Measured: this silently removed 40 of 242 TUs (16.5% of the
            # population) from the first full sweep — including engine_acceptor_test,
            # the loopback harness's dependents and every live-TLS session test, i.e.
            # exactly the co_spawn-dense files this audit exists for. It surfaced only
            # because a failed parse is reported as UNSEEN rather than clean; had this
            # tool bailed on Fatal only (as it originally did), the sweep would have
            # reported a confident, 16.5%-incomplete clean.
            e["arguments"] = shlex.split(e["command"])
        if only_with_cospawn:
            try:
                with open(f, encoding="utf-8", errors="replace") as src:
                    if "co_spawn" not in src.read():
                        continue
            except OSError:
                continue
        out.append(e)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    ap.add_argument("--build-dir", default="build/linux-clang-debug")
    ap.add_argument("--libclang", default="/opt/llvm22/lib/libclang.so")
    ap.add_argument(
        "--resource-dir",
        default="/opt/llvm22/lib/clang/22",
        help="clang resource dir (`clang++ -print-resource-dir`). libclang does not "
             "infer it from the compilation database, and without it every TU dies on "
             "'stddef.h file not found' — which this tool reports as a FAILED PARSE "
             "rather than as a clean file, deliberately.",
    )
    ap.add_argument("--filter", default="", help="only files whose path contains this substring")
    ap.add_argument("--json-out", default="", help="write the full site table here")
    ap.add_argument("--jobs", type=int, default=4, help="parallel TU parses")
    ap.add_argument("--all-files", action="store_true",
                    help="parse every TU, not just those mentioning co_spawn")
    args = ap.parse_args()

    if os.path.exists(args.libclang):
        ci.Config.set_library_file(args.libclang)

    extra = ["-resource-dir", args.resource_dir] if args.resource_dir else []
    entries = load_db(args.build_dir, not args.all_files)
    if args.filter:
        entries = [e for e in entries if args.filter in e["file"]]
        if not entries:
            print(f"ERROR: --filter {args.filter!r} matched no TU in the compilation database.")
            return 1

    best: dict[tuple[str, int, int], dict] = {}
    errors: list[tuple[str, str]] = []
    repo_root = os.environ.get("FIXPP_REPO_ROOT", os.getcwd())
    jobs = [(e, extra, repo_root) for e in entries]
    done = 0
    with cf.ProcessPoolExecutor(
        max_workers=args.jobs, initializer=_worker_init, initargs=(args.libclang, repo_root)
    ) as pool:
        for fname, sites, err in pool.map(_worker, jobs, chunksize=1):
            done += 1
            if err:
                errors.append((fname, err))
            for s in sites:
                key = (s["file"], s["line"], s["col"])
                prev = best.get(key)
                # A header is parsed once per includer. Keep the WORST verdict —
                # a dedupe must never be the thing that turns a FLAG into a SAFE.
                if prev is None or VERDICT_RANK[s["verdict"]] < VERDICT_RANK[prev["verdict"]]:
                    best[key] = s
            if done % 20 == 0:
                print(f"  ... {done}/{len(jobs)} TUs, {len(best)} sites so far", file=sys.stderr)
    all_sites = list(best.values())

    tally: dict[str, int] = {}
    for s in all_sites:
        tally[s["verdict"]] = tally.get(s["verdict"], 0) + 1

    print(f"TUs parsed            : {len(entries) - len(errors)}")
    print(f"TUs that FAILED parse : {len(errors)}")
    print(f"named-closure sites   : {len(all_sites)}")
    if UNKNOWN_KINDS:
        tot = sum(UNKNOWN_KINDS.values())
        print(f"unnamed cursor kinds  : {tot} node(s) over {len(UNKNOWN_KINDS)} id(s) "
              f"{sorted(UNKNOWN_KINDS)[:8]} — recursed through, not pruned (see header)")
    for v in ("FLAG", "MANUAL", "SAFE-DRIVEN", "SAFE-OUTLIVES"):
        print(f"  {v:<14}: {tally.get(v, 0)}")

    for s in sorted(all_sites, key=lambda x: (x["verdict"] != "FLAG", x["file"], x["line"])):
        if s["verdict"] in ("FLAG", "MANUAL"):
            rel = os.path.relpath(s["file"])
            print(f"\n[{s['verdict']}] {rel}:{s['line']}  co_spawn(..., {s['closure']}(), ...)")
            print(f"         {s['why']}")

    if errors:
        print(f"\n⚠️ {len(errors)} TU(s) failed to parse — these are NOT clean, they are UNSEEN:")
        for f, err in errors[:10]:
            print(f"    {os.path.relpath(f)}: {err}")

    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as fh:
            json.dump({"sites": all_sites, "errors": errors}, fh, indent=2)
        print(f"\nwrote {args.json_out}")

    # Fails closed: a sweep that parsed nothing, or found no site, reports the
    # same "0 FLAG" a clean tree does.
    if not entries:
        print("ERROR: no TUs selected — the scan reached nothing.")
        return 1
    if not all_sites:
        print("ERROR: zero named-closure sites found — the matcher reached nothing.")
        return 1
    # A FLAG is the whole point; fail on it.
    return 1 if any(s["verdict"] == "FLAG" for s in all_sites) else 0


if __name__ == "__main__":
    sys.exit(main())
