# Contract — Installed include interface per exported target

**Feature**: 086-capi-include-isolation · **Date**: 2026-08-03

What a consumer of the **installed package** can and cannot `#include`, per target it links. This is the
contract `.specify/architecture.md` §7.4:503 asserted and the package did not deliver
([#218](https://github.com/CatalinSerafimescu/fixpp/issues/218)).

Scope: the **installed** interface. In-tree include behaviour is out of scope and unchanged (`data-model.md`
I9/I10).

---

## 1. Normative reachability

`find_package(fixpp REQUIRED)`, then `target_link_libraries(app PRIVATE <target>)` and nothing else — no
include directories, no library paths, no other hints.

| Header group | `fixpp::capi` | `fixpp::service` | `fixpp::fixpp` |
|---|---|---|---|
| `<fix/c_api.h>` | **MUST** resolve | **MUST** resolve | **MUST** resolve |
| `<fix/c_api/*.h>` — all 11 | **MUST** resolve | **MUST** resolve | **MUST** resolve |
| `<fixpp/service/control_plane_factory.hpp>` | **MUST NOT** resolve — its own probe, §4 | **MUST** resolve | **MUST** resolve |
| `<fixpp/wire/…>`, `<fixpp/session/…>`, `<fixpp/dict/…>`, `<fixpp/core/…>`, `<fixpp/tls/…>`, `<fixpp/transport/…>`, `<fixpp/log/…>`, `<fixpp/tap/…>`, `<fixpp/v4x/…>` | **MUST NOT** resolve | **MUST NOT** resolve | **MUST** resolve |

The **eleven** sub-headers, named so "all 11" is checkable rather than approximate:
`decimal.h`, `dict.h`, `engine.h`, `error.h`, `export.h`, `handles.h`, `log.h`, `message.h`, `otel.h`,
`session.h`, `version.h` — plus the entry header `fix/c_api.h`. **12 files total.**

> **The census is derived, not transcribed.** `find include/fix -type f | sort` produces exactly the list
> above; `specs/084-packaging-cpack-export/contracts/package-layout.md` §2a (`:132`) independently records
> "`include/fix/` + `include/fix/c_api/` (12 files)". `store.h` — named among the *designed* domain-split
> headers at `.specify/2i-capi.md:133` — is **not** part of this set and not a gap: the C-ABI surface is DONE
> (CA-001..010) and GA-frozen at `1.5.0`, additive-only
> (**[parent-repo]**`/REMAINING-WORK.md:7`; **[parent-repo]** is the parent research repo, absolute root in
> `spec.md` → Normative References → "Cross-repository citations"), and `2i-capi.md:93` assigns the store
> *function* surface to design doc **2e**. Any implementation of this contract MUST re-derive the list by
> command rather than copying it.

### 1a. Targets this contract does NOT bind

`fixpp::capi_objects` and `fixpp::log_otlp` are **closure-only** export members: no public header names them
(`grep -rn capi_objects include/` → 0 hits) and nothing instructs anyone to link them — the only "link
`fixpp::X`" instruction in any public header is `include/fixpp/config/toml_config_loader.hpp:7-8`, for
`fixpp::config_toml`. They keep their present whole-tree interfaces. A consumer that links them directly is
outside the documented consumption path and gets no isolation guarantee. *(FR-003a — the boundary was set
deliberately at clarify; narrowing `fixpp::capi_objects` would cascade into the in-tree graph and into the
export-closure coupling that makes `find_package` `FATAL_ERROR`.)*

> **On `CMakeLists.txt:575-585`.** Earlier drafts cited that comment as the taxonomy. It classifies the **five
> targets the umbrella does not reach** — `fixpp_capi`, `fixpp_config_toml`, `fixpp_tap`, `fixpp_service`,
> `fixpp_log_otlp` — declaring four by-name and **`fixpp_log_otlp` alone** closure-only. `capi_objects` is not
> classified there at all. The classification above is unchanged and correct; its basis is the measured
> predicate, not that comment. `capi_objects`' export-set membership is likewise by **explicit enumeration**
> (`CMakeLists.txt:562`, consumed by `install(TARGETS ${FIXPP_EXPORT_TARGETS} EXPORT fixppTargets)` at `:733`),
> not by closure inference.

## 2. Delivered mechanism

| Target | `INTERFACE_INCLUDE_DIRECTORIES` (installed) | `INTERFACE_LINK_LIBRARIES` (installed) |
|---|---|---|
| `fixpp::capi` | `${_IMPORT_PREFIX}/include/capi` | `$<LINK_ONLY:fixpp::capi_objects>` |
| `fixpp::service` | `${_IMPORT_PREFIX}/include/service-iface` | `fixpp::capi` |
| `fixpp::fixpp` | `${_IMPORT_PREFIX}/include` | `fixpp::session` |

`$<LINK_ONLY:>` is the whole mechanism: the objects target is still linked, so every symbol still resolves,
but its include directories are **not** inherited. Measured, not inferred — `research.md` R3.

`fixpp::service` reaches the C-ABI headers through its existing link to `fixpp::capi`; it declares no C-ABI
root of its own.

### 2a. Source changes required

| File | Change | Why it is named |
|---|---|---|
| `src/capi/CMakeLists.txt:46` | `PUBLIC` → `PRIVATE`, plus a new `target_include_directories(fixpp_capi PUBLIC …)` | the transitive path #218 identifies |
| `src/service/CMakeLists.txt:12` | `$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>` → the service-iface root | **not** inherited from `fixpp_capi`, so narrowing that target does not touch it; every other requirement can be satisfied while this line survives (FR-011d) |
| `CMakeLists.txt` (near `:446-451`) | **two added** `install(DIRECTORY …)` rules | the new roots |
| `CMakeLists.txt:446-451` | **unchanged** — acquires **no new** `PATTERN … EXCLUDE` for the isolated subtrees *(it already carries two, for `fixpp/core/test` and `fixpp/transport/test`, `:449-450` — this feature adds none)* | FR-005a additivity |
| `tests/consumer/CMakeLists.txt` | `project(fixpp_consumer_witness CXX)` (`:40`) → `C CXX`; + the compile-only positive probe targets; + the configure-time must-fail probes (§4, **three** of them: `<fixpp/wire/parser.hpp>` and `<fixpp/service/control_plane_factory.hpp>` through `fixpp::capi`, `<fixpp/wire/parser.hpp>` through `fixpp::service`); + the usage-requirement probe target and its `file(GENERATE)` | the C-side installed-interface gap (FR-002); the negative cells, which cannot be targets in this sub-project (§4); and C-3 leg 3 |
| `tests/consumer/run_consumer_witness.cmake` | + a read-back and compare of the generated usage-requirement file, **after** the `cmake --build` at `:96-104` | FR-009a(ii) / C-3 leg 3 — `file(GENERATE)` writes at generate time, so the assertion has to live downstream of the sub-build or it does not exist |
| `tests/consumer/consumer_capi_witness.cpp` | + a reference to an entry point that pulls the session/dictionary closure **at link time** — a namespace-scope, non-`static`, non-`const` pointer initialised with its address, or a call; **not** an address assigned to an unused local, which can be optimised away together with its relocation | FR-009 — as it stands the witness passes even if the transitive archive edge is lost. This TU is built and linked, **never run** (`tests/consumer/CMakeLists.txt:71`), so the reference carries no runtime obligation |
| `tests/packaging/run_package_contents_witness.cmake` | + presence assertions (FR-010) **and** the isolated-root containment assertions (FR-010a / C-5) | the existing gates' regexes are anchored on `^include/fixpp/…` and cannot see the new roots |

## 3. Invariants a change must preserve

- **C-1 Additive.** The installed path set is a strict superset of the pre-feature set. Verified by comparing
  **produced artifacts** — the staged-install manifests of `quickstart.md` §2, each from a prefix created empty
  — never by reading install rules. §8's packaging witness carries the same property at the CPack-package
  level. *(FR-005a / SC-003a.)*
- **C-2 Export closure stable.** 18 members before, 18 after; `fixpp::capi_objects` stays a member and the
  shipped `lib/objects-<CONFIG>/**` files stay valid. Re-measured from a real generate run. *(FR-016.)*
- **C-3 Nothing but the include path and the enumerated, unreachable definition set is withheld.** Narrowing an
  *include* interface must not narrow the *link* interface **or any other usage requirement the closure relies
  on**, except for a set enumerated and recorded in advance. `$<LINK_ONLY:>` withholds
  `INTERFACE_COMPILE_DEFINITIONS`, `INTERFACE_COMPILE_OPTIONS`, `INTERFACE_COMPILE_FEATURES` and
  `INTERFACE_SYSTEM_INCLUDE_DIRECTORIES` too — and the closure carries **at least two** live PUBLIC
  definitions: `FIXPP_LOG_MIN_LEVEL` (`src/log/CMakeLists.txt:27`), consumed unguarded at
  `include/fixpp/log/logger.hpp:275,301,333`, and `ASIO_STANDALONE`, carried by `asio::asio`, which is linked
  **unwrapped** inside the C-ABI closure. The complete set is enumerated per FR-009a(ii)(a) and membership is
  decided by the predicate, not by this list. An installed C-ABI consumer **does** lose them (measured,
  `research.md` R10); nothing reaches `logger.hpp` from such a consumer today, so there is no live break, and
  that loss is the enumerated exception this invariant's title names. The invariant exists so any *other*
  withheld requirement, or a future header that did reach one, would not pass silently.

  > **Asserted THREE ways, on three different instruments — none is a substitute for another** *(restated at
  > Gate A r2; the previous two-way formulation was unsatisfiable and its second leg was blind to the property
  > it claimed to check)*:
  >
  > 1. **Link interface** — `fixpp::capi` still links to a working binary (FR-009: build + link of
  >    `consumer_capi_witness`, with a reference that pulls the session/dictionary closure out of the archive).
  > 2. **Direct-property delta** — `fixpp::capi`'s generated `set_target_properties(…)` block changes OFF→ON in
  >    exactly the enumerated ways and no others (FR-009a(i)). The enumeration is **filled in from the
  >    measurement**, not transcribed ahead of it; `research.md` R3 carries the measured shape and
  >    `quickstart.md` §3 the extraction. **Note what the OFF→ON delta actually is**: the include property is
  >    *gained* and `INTERFACE_LINK_LIBRARIES` is *rewritten* — "loses only `INTERFACE_INCLUDE_DIRECTORIES`" is
  >    false of the delivered design and was never satisfiable.
  > 3. **Effective usage-requirement delta at the consumer** — leg 2 is **structurally incapable** of observing
  >    compile definitions: they reach a C-ABI consumer via `fixpp_capi_objects` → `fixpp_log`
  >    (`src/capi/CMakeLists.txt:29-38`) and never appear in `fixpp::capi`'s block, which reads identically
  >    either way. Measured instead at a probe target inside the configured consumer sub-project
  >    (`file(GENERATE … "$<TARGET_PROPERTY:<probe>,COMPILE_DEFINITIONS>")`), compared **after the sub-build**
  >    by the driver — a `file(GENERATE)` nothing reads back asserts nothing. Instrument measured in
  >    `research.md` R10: it reports `FIXPP_LOG_MIN_LEVEL` at ISO=OFF and loses it at ISO=ON. (FR-009a(ii).)
- **C-4 In-tree unchanged.** No source file edited to accommodate the layout; the full suite result matches the
  pre-change baseline on the same host. *(FR-005 / SC-007.)*
- **C-5 The isolated roots contain only what they claim.** Every installed path under `include/capi/` matches
  `^include/capi/fix/`; every path under `include/service-iface/` matches
  `^include/service-iface/fixpp/service/`. This is the **only** assertion that traces **FR-001**, which is a
  property of a *root*, not of a target. The existing content gates cannot cover it: the denylist anchors on
  `^include/fixpp/…` (`tests/packaging/run_package_contents_witness.cmake:484-487`) and the exact-set
  generated-tree check on `^include/fixpp/(v[A-Za-z0-9]+)/…` (`:508`), so neither regex can ever match a path
  under the new roots. Without C-5, a partial or over-broad duplication under an isolated root passes every
  gate unless a negative probe happens to name the duplicated header. *(FR-010a.)*

## 4. How this contract is asserted

> ### ⚠️ Two rules that decide whether the assertion is worth anything
>
> **A "MUST NOT resolve" cell is asserted by COMPILATION, never by a build that links.** A link stage fails
> for reasons unrelated to include reachability. Measured: the research probe reported a false failure on the
> umbrella row caused by an unresolved symbol while every `#include` resolved correctly (`research.md` R5).
> The probe fell into the trap this feature exists to avoid — on its first attempt, against a *correct* design.
>
> **A "MUST resolve" cell can never establish a "MUST NOT resolve" cell.** Under the additive layout
> `<fix/c_api.h>` resolves from *either* root, so observing that it compiles is exactly as consistent with the
> defect being fully present as with it being fixed. Only the **pair**, from the same configured consumer,
> discriminates. Evidence offered for §1 MUST be the pair. *(FR-008a.)*

### 4a. Where a ❌ cell can physically live — the harness constrains the mechanism

`tests/consumer/` is a **standalone sub-project** driven by `tests/consumer/run_consumer_witness.cmake`, which
stage-installs, configures once, runs **one** `cmake --build`, and raises
`message(FATAL_ERROR "consumer build failed")` on **any** non-zero build exit (`:96-104`). A ❌ probe expressed
as a *build target that must fail* therefore reds the whole witness: **the assertion cannot be a target in this
sub-project at all.** (This is why the earlier "`OBJECT` library that must fail to build" wording was
unimplementable — `/speckit-tasks` would have emitted a task that cannot be built.)

**The property the mechanism must have** (FR-006a): evaluate the ❌ cell in a context where the target's usage
requirements propagate exactly as they do to a real consumer target, **compile without linking**, and **invert**
the result so that *compiling* is the failure.

**Instance to implement — MEASURED, `research.md` R9**: `try_compile(<var> ... LINK_LIBRARIES fixpp::capi)`
evaluated at **consumer-configure time**, with `CMAKE_TRY_COMPILE_TARGET_TYPE` set to `STATIC_LIBRARY` for the
duration (compile-only — no link stage, no `main()` required, which is exactly R5's rule) and **restored
afterwards** so it does not leak into any `check_*` module in the same scope. `<var>` must be **FALSE**; a TRUE
result raises `FATAL_ERROR`, which fails the *configure* step and is caught by the driver at `:91-92`. This
keeps both the ✅ and the ❌ cells inside **one configured consumer**, which is what FR-008a's paired-evidence
rule requires.

R9 ran exactly this shape against the Phase-0 fixture at both stages and establishes the three things the
mechanism depends on: `LINK_LIBRARIES <imported target>` **does** propagate that target's
`INTERFACE_INCLUDE_DIRECTORIES`; `STATIC_LIBRARY` makes it genuinely compile-only, so R5's link-stage confound
cannot recur; and the pair **discriminates** — the negative probe returns TRUE at ISO=OFF and FALSE at ISO=ON.
This is no longer an unverified decision rule. **What R9 does not cover**: the 18-member tree under the Conan
toolchain, the real `tests/consumer/` sub-project, and MSVC.

**Fallback, if FR-007's demonstrated-red observation does not go red under a reverted isolation in the real
tree** — a dedicated probe sub-project with its own `cmake -P` driver asserting a **non-zero** build result,
mirroring the existing `execute_process` + `RESULT_VARIABLE` shape at `run_consumer_witness.cmake:96-104`.
R9 removes the *reason to expect* this fallback; it does not remove the obligation to check. **FR-007 is still
the decider**, and R9's fixture-level red is not a substitute for observing the *shipped* witness red.

✅ cells need no inversion and stay ordinary **compile-only targets** (`OBJECT` libraries) in the same
sub-project: a build failure reds the witness, which is the correct polarity.

| Assertion | Kind | Target |
|---|---|---|
| `fixpp::capi` links and resolves a real symbol **from the transitive archive set** | **link only** | `consumer_capi_witness` — **exists**, extended per FR-009. **Building and linking IS the assertion** (`tests/consumer/CMakeLists.txt:71`); the driver runs only `${_sub_build}/consumer_witness` (`run_consumer_witness.cmake:110`, `^PASS:` at `:142-143`), so this binary is **never executed** and no runtime behaviour is asserted. The added reference must pull the entry point's object out of the archive at *link* time — a namespace-scope, non-`static`, non-`const` pointer initialised with its address (or a call), never an address assigned to an unused local, which can be elided with its relocation |
| `fixpp::capi` reaches all 12 C-ABI headers, from **C++** | compile-only target | new |
| `fixpp::capi` reaches all 12 C-ABI headers, from **C** | compile-only target, C language | new — `project(... C CXX)`; closes US1's "C or C++ integrator" promise for the *installed* interface (in-tree C-cleanliness is already pinned at `tests/capi/CMakeLists.txt:13`, `:23`) |
| `fixpp::capi` does **not** reach a C++ engine header (`<fixpp/wire/parser.hpp>`) | configure-time `try_compile`, **asserted FALSE** (§4a) | new — C++ only; a C compiler rejecting a C++ header proves nothing about isolation |
| `fixpp::capi` does **not** reach the **service plugin header** `<fixpp/service/control_plane_factory.hpp>` | configure-time `try_compile`, **asserted FALSE** (§4a) | **new** — a **distinct** §1 matrix cell, provisioned by nothing before Gate A r2. It is not covered by the engine-header probe above: the service header is the one C++ header this feature deliberately republishes at a *second* installed root, so a mis-wired `fixpp::capi` that picked up `include/service-iface` would leak this cell while the `<fixpp/wire/parser.hpp>` probe still passed. Measured FALSE at ISO=ON in `research.md` R4 row 4 — carried into the harness here |
| `fixpp::capi`'s effective usage requirements lose nothing but the enumerated, unreachable definition set | `file(GENERATE)` on a probe target + **read-back and compare in the driver, after the sub-build** | **new** — C-3 leg 3 / FR-009a(ii). Instrument measured in `research.md` R10. The read-back is not optional: `file(GENERATE)` writes at generate time and a `file(GENERATE)` nothing compares asserts nothing |
| `fixpp::service` reaches the plugin header + the C ABI | compile-only target | new |
| `fixpp::service` does **not** reach a C++ engine header | configure-time `try_compile`, **asserted FALSE** (§4a) | new |
| `fixpp::fixpp` reaches `<fix/c_api.h>` **and** `<fixpp/service/control_plane_factory.hpp>` | compile-only target | **new** — `consumer_witness.cpp:34-37` includes neither, so FR-004's C-ABI leg, US3 scenario 2 and **FR-011c** are witnessed by nothing today |
| `fixpp::fixpp` reaches the C++ engine surface | compile + link + run | `consumer_witness` — **exists, unchanged** (SC-003 requires exactly that; the new umbrella probe above is a *separate* TU precisely so this one is not edited) |
| The headers ship at every delivered path | package content | extends the packaging witness (FR-010) |
| The isolated roots contain **only** their declared subtree | package content | extends the packaging witness (FR-010a / C-5) — the only assertion tracing FR-001 |

Each **must fail** assertion carries a demonstrated-red obligation: observed failing with **its own** isolation
removed (per FR-011e the service demonstration reverts `src/service/CMakeLists.txt:12` alone), passing with it
present, both recorded with the command, the exit code and the first diagnostic line, in
`.specify/decisions/086-capi-include-isolation-verify.md` (FR-007). The probe header must be one whose own
disappearance would itself be a defect, so the assertion cannot pass for the wrong reason (FR-008).

## 5. Consumer-visible compatibility

- **No include spelling changes.** `<fix/c_api.h>` is written exactly as today from every target.
- **No `find_package` changes.** Same call, same target names.
- **Non-CMake consumers keep working.** A bare `-I<prefix>/include` with `#include <fix/c_api.h>` resolves
  before and after, because `include/fix/` never moves. This is what the additive layout buys, and it is why
  this feature ships no migration note. *(Clarified 2026-08-03.)*
- **A consumer that links BOTH the umbrella and an isolated target — the stated outcome.** Both roots land on
  the search path. The include spelling is identical from either, so exactly **one** root resolves any given
  `#include` in any given translation unit, and since both roots are installed from one source directory
  (I2) the bytes are the same either way — the consumer sees no difference. **The package does not, and
  cannot, reject the combination**: nothing in an installed CMake package can observe which targets a consumer
  links together. `architecture.md:509` / §7.4:543 present this as rejected, but that rejection is an
  **in-tree convention** — and even in-tree it is not mechanically enforced, because `tools/check_layers.py`
  is a source include-edge lint over `src/**` and `bindings/**` (`:2-7`, `:173-176`) with no notion of a link
  interface. Stated here because `spec.md`'s edge case requires it to be stated rather than left implicit.
