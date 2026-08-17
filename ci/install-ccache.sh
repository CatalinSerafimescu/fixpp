#!/usr/bin/env bash
# Install the PINNED ccache build (#259). Used by BOTH sides of the wheel lane:
#
#   host      .github/workflows/tier1.yml, before restore/stats/seed
#   container bindings/python/cibw-before-all.sh, before the wheel compile
#
#   ci/install-ccache.sh [<prefix>]     # default /usr/local/bin
#
# ── WHY ONE SCRIPT AND NOT TWO COPIES OF A VERSION STRING ────────────────────
#
# The host and the container share ONE bind-mounted CCACHE_DIR. If they run
# different ccache versions they can disagree about that directory's contents,
# and the failure is silent in the direction that matters: a host-side
# `--print-stats` that cannot read a container-written cache reports zero
# cacheable calls, which is INDISTINGUISHABLE from "the cache didn't help" —
# exactly the conclusion #259 exists to change.
#
# Pinning the version in one file makes the two sides equal BY CONSTRUCTION
# rather than by two comments agreeing. It also removes the reason the stats
# have to be read inside the container: the concern there was a version split
# (Ubuntu's ccache 4.x against EL8's EPEL 3.7.7), and there is no split left.
#
# ⚠️ NOT the distro package on the container side. EPEL IS enabled in
# manylinux_2_28 and `dnf install ccache` DOES succeed — which is what makes it
# the tempting choice — but EL8 ships ccache 3.7.7 (2019): no `--print-stats`
# (ci/ccache-stats.sh's first call), and no zstd, so CCACHE_COMPRESSLEVEL is
# inert. Probed in the pinned image 2026-08-17.
#
# ⚠️ The MUSL-STATIC build, not the -glibc one. Both were run in the pinned
# image and both work against its glibc 2.28; the static build additionally
# cannot be broken by a future image bump, and a compiler cache must never
# redden a lane whose build and tests pass.
#
# The digest is VERIFIED before the archive is unpacked — this puts an
# executable into the container that builds the shipped wheel.
set -euo pipefail

PREFIX="${1:-/usr/local/bin}"

# ⚠️ Bumping these two lines is a deliberate act: it changes the compiler cache
# on BOTH sides at once. Take the version and the checksum from the same
# ccache release; do not update one without the other.
CCACHE_VERSION=4.13.6
CCACHE_SHA256=156ec57c5198cc849d92834023d09910b83dc5504c6cf405d09e6ae7b208a3e5

NAME="ccache-${CCACHE_VERSION}-linux-x86_64-musl-static"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

curl -sSLf -o "$WORK/${NAME}.tar.xz" \
  "https://github.com/ccache/ccache/releases/download/v${CCACHE_VERSION}/${NAME}.tar.xz"
echo "${CCACHE_SHA256}  $WORK/${NAME}.tar.xz" | sha256sum -c -
tar xf "$WORK/${NAME}.tar.xz" -C "$WORK"
install -m 0755 "$WORK/${NAME}/ccache" "$PREFIX/ccache"

# Fail LOUDLY here rather than at the first compile. A launcher that is not on
# PATH makes every compiler invocation exit 127, which surfaces as a confusing
# whole-build failure far from its cause — the shape recorded at tier1.yml's
# ccache step and fixed once already on Tier 3 (#177).
"$PREFIX/ccache" --version | head -1
echo "ccache installed at $PREFIX/ccache"
