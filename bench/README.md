# fixpp Benchmark Suite

## Status

**SOFT GATE — Phase 3**: The ±5% performance regression gate described in
`[const §VIII.2]` is enforced as **report-only** until Phase 4 module 1 closes.
The CI bench job always exits 0 this phase.

The gate becomes hard once real perf-sensitive code lands in Phase 4.

## Structure

```
bench/
  src/                  benchmark translation units
  baselines/            committed JSON baselines (Google Benchmark --benchmark_format=json)
  README.md             this file
```

## Running locally

```bash
# Build
cmake --preset linux-clang-release -DFIXPP_BUILD_BENCH=ON
cmake --build --preset linux-clang-release

# Run and capture
./build/linux-clang-release/bin/placeholder_bench \
  --benchmark_format=json \
  --benchmark_out=current.json

# Compare vs baseline (exits 0; report-only)
python3 tools/bench_compare.py bench/baselines/placeholder.json current.json
```

## Updating baselines

When an intentional performance change lands, update the baseline **in the
same PR** with rationale in the PR body per `[const §VIII.2]`:

```bash
cp current.json bench/baselines/placeholder.json
git add bench/baselines/placeholder.json
```
