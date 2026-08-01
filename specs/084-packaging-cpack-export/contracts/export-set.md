# Contract — `fixpp::` Export Set

**Feature**: 084-packaging-cpack-export · **Date**: 2026-07-31

The consumer-facing CMake contract. Evidence for every claim is in [`research.md`](../research.md) R1–R4, R7.

---

## 1. What a consumer writes

```cmake
find_package(fixpp REQUIRED)
target_link_libraries(my_app PRIVATE fixpp::fixpp)
```

That is the whole contract **for the core C++ surface**, *for a consumer whose environment can resolve fixpp's third-party dependencies* — from any provider. The consumer names **no** include directory, **no** library path, **no** third-party dependency of fixpp, and **no** link ordering.

> ### The umbrella is not the whole export set — the five sign-off members are OPT-IN, linked by name
>
> *(Added at the 2026-08-01 sign-off, because D1 = Option A exists to serve a consumer this section otherwise never mentions.)* `fixpp::fixpp` links `fixpp_session`, which transitively pulls the measured eleven. It does **not** reach `fixpp_capi`, `fixpp_config_toml`, `fixpp_tap`, `fixpp_service` or `fixpp_log_otlp` — `fixpp_session` links none of them. That is **correct, not a gap**: `src/config/CMakeLists.txt:5-6` records `fixpp_config_toml` as an *"OPT-IN target: no module in the core graph links this"*, and a C-ABI consumer deliberately does not want the C++ umbrella (Article IV §2 / `architecture.md:509`, where linking both fails the `tools/check_layers.py` lint). Those five are exported so they are **available by name**, not so the umbrella drags them in.
>
> ```cmake
> find_package(fixpp REQUIRED)
> target_link_libraries(c_abi_consumer  PRIVATE fixpp::capi)          # C ABI only — NOT fixpp::fixpp
> target_link_libraries(toml_consumer   PRIVATE fixpp::config_toml)   # the name the header itself names
> ```
>
> **⚠️ Implementation obligation — `EXPORT_NAME`, or the exported names contradict `architecture.md` §7.4.** `install(EXPORT … NAMESPACE fixpp::)` derives each imported name from the **target** name, and **`add_library(fixpp::X ALIAS …)` does not carry into an export**. Left alone, the export publishes `fixpp::fixpp_capi`, `fixpp::fixpp_config_toml`, `fixpp::fixpp_tap`, `fixpp::fixpp_service` — while `architecture.md:503` (the ground D1 Option A was decided on) says C-ABI consumers link **`fixpp::capi`**, and `include/fixpp/config/toml_config_loader.hpp:7-8` tells consumers to link **`fixpp::config_toml`**. `set_target_properties(<tgt> PROPERTIES EXPORT_NAME <short>)` is the mechanism that makes the installed names match the in-tree aliases; **`EXPORT_NAME` is set nowhere in the repository today** (census: `grep -rn EXPORT_NAME src/ cmake/ CMakeLists.txt` → 0). The same applies to every member with an alias — `fixpp::core`, `fixpp::session`, `fixpp::wire`, … (`src/core/CMakeLists.txt:10` and thirteen siblings). Pick one convention and apply it to the whole set; a half-applied one is worse than either.

> **Scoped at Gate A round 1, rescoped at sign-off — the precondition is "the dependencies are findable", not "Conan is present".** `find_dependency` **locates** a package; it does not **provide** one, so the clause holds only where the six dependencies in §4 are resolvable. What round 1 got wrong is *who* can make them resolvable: `src/` links **only imported target names**, with **zero** `find_library(…)` calls and **zero** `.conan2` paths anywhere under `src/`, `cmake/` or the root `CMakeLists.txt`. `install(EXPORT)` therefore writes target names, and each `find_dependency(X)` is a plain `find_package(X)` against the **consumer's** `CMAKE_PREFIX_PATH`. **The package is provider-agnostic by construction**; Conan is how fixpp is built, not how anyone must consume it. The residual obligations — keep the generated config free of build-host paths, declare tested-against versions, and say which dependencies a consumer will likely have to supply — are FR-018e's, and **SC-016** proves them against a prefix the producing build's package manager did not fill. The existing witness still cannot observe any of this, because it is handed the producer's Conan toolchain (`tests/consumer/CMakeLists.txt:39-44`).

---

## 2. Export set membership

> ### ✅ MEASURED at Gate A round 2 — with one scope qualifier and two open implementation items
>
> Round 1 marked this table PROVISIONAL. The prerequisite it named — an executed `install(TARGETS … EXPORT …)` + `cmake --preset linux-gcc-release` run — **was executed**, and the eleven members below are its output (research R2; `research/reviews/orchestrator_084-packaging-cpack-export_gate_a_r1_measurements.md`, M3–M5). Membership is no longer a reading.
>
> **⚠️ Scope qualifier — the eleven are the MEASURED floor, and the sign-off has since added more.** The measurement excluded the four then-open shipped-header subtrees (`fixpp_capi*`, `fixpp_config_toml`, `fixpp_tap`, `fixpp_service` — D1 / FR-012a). **All four are now `Export`** ([`package-layout.md` §2a](./package-layout.md), user 2026-08-01), and the members they bring are **derived by reading, not measured** — see §2a below. Quote the eleven only as the measured floor, never as the export set.
>
> **Two generate blockers remain — as implementation work, not as evidence gaps.** The missing include interfaces (research R11, FR-002a — a definite **nine** files at round 2, widened to **13 of 14** files / **14** targets by the 2026-08-01 sign-off) and `fixpp_transport`'s `FILE_SET` (research R12, FR-002b) are both confirmed hard errors by the same run. The run deliberately did **not** measure whether the export *succeeds* once they are fixed, nor which rewrite form to use — those are implementation choices, and nothing in this contract is waiting on a further Gate-A experiment.

**Exported** — the measured `$<LINK_ONLY:>`-expanded transitive closure a real FIX client links, **eleven members** plus the `fixpp::fixpp` umbrella this feature creates (research R2, FR-008a):

| Target | Rationale |
|---|---|
| `fixpp::fixpp` | Umbrella; links `fixpp_session`, which pulls the rest transitively |
| `fixpp_session` | Sessions — linked directly by the real client |
| `fixpp_transport` | Transport — linked directly by the real client. **Carries the repository's only `FILE_SET HEADERS`** (`src/transport/CMakeLists.txt:53-60`); its disposition is FR-002b |
| `fixpp_tls` | Secured transport — linked directly by the real client |
| `fixpp_wire` | Parser/encoder — reachable from the public parser header |
| `fixpp_dictionary` | Dictionaries and runtime loading. Links `fixpp_dict_dispatch_bridge` **PUBLIC** (`src/dictionary/CMakeLists.txt:97`) |
| `fixpp_core` | Base — every other member depends on it |
| `fixpp_sync` | Synchronisation symbols required by session/transport/tls |
| `fixpp_log` | Logging — **carries `FIXPP_LOG_MIN_LEVEL` as a PUBLIC compile definition** |
| `fixpp_dict_dispatch_bridge` | Reify dispatch bridge — **unconditional in every real build** (its only guard is the nested bootstrap sub-configure, `cmake/Codegen.cmake:205`) and reached by a PUBLIC edge from `fixpp_dictionary`. The existing witness already links it by name (`tests/consumer/CMakeLists.txt:56`) |
| `fixpp_dict_dispatch` | **Link-closure-only member with a deliberately EMPTY install interface.** Pulled in by the bridge's `PRIVATE fixpp::dict::dispatch` edge (`src/dictionary/CMakeLists.txt:89`), which for a STATIC library becomes a `$<LINK_ONLY:>` export requirement. Its build-tree include dir (`cmake/Codegen.cmake:589-590`) covers `_dispatch/`, which is install-EXCLUDED (`CMakeLists.txt:349`) — so it gets **no** `$<INSTALL_INTERFACE:>` at all, which is what makes B2 hold by construction rather than by luck. See research R2 for the rejected restructuring alternative |
| `fixpp_otel` | **Mandatory member — not conditional** *(corrected at Gate A round 2 by measurement; both round-1 reviews had it as "conditionally present" and neither identified it as a hard generate error)*. `fixpp_session` links it PUBLIC (`src/session/CMakeLists.txt:55-56`), so `install(EXPORT)` fails at generate without it. The target **always exists** — an empty INTERFACE stub when `FIXPP_BUILD_OTEL=OFF` (`CMakeLists.txt:170`). *(Precision added at sign-off: that PUBLIC edge is declared **inside** `if(FIXPP_BUILD_OTEL)` — `src/session/CMakeLists.txt:55-57` — so in an OTel-OFF tree nothing links `fixpp_otel` and it is not in the **closure**. Membership in every configuration is held instead by this feature exporting the target **unconditionally**, which is the form of the claim SC-015 actually exercises. What varies is its content, and — in the OTel-OFF case only — what would have reached it by closure.)* See §4 for the dependency consequence |

**Telemetry content, not telemetry membership.** `fixpp_otel` is always in the set; the OTel SDK link edges inside it are not. In an SDK-present build it is a **STATIC** library linking seven `opentelemetry-cpp::*` imported targets PUBLIC (`src/otel/CMakeLists.txt:24-45`); in an OTel-OFF build it is an empty INTERFACE library whose own comment reads *"no headers, no SDK symbols, no link edges"* (`CMakeLists.txt:167-169`). That is why the config file must be generated from what was built rather than from a fixed list (invariant I3) — and why **SC-015 holds by construction rather than by luck**: an OTel-OFF export of `fixpp_otel` contributes none of the seven names to `fixppTargets.cmake`, so no telemetry `find_dependency` is emitted and the config resolves. This is the identical argument the `fixpp_dict_dispatch` row above makes for its empty *install* interface; it is stated here so SC-015 is not left to be assumed. `fixpp_log_otlp` is a separate target and is **not** in the measured closure — but the sign-off's `fixpp_config_toml` export pulls it into the export set in every in-scope configuration; see §2a.

**Proposed OUT — the per-version `fixpp::dict::<ver>` INTERFACE targets** *(changed at Gate A round 1; previously exported as a class)*. Three verified grounds, in research R2: both install rules share the destination `${CMAKE_INSTALL_INCLUDEDIR}` (`CMakeLists.txt:323` and `:348`), so the installed generated headers are already reachable through the umbrella's single include root and these targets add **no post-install capability**; `fixpp_dict_vt11` (`cmake/Codegen.cmake:539`) would export with an install interface over `vt11/`, which is denylisted at `CMakeLists.txt:350` and therefore **absent**; and every extra member is additional closure risk. If they are kept, they MUST be enumerated by name — never exported as a class — `vt11` MUST be dispositioned explicitly, and a check other than SC-002 must carry the install-interface claim.

**Never exported** (FR-007, settled by 078 Gate B P1):

| Target | Why |
|---|---|
| `fixpp_builders_<ver>` | Install-scope coherence — an installed consumer would get unresolvable `build_` symbols |
| `fixpp_validators_<ver>` | Same, for `validate_` symbols |

---

## 2a. Members added by the sign-off — **DERIVED, NOT MEASURED**

> ### ⚠️ Read this before quoting the list below
>
> The eleven in §2 are the output of an **executed** `install(TARGETS … EXPORT …)` + generate run. The members here are the output of **reading** `target_link_libraries` — the exact method round 1 caught being wrong in three places across a three-level cascade, and blind to two mechanism-level blockers. The list is what the reading yields; it is **not** evidence that the export set is complete.
>
> **Implementation obligation (not optional, not a nice-to-have):** once these targets are wired into the export, **re-run the generate experiment** — the same discipline that produced the measured eleven and that caught RC-1 — and reconcile the result against this list before `tasks.md` is treated as closed. Each blocker masks the next, so a single run does not reveal the set; peel the errors one at a time as the round-1 measurement did (`research/reviews/orchestrator_084-packaging-cpack-export_gate_a_r1_measurements.md`, Method).

**At least six, from the D1 = Option A / FR-012a = export dispositions:**

| Target | Why it is a member | Kind / shape note |
|---|---|---|
| `fixpp_capi` | D1 Option A — the shipped `include/fix/` headers get their library (`src/capi/CMakeLists.txt:43`) | STATIC, **no sources and no include directories of its own** |
| `fixpp_capi_objects` | **Forced.** `fixpp_capi` is `target_link_libraries(fixpp_capi PUBLIC fixpp_capi_objects)` (`src/capi/CMakeLists.txt:45`), so it is in the link interface. Demoting the edge to `PRIVATE` does **not** escape it — `$<LINK_ONLY:>` entries are export requirements too (B1 / FR-008a), which is precisely how `fixpp_dict_dispatch` was dragged in | **OBJECT** (`:11`) — a materially **different install shape**: `install(TARGETS …)` on an OBJECT library requires an `OBJECTS DESTINATION`. Whether the export needs that destination or a wiring change is the first thing the re-measurement must answer |
| `fixpp_config_toml` | FR-012a — the `include/fixpp/config/` headers get their library (`src/config/CMakeLists.txt:13`) | STATIC. Adds **`tomlplusplus`** to §4 |
| `fixpp_log_otlp` | **Forced, and it contradicts a standing claim.** `fixpp_config_toml` links it `PRIVATE` under `if(TARGET fixpp::log_otlp)` (`src/config/CMakeLists.txt:43-45`); that target is created under `if(TARGET opentelemetry-cpp::api)` (`src/log/CMakeLists.txt:38-40`); `src/log` is processed before `src/config` (`CMakeLists.txt:163` vs `:174`); every in-scope configuration is OTel-ON. So in **every shipped configuration** the guard is live and the PRIVATE edge becomes a `$<LINK_ONLY:>` export requirement. §2's `fixpp_log` row and research R2 both say `fixpp_log_otlp` is "not in the measured closure" — **true of the measured closure, and no longer true of the export set** | STATIC. Its three additional `opentelemetry-cpp::*` imported targets (`src/log/CMakeLists.txt:49-51` — `logs`, `exporter_otlp_http_log`, `otlp_recordable`) add **no new `find_dependency`**: they are components of the same `opentelemetry-cpp` package §4 already requires |
| `fixpp_tap` | FR-012a directly, **and** as a side effect of D1 — `fixpp_capi_objects` links it PUBLIC (`src/capi/CMakeLists.txt:36`) | INTERFACE |
| `fixpp_service` | FR-012a, bound to D1: its only link edge is `fixpp_capi` (`src/service/CMakeLists.txt:15`), now a member | INTERFACE |

**Bounded by reading, unbounded in fact.** Each of the six was read out to its own PUBLIC/PRIVATE closure and none of them names a target outside this set — `fixpp_capi_objects`' PUBLIC list (`src/capi/CMakeLists.txt:28-41`) is core/session/wire/dictionary/transport/tls/log/tap plus `fixpp_otel`; `fixpp_config_toml`'s (`src/config/CMakeLists.txt:30-38`) is core/session/dictionary/tls/transport/log plus the two above; `fixpp_tap`'s and `fixpp_service`'s are single-level. **That is a reading, and this section exists because readings of exactly this shape have been wrong here before.** The measured cascade ran three levels; nothing guarantees this one does not.

---

## 3. Boundary rules

**B0 — Every member must have an include interface at all.** `$<BUILD_INTERFACE:>` for the build tree, `$<INSTALL_INTERFACE:>` for the installed prefix. **No module target has either today** — verified census: `grep -rn "BUILD_INTERFACE" src/` → 0 matches, with every module declaring `PUBLIC "${CMAKE_SOURCE_DIR}/include"` raw. `install(EXPORT)` rejects such a target at **generate** time, so this is a precondition of the whole contract, not a refinement of it. Research R11, FR-002a. *(Related: no member may carry an uninstalled interface `FILE_SET` — research R12, FR-002b.)*

**B1 — Closure, over the `$<LINK_ONLY:>`-expanded interface.** No exported target may expose a link-interface dependency on a non-exported target. For a STATIC library, `PRIVATE` link dependencies land in `INTERFACE_LINK_LIBRARIES` as `$<LINK_ONLY:…>` and count exactly like public ones (FR-008a) — a closure computed from the PUBLIC column alone is incomplete by construction. CMake enforces this at **generate** time, which is why SC-007a's evidence is a recorded red generate run or a nested scratch configure, not a ctest assertion: a tree with a broken export set produces no build system for ctest to run in.

**B2 — The install interface must not reach denylisted content.** `CMakeLists.txt:349-355` excludes **seven** patterns: `_dispatch`, `vt11`, `messages`, `groups`, `validators`, `all.hpp`, `groups.hpp`. Any `$<INSTALL_INTERFACE:>` added by this feature must resolve to the *installed* include directory, whose contents are already filtered. **A member with an empty install interface satisfies B2 by construction** — that is the basis of `fixpp_dict_dispatch`'s disposition in §2. *(The five-pattern 078 tail is a subset; every "must be absent" list in this feature is keyed to the full seven — FR-009.)*

**B3 — `Args` stay unexported (FR-010, verified).** The span-based typed-builder `Args` live under `messages/` and `groups/`. Two independent mechanisms keep them out (research R1): the denylist, and the fact that the dict targets carry no install interface at all — `cmake/Codegen.cmake:543-544` gives each `fixpp::dict::<ver>` only `$<BUILD_INTERFACE:>`. **Confirmed clean — no escalation.** *(Sharpened at Gate A round 2: for the per-version targets this is a current-state fact and they are proposed **out** of the export set anyway, so nothing changes it. For `fixpp_dict_dispatch` it is no longer merely a fact — that target is a **confirmed export-set member** (§2) whose only include directory covers the install-excluded `_dispatch/` tree, so keeping its `$<INSTALL_INTERFACE:>` **empty** is a standing design obligation of this feature, not an observation about the status quo. Giving it a non-empty install interface would trip the obligation below.)*

> **Standing obligation.** This confirmation holds only for the export set as designed here. Any future `$<INSTALL_INTERFACE:>` resolving somewhere other than the denylisted install tree re-opens FR-010, and the deferred "Option 3" `Args` decision must then be escalated before shipping.

**B4 — Compile definitions.** `FIXPP_LOG_MIN_LEVEL` **must** propagate (public headers branch on it; it is build-type-conditional — Debug `0`, Release `2`). `FIXPP_BUILD_OTEL` **must not** (it reaches no public header; adding it would create an ODR mismatch that currently cannot occur). Research R4.

**B5 — Link ordering is ours, not the consumer's.** `libfixpp_tls.a` references cryptography symbols and requires the fixpp archives to precede them. Exported target dependencies must let CMake derive this. Research R7, FR-010b.

> **Cause corrected at Gate A round 2 — the wrong first hypothesis was still here.** This clause previously read *"the hand-ordering in `perf/CMakeLists.txt:56-57` and `tests/interop` exists **only because those link raw archive paths**"*. Research R7 retracted exactly that sentence at round 1 as **wrong and misdirecting**; the correction landed in `research.md` and not here, and this contract is the clause an implementer reads when a consumer link fails — so it was handing them the retracted hypothesis under a citation of the section that retracts it. **The real cause**: `perf/CMakeLists.txt:58-61` links `fixpp_session` / `fixpp_transport` / `fixpp_tls` as **targets**; the raw paths at `:62-64` are the OpenSSL/ZLIB `find_library` results. Because the harness resolves OpenSSL by raw `find_library(... NO_DEFAULT_PATH)` rather than through the `OpenSSL::` imported targets, CMake has **no dependency edge** between the fixpp archives and libcrypto to order on. Exported targets carrying the imported-target dependency are what removes the need for hand-ordering.

> **A second, independent link-ordering hazard — recorded here because this is where an implementer will look for it.** The fixpp archives contain **two deliberate static-archive cycles**: `fixpp_wire` links `fixpp_dictionary` PUBLIC (`src/wire/CMakeLists.txt:27-30`) while `fixpp_dictionary` links `fixpp_core fixpp_wire` PUBLIC (`src/dictionary/CMakeLists.txt:46-49`), annotated at `:42-45` as *"a permitted static-archive cycle (CMake repeats the archives)"*; and dictionary → bridge → dictionary (`:88-90`, `:97`). The existing minimal witness hand-rolls `-Wl,--start-group` for precisely this (`tests/consumer/CMakeLists.txt:61-74`), and Assumption 7 / `quickstart.md` §2 propose deleting that hand-rolling in favour of `fixpp::fixpp`. **This is expected to be safe, not a known defect**: CMake repeats archives itself when ordering exported *targets*, and the witness's hand-rolling exists because it passes **raw archive paths** (`:55-58`) that carry no dependency graph to repeat. The reasoning is recorded rather than assumed, so a link failure after the switch is diagnosed against a stated expectation instead of a blank.

---

## 4. Third-party dependency resolution

The set is **derived**, not enumerated (FR-010c): for every export-set member, every imported target in its `$<LINK_ONLY:>`-expanded link interface contributes its `find_package` name. `fixppConfig.cmake` resolves them so consumers need not name them:

| Dependency | Why | Always? |
|---|---|---|
| OpenSSL | `fixpp_transport` PUBLIC (`src/transport/CMakeLists.txt:48-49`); `fixpp_tls` PRIVATE | Yes |
| asio | `fixpp_session`, `fixpp_tls` PUBLIC | Yes |
| pugixml | `fixpp_dictionary` PRIVATE — static libs don't link their private deps, so the consumer's final link must resolve it | Yes |
| **Crc32c** | `fixpp_session` PRIVATE (`src/session/CMakeLists.txt:91`) — **same mechanism as pugixml**; omitted from this table until Gate A round 1 | Yes |
| **opentelemetry-cpp** | `fixpp_otel` PUBLIC — **seven** imported targets (`src/otel/CMakeLists.txt:24-45`), and `fixpp_otel` is a **mandatory** export-set member (§2) | **Yes, for every artifact this feature ships** — all six in-scope configurations are OTel-ON (Assumption 4). Absent only in the OTel-OFF build SC-015 exercises. **Keyed on `if(TARGET opentelemetry-cpp::api)` (`:36`), not on `FIXPP_BUILD_OTEL`** — so this leg must be derived from the configured target graph (I3), never from option state |
| **tomlplusplus** | `fixpp_config_toml` PRIVATE (`src/config/CMakeLists.txt:37`) | **Yes** — the FR-012a sign-off exports `fixpp_config_toml` (`package-layout.md` §2a, 2026-08-01) |

**Six dependencies, not five.** All six are resolved by **plain `find_package` against the consumer's `CMAKE_PREFIX_PATH`** — nothing in the exported graph is provider-specific. Census: **zero** `find_library(…)` calls and **zero** `.conan2` paths under `src/`, `cmake/` or the root `CMakeLists.txt`; every link edge names an imported target. Tested-against versions and the per-dependency ABI/availability characterisation are in [`package-layout.md` §4](./package-layout.md) (FR-018e).

> **The failure mode is configure-time, not link-time.** The generated `fixppTargets.cmake` names e.g. `Crc32c::crc32c` in an `INTERFACE_LINK_LIBRARIES` property; without the matching `find_dependency`, `find_package(fixpp)` itself errors with *"the target was not found … A find_package call is missing for an IMPORTED target"*. It fires for every consumer, including the minimal witness — so an omission here is loud, not silent. What is *silent* is the R13 gap it was mistaken for: every existing witness is handed the producer's toolchain, so none can fail on a dependency the consumer would have to supply. **SC-016** is what removes that blindness.
>
> **Weight this carries into FR-018e.** Because `fixpp_otel` is exported in every configuration and every in-scope configuration is OTel-ON, **every package this feature produces requires `opentelemetry-cpp` at consumer configure time** — the largest surface in the graph (seven imported targets from `fixpp_otel`, plus three more from `fixpp_log_otlp`; 14 Conan packages in the producing build, measurement M1) and the one FR-018e classifies as ABI-fragile *and* rarely distro-packaged. It is the dependency a consumer is most likely to have to build themselves, and the one where a version mismatch against the tested `1.26.0` is most likely to bite. That belongs on the table when a consumer plans an integration, not discovered afterwards.

**Deliberately NOT resolved: ZLIB.** No fixpp target links it (research R3). It appears in `tests/transport` and `perf` only because those bypass the imported targets with raw `find_library(... NO_DEFAULT_PATH)`. A consumer using the imported targets gets any compression dependency transitively. **A spurious requirement is as much a packaging defect as a missing one** — the real-client witness links for real, so a genuine need surfaces as a link failure rather than being masked.

---

## 5. Version compatibility

Version comes from `project(VERSION)` — `0.0.1` today (`CMakeLists.txt:5`), never a packaging-local literal (FR-005). A later bump propagates with no packaging change.

An incompatible request fails at **configure** time with a version-specific diagnostic (FR-006, SC-006). Failing later — at build or link — would be a defect.

---

## 6. What this contract does not promise

- **No shared libraries of the core C++ targets.** Every compiled C++ library target is STATIC by design. *(Basis re-derived at Gate A round 1 — the earlier "REMAINING-WORK A-1 deliberately holds the `0→1` freeze" is false; that freeze is **CLOSED**, GA-frozen at `1.5.0`, `REMAINING-WORK.md:7`, and in any case governs the **C** ABI, not these C++ targets.)* The surviving ground: the C ABI has freeze machinery — a version script restricting exports to `fixpp_*` per `[const §X.2]` (`src/capi/CMakeLists.txt:73`), a header-hash baseline, a symbol golden — and the core C++ targets have **none of it**. Shipping them shared would create a de-facto binary-compatibility surface with no freeze artifact and no gate. That is an ABI commitment made by accident, which a packaging feature must not do.
- **No typed-builder API.** `build_<Msg>` / `validate_<Msg>` are unavailable to installed consumers (B3, FR-007).
- **No C ABI *surface* changes.** `include/fix/c_api*` is untouched by this feature — no declaration and no exported symbol changes. **D1 is decided (Option A, 2026-08-01): the static `fixpp_capi` IS packaged**, which does not modify the frozen surface — it is a packaging decision governed by the existing freeze controls, not an ABI-commitment decision. **No shared C-ABI library is packaged**: `fixpp_capi_shared` is gated on `FIXPP_BUILD_TESTS` (`src/capi/CMakeLists.txt:47-48`) and does not exist in a packaging build with tests off. Recorded as a known limitation, not a silent omission — the dynamic-isolation consumer of Article IV §2 remains unserved, and the `WINDOWS_EXPORT_ALL_SYMBOLS ON` symbol-surface hazard (`:70`) attaches only to that unexported variant.
- **No dependency provisioning** — see the §1 note and FR-018e. This contract makes the package *resolvable from any provider*; it does not ship, vendor, or install the six dependencies themselves. Two of them (`Crc32c`, `opentelemetry-cpp`) are rarely available from a platform package manager and a consumer should expect to supply them.
- **No cross-configuration mixing.** A consumer built in a different configuration must resolve correctly or fail with a clear diagnostic — never with an undefined symbol at link time (spec User Story 1, scenario 3).
