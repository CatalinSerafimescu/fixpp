# Phase 1 Data Model — 084-packaging-cpack-export

This feature has no runtime data model. Its "entities" are **build-system and distribution artifacts**, and their invariants are what the success criteria check. Modelling them explicitly matters because most of the defect classes here are *structural* — an install rule matching nothing, a target exported with a dangling dependency, a package consumed from the wrong build.

---

## E1 — Export Set

The collection of build targets made available to consumers under the `fixpp::` namespace.

| Attribute | Value |
|---|---|
| Export name | `fixppTargets` |
| Namespace | `fixpp::` |
| Umbrella target | `fixpp::fixpp` |
| Members | Transitive closure a real client links (research R2) + per-version dict INTERFACE targets |
| Excluded | `fixpp_builders_<ver>`, `fixpp_validators_<ver>` (FR-007) |

**Members** (`fixpp_core`, `fixpp_sync`, `fixpp_log`, `fixpp_wire`, `fixpp_dictionary`, `fixpp_tls`, `fixpp_transport`, `fixpp_session`, `fixpp::dict::<ver>`), plus telemetry targets **only when built**.

**Invariants**

- **I1 — Closure.** No member may expose a link-interface dependency on a non-member (FR-008). CMake enforces this at generate time; SC-007 additionally requires a check *proven to fail* on a deliberately broken input.
- **I2 — Denylist coherence.** No member's install interface may resolve to a location containing denylisted generated artifacts (FR-009, FR-010).
- **I3 — Configuration-dependence.** Membership varies with build options. The generated config must reflect what was built, never a hardcoded list (research R2).
- **I4 — Dual include interface.** Every member carrying headers needs both `$<BUILD_INTERFACE:>` and `$<INSTALL_INTERFACE:>`. The dict targets currently have only the former (`cmake/Codegen.cmake:543-544`).
- **I5 — Compile-definition fidelity.** `FIXPP_LOG_MIN_LEVEL` must reach consumers (public headers branch on it, and it is build-type-conditional). `FIXPP_BUILD_OTEL` must **not** — it reaches no public header, and adding it would create an ODR mismatch that currently cannot occur (research R4).

---

## E2 — Package Config

The generated files that make `find_package(fixpp)` succeed.

| Attribute | Value |
|---|---|
| Files | `fixppConfig.cmake`, `fixppConfigVersion.cmake`, `fixppTargets*.cmake` |
| Version source | `project(VERSION)` — currently `0.0.1` (`CMakeLists.txt:5`) |
| Dependencies resolved | OpenSSL, asio, pugixml + conditionally opentelemetry-cpp |

**Invariants**

- **I6 — Transitive resolution.** Every third-party dependency the exported targets need is resolved without the consumer naming it (FR-004).
- **I7 — No false dependencies.** The config must not require packages the library does not use. ZLIB is deliberately excluded (research R3) — a spurious requirement is as much a defect as a missing one.
- **I8 — Conditional telemetry.** The telemetry dependency is required only when the package was built with telemetry. SC-015 exercises this.
- **I9 — Version compatibility.** An incompatible requested version fails at **configure** time with a version-specific diagnostic (FR-006, SC-006) — never at build or link.

---

## E3 — Package Artifact

One distributable file for one (platform, toolchain, configuration, format) tuple.

| Attribute | Source |
|---|---|
| Product / version | `project()` |
| Platform · toolchain · configuration | The preset being built |
| Format | DEB · RPM · TGZ (Linux) · ZIP (Windows) |
| Provenance | Configuration + source revision (FR-021a) |

**Invariants**

- **I10 — Name uniqueness.** Names encode product, version, platform, toolchain, configuration, and are unique across the matrix (FR-017).
- **I11 — Content sufficiency.** Contains public headers, exported libraries, config files, dictionaries, and the attribution set (FR-012, FR-018a/b).
- **I12 — Content exclusion.** Contains no test executables, no build scratch, no denylisted generated artifacts (FR-013).
- **I13 — Verified by enumeration.** I11 and I12 are checked by listing package contents, never by reading install rules (FR-018d).
- **I14 — Debug fidelity.** Debug artifacts yield usable symbolication; Release artifacts carry no debug information (SC-005). The mechanism differs by platform — on Linux the information lives inside the archive members; on Windows it lives in separate symbol files (FR-019).
- **I15 — Survives tree deletion.** Written outside every build tree (FR-021), because the build strategy deletes trees between configurations.

---

## E4 — Attribution Set

The files discharging the vendored dictionaries' upstream license obligations.

| File | Obligation discharged | Exists today? |
|---|---|---|
| Project license | Declares the package's own license (FR-018) | Yes — `LICENSE`, `LICENSE-COMMERCIAL.md` |
| Upstream license text | Reproduce notice + conditions + disclaimer in the distribution | Yes — `dictionaries/QUICKFIX_LICENSE.txt` |
| `NOTICE` | Carry the acknowledgment sentence in end-user documentation | **No — created by this feature** |

**Invariants**

- **I16 — Both obligations, not one.** The upstream license file *states* the acknowledgment requirement; it does not *satisfy* it. Shipping it alone leaves the second obligation undischarged (FR-018b).
- **I17 — Verbatim.** The acknowledgment sentence is reproduced exactly as the upstream license specifies.
- **I18 — Completeness is a gate.** A package missing dictionaries, upstream license, or `NOTICE` fails (SC-013). Partial attribution is a legal defect, not a cosmetic one.
- **I19 — No implied endorsement.** Package descriptions state third-party compatibility as fact and never imply endorsement or affiliation (FR-018c) — the upstream license forbids using its names to promote derived products.

---

## E5 — Staging Prefix

The transient location where install rules deposit files for packaging.

**Invariants**

- **I20 — Inside the build tree.** Located within the build directory so tree deletion removes it (FR-020).
- **I21 — Never a system location.** `CMAKE_INSTALL_PREFIX` must never point at a system path — this is what makes I20 hold, and its violation would silently pollute the host.

---

## E6 — Witness

A consumer program proving the export works. Two tiers, deliberately distinct.

| | Minimal | Real client |
|---|---|---|
| Basis | `fixpp::consumer::install-witness` (existing, extended) | `perf/fixpp_perf_driver.cpp` (existing, adapted) |
| Proves | `find_package` resolves; include paths arrive | The export is **sufficient to build a working FIX application** |
| Catches | Config-package regressions | An export that resolves but cannot **link** |
| Cost | Cheap — runs in every configuration | Heavier — at least one configuration |

**Invariants**

- **I22 — Header coverage.** The minimal witness includes both a hand-written public header and a generated per-version header (SC-002). They arrive via different install rules; a core-header-only witness goes green against a broken dict export.
- **I23 — No source-tree participation.** The real client names no path inside the fixpp source tree (SC-012).
- **I24 — Current-build provenance.** A witness must consume a package produced by the build under test and fail on provenance mismatch (FR-021a). This is live rather than theoretical: `artifacts/` outlives the deletion cycle, so older packages persist alongside current ones.
- **I25 — Link-interface exercise.** The real-client tier must actually link. A header-only consumer cannot detect a wrong link order (research R7) — the failure mode FR-010b exists to prevent.
