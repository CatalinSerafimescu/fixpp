#!/usr/bin/env bash
# CI-side: derive the Python-bindings LEG POLICY from a CMake preset.
#
# ⚠️ THE FILENAME IS NARROWER THAN THE JOB, deliberately. #257 added the install
# polarity (`install_python`) here rather than in a sibling script, because the
# mapping it needs is the SAME preset->facts mapping: which legs build a
# `-release` package. A second script would be a second census of the matrix to
# keep in sync, which this file's own closing note rules out ("a hardcoded list
# here would be a second census"). Renaming was rejected as churn across
# tier1.yml and ci/test-tier1-python-policy.sh's 58 pinned arms.
#
# #254 folds the `python-bindings` job into the six `linux` matrix legs. That
# job carried its own `strategy.matrix.include`, which spelled the sanitizer
# FIVE times per leg: the preset name, `FIXPP_ENABLE_<SAN>=ON`, the build-dir
# suffix, `FIXPP_PYTHON_SANITIZER`, and `san_opts`. Two of those collapse for
# free (the preset already carries `FIXPP_ENABLE_<SAN>` and BUILD_SHARED_LIBS,
# CMakePresets.json). The remaining three are derived HERE, once, from
# `matrix.preset`, and consumed by expression — never re-spelled.
#
# WHY A SCRIPT AND NOT AN INLINE `run:` `case`. #248's thesis, in this repo's
# own words at ci/test-ccache-scripts.sh, is "extracting TIER 1's in-workflow
# `run:` script into a tested `ci/` script". Putting a six-arm, quote-nested
# mapping into a `run:` block and then teaching ci/test-tier1-python-policy.sh
# to extract and parse it out of YAML would create a second, larger instance of
# exactly the problem #248 exists to remove. It is also the only form that can
# be tested faithfully: Actions runs `run:` as a bash SCRIPT FILE (`bash -e {0}`),
# so a probe that exercises it via `bash -c` is a false-RED/false-GREEN
# generator. An executable script is testable as itself.
#
# Usage (from the library submodule dir, as the workflow's working directory):
#   ci/derive-python-sanitizer.sh linux-clang-ubsan >> "$GITHUB_OUTPUT"
#
# stdout — exactly three KEY=VALUE lines, GITHUB_OUTPUT-shaped:
#   sanitizer=<none|asan|ubsan|tsan>   → -DFIXPP_PYTHON_SANITIZER, and the `if:`
#                                        discriminant for the two pytest steps
#   rt_base=<''|asan|ubsan_standalone|tsan>
#                                      → the libclang_rt basename to LD_PRELOAD
#   san_opts=<''|ASAN_OPTIONS=…|UBSAN_OPTIONS=…|TSAN_OPTIONS=…>
#                                      → prefixed to the pytest invocation
#   install_python=<ON|OFF>            → -DFIXPP_INSTALL_PYTHON, and the
#                                        discriminant for the two Configure-time
#                                        L-056-4 assertions (#257)
#   py_witness=<absent|package>        → the ctest name those assertions expect
#   py_layout=<sitearch|package>       → -DFIXPP_PY_INSTALL_LAYOUT
#
# ⚠️ py_layout is NOT py_witness. They coincide on the packaging legs and
# DIVERGE everywhere else: the witness is `absent` when nothing is installed,
# but `absent` is not a layout — feeding it to FIXPP_PY_INSTALL_LAYOUT is a
# configure-time FATAL_ERROR (that variable fails closed on an unknown value
# rather than defaulting). Hence two outputs, not one reused.
#
# ⚠️ `install_python` and `py_witness` are ONE fact emitted twice, and they are
# emitted together so they cannot drift. The workflow asserts the witness NAME
# rather than a count, because `-L python` matches exactly one test in every
# configuration — a count alone cannot tell the OFF witness from the ON one.
#
# Exit 0 on a known preset. On anything else: `::error::` on stderr, exit 1.
#
# ── The four load-bearing properties, each named with the false-green it stops ─
#
# 1. AN UNKNOWN PRESET IS FATAL, NEVER A SILENT `none`. A defaulted `none` on a
#    future sanitizer leg builds an UNINSTRUMENTED _fixpp.so and reports green —
#    the #251 class exactly (an uninstrumented artifact reporting green for a
#    whole run). The constitution (Article IX §2) makes the sanitizer legs a
#    REQUIRED signal, so a leg that silently stops being instrumented is a
#    constitutional false-green, not a cosmetic bug.
#
# 2. `san_opts` DERIVES FROM THE SAME DISCRIMINANT IN THE SAME CALL. A sanitizer
#    leg that loses `halt_on_error=1` runs, finds, prints — and exits 0. One
#    discriminant, one place, or the two drift.
#
# 3. NO `contains(matrix.preset, 'san')` SNIFFING in the workflow's `if:`
#    conditions. Substring sniffing is the third spelling under another name,
#    and it also matches `ubsan`/`asan` inside a future preset name.
#
# 4. `FIXPP_ENABLE_<SAN>` AND `BUILD_SHARED_LIBS` ARE DELIBERATELY NOT EMITTED —
#    the preset owns them. Passing them again is how the deleted job ended up
#    reconstructing a preset by hand.
#
# ⚠️ `$GITHUB_WORKSPACE` IN THE TSAN VALUE IS EMITTED UNEXPANDED, ON PURPOSE.
#    The consumer is `env ${{ steps.pysan.outputs.san_opts }} pytest …` inside a
#    `run:` block, so the shell expands it there — byte-for-byte the behaviour of
#    the matrix value this replaces (the deleted job's `san_opts` was a
#    double-quoted YAML scalar, likewise expanded by the consuming shell).
#    Expanding it here would also work today, but it would be a silent change to
#    a proven-green form, and it would make the value differ between the script's
#    own test harness and CI.
#
# The exhaustiveness of the `case` below is NOT asserted here — it is asserted by
# ci/test-tier1-python-policy.sh, which reads the `linux` job's
# `strategy.matrix.preset` list out of tier1.yml and drives this script over
# EXACTLY that set. That is deliberate: a script cannot know the matrix it
# serves, and a hardcoded list here would be a second census to keep in sync.
#
# ⚠️ What that does and does not cover (Gate B round 1, F3): every preset the
# matrix names must have an arm here, and every arm the pin's table names must be
# in the matrix. An EXTRA arm here that the matrix never names is not detected —
# it is unreachable in CI, since the only call site passes `matrix.preset`, so it
# is dead code rather than a false-green. Do not read "exact-set" as covering it.
set -euo pipefail

PRESET="${1:?usage: derive-python-sanitizer.sh <preset>}"

# Default polarity: OFF everywhere. Only the packaging legs opt IN, below.
# Defaulting to OFF is the fail-safe direction -- a new preset that forgets to
# choose ships nothing, rather than silently adding a payload to an artifact.
INSTALL_PYTHON=OFF
PY_WITNESS=absent
PY_LAYOUT=sitearch

case "$PRESET" in
  linux-clang-debug)
    SANITIZER=none
    RT_BASE=''
    SAN_OPTS=''
    ;;
  # ── #257 — the two legs that build `fixpp-package` ───────────────────────
  # These, and only these, SHIP the python payload: their `fixpp-package`
  # output becomes packages-linux-{clang,gcc}-release. Every other leg keeps
  # FIXPP_INSTALL_PYTHON=OFF, so the payload cannot reach an artifact nobody
  # decided to put it in.
  #
  # ⚠️ Do NOT rewrite this as `contains(preset, 'release')` in the workflow.
  # That is the same mapping spelled a second time under another name (the
  # trap property 3 below names for the sanitizer axis), and it would also
  # match a future non-packaging preset with 'release' in its name.
  linux-clang-release|linux-gcc-release)
    SANITIZER=none
    RT_BASE=''
    SAN_OPTS=''
    INSTALL_PYTHON=ON
    PY_WITNESS=package
    PY_LAYOUT=package
    ;;
  linux-clang-asan)
    SANITIZER=asan
    RT_BASE=asan
    SAN_OPTS='ASAN_OPTIONS=detect_leaks=0:halt_on_error=1'
    ;;
  linux-clang-ubsan)
    SANITIZER=ubsan
    # ⚠️ NOT `ubsan`. Clang ships the standalone UBSan runtime as
    # libclang_rt.ubsan_standalone[-x86_64].so; there is no libclang_rt.ubsan.*.
    # This is the one non-identity row in the table and therefore the one a
    # future edit "normalises" away — ci/test-tier1-python-policy.sh asserts it
    # explicitly and mutates it to prove the assertion RED.
    RT_BASE=ubsan_standalone
    SAN_OPTS='UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1'
    ;;
  linux-clang-tsan)
    SANITIZER=tsan
    RT_BASE=tsan
    # The suppressions file filters interpreter-internal reports; CPython is not
    # instrumented. See bindings/python/tests/tsan_suppressions.txt.
    SAN_OPTS='TSAN_OPTIONS=suppressions=$GITHUB_WORKSPACE/bindings/python/tests/tsan_suppressions.txt:halt_on_error=1'
    ;;
  *)
    echo "::error::derive-python-sanitizer.sh: unknown preset '$PRESET'." >&2
    echo "::error::Refusing to default to 'none' — that would build an UNINSTRUMENTED _fixpp.so and report green (#251 class)." >&2
    echo "::error::Add the preset to the case in ci/derive-python-sanitizer.sh and to the table in .specify/ci254-python-fold.md §4.3.2." >&2
    exit 1
    ;;
esac

echo "sanitizer=$SANITIZER"
echo "rt_base=$RT_BASE"
echo "san_opts=$SAN_OPTS"
echo "install_python=$INSTALL_PYTHON"
echo "py_witness=$PY_WITNESS"
echo "py_layout=$PY_LAYOUT"
