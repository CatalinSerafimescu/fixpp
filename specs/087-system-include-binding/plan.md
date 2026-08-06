# Implementation Plan: System include directories bound at the installed-package consumer

**Branch**: `087-system-include-binding` · **Date**: 2026-08-04 · **Spec**: [spec.md](./spec.md)
**Issue**: [#234](https://github.com/CatalinSerafimescu/fixpp/issues/234) · **Inherits**:
`specs/086-capi-include-isolation/contracts/include-interface.md` **C-3** (`:122-149`)

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
That ordering is the deliberate correction of 086's failure mode, not a formality. The extraction command and
its **verbatim output for both platforms** are transcribed in `research.md` R6, with the reply-index filenames
and staged prefixes that distinguish two runs from one — added at Gate A round 1, where the claim was true but
unevidenced from the bundle alone.

## Technical Context

| | |
|---|---|
| **Language / build** | CMake ≥ 3.30 (measured on 3.30.0); no C++ source change |
| **Instrument** | File API `codemodel-v2`; query file written **before** configure; reply globbed as `target-<name>-*.json` |
| **Legs** | `fixpp::capi` (expect **1** entry), `fixpp::service` (expect **2**) — measured, R4 |
| **Platforms measured** | Linux/clang/Ninja/Release **and** MSVC BuildTools 2022/Ninja/Debug under Conan — **identical** results (R6) |
| **Test tier** | extends `fixpp::consumer::install-witness`; **no new ctest registration** |
| **Scale/Scope** | **1 new CMake script** — `tests/consumer/compare_system_includes.cmake`, with two documented modes (**compare** per leg, **leg-set** over the collected results; contract C-6.1) — + **1 new carrier target** (`probe_system_include_contract`). **10 files edited**, re-derived from contract §4a and §6a: 2 in `tests/consumer/` (`CMakeLists.txt`, `run_consumer_witness.cmake`) · 3 workflows (`tier1.yml`, `tier2.yml`, `tier3-libcxx.yml` — FR-014) · `src/capi/CMakeLists.txt` (§4a row 4) · 4 of 086's spec artifacts (§4a rows 1, 2, 3, 5). §4a rows 6 and 7 land in the two `tests/consumer/` files already counted. 2 expectations declared; 2 existing probe TUs read, none added unless a leg lacks one; **9 red demonstrations** + a green control *(re-sized at Gate A round 1; file list re-derived at round 2, where it omitted `src/capi/CMakeLists.txt`, two workflows and the 086 artifacts)* |
| **Unknowns** | **none blocking.** FR-009 discharged by R6; the spec's one deferred question (do compiler built-ins appear?) is answered **no**, on both platforms (R2/R6) |

## Constitution Check

| Article | Verdict | Basis |
|---|---|---|
| **II** — Language/compilers/platforms | **PASS** | No source change. Instrument verified on both Tier-1 (clang) and Tier-2 (MSVC) toolchains. |
| **III** — Build & dependency toolchain | **PASS** | No new dependency. File API is built into CMake; JSON is read with the tree's existing means. |
| **VI §2/§5** — Spec coverage discipline | **PASS** (§5) · **N/A** (§2) | **§5 (presence):** `spec.md` carries `## Normative References`, discharged the way 086 did (`specs/086-capi-include-isolation/spec.md:655-662`) — the FIX-normative set is empty (`grep -c "086\|087" spec/feature-catalogue.md` → 0, so `[const §VI.4]` is not engaged) and the governing constitutional/architectural authorities are named instead. *(Added at Gate A round 1; its absence was a direct `[const §VI.5]` violation.)* **§2 (canonical `Spec ref` format): NOT ENGAGED.** §2 governs *"Every OFFICIAL row's `Spec ref`"* (`.specify/constitution.md:161`) — a field of the **catalogue**, not cross-artifact citation hygiene inside a spec bundle — and this feature adds no OFFICIAL row, on the same grep that disengages `[const §VI.4]` above. §2 is therefore not engaged for the same reason §4 is not. *(A note, not the discharge: the bundle's inherited-artifact citations are repository-relative with clause identifiers, defined **once** in `contracts/system-include-interface.md` §4a and cited from `spec.md` FR-011 rather than restated — sound practice, but not the practice §2 governs.)* *(Basis corrected at Gate A instance 2 round 2: the previous §2 basis asserted a discharge of an obligation §2 does not govern. Nothing violates §2, engaged or not; the §5 discharge and the row's verdict are unaffected.)* |
| **VII §3** — TDD mandatory | **PASS** | The plan **mandates** writing the gate and observing the initial red before the first green — FR-007, SC-003 (four causes required). **Nine** red demonstrations are specified in contract §5 and required as delivery evidence. **Sequencing step 2** is the initial red and precedes the first green (step 3). *(Restated in the mandating voice at Gate A round 2 — the verdict and the count were right, the completed-state tense was not; nothing is implemented and `spec.md` is `Status: Draft`.)* |
| **VII §4** — No code without a test | **PASS** | The deliverable *is* a test. |
| **VII §8** — Label-selectable tests | **PASS** | Rides the existing `consumer` label; no new executable and no new registration. **FR-014 requires** the label's registration count to be asserted in CI before the run (contract §6), so label-selectability cannot silently degrade to selecting nothing. **The feature's edits outside `tests/` and `specs/` are four files**, recorded here because no article governs them directly and so that no file in the diff is uncovered by this table: `.github/workflows/tier1.yml`, `.github/workflows/tier2.yml` and `.github/workflows/tier3-libcxx.yml` (FR-014, contract §6a — every workflow that runs the witness, modelled on the `packaging` assertions at `tier1.yml:528-540` and `tier2.yml:371-384`), plus **`src/capi/CMakeLists.txt:63-67`** (FR-011, contract §4a row 4 — operational source documentation that would otherwise contradict the delivered mechanism). *(Re-derived at Gate A round 2: this row previously claimed the CI assertion **was already** made — it is not; `tier1.yml` mentions `consumer` only in comments at `:507`/`:509` and its sole count assertion is for `packaging` — and claimed `tier1.yml` was the **only** out-of-tree edit, which the §4a row-4 source edit added at round 1 already falsified.)* |
| **VIII** — Performance budgets | **N/A** | No runtime code path; configure-time only. |
| **IX §1** — Coverage | **N/A** | The gate ranges over `include/fixpp/<mod>/*` + `src/<mod>/*`, and measures **executed C++ translation units**. This feature changes **no C++ TU**: its only edit under `src/` is a comment block in `src/capi/CMakeLists.txt:63-67` (contract §4a row 4), which is a build script, not a coverable TU. *(Basis re-based at Gate A round 2 — the verdict survives, but the previous basis, "this feature edits none", became literally false once §4a row 4 put `src/capi/` in scope at round 1.)* |
| **IX §2/§3** — Sanitizers | **CI-DELEGATED** | **No executed C++ TU changed**; any added probe TU is compile-only (an `OBJECT` library, never linked and never run), so no sanitizer path is affected. Same disposition 086 recorded and Gate B accepted. *(Basis restated at Gate A round 1 — the previous "zero C++ TUs changed" contradicted the IX §4 row and Project Structure, both of which contemplate a new probe TU.)* |
| **IX §4** — Static analysis | **PASS** | Any new probe TU goes through clang-tidy/clang-format as 086's did; the sub-build already sets `CMAKE_EXPORT_COMPILE_COMMANDS=ON`. |
| **IX §5** — ABI check | **N/A** | `include/fix/**` byte-unchanged; no symbol, header or version-script change. |
| **X §1/§2** — ABI policy | **PASS** | Asserts an existing ABI-boundary property; does not move the surface. |
| **X §6** — ABI-affecting ⇒ four controls | **4 of 4 DONE** | `/clarify` **DONE** (3 questions, all resolved) · **user `/plan` sign-off DONE — granted 2026-08-04** · **Codex Gate A DONE — CONVERGED instance 2 round 2, user-signed-off 2026-08-04** (2 loop instances, 5 rounds, 3 Opus rewrites + 1 Codex fixer; record `research/G19-fix-fpml-iso20022/decisions/speckit/087-system-include-binding-gatea.md`) · **`/analyze` DONE 2026-08-05** — 7 findings, **0 CRITICAL**, requirement coverage 26/26 (18 FR + 8 SC); all remediation landed in `tasks.md`/`plan.md`, none in `spec.md`, so no step-6 re-run was triggered. The one HIGH was a prefix inconsistency that would have voided demonstrations #3 and #4's token assertions **without erroring** (see `tasks.md` T005a). *(Row updated twice on 2026-08-05: it read "2 of 4 … Gate A pending" at `/speckit-tasks`, then "3 of 4" until `/analyze` closed.)* Treated as ABI-adjacent to match 086's disposition, since the property asserted *is* the C-ABI consumption boundary. |
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
├── spec.md              # 18 FR / 8 SC / 3 user stories; all clarifications resolved
├── plan.md              # this file
├── tasks.md             # 44 tasks, T001–T044 (pipeline step 5, 2026-08-05); five countable sets
│                        #   pinned in its header + a requirement→task map over 18/18 FR, 8/8 SC
├── research.md          # R1–R7; BOTH platforms measured on the real consumer project
├── data-model.md        # E1 include entry / E2 observed set / E3 declared expectation /
│                        #   E4 per-leg result file; invariants I1–I7
│                        #   (this line listed three entities and omitted E1 and E4 until the
│                        #    step-9 audit — the same summarise-vs-do drift CHK018 found in spec.md)
├── contracts/
│   └── system-include-interface.md   # normative expected sets + comparison rules
├── quickstart.md        # reproduce every measurement and every red
└── checklists/
    └── requirements.md  # 16/16 — item "All mandatory sections completed" was FALSE at Gate A r1
                         #   (no `## Normative References`); closed there, see its Notes
```

### Source Code (repository root)

```text
tests/consumer/
├── compare_system_includes.cmake   # NEW — the comparator, a standalone `cmake -P` script with TWO modes
│                                   #   (contract C-6.1) — NOT an inline block in the driver:
│                                   #   * compare  (reply-dir, leg, install-prefix, expectation, result-file)
│                                   #     — VALIDATES ITS ARGUMENTS FIRST (unknown leg or EMPTY expectation
│                                   #     => LEG_ERROR, before the reply is located — the guard that makes
│                                   #     data-model I3 a runtime property), then parses the reply,
│                                   #     normalises against the install prefix, compares
│                                   #     as an unordered set matched BY PATH in two stages, writes a result
│                                   #     file naming that leg BEFORE terminating, and emits the COMPLETE
│                                   #     token set: LEAK / DROP / RECLASSIFIED, or one of MISSING_REPLY /
│                                   #     INPUT_ERROR / LEG_ERROR as a pre-comparison termination
│                                   #   * leg-set  (result-file list) — the C-6.4 "exactly two distinct
│                                   #     known legs" assertion; LEG_ERROR. Separately invocable so
│                                   #     demonstration #6a's missing-leg and duplicated-leg sub-cases are
│                                   #     pure invocation, with no tree edit (contract C-6.4, §5 row 6a)
├── CMakeLists.txt                  # + declared expectations for both legs (literals with rationale)
│                                   # + NEW target `probe_system_include_contract` — the 087 carrier; runs
│                                   #   compare mode once per leg, in CAPI-THEN-SERVICE order; the capi
│                                   #   result therefore exists before a service red can fail the build
│                                   #   (C-6.2, FR-007a same-run evidence),
│                                   #   then leg-set mode over the collected results (C-6.4)
│                                   # + FR-011 §4a row 6 — scope the :205-218 "nothing survives" rationale
│                                   #   to 086's instrument; 087 declares non-empty expectations here
├── run_consumer_witness.cmake      # + emit the File API query BEFORE the configure step
│                                   # + add `probe_system_include_contract` to _required_targets (FR-006)
│                                   # + update the _build_rc FATAL_ERROR text (contract C-6.3) — BOTH its
│                                   #   defects: "the 086 witness targets BY NAME", and its diagnosis that
│                                   #   an error here is "not that the code is broken", which a genuine
│                                   #   LEAK/DROP/RECLASSIFIED red now surfaces through
│                                   # + FR-011 §4a row 7 — name the three/four seam at :171-180
└── <probe TUs>                     # reuse existing probes whose link line already matches each leg —
                                    #   `probe_usage_requirements` (capi) and `probe_service_positive`
                                    #   (service) are the two read; add only what does not already exist

.github/workflows/tier1.yml         # + assert the `consumer` label's registration COUNT before running it
.github/workflows/tier2.yml         #   — ONE unconditional step per workflow, after `Build` and before the
.github/workflows/tier3-libcxx.yml  #   first test step (ctest -N needs a configured tree). Modelled on
                                    #   tier1.yml:528-540 / tier2.yml:371-384's `packaging` assertion;
                                    #   tier 2's copy uses `shell: bash` like its own. All three because
                                    #   the witness runs on every lane of every tier (FR-014, §6/§6a)

src/capi/CMakeLists.txt             # + FR-011 §4a row 4 — the :63-67 comment says system include dirs are
                                    #   NOT asserted; after 087 it must say they ARE, by the File API at the
                                    #   installed consumer, and that "no collected property" is WHY

specs/086-capi-include-isolation/   # + FR-011 §4a rows 1, 2, 3, 5 — contracts/include-interface.md C-3,
                                    #   spec.md FR-009a, checklists/abi.md CHK006, and a PROVENANCE-
                                    #   PRESERVING append to research.md:277-281 (never a rewrite)
```

**Structure Decision**: no new directory and no new registered test. The consumer witness already solves the
only hard part — configuring a standalone project against a staged install, which is the sole way to observe
an *installed* interface at all. This feature adds an observation to that existing sub-build.

**But the comparison gets its own identity.** It is a standalone script carried by a uniquely named new
target, not an inline block in the driver. An inline block leaves every named target buildable and the
witness green when it is deleted — the same defect class the driver's own comment at
`tests/consumer/run_consumer_witness.cmake:99-106` names, and the reason 086's `_required_targets` list exists
at all. Reusing an existing 086 target as the carrier would leave the 087 comparison deletable without any
name disappearing, and would make US2 acceptance scenario 3 undeliverable as written. *(Restructured at Gate A
round 1 — root cause #1.)*

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

Ordered by Article VII §3 and FR-007, which coincide here. **Every step is annotated with the FRs it
discharges**, and the closing line accounts for the FRs discharged elsewhere — so no FR reaches `tasks.md`
without either a step or an explicit disposition. *(FR-indexed at Gate A round 1; FR-008 and FR-012 previously
had no step, and this project has a recorded incident of `/speckit-tasks` silently dropping tasks.)*

1. **Build the instrument as a standalone comparator, and the carrier that runs it.**
   `tests/consumer/compare_system_includes.cmake` with its **two `cmake -P` modes** — **compare**
   (reply-dir / leg / install-prefix / expectation / result-file), which **validates its arguments first**
   (an unknown `leg` or an **empty `expectation`** ⇒ `LEG_ERROR`, before the reply is located — the guard
   that makes `data-model.md` I3 a runtime property rather than a property of the declared literal, C-6.4),
   then implements C-1's **two-stage,
   match-by-`path`** algorithm, normalising against the staged prefix, writing a per-leg result that names the
   leg **before** terminating, and emitting the **complete** token set; and **leg-set** (result-file list),
   the C-6.4 assertion, made separately invocable so demonstration #6a's sub-cases need no tree edit. Plus
   the new `probe_system_include_contract` target, which runs compare mode in **`capi`-then-`service`** order
   and then leg-set mode over the collected results on the green path. The driver emits
   the File API query **before** configure and names the new target in `_required_targets`, updating its
   `FATAL_ERROR` text for **both** of that message's defects in the same edit (C-6.3).
   → **FR-001, FR-001a, FR-002, FR-004, FR-005, FR-006** · contract §2, §2a, C-1, C-6
2. **Assert an expectation that is deliberately WRONG**, in the `LEAK` direction on the service leg
   (declare `include/service-iface` only) — **with the `capi` expectation already declared at its measured
   value**, which contract §5's demonstration-#1 box makes a requirement of the row rather than an incidental
   choice: the carrier runs `capi` first and short-circuits, so an absent or wrong `capi` expectation reds
   *that* leg (`LEG_ERROR` or a comparison red) and the service comparison is never reached. Observe the gate
   **red** naming `include/capi`. This comes before any green: a gate first seen green has not been shown to
   measure anything, and vacuity is this feature's dominant risk.
   → **FR-007** · demonstration #1 · Article VII §3
3. **Correct the *service* expectation to its measured set** (R4 — service 2 entries; capi's 1-entry
   expectation is already correct from step 2), declared as literals with
   a per-member rationale, never computed from the observation. Gate goes green.
   → **FR-003, FR-003a** · contract C-4 (review-time invariant — say so, do not claim it is enforced)
4. **Red — leak (package-side).** Apply contract §5's demonstration-#2 diff to `src/capi/CMakeLists.txt:110-112`
   (**not** a `PRIVATE`→`PUBLIC` keyword flip; `:125-128` is not touched) and re-stage. The observed set gains
   the umbrella include root and the third-party roots; **the count is recorded at demonstration time — no
   expected figure is stated, because none has been measured on a reverted `fixpp::capi`.**
   → **FR-007, SC-002** · demonstration #2
5. **Red — drop, and red — reclassified (reply-side).** Against a **copy** of a real reply directory: delete
   one `includes[]` entry (**`DROP`** alone); flip one entry's `isSystem` with both paths preserved
   (**`RECLASSIFIED`** alone — C-1 stage 1 claims the path-matched pair and removes it, so no `LEAK` or
   `DROP` accompanies it; this row is what makes the staging normative rather than an implementation note).
   Both invoke the **shipped** comparator. The drop is reachable only because FR-003a asserts equality rather
   than containment — the demonstration that justifies the clarify decision; the reclassification is the only
   exercise of the classification leg, which never varies in the happy path.
   → **FR-003a, FR-007, SC-003** · demonstrations #3, #4 · contract C-1
6. **Red — missing reply, input error, and leg error.** Against a copy: delete the per-target reply (and,
   separately, the whole reply directory) → `MISSING_REPLY` naming the artifact, never read as "no includes".
   Truncate the per-target JSON so it is present-but-unparseable → `INPUT_ERROR`. Drive the shipped script
   wrongly → `LEG_ERROR`, in **four** mandatory sub-cases, all pure `cmake -P` invocation with no tree or
   reply edit: compare mode with an unknown `leg`; leg-set mode over **one** result file (missing leg);
   leg-set mode over the **same file twice** (duplicated leg); and compare mode with an **empty
   `expectation` argument**, every other argument correct. The missing-leg sub-case is the one C-6.4's
   rationale is about and is **not** discharged by the unknown-leg one; the empty-expectation sub-case reds
   at argument validation, before the reply is located, and is what demonstrates that `data-model.md` I3 is
   a runtime property rather than a property of the declared literal.
   **Three distinct tokens for three distinct causes** —
   a corrupt reply and a mis-driven carrier are different defects, and one token for both would not
   discriminate them. → **FR-005, FR-008, FR-001a, SC-003, SC-004** · demonstrations #5, #6, #6a · contract
   C-2, C-6.4
7. **Red — carrier deleted.** Remove the **new 087 target** `probe_system_include_contract`; the build fails
   by name. Ninja's phrasing is `ninja: error: unknown target '<name>'` — **measured in 086**, and *not*
   Make's "No rule to make target". Second sub-case: delete `compare_system_includes.cmake` and keep the
   target; its own command fails. → **FR-006, SC-003** · demonstration #7
8. **Service leg red, restoring the PRE-086 service `$<INSTALL_INTERFACE:>` value ALONE** — the exact diff
   and its `git show cb397284` provenance are in contract §5's demonstration-#8 box. Observed becomes
   `{include, include/capi}` against expected `{include/service-iface, include/capi}`, so the asserted token
   set is **`LEAK` *and* `DROP`** from one mutation — the concrete case that falsified the old
   one-token-per-failure rule. Same-run evidence that the C-ABI leg stayed isolated comes from the **carrier's
   own capi-leg result** in that invocation: the carrier runs `capi` first, and `compare` writes that result
   before the later service red terminates the build. Read out of the same reply directory (§2b) — not a
   second staging run and not a manual follow-up read.
   → **FR-007a, SC-002** · demonstration #8 · contract C-1, C-6.2
9. **Assert the `consumer` label's registration count in CI — in ALL THREE workflows.** One unconditional
   step each in `tier1.yml`, `tier2.yml` and `tier3-libcxx.yml`, placed after that workflow's `Build` step
   and before its first test step (`ctest -N` needs a configured tree), modelled on the `packaging`
   assertions at `tier1.yml:528-540` / `tier2.yml:371-384`; tier 2's uses `shell: bash` as its own does.
   Expected count **1** as of 2026-08-04 on every lane — `CMakeLists.txt:421` is the sole `LABELS consumer`
   and `FIXPP_BUILD_CODEGEN_TOOL` (`:290`) defaults ON, overridden nowhere.
   → **FR-014, SC-008** · contract §6, §6a
10. **Run the full suite unchanged** and confirm the three properties 086 already compares still compare
    equal — 087 adds a leg and must not weaken, replace or re-scope the existing one.
    → **FR-012, FR-013, SC-005** · `quickstart.md` §5
11. **Reconcile the inherited artifacts** (FR-011). The amendment set is defined once at
    `contracts/system-include-interface.md` §4a — **seven** artifacts, including `src/capi/CMakeLists.txt:63-67`
    (operational source documentation), the two consumer-harness scope records at
    `tests/consumer/CMakeLists.txt:205-218` and `tests/consumer/run_consumer_witness.cmake:171-180` (rows 6
    and 7, added at Gate A round 2), and a **provenance-preserving** append to
    `specs/086-capi-include-isolation/research.md:277-281`. §4a now carries the exhaustiveness grep's actual
    output beside the claim, with each hit mapped to its row. → **FR-011, SC-007** · contract §4a

**FRs discharged outside this block, with their disposition** — so the accounting is complete:
**FR-009, FR-010, FR-010a** are discharged by measurement in `research.md` R6, transcribed there verbatim at
Gate A round 1 (both platforms measured on the real consumer project before any artifact prescribed the
mechanism; no scope-out is needed, and contract §1 records the toolchain scope of that measurement).
**SC-006** — *"the observing mechanism produces a non-empty observation on both Linux and MSVC-under-Conan,
recorded per platform, before any artifact prescribes it"* — is FR-009's success criterion and is discharged
by that same R6 record; it is named here because every other SC is annotated on a step above (SC-002/003/004/
005/007/008) or in `quickstart.md` (SC-001), and this block asserts its own completeness. *(SC-006 added at
Gate A instance 2 round 1; the block accounted for FRs only.)*

## Complexity Tracking

| Risk | Why plausible | Mitigation |
|---|---|---|
| The gate passes having measured nothing | The dominant failure mode; 086's Gate B found five P1s of this shape | Step 2 asserts a wrong expectation FIRST; the expected set is non-empty (R4) so empty ≠ pass — including a reply that exists and parses but yields zero entries, which `present` does **not** catch (`data-model.md` I3), and **`compare` rejects an empty `expectation` argument with `LEG_ERROR`** so I3's arithmetic cannot be voided by a mis-spelled expectation variable reaching it as ∅ (contract C-6.4, demonstration #6a sub-case *(iv)*); a missing reply is FATAL (FR-005); the comparator is a named target, so deleting it fails the build (FR-006); and **FR-014 requires** the `consumer` label's registration count to be asserted in CI on all three workflows, so a lane that stops registering the witness fails instead of passing on zero selected tests *(mandated, not yet made — restated in the mandating voice at Gate A round 2)* |
| The instrument works on one platform only | **Exactly what happened to 086's R9** — measured on a Linux fixture, prescribed, then failed under Conan/MSVC after sign-off | **Already closed**: R6 measured MSVC-under-Conan on the real consumer project; results identical. FR-010a's scope-out is not needed |
| Absolute paths make the comparison machine-specific | Observed paths embed the stage prefix, which differs per run and per platform (`/tmp/…` vs `C:/temp/…`) | Compare prefix-relative (R5); the File API emits forward slashes on both platforms (R6), so only the expected side needs normalising |
| Hard-coded reply filename breaks at the next configure | Reply names carry a content hash | Glob `target-<name>-*.json` or read the index; never hard-code a reply name |
| A green classification check is read as evidence | `isSystem` is uniformly `true` today, so that leg never varies in the happy path | Demonstration #4 induces a `RECLASSIFIED` red against a copied reply with one `isSystem` flipped and both paths preserved — so the leg is exercised by a demonstration, not only disclaimed. Still stated in the research residuals and the contract so a *green* run is not misread as having exercised it |
| Doc drift after a mid-implementation change | 086 spent four Gate B rounds on exactly this | One authority (`contracts/system-include-interface.md`); every operational prescriber swept in the *same commit* as any mechanism change |
| The 086 artifacts are updated to claim more than 087 delivers | FR-011 edits another feature's contract | C-3's amendment states exactly which legs are now bound and by what, and leaves the reachability-matrix scope note intact |

## Gate A

- Round 1 applied 2026-08-04: Codex P1=3 P2=7 P3=2; Opus post-judging P1=5 P2=8 P3=5; rewrite addresses root
  causes #1 (comparator has no standalone identity), #2 (load-bearing figures stated without their
  measurement), #3 (inherited-artifact references written as bare filenames). Reviews:
  `research/reviews/codex_087-system-include-binding_gate_a_review.md`,
  `research/reviews/opus_087-system-include-binding_gate_a_adversarial_review.md`,
  `research/reviews/orchestrator_087-system-include-binding_gate_a_r1_measurements.md`.
- Round 2 applied 2026-08-04: Codex P1=1 P2=2 P3=2; Opus post-judging P1=1 P2=5 P3=5; rewrite addresses root
  causes #1 (token vocabulary specified without the algorithm that assigns tokens), #2 (round-1 additions
  swept into the artifacts that do the work but not into those that summarise it), #3 (exhaustiveness claims
  anchored to commands whose output was never read back). Reviews:
  `research/reviews/codex_087-system-include-binding_gate_a_2_review.md`,
  `research/reviews/opus_087-system-include-binding_gate_a_2_adversarial_review.md`.
- Gate A EXHAUSTED at round 3 (Codex P1=1 P2=1 P3=1; Opus post-judging P1=1 P2=1 P3=3); user elected the
  Codex-fixer escalation 2026-08-04. Residuals applied by a fresh Codex session: the carrier's both-legs rule
  vs §5's red direct invocations (resolved by (a) leg-ordering), and the missing install-prefix in compare
  mode's tuple, plus three P3s. Reviews:
  `research/reviews/codex_087-system-include-binding_gate_a_3_review.md`,
  `research/reviews/opus_087-system-include-binding_gate_a_3_adversarial_review.md`.
- Instance 2 round 1 applied 2026-08-04: Codex P1=0 P2=1 P3=0; Opus post-judging P1=0 P2=1 P3=3. Root cause:
  C-2's `LEG_ERROR` cause list, C-6.4's definition and §5 row 6a's mandatory sub-cases disagreed on how many
  invocation faults exist and which are demonstrated — the empty-expectation cause was named once and carried
  nowhere, degrading R7 guard #1 from mechanised to declared. Reviews:
  `research/reviews/codex_087-system-include-binding_gate_a_i2_review.md`,
  `research/reviews/opus_087-system-include-binding_gate_a_i2_adversarial_review.md`.
- Instance 2 round 2 CONVERGED 2026-08-04: Codex P1=0 P2=0 P3=0; Opus post-judging P1=0 P2=0 P3=2 —
  user-signed-off. Both residual P3s applied as sign-off edits: the Article VI §2 basis (misattributed — §2
  governs the catalogue's `Spec ref` field and is not engaged, since 087 adds no OFFICIAL row; the PASS verdict
  and the VI §5 discharge are unaffected), and the per-leg result file's location pinned to the sub-build tree
  that `run_consumer_witness.cmake:46` wipes each run. Reviews:
  `research/reviews/codex_087-system-include-binding_gate_a_i2_2_review.md`,
  `research/reviews/opus_087-system-include-binding_gate_a_i2_2_adversarial_review.md`.

### Post-convergence amendment applied at `/speckit-tasks` (2026-08-05)

- **Demonstration #1 could not emit its asserted token as written.** §5 row 1 said only *"before **the** correct
  expectation is ever written … declare the `service` expectation as `include/service-iface` only"*, leaving the
  **`capi`** expectation's state unstated, and step 3 above said *"correct the **expectations**"* (plural). Read
  together, an implementer declares **neither** expectation at demonstration time. Composed with the two clauses
  added *after* row 1 was drafted — C-6.2's `capi`-before-`service` ordering (round 3) and C-6.4's rejection of an
  empty `expectation` argument (instance 2 round 1) — that reading reds the **capi** leg with **`LEG_ERROR`** at
  argument validation, short-circuits the carrier's `COMMAND` list, and never reaches the service comparison: the
  row records the wrong token, and the feature's only vacuity proof is not taken. Closed by pinning the `capi`
  expectation at its measured value as a **requirement** of the row, in a new demonstration-#1 box in contract §5,
  swept in the same commit to `plan.md` steps 2–3, `quickstart.md` §4 row 1, and `tasks.md` T013/T014. Authority-first
  per this bundle's own rule; recorded here so Gate B's spec-vs-delivered audit sees a post-sign-off contract edit.

### Round 1 — disagreements

- **Codex P3-2 (malformed constitutional citation at `checklists/requirements.md:35`) — NOT APPLIED.** Codex
  proposed rewriting `[const §XVI.3]` as `[const §XVI §3]`, calling the dot form malformed against "the
  bundle's stated article/clause form". The premise does not hold, and the decisive evidence is normative:
  **`.specify/constitution.md:87` prescribes the dot form** — *"Citation form: other documents cite articles
  as `[const §Roman.arabic]` (e.g., `[const §VIII.3]`)"*. The form Codex proposes appears **nowhere** in this
  bundle, nowhere in the constitution, and nowhere in recent specs; the dot form is near-universal
  (`specs/086-capi-include-isolation/` uses `[const §VI.5]`, `[const §X.6]`, `[const §X.1]`, `[const §VII.8]`,
  `[const §VI.4]`, `[const §IV.2]`; `specs/083-*/` yields ~40 hits, all dot form; the constitution's own
  cross-reference at `:223` is `[const §IX.5]`). Applying the change would introduce the only non-canonical
  citation in the bundle **and** violate `:87`. The line is left as written. *(The Opus adversarial review
  reached the same conclusion independently and refuted the finding; `:87` is an additional anchor found
  while applying it.)*

### Round 1 — findings NOT closed as the review shaped them

- **NEW-P2-2 (measurements taken through a hand-rolled configure).** The review offered two options: re-take
  the measurement through the driver, or enumerate the four configure divergences and dismiss each. The second
  was taken (`research.md` R1's divergence box) — the re-take is step 1 of implementation and cannot be done
  pre-tasks without writing the deliverable. Two of the four divergences (prefix, build type) turned out to
  have positive cross-platform evidence from R6 rather than only an argument; the other two are argued. The
  box says so explicitly rather than claiming equivalence.
