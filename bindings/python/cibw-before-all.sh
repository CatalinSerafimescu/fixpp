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
# install + import + functional subset (3.10-3.13). The tier1 `python-bindings`
# legs are a TEST VEHICLE — not a byte of them ships — and the
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
