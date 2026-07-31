# Quickstart — Validating Group Delimiter Resolution

**Feature**: `083-group-delimiter-resolution` | **Date**: 2026-07-30

How to reproduce the defect, verify the fix, and — most importantly — **disprove** the fix if it is wrong. Run from the library root.

## Build constraints (read before building)

- **`-j2` maximum.** Wide parallel C++ builds OOM-kill the session.
- **Never use `cmake --preset`.** Conan's `CMakeUserPresets.json` has a duplicate `conan-debug` preset collision that makes CMake refuse to read *any* preset. Configure with `-B` instead.
- Local verification is Clang-only; the GCC-Release and MSVC jobs are CI-only and can fail on things a local run cannot see.

## 0. Establish the baseline before changing anything

The spec's figures are measured, not inherited — reproduce them first, or the success criteria have no denominator.

```
# scratch probe, NOT checked in and NOT in the working tree
delim_probe3.cpp   # oracle member sets + independent document-order walk
                   # vs the runtime, across all ten dictionaries
```

Expected on `main` @ `0539b56d`: **335** wrong-delimiter contexts, **52** polluted, **30** unregistered, **232** wrong-delimiter contexts whose true delimiter is itself a nested group's count tag.

> **Authority of record** *(added Gate A round 1)*. `delim_probe3.cpp` cannot be re-run — not by Gate A, Gate B, CI, `/speckit-verify`, or a maintainer auditing SC-001. So do **not** treat this section as the thing to reproduce. Phase 1 rebuilds the measurement inside the checked-in pin `tests/dictionary/delimiter_census_test.cpp`, and **SC-015 makes reconciliation an exit criterion**: the pin's observed RED counts are compared against 335 / 52 / 30 / 232, and any delta is explained in `research.md` — as falsifying the probe, the pin, or the spec — with `spec.md` amended, **before Phase 2 starts**. From that point the **pin** is the authority for every figure in the spec's Baseline table. Rebuilding a measurement is not the same as agreeing with it.

> Both probe-fidelity requirements below are requirements **on the pin**, not only on the probe.

**Two probe-fidelity requirements.** Get either wrong and the numbers are fiction:

1. **Discriminate a context miss from a wrong answer.** The context-keyed accessor falls back to the bare global store on a miss, so an unregistered context returns the *global* member set and reads as massive pollution. Compare the returned span's data pointer against the bare span's. Skipping this inflated the original report by 10 contexts.
2. **Score the 30 unregistered contexts too.** The baseline probe `continue`s past them *before* the delimiter check, so 335 excludes them. They enter the **affected set** once the loader fix registers their parents — post-fix that set is **365 = 335 measured + 30 projected**. This is the one projected rather than measured figure in the spec; measuring it is a Phase 1 task. **365 is the size of the affected set, not the context population** — the population is the Baseline table's `contexts` column, 56,246 rising to ~56,276.

## 1. Reproduce the user-visible defect (Phase 1 RED)

This closes the single inference in the analysis. No wire reproduction existed at triage — the "wrong delimiter causes mis-parsing" claim was a reading of the scanner's logic, not a measurement. **Do this before writing any fix.**

Two assertions, one asymmetry:

| Build a message for… | Expect today |
|---|---|
| a divergent context (`CollateralRequest(AX)` / `NoExecs(124)` is cleanest) | **REJECTED** |
| a first-seen context of the same count tag | **ACCEPTED** |

If both accept, the feature's premise is wrong and the spec must be revisited — say so rather than proceeding.

### 1a. Reproduce fixpp#210's own two consequences (Phase 1 RED — added Gate A round 1)

The issue named two user-visible consequences and the bundle originally closed both with substitutes: SC-002's member-set **count** falling 52 → 0, which is a proxy, not the leniency #210 filed. Both need their own RED witness (FR-010a, FR-007a, SC-013):

| # | Shape | Today | After |
|---|---|---|---|
| C1 — over-permissive membership | a named polluted context; a message carrying the **injected tag inside** the group, where the dictionary does not declare it | **ACCEPTED** (the leniency #210 filed) | REJECTED |
| C2 — extent mis-parse | the same context, with a **message-level field following the group** whose tag equals the injected member | **SWALLOWED** into the last instance — the extent scan does not stop there | not swallowed |

C2 is the pin #210's author explicitly sequenced first (*"this should be the first thing the fix's RED pin establishes"*). It is a **different mechanism** from §1 and §2 above — an extent-*termination* defect at `include/fixpp/wire/validator.hpp:365-366`, not an instance-*open* defect at `:357` — so neither of those witnesses may stand in for it. If C2 proves unreachable, record the shapes tried and the negative result in the spec's Assumptions; do not quietly drop it.

## 2. Reproduce the reception defect (Phase 1 RED)

Minimal shape, from fixpp#208:

```
35=X | 100=2 | 200=1 201=A | 200=1 201=B
```

outer group `100` delimited by `200`, where `200` is itself a nested group delimited by `201`.

| instances | today |
|---|---|
| 2 | **REJECTED** — instance-count mismatch |
| 1 | ACCEPTED |

The single-instance case passing is exactly why this survived. **The two-instance form is the witness; the one-instance form is the regression guard.**

### 2a. The SAME shape, read back rather than validated (Phase 1 RED — added Gate A round 3)

The shape above has a **second** scanner, in a different subsystem, with the **same** asymmetry: `OffsetTable::consume_group_extent` consumes the instance-opening delimiter with a bare `++k` (`src/wire/offset_table.cpp:475`) and descends only for members scanned after it (`:485-488`). Take the identical bytes and go through the read path instead:

| what to observe | today | after (FR-021e / C-8.0c) |
|---|---|---|
| `group(100)->entry_count()` — the extent | spans **one** instance | spans **all** `declared` |
| `group_slices()` / `MessageView::group<>()` instance count | **1** | **2** |
| the one-instance form | 1 | 1 *(must not regress)* |

**Why this matters more after Phase 2 than before it.** Pre-083 the two paths agree because both are broken — validation rejects this message outright, so no caller reaches the read path. Once FR-007 lands, validation accepts and the read path silently returns one instance. That converts a loud rejection into **silent instance loss** through `MessageView::group<>()` and the C-ABI top-level group getter, on the **262** contexts (232 measured + 30 newly registering) whose post-fix delimiter is a nested group's count tag. A correct split cannot recover it: `group_slices_status` takes its bound from this extent (`:648` ← `:550`).

**Do not stop at the count.** Assert the extent and the boundaries, and derive the expected extent from the fixture's own entry layout — a value captured after the fix pins whatever the fix did.

## 3. Prove the pin can fail (Phase 1 RED — do not skip)

Run the all-ten delimiter pin before any fix and **reconcile the failure counts against 335 / 52 / 30 / 232** — recording them is not enough (SC-015). A gate never observed red proves nothing; a gate observed red at a number nobody compared to anything proves only that something is red. If the pin reds at a different count, say in `research.md` whether that falsifies the probe, the pin, or the spec, and amend `spec.md` before Phase 2.

Then, after the fix, deliberately reintroduce global first-seen resolution and confirm the pin goes red again (FR-014). Two different guards:

- pin red before the fix → it is measuring something;
- pin red on reintroduction → it is measuring *this* something.

**Non-circularity check** (FR-013): expected values come from the independent document walk, and a documented sample is cross-checked against a third authority — codegen's group-order data, or fixpp#208's tabulated Orchestra values. If the oracle mirrors the fixed loader's logic, a green pin means nothing.

**Do not count the 78 collision-membership cases as coverage** (FR-016). Their discriminator is derived independently of the delimiter, so their green says nothing here. A sibling feature had to add an exclusion parameter to that helper specifically so the injected delimiter would not be picked as a discriminator — direct evidence they route around this defect.

## 4. Verify, in phase order

Phases 2 and 3 are **not** interchangeable — see `contracts/consume_group.md`.

Gates rewritten 2026-07-30 (Gate A round 1) — see `plan.md`'s phasing table for the reasoning. The one that mattered most: the old "**delimiter pin failure count unchanged**" row after Phase 2 was **vacuous**. Phase 2 touches `include/fixpp/wire/validator.hpp` only and the pin is a dictionary-level assertion over loader output, so the count could not move whatever Phase 2 did.

| After phase | Check | Expected |
|---|---|---|
| 1 — RED | pin RED counts vs spec's 335 / 52 / 30 / 232 | **reconciled**, not merely recorded (SC-015); delta explained + spec amended before Phase 2 |
| 1 — RED | D-12 — instrument the **`fr.type` predicate** for tag 146 (loop-visible type vs `field_ref("R",146).type`), not "which call path" | answered and recorded; branch (a) ⇒ amend D-1/D-3 **and re-derive C-3.4a's checked set** before Phase 3 |
| 2 — receiver | nested-delimiter repro, **bare** fixture (W-1) | green |
| 2 — receiver | nested-delimiter repro on a **populated context store** (W-1a) | green — this is the real Phase-2 exit witness; W-1 alone only exercises the bare fallback (`table_view.hpp:346-349`, `:364`) |
| 2 — receiver | the ten dictionaries' **currently resolved delimiters** | unchanged — the real content of "must not touch resolution" |
| 3 — loaders | delimiter pin, all ten | green, no carve-out |
| 3 — loaders | FIX 5.0 SP2 registered groups | 502 → 505, matching codegen, delta named by three groups |
| 3 — loaders | all ten dictionaries load, fail-closed default | all load *(C-7.1 — precondition, run before enabling)* |
| 3 — loaders | SC-004's named eight-tag **wire** subset | green **at this exit**, not deferred to Phase 5 |
| 3 — loaders | nested/parent delimiter re-census under **post-fix** delimiters | 0 collisions, L-063-4 re-stated *(C-7.2)* |
| 4 — consumers | member-set exactness | 0 polluted, as a consequence of the delimiter pin, not a second assertion |
| 4 — consumers | C-ABI construction vs validation (W-11) | agree **in both directions**; no exported signature changed |
| 4 — consumers | five disclosed delimiter moves (W-12) | old opening tag rejects, new accepts — pinned, not merely described |
| 4 — consumers | every registered group still answers the bare predicate (W-13) | non-zero on all ten — D-10's total-regression pin |
| 4 — consumers | typed-read vs validation instance counts **and boundaries** (W-9) | agree — on a **divergent** context under a **non-empty parent path**, or the bare-global fallback masks an off-by-one key; run over **both** fixtures, (b) delimiter tag reappearing at depth **and (c) delimiter that IS a nested group's count tag** *(fixture (c) added Gate A r3 — with (b) alone W-9 cannot see the C-8.0c defect)* |
| **3 (exit)** / 4 — consumers | offset-table extent walk descends at the instance-opening delimiter (**W-10a**, FR-021e / SC-016) | extent spans **all** `declared` instances on a mode-(c) shape — expected value derived from the fixture's layout, not captured from the implementation; one-instance form still reports **1**; depth-capped form returns `err_group_too_large` **and returns immediately**. **Leg 4 re-specified at Gate A fresh loop round 1**: the error code alone passes against an implementation that omits the `if (overflow) return k;` mirror (`overflow` is a `bool&` and reaches `group()`'s check either way), and the omission does **not** hang — it burns `declared` no-op iterations, a **DoS**, so no `TIMEOUT` fires on an unconstrained fixture. The leg therefore requires **`declared` ≥ 2 at the frame whose descent hits the depth cap** — **not** the outermost frame, whose count the un-mirrored implementation is invariant under too, so varying it would false-green — and is decided by a **`group_member_fn_` invocation count asserted equal across that frame's `declared` = 2 and `declared` = 8** — the two runs differing in **exactly one digit of that frame's count field**, entry layout byte-identical (counting wrapper on the existing construction-time callback seam — no production change); the per-test `TIMEOUT` stays as a **backstop only**. **Observe leg 1 RED before C-8.0c lands** — leg 4 cannot be RED pre-fix, because the branch it discriminates does not exist yet. *(Phase: required green at the **Phase-3** exit as of Gate A fresh loop round 1, re-run at the Phase-4 exit.)* |
| 4 — consumers | the frozen wire locals (**W-10**) | unchanged pre-083 values for `consume_group_extent`'s extent bound, `group()`'s `group_index` and the reserve bound — **on a divergent context whose delimiter is NOT a nested group's count tag**, and the case asserts that exclusion itself. *(Fixture re-stated Gate A r3: as written at r2 this row pinned the truncated extent as correct and would have certified the defect green.)* |
| 4 — consumers | commit-side `table_view` built once per session, never per message (W-11a) | 1 `as_table_view()` per session over N>1 commits — `[const §XV.1]` / C-9.2a |
| 5 — evidence | **three** benchmarks + baselines — inbound validate, typed read, C-ABI commit | in the same change; ±5% budget each (FR-022) |

## 5. Selecting tests

Select by label, never by executable name:

```
ctest -L dictionary
ctest -L wire
ctest -L capi           # required — FR-018/SC-012 land in tests/capi/
ctest -L codegen        # required — the loader change is codegen-adjacent
```

`ctest -L codegen` is not optional here. A label-filtered run that covers the plausible gates for a changed subsystem can still miss that subsystem's count pin — and this feature moves a count from 502 to 505.

## 6. What "done" looks like

| | before | after | provenance |
|---|---|---|---|
| wrong delimiter | 335 of 335 **measured** | **0 wrong**, over the 365 contexts in the *affected set* | 335 measured; the 30 that make it 365 are **projected** until the Phase-1 pin measures them |
| polluted member sets | 52 measured | 0 | measured |
| unregistered contexts | 30 measured | 0 | measured |
| FIX50SP2 groups | 502 measured | 505 | measured |
| delimiter pinned anywhere | **nothing** | all ten dictionaries, no carve-out | — |

*(365 is the affected set, not the context population — the population is 56,246 rising to ~56,276. The projection marker was added at Gate A round 1; the row previously read "0 of 365" with no marker, in a bundle that is otherwise explicit about which figures are projections.)*

Plus: both load dispositions witnessed; the typed-read splitter characterised by evidence rather than left as an unverified note; behaviour changes recorded as operator-facing rows and release notes; the interop divergence observed and documented.
