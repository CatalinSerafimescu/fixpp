#!/usr/bin/env python3
"""tools/test_check_admin_emit_observation.py — positive/negative test for
tools/check_admin_emit_observation.py (the SC-002 static gate).

Each arm writes one fixture .cpp into a temp directory and runs the gate on it,
so the source tree is never edited. A flagged arm must exit 1 AND print the
message of the defect it seeds: the gate also exits 1 when it cannot read its
input, and an arm that checked the exit code alone would pass on that.

  (a) builder, hook, emit, with more comment lines between them than the
      window and few code lines: the window counts code lines, so this passes.
  (b) builder with its emit beyond the window in code lines: flagged as having
      no emit within the window.
  (c) builder, then emit, with no hook between them: flagged as unwired.
  (d) as (c), with the hook only inside comments: flagged as unwired, because
      a hook token inside a comment is not a call.

The listed build helpers (Session::build_inbound_reject_ and
Session::build_inbound_business_reject_) wrap a builder and return the frame;
the arms below define them in the shape session.cpp does, with string and
character literals holding braces in their bodies.
  (e) helper definitions whose bodies hold raw builders, with wired callers:
      passes, because a builder inside a listed helper is not a site.
  (f) a build_inbound_reject_ caller with no fire_to_admin_ before its emit:
      flagged at the caller's line.
  (g) as (f) for a build_inbound_business_reject_ caller with no ->toApp.
  (h) a raw builder on the line after a helper's closing brace: flagged there.
  (i) a raw builder inside Session::build_inbound_reject_v2_, a helper not
      listed: flagged.
  (j) a raw builder inside MockSession::build_inbound_reject_, not a Session
      member: flagged, and its name is read as a helper call.
  (k) a listed helper whose definition emits: refused, and the builder in it
      stays a site.
  (l) a listed helper whose definition does not close: refused, and the
      builder in it stays a site.
  (m) Session::build_inbound_reject_ wrapping build_business_message_reject:
      refused, because its builder needs ->toApp, not the listed
      fire_to_admin_; the builder in it stays a site.
  (n) Session::build_inbound_business_reject_ wrapping build_reject_with_text:
      refused the same way, the other direction.
  (o) a listed helper whose definition calls no raw builder: refused.
  (p) a listed helper whose comment and ordinary string literal name the
      other family's builder, around its own builder: passes.

Line numbers in the expected messages are found in the fixture by a marker,
and each flagged arm names how many sites the gate must report.

The expected messages are spelled here, not read from the gate, so a change to
the gate's wording turns these arms RED instead of following it.
"""

import subprocess
import sys
import tempfile
from pathlib import Path

GATE = Path(__file__).resolve().parent / "check_admin_emit_observation.py"

# More filler lines than the gate's window, whichever way it counts them.
FILLER = 100

OK_TEXT = "[check_admin_emit_observation] OK"
FAIL_TEXT = "[check_admin_emit_observation] FAIL"
NO_EMIT_TEXT = "builder 'build_logout(' has no store_then_emit within"
UNWIRED_TEXT = "'fire_to_admin_(' was NOT found before store_then_emit"


def fixture(body: list[str]) -> str:
    return "\n".join(["void Session::f() {", *body, "}", ""])


def unwired(n: int) -> str:
    return f"[check_admin_emit_observation] FAIL — {n} unwired emit site(s)"


def line_of(source: str, marker: str) -> int:
    hits = [k for k, line in enumerate(source.split("\n"), 1) if marker in line]
    assert len(hits) == 1, f"marker {marker!r} is on {len(hits)} fixture lines"
    return hits[0]


# The two listed helpers, in session.cpp's shape. The literals put braces where an
# unmasked brace count would close helper 1 before its builder and leave helper 2
# open to the end of the file.
HELPER_DEFS = [
    "fixpp::core::expected_t<std::span<std::byte>> Session::build_inbound_reject_(",
    "    std::span<std::byte> site, int reason) noexcept {",
    '    std::string_view const sep = "}";',
    "    auto const build = [&](std::span<std::byte> out) {",
    "        return fixpp::session::build_reject_with_text(out, seq, reason, sep);  // HELPER1",
    "    };",
    "    return build(site);",
    "}",
    "",
    "fixpp::core::expected_t<std::span<std::byte>> Session::build_inbound_business_reject_(",
    "    std::span<std::byte> site) noexcept {",
    "    char const open = '{';",
    "    auto const build = [&](std::span<std::byte> out) {",
    "        return fixpp::session::build_business_message_reject(out, seq, open);",
    "    };",
    "    return build(site);",
    "}  // END-HELPERS",
]

WIRED_CALLERS = [
    "void Session::admin_site() {",
    "    auto r = build_inbound_reject_(buf, 3);",
    "    if (!fire_to_admin_(*r)) return;",
    "    auto e = co_await store_then_emit(seq, *r);",
    "}",
    "void Session::app_site() {",
    "    auto r = build_inbound_business_reject_(buf);",
    "    if (!engine_.application->toApp(mv, sid)) return;",
    "    auto e = co_await store_then_emit(seq, *r);",
    "}",
]


def source(*blocks: list[str]) -> str:
    return "\n".join(line for block in blocks for line in block) + "\n"


SRC_E = source(WIRED_CALLERS, HELPER_DEFS)
SRC_F = source(
    [
        "void Session::admin_site() {",
        "    auto r = build_inbound_reject_(buf, 3);  // CALLER",
        "    int x = 0;",
        "    auto e = co_await store_then_emit(seq, *r);",
        "}",
    ],
    HELPER_DEFS,
)
SRC_G = source(
    [
        "void Session::app_site() {",
        "    auto r = build_inbound_business_reject_(buf);  // CALLER",
        "    int x = 0;",
        "    auto e = co_await store_then_emit(seq, *r);",
        "}",
    ],
    HELPER_DEFS,
)
SRC_H = source(
    WIRED_CALLERS,
    HELPER_DEFS[:-1],
    [
        "}  void Session::g() { auto r = build_reject_with_text(buf);  // AFTER",
        "    int x = 0;",
        "    auto e = co_await store_then_emit(seq, *r);",
        "}",
    ],
)
SRC_I = source(
    WIRED_CALLERS,
    HELPER_DEFS,
    [
        "fixpp::core::expected_t<std::span<std::byte>> Session::build_inbound_reject_v2_(",
        "    std::span<std::byte> site) noexcept {",
        "    return fixpp::session::build_reject_with_text(site, seq);  // LOOKALIKE",
        "}",
    ],
)
SRC_J = source(
    WIRED_CALLERS,
    HELPER_DEFS,
    [
        "fixpp::core::expected_t<std::span<std::byte>> MockSession::build_inbound_reject_(  // DEF",
        "    std::span<std::byte> site) noexcept {",
        "    return fixpp::session::build_reject_with_text(site, seq);  // LOOKALIKE",
        "}",
    ],
)
SRC_K = source(
    [
        "fixpp::core::expected_t<std::span<std::byte>> Session::build_inbound_reject_(  // DEF",
        "    std::span<std::byte> site) noexcept {",
        "    auto r = fixpp::session::build_reject_with_text(site, seq);  // BUILD",
        "    auto e = co_await store_then_emit(seq, *r);  // EMIT",
        "    return r;",
        "}",
    ],
    WIRED_CALLERS,
)
SRC_L = source(
    WIRED_CALLERS,
    [
        "fixpp::core::expected_t<std::span<std::byte>> Session::build_inbound_reject_(  // DEF",
        "    std::span<std::byte> site) noexcept {",
        "    return fixpp::session::build_reject_with_text(site, seq);  // BUILD",
    ],
)

# (m) and (n): one helper's builder swapped for the other family's; (o): no builder.
SRC_M = source(
    WIRED_CALLERS,
    [
        "fixpp::core::expected_t<std::span<std::byte>> Session::build_inbound_reject_(  // DEF",
        "    std::span<std::byte> site) noexcept {",
        "    return fixpp::session::build_business_message_reject(site, seq);  // SWAPPED",
        "}",
    ],
    HELPER_DEFS[9:],
)
SRC_N = source(
    WIRED_CALLERS,
    HELPER_DEFS[:9],
    [
        "fixpp::core::expected_t<std::span<std::byte>> Session::build_inbound_business_reject_("
        "  // DEF",
        "    std::span<std::byte> site) noexcept {",
        "    return fixpp::session::build_reject_with_text(site, seq);  // SWAPPED",
        "}",
    ],
)
SRC_O = source(
    WIRED_CALLERS,
    [
        "fixpp::core::expected_t<std::span<std::byte>> Session::build_inbound_reject_(  // DEF",
        "    std::span<std::byte> site) noexcept {",
        "    return build_something_else(site, seq);",
        "}",
    ],
    HELPER_DEFS[9:],
)
# (p): the other family's builder named only in a comment and in a string literal.
SRC_P = source(
    WIRED_CALLERS,
    [
        "fixpp::core::expected_t<std::span<std::byte>> Session::build_inbound_reject_(",
        "    std::span<std::byte> site) noexcept {",
        "    // not build_business_message_reject(site, seq): a Reject needs fire_to_admin_",
        '    std::string_view const why = "build_business_message_reject(";',
        "    return fixpp::session::build_reject_with_text(site, seq, why);",
        "}",
    ],
    HELPER_DEFS[9:],
)


def comment_filler() -> list[str]:
    half = FILLER // 2
    lines = [f"    // comment line {k}" for k in range(half)]
    lines += ["    /*"] + [f"       block comment line {k}" for k in range(half)] + ["    */"]
    return lines


ARMS = {
    "a_comment_lines_do_not_count": (
        fixture(
            [
                "    auto r = build_logout(buf);",
                *comment_filler(),
                "    if (!fire_to_admin_(*r)) return;",
                "    auto e = co_await store_then_emit(seq, *r);",
            ]
        ),
        0,
        [OK_TEXT],
    ),
    "b_emit_beyond_the_window_in_code_lines": (
        fixture(
            [
                "    auto r = build_logout(buf);",
                *[f"    int x{k} = {k};" for k in range(FILLER)],
                "    if (!fire_to_admin_(*r)) return;",
                "    auto e = co_await store_then_emit(seq, *r);",
            ]
        ),
        1,
        [FAIL_TEXT, NO_EMIT_TEXT],
    ),
    "c_no_hook": (
        fixture(
            [
                "    auto r = build_logout(buf);",
                "    int x = 0;",
                "    auto e = co_await store_then_emit(seq, *r);",
            ]
        ),
        1,
        [FAIL_TEXT, UNWIRED_TEXT],
    ),
    "d_hook_only_in_comments": (
        fixture(
            [
                "    auto r = build_logout(buf);",
                "    // if (!fire_to_admin_(*r)) return;",
                "    /* fire_to_admin_(*r); */",
                "    auto e = co_await store_then_emit(seq, *r);",
            ]
        ),
        1,
        [FAIL_TEXT, UNWIRED_TEXT],
    ),
    "e_helper_bodies_are_not_sites": (SRC_E, 0, [OK_TEXT]),
    "f_helper_caller_without_fire_to_admin": (
        SRC_F,
        1,
        [
            unwired(1),
            f"f_helper_caller_without_fire_to_admin.cpp:{line_of(SRC_F, '// CALLER')}: "
            "admin/app emit site uses 'build_inbound_reject_(' but 'fire_to_admin_(' was "
            "NOT found before store_then_emit",
        ],
    ),
    "g_helper_caller_without_toapp": (
        SRC_G,
        1,
        [
            unwired(1),
            f"g_helper_caller_without_toapp.cpp:{line_of(SRC_G, '// CALLER')}: "
            "admin/app emit site uses 'build_inbound_business_reject_(' but '->toApp(' was "
            "NOT found before store_then_emit",
        ],
    ),
    "h_raw_builder_after_the_helper": (
        SRC_H,
        1,
        [
            unwired(1),
            f"h_raw_builder_after_the_helper.cpp:{line_of(SRC_H, '// AFTER')}: "
            "admin/app emit site uses 'build_reject_with_text(' but 'fire_to_admin_(' was "
            "NOT found before store_then_emit",
        ],
    ),
    "i_unlisted_helper_name": (
        SRC_I,
        1,
        [
            unwired(1),
            f"i_unlisted_helper_name.cpp:{line_of(SRC_I, '// LOOKALIKE')}: "
            "builder 'build_reject_with_text(' has no store_then_emit within",
        ],
    ),
    "j_helper_name_in_another_class": (
        SRC_J,
        1,
        [
            unwired(2),
            f"j_helper_name_in_another_class.cpp:{line_of(SRC_J, '// DEF')}: "
            "builder 'build_inbound_reject_(' has no store_then_emit within",
            f"j_helper_name_in_another_class.cpp:{line_of(SRC_J, '// LOOKALIKE')}: "
            "builder 'build_reject_with_text(' has no store_then_emit within",
        ],
    ),
    "k_listed_helper_that_emits": (
        SRC_K,
        1,
        [
            unwired(2),
            f"k_listed_helper_that_emits.cpp:{line_of(SRC_K, '// BUILD')}: "
            "admin/app emit site uses 'build_reject_with_text(' but 'fire_to_admin_(' was "
            "NOT found before store_then_emit",
            f"k_listed_helper_that_emits.cpp:{line_of(SRC_K, '// DEF')}: helper "
            "'Session::build_inbound_reject_(' definition contains store_then_emit at "
            f":{line_of(SRC_K, '// EMIT')} — a listed helper must return the frame without "
            "emitting",
        ],
    ),
    "l_listed_helper_that_does_not_close": (
        SRC_L,
        1,
        [
            unwired(2),
            f"l_listed_helper_that_does_not_close.cpp:{line_of(SRC_L, '// BUILD')}: "
            "builder 'build_reject_with_text(' has no store_then_emit within",
            f"l_listed_helper_that_does_not_close.cpp:{line_of(SRC_L, '// DEF')}: helper "
            "'Session::build_inbound_reject_(' definition does not close — check the site "
            "manually",
        ],
    ),
    "m_reject_helper_wrapping_the_bmr_builder": (
        SRC_M,
        1,
        [
            unwired(2),
            f"m_reject_helper_wrapping_the_bmr_builder.cpp:{line_of(SRC_M, '// DEF')}: helper "
            "'Session::build_inbound_reject_(' is listed with 'fire_to_admin_(' but the "
            "builders its definition calls need '->toApp(' — a listed helper's hook is the "
            "hook of the builder it wraps",
            f"m_reject_helper_wrapping_the_bmr_builder.cpp:{line_of(SRC_M, '// SWAPPED')}: "
            "builder 'build_business_message_reject(' has no store_then_emit within",
        ],
    ),
    "n_bmr_helper_wrapping_the_reject_builder": (
        SRC_N,
        1,
        [
            unwired(2),
            f"n_bmr_helper_wrapping_the_reject_builder.cpp:{line_of(SRC_N, '// DEF')}: helper "
            "'Session::build_inbound_business_reject_(' is listed with '->toApp(' but the "
            "builders its definition calls need 'fire_to_admin_(' — a listed helper's hook is "
            "the hook of the builder it wraps",
            f"n_bmr_helper_wrapping_the_reject_builder.cpp:{line_of(SRC_N, '// SWAPPED')}: "
            "builder 'build_reject_with_text(' has no store_then_emit within",
        ],
    ),
    "o_listed_helper_without_a_builder": (
        SRC_O,
        1,
        [
            unwired(1),
            f"o_listed_helper_without_a_builder.cpp:{line_of(SRC_O, '// DEF')}: helper "
            "'Session::build_inbound_reject_(' is listed with 'fire_to_admin_(' but the "
            "builders its definition calls need no hook — a listed helper's hook is the hook "
            "of the builder it wraps",
        ],
    ),
    "p_other_family_named_only_in_a_comment_and_a_literal": (SRC_P, 0, [OK_TEXT]),
}


def main() -> int:
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        for name, (source, want_rc, want_texts) in ARMS.items():
            path = Path(tmp) / f"{name}.cpp"
            path.write_text(source, encoding="utf-8")
            run = subprocess.run(
                [sys.executable, str(GATE), str(path)], capture_output=True, text=True
            )
            out = run.stdout + run.stderr
            missing = [t for t in want_texts if t not in out]
            ok = run.returncode == want_rc and not missing
            print(f"[{'PASS' if ok else 'FAIL'}] {name}: rc={run.returncode} (want {want_rc})")
            if not ok:
                failures += 1
                for t in missing:
                    print(f"    missing from the gate's output: {t!r}")
                print("    gate output:")
                for line in out.splitlines():
                    print(f"      {line}")
    print(f"test_check_admin_emit_observation: {len(ARMS) - failures} of {len(ARMS)} arms pass")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
