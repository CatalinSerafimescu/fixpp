#!/usr/bin/env bash
# Assert that cibuildwheel actually started the PINNED manylinux image (#259).
#
#   ci/assert-wheel-image.sh <cibuildwheel-log> <expected image ref> [<build-outcome>]
#
#   build-outcome — steps.<build>.outcome. When it is anything other than
#                   `success`, a missing image line is REPORTED and not
#                   attributed (exit 0).
#
# ⚠️ WHY THE BUILD OUTCOME IS AN INPUT. Without it, ANY build failure before a
# container starts — disk exhaustion, a Conan resolution error, a compile error
# — leaves no `Starting container image` line, and this script would announce
# "the pin is not taking effect": a confident, specific, WRONG cause stacked on
# top of the real failure. That is the exact class PR #245 exists to remove, and
# which it was itself caught shipping four times. `ci/ccache-stats.sh` already
# carries the same guard for its liveness claim; this mirrors it deliberately.
#
# ── WHY THIS IS AN ASSERTION AND NOT A COMMENT ───────────────────────────────
#
# The wheel lane's ccache tag is derived from the digest pinned in
# bindings/python/pyproject.toml. If that pin is ever ignored — a cibuildwheel
# change, an overriding CIBW_MANYLINUX_X86_64_IMAGE in the environment, a typo
# that silently falls back to the alias — the cache is keyed to a toolchain that
# is NOT the one compiling. Nothing else would notice: the build succeeds, the
# cache reports hits, and every one of them is against the wrong compiler
# identity. A pin that is silently ignored is worse than no pin at all.
#
# ── WHY IT LIVES HERE AND NOT IN A `run:` BLOCK ──────────────────────────────
#
# #248: workflow-embedded shell has no harness, and this repo has now paid
# repeatedly for verification greps that could never go RED. In a script it is
# reachable by ci/test-ccache-scripts.sh, which drives BOTH outcomes — a match
# and a deliberately wrong pin — so "it passed" means something.
#
# ⚠️ `set -eu` WITHOUT pipefail-dependent logic: every check below is explicit.
# This one IS fatal by design, unlike the cache scripts around it. A cache that
# is down must not redden a lane; a cache keyed to the wrong compiler must.
set -eu

LOG="${1:?usage: assert-wheel-image.sh <cibuildwheel-log> <expected image ref> [<build-outcome>]}"
REF="${2:?usage: assert-wheel-image.sh <cibuildwheel-log> <expected image ref> [<build-outcome>]}"
BUILD_OUTCOME="${3-success}"

# A build that never got far enough to start a container is a SUFFICIENT
# explanation for a missing image line, so no claim is made about the pin either
# way. Deliberately checked BEFORE the log-exists branch: a build that died
# early may well have produced no log at all.
if [ "$BUILD_OUTCOME" != "success" ]; then
  if [ ! -f "$LOG" ] || ! grep -qF "Starting container image $REF" "$LOG"; then
    echo "pinned-image: the Build step reported \`${BUILD_OUTCOME}\`, and no line naming the pinned image was found. A build that did not reach the container is a sufficient explanation, so NO claim is made about the pin. Fix the build first; this assertion is only meaningful on a successful one."
    exit 0
  fi
fi

if [ ! -f "$LOG" ]; then
  echo "::error::cibuildwheel produced no log at '$LOG', so the pinned-image assertion could not run. Treat this run's ccache tag as unverified rather than assuming the pin held."
  exit 1
fi

echo "pinned:  $REF"
# Show what the log DOES say before judging it — on a failure this is the whole
# diagnosis, and printing it only in the error branch would hide it whenever the
# expected line is merely spelled differently.
grep -E 'Starting container image' "$LOG" | head -3 || true

# `grep -F` — the reference contains '/' , '.' and ':' , none of which should be
# read as regex. `-c` and an explicit numeric test rather than bare `grep -q`,
# so a zero count is an EXPLICIT failure rather than an exit status that a
# future `|| true` could swallow.
n="$(grep -cF "Starting container image $REF" "$LOG" || true)"
if [ "${n:-0}" -lt 1 ]; then
  echo "::error::cibuildwheel did NOT start the pinned image. Expected 'Starting container image ${REF}'. The pin in bindings/python/pyproject.toml is not taking effect, so this lane's ccache tag is keyed to a toolchain that is not building the wheel — the cache would report hits against the wrong compiler."
  exit 1
fi

echo "pinned-image assertion: matched ${n} line(s)."
