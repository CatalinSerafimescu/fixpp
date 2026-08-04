# Feature Specification: System include directories bound at the installed-package consumer

**Feature Branch**: `087-system-include-binding`

**Created**: 2026-08-04

**Status**: Draft

**Input**: User description: "Bind INTERFACE_SYSTEM_INCLUDE_DIRECTORIES — the fourth usage requirement `$<LINK_ONLY:>` withholds — by asserting CMake File API codemodel compile groups (which expose each include entry with its isSystem classification, without parsing compiler-specific command lines) against an allowed system-root set for the installed-package consumer probe. Closes the C-3 scope limit recorded by 086 (issue #234)."

**Tracking issue**: [#234](https://github.com/CatalinSerafimescu/fixpp/issues/234)

**Inherits**: `specs/086-capi-include-isolation/contracts/include-interface.md` **C-3** — this feature closes the
scope limit that contract records against itself.

---

## Context — the gap is a *measurement* gap, not a missing comparison

086 narrowed `fixpp::capi`'s installed include interface with `$<LINK_ONLY:fixpp::capi_objects>`. That
generator expression withholds **four** usage requirements from consumers:

| withheld by `$<LINK_ONLY:>` | asserted today? | by what |
|---|---|---|
| `INTERFACE_COMPILE_DEFINITIONS` | **yes** | FR-009a(ii) generate-and-compare |
| `INTERFACE_COMPILE_OPTIONS` | **yes** | FR-009a(ii) generate-and-compare |
| `INTERFACE_COMPILE_FEATURES` | **yes** | FR-009a(ii) generate-and-compare |
| **`INTERFACE_SYSTEM_INCLUDE_DIRECTORIES`** | **NO** | — this feature |

### The distinction that defines the work

All three covered properties are compared **empty against empty** today
(`tests/consumer/CMakeLists.txt`: `FIXPP_086_EXPECTED_COMPILE_DEFINITIONS ""`, and the two siblings). That is
**not** vacuous, and the reason matters: the *observed* side is genuinely read off the configured consumer
target, so if a definition ever did propagate, `OBSERVED` would become non-empty and the compare would fail.
Empty-vs-empty is a real assertion when one side is measured.

The system-include case fails for a different reason. **There is no documented *collected*
`SYSTEM_INCLUDE_DIRECTORIES` target property for a consumer**, so the observed side cannot be read at all.
A fourth leg written the same way would report empty **by construction rather than by measurement** — an
assertion that cannot fail no matter what the package does. 086 excluded it deliberately for exactly this
reason (Gate B r1 P2 #6, narrowed again at r2 P2 #4), because a vacuous leg is *worse* than an absent one: it
looks like coverage.

**So this feature's obligation is not "add a fourth comparison". It is "obtain a genuinely measured observed
set for system include directories, then compare it against an expectation with a named origin."**

### What is uncovered today, concretely

086's §1 reachability matrix constrains system include directories **only at its two named boundaries** — a
propagated system path that makes `<fixpp/wire/parser.hpp>` or `<fixpp/service/control_plane_factory.hpp>`
reachable does red a ❌ probe. Everything else leaves every cell green:

- a system include directory **dropped** by the narrowing that a consumer needed, affecting any header other
  than those two;
- a directory **gained** — e.g. a third-party root leaking through the closure — making some *other* header
  reachable;
- a change in the **`SYSTEM` classification** of a directory that remains on the path, which changes warning
  suppression and header search ordering without changing reachability at all.

This is a completeness gap in the witness. No live defect is known; the point is that one would not be seen.

---

## User Scenarios & Testing *(mandatory)*

### User Story 1 - A C-ABI integrator does not silently inherit third-party system include roots (Priority: P1)

A consumer of the **installed** package links `fixpp::capi` and nothing else. The isolation promises they
reach the C-ABI headers and no C++ engine surface. That promise must extend to **system** include roots: the
consumer must not quietly acquire the engine's third-party include directories (asio, OpenTelemetry, etc.)
merely because they are `SYSTEM`-classified and therefore invisible to a reachability probe aimed at two
named headers.

**Why this priority**: it is the property the feature exists to bind, and the one whose absence 086 recorded
against itself. Without it the isolation's own contract clause (C-3) remains knowingly unenforced.

**Independent Test**: configure a standalone consumer against a staged install, link only `fixpp::capi`, and
read the effective include list for a real consumer target **with each entry's system/non-system
classification**. Compare against an expectation whose origin is declared in the tree.

**Acceptance Scenarios**:

1. **Given** a correctly isolated installed package, **When** the consumer probe is configured and its
   effective include list read, **Then** every entry is accounted for by the declared expectation and the
   gate passes.
2. **Given** the isolation is reverted so the objects target's include interface propagates again, **When**
   the same read is performed, **Then** the observed set contains roots the expectation does not allow and
   the gate **fails**, naming the offending entries.
3. **Given** an entry that is on the path in both states but whose **system classification** differs,
   **When** the read is performed, **Then** the mismatch is reported — classification is part of the compared
   value, not discarded.

---

### User Story 2 - The gate cannot pass by measuring nothing (Priority: P1)

The failure mode this feature exists to avoid is a gate that reports success because it observed nothing.
Whatever mechanism supplies the observed set must be proven to *produce data* before its emptiness is ever
interpreted as a pass.

**Why this priority**: equal-first with US1. A vacuous version of this gate is strictly worse than today's
honest omission, and 086's Gate B found five separate P1s of the form "a gate that cannot fail, or that
vanishes silently when removed". This story is the direct guard against repeating that.

**Independent Test**: delete or corrupt the mechanism's input and confirm the gate goes **red**, rather than
reporting an empty observation as success.

**Acceptance Scenarios**:

1. **Given** the mechanism's output is absent, **When** the gate runs, **Then** it fails with a diagnostic
   naming the missing input — it does **not** treat "nothing observed" as "nothing leaked".
2. **Given** the observed set is present but empty while a non-empty set was expected, **When** the gate
   runs, **Then** it fails.
3. **Given** the gate's carrier is deleted from the tree entirely, **When** the consumer witness runs,
   **Then** it fails by name, because the carrier is required explicitly rather than picked up implicitly.
4. **Given** a failure unrelated to system includes (a broken toolchain, a malformed input), **When** the
   gate runs, **Then** its diagnostic is distinguishable from a genuine violation.

---

### User Story 3 - The expectation has a declared origin, not one derived from the run (Priority: P2)

The allowed set must be written down in the tree with a stated rationale, so that a change to it is a visible,
reviewable edit rather than an automatic accommodation of whatever the run produced.

**Why this priority**: P2 because US1/US2 already make the gate real; this makes it *stay* real. A comparison
whose expected value is computed from the run it checks is satisfied by anything — the same no-op shape as a
generated file nothing reads (086 FR-009a(ii)'s stated rationale).

**Independent Test**: inspect the expectation's definition site; confirm it is a literal with a comment
explaining membership, and that no code path recomputes it from the observation.

**Acceptance Scenarios**:

1. **Given** the expectation, **When** it is read, **Then** its members are enumerated with a stated reason
   for each class of entry.
2. **Given** a new allowed root is required, **When** it is added, **Then** the change is a deliberate edit to
   the declared expectation, visible in review.

---

### Edge Cases

- **Toolchain-supplied roots differ per platform and per compiler version.** The expectation must tolerate
  compiler/SDK-owned directories without enumerating volatile absolute paths, or the gate becomes a
  maintenance burden that gets disabled — a failure mode as bad as absence. Resolved in Clarifications.
- **The mechanism may not be equally available on every platform.** If the observed set cannot be obtained on
  some platform, the gate must **fail loudly there or be explicitly scoped out with a recorded reason** — it
  must never silently degrade to passing. (This is the direct lesson of 086's post-sign-off round, where a
  mechanism measured only on Linux/clang proved unusable under Conan/MSVC.)
- **Generator differences.** Diagnostics and target-not-found phrasing differ per generator; anything matched
  textually must be verified against the generator actually in use rather than assumed.
- **An entry present but with a different classification** must not compare equal to its differently-classified
  twin.
- **Ordering** of include entries is significant to header search; the comparison must state whether it is
  order-sensitive and be consistent with that statement.

---

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST obtain, for a real consumer target that links only `fixpp::capi` from the
  **installed** package, the effective list of include directories used to compile it, **each with its
  system / non-system classification**.
- **FR-001a**: The system MUST do the same for a second, independent consumer target that links only
  `fixpp::service` from the installed package, with its own declared expectation. *(Clarified 2026-08-04 —
  the two legs are independently declared; 086 measured that narrowing `fixpp::capi` does not touch
  `src/service/CMakeLists.txt`'s own `$<INSTALL_INTERFACE:>`.)*
- **FR-002**: The observed set of FR-001 MUST be **measured** from the configured consumer, not defaulted,
  inferred, or derived from the expectation it is compared against.
- **FR-003**: The system MUST compare the observed set against an expectation **declared in the tree** with a
  stated rationale for membership.
- **FR-003a**: The comparison MUST assert **exact set equality**, not containment. An entry present in the
  observed set but absent from the expectation MUST fail (a leak), **and** an entry present in the expectation
  but absent from the observed set MUST fail (a drop). An entry appearing on both sides with a **different
  system/non-system classification** MUST fail. The expectation is therefore a **closed set**: if the
  observing mechanism reports compiler-owned roots, they are enumerated with a stated rule rather than
  tolerated by an open-ended allowance. *(Clarified 2026-08-04. A subset check cannot see an omission that is
  missing from both sides, and a drop is half of what C-3 claims.)*
- **FR-004**: A mismatch MUST fail the consumer witness with a diagnostic that names the offending entries and
  the direction of the mismatch (unexpected present / expected absent / classification differs).
- **FR-005**: The gate MUST fail when its input is missing or unreadable. Absence of an observation MUST NOT
  be reported as a pass.
- **FR-006**: The gate's carrier MUST be required **by name** by `tests/consumer/run_consumer_witness.cmake`,
  so that deleting or renaming it fails the witness rather than silently reducing coverage.
- **FR-007**: The gate MUST be **demonstrated red for its own cause**, with the isolation reverted, and green
  with it restored — both observations recorded as evidence.
- **FR-007a**: The **service** leg's red demonstration MUST be produced by reverting the service
  `$<INSTALL_INTERFACE:>` line **alone**, and MUST capture, from that same run, evidence that the C-ABI leg
  remained isolated. Reverting the C-ABI leg reds **both** probes — `fixpp_service` links `fixpp_capi` — so a
  service red obtained that way is not attributable to the service leg. *(Inherited from 086 FR-011e, which
  established this hazard by measurement.)*
- **FR-008**: A **counter-test** MUST demonstrate that an unrelated failure (e.g. a malformed input) is
  reported distinguishably from a genuine system-include violation.
- **FR-009**: The mechanism supplying FR-001 MUST be **measured on both Linux and MSVC-under-Conan** before
  being prescribed by any artifact. A mechanism verified on one toolchain MUST NOT be documented as the
  delivered design until the other is checked, or its platform scope MUST be stated explicitly.
- **FR-010**: If the mechanism is unavailable on a platform, the gate MUST fail there or be explicitly scoped
  out with a recorded, reviewable reason. Silent degradation to a pass is prohibited.
- **FR-010a**: The disposition for that case is **decided in advance** (clarified 2026-08-04): scope the gate
  to the platforms where the mechanism is measured to work, record the exclusion and its reason in the
  contract and in the ABI checklist, and on the excluded platform the gate MUST **fail loudly or be visibly
  absent**. An implementation MUST NOT choose, at implementation time, to let the gate no-op on a platform.
  If FR-009's measurement shows the mechanism works everywhere, this requirement is discharged by recording
  that result. *(Decided up front because 086 did not: a mechanism measured only on Linux/clang was written
  into a contract, cleared six Gate B rounds, and then failed on `windows-msvc-debug`.)*
- **FR-011**: 086's contract clause **C-3** and every other artifact that records its scope limit MUST be
  updated to reflect the property becoming bound. **The amendment set is defined once, as repository-relative
  paths with clause identifiers, in `contracts/system-include-interface.md` §4a** — **seven** artifacts,
  including the operational source comment at `src/capi/CMakeLists.txt:63-67`, the two consumer-harness scope
  records at `tests/consumer/CMakeLists.txt:205-218` and `tests/consumer/run_consumer_witness.cmake:171-180`
  *(added at Gate A round 2 — they are hits of §4a's own exhaustiveness grep that the table omitted)*, and a
  **provenance-preserving** append to 086's historical measurement record. This requirement does not restate
  the list; §4a is the authority, and a site discovered during the sweep is added there first.
- **FR-012**: The existing three-property comparison MUST continue to pass unchanged; this feature adds a leg
  and MUST NOT weaken, replace, or re-scope the existing one.
- **FR-013**: The consumer witness MUST NOT gain a new registered test; the assertion rides the existing
  `fixpp::consumer::install-witness` registration, consistent with 086. *(Assumption — see Assumptions.)*
- **FR-014**: CI MUST assert the **registration count** of the `consumer` label before running it, and fail
  the lane if it is not the expected number. `ctest` exits 0 when a label filter matches nothing, and the
  witness registers only under `FIXPP_BUILD_CODEGEN_TOOL`; without this assertion a lane on which that option
  goes OFF reports green having run the 087 gate zero times — the last vacuity path **a mechanised check can
  close**, and the one no demonstration in the gate itself can reach. (Contract §6 enumerates the residual
  paths that remain review-enforced by construction.) This obligation is **unqualified by tier**: it applies to
  **every** workflow that runs the witness — `tier1.yml`, `tier2.yml` and `tier3-libcxx.yml` — because
  `FIXPP_BUILD_CODEGEN_TOOL` defaults ON and is overridden nowhere, so the hazard is latent on every lane
  equally. Modelled on the assertion `tier1.yml` and `tier2.yml` already carry for the `packaging` label.
  Contract §6, scoped in §6a. *(Added at Gate A round 1; scope made explicit at round 2, where the plan was
  found to prescribe tier 1 alone.)*

### Key Entities

- **Observed include set** — the include directories used to compile the consumer probe, each carrying a path
  and a system/non-system classification. Measured per configuration.
- **Allowed expectation** — the declared set the observed side is compared against, defined in the tree with
  a rationale, including how toolchain-owned roots are treated.
- **Consumer probe target** — the real target whose compilation is measured. **Two of them**, one linking only
  `fixpp::capi` and one linking only `fixpp::service`, each from the installed package so the measurement
  reflects the shipped interface. They are separate targets with separate expectations because the two
  install interfaces are independently declared.

---

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: With the isolation intact, the consumer's observed include set is **exactly equal** to the
  declared expectation — zero unexpected entries, zero missing entries, zero classification mismatches.
- **SC-002**: With the isolation reverted, the gate fails, and the failure names at least one offending entry
  — observed, not predicted. Demonstrated **per leg**: once by reverting the C-ABI line, and once by reverting
  the service line **alone** with same-run evidence that the C-ABI leg stayed isolated (FR-007a).
- **SC-003**: The gate is observed red for **at least four distinct causes**: an **added** entry (isolation
  regression / leak), a **removed** entry (a root the consumer needed, dropped — reachable only because
  FR-003a asserts equality rather than containment), a missing/unreadable observation, and deletion of the
  carrier. Each red is recorded with its exit status and first diagnostic.
- **SC-004**: An unrelated failure is shown to produce a diagnostic distinguishable from a genuine violation
  (one recorded counter-test).
- **SC-005**: The full existing suite passes unchanged, and the three previously-covered properties still
  compare equal.
- **SC-006**: The observing mechanism is confirmed to produce a **non-empty** observation on **both** Linux
  and MSVC-under-Conan, recorded per platform, before any artifact prescribes it.
- **SC-007**: Every artifact that previously recorded C-3 as unbound is updated; no document still describes
  the property as an open scope limit. Discharged against the enumerated amendment set in
  `contracts/system-include-interface.md` §4a, which includes the historical research record (amended by
  **appending provenance**, never by rewriting) and the operational source comment — so the universal wording
  is not silently narrowed to "current normative artifacts".
- **SC-008**: A lane on which `fixpp::consumer::install-witness` fails to register **fails**, rather than
  reporting green on zero selected tests: the `consumer` label's registration count is asserted before the
  test step runs, on **every** workflow that runs the witness — tier 1, tier 2 and tier 3 (FR-014,
  contract §6/§6a).

---

## Assumptions

- **No new ctest registration.** The assertion extends the existing `fixpp::consumer::install-witness`, as
  086's probes do. This keeps the zero-selection hazard (CHK063, inherited from 084) from widening — and
  FR-014 **narrows** it for the `consumer` label, by asserting the registration count in CI before the run.
  Asserting a count is not a registration, so FR-013 is unaffected.
- **Scope is both installed consumer targets** — `fixpp::capi` **and** `fixpp::service` (clarified
  2026-08-04). No longer an assumption: 086 measured that the service leg is *not* implied by the capi leg,
  since its `$<INSTALL_INTERFACE:>` is independently declared. Each leg gets its own probe, expectation and
  red demonstration, and the service red must revert the service line alone (FR-007a).
- **No production C/C++ source changes.** Like 086, this is build-system and test wiring; the C-ABI surface
  does not move.
- **The staged-install consumer harness is reused.** `tests/consumer/` already solves configuring a standalone
  project against a staged install, which is the only way to observe an installed interface at all.
- **Order sensitivity defaults to order-insensitive** comparison unless clarification says otherwise, with
  ordering effects called out as a recorded limitation rather than silently ignored.

---

## Clarifications

### Session 2026-08-04

- Q: What must the system-include assertion bind — exact set equality, containment, or a deny-list? →
  A: **Exact set equality** over the observed entries (path **and** system/non-system classification). Any
  addition *or* removal fails.

  **Rationale carried into FR-003a**: exact equality is the only form that detects a **dropped** root, which
  is half of what C-3 claims. A containment/subset check is structurally blind to an omission that is missing
  from both sides, and a deny-list only catches leaks it was told to look for — a newly-introduced third-party
  dependency leaking through would pass silently. This also settles the toolchain-root question that was
  originally raised here: the expectation is a **closed set**, so compiler-owned roots (if they appear at all)
  must be handled by a **stated rule recorded with the expectation**, not by open-ended tolerance.

  **Measurement this creates a dependency on (owned by `/speckit-plan`)**: whether the observing mechanism
  reports the compiler's **built-in** search path at all, or only the directories the build system supplies.
  If only build-system-supplied entries appear, the expected set is small and stable and no toolchain rule is
  needed. If built-ins do appear, they are enumerated once per toolchain with the rule stated beside them.
  Either way the assertion form is unchanged — this decides the *contents* of the expectation, not its shape.

- Q: Does this feature bind `fixpp::service`'s system include interface as well as `fixpp::capi`'s? →
  A: **Both.** Two independent legs, each with its own probe, its own declared expectation, and its own
  demonstrated red.

  **Rationale carried into FR-001a/FR-007a**: 086 *measured* that the two legs are independent —
  `src/service/CMakeLists.txt` declares its own `$<INSTALL_INTERFACE:>`, so narrowing `fixpp::capi` does not
  touch it, and every other requirement could be satisfied while the service line still leaked. Binding only
  the C-ABI leg would recreate, one target over, exactly the asymmetry #234 exists to close.

  **Directional hazard inherited from 086 FR-011e** — the demonstrations must respect it: `fixpp_service`
  links `fixpp_capi`, so reverting the **capi** leg reds **both** probes. A service red taken by reverting
  capi therefore proves nothing about the service leg. The service demonstration MUST revert the service
  line **alone** and MUST capture evidence, from that same run, that the capi leg was still isolated.

- Q: If the observing mechanism proves unavailable or inconsistent on MSVC-under-Conan, is the accepted
  outcome to scope the gate with a recorded reason, or to block the feature? →
  A: **Scope it out with a recorded reason — and on the excluded platform the gate MUST fail loudly or be
  visibly absent, never silently pass.**

  **Rationale carried into FR-010a**: the decision is made **before** implementing, which is the specific
  thing 086 did not do. There, a mechanism measured only on Linux/clang was written into the contract, shipped
  through six Gate B rounds, and then failed on `windows-msvc-debug` — forcing a post-sign-off round. Deciding
  the disposition up front gives FR-009's measurement a defined consequence either way, instead of letting it
  become a late surprise.

  **The prohibited outcome is silent degradation, not partial coverage.** Partial coverage that is *recorded*
  is honest; a gate that quietly no-ops on a platform reports green while asserting nothing, which is the
  defect class this whole line of work exists to remove.

---

## Out of Scope

- Binding system include directories for targets other than the installed-package consumer probes — in-tree
  include behaviour is unchanged and out of scope, as it was for 086.
- Re-opening the three properties already covered by FR-009a(ii).
- Any change to the C-ABI header surface, symbol set, or version script.

---

## Normative References

*(Added at Gate A round 1. Its absence was a direct `[const §VI.5]` violation — the same one 085 recorded at
this gate — and made `checklists/requirements.md`'s "All mandatory sections completed" tick false; see that
file's Note.)*

Per `[const §VI.5]` (`.specify/constitution.md:164`), the exact entries that inform this spec. **This feature
has no FIX-normative content and introduces no OFFICIAL catalogue rows** — it asserts a build-system property
of the installed package and changes nothing about message semantics, encoding or validation, so no
`[DocAbbrev §X.Y.Z]` FIX section is engaged and `[const §VI.4]`'s coverage-index obligation is not triggered.
*(Verified: `grep -c "086\|087" spec/feature-catalogue.md` → **0**.)* The governing authorities are
constitutional, architectural and inherited, and they are listed here because §5 is a **presence** obligation:
the honest discharge is to record that the FIX set is empty and name what does govern — the form 086 used
(`specs/086-capi-include-isolation/spec.md:655-662`).

- **`[const §IV.2]`** (`.specify/constitution.md:141`) — the C ABI is the AGPL/commercial legal-isolation
  boundary. This is the article that makes an unenforced C-ABI include boundary a defect rather than a
  tidiness concern, and it is why the fourth withheld usage requirement is worth binding at all.
- **`[const §VI.5]`** (`.specify/constitution.md:164`) — the presence obligation this section discharges.
- **`[const §X.6]`** (`.specify/constitution.md:225`) — ABI-affecting features trigger all four mandatory
  controls (`/clarify`, `/analyze`, Codex Gate A, user `/plan` sign-off). Tracked in `plan.md`'s Constitution
  Check; the property asserted here *is* the C-ABI consumption boundary, so 086's ABI-adjacent disposition is
  matched.
- **`[const §XVI.3]`** — `/clarify` is mandatory and was run (3 questions, all resolved; see Clarifications).
- **`.specify/architecture.md` §7.4** (`:498`, "CMake target layout") — the exported-target layout whose
  installed interface this feature measures.
- **`specs/086-capi-include-isolation/contracts/include-interface.md` C-3** (`:122-149`) — the inherited
  scope limit this feature closes; the amendment set it belongs to is enumerated at
  `contracts/system-include-interface.md` §4a.

No FIX-normative section is cited because none applies.
