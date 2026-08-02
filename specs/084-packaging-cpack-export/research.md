# Phase 0 Research — 084-packaging-cpack-export

**Date**: 2026-07-31 · **Branch**: `084-packaging-cpack-export`

Every finding below was verified against source in this worktree on the date above. Citations are `file:line` at that revision. This document is the evidence base for `plan.md`; where it corrects something asserted in `spec.md`, that is called out explicitly.

---

## R1 — FR-010: does the export set reach the typed-builder `Args` trees? **NO. Confirmed clean.**

This is the spec's escalation trigger, so it is resolved first.

**Two independent mechanisms both prevent it:**

1. **The install exclusion set.** `CMakeLists.txt:349-355` excludes **seven** patterns from the generated-header install: `_dispatch`, `vt11`, `messages`, `groups`, `validators`, `all.hpp`, `groups.hpp`. `Args` live under `messages/` and `groups/` (`:351-352`). *(Corrected at Gate A round 1: this previously cited only the five-pattern 078 tail. `_dispatch/` — the build-tree-private reify bridge — and `vt11/` — FIXT.1.1, outside the public v42/v44/v50sp2 set — are excluded on independent grounds and are part of the same block. Every downstream "must be absent" list is keyed to the full seven; see FR-009.)*
2. **The dict targets carry a build-tree-only include path.** `cmake/Codegen.cmake:543-544` gives each `fixpp::dict::<ver>` INTERFACE target exactly one include directory, `$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/_codegen/include>` — **no `$<INSTALL_INTERFACE:>` at all**. An exported target therefore cannot point a consumer at anything, let alone at `Args`, until this feature adds an install interface. When it does, that interface resolves to the *installed* include directory, whose contents are already filtered by mechanism 1. *(Note the scope of this claim, narrowed at Gate A round 1: the dict targets have `$<BUILD_INTERFACE:>` and not `$<INSTALL_INTERFACE:>`. The **module** targets have **neither** — see R11.)*

**What actually survives the denylist** (measured on `build/linux-clang-debug/_codegen/include/fixpp/v44/`): `Fields.hpp` (1.9 MB), `Messages.hpp` (1.9 MB), `Reify.hpp` (1.9 MB), `Validator.hpp` (920 KB), `NormativeReferences.md`. Excluded: `messages/`, `groups/`, `validators/`.

**Decision**: FR-010 is discharged as a confirmation. The deferred "Option 3" `Args` representation change (SC-001 / L-078-1) remains untouched and unforeclosed. **No escalation required.**

**Standing obligation**: the confirmation is only valid for the export set as designed here. If a later change adds an `$<INSTALL_INTERFACE:>` that resolves anywhere other than the denylisted install tree, FR-010 must be re-verified. *(Sharpened at Gate A round 2, and swept to `export-set.md` B3: mechanism 2 is a **current-state fact** for the per-version `fixpp::dict::<ver>` targets, which R2 **decided, excluded** from the export set — but a **standing design obligation** for `fixpp_dict_dispatch`, which the measurement confirmed **is** a member and whose only include directory covers the install-excluded `_dispatch/` tree. Its `$<INSTALL_INTERFACE:>` must be kept **empty**, which is exactly what makes B2/I2 hold by construction.)*

**Incidental finding**: `NormativeReferences.md` is installed into the public include tree — a Markdown file shipping as a header. Cosmetic, but it lands in every package. Cheap to exclude; recorded as a candidate, not a requirement.

---

## R2 — The export set is a transitive closure, and it is configuration-dependent

> ### ✅ STATUS AT GATE A ROUND 2 — **membership MEASURED. The round-1 blocking prerequisite is DISCHARGED.**
>
> Round 1 marked this section **PROVISIONAL** because it was derived by **reading** each target's `target_link_libraries` and stopping there, and named a blocking prerequisite: write the real `install(TARGETS … EXPORT fixppTargets)` + `install(EXPORT …)` and run a generate. **That run was executed** by the orchestrator between rounds 1 and 2, against a real configured `linux-gcc-release` tree, with the scratch export reverted afterwards. Evidence, quoted verbatim from the run: `research/reviews/orchestrator_084-packaging-cpack-export_gate_a_r1_measurements.md` (M1–M6).
>
> **The reading was wrong in three places and blind to two mechanisms**, exactly as round 1 predicted:
>
> 1. **`INTERFACE_INCLUDE_DIRECTORIES`** — never inspected by the reading. Confirmed as a hard generate error for **all eight** module targets that reached it (M4); `grep -rn BUILD_INTERFACE src/` → 0 matches (R11, FR-002a).
> 2. **`FILE_SET`s** — never considered. `fixpp_transport`'s is the **first** error the export produces (M3), ahead of every closure error (R12, FR-002b).
> 3. **`$<LINK_ONLY:>` expansion of PRIVATE deps** — never performed. The closure ran **three levels deep** (M5): `fixpp_dictionary` → `fixpp_dict_dispatch_bridge` → `fixpp_dict_dispatch`, plus `fixpp_session` → `fixpp_otel`. Each blocker masks the next, so a single run does not reveal the set (FR-008a).
>
> **What the run settled, and what it did not.** It settled **membership** for the currently-decided scope. It did **not** settle the **include-interface rewrite form** (per-module vs centralised) or the **`FILE_SET` disposition** — the measurement says so in as many words: *"Not measured: whether the export succeeds once all three blockers are fixed. That needs the `src/*/CMakeLists.txt` include-interface edits, which are implementation, not a Gate-A experiment."* Those two remain open **implementation** work (FR-002a, FR-002b) and must not be described as awaiting a generate run — the run has happened.
>
> **⚠️ SCOPE QUALIFIER — carry it wherever this set is quoted.** The eleven members below are the minimum for the closure that **excludes** the four then-open shipped-header subtrees: `fixpp_capi*`, `fixpp_config_toml`, `fixpp_tap`, `fixpp_service` (D1 / FR-012a, R14). Those were **not measured**. **All four are now `export`** (Gate A sign-off, user 2026-08-01), and the members they bring — at least six, including the forced `fixpp_capi_objects` and `fixpp_log_otlp` — are **derived by reading, not measured**: `contracts/export-set.md` §2a names them and carries the standing obligation to **re-run the generate experiment** once they are wired. Quoting the eleven flat, without this qualifier, would recreate the round-1 defect at one remove: a scoped result read as a complete one.

**Measured minimum export set** (executed, `linux-gcc-release`, OTel-ON — M5):

```
fixpp_core · fixpp_sync · fixpp_log · fixpp_wire · fixpp_dictionary
fixpp_tls · fixpp_transport · fixpp_session
fixpp_dict_dispatch_bridge · fixpp_dict_dispatch · fixpp_otel
```

`fixpp_log_otlp` is **not** among the measured eleven — that closure never requires it. **The sign-off answers the question the measurement left open, and the answer is yes**: exporting `fixpp_config_toml` (FR-012a, 2026-08-01) drags `fixpp_log_otlp` in, because `src/config/CMakeLists.txt:43-45` links it `PRIVATE` under `if(TARGET fixpp::log_otlp)`, that target is created under `if(TARGET opentelemetry-cpp::api)` (`src/log/CMakeLists.txt:38-40`), `src/log` is processed before `src/config` (`CMakeLists.txt:163` vs `:174`), and every in-scope configuration is OTel-ON. A PRIVATE dep of a STATIC library is a `$<LINK_ONLY:>` export requirement (FR-008a). This is **derived, not measured** — `contracts/export-set.md` §2a.

The per-target link declarations below remain the explanation of *why* each member is there; the membership itself is now the measurement's, not the reading's.

| Target | Kind | PUBLIC link interface | PRIVATE |
|---|---|---|---|
| `fixpp_core` | STATIC | *(none declared)* | — |
| `fixpp_log` | STATIC | `fixpp_core` · **`FIXPP_LOG_MIN_LEVEL` (PUBLIC compile definition)** | — |
| `fixpp_sync` | STATIC | *(atomic_shared_ptr fallback symbols)* | — |
| `fixpp_wire` | STATIC | `fixpp_core`, `fixpp_dictionary` | — |
| `fixpp_dictionary` | STATIC | `fixpp_core`, `fixpp_wire`, **`fixpp_dict_dispatch_bridge`** (`src/dictionary/CMakeLists.txt:97` — PUBLIC, and **not** conditional; see below) | `pugixml::pugixml` |
| `fixpp_tls` | STATIC | `fixpp_core`, `fixpp_sync`, `asio::asio` | `OpenSSL::Crypto` |
| `fixpp_transport` | STATIC | `fixpp_core`, `fixpp_sync`, `fixpp_tls`, `fixpp_log`, `OpenSSL::SSL`, `OpenSSL::Crypto` | — |
| `fixpp_session` | STATIC | `fixpp_core`, `fixpp_sync`, `fixpp_dictionary`, `fixpp_wire`, `fixpp_transport`, `fixpp_log`, `asio::asio` (+ **`fixpp_otel`**, `src/session/CMakeLists.txt:55-56` — declared under `if(FIXPP_BUILD_OTEL)`, but a stub `fixpp_otel` exists when the option is OFF (`CMakeLists.txt:170`), and **all six in-scope configurations are OTel-ON** (Assumption 4), so it is a **mandatory** closure member for every artifact this feature ships — confirmed as a hard generate error at M5, not a conditional) | **`Crc32c::crc32c`** (`src/session/CMakeLists.txt:91`) |
| `fixpp_dict_dispatch_bridge` | STATIC | *(none)* | `fixpp::dict::dispatch`, `fixpp_wire`, `fixpp_dictionary` (`src/dictionary/CMakeLists.txt:88-90`) |

**The eight targets above are the *reading-derived* first level** — `fixpp_core`, `fixpp_sync`, `fixpp_log`, `fixpp_wire`, `fixpp_dictionary`, `fixpp_tls`, `fixpp_transport`, `fixpp_session`. The measurement added **three** the reading missed (`fixpp_dict_dispatch_bridge`, `fixpp_dict_dispatch`, `fixpp_otel`); the measured eleven at the top of this section are the operative list.

**`fixpp_dict_dispatch_bridge` is a member, and it is NOT conditional.** *(Corrected at Gate A round 1 — this section previously listed it under "conditionally present", which was an evidence defect, not a wording one.)* `src/dictionary/CMakeLists.txt:79-81` creates it and `:97` does `target_link_libraries(fixpp_dictionary PUBLIC fixpp_dict_dispatch_bridge)` — a **PUBLIC** edge, so it is unavoidably in the link closure. Its only guard is `if(NOT FIXPP_CODEGEN_BOOTSTRAP_RUNNING)` (`:72`), and that variable is set in exactly one place: the nested bootstrap sub-configure at `cmake/Codegen.cmake:205`. **In every real build the bridge exists unconditionally.** Independent confirmation: the existing consumer witness already links it by name (`tests/consumer/CMakeLists.txt:56`).

**Disposition — the bridge drags `fixpp_dict_dispatch` in, and that pair needs an explicit answer.** The bridge links `PRIVATE fixpp::dict::dispatch fixpp_wire fixpp_dictionary` (`src/dictionary/CMakeLists.txt:88-90`). Per FR-008a those PRIVATE deps become `$<LINK_ONLY:…>` export requirements, so **`fixpp_dict_dispatch` must join the export set** — confirmed by execution at M5, as the *third* level of the cascade, visible only after the bridge itself was added. But its sole include directory is `$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/_codegen/include>` (`cmake/Codegen.cmake:589-590`), over a tree whose `_dispatch/` subdirectory is install-EXCLUDED (`CMakeLists.txt:349`) — so it cannot be given an install interface that resolves to installed content.

> **Chosen disposition**: export both, with `fixpp_dict_dispatch` carrying **no `$<INSTALL_INTERFACE:>` at all** and documented as a **link-closure-only member**. It is an INTERFACE target with no compiled output; its export-set membership exists solely to satisfy CMake's closure rule for the bridge's `$<LINK_ONLY:>` edge. An **empty** install interface cannot resolve to denylisted content, so invariant I2 / boundary rule B2 hold by construction rather than by luck — which is the property that makes this coherent rather than a papering-over. The membership and the empty interface must both be asserted, so a future change that gives it a non-empty install interface trips FR-010's standing obligation.
>
> **Alternative rejected**: restructuring so the bridge's build-tree-only dependency does not escape into its interface (e.g. absorbing the dispatch include directory directly onto the bridge and dropping the `fixpp::dict::dispatch` link edge). It is cleaner, but it edits the reify-bridge wiring that `src/dictionary/CMakeLists.txt:85-87` explicitly warns must stay narrow, for no packaging benefit over the empty-interface disposition. Recorded so the choice is visible, not silent. **This is a disposition, not a routed decision** — it does not go to Gate A.

**`fixpp_otel` is a mandatory member, not a conditional one** *(corrected at Gate A round 2 by measurement M5; both round-1 reviews had it as "conditionally present", and neither identified it as a hard generate error).* `src/session/CMakeLists.txt:55-56` links it PUBLIC from `fixpp_session`, so the export fails at generate without it. The target **always exists**: with `FIXPP_BUILD_OTEL=OFF` an empty INTERFACE stub is created instead (`CMakeLists.txt:170`), whose own comment at `:167-169` reads *"no headers, no SDK symbols, no link edges"*. What varies is its **content**, never its presence.

> **Precision on "membership", added at the 2026-08-01 sign-off — the conclusion is unchanged, its ground is narrower.** The PUBLIC edge is declared **inside** `if(FIXPP_BUILD_OTEL)` (`src/session/CMakeLists.txt:55-57`), so in an OTel-OFF tree nothing links `fixpp_otel` and it is **not in the closure**. M5 measured an OTel-**ON** tree, so it measured the guard live. Membership in *every* configuration is therefore held by this feature exporting `fixpp_otel` **unconditionally**, not by the closure — which is the form SC-015 actually exercises, and which leaves every downstream consequence (the `opentelemetry-cpp` `find_dependency` for every shipped artifact; SC-015 holding by the stub's absence of link edges) exactly as stated. Restatements of "mandatory closure member" elsewhere in the bundle are correct within their OTel-ON framing; this is the precise form.

`fixpp_log_otlp` is a genuinely separate target and is **not** in the measured closure — but see the sign-off note above the table: exporting `fixpp_config_toml` brings it into the **export set**.

> **Consequence for the dependency set — `opentelemetry-cpp` becomes effectively unconditional for every artifact this feature ships.** In an OTel-ON build `fixpp_otel` is a **STATIC** library linking **seven** `opentelemetry-cpp::*` imported targets PUBLIC (`src/otel/CMakeLists.txt:24-45`). Exporting it writes all seven names into the generated `fixppTargets.cmake` `INTERFACE_LINK_LIBRARIES`, so a consumer without `find_dependency(opentelemetry-cpp)` gets a **configure-time hard error inside `find_package(fixpp)`** — the same mechanism R3 documents for `Crc32c`. Since Assumption 4 makes **all six in-scope configurations OTel-ON**, every package this feature produces requires the heaviest dependency in the graph (14 Conan packages, M1) at consumer configure. That raises the stakes of the FR-018e decision and of R13's "resolvable is not installable"; it is stated here rather than left to be discovered.
>
> **The obligation is keyed to SDK presence, not to the option.** The guard is `if(TARGET opentelemetry-cpp::api)` (`src/otel/CMakeLists.txt:36`), not `if(FIXPP_BUILD_OTEL)`. This is why invariant I3 — *the generated config must reflect what was built, never a hardcoded list* — is load-bearing here rather than decorative: option state alone does not determine the `find_dependency` set.
>
> **SC-015 survives, and it survives by construction rather than by luck — which this section previously left unsaid.** In an OTel-OFF build `fixpp_otel` is `add_library(fixpp_otel INTERFACE)` (`CMakeLists.txt:170`) with **no link edges at all**, so exporting it contributes **no** `opentelemetry-cpp::*` names to `fixppTargets.cmake` and generates no telemetry `find_dependency`. This is the identical argument made for `fixpp_dict_dispatch`'s empty install interface below, and it must be made explicitly for `fixpp_otel` too — otherwise a reader is left to *assume* SC-015 works.

**Decision (membership settled by measurement; see the round-2 banner)**: export the measured eleven — the reading-derived closure above plus `fixpp_dict_dispatch_bridge`, `fixpp_dict_dispatch` and `fixpp_otel` — **plus the at-least-six the 2026-08-01 sign-off adds** (`fixpp_capi`, `fixpp_capi_objects`, `fixpp_config_toml`, `fixpp_log_otlp`, `fixpp_tap`, `fixpp_service`; `contracts/export-set.md` §2a). The umbrella `fixpp::fixpp` links `fixpp_session` (which transitively pulls the measured rest). **The export set is still not a fixed list**: the six additions are derived rather than measured, and the OTel content varies with build options — so the generated config must be produced from what was actually built rather than from a hardcoded enumeration (I3), and the generate experiment must be re-run once the additions are wired.

**Per-version `fixpp::dict::<ver>` targets — DECIDED: excluded from the export set.** *(Decided at Gate A round 1; not one of the three decisions routed to Gate A round 3 for user sign-off — D1/FR-018e/D3 — because these three verified facts settle it without requiring a user tradeoff.)* They were previously included by default. Three verified facts argue against:

- **They add no post-install capability.** Both install rules write to the **same** destination — `CMakeLists.txt:323` and `:348` are both `DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"` — so once the umbrella carries `$<INSTALL_INTERFACE:include>`, `#include <fixpp/v44/Fields.hpp>` resolves through the umbrella whether or not `fixpp::dict::v44` has any install interface at all.
- **One of them would export with zero installed content.** `cmake/Codegen.cmake:539` creates `fixpp_dict_vt11` alongside v42/v44/v50sp2, and `vt11/` is excluded at `CMakeLists.txt:350`. Exporting "the per-version dict targets" *as a class* therefore exports a member whose install interface could only ever be empty or wrong.
- **They add closure risk.** Every extra member is another target `install(EXPORT)` must resolve.

**Standing re-open trigger, not a live branch**: if a *future* change adds any of these targets to the export set, it MUST enumerate them explicitly (never export as a class), disposition `vt11` by name, and provide a check other than SC-002 for "the per-version install interface is correct" — SC-002 structurally cannot carry that claim, for the same-destination reason above. This is a condition on a later change, not an open choice this feature leaves for the implementer.

**Alternatives rejected**: a headers-only INTERFACE umbrella — provably insufficient, since the real client links three STATIC targets directly (settled as FR-010a). Exporting only `fixpp_session` without its dependencies — CMake rejects an export set whose members reference non-exported targets (FR-008).

---

## R3 — `find_dependency` set: **OpenSSL, asio, pugixml, Crc32c, opentelemetry-cpp, tomlplusplus.** ZLIB is NOT required.

*(Heading corrected at Gate A round 2: opentelemetry-cpp read "conditional". `fixpp_otel` is exported in every configuration and every in-scope configuration is OTel-ON, so it is required by every artifact this feature ships. Corrected again at the 2026-08-01 sign-off: **tomlplusplus is now unconditional** — the FR-012a disposition exports `fixpp_config_toml`. The set is **six**.)*

> **The set is provider-agnostic, and that is a source fact, not an aspiration** *(added at the sign-off with FR-018e)*. Every one of the six is reached through an **imported target name** — `OpenSSL::SSL`/`OpenSSL::Crypto`, `asio::asio`, `pugixml::pugixml`, `Crc32c::crc32c`, `tomlplusplus::tomlplusplus`, `opentelemetry-cpp::*`. Census across `src/`, `cmake/` and the root `CMakeLists.txt`: **zero** `find_library(…)` calls, **zero** `.conan2` paths. So `install(EXPORT)` writes target names and each `find_dependency(X)` is a plain `find_package(X)` against the **consumer's** `CMAKE_PREFIX_PATH`. R13's "the package is Conan-only" conclusion inferred a consumption constraint from a build convention and is retracted below.

**Derived, not enumerated** *(method corrected at Gate A round 1)*. The set is produced by the FR-010c rule: for every export-set member, every imported target in its `$<LINK_ONLY:>`-expanded link interface contributes its `find_package` package name. PRIVATE deps count — a static library does not link them, so the consumer's final link must resolve their symbols. Applying the rule to R2's **measured** closure:

| Package | Contributed by | Declaration | Conan pin |
|---|---|---|---|
| OpenSSL | `fixpp_transport` PUBLIC (`src/transport/CMakeLists.txt:48-49`); `fixpp_tls` PRIVATE | `find_package(OpenSSL 3.0 QUIET)` — `CMakeLists.txt:47` | `openssl/3.6.2` |
| asio | `fixpp_session`, `fixpp_tls` PUBLIC | `find_package(asio CONFIG QUIET)` — `CMakeLists.txt:38` | `asio/1.38.0` |
| pugixml | `fixpp_dictionary` PRIVATE | `find_package(pugixml CONFIG REQUIRED)` — `src/dictionary/CMakeLists.txt:9` | `pugixml/1.15` |
| **Crc32c** | **`fixpp_session` PRIVATE** — `target_link_libraries(fixpp_session PRIVATE Crc32c::crc32c)`, `src/session/CMakeLists.txt:91` | `find_package(Crc32c CONFIG QUIET)` — `CMakeLists.txt:43` | `crc32c/1.1.2` |
| **opentelemetry-cpp** | **`fixpp_otel` PUBLIC** — seven imported targets (`src/otel/CMakeLists.txt:24-45`), and `fixpp_otel` is a **measured mandatory** closure member (R2/M5) | `find_package(opentelemetry-cpp CONFIG QUIET)` — `CMakeLists.txt:52` | `opentelemetry-cpp/1.26.0`. **Unconditional for every artifact this feature ships** — all six in-scope configurations are OTel-ON (Assumption 4). Absent only in the OTel-OFF build SC-015 exercises, where the stub carries no link edges (`CMakeLists.txt:167-170`) and so contributes none of the seven names. **Keyed on `if(TARGET opentelemetry-cpp::api)` (`src/otel/CMakeLists.txt:36`), not on the option** — so the set must be derived from the configured target graph (I3), not from option state |
| **tomlplusplus** | **`fixpp_config_toml` PRIVATE** (`src/config/CMakeLists.txt:37`) | `find_package(tomlplusplus CONFIG REQUIRED)` — `src/config/CMakeLists.txt:11` | `tomlplusplus/3.4.0` (`conanfile.py:77`) — **unconditional as of the 2026-08-01 sign-off**, which exports `fixpp_config_toml` |

**`Crc32c` was omitted from this section's earlier enumeration**, and it reaches consumers by exactly the mechanism the section already argued for pugixml. The failure mode is worth naming precisely because it is *not* the one an implementer expects: a missing `find_dependency` is not a link-time undefined symbol — the generated `fixppTargets.cmake` names `Crc32c::crc32c` in an `INTERFACE_LINK_LIBRARIES` property, so `find_package(fixpp)` itself fails at **configure** with *"the target was not found … A find_package call is missing for an IMPORTED target"*. It fires for every consumer, including the minimal witness.

**ZLIB stays out.** It appears only in `tests/transport/CMakeLists.txt:18-22` and `perf/CMakeLists.txt:19,24-25`; independently confirmed by **zero `ZLIB`/`zlib` matches anywhere under `src/`**. Those harnesses add it because they resolve OpenSSL by raw `find_library(... NO_DEFAULT_PATH)`, which bypasses the imported target's own interface; a consumer using the `OpenSSL::SSL` / `OpenSSL::Crypto` imported targets gets any compression dependency transitively.

**Decision**: `fixppConfig.cmake` calls `find_dependency` for OpenSSL, asio, pugixml, Crc32c and tomlplusplus, plus **opentelemetry-cpp for every package this feature ships** (all six in-scope configurations are OTel-ON — the telemetry leg is emitted when the OTel SDK was present at configure, keyed on the target graph per I3, and is absent only in the OTel-OFF build SC-015 exercises). ZLIB is deliberately omitted. `fixpp_log_otlp`'s three additional `opentelemetry-cpp::*` targets (`src/log/CMakeLists.txt:49-51`) add **no** new `find_dependency` — they are components of the same package.

**Scope limit — `find_dependency` locates; it does not provide.** Getting this table right makes the config file *correct*; it does not put the six packages on a consumer's disk. They are Conan-pinned *for this project's builds* (`conanfile.py:66`–`:69`, `:77`, `:94`), but the consumer may satisfy them from any provider — see the provider-agnostic note above. What a consumer must be **told** is which of the six they will likely have to supply themselves (`Crc32c`, `opentelemetry-cpp`): FR-018e obligation 3, R13.

**Rationale for not adding it "just in case"**: an unconditional `find_dependency(ZLIB)` makes every consumer require a package the library does not use — a false dependency is as much a packaging defect as a missing one. The real-client witness links for real, so a genuine need would surface as a link failure rather than being silently masked.

**Why `pugixml` despite being a PRIVATE dependency**: static libraries do not link their private dependencies; the consumer's final link must resolve `pugixml` symbols referenced by `fixpp_dictionary`. The build-tree failure mode is documented at `CMakeLists.txt:299-304`.

---

## R4 — `FIXPP_LOG_MIN_LEVEL` must survive export. `FIXPP_BUILD_OTEL` must not leak.

Two compile definitions, opposite dispositions — established by checking which reach public headers.

- **`FIXPP_LOG_MIN_LEVEL`** — used in `include/fixpp/log/logger.hpp` and `include/fixpp/log/level.hpp`, set `PUBLIC` on `fixpp_log`, and **build-type-conditional** (`CMakeLists.txt:60-64`: Debug `0`, Release `2`). It drives an `if constexpr` compile-time cutoff. If the export drops it, a consumer's cutoff silently differs from the shipped library's. It propagates correctly through the exported target's `INTERFACE_COMPILE_DEFINITIONS` provided the export is not hand-written to omit it.
- **`FIXPP_BUILD_OTEL`** — propagated by `add_compile_definitions(...)` at `CMakeLists.txt:102`, which is directory-scoped and does **not** travel with an exported target. **Verified zero occurrences under `include/`**, so no public header branches on it and consumers do not need it. It affects compiled sources only.

**Decision**: no special handling required for either — the natural export propagates the one that must travel and cannot propagate the one that must not. **Recorded because it looks like a hazard and is not**: an implementer who "fixes" this by adding `FIXPP_BUILD_OTEL` to the exported interface would introduce the ODR mismatch that currently cannot occur.

---

## R5 — `linux-gcc-debug` needs a tracked Conan profile, and adding one follows precedent

`conan/profiles/` **is** a tracked in-repo directory with 14 profiles (an earlier note in this feature that only referenced `~/.conan2/profiles/` was incomplete). There is no `linux-gcc-debug`; `conan/profiles/linux-gcc-release` pins `build_type=Release`, `compiler.version=13`, `libcxx=libstdc++11`, `cppstd=23`, `CC/CXX=gcc-13/g++-13`, Ninja.

**Constitution question**: Article III §3 enumerates 11 profiles and does not list `linux-gcc-debug`. Does adding one require an amendment (Article XX)?

**No.** The enumeration is **already non-exhaustive** — four profiles exist in-repo that Article III §3 does not list: `linux-clang-libc++`, `linux-clang-libc++-asan`, `linux-clang-libc++-tsan`, `linux-clang-libc++-ubsan`. Adding a profile without amending the article is established practice.

**Decision**: add `conan/profiles/linux-gcc-debug` as a `build_type=Debug` sibling of the release profile, and a matching `linux-gcc-debug` preset mirroring `linux-gcc-release` (`CMakePresets.json:98-109`) with `CMAKE_BUILD_TYPE=Debug`. **Alternative rejected**: passing `-s build_type=Debug` on the command line without a tracked profile — it would leave the configuration unreproducible and outside the Article III §2 "declared, pinned" discipline.

---

## R6 — Package publication: the constitution is more specific than the clarification assumed

**Article IV §5**: *"v1.0 release artifacts are built but not published. Conan packages and Python wheels are attached to GitHub releases; no upload to Conan Center or PyPI in v1. Publishing is gated on production-readiness and the README disclaimer being removed."*

This is a **stronger and more precise basis** for the Q1 clarification than Article V §5 (the disclaimer rule) cited in `spec.md`. It states the v1.0 distribution model directly: build artifacts, attach to releases, do not upload to package registries.

**Decision**: keep the clarified answer (CI artifacts only). On the *publication* axis it is a subset of what Article IV §5 permits — the article allows attaching to GitHub releases; this feature does not set that up. That is a scope choice, not a conflict.

**On the *artifact-class* axis it is not a subset, and that must be stated rather than glossed** *(corrected at Gate A round 1)*. Article IV §5 names exactly two artifact kinds — **Conan packages and Python wheels** (`.specify/constitution.md:144`) — and Article IV §1 says the C++ library *"is consumed in-process by C++23 code **via Conan**"* (`:140`). This feature produces DEB, RPM, TGZ and ZIP, **none of which appears in Article IV**, and produces no Conan package. So the earlier framing ("deliberately narrower than the article permits") was wrong in kind: native OS packages are a *different* artifact class, not a narrower slice of the same one.

**Disposition**: the article enumerates what is *published*; it does not enumerate an exhaustive set of buildable artifact kinds, and it forbids none. Native OS packages are therefore outside Article IV's distribution model rather than in conflict with it, and this feature does not change what Article IV governs. The `install(TARGETS)` / `install(EXPORT)` work is moreover a genuine prerequisite for the Conan package Article IV §1 *does* name — which is why the plan's IV §1 row reads "PASS — advances it". Recorded here, and as a Constitution Check row, so the gap is visible rather than implied.

---

## R7 — Static-link ordering must be carried by the export, not by the consumer

`perf/CMakeLists.txt:56-57`: *"fixpp libs FIRST (immediately before OpenSSL) so static-link order resolves the OpenSSL symbols `libfixpp_tls.a` references."* Both the perf driver and `tests/interop` reproduce this ordering by hand.

**Decision**: the exported targets must declare their dependencies such that CMake's own topological ordering produces a correct link line, so no consumer restates it. This is what target-level dependency declaration is for. **This is FR-010b, and only a witness that actually links can confirm it** — the check must exercise the link interface, not merely resolve `find_package`.

**Cause corrected at Gate A round 1.** This section previously said *"the hand-ordering in the harnesses exists because they link raw archive paths rather than targets."* That is wrong and would misdirect an implementer diagnosing a link-order failure: `perf/CMakeLists.txt:58-61` links `fixpp_session` / `fixpp_transport` / `fixpp_tls` as **targets**; the raw paths are the OpenSSL/ZLIB `find_library` results (`:20-25`, `:62-64`). The real cause is that the harness resolves OpenSSL by raw `find_library(... NO_DEFAULT_PATH)` instead of using the `OpenSSL::` imported targets, so CMake has no dependency edge between the fixpp archives and libcrypto to order on. The conclusion — exported target dependencies let CMake order the link line — survives unchanged.

**Related stale comment**: `perf/CMakeLists.txt:15-17` claims `fixpp_transport`/`fixpp_tls` expose asio/ssl (OpenSSL) *"PRIVATEly"*. `src/transport/CMakeLists.txt:43-49` links OpenSSL **PUBLIC**, with a long comment at `:33-41` explaining why. The perf comment is stale; noted so the packaged-variant adaptation does not carry it forward.

---

## R8 — The existing consumer witness hand-rolls what the export will replace

`tests/consumer/CMakeLists.txt` is a standalone `project()` that today does `find_package(pugixml CONFIG REQUIRED)`, adds `${FIXPP_STAGE_PREFIX}/include` by hand (`:50`), and links a **globbed archive list** `${_fixpp_archives}` plus `pugixml` (`:65`). Its own header comment (`:14`) states: *"There is no fixpp CMake package-config / find_package(fixpp) export."*

**Decision**: replace the hand-rolled discovery with `find_package(fixpp)` + `fixpp::fixpp`. The surrounding harness — `run_consumer_witness.cmake`, stage-install, build-type inheritance (`CMakeLists.txt:299-305`) — is reused unchanged. Per SC-002 the consumer must include **both** a hand-written `include/` header and a generated per-version header (e.g. `Fields.hpp`), because those arrive via two different install rules and only the second exercises the dict export.

---

## R9 — The real client needs three subtractions, and it inverts a standing caution

`perf/fixpp_perf_driver.cpp` includes only public `<fixpp/...>` headers plus one test helper. Its build (`perf/CMakeLists.txt`) must be adapted for out-of-tree use:

1. Drop `src/` and `tests/` from the include path (`:51-54`) — SC-012 forbids any source-tree path.
2. Drop the `HdrHistogram_c` `FetchContent` (`:43-47`) — a network fetch, and latency instrumentation is irrelevant to a link-and-run witness.
3. Replace `support/minimal_dictionary.hpp` with a runtime load of a **shipped** dictionary via `fixpp::dict::load_any` (`include/fixpp/dict/load_any.hpp`), which is what FR-018a makes possible.

It is gated `FIXPP_BUILD_INTEROP_PERF` (default OFF, `cmake/ProjectOptions.cmake:10`), so the witness enables it explicitly. **D3 closed at the 2026-08-01 sign-off**: exactly one CI lane — `linux-gcc-release` — turns it on and gates SC-011/SC-012; the other five run the minimal tier. Recorded as **FR-026a** so it drives a task rather than living in plan prose (`plan.md` → Gate A → Sign-off decisions).

**Inverted caution**: the driver is documented as needing an in-tree build so it links freshly generated libraries rather than a stale prebuilt one. Building it against an installed package is a deliberate inversion, safe **only** because the package comes from the build under test — which FR-021a enforces via provenance. The hazard is live rather than theoretical because `artifacts/` deliberately survives the build-tree deletion cycle, so older packages persist alongside current ones.

---

## R10 — CPack invocation model: per-configuration, single-config

Both Linux and Windows in-scope presets are single-config Ninja/MSVC-with-explicit-`CMAKE_BUILD_TYPE`. Combined with the serial build-and-delete discipline (only one configuration exists at a time), a multi-config generator would provide nothing.

**Decision**: one CPack invocation per configured build tree, producing that configuration's package set. Configuration is encoded in the artifact name (FR-017).

**Alternative rejected**: a multi-config generator or paired invocations on a shared tree — both require two configurations to coexist, which the storage budget explicitly forbids (Assumption 5, SC-008).

**Staging**: CPack stages into `_CPack_Packages/` inside the build directory, so deleting the tree removes the staged files automatically. `CMAKE_INSTALL_PREFIX` must never point at a system location (FR-020). Finished artifacts are copied to a location outside every build tree (FR-021).

---

## R11 — **The export set as designed cannot pass `cmake` generate**: no module target has a generator-expression include interface

*(New at Gate A round 1. This is the review's top finding and it blocks.)*

Every module target sets its PUBLIC include directory as a bare absolute source path with no generator expression:

`src/core/CMakeLists.txt:13`, `src/core/sync/CMakeLists.txt:26`, `src/wire/CMakeLists.txt:21`, `src/dictionary/CMakeLists.txt:35`, `src/tls/CMakeLists.txt:20`, `src/transport/CMakeLists.txt:22-26`, `src/log/CMakeLists.txt:17` (and `:43` for `fixpp_log_otlp`), `src/session/CMakeLists.txt:28`, `src/otel/CMakeLists.txt:16`, `src/config/CMakeLists.txt:23`, `src/tap/CMakeLists.txt:8`, `src/service/CMakeLists.txt:11`, `src/capi/CMakeLists.txt:26` — all `PUBLIC "${CMAKE_SOURCE_DIR}/include"`.

**Census**: `grep -rn "BUILD_INTERFACE" src/` returns **zero matches**.

`install(EXPORT)` rejects such a target at generate time: *"Target … INTERFACE_INCLUDE_DIRECTORIES property contains path … which is prefixed in the source directory. Generating done — CMake Generate step failed."*

**Why this matters more than its size suggests.** It is the *first* thing that happens when anyone writes `install(TARGETS fixpp_core EXPORT fixppTargets)` — the design's central deliverable does not configure. And `data-model.md` I4 previously asserted the opposite as verified fact (it said the dict targets have "only the former", whose only reading is that the module targets already have `$<BUILD_INTERFACE:>`). In a bundle whose stated method is "verified, not assumed", a false verification is worse than a gap; I4 now carries an explicit correction rather than a reworded claim.

**Consequence for scope — the definite edit list is THIRTEEN of the fourteen files** *(narrowed to nine at Gate A round 2 from the measured export set; widened at the 2026-08-01 sign-off, which exports all four conditional subtrees)*:

`src/core`, `src/core/sync`, `src/log`, `src/wire`, `src/dictionary`, `src/tls`, `src/transport`, `src/session`, `src/otel` — the round-2 nine — **plus `src/config`, `src/tap`, `src/service`, `src/capi`**.

`src/core/test/CMakeLists.txt:15` (`fixpp_mock_clock`) is the **one file that stays out**: its subtree `include/fixpp/core/test/` is the FR-012a `exclude` row, and the target is `FIXPP_BUILD_TESTS`-only.

**Thirteen files, fourteen targets.** `src/log/CMakeLists.txt` carries **two** — `fixpp_log` (`:16-17`) and `fixpp_log_otlp` (`:42-43`), the latter now an export-set member (R2 / `export-set.md` §2a) — so no new *file* joins the list but a second target in an existing one does. And the `src/capi` edit is on **`fixpp_capi_objects`** (`:25-26`), **not** on `fixpp_capi`, which declares no include directories at all (`:43-45`).

M4 named only the first eight of the nine, because `fixpp_otel` was not yet in the export set when M4 ran — it was added at M5. `src/otel/CMakeLists.txt:15-16` declares `target_include_directories(fixpp_otel PUBLIC "${CMAKE_SOURCE_DIR}/include")` raw, identically to the others, so it fails the same generate check the moment it joins. **Two measured members correctly need nothing**: `fixpp_dict_dispatch_bridge`'s only include directory is `PRIVATE` (`src/dictionary/CMakeLists.txt:82-84`), so it has no `INTERFACE_INCLUDE_DIRECTORIES` to reject; `fixpp_dict_dispatch` already carries `$<BUILD_INTERFACE:…>` (`cmake/Codegen.cmake:589-590`) — which is why neither appeared in M4. `fixpp_capi` needs nothing for the same first reason.

Whether the rewrite is done **per-module** in each `src/*/CMakeLists.txt` or **centrally** via `set_property(TARGET … PROPERTY INTERFACE_INCLUDE_DIRECTORIES …)` from the root is a design choice — **but both forms edit build files under `src/`**, which falsifies the plan's earlier "this feature touches neither `src` nor `include`". See FR-002a, spec Assumption 11, `plan.md` → Project Structure and its Article IX row.

**Not decided here — and NOT waiting on a generate run** *(corrected at Gate A round 2)*: which rewrite form. Round 1 said this was "settled by the R2 blocking prerequisite's generate run". That run has now happened and explicitly did **not** settle it — the measurement records *"Not measured: whether the export succeeds once all three blockers are fixed. That needs the `src/*/CMakeLists.txt` include-interface edits, which are implementation, not a Gate-A experiment."* The rewrite form and the R12 file-set disposition are **open implementation choices** (FR-002a, FR-002b), to be recorded when made. Nothing further is owed by measurement.

---

## R12 — `fixpp_transport`'s `FILE_SET HEADERS` is a second, independent generate blocker

*(New at Gate A round 1.)*

`src/transport/CMakeLists.txt:53-60` declares `target_sources(fixpp_transport PUBLIC FILE_SET HEADERS BASE_DIRS "${CMAKE_SOURCE_DIR}/include" FILES …)` over six public transport headers. It is the **only** `FILE_SET` in the repository — `grep -rn FILE_SET src/ CMakeLists.txt cmake/` returns exactly one hit.

CMake makes this a hard error: *"install TARGETS target … is exported but not all of its interface file sets are installed."* `fixpp_transport` is an unavoidable export-set member, so the export cannot be written without choosing among:

| Disposition | Consequence |
|---|---|
| Install the file set | Duplicates headers already installed by `CMakeLists.txt:321-324`; needs a `FILE_SET HEADERS DESTINATION` chosen so the two rules do not conflict |
| Demote it to `PRIVATE` | Touches `src/transport/CMakeLists.txt`; the file set is currently annotated *"Phase 2 public headers (installed via top-level install(DIRECTORY include/))"*, i.e. it already relies on the directory rule for installation, so demotion may be near-free — **verify before relying on it** |
| Drop it | Same file, larger blast radius; loses whatever IDE/header-dependency benefit the file set provides |

### ✅ DECIDED at implement (2026-08-02, T020): **demote to `PRIVATE`**

**No `DESTINATION` is owed** — that obligation attaches only to the first option.

Grounds, all source-verified before choosing:

1. **The file set is not the install mechanism and never was.** Its own annotation at `src/transport/CMakeLists.txt:52` reads *"Phase 2 public headers (installed via top-level install(DIRECTORY include/))"*. All eight `FILES` entries are `include/fixpp/transport/*.hpp`, inside the tree `CMakeLists.txt:321-324` installs unconditionally. Demotion therefore changes **nothing about what ships** — it is not a change in delivered content and must not be recorded as one.
2. **The include interface it contributes is already declared independently.** A `PUBLIC` `FILE_SET HEADERS` adds its `BASE_DIRS` to `INTERFACE_INCLUDE_DIRECTORIES`; here that is `${CMAKE_SOURCE_DIR}/include`, the *same* path `target_include_directories(fixpp_transport PUBLIC …)` already declares at `:22-26` — which T011 rewrites into `$<BUILD_INTERFACE:>`/`$<INSTALL_INTERFACE:>` form. Demotion drops a duplicate, not a usage requirement.
3. **Installing it instead would make transport's eight headers uniquely special.** Every other module's public headers reach the package through the directory rule. Option (i) adds a second install rule writing the same files to the same prefix for one module alone — redundancy that reads as intent and invites a future maintainer to "fix" the other modules to match.
4. **Dropping it (option iii) is strictly worse than demoting.** Same file edited, but the eight headers stop being listed as sources of `fixpp_transport` at all, losing the IDE/header-dependency association for no gain.

**Verification obligation — this decision is not self-certifying.** R12's own table says *"demotion may be near-free — verify before relying on it"*. Two checks are owed at T024's re-measurement, and demotion is not accepted until both pass: (a) `cmake --preset linux-gcc-release` generates with `fixpp_transport` in the export set and the *"not all of its interface file sets are installed"* diagnostic gone; (b) a property dump confirms `fixpp_transport`'s `INTERFACE_INCLUDE_DIRECTORIES` still carries the install-interface path after the demotion — i.e. that grounds 2 held in fact and not merely on reading.

---

## R13 — `find_dependency` does not make the package installable — and no designed witness can detect it

> ### ⚠️ HALF RETRACTED at Gate A sign-off (2026-08-01)
>
> **Retracted: "the dependency set is Conan-only", and everything that followed from it.** That conclusion inferred a *consumption* constraint from a *build* convention. The exported graph names **only imported targets** — census across `src/`, `cmake/` and the root `CMakeLists.txt`: **zero** `find_library(…)` calls, **zero** `.conan2` paths — so `install(EXPORT)` writes target names and each `find_dependency(X)` is a plain `find_package(X)` against the **consumer's** `CMAKE_PREFIX_PATH`. **The package is provider-agnostic by construction.** The `CONFIG REQUIRED` lookups at `src/dictionary/CMakeLists.txt:9` and `src/config/CMakeLists.txt:11` do constrain the consumer to a config-mode provider, but both upstreams ship CMake config packages and neither call is Conan-specific. The vendor / distro-`Depends:` / Conan-consumption-only option set built on this premise is **withdrawn** — it was a mis-framing, not a live choice.
>
> **Surviving, and unchanged: the blindness.** Every designed witness is handed the producer's Conan toolchain, so none of them can fail on a dependency the *consumer* would have to supply, nor on a generated config that baked in a build-host path. That is the real defect this section found, and it is what SC-016 exists for. FR-018e now carries four obligations instead of a three-way choice.

*(New at Gate A round 1. Second blocker.)*

**The dependency set is Conan-pinned for this project's builds** — `pugixml/1.15` (`conanfile.py:66`), `asio/1.38.0` (`:67`), `crc32c/1.1.2` (`:68`), `openssl/3.6.2` (`:69`), `tomlplusplus/3.4.0` (`:77`), plus `opentelemetry-cpp/1.26.0` (`:94`, inside `requirements()` under `if self.options.with_otel`). **Two of the six — `Crc32c` and `opentelemetry-cpp` — are rarely offered by a platform package manager**, so a consumer will typically have to supply them; that is the honest statement FR-018e obligation 3 requires, and it is a much narrower claim than "Conan-only".

**The witness structurally cannot detect it.** The harness passes the *producing* build's Conan toolchain straight into the consumer sub-build: `CMakeLists.txt:284-306` hands `-DFIXPP_BUILD_TYPE=${CMAKE_BUILD_TYPE}` into `tests/consumer/run_consumer_witness.cmake`, which configures the consumer with `-DCMAKE_TOOLCHAIN_FILE=<build-dir>/conan_toolchain.cmake` — documented in the witness's own comment at `tests/consumer/CMakeLists.txt:39-44`. The consumer therefore sees exactly the producer's dependency graph. SC-001's *"zero manually specified include or library paths"* is satisfiable while the package is unusable anywhere else. This is the `feedback_verification_corpus_built_from_the_read_it_checks_is_blind` shape: the witness cannot fail on the defect it is nominated to gate. `contracts/export-set.md` §1's *"the consumer names **no** third-party dependency of fixpp"* is true **only under the producer's toolchain**, and now says so.

**The consequence is a package with no dependency metadata.** `find_dependency` is a thin wrapper over `find_package` — it *locates*, it does not *provide*. FR-018 required product/version/description/license/maintainer and **no** dependency statement at all, so an operator whose host lacks `Crc32c` or `opentelemetry-cpp` meets that fact for the first time at `find_package(fixpp REQUIRED)` — at **configure**, with no warning from the package.

**Fix, in two parts**: **FR-018e** (as rewritten at the sign-off) requires the config to stay provider-agnostic, to declare a tested-against version and ABI character per dependency, and to say honestly which dependencies a consumer must supply; **SC-016** proves it on a prefix the producing build's package manager did not fill, with a **pass** state and an explicit **red** leg (remove one named dependency → that dependency's `find_dependency` diagnostic and no other).

*(Evidence basis: source facts — the imported-target census, the `CONFIG REQUIRED` lookups, the toolchain-inheriting harness — not a repro.)*

---

## R14 — "Shipped header with no exported library" is a class of four, not a single C-ABI question

*(New at Gate A round 1.)*

`CMakeLists.txt:321-324` installs the **entire** `include/` tree unconditionally. Applying the bundle's own rule exhaustively across `include/` versus the measured export set (R2):

| Shipped subtree | Backing target | In export set? |
|---|---|---|
| `include/fix/` + `include/fix/c_api/` | `fixpp_capi` (`src/capi/CMakeLists.txt:43`) | Was: No — routed to D1. **Now: Yes** (D1 = Option A, 2026-08-01) — and it forces `fixpp_capi_objects` in with it |
| `include/fixpp/config/` | `fixpp_config_toml` (`src/config/CMakeLists.txt:13`); `architecture.md:67` module 14, Public: Yes | Was: No. **Now: Yes** — adds `tomlplusplus` to R3 and forces `fixpp_log_otlp` in |
| `include/fixpp/tap/` | `fixpp_tap` INTERFACE (`src/tap/CMakeLists.txt:4-5`); `architecture.md:62` module 9, Public: Yes | Was: No. **Now: Yes** — twice over, since `fixpp_capi_objects` links it PUBLIC (`src/capi/CMakeLists.txt:36`) |
| `include/fixpp/service/` | `fixpp_service` INTERFACE (`src/service/CMakeLists.txt:7-8`); `architecture.md:64` module 11, and §8 `:523` names it public plugin surface | Was: No, bound to D1 via `fixpp_capi` (`:15`). **Now: Yes**, following D1 |
| `include/fixpp/otel/` | `fixpp_otel` — a STATIC library when the OTel SDK is present, an empty INTERFACE **stub** when `FIXPP_BUILD_OTEL=OFF` (`CMakeLists.txt:170`) | **Yes — always**, held by this feature exporting the target unconditionally rather than by the closure (the PUBLIC edge from `fixpp_session` sits inside `if(FIXPP_BUILD_OTEL)`, `src/session/CMakeLists.txt:55-57`). The subtree ships regardless: `include/fixpp/session/engine.hpp:32` includes `<fixpp/otel/trace_context.hpp>` unguarded |

`include/fixpp/config/toml_config_loader.hpp:7-8` tells consumers in so many words to link `fixpp::config_toml`, so the `config` case is the sharpest: the package ships a header whose own documentation names a target the package does not provide. `tap` and `service` are INTERFACE targets, so the practical harm is smaller — a consumer gets headers but no include-dir or transitive-dependency propagation — but the *rule* the bundle states ("a shipped header with no library is a defect") applies identically.

**Decision**: dispositioned as a class (FR-012a), with the exhaustive per-subtree table in `contracts/package-layout.md` §2a and SC-009a asserting each row's disposition is one of `export` / `exclude` — an `OPEN` row **fails** it (the earlier "no row is empty" form was retracted at Gate A round 2: `OPEN` satisfied it, so the gate could not fail on the defect it exists to catch).

**Closed at the 2026-08-01 sign-off: the class resolves to `export`.** All four subtrees above get their backing target, `tomlplusplus` joins the R3 set unconditionally, and the two forced additions (`fixpp_capi_objects`, `fixpp_log_otlp`) are recorded as **derived, not measured**. The class was **wider than four**: the exhaustive table also disposed of `include/fixpp/otel/`, the two `detail/` subtrees (ship — three of four headers are reached from public headers), and the two **test-support** subtrees, which are the class's single `exclude` — `include/fixpp/core/test/` and `include/fixpp/transport/test/`, removed from what ships as a deliberate change in delivered content.

---

## Open items carried into Phase 1

| Item | Disposition |
|---|---|
| Exact upstream-license file layout inside the package | Design in `contracts/`; obligations fixed by FR-018b |
| Whether `NormativeReferences.md` is excluded from the include tree | Candidate cleanup (R1); not a requirement |
| Whether OpenSSL's imported target carries compression transitively in every in-scope configuration | Verified empirically by the real-client link (R3); no config change unless it fails |

## Open items added at Gate A round 1

| Item | Disposition |
|---|---|
| **Export-set membership** (R2) | ✅ **Measured floor CLOSED at Gate A round 2** — the executed run produced the eleven-member minimum (M3–M5). **⚠️ RE-OPENED as an implementation obligation by the 2026-08-01 sign-off**: the six members D1/FR-012a add are **derived by reading, not measured**, and the reading method has been wrong here before. **Re-run the generate experiment once they are wired** and reconcile against `contracts/export-set.md` §2a |
| **Include-interface rewrite form** — per-module vs centralised from the root (R11) | ✅ **DECIDED at implement (2026-08-02, T005): PER-MODULE.** See the decision note below. T006–T018 stand as written; **no** task collapses and no waiver row is owed. The file list is **13 of 14** regardless of form (R11), carrying **14 targets** |
| **`fixpp_capi_objects` install shape** (new, sign-off) | It is an **OBJECT** library, so `install(TARGETS …)` needs an `OBJECTS DESTINATION` — a shape no other member has. Whether the export takes that destination or a wiring change is the first thing the re-measurement must answer |
| **`fixpp_transport` `FILE_SET` disposition** (R12, FR-002b) | Install / demote to PRIVATE / drop. Record the choice and its `DESTINATION` |
| **Dependency provisioning** (R13, FR-018e) | ✅ **CLOSED at the 2026-08-01 sign-off — and the question was mis-framed.** The package is **provider-agnostic by construction**; the three-option table is withdrawn. FR-018e now carries four obligations (no build-host paths in the installed config; tested-against version + ABI character per dependency; honest availability statement; SC-016 proves it) |
| **Shipped-header dispositions** (R14, FR-012a) | ✅ **CLOSED at the 2026-08-01 sign-off** — the class resolves to `export`; `tomlplusplus` joins R3 unconditionally |
| **Per-version `fixpp::dict::<ver>` targets** (R2) | ✅ **DECIDED — excluded.** Not routed to Gate A round 3 sign-off (unlike D1/FR-018e/D3); settled by the three verified grounds above. Standing re-open trigger only: if a future change adds them, enumerate by name, disposition `vt11` explicitly, and provide a check other than SC-002 for their install interface |
| **`include/fixpp/otel/` in an OTel-OFF package** (`package-layout.md` §2a) | ✅ **CLOSED at the 2026-08-01 sign-off — it ships in every configuration.** `include/fixpp/session/engine.hpp:32` includes `<fixpp/otel/trace_context.hpp>` unguarded, so excluding it would break a public session header even in an OTel-OFF build |
| **Test-support headers** — `include/fixpp/core/test/`, `include/fixpp/transport/test/` | ✅ **CLOSED at the 2026-08-01 sign-off — EXCLUDE**, recorded as a deliberate change in delivered content. This is the first `PATTERN` filter `CMakeLists.txt:321-324` will carry, and it keeps `src/core/test/CMakeLists.txt` out of the FR-002a edit list |
| **Artifact-directory retention** (spec Assumption 5, SC-008) | ✅ **DECIDED at implement (2026-08-02, T004)** — recorded in `quickstart.md` §0. Artifacts stay on the 64 GB volume and **accumulate across the whole matrix run** (T062's count+name-set assertion requires all fourteen to coexist; a between-configuration purge would make SC-003 unfalsifiable). Purge at the **start** of a run only. Budget enforced by a preflight `df` check per configuration — ≥ 12 GB free before a Release, ≥ 33 GB before a Debug. Projected total ≈ 7 GB, **projected not measured** — T064 measures it and corrects the figure |
| **`architecture.md` §7.4 reconciliation** (FR-024a) | Committable on this branch — do **not** bundle with FR-024/FR-025's parent-repo staging |

### T005 decision note — the include-interface rewrite is PER-MODULE

*(Recorded 2026-08-02 at implement. FR-002a; `plan.md:429` open choice 1.)*

**Form**: in each of the 13 files, rewrite in place to

```cmake
target_include_directories(<tgt> PUBLIC
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
```

preserving each target's existing `PRIVATE` entries verbatim (`fixpp_transport` and `fixpp_dictionary` both carry them).

**Why not centralised.** The centralised form is fewer edits and worse on three counts, two of them specific to *this* feature:

1. **It creates a second target list that must stay in sync with the export set.** A root-level sweep needs an enumeration of the 14 targets. This feature already has one hard target-list-maintenance problem — T023's `install(TARGETS … EXPORT)` membership, whose re-open checklist (T019/T022/T023/T026/T030) exists precisely because a list derived by reading was wrong three times. Adding a *second* list, updated by a different task, reproduces the exact "half-applied sweep" failure T022 is written to prevent. A member added later would silently miss the include rewrite while appearing in the export.
2. **`set_property(TARGET … PROPERTY INTERFACE_INCLUDE_DIRECTORIES …)` overwrites rather than appends.** Several targets carry interface include directories from elsewhere — the codegen-generated ones via `cmake/Codegen.cmake:541-544`, and `fixpp_transport`'s file-set `BASE_DIRS` until T020 demotes it. A root-level `set_property` would clobber them silently; getting it right means read-modify-write per target, which is the per-module edit with extra indirection and no locality.
3. **It separates the usage requirement from the target definition**, against the surrounding style — every module in `src/` declares its own include directories next to its own `add_library`. A reader of `src/session/CMakeLists.txt` would no longer see how `fixpp_session` gets its include interface.

**Cost accepted**: 13 near-identical edits. That is a mechanical sweep, and T019 already exists as its exact-count census gate (13 files / 14 targets, `src/core/test/CMakeLists.txt` verifiably unedited) — so the repetition is *checked*, which is what makes it the cheaper risk.

**Consequence for the task list**: T005's conditional ("if the centralised form is chosen, T006–T018 collapse into one root-level task and each per-file row is marked with a waiver") **does not fire**. No waiver rows are owed.

### T022 decision note — the `EXPORT_NAME` convention already exists in-tree, and T026's check has a hole

*(Recorded 2026-08-02 at implement, from the executed alias census. FR-003; `contracts/export-set.md` §1.)*

**Convention chosen: `EXPORT_NAME` = the target name with its `fixpp_` prefix stripped** — i.e. exactly the short name each target's existing `add_library(fixpp::<short> ALIAS …)` already publishes. This is not a new convention; it is the repository's de-facto one, held consistently across **15** aliases (`grep -rn "add_library(fixpp::[A-Za-z_:]* ALIAS" src/ cmake/ CMakeLists.txt`). Choosing anything else would make the installed package disagree with every in-tree consumer and with `architecture.md:503`.

Two findings the census produced that the task list did not anticipate:

**1. Two export-set members have NO alias at all** — `fixpp_dict_dispatch_bridge` (`src/dictionary/CMakeLists.txt:79`) and `fixpp_capi_objects` (`src/capi/CMakeLists.txt:11`). Every other member has one. This **breaks T026's check as written**: T026 asserts the alias census and the installed imported-name set are *"equal both directions, never a subset"*, which cannot hold when two members contribute an installed name and no alias to match it. T026's stated red — *"a member whose installed imported name does not match its existing in-tree alias"* — does not cover *"a member with no in-tree alias"*.

  **Disposition**: give both an `EXPORT_NAME` under the same convention (`dict_dispatch_bridge`, `capi_objects`) and **add the matching in-tree alias**, rather than carving them out of the equality check. Grounds: both are genuine export members (the bridge by the measured `$<LINK_ONLY:>` closure, `capi_objects` by `fixpp_capi`'s PUBLIC edge), so a consumer *can* see them; an exception list is the "half-applied sweep" T022 exists to prevent, and it would have to be maintained in the test. Adding two aliases keeps the gate a plain set equality with **zero** exemptions.

**2. `fixpp_dict_dispatch`'s alias is NESTED — `fixpp::dict::dispatch`** (`cmake/Codegen.cmake:588`), not `fixpp::dict_dispatch`. Under `install(EXPORT … NAMESPACE fixpp::)` that requires `EXPORT_NAME dict::dispatch`, i.e. a **`::` inside an `EXPORT_NAME`**. ⚠️ **Whether CMake accepts that is NOT assumed — it is verified at T024's generate run before the convention is applied to this target.** If it is rejected, the fallback is `EXPORT_NAME dict_dispatch` publishing `fixpp::dict_dispatch`, and the divergence from the in-tree `fixpp::dict::dispatch` alias is then a **recorded, named exemption in T026** with this note as its rationale — not a silent mismatch. `fixpp::dict::runtime` (`:597`) has the same shape but is **not** an export member, so it is unaffected.

**3. The `fixpp::fixpp` umbrella (T029) is a new target and needs both** an alias and an `EXPORT_NAME` (`fixpp`) so that it, too, appears on both sides of T026's equality.
