#!/usr/bin/env python3
r"""Standing census of PRESET-CONDITIONAL gtest cases in tests/ (issue #353).

THE DEFECT THIS EXISTS FOR
--------------------------
`tests/session/test_seqnum_drain_on_close.cpp` guarded a whole TEST with

    #if !defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_UNDEFINED__)

so the ASan and UBSan legs never compiled it. That test held BOTH of the
use-after-frees fixed in #291, and the sanitizer matrix — this repo's primary
detector for exactly that class of bug — could not have reported either one. The
defect survived from PR #290 to #291 for that reason alone.

⚠️ NOTHING REPORTS THIS ON ITS OWN. A skipped test and a passing test look
IDENTICAL in a leg's summary: both contribute zero failures. The only signal is a
per-preset test COUNT, and nobody compares those. Measured while fixing #291:
`--gtest_filter='SeqnumDrainOnClose.*'` ran **4** tests under linux-clang-debug
and **3** under linux-clang-asan.

WHAT THIS CHECKS — and, deliberately, what it does NOT decide
-------------------------------------------------------------
It enumerates every gtest case definition that sits inside a preprocessor
conditional whose condition depends on a SANITIZER macro or on NDEBUG, and
requires each one to carry a recorded disposition in the allowlist file. That is
all. It does NOT try to decide whether a given carve-out is legitimate.

That restraint is the design. Deciding legitimacy means computing the POLARITY of
the condition — whether the branch holding the test is the sanitizer-ON or the
sanitizer-OFF one — which means evaluating C preprocessor expressions, including
`&&`/`||`/`!`, arbitrary object-like macros, and `__has_feature`. A classifier
that gets polarity wrong in the safe-looking direction would report clean on a
real carve-out, which is the failure this file exists to prevent. So the tool
enumerates and the ALLOWLIST decides, in text a human wrote and a reviewer reads.

Both directions are enumerated, because both change what a leg runs:

  * a test COMPILED OUT under a sanitizer  — the #353 defect class; a hole in the
    detector.
  * a test COMPILED IN ONLY under a sanitizer — e.g. the ASan-only witness in
    `tests/capi/send_recv_test.cpp`. Not a hole; the opposite. It is listed so
    that the set of preset-conditional cases is COMPLETE, and so a new one is a
    deliberate entry rather than a silent divergence.

An `#if` around an ASSERTION rather than around a TEST is NOT reported, and that
is the single most important discrimination here — it is the shape of the entire
`tests/alloc_guard/` cluster, where a sanitizer already replaces global
`operator new` so the TU must not. Those tests still RUN under the sanitizer;
only one EXPECT is elided. Reporting them would bury the one finding that matters
under a dozen that do not.

DENOMINATORS — every one fails closed
-------------------------------------
A scan that walks no file, recognises no test macro, or finds no conditional
directive reports the same "0 findings" a clean tree does. Each is asserted:

  * files scanned > 0
  * gtest case definitions found > 0   (the TEST matcher works at all)
  * conditional regions found > 0      (the directive walk works at all)
  * every allowlist entry MATCHED      (a stale entry is an ERROR, not a pass —
                                        otherwise a deleted test leaves an entry
                                        that would silently absorb a future one
                                        of the same name)

⚠️ THESE WITNESS THE SCAN'S REACH, NOT ITS DETECTOR. Break the "is this test
inside a sanitizer-conditional region" test itself and every denominator above
still passes on a tree that has the defect. `--self-test` is what guards the
detector. Run both, as a pair, or you have a gate that cannot fail for the reason
it exists.

⚠️ THIS IS A LEXER. KNOWN BLIND SPOTS, none of which it pretends to cover:

  1. A test defined by a macro that itself expands to `TEST(...)`. The scanner
     reads unpreprocessed text and sees no case.
  2. A conditional on a macro whose sanitizer provenance arrives from a HEADER
     or from the build system (`-D`), not from a `#define` in the same file.
     Provenance is tracked within a translation unit's own text only.
  3. Non-sanitizer, non-NDEBUG preset conditionals (`_WIN32`, `__linux__`,
     feature macros). Deliberately out of scope: platform divergence is expected
     and enumerating it would drown the signal.
  4. `#if 0` / dead branches. A test inside one is reported if the condition text
     mentions a tracked macro, and not otherwise; no branch is evaluated.

⚠️ THE C++ LEXING HERE IS A SECOND COPY, and staying a copy is a decision with
measurements behind it. `tools/check_co_spawn_lambda.py` ships the same
`splice`/`strip_noncode` pair for the same class of scan. `tools/` has NO
cross-imports at all (all 20 checkers are standalone single files), so importing
or lifting a shared module would be architecturally novel for one gate. What was
done instead: the ONE divergence with reach — the C++14 digit separator, which
the sibling handled and this did not — was ported, with a self-test arm so it
cannot regress.

The other divergences were MEASURED, not waved off, and are inert in this tree:

  * `\r\n` continuations: **0** files under `tests/` contain CR (`git grep -lI $'\r'`).
    And a failed splice degrades safely here — the `#if` line still carries the
    tracked macro, so the region is still recognised.
  * `#include <...>` angle-path stripping: irrelevant to this scanner, which looks
    for directives and TEST macros; neither can appear inside an angle path.

Both sweeps were run end to end under the two strippers and produced identical
counters and identical finding sets. ⚠️ **That is a measurement of TODAY, and the
next hardening will land on one copy only.** If you touch either lexer, diff it
against the other; a third copy already exists in shell
(`tools/check_dictionary_snapshot_exclusivity.sh`'s `strip_comments_preserve_code`).

Buildless — python3 only, no compiler, no compilation database. It therefore runs
in the UNGATED tier1 job and fires during review, unlike a per-preset test-count
comparison, which needs two built binaries and so could only ever run behind the
gate labels — i.e. emit nothing during the review rounds that are the only thing
between a fresh carve-out and merge.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
import tempfile

# ── Macros whose presence in a condition makes that condition preset-dependent ──
# Seeded set. Provenance propagates: a macro #define'd inside a region already
# governed by one of these becomes one of these (that is how the alloc_guard
# cluster's FIXPP_SANITIZER_REPLACES_NEW and capi's FIXPP_CAPI_TEST_ASAN are
# reached without hardcoding either name).
SEED_MACROS = frozenset(
    {
        "__SANITIZE_ADDRESS__",
        "__SANITIZE_THREAD__",
        "__SANITIZE_UNDEFINED__",
        "__SANITIZE_MEMORY__",
        "__SANITIZE_LEAK__",
        "NDEBUG",
    }
)

# `__has_feature(address_sanitizer)` and friends. Clang-only spelling; matched as
# text because the scanner does not evaluate it.
HAS_FEATURE_RE = re.compile(r"__has_feature\s*\(\s*\w*sanitizer\s*\)")

# gtest case definitions. TEST / TEST_F / TEST_P / TYPED_TEST / TYPED_TEST_P /
# TEST_P_INSTANTIATE is not a case. Anchored at line start (possibly indented) so
# a mention inside an expression is not a definition.
TEST_DEF_RE = re.compile(
    r"^[ \t]*(TEST|TEST_F|TEST_P|TYPED_TEST|TYPED_TEST_P)\s*\(\s*"
    r"([A-Za-z_]\w*)\s*,\s*([A-Za-z_]\w*)\s*\)",
    re.MULTILINE,
)

IDENT_RE = re.compile(r"[A-Za-z_]\w*")

SOURCE_SUFFIXES = (".cpp", ".cc", ".cxx", ".hpp", ".h", ".hh")


class ScanError(Exception):
    """A structural problem in the input the scanner refuses to guess past."""


def splice_continuations(text: str) -> tuple[str, list[int]]:
    """Join backslash-newline continuations (C++ phase 2), keeping a line map.

    Returns (spliced_text, line_map) where line_map[i] is the ORIGINAL 1-based
    line number of spliced line i (0-based index).
    """
    out_lines: list[str] = []
    line_map: list[int] = []
    pending = ""
    pending_origin = 0
    for idx, raw in enumerate(text.split("\n"), start=1):
        if raw.endswith("\\"):
            if not pending:
                pending_origin = idx
            pending += raw[:-1]
            continue
        if pending:
            out_lines.append(pending + raw)
            line_map.append(pending_origin)
            pending = ""
        else:
            out_lines.append(raw)
            line_map.append(idx)
    if pending:
        out_lines.append(pending)
        line_map.append(pending_origin)
    return "\n".join(out_lines), line_map


def strip_noncode(text: str) -> str:
    """Blank out comments and string/char literals, PRESERVING line structure.

    Every removed character is replaced by a space (newlines kept), so line and
    column positions are unchanged and the directive walk still lines up with
    line_map. Raw string literals are handled, because `R"(#if ...)"` inside one
    is not a directive.
    """
    out = list(text)
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        # Raw string literal: optional prefix, R"delim( ... )delim"
        if c == "R" and i + 1 < n and text[i + 1] == '"':
            j = text.find("(", i + 2)
            if j != -1:
                delim = text[i + 2 : j]
                close = ')' + delim + '"'
                k = text.find(close, j + 1)
                if k != -1:
                    for p in range(i, k + len(close)):
                        if out[p] != "\n":
                            out[p] = " "
                    i = k + len(close)
                    continue
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                out[i] = " "
                i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            k = text.find("*/", i + 2)
            k = n if k == -1 else k + 2
            for p in range(i, k):
                if out[p] != "\n":
                    out[p] = " "
            i = k
            continue
        if c == "'":
            # C++14 digit separator (`10'000`, `0x1F'FF`) is NOT a char literal.
            # Distinguish by the token to its left: a separator's token starts with
            # a digit; an encoding prefix (`L'a'`, `u8'a'`) starts with a letter.
            #
            # ⚠️ PORTED FROM THE SIBLING LEXER, which is the point of this comment.
            # `tools/check_co_spawn_lambda.py`'s strip_noncode/splice pair does the
            # same job and had this guard when this file did not. Measured on the
            # two implementations before the port:
            #     static constexpr int kN = 10'000;  // x
            #   sibling  -> "static constexpr int kN = 10'000;      "   (correct)
            #   this one -> "static constexpr int kN = 10           "   (ate the line)
            # No reachable miss existed here — the scan breaks at a newline and a
            # digit separator cannot share a line with `#if` or `TEST(` — so this is
            # DRIFT REPAIR, not a bug fix. `tools/` has no cross-imports by
            # convention, so the copies stay separate; when you touch either, DIFF
            # IT AGAINST THE OTHER. A third copy lives in shell at
            # tools/check_dictionary_snapshot_exclusivity.sh's
            # strip_comments_preserve_code.
            k = i - 1
            while k >= 0 and (text[k].isalnum() or text[k] == "'"):
                k -= 1
            tok = text[k + 1 : i]
            if tok and tok[0].isdigit() and i + 1 < n and text[i + 1].isalnum():
                i += 1
                continue

        if c in ('"', "'"):
            quote = c
            j = i + 1
            while j < n and text[j] != quote:
                if text[j] == "\\":
                    j += 1
                if text[j : j + 1] == "\n":
                    break
                j += 1
            j = min(j + 1, n)
            for p in range(i, j):
                if out[p] != "\n":
                    out[p] = " "
            i = j
            continue
        i += 1
    return "".join(out)


def condition_macros(cond: str) -> set[str]:
    """Identifiers a preprocessor condition depends on (plus `defined` stripped)."""
    names = set(IDENT_RE.findall(cond))
    names.discard("defined")
    return names


def scan_text(text: str, path: str) -> tuple[list[dict], int, int]:
    """Scan one translation unit's text.

    Returns (findings, n_test_defs, n_conditional_regions).
    """
    spliced, line_map = splice_continuations(text)
    code = strip_noncode(spliced)
    lines = code.split("\n")

    tracked = set(SEED_MACROS)
    # Stack of open conditionals: dicts with the governing condition text, the
    # branch label, and whether the region is preset-dependent.
    stack: list[dict] = []
    findings: list[dict] = []
    n_regions = 0
    n_tests = 0

    def orig_line(i: int) -> int:
        return line_map[i] if i < len(line_map) else -1

    def is_preset_dependent(cond: str) -> bool:
        if HAS_FEATURE_RE.search(cond):
            return True
        return bool(condition_macros(cond) & tracked)

    for i, line in enumerate(lines):
        stripped = line.strip()

        if stripped.startswith("#"):
            body = stripped[1:].strip()
            directive = body.split(None, 1)[0] if body else ""
            rest = body[len(directive) :].strip()

            if directive in ("if", "ifdef", "ifndef"):
                cond = rest
                dep = is_preset_dependent(cond)
                stack.append(
                    {
                        "cond": cond,
                        "directive": directive,
                        "branch": "if",
                        "dep": dep,
                        "line": orig_line(i),
                    }
                )
                n_regions += 1
                continue

            if directive in ("elif", "elifdef", "elifndef"):
                if not stack:
                    raise ScanError(f"{path}:{orig_line(i)}: #{directive} with no open #if")
                top = stack[-1]
                top["branch"] = "elif"
                # An #elif chain's later branches are still governed by the whole
                # chain's dependence, so OR the new condition in rather than
                # replacing it.
                top["dep"] = top["dep"] or is_preset_dependent(rest)
                top["cond"] = f"{top['cond']}  [#elif {rest}]"
                continue

            if directive == "else":
                if not stack:
                    raise ScanError(f"{path}:{orig_line(i)}: #else with no open #if")
                stack[-1]["branch"] = "else"
                continue

            if directive == "endif":
                if not stack:
                    raise ScanError(f"{path}:{orig_line(i)}: #endif with no open #if")
                stack.pop()
                continue

            if directive == "define":
                name = rest.split("(", 1)[0].split(None, 1)[0] if rest else ""
                if not name:
                    continue
                body = rest[len(name):]
                # Two independent ways a macro becomes preset-dependent:
                #
                #  1. PROVENANCE — defined anywhere inside a preset-dependent
                #     region (how FIXPP_SANITIZER_REPLACES_NEW and
                #     FIXPP_CAPI_TEST_ASAN are reached).
                #  2. ⚠️ ALIASING — its REPLACEMENT TEXT names a tracked macro,
                #     e.g. `#define RELEASE_BUILD NDEBUG` at file scope, outside
                #     any conditional. A later `#if RELEASE_BUILD` is then
                #     release-only and the census used to miss it entirely. This
                #     needs neither a header nor a -D, so it is NOT covered by the
                #     documented provenance blind spot.
                inside_dep = any(fr["dep"] for fr in stack)
                aliases_tracked = bool(condition_macros(body) & tracked) or bool(
                    HAS_FEATURE_RE.search(body)
                )
                if inside_dep or aliases_tracked:
                    tracked.add(name)
                elif name in tracked:
                    # ⚠️ An UNCONDITIONAL redefinition to something preset-FREE
                    # clears provenance. Without this, `tracked` was append-only:
                    # a macro that had been preset-dependent stayed so forever, so
                    # a later `#if MODE` guarding a test present in EVERY preset
                    # demanded an allowlist entry and BLOCKED THE MERGE on a
                    # non-carve-out. A gate whose false positives block is worse
                    # than one that misses.
                    tracked.discard(name)
                continue

            if directive == "undef":
                # Same reasoning as the redefinition case above: after `#undef`
                # the name carries no preset provenance at all.
                name = rest.split(None, 1)[0] if rest else ""
                tracked.discard(name)
                continue
            continue

        m = TEST_DEF_RE.match(line)
        if m:
            n_tests += 1
            governing = [fr for fr in stack if fr["dep"]]
            if governing:
                outer = governing[0]
                findings.append(
                    {
                        "file": path,
                        "line": orig_line(i),
                        "case": f"{m.group(2)}.{m.group(3)}",
                        "cond": outer["cond"],
                        "branch": outer["branch"],
                        "cond_line": outer["line"],
                    }
                )

    if stack:
        raise ScanError(
            f"{path}: {len(stack)} unterminated conditional(s), outermost opened at "
            f"line {stack[0]['line']} — refusing to report a partial scan"
        )
    return findings, n_tests, n_regions


def parse_allowlist(path: str) -> dict[str, str]:
    """`Suite.Case  <TAB or 2+ spaces>  justification` per line; # comments ignored."""
    entries: dict[str, str] = {}
    if not os.path.exists(path):
        return entries
    with open(path, encoding="utf-8") as fh:
        for raw in fh:
            line = raw.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            parts = re.split(r"\t|\s{2,}", line.strip(), maxsplit=1)
            if len(parts) != 2 or not parts[1].strip():
                raise ScanError(
                    f"{path}: malformed entry (need 'Suite.Case<2+ spaces>justification'): {line!r}"
                )
            entries[parts[0].strip()] = parts[1].strip()
    return entries


def iter_sources(root: str):
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in ("build", ".git", "__pycache__")]
        for fn in sorted(filenames):
            if fn.endswith(SOURCE_SUFFIXES):
                yield os.path.join(dirpath, fn)


def run_scan(tests_root: str, allowlist_path: str) -> int:
    findings: list[dict] = []
    n_files = n_tests = n_regions = 0
    for path in iter_sources(tests_root):
        with open(path, encoding="utf-8", errors="replace") as fh:
            text = fh.read()
        n_files += 1
        f, t, r = scan_text(text, path)
        findings.extend(f)
        n_tests += t
        n_regions += r

    print(f"files scanned                 : {n_files}")
    print(f"gtest case definitions found  : {n_tests}")
    print(f"conditional regions found     : {n_regions}")
    print(f"preset-conditional cases      : {len(findings)}")

    failed = False
    # ── Denominators. Each fails closed. ──
    for label, value in (
        ("files scanned", n_files),
        ("gtest case definitions found", n_tests),
        ("conditional regions found", n_regions),
    ):
        if value == 0:
            print(f"ERROR: denominator '{label}' is 0 — the scan reached nothing.")
            failed = True
    if failed:
        return 1

    allow = parse_allowlist(allowlist_path)
    seen: set[str] = set()
    for f in findings:
        seen.add(f["case"])
        if f["case"] not in allow:
            print(
                f"\nUNDISPOSITIONED preset-conditional test case:\n"
                f"  {f['file']}:{f['line']}  {f['case']}\n"
                f"  governed by #if at line {f['cond_line']}: {f['cond']}\n"
                f"  (test body sits in the '{f['branch']}' branch)\n"
                f"  → add a line to {allowlist_path} recording WHY this case is\n"
                f"    preset-conditional, or remove the carve-out. See #353."
            )
            failed = True

    stale = sorted(set(allow) - seen)
    for case in stale:
        print(
            f"\nSTALE allowlist entry: {case!r} is listed in {allowlist_path} but no\n"
            f"  preset-conditional definition of it was found. Remove the entry.\n"
            f"  (A stale entry is an error, not a pass: left in place it would\n"
            f"   silently absorb a future carve-out of the same name.)"
        )
        failed = True

    if not failed:
        print(f"\nOK: all {len(findings)} preset-conditional case(s) carry a disposition.")
    return 1 if failed else 0


# ─────────────────────────────────────────────────────────────────────────────
# Self-test — guards the DETECTOR, which no denominator above can do.
# ─────────────────────────────────────────────────────────────────────────────

SELF_TEST_CASES: list[tuple[str, str, list[str]]] = [
    (
        "test compiled OUT under ASan is flagged (the #353 shape)",
        """
#if !defined(__SANITIZE_ADDRESS__)
TEST(Suite, CarvedOut) { }
#endif
TEST(Suite, Plain) { }
""",
        ["Suite.CarvedOut"],
    ),
    (
        "test compiled IN only under ASan is flagged too (completeness)",
        """
#if defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define MY_ASAN 1
#  endif
#endif
#ifdef MY_ASAN
TEST(Suite, AsanOnly) { }
#endif
""",
        ["Suite.AsanOnly"],
    ),
    (
        "assertion-only carve-out is NOT flagged (the alloc_guard shape)",
        """
#if defined(__SANITIZE_ADDRESS__)
#define REPLACES_NEW 1
#endif
TEST(Suite, StillRuns) {
#if !REPLACES_NEW
  EXPECT_EQ(count, 0);
#endif
  EXPECT_TRUE(other);
}
""",
        [],
    ),
    (
        "NDEBUG carve-out is flagged (the other half of #353's own guard)",
        """
#if !defined(NDEBUG)
TEST(Suite, DebugOnly) { }
#endif
""",
        ["Suite.DebugOnly"],
    ),
    (
        "non-sanitizer preset conditional is NOT flagged (documented blind spot 3)",
        """
#ifdef _WIN32
TEST(Suite, WindowsOnly) { }
#endif
""",
        [],
    ),
    (
        "a directive inside a COMMENT is not a directive",
        """
// #if !defined(__SANITIZE_ADDRESS__)
TEST(Suite, NotGuarded) { }
// #endif
""",
        [],
    ),
    (
        "a directive inside a RAW STRING is not a directive",
        '''
const char* s = R"(
#if !defined(__SANITIZE_ADDRESS__)
)";
TEST(Suite, NotGuardedEither) { }
''',
        [],
    ),
    (
        "the #else branch of a sanitizer conditional is flagged",
        """
#if defined(__SANITIZE_ADDRESS__)
TEST(Suite, UnderAsan) { }
#else
TEST(Suite, WithoutAsan) { }
#endif
""",
        ["Suite.UnderAsan", "Suite.WithoutAsan"],
    ),
    (
        "a C++14 digit separator is not a char literal (ported guard)",
        """
#if !defined(__SANITIZE_ADDRESS__)
static constexpr int kN = 10'000;
TEST(Suite, AfterDigitSeparator) { }
#endif
""",
        ["Suite.AfterDigitSeparator"],
    ),
    (
        "backslash-continued condition still parses (C++ phase 2)",
        """
#if !defined(__SANITIZE_ADDRESS__) && \\
    !defined(__SANITIZE_THREAD__)
TEST(Suite, Continued) { }
#endif
""",
        ["Suite.Continued"],
    ),
    (
        "TEST_F / TEST_P / TYPED_TEST are all recognised as cases",
        """
#if !defined(__SANITIZE_ADDRESS__)
TEST_F(Fix, A) { }
TEST_P(Fix, B) { }
TYPED_TEST(Fix, C) { }
#endif
""",
        ["Fix.A", "Fix.B", "Fix.C"],
    ),
    (
        "a file-scope ALIAS of a tracked macro is itself tracked (Codex finding)",
        # `#define RELEASE_BUILD NDEBUG` outside any conditional: the later
        # `#if RELEASE_BUILD` is release-only and used to be missed entirely.
        # Needs neither a header nor a -D, so the documented provenance blind
        # spot did not cover it.
        """
#define RELEASE_BUILD NDEBUG
#if RELEASE_BUILD
TEST(Suite, ReleaseOnly) { }
#endif
""",
        ["Suite.ReleaseOnly"],
    ),
    (
        "#undef clears provenance — no false positive on a non-carve-out",
        """
#if defined(NDEBUG)
#define MODE 1
#else
#define MODE 0
#endif
#undef MODE
#define MODE 1
#if MODE
TEST(Suite, AlwaysPresent) { }
#endif
""",
        [],
    ),
    (
        "an unconditional redefinition to a preset-FREE body also clears it",
        # Without this the tracked set was append-only and CI demanded an
        # allowlist entry for a test present in every preset — a false positive
        # that BLOCKS merge, which is worse than one that misses.
        """
#if defined(__SANITIZE_ADDRESS__)
#define MODE 1
#endif
#define MODE 1
#if MODE
TEST(Suite, PresentEverywhere) { }
#endif
""",
        [],
    ),
    (
        "#elif: dependence is OR-ed in FORWARD, so an earlier non-dependent arm stays clean",
        # Pins the exact semantics of the `#elif` OR-in, which had no arm at all.
        # FirstArm is compiled iff SOMETHING_UNRELATED — a sanitizer flipping does
        # NOT change whether it exists, so it is correctly not reported. SecondArm
        # is compiled iff (not SOMETHING_UNRELATED and not ASan), which a sanitizer
        # DOES change. ⚠️ The first version of this arm expected BOTH and was wrong;
        # the self-test caught the author, not the tool. Keep it asymmetric.
        """
#if defined(SOMETHING_UNRELATED)
TEST(Suite, FirstArm) { }
#elif !defined(__SANITIZE_ADDRESS__)
TEST(Suite, SecondArm) { }
#endif
""",
        ["Suite.SecondArm"],
    ),
    (
        "#elif: an arm AFTER a dependent one stays dependent (the OR is sticky)",
        """
#if !defined(__SANITIZE_ADDRESS__)
TEST(Suite, Guarded) { }
#elif defined(SOMETHING_UNRELATED)
TEST(Suite, StillDependent) { }
#endif
""",
        ["Suite.Guarded", "Suite.StillDependent"],
    ),
    (
        "nested conditional inherits the outer region's dependence",
        """
#if !defined(__SANITIZE_ADDRESS__)
#ifdef SOMETHING_ELSE
TEST(Suite, Nested) { }
#endif
#endif
""",
        ["Suite.Nested"],
    ),
]


def run_self_test() -> int:
    passed = 0
    failed = 0
    for name, src, expected in SELF_TEST_CASES:
        try:
            findings, _, _ = scan_text(src, "<self-test>")
            got = sorted(f["case"] for f in findings)
        except ScanError as exc:  # a fixture that should parse must parse
            print(f"FAIL  {name}\n      raised ScanError: {exc}")
            failed += 1
            continue
        if got != sorted(expected):
            print(f"FAIL  {name}\n      expected {sorted(expected)}\n      got      {got}")
            failed += 1
        else:
            passed += 1

    # Arms asserting that a STRUCTURAL error is loud rather than silently clean.
    error_cases = [
        ("unterminated #if errors", "#if !defined(NDEBUG)\nTEST(S, A) { }\n"),
        ("#endif with no #if errors", "TEST(S, A) { }\n#endif\n"),
        ("#else with no #if errors", "#else\nTEST(S, A) { }\n"),
    ]
    for name, src in error_cases:
        try:
            scan_text(src, "<self-test>")
        except ScanError:
            passed += 1
        else:
            print(f"FAIL  {name}\n      parsed clean; a malformed TU must ERROR, not report 0")
            failed += 1

    # Arm asserting a malformed ALLOWLIST is loud.
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as fh:
        fh.write("SuiteWithNoJustification\n")
        bad_allow = fh.name
    try:
        parse_allowlist(bad_allow)
    except ScanError:
        passed += 1
    else:
        print("FAIL  malformed allowlist entry\n      parsed clean; it must ERROR")
        failed += 1
    finally:
        os.unlink(bad_allow)

    total = passed + failed
    print(f"\nself-test: {passed}/{total} passed")
    return 1 if failed else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    ap.add_argument("--self-test", action="store_true", help="run the detector fixtures and exit")
    ap.add_argument("--tests-root", default="tests", help="directory to scan (default: tests)")
    ap.add_argument(
        "--allowlist",
        default="ci/expected-preset-conditional-tests.txt",
        help="dispositions file",
    )
    args = ap.parse_args()

    if args.self_test:
        return run_self_test()

    try:
        return run_scan(args.tests_root, args.allowlist)
    except ScanError as exc:
        print(f"ERROR: {exc}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
