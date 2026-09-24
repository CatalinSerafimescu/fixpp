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

## 2. Functional witnesses (Article VII §8: select by label)

Every ctest entry this feature adds to or edits carries the label `091` (set in the CMakeLists that
registers it; the wire tests carry no label today). The expected population is the **set of ctest
names**, not a count, and it is a **checked-in manifest**:
`specs/091-data-field-bytes/expected-ctest-091.txt`, one ctest name per line.

**The names are `/speckit-tasks` work, not Gate A's.** Tasks decides each test's file, executable and
registration, and writes the manifest from the names it registers. The entries it must cover, by
role: `wire_body_builder_test` (standalone, §VII.8-exempt: in-TU global `operator new` counter; C-1
except C-1.9); `wire_dict_tests` (C-1.9, the per-dictionary drift arm); the C-2.5a loader test under
`tests/dictionary/`; `test_067_builder_failclosed` (the flipped `_418` pins); the entry holding the
C-2.6 witnesses; `fixpp::dict::read-tier-byte-diff`; the C-2.2 census and C-2.3 test under
`tests/codegen/`; the §5 session witnesses; the C-ABI exact-version bucket (`capi_pure_tests`, which
holds `version_test.cpp`); the FR-019 C-ABI before/after test's bucket (for example
`capi_length_data`, or whichever entry tasks registers it in), including its `fixpp_session_send`
assertion's entry if that lands elsewhere; the FR-019 inbound-drop witness's entry (`wire_dict_tests`
if it lands in `dict_hooks_custom_pair_test.cpp`, else the session entry that holds it). Gate A fixes the gate below, which cannot pass with a
placeholder: a placeholder is not a registered ctest name.

```bash
T=build/linux-clang-debug
M=specs/091-data-field-bytes/expected-ctest-091.txt
R=$(mktemp -d)                                   # fresh per run; never a reused path
test -s "$M" || { echo "RED: manifest missing or empty"; exit 1; }
sort -u "$M" > "$R/expected.txt"
ctest --test-dir $T -N -L '^091$' | sed -n 's/^ *Test *#[0-9]*: //p' | sort -u > "$R/labelled.txt"
ctest --test-dir $T -N          | sed -n 's/^ *Test *#[0-9]*: //p' | sort -u > "$R/all.txt"
test -s "$R/labelled.txt" || { echo "RED: no test carries label 091"; exit 1; }
diff "$R/expected.txt" "$R/labelled.txt" || { echo "RED: label set != manifest"; exit 1; }
comm -23 "$R/expected.txt" "$R/all.txt" | grep . && exit 1  # every entry must be a registered test
ctest --test-dir $T -L '^091$' --output-on-failure
```

Then run the **full** suite, `ctest --test-dir build/linux-clang-debug --output-on-failure`. The
loader change (FR-017) moves FIX 5.0 SP2 dictionary answers that unlabelled `tests/dictionary` and
`tests/codegen` tests read (for example the `table_view` differential, `length_data_table_test` and
the codegen determinism golden); the label run is not what covers them, the full run is.

(Use `-L '^091$'`, not `-L failclosed`: that label also matches `test_067_builder_roundtrip`.)

Expected:
- the four `DataField_EncodedText_*_418` cases **pass as success cases** (verbatim emit + re-parse);
- `SohInValue_RejectedBeforeAnyByteReachesOut` passes, **unchanged**:
  `git diff origin/main -- tests/session/test_067_builder_failclosed.cpp` shows no edit inside that
  `TEST` body;
- the C-1 clauses in `contracts/body-builder-data.md` each pass.

## 3. Mutants (run in a scratch copy of the tree, never in the PR worktree)

Each row names an existing test that must be shown RED, then GREEN after reverting.

| Mutant | Must turn RED |
|---|---|
| `field_data` routes through `append_string_field` (re-applies the content guard) | the four `_418` success cases; C-1.2 |
| the commit pair check is deleted | C-1.7; C-1.9's "malformed hand-written custom pair refused" arm |
| a group node is fed as `observe(no_tag, <count digits>)` instead of `observe(no_tag, {})` | C-1.7's "group `no_tag` 354 + sibling one-byte 355" case |
| `FIXPP_C_ABI_VERSION_MINOR` is set back to 8 | `tests/capi/version_test.cpp` exact-version cell (FR-019) |
| the loader's component/group walk is removed (C-ABI view) | the FR-019 C-ABI test (the malformed component-only custom pair commits `FIXPP_ERR_OK` again); its additive-widening assertion (`fixpp_msg_set_string` refuses the SOH-bearing Data value again); its `fixpp_msg_set_data(5002, len = 0)` assertion (`FIXPP_ERR_TYPE_MISMATCH` again); its `fixpp_session_send` assertion (the malformed payload sends `FIXPP_ERR_OK` again); the FR-019 inbound-drop witness (`Parser<Index>` accepts the malformed frame again); both FR-019 reader witnesses (tag 1137 reads `"9"` again; `msg_type()` is `"D"` again) |
| `field_data` appends Data before Length | C-1.1, C-1.10 |
| the second-append rollback is removed (Length survives a failed Data append) | C-1.5 (its commit-and-byte-compare oracle; the stray Length fails INV-6 or changes the bytes) |
| `field_data` resolves through `hooks_` instead of the standard table | C-1.9's "`field_data(5002, …)` refused" arm |
| commit's checker is built from `none()` instead of `hooks_` | C-1.9's "malformed custom pair refused at commit" arm |
| the emitter passes `item.tag` instead of `item.data_tag` | the v44 builder build (C-2.2 `static_assert`) |
| the emitter changes only the top-level arm | C-2.6 **nested** 256-value witness; C-2.2 census (wrong arm) |
| the `message_encoding` selection uses "begins with `Encoded`" | the C-2.3 v50sp2 witness (a message whose only encoded field is `DerivativeEncoded*` / `InstrumentScopeEncoded*`) |
| the loader's component/group walk is removed ("loader walk removed": the new emitter over the unfixed loader) | the per-dictionary drift arm (FIX50SP2, the five pairs); C-2.2 census control (a) and the orphan-half check on v50sp2; C-2.5a arms (i), (ii), (iv), (v), (vi), (vii), (viii) |
| the new walk skips non-field children instead of breaking | C-2.5a arm (iii) |
| the new walk visits only components and their direct `<group>` children | C-2.5a arms (v) and (vi) |
| groups are walked depth-first, right after their container, instead of after all component definitions | C-2.5a arm (vii), all placements |
| the group walk is entered only from messages and component definitions | C-2.5a arm (viii) (a) and (b), and arm (vii)(c) |
| one non-FIX50SP2 drift leg's probe returns nothing (e.g. FIX44's `message_fields()` result emptied) | that leg's non-empty assertion (FR-018) |
| `set_data` skips pair resolution (forwards straight to `append_bytes_field`) | C-1.4's `set_data` arms for tags 11 (the SOH-bearing payload) and 354; C-1.9's `set_data(5002, …)` arm |
| `set_data` skips the second-append rollback (Length survives a failed Data append) | C-1.5's nested `set_data` twin (its commit-and-byte-compare oracle) |
| `set_data` resolves through `hooks_` instead of the standard table | C-1.9's `set_data(5002, …)` arm |
| `set_data` omits the owner check | C-1.4b default handle (under ASan it must fail, not UB-pass) |
| `set_data` omits `is_innermost_open` | C-1.4b outer handle and closed-group handle |
| `builder_bench`'s WithGroup / Raw case builds a body missing one scalar field (both cases), or one of the three `kParties` entries (WithGroup), with the exact prechecks in place | that case reports `SkipWithError` (run before the §6 measurement) |

## 4. Golden regeneration check

Follow `contracts/codegen-builders.md`:
- C-2.2 (exact IR-vs-standard-table census plus orphan-half check, control (a) proven on the
  new-emitter/unfixed-loader output first);
- C-2.4 (structural residual diff, proven RED with an injected stray emit);
- C-2.5 (exactly `v50sp2/Fields.hpp` and `v50sp2/Validator.hpp` rebaselined, each with its
  re-derivation recipe run; every other read-tier pin unchanged).

## 5. Session path

A test sends a message carrying XmlData(212/213) with SOH in the value through `Session::send_impl`,
then re-parses the emitted frame. Expected: 212 and 213 are adjacent in the header, and the 213
value round-trips.

A second test sends a generated-builder message with `message_encoding` and a non-ASCII
`encoded_text` through `send_impl`. Expected: `347` sits in the header, and `354`/`355` are in the
body with their verbatim bytes.

## 6. Performance (SC-005)

**Owner intent, kept checkable.** The owner fixed the comparand as "a baseline taken on unmodified
`main` before any edit" (spec Clarifications): production code equal to `main`, never a post-change
re-baseline. Article VIII §2 fixes the procedure: merge-base, both trees built and benchmarked in the
same session, A-B-A-B, min-per-tree. Both hold at once:

- **base** = `builder_bench` built in this session, same preset and toolchain as the candidate, from
  a **detached worktree** at the **current** `git merge-base HEAD origin/main` (after `git fetch`),
  with this branch's **final** `bench/wire/builder_bench.cpp` and `bench/wire/CMakeLists.txt` copied
  in and nothing else. `git diff --stat <merge-base> -- src include tools cmake` inside that worktree
  MUST be empty: that empty diff is the "unmodified main" guarantee. The final bench source MUST
  compile against the merge-base API (it may not call `field_data`/`set_data`); if it cannot, stop
  and report to the owner.
- **candidate** = the branch's `builder_bench`, after §1's codegen freshness step on this preset.
- `/mnt/wsl/fixppbuild/091-baseline/builder_bench.base` (frozen at `4749f589`) is a drift
  cross-check only, **never** the comparand.
- Never `git checkout` in a shared checkout: the base is its own worktree, on `/mnt/wsl/fixppbuild`.
- Before measuring, the WithGroup/Raw exact-precheck mutants of §3 are shown to `SkipWithError`.

```bash
MB=$(git merge-base HEAD origin/main)
W=/mnt/wsl/fixppbuild/091-base-wt
git worktree add --detach "$W" "$MB"
for f in bench/wire/builder_bench.cpp bench/wire/CMakeLists.txt; do git show HEAD:$f > "$W/$f"; done
git -C "$W" diff --stat "$MB" -- src include tools cmake   # must print nothing
git -C "$W" status --porcelain   # must list exactly the two bench files above, nothing else
# configure + build builder_bench in $W with the same preset (linux-clang-release) and toolchain
B=$W/build/linux-clang-release/bench/wire/builder_bench
C=build/linux-clang-release/bench/wire/builder_bench
O=$(mktemp -d -p /mnt/wsl/fixppbuild 091-ab.XXXX)          # fresh per session; never a reused path
for i in 1 2 3 4; do
  for t in base cand; do
    bin=$B; [ $t = cand ] && bin=$C
    taskset -c 3 $bin --benchmark_repetitions=15 --benchmark_min_time=0.2s \
      --benchmark_report_aggregates_only=true --benchmark_out=$O/$t-$i.json \
      --benchmark_out_format=json >/dev/null
  done
done
# after the xml_loader_bench run below:  git worktree remove --force "$W"
```

**Every leg must exist.** A missing or empty `$O/<t>-<i>.json`, or a leg lacking any of the four
cases, fails the run; it is never skipped.

**Precondition — noise floor, from the same session (no extra runs).** Per case, take the medians of
all **base** legs and compute (max − min) / min. The floor is the maximum of that over
`NoGroup`, `WithGroup` and `Raw`. If the floor exceeds **1 %**, the verdict is **inconclusive**: it
goes to the owner with the figures and is never compared. (1 % is budget/3, so a 3 % verdict stays
resolvable; 0.6 % would sit at the dated A/A maximum in R-10.)

**Verdict**, only when the precondition holds: per case, candidate minimum-of-medians against base
minimum-of-medians:
- `NoGroup`, `WithGroup` and `Raw` must be ≤ +3 %;
- `AsciiEncodedText` is reported only.

Over budget → back to the owner, with the floor and the per-leg figures. Never silently relax.

**`xml_loader_bench` (FR-017 engages this paired row).** Same procedure, same session shape, with the
base worktree above (no bench copy needed): A-B-A-B of `bench/dictionary/xml_loader_bench`. Pass
condition: Article VIII §2's budget (a slowdown ≤ +5 %). Over it → the §2 approval path, never
self-declared.
