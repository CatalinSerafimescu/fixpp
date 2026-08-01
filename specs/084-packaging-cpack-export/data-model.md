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
| Members | `$<LINK_ONLY:>`-expanded transitive closure a real client links — **eleven MEASURED** at Gate A round 2 (research R2), plus **at least six DERIVED** at the 2026-08-01 sign-off (`contracts/export-set.md` §2a). The two are not the same kind of claim; see below |
| Excluded | `fixpp_builders_<ver>`, `fixpp_validators_<ver>` (FR-007) |

**Measured members** — `fixpp_core`, `fixpp_sync`, `fixpp_log`, `fixpp_wire`, `fixpp_dictionary`, `fixpp_tls`, `fixpp_transport`, `fixpp_session`, `fixpp_dict_dispatch_bridge`, `fixpp_dict_dispatch`, **`fixpp_otel`** — plus the `fixpp::fixpp` umbrella this feature creates. `fixpp_otel` is a **mandatory** member in every configuration, not a conditional one: `fixpp_session` links it PUBLIC (`src/session/CMakeLists.txt:55-56`) and a stub exists when `FIXPP_BUILD_OTEL=OFF` (`CMakeLists.txt:170`), so what varies is its **content**, never its membership. `fixpp_log_otlp` is **not** in the measured closure. The per-version `fixpp::dict::<ver>` INTERFACE targets are **proposed out** — research R2 records why (same install destination as the umbrella, so they add no post-install capability; `fixpp_dict_vt11` would export with denylisted, i.e. absent, content).

> **This is a measured result, and it is scoped.** Round 1 marked it a candidate because R2's method never expanded PRIVATE deps, never read `INTERFACE_INCLUDE_DIRECTORIES`, and never considered `FILE_SET`s. The executed `install(TARGETS … EXPORT …)` + generate run settled membership (`research/reviews/orchestrator_084-packaging-cpack-export_gate_a_r1_measurements.md`, M3–M5) and confirmed all three gaps — the reading missed three members across a **three-level** cascade. **⚠️ The eleven are the minimum for the closure that EXCLUDES the four then-open shipped-header subtrees.**

**Derived members added at the 2026-08-01 sign-off — NOT measured.** D1 = Option A and the FR-012a class = `export` bring in **at least six**: `fixpp_capi`, **`fixpp_capi_objects`** (forced — `fixpp_capi` links it PUBLIC and carries no sources or include dirs of its own, `src/capi/CMakeLists.txt:43-45`; it is an **OBJECT** library, so it needs an `OBJECTS DESTINATION`), `fixpp_config_toml`, **`fixpp_log_otlp`** (forced — `fixpp_config_toml` links it PRIVATE under a guard that is live in every OTel-ON configuration, `src/config/CMakeLists.txt:43-45`), `fixpp_tap`, `fixpp_service`. **These six come from *reading* `target_link_libraries`, which is the exact method the measurement caught being wrong in three places across a three-level cascade.** Re-running the generate experiment once they are wired is an implementation obligation, not an optional check — `contracts/export-set.md` §2a is authoritative for the list and the obligation.

**Invariants**

- **I1 — Closure.** No member may expose a link-interface dependency on a non-member (FR-008), where "link interface" means the **`$<LINK_ONLY:>`-expanded** one: for a STATIC library, `PRIVATE` link dependencies land in `INTERFACE_LINK_LIBRARIES` as `$<LINK_ONLY:…>` and are export requirements exactly like public ones (FR-008a). CMake enforces closure at **generate** time — which is why SC-007a's evidence is a recorded red *generate* run or a nested scratch configure, not a ctest assertion: a tree with a broken export set produces no build system for ctest to run in.
- **I2 — Denylist coherence.** No member's install interface may resolve to a location containing denylisted generated artifacts (FR-009, FR-010). The reference set is the **7 patterns** at `CMakeLists.txt:349-355`, asserted as set equality. A member with an **empty** install interface satisfies I2 by construction — this is the basis of `fixpp_dict_dispatch`'s disposition (research R2).
- **I3 — Configuration-dependence.** Membership no longer varies with FR-012a/D1 — those are decided (2026-08-01, all `export`) — but it still varies with **build options**: `fixpp_log_otlp` exists only under `if(TARGET opentelemetry-cpp::api)` (`src/log/CMakeLists.txt:38`), and each member's **interface** varies too. The generated config must reflect what was built, never a hardcoded list (research R2). *(Sharpened at Gate A round 2: the load-bearing case is `fixpp_otel`, whose SDK link edges are guarded by `if(TARGET opentelemetry-cpp::api)` — `src/otel/CMakeLists.txt:36` — **not** by `FIXPP_BUILD_OTEL`. Option state alone therefore does not determine the `find_dependency` set, which is what makes this invariant load-bearing rather than decorative.)*
- **I4 — Include interface.** Every member needs a build-tree/install-tree-discriminated include interface: `$<BUILD_INTERFACE:>` **and** `$<INSTALL_INTERFACE:>`. A member with neither is not a degraded export — `install(EXPORT)` rejects it at **generate** time.

  > **Correction — this invariant previously stated a false verified fact.** It read: *"The dict targets currently have only the former (`cmake/Codegen.cmake:543-544`)"*, whose only reading is that the **non-dict** members already carry `$<BUILD_INTERFACE:>`. **They carry neither.** Verified census: `grep -rn "BUILD_INTERFACE" src/` → **0 matches**; every module target declares `PUBLIC "${CMAKE_SOURCE_DIR}/include"` raw (`src/core/CMakeLists.txt:13` and twelve siblings, enumerated in research R11). The *dict* half of the original claim is correct and stands: `cmake/Codegen.cmake:543-544` gives each `fixpp::dict::<ver>` exactly `$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/_codegen/include>` and no install interface. Recorded as a visible repair rather than a reworded claim, because this bundle's method is "verified, not assumed" and a false verification must be seen to fail. Scope consequence: FR-002a, and `src/*/CMakeLists.txt` inside the feature's touched-file set.

- **I4a — File sets.** No member may carry an interface `FILE_SET` that is not installed; `install(TARGETS … EXPORT …)` is a hard error otherwise. `fixpp_transport` carries the repository's only one (`src/transport/CMakeLists.txt:53-60`), and its disposition is FR-002b / research R12.
- **I5 — Compile-definition fidelity.** `FIXPP_LOG_MIN_LEVEL` must reach consumers (public headers branch on it, and it is build-type-conditional). `FIXPP_BUILD_OTEL` must **not** — it reaches no public header, and adding it would create an ODR mismatch that currently cannot occur (research R4).

---

## E2 — Package Config

The generated files that make `find_package(fixpp)` succeed.

| Attribute | Value |
|---|---|
| Files | `fixppConfig.cmake`, `fixppConfigVersion.cmake`, `fixppTargets*.cmake` |
| Version source | `project(VERSION)` — currently `0.0.1` (`CMakeLists.txt:5`) |
| Dependencies resolved | **Derived** per FR-010c, not enumerated — **six**: OpenSSL, asio, pugixml, **Crc32c**, **opentelemetry-cpp**, **tomlplusplus**. *(tomlplusplus became unconditional at the 2026-08-01 sign-off, which exports `fixpp_config_toml`. opentelemetry-cpp was corrected from conditional at round 2: `fixpp_otel` is exported in every configuration and all six in-scope configurations are OTel-ON; it is absent only in the OTel-OFF build SC-015 exercises.)* |
| Dependencies *provided* | **None, by design — the package is provider-agnostic (FR-018e).** Every edge names an imported target; there are zero `find_library(…)` calls and zero `.conan2` paths under `src/`, `cmake/`, or the root `CMakeLists.txt`, so each `find_dependency` resolves against the **consumer's** `CMAKE_PREFIX_PATH`, from any provider. Two of the six (`Crc32c`, `opentelemetry-cpp`) are rarely distro-packaged and a consumer should expect to supply them |

**Invariants**

- **I6 — Transitive resolution.** Every third-party dependency the exported targets need is resolved without the consumer naming it (FR-004), where the set is produced by the FR-010c derivation rule over each member's `$<LINK_ONLY:>`-expanded interface — **PRIVATE deps included**, because a static library does not link them and the consumer's final link must resolve them. Omitting one is a **configure-time** failure inside `find_package(fixpp)`, not a link-time undefined symbol.
- **I6a — Resolution is not provision, but it IS provider-agnostic.** `find_dependency` locates a package; it does not install one — so I6 holds only where the six are findable. *(Rescoped at the 2026-08-01 sign-off: it does **not** follow that they are findable only under Conan. `src/` links only imported target names, so the exported graph names no provider, and each `find_dependency` is a plain `find_package` against the consumer's `CMAKE_PREFIX_PATH`.)* What survives from round 1 is the blindness: the existing witness inherits the producer's Conan toolchain (`tests/consumer/CMakeLists.txt:39-44`), so I6 can hold on the producing host without the provider-agnostic property ever being exercised. FR-018e carries the residual obligations — keep the generated config free of build-host paths, declare tested-against versions, and say which dependencies a consumer must supply — and **SC-016** is the witness that tests them.
- **I7 — No false dependencies.** The config must not require packages the library does not use. ZLIB is deliberately excluded (research R3) — a spurious requirement is as much a defect as a missing one. Symmetrically: a *derivation rule* rather than a hand-written list is what keeps this invariant and I6 from drifting apart, which is exactly what happened to FR-010c's original enumeration (it named compression and networking, which no target needs, and omitted Crc32c, which every consumer does).
- **I8 — Conditional telemetry — conditional on the SDK, not on the option, and unconditional in practice.** *(Restated at Gate A round 2.)* `fixpp_otel` is a **mandatory** export-set member in every configuration (E1), so the telemetry `find_dependency` is **not** avoided by the target being absent. It is required whenever the OTel SDK was present at configure — the guard is `if(TARGET opentelemetry-cpp::api)` (`src/otel/CMakeLists.txt:36`), **not** `if(FIXPP_BUILD_OTEL)` — which for this feature means **every shipped artifact**, since all six in-scope configurations are OTel-ON (Assumption 4), at a cost of 14 Conan packages at consumer configure. **The invariant holds by construction in the OTel-OFF case**: the stub is an empty INTERFACE library with no link edges (`CMakeLists.txt:167-170`), so it contributes none of the seven `opentelemetry-cpp::*` names to `fixppTargets.cmake`. SC-015 exercises exactly that case, and it is a real gate only while the config is derived from the built target graph (I3) rather than hardcoded.
- **I9 — Version compatibility.** An incompatible requested version fails at **configure** time with a version-specific diagnostic (FR-006, SC-006) — never at build or link.

---

## E3 — Package Artifact

One distributable file for one (platform, toolchain, configuration, format) tuple.

| Attribute | Source |
|---|---|
| Product / version | `project()` |
| Platform · toolchain · configuration | The preset being built |
| Format | DEB · RPM · TGZ (Linux) · ZIP (Windows) |
| Provenance | Configuration + source revision **+ worktree cleanliness or a build-input content hash** (FR-021a) |
| Dependency metadata | Per FR-018e: a **tested-against version per dependency**, read from `conanfile.py` and cited (`:66`–`:69`, `:77`, and `:94` for `opentelemetry-cpp/1.26.0`), with each marked ABI-stable / no-ABI-surface / ABI-fragile, plus an honest statement of which the consumer will likely have to provide |

**Invariants**

- **I10 — Name uniqueness.** Names encode product, version, platform, toolchain, configuration, and are unique across the matrix (FR-017).
- **I11 — Content sufficiency.** Contains public headers, exported libraries, config files, dictionaries, and the attribution set (FR-012, FR-018a/b).
- **I11a — Every shipped header has a disposition, and `OPEN` is not one.** No `include/<subtree>` ships without either a backing target in the export set or a recorded deliberate exclusion (FR-012a). The allowed values are exactly **`export`** and **`exclude`**; `OPEN` marks a routed, gate-blocking decision and **fails** SC-009a rather than satisfying it. *(Tightened at Gate A round 2: SC-009a previously asserted only "non-empty", which `OPEN` satisfies — so the gate could not fail on the defect it exists to catch. `contracts/package-layout.md` §2a now requires every `OPEN` row to name its routed decision and that decision's options; closing them is Gate A's job.)* **All seven `OPEN` rows were closed at the 2026-08-01 sign-off** — six `export`, one `exclude` (the two test-support subtrees, taken as one row). `contracts/package-layout.md` §2a, not this summary, remains authoritative (research R14). The invariant stays live: a subtree added later with no disposition still violates it.
- **I12 — Content exclusion.** Contains no test executables, no build scratch, no denylisted generated artifacts, **and no test-support headers** — `include/fixpp/core/test/` and `include/fixpp/transport/test/` are excluded per the FR-012a sign-off, which is the first `PATTERN` filter `CMakeLists.txt:321-324` will carry (FR-013). The exclusion reference is the **7-pattern set** at `CMakeLists.txt:349-355`, asserted as set equality — a check built from the 078 five-pattern tail would pass a package leaking `_dispatch/` or `vt11/`.
- **I13 — Verified by enumeration.** I11 and I12 are checked by listing package contents, never by reading install rules (FR-018d).
- **I14 — Debug fidelity.** Debug artifacts yield usable symbolication; Release artifacts carry no debug information (SC-005). The mechanism differs by platform — on Linux the information lives inside the archive members; on Windows it lives in separate symbol files (FR-019).
- **I15 — Survives tree deletion, and therefore accumulates.** Written outside every build tree (FR-021), because the build strategy deletes trees between configurations. **The accumulation is itself budgeted**: the artifact directory shares the 64 GB volume with the build tree and a 20 GB ccache, and FR-015's three redundant Linux formats multiply the ~4.6 GB payload across four Linux configurations. SC-008 measures the whole-volume high-water mark, and the feature must state a retention rule or place the directory on different storage (spec Assumption 5).

---

## E4 — Attribution Set

The files discharging the vendored dictionaries' upstream license obligations.

| File | Obligation discharged | Exists today? |
|---|---|---|
| Project license | Declares the package's own license (FR-018) | Yes — `LICENSE`, `LICENSE-COMMERCIAL.md` |
| Upstream license text | Upstream clauses **1 and 2** — retain/reproduce notice + conditions + disclaimer, in source and binary form | Yes — `dictionaries/QUICKFIX_LICENSE.txt` |
| `NOTICE` | Upstream clause **3** — the acknowledgment sentence in end-user documentation | **No — created by this feature** |
| *(product name `fixpp`)* | Upstream clause **5** — derived products may not be called "QuickFIX" nor carry it in their name | Satisfied by FR-017; recorded, not built |
| *(description wording)* | Upstream clause **4** — no endorsement/promotion using upstream names | FR-018c constrains DEB/RPM description fields |

> **Clause coverage corrected at Gate A round 1.** This model previously presented "two obligations, not one" as the complete enumeration, covering clauses 2 and 3. The license has **five** clauses. Clause 1 (source-form redistribution) *does* apply — the packages ship the dictionary XML, which is the redistributed material in source form — and is discharged by the same shipped license file. Clauses 4 and 5 are discharged by FR-018c and FR-017 respectively but were uncredited here. The outcome was already correct; the *model* claimed completeness it did not have.

**Invariants**

- **I16 — Two distinct acts, not one.** Shipping the upstream license file discharges clauses 1 and 2. Clause 3 asks for the acknowledgment sentence in end-user documentation — a `NOTICE` file is the conservative discharge this feature implements. *(Softened at Gate A round 1: clause 3 is conditional — "if any" — and offers an explicit alternative, "this acknowledgment may appear in the software itself, if and wherever such third-party acknowledgments normally appear". The earlier flat claim that shipping the license "does **not**" discharge it asserted one reading of a conditional clause as fact. The conservative implementation stands; the certainty does not. See spec Assumption 10.)*
- **I17 — Verbatim, against a pinned anchor.** The acknowledgment sentence's single source is `dictionaries/QUICKFIX_LICENSE.txt:19-20`. It spans two lines, sits indented inside clause 3, and is itself enclosed in quotation marks — so neither the `NOTICE` content nor SC-013's check may be written from memory, and the comparison must be whitespace-normalised against that anchor. A "verbatim" requirement with no pinned reference text is unfalsifiable, and this is the one obligation this bundle classifies as **legal** rather than cosmetic (I18).
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

A consumer program proving the export works. **Three** tiers, deliberately distinct — the third added at Gate A round 1 because the first two share a blind spot.

| | Minimal | Real client | Clean environment |
|---|---|---|---|
| Basis | `fixpp::consumer::install-witness` (existing, extended) | `perf/fixpp_perf_driver.cpp` (existing, adapted) | New (SC-016) |
| Proves | `find_package` resolves; the export **closure links**; both header kinds arrive | The export is **sufficient to build a working FIX application** | `find_package(fixpp)` resolves against dependencies the producing build's package manager did not provide |
| Catches | Config-package regressions; a missing `find_dependency`; the `FILE_SET` blocker | An export that resolves but cannot **link a real application**, and wrong link order | A config that bakes in a build-host path or a provider-specific assumption (FR-018e obligations 1 and 4) |
| Inherits from the producer | Conan toolchain (`tests/consumer/CMakeLists.txt:39-44`), build type | Conan toolchain, and lives in the source tree | **Nothing** |
| Cost | Cheap — runs in every configuration | Heavier — CI-gated on `linux-gcc-release` only (FR-026a, D3) | Cheap — one configuration |

> **Tier framing corrected at Gate A round 1.** The minimal tier was described as proving only that "`find_package` resolves; include paths arrive", with link-interface failures attributed exclusively to the real-client tier. That is no longer true: switching it from four hand-listed archives (`tests/consumer/CMakeLists.txt:56`) to `fixpp::fixpp` gives it the full export closure plus OpenSSL, asio and Crc32c, so it **does** exercise the link interface — just not against a live counterparty. And the two original tiers share a defect neither can see: both inherit the producing build's Conan toolchain, so neither can fail on the dependency-provisioning gap (research R13). Hence the third tier.

**Invariants**

- **I22 — Header coverage.** The minimal witness includes both a hand-written public header and a generated per-version header, reaching both through `fixpp::fixpp` alone (SC-002). They arrive via different install rules (`CMakeLists.txt:321` and `:346`), so this proves the generated headers were installed and are reachable. **What it cannot prove**: that a per-version `fixpp::dict::<ver>` target has a correct install interface — both rules write to the same `${CMAKE_INSTALL_INCLUDEDIR}` (`:323` == `:348`), so the include resolves through the umbrella either way. If those targets stay in the export set, a different check must carry that claim (research R2).
- **I23 — No source-tree participation.** The real client names no path inside the fixpp source tree (SC-012). **The "build with the source tree unavailable" form is impossible by construction** — the client and its `CMakeLists.txt` both live in that tree — so the invariant is held by the named equivalent check: configure from outside the tree with `CMAKE_PREFIX_PATH` set only to the staged prefix, then assert zero source-root paths in `compile_commands.json` and the link line.
- **I23a — Provider-agnostic resolution, proven on a prefix the producer did not fill.** At least one witness inherits nothing from the producing build — no `conan_toolchain.cmake`, no source tree, no producer-supplied `CMAKE_PREFIX_PATH` entry (SC-016). Without it, I6 and I23 hold while the provider-agnostic property is never exercised. *(Restated at the 2026-08-01 sign-off: the witness now has a **pass** state — `find_package(fixpp)` succeeds against dependencies the producer's package manager did not supply — and its **red** leg is explicit, because a gate never proven red proves nothing: remove one **named** dependency from that prefix and `find_package(fixpp)` must fail with that dependency's `find_dependency` diagnostic and no other. The round-2 "assert which failure" refinement survives as the red leg rather than as the expected outcome.)* The same run asserts FR-018e obligation 1: the **installed** `fixppConfig.cmake` / `fixppTargets*.cmake` carry no path under the build host's package-manager cache.
- **I24 — Current-build provenance.** A witness must consume a package produced by the build under test and fail on provenance mismatch (FR-021a). This is live rather than theoretical: `artifacts/` outlives the deletion cycle, so older packages persist alongside current ones. **Configuration + source revision alone is insufficient** — it cannot distinguish two packages built from the same commit either side of an uncommitted edit, which is the staleness the invariant exists to catch; provenance must additionally record worktree cleanliness or a build-input content hash.
- **I25 — Link-interface exercise.** A witness must actually link. `find_package` resolution alone cannot detect a wrong link order (research R7) — the failure mode FR-010b exists to prevent. Both the minimal and real-client tiers now satisfy this; only the real-client tier additionally *runs* the result against a live counterparty.
