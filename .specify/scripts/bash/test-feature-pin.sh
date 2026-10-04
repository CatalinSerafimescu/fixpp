#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# .specify/scripts/bash/test-feature-pin.sh
#
# Guards the fixpp-local patch to .specify/scripts/bash/common.sh (fixpp#490):
# the tracked .specify/feature.json pin must not resolve an unrelated branch's
# feature. A pin is trusted only on the branch it records, or when it names an
# existing specs/<branch> itself — its identity is the whole normalized path, never its
# basename. A pin trusted by branch NAME must also still be identified by its
# recorded "pinned_at" commit, so a re-created branch of the same name does not
# inherit a merged pin (fixpp#499, arms 17-20). Every arm runs three times: with
# jq, with only jq removed (the python3 reader), and with both removed (the
# grep/sed reader and the printf writer).
# Runs the repo's own common.sh + check-prerequisites.sh inside a
# throwaway git repo, so the real pin and working tree are never touched.
# NOT wired into CI (a .specify/-only change runs no matrix, by choice): run it
# by hand after any Spec-Kit refresh — a refresh that drops the patch goes RED.
set -euo pipefail
# An exported GIT_DIR (e.g. run from a git hook) would point every
# `git -C "$work"` below at the REAL checkout — commits and branch switches included.
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_COMMON_DIR GIT_CEILING_DIRECTORIES

scripts="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
work="${tmp}/repo"
mkdir -p "${work}/.specify/scripts/bash"
cp "${scripts}/common.sh" "${scripts}/check-prerequisites.sh" "${work}/.specify/scripts/bash/"
mkdir -p "${work}/specs/089-shipped" "${work}/specs/090-bundle" "${work}/specs/091-own" "${work}/specs/feature/x"

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
# A PATH with only jq removed, so the python3 reader runs (fixpp#499): with jq
# gone it is the first parser tried, and the nojq PATH above removes it too.
pyonly=''
for i in "${!path_dirs[@]}"; do
    d="${path_dirs[$i]}"
    if [[ -e "$d/jq" ]]; then
        f_dir="${tmp}/pyonly${i}"
        mkdir -p "$f_dir"
        for f in "$d"/*; do
            [[ "${f##*/}" == jq ]] && continue
            ln -s "$f" "${f_dir}/${f##*/}" 2>/dev/null || true
        done
        d="$f_dir"
    fi
    pyonly="${pyonly:+${pyonly}:}${d}"
done

fails=0
pass() { echo "PASS: $1"; }
fail() { echo "FAIL: $1"; fails=$((fails + 1)); }

pin() { printf '%s\n' "$1" > "${work}/.specify/feature.json"; }

field() { sed -n "s/^$1: //p" <<< "$2"; }
# Resolve with every SPECIFY_* override scrubbed: an inherited one would make
# every arm below vacuous. $1 = PATH to use. Extra args ($2+) are VAR=val
# assignments applied only to this one invocation (e.g. GIT_DIR=...,
# GIT_TEST_ASSUME_DIFFERENT_OWNER=1), so the leak under test is injected per
# call and never left set for the rest of the suite.
resolve() {
    local pathval="$1"; shift
    (cd "$work" && env -u SPECIFY_FEATURE -u SPECIFY_FEATURE_DIRECTORY -u SPECIFY_INIT_DIR \
        PATH="$pathval" "$@" bash .specify/scripts/bash/check-prerequisites.sh --paths-only 2>&1)
}
# Same as resolve(), rooted at an arbitrary directory holding its own
# .specify/scripts/bash copy (arms 10-11, which are not under $work).
resolve_in() {
    local dir="$1" pathval="$2"; shift 2
    (cd "$dir" && env -u SPECIFY_FEATURE -u SPECIFY_FEATURE_DIRECTORY -u SPECIFY_INIT_DIR \
        PATH="$pathval" "$@" bash .specify/scripts/bash/check-prerequisites.sh --paths-only 2>&1)
}
# A refusal must be THIS refusal: any non-zero exit (a crash, an unbound
# variable) would otherwise pass. $1 = arm, $2 = expected reason text,
# $3+ = extra env assignments forwarded to resolve().
refused() {
    local out
    if out="$(resolve "$P" "${@:3}")"; then
        fail "[$mode] $1 resolved: $(field FEATURE_DIR "$out")"
    elif grep -qF "$2" <<< "$out" && grep -qF 'fixpp#490' <<< "$out"; then
        pass "[$mode] $1 refused"
    else
        fail "[$mode] $1 failed for the wrong reason: ${out}"
    fi
}

# --- Fixtures outside $work, for the git-failure / non-git / unborn-branch
#     paths (fixpp#490). ---

# Arm 8: a second git repo on branch 'foreign', with its own bundle, so a
# leaked GIT_DIR resolves plausibly instead of erroring for an unrelated
# reason.
other="${tmp}/other"
mkdir -p "${other}/specs/089-shipped"
git -C "$other" init -q -b foreign
git -C "$other" config user.name t
git -C "$other" config user.email t@t
git -C "$other" commit -q --allow-empty -m root

# Arm 10: a directory with NO .git ancestor at all, holding its own copy of
# the scripts and a legacy (no-branch) pin -- exercises the genuine non-git
# path (upstream behaviour kept). mktemp honours TMPDIR, so confirm the
# precondition rather than assume it.
nogit="${tmp}/nogit"
mkdir -p "${nogit}/.specify/scripts/bash" "${nogit}/specs/089-shipped"
cp "${scripts}/common.sh" "${scripts}/check-prerequisites.sh" "${nogit}/.specify/scripts/bash/"
printf '{"feature_directory":"specs/089-shipped"}\n' > "${nogit}/.specify/feature.json"
d="$nogit"
while [[ -n "$d" ]]; do
    if [[ -e "$d/.git" ]]; then
        fail "arm 10 precondition: $nogit has a .git ancestor at $d (TMPDIR is inside a git tree)"
        break
    fi
    [[ "$d" == / ]] && break
    d="$(dirname "$d")"
done

# Arm 11: a fresh, unborn-branch repo (no commit yet) with its own scripts and
# a matching bundle, no pin.
unborn="${tmp}/unborn"
mkdir -p "${unborn}/.specify/scripts/bash" "${unborn}/specs/092-unborn"
cp "${scripts}/common.sh" "${scripts}/check-prerequisites.sh" "${unborn}/.specify/scripts/bash/"
git -C "$unborn" init -q -b 092-unborn
git -C "$unborn" config user.name t
git -C "$unborn" config user.email t@t

for mode in default nojq pyonly; do
    P="$PATH"; [[ "$mode" == nojq ]] && P="$nojq"; [[ "$mode" == pyonly ]] && P="$pyonly"
    if [[ "$mode" == nojq ]] && PATH="$P" bash -c 'command -v jq || command -v python3' >/dev/null 2>&1; then
        fail "[$mode] jq or python3 still reachable"; continue
    fi
    if [[ "$mode" == pyonly ]] && { PATH="$P" bash -c 'command -v jq' >/dev/null 2>&1 \
            || ! PATH="$P" bash -c 'command -v python3' >/dev/null 2>&1; }; then
        fail "[$mode] jq still reachable, or python3 not reachable"; continue
    fi

    # 1. Bundle-less branch + legacy pin to a shipped feature -> must refuse.
    g switch -q -C 447-bundleless main
    pin '{"feature_directory":"specs/089-shipped"}'
    refused "bundle-less branch" "No feature for branch '447-bundleless'"

    # 1b. --paths-only resolves via SPECIFY_FEATURE_DIRECTORY and never writes
    #     the pin (upstream #3025), even when the override differs from it in
    #     both directory and recorded branch.
    pin '{"feature_directory":"specs/089-shipped","branch":"main"}'
    before="$(cksum < "${work}/.specify/feature.json")"
    rc=0
    out="$(cd "$work" && env -u SPECIFY_FEATURE -u SPECIFY_INIT_DIR PATH="$P" \
        SPECIFY_FEATURE_DIRECTORY=specs/090-bundle \
        bash .specify/scripts/bash/check-prerequisites.sh --paths-only 2>&1)" || rc=$?
    if [[ $rc -eq 0 && "$(field FEATURE_DIR "$out")" == "${work}/specs/090-bundle" ]]; then
        pass "[$mode] --paths-only resolved via the override"
    else
        fail "[$mode] --paths-only via override: rc=$rc ${out}"
    fi
    [[ "$(cksum < "${work}/.specify/feature.json")" == "$before" ]] \
        && pass "[$mode] --paths-only left feature.json untouched" \
        || fail "[$mode] --paths-only rewrote feature.json"

    # 2. Branch with its own bundle + pin recorded for another branch -> own
    #    bundle, with the stale-pin NOTE naming the ignored pin, its recorded
    #    branch, and the bundle used (fixpp#490).
    g switch -q -C 091-own main
    pin '{"feature_directory":"specs/089-shipped","branch":"main"}'
    if out="$(resolve "$P")" && [[ "$(field FEATURE_DIR "$out")" == "${work}/specs/091-own" ]]; then
        pass "[$mode] stale pin ignored for branch with own bundle"
    else
        fail "[$mode] branch with own bundle: ${out}"
    fi
    if grep -qF "NOTE: ignoring .specify/feature.json pin 'specs/089-shipped' (branch 'main'); using specs/091-own (fixpp#490)." <<< "${out:-}"; then
        pass "[$mode] stale-pin NOTE names the ignored pin and the branch bundle"
    else
        fail "[$mode] stale-pin NOTE missing or wrong: ${out:-}"
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
    grep -qF 'NOTE:' <<< "${out:-}" \
        && fail "[$mode] honoured pin unexpectedly emitted a NOTE: ${out:-}" \
        || pass "[$mode] honoured pin emitted no NOTE"

    # 5. Legacy pin (no branch key) naming specs/<branch> itself -> honoured.
    g switch -q -C 089-shipped main
    pin '{"feature_directory":"specs/089-shipped"}'
    if out="$(resolve "$P")" && [[ "$(field FEATURE_DIR "$out")" == "${work}/specs/089-shipped" ]]; then
        pass "[$mode] legacy pin matching the branch honoured"
    else
        fail "[$mode] legacy pin matching the branch: ${out}"
    fi
    grep -qF 'NOTE:' <<< "${out:-}" \
        && fail "[$mode] honoured legacy pin unexpectedly emitted a NOTE: ${out:-}" \
        || pass "[$mode] honoured legacy pin emitted no NOTE"

    # 6. Detached HEAD -> refuse (no branch to validate against).
    g switch -q --detach main
    refused "detached HEAD" "Detached HEAD"

    # 7. Persisting via SPECIFY_FEATURE_DIRECTORY records the branch, and the
    #    written pin then resolves on that branch without the env var.
    g switch -q -C 447-bundleless main
    rm -f "${work}/.specify/feature.json"
    rc=0
    (cd "$work" && env -u SPECIFY_FEATURE -u SPECIFY_INIT_DIR PATH="$P" \
        SPECIFY_FEATURE_DIRECTORY=specs/090-bundle \
        bash -c 'source .specify/scripts/bash/common.sh && get_feature_paths >/dev/null') || rc=$?
    if [[ $rc -eq 0 ]] && grep -q '"branch":"447-bundleless"' "${work}/.specify/feature.json" \
        && out="$(resolve "$P")" && [[ "$(field FEATURE_DIR "$out")" == "${work}/specs/090-bundle" ]]; then
        pass "[$mode] persisted pin records its branch and round-trips"
    else
        fail "[$mode] persist round-trip: rc=$rc $(cat "${work}/.specify/feature.json" 2>/dev/null) / ${out:-}"
    fi

    # 7b. Re-persisting a LEGACY pin (same directory, no branch key) still
    #     writes the branch: the skip-write check must compare branch too, not
    #     just feature_directory (fixpp#490 migration path).
    pin '{"feature_directory":"specs/090-bundle"}'
    rc=0
    (cd "$work" && env -u SPECIFY_FEATURE -u SPECIFY_INIT_DIR PATH="$P" \
        SPECIFY_FEATURE_DIRECTORY=specs/090-bundle \
        bash -c 'source .specify/scripts/bash/common.sh && get_feature_paths >/dev/null') || rc=$?
    if [[ $rc -eq 0 ]] && grep -q '"branch":"447-bundleless"' "${work}/.specify/feature.json"; then
        pass "[$mode] re-persisting a legacy same-directory pin still records the branch"
    else
        fail "[$mode] legacy pin migration: rc=$rc $(cat "${work}/.specify/feature.json" 2>/dev/null)"
    fi

    # 8. A foreign GIT_DIR (leaked from the caller's environment) must not
    #    redirect branch detection to a different repository (fixpp#490
    #    recreated).
    g switch -q -C 447-bundleless main
    pin '{"feature_directory":"specs/089-shipped","branch":"foreign"}'
    refused "foreign GIT_DIR" "No feature for branch '447-bundleless'" "GIT_DIR=${other}/.git"

    # 9. A git failure at repo_root (dubious ownership) must refuse, not fall
    #    back to the untrusted legacy pin.
    rc=0
    env GIT_TEST_ASSUME_DIFFERENT_OWNER=1 PATH="$P" git -C "$work" rev-parse >/dev/null 2>&1 || rc=$?
    if [[ $rc -eq 0 ]]; then
        fail "[$mode] arm 9 precondition: GIT_TEST_ASSUME_DIFFERENT_OWNER=1 not honoured by this git (safe.directory=$(git config --global --get-all safe.directory 2>/dev/null | paste -sd, -))"
    else
        refused "git failure (dubious ownership)" "git cannot read the branch" GIT_TEST_ASSUME_DIFFERENT_OWNER=1
    fi

    # 10. A genuine non-git directory keeps the upstream legacy-pin behaviour.
    if out="$(resolve_in "$nogit" "$P")" && [[ "$(field FEATURE_DIR "$out")" == "${nogit}/specs/089-shipped" ]]; then
        pass "[$mode] genuine non-git directory resolves the legacy pin"
    else
        fail "[$mode] genuine non-git directory: ${out:-}"
    fi

    # 11. An unborn branch (no commit yet) resolves its own bundle rather than
    #     being misclassified as detached HEAD.
    if out="$(resolve_in "$unborn" "$P")" && [[ "$(field FEATURE_DIR "$out")" == "${unborn}/specs/092-unborn" ]]; then
        pass "[$mode] unborn branch resolves its own bundle"
    else
        fail "[$mode] unborn branch: ${out:-}"
    fi

    # 12. Pin whose BASENAME is the branch but whose path is elsewhere, recorded
    #     on another branch -> stale: the branch bundle, with the NOTE.
    g switch -q -C 091-own main
    pin '{"feature_directory":"elsewhere/091-own","branch":"main"}'
    if out="$(resolve "$P")" && [[ "$(field FEATURE_DIR "$out")" == "${work}/specs/091-own" ]] \
        && grep -qF "NOTE: ignoring .specify/feature.json pin 'elsewhere/091-own' (branch 'main')" <<< "$out"; then
        pass "[$mode] basename-alias pin recorded elsewhere is stale"
    else
        fail "[$mode] basename-alias stale pin: ${out:-}"
    fi
    # 13. Same path, recorded on THIS branch, while specs/<branch> exists -> refuse.
    pin '{"feature_directory":"elsewhere/091-own","branch":"091-own"}'
    refused "basename-alias pin/branch-bundle disagreement" "also has its own bundle"
    # 14. Path normalisation: ./, trailing slash, absolute -- each names specs/091-own.
    for fd in "./specs/091-own" "specs/091-own/" "${work}/specs/091-own"; do
        pin "{\"feature_directory\":\"${fd}\",\"branch\":\"091-own\"}"
        if out="$(resolve "$P")" && [[ "$(field FEATURE_DIR "$out")" == "${work}/specs/091-own" ]] \
            && ! grep -qF 'NOTE:' <<< "$out"; then
            pass "[$mode] pin '${fd}' is the branch bundle"
        else
            fail "[$mode] pin '${fd}': ${out:-}"
        fi
    done
    # 14b. A pin naming specs/<branch> exactly but recorded on ANOTHER branch
    #      (create-new-feature.sh persists before any branch switch) -> the
    #      same directory, honoured, no NOTE.
    pin '{"feature_directory":"specs/091-own","branch":"main"}'
    if out="$(resolve "$P")" && [[ "$(field FEATURE_DIR "$out")" == "${work}/specs/091-own" ]] \
        && ! grep -qF 'NOTE:' <<< "$out"; then
        pass "[$mode] pin naming specs/<branch> recorded elsewhere honoured"
    else
        fail "[$mode] pin naming specs/<branch> recorded elsewhere: ${out:-}"
    fi
    # 15. Slash branch: persisted pin round-trips; legacy form honoured.
    g switch -q -C feature/x main
    rm -f "${work}/.specify/feature.json"
    rc=0
    (cd "$work" && env -u SPECIFY_FEATURE -u SPECIFY_INIT_DIR PATH="$P" \
        SPECIFY_FEATURE_DIRECTORY=specs/feature/x \
        bash -c 'source .specify/scripts/bash/common.sh && get_feature_paths >/dev/null') || rc=$?
    if [[ $rc -eq 0 ]] && grep -qF '"branch":"feature/x"' "${work}/.specify/feature.json" \
        && out="$(resolve "$P")" && [[ "$(field FEATURE_DIR "$out")" == "${work}/specs/feature/x" ]] \
        && ! grep -qF 'NOTE:' <<< "$out"; then
        pass "[$mode] slash-branch pin round-trips"
    else
        fail "[$mode] slash-branch round-trip: rc=$rc $(cat "${work}/.specify/feature.json" 2>/dev/null) / ${out:-}"
    fi
    pin '{"feature_directory":"specs/feature/x"}'
    if out="$(resolve "$P")" && [[ "$(field FEATURE_DIR "$out")" == "${work}/specs/feature/x" ]] \
        && ! grep -qF 'NOTE:' <<< "$out"; then
        pass "[$mode] slash-branch legacy pin honoured"
    else
        fail "[$mode] slash-branch legacy pin: ${out:-}"
    fi

    # 16. Bundle-less branch + a pin naming specs/<branch> exactly but NOT
    #     recorded on this branch -> refuse: path identity alone is trusted
    #     only when that bundle exists (fixpp#496 Gate B r3). A pin recorded
    #     on THIS branch is honoured even when its directory does not exist
    #     yet (explicit SPECIFY_FEATURE_DIRECTORY persisted here).
    g switch -q -C 093-ghost main
    if [[ -e "${work}/specs/093-ghost" ]]; then
        fail "[$mode] arm 16 precondition: specs/093-ghost exists"
    fi
    pin '{"feature_directory":"specs/093-ghost","branch":"main"}'
    refused "bundle-less branch, pin naming its path recorded elsewhere" "No feature for branch '093-ghost'"
    pin '{"feature_directory":"specs/093-ghost"}'
    refused "bundle-less branch, legacy pin naming its path" "No feature for branch '093-ghost'"
    pin '{"feature_directory":"specs/093-ghost","branch":"093-ghost"}'
    if out="$(resolve "$P")" && [[ "$(field FEATURE_DIR "$out")" == "${work}/specs/093-ghost" ]] \
        && ! grep -qF 'NOTE:' <<< "$out"; then
        pass "[$mode] pin recorded on this branch honoured before its directory exists"
    else
        fail "[$mode] this-branch pin to a not-yet-created directory: ${out:-}"
    fi

    # --- fixpp#499: a pin trusted by branch NAME must not survive into a
    #     re-created branch of that name. Each arm builds its own repo, because
    #     these arms commit the pin and merge it, which the shared $work repo's
    #     uncommitted-pin arms above must not see. The branch is 094-reuse and
    #     the pinned bundle specs/090-bundle, so only the branch-name rule can
    #     honour the pin; specs/094-reuse never exists.
    reuse="${tmp}/reuse-${mode}"
    rr() { git -C "$reuse" "$@"; }
    new_reuse_repo() {
        rm -rf "$reuse"
        mkdir -p "${reuse}/.specify/scripts/bash" "${reuse}/specs/090-bundle"
        cp "${scripts}/common.sh" "${scripts}/check-prerequisites.sh" "${reuse}/.specify/scripts/bash/"
        : > "${reuse}/specs/090-bundle/spec.md"
        rr init -q -b main
        rr config user.name t
        rr config user.email t@t
        rr add -A
        rr commit -q -m root
    }
    # Persist a pin the way a Spec-Kit command does: SPECIFY_FEATURE_DIRECTORY
    # through get_feature_paths without --no-persist.
    persist_reuse() {
        (cd "$reuse" && env -u SPECIFY_FEATURE -u SPECIFY_INIT_DIR PATH="$P" \
            SPECIFY_FEATURE_DIRECTORY="$1" \
            bash -c 'source .specify/scripts/bash/common.sh && get_feature_paths >/dev/null')
    }
    commit_in_reuse() {  # $1 = file to touch, $2 = message
        printf '%s\n' "$2" >> "${reuse}/$1"
        rr add -A
        rr commit -q -m "$2"
    }
    honoured_reuse() {  # $1 = arm
        local out
        if out="$(resolve_in "$reuse" "$P")" && [[ "$(field FEATURE_DIR "$out")" == "${reuse}/specs/090-bundle" ]]; then
            pass "[$mode] $1"
        else
            fail "[$mode] $1: ${out:-}"
        fi
    }
    refused_reuse() {  # $1 = arm; the refusal must be the identity refusal
        local out
        if out="$(resolve_in "$reuse" "$P")"; then
            fail "[$mode] $1 resolved: $(field FEATURE_DIR "$out")"
        elif grep -qF 'does not identify this branch' <<< "$out" \
                && grep -qF 'fixpp#490' <<< "$out" \
                && grep -qF "SPECIFY_FEATURE_DIRECTORY='specs/090-bundle'" <<< "$out"; then
            pass "[$mode] $1 refused"
        else
            fail "[$mode] $1 failed for the wrong reason: ${out}"
        fi
    }

    # 17. RED shape (the issue's): pin on 094-reuse, merge it, delete the
    #     branch, re-create it from main without specs/094-reuse -> refuse.
    #     Both pin shapes (pinned before the branch has a commit of its own, as
    #     /speckit-specify does right after `git switch -c`; and pinned after
    #     one), under both a merge commit and a squash.
    for shape in pin-first pin-after-commit; do
        for how in no-ff squash; do
            new_reuse_repo
            rr switch -q -c 094-reuse main
            [[ "$shape" == pin-after-commit ]] && commit_in_reuse work.txt "own work"
            if ! persist_reuse specs/090-bundle; then
                fail "[$mode] arm 17 ($shape, $how): persisting the pin failed"; continue
            fi
            honoured_reuse "pin honoured on its own branch before commit ($shape)"
            rr add -A
            rr commit -q -m pin
            honoured_reuse "pin honoured on its own branch after commit ($shape, $how)"
            rr switch -q main
            if [[ "$how" == no-ff ]]; then
                rr merge -q --no-ff -m merge 094-reuse
            else
                rr merge -q --squash 094-reuse >/dev/null
                rr commit -q -m squash
            fi
            rr branch -q -D 094-reuse
            rr switch -q -c 094-reuse main
            if [[ -e "${reuse}/specs/094-reuse" ]] || ! grep -qF '"branch":"094-reuse"' "${reuse}/.specify/feature.json"; then
                fail "[$mode] arm 17 precondition ($shape, $how): specs/094-reuse exists or the merged pin is not for 094-reuse"
                continue
            fi
            refused_reuse "re-created branch inheriting a merged pin ($shape, $how)"
        done
    done

    # 18. Must honour: main advances and is MERGED into the branch, as this
    #     repo routinely does; and the branch keeps working after it.
    new_reuse_repo
    rr switch -q -c 094-reuse main
    persist_reuse specs/090-bundle || fail "[$mode] arm 18: persisting the pin failed"
    rr add -A
    rr commit -q -m pin
    commit_in_reuse work.txt "own work"
    rr switch -q main
    commit_in_reuse base.txt "main moves on"
    rr switch -q 094-reuse
    rr merge -q --no-ff -m "merge main" main
    honoured_reuse "pin honoured after main is merged into its branch"
    commit_in_reuse work.txt "more work"
    honoured_reuse "pin honoured after further work on its branch"
    # Re-persisting the same, still-valid pin keeps it byte-for-byte: HEAD
    # moving is not a reason to rewrite the tracked file.
    before="$(cksum < "${reuse}/.specify/feature.json")"
    persist_reuse specs/090-bundle || fail "[$mode] arm 18: re-persisting the pin failed"
    [[ "$(cksum < "${reuse}/.specify/feature.json")" == "$before" ]] \
        && pass "[$mode] re-persisting a still-valid pin left feature.json untouched" \
        || fail "[$mode] re-persisting a still-valid pin rewrote feature.json: $(cat "${reuse}/.specify/feature.json")"
    # A rewrite left uncommitted would stop arm 19's branch switches under set -e.
    rr checkout -q -- .specify/feature.json

    # 19. Disclosed false refusal: rebasing the branch onto a newer main moves
    #     it off the commit the pin recorded, so the pin is refused (fail
    #     closed) with the re-pin remedy; following that remedy restores it.
    rr switch -q main
    commit_in_reuse base.txt "main moves again"
    rr switch -q 094-reuse
    rr rebase -q main >/dev/null 2>&1 || { rr rebase --abort >/dev/null 2>&1 || true; }
    if ! rr merge-base --is-ancestor main 094-reuse; then
        fail "[$mode] arm 19 precondition: the rebase onto main did not complete"
    else
        refused_reuse "pin refused after its branch is rebased"
        # The remedy exactly as the refusal spells it. Its exit status is not
        # the point (the bundle has no plan.md); the re-pin it leaves is.
        (cd "$reuse" && env -u SPECIFY_FEATURE -u SPECIFY_INIT_DIR PATH="$P" \
            SPECIFY_FEATURE_DIRECTORY='specs/090-bundle' \
            .specify/scripts/bash/check-prerequisites.sh --json >/dev/null 2>&1) || true
        honoured_reuse "pin honoured again after the re-pin the refusal names"
    fi

    # 20. Disclosed false refusal: a pin written before the branch has a
    #     commit of its own, then the branch fast-forwarded to a newer main,
    #     is refused too (its pinned commit is now followed by a main commit).
    new_reuse_repo
    rr switch -q -c 094-reuse main
    persist_reuse specs/090-bundle || fail "[$mode] arm 20: persisting the pin failed"
    rr switch -q main
    commit_in_reuse base.txt "main moves on"
    rr switch -q 094-reuse
    rr merge -q --ff-only main
    refused_reuse "uncommitted pin refused after its branch is fast-forwarded"
done

if (( fails )); then
    echo "test-feature-pin: ${fails} arm(s) FAILED"
    exit 1
fi
echo "test-feature-pin: all arms passed"
