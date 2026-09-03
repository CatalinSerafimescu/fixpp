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
`--self-test` guards the DETECTOR against synthetic fixtures, which no denominator
can: parse counts and TU counts witness the scan's REACH, and a sweep reporting no
FLAG is otherwise indistinguishable from a walker that CANNOT emit FLAG — a broken
scope comparison, a VERDICT_RANK inversion, an unreachable branch. Each arm pins a
defect this tool actually had.

⚠️ `--self-test` IS NOT A SUBSTITUTE FOR THE IN-TREE SEEDED ARM. Its fixtures are
minimal TUs with a stub `co_spawn`; they cannot prove the walker survives the real
compile flags, the real headers, or asio's own types. Seed a real site by hand
(wrap a closure declaration in a bare block so its scope closes before the driving
call), run, confirm FLAG, revert — after EVERY change to the detector, not once.

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
`tools/reconcile_co_spawn_census.py` re-derives it a second way and diffs the site
SETS (not the counts). It consumes this tool's `--json-out`, including the exact
file population it scanned, so the two instruments cannot silently diverge.

IT RUNS IN CI (GATED), AND IT ALSO RUNS LOCALLY ON DEMAND.
----------------------------------------------------------
⚠️ An earlier version of this paragraph said "RUN IT LOCALLY, ON DEMAND. IT IS
DELIBERATELY NOT WIRED INTO CI" and argued the case at length. That decision was
reversed once the cost figure below was corrected, and the job now exists:
`co-spawn-closure-audit` in .github/workflows/tier1.yml. The argument is kept
below because the SHAPE of the trade did not change — only the number did.
A full sweep is 242 TUs and takes **539 s (~9 min) at `--jobs 4`**, measured end
to end, so roughly 18-20 min on a 2-vCPU runner.

⚠️ AN EARLIER VERSION OF THIS PARAGRAPH SAID "~31 s per TU, dominated by a real
clang parse ... ~35 min", AND BOTH HALVES WERE WRONG. Profiling put the libclang
parse at 2.2 s and the Python AST walk at 24.5 s of that 31 s — the parse was 8 %
of the cost, not the cause of it. `os.path.realpath` was being called for EVERY
cursor in the AST (4.19 M `lstat` calls against 517 distinct paths); memoising it
took the walk 25.1 s -> 4.4 s with output verified element-wise identical. The
figure mattered because it was cited as the reason not to wire this into CI, so
it is corrected here rather than quietly dropped.

WHAT THE JOB CANNOT DO, which the corrected cost does not change:
a diff-scoped variant is strictly WEAKER — it cannot see a site whose SAFETY
changed because a driving call moved in a file the diff did not touch — and
either variant needs a configured build, so it could only live in a GATED job,
emitting nothing during the review rounds that are the only thing between a fresh
unsafe site and merge. That window is exactly what #291's buildless lexer covers.
So the job is real but GATED: it cannot see a fresh unsafe site during review, and
that window still belongs to #291's buildless lexer. Do not delete the lexer on
the grounds that this job covers the same rule — it does not cover the same
WINDOW.

WHEN TO RUN IT LOCALLY ANYWAY, since the gated job is not enough:

Weigh that against what it guards. The immediately-invoked form is already caught
by tools/check_co_spawn_lambda.py in the ungated job. What is left to this tool
is the NAMED-closure form, which needs a closure declared in a narrower scope
than its driving call — and the population is safe today. Installing a two-hour
job with a pip libclang dependency to watch that is a worse trade than running
this when there is a reason to.

  
  * before a release, or after a wave of new co_spawn sites;
  * when touching the pump/drain helpers, since DRIVING_FREE_FUNCTIONS decides
    what counts as driven and a rename there silently turns callers into FLAGs;
  * when #291's lexer fires, as the AST-level follow-up to whatever it caught.

Usage — RUN THE PAIR, always:
        python3 tools/audit_co_spawn_named_closure.py --self-test
        python3 tools/audit_co_spawn_named_closure.py --jobs 4 --json-out sweep.json
        python3 tools/reconcile_co_spawn_census.py --audit-json sweep.json
        python3 tools/audit_co_spawn_named_closure.py --filter <path-substring>

⚠️ A SWEEP WITHOUT `--self-test` IS A GATE THAT CANNOT FAIL FOR THE REASON IT
EXISTS. The sweep's own counters witness REACH; only the fixtures witness
DETECTION. This is not hypothetical for this tool: the argument-level lambda
prune was BROKEN and every in-tree check still passed — implicit AST nodes happen
to wrap the argument in real code, so the bug hid. `--self-test` caught it.

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
import shutil
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


def is_context_type(tspell: str) -> bool:
    """True only for an actual execution CONTEXT, not something derived from one.

    ⚠️ A SUBSTRING TEST IS WRONG HERE, and it silently mis-classified every strand
    in the tree. `asio::make_strand(ioc)` has type
    `asio::strand<asio::io_context::executor_type>` — which CONTAINS "io_context",
    so a substring check called it a context declaration rather than an executor
    derived from one. The strand then never entered the alias map, so
    `co_spawn(strand, ...); ioc.run();` could not be resolved and read as FLAG.
    Measured: 5 such sites in tests/sync/, all false.

    The discriminator is that a context type is not PARAMETERISED BY one and is
    not a strand/executor. Re-derive from the type spelling, not from a name.
    """
    if any(k in tspell for k in ("strand", "executor", "any_io_executor")):
        return False
    return any(m in tspell for m in IOC_TYPE_MARKERS)


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


def root_object_of(cursor) -> str:
    """Spelling of the first VarDecl a DeclRefExpr in this subtree refers to.

    `ioc` for `ioc`, for `ioc.run()` and for `ioc.get_executor()`; `entry` for
    `*entry.session_strand`. Used to decide WHICH context a co_spawn was made on
    and WHICH context a driving call drove — without that correlation the
    classifier silences a real FLAG whenever any unrelated io_context happens to
    be in scope. Empty when it cannot be resolved, which the caller treats as
    MANUAL rather than as a match.
    """
    for node in [cursor] + list(_descend(cursor)):
        if kind_of(node) != ci.CursorKind.DECL_REF_EXPR:
            continue
        d = node.referenced
        if d is not None and kind_of(d) in (ci.CursorKind.VAR_DECL, ci.CursorKind.PARM_DECL):
            return d.spelling or ""
    return ""


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
    """cursor.kind, or None when the bindings cannot name it (see above).

    ⚠️ UNKNOWN_KINDS IS A PER-PROCESS COUNTER AND MUST BE RETURNED, NOT READ FROM
    THE PARENT. Every call happens inside a ProcessPoolExecutor child, so the
    parent's copy of this module global stays EMPTY no matter what the children
    saw. The tool shipped with `if UNKNOWN_KINDS:` in the parent as its only
    declared mitigation for the LLVM-22-vs-libclang-18 residual — i.e. a guard
    that was silent by construction, which is precisely the failure class this
    tool exists to avoid, inside the tool itself. `_worker` now returns the
    counter and the parent merges it.
    """
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
        self.repo_root = os.path.realpath(repo_root)
        # ⚠️ MEMOISED, and it is not a micro-optimisation. realpath() was called
        # for EVERY cursor in the AST — 4.19 M lstat calls and 13.2 s of a 25 s
        # walk on one TU, against 517 DISTINCT paths. Memoising made the walk
        # 25.1 s -> 4.4 s (5.8x) with sites and events verified element-wise
        # identical. The invariant is structural, not a survey: paths do not
        # change during a run.
        self._rp: dict[str, str] = {}
        self.sites: list[dict] = []
        # Flat, in source order: closure_decl / ioc_decl / drive records.
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
            if fn is None:
                in_tu = False
            else:
                real = self._rp.get(fn)
                if real is None:
                    real = os.path.realpath(fn)
                    self._rp[fn] = real
                in_tu = real.startswith(self.repo_root + os.sep) and (
                    os.sep + "build" + os.sep) not in real
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
            elif not is_lambda_type(tspell) and not is_context_type(tspell):
                # An executor/strand derived from a context: `auto s =
                # make_strand(ioc);`, `auto ex = ioc.get_executor();`. A coroutine
                # spawned on `s` is driven by running `ioc`, so the site's executor
                # must resolve transitively before any drive can be matched to it.
                # Without this, `co_spawn(strand1, ...); ioc.run();` read as FLAG —
                # measured, 6 such sites in tests/sync/, all false.
                #
                # Also catches the future a co_spawn was assigned to
                # (`auto f = co_spawn(...); f.get();`), whose `.get()` blocks until
                # that coroutine completes and is therefore a drive for THAT site.
                base = ""
                for ch in cursor.get_children():
                    base = root_object_of(ch)
                    if base and base != cursor.spelling:
                        break
                if base and base != cursor.spelling:
                    self.events.append(
                        {"kind": "exec_alias", "name": cursor.spelling, "base": base,
                         "off": off, "scope": self.scope_key(stack)}
                    )
                return
            elif is_context_type(tspell):
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
            # For a method call the object is the receiver; for a free pump helper
            # it is the io_context passed as its first argument. root_object_of on
            # the whole call expression resolves both.
            self.events.append(
                {
                    "kind": "drive",
                    "obj": root_object_of(cursor),
                    "name": spelling,
                    "off": off,
                    "scope": self.scope_key(stack),
                    "line": cursor.location.line,
                }
            )
            return

        # (c) a call to a LOCAL CLOSURE, e.g. `pump();`. Recorded because such a
        # helper very often IS the driving call: its body runs `ioc.run()` or
        # `ioc.poll()`, but that drive's source offset is BEFORE the co_spawn (the
        # lambda is declared earlier) even though it EXECUTES after. Without this,
        # every site driven through a local pump helper read as FLAG — measured, 12
        # of them, all false.
        if spelling == "operator()":
            ref = cursor.referenced
            parent = ref.semantic_parent if ref is not None else None
            if parent is not None and is_lambda_type(parent.type.spelling if parent.type else ""):
                pass
            callee = root_object_of(cursor)
            if callee:
                self.events.append(
                    {"kind": "closure_call", "name": callee, "off": off,
                     "scope": self.scope_key(stack), "line": cursor.location.line}
                )
            return

        # (d) co_spawn sites with a named-closure invocation argument
        if spelling != "co_spawn":
            return

        closure_var = self._named_closure_arg(cursor)
        if closure_var is None:
            return
        exec_args = list(call.get_arguments()) if False else list(cursor.get_arguments())
        self.sites.append(
            {
                "exec": root_object_of(exec_args[0]) if exec_args else "",
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
            # `co_spawn(..., read_bufs(), ...)` in
            # tests/transport/test_asio_plain_transport_config.cpp's
            # `AsioPlainTransportConfig.LingerAndBufferSizeKnobsApplied`, where the
            # argument is a correctly-passed uninvoked `[&]() -> awaitable<void>`
            # and the `auto read_bufs = [&](const asio::ip::tcp::socket& s, ...)`
            # declared inside it is a plain non-coroutine helper.
            # This is the same confusion tools/check_co_spawn_lambda.py's docstring
            # calls out — telling the argument-level invocation from an inner
            # invoked lambda nested inside a correctly-passed outer one.
            # ⚠️ THE ARGUMENT ITSELF MAY BE THE LAMBDA, and pruning only NESTED
            # ones is not enough. A correctly-passed uninvoked `[&]{...}` is not a
            # named-closure site, so descending into its body finds whatever it
            # invokes internally and reports THAT as the spawned argument.
            #
            # This survived the in-tree check by luck: there, implicit nodes
            # (materialize-temporary / construct-expr) sit between the call and the
            # lambda, so `arg` was a wrapper and the nested-prune caught it. In a
            # fixture where the lambda is the argument directly, it was not caught.
            # Found by --self-test, which is the arm's entire purpose.
            if kind_of(arg) == ci.CursorKind.LAMBDA_EXPR:
                continue
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

    # ⚠️ THE EXECUTOR MUST MATCH. Without this, ANY io_context in scope silenced a
    # real finding: a closure spawned on `a` was certified SAFE because an
    # unrelated local `b` was declared later or had `b.run()` called. Both hostile
    # reviewers found this independently, and one DEMONSTRATED it by driving
    # classify() on the same event list with and without one extra unrelated
    # ioc_decl — FLAG became SAFE-OUTLIVES. It is a false-SAFE for the exact UAF
    # this tool exists to catch.
    #
    # An unresolvable executor is MANUAL, never a match. Guessing in the
    # permissive direction here is what the bug was.
    alias = {e["name"]: e["base"] for e in events if e["kind"] == "exec_alias"}

    def resolve(name: str) -> set[str]:
        """All names this one may stand for: itself plus its alias chain.

        `strand1` -> {strand1, ioc}. Matching on the SET keeps the executor
        correlation honest (an unrelated context still cannot certify a site)
        while accepting the legitimate indirections this tree actually uses.
        """
        seen, cur = {name}, name
        while cur in alias and alias[cur] not in seen:
            cur = alias[cur]
            seen.add(cur)
        return seen

    site_exec = site.get("exec", "")
    if not site_exec:
        return ("MANUAL", "could not resolve which io_context/executor this site spawns on, "
                          "so no driving call or context lifetime can be matched to it")

    # SAFE-OUTLIVES: THIS site's io_context declared in the SAME scope AFTER the
    # closure, or in a scope the closure's scope encloses, dies first.
    for e in events:
        if e["kind"] != "ioc_decl" or e["name"] not in resolve(site_exec):
            continue
        if e["scope"] == cscope and e["off"] > decl["off"]:
            return ("SAFE-OUTLIVES", f"io_context `{e['name']}` (line {e['line']}) is declared "
                                     f"after the closure in the same scope, so the closure outlives it")
        if len(e["scope"]) > len(cscope) and encloses(cscope, e["scope"]):
            return ("SAFE-OUTLIVES", f"io_context `{e['name']}` (line {e['line']}) lives in a scope "
                                     f"nested inside the closure's, so the closure outlives it")

    # Which local closures drive THIS site's context somewhere in their body? A
    # drive inside a helper lambda is recorded at the helper's own source offset,
    # which is EARLIER than the co_spawn that the helper later drives — so it must
    # be attributed to the CALL, not to the definition.
    driving_closures: set[str] = set()
    for d in events:
        if d["kind"] != "closure_decl":
            continue
        body_lo, body_hi = d["off"], d.get("end", d["off"])
        for e in events:
            if (e["kind"] == "drive" and e.get("obj", "") in resolve(site_exec)
                    and body_lo <= e["off"] <= body_hi):
                driving_closures.add(d["name"])
                break

    # SAFE-DRIVEN: a driving call ON THIS SITE'S CONTEXT, after the co_spawn, while
    # the closure is in scope — either directly, or through a local helper closure
    # whose body drives it.
    for e in events:
        if e["off"] <= site["off"]:
            continue
        if e["kind"] == "closure_call" and e["name"] in driving_closures:
            if encloses(cscope, e["scope"]):
                return ("SAFE-DRIVEN", f"`{e['name']}()` at line {e['line']} runs after the "
                                       f"co_spawn and drives `{site_exec}` in its body")
            continue
        if e["kind"] != "drive":
            continue
        if e.get("obj", "") not in resolve(site_exec):
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


def _worker_init(libclang: str, repo_root: str) -> None:
    # The parent already validated existence; a child that cannot load the named
    # library must raise rather than fall back to a different one.
    if libclang:
        ci.Config.set_library_file(libclang)


def _worker(job: tuple) -> tuple:
    """Parse ONE TU and classify its sites. Runs in a child process.

    Classification happens here rather than in the parent because a site's
    verdict depends on the EVENTS of the TU it was seen in, and shipping every
    TU's event list back would dominate the transfer.
    """
    entry, extra, repo_root = job
    os.environ["FIXPP_REPO_ROOT"] = repo_root
    UNKNOWN_KINDS.clear()
    sites, events, err = parse_tu(entry, extra)
    if err:
        return (entry["file"], [], err, dict(UNKNOWN_KINDS))
    out = []
    for s in sites:
        verdict, why = classify(s, events)
        s["verdict"] = verdict
        s["why"] = why
        out.append(s)
    return (entry["file"], out, None, dict(UNKNOWN_KINDS))


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
            except OSError as exc:
                # NOT `continue`. An unreadable source used to be dropped from the
                # population entirely -- not an error, not UNSEEN, and the `files`
                # list handed to the cross-check shrank to match, so the second
                # instrument could not see the hole either. Fail-toward-clean in a
                # tool whose whole argument is "UNSEEN, not clean". Keep it: the
                # parse will fail and be reported as an error, which now also
                # fails the run.
                print(f"cannot read {f}: {exc} -- kept in the population so it is "
                      f"reported as UNSEEN rather than dropped", file=sys.stderr)
        out.append(e)
    return out


# ─────────────────────────────────────────────────────────────────────────────
# Self-test — guards the DETECTOR, which no denominator can.
# ─────────────────────────────────────────────────────────────────────────────

# Minimal stand-ins. Deliberately NOT asio: the walker keys on the callee spelling
# `co_spawn`, on a VarDecl whose type spells `(lambda at ...)`, and on the OWNER of
# a driving call — none of which needs the real library. Keeping the fixtures
# library-free is what makes this runnable without a compilation database.
SELF_TEST_PREAMBLE = """
namespace std { template <class T> struct unique_ptr { T* get() const; }; }
struct io_context {
  void run();
  void poll();
};
namespace fixpp { namespace test_support {
  // Defined, not just declared: a template instantiated with a LAMBDA type has no
  // linkage, so a declaration-only stub makes the fixture fail to compile — and a
  // fixture that does not compile is a FAILED arm here, deliberately, never a skip.
  template <class F> bool pump_until(io_context&, F) { return true; }
} }
template <class E, class A, class T> void co_spawn(E&, A, T) {}
struct detached_t {}; static detached_t detached;
"""

SELF_TEST_CASES: list[tuple[str, str, list[tuple[str, str]]]] = [
    (
        "closure in a NARROWER scope than the driving call is FLAGged",
        """
void f() {
  io_context ioc;
  { auto lam = [&]() { return 0; }; co_spawn(ioc, lam(), detached); }
  ioc.run();
}
""",
        [("lam", "FLAG")],
    ),
    (
        "closure in the SAME scope, driving call after, is SAFE-DRIVEN",
        """
void f() {
  io_context ioc;
  auto lam = [&]() { return 0; };
  co_spawn(ioc, lam(), detached);
  ioc.run();
}
""",
        [("lam", "SAFE-DRIVEN")],
    ),
    (
        "closure declared BEFORE the io_context outlives it — SAFE-OUTLIVES",
        """
void f() {
  auto lam = [&]() { return 0; };
  io_context ioc;
  co_spawn(ioc, lam(), detached);
}
""",
        [("lam", "SAFE-OUTLIVES")],
    ),
    (
        "a correctly-passed UNINVOKED lambda is not a named-closure site at all",
        """
void f() {
  io_context ioc;
  co_spawn(ioc, [&]() { return 0; }, detached);
  ioc.run();
}
""",
        [],
    ),
    (
        "a closure invoked INSIDE the spawned lambda is not the spawned argument",
        # The lambda-body prune. Without it this reported the inner call as the
        # site — one false MANUAL in tests/transport/test_asio_plain_transport_config.cpp.
        """
void f() {
  io_context ioc;
  co_spawn(ioc, [&]() { auto inner = [&]() { return 1; }; return inner(); }, detached);
  ioc.run();
}
""",
        [],
    ),
    (
        "a LOCAL PUMP HELPER counts as the driving call (its drive is lexically earlier)",
        # `pump` is declared before the co_spawn, so the ioc.run() inside its body
        # has an EARLIER source offset than the site — but it EXECUTES after, via
        # the call. Attributing the drive to the definition instead of the call
        # produced 12 false FLAGs in tests/sync/ on a real sweep.
        """
void f() {
  io_context ioc;
  auto pump = [&]() { ioc.run(); };
  auto lam = [&]() { return 0; };
  co_spawn(ioc, lam(), detached);
  pump();
}
""",
        [("lam", "SAFE-DRIVEN")],
    ),
    (
        "a STRAND resolves back to its io_context (co_spawn(strand,...); ioc.run())",
        """
struct strand_t { };
strand_t make_strand(io_context&);
void f() {
  io_context ioc;
  auto s = make_strand(ioc);
  auto lam = [&]() { return 0; };
  co_spawn(s, lam(), detached);
  ioc.run();
}
""",
        [("lam", "SAFE-DRIVEN")],
    ),
    (
        "a strand from a DIFFERENT context still does not certify the site",
        """
struct strand_t { };
strand_t make_strand(io_context&);
void f() {
  io_context a;
  io_context b;
  auto s = make_strand(b);
  auto lam = [&]() { return 0; };
  co_spawn(s, lam(), detached);
  a.run();
}
""",
        [("lam", "FLAG")],
    ),
    (
        "a local helper that drives a DIFFERENT context does not certify the site",
        """
void f() {
  io_context a;
  io_context b;
  auto pump_b = [&]() { b.run(); };
  auto lam = [&]() { return 0; };
  co_spawn(a, lam(), detached);
  pump_b();
}
""",
        [("lam", "FLAG")],
    ),
    (
        "an UNRELATED io_context driven in scope must NOT certify the site",
        # Both hostile reviewers found this independently; one demonstrated it by
        # driving classify() with and without the extra ioc_decl. `lam` is spawned
        # on `a` and only `b` is ever run, so this is a real UAF and must FLAG.
        """
void f() {
  io_context a;
  {
    io_context b;
    auto lam = [&]() { return 0; };
    co_spawn(a, lam(), detached);
    b.run();
  }
  a.run();
}
""",
        [("lam", "FLAG")],
    ),
    (
        "an unrelated io_context declared later must NOT give SAFE-OUTLIVES",
        """
void f() {
  io_context a;
  auto lam = [&]() { return 0; };
  co_spawn(a, lam(), detached);
  { io_context unrelated; (void)unrelated; }
}
""",
        [("lam", "FLAG")],
    ),
    (
        "driving the RIGHT context still gives SAFE-DRIVEN when two are in scope",
        """
void f() {
  io_context a;
  io_context b;
  auto lam = [&]() { return 0; };
  co_spawn(a, lam(), detached);
  b.run();
  a.run();
}
""",
        [("lam", "SAFE-DRIVEN")],
    ),
    (
        "unique_ptr::get() must NOT count as a driving call (owner check)",
        # An earlier version keyed on the bare method NAME, so this read as driven
        # and would have marked essentially every site in the tree SAFE-DRIVEN.
        """
void f() {
  io_context ioc;
  { auto lam = [&]() { return 0; }; co_spawn(ioc, lam(), detached); }
  std::unique_ptr<int> p;
  p.get();
}
""",
        [("lam", "FLAG")],
    ),
    (
        "a NAMESPACED free pump helper does count as driving (owner_of kind check)",
        # owner_of once tested for an empty parent SPELLING; a free function's
        # parent is a NAMESPACE, so fixpp::test_support::pump_until was missed and
        # produced two false FLAGs.
        """
void f() {
  io_context ioc;
  auto lam = [&]() { return 0; };
  co_spawn(ioc, lam(), detached);
  fixpp::test_support::pump_until(ioc, [&]{ return true; });
}
""",
        [("lam", "SAFE-DRIVEN")],
    ),
]


def _load_libclang(libclang: str) -> str | None:
    """Point cindex at an EXPLICIT library, or say why not.

    ⚠️ AN EXPLICIT PATH THAT DOES NOT EXIST IS AN ERROR, NOT A FALLBACK. This
    used to be `if libclang and os.path.exists(libclang): set_library_file(...)`,
    which silently ignored a wrong `--libclang` and loaded whatever cindex could
    find — in practice PyPI's bundled 18, which cannot parse this C++23 tree. The
    self-test still passed 7/7 through the fallback, because its fixtures are
    simple enough for 18; only a full sweep would have shown the damage, as 242
    UNSEEN files. A caller that names a library means it.
    """
    if not libclang:
        return None
    if not os.path.exists(libclang):
        return f"--libclang {libclang!r} does not exist"
    ci.Config.set_library_file(libclang)
    return None


def run_self_test(libclang: str, resource_dir: str) -> int:
    err = _load_libclang(libclang)
    if err:
        print(f"ERROR: {err}")
        return 1
    args = ["-std=c++20", "-x", "c++"]
    if resource_dir:
        args += ["-resource-dir", resource_dir]

    passed = failed = 0
    for name, body, expected in SELF_TEST_CASES:
        src = SELF_TEST_PREAMBLE + body
        idx = ci.Index.create()
        tu = idx.parse("selftest.cpp", args=args, unsaved_files=[("selftest.cpp", src)])
        bad = [d for d in tu.diagnostics if d.severity >= ci.Diagnostic.Error]
        if bad:
            print(f"FAIL  {name}\n      fixture did not compile: {bad[0].spelling}")
            failed += 1
            continue
        w = SiteWalker("selftest.cpp", os.getcwd())
        # The fixture is an unsaved buffer, not a repo file; accept it explicitly.
        w.repo_root = ""
        w.walk(tu.cursor, [])
        got = sorted((s["closure"], classify(s, w.events)[0]) for s in w.sites)
        if got != sorted(expected):
            print(f"FAIL  {name}\n      expected {sorted(expected)}\n      got      {got}")
            failed += 1
        else:
            passed += 1

    total = passed + failed
    print(f"\nself-test: {passed}/{total} passed")
    return 1 if failed else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    ap.add_argument("--build-dir", default="build/linux-clang-debug")
    # ⚠️ NO HARDCODED LOCAL PATH. An absolute author-machine default is what broke
    # the cross-check tool in CI on its first run. Empty means "let cindex find
    # one"; the CI job passes both explicitly, derived from the LLVM it installed.
    ap.add_argument(
        "--libclang", default=os.environ.get("FIXPP_LIBCLANG", ""),
        help="path to libclang.so (default: $FIXPP_LIBCLANG, else cindex's own search)",
    )
    ap.add_argument(
        "--resource-dir",
        default=os.environ.get("FIXPP_CLANG_RESOURCE_DIR", ""),
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
    ap.add_argument("--self-test", action="store_true",
                    help="run the detector fixtures and exit (needs no compilation database)")
    args = ap.parse_args()

    if args.self_test:
        return run_self_test(args.libclang, args.resource_dir)

    err = _load_libclang(args.libclang)
    if err:
        print(f"ERROR: {err}")
        return 1

    extra = ["-resource-dir", args.resource_dir] if args.resource_dir else []
    entries = load_db(args.build_dir, not args.all_files)
    if args.filter:
        entries = [e for e in entries if args.filter in e["file"]]
        if not entries:
            print(f"ERROR: --filter {args.filter!r} matched no TU in the compilation database.")
            return 1

    # ⚠️ THE POPULATION IS PART OF THE RESULT, not an implementation detail.
    # tools/reconcile_co_spawn_census.py diffs its site SET against this run's, and
    # a set-diff is only sound if both instruments saw the SAME files. When
    # reconcile re-derived the list itself, a `--filter`ed audit made every site
    # outside the filter appear as "only clang-query" -- the bucket whose whole
    # point is that each entry must be hand-inspected -- while a truncated
    # reconcile run manufactured "only libclang (ALARMING)" hits, which is the
    # signal reserved for "the matcher is missing a shape". Both directions turn
    # the cross-check into noise that reads like a finding. Emitting the list here
    # makes the population shared BY CONSTRUCTION; the two DETECTORS stay
    # independent, which is the part the cross-check actually rests on.
    population = sorted(
        os.path.realpath(os.path.join(e.get("directory", "."), e["file"])) for e in entries
    )

    best: dict[tuple[str, int, int], dict] = {}
    errors: list[tuple[str, str]] = []
    repo_root = os.environ.get("FIXPP_REPO_ROOT", os.getcwd())
    jobs = [(e, extra, repo_root) for e in entries]
    done = 0
    with cf.ProcessPoolExecutor(
        max_workers=args.jobs, initializer=_worker_init, initargs=(args.libclang, repo_root)
    ) as pool:
        for fname, sites, err, unknown in pool.map(_worker, jobs, chunksize=1):
            done += 1
            for kid, cnt in unknown.items():
                UNKNOWN_KINDS[kid] = UNKNOWN_KINDS.get(kid, 0) + cnt
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
            json.dump({"sites": all_sites, "errors": errors, "files": population}, fh, indent=2)
        print(f"\nwrote {args.json_out}")

    # Fails closed: a sweep that parsed nothing, or found no site, reports the
    # same "0 FLAG" a clean tree does.
    if not entries:
        print("ERROR: no TUs selected — the scan reached nothing.")
        return 1
    if not all_sites:
        print("ERROR: zero named-closure sites found — the matcher reached nothing.")
        return 1

    # ⚠️ A FAILED PARSE FAILS THE RUN. It used to only PRINT, so a sweep where 1 of
    # 242 TUs did not parse — the one holding the unsafe closure — returned 0 while
    # the surviving TUs satisfied every denominator. The comments and the CI job
    # both claimed parse failures were loud; only the print was. An UNSEEN file is
    # not a clean file, and the exit code is the only part CI reads.
    if errors:
        print(f"\nERROR: {len(errors)} TU(s) were not parsed. They are UNSEEN, not clean.")
        return 1

    # MANUAL is not a verdict, it is an instruction to read the site. Letting it
    # pass silently is how a sampled audit becomes a bill of health.
    manual = [s for s in all_sites if s["verdict"] == "MANUAL"]
    if manual:
        print(f"\nERROR: {len(manual)} site(s) need a human disposition (MANUAL).")
        return 1

    return 1 if any(s["verdict"] == "FLAG" for s in all_sites) else 0


if __name__ == "__main__":
    sys.exit(main())
