---
id: 002-dictionary-xml-loader
title: Data Model — Dictionary entities, invariants, error mapping
spec_kit_step: /plan Phase 1
last_updated: 2026-05-14
status: drafted (round 1)
inherits_design: .specify/2c-codegen.md v1.3 §4.1, §4.2, §4.3, §4.5, §6.1.1, §6.7
---

# Data Model — 002-dictionary-xml-loader

All entities and invariants are **inherited from `.specify/2c-codegen.md` v1.3**. This document records each entity's fields, validation rules, and lifetime contract in the canonical `/plan` Phase 1 format. No new entity is introduced at `/plan` time; the *shape* of the value types is locked in `[2c §4.1]` / `[2c §4.2]` / `[2c §4.3]` and re-asserted in `contracts/*.hpp`.

Citation form per research.md "Citation verification pass" — `[const §<Roman>.<arabic>]`, `[2c §X.Y]`, `[arch §X.Y]`. The seven entities below match the seven contract headers in `contracts/`.

## Entity 1 — `fixpp::dict::FieldRef` (per-tag metadata)

**Header:** `contracts/field_ref.hpp` (extract source: `[2c §4.1]`)

**Fields:**

| Field | Type | Domain | Notes |
|---|---|---|---|
| `tag` | `std::uint16_t` | `[0, 65535]` | Matches `[2b §1.2]`'s wire range. |
| `type` | `field_data_type` | enum, 28 named variants + `DialectExtension` sentinel | `DialectExtension` is reserved but unused by `XmlLoader` in this PR (F2 deferred). |
| `rule` | `field_presence` | `{NotDeclared, Optional, Required, Conditional}` | `Conditional` not emitted by the runtime XML loader in v1.0 (codegen-version base only per `[2c §4.1]`). |
| `condition_index` | `std::uint16_t` | 0 = no conditional rule; else index into the per-message conditional-rule table | Always 0 from `XmlLoader` in this PR. |
| `group_no_tag` | `std::uint16_t` | 0 if not inside a group; else the `NoXxx` tag of the enclosing group | One-indirection encoding of group context. |
| `component_index` | `std::uint16_t` | 0 if not inside a component; else index into per-version `ComponentRef` table | One-indirection encoding of component context. |
| `enum_table_index` | `std::uint16_t` | 0 if not enum-constrained; else index into per-version enum-value table | The enum-value table itself is out of scope for this PR (codegen-emitted); loader populates the index but consumers materialise tables later. |
| `length_pair_data_tag` | `std::uint16_t` | 0 if not a paired LENGTH; else the tag of the following DATA field | Loader populates from XML `<field>` adjacencies per `[FIX50SP2 §3]`. |
| `_reserved` | `std::uint16_t` | always 0 on emit; ignored on read | Forward-compat. Reserved under `FIXPP_DICT_FIELDREF_RESERVED_USED`. |

**Invariants:**

- `sizeof(FieldRef) == 16` (AC-F1).
- `alignof(FieldRef) == 2` (AC-F1).
- `std::is_standard_layout_v<FieldRef> == true` (AC-F1).
- `std::is_trivially_copyable_v<FieldRef> == true` (AC-F1).
- `_reserved == 0` after `XmlLoader::load*` returns (AC-F4); reader code never reads `_reserved`.
- `rule == Conditional` is never emitted by `XmlLoader` in this PR (AC-F5 valid set is `{Optional, Required, Conditional}`; loader produces the first two).
- One `FieldRef` per **`(MsgType, tag)` pair**, not per global tag. `OrderID(37)` appears in `ExecutionReport` and `OrderCancelRequest` with potentially different `rule`s (per `[2c §4.1]` Notes).

**State transitions:** none — `FieldRef` is an immutable POD. Loaded once by `XmlLoader::load*` (PMR-allocated into the `Dictionary`'s metadata block), read-only thereafter.

**Test seams:**

- Seam #4 (shape static_assert) → `tests/dictionary/ref_shape_test.cpp` re-asserts AC-F1..F5.
- Seam #8 (round-trip) → `tests/dictionary/round_trip_test.cpp` walks every `(MsgType, tag)` and verifies `field_ref(msg_type, tag)` ↔ `field(msg_type, tag)` (canonical vs descriptive-alias) agreement per AC-D1 / AC-D2 (the spec's AC-D1 was rewritten in Gate A round 1 to canonicalize on `field_ref(msg_type, tag)` — see spec.md round-1 edit).

## Entity 2 — `fixpp::dict::ComponentRef` (per-component metadata)

**Header:** `contracts/component_ref.hpp` (extract source: `[2c §4.2]`)

**Fields:**

| Field | Type | Domain | Notes |
|---|---|---|---|
| `component_id` | `std::uint16_t` | unique per version | Loader assigns sequentially in XML declaration order. |
| `name_offset` | `std::uint16_t` | offset into per-version name string pool | The name string pool is PMR-allocated alongside the FieldRef[] array in the metadata handle; aliased by `std::string_view` accessors. |
| `first_field_index` | `std::uint16_t` | index into the per-version FieldRef array | First field of the component. |
| `field_count` | `std::uint16_t` | non-zero for declared components | Empty components are rejected at load time with `dict::xml_parse_error`. |
| `parent_component_id` | `std::uint16_t` | 0 if top-level; else enclosing component | Components nest in FIX 4.4+. FIX 4.2 components are always top-level. |
| `_reserved` | `std::uint16_t` | always 0 on emit; ignored on read | Forward-compat. |

**Invariants:**

- `sizeof(ComponentRef) == 12` (AC-F2).
- `std::is_standard_layout_v<ComponentRef> == true` (AC-F2 implicit; re-asserted in `contracts/component_ref.hpp`).
- `std::is_trivially_copyable_v<ComponentRef> == true` (AC-F2).
- `_reserved == 0` after `XmlLoader::load*` returns; reader code never reads it.

**State transitions:** none — immutable POD per `[2c §4.2]`.

## Entity 3 — `fixpp::dict::GroupRef` (per-repeating-group metadata)

**Header:** `contracts/group_ref.hpp` (extract source: `[2c §4.2]`)

**Fields:**

| Field | Type | Domain | Notes |
|---|---|---|---|
| `no_tag` | `std::uint16_t` | `NoXxx` delimiter tag (e.g., 73 for `NoOrders`, 555 for `NoLegs`) | Primary key. |
| `first_field_tag` | `std::uint16_t` | first-field-of-group rule per `[FIX50SP2 §3]` | Consumed by `wire::Validator` (`[2b §4.6]`) once that feature ships. |
| `first_field_index` | `std::uint16_t` | index into per-version FieldRef array | First field of the group's tuple. |
| `field_count` | `std::uint16_t` | non-zero | Empty groups rejected at load time. |
| `parent_group_no_tag` | `std::uint16_t` | 0 if not nested; else enclosing group's `NoXxx` tag | Handles W-007 nested repeating groups per `[2b §4.7]`. |
| `_reserved` | `std::uint16_t` | always 0 on emit; ignored on read | Forward-compat. |

**Invariants:**

- `sizeof(GroupRef) == 12` (AC-F3).
- `std::is_standard_layout_v<GroupRef> == true` (AC-F3 implicit).
- `std::is_trivially_copyable_v<GroupRef> == true` (AC-F3).

**State transitions:** none.

## Entity 4 — `fixpp::dict::Dictionary` (loader output owner)

**Header:** `contracts/dictionary.hpp` (extract source: `[2c §4.3]`, loader-MVS subset)

**Fields (visible private state):**

| Field | Type | Domain | Notes |
|---|---|---|---|
| `handle_` | `detail::dict_metadata_handle_ptr` (= `std::shared_ptr<const dict_metadata_handle>`) | non-null after construction by `XmlLoader::load*` | Allocated via `std::allocate_shared` over `std::pmr::polymorphic_allocator<dict_metadata_handle>` so the shared-control-block deallocator returns memory to the originating PMR per `[2c §4.3]` / C-R2-P1-1. |

**Metadata block contents** (inside `detail::dict_metadata_handle`):

| Field | Type | Domain | Notes |
|---|---|---|---|
| FieldRef arrays per MsgType | `std::span<FieldRef const>[]` | PMR-allocated copies of the per-MsgType FieldRef sequences | Sorted by tag ascending per research.md D-6. |
| ComponentRef array | `std::span<ComponentRef const>` | PMR-allocated; sorted by name | research.md D-6. |
| GroupRef array | `std::span<GroupRef const>` | PMR-allocated; sorted by `no_tag` ascending | research.md D-6. |
| MessageEntry list | `std::span<MessageEntry const>` | PMR-allocated; sorted by MsgType **bytewise** (`std::ranges::lexicographical_compare` over raw `unsigned char` — locale-independent per research.md D-6 / Gate A round 1 P2.4) | Drives AC-D5 iteration. |
| Name string pool | `std::pmr::string` or `std::pmr::vector<char>` | PMR-allocated | All `string_view`-returning accessors alias into this pool. |
| Required-fields-per-MsgType lookup | `std::span<std::uint16_t const>[]` indexed by MsgType | PMR-allocated | Drives `required_fields(msg_type)`. |
| Tag-by-name index | `std::pmr::flat_map<std::string_view, std::uint16_t>` or sorted-pair array | PMR-allocated | Drives `field_by_name(name)`. |
| Per-MsgType FieldRef-by-tag index | `std::pmr::flat_map<...>` or sorted-pair array | PMR-allocated | Drives `field_ref(msg_type, tag)`. |
| `which_session_version_` | `session_version` | one of `{v40, v41, v42, v43, v44, v50, v50sp1, v50sp2, vt11}` | Loaded from `<fix major minor [servicepack]>` per AC-L4. `session_version::Unknown` never occurs after a successful `load*` call. |
| `mr_` | `std::pmr::memory_resource*` | non-null | Owning resource; the same `mr` passed to `XmlLoader::load*`. |

**Invariants:**

- After successful construction by `XmlLoader::load*`:
  - `handle_ != nullptr`.
  - `which_session_version() != session_version::Unknown` (AC-L4 — the loader either returns a valid Dictionary or throws `dict::unknown_version_error`).
  - All PMR-allocated tables and the name string pool reside on `mr` (AC-P1).
  - `messages()` iteration order is deterministic and stable across loads of the same XML on the same machine and across machines (NFR-002-4 — research.md D-6).
- **Immutability after construction.** Every public method is `const` and `noexcept` (AC-T1, AC-D8). The metadata block is never mutated after `XmlLoader::load*` returns — the shared_ptr is to `const dict_metadata_handle`.
- **Move discipline.** Move ctor / move assign are `= default` `noexcept`; the shared_ptr move is no-throw, allocates nothing, touches no atomics. Heap-pinned metadata-handle address survives the move; consumers that aliased the metadata storage (none in this PR; `dict::table_view` is out of scope) remain valid.
- **Copy is deleted.** `Dictionary` is move-only per `[2c §4.3]` and `contracts/dictionary.hpp` (matches spec.md §A3 round-1 wording). Caller-side multi-session sharing uses `std::shared_ptr<Dictionary>` at the consumer's discretion, not a builtin refcount on the value type. A future deep-copy method may be added; not in scope here.
- **Reserved internal slot for F2.** The internal `dict_metadata_handle` layout in `contracts/dictionary.hpp` reserves room for a future `base_keepalive_` slot (a copy of the base `Dictionary`'s `handle_`) per `[2c §4.3]` canonical at `2c-codegen.md:1676`; F2 (DialectOverlay) will add the slot when it lands. This is a private-layout reservation only — no public-API impact and no test seam in this PR.
- **Thread-safety.** Frozen-after-handoff per `[2c §6.1.1]`: a single `Dictionary` is safe to read concurrently from N threads without external synchronisation (AC-T2).

**State transitions:**

```
                    XmlLoader::load*(path|text, mr)
                                │
                                │  PMR allocate `dict_metadata_handle` +
                                │  walk pugixml DOM → emit sorted tables
                                │  into `mr`-backed `std::pmr::vector`s
                                ▼
                       ┌──────────────────────┐
                       │  Dictionary (frozen) │   ◄── all public methods
                       │  handle_ != nullptr  │       const + noexcept
                       └──────────┬───────────┘
                                  │
                          std::move (no-throw)
                                  │
                                  ▼
              consumer-owned `Dictionary` (still frozen; aliased
                metadata stays at the same heap address)
                                  │
                       last shared_ptr ref dropped
                                  │
                                  ▼
                control-block deallocator returns memory to `mr`
```

Failure transitions (during `XmlLoader::load*`, before the `Dictionary` is materialised):

- malformed XML / dangling component ref / unknown FIX type → throws `dict::xml_parse_error`; no `Dictionary` constructed.
- version string outside v1.0-supported nine → throws `dict::unknown_version_error`; no `Dictionary` constructed.
- PMR allocation failure (any byte) → trapped via `core::detail::trap_throw_or_throw<dict::xml_oom_error>` (NEW helper added in this PR per research.md D-3), re-thrown as `dict::xml_oom_error`; partial state torn down deterministically; no leak (verified by ASan + the PMR tracking resource, seam #2 + seam #9).

**Test seams:**

- Seam #5 (determinism) → `tests/dictionary/determinism_test.cpp` (NFR-002-4).
- Seam #6 (TSan concurrent readers) → `tests/dictionary/concurrent_readers_test.cpp` (AC-T1, AC-T2).
- Seam #8 (round-trip) → `tests/dictionary/round_trip_test.cpp` (AC-D1, AC-D2, AC-D5).
- AC-D6 / AC-D7 → `tests/dictionary/dictionary_lookup_test.cpp` parameterized over the four shipped versions.

## Entity 5 — `fixpp::dict::XmlLoader` (the loader value)

**Header:** `contracts/xml_loader.hpp` (extract source: `[2c §4.5]`, two methods only)

**Fields:** none (stateless; `XmlLoader` is a value with no member state in v1.0).

**Methods:**

| Method | Signature | Throws | ACs |
|---|---|---|---|
| `load` | `Dictionary load(std::filesystem::path const&, std::pmr::memory_resource*)` | `xml_parse_error` (AC-L2, L3, L5–L8), `unknown_version_error` (AC-L4), `xml_oom_error` (AC-L9) | AC-L1, AC-L2..L9, AC-P1, AC-P2 |
| `load_from_string` | `Dictionary load_from_string(std::string_view, std::pmr::memory_resource*)` | same as `load` modulo AC-L2 (no path resolution) | AC-L10 + AC-L3..L8 via inline-XML literals |

**Invariants:**

- Stateless. `XmlLoader{}.load(p, mr)` and `XmlLoader{}.load(p, mr)` produce structurally equal `Dictionary` values (NFR-002-4).
- **No `mut`able / global / static state** (spec.md §3 "Edge Cases" #1). Two threads calling `load` on **separate** `XmlLoader` values is safe; calling `load` from N threads on the **same** `XmlLoader` value is also safe — there is no shared mutable state to race on (research.md D-7, updated in Gate A round 1 P2.3).
- **`mr != nullptr` is a caller precondition** (research.md D-5). Debug-asserted; release-undefined; not surfaced as a runtime error.
- **pugixml is an implementation detail** (research.md D-15). Public header does not transitively expose `pugixml.hpp`.
- **Out of scope per /clarify Q2 → A:** `load_overlay(...)` and `load_overlay_from_string(...)` are absent from this PR's `XmlLoader` (spec.md §10 F2). Future addition is source-compatible by C++ language rule (added member functions can never invalidate existing call sites) and stays within the `[arch §9.3]` "Stable from v1.0" tier. (The earlier draft's `[arch §9.2]` cite for this claim did not resolve — see plan.md `## Gate A` round 1.)

**State transitions:** none (stateless value).

**Test seams:**

- Seam #1 (mock `XmlSource`) → covered by `load_from_string` itself; no dedicated test file.
- Seam #2 (PMR allocation tracking) → `tests/dictionary/pmr_allocation_test.cpp`.
- Seam #7 (negative-path XML samples) → `tests/dictionary/negative_paths_test.cpp`.
- Seam #9 (allocator-failure injection) → `tests/dictionary/oom_injection_test.cpp`.
- Seam #10 (XML-parser-error injection) → `tests/dictionary/parser_error_test.cpp`.

## Entity 6 — `fixpp::dict::xml_parse_error` / `unknown_version_error` / `xml_oom_error` (error taxonomy)

**Header:** `contracts/error.hpp`

| Exception type | Base | Constituent ACs | Enum mate (per research.md D-10) |
|---|---|---|---|
| `xml_parse_error` | `std::runtime_error` | AC-L2 (I/O), AC-L3 (malformed XML), AC-L5 (missing/bad `<field number>`), AC-L6 (duplicate field), AC-L7 (component dangling), AC-L8 (unknown FIX type) | `fixpp::core::error::dict_xml_parse_failed` |
| `unknown_version_error` | `std::runtime_error` | AC-L4 (FIX version outside v1.0-supported nine) | `fixpp::core::error::dict_unknown_version` |
| `xml_oom_error` | `std::bad_alloc` | AC-L9 (PMR allocation failure) | `fixpp::core::error::dict_xml_oom` |

**Invariants:**

- `xml_parse_error` and `unknown_version_error` both derive from `std::runtime_error` → a generic `catch (std::exception&)` at the engine-init call site catches both. `xml_oom_error` derives from `std::bad_alloc`, which itself derives from `std::exception`, so the same generic catch covers all three.
- Every exception type carries a `[[nodiscard]] fixpp::core::error code() const noexcept` accessor returning the matching enum variant; callers can route by `code()` instead of `dynamic_cast`.
- The three new enum variants (`dict_xml_parse_failed`, `dict_unknown_version`, `dict_xml_oom`) are **additive** to `fixpp::core::error` — no shape change of the enum's underlying type, no renumbering of existing variants. C-ABI surface impact deferred to 2i (research.md D-10).
- **Sibling I/O-translation variant.** AC-L2 specifically allows a future sibling exception type (`dict::xml_io_error` or similar) derived from `std::runtime_error`, ratified at `/plan` if the path-resolution failure mode warrants its own type. **Decision (research.md D-4):** unify under `xml_parse_error` for v1.0; the exception's `what()` carries the underlying `std::filesystem::filesystem_error` message. A future `xml_io_error` split is additive and Article-XX-free.

**State transitions:** none — exceptions are throw-once objects.

**Test seams:**

- Seam #7 → negative-path test verifies each exception type fires for its AC.
- Seam #9 → OOM injection verifies `xml_oom_error` translation.
- Seam #10 → parser-error injection verifies `pugi::xml_parse_result → xml_parse_error` translation.

## Entity 7 — `fixpp::dict::session_version` / `application_version` (version enums)

**Header:** `contracts/version_profile.hpp` (extract source: `[2c §4.3]`, enums only — full `version_profile` struct deferred)

| Enum | Variants | Domain |
|---|---|---|
| `session_version` | `Unknown=0, v40=1, v41=2, v42=3, v43=4, v44=5, v50=6, v50sp1=7, v50sp2=8, vt11=9` | The nine v1.0-supported versions per `[2c §1.3]`. |
| `application_version` | `Unknown=0, v40=1, v41=2, v42=3, v43=4, v44=5, v50=6, v50sp1=7, v50sp2=8` | No `vt11` (FIXT is session-only). |

**Invariants:**

- The underlying type is `std::uint8_t` for both enums (matches `[2c §4.3]`).
- `Unknown` is the parse-failure / unset sentinel. `Dictionary::which_session_version()` never returns `Unknown` after a successful `load*` call (the loader either succeeds with a known version or throws `unknown_version_error`).
- This PR's XML-data shipping subset (per /clarify Q1 → B) covers `session_version::{v42, v44, v50sp2, vt11}` and `application_version::{v42, v44, v50sp2}` (FIXT is session-only). The five F1-deferred values exist in the enum but no XML is checked in for them.
- **`application_version` is NOT exercised by this PR's loader output (Gate A round 1 P2.2).** The loader sets `which_session_version()` on the produced `Dictionary` and stores nothing about the application version. For a `Dictionary` loaded from `FIXT11.xml` (a session-only vocabulary), no `application_version` value is meaningful — FIXT carries no application messages, and the application-side identity (`DefaultApplVerID(1137)` / `ApplVerID(1128)`) is established at session-handshake time by the future wire/session integration feature, not at XML-load time. The full `version_profile` struct (`[2c §4.3]`) carrying both axes plus a per-message-override bit is deferred to that downstream feature; `application_version` ships as a *value space* here for the loader's version-string parse table (e.g., to recognize "5.0SP2" → `v50sp2`), not as a Dictionary-side `default_appl` slot.
- **Wire `ApplVerID(1128)` mapping table** lives in `[2c §4.3]`. The mapping is **not** the C++ enum's internal indices (`Unknown=0, v40=1, …, v50sp2=8`); a future parse implementation must use the spec table per `[FIXT §5.1]` / `[FIXT §5.3]` (deferred to the wire/session feature).

**State transitions:** none — enums are immutable value types.

## Cross-entity relationships

```
                ┌──────────────────┐
                │  XmlLoader       │  (stateless value)
                └────────┬─────────┘
                         │
                         │  load(path, mr) / load_from_string(text, mr)
                         │
                         ▼
                ┌────────────────────────────────┐
                │  Dictionary                    │  (move-only owner)
                │   handle_ → dict_metadata_handle (heap-pinned)
                │     ├── FieldRef[]     (PMR on `mr`)         ──┐
                │     ├── ComponentRef[] (PMR on `mr`)           │
                │     ├── GroupRef[]     (PMR on `mr`)           │  Entities 1–3
                │     ├── MessageEntry[] (PMR on `mr`)           │  (POD value types,
                │     ├── name string pool (PMR on `mr`)         │   trivially copyable)
                │     ├── required-fields-per-MsgType lookup    │
                │     ├── tag-by-name index                     │
                │     ├── per-MsgType FieldRef-by-tag index     │
                │     ├── which_session_version_                ──┘
                │     └── mr_                                    
                └────────────────────────────────┘
                         │
                         │ (throws on failure — exits via)
                         ▼
                ┌──────────────────┐
                │  Error taxonomy  │   xml_parse_error  : std::runtime_error
                │  (Entity 6)      │   unknown_version_error : std::runtime_error
                │                  │   xml_oom_error    : std::bad_alloc
                └──────────────────┘
```

## Error mapping summary

Every AC-L* acceptance criterion mapped to its exception type and `core::error` variant:

| AC | Trigger | Exception thrown | `core::error` variant carried |
|---|---|---|---|
| AC-L1 | well-formed FIX44.xml load succeeds | (none — returns `Dictionary` by value) | — |
| AC-L2 | unreadable / nonexistent path | `xml_parse_error` | `dict_xml_parse_failed` |
| AC-L3 | malformed XML | `xml_parse_error` | `dict_xml_parse_failed` |
| AC-L4 | FIX major/minor outside v1.0-supported nine | `unknown_version_error` | `dict_unknown_version` |
| AC-L5 | `<field>` missing or non-numeric `number` | `xml_parse_error` | `dict_xml_parse_failed` |
| AC-L6 | duplicate `<field number="N">` | `xml_parse_error` | `dict_xml_parse_failed` |
| AC-L7 | `<message>`/`<group>` references undefined `<component>` | `xml_parse_error` | `dict_xml_parse_failed` |
| AC-L8 | `<field type="UNKNOWN_TYPE">` outside `[FIX50SP2 §3.3]` | `xml_parse_error` | `dict_xml_parse_failed` |
| AC-L9 | PMR allocation failure during load | `xml_oom_error` (trapped via `core::detail::trap_throw_or_throw<dict::xml_oom_error>` — new helper added in this PR per research.md D-3) | `dict_xml_oom` |
| AC-L10 | `load_from_string(xml_text, mr)` equivalent to `load(path, mr)` for the same grammar | (positive path; same errors as L2..L9 apply) | — |

**No exception escapes outside `xml_*_error`.** A `pugi::xml_parse_result.status != status_ok` becomes `xml_parse_error` at the wrapper site. A `std::filesystem` access error becomes `xml_parse_error` at the wrapper site. A PMR `bad_alloc` becomes `xml_oom_error` via `trap_throw_or_throw<dict::xml_oom_error>` (the new helper added in this PR per research.md D-3). No `std::runtime_error` (other than our two derived types) and no `std::bad_alloc` (other than `xml_oom_error`) escape `XmlLoader::load*`.

## PMR allocation accounting (NFR-002-2, AC-P1, AC-P2 summary)

| Allocation site | Resource | Counted by AC-P1? | Notes |
|---|---|---|---|
| `dict_metadata_handle` (the heap-pinned block itself) | `mr` (via `std::allocate_shared`) | yes | One allocation. |
| `FieldRef[]` storage per MsgType | `mr` (via `std::pmr::vector`) | yes | Sorted by tag at emit. |
| `ComponentRef[]` storage | `mr` | yes | Sorted by name. |
| `GroupRef[]` storage | `mr` | yes | Sorted by `no_tag`. |
| `MessageEntry[]` storage | `mr` | yes | Sorted by MsgType. |
| Name string pool | `mr` | yes | One contiguous allocation; aliased by all string_view-returning accessors. |
| Required-fields lookup | `mr` | yes | One `pmr::vector<std::uint16_t>` per MsgType. |
| Tag-by-name + per-MsgType indices | `mr` | yes | Sorted-pair arrays or flat_map. |
| pugixml transient DOM | `malloc/free` (default) | **no** | NFR-002-2 ("zero allocation against the global `new`") satisfied by construction — pugixml does not call `operator new`. The transient DOM is released when `xml_document` goes out of scope at the bottom of `load*`. research.md D-16. |

## Determinism oracle (NFR-002-4 seam #5)

The deterministic-output invariant is enforceable by hashing the iteration order of `messages()` and the `FieldRef` lookup output for a fixed `MsgType`:

```
auto d1 = XmlLoader{}.load("dictionaries/FIX44.xml", &mr1);
auto d2 = XmlLoader{}.load("dictionaries/FIX44.xml", &mr2);

assert(hash_iteration(d1) == hash_iteration(d2));
assert(hash_field_ref_output(d1, "D") == hash_field_ref_output(d2, "D"));
```

Where `hash_iteration` walks `d.messages()` and concatenates `(msg_type, name)` pairs; `hash_field_ref_output` walks the per-MsgType FieldRef array and concatenates `(tag, type, rule)` triples. Both must produce byte-identical SHA-256 digests across runs. The mechanism that delivers this is the sorted storage invariant (research.md D-6) — not a runtime sort at iterate time.

## Threading model (AC-T1, AC-T2, NFR-002-3 summary)

| Object | Construction discipline | Read discipline |
|---|---|---|
| `XmlLoader` (per-instance) | single-threaded; no shared mutable state | N/A (stateless value) |
| `Dictionary` | constructed once per XML by `XmlLoader::load*`; the construction call is single-threaded | safe to share read-only across N threads without locking (every accessor is `const` and `noexcept`); verified by TSan seam #6 |

No `mutable` member, no `thread_local` storage, no `static` non-const state anywhere in the loader. Per `[const §XV]` `thread_local` is banned engine-wide; this entity respects it.
