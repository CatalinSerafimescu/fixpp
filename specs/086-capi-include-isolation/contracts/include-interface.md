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
| `<fix/c_api/*.h>` — all 12 | **MUST** resolve | **MUST** resolve | **MUST** resolve |
| `<fixpp/service/control_plane_factory.hpp>` | **MUST NOT** resolve | **MUST** resolve | **MUST** resolve |
| `<fixpp/wire/…>`, `<fixpp/session/…>`, `<fixpp/dict/…>`, `<fixpp/core/…>`, `<fixpp/tls/…>`, `<fixpp/transport/…>`, `<fixpp/log/…>`, `<fixpp/tap/…>`, `<fixpp/v4x/…>` | **MUST NOT** resolve | **MUST NOT** resolve | **MUST** resolve |

The twelve sub-headers, named so "all 12" is checkable rather than approximate:
`decimal.h`, `dict.h`, `engine.h`, `error.h`, `export.h`, `handles.h`, `log.h`, `message.h`, `otel.h`,
`session.h`, `version.h` — plus the entry header `fix/c_api.h`. **13 files total.**

### 1a. Targets this contract does NOT bind

`fixpp::capi_objects` and `fixpp::log_otlp` are **closure-only** export members by the project's existing
taxonomy (`CMakeLists.txt:575-584`): no public header names them and nothing instructs anyone to link them.
They keep their present whole-tree interfaces. A consumer that links them directly is outside the documented
consumption path and gets no isolation guarantee. *(FR-003a — the boundary was set deliberately at clarify;
narrowing `fixpp::capi_objects` would cascade into the in-tree graph and into the export-closure coupling that
makes `find_package` `FATAL_ERROR`.)*

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
| `CMakeLists.txt:446-451` | **unchanged** — acquires no `PATTERN … EXCLUDE` | FR-005a additivity |

## 3. Invariants a change must preserve

- **C-1 Additive.** The installed path set is a strict superset of the pre-feature set. Verified by comparing
  **produced package manifests**, never by reading install rules. *(FR-005a / SC-003a.)*
- **C-2 Export closure stable.** 18 members before, 18 after; `fixpp::capi_objects` stays a member and the
  shipped `lib/objects-<CONFIG>/**` files stay valid. Re-measured from a real generate run. *(FR-016.)*
- **C-3 Symbols still resolve.** Narrowing an *include* interface must not narrow the *link* interface.
  `fixpp::capi` must still link to a working binary. *(FR-009.)*
- **C-4 In-tree unchanged.** No source file edited to accommodate the layout; the full suite result matches the
  pre-change baseline on the same host. *(FR-005 / SC-007.)*

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

| Assertion | Kind | Target |
|---|---|---|
| `fixpp::capi` links and resolves a real symbol | link + run | `consumer_capi_witness` — **exists**, unchanged in intent (FR-009) |
| `fixpp::capi` reaches all 13 C-ABI headers | compile-only | new |
| `fixpp::capi` does **not** reach a C++ engine header | compile-only, **must fail** | new |
| `fixpp::service` reaches the plugin header + the C ABI | compile-only | new |
| `fixpp::service` does **not** reach a C++ engine header | compile-only, **must fail** | new |
| `fixpp::fixpp` reaches everything | compile + link | `consumer_witness` — **exists**, unchanged (FR-004) |
| The headers ship at every delivered path | package content | extends the packaging witness (FR-010) |

Each **must fail** assertion carries a demonstrated-red obligation: observed failing with the isolation
removed, passing with it present, both recorded with the commands that produced them (FR-007). The probe header
must be one whose own disappearance would itself be a defect, so the assertion cannot pass for the wrong reason
(FR-008).

## 5. Consumer-visible compatibility

- **No include spelling changes.** `<fix/c_api.h>` is written exactly as today from every target.
- **No `find_package` changes.** Same call, same target names.
- **Non-CMake consumers keep working.** A bare `-I<prefix>/include` with `#include <fix/c_api.h>` resolves
  before and after, because `include/fix/` never moves. This is what the additive layout buys, and it is why
  this feature ships no migration note. *(Clarified 2026-08-03.)*
