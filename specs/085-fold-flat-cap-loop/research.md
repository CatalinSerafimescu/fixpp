# Phase 0 Research: 085-fold-flat-cap-loop

**Date**: 2026-08-03 · **Branch**: `085-fold-flat-cap-loop` · **Base**: `main` = `c1564dd2`

This document discharges **A-001**, which obliges `/speckit-plan` to *re-verify* the spec's four-step redundancy argument against the tree it plans on rather than inherit the conclusion. Every claim below was re-derived from `src/wire/offset_table.cpp` and `include/fixpp/wire/offset_table.hpp` at `c1564dd2` during this phase. Where a re-verification **changed** a spec claim, it is called out as such.

---

## R-1 — A-001 re-verification: the four-step redundancy argument

**Decision**: The argument **holds, all four steps**. FR-001/FR-002 stand as written. The dictionary path's flat cap loop is unreachable-as-an-error and its removal is a strict no-op on observable behaviour.

### Step 1 — both walks derive `delim` from the same entry

| Site | Code | `first` |
|---|---|---|
| `group()` | `:545` `first = count_idx + 1U`; `:551` `delim = entries_[first].tag` | `count_idx + 1` |
| `consume_group_extent` | `:450` `first = count_idx + 1U`; `:458` `delim = entries_[first].tag` | `count_idx + 1` |

`group()` passes its own `count_idx` to `consume_group_extent` at `:575`, so both compute `first` from the same value and read the same entry. **Identical by construction, not by coincidence.** ✅

*Note for Gate A — corrected at Gate A round 1.* This step is a **premise of the one-time removal**, not a standing invariant, and the earlier version of this note said otherwise. 083 moved the **splitter's** delimiter to the dictionary store (`:704-711`) but deliberately left `group()`'s and `consume_group_extent`'s wire-derived (L-063-4's "the splitter is still flat"). While the flat loop is still present, re-pointing *one* of `:551` / `:458` without the other would break the superset relation and void FR-002's no-op claim — which is exactly why the equality has to hold **at the commit the removal is judged on**. Once the loop is gone, it stops mattering: there is no second partition left for two keys to disagree about, and `group():551`'s only remaining dictionary-path use is the recognition gate at `:566`. So the equality is recorded as **C-1a**, a delivery-time proof premise discharged by inspection, and **C-1** is restated as the property that survives — `consume_group_extent` must cap the same nesting-aware instances whose extent it returns. See `contracts/group_cap_accounting.md` and R-7 below.

### Step 2 — the flat boundary set is a superset of the nesting-aware one

`consume_group_extent` opens an instance **only** at an index whose tag equals `delim` — the outer `while` condition at `:477` (`entries_[k].tag == delim`) is what admits each iteration, and `inst_start = k` is taken at `:478` immediately inside it.

The flat loop fires a boundary at `:586` for **every** `k` in `(first, group_end]` with `entries_[k].tag == delim`, plus unconditionally at `k == group_end`.

For `k == first` the flat loop does not fire (guarded `k > first`), but it initialises `inst_start = first` at `:584`, which is the same segment start. So:

> **{nesting-aware instance starts} ⊆ {flat boundaries} on `[first, group_end]`.** ✅

### Step 3 — therefore flat segments refine nesting-aware instances

Both walks cover exactly `[first, group_end]`: `group_end` **is** `consume_group_extent`'s return value (`:575`), and the flat loop's range is `[first, group_end]` (`:585`). With a superset of cut points over an identical interval, every flat segment lies inside exactly one nesting-aware instance. Hence

```
inst_count at :590   ≤   (k - inst_start) at :521      for the enclosing instance
```

**This is not a vacuous refinement.** On the 485 contexts where the outer delimiter is itself a nested group's count tag (083 leg (c): FIX50SP2 240 + Orchestra 245), the flat walk fires boundaries *inside* the nested extent that the nesting-aware walk correctly treats as interior — a strictly finer partition. That is the direction that makes flat segments smaller, never larger. ✅

### Step 4 — the nesting-aware walk runs first and returns on breach

`:521-524` sets `overflow = true` and returns the moment any instance exceeds the cap; `group()` converts that to `err_group_too_large` at `:576-578`, **before** `:584`. Combined with step 3: if a flat segment could exceed the cap, the enclosing nesting-aware instance already did, and `group()` already returned. ✅

**Empirical corroboration (added at Gate A round 1).** Deleting `:521-524` on `main` = `c1564dd2` and re-running the dictionary cap pin `WireOffsetTable.DoSCapPerInstanceRejectsOversizedSingleInstance` leaves it **GREEN** — the flat loop at `:584-595` catches the identical breach and returns the identical `wire_group_too_large` from `:592`. That is the redundancy of steps 2–4 observed rather than argued: on that fixture the two walks reach the same verdict independently. It also establishes an **ordering constraint** that the bundle's own mutation procedure has to honour — FR-005b's RED is only obtainable *after* the relocation, because until then the flat loop pre-empts. See FR-005b and `quickstart.md` §2a-mut.

### Degenerate exits — all five checked

| `consume_group_extent` exit | Line | Reachable from `group()`? | Effect on the flat loop |
|---|---|---|---|
| depth ≥ `kMaxGroupDepth` | `:446-449` | Yes | `overflow` → returns at `:577`; loop unreached |
| `first >= entries_.size()` | `:451-453` | **No** — `group()` guards at `:546` | — |
| dict-free | `:454-456` | **No** — `group()` guards at `:553` | — |
| `delim` not a member | `:461-462` | **No** — `group()` guards at `:566` | — |
| `declared == 0` | `:465-467` | Yes | returns `first`, so `group_end == first`; the loop's single iteration measures `0` |

*Bound, stated precisely (corrected at Gate A round 1):* the outer `while` at `:477` is `k < entries_.size() && inst < declared && entries_[k].tag == delim` and the inner member scan breaks on a non-member at `:503-504`, so `consume_group_extent` consumes **up to** `declared` delimiter-opened instances — bounded by the entry table and by membership termination — not *exactly* `declared`. Nothing in steps 1–4 depends on the difference: fewer instances means a smaller `[first, group_end]`, and the superset relation is over whatever interval is actually returned. `spec.md`'s Walk-1 description and its lying-count edge case carry the same correction.

**Alternatives considered**: taking the spec's argument as given (rejected — A-001 exists precisely because an inherited proof is not a proof); proving it empirically by differential-testing before/after over the corpora (rejected as the *primary* basis — it is evidence, not proof, and is anyway delivered as SC-002; but it is retained as the cross-check).

---

## R-2 — CORRECTION to the spec: the dict-free breach is unreachable under default configuration

**Decision**: FR-003a's severity as first drafted was **wrong** and has been corrected in `spec.md`. Recorded here because the correction is load-bearing for both the coverage plan (R-3) and the issue text.

```
include/fixpp/wire/offset_table.hpp:27   default_max_offset_entries            = 4096
include/fixpp/wire/offset_table.hpp:28   default_max_group_entries_per_instance = 4096
src/wire/offset_table.cpp:326            if (entries_.size() >= cfg_.max_offset_entries) { ... }
```

The two defaults are **equal**, and `build()` clamps the table at the first. The dict-free segment is bounded by `entries_.size() - first ≤ 4095 < 4096`, so `inst_count > cap` is **arithmetically impossible** under default `Config`.

The repository had already derived this, for this exact call site, in a coverage waiver:

> `tests/wire/offset_table_error_path_test.cpp:13-14` — *"`group()` `err_group_too_large` — provably unreachable: max table entries = 4096, so avail ≤ 4095 < `default_max_group_entries_per_instance`"*

**What survives**: the defect is real but **config-dependent**. Both bounds are public `Config` members (`offset_table.hpp:94-95`), and tightening the per-instance cap while leaving the table cap at its default is a natural hardening choice — exactly the configuration that turns the trailing-field over-count into a false rejection. Three existing tests already build that shape for other reasons (`tight_cfg{.max_offset_entries = 4096, .max_group_entries_per_instance = 3}` — `offset_table_test.cpp:186,225`, `group_slice_trailing_soh_test.cpp:234`).

Filed as **fixpp#220** with this framing. `SC-010` now fails a record that presents it as a default-path defect, symmetric with failing one that omits it.

**Alternatives considered**: filing it as a live public-API defect (rejected — false, and it would have mis-prioritised triage); dropping it as theoretical (rejected — the reachable `Config` is a *sensible* one, not a contrived one).

---

## R-3 — The removed line is currently dead in the whole suite; the relocation makes it live

**Decision**: This feature **improves** coverage rather than threatening it, and retires a stale waiver. Article IX §1's binding rule ("no silent uncovered error/edge path") is satisfied by construction.

Today `group()` has **two** `err_group_too_large` returns:

| Return | Origin | Covered today? |
|---|---|---|
| `:577` | `consume_group_extent` overflow (`:521-524` cap, `:446-449` depth) | **Yes** — `WireOffsetTable.DoSCapPerInstanceRejectsOversizedSingleInstance` (`offset_table_test.cpp:198-235`) drives it with `tight_cfg` |
| `:592` | the flat cap loop | **No** — dead in the entire suite |

`:592` is dead because it needs dict-free construction **and** a tightened `Config` **simultaneously**, and no test does both: every `tight_cfg` test goes through `Parser` + `table_view` (dictionary path, where step 4 pre-empts it), and every dict-free test uses default `Config`.

After this feature: `:592`'s successor lives only on the dict-free branch, and the **new** FR-004 test supplies exactly the missing combination — dict-free + tight `Config` — making it live for the first time.

**Consequential finding — the waiver at `offset_table_error_path_test.cpp:10-14` is stale in two ways** and must be repaired (not merely re-pointed):

1. Its line numbers (`125-127`, `157-158`, `183-184`, `231-232`) are from a pre-063 revision of a file that is now 700+ lines. Already wrong before this feature.
2. Its *claim* is now partly false: it waives "`group()` `err_group_too_large`" as a whole, but `:577` has been **covered** since 063 shipped `consume_group_extent`'s cap. Only `:592` is unreachable, and this feature makes even that one reachable.

Leaving it would be a waiver asserting unreachability for a branch a delivered test exercises — the [`feedback_coverage_push_enshrines_bugs`] failure shape inverted. Scoped as a task.

**Alternatives considered**: re-pointing the line numbers only (rejected — preserves the false claim); deleting the waiver block (rejected — three of its four entries are still valid and load-bearing).

---

## R-4 — Delivered code shape

**Decision**: Per FR-001a (clarified 2026-08-03; wording corrected at Gate A round 1), the loop moves into the dict-free `else` branch **without semantic change**, re-indented one level and otherwise untouched. `consume_group_extent` is not touched.

**The block being moved, quoted verbatim from `src/wire/offset_table.cpp:584-595` at `main` = `c1564dd2`:**

```cpp
    std::size_t inst_start = first;
    for (std::size_t k = first; k <= group_end; ++k) {
        bool const boundary = (k == group_end) || (k > first && entries_[k].tag == delim);
        if (!boundary) {
            continue;
        }
        std::size_t const inst_count = k - inst_start;
        if (inst_count > cfg_.max_group_entries_per_instance) {
            return err_group_too_large<group_index>();
        }
        inst_start = k;
    }
```

> **Corrected at Gate A round 1.** This section previously showed the block as `for (std::size_t k = first, inst_start = first; k <= group_end; ++k) { ... }` — a **rewrite**, not the source: it merges two declarations into the for-init and changes `inst_start`'s scope. The one artifact showing the delivered shape showed a shape that violated the bundle's own FR-001a/C-3. The text above is the real block, pasted from the tree.

**Delivered shape** (`:553-596` at `c1564dd2` — dictionary branch, `else` branch, shared return):

```
  if (opaque_dict_ != nullptr && group_member_fn_ != nullptr) {
      ...  membership check at :566 (unchanged — still needs `delim`)
      group_end = consume_group_extent(count_idx, ctx, ctx.depth, overflow);
      if (overflow) { return err_group_too_large<group_index>(); }
      // FR-002 comment lands HERE. Two parts (SC-005a): (1) why the second
      // walk went — R-1's redundancy argument; (2) what stands in its place —
      // C-1: this branch's cap is now SOLELY consume_group_extent's, applied
      // over the same nesting-aware instances whose extent it returns.
  } else {
      group_end = entries_.size();
      // relocated, SOLE cap enforcement on this path — the eleven lines
      // quoted above, indented one level, nothing else changed (C-3's
      // nine-item checklist).
  }
  return group_index{no_tag, first, group_end - first};
```

**On "byte-identical".** FR-003 originally claimed the dict-free preservation was byte-identical. It cannot be: the block sits at 4-space function-body indentation and the `else` body is 8-space, so relocation necessarily re-indents, and `clang-format` (`[const §IX.4]`) will force it. FR-001a and C-3 now state the rule as an explicit **semantic-preservation checklist** — nine named elements, mechanical re-indentation permitted, nothing else. That keeps FR-003 discharged by a bounded diff inspection rather than silently reverting it to the equivalence argument the bundle set out to avoid.

Ordering note (FR-009): `group_end` is assigned before the loop in the `else` branch, exactly as today, so no work is added on either path and the `return` at `:596` is unchanged.

**Alternatives considered**:
- *Remove `consume_group_extent`'s dict-free bail so one traversal serves both paths* — the most literal reading of "fold". Rejected at `/clarify` Q1: with no dictionary that walk can be neither membership-driven nor nesting-aware, so it would embed a flat mode inside a function whose entire contract is that it is not flat, and would put FR-008 in play for no behavioural gain.
- *Extract a shared cap helper* — rejected: an abstraction over a two-line comparison, and it would make C-3's preservation checklist un-walkable over the diff.

---

## R-5 — Test inventory: what must stay green, what is new

Re-verified by enumeration at `c1564dd2`.

**Dictionary-path cap pins (must stay green, unchanged):**
- `WireOffsetTable.DoSCapPerInstanceRejectsOversizedSingleInstance` — `offset_table_test.cpp:198-235`
- `WireOffsetTable.DoSCapPerInstanceAllowsAggregateOverCap` — `offset_table_test.cpp:164-196`

**Dict-free path coverage (must stay green — this is what makes FR-003's relocation *checkable*):** roughly sixteen direct `OffsetTable{frame, mr}` constructions across `offset_table_test.cpp` (`:58,104,117,129,150,254`), `offset_table_overflow_test.cpp` (`:74,113,155`), `offset_table_error_path_test.cpp` (`:68,100,125,190`) and `hostile_input_hardening_test.cpp` (`:78,107,132,161,284,319`). Notably `offset_table_test.cpp:150-157` already calls `group(453)` **dict-free** and asserts its extent — i.e. the relocated branch is on a live, asserted path today.

**Census / split pins (SC-001):** `DelimiterCensus.RedCountsReconcileWithSpecBaseline` (`tests/dictionary/delimiter_census_test.cpp:476`) and all seven `TypedReadSplitAgreement.*` (`tests/wire/typed_read_split_agreement_test.cpp:194,296,337,383,452,633,820`).

**New (FR-004/FR-005a):** two cases, named by SC-004 so the selector and its expected count are deterministic —

- `WireOffsetTable.DictFreeDoSCapPerInstanceRejectsOversizedInstance` — dict-free + tight-`Config` cap pin;
- `WireOffsetTable.DictFreeDoSCapPerInstanceAllowsWhenCapRaised` — its bracketing companion (same frame, cap raised above it → succeeds).

Both join `tests/wire/offset_table_test.cpp`, which is compiled into the **`wire_pure_tests`** bucket (`tests/wire/CMakeLists.txt:43`, LABELS `"079;wire"` at `:80`) — isolation-safe per `[const §VII.8]`, **no new executable**. `--gtest_filter='WireOffsetTable.DictFreeDoSCapPerInstance*'` must select exactly **2** once they land.

**New (FR-001b):** the red-first **structural** pin `WireOffsetTable.FR001_SingleTraversalSourceInspection`, also joining `tests/wire/offset_table_test.cpp` / `wire_pure_tests`, plus one `FIXPP_SRC_DIR` line on that bucket in `tests/wire/CMakeLists.txt` (the bucket does not define it today — verified). Authored and observed **RED on the unmodified tree before the relocation**; GREEN after. Article VII §3's failing-test-first artifact (SC-005b). Construction copied from `tests/dictionary/load_any_test.cpp:143-171`. `--gtest_filter='WireOffsetTable.FR001_SingleTraversalSourceInspection'` must select exactly **1** once it lands.

**New (FR-005b):** no new test — a recorded RED-under-mutation transcript for the **pre-existing** dictionary pin `WireOffsetTable.DoSCapPerInstanceRejectsOversizedSingleInstance`, obtained by deleting `consume_group_extent`'s per-instance comparison (`:521-524`). **Valid only post-relocation** — on baseline the flat loop at `:584-595` pre-empts and the pin stays GREEN (measured; see R-1 step 4 and FR-005b). It is the discharge SC-003 had been demanding with no artifact behind it, and it guards contract **C-1**'s *existence* half only: the fixture is one **unnested** instance, so it cannot distinguish flat from nesting-aware partitions and does **not** guard C-1's partition coupling *(narrowed at Gate A round 2)*.

**Registered CTest names and counts (verified 2026-08-03 at `c1564dd2`, `build/linux-clang-debug`).** Recorded here because the round-1 quickstart selected all of these by GoogleTest *case* name via `ctest -R`, which matches nothing and exits 0:

| Selection | Count |
|---|---|
| `ctest -L wire` (`wire_pure_tests`, `wire_dict_tests`, `validator_legacy_char_type_test`, `required_scope_two_tier_test`) | **4** — observed green, 45.99 s |
| `ctest -L capi` | **22** |
| `ctest -L dictionary` | **17** |
| `ctest -R '^delimiter_census$'` | **1** |
| `wire_dict_tests --gtest_filter='TypedReadSplitAgreement.*'` | **7** cases |
| `wire_pure_tests --gtest_filter='WireOffsetTable.DoSCapPerInstance*'` | **2** cases |

---

## R-6 — Gate obligations that apply, and how each is discharged

| Article | Obligation | Discharge |
|---|---|---|
| VII §3 | TDD, red-green-refactor | **PASS (planned) — by compliance.** *(Corrected twice. Round 1 replaced an unachievable "the new dict-free cap pin is observed RED **before** the relocation" with "NOT CLEANLY APPLICABLE". **Round 2 withdraws that disposition too.**)* The round-1 *cap-pin* finding stands: on baseline the flat cap loop sits **after** the `if/else` (`:584-595` follows `:553-582`) and runs on **both** paths, so a dict-free + tight-`Config` **cap** pin is green on baseline. What round 1 got wrong is the inference from there to *no red-first artifact exists*. A **structural** one does. **Artifact: FR-001b's `WireOffsetTable.FR001_SingleTraversalSourceInspection`** — a permanent, behaviour-blind source-inspection pin asserting the flat block sits inside the dict-free `else` and not in `group()`'s body after the `if/else`. RED on the unmodified tree, GREEN after the relocation; verified by **SC-005b**; RED output in `.specify/decisions/085-fold-flat-cap-loop-verify.md`. Discriminant measured at `c1564dd2` rather than assumed — 4-space `inst_start` declarations **1**, 8-space **0** ⇒ RED. Precedent: `tests/dictionary/load_any_test.cpp:143-171`, same construction, plain `std::string::find`, **no AST** — so round 1's "no precedent / novel structural-AST gate" rationale was false on **both** clauses and is deleted. Route forced by `.specify/constitution.md:86` and `[const §XX.1]` (`:402`): a conflict with a mandatory article is resolved by compliance, amendment or a rationale-bearing waiver — never by a locally invented verdict. The FR-005a(i)/FR-005b **mutation transcripts** remain as supplementary evidence |
| VII §7 | Parser-touching ⇒ fuzz harness | No new parser code; existing `tests/fuzz/` harnesses cover `OffsetTable`. No new harness required |
| VII §8 | Grouped buckets, select by label | New tests join the existing `offset_table_test.cpp` bucket; `ctest -L wire` |
| VIII §2/§3 | Bench in the same PR, ±5% | **§3** — the three existing `BM_TypedReadGroup_{Flat2,ModeC2,ModeC8}` ship in this PR (A-004). **§2** — ±5% is measured against **`bench/baselines/wire/typed_read_group_bench.json`**, per SC-006 leg 1: each case's `_median` `cpu_time` from `--benchmark_format=json` versus that case's **`seed_median_ns`** (`386`/`661`/`1643`), at the baseline's own recorded `repetitions: 9`, `min_time_s: 0.5`, release, same host. The same-session `main`-vs-branch A/B is leg 2, supplemental noise control. *(Basis corrected at Gate A round 2 — round 1 named only the A/B, which is not the comparison the article specifies.)* **`tools/bench_compare.py` cannot perform this comparison for this file**: `load_benchmarks` keys on `cpu_time`, which this baseline does not carry, so all three rows print `N/A` (verified). No baseline file is updated and the comparator is not modified — this is not an intentional perf change and both are out of scope |
| VIII §5 | Zero hot-path alloc | Removal only; FR-009 |
| IX §1 | ≥95% line / ≥85% branch on touched modules; no silent uncovered error path | **PLANNED — expected to improve.** See R-3: the one previously-dead branch becomes covered and the stale waiver is repaired, so the line arithmetic nets positive. The **percentages themselves are unmeasured pre-implementation** — they are measured at `/speckit-verify` with fresh per-binary profraw and recorded in `.specify/decisions/085-fold-flat-cap-loop-verify.md`, which is the artifact §1's binding rule names. *(Qualifier added at Gate A round 1 for consistency with the "(planned)" verdicts on VIII §2/§3 and IX §2/§4.)* |
| IX §2/§4 | ASan/UBSan/TSan + static analysis | SC-008; no new constructs |
| X | C-ABI contract | Untouched — FR-008/SC-007 |

**No Complexity Tracking entries.** This feature adds no abstraction, no dependency, and no configuration surface; it deletes a loop from one branch and relocates it to another.

---

## R-7 — Residual risk

1. **After the removal, the dictionary path's cap has no backstop.** *(Restated at Gate A round 1. The previous version of this item said "Step 1 is a standing invariant, not a one-time fact" and warned that re-pointing `:551` or `:458` would let the dict path under-enforce the cap. That is wrong on both halves — see the corrected note under R-1 step 1 and `contracts/group_cap_accounting.md` C-1/C-1a.)* Step 1's delimiter-source equality is a **one-time fact** about `c1564dd2` that licenses the removal and then stops binding: with the second walk gone there is no rival partition, and `group():551`'s only remaining dictionary-path use is the recognition gate at `:566`. What *is* a standing risk is narrower and real: `consume_group_extent`'s per-instance check (`:521-524`) becomes the dictionary path's **sole** DoS defence, so a future change to that walk — the instance-opening rule, the delimiter it walks on (`:458`), or the extent it returns (`:527`) — could under-enforce with nothing left to mask it. Mitigated by **C-1** as a contract, by the FR-002 source comment (content pinned by SC-005a), **and partly by a test**: FR-005b/SC-003 prove the dictionary pin RED when `:521-524` is mutated away — **post-relocation only**, since on baseline the flat loop still pre-empts and the pin stays green (measured; R-1 step 4). The old "no test can observe a cap that correctly never fires" rationale does not survive the restatement — the restated property's *existence* half is directly mutation-testable. **What that mutation does not reach** *(narrowed at Gate A round 2)*: the pin's frame is a single **unnested** instance, so deleting the whole comparison cannot distinguish the flat partition from the nesting-aware one. A change re-anchoring `inst_start` after a nested descent (`:493` / `:512`) would under-count only for nested instances and leave the pin green — which is precisely the "instance-opening rule" half of this risk. That half stays mitigated by the contract and the comment, not by a test; a nesting-sensitive fixture to close it was considered and rejected as out of scope for a removal (`plan.md` `### Round 2 — disagreements`).
2. **fixpp#220 stays open at merge.** By design (FR-003a). The risk is that the limitation row and the issue drift apart; SC-010 pins the citation in both directions.
3. **The bench delta may be within noise.** Expected — the removed walk is `O(extent)` on a path already dominated by the nesting-aware walk. SC-006 asserts *no regression*, not an improvement, precisely so a null result is a pass and not a temptation to re-tune the fixture (A-004).
