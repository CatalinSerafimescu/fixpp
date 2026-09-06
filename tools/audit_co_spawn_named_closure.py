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
import re
import shlex
import shutil
import subprocess
import sys

import clang.cindex as ci

# ⚠️ THE FIRST CROSS-TOOL IMPORT IN tools/, and it is deliberate. The alternative
# was a SECOND copy of a 54-line comment/literal blanker that must agree with the
# first forever; a byte-identical copy propagates a claim that is false at the new
# site the moment one of them is fixed. check_co_spawn_lambda is side-effect-free
# at module scope (constants + `if __name__`), so importing it runs nothing.
# sys.path[0] is tools/ when run as `python3 tools/<x>.py` (how CI invokes both),
# but that is not guaranteed for any other entry point -- hence the explicit
# insert rather than relying on it.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_co_spawn_lambda import splice, strip_noncode  # noqa: E402

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
# ⚠️ THIS SET IS WHY THIS TOOL RUNS IN CI, AND #289 BATCH 17 PROVED THE HEADER'S OWN
# WARNING LIVE. That warning says to re-run this "when touching the pump/drain helpers,
# since DRIVING_FREE_FUNCTIONS decides what counts as driven and a rename there silently
# turns callers into FLAGs". Batch 17 did not rename one -- it ADDED one
# (`run_to_exhaustion_or_report`, which replaces a bare `ioc.run()`), taught three other
# instruments about it, and missed this one. Seven sites across three files went FLAG in
# CI, every one of them correctly driven. The lesson is the enumeration, not the entry:
# when a new pump spelling lands, `git grep -l 'DRIVING_FREE_FUNCTIONS\|GUARD = re.compile\|CALLS = (' ci/ tools/`
# names every instrument that has to learn it.
DRIVING_FREE_FUNCTIONS = frozenset(
    {
        "pump_until",
        "pump_until_ready",
        "run_window_then_ready",
        "run_to_exhaustion_or_report",
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
        # Every in-repo file this walk touched a cursor in (#363). NOT the same as
        # "files with sites": a header can be parsed and contain no site at all,
        # and that distinction is exactly what the header gate needs — refusing on
        # "no site reported" would fail CI on a parsed header whose only co_spawn
        # is an ordinary coroutine call, which is ordinary code.
        self.files_seen: set[str] = set()
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
                if in_tu:
                    self.files_seen.add(real)
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


def parse_tu(
    entry: dict, extra_args: list[str]
) -> tuple[list[dict], list[dict], str | None, set[str]]:
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
        # ⚠️ PARITY WITH THE CROSS-CHECK, and it is not cosmetic. CMake's C++20
        # module scanner emits `@<obj>.modmap` into every compile command; those
        # files are written by the BUILD, so on the configure-only tree CI parses,
        # none exist. libclang does not implement `@file` expansion, so it sailed
        # past a missing one and reported a clean parse while clang-query — which
        # goes through the driver — errored on all 242 files. Relying on a parser's
        # failure to implement a feature is not a decision; strip it explicitly so
        # both instruments see the same command line. See sanitize_db() in
        # tools/reconcile_co_spawn_census.py for the measurement.
        if a.startswith("@") and a.endswith(".modmap"):
            continue
        cleaned.append(a)
    cleaned += extra_args

    path = os.path.join(entry.get("directory", "."), entry["file"])
    try:
        tu = idx.parse(path, args=cleaned, options=0)
    except ci.TranslationUnitLoadError as exc:
        return [], [], f"parse failed: {exc}", set()

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
        return [], [], f"{len(bad)} error diagnostic(s), first at {where}: {bad[0].spelling}", set()

    w = SiteWalker(path, os.environ.get("FIXPP_REPO_ROOT", os.getcwd()))
    w.walk(tu.cursor, [])
    return w.sites, w.events, None, w.files_seen


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
    sites, events, err, files_seen = parse_tu(entry, extra)
    if err:
        return (entry["file"], [], err, dict(UNKNOWN_KINDS), files_seen)
    out = []
    for s in sites:
        verdict, why = classify(s, events)
        s["verdict"] = verdict
        s["why"] = why
        out.append(s)
    return (entry["file"], out, None, dict(UNKNOWN_KINDS), files_seen)


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
            # ⚠️ SCOPE LIMIT (#363). This tests the MAIN FILE's own text. A header
            # carrying a named-closure `co_spawn` whose every including .cpp lacks
            # the literal token is therefore never parsed — and the cross-check
            # consumes this same population, so it cannot report the omission
            # either. Sharing the population is what makes the set-diff mean
            # anything; it also means the population is the one thing the PAIR
            # cannot check.
            #
            # ⚠️ IT IS NO LONGER SILENT, AND THAT IS THE WHOLE OF THE #363 FIX. A
            # third instrument outside the pair — screen_headers(), run from main()
            # on every prefiltered sweep — screens the headers directly and FAILS
            # the run if any carries the shape. The limit below is unchanged; what
            # changed is that hitting it is now loud instead of invisible. Do not
            # re-describe this as "undetectable"; do not delete the call in main()
            # without restoring some other way to see past this line.
            #
            # No count is recorded here — that would rot, and the check that
            # replaced the recipe cannot. The CONDITION is "a header with the
            # named-closure form, reachable only from TUs without the token";
            # screen_headers() evaluates exactly that on every run. ⚠️ A
            # line-based grep UNDER-REPORTS the shape twice over: the argument list
            # spans lines, and the closure is often invoked WITH arguments
            # (`make_waiter(1)`), so an empty-parens pattern silently misses it —
            # measured 2 against an AST truth of 10 on tests/sync/
            # test_fifo_across_cycles.cpp. Validate any screen against a file whose
            # site count this tool already reports before trusting a zero from it.
            #
            # `--all-files` removes the limit at the cost of parsing every TU.
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
        # ⚠️ ONE ARM PER FREE-FUNCTION DRIVER SPELLING, so widening DRIVING_FREE_FUNCTIONS is
        # proven rather than asserted. #289 batch 17 added `run_to_exhaustion_or_report` and
        # taught three other instruments about it while missing this set; seven correctly
        # driven sites across three files went FLAG in CI. An entry in a frozenset with no
        # arm is a claim, not a check.
        "run_to_exhaustion_or_report is a DRIVING free function (#289 batch 17)",
        """
namespace fixpp { namespace test_support {
bool run_to_exhaustion_or_report(io_context&, int&, const char*);
} }
void f() {
  io_context ioc;
  int fut = 0;
  auto lam = [&]() { return 0; };
  co_spawn(ioc, lam(), detached);
  if (!fixpp::test_support::run_to_exhaustion_or_report(ioc, fut, "S::C")) { return; }
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


# ─────────────────────────────────────────────────────────────────────────────
# HEADER SCREEN (#363) -- the population's own blind spot, made LOUD.
#
# The prefilter in load_db() tests each compile-database entry's MAIN FILE text
# for the literal token. A header carrying a named-closure co_spawn whose every
# including .cpp lacks the token is therefore never parsed -- and
# reconcile_co_spawn_census.py deliberately CONSUMES this population rather than
# re-deriving one (that is what makes its set-diff mean anything), so the pair
# cannot report the omission either. The population is the one thing the two
# instruments do not check.
#
# This does not widen the population -- parsing every TU costs ~20 min on 242
# TUs. It converts a SILENT hole into a LOUD one: screen the headers directly,
# and if any carries the shape, refuse to report and say to re-run --all-files.
# A false alarm here costs one --all-files run; a false zero costs the finding.
#
# ⚠️ THIS SCREEN IS AN INSTRUMENT AND IT IS PINNED IN --self-test, because the
# ISSUE'S OWN SCREEN WAS WRONG TWICE. A line-based grep misses the shape (the
# argument list spans lines) and an empty-parens pattern misses it again (the
# closure is frequently invoked WITH arguments, `make_waiter(1)`) -- measured 2
# against an AST truth of 10. Balanced-paren scanning is what reproduces 10/5.
FORWARDING_WRAPPERS = frozenset({"std::move", "move", "std::forward", "forward"})
# A leading `*` or `&` is a call through a pointer/reference to a closure --
# `(*p)()` after the paren unwrap. Nominate it; the walker decides.
_IDENT_HEAD = re.compile(r"^\s*[*&]?\s*([A-Za-z_][A-Za-z0-9_:]*)\s*")


def _after_first_top_level_comma(s: str) -> str | None:
    """Everything after `s`'s first depth-0 comma, or None if it has none.

    ⚠️ DELIBERATELY DOES NOT SPLIT THE WHOLE LIST. The previous version split on
    every top-level comma and looked at element [1], which silently LOST every
    template-instantiated call: `co_spawn(ioc, make_thing<A,B>(), tok)` split
    inside the template argument list and yielded `make_thing<A`. Returning the
    remainder uncut lets _names_a_call() do its own balanced `<...>` scan, so the
    comma inside `<>` never has to be recognised as such.
    """
    # ⚠️ ANGLE BRACKETS COUNT HERE TOO, AND FORGETTING THEM WAS THE SAME BUG
    # TWICE. `_names_a_call` learned to balance `<...>` for the CALLEE
    # (`make_thing<A,B>()`); this function is the other half and was left with the
    # identical hole, so `co_spawn(pool<A,B>::get(), make_waiter(1), tok)` split
    # inside the EXECUTOR's template list and lost the site. Fixing the callee and
    # not the splitter was fixing a blast radius rather than a cause.
    #
    # `<` is ambiguous with less-than, so it opens a template list only when it
    # directly follows an identifier character — `pool<` does, `a <` does not.
    # That is a heuristic; it errs toward NOMINATING, which is the safe direction
    # for a candidate generator whose over-approximation inside a parsed file is
    # free.
    depth = 0
    angle = 0
    prev = ""
    for i, ch in enumerate(s):
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        elif ch == "<" and depth == 0 and (prev.isalnum() or prev in "_>"):
            angle += 1
        elif ch == ">" and angle > 0:
            angle -= 1
        elif ch == "," and depth == 0 and angle == 0:
            return s[i + 1 :]
        if not ch.isspace():
            prev = ch
    return None


def _names_a_call(arg: str) -> str | None:
    r"""The callee name if `arg` begins `ident(` or `ident<...>(`, else None.

    ⚠️ THE `<...>` ARM IS NOT COSMETIC -- WITHOUT IT THE SCREEN FAILS TOWARD
    CLEAN. A bare `^ident\s*\(` pattern misses EVERY templated closure call,
    including the single-argument `make_thing<A>()`, because `<A>` sits between
    the identifier and the paren. Measured before the fix: `make_thing<A>()`,
    `make_thing<A,B>()` and `make_thing<A, B>(1)` all screened as NO HIT while
    the non-template control screened as a hit. In a screen whose whole purpose
    is to stop being silent, that is the defect it exists to prevent.
    """
    # ⚠️ A PARENTHESISED CALLEE IS ORDINARY C++ AND WAS MISSED. `co_spawn(ioc,
    # (lam)(), tok)` is the same site as `co_spawn(ioc, lam(), tok)` — the AST
    # walker sees through the parens and reports it — but a matcher anchored on
    # an identifier sees a leading `(` and returns nothing. Strip balanced
    # wrapping parens first. Found by hostile review, not by the self-test, which
    # is the fourth shape this screen has been wrong about.
    arg = arg.lstrip()
    while arg.startswith("("):
        depth, k = 0, 0
        while k < len(arg):
            if arg[k] == "(":
                depth += 1
            elif arg[k] == ")":
                depth -= 1
                if depth == 0:
                    break
            k += 1
        if depth != 0 or k + 1 >= len(arg):
            break
        inner, after = arg[1:k].strip(), arg[k + 1 :].lstrip()
        # `(lam)(...)` unwraps; `(a + b)` or a cast does not name a call.
        if not after.startswith("("):
            break
        arg = inner + after
    # ⚠️ THE CALLEE IS A PATH, NOT AN IDENTIFIER. `obj.make_waiter()`,
    # `self->make_waiter()` and `(*p)()` are all ordinary spellings of the same
    # site and were all missed while a bare `lam()` was caught. Walk
    # ident (`<...>`)? ( `.` | `->` | `::` ) ident (`<...>`)? ... and then require
    # the paren.
    m = _IDENT_HEAD.match(arg)
    if not m:
        return None  # a lambda literal `[&]...`, a brace-init, anything unnamed
    name, j = m.group(1), m.end()

    def skip_template(k: int) -> int | None:
        """Index past a balanced `<...>` at k, or None if it does not close."""
        if k >= len(arg) or arg[k] != "<":
            return k
        d = 0
        while k < len(arg):
            if arg[k] == "<":
                d += 1
            elif arg[k] == ">":
                d -= 1
                if d == 0:
                    return k + 1
            k += 1
        return None  # an unbalanced `<` is a comparison, not a template list

    while True:
        nxt = skip_template(j)
        if nxt is None:
            return None
        j = nxt
        while j < len(arg) and arg[j].isspace():
            j += 1
        for sep in ("->", "::", "."):
            if arg.startswith(sep, j):
                k = j + len(sep)
                while k < len(arg) and arg[k].isspace():
                    k += 1
                seg = _IDENT_HEAD.match(arg[k:])
                if not seg:
                    return None
                name, j = seg.group(1), k + seg.end()
                break
        else:
            break
    return name if j < len(arg) and arg[j] == "(" else None


def screen_named_closure(text: str) -> list[int]:
    """1-based line numbers of `co_spawn(<ex>, <ident>(<args>), ...)` in `text`.

    Whole-file and balanced-paren, NOT line-based -- see the note above for the
    two ways a simpler pattern reports a false zero on this exact shape.

    ⚠️ RUNS ON BLANKED CODE, NEVER ON RAW TEXT, and it used the raw text first.
    A `co_spawn(ex, name(...), ...)` inside a `//` comment, a `/* */` block, or a
    string literal screened IDENTICALLY to a real one -- measured, all four
    returning [1]. Since a hit REFUSES the whole audit in a gating CI step, one
    commented-out example added to any tracked header would have failed tier1
    with "a header carries the named-closure co_spawn shape". The sibling scanner
    check_co_spawn_lambda.scan_text() already blanks for exactly this reason on
    its own balanced-paren walk; reuse it rather than re-deriving it.
    splice() is not optional either: without it a backslash-newline inside a
    `/* ... */` close leaves the stripper believing the comment never closes,
    and it blanks the REST OF THE FILE into a clean result -- fail-toward-clean in the screen whose
    whole job is to stop being silent.

    Deliberately over-approximates: any identifier-call in argument 2 counts,
    because a screen that guesses wrong should guess LOUD. The one enumerated
    exception is FORWARDING_WRAPPERS -- `std::move(coro)` names a std facility
    and moves an awaitable, so it cannot be a named-closure invocation. That
    exception is NOT covered by the 10/5 oracle below (neither oracle file
    contains the shape), so it carries its own self-test arm; without one, a
    count identity on two files would have been mistaken for proof the screen
    does not over-match.
    """
    spliced, line_of = splice(text)
    code = strip_noncode(spliced)

    def lineno(off: int) -> int:
        # Map a blanked-text offset back to the ORIGINAL line, so a refusal names
        # a line a human can open. splice() removed characters; a raw
        # `text[:off].count()` would drift past every line continuation.
        if off < len(line_of):
            return line_of[off]
        return line_of[-1] if line_of else 1

    hits: list[int] = []
    for m in re.finditer(r"\bco_spawn\s*\(", code):
        i, depth = m.end(), 1
        while i < len(code) and depth:
            if code[i] in "([{":
                depth += 1
            elif code[i] in ")]}":
                depth -= 1
            i += 1
        if depth:
            continue  # unbalanced (macro, truncated file) -- not a claim either way
        rest = _after_first_top_level_comma(code[m.end() : i - 1])
        if rest is None:
            continue  # fewer than two arguments -- not the shape
        name = _names_a_call(rest)
        if name is not None and name not in FORWARDING_WRAPPERS:
            hits.append(lineno(m.start()))
    return hits


def includers_of(header_rel: str, build_dir: str, repo_root: str) -> list[dict]:
    """Compile-database entries whose OWN TEXT `#include`s `header_rel`.

    Direct includers only, matched on the include line rather than the bare
    basename so a mention in a comment does not admit a TU. Transitive inclusion
    is not resolved — a header reached only through another header is not found
    here, and that is the residual the gate still refuses on.
    """
    base = os.path.basename(header_rel)
    pat = re.compile(r'#\s*include\s*[<"][^">]*' + re.escape(base) + r'[">]')
    out: list[dict] = []
    for e in load_db(build_dir, only_with_cospawn=False):
        f = os.path.realpath(os.path.join(e.get("directory", "."), e["file"]))
        try:
            with open(f, encoding="utf-8", errors="replace") as fh:
                if pat.search(fh.read()):
                    out.append(e)
        except OSError:
            continue  # unreadable TUs are already handled by load_db's own path
    return out


def screen_headers(repo_root: str) -> dict[str, list[int]]:
    """Every tracked header carrying the named-closure shape. Empty == none."""
    try:
        listing = subprocess.run(
            ["git", "grep", "-l", "co_spawn", "--", "*.hpp", "*.h"],
            cwd=repo_root, capture_output=True, text=True, check=False)
    except OSError as exc:
        raise RuntimeError(f"header screen could not run git grep: {exc}") from exc
    if listing.returncode not in (0, 1):  # 1 == no match, which is a real answer
        raise RuntimeError(f"header screen: git grep failed: {listing.stderr.strip()}")
    out: dict[str, list[int]] = {}
    for rel in listing.stdout.split("\n"):
        if not rel:
            continue
        try:
            with open(os.path.join(repo_root, rel), encoding="utf-8", errors="replace") as fh:
                hits = screen_named_closure(fh.read())
        except OSError as exc:
            # Unreadable is NOT clean -- same rule the population uses.
            raise RuntimeError(f"header screen could not read {rel}: {exc}") from exc
        if hits:
            out[rel] = hits
    return out


def run_self_test(libclang: str, resource_dir: str) -> int:
    err = _load_libclang(libclang)
    if err:
        print(f"ERROR: {err}")
        return 1
    args = ["-std=c++20", "-x", "c++"]
    if resource_dir:
        args += ["-resource-dir", resource_dir]

    passed = failed = 0

    # ── screen arms (#363) -- no libclang needed; see screen_named_closure ──
    # The two in-tree oracles are files whose site count this tool ALREADY
    # reports, so they pin the screen against something external rather than
    # against itself. The synthetic arms cover the shapes the oracles do not
    # contain.
    #
    # ⚠️ THESE ARMS DO NOT PIN "THE SCREEN AND THE WALKER AGREE ABOUT WHAT A SITE
    # IS", and an earlier version of the failure message claimed they did. The two
    # disagree BY DESIGN and always have: the screen nominates any identifier call
    # in argument two, so it reports `run_liveness_loop()`, `run_accept_loop()`,
    # `asio::bind_executor(...)` — all of which the walker correctly REJECTS,
    # since none is a lambda-typed variable (see SiteWalker._named_closure_arg,
    # which names run_liveness_loop() as its example of what a crude grep
    # over-counts). Neither oracle file contains a divergent shape, so an arm
    # asserting agreement could not have failed for the reason it stated.
    #
    # Since the gate refuses only on UNPARSED files, that divergence costs
    # nothing: the screen is a CANDIDATE GENERATOR and the walker is the
    # authority. What these arms actually pin is narrower and still worth having —
    # that the screen has not stopped seeing a shape it used to see.
    screen_arms: list[tuple[str, str, int]] = [
        ("screen/oracle-fifo-across-cycles", "tests/sync/test_fifo_across_cycles.cpp", 10),
        ("screen/oracle-asan-clean", "tests/sync/test_asan_clean.cpp", 5),
    ]
    repo_root = os.environ.get("FIXPP_REPO_ROOT", os.getcwd())
    for name, rel, expected_n in screen_arms:
        path = os.path.join(repo_root, rel)
        try:
            with open(path, encoding="utf-8", errors="replace") as fh:
                got = len(screen_named_closure(fh.read()))
        except OSError as exc:
            print(f"FAIL  {name}\n      oracle unreadable: {exc}")
            failed += 1
            continue
        if got != expected_n:
            print(f"FAIL  {name}\n      expected {expected_n} screen hits, got {got}. "
                  f"⚠️ Do NOT relax the expectation to match — re-derive why the count "
                  f"moved. These two files are pinned because their shapes are ones the "
                  f"screen and the walker DO agree on, so a change here means the screen "
                  f"stopped seeing a shape it used to see.")
            failed += 1
        else:
            passed += 1

    synthetic: list[tuple[str, str, int]] = [
        # Multi-line argument list -- the shape a line-based grep misses.
        ("screen/multiline-args",
         "co_spawn(\n    ioc,\n    make_waiter(1),\n    asio::use_future);", 1),
        # Closure invoked WITH arguments -- the shape an empty-parens pattern misses.
        ("screen/closure-with-args", "co_spawn(ioc, make_waiter(1), use_future);", 1),
        # Zero-argument closure.
        ("screen/closure-no-args", "co_spawn(ioc, holder(), use_future);", 1),
        # A lambda LITERAL is not a named closure -- nothing outlives the call.
        ("screen/lambda-literal",
         "co_spawn(ioc, [&]() -> asio::awaitable<void> { co_return; }, use_future);", 0),
        # Forwarding wrappers move an awaitable, not a closure. NOT covered by the
        # oracles (neither file contains the shape), which is why this arm exists:
        # without it, 10/5 would have been mistaken for proof of no over-matching.
        ("screen/forwarder-move", "co_spawn(pool.get_executor(), std::move(coro), use_future);", 0),
        ("screen/forwarder-forward", "co_spawn(ex, std::forward<C>(c), use_future);", 0),
        # An executor argument containing its own parens must not break the split.
        ("screen/executor-with-parens",
         "co_spawn(ioc.get_executor(), make_waiter(1), use_future);", 1),
        # ── NON-CODE ARMS. Each of these screened as 1 -- identical to the
        # control -- before strip_noncode() was applied. A hit REFUSES the audit
        # in a gating CI step, so one commented-out example in a tracked header
        # would have failed tier1. These are the arms that would catch the
        # blanking being dropped again.
        ("screen/comment-line", "// co_spawn(ioc, make_waiter(1), use_future);", 0),
        ("screen/comment-block", "/* co_spawn(ioc, holder(), use_future); */", 0),
        ("screen/string-literal", 'const char* s = "co_spawn(ioc, holder(), t)";', 0),
        # splice(): a backslash-newline inside the comment close. Without splicing
        # the stripper believes the comment never ends and blanks the rest of the
        # file -- so the REAL call below it would vanish and the screen would
        # report a clean 0. The control is the point of this arm.
        ("screen/spliced-comment-close",
         "/* co_spawn(ioc, holder(), t); *\\\n/\nco_spawn(ioc, make_waiter(1), use_future);", 1),
        # Blanking must not eat real code that merely sits after a comment.
        ("screen/code-after-comment",
         "// co_spawn(ioc, holder(), t);\nco_spawn(ioc, make_waiter(1), use_future);", 1),
        # ── TEMPLATE ARMS. Every one of these screened as NO HIT before
        # _names_a_call() grew its balanced `<...>` scan -- silently, which in a
        # screen built to be loud is the defect it exists to prevent. Note the
        # single-argument case fails too: `<A>` sits between the identifier and
        # the paren, so it is not only about the comma.
        ("screen/template-one-arg", "co_spawn(ioc, make_thing<A>(), use_future);", 1),
        ("screen/template-two-args", "co_spawn(ioc, make_thing<A,B>(), use_future);", 1),
        ("screen/template-spaced-with-args",
         "co_spawn(ioc, make_thing<A, B>(1), use_future);", 1),
        ("screen/template-nested", "co_spawn(ioc, make_thing<A<B>>(), use_future);", 1),
        # A templated forwarder is still a forwarder.
        ("screen/template-forwarder", "co_spawn(ex, std::forward<C>(c), use_future);", 0),
        # A bare `<` is a comparison, not a template list -- must not be read as
        # an unterminated template and must not become a hit.
        ("screen/comparison-not-template", "co_spawn(ioc, a < b, use_future);", 0),
        # Fewer than two arguments is not the shape.
        ("screen/single-argument", "co_spawn(ioc);", 0),
        # ── PARENTHESISED CALLEE. `(lam)()` is the same site as `lam()` and the
        # AST walker reports it; a matcher anchored on an identifier saw the
        # leading `(` and returned nothing. Found by hostile review — the FOURTH
        # shape this screen has been wrong about, and the third found by a human
        # rather than by these arms.
        ("screen/paren-callee", "co_spawn(ioc, (lam)(), use_future);", 1),
        ("screen/paren-callee-double", "co_spawn(ioc, ((lam))(), use_future);", 1),
        ("screen/paren-callee-template", "co_spawn(ioc, (make<A,B>)(), use_future);", 1),
        ("screen/paren-forwarder", "co_spawn(ex, (std::move)(coro), use_future);", 0),
        # Parens that do NOT wrap a callee must stay silent.
        ("screen/paren-cast", "co_spawn(ioc, (Foo)x, use_future);", 0),
        ("screen/paren-arith", "co_spawn(ioc, (a + b), use_future);", 0),
        # ── THE CALLEE IS A PATH, NOT AN IDENTIFIER. All four were missed while a
        # bare `lam()` was caught. The last one is the SAME `<...>` defect as
        # screen/template-two-args but on the EXECUTOR side: fixing the callee's
        # template handling and not the comma splitter's was fixing a blast radius
        # rather than a cause.
        ("screen/member-call", "co_spawn(ioc, obj.make_waiter(), use_future);", 1),
        ("screen/arrow-call", "co_spawn(ioc, self->make_waiter(), use_future);", 1),
        ("screen/deref-callee", "co_spawn(ioc, (*p)(), use_future);", 1),
        ("screen/template-comma-in-executor",
         "co_spawn(pool<A,B>::get(), make_waiter(1), use_future);", 1),
        # Qualified + templated member path, both halves at once.
        ("screen/qualified-template-member",
         "co_spawn(ioc, reg<A,B>::inst().make<C>(), use_future);", 1),
        # A member path on a forwarder is still a forwarder.
        ("screen/member-forwarder", "co_spawn(ex, std::move(coro), use_future);", 0),
    ]
    for name, body, expected_n in synthetic:
        got = len(screen_named_closure(body))
        if got != expected_n:
            print(f"FAIL  {name}\n      expected {expected_n}, got {got}\n      body: {body!r}")
            failed += 1
        else:
            passed += 1

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

    repo_root = os.environ.get("FIXPP_REPO_ROOT", os.getcwd())

    # ── #363: EXTEND the population to cover screened header hits ─────────────
    # The blind spot is a header carrying the shape that no admitted TU pulls in.
    # Refusing on it was the first design and it is the wrong end: the operator's
    # only remedy was a ~20 min --all-files run. Instead, ADMIT the TUs that
    # include such a header — the issue's own option (3) — so the walker gets to
    # adjudicate the nominations instead of the run dying on them.
    #
    # This found a real instance on its first CI run:
    # tests/session/support/group_dispatch_fixture.hpp carries three sites and
    # ALL SIX of its includers have no `co_spawn` token of their own, so the
    # prefilter admitted none of them. The sites are `sess.open()` /
    # `sess.on_inbound_frame(...)` — member calls, the shape the screen itself
    # was blind to until #363's own review round, which is why the hole read as
    # empty for so long.
    #
    # The screen stays a NOMINATOR: everything it finds is handed to the walker,
    # which decides. The post-sweep gate below then refuses only for a header
    # that has no includer in the compile database at all.
    if not args.all_files:
        try:
            pre_hits = screen_headers(repo_root)
        except RuntimeError as exc:
            print(f"ERROR: {exc}")
            print("The header screen could not run. That is NOT a clean result — it is "
                  "the same fail-toward-clean this check exists to remove.")
            return 1
        have = {os.path.realpath(os.path.join(e.get("directory", "."), e["file"]))
                for e in entries}
        added = 0
        for rel in sorted(pre_hits):
            for e in includers_of(rel, args.build_dir, repo_root):
                f = os.path.realpath(os.path.join(e.get("directory", "."), e["file"]))
                if f not in have:
                    have.add(f)
                    entries.append(e)
                    added += 1
        if added:
            print(f"header screen (#363): admitted {added} extra TU(s) so the walker can "
                  f"adjudicate {sum(len(v) for v in pre_hits.values())} screened header "
                  f"nomination(s) in {len(pre_hits)} header(s).")
        population = sorted(
            os.path.realpath(os.path.join(e.get("directory", "."), e["file"]))
            for e in entries
        )

    best: dict[tuple[str, int, int], dict] = {}
    parsed_files: set[str] = set()
    errors: list[tuple[str, str]] = []
    jobs = [(e, extra, repo_root) for e in entries]
    done = 0
    with cf.ProcessPoolExecutor(
        max_workers=args.jobs, initializer=_worker_init, initargs=(args.libclang, repo_root)
    ) as pool:
        for fname, sites, err, unknown, files_seen in pool.map(_worker, jobs, chunksize=1):
            done += 1
            parsed_files |= files_seen
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

    # ── #363: the population's blind spot, CHECKED — after the sweep, against
    # what the walker actually saw ──────────────────────────────────────────────
    #
    # ⚠️ THIS RAN BEFORE THE SWEEP AND REFUSED ON THE WRONG CONDITION. The blind
    # spot is a conjunction — a header carries the shape AND is unreachable from
    # the population — and the first version tested only the first conjunct. That
    # is not a nitpick: the walker records sites IN-REPO, NOT IN-TU (see the
    # in_tu note in SiteWalker), precisely so header sites are reported. So a
    # header included by an ADMITTED TU is already parsed and already in the
    # report, and refusing on it would have failed CI with the explanation "the
    # prefilter cannot see it" — FALSE for exactly that header — and the only
    # remedy offered was a 20-minute --all-files run to disprove a hole that was
    # never there.
    #
    # ⚠️ AND THE ARM THAT WAS SUPPOSED TO PROVE THIS GATE WORKS DEMONSTRATED THE
    # FALSE REFUSAL INSTEAD. The seeded header was tests/session/_fixtures_/
    # test_double_fsm.hpp, which 21 token-carrying .cpp files include — i.e. the
    # most thoroughly ADMITTED header available. It went red for the wrong
    # reason, and a forced-RED arm that fires for the wrong reason certifies
    # nothing. Proving a guard CAN fire is not proving it fires ON ITS CONDITION.
    #
    # THE ARM THAT DOES DISCRIMINATE seeds BOTH sides in one run and requires the
    # gate to separate them. Against --filter test_fifo_across_cycles:
    #
    #   seed in include/fixpp/core/sync/async_mutex.hpp   (the TU INCLUDES it)
    #   seed in tests/session/_fixtures_/test_double_fsm.hpp (it does NOT)
    #
    #   result: named-closure sites 10 -> 11  (the walker picked up the covered
    #           seed), and the refusal named test_double_fsm.hpp ONLY.
    #   control (both seeds reverted): 10 sites, no refusal.
    #
    # The site-count moving is the part that matters: it proves the covered seed
    # was genuinely parsed, so its ABSENCE from the refusal is a discrimination
    # rather than a miss. Re-derive with that pair; a single-sided seed cannot
    # tell this gate from the one it replaced.
    #
    # So the screen NOMINATES and the walker CONFIRMS: a hit is a finding only
    # where the sweep reported no site in that header. That also demotes the
    # regex from an authority to a candidate generator — it may over-approximate
    # loudly without costing a false CI failure, and it stops being a second
    # definition of "what is a site" competing with the AST walker's.
    #
    # ⚠️ THE CONDITION IS "WAS THIS FILE PARSED", NOT "DID IT YIELD A SITE", AND
    # THE DIFFERENCE IS A CI OUTAGE. It was written the second way first, using
    # the site list as a proxy because the walker did not report what it parsed.
    # That proxy fails on ORDINARY CODE: the screen deliberately nominates any
    # identifier call in argument two, so `co_spawn(ioc, run_pump(), tok)` in a
    # header is a candidate — while the AST walker correctly reports no site,
    # because `run_pump` is a plain coroutine function, not a lambda-typed
    # variable. Under the proxy that header had no site, the gate refused, and a
    # perfectly ordinary spawn in a perfectly ordinary header would have failed
    # the required CI command with no way to satisfy it. SiteWalker now records
    # every in-repo file it walked a cursor in (`files_seen`), so the gate asks
    # the question it means. Over-approximation inside a PARSED file is now free,
    # which is what lets the screen stay deliberately loud.
    #
    # ⚠️ TWO RESIDUALS, AND A GREEN HERE MEANS LESS THAN IT LOOKS. Neither is
    # closable without `--all-files`; both are stated so the green is read for
    # what it is.
    #
    #   1. THE SCREEN CAN MISS THE SHAPE. It is a regex over blanked text and has
    #      already been wrong FOUR times — raw text, forwarding wrappers,
    #      template argument lists, parenthesised callees — every one found by a
    #      reviewer, none by this gate's own arms. A miss in an unreachable header
    #      is silent.
    #
    #   2. `files_seen` IS FILE-GRANULAR; THE HAZARD IS REGION-GRANULAR. `walk`
    #      adds a file on ANY in-repo cursor, so the set proves ">= 1 cursor from
    #      this file was walked" — NOT "the region holding this site was parsed".
    #      The screen ignores the preprocessor (measured: a `co_spawn` inside
    #      `#ifdef FIXPP_WIN ... #endif` is still nominated). So a header whose
    #      site sits in a region compiled out of every admitted TU, but which
    #      contributes any other cursor, lands in `parsed_files` and this gate
    #      passes SILENTLY. Zero instances today; the shape is ordinary enough
    #      that it will not stay that way by luck.
    #
    # Treat a green as "no NOMINATED candidate lies in a file the walker never
    # touched" — never as "no uncovered site exists".
    if not args.all_files:
        try:
            header_hits = screen_headers(repo_root)
        except RuntimeError as exc:
            print(f"ERROR: {exc}")
            print("The header screen could not run. That is NOT a clean result — it is "
                  "the same fail-toward-clean this check exists to remove.")
            return 1
        unseen = {
            rel: lines
            for rel, lines in header_hits.items()
            if os.path.realpath(os.path.join(repo_root, rel)) not in parsed_files
        }
        if unseen:
            print("ERROR: a header carries the named-closure co_spawn shape and the sweep "
                  "NEVER PARSED it (#363).")
            for rel, lines in sorted(unseen.items()):
                print(f"  {rel}: lines {', '.join(str(n) for n in lines)}")
            print("\nThe prefilter admits a TU only if its MAIN FILE text contains the "
                  "literal token, so a header reachable only from TUs without it is never "
                  "parsed — and reconcile_co_spawn_census.py consumes this same "
                  "population, so the cross-check cannot see the hole either. That is the "
                  "#363 blind spot.\n"
                  "This is NOT the screen merely over-matching: a nomination inside a "
                  "PARSED file is ignored, because the test is whether the walker PARSED "
                  "the file, not whether it agreed there was a site there.\n"
                  "Re-run with --all-files to include it in the population.")
            return 1
        print(f"header screen (#363): {len(population)} TUs, {len(parsed_files)} in-repo "
              f"files parsed; {len(header_hits)} screened header hit(s), all inside the "
              f"parsed set.")


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
