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
> **Every C-ABI header carries the `fix/` component in its own include spelling** — in *two* forms, both
> broken by the same path. A consumer writes `#include <fix/c_api.h>`
> (`tests/consumer/consumer_capi_witness.cpp:26`); the entry header pulls its sub-headers in **quoted** form
> (`include/fix/c_api.h:40-48` — `#include "fix/c_api/export.h"` … `"fix/c_api/message.h"`); and the
> sub-headers reference each other in **angle-bracket** form (`include/fix/c_api/session.h:29-31` —
> `#include <fix/c_api/error.h>` …). An interface include path of `<prefix>/include/fix` makes all three
> resolve to `<prefix>/include/fix/fix/c_api/…` and **breaks every C-ABI consumer** — the quoted form too,
> since the includer-relative search finds `include/fix/fix/…` no more than the `-I` search does. The clause is
> self-inconsistent with the project's own include convention and cannot be satisfied literally.
>
> **This feature delivers the isolation by *additional* installed include roots instead** (user decisions,
> 2026-08-03 — see Clarifications), which preserves every existing include spelling and every existing
> installed path verbatim while making the restriction real for the targets consumers are told to link.

### What was measured before writing this spec (2026-08-03, `main` @ `24595e11`)

| Fact | Evidence | Consequence for scope |
|---|---|---|
| The C-ABI public header set is **12 files** | `find include/fix -type f` → 12: `c_api.h` plus **11** sub-headers (`decimal`, `dict`, `engine`, `error`, `export`, `handles`, `log`, `message`, `otel`, `session`, `version`). `specs/084-packaging-cpack-export/contracts/package-layout.md` §2a records the same figure ("`include/fix/` + `include/fix/c_api/` (12 files)") | This is the census FR-002, SC-001 and SC-001a range over. **`store.h` is not missing**: `.specify/2i-capi.md:133` lists it among the *designed* domain-split headers, but the C-ABI surface is DONE (CA-001..010) and GA-frozen at `1.5.0`, additive-only (**[parent-repo]**`/REMAINING-WORK.md:7` — root given under Normative References), and `2i-capi.md:93` assigns the store *function* surface to design doc **2e**. The delivered 12-file set **is** the intended v1.0 surface; a `store.h` would be a post-GA MINOR addition. **Not a question 086 answers** |
| The C-ABI headers are **self-contained** | `include/fix/**` includes only `<fix/c_api/...>` / `"fix/c_api/..."` and C stdlib headers (`stdint.h`, `stddef.h`, `stdbool.h`). **Zero** `<fixpp/...>` includes | Isolation is achievable — no header has to be rewritten to make the boundary hold |
| **In-tree blast radius is zero** | `CMakeLists.txt:234` — `include_directories("${CMAKE_SOURCE_DIR}/include")`, directory-scoped over the whole build. The **11 of 28** `tests/capi/*.cpp` whose compilation depends on the `-I` search for a `fixpp/` header receive it from there, **not** through the `fixpp_capi` target — **6** spell it `<fixpp/...>` and **5** only `"fixpp/..."` (`error_surface_test`, `error_block_test`, `capi_group_delimiter_ctx_test`, `error_live_test`, `send_recv_test`), and since `tests/capi/` contains no `fixpp/` subdirectory the quoted form falls back to the same search. Counted by `ls tests/capi/*.cpp \| wc -l`, `grep -l 'fixpp/' tests/capi/*.cpp \| wc -l`. *(The angle-bracket-only figure is the count this table carried until Gate A r2; `include_directories()` is directory-scoped over all 28 regardless of spelling, so the argument is untouched — but the figure named the wrong set, which is exactly the quoted-vs-angle-bracket conflation corrected elsewhere in this Context table.)* | The gap is installed-package-only, exactly as #218 states. Narrowing the target's interface cannot break an in-tree build |
| `fixpp_capi_objects` is **load-bearing in the export closure** | It is an export member by **explicit enumeration** — listed by name in `FIXPP_EXPORT_TARGETS` (`CMakeLists.txt:562`), consumed by `install(TARGETS ${FIXPP_EXPORT_TARGETS} EXPORT fixppTargets)` (`:733`) — **not** because `fixpp_capi` is source-less and links it `PUBLIC` (`src/capi/CMakeLists.txt:46`), which was this row's basis until Gate A r2 and is superseded by FR-003a and `research.md` R2. The distinction matters here: membership by enumeration is stable under *any* change to that link keyword. The shipped `lib/objects-<CONFIG>/` files are checked by `_cmake_import_check_files_for_fixpp::capi_objects` and removing them makes `find_package(fixpp)` **`FATAL_ERROR`** for every consumer | Any change to that link keyword is a **measured** obligation, not a read-off-the-source one. Export-set membership and the shipped object files must be re-verified on a real configure + install |
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
  `fixpp::capi_objects` keeps its whole-tree interface: it is **closure-only** — no public header names it
  (`grep -rn capi_objects include/` → **0 hits**) and nothing instructs anyone to link it (the only such
  instruction in any public header is `include/fixpp/config/toml_config_loader.hpp:7-8`, for
  `fixpp::config_toml`). Narrowing it would cascade into the in-tree graph and into the export-closure coupling
  that makes `find_package` `FATAL_ERROR`.

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
3. **Given** the same consumer, **When** it includes any of the **eleven** C-ABI sub-headers by their documented
   spelling (`<fix/c_api/session.h>`, `<fix/c_api/message.h>`, …), **Then** each resolves — from a **C**
   translation unit as well as a C++ one.

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
  combination Article IV §2 / `architecture.md`:509 rejects; the installed package has no lint at all, and
  `tools/check_layers.py` does not enforce it either — it is a **source include-edge lint over `src/**` and
  `bindings/**`** (`tools/check_layers.py:2-7`, `:173-176`), with no notion of a CMake link interface and no
  reach into an installed consumer. **Stated outcome** (contract §5): both roots land on the search path, the
  include spelling is identical so exactly one root wins per translation unit, the headers are byte-identical
  (I2), and the package **does not and cannot reject the combination**. The rejection is an in-tree
  convention, documented, not mechanically enforced anywhere.
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
  eleven** sub-headers at their existing spellings (`<fix/c_api.h>`, `<fix/c_api/{decimal,dict,engine,error,export,handles,log,message,otel,session,version}.h>`
  — **12 files**, the census measured in Context). No consumer-visible include spelling changes. The census
  MUST be derived from the tree (`find include/fix -type f`), never transcribed, so it cannot drift from the
  delivered set. **The resolution MUST be asserted from a C translation unit as well as a C++ one** — US1
  promises the behaviour to "a C or C++ integrator", and while header C-cleanliness is already pinned in-tree
  by two pure-C gates (`tests/capi/CMakeLists.txt:12` `enable_language(C)`, `:13` and `:23`), no gate compiles
  C against the *installed* interface.
- **FR-003**: The C-ABI consumer target's installed interface MUST NOT make any `<fixpp/...>` header reachable —
  neither directly nor transitively through any target it links. *(Stated as reachability, not as a property of
  one target's `INTERFACE_INCLUDE_DIRECTORIES`, because the defect in #218 is precisely that the direct property
  was empty while the transitive one was wide open.)*
- **FR-003a**: The isolation obligation in FR-003 applies to **by-name** export members — the targets a consumer
  is instructed to link (`fixpp::capi`, `fixpp::service`). It does **not** apply to **closure-only** members,
  which keep their present interfaces. `fixpp::capi_objects` is closure-only **by the measured predicate**: no
  public header names it (`grep -rn capi_objects include/` → 0 hits) and no public header instructs linking it
  (the only such instruction is `include/fixpp/config/toml_config_loader.hpp:7-8`, naming `fixpp::config_toml`).
  Its export-set membership is by **explicit enumeration** — it is listed by name in `FIXPP_EXPORT_TARGETS`
  (`CMakeLists.txt:562`), consumed by `install(TARGETS ${FIXPP_EXPORT_TARGETS} EXPORT fixppTargets)` — not by
  closure inference. *(The `CMakeLists.txt:575-585` commentary that earlier versions of this spec cited
  classifies the **five targets the umbrella does not reach**, and names only `fixpp_log_otlp` closure-only;
  `capi_objects` is not classified there at all. The conclusion is unchanged, the basis is now the predicate
  rather than that comment. Clarified 2026-08-03. This bounds the witness: it asserts what the documented
  consumption path does, not what an undocumented one could reach.)*
- **FR-004**: The installed C++ umbrella target MUST continue to resolve both the full `<fixpp/...>` surface and
  the C-ABI entry header, with no new consumer-side hints. **This needs a new assertion**: the existing umbrella
  witness includes only `<fixpp/dict/dictionary.hpp>`, `<fixpp/dict/xml_loader.hpp>`, `<fixpp/wire/parser.hpp>`
  and `<fixpp/v44/Messages.hpp>` (`tests/consumer/consumer_witness.cpp:34-37`) — **neither `<fix/c_api.h>` nor
  `<fixpp/service/control_plane_factory.hpp>`** — so it does not witness the C-ABI leg of this requirement, US3
  scenario 2, or FR-011c. The new assertion MUST be a **separate compile-only umbrella probe**, not an edit to
  `consumer_witness.cpp`: SC-003 requires that witness to pass unchanged, and changing what it includes would
  spend the very invariant it exists to hold.
- **FR-005**: The in-tree build MUST be unaffected: every existing target configures, builds and tests as before,
  with no source edited to satisfy the new include layout.
- **FR-005a**: The installed **file layout** change MUST be **purely additive**. No header may be removed from,
  or moved within, the location it occupies in the package today; the existing install rule for the public
  header tree MUST NOT acquire an exclusion for the isolated subtrees. A consumer compiling today against
  `<prefix>/include` with a bare include-path flag MUST continue to resolve every header it resolves now,
  `<fix/c_api.h>` included. *(Clarified 2026-08-03.)*
- **FR-005b**: **Additivity in FR-005a constrains the installed file layout ONLY. It does NOT constrain the
  target graph, and satisfying it is not sufficient.** Adding a root does not subtract one: today
  `fixpp::capi`'s whole-tree include path arrives through `INTERFACE_LINK_LIBRARIES "fixpp::capi_objects"`, and
  `fixpp::service` declares its own at `src/service/CMakeLists.txt:12`. Both **MUST** be cut, and each by-name
  target given its own restricted installed include interface. A change that adds the new roots and install
  rules while leaving those two edges intact ships a package that **satisfies FR-005a, FR-010 and SC-003a and
  still fails FR-003** — every content-shaped gate green over the live defect. *(Added after clarify: the
  additive answer resolves the layout question and leaves the target-graph obligation untouched; they are
  independent and the spec must not let one be read as discharging the other.)*

**The witnesses**

- **FR-006**: A consumer witness MUST assert that, against the installed package with only the C-ABI target
  linked, including a C++ engine header **fails to compile**.
- **FR-006a**: **The must-fail assertion MUST be expressed in a form the existing consumer-witness harness can
  carry.** `tests/consumer/` is a standalone sub-project whose driver runs one `cmake --build` and raises
  `FATAL_ERROR` on **any** non-zero build exit (`tests/consumer/run_consumer_witness.cmake:96-104`). A probe
  *target* that is required to fail therefore reds the entire witness and the assertion cannot be expressed as
  a target at all. The mechanism MUST evaluate the negative cell where `fixpp::capi`'s usage requirements
  propagate exactly as they do to a real consumer target, and MUST invert the result so that *compiling* is
  the failure. *(Mechanism instance and fallback: `contracts/include-interface.md` §4a and `research.md` R5;
  the instance is **measured** in `research.md` **R9**.)*
- **FR-007**: That assertion MUST be **demonstrated red** — the record MUST show it failing when the isolation
  is removed and passing when it is present. An assertion never observed failing is not evidence.
- **FR-008**: The compile-must-fail witness MUST probe a header whose own disappearance would be a defect, and
  MUST distinguish "failed because isolation holds" from "failed for any other reason". A generic
  build-failure check is not sufficient.
- **FR-008a**: **The positive assertion alone cannot detect the defect, and MUST NOT be cited as if it could.**
  Because the layout is additive, `<fix/c_api.h>` resolves from **either** root — so a witness observing that
  it compiles cannot distinguish "isolation delivered, resolved from the isolated root" from "isolation absent,
  resolved from the whole-tree root". Only the **paired** observation discriminates: the C-ABI include succeeds
  **and** the C++ engine include fails. Any evidence offered for FR-003 / SC-001 MUST be the pair; a passing
  positive witness on its own is compatible with the defect being fully present. *(This is the
  additive-layout counterpart of the trap in #218 itself, where a target property read clean while the
  transitive path was wide open.)*
- **FR-009**: The existing positive C-ABI consumer witness (links the C-ABI target by its exported name,
  includes the entry header, resolves a real symbol) MUST continue to pass unchanged in intent, and MUST be
  **strengthened to exercise the transitive archive set**. As it stands it references only
  `fixpp_library_version()` and `fixpp_strerror()` (`tests/consumer/consumer_capi_witness.cpp:31,36`), whose
  objects (`version.cpp`, `error.cpp`) reference nothing outside `fixpp_capi_objects` — and the consumer link
  line carries the archive with **zero loose objects** (measured, `tests/packaging/run_package_contents_witness.cmake:439-441`),
  so the engine archives arrive purely through `capi_objects`' `INTERFACE_LINK_LIBRARIES`. If that edge were
  lost, this witness would still pass. It MUST additionally reference an entry point whose object pulls the
  session/dictionary closure out of the archive (`fixpp_dict_load_from_xml` / `fixpp_engine_create` —
  `src/capi/dictionary.cpp`, `src/capi/engine.cpp`). *(This witness is **built and linked, never run**:
  `run_consumer_witness.cmake:110` runs `${_sub_build}/consumer_witness` — the umbrella witness — and asserts
  `^PASS:` on that binary alone (`:142-143`); `tests/consumer/CMakeLists.txt:71` states it in as many words,
  "Building and linking IS the assertion — it need not run". The reference must therefore pull the entry
  point's object out of the archive at **link** time. **The form is load-bearing**: a namespace-scope,
  non-`static`, non-`const` pointer initialised with the entry point's address (or a call), never an address
  assigned to an unused local — a local can be optimised away *together with its relocation*, silently
  restoring the gap this requirement exists to close *(Gate A r3 carry-forward #6)*. And
  **no runtime behaviour is asserted**. Verifiers MUST NOT record a run of `consumer_capi_witness` as FR-009 or
  SC-008 evidence; there is none. Adding a runtime assertion on the C-ABI leg would be a separate, larger
  change to `run_consumer_witness.cmake` and is not in scope here.)*
- **FR-009a**: **Narrowing the include interface MUST NOT withhold any other usage requirement the closure
  relies on.** `$<LINK_ONLY:>` withholds `INTERFACE_COMPILE_DEFINITIONS`, `INTERFACE_COMPILE_OPTIONS`,
  `INTERFACE_COMPILE_FEATURES` and `INTERFACE_SYSTEM_INCLUDE_DIRECTORIES` as well as include directories, and
  the closure carries at least one live PUBLIC compile definition — `FIXPP_LOG_MIN_LEVEL`
  (`src/log/CMakeLists.txt:27`, documented at `:24-26` as propagated to every consumer, consumed unguarded at
  `include/fixpp/log/logger.hpp:275,301,333`). No C-ABI consumer reaches `logger.hpp` today, so nothing breaks
  now; the requirement exists because the safety argument was stated over include directories only and would
  not notice if one did. The requirement is discharged by **two separate, separately-measured obligations** —
  they observe different things and neither can stand in for the other.

  - **FR-009a(i) — direct-property delta, as a CLOSED ENUMERATION.** `fixpp::capi`'s generated
    `set_target_properties(fixpp::capi PROPERTIES …)` block MUST change, OFF→ON, in exactly the ways the
    **re-measured** diff shows and in no other way: the changed properties are enumerated **by name after that
    measurement** (`research.md` R3 carries the measurement; `quickstart.md` §3 the extraction), and no
    property outside that enumeration may be added, removed or altered. *The enumeration is filled in from the
    measurement, never transcribed ahead of it* — a hardcoded property list written before the real
    `fixppTargets.cmake` is generated becomes an unsatisfiable MUST the moment the real block carries one more
    property than the fixture did. What is fixed by this requirement is the **shape** of the check (closed
    enumeration, positively named, no wildcard "nothing else may differ"), not its contents.

    **The enumeration, now filled in** *(Gate A r3 carry-forward #3 — measured, so it is no longer a blank)*:

    | stage | property NAMES in `fixpp::capi`'s generated block | source |
    |---|---|---|
    | **OFF** (pre-feature) | `{INTERFACE_LINK_LIBRARIES}` | measured on the real installed artifact, `research.md` R3 |
    | **ON** (isolated) | `{INTERFACE_INCLUDE_DIRECTORIES, INTERFACE_LINK_LIBRARIES}` | `contracts/include-interface.md` §2 |

    Note what the delta actually **is**: the include property is **gained** and `INTERFACE_LINK_LIBRARIES` is
    **rewritten** (bare → `$<LINK_ONLY:…>`). "Loses only `INTERFACE_INCLUDE_DIRECTORIES`" is false of the
    delivered design and was never satisfiable. `quickstart.md` §3 compares both name sets against these two
    maps rather than printing them, so an unexpected **third** changed property fails the step.
  - **FR-009a(ii) — effective usage-requirement delta, MEASURED AT THE CONSUMER.** FR-009a(i) is structurally
    incapable of observing compile definitions: they reach a C-ABI consumer through
    `fixpp_capi_objects` → `fixpp_log` (`src/capi/CMakeLists.txt:29-38`), never through `fixpp::capi`'s own
    block, which reads identically either way. `/speckit-implement` MUST therefore (a) enumerate the closure's
    PUBLIC/INTERFACE compile definitions, options and features **once**, and (b) measure, at a probe target
    inside the configured consumer sub-project, which of them a `fixpp::capi` consumer actually receives —
    `file(GENERATE … CONTENT "$<TARGET_PROPERTY:<probe>,COMPILE_DEFINITIONS>")` **and the same for
    `COMPILE_OPTIONS` and `COMPILE_FEATURES`**, all three in the one `file(GENERATE)` and all three in the one
    driver compare (three lines, no new machinery), **not** by diffing the targets file. `$<LINK_ONLY:>`
    withholds all three, and measured on the real export the live surface is `COMPILE_DEFINITIONS` only — but
    this requirement must not be narrower than its own claim, which is about *usage requirements*, not about
    definitions *(Gate A r3 carry-forward #5)*. **The comparison step MUST be named and MUST run**: `file(GENERATE)` writes at *generate* time, so no
    configure-time `if()` can read it back — the read-and-compare belongs in the driver after the sub-build
    (`tests/consumer/run_consumer_witness.cmake`, after the build at `:96-104`), or in a `cmake -P` compare
    attached as a build step. A `file(GENERATE)` whose output nothing reads asserts nothing. The measured set
    MUST equal the enumerated expected set exactly. **The expected set MUST have a named producer in the tree,
  not in the implementer's head**: step (a)'s enumeration is written into `tests/consumer/CMakeLists.txt` as a
  literal and handed to the driver (the same way `FIXPP_STAGE_PREFIX` already is,
  `run_consumer_witness.cmake:86`), so the comparison has a right-hand side with an origin. A comparison whose
  expected value is derived from the run it checks is satisfied by whatever the run produced — the same
  no-op-gate shape as a `file(GENERATE)` nothing reads. One **recorded, pre-approved exception** applies: the
    definitions enumerated in (a) as presently unreachable from any C-ABI consumer — today **at least**
    `FIXPP_LOG_MIN_LEVEL` and `ASIO_STANDALONE`; the complete set is enumerated per (a) and membership is
    decided by the predicate, not by this list — are **permitted** to be absent, as an accepted and recorded consequence of the
    narrowing. Anything else that goes missing MUST be republished on `fixpp_capi` directly. *(Measured
    instrument: `research.md` R10 — the probe reads `FIXPP_LOG_MIN_LEVEL` at ISO=OFF and loses it at ISO=ON, so
    the check discriminates rather than merely reporting.)*
- **FR-010**: The package-contents witness MUST assert the C-ABI headers are present at **both** their existing
  and their isolated path, and the service plugin header likewise, prefix-normalised so the assertions hold on
  every generator, and MUST fail if any is absent.
- **FR-010a**: The package-contents witness MUST **also** assert what the isolated roots *contain* — every
  installed path under `include/capi/` matches `^include/capi/fix/`, and every path under
  `include/service-iface/` matches `^include/service-iface/fixpp/service/`. This is the only assertion that
  traces FR-001, which is a property of a **root** rather than of a target, and the existing content gates are
  structurally blind to it: the denylist anchors on `^include/fixpp/…`
  (`tests/packaging/run_package_contents_witness.cmake:484-487`) and the exact-set generated-tree check on
  `^include/fixpp/(v[A-Za-z0-9]+)/…` (`:508`) — neither regex can ever match a path under the new roots.
  Without it, a partial or over-broad duplication under an isolated root passes every gate unless a negative
  probe happens to name the duplicated header.

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
  isolating the service surface must not remove it from the C++ surface. **Witnessed by the new umbrella probe
  of FR-004**, since no existing witness includes that header.
- **FR-011d**: The whole-tree installed include declaration on the service target —
  `"$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"` at **`src/service/CMakeLists.txt:12`** — MUST be
  replaced by the isolated root. It is named explicitly because it is the single line that makes FR-011b false,
  it is *not* inherited from the C-ABI target (so narrowing that target does not touch it), and every other
  requirement in this feature can be satisfied while it survives.
- **FR-011e**: **The independence in FR-011d is directional, and the red demonstrations MUST be written to
  it.** *Forward* (the fix direction) it holds: narrowing `fixpp::capi` does not narrow `fixpp::service`, so
  both edits are needed. *Backward* (the revert direction) it does **not**: `fixpp_service` links `fixpp_capi`
  (`src/service/CMakeLists.txt:16`), so reverting `src/capi/CMakeLists.txt:46` to `PUBLIC` restores the
  un-wrapped `INTERFACE_LINK_LIBRARIES "fixpp::capi_objects"` and `fixpp::service` inherits
  `${_IMPORT_PREFIX}/include` transitively — reddening **both** probes. A verifier who reverts the C-ABI line
  and observes both go red records SC-002 evidence for the service leg that proves nothing about
  `src/service/CMakeLists.txt:12`. The service red demonstration MUST therefore be produced by reverting
  **`src/service/CMakeLists.txt:12` only**, with the C-ABI isolation intact, and MUST record `fixpp::capi`'s
  observed properties from that same run as proof the C-ABI leg was not the cause.
- **FR-012**: The compile-must-fail witness obligations (FR-006, FR-006a, FR-007, FR-008) apply to the service
  consumer target as well as the C-ABI one: a witness MUST assert that a C++ engine header fails to compile
  when only the service target is linked, expressed in the same harness-compatible form (FR-006a), and that
  assertion MUST likewise be demonstrated red — by the isolating revert FR-011e specifies, not by the C-ABI one.

**The record**

> **FR-013 … FR-015 are scoped by CLAIM, not by line label.** The `:503` and `:504` claims each exist in **two**
> places in the current file — the 084 reconciliation-table row *and* the original clause prose — and are
> replicated verbatim into two CMake files. A queue that names line labels fixes one site and leaves the rest,
> which is the US5 failure mode itself. The site lists below are **non-exhaustive evidence**, not the scope.

- **FR-013**: **No statement about the C-ABI or service targets' include interfaces may remain untrue of the
  delivered tree** — in `.specify/architecture.md` §7.4 and §8, in `specs/084-packaging-cpack-export/contracts/package-layout.md`
  §2a, or in the CMake comments that restate them. In particular the literal
  `INTERFACE_INCLUDE_DIRECTORIES = include/fix/` prescription MUST NOT survive in any form, since it cannot be
  satisfied without breaking `<fix/c_api.h>`. *Known sites (non-exhaustive)*: `architecture.md:514` (the 084
  reconciliation row for `:503`), `:515` (the row for `:504`), `:537` (the original `:503` clause prose),
  `:538` (the original `:504` prose — which additionally names `fixpp::service-iface`, a target that does not
  exist under that name, and `ControlPlane` / `ControlPlaneConfig`, two types that do not exist at all; the one
  shipped header is `control_plane_factory.hpp`).
- **FR-013a**: The reconciliation MUST state the service target's **delivered include interface** — not only
  its *kind* and *name*, which is all `architecture.md:515` dispositions today and is how the second instance
  of the same gap went unrecorded while `:503`'s was being reconciled.
- **FR-014**: **No statement about what `tools/check_layers.py` enforces may remain untrue**, and §8 MUST
  attribute each enforcement it claims to the mechanism that actually performs it, for **both** boundaries.
  The script is a **source include-edge lint**: it walks `#include` lines under `src/**` and `bindings/**`
  against an allowed-edge whitelist (`tools/check_layers.py:2-7`, `:173-176`). It reads no CMake target links,
  sees no installed consumer, and has no notion of a link interface. *Known sites (non-exhaustive)*:
  `architecture.md:543` ("any target downstream of `fixpp::capi` that also lists `fixpp` … fails the
  `tools/check_layers.py` lint" — false, and inside the very section FR-013 edits), `:518` (the 084 row
  "STILL ENFORCED, narrower than it reads" — understates it), `:561` (claims the lint "scans `service/` source"
  — there is no `service/` directory at the repo root), `:557` (names `include/fixpp/service/control_plane.h`,
  which does not exist), `:560` (names `fixppd` and `fixpp::service-iface`, neither of which exists as a
  target); and the verbatim replications at `CMakeLists.txt:580` and `tests/consumer/CMakeLists.txt:68-69`,
  **which are where this bundle inherited the claim from** — leaving them re-seeds the next reader exactly as
  `:503` seeded 084.
- **FR-015**: `specs/084-packaging-cpack-export/contracts/package-layout.md` §2a MUST be reconciled wherever it
  reasons about the C-ABI target's include path or the export-closure consequences of D1 Option A, **and its
  citations into `src/capi/CMakeLists.txt` and `CMakeLists.txt` MUST be re-verified as a set**, not one line at
  a time. **The drift is not a constant, which is exactly why an offset cannot be applied blind.** Every
  citation at or below the `add_library(fixpp_capi STATIC)` insertion point is +1: `:45` → `:46` (the `PUBLIC`
  link), `:43` → `:44` (`fixpp_capi STATIC`), `:47-48` → `:48-49` (the `fixpp_capi_shared` gate), `:70` → `:71`
  (`WINDOWS_EXPORT_ALL_SYMBOLS`). But §2a also cites `:36` for the `fixpp_tap` PUBLIC edge (`package-layout.md:132`
  and `:134`); the edge is at **`:37`** — and `:36` sits *above* the insertion point, inside the
  `target_link_libraries(fixpp_capi_objects PUBLIC …)` block at `:29-38`, so that one is **not** explained by
  the same shift. Meanwhile `:11` (`add_library(fixpp_capi_objects OBJECT`) is still correct. Separately, the
  **five** `CMakeLists.txt:321-324` citations for the public-header install rule now point at `:446-451`.
  Re-verify each citation against the file; do not offset them by a constant, and do not correct only the one
  line this feature happens to touch — that is the phrase-scoped error this queue exists to stop.
- **FR-016**: If the export closure or its member count changes, the 18-member figure in §7.4's reconciliation
  table and the export-membership assertion MUST both be re-measured from a real generate run — never derived by
  reading `target_link_libraries`. *(§2a records that reading it out was wrong in three places across a
  three-level cascade.)*
- **FR-017**: Issue #218 MUST be closed with the delivered disposition, explicitly recording that its Option 1
  as written was not implementable and why.

### Key Entities

- **C-ABI public header set**: the entry header plus **eleven** sub-headers under `include/fix/` — **12 files**,
  measured. Self-contained; no dependency on any C++ engine header. This is the set that must be reachable from
  the isolated root.
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

- **SC-001**: With only the C-ABI consumer target linked from an installed package, **12 of 12** C-ABI headers
  are reachable (the census measured in Context: `c_api.h` + 11 sub-headers, derived by
  `find include/fix -type f`, not transcribed), and **no** C++ engine header is — **evidenced by the named
  negative probes plus C-5 root containment**, which is what "0" means here. Concretely: the
  `<fixpp/wire/parser.hpp>` and `<fixpp/service/control_plane_factory.hpp>` probes of
  `contracts/include-interface.md` §4 both assert FALSE, **and** `fixpp::capi`'s only installed include root is
  `include/capi`, whose contents C-5 / I11 pin to `^include/capi/fix/`. *(Scoped this way deliberately: a "0
  over all nine header families" claim asserted by two probes would be a universal claim on representative
  evidence. The two named probes plus the pinned root are what is actually measured, and together they exclude
  every family — nothing outside `include/capi/fix/` is on the search path at all.)* Evidence is the **paired**
  observation required by FR-008a — the positive reachability counts and the negative results from the *same*
  configured consumer. The positive count alone does not satisfy SC-001, because under the additive layout it
  is equally consistent with the defect being present.
- **SC-001a**: With only the service consumer target linked, the service plugin interface header **and** the 12
  C-ABI headers are reachable, and no C++ engine header is — same evidentiary basis as SC-001 (the named
  `<fixpp/wire/parser.hpp>` negative probe for `fixpp::service`, plus C-5 containment of
  `include/service-iface`), and the same paired-evidence rule.
- **SC-002**: **Each** compile-must-fail assertion (C-ABI target, service target) is observed **failing** at
  least once with **its own** isolation removed — per FR-011e, the service demonstration reverts
  `src/service/CMakeLists.txt:12` alone — and **passing** with it present; both observations are recorded with
  the commands that produced them, the exit code, and the first diagnostic line, in
  `.specify/decisions/086-capi-include-isolation-verify.md`.
- **SC-003**: The umbrella consumer witness passes with **zero** edits to its include paths, library paths, or
  `find_package` invocation.
- **SC-003a**: Every header path present in a package built before this change is still present after it —
  the installed layout is a strict superset. Verified by comparing **produced artifacts** — the staged-install
  manifests of §2, from two prefixes each created empty — never by reading the install rules. *(§8's packaging
  witness carries the assertion at the CPack-package level; §2's staged trees are the same property observed on
  the artifact `cmake --install` actually produced, which is what "not by reading install rules" asks for.)*
- **SC-004**: Every clause of `architecture.md` §7.4:503 is checkable against the shipped targets file, and
  **all** of them check out. No clause remains that would break `<fix/c_api.h>` if implemented literally.
- **SC-005**: The package-contents witness asserts the C-ABI headers positively, and that assertion is observed
  failing when their install rule is removed.
- **SC-006**: The export set's membership is re-measured from a generate run; the count recorded in §7.4 matches
  the measurement, whether or not it changed.
- **SC-007**: In-tree, the full test suite result is unchanged from the pre-change baseline on the same host —
  no test newly fails, and **no production C/C++ source or public header** was edited to accommodate the
  include-layout change. *(The feature does add witness translation units under `tests/consumer/`; the claim is
  about production source, which is what FR-005 and the "zero source files change" assumption mean.)* The baseline
  is a **durable artifact captured from a named pre-feature commit** (quickstart §0/§2), not an ambient file;
  the comparison is automated to a non-zero exit, not eyeballed. **"No test newly fails" is asserted by
  RESULT, not by test names.** A name-set diff is structurally incapable of observing a failure, and this
  feature registers no new ctest test (the probes are targets and configure-time `try_compile` inside an
  existing sub-project), so the name sets are expected identical whatever happens. The assertions are therefore
  (a) both `ctest` runs' **exit codes** are captured and the after-run's MUST be 0; (b) the baseline run's exit
  code MUST be 0 too — or, if the host's baseline is not green, its failing tests MUST be enumerated by name in
  `.specify/decisions/086-capi-include-isolation-verify.md` and the after-run's failure set MUST equal exactly
  that enumerated set; (c) the per-test **status** lines and the `N% tests passed, M tests failed out of K`
  summary line MUST match between the two runs; (d) the test-name sets MUST match, so no test disappears.
  *(Commands in `quickstart.md` §9. "Unchanged" against an equally-red baseline is not a pass — leg (b) is what
  makes that explicit.)*
- **SC-008**: `find_package(fixpp)` succeeds for a consumer of **each of the three by-name targets** — umbrella,
  C-ABI **and service** — with **no** configure-time `FATAL_ERROR` from the imported-file existence checks.
  `fixpp::service` is included because US6/FR-011 put it in scope and its imported-target resolution is the one
  newly exercised path.

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
  is treated as part of the frozen surface even though the freeze formally governs symbols. The freeze record
  lives **outside this repository**, in the parent research repo at **[parent-repo]**`/REMAINING-WORK.md:7`
  (`git ls-files | grep -i remaining` in this repo returns nothing; the absolute root that **[parent-repo]**
  stands for is given in the Cross-repository citations note under Normative References) — cited this way
  because it is also the disposition record for the 12-file census (Context).
- **No source changes.** The C-ABI headers are already self-contained (measured), so isolation is a build and
  packaging change. Any need to edit a header to achieve it would invalidate this assumption and should be
  raised rather than absorbed.
- **In-tree behaviour is preserved by the directory-scoped include path** at `CMakeLists.txt:234`; the isolation
  is an *installed-interface* property only. In-tree enforcement remains `tools/check_layers.py`'s job — a
  **source include-edge lint over `src/**` and `bindings/**`** (`tools/check_layers.py:2-7`, `:173-176`), not a
  CMake target-link check — and this feature does not extend it.
- **Verification tier.** The consumer witnesses run under the producing build's environment
  (`tests/consumer/CMakeLists.txt:33-38`); they cannot fail on a dependency a third-party consumer would have to
  supply. This feature inherits that limit and does not claim to close it.
- **`fixpp_capi_objects` stays in the export closure** unless the plan proves otherwise on a real install, since
  removing it without also removing the generated existence check breaks `find_package` for every consumer.
- **Platform coverage.** Linux (clang + gcc) locally; MSVC via CI. The `usr/`-prefix asymmetry means any content
  assertion must be written to hold on both without a platform branch.

## Normative References

Per `[const §VI.5]` (`.specify/constitution.md:164`), the exact entries that inform this spec. **This feature
has no FIX-normative content and introduces no OFFICIAL catalogue rows** — it changes nothing about message
semantics, encoding or validation, so no `[DocAbbrev §X.Y.Z]` FIX section is engaged and `[const §VI.4]`'s
coverage-index obligation is not triggered. The governing authorities are constitutional and architectural, and
they are listed here because §5 is a **presence** obligation: the honest discharge is to record that the FIX set
is empty and name what does govern.

- **`[const §IV.2]`** (`.specify/constitution.md:141`) — the C ABI is the AGPL/commercial legal-isolation
  boundary. This is the article that makes an unenforced C-ABI include boundary a defect rather than a
  cosmetic one; US1 and FR-003 exist to deliver it.
- **`[const §X.1]`** (`.specify/constitution.md:220`) — *"The C ABI in `include/fix/c_api.h` is a versioned
  contract. Every change to it is reviewed against the contract; Codex Gate A is mandatory."* Quoted exactly:
  the article governs changes to that **header**, and this feature changes no header byte. Gate A is run and
  the four §6 controls applied by deliberate conservative classification — see `plan.md`'s Article X §6 box.
- **`[const §X.6]`** (`.specify/constitution.md:225`) — ABI-affecting features trigger all four mandatory
  controls (`/clarify`, `/analyze`, Codex Gate A, user `/plan` sign-off). Tracked in `plan.md`'s Constitution
  Check.
- **`[const §VII.8]`** (`.specify/constitution.md:178`) — *"Tests are selected by `ctest -L <label>`, never
  `-R <exe-name>`."* Binds every selector in `quickstart.md`.
- **`[const §IX]`** (`.specify/constitution.md:196-215`) — the unchanged mandatory matrix. §4's static-analysis
  controls apply to the new witness translation units this feature adds.
- **`.specify/architecture.md` §7.4 (`:498-544`) and §8 (`:551` ff.)** — the claims being reconciled (FR-013,
  FR-013a, FR-014). §7.4's 084 reconciliation table at `:509-518` is the record that found `:503` NOT
  DELIVERED.
- **`specs/084-packaging-cpack-export/contracts/package-layout.md` §2a (`:96-140`)** — the D1 Option A decision
  and the 12-file C-ABI header census (`:132`), which this feature must either preserve or explicitly
  supersede (FR-015).
- **`REMAINING-WORK.md:7`** — **[parent-repo]**, see the note below — the C-ABI GA freeze at `1.5.0`,
  additive-only for `FIXPP_C_ABI_VERSION_MAJOR == 1`. The authority for treating the delivered 12-file set as
  the complete v1.0 surface.

> ### Cross-repository citations — the root they resolve against
>
> Several citations in this bundle name files that do **not** exist in this repository and cannot be resolved
> from this worktree (`git ls-files | grep -i remaining` returns nothing). They live in the **parent research
> repository**, whose absolute root on this host is:
>
> ```
> [parent-repo] = /home/catalin/Work/Programming/Antreprenoriat/research/G19-fix-fpml-iso20022/
> ```
>
> Everything written as **[parent-repo]**`/<path>` in this bundle — `REMAINING-WORK.md`,
> `phases/phase-4/parallel-worktrees.md`, `phases/phase-4/cleanup-phase.md`, `research/reviews/*` — resolves
> under that root, not under this repo's `research/` (which is a different directory entirely). Citations
> written **without** a `[parent-repo]` marker resolve against this repository's root.

## Dependencies

- Feature **084-packaging-cpack-export** (merged, PR #219) — supplies the `install(EXPORT)`, the package config,
  the consumer witness harness, and the packaging-contents witness this feature extends. Without it there is no
  installed include interface to isolate.
- `.specify/architecture.md` §7.4 / §8 — the claims being reconciled.
- `specs/084-packaging-cpack-export/contracts/package-layout.md` §2a — the D1 Option A decision this feature
  must either preserve or explicitly supersede.
