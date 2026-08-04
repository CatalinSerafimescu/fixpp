# Tasks: C-ABI include isolation, delivered by the installed package

**Feature**: `086-capi-include-isolation` · **Branch**: `086-capi-include-isolation` · **Date**: 2026-08-03
**Input**: [spec.md](./spec.md) (30 FR / 10 SC / 6 user stories), [plan.md](./plan.md),
[research.md](./research.md) (R1–R10), [data-model.md](./data-model.md),
[contracts/include-interface.md](./contracts/include-interface.md), [quickstart.md](./quickstart.md)
**Gate A**: converged round 3, 2 rewrites, user-signed-off 2026-08-03.

> ### ⚠️ Worktree — read before running anything
> Every path below is relative to **`~/Work/Programming/fixpp-parallel`**, not
> `research/G19-fix-fpml-iso20022/library` (held by another session on `085-fold-flat-cap-loop` — **never**
> `git checkout` there). `/speckit-verify` and `/gate-b` hardcode the main checkout; substitute this path.
> `.specify/decisions/` is gitignored and per-tree — symlink the records before `/gate-b`, and turn them back
> into real files before the worktree is retired. See `phases/phase-4/parallel-worktrees.md` §4/§6.
>
> **Build discipline**: `-j2` maximum (wider OOM-kills the host); one owner per build directory;
> `export CCACHE_DIR=/mnt/wsl/fixppbuild/ccache`.

> ### TDD ordering is constitutional here, and it coincides with FR-007
> Article VII §3 requires the failing test first. FR-007 independently requires each must-fail assertion to be
> **observed red**. The two coincide, so the ordering below is not stylistic: **every probe is written and
> observed red (T-red tasks) before the CMake edit that makes it green.** A probe that was never seen red is
> not evidence (`feedback_sanitizer_canary_must_be_proven_red`).

> ### Two traps this feature already paid for — do not re-enter them
> 1. **A ❌ assertion may never be a build target that must FAIL.** `run_consumer_witness.cmake:96-104` raises
>    `FATAL_ERROR` on *any* non-zero build exit, so a must-fail target reds the whole witness. Since Gate B r2
>    the ❌ cells assert the opposite — `__has_include` + a unique-token `#error`, which must **compile** — so
>    since r3 they ARE ordinary `OBJECT` targets, named in the driver's `_required_targets`, and building them
>    is the assertion (contracts §4a **r3 box**). They were configure-time `try_compile` calls in between;
>    that form cannot resolve Conan's imported-target closure and failed every MSVC Debug CI run.
> 2. **Never terminate an extraction range on a blank line.** CMake emits one between `add_library()` and
>    `set_target_properties()`; the round-1 `awk '/…/,/^$/'` captured two identical lines and `diff` exited 0
>    *unconditionally*. Anchor on `/^set_target_properties\(<target> PROPERTIES$/,/^\)$/`.

**Total: 60 tasks** (T001–T058 plus T014a and T019a, added at `/speckit-analyze` remediation — see the Gate A + analyze note below). Tests are not optional in this feature — the witnesses *are* the deliverable.

---

## Phase 1 — Setup

- [X] T001 Export `CCACHE_DIR=/mnt/wsl/fixppbuild/ccache` and confirm headroom with `df -h /mnt/wsl/fixppbuild`; reclaim a stale build tree if free space is under ~35 GB (a Debug tree measures 22–31 GB)
- [X] T002 Create the durable evidence directory `$FIXPP_086_EVIDENCE` (default `~/fixpp-086-evidence`, **not** `/tmp`) per `quickstart.md` §0
- [X] T003 Run the profile-matched `conan install ... -of build/linux-clang-release` **before** `cmake --preset` — the preset hardcodes `build/linux-clang-release/conan_toolchain.cmake`, which does not exist in a fresh worktree (Gate A r2 finding R2-2)
- [X] T004 Configure and build `linux-clang-release` (`-j2`), then `cmake --install --prefix /tmp/fixpp-stage-086` to produce the pre-change staged install
- [X] T005 Record the required option set in the evidence dir — `FIXPP_BUILD_TESTS=ON`, `FIXPP_BUILD_CODEGEN_TOOL=ON`, `FIXPP_BUILD_OTEL=ON ⇒ FIXPP_PACKAGING_ENABLED=ON` — with the guard citations (`CMakeLists.txt:349`, `:389`, `:401`, `:126-129`, `:83`); a wrong preset silently deregisters the tests this feature asserts with

## Phase 2 — Foundational (blocks every user story)

- [X] T006 Capture the **pre-feature baseline**: pin `BASE=$(git merge-base HEAD origin/main)`, record it to `$FIXPP_086_EVIDENCE/baseline-commit.txt`, `git worktree add /tmp/fixpp-086-base "$BASE"` (a **third** worktree — the main checkout must not be switched), `conan install` + configure + build + install it to `/tmp/fixpp-stage-086-base`
- [X] T007 Capture `$FIXPP_086_EVIDENCE/before.txt` (installed manifest) and `ctest-before.txt` **with exit code**, asserting the baseline suite is green or enumerating its pre-existing failures — SC-003a/SC-007 evidence. Start from an emptied stage prefix (`cmake --install` does not remove stale files, so a dirty prefix makes `comm -23` pass falsely)
- [X] T008 Convert `tests/consumer/` to `project(fixpp_consumer_witness C CXX)` so the C-language probe can exist (contracts §4; closes US1's "C or C++ integrator" promise for the *installed* interface)
- [X] T009 Add the three ❌ probe targets to `tests/consumer/CMakeLists.txt` as compile-only `OBJECT` libraries linked against the imported target (`probe_capi_negative`, `probe_capi_negative_service` → `fixpp::capi`; `probe_service_negative` → `fixpp::service`), each body `__has_include` + a unique-token `#error` so it compiles iff the forbidden header is unreachable, and add all three to `run_consumer_witness.cmake`'s `_required_targets` so a deleted probe fails the build by name (`ninja: error: unknown target '<name>'` — measured; the Makefile generators' "No rule to make target" phrasing never appears here). Building them IS the assertion — the driver's existing non-zero-exit `FATAL_ERROR` (`:96-104`) is the gate; output containing `FIXPP_086_FORBIDDEN_HEADER_REACHABLE` is a leak, any other failure a broken probe, both fatal and both readable from the verbatim compiler output. **Amended at Gate B r3**: this task read "add the `try_compile` scaffolding … `CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY`, restored after use" until CI proved `try_compile` cannot resolve Conan's `CONAN_LIB::…_DEBUG` closure on MSVC Debug (contracts §4a r3 box). R9's measurement of the retired form stands but is Linux/clang-scoped
- [X] T010 Add the `file(GENERATE)` usage-requirement probe **and its driver read-back** after the sub-build — a `file(GENERATE)` nothing compares asserts nothing (contracts §4, FR-009a(ii), instrument measured in R10)

---

## Phase 3 — User Story 1: a C-ABI consumer cannot reach the C++ surface (P1) 🎯 MVP

**Goal**: linking only `fixpp::capi` from the installed package reaches all 12 C-ABI headers and **no**
`<fixpp/...>` header. **Independent test**: standalone consumer, `find_package(fixpp)` + one
`target_link_libraries` line, nothing else.

### Probes first — observed RED (Article VII §3 / FR-007)

- [X] T011 [P] [US1] Add the C++ positive probe TU covering **all 12** C-ABI headers (`fix/c_api.h` + the 11 sub-headers) in `tests/consumer/probe_capi_positive.cpp`
- [X] T012 [P] [US1] Add the **C** positive probe TU (same 12 headers, compiled as C) in `tests/consumer/probe_capi_positive_c.c`
- [X] T013 [P] [US1] Add the ❌ engine-header probe source `tests/consumer/probe_capi_negative.cpp` (`#include <fixpp/wire/parser.hpp>`) — probe a header whose own disappearance would itself be a defect (FR-008)
- [X] T014 [P] [US1] Add the ❌ **service-header** probe source `tests/consumer/probe_capi_negative_service.cpp` (`#include <fixpp/service/control_plane_factory.hpp>`) — a **distinct** matrix cell: a mis-wired `fixpp::capi` that picked up `include/service-iface` would leak this while the engine probe still passed
- [X] T014a [US1] **Extend `tests/consumer/consumer_capi_witness.cpp` per FR-009** — add a **non-elidable** reference — **a CALL reached from a branch the compiler cannot fold** (Gate B r2 P2 #7). A namespace-scope pointer is NOT sufficient: `--gc-sections` or LTO can discard the data section holding it. The TU is never executed, so the call carries no runtime obligation and may be one that would fail if it ever ran to `fixpp_dict_load_from_xml` **and** `fixpp_engine_create`, so the witness pulls the **session/dictionary closure** out of the archive at *link* time. Today it references only `fixpp_library_version` + `fixpp_strerror`, whose objects need not reference session/dictionary/transport/TLS or either static-archive cycle — so it would stay green even if `$<LINK_ONLY:>` silently dropped a real transitive edge. Runtime behaviour is IRRELEVANT here: `consumer_capi_witness` is built and linked but **never executed** (the driver runs `consumer_witness` only), so a call that would fail if it ever ran is perfectly acceptable
- [X] T015 [US1] Wire T011–T014 into `tests/consumer/CMakeLists.txt` against `fixpp::capi`; run the witness and **record both ❌ probes reporting reachable=TRUE** (i.e. the headers ARE reachable) against the unfixed package — this is the FR-007 red observation for the C-ABI leg, written to `$FIXPP_086_EVIDENCE/`

### Then the edit that makes them green

- [X] T016 [US1] `src/capi/CMakeLists.txt`, **the `target_link_libraries(fixpp_capi …)` call** (cited by CONSTRUCT — this is an instruction someone acts on, and 086's own comment block moved it from `:46` to `:94-96`) — `PUBLIC → PRIVATE fixpp_capi_objects` so CMake records `$<LINK_ONLY:>` and withholds the include directories while still linking every object (measured: the archive still absorbs its objects, R1)
- [X] T017 [US1] `src/capi/CMakeLists.txt` — add `target_include_directories(fixpp_capi PUBLIC "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>" "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/capi>")`; BUILD stays permissive so in-tree is untouched (R8/I9)
- [X] T018 [US1] `CMakeLists.txt` (near `:446-451`) — add `install(DIRECTORY "${CMAKE_SOURCE_DIR}/include/fix/" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/capi/fix")`. **Do not** add any `PATTERN … EXCLUDE` to the existing rule (FR-005a; note `:449-450` already carries two unrelated exclusions)
- [X] T019 [US1] Rebuild + re-install; confirm all four probes now behave per contracts §1 and record the paired evidence (positive TRUE **and** negative FALSE from the same configured consumer — FR-008a: the positive alone is equally consistent with the defect being present)
- [X] T019a [US1] Run `quickstart.md` §3 and **record its evidence** — the direct-property-delta assertion (C-3 leg 2 / FR-009a(i)): extract each target's property block with a range anchored on `/^set_target_properties\(<target> PROPERTIES$/,/^\)$/` (**never** a blank-line terminator) and compare the observed delta against the closed enumeration — OFF `{INTERFACE_LINK_LIBRARIES}`, ON `{INTERFACE_INCLUDE_DIRECTORIES, INTERFACE_LINK_LIBRARIES}` — so an unexpected **third** changed property fails. C-3 states its three legs are non-substitutable, and this one is otherwise reachable only by someone reading the quickstart rather than the task list (mirrors how T038 mirrors §7)

**Checkpoint**: US1 is independently deliverable — the C-ABI isolation ships even if nothing below lands.

---

## Phase 4 — User Story 6: the service plugin boundary holds too (P2)

**Goal**: `fixpp::service` reaches the plugin header and the C ABI, and no engine header.
**Sequenced here, before US2**, because US2's red demonstration must revert *this* leg independently.

- [X] T020 [P] [US6] Add the service positive probe TU `tests/consumer/probe_service_positive.cpp` (`<fixpp/service/control_plane_factory.hpp>` **and** `<fix/c_api.h>` — FR-011a)
- [X] T021 [P] [US6] Add the ❌ engine-header probe source `tests/consumer/probe_service_negative.cpp` for the service target (FR-011b)
- [X] T022 [US6] Wire T020–T021 against `fixpp::service`; record the ❌ probe reporting reachable=TRUE against the unfixed package — the FR-007 red observation for the **service** leg
- [X] T023 [US6] `src/service/CMakeLists.txt`, **the `$<INSTALL_INTERFACE:…>` entry of `target_include_directories(fixpp_service …)`** (cited by CONSTRUCT — this is an instruction someone acts on, and 086's own comment block moved it from `:12` to `:26`) — replace `"$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"` with the `service-iface` root. **This line is not inherited from `fixpp_capi`** — narrowing that target does not touch it, and every other requirement can be satisfied while it survives (FR-011d)
- [X] T024 [US6] `CMakeLists.txt` — add `install(DIRECTORY "${CMAKE_SOURCE_DIR}/include/fixpp/service/" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/service-iface/fixpp/service")`
- [X] T025 [US6] Rebuild + re-install; confirm the service matrix row and that `fixpp::service` still reaches the C ABI **through its existing link to `fixpp::capi`** (no C-ABI root declared on it — R3)

---

## Phase 5 — User Story 3: C++ consumers are unaffected (P1)

- [X] T026 [P] [US3] Add a **separate** umbrella probe TU `tests/consumer/probe_umbrella.cpp` including `<fix/c_api.h>` **and** `<fixpp/service/control_plane_factory.hpp>` — `consumer_witness.cpp:34-37` includes neither, so FR-004's C-ABI leg, US3 scenario 2 and FR-011c are witnessed by nothing today
- [X] T027 [US3] Wire T026 against `fixpp::fixpp`. **Do not edit `consumer_witness.cpp`** — SC-003 trades on it remaining unchanged, which is exactly why the probe is a separate TU
- [X] T028 [US3] Confirm `consumer_witness` still configures, builds and runs with **zero** edits to its include paths, library paths or `find_package` call (SC-003)

---

## Phase 6 — User Story 2: the claim is held by a witness that can fail (P1)

**Goal**: each must-fail assertion is *demonstrated* red. Depends on US1 and US6 being green first.

- [X] T029 [US2] Write the demonstrated-red helper: install an `EXIT` trap **before** any edit, save `isolation.patch` plus copies of both CMake files, run each `ctest` under a controlled `set +e`, capture stdout/stderr and the **real** exit status to separate files, assert the status is non-zero, **capture and record the first diagnostic line** as an evidence artifact, restore, remove the trap. *(Wording matches `quickstart.md` §5, which requires capture — not a content assertion. If a content match is wanted, add it to §5 first so the two do not drift.)*
- [X] T030 [US2] **Demonstration A** — revert **the `PRIVATE fixpp_capi_objects` line of `target_link_libraries(fixpp_capi …)`** (by CONSTRUCT, not by line — `:46` is now a blank line, and following it literally would demonstrate nothing) alone; rebuild, re-install, confirm the C-ABI ❌ probes go TRUE and the witness reds; record commands + exit code + first diagnostic line to `$FIXPP_086_EVIDENCE/`
- [X] T031 [US2] **Demonstration B** — revert **the `$<INSTALL_INTERFACE:…/service-iface>` entry in `src/service/CMakeLists.txt`** (by CONSTRUCT; `:12` is now a comment) **alone**. FR-011e: the independence is *directional* — `fixpp_service` links `fixpp_capi` (`src/service/CMakeLists.txt:16`), so reverting the C-ABI leg reds **both** probes and cannot stand in for this one. Record `fixpp::capi`'s properties from *this* run to show it stayed isolated
- [X] T032 [US2] After the final restore, **rebuild and re-install** before any later step reads the export file — `cmake --install` copies a generate-time artifact and cannot regenerate it, so §§6/8/9 would otherwise run against demonstration B's reverted export *(Gate A r3 carry-forward #2, N2)*
- [X] T033 [US2] Assert both demonstrations in the record: red with isolation removed, green with it present, each with the command that produced it (SC-002)

---

## Phase 7 — User Story 4: the package still declares what it ships (P2)

- [X] T034 [P] [US4] `tests/packaging/run_package_contents_witness.cmake` — assert the C-ABI headers are present at **both** delivered paths, and the service header at both (FR-010). Prefix-normalise: DEB/RPM/TGZ carry a `usr/` component, the Windows ZIP does not — a `usr/`-anchored glob finds nothing there and reports "the package carries no C-ABI headers", a defect claim manufactured by the test
- [X] T035 [P] [US4] Add the **isolated-root containment** assertion — each new root contains only its declared subtree and no `<fixpp/...>` engine header (FR-010a / C-5 / I11). This is the **only** assertion tracing FR-001; the existing regexes at `:484-487` and `:508` are structurally blind to the new roots
- [X] T036 [US4] Prove T034/T035 can fail: remove an install rule locally, observe the witness red, restore (SC-005)
- [X] T037 [US4] Automate the additive-superset check — `comm -23 before.txt after.txt` must be **empty**, with a non-zero exit on any removal (SC-003a). Compare produced manifests, never install rules

---

## Phase 8 — User Story 5: the architecture stops claiming something untrue (P2)

**Ordered strictly after the measurement in T038** — §2a records that deriving export facts by *reading*
`target_link_libraries` was wrong in three places across a three-level cascade.

- [X] T038 [US5] **Re-measure** the export set from a real generate run: member count and the shipped `lib/objects-<CONFIG>/**` file count (FR-016 / SC-006). Predicted unchanged at 18/11 (R2) — predicted is not measured. Assert the **11** by its by-construction source (`src/capi/CMakeLists.txt:11-23`); leave the 18 as a measurement, since SC-006 says "whether or not it changed"
- [X] T039 [US5] Verify `find_package(fixpp)` succeeds for a consumer of **each** by-name target with no configure-time `FATAL_ERROR` from `_cmake_import_check_files_for_fixpp::capi_objects` (SC-008)
- [X] T040 [P] [US5] `.specify/architecture.md` §7.4:503 — rewrite against the measured result. The literal `INTERFACE_INCLUDE_DIRECTORIES = include/fix/` prescription **must not survive in any form**: it cannot be satisfied without breaking `<fix/c_api.h>` (FR-013)
- [X] T041 [P] [US5] `.specify/architecture.md` §7.4:504 — state the **service** target's delivered include interface; its current row dispositions only kind and name, which is how the second instance of the same gap went unrecorded (FR-013a)
- [X] T042 [P] [US5] `.specify/architecture.md` §8 — attribute each enforcement to the mechanism that actually performs it, for **both** boundaries (FR-014). Scope by **claim**, not by line label: the known sites (`:514`, `:515`, `:518`, `:537`, `:538`, `:543`, `:557`, `:560`, `:561`) are non-exhaustive evidence
- [X] T043 [P] [US5] Correct every statement about `tools/check_layers.py` — it is a **source `#include`-edge lint** over `src/**` and `bindings/**` (`:2-7`, `:173-176`); it reads no CMake target links and cannot see installed consumers (FR-014). Also `CMakeLists.txt:580` and `tests/consumer/CMakeLists.txt:68-69`
- [X] T044 [US5] `specs/084-packaging-cpack-export/contracts/package-layout.md` §2a — reconcile the C-ABI include-path reasoning and re-verify its citation set **as a set**. The drift is **not** a constant: `:45→:46`, `:43→:44`, `:47-48→:48-49`, `:70→:71` are +1 from an insertion at `:44`, but `:36→:37` is a separate error and `:11` is already correct (FR-015)

---

## Phase 9 — Polish & cross-cutting

### Gate A round-3 carry-forwards (nine one-line edits, none blocking)

- [X] T045 [P] Replace "today **exactly** `FIXPP_LOG_MIN_LEVEL`" with "today **at least** `FIXPP_LOG_MIN_LEVEL` and `ASIO_STANDALONE`; the complete set is enumerated per (a) and membership is decided by the predicate, not by this list" at `spec.md:409-410`, `research.md:288`, `contracts` §4:97-100 — `asio::asio` carries `ASIO_STANDALONE` and is linked *unwrapped* inside the C-ABI closure, so **two** definitions are withheld *(carry-forward #1, N1)*
- [X] T046 [P] Paste the two property maps into `research.md` R3 and `spec.md` FR-009a(i): OFF = `{INTERFACE_LINK_LIBRARIES}` (measured on the real artifact), ON = `{INTERFACE_INCLUDE_DIRECTORIES, INTERFACE_LINK_LIBRARIES}` (contract §2) *(carry-forward #3)*
- [X] T047 [P] `quickstart.md:207` — turn the property print into a **compare** against those two maps so an unexpected third changed property fails *(carry-forward #4)*
- [X] T048 [P] `spec.md` FR-009a(ii) — add `COMPILE_OPTIONS` and `COMPILE_FEATURES` to the same `file(GENERATE)` and the same driver compare (three lines, no new machinery). Measured on the real export: the live surface is `COMPILE_DEFINITIONS` only, but the requirement should not be narrower than its own claim *(carry-forward #5)*
- [X] T049 [P] `spec.md:369`, `contracts` §2a:82 and §4:187 — replace "taking its address suffices" with a non-elidable form (namespace-scope non-`static` non-`const` pointer, or a call; the TU is never executed so a call carries no runtime contract) *(carry-forward #6)*
- [X] T050 [P] `quickstart.md:434-435` — `git diff --quiet … || { echo "SC-007 FAIL: production source edited"; exit 1; }`; `--stat` exits 0 whether or not it prints, so the "MUST be empty" comment currently asserts nothing *(carry-forward #7)*
- [X] T051 [P] `plan.md:83` — scope the Article VII §4 cell to code-binding requirements and point FR-013…FR-017 / SC-004 / SC-006 at sequencing steps 5–7 and quickstart §7 *(carry-forward #8)*
- [X] T052 [P] Fold into `plan.md` step 4b: the `set -e` leg (a verifier pasting §§1–5 into one shell aborts on the *expected* non-zero **before** the restore) and the display-only `:295-297` check; add a `clang-tidy -p` invocation over the new probe TUs and soften the Article IX cell from "discharged" to "discharged **by** step 4a" *(carry-forwards #9, R3-3/R3-7)*
- [X] T053 Add `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` to the consumer sub-build configure so clang-tidy has a compile DB for the new probe TUs — it is configured at `run_consumer_witness.cmake:77-90` and bypasses the `_base` preset that sets it (`CMakePresets.json:12`)

### Verification

- [ ] T054 Full in-tree suite on the same host; result matches the T007 baseline — no test newly fails (SC-007), compared by **per-test status**, not by name set (a name-set diff cannot see a red)
- [X] T055 `git diff --quiet <baseline> -- '*.cpp' '*.hpp' '*.h' ':!tests/consumer/*'` — **no production source or public header edited** (SC-007). The C-ABI headers are self-contained, so isolation needs no source change; if one becomes necessary, that invalidates a stated assumption and must be raised, not absorbed
- [ ] T056 Close issue **#218** with the delivered disposition, explicitly recording that its Option 1 as written was **not implementable** and why (FR-017)

### Mandatory close-out (hard `/gate-b` preconditions — Article XVII §8)

- [ ] T057 **Catalogue close-out** — flip every feature-owned OFFICIAL `spec/feature-catalogue.md` row to `done` and add/update the matching `spec/coverage-index.md` entry
- [ ] T058 **Feature-completeness audit (FINAL TASK)** — tasks ↔ FR/SC ↔ catalogue rows all map to a landed test + implementation; record the 100%-or-waived verdict in `.specify/decisions/086-capi-include-isolation-verify.md` under `## Completeness`. `/gate-b` pre-flight 4d **HARD-BLOCKS** without this record

---

## Dependencies

```
Phase 1 (Setup) ──► Phase 2 (Foundational) ──┬─► Phase 3  US1  (P1) ─┐
                                              │                       ├─► Phase 6  US2  (P1, red demos)
                                              ├─► Phase 4  US6  (P2) ─┘
                                              └─► Phase 5  US3  (P1)
Phase 3 + Phase 4 ──► Phase 7 US4 (packaging) ──► T038 (measure) ──► Phase 8 US5 (docs)
All ──► Phase 9 (polish, verification, close-out)
```

**Hard orderings, each for a stated reason:**

- **Probe before edit**, within every story — Article VII §3 and FR-007 (T011–T015 before T016–T018; T020–T022 before T023–T024).
- **US1 and US6 before US2** — a red demonstration needs something to revert.
- **T032 (rebuild) before any later export read** — `cmake --install` copies a generate-time file.
- **T038 (measure) before T040–T044 (docs)** — reconciling documents against a *prediction* is the 084 §2a failure this feature exists to correct.
- **T057 before T058** — the audit checks the catalogue state.

## Parallel opportunities

- **T011–T014** — four independent probe TUs, different files.
- **T020/T021**, **T034/T035**, **T040–T043** — independent files within their phase.
- **T045–T052** — eight independent documentation edits.
- Not parallel: anything touching `tests/consumer/CMakeLists.txt` (T015, T022, T027) or `CMakeLists.txt`
  (T018, T024) — same file, and `ninja` takes no lock, so two builds against one directory can corrupt
  `.ninja_deps`.

## Implementation strategy

**MVP = Phase 1 + Phase 2 + Phase 3 (US1).** That alone closes issue #218's headline defect: a C-ABI consumer
can no longer reach `<fixpp/...>`, and the claim is held by a witness proven able to fail. Everything after is
genuine but incremental — US6 closes the second instance of the same claim, US3 proves nothing regressed, US4
stops the package silently dropping the headers, US5 stops the architecture asserting something untrue.

**Deliver in phase order and stop anywhere.** Each checkpoint is a coherent state: the isolation without the
service leg is still an improvement; the isolation without the doc reconciliation is a correct package with a
stale document (the status quo ante, minus the defect).

**Do not reorder the documentation phase earlier.** It is last because it must be written against a
measurement that does not exist until T038.
