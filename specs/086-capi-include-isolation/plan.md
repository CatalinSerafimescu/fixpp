# Implementation Plan: C-ABI include isolation, delivered by the installed package

**Branch**: `086-capi-include-isolation` | **Date**: 2026-08-03 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/086-capi-include-isolation/spec.md`

**Tracking issue**: [#218](https://github.com/CatalinSerafimescu/fixpp/issues/218)

## Summary

`architecture.md` §7.4:503 claims `fixpp::capi` restricts its include path so C-ABI consumers cannot reach the
C++ headers, and §8 calls that a *structural* enforcement of the AGPL/commercial boundary. The installed
package does not deliver it: `fixpp::capi` carries no include directories of its own and inherits the whole
tree through `fixpp::capi_objects`. `fixpp::service` leaks the same claim independently, via its own
declaration at `src/service/CMakeLists.txt:26`.

**Approach — three additive installed include roots plus two target-graph edits.** `fixpp_capi` links its
objects `PRIVATE` (so CMake records `$<LINK_ONLY:>` and withholds the include directories while still linking
every object) and gains its own `$<INSTALL_INTERFACE:…/capi>`; `fixpp_service`'s whole-tree declaration is
replaced by `…/service-iface`. Two `install(DIRECTORY)` rules add the new roots. **Nothing moves** — the
existing header install rule is untouched, so every current include spelling and every current installed path
keeps working, including a bare `-I<prefix>/include` from a non-CMake consumer.

The issue's own proposed remedy — §7.4:503's literal `INTERFACE_INCLUDE_DIRECTORIES = include/fix/` — is
**rejected as unimplementable**: every C-ABI header is included *through* the `fix/` component, so that path
resolves to `include/fix/fix/c_api.h` and breaks every consumer. Evidence in `spec.md` → Context.

Every mechanism above is **measured or explicitly marked unproven** in `research.md`, not read off
`target_link_libraries` — the method `package-layout.md` §2a records as having been wrong in three places
across a three-level cascade. *(R2's stated basis, R5's mechanism and R8's scope were each corrected at Gate A
round 1; R3 gained the pasted generated text, R5's "the witness is run" premise was corrected against the
harness, R8's amendment's single unsatisfiable invariant was split in two, and **R10** was added — all at Gate
A round 2. The amendments and the residual "not yet proven" list are in `research.md`.)*

## Technical Context

**Language/Version**: CMake 3.28+ (measured on 3.30.0); C++23 for the witness translation units

**Primary Dependencies**: none new. Consumes 084's `install(EXPORT)`, package config, consumer-witness harness
and packaging-contents witness.

**Storage**: N/A

**Testing**: CTest + the standalone consumer-witness sub-project pattern (`tests/consumer/`,
`run_consumer_witness.cmake`); packaging assertions in `tests/packaging/`. GoogleTest is **not** involved —
every assertion in this feature is a compile/configure outcome, not a runtime one.

**Target Platform**: Linux (clang + gcc) locally; MSVC via CI. DEB/RPM/TGZ carry a `usr/` prefix component,
Windows ZIP does not.

**Project Type**: C++ library — build-system and packaging change only. **Zero production source or public
header changes**; the feature adds witness translation units under `tests/consumer/` and extends one existing
witness TU (FR-009).

**Performance Goals**: N/A — no runtime code path is touched, so Article VIII §3 (bench in the same PR) is not
triggered.

**Constraints**: strictly additive installed layout (FR-005a); no consumer-visible include spelling change;
export-set membership must not move (18 members); `fixpp_capi_objects`' shipped object files are checked at
`find_package` time and must stay valid.

**Scale/Scope**: **6** CMake/CMake-script files edited (`CMakeLists.txt`, `src/capi/CMakeLists.txt`,
`src/service/CMakeLists.txt`, `tests/consumer/CMakeLists.txt`, `tests/consumer/run_consumer_witness.cmake`,
`tests/packaging/run_package_contents_witness.cmake`), 2 install rules added, **~7** new witness translation
units (C++ positive ×2, C positive ×1, umbrella probe ×1, the two `try_compile` negative probe sources — one
engine header, one service header, consumed by **three** `try_compile` calls — plus the usage-requirement probe
TU), 1 existing witness TU extended (`consumer_capi_witness.cpp`, FR-009), 3 documents reconciled. *(Counted
2026-08-03 against the Project Structure below; the earlier "3 CMake files / ~4 targets" figures did not
match it. Raised from 5 files / ~6 TUs at Gate A r2, when the driver edit (FR-009a(ii), Article IX §4) and the
service-header negative cell were added.)*

## Constitution Check

*GATE: must pass before Phase 0. Re-checked after Phase 1 — see below.*

| Article | Requirement | Status |
|---|---|---|
| **IV §2** — the C ABI is the legal isolation boundary for AGPL/commercial dual licensing | This feature *delivers* the boundary the article assumes. Directly aligned. | ✅ |
| **X §1** — quoted exactly: *"The C ABI in `include/fix/c_api.h` is a versioned contract. Every change to it is reviewed against the contract; Codex Gate A is mandatory."* (`.specify/constitution.md:220`) | The article's literal trigger is a change **to that header**, and this feature changes no header byte. Gate A is run anyway (pipeline step 4, blocks `/speckit-tasks`) as the **deliberate conservative classification** recorded in the §6 box below. | ✅ planned |
| **X §6** — ABI-affecting features trigger **all four** mandatory controls | `/clarify` ✅ done · `/analyze` ⏳ step 6 · Gate A ⏳ step 4 · **user `/plan` sign-off ✅ GIVEN 2026-08-03** | ✅ |
| **X §2** — no C++ symbol leakage through the C ABI | Unaffected: no symbol, header content or version script changes. The existing `nm`/`dumpbin` gate keeps applying. | ✅ |
| **VII §3** — TDD mandatory, failing test first | **Binds task ordering.** Each isolation witness is written and observed **red** before the CMake change that makes it green. This is also FR-007's demonstrated-red obligation, so the two coincide. | ✅ ordering fixed |
| **VII §4** — no code without a test | Scoped to the **code-binding** requirements: every one of those maps to an assertion in `contracts/include-interface.md` §4. The article does not reach the requirements that bind **documents** rather than code — FR-013, FR-013a, FR-014, FR-015, FR-016, FR-017 and SC-004 / SC-006 — and claiming a §4 assertion for them would be false. Those are discharged by **sequencing steps 5–7** and by `quickstart.md` §7, whose export re-measurement is what the document reconciliation is written against *(Gate A r3 carry-forward #8)*. | ✅ |
| **VII §8** — group isolation-safe tests, select by ctest label (`.specify/constitution.md:178`) | New witnesses ride the existing standalone consumer sub-project, which VII §8 explicitly admits for "exact-set completeness gates". **Every selector is `-L <label>`, never `-R <exe-name>`** — `quickstart.md` §6 previously used `-R consumer_witness`, which matched nothing (the registered name is `fixpp::consumer::install-witness`) and exited 0; corrected at Gate A r1, with `--no-tests=error` added everywhere so an empty selection can no longer pass. | ✅ |
| **VIII §3** — no perf change without a benchmark in the same PR | Not triggered: no runtime code path changes. | ✅ n/a |
| **IX** — coverage / sanitizers / static analysis (`.specify/constitution.md:196-215`) | **PASS via the unchanged mandatory matrix.** §2 (ASan/UBSan/TSan), §5 (abidiff) and §6 (two-tier CI) are unchanged obligations that run regardless of this table. **§4 applies to the new witness sources**, and discharging it needs one build-system change that is named here rather than assumed. The new probe TUs are **structurally absent from the compile database clang-tidy reads**: `tests/consumer/` is a standalone project, never `add_subdirectory()`'d (`CMakeLists.txt:394-397`), driven only via `cmake -P` at test time, while `.pre-commit-config.yaml:33` runs clang-tidy with `-p=build/linux-clang-debug`. **And the sub-build emits no database of its own today**: it is configured with explicit arguments at `run_consumer_witness.cmake:77-90` that do **not** include `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`, and it does not go through the `_base` preset that sets it (`CMakePresets.json:12`) — verified, `EXPORT_COMPILE_COMMANDS` appears nowhere under `tests/consumer/`. **Discharge route**: add that one flag to the consumer configure step; the database then lands at `${CMAKE_BINARY_DIR}/_consumer_witness/build/compile_commands.json` and survives until the *start* of the next run (`file(REMOVE_RECURSE)` at `:46`, not at the end), so clang-tidy over the probe TUs runs after the witness has been run once (sequencing step 4a). `clang-format` is file-based and applies unconditionally. `cppcheck` and IWYU are wired into neither `.github/workflows/` nor pre-commit anywhere — a **pre-existing project-wide gap, not this feature's to close**. Recorded this way so `/speckit-verify` neither stumbles nor records the obligation as met while clang-tidy silently fell back to a guessed command line. Only §1's **incremental runtime-coverage** obligation is n/a: the gate ranges over `include/fixpp/<mod>/*` + `src/<mod>/*` and this feature edits no such file. | ✅ PASS (§1 n/a; §4 discharged **by** step 4a's flag **and** step 4b's clang-tidy run over the probe TUs — the flag alone only makes the database exist) |
| **XIX** — documentation | FR-013…FR-017 reconcile `architecture.md` §7.4:503/:504, §8, and `package-layout.md` §2a. | ✅ planned |

**No violations. Complexity Tracking is therefore empty and omitted.**

> ### ⚠️ Article X §6 — user sign-off on this plan is a CONSTITUTIONAL control, not a courtesy
>
> §6 names four mandatory controls for ABI-affecting features. Three are pipeline steps that run on their own;
> the fourth — **user `/plan` sign-off** — has no automation and is the one that gets dropped. It is recorded
> here so `/gate-a` can check it rather than assume it.
>
> Whether this feature is "ABI-affecting" is arguable — no symbol, header byte, or version script changes; only
> the *consumption* interface does. It is treated as in-scope deliberately, because the article exists to stop
> exactly this class of change from reaching consumers unreviewed, and because the surface being changed is the
> one Article IV §2 designates the legal boundary.
>
> **✅ SIGNED OFF — user, 2026-08-03**, on this plan as written: three additive installed include roots, the
> `PRIVATE`/`$<LINK_ONLY:>` mechanism measured in `research.md` R1–R3, `fixpp::service` in scope, isolation
> bound to by-name targets only. The remaining three §6 controls run as pipeline steps.

### Post-Phase-1 re-check

Re-evaluated against the delivered design. No new gate is triggered and no status above changes. Two findings
worth recording:

- **VII §3 (TDD) got sharper, not weaker.** Phase 0 R5 showed a witness that *links* can report failure for
  reasons unrelated to include reachability — it produced a false negative against a correct design on the
  first attempt. The red demonstration therefore has to distinguish *why* it went red, which is now a stated
  task obligation rather than an implicit one.
- **X §2 confirmed untouched.** `$<LINK_ONLY:>` changes only usage requirements; the archive's contents are
  byte-equivalent (R1), so the symbol surface the `nm`/`dumpbin` gate polices cannot have moved.

## Project Structure

### Documentation (this feature)

```text
specs/086-capi-include-isolation/
├── spec.md                          # what and why (30 FR, 10 SC — counted Gate A r1, RE-DERIVED r2)
├── plan.md                          # this file
├── research.md                      # Phase 0 — R1..R10; R2/R5/R8 amended + R9 at Gate A r1;
│                                   #   R3/R5/R8 amended + R10 added at Gate A r2
├── data-model.md                    # Phase 1 — roots, targets, reachability (E1..E4, I1..I11)
├── contracts/
│   └── include-interface.md         # Phase 1 — the normative per-target reachability contract (C-1..C-5)
├── quickstart.md                    # Phase 1 — end-to-end validation procedure
├── checklists/
│   └── requirements.md              # spec quality, 17/17 (16/16 at clarify + Normative References, Gate A r1)
└── tasks.md                         # NOT created by /speckit-plan — step 5, after Gate A
```

> Counts here are **derived**, not remembered: `grep -o '\*\*FR-[0-9a-z]*\*\*' spec.md | sort -u | wc -l` and
> the same for `SC-`. The pre-Gate-A figures ("36 FR, 8 SC") matched no measurement.

### Source code (repository root)

```text
CMakeLists.txt                       # +2 install(DIRECTORY) rules near :446-451; :446-451 itself UNCHANGED
src/capi/CMakeLists.txt              # :46 PUBLIC -> PRIVATE; + target_include_directories(fixpp_capi …)
src/service/CMakeLists.txt           # :12 whole-tree INSTALL_INTERFACE -> service-iface root
tests/consumer/
├── CMakeLists.txt                   # :40 project(... CXX) -> (... C CXX); + compile-only ✅ probe targets;
│                                    #   + the ❌ try_compile assertions (configure-time, asserted FALSE)
├── consumer_capi_witness.cpp        # EXISTS — links fixpp::capi; EXTENDED per FR-009 to reach the
│                                    #   session/dictionary closure AT LINK TIME (take the address).
│                                    #   BUILT AND LINKED, NEVER RUN: the driver runs only
│                                    #   consumer_witness (run_consumer_witness.cmake:197, ^PASS: at
│                                    #   :142-143); tests/consumer/CMakeLists.txt:83 — "Building and
│                                    #   linking IS the assertion". No runtime obligation. (Gate A r2)
├── consumer_witness.cpp             # EXISTS — links the umbrella, UNCHANGED (SC-003 requires it)
├── run_consumer_witness.cmake       # + -DCMAKE_EXPORT_COMPILE_COMMANDS=ON on the configure step (Art IX §4)
│                                    #   + read-back/compare of the usage-requirement file after the sub-build
│                                    #     (FR-009a(ii)) — a file(GENERATE) nothing reads asserts nothing
└── <new probe TUs>                  # ✅ compile-only: 12 C-ABI headers from C++ and from C; umbrella probe
                                     #   carrying <fix/c_api.h> + <fixpp/service/control_plane_factory.hpp>
                                     #   (FR-004 / FR-011c — witnessed by NOTHING today)
                                     # ❌ negative sources, consumed by try_compile, never built as targets
tests/packaging/
└── run_package_contents_witness.cmake   # + presence assertions (FR-010) + isolated-root containment (FR-010a/C-5)
.specify/architecture.md             # §7.4:503, §7.4:504, §8 reconciled (FR-013, FR-013a, FR-014)
specs/084-packaging-cpack-export/contracts/package-layout.md   # §2a reconciled; :45 -> :46 citation fix (FR-015)
```

**Structure Decision**: no new directory. The feature extends two existing witness tiers (`tests/consumer/`,
`tests/packaging/`) because both already solve the hard part — configuring a standalone project against a
staged install, which is the only way to observe an installed include interface at all. **No production C/C++
source or public header changes**: the C-ABI headers are already self-contained (zero `<fixpp/…>` includes,
measured), so isolation needs no source change. Two `CMakeLists.txt` files *under* `src/` do change
(`src/capi`, `src/service`) — that is the feature — and the earlier claim that "`src/` is untouched" was
literally false against this same structure block.

> ### ⚠️ The ❌ cells cannot be build targets — this constrains the structure, not just the wording
>
> `tests/consumer/` is a standalone sub-project driven by `tests/consumer/run_consumer_witness.cmake`, which
> runs **one** `cmake --build` and `message(FATAL_ERROR "consumer build failed")` on **any** non-zero exit
> (`:96-104`). A compile-must-fail *target* there reds the entire witness, so the earlier plan of
> "compile-only isolation probe targets" covering both polarities was **unbuildable** and would have produced
> `/speckit-tasks` tasks that cannot be implemented.
>
> **Delivered shape — MEASURED (`research.md` R9), not a decision rule**: ✅ cells are `OBJECT` targets (build
> failure reds the witness — correct polarity); ❌ cells are `try_compile(... LINK_LIBRARIES fixpp::capi)` at
> consumer-**configure** time with `CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY` (compile-only, no `main()`,
> restored after), asserted **FALSE** — a TRUE result raises `FATAL_ERROR` and the driver reports it at
> `:91-92`. Both polarities stay inside **one configured consumer**, which is what FR-008a's paired-evidence
> rule requires. R9 ran this shape against the Phase-0 fixture at both stages: the imported target's
> `INTERFACE_INCLUDE_DIRECTORIES` **do** propagate through `LINK_LIBRARIES`, there is no link stage, and the
> negative probe reads TRUE at ISO=OFF / FALSE at ISO=ON — so the pair discriminates. Still open, and owned by
> FR-007: the same shape on the **18-member tree under Conan**, in the real `tests/consumer/` sub-project, and
> under MSVC. Fallback if FR-007's red demonstration does not fire there: a dedicated probe sub-project with
> its own `cmake -P` driver asserting a non-zero build result — no longer expected, still checked. Full
> statement: `contracts/include-interface.md` §4a, `research.md` R5 + R9.

## Implementation sequencing (input to `/speckit-tasks`)

Order is constrained by Article VII §3 and by FR-007's demonstrated-red obligation, which coincide here.

1. **Witnesses first, observed RED.** Add the probes against the *current* package. The negative probes must
   fail to fail — i.e. they compile, so the `try_compile` result is TRUE and the assertion raises
   `FATAL_ERROR`, proving today's package leaks. That observation is FR-007 evidence for `fixpp::capi` and,
   separately, for `fixpp::service`.
2. **The `fixpp_capi` edit** — `PRIVATE` + its own `$<INSTALL_INTERFACE:…/capi>` + the install rule. Probes for
   `fixpp::capi` go green.
3. **The `fixpp_service` edit** — `src/service/CMakeLists.txt:26` + its install rule. Probes for
   `fixpp::service` go green. Kept a separate step because this line is **not** reachable from step 2: it is
   independently declared, and every other requirement can be satisfied while it survives (FR-011d).

   > **The independence is directional — the red demonstrations must respect it (FR-011e).** *Forward*, step 3
   > is genuinely not implied by step 2. *Backward* it is: `fixpp_service` links `fixpp_capi`
   > (`src/service/CMakeLists.txt:30`), so reverting `src/capi/CMakeLists.txt:94-96` to `PUBLIC` reds **both**
   > probes via the restored transitive include path. The service red demonstration must therefore revert
   > `src/service/CMakeLists.txt:26` **alone**, C-ABI isolation intact, and record `fixpp::capi`'s properties
   > from that run as proof. "One revert cannot stand in for the other" is true of the *fixes*, not of the
   > *reverts*.

3a. **The usage-requirement measurement** (FR-009a(ii) / C-3 leg 3) — the probe target + `file(GENERATE)` in
   `tests/consumer/CMakeLists.txt`, **and** the read-back-and-compare in `run_consumer_witness.cmake` after the
   build at `:96-104`. The two are one task: `file(GENERATE)` writes at generate time, so shipping the generate
   without the compare ships a gate that asserts nothing. Instrument measured in `research.md` R10.

4. **Packaging assertions** — presence (FR-010), prefix-normalised for the Windows ZIP, **and** isolated-root
   containment (FR-010a / C-5), which is the only assertion tracing FR-001.

4a. **Static-analysis plumbing for the probe TUs** (Article IX §4, Gate A r2 / N3) — add
   `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` to the consumer configure step (`run_consumer_witness.cmake:77-90`),
   then run clang-tidy against `${CMAKE_BINARY_DIR}/_consumer_witness/build/compile_commands.json` **after**
   the witness has been run once. Without the flag the probe TUs are in no compile database at all and
   clang-tidy would silently guess a command line — the obligation would read as met while nothing checked it.

4b. **Red-demonstration evidence hardening** (Gate A r2 / R2-4) — in `quickstart.md` §5, redirect each
   demonstration's CTest output into `$FIXPP_086_EVIDENCE/` and **assert** non-zero rather than printing
   `rc=$?`, and wrap the two demonstrations in a `trap` that restores both saved `CMakeLists.txt` copies on any
   exit. Ergonomic hardening of a human-driven demonstration whose restore point already exists; it blocks
   nothing and is deliberately *not* a Gate A precondition.

   Folded in at Gate A r3 *(carry-forwards #9, R3-3/R3-7)*:
   - **The `set -e` leg.** A verifier who pastes §§1–5 into one shell has `set -euo pipefail` in effect from
     §2. The demonstrations' `ctest` runs are *expected* to exit non-zero, so the shell aborts on the expected
     failure **before the restore line runs**, leaving the tree reverted. Each demonstration therefore runs
     under a controlled `set +e` with the status captured to its own file, and the `trap` above is what makes
     the restore unconditional rather than merely written down.
   - **`:295-297` is display-only.** The `grep -A7` reads at that point print the property block for the
     verifier to look at; they assert nothing. The assertion is the §3 compare against the two closed property
     maps, and the FR-011e proof in demonstration B is that same compare re-run on demonstration B's own
     staged prefix.
   - **clang-tidy over the probe TUs**, not merely a compile database that exists: after step 4a's flag lands
     and the witness has run once, run
     `clang-tidy -p build/linux-clang-release/_consumer_witness/build tests/consumer/probe_*.cpp` and record
     the result. A database nobody points a linter at discharges Article IX §4 no better than no database.
5. **Re-measure the export set** from a real generate run (FR-016). Predicted 18/18 unchanged; predicted is not
   measured.
6. **Document reconciliation** (FR-013, FR-013a, FR-014, FR-015) — written against the *measured* result of
   step 5, not against this plan's prediction.
7. **Close-out** — catalogue row, coverage index, issue #218 disposition (FR-017), and the parallel-worktree
   close-out items in **[parent-repo]**`/phases/phase-4/cleanup-phase.md` (**[parent-repo]** is the parent
   research repo; its absolute root is in `spec.md` → Normative References → "Cross-repository citations").

## Risks

| Risk | Why it is plausible | Mitigation |
|---|---|---|
| The real 18-member tree behaves unlike the 5-target repro | fixpp has two deliberate static-archive cycles and a Conan toolchain the fixture lacks | Step 5 re-measures on a real generate run before any doc is written (FR-016) |
| A witness passes for the wrong reason | Measured, not hypothetical: the research probe produced a false negative on its first attempt (R5) | Compile-only negatives; probe headers whose disappearance would itself be a defect (FR-008); paired evidence (FR-008a). **R9 measures the prescribed `try_compile` shape end-to-end**: no link stage at all, and the negative probe reads TRUE at ISO=OFF / FALSE at ISO=ON, so it discriminates rather than merely failing |
| The ❌ mechanism cannot express the assertion in the real harness | The first mechanism named was unbuildable (a must-fail target reds the whole witness); the replacement was initially unverified | **Retired at Gate A r1 for the fixture**: R9 proves `try_compile` + `LINK_LIBRARIES` propagates the imported include interface and is genuinely compile-only. Residual is real-tree/Conan/MSVC only — owned by FR-007, with R5's `cmake -P` fallback still named |
| The content gate goes green on Windows while finding nothing | A `usr/`-anchored glob matches nothing in the ZIP and reads as "no C-ABI headers shipped" | Prefix-normalise before comparing; never anchor a glob on `usr/` (`package-layout.md` §2) |
| Docs reconciled against the prediction rather than the measurement | The 084 §2a note records this exact failure — derived-not-measured export members, wrong in three places | Step 6 is ordered strictly after step 5 |
| Verify runs against the wrong tree | `/speckit-verify` and `/gate-b` hardcode the main checkout, which holds `085` | Substitute the worktree path; symlink decision records (**[parent-repo]**`/phases/phase-4/parallel-worktrees.md` §4) |
| Build-tree disk exhaustion | `/mnt/wsl/fixppbuild` has ~23 GB free; a Debug tree measures 22–31 GB | Reclaim before the verify matrix, or run Release-only locally and take Debug from CI |

## Gate A

- Round 1 applied 2026-08-03: Codex P1=3 P2=10 P3=3; Opus post-judging P1=5 P2=12 P3=8; rewrite addresses root
  causes **RC-1 (load-bearing facts asserted from prose and comments instead of from the artifact)**, **RC-2
  (the validation procedure was written without reading the harness it extends)**, **RC-3 (the
  document-correction queue was scoped by line label rather than by claim)**. Reviews:
  `research/reviews/codex_086-capi-include-isolation_gate_a_review.md`,
  `research/reviews/opus_086-capi-include-isolation_gate_a_adversarial_review.md`.

- Round 2 applied 2026-08-03: Codex P1=1 P2=5 P3=0; Opus post-judging P1=2 P2=4 P3=3; rewrite addresses root
  causes **RC-A (a verification instrument written from the *shape* of the artifact it inspects rather than
  from a sample of it)**, **RC-B (a reviewer's counter-proposal applied as literal wording without checking it
  against the bundle's own measurements)**, **RC-C (fixes applied at the sites a finding named rather than
  across the claim — RC-3 recurring)**. Reviews: **[parent-repo]**`/research/reviews/codex_086-capi-include-isolation_gate_a_2_review.md`,
  **[parent-repo]**`/research/reviews/opus_086-capi-include-isolation_gate_a_2_adversarial_review.md`
  (**[parent-repo]** = the parent research repo; absolute root in `spec.md` → Normative References →
  "Cross-repository citations" — these files do **not** resolve against this repository).

  **RC-A and RC-B were discharged by running things, not by reasoning about them.** Every instrument this round
  writes into the bundle was executed against a staged fixture on this host first, and the results are pasted
  into `research.md` rather than described: the blank-line export-file shape and the false-green `awk` (R3, and
  the replacement range measured at both stages), the `-A4`-vs-`-A7` window, the `file(GENERATE)` +
  `$<TARGET_PROPERTY:…,COMPILE_DEFINITIONS>` instrument and its OFF/ON discrimination (**R10**, new), the
  `ctest` status-line and summary-line formats behind §9's assertions, and the census counts behind the R2-6
  corrections. Two of this round's own counter-proposals were **narrowed after checking them**: the review's
  claim that the consumer sub-build already leaves a `compile_commands.json` is false as written — the
  sub-build is configured at `run_consumer_witness.cmake:77-90` without
  `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` and bypasses the `_base` preset that sets it (`CMakePresets.json:12`),
  so the Article IX row now names adding that flag as the discharge route; and the review's `.pre-commit-config.yaml:32`
  / `CMakeLists.txt:395-397` line labels were corrected to `:33` / `:394-397` by opening the files.

### Round 2 — disagreements

Findings where the applied fix differs from the proposal as written. Recorded so round 3 does not re-open them.

- **R2-5, alternative (a) vs (b) — (b) chosen.** The review's *required* leg (a second configure-time
  `try_compile` asserted FALSE for `<fixpp/service/control_plane_factory.hpp>` through `fixpp::capi`) is
  applied in full: it is now a row in `contracts/include-interface.md` §4 and a third `try_compile` in the
  sequencing. For the follow-on the review offered (a) an exact-set assertion over a probe target's evaluated
  `INCLUDE_DIRECTORIES` **or** (b) scoping SC-001/SC-001a's "0" to the named probes plus C-5 root containment —
  "(a) or (b), not both". **(b) is applied.** The reason is *not* that (a) needs machinery — FR-009a(ii) lands
  the `file(GENERATE)` + driver-comparison machinery regardless, and R10 measures that `INCLUDE_DIRECTORIES`
  evaluates cleanly to one entry on the fixture, so (a) is demonstrably implementable. It is that the review
  itself records the (b) evidence as *adequate* — `fixpp::capi`'s only include root after the change is
  `include/capi`, whose contents C-5/I11 pin — so (a) would add a second gate for a property already
  evidenced, while an exact-set MUST measured only on a 5-target fixture is the shape most likely to become
  unsatisfiable against the 18-member Conan tree and be weakened later. R10 item 6 records the measurement so a
  future feature can adopt (a) without re-deriving it.
- **N3, the compile-database fact — proposal corrected, obligation kept.** See the RC-A/RC-B note above: the
  route named in the Article IX row is "add the flag, then point clang-tidy at the database", not "the database
  is already there".

### Round 1 — disagreements

Findings where Codex's proposed fix was **not** applied as written, because the adversarial review rejected,
downgraded or narrowed it. Recorded so round 2 does not re-open them.

- **Codex #1, the `store.h` leg — REJECTED.** Codex speculated the 11-vs-12 discrepancy "may be an
  inherited-surface gap rather than a typo" and asked this feature to decide "whether `store.h` must first be
  delivered". It must not, and 086 owes no such ruling. The C-ABI surface is **DONE** (CA-001..010) and
  **GA-frozen at `1.5.0`, additive-only**
  (**[parent-repo]**`/REMAINING-WORK.md:7` — the same line `spec.md` already cites
  for the freeze), and `.specify/2i-capi.md:93` assigns the store *function* surface to design doc **2e**, not
  to 2i's v1.0 header split; the handle `fixpp_store_t` did ship (`include/fix/c_api/handles.h:61`). The
  delivered 12-file set **is** the intended v1.0 surface; a `store.h` would be a post-GA MINOR addition.
  **Applied instead**: the count leg only — every count, list, matrix row and success criterion corrected to
  12 files / 11 sub-headers, the census required to be derived by command, and the disposition recorded once
  in `spec.md` Context and `contracts` §1 so it is not re-opened.
- **Codex #13 (Article IX) — DOWNGRADED P2 → P3, and narrowed.** Codex framed the whole article as wrongly
  waived. Article IX **§1**'s coverage leg genuinely *is* n/a — the gate ranges over `include/fixpp/<mod>/*` +
  `src/<mod>/*` and this feature edits no such file — and §§2-6 are unchanged CI obligations that run
  regardless of what a plan table says. **Applied instead**: the residual only — the table now reads "PASS via
  the unchanged mandatory matrix", names **§4** (clang-tidy / clang-format / cppcheck / IWYU) as applying to
  the new witness sources so `/speckit-verify` cannot skip them, and marks only §1's incremental coverage
  obligation n/a.
- **Codex #16 (Article X §1) — NARROWED to a quoting fix.** Codex asked for the classification to be
  "identified as a deliberate conservative classification rather than the literal trigger". `plan.md`'s
  Article X §6 box **already** says exactly that ("Whether this feature is 'ABI-affecting' is arguable … It is
  treated as in-scope deliberately"). **Applied instead**: the §1 table cell now quotes the article verbatim
  and points at the existing box; no new prose was added for a statement the file already carried.
- **Codex #8 (C consumers) — NARROWED.** Codex read the gap as "C++ compilation cannot detect C-only
  incompatibilities in the supposedly pure-C headers". Header C-cleanliness is **already pinned in-tree**:
  `tests/capi/CMakeLists.txt:12` does `enable_language(C)` and `:13` / `:23` build two pure-C compile+link
  gates. **Applied instead**: only the real gap — C consumption of the *installed* interface
  (`project(fixpp_consumer_witness C CXX)` + one C probe TU). Every negative probe stays C++, because a C
  compiler rejecting a C++ header proves nothing about isolation.
- **Codex #7 (positive link witness) — REFRAMED, premise not accepted.** Codex's premise was that
  `$<LINK_ONLY:>` might silently break the transitive link graph; the adversarial review verified the
  mechanism holds. **Applied instead** on the finding's own ground: the witness is too weak *whatever* the
  mechanism does — `version.cpp` / `error.cpp` reference nothing outside `fixpp_capi_objects`, and the
  consumer link line carries the archive with **zero loose objects**
  (`tests/packaging/run_package_contents_witness.cmake:439-441`), so it would pass even if the archive edge
  were lost (FR-009). Codex's "inspect the evaluated link closure on the real Conan build" is already FR-016.
- **Codex #10 (export stability) — CONCLUSION KEPT, BASIS REPLACED.** Codex was right that `research.md` R2
  cited the wrong mechanism and that `CMakeLists.txt:608-584` does not classify `capi_objects`. But the
  *classification itself* is factually correct — `grep -rn "capi_objects" include/` returns **zero** hits and
  the only "link `fixpp::X`" instruction in any public header is
  `include/fixpp/config/toml_config_loader.hpp:7-8`. **Applied instead**: the classification is re-grounded in
  that measured predicate and membership in the explicit `FIXPP_EXPORT_TARGETS` enumeration
  (`CMakeLists.txt:596` → `install(TARGETS … EXPORT fixppTargets)` at `:770`); the `:608-621` citation is
  dropped, not the conclusion.
- **Codex #11 / #12 — SCOPE WIDENED rather than applied as written.** Both were correct but line-scoped.
  Applying them literally would have fixed the named lines and left the duplicates (RC-3). **Applied
  instead**: FR-013…FR-015 rewritten as **claim predicates**, with the known sites — `architecture.md:514`,
  `:515`, `:518`, `:537`, `:538`, `:543`, `:557`, `:560`, `:561`; `CMakeLists.txt:615`;
  `tests/consumer/CMakeLists.txt:75-79` — attached as non-exhaustive evidence.

## Next

Pipeline step 4 — **`/gate-a 086-capi-include-isolation`**, which blocks `/speckit-tasks` (const §XVII.1).
Article X §6 additionally requires **explicit user sign-off on this plan** before implementation.
