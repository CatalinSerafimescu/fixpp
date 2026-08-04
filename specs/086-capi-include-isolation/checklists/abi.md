# Checklist: C-ABI consumption surface — requirements quality

**Purpose**: Unit tests for the *requirements* governing the C-ABI and service consumption interfaces.
Validates whether they are complete, unambiguous, consistent and measurable — **not** whether the
implementation works (that is `/speckit-verify` and Gate B's job).
**Created**: 2026-08-04 · **Audience**: Gate B reviewer · **Depth**: formal gate
**Feature**: [spec.md](../spec.md) · **Domain**: Article IV §2 / Article X — the legal isolation boundary

## Requirement Completeness

- [x] CHK001 - Is the complete set of headers that MUST be reachable through the C-ABI target enumerated by name, rather than described by a count? [Completeness, Spec §FR-002] — PASS: FR-002 enumerates by brace-expansion name (`<fix/c_api/{decimal,dict,engine,error,export,handles,log,message,otel,session,version}.h>`), and `contracts/include-interface.md` §1 lists all 11 sub-header names verbatim plus the entry header. Anchor spot-verified: `find include/fix -type f` returns exactly the 12 named files.
- [x] CHK002 - Are requirements defined for **every** cell of the reachability matrix, including each ❌ cell, or does any cell rely on being implied by another? [Completeness, Contracts §1] — PASS: contracts §1's table states MUST/MUST NOT explicitly for every (target × header-group) cell across `fixpp::capi`, `fixpp::service`, `fixpp::fixpp`; no cell is left to inference.
- [x] CHK003 - Is the boundary of the isolation obligation — which targets it binds and which it deliberately does not — stated as a requirement rather than left to inference? [Completeness, Spec §FR-003a] — PASS: FR-003a states the by-name/closure-only boundary as a MUST-scoped requirement with a measured predicate, not inference.
- [x] CHK004 - Are requirements defined for what the service target MUST reach, not only for what it must not? [Completeness, Spec §FR-011a] — PASS: FR-011a is a positive MUST-resolve requirement, paired with FR-011b's negative one.
- [x] CHK005 - Does the spec state a requirement for the C++ umbrella's continued reach over **both** header sets, or only over the C++ surface? [Completeness, Spec §FR-004, §FR-011c] — PASS: FR-004 covers the C-ABI leg, FR-011c the service leg, both witnessed by the new umbrella probe (contracts §4 row "reaches `<fix/c_api.h>` and `<fixpp/service/…>`").
- [x] CHK006 - Are requirements stated for usage requirements **other than** include directories that the narrowing mechanism also withholds? [Completeness, Spec §FR-009a] — PASS **with a recorded scope limit (Gate B r3/r4)**: FR-009a binds `COMPILE_DEFINITIONS`, `COMPILE_OPTIONS` and `COMPILE_FEATURES`; it does **NOT** bind `INTERFACE_SYSTEM_INCLUDE_DIRECTORIES`, which no collected consumer property exposes. The CMake File API assertion that would close it is an **OPEN FOLLOW-UP**, not delivered by 086. Original disposition: FR-009a states two separately-measured obligations (direct-property delta, effective usage-requirement delta) covering compile definitions/options/features/system-include-dirs, not just include paths. (The concrete exception-set wording is stale — see CHK012, DD-DECIDED — but the requirement's existence and scope are complete.)
- [x] CHK007 - Is there a requirement covering what happens to a consumer that links both isolated and non-isolated targets? [Coverage, Edge Case, Spec §Edge Cases] — PASS: spec.md Edge Cases "Both roots reachable at once" states the outcome explicitly, mirrored in contracts §5.

## Requirement Clarity

- [x] CHK008 - Is "reachable" defined precisely enough to be evaluated — specifically, is it defined over the **transitive** interface rather than a single target's property? [Clarity, Spec §FR-003] — PASS: FR-003's parenthetical explicitly rejects the direct-property-only reading ("Stated as reachability, not as a property of one target's `INTERFACE_INCLUDE_DIRECTORIES`"), and data-model I6 defines `reachable(target, header)` transitively/recursively over `INTERFACE_LINK_LIBRARIES`.
- [x] CHK009 - Is the distinction between "by-name" and "closure-only" export members defined with a test a reader can apply to a new target, rather than by enumeration alone? [Clarity, Spec §FR-003a] — PASS: FR-003a and contracts §1a both state the repeatable predicate ("no public header names it… and no public header instructs linking it"), applicable to any future target, not just enumerated by name.
- [x] CHK010 - Is "the isolated root" specified unambiguously enough that two implementers would produce the same installed path? [Clarity, Data-model §E1] — PASS: data-model E1 gives the exact destination path and literal `install(DIRECTORY …)` command for each new root.
- [x] CHK011 - Where a requirement names a specific source line as the thing to change, is the **claim** stated as well, so the requirement survives the line moving? [Clarity, Spec §FR-011d] — PASS: FR-011d quotes the actual line content (`"$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"`) alongside the line number. Anchor spot-verified: `src/service/CMakeLists.txt:26` reads exactly that text today.
- [x] CHK012 - Is the withheld-definition exception stated as a predicate rather than as a fixed list that can silently go stale? [Clarity, Ambiguity, Spec §FR-009a] — DD-DECIDED §Gate A carry-forward (T045): structurally the exception is a predicate ("definitions enumerated in (a) as presently unreachable" — re-derived from step (a), not hardcoded), but the concrete instance text currently reads "today **exactly** `FIXPP_LOG_MIN_LEVEL`" at `spec.md:409-410`, which the Gate A round-3 record already found stale (`asio::asio` carries `ASIO_STANDALONE` and is linked unwrapped in the closure, so a second definition is withheld). This is recorded as Gate A carry-forward T045 (`research.md:288`, `contracts` §4:97-100 also affected), owned by `/speckit-implement`, not re-opened here.

## Requirement Consistency

- [x] CHK013 - Do the reachability requirements in the spec and the normative matrix in the contract agree cell-for-cell, with no cell present in one and absent from the other? [Consistency, Spec §FR-003 vs Contracts §1] — PASS: US1/US6/US3 scenarios and FR-003/FR-011a/FR-011b cover the same cell set as contracts §1's matrix; no orphan cell either direction.
- [x] CHK014 - Is the additivity requirement stated so it cannot be read as discharging the target-graph obligation? [Consistency, Conflict, Spec §FR-005a vs §FR-005b] — PASS: FR-005b is an explicit anti-misread clause ("Additivity in FR-005a constrains the installed file layout ONLY. It does NOT constrain the target graph, and satisfying it is not sufficient"), naming the exact failure mode (green on FR-005a/FR-010/SC-003a while FR-003 still fails).
- [x] CHK015 - Are the two isolation legs' independence claims consistent in **both** directions, given that one target links the other? [Consistency, Spec §FR-011d vs §FR-011e] — PASS: FR-011e states the asymmetry precisely (forward-independent, backward-not) and prescribes how the red demonstrations must respect it. Anchor spot-verified: `src/service/CMakeLists.txt:30` does link `fixpp_capi`, grounding the claimed asymmetry in real code.
- [x] CHK016 - Do the spec, data-model and contract describe the same set of installed roots with the same contents? [Consistency, Spec §Clarifications, Data-model §E1, Contracts §2] — PASS: all three name the same 3 roots (`include`, `include/capi`, `include/service-iface`) with identical contents and identical install commands.
- [x] CHK017 - Does any surviving statement describe a ❌ assertion as a build target, contradicting the mechanism requirement? [Consistency, Conflict, Spec §FR-006a] — PASS: no surviving contradiction found. FR-006a, contracts §4a, research R5/R9, plan.md's "❌ cells cannot be build targets" box, and tasks.md's "Two traps" banner are all consistent post-Gate-A-r1 correction — every current statement of the mechanism uses configure-time `try_compile`, never a build target. Realizability checked: the real `tests/consumer/CMakeLists.txt` calls `find_package(fixpp REQUIRED)` at line 55 before any probe would be added, so `fixpp::capi` is a defined imported target by the point a `try_compile(... LINK_LIBRARIES fixpp::capi)` would fire — matching R9's own fixture method (which also used `find_package`, per research.md's Method section), not R10's `include(fixppTargets.cmake)` variant (R10 §4a flags that gap for itself only). No unrealizable mechanism found.
- [x] CHK018 - Can the "0 C++ engine headers reachable" criterion be objectively evaluated given the evidence the design actually produces, or does it claim more than its named evidence establishes? [Measurability, Spec §SC-001] — PASS: SC-001 explicitly declines the stronger, unevidenced claim ("a '0 over all nine header families' claim asserted by two probes would be a universal claim on representative evidence") and scopes itself to exactly what is measured (two named negative probes + C-5 root containment).
- [x] CHK019 - Is the demonstrated-red obligation expressed so that a reviewer can tell whether it was met, including **what** must be recorded? [Measurability, Spec §FR-007, §SC-002] — PASS: SC-002 names the exact record content (command, exit code, first diagnostic line) and its location (`.specify/decisions/086-capi-include-isolation-verify.md`).
- [x] CHK020 - Is the requirement that a positive assertion cannot establish a negative one stated as a binding rule on evidence, not merely as an explanatory note? [Measurability, Spec §FR-008a] — PASS: FR-008a uses MUST/MUST NOT language ("MUST NOT be cited as if it could… MUST be the pair"), not merely explanatory prose.
- [x] CHK021 - Does the spec require the must-fail probe to fail *for the isolation reason*, with a stated way to distinguish that from any other failure? [Measurability, Spec §FR-008] — PASS: FR-008's MUST clause is operationalized by contracts §4a/research R5 (compile-only, no link stage, avoiding the measured link-failure confound) — a stated, not merely asserted, distinguishing mechanism. (Checklist purpose is requirement quality, not implementation verification; the requirement text plus its named operationalization satisfy this.)
- [x] CHK022 - Are the success criteria for the service leg as strong as those for the C-ABI leg, or weaker without a stated reason? [Consistency, Spec §SC-001a] — PASS: SC-001a states "same evidentiary basis as SC-001… and the same paired-evidence rule" — explicitly symmetric.

## Scenario & Edge Case Coverage

- [x] CHK023 - Are requirements defined for a C consumer as distinct from a C++ consumer, given the user story promises both? [Coverage, Spec §US1] — PASS: US1 Acceptance Scenario 3 and FR-002 both require C-translation-unit resolution distinct from C++; contracts §4 carries it as a separate table row.
- [x] CHK024 - Is the case of the same header being reachable from two installed roots addressed in requirements? [Edge Case, Spec §Edge Cases] — PASS: Edge Cases "The same header at two installed paths" plus data-model I2/I3 and FR-008a's rationale all address it directly.
- [x] CHK025 - Are requirements stated for a consumer that bypasses CMake entirely (bare include-path flag)? [Coverage, Spec §FR-005a] — PASS: FR-005a states the bare-include-path-flag case explicitly; Edge Cases "A consumer that never uses CMake" restates it.
- [x] CHK026 - Is there a requirement covering the case where a future dependency contributes a usage requirement the current exception predicate does not admit? [Gap, Coverage] — PASS: FR-009a(ii)'s closing sentence — "Anything else that goes missing MUST be republished on `fixpp_capi` directly" — binds any future withheld requirement not already in the pre-approved enumerated exception; the closed-enumeration comparison (FR-009a(i)/(ii)) would fail on an unaccounted-for future definition rather than silently admitting it.

## Dependencies & Assumptions

- [x] CHK027 - Is the assumption that the C-ABI headers are self-contained recorded, with a stated consequence if it becomes false? [Assumption, Spec §Assumptions] — PASS: Assumptions states it and the consequence ("Any need to edit a header to achieve it would invalidate this assumption and should be raised rather than absorbed"). Anchor spot-verified: `grep -rn "#include" include/fix/` shows zero `<fixpp/…>` includes anywhere under `include/fix/`.
- [x] CHK028 - Is the assumption that the export closure does not move recorded as an assumption requiring measurement, rather than as a fact? [Assumption, Spec §FR-016] — PASS: Assumptions + FR-016 + data-model I5 all frame the 18-member figure as "predicted, must be re-measured," never as settled fact.
- [x] CHK029 - Is the dependency on the merged predecessor feature's export machinery documented? [Dependency, Spec §Dependencies] — PASS: Dependencies section names 084-packaging-cpack-export (PR #219) and what it supplies.
- [x] CHK030 - Is the C-ABI surface freeze status cited from a resolvable location, given the cited file lives in a different repository? [Traceability, Assumption] — PASS: the "Cross-repository citations" note gives the resolvable absolute root. Anchor spot-verified: `[parent-repo]/REMAINING-WORK.md:7` (`/home/catalin/Work/Programming/Antreprenoriat/research/G19-fix-fpml-iso20022/REMAINING-WORK.md`, line 7) does state "the C ABI surface is DONE… GA-frozen at `1.5.0`… additive-only changes bump MINOR" — matches the spec's citation exactly.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 29 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 1 |
| WAIVED | 0 |
| **Total** | 30 |

### SPEC-FIXED items

None.

### DD-DECIDED items

- CHK012 — Gate A round-3 carry-forward, T045 (`.specify/decisions/086-capi-include-isolation-gatea.md` → "Carry-forward into `/speckit-implement`"); rationale: `spec.md:409-410`'s "today exactly `FIXPP_LOG_MIN_LEVEL`" is a known-stale instance of an otherwise-predicate exception (`ASIO_STANDALONE` is also withheld) — already scheduled as a one-line fix at `/speckit-implement`, not re-opened here.

### WAIVED items

None.

Anchors spot-verified: `src/service/CMakeLists.txt:26,16`; `src/capi/CMakeLists.txt:94-96`; `include/fix/**` (zero `<fixpp/…>` includes, 12-file census); `tests/consumer/CMakeLists.txt:59`; `[parent-repo]/REMAINING-WORK.md:7` — all resolve and say what the bundle claims, against the signed-off Gate A revision (`086-capi-include-isolation-gatea.md`, round 3, user-signed-off 2026-08-03).
