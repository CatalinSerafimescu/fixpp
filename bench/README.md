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
python3 tools/bench_seed.py current.json bench/baselines/placeholder.json
git add bench/baselines/placeholder.json
```

`tools/bench_seed.py` copies the Google Benchmark output unchanged except for
the added `context` key(s): `context.git_sha` is the full SHA of `HEAD` (#276).
Run it in the checkout the benchmark binary was built from, before committing or editing
anything else there; it sees the tree when it runs, not when the binary was
built. It refuses a dirty tree (tracked changes, or untracked files that are not
ignored, other than the two files it is given) and writes nothing; `--allow-dirty`
seeds anyway and records `context.git_dirty: true`.

To ask whether a baseline was invalidated later in its own PR, list what changed
after its seeding commit in the paths it measures:
`git log --name-only <git_sha>..HEAD -- <paths the baseline covers> ':!bench/baselines'`.
The range means "after seeding" only while `<git_sha>` is an ancestor of `HEAD`
(`git merge-base --is-ancestor <git_sha> HEAD`), so ask it on the PR branch; after
a squash merge the seeding commit is off `main`, and the range must end at the
PR's head (`git fetch origin pull/<N>/head`) instead.
A baseline without `context.git_sha` has an unknown seeding commit; do not
backfill one, because a SHA written after the fact is a guess, not a record.
