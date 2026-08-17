#!/usr/bin/env bash
# ci/test-bench-gate.sh — regression pin for the #209 bench gate.
#
# WHY THIS EXISTS. The gate it pins replaced a job that was soft in THREE
# independent places (both steps `continue-on-error: true`, and
# tools/bench_compare.py `return 0` by construction) and was absent from
# tier1-required's needs:. Nothing about "we made it hard" is self-evident, and
# this repo has a standing lesson that a canary never RUN red proves nothing
# (feedback_sanitizer_canary_must_be_proven_red) and that a gate which observes
# but never exit-1s is a false green
# (feedback_ci_gate_observes_not_asserts_witness_skips_into_green).
#
# So every cell of the gate gets a MUTANT here, and each mutant must redden the
# ONE cell that names its defect — not merely produce some nonzero exit. A
# comparator that died on a Python syntax error would otherwise "prove" every
# negative at once.
#
# ⚠️ GREEN CONTROLS ARE AS LOAD-BEARING AS THE RED ONES. G1-G3 pin shapes that
# MUST pass: in particular a `stddev` aggregate of exactly 0.0, which is the
# correct output for identical repetitions. An earlier draft's "every timing
# must be > 0" would have failed on it — the cell would have been the defect.
#
# HOW. Synthetic Google-Benchmark JSON, generated here. No build tree, no
# compiler, no benchmark binaries, no network — python3 and coreutils only.
# Runner cells use shell stubs standing in for benchmark executables.
#
# Usage: ci/test-bench-gate.sh
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CMP="$repo_root/tools/bench_compare.py"
RUNNER="$repo_root/ci/run-bench-suite.sh"

fail() { echo "FAIL: $1" >&2; exit 1; }
command -v python3 >/dev/null || fail "python3 is required"
[ -f "$CMP" ]    || fail "comparator not found: $CMP"
[ -x "$RUNNER" ] || fail "runner not found or not executable: $RUNNER"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# ⚠️ DECLARED vs RUN, checked by machine at the end. A summary claiming N cells
# where N-1 ran is not something to leave to an eyeball — the same discipline
# ci/test-tier1-python-policy.sh records for its mutant count.
CELLS_DECLARED=35
cells_run=0

# ── fixture generation ───────────────────────────────────────────────────────
# A minimal but SCHEMA-FAITHFUL Google-Benchmark aggregates-only document:
# median + mean + stddev per benchmark, which is exactly what
# `--benchmark_repetitions=N --benchmark_report_aggregates_only=true` emits.
mkfix() {
  # mkfix <out.json> <name>=<median_ns> [<name>=<median_ns> ...]
  local out="$1"; shift
  python3 - "$out" "$@" <<'PY'
import json, sys
out, specs = sys.argv[1], sys.argv[2:]
rows = []
for s in specs:
    name, val = s.split("=")
    v = float(val)
    for agg, x in (("mean", v), ("median", v), ("stddev", 0.0)):
        rows.append({
            "name": f"{name}_{agg}", "run_name": name, "run_type": "aggregate",
            "aggregate_name": agg, "repetitions": 3, "threads": 1,
            "iterations": 1000, "real_time": x, "cpu_time": x,
            "time_unit": "ns",
        })
json.dump({"context": {"num_cpus": 4, "library_build_type": "release"},
           "benchmarks": rows}, open(out, "w"), indent=2)
PY
}

# Mutate one field of one row in a fixture, in place.
mutate() {
  # mutate <file> <row-substring-match> <key> <json-value>
  python3 - "$@" <<'PY'
import json, sys
path, match, key, val = sys.argv[1:5]
d = json.load(open(path))
n = 0
for r in d["benchmarks"]:
    if match in r.get("name", ""):
        if val == "__DELETE__":
            r.pop(key, None)
        else:
            r[key] = json.loads(val)
        n += 1
assert n > 0, f"mutation matched no row: {match}"
json.dump(d, open(path, "w"), indent=2)
PY
}

manifest() {
  # manifest <out> <line>...
  local out="$1"; shift
  printf '%s\n' "$@" > "$out"
}

# ── assertion helpers ────────────────────────────────────────────────────────
# ⚠️ A red cell is checked for ITS OWN reason, never merely for a nonzero exit.
expect_red() {
  local id="$1" desc="$2" want="$3"; shift 3
  local out="$TMP/out.$id"
  if "$@" >"$out" 2>&1; then
    fail "$id ($desc) did NOT fail the gate — output:\n$(cat "$out")"
  fi
  if ! grep -qF "$want" "$out"; then
    fail "$id ($desc) failed for the WRONG reason (wanted '$want') — output:\n$(cat "$out")"
  fi
  echo "RED (expected): $id — $desc"
  cells_run=$((cells_run + 1))
}

expect_green() {
  local id="$1" desc="$2"; shift 2
  local out="$TMP/out.$id"
  if ! "$@" >"$out" 2>&1; then
    fail "$id ($desc) should have PASSED but failed — output:\n$(cat "$out")"
  fi
  echo "GREEN (expected): $id — $desc"
  cells_run=$((cells_run + 1))
}

# ═══ SUITE MODE: tier 1 (hard) + tier 3 (informational) ══════════════════════

SUITE="$TMP/suite"; mkdir -p "$SUITE"
BASE="$TMP/baselines"; mkdir -p "$BASE/dictionary"
MAN="$TMP/manifest.txt"

mkfix "$SUITE/alpha_bench.json" BM_A=100 BM_B=200
mkfix "$BASE/dictionary/alpha.json" BM_A=100 BM_B=200
manifest "$MAN" \
  "bench/x/alpha_bench   gb-json:dictionary/alpha.json   no"

sc() { python3 "$CMP" --suite "$SUITE" --baselines-dir "$BASE" --manifest "$MAN"; }

# G1 — the clean shape must pass. Without this the RED cells prove only that the
# comparator can fail, which a syntax error also achieves.
expect_green G1 "clean suite passes" sc

# G2 — stddev == 0.0 is CORRECT for identical repetitions and must not trip T1-5.
# This is the cell that would have been the defect under a uniform "> 0" rule.
expect_green G2 "stddev aggregate of exactly 0.0 is accepted" sc

# T1-2 — nothing measured.
cp "$SUITE/alpha_bench.json" "$TMP/save.json"
python3 -c "import json,sys;d=json.load(open(sys.argv[1]));d['benchmarks']=[];json.dump(d,open(sys.argv[1],'w'))" "$SUITE/alpha_bench.json"
expect_red T1-2 "benchmarks: [] — the shape 2 shipped baselines are in today" "[T1-2]" sc
cp "$TMP/save.json" "$SUITE/alpha_bench.json"

# T1-1a — results file absent.
mv "$SUITE/alpha_bench.json" "$TMP/hidden.json"
expect_red T1-1a "results file missing" "[T1-1]" sc
mv "$TMP/hidden.json" "$SUITE/alpha_bench.json"

# T1-1b — results file unparseable.
cp "$SUITE/alpha_bench.json" "$TMP/save.json"
echo "{ this is not json" > "$SUITE/alpha_bench.json"
expect_red T1-1b "results file unparseable" "[T1-1]" sc
cp "$TMP/save.json" "$SUITE/alpha_bench.json"

# T1-1c — a manifest declares a gb-json comparand that does not exist. The
# manifest ASSERTED it exists, so this is a defect, not an informational note.
mv "$BASE/dictionary/alpha.json" "$TMP/hidden_base.json"
expect_red T1-1c "declared gb-json baseline missing" "declared baseline" sc
mv "$TMP/hidden_base.json" "$BASE/dictionary/alpha.json"

# T1-4 — a row with no usable identity.
cp "$SUITE/alpha_bench.json" "$TMP/save.json"
mutate "$SUITE/alpha_bench.json" BM_A_median name 123
expect_red T1-4 "row name non-string" "[T1-4]" sc
cp "$TMP/save.json" "$SUITE/alpha_bench.json"

# T1-5 — the measurement itself, four separate ways.
for spec in "__DELETE__:absent:T1-5a" '"abc":non-numeric:T1-5b' 'NaN:NaN:T1-5c' '-1.0:negative:T1-5d'; do
  val="${spec%%:*}"; rest="${spec#*:}"; what="${rest%%:*}"; id="${rest##*:}"
  cp "$SUITE/alpha_bench.json" "$TMP/save.json"
  if [ "$val" = "NaN" ]; then
    python3 -c "
import json,sys
d=json.load(open(sys.argv[1]))
for r in d['benchmarks']:
    if r['name']=='BM_A_median': r['cpu_time']=float('nan')
open(sys.argv[1],'w').write(json.dumps(d))" "$SUITE/alpha_bench.json"
  else
    mutate "$SUITE/alpha_bench.json" BM_A_median cpu_time "$val"
  fi
  expect_red "$id" "cpu_time $what" "[T1-5]" sc
  cp "$TMP/save.json" "$SUITE/alpha_bench.json"
done

# T1-6 — duplicate identity. Set equality cannot see a lost repetition;
# xml_loader.json carries 21 rows with 6 duplicate names.
cp "$SUITE/alpha_bench.json" "$TMP/save.json"
python3 -c "
import json,sys
d=json.load(open(sys.argv[1]))
dup=[r for r in d['benchmarks'] if r['name']=='BM_A_median'][0]
d['benchmarks'].append(dict(dup))
json.dump(d,open(sys.argv[1],'w'))" "$SUITE/alpha_bench.json"
expect_red T1-6 "duplicate (name, run_type, aggregate_name) identity" "[T1-6]" sc
cp "$TMP/save.json" "$SUITE/alpha_bench.json"

# ── T1-7 — units. THREE cells, and the GREEN one is why. ─────────────────────
#
# ⚠️ G8 PINS A SHAPE AN EARLIER DRAFT WOULD HAVE REJECTED ON A CORRECT TREE.
# That draft required ONE time_unit per binary. But the allowlisted
# `table_view_footprint_bench` deliberately mixes them: five benchmarks carry
# `->Unit(benchmark::kMicrosecond)` (table_view_footprint_bench.cpp:118, :134,
# :193, :218, :231) while `BM_TableView_Sizeof` uses the default ns. Under the
# old rule the CELL was the defect and AC-3 could never have gone green. Found
# by Codex round 2, F3.
cp "$SUITE/alpha_bench.json" "$TMP/save.json"
mutate "$SUITE/alpha_bench.json" BM_A time_unit '"us"'
expect_green G8 "two DIFFERENT benchmarks may use different time_units" sc
cp "$TMP/save.json" "$SUITE/alpha_bench.json"

# T1-7a — but ONE benchmark reporting two units within a run is incoherent:
# its own aggregates stop being comparable with each other.
cp "$SUITE/alpha_bench.json" "$TMP/save.json"
mutate "$SUITE/alpha_bench.json" BM_A_median time_unit '"us"'
expect_red T1-7a "one benchmark reports two time_units in one run" "[T1-7]" sc
cp "$TMP/save.json" "$SUITE/alpha_bench.json"

# T1-7b — an unrecognised unit. Worse than a missing number, because it still
# compares: the scale is simply unknown.
cp "$SUITE/alpha_bench.json" "$TMP/save.json"
mutate "$SUITE/alpha_bench.json" BM_A time_unit '"furlongs"'
expect_red T1-7b "unrecognised time_unit" "unrecognised time_unit" sc
cp "$TMP/save.json" "$SUITE/alpha_bench.json"

# T1-9 — a row reporting zero iterations timed nothing.
cp "$SUITE/alpha_bench.json" "$TMP/save.json"
mutate "$SUITE/alpha_bench.json" BM_A_median iterations '0'
expect_red T1-9 "iterations is zero" "[T1-9]" sc
cp "$TMP/save.json" "$SUITE/alpha_bench.json"

# T1-8 — Google Benchmark's own error disposition. The row still exists, so
# without this cell it is read as a measurement.
cp "$SUITE/alpha_bench.json" "$TMP/save.json"
mutate "$SUITE/alpha_bench.json" BM_A_median error_occurred 'true'
expect_red T1-8 "error_occurred row" "[T1-8]" sc
cp "$TMP/save.json" "$SUITE/alpha_bench.json"

# G3 — a `none:` comparand must PRINT ITS REASON, not vanish. A silently
# skipped row is how a gate reads green on nothing.
manifest "$MAN" "bench/x/alpha_bench   none:hand-authored-record-no-cpu_time-field   no"
if ! sc > "$TMP/g3.out" 2>&1; then fail "G3: none: comparand should still pass tier 1"; fi
grep -q "NOT COMPARED — hand-authored-record-no-cpu_time-field" "$TMP/g3.out" \
  || fail "G3: the tier-3 skip reason was not printed — output:\n$(cat "$TMP/g3.out")"
echo "GREEN (expected): G3 — a none: row prints its disqualifier by name"
cells_run=$((cells_run + 1))

# M-arity — a manifest row with the wrong field count. Silently binding a field
# to "" would make the comparator look for a baseline named "".
manifest "$MAN" "bench/x/alpha_bench   none:whatever"
expect_red M-arity "manifest row with 2 fields not 3" "expected exactly 3 fields" sc

# M-tier2 — an unrecognised tier-2 disposition must not read as `no`.
manifest "$MAN" "bench/x/alpha_bench   none:whatever   mabye"
expect_red M-tier2 "manifest tier-2 field misspelled" "must be 'paired' or 'no'" sc

# M-empty — a manifest that parses to zero rows satisfies everything vacuously.
manifest "$MAN" "# only a comment"
expect_red M-empty "manifest with zero rows" "yielded zero rows" sc

# ═══ PAIRED MODE: tier 2 (hard) ══════════════════════════════════════════════

A1="$TMP/a1"; B1="$TMP/b1"; A2="$TMP/a2"; B2="$TMP/b2"
mkdir -p "$A1" "$B1" "$A2" "$B2"
PMAN="$TMP/pmanifest.txt"
manifest "$PMAN" "bench/x/alpha_bench   none:n/a   paired"

pc() { python3 "$CMP" --paired --a1 "$A1" --b1 "$B1" --a2 "$A2" --b2 "$B2" \
                     --manifest "$PMAN" --band 50; }

setlegs() {  # setlegs <a1> <b1> <a2> <b2>  (single benchmark BM_A)
  mkfix "$A1/alpha_bench.json" "BM_A=$1"
  mkfix "$B1/alpha_bench.json" "BM_A=$2"
  mkfix "$A2/alpha_bench.json" "BM_A=$3"
  mkfix "$B2/alpha_bench.json" "BM_A=$4"
}

# G4 — candidate matches base: pass.
setlegs 100 100 102 101
expect_green G4 "candidate ≈ base passes tier 2" pc

# G5 — a 40% slowdown is UNDER the provisional 50% band and must pass. This pins
# the band's lower edge: without it, a cell asserting "regression detected"
# could be satisfied by a gate that fails on everything.
setlegs 140 100 140 100
expect_green G5 "40% slowdown is inside the ±50% band" pc

# T2-REG — the signal the gate exists for. #263 measured 60-90%; 90% here.
setlegs 190 100 190 100
expect_red T2-REG "candidate 90% slower than base (a #263-magnitude regression)" "REGRESSION" pc

# T2-NOISE-A — the two CANDIDATE measurements disagree by more than the band.
setlegs 100 100 300 100
expect_red T2-NOISE-A "A-vs-A noise floor exceeds the band" "UNINFORMATIVE" pc

# T2-TRANSIENT — ⚠️ THE BYPASS CODEX ROUND 2 FOUND, pinned as a REGRESSION that
# must still be caught. True base 100, candidate 160 (+60%), with a 20%
# transient inflating ONE base leg to 120. Both candidate legs agree perfectly,
# so A-vs-A = 0% and any A-B-A design calls the run informative.
#
# This cell has now falsified TWO of my own fixes, which is the whole reason it
# exists as a fixture rather than an argument:
#   * plain A-B-A            -> base 120, delta +33%, inside ±50%  -> PASS
#   * A-B-A-B averaging legs -> base 110, delta +45%, inside ±50%  -> PASS
# Only MIN-per-tree gets it: base = min(120,100) = 100, delta = +60% -> RED.
# Benchmark noise is one-sided (contention only ever slows a run down), so the
# fastest observation of each tree is its least-contaminated estimate.
#
# Note it is asserted as REGRESSION, not UNINFORMATIVE: a 20% transient is
# under the 20% noise gate, so the noise check does NOT save us here. The
# comparand choice is what does.
setlegs 160 120 160 100
expect_red T2-TRANSIENT "regression detected despite a base-leg transient" "REGRESSION" pc

# T2-DEL — a benchmark the BASE measures that the candidate no longer emits.
# Under a plain intersection this is a live bypass: rename the slow benchmark
# and its regression leaves the gate with it.
mkfix "$A1/alpha_bench.json" BM_RENAMED=100
mkfix "$A2/alpha_bench.json" BM_RENAMED=100
mkfix "$B1/alpha_bench.json" BM_A=100
mkfix "$B2/alpha_bench.json" BM_A=100
expect_red T2-DEL "benchmark present in base, renamed away in candidate" "[T2-DEL]" pc

# G7 — the asymmetry. A benchmark present only in the CANDIDATE is a PR ADDING
# one, which Article VIII §3 positively requires. It must NOT be an error.
mkfix "$A1/alpha_bench.json" BM_A=100 BM_NEW=50
mkfix "$A2/alpha_bench.json" BM_A=100 BM_NEW=50
expect_green G7 "a benchmark added by the candidate is not an error" pc

# T2-LEG — a missing leg. Silence here is a tier-2 false green.
setlegs 100 100 100 100
mv "$B1/alpha_bench.json" "$TMP/hidden_b.json"
expect_red T2-LEG "merge-base leg missing" "leg B1" pc
mv "$TMP/hidden_b.json" "$B1/alpha_bench.json"

# T2-NAMES — every leg ran but base and candidate share no name at all.
mkfix "$B1/alpha_bench.json" BM_ONLYBASE=100
mkfix "$B2/alpha_bench.json" BM_ONLYBASE=100
expect_red T2-NAMES "no benchmark name present in all four legs" "[T2-0]" pc
setlegs 100 100 100 100

# T2-VACUOUS — `paired` removed from every manifest row. The whole hard timing
# axis would silently assert nothing.
manifest "$PMAN" "bench/x/alpha_bench   none:n/a   no"
expect_red T2-VACUOUS "no manifest row marked paired" "no manifest row is marked" pc
manifest "$PMAN" "bench/x/alpha_bench   none:n/a   paired"

# ═══ RUNNER: ci/run-bench-suite.sh ═══════════════════════════════════════════

BUILD="$TMP/build"; mkdir -p "$BUILD/bench/x"
RMAN="$TMP/rmanifest.txt"
manifest "$RMAN" "bench/x/alpha_bench   none:n/a   no"

make_stub() {
  # make_stub <mode>  — a stand-in benchmark executable
  cat > "$BUILD/bench/x/alpha_bench" <<STUB
#!/usr/bin/env bash
out=""
for a in "\$@"; do case "\$a" in --benchmark_out=*) out="\${a#*=}";; esac; done
case "$1" in
  ok)     printf '{"context":{},"benchmarks":[{"name":"BM_A_median","aggregate_name":"median","run_type":"aggregate","cpu_time":1.0,"real_time":1.0,"time_unit":"ns"}]}' > "\$out" ;;
  crash)  exit 3 ;;
  silent) exit 0 ;;
esac
STUB
  chmod +x "$BUILD/bench/x/alpha_bench"
}

rc() { bash "$RUNNER" "$BUILD" "$TMP/rout" "$RMAN"; }

# G6 — the runner's happy path.
make_stub ok
rm -rf "$TMP/rout"
expect_green G6 "runner executes an allowlisted binary and writes results" rc

# R-MISSING — an allowlisted binary absent from the build tree. This was
# literally the old job's `continue-on-error` case: the suite silently shrank.
rm -f "$BUILD/bench/x/alpha_bench"
expect_red R-MISSING "allowlisted binary not built" "is not built or not executable" rc

# R-CRASH — a benchmark that exits non-zero.
make_stub crash
expect_red R-CRASH "benchmark exits non-zero" "exited non-zero" rc

# R-SILENT — a benchmark that exits 0 having written nothing.
make_stub silent
expect_red R-SILENT "benchmark exits 0 but writes no results" "wrote no results" rc

# ── declared vs run ──────────────────────────────────────────────────────────
if [ "$cells_run" -ne "$CELLS_DECLARED" ]; then
  fail "declared $CELLS_DECLARED cells but ran $cells_run — an early return or a \
commented-out cell changes this total, which is exactly why it is machine-checked"
fi
echo
echo "PASS: bench gate pin — $cells_run/$CELLS_DECLARED cells, each red one proven \
red for its own named reason."
