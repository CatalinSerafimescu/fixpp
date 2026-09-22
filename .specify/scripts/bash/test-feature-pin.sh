#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# .specify/scripts/bash/test-feature-pin.sh
#
# Guards the fixpp-local patch to .specify/scripts/bash/common.sh (fixpp#490):
# the tracked .specify/feature.json pin must not resolve a branch it was not
# written on. Runs the repo's own common.sh + check-prerequisites.sh inside a
# throwaway git repo, so the real pin and working tree are never touched.
# NOT wired into CI (a .specify/-only change runs no matrix, by choice): run it
# by hand after any Spec-Kit refresh — a refresh that drops the patch goes RED.
set -euo pipefail
# An exported GIT_DIR (e.g. ctest run from a git hook) would point every
# `git -C "$work"` below at the REAL checkout — commits and branch switches included.
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_COMMON_DIR GIT_CEILING_DIRECTORIES

scripts="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
work="${tmp}/repo"
mkdir -p "${work}/.specify/scripts/bash"
cp "${scripts}/common.sh" "${scripts}/check-prerequisites.sh" "${work}/.specify/scripts/bash/"
mkdir -p "${work}/specs/089-shipped" "${work}/specs/090-bundle" "${work}/specs/091-own"

g() { git -C "$work" "$@"; }
g init -q
g config user.name t
g config user.email t@t
g commit -q --allow-empty -m root
g branch -M main

# A PATH with jq and python3 removed, to exercise the grep/sed reader and the
# printf writer (Windows runners commonly lack jq). Only PATH entries holding
# one of them are replaced, by a filtered symlink copy; the rest are kept.
nojq=''
IFS=: read -r -a path_dirs <<< "$PATH"
for i in "${!path_dirs[@]}"; do
    d="${path_dirs[$i]}"
    if [[ -e "$d/jq" || -e "$d/python3" ]]; then
        f_dir="${tmp}/nojq${i}"
        mkdir -p "$f_dir"
        for f in "$d"/*; do
            case "${f##*/}" in jq|python3|python3.*) continue ;; esac
            ln -s "$f" "${f_dir}/${f##*/}" 2>/dev/null || true
        done
        d="$f_dir"
    fi
    nojq="${nojq:+${nojq}:}${d}"
done

fails=0
pass() { echo "PASS: $1"; }
fail() { echo "FAIL: $1"; fails=$((fails + 1)); }

pin() { printf '%s\n' "$1" > "${work}/.specify/feature.json"; }

# Resolve with every SPECIFY_* override scrubbed: an inherited one would make
# every arm below vacuous. $1 = PATH to use.
resolve() {
    (cd "$work" && env -u SPECIFY_FEATURE -u SPECIFY_FEATURE_DIRECTORY -u SPECIFY_INIT_DIR \
        PATH="$1" bash .specify/scripts/bash/check-prerequisites.sh --paths-only 2>&1)
}
field() { sed -n "s/^$1: //p" <<< "$2"; }
# A refusal must be THIS refusal: any non-zero exit (a crash, an unbound
# variable) would otherwise pass. $1 = arm, $2 = expected reason text.
refused() {
    local out
    if out="$(resolve "$P")"; then
        fail "[$mode] $1 resolved: $(field FEATURE_DIR "$out")"
    elif grep -qF "$2" <<< "$out" && grep -qF 'fixpp#490' <<< "$out"; then
        pass "[$mode] $1 refused"
    else
        fail "[$mode] $1 failed for the wrong reason: ${out}"
    fi
}

for mode in default nojq; do
    P="$PATH"; [[ "$mode" == nojq ]] && P="$nojq"
    if [[ "$mode" == nojq ]] && PATH="$P" bash -c 'command -v jq || command -v python3' >/dev/null 2>&1; then
        fail "[$mode] jq or python3 still reachable"; continue
    fi

    # 1. Bundle-less branch + legacy pin to a shipped feature -> must refuse.
    g switch -q -C 447-bundleless main
    pin '{"feature_directory":"specs/089-shipped"}'
    refused "bundle-less branch" "No feature for branch '447-bundleless'"

    # 1b. --paths-only must never write the pin (upstream #3025), even when the
    #     env override differs from it in both directory and recorded branch.
    pin '{"feature_directory":"specs/089-shipped","branch":"main"}'
    before="$(cksum < "${work}/.specify/feature.json")"
    (cd "$work" && env -u SPECIFY_FEATURE -u SPECIFY_INIT_DIR PATH="$P" \
        SPECIFY_FEATURE_DIRECTORY=specs/090-bundle \
        bash .specify/scripts/bash/check-prerequisites.sh --paths-only >/dev/null 2>&1) || true
    [[ "$(cksum < "${work}/.specify/feature.json")" == "$before" ]] \
        && pass "[$mode] --paths-only left feature.json untouched" \
        || fail "[$mode] --paths-only rewrote feature.json"

    # 2. Branch with its own bundle + pin recorded for another branch -> own bundle.
    g switch -q -C 091-own main
    pin '{"feature_directory":"specs/089-shipped","branch":"main"}'
    if out="$(resolve "$P")" && [[ "$(field FEATURE_DIR "$out")" == "${work}/specs/091-own" ]]; then
        pass "[$mode] stale pin ignored for branch with own bundle"
    else
        fail "[$mode] branch with own bundle: ${out}"
    fi

    # 3. Same branch, pin recorded for it but naming another bundle -> refuse.
    pin '{"feature_directory":"specs/090-bundle","branch":"091-own"}'
    refused "pin/branch-bundle disagreement" "also has its own bundle"

    # 4. B6 shape: branch name differs from the bundle, pin recorded on this
    #    branch (key order reversed to exercise the grep/sed reader) -> the pin,
    #    and BRANCH reports the git branch, not the bundle.
    g switch -q -C 447-bundleless main
    pin '{"branch":"447-bundleless","feature_directory":"specs/090-bundle"}'
    if out="$(resolve "$P")" && [[ "$(field FEATURE_DIR "$out")" == "${work}/specs/090-bundle" ]]; then
        pass "[$mode] pin recorded on this branch honoured"
    else
        fail "[$mode] pin recorded on this branch: ${out}"
    fi
    [[ "$(field BRANCH "${out:-}")" == 447-bundleless ]] \
        && pass "[$mode] BRANCH is the git branch" \
        || fail "[$mode] BRANCH reported as '$(field BRANCH "${out:-}")'"

    # 5. Legacy pin (no branch key) whose bundle is named after the branch -> honoured.
    g switch -q -C 089-shipped main
    pin '{"feature_directory":"specs/089-shipped"}'
    if out="$(resolve "$P")" && [[ "$(field FEATURE_DIR "$out")" == "${work}/specs/089-shipped" ]]; then
        pass "[$mode] legacy pin matching the branch honoured"
    else
        fail "[$mode] legacy pin matching the branch: ${out}"
    fi

    # 6. Detached HEAD -> refuse (no branch to validate against).
    g switch -q --detach main
    refused "detached HEAD" "Detached HEAD"

    # 7. Persisting via SPECIFY_FEATURE_DIRECTORY records the branch, and the
    #    written pin then resolves on that branch without the env var.
    g switch -q -C 447-bundleless main
    rm -f "${work}/.specify/feature.json"
    (cd "$work" && env -u SPECIFY_FEATURE -u SPECIFY_INIT_DIR PATH="$P" \
        SPECIFY_FEATURE_DIRECTORY=specs/090-bundle \
        bash -c 'source .specify/scripts/bash/common.sh && get_feature_paths >/dev/null')
    if grep -q '"branch":"447-bundleless"' "${work}/.specify/feature.json" \
        && out="$(resolve "$P")" && [[ "$(field FEATURE_DIR "$out")" == "${work}/specs/090-bundle" ]]; then
        pass "[$mode] persisted pin records its branch and round-trips"
    else
        fail "[$mode] persist round-trip: $(cat "${work}/.specify/feature.json") / ${out:-}"
    fi
done

if (( fails )); then
    echo "test-feature-pin: ${fails} arm(s) FAILED"
    exit 1
fi
echo "test-feature-pin: all arms passed"
