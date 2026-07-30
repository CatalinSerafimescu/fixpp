# Quickstart: Validating Structural Repeating-Group Detection

**Feature**: `082-structural-group-detection` | **Date**: 2026-07-29

Runnable scenarios that prove the feature end-to-end. Each names the requirement it discharges
and the observable **before → after**. Contract IDs refer to
[`contracts/group-detection.md`](./contracts/group-detection.md); decision IDs to
[`research.md`](./research.md).

## Prerequisites

```bash
cd research/G19-fix-fpml-iso20022/library      # all commands run from the submodule root
```

A configured build directory (any existing preset). Two traps apply to this feature specifically:

- **Force a clean codegen rebuild before trusting any golden diff.** Non-debug directories have
  silently compiled a stale `Reify.hpp`; a stale emitter object makes a golden comparison
  meaningless. Rebuild the codegen tool from scratch, not incrementally.
- **`tools/codegen/**` is touched ⇒ `ctest -L codegen` is mandatory.** A label-filtered run that
  omits it has previously missed a subsystem's COUNT pin.

---

## S0 — Baseline: reproduce the census oracle (no build required)

Establishes ground truth independently of the library. Run this **first**; it is the source K1–K3
assert against, and it must give identical output before and after the change (it reads XML, not
code).

```bash
python3 specs/082-structural-group-detection/contracts/predicate_census.py
```

**Expected**: the C2 table — FIX40 `0/4`, FIX41 `0/7`, FIX42 `0/18` DIFFER; FIX43 `34/34` DIFFER
with `82` type-only and `576` struct-only; FIX44/50/50SP1/50SP2/T11/FIX-Latest EQUAL; no
zero-member `<group>` warnings.

*Discharges*: the evidence base for FR-005, FR-013, FR-014 · *Contract*: C2.

---

## S1 — FIX42 groups become visible at the runtime tier

*Discharges*: FR-005, SC-001 · *Contract*: C1.1, K1, K4 · *US1*

```bash
ctest --test-dir <build> -L dictionary --output-on-failure
```

**Before**: `as_table_view()` on `FIX42.xml` registers **0** groups.
**After**: exactly **18**, each with its declared member set — matching S0's struct column by
exact-set equality in **both** directions. FIX40 → 4, FIX41 → 7.

Also asserts K4: both stores (legacy bare and 063 context-scoped) agree on every newly-visible
group. A pass on one store alone would be a half-restructure.

---

## S2 — FIX43: one tag moves, one tag must not

*Discharges*: FR-011, FR-012, FR-013, SC-003 · *Contract*: C2, K3 · *US3*

Same command as S1.

**After**:
- tag **576** `NoClearingInstructions` **is** registered, member `ClearingInstruction`, and a
  populated group reads membership-bounded (before: absent / `TYPE_MISMATCH`);
- tag **82** `NoRpts` is **not** registered, and `ListStatus` still enforces it as a plain
  **required** field — unchanged from today (no-regression pin, D-2);
- the FIX43 set differs from baseline by exactly `{+576}`.

---

## S3 — Non-regression on the six unaffected dictionaries

*Discharges*: FR-014, SC-002 · *Contract*: C3, K2

Same command as S1. FIX44 / FIX50 / FIX50SP1 / FIX50SP2 / FIXT11 / Orchestra FIX Latest must
register **exactly** their pre-feature sets — 0 additions, 0 removals. Exact-set, not containment:
a subset check passes while silently dropping a group.

---

## S4 — The parse correction is ungated

*Discharges*: FR-006a, SC-008, SC-008a · *Contract*: C4.1 · *US1*

```bash
ctest --test-dir <build> -L wire -L session --output-on-failure
```

Feed a FIX 4.2 `MarketDataSnapshotFullRefresh` with a populated `NoMDEntries(268)` group to a
session with **`validate_inbound_messages` OFF**.

**Before**: a typed / C-ABI group query returns `TYPE_MISMATCH` or absent.
**After**: a membership-bounded read — *with the strict flag still off*, which is the point. The
flag must be off in this scenario so the parse axis and the validation axis are never conflated.

Companion assertion (SC-008a): with the flag off, **no** FIX40/41/42 message is newly *rejected* —
read shape changes, acceptance does not.

---

## S5 — Regenerate and diff every version (the discriminating check)

*Discharges*: FR-015, FR-016, FR-016a, SC-004, SC-005 · *Contract*: K5 · **Highest-value scenario**

```bash
cmake --build <build> --target fixpp-codegen   # after a CLEAN rebuild — see Prerequisites
ctest --test-dir <build> -L codegen --output-on-failure
```

**Must be byte-identical**: `v44`, `v50sp2`, `vt11`, `vlatest` read goldens; `v44`, `v50sp2`,
`vlatest` builder golden sets; **and** `v42`'s own `Fields.hpp` + `Validator.hpp` (D-4/D-10 —
`FieldRef::type` is not modified and `emit_validator` has no group axis, so these are real
predictions, not tautologies).

**Must change**: `v42/Messages.hpp` gains exactly **18** `class G_` (from 0) plus group accessors
on the 22 group-bearing messages; `v42/Reify.hpp` gains the matching owning-class accessors.

Every moved count needs a **by-construction** reconciliation to FIX42's declared structure. A
golden that merely "regenerated" is not evidence — an unexpected delta is a finding.

---

## S6 — The v42 typed builder tier exists and cannot silently omit a group

*Discharges*: FR-007, FR-008, FR-009, FR-010, FR-016b, SC-006 · *Contract*: C4.3 · *US2 — the #196 deliverable*

Same command as S5.

**Before**: `V42EmitsNoBuilders` asserts `v42/all.hpp` and `v42/messages/` are absent.
**After**, that test is **inverted** (not deleted; its `vt11` companions stay untouched) and:

- the full 078 split layout is emitted for `v42` — `messages/<Msg>.hpp`, `groups/<PlanName>.hpp`
  + `groups.hpp`, `validators/traits.hpp`, `all.hpp` — over all **39** application messages;
- `validate_<Msg>` **rejects** an `Args` value omitting a group declared `required='Y'`, at all
  **14** message/group pairs (e.g. `NewOrderList`/`NoOrders`). This is the Article-VI heart of the
  feature: the group is representable, so its absence is *detectable* rather than silent — the
  exact failure mode that forced the 077 descope;
- `emit_builders` output matches the new `v42` goldens, including the `--families official` pin;
- no version-name predicate remains in the driver; `vt11` still self-skips via its empty registry.

---

## S7 — A grouped *and* nested FIX 4.2 write round-trips

*Discharges*: SC-007, closes L-061-1 · *US4*

Build a `MassQuote` with `NoQuoteSets(296)` containing `NoQuoteEntries(295)` — one of FIX42's five
nested group occurrences — via the `v42` builder tier.

**Expected**: emitted bytes match an independently-derived (QuickFIX) golden, and parsing them
back through the `v42` read tier round-trips every field and **both** group levels. Before this
feature no v42 grouped write was expressible at all; all five exemplars were forced to `v44`.

---

## S8 — Documentation closure

*Discharges*: FR-006c, FR-019, SC-010

Confirm `spec/behaviors-and-limitations.md`:
- closes **L-063-1**, **L-061-1**, **L-066-1**, **L-077-1**;
- records the FIX43 `+576` correction with its evidence;
- carries the FR-006c named behavior change **and operator-facing release note**: FIX40/41/42
  inbound group reads change shape, and strict-validation deployments on those versions may see
  new rejects.

Also confirm the four now-false carve-out comments are rewritten rather than left stale:
`required_scope_test.cpp:107`, `required_scope_census_test.cpp:341`,
`reused_tag_census_test.cpp:158`, `validator_type_check_test.cpp:966`.

---

## Full local gate

```bash
ctest --test-dir <build> -L codegen -L dictionary -L wire -L session --output-on-failure
```

Then `/speckit-verify` for the sanitizer / coverage / static-analysis matrix. Note that
`/speckit-verify` is clang-only — the `gcc-release` and MSVC legs are CI-only jobs.
