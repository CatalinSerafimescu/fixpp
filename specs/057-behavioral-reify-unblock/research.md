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

**Decision.** Add (a) a shipped **declaring** header `include/fixpp/dict/reify_dispatch_bridge.hpp` declaring
two free functions in `fixpp::dict` — `reify_dispatch_fixt(view, char, profile, mr)` and
`reify_dispatch_application(view, msg_type_sv, appver, profile, mr)`, each `-> core::expected_t<owning_message_handle>`;
(b) a single **generated-aware TU** `src/dictionary/reify_dispatch_bridge.cpp` — the ONLY TU that
`#include`s `<fixpp/_dispatch/reify_dispatch_{fixt,application}.hpp>` — defining those functions by delegating
to the inline `dispatch::dispatch_{fixt,application}` helpers; (c) shipped `reify.cpp` includes only the
declaring header and calls the two functions at its placeholder sites (`reify.cpp:211`, `:236`).

**CMake.** Codegen runs at **configure time** (`cmake/Codegen.cmake:216-225`, `execute_process`), emitting to
`${CMAKE_BINARY_DIR}/_codegen/...`. The generated-header dir is already exposed by the **existing**
`fixpp::dict::dispatch` INTERFACE target (`Codegen.cmake:272-277`, carrying `$<BUILD_INTERFACE:.../_codegen/include>`).
Add a new `fixpp_dict_dispatch_bridge` STATIC target that: `target_link_libraries(... PRIVATE fixpp::dict::dispatch fixpp_wire)`
for the include dir; `add_dependencies(... fixpp_codegen_generate)`; is linked into `fixpp_dictionary`
(`src/dictionary/CMakeLists.txt`). Keep the `_codegen` include dir on the bridge target **only** — never add
it to `fixpp_dictionary` broadly, or every dict TU gains build-tree reach.

**Rationale.** Confines the sole build-tree `#include` to one TU while `dict::reify()` (shipped) sees only a
plain declaring header — satisfying NFR-003-8 / arch §2.4 v0.3 with the mechanism already blessed at 003/004
(the `fixpp_dictionary → fixpp_wire` **link** edge already exists from T028; this adds no new module cycle).

**Alternatives considered.** (i) `dict::reify()` includes the generated headers directly — rejected, violates
NFR-003-8. (ii) Move the whole reify body into the generated tree — rejected, `reify()` is the shipped public
entry. (iii) A literal `dictionary→wire` whitelist edge — already rejected at 003 Gate A (forbidden cycle).

**check_layers strategy (RESOLVED — Gate-A-flagged).** `tools/check_layers.py` scans `src/**` + `bindings/**`
and maps a file's module from its path, flagging includes of other modules (`dictionary` allows only `{core}`).
A bridge TU under `src/dictionary/` including `<fixpp/_dispatch/...>` / `<fixpp/v44/...>` would flag.
**Chosen: option 2 — a per-file exempt-include refinement** (the bridge TU is registered with its OWN allowed
build-tree includes), NOT adding `_dispatch`/`vXX` to the shared `BRIDGE_EXEMPT_INCLUDES` set (which would
silently de-guard `reify.cpp`, already a `BRIDGE_SOURCE_FILE` — the exact NFR-003-8 hole to avoid).
**Rejected: option 1** (place the TU outside the scanned dirs) — it hides production code from the guard
rather than guarding it. The plan owes a **discriminating check that the guard still FAILS** if `reify.cpp`
(not the bridge TU) includes a build-tree header (avoids a "gate observes but never asserts" false-green).
Flagged for Gate A (Codex will weigh in regardless).

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

**Construction seam (Gate-A-flagged).** `reify()` returns `expected_t<owning_message_handle>` and OOM must
surface as `dict_reify_oom`, so construction is a static fallible factory (e.g.
`owning_message_handle::from_frame(rmv, view, mr)`), used only by the bridge functions + `reify()`. Keep it
non-user-constructible (private ctor + friend, or a `detail::`-namespaced factory) to preserve the current
"users only obtain handles from `reify()`" invariant. Exact visibility = Gate A detail; does not add C-ABI
or public-builder surface (FR-012 intact — an internal C++ construction path on an existing dict type).

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
Generated multi-char arms: **v44=68, v50sp2=210, v42=0, FIXT-admin=0** — so `dispatch_fixt(char,…)` needs no
multi-char change, and `reify.cpp`'s FIXT branch (`:197`, `size()==1`-gated) already forwards the full
`mt_sv` to the application path, so **`reify.cpp` needs no multi-char edit**.

**Alternatives considered.** A `std::string_view` switch / hash — rejected as heavier than a packed-uint16
switch given the proven 2-char ceiling.

**Evidence.** `emit_dispatch.cpp:272-296`; grep counts (v44=68/v50sp2=210/v42=0/vt11=0); `reify.cpp:197,214-232`.

---

## D-4 — Emitter success-return (REVISED per D-2)

**Decision.** Current stub (`emit_dispatch.cpp:303-316`) per arm calls `owning_<ns>::owning_<Id>::from_view(view, mr)`,
discards it, returns the placeholder. `rmv_app` is built at `:263-270` as `{kind::application, profile.session,
application_version::<ns>}`. Per the D-2 simplification, the arm no longer references the typed owner — replace
the body with the handle factory:

```cpp
return ::fixpp::dict::owning_message_handle::from_frame(rmv_app, view, mr);
```

which deep-copies (single copy; `bad_alloc → dict_reify_oom` inside the factory). Drop the `(void)own` +
stale "2b-unblock" comment + placeholder return + the now-dead `(void)rmv_app`/`(void)profile` suppressions
(`:240,:271`). **Mirror the identical change in `emit_dispatch_fixt`** (`:159-161`): replace with
`return ::fixpp::dict::owning_message_handle::from_frame(rmv, view, mr);` and drop `(void)rmv` (`:140`).

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

**Non-discriminating-green trap (the deliverable):** current tests use an empty `MV mv{}`; `from_frame` on an
empty view deep-copies empty bytes and **succeeds**, so `version()` asserts pass off `rmv` alone — that does
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
  `fixpp/_dispatch/*` or `fixpp/vXX/*`. Caveat: it scans `src/**`+`bindings/**`, **not `include/`** (own
  comment `:84-88,173`), so the shipped bridge *header*'s cleanliness is by-inspection (fine — it only
  declares). Keep the guard green (D-1 per-file strategy).
- **(b) Byte-determinism:** `tests/codegen/determinism_test.cpp` asserts byte-identical generated headers
  run-to-run (AC-T1) + no writes outside `--out`; `codegen_build_graph_test.cmake` asserts `git status
  --porcelain` clean. Emitting multi-char arms in the existing bytewise-sorted order preserves both
  (FR-008/SC-005).
- **REGEN IS CONFIGURE-TIME**, triggered by **XML-mtime-vs-`Messages.hpp`-marker or missing marker**
  (`Codegen.cmake:166-188`), **NOT** by the emitter binary. **Ordered task step (not a footnote):** after the
  emitter edit → (1) rebuild `fixpp-codegen`; (2) **force regen** by `rm -rf ${build}/_codegen`; (3)
  reconfigure; (4) **assert on the regenerated header** — `grep -L dict_reify_wire_body_not_ready
  ${build}/_codegen/include/fixpp/_dispatch/reify_dispatch_application.hpp` and confirm the new
  `from_frame(` return is present — **before** compiling tests. Skipping this compiles the **stale placeholder**
  headers into a **false green** (the exact `project_codegen_emitter_staleness` trap).
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

1. **Bridge-TU location + check_layers (D-1):** RESOLVED — TU in `src/dictionary/reify_dispatch_bridge.cpp`;
   `check_layers.py` gets a **per-file** exempt-include for that one TU (reify.cpp stays guarded); plan owes a
   discriminating "guard still bites" check. Gate-A-reviewable.
2. **`as<Msg>()` (D-2):** RESOLVED — **left stubbed** (T059); no in-scope 057 test exercises it; the simplified
   storage does not foreclose it.
3. **SC-002 frame helpers (D-6):** RESOLVED — add `make_*_frame()` siblings (v42 / v50sp2 / FIXT-admin / `AS`
   multi-char) to `reify_test_frame.hpp`.

## Central files

`tools/codegen/fixpp-codegen/emit_dispatch.cpp`; `include/fixpp/dict/reify.hpp` +
`include/fixpp/dict/reify_dispatch_bridge.hpp` (new); `src/dictionary/reify.cpp` +
`src/dictionary/reify_dispatch_bridge.cpp` (new) + `CMakeLists.txt`; `cmake/Codegen.cmake`;
`tools/check_layers.py`; `tests/dictionary/reify_dispatch_test.cpp` + `tests/support/reify_test_frame.hpp`;
`spec/behaviors-and-limitations.md` + `spec/feature-catalogue.md`.
