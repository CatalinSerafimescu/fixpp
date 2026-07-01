# Implementation Plan: Behavioral Reify / Typed-Read Round-Trip Unblock (057)

**Branch**: `057-behavioral-reify-unblock` | **Date**: 2026-07-01 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/057-behavioral-reify-unblock/spec.md`

## Summary

Lift the 003-deferred **behavioral reify** (R6 / L-003-1): make `dict::reify()` / `dict::reify_as<Msg>()`
return **live** owning handles for parsed application, multi-char, and FIXT-admin frames instead of the
`dict_reify_wire_body_not_ready` placeholder. The root blocker is **build-architecture, not decode logic**
— the decode primitives are all live (the R6 frozen `message_view_contract.hpp` stub was retired at the
004 cutover / T028; it now re-exports the real `parser.hpp` `MessageView<Index>`, so `bytes()`,
`get<Tag>()`, `Framer`-rebuild all work), but shipped `src/dictionary/reify.cpp` may not `#include` the
build-tree-generated `_dispatch/*.hpp` where the dispatch switches live (arch §2.4 v0.3 / NFR-003-8).

Technical approach (research.md D-1..D-7):
1. **Dispatch bridge** (D-1): a shipped **declaring** header `include/fixpp/dict/reify_dispatch_bridge.hpp`
   + a single **generated-aware TU** (the only TU that includes `<fixpp/_dispatch/reify_dispatch_*.hpp>`)
   that defines two free functions delegating to the inline `dispatch::dispatch_{fixt,application}` helpers;
   `reify.cpp` calls those at its two placeholder sites. Wired via a new `fixpp_dict_dispatch_bridge` target
   that consumes the **already-existing** `fixpp::dict::dispatch` INTERFACE target's `_codegen` include dir.
2. **Simplified handle** (D-2, revised post-advisor): `owning_message_handle` stores
   `{resolved_message_version, pmr::vector<byte> bytes_, lazy view_cache_}` — **no type-erasure**, because
   its entire in-scope surface (`version`/`msg_type`/`view`/`field_value`) is untyped and `as<Msg>()` stays
   T059-stubbed. A fallible factory deep-copies the frame span (`bad_alloc → dict_reify_oom`), mirroring
   `owning_<Msg>::from_view`.
3. **Emitter** (D-3/D-4): `tools/codegen/fixpp-codegen/emit_dispatch.cpp` — replace the placeholder arm body
   with the handle factory call (both application + FIXT emitters); **remove the `size()>1` guard and the
   `size()!=1` skip**, emitting **two-level length-first dispatch** (single-char `switch(mt)` for len==1; a
   `switch` on the packed `uint16(c0<<8|c1)` for len==2). Multi-char generated arms: v44=68, v50sp2=210
   (v42=0, FIXT-admin=0).
4. **`reify_as<Msg>`** (D-5): define inline in `reify.hpp`, delegating directly to
   `owning_message_t<Msg>::from_view` (no bridge — `Msg` is compile-time known), guarded by the contract-
   required `dict_reify_msg_type_mismatch` check.
5. **Tests** (D-6): remove the `FIXPP_R6_WIRE_BODY_READY` guards (defined nowhere → currently skipping),
   flip the R6 positive-oracle asserts from `== dict_reify_wire_body_not_ready` to live-handle asserts, and
   add **discriminating per-field witnesses** using the existing `make_nos_frame()`/`make_frame_view()`
   helpers (+ new `make_*_frame()` siblings for the v42/v50sp2/FIXT-admin/multi-char SC-002 paths).
6. **Docs** (FR-011): flip L-003-1 deferred→shipped; update the 003 §11 R6 ref + the D-008/OSS-010 catalogue
   supplemental notes.

## Technical Context

**Language/Version**: C++23 (engine + generated headers + bridge TU); host-side codegen tool is C++23.
**Primary Dependencies**: existing only — `fixpp::wire` (`MessageView<Index>`/`Framer`/`OffsetTable` via the
`message_view_contract.hpp` re-export), `fixpp::dict` (`resolved_message_version`, `owning_<Msg>` flyweights,
the `fixpp::dict::dispatch` INTERFACE target's generated `_dispatch/` headers). **No new third-party dep.**
**Storage**: N/A (reify deep-copies the frame into a caller-supplied `std::pmr::memory_resource`).
**Testing**: GoogleTest (`tests/dictionary/reify_dispatch_test.cpp`, `tests/support/reify_test_frame.hpp`,
`frame_view_factory.hpp`); mutation-discrimination (revert-arm-to-placeholder → RED); sanitizers
(ASan/UBSan/TSan); codegen determinism + build-graph-clean gates; `check_layers.py` layer-hygiene gate.
**Target Platform**: Linux (Tier 1) primary; Windows/MSVC (Tier 2) + libc++ (Tier 3) unaffected (no
platform-specific code). macOS Tier-4 TBD.
**Project Type**: codegen + runtime dispatch wiring inside a C++23 FIX-engine library.
**Performance Goals**: reify is a **materialize** operation (explicitly exempt from the §XV.1 zero-alloc
parse→fromApp hot path); a single deep-copy into the caller `mr` per call, O(frame size); `view()` re-frame
is lazy/cached. No hot-path change.
**Constraints**: NO new wire/error/public-builder/C-ABI/dependency surface (FR-012); shipped `src/` never
`#include`s a build-tree header (NFR-003-8, `check_layers.py`); regenerated codegen byte-deterministic
(B-003-3, `determinism_test`); build caps max `-j2`, sanitizer presets ONE AT A TIME (WSL2 OOM).
**Scale/Scope**: 1 new shipped header + 1 new generated-aware TU + 1 CMake target; emitter change (2 funcs);
`reify.cpp` (2 delegation sites + 1 error-path fix); `reify.hpp` (handle storage + `reify_as` def);
`check_layers.py` (per-file exempt refinement); test activation + new frame-helper siblings; 3 doc files.

## Constitution Check

*GATE: Must pass before Phase 0 research (initial) and re-checked after Phase 1 design (below).*

This is a **codegen-layout** feature (Appendix A trigger). Mandatory controls: **`/clarify`** (done — one Q,
multi-char folded in), **`/analyze`** (mandatory before `/implement`), **Codex Gate A** (mandatory),
**user `/plan` sign-off** (this document). Gate B + `/speckit-verify` mandatory per Article XVII.

- **Article I §1 — codegen-vs-runtime split.** `dict::reify` runtime-dispatch is explicitly named as
  generated for v42/v44/v50sp2/vt11. This feature makes that generated dispatch *live*; fully aligned.
  Runtime-XML-only versions still fall through to `dict_reify_unknown_msg_type` (L-003-2 preserved). **PASS.**
- **Article III §5 — `tools/` is build-only; codegen at configure.** The emitter edit + configure-time
  regeneration keeps codegen build-tree-only (B-003-3); no runtime dep on tooling. **PASS.**
- **Article VII — TDD + no untested code.** Every arm change lands behind an activated, mutation-tested
  witness (FR-010); RED-first via the discriminating per-field assertions. **PASS by construction.**
  - §7 Fuzzing (parser-touching): reify consumes an **already-parsed/validated** view and re-frames a
    byte-copy via the existing `Framer` (already fuzzed at 004); no *new* parser surface is introduced, so
    no new fuzz harness is owed. Noted for `/speckit-verify`. **PASS (N/A, rationale recorded).**
- **Article VIII §5 / Article XV §1 — hot-path allocator.** Reify is the "rare materialise" case explicitly
  permitted to allocate from arena/PMR; it is **not** on the parse→fromApp zero-alloc path. The simplified
  handle draws its single deep-copy from the caller `mr` and adds **one** heap pimpl node (no second
  type-erased node — the advisor-driven simplification removes the double-alloc the type-erased design would
  have incurred). **PASS (materialise exemption; documented in the plan).**
- **Article IX — coverage/sanitizers/static.** ≥95% line / ≥85% branch on touched modules (`src/dictionary/`,
  `include/fixpp/dict/`, the bridge TU); ASan/UBSan/TSan Tier-1; clang-tidy/clang-format/cppcheck/iwyu clean.
  Enforced by `/speckit-verify`. **PASS-by-plan.**
- **Article X — ABI Policy.** **N/A** — no `include/fix/c_api/` change, no new `extern "C"` symbol, no new
  `fixpp_error_t` value (FR-012). The reify errors are C++ `core::error` values that already exist. **PASS (N/A).**
- **Article XI — concurrency.** Reify is synchronous; no coroutine/awaitable/mutex added. The bridge header
  and TU include no `asio::awaitable`, so §XV.9 (no `std::mutex` in awaitable headers) is not engaged. **PASS (N/A).**
- **Article XV — banned patterns.** §6 (runtime-only validation banned; constexpr metadata + typed accessors)
  — this feature *delivers* the typed-read path, aligned. §13 (eager codegen w/o runtime path) — the runtime
  `view.get(tag)` path is untouched; hybrid preserved. §18 (no research/decision content in repo) — plan
  artifacts live under `specs/` (spec-kit output), not `research/`/`decisions/`. **PASS.**
- **Article XVI §3/§4 — `/clarify` + `/analyze` mandatory (codegen trigger).** `/clarify` done; `/analyze`
  scheduled before `/implement`. **PASS.**
- **Article XVII — gates.** Gate A (this design), Gate B (PR), `/speckit-verify` (post-implement), §7 local
  build gate all apply and are in the pipeline. **PASS-by-pipeline.**
- **Article XVIII §7 — v1.0 typed-message scope.** Multi-char dispatch makes generated arms for messages
  beyond A-001..A-013 *reifiable at runtime*, but does **not** close their catalogue rows (no builders / no
  per-message typed tests). Making a generated dispatch arm live is runtime access, not a typed-message row
  closure. No scope violation. **PASS (noted).**

**Result: no violations.** Complexity Tracking is empty (the advisor-driven D-2 simplification removed the
only candidate over-engineering). Two design points are flagged for **Gate A** review (not violations):
(1) the `check_layers.py` per-file exempt-include refinement + a discriminating "guard still bites if
`reify.cpp` includes a build-tree header" check; (2) the `owning_message_handle` construction-seam
visibility (private-constructor+friend vs `detail::` factory).

## Project Structure

### Documentation (this feature)

```text
specs/057-behavioral-reify-unblock/
├── plan.md              # This file
├── research.md          # Phase 0 — D-1..D-7 decisions
├── data-model.md        # Phase 1 — handle storage, dispatch bridge, two-level key
├── quickstart.md        # Phase 1 — reify usage + verification recipe
├── contracts/           # Phase 1 — bridge header + handle-factory + reify_as contracts
│   └── reify-dispatch-bridge.md
├── checklists/
│   └── requirements.md  # from /speckit-specify (all-pass)
└── tasks.md             # Phase 2 output (/speckit-tasks — NOT created here)
```

### Source Code (repository root — real paths)

```text
tools/codegen/fixpp-codegen/
└── emit_dispatch.cpp                    # EMITTER: uniform handle-factory arm + two-level dispatch
                                         #   (emit_dispatch_application + emit_dispatch_fixt)

include/fixpp/dict/
├── reify.hpp                            # owning_message_handle storage (bytes_+rmv+lazy view);
│                                        #   reify_as<Msg> inline def; construction seam
└── reify_dispatch_bridge.hpp            # NEW shipped DECLARING header (declares the 2 bridge fns;
                                         #   no build-tree include)

src/dictionary/
├── reify.cpp                            # delegate the 2 placeholder sites to the bridge fns;
│                                        #   handle-factory impl; fix get<35>-absent error path
├── reify_dispatch_bridge.cpp            # NEW generated-aware TU (the ONLY #include of _dispatch/*)
└── CMakeLists.txt                       # new fixpp_dict_dispatch_bridge target; link into fixpp_dictionary

cmake/Codegen.cmake                      # (reuse fixpp::dict::dispatch INTERFACE target; add_dependencies)

tools/check_layers.py                    # per-file exempt-include refinement (bridge TU only; reify.cpp stays guarded)

tests/dictionary/
└── reify_dispatch_test.cpp              # remove FIXPP_R6_WIRE_BODY_READY guards; flip oracle asserts;
                                         #   add discriminating per-field witnesses (incl. AS multi-char)
tests/support/
├── reify_test_frame.hpp                 # add make_*_frame() siblings (v42 / v50sp2 / FIXT-admin / AS)
└── frame_view_factory.hpp              # (reuse make_frame_view)

spec/behaviors-and-limitations.md        # L-003-1 deferred → shipped (FR-011)
spec/feature-catalogue.md                # D-008/OSS-010 R6 supplemental-note update (FR-011)
```

**Structure Decision**: Single-library layout. The one structural addition is the **dispatch bridge**
(declaring header in `include/fixpp/dict/` + generated-aware TU in `src/dictionary/` + a CMake target that
consumes the existing `fixpp::dict::dispatch` include dir). The bridge is the minimal seam that lets shipped
`reify.cpp` reach the generated dispatch without violating NFR-003-8; it reuses the layer-hygiene mechanism
(arch §2.4 v0.3 bridge carve-out + `check_layers.py`) already established at 003/004, refined to a per-file
exempt-include so `reify.cpp` itself stays guarded.

## Complexity Tracking

> No Constitution Check violations — this section is intentionally empty. The one candidate complexity
> (a type-erased `owner_base`/`owner_impl` in `owning_message_handle`) was **rejected** during Phase-0
> review: 057's handle surface is entirely untyped, so byte+version storage suffices and a future
> `as<Msg>()` can materialize from the stored bytes without construction-time type-erasure.
