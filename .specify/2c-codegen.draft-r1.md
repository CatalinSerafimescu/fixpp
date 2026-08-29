# Design Doc 2c — Dictionary Codegen: Header Layout, Multi-Version Coexistence, Dialect Overlay Binding

> **Status:** Draft v0.1 — Pre-Gate-A  ⛔ **SUPERSEDED — ARCHIVED v0.1. DO NOT DESIGN FROM THIS FILE.** Read `.specify/2c-codegen.md` (v1.4) instead. `research/reviews/opus_2c_codegen_*` closing recommendation triggered a **full rewrite**; this file is the archived v0.1. Marked 2026-08-29: the successor pointed here, but nothing here pointed forward, so a reader opening this file directly had no way to learn it holds a **rejected** design.
> **Date:** 2026-05-08
> **Owner:** `fixpp::dict` (`include/fixpp/dict/`, `src/dictionary/`) + the codegen tool `tools/codegen/fixpp-codegen` + the per-version generated header packs `include/fixpp/v42/`, `v44/`, `v50sp2/`, `vt11/` (build-tree only).
> **Inherits:** `[arch §1]` (goals), `[arch §2]` (module layering — `dictionary` sits below `wire` and is consumed by `session`/`capi`), `[arch §3]` (namespaces — `fixpp::dict`, `fixpp::v42`, `fixpp::v44`, `fixpp::v50sp2`, `fixpp::vt11`), `[arch §4.2]` (full `dictionary` module surface), `[arch §5.2]` (allocator policy — PMR-aware, per-session resource), `[arch §5.3]` (error model — `expected_t<T>` on hot path, exceptions reserved for `XmlLoader` construction), `[arch §5.5]` (lifetime model — typed messages are flyweights), `[arch §5.6]` (configuration shape — `SessionConfig` frozen at session open; dialect-overlay swap is its own API), `[arch §6]` (plugin pattern — ≤5 pure-virtual cap if any interface is virtualized), `[arch §7.3]` (header surface), `[arch §7.4]` (CMake target layout), `[arch §9.1]` (public vs internal headers), `[arch §9.2]` (versioning), `[arch §10]` (handoff requirements — row 2c).
> **Cites:** `[const §VI]` (spec coverage — every owned row maps to a coverage-index entry), `[const §VII]` (testing — ≥10 seams), `[const §VIII.5]` (zero-allocation hot path), `[const §X]` (C ABI deferred to 2i; 2c-side commitments only), `[const §XIV.2]` (≤5 pure-virtual cap on plugin interfaces), `[const §XV]` (banned patterns — no `thread_local`, no synchronous logging, no LGPL deps; banned-pattern §13's "eager codegen with no runtime dictionary path" mandates the hybrid model 2c implements), `[const §XVII.1]` (Codex Gate A required), `[const §XVIII]` (post-v1 roadmap — FIX-Latest A-035..A-065 explicitly out of scope), `[const §VI.5]` (Normative References format), `[FIX-SL §3] / [FIX42 §...]/[FIX44 §...]/[FIX50SP2 §...] / [FIXT §5]` (per-version dictionaries; FIXT.1.1 application-version handling), `[SYN §3.3 Q11]` (codegen output format = header-only `constexpr` arrays), `[SYN §3.3 Q12]` (multi-version coexistence = supported, version-namespaced types), `[SYN §3.3 Q13]` (dialect-extension layering = additive at runtime), `[2a §4.2]`, `[2a §7.2]` (`fixpp::decimal_t` substitution at FLOAT accessors), `[2b §4.3]`, `[2b §4.6]`, `[2b §6.4]`, `[2b §6.6]`, `[2b §7.2]` (wire surface that typed messages and `dict::table_view` consume).
> **Catalogue rows owned (in part):** **Dictionary infrastructure:** D-001..D-011 (XML loader, runtime dictionary, version registry, dialect overlays, custom-tag promotion, generated metadata tables, codegen pipeline), OSS-001 (QuickFIX-XML compatible loader), OSS-010 (header-only generated typed messages with `constexpr` field metadata). **Application-message generated typed-message classes + `constexpr` field metadata** (typed-message *classes only* — parse/serialize/validate behaviour is owned by **2b**): A-001..A-034 (order-management, A-024 dropped per `[SYN §4.4]`), M-001..M-012 (market data), P-001..P-008 (post-trade), C-001..C-003 (collateral / positions / account), R-001..R-005 (reg / IOI / news), N-001..N-003 (network counterparty / user request). FIX-Latest A-035..A-065 are **post-v1.0** per `[const §XVIII.2]` and explicitly out of 2c scope.
> **Convergence log:** see end-of-doc Appendix C — to be populated after Codex Gate A.

---

## 1. Goals

1. Define the public surface of `fixpp::dict` — the runtime metadata types (`Dictionary`, `FieldRef`, `ComponentRef`, `GroupRef`, `XmlLoader`, `DialectOverlay`, `table_view`) and the per-version generated typed-message namespaces (`fixpp::v42`, `fixpp::v44`, `fixpp::v50sp2`, `fixpp::vt11`) — so every consumer (`wire` per 2b, `session`, `capi` per 2i, `bindings/python` per 2m) compiles against a single locked interface.
2. Lock the **codegen output format** as **header-only `constexpr` arrays** per `[SYN §3.3 Q11]`: every per-version artifact (`Messages.hpp`, `Fields.hpp`, `Validator.hpp`) is a header-only translation unit emitted into the build tree (`build/<preset>/_codegen/include/fixpp/<vXX>/`, never the source tree per `[arch §7.2]`); compile-time cost is the trade we accept; C++23 modules / PCH are explicitly post-v1.0 per `[SYN §3.3 Q11]`.
3. Lock the **multi-version coexistence model** per `[SYN §3.3 Q12]`: a single binary may host FIX 4.2, FIX 4.4, FIX 5.0 SP2, and FIXT.1.1 simultaneously; `fixpp::v42::NewOrderSingle` and `fixpp::v50sp2::NewOrderSingle` are distinct types under distinct namespaces; each `Session` owns one `Dictionary`; the typed surface is parameterised by version at compile time (per-namespace), and the C-ABI surface (owned by **2i**) carries a runtime version tag on `fixpp_msg_t`.
4. Lock the **dialect-extension layering** per `[SYN §3.3 Q13]`: per-session FIX dialect overrides (catalogue row **COM-011**) compose **additively at runtime** onto the loaded standard `Dictionary`; no full pre-built dialect dictionary is required. Validation is dialect-aware. The overlay is value-typed by default (§4.4 decision), composed at session-open time, frozen for the session's lifetime per `[arch §5.6]`.
5. Specify the **typed-message flyweight contract** owned here: every generated `fixpp::v42::NewOrderSingle{view}` etc. is a flyweight over a `wire::View`-derived `MessageView` (per `2b §4.3`), inherits `2b §6.4`'s lifetime contract, never owns bytes, never allocates. Per-tag accessors carry `[[clang::lifetimebound]]` on view-returning methods and `[[nodiscard]]` on every `expected_t<T>`-returning method.
6. Specify the cross-strand **view-escape hatch** named in `2b §6.6`: `MessageView::reify(mr) → fixpp::vXX::owning_message_t<MsgClass>` — a deep-copy of the parsed message into caller-owned PMR storage, version-tagged, safe to move across threads. This document owns the type's exact shape, arena binding, lifetime class, and interaction with the per-version typed accessors (§4.8).
7. Stay zero-allocation on the hot path between parse and `fromApp` per `[const §VIII.5]` and `[2b §6.6]`'s three-arena pinning. Codegen output is `constexpr` — static storage, no allocation. Typed-message accessors are flyweights — no allocation.
8. Stay exception-free on the hot path per `[arch §5.3]`. Exceptions are reserved for **construction-time** `XmlLoader` failures (bad XML, unknown FIX version, malformed dialect overlay file) — the same ergonomic carve-out 2a/2b take.

### 1.1 Scope boundary — what 2c owns vs what it doesn't

2c owns the *typed-message classes themselves* and the *constexpr metadata* they consult; it does **not** own:

- **Wire-format mechanics.** Parse / serialize / validate of bytes-on-the-wire is owned by **2b** (`Parser`, `Writer`, `Validator`, `Framer`). Typed messages reuse 2b's primitives: a `fixpp::v50sp2::NewOrderSingle` is constructed from a `wire::MessageView<wire::access_mode::Index>` (per `2b §4.3`); its accessors call `MessageView::get<Tag>()` under the hood.
- **Field representation types.** Decimal is `fixpp::decimal_t` from **2a** (per `2a §4.4`'s `FIXPP_DECIMAL_T` alias rule — one symbol set per build); integer/string/timestamp/UTCTimestamp/boolean/MultiCharValue/MultiStringValue/etc. are concrete representations selected by `dict::field_traits<DataType>` specializations (§4.1.3). 2c selects which trait specialization to substitute at each accessor; it does not re-implement field decoding.
- **Session semantics.** Sequence numbers, gap fills, recovery, ResendRequest — owned by `session/` (Phase 4). 2c provides the typed-message *classes* the session FSM dispatches on; it does not interpret OrdStatus, ExecType, or any application semantics.
- **C ABI surface.** `fixpp_msg_t`, `fixpp_dict_t`, the per-tag C-typed accessors (`fixpp_msg_field_int`, `fixpp_msg_field_decimal`, …), and the runtime version tag's bit layout are owned by **2i**. 2c records the *contract* (`fixpp_msg_t` carries a version tag per `[SYN §3.3 Q12]`; §5) and the C++ surface 2i wraps; the C-typed shape itself is 2i's call.

### 1.2 Magnitude domain — codegen footprint and scale boundaries

These caps are observed properties of the per-version dictionaries and a budget for the header set 2c emits. They are **not** wire-layer DoS bounds (those live in `2b §1.2` and apply at parse time on a hostile peer); they bound the *static* footprint of the codegen output and the runtime cost of merging a `DialectOverlay`.

- **Per-version typed-message count.** FIX 4.2: ~50 messages. FIX 4.4: ~95 messages (with components). FIX 5.0 SP2: ~118 messages (the v1.0-locked set, A-001..A-034 + M-/P-/C-/R-/N- families minus A-024). FIXT.1.1: 6 session-layer messages. The codegen tool emits one `class` (or `struct`) per message under each version's namespace.
- **Per-version field-metadata table size.** FIX 5.0 SP2 standard dictionary defines ~1700 distinct field tags and ~300 components. The `Fields.hpp` table per version is a `constexpr std::array<FieldRef, N>` sorted by tag (binary-searchable at compile time and at runtime for the rare lookup that doesn't go through a typed accessor). At 24 bytes per `FieldRef` entry (§4.1) the table is ~40 KiB of static data per version — comfortable for ROM/text-section budgets. Across all four supported versions: ~160 KiB total.
- **Per-version `Validator.hpp` size.** Per-message rule tables (required-field sets, conditional-rule pointers, header/trailer ordering) for FIX 5.0 SP2's ~118 messages: ~30 KiB. Across all four versions: ~100 KiB.
- **Per-version `Messages.hpp` size.** Header text on the order of 200–400 KiB per version (one `class` per typed message; per-tag accessors are inline `constexpr` shells over `wire::MessageView::get<Tag>()`). Across all four versions: ~1.2 MB of header text fed to the compiler when a TU pulls in *all* versions, which is the worst case (a translator/gateway). Single-version TUs see one version's worth.
- **Compile-time cost ceiling.** A single TU including all four `Messages.hpp` headers compiles in **≤ 8 s** on Linux/Clang/x86_64 release at `-O2` (Tier 1 regression bar; §9 seam #2). A TU including only one version compiles in **≤ 3 s**. Above these, the §10 Q1 follow-up (modules/PCH adoption) is reopened.
- **Dialect-overlay merge cost.** A typical per-session `DialectOverlay` adds 5–50 fields and 0–5 messages. The merge into a base `Dictionary` runs once at session open and completes in **≤ 1 ms** on the same hardware (Tier 1 regression bar; §9 seam #4). Above: the overlay is misconfigured (e.g., 10⁴ fields), and `XmlLoader` rejects with `dict_overlay_too_large`.
- **`owning_message_t<>` deep-copy cost.** A typed `reify(mr)` of a 20-tag `NewOrderSingle` into a fresh PMR arena completes in **≤ 1 µs** on the same hardware; a 200-tag Instrument-heavy message completes in **≤ 10 µs** (Tier 1 regression bar; §9 seam #6). The cost is dominated by the byte-copy of the underlying frame plus a re-parse to populate the owning offset table (§4.8).

These are **caller-relevant scale boundaries**, not invariants of the FIX spec; they exist to bound static footprint, codegen compile cost, and per-session merge cost. The §10 Q1 follow-up confirms compile-cost actuals against this budget once 2c implementation lands.

## 2. Non-goals

- **No FIX-Latest application messages (A-035..A-065).** Per `[const §XVIII.2]`, FIX Latest is v1.2; 2c emits typed-message classes only for the v1.0-locked set (FIX 4.2, FIX 4.4, FIX 5.0 SP2 + FIXT.1.1, A-001..A-034 minus A-024, M-/P-/C-/R-/N-). The codegen tool *recognises* unknown MsgTypes in the dictionary XML but emits them only under a `FIXPP_CODEGEN_ENABLE_FIX_LATEST` feature flag scheduled for v1.2; v1.0 builds reject `<message msgtype="…">` entries outside the locked set with a build-time codegen warning (downgradable to error in CI).
- **No SOFH / SBE / FAST / FIXP / JSON / GPB / FIX MMT codegen.** Per `[const §XVIII.2]` these are v1.1+ shipping targets; 2c v1.0 emits Tag=Value SOH typed messages only. The codegen tool's output IR is encoding-agnostic on paper (the same `FieldRef` metadata can drive an SBE encoder), but the v1.0 output is exclusively the header-only `constexpr` Tag=Value form `[SYN §3.3 Q11]`.
- **No runtime dictionary mutation after session open.** Per `[arch §5.6]`, `SessionConfig` (and therefore the active `Dictionary` + `DialectOverlay`) is frozen at session open. Hot-swapping a dictionary mid-session is out of scope; the supported pattern is close the session, swap, reopen.
- **No codegen-as-a-runtime-service.** `tools/codegen/fixpp-codegen` is a build-time host tool per `[const §III.5]`; it does not link into the engine binary and is not invokable from C++ user code. Runtime *additions* to a loaded standard dictionary go through `DialectOverlay` (§4.4), not by re-running codegen.
- **No `std::variant`-of-typed-messages dispatch surface.** Each generated typed message is its own class; the session FSM dispatches by `MsgType` string and constructs the appropriate typed view. A "sum type over all messages" is rejected — every additional message in the variant inflates compile time and overload-resolution cost across every TU. Users wanting variant-style dispatch build their own.
- **No reflection-based field iteration.** Typed messages expose named per-tag accessors (`order.cl_ord_id()`, `order.symbol()`, …) generated from the dictionary; bulk iteration goes through the underlying `wire::MessageView::begin()/end()` (per `2b §4.3`), which is the iterator the user gets if they want to walk every field generically. No `magic_get`-style reflection over the typed class members.
- **No `__attribute__((constructor))` global registry.** Each typed-message class is referenced by name when the user constructs one; there is no auto-registration of "every typed message in this version" into a global table. This avoids static-initialisation ordering problems and ROM bloat in TUs that only use a few messages.

## 3. Inherited surface

From `[arch §4.2]`:

> `fixpp::dict::Dictionary` — runtime, owns field metadata for one FIX version + dialect overlays.
> `fixpp::dict::XmlLoader` — QuickFIX-XML compatible loader (`FIX42.xml`, `FIX44.xml`, …).
> `fixpp::dict::DialectOverlay` — per-session overrides on top of a base dictionary `[SYN §3.3 Q13]`.
> `fixpp::v42::*`, `fixpp::v44::*`, `fixpp::v50sp2::*`, `fixpp::vt11::*` — generated typed messages.
> `fixpp::dict::ComponentRef`, `fixpp::dict::FieldRef`, `fixpp::dict::GroupRef` — metadata accessors used by typed messages and validator.
>
> Codegen pipeline (locked):
> 1. `tools/codegen/fixpp-codegen` reads `dictionaries/FIXxx.xml`.
> 2. Emits `include/fixpp/<vXX>/Messages.hpp` (typed messages — flyweights), `include/fixpp/<vXX>/Fields.hpp` (`constexpr` field metadata tables), `include/fixpp/<vXX>/Validator.hpp` (per-message rules).
> 3. CMake target `fixpp::dict::generate-vXX` runs at configure time; outputs go into the build tree, not the source tree, so a dirty checkout never carries stale codegen.

From `[arch §10]` row 2c:

> Dictionary codegen — Header layout, multi-version coexistence, dialect overlay binding — cross-cutting hooks: §4.2; §3 namespaces.

From `[arch §3]`:

> `fixpp::dict` — Runtime dictionary, XML loader, dialect overlays. Module: `dictionary`.
> `fixpp::v42`, `fixpp::v44`, `fixpp::v50sp2`, `fixpp::vt11` (FIXT.1.1) — Generated typed messages, version-namespaced `[SYN §3.3 Q12]`. One `Messages.hpp` per version.

From `[arch §5.5]`:

> Flyweights are the rule for `wire::View`, typed messages, and offset-table accessors. They never own buffers `[SYN §3.1 Q2]`.
> `[[clang::lifetimebound]]` marks every view-returning constructor and accessor.

From `[arch §5.6]`:

> `SessionConfig` is value-typed and frozen at session open. No mid-session reconfiguration of dictionary, security profile, message store, executor, lock policy. Mutating ops (e.g., dialect overlay swap) go through their own APIs and are explicitly thread-aware.

From `[arch §6]`:

> Each pluggable interface gets ≤5 pure-virtual methods `[const §XIV.2]`. Larger surfaces require a one-paragraph justification reviewed at Gate A.

From `[arch §7.3]`, `[arch §7.4]`, `[arch §9.1]`, `[arch §9.2]` (header surface, CMake targets, public/internal split, versioning) — applied verbatim in §4 and §7 below.

From `[const §XV.13]` (banned patterns):

> Eager codegen with no runtime dictionary path. Hybrid mandated: codegen for standard fields (D-008), runtime XML loader for custom (D-007 + D-009).

This document **delivers the hybrid**: codegen emits the standard per-version fields/messages as `constexpr` arrays; `XmlLoader` plus `DialectOverlay` cover D-007 (custom-field handling) and D-009 (runtime dictionary loading) at session open.

This document refines that surface; it does **not** diverge.

## 4. Public C++ API

The dictionary module's public surface lives under `include/fixpp/dict/`. The generated typed-message headers live under `include/fixpp/<vXX>/` in the build tree (not the source tree, per `[arch §7.2]`).

### 4.1 `fixpp::dict::FieldRef` — per-tag metadata

The smallest and most-replicated metadata unit: one `FieldRef` per (version, tag, message-context) triple in `Fields.hpp`. Lifetime: `constexpr` static storage.

```cpp
// include/fixpp/dict/field_ref.hpp
namespace fixpp::dict {

// Field data type per [FIX50SP2 §3.3]. Compile-time enumeration; the fully
// expanded type list (PRICE, QTY, AMT, PRICEOFFSET, PERCENTAGE, INT, LENGTH,
// SEQNUM, NUMINGROUP, STRING, MULTIPLEVALUESTRING, MULTIPLECHARVALUE, CHAR,
// CURRENCY, EXCHANGE, COUNTRY, MONTHYEAR, UTCTIMESTAMP, UTCTIMEONLY,
// UTCDATEONLY, LOCALMKTDATE, TZTIMEONLY, TZTIMESTAMP, BOOLEAN, DATA,
// XMLDATA, LANGUAGE) is fixed at the FIX 5.0 SP2 spec level; older versions
// are subsets.
enum class data_type : std::uint8_t {
    Int, Length, SeqNum, NumInGroup, DayOfMonth,
    Price, Qty, Amt, PriceOffset, Percentage, Float,
    Char, Boolean,
    String, MultiCharValue, MultiStringValue,
    Currency, Exchange, Country, MonthYear,
    UtcTimestamp, UtcTimeOnly, UtcDateOnly, LocalMktDate, TzTimeOnly, TzTimestamp,
    Language,
    Data, XmlData,
    // Sentinel for dialect-introduced types not in the standard set:
    DialectExtension,
};

// Field-presence rule. Encoded explicitly so a single FieldRef carries both
// "is this field declared on this MsgType?" and "if so, is it required?".
// Conditional-required rules carry a non-null `condition` index into the
// per-message conditional-rule table (§4.1.4); the wire-layer Validator
// (`2b §4.6` rule 6) consults the table to decide presence at validate time.
enum class presence : std::uint8_t {
    NotDeclared = 0,    // tag is not part of this MsgType's grammar
    Optional    = 1,
    Required    = 2,
    Conditional = 3,    // see condition_index_; consult table
};

struct FieldRef {
    std::uint16_t tag;                // 0..65535 (matches `2b §1.2`'s wire range)
    data_type     type;               //  1 byte
    presence      rule;               //  1 byte
    std::uint16_t condition_index;    //  index into per-message conditional-rule
                                      //  table; 0 if rule != Conditional.
                                      //  Indirection chosen over inline closure
                                      //  to keep FieldRef trivially copyable
                                      //  and constexpr-friendly.
    std::uint16_t group_no_tag;       // 0 if not inside a group; otherwise
                                      // the NoXxx tag of the enclosing group.
                                      // Encodes group-context with one indirection.
    std::uint16_t component_index;    // 0 if not inside a component; otherwise
                                      // an index into the per-version
                                      // ComponentRef table (§4.2).
    std::uint16_t enum_table_index;   // 0 if not enum-constrained; otherwise
                                      // an index into a per-version
                                      // constexpr enum-value table.
    std::uint16_t length_pair_data_tag; // For LENGTH-typed fields paired with
                                      // a DATA field per `[FIX50SP2 §3]`
                                      // Length+Data semantics: the tag of the
                                      // following DATA field. 0 if not paired.
                                      // This is the table 2b §4.3's
                                      // field_iterator's static `constexpr`
                                      // Length+Data table is built from
                                      // (§7.1 + §10 Q2).
    std::uint16_t _reserved;          // padding to 16 bytes for cache-line
                                      // friendliness; reserved for future flags
                                      // under FIXPP_DICT_FIELDREF_RESERVED_USED.
};
static_assert(sizeof(FieldRef) == 16);
static_assert(alignof(FieldRef) == 2);
static_assert(std::is_standard_layout_v<FieldRef>);
static_assert(std::is_trivially_copyable_v<FieldRef>);

}  // namespace fixpp::dict
```

Notes:

- **One `FieldRef` per (MsgType, tag) pair**, not per tag globally. Tag 11 (`ClOrdID`) appears in NewOrderSingle's `FieldRef` table (presence=Required), in ExecutionReport's (presence=Optional), in OrderCancelRequest's (presence=Required), etc. — each carries the `presence` rule appropriate to its message context. The per-version `Fields.hpp` therefore holds *all per-MsgType FieldRefs* sorted first by MsgType-index then by tag; the per-message `Validator.hpp` references slices into this array.
- **Group context is one `uint16_t` indirection** (`group_no_tag`), not a recursive embedded structure. A field inside a nested group (e.g., `LegInstrument` inside `NoLegs`) carries the innermost `NoXxx` tag; the per-version `GroupRef` table (§4.2) carries the parent-of-group chain so `wire::Validator` can resolve nesting at validate time. This keeps `FieldRef` trivially copyable (a hard requirement for `constexpr` storage of arrays of millions of entries — §1.2 budget).
- **`condition_index` is an indirection**, not an embedded `std::function`. Conditional-Required rules per `[FIX50SP2 §3.4]` are codegen'd as a per-message `constexpr std::array<conditional_rule, N>` whose entries are simple structs (e.g., `{tag, predicate_id, payload}`); the `wire::Validator`'s default impl interprets them. No type-erased callable; no allocation. The full conditional-rule grammar is small (the FIX 5.0 SP2 spec defines ~15 distinct predicate shapes — "if tag X is present then tag Y must be present", "if tag Z = 'value' then tag W must be present", etc.) and is enumerated in `Validator.hpp` per version.
- **`length_pair_data_tag`** is the 2c-side commitment for `2b §4.3`'s `field_iterator` Length+Data static table. See §7.1 for the binding mechanism and §10 Q2 for the dialect-overlay extension question.

### 4.2 `fixpp::dict::ComponentRef` and `fixpp::dict::GroupRef`

Components are FIX 4.4+'s reusable field bundles (e.g., `Instrument`, `Parties`, `OrderQtyData`). Groups are repeating-field tuples (e.g., `NoLegs`/`Legs`, `NoMDEntries`/`MDEntries`).

```cpp
// include/fixpp/dict/component_ref.hpp
namespace fixpp::dict {

struct ComponentRef {
    std::uint16_t component_id;        // unique per version
    std::uint16_t name_offset;         // offset into per-version name string pool
                                       // (constexpr std::string_view; for diagnostics)
    std::uint16_t first_field_index;   // index into the per-version FieldRef array
    std::uint16_t field_count;         // number of fields in this component
    std::uint16_t parent_component_id; // 0 if top-level; otherwise the enclosing
                                       // component (components nest in FIX 4.4+)
    std::uint16_t _reserved;
};
static_assert(sizeof(ComponentRef) == 12);
static_assert(std::is_trivially_copyable_v<ComponentRef>);

struct GroupRef {
    std::uint16_t no_tag;              // NoXxx tag (e.g., 73 for NoOrders, 555 for NoLegs)
    std::uint16_t first_field_tag;     // First field of group rule per [FIX50SP2 §3]
                                       // — used by wire::Validator (`2b §4.6`'s
                                       // group_first_field) and by group_view::iter()
                                       // (`2b §4.7`).
    std::uint16_t first_field_index;   // index into the per-version FieldRef array
                                       // for the group's field list
    std::uint16_t field_count;
    std::uint16_t parent_group_no_tag; // 0 if not nested; otherwise the enclosing
                                       // group's NoXxx tag (handles W-007 nested
                                       // repeating groups per `2b §4.7`).
    std::uint16_t _reserved;
};
static_assert(sizeof(GroupRef) == 12);
static_assert(std::is_trivially_copyable_v<GroupRef>);

}  // namespace fixpp::dict
```

The `_reserved` byte in each table mirrors the 2a `_reserved` discipline (forward-compat under `FIXPP_DICT_FIELDREF_RESERVED_USED` etc.); ignored on read in v1.0.

### 4.3 `fixpp::dict::Dictionary` — runtime owner + dialect-overlay binding

`Dictionary` is the runtime handle a `Session` holds. It is constructed by `XmlLoader::load(...)` (§4.5), composed with an optional `DialectOverlay` (§4.4) at session open, and frozen for the session lifetime per `[arch §5.6]`.

```cpp
// include/fixpp/dict/dictionary.hpp
namespace fixpp::dict {

// Compile-time enumeration of the supported FIX versions. One value per
// generated namespace. Carried into `fixpp_msg_t` as a runtime tag at the
// C-ABI boundary (`[SYN §3.3 Q12]`; §5).
enum class version : std::uint8_t {
    v42      = 1,    // fixpp::v42
    v44      = 2,    // fixpp::v44
    v50sp2   = 3,    // fixpp::v50sp2
    vt11     = 4,    // fixpp::vt11 (FIXT.1.1 session layer)
    // 0 reserved as "unknown / not set". Future versions slot in numerically;
    // the v1.x roadmap (FIX-Latest, etc.) gets distinct values per
    // `[const §XVIII.2]`.
};

// Lifetime: typically constructed once at engine init from XmlLoader output,
// optionally composed with one DialectOverlay per session, then frozen.
// Owned-storage: holds `pmr::vector` views over the FieldRef / ComponentRef /
// GroupRef / conditional-rule arrays, plus the dialect-overlay merge result.
//
// Storage classes:
//   - The standard per-version FieldRef/ComponentRef/GroupRef tables are
//     `constexpr` (`Fields.hpp` per version) — borrowed via span, not copied.
//   - The dialect-overlay's *additions and overrides* are heap-allocated on a
//     PMR resource the user supplies at construction (typically the session's
//     long-lifetime arena; see §8 and `[arch §5.2]`).
//   - The composed lookup tables (the merged FieldRef view consumed by
//     wire::Validator and codegen accessors) live in the same dialect-overlay
//     PMR storage; building the merged view is the §6.4 operation.
class Dictionary {
public:
    [[nodiscard]] version which() const noexcept;

    // Look up a single field by tag in the *MsgType context*. Returns the
    // composed (overlay + standard) FieldRef; presence reflects the rule for
    // this message type. Returns NotDeclared if the tag is not part of this
    // MsgType's grammar (after overlay merge).
    [[nodiscard]] FieldRef
    field_ref(std::string_view msg_type, std::uint16_t tag) const noexcept;

    // Required-field set for a given MsgType (after overlay merge). Returned
    // span aliases Dictionary-owned storage; lifetime is the Dictionary's.
    [[nodiscard]] std::span<std::uint16_t const>
    required_fields(std::string_view msg_type) const noexcept
        [[clang::lifetimebound]];

    // Is `tag` declared for `msg_type` per the composed dictionary?
    // Drives `wire::Validator` rule 5 (`2b §6.5`).
    [[nodiscard]] bool
    field_valid_for(std::string_view msg_type, std::uint16_t tag) const noexcept;

    // First-field-of-group rule per `[FIX50SP2 §3]` — used by wire's
    // group_view::iter() (`2b §4.7`) and Validator (`2b §4.6`'s
    // group_first_field).
    [[nodiscard]] std::uint16_t
    group_first_field(std::uint16_t no_tag) const noexcept;

    // Length+Data pair lookup for `2b §4.3`'s field_iterator dict-free path
    // and for Index-mode parsing. The standard (codegen'd) table is the
    // exhaustive FIX 5.0 SP2 list per `[FIX50SP2 §3.3]`; dialect overlays
    // may extend it (§7.1).
    [[nodiscard]] std::uint16_t
    length_pair_data_tag(std::uint16_t length_tag) const noexcept;

    // Overlay-aware promotion: was this tag a "previously-unknown custom" tag
    // (D-007 / COM-011) that the active overlay promoted into the typed
    // surface? Diagnostic / observability hook; not on the hot path.
    [[nodiscard]] bool was_dialect_promoted(std::uint16_t tag) const noexcept;

    // Compose this base Dictionary with a DialectOverlay. Returns a *new*
    // Dictionary with the merged tables; the original (this) is unchanged.
    // Allocations come from `mr` per `[arch §5.2]`. The mr MUST outlive
    // the returned Dictionary.
    //
    // Construction-time API: called by Session::open() before the session
    // is exposed to the user. Failure (overlay tag conflict that the
    // policy declines, malformed overlay) returns
    // expected_t<Dictionary> with an error::dict_overlay_* code (§6.7).
    [[nodiscard]] expected_t<Dictionary>
    with_overlay(DialectOverlay const& overlay,
                 std::pmr::memory_resource* mr) const noexcept;

    // Bridge to wire's table_view (§4.6). Cheap (returns a value-typed
    // borrowed handle); aliases this Dictionary's storage. The returned
    // table_view is what wire::Parser<Index> and wire::Validator consume.
    [[nodiscard]] table_view as_table_view() const noexcept
        [[clang::lifetimebound]];

private:
    // Implementation detail: holds spans over the standard `constexpr`
    // tables + a merged-overrides storage region in the user-supplied mr.
};

}  // namespace fixpp::dict
```

`Dictionary` is move-only: copying would silently duplicate the merged-overlay storage. Sessions hold a `Dictionary` by value (in `SessionConfig`); the session's PMR resource owns the overlay storage.

### 4.4 `fixpp::dict::DialectOverlay` — additive-merge value type

**v0.1 design decision: `DialectOverlay` is a value type, not a runtime virtual interface.** The catalogue row COM-011 ("dialect overlay") and `[SYN §3.3 Q13]` describe the *behaviour* (additive at runtime, per-session); they do not require pluggability. A runtime virtual interface would buy the ability to inject computed-on-the-fly overlay rules (e.g., a regulator-supplied OPRA-feed rules engine), but the v1.0 use cases — venue dialects, regulator-mandated tags, COM-011 customer-overlay scenarios — are all *static data* known at session open and loaded from XML or built programmatically. A virtual interface would (a) pay vtable overhead on every per-tag lookup, (b) prevent the overlay-merge result from being a `Dictionary`-owned `constexpr`-flavour table, and (c) require justification against the `[const §XIV.2]` ≤5-pure-virtual cap. Justification: none of the tested overlay scenarios need it. The decision is reversible — a future minor-version `DialectOverlayPlugin` virtual interface can be added without breaking the value-typed `DialectOverlay` API; tracked in §10 Q3.

```cpp
// include/fixpp/dict/dialect_overlay.hpp
namespace fixpp::dict {

// Conflict policy when an overlay's field/message definition collides with
// the base dictionary's. Default: OverlayWins (the overlay's rule replaces
// the base's; matches the "additive at runtime" framing of `[SYN §3.3 Q13]`
// where additions extend and overrides supersede).
enum class overlay_conflict_policy : std::uint8_t {
    OverlayWins = 0,   // default; overlay's rule replaces base's
    BaseWins    = 1,   // overlay is "advisory only"; base prevails
    Reject      = 2,   // any collision returns error::dict_overlay_conflict
};

struct DialectOverlay {
    // Field additions and overrides. Each entry overrides the same (msg_type,
    // tag) pair in the base Dictionary if present, or adds a new declaration
    // if not.
    std::pmr::vector<FieldRef> field_additions;

    // Message additions. Each entry registers a new MsgType not present in
    // the base dictionary (typical use: COM-011 dialect-private MsgTypes).
    // Existing MsgTypes cannot be wholesale replaced; field-level overrides
    // suffice for the supported scenarios.
    std::pmr::vector<message_addition> message_additions;

    // Length+Data pair extensions for `2b §4.3`'s field_iterator. Each entry
    // registers a (length_tag, data_tag) pair for a dialect-private binary
    // BLOB field. See §7.1 + §10 Q2 for the dict-free-path implications.
    std::pmr::vector<length_pair_addition> length_pair_additions;

    overlay_conflict_policy conflict_policy = overlay_conflict_policy::OverlayWins;

    // Diagnostic name (logged when an overlay merges); not load-bearing.
    std::pmr::string name;
};

struct message_addition {
    std::pmr::string msg_type;            // e.g., "U1" for a custom MsgType
    std::pmr::vector<FieldRef> fields;    // the message's grammar
    std::pmr::vector<std::uint16_t> required_fields;
    // No conditional-rule support in v1.0 dialect additions; the base
    // dictionary's conditional rules apply unchanged. Adding conditional
    // rules at the overlay layer is a v1.x follow-up; tracked in §10 Q4.
};

struct length_pair_addition {
    std::uint16_t length_tag;
    std::uint16_t data_tag;
};

}  // namespace fixpp::dict
```

`DialectOverlay` is a plain value type holding `pmr::vector`s. It is constructed either:

- by `XmlLoader::load_overlay(path, mr)` — reads a QuickFIX-XML-style overlay file (a `<fix>...</fix>` document with the same schema as the base FIX dictionary, containing only the additions/overrides), or
- programmatically by user code that wants to build overlays in-process (e.g., a translator that reads venue-config from a database and constructs `FieldRef` entries).

The merge into a `Dictionary` runs in `Dictionary::with_overlay(...)` (§4.3) — **once at session open**, not per-message. The merge result is stored in the user-supplied PMR resource; subsequent per-message lookups consult the merged tables with no further allocation. Merge cost ≤ 1 ms per §1.2.

### 4.5 `fixpp::dict::XmlLoader` — QuickFIX-XML compatible loader

```cpp
// include/fixpp/dict/xml_loader.hpp
namespace fixpp::dict {

class XmlLoader {
public:
    // Construction-time exception allowed per `[arch §5.3]`. The hot path
    // (parse / serialize / validate) is exception-free; XmlLoader runs only
    // at engine init / session open, where the alternative is
    // expected_t<Dictionary> at a call site that reads "throws on bad XML"
    // ergonomically.
    XmlLoader();

    // Load a per-version standard or QuickFIX-XML-format dictionary from a
    // file path. Allocates from `mr`; the returned Dictionary borrows the
    // standard `constexpr` tables (no copy) and stores XML-only fields
    // (e.g., XML-defined custom tags not present in the standard) in mr.
    //
    // Throws `dict::xml_parse_error` (which derives from `std::runtime_error`)
    // on malformed XML; throws `dict::unknown_version_error` on a version
    // string outside the supported set; otherwise returns the loaded
    // Dictionary by value.
    [[nodiscard]] Dictionary
    load(std::filesystem::path const& xml_path,
         std::pmr::memory_resource* mr);

    // Load a dialect overlay XML file. Same exception discipline.
    [[nodiscard]] DialectOverlay
    load_overlay(std::filesystem::path const& xml_path,
                 std::pmr::memory_resource* mr);

    // Bypass for in-process construction (testing, programmatic overlay
    // building, embedded scenarios where filesystem isn't available).
    [[nodiscard]] Dictionary
    load_from_string(std::string_view xml_text,
                     std::pmr::memory_resource* mr);

    [[nodiscard]] DialectOverlay
    load_overlay_from_string(std::string_view xml_text,
                             std::pmr::memory_resource* mr);
};

}  // namespace fixpp::dict
```

`XmlLoader` exists primarily for D-007 (custom-field handling) and D-009 (runtime dictionary loading from QuickFIX XML). Standard per-version dictionaries are *also* loadable through it (round-trip the QuickFIX XML), but the typical session uses the codegen'd `constexpr` tables from `Fields.hpp` directly and applies an overlay (loaded via `XmlLoader::load_overlay`) on top.

XML schema compatibility: `XmlLoader` accepts the QuickFIX XML schema (`fields`, `messages`, `components`, `header`, `trailer` top-level elements per `[OSS-001]` / D-009). Extensions for dialect overlays add a top-level `<overlay>` wrapper; outside that wrapper the overlay file's schema is identical to the base file's.

### 4.6 `dict::table_view` — value-typed borrowed handle into a `Dictionary`

`table_view` is the type `wire::Parser<Index>` and `wire::Validator` consume per `2b §7.2`. It is a *value-typed* view (not a virtual interface) into a `Dictionary`'s composed tables.

```cpp
// include/fixpp/dict/table_view.hpp
namespace fixpp::dict {

// A value-typed handle into a Dictionary's composed metadata. Cheap to copy
// (a few pointers + a span), borrows the underlying Dictionary storage,
// MUST NOT outlive the Dictionary.
//
// Returned by Dictionary::as_table_view() (§4.3). Held by value inside
// wire::Parser<Index> (`2b §4.3`) and wire::dictionary_driven_validator
// (`2b §4.6`). Copy/move: trivially copyable; no ownership transfer.
//
// Lifetime: aliases the Dictionary's merged-overlay storage; same lifetime
// class as `wire::View` flyweights (`2b §4.1`). [[clang::lifetimebound]]
// on the constructor's Dictionary parameter binds the lifetime.
class table_view {
public:
    constexpr table_view() noexcept = default;

    [[nodiscard]] FieldRef
    field_ref(std::string_view msg_type, std::uint16_t tag) const noexcept;

    [[nodiscard]] std::span<std::uint16_t const>
    required_fields(std::string_view msg_type) const noexcept
        [[clang::lifetimebound]];

    [[nodiscard]] bool
    field_valid_for(std::string_view msg_type, std::uint16_t tag) const noexcept;

    [[nodiscard]] std::uint16_t
    group_first_field(std::uint16_t no_tag) const noexcept;

    [[nodiscard]] std::uint16_t
    length_pair_data_tag(std::uint16_t length_tag) const noexcept;

    [[nodiscard]] version which() const noexcept;

private:
    friend class Dictionary;
    explicit constexpr table_view(detail::dict_metadata_handle h) noexcept;
    detail::dict_metadata_handle handle_{};   // ~3 pointers
};

}  // namespace fixpp::dict
```

`table_view` is **borrowed** (does not own), **trivially copyable** (sized to ~3 pointers — a span over the per-version `FieldRef` array, a span over the dialect-overlay overrides, and a small `(version, msg_type_index_root)` discriminator). Copy/move semantics: trivial copy, no ownership transfer. Aliases the `Dictionary`'s storage; the `Dictionary` MUST outlive every `table_view` taken from it.

The `wire::dictionary_driven_validator` (per `2b §4.6`) holds a `table_view` by value. The `Dictionary` lives in `SessionConfig` (per `[arch §4.4]` SessionConfig field list); the `table_view` aliases it for the session's lifetime — both are frozen after session open, so the lifetime relationship is straightforward.

### 4.7 Generated typed messages — `fixpp::vXX::*`

Every typed-message class is generated by `fixpp-codegen` into `include/fixpp/<vXX>/Messages.hpp`. The class shape is uniform across versions and across messages; only the per-tag accessor list and the `MsgType` constant vary.

```cpp
// build/<preset>/_codegen/include/fixpp/v50sp2/Messages.hpp  (excerpt)
namespace fixpp::v50sp2 {

// Generated typed-message class. Flyweight over a wire::MessageView<Index>;
// inherits the lifetime contract of `2b §6.4`. Constructor binds the view
// by reference; the typed message MUST NOT outlive the view (which itself
// MUST NOT outlive the originating frame buffer).
class NewOrderSingle {
public:
    static constexpr std::string_view msg_type_v = "D";  // [FIX50SP2 §...]
    static constexpr dict::version    version_v  = dict::version::v50sp2;

    // Construct from a wire::MessageView<Index>. Validation is the user's
    // responsibility (typically the session FSM has already run
    // wire::Validator::validate(view)); this constructor does not validate.
    explicit constexpr NewOrderSingle(
        wire::MessageView<wire::access_mode::Index> const& view
            [[clang::lifetimebound]]) noexcept
        : view_(view) {}

    // Per-tag typed accessors. Each one is an inline constexpr shell over
    // wire::MessageView::get<Tag>() with the field-traits dispatch baked in
    // by codegen. [[nodiscard]] because expected_t<T> is the return type.
    // [[clang::lifetimebound]] on view-returning accessors (string_view,
    // span). The Tag template arg is the constexpr tag number from
    // `[FIX50SP2 §...]`.

    [[nodiscard]] constexpr expected_t<std::string_view>
    cl_ord_id() const noexcept [[clang::lifetimebound]]
    { return view_.template get_string<11>(); }

    [[nodiscard]] constexpr expected_t<std::string_view>
    symbol() const noexcept [[clang::lifetimebound]]
    { return view_.template get_string<55>(); }

    [[nodiscard]] constexpr expected_t<char>
    side() const noexcept
    { return view_.template get_char<54>(); }

    [[nodiscard]] constexpr expected_t<fixpp::decimal_t>
    order_qty() const noexcept
    { return view_.template get_decimal<38>(); }

    [[nodiscard]] constexpr expected_t<fixpp::decimal_t>
    price() const noexcept
    { return view_.template get_decimal<44>(); }

    // Repeating-group accessor: returns a wire::group_view<Leg> bound to
    // the underlying offset table. group_view contract from `2b §4.7`;
    // Leg is a per-message generated struct (also under fixpp::v50sp2).
    [[nodiscard]] constexpr wire::group_view<NewOrderSingle::Leg>
    legs() const noexcept [[clang::lifetimebound]]
    { return view_.template group<555 /* NoLegs */, NewOrderSingle::Leg>(); }

    // ... (one accessor per declared field; codegen emits the full set)

    // Bridge to wire view for advanced consumers (raw bytes, iteration).
    [[nodiscard]] constexpr wire::MessageView<wire::access_mode::Index> const&
    view() const noexcept [[clang::lifetimebound]] { return view_; }

    // Reify into an owning copy, for cross-strand transport. See §4.8.
    [[nodiscard]] expected_t<owning_message_t<NewOrderSingle>>
    reify(std::pmr::memory_resource* mr) const noexcept;

    // Per-message group struct (also a flyweight).
    class Leg {
        // ... per-tag accessors for the group entry's fields ...
    };

private:
    wire::MessageView<wire::access_mode::Index> const& view_;
};

}  // namespace fixpp::v50sp2
```

Key properties:

- **Flyweight contract.** The typed message holds a `MessageView` by reference; copying the typed message rebinds to the same view. The `[[clang::lifetimebound]]` on the constructor parameter chains the lifetime warning into the typed message's accessors. Per `[arch §5.5]` and `2b §6.4`.
- **Zero allocation per accessor.** Every typed accessor is `constexpr` and `noexcept` and dispatches directly to `wire::MessageView::get<Tag>()` (which is itself zero-allocation per `2b §4.3`). The compiler inlines the chain; the per-accessor cost is one `OffsetTable::find` call (~15 ns per `2b §6.6`).
- **`[[nodiscard]]` on every `expected_t<T>`-returning method.** Mandated by the convergence-log-frozen rule from 2a/2b reviews; codegen template emits the attribute unconditionally.
- **`[[clang::lifetimebound]]` on every view-returning method** (anything returning `std::string_view`, `std::span`, `wire::group_view<...>`). Codegen emits the attribute unconditionally; per `[arch §5.5]`.
- **`Leg`-and-similar nested group structs** are themselves flyweight types over `wire::group_view<T>::operator[](i)` (per `2b §4.7`); they follow the same accessor discipline.
- **Unknown-fields access.** Every typed message inherits a `unknown_fields()` method from a generated mixin that delegates to `view().unknown_fields()` (per `2b §4.8`); users wanting opaque round-trip preservation walk it directly.
- **Multi-version disambiguation.** `fixpp::v42::NewOrderSingle` and `fixpp::v50sp2::NewOrderSingle` are distinct types under distinct namespaces. They cannot be implicitly converted; a translator/gateway that converts FIX 4.2 → FIX 5.0 SP2 builds an explicit converter (using both typed surfaces and a `fixpp::v50sp2::owning_message_t<NewOrderSingle>` constructed from scratch). 2c does not provide a built-in cross-version converter; that is application-specific (and a candidate for a v1.x utility crate).

### 4.8 `owning_message_t<>` — the cross-strand reify target

`2b §6.6`'s view-escape contract names `MessageView::reify(mr) → fixpp::vXX::owning_message_t<MsgClass>` as the supported deep-copy hatch. This document owns its shape.

**v0.1 design decision: `owning_message_t<MsgClass>` is a per-version, per-message-class generated type (not a generic dict-driven container).** Rationale:

- The typed-message API surface (named accessors, repeating-group `legs()` etc.) is the user-facing benefit of codegen; the reified copy must offer the same surface so user code crossing a strand boundary doesn't degrade to "a `Dictionary` plus a tag-key map".
- Generic dict-driven storage (a `pmr::vector<(tag, value_bytes)>` plus a `Dictionary` reference) costs one extra indirection per accessor and doesn't constexpr-friendly compose with the per-message accessor inlining.
- Per-version, per-message generation is *already paid for* by the typed-message classes; `owning_message_t<MsgClass>` is a sibling generated type, not new codegen weight.
- The cost: one extra class per typed message in `Messages.hpp`. At ~118 messages × 4 versions (~470 classes) and ~1 KiB each of generated code, this adds ~470 KiB to the per-version header set — within the §1.2 compile-cost budget.

```cpp
// build/<preset>/_codegen/include/fixpp/v50sp2/Messages.hpp  (excerpt continued)
namespace fixpp::v50sp2 {

// Owning copy of a NewOrderSingle, deep-copied from a wire::MessageView<Index>.
// Owns its own bytes (copied into the supplied PMR memory_resource), its own
// OffsetTable (rebuilt over the copied bytes), and exposes the same typed
// accessor surface as NewOrderSingle. Safe to move across thread/strand
// boundaries; lifetime is bounded by the supplied memory_resource.
class owning_NewOrderSingle {
public:
    // Construct from a NewOrderSingle (or directly from a wire::MessageView).
    // mr MUST outlive the owning_message_t. Returns expected_t<...> from
    // NewOrderSingle::reify(mr); construction itself is noexcept after
    // the deep-copy succeeds.
    static expected_t<owning_NewOrderSingle>
    from_view(wire::MessageView<wire::access_mode::Index> const& view,
              std::pmr::memory_resource* mr) noexcept;

    // Move-only. Copy is rejected — copying would silently duplicate the
    // PMR storage, which is an antipattern (the user wanted move-across-
    // strand, not duplicate-on-each-thread).
    owning_NewOrderSingle(owning_NewOrderSingle const&) = delete;
    owning_NewOrderSingle& operator=(owning_NewOrderSingle const&) = delete;
    owning_NewOrderSingle(owning_NewOrderSingle&&) noexcept = default;
    owning_NewOrderSingle& operator=(owning_NewOrderSingle&&) noexcept = default;

    static constexpr std::string_view msg_type_v = "D";
    static constexpr dict::version    version_v  = dict::version::v50sp2;

    // Same accessor surface as NewOrderSingle. Each accessor delegates to
    // the owned MessageView's get<Tag>() (which aliases the owned bytes).
    [[nodiscard]] expected_t<std::string_view> cl_ord_id() const noexcept
        [[clang::lifetimebound]];
    [[nodiscard]] expected_t<std::string_view> symbol() const noexcept
        [[clang::lifetimebound]];
    [[nodiscard]] expected_t<char> side() const noexcept;
    [[nodiscard]] expected_t<fixpp::decimal_t> order_qty() const noexcept;
    [[nodiscard]] expected_t<fixpp::decimal_t> price() const noexcept;
    [[nodiscard]] wire::group_view<NewOrderSingle::Leg> legs() const noexcept
        [[clang::lifetimebound]];
    // ... (one per declared field, mirroring NewOrderSingle)

    // Bridge to a flyweight NewOrderSingle over the owned view, for code
    // paths that work polymorphically against the flyweight type.
    [[nodiscard]] NewOrderSingle as_view() const noexcept
        [[clang::lifetimebound]];

    [[nodiscard]] dict::version which() const noexcept { return version_v; }

private:
    // Owned storage:
    std::pmr::vector<std::byte>                               bytes_;
    wire::frame_view                                          frame_;
    wire::MessageView<wire::access_mode::Index>               view_;
    std::pmr::memory_resource*                                mr_;
};

// Generic alias the rest of the engine refers to. Codegen emits one
// owning_<Msg> class per typed message; the alias maps the generic
// owning_message_t<Msg> the rest of the engine refers to.
template <class Msg> struct owning_message_traits;
template <> struct owning_message_traits<NewOrderSingle> {
    using type = owning_NewOrderSingle;
};
template <class Msg>
using owning_message_t = typename owning_message_traits<Msg>::type;

}  // namespace fixpp::v50sp2
```

Key properties:

- **Per-version, per-message generated.** One `owning_<Msg>` class per typed message (e.g., `owning_NewOrderSingle`, `owning_ExecutionReport`, …) under each version's namespace. The generic `owning_message_t<NewOrderSingle>` alias resolves to `owning_NewOrderSingle` via `owning_message_traits` — the rest of the engine writes `fixpp::v50sp2::owning_message_t<NewOrderSingle>` without naming the underlying type.
- **Owns its bytes.** The deep-copy at `reify(mr)` allocates `bytes_` from `mr` (one allocation, sized to `frame_view::bytes().size()`), `memcpy`s the source frame into it, and rebuilds a `wire::frame_view` + `wire::MessageView<Index>` over the owned bytes (using a fresh `OffsetTable` allocated from the same `mr`).
- **Bound to its `mr`.** The `owning_message_t<>` carries `mr_` so lifetime ends when the user releases the resource. Move-only, copy-deleted.
- **Carries a runtime version tag.** `version_v` is `constexpr` per class; `which()` returns it. The C-ABI `fixpp_msg_t` (per `[SYN §3.3 Q12]`; §5) borrows this for its runtime version discriminator.
- **Allocation cost.** One PMR allocation for `bytes_` (sized to the frame); one allocation for the offset table (sized per `2b §4.4`); one allocation for the hash overlay. Three allocations total per `reify()`. Bench bar: ≤ 1 µs / 20-tag, ≤ 10 µs / 200-tag (§1.2; §9 seam #6).
- **Lifetime contract.** Per `[arch §5.5]`: `owning_message_t<>` is an *owning* type, not a flyweight. Move semantics are enabled; copy is deleted. The accessors return view types (`std::string_view`, `wire::group_view<...>`) that alias the owned bytes; `[[clang::lifetimebound]]` chains the warning correctly (the view aliases `*this`'s bytes, not some external buffer).

The `MessageView::reify(mr)` referenced by `2b §6.6` resolves through the typed-message class's `reify(mr)` method (one per version, per message). For C-ABI consumers (per **2i**), the C-side `fixpp_msg_reify(msg, mr) → fixpp_owning_msg_t*` handle dispatches through the same path with the runtime version tag selecting the right `owning_<Msg>` shape.

## 5. Public C ABI

The C-ABI surface for the `dict_*` and `msg_*` symbol families is **delegated to 2i** per `[arch §4.10]`. 2c records the following 2c-side commitments that 2i must honour:

1. **`fixpp_msg_t` carries a runtime version tag** per `[SYN §3.3 Q12]`. Concretely: an opaque `fixpp_msg_t` holds (internally) a `wire::MessageView<Index>*` plus a `dict::version` byte. The C-side `fixpp_msg_version(fixpp_msg_t)` getter returns the tag. The bit layout, the on-wire size of `fixpp_msg_t`'s opaque struct, and the version-tag's numeric range are 2i's call (subject to `[const §X.1]` SemVer); the *contract* — "every `fixpp_msg_t` is unambiguously associated with one FIX version" — is non-negotiable here.
2. **`fixpp_dict_t` is the C-ABI handle for `dict::Dictionary`.** Opaque; constructed via `fixpp_dict_load_from_xml(path, error_out)` and `fixpp_dict_apply_overlay(dict, overlay, error_out)`. 2i confirms the lifetime/refcounting model.
3. **C-ABI accessors mirror the typed-message accessor family.** For each typed message in §4.7, 2i emits a `fixpp_<msg>_<field>(msg, error_out, value_out)` shape (or a generic `fixpp_msg_field_int(msg, tag, error_out, value_out)` family — 2i's call between the named-accessor and tag-keyed flavours). 2c's commitment is that the *underlying `wire::MessageView<Index>` surface* is stable: 2i can wrap it under either accessor shape without 2c churn.
4. **`owning_message_t<>` reify exposes a C-ABI handle.** The C-ABI `fixpp_msg_reify(msg, mr) → fixpp_owning_msg_t*` returns an opaque handle that delegates internally through the version-tag dispatch to the right `owning_<Msg>::from_view(...)` constructor. 2i owns the `fixpp_owning_msg_t` type, the freeing protocol, and the per-tag accessors over an owning message.
5. **`dict::version` enum maps to a C-ABI constant set.** 2i emits the corresponding `FIXPP_VERSION_42`, `FIXPP_VERSION_44`, `FIXPP_VERSION_50SP2`, `FIXPP_VERSION_T11` constants in `<fix/c_api.h>`. Numeric values match `dict::version` (`v42 = 1`, etc.) so the conversion is identity. The 0 reservation for "unknown" carries through.
6. **Dialect-overlay through C ABI** is symmetric with `XmlLoader::load_overlay`: `fixpp_dialect_overlay_load_from_xml(path, mr, error_out) → fixpp_dialect_overlay_t*`, `fixpp_session_apply_overlay(session, overlay, error_out)`. The session-create entry point (per **2i**'s session API) takes an optional overlay argument; mid-session swap is rejected per `[arch §5.6]`. 2i confirms the exact entry-point names.

The *C-ABI does not expose `dict::FieldRef`/`ComponentRef`/`GroupRef` directly* — those are C++ struct types per `[const §X.2]` (no C++ leakage through C ABI). C-ABI consumers needing field-metadata introspection (e.g., a generic protocol-aware tool) go through 2i's introspection family (`fixpp_dict_field_type(dict, msg_type, tag, …)`) which is C-typed.

## 6. Behavioral contract

### 6.1 Allocation, exceptions, threading on the hot path

- **Allocation.** Codegen output is `constexpr` static storage; per-tag accessors allocate nothing (they delegate to `wire::MessageView::get<Tag>()` which is allocation-free per `2b §4.3` Index mode). `Dictionary::field_ref(...)`, `required_fields(...)`, `field_valid_for(...)`, etc., are `noexcept` and consult a borrowed `table_view` — no allocation. `DialectOverlay::with_overlay(...)` allocates *once at session open* from the user-supplied PMR (per `[arch §5.2]`); the merge result is heap-allocated but lives on the session's long-lifetime arena. Hot-path discipline per `[const §VIII.5]` is preserved.
- **Exceptions.** Hot path is exception-free per `[arch §5.3]`. `XmlLoader::load(...)` and `XmlLoader::load_overlay(...)` may throw `dict::xml_parse_error` or `dict::unknown_version_error` — these are construction-time failures, not hot-path errors, and are the same ergonomic carve-out 2a/2b take. `Dictionary::with_overlay(...)` returns `expected_t<Dictionary>` (no throw) because it *can* be invoked at session-open under tight error handling; `XmlLoader` throws because the alternative (`expected_t<Dictionary>` from a multi-step XML parse) is awkward.
- **Threading.** `Dictionary` is thread-safe-on-read after construction (frozen value). `table_view` is trivially copyable and value-typed; safe to share across threads as long as the underlying `Dictionary` is alive. `DialectOverlay` and `XmlLoader` are constructed/used in single-threaded contexts (engine init, session open) — no synchronisation guarantees on concurrent use. Typed-message flyweights are read-only and trivially copyable; safe to read concurrently from multiple threads as long as the underlying `wire::MessageView` is alive (which it is not, per `2b §6.4`'s lifetime contract — concurrency across the per-message-arena reset boundary requires `reify()` first).
- **`thread_local` is prohibited** per `[const §XV]` and `[arch §5.4]`. Codegen output never emits `thread_local`; `Dictionary`, `DialectOverlay`, `XmlLoader` never store `thread_local`; typed-message flyweights have no static state.

### 6.2 Latency Tier 1 ceilings

These are bench-harness regression bars (§9 seam #5); CI fails on >5% regression vs the previous tagged release. Targets are on Linux/Clang/x86_64, warm cache, default build.

| Operation | Workload | Ceiling | Notes |
|---|---|---|---|
| `Dictionary::field_ref` | merged dict, MsgType="D", common tag (e.g., 11) | ≤ 30 ns | binary search over per-MsgType FieldRef array |
| `Dictionary::required_fields` | MsgType="D" | ≤ 5 ns | returns a precomputed span |
| `Dictionary::field_valid_for` | MsgType="D", tag=11 | ≤ 25 ns | one binary search |
| `Dictionary::group_first_field` | NoLegs | ≤ 15 ns | flat lookup table over GroupRef |
| `Dictionary::length_pair_data_tag` | RawDataLength | ≤ 15 ns | flat lookup |
| Typed accessor (constexpr, e.g., `NewOrderSingle::cl_ord_id`) | 20-tag message, warm | ≤ 20 ns | inlines into one `OffsetTable::find` + traits dispatch |
| `Dictionary::with_overlay` | overlay with 50 fields, 5 messages | ≤ 1 ms | one-time merge cost; §1.2 |
| `MessageView::reify(mr)` (via typed `reify`) | 20-tag message, fresh PMR | ≤ 1 µs | byte-copy + offset-table rebuild; §1.2 |
| `MessageView::reify(mr)` | 200-tag Instrument-heavy | ≤ 10 µs | dominated by byte-copy; §1.2 |
| `XmlLoader::load` | FIX50SP2 standard XML, ~1700 fields | ≤ 100 ms | once per engine init; non-hot-path |

### 6.3 Multi-version coexistence

Per `[SYN §3.3 Q12]`:

- **One binary, multiple versions.** A translator/gateway TU may `#include <fixpp/v42/Messages.hpp>` and `#include <fixpp/v50sp2/Messages.hpp>` simultaneously; the two namespaces never collide. Compile cost rises as in §1.2; runtime cost is zero (each version's `constexpr` tables are independent ROM).
- **One `Dictionary` per `Session`.** A session is parameterised at open time by exactly one base `Dictionary` (one `version`); typed-message dispatch on inbound messages uses that version's namespace (the session FSM constructs `fixpp::vXX::*` typed views per its configured version). A multi-version process runs multiple `Session` instances, one per FIX version per counterparty.
- **FIXT.1.1 transport, FIX 5.0 SP2 application.** Per `[FIXT §5]`'s `DefaultApplVerID` and `[FIXT §5.3]`'s `ApplVerID(1128)` per-message: the session-layer messages (`Logon`, `Heartbeat`, `Logout`, `TestRequest`, `ResendRequest`, `SequenceReset`, `Reject`) use `fixpp::vt11::*`; the application-layer messages on the same session use `fixpp::v50sp2::*`. The session's `Dictionary` for FIXT.1.1 carries both vocabularies (the FIXT session-layer plus the configured application version); the typed dispatch picks `v50sp2` for application MsgTypes and `vt11` for session MsgTypes by inspecting `MsgType`. This is a *session-FSM* concern (Phase 4 / `2d`-adjacent); 2c's commitment is that the per-version `Dictionary` is constructible and the typed namespaces don't collide.
- **No implicit cross-version conversion.** `fixpp::v42::NewOrderSingle` and `fixpp::v50sp2::NewOrderSingle` are different types. A translator that converts FIX 4.2 → FIX 5.0 SP2 builds an explicit converter (read fields from one, write fields into a `Writer` in the other version's namespace, then `commit()`). 2c does not generate cross-version converters; this is application-specific, and a candidate for a v1.x utility.

### 6.4 Dialect-overlay additive merge contract

Per `[SYN §3.3 Q13]`:

- **Additive.** An overlay extends the base dictionary with new fields, new messages, new Length+Data pairs. It does *not* delete from the base.
- **Override-permitting (with policy).** An overlay may override a base field's `presence` rule for a specific MsgType (e.g., promote tag 109 `ClientID` from Optional to Required on a venue-specific NewOrderSingle); the conflict policy `OverlayWins | BaseWins | Reject` (§4.4) decides what happens on override collision. Default `OverlayWins`.
- **Merged once, frozen.** `Dictionary::with_overlay(...)` produces a new `Dictionary` with the merged tables; the original base is unchanged. The merged result is frozen for the session's lifetime per `[arch §5.6]`.
- **Lifetime relative to base.** The merged `Dictionary` borrows the base's `constexpr` tables (no copy of the standard FieldRef array — just additive entries from the overlay are heap-allocated). The merged `Dictionary` MUST NOT outlive the user-supplied PMR resource that backs the additive entries; if the base is `constexpr` (the typical case), the base outlives any merge.
- **Promotion semantics for D-007 / COM-011.** A "previously-unknown custom" tag (a tag that the base dictionary has no `FieldRef` for, but that the overlay declares) is promoted: after merge, `Dictionary::field_valid_for(msg_type, custom_tag)` returns `true`, the typed accessors codegen'd from the *base* version do not include it (codegen ran against the standard XML, not the overlay), and runtime access via `wire::MessageView::get<Tag>(custom_tag)` succeeds. The typed accessor surface for overlay-promoted tags is *not* generated at session open (no runtime codegen); users wanting typed access either (a) use the runtime tag-keyed accessor, (b) build their own typed wrapper, or (c) regenerate the codegen against the venue XML and rebuild — the trade `[SYN §3.3 Q13]` explicitly accepts to avoid runtime codegen.
- **Conflict-policy granularity.** The conflict policy applies per-field, not per-overlay-as-a-whole. An overlay with five overrides under `OverlayWins` policy applies all five; a single conflicting override under `Reject` rejects the entire merge with `error::dict_overlay_conflict`. `BaseWins` silently drops the overlay's overrides while keeping its additions; useful for advisory overlays (telemetry-only dialect awareness).

### 6.5 Lifetime contract on typed-message flyweights

Inherited from `2b §6.4` and `[arch §5.5]`:

- A typed-message flyweight (e.g., `fixpp::v50sp2::NewOrderSingle`) holds a `wire::MessageView<Index>` by reference; the view aliases the originating frame buffer; the buffer's lifetime is the per-message arena's slot, reset by the session FSM after `fromApp` returns.
- Capturing a typed-message flyweight past `fromApp` return is undefined in release; debug builds trap via `2b §6.4`'s generation-counter mechanism (the typed accessor calls flow through `wire::MessageView::get<Tag>()` which calls `View::check_alive()`).
- The supported escape hatch is `typed_msg.reify(mr) → owning_message_t<MsgClass>` (§4.8); deep-copies the bytes plus offset table into caller-owned PMR storage, returns a value-typed owning copy that can be moved across thread/strand boundaries.
- `[[clang::lifetimebound]]` is on every view-returning accessor; codegen emits the attribute unconditionally; per `[arch §5.5]`. GCC honours it on parameters; MSVC ignores (accepted gap per `[const §IX.4]`).

### 6.6 `owning_message_t<>` reify contract

The cross-strand view-escape contract referenced by `2b §6.6`:

1. **Entry point: `typed_msg.reify(mr)`** on any typed-message flyweight (e.g., `NewOrderSingle::reify(mr)`). Equivalent C-ABI: `fixpp_msg_reify(msg, mr)` per **2i**.
2. **Allocation discipline.** Three PMR allocations from `mr`: bytes (one), offset-table entry array (one), hash overlay (one). Bounded by `frame_view::bytes().size()` plus the `2b §1.2` offset-table caps. No allocation outside `mr`.
3. **Version tagging.** The returned `owning_message_t<MsgClass>` carries `version_v` as a `constexpr` member; `which()` returns it. C-ABI's `fixpp_owning_msg_t` carries the runtime tag.
4. **Lifetime.** Bounded by `mr`'s lifetime. The `owning_message_t<>` is move-only; copy is deleted; move transfers all three PMR allocations.
5. **Re-validate on the receiver side?** The reified copy preserves the original bytes verbatim; if the original was `wire::Validator`-passed before reify, it remains valid (no fields changed). If a receiver wants to re-validate (e.g., suspicious of in-flight corruption between strands — extremely unlikely in-process, but a paranoid pattern), `wire::Validator::validate(reified.view(), scratch_mr)` is callable again; the receiver supplies a fresh scratch `mr`.
6. **Errors.** `reify()` returns `expected_t<owning_message_t<MsgClass>>`; the only failure mode is `mr` PMR-allocation-failure (e.g., out-of-arena) which surfaces as `error::dict_reify_oom` (§6.7). The byte-copy and offset-table-rebuild themselves cannot fail on a validated source.
7. **Cross-strand safety.** Once constructed, the `owning_message_t<>` is safe to `std::move` across thread/strand boundaries. The receiving thread's accessors return views aliasing the owning copy's bytes; `[[clang::lifetimebound]]` on `*this` chains the lifetime warning correctly.

### 6.7 Errors introduced by this design

Per `2b §6.7`'s pattern, 2c collects every new `fixpp::core::error` variant it adds so **2i** has a stable C-ABI mapping target and `[const §X.4]` forwards-compat applies against a known list.

| `fixpp::core::error` variant | Source section | Remediation class |
|---|---|---|
| `dict_unknown_version` | §4.5 — `XmlLoader::load` rejects an XML with a `BeginString` outside the supported set | configuration error — fix XML or upgrade to the v1.x supporting that version |
| `dict_xml_parse_failed` | §4.5 — `XmlLoader::load*` rejects malformed XML | configuration error — fix XML |
| `dict_xml_schema_violation` | §4.5 — XML parses but violates QuickFIX schema (missing `<fields>`, unknown attribute, etc.) | configuration error — fix XML or schema |
| `dict_overlay_conflict` | §4.4 / §6.4 — `with_overlay` rejects a collision under `Reject` policy | configuration error — reconcile overlay vs base |
| `dict_overlay_too_large` | §1.2 / §4.4 — overlay has > N entries (DoS guard against pathological overlays) | configuration error — split or sanity-check overlay |
| `dict_field_not_in_version` | §4.4 / §6.4 — overlay references a base FIX version that does not declare the field's enclosing component | configuration error — pick correct base version |
| `dict_msg_type_not_in_version` | §4.4 / §6.4 — overlay references a MsgType not in the base version | configuration error — pick correct base version or include the message in the overlay |
| `dict_length_pair_collision` | §4.4 — overlay's Length+Data pair conflicts with a base pair | configuration error — fix overlay |
| `dict_reify_oom` | §6.6 — `reify(mr)` PMR allocation failed | runtime error — caller arena exhausted; expand mr |
| `dict_table_view_stale` | §4.6 — `table_view` accessed after underlying `Dictionary` destroyed (debug-build only; release UB) | bug — fix lifetime |

(10 variants; 2i confirms the C-ABI mapping. Following 2a/2b's coalescing pattern: configuration errors → `FIXPP_ERR_DICT_CONFIG`; capacity → `FIXPP_ERR_DICT_LIMIT_EXCEEDED`; runtime allocation → reuses `FIXPP_ERR_OOM`. Final coalescing is 2i's call.)

## 7. Integration with adjacent modules

### 7.1 Wire (`[arch §4.3]`, owner **2b**)

Three integration points:

1. **Typed-message classes consume `wire::MessageView<Index>`.** Every generated typed message in `fixpp::vXX::*` is constructed from a `wire::MessageView<Index>`; the per-tag accessors delegate to `MessageView::get<Tag>()` and `group<NoTag, GroupT>()`. 2c does not re-implement parse/serialize; it generates *typed shells* over 2b's primitives. Per `2b §7.2`.
2. **`dict::table_view` flows into `wire::dictionary_driven_validator`.** 2b's `dictionary_driven_validator` (per `2b §4.6`) holds a `dict::table_view` by value. 2c provides `Dictionary::as_table_view()` (§4.3); the session FSM passes the value at validator construction. Since `table_view` is value-typed and trivially copyable (§4.6), there is no virtual `wire/` → `dict/` runtime edge — `dictionary_driven_validator` consults the table by value-typed call without indirection through a virtual dictionary interface.
3. **Length+Data static table extension.** 2b's `field_iterator` (`2b §4.3`) uses a static `constexpr` table of FIX-standard Length+Data tag pairs to skip SOH bytes inside `data`-typed fields without consulting the runtime dictionary (Iter mode is dict-free by design). 2c **owns the source of truth for that table**: the per-version `Fields.hpp` includes a `constexpr std::array<length_pair, N>` derived from the standard XML's `<field type="LENGTH" />` declarations and their paired `<field type="DATA" />` neighbours. 2b's `field_iterator` `#include`s the FIX 5.0 SP2 version's table (the most permissive, since FIX 5.0 SP2 is a superset of 4.4/4.2 for Length+Data pairs by `[FIX50SP2 §3.3]`).
   - **Dialect-overlay extension of the static table.** Per `[SYN §3.3 Q13]`, dialect overlays may add new Length+Data pairs (rare; mostly venue-specific binary BLOBs). The static table cannot be extended at runtime *for Iter-mode consumers*: Iter mode is dict-free by construction, so a dialect-promoted pair has no runtime path to inform `field_iterator`. **v0.1 decision:** dialect-promoted Length+Data pairs require Index mode (which has the runtime `Dictionary` and consults `Dictionary::length_pair_data_tag(...)`). Iter mode (tap, async logger) is documented as standard-FIX-only; this is acceptable for v1.0 because Iter consumers are streaming/observability components that don't need typed access to dialect-private BLOB fields. Tracked in §10 Q2 alongside `2b §10 Q5`.

### 7.2 Session (2d, 2e — to be drafted)

- **Per-session `Dictionary`.** `SessionConfig` carries one `dict::Dictionary` value (per `[arch §4.4]`); the session's `Parser`/`Writer`/`Validator` consume it through `Dictionary::as_table_view()`.
- **Dialect-overlay swap is its own API.** Per `[arch §5.6]`, mid-session dictionary swap is rejected. The supported pattern: `Listener::open(SessionConfig)` includes the overlay at construction; mid-session change closes-and-reopens. 2d's threading contract confirms the swap point.
- **MessageStore consumes raw frames, not typed messages.** Per `2a §7.1` v0.3 and `2b §7.4`, MessageStore journals raw bytes; 2c's typed messages are not the persistence shape. Typed messages are constructed *on the fly* in `fromApp` callbacks; `reify()` is the supported "carry past the strand" path.

### 7.3 C ABI (`[arch §4.10]`, owner **2i**)

- `fixpp_msg_t` carries a runtime version tag derived from `dict::version` (§5).
- `fixpp_dict_t`, `fixpp_dialect_overlay_t`, `fixpp_owning_msg_t` are 2i-owned opaque handles wrapping the C++ types defined here.
- C-ABI accessor families wrap the typed-message accessor surface; 2i's choice between named (`fixpp_neworder_single_cl_ord_id`) and tag-keyed (`fixpp_msg_field_string(msg, 11, ...)`) shapes is independent of 2c.

### 7.4 Service (2j, control plane)

The control plane may **swap a session's dictionary at session-create time only** per `[arch §5.6]` and §6.3. Concretely: the gRPC `OpenSession` request (per `[arch §8.1]`) carries the FIX version and an optional `DialectOverlay` reference (a path or in-line XML); the service resolves them against engine-loaded `Dictionary` instances and constructs the per-session `Dictionary` before the session is exposed. The control plane does not expose a "change dictionary on a live session" RPC.

### 7.5 SWIG / Python (`[arch §4.12]`, owner **2m**)

- **Typed-message exposure shape.** Per-message classes in `fixpp::vXX::*` are SWIG-wrapped one-to-one. The Python side sees `fixpp.v50sp2.NewOrderSingle`, `fixpp.v42.NewOrderSingle`, etc., as distinct classes under distinct submodules. The version namespace maps to a Python submodule.
- **Version tag.** `fixpp.v50sp2.NewOrderSingle.version` returns `dict.version.v50sp2` (Python enum); cross-version polymorphism in Python uses an `isinstance` dispatch on the typed class, with the version tag accessible for tooling.
- **Owning-message `reify`.** SWIG wraps the C-ABI `fixpp_msg_reify` family per **2i**; the Python wrapper hides the PMR resource handling (a Python-allocated buffer pool is the default `mr`).
- **Dialect overlay in Python.** SWIG wraps `XmlLoader::load_overlay` as `fixpp.dict.load_overlay(path)` returning a Python-side `DialectOverlay` object that flows into `fixpp.session.SessionConfig`.

## 8. PMR — recap

Three storage classes for 2c-owned data:

| Storage | Lifetime | Holds | Reset by |
|---|---|---|---|
| Per-version `constexpr` tables (`Fields.hpp`, `Validator.hpp`) | static (program lifetime) | per-version `FieldRef`/`ComponentRef`/`GroupRef` arrays, conditional-rule tables, Length+Data pair tables, required-field sets | never (static storage) |
| `Dictionary` overlay-merge storage | session lifetime | merged-overlay `FieldRef` overrides, additive entries, message additions, Length+Data pair additions | session destruction |
| `owning_message_t<>` storage | caller `mr` lifetime | one `pmr::vector<std::byte>` (the deep-copied frame), one offset-table `entry[]`, one hash overlay | caller resets `mr` |

- **Codegen output is `constexpr`** → static storage, zero allocation, no `new`/`delete` ever (`[const §VIII.5]`).
- **`Dictionary` ownership.** Constructed by `XmlLoader::load(...)` (allocates from user-supplied `mr`), composed with `with_overlay(...)` (allocates additional from the same `mr` or a new one). Held by value in `SessionConfig`. The user supplies the `mr` (typically the session's long-lifetime arena per `[arch §5.2]`); the user owns when to destroy.
- **`DialectOverlay` storage.** PMR-allocated `pmr::vector` members; lifetime = `DialectOverlay` value's lifetime (typically constructed at session open, consumed by `Dictionary::with_overlay(...)`, then dropped if the merged dictionary is the only retained handle).
- **`owning_message_t<>` storage.** Caller-supplied PMR per `reify(mr)`; the value carries `mr` and frees on move-end.
- **Banned: `thread_local`.** Per `[const §XV]` and `[arch §5.4]`. Codegen never emits `thread_local`; runtime types never allocate via `thread_local`. Trace context (per `[const §XIII.3]`) is strand-stored, not `thread_local`; 2c is not involved.

## 9. Test seams

Per `[arch §10]` requirement (4) and `[const §VII]`: every design doc ends with the test seams it exposes.

1. **Conformance corpus — every supported version × every owned message round-trips.** `tests/codegen/conformance/` holds (a) public FIX corpora (QuickFIX `examples/*.dat`, public exchange specifications' sample messages, ICAP regression set) for FIX 4.2, 4.4, 5.0 SP2, FIXT.1.1 session messages; (b) parameterised GTests that parse each corpus message into the appropriate typed class, exercise every per-tag accessor, reify, and re-serialize. Catches codegen-template regressions, accessor mismatches, version-namespace collisions.
2. **Compile-time cost regression.** Bench harness measures the wall-clock to compile a TU including (a) one version's `Messages.hpp`, (b) all four versions'. CI fails if median exceeds the §1.2 ceilings (single-version ≤ 3 s; all-version ≤ 8 s). Also tracks per-header preprocessor expansion size to spot template-bloat regressions before they impact the wall-clock metric.
3. **Per-tag accessor latency regression.** Google Benchmark on `NewOrderSingle::cl_ord_id`, `::symbol`, `::price` over a warm-cache 20-tag message; CI fails on >5% regression vs baseline. Target: ≤ 20 ns per accessor (§6.2).
4. **Dialect-overlay merge cost regression.** Bench `Dictionary::with_overlay(...)` with an overlay of 50 fields + 5 messages; target ≤ 1 ms (§1.2). Larger overlay tests (500 fields) verify scaling is linear, not quadratic, to catch a hash-collision regression.
5. **Codegen lookup latency regression.** Bench `Dictionary::field_ref`, `field_valid_for`, `required_fields`, `group_first_field`, `length_pair_data_tag` on the merged dictionary; CI fails on >5% regression. Targets per §6.2.
6. **`owning_message_t<>` reify latency regression.** Bench `reify(mr)` on 20-tag and 200-tag messages; targets ≤ 1 µs and ≤ 10 µs (§1.2). Also verifies the resulting `owning_message_t<>` is move-safe across `std::thread` boundaries (a small smoke test that posts the value to a `std::thread` and reads back).
7. **Allocation guard (Linux).** `tools/check_alloc.py` runs the typed-accessor read loop and the reify/move/access loop under the `mallocnesia` interceptor; any allocation between the typed-message constructor and the typed accessor reads (the read path) fails CI; the reify path is allowed three allocations and no more (`[const §VIII.5]` plus §1.2 budget). Same Linux-only caveat as `2a §9 seam #6`.
8. **Fuzzer (libFuzzer) — XmlLoader malformed input.** `tests/fuzz/fuzz_dict_xml_loader.cpp` feeds arbitrary bytes to `XmlLoader::load_from_string`; targets ASan + UBSan invariants. Required by `[const §IX.4]`.
9. **Fuzzer (libFuzzer) — dialect-overlay merge edge cases.** `tests/fuzz/fuzz_dict_overlay_merge.cpp` synthesizes random `DialectOverlay` values (random field additions, random conflict policies) and merges them against each base version; verifies no UB / crash, and that any returned `error::dict_overlay_*` matches the documented variant. Catches overlay-merge invariants the per-test corpus might miss.
10. **Multi-version coexistence test.** Single-process test opens two sessions: one FIXT.1.1 + FIX 5.0 SP2, one FIX 4.4. Sends a `NewOrderSingle` on each, dispatches to the correct typed handler, verifies no namespace bleed (e.g., a `fixpp::v44::NewOrderSingle` cannot be implicitly converted to `fixpp::v50sp2::NewOrderSingle`). Lives in `tests/integration/multi_version.cpp`.
11. **Dialect-overlay precedence test.** With base FIX 5.0 SP2 and an overlay that overrides `Side(54)`'s `presence` for `NewOrderSingle`, verifies (a) overlay wins under `OverlayWins`, (b) base wins under `BaseWins`, (c) `Reject` policy errors out at `with_overlay`, (d) removing the overlay restores base behaviour (close-and-reopen pattern). Verifies `Dictionary::was_dialect_promoted(custom_tag)` returns `true` for genuinely-new tags and `false` for tags the base already had.
12. **`owning_message_t<>` cross-strand handoff test.** Construct a `MessageView` on thread A, call `reify(mr)`, `std::move` the `owning_message_t<>` to thread B (via `std::async` or a `concurrent_queue`), read accessors on thread B after the per-message arena on thread A has been reset. Verifies (a) no UB, (b) values match what was read on thread A pre-reset, (c) the original `MessageView` (still on thread A) traps in debug if accessed post-reset (the documented footgun from `2b §6.4`).
13. **Static-assert tests for typed-message flyweight size/alignment.** A per-message-class `static_assert(sizeof(NewOrderSingle) == sizeof(wire::MessageView<Index> const&))` — the typed message holds exactly one reference, no other state. Catches accidental member additions in the codegen template that would inflate every typed-message instance.
14. **Length+Data static-table coverage test.** Verifies the codegen-emitted Length+Data pair table for each version is exhaustive against the standard XML (every `<field type="LENGTH" />` paired with the documented `<field type="DATA" />` neighbour); cross-checks against `[FIX50SP2 §3.3]`'s field-pair list. Catches a codegen regression that drops a pair (which would make `wire::field_iterator` skip SOH incorrectly inside a `data`-typed field).
15. **Drop-A-024 regression test.** Verifies that `fixpp::v44::*` and `fixpp::v50sp2::*` namespaces do not contain a typed class for the dropped A-024 catalogue row (per `[SYN §4.4]` — duplicate of A-018). Codegen rejects A-024 in the dictionary XML or the build's allow-list.

## 10. Open questions

Cross-doc handoffs and within-2c follow-ups:

| # | Question | Disposition | Owner |
|---|---|---|---|
| 1 | Codegen-output compile-time cost spike on the all-versions TU — is the §1.2 ≤ 8 s ceiling defensible on the engine-target hardware? If not, the post-v1.0 modules / PCH adoption (per `[SYN §3.3 Q11]`) reopens earlier than planned. | Spike during 2c implementation against representative TUs (a translator that includes all four versions); if the ceiling is hot, reopen `[SYN §3.3 Q11]` for a v1.x preview. | 2c (this doc, post-Gate-A spike) |
| 2 | Iter-mode dialect-overlay Length+Data pair extension — is "Iter mode is standard-FIX-only" acceptable for v1.0, or does a real dialect overlay need to inject its own static table into a per-TU `field_iterator` (e.g., codegen the dialect's table into a header users include alongside `fixpp/v50sp2/Messages.hpp`)? | Confirm at first dialect-overlay user pull; expected acceptance "yes, v1.0 acceptable" because Iter consumers (tap, logger) don't read BLOB fields. Extension path documented as `2b §10 Q5` and reopens here if a real consumer hits it. | 2b + 2c |
| 3 | Should `DialectOverlay` ever become a runtime virtual `DialectOverlayPlugin` interface? — For v1.0, value-typed is sufficient (§4.4). The trigger for revisiting is a real consumer asking for "plug in a regulator's rules engine that computes overlay rules at runtime"; the v1.0 surface accommodates the addition without breaking change. | DEFERRED to first user pull. Tracked here so future readers see the boundary. | 2c follow-up |
| 4 | Conditional-rule support in dialect overlays — v1.0 dialect additions cannot introduce new conditional rules (§4.4 `message_addition` comment); only the base dictionary's rules apply. Is that acceptable for v1.0, or do real dialects need conditional-rule overlay support? | DEFERRED to first dialect-overlay user pull; expected disposition: acceptable for v1.0 (most venue dialects layer simple field additions, not new conditional logic). Reopens here if a real dialect needs it. | 2c follow-up |
| 5 | Per-version header set placement — confirm `include/fixpp/v42/Messages.hpp`, `Fields.hpp`, `Validator.hpp` are stable identifiers for IDE / Doxygen tooling discovery; the build-tree placement (`build/<preset>/_codegen/include/fixpp/v42/`) is opaque to tooling that resolves through the `INTERFACE_INCLUDE_DIRECTORIES` of the `fixpp::dict` CMake target. | Verify at 2c implementation; emit a CMake module map (`fixpp::dict::v42`, `fixpp::dict::v44`, etc.) for fine-grained linkage control if needed. | 2c |
| 6 | A-024 codegen rejection mechanism — fail at codegen time (`fixpp-codegen` errors out if A-024 appears in the source XML) vs filter at build time (codegen skips the message and the build proceeds). v0.1 picks the first (loud failure) for safety; reconsider if the maintainer of the FIX dictionary XML pushes a refresh that includes A-024. | DEFERRED; v0.1 default = error out. | 2c |
| 7 | Confirm with **2e** that MessageStore's canonical model is raw frames (per `2a §7.1` v0.3 and `2b §7.4`); 2c's `owning_message_t<>` is the cross-strand handoff target, *not* the persistence shape. If 2e proposes a typed-payload variant for MessageStore (alongside the raw-frame default), 2c may need to expose `owning_message_t<>` serialise-back-to-bytes for the typed-payload path; v0.1 assumes that's not needed. | Confirm at **2e**. | 2c + 2e |
| 8 | Codegen-output `_reserved` byte semantics in `FieldRef` (§4.1) — match 2a's pattern (ignore on read in v1.0; future minor version may use under `FIXPP_DICT_FIELDREF_RESERVED_USED`)? Or omit the byte entirely and rely on 16-byte alignment as already-pinned via `static_assert`? v0.1 keeps the `_reserved` byte for parity with 2a and the same forward-compat story. | DECIDED match 2a. Reopens only if Codex/Opus push back at Gate A. | 2c |

## 11. Hand-off

After Gate A and user sign-off, **2c** unblocks:

- **2d** (application threading contract) — knows the typed-message dispatch shape (per-version namespaces, flyweight constructor signatures, `owning_message_t<>` cross-strand contract); per-session `Dictionary` is a `SessionConfig` field.
- **2e** (MessageStore async API) — knows that typed messages are flyweights over raw frames, that `owning_message_t<>` is the cross-strand handoff (not the persistence shape per `2a §7.1` / `2b §7.4`), and that any typed-payload-persistence variant needs a 2c-side serialise-back-to-bytes path (documented as §10 Q7).
- **2i** (C ABI message rep + error enum) — knows `fixpp_msg_t` carries a runtime version tag (§5), the `dict::version` enum maps to a C-ABI constant set, and the C-ABI accessor surface wraps the typed-message classes per the §5 commitments. New `error::dict_*` variants from §6.7 enter the C-ABI mapping.
- **2j** (control-plane interface) — knows that dictionary swap is session-create-time only (§6.3), the gRPC `OpenSession` carries version + optional overlay, no live-swap RPC.
- **2m** (SWIG / Python binding shape) — knows the per-version submodule shape (`fixpp.v42`, `fixpp.v50sp2`, …), the typed-message exposure shape, the version-tag accessor, the dialect-overlay binding (per §7.5).

**2c does not add new catalogue rows.** D-001..D-011, OSS-001, OSS-010 are already OFFICIAL; the per-message typed-class rows (A-001..A-034 minus A-024, M-/P-/C-/R-/N- families) are already OFFICIAL and already split between this doc (typed classes + metadata) and 2b (parse/serialize/validate). 2c's contribution to those rows is the *typed-class generation*; the rows themselves already exist.

`feature-catalogue.md` does **not** need a row added when 2c lands. (Distinct from 2d's `NFR-015` clock row, tracked in `[arch §11]` row 7.)

---

## Appendix A — Catalogue row coverage

For each owned row, this doc owns the *typed-class generation and metadata*; the row's parse/serialize is owned by **2b**, the field representation types (decimal) by **2a** and the rest by 2c's `dict::field_traits<...>` specializations selected at codegen time, and the C-ABI accessor signatures by **2i**.

| Row | Family | What 2c covers |
|---|---|---|
| **D-001** | Dictionary load | `XmlLoader::load` (§4.5). |
| **D-002** | Per-version standard dictionaries | `fixpp/v42/Fields.hpp`, `v44/`, `v50sp2/`, `vt11/` (§4.7). |
| **D-003** | Field metadata table | `dict::FieldRef` (§4.1) + per-version `Fields.hpp` (§4.7). |
| **D-004** | Component metadata | `dict::ComponentRef` (§4.2). |
| **D-005** | Group metadata | `dict::GroupRef` (§4.2). |
| **D-006** | Required-field sets | `Dictionary::required_fields` (§4.3); per-message tables in `Validator.hpp`. |
| **D-007** | Custom-tag handling | `XmlLoader::load_overlay` + `DialectOverlay::field_additions` (§4.4, §4.5); promotion semantics §6.4. |
| **D-008** | Standard-tag codegen | per-version generated typed-message classes (§4.7). |
| **D-009** | Runtime XML loader | `XmlLoader::load` (§4.5). |
| **D-010** | Multi-version coexistence | per-version namespaces (§4.7); `Dictionary::which()` (§4.3). |
| **D-011** | Dialect overlays | `DialectOverlay` value type (§4.4); `with_overlay` merge (§6.4). |
| **OSS-001** | QuickFIX-XML compatible loader | `XmlLoader::load` accepts QuickFIX XML schema (§4.5). |
| **OSS-010** | Header-only generated typed messages with `constexpr` field metadata | per-version `Messages.hpp` + `Fields.hpp` are header-only with `constexpr` tables (§4.1, §4.7). |
| **A-001..A-034 minus A-024** | Order-management | per-message generated typed classes under `fixpp::v42::*` / `v44::*` / `v50sp2::*`; FIX-Latest A-035..A-065 explicitly out of scope per `[const §XVIII.2]`. |
| **M-001..M-012** | Market data | per-message generated typed classes (e.g., `MarketDataIncrementalRefresh`, `MarketDataSnapshotFullRefresh`, `SecurityList`, `SecurityDefinition`, `SecurityStatus`, …). |
| **P-001..P-008** | Pre-trade / post-trade | per-message generated typed classes (e.g., `Quote`, `QuoteRequest`, `QuoteCancel`, `MassQuote`, `IndicationOfInterest`, `TradeCaptureReport`, `TradeCaptureReportAck`, `AllocationInstruction`). |
| **C-001..C-003** | Collateral / positions / account | per-message generated typed classes (e.g., `CollateralRequest`, `CollateralAssignment`, `PositionReport`, `PositionMaintenanceRequest`, `AccountSummaryReport`). |
| **R-001..R-005** | Reg / IOI / news | per-message generated typed classes (e.g., `News`, `Email`, `RegistrationInstructions`, `RegistrationInstructionsResponse`, `IndicationOfInterest`-reg flavour). |
| **N-001..N-003** | Network counterparty / user request | per-message generated typed classes (`NetworkCounterpartySystemStatusRequest`, `NetworkCounterpartySystemStatusResponse`, `UserRequest`, `UserResponse`, `UserNotification`). |

(Final per-tag mapping confirmed at codegen-tool implementation; the list above is upper-bound, not exhaustive on a per-tag basis.)

## Appendix B — Normative References

Per `[const §VI.5]`, every `/specify` artifact lists the exact coverage-index references that inform it. This doc binds to:

| Topic | Source | Where applied |
|---|---|---|
| Tag=Value SOH encoding (typed messages reuse 2b's primitives) | `[FIX-SL §3] Header / Body / Trailer`, `[FIX50SP2 §3] Encoding` | §4.7 typed-message accessors, §7.1 wire integration |
| FIX field data types (incl. PRICE / QTY / AMT family for FLOAT, integer types, string types, timestamp types) | `[FIX50SP2 §3.3] Field data types` | §4.1 `data_type` enum, §4.7 typed accessors, §7.1 Length+Data table source-of-truth |
| Conditional-Required field semantics | `[FIX50SP2 §3.4] Conditional fields` | §4.1.4 conditional-rule encoding via `condition_index_`, §6.4 dialect-overlay merge |
| Application-version handling (FIXT.1.1 + FIX 5.0 SP2) | `[FIXT §5] Application-version handling`, `[FIXT §5.1] DefaultApplVerID`, `[FIXT §5.3] ApplVerID per message` | §4.3 `dict::version` enum, §6.3 multi-version coexistence rules |
| Per-message tables for application messages — order management | `[FIX44 §...]`, `[FIX50SP2 §...]` per-message field grammars (catalogue **A-001..A-034**) | §4.7 typed classes; Appendix A |
| Per-message tables for market data | `[FIX44 §...]`, `[FIX50SP2 §...]` (catalogue **M-001..M-012**) | §4.7; Appendix A |
| Per-message tables for pre-trade / post-trade | `[FIX44 §...]`, `[FIX50SP2 §...]` (catalogue **P-001..P-008**) | §4.7; Appendix A |
| Per-message tables for collateral / positions / account | `[FIX44 §...]`, `[FIX50SP2 §...]` (catalogue **C-001..C-003**) | §4.7; Appendix A |
| Per-message tables for reg / IOI / news | `[FIX44 §...]`, `[FIX50SP2 §...]` (catalogue **R-001..R-005**) | §4.7; Appendix A |
| Per-message tables for network counterparty / user request | `[FIX44 §...]`, `[FIX50SP2 §...]` (catalogue **N-001..N-003**) | §4.7; Appendix A |
| Hot-path allocation discipline | `[const §VIII.5]` | §6.1, §6.2 (ceilings), §8 PMR recap |
| C-ABI surface (deferred to 2i; 2c-side commitments only) | `[const §X]`, `[arch §4.10]` | §5 |
| Plugin interface ≤5 pure-virtual cap | `[const §XIV.2]` | §4.4 `DialectOverlay` value-typed decision (no virtual pure-virtuals declared at all in v1.0); §10 Q3 future-virtual question |
| Banned patterns — eager codegen with no runtime path mandates hybrid | `[const §XV.13]` | §3 inheritance from `[const §XV.13]` motivates `XmlLoader` + `DialectOverlay` runtime path alongside codegen |
| Banned patterns — no `thread_local` on hot path | `[const §XV]`, `[arch §5.4]` | §6.1 codegen output never emits `thread_local` |
| Banned patterns — no synchronous logging on hot path | `[const §XV.5]`, `[const §XIII.2]` | §6.1 codegen output never emits sync-log calls |
| Post-v1 roadmap — FIX-Latest A-035..A-065 + SOFH/SBE/FAST/FIXP/JSON/GPB/MMT incremental releases out of scope | `[const §XVIII.2]` | §2 non-goals, §10 Q1 |
| Codegen output format — header-only `constexpr` arrays | `[SYN §3.3 Q11]` | §1 goal 2, §1.2 compile-cost ceilings, §10 Q1 follow-up |
| Multi-version coexistence — version-namespaced types | `[SYN §3.3 Q12]` | §1 goal 3, §4.7, §6.3, §5 (C-ABI version tag) |
| Dialect-extension layering — additive at runtime | `[SYN §3.3 Q13]` | §1 goal 4, §4.4 `DialectOverlay`, §6.4 merge contract, §7.1 Length+Data extension |
| Decimal extension point (typed accessors substitute `fixpp::decimal_t` at FLOAT) | `[2a §4.4]`, `[2a §7.2]` | §4.7 typed accessors |
| Wire surface (typed messages over `MessageView`; `dict::table_view` consumed by `wire::Validator`) | `[2b §4.3]`, `[2b §4.6]`, `[2b §7.2]` | §4.7, §7.1 |
| View-escape contract — `MessageView::reify(mr) → owning_message_t<>` | `[2b §6.6]` | §1 goal 6, §4.8, §6.6 |
| Architectural inheritance | `[arch §1]`, `[arch §2]`, `[arch §3]`, `[arch §4.2]`, `[arch §5.2]`, `[arch §5.3]`, `[arch §5.5]`, `[arch §5.6]`, `[arch §6]`, `[arch §7.3]`, `[arch §7.4]`, `[arch §9.1]`, `[arch §9.2]`, `[arch §10]` | §3 |
| Codex Gate A trigger (codegen layout — Appendix A trigger row) | `[const §XVII.1]`, `[const Appendix A]` row "Codegen layout" | this doc requires Gate A before `/tasks` |
| Normative-References completeness rule | `[const §VI.5]` | this Appendix B |

Engineering-judgment decisions (16-byte `FieldRef`, `_reserved` byte forward-compat, `DialectOverlay` value-typed default vs virtual interface, per-version compile-cost ceilings, `owning_message_t<>` per-message-class generation choice, conflict-policy default `OverlayWins`, A-024 hard-error-on-codegen) cite `[SYN §3.3 Q11–Q13]` and `[const §XIV.2]` inline at point of use and are intentionally omitted from this appendix.

## Appendix C — Convergence log

> To be populated after Codex Gate A.
