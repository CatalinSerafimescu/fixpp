#!/usr/bin/env bash
# ci/red-arms/filesink-join-and-backpressure.sh -- the arms that justify #400 + #211.
#
# ⚠️ A GREEN TEST IS NOT EVIDENCE THAT ITS ASSERTIONS CAN FAIL. Both changes here
# replace or add an assertion, and this repo's most recurring defect is an
# instrument that reports clean because it COULD NOT report anything else. So
# every new assertion gets a mutant that must redden it, and -- for #400 -- the
# PRE-FIX assertion gets the SAME mutant, to show it stays green. Without that
# second half, "the old band was blind to the detach regression" is an argument.
# With it, it is a measurement.
#
#   ARM 0  unmutated, both new tests         -> GREEN. Attribution control: a red
#                                              below is the injected defect and
#                                              not the machine or the build.
#
#   ARM 1  close() does not wait for the
#          in-flight fsync, NEW test         -> RED. #400's causal assertion.
#
#   ARM 2  the SAME mutant, PRE-FIX test     -> GREEN. ⚠️ THIS ARM IS THE POINT OF
#                                              #400. The band it shipped with
#                                              (`close_elapsed < 500+200 ms`) is
#                                              satisfied by a close() that returns
#                                              in ~0 ms, which is exactly what the
#                                              regression does. A green here is
#                                              the DEFECT being demonstrated, not
#                                              a passing arm -- read the grading
#                                              line, not the colour.
#
#   ARM 3  emit() short-writes every line    -> RED. #211's integrity assertion.
#                                              Models the uncounted-loss path the
#                                              test's accounting comment names:
#                                              emit() only counts `written > 0`,
#                                              so a short fwrite loses bytes and
#                                              increments nothing.
#
#   ARM 4  the rotation storm removed from
#          the test's own config             -> RED. #211's stall lever. ⚠️ THIS IS
#                                              THE SPURIOUS-HIT ARM, and it is
#                                              here because the first version of
#                                              the test FAILED it. A fixed 4000-
#                                              record burst at a 64-slot ring
#                                              reported drops=3936 on five
#                                              consecutive runs -- exactly
#                                              4000-64, i.e. the drain consumed
#                                              nothing and an in-memory mock would
#                                              have given the identical number.
#                                              `drop_count() > 0` was passing for
#                                              a reason unrelated to FileSink. A
#                                              forced MISS could never have caught
#                                              that; only removing the lever and
#                                              demanding RED does.
#
#   ARM 5  the fsync worker is STARVED
#          (delayed past flush()'s deadline)  -> GREEN. ⚠️ THIS ARM GUARDS A FALSE
#                                              RED THAT WAS REAL, not theoretical.
#                                              flush() only POSTS a request;
#                                              `worker_cmd_` is a single slot and
#                                              close() -> stop_worker() OVERWRITES
#                                              a still-pending request with
#                                              WorkerCmd::stop, so a worker that
#                                              had not yet woken exits WITHOUT
#                                              ever calling fsync_fn -- and the
#                                              causal assertion then failed on
#                                              CORRECT production code. Measured:
#                                              with this mutant and no wait,
#                                              FlushDeadlineBounded FAILED in
#                                              301 ms; with the wait it passes.
#                                              The test now waits for the callback
#                                              to be ENTERED before close(), which
#                                              closes the window structurally
#                                              rather than making it unlikely.
#                                              Delete that wait and this arm
#                                              reddens.
#
#   ARM 6  read_sequence_ acquire -> relaxed
#          (TSan presets only)               -> PAIRED. The mutant must produce
#                                              >0 ThreadSanitizer reports and the
#                                              restored tree exactly 0. This is
#                                              the ONLY arm that touches the one
#                                              production line this PR changes
#                                              (#402), and the only one graded on
#                                              sanitizer output rather than exit
#                                              code. SKIPPED, loudly and without
#                                              counting as a pass, on any non-TSan
#                                              preset.
#
# ⚠️ ARM 1 AND ARM 4 MUST NOT BE COLLAPSED into "the tests fail when the code is
# broken". They rule out each other's blind spot: arm 1 says the join assertion
# is load-bearing, arm 4 says the backpressure test reaches the real sink at
# all. Either alone leaves the other's failure mode live.
#
# ⚠️ AND ARM 4 GRADES ASSERT_TRUE(rotation_seen), NOT THE DROP-RISE CHECK. That
# ASSERT is fatal and sits above EXPECT_GT(drops, drops_at_first_rotation), so
# removing the storm lever returns from the test before the drop check is ever
# evaluated. This matters because the drop check is NOT sink-coupled -- any
# unpaced producer against any sink satisfies it (see the comment on it). The
# sink-coupling this arm proves lives entirely in "an archive appeared", which
# only a real FileSink can produce. Do not re-describe arm 4 as grading the
# backpressure assertion; an earlier revision of this header did, and it was
# wrong.
#
# ⚠️ AND ARM 5 IS THE ONLY ARM THAT EXPECTS GREEN FROM A MUTANT. Arms 1-4 ask
# "can the check fail when it should?"; arm 5 asks "can it PASS when it should?"
# -- a check that reddens on correct code is as broken as one that never reddens,
# and nothing else here would notice.
#
# ⚠️ WHY THE ARM-1 MUTANT HAS TWO PARTS. `join()` -> `detach()` alone leaves the
# worker touching worker_mu_ / worker_fsync_done_ after the stack-local FileSink
# is destroyed -- a use-after-free that would abort the process for a reason that
# is NOT the assertion under test, and would then grade arm 2 as RED for the
# wrong reason. The second part makes the detached worker return without touching
# any member after its fsync, isolating the assertion. It does not weaken the
# mutant: close() still returns without waiting, which is the whole regression.
#
# ⚠️ EACH ARM CHECKS ITS BINARY'S SHA MOVED. A mutation that failed to apply
# leaves the previous binary in place and the arm grades a tree it did not build.
# Restores by `cp` from copies taken at entry -- never `git checkout --`, which
# would wipe uncommitted work in the same files.
#
# Usage:  ci/red-arms/filesink-join-and-backpressure.sh [PRESET] [BASE_REF]
set -uo pipefail

PRESET="${1:-linux-gcc-release}"
BASE_REF="${2:-origin/main}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$HERE" || exit 1

BUILD="build/${PRESET}"
[ -d "$BUILD" ] || { echo "no such build tree: $BUILD"; exit 2; }

SRC_SINK="src/log/file_sink.cpp"
SRC_FSYNC="tests/log/test_file_sink_async_fsync.cpp"
SRC_BP="tests/log/test_file_sink_backpressure.cpp"
# ⚠️ PRODUCTION FILE, mutated by arm 6. It is in the save/restore set for that
# reason: an arm that dies between mutate and restore must not leave the ring's
# memory ordering weakened in the working tree.
SRC_LOGGER="src/log/logger.cpp"

BIN_FSYNC="$BUILD/bin/log_file_fsync_test"
BIN_BP="$BUILD/bin/log_file_backpressure_test"

SAVE="$(mktemp -d)"
cp "$SRC_SINK"   "$SAVE/file_sink.cpp"
cp "$SRC_FSYNC"  "$SAVE/test_fsync.cpp"
cp "$SRC_BP"     "$SAVE/test_bp.cpp"
cp "$SRC_LOGGER" "$SAVE/logger.cpp"

restore() {
  cp "$SAVE/file_sink.cpp" "$SRC_SINK"
  cp "$SAVE/test_fsync.cpp" "$SRC_FSYNC"
  cp "$SAVE/test_bp.cpp"    "$SRC_BP"
  cp "$SAVE/logger.cpp"     "$SRC_LOGGER"
}
trap 'restore; rm -rf "$SAVE"' EXIT

fails=0
sha_of() { [ -f "$1" ] && sha256sum "$1" | cut -c1-16 || echo "absent"; }

# ⚠️ A GREEN-EXPECTING ARM NEEDS THIS AND THE BINARY-SHA GUARD DOES NOT COVER IT.
# Arm 2 expects GREEN, so "the binary moved" is satisfied by swapping the TEST
# file alone -- and on the first run of this script that is exactly what
# happened: arm 1's mutation aborted on a stale anchor, wrote nothing, and arm 2
# then reported the GREEN it wanted against an UNMUTATED sink. A green arm that
# proves nothing is worse than a red one. So an arm whose meaning depends on a
# mutant must assert the mutant is actually in the source it is about.
require_mutant() {  # require_mutant <file> <marker>
  if ! grep -q -- "$2" "$1"; then
    echo "  PRECONDITION FAILED: '$2' absent from $1 -- the arm would grade an unmutated tree"
    fails=$((fails + 1)); return 1
  fi
}

build_target() {  # build_target <target>
  cmake --build "$BUILD" --target "$1" -j "${J:-6}" >"$SAVE/build.log" 2>&1
}

# mutate <file> <anchor> <replacement> -- single-site textual mutation.
# The count-must-be-1 assertion is the point: a drifted anchor matches zero or
# many, and a silent no-op mutation would leave the arm grading an UNMUTATED
# tree. Arms 3 and 4 each carried their own copy of this; one copy cannot drift
# from the other. NOT used for the arm-1 detach mutant: that one has a 6-line
# C++ anchor whose exact leading whitespace is load-bearing, and its failure
# branch also has to charge arm 2, so the inline heredoc is the right shape there.
mutate() {
  python3 - "$1" "$2" "$3" <<'PY'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text()
a, b = sys.argv[2], sys.argv[3]
assert s.count(a) == 1, f"anchor count = {s.count(a)} (expected 1)"
p.write_text(s.replace(a, b))
PY
}

# run_arm <name> <expect: RED|GREEN> <target> <binary> <gtest filter>
run_arm() {
  local name="$1" expect="$2" target="$3" bin="$4" filter="$5"
  local before after
  before="$(sha_of "$bin")"

  if ! build_target "$target"; then
    echo "  ARM $name: BUILD FAILED -- the mutation does not compile"
    tail -15 "$SAVE/build.log"
    fails=$((fails + 1)); return 1
  fi

  after="$(sha_of "$bin")"
  # ⚠️ The binary must have MOVED for a mutated arm. If it did not, the mutation
  # never reached the compiler and this arm grades the previous build.
  if [ "$expect" = "RED" ] && [ "$before" = "$after" ]; then
    echo "  ARM $name: BINARY UNCHANGED ($before) -- mutation did not apply; arm is void"
    fails=$((fails + 1)); return 1
  fi

  local out rc
  out="$("$bin" --gtest_filter="$filter" 2>&1)"; rc=$?

  local got="GREEN"; [ "$rc" -ne 0 ] && got="RED"
  if [ "$got" = "$expect" ]; then
    echo "  ARM $name: $got (expected $expect)  [rc=$rc, bin $before -> $after]"
  else
    echo "  ARM $name: $got but expected $expect  [rc=$rc, bin $before -> $after]"
    echo "$out" | tail -20
    fails=$((fails + 1))
  fi
}

echo "== red arms for #400 (close() joins) + #211 (FileSink backpressure) =="
echo "   preset=$PRESET base=$BASE_REF"

# ── ARM 0: unmutated ─────────────────────────────────────────────────────────
echo "-- ARM 0: unmutated control"
run_arm 0a GREEN log_file_fsync_test        "$BIN_FSYNC" 'FileSinkFsyncTest.*'
run_arm 0b GREEN log_file_backpressure_test "$BIN_BP"    'FileSinkBackpressureTest.*'

# ── The arm-1/2 mutant: close() stops waiting for the in-flight fsync ────────
apply_detach_mutant() {
  python3 - "$SRC_SINK" <<'PY'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text()

# (a) close()/rotate() no longer wait for the worker.
a = "        fsync_worker_.join();"
assert s.count(a) == 1, f"join() site count = {s.count(a)}"
s = s.replace(a, "        fsync_worker_.detach();  // MUTANT (#400 arm)")

# (b) the detached worker must not touch members after its fsync, or the arm
#     measures a use-after-free instead of the assertion. See the header.
b = """                // Signal completion.
                {
                    std::lock_guard<std::mutex> done_lk(worker_mu_);
                    worker_fsync_done_ = true;
                }
                worker_done_cv_.notify_one();"""
assert s.count(b) == 1, "completion-signal block not found verbatim"
s = s.replace(b, "                return;  // MUTANT (#400 arm): no member touch after fsync")

p.write_text(s)
PY
}

echo "-- ARM 1: close() does not wait for the in-flight fsync, NEW test"
if apply_detach_mutant && require_mutant "$SRC_SINK" "MUTANT (#400 arm)"; then
  run_arm 1 RED log_file_fsync_test "$BIN_FSYNC" 'FileSinkFsyncTest.FlushDeadlineBounded'

  echo "-- ARM 2: the SAME mutant against the PRE-FIX assertion (must stay GREEN)"
  if git show "${BASE_REF}:${SRC_FSYNC}" > "$SRC_FSYNC" 2>/dev/null &&
     require_mutant "$SRC_SINK" "MUTANT (#400 arm)"; then
    run_arm 2 GREEN log_file_fsync_test "$BIN_FSYNC" 'FileSinkFsyncTest.FlushDeadlineBounded'
    echo "     ^ GREEN here is #400 reproduced: the pre-fix band cannot see a"
    echo "       close() that returns without joining."
  else
    echo "  ARM 2: could not stage the pre-fix test -- skipped, NOT passed"
    fails=$((fails + 1))
  fi
else
  echo "  ARM 1: MUTATION FAILED TO APPLY -- source moved; re-derive the anchors"
  echo "  ARM 2: NOT RUN (it is only meaningful with arm 1's mutant in place)"
  fails=$((fails + 2))
fi
restore

# ── ARM 3: emit() short-writes ───────────────────────────────────────────────
echo "-- ARM 3: emit() short-writes every line (uncounted loss / torn line)"
if mutate "$SRC_SINK" \
    "        auto written = std::fwrite(line.data(), 1, line.size(), stream_);" \
    "        auto written = std::fwrite(line.data(), 1, line.size() - 5, stream_);  // MUTANT (#211 arm)"
then
  run_arm 3 RED log_file_backpressure_test "$BIN_BP" 'FileSinkBackpressureTest.*'
else
  echo "  ARM 3: MUTATION FAILED TO APPLY"; fails=$((fails + 1))
fi
restore

# ── ARM 4: the stall lever removed ───────────────────────────────────────────
echo "-- ARM 4: rotation storm removed from the test's own config"
if mutate "$SRC_BP" \
    "    cfg.max_file_bytes = 200u;" \
    "    cfg.max_file_bytes = 256u * 1024u * 1024u;  // MUTANT (#211 arm): no storm"
then
  run_arm 4 RED log_file_backpressure_test "$BIN_BP" \
    'FileSinkBackpressureTest.RealFileSinkDropsAccountablyUnderRotationStorm'
else
  echo "  ARM 4: MUTATION FAILED TO APPLY"; fails=$((fails + 1))
fi
restore

# ── ARM 5: the worker is starved past flush()'s deadline ─────────────────────
echo "-- ARM 5: fsync worker starved (must stay GREEN -- guards a real false red)"
if mutate "$SRC_SINK" \
    "        fsync_worker_ = std::thread([this]() {" \
    "        fsync_worker_ = std::thread([this]() {
            std::this_thread::sleep_for(std::chrono::milliseconds{300});  // MUTANT (#400 arm 5)"
then
  run_arm 5 GREEN log_file_fsync_test "$BIN_FSYNC" 'FileSinkFsyncTest.FlushDeadlineBounded'
  echo "     ^ GREEN means the test tolerates a worker that has not been scheduled."
  echo "       Remove the wait-for-entered before close() and this goes RED."
else
  echo "  ARM 5: MUTATION FAILED TO APPLY"; fails=$((fails + 1))
fi
restore

# ── ARM 6: the #402 acquire on read_sequence_ ────────────────────────────────
#
# ⚠️ THE ONLY PRODUCTION LINE IN THIS PR HAD NO ARM UNTIL THIS ONE. Arms 0-5 all
# mutate file_sink.cpp or a test; the relaxed->acquire change was justified
# entirely by prose. This arm makes it reproducible.
#
# It cannot be graded by exit code like the others: a TSan report is not a gtest
# failure, so the grader COUNTS "WARNING: ThreadSanitizer" lines instead. It is
# PAIRED on purpose -- the mutant must report non-zero AND the restored tree must
# report zero. The zero alone would be worthless: an instrument that cannot
# report anything reports clean, which is this repo's most recurring defect.
#
# ⚠️ THE RED HALF IS PROBABILISTIC, AND THAT IS RECORDED RATHER THAN TUNED. TSan
# must still hold a shadow cell for the slot's previous generation when the
# reusing write lands; that is a property of its shadow-memory eviction under
# this corpus, not a structural guarantee. If it ever stops firing, the correct
# response is to say so here -- NOT to enlarge the corpus until it fires again,
# which is tuning an instrument until it returns the wanted answer.
tsan_reports() {  # tsan_reports <binary> <filter>
  "$1" --gtest_filter="$2" 2>&1 | grep -c 'WARNING: ThreadSanitizer' || true
}

echo "-- ARM 6: #402 acquire on read_sequence_ (paired: mutant reports, fix does not)"
case "$PRESET" in
  *tsan*)
    ARM6_FILTER='FileSinkBackpressureTest.RealFileSinkDropsAccountablyUnderRotationStorm'
    if mutate "$SRC_LOGGER" \
        "            std::uint64_t r = read_sequence_.load(std::memory_order_acquire);" \
        "            std::uint64_t r = read_sequence_.load(std::memory_order_relaxed);  // MUTANT (#402 arm 6)"
    then
      if build_target log_file_backpressure_test; then
        mutant_n="$(tsan_reports "$BIN_BP" "$ARM6_FILTER")"
      else
        echo "  ARM 6: BUILD FAILED (mutant)"; tail -15 "$SAVE/build.log"; mutant_n="build-failed"
      fi
    else
      echo "  ARM 6: MUTATION FAILED TO APPLY"; mutant_n="mutate-failed"
    fi
    restore
    if build_target log_file_backpressure_test; then
      fixed_n="$(tsan_reports "$BIN_BP" "$ARM6_FILTER")"
    else
      echo "  ARM 6: BUILD FAILED (restored)"; fixed_n="build-failed"
    fi
    if [ "$mutant_n" -gt 0 ] 2>/dev/null && [ "$fixed_n" -eq 0 ] 2>/dev/null; then
      echo "  ARM 6: GRADED (relaxed -> $mutant_n ThreadSanitizer report(s); acquire -> $fixed_n)"
    else
      echo "  ARM 6: DID NOT GRADE (relaxed -> $mutant_n, acquire -> $fixed_n; want >0 then 0)"
      fails=$((fails + 1))
    fi
    ;;
  *)
    echo "  ARM 6: SKIPPED -- needs a TSan preset; got '$PRESET'."
    echo "         This arm is NOT counted as passing. Re-run as:"
    echo "         ci/red-arms/filesink-join-and-backpressure.sh linux-clang-tsan"
    ;;
esac
restore

# Rebuild clean so the tree is not left holding mutant binaries.
build_target log_file_fsync_test        >/dev/null 2>&1
build_target log_file_backpressure_test >/dev/null 2>&1

echo
if [ "$fails" -eq 0 ]; then
  echo "red arms: all arms graded as expected"
else
  echo "red arms: $fails arm(s) did NOT grade as expected"
fi
exit "$fails"
