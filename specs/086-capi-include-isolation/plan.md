# Implementation Plan: C-ABI include isolation, delivered by the installed package

**Branch**: `086-capi-include-isolation` | **Date**: 2026-08-03 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/086-capi-include-isolation/spec.md`

**Tracking issue**: [#218](https://github.com/CatalinSerafimescu/fixpp/issues/218)

## Summary

`architecture.md` §7.4:503 claims `fixpp::capi` restricts its include path so C-ABI consumers cannot reach the
C++ headers, and §8 calls that a *structural* enforcement of the AGPL/commercial boundary. The installed
package does not deliver it: `fixpp::capi` carries no include directories of its own and inherits the whole
tree through `fixpp::capi_objects`. `fixpp::service` leaks the same claim independently, via its own
declaration at `src/service/CMakeLists.txt:12`.

**Approach — three additive installed include roots plus two target-graph edits.** `fixpp_capi` links its
objects `PRIVATE` (so CMake records `$<LINK_ONLY:>` and withholds the include directories while still linking
every object) and gains its own `$<INSTALL_INTERFACE:…/capi>`; `fixpp_service`'s whole-tree declaration is
replaced by `…/service-iface`. Two `install(DIRECTORY)` rules add the new roots. **Nothing moves** — the
existing header install rule is untouched, so every current include spelling and every current installed path
keeps working, including a bare `-I<prefix>/include` from a non-CMake consumer.

The issue's own proposed remedy — §7.4:503's literal `INTERFACE_INCLUDE_DIRECTORIES = include/fix/` — is
**rejected as unimplementable**: every C-ABI header is included *through* the `fix/` component, so that path
resolves to `include/fix/fix/c_api.h` and breaks every consumer. Evidence in `spec.md` → Context.

Every mechanism above is **measured** in `research.md`, not read off `target_link_libraries` — the method
`package-layout.md` §2a records as having been wrong in three places across a three-level cascade.

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

**Project Type**: C++ library — build-system and packaging change only. **Zero source files change.**

**Performance Goals**: N/A — no runtime code path is touched, so Article VIII §3 (bench in the same PR) is not
triggered.

**Constraints**: strictly additive installed layout (FR-005a); no consumer-visible include spelling change;
export-set membership must not move (18 members); `fixpp_capi_objects`' shipped object files are checked at
`find_package` time and must stay valid.

**Scale/Scope**: 3 CMake files edited, 2 install rules added, ~4 new witness targets, 3 documents reconciled.

## Constitution Check

*GATE: must pass before Phase 0. Re-checked after Phase 1 — see below.*

| Article | Requirement | Status |
|---|---|---|
| **IV §2** — the C ABI is the legal isolation boundary for AGPL/commercial dual licensing | This feature *delivers* the boundary the article assumes. Directly aligned. | ✅ |
| **X §1** — every change to the C ABI is reviewed against the contract; Gate A mandatory | Gate A is scheduled (pipeline step 4) and blocks `/speckit-tasks`. | ✅ planned |
| **X §6** — ABI-affecting features trigger **all four** mandatory controls | `/clarify` ✅ done · `/analyze` ⏳ step 6 · Gate A ⏳ step 4 · **user `/plan` sign-off ✅ GIVEN 2026-08-03** | ✅ |
| **X §2** — no C++ symbol leakage through the C ABI | Unaffected: no symbol, header content or version script changes. The existing `nm`/`dumpbin` gate keeps applying. | ✅ |
| **VII §3** — TDD mandatory, failing test first | **Binds task ordering.** Each isolation witness is written and observed **red** before the CMake change that makes it green. This is also FR-007's demonstrated-red obligation, so the two coincide. | ✅ ordering fixed |
| **VII §4** — no code without a test | Every requirement maps to an assertion in `contracts/include-interface.md` §4. | ✅ |
| **VII §8** — group isolation-safe tests, select by ctest label | New witnesses are standalone by necessity (each is a separate sub-project configure, exactly like `consumer_capi_witness`), which VII §8 explicitly admits for "exact-set completeness gates". Selected by label, never `-R <exe-name>`. | ✅ |
| **VIII §3** — no perf change without a benchmark in the same PR | Not triggered: no runtime code path changes. | ✅ n/a |
| **IX** — coverage / sanitizers / static analysis | No new runtime code, so no new coverage obligation. Sanitizer matrix unchanged. | ✅ n/a |
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
├── spec.md                          # what and why (36 FR, 8 SC)
├── plan.md                          # this file
├── research.md                      # Phase 0 — R1..R8, all MEASURED
├── data-model.md                    # Phase 1 — roots, targets, reachability (E1..E4, I1..I10)
├── contracts/
│   └── include-interface.md         # Phase 1 — the normative per-target reachability contract
├── quickstart.md                    # Phase 1 — end-to-end validation procedure
├── checklists/
│   └── requirements.md              # spec quality, 16/16
└── tasks.md                         # NOT created by /speckit-plan — step 5, after Gate A
```

### Source code (repository root)

```text
CMakeLists.txt                       # +2 install(DIRECTORY) rules near :446-451; :446-451 itself UNCHANGED
src/capi/CMakeLists.txt              # :46 PUBLIC -> PRIVATE; + target_include_directories(fixpp_capi …)
src/service/CMakeLists.txt           # :12 whole-tree INSTALL_INTERFACE -> service-iface root
tests/consumer/
├── CMakeLists.txt                   # + compile-only isolation probe targets
├── consumer_capi_witness.cpp        # EXISTS — links fixpp::capi, unchanged in intent (FR-009)
├── consumer_witness.cpp             # EXISTS — links the umbrella, unchanged (FR-004)
└── <new probe TUs>                  # compile-only: positive (13 C-ABI headers) + negative (must fail)
tests/packaging/
└── run_package_contents_witness.cmake   # + assertions for every delivered C-ABI / service path (FR-010)
.specify/architecture.md             # §7.4:503, §7.4:504, §8 reconciled (FR-013, FR-013a, FR-014)
specs/084-packaging-cpack-export/contracts/package-layout.md   # §2a reconciled; :45 -> :46 citation fix (FR-015)
```

**Structure Decision**: no new directory. The feature extends two existing witness tiers (`tests/consumer/`,
`tests/packaging/`) because both already solve the hard part — configuring a standalone project against a
staged install, which is the only way to observe an installed include interface at all. `src/` is untouched:
the C-ABI headers are already self-contained (zero `<fixpp/…>` includes, measured), so isolation needs no
source change.

## Implementation sequencing (input to `/speckit-tasks`)

Order is constrained by Article VII §3 and by FR-007's demonstrated-red obligation, which coincide here.

1. **Witnesses first, observed RED.** Add the compile-only probes against the *current* package. The negative
   probes must fail to fail — i.e. they compile, proving today's package leaks. That observation is FR-007
   evidence for `fixpp::capi` and, separately, for `fixpp::service`. One revert cannot stand in for the other.
2. **The `fixpp_capi` edit** — `PRIVATE` + its own `$<INSTALL_INTERFACE:…/capi>` + the install rule. Probes for
   `fixpp::capi` go green.
3. **The `fixpp_service` edit** — `src/service/CMakeLists.txt:12` + its install rule. Probes for
   `fixpp::service` go green. Kept a separate step because this line is **not** reachable from step 2: it is
   independently declared, and every other requirement can be satisfied while it survives (FR-011d).
4. **Packaging assertions** (FR-010), prefix-normalised for the Windows ZIP.
5. **Re-measure the export set** from a real generate run (FR-016). Predicted 18/18 unchanged; predicted is not
   measured.
6. **Document reconciliation** (FR-013, FR-013a, FR-014, FR-015) — written against the *measured* result of
   step 5, not against this plan's prediction.
7. **Close-out** — catalogue row, coverage index, issue #218 disposition (FR-017), and the parallel-worktree
   close-out items in `phases/phase-4/cleanup-phase.md`.

## Risks

| Risk | Why it is plausible | Mitigation |
|---|---|---|
| The real 18-member tree behaves unlike the 5-target repro | fixpp has two deliberate static-archive cycles and a Conan toolchain the fixture lacks | Step 5 re-measures on a real generate run before any doc is written (FR-016) |
| A witness passes for the wrong reason | Measured, not hypothetical: the research probe produced a false negative on its first attempt (R5) | Compile-only negatives; probe headers whose disappearance would itself be a defect (FR-008); paired evidence (FR-008a) |
| The content gate goes green on Windows while finding nothing | A `usr/`-anchored glob matches nothing in the ZIP and reads as "no C-ABI headers shipped" | Prefix-normalise before comparing; never anchor a glob on `usr/` (`package-layout.md` §2) |
| Docs reconciled against the prediction rather than the measurement | The 084 §2a note records this exact failure — derived-not-measured export members, wrong in three places | Step 6 is ordered strictly after step 5 |
| Verify runs against the wrong tree | `/speckit-verify` and `/gate-b` hardcode the main checkout, which holds `085` | Substitute the worktree path; symlink decision records (`phases/phase-4/parallel-worktrees.md` §4) |
| Build-tree disk exhaustion | `/mnt/wsl/fixppbuild` has ~23 GB free; a Debug tree measures 22–31 GB | Reclaim before the verify matrix, or run Release-only locally and take Debug from CI |

## Next

Pipeline step 4 — **`/gate-a 086-capi-include-isolation`**, which blocks `/speckit-tasks` (const §XVII.1).
Article X §6 additionally requires **explicit user sign-off on this plan** before implementation.
