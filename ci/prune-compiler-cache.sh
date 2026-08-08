#!/usr/bin/env bash
# Delete dead GHCR versions of a COMPILER-CACHE package, keeping only $CURRENT_TAG.
#
#   ci/prune-compiler-cache.sh <pkg> <label> <tag-regex> <current-tag>
#
# e.g.
#   ci/prune-compiler-cache.sh fixpp-sccache windows-msvc-debug \
#       '^sccache-windows-msvc-debug-' sccache-windows-msvc-debug-14.44.35207
#   DRY_RUN=1 ci/prune-compiler-cache.sh fixpp-ccache linux-clang-libc++ \
#       '^ccache-linux-clang-libcxx-clang([0-9]+|unknown)-[0-9a-f]{8}$' ''
#
# Callers normally reach this through a thin wrapper that knows how to build the
# regex from a preset — ci/prune-sccache.sh and ci/prune-ccache.sh. Both exist so
# the documented standalone command stays short, and so the tag grammar lives
# next to the key script that mints it.
#
# Passing an empty CURRENT_TAG keeps nothing — use only when reclaiming a preset
# wholesale. Any non-empty value is protected.
#
# ── ⚠️ THE REGEX MUST BE ANCHORED, AND THAT IS ENFORCED ──────────────────────
#
# This script used to be sccache-only and matched with
# `startswith("sccache-\($preset)-")`. That is safe on Tier 2 BY ACCIDENT of
# naming: no MSVC preset is a prefix of another (windows-msvc-{debug,release,
# asan}). It is NOT safe in general, and it is specifically unsafe for the
# libc++ presets this was generalised for — `linux-clang-libcxx` IS a prefix of
# `linux-clang-libcxx-asan`, so a prefix match run for the plain lane would
# classify all three sanitizer lanes' live caches as "mine" and delete them.
#
# So the discrimination moved OUT of here, to the caller that knows the tag
# grammar. What this script enforces is that the regex is anchored at BOTH ends
# — a caller who forgets either gets a refusal, not a substring match.
#
# ⚠️ An earlier draft enforced `^` only, reasoning that "it cannot check the END
# anchor, because the two tag grammars legitimately differ". That is a
# non-sequitur, and it is recorded because it is an easy one to re-derive:
# differing grammars prevent this script from requiring a particular SHAPE, not
# from requiring a terminal `$`. Each caller states its own end — sccache's
# toolset is dotted-numeric (`[0-9.]+$`), ccache's is `clangNN-<sha8>` — and
# neither needs this file to know which.
#
# The trade is stated rather than hidden: end-anchoring converts possible
# over-DELETION into possible silent under-deletion if a tag grammar later
# changes without its regex following. That is why `ccache_tag_regex` lives
# beside the minting expression in ci/ccache-cache-key.sh, and why
# ci/test-ccache-scripts.sh derives its superseded fixture from a REAL minted
# tag rather than a hardcoded one.
#
# Verified safe for Tier 2 at the time of the change by MEASUREMENT, not
# reasoning: all three live fixpp-sccache tags
# (`sccache-windows-msvc-{asan,debug,release}-14.44.35207`) match the
# end-anchored form, so no existing version stopped being classified as
# reclaimable.
#
# ── ⚠️ WHETHER THIS DELETES FROM CI DEPENDS ENTIRELY ON THE TOKEN ────────────
#
# Deleting a package VERSION needs `delete:packages`. `GITHUB_TOKEN`'s
# `packages: write` is read+write only and does NOT carry it, so under the
# fallback token this script deletes nothing and reports `prune: PENDING <n>`.
# Since 2026-08-06 the seed steps pass `secrets.GHCR_PAT || secrets.GITHUB_TOKEN`
# and a classic PAT with `delete:packages` + `read:packages` is stored as
# `GHCR_PAT`, so **CI does delete when that secret is present**. Do not read the
# absence of a local prune as proof that nothing in CI can remove a version.
#
# Why this mattered enough to build: MEASURED on the sibling package —
# fixpp-conan-cache's three MSVC profiles each held TWO tags (2026-08-02 and
# 2026-08-03), the older having survived a seed whose prune ran, while every
# Linux profile held exactly ONE because those are seeded locally with a
# delete-scoped `gh`. That asymmetry costs Conan almost nothing: its tag is
# content-hashed, so each push mints a NEW tag and orphans no manifest (zero
# untagged versions there). A compiler-cache tag is ROLLING, so every republish
# orphans an untagged version of several GB. Left unreclaimed it accumulates
# indefinitely — and Tier 3 republishes FOUR legs per push:main.
#
# Runnable standalone precisely because the CI path is conditional on a secret:
# the seed scripts report any pending backlog into the job summary so it is
# visible rather than assumed, and a maintainer can drain it by hand.
#
# BEST-EFFORT: never exits non-zero. Prints a machine-greppable
# `prune: PENDING <n>` line when deletes were refused.
set -uo pipefail
PKG="${1:?usage: prune-compiler-cache.sh <pkg> <label> <tag-regex> <current-tag>}"
LABEL="${2:?usage: prune-compiler-cache.sh <pkg> <label> <tag-regex> <current-tag>}"
TAG_RE="${3:?usage: prune-compiler-cache.sh <pkg> <label> <tag-regex> <current-tag>}"
CURRENT_TAG="${4-}"

# Route missing tools through the SAME `prune: PENDING` marker as every other
# non-deletion outcome. A bare "skipping" line is invisible to the caller, which
# greps for that marker to write the backlog note into the job summary — so a
# runner image that stopped shipping jq would silently disable reclamation while
# every seed still reported success.
command -v jq >/dev/null 2>&1 || { echo "prune: PENDING ? — jq not found; nothing was examined, let alone deleted"; exit 0; }
command -v gh >/dev/null 2>&1 || { echo "prune: PENDING ? — gh not found; nothing was examined, let alone deleted"; exit 0; }

case "$TAG_RE" in
  '^'*) : ;;
  *) echo "prune: PENDING ? — tag regex '$TAG_RE' is not anchored at '^'; refusing to classify by substring (see this script's header for the libcxx prefix collision that makes this fatal)"; exit 0 ;;
esac
case "$TAG_RE" in
  *'$') : ;;
  *) echo "prune: PENDING ? — tag regex '$TAG_RE' is not anchored at '\$'; a start-only anchor still matches a LONGER preset's tags, which is how pruning one lane deletes a sibling lane's live cache. Nothing was examined."; exit 0 ;;
esac

# SAFE UNDER A STATED CONTRACT, mirroring prune-conan-cache.sh:
#  - an UNTAGGED version is unreachable *through this repo's cache contract*,
#    which is tag-only: the restore scripts pull `$IMAGE:$TAG` and nothing here
#    ever pulls by digest. It is NOT unreachable in general — an OCI manifest
#    can always be pulled by `@sha256:…`, so anyone who pins a digest by hand
#    (e.g. parking a known-good cache for rollback) is outside the contract and
#    this script will reclaim it. Do not pin a digest against these packages.
#  - a TAGGED version is deleted only when EVERY one of its tags matches
#    $TAG_RE AND none of them is $CURRENT_TAG. A version sharing a tag with
#    anything else is left alone.
#
# FAIL CLOSED on anything unexpected. `.metadata.container.tags // []` silently
# turned a MISSING tags field into a proven-empty one, i.e. into "untagged, safe
# to delete" — which is the one classification that bypasses the $CURRENT_TAG
# guard. A degraded or reshaped API response could therefore delete the live
# cache. Absence of evidence is not evidence of absence: if any version does not
# carry a tags ARRAY, or if $CURRENT_TAG is not found exactly once, this script
# deletes nothing and says so.
# List and parse as SEPARATE steps. Piping the api straight into jq with
# `2>/dev/null` makes a failed call indistinguishable from a clean package: both
# yield an empty DEAD, the loop below prints nothing, and the caller records
# `pending=0`. MEASURED on run 31121588649 — this step printed not one line on
# either seeded leg while the package was in fact carrying two orphaned
# multi-GB versions, which a local run of this same script then deleted. A
# reclaim path that reports "nothing to do" when it could not even ask is worse
# than one that reports a backlog.
# ⚠️ NO LEADING SLASH ON ANY `gh api` ENDPOINT HERE. Under Git Bash on the
# windows runner, MSYS rewrites a leading-slash argument into a Windows path, so
# `/user/packages/...` reached gh as
# `C:/Program Files/Git/user/packages/container/fixpp-sccache/versions/<id>` and
# every DELETE was refused with `invalid API endpoint`. gh's own error says to
# omit the leading slash; the slash-free form is what its docs use anyway.
#
# The conversion is SHAPE-DEPENDENT, which is what made this so misleading: this
# LIST carries `?per_page=100` and was NOT converted, so listing and classifying
# worked perfectly while every delete failed. The result looked exactly like a
# permissions problem — and was misdiagnosed as one twice. Same MSYS behaviour
# `winpath()` in ci/conan-cache-key.sh already documents ("sometimes mangles such
# arguments and sometimes does not; it depends on the argument's shape").
if ! RAW=$(gh api --paginate "user/packages/container/$PKG/versions?per_page=100" 2>&1); then
  echo "prune: PENDING ? — could not LIST $PKG versions; nothing was examined, let alone deleted"
  # gh prints a one-line diagnosis AND a pretty-printed JSON body; the first
  # line is often just `{`, so collapse and truncate rather than head -1.
  echo "prune: (api said) $(printf '%s' "$RAW" | tr -s '\n\r\t ' ' ' | cut -c1-200)"
  exit 0
fi

bail() { echo "prune: PENDING ? — $1; nothing was deleted"; exit 0; }

# FLATTEN THE PAGES. `gh api --paginate` emits each page as its own top-level
# JSON array, not one merged array, and this endpoint pages at 30 by default.
# The streaming `.[]` filter below tolerates that, but the two scalar guards do
# NOT: they would emit one count per page ("0\n0"), every `[ "$x" = "0" ]`
# comparison would fail, and the script would fail closed FOREVER the moment the
# package exceeded one page — locking out the very cleanup that shrinks it.
# `jq -s add` rather than `gh --slurp` so this does not depend on the gh version.
# VALIDATE THE SHAPE BEFORE FLATTENING. `jq -s 'add // []'` maps empty output,
# `null` and `{}` all onto `[]` — so a SUCCESSFUL-but-malformed response (a
# truncated body, an API shape change) sailed past the `-n "$VERSIONS"` check as
# a legitimately empty package and rendered "nothing to prune". A zero exit from
# `gh` is not evidence the body was what we think it is.
# `-e` makes jq's exit status follow the expression's value, so false → bail.
printf '%s' "$RAW" | jq -se 'length > 0 and all(.[]; type == "array")' >/dev/null 2>&1 \
  || bail "the LIST response was not one-or-more JSON arrays (empty, null, or an unexpected shape)"

VERSIONS=$(printf '%s' "$RAW" | jq -s 'add // []' 2>/dev/null)
[ -n "$VERSIONS" ] || bail "could not parse the version list as JSON"

# Guard 1: every version must carry a real tags array. No `// []` fallback.
MALFORMED=$(printf '%s' "$VERSIONS" \
  | jq '[ .[] | select((.metadata.container.tags | type) != "array") ] | length' 2>/dev/null)
[ -n "$MALFORMED" ] || bail "could not parse the version list as JSON"
[ "$MALFORMED" = "0" ] || bail "$MALFORMED version(s) carry no tags ARRAY — the API response is not the shape this script classifies on"

# Guard 2: the tag we are protecting must be present exactly once. Zero means we
# are about to prune around a keep-tag that does not exist — the caller computed
# a different key than the one that was published, and every "dead" version in
# that listing is in fact the live cache. Two means the invariant is already
# broken and a human should look. An EMPTY CURRENT_TAG is the documented
# reclaim-a-preset-wholesale mode and skips this guard by design.
if [ -n "$CURRENT_TAG" ]; then
  KEEP=$(printf '%s' "$VERSIONS" \
    | jq --arg cur "$CURRENT_TAG" '[ .[] | select(.metadata.container.tags | index($cur)) ] | length' 2>/dev/null)
  [ "$KEEP" = "1" ] || bail "keep-tag '$CURRENT_TAG' matched ${KEEP:-?} versions, expected exactly 1"
fi

# Status-checked like every other read here. An unchecked `mapfile < <(jq …)`
# turns a jq failure into an empty DEAD, a loop that prints nothing and a
# `pending=0` the caller reports as clean — the same silent-empty that let this
# package accumulate 1.3 GB of orphans unnoticed. Guards 1 and 2 make a failure
# here unlikely, not impossible.
#
# ⚠️ `test($re)` REPLACED `startswith("sccache-\($preset)-")`. An INVALID regex
# makes jq abort rather than return false, so a malformed pattern lands in the
# bail below — which is the direction we want. It cannot silently match nothing
# and report a clean package.
if ! DEADTXT=$(
  printf '%s' "$VERSIONS" \
  | jq -r --arg re "$TAG_RE" --arg cur "$CURRENT_TAG" '
      .[]
      | . as $v
      | ($v.metadata.container.tags) as $tags
      | (($tags | length) == 0)                  as $untagged
      | ([ $tags[] | test($re) ] | all)          as $mine
      | (($tags | index($cur)) != null)          as $is_current
      | select(($is_current | not) and ($untagged or $mine))
      | "\($v.id)\t\(if $untagged then "<untagged>" else ($tags | join(",")) end)"'
); then
  bail "could not classify the version list (jq failed — check that '$TAG_RE' is a valid regex)"
fi
mapfile -t DEAD <<< "$DEADTXT"

deleted=0 pending=0
for row in "${DEAD[@]:-}"; do
  [ -z "$row" ] && continue
  vid="${row%%$'\t'*}"; tags="${row#*$'\t'}"
  if [ "${DRY_RUN:-0}" = "1" ]; then
    echo "prune (dry-run): would delete version $vid  tags=[$tags]"
    continue
  fi
  # PRINT WHAT THE API ACTUALLY SAID. The previous shape discarded the response
  # (`>/dev/null 2>&1`) and every failure printed the fixed string "token lacks
  # delete:packages" — a GUESS presented as a diagnosis. It happened to be right
  # once, which is worse than being wrong: when the token was re-issued on
  # 2026-08-06 and the DELETEs still failed, the log said the same sentence, so
  # the second cause was unrecoverable from CI and the fix was declared resolved
  # while the package kept accumulating orphans. A diagnostic that cannot be
  # contradicted by the thing it describes is not a diagnostic.
  # NO LEADING SLASH — see the note above `RAW=` for why. This is the call that
  # was actually being mangled on the windows runner.
  if out=$(gh api --method DELETE "user/packages/container/$PKG/versions/$vid" 2>&1); then
    echo "prune: deleted version $vid  tags=[$tags]"
    deleted=$((deleted + 1))
  elif printf '%s' "$out" | grep -qE '"status": *"404"|Package version not found'; then
    # ALREADY GONE IS SUCCESS, NOT A REFUSAL. The matrix legs run in PARALLEL
    # and each prunes the shared pool of untagged versions, so they routinely
    # list the same orphan and race to delete it. The losers get 404. Counting
    # that as PENDING would make every successful cleanup report "dead
    # version(s) could not be reclaimed" on most legs — an instrument crying
    # wolf, which is the failure mode this script exists to end.
    # Safe to treat as reclaimed: we listed this id moments ago, so a 404 now
    # means something else removed it, which is the outcome we wanted.
    echo "prune: already gone $vid  tags=[$tags] (deleted by a concurrent leg)"
  else
    # Same collapse-and-truncate as the LIST failure above: gh prints a one-line
    # diagnosis followed by a pretty-printed JSON body, so the first line is
    # often just `{`.
    echo "prune: REFUSED version $vid  tags=[$tags] — $(printf '%s' "$out" | tr -s '\n\r\t ' ' ' | cut -c1-200)"
    pending=$((pending + 1))
  fi
done

[ "$deleted" -gt 0 ] && echo "prune: deleted $deleted"
if [ "$pending" -gt 0 ]; then
  # Greppable, and deliberately not silent. The previous shape logged
  # "delete FAILED (non-fatal)" per version, which reads as noise and let the
  # sibling package quietly double its MSVC tag count unnoticed for a week.
  #
  # NO CAUSAL CLAIM HERE. The cause is whatever the per-version REFUSED lines
  # above report; asserting one in the summary is what made the last two
  # failures look identical.
  echo "prune: PENDING $pending  (see the REFUSED line(s) above for what the API said; run the prune for $LABEL locally with a delete:packages token to reclaim)"
fi
