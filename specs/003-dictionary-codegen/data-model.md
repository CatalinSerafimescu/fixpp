---
id: 003-dictionary-codegen
title: Data model — generated artifacts, reify bridge, registry shape
spec_kit_step: /plan Phase 1
last_updated: 2026-05-15
inherits_design: .specify/2c-codegen.md v1.4 (RC#2 [const §XX] amendment, commit 41dd8c1)
replan_applied: 2026-05-15 — RC#1/RC#2/RC#3 resolved in-bundle (see plan.md "Re-/plan (RC resolution)")
---

# Phase 1 Data Model — 003-dictionary-codegen

Entities are the *generated artifacts* and the *runtime bridge values*. The `FieldRef`/`ComponentRef`/`GroupRef`/`Dictionary` types and the `session_version`/`application_version` enums were shipped by 002 and are consumed here verbatim. **RC#1 RESOLVED (re-`/plan` 2026-05-15; Gate A round 1 Codex P1-1 / Opus root cause #1):** `version_profile` (struct), `resolved_message_version`, `dict::resolve_application_version` (free fn), and `<fixpp/dict/field_traits.hpp>` (`field_traits<T>`/`decode_field<T>`) were **NOT** shipped by 002 — 002 deferred the struct + free function (`specs/002-dictionary-xml-loader/contracts/version_profile.hpp:59-66`; on-disk `include/fixpp/dict/version_profile.hpp` carries the two enums only) and never owned `field_traits.hpp` (absent on disk). They are now **003-owned**, materialised in-bundle: `contracts/version_profile.hpp` (additive edit to the 002 enums-only file — new Entity 10) and the NET-NEW `contracts/field_traits.hpp` → `include/fixpp/dict/field_traits.hpp` (new Entity 11), with their own ACs, error-taxonomy, the wire `ApplVerID(1128)`→C++ `application_version` enum-mapping table (2c §4.3:486-501), and seam→file bindings (plan.md Test-seam map). **RC#2 RESOLVED:** the decimal route is re-derived from corrected `2c-codegen.md` v1.4 (PMR-mandatory `decimal_t::parse(span, mr)`; `[const §XX]` amendment, commit 41dd8c1). **RC#3 RESOLVED:** the dict↔wire bridge edge is covered by the `arch §2.4` v0.2→v0.3 carve-out amendment (no module cycle). Every other shape is inherited from `[2c §4.7]`/`[2c §4.8]`/`[2c §4.9]`; this document records the invariants, the error mapping, and the PMR allocation accounting `/tasks` and Gate A consume.

## Entity 1 — Generated typed message `fixpp::<vXX>::<Msg>` (`[2c §4.7]`)

- **Source:** emitted by `fixpp-codegen` into `_codegen/include/fixpp/<vXX>/Messages.hpp`, one `class` per standard message in `dictionaries/<VER>.xml` (filtered: A-014..A-034 and FIX-Latest excluded — AC-G9/AC-G10).
- **Shape:** flyweight holding exactly one `wire::MessageView<wire::access_mode::Index> const&`. Static members `msg_type_v` (`constexpr std::string_view`), `version_v` (`constexpr application_version`). One `explicit ... noexcept` view-binding ctor (no validation). Per-field `[[nodiscard]] inline ... noexcept` accessor returning `expected_t<T>` (string/int/char via `dict::field_traits<T>` + `decode_field<T>`, `[2c §4.1.3]` / Entity 11); the **decimal** accessor is the `[2c §4.7]` decimal route — **not** a `field_traits` specialisation (`[2c §4.1.3]` `.specify/2c-codegen.md:269-277` excludes `decimal_t`). View-returning accessors carry `[[clang::lifetimebound]]`. Repeating groups → `wire::group_view<Leg>`-returning accessor; nested `Leg` flyweight with the same discipline. `field_value(uint16_t)` forwarder on the message and every nested group struct. `view()` bridge accessor.

  > **Decimal route — RC#2 RESOLVED, re-derived from `2c-codegen.md` v1.4** (`[const §XX]` amendment, commit 41dd8c1; `.specify/2c-codegen.md:263,313,1051,1153`). v1.3's `decimal_t::from_chars(fv->bytes())` was a phantom symbol (no-`mr` member named `from_chars` on `decimal_t`, absent on merged 001/2a — only PMR-mandatory `decimal_traits<T>::from_chars(span,mr)` / `decimal_t::parse(span,mr)` exist; 2a's Gate A removed the no-`mr` form). **Corrected v1.4 contract:** the generated decimal accessor takes an **explicit `std::pmr::memory_resource* mr` parameter** and calls `fixpp::decimal_t::parse(fv->bytes(), mr)` (`[2a §4.3]`). The flyweight still holds **no** `memory_resource*` — I-1 (`sizeof == one pointer`, AC-G7) is **preserved**; `mr` is caller-threaded into the accessor *signature*, not stored. Zero-alloc for the default `pod_decimal` trait (it ignores `mr`); an allocating substituted `FIXPP_DECIMAL_T` (e.g. `cpp_dec_float`, supported per `[2a §4.4]`) may draw from `mr` per call (AC-G4a). Decimal latency is the separate ≤ 75 ns row (`[2c §6.2]`), not the ≤ 20 ns string/int/char row. AC-G4 / AC-G4a / NFR-003-4 (decimal arm) are **no longer blocked** — they are derived from this corrected contract. See `contracts/generated_message.hpp` `price(mr)`.
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
- **`owning_message_t<Msg>` definition (Gate A round 1, Opus N-P2-1 — previously used but undefined):** `template<class Msg> using owning_message_t = typename Msg::owning_type;` pinned in `contracts/reify.hpp`, so `owning_message_t<v44::NewOrderSingle> ≡ v44::owning_NewOrderSingle` (each `Reify.hpp` emits `owning_<Msg>` and exposes it as `Msg::owning_type`). This is the name-mangling/ADL surface every downstream consumer binds to — it is contract-pinned, not deferred (`[2c §4.8]`; AC-R1).
- **Dependency note (Codex P1-1 / Opus RC#1 — RESOLVED re-`/plan` 2026-05-15):** `version_profile` (the `reify` resolution input) + `resolve_application_version` are 003-owned (Entity 10), materialised in `contracts/version_profile.hpp` as an additive edit to the 002 enums-only file. No longer a blocking dependency.

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

## Entity 10 — `version_profile` / `resolved_message_version` / `dict::resolve_application_version` (`[2c §4.3]`, 003-OWNED — RC#1)

- **Source:** `contracts/version_profile.hpp` → an **additive edit** to the 002-shipped `include/fixpp/dict/version_profile.hpp` (which carries only the `session_version`/`application_version` enums; 002 deferred the rest — `specs/002-dictionary-xml-loader/contracts/version_profile.hpp:59-66`). 003 appends the two structs + the free-function declaration **below** the unchanged 002 enums (same additive discipline as `core/error.hpp`, D-10). 003 does **not** redeclare/renumber the enums.
- **Shapes (verbatim `[2c §4.3:408-481]`):**
  - `struct version_profile { session_version session; application_version default_appl; bool has_per_message_override; std::uint8_t _reserved; };` — `sizeof == 4`; `is_trivially_copyable`.
  - `struct resolved_message_version { enum class kind:uint8_t{session_admin,application}; kind k; session_version session; application_version application; std::uint8_t _reserved; };` — `sizeof == 4`; `alignof == 1`; `is_trivially_copyable`.
  - `[[nodiscard]] expected_t<application_version> resolve_application_version(version_profile, std::string_view) noexcept;` — free function (no `Dictionary` needed; the `Dictionary` member is a thin wrapper).
- **Resolution algorithm (`[2c §4.3:465-474]`):** non-empty `appl_ver_id` → parse via the **wire→C++ mapping table** (`[2c §4.3:486-501]`, reproduced in `contracts/version_profile.hpp`); parse failure → `dict_unknown_appl_ver_id`. Empty → `profile.default_appl`; if that is `Unknown` → `dict_unresolved_application_version` (per RC#1; the v1.0 sentinel fall-through to `dict_reify_unknown_msg_type` is **closed** — AC-D6).
- **Invariants:**
  - **I-13.** The parse uses the FIX wire enum values (`"2"`=v40 … `"9"`=v50sp2), **not** the C++ `application_version` internal indices (Unknown=0…v50sp2=8); reusing the C++ index mis-maps FIX 5.0 + SP variants (N2-P3-1; AC-VP4).
  - **I-14.** `version_profile`/`resolved_message_version` `_reserved` bytes zero on emit, ignored on read in v1.0 (matches the 2a/2c reserved discipline; AC-VP5).
- **ACs (NEW, spec §4.3):** AC-VP1 (additive-edit shape + the two `static_assert`s for each struct), AC-VP2 (`resolve_application_version` is a free function; the `Dictionary` member wraps it), AC-VP3 (full wire→C++ mapping table coverage `"2".."9"` + empty→default + `"0"`/`"1"`→`dict_unknown_appl_ver_id`), AC-VP4 (negative: C++ index NOT reused — a `static_assert`/test that wire `"7"`→`v50`, not the C++ index 7=`v50sp1`), AC-VP5 (`_reserved` discipline).

## Entity 11 — `dict::field_traits<T>` / `dict::decode_field<T>` (`[2c §4.1.3]`, 003-OWNED — RC#1)

- **Source:** `contracts/field_traits.hpp` → the **NET-NEW** `include/fixpp/dict/field_traits.hpp` (002 ships no such file — `specs/002-dictionary-xml-loader/spec.md:189`; absent on disk). The 2c-owned typed-decoding layer on the ≤ 20 ns hot path, consumed by `contracts/generated_message.hpp:10`, every string/int/char accessor, and `dict::reify` step 3 (`[2c §4.8]`).
- **Shape (verbatim `[2c §4.1.3:265-309]`):** primary `template<class T> struct field_traits;` + specialisations for `std::string_view`, `char`, `std::int32_t`, `std::int64_t`, `bool`, the timestamp/UTCTimestamp/date types, and the MultiCharValue/MultiStringValue split — each a single `static [[nodiscard]] from_field_view(wire::field_view const&) noexcept -> expected_t<T>`; plus `template<class T> [[nodiscard]] inline expected_t<T> decode_field(expected_t<wire::field_view>) noexcept`.
- **Invariants:**
  - **I-15.** `from_field_view` / `decode_field` are `noexcept` + **allocation-free** (NFR-003-4 string/int/char arm; verified under `mallocnesia`).
  - **I-16.** `decimal_t` is **not** a `field_traits` specialisation (`[2c §4.1.3:269-277]`); the decimal route is the Entity 1 / AC-G4 `[2c §4.7]` PMR accessor. Enforced negatively (AC-FT2: `field_traits<fixpp::decimal_t>` is not a defined specialisation).
- **ACs (NEW, spec §4.x):** AC-FT1 (file exists at `include/fixpp/dict/`, 003-NEW; declares primary + listed specialisations + `decode_field`), AC-FT2 (decimal exclusion, negative), AC-FT3 (`decode_field` forwards the `get<>()` error on `!fv`, else `field_traits<T>::from_field_view(*fv)`).

## Error mapping (spec §4.3–§4.5; D-10)

| Condition | Surface | Variant |
|---|---|---|
| `reify_as` view MsgType ≠ `Msg::msg_type_v` | `expected_t` unexpected | `dict_reify_msg_type_mismatch` (AC-R8) |
| Resolved (version, MsgType) has no owner | `expected_t` unexpected | `dict_reify_unknown_msg_type` (AC-D5/D7) |
| PMR allocation failure in any reify path | `expected_t` unexpected (trapped via `[2a §4.2]` `trap_throw`) | `dict_reify_oom` (AC-R7) |
| FIXT default_appl == Unknown + no `ApplVerID(1128)` | `expected_t` unexpected | `dict_unresolved_application_version` (AC-D6) |
| Non-empty `ApplVerID` that does not parse | `expected_t` unexpected | `dict_unknown_appl_ver_id` (AC-D7) |
| `version_registry::get` — no Dictionary for a resolvable version | `expected_t` unexpected | `dict_no_dictionary_for_application_version` (AC-X2) |

`dict_reify_version_mismatch` is **not** a failure mode of `reify_as` (dropped per RC#1; AC-R8).

**Slots LOCKED at re-`/plan` (RC#1 makes the surface owned, so D-10's "locked at /tasks" is now resolvable).** On-disk `include/fixpp/core/error.hpp` ends at slot 22 (`dict_xml_oom`; verified). The six 003-owned variants append non-renumbering, preserving every existing slot (`out_of_memory=1`, `decimal_*=10..13`, 002 `dict_xml_*=20..22`), `[const §X.4]`-forwards-compatible:

| Variant | Slot |
|---|---|
| `dict_reify_msg_type_mismatch` | 23 |
| `dict_reify_unknown_msg_type` | 24 |
| `dict_reify_oom` | 25 |
| `dict_unresolved_application_version` | 26 |
| `dict_unknown_appl_ver_id` | 27 |
| `dict_no_dictionary_for_application_version` | 28 |

**Cross-feature note (avoids an RC#1-class phantom-ownership repeat).** The "field absent" error returned by `wire::MessageView::get<1128>()` is **2b/wire-owned**, *not* a 003 slot. `dict::reify` maps "`get<1128>()` reported field-absent" → empty `appl_ver_id` (→ resolution step 2). 003 deliberately does **not** define a `dict_field_not_present` enum slot; the six above are the exact 003-owned set. The C-ABI mapping + `tools/abi_history/error_codes_v1.txt` audit-trail update remain 2i-owned under the same time-bounded waiver shape as 002 D-10 (no C-ABI surface here; auto-expires at the first C-ABI consumer commit).

## State transitions

- **Typed flyweight:** stateless over a borrowed view; no transitions. Lifetime tied to the view (I-4).
- **`owning_<Msg>`:** `constructed (caches empty) → first view()/accessor (caches populated) → moved (both sides' caches reset to nullopt) → moved-to first access (rebuilt against post-move bytes_)`. Concurrent reads on one instance: UB (I-10).
- **`dict::reify` dispatch:** `peek MsgType → [FIXT-admin hit → vt11 owner] | [miss → read ApplVerID → resolve_application_version → app owner] | [unresolved → dict_unresolved_application_version] | [no owner → dict_reify_unknown_msg_type]` (D-6/D-7).

## PMR allocation accounting (closes NFR-003-3/4; seam #7/#16)

- **Typed-accessor read path:** **string/int/char: 0 allocations** — every such accessor delegates to `wire::MessageView::get<Tag>()` (allocation-free Index mode) + an allocation-free `field_traits<T>` decode (NFR-003-4; AC-T3; verified under `mallocnesia`, Linux). **Decimal arm — RC#2 RESOLVED (v1.4):** the decimal accessor takes an explicit `std::pmr::memory_resource* mr` (caller-threaded; the flyweight still holds no arena — I-1 `sizeof == one pointer` preserved) and calls `decimal_t::parse(fv->bytes(), mr)` (`[2a §4.3]`). It is **0 allocations for the default `pod_decimal` trait** (it ignores `mr`); an allocating substituted `FIXPP_DECIMAL_T` (`cpp_dec_float`, supported per `[2a §4.4]`) may draw from the caller-supplied `mr` per call — never raw `new`/`delete`, `[const §VIII.5]`/`[const §XV.1]`-coherent (any heap traffic is the caller's arena, `[arch §5.2]`). NFR-003-4's decimal arm + AC-G4a are **derived from this resolved v1.4 contract** (no longer blocked). The arena-backed `owning_<Msg>` path (Entity 4) remains the cross-strand escape; it is not *required* for the borrowed decimal read (the caller passes `mr` directly).
- **`reify_as<Msg>`:** ≤ 4 PMR allocations (Entity 4 itemisation); none outside `mr` (NFR-003-3; AC-R7).
- **`reify` (runtime-dispatch):** `reify_as` budget + ≤ 1 for the type-erased handle if heap-backed (Entity 5).
- **Generated `constexpr` tables:** static storage, **0** runtime allocations ever (NFR-003-5; Entity 2 I-5).
- **OOM injection (seam #16):** a bounded PMR failing after N bytes injected into `reify_as` / `reify` / `owning_<Msg>::from_view` → `dict_reify_oom`; none terminate (AC-R7).
