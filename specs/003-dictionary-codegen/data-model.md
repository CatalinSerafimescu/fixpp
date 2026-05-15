---
id: 003-dictionary-codegen
title: Data model — generated artifacts, reify bridge, registry shape
spec_kit_step: /plan Phase 1
last_updated: 2026-05-15
inherits_design: .specify/2c-codegen.md v1.3
---

# Phase 1 Data Model — 003-dictionary-codegen

Entities are the *generated artifacts* and the *runtime bridge values*, not new runtime metadata types (the `FieldRef`/`ComponentRef`/`GroupRef`/`Dictionary`/`version_profile` types were shipped by 002 and are consumed here verbatim). Every shape is inherited from `[2c §4.7]`/`[2c §4.8]`/`[2c §4.9]`; this document records the invariants, the error mapping, and the PMR allocation accounting `/tasks` and Gate A consume.

## Entity 1 — Generated typed message `fixpp::<vXX>::<Msg>` (`[2c §4.7]`)

- **Source:** emitted by `fixpp-codegen` into `_codegen/include/fixpp/<vXX>/Messages.hpp`, one `class` per standard message in `dictionaries/<VER>.xml` (filtered: A-014..A-034 and FIX-Latest excluded — AC-G9/AC-G10).
- **Shape:** flyweight holding exactly one `wire::MessageView<wire::access_mode::Index> const&`. Static members `msg_type_v` (`constexpr std::string_view`), `version_v` (`constexpr application_version`). One `explicit ... noexcept` view-binding ctor (no validation). Per-field `[[nodiscard]] inline ... noexcept` accessor returning `expected_t<T>` (string/int/char via `dict::field_traits<T>`; decimal via `decimal_t::from_chars`); view-returning accessors carry `[[clang::lifetimebound]]`. Repeating groups → `wire::group_view<Leg>`-returning accessor; nested `Leg` flyweight with the same discipline. `field_value(uint16_t)` forwarder on the message and every nested group struct. `view()` bridge accessor.
- **Invariants:**
  - **I-1.** `sizeof(<Msg>) == sizeof(wire::MessageView<Index> const*)` — exactly one reference, no other state (AC-G7; emitted `static_assert`; seam #18).
  - **I-2.** Per-tag accessors are `inline noexcept`, **not** `constexpr`; only `msg_type_v`/`version_v` are `constexpr` (AC-G11; N-P2-1 — `OffsetTable::find` is non-`constexpr`).
  - **I-3.** Every `expected_t<T>` method carries `[[nodiscard]]`; every view-returning method carries `[[clang::lifetimebound]]` (NFR-003-6; emitted unconditionally).
  - **I-4.** Capturing the flyweight past the originating view's lifetime is release-UB; debug-traps via the `[2b §6.4]` generation counter through the accessor path (AC-G8).
- **Lifetime:** inherits `[2b §6.4]` — must not outlive the view, which must not outlive the frame buffer. The supported cross-strand escape is Entity 4 (`owning_<Msg>` via `dict::reify_as`).

## Entity 2 — Generated `Fields.hpp` constexpr tables (`[2c §4.2]`)

- **Source:** `_codegen/include/fixpp/<vXX>/Fields.hpp` — `constexpr` arrays of the **002-shipped** `fixpp::dict::FieldRef`/`ComponentRef`/`GroupRef` (their `sizeof`/`alignof`/standard-layout invariants from `[2c §4.1]`/`[2c §4.2]` hold unchanged).
- **Invariants:**
  - **I-5.** Static storage; zero allocation; no `new`/`delete` ever; no `thread_local` (`[const §VIII.5]`/`[const §XV]`; NFR-003-5; AC-V1).
  - **I-6.** Arrays sorted for O(log N) `(MsgType, tag)` lookup, per the codegen per-version contiguous-slice layout (distinct from 002's runtime side-table layout) (AC-V2).
  - **I-7.** `_reserved` bytes zero on emit, ignored on read (matches 2a discipline; `[2c §10]` Q8; AC-V6).

## Entity 3 — Generated `Validator.hpp` + Length+Data pair table (`[2c §4.2]`)

- **Source:** `_codegen/include/fixpp/<vXX>/Validator.hpp` — per-message rule tables + the Length+Data pair table.
- **Scope:** this PR **emits + shape/exhaustiveness-tests** them against the source XML (AC-V3/AC-V4; seam #19). *Behavioral* validation (`wire::dictionary_driven_validator` rejecting a bad message) is out of scope (spec §5).
- **Invariant I-8:** the Length+Data pair table is **exhaustive** vs source XML — every `<field type="LENGTH">` paired with its documented `<field type="DATA">` neighbour; cross-checked vs `[FIX50SP2 §3.3]` (AC-V4; seam #19).

## Entity 4 — `owning_<Msg>` (`[2c §4.8]`, emitted into `Reify.hpp`)

- **Source:** `_codegen/include/fixpp/<vXX>/Reify.hpp`, one `owning_<Msg>` per typed message.
- **Shape:** move-only (copy deleted); custom `noexcept` move ctor/assign (**not** `= default`); owns `bytes_` (`pmr::vector<std::byte>`) + a rebuilt `OffsetTable` on the supplied `mr`; lazy `frame_cache_`/`view_cache_` (`std::optional`); same accessor surface as the flyweight; `which()` returns `version_v`.
- **Invariants:**
  - **I-9.** No reference members; `is_nothrow_move_constructible_v`; move ctor not trivial/defaulted; move resets *both* source and destination caches to `std::nullopt`; moved-to rebuilds `view()` against post-move `bytes_.data()` (AC-R4; D-11; seam #14).
  - **I-10.** Single-strand-only — lazy `view()` cache write is unsynchronized; concurrent reads on one instance are UB (N-P1-2; AC-T3). Thread-safe usage = reify-on-A → move → consume-on-B (AC-R5; seam #12).
- **PMR accounting (≤ 4 per `reify_as<Msg>`, `[2c §1.2]` N-P2-4):** (1) `bytes_` vector; (2) `OffsetTable` object; (3) `OffsetTable` entry array; (4) `OffsetTable` hash overlay (may fuse with #3). No allocation outside `mr` (AC-R7; seam #7/#16).

## Entity 5 — `owning_message_handle` (`[2c §4.8]`)

- **Shape:** move-only type-erased handle. `version()` → `resolved_message_version`; `msg_type()`/`view()`/`field_value(tag)` per `[2c §4.8]`; `as<Msg>()` → borrowed `owning_<Msg> const*` or `nullptr` on version/MsgType mismatch (AC-R6). No UB / no throw on mismatch.
- **Allocation:** the type-erased payload may add ≤ 1 allocation if heap-backed (small-buffer-optimised variant elides it) — implementation-defined per `[2c §1.2]` N-P2-4.

## Entity 6 — Reify free function templates (`<fixpp/dict/reify.hpp>`, `[2c §4.8]`)

- `dict::reify_as<Msg>(view, mr) noexcept -> expected_t<owning_message_t<Msg>>`
- `dict::reify(view, profile, mr) noexcept -> expected_t<owning_message_handle>`
- Free function templates in `namespace fixpp::dict`; **no method added to `wire::MessageView`** (AC-R1).

## Entity 7 — `dict::version_registry` (`<fixpp/dict/version_registry.hpp>`, `[2c §4.9]`)

- **Shape:** `class version_registry` with `[[nodiscard]] expected_t<Dictionary const*> get(application_version) const noexcept [[clang::lifetimebound]]`. Concrete value type, not a virtual interface (`[const §XIV.2]` N/A; D-14).
- **Scope:** **shape only** (AC-X1..X3). Ownership/construction deferred to 2d (`[2c §10]` Q10; spec §10 F3). In-test hand-built registry; no engine wiring.

## Entity 8 — Shared dispatch headers (`_dispatch/`, `[2c §4.8]`/`[2c §6.3]`)

- `reify_dispatch_fixt.hpp` — exactly the 7 FIXT admin MsgTypes (`0/1/2/3/4/5/A`).
- `reify_dispatch_application.hpp` — one case per (codegen `application_version`, MsgType) across v42/v44/v50sp2 (~470).
- **Invariant I-11:** the default arm is fail-loud — returns `dict_reify_unknown_msg_type`, never misdispatches (R3; AC-D5/AC-D7). Included once per dispatch-consuming TU (D-6; `[2c §7.6]` `fixpp::dict::dispatch`).

## Entity 9 — Vendored frozen wire contract (`<fixpp/wire/message_view_contract.hpp>`, R6 / D-2)

- **Shape:** `wire::MessageView<wire::access_mode::Index>` with `get<Tag>()`/`get(uint16_t)`/`group<NoTag,T>()`/`unknown_fields()`; `wire::field_view`; `wire::group_view<T>`; the `[2b §6.4]` debug generation-counter trap. Surface **frozen**, locked by `[2b §4.3]`/`[2b §4.7]`.
- **Invariant I-12:** the contract test `static_assert`s the exact member signatures + the I-1 `sizeof` invariant; 2b later replaces the body against this same surface; signature drift fails the contract test at compile time (D-2; flagged for Gate A).

## Error mapping (spec §4.3–§4.5; D-10)

| Condition | Surface | Variant |
|---|---|---|
| `reify_as` view MsgType ≠ `Msg::msg_type_v` | `expected_t` unexpected | `dict_reify_msg_type_mismatch` (AC-R8) |
| Resolved (version, MsgType) has no owner | `expected_t` unexpected | `dict_reify_unknown_msg_type` (AC-D5/D7) |
| PMR allocation failure in any reify path | `expected_t` unexpected (trapped via `[2a §4.2]` `trap_throw`) | `dict_reify_oom` (AC-R7) |
| FIXT default_appl == Unknown + no `ApplVerID(1128)` | `expected_t` unexpected | `dict_unresolved_application_version` (AC-D6) |
| Non-empty `ApplVerID` that does not parse | `expected_t` unexpected | `dict_unknown_appl_ver_id` (AC-D7) |
| `version_registry::get` — no Dictionary for a resolvable version | `expected_t` unexpected | `dict_no_dictionary_for_application_version` (AC-X2) |

`dict_reify_version_mismatch` is **not** a failure mode of `reify_as` (dropped per RC#1; AC-R8). All variants append to `fixpp::core::error` at unused slots ≥ 23, non-renumbering (D-10).

## State transitions

- **Typed flyweight:** stateless over a borrowed view; no transitions. Lifetime tied to the view (I-4).
- **`owning_<Msg>`:** `constructed (caches empty) → first view()/accessor (caches populated) → moved (both sides' caches reset to nullopt) → moved-to first access (rebuilt against post-move bytes_)`. Concurrent reads on one instance: UB (I-10).
- **`dict::reify` dispatch:** `peek MsgType → [FIXT-admin hit → vt11 owner] | [miss → read ApplVerID → resolve_application_version → app owner] | [unresolved → dict_unresolved_application_version] | [no owner → dict_reify_unknown_msg_type]` (D-6/D-7).

## PMR allocation accounting (closes NFR-003-3/4; seam #7/#16)

- **Typed-accessor read path:** **0 allocations** — every accessor delegates to `wire::MessageView::get<Tag>()`, allocation-free Index mode (NFR-003-4; AC-T3; verified under `mallocnesia`, Linux).
- **`reify_as<Msg>`:** ≤ 4 PMR allocations (Entity 4 itemisation); none outside `mr` (NFR-003-3; AC-R7).
- **`reify` (runtime-dispatch):** `reify_as` budget + ≤ 1 for the type-erased handle if heap-backed (Entity 5).
- **Generated `constexpr` tables:** static storage, **0** runtime allocations ever (NFR-003-5; Entity 2 I-5).
- **OOM injection (seam #16):** a bounded PMR failing after N bytes injected into `reify_as` / `reify` / `owning_<Msg>::from_view` → `dict_reify_oom`; none terminate (AC-R7).
