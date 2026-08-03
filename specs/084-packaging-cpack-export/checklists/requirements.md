# Specification Quality Checklist: Installable Packaging (CPack) + CMake Package-Config Export

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-07-31
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs)
- [x] Focused on user value and business needs
- [x] Written for non-technical stakeholders
- [x] All mandatory sections completed

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain
- [x] Requirements are testable and unambiguous
- [x] Success criteria are measurable
- [x] Success criteria are technology-agnostic (no implementation details)
- [x] All acceptance scenarios are defined
- [x] Edge cases are identified
- [x] Scope is clearly bounded
- [x] Dependencies and assumptions identified

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria
- [x] User scenarios cover primary flows
- [x] Feature meets measurable outcomes defined in Success Criteria
- [x] No implementation details leak into specification

## Notes

- Items marked incomplete require spec updates before `/speckit-clarify` or `/speckit-plan`.

### Validation record

Validated in a single pass — the spec was authored against these criteria rather than drafted and then corrected, so there is no multi-iteration history to report. Three points where the criteria actively shaped the wording:

1. *No implementation details* — the feature description that seeded this spec named concrete CMake mechanisms (`install(EXPORT)`, `CMakePackageConfigHelpers`, `$<INSTALL_INTERFACE:>`). These were deliberately **not** carried into FR-001..FR-010, which state capabilities instead; selecting the mechanism is `/speckit-plan`'s job. Package *formats* (DEB/RPM/TGZ/ZIP) **are** retained — they are the user-visible deliverable and an explicit user decision, not an implementation choice.
2. *Success criteria technology-agnostic* — SC-005 states the outcome ("Debug packages yield usable symbolication … each by its own platform-appropriate check") rather than naming symbol-file formats or inspection tools, even though the underlying platform asymmetry is real and is captured as a requirement in FR-019.
3. *Requirements testable* — "the denylist must stay coherent with the export set" is unfalsifiable as prose, so FR-009 requires a **machine-checkable** statement of it, and SC-007b requires that assertion be **proven to fail on a deliberately broken input** before it counts as a gate. This follows the project's standing rule that a gate never observed failing proves nothing. *(Gate A round 1: the original SC-007 bundled two legs with incompatible mechanisms and was split — SC-007a's export-closure leg is enforced at CMake **generate** time, so it cannot be demonstrated by a ctest that would need a build system a broken tree never produces.)*

**Deliberate deviations from a strict reading of the checklist:**

- *"Written for non-technical stakeholders"* is satisfied only in the sense available to a build-and-distribution feature. Terms like "static library", "export set", and "debug information" are the domain vocabulary of the actual stakeholder (an integrator or release engineer). Replacing them would obscure rather than clarify.
- The **Context: verified starting state** table cites `file:line` evidence. This is intentional and is not "implementation detail leaking into spec": the anchor doc contains three verified-stale claims (FR-024), so this spec pins what was actually checked, on what date, against what source. Removing the citations would reintroduce exactly the drift this feature exists to correct.

### Carried constraints (do not relitigate downstream)

- **FR-007** (builder/validator libraries stay unexported) is settled by 078 Gate B P1.
- **FR-010** is a *verification* obligation with an escalation path, not a decision to be made in this feature.
- The **explicit non-goal** on shared variants of the core C++ targets rests on those targets having **no ABI-freeze mechanism** — no version script, no header-hash baseline, no symbol golden — unlike the C ABI, which has all three. *(Corrected at Gate A round 1: this previously read "an ABI decision owned by REMAINING-WORK A-1". That freeze is **CLOSED**, GA-frozen at `1.5.0` — `REMAINING-WORK.md:7` — and it governs the C ABI, not these C++ targets. The non-goal stands; its basis changed.)*

### Open items blocking `/speckit-plan`

None. The three questions raised at specification time were resolved in the 2026-07-31 clarification session (Debug publication → CI artifacts only; GA version → keep `0.0.1`, defer to item 13; package licensing → ship dictionaries with full attribution). Each answer is recorded in the spec's **Clarifications** section and applied to Assumptions 2, 3, and 10 respectively.

### Re-validation after clarification (2026-07-31)

All 16 items remain passing; no regressions. Three notes on items the new content touches:

- *No implementation details* — still passing. FR-018b names a `NOTICE` file, which is a legally-required deliverable artifact with a conventional name, not an implementation choice. FR-018a/c/d likewise state obligations rather than mechanisms.
- *Requirements testable* — strengthened. The attribution requirements pair with SC-013/SC-014, and FR-018d requires verification by **enumerating installed package contents** rather than by reading install rules, because an install rule that silently matches nothing yields a legally deficient package that looks correct in the build system.
- *Dependencies and assumptions identified* — strengthened. Assumption 10 states explicitly that this feature discharges the mechanical attribution only and does **not** close REMAINING-WORK item 15d, whose counsel review remains open. That boundary is recorded so downstream review does not mistake shipped attribution for legal clearance.

### Re-validation after Gate A round 1 (2026-07-31)

All 16 items remain passing. Four notes on what the round-1 rewrite changed and why no box flips:

- *No [NEEDS CLARIFICATION] markers remain* — still passing. The round-1 rewrite added a `### Session 2026-07-31 (Gate A round 1)` clarification block, but each entry is an **answered** question with its evidence, not an open marker. The two items that remain genuinely open — D1 and the FR-018e provisioning choice — are recorded as routed decisions with option tables, which is where an undecided design question belongs.
- *Requirements are testable and unambiguous* — strengthened, and one weakness removed. FR-010c no longer carries a hand-written dependency list (which was wrong in both directions) but a **derivation rule**; FR-018b now pins its "verbatim" reference to `dictionaries/QUICKFIX_LICENSE.txt:19-20`, without which SC-013 was unfalsifiable; FR-009 asserts the exclusion set as **set equality** over all seven patterns rather than as a subset.
- *Success criteria are measurable* — strengthened. SC-012's primary form was impossible by construction and its fallback unspecified; the equivalent check is now named. SC-002 was structurally incapable of failing on the defect it claimed to cover; it now claims only what it can prove. SC-016 is new, and covers a defect class no existing criterion could observe.
- *Dependencies and assumptions identified* — Assumptions 1, 3, 5, 7 and 11 each had a factual or scope error corrected. Assumption 11's correction is the load-bearing one: the feature's touched-file set now includes `src/*/CMakeLists.txt`, which moves the Article IX disposition from `N/A` to unresolved.

**One item was deliberately NOT closed at round 1**: `research.md` R2's export-set membership was marked provisional pending an executed `install(TARGETS … EXPORT …)` generate run. That was a blocker on `/speckit-tasks`, not on this checklist — a documentation pass could not produce the evidence, and writing a fresh membership list without it would have repeated the exact "reading presented as a verification" defect the review found.

### Re-validation after Gate A round 2 (2026-07-31)

All 16 items remain passing; no box flips and no requirement was added, so the spec's **41 FR, 18 SC, 12 assumptions** count is unchanged.

**✅ The round-1 carry-forward is CLOSED.** The generate run was executed by the orchestrator between rounds (`research/reviews/orchestrator_084-packaging-cpack-export_gate_a_r1_measurements.md`) and R2 now carries a **measured** eleven-member export set instead of a candidate table. Round 1's refusal to write a membership list from reading is vindicated rather than merely defensible: the reading missed **three** members across a three-level cascade and was blind to both generate blockers.

Three notes on what round 2 changed and why no box flips:

- *Success criteria are measurable* — strengthened, and one criterion repaired. **SC-009a** asserted only that each `include/<subtree>` row was **non-empty**, which `OPEN` satisfies — so it passed over a table in which seven rows were undecided, a gate that could not fail on the defect it exists to catch. It now requires `disposition ∈ {export, exclude}`, and an `OPEN` row is a failure. **SC-016** had no falsifiable state under FR-018e option (c) ("expected to fail" cannot distinguish the expected failure from any other) and now names the specific expected failure and the specific expected successes.
- *Requirements testable and unambiguous* — **Assumption 8** was the last site still saying the gcc Debug configuration reuses the release Conan profile with an override; it now matches research R5, `plan.md`, and the executed measurement (a tracked `conan/profiles/linux-gcc-debug`).
- *Dependencies and assumptions identified* — strengthened. Absorbing the measurement makes `fixpp_otel` a **mandatory** export-set member, which makes `opentelemetry-cpp` (14 Conan packages) a required consumer-configure dependency of **every** artifact this feature ships — previously framed as conditional in six places. The bundle also now states, rather than assumes, *why* SC-015 still holds: the OTel-OFF stub is an empty INTERFACE library with no link edges.

**Still deliberately NOT closed here** — and correctly so: **D1** and the **FR-012a class** dispositions, and the **FR-018e** provisioning choice. These are routed decisions owed by Gate A, recorded with option tables. A checklist pass that filled them in would pre-decide them.

### Re-validation after Gate A sign-off (2026-08-01)

All 16 items remain passing. **One requirement was added** — FR-026a, carrying decision D3 — so the count moves to **42 FR, 18 SC, 12 assumptions**. No SC was added; SC-016 and FR-018e were rewritten in place, and no FR or SC was renumbered (the suffixed convention stands, per the round-1 disagreement record).

**✅ All three routed decisions are CLOSED**, by the user rather than by a rewriter, which is what round 1 routed them here for:

- **D1 / the FR-012a class → `export`.** Every shipped header gets a library behind it: `fixpp_capi` (static), `fixpp_config_toml`, `fixpp_tap`, `fixpp_service`. One deliberate `exclude` — the two test-support subtrees — recorded as a change in delivered content.
- **FR-018e → the package is provider-agnostic by construction.** The vendor / distro-`Depends:` / Conan-only table is **withdrawn** as a mis-framing, not chosen among.
- **D3 → gate the real-client witness on `linux-gcc-release` only**, recorded as FR-026a.

Four notes on why no box flips:

- *Requirements are testable and unambiguous* — **strengthened**. FR-018e replaced a three-way choice with four checkable obligations, each with a named check; FR-012a's parenthetical now records dispositions rather than routing; FR-013 gained the test-support-header clause that "test **executables**" never reached.
- *Success criteria are measurable* — **strengthened, and one criterion gained a pass state**. **SC-016** was previously satisfiable by a recorded *failure* under one branch of a decision; it now asserts that `find_package(fixpp)` **succeeds** against dependencies the producer's package manager did not supply, with an explicitly demonstrated **red** leg. **SC-009a** is now satisfiable (no row reads `OPEN`) and remains live for subtrees added later.
- *No [NEEDS CLARIFICATION] markers remain* — still passing, and now with **nothing routed**. The spec's D1 Open Question is marked RESOLVED with its answer; a new `Session 2026-08-01 (Gate A sign-off)` clarification block records all three decisions.
- *Dependencies and assumptions identified* — **strengthened**. The `find_dependency` set is now six (tomlplusplus became unconditional), each carries a tested-against version **read from `conanfile.py` and cited** rather than transcribed, and each is classified ABI-stable / no-ABI-surface / ABI-fragile.

**One item deliberately carried, not closed** — and it is a *new* one: the six export members the D1/FR-012a sign-off adds are **derived by reading, not measured**. That is a blocker on nothing here, but it is a standing implementation obligation (`contracts/export-set.md` §2a): re-run the `install(TARGETS … EXPORT …)` + generate experiment once they are wired. Recording it as an obligation rather than treating the reading as settled is the same judgement round 1 made, and the measurement vindicated.
