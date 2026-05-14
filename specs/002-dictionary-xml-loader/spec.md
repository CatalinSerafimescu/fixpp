---
id: 002-dictionary-xml-loader
title: XML data dictionary loader — `fixpp::dict::XmlLoader` + `Dictionary` runtime
module: dictionary/
phase: 4
status: drafted
verdict: TBD
spec_kit_step: /specify
last_updated: 2026-05-14
owner: fixpp::dict (C++); first Phase 4 entry into the `dictionary/` module
inherits_design: .specify/2c-codegen.md (v1.3, signed off 2026-05-10)
catalogue_rows: D-007 (XML data dictionary format loader — first canonical target), D-010 (Component definition support), OSS-001 (QuickFIX-XML compatible loader reference)
gate_a_required: yes (touches public C++ API; opens `dictionary/` module)
---

# 002-dictionary-xml-loader — XML data dictionary loader

> **/specify scope.** This spec captures *what* the dictionary loader does and *why*. The full *how* is locked in design doc [`2c-codegen.md`](../../.specify/2c-codegen.md) v1.3 — public API for `fixpp::dict::Dictionary`, `FieldRef`, `ComponentRef`, `GroupRef`, `XmlLoader`; exception-API carve-out for construction-time failures; PMR allocator policy; multi-version coexistence model; integration with `wire`/`session`/`capi`. This document does not re-derive any of that; it carves the first Phase 4 feature scope out of 2c so `/clarify`, `/plan`, `/tasks`, and `/implement` have a single starting artifact.

## 1. Summary

Ship the runtime XML data-dictionary loader — `fixpp::dict::XmlLoader`, `fixpp::dict::Dictionary`, `fixpp::dict::FieldRef`, `fixpp::dict::ComponentRef`, `fixpp::dict::GroupRef` — as the first feature of the `dictionary/` module. Reads a QuickFIX-XML-format FIX data-dictionary file and returns a populated, immutable `Dictionary` by value (per `[2c §4.5]`'s exception-API carve-out — construction-time failures throw typed exceptions; hot-path APIs on the resulting `Dictionary` are `noexcept`).

**Version coverage in this PR** (per /clarify 2026-05-14 Q1 → B): the four codegen-target versions per `[2c §1.3]` — `FIX42`, `FIX44`, `FIX50SP2`, `FIXT11`. The loader structurally accepts all nine v1.0-supported versions per `[2c §1.3]` (AC-L4), but only the four codegen-target XMLs are checked in and headline-tested here. The five runtime-XML-only versions (`FIX40`, `FIX41`, `FIX43`, `FIX50`, `FIX50SP1`) are tracked as follow-up **F1** in §10.

**Overlay surface in this PR** (per /clarify Q2 → A): `XmlLoader::load_overlay*` is absent. `DialectOverlay` (D-009) defers to a dedicated future feature, tracked as follow-up **F2** in §10.

**XML parser selection** (per /clarify Q3 → A): deferred to /plan, tracked as follow-up **F3** in §10.

> **Style note.** The triggering description framed the public surface as `expected_t<Dictionary>`-returning. This spec follows the canonical design doc `[2c §4.5]`, which locks `XmlLoader::load` as **`Dictionary`-by-value with construction-time typed exceptions** (`dict::xml_parse_error`, `dict::unknown_version_error`, `dict::xml_oom_error`) — a deliberate carve-out of `[arch §5.3]` parallel to `[2a §4.2]`'s `core::detail::trap_throw`. The hot-path APIs on `Dictionary` remain `noexcept`.

## 2. Why (user value)

Every FIX consumer downstream of `dictionary/` needs runtime field/message/component metadata to:

1. **Validate received messages** — `wire::Validator` per `[2b §4.6]` consumes `FieldRef::presence` per `(MsgType, tag)` to detect missing-required and unknown-field violations.
2. **Drive codegen output** — the `tools/codegen/fixpp-codegen` D-008 consumer reads `Dictionary` to emit `Fields.hpp` / `Messages.hpp` / `Validator.hpp` per-version headers (`[2c §1.3]`).
3. **Resolve component and repeating-group structure at runtime** — `ComponentRef` and `GroupRef` enable nested-field lookup (`Instrument`, `Parties`, `NoPartyIDs`, `NoAllocs`, …).
4. **Support runtime-XML-only FIX versions** — five of nine v1.0-supported versions (4.0, 4.1, 4.3, 5.0, 5.0SP1) have no codegen path per `[2c §1.3]`; their only `Dictionary` source is `XmlLoader`.

Without this primitive, `wire`'s validator has no metadata source, codegen has no input, and the engine cannot accept any FIX message at all. Every downstream feature in `wire/`, `session/`, and `capi/` is blocked.

## 3. User stories

### 3.1 Engine integrator (in-tree, `wire::Validator`) (Priority: P1)

> *As a wire-layer author (**2b**), I want to call `XmlLoader().load("FIX44.xml", mr)` once at session-open and receive a `Dictionary` value, so I can pass it to `wire::Validator` (per `[2b §4.6]`) without writing a custom XML parser.*

**Why this priority**: This is the MVP path — wire's validator is blocked without a `Dictionary` source, and a real-XML, real-`Dictionary` end-to-end load is what makes every other consumer downstream usable. Cannot ship anything else from this module without it.

**Independent Test**: Drop the upstream QuickFIX `FIX44.xml` on disk, call `XmlLoader().load(...)`, then assert the loaded `Dictionary` reports the headline tags / messages / components / groups (AC-D6, AC-D7).

### 3.2 In-process loader (tests, programmatic XML) (Priority: P1)

> *As a test author, I want `XmlLoader().load_from_string(crafted_bad_xml, mr)` so I can drive the negative-path assertions (AC-L3..L8) without on-disk fixtures cluttering the repo.*

**Why this priority**: Negative-path coverage of the loader is the bulk of the test surface; doing it via on-disk fixtures only would push 6+ small XML files into the repo. `load_from_string` is already locked in 2c §4.5, so no design-doc cost.

**Independent Test**: Feed each AC-L*-shaped malformed XML literal to `load_from_string` and assert the expected typed exception fires.

### 3.3 Codegen consumer (**2c** D-008) (Priority: P2)

> *As the `fixpp-codegen` tool author, I want to load the same FIX XML files in offline mode and iterate over messages/fields/components/groups, so I can emit per-version `constexpr` `FieldRef[]` / `MessageRef[]` arrays into `include/fixpp/v44/Fields.hpp` etc.*

**Why this priority**: Codegen is a separate Phase 4 feature (D-008) that consumes the `Dictionary` produced here. The iteration surface (AC-D5) must be in scope, but actual codegen output is downstream.

**Independent Test**: Iterate `Dictionary::messages()` and for each message walk `(MsgType, tag)` `field()` lookups; assert exhaustive coverage of the loaded XML (AC-D5 + round-trip seam #8).

### 3.4 Multi-version session host (Priority: P2)

> *As a session host wiring up FIXT.1.1 + FIX 5.0 SP2 simultaneously, I want to load both XMLs and own them as distinct `Dictionary` values keyed by the version profile (`[2c §4.3]`), so the session resolves application-message versions per `[FIXT §5.1 / §5.3]` without recompiling.*

**Why this priority**: Per /clarify Q1 → B, this PR ships the four codegen-target versions including `FIX50SP2` and `FIXT11`, which together form the canonical FIXT-session-over-FIX5.0SP2-application pairing. The story is exercised by AC-D6 (parameterized per version).

**Independent Test**: Load `FIXT11.xml` and `FIX50SP2.xml` into separate `Dictionary` values; assert FIXT11's headlines (admin-only: Logon/Logout/Heartbeat/TestRequest/ResendRequest/Reject/SequenceReset) and FIX50SP2's headlines (application: NewOrderSingle/ExecutionReport/MarketDataRequest etc.) appear in their respective dictionaries and **not** in each other.

### 3.5 Runtime-XML-only FIX version consumer (Priority: P3 — deferred to F1)

> *As a consumer of a legacy venue running FIX 4.3 (one of the five runtime-XML-only versions per `[2c §1.3]`), I want to load `FIX43.xml` at startup and use the resulting `Dictionary` to drive runtime tag-keyed field access (per `[2b §4.3]` `view.get(uint16_t tag)`), so I can speak the venue's dialect without waiting for v1.x codegen.*

**Why this priority**: Per /clarify Q1 → B this PR does not ship XMLs for FIX 4.0/4.1/4.3/5.0/5.0SP1. The loader structurally accepts these versions (AC-L4 lists the full nine per `[2c §1.3]`); the deferral is the XML data + version-specific headline tests. Tracked as follow-up **F1** in §10. Story is therefore **not exercised in this PR**.

**Independent Test**: (Deferred.) Load `FIX43.xml` once it ships under F1, assert AC-D6-style headline checks parameterized for FIX 4.3.

### Edge Cases

- **What happens when the same XML is loaded twice into separate `Dictionary` values?** Both must be structurally equal (NFR-002-4 determinism); no global / static state in `XmlLoader`.
- **What happens when `mr` is null?** `XmlLoader::load(path, nullptr)` — `[2c §6.1.1]` mandates PMR-aware allocation; passing `nullptr` is a precondition violation. Test seam: assert via `trap_throw` to `dict::xml_oom_error`, or document as caller-side UB at /clarify time.
- **What happens when XML is well-formed but semantically inconsistent** (e.g., `<message>` references a `<field>` that exists but has the wrong type for its declared usage)? Out of scope for the loader — semantic validation belongs to `wire::Validator`. The loader fails only on structural defects (AC-L2..L8).
- **What happens during PMR allocation failure mid-load?** Partial state is destroyed; `dict::xml_oom_error` thrown (AC-L9). No leak — verified by ASan + the PMR tracking resource.
- **What happens when the XML declares a FIX version not in the v1.0 supported nine?** `dict::unknown_version_error` (AC-L4). Tested by feeding `<fix major="6" minor="0">`.

## Clarifications

### Session 2026-05-14

- **Q1: FIX version coverage in this PR?** → **A: Option B** — four codegen-target versions (`FIX42`, `FIX44`, `FIX50SP2`, `FIXT11`) per `[2c §1.3]`. Discharges D-001 (FIX 4.2), D-002 (FIX 4.4), D-003 (FIX 5.0SP2 + FIXT.1.1) in this PR. The loader structurally accepts all nine v1.0-supported versions (AC-L4) — only the XML data and version-specific headline tests for the five runtime-XML-only versions (FIX 4.0/4.1/4.3/5.0/5.0SP1) are deferred. Tracked as follow-up **F1** in §10.
- **Q2: `XmlLoader::load_overlay*` surface in this PR?** → **A: Option A** — absent. The public `XmlLoader` header ships only `load` / `load_from_string`. The `DialectOverlay` value type, its `[2c §4.4.1]` grammar closure, and the `[2c §6.4]` additive-merge contract all defer to a dedicated D-009 feature. Tracked as follow-up **F2** in §10. **User direction:** clearly mark this gap in §10 so the surface extension is visible for the future feature.
- **Q3: Third-party XML parser pre-selection?** → **A: Option A** — defer to /plan with a 2–3 candidate evaluation under `[const §V.3]` (third-party-deps procedure, cited if applicable). Codex Gate A reviews the choice. Tracked as follow-up **F3** in §10.

## 4. Functional acceptance criteria

Lifted from `[2c §4.1]`–`[2c §4.5]` and `[2c §6.1.1]`; one bullet per testable property.

### 4.1 Load — `XmlLoader::load` / `load_from_string`

- **AC-L1.** `XmlLoader().load("FIX44.xml", mr)` returns a populated `Dictionary` by value when `FIX44.xml` is the upstream QuickFIX-format dictionary.
- **AC-L2.** Loading an unreadable / nonexistent path throws `dict::xml_parse_error` (or a sibling I/O-translation variant ratified at /plan, derived from `std::runtime_error`).
- **AC-L3.** Loading malformed XML (unclosed tag, invalid encoding, broken UTF-8) throws `dict::xml_parse_error`.
- **AC-L4.** Loading XML whose `<fix major="..." minor="..." [servicepack="..."]>` does not resolve to one of the nine v1.0-supported versions per `[2c §1.3]` throws `dict::unknown_version_error`.
- **AC-L5.** Loading XML containing a `<field>` with no `number` attribute (or with a non-numeric value) throws `dict::xml_parse_error`.
- **AC-L6.** Loading XML containing duplicate `<field number="N">` definitions throws `dict::xml_parse_error`.
- **AC-L7.** Loading XML where a `<group>` or `<message>` references a `<component name="X">` not defined in the same file throws `dict::xml_parse_error`.
- **AC-L8.** Loading XML containing `<field type="UNKNOWN_TYPE">` outside the FIX-type vocabulary per `[FIX50SP2 §3.3]` throws `dict::xml_parse_error`.
- **AC-L9.** PMR allocation failure during a `load*` call throws `dict::xml_oom_error` (the construction-time analogue, translated from PMR's `std::bad_alloc` via `core::detail::trap_throw` per `[2c §6.1.1]` / `[2a §4.2]`).
- **AC-L10.** `load_from_string(xml_text, mr)` accepts a `std::string_view` containing the same XML grammar as `load(path, mr)` and produces a structurally identical `Dictionary`. Used by AC-L3..L8 negative-path tests.

### 4.2 Dictionary lookup

- **AC-D1.** `Dictionary::field(uint16_t tag)` returns the `FieldRef` for that tag in the dictionary's default-message context, or `std::nullopt` if undefined.
- **AC-D2.** `Dictionary::field(MsgType, uint16_t tag)` returns the `FieldRef` for that `(MsgType, tag)` pair, including the message-specific `presence` rule (`Required` / `Optional` / `Conditional` per `[2c §4.1]`).
- **AC-D3.** `Dictionary::field_by_name(std::string_view name)` returns the tag for a known field name (e.g., `"ClOrdID"` → 11), or `std::nullopt`. Case-sensitive exact match against the XML's `name` attribute (assumption A2).
- **AC-D4.** `Dictionary::component(std::string_view name)` returns the `ComponentRef`; `Dictionary::group(uint16_t no_xxx_tag)` returns the `GroupRef` keyed by the delimiter tag.
- **AC-D5.** `Dictionary::messages()` returns an iterable of `(MsgType, message-name)` pairs sorted by MsgType.
- **AC-D6.** For each of the four shipped FIX versions per /clarify Q1 → B (`FIX42`, `FIX44`, `FIX50SP2`, `FIXT11`), loading the corresponding XML produces a `Dictionary` containing that version's headline messages:
  - **FIX44** (canonical reference): `NewOrderSingle` MsgType=`D`, `ExecutionReport` MsgType=`8`, `Logon` MsgType=`A`, `Heartbeat` MsgType=`0`, `Reject` MsgType=`3`; components `Instrument`, `Parties`.
  - **FIX42**: same five headline messages as FIX44 (subset); components `Instrument` (FIX 4.2 simpler form), no `Parties` (post-4.2 addition).
  - **FIX50SP2**: application headlines `NewOrderSingle`, `ExecutionReport`, `MarketDataRequest`, `MarketDataSnapshotFullRefresh`; component `Instrument` (5.0SP2 form).
  - **FIXT11**: session/admin headlines only — `Logon`, `Logout`, `Heartbeat`, `TestRequest`, `ResendRequest`, `Reject`, `SequenceReset`. **No** application headlines (FIXT is session-transport only).
- **AC-D7.** Loading FIX44.xml produces correct `NoXxx` delimiter tags on standard repeating groups (`NoPartyIDs` = 453, `NoAllocs` = 78, `NoLegs` = 555). Equivalent per-version delimiter checks for `FIX42`, `FIX50SP2`, `FIXT11` parameterized in `tests/dictionary/dictionary_lookup_test.cpp`.
- **AC-D8.** Every `Dictionary` public lookup method is `noexcept`.

### 4.3 FieldRef / ComponentRef / GroupRef shape

- **AC-F1.** `sizeof(FieldRef) == 16`, `alignof(FieldRef) == 2`, `std::is_standard_layout_v<FieldRef> == true`, `std::is_trivially_copyable_v<FieldRef> == true` per `[2c §4.1]` static_asserts.
- **AC-F2.** `sizeof(ComponentRef) == 12`, `std::is_trivially_copyable_v<ComponentRef> == true` per `[2c §4.2]`.
- **AC-F3.** `GroupRef` shape per `[2c §4.2]`.
- **AC-F4.** `_reserved` bytes are zero on load; ignored on read (per `[2c §4.1]` discipline).
- **AC-F5.** `FieldRef::presence ∈ {Required, Optional, Conditional}` per `[2c §4.1]`.

### 4.4 Threading / sharing

- **AC-T1.** `Dictionary` is immutable after construction (every public accessor is `const` and `noexcept`).
- **AC-T2.** A single `Dictionary` can be shared read-only across N sessions/threads without locking. Test seam: hold one `Dictionary`, run N reader threads concurrently, no data races under TSan.

### 4.5 PMR allocator

- **AC-P1.** `XmlLoader::load(path, mr)` and `load_from_string(xml_text, mr)` allocate every byte of the resulting `Dictionary`'s metadata storage from `mr`. Test seam: pass a `monotonic_buffer_resource` upstreamed to a `pmr_allocation_tracking_resource` and assert zero allocations against the global `new`.
- **AC-P2.** PMR allocation failure translates to `dict::xml_oom_error` per AC-L9 — *not* into an `std::bad_alloc` escape.

## 5. Out of scope

- **Codegen tool (D-008 / `fixpp-codegen`)** — emits `Fields.hpp` / `Messages.hpp` per-version headers from a loaded `Dictionary`. Separate Phase 4 feature.
- **DialectOverlay (D-009 / `[2c §4.4]`) end-to-end** — see §10 Q2 for whether `XmlLoader::load_overlay` / `load_overlay_from_string` ship in *this* PR as a usable surface or are deferred. Either way the overlay-merge semantics (`[2c §6.4]`) are out of scope here.
- **`dict::table_view` (`[2c §4.6]`)** — value-typed borrowed handle into a `Dictionary`; consumed by 2b wire / typed-message accessors, not part of the loader MVS.
- **Generated typed messages (`fixpp::v44::*` etc.) — `[2c §4.7]`** — owned by D-008.
- **`dict::reify` / `dict::reify_as` bridge (`[2c §4.8]`)** — owned by D-008.
- **`dict::version_registry` (`[2c §4.9]`)** — engine-level multi-`Dictionary` registry; lands when the second loaded `Dictionary` arrives.
- **Per-message validator (`[2b §4.6]` `dictionary_driven_validator`)** — consumes the `Dictionary` produced here; lives in a wire-layer feature.
- **C ABI surface for `Dictionary`** — `fixpp_dict_t` opaque handle is owned by 2i; not part of this PR.
- **SWIG / Python bindings for `Dictionary`** — owned by 2m; not part of this PR.
- **FIX Latest / FIX Orchestra format (D-011)** — post-v1.0 per `[const §XVIII.2]`.

## 6. Non-functional requirements

| NFR | Requirement | How verified |
|---|---|---|
| NFR-002-1 | `XmlLoader::load(FIX44.xml, mr)` completes in ≤500 ms wall-clock on a typical developer machine (warm filesystem cache). Loaded once per session, not on the hot path. | Bench harness `bench/dictionary/xml_loader_bench.cpp`; CI bar at 1 s regression gate; user-facing target 500 ms. |
| NFR-002-2 | Zero allocation against the global `new` for the entire `load*` call when `mr` is provided. | Test seam #2 — `pmr_allocation_tracking_resource`; global counter must read 0. |
| NFR-002-3 | `Dictionary` is shareable read-only across N threads without locking; no `mutable` state on the lookup path. | Test seam #6 — TSan run with N concurrent readers, 0 reports. |
| NFR-002-4 | Loader is deterministic: byte-identical XML input produces a `Dictionary` whose `messages()` iteration order and `field()` lookup ordering are byte-stable across runs and across machines. | Test seam #5 — load FIX44.xml twice in one process, hash the iteration order, assert equal. |
| NFR-002-5 | All non-AC-L9 errors throw a `dict::xml_*` typed exception derived from `std::runtime_error` (`[2c §4.5]`). No `std::bad_alloc` escapes (AC-P2). Hot-path `Dictionary` APIs are `noexcept` (AC-D8). | Negative-path tests (AC-L2..L10); `noexcept` static-asserts on every `Dictionary` public accessor. |
| NFR-002-6 | The `dictionary → core` layer edge is the only addition to `tools/check_layers.py`'s allowed-edges map; no `dictionary → wire` or `dictionary → session` edge introduced. | `tools/check_layers.py` clean in CI. |

## 7. Files in scope

> The full files-to-create list is locked at /plan; this section names the bright lines.

- **C++ headers (public):** `include/fixpp/dict/dictionary.hpp`, `include/fixpp/dict/xml_loader.hpp`, `include/fixpp/dict/field_ref.hpp`, `include/fixpp/dict/component_ref.hpp`, `include/fixpp/dict/group_ref.hpp`, `include/fixpp/dict/error.hpp`.
- **C++ headers (core trivial-fold, if first consumer):** `include/fixpp/core/expected.hpp`, `include/fixpp/core/span.hpp`, `include/fixpp/core/pmr.hpp`, `include/fixpp/core/error.hpp` — per `phases/phase-4/core/README.md`'s "trivial — fold into first consumer" disposition. The exact list is /plan-locked; some may already exist from 001.
- **Implementation:** `src/dictionary/xml_loader.cpp`, `src/dictionary/dictionary.cpp`.
- **Dictionaries (XML data):** `dictionaries/FIX42.xml`, `dictionaries/FIX44.xml`, `dictionaries/FIX50SP2.xml`, `dictionaries/FIXT11.xml` checked in as data (sourced from upstream QuickFIX repo per OSS-001 reference; commit/hash pinned at /plan). Per /clarify Q1 → B.
- **Tests:** `tests/dictionary/xml_loader_test.cpp`, `tests/dictionary/dictionary_lookup_test.cpp`, `tests/dictionary/ref_shape_test.cpp`, `tests/dictionary/negative_paths_test.cpp` (AC-L2..L8, L10), `tests/dictionary/concurrent_readers_test.cpp` (TSan, AC-T2), `tests/dictionary/pmr_allocation_test.cpp` (AC-P1, AC-L9).
- **Bench:** `bench/dictionary/xml_loader_bench.cpp`.
- **CMake:** `src/dictionary/CMakeLists.txt`, target `fixpp::dictionary` (linkage shape /plan-locked).
- **Layer lint:** `tools/check_layers.py` allow-edge `dictionary → core`.

## 8. Inheritance / dependencies

- **Inherits design from:** `[2c §4.1]` FieldRef; `[2c §4.2]` ComponentRef / GroupRef; `[2c §4.3]` Dictionary; `[2c §4.5]` XmlLoader; `[2c §6.1.1]` allocation / exception / threading on the hot path; `[2c §6.7]` errors-introduced sub-table; `[2c §9]` test seams. `[arch §4.2]` dictionary module surface; `[arch §5.2]` PMR allocator policy; `[arch §5.3]` error model (construction-time exception carve-out).
- **Depends on (in-tree):** `fixpp::core::error`, `fixpp::core::expected_t<T>`, `fixpp::core::detail::trap_throw` (per `[2a §4.2]`), `fixpp::core::pmr` aliases. The trivial-fold `core/` primitives ship in this PR alongside the loader (per `phases/phase-4/core/README.md` disposition).
- **Depends on (third-party):** an XML parsing library — selection belongs to /plan, not /specify. Constraint: must either be exception-light or wrap cleanly through `trap_throw`, and either be header-only or already approved via the third-party-dependency procedure named in the constitution (`[const §V.3]`, cited if applicable).
- **Unblocks:** D-008 codegen (consumes `Dictionary`); 2b wire validator (consumes `FieldRef::presence`); session FSM (uses `Dictionary` per `[2c §4.3]`); 2m Python bindings via `dict::table_view` (future feature).

## 9. Test seams (carried from `[2c §9]`, scoped to this feature)

1. **Mock `XmlSource`** — `load_from_string` covers in-memory testing without on-disk fixtures.
2. **`pmr_allocation_tracking_resource`** — counts allocations against the global `new` for AC-P1 + AC-P2.
3. **Clock seam** — N/A on the load path; documented for completeness.
4. **FieldRef / ComponentRef / GroupRef shape static_assert seam** — `tests/dictionary/ref_shape_test.cpp` re-asserts `[2c §4.1]` and `[2c §4.2]` invariants in the consumer of the header; catches future ABI drift.
5. **Determinism oracle** — load FIX44.xml twice in one process, hash the `messages()` iteration order and `field()` lookup output, assert equal (NFR-002-4).
6. **Concurrent-reader TSan harness** — N reader threads against one `Dictionary` (AC-T2 / NFR-002-3).
7. **Negative-path XML samples** — one per AC-L2..L8 / L10 stored under `tests/dictionary/fixtures/bad_xml/` (or inline as `load_from_string` arguments).
8. **Round-trip seam** — load FIX44.xml, iterate every `(MsgType, tag)` pair, look it up by both `field(tag)` and `field(MsgType, tag)`, assert idempotent.
9. **Allocator-failure injection** — `pmr::memory_resource` that throws `std::bad_alloc` on the Nth allocate; verify translation to `dict::xml_oom_error` per AC-L9.
10. **XML-parser-error injection** — crafted XML triggering the underlying parser's error path; verify translation to `dict::xml_parse_error` per AC-L3.

## 10. Follow-ups & deferred work

> Resolutions from /clarify Session 2026-05-14 (see Clarifications above) ratified the scope cuts; this section names the items intentionally left unshipped, with a concrete trigger criterion for each. The Spec-Kit `/clarify` cap of 3 markers is now zero — each former marker has become either a ratified decision or a tracked follow-up.

### F1 — Five runtime-XML-only FIX versions

- **What's deferred:** XML data files (`dictionaries/FIX40.xml`, `FIX41.xml`, `FIX43.xml`, `FIX50.xml`, `FIX50SP1.xml`) + per-version headline tests in `tests/dictionary/dictionary_lookup_test.cpp`.
- **Why:** /clarify Q1 → B caps this PR at the four codegen-target versions to keep scope comparable to 001-core-decimal.
- **Catalogue rows:** D-004 (FIX 4.0/4.1), D-005 (FIX 4.3), D-006 (FIX 5.0/5.0SP1).
- **Source of truth:** `[2c §1.3]` listed under "Runtime-XML scope".
- **Loader-side support:** **already in scope this PR.** AC-L4 lists the full nine versions per `[2c §1.3]` as structurally accepted; the deferred work is the XML data + version-specific assertions, not the version-string parsing or the loader's code path. A future feature implementing F1 is ~1 PR per version (data + tests; no new loader code).
- **Trigger:** A consumer (venue / customer) needs one of these versions, or before any wire-validator feature is exercised against a venue running them.

### F2 — DialectOverlay end-to-end (D-009)

- **What's deferred:** `XmlLoader::load_overlay(path, mr)` and `load_overlay_from_string(text, mr)` methods; the `fixpp::dict::DialectOverlay` value type; `[2c §4.4.1]` overlay-grammar closure (max additions per overlay, length-pair rules); `[2c §6.4]` additive-merge contract; the overlay-allocation timing seam in `tests/dictionary/`.
- **Why:** /clarify Q2 → A — defer to a dedicated D-009 feature that owns `DialectOverlay` end-to-end. Per user direction (2026-05-14), this gap is explicitly named here so any future review of `XmlLoader`'s public surface sees the unfinished extension.
- **Catalogue rows:** D-009 (custom dictionary extension via overlay), COM-011 (per-session venue-specific FIX dialect).
- **Source of truth:** `[2c §4.4]`, `[2c §4.5]` (the `load_overlay*` declarations), `[2c §6.4]` (merge contract), `[2c §4.4.1]` (grammar closure).
- **Public-surface impact:** Extending `XmlLoader` with two new public methods in the future is non-breaking ABI per `[arch §9.2]` versioning (additive). No call site in v1.0 depends on the absence of these methods.
- **Trigger:** First per-session venue-dialect requirement, or first OSS feature integration that needs to mutate a base dictionary at session-open time.

### F3 — Third-party XML parser selection

- **What's deferred:** The XML library choice (libexpat / tinyxml2 / pugixml / rapidxml / other).
- **Why:** /clarify Q3 → A — /plan evaluates 2–3 candidates against `[2c §9]` test seams.
- **Source of truth:** `[const §V.3]` third-party-deps procedure (cited if applicable; see §11 R1 for the underlying risk).
- **Trigger:** /plan phase of this same feature (immediate next step after `/speckit-clarify`).

## 11. Risk register

- **R1 — XML parser dependency choice.** /plan decision (libexpat? tinyxml2? pugixml? rapidxml?); selection affects compile time, runtime cost, exception-vs-error surface, build complexity. Mitigation: /plan evaluates 2–3 candidates against `[2c §9]` test seams; Codex Gate A reviews the choice.
- **R2 — XML source-of-truth (FIX42, FIX44, FIX50SP2, FIXT11).** Multiple QuickFIX-format dictionaries exist (QuickFIX upstream, QuickFIX/N, vendor variants); pick one canonical source per version. Mitigation: lock at /plan to upstream QuickFIX repository commit/hash for all four shipped versions; record the pinned commit in `dictionaries/README.md`.
- **R3 — `FieldRef` shape drift.** `[2c §4.1]` static_asserts on `sizeof(FieldRef) == 16` etc. ABI-fragile; this feature is the first to materialize the struct. Mitigation: ship `tests/dictionary/ref_shape_test.cpp` per test seam #4 *in the same PR* so any future drift fails CI before merge.
- **R4 — Performance bar miss.** NFR-002-1 target ≤500 ms FIX44 load; CI regression gate at 1 s. Mitigation: bench harness in scope.
- **R5 — `core/` trivial-fold drift.** First consumer dragging in `core::error` / `expected_t<T>` etc. fixes their public shape for the rest of the engine. Mitigation: every trivial-fold header gets a `tests/core/<name>_shape_test.cpp` shape assertion before merge.

## 12. Definition of done

- All AC-L*, AC-D*, AC-F*, AC-T*, AC-P* tests pass on all CI presets (debug, release, sanitizers: ASan + UBSan + TSan).
- All NFR-002-* checks pass on at least the release preset.
- All `[2c §4.1]` and `[2c §4.2]` static_asserts present in the public header and re-asserted in `tests/dictionary/ref_shape_test.cpp`.
- `tools/check_layers.py` reports clean (only `dictionary → core` edge added).
- `clang-tidy` and `clang-format` clean per Tier-1 CI.
- Codex Gate A converged (round 1 or 2; rewrite reset on round 3 if needed per `[const §XVII.1]`).
- `/speckit-verify` record fully GREEN per `[const §XVII.8]`.
- Codex Gate B converged with high-severity findings = 0 (medium / low fixes or waivers documented in this spec's §10 + the feature sub-file).
- Documentation: `docs/src/dictionary/xml-loader.md` covering "how to load a dictionary at session open", "supported FIX versions in v1.0" (whichever subset Q1 resolves to), and the `dict::xml_*` exception taxonomy.

## 13. References

- Constitution: `.specify/constitution.md` — `[const §I.1]` v1.0 surface; `[const §V.3]` third-party-deps procedure (cited for /plan if applicable); `[const §VI]` spec coverage; `[const §VII]` testing; `[const §VIII.5]` zero-allocation hot path (applies to `Dictionary` lookups, not `XmlLoader`); `[const §X]` C-ABI (cited for what is *not* in scope); `[const §XVII.1]` Codex Gate A; `[const §XVII.8]` `/speckit-verify` precondition; `[const §XVIII.2]` post-v1 roadmap (D-011 deferral).
- Architecture: `.specify/architecture.md` — `[arch §4.2]` `dictionary` module surface; `[arch §5.2]` PMR allocator policy; `[arch §5.3]` error model + construction-time exception carve-out; `[arch §10]` row 2c handoff.
- Design doc: `.specify/2c-codegen.md` v1.3 — `[2c §1.3]` version coverage; `[2c §4.1]` FieldRef; `[2c §4.2]` ComponentRef / GroupRef; `[2c §4.3]` Dictionary; `[2c §4.4]` DialectOverlay (cited for Q2 boundary); `[2c §4.5]` XmlLoader; `[2c §6.1.1]` allocation / exception / threading on the hot path; `[2c §6.7]` errors-introduced sub-table; `[2c §9]` test seams; `[2c §10]` open questions.
- Sibling docs: `.specify/2a-decimal.md` — `[2a §4.2]` `core::detail::trap_throw`. `.specify/2b-wire.md` — `[2b §4.3]` `view.get(uint16_t tag)`; `[2b §4.6]` `dictionary_driven_validator`.
- Catalogue: `spec/feature-catalogue.md` — D-007 (XML loader), D-008 (codegen — out of scope), D-009 (custom dict extension — see Q2 boundary), D-010 (component definition support — implicit, since `ComponentRef` ships), OSS-001 (QuickFIX-XML compatible loader reference), OSS-010 (header-only generated typed messages — out of scope).
- Spec references (XML format): `[FIX44]` FIX 4.4 application specification (QuickFIX-XML format follows this); `[FIX50SP2 §3.3]` field data types vocabulary (used in AC-L8); `[FIXT §5.1]` `ApplVerID(1128)` (multi-version coexistence rationale; not exercised here but referenced for context).

## Assumptions

- **A1.** PMR-awareness is mandatory: every `XmlLoader::load*` accepts a `std::pmr::memory_resource*` per `[arch §5.2]` and `[2c §6.1]`. Non-PMR overloads are not in v1.0.
- **A2.** Field-name lookup (`field_by_name`) is case-sensitive exact match against the XML's `name` attribute (industry default for QuickFIX-format dictionaries). If a venue ships case-variant XML, the caller normalizes upstream.
- **A3.** `Dictionary` is value-typed and movable; copies are deep (since metadata is PMR-allocated). Sharing across sessions/threads uses `std::shared_ptr<const Dictionary>` at the caller's layer, *not* a builtin refcount — this is consistent with `[2c §4.3]`'s heap-pinned metadata-handle design.
- **A4.** The loader is single-pass over the XML — it does not require seeking back to earlier nodes; this is enforceable given QuickFIX-format dictionaries declare `fields` before `messages`.
- **A5.** Loader determinism (NFR-002-4) is achieved by sorted storage of `FieldRef[]` (by `MsgType` then `tag`) and `ComponentRef[]` (by name) inside `Dictionary`; iteration order falls out of the storage order without an explicit sort at iterate time.
