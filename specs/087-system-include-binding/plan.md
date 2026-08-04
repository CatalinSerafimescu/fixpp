# Implementation Plan: System include directories bound at the installed-package consumer

**Branch**: `087-system-include-binding` · **Date**: 2026-08-04 · **Spec**: [spec.md](./spec.md)
**Issue**: [#234](https://github.com/CatalinSerafimescu/fixpp/issues/234) · **Inherits**: 086
`contracts/include-interface.md` **C-3**

> **Worktree.** All paths are relative to `~/Work/Programming/fixpp-parallel`, **not**
> `research/G19-fix-fpml-iso20022/library`. `/speckit-verify` and `/gate-b` hardcode the main checkout —
> substitute this path. Never `git checkout` in the main checkout; it holds another session's branch.

---

## Summary

Bind the fourth usage requirement `$<LINK_ONLY:>` withholds. 086 asserts three of the four
(`COMPILE_DEFINITIONS`, `COMPILE_OPTIONS`, `COMPILE_FEATURES`) and records `INTERFACE_SYSTEM_INCLUDE_DIRECTORIES`
as knowingly unbound, because no *collected* consumer property exists and a comparison written against it
would be empty **by construction rather than by measurement**.

The instrument is the **CMake File API `codemodel-v2` reply**, whose `compileGroups[].includes[]` carries each
include entry with an `isSystem` flag. Two legs — `fixpp::capi` and `fixpp::service` — each compared for
**exact set equality** against an expectation declared in the tree, prefix-relative.

**The mechanism is already measured on both platforms** (research R3, R6) before any artifact prescribes it.
That ordering is the deliberate correction of 086's failure mode, not a formality.

## Technical Context

| | |
|---|---|
| **Language / build** | CMake ≥ 3.30 (measured on 3.30.0); no C++ source change |
| **Instrument** | File API `codemodel-v2`; query file written **before** configure; reply globbed as `target-<name>-*.json` |
| **Legs** | `fixpp::capi` (expect **1** entry), `fixpp::service` (expect **2**) — measured, R4 |
| **Platforms measured** | Linux/clang/Ninja/Release **and** MSVC BuildTools 2022/Ninja/Debug under Conan — **identical** results (R6) |
| **Test tier** | extends `fixpp::consumer::install-witness`; **no new ctest registration** |
| **Scale/Scope** | ~2 CMake/CMake-script files edited, 2 expectations declared, up to 2 probe TUs reused, 4+ red demonstrations |
| **Unknowns** | **none blocking.** FR-009 discharged by R6; the spec's one deferred question (do compiler built-ins appear?) is answered **no**, on both platforms (R2/R6) |

## Constitution Check

| Article | Verdict | Basis |
|---|---|---|
| **II** — Language/compilers/platforms | **PASS** | No source change. Instrument verified on both Tier-1 (clang) and Tier-2 (MSVC) toolchains. |
| **III** — Build & dependency toolchain | **PASS** | No new dependency. File API is built into CMake; JSON is read with the tree's existing means. |
| **VI** — Spec coverage discipline | **PASS** | Cites are exact (`contracts/include-interface.md` C-3, `spec.md` FR-009a, `checklists/abi.md` CHK006). |
| **VII §3** — TDD mandatory | **PASS** | The gate is written and **observed red** before the expectation is corrected to pass — FR-007, SC-003 (four causes). Sequencing step 1. |
| **VII §4** — No code without a test | **PASS** | The deliverable *is* a test. |
| **VII §8** — Label-selectable tests | **PASS** | Rides the existing `consumer` label; no new executable. |
| **VIII** — Performance budgets | **N/A** | No runtime code path; configure-time only. |
| **IX §1** — Coverage | **N/A** | Gate ranges over `include/fixpp/<mod>/*` + `src/<mod>/*`; this feature edits none. |
| **IX §2/§3** — Sanitizers | **CI-DELEGATED** | Zero C++ TUs changed. Same disposition 086 recorded and Gate B accepted. |
| **IX §4** — Static analysis | **PASS** | Any new probe TU goes through clang-tidy/clang-format as 086's did; the sub-build already sets `CMAKE_EXPORT_COMPILE_COMMANDS=ON`. |
| **IX §5** — ABI check | **N/A** | `include/fix/**` byte-unchanged; no symbol, header or version-script change. |
| **X §1/§2** — ABI policy | **PASS** | Asserts an existing ABI-boundary property; does not move the surface. |
| **X §6** — ABI-affecting ⇒ four controls | **2 of 4 DONE** | `/clarify` **DONE** (3 questions, all resolved) · **user `/plan` sign-off DONE — granted 2026-08-04** · `/analyze` pending (pipeline step 6) · Gate A pending (step 4). Treated as ABI-adjacent to match 086's disposition, since the property asserted *is* the C-ABI consumption boundary. |
| **XV** — Banned patterns | **PASS** | No banned construct introduced. |
| **XVI §3** — `/clarify` mandatory | **PASS** | Run, not skipped on "spec complete". |
| **XVII §3** — Author/reviewer independence | **PASS** | Implementation by Opus; Gate A/B reviewers are fresh Codex sessions. |
| **XVII §8** — Verify + gate evidence | **PLANNED** | `/speckit-verify` + paired gate records before any `gate-*-done` label. |

**No unjustified violation.**

> ### ✅ Article X §6 — user `/plan` sign-off GRANTED 2026-08-04
>
> Signed off on this plan and `contracts/system-include-interface.md`. Two of the four mandatory controls are
> now discharged (`/clarify`, user sign-off); `/gate-a` and `/speckit-analyze` remain and are ordered by
> `.specify/pipeline.md` — **Gate A runs after `/plan` and BEFORE `/speckit-tasks`**, `/analyze` at step 6.

## Project Structure

### Documentation (this feature)

```text
specs/087-system-include-binding/
├── spec.md              # 17 FR / 7 SC / 3 user stories; all clarifications resolved
├── plan.md              # this file
├── research.md          # R1–R7; BOTH platforms measured on the real consumer project
├── data-model.md        # observed set / declared expectation / probe target
├── contracts/
│   └── system-include-interface.md   # normative expected sets + comparison rules
├── quickstart.md        # reproduce every measurement and every red
└── checklists/
    └── requirements.md  # 16/16
```

### Source Code (repository root)

```text
tests/consumer/
├── CMakeLists.txt                  # + declared expectations for both legs (literals with rationale)
├── run_consumer_witness.cmake      # + emit the File API query BEFORE the configure step
│                                   # + read the codemodel reply after configure
│                                   # + compare BOTH legs for exact set equality
│                                   # + carrier named in _required_targets (FR-006)
└── <probe TUs>                     # reuse existing probes whose link line already matches each leg;
                                    #   add only what does not already exist
```

**Structure Decision**: no new directory and no new registered test. The consumer witness already solves the
only hard part — configuring a standalone project against a staged install, which is the sole way to observe
an *installed* interface at all. This feature adds an observation to that existing sub-build.

> ### ⚠️ The query file must exist BEFORE configure — this constrains WHERE the emission lives
>
> The File API produces a reply only if `.cmake/api/v1/query/codemodel-v2` was present when CMake ran on the
> sub-build. The driver (`run_consumer_witness.cmake`) is what configures the sub-build, so **the query must
> be created by the driver before its `execute_process` configure step** — *not* by
> `tests/consumer/CMakeLists.txt`, which executes *during* that configure and is therefore too late to
> request its own reply.
>
> This is the most likely way for the gate to observe nothing, which is precisely why **a missing reply is
> FATAL, never empty** (FR-005). Measured both platforms: with the query in place a reply is always produced;
> the realistic failure is total absence, not a partial read.

## Implementation sequencing (input to `/speckit-tasks`)

Ordered by Article VII §3 and FR-007, which coincide here.

1. **Emit the query, read the reply, and assert an expectation that is deliberately WRONG.** Observe the gate
   **red**, proving it reads real data. This comes first: a gate first seen green has not been shown to
   measure anything, and vacuity is this feature's dominant risk.
2. **Correct the expectation to the measured sets** (R4 — capi 1 entry, service 2). Gate goes green.
3. **Red — leak.** Revert the C-ABI isolation; the observed set moves **1 → 7** and the gate names the six
   third-party roots (asio, OpenSSL, zlib, OpenTelemetry, protobuf, abseil).
4. **Red — drop.** Remove an expected entry from the observed side; reachable **only** because FR-003a asserts
   equality rather than containment. This is the demonstration that justifies the clarify decision.
5. **Red — missing reply.** Delete the reply file; the gate fails naming it, and does **not** read as
   "no includes".
6. **Red — carrier deleted.** Remove the carrier target; the build fails by name. Ninja's phrasing is
   `ninja: error: unknown target '<name>'` — **measured in 086**, and *not* Make's "No rule to make target".
7. **Service leg red, reverting the service line ALONE**, capturing same-run evidence that the C-ABI leg
   stayed isolated (FR-007a — reverting capi reds both legs and proves nothing about service).
8. **Reconcile the inherited artifacts** (FR-011): 086 `contracts/include-interface.md` C-3, `spec.md`
   FR-009a, `checklists/abi.md` CHK006 — the property is no longer an open scope limit.

## Complexity Tracking

| Risk | Why plausible | Mitigation |
|---|---|---|
| The gate passes having measured nothing | The dominant failure mode; 086's Gate B found five P1s of this shape | Step 1 asserts a wrong expectation FIRST; the expected set is non-empty (R4) so empty ≠ pass; a missing reply is FATAL (FR-005) |
| The instrument works on one platform only | **Exactly what happened to 086's R9** — measured on a Linux fixture, prescribed, then failed under Conan/MSVC after sign-off | **Already closed**: R6 measured MSVC-under-Conan on the real consumer project; results identical. FR-010a's scope-out is not needed |
| Absolute paths make the comparison machine-specific | Observed paths embed the stage prefix, which differs per run and per platform (`/tmp/…` vs `C:/temp/…`) | Compare prefix-relative (R5); the File API emits forward slashes on both platforms (R6), so only the expected side needs normalising |
| Hard-coded reply filename breaks at the next configure | Reply names carry a content hash | Glob `target-<name>-*.json` or read the index; never hard-code a reply name |
| A green classification check is read as evidence | `isSystem` is uniformly `true` today, so that leg never varies in the happy path | Stated in research residuals and the contract, so a green run is not misread as having exercised classification |
| Doc drift after a mid-implementation change | 086 spent four Gate B rounds on exactly this | One authority (`contracts/system-include-interface.md`); every operational prescriber swept in the *same commit* as any mechanism change |
| The 086 artifacts are updated to claim more than 087 delivers | FR-011 edits another feature's contract | C-3's amendment states exactly which legs are now bound and by what, and leaves the reachability-matrix scope note intact |

## Gate A

*(populated by `/gate-a` — run after this plan, BEFORE `/speckit-tasks`)*
