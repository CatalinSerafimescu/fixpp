# Quickstart — validating 091 data-field-bytes

Run from the library root (the tree that owns branch `091-data-field-bytes`). The preset is
`linux-clang-debug` for tests and `linux-clang-release` for the benchmark. Check `df -h /mnt/e` before
any full rebuild of the main checkout's `-debug` tree.

## 1. Codegen freshness (required after any emitter change)

```bash
cmake --build build/<preset> --target fixpp-codegen
rm -rf build/<preset>/_codegen
cmake --build build/<preset>
```

A plain incremental build keeps the old generated headers: codegen runs at configure time and does
not track the emitter binary.

## 2. Functional witnesses

```bash
ctest --test-dir build/linux-clang-debug -R 'test_067_builder_failclosed|test_body_builder|dict_hooks_custom_pair|test_069_all_families_roundtrip' --output-on-failure
```

Expected:
- the four `DataField_EncodedText_*_418` cases **pass as success cases** (verbatim emit + re-parse);
- `SohInValue_RejectedBeforeAnyByteReachesOut` passes, **unchanged**:
  `git diff origin/main -- tests/session/test_067_builder_failclosed.cpp` shows no edit inside that
  `TEST` body;
- the C-1 clauses in `contracts/body-builder-data.md` each pass.

## 3. Mutants (run in a scratch copy of the tree, never in the PR worktree)

| Mutant | Must turn RED |
|---|---|
| `field_data` routes through `append_string_field` (re-applies the content guard) | the four `_418` success cases, C-1.2 |
| the commit pair check is deleted | C-1.7 |
| `field_data` appends Data before Length | C-1.1, C-1.10 |
| the second-append rollback is removed (Length survives a failed Data append) | C-1.5 |
| the emitter passes `item.tag` instead of `item.data_tag` | the v44 builder build (C-2.2 `static_assert`) |
| the emitter changes only the top-level arm | the nested-group case of C-1.2 via a generated builder |
| the `message_encoding` selection uses "begins with `Encoded`" | the C-2.3 v50sp2 witness (a message whose only encoded field is `DerivativeEncoded*` / `InstrumentScopeEncoded*`) |

## 4. Golden regeneration check

Follow `contracts/codegen-builders.md` C-2.2 (count census, proven RED on the old goldens first) and
C-2.4 (diff filter; read-tier SHA-256 pins unchanged).

## 5. Session path

A test sends a message carrying XmlData(212/213) with SOH in the value through `Session::send_impl`,
then re-parses the emitted frame. Expected: 212 and 213 are adjacent in the header, and the 213
value round-trips.

A second test sends a generated-builder message with `message_encoding` and a non-ASCII
`encoded_text` through `send_impl`. Expected: `347` sits in the header, and `354`/`355` are in the
body with their verbatim bytes.

## 6. Performance (SC-005)

```bash
B=/mnt/wsl/fixppbuild/091-baseline/builder_bench.base          # frozen baseline, commit 4749f589
C=build/linux-clang-release/bench/wire/builder_bench           # candidate, after step 1 on this preset
for i in 1 2 3 4; do
  for t in base cand; do
    bin=$B; [ $t = cand ] && bin=$C
    taskset -c 3 $bin --benchmark_repetitions=15 --benchmark_min_time=0.2s \
      --benchmark_report_aggregates_only=true --benchmark_out=/mnt/wsl/fixppbuild/091-baseline/$t-$i.json \
      --benchmark_out_format=json >/dev/null
  done
done
```

Compare per-case minimum-of-medians, candidate against base:
- `NoGroup`, `WithGroup` and `Raw` must be ≤ +3 %;
- `AsciiEncodedText` is reported only.

Over budget → back to the owner. The A/A noise floor measured at baseline was ≤ 0.6 % (research.md
R-10).
