# Quickstart: validating 085-fold-flat-cap-loop

**Branch**: `085-fold-flat-cap-loop` · **Date**: 2026-08-03 · **Anchored at**: `main` = `c1564dd2`

This feature's deliverable is a **removal**, so "it works" cannot be shown by exercising a new capability. Validation is therefore three separate claims, each with its own procedure:

0. **The red-first structural pin goes RED** — run **before** the relocation, on the unmodified tree (FR-001b, SC-005b).
1. **Nothing changed** on the dictionary path (SC-001, SC-002, SC-005, SC-005a).
2. **The cap still fires**, on both paths, and both pins are load-bearing (SC-003, SC-004, SC-004a).
3. **No regression** in performance or hygiene (SC-006 – SC-008).

Run them in that order. §0 is the only step that runs **before** the code change; claim 2 is the one that can silently false-pass, so it carries the two mutation steps.

> **Rewritten at Gate A round 1.** The previous version of this file was authored without executing a single command against the repository. Every `--preset` it named was nonexistent *and* forbidden by the antecedent's own build preamble, and five of its `ctest -R` selectors matched **zero** registered tests — while exiting 0. The whole procedure passed while running nothing. Every command below has been run, or its selection count checked with `-N` / `--gtest_list_tests`, against `build/linux-clang-debug` at `c1564dd2` on 2026-08-03.
>
> **Fail-fast mechanism corrected at Gate A round 2 — and this time the mechanism itself was executed.** Round 1 added a count assertion to every selection but the assertion **did not halt**: `expect` only `return`s, and the two mutation steps chained it as `expect … || return 1` at *top level*, where `return` is invalid, prints an error and continues. Round 1 then wrote four sentences asserting the opposite. Every code block below is now a **subshell** whose assertions chain `|| exit 1`, so a failed count exits the block **before** the command it guards. Verified by running each corrected form twice — once with the true expected count, once with a deliberately wrong one — in **`bash script.sh`, pasted `bash -i`, `zsh script.sh` and pasted `zsh -i`** (this project's tooling runs zsh interactively, the form round 1's guard silently continued under). In all four, the wrong-count variant printed `SELECTOR FAIL`, exited the block with status 1, and the guarded command did not run.

---

## Build constraints (read before building)

Four rules, each with the reason and the reproduction. Transplanted from `specs/083-group-delimiter-resolution/quickstart.md:7-11` and extended.

- **`-j2` maximum.** Wide parallel C++ builds in this tree have OOM-killed the session.

- ~~**Never use `cmake --preset` / `ctest --preset`.**~~ **LIFTED by the 084 rebase (2026-08-03) — `cmake --preset` now WORKS; this rule no longer applies.** 084 removed the *cause*, not just the symptom: `conan/profiles/*` now set `tools.cmake.cmaketoolchain:user_presets=` (empty), so Conan **no longer generates `CMakeUserPresets.json` at all** — and with no accumulating `include:` list there is no `Duplicate preset` collision to break `--preset`. Verified after rebasing onto `8dc7ec9a`: delete the stale gitignored `CMakeUserPresets.json` + each `build/<preset>/CMakePresets.json`, re-run `conan install -pr conan/profiles/<preset> -of build/<preset>`, and the file is **not** regenerated; `cmake --preset linux-clang-debug` then succeeds. **Every step in this file may now use `cmake --preset <P>` / `cmake --build --preset <P>` / `ctest --preset <P>`, which is what `tier1.yml` actually runs** — preferred, since `-S`/`-B` re-derived the flags by hand and is where this feature's own verify pass mis-set four sanitizer caches. The `-S`/`-B` forms below still work and are left in place rather than rewritten wholesale. The original constraint and its two recorded mechanisms are preserved verbatim below as history — **do not read them as current state.**

  *(Historical, pre-084 — the rule as it stood when this file was written.)* **Never use `cmake --preset` / `ctest --preset`.** Reproduced from the library root, 2026-08-03:

  ```
  $ cmake --list-presets
  CMake Error: Could not read presets from …/library:
  File not found: …/library/build/linux-clang-coverage/CMakePresets.json
  ```

  `CMakeUserPresets.json` is Conan-generated and `include`s one preset file per build directory; `build/linux-clang-coverage/` does not exist here, so CMake refuses to read **any** preset. Configure with `-S`/`-B` instead. *(083 recorded a different mechanism for the same symptom — a duplicate `conan-debug` preset collision. The mechanism has changed; the rule has not. Do not copy 083's stated cause forward — the failure above is the one this tree actually produces.)*

- **Select tests by ctest **label**, or by `--gtest_filter` on a bucket binary — never `ctest -R <GoogleTest-case-name>`.** CTest registers **bucket** names, not GoogleTest case names. The three buckets this feature reads from:

  | GoogleTest suite | lives in `.cpp` | registered CTest test | labels |
  |---|---|---|---|
  | `WireOffsetTable.*` | `tests/wire/offset_table_test.cpp` | **`wire_pure_tests`** | `079;wire` |
  | `TypedReadSplitAgreement.*` | `tests/wire/typed_read_split_agreement_test.cpp` | **`wire_dict_tests`** | `075;083;wire` |
  | `DelimiterCensus.*` | `tests/dictionary/delimiter_census_test.cpp` | **`delimiter_census`** (lower case) | `dictionary;083;census` |

  Binaries land in `build/linux-clang-debug/bin/` (`wire_pure_tests`, `wire_dict_tests`, `dictionary_delimiter_census_test`). `[const §VII.8]` (`.specify/constitution.md:178`) requires label selection over `-R <exe-name>`; passing a *case* name to `-R` is neither form and matches nothing at all.

- **Assert a count on every selection — test *and* benchmark — and put the assertion where it can halt.** Verified 2026-08-03:

  ```
  $ ctest --test-dir build/linux-clang-debug -R TypedReadSplitAgreement ; echo $?
  No tests were found!!!
  0
  $ ./build/linux-clang-debug/bin/wire_pure_tests --gtest_filter='Nope.Nope' ; echo $?
  0
  $ ./build/linux-clang-release/bench/wire/typed_read_group_bench --benchmark_filter='NoSuchXYZ' ; echo $?
  Failed to match any benchmarks against regex: NoSuchXYZ
  0
  ```

  **All three** zero-match forms exit 0. Switching from `ctest -R` to `--gtest_filter` relocates the false-green rather than closing it, and `--gtest_fail_if_no_test_selected` does **not** exist in the vendored GoogleTest (checked — the flag is rejected as unknown). Only an explicit count assertion closes it, and only if the assertion **stops the block**. Every step below carries one, inside a subshell, chained `|| exit 1`. *(Corrected at Gate A round 2: the round-1 file asserted "**Every step below carries one**" while §3a — the benchmark step — carried **none**, and the assertions it did carry did not halt. Both are fixed; the benchmark leg is closed with `--benchmark_list_tests=true`, whose zero-match output goes to stderr and yields a count of 0.)*

- **Local verification is Clang-only.** The GCC-Release and MSVC jobs are CI-only and can fail on things a local run cannot see (A-007).

### Expected counts, and where they come from

| Selection | Expected | Basis |
|---|---|---|
| `ctest -L wire` | **4/4** | registered count via `ctest -N` **and observed green**, 45.99 s, 2026-08-03 |
| `ctest -L capi` | **22/22** | registered count via `ctest -N`, 2026-08-03 (matches 083's recorded 22/22) |
| `ctest -L dictionary` | **17/17** | registered count via `ctest -N`, 2026-08-03 (matches 083's recorded 17/17) |
| `ctest -R '^delimiter_census$'` | **1/1** | registered count via `ctest -N`, 2026-08-03 |
| `--gtest_filter='TypedReadSplitAgreement.*'` | **7** cases | `--gtest_list_tests`, 2026-08-03 |
| `--gtest_filter='DelimiterCensus.RedCountsReconcileWithSpecBaseline'` | **1** case | `--gtest_list_tests`, 2026-08-03 |
| `--gtest_filter='WireOffsetTable.DoSCapPerInstance*'` | **2** cases | `--gtest_list_tests`, 2026-08-03 |
| `--gtest_filter='WireOffsetTable.DictFreeDoSCapPerInstance*'` | **2** cases *(**0 today** — the pins do not exist yet)* | SC-004 names both cases; the count becomes assertable only after `/speckit-implement` lands them |
| `--gtest_filter='WireOffsetTable.FR001_SingleTraversalSourceInspection'` | **1** case *(**0 today** — the pin does not exist yet)* | FR-001b/SC-005b name it; the count becomes assertable once `/speckit-implement` authors it — which, for this one pin, happens **before** the relocation |
| `--benchmark_list_tests=true --benchmark_filter='BM_TypedReadGroup_(Flat2\|ModeC2\|ModeC8)'` | **3** cases | `--benchmark_list_tests`, 2026-08-03, `build/linux-clang-release` |

These are **registered** counts as-of `c1564dd2`, not a close-out run record. `ctest -L wire` is the one additionally observed green here; the rest are `-N` enumerations (the `capi` and `dictionary` buckets carry 600 s per-test timeouts and are re-run at `/speckit-verify`, which is where 083 pasted its observed columns too).

> **`ctest -L wire` stays 4 after this feature ships.** All **three** new cases (SC-004's two dict-free cap pins + FR-001b's structural pin) join an **existing** bucket (`offset_table_test.cpp` → `wire_pure_tests`) per `[const §VII.8]`, so the *CTest* count does not move — 5 would mean someone added a new executable, which FR-001a/plan Structure forbid. What moves is the *GoogleTest* count inside `wire_pure_tests`, which is why the new cases are asserted by `--gtest_filter` count, not by a bucket count.

> **How the guards halt — the one convention every block below follows.** `expect` **reports and returns non-zero; it does not itself halt.** So every code block is wrapped in a **subshell** `( … )` and every assertion is chained `expect … || exit 1`. A failed count then exits the subshell **before** the command it guards, in every invocation form — pasted into an interactive shell, or run as a script, under bash or zsh alike. Do **not** write `|| return 1` at top level: `return` is invalid outside a function, bash prints `return: can only 'return' from a function or sourced script` and **execution continues**; that was round 1's mechanism and it silently did nothing. Commands the block *expects* to fail — the mutation steps' RED test runs — are deliberately left unchained, so the failure output is produced and can be captured. *(Corrected and executed at Gate A round 2; see the banner at the top of this file for the four shells it was checked in.)*

---

## Prerequisites

**All commands in this file are relative to the library root** — the directory containing `CMakeLists.txt`, `src/`, `build/` and `.specify/`. Start there; there is no `cd` to perform. *(Round 1 printed `cd research/G19-fix-fpml-iso20022/library` here, which fails from the very root every other path in this file assumes. Removed at Gate A round 2.)*

```bash
cmake -S . -B build/linux-clang-debug          # reconfigure; reuses the cached Conan toolchain
cmake --build build/linux-clang-debug -j2      # -j2: wider parallelism OOM-kills the session
```

Paste these helpers once per shell; every step below uses them. Subshells inherit them, so each step's `( … )` block sees them without redefinition.

```bash
BIN=build/linux-clang-debug/bin
BD=build/linux-clang-debug
BENCH=build/linux-clang-release/bench/wire/typed_read_group_bench

# number of GoogleTest cases a filter selects (0 on no match; `|| true` because
# grep -c exits 1 on zero matches and would otherwise mask the count)
gt_count() { "$1" --gtest_list_tests --gtest_filter="$2" 2>/dev/null | grep -c '^  ' || true; }
# number of CTest tests a selection would run
ct_count() { ctest --test-dir "$BD" -N "$@" 2>/dev/null | sed -n 's/^Total Tests: //p'; }
# number of Google Benchmark cases a filter selects (0 on no match — the
# "Failed to match any benchmarks" line goes to stderr, so it is not counted)
bm_count() { "$1" --benchmark_list_tests=true --benchmark_filter="$2" 2>/dev/null | grep -c . || true; }
# report a count mismatch; ALWAYS chain it `|| exit 1` inside a ( … ) block
expect() { [ "$1" = "$2" ] || { echo "SELECTOR FAIL: expected $2, selected $1"; return 1; }; }
```

---

## 0. The red-first structural pin goes RED (FR-001b, SC-005b) — **before the relocation**

This is Article VII §3's failing-test-first artifact (`[const §VII.3]`, `.specify/constitution.md:173`) and the **only** step in this file that runs on the unmodified tree. Running it after the relocation does not discharge SC-005b — a structural pin that was green from birth proves nothing.

```bash
# 1. On the UNMODIFIED tree, author WireOffsetTable.FR001_SingleTraversalSourceInspection
#    in tests/wire/offset_table_test.cpp, and add FIXPP_SRC_DIR to the bucket in
#    tests/wire/CMakeLists.txt (mirrors tests/dictionary/CMakeLists.txt:178-183):
#      target_compile_definitions(wire_pure_tests PRIVATE
#        "FIXPP_SRC_DIR=\"${CMAKE_SOURCE_DIR}/src\"")
cmake -S . -B "$BD"
cmake --build "$BD" -j2
(
  expect "$(gt_count $BIN/wire_pure_tests 'WireOffsetTable.FR001_SingleTraversalSourceInspection')" 1 || exit 1
  $BIN/wire_pure_tests --gtest_filter='WireOffsetTable.FR001_SingleTraversalSourceInspection'
)
# 2. Capture the RED output. 3. THEN land the relocation. 4. Re-run: GREEN.
```

**Expected on the unmodified tree**: **1 case selected, and it FAILS.** The pin asserts the flat cap block sits inside `group()`'s dict-free `else`; on baseline it sits in the function body after the `if/else`, which is exactly what this feature moves.

**Why the discriminant works — measured, not assumed** (2026-08-03, `src/wire/offset_table.cpp` at `main` = `c1564dd2`): the flat block's own `std::size_t inst_start = first;` statement occurs **once at 4-space** function-body indentation (`:584`) and **zero times at 8-space** `else`-body indentation. `group_slices_status`'s unrelated `inst_start` (`:712`) sits at 16-space and matches neither key. So a pin asserting *"no 4-space occurrence, exactly one 8-space occurrence"* is RED today and GREEN after the move. `clang-format` is a Tier-1 gate (`[const §IX.4]`), so indentation is a stable signal in this tree; brace-region membership within `group()` is an equally available key if delivery prefers it — the requirement is the red-first *sequence*, not this particular token.

**The pin carries a SECOND key, and it is not redundant with the first.** `inst_start` proves the block **moved**; it says nothing about whether the block moved **unchanged**. An `else`-inverted relocation — `if (boundary) { … }` replacing `if (!boundary) continue;` — leaves `inst_start` at 8-space and passes key (a) while violating C-3 #4 and FR-001a. Key (b): **no** `"\n        if (!boundary) {\n"` (8-space), **exactly one** `"\n            if (!boundary) {\n"` (12-space). Measured at `c1564dd2`: 8-space **1**, 12-space **0**, 4-space **0**, 16-space **0**. Collision-free — `boundary` is an identifier at exactly two sites in the file, this block (`:586-587`) and `group_slices_status`'s walk (`:714-716`), the latter in the **positive** form at 20-space. Reflow-free — the key line is 24 chars, 28 after the shift, against `ColumnLimit: 100`. *(Added post-`/speckit-checklist-audit`, 2026-08-03. The inversion is the one C-3 item no behavioural test, sanitizer or corpus can observe — R-1 makes the dictionary-path flat re-walk unreachable-as-an-error, so semantics are identical either way — and the sibling walk's positive form makes "match the sibling" a natural edit. It was the bundle's single acknowledged blind spot; this closes it at zero runtime cost.)*

**Precedent — this is not a new kind of test.** `tests/dictionary/load_any_test.cpp:143-171` (`LoadAny.FR004_SingleSharedDispatchSourceInspection`) is the same construction already shipping: a permanent, behaviour-blind structural pin that slurps production source through `FIXPP_SRC_DIR` and asserts token presence/absence with plain `std::string::find`. **No AST, no new tooling.** Mirror its comment block, including the sentence saying *why* behaviour cannot distinguish the two states.

**Record** the RED output in `.specify/decisions/085-fold-flat-cap-loop-verify.md`, alongside the two mutation transcripts. A missing RED capture fails SC-005b regardless of how many tests are green afterwards.

---

## 1. Nothing changed on the dictionary path

### 1a. Census and split pins (SC-001)

```bash
(
  expect "$(ct_count -R '^delimiter_census$')" 1 || exit 1
  ctest --test-dir "$BD" -R '^delimiter_census$' --output-on-failure

  expect "$(gt_count $BIN/dictionary_delimiter_census_test 'DelimiterCensus.RedCountsReconcileWithSpecBaseline')" 1 || exit 1
  expect "$(gt_count $BIN/wire_dict_tests 'TypedReadSplitAgreement.*')" 7 || exit 1
  $BIN/wire_dict_tests --gtest_filter='TypedReadSplitAgreement.*'
)
```

**Expected**: `delimiter_census` **1/1 PASS**, with `DelimiterCensus.RedCountsReconcileWithSpecBaseline` reporting the **same baseline counts as `main`** — no fixture edit, no baseline update. All **7** `TypedReadSplitAgreement.*` cases pass (`7 tests from 1 test suite ran. [  PASSED  ] 7 tests.`).

**A fixture or baseline edit made to turn these green is a failure of SC-001, not a fix.** Their whole purpose is to be insensitive to this change.

### 1b. Corpus equivalence (SC-002)

```bash
(
  expect "$(ct_count -L wire)"       4  || exit 1
  expect "$(ct_count -L capi)"       22 || exit 1
  expect "$(ct_count -L dictionary)" 17 || exit 1

  ctest --test-dir "$BD" -L wire       --output-on-failure
  ctest --test-dir "$BD" -L capi       --output-on-failure
  ctest --test-dir "$BD" -L dictionary --output-on-failure
)
```

**Expected**: **4/4**, **22/22**, **17/17** — identical pass set to `main`, no fixture or expected-value edit. In particular the ~16 dict-free `OffsetTable{frame, mr}` tests inside `wire_pure_tests` stay green, including `offset_table_test.cpp:150-157`, which calls `group(453)` dict-free and asserts its extent. Those are what make C-3's preservation checklist checkable rather than asserted.

> **What this does and does not prove.** It proves *every asserted behaviour in the shipped corpora is unchanged*. It does **not** prove every successful call returned an identical value — many of these tests assert bounds rather than exact tuples (e.g. `EXPECT_GT(g->entry_count(), 0U)` at `offset_table_test.cpp:156`). The **universal** equivalence claim is carried by `research.md` **R-1**'s source argument plus §1c below, not by this run. SC-002 is worded to match.

### 1c. One traversal, and the move is semantics-preserving (SC-005, C-1a, C-3)

Discharged by **source inspection**, not a command:

```bash
git diff main -- src/wire/offset_table.cpp
```

**Expected shape**: the loop disappears from the dictionary branch and reappears inside the `else`, re-indented one level and otherwise unchanged. Walk `contracts/group_cap_accounting.md` C-3's **nine-item checklist** over the moved block — separate `inst_start` declaration, loop bounds, boundary predicate and disjunct order, the `continue`, the segment measure, the **strict** `>`, the immediate `err_group_too_large` return, the post-check re-anchor, the unchanged function return. A mechanical indent shift is expected (`clang-format` is a Tier-1 gate, `[const §IX.4]`); anything else — a merged declaration, an inverted condition, a helper — violates FR-001a even if every test is green.

Then confirm **C-1a** — and note that **`git diff` cannot show it**. C-1a's four anchors sit outside the relocation's hunks: `consume_group_extent:450,458` is ≈130 lines above them and `group():545,551` ≈30 lines above, so default diff context does not reach either. Display them directly against the base commit:

```bash
git show main:src/wire/offset_table.cpp | sed -n '450p;458p;545p;551p;575p'
```

**Expected**: `:450` and `:545` both `first = count_idx + 1U`; `:458` and `:551` both `delim = entries_[first].tag`; `:575` the `consume_group_extent(count_idx, …)` call that passes the *same* `count_idx` into the first pair. That is the removal's proof premise — identical by construction — and it is discharged here and carries nothing forward (see C-1a's rationale). Its **primary** discharge is `research.md` R-1 step 1, whose table carries the same four anchors with the derivation; this is the delivery-time re-check. *(Command added at Gate A round 2 — round 1 named `git diff` as C-1a's discharge, and `git diff` cannot display these lines.)*

### 1d. The FR-002 comment landed, and says the right thing (SC-005a) — **do not skip**

```bash
git diff main -- src/wire/offset_table.cpp | grep -n '^+.*//'
```

The comment is the **sole carrier** of the plan's only High-impact-risk mitigation, and until Gate A round 1 nothing in this bundle checked that it existed at all. Read it against SC-005a:

| Must state | Check |
|---|---|
| **Why the second walk went** — the cap is already applied inside the nesting-aware extent walk, over boundaries the flat partition merely refined, and that walk returns first on breach | present, and matches `research.md` R-1 |
| **What stands in its place (C-1)** — this branch's per-instance DoS defence is now *solely* `consume_group_extent`'s cap over the same instances whose extent it returns; a change to that walk must re-verify the cap measures that partition, or re-introduce an independent cap here | present |
| Anchoring (FR-007b) — each site named by **function and role first**, any line number appended and stamped as-of the merge sha | every reference |

**FAIL the step** if the comment instead states, as a *standing* rule, that `group()`'s and `consume_group_extent`'s delimiter sources must stay equal. After the removal that equality governs group *recognition* (`:566`), not cap accounting — and `group_slices_status:704-715` already resolves its delimiter from the dictionary store over the same extent, benignly, on shipped `main`. Writing the old claim into the source would enshrine a false consequence for exactly the maintainer most likely to touch these lines.

---

## 2. The cap still fires — and both pins are load-bearing

### 2a. Dictionary path (SC-003)

```bash
(
  expect "$(gt_count $BIN/wire_pure_tests 'WireOffsetTable.DoSCapPerInstance*')" 2 || exit 1
  $BIN/wire_pure_tests --gtest_filter='WireOffsetTable.DoSCapPerInstance*'
)
```

**Expected**: **2 cases, both pass** — `DoSCapPerInstanceRejectsOversizedSingleInstance` (breach → `wire_group_too_large`) and `DoSCapPerInstanceAllowsAggregateOverCap` (aggregate over cap, no single instance over it → success).

### 2a-mut. Prove the dictionary pin RED (SC-003, FR-005b) — **mandatory, and ORDER-DEPENDENT**

After this feature the dictionary path has **no other cap**. A pin never observed RED there proves nothing, and it is the test guard for contract **C-1**.

> ### ⚠ PRECONDITION: run this **only after the relocation has landed**, never on baseline.
>
> **Verified empirically, 2026-08-03, on `main` = `c1564dd2`:** deleting `consume_group_extent`'s per-instance comparison (`:521-524`) on baseline leaves the pin **GREEN** —
>
> ```
> [ RUN      ] WireOffsetTable.DoSCapPerInstanceRejectsOversizedSingleInstance
> [       OK ] WireOffsetTable.DoSCapPerInstanceRejectsOversizedSingleInstance (0 ms)
> [  PASSED  ] 1 test.
> ```
>
> That is **correct behaviour, not a defect**. On baseline the flat cap loop still sits after the `if/else` at `:584-595` and runs on the dictionary path too, so it catches the same breach one branch later and returns the same `wire_group_too_large` from `:592`. The RED only becomes available once the loop is gone from that branch — which is precisely why this mutation is *the* discharge for C-1 and could not have been one before.
>
> **Sequence**: `/speckit-implement` lands the removal → §1 confirms nothing changed → **then** this step. Running it on baseline produces a false alarm and, read against the stop-condition below, would send the reader chasing a fixture bug that does not exist.
>
> *(Bonus witness — record it: the baseline green above is independent evidence for `research.md` R-1 steps 2–4. It shows the flat loop genuinely does catch what the nesting-aware cap catches on this shape, i.e. the redundancy this feature removes is observable, not merely argued.)*

```bash
# PRECONDITION: the relocation has landed; §1 is green.
# 1. In src/wire/offset_table.cpp, delete consume_group_extent's per-instance
#    comparison — the `if ((k - inst_start) > cfg_.max_group_entries_per_instance)
#    { overflow = true; return k; }` block (`:521-524` as-of main = c1564dd2).
(
  cmake --build "$BD" -j2 || exit 1
  expect "$(gt_count $BIN/wire_pure_tests 'WireOffsetTable.DoSCapPerInstanceRejectsOversizedSingleInstance')" 1 || exit 1
  # deliberately NOT chained — this run is EXPECTED to fail, and its output is the artifact
  $BIN/wire_pure_tests --gtest_filter='WireOffsetTable.DoSCapPerInstanceRejectsOversizedSingleInstance'
)
# 2. Capture the failure output verbatim.
# 3. Restore the block; rebuild; re-run; confirm green.
```

**Expected**: the pin **fails** — the over-cap frame is accepted instead of returning `wire_group_too_large`. Record the mutation applied, the selected count (**1**), and the failure output in `.specify/decisions/085-fold-flat-cap-loop-verify.md`.

**What this transcript proves, and what it does not.** It proves the dictionary path's **cap check is load-bearing** — enforcement exists there and deleting it is observable. It does **not** prove contract **C-1**'s partition coupling: this fixture (`tests/wire/offset_table_test.cpp:199-235`) is `453=1` with a single *unnested* instance, so its flat and nesting-aware partitions are identical and no whole-check deletion can tell them apart. Do not write the wider claim into the verify record. *(Narrowed at Gate A round 2.)*

**If the pin stays GREEN with the comparison deleted, diagnose in this order:**

1. **Was the relocation actually applied?** This is the overwhelmingly likely cause and the one measured above — if the flat loop is still on the dictionary branch, it pre-empts and green is the *correct* result. Re-check §1c's diff (and §0's pin, which is GREEN only post-relocation) before anything else.
2. Only if the removal is confirmed landed: the fixture is not breaching the cap, or the mutation was applied to the wrong site.

A **0**-count selection cannot reach either diagnosis: the `expect` above exits the block first, so the test command never runs.

### 2b. Dict-free path (SC-004)

```bash
(
  expect "$(gt_count $BIN/wire_pure_tests 'WireOffsetTable.DictFreeDoSCapPerInstance*')" 2 || exit 1
  $BIN/wire_pure_tests --gtest_filter='WireOffsetTable.DictFreeDoSCapPerInstance*'
)
```

**Expected after the pins land**: **2 cases, both pass** — `DictFreeDoSCapPerInstanceRejectsOversizedInstance` fails the over-cap frame with `wire_group_too_large`, and `DictFreeDoSCapPerInstanceAllowsWhenCapRaised` succeeds on the **same frame** with `max_group_entries_per_instance` raised above it.

> **This count is 0 today** and the `expect` will fail until `/speckit-implement` adds the two cases. That is the intended state, not a defect in the procedure — the names are fixed by SC-004 precisely so the selector and its count are deterministic once they exist.

> The frame needs **both** dict-free construction **and** a tightened `Config` — e.g. `tight_cfg{.max_offset_entries = 4096, .max_group_entries_per_instance = 3}`. Under default `Config` this branch is arithmetically unreachable (`research.md` R-2), which is exactly why no such test existed before.

### 2c. Prove the dict-free pin RED (SC-004a, FR-005a(i)) — **mandatory**

**This step and §2a-mut cannot be skipped.** A cap pin never observed RED proves nothing.

> **PRECONDITION — also post-relocation.** Like §2a-mut, this step runs inside `/speckit-implement` *after* the relocation and after §2b's two pins exist. It cannot run on baseline at all: the pins are not there (§2b's count is 0 today). Stated here rather than only in §2b's note, because a reader working top-to-bottom reaches this step first in a mutation mindset.

```bash
# PRECONDITION: the relocation has landed and §2b's two pins exist and are green.
# 1. Delete the cap check inside the dict-free branch of OffsetTable::group()
#    (the `if (inst_count > cfg_.max_group_entries_per_instance) return ...`).
(
  cmake --build "$BD" -j2 || exit 1
  expect "$(gt_count $BIN/wire_pure_tests 'WireOffsetTable.DictFreeDoSCapPerInstanceRejectsOversizedInstance')" 1 || exit 1
  # deliberately NOT chained — this run is EXPECTED to fail, and its output is the artifact
  $BIN/wire_pure_tests --gtest_filter='WireOffsetTable.DictFreeDoSCapPerInstanceRejectsOversizedInstance'
)
# 2. Capture the failure output verbatim.
# 3. Restore the check; rebuild; re-run; confirm green.
```

**Expected**: the pin fails with the group request unexpectedly succeeding. Record the mutation applied, the selected count (**1**), and the failure output in `.specify/decisions/085-fold-flat-cap-loop-verify.md` and cite it from the test's comment block.

**If the pin stays GREEN with the check deleted, stop.** The test is not exercising the branch — most likely the frame is not reaching the dict-free path, or `Config` was not tightened. A **0**-count selection cannot produce this symptom at all: the `expect` above exits the block before the test command runs.

---

## 3. No regression

### 3a. Benchmark (SC-006)

Two legs. **Leg 1 is the constitutional check** (`[const §VIII.2]`, `.specify/constitution.md:185` — ±5% vs `bench/baselines/`); **leg 2 is the same-session A/B**, retained as noise control. Round 1 shipped only leg 2 while the plan's VIII §2 row cited it as leg 1; corrected at Gate A round 2.

**Leg 1 — against the checked-in baseline.** Run at the baseline file's **own** recorded parameters (`bench/baselines/wire/typed_read_group_bench.json` → `context`: `repetitions: 9`, `min_time_s: 0.5`, `build_type: release`, host `WSLUBUNTU24.04`).

```bash
FILTER='BM_TypedReadGroup_(Flat2|ModeC2|ModeC8)'

# TAG names the revision under measurement. Leg 2 runs this block TWICE and the
# two runs MUST NOT share an output path — see the leg-2 warning below.
TAG=main                      # leg 2, second run: TAG=branch
OUT="/tmp/085-bench-$TAG.json"

(
  set -u
  # Every step chained: a release-only compile failure must not fall through to
  # a stale binary. §3a is the ONLY step that builds release, so nothing else
  # would catch it.
  cmake -S . -B build/linux-clang-release                                 || exit 1
  cmake --build build/linux-clang-release --target typed_read_group_bench -j2 || exit 1

  expect "$(bm_count $BENCH "$FILTER")" 3 || exit 1

  # Freshness, both ends: remove any prior artifact, then require the run to
  # have produced one. Google Benchmark exits non-zero on an unopenable
  # --benchmark_out ("invalid file name"), and WITHOUT the `|| exit 1` the
  # parse below would silently read the PREVIOUS run's file and print three
  # plausible in-budget rows at status 0.
  rm -f "$OUT"
  $BENCH --benchmark_filter="$FILTER" \
         --benchmark_repetitions=9 --benchmark_min_time=0.5s \
         --benchmark_format=json --benchmark_out="$OUT" \
         --benchmark_out_format=json > /dev/null                          || exit 1
  [ -s "$OUT" ] || { echo "FRESHNESS FAIL: $OUT was not produced"; exit 1; }

  python3 - bench/baselines/wire/typed_read_group_bench.json "$OUT" <<'PY'
import json, sys
base = {b["name"]: b.get("seed_median_ns") for b in json.load(open(sys.argv[1]))["benchmarks"]}
cur  = {b["name"].removesuffix("_median"): b["cpu_time"]
        for b in json.load(open(sys.argv[2]))["benchmarks"] if b.get("aggregate_name") == "median"}
for n in ("BM_TypedReadGroup_Flat2", "BM_TypedReadGroup_ModeC2", "BM_TypedReadGroup_ModeC8"):
    b, c = base[n], cur[n]
    d = (c - b) / b * 100.0
    print(f"{n:<28} baseline {b:>5} ns   current {c:>8.1f} ns   {d:+6.1f}%"
          f"   {'OVER +/-5%' if abs(d) > 5.0 else 'within +/-5%'}")
PY
)
```

*(Binary path verified 2026-08-03 — the target sets `RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bench/wire"`, `bench/wire/CMakeLists.txt:148-150`, so it is **not** under `bin/`. `--benchmark_min_time=0.5s` — the `s` suffix is required by the vendored Google Benchmark and was accepted without warning; the `_median` aggregate row is emitted with `time_unit: ns`. Both checked by running the block.)*

> **Why `tools/bench_compare.py` is not used here, despite being the repo's comparator.** It reads each benchmark's **`cpu_time`** (`load_benchmarks`), and this baseline file does not carry that key — its numbers live under **`seed_median_ns`** (and `pre083_median_ns`). Run against it, the comparator's own null-guard prints `N/A` for all three rows and computes no delta. The comparison above is therefore done directly against the field the baseline actually has. Neither `bench/baselines/` nor `tools/bench_compare.py` is modified: no baseline update (this is not an intentional perf change), and the comparator's docstring already declares itself a **soft** gate (*"Always exits 0 (SOFT gate until Phase 4 module 1)"*), so nothing is being bypassed. *(Found at Gate A round 2 by running the reviews' own recommended command; neither review caught it.)*

**Expected**: exactly **3** cases selected, and three rows printed. **Read leg 1 together with leg 2, not alone.** Three back-to-back runs of exactly this block on **unmodified `main`**, 2026-08-03, same host as the baseline's `context`:

| Run | `Flat2` | `ModeC2` | `ModeC8` |
|---|---|---|---|
| 1 | **+7.1%** | +1.8% | **+5.8%** |
| 2 | +4.0% | +4.9% | +2.2% |
| 3 | −2.4% | +2.1% | +2.7% |

That is a ~9-point spread on `Flat2` against a ±5% budget, on code **identical** to the baseline's — two of nine readings fell outside budget with nothing changed.

**A second reason, also measured: the baseline does not say which clock its `seed_median_ns` came from.** On the third run above, the `_median` rows' `real_time` sat **3.7–4.6% below** their `cpu_time` (`359.4`/`648.7`/`1623.8` vs `376.8`/`675.1`/`1686.6` ns). Read as `real_time`, `Flat2` is −6.9%; as `cpu_time`, −2.4%. The block above fixes the comparison on **`cpu_time`**, because that is the field `tools/bench_compare.py::load_benchmarks` reads and therefore this repo's convention — but a ±4% clock ambiguity on a ±5% budget is the second reason a lone leg-1 row is not a finding.

**So leg 1's disposition is explicit, per SC-006:** all three rows within ±5% on a recorded run ⇒ **PASS**. Otherwise ⇒ **`WITHIN-NOISE`**, which requires the same block re-run against **unmodified `main`** in the same session with those control numbers attached, *and* leg 2 within ±5%. Leg 1 alone never blocks and never clears; **a >±5% leg-2 A/B delta is the blocking signal.** Recorded this way because a procedure that presented leg 1 as a crisp pass/fail would hand `/speckit-implement` a false regression on its first red reading.

**Leg 2 — same-session `main`-vs-branch A/B.** Run the same block on `main` and on the branch, **same machine, same session**, and put both sets of numbers in the PR body. This is what isolates the delta attributable to the relocation from host drift, and a >±5% **A/B** delta is the blocking signal. A null result is a **pass** — the bench exists to prove no regression, not to claim a win (A-004). Do not add a bench case tuned to make the removal look good.

> ⚠️ **Set `TAG=main` for the first run and `TAG=branch` for the second — never reuse one path.** Leg 2 is the *blocking* signal, and it is the leg a shared output path silently destroys: if the branch run fails after the `main` run succeeded, a single hard-coded `/tmp/085-bench.json` leaves the branch comparison reading `main`'s numbers, yielding a ≈0% A/B delta and a **pass on identical data**. Distinct `TAG`s plus the `rm -f` / `[ -s "$OUT" ]` pair close that; the `|| exit 1` chain closes the single-leg case. *(Found at Gate A round 3 by executing the block against a pre-existing output file — the failure was reproduced, not hypothesised, and two of its three vectors were missed by the review that first reported it.)*

Compare the two runs by name:

```bash
(
  set -u
  for f in /tmp/085-bench-main.json /tmp/085-bench-branch.json; do
    [ -s "$f" ] || { echo "A/B FAIL: $f missing or empty"; exit 1; }
  done
  python3 - /tmp/085-bench-main.json /tmp/085-bench-branch.json <<'PY' || exit 1
import json, sys

NAMES = ("BM_TypedReadGroup_Flat2", "BM_TypedReadGroup_ModeC2", "BM_TypedReadGroup_ModeC8")

def medians(p):
    return {b["name"].removesuffix("_median"): b["cpu_time"]
            for b in json.load(open(p))["benchmarks"]
            if b.get("aggregate_name") == "median"}

a_all, b_all = medians(sys.argv[1]), medians(sys.argv[2])

# Decide ONCE, over all three, before printing anything. A per-name loop in the
# SHELL cannot do this: each python process's exit status is overwritten by the
# next iteration, so a missing FIRST case prints its error and still ends at
# status 0 (order-dependent — a missing LAST case would happen to fail).
missing = [f"{n} absent from {p}"
           for n in NAMES
           for p, d in ((sys.argv[1], a_all), (sys.argv[2], b_all)) if n not in d]
if missing:
    sys.exit("A/B FAIL:\n  " + "\n  ".join(missing))

rows, blocked = [], False
for n in NAMES:
    a, b = a_all[n], b_all[n]
    d = (b - a) / a * 100.0
    over = abs(d) > 5.0
    blocked |= over
    rows.append(f"{n:<28} main {a:>8.1f} ns   branch {b:>8.1f} ns   {d:+6.1f}%"
                f"   {'BLOCKS (>+/-5% A/B)' if over else 'within +/-5%'}")
print("\n".join(rows))
if blocked:
    sys.exit("A/B BLOCKS: at least one case outside +/-5%")
PY
)
echo "ab_status=$?"
```

**Expected**: three rows, all `within +/-5%`, and `ab_status=0`.

> ⚠️ **Why this is one invocation and not a `for` loop.** The first version of this comparator ran one `python3` per name inside a three-iteration shell loop. `med()` raised correctly on a missing case — and the loop then **overwrote its status with the next iteration's**, printing the remaining rows and exiting 0. Worse, it was order-dependent: a missing *last* case would have failed, a missing *first* case would not. Deciding all three inside a single process, before any row is printed, is what makes a truncated run unable to read as a pass. *(Reproduced under both bash and zsh in the fresh Gate A round — the fix for the round-3 finding had itself reintroduced the same false-green class it was written to close. Fourth occurrence in this section; see `plan.md` `## Gate A`.)*

### 3b. Sanitizers and static analysis (SC-008) — **not runnable from here; handed off**

There is **no sanitizer build directory in this tree** (`build/` holds only `linux-clang-debug`, `linux-clang-release`, `linux-gcc-release`, `wheel-local`), and `cmake --preset` is broken (see Build constraints), so the ASan/UBSan/TSan legs cannot be configured by a command this file could honestly print. Writing three that cannot run is the exact defect this quickstart was rewritten to remove.

| Owner | What it runs |
|---|---|
| **`/speckit-verify` Step 1–2** | `clang-tidy` / `clang-format` / `cppcheck` / `iwyu` / `check_layers.py` on the changed files, then the preset matrix `linux-clang-{debug,release,asan,ubsan,tsan,coverage}` + `linux-gcc-release`, each configured fresh |
| **Tier-1 CI** | the same matrix as a merge gate, plus the legs a local clang-only run cannot see (A-007) |

**Expected**: clean. No new constructs are introduced, so any sanitizer finding here is a signal that the relocation was not semantics-preserving — re-read C-3's checklist before suspecting the sanitizer.

Coverage (`[const §IX.1]`) is measured in the same place and recorded in `.specify/decisions/085-fold-flat-cap-loop-verify.md`; it is deliberately not a quickstart step, matching `specs/083-group-delimiter-resolution/quickstart.md`, which has no coverage section either.

### 3c. ABI hygiene (SC-007)

```bash
git diff --name-only main -- include/ src/capi/
```

**Expected**: **empty output**. Zero change to exported symbols, public headers, error enum values and the C-ABI version follows from touching no file on that surface; `include/fixpp/wire/offset_table.hpp` in particular must be untouched. The `abidiff`/`nm` gates at `/speckit-verify` and in Tier-1 CI remain the authority. *(A command was added at Gate A round 1 — this step previously stated an "Expected" with nothing to produce it.)*

---

## 4. Documentation obligations

Not runnable, but they gate close-out:

| Check | Requirement |
|---|---|
| L-063-4 records leg 2 DELIVERED, leg 1's descope evidence intact | FR-006, SC-009 |
| Both surviving flat sites named — **with their differing delimiter sources** (wire vs the per-context store) | FR-007, FR-007a |
| References anchored by function and role; line numbers stamped as-of the merge sha; historical brackets byte-unchanged — **including the FR-002 source comment** | FR-007b |
| Limitation row cites **fixpp#220** and states the default-config unreachability | FR-003a, SC-010 |
| The stale waiver at `tests/wire/offset_table_error_path_test.cpp:10-14` repaired — its claim that `group()`'s `err_group_too_large` is "provably unreachable" is now false for `:577` and, after this feature, for the relocated branch too | research.md R-3 |

---

## Failure triage

| Symptom | Likely cause |
|---|---|
| A selector `expect` fails with `selected 0` | Wrong name, wrong binary, or a stale build. **Never** re-run without the count check — a 0-match run exits 0. The block has already exited; the guarded command did not run |
| `TypedReadSplitAgreement.*` red | The relocation altered the dictionary path. Check the loop was removed, not the `consume_group_extent` call |
| A dict-free test red | The relocation was not semantics-preserving — re-walk C-3's nine-item checklist |
| §0's structural pin **green** on the unmodified tree | The pin is not discriminating — it would be green from birth, which discharges nothing (SC-005b). Check **both** keys against §0's measured counts (4-space `inst_start` = 1, 8-space = 0 at `c1564dd2`) before suspecting the build |
| The **dictionary** pin green under mutation (§2a-mut) | **Check the relocation FIRST — this is ordering, not reachability.** On baseline the frame *does* reach `:521-524`; the pin stays green because the flat loop at `:584-595` is still on the dictionary branch and returns the identical `wire_group_too_large` from `:592` one branch later (measured — FR-005b). Confirm §1c's diff and §0's now-green pin. **Only after** the removal is confirmed landed should you suspect the fixture or a mis-applied mutation. *(Row split and corrected at Gate A round 2 — it previously attributed this symptom to the frame not reaching the site, contradicting §2a-mut's own triage in this same file and sending the reader after a fixture bug that does not exist.)* |
| The **dict-free** pin green under mutation (§2c) | Here reachability *is* the likely cause: `Config` was not tightened below the segment size, or the table is dictionary-backed so the `else` branch is never taken |
| `DelimiterCensus` red | Something re-pointed a delimiter. Nothing in this feature should touch delimiter resolution — and note that a **divergent delimiter source** is not by itself a cap defect (see C-1) |
| Bench leg 1 (vs `bench/baselines/`) regressed >5% | Unexpected for a removal, **and this host drifts ~3pp run-to-run** (§3a, measured on unmodified `main`). Re-run leg 1, then read leg 2's same-session A/B — that is the discriminating measurement. Suspect a build/config difference before suspecting the change |
| Bench leg 2 (same-session A/B) regressed >5% | This one *is* attributable to the change. Confirm both runs used the same build type and the same `--benchmark_repetitions`/`--benchmark_min_time` before triaging the code |
