# Feature Specification: C-ABI include isolation, delivered by the installed package

**Feature Branch**: `086-capi-include-isolation`

**Created**: 2026-08-03

**Status**: Draft

**Input**: User description: "Deliver the C-ABI include isolation that architecture.md §7.4:503 claims but the installed package does not provide (issue #218), and add the compile-fails consumer witness that holds the claim honest."

**Tracking issue**: [#218](https://github.com/CatalinSerafimescu/fixpp/issues/218)

---

## Context — what is broken, and what the issue got wrong

`.specify/architecture.md` §7.4:503 states that `fixpp::capi` restricts its interface include path so C-ABI
consumers **cannot** reach the C++ headers, and §8 leans on that property as a *structural* enforcement of the
service-mode boundary. The installed package does not deliver it. Measured in the shipped `fixppTargets.cmake`:
`fixpp::capi` carries **no** `INTERFACE_INCLUDE_DIRECTORIES` of its own, and its only link dependency
(`fixpp::capi_objects`) exposes `${_IMPORT_PREFIX}/include` — the whole tree. A C-ABI consumer reaches
`<fixpp/...>` transitively.

> ### ⚠️ The remedy issue #218 proposes is NOT implementable as written
>
> §7.4:503 prescribes `INTERFACE_INCLUDE_DIRECTORIES = include/fix/`, and issue #218 inherits that wording.
> Every C-ABI header is included as `<fix/c_api.h>` / `<fix/c_api/error.h>` — the entry header at
> `tests/consumer/consumer_capi_witness.cpp:26`, and the twelve sub-headers referencing each other the same way
> (`include/fix/c_api/*.h`). An interface include path of `<prefix>/include/fix` makes those resolve to
> `<prefix>/include/fix/fix/c_api.h` and **breaks every C-ABI consumer**. The clause is self-inconsistent with
> the project's own include convention and cannot be satisfied literally.
>
> **This feature delivers the isolation by *additional* installed include roots instead** (user decisions,
> 2026-08-03 — see Clarifications), which preserves every existing include spelling and every existing
> installed path verbatim while making the restriction real for the targets consumers are told to link.

### What was measured before writing this spec (2026-08-03, `main` @ `24595e11`)

| Fact | Evidence | Consequence for scope |
|---|---|---|
| The C-ABI headers are **self-contained** | `include/fix/**` includes only `<fix/c_api/...>` and C stdlib headers (`stdint.h`, `stddef.h`, `stdbool.h`). **Zero** `<fixpp/...>` includes | Isolation is achievable — no header has to be rewritten to make the boundary hold |
| **In-tree blast radius is zero** | `CMakeLists.txt:234` — `include_directories("${CMAKE_SOURCE_DIR}/include")`, directory-scoped over the whole build. The 6 of 28 `tests/capi/*.cpp` that use `<fixpp/...>` receive it from there, **not** through the `fixpp_capi` target | The gap is installed-package-only, exactly as #218 states. Narrowing the target's interface cannot break an in-tree build |
| `fixpp_capi_objects` is **load-bearing in the export closure** | `contracts/package-layout.md` §2a — it is an export member *because* `fixpp_capi` is source-less and links it `PUBLIC` (`src/capi/CMakeLists.txt:46`); the shipped `lib/objects-<CONFIG>/` files are checked by `_cmake_import_check_files_for_fixpp::capi_objects` and removing them makes `find_package(fixpp)` **`FATAL_ERROR`** for every consumer | Any change to that link keyword is a **measured** obligation, not a read-off-the-source one. Export-set membership and the shipped object files must be re-verified on a real configure + install |
| `fixpp_service` leaks the same claim **independently** | `src/service/CMakeLists.txt:10-13` declares its own `$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>` — the whole tree — and is an export member. It does not inherit from `fixpp_capi` | §8's boundary claim is unenforced there too. **In scope** (clarified 2026-08-03) — US6 / FR-011 |
| The service public surface is **one self-contained header** | `include/fixpp/service/control_plane_factory.hpp` — a pure abstract base, **zero** `#include` directives. `include/fixpp/service/` holds nothing else but a `.gitkeep`. No `fixppd` target exists in any `CMakeLists.txt` yet (Phase-3 stub) | Isolating it is cheap, but the header must **also** stay reachable from the C++ umbrella: `EngineConfig` holds a `unique_ptr<ControlPlaneFactory>`, so the value type needs the base complete |
| The package-contents witness has **no** C-ABI header assertion | `tests/packaging/run_package_contents_witness.cmake` asserts `include/fixpp/wire/parser.hpp` (:371) and `include/fixpp/v44/Messages.hpp` (:374); nothing asserts any `include/fix/**` path | Relocating the C-ABI headers would today be invisible to the packaging gate — the headers could vanish entirely and it would stay green |
| `fixpp_capi_shared` is a **second** propagation path | `src/capi/CMakeLists.txt:50` links the same OBJECT library `PUBLIC` | Test-only (`FIXPP_BUILD_TESTS`), but not zero — it must be considered when the link keyword changes |

---

## Clarifications

### Session 2026-08-03

- Q: Non-CMake consumers — relocating the C-ABI headers breaks a bare `-I<prefix>/include` +
  `#include <fix/c_api.h>`. How should the installed layout handle it? → A: **Install at BOTH roots.**
  `include/fix/` stays exactly where it is *and* a copy is installed under `include/capi/fix/`. The change is
  purely additive: no existing consumer breaks, and the umbrella's install rule needs no exclusion. Isolation is
  a **target-interface** property, so what lives at another root does not weaken it — `fixpp::capi` sees only
  the isolated root. Both copies come from one source directory, so they cannot drift.
- Q: `fixpp::service` leaks the same §8 boundary claim independently — in scope for 086? → A: **In scope.**
  A third installed root, `include/service-iface/`, carries the service plugin interface; the one header
  involved ships at two paths for the same reason the C-ABI headers do (the C++ umbrella must keep reaching it,
  because `EngineConfig` holds a `unique_ptr<ControlPlaneFactory>`). §8's boundary claim is delivered whole
  rather than leaving a second known hole open.
- Q: How strict is the isolation boundary the witness must hold? → A: **By-name targets only.** Isolation is
  asserted for the targets a consumer is *instructed* to link (`fixpp::capi`, `fixpp::service`).
  `fixpp::capi_objects` keeps its whole-tree interface: it is a **closure-only** export member by the project's
  own existing taxonomy (`CMakeLists.txt:575-584` — no public header names it and nothing instructs anyone to
  link it), the same class as `fixpp::log_otlp`. Narrowing it would cascade into the in-tree graph and into the
  export-closure coupling that makes `find_package` `FATAL_ERROR`.

**Resulting installed layout** (three roots, additive — nothing moves):

| Root | Contents | Reached by |
|---|---|---|
| `<prefix>/include` | `fix/` + `fixpp/` — unchanged, the whole tree | `fixpp::fixpp` (C++ umbrella) |
| `<prefix>/include/capi` | `fix/` only | `fixpp::capi` (C-ABI consumer target) |
| `<prefix>/include/service-iface` | `fixpp/service/` only | `fixpp::service` (+ the C-ABI root) |

---

## User Scenarios & Testing *(mandatory)*

### User Story 1 - A C-ABI consumer cannot reach the C++ surface (Priority: P1)

An integrator writes a C or C++ program against the C ABI, installs the fixpp package, and links only the
C-ABI consumer target. They should be able to use the whole C ABI, and they should be **unable** to reach the
C++ engine headers — not by convention or documentation, but because the headers are not on their include path.
Today they can reach them, so a C-ABI consumer can drift across the AGPL/commercial legal-isolation boundary
(Article IV §2) without anything objecting.

**Why this priority**: This is the defect. Everything else in this feature exists to deliver or to hold it.

**Independent Test**: Install the package to a staging prefix; configure a standalone consumer project that
does `find_package(fixpp)` and links only the C-ABI consumer target. Compiling a translation unit that includes
the C-ABI entry header succeeds; compiling one that includes a C++ engine header fails at preprocessing.

**Acceptance Scenarios**:

1. **Given** a staged install of the package, **When** a standalone consumer links only the C-ABI consumer
   target and includes `<fix/c_api.h>`, **Then** it configures, compiles and links successfully, and calls a
   real C-ABI symbol resolved from the installed archive.
2. **Given** the same consumer, **When** it additionally includes `<fixpp/session/engine.hpp>`, **Then**
   compilation **fails** with a file-not-found diagnostic.
3. **Given** the same consumer, **When** it includes any of the twelve C-ABI sub-headers by their documented
   spelling (`<fix/c_api/session.h>`, `<fix/c_api/message.h>`, …), **Then** each resolves.

---

### User Story 2 - The claim is held by a witness that can fail (Priority: P1)

The isolation claim has stood in `architecture.md` since it was written, unverified, and was discovered to be
false only when feature 084 built an export and someone read the generated targets file. Whatever this feature
delivers must be pinned by an assertion that goes **red** when the isolation regresses — otherwise the next
refactor silently undoes it and the document goes stale again in exactly the same way.

**Why this priority**: Equal-first with US1. A delivered-but-unpinned isolation is the same failure mode one
refactor later. Per the project's standing rule, a gate that has never been *proven* red proves nothing.

**Independent Test**: Deliberately restore the pre-fix include interface (a one-line local revert); the witness
must fail. Restore the fix; it must pass. Both directions demonstrated, not asserted.

**Acceptance Scenarios**:

1. **Given** the delivered isolation, **When** the consumer witness suite runs, **Then** the compile-must-fail
   assertion passes (the C++ include did not compile) and reports which header it tried.
2. **Given** the isolation locally reverted, **When** the same suite runs, **Then** the assertion **fails**,
   and the failure names the isolation as the cause rather than reporting a generic build error.
3. **Given** a C++ engine header is renamed or deleted, **When** the witness runs, **Then** it does not
   silently pass on a file-not-found that has nothing to do with isolation — the header it probes must be one
   whose absence would itself be a defect. *(A compile-fails assertion that would pass for the wrong reason is
   not a witness; it is a proxy.)*

---

### User Story 3 - C++ consumers are unaffected (Priority: P1)

A C++ integrator links the C++ umbrella target and keeps using the whole surface — including the C-ABI entry
header, which `architecture.md` §7.4 explicitly says the umbrella exposes. Nothing about their include
spellings, their `find_package` call, or their build changes.

**Why this priority**: The isolation is worthless if it is bought by breaking the primary public surface
(Article IV §1 — the C++ library *is* the primary public surface).

**Independent Test**: The existing umbrella consumer witness continues to configure, build and run unchanged,
with no new include paths or hints added to it.

**Acceptance Scenarios**:

1. **Given** a staged install, **When** the umbrella consumer witness builds, **Then** it succeeds with no edit
   to its include paths, library paths, or `find_package` call.
2. **Given** the same consumer, **When** it includes `<fix/c_api.h>`, **Then** it resolves — the umbrella
   exposes both surfaces, per §7.4:502.

---

### User Story 4 - The package still declares what it ships (Priority: P2)

Whoever inspects a produced package can see that the C-ABI headers are present, at their delivered location,
in every configuration and on every platform. The packaging gate must assert this positively, so a future change
that drops or moves them fails loudly rather than shipping a package with no C ABI in it.

**Why this priority**: Second only to the isolation itself. The packaging witness today asserts two
`include/fixpp/**` paths and nothing under the C-ABI tree, so relocating those headers is currently invisible
to it — the very change this feature makes is one the gate cannot see.

**Independent Test**: Produce a package; assert the C-ABI entry header and its sub-headers are present at the
delivered path. Remove them from the install rules; the assertion fails.

**Acceptance Scenarios**:

1. **Given** a produced package on any supported generator, **When** the contents witness runs, **Then** it
   asserts the C-ABI entry header is present at its delivered path.
2. **Given** a package built with the C-ABI headers' install rule removed, **When** the witness runs,
   **Then** it fails and names the missing path.
3. **Given** the Windows ZIP (no `usr/` prefix component) and a Linux DEB/RPM (with one), **When** the witness
   runs on each, **Then** both pass — the assertion normalises the prefix rather than anchoring on it.

---

### User Story 5 - The architecture stops claiming something untrue (Priority: P2)

A reader of `architecture.md` §7.4 and §8 gets a description of what actually ships. The `:503` clause — both
its literal `include/fix/` prescription, which cannot work, and its "cannot accidentally" claim, which was
false — is replaced by the delivered mechanism. `contracts/package-layout.md` §2a, which reasons about
`fixpp_capi`'s include path when justifying D1 Option A, is reconciled with whatever this feature changes.

**Why this priority**: The document has already misled one feature (084 measured the gap and correctly
declined to fix it unilaterally). Leaving a reconciled-but-stale clause behind repeats it.

**Independent Test**: Read §7.4:503 and §8 against the delivered targets file; every claim is checkable and
checks out. No clause prescribes an include path that would break `<fix/c_api.h>`.

**Acceptance Scenarios**:

1. **Given** the delivered package, **When** §7.4:503 is read against the shipped `fixppTargets.cmake`,
   **Then** every clause it states is true of the file.
2. **Given** §8's boundary rule, **When** it is read against what is enforced, **Then** each enforcement it
   names is attributed to the mechanism that actually performs it (installed include interface, in-tree lint,
   or consumer witness) — not to a mechanism that does not.

---

---

### User Story 6 - The service plugin boundary holds too (Priority: P2)

`architecture.md` §8 says the service reaches the engine **exclusively** through `extern "C"` symbols, and
§7.4:504 names the service target's interface as the public plugin surface. Today that target declares the
whole `include/` tree as its own installed interface, independently of the C-ABI target — so the same claim is
false in a second place, and would stay false even after US1 is delivered. A plugin author linking the service
target should reach the plugin interface and the C ABI, and nothing else.

**Why this priority**: Same claim, same document, same failure mode — but its only intended consumer
(`fixppd`) does not exist in any `CMakeLists.txt` yet, so no integrator is affected today. It is delivered here
because leaving a known second hole open is how §7.4:503 became false in the first place.

**Independent Test**: A standalone consumer links only the service target from a staged install. The plugin
interface header and the C-ABI headers compile; a C++ engine header does not.

**Acceptance Scenarios**:

1. **Given** a staged install, **When** a consumer links only the service target and includes
   `<fixpp/service/control_plane_factory.hpp>`, **Then** it compiles.
2. **Given** the same consumer, **When** it includes `<fix/c_api.h>`, **Then** it compiles — the service reaches
   the engine through the C ABI, so that surface is deliberately available.
3. **Given** the same consumer, **When** it includes `<fixpp/wire/parser.hpp>`, **Then** compilation **fails**.
4. **Given** a C++ consumer of the umbrella, **When** it includes `<fixpp/service/control_plane_factory.hpp>`,
   **Then** it still compiles — the isolation adds a root, it does not remove the header from the C++ surface.

---

### Edge Cases

- **A consumer that never uses CMake.** A C program on a Debian/RPM install that compiles with a bare
  `-I/usr/include` and `#include <fix/c_api.h>` resolves today, and **must keep resolving** — the layout change
  is additive, so nothing moves out from under it (FR-005a). This is what makes the isolation safe to ship
  without a migration note.
- **The same header at two installed paths.** The C-ABI headers and the service plugin header each ship at two
  locations. They are installed from **one** source directory, so the copies cannot drift — but a content
  assertion that counts headers, or asserts an exact set of installed paths, will see both and must be written
  knowing that. A consumer never sees both: the include spelling is identical, so exactly one root resolves it.
- **Both roots reachable at once.** A consumer that links *both* the umbrella and the C-ABI target is the
  combination Article IV §2 / `architecture.md`:509 rejects and `tools/check_layers.py` enforces in-tree; the
  installed package has no such lint. What the package does in that case must be stated, not left implicit.
- **The witness's own build environment.** The consumer witness tier is green under the *producing build's*
  environment only (`tests/consumer/CMakeLists.txt:33-38`, spec Assumption 7 of 084). A compile-must-fail
  assertion is not exempt: it must fail for the isolation reason, not because the sub-build lacked a toolchain.
- **The `usr/` prefix asymmetry.** Linux DEB/RPM/TGZ carry a `usr/` component; the Windows ZIP does not
  (`contracts/package-layout.md` §2). Any new content assertion that anchors on `usr/` finds nothing on
  Windows and reports "the package carries no C-ABI headers" — a defect claim about the product manufactured
  by the test.
- **Export-set membership drift.** If the mechanism changes which targets are in the export closure, the
  18-member count recorded in `architecture.md` §7.4's reconciliation table and the T024 membership assertion
  both move with it. Either the count is re-measured and both updated, or the mechanism must leave the closure
  unchanged.
- **The shipped OBJECT files are checked at `find_package` time.** `_cmake_import_check_files_for_fixpp::capi_objects`
  makes a missing `lib/objects-<CONFIG>/**` a configure-time `FATAL_ERROR` for every consumer. A change that
  drops `fixpp_capi_objects` from the export set without also dropping that check breaks every consumer.

## Requirements *(mandatory)*

### Functional Requirements

**The isolation**

- **FR-001**: The installed package MUST provide an include root from which the C-ABI public headers are
  reachable and from which **no** `<fixpp/...>` header is reachable.
- **FR-002**: The C-ABI consumer target's installed interface MUST resolve the C-ABI entry header and **all
  twelve** sub-headers at their existing spellings (`<fix/c_api.h>`, `<fix/c_api/{decimal,dict,engine,error,export,handles,log,message,otel,session,version}.h>`).
  No consumer-visible include spelling changes.
- **FR-003**: The C-ABI consumer target's installed interface MUST NOT make any `<fixpp/...>` header reachable —
  neither directly nor transitively through any target it links. *(Stated as reachability, not as a property of
  one target's `INTERFACE_INCLUDE_DIRECTORIES`, because the defect in #218 is precisely that the direct property
  was empty while the transitive one was wide open.)*
- **FR-003a**: The isolation obligation in FR-003 applies to **by-name** export members — the targets a consumer
  is instructed to link (`fixpp::capi`, `fixpp::service`). It does **not** apply to **closure-only** members,
  which keep their present interfaces. `fixpp::capi_objects` is closure-only by the project's existing taxonomy
  (`CMakeLists.txt:575-584`): no public header names it and nothing instructs anyone to link it — the same class
  as `fixpp::log_otlp`. *(Clarified 2026-08-03. This bounds the witness: it asserts what the documented
  consumption path does, not what an undocumented one could reach.)*
- **FR-004**: The installed C++ umbrella target MUST continue to resolve both the full `<fixpp/...>` surface and
  the C-ABI entry header, with no new consumer-side hints.
- **FR-005**: The in-tree build MUST be unaffected: every existing target configures, builds and tests as before,
  with no source edited to satisfy the new include layout.
- **FR-005a**: The installed layout change MUST be **purely additive**. No header may be removed from, or moved
  within, the location it occupies in the package today; the existing install rule for the public header tree
  MUST NOT acquire an exclusion for the isolated subtrees. A consumer compiling today against
  `<prefix>/include` with a bare include-path flag MUST continue to resolve every header it resolves now,
  `<fix/c_api.h>` included. *(Clarified 2026-08-03.)*

**The witnesses**

- **FR-006**: A consumer witness MUST assert that, against the installed package with only the C-ABI target
  linked, including a C++ engine header **fails to compile**.
- **FR-007**: That assertion MUST be **demonstrated red** — the record MUST show it failing when the isolation
  is removed and passing when it is present. An assertion never observed failing is not evidence.
- **FR-008**: The compile-must-fail witness MUST probe a header whose own disappearance would be a defect, and
  MUST distinguish "failed because isolation holds" from "failed for any other reason". A generic
  build-failure check is not sufficient.
- **FR-009**: The existing positive C-ABI consumer witness (links the C-ABI target by its exported name,
  includes the entry header, resolves a real symbol) MUST continue to pass unchanged in intent.
- **FR-010**: The package-contents witness MUST assert the C-ABI headers are present at **both** their existing
  and their isolated path, and the service plugin header likewise, prefix-normalised so the assertions hold on
  every generator, and MUST fail if any is absent.

**The service plugin boundary** *(in scope per clarification, 2026-08-03)*

- **FR-011**: The installed package MUST provide an include root from which the service plugin interface is
  reachable and from which no `<fixpp/...>` header **other than the service plugin interface itself** is
  reachable.
- **FR-011a**: The service consumer target's installed interface MUST resolve both the service plugin interface
  header at its existing spelling (`<fixpp/service/control_plane_factory.hpp>`) and the C-ABI headers at theirs
  — the service reaches the engine through the C ABI, so it needs that surface and only that surface.
- **FR-011b**: The service consumer target's installed interface MUST NOT make the C++ engine headers
  (`<fixpp/wire/...>`, `<fixpp/session/...>`, `<fixpp/dict/...>`, …) reachable, directly or transitively.
- **FR-011c**: The service plugin interface header MUST remain reachable from the C++ umbrella at its existing
  spelling — `EngineConfig` holds a `unique_ptr<ControlPlaneFactory>` and needs the base type complete, so
  isolating the service surface must not remove it from the C++ surface.
- **FR-012**: The compile-must-fail witness obligations (FR-006 … FR-008) apply to the service consumer target
  as well as the C-ABI one: a witness MUST assert that a C++ engine header fails to compile when only the
  service target is linked, and that assertion MUST likewise be demonstrated red.

**The record**

- **FR-013**: `architecture.md` §7.4:503 MUST be rewritten to describe the delivered mechanism. The literal
  `INTERFACE_INCLUDE_DIRECTORIES = include/fix/` prescription MUST NOT survive in any form, since it cannot be
  satisfied without breaking `<fix/c_api.h>`.
- **FR-013a**: `architecture.md` §7.4:504 MUST be updated to state the service target's delivered include
  interface. Its current reconciliation row dispositions only the target's *kind* and *name* and says nothing
  about its include path — which is how the second instance of the same gap went unrecorded while §7.4:503's
  was being reconciled.
- **FR-014**: `architecture.md` §8 MUST attribute each enforcement it claims to the mechanism that performs it,
  for **both** boundaries it asserts — the C-ABI consumer boundary and the service boundary.
- **FR-015**: `contracts/package-layout.md` §2a MUST be reconciled wherever it reasons about the C-ABI target's
  include path or the export-closure consequences of D1 Option A. *(Note: §2a cites `src/capi/CMakeLists.txt:45`
  for the `PUBLIC` link; the file has it at `:46`. Correct the citation while there.)*
- **FR-016**: If the export closure or its member count changes, the 18-member figure in §7.4's reconciliation
  table and the export-membership assertion MUST both be re-measured from a real generate run — never derived by
  reading `target_link_libraries`. *(§2a records that reading it out was wrong in three places across a
  three-level cascade.)*
- **FR-017**: Issue #218 MUST be closed with the delivered disposition, explicitly recording that its Option 1
  as written was not implementable and why.

### Key Entities

- **C-ABI public header set**: the entry header plus twelve sub-headers under `include/fix/`. Self-contained;
  no dependency on any C++ engine header. This is the set that must be reachable from the isolated root.
- **C-ABI consumer target** (`fixpp::capi`): the target a C-ABI integrator links. Today source-less, reaching
  everything through the OBJECT library. Its *reachable include set* is the subject of this feature.
- **C-ABI object library** (`fixpp::capi_objects`): supplies the objects and, today, the whole-tree include
  path. A forced export-set member whose shipped object files are checked at `find_package` time.
- **Service plugin interface** (`include/fixpp/service/`): one self-contained abstract base header. Public
  plugin surface per §7.4:504, *and* a type the C++ engine's `EngineConfig` holds by pointer — so it belongs to
  two surfaces at once, which is why isolating it means adding a root rather than moving the header.
- **Service consumer target** (`fixpp::service`): what a control-plane plugin author links. Reaches the engine
  through the C ABI only (§8). Its reachable include set is the subject of US6.
- **C++ umbrella target** (`fixpp::fixpp`): the primary public surface. Must reach every header set.
- **Consumer witness project**: a standalone CMake project configured against a staged install, deliberately
  not inheriting the main build's directory-scoped include path — which is what makes it able to observe the
  installed include interface at all.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: With only the C-ABI consumer target linked from an installed package, **0** of the C++ engine
  headers are reachable, and **13 of 13** C-ABI headers are.
- **SC-001a**: With only the service consumer target linked, **0** C++ engine headers are reachable, while the
  service plugin interface header **and** the 13 C-ABI headers are.
- **SC-002**: **Each** compile-must-fail assertion (C-ABI target, service target) is observed **failing** at
  least once with its isolation removed and **passing** with it present; both observations are recorded with
  the commands that produced them.
- **SC-003**: The umbrella consumer witness passes with **zero** edits to its include paths, library paths, or
  `find_package` invocation.
- **SC-003a**: Every header path present in a package built before this change is still present after it —
  the installed layout is a strict superset. Verified by comparing produced package manifests, not by reading
  the install rules.
- **SC-004**: Every clause of `architecture.md` §7.4:503 is checkable against the shipped targets file, and
  **all** of them check out. No clause remains that would break `<fix/c_api.h>` if implemented literally.
- **SC-005**: The package-contents witness asserts the C-ABI headers positively, and that assertion is observed
  failing when their install rule is removed.
- **SC-006**: The export set's membership is re-measured from a generate run; the count recorded in §7.4 matches
  the measurement, whether or not it changed.
- **SC-007**: In-tree, the full test suite result is unchanged from the pre-change baseline on the same host —
  no test newly fails, and **no source file** was edited to accommodate the include-layout change.
- **SC-008**: `find_package(fixpp)` succeeds for a consumer of each target (umbrella, C-ABI) with **no**
  configure-time `FATAL_ERROR` from the imported-file existence checks.

## Assumptions

- **The direction is settled.** Isolation is delivered by giving the isolated surfaces their own installed
  include roots, **in addition to** the existing root, which is left untouched (user decisions, 2026-08-03 —
  see Clarifications). The literal §7.4:503 prescription is rejected as unimplementable, with the evidence
  recorded above.
- **Additive, therefore no migration.** Because nothing moves, this feature ships no consumer migration note
  and no deprecation. If the plan finds a reason the change cannot stay additive, that invalidates FR-005a and
  must be raised, not absorbed.
- **`fixpp::service` is delivered without a consumer.** No `fixppd` target exists yet, so US6 cannot be
  exercised by a real service build. Its witness is a standalone consumer project, the same shape as the C-ABI
  one — which is the only way to observe an installed include interface at all.
- **`<fix/c_api.h>` is frozen.** No consumer-visible include spelling changes. The C ABI's include convention
  is treated as part of the frozen surface even though the freeze (`REMAINING-WORK.md:7`) formally governs
  symbols.
- **No source changes.** The C-ABI headers are already self-contained (measured), so isolation is a build and
  packaging change. Any need to edit a header to achieve it would invalidate this assumption and should be
  raised rather than absorbed.
- **In-tree behaviour is preserved by the directory-scoped include path** at `CMakeLists.txt:234`; the isolation
  is an *installed-interface* property only. In-tree enforcement remains `tools/check_layers.py`'s job, and this
  feature does not extend it.
- **Verification tier.** The consumer witnesses run under the producing build's environment
  (`tests/consumer/CMakeLists.txt:33-38`); they cannot fail on a dependency a third-party consumer would have to
  supply. This feature inherits that limit and does not claim to close it.
- **`fixpp_capi_objects` stays in the export closure** unless the plan proves otherwise on a real install, since
  removing it without also removing the generated existence check breaks `find_package` for every consumer.
- **Platform coverage.** Linux (clang + gcc) locally; MSVC via CI. The `usr/`-prefix asymmetry means any content
  assertion must be written to hold on both without a platform branch.

## Dependencies

- Feature **084-packaging-cpack-export** (merged, PR #219) — supplies the `install(EXPORT)`, the package config,
  the consumer witness harness, and the packaging-contents witness this feature extends. Without it there is no
  installed include interface to isolate.
- `.specify/architecture.md` §7.4 / §8 — the claims being reconciled.
- `specs/084-packaging-cpack-export/contracts/package-layout.md` §2a — the D1 Option A decision this feature
  must either preserve or explicitly supersede.
