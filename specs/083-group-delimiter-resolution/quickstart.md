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
# scratch probe, not checked in
delim_probe3.cpp   # oracle member sets + independent document-order walk
                   # vs the runtime, across all ten dictionaries
```

Expected on `main` @ `0539b56d`: **335** wrong-delimiter contexts, **52** polluted, **30** unregistered, **232** wrong-delimiter contexts whose true delimiter is itself a nested group's count tag.

**Two probe-fidelity requirements.** Get either wrong and the numbers are fiction:

1. **Discriminate a context miss from a wrong answer.** The context-keyed accessor falls back to the bare global store on a miss, so an unregistered context returns the *global* member set and reads as massive pollution. Compare the returned span's data pointer against the bare span's. Skipping this inflated the original report by 10 contexts.
2. **Score the 30 unregistered contexts too.** The baseline probe `continue`s past them *before* the delimiter check, so 335 excludes them. They enter the population once the loader fix registers their parents — post-fix the denominator is **365**. This is the one projected rather than measured figure in the spec; measuring it is a Phase 1 task.

## 1. Reproduce the user-visible defect (Phase 1 RED)

This closes the single inference in the analysis. No wire reproduction existed at triage — the "wrong delimiter causes mis-parsing" claim was a reading of the scanner's logic, not a measurement. **Do this before writing any fix.**

Two assertions, one asymmetry:

| Build a message for… | Expect today |
|---|---|
| a divergent context (`CollateralRequest(AX)` / `NoExecs(124)` is cleanest) | **REJECTED** |
| a first-seen context of the same count tag | **ACCEPTED** |

If both accept, the feature's premise is wrong and the spec must be revisited — say so rather than proceeding.

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

## 3. Prove the pin can fail (Phase 1 RED — do not skip)

Run the all-ten delimiter pin before any fix and **record the failure counts**. A gate never observed red proves nothing.

Then, after the fix, deliberately reintroduce global first-seen resolution and confirm the pin goes red again (FR-014). Two different guards:

- pin red before the fix → it is measuring something;
- pin red on reintroduction → it is measuring *this* something.

**Non-circularity check** (FR-013): expected values come from the independent document walk, and a documented sample is cross-checked against a third authority — codegen's group-order data, or fixpp#208's tabulated Orchestra values. If the oracle mirrors the fixed loader's logic, a green pin means nothing.

**Do not count the 78 collision-membership cases as coverage** (FR-016). Their discriminator is derived independently of the delimiter, so their green says nothing here. A sibling feature had to add an exclusion parameter to that helper specifically so the injected delimiter would not be picked as a discriminator — direct evidence they route around this defect.

## 4. Verify, in phase order

Phases 2 and 3 are **not** interchangeable — see `contracts/consume_group.md`.

| After phase | Check | Expected |
|---|---|---|
| 2 — receiver | nested-delimiter repro | green |
| 2 — receiver | delimiter pin failure count | **unchanged** — this phase must not touch resolution |
| 3 — loaders | delimiter pin, all ten | green, no carve-out |
| 3 — loaders | FIX 5.0 SP2 registered groups | 502 → 505, matching codegen |
| 3 — loaders | all ten dictionaries load, fail-closed default | all load *(precondition — run before enabling)* |
| 4 — consumers | member-set exactness | 0 polluted, as a consequence of the delimiter pin, not a second assertion |
| 4 — consumers | C-ABI construction vs validation | agree; no exported signature changed |
| 4 — consumers | typed-read vs validation instance counts | agree |
| 5 — evidence | benchmark + baseline | in the same change; ±5% budget |

## 5. Selecting tests

Select by label, never by executable name:

```
ctest -L dictionary
ctest -L wire
ctest -L codegen        # required — the loader change is codegen-adjacent
```

`ctest -L codegen` is not optional here. A label-filtered run that covers the plausible gates for a changed subsystem can still miss that subsystem's count pin — and this feature moves a count from 502 to 505.

## 6. What "done" looks like

| | before | after |
|---|---|---|
| wrong delimiter | 335 of 335 measured | 0 of 365 |
| polluted member sets | 52 | 0 |
| unregistered contexts | 30 | 0 |
| FIX50SP2 groups | 502 | 505 |
| delimiter pinned anywhere | **nothing** | all ten dictionaries, no carve-out |

Plus: both load dispositions witnessed; the typed-read splitter characterised by evidence rather than left as an unverified note; behaviour changes recorded as operator-facing rows and release notes; the interop divergence observed and documented.
