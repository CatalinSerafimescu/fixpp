#!/usr/bin/env bash
# ci/apt-guard.sh — bound and retry an apt-backed install command.
#
#   ci/apt-guard.sh <label> -- <command> [args...]
#
# WHY THIS EXISTS (#300)
#
# Every Linux lane in every tier installs its toolchain prerequisites with a
# bare `apt-get install` (or `llvm.sh <N> all`, which is an apt operation
# wearing a different hat). None of them carried a bound. When an Ubuntu or
# apt.llvm.org mirror is slow or wedged the command does not fail — it HANGS,
# and it burns the JOB-level timeout (180-240 min) before anything goes red.
#
# The failure does not announce itself. It presents as "CI is slow today", or
# at the extreme as a job that times out during *Build*, because by the time
# the job timeout fires the log's last useful line is an apt fetch that never
# returned. Attribution costs a manual log read every time.
#
# ⚠️ ORDER MATTERS, AND THE TIMEOUT IS THE LOAD-BEARING HALF. A retry without
# a timeout MULTIPLIES the hang rather than bounding it. This script therefore
# bounds EVERY attempt individually; the retry only ever runs against an
# already-bounded attempt. Do not add a retry path that skips the `timeout`.
#
# WHY A WRAPPER RATHER THAN `timeout-minutes:` ON THE STEP
#
# Most apt calls in this repo are not standalone steps — they are the tail of a
# composite step that first runs `llvm.sh <N> all`, symlinks the unversioned
# compiler, and prints a version. Re-derive with:
#
#   grep -n 'apt-get install' .github/workflows/*.yml
#
# A step-level `timeout-minutes` on those composite steps would have to be
# large enough to cover a legitimate full-toolchain install, which is exactly
# the bound that fails to catch a wedged mirror. Bounding the individual
# command instead keeps the budget tight AND names apt in the failure, which is
# the attribution half of the problem.
#
# EXIT CODES
#   0    the command succeeded (on some attempt)
#   124  every attempt timed out — `timeout`'s own code, preserved deliberately
#        so a wedged mirror is distinguishable from a package that does not
#        exist
#   *    the command's own failing exit code from the final attempt
set -euo pipefail

# Per-attempt wall-clock budget and attempt count. Overridable from the
# environment so a workflow can widen a genuinely heavy install (a full LLVM
# toolchain) without every caller re-stating the common case, and so the test
# harness can drive both to small values.
#
# ⚠️ These are BUDGETS, not measurements. A healthy install is far under
# APT_GUARD_TIMEOUT; the budget is sized to catch a wedged mirror, not to track
# how long apt currently takes. Do not "tune" them toward an observed duration
# — that converts a bound into a flake.
APT_GUARD_TIMEOUT="${APT_GUARD_TIMEOUT:-300}"
APT_GUARD_ATTEMPTS="${APT_GUARD_ATTEMPTS:-3}"
APT_GUARD_BACKOFF="${APT_GUARD_BACKOFF:-15}"
# Grace between SIGTERM and SIGKILL. Overridable only so the harness can drive
# it small; no caller should need to change it.
APT_GUARD_KILL_AFTER="${APT_GUARD_KILL_AFTER:-10}"

if [ "$#" -lt 3 ]; then
    echo "usage: ci/apt-guard.sh <label> -- <command> [args...]" >&2
    exit 2
fi

LABEL="$1"
shift
if [ "$1" != "--" ]; then
    echo "::error::apt-guard: expected '--' after the label, got '$1'" >&2
    exit 2
fi
shift

# `sudo` is how every caller invokes apt, and sudo does not forward a SIGTERM
# to its child. `timeout --kill-after` is what closes that: SIGTERM first (so a
# well-behaved apt can unwind), SIGKILL 10s later if it is genuinely wedged.
# Without --kill-after a hung `sudo apt-get` survives its own timeout and the
# bound is decorative.
run_attempt() {
    timeout --kill-after="${APT_GUARD_KILL_AFTER}s" "${APT_GUARD_TIMEOUT}s" "$@"
}

# Killing apt mid-transaction can leave dpkg needing a `--configure -a` before
# it will accept another install. Without this the retry fails on a broken
# dpkg state rather than on the mirror, which reads as an unrelated defect.
# Best-effort: it must never itself fail the guard.
recover_dpkg() {
    if command -v dpkg >/dev/null 2>&1; then
        sudo dpkg --configure -a >/dev/null 2>&1 || true
    fi
}

rc=0
attempt=1
while [ "$attempt" -le "$APT_GUARD_ATTEMPTS" ]; do
    echo "── apt-guard [$LABEL] attempt $attempt/$APT_GUARD_ATTEMPTS (bound ${APT_GUARD_TIMEOUT}s): $*"
    rc=0
    run_attempt "$@" || rc=$?

    if [ "$rc" -eq 0 ]; then
        [ "$attempt" -gt 1 ] && echo "── apt-guard [$LABEL] succeeded on attempt $attempt"
        exit 0
    fi

    # 124 is `timeout`'s "the command outlived its budget". Distinguished in
    # the log because it means the MIRROR, not the package set — the two need
    # different responses from whoever reads the failure.
    if [ "$rc" -eq 124 ]; then
        echo "::warning::apt-guard [$LABEL] attempt $attempt exceeded ${APT_GUARD_TIMEOUT}s (mirror slow or wedged)"
    else
        echo "::warning::apt-guard [$LABEL] attempt $attempt failed with exit $rc"
    fi

    if [ "$attempt" -lt "$APT_GUARD_ATTEMPTS" ]; then
        recover_dpkg
        sleep_for=$(( APT_GUARD_BACKOFF * attempt ))
        echo "── apt-guard [$LABEL] retrying in ${sleep_for}s"
        sleep "$sleep_for"
    fi
    attempt=$(( attempt + 1 ))
done

# Attribution: the whole point of the issue is that this failure currently
# reads as "the build timed out". Name apt, the label, and the budget, so the
# log line that survives says what actually broke.
if [ "$rc" -eq 124 ]; then
    echo "::error::apt-guard [$LABEL] FAILED — all $APT_GUARD_ATTEMPTS attempts exceeded ${APT_GUARD_TIMEOUT}s. This is a wedged or degraded apt mirror, not a build failure."
else
    echo "::error::apt-guard [$LABEL] FAILED — all $APT_GUARD_ATTEMPTS attempts failed; last exit $rc."
fi
exit "$rc"
