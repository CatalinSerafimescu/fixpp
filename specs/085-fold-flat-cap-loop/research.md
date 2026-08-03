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

*Note for Gate A:* this is the one step that could silently break later. 083 moved the **splitter's** delimiter to the dictionary store (`:704-711`) but deliberately left `group()`'s and `consume_group_extent`'s wire-derived (L-063-4's "the splitter is still flat"). If a future feature re-points *one* of `:551` / `:458` at the store without the other, step 1 fails and FR-002's no-op claim is void. C-1 below turns this into a contract obligation rather than a comment.

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

### Degenerate exits — all five checked

| `consume_group_extent` exit | Line | Reachable from `group()`? | Effect on the flat loop |
|---|---|---|---|
| depth ≥ `kMaxGroupDepth` | `:446-449` | Yes | `overflow` → returns at `:577`; loop unreached |
| `first >= entries_.size()` | `:451-453` | **No** — `group()` guards at `:546` | — |
| dict-free | `:454-456` | **No** — `group()` guards at `:553` | — |
| `delim` not a member | `:461-462` | **No** — `group()` guards at `:566` | — |
| `declared == 0` | `:465-467` | Yes | returns `first`, so `group_end == first`; the loop's single iteration measures `0` |

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

**Decision**: Per FR-001a (clarified 2026-08-03), the loop moves **verbatim** into the dict-free `else` branch. `consume_group_extent` is not touched.

```
  if (opaque_dict_ != nullptr && group_member_fn_ != nullptr) {
      ...  membership check at :566 (unchanged — still needs `delim`)
      group_end = consume_group_extent(count_idx, ctx, ctx.depth, overflow);
      if (overflow) { return err_group_too_large<group_index>(); }
      // FR-002 comment lands HERE: why no second walk is needed.
  } else {
      group_end = entries_.size();
      // relocated verbatim — SOLE cap enforcement on this path
      for (std::size_t k = first, inst_start = first; k <= group_end; ++k) { ... }
  }
  return group_index{no_tag, first, group_end - first};
```

Ordering note (FR-009): `group_end` is assigned before the loop in the `else` branch, exactly as today, so no work is added on either path and the `return` at `:596` is unchanged.

**Alternatives considered**:
- *Remove `consume_group_extent`'s dict-free bail so one traversal serves both paths* — the most literal reading of "fold". Rejected at `/clarify` Q1: with no dictionary that walk can be neither membership-driven nor nesting-aware, so it would embed a flat mode inside a function whose entire contract is that it is not flat, and would put FR-008 in play for no behavioural gain.
- *Extract a shared cap helper* — rejected: an abstraction over a two-line comparison, and it would make FR-003's byte-identical claim un-inspectable.

---

## R-5 — Test inventory: what must stay green, what is new

Re-verified by enumeration at `c1564dd2`.

**Dictionary-path cap pins (must stay green, unchanged):**
- `WireOffsetTable.DoSCapPerInstanceRejectsOversizedSingleInstance` — `offset_table_test.cpp:198-235`
- `WireOffsetTable.DoSCapPerInstanceAllowsAggregateOverCap` — `offset_table_test.cpp:164-196`

**Dict-free path coverage (must stay green — this is what makes FR-003's byte-identical relocation *checkable*):** roughly sixteen direct `OffsetTable{frame, mr}` constructions across `offset_table_test.cpp` (`:58,104,117,129,150,254`), `offset_table_overflow_test.cpp` (`:74,113,155`), `offset_table_error_path_test.cpp` (`:68,100,125,190`) and `hostile_input_hardening_test.cpp` (`:78,107,132,161,284,319`). Notably `offset_table_test.cpp:150-157` already calls `group(453)` **dict-free** and asserts its extent — i.e. the relocated branch is on a live, asserted path today.

**Census / split pins (SC-001):** `DelimiterCensus.RedCountsReconcileWithSpecBaseline` (`tests/dictionary/delimiter_census_test.cpp:476`) and all seven `TypedReadSplitAgreement.*` (`tests/wire/typed_read_split_agreement_test.cpp:194,296,337,383,452,633,820`).

**New (FR-004/FR-005a):** a dict-free + tight-`Config` cap pin, its bracketing companion (same frame, cap raised above it → succeeds), and a recorded RED-under-mutation transcript. Per Article VII §8 these are isolation-safe and belong in the existing `offset_table_test.cpp` bucket — **no new executable**.

---

## R-6 — Gate obligations that apply, and how each is discharged

| Article | Obligation | Discharge |
|---|---|---|
| VII §3 | TDD, red-green-refactor | The new dict-free cap pin is written and observed RED **before** the relocation; recorded per FR-005a(i) |
| VII §7 | Parser-touching ⇒ fuzz harness | No new parser code; existing `tests/fuzz/` harnesses cover `OffsetTable`. No new harness required |
| VII §8 | Grouped buckets, select by label | New tests join the existing `offset_table_test.cpp` bucket; `ctest -L wire` |
| VIII §2/§3 | Bench in the same PR, ±5% | The three existing `BM_TypedReadGroup_{Flat2,ModeC2,ModeC8}` run before/after, numbers in the PR body (SC-006). No baseline file is updated — this is not an intentional perf change |
| VIII §5 | Zero hot-path alloc | Removal only; FR-009 |
| IX §1 | ≥95% line / ≥85% branch on touched modules; no silent uncovered error path | Improves — see R-3. The one previously-dead branch becomes covered; the stale waiver is repaired |
| IX §2/§4 | ASan/UBSan/TSan + static analysis | SC-008; no new constructs |
| X | C-ABI contract | Untouched — FR-008/SC-007 |

**No Complexity Tracking entries.** This feature adds no abstraction, no dependency, and no configuration surface; it deletes a loop from one branch and relocates it to another.

---

## R-7 — Residual risk

1. **Step 1 is a standing invariant, not a one-time fact.** If a later feature re-points `group():551` or `consume_group_extent():458` at the dictionary store without the other, the redundancy argument silently breaks and the dict path could under-enforce the cap. Mitigated by C-1 (contract) — *not* by a test, because no test can observe a cap that correctly never fires.
2. **fixpp#220 stays open at merge.** By design (FR-003a). The risk is that the limitation row and the issue drift apart; SC-010 pins the citation in both directions.
3. **The bench delta may be within noise.** Expected — the removed walk is `O(extent)` on a path already dominated by the nesting-aware walk. SC-006 asserts *no regression*, not an improvement, precisely so a null result is a pass and not a temptation to re-tune the fixture (A-004).
