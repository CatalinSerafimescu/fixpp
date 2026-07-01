#!/usr/bin/env bash
# build-wheel.sh — build the single fixpp abi3 wheel via cibuildwheel from a
# PRISTINE git worktree of committed HEAD.
#
# Why a worktree: cibuildwheel copies the whole *current working directory* into
# the manylinux container with a blind `tar -c .` (oci_container.py:copy_into) —
# it honours no .gitignore / .dockerignore and cannot be told to exclude
# anything, and it requires package_dir to live under cwd. Because
# pyproject's `cmake.source-dir = "../.."` forces cwd to be the submodule root,
# a plain `cibuildwheel bindings/python` from the root sweeps the multi-GB
# build/ + .codegraph/ trees into the container (once ~100G in seconds).
#
# A detached worktree contains only tracked files; build/ and .codegraph/ are
# gitignored and therefore physically ABSENT, so the tar is ~tens of MB. Builds
# committed HEAD — commit your work before running. Wheel(s) land in
# <submodule-root>/wheelhouse/.
#
# All cibuildwheel configuration (build selector, image, before-all toolchain
# setup, the Conan toolchain-file handoff) lives in bindings/python/pyproject.toml
# [tool.cibuildwheel] — the single source shared with the tier1.yml CI gate.
set -euo pipefail

ROOT="$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)"
WT="$(mktemp -d "${TMPDIR:-/tmp}/fixpp-wheel-wt.XXXXXX")"
cleanup() { git -C "$ROOT" worktree remove --force "$WT" 2>/dev/null || rm -rf "$WT"; }
trap cleanup EXIT

git -C "$ROOT" worktree add --detach "$WT" HEAD >&2

( cd "$WT" && cibuildwheel --platform linux bindings/python )

mkdir -p "$ROOT/wheelhouse"
cp -v "$WT"/wheelhouse/*.whl "$ROOT/wheelhouse/"
echo "wheel(s) in $ROOT/wheelhouse/" >&2
