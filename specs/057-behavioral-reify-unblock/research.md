# Phase-0 Research: Behavioral Reify Unblock (057)

**Scope decision confirmed (pre-research):** the blocker is build-architecture, not decode. The R6 frozen
`include/fixpp/wire/message_view_contract.hpp` stub was **retired at the 004 cutover (T028)** — its header
banner (lines 4-12) states it is now a "thin RE-EXPORT of the real OffsetTable-backed wire surface in
`<fixpp/wire/parser.hpp>`", with `static_assert`s pinning `MessageView<Index> : public View`. Therefore
`bytes()`, `get<Tag>()`, `group<>()`, and the `Framer`-rebuild in `owning_<Msg>::view()` are **all live**.
The `reify.cpp` comment "R6: the frozen stub returns dict_xml_parse_failed for every get<>()" (`reify.cpp:174`)
is **stale** and must be corrected. Consequence: no frozen-contract swap is in scope; only the emitter's
final return + `dict::reify()`'s delegation-to-generated-dispatch are stubbed.

---

## D-1 — The dispatch bridge (the crux)

**Decision (build-tree pivot — supersedes the earlier "shipped TU in `src/dictionary/`" choice).** Add (a) a
**private same-module** declaring header `src/dictionary/reify_dispatch_bridge.hpp` (NOT a shipped/public
`include/` header) declaring two free functions in `fixpp::dict` — `reify_dispatch_fixt(view, char, profile,
mr)` and `reify_dispatch_application(view, msg_type_sv, appver, profile, mr)`, each `->
core::expected_t<owning_message_handle>`; it includes **no** build-tree header — only the already-available
`owning_message_handle` / `MessageView` types; (b) a single **build-tree-generated** TU — the ONLY TU that
`#include`s `<fixpp/_dispatch/reify_dispatch_{fixt,application}.hpp>` — **materialized at configure time** from
a repo-checked-in template `cmake/templates/reify_dispatch_bridge.cpp.in` via `configure_file` into
`${CMAKE_BINARY_DIR}/_codegen/reify_dispatch_bridge.cpp`, defining those functions by delegating to the inline
`dispatch::dispatch_{fixt,application}` helpers. The template body is **FIXED / version-independent** (two
wrappers), so `configure_file` is sufficient — no new `emit_dispatch.cpp` surface is required for the bridge TU
(a codegen-emitted TU would be acceptable too, but `configure_file` is the simpler mechanism); (c) shipped
`reify.cpp` includes only the private declaring header and calls the two functions at its placeholder sites
(`reify.cpp:211`, `:236`). **Because the sole build-tree includer is generated into `${build}/_codegen/`, NO
shipped `src/**` file includes a build-tree header — NFR-003-8 is satisfied literally, WITHOUT extending the
arch §2.4 carve-out and WITHOUT an amendment.**

**CMake.** Codegen runs at **configure time** (`cmake/Codegen.cmake:216-225`, `execute_process`), emitting to
`${CMAKE_BINARY_DIR}/_codegen/...`. The generated-header dir is already exposed by the **existing**
`fixpp::dict::dispatch` INTERFACE target (`Codegen.cmake:272-277`, carrying `$<BUILD_INTERFACE:.../_codegen/include>`).
Add a new `fixpp_dict_dispatch_bridge` STATIC target that compiles the `configure_file`d
`${build}/_codegen/reify_dispatch_bridge.cpp`, with: `target_link_libraries(... PRIVATE fixpp::dict::dispatch
fixpp_wire)` for the include dir; `add_dependencies(... fixpp_codegen_generate)`; is linked into
`fixpp_dictionary` (`src/dictionary/CMakeLists.txt`). Keep the `_codegen` include dir on the bridge target
**only** — never add it to `fixpp_dictionary` broadly, or every dict TU gains build-tree reach.

**Declare the `bridge ↔ dictionary` back-edge (both ways).** The bridge's generated arms call
`detail::owning_message_handle_from_frame` (defined out-of-line in `reify.cpp` → `fixpp_dictionary`), while
`fixpp_dictionary`'s `reify.cpp` calls the bridge's `reify_dispatch_*`. The two static archives therefore form
a **mutual** dependency, so the `fixpp_dict_dispatch_bridge ↔ fixpp_dictionary` edge MUST be declared **both
ways** — mirroring the existing two-way `fixpp_wire ↔ fixpp_dictionary` cycle declared+commented at
`src/dictionary/CMakeLists.txt:32-42` — and NOT left to TU co-location luck (which the Gate A round-2 review
showed is fragile: it links only because `owning_message_handle_from_frame` and `reify()` happen to share
`reify.cpp.o`).

**Forward-reference note (intentional, generate-phase-resolved — NOT a bug).** Defining
`fixpp_dict_dispatch_bridge` in `src/dictionary/CMakeLists.txt` (added at `CMakeLists.txt:148`)
forward-references `fixpp::dict::dispatch` / `fixpp_codegen_generate`, which are defined later by
`include(cmake/Codegen.cmake)` (`:190`). This is **legal and resolved at the CMake generate phase** —
`fixpp_dictionary` already forward-links `fixpp_wire` from the later `src/wire` subdir and builds today — so a
future reader must NOT "fix" it by restructuring CMake.

**Rationale.** Confines the sole build-tree `#include` to one **build-tree-generated** TU while `dict::reify()`
(shipped) sees only a private plain declaring header — satisfying NFR-003-8 / arch §2.4 v0.3 with **no carve-out
extension**. NOTE this is a **NEW mechanism**, not one "already blessed at 003/004": 003/004 blessed header-only
dict↔wire glue, the two named hand-written headers, the frozen stub, and the `fixpp_dictionary → fixpp_wire`
CMake **link** edge (which already exists from T028; this adds no new module cycle) — it did NOT bless a
shipped `src/` `.cpp` including build-tree `_dispatch/` headers. The build-tree pivot avoids the carve-out
question entirely by having no shipped `src/` build-tree includer at all.

**Alternatives considered.** (i) `dict::reify()` includes the generated headers directly — rejected, violates
NFR-003-8. (ii) Move the whole reify body into the generated tree — rejected, `reify()` is the shipped public
entry. (iii) A literal `dictionary→wire` whitelist edge — already rejected at 003 Gate A (forbidden cycle).
(iv) A **shipped `src/dictionary/reify_dispatch_bridge.cpp`** including the build-tree headers (the pre-pivot
choice) — rejected: it would be the first shipped-`src/` build-tree includer, an unrecorded extension of the
Article-XX-ratified arch §2.4 file-list requiring an amendment + user sign-off (the direct 003 RC#3 precedent);
the build-tree pivot dissolves the need for any amendment.

**check_layers strategy (RESOLVED by the build-tree pivot — no exempt at all).** `tools/check_layers.py` scans
`src/**` + `bindings/**` and maps a file's module from its path, flagging includes of other modules
(`dictionary` allows only `{core}`). Under the pivot the bridge TU is generated into `${build}/_codegen/`,
which `check_layers.py` does **not** scan — so **no `_dispatch`/`vXX` exempt is added anywhere** (neither a
shared `BRIDGE_EXEMPT_INCLUDES` entry nor a per-file exempt); `tools/check_layers.py` is **unchanged**. This
inverts the pre-pivot analysis: the earlier-REJECTED "place the TU outside the scanned dirs" is now
essentially what is chosen, but it is **principled** here — the bridge TU is **generated build-only code**
(where build-tree includes belong), not hidden hand-maintained production source. `reify.cpp` and the private
declaring header REMAIN under the scan and stay clean (neither includes a build-tree header). **What survives
unchanged:** the plan still owes a **discriminating negative-test that the guard still FAILS** if `reify.cpp`
(not the build-tree TU) includes a build-tree header — no exempt does not mean no guard (avoids a "gate
observes but never asserts" false-green).

**Evidence.** `cmake/Codegen.cmake:5-9,216-277`; `src/dictionary/CMakeLists.txt:11-43`;
`tools/check_layers.py:34,56,84-102,135-155,173`; `reify.cpp:159-167,199-236`.

---

## D-2 — `owning_message_handle` storage (REVISED post-advisor: NO type-erasure)

**Decision.** The handle stores `{resolved_message_version version, std::pmr::vector<std::byte> bytes_,
mutable std::optional<wire::MessageView<Index>> view_cache_}` — **identical to a concrete `owning_<Msg>`
minus the typed accessors**. Construction is a **fallible factory** (mirrors `owning_<Msg>::from_view`:
`try { deep-copy view.bytes() into mr } catch (bad_alloc) { return dict_reify_oom }`). `view()` lazily
re-frames from `bytes_` via the `Framer` (same pattern as `owning_<Msg>::view()`, `v44/Reify.hpp:177-193`);
`msg_type()` reads `view().get<35>()`; `field_value(tag)` delegates to `view().get(tag)`.

**Why NOT type-erasure (the original research proposal).** 057's entire in-scope public surface
(`version`/`msg_type`/`view`/`field_value`, `reify.hpp:84-89`) is **untyped**. The only typed method
`as<Msg>()` (`reify.hpp:93-94`) returns a borrowed `owning_message_t<Msg> const*` and is **T059-deferred /
out of scope** (spec Assumptions; left stubbed). The `impl` comment `/* small-variant OR heap polymorphic
owner */` (`reify.hpp:97`) is **descriptive of an option**, not a bound contract (003 Gate A did not pin a
polymorphic-owner requirement). Wrapping a concrete `owning_<Msg>` solely to call its untyped `view()`/
`bytes()` is pure overhead + a second heap node (Article VIII §5 / XV.1 concern). A future `as<Msg>()` can
materialize an `owning_<Msg>` from the stored `bytes_` at call time — it never needs construction-time
erasure — so the simpler storage **does not foreclose** T059.

**Construction seam (RESOLVED at Gate A — `detail::` free function).** `reify()` returns
`expected_t<owning_message_handle>` and OOM must surface as `dict_reify_oom`, so construction is a fallible
factory pinned to a **single hand-written free function
`fixpp::dict::detail::owning_message_handle_from_frame(rmv, view, mr)`** — declared in `reify.hpp` under
`namespace fixpp::dict::detail` and **DEFINED out-of-line in `reify.cpp` (`fixpp_dictionary`)**: the handle is
a heap pimpl (E-1), so the factory needs the complete `impl` type and cannot be defined inline in the header.
Called by the generated dispatch functions in the build-tree TU (and by `reify()`). `owning_message_handle`
`friend`s **this ONE stable name** to reach its private ctor/storage (standard passkey/attorney pattern; a
`detail::`-only passkey tag threaded through a private ctor is an equally valid alternative — this bundle
picks the free function + single friend). **What stays REJECTED as fragile:** friending the **many
emitter-controlled generated dispatch-function names** — those are brittle, numerous, and emitter-controlled.
Friending one fixed hand-written name is NOT that and is the standard pattern. It is **NOT a user construction
surface** ("handles come only from `reify()`"), preserving FR-012 (an internal C++ construction path on an
existing dict type — no C-ABI / public-builder surface added). (Because the factory is defined out-of-line in
`fixpp_dictionary` and called from the bridge TU, a `bridge ↔ dictionary` back-edge results — see D-1 CMake.)

**Evidence.** `reify.hpp:70-100`; `reify.cpp:90-96,114-138` (impl currently stores ONLY `version`, no owner,
no matching ctor); `build/.../v44/Reify.hpp:37-55,177-203`.

---

## D-3 — Multi-char dispatch shape (folded in per Clarification 2026-07-01)

**Decision.** Max MsgType length is **2** (grep for ≥3-char `msg_type_v` empty across all versions). Two
emitter edits in `emit_dispatch_application` (`emit_dispatch.cpp`): (1) **remove the skip** `if
(m.msg_type.size() != 1) continue;` (`:294-296`) so 2-char arms emit; (2) **replace the guard** `if
(msg_type.size() > 1) return dict_reify_unknown_msg_type` (`:279-283`) with **length-first two-level
dispatch** — keep `switch(mt)` for `len==1`; for `len==2` a `switch` on the packed
`static_cast<uint16_t>(msg_type[0])<<8 | msg_type[1]` (case labels `('A'<<8)|'S'` …); `empty()` and both
defaults fall to `dict_reify_unknown_msg_type` (FR-009/FR-014). Length-first is essential so single-char `'A'`
cannot collide with a two-char `"A?"`. Emit in the existing bytewise-sorted order (determinism preserved).
Generated multi-char arms (counting rule: unique 2-char `msg_type_v` = generated message classes with
`len(msg_type_v)==2` = one dispatch arm each): **v44=34, v50sp2=105, v42=0, FIXT-admin=0** — so `dispatch_fixt(char,…)` needs no
multi-char change, and `reify.cpp`'s FIXT branch (`:197`, `size()==1`-gated) already forwards the full
`mt_sv` to the application path, so **`reify.cpp` needs no multi-char edit**.

**Alternatives considered.** A `std::string_view` switch / hash — rejected as heavier than a packed-uint16
switch given the proven 2-char ceiling.

**Evidence.** `emit_dispatch.cpp:272-296`; unique 2-char `msg_type_v` counts from the generated `Messages.hpp`
(v44=34/v50sp2=105/v42=0/vt11=0; max MsgType length = 2); `reify.cpp:197,214-232`.

---

## D-4 — Emitter success-return (REVISED per D-2)

**Decision.** Current stub (`emit_dispatch.cpp:303-316`) per arm calls `owning_<ns>::owning_<Id>::from_view(view, mr)`,
discards it, returns the placeholder. `rmv_app` is built at `:263-270` as `{kind::application, profile.session,
application_version::<ns>}`. Per the D-2 simplification, the arm no longer references the typed owner — replace
the body with the handle factory:

```cpp
return ::fixpp::dict::detail::owning_message_handle_from_frame(rmv_app, view, mr);
```

which deep-copies (single copy; `bad_alloc → dict_reify_oom` inside the factory). Drop the `(void)own` +
stale "2b-unblock" comment + placeholder return + the now-dead `(void)rmv_app`/`(void)profile` suppressions
(`:240,:271`). **Mirror the identical change in `emit_dispatch_fixt`** (`:159-161`): replace with
`return ::fixpp::dict::detail::owning_message_handle_from_frame(rmv, view, mr);` and drop `(void)rmv` (`:140`).

**Consequence.** The generated `reify_dispatch_{application,fixt}.hpp` no longer reference `owning_<Msg>` at
all — the per-MsgType `switch` remains solely as the **known-MsgType membership test** (known → make handle;
unknown → `dict_reify_unknown_msg_type`, preserving FR-009). This is simpler than the original D-4 (no
move-and-erase) and keeps the bridge TU independent of the per-version `Reify.hpp`.

**Evidence.** `emit_dispatch.cpp:140,159-161,240,263-271,303-316`.

---

## D-5 — `reify_as<Msg>` (typed entry, no bridge)

**Decision.** `reify_as<Msg>` is **declared but never defined** (`reify.hpp:106-109`; `emit_reify.cpp:326`
T059-deferred) and **not instantiated anywhere** today (only comment references; `round_trip_property_test.cpp:11`
forbids use — so no link error exists). Define it **inline in shipped `reify.hpp`**, delegating DIRECTLY to
`owning_message_t<Msg>::from_view(view, mr)` — no runtime bridge (Msg is compile-time known), header-definable
via dependent names (`owning_message_t<Msg>` resolves through the caller-included `Reify.hpp` traits
specialization), so shipped source stays build-tree-clean. **Contract-required guard:** compare
`view.get<35>()` against `owning_message_t<Msg>::msg_type_v` (`static constexpr string_view`, `v44/Reify.hpp:39`)
and return `dict_reify_msg_type_mismatch` before delegating (`reify.hpp:104` names it a failure mode).

**Evidence.** `reify.hpp:104-109`; `emit_reify.cpp:326`; `v44/Reify.hpp:39,53`.

---

## D-6 — Test activation (discriminating, not empty-view green)

**Decision.** `FIXPP_R6_WIRE_BODY_READY` is **defined nowhere** — so the `#ifndef` branch
(`reify_dispatch_test.cpp:141-149,237-244`) is always live and the deferred tests currently `GTEST_SKIP()`.
Per FR-010, **remove the `#ifndef`/`#else`/`#endif` guards + the `_R6Deferred` skip stubs**, keeping the
real-assertion bodies (`:153-169`, `:248-274`). Flip the R6 positive-oracle asserts:
`SevenAdminMsgTypesAllHit` (`:112-135`) `EXPECT_EQ(r.error(), dict_reify_wire_body_not_ready)` →
`ASSERT_TRUE(r.has_value())` + `version().k==session_admin`, `.session==vt11`, `.application==Unknown`;
`MustIncludeSubsetAllHit` (`:196-234`) → `has_value()` + `version().application==c.version`. Rewrite the
obsolete frozen-stub / `dict_xml_parse_failed` narrative comments (`:323-345` and elsewhere).

**Non-discriminating-green trap (the deliverable):** current tests use an empty `MV mv{}`;
`detail::owning_message_handle_from_frame` on an empty view deep-copies empty bytes and **succeeds**, so
`version()` asserts pass off `rmv` alone — that does
NOT prove `field_value()` round-trips (FR-004). Add **discriminating per-field witnesses** using the existing
helpers `tests/support/reify_test_frame.hpp::make_nos_frame()` (real FIX 4.4 NewOrderSingle: `35=D, 11=ORD1,
55=AAPL, 49=S, 56=T`, BodyLength+CheckSum computed) and `frame_view_factory.hpp::make_frame_view()` (rebuilds a
`MessageView<Index>` via the Framer friend seam): `dict::reify(make_frame_view(make_nos_frame()), profile_v44,
mr)` → `has_value()` + `version()` + `field_value(11) == "ORD1"`. **Mutation-tested** (revert the arm to the
placeholder → RED). Add `make_*_frame()` siblings for the **v42, v50sp2, FIXT-admin, and multi-char** SC-002
paths; for multi-char pick **`AS` AllocationReport** (real body fields) so the assertion is discriminating,
not a header-only read.

**Evidence.** `reify_dispatch_test.cpp:112-149,196-274,323-345`; `tests/support/reify_test_frame.hpp:22-38`;
`tests/support/frame_view_factory.hpp:26,38`.

---

## D-7 — Guards, determinism, and the regen footgun

**Decision.**
- **(a) Shipped-src build-tree-include guard:** `check_layers.py` catches a shipped `src/` TU including
  `fixpp/_dispatch/*` or `fixpp/vXX/*`. The private bridge header `src/dictionary/reify_dispatch_bridge.hpp`
  lives under `src/**`, so it **IS scanned** (module=`dictionary`, `ALLOWED={core}` — consistent with D-1),
  and the scan is **direct-include-only** (per-line regex `:135-155`; it does NOT follow transitive includes).
  The pinned recipe (C-1) keeps it green: the header includes ONLY `<fixpp/dict/reify.hpp>` (a dict→dict
  self-include, allowed `:143-144`) + std (`<string_view>`, `<memory_resource>`), so every wire/dict type its
  two declarations name arrives **transitively** through `reify.hpp` — which lives in `include/`, the dir the
  scanner does **not** scan (`:84-88,173`) — and is invisible to the direct-only scanner. A **direct**
  `<fixpp/wire/...>` (or `_dispatch/`/`vXX`) include in the header would be a `dictionary → wire` violation →
  RED. Keep the guard green (D-1 build-tree pivot); the discriminating negative test MUST bite for BOTH
  `reify.cpp` AND this header (contract Layer-hygiene §).
- **(b) Byte-determinism:** `tests/codegen/determinism_test.cpp` asserts byte-identical generated headers
  run-to-run (AC-T1) + no writes outside `--out`; `codegen_build_graph_test.cmake` asserts `git status
  --porcelain` clean. Emitting multi-char arms in the existing bytewise-sorted order preserves both
  (FR-008/SC-005).
- **REGEN IS CONFIGURE-TIME**, triggered by **XML-mtime-vs-`Messages.hpp`-marker or missing marker**
  (`Codegen.cmake:166-188`), **NOT** by the emitter binary. **Ordered task step (not a footnote):** after the
  emitter edit → (1) rebuild `fixpp-codegen`; (2) **force regen** by `rm -rf ${build}/_codegen`; (3)
  reconfigure; (4) **assert on BOTH regenerated headers** — `emit_dispatch_fixt` is mirrored per D-4, so
  `reify_dispatch_fixt.hpp` is also regenerated + compiled by the bridge TU — for EACH of
  `${build}/_codegen/include/fixpp/_dispatch/reify_dispatch_application.hpp` AND `.../reify_dispatch_fixt.hpp`:
  `grep -L dict_reify_wire_body_not_ready <hdr>` (placeholder gone) AND
  `grep -c 'detail::owning_message_handle_from_frame' <hdr>` > 0 (≥1 live factory call present) — **before**
  compiling tests. Skipping this compiles the **stale placeholder** headers into a **false green** (the exact
  `project_codegen_emitter_staleness` trap).
- **`dict_reify_wire_body_not_ready` post-feature status: RETIRED from all live producers.** After 057 no
  success path returns it, so the mutation-discrimination signal is clean. The `reify.cpp` `get<35>`-absent
  branch (`:181-192`) — always-taken under the old frozen stub, now the **defensive / near-unreachable**
  path (a validated frame always carries tag 35) — is switched to `dict_reify_unknown_msg_type` (honest
  existing code; reachability noted). The placeholder code itself remains defined in the error enum (removing
  an enum value is out of scope) but has **zero live producers**.

**Evidence.** `check_layers.py:84-88,150-155,173`; `tests/codegen/determinism_test.cpp:5-23`;
`Codegen.cmake:166-188,213-234`; `reify.cpp:181-192`.

---

## Resolved cross-cutting clarifications

1. **Bridge-TU location + check_layers (D-1, build-tree pivot):** RESOLVED — bridge TU is **build-tree-
   generated** (`configure_file` from `cmake/templates/reify_dispatch_bridge.cpp.in` → `${build}/_codegen/`),
   the declaring header is **private** under `src/dictionary/`; `check_layers.py` is **unchanged** (no exempt —
   the generated TU is outside the scanned dirs by design; `reify.cpp` + the private header stay guarded); plan
   still owes the discriminating "guard still bites" negative-test.
2. **`as<Msg>()` (D-2):** RESOLVED — **left stubbed** (T059); no in-scope 057 test exercises it; the simplified
   storage does not foreclose it.
3. **SC-002 frame helpers (D-6):** RESOLVED — add `make_*_frame()` siblings (v42 / v50sp2 / FIXT-admin / `AS`
   multi-char) to `reify_test_frame.hpp`.

## Central files

`tools/codegen/fixpp-codegen/emit_dispatch.cpp`; `include/fixpp/dict/reify.hpp`;
`src/dictionary/reify_dispatch_bridge.hpp` (new, private declaring header); `src/dictionary/reify.cpp` +
`CMakeLists.txt`; `cmake/templates/reify_dispatch_bridge.cpp.in` (new template) → build-tree
`${build}/_codegen/reify_dispatch_bridge.cpp`; `cmake/Codegen.cmake`;
`tools/check_layers.py` (unchanged — for the discriminating guard check only);
`tests/dictionary/reify_dispatch_test.cpp` + `tests/support/reify_test_frame.hpp`;
`spec/behaviors-and-limitations.md` + `spec/feature-catalogue.md`.
