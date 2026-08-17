#!/usr/bin/env bash
# cibuildwheel CIBW_BEFORE_ALL — runs ONCE inside the manylinux_2_28 container
# before any wheel build. Sets up the C++23 toolchain + Conan dependency tree and
# writes the Conan CMake toolchain to /tmp/wheel-conan, which the build step
# consumes via config-settings cmake.define.CMAKE_TOOLCHAIN_FILE (pyproject
# [tool.cibuildwheel.config-settings]). $1 = cibuildwheel {project} (the engine
# source root, where conanfile.py lives; the binding's cmake.source-dir=../..).
#
# The exact recipe was proven by a configure-only probe in the pinned image
# (research D-2/D-7): gcc-toolset-14 (gcc 14.2 — C++23/std::expected), SWIG 4.4
# from the build-frontend, Python3 Development.SABIModule found → real abi3 link.
#
# ⚠️ THE WHEEL DOES NOT WRAP THE BINARIES FROM THE CI clang/gcc BUILDS. It
# compiles the engine from source, here, and differs from every leg that runs the
# Python test suite in THREE dimensions at once:
#
#            shipped wheel (this script)   | Tier 1 legs running the py tests
#   compiler gcc-toolset-14                | clang 22
#   build    Release                       | Debug (+ asan/ubsan/tsan)
#   OTel     with_otel=False  (below)      | with_otel default True (conanfile.py)
#
# So the artifact users actually run is exercised only by python-wheel-test's
# install + import + functional subset (3.10-3.13). The tier1 legs that run the
# Python test suite — the six `linux` matrix legs since #254, previously a
# separate `python-bindings` job — are a TEST VEHICLE, not a byte of them ships,
# and the
# packages-linux-{clang,gcc}-release artifacts are the C++ deliverable and carry
# no Python at all. See L-056-4 in spec/behaviors-and-limitations.md.
set -euo pipefail
PROJECT="${1:?cibw-before-all: project root argument required}"

# openssl 3.x ./Configure needs these perl modules; manylinux_2_28 ships none of
# them (build() fails at IPC::Cmd / Time::Piece otherwise). perl-FindBin is part
# of perl-core — naming it separately makes dnf fail (it is not a package).
dnf install -y -q perl-core perl-IPC-Cmd perl-Data-Dumper perl-Time-Piece

GT=/opt/rh/gcc-toolset-14/root/usr/bin   # gcc-toolset-14 toolchain binaries
PY=/opt/python/cp310-cp310/bin           # a manylinux CPython for pip/conan
export PATH="$PY:$PATH"

# ── ccache (#259) ────────────────────────────────────────────────────────────
#
# This lane compiled its whole ninja graph uncached on every run — 68 min of a
# 69 min job, and (with #241's coverage cache landed) Tier 1's critical path.
#
# ⚠️ NOT `dnf install ccache`, AND THE REASON IS MEASURED, NOT STYLISTIC.
# EPEL *is* enabled in this image and the package *does* install — which is what
# makes it the tempting choice — but EL8 ships **ccache 3.7.7** (2019).
# `ci/ccache-stats.sh` opens with `ccache --print-stats`, a 4.x-only feature, and
# every counter it parses is 4.x machine-readable output; 3.7.7 also predates
# zstd, so the `CCACHE_COMPRESSLEVEL` every other lane sets would be inert. The
# EPEL route does not degrade quietly — it fails the reporting step outright.
# (Probed in this exact pinned image on 2026-08-17.)
#
# The static build is chosen over the `-glibc` one deliberately. Both were run
# here and both work against this image's glibc 2.28 — the static one simply
# cannot be broken by a future image bump, and a compiler cache must NEVER
# redden a lane whose build and tests pass.
#
# Version and digest are pinned and the digest is VERIFIED before the binary is
# unpacked: this downloads an executable into the container that builds the
# shipped wheel.
CCACHE_VERSION=4.13.6
CCACHE_TARBALL="ccache-${CCACHE_VERSION}-linux-x86_64-musl-static.tar.xz"
CCACHE_SHA256=156ec57c5198cc849d92834023d09910b83dc5504c6cf405d09e6ae7b208a3e5

(
  cd /tmp
  curl -sSLf -o "$CCACHE_TARBALL" \
    "https://github.com/ccache/ccache/releases/download/v${CCACHE_VERSION}/${CCACHE_TARBALL}"
  echo "${CCACHE_SHA256}  ${CCACHE_TARBALL}" | sha256sum -c -
  tar xf "$CCACHE_TARBALL"
  install -m 0755 "ccache-${CCACHE_VERSION}-linux-x86_64-musl-static/ccache" /usr/local/bin/ccache
)
# Fail loudly HERE if the binary cannot run, rather than at the first compile:
# a missing launcher makes every compiler invocation exit 127, which surfaces as
# a confusing whole-build failure far from its cause (the #177 / exit-127 shape).
ccache --version | head -1
echo "ccache: $(command -v ccache)"

pip install -q "conan>=2"                 # swig/ninja/scikit-build-core come from
                                          # the build-frontend's build-requires
conan profile detect --force

# Drive the engine build with gcc-toolset-14 (compiler_executables bakes the
# compiler into the toolchain so scikit-build-core's cmake uses it without
# sourcing conanbuild.sh); with_otel=False + static OpenSSL → self-contained .so
# (paired with -DFIXPP_BUILD_OTEL=OFF in pyproject cmake.define). user_presets=
# stops Conan writing CMakeUserPresets.json into the (copied) source tree.
conan install "$PROJECT" -of /tmp/wheel-conan \
  -s compiler=gcc -s compiler.version=14 -s compiler.cppstd=23 \
  -s compiler.libcxx=libstdc++11 -s build_type=Release \
  -c "tools.build:compiler_executables={\"c\":\"$GT/gcc\",\"cpp\":\"$GT/g++\"}" \
  -c tools.cmake.cmaketoolchain:user_presets= \
  -o "fixpp/*:with_otel=False" -o "openssl/*:shared=False" \
  --build=missing
