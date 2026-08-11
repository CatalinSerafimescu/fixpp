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
- **I11 (roots contain only their subtree)**: every installed path under `include/capi/` matches
  `^include/capi/fix/`, and every path under `include/service-iface/` matches
  `^include/service-iface/fixpp/service/`. This is a property of the **root**, which is what FR-001 states and
  what no target-property check can observe; the existing packaging gates are anchored on `^include/fixpp/…`
  and structurally cannot see these roots. *(FR-001 / FR-010a; asserted as contract invariant C-5.)*

## E2 — Exported target, classified by consumption role

The by-name / closure-only distinction is the project's own, but it is grounded here in a **measured
predicate**, not in a comment: a member is *closure-only* when no public header names it
(`grep -rn <target> include/`) and no public header instructs linking it. `CMakeLists.txt:608-622` states the
distinction in prose, but it ranges over the **five targets the umbrella does not reach** and classifies only
`fixpp_log_otlp` as closure-only — `capi_objects` is not classified there at all, so that comment cannot be
the authority for the row below. Export-set **membership** is separately by explicit enumeration
(`CMakeLists.txt:596` → `install(TARGETS ${FIXPP_EXPORT_TARGETS} EXPORT fixppTargets)` at `:770`).

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
- **I8 (compile-only, and inverted outside the build)**: ❌ cells are asserted by **compilation**, never by a
  build that links. A link stage fails and succeeds for reasons unrelated to include reachability — measured:
  the research probe's umbrella row reported a false ❌ caused by an unresolved symbol while every `#include`
  resolved. A ❌ cell also cannot be a **build target** in the consumer sub-project at all: its driver
  `FATAL_ERROR`s on any non-zero build exit (`tests/consumer/run_consumer_witness.cmake:100-108`), so a
  must-fail target would red the whole witness. The result must be inverted where the build cannot see it —
  at consumer-**configure** time. *(FR-008 / FR-006a; research R5; mechanism in
  `contracts/include-interface.md` §4a.)*

## E4 — Build-tree vs install-tree interface

Every isolated target carries **two** generator expressions, and only one of them changes.

| Expression | Value | Changes? |
|---|---|---|
| `$<BUILD_INTERFACE:…>` | `${CMAKE_SOURCE_DIR}/include` — permissive | **no** |
| `$<INSTALL_INTERFACE:…>` | the isolated root | **yes — this is the feature** |

**Invariants**

- **I9 (in-tree untouched)**: the isolation is an *installed-interface* property. In-tree, the directory-scoped
  `include_directories("${CMAKE_SOURCE_DIR}/include")` at `CMakeLists.txt:234` covers the whole build — over
  **all 28** `tests/capi/*.cpp` regardless of include spelling, so the **11** whose compilation depends on the
  `-I` search for a `fixpp/` header (**6** spelling it `<fixpp/…>`, **5** only `"fixpp/…"`) keep two
  independent reasons to compile. *(FR-005. The figure was "6 of 28" — the angle-bracket count — until Gate A
  r2; it named a subset of the dependent set, and the directory-scoped argument never depended on it.)*
- **I10 (lint unchanged, and narrower than the documents claim)**: in-tree layering enforcement remains
  `tools/check_layers.py` (`architecture.md`:509). Its `"capi"` and `"service"` rule rows are untouched by this
  feature. **What it actually is**: a *source include-edge lint* — it walks `#include` lines in
  `src/**/*.{cpp,hpp,h}` and `bindings/**/*.{cpp,hpp,h}` against an allowed-edge whitelist and exits 1 on any
  violation (`tools/check_layers.py:2-7`, `:173-176`). It reads **no** CMake target links, so it cannot reject
  "a target that links both `fixpp` and `fixpp::capi`" as `architecture.md:543`, `CMakeLists.txt:615` and
  `tests/consumer/CMakeLists.txt:75-79` all claim; and it never sees an installed consumer, which is why #218
  went uncaught in-tree (issue text, "Why it was not caught in-tree"). Correcting those three sites is FR-014. *(Amended at `/simplify`: FR-014 is written as an UNBOUNDED prohibition — "no statement … may remain untrue" — and a three-site list is not a discharge of it. Re-run BY PREDICATE over `.specify/*.md`, `tools/`, `src/`, `tests/`, `bindings/` and the root `CMakeLists.txt`, the live census is SIX: the three above plus `.specify/api-contract.md:223`, `.specify/architecture.md:131` and the `:509` row at `:519`, and `tests/consumer/consumer_capi_witness.cpp`. `api-contract.md:223` mattered most — left uncorrected it would have asserted the exact opposite of the `architecture.md` §8 text this feature rewrote, i.e. a contradiction between two `.specify`-tier normative docs that 086 itself would have created. Occurrences inside `specs/0NN-*/` historical bundles and the frozen Phase-2 design docs `.specify/2j-*.md` / `.specify/2m-*.md` are inherited-from records, not live claims, and are deliberately left.)*
