# Tasks: System include directories bound at the installed-package consumer

**Feature**: `087-system-include-binding` · **Branch**: `087-system-include-binding` · **Date**: 2026-08-05
**Issue**: [#234](https://github.com/CatalinSerafimescu/fixpp/issues/234)
**Input**: [spec.md](./spec.md) (18 FR / 8 SC / 3 user stories), [plan.md](./plan.md) (sequencing steps 1–11),
[research.md](./research.md) (R1–R7), [data-model.md](./data-model.md) (E1–E4, I1–I7),
[contracts/system-include-interface.md](./contracts/system-include-interface.md), [quickstart.md](./quickstart.md)
**Gate A**: CONVERGED instance 2 round 2, user-signed-off 2026-08-04 — 2 loop instances, 5 rounds, 3 Opus
rewrites + 1 Codex fixer. Record:
`research/G19-fix-fpml-iso20022/decisions/speckit/087-system-include-binding-gatea.md`.

> ### ⚠️ Worktree — read before running anything
> Every path below is relative to **`~/Work/Programming/fixpp-parallel`**, not
> `research/G19-fix-fpml-iso20022/library` (held by another session — **never** `git checkout` there;
> at Gate A it was on `tests/228-firstframe-bounded-window-witness`, then `088-firstframe-budget-timer-lifetime`).
> `/speckit-verify` and `/gate-b` hardcode the main checkout; substitute this path per
> `phases/phase-4/parallel-worktrees.md` §4.
>
> Unlike 086, **`.specify/decisions/` here is a symlink to the parent's tracked
> `research/G19-fix-fpml-iso20022/decisions/speckit/`** — records written from this worktree are real tracked
> files visible from either tree. No pre-`/gate-b` symlinking, and nothing to convert back at retirement.
>
> **CodeGraph IS indexed in this worktree as of 2026-08-05** (`state: complete`, 3539 files) — the note that
> said otherwise was written before the index existed. Pass **this worktree's** path,
> `projectPath: /home/catalin/Work/Programming/fixpp-parallel`, and never the main checkout's: an explicit
> `projectPath` silences the borrowed-index warning and would answer from another branch, which is now a
> *silent* wrong answer because that tree is indexed too.
>
> **But the index does not cover CMake.** Its languages are `c, cpp, python, xml, yaml` — **zero** `.cmake` or
> `CMakeLists.txt` files are in it. This feature's mechanism (`compare_system_includes.cmake`,
> `run_consumer_witness.cmake`, `tests/consumer/CMakeLists.txt`, `src/*/CMakeLists.txt`) is therefore invisible
> to CodeGraph: read it with **Read/Grep**, and treat "codegraph found nothing" about a CMake symbol as
> *no evidence*, never as absence. CodeGraph **is** authoritative for the C++ probe TUs the reply is read
> from — `probe_usage_requirements.cpp`, `probe_service_positive.cpp`, `probe_umbrella.cpp` — and for the
> three workflow YAMLs (indexed, 0 symbols: use it for file location, not structure).
>
> **Build discipline**: `-j2` maximum (wider OOM-kills the host); one owner per build directory;
> `export CCACHE_DIR=/mnt/wsl/fixppbuild/ccache`.

> ### The contract is the single authority
> `contracts/system-include-interface.md` is the authority for the 087 mechanism. Anything here, in `spec.md`,
> `plan.md`, `research.md` or `quickstart.md` that contradicts it is stale: **correct the contract first, then
> sweep every prescriber in the SAME commit** (T037). 086 spent four Gate B rounds learning that fixing a
> mechanism without sweeping its specification merely relocates the defect.

> ### TDD ordering is constitutional here, and it coincides with FR-007
> Article VII §3 requires the failing test first; FR-007 independently requires each must-fail assertion to be
> **observed red**. **T013 (the vacuity proof, demonstration #1) precedes the first green (T015).** A gate
> first seen green has not been shown to measure anything, and vacuity is this feature's dominant risk — 086's
> Gate B found five separate P1s of the shape "a gate that cannot fail, or that vanishes silently when removed".

> ### Evidence discipline for every red
> Record the **real exit status** (`> file` discards it — capture it), the **asserted diagnostic token**, and
> the **first diagnostic line**. An exit status alone does not identify which C-1/C-2 branch fired, so it does
> not discharge the branch the row exists to exercise (contract §5). Restore between demonstrations and
> re-confirm green.

**Total: 45 tasks** (T001–T044 plus **T005a**, added at `/speckit-analyze` remediation 2026-08-05 — the 086
T014a/T019a precedent). Tests are not optional in this feature — the gate *is* the deliverable.

**Countable sets this list must satisfy** (each was a Gate A finding; the counts are the cheap audit):

| set | count | tasks |
|---|---|---|
| §5 demonstrated-red rows + green control | **9 + 1** | T013, T015–T023 |
| C-6.4 `LEG_ERROR` causes / §5 row 6a sub-cases | **4** | T022 (i)–(iv) |
| §5 rows carrying **mandatory** sub-cases | row 5 → **2** · row 6a → **4** · row 7 → **2** | T020, T022, T023 |
| FR-011 / contract §4a amendment rows | **7** | T029–T035 |
| FR-014 workflows carrying the count assertion | **3** | T024, T025, T026 |
| R7 anti-vacuity guards | **6** | T008 (#1), T008 (#2), T011 (#3), T010+T008 (#4), T009 (#5), T024–T026 (#6) |

---

## Phase 1 — Setup

- [X] T001 Export `CCACHE_DIR=/mnt/wsl/fixppbuild/ccache` (unset by default it silently uses `~/.cache/ccache`) and confirm headroom with `df -h /mnt/wsl/fixppbuild` — a Debug tree measures 22–31 GB; build `-j2` maximum, per `quickstart.md` §0
- [X] T002 Create the durable evidence directory `$FIXPP_087_EVIDENCE` (default `~/fixpp-087-evidence`, **not** `/tmp`) per `quickstart.md` §0
- [X] T003 Run the profile-matched `conan install ... -of build/linux-clang-release` **before** `cmake --preset` — the preset hardcodes `build/linux-clang-release/conan_toolchain.cmake`, which does not exist in a fresh worktree (086 T003) — then configure and build `-j2`
- [X] T004 **For the manual File-API probe only** (`quickstart.md` §1's R1/R3 reproduction — *not* the shipped gate): `cmake --install build/linux-clang-release --prefix /tmp/fixpp-stage-087` into an **emptied** prefix (`cmake --install` does not remove stale files, so a dirty prefix makes a later comparison pass falsely). Any prefix works here — the comparison is prefix-relative (I1, C-3). `quickstart.md` §1 reuses `/tmp/fixpp-stage-086` instead; either is fine for the manual probe, and **neither is the prefix the gate uses** — see T005a
- [X] T005 Capture the pre-feature baseline in `$FIXPP_087_EVIDENCE/`: `ctest --test-dir build/linux-clang-release --no-tests=error --output-on-failure` → `ctest-before.txt` **with its exit code**, recorded **per-test status** (a name-set diff cannot see a red); plus `ctest --test-dir build/linux-clang-release -L consumer -N` → expect `Total Tests: 1` (the FR-014 baseline), and the option guards `CMakeLists.txt:290` (`FIXPP_BUILD_CODEGEN_TOOL` default ON, overridden nowhere), `:401`, `:421` (the sole `LABELS consumer`)
- [X] T005a **Pin the three paths the gate actually owns**, and use these in T007 and T017–T022 — *not* T004's prefix. `CMakeLists.txt:407` passes `FIXPP_WITNESS_WORK_DIR=${CMAKE_BINARY_DIR}/_consumer_witness`, and `run_consumer_witness.cmake:33-34` derives both children from it, so for `build/linux-clang-release`:

  | what | path |
  |---|---|
  | **install prefix** — the third argument to every direct `compare` invocation | `build/linux-clang-release/_consumer_witness/stage` |
  | **sub-build** — holds the per-leg result files (C-6.1) | `build/linux-clang-release/_consumer_witness/build` |
  | **reply directory** — the source for every `cp -r` in T017–T022 | `build/linux-clang-release/_consumer_witness/build/.cmake/api/v1/reply` |

  **Passing the wrong prefix silently invalidates the token assertions rather than erroring.** Under C-3 an observed entry outside the prefix stays in canonical absolute form, so *nothing* path-matches: stage 1 claims no pair, and the comparison degenerates to all-`LEAK` + all-`DROP`. Demonstration #3's pure `DROP` and demonstration #4's "`RECLASSIFIED`, **and that token alone**" both become unreachable — and the repairs an implementer reaches for first (relax the prefix rule, or build the expectation from absolute paths) violate C-3 and C-4 respectively. All three paths **survive a completed run**: `run_consumer_witness.cmake:46` wipes `_stage` and `_sub_build` at the **start** of a run, never at the end

---

## Phase 2 — Foundational (blocks every user story)

**⚠️ CRITICAL**: nothing can be observed until the query exists and the comparator is invocable. No expectation
is correct at the end of this phase — that is deliberate (T013 comes next).

- [X] T006 `tests/consumer/run_consumer_witness.cmake` — create `<sub-build>/.cmake/api/v1/query/codemodel-v2` **before** the configure `execute_process` (`:80`; `_sub_build` at `:34`; the wipe at `:46`). `tests/consumer/CMakeLists.txt` **cannot** do this — it executes *during* the configure it would be requesting a reply for (I6, contract §2a). This edit alone; the `_required_targets` and `FATAL_ERROR` edits are T011/T012
- [X] T007 **Re-take the R1/R3 measurement through the shipped driver**, not through `research.md` R1's hand-rolled configure, and confirm `probe_usage_requirements` → **1** and `probe_service_positive` → **2**, all `isSystem=true`, forward slashes; read them out of **T005a's reply directory** after the witness run and record to `$FIXPP_087_EVIDENCE/driver-observed.txt`. This is the deferred half of Gate A round 1's NEW-P2-2 disposition: R1's divergence box *argued* the four configure divergences are immaterial and explicitly deferred the re-take to implementation step 1. If the driver's numbers differ from R4, that is a contract-level finding — raise it, do not absorb it
- [X] T008 **NEW `tests/consumer/compare_system_includes.cmake`, `compare` mode** `(reply-dir, leg, install-prefix, expectation, result-file)`: **validate arguments FIRST** — unknown `leg` or **empty `expectation`** ⇒ **`LEG_ERROR`**, before any reply is located (C-6.4; this is what makes `data-model.md` I3 a *runtime* invariant rather than a property of the declared literal); then glob `target-<name>-*.json` (C-5 — reply names carry a content hash, **never** hard-code one); missing reply directory or missing per-target reply ⇒ **`MISSING_REPLY`** naming the artifact, **never** read as "no includes"; present-but-unparseable, or parsing without the expected `compileGroups` structure ⇒ **`INPUT_ERROR`** naming the file and the parse failure; normalise observed paths **prefix-relative** against `install-prefix` (C-3, I1); compare per **C-1's two ordered stages** — match by `path`, a matched pair with differing `isSystem` ⇒ **`RECLASSIFIED`** and **remove every path-matched pair before stage 2**, then observed-only ⇒ **`LEAK`**, expected-only ⇒ **`DROP`**; emit the **complete** token set (more than one C-1 token MAY fire and every non-empty class MUST be named); and **write the per-leg result file naming that `leg` BEFORE terminating, including on a red comparison** (C-6.1 — a `message(FATAL_ERROR)`-first implementation emits a token and leaves no result behind, the exact hole C-6.2 exists to prevent)
- [X] T009 **Same script, `leg-set` mode** `(result-file list)` — assert **exactly two** per-leg results with **distinct, known** legs (`capi`, `service`); missing, duplicate or unknown leg ⇒ **`LEG_ERROR`**. It reads the `leg` recorded in each result file; without that field it cannot distinguish "one file twice" from "two distinct legs". It MUST be **separately invocable** as `cmake -P` over an arbitrary list — that is what makes demonstration #6a's missing-leg and duplicated-leg sub-cases *pure invocation* with no tree edit (C-6.4). Both modes live in the one script on purpose: deleting the file still fails the carrier's own command, and adds no second entry to the out-of-tree file accounting
- [ ] T010 `tests/consumer/CMakeLists.txt` — declare the **NEW, uniquely named** carrier target `probe_system_include_contract`: `compare` mode once per leg in **`capi` then `service` order** (C-6.2 — load-bearing, not incidental: it is what keeps §5 row #8 satisfiable and makes FR-007a's same-run evidence a *gate output*), then `leg-set` over the collected results on the green path. The `result-file` paths the carrier passes MUST lie **under `CMAKE_BINARY_DIR`** as this file sees it — the `-B` tree `run_consumer_witness.cmake:46` wipes at the **start** of every run — so `leg-set`'s "exactly two" is an assertion about **this** run (C-6.1, I7). **Do not reuse an 086 target** as the carrier: that would make the 087 comparison deletable without any name disappearing (C-6.2), and would make US2 acceptance scenario 3 undeliverable as written
- [ ] T011 `tests/consumer/run_consumer_witness.cmake` — add `probe_system_include_contract` to `_required_targets` **by name**, so deleting either the target *or* the script fails the build (FR-006, R7 guard #3). Ninja's phrasing is `ninja: error: unknown target '<name>'` — measured in 086; the Makefile generators' "No rule to make target" wording never appears in this project
- [ ] T012 `tests/consumer/run_consumer_witness.cmake:135-142` — rewrite the `_build_rc` `FATAL_ERROR` text for **both** of its defects in this same edit (C-6.3): (a) it says the driver builds *"the **086** witness targets BY NAME"*, which a 087 entry falsifies at birth; (b) it asserts a **diagnosis** — that an error here *"means a gate was deleted or renamed, **not that the code is broken**"* (`:139-140`) — which becomes false the moment an ordinary `LEAK`/`DROP`/`RECLASSIFIED` red surfaces through this same branch. The replacement MUST enumerate both dispositions, and point the reader at the token and first diagnostic line printed directly below

**Checkpoint**: the comparator and its carrier exist and are required by name. No expectation is yet correct.

---

## Phase 3 — User Story 2 (P1): the gate cannot pass by measuring nothing — THE INITIAL RED

**Goal**: prove the gate reads *real data* before it is ever seen green.

**Independent Test**: with a deliberately wrong expectation and everything else correct, the gate goes red and
names the entry it did not expect.

- [ ] T013 [US2] **Demonstration #1 — the vacuity proof (expectation-side).** In `tests/consumer/CMakeLists.txt` declare the **`capi`** expectation **at its measured value** (`include/capi`, `isSystem=true`) and the **`service`** expectation as `include/service-iface` **only**, omitting `include/capi` — a strict, **non-empty** subset of the measured set — then run the witness against the real reply. **The correct `capi` expectation is a requirement of this row, not a choice** (contract §5's demonstration-#1 box): the carrier runs `capi` **first** and its `COMMAND` list short-circuits, so an *absent* `capi` expectation reds as **`LEG_ERROR`** at argument validation and a *wrong* one reds the capi comparison — either way the build stops and the service leg is never compared, so the row records the wrong token. "Before the correct expectation is written" scopes to the **service** leg alone. Expect **RED, token `LEAK`**, naming `include/capi` as observed-but-unexpected. Record exit status + token + first diagnostic line to `$FIXPP_087_EVIDENCE/red-01-vacuity.txt`. The **service** leg is used because the capi leg's single entry admits no non-empty strict subset, and the `LEAK` direction is by construction so it cannot be confused with #3's `DROP`. **This is the only class that reds the gate without touching the package, the reply or the invocation** — nothing green may be recorded before it

**Checkpoint**: the gate is proven able to fail for the right reason.

---

## Phase 4 — User Story 1 (P1) 🎯 MVP: both installed legs bound to a measured observation

**Goal**: `fixpp::capi` and `fixpp::service` each compare **exactly equal** to an expectation declared in the
tree, and each is demonstrated red for its own cause.

**Independent Test**: configure the consumer against the staged install, read the effective include list with
each entry's `isSystem`, and compare to the declared expectation — exact set equality, per leg.

- [ ] T014 [US1] `tests/consumer/CMakeLists.txt` — correct the **`service`** expectation to its measured set (R4); `capi`'s is already correct from T013, and this task adds its rationale comment if T013 did not. Both are **literals with a per-member rationale comment** (E3's `origin` column): `capi` = `include/capi` *(system)* — the C-ABI root 086 installs, `fixpp::capi`'s only `$<INSTALL_INTERFACE:>`; `service` = `include/service-iface` *(system)* — `src/service/CMakeLists.txt`'s own independently-declared install interface — and `include/capi` *(system)* — inherited through `fixpp::service`'s link to `fixpp::capi`. **Nothing may derive either from the observation it checks** (C-4, I4)
- [ ] T015 [US1] Run the witness **green** and record `$FIXPP_087_EVIDENCE/green-control.txt`: both legs compare equal, **exactly two** leg results — no more and no fewer (C-6.4) — and `ctest -L consumer --no-tests=error` selects **1** test. SC-001, and §5's control row
- [ ] T016 [US1] **Demonstration #2 — leak, package-side (`LEAK`).** Apply contract §5's demonstration-#2 **diff** to `src/capi/CMakeLists.txt:97-99` — collapsing the two-keyword arrangement back to `target_link_libraries(fixpp_capi PUBLIC fixpp_capi_objects)`. It is **not** a `PRIVATE`→`PUBLIC` keyword flip (that leaves a redundant second `PUBLIC "$<BUILD_INTERFACE:…>"` entry), and `:112-115` is **NOT** touched. Re-stage the install, re-run the witness. **The expectation is qualitative on purpose: record the count you observe; do NOT write a number back into the contract.** No figure has ever been measured on a reverted `fixpp::capi` — the reverted set retains `include/capi`, which R3's 7-entry `probe_umbrella` set does not contain, so the two are provably different. Restore and re-confirm green
- [ ] T017 [US1] **Demonstration #3 — drop, reply-side (`DROP`).** `cp -r` **T005a's reply directory** (a real one, produced by a real configure); in the **copy**'s `target-probe_service_positive-*.json` delete one entry from `compileGroups[].includes[]`; invoke the **shipped** `compare_system_includes.cmake` against the copy, passing **T005a's install prefix** — the one that configure actually used, `…/_consumer_witness/stage`, **not** T004's `/tmp/fixpp-stage-087`. Without the right prefix C-3's prefix-relative comparison cannot be performed and the row degenerates to all-`LEAK` + all-`DROP`. Expect red naming the deleted entry as expected-but-absent — reachable **only** because C-1 asserts equality, not containment. "Remove an entry from the observed side" is **not** achievable by editing the tree: `run_consumer_witness.cmake:46` wipes and reconfigures the sub-build every run
- [ ] T018 [US1] **Demonstration #4 — reclassified, reply-side (`RECLASSIFIED`, and that token ALONE).** In a **copy** of T005a's reply directory, flip one entry's `isSystem` `true`→`false` leaving **both paths identical**; invoke the shipped script against the copy with **T005a's install prefix**. **This row is the one the wrong prefix destroys most quietly**: with a non-matching prefix nothing path-matches, C-1 stage 1 claims no pair, and the mutation is never even compared — the run reds with `LEAK`+`DROP` and no `RECLASSIFIED` at all. Expect red naming the path and both classifications, with **no** accompanying `LEAK` or `DROP` — C-1 stage 1 claims the path-matched pair and removes it, leaving stage 2 nothing. **This row is why C-1's staging is normative**, and it is the **only** demonstration that exercises FR-003a's classification leg: `isSystem` is uniformly `true` in the passing state, so no happy-path run varies it and a comparator that parsed `path` and discarded `isSystem` would satisfy every other row
- [ ] T019 [US1] **Demonstration #8 — the service leg (`LEAK` *and* `DROP`, from one mutation).** Restore the **pre-086** service `$<INSTALL_INTERFACE:>` value in `src/service/CMakeLists.txt:24-27` **alone** — the exact diff and its `git show cb397284` provenance are in contract §5's demonstration-#8 box; it is a **restore of the pre-086 value**, not a deletion of the entry (a different mutation and a different token). `src/capi/CMakeLists.txt` is **NOT** touched: reverting capi reds **both** legs and proves nothing about service (FR-007a, inherited from 086 FR-011e). Observed becomes `{include, include/capi}` vs expected `{include/service-iface, include/capi}` ⇒ red naming **`include`** as observed-but-unexpected **and `include/service-iface`** as expected-but-absent. **Plus the same-run capi evidence**: the carrier's own `capi`-leg result from that invocation, still exactly `include/capi` — emitted because the carrier runs `capi` first and `compare` writes its result before the later service red terminates the build (C-6.2, §2b). Record **that**, from the carrier's output — **not** a manual follow-up read of a surviving work directory and **not** a second staging run. Restore and re-confirm green

**Checkpoint**: both legs are bound, each demonstrated red for its own cause, green restored.

---

## Phase 5 — User Story 2 (P1), continued: the remaining anti-vacuity guards

**Goal**: every enumerated way this gate could report success while asserting nothing is either demonstrated
closed or explicitly recorded as review-enforced.

**Independent Test**: delete or corrupt the mechanism's input, or drive it wrongly, and confirm it goes red
with a **distinguishable** diagnostic rather than reporting an empty observation as success.

- [ ] T020 [US2] **Demonstration #5 — missing reply (`MISSING_REPLY`), two MANDATORY sub-cases** (contract §5 row 5 — neither discharges the other: one is a directory that exists but holds no reply for this leg, the other is no directory at all, and §2a says the *second* is the realistic failure)**.** In a **copy** of a real reply directory: (a) delete the per-target `target-<name>-*.json`; (b) separately, delete the whole reply directory. Invoke the shipped script against each with the original install prefix. Expect red **naming the missing artifact** — not read as "no includes", and distinct from #6's token
- [ ] T021 [US2] **Demonstration #6 — input error (`INPUT_ERROR`).** In a **copy**, truncate the per-target JSON mid-object so it is **present but unparseable**; invoke the shipped script with the original prefix. Expect red naming the file and the parse failure, **distinguishable from `MISSING_REPLY` and from every C-1 token**. This is FR-008 / SC-004 — an unrelated failure reported distinguishably from a genuine violation
- [ ] T022 [US2] **Demonstration #6a — leg error (`LEG_ERROR`), FOUR mandatory sub-cases, all pure `cmake -P` invocation with no tree and no reply mutation**: *(i)* `compare` mode with an **unknown `leg`*; *(ii)* `leg-set` mode over **one** result file — the **missing-leg** case; *(iii)* `leg-set` mode over the **same result file twice** — the **duplicated-leg** case; *(iv)* `compare` mode with an **empty `expectation` argument**, every other argument correct and the reply **correct** — it must red at argument validation, *before* the reply is located. **Source the result files for *(ii)*/*(iii)* from T005a's sub-build tree** — T015's green run leaves both there, and they survive because the wipe is at the *start* of a run; copy them out before re-running the witness. Use a real one, not a hand-written stand-in, or the sub-case does not exercise the shipped result-file format. Record each of the four separately with its own exit status and first diagnostic line. **Sub-case *(ii)* is not discharged by *(i)***: C-6.4's own rationale is the missing-leg case — a comparator implemented for `capi` alone runs through an already-required target and reports green, silently deleting FR-001a and half of SC-001. **Sub-case *(iv)* is the only demonstration of the guard between an empty expectation and a green ∅-vs-∅ comparison** — it is what makes `data-model.md` I3 a runtime property. All four must be **distinguishable from `INPUT_ERROR`**: a corrupt reply and a mis-driven carrier are different defects with different owners
- [ ] T023 [US2] **Demonstration #7 — carrier deleted, two MANDATORY sub-cases** (contract §5 row 7 — they discharge different R7 guards: #3 by-name requirement and #4 script deletion, so (a) alone leaves #4 unproven)**.** (a) Delete the **new 087 target** `probe_system_include_contract` from `tests/consumer/CMakeLists.txt` → the build fails **by name**: `ninja: error: unknown target 'probe_system_include_contract'` (Ninja's phrasing, *not* Make's "No rule to make target"). (b) Delete `tests/consumer/compare_system_includes.cmake` and **leave the target** → the target's own command fails. **Deleting an 086 target instead would re-prove an 086 obligation, not this one.** Restore after each
- [ ] T024 [P] [US2] `.github/workflows/tier1.yml` — add **one unconditional step** (no `if:` guard) running `ctest --preset <preset> -L consumer -N`, parsing `Total Tests:`, and **exit-1ing** if the number is not **1**. Placed **after the `Build` step (`:492`) and before the first test step (`:511`)** — `ctest -N` reads `CTestTestfile.cmake` and needs a configured tree. Modelled on this file's own `packaging` assertion at `:528-540`. The two test steps (`:513`, `:544`) are mutually exclusive by `if:` and jointly cover every lane, so one unguarded step covers both without six conditional copies
- [ ] T025 [P] [US2] `.github/workflows/tier2.yml` — the same step, after `:323` and before `:360`, **with `shell: bash`**: this file's `ctest` steps are `shell: cmd`, and its own `packaging` assertion at `:371-384` already switches to bash for exactly this. **Model tier 2 on its own step, not on tier 1's.** Covers `:363` and `:389`
- [ ] T026 [P] [US2] `.github/workflows/tier3-libcxx.yml` — the same step, after `:314` and before `:339`; covers `:341` and `:349`, both unfiltered. **This workflow has no count assertion of any kind today**, and it is the one that carries libc++ — contract §1's "unmeasured toolchains are covered because CI executes the same gate" argument does not hold for libc++ until this step lands (§6a)
- [ ] T027 [US2] Prove the three assertions actually **fail** rather than merely observe: run the command locally (`ctest --test-dir build/linux-clang-release -L consumer -N | sed -n 's/^Total Tests: //p'` → `1`) and confirm the parse-and-exit-1 leg reds when fed a wrong expected number. A CI step that computes a value and never `exit 1`s is a false green, and this assertion exists precisely because `ctest -L` **exits 0 when it selects nothing**

**Checkpoint**: all six R7 guards are in place; guards #1–#5 are mechanised and demonstrated, #6 is asserted in
CI on all three workflows.

---

## Phase 6 — User Story 3 (P2): the expectation has a declared origin, not one derived from the run

**Goal**: a change to the allowed set is a visible, reviewable edit — not an automatic accommodation of
whatever the run produced.

**Independent Test**: inspect the definition site; confirm it is a literal with a per-member rationale and that
no code path recomputes it from the observation.

- [ ] T028 [US3] Inspect `tests/consumer/CMakeLists.txt`'s expectation block and `compare_system_includes.cmake`: confirm each member carries a stated reason, and that **no path derives the expectation from the observation** it is compared against. Record the inspection — and record explicitly that **C-4 / I4 are review-time invariants only**: no demonstration in §5 and no mechanised check would catch a future edit that reintroduced a computed expectation, so this record *is* the enforcement. Do not describe it as gate-enforced. Contract §6's residual list says the same about the carrier's own `leg-set` invocation line

---

## Phase 7 — Polish, reconciliation, verification & close-out

### FR-011 / contract §4a — the amendment set is **SEVEN** rows, defined once in §4a and cited elsewhere

> Extend §4a **first** if the sweep finds a site it does not list, then follow it here. Paths are
> repository-relative; every target is *086's* artifact or shipped source, never 087's own.

- [ ] T029 [P] §4a **row 1** — `specs/086-capi-include-isolation/contracts/include-interface.md` **C-3** (`:122-149`): the property is now **bound by 087** for the two legs in §1, by the File API instrument of §2. It must **not** claim more: 086's §1 reachability matrix still covers system include directories only at its two named header boundaries, and its scope note stays intact
- [ ] T030 [P] §4a **row 2** — `specs/086-capi-include-isolation/spec.md` **FR-009a** (`:373`; sub-clauses `:396`, `:417`): note the fourth property is now bound by 087, and that FR-009a's own instrument is unchanged
- [ ] T031 [P] §4a **row 3** — `specs/086-capi-include-isolation/checklists/abi.md` **CHK006** (`:16`): the recorded scope limit is **closed**, not an open follow-up
- [ ] T032 [P] §4a **row 4** — `src/capi/CMakeLists.txt:63-67`: **operational source documentation**, and the feature's only edit under `src/`. Today it says system include directories are **NOT** asserted; after 087 it must say they **are**, by the File API at the installed consumer, and that "no collected consumer property exists" is *why* the instrument is the File API rather than a target property
- [ ] T033 [P] §4a **row 5** — `specs/086-capi-include-isolation/research.md:277-281` (clause at `:280`): **provenance-preserving APPEND ONLY**, in a dated parenthetical — *not asserted by 086; subsequently bound by 087 (#234)*. **Never rewrite it** to read as though it always said this: it is a historical measurement record, and row 5 exists precisely so SC-007's universal wording is not quietly narrowed to "current normative artifacts"
- [ ] T034 §4a **row 6** — `tests/consumer/CMakeLists.txt:205-218` (clause at `:213`): scope 086's closing sentence (*"naming an expected non-empty list instead would have to enumerate what survives, and nothing does"*) to **086's instrument** — the three target-property comparisons declared there, for which it stays true — and record that 087 declares two **non-empty** expectations in this same file against a *different* observation: system-classified effective includes at the installed consumer, read through the File API. **Provenance-preserving**: 086's sentence is not deleted or reversed. *(Not `[P]` — same file as T010/T014.)*
- [ ] T035 §4a **row 7** — `tests/consumer/run_consumer_witness.cmake:171-180` (clause at `:173`): it enumerates **four** properties `$<LINK_ONLY:>` withholds then requires an empty effective set for **all three**, leaving the seam unexplained. Name it: 086's instrument here binds three by construction; the fourth is bound by 087 through the File API in `probe_system_include_contract`, listed in this same file's `_required_targets`. Leg 3's own scope is unchanged. *(Not `[P]` — same file as T006/T011/T012.)*
- [ ] T036 **SC-007** — re-run §4a's exhaustiveness grep and **read its output back**, not merely name it: `grep -rn "INTERFACE_SYSTEM_INCLUDE_DIRECTORIES\|SYSTEM_INCLUDE_DIRECTORIES" specs/ src/ tests/ | grep -v "^specs/087-system-include-binding/" | cut -d: -f1,2`. Confirm the **ten** in-scope hits map to the seven rows with no residue, and that no document still describes the property as an open scope limit. Paste the output into `$FIXPP_087_EVIDENCE/sc-007-grep.txt`. A site not in §4a is added **there first**, then swept — Gate A round 2 found this very claim anchored to a command whose own output falsified it. **If §4a's row count changes, sweep `spec.md` FR-011 in the same commit**: FR-011 names the count ("**seven** artifacts") and three of the seven paths, deliberately and with §4a marked as the authority, so it is the one place that goes stale if the table grows *(residual recorded at the step-9 checklist audit, CHK014)*

### Verification

- [ ] T037 **Doc-drift sweep.** If the mechanism changed at all during implementation, correct `contracts/system-include-interface.md` **first** and sweep `spec.md`, `plan.md`, `research.md`, `data-model.md`, `quickstart.md` and this file in the **same commit**. Specifically re-derive, against what actually shipped: `plan.md`'s Technical Context **"1 new script + 1 new carrier target, 10 files edited"**, its Article VII §8 row's **four out-of-tree files** (three workflows + `src/capi/CMakeLists.txt`), and its Article IX §1/§2 bases. If the implementer takes contract §6's rejected shared-`.github/scripts/` option, Technical Context, Project Structure **and** the VII §8 row are updated in the same commit
- [ ] T038 **MSVC — run the shipped gate under MSVC/Conan locally** via `reference_msvc_local_build_procedure` (sandbox `C:\temp\fixpp-parallel`, BuildTools under `C:\Program Files (x86)\`, toolset pinned; **the rsync source path must be ABSOLUTE** — a drifted cwd once copied the build tree into the sandbox). R6 measured the *instrument* on MSVC, but `compare_system_includes.cmake` is new CMake script code that has never executed there, and 086 cleared **six** Gate B rounds before failing on `windows-msvc-debug`. If it genuinely cannot be run locally, record that explicitly — the first CI run is then the first MSVC execution — rather than leaving it unstated
- [ ] T039 **FR-013** — `ctest --test-dir build/linux-clang-release -L consumer -N` still reports exactly **1**: the assertion rides `fixpp::consumer::install-witness` and adds **no** new registered test. Asserting a count is not a registration
- [ ] T040 **FR-012 / SC-005** — run the **full** in-tree suite (`ctest --test-dir build/linux-clang-release --no-tests=error --output-on-failure`), **not** `-L consumer`: the diff reaches `src/capi/` and `.github/`, and a label-filtered run misses the codegen count pin and the git-cleanliness gate — and `fixpp::dict::codegen-build-graph-check` reds on an **uncommitted** tree, so commit before the final run. Compare **per-test status** against T005's baseline; confirm the three properties 086 already compares still compare equal
- [ ] T041 If any new C++ TU was added (none is expected — `probe_usage_requirements` and `probe_service_positive` already exist and are the two targets read), run it through clang-format and clang-tidy; the consumer sub-build sets `CMAKE_EXPORT_COMPILE_COMMANDS=ON` so a compile DB exists (086 T053)
- [ ] T042 Close issue **#234** with the delivered disposition, recording that C-3's scope limit is closed and by what instrument

### Mandatory close-out (hard `/gate-b` preconditions — Article XVII §8)

- [ ] T043 [P] **Catalogue close-out** — flip every feature-owned OFFICIAL `spec/feature-catalogue.md` row to `done` with its evidence ref and add/update the matching `spec/coverage-index.md` entry. **Expected disposition for 087: none to flip** — this feature has no FIX-normative content and adds no OFFICIAL row (`grep -c "086\|087" spec/feature-catalogue.md` → **0**, which is also what disengages `[const §VI.4]` and Article VI §2 in `plan.md`'s Constitution Check). **Re-run the grep and record the zero as evidence** — do not skip the task on the strength of the spec's claim
- [ ] T044 **Feature-completeness audit (MUST BE THE FINAL TASK)** — assert against the merged tree that (i) every row in this file is `[X]` or carries an explicit waiver rationale; (ii) every **18 FR** and **8 SC** maps to a landed test AND a landed implementation (use the map below); (iii) every feature-owned OFFICIAL catalogue row is `done` with a matching `coverage-index.md` entry, or the recorded zero from T043. Also re-check the five countable sets in this file's header — 9 reds + control, 4 `LEG_ERROR` sub-cases, 7 §4a rows, 3 workflows, 6 R7 guards. Record the 100%-or-waived verdict in `.specify/decisions/087-system-include-binding-verify.md` under `## Completeness`. `/gate-b` pre-flight 4d **HARD-BLOCKS** without this record

---

## Requirement → task map

Every FR and SC has a task or an explicit disposition — `plan.md`'s sequencing block asserts this, and this
project has a recorded incident of `/speckit-tasks` silently dropping tasks.

| requirement | tasks |
|---|---|
| FR-001 | T006, T007, T008 |
| FR-002 | T006, T007, T008 (implementation) · **T013 (demonstration)** — the vacuity proof *is* FR-002's evidence: it is the only class that reds the gate without touching package, reply or invocation, so it is what shows the observed side is measured rather than defaulted or derived |
| FR-001a | T009, T010, T022(ii) |
| FR-003, FR-003a | T014, T017, T018 · T028 (the declared-origin inspection) |
| FR-004 | T008 (implementation — the complete token set) · **T017, T018, T019 (demonstrations)** — "the direction of the mismatch" is asserted by #3's pure `DROP`, #4's `RECLASSIFIED`-alone, and #8's `LEAK`+`DROP` from one mutation |
| FR-005 | T008, T020 |
| FR-006 | T011, T023 |
| FR-007 | T013, T016, T017, T018, T020, T021, T022, T023 |
| FR-007a | T010 (leg ordering), T019 |
| FR-008 | T021, T022 |
| FR-009, FR-010, FR-010a | **discharged pre-implementation** by `research.md` R6 (both platforms measured before any artifact prescribed the mechanism); re-confirmed through the shipped path by T007 and T038 |
| FR-011 | T029–T035 (seven rows) |
| FR-012 | T040 |
| FR-013 | T039 |
| FR-014 | T024, T025, T026, T027 |
| SC-001 | T015 |
| SC-002 | T016, T019 |
| SC-003 | T016 (added), T017 (removed), T020 (missing observation), T023 (carrier deleted) — four distinct causes |
| SC-004 | T021 |
| SC-005 | T040 |
| SC-006 | **discharged** by R6 with FR-009 |
| SC-007 | T029–T036 |
| SC-008 | T024–T027 |

**Tasks with no FR anchor, and why** — so the completeness audit does not read them as strays: T001–T005a (setup
and path pinning) · **T012** (the `_build_rc` `FATAL_ERROR` rewrite — a **contract C-6.3** obligation with no FR
behind it; it ships two defects at birth if skipped) · T028 ([US3], covered by FR-003 above) · T037 (the
contract's own authority-first sweep rule) · T041 (Article IX §4) · T042, T043, T044 (close-out).

**T027 and T039 deliberately run the same command** (`ctest -L consumer -N` → 1) for different claims: T027
proves the *CI step* reds when the count is wrong (a step that computes and never `exit 1`s is a false green);
T039 proves *this feature* added no registration (FR-013). Neither substitutes for the other.

---

## Dependencies

```
Phase 1 (Setup, T001–T005)
        │
        ▼
Phase 2 (Foundational, T006–T012)
        │
        ▼
Phase 3  T013  US2 — THE INITIAL RED  (vacuity proof; NO green before it)
        │
        ▼
Phase 4  T014 → T015 (green control) → T016, T017, T018, T019   US1
        │                                    │
        ▼                                    ▼
Phase 5  T020, T021, T022, T023  US2   ·  T024‖T025‖T026 → T027
        │
        ▼
Phase 6  T028  US3
        │
        ▼
Phase 7  T029‖T030‖T031‖T032‖T033 · T034 · T035 → T036 → T037 → T038 → T039 → T040 → T041 → T042 → T043 → T044
```

**Hard orderings, each for a stated reason:**

- **T013 before T015** — Article VII §3 and FR-007. A gate first seen green has not been shown to measure
  anything. This is the single most important ordering in the file.
- **T006 before T007** — no reply exists until the query file precedes the configure (I6).
- **T008 before T009/T010** — `leg-set` reads the `leg` field `compare` writes; the carrier invokes both.
- **T010's `capi`-before-`service` ordering before T019** — row #8 reverts the *service* line alone, so the
  capi result must already be written when the service leg reds. Without the ordering, FR-007a's same-run
  evidence degrades from a gate output to a manual read of a directory nothing pins.
- **T015 (green) before T016–T023** — every red demonstration needs a green control to restore to and
  re-confirm against.
- **T024–T026 before T027** — the false-green check needs the steps to exist.
- **T036 after T029–T035** — the exhaustiveness grep is re-run against the swept tree.
- **T040 after every source and workflow edit, and after a commit** — `codegen-build-graph-check` reds on an
  uncommitted tree.
- **T043 before T044** — the audit checks the catalogue state.

## Parallel opportunities

- **T024 ‖ T025 ‖ T026** — three different workflow files.
- **T029 ‖ T030 ‖ T031 ‖ T032 ‖ T033** — five independent 086-artifact / source edits, different files.
- **T017 ‖ T018 ‖ T020 ‖ T021 ‖ T022** — all operate on **copies** of a reply directory or are pure `cmake -P`
  invocations; none mutates the tree, so they do not serialise on a build.
- **Not parallel**: anything touching `tests/consumer/CMakeLists.txt` (T010, T013, T014, T034) or
  `tests/consumer/run_consumer_witness.cmake` (T006, T011, T012, T035) — same file. Nor T016/T019/T023, which
  mutate the tree and re-stage; `ninja` takes no lock, so two builds against one directory can corrupt
  `.ninja_deps`.

## Implementation strategy

**MVP = Phase 1 + Phase 2 + Phase 3 + Phase 4.** That closes #234's headline defect: the fourth usage
requirement `$<LINK_ONLY:>` withholds is bound for both installed legs, against a **measured** observation,
by a gate proven able to fail *before* it was ever seen green.

**Deliver in phase order and stop anywhere.** Each checkpoint is a coherent state: the binding without the CI
count assertion is a real gate with a disclosed lane hazard; the binding without the FR-011 sweep is a correct
mechanism with stale 086 documents (the status quo ante, minus the defect).

**Do not reorder Phase 3 after Phase 4.** The whole feature exists to remove gates that pass having asserted
nothing; a version of it whose own gate was first observed green would reproduce the defect class inside its
own delivery. Three of Gate A's four decisive catches were exactly that shape.

## Notes

- **`[P]` tasks** = different files, no dependency on an incomplete task.
- **The contract wins.** Any conflict between this file and
  `contracts/system-include-interface.md` is resolved in the contract, then swept here (T037).
- **Do not put a number on demonstration #2.** Its expectation is qualitative *on purpose*; the reverted
  `fixpp::capi` cardinality has never been measured, and substituting a second inferred figure would reproduce
  the defect the contract's demonstration-#2 box exists to remove.
- **Never hard-code a reply filename.** `target-<name>-<config>-<hash>.json` — the hash changes every
  configure (C-5, I5).
- **Pass T005a's install prefix — `…/_consumer_witness/stage` — to every direct `compare` invocation** in
  T017–T022, never T004's `/tmp/fixpp-stage-087`. The copied replies preserve absolute paths from the tree
  configure actually staged, and a standalone `cmake -P` inherits no CMake variables. A wrong prefix does not
  error: under C-3 nothing path-matches, so the comparison degenerates to all-`LEAK` + all-`DROP` and quietly
  voids the token assertions of #3 and #4.
- **Version pins**: nothing in this feature adds or bumps a dependency. If one ever appears here, verify the
  current published version against the registry rather than copying a figure out of an anchor doc.
- **Commit after each task or logical group**; `-j2` maximum; one owner per build directory.
