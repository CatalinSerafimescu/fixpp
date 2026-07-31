# Phase 0 Research — 084-packaging-cpack-export

**Date**: 2026-07-31 · **Branch**: `084-packaging-cpack-export`

Every finding below was verified against source in this worktree on the date above. Citations are `file:line` at that revision. This document is the evidence base for `plan.md`; where it corrects something asserted in `spec.md`, that is called out explicitly.

---

## R1 — FR-010: does the export set reach the typed-builder `Args` trees? **NO. Confirmed clean.**

This is the spec's escalation trigger, so it is resolved first.

**Two independent mechanisms both prevent it:**

1. **The install denylist.** `CMakeLists.txt:351-355` excludes `messages/`, `groups/`, `validators/`, `all.hpp`, `groups.hpp` from the generated-header install. `Args` live under `messages/` and `groups/`.
2. **The dict targets carry a build-tree-only include path.** `cmake/Codegen.cmake:543-544` gives each `fixpp::dict::<ver>` INTERFACE target exactly one include directory, `$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/_codegen/include>` — **no `$<INSTALL_INTERFACE:>` at all**. An exported target therefore cannot point a consumer at anything, let alone at `Args`, until this feature adds an install interface. When it does, that interface resolves to the *installed* include directory, whose contents are already filtered by mechanism 1.

**What actually survives the denylist** (measured on `build/linux-clang-debug/_codegen/include/fixpp/v44/`): `Fields.hpp` (1.9 MB), `Messages.hpp` (1.9 MB), `Reify.hpp` (1.9 MB), `Validator.hpp` (920 KB), `NormativeReferences.md`. Excluded: `messages/`, `groups/`, `validators/`.

**Decision**: FR-010 is discharged as a confirmation. The deferred "Option 3" `Args` representation change (SC-001 / L-078-1) remains untouched and unforeclosed. **No escalation required.**

**Standing obligation**: the confirmation is only valid for the export set as designed here. If a later change adds an `$<INSTALL_INTERFACE:>` that resolves anywhere other than the denylisted install tree, FR-010 must be re-verified.

**Incidental finding**: `NormativeReferences.md` is installed into the public include tree — a Markdown file shipping as a header. Cosmetic, but it lands in every package. Cheap to exclude; recorded as a candidate, not a requirement.

---

## R2 — The export set is a transitive closure, and it is configuration-dependent

Derived by reading each target's `target_link_libraries`, not assumed.

| Target | Kind | PUBLIC link interface | PRIVATE |
|---|---|---|---|
| `fixpp_core` | STATIC | *(none declared)* | — |
| `fixpp_log` | STATIC | `fixpp_core` · **`FIXPP_LOG_MIN_LEVEL` (PUBLIC compile definition)** | — |
| `fixpp_sync` | STATIC | *(atomic_shared_ptr fallback symbols)* | — |
| `fixpp_wire` | STATIC | `fixpp_core`, `fixpp_dictionary` | — |
| `fixpp_dictionary` | STATIC | `fixpp_core`, `fixpp_wire` (+ `fixpp_dict_dispatch_bridge`, conditional) | `pugixml::pugixml` |
| `fixpp_tls` | STATIC | `fixpp_core`, `fixpp_sync`, `asio::asio` | `OpenSSL::Crypto` |
| `fixpp_transport` | STATIC | `fixpp_core`, `fixpp_sync`, `fixpp_tls`, `fixpp_log`, `OpenSSL::SSL`, `OpenSSL::Crypto` | — |
| `fixpp_session` | STATIC | `fixpp_core`, `fixpp_sync`, `fixpp_dictionary`, `fixpp_wire`, `fixpp_transport`, `fixpp_log`, `asio::asio` (+ `fixpp_otel`, **conditional on `FIXPP_BUILD_OTEL`**, `src/session/CMakeLists.txt:55`) | — |

**Minimum export set** (transitive closure of what the real client links): `fixpp_core`, `fixpp_sync`, `fixpp_log`, `fixpp_wire`, `fixpp_dictionary`, `fixpp_tls`, `fixpp_transport`, `fixpp_session`.

**Conditionally present**: `fixpp_otel`, `fixpp_log_otlp` (`if(TARGET opentelemetry-cpp::api)`), `fixpp_dict_dispatch_bridge`.

**Decision**: export the closure above plus the `fixpp::dict::<ver>` INTERFACE targets. The umbrella `fixpp::fixpp` links `fixpp_session` (which transitively pulls the rest). **The export set is not a fixed list** — it varies with build options, so the generated config must be produced from what was actually built rather than from a hardcoded enumeration.

**Alternatives rejected**: a headers-only INTERFACE umbrella — provably insufficient, since the real client links three STATIC targets directly (settled as FR-010a). Exporting only `fixpp_session` without its dependencies — CMake rejects an export set whose members reference non-exported targets (FR-008).

---

## R3 — `find_dependency` set: **OpenSSL, asio, pugixml + conditional opentelemetry-cpp.** ZLIB is NOT required.

**This corrects `spec.md` FR-010c**, which listed ZLIB on the strength of the real client's link line.

ZLIB appears only in `tests/transport/CMakeLists.txt:18` and `perf/CMakeLists.txt:19,24-25`. **No fixpp target links ZLIB.** Those harnesses add it because they resolve OpenSSL by raw `find_library(... NO_DEFAULT_PATH)`, which bypasses the imported target's own interface; a consumer using the `OpenSSL::SSL` / `OpenSSL::Crypto` imported targets gets any compression dependency transitively.

**Decision**: `fixppConfig.cmake` calls `find_dependency` for OpenSSL, asio, and pugixml, plus opentelemetry-cpp **only when the package was built with telemetry enabled**. ZLIB is deliberately omitted.

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

**Decision**: keep the clarified answer (CI artifacts only). It is a *subset* of what Article IV §5 permits — the article allows attaching to GitHub releases; this feature does not set that up. That is a scope choice, not a conflict. **Recorded so review does not read the narrower behaviour as contradicting Article IV §5.**

---

## R7 — Static-link ordering must be carried by the export, not by the consumer

`perf/CMakeLists.txt:56-57`: *"fixpp libs FIRST (immediately before OpenSSL) so static-link order resolves the OpenSSL symbols `libfixpp_tls.a` references."* Both the perf driver and `tests/interop` reproduce this ordering by hand.

**Decision**: the exported targets must declare their dependencies such that CMake's own topological ordering produces a correct link line, so no consumer restates it. This is what target-level dependency declaration is for; the hand-ordering in the harnesses exists because they link raw archive paths rather than targets. **This is FR-010b, and the real-client witness is the only check that can confirm it** — a header-only consumer never links and cannot detect a wrong order.

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

It is gated `FIXPP_BUILD_INTEROP_PERF` (default OFF), so the witness enables it explicitly.

**Inverted caution**: the driver is documented as needing an in-tree build so it links freshly generated libraries rather than a stale prebuilt one. Building it against an installed package is a deliberate inversion, safe **only** because the package comes from the build under test — which FR-021a enforces via provenance. The hazard is live rather than theoretical because `artifacts/` deliberately survives the build-tree deletion cycle, so older packages persist alongside current ones.

---

## R10 — CPack invocation model: per-configuration, single-config

Both Linux and Windows in-scope presets are single-config Ninja/MSVC-with-explicit-`CMAKE_BUILD_TYPE`. Combined with the serial build-and-delete discipline (only one configuration exists at a time), a multi-config generator would provide nothing.

**Decision**: one CPack invocation per configured build tree, producing that configuration's package set. Configuration is encoded in the artifact name (FR-017).

**Alternative rejected**: a multi-config generator or paired invocations on a shared tree — both require two configurations to coexist, which the storage budget explicitly forbids (Assumption 5, SC-008).

**Staging**: CPack stages into `_CPack_Packages/` inside the build directory, so deleting the tree removes the staged files automatically. `CMAKE_INSTALL_PREFIX` must never point at a system location (FR-020). Finished artifacts are copied to a location outside every build tree (FR-021).

---

## Open items carried into Phase 1

| Item | Disposition |
|---|---|
| Exact upstream-license file layout inside the package | Design in `contracts/`; obligations fixed by FR-018b |
| Whether `NormativeReferences.md` is excluded from the include tree | Candidate cleanup (R1); not a requirement |
| Whether OpenSSL's imported target carries compression transitively in every in-scope configuration | Verified empirically by the real-client link (R3); no config change unless it fails |
