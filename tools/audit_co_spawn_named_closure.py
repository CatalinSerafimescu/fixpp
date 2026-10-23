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
     compare across files, and source offsets are compared only within one file
     (every event carries the file its offset is relative to), so this lands as
     FLAG or MANUAL — the loud direction, but it means such sites need reading
     rather than dismissing.
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
# ⚠️ `yield_window_then_ready` (#289 batch 19) IS DELIBERATELY ABSENT, and the enumeration
# above is exactly why that had to be a decision rather than an omission. It is a pump
# helper in `tests/support/pump_until_ready.hpp` and every other instrument had to learn
# it -- but it takes NO `io_context&`. It drives by yielding the CALLING COROUTINE back
# to its own executor, so this tool cannot resolve which context it drove, which is the
# whole basis on which an entry here credits a caller. Adding it would make the set say
# something the resolver cannot honour. The exclusion follows the rule two paragraphs
# above verbatim -- "if in doubt about whether something drives, leave it OUT" -- and it
# errs LOUD: a named closure driven only by it reads FLAG, never SAFE. There are no such
# sites today (batch 19's eight all spawn UNNAMED lambdas, which this tool does not
# track); the first one is a finding to read, not a false alarm to silence by adding the
# name here. Pinned by a self-test arm below.
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


_REALPATH: dict[str, str] = {}


def _realpath(fn: str) -> str:
    real = _REALPATH.get(fn)
    if real is None:
        real = os.path.realpath(fn)
        _REALPATH[fn] = real
    return real


def file_of(loc) -> str:
    """Real path of the file a SourceLocation's OFFSET is relative to; "" if none."""
    return _realpath(loc.file.name) if loc.file is not None else ""


def decl_key(decl) -> str:
    """Identity of a DECLARATION: file and offset of its canonical declaration.

    ⚠️ IDENTITY, NOT SPELLING (#586). Both the walked VarDecl and every
    reference to it reach this through `canonical`, so the two spellings of one
    declaration always produce one key.
    """
    c = decl.canonical
    return f"{file_of(c.location)}:{c.location.offset}"


def root_object_of(cursor) -> tuple[str, str]:
    """(spelling, decl_key) of the first VarDecl a DeclRefExpr in this subtree refers to.

    `ioc` for `ioc`, for `ioc.run()` and for `ioc.get_executor()`; `entry` for
    `*entry.session_strand`. Used to decide WHICH context a co_spawn was made on
    and WHICH context a driving call drove — without that correlation the
    classifier silences a real FLAG whenever any unrelated io_context happens to
    be in scope. The KEY is what correlates; the spelling is for messages only.
    Both empty when it cannot be resolved, which the caller treats as MANUAL
    rather than as a match.
    """
    for node in [cursor] + list(_descend(cursor)):
        if kind_of(node) != ci.CursorKind.DECL_REF_EXPR:
            continue
        d = node.referenced
        if d is not None and kind_of(d) in (ci.CursorKind.VAR_DECL, ci.CursorKind.PARM_DECL):
            return (d.spelling or "", decl_key(d))
    return ("", "")


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
        # ⚠️ AN OFFSET MEANS NOTHING WITHOUT ITS FILE (#586). Offsets are per
        # file. Every event carries the file its offset is relative to, taken
        # from the SAME location, and classify() compares offsets only within
        # one file.
        off_file = file_of(cursor.extent.start)

        # (a) VarDecls: closures and io_contexts
        if k == ci.CursorKind.VAR_DECL:
            tspell = cursor.type.spelling
            if is_lambda_type(tspell):
                self.events.append(
                    {
                        "kind": "closure_decl",
                        "name": cursor.spelling,
                        "key": decl_key(cursor),
                        "off": off,
                        "off_file": off_file,
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
                own = decl_key(cursor)
                # ⚠️ A CONDITIONAL INITIALIZER HAS ONE BASE PER BRANCH (#586).
                # `c ? any_io_executor{a…} : any_io_executor{b…}` stands for `a` OR
                # `b`, and root_object_of over the whole initializer returns the
                # first variable it meets, which is the CONDITION's. Each branch
                # is resolved with root_object_of on its own; the condition is
                # never a base. A branch that does not resolve keeps an empty
                # base, which ends that path unresolved, and classify() takes the
                # worst verdict over the paths, so it cannot be driven.
                cond = conditional_initializer(cursor)
                if cond is not None:
                    ops = list(cond.get_children())
                    roots = ([root_object_of(b) for b in ops[1:3]] if len(ops) == 3
                             else [("", "")])
                    self.events.append(
                        {"kind": "exec_alias", "name": cursor.spelling, "key": own,
                         "base": " | ".join(r[0] or "?" for r in roots),
                         "base_keys": [r[1] if r[1] != own else "" for r in roots],
                         "off": off, "off_file": off_file, "scope": self.scope_key(stack)}
                    )
                    return
                base = ("", "")
                for ch in cursor.get_children():
                    base = root_object_of(ch)
                    if base[1] and base[1] != own:
                        break
                if base[1] and base[1] != own:
                    self.events.append(
                        {"kind": "exec_alias", "name": cursor.spelling, "key": own,
                         "base": base[0], "base_keys": [base[1]],
                         "off": off, "off_file": off_file, "scope": self.scope_key(stack)}
                    )
                return
            elif is_context_type(tspell):
                self.events.append(
                    {
                        "kind": "ioc_decl",
                        "name": cursor.spelling,
                        "key": decl_key(cursor),
                        "off": off,
                        "off_file": off_file,
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
            obj, obj_key = root_object_of(cursor)
            self.events.append(
                {
                    "kind": "drive",
                    "obj": obj,
                    "obj_key": obj_key,
                    "name": spelling,
                    "off": off,
                    "off_file": off_file,
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
            callee, callee_key = root_object_of(cursor)
            if callee_key:
                self.events.append(
                    {"kind": "closure_call", "name": callee, "key": callee_key, "off": off,
                     "off_file": off_file, "scope": self.scope_key(stack),
                     "line": cursor.location.line}
                )
            return

        # (d) co_spawn sites with a named-closure invocation argument
        if spelling != "co_spawn":
            return

        # ⚠️ ONE SITE PER CLOSURE, NOT PER co_spawn. A co_spawn can name several
        # closures -- one in the token, one in the executor expression -- and
        # keeping only the first let a long-lived one decide the verdict for a
        # short-lived one in another argument: a false SAFE. Each is recorded
        # under the same (file, line, col), and main() keeps the WORST verdict
        # per key, as it already does for a header seen from several TUs.
        closures = self._named_closure_args(cursor)
        if not closures:
            return
        exec_args = list(call.get_arguments()) if False else list(cursor.get_arguments())
        exec_name, exec_key = root_object_of(exec_args[0]) if exec_args else ("", "")
        for closure in closures:
            self.sites.append(
                {
                    "exec": exec_name,
                    "exec_key": exec_key,
                    "file": os.path.realpath(cursor.location.file.name),
                    "line": cursor.location.line,
                    "col": cursor.location.column,
                    "off": off,
                    "off_file": off_file,
                    "scope": self.scope_key(stack),
                    "closure": closure.spelling,
                    "closure_key": decl_key(closure),
                }
            )

    @classmethod
    def _named_closure_args(cls, call) -> list:
        """Every closure VARIABLE's declaration that an argument invokes as `var()`.

        Only a call through a NAMED closure object counts. An immediately-invoked
        temporary is #291's form and belongs to the lexer, not here; a call to a
        plain awaitable-returning function (`run_liveness_loop()`) is not a
        closure at all and is exactly what the issue warns the crude grep
        over-counts.
        """
        found: dict[str, object] = {}
        for index, arg in enumerate(call.get_arguments()):
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
            # Argument 0 is the executor. It is copied into co_spawn before co_spawn
            # returns, so a closure called while initialising a named executor
            # local cannot be reached from the spawned frame: the named-local
            # follow is not applied there (see _closures_called_in).
            for d in cls._closures_called_in(arg, set(), follow_local=index != 0):
                found.setdefault(decl_key(d), d)
        return list(found.values())

    @classmethod
    def _closures_called_in(cls, expr, followed: set[str], follow_local: bool = True) -> list:
        """The lambda-typed VarDecls invoked in `expr`, in source order.

        Lambda bodies are pruned (see _named_closure_args). A closure call may sit
        anywhere below the argument, not only at its root: `bind_slot(s,
        record_into(out))` invokes `record_into` one call down (#586 shape A).

        ⚠️ A NAMED LOCAL IS FOLLOWED TO ITS INITIALIZER (#586 shape B). `const
        auto token = bind_slot(s, record_into(out)); co_spawn(ex, op(), token);`
        is the same site as shape A with the token moved into a local.
        Only a local that IS the expression's value is followed (value_local);
        a receiver (`sess` in `sess.send(x)`) or a local passed into another
        call is not, because the closure behind it has no bearing on what the
        co_spawn is handed. The follow chains to a fixed point through
        `followed`. The clang-query cross-check follows ONE hop with the same
        rule, so a longer chain shows up there as ONLY LIBCLANG -- loud, never
        silent.

        `follow_local` is False for the executor argument: the follow applies to
        the awaitable and token positions only, in both instruments. A closure
        called directly in the executor expression is still collected.
        """
        out = []
        for node in [expr] + list(_descend(expr, prune_lambda_bodies=True)):
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
                    out.append(d)
                    break
        d = value_local(expr) if follow_local else None
        if d is not None and not is_lambda_type(d.type.spelling):
            key = decl_key(d)
            if key not in followed:
                followed.add(key)
                for init in d.get_children():
                    if kind_of(init) == ci.CursorKind.LAMBDA_EXPR:
                        continue
                    out += cls._closures_called_in(init, followed)
        return out


def _in_std(decl) -> bool:
    """True if `decl` is declared in namespace `std` (inline namespaces included)."""
    p = decl.semantic_parent
    while p is not None and kind_of(p) == ci.CursorKind.NAMESPACE:
        if p.spelling == "std":
            return True
        p = p.semantic_parent
    return False


def value_local(expr):
    """The VarDecl whose value `expr` IS, or None.

    Sees through parentheses, implicit nodes, a one-argument constructor (the
    copy or move into a by-value parameter) and `std::move` / `std::forward`.
    A node whose kind the bindings cannot name is seen through as a wrapper when
    it has one child, by the rule in the version-skew note above. Anything else
    -- a member call, a call taking the local as an argument -- is not the
    local's value, and None is returned.
    """
    node = expr
    while node is not None:
        k = kind_of(node)
        if k == ci.CursorKind.DECL_REF_EXPR:
            d = node.referenced
            return d if d is not None and kind_of(d) == ci.CursorKind.VAR_DECL else None
        if k is None or k in (ci.CursorKind.UNEXPOSED_EXPR, ci.CursorKind.PAREN_EXPR):
            kids = list(node.get_children())
            node = kids[0] if len(kids) == 1 else None
            continue
        if k == ci.CursorKind.CALL_EXPR:
            ref = node.referenced
            args = list(node.get_arguments())
            if ref is None or len(args) != 1:
                return None
            if kind_of(ref) == ci.CursorKind.CONSTRUCTOR or (
                    ref.spelling in ("move", "forward") and _in_std(ref)):
                node = args[0]
                continue
        return None
    return None


_REF_KINDS = frozenset(
    {ci.CursorKind.TYPE_REF, ci.CursorKind.TEMPLATE_REF, ci.CursorKind.NAMESPACE_REF}
)


def _expr_children(node) -> list:
    """Children that are not type, template or namespace references."""
    return [c for c in node.get_children() if kind_of(c) not in _REF_KINDS]


def conditional_initializer(var):
    """The `?:` a VarDecl's initializer IS, or None.

    Sees through parentheses, implicit nodes, casts, a braced or functional
    construction (`any_io_executor{c ? a : b}`) and a one-argument constructor,
    by value_local's rule for nodes the bindings cannot name. Anything else --
    a `?:` nested inside a call's argument, say -- is not the initializer's value.
    """
    kids = _expr_children(var)
    node = kids[0] if len(kids) == 1 else None
    while node is not None:
        k = kind_of(node)
        if k == ci.CursorKind.CONDITIONAL_OPERATOR:
            return node
        if k == ci.CursorKind.CALL_EXPR:
            ref = node.referenced
            args = list(node.get_arguments())
            if ref is None or kind_of(ref) != ci.CursorKind.CONSTRUCTOR or len(args) != 1:
                return None
            node = args[0]
            continue
        if k is None or k in (ci.CursorKind.UNEXPOSED_EXPR, ci.CursorKind.PAREN_EXPR,
                              ci.CursorKind.CXX_FUNCTIONAL_CAST_EXPR,
                              ci.CursorKind.CSTYLE_CAST_EXPR,
                              ci.CursorKind.CXX_STATIC_CAST_EXPR,
                              ci.CursorKind.INIT_LIST_EXPR):
            kids = _expr_children(node)
            node = kids[0] if len(kids) == 1 else None
            continue
        return None
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


def _before(a: dict, b: dict) -> bool:
    """`a` is lexically before `b`: same file, smaller offset. See _record's note."""
    return a["off_file"] == b["off_file"] and a["off"] < b["off"]


def classify(site: dict, events: list[dict]) -> tuple[str, str]:
    closures = [
        e
        for e in events
        if e["kind"] == "closure_decl"
        and e["key"] == site["closure_key"]
        and _before(e, site)
        and encloses(e["scope"], site["scope"])
    ]
    if not closures:
        return ("MANUAL", "closure declaration not found in an enclosing block scope "
                          "(member, parameter, or captured from another TU)")
    decl = max(closures, key=lambda e: e["off"])

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
    # Keyed by DECLARATION (decl_key), so one name declared in two scopes is two
    # entries rather than one that the later declaration overwrites (#586).
    alias = {e["key"]: e["base_keys"] for e in events if e["kind"] == "exec_alias"}
    names = {e["key"]: e["name"] for e in events if "key" in e}

    def resolve(key: str) -> list[list[str]]:
        """Every declaration chain this one may stand for, one per path.

        `strand1` -> [[strand1, ioc]]. Matching on a path's SET keeps the
        executor correlation honest (an unrelated context still cannot certify a
        site) while accepting the legitimate indirections this tree actually
        uses. A conditional alias forks the path, one per branch; an empty base
        (a branch that did not resolve) or a cycle ends its path where it is.
        """
        paths: list[list[str]] = []

        def walk(chain: list[str]) -> None:
            bases = alias.get(chain[-1], [])
            if not bases:
                paths.append(chain)
                return
            for b in bases:
                if not b or b in chain:
                    paths.append(chain)
                else:
                    walk(chain + [b])

        walk([key])
        return paths

    paths = resolve(site["exec_key"]) if site.get("exec_key") else []
    if not paths:
        return ("MANUAL", "could not resolve which io_context/executor this site spawns on, "
                          "so no driving call or context lifetime can be matched to it")
    # ⚠️ EVERY PATH MUST BE SAFE, so the WORST verdict over the paths stands. A
    # conditional executor is SAFE-DRIVEN only when each branch is driven; taking
    # the best, or the first branch, certifies a branch nothing drives.
    results = [(p, _classify_on(site, events, decl, set(p))) for p in paths]
    worst = min(results, key=lambda r: VERDICT_RANK[r[1][0]])[1]
    if len(results) == 1:
        return worst
    return (worst[0], "; ".join(
        f"executor branch `{' -> '.join(names.get(k, '?') for k in p)}`: {v} ({why})"
        for p, (v, why) in results))


def _classify_on(site: dict, events: list[dict], decl: dict,
                 exec_keys: set[str]) -> tuple[str, str]:
    """classify() for ONE executor path: `exec_keys` is that path's declarations."""
    cscope = decl["scope"]
    site_exec = site.get("exec", "")

    # SAFE-OUTLIVES: THIS site's io_context declared in the SAME scope AFTER the
    # closure, or in a scope the closure's scope encloses, dies first.
    for e in events:
        if e["kind"] != "ioc_decl" or e["key"] not in exec_keys:
            continue
        if e["scope"] == cscope and _before(decl, e):
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
            if (e["kind"] == "drive" and e.get("obj_key", "") in exec_keys
                    and e["off_file"] == d["off_file"]
                    and body_lo <= e["off"] <= body_hi):
                driving_closures.add(d["key"])
                break

    # SAFE-DRIVEN: a driving call ON THIS SITE'S CONTEXT, after the co_spawn, while
    # the closure is in scope — either directly, or through a local helper closure
    # whose body drives it.
    for e in events:
        if not _before(site, e):
            continue
        if e["kind"] == "closure_call" and e["key"] in driving_closures:
            if encloses(cscope, e["scope"]):
                return ("SAFE-DRIVEN", f"`{e['name']}()` at line {e['line']} runs after the "
                                       f"co_spawn and drives `{site_exec}` in its body")
            continue
        if e["kind"] != "drive":
            continue
        if e.get("obj_key", "") not in exec_keys:
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
            # This prefilter tests the main file's own text. screen_headers()
            # separately nominates every header containing a real-code `co_spawn(`
            # call, and main() admits that header's includers for walker adjudication.
            # The sweep refuses if a nominated header is never parsed.
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
        # The coroutine-side driver (#289 batch 19). It takes NO io_context -- it drives by
        # yielding the calling coroutine back to its own executor -- so it also straddles
        # the "driving call must name the context" assumption the other arms share.
        # ⚠️ THE EXPECTED VERDICT IS FLAG, AND THAT IS THE POINT. This arm pins the
        # DELIBERATE EXCLUSION documented at DRIVING_FREE_FUNCTIONS: the helper drives by
        # yielding the calling coroutine, names no io_context, and so cannot be resolved
        # to one. Adding it to the set flips this arm -- which is the intended trip-wire,
        # not a failure to fix by editing the expectation.
        "yield_window_then_ready is NOT credited as a driver (no io_context to resolve)",
        """
namespace fixpp { namespace test_support {
bool yield_window_then_ready(int&, int, const char*);
} }
void f() {
  io_context ioc;
  int fut = 0;
  auto lam = [&]() { return 0; };
  co_spawn(ioc, lam(), detached);
  if (!fixpp::test_support::yield_window_then_ready(fut, 8, "S::C")) { return; }
}
""",
        [("lam", "FLAG")],
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
        # The arm above cannot fail for its stated reason on its own: `p` is not
        # the site's executor, so the executor match rejects `p.get()` whether or
        # not the owner is checked. Here the non-driving `run()` is reached
        # THROUGH the site's own executor root, so only the owner check rejects
        # it. Mutant: accept any owner for a DRIVING_METHODS name.
        "a run() whose OWNER is not a context does not drive, even on the site's own root",
        """
struct runner_t { void run(); };
struct rig_t { io_context ioc; runner_t r; };
void f() {
  rig_t rig;
  { auto lam = [&]() { return 0; }; co_spawn(rig.ioc, lam(), detached); rig.r.run(); }
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
    (
        # #586 shape A. The closure call sits one call BELOW the co_spawn's own
        # argument. Mutant: test only the argument node itself, not its subtree.
        "a closure call NESTED inside a co_spawn argument is a site (#586 shape A)",
        """
struct slot_t { };
struct handler_t { };
template <class S, class H> int bind_slot(S, H) { return 0; }
int op();
void f() {
  io_context ioc;
  int out = 0;
  auto record_into = [&](int) { return handler_t{}; };
  co_spawn(ioc, op(), bind_slot(slot_t{}, record_into(out)));
  ioc.run();
}
""",
        [("record_into", "SAFE-DRIVEN")],
    ),
    (
        # #586 shape B. The token is built in a NAMED LOCAL before the call; the
        # site must be followed to that local's initializer, never left out.
        "a token built in a NAMED LOCAL is followed to its initializer (#586 shape B)",
        """
struct slot_t { };
struct handler_t { };
template <class S, class H> int bind_slot(S, H) { return 0; }
int op();
void f() {
  io_context ioc;
  int out = 0;
  auto record_into = [&](int) { return handler_t{}; };
  const auto token = bind_slot(slot_t{}, record_into(out));
  co_spawn(ioc, op(), token);
  ioc.run();
}
""",
        [("record_into", "SAFE-DRIVEN")],
    ),
    (
        # The same follow when the local sits BELOW the argument rather than
        # being it. Mutant: follow only the argument node itself.
        "a named local BELOW the argument is followed too (#586 shape B)",
        """
namespace std { template <class T> T&& move(T& t) { return static_cast<T&&>(t); } }
void f() {
  io_context ioc;
  int out = 0;
  auto make = [&](int) { return 0; };
  auto aw = make(out);
  co_spawn(ioc, std::move(aw), detached);
  ioc.run();
}
""",
        [("make", "SAFE-DRIVEN")],
    ),
    (
        # One site can name more than one closure. Every one is classified and
        # the worst verdict stands, so a long-lived closure found in one argument
        # cannot hide a short-lived one in another. Mutant: keep only the first
        # closure found.
        "a SECOND closure in the same co_spawn is classified too; the worst verdict stands",
        """
struct slot_t { };
template <class S, class H> int bind_slot(S, H) { return 0; }
void f() {
  io_context ioc;
  auto helper = [&]() { return 0; };
  {
    auto lam = [&]() { return 0; };
    co_spawn(ioc, lam(), bind_slot(slot_t{}, helper()));
  }
  ioc.run();
}
""",
        [("helper", "SAFE-DRIVEN"), ("lam", "FLAG")],
    ),
    (
        # The same, with the long-lived closure reached FIRST, in the executor
        # expression -- the order in which keeping only the first closure hides
        # the FLAG. Mutant: as above.
        "a closure in the EXECUTOR expression does not hide the spawned closure's verdict",
        """
struct strand_t { };
strand_t& pick(io_context&, int);
void f() {
  io_context ioc;
  auto helper = [&]() { return 0; };
  { auto lam = [&]() { return 0; }; co_spawn(pick(ioc, helper()), lam(), detached); }
  ioc.run();
}
""",
        [("helper", "SAFE-DRIVEN"), ("lam", "FLAG")],
    ),
    (
        # The named-local follow does not apply to the EXECUTOR argument. The
        # executor is copied into co_spawn before it returns, so a closure called
        # while initialising a named executor local cannot be reached from the
        # spawned frame. Mutant: follow the named local at argument 0 as well.
        "a named EXECUTOR local is not followed into its initializer (#586 shape B)",
        """
struct exec_t { };
struct sess_t { exec_t executor(); };
int op();
void f() {
  io_context ioc;
  auto session_of = [&](int) -> sess_t& { static sess_t s; return s; };
  const exec_t exec = session_of(1).executor();
  auto start = [&]() { co_spawn(exec, op(), detached); };
  start();
}
""",
        [],
    ),
    (
        # A CONDITIONAL executor alias resolves each branch, and the site is
        # SAFE-DRIVEN only when every branch is driven. Mutant: take the
        # condition operand as the base.
        "a conditional executor whose branches are BOTH driven is SAFE-DRIVEN",
        """
struct exec_t { };
exec_t make_exec(io_context&);
void f(bool on_a) {
  io_context ioc_a;
  io_context ioc_b;
  auto lam = [&]() { return 0; };
  const exec_t exec = on_a ? exec_t{make_exec(ioc_a)} : exec_t{make_exec(ioc_b)};
  co_spawn(exec, lam(), detached);
  ioc_a.run();
  ioc_b.run();
}
""",
        [("lam", "SAFE-DRIVEN")],
    ),
    (
        # Only the FIRST branch is driven. Mutants: resolve the first branch
        # only; accept any driven branch; take the condition operand as the base
        # (the condition reads the driven context).
        "a conditional executor with one branch NOT driven stays FLAG",
        """
struct exec_t { };
exec_t make_exec(io_context&);
bool idle(io_context&);
void f() {
  io_context ioc_a;
  io_context ioc_b;
  auto lam = [&]() { return 0; };
  const exec_t exec = idle(ioc_a) ? exec_t{make_exec(ioc_a)} : exec_t{make_exec(ioc_b)};
  co_spawn(exec, lam(), detached);
  ioc_a.run();
}
""",
        [("lam", "FLAG")],
    ),
    (
        # The same, with the `?:` inside a braced construction. Mutant: recognise
        # a conditional only at the top of the initializer.
        "a conditional executor INSIDE a construction is resolved per branch too",
        """
struct exec_t { };
exec_t make_exec(io_context&);
bool idle(io_context&);
void f() {
  io_context ioc_a;
  io_context ioc_b;
  auto lam = [&]() { return 0; };
  const exec_t exec = exec_t{idle(ioc_a) ? make_exec(ioc_a) : make_exec(ioc_b)};
  co_spawn(exec, lam(), detached);
  ioc_a.run();
}
""",
        [("lam", "FLAG")],
    ),
    (
        # The condition operand is never a base: here it reads the only driven
        # context, and neither branch is driven. Mutant: take the condition
        # operand as the base.
        "the condition of a conditional executor is not taken as its base",
        """
struct exec_t { };
exec_t make_exec(io_context&);
bool idle(io_context&);
void f() {
  io_context ioc_a;
  io_context ioc_b;
  io_context ioc_c;
  auto lam = [&]() { return 0; };
  const exec_t exec = idle(ioc_c) ? exec_t{make_exec(ioc_a)} : exec_t{make_exec(ioc_b)};
  co_spawn(exec, lam(), detached);
  ioc_c.run();
}
""",
        [("lam", "FLAG")],
    ),
    (
        # The follow runs to a fixed point. The clang-query matcher follows one
        # hop only, so a chain like this one lands in the cross-check's ONLY
        # LIBCLANG bucket -- the loud direction. Mutant: follow one hop only.
        "a CHAIN of named locals is followed to the closure call (#586 shape B)",
        """
struct slot_t { };
struct handler_t { };
template <class S, class H> int bind_slot(S, H) { return 0; }
int op();
void f() {
  io_context ioc;
  int out = 0;
  auto record_into = [&](int) { return handler_t{}; };
  const auto first = bind_slot(slot_t{}, record_into(out));
  const auto token = first;
  co_spawn(ioc, op(), token);
  ioc.run();
}
""",
        [("record_into", "SAFE-DRIVEN")],
    ),
    (
        # Only a local that IS the argument's value is followed. A receiver is
        # not: the closure that built `sess` has no bearing on what co_spawn is
        # handed. Mutant: follow every local referenced in the argument.
        "a RECEIVER local is not followed into its initializer (#586 shape B)",
        """
struct sess_t { int send(int); };
void f() {
  io_context ioc;
  auto make_sess = [&]() { return sess_t{}; };
  auto sess = make_sess();
  co_spawn(ioc, sess.send(1), detached);
  ioc.run();
}
""",
        [],
    ),
    (
        # A closure called only inside a lambda in the followed initializer is
        # not the argument. Mutant: descend without pruning lambda bodies.
        "a closure called only inside a lambda in the local's initializer is not a site (#586 shape B)",
        """
struct slot_t { };
struct handler_t { };
template <class S, class H> int bind_slot(S, H) { return 0; }
int op();
void f() {
  io_context ioc;
  int out = 0;
  auto record_into = [&](int) { return handler_t{}; };
  const auto wrapped = bind_slot(slot_t{}, [&]() { return record_into(out); });
  co_spawn(ioc, op(), wrapped);
  ioc.run();
}
""",
        [],
    ),
    (
        # #586 D, one direction. The alias table resolved a NAME over the whole
        # TU, last declaration wins, so an unrelated `h` in a later function
        # re-pointed this site's `h` at another context. Mutant: key the alias
        # table by spelling.
        "an unrelated later local with the SAME NAME does not re-point an alias (SAFE stays SAFE)",
        """
struct strand_t { };
strand_t make_strand(io_context&);
void f() {
  io_context ioc;
  auto h = make_strand(ioc);
  auto lam = [&]() { return 0; };
  co_spawn(h, lam(), detached);
  ioc.run();
}
void g() {
  io_context other;
  auto h = make_strand(other);
  (void)h;
}
""",
        [("lam", "SAFE-DRIVEN")],
    ),
    (
        # #586 D, the other direction: the same mechanism must not certify a
        # site whose context is never driven.
        "an unrelated later local with the SAME NAME does not re-point an alias (FLAG stays FLAG)",
        """
struct strand_t { };
strand_t make_strand(io_context&);
void f() {
  io_context a;
  io_context b;
  auto h = make_strand(a);
  auto lam = [&]() { return 0; };
  co_spawn(h, lam(), detached);
  b.run();
}
void g() {
  io_context b;
  auto h = make_strand(b);
  (void)h;
}
""",
        [("lam", "FLAG")],
    ),
    (
        # #586 E. A drive in a HEADER whose offset falls inside a main-file
        # closure's extent was credited to that closure. The header drives the
        # SAME context the site spawns on, so declaration identity alone cannot
        # tell the two apart; only the file can. The padding comments place the
        # header's drive inside `idle`'s extent; the arm checks that placement
        # itself and fails if it does not hold. Mutant: compare offsets without
        # the file.
        "a drive in ANOTHER FILE at an offset inside a closure's extent is not credited to it",
        """
#include "selftest_drive.hpp"
void f() {
  {
    auto lam = [&]() { return 0; };
    auto idle = [&]() {
      /* PAD_IDLE */
    };
    co_spawn(g_ioc, lam(), detached);
    idle();
  }
}
""",
        [("lam", "FLAG")],
        {
            "selftest_drive.hpp": """
inline io_context g_ioc;
/* PAD_HEADER */
inline void drive_global() { g_ioc.run(); }
""",
        },
    ),
]

# The E arm's padding. The header drive must land at an offset that a main-file
# closure's extent covers; these sizes place it there with a wide margin either
# side, and run_self_test() checks the placement rather than trusting the sizes.
_PAD = {"PAD_HEADER": 1500, "PAD_IDLE": 4000}


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
# HEADER SCREEN (#363)
#
# The compile-database prefilter reads main files. This screen nominates every
# tracked header containing a real-code `co_spawn(` call so main() can admit its
# includers for walker adjudication. The post-sweep check refuses only when a
# nominated header was never parsed.


def screen_named_closure(text: str) -> list[int]:
    """Nominate every real-code `co_spawn(` call by its original 1-based line.

    The walker adjudicates every nomination. A call not spelled `co_spawn(` in
    the header's own text, such as a macro or alias defined elsewhere, is outside
    this screen's condition.
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

    return [lineno(m.start()) for m in re.finditer(r"\bco_spawn\s*\(", code)]


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
    """Every tracked `*.hpp`/`*.h` holding a real-code `co_spawn(` call, by line. Empty == none."""
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

    synthetic: list[tuple[str, str, int]] = [
        # Multi-line argument list -- the shape a line-based grep misses.
        ("screen/multiline-args",
         "co_spawn(\n    ioc,\n    make_waiter(1),\n    asio::use_future);", 1),
        # Closure invoked WITH arguments -- the shape an empty-parens pattern misses.
        ("screen/closure-with-args", "co_spawn(ioc, make_waiter(1), use_future);", 1),
        # Zero-argument closure.
        ("screen/closure-no-args", "co_spawn(ioc, holder(), use_future);", 1),
        # Every real-code call is nominated whatever its arguments.
        ("screen/lambda-literal",
         "co_spawn(ioc, [&]() -> asio::awaitable<void> { co_return; }, use_future);", 1),
        ("screen/forwarder-move", "co_spawn(pool.get_executor(), std::move(coro), use_future);", 1),
        ("screen/forwarder-forward", "co_spawn(ex, std::forward<C>(c), use_future);", 1),
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
        ("screen/template-one-arg", "co_spawn(ioc, make_thing<A>(), use_future);", 1),
        ("screen/template-two-args", "co_spawn(ioc, make_thing<A,B>(), use_future);", 1),
        ("screen/template-spaced-with-args",
         "co_spawn(ioc, make_thing<A, B>(1), use_future);", 1),
        ("screen/template-nested", "co_spawn(ioc, make_thing<A<B>>(), use_future);", 1),
        ("screen/template-forwarder", "co_spawn(ex, std::forward<C>(c), use_future);", 1),
        ("screen/comparison-not-template", "co_spawn(ioc, a < b, use_future);", 1),
        ("screen/single-argument", "co_spawn(ioc);", 1),
        ("screen/paren-callee", "co_spawn(ioc, (lam)(), use_future);", 1),
        ("screen/paren-callee-double", "co_spawn(ioc, ((lam))(), use_future);", 1),
        ("screen/paren-callee-template", "co_spawn(ioc, (make<A,B>)(), use_future);", 1),
        ("screen/paren-forwarder", "co_spawn(ex, (std::move)(coro), use_future);", 1),
        ("screen/paren-cast", "co_spawn(ioc, (Foo)x, use_future);", 1),
        ("screen/paren-arith", "co_spawn(ioc, (a + b), use_future);", 1),
        ("screen/member-call", "co_spawn(ioc, obj.make_waiter(), use_future);", 1),
        ("screen/arrow-call", "co_spawn(ioc, self->make_waiter(), use_future);", 1),
        ("screen/deref-callee", "co_spawn(ioc, (*p)(), use_future);", 1),
        ("screen/template-comma-in-executor",
         "co_spawn(pool<A,B>::get(), make_waiter(1), use_future);", 1),
        # Qualified + templated member path, both halves at once.
        ("screen/qualified-template-member",
         "co_spawn(ioc, reg<A,B>::inst().make<C>(), use_future);", 1),
        ("screen/member-forwarder", "co_spawn(ex, std::move(coro), use_future);", 1),
        ("screen/token-call", "co_spawn(ex, aw, record_into(out));", 1),
        ("screen/token-call-moved-awaitable",
         "co_spawn(ex, std::move(aw), record_into(out));", 1),
        ("screen/token-call-bound", "co_spawn(ex, aw, bind_slot(s, record_into(out)));", 1),
        ("screen/named-token",
         "const auto token = bind_slot(s, record_into(out));\nco_spawn(ex, aw, token);", 1),
        ("screen/executor-call", "co_spawn(get_ex(), aw, detached);", 1),
    ]
    for name, body, expected_n in synthetic:
        got = len(screen_named_closure(body))
        if got != expected_n:
            print(f"FAIL  {name}\n      expected {expected_n}, got {got}\n      body: {body!r}")
            failed += 1
        else:
            passed += 1

    # Absolute paths, so an arm's `#include "<header>"` resolves to its unsaved
    # header whatever the working directory is.
    fixture_dir = os.path.realpath(os.getcwd())

    def unpad(text: str) -> str:
        for marker, n in _PAD.items():
            text = text.replace(f"/* {marker} */", "/*" + " " * n + "*/")
        return text

    for case in SELF_TEST_CASES:
        name, body, expected = case[:3]
        headers = case[3] if len(case) > 3 else {}
        src = unpad(SELF_TEST_PREAMBLE + body)
        main_path = os.path.join(fixture_dir, "selftest.cpp")
        unsaved = [(main_path, src)] + [
            (os.path.join(fixture_dir, h), unpad(t)) for h, t in headers.items()]
        if headers:
            # The E arm is only meaningful while the header's drive offset lies
            # inside the main file's `idle` extent. Check it from the text, so a
            # padding change cannot turn the arm into one that passes vacuously.
            htext = unsaved[1][1]
            drive_off = htext.find("g_ioc.run()")
            lo = src.find("auto idle")
            hi = src.find("};", lo)
            if drive_off < 0 or lo < 0 or not lo < drive_off < hi:
                print(f"FAIL  {name}\n      the header drive (offset {drive_off}) is not inside "
                      f"`idle`'s extent ({lo}..{hi}); re-size _PAD")
                failed += 1
                continue
        idx = ci.Index.create()
        tu = idx.parse(main_path, args=args, unsaved_files=unsaved)
        bad = [d for d in tu.diagnostics if d.severity >= ci.Diagnostic.Error]
        if bad:
            print(f"FAIL  {name}\n      fixture did not compile: {bad[0].spelling}")
            failed += 1
            continue
        w = SiteWalker(main_path, os.getcwd())
        # The fixture is an unsaved buffer, not a repo file; accept it explicitly.
        w.repo_root = ""
        w.walk(tu.cursor, [])
        unsaved_text = {os.path.realpath(path): text for path, text in unsaved}
        screen_lines = {
            path: set(screen_named_closure(text)) for path, text in unsaved_text.items()
        }
        missed = [
            (site["file"], site["line"])
            for site in w.sites
            if site["line"] not in screen_lines.get(os.path.realpath(site["file"]), set())
        ]
        if missed:
            path, line = missed[0]
            print(f"FAIL  screen/superset/{name}\n"
                  f"      walker site at {path}:{line} was not nominated by the screen")
            failed += 1
        else:
            passed += 1
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
    # So the screen NOMINATES and the walker CONFIRMS. That also demotes the
    # regex from an authority to a candidate generator — it may over-approximate
    # loudly without costing a false CI failure, and it stops being a second
    # definition of "what is a site" competing with the AST walker's.
    #
    # The condition is "was this file parsed", not "did it yield a site".
    # SiteWalker records every in-repo file it walks in `files_seen`. The screen
    # nominates every real-code call, and a nomination in a parsed file costs
    # nothing because the walker adjudicates it.
    #
    # ⚠️ TWO RESIDUALS, AND A GREEN HERE MEANS LESS THAN IT LOOKS. Neither is
    # closable without `--all-files`; both are stated so the green is read for
    # what it is.
    #
    #   1. The screen cannot see a call not spelled `co_spawn(` in the header's
    #      own text, such as a macro or alias defined in another file.
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
            print("ERROR: a header holds a real-code `co_spawn(` call and the sweep "
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
