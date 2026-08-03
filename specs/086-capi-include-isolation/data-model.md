# Data model — 086 C-ABI include isolation

**Date**: 2026-08-03 · **Branch**: `086-capi-include-isolation`

This feature has no runtime data model. Its "entities" are **installed include roots**, **exported targets**,
and the **reachability relation** between them — which is the thing #218 got wrong and the thing every witness
in this feature measures.

---

## E1 — Installed include root

A directory under the install prefix that appears on some target's `INTERFACE_INCLUDE_DIRECTORIES`.

| Root | Contents | Introduced | Install rule |
|---|---|---|---|
| `<prefix>/include` | `fix/` + `fixpp/` — the whole public tree | exists today | `CMakeLists.txt:446-451` — **unchanged**, acquires no exclusion |
| `<prefix>/include/capi` | `fix/` only | **new** | `install(DIRECTORY include/fix/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/capi/fix)` |
| `<prefix>/include/service-iface` | `fixpp/service/` only | **new** | `install(DIRECTORY include/fixpp/service/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/service-iface/fixpp/service)` |

**Invariants**

- **I1 (additive)**: the set of installed file paths after this feature is a strict **superset** of the set
  before it. No path is removed or relocated. *(FR-005a; verified by manifest comparison, SC-003a — not by
  reading install rules.)*
- **I2 (single source)**: each new root is installed from the **same source directory** as its counterpart in
  `<prefix>/include`. The copies cannot drift because there is only one original.
- **I3 (spelling preserved)**: the header's include spelling is identical from every root — `<fix/c_api.h>`
  resolves the same way whether it came from `include/` or `include/capi/`. A consumer therefore never sees
  both; exactly one root on its search path wins, and both hold identical bytes.

## E2 — Exported target, classified by consumption role

`CMakeLists.txt:575-584` already establishes this taxonomy; this feature adopts it rather than inventing one.

| Target | Role | Isolated? | Reachable roots after |
|---|---|---|---|
| `fixpp::fixpp` | by-name — C++ umbrella, the primary public surface | no, by design | `include` |
| `fixpp::capi` | by-name — the C-ABI consumer target | **yes** | `include/capi` |
| `fixpp::service` | by-name — the control-plane plugin surface | **yes** | `include/service-iface` + `include/capi` (via its link to `fixpp::capi`) |
| `fixpp::capi_objects` | **closure-only** — no public header names it, nothing instructs linking it | no (FR-003a) | `include` |
| `fixpp::config_toml` | by-name — out of scope, unrelated surface | no | `include` |
| `fixpp::log_otlp` | closure-only | no | — |
| the other 12 members | closure / C++ engine internals | no | `include` |

**Invariants**

- **I4 (by-name only)**: the isolation obligation binds exactly the targets a consumer is *instructed* to link.
  Closure-only members keep their present interfaces. *(FR-003a.)*
- **I5 (closure stable)**: export-set **membership** is unchanged by this feature — 18 members before and after.
  `$<LINK_ONLY:>` keeps `fixpp_capi_objects` an export requirement (research R2), so the
  `_cmake_import_check_files_for_fixpp::capi_objects` block and the shipped `lib/objects-<CONFIG>/**` files
  stay valid. *(Predicted from the repro; **must be re-measured** on a real generate run — FR-016.)*

## E3 — Reachability relation

`reachable(target, header)` — whether a translation unit that links only `target`, from an installed package,
can `#include` `header`. This is the relation the whole feature is about, and the **only** thing the witnesses
assert.

**The normative matrix** (see `contracts/include-interface.md` for the full contract):

| | `<fix/c_api.h>` | `<fixpp/service/…>` | `<fixpp/wire/…>`, `<fixpp/session/…>`, … |
|---|---|---|---|
| `fixpp::capi` | ✅ | ❌ | ❌ |
| `fixpp::service` | ✅ | ✅ | ❌ |
| `fixpp::fixpp` | ✅ | ✅ | ✅ |

**Invariants**

- **I6 (transitive)**: reachability is evaluated over the **transitive** interface — every root reachable
  through every target in `INTERFACE_LINK_LIBRARIES`, recursively. #218 exists precisely because the direct
  property was empty while the transitive one was open. A check that reads one target's
  `INTERFACE_INCLUDE_DIRECTORIES` does not measure this relation. *(FR-003.)*
- **I7 (paired evidence)**: a ✅ cell alone can never establish a ❌ cell. Under the additive layout
  `<fix/c_api.h>` resolves from *either* root, so observing that it compiles is equally consistent with
  isolation being fully absent. Only the **pair** — the ✅ and the ❌ from the same configured consumer —
  discriminates. *(FR-008a; this is the additive-layout form of the original #218 trap.)*
- **I8 (compile-only)**: ❌ cells are asserted by **compilation**, never by a build that links. A link stage
  fails and succeeds for reasons unrelated to include reachability — measured: the research probe's umbrella
  row reported a false ❌ caused by an unresolved symbol while every `#include` resolved. *(FR-008; research
  R5.)*

## E4 — Build-tree vs install-tree interface

Every isolated target carries **two** generator expressions, and only one of them changes.

| Expression | Value | Changes? |
|---|---|---|
| `$<BUILD_INTERFACE:…>` | `${CMAKE_SOURCE_DIR}/include` — permissive | **no** |
| `$<INSTALL_INTERFACE:…>` | the isolated root | **yes — this is the feature** |

**Invariants**

- **I9 (in-tree untouched)**: the isolation is an *installed-interface* property. In-tree, the directory-scoped
  `include_directories("${CMAKE_SOURCE_DIR}/include")` at `CMakeLists.txt:234` covers the whole build, so the
  6 of 28 `tests/capi/*.cpp` that include `<fixpp/…>` keep two independent reasons to compile. *(FR-005.)*
- **I10 (lint unchanged)**: in-tree layering enforcement remains `tools/check_layers.py` (`architecture.md`:509).
  Its `"capi"` and `"service"` rule rows are untouched by this feature — it operates on the in-tree graph and
  structurally cannot see the installed interface, which is why #218 went uncaught (issue text, "Why it was not
  caught in-tree").
