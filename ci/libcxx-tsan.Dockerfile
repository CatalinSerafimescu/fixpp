# syntax=docker/dockerfile:1
#
# fixpp-libcxx-tsan — reproducible builder for the TSan-instrumented libc++
# consumed by tier3-libcxx.yml (the linux-clang-libc++-tsan lane).
#
# WHY: the apt `libc++-22-dev` shipped to the Tier-3 runner is NOT TSan-
# instrumented, so TSan cannot see libc++'s out-of-line cv/mutex synchronisation
# and reports FALSE std::future/promise data races (ref #25; research findings
# 046-libcxx-tier3-and-006-lostwake.md). This image rebuilds libc++ + libc++abi
# + libunwind from the SAME LLVM release with -DLLVM_USE_SANITIZER=Thread,
# ABI-identical to stock (LIBCXX_ABI_VERSION=1, no unstable ABI) so the already-
# built test binaries load it with NO relink.
#
# ARTIFACT CARRIER: tier3-libcxx.yml `docker cp`s /libcxx-tsan out to the runner
# and prepends it to LD_LIBRARY_PATH ahead of the apt libc++. The consumer PINS
# THIS IMAGE BY DIGEST — a re-pushed tag must be manually verified, then the
# digest updated in tier3-libcxx.yml (see that file's "Fetch instrumented
# libc++" step). Republish via .github/workflows/publish-libcxx-tsan.yml.

ARG LLVM_VERSION=22.1.2
ARG TRIPLE=x86_64-unknown-linux-gnu

# Build on ubuntu:24.04 to match the Tier-3 runner's glibc ABI exactly.
FROM ubuntu:24.04 AS build
ARG LLVM_VERSION
ARG TRIPLE
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates wget gnupg lsb-release software-properties-common \
      git cmake ninja-build python3 build-essential \
 && rm -rf /var/lib/apt/lists/*

# Clang 22 via apt.llvm.org — the SAME toolchain (clang-22 / clang++-22) that
# tier3-libcxx.yml installs to compile the test TUs, so the instrumented
# runtime's ABI/name-mangling matches the objects that load it.
RUN wget -O /tmp/llvm.sh https://apt.llvm.org/llvm.sh \
 && chmod +x /tmp/llvm.sh \
 && /tmp/llvm.sh 22 \
 && rm -rf /var/lib/apt/lists/*

# Shallow-clone only the tagged release.
RUN git clone --depth 1 --branch "llvmorg-${LLVM_VERSION}" \
      https://github.com/llvm/llvm-project.git /llvm

# Build the runtimes SHARED + TSan-instrumented at the DEFAULT (stable) ABI, so
# the sonames are drop-in for the apt libc++.so.1 / libc++abi.so.1 the tests
# linked against. Per-target runtime dir forced ON → deterministic install path
# ${prefix}/lib/${TRIPLE}/ (matches the layout the consumer expects).
RUN cmake -G Ninja -S /llvm/runtimes -B /build \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_INSTALL_PREFIX=/opt/llvm-tsan \
      -DCMAKE_C_COMPILER=clang-22 \
      -DCMAKE_CXX_COMPILER=clang++-22 \
      -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi;libunwind" \
      -DLLVM_ENABLE_PER_TARGET_RUNTIME_DIR=ON \
      -DLLVM_USE_SANITIZER=Thread \
      -DLIBCXX_ABI_VERSION=1 \
      -DLIBCXX_ENABLE_STATIC=OFF    -DLIBCXX_ENABLE_SHARED=ON \
      -DLIBCXXABI_ENABLE_STATIC=OFF -DLIBCXXABI_ENABLE_SHARED=ON \
      -DLIBUNWIND_ENABLE_STATIC=OFF -DLIBUNWIND_ENABLE_SHARED=ON \
 && ninja -C /build cxx cxxabi unwind \
 && ninja -C /build install-cxx install-cxxabi install-unwind

# Stage only the shared objects (+ soname symlinks) the consumer LD_LIBRARY_PATHs.
RUN mkdir -p /libcxx-tsan \
 && cp -a /opt/llvm-tsan/lib/${TRIPLE}/libc++.so*    /libcxx-tsan/ \
 && cp -a /opt/llvm-tsan/lib/${TRIPLE}/libc++abi.so* /libcxx-tsan/ \
 && cp -a /opt/llvm-tsan/lib/${TRIPLE}/libunwind.so* /libcxx-tsan/ \
 && ls -l /libcxx-tsan

# ── Artifact carrier ─────────────────────────────────────────────────────────
# ubuntu:24.04 (not scratch) so `docker create` works without a CMD quirk and an
# operator can `docker run --rm <img>` to inspect the payload.
FROM ubuntu:24.04
ARG LLVM_VERSION
LABEL org.opencontainers.image.source="https://github.com/CatalinSerafimescu/fixpp"
LABEL org.opencontainers.image.description="TSan-instrumented libc++/libc++abi/libunwind (llvmorg-${LLVM_VERSION}) for the fixpp Tier-3 linux-clang-libc++-tsan lane. Artifact carrier: /libcxx-tsan is docker cp'd out and LD_LIBRARY_PATH-prepended ahead of the apt libc++."
COPY --from=build /libcxx-tsan /libcxx-tsan
CMD ["ls", "-l", "/libcxx-tsan"]
