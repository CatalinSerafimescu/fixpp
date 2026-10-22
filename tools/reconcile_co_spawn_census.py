#!/usr/bin/env python3
r"""Second instrument for the #354 named-closure census — a SET diff, not a count.

WHY A SECOND INSTRUMENT AT ALL
------------------------------
#354 states the population as "~190 sites", measured with

    callExpr(callee(functionDecl(hasName("co_spawn"))),
             hasAnyArgument(cxxOperatorCallExpr(hasOverloadedOperatorName("()"))))

through clang-query. That matcher, in clang-query's DEFAULT traversal mode,
reports **0 matches on a file containing 3 real sites** (measured on
tests/sync/test_fifo_fairness.cpp): implicit nodes — materialize-temporary,
construct-expr, casts — sit between the co_spawn argument and the operator call,
and only `set traversal IgnoreUnlessSpelledInSource` sees through them.

So the issue's own census figure came from an instrument that CAN report a false
low, and reconciling against it would either falsely reassure or send someone
hunting a phantom gap. It is re-derived here instead, twice, by two instruments
that share no code:

  * tools/audit_co_spawn_named_closure.py — libclang, walks the AST itself, and
    additionally resolves the closure VARIABLE and its scope.
  * this script — clang-query with the corrected traversal, parsing its diagnostic
    output.

⚠️ A COUNT IDENTITY IS NOT AGREEMENT. Two instruments can report the same total
while disagreeing about which sites those are — one over-matching by N and
under-matching by N. This compares SITE SETS keyed on (file, line) and prints
both differences separately. Equal counts with a non-empty symmetric difference is
a FINDING, not a pass.

⚠️ NEITHER SIDE IS THE ORACLE. A disagreement means one of them is wrong and the
site must be read. clang-query's matcher is over-inclusive by design here — it
matches ANY `operator()` call at or below an argument (or in the initializer of a
named local the argument refers to), including one on a non-closure functor —
while the libclang walker requires the callee to resolve to a VarDecl of lambda
type. `--self-test` pins which shapes the matcher sees; see MATCHER. So clang-query >= libclang is the EXPECTED direction, and every extra
must be inspected rather than assumed benign; libclang > clang-query is the
alarming direction and means the matcher is missing a shape.
"""

from __future__ import annotations

import argparse
import concurrent.futures as cf
import shutil
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile

# ⚠️ THE CLOSURE CALL MAY SIT ANYWHERE BELOW AN ARGUMENT, NOT ONLY AT IT (#586).
# The matcher used to be `hasAnyArgument(cxxOperatorCallExpr(...))`, which sees
# a closure call only as a DIRECT argument, while the libclang walker sees one
# nested below it (`bind_cancellation_slot(slot, record_into(out))`) — so a
# correct site failed the cross-check. It now matches the argument or any
# descendant, under the same prune the walker applies: never inside a lambda
# body that is itself below THIS co_spawn.
#
# The prune is anchored to this co_spawn through `equalsBoundNode("cs")`. The
# binding is made by the FIRST `allOf` operand, which is what makes it visible to
# the later ones; `.bind()` on the outer matcher binds only after its inner
# matchers have run. An unanchored `unless(hasAncestor(lambdaExpr()))` would drop
# every co_spawn written inside a lambda that another co_spawn spawns.
#
# ⚠️ A NAMED LOCAL IS FOLLOWED ONE HOP (#586 shape B). When an argument's value
# IS a non-parameter VarDecl -- the argument itself, or the operand of
# `std::move` / `std::forward` -- the local is followed to its initializer, where
# the same closure-call test runs with the prune anchored to that VarDecl.
# Without it, `const auto token = …record_into(out)…; co_spawn(ex, op(), token)`
# was listed by neither instrument. A receiver or a local passed into another
# call is not followed, by the walker's same rule (value_local). The walker
# chains to a fixed point; a chain longer than one hop therefore shows up here as
# ONLY LIBCLANG, never as agreement.
_PRUNE_CS = 'unless(hasAncestor(lambdaExpr(hasAncestor(callExpr(equalsBoundNode("cs"))))))'
_CLOSURE_CALL = f'cxxOperatorCallExpr(hasOverloadedOperatorName("()"), {_PRUNE_CS})'
_INIT_CALL = (
    'cxxOperatorCallExpr(hasOverloadedOperatorName("()"), '
    'unless(hasAncestor(lambdaExpr(hasAncestor(varDecl(equalsBoundNode("tok")))))))'
)
_NAMED_LOCAL = (
    'declRefExpr(to(varDecl(allOf(varDecl().bind("tok"), unless(parmVarDecl()), '
    f'hasInitializer(anyOf({_INIT_CALL}, hasDescendant({_INIT_CALL})))))))'
)
_MOVED_LOCAL = (
    'callExpr(callee(functionDecl(hasAnyName("::std::move", "::std::forward"))), '
    f'argumentCountIs(1), hasArgument(0, {_NAMED_LOCAL}))'
)
MATCHER = (
    'callExpr(allOf(callExpr().bind("cs"), callee(functionDecl(hasName("co_spawn"))), '
    f'hasAnyArgument(anyOf({_CLOSURE_CALL}, hasDescendant({_CLOSURE_CALL}), '
    f'{_NAMED_LOCAL}, {_MOVED_LOCAL}))))'
)
LOC_RE = re.compile(r'^(/[^:]+):(\d+):(\d+): note: "root" binds here', re.MULTILINE)
# The message only, without the file:line:col prefix, so identical diagnostics from
# 200 different TUs collapse to one line with a count instead of scrolling past.
#
# ⚠️ `fatal ` IS OPTIONAL AND THAT MATTERS. This started life as
# `^(?:.*?:\d+:\d+: )?error: (.+)$`, which cannot match `... : fatal error: ...` —
# and a missing resource dir, the most likely way this job breaks, reports exactly
# `fatal error: 'stddef.h' file not found`. That regex REPLACED a naive
# `"error:" in out` substring test, so the "improvement" was strictly worse in the
# only direction that counts: the forced-unseen arm below went from failing (right,
# for a clumsy reason) to reporting the file clean (wrong). The arm is the only
# reason this is not still in the tree.
#
# The leading `\S` keeps it off clang's echoed SOURCE lines, which are indented and
# routinely contain tokens like `__throw_length_error`. The `.*?: ` covers both
# diagnostic shapes: `file:line:col: ` and a bare `clang-query: ` prefix.
ERR_RE = re.compile(r'^(?:\S.*?: )?(?:fatal )?error: (.+)$', re.MULTILINE)


def _one_file(job: tuple) -> tuple:
    cq, build_dir, f, timeout, extra = job
    cmd = [cq, "-p", build_dir]
    cmd += [f"--extra-arg={a}" for a in extra]
    cmd += [
        f,
        "-c", "set traversal IgnoreUnlessSpelledInSource",
        "-c", "set output diag",
        "-c", f"match {MATCHER}",
    ]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return (f, [], "timeout", "")
    out = r.stdout + r.stderr
    # ⚠️ FAIL CLOSED ON *ANY* ERROR, whether or not the file still produced matches.
    # clang-query prints "0 matches." both for a clean file and for one it could not
    # compile, and a compile error here is severity-error rather than fatal — so it
    # does NOT stop the run. This check used to read
    #
    #     if "error:" in out and "0 matches" in out:
    #
    # which accepted any file that errored but still matched something. That is the
    # wrong half of the condition: an error means the AST is TRUNCATED, so the site
    # list for that file is a floor, not a census — the very shape whose absence the
    # set-diff is claiming to prove. It also hid the systemic case: on the first CI
    # run 203 of 242 files landed in this bucket and the other 39 were exactly the
    # files that happened to match something, so a single repo-wide diagnostic read
    # as "203 broken files" and the count of genuinely-clean parses was unknowable.
    # A file that errored is UNSEEN. This is the same trap that bit the libclang
    # walker (which originally bailed only on Fatal), and both instruments now use
    # the same rule.
    errs = ERR_RE.findall(out)
    if errs:
        return (f, [], "compile error(s) — file is UNSEEN, not clean", errs[0].strip()[:180])
    if "matches." not in out and "match." not in out:
        return (f, [], "no match tally in output", "")
    return (f, [(os.path.realpath(m.group(1)), int(m.group(2)))
                for m in LOC_RE.finditer(out)], None, "")


def sanitize_db(build_dir: str, out_dir: str) -> str:
    """Copy compile_commands.json minus the `@…o.modmap` response-file arguments.

    ⚠️ WHY THIS EXISTS, and why the two instruments needed it for OPPOSITE reasons.
    CMake's C++20 module scanner puts an `@<obj>.modmap` response file into every
    compile command. Those are written BY THE BUILD, and the CI job deliberately
    builds only `fixpp_codegen_generate` — so on a configure-only tree not one of
    them exists. clang-query goes through the clang DRIVER, which expands `@file`,
    and reported

        no such file or directory: '@src/session/CMakeFiles/fixpp_session.dir/engine.cpp.o.modmap'

    on all 242 files. libclang never expands `@file` at all, so the walker sailed
    past the same argument and reported a clean parse — which is exactly how one
    instrument read 0 errors while the other read 242, on identical flags.

    That asymmetry is also why this cannot be left to "libclang happens to ignore
    it": the walker's correctness there is an accident of a parser that does not
    implement response files, not a decision anyone made. Both tools now get the
    same cleaned command line.

    Dropping the argument is a semantic no-op HERE and the assert below is what
    keeps that true: this tree contains no C++20 modules, so every modmap CMake
    emits is 0 bytes. If one ever has content, the file genuinely needs it and
    silently dropping it would mis-parse a modular TU — so that fails loudly
    instead of quietly measuring the wrong thing.
    """
    src = os.path.join(build_dir, "compile_commands.json")
    with open(src, encoding="utf-8") as fh:
        db = json.load(fh)

    def strip(args: list[str]) -> list[str]:
        keep = []
        for a in args:
            if a.startswith("@") and a.endswith(".modmap"):
                p = a[1:]
                if not os.path.isabs(p):
                    p = os.path.join(build_dir, p)
                if os.path.exists(p) and os.path.getsize(p) > 0:
                    raise SystemExit(
                        f"ERROR: {p} is {os.path.getsize(p)} bytes, not empty. This tree has "
                        f"started using C++20 modules and dropping the module map would "
                        f"mis-parse that TU. Teach this script to keep it (and build the "
                        f"modmaps in CI) rather than removing this check."
                    )
                continue
            keep.append(a)
        return keep

    for e in db:
        if "arguments" in e:
            e["arguments"] = strip(e["arguments"])
        elif "command" in e:
            e["command"] = shlex.join(strip(shlex.split(e["command"])))

    dst = os.path.join(out_dir, "compile_commands.json")
    with open(dst, "w", encoding="utf-8") as fh:
        json.dump(db, fh)
    return out_dir


def run_clang_query(cq: str, build_dir: str, files: list[str], timeout: int,
                    jobs: int, extra: list[str]) -> tuple[set, list]:
    sites: set[tuple[str, int]] = set()
    failures: list[tuple[str, str, str]] = []
    done = 0
    with cf.ProcessPoolExecutor(max_workers=jobs) as pool:
        for f, found, err, diag in pool.map(
            _one_file, [(cq, build_dir, x, timeout, extra) for x in files], chunksize=1
        ):
            done += 1
            if err:
                failures.append((f, err, diag))
            sites.update(found)
            if done % 20 == 0:
                print(f"  ... {done}/{len(files)} files, {len(sites)} sites", file=sys.stderr)
    return sites, failures


# ─────────────────────────────────────────────────────────────────────────────
# Self-test — guards the MATCHER, which the real-tree run cannot: a matcher that
# misses a shape on both sides of the diff reports agreement. Each cell is one
# synthetic TU run through the production path (sanitize_db + _one_file); the
# expected sites are the lines carrying a `// SITE` marker, so no line number is
# written down anywhere.
# ─────────────────────────────────────────────────────────────────────────────

# A minimal `asio` stand-in. The token, slot and awaitable are class types with
# user-declared special members, so the AST carries the implicit construct /
# materialize nodes real asio code does and `IgnoreUnlessSpelledInSource` has
# something to see through. Templates instantiated with a lambda type are
# DEFINED, because a declaration-only stub does not compile for a type with no
# linkage, and a fixture that does not compile is a failed cell.
SELF_TEST_PREAMBLE = """
namespace std { template <class T> T&& move(T& t) { return static_cast<T&&>(t); } }
namespace asio {
template <class T> struct awaitable { awaitable(); awaitable(const awaitable&); ~awaitable(); };
struct cancellation_slot { cancellation_slot(); };
struct cancellation_signal { cancellation_slot slot(); };
template <class S, class H> struct bound { bound(S, H) {} bound(const bound&) {} ~bound() {} };
template <class S, class H> bound<S, H> bind_cancellation_slot(S s, H h) { return bound<S, H>(s, h); }
struct io_context { void run(); };
template <class E, class A, class T> void co_spawn(E&, A, T) {}
}
struct handler { handler(); handler(const handler&); ~handler(); void operator()(); };
asio::awaitable<void> op();
"""

SELF_TEST_CASES: list[tuple[str, str]] = [
    (
        "closure call as the DIRECT argument is listed (#586 shape C)",
        """
void f(asio::io_context& ex, int out) {
  asio::cancellation_signal cancel;
  auto record_into = [&](int) { return handler(); };
  auto record_with_slot = [&](int o) { return asio::bind_cancellation_slot(cancel.slot(), record_into(o)); };
  asio::co_spawn(ex, op(), record_with_slot(out));  // SITE
}
""",
    ),
    (
        # Mutant: the matcher before #586, which tests the argument node only.
        "closure call NESTED inside an argument is listed (#586 shape A)",
        """
void f(asio::io_context& ex, int out) {
  asio::cancellation_signal cancel;
  auto record_into = [&](int) { return handler(); };
  asio::co_spawn(ex, op(), asio::bind_cancellation_slot(cancel.slot(), record_into(out)));  // SITE
}
""",
    ),
    (
        # Mutant: drop the named-local arms from the matcher.
        "token built in a NAMED LOCAL is followed to its initializer (#586 shape B)",
        """
void f(asio::io_context& ex, int out) {
  asio::cancellation_signal cancel;
  auto record_into = [&](int) { return handler(); };
  const auto token = asio::bind_cancellation_slot(cancel.slot(), record_into(out));
  asio::co_spawn(ex, op(), token);  // SITE
}
""",
    ),
    (
        # Mutant: drop the std::move / std::forward arm.
        "a named local under std::move is followed too (#586 shape B)",
        """
void f(asio::io_context& ex, int out) {
  auto make = [&](int) { return op(); };
  auto aw = make(out);
  asio::co_spawn(ex, std::move(aw), handler());  // SITE
}
""",
    ),
    (
        # Mutant: drop the lambda prune from the closure-call arm.
        "a closure called inside a LAMBDA argument's body is not a site",
        """
void f(asio::io_context& ex, int out) {
  auto record_into = [&](int) { return handler(); };
  asio::co_spawn(ex, [&]() { return record_into(out); }, handler());
}
""",
    ),
    (
        # The prune is anchored to THIS co_spawn: a co_spawn inside a lambda
        # passed to another co_spawn is still a site, and the outer one is not.
        # Mutants: an unanchored prune drops the inner site; no prune lists the
        # outer one.
        "a co_spawn NESTED in a lambda argument is a site; the outer co_spawn is not",
        """
void f(asio::io_context& ex, int out) {
  auto record_into = [&](int) { return handler(); };
  asio::co_spawn(ex, [&]() {
    asio::co_spawn(ex, op(), record_into(out));  // SITE
    return 0;
  }, handler());
}
""",
    ),
    (
        # Only a local that IS the argument's value is followed; a receiver is
        # not. Mutant: follow a local anywhere below the argument.
        "a RECEIVER local is not followed into its initializer (#586 shape B)",
        """
struct sess_t { asio::awaitable<void> send(int); };
void f(asio::io_context& ex) {
  auto make_sess = [&]() { return sess_t(); };
  auto sess = make_sess();
  asio::co_spawn(ex, sess.send(1), handler());
}
""",
    ),
    (
        # Mutant: drop the lambda prune inside the followed initializer.
        "a closure called only inside a lambda in the local's initializer is not a site",
        """
void f(asio::io_context& ex, int out) {
  asio::cancellation_signal cancel;
  auto record_into = [&](int) { return handler(); };
  const auto wrapped = asio::bind_cancellation_slot(cancel.slot(), [&]() { return record_into(out); });
  asio::co_spawn(ex, op(), wrapped);
}
""",
    ),
    (
        # The libclang walker follows VarDecls only, never parameters. Mutant:
        # drop `unless(parmVarDecl())`, which lets a default argument through.
        "a PARAMETER is not followed into its default argument",
        """
struct functor { handler operator()() const; };
inline functor make_handler;
void g(asio::io_context& ex, handler h = make_handler()) {
  asio::co_spawn(ex, op(), h);
}
""",
    ),
    (
        "a co_spawn with no closure call anywhere is not a site",
        """
void f(asio::io_context& ex) {
  asio::co_spawn(ex, op(), handler());
}
""",
    ),
]

# A TU that does not compile must come back UNSEEN, never as a clean file.
SELF_TEST_UNSEEN = """
void f(asio::io_context& ex) {
  asio::co_spawn(ex, op(), no_such_token);
}
"""


def run_self_test(cq: str, extra: list[str], timeout: int) -> int:
    passed = failed = 0
    with tempfile.TemporaryDirectory(prefix="cospawn-selftest-") as tmp:
        tmp = os.path.realpath(tmp)
        cells = [(name, body) for name, body in SELF_TEST_CASES]
        cells.append(("a TU that does not compile is UNSEEN", SELF_TEST_UNSEEN))
        paths, db = [], []
        for i, (_name, body) in enumerate(cells):
            path = os.path.join(tmp, f"cell_{i}.cpp")
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(SELF_TEST_PREAMBLE + body)
            paths.append(path)
            db.append({"directory": tmp, "file": path,
                       "arguments": ["clang++", "-std=c++20", "-c", path]})
        build = os.path.join(tmp, "build")
        os.makedirs(build)
        with open(os.path.join(build, "compile_commands.json"), "w", encoding="utf-8") as fh:
            json.dump(db, fh)
        dbdir = os.path.join(tmp, "db")
        os.makedirs(dbdir)
        sanitize_db(build, dbdir)

        for i, (name, body) in enumerate(cells):
            f, found, err, diag = _one_file((cq, dbdir, paths[i], timeout, extra))
            if i == len(cells) - 1:
                if err is None:
                    print(f"FAIL  {name}\n      a TU with a compile error was reported as seen, "
                          f"with sites {sorted(found)}")
                    failed += 1
                else:
                    passed += 1
                continue
            if err is not None:
                print(f"FAIL  {name}\n      fixture is UNSEEN: {err}: {diag}")
                failed += 1
                continue
            src_lines = (SELF_TEST_PREAMBLE + body).split("\n")
            want = {n for n, text in enumerate(src_lines, 1) if "// SITE" in text}
            got = {line for fn, line in found if fn == paths[i]}
            if got != want or len(got) != len(found):
                print(f"FAIL  {name}\n      expected site lines {sorted(want)}, "
                      f"got {sorted(found)}")
                failed += 1
            else:
                passed += 1

    print(f"\nself-test: {passed}/{passed + failed} passed")
    return 1 if failed else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    ap.add_argument("--build-dir", default="build/linux-clang-debug")
    # ⚠️ RESOLVED FROM PATH, never hardcoded. This defaulted to an absolute path
    # on the author's machine (`/opt/llvm22/bin/clang-query`) and CI died on
    # FileNotFoundError the first time it ran — a local environment leaked into a
    # shipped default. Fails loud below rather than silently skipping the
    # cross-check, which would leave the audit's zero resting on one instrument.
    ap.add_argument(
        "--clang-query",
        default=(shutil.which("clang-query") or shutil.which("clang-query-22") or ""),
        help="clang-query binary (default: first found on PATH)",
    )
    ap.add_argument("--audit-json", default="", help="--json-out from the libclang walker")
    ap.add_argument("--self-test", action="store_true",
                    help="run the matcher fixtures and exit (needs no compilation database)")
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("--jobs", type=int, default=4)
    # ⚠️ THE TWO INSTRUMENTS MUST PARSE WITH THE SAME CONFIGURATION, or the set-diff
    # measures the configuration rather than the detectors. The libclang walker is
    # given `-resource-dir` explicitly because libclang cannot find its own; passing
    # the same one here keeps the comparison about the matcher-vs-walker difference,
    # which is the only difference the cross-check is entitled to attribute a
    # disagreement to.
    ap.add_argument(
        "--extra-arg",
        action="append",
        default=[],
        metavar="FLAG",
        help="forwarded to clang-query as --extra-arg=FLAG (repeatable); pass the same "
             "-resource-dir the libclang walker gets",
    )
    args = ap.parse_args()

    if not args.clang_query or not shutil.which(args.clang_query):
        print(
            f"ERROR: clang-query not found (--clang-query={args.clang_query!r}).\n"
            "  The cross-check cannot run, and skipping it would leave the audit's\n"
            "  result resting on a single instrument. Install clang-tools or pass\n"
            "  --clang-query explicitly."
        )
        return 1

    if args.self_test:
        return run_self_test(args.clang_query, args.extra_arg, args.timeout)
    if not args.audit_json:
        ap.error("--audit-json is required (except with --self-test)")

    with open(args.audit_json, encoding="utf-8") as fh:
        audit = json.load(fh)
    lib_sites = {(os.path.realpath(s["file"]), s["line"]) for s in audit["sites"]}

    # ⚠️ THE POPULATION COMES FROM THE AUDIT RUN, NEVER RE-DERIVED HERE.
    # A set-diff is only sound if both instruments saw the same files. This script
    # used to rebuild the list from compile_commands.json with a hand-copied
    # equivalent of the audit's own filter -- which silently diverged the moment
    # the audit was run with `--filter`, putting every site outside the filter into
    # the "only clang-query" bucket whose entire purpose is that each entry gets
    # hand-inspected. `--limit` here did the mirror-image damage, manufacturing
    # "only libclang (ALARMING)" hits, which is the signal reserved for "the
    # matcher is missing a shape". Both readings look like findings and are
    # artefacts of the population, not the detectors.
    #
    # The independence this script claims is between the two DETECTORS -- a
    # clang-query matcher and a libclang walker, sharing no code. It was never
    # about the population, and sharing the population is what makes the diff mean
    # anything.
    if "files" not in audit:
        print(
            "ERROR: the audit JSON has no `files` key, so the population it scanned is\n"
            "  unknown and a set-diff against it would be meaningless. Re-run\n"
            "  tools/audit_co_spawn_named_closure.py --json-out <file> to produce one."
        )
        return 1
    files = [f for f in audit["files"] if os.path.exists(f)]
    missing = len(audit["files"]) - len(files)
    if missing:
        print(f"⚠️ {missing} file(s) from the audit population no longer exist — the audit "
              f"JSON is stale relative to the tree.")

    with tempfile.TemporaryDirectory(prefix="cospawn-db-") as tmp:
        cq_sites, failures = run_clang_query(
            args.clang_query, sanitize_db(args.build_dir, tmp), files,
            args.timeout, args.jobs, args.extra_arg
        )

    # ⚠️ A SITE IN AN UNSEEN FILE IS NOT A MATCHER GAP, and calling it one sends the
    # reader to the wrong place. `only libclang (ALARMING)` means exactly one thing —
    # "the clang-query matcher is missing a SHAPE" — but a file clang-query could not
    # compile contributes every one of its libclang sites to that bucket for a reason
    # that has nothing to do with the matcher. The first CI run reported 10 ALARMING
    # sites, all of them in tests/sync/test_fifo_across_cycles.cpp, a file that same
    # run listed as unseen; run locally against a file it CAN compile, the matcher
    # finds all 10. Ninety minutes went into "which shape does the matcher miss?"
    # before the answer turned out to be "none — read the other bucket".
    #
    # Unseen files contribute no clang-query sites by construction (_one_file returns
    # an empty list for them), so only the libclang side needs partitioning.
    unseen = {f for f, _why, _diag in failures}
    only_cq = sorted(cq_sites - lib_sites)
    only_lib_all = lib_sites - cq_sites
    only_lib = sorted(s for s in only_lib_all if s[0] not in unseen)
    lib_in_unseen = sorted(s for s in only_lib_all if s[0] in unseen)

    print(f"files queried              : {len(files)}")
    print(f"clang-query sites (line)   : {len(cq_sites)}")
    print(f"libclang walker sites      : {len(lib_sites)}")
    print(f"agreed                     : {len(cq_sites & lib_sites)}")
    print(f"only clang-query (expected): {len(only_cq)}")
    print(f"only libclang  (ALARMING)  : {len(only_lib)}")
    print(f"only libclang, in a file clang-query could not see: {len(lib_in_unseen)}")
    print(f"files clang-query could NOT see: {len(failures)}")

    for f, line in only_cq:
        print(f"  [only clang-query] {os.path.relpath(f)}:{line}")
    for f, line in only_lib:
        print(f"  [ONLY LIBCLANG]    {os.path.relpath(f)}:{line}")

    # Group by diagnostic, not by file: one repo-wide cause printed 203 times reads
    # as 203 problems, and the truncated per-file list that used to be here showed 15
    # paths and not one error message — the tool detected the failure and threw away
    # the only evidence of what it was.
    by_diag: dict[str, list[str]] = {}
    for f, why, diag in failures:
        by_diag.setdefault(diag or why, []).append(f)
    for diag, fs in sorted(by_diag.items(), key=lambda kv: -len(kv[1])):
        print(f"  [unseen by clang-query] {len(fs)} file(s): {diag}")
        for f in sorted(fs)[:5]:
            print(f"      {os.path.relpath(f)}")
        if len(fs) > 5:
            print(f"      ... and {len(fs) - 5} more")

    if not files or not cq_sites:
        print("ERROR: the reconciliation instrument reached nothing.")
        return 1

    # ⚠️ A DISAGREEMENT IS THE FINDING, SO IT MUST FAIL. This used to print the
    # buckets and exit 0 — including the one labelled ALARMING — so the whole
    # cross-check was advisory text no caller could act on. A set-diff whose exit
    # code cannot express "the sets differ" is not a cross-check.
    #
    # `only clang-query` is expected-but-not-benign: that matcher is deliberately
    # over-inclusive (any operator() call in an argument), so each entry needs a
    # human look. It fails for that reason, not because it is wrong.
    bad = 0
    if only_lib:
        print(f"\nERROR: {len(only_lib)} site(s) seen ONLY by the libclang walker — the "
              f"clang-query matcher is missing a shape.")
        bad = 1
    if only_cq:
        print(f"\nERROR: {len(only_cq)} site(s) seen ONLY by clang-query. Each must be "
              f"inspected; the matcher is over-inclusive by design, so these are not "
              f"automatically benign.")
        bad = 1
    if failures:
        print(f"\nERROR: {len(failures)} file(s) clang-query could not see. UNSEEN, not clean.")
        bad = 1
    if missing:
        print(f"\nERROR: {missing} file(s) in the audit population no longer exist.")
        bad = 1
    return bad


if __name__ == "__main__":
    sys.exit(main())
