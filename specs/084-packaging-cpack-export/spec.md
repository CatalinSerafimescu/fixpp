# Feature Specification: Installable Packaging (CPack) + CMake Package-Config Export

**Feature Branch**: `084-packaging-cpack-export`

**Created**: 2026-07-31

**Status**: Draft

**Input**: REMAINING-WORK.md item B-10 ("Installable packaging (CPack) — `fixpp` + `fixpp-dev` per platform × build, Rel+Dbg; + CMake package-config export (`install(EXPORT)` / `find_package(fixpp)`) — 078 follow-up folded in"), narrowed by a user scope decision dated 2026-07-31.

---

## Context: verified starting state

Every claim below was checked against source in this worktree on 2026-07-31. Where the anchor doc (`remaining-work/packaging-cpack.md`) disagrees, the anchor doc is stale and is corrected by this feature (see FR-024).

| Fact | Evidence |
|---|---|
| No CPack integration exists anywhere | zero `CPACK_*` / `include(CPack)` matches across all `CMakeLists.txt` + `*.cmake` |
| No `install(TARGETS)` / `install(EXPORT)` for the C++ library | only match is `bindings/python/CMakeLists.txt:196` (the Python module) |
| Installation today is headers only | `CMakeLists.txt:321` (`install(DIRECTORY include/)`) and `CMakeLists.txt:346` (generated typed headers) |
| The 078 install denylist is live | `CMakeLists.txt:351-355` — excludes `messages/`, `groups/`, `validators/`, `all.hpp`, `groups.hpp` |
| Project version is `0.0.1` | `CMakeLists.txt:5` |
| Every C++ target is STATIC except one | `add_library` census: `fixpp_core`, `fixpp_session`, `fixpp_wire`, `fixpp_dictionary`, … all STATIC; only `fixpp_capi_shared` is SHARED |
| `linux-gcc-debug` does not exist | `CMakePresets.json` has six `gcc` matches, all `linux-gcc-release`; the `gcc13` Conan profile hardcodes `build_type=Release` |
| Debug archives carry DWARF; Release archives do not | measured `libfixpp_core.a`: Release 13 KB / 0 `.debug_info` sections, Debug 228 KB / 4 |
| All existing Linux legs are OTel-ON | `FIXPP_BUILD_OTEL:BOOL=ON` in the `linux-clang-debug`, `linux-clang-release`, `linux-gcc-release` caches |

---

## Clarifications

### Session 2026-07-31

- Q: Are Debug packages published as release artifacts, or kept as CI-internal artifacts only? → A: CI artifacts only. Grounded in constitution Article V §5 — the mandatory "Work in progress — NOT for production use" disclaimer stays in force until publishing is unblocked, so no configuration is published as a release artifact at this stage. Revisit at GA under REMAINING-WORK item 13.
- Q: Is the GA version string decided here, or deferred to REMAINING-WORK item 13? → A: Keep `0.0.1`; defer the GA version to item 13. FR-005 already binds the package version to the declared project version, so a later bump propagates without any packaging change. Consistent with the deliberately-held `0→1` ABI freeze (REMAINING-WORK A-1) — a `1.x` package name would imply an ABI stability the project is not claiming.
- Q: What license/maintainer metadata do the Linux packages carry, and do they ship the vendored QuickFIX dictionaries? → A: **Ship the dictionaries with full attribution.** Packages declare the project's own license and additionally carry the upstream license text **and** a `NOTICE` file bearing the upstream's required acknowledgment sentence verbatim. Rationale and the limit of what this feature can settle are recorded in Assumption 10.

---

## User Scenarios & Testing *(mandatory)*

### User Story 1 — A downstream C++ project consumes fixpp via `find_package` (Priority: P1)

An integrator wants to use fixpp in their own CMake project. They install a fixpp package, write `find_package(fixpp REQUIRED)` and `target_link_libraries(app PRIVATE fixpp::fixpp)`, include a core header, and build. Everything — include paths, compile features, transitive third-party dependencies — arrives through the imported target. They never hand-add an include directory and never guess a link line.

**Why this priority**: This is the entire point of a `-dev` package. Today a consumer must hand-add the include directory because there is no config package at all, which makes fixpp effectively un-consumable by normal CMake projects. Every other story in this feature is packaging *around* this capability. It is also the 078 follow-up that B-10 explicitly folds in.

**Independent Test**: Extend the existing `fixpp::consumer::install-witness` ctest (`CMakeLists.txt:289-311`, driven by `tests/consumer/run_consumer_witness.cmake`) to stage-install the build, then configure the standalone consumer project against the staged prefix using `find_package(fixpp)` + `fixpp::fixpp` rather than a hand-added include path. Green witness = story delivered, with zero packaging work required.

**Acceptance Scenarios**:

1. **Given** a staged install prefix produced from any in-scope configuration, **When** a standalone CMake project calls `find_package(fixpp REQUIRED)`, **Then** configuration succeeds and `fixpp::fixpp` is defined as an imported target.
2. **Given** that consumer project, **When** it links only `fixpp::fixpp` and includes a public core header, **Then** it compiles and links with no manually specified include directory and no manually specified library path.
3. **Given** a consumer built in a configuration different from the installed package, **When** it configures, **Then** it either resolves correctly or fails with a clear diagnostic — never with an undefined-symbol error at link time.
4. **Given** an installed package, **When** the consumer's `find_package` runs, **Then** every third-party dependency required by the exported targets is located transitively without the consumer naming it.
5. **Given** the installed include tree, **When** a consumer attempts to include a typed-builder `Args` header, **Then** the header is absent — confirming the 078 export-scope boundary holds.
6. **Given** an installed package, **When** a **real FIX client program** — one that establishes sessions, sends and receives application messages over a transport, and is already used to benchmark fixpp against a third-party engine — is built against it, **Then** it compiles, links, and runs against a live counterparty.
7. **Given** that real client, **When** it is built, **Then** it names only the installed package and its own dependencies — it does not reach into the fixpp source tree, its private header roots, or its test-support headers.

**Witness strategy for this story** — two tiers, because they fail differently:

- The **minimal** tier (the existing `fixpp::consumer::install-witness`) proves `find_package` resolves and the umbrella target carries include paths. It is fast, runs in every configuration, and catches config-package regressions.
- The **real-client** tier proves the export is *sufficient to build a working FIX application*. This is the tier that catches an export set that resolves but cannot link — which the minimal witness structurally cannot detect, because a header-only consumer never exercises the link interface.

The real-client tier is grounded in an existing program rather than a purpose-built one: `perf/fixpp_perf_driver.cpp` already consumes only public `<fixpp/...>` headers (engine, session, application, transport, TLS, parser) and links the session/transport/TLS stack with a documented static-link-order constraint against OpenSSL. It is the fixpp half of the cross-engine benchmark rig — it establishes real sessions over real sockets in both plaintext and TLS, in single-process and split initiator/acceptor topologies — so it is a genuine client by construction, not a mock.

Three adaptations the packaged variant requires, none of which change the program's status as a real client:

- Its one non-public include is a **test-support dictionary helper**; the packaged variant MUST replace it with a runtime dictionary load through the public API, reading a dictionary shipped in the package (FR-018a).
- Its build **fetches an external histogram library from the network** and adds the source and test directories to its include path. The packaged variant MUST drop all three — the histogram is latency instrumentation irrelevant to a link-and-run witness, and the include paths are exactly what SC-012 forbids.
- It is **disabled by default** behind a build option, so the witness must enable it explicitly rather than assume it is present.

**A standing caution from the benchmark work applies directly here**: this driver is documented as needing an in-tree build so that it links freshly generated libraries rather than a stale prebuilt one. Building it out-of-tree against an installed package is a deliberate inversion of that guidance, and it is safe *only* because the package is produced from the build under test — which is what FR-021a exists to enforce. Treat any relaxation of FR-021a as reintroducing the staleness trap that guidance was written to prevent.

---

### User Story 2 — An operator installs fixpp from a platform-native package (Priority: P2)

An operator on a supported platform obtains a package artifact for their toolchain and configuration and installs it with their platform's normal tooling. They get public headers, the static libraries, and the CMake package config in standard locations, with package metadata that identifies the product, version, and license.

**Why this priority**: This is B-10's stated business value — "operators need installable artifacts, not just source builds." It ranks below Story 1 because a package whose contents cannot be consumed via `find_package` is a container with no product in it; Story 1 defines the payload, Story 2 distributes it.

**Independent Test**: Run the package step on a configured build tree and assert the produced artifacts exist, carry the expected name/version, and — for at least one generator — that extracting them yields the same file set as a direct staged install.

**Acceptance Scenarios**:

1. **Given** a configured and built tree for an in-scope Linux configuration, **When** the package step runs, **Then** DEB, RPM, and TGZ artifacts are produced, each named to encode product, version, platform, toolchain, and configuration.
2. **Given** a configured and built tree for an in-scope Windows configuration, **When** the package step runs, **Then** a ZIP artifact is produced with the same naming scheme.
3. **Given** any produced package, **When** its contents are listed, **Then** they contain the public headers, the exported libraries, and the CMake package-config files — and contain no test executables, no build system scratch, and no denylisted generated sources.
4. **Given** a Debug-configuration package, **When** its libraries are inspected, **Then** debug information is present and usable for symbolication.
5. **Given** a Release-configuration package, **When** its libraries are inspected, **Then** no debug information is present.

---

### User Story 3 — Every green CI run leaves downloadable package artifacts (Priority: P3)

A maintainer opens a completed CI run for a supported lane and downloads the package artifacts it produced, without rebuilding anything locally.

**Why this priority**: Real but derivative — it automates Story 2's output. Deferring it costs convenience, not capability.

**Independent Test**: Inspect a CI run for an in-scope lane and confirm package artifacts are attached with the expected names.

**Acceptance Scenarios**:

1. **Given** a green CI run on an in-scope lane, **When** the run completes, **Then** that lane's package artifacts are attached and downloadable.
2. **Given** artifacts from multiple lanes in one run, **When** they are listed, **Then** every artifact name is unique and identifies its platform, toolchain, and configuration.

---

### Edge Cases

- **Configuration mismatch between package and consumer.** A dependency resolved for one configuration can leave an imported target with no location for another, producing undefined references at link time rather than a configuration error. The existing witness already documents this exact failure for the private XML-parser dependency (`CMakeLists.txt:299-304`). Must fail loudly at configure time, not silently at link time.
- **A new generated-artifact kind appears under the codegen include root.** The denylist at `CMakeLists.txt:351-355` is an allowlist-by-exclusion: anything new is installed by default. A future emitter that adds a directory type would silently leak unexported symbols into the package.
- **An exported target transitively reaches an unexported one.** Exporting a target whose link interface names `fixpp_builders_<ver>` or `fixpp_validators_<ver>` breaks consumer configuration, because those targets are absent from the export set by design.
- **Debug package size.** Debug archives carry DWARF for every translation unit; the measured Release-to-Debug ratio on a single library is ~17×. Packages must remain producible without exhausting build-host storage.
- **Windows debug information lives outside the library file.** Debug info is emitted to separate symbol files rather than into the static library, so a Windows Debug package built with Linux-shaped install rules would ship undebuggable libraries. There is no Linux counterpart to this rule.
- **Stale export state across a reconfigure.** A tree configured before the export existed, then reconfigured, must not produce a package containing a partially-generated config.
- **A consumer requests an incompatible version.** `find_package(fixpp 1.0 REQUIRED)` against an installed 0.0.1 must fail at configure time with a version diagnostic.

---

## Requirements *(mandatory)*

### Functional Requirements

#### Package-config export (the 078 follow-up)

- **FR-001**: The build MUST define a single public umbrella target, exported under the `fixpp::` namespace, that a consumer links to obtain the public API.
- **FR-002**: The umbrella target MUST carry the public include directories such that they resolve correctly both from within the build tree and from an installed prefix.
- **FR-003**: The build MUST install an export set and generate a package-config file plus a version file, placing them where `find_package(fixpp)` locates them without consumer-side hints.
- **FR-004**: The package-config file MUST resolve every third-party dependency that the exported targets require, so a consumer never names those dependencies itself.
- **FR-005**: The version file MUST derive its version from the project version declared in the build system (currently `0.0.1`) — never from a separately maintained literal.
- **FR-006**: The version file MUST reject an incompatible requested version at configure time with a version-specific diagnostic.
- **FR-007**: The precompiled per-version builder and validator libraries MUST remain outside the export set. *(Settled by 078 Gate B P1 — install-scope coherence. Not open for relitigation.)*
- **FR-008**: No target in the export set may expose a link-interface dependency on a target outside the export set.
- **FR-009**: The generated-header install denylist MUST remain coherent with the export set; the feature MUST leave a machine-checkable statement of that coherence rather than relying on a comment.
- **FR-010**: The feature MUST verify that the designed export set reaches nothing under the typed-builder `messages/` or `groups/` trees, and MUST record that verification with its evidence. *(The span-based `Args` API-freeze gate — `packaging-cpack.md:45` / SC-001 / L-078-1. The existing denylist at `CMakeLists.txt:351-355` already excludes both trees, so this is expected to be a confirmation. **If the final export set does reach either tree, work MUST STOP and the deferred "Option 3" decision MUST be escalated to the user before any export ships.**)*

- **FR-010a**: The export set MUST be sufficient for a consumer to build and link a working FIX application — establishing sessions, exchanging application messages, and using secured transport — using only the installed package. *(A headers-only export is insufficient by construction: `perf/fixpp_perf_driver.cpp` links the session, transport, and TLS libraries directly. This settles the "which targets are exported" question as a requirement rather than leaving it to design.)*
- **FR-010b**: The exported link interface MUST carry the ordering and transitive-dependency constraints that these libraries require, such that a consumer linking the umbrella target does not have to reproduce them. *(The TLS library references symbols from the system cryptography libraries and requires a specific static-link order; `perf/CMakeLists.txt:56-57` documents this and reproduces it by hand today. A consumer of an installed package cannot be expected to know it.)*
- **FR-010c**: The package-config file MUST resolve **every** third-party dependency the exported targets need — including the cryptography, compression, XML-parsing, and networking libraries — not only the XML parser. *(Enumerated from the real client's actual link line and the private dependency documented at `CMakeLists.txt:299-304`. FR-004 states the principle; this states that the enumeration must be derived from a real client's requirements rather than assumed.)*

#### Packaging

- **FR-011**: The build MUST produce installable package artifacts for each in-scope configuration.
- **FR-012**: Each package MUST contain the public headers, the exported libraries, and the CMake package-config files.
- **FR-013**: Each package MUST exclude test executables, build-system scratch, and every denylisted generated artifact.
- **FR-014**: Packages MUST be produced for both Release and Debug of each in-scope toolchain.
- **FR-015**: Linux packages MUST be produced in DEB, RPM, and TGZ forms; Windows packages MUST be produced in ZIP form.
- **FR-016**: The packaging design MUST leave a documented seam for adding a Windows installer format later, without requiring that format now.
- **FR-017**: Package artifact names MUST encode product, version, platform, toolchain, and configuration, and MUST be unique across the in-scope matrix.
- **FR-018**: Package metadata MUST identify product name, version, description, license, and maintainer. The declared license MUST be the project's own (`AGPL-3.0`, per constitution Article V §1).
- **FR-018a**: Packages MUST include the vendored FIX dictionary data files, so that the runtime dictionary-loading API shipped in the same package has inputs to read. *(`dict::load_any` takes a filesystem path; today `dictionaries/` has **no install rule at all**, so a package would ship the API with none of its data.)*
- **FR-018b**: Every package containing those dictionaries MUST also carry, in the installed tree, **both**: (a) the upstream license text verbatim, and (b) a `NOTICE` file containing the upstream's required acknowledgment sentence verbatim. *(These are two distinct obligations. The upstream license's redistribution clause is satisfied by shipping the license text; its acknowledgment clause requires a specific sentence to appear in end-user documentation, which the license file states as a requirement but does not itself satisfy. No `NOTICE` file exists in the repository today — this feature creates it.)*
- **FR-018c**: Package descriptions MUST state third-party engine compatibility as fact and MUST NOT imply endorsement by, or affiliation with, the upstream project. *(The upstream license forbids using its names to endorse or promote derived products. This constrains marketing wording in DEB/RPM description fields, which is otherwise easy to write carelessly.)*
- **FR-018d**: The attribution set (dictionaries, license text, `NOTICE`) MUST be verified present by enumerating installed package contents, not by inspecting install rules. *(An install rule that silently matches nothing produces a legally deficient package that looks correct in the build system.)*
- **FR-019**: Windows Debug packages MUST include the separate debug-symbol files that the Microsoft toolchain emits alongside its static libraries. *(This has no Linux counterpart — on Linux the debug info is inside the archive members. The exact artifact naming and location MUST be verified against real Microsoft-toolchain output during implementation, not assumed.)*
- **FR-020**: The staging location used to assemble a package MUST live inside the build tree, so that deleting a build tree also removes everything staged from it and no system location is ever written to.
- **FR-021**: Produced package artifacts MUST be written to a location that survives deletion of the build tree.
- **FR-021a**: Any witness that consumes an installed package MUST consume one produced by the **current** build, and MUST NOT be satisfiable by a previously produced artifact. Each package artifact MUST carry provenance sufficient to detect this — at minimum the configuration it was built from and the source revision — and the witness MUST fail if it consumes a package whose provenance does not match the build under test.

  > **Why this is a live hazard rather than a theoretical one.** The build strategy in Assumption 5 deletes each build tree after packaging, while FR-021 deliberately preserves the produced artifacts *past* that deletion. Those two rules together create a directory of surviving packages from earlier configurations and earlier source states — precisely the input that would let a witness report green against a package that predates the change under test. The known hazard for this repository is a consumer linking a library of unknown vintage rather than the freshly generated one; the surviving-artifact directory is the mechanism by which that could happen here.

#### Build matrix

- **FR-022**: The build system MUST gain a `linux-gcc-debug` configuration — preset and dependency-resolution settings — because packaging a gcc Debug artifact is in scope and no such configuration exists today.
- **FR-023**: Every in-scope configuration MUST be buildable and packageable one at a time, with the preceding configuration's build tree deleted, on a build host with substantially less storage than the whole matrix would require simultaneously.

#### CI and documentation

- **FR-024**: The feature MUST correct the verified-stale claims in `remaining-work/packaging-cpack.md`: line 38 ("no install() rules at all" — two exist, at `CMakeLists.txt:321` and `:346`), line 79 (`project(VERSION)` "unset / 0.0.0" — it is `0.0.1`), and the drifted citations in line 43 (`:328` → `:321`; `:352-364` → `:345-357`).
- **FR-025**: The feature MUST record the 2026-07-31 user scope narrowing in the anchor doc, so the descoped platforms read as a decision rather than as an unmet prerequisite.

  > **Sequencing constraint for FR-024 and FR-025.** Both target `research/G19-fix-fpml-iso20022/REMAINING-WORK.md` and `remaining-work/packaging-cpack.md`, which live in the **parent repository**, a sibling of the library submodule — they cannot be committed on this feature branch. The parent working tree also currently carries an unrelated in-flight modification. These edits MUST therefore be deferred to close-out and staged deliberately as a parent-repo commit, never bundled with a submodule commit and never swept in alongside another feature's parent-pointer bump.
- **FR-026**: CI lanes covering in-scope configurations MUST attach their package artifacts to the run, named per FR-017.

### Key Entities

- **Package artifact** — one distributable file for one (platform, toolchain, configuration, format) tuple. Attributes: product, version, platform, toolchain, configuration, format.
- **Export set** — the collection of build targets made available to consumers under the `fixpp::` namespace. Its boundary is a correctness surface (FR-007, FR-008, FR-010), not a convenience choice.
- **Package config** — the generated files that let `find_package(fixpp)` succeed: imported target definitions, transitive dependency resolution, and version compatibility logic.
- **Install denylist** — the exclusion set (`CMakeLists.txt:351-355`) keeping unexported generated artifacts out of the installed tree. Must stay coherent with the export set.
- **Staging prefix** — the transient location where install rules deposit files for a package. Lives inside the build tree (FR-020).

---

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A consumer project that names only `find_package(fixpp)` and the umbrella target compiles and links against an installed package, with zero manually specified include or library paths.
- **SC-002**: The consumer witness passes for every in-scope configuration, and the consumer includes **both** a hand-written public header and a generated per-version typed header. *(Both categories reach the installed tree from different install rules — `CMakeLists.txt:321` and `:346` — and the second is filtered by the denylist. A witness that includes only a core header would go green against a per-version export whose installed headers are absent: `find_package` would succeed, the imported target would exist, and the failure would surface only at a consumer's `#include`.)*
- **SC-003**: All six in-scope configurations produce package artifacts; the count and names match the declared matrix exactly, with no silent omissions.
- **SC-004**: Zero test executables, zero build-system scratch files, and zero denylisted generated artifacts appear in any produced package — verified by enumerating package contents, not by inspection of install rules.
- **SC-005**: Debug packages yield usable symbolication; Release packages contain no debug information. Verified on both Linux and Windows, each by its own platform-appropriate check.
- **SC-006**: Requesting an incompatible version through `find_package` fails at configure time with a version-specific diagnostic — not at build or link time.
- **SC-007**: A machine-checkable assertion fails if a target enters the export set while exposing a dependency on a target outside it (FR-008), or if a new generated-artifact kind escapes the denylist (FR-009). Each assertion is proven to fail on a deliberately broken input before being accepted as a gate.
- **SC-008**: The complete matrix is produced on a build host with under 64 GB of free build storage, building and deleting one configuration at a time.
- **SC-009**: The FR-010 `Args` boundary verification is recorded with its evidence, stating explicitly whether the export set reaches the typed-builder trees.
- **SC-010**: CI runs for in-scope lanes attach package artifacts with unique, matrix-identifying names.
- **SC-011**: A real FIX client — an existing benchmark or interoperability program, not one written for this feature — builds, links, and runs against an installed package while naming no path inside the fixpp source tree. Demonstrated on at least one in-scope configuration.
- **SC-012**: That real client's build is proven to depend on the installed package alone: it succeeds with the fixpp source tree unavailable, or an equivalent check establishes that no source-tree path participates. *(Without this, a witness configured next to the source tree can silently pick up headers or libraries from it and report green for an export that would fail on any other machine.)*
- **SC-013**: Every produced package contains the vendored dictionary data files, the upstream license text, and a `NOTICE` file carrying the required acknowledgment sentence — verified by enumerating package contents. A package missing any of the three fails the check.
- **SC-014**: A consumer can load a shipped dictionary through the public runtime-loading API using only paths inside the installed prefix, with no file from the source tree. *(Proves FR-018a delivered a usable pairing of API and data, not merely co-located files.)*
- **SC-015**: The generated package config resolves successfully against a build with telemetry **disabled**, not only against telemetry-enabled builds. *(Guards the defect class described in Assumption 12: a config that unconditionally requires the telemetry dependency breaks every telemetry-disabled consumer, and no telemetry-enabled configuration can detect it.)*

---

## Scope

### In scope

Six configurations:

| Platform | Toolchain | Configurations | Package formats |
|---|---|---|---|
| Linux | clang | Release, Debug | DEB, RPM, TGZ |
| Linux | gcc | Release, Debug | DEB, RPM, TGZ |
| Windows | MSVC | Release, Debug | ZIP |

### Out of scope — standing user decision, 2026-07-31

These are **explicit narrowings by the user**, not unmet prerequisites. Recorded here so downstream review does not read them as contradicting the anchor doc:

- **macOS / Tier-4 and its native installer format.** The anchor doc (`packaging-cpack.md:53-55`) treats the macOS lane as a blocking prerequisite; the user removed macOS from scope entirely.
- **Linux clang-libc++ / Tier-3.** Same basis (`packaging-cpack.md:69`).
- **"OTel-enabled builds must feed the packages"** (`packaging-cpack.md:54`). Out of scope as a *requirement*. Note this does not mean packages are built with OTel disabled — all four existing Linux legs are OTel-ON and the in-scope packages stay consistent with them (see Assumptions).
- **Windows installer formats beyond ZIP.** Deferred by user decision; FR-016 preserves the seam.

### Explicit non-goals

- **Do not add shared-library variants of the core targets.** Every C++ target is deliberately STATIC. Introducing shared variants is an ABI commitment, and REMAINING-WORK A-1 is deliberately *holding* the `0→1` ABI freeze. A packaging feature must not make that decision as a side effect.
- **Do not export the builder/validator libraries** (FR-007 — settled by 078 Gate B P1).
- **Do not resolve the deferred "Option 3" `Args` representation change.** FR-010 only *verifies* the boundary is untouched; if it turns out not to be, the decision escalates rather than being made here.

---

## Assumptions

Recorded because the feature description did not fully specify them. Each is a reasonable default, not a discovered fact.

1. **One package per configuration, dev-shaped** (user decision). Each package carries headers + static libraries + CMake package config. There is **no separate binary-only runtime package** before v1.0. Rationale: the anchor doc defines the runtime package as "shared libs, no headers", but the `add_library` census shows exactly one SHARED target in the entire project — such a package would contain a single library and nothing else. The alternative (adding shared variants) is ruled out as an ABI decision above.

2. **Both Release and Debug packages are produced and archived as CI artifacts; none are published as release artifacts.** *(Clarified 2026-07-31.)* Constitution Article V §5 keeps a "Work in progress — NOT for production use" disclaimer in force until publishing is unblocked, so this feature produces distributable artifacts without establishing a distribution channel. Packages are downloadable from CI runs for maintainer use. Publication is REMAINING-WORK item 13's decision at GA, and this feature MUST NOT pre-empt it.

3. **Package version is the project version, `0.0.1`, and this feature does not change it.** *(Clarified 2026-07-31.)* The GA version string is REMAINING-WORK item 13's decision. Because FR-005 binds the package version to the declared project version rather than to a packaging-local literal, a later bump propagates with no change to any packaging rule. A `1.x` package name now would imply an ABI stability that REMAINING-WORK A-1 is deliberately withholding.

4. **In-scope packages are built OTel-ON**, consistent with all four existing Linux legs (`FIXPP_BUILD_OTEL:BOOL=ON` verified in three existing caches). Descoping the *requirement* that OTel-ON builds feed packages does not license producing an OTel-OFF package for one leg only — that would make the gcc Debug package the sole one missing the OTel targets. Building dependencies with OTel disabled is acceptable **only** as a development accelerator while the packaging logic is being written (it reduces the gcc-Debug from-source dependency build from 9 packages to 3), never for a shipped artifact.

5. **The build matrix is exercised one configuration at a time**, deleting each build tree before the next. Measured basis: a Debug tree is ~22 GB, of which ~11 GB is test executables and ~4.5 GB is object files; the installable payload is ~4.6 GB of archives. Peak single-configuration footprint is therefore well within the available build storage (FR-023, SC-008).

   **Ordering starts with gcc Release** — the only configuration that is cheap on *both* axes: a 3.4 GB tree and zero third-party dependencies to build (Assumption 9). The smallest-tree configuration is clang Release, but it carries 5 dependency builds, so "smallest" and "fastest to first package" are different configurations here. Validating the export and packaging logic on gcc Release means the first end-to-end run is bounded by fixpp's own compile time and nothing else.

6. **Windows packaging is produced in a separate build sandbox** on different storage from the Linux matrix, so the Windows configurations do not consume the Linux build-storage budget.

7. **The existing consumer witness is extended, not replaced.** `fixpp::consumer::install-witness` already stage-installs, configures a standalone project against the staged prefix, and inherits the build configuration. Only its discovery mechanism changes — from a hand-added include directory to `find_package`.

8. **Dependency resolution for the new gcc Debug configuration reuses the existing dependency profile** with the configuration overridden, rather than introducing a separate profile — unless resolution proves that insufficient.

9. **Third-party dependency availability is uneven across the in-scope configurations, and the gap is time, not storage.** Resolved 2026-07-31 against the local dependency cache: clang Debug and gcc Release need **nothing** built; clang Release needs **5** packages built from source; gcc Debug needs **9**. In both deficient cases the cost is dominated by the telemetry SDK and its transitive chain. Ordering should account for this — the nominally "cheapest" configuration by disk is not the cheapest by wall-clock — and the dependency build is a one-time cost per (toolchain, configuration) pair that persists in the shared cache. This shared cache lives on different storage from the build matrix and is unaffected by the build-tree deletion cycle in Assumption 5.

10. **Packages ship the vendored dictionaries with full attribution — and this feature discharges the mechanical obligations only.** *(Clarified 2026-07-31.)* Shipping them keeps the package self-sufficient: the same package exposes a runtime dictionary-loading API that reads files from disk, and omitting the data would ship an API with no inputs. The attribution obligations are **two**, not one: the upstream license text must be reproduced in the distribution (satisfied by shipping it), *and* a specific acknowledgment sentence must appear in end-user documentation (satisfied only by a `NOTICE` file, which does not exist in the repository today and which this feature creates). Shipping the license file alone does **not** discharge the second.

    **Explicit limit — this feature does not close REMAINING-WORK item 15d.** That item's open question is whether the upstream's advertising-style acknowledgment clause constitutes an additional restriction incompatible with the project's own license, and it is pending counsel review. Choosing to ship the dictionaries makes that question *live* rather than hypothetical; it does not answer it. This feature implements the mechanical attribution correctly and MUST NOT be read as legal clearance. Item 15d remains open and gates publishing (constitution Article V §5), which is consistent with Assumption 2 — nothing here is published as a release artifact.

11. **The verification matrix is derived from touched trees, and this feature is expected to fall outside the bucket that triggers the full preset matrix.** The verification gate buckets a change by the paths it touches: library sources and public headers trigger the full C++ preset matrix; the C-ABI surface triggers the ABI gates; the Python bindings trigger their own matrix; a change confined to specifications and documentation triggers none. This feature touches build configuration, shared build modules, the preset set, the consumer witness, and packaging metadata — **none of which is a library source or public header**. The full preset matrix and the coverage gate are therefore expected to be inapplicable.

    **This MUST be derived from the actual diff at verification time, not assumed here.** Two things make it worth checking rather than asserting: the bucket taxonomy has **no explicit bucket for a build-system-only change** — such a change is neither a source change nor strictly documentation-only — and if the implementation ends up touching a public header, the full matrix applies after all. If the diff lands outside every bucket, that gap MUST be recorded explicitly in the verification record rather than silently resolved as documentation-only. Any preset that is skipped MUST be skipped through the gate's own waiver mechanism, which requires a paired recorded rationale; an unpaired skip is a failure by design.

12. **Excluding the alternate-standard-library configuration from verification requires replacing the coverage it provides, not merely noting its absence.** That configuration is the only one built with telemetry disabled. Consequently it is the only one that would expose a config package which unconditionally requires the telemetry dependency — a defect that would break **every** consumer of a telemetry-disabled build, and which no telemetry-enabled configuration can detect. Since that configuration is descoped from packaging (user decision, 2026-07-31) and is expected to be out of the verification matrix per Assumption 11, this feature MUST instead verify the underlying risk directly: the generated package config MUST be exercised against a telemetry-disabled build and MUST resolve. This is materially cheaper than building that configuration (its tree measures ~8 GB) and it tests the actual hazard rather than a proxy for it. **Dropping the configuration without this substitute check is not acceptable** — it would remove the only existing coverage of a real packaging defect class.

---

## Open Questions

### ⚠️ OPEN — surfaced during Phase 0 planning: what does the package do about the C ABI?

**The defect is verified, the remedy is a decision.** `include/fix/c_api.h` and `include/fix/c_api/` exist, and the header install rule at `CMakeLists.txt:321` installs the **entire** `include/` tree. Packages therefore ship the C-ABI header **with no library behind it** — structurally the same defect FR-018a fixes for the dictionaries, and it exists in the current install rules independent of this feature.

It matters because constitution Article IV §2 makes the C ABI *"the legal isolation boundary for AGPL/commercial dual licensing"* — a commercial consumer links that surface, not `fixpp::fixpp`. Three targets exist: `fixpp_capi` (STATIC), `fixpp_capi_objects` (OBJECT), `fixpp_capi_shared` (SHARED).

| Option | Answer | Implications |
|---|---|---|
| A | Export the static C-ABI library so the shipped header has a library | Smallest coherent fix — removes the API-without-substance defect without touching the shared library. Commercial consumers needing dynamic isolation are still unserved. |
| B | Export the static **and** shared C-ABI libraries | Serves the Article IV §2 isolation boundary properly, and the shared library is the one place a binary-only runtime package would have real content. But shipping a consumable shared C ABI sits closer to an ABI commitment than shipping headers, and REMAINING-WORK A-1 deliberately holds the `0→1` freeze. |
| C | Exclude `include/fix/` from the package; state C-ABI packaging as a non-goal | Internally consistent, but **removes a header that ships today** — a regression in delivered content, and it must be stated rather than allowed to happen silently. |

**Not resolved here deliberately.** All three readings are defensible and they differ materially — B in particular touches the held ABI freeze, which is not this feature's call to make. Routed to Gate A with the evidence above. **Whichever is chosen, the outcome must be stated explicitly**; the one unacceptable outcome is leaving the header shipping with no library and no recorded decision.

### Resolved

All three questions raised at specification time were resolved in the 2026-07-31 clarification session — see the **Clarifications** section for the answers and Assumptions 2, 3, and 11 for how each is applied.

One item is deliberately **carried, not resolved**: REMAINING-WORK item 15d (whether the upstream dictionary license's acknowledgment clause is compatible with the project's own license) remains open and pending counsel review. This feature discharges the mechanical attribution obligations and does not close 15d — see Assumption 11. Because 15d gates publishing and nothing here is published (Assumption 2), it does not block this feature.
