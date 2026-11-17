#!/usr/bin/env bash
# Regression pin for the compressed, split sccache archive (103, tasks T160):
# ci/seed-sccache.sh writes a zstd-compressed tar split into parts plus a manifest,
# and ci/restore-sccache.sh checks the manifest, reassembles the parts in its order,
# tests and extracts them. Shims `oras` (a directory per tag stands in for GHCR)
# and `sccache`, and drives COPIES of the real scripts, so a mutant can be applied
# to a copy and never to the tree.
#
# Cells, on a fixture cache of incompressible files in nested directories:
#   single   one part: seed, then restore into a cache holding a sentinel → HIT,
#            the restored tree equals the fixture, the seed's size line keeps its
#            shape;
#   multi    several parts (SCCACHE_ARCHIVE_PART_BYTES, the seed's test seam,
#            set small) → HIT, the tree equals the fixture;
#   legacy   a tag holding only the pre-103 uncompressed `sccache-<preset>.tar`
#            → HIT by format detection, the tree equals the fixture;
#   pre103   the pre-103 restore script (ci/fixtures/sccache-pre-103/) pulling a
#            new archive → its "archive was missing" MISS, exit 0;
#   truncated / reordered  a part truncated, or two parts' contents swapped →
#            the manifest's MISS naming the part.
# Every MISS cell asserts exit 0, `hit=false`, its own MISS line (spelled out
# here, not read from the script) and that the sentinel in SCCACHE_DIR survives:
# nothing partial was swapped in.
#
# Then each mutant is applied to a fresh copy (a mutation that changes nothing is
# refused, by cmp) and its named cell must go RED:
#   M1 the restore reads only the first part (no reassembly)      → multi
#   M2 the seed pushes only the first part (a layer dropped)       → multi
#   M3 the restore's manifest check deleted                        → truncated, reordered
#   M4 the restore reads the legacy name as the new format         → legacy
# With M3 applied, zstd's own frame checksum still refuses a truncated or reordered
# archive, so the outcome stays a MISS and only its line changes: the manifest
# check names the part, and refuses a missing part before anything is decoded.
#
# The pre-103 fixture is the pair of scripts at 103's merge-base, the key file with one
# `claim-ok` marker added to a comment for the comment-claim lint; re-derive with
# `git show "$(git merge-base HEAD origin/main)":ci/restore-sccache.sh` (and the key
# file beside it), then `diff` against the fixture.
#
# Needs bash, coreutils, tar and zstd. No network, no build.
# Usage: bash ci/test-sccache-archive.sh
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PRESET=windows-msvc-asan
TOOLSET=14.44.35207
TAG="sccache-${PRESET}-${TOOLSET}"
IMAGE_REF="ghcr.io/catalinserafimescu/fixpp-sccache:${TAG}"
PREFIX="sccache-${PRESET}.tar.zst.part-"
MANIFEST="sccache-${PRESET}.tar.zst.manifest"
LEGACY="sccache-${PRESET}.tar"

for tool in zstd tar sha256sum split cmp; do
  command -v "$tool" >/dev/null 2>&1 || { echo "FAIL: $tool is not on PATH" >&2; exit 2; }
done

root="$(mktemp -d)"
trap 'rm -rf "$root"' EXIT
shims="$root/shims"
mkdir -p "$shims"

# Fake oras. `push <ref> [--artifact-type V] [--annotation V]... <file:type>...`
# copies the files into $FAKE_REGISTRY/<tag>/, replacing what was there; `pull
# <ref> -o <dir>` copies them out, and fails as a missing tag does when there is
# none. Any other argv is a SHIM-VIOLATION (exit 2), never a silent success.
cat > "$shims/oras" <<'SHIM'
#!/usr/bin/env bash
want="ghcr.io/catalinserafimescu/fixpp-sccache:"
case "${1:-}" in
  push)
    ref="${2:-}"; shift 2
    case "$ref" in "$want"*) ;; *) echo "SHIM-VIOLATION: oras push $ref" >&2; exit 2 ;; esac
    tag="${ref#"$want"}"; dest="$FAKE_REGISTRY/$tag"
    rm -rf "$dest"; mkdir -p "$dest"
    while [ "$#" -gt 0 ]; do
      case "$1" in
        --artifact-type|--annotation) shift 2 ;;
        -*) echo "SHIM-VIOLATION: oras push flag $1" >&2; exit 2 ;;
        *) f="${1%%:*}"; [ -f "$f" ] || { echo "SHIM-VIOLATION: oras push of missing $f" >&2; exit 2; }
           cp "$f" "$dest/"; shift ;;
      esac
    done ;;
  pull)
    ref="${2:-}"
    [ "${3:-}" = "-o" ] && [ -n "${4:-}" ] || { echo "SHIM-VIOLATION: oras $*" >&2; exit 2; }
    case "$ref" in "$want"*) ;; *) echo "SHIM-VIOLATION: oras pull $ref" >&2; exit 2 ;; esac
    src="$FAKE_REGISTRY/${ref#"$want"}"
    [ -d "$src" ] || { echo "Error: not found" >&2; exit 1; }
    mkdir -p "$4" && cp "$src"/* "$4"/ ;;
  *) echo "SHIM-VIOLATION: oras $*" >&2; exit 2 ;;
esac
SHIM
# Fake sccache: no server to stop; a server that starts.
cat > "$shims/sccache" <<'SHIM'
#!/usr/bin/env bash
case "${1:-}" in
  --stop-server) echo "sccache: error: couldn't connect to server" >&2; exit 2 ;;
  --zero-stats) exit 0 ;;
  *) echo "SHIM-VIOLATION: sccache $*" >&2; exit 2 ;;
esac
SHIM
chmod +x "$shims/oras" "$shims/sccache"

# The fixture cache: incompressible files in nested directories, large enough
# that a small part size gives several parts.
fixture="$root/fixture"
mkdir -p "$fixture/a/b" "$fixture/c"
head -c 70000 /dev/urandom > "$fixture/a/b/entry1"
head -c 50000 /dev/urandom > "$fixture/a/entry2"
head -c 30000 /dev/urandom > "$fixture/c/entry3"
printf 'small\n' > "$fixture/top"

# A scripts directory: copies of the three scripts plus a prune stub.
make_scripts() {
  local d="$1"
  mkdir -p "$d"
  cp "$repo_root/ci/seed-sccache.sh" "$repo_root/ci/restore-sccache.sh" \
     "$repo_root/ci/sccache-cache-key.sh" "$d/"
  printf '#!/usr/bin/env bash\necho "prune: stub"\n' > "$d/prune-sccache.sh"
  chmod +x "$d"/*.sh
}

# run_script <scripts-dir> <seed|restore> <case-dir> [VAR=value...]
# Runs the script with the shims first on PATH and a per-case environment; its
# output goes to <case-dir>/out, its status to <case-dir>/rc.
run_script() {
  local d="$1" which="$2" c="$3"; shift 3
  mkdir -p "$c/rt"
  : > "$c/output"; : > "$c/summary"
  set +e
  ( cd "$c" && env PATH="$shims:$PATH" VCToolsVersion="$TOOLSET" \
      SCCACHE_DIR="$c/cache" FAKE_REGISTRY="$c/registry" RUNNER_TEMP="$c/rt" \
      GITHUB_OUTPUT="$c/output" GITHUB_STEP_SUMMARY="$c/summary" "$@" \
      bash "$d/$which-sccache.sh" "$PRESET" ) > "$c/out" 2>&1
  echo $? > "$c/rc"
  set -e
}

# Checks; each prints its reason and returns 1.
want_rc()     { [ "$(cat "$1/rc")" = "$2" ] || { echo "  rc $(cat "$1/rc"), want $2"; return 1; }; }
want_line()   { grep -qF -- "$2" "$1/out" || { echo "  no line: $2"; return 1; }; }
want_hit()    { grep -qx "hit=$2" "$1/output" || { echo "  no hit=$2 in GITHUB_OUTPUT"; return 1; }; }
want_tree()   { diff -r "$fixture" "$1/cache" >/dev/null 2>&1 || { echo "  restored tree differs from the fixture"; return 1; }; }
want_sentinel() { [ -f "$1/cache/sentinel" ] || { echo "  the sentinel in SCCACHE_DIR is gone: a partial restore was swapped in"; return 1; }; }

# seed_into <scripts-dir> <case-dir> [VAR=value...]: seed from the fixture.
seed_into() {
  local d="$1" c="$2"; shift 2
  rm -rf "$c"; mkdir -p "$c"; cp -r "$fixture" "$c/cache"
  run_script "$d" seed "$c" "$@"
  want_rc "$c" 0 && want_line "$c" "sccache-cache SEEDED \`$TAG\`"
}
# fresh_cache <case-dir>: an SCCACHE_DIR holding only the sentinel.
fresh_cache() { rm -rf "$1/cache"; mkdir -p "$1/cache"; printf 'sentinel\n' > "$1/cache/sentinel"; }
nparts() { ls "$1/registry/$TAG" | grep -c "^${PREFIX//./\\.}[0-9][0-9][0-9]\$" || true; }

cell_single() {
  local d="$1" c="$root/case-single"
  seed_into "$d" "$c" || return 1
  [ "$(nparts "$c")" = 1 ] || { echo "  $(nparts "$c") parts, want 1"; return 1; }
  [ -f "$c/registry/$TAG/$MANIFEST" ] || { echo "  no manifest pushed"; return 1; }
  grep -Eq "^sccache-cache archive \`$TAG\` — [^ ]+ archive, [^ ]+ on disk \(cap \`[^\`]+\`\)\$" "$c/out" \
    || { echo "  the size line lost its shape"; return 1; }
  fresh_cache "$c"; run_script "$d" restore "$c"
  want_rc "$c" 0 && want_hit "$c" true && want_line "$c" "sccache-cache HIT  \`$TAG\`" && want_tree "$c"
}

cell_multi() {
  local d="$1" c="$root/case-multi"
  seed_into "$d" "$c" SCCACHE_ARCHIVE_PART_BYTES=40000 || return 1
  [ "$(nparts "$c")" -ge 3 ] || { echo "  $(nparts "$c") parts, want at least 3"; return 1; }
  fresh_cache "$c"; run_script "$d" restore "$c"
  want_rc "$c" 0 && want_hit "$c" true && want_line "$c" "sccache-cache HIT  \`$TAG\`" && want_tree "$c"
}

cell_legacy() {
  local d="$1" c="$root/case-legacy"
  rm -rf "$c"; mkdir -p "$c/registry/$TAG"
  tar -cf "$c/registry/$TAG/$LEGACY" -C "$fixture" .
  fresh_cache "$c"; run_script "$d" restore "$c"
  want_rc "$c" 0 && want_hit "$c" true && want_line "$c" "sccache-cache HIT  \`$TAG\`" && want_tree "$c"
}

# A registry holding a multi-part archive, written by the in-tree seed.
multi_registry() {
  local c="$1"
  seed_into "$root/scripts-clean" "$c" SCCACHE_ARCHIVE_PART_BYTES=40000 >/dev/null || return 1
}

cell_pre103() {
  local c="$root/case-pre103" d="$root/scripts-pre103"
  multi_registry "$c" || { echo "  setup: seed failed"; return 1; }
  rm -rf "$d"; mkdir -p "$d"
  cp "$repo_root/ci/fixtures/sccache-pre-103/restore-sccache.sh" \
     "$repo_root/ci/fixtures/sccache-pre-103/sccache-cache-key.sh" "$d/"
  [ -s "$d/restore-sccache.sh" ] && [ -s "$d/sccache-cache-key.sh" ] || { echo "  the pre-103 fixture is missing"; return 1; }
  fresh_cache "$c"; run_script "$d" restore "$c"
  want_rc "$c" 0 && want_hit "$c" false && want_sentinel "$c" \
    && want_line "$c" "sccache-cache MISS \`$TAG\` — pulled, but the archive was missing or did not extract cleanly."
}

cell_truncated() {
  local d="$1" c="$root/case-truncated" p
  multi_registry "$c" || { echo "  setup: seed failed"; return 1; }
  p="$c/registry/$TAG/${PREFIX}001"
  head -c 1000 "$p" > "$p.short" && mv "$p.short" "$p"
  fresh_cache "$c"; run_script "$d" restore "$c"
  want_rc "$c" 0 && want_hit "$c" false && want_sentinel "$c" \
    && want_line "$c" "sccache-cache MISS \`$TAG\` — pulled, but part ${PREFIX}001 does not match the manifest. Treated as no cache; compiling from scratch."
}

cell_reordered() {
  local d="$1" c="$root/case-reordered" r
  multi_registry "$c" || { echo "  setup: seed failed"; return 1; }
  r="$c/registry/$TAG"
  mv "$r/${PREFIX}000" "$r/swap" && mv "$r/${PREFIX}001" "$r/${PREFIX}000" && mv "$r/swap" "$r/${PREFIX}001"
  fresh_cache "$c"; run_script "$d" restore "$c"
  want_rc "$c" 0 && want_hit "$c" false && want_sentinel "$c" \
    && want_line "$c" "sccache-cache MISS \`$TAG\` — pulled, but part ${PREFIX}000 does not match the manifest. Treated as no cache; compiling from scratch."
}

failures=0
run_cell() {
  local name="$1" d="$2" why
  if why="$("cell_$name" "$d" 2>&1)"; then echo "ok   $name"; return 0; fi
  echo "FAIL $name"; printf '%s\n' "$why"; return 1
}

make_scripts "$root/scripts-clean"
for cell in single multi legacy pre103 truncated reordered; do
  run_cell "$cell" "$root/scripts-clean" || failures=$((failures + 1))
done

# mutate <id> <file> <old> <new> [<old> <new>...]: a fresh copy with each exact
# replacement made; a replacement whose text is not found exactly once is refused,
# and so is a mutant that leaves the file byte-identical.
mutate() {
  local id="$1" file="$2" d="$root/scripts-$1"; shift 2
  rm -rf "$d"; make_scripts "$d"
  cp "$d/$file" "$d/$file.orig"
  while [ "$#" -ge 2 ]; do
    OLD="$1" NEW="$2" python3 - "$d/$file" <<'PY' || return 1
import os, sys
p = sys.argv[1]; s = open(p).read(); old = os.environ["OLD"]
if s.count(old) != 1:
    sys.exit(f"mutant anchor found {s.count(old)} times in {p}")
open(p, "w").write(s.replace(old, os.environ["NEW"]))
PY
    shift 2
  done
  if cmp -s "$d/$file" "$d/$file.orig"; then echo "mutant $id changed nothing" >&2; return 1; fi
  echo "$d"
}

# expect_red <id> <cell...>: each named cell must fail on the mutant.
expect_red() {
  local id="$1" d="$2"; shift 2
  for cell in "$@"; do
    if why="$("cell_$cell" "$d" 2>&1)"; then
      echo "FAIL mutant $id: cell $cell stayed GREEN"; failures=$((failures + 1))
    elif printf '%s\n' "$why" | grep -q 'setup:'; then
      echo "FAIL mutant $id: cell $cell failed in its setup, not at the property"; printf '%s\n' "$why"
      failures=$((failures + 1))
    else
      echo "ok   mutant $id: cell $cell RED:$(printf '%s' "$why" | head -1)"
    fi
  done
}

# M1: the restore tests and extracts the first part only.
if d="$(mutate M1 restore-sccache.sh \
      'cat "${parts[@]}" | zstd -q -t' 'cat "${parts[0]}" | zstd -q -t' \
      'cat "${parts[@]}" | zstd -q -dc | tar -xf - -C "$STAGE"' \
      'cat "${parts[0]}" | zstd -q -dc | tar -xf - -C "$STAGE"')"; then
  expect_red M1 "$d" multi
else
  echo "FAIL mutant M1 did not apply"; failures=$((failures + 1))
fi

if d="$(mutate M2 seed-sccache.sh \
      'for f in "${parts[@]}"; do push_files+=("$f:application/zstd"); done' \
      'push_files+=("${parts[0]}:application/zstd")')"; then
  expect_red M2 "$d" multi
else
  echo "FAIL mutant M2 did not apply"; failures=$((failures + 1))
fi

if d="$(mutate M3 restore-sccache.sh \
      '    if [ "$(sha256sum < "$WORK/$name" | cut -d'"' '"' -f1)" != "$digest" ]; then
      echo "part $name does not match the manifest"; return 1
    fi
' '')"; then
  expect_red M3 "$d" truncated reordered
else
  echo "FAIL mutant M3 did not apply"; failures=$((failures + 1))
fi

if d="$(mutate M4 restore-sccache.sh \
      'tar -xf "$WORK/$SCCACHE_ARCHIVE_LEGACY" -C "$STAGE"' \
      'zstd -q -dc "$WORK/$SCCACHE_ARCHIVE_LEGACY" | tar -xf - -C "$STAGE"')"; then
  expect_red M4 "$d" legacy
else
  echo "FAIL mutant M4 did not apply"; failures=$((failures + 1))
fi

if [ "$failures" -ne 0 ]; then
  echo "test-sccache-archive: $failures failure(s)" >&2
  exit 1
fi
echo "test-sccache-archive: all cells and mutants as expected"
