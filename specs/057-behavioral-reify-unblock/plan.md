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
1. **Dispatch bridge** (D-1, build-tree pivot): a **private same-module** declaring header
   `src/dictionary/reify_dispatch_bridge.hpp` (NOT a shipped/public `include/` header; it includes no
   build-tree header — only the already-available `owning_message_handle` / `MessageView` types) declaring two
   `fixpp::dict` free functions; `reify.cpp` includes only this private header and calls the two functions at
   its placeholder sites. The **generated-aware bridge TU** (the only TU that `#include`s
   `<fixpp/_dispatch/reify_dispatch_*.hpp>`) is **NOT a shipped `src/` file** — it is materialized at configure
   time from a repo-checked-in template `cmake/templates/reify_dispatch_bridge.cpp.in` via `configure_file`
   into `${CMAKE_BINARY_DIR}/_codegen/reify_dispatch_bridge.cpp` (the template body is FIXED / version-
   independent: two wrappers delegating to `dispatch::dispatch_{fixt,application}`, so `configure_file` is
   sufficient — no new emitter surface). It compiles into a new private `fixpp_dict_dispatch_bridge` STATIC
   target that consumes the **already-existing** `fixpp::dict::dispatch` INTERFACE target's `_codegen` include
   dir (`add_dependencies(... fixpp_codegen_generate)`), linked into `fixpp_dictionary`. **Consequence: no
   shipped `src/**` file includes a build-tree header → NFR-003-8 satisfied literally, WITHOUT extending the
   arch §2.4 ratified carve-out and WITHOUT an amendment.** `check_layers.py` does not scan the build-tree TU
   (correct — build-tree code is where build-tree includes belong); `reify.cpp` + the private declaring header
   remain under its scan and stay guarded.
2. **Simplified handle** (D-2, revised post-advisor): `owning_message_handle` stores
   `{resolved_message_version, pmr::vector<byte> bytes_, lazy view_cache_}` — **no type-erasure**, because
   its entire in-scope surface (`version`/`msg_type`/`view`/`field_value`) is untyped and `as<Msg>()` stays
   T059-stubbed. A fallible factory deep-copies the frame span (`bad_alloc → dict_reify_oom`), mirroring
   `owning_<Msg>::from_view`.
3. **Emitter** (D-3/D-4): `tools/codegen/fixpp-codegen/emit_dispatch.cpp` — replace the placeholder arm body
   with the handle factory call (both application + FIXT emitters); **remove the `size()>1` guard and the
   `size()!=1` skip**, emitting **two-level length-first dispatch** (single-char `switch(mt)` for len==1; a
   `switch` on the packed `uint16(c0<<8|c1)` for len==2). Multi-char generated arms (unique 2-char
   `msg_type_v` = generated message classes with `len(msg_type_v)==2` = one arm each): **v44=34, v50sp2=105**
   (v42=0, FIXT-admin=0). Max MsgType length is 2 (validates the packed-uint16 two-level dispatch).
4. **`reify_as<Msg>`** (D-5): define inline in `reify.hpp`, delegating directly to
   `owning_message_t<Msg>::from_view` (no bridge — `Msg` is compile-time known), guarded by the contract-
   required `dict_reify_msg_type_mismatch` check.
5. **Tests** (D-6): remove the `FIXPP_R6_WIRE_BODY_READY` guards (defined nowhere → currently skipping),
   flip the R6 positive-oracle asserts from `== dict_reify_wire_body_not_ready` to live-handle asserts, and
   add **discriminating per-field witnesses** using the existing `make_nos_frame()`/`make_frame_view()`
   helpers (+ new `make_*_frame()` siblings for the v42/v50sp2/FIXT-admin/multi-char SC-002 paths).
6. **Docs** (FR-011): flip L-003-1 to a **partial** unblock (runtime `reify()` + `reify_as<Msg>` shipped;
   `owning_message_handle::as<Msg>()` typed-downcast half stays AC-R6-deferred / T059-stubbed); update the 003
   §11 R6 ref + the D-008/OSS-010 catalogue supplemental notes accordingly.

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
`#include`s a build-tree header (NFR-003-8, `check_layers.py`) — satisfied literally by the build-tree pivot,
no carve-out extension; regenerated codegen byte-deterministic (B-003-3, `determinism_test`); build caps max
`-j2`, sanitizer presets ONE AT A TIME (WSL2 OOM).
**Scale/Scope**: 1 new private declaring header (`src/dictionary/`) + 1 new CMake template
(`cmake/templates/reify_dispatch_bridge.cpp.in`) + 1 build-tree-generated bridge TU + 1 CMake target; emitter
change (2 funcs); `reify.cpp` (2 delegation sites + 1 error-path fix); `reify.hpp` (handle storage + `reify_as`
def); test activation + new frame-helper siblings + a discriminating "guard still bites" layer-hygiene check
(no `check_layers.py` source change — the pivot needs no exempt); 3 doc files.

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
- **Article VII — TDD + no untested code.** Every **dispatch-path shape** / the emitter change lands behind an
  activated, mutation-tested witness (FR-010); RED-first via the discriminating per-field assertions. The D-7
  structural grep-assert (each regenerated header — application AND FIXT — contains
  `detail::owning_message_handle_from_frame` and no `dict_reify_wire_body_not_ready`) is the effective all-arms
  structural coverage across the uniform emitter body. **PASS by construction.**
  - §7 Fuzzing (parser-touching): reify consumes an **already-parsed/validated** view and re-frames a
    byte-copy via the existing `Framer` (already fuzzed at 004); no *new* parser surface is introduced, so
    no new fuzz harness is owed. Noted for `/speckit-verify`. **PASS (N/A, rationale recorded).**
- **Article VIII §5 / Article XV §1 — hot-path allocator.** Reify is the "rare materialise" case explicitly
  permitted to allocate from arena/PMR; it is **not** on the parse→fromApp zero-alloc path. The simplified
  handle draws its single deep-copy from the caller `mr` and adds **one** heap pimpl node (no second
  type-erased node — the advisor-driven simplification removes the double-alloc the type-erased design would
  have incurred). **PASS (materialise exemption; documented in the plan).**
- **Article IX — coverage/sanitizers/static.** ≥95% line / ≥85% branch on touched modules (`src/dictionary/`,
  `include/fixpp/dict/` for `reify.hpp`); ASan/UBSan/TSan Tier-1; clang-tidy/clang-format/cppcheck/iwyu clean.
  **Generated-arm coverage disposition (pre-empting a `/speckit-verify` coverage RED):** the generated
  `_dispatch/*.hpp` inline arm bodies (now 34 v44 + 105 v50sp2 multi-char arms + the single-char arms), which
  are `#include`d into the bridge TU's object, are **coverage-excluded** as generated headers — consistent with
  prior codegen features and `2c-codegen.md` seam #15b ("representative subset + exhaustive smoke nightly").
  The bridge TU's own ≥95% line target applies only to its **two wrapper functions** (fully exercised). This is
  NOT an FR-013 scope failure — only a representative message set is graded, by design. Enforced by
  `/speckit-verify`. **PASS-by-plan.**
- **Article X — ABI Policy.** **N/A** — no `include/fix/c_api/` change, no new `extern "C"` symbol, no new
  `fixpp_error_t` value (FR-012). The reify errors are C++ `core::error` values that already exist. **PASS (N/A).**
- **Article XI — concurrency.** Reify is synchronous; no coroutine/awaitable/mutex added. The bridge
  header (the private declaring header) and the build-tree TU include no `asio::awaitable`, so §XV.9 (no
  `std::mutex` in awaitable headers) is not engaged. **PASS (N/A).**
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
only candidate over-engineering). Two design points raised at Gate A are now **RESOLVED** (Gate A round 1):
(1) **layer hygiene** — the bridge TU is pivoted into the build tree (generated into `${build}/_codegen/`), so
NO `check_layers.py` exempt is added at all (shared or per-file); `reify.cpp` + the private declaring header
`src/dictionary/reify_dispatch_bridge.hpp` (which IS scanned — it lives under `src/**`) stay clean under the
pinned include recipe (only `<fixpp/dict/reify.hpp>` + std; no direct `<fixpp/wire/...>`), and the
discriminating "guard still bites if `reify.cpp` OR the bridge header includes a build-tree/wire header"
negative-test is retained; (2) the `owning_message_handle` construction seam is pinned to a **single
hand-written `fixpp::dict::detail` free function `detail::owning_message_handle_from_frame`** (declared in
`reify.hpp`, defined out-of-line in `reify.cpp` — the handle is a heap pimpl), with `owning_message_handle`
`friend`-ing that ONE stable name (passkey/attorney pattern); what research D-2 still rejects as fragile is
friending the many emitter-controlled *generated* dispatch-function names, NOT this single fixed name (see
research D-2 / contract C-2).

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
└── reify.hpp                            # owning_message_handle storage (bytes_+rmv+lazy view);
                                         #   reify_as<Msg> inline def; DECLARES detail::owning_message_handle_from_frame
                                         #   (defined out-of-line in reify.cpp — heap pimpl) + friends it;
                                         #   refresh inherited :70/:97 comments (see data-model E-1)

src/dictionary/
├── reify.cpp                            # delegate the 2 placeholder sites to the bridge fns;
│                                        #   handle-factory impl; fix get<35>-absent error path
├── reify_dispatch_bridge.hpp            # NEW PRIVATE same-module DECLARING header (declares the 2 bridge
│                                        #   fns; no build-tree include — only owning_message_handle/MessageView)
└── CMakeLists.txt                       # new fixpp_dict_dispatch_bridge target; configure_file the template;
                                         #   declare the fixpp_dict_dispatch_bridge <-> fixpp_dictionary back-edge
                                         #   BOTH ways (mirror the fixpp_wire<->fixpp_dictionary cycle at :32-42);
                                         #   forward-ref to fixpp::dict::dispatch is generate-phase-resolved (not a bug)

cmake/templates/
└── reify_dispatch_bridge.cpp.in         # NEW checked-in template; configure_file'd → build-tree bridge TU

cmake/Codegen.cmake                      # (reuse fixpp::dict::dispatch INTERFACE target; add_dependencies)

${CMAKE_BINARY_DIR}/_codegen/
└── reify_dispatch_bridge.cpp            # GENERATED bridge TU (the ONLY #include of _dispatch/*; NOT shipped,
                                         #   NOT under check_layers.py scan — build-tree code)

tools/check_layers.py                    # UNCHANGED (build-tree pivot needs no exempt; reify.cpp stays guarded)

tests/dictionary/
└── reify_dispatch_test.cpp              # remove FIXPP_R6_WIRE_BODY_READY guards; flip oracle asserts;
                                         #   add discriminating per-field witnesses (incl. AS multi-char)
tests/support/
├── reify_test_frame.hpp                 # add make_*_frame() siblings (v42 / v50sp2 / FIXT-admin / AS)
└── frame_view_factory.hpp              # (reuse make_frame_view)

spec/behaviors-and-limitations.md        # L-003-1 → PARTIAL unblock (reify/reify_as shipped; as<Msg>() T059) (FR-011)
spec/feature-catalogue.md                # D-008/OSS-010 R6 supplemental-note update (FR-011)
```

**Structure Decision**: Single-library layout. The one structural addition is the **dispatch bridge**
(private declaring header in `src/dictionary/` + a **build-tree-generated** TU materialized from a checked-in
`cmake/templates/reify_dispatch_bridge.cpp.in` + a CMake target that consumes the existing
`fixpp::dict::dispatch` include dir). The bridge is the minimal seam that lets shipped `reify.cpp` reach the
generated dispatch without violating NFR-003-8. **Note the build-tree pivot is a NEW mechanism**, not the one
"already blessed at 003/004": 003/004 blessed header-only dict↔wire glue, the two named hand-written headers,
the frozen stub, and the CMake `fixpp_dictionary → fixpp_wire` link edge — it did NOT bless a shipped `src/`
`.cpp` including build-tree `_dispatch/` headers. By generating the sole build-tree includer into
`${build}/_codegen/`, there is no shipped `src/` build-tree includer at all, so NFR-003-8 is satisfied
literally and the arch §2.4 carve-out is neither extended nor amended.

**Link graph.** The construction factory `detail::owning_message_handle_from_frame` is defined out-of-line in
`reify.cpp` (heap pimpl — data-model E-1) → it lives in `fixpp_dictionary`, and is called only from the
generated `_dispatch` arms compiled in the bridge TU; the bridge's `reify_dispatch_*` are in turn called from
`reify.cpp`. That mutual static-archive dependency means the `fixpp_dict_dispatch_bridge ↔ fixpp_dictionary`
back-edge MUST be declared **both ways** (mirroring the existing two-way `fixpp_wire ↔ fixpp_dictionary` cycle
declared+commented at `src/dictionary/CMakeLists.txt:32-42`), not left to TU co-location luck (research D-1).
**Forward-reference note (not a bug):** defining `fixpp_dict_dispatch_bridge` in `src/dictionary/CMakeLists.txt`
forward-references the later-defined `fixpp::dict::dispatch` / `fixpp_codegen_generate`; this is legal and
generate-phase-resolved — `fixpp_dictionary` already forward-links `fixpp_wire` from a later subdir and builds
today — so a future reader must not "fix" it by restructuring CMake.

## Complexity Tracking

> No Constitution Check violations — this section is intentionally empty. The one candidate complexity
> (a type-erased `owner_base`/`owner_impl` in `owning_message_handle`) was **rejected** during Phase-0
> review: 057's handle surface is entirely untyped, so byte+version storage suffices and a future
> `as<Msg>()` can materialize from the stored bytes without construction-time type-erasure.

## Phase 2 task seeds (binding — `/speckit-tasks` MUST emit)

- **Forced-regen gate (named task, NOT a research footnote).** Because codegen is configure-time and triggered
  by XML-mtime, an emitter edit alone leaves the STALE placeholder `_dispatch/*.hpp` in `${build}/_codegen/`
  (the known `project_codegen_emitter_staleness` false-green trap). After the emitter edit, and **before**
  compiling any test, a dedicated task MUST: (1) rebuild `fixpp-codegen`; (2) `rm -rf ${build}/_codegen`;
  (3) reconfigure; (4) **assert on BOTH regenerated headers** — the application AND the FIXT header
  (`emit_dispatch_fixt` is mirrored per D-4, so `reify_dispatch_fixt.hpp` is regenerated + compiled by the
  bridge TU too) — for EACH of `${build}/_codegen/include/fixpp/_dispatch/reify_dispatch_application.hpp` AND
  `.../reify_dispatch_fixt.hpp`: `grep -L dict_reify_wire_body_not_ready <hdr>` (the placeholder is gone) AND
  `grep -c 'detail::owning_message_handle_from_frame' <hdr>` > 0 (≥1 live factory call present). **Acceptance
  check = those two greps on each of the two headers.**

## Gate A

- Round 1 applied 2026-07-01: Codex P1=2 P2=5 P3=1; Opus post-judging P1=1 P2=6 P3=4; rewrite addresses RC#1 (bridge-TU build-tree pivot — USER DECISION, no arch §2.4 amendment), RC#2 (arm counts 34/105), RC#3 (detail:: from_frame factory + forced-regen named task + generated-arm coverage-exclusion), + Codex-P1#2-downgrade residuals (FR-011 partial-unblock, reify.hpp comment refresh), Article-VII wording, checklist note, 2 reify_as/get<35> edge cases. Reviews: research/reviews/codex_057-behavioral-reify-unblock_gate_a_review.md, research/reviews/opus_057-behavioral-reify-unblock_gate_a_adversarial_review.md.
- Round 2 applied 2026-07-01: Codex P1=1 P2=3 P3=1; Opus post-judging P1=0 P2=3 P3=4 (Codex CMake-ordering P1 REFUTED empirically — dropped, not actioned); rewrite addresses FR-008 arm-contract, detail::owning_message_handle_from_frame seam pin + propagation + out-of-line-in-reify.cpp definition site + declared bridge↔dictionary back-edge, private-header include recipe + extended negative test, forced-regen greps extended to FIXT header + unified strings, FR reorder/dangling-ref cleanup, CMake forward-ref robustness note. Reviews: research/reviews/codex_057-behavioral-reify-unblock_gate_a_2_review.md, research/reviews/opus_057-behavioral-reify-unblock_gate_a_2_adversarial_review.md.

### Round 1 — disagreements

- NO Codex findings were applied against the Opus judge's disposition. Codex P1#2 (D-2 "no type-erasure"
  reopens 003's handle contract) was judged an **over-escalation** by the Opus judge → byte-only storage is
  **kept** (byte storage does not foreclose a future AC-R6-compliant `as<Msg>()`; no 003 re-Gate-A). Codex
  P1#1 (shipped-`src/` bridge TU includes build-tree headers) was **confirmed** as a real blocker but
  **re-diagnosed** (an unrecorded carve-out extension, not a "de-guard") and resolved via the build-tree pivot
  (USER decision) rather than the arch §2.4 v0.3→v0.4 amendment path.

### Round 2 — disagreements

- The Codex round-2 P1 (a claimed configure-time CMake failure — `src/dictionary` added before
  `cmake/Codegen.cmake` defines `fixpp::dict::dispatch` / `fixpp_codegen_generate`) was judged an
  **over-escalation** and **REFUTED empirically** by the Opus judge: the forward reference is legal and
  resolved at the CMake **generate** phase (the ordering premise is true; the "will fail" conclusion is false),
  with the in-repo precedent that `fixpp_dictionary` already forward-links `fixpp_wire` from a later subdir and
  builds today. It was therefore **dropped, not applied** — no CMake restructuring. A one-sentence
  forward-reference robustness note was added (research D-1 / Structure Decision) so a future reader does not
  "fix" the non-bug.
