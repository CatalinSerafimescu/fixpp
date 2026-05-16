# Design Doc 2c — Dictionary Codegen: Header Layout, Multi-Version Coexistence, Dialect Overlay Binding

> **Status:** Draft v1.4 — post-sign-off targeted amendment (RC#2: decimal decoding API coherence)
> **Date:** 2026-05-15
> **Owner role:** 2c codegen design lead. `fixpp::dict` (`include/fixpp/dict/`, `src/dictionary/`) + the codegen tool `tools/codegen/fixpp-codegen` + the per-version generated header packs `include/fixpp/v42/`, `v44/`, `v50sp2/`, `vt11/` (build-tree only).
> **Inherits:** `[arch §1]` (goals), `[arch §2]` (module layering — `dictionary` sits below `wire` and is consumed by `session`/`capi`), `[arch §3]` (namespaces — `fixpp::dict`, `fixpp::v42`, `fixpp::v44`, `fixpp::v50sp2`, `fixpp::vt11`), `[arch §4.2]` (full `dictionary` module surface), `[arch §5.2]` (allocator policy — PMR-aware, per-session resource), `[arch §5.3]` (error model — `expected_t<T>` on hot path, exceptions reserved for `XmlLoader` construction), `[arch §5.4]` (banned `thread_local`), `[arch §5.5]` (lifetime model — typed messages are flyweights), `[arch §5.6]` (configuration shape — `SessionConfig` frozen at session open), `[arch §6]` (plugin pattern — ≤5 pure-virtual cap if any interface is virtualized), `[arch §7.3]` (header surface), `[arch §7.4]` (CMake target layout), `[arch §9.1]` (public vs internal headers), `[arch §9.2]` (versioning), `[arch §10]` (handoff requirements — row 2c).
> **Cites:** `[const §I.1]` (v1.0 version surface), `[const §VI]` (spec coverage — every owned row maps to a coverage-index entry), `[const §VI.4]` (bidirectional traceability), `[const §VI.5]` (Normative References format), `[const §VII]` (testing — ≥10 seams), `[const §VIII.5]` (zero-allocation hot path), `[const §X]` (C ABI deferred to 2i; 2c-side commitments only), `[const §X.2]` (no C++ leakage through C ABI), `[const §XIV.2]` (≤5 pure-virtual cap on plugin interfaces), `[const §XV]` (banned patterns — no `thread_local`, no synchronous logging on the hot path; banned-pattern §XV.13 mandates the hybrid model 2c implements), `[const §XVII.1]` (Codex Gate A required), `[const §XVIII]` (post-v1 roadmap — FIX-Latest A-035..A-065 explicitly out of scope), `[const §XX]` (amendment procedure — Appendix D drafts amendment language for orchestrator application). Spec citations: `[FIX-SL §3]`, `[FIX42] FIX 4.2 application specification` (per `feature-catalogue.md` line 70 `Spec ref`), `[FIX44] FIX 4.4 application specification` (per the same `Spec ref` family), `[FIX50SP2 §3]`, `[FIX50SP2 §3.3]`, `[FIX50SP2 §3.4]`, `[FIXT §5]`, `[FIXT §5.1]`, `[FIXT §5.3]`. Per-message exhaustive references stay routed through the generated `_codegen/include/fixpp/<vXX>/NormativeReferences.md` per Appendix B (per C-P3-1). Sibling docs: `[2a §4.2]` (`fixpp::core::detail::trap_throw`), `[2a §4.4]` (`FIXPP_DECIMAL_T` alias rule), `[2a §6.5]` (decimal parse latency baseline), `[2a §7.2]` (decimal substitution at FLOAT accessors), `[2b §4.3]` (`Parser`, `MessageView`, `field_iterator`), `[2b §4.4]` (`OffsetTable`), `[2b §4.6]` (`dictionary_driven_validator`), `[2b §6.4]` (lifetime contract on flyweights), `[2b §6.6]` (three-arena pinning + view-escape contract), `[2b §6.7]` (errors-introduced sub-table), `[2b §7.2]` (wire surface that typed messages and `dict::table_view` consume), `[2b §7.4]` (MessageStore raw-frame contract). Synthesis: `[SYN §3.3 Q11]` (codegen output format = header-only `constexpr` arrays), `[SYN §3.3 Q12]` (multi-version coexistence = supported, version-namespaced types), `[SYN §3.3 Q13]` (dialect-extension layering = additive at runtime).
> **Catalogue rows owned (in part):** **Dictionary infrastructure (per §1.3 dispositions):** D-001 (FIX 4.2 dict — codegen + runtime XML), D-002 (FIX 4.4 dict — codegen + runtime XML), D-003 (FIX 5.0SP2 + FIXT.1.1 dict — codegen + runtime XML), D-004 (FIX 4.0/4.1 dict — runtime XML only), D-005 (FIX 4.3 dict — runtime XML only), D-006 (FIX 5.0/5.0SP1 dict — runtime XML only), D-007 (XML loader for all 9 versions), D-008 (codegen, four versions only — codegen-vs-runtime-XML disposition recorded as a coverage-index supplemental note per Appendix D §2), D-009 (custom dictionary extension via `DialectOverlay`), D-010 (component definition support), D-011 (FIX Latest / FIX Orchestra — **deferred post-v1.0** per `[const §XVIII.2]`). **OSS rows:** OSS-001 (QuickFIX-XML compatible loader), OSS-010 (header-only generated typed messages with `constexpr` field metadata). **Application-message generated typed-message classes + `constexpr` field metadata** (typed-message *classes only* — parse/serialize/validate behaviour is owned by **2b**): A-001..A-013 (order-management; codegen for v42/v44/v50sp2), A-014..A-034 (additional order-management variants; **runtime-XML-only in v1.0; codegen deferred to v1.x** per the proposed `[const §XVIII.7]` sub-clause and the `[arch §4.2]` row 2c amendment in Appendix D §3), M-001..M-012 (market data), P-001..P-008 (post-trade), C-001..C-003 (collateral / positions / account), R-001..R-005 (reg / IOI / news), N-001..N-003 (network counterparty / user request). FIX-Latest application-message rows A-035..A-065 are **post-v1.0** per `[const §XVIII]` and explicitly out of 2c scope.
> **Convergence log:** see end-of-doc Appendix C — the v1.3 → v1.4 entry (prepended) records the post-sign-off targeted amendment per `[const §XX]` that fixes RC#2 (decimal decoding API incoherent with the merged 2a/001 PMR-mandatory surface); it is **not** a Gate A round. The v1.2 → v1.3 Gate A round 3 convergence pass entry (Codex round-3 review 1 P1 / 1 P2 / 1 P3 and Opus round-3 adversarial review 1 new P1 / 0 new P2 / 0 new P3, 1 root cluster) is preserved below, see Appendix C. The v1.1 → v1.2 Gate A round 2 entry (2 Codex P1 + 1 Codex P2 + 2 Codex P3 + 0 new Opus P1 + 1 new Opus P2 + 2 new Opus P3) and the v1.0 → v1.1 Gate A round 1 entry (5 Codex + 12 Opus findings, 3 root causes) are preserved below; v0.1 archived as `2c-codegen.draft-r1.md` (full rewrite triggered by Opus closing recommendation; user signed off Root cause #4 constitutional decision before reset re-spawn). Appendix D drafts the proposed constitutional amendment + architecture/coverage-index supplemental notes that the orchestrator applies on sign-off.

---

## 1. Goals

1. Define the public surface of `fixpp::dict` — the runtime metadata types (`Dictionary`, `FieldRef`, `ComponentRef`, `GroupRef`, `XmlLoader`, `DialectOverlay`, `table_view`, `version_profile`, the `dict::reify` bridge) and the per-version generated typed-message namespaces (`fixpp::v42`, `fixpp::v44`, `fixpp::v50sp2`, `fixpp::vt11`) — so every consumer (`wire` per 2b, `session`, `capi` per 2i, `bindings/python` per 2m) compiles against a single locked interface.
2. Lock the **codegen output format** as **header-only `constexpr` arrays** per `[SYN §3.3 Q11]`: every per-version artifact (`Messages.hpp`, `Fields.hpp`, `Validator.hpp`, `Reify.hpp`, `NormativeReferences.md`) is a header-only translation unit (or, for `NormativeReferences.md`, a generated companion file) emitted into the build tree (`build/<preset>/_codegen/include/fixpp/<vXX>/`, never the source tree per `[arch §7.2]`); compile-time cost is the trade we accept; C++23 modules / PCH are explicitly post-v1.0 per `[SYN §3.3 Q11]`.
3. Lock the **multi-version coexistence model** per `[SYN §3.3 Q12]` and `[FIXT §5]`: a single binary may host FIX 4.2, FIX 4.4, FIX 5.0 SP2, and FIXT.1.1 simultaneously; `fixpp::v42::NewOrderSingle` and `fixpp::v50sp2::NewOrderSingle` are distinct types under distinct namespaces; each `Session` owns one `Dictionary` whose `version_profile` (§4.3) carries the *session* version, the *default application* version, and a *per-message-override* policy bit; the typed-message dispatch resolves the application version per message per `[FIXT §5.1]` / `[FIXT §5.3]` (`ApplVerID(1128)` if present, else `DefaultApplVerID(1137)`, else the session's configured default). The C-ABI surface (owned by **2i**) carries a runtime version tag on `fixpp_msg_t` reflecting the *resolved* per-message application version.
4. Lock the **dialect-extension layering** per `[SYN §3.3 Q13]`: per-session FIX dialect overrides (catalogue row D-009 / COM-011) compose **additively at runtime** onto the loaded standard `Dictionary`; no full pre-built dialect dictionary is required. Validation is dialect-aware *within the bounds of the v1.0 overlay grammar* (§4.4.1). The overlay is value-typed by default (§4.4 decision), composed at session-open time, frozen for the session's lifetime per `[arch §5.6]`.
5. Specify the **typed-message flyweight contract** owned here: every generated `fixpp::v42::NewOrderSingle{view}` etc. is a flyweight over a `wire::View`-derived `MessageView` (per `[2b §4.3]`), inherits `[2b §6.4]`'s lifetime contract, never owns bytes, never allocates. Per-tag accessors carry `[[clang::lifetimebound]]` on view-returning methods and `[[nodiscard]]` on every `expected_t<T>`-returning method.
6. Specify the cross-strand **view-escape hatch** named in `[2b §6.6]` — *the bare `MessageView::reify(mr)` reference 2b makes is satisfied by 2c-published free function templates in `<fixpp/dict/reify.hpp>`*, not by retroactively adding a method to 2b's `MessageView`. Two entry points: (a) `dict::reify_as<Msg>(view, mr)` for callers that know the typed message class at compile time, returning `expected_t<owning_message_t<Msg>>`; (b) `dict::reify(view, profile, mr)` for runtime dispatch from a session FSM or C-ABI `fixpp_msg_t`, returning `expected_t<owning_message_handle>` (a type-erased holder the C-ABI wraps as `fixpp_owning_msg_t`). The dispatch table is auto-generated alongside the typed messages — one entry per (resolved-version, MsgType) pair (§4.8).
7. Stay zero-allocation on the hot path between parse and `fromApp` per `[const §VIII.5]` and `[2b §6.6]`'s three-arena pinning. Codegen output is `constexpr` — static storage, no allocation. Typed-message accessors are flyweights — no allocation.
8. Stay exception-free on the hot path per `[arch §5.3]`. Exceptions are reserved for **construction-time** `XmlLoader` failures (bad XML, unknown FIX version, malformed dialect overlay file) — the same ergonomic carve-out 2a/2b take. Every `noexcept` API in this doc that internally allocates from PMR routes the throw through `fixpp::core::detail::trap_throw` (per `[2a §4.2]`) and translates to a documented `dict::error` variant (§6.1.1, §6.7).

### 1.1 Scope boundary — what 2c owns vs what it doesn't

2c owns the *typed-message classes themselves*, the *constexpr metadata* they consult, the *runtime dictionary* (`Dictionary` + `XmlLoader` + `DialectOverlay` + `table_view`), and the *reify bridge* (`dict::reify` + `dict::reify_as`); it does **not** own:

- **Wire-format mechanics.** Parse / serialize / validate of bytes-on-the-wire is owned by **2b** (`Parser`, `Writer`, `Validator`, `Framer`). Typed messages reuse 2b's primitives: a `fixpp::v50sp2::NewOrderSingle` is constructed from a `wire::MessageView<wire::access_mode::Index>` (per `[2b §4.3]`); its accessors call `MessageView::get<Tag>()` under the hood.
- **Field representation types.** Decimal is `fixpp::decimal_t` from **2a** (per `[2a §4.4]`'s `FIXPP_DECIMAL_T` alias rule — one symbol set per build); integer/string/timestamp/UTCTimestamp/boolean/MultiCharValue/MultiStringValue/etc. are concrete representations selected by `dict::field_traits<DataType>` specializations (§4.1.3). 2c selects which trait specialization to substitute at each accessor; it does not re-implement field decoding.
- **Session semantics.** Sequence numbers, gap fills, recovery, ResendRequest, the `ApplVerID(1128)` resolution algorithm at the *session-FSM* layer — owned by `session/` (Phase 4). 2c provides the typed-message *classes* the session FSM dispatches on, the `dict::reify` bridge it dispatches *through*, and the `version_profile` shape it consults; 2c does not interpret OrdStatus, ExecType, or any application semantics, and does not own the FSM state machine that walks header `ApplVerID` per `[FIXT §5.3]`.
- **C ABI surface.** `fixpp_msg_t`, `fixpp_dict_t`, `fixpp_owning_msg_t`, the per-tag C-typed accessors (`fixpp_msg_field_int`, `fixpp_msg_field_decimal`, …), the runtime version tag's bit layout, and the opaque arena handle shape are owned by **2i**. 2c records the *contract* (§5) and the C++ surface 2i wraps; the C-typed shape itself is 2i's call.

### 1.2 Magnitude domain — codegen footprint and scale boundaries

These caps are observed properties of the per-version dictionaries and a budget for the header set 2c emits. They are **not** wire-layer DoS bounds (those live in `[2b §1.2]` and apply at parse time on a hostile peer); they bound the *static* footprint of the codegen output and the runtime cost of merging a `DialectOverlay`.

- **Per-version typed-message count (codegen scope).** FIX 4.2: ~50 messages. FIX 4.4: ~95 messages (with components). FIX 5.0 SP2: ~118 messages (the v1.0-locked set, A-001..A-013 + M-/P-/C-/R-/N- families). FIXT.1.1: 7 session-layer messages per `[FIXT §5]` (`Logon`, `Heartbeat`, `Logout`, `TestRequest`, `ResendRequest`, `SequenceReset`, `Reject`). The codegen tool emits one `class` per message under each version's namespace.
- **Per-version field-metadata table size.** FIX 5.0 SP2 standard dictionary defines ~1700 distinct field tags and ~300 components. The `Fields.hpp` table is keyed by *(MsgType, tag)* occurrences (§4.1) — ~118 messages × ~30 average tags per message ≈ ~3500 `FieldRef` entries; at 16 bytes per entry the per-version table is **~56 KiB** of static data. Across all four codegen versions: ~224 KiB total — comfortable for ROM/text-section budgets. The per-occurrence sizing pattern matches `[2b §1.2]`'s offset-table sizing rebuild — `[2b §1.2]` v0.2 pinned offset-table cost per *occurrence* per message rather than per distinct tag globally; 2c's per-version field-metadata table follows the same shape (per N-P3-2).
- **Per-version `Validator.hpp` size.** Per-message rule tables (required-field sets, conditional-rule pointers, header/trailer ordering) for FIX 5.0 SP2's ~118 messages: ~30 KiB. Across all four codegen versions: ~100 KiB.
- **Per-version `Messages.hpp` + `Reify.hpp` size (per N-P2-6).** `Messages.hpp` header text on the order of 200–400 KiB per version (one `class` per typed message; per-tag accessors are `inline noexcept` shells over `wire::MessageView::get<Tag>()`). The per-version `Reify.hpp` carries the per-message `owning_<Msg>` classes only (no dispatch switch) — about **~370 KiB per version**. The dispatch switch is dispatch-shared (one-shot, not per-version): `_codegen/include/fixpp/_dispatch/reify_dispatch_application.hpp` (the ~470-case application switch across the four codegen versions) plus `_codegen/include/fixpp/_dispatch/reify_dispatch_fixt.hpp` (the 7-case FIXT admin switch — see §4.8 / RC-1) total **~50 KiB**, included once per all-versions TU, not four times. Across all four codegen versions: per-version-replicated cost is ~370 KiB × 4 ≈ 1.5 MB; dispatch-shared cost is ~50 KiB once. All-versions TU compile-time math reflects this — single-version TUs do not pull in the dispatch headers unless they explicitly depend on the `fixpp::dict::dispatch` CMake target (§7.6).
- **Compile-time cost ceiling.** A single TU including only one version's `Messages.hpp` + `Reify.hpp` compiles in **≤ 3 s** on Linux/Clang/x86_64 release at `-O2` (Tier 1 regression bar; §9 seam #2). The all-versions TU is **not supported by default** — a translator/gateway is expected to build one TU per direction, including only that direction's version. If a downstream consumer insists on the all-versions TU, the soft ceiling is **≤ 15 s**; above this, the §10 follow-up (modules/PCH adoption) is reopened. Per N-P2-3.
- **Dialect-overlay merge cost.** A typical per-session `DialectOverlay` adds 5–50 fields and 0–5 messages. The merge into a base `Dictionary` runs once at session open and completes in **≤ 1 ms** on the same hardware (Tier 1 regression bar; §9 seam #4). Asymptotic complexity: **O(N_base + N_overlay log N_overlay)** where `N_base` is the merged-table-clone cost and the `log` factor comes from sorted-merge of overlay additions into the per-MsgType `FieldRef` arrays (per N-P3-3; §6.4). Above 1 ms with overlays at the §1.2 size budget: regression. Above the §4.4.2 entry-cap (1024 additions): `XmlLoader` rejects with `dict_overlay_too_large`.
- **`owning_message_t<>` deep-copy cost.** A typed `dict::reify_as<Msg>(view, mr)` of a 20-tag `NewOrderSingle` into a fresh PMR arena completes in **≤ 1 µs** on the same hardware; a 200-tag Instrument-heavy message completes in **≤ 10 µs** (Tier 1 regression bar; §9 seam #6). The cost is dominated by the byte-copy of the underlying frame plus an offset-table rebuild.
- **`owning_message_t<>` allocation count (re-derived against the §4.8 declaration per N-P2-4).** **≤ 4 PMR allocations** per `dict::reify_as<Msg>`, itemised against the L1063-1072-shape `owning_<Msg>` declaration:
  1. The `bytes_` `pmr::vector<std::byte>` storage allocation (one allocation, sized to the source `frame_view::bytes().size()`).
  2. The `unique_ptr<wire::OffsetTable, pmr_deleter>` allocation for the `OffsetTable` *object itself* (one allocation, `sizeof(OffsetTable)` bytes — previously invisible in v1.0 prose).
  3. The `OffsetTable`'s entry array (one allocation, per `[2b §4.4]`'s open-address layout).
  4. The `OffsetTable`'s hash overlay (one allocation, per `[2b §4.4]`; possibly fused with #3 by the implementation but not required to be).
  Total: 4. The v1.0 prose at §1.2 about "any per-message `pmr::vector` the `owning_<Msg>` class needs to root the `frame_view`/`MessageView` rebuild" is dropped — there is no such vector in the §4.8 declaration; the lazy-view design (§4.8) materializes `frame_view`/`MessageView` from `bytes_.data()` directly. The runtime-dispatch variant `dict::reify(view, profile, mr)` adds one allocation if the type-erased `owning_message_handle` is heap-backed (which is implementation-defined; the small-buffer-optimised variant elides it). Per N-P2-4 + N-P2-5.

These are **caller-relevant scale boundaries**, not invariants of the FIX spec; they exist to bound static footprint, codegen compile cost, and per-session merge cost. The §10 Q1 follow-up confirms compile-cost actuals against this budget once 2c implementation lands.

### 1.3 Version coverage in v1.0

This is the load-bearing scope decision; it differs from the v0.1 draft, which silently re-scoped the constitutional surface. The user's locked decision: **keep four codegen namespaces (`v42`, `v44`, `v50sp2`, `vt11`); ship runtime-XML support for FIX 4.0 / 4.1 / 4.3 / 5.0 / 5.0 SP1 in addition to the codegen versions**. Appendix D drafts the constitutional amendment that the orchestrator applies on sign-off.

**Application messages.** A-001..A-013 (typed) plus M-/P-/C-/R-/N- families: typed-message classes generated for v42/v44/v50sp2 (and any subset thereof a message has been defined for in the source XML). **A-014..A-034 are codegen-deferred to v1.x** per the proposed `[const §XVIII.7]` sub-clause (Appendix D §3); v1.0 ships runtime-XML access for those messages via `view.get(uint16_t tag)` per `[2b §4.3]`. Out of scope for v1.0: FIX-Latest A-035..A-065 per `[const §XVIII]`. (Per RC#3 / C-P1-1.)

**Codegen scope** (D-008 discharged for these versions; codegen-vs-runtime-XML disposition recorded as a coverage-index supplemental note per Appendix D §2). Typed-message classes, `constexpr` field metadata, per-message validators, `Reify.hpp` runtime-dispatch switch all generated under per-version namespaces:

- FIX 4.2 (`fixpp::v42`)
- FIX 4.4 (`fixpp::v44`)
- FIX 5.0 SP2 (`fixpp::v50sp2`)
- FIXT.1.1 session-layer (`fixpp::vt11`) — 7 admin types per `[FIXT §5]`

**Runtime-XML scope** (D-001 / D-002 / D-003 / D-007 / D-009 discharged across nine versions). `XmlLoader` accepts QuickFIX-XML for any of these; runtime `Dictionary` works (field/required/group/length-pair lookups against a runtime-loaded XML); the user accesses fields through the runtime tag-keyed accessor (`view.get(uint16_t tag)` per `[2b §4.3]`); **no typed accessors**, no `fixpp::v40::*` / `v41::*` / `v43::*` / `v50::*` / `v50sp1::*` namespace generated:

- FIX 4.0, FIX 4.1
- FIX 4.2 (also codegen)
- FIX 4.3
- FIX 4.4 (also codegen)
- FIX 5.0, FIX 5.0 SP1
- FIX 5.0 SP2 (also codegen)
- FIXT.1.1 (also codegen)

**Out of scope for v1.0** per `[const §XVIII]`:

- FIX-Latest application messages (A-035..A-065) and FIX-Orchestra repository format (D-011) — `[const §XVIII.2]` v1.2.
- SOFH (`[const §XVIII.2]` v1.1), SBE (v1.3), FIXP (v1.4), FAST (v1.5), JSON (v1.6), GPB (v1.7), FIX MMT (v1.8).

**Catalogue row dispositions** (Appendix A elaborates against actual catalogue text):

- D-001 (FIX 4.2 data dictionary — all standard messages, fields, components, groups): **codegen + runtime XML.**
- D-002 (FIX 4.4 data dictionary): **codegen + runtime XML.**
- D-003 (FIX 5.0SP2 + FIXT.1.1 data dictionary): **codegen + runtime XML.**
- D-004 (FIX 4.0, 4.1 data dictionaries — older, minimal): **runtime XML only** (no codegen). Post-v1.0 codegen deferred per the proposed amendment (Appendix D).
- D-005 (FIX 4.3 data dictionary): **runtime XML only.**
- D-006 (FIX 5.0, 5.0SP1 data dictionaries): **runtime XML only.**
- D-007 (XML data dictionary format loader — parse FIX standard XML at runtime): **all 9 versions** above.
- D-008 (Code-generated constexpr field metadata — zero-runtime-cost field lookup): **four codegen versions only** (v42, v44, v50sp2, vt11). The catalogue row title in `feature-catalogue.md` line 77 covers the broader 4.0–5.0SP2 surface (left intact per the locked decision); the codegen-vs-runtime-XML disposition is recorded as a supplemental note attached to D-008 in `library/spec/coverage-index.md` (the orchestrator applies the supplemental-note edit on sign-off — Appendix D §2). Per RC#3 / C-P2-2.
- D-009 (Custom dictionary extension — user-defined fields and messages): `DialectOverlay` value type + additive merge (§4.4 / §6.4), within the v1.0 grammar closure (§4.4.1).
- D-010 (Component definition support — reusable field groups, Instrument/Parties/etc.): `ComponentRef` (§4.2) + per-version generated component shells under each codegen namespace; runtime-XML versions reach components through the runtime `Dictionary` lookup.
- D-011 (FIX Latest / FIX Orchestra repository format): **deferred post-v1.0** per `[const §XVIII.2]`.

`feature-catalogue.md` is **not edited from this rewrite** — the catalogue text stands. `library/spec/coverage-index.md` receives a single supplemental note attached to D-008 (codegen-vs-runtime-XML scope split — Appendix D §2); the orchestrator applies that edit during the sign-off commit. This section records the dispositions and Appendix A maps them against the actual row titles.

## 2. Non-goals

- **No FIX-Latest application messages (A-035..A-065).** Per `[const §XVIII.2]`, FIX Latest is v1.2. The codegen tool *recognises* unknown MsgTypes in the dictionary XML but emits them only under a `FIXPP_CODEGEN_ENABLE_FIX_LATEST` feature flag scheduled for v1.2; v1.0 builds reject `<message msgtype="…">` entries outside the locked set with a build-time codegen warning (downgradable to error in CI).
- **No codegen for FIX 4.0 / 4.1 / 4.3 / 5.0 / 5.0 SP1.** Runtime-XML support ships in v1.0; per-version codegen is post-v1.0 best-effort per Appendix D's proposed amendment to `[const §XVIII]`. Users on those versions access fields through the runtime tag-keyed accessor (per `[2b §4.3]`'s `MessageView::get(uint16_t tag)`).
- **No SOFH / SBE / FAST / FIXP / JSON / GPB / FIX MMT codegen.** Per `[const §XVIII.2]` these are v1.1+ shipping targets; 2c v1.0 emits Tag=Value SOH typed messages only. The codegen tool's output IR is encoding-agnostic on paper (the same `FieldRef` metadata can drive an SBE encoder), but the v1.0 output is exclusively the header-only `constexpr` Tag=Value form per `[SYN §3.3 Q11]`.
- **No runtime dictionary mutation after session open** (per N-P2-6). Per `[arch §5.6]`, `SessionConfig` (and therefore the active `Dictionary` + `DialectOverlay`) is frozen at session open. **2c rejects mid-session swap categorically** — there is no `Session::swap_dialect_overlay(...)` API, no `swap_dictionary(...)` API. The supported pattern is close the session, swap, reopen. Appendix D §5 drafts the corresponding `[arch §5.6]` amendment to drop "dialect overlay swap" from the list of supported mutating ops.
- **No codegen-as-a-runtime-service.** `tools/codegen/fixpp-codegen` is a build-time host tool per `[const §III.5]`; it does not link into the engine binary and is not invokable from C++ user code. Runtime *additions* to a loaded standard dictionary go through `DialectOverlay` (§4.4), not by re-running codegen.
- **No `std::variant`-of-typed-messages dispatch surface.** Each generated typed message is its own class; the session FSM dispatches by `MsgType` string and constructs the appropriate typed view, *or* calls the type-erased `dict::reify(view, profile, mr)` runtime-dispatch bridge (§4.8) which returns an `owning_message_handle` (a type-erased owner the C-ABI wraps). A user-facing "sum type over all messages" is rejected — every additional message in the variant inflates compile time and overload-resolution cost across every TU. Users wanting variant-style dispatch build their own.
- **No reflection-based field iteration.** Typed messages expose named per-tag accessors (`order.cl_ord_id()`, `order.symbol()`, …) generated from the dictionary; bulk iteration goes through the underlying `wire::MessageView::begin()/end()` (per `[2b §4.3]`), which is the iterator the user gets if they want to walk every field generically. No `magic_get`-style reflection over the typed class members.
- **No `__attribute__((constructor))` global registry.** Each typed-message class is referenced by name when the user constructs one; there is no auto-registration of "every typed message in this version" into a global table. The runtime-dispatch switch in `Reify.hpp` is a *compile-time-emitted* `switch` over (resolved-version, MsgType), not a static-init-ordering-dependent registry.
- **No mid-session overlay grammar extension.** The v1.0 overlay grammar (§4.4.1) is hard-restricted: additive fields/messages/components, simple types only; **no conditional-required rules, no Length+Data pair declarations**. Overlay XML that declares either is rejected with `dict_overlay_unsupported_rule` / `dict_overlay_unsupported_length_pair`. Per RC-6; §10 carries the v1.x extension question.

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
> 2. Emits `include/fixpp/<vXX>/Messages.hpp` (typed messages — flyweights), `include/fixpp/<vXX>/Fields.hpp` (`constexpr` field metadata tables), `include/fixpp/<vXX>/Validator.hpp` (per-message rules), `include/fixpp/<vXX>/Reify.hpp` (per-message `owning_<Msg>` classes + runtime-dispatch switch — added in v1.0 per RC-2), `include/fixpp/<vXX>/NormativeReferences.md` (generated per-message spec citations — added in v1.0 per C-P3-2).
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

> `SessionConfig` is value-typed and frozen at session open. No mid-session reconfiguration of dictionary, security profile, message store, executor, lock policy. Mutating ops (e.g., pinset rotation, dialect overlay swap) go through their own APIs and are explicitly thread-aware.

(2c v1.0 narrows this: 2c explicitly rejects mid-session dialect-overlay swap; Appendix D §5 amends `[arch §5.6]` to drop the implication.)

From `[arch §6]`:

> Each pluggable interface gets ≤5 pure-virtual methods `[const §XIV.2]`. Larger surfaces require a one-paragraph justification reviewed at Gate A.

From `[arch §7.3]`, `[arch §7.4]`, `[arch §9.1]`, `[arch §9.2]` (header surface, CMake targets, public/internal split, versioning) — applied verbatim in §4 and §7 below.

From `[const §XV.13]` (banned patterns):

> Eager codegen with no runtime dictionary path. Hybrid mandated: codegen for standard fields (D-008), runtime XML loader for custom (D-007 + D-009).

This document **delivers the hybrid**: codegen emits the standard per-version fields/messages as `constexpr` arrays for the four codegen versions (§1.3); `XmlLoader` plus `DialectOverlay` cover D-007 (custom-field handling) and D-009 (runtime dictionary loading) at session open *for all nine v1.0-supported versions*.

This document refines that surface; it does **not** diverge.

## 4. Public C++ API

The dictionary module's public surface lives under `include/fixpp/dict/`. The generated typed-message headers live under `include/fixpp/<vXX>/` in the build tree (not the source tree, per `[arch §7.2]`).

### 4.1 `fixpp::dict::FieldRef` — per-tag metadata

The smallest and most-replicated metadata unit: one `FieldRef` per (codegen-version, MsgType, tag) triple in `Fields.hpp`. Lifetime: `constexpr` static storage for codegen versions; PMR-allocated metadata-handle storage for runtime-XML versions (§4.3, §6.5).

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
// Conditional-required rules carry a non-null `condition_index` into the
// per-message conditional-rule table; the wire-layer Validator
// (`[2b §4.6]` rule 6) consults the table to decide presence at validate time.
// Per RC-6: only the *codegen versions* may carry Conditional rules in v1.0;
// the v1.0 dialect-overlay grammar (§4.4.1) rejects overlay XML that
// declares conditional-required tags with `dict_overlay_unsupported_rule`.
enum class presence : std::uint8_t {
    NotDeclared = 0,    // tag is not part of this MsgType's grammar
    Optional    = 1,
    Required    = 2,
    Conditional = 3,    // codegen-version base only in v1.0; consult condition_index
};

struct FieldRef {
    std::uint16_t tag;                // 0..65535 (matches `[2b §1.2]`'s wire range)
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
                                      // The codegen-emitted table 2b §4.3's
                                      // `field_iterator`'s static `constexpr`
                                      // Length+Data table is built from
                                      // (§7.1). v1.0 dialect overlays cannot
                                      // extend this table — overlay XML
                                      // declaring a Length+Data pair is
                                      // rejected with
                                      // `dict_overlay_unsupported_length_pair`
                                      // (§4.4.1, RC-6).
    std::uint16_t _reserved;          // padding to 16 bytes for cache-line
                                      // friendliness; reserved for future flags
                                      // under FIXPP_DICT_FIELDREF_RESERVED_USED
                                      // (set to zero on emit; ignored on read in
                                      // v1.0; matches `[2a §4.2]` discipline —
                                      // C-P3-1).
};
static_assert(sizeof(FieldRef) == 16);
static_assert(alignof(FieldRef) == 2);
static_assert(std::is_standard_layout_v<FieldRef>);
static_assert(std::is_trivially_copyable_v<FieldRef>);

}  // namespace fixpp::dict
```

Notes:

- **One `FieldRef` per (MsgType, tag) pair**, not per tag globally. For example, tag `OrderID(37)` appears in `ExecutionReport`'s `FieldRef` table (presence=Required for normal order workflows), in `Confirmation`'s (presence=Optional), in `OrderCancelRequest`'s (presence=Optional, since the ack carries it back), etc. — each carries the `presence` rule appropriate to its message context. The per-version `Fields.hpp` therefore holds *all per-MsgType FieldRefs* sorted first by MsgType-index then by tag; the per-message `Validator.hpp` references slices into this array. (Per N-P3-1 — corrected from v0.1's `ClOrdID(11)` example, which is in fact required across most order-workflow MsgTypes.)
- **Group context is one `uint16_t` indirection** (`group_no_tag`), not a recursive embedded structure. A field inside a nested group (e.g., `LegInstrument` inside `NoLegs`) carries the innermost `NoXxx` tag; the per-version `GroupRef` table (§4.2) carries the parent-of-group chain so `wire::Validator` can resolve nesting at validate time. This keeps `FieldRef` trivially copyable (a hard requirement for `constexpr` storage of the per-version arrays — §1.2 budget).
- **`condition_index` is an indirection**, not an embedded `std::function`. Conditional-Required rules per `[FIX50SP2 §3.4]` are codegen'd as a per-message `constexpr std::array<conditional_rule, N>` whose entries are simple structs (e.g., `{tag, predicate_id, payload}`); the `wire::Validator`'s default impl interprets them. No type-erased callable; no allocation. The full conditional-rule grammar is small (the FIX 5.0 SP2 spec defines ~15 distinct predicate shapes — "if tag X is present then tag Y must be present", "if tag Z = 'value' then tag W must be present", etc.) and is enumerated in `Validator.hpp` per codegen version. **Dialect overlays cannot introduce new conditional rules** in v1.0 (§4.4.1, RC-6).
- **`length_pair_data_tag`** is the 2c-side commitment for `[2b §4.3]`'s `field_iterator` Length+Data static table. See §7.1 for the binding mechanism. **Dialect overlays cannot extend this table** in v1.0 (§4.4.1, RC-6); users with custom Length+Data pairs regenerate `fixpp-codegen` against a custom XML (§7.1 trailing paragraph).
- **`_reserved` discipline.** Set to zero on emit; ignored on read in v1.0; reserved for future flags under `FIXPP_DICT_FIELDREF_RESERVED_USED`. Matches the same discipline `[2a §4.2]` and `ComponentRef` / `GroupRef` (§4.2) follow. Per C-P3-1.

#### 4.1.3 `dict::field_traits<T>` — typed decoding over `wire::field_view`

The 2c-owned typed-decoding layer referenced from `[2b §1]` line 28 and `[2b W-009]` (line 806). `wire::MessageView<Index>` itself exposes only the two untyped accessors per `[2b §4.3]` lines 281–288 (`template <std::uint16_t Tag> get() const noexcept -> expected_t<field_view>` and `get(std::uint16_t tag) const noexcept -> expected_t<field_view>`); the typed-by-`T` decoding is layered here, in `fixpp::dict`, plus — for the decimal case — the merged 2a parse entry point `decimal_t::parse(span, mr)` per `[2a §4.3]` (a thin shell over `decimal_traits<FIXPP_DECIMAL_T>::from_chars(span, mr)`, `[2a §4.2]`).

> **RC#2 amendment (v1.4, `[const §XX]`).** v1.3 described the decimal route as `decimal_t::from_chars(fv->bytes())` — a no-`mr`, member-on-`decimal_t` call citing `[2a §4.2]`. That symbol does not exist on the merged 2a/001 surface: 2a's only decimal-parse entry points are `decimal_traits<T>::from_chars(std::span<const std::byte>, std::pmr::memory_resource*) noexcept -> expected_t<T>` (`[2a §4.2]`, mirrored at `specs/001-core-decimal/contracts/decimal_traits.hpp:98-100`) and the `decimal<T>` shell `decimal_t::parse(std::span<const std::byte>, std::pmr::memory_resource*) noexcept -> expected_t<decimal>` (`[2a §4.3]`, mirrored at `decimal_traits.hpp:162-163`), **both PMR-mandatory**; 2a's own Gate A explicitly removed the single-argument form (`[2a §8]`; `2a-decimal.md:532` Codex P2 #4 / Opus P1 #4). The corrected decimal route below threads an explicit `std::pmr::memory_resource* mr` into the decimal accessor and calls the real entry point `decimal_t::parse(fv->bytes(), mr)` (`[2a §4.3]`; its return type `expected_t<decimal_t>` matches the accessor return type exactly). All other v1.3 decisions in this sub-section (and the non-decimal `field_traits<T>` / `decode_field<T>` route) carry forward unchanged.

```cpp
// include/fixpp/dict/field_traits.hpp
namespace fixpp::dict {

// Primary template; specialisations cover std::string_view, char,
// std::int32_t / std::int64_t, bool, the timestamp/date types, and the
// MultiCharValue / MultiStringValue split. The `decimal_t` case is NOT a
// `field_traits` specialisation — it routes through the merged 2a
// PMR-mandatory parse `decimal_t::parse(span, mr)` (`[2a §4.3]`, a shell
// over `decimal_traits<FIXPP_DECIMAL_T>::from_chars`, `[2a §4.2]`)
// directly (a `field_traits<decimal_t>` shell would just forward to the
// same call, and could not carry the required `mr` through
// `from_field_view`'s mr-less signature).
template <class T>
struct field_traits;  // primary; specialised below

template <>
struct field_traits<std::string_view> {
    [[nodiscard]] static expected_t<std::string_view>
    from_field_view(wire::field_view const& fv) noexcept;
};

template <>
struct field_traits<char> {
    [[nodiscard]] static expected_t<char>
    from_field_view(wire::field_view const& fv) noexcept;
};

// ... specialisations for the other field-view-decodable types ...

// Helper that combines the get-then-check-then-decode shape every typed
// accessor in §4.7 (and the `dict::reify` algorithm in §4.8) inlines.
// Defined for every T that has a `field_traits<T>` specialisation; the
// decimal case uses `decimal_t::parse(fv->bytes(), mr)` (`[2a §4.3]`)
// inline at the call site (it needs the caller-threaded `mr`, which this
// mr-less helper cannot forward) rather than going through this helper.
template <class T>
[[nodiscard]] inline expected_t<T>
decode_field(expected_t<wire::field_view> fv) noexcept {
    if (!fv) return std::unexpected{fv.error()};
    return field_traits<T>::from_field_view(*fv);
}

}  // namespace fixpp::dict
```

`field_traits<T>::from_field_view` and `decode_field<T>` are `noexcept` and allocation-free; they sit on the typed-accessor hot path (§6.2 ≤ 20 ns ceiling for the string/int/char accessors). Both are referenced from §4.7 (typed-message accessor sketches) and §4.8 (`dict::reify` algorithm step 3); the formal home is here, in §4.1.3, so the references in those sections compile against a single declaration.

The **decimal route is distinct and is *not* `field_traits`-routed and *not* unconditionally allocation-free** (RC#2 / `[const §XX]` v1.4). It calls the merged 2a `decimal_traits<decimal_t>::from_chars(fv->bytes(), mr)` (`[2a §4.2]`), which is `noexcept` but **PMR-mandatory**: it takes a non-null `std::pmr::memory_resource* mr`. For the default `FIXPP_DECIMAL_T == pod_decimal` the parse ignores `mr` and is allocation-free, so the *default-traits* decimal accessor stays zero-alloc; but for an allocating substituted `FIXPP_DECIMAL_T` (e.g. `cpp_dec_float`, a supported substitution per `[2a §4.4]`) the parse may allocate from `mr` per call. The decimal accessor therefore takes an explicit `std::pmr::memory_resource* mr` argument so a valid resource is always in scope (the borrowed flyweight holds no arena and the AC-G7 `sizeof == one pointer` invariant forbids adding one — §4.7); the latency ceiling for the decimal accessor is the separate ≤ 75 ns row in §6.2, not the ≤ 20 ns string/int/char row. The decimal route's allocation contract is `[const §VIII.5]`-coherent: any heap traffic goes through the caller-supplied per-message arena `[arch §5.2]`, never raw `new`/`delete`, consistent with `[const §XV.1]`.

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
    std::uint16_t _reserved;           // forward-compat; zero on emit, ignore on
                                       // read in v1.0; reserved under
                                       // FIXPP_DICT_COMPONENTREF_RESERVED_USED
                                       // (per C-P3-1).
};
static_assert(sizeof(ComponentRef) == 12);
static_assert(std::is_trivially_copyable_v<ComponentRef>);

struct GroupRef {
    std::uint16_t no_tag;              // NoXxx tag (e.g., 73 for NoOrders, 555 for NoLegs)
    std::uint16_t first_field_tag;     // First field of group rule per [FIX50SP2 §3]
                                       // — used by wire::Validator (`[2b §4.6]`'s
                                       // group_first_field) and by group_view::iter()
                                       // (`[2b §4.7]`).
    std::uint16_t first_field_index;   // index into the per-version FieldRef array
                                       // for the group's field list
    std::uint16_t field_count;
    std::uint16_t parent_group_no_tag; // 0 if not nested; otherwise the enclosing
                                       // group's NoXxx tag (handles W-007 nested
                                       // repeating groups per `[2b §4.7]`).
    std::uint16_t _reserved;           // forward-compat; zero on emit, ignore on
                                       // read in v1.0; reserved under
                                       // FIXPP_DICT_GROUPREF_RESERVED_USED
                                       // (per C-P3-1).
};
static_assert(sizeof(GroupRef) == 12);
static_assert(std::is_trivially_copyable_v<GroupRef>);

}  // namespace fixpp::dict
```

The `_reserved` `uint16_t` in each table mirrors the 2a discipline: zero on emit, ignore on read in v1.0; future minor version may use under the matching feature macro per C-P3-1. Applies uniformly to `FieldRef`, `ComponentRef`, `GroupRef`.

### 4.3 `fixpp::dict::Dictionary` — runtime owner + dialect-overlay binding

`Dictionary` is the runtime handle a `Session` holds. It is constructed by `XmlLoader::load(...)` (§4.5), composed with an optional `DialectOverlay` (§4.4) at session open, and frozen for the session lifetime per `[arch §5.6]`.

The v1.x design pins the *metadata block* (the merged FieldRef / ComponentRef / GroupRef tables and the dialect-overlay overrides) on the heap behind a single `dict::detail::dict_metadata_handle`. `Dictionary` carries a `dict::detail::dict_metadata_handle_ptr` — now a `std::shared_ptr<const dict_metadata_handle>` allocated via `std::allocate_shared` over a `std::pmr::polymorphic_allocator<dict_metadata_handle>` so the shared-control-block deallocator returns memory to the originating PMR resource (per RC#2 / N-P1-1; ownership shape consolidated to `shared_ptr` everywhere per C-R2-P1-1). The `with_overlay` sharing path stores a *copy* of the base's `handle_` inside the merged handle's `base_keepalive_` slot (the same `shared_ptr<const dict_metadata_handle>` — both the base's `Dictionary::handle_` and the merged handle's `base_keepalive_` share a single control block). The keepalive lives inside the merged handle, not on the `Dictionary` value. This makes `Dictionary` cheap to move (a `shared_ptr` move is no-throw — no allocation, no atomic ref-count change — preserving the same `noexcept` discipline `unique_ptr` move offered), makes the metadata block's address survive moves, and pins the lifetime root that `table_view` (§4.6) borrows from. Per RC-3 / RC#2 / C-R2-P1-1.

```cpp
// include/fixpp/dict/version_profile.hpp
namespace fixpp::dict {

// The *session* version on the wire, per `[FIXT §5]`. For unified
// pre-FIXT.1.1 sessions, both session and application-default coincide.
enum class session_version : std::uint8_t {
    Unknown = 0,
    v40     = 1,    // runtime-XML only (no codegen namespace)
    v41     = 2,    // runtime-XML only
    v42     = 3,    // codegen + runtime-XML
    v43     = 4,    // runtime-XML only
    v44     = 5,    // codegen + runtime-XML
    v50     = 6,    // runtime-XML only
    v50sp1  = 7,    // runtime-XML only
    v50sp2  = 8,    // codegen + runtime-XML
    vt11    = 9,    // codegen (FIXT.1.1 session-layer); split-vocabulary parent
};

// The *default application* version a FIXT.1.1 session resolves to when
// `ApplVerID(1128)` is absent on a message and `DefaultApplVerID(1137)` was
// set at Logon time (`[FIXT §5.1]`). For unified pre-FIXT.1.1 sessions, this
// equals the session version. The session FSM (Phase 4) walks the
// resolution algorithm at message time; 2c records the *value space*.
enum class application_version : std::uint8_t {
    Unknown = 0,
    v40     = 1,
    v41     = 2,
    v42     = 3,
    v43     = 4,
    v44     = 5,
    v50     = 6,
    v50sp1  = 7,
    v50sp2  = 8,
};

// 4-byte profile carried by every `Dictionary`. Replaces the v0.1
// single-byte `dict::version` enum. Per RC-1.
struct version_profile {
    session_version     session;                  // 1 byte
    application_version default_appl;             // 1 byte
    bool                has_per_message_override; // 1 byte;
                                                  // true iff the session is
                                                  // FIXT.1.1 AND `ApplVerID(1128)`
                                                  // is allowed per message
                                                  // (`[FIXT §5.3]`).
    std::uint8_t        _reserved;                // pad to 4 bytes; zero on
                                                  // emit, ignore on read in
                                                  // v1.0; future flags under
                                                  // FIXPP_DICT_VERSION_PROFILE_RESERVED_USED.
};
static_assert(sizeof(version_profile) == 4);
static_assert(std::is_trivially_copyable_v<version_profile>);

// Resolved per-message version axis. Closes the v1.0 RC#1 gap (a value
// that can carry both `vt11` admin messages and any `application_version`
// resolution result). The runtime dispatch in `dict::reify` walks two
// distinct dispatch families keyed off `kind`:
//   - `kind::session_admin` — MsgType matched the FIXT admin set
//     (Logon/Heartbeat/TestRequest/Reject/SequenceReset/Logout/ResendRequest);
//     dispatch through `_codegen/include/fixpp/_dispatch/reify_dispatch_fixt.hpp`
//     (7 cases) into `vt11::owning_<Msg>`. The `application` byte is
//     `application_version::Unknown` for this kind.
//   - `kind::application` — MsgType is an application message; the
//     `application` byte carries the resolved `application_version` from
//     `Dictionary::resolve_application_version(...)` (which now errors on
//     unresolved Unknown rather than returning it as success). Dispatch
//     through `_codegen/include/fixpp/_dispatch/reify_dispatch_application.hpp`
//     (~470 cases across the four codegen versions).
// 3 bytes data + 1 byte pad → 4 bytes total; align 1.
struct resolved_message_version {
    enum class kind : std::uint8_t { session_admin, application };
    kind                k;                   // 1 byte
    session_version     session;             // 1 byte; the FIXT session of
                                             // an application message, or
                                             // `vt11` for session_admin
    application_version application;         // 1 byte; valid when
                                             // k == application; equal to
                                             // application_version::Unknown
                                             // when k == session_admin
    std::uint8_t        _reserved;           // pad to 4 bytes; zero on emit,
                                             // ignore on read in v1.0
};
static_assert(sizeof(resolved_message_version) == 4);
static_assert(alignof(resolved_message_version) == 1);
static_assert(std::is_trivially_copyable_v<resolved_message_version>);

// Free-function form of the `[FIXT §5]` priority resolution. Per C-R2-P2-1:
// the algorithm needs only `version_profile` (the session/default-appl
// surface) plus the wire `ApplVerID(1128)` value, NOT a Dictionary's merged
// metadata block. Published as a free function so callers that don't hold a
// `Dictionary` (notably `dict::reify`, which receives only `MessageView` +
// `version_profile` + `mr`; and `version_registry` consumers) can run the
// resolution algorithm directly.
//
// Algorithm:
//   1. If `appl_ver_id_value` is non-empty, parse it via the wire→C++
//      enum mapping (see §4.3 wire-mapping table near `application_version`
//      and N2-P3-1). On parse success return the resolved
//      `application_version`; on parse failure return
//      `dict_unknown_appl_ver_id`.
//   2. If `appl_ver_id_value` is empty, use `profile.default_appl`. If
//      `profile.default_appl == application_version::Unknown`, return
//      `dict_unresolved_application_version` (per RC#1 / C-P1-5; matches
//      the member-function semantics).
//
// `Dictionary::resolve_application_version` (member, §4.3 in the
// `Dictionary` class) is a thin wrapper that calls
// `dict::resolve_application_version(this->which(), appl_ver_id_value)`.
[[nodiscard]] expected_t<application_version>
resolve_application_version(version_profile profile,
                            std::string_view appl_ver_id_value) noexcept;

}  // namespace fixpp::dict
```

**Wire `ApplVerID(1128)` → C++ `application_version` enum mapping.** Per `[FIXT §5.1]` (`DefaultApplVerID(1137)`) and `[FIXT §5.3]` (per-message `ApplVerID(1128)`), the FIX-defined wire enum carries values that do *not* coincide with this doc's C++ `application_version` internal indices. The parse implementation of `dict::resolve_application_version` (and the member wrapper on `Dictionary`) must use the spec mapping below — recreating the conflation by reusing the C++ enum's internal index would mis-map FIX 5.0 and the SP variants. Cited inline so a future implementer doesn't repeat the mistake (per N2-P3-1):

| Wire `1128` value | FIX version | C++ `application_version` |
|---|---|---|
| `"0"` | FIX 2.7 | (not modelled — pre-4.0; reject as unknown) |
| `"1"` | FIX 3.0 | (effectively unused; reject as unknown) |
| `"2"` | FIX 4.0 | `application_version::v40` |
| `"3"` | FIX 4.1 | `application_version::v41` |
| `"4"` | FIX 4.2 | `application_version::v42` |
| `"5"` | FIX 4.3 | `application_version::v43` |
| `"6"` | FIX 4.4 | `application_version::v44` |
| `"7"` | FIX 5.0 | `application_version::v50` |
| `"8"` | FIX 5.0 SP1 | `application_version::v50sp1` |
| `"9"` | FIX 5.0 SP2 | `application_version::v50sp2` |

Empty `appl_ver_id_value` → use `profile.default_appl` (per `[FIXT §5.1]`). The C++ `application_version` enum's internal indices (`Unknown=0, v40=1, …, v50sp2=8`) are NOT the wire values; do not reuse them in the parse switch.

```cpp
// include/fixpp/dict/dictionary.hpp
namespace fixpp::dict {

namespace detail {
    // PMR-aware deleter — published utility, but its primary use in v1.2 is
    // *via* `std::allocate_shared` (whose internal control block invokes the
    // deleter logic implicitly) rather than as a direct `unique_ptr`
    // deleter. Carries `mr` plus the object's size/align so destruction
    // returns memory to the originating PMR resource. Retained as a
    // published utility for callers that hold `dict_metadata_handle`-like
    // objects through a `unique_ptr` (e.g., transient internal scratch
    // values within `XmlLoader`); the public `dict_metadata_handle_ptr`
    // alias no longer uses it directly. Per RC#2 / N-P1-1 /
    // C-R2-P1-1.
    template <class T>
    struct pmr_deleter {
        std::pmr::memory_resource* mr;
        std::size_t                object_size;
        std::size_t                object_align;
        constexpr void operator()(T* p) const noexcept {
            if (!p) return;
            p->~T();
            mr->deallocate(p, object_size, object_align);
        }
    };

    // Heap-pinned metadata block. Holds the merged FieldRef/ComponentRef/
    // GroupRef tables (either spans into per-version `constexpr` arrays for
    // codegen versions, or PMR-allocated copies for runtime-XML versions
    // and overlay-merged Dictionaries), plus the dialect-overlay overrides,
    // plus the `version_profile`, plus an optional `base_keepalive_` slot
    // (a `dict_metadata_handle_ptr` — i.e., a
    // `shared_ptr<const dict_metadata_handle>` — that pins the *base*
    // dictionary's metadata block alive for the merged dictionary's
    // lifetime — populated only on the `with_overlay` sharing path; null
    // otherwise). Stable address across `Dictionary` moves. Per RC#2 /
    // RC-3 / N-P1-1 / C-R2-P1-1.
    class dict_metadata_handle {
    public:
        // Members (full layout published; no longer just a forward
        // declaration):
        //   std::span<FieldRef const>     merged_fields_;
        //   std::span<ComponentRef const> merged_components_;
        //   std::span<GroupRef const>     merged_groups_;
        //   version_profile               profile_;
        //   std::pmr::memory_resource*    mr_;                // owning resource
        //   dict_metadata_handle_ptr      base_keepalive_;    // null unless
        //                                                     // produced by
        //                                                     // `with_overlay`;
        //                                                     // a copy of the
        //                                                     // base's handle_,
        //                                                     // sharing the
        //                                                     // base's control
        //                                                     // block (per
        //                                                     // C-R2-P1-1)
        // … plus PMR-allocated storage for runtime-XML / overlay copies that
        // backs the spans above. Implementation detail beyond this point;
        // accessors mirror `Dictionary`'s public surface.
        // ...
    };

    // Public alias used by `Dictionary` and friends. The handle is
    // shared-owned: constructed via `std::allocate_shared<dict_metadata_handle>(
    // std::pmr::polymorphic_allocator<dict_metadata_handle>{mr}, ...)` so the
    // control block's deallocator returns memory to the originating `mr`.
    // `Dictionary::operator=(Dictionary&&) noexcept` is no-throw because
    // `shared_ptr` move is no-throw (no allocation, no atomic ref-count
    // change). The `with_overlay` sharing path copies the base's
    // `dict_metadata_handle_ptr` into the merged handle's `base_keepalive_`
    // slot, sharing the same control block. Per RC#2 / N-P1-1 / C-R2-P1-1.
    using dict_metadata_handle_ptr = std::shared_ptr<const dict_metadata_handle>;
}

// Lifetime: typically constructed once at engine init from XmlLoader output,
// optionally composed with one DialectOverlay per session, then frozen.
// Holds a single `detail::dict_metadata_handle_ptr` (a
// `shared_ptr<const dict_metadata_handle>` allocated via
// `std::allocate_shared` over a `pmr::polymorphic_allocator` — see the
// detail block above). Moves are cheap (`shared_ptr` move is no-throw and
// touches no atomics) and the metadata block's address survives the move.
// `with_overlay` allocates a fresh `dict_metadata_handle` on the
// user-supplied `mr`, and stores a *copy* of the base's `handle_` in the
// merged handle's `base_keepalive_` slot (the same
// `shared_ptr<const dict_metadata_handle>`, sharing the base's control
// block — NOT a separate `shared_ptr` constructed from the base's raw
// pointer), so the merged Dictionary lives independently while base
// storage is kept alive (sharing path; see §6.5). Per C-R2-P1-1.
//
// Storage classes (§8 PMR recap):
//   - The standard per-version FieldRef/ComponentRef/GroupRef tables are
//     `constexpr` (`Fields.hpp` per codegen version) — borrowed via span
//     by the metadata handle, not copied.
//   - For runtime-XML versions (FIX 4.0/4.1/4.3/5.0/5.0SP1), the loaded
//     tables are PMR-allocated into the metadata handle's storage.
//   - The dialect-overlay's *additions and overrides* are PMR-allocated
//     into the merged Dictionary's metadata handle on a resource the user
//     supplies at `with_overlay` time (typically the session's long-lifetime
//     arena; see §8 and `[arch §5.2]`).
//   - The composed lookup tables (the merged FieldRef view consumed by
//     wire::Validator and codegen accessors) live in the same metadata
//     handle; building the merged view is the §6.4 operation.
class Dictionary {
public:
    // Lifecycle: move-only-on-init, frozen-after-first-handoff, thread-safe-
    // on-read; `with_overlay` is single-threaded. Per N-P2-4. Move is
    // `noexcept` and allocates nothing — the underlying
    // `dict_metadata_handle_ptr` is a `shared_ptr` and `shared_ptr` move
    // touches no atomics (per C-R2-P1-1; matches the prior `unique_ptr`-
    // based move semantics one-for-one).
    Dictionary(Dictionary const&) = delete;
    Dictionary& operator=(Dictionary const&) = delete;
    Dictionary(Dictionary&&) noexcept = default;
    Dictionary& operator=(Dictionary&&) noexcept = default;
    ~Dictionary();

    // Returns the version profile for this Dictionary.
    [[nodiscard]] version_profile which() const noexcept;

    // Resolve the per-message application version per `[FIXT §5]`. For unified
    // pre-FIXT.1.1 dictionaries, returns `which().default_appl` regardless
    // of `appl_ver_id_value`. For FIXT.1.1 dictionaries (`which().session ==
    // session_version::vt11`), returns the value derived per the spec
    // resolution algorithm: if `appl_ver_id_value` is non-empty, parse it
    // and return; else return `which().default_appl`. The session FSM
    // (Phase 4) is the call site; 2c records the algorithm.
    //
    // **Thin wrapper (per C-R2-P2-1).** This member function is provided
    // for callers that already hold a `Dictionary`; its body is
    //     return dict::resolve_application_version(this->which(),
    //                                              appl_ver_id_value);
    // The free function `dict::resolve_application_version(version_profile,
    // std::string_view)` (declared in `<fixpp/dict/version_profile.hpp>`,
    // see above) is the standalone form for `dict::reify` and
    // `version_registry` callers who don't hold a `Dictionary`. Both share
    // the identical algorithm; the algorithm depends only on the
    // `version_profile`, not on the merged metadata block.
    //
    // Errors:
    //   - `dict_unknown_appl_ver_id` if `appl_ver_id_value` is present
    //     but doesn't parse to a known `application_version`.
    //   - `dict_unresolved_application_version` if both inputs are absent
    //     (empty `appl_ver_id_value` AND `which().default_appl ==
    //     application_version::Unknown`). This is the FIXT.1.1 session
    //     edge case: Logon never set `DefaultApplVerID(1137)` and the
    //     inbound message lacks the per-message `ApplVerID(1128)` override
    //     and the session config supplied no fallback. Closes the v1.0
    //     RC#1 "Unknown as success sentinel" gap (per C-P1-5 / RC#1).
    //     `application_version::Unknown` is no longer a successful return
    //     value from this function.
    [[nodiscard]] expected_t<application_version>
    resolve_application_version(
        std::string_view appl_ver_id_value) const noexcept;

    // Look up a single field by tag in the *MsgType context*. Returns the
    // composed (overlay + standard) FieldRef; presence reflects the rule for
    // this message type. Returns NotDeclared if the tag is not part of this
    // MsgType's grammar (after overlay merge).
    [[nodiscard]] FieldRef
    field_ref(std::string_view msg_type, std::uint16_t tag) const noexcept;

    // Required-field set for a given MsgType (after overlay merge). Returned
    // span aliases the metadata-handle storage; lifetime is the metadata
    // block's (i.e., survives `Dictionary` moves; ends when the last
    // `Dictionary` referring to the handle is destroyed).
    [[nodiscard]] std::span<std::uint16_t const>
    required_fields(std::string_view msg_type) const noexcept
        [[clang::lifetimebound]];

    // Is `tag` declared for `msg_type` per the composed dictionary?
    // Drives `wire::Validator` rule 5 (`[2b §6.5]`).
    [[nodiscard]] bool
    field_valid_for(std::string_view msg_type, std::uint16_t tag) const noexcept;

    // First-field-of-group rule per `[FIX50SP2 §3]` — used by wire's
    // `group_view::iter()` (`[2b §4.7]`) and Validator (`[2b §4.6]`'s
    // group_first_field).
    [[nodiscard]] std::uint16_t
    group_first_field(std::uint16_t no_tag) const noexcept;

    // Length+Data pair lookup for `[2b §4.3]`'s field_iterator dict-free path
    // and for Index-mode parsing. The standard (codegen'd) table is the
    // exhaustive FIX 5.0 SP2 list per `[FIX50SP2 §3.3]`; runtime-XML
    // versions get their per-version pair list at load time. Dialect
    // overlays do NOT extend this table in v1.0 (§4.4.1, RC-6).
    [[nodiscard]] std::uint16_t
    length_pair_data_tag(std::uint16_t length_tag) const noexcept;

    // Overlay-aware promotion: was this tag a "previously-unknown custom" tag
    // (D-009 / COM-011) that the active overlay promoted into the typed
    // surface? Diagnostic / observability hook; not on the hot path.
    [[nodiscard]] bool was_dialect_promoted(std::uint16_t tag) const noexcept;

    // Compose this base Dictionary with a DialectOverlay. Returns a *new*
    // Dictionary whose metadata handle's `base_keepalive_` slot carries a
    // copy of this Dictionary's `handle_` (sharing the same control block;
    // sharing path; §6.5) and adds the overlay's entries on top in the
    // supplied PMR resource. `mr` MUST outlive the returned Dictionary.
    // Per C-R2-P1-1.
    //
    // Construction-time API: called by Session::open() before the session
    // is exposed to the user.
    //
    // Threading (per N-P2-3 — refined from v1.0's blanket "single-threaded"):
    //   - Concurrent `with_overlay` calls on the same *base* `Dictionary`
    //     from different threads are SAFE. The base's metadata is read-only
    //     once the base has been frozen-after-handoff (per N-P2-4); the
    //     `shared_ptr` ref-count operations on the merged handle's
    //     `base_keepalive_` are atomic.
    //   - Concurrent `with_overlay` calls *on the same overlay value* from
    //     different threads are UB. The overlay's `pmr::vector::push_back`
    //     during overlay-build races; treat the overlay value as
    //     single-threaded during build (which matches the documented
    //     `DialectOverlay::create(mr)` → populate → consume pattern in
    //     §4.4 / §6.4).
    // Failure modes:
    //   - dict_overlay_conflict          (overlay collides under Reject policy)
    //   - dict_overlay_too_large         (entry cap exceeded; §4.4.2)
    //   - dict_overlay_unsupported_rule  (overlay declares a conditional-
    //                                     required rule; §4.4.1, RC-6)
    //   - dict_overlay_unsupported_length_pair (overlay declares a
    //                                     Length+Data pair; §4.4.1, RC-6)
    //   - dict_overlay_oom               (PMR allocation failure; trapped
    //                                     via `[2a §4.2]` `trap_throw`;
    //                                     §6.1.1, RC-5)
    //   - dict_field_not_in_version, dict_msg_type_not_in_version
    //
    // Asymptotic complexity: O(N_base + N_overlay log N_overlay). §6.4,
    // N-P3-3.
    [[nodiscard]] expected_t<Dictionary>
    with_overlay(DialectOverlay const& overlay,
                 std::pmr::memory_resource* mr) const noexcept;

    // Bridge to wire's table_view (§4.6). Cheap (returns a value-typed
    // borrowed handle); aliases this Dictionary's metadata-handle storage.
    // The returned table_view is what wire::Parser<Index> and
    // wire::dictionary_driven_validator consume. The metadata handle's
    // address is heap-pinned (§4.3, RC-3) so `Dictionary` moves do not
    // invalidate outstanding `table_view`s.
    [[nodiscard]] table_view as_table_view() const noexcept
        [[clang::lifetimebound]];

private:
    // Heap-pinned metadata block, shared-owned (per RC#2 / N-P1-1 /
    // C-R2-P1-1). `dict_metadata_handle_ptr` is now
    // `shared_ptr<const dict_metadata_handle>`. Constructed via
    // `std::allocate_shared<dict_metadata_handle>(
    //      std::pmr::polymorphic_allocator<dict_metadata_handle>{mr}, ...)`
    // so the shared-control-block's deallocator returns memory to the
    // originating `mr`. The shared-base sharing path lives *inside* the
    // merged `dict_metadata_handle`'s `base_keepalive_` slot (a *copy* of
    // the base's `handle_`; the merged handle and the base share the same
    // control block) — not on the `Dictionary` value.
    detail::dict_metadata_handle_ptr handle_;
};

}  // namespace fixpp::dict
```

`Dictionary` is move-only; copying would silently duplicate the metadata-handle storage. Sessions hold a `Dictionary` by value (in `SessionConfig`); the `detail::dict_metadata_handle_ptr` (a `shared_ptr<const dict_metadata_handle>` allocated via `std::allocate_shared` over a `std::pmr::polymorphic_allocator`, per C-R2-P1-1) makes the move cheap (a `shared_ptr` move is no-throw, allocates nothing, and touches no atomics — the same `noexcept` discipline `unique_ptr` move offered) and pins the metadata block on the heap so `table_view`s and `dictionary_driven_validator` instances that aliased it before the move remain valid. The `with_overlay` sharing path stores a *copy* of the base's `handle_` as `base_keepalive_` *inside the merged handle* — the merged handle's `base_keepalive_` and the base's `Dictionary::handle_` share the same control block (one `shared_ptr<const dict_metadata_handle>` pointing at the base's metadata block, refcount = 2 after `with_overlay` returns; refcount → 1 after the user drops the base, → 0 when the merged dict is finally destroyed). Per RC#2 / RC-3 / N-P1-1 / C-R2-P1-1.

The `table_view` value-typed handle (§4.6) borrows the metadata block by raw pointer (`detail::dict_metadata_handle const*`); its lifetime contract is **explicitly tied** to the heap-pinned `dict_metadata_handle` lifetime — as long as some `Dictionary` (base or merged) is alive, the metadata block is alive, and `table_view` is valid. `Dictionary` move into `SessionConfig` does not invalidate any outstanding `table_view` because the `dict_metadata_handle_ptr` move keeps the same heap pointer. Test seam #13 (move-into-`SessionConfig` + outstanding `table_view`) exercises this concrete contract.

### 4.4 `fixpp::dict::DialectOverlay` — additive-merge value type

**v1.0 design decision: `DialectOverlay` is a value type, not a runtime virtual interface.** The catalogue row D-009 and `[SYN §3.3 Q13]` describe the *behaviour* (additive at runtime, per-session); they do not require pluggability. A runtime virtual interface would buy the ability to inject computed-on-the-fly overlay rules (e.g., a regulator-supplied OPRA-feed rules engine), but the v1.0 use cases — venue dialects, regulator-mandated tags, customer-overlay scenarios — are all *static data* known at session open and loaded from XML or built programmatically. A virtual interface would (a) pay vtable overhead on every per-tag lookup, (b) prevent the overlay-merge result from being a metadata-handle-owned table, and (c) require justification against the `[const §XIV.2]` ≤5-pure-virtual cap. Justification: none of the tested overlay scenarios need it. The decision is reversible — a future minor-version `DialectOverlayPlugin` virtual interface can be added without breaking the value-typed `DialectOverlay` API; tracked in §10.

```cpp
// include/fixpp/dict/dialect_overlay.hpp
namespace fixpp::dict {

// Conflict policy when an overlay's field/message definition collides with
// the base dictionary's. Default: OverlayWins.
enum class overlay_conflict_policy : std::uint8_t {
    OverlayWins = 0,   // default; overlay's rule replaces base's
    BaseWins    = 1,   // overlay is "advisory only"; base prevails
    Reject      = 2,   // any collision returns error::dict_overlay_conflict
};

// Constructed via DialectOverlay::create(mr) or
// XmlLoader::load_overlay(path, mr). The default constructor is deleted —
// every DialectOverlay carries a non-null PMR resource for its `pmr::vector`
// and `pmr::string` members. Per C-P2-5 (programmatic construction must pin
// the resource).
class DialectOverlay {
public:
    // Factory: pins the resource for all member containers. `mr` MUST
    // outlive the DialectOverlay (typically the session's long-lifetime
    // arena, or a scratch arena consumed by `Dictionary::with_overlay`).
    [[nodiscard]] static DialectOverlay create(
        std::pmr::memory_resource* mr) noexcept;

    DialectOverlay() = delete;
    DialectOverlay(DialectOverlay const&) = delete;
    DialectOverlay& operator=(DialectOverlay const&) = delete;
    DialectOverlay(DialectOverlay&&) noexcept;
    DialectOverlay& operator=(DialectOverlay&&) noexcept;
    ~DialectOverlay();

    // Field additions and overrides. Each entry overrides the same (msg_type,
    // tag) pair in the base Dictionary if present, or adds a new declaration
    // if not. v1.0 grammar (§4.4.1) restricts the FieldRef shapes that can
    // appear here — see §4.4.1 for the closure.
    [[nodiscard]] std::pmr::vector<FieldRef>& field_additions() noexcept;
    [[nodiscard]] std::span<FieldRef const> field_additions() const noexcept
        [[clang::lifetimebound]];

    // Message additions. Each entry registers a new MsgType not present in
    // the base dictionary (typical use: COM-011 dialect-private MsgTypes).
    // Existing MsgTypes cannot be wholesale replaced; field-level overrides
    // suffice for the supported scenarios.
    [[nodiscard]] std::pmr::vector<message_addition>& message_additions() noexcept;
    [[nodiscard]] std::span<message_addition const> message_additions() const noexcept
        [[clang::lifetimebound]];

    overlay_conflict_policy conflict_policy() const noexcept;
    void set_conflict_policy(overlay_conflict_policy p) noexcept;

    [[nodiscard]] std::string_view name() const noexcept [[clang::lifetimebound]];
    void set_name(std::string_view n);

    [[nodiscard]] std::pmr::memory_resource* resource() const noexcept;

private:
    explicit DialectOverlay(std::pmr::memory_resource* mr) noexcept;
    // ... PMR-pinned members ...
};

struct message_addition {
    std::pmr::string msg_type;            // e.g., "U1" for a custom MsgType
    std::pmr::vector<FieldRef> fields;    // the message's grammar (v1.0
                                          // grammar closure per §4.4.1)
    std::pmr::vector<std::uint16_t> required_fields;
    // No conditional-rule support in v1.0 dialect additions; overlay XML
    // declaring conditional-required tags is rejected with
    // `dict_overlay_unsupported_rule` (§4.4.1, RC-6).
};

}  // namespace fixpp::dict
```

`DialectOverlay` is constructed either:

- by `XmlLoader::load_overlay(path, mr)` — reads a QuickFIX-XML-style overlay file (a `<fix>...</fix>` document with the same schema as the base FIX dictionary, containing only the additions/overrides), or
- programmatically by user code that wants to build overlays in-process (e.g., a translator that reads venue-config from a database and constructs `FieldRef` entries) via `DialectOverlay::create(mr)` followed by `field_additions().push_back(...)` etc.

Both paths pin the overlay's storage to a non-null PMR resource (per C-P2-5; §9 seam #11 verifies).

The merge into a `Dictionary` runs in `Dictionary::with_overlay(...)` (§4.3) — **once at session open**, not per-message. The merge result is stored in the user-supplied PMR resource; subsequent per-message lookups consult the merged tables with no further allocation. Merge cost ≤ 1 ms per §1.2; complexity O(N_base + N_overlay log N_overlay) per N-P3-3.

#### 4.4.1 Dialect-overlay grammar in v1.0

The v1.0 overlay grammar is **hard-restricted** because the validator and the `field_iterator`'s static Length+Data table cannot enforce or skip the broader grammar at runtime without a recompile. Per RC-6, the overlay loader and `Dictionary::with_overlay` reject XML that exceeds the closure with documented errors rather than silently accept a grammar the engine cannot honour.

**Overlay XML CAN:**

- **Add new fields** with simple types (`String`, `Int`, `Char`, `Boolean`, `Currency`, `Exchange`, `Country`, `MonthYear`, `UtcTimestamp`, `UtcTimeOnly`, `UtcDateOnly`, `LocalMktDate`, `Float` — which substitutes `fixpp::decimal_t` per `[2a §7.2]`, and any of the integer family `Length`/`SeqNum`/`NumInGroup`/`DayOfMonth`).
- **Add new MsgTypes** with a flat field list (`Required` + `Optional` only — no `Conditional`).
- **Add new components** (reusable field bundles), referenced by the new MsgTypes' field lists.
- **Promote a tag's optionality** on an existing MsgType — e.g., promote `Optional` to `Required` for a venue-specific `NewOrderSingle` ClientID. The tag must already be declared on the base MsgType; this is a presence-rule change, not a new tag.
- **Add a new MsgType that uses a new component or new repeating group** (the overlay declares the component/group along with the MsgType; per N-P2-1's recommendation we permit new groups inside new MsgTypes only, not into existing MsgTypes).

**Overlay XML CANNOT:**

- **Declare conditional-required rules.** Overlay XML containing `<field required="conditional" condition="..."/>` is rejected with `dict_overlay_unsupported_rule`. (Per RC-6 / C-P2-4.)
- **Declare Length+Data pairs.** Overlay XML containing a `<field type="LENGTH" pair="DataTag"/>` declaration or equivalent is rejected with `dict_overlay_unsupported_length_pair`. Users with custom Length+Data pairs regenerate `fixpp-codegen` against a custom XML (§7.1 trailing paragraph).
- **Add a new repeating group to an existing MsgType.** Adding groups is permitted only inside *new* overlay MsgTypes; promoting an existing message's grammar to carry a new group requires regen.
- **Change a field's data type** on an existing MsgType.
- **Remove a field** from an existing MsgType.

The closure is enforced by `XmlLoader::load_overlay` at parse time (rejecting XML that exceeds the grammar) and by `Dictionary::with_overlay` at merge time (rejecting overlays constructed programmatically that violate the closure). §9 seams #11–#13 verify the reject paths.

#### 4.4.2 Entry caps

Per §1.2 the overlay merge cost ceiling is ≤ 1 ms; to keep this enforceable the loader pins:

- **Max field additions:** 1024 entries (configurable per session under `FIXPP_DICT_OVERLAY_MAX_FIELDS`; defaults to 1024).
- **Max message additions:** 64 entries.
- **Max component additions:** 64 entries.
- **Max XML bytes:** 4 MiB (configurable under `FIXPP_DICT_XML_MAX_BYTES`).
- **Max XML nesting depth:** 32 (configurable under `FIXPP_DICT_XML_MAX_DEPTH`).
- **External entities / DTDs:** disabled by default (DoS guard against billion-laughs; per C-P2-2).
- **Component-cycle detection:** the loader detects cyclical component references (a `<component name="X"><component-ref name="Y"/></component><component name="Y"><component-ref name="X"/></component>` pattern) at parse time and rejects with `dict_xml_schema_violation`.

Above any cap: `dict_overlay_too_large` for overlay-side caps, `dict_xml_too_large`/`dict_xml_too_deep`/`dict_xml_schema_violation` for XML-shape caps.

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
    // ergonomically. PMR allocations within `load*` are wrapped in
    // `[2a §4.2]`'s `fixpp::core::detail::trap_throw` and translated to
    // `dict_xml_oom` on PMR failure (§6.1.1, RC-5).
    XmlLoader();

    // Load a per-version standard or QuickFIX-XML-format dictionary from a
    // file path. Allocates from `mr`; the returned Dictionary's metadata
    // handle is heap-pinned (§4.3) and either borrows the per-version
    // `constexpr` tables (codegen versions: v42, v44, v50sp2, vt11) or
    // PMR-allocates copies of the loaded tables (runtime-XML-only versions:
    // v40, v41, v43, v50, v50sp1).
    //
    // Throws `dict::xml_parse_error` (which derives from `std::runtime_error`)
    // on malformed XML; throws `dict::unknown_version_error` on a version
    // string outside the v1.0 supported set; otherwise returns the loaded
    // Dictionary by value. PMR allocation failure is translated to a thrown
    // `dict::xml_oom_error` (the constructor-time analogue; XmlLoader is
    // exception-API by carve-out).
    [[nodiscard]] Dictionary
    load(std::filesystem::path const& xml_path,
         std::pmr::memory_resource* mr);

    // Load a dialect overlay XML file. Same exception discipline. Rejects
    // overlay grammar that exceeds the §4.4.1 closure with
    // `dict::xml_unsupported_rule_error` / `dict::xml_unsupported_length_pair_error`.
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

`XmlLoader` exists primarily for D-007 (XML loader, all 9 versions) and D-009 (custom dictionary extension). For the four codegen versions, the typical session uses the codegen'd `constexpr` tables and applies an overlay (loaded via `XmlLoader::load_overlay`) on top. For the five runtime-XML-only versions, `XmlLoader::load` is the only path to a working `Dictionary`.

XML schema compatibility: `XmlLoader` accepts the QuickFIX XML schema (`fields`, `messages`, `components`, `header`, `trailer` top-level elements per OSS-001). Extensions for dialect overlays add a top-level `<overlay>` wrapper; outside that wrapper the overlay file's schema is identical to the base file's, restricted by §4.4.1's grammar closure.

### 4.6 `dict::table_view` — value-typed borrowed handle into a `Dictionary`

`table_view` is the type `wire::Parser<Index>` and `wire::dictionary_driven_validator` consume per `[2b §7.2]`. It is a *value-typed* view (not a virtual interface) into a `Dictionary`'s composed tables.

**Visibility decision (per N-P1-4): `table_view` is public, value-typed, non-friend-constructible.** Users consume the engine-supplied one (returned by `Dictionary::as_table_view()`) and pass it into custom `Validator` plugins. Plugin authors cannot synthesize their own `table_view` directly (the constructor is private; only `Dictionary` is a friend). A multi-source rules engine that wants to combine two dictionaries' tables runs `Dictionary::with_overlay` to produce a merged `Dictionary` and takes a single `table_view` from it; the v1.0 surface deliberately does not expose `table_view` composition because the closure of the v1.0 overlay grammar (§4.4.1) is what `wire::dictionary_driven_validator` relies on.

```cpp
// include/fixpp/dict/table_view.hpp
namespace fixpp::dict {

// A value-typed handle into a Dictionary's composed metadata. Cheap to copy
// (a few pointers + a span), borrows the Dictionary's heap-pinned metadata
// block (§4.3 / RC-3) by raw pointer (`detail::dict_metadata_handle const*`).
// Lifetime contract (per C-R2-P1-1): the metadata block stays alive as long
// as **any** `Dictionary` that references it through `handle_` *or*
// `dict_metadata_handle::base_keepalive_` is alive — i.e., the union of all
// `shared_ptr` references through the same control block. A `table_view`
// MUST NOT outlive that union; in the common case it MUST NOT outlive the
// originating `Dictionary` value, but on the `with_overlay` sharing path,
// the merged Dictionary's `base_keepalive_` keeps the base metadata block
// alive for the merged Dictionary's lifetime even after the user drops the
// base handle (§6.5).
//
// Returned by Dictionary::as_table_view() (§4.3). Held by value inside
// wire::Parser<Index> (`[2b §4.3]`) and wire::dictionary_driven_validator
// (`[2b §4.6]`). Copy/move: trivially copyable; no ownership transfer.
//
// Lifetime: aliases the Dictionary's heap-pinned metadata-handle storage;
// same lifetime class as `wire::View` flyweights (`[2b §4.1]`) — accessing
// after the metadata handle is destroyed is debug trap, release UB (per
// RC-3, treated like `wire::View` per `[2b §6.4]`; NOT a recoverable
// `expected_t<>` error). [[clang::lifetimebound]] on the constructor's
// Dictionary parameter binds the lifetime.
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

    [[nodiscard]] version_profile which() const noexcept;

private:
    friend class Dictionary;
    explicit constexpr table_view(detail::dict_metadata_handle const* h) noexcept;
    detail::dict_metadata_handle const* handle_{nullptr};
};

}  // namespace fixpp::dict
```

`table_view` is **borrowed** (does not own), **trivially copyable** (sized to one pointer + small discriminator). Aliases the `Dictionary`'s heap-pinned metadata handle; the metadata handle's address is stable across `Dictionary` moves (§4.3 / RC-3 / N-P1-1) so the documented `Dictionary`-by-value-in-`SessionConfig` pattern works without lifetime hazards.

Because the metadata handle is heap-pinned, the v0.1 `dict_table_view_stale` `dict::error` variant is **deleted** in v1.0: stale-`table_view` access (after the underlying metadata block is destroyed) is treated like stale `wire::View` access per `[2b §6.4]` — debug trap via a generation-counter mechanism, release UB, NOT a recoverable error. Per RC-3 / C-P2-6.

The `wire::dictionary_driven_validator` (per `[2b §4.6]`) holds a `table_view` by value; the `Dictionary` lives in `SessionConfig`; the metadata handle outlives both for the session's lifetime. Both are frozen after session open, so the lifetime relationship is straightforward.

### 4.7 Generated typed messages — `fixpp::vXX::*`

Every typed-message class is generated by `fixpp-codegen` into `include/fixpp/<vXX>/Messages.hpp` for the four codegen versions (v42, v44, v50sp2, vt11; per §1.3). The class shape is uniform across versions and across messages; only the per-tag accessor list and the `MsgType` constant vary. Runtime-XML-only versions get no typed namespace.

```cpp
// build/<preset>/_codegen/include/fixpp/v50sp2/Messages.hpp  (excerpt)
namespace fixpp::v50sp2 {

// Generated typed-message class. Flyweight over a wire::MessageView<Index>;
// inherits the lifetime contract of `[2b §6.4]`. Constructor binds the view
// by reference; the typed message MUST NOT outlive the view (which itself
// MUST NOT outlive the originating frame buffer).
class NewOrderSingle {
public:
    static constexpr std::string_view msg_type_v = "D";  // [FIX50SP2 §...]
    static constexpr application_version version_v = application_version::v50sp2;

    // Construct from a wire::MessageView<Index>. Validation is the user's
    // responsibility (typically the session FSM has already run
    // wire::Validator::validate(view)); this constructor does not validate.
    explicit NewOrderSingle(
        wire::MessageView<wire::access_mode::Index> const& view
            [[clang::lifetimebound]]) noexcept
        : view_(view) {}

    // Per-tag typed accessors. Each one is an `inline noexcept` shell over
    // wire::MessageView::get<Tag>() (the typed-tag accessor signed at
    // `[2b §4.3]`, returning `expected_t<wire::field_view>`) with the
    // field-traits dispatch baked in by codegen via
    // `dict::field_traits<T>::from_field_view(...)` for non-decimal types
    // and the merged 2a PMR-mandatory parse
    // `decimal_t::parse(fv->bytes(), mr)` for decimal types (per
    // `[2a §4.3]`; decimal accessors take an extra
    // `std::pmr::memory_resource* mr` arg — RC#2 / `[const §XX]` v1.4).
    // The traits family is the 2c-owned typed-decoding layer
    // referenced from `[2b §1]` line 28 / W-009; `MessageView` itself
    // exposes only the untyped `get<Tag>() -> expected_t<field_view>` and
    // `get(uint16_t) -> expected_t<field_view>` accessors per `[2b §4.3]`
    // (lines 281–288). NOT `constexpr` — `OffsetTable::find` is
    // non-constexpr (per `[2b §4.4]`), and `MessageView::get<Tag>` is
    // `noexcept` not `constexpr`. Per N-P2-1.
    // [[nodiscard]] because expected_t<T> is the return type.
    // [[clang::lifetimebound]] on view-returning accessors (string_view,
    // span). The Tag template arg is the constexpr tag number from
    // `[FIX50SP2 §...]`. Codegen factors the three-line "get → check →
    // decode" body into the helper `dict::decode_field<T>(...)` (declared
    // alongside `field_traits` in §4.1.3) so each emitted accessor reads
    // as a single forwarding line; the expanded shape is shown for
    // `cl_ord_id` for clarity.

    [[nodiscard]] inline expected_t<std::string_view>
    cl_ord_id() const noexcept [[clang::lifetimebound]]
    {
        auto fv = view_.template get<11>();
        if (!fv) return std::unexpected{fv.error()};
        return dict::field_traits<std::string_view>::from_field_view(*fv);
    }

    [[nodiscard]] inline expected_t<std::string_view>
    symbol() const noexcept [[clang::lifetimebound]]
    { return dict::decode_field<std::string_view>(view_.template get<55>()); }

    [[nodiscard]] inline expected_t<char>
    side() const noexcept
    { return dict::decode_field<char>(view_.template get<54>()); }

    // Decimal accessors take an explicit per-message arena `mr` (RC#2 /
    // `[const §XX]` v1.4): the merged 2a parse entry point is
    // PMR-mandatory and the flyweight holds no arena (the AC-G7
    // `sizeof == one pointer` invariant forbids storing one). Callers
    // pass the per-message arena `[arch §5.2]` — the same arena the
    // underlying `MessageView` aliases per `[2b §6.4]`. `mr` must be
    // non-null (the wire layer always supplies a valid resource, per
    // `[2a §4.2]`). For the default `pod_decimal` trait the parse ignores
    // `mr` and is allocation-free; allocating substituted traits draw
    // from `mr`.
    [[nodiscard]] inline expected_t<fixpp::decimal_t>
    order_qty(std::pmr::memory_resource* mr) const noexcept
    {
        auto fv = view_.template get<38>();
        if (!fv) return std::unexpected{fv.error()};
        return fixpp::decimal_t::parse(fv->bytes(), mr);  // [2a §4.3]
    }

    [[nodiscard]] inline expected_t<fixpp::decimal_t>
    price(std::pmr::memory_resource* mr) const noexcept
    {
        auto fv = view_.template get<44>();
        if (!fv) return std::unexpected{fv.error()};
        return fixpp::decimal_t::parse(fv->bytes(), mr);  // [2a §4.3]
    }

    // Repeating-group accessor: returns a wire::group_view<Leg> bound to
    // the underlying offset table. group_view contract from `[2b §4.7]`;
    // Leg is a per-message generated struct (also under fixpp::v50sp2).
    [[nodiscard]] inline wire::group_view<NewOrderSingle::Leg>
    legs() const noexcept [[clang::lifetimebound]]
    { return view_.template group<555 /* NoLegs */, NewOrderSingle::Leg>(); }

    // ... (one accessor per declared field; codegen emits the full set)

    // Overlay-promoted tag access (per N-P1-2 / §4.7.1). Forwarded runtime
    // tag-keyed accessor on every typed message. Present on every codegen-
    // generated class so users can reach overlay-promoted tags through the
    // typed surface without dropping back to view().
    [[nodiscard]] inline expected_t<wire::field_view>
    field_value(std::uint16_t tag) const noexcept [[clang::lifetimebound]]
    { return view_.get(tag); }

    // Bridge to wire view for advanced consumers (raw bytes, iteration).
    [[nodiscard]] inline wire::MessageView<wire::access_mode::Index> const&
    view() const noexcept [[clang::lifetimebound]] { return view_; }

    // Per-message group struct (also a flyweight).
    class Leg {
        // ... per-tag accessors for the group entry's fields ...
        // also has `field_value(uint16_t)` for overlay-promoted group tags.
    };

private:
    wire::MessageView<wire::access_mode::Index> const& view_;
};

// Compile-time invariant: a typed flyweight holds exactly one reference and
// no other state. Catches accidental member additions in the codegen
// template.
static_assert(sizeof(NewOrderSingle)
              == sizeof(wire::MessageView<wire::access_mode::Index> const*));

}  // namespace fixpp::v50sp2
```

Key properties:

- **Flyweight contract (per N-P1-3).** The typed flyweight holds `wire::MessageView<Index> const&` — a *reference*, matching the lifetime contract from `[2b §6.4]` (the view aliases the originating frame buffer, the buffer's lifetime is the per-message arena's slot). Because the member is a reference the implicit copy/move-assignment operators are deleted, which is the right semantic for a flyweight: it is constructible and copyable (the implicit copy-ctor copies the reference, which is correct), but you cannot reassign one. The cross-strand transport path is `dict::reify_as<NewOrderSingle>(view, mr)` (§4.8), which produces an `owning_NewOrderSingle` whose internal members do *not* alias each other through references — see §4.8 for the value-typed shape.
- **Zero allocation per non-decimal accessor; decimal accessor is PMR-routed (RC#2 / `[const §XX]` v1.4).** Every *string/int/char* typed accessor is `inline noexcept`, zero-arg, and dispatches directly to `wire::MessageView::get<Tag>()` (itself zero-allocation per `[2b §4.3]` Index mode) + an allocation-free `field_traits<T>` decode — these stay genuinely zero-alloc on the ≤ 20 ns ceiling. The compiler inlines the chain; the per-accessor cost is one `OffsetTable::find` call (~15 ns per `[2b §6.6]`) plus the type-specific dispatch (string/int/char ≤ 5 ns). The **decimal** accessor differs: it takes an explicit `std::pmr::memory_resource* mr` and calls `decimal_t::parse(fv->bytes(), mr)` (`[2a §4.3]`), which is `noexcept` but PMR-mandatory — allocation-free *only* for the default `pod_decimal` trait (which ignores `mr`); an allocating substituted `FIXPP_DECIMAL_T` may draw from `mr` per call. Decimal cost is `find` (~15 ns) + decimal parse (~50 ns per `[2a §6.5]`) on the separate ≤ 75 ns row (§6.2), not the ≤ 20 ns row. Any decimal heap traffic is confined to the caller-supplied per-message arena `[arch §5.2]` — no raw `new`/`delete`, `[const §VIII.5]` / `[const §XV.1]`-coherent.
- **`[[nodiscard]]` on every `expected_t<T>`-returning method.** Mandated by the convergence-log-frozen rule from 2a/2b reviews; codegen template emits the attribute unconditionally.
- **`[[clang::lifetimebound]]` on every view-returning method** (anything returning `std::string_view`, `std::span`, `wire::field_view`, `wire::group_view<...>`). Codegen emits the attribute unconditionally; per `[arch §5.5]`.
- **`Leg`-and-similar nested group structs** are themselves flyweight types over `wire::group_view<T>::operator[](i)` (per `[2b §4.7]`); they follow the same accessor discipline and carry their own `field_value(uint16_t)` forwarder for overlay-promoted group tags.
- **Unknown-fields access.** Every typed message inherits a `unknown_fields()` method from a generated mixin that delegates to `view().unknown_fields()` (per `[2b §4.8]`); users wanting opaque round-trip preservation walk it directly.
- **Multi-version disambiguation.** `fixpp::v42::NewOrderSingle` and `fixpp::v50sp2::NewOrderSingle` are distinct types under distinct namespaces. They cannot be implicitly converted; a translator/gateway that converts FIX 4.2 → FIX 5.0 SP2 builds an explicit converter (using both typed surfaces and a `fixpp::v50sp2::owning_message_t<NewOrderSingle>` constructed from scratch). 2c does not provide a built-in cross-version converter; that is application-specific (and a candidate for a v1.x utility crate).
- **Drop `constexpr` from per-tag accessors per N-P2-1.** Only `msg_type_v` and `version_v` are `constexpr` (they enable `static_assert(NewOrderSingle::msg_type_v == "D")` and equivalent compile-time dispatch). Per-tag accessors are `inline noexcept` only — `OffsetTable::find` is non-`constexpr` per `[2b §4.4]`, so claiming `constexpr` evaluability on the chain would be a lie.

#### 4.7.1 Overlay-promoted tag access on typed messages

Per N-P1-2: when a `DialectOverlay` adds a custom tag (e.g., `9999=VenueRiskID`) to a venue-specific `NewOrderSingle`, the typed accessor surface generated against the *standard* XML does not include the new accessor (codegen ran against the standard XML, not the overlay). Users who want to reach the overlay-promoted tag through the typed surface have two paths:

1. **Forwarded runtime tag-keyed accessor.** Every codegen-generated class carries a `field_value(uint16_t tag) -> expected_t<wire::field_view>` method that forwards to the underlying `wire::MessageView::get(uint16_t tag)` (per `[2b §4.3]`). This works for both compile-time-known and runtime-known tags:

   ```cpp
   // Compile-time-known overlay tag (constexpr value, runtime call site):
   constexpr std::uint16_t VENUE_RISK_ID = 9999;
   auto fv = nos.field_value(VENUE_RISK_ID);  // expected_t<field_view>
   if (fv) { auto sv = fv->as_string(); ... }

   // Runtime-resolved tag (e.g., from a config table):
   std::uint16_t custom = config.lookup("VenueRiskID");
   auto fv = nos.field_value(custom);
   ```

   `field_value` is on every typed message and every nested group struct (`Leg::field_value`, etc.). It is the supported v1.0 ergonomics for overlay-promoted tags.

2. **Regenerate codegen against the venue XML.** Users who want a fully typed `nos.venue_risk_id()` accessor regenerate `Messages.hpp`/`Fields.hpp` against their dialect's XML using `fixpp-codegen` (the build-time host tool per `[const §III.5]`). This is a recompile-against-dialect-XML path, not a runtime path; the resulting headers live under the user's own build target (typically a CMake target named `myorg::fixpp::dict::v50sp2_venuex` or similar — §7.X).

The `Dictionary::was_dialect_promoted(custom_tag)` diagnostic returns `true` for tags reached this way, so observability tooling can flag overlay-promoted access.

### 4.8 `owning_message_t<>` and the `dict::reify` bridge

`[2b §6.6]` names `MessageView::reify(mr) → fixpp::vXX::owning_message_t<MsgClass>` as the supported deep-copy hatch. v1.0 publishes this as a *2c-owned bridge* in `<fixpp/dict/reify.hpp>` (free function templates), satisfying 2b's reference without retroactively adding methods to the 2b-owned `wire::MessageView` class. Per RC-2 / C-P1-3.

```cpp
// include/fixpp/dict/reify.hpp
namespace fixpp::dict {

// Type-erased holder for a reified owning message. Returned by the
// runtime-dispatch variant `dict::reify(view, profile, mr)` for callers
// that don't know the message class at compile time (session FSM,
// C-ABI `fixpp_owning_msg_t` wrapper, IPC bridges).
//
// Internal shape: a small variant over the union of all generated
// `owning_<Msg>` types across the four codegen versions, OR a heap-allocated
// polymorphic owner (implementation chooses; the public surface only
// guarantees a stable accessor set). The implementation may use a
// small-buffer-optimised variant up to a published size threshold to elide
// the heap allocation; above the threshold it heap-allocates.
class owning_message_handle {
public:
    owning_message_handle(owning_message_handle const&) = delete;
    owning_message_handle& operator=(owning_message_handle const&) = delete;
    owning_message_handle(owning_message_handle&&) noexcept;
    owning_message_handle& operator=(owning_message_handle&&) noexcept;
    ~owning_message_handle();

    // Resolved per-message version (the value from `[FIXT §5]` resolution).
    // Returns a `resolved_message_version` (RC#1): for FIXT admin messages
    // (Logon, Heartbeat, …) `kind == session_admin`, `session == vt11`,
    // `application == application_version::Unknown`; for application
    // messages `kind == application` and `application` carries the resolved
    // value (v42, v44, v50sp2). Replaces the v1.0 `application_version`
    // return type (which could not represent vt11 admin messages). The
    // C-ABI exposes the three bytes `(kind, session, application)` per §5
    // commitment 1; 2i may surface getters for `kind` and either of the
    // version bytes at its discretion.
    [[nodiscard]] resolved_message_version version() const noexcept;

    [[nodiscard]] std::string_view msg_type() const noexcept
        [[clang::lifetimebound]];

    // Bridge to wire view over the owned bytes; aliases the handle's
    // storage; lifetime ends with the handle. `[[clang::lifetimebound]]`
    // chains the lifetime warning correctly on accessor returns.
    [[nodiscard]] wire::MessageView<wire::access_mode::Index> const&
    view() const noexcept [[clang::lifetimebound]];

    // Tag-keyed runtime accessor for callers that don't want to downcast.
    [[nodiscard]] expected_t<wire::field_view>
    field_value(std::uint16_t tag) const noexcept
        [[clang::lifetimebound]];

    // Downcast to a typed `owning_<Msg>` if the version + msg_type match.
    // Returns nullptr if the handle's resolved version + msg_type do not
    // match `Msg::version_v` + `Msg::msg_type_v`. The pointer aliases the
    // handle's storage.
    template <class Msg>
    [[nodiscard]] auto as() const noexcept
        [[clang::lifetimebound]] -> owning_<Msg> const*;

private:
    // Implementation detail. Either a small-variant over the (resolved-
    // version, MsgType)-fan-out of generated `owning_<Msg>` types, or a
    // heap-allocated polymorphic owner. Both shapes carry the resolved
    // application version and the MsgType.
    struct impl;
    /* ... */
};

// Typed entry point: caller knows the message class at compile time. No
// dispatch overhead. Returns expected_t<owning_message_t<Msg>>; failures:
//   - dict_reify_oom            (PMR allocation failure trapped via
//                                `[2a §4.2]` `trap_throw`)
//   - dict_reify_msg_type_mismatch (the view's MsgType doesn't match
//                                Msg::msg_type_v)
//
// `dict_reify_version_mismatch` is **dropped** from this entry point per
// RC#1 / C-P2-3: the caller named `Msg::version_v` explicitly by picking
// `Msg`, so a resolved-version mismatch is a caller bug, not engine
// validation. `wire::MessageView` carries no resolved-version state in
// any case (resolution requires `version_profile` + `ApplVerID(1128)`
// which `reify_as` does not receive). The MsgType check stays (it has
// inputs); the version check moves to `reify` only.
template <class Msg>
[[nodiscard]] expected_t<owning_message_t<Msg>>
reify_as(wire::MessageView<wire::access_mode::Index> const& view,
         std::pmr::memory_resource* mr) noexcept;

// Runtime-dispatch entry point: caller has only a type-erased view and a
// version_profile. The `profile` typically comes from the session's
// Dictionary (`session.dictionary().which()`).
//
// Resolution algorithm (per RC#1):
//   1. Peek `MsgType(35)` from `view`.
//   2. Test against the FIXT admin set (the 7 names from §6.3 step 2:
//      `0`/Heartbeat, `1`/TestRequest, `2`/ResendRequest, `3`/Reject,
//      `4`/SequenceReset, `5`/Logout, `A`/Logon). On hit, build
//      `resolved_message_version{kind::session_admin, profile.session,
//      application_version::Unknown}` and dispatch through
//      `_codegen/include/fixpp/_dispatch/reify_dispatch_fixt.hpp` (7
//      cases) into `vt11::owning_<Msg>`, returned wrapped in
//      `owning_message_handle`.
//   3. On miss, read `ApplVerID(1128)` from the view via
//      `view.template get<1128>()` (per `[2b §4.3]` lines 281–288 — the
//      only typed-tag accessor `MessageView<Index>` exposes; returns
//      `expected_t<wire::field_view>`). On success, decode the returned
//      `field_view` to a `std::string_view` via
//      `dict::field_traits<std::string_view>::from_field_view(*fv)` (the
//      typed-decoding layer of §4.1.3). On a `dict_field_not_present`
//      error from the `get<1128>()` call, treat the field as absent and
//      use an empty `std::string_view{}` as the resolution input. Then
//      pass the resulting `std::string_view` to
//      `dict::resolve_application_version(profile, appl_ver_id_value)`
//      (the free function from §4.3, which the `Dictionary` member
//      wraps; per C-R2-P2-1 — `dict::reify` has no `Dictionary` in scope
//      so the free-function form is the supported call site). On
//      `dict_unresolved_application_version`, propagate the error (do
//      NOT fall through to a sentinel). On success, build
//      `resolved_message_version{kind::application, profile.session,
//      resolved_appl}` and dispatch through
//      `_codegen/include/fixpp/_dispatch/reify_dispatch_application.hpp`
//      (the ~470-case application switch).
//
// Failures: `dict_reify_oom`, `dict_reify_msg_type_mismatch`, plus
//   - dict_unknown_appl_ver_id              (per-message ApplVerID parse
//                                            failed)
//   - dict_unresolved_application_version   (FIXT.1.1 session never set
//                                            DefaultApplVerID and the
//                                            inbound message lacks
//                                            ApplVerID(1128); per RC#1)
//   - dict_reify_unknown_msg_type           (resolved-version + MsgType
//                                            combination has no codegen-
//                                            emitted owning_<Msg> — e.g.,
//                                            the resolved version is a
//                                            runtime-XML-only version like
//                                            v50sp1 — those have no typed
//                                            owners in v1.0)
[[nodiscard]] expected_t<owning_message_handle>
reify(wire::MessageView<wire::access_mode::Index> const& view,
      version_profile profile,
      std::pmr::memory_resource* mr) noexcept;

}  // namespace fixpp::dict
```

The per-message `owning_<Msg>` classes live under `fixpp::vXX::*` in the codegen output's `Reify.hpp`:

```cpp
// build/<preset>/_codegen/include/fixpp/v50sp2/Reify.hpp  (excerpt)
namespace fixpp::v50sp2 {

// Owning copy of a NewOrderSingle, deep-copied from a
// wire::MessageView<Index>. Owns its own bytes (copied into the supplied
// PMR memory_resource), its own OffsetTable (rebuilt over the copied bytes),
// and exposes the same typed accessor surface as NewOrderSingle. Safe to
// move across thread/strand boundaries; lifetime is bounded by the supplied
// memory_resource.
//
// Lazy-view design (per N-P1-3): `owning_NewOrderSingle` does NOT carry a
// `MessageView` value member that aliases its `bytes_`. Instead, `view()`
// is computed on access from the owned `frame_view` (which is constructed
// over `bytes_`'s data pointer). The `frame_cache_` / `view_cache_`
// `std::optional`s are reset on move (custom `noexcept` move ctor +
// move-assign) and rebuilt on first `view()` call after move.
//
// **Single-strand-only (per N-P1-2 / C-P1-4).** `owning_<Msg>` instances
// are single-strand-only. The lazy `view()` cache materializes on first
// access and may NOT be accessed concurrently from multiple threads even
// on a `const owning_<Msg>&`. Documented use is reify-on-strand-A → post
// to queue → consume-on-strand-B; concurrent reads on the same instance
// are unsupported. The §6.1 "thread-safe-on-read" claim explicitly carves
// `owning_<Msg>` out (see §6.1).
class owning_NewOrderSingle {
public:
    // Construct from a wire::MessageView<Index> (or via `dict::reify_as`).
    // mr MUST outlive the owning_NewOrderSingle. Returns expected_t<...>;
    // PMR allocation failure surfaces as dict_reify_oom (RC-5).
    static expected_t<owning_NewOrderSingle>
    from_view(wire::MessageView<wire::access_mode::Index> const& view,
              std::pmr::memory_resource* mr) noexcept;

    // Move-only. Copy is rejected — copying would silently duplicate the
    // PMR storage, which is an antipattern (the user wanted move-across-
    // strand, not duplicate-on-each-thread).
    //
    // Custom move ctor + move assignment per N-P1-2 / C-P1-4. The
    // `std::optional` move ctor *moves the contained T*; it does NOT
    // reset the source's optional. Defaulted move would leave a stale
    // cached `view_` on the moved-to instance aliasing pre-move
    // `bytes_.data()` (or freed memory under allocator-unequal moves).
    // The custom move:
    //   1. Moves `bytes_`, `offsets_`, `mr_`.
    //   2. Constructs the destination's `frame_cache_` / `view_cache_`
    //      as `std::nullopt` (do not copy or move from the source's).
    //   3. `reset()`s the source's `frame_cache_` / `view_cache_` so
    //      neither side holds a cache aliasing pre-move state.
    owning_NewOrderSingle(owning_NewOrderSingle const&) = delete;
    owning_NewOrderSingle& operator=(owning_NewOrderSingle const&) = delete;
    owning_NewOrderSingle(owning_NewOrderSingle&& other) noexcept
        : bytes_(std::move(other.bytes_)),
          offsets_(std::move(other.offsets_)),
          mr_(other.mr_),
          frame_cache_(std::nullopt),     // explicit reset
          view_cache_(std::nullopt) {     // explicit reset
        // Source's caches are also invalidated:
        other.frame_cache_.reset();
        other.view_cache_.reset();
    }
    owning_NewOrderSingle& operator=(owning_NewOrderSingle&& other) noexcept {
        if (this != &other) {
            bytes_   = std::move(other.bytes_);
            offsets_ = std::move(other.offsets_);
            mr_      = other.mr_;
            frame_cache_.reset();
            view_cache_.reset();
            other.frame_cache_.reset();
            other.view_cache_.reset();
        }
        return *this;
    }

    static constexpr std::string_view msg_type_v = "D";
    static constexpr application_version version_v = application_version::v50sp2;

    // Same accessor surface as the flyweight NewOrderSingle. Each accessor
    // delegates to view().get<Tag>() (which is computed lazily from the
    // owned bytes_). The decimal accessors mirror the flyweight's
    // PMR-mandatory shape (RC#2 / `[const §XX]` v1.4) but default `mr` to
    // the instance's own owned `mr_` (§4.8 private members) — an
    // `owning_<Msg>` always has an arena in scope, so the argument is a
    // defaulted convenience, not a caller obligation as on the flyweight.
    [[nodiscard]] expected_t<std::string_view> cl_ord_id() const noexcept
        [[clang::lifetimebound]];
    [[nodiscard]] expected_t<std::string_view> symbol() const noexcept
        [[clang::lifetimebound]];
    [[nodiscard]] expected_t<char> side() const noexcept;
    [[nodiscard]] expected_t<fixpp::decimal_t>
    order_qty(std::pmr::memory_resource* mr = nullptr) const noexcept;  // nullptr → use mr_; [2a §4.3]
    [[nodiscard]] expected_t<fixpp::decimal_t>
    price(std::pmr::memory_resource* mr = nullptr) const noexcept;      // nullptr → use mr_; [2a §4.3]
    [[nodiscard]] wire::group_view<NewOrderSingle::Leg> legs() const noexcept
        [[clang::lifetimebound]];
    [[nodiscard]] expected_t<wire::field_view>
    field_value(std::uint16_t tag) const noexcept
        [[clang::lifetimebound]];
    // ... (one per declared field, mirroring NewOrderSingle)

    // View bridge: lazy-computed `MessageView` over the owned bytes.
    // First call after construction or move rebuilds the internal
    // `frame_view` + `OffsetTable` cache; subsequent calls return cached.
    // `[[clang::lifetimebound]]` chains lifetime to *this.
    [[nodiscard]] wire::MessageView<wire::access_mode::Index> const&
    view() const noexcept [[clang::lifetimebound]];

    [[nodiscard]] application_version which() const noexcept { return version_v; }

private:
    // Owned storage. No reference members; no value members that alias each
    // other through references. Per N-P1-3.
    std::pmr::vector<std::byte>          bytes_;
    // Reconstructed offset table for `bytes_`; PMR-allocated on `mr_`.
    // Up to two PMR allocations (entry array + hash overlay; per
    // `[2b §4.4]`), counted in the §1.2 ≤ 4 PMR allocation budget.
    std::unique_ptr<wire::OffsetTable, /* PMR-aware deleter */> offsets_;
    std::pmr::memory_resource*           mr_ = nullptr;
    // Lazy view cache. Mutable so `view() const` can populate. Invalidated
    // on move; rebuilt on first access after move.
    mutable std::optional<wire::frame_view>                          frame_cache_;
    mutable std::optional<wire::MessageView<wire::access_mode::Index>> view_cache_;
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

**Runtime-dispatch switch (per RC#1).** The `dict::reify(view, profile, mr)` runtime variant peeks `MsgType(35)` and tests it against the FIXT admin set (the 7 names from §6.3 step 2: `0`/Heartbeat, `1`/TestRequest, `2`/ResendRequest, `3`/Reject, `4`/SequenceReset, `5`/Logout, `A`/Logon). On hit, it dispatches through the FIXT-admin switch in `_codegen/include/fixpp/_dispatch/reify_dispatch_fixt.hpp` (7 cases) into `vt11::owning_<Msg>`. On miss, it reads `ApplVerID(1128)` via `view.template get<1128>()` (per `[2b §4.3]`, the only typed-tag accessor `MessageView<Index>` exposes), decodes the returned `field_view` to a `std::string_view` via `dict::field_traits<std::string_view>::from_field_view(...)` (§4.1.3), maps a `dict_field_not_present` error from `get<1128>()` to an empty `std::string_view{}` for the resolution input, calls the free function `dict::resolve_application_version(profile, appl_ver_id_value)` (per C-R2-P2-1 — `dict::reify` has no `Dictionary` in scope; the algorithm is profile-only and now errors on unresolved Unknown — see §4.3 / RC#1), and dispatches through the application switch in `_codegen/include/fixpp/_dispatch/reify_dispatch_application.hpp` (~470 cases across the four codegen versions). Sketches (codegen output):

```cpp
// build/<preset>/_codegen/include/fixpp/_dispatch/reify_dispatch_fixt.hpp
// 7 cases — FIXT.1.1 session-layer admin MsgTypes per `[FIXT §5]`.
namespace fixpp::dict::detail {

inline expected_t<owning_message_handle>
reify_dispatch_fixt(wire::MessageView<wire::access_mode::Index> const& view,
                    std::string_view msg_type,
                    std::pmr::memory_resource* mr) noexcept {
    if (msg_type == "A")  return wrap(fixpp::vt11::owning_Logon::from_view(view, mr));
    if (msg_type == "0")  return wrap(fixpp::vt11::owning_Heartbeat::from_view(view, mr));
    if (msg_type == "1")  return wrap(fixpp::vt11::owning_TestRequest::from_view(view, mr));
    if (msg_type == "2")  return wrap(fixpp::vt11::owning_ResendRequest::from_view(view, mr));
    if (msg_type == "3")  return wrap(fixpp::vt11::owning_Reject::from_view(view, mr));
    if (msg_type == "4")  return wrap(fixpp::vt11::owning_SequenceReset::from_view(view, mr));
    if (msg_type == "5")  return wrap(fixpp::vt11::owning_Logout::from_view(view, mr));
    return std::unexpected{error::dict_reify_unknown_msg_type};
}

}  // namespace fixpp::dict::detail
```

```cpp
// build/<preset>/_codegen/include/fixpp/_dispatch/reify_dispatch_application.hpp
// ~470 cases — application MsgTypes across the four codegen versions
// (v50sp2, v44, v42; vt11 carries no application messages).
namespace fixpp::dict::detail {

inline expected_t<owning_message_handle>
reify_dispatch_application(
    wire::MessageView<wire::access_mode::Index> const& view,
    application_version resolved,
    std::string_view msg_type,
    std::pmr::memory_resource* mr) noexcept {
    using av = application_version;
    switch (resolved) {
    case av::v50sp2: {
        if (msg_type == "D")  return wrap(fixpp::v50sp2::owning_NewOrderSingle::from_view(view, mr));
        if (msg_type == "8")  return wrap(fixpp::v50sp2::owning_ExecutionReport::from_view(view, mr));
        // ... ~118 cases per version ...
        return std::unexpected{error::dict_reify_unknown_msg_type};
    }
    case av::v44: { /* ~95 cases */ }
    case av::v42: { /* ~50 cases */ }
    // Runtime-XML-only versions (v40, v41, v43, v50, v50sp1) hit the default
    // arm and return `dict_reify_unknown_msg_type` — they have no codegen-
    // emitted owning_<Msg> in v1.0.
    default:
        return std::unexpected{error::dict_reify_unknown_msg_type};
    }
}

}  // namespace fixpp::dict::detail
```

The `dict::reify` entry point above (§4.8) drives the two-stage walk: peek MsgType → FIXT admin set test → either `reify_dispatch_fixt` or `dict::resolve_application_version(profile, appl_ver_id_value)` (the free-function form; per C-R2-P2-1) + `reify_dispatch_application`. Both dispatch headers live under the dispatch-shared CMake target `fixpp::dict::dispatch` (§7.6). The `application_version` enum cannot represent vt11 (and is not asked to, post-RC#1); the dispatch axis is `kind` first, application_version second.

Key properties:

- **Per-version, per-message generated.** One `owning_<Msg>` class per typed message under each codegen version's namespace. The generic `owning_message_t<NewOrderSingle>` alias resolves to `owning_NewOrderSingle` via `owning_message_traits` — the rest of the engine writes `fixpp::v50sp2::owning_message_t<NewOrderSingle>` without naming the underlying type.
- **Owns its bytes.** The deep-copy at `dict::reify_as<Msg>(view, mr)` allocates `bytes_` from `mr` (one allocation, sized to `frame_view::bytes().size()`), `memcpy`s the source frame into it, and rebuilds an `OffsetTable` over the owned bytes (up to two more PMR allocations per `[2b §4.4]`).
- **Bound to its `mr`.** The `owning_<Msg>` carries `mr_` so lifetime ends when the user releases the resource. Move-only, copy-deleted.
- **Carries the resolved application version.** `version_v` is `constexpr` per class. The C-ABI `fixpp_owning_msg_t` (per **2i**) borrows this for its runtime version discriminator.
- **Allocation cost.** ≤ 4 PMR allocations per `dict::reify_as<Msg>` (per N-P2-5; §1.2). Bench bar: ≤ 1 µs / 20-tag, ≤ 10 µs / 200-tag (§9 seam #6).
- **Lifetime contract.** Per `[arch §5.5]`: `owning_<Msg>` is an *owning* type, not a flyweight. Move semantics are enabled; copy is deleted. The accessors return view types (`std::string_view`, `wire::group_view<...>`) that alias the owned bytes; `[[clang::lifetimebound]]` chains the warning correctly (the view aliases `*this`'s bytes, not some external buffer). Move is custom `noexcept` (not `= default`); destination caches initialize to `std::nullopt`; source caches are explicitly `reset()`; first `view()` after move rebuilds against post-move `bytes_.data()`. See §6.6 contract bullet 4 and §9 seam #14.


### 4.9 `dict::version_registry` — engine-level dictionary registry

Per N-P2-7. The FIXT.1.1 cross-vocabulary worked example at §6.3 lands per-message-override frames on a *different* application version's metadata than the session's `default_appl`. v1.0 left the cross-`Dictionary` lookup mechanism implicit ("the engine pre-loads all four codegen-version dictionaries at init and the FSM looks up the right one by version byte when needed"); v1.1 publishes the shape so the lookup is part of the documented surface, not implementation detail.

```cpp
// include/fixpp/dict/version_registry.hpp
namespace fixpp::dict {

// Engine-level lookup for cross-version per-message-override resolution.
// The session FSM holds (or reaches) a `version_registry` to obtain the
// `Dictionary` for a per-message-override `application_version` distinct
// from `version_profile.default_appl`. Constructed by the engine at init
// from `EngineConfig::dictionaries`; queried per-message at dispatch time
// when `dict::reify` resolves a per-message ApplVerID(1128).
class version_registry {
public:
    // Returns a borrowed pointer to the Dictionary for the given
    // application_version. Errors with
    // `dict_no_dictionary_for_application_version` if no Dictionary is
    // registered for that version (e.g., the engine config didn't load FIX
    // 4.4 but a FIXT.1.1 message arrived with `ApplVerID(1128)=6`, which
    // resolved successfully to `application_version::v44` via
    // `dict::resolve_application_version` but the registry has no entry for
    // it). Per N2-P3-2: this is a different failure mode from
    // `dict_unknown_appl_ver_id` (which is a parse failure on the wire
    // string); the registry's null-lookup case means the wire string
    // parsed but the engine config didn't load the Dictionary. The returned
    // pointer aliases the registry's storage; lifetime is the registry's
    // lifetime (`[[clang::lifetimebound]]` chains the warning).
    [[nodiscard]] expected_t<Dictionary const*>
        get(application_version v) const noexcept [[clang::lifetimebound]];

    // Engine constructs at init from `EngineConfig::dictionaries`;
    // 2c does not pin the construction shape (engine-owned-by-value vs
    // session-borrowed) — that lives in 2d threading + EngineConfig
    // design (§10 Q10).
};

}  // namespace fixpp::dict
```

**Use.** When `dict::reify(view, profile, mr)` resolves a per-message `ApplVerID(1128)` to a different `application_version` than `profile.default_appl` (Frame 3 in the §6.3 worked example: NOS with `ApplVerID=6` on a FIXT.1.1 session whose `default_appl == v50sp2` — wire value `6` = FIX 4.4 per `[FIXT §5]` and the §4.3 mapping table), the FSM holds the resolved Dictionary via `version_registry::get(application_version)` so the metadata for the per-message-override version is available at dispatch time. The §10 Q10 follow-up confirms the registry's exact ownership model (engine-owned-by-value vs session-borrowed) alongside 2d threading + EngineConfig design.

## 5. Public C ABI

The C-ABI surface for the `dict_*` and `msg_*` symbol families is **delegated to 2i** per `[arch §4.10]`. 2c records the following 2c-side commitments that 2i must honour. **No commitment in this section names `mr` or `std::pmr::memory_resource*`**; the C ABI speaks in C-shape only per `[const §X.2]` (no C++ leakage) and C-P1-4. PMR mapping is 2i's call inside `<fix/c_api.h>`'s implementation; the C surface either uses an engine-owned default arena (with explicit destroy semantics) or accepts an opaque `fixpp_arena_t` handle that 2i internally bridges to PMR.

1. **`fixpp_msg_t` carries a runtime resolved-message-version tag (per RC#1).** Concretely: an opaque `fixpp_msg_t` holds (internally) a wire-side message reference plus the **resolved per-message version**, exposed at the C ABI as the three bytes `(kind_byte, session_byte, application_byte)` (mirroring the C++ `resolved_message_version` of §4.3). Plus version-namespace padding to a stable C-side struct width that is 2i's call. The bytes encode: `kind` (`0` = session_admin, `1` = application), `session` (drawn from `session_version`), `application` (drawn from `application_version`; equal to the unknown sentinel from commitment 5 when `kind == session_admin`). The contract — "every `fixpp_msg_t` is unambiguously associated with one resolved per-message version (which may be a FIXT admin message or an application message at any of the four codegen versions)" — is non-negotiable here. The C-side getters (e.g., `fixpp_msg_version(fixpp_msg_t) → fixpp_resolved_msg_version_t`) and the bit layout are 2i's call (subject to `[const §X.1]` SemVer). For FIXT.1.1 sessions the resolved tag may differ across messages on the same session; the FSM populates it per message per `[FIXT §5.1]` / `[FIXT §5.3]`.

2. **`fixpp_dict_t` is the C-ABI handle for `dict::Dictionary`.** Opaque; constructed via `fixpp_dict_load_from_xml(path, error_out) → fixpp_dict_t*` and `fixpp_dict_apply_overlay(dict, overlay, error_out) → fixpp_dict_t*`. **No PMR resource parameter on the C surface** — the engine owns a default arena keyed to the `fixpp_dict_t`'s lifetime; release happens through `fixpp_dict_destroy(dict)`. 2i confirms the lifetime / refcounting model.

3. **C-ABI accessors mirror the typed-message accessor family.** For each typed message in §4.7, 2i emits a `fixpp_<msg>_<field>(msg, error_out, value_out)` shape (or a generic `fixpp_msg_field_int(msg, tag, error_out, value_out)` family — 2i's call between the named-accessor and tag-keyed flavours). 2c's commitment is that the *underlying `wire::MessageView<Index>` surface* is stable: 2i can wrap it under either accessor shape without 2c churn. The forwarder `field_value(uint16_t tag)` from §4.7.1 supports the tag-keyed flavour for overlay-promoted tags.

4. **`owning_message_t<>` reify exposes a C-ABI handle.** The C-ABI `fixpp_msg_reify(msg, error_out) → fixpp_owning_msg_t*` returns an opaque handle that delegates internally through the version-tag dispatch (the runtime-dispatch switch from §4.8) to the right `owning_<Msg>::from_view(...)` constructor. **No PMR parameter on the C surface** — the engine supplies a default arena (typically the session's long-lifetime arena) keyed to the `fixpp_owning_msg_t`'s lifetime; release happens through `fixpp_owning_msg_destroy(handle)`. 2i owns the `fixpp_owning_msg_t` type, the freeing protocol, and the per-tag accessors over an owning message; whether the engine-owned arena is a global default or a per-session-bound default is 2i's call.

5. **`application_version` enum maps to a C-ABI constant set.** 2i emits the corresponding `FIXPP_APPL_VER_UNKNOWN` (= 0; per N-P3-3 — required so callers can compare against the sentinel that `kind::session_admin` populates into `application_byte`), `FIXPP_APPL_VER_FIX40`, `FIXPP_APPL_VER_FIX41`, `FIXPP_APPL_VER_FIX42`, `FIXPP_APPL_VER_FIX43`, `FIXPP_APPL_VER_FIX44`, `FIXPP_APPL_VER_FIX50`, `FIXPP_APPL_VER_FIX50SP1`, `FIXPP_APPL_VER_FIX50SP2` constants in `<fix/c_api.h>`. Numeric values match `application_version` (`Unknown = 0`, `v40 = 1`, etc.) so the conversion is identity.

   The session-level `session_version` enum is *not* exposed at the C ABI by default — the resolved per-message tag (commitment 1) is the user-relevant value. **2i may expose a session-level version getter at its discretion**; 2c's `version_profile.session` is the value that getter would return. 2c does NOT unilaterally commit 2i to a specific function signature here (per N-P2-2 — refined from v1.0's "if a non-C++ consumer needs the session version, 2i exposes a separate `fixpp_session_version(session) → int` getter" wording, which over-committed 2i without 2i's sign-off). 2i may also expose getters for the resolved-message-version's `kind` byte and either of the version bytes (commitment 1) at its discretion.

6. **Dialect-overlay through C ABI** is symmetric with `XmlLoader::load_overlay`: `fixpp_dialect_overlay_load_from_xml(path, error_out) → fixpp_dialect_overlay_t*`, `fixpp_session_apply_overlay(session, overlay, error_out)` (session-create-time only; mid-session swap is rejected per §7.2 / `[arch §5.6]` / Appendix D §5). The session-create entry point (per **2i**'s session API) takes an optional overlay argument. **No PMR parameter on the C surface** — the engine-owned arena is keyed to the overlay's lifetime; release through `fixpp_dialect_overlay_destroy(overlay)`. 2i confirms the exact entry-point names.

The *C-ABI does not expose `dict::FieldRef`/`ComponentRef`/`GroupRef` directly* — those are C++ struct types per `[const §X.2]` (no C++ leakage through C ABI). C-ABI consumers needing field-metadata introspection (e.g., a generic protocol-aware tool) go through 2i's introspection family (`fixpp_dict_field_type(dict, msg_type, tag, …)`) which is C-typed.

## 6. Behavioral contract

### 6.1 Allocation, exceptions, threading on the hot path

- **Allocation.** Codegen output is `constexpr` static storage; **non-decimal** per-tag accessors allocate nothing (they delegate to `wire::MessageView::get<Tag>()` — allocation-free per `[2b §4.3]` Index mode — plus an allocation-free `field_traits<T>` decode). The **decimal** per-tag accessor is PMR-mandatory (RC#2 / `[const §XX]` v1.4): it takes an explicit `std::pmr::memory_resource* mr` and calls `decimal_t::parse(fv->bytes(), mr)` (`[2a §4.3]`), allocation-free for the default `pod_decimal` trait but potentially arena-allocating for a substituted `FIXPP_DECIMAL_T`; any heap traffic is confined to the caller-supplied per-message arena `[arch §5.2]`, never raw `new`/`delete`, so `[const §VIII.5]` / `[const §XV.1]` hot-path discipline is preserved (the constraint is "no `new`/`delete` between parse and `fromApp`", which arena/PMR allocation satisfies — `[const §VIII.5]`). `Dictionary::field_ref(...)`, `required_fields(...)`, `field_valid_for(...)`, `group_first_field(...)`, `length_pair_data_tag(...)`, `was_dialect_promoted(...)`, `resolve_application_version(...)` are `noexcept` and consult the heap-pinned metadata-handle storage by pointer chase — no allocation. `Dictionary::with_overlay(...)` allocates *once at session open* from the user-supplied PMR (per `[arch §5.2]`); the merge result is heap-allocated but lives on the session's long-lifetime arena. `dict::reify_as<Msg>` and `dict::reify(...)` allocate from the user-supplied PMR (≤ 4 allocations per N-P2-5) at the cross-strand-handoff boundary, which is *outside* the parse → `fromApp` hot path. Hot-path discipline per `[const §VIII.5]` is preserved.
- **Exceptions.** Hot path is exception-free per `[arch §5.3]`. `XmlLoader::load(...)` and `XmlLoader::load_overlay(...)` may throw `dict::xml_parse_error`, `dict::unknown_version_error`, `dict::xml_unsupported_rule_error`, `dict::xml_unsupported_length_pair_error`, or `dict::xml_oom_error` — these are construction-time failures, not hot-path errors, and are the same ergonomic carve-out 2a/2b take. Every other function in this doc is `noexcept`; PMR allocations inside `noexcept` functions are wrapped in `trap_throw` (see §6.1.1).
- **Threading.** `Dictionary` is **move-only-on-init, frozen-after-first-handoff, thread-safe-on-read** (per N-P2-4). Construction (`XmlLoader::load`, `Dictionary::with_overlay`) is single-threaded on the *overlay value* (per N-P2-3); once the `Dictionary` is handed to a session (via `SessionConfig`), it is frozen for the session's lifetime and safe to read concurrently from multiple threads. **Concurrent `with_overlay` calls on the same base `Dictionary` from different threads are safe** — the base's metadata is read-only; ref-count operations on the merged handle's `base_keepalive_` `shared_ptr` are atomic. **Concurrent `with_overlay` calls on the same overlay value are UB** — the overlay's `pmr::vector::push_back` during overlay-build races; treat the overlay value as single-threaded during build (per N-P2-3, refined from v1.0's blanket "single-threaded" prohibition). `table_view` is trivially copyable and value-typed; safe to share across threads as long as the underlying `Dictionary` (and its metadata handle) is alive. `DialectOverlay` and `XmlLoader` are constructed/used in single-threaded contexts (engine init, session open) — no synchronisation guarantees on concurrent use.

  **Typed-message flyweights** (`fixpp::v42::NewOrderSingle` etc.) are read-only and trivially-copy-constructible and address-immutable across copies (per N-P2-5 — refined from v1.0's "trivially copyable" claim, which is false in standard C++ for a class with a reference member: the implicit copy assignment is deleted, and trivial-copyability requires assignment to be trivial). Safe to read concurrently from multiple threads as long as the underlying `wire::MessageView` is alive (across the per-message-arena reset boundary the underlying view is *not* alive — concurrency across the reset boundary requires `dict::reify_as` first to materialize an `owning_<Msg>`; per N-P3-1, refined from v1.0's parenthetical "(which it is not, per `[2b §6.4]`'s lifetime contract — concurrency across the per-message-arena reset boundary requires `dict::reify_as` first)" whose antecedent was ambiguous).

  **`owning_<Msg>` instances are single-strand-only (per N-P1-2 / C-P1-4).** The lazy `view()` cache (the `mutable optional<frame_view>` / `optional<MessageView<Index>>` members of §4.8) materializes on first call and may NOT be accessed concurrently from multiple threads even on a `const owning_<Msg>&`. Documented use is reify-on-strand-A → post to queue → consume-on-strand-B; concurrent reads on the same instance are unsupported. The `dict::reify` path takes the cross-strand reset boundary; the `owning_<Msg>` value is the *handoff*, not a thread-shared object.
- **`thread_local` is prohibited** per `[const §XV]` and `[arch §5.4]`. Codegen output never emits `thread_local`; `Dictionary`, `DialectOverlay`, `XmlLoader` never store `thread_local`; typed-message flyweights have no static state; the runtime-dispatch switch in `Reify.hpp` is a pure `switch` with no static state.

#### 6.1.1 PMR allocation discipline inside `noexcept` functions (`trap_throw`)

Every PMR allocation inside a `noexcept` function in this doc is wrapped in `fixpp::core::detail::trap_throw` per `[2a §4.2]`'s pattern, and translated to a documented `dict::error` variant. Mirrors 2a verbatim. Per RC-5 / C-P2-3.

| Function | PMR allocation site | `dict::error` on failure |
|---|---|---|
| `Dictionary::with_overlay(...) noexcept` | merged-table allocation, overlay-override copy | `dict_overlay_oom` |
| `dict::reify_as<Msg>(view, mr) noexcept` | bytes copy + offset-table rebuild + `owning_<Msg>` member init | `dict_reify_oom` |
| `dict::reify(view, profile, mr) noexcept` | bytes copy + offset-table rebuild + `owning_message_handle` storage | `dict_reify_oom` |
| `owning_<Msg>::from_view(view, mr) noexcept` | bytes + offset table + member init (called from `reify_as`) | `dict_reify_oom` |
| `XmlLoader::load*` (also `load_overlay*`) | DOM build, FieldRef array allocation, name string pool | `dict_xml_oom` (XmlLoader is exception-API by carve-out, but the internal allocator wrap traps `bad_alloc` and re-throws as `dict::xml_oom_error`; `noexcept`-API paths that route through `XmlLoader` internally — none in v1.0 — would translate to `dict_xml_oom`) |

Test seam #16 (per §9) injects PMR allocation failure (a bounded `monotonic_buffer_resource` with a tracking upstream that fails after N bytes) and verifies each entry returns its documented error rather than terminating. Cites `[2a §4.2]` for the pattern.

### 6.2 Latency Tier 1 ceilings

These are bench-harness regression bars (§9 seam #5); CI fails on >5% regression vs the previous tagged release. Targets are on Linux/Clang/x86_64, warm cache, default build.

| Operation | Workload | Ceiling | Notes |
|---|---|---|---|
| `Dictionary::field_ref` | merged dict, MsgType="D", common tag (e.g., 37) | ≤ 30 ns | binary search over per-MsgType FieldRef array |
| `Dictionary::required_fields` | MsgType="D" | ≤ 5 ns | returns a precomputed span |
| `Dictionary::field_valid_for` | MsgType="D", tag=37 | ≤ 25 ns | one binary search |
| `Dictionary::group_first_field` | NoLegs | ≤ 15 ns | flat lookup table over GroupRef |
| `Dictionary::length_pair_data_tag` | RawDataLength | ≤ 15 ns | flat lookup |
| `Dictionary::resolve_application_version` | FIXT.1.1 dict, ApplVerID present | ≤ 20 ns | string parse + enum map |
| Typed accessor — string/int/char (e.g., `NewOrderSingle::cl_ord_id`) | 20-tag message, warm | ≤ 20 ns | inlines into one `OffsetTable::find` (~15 ns per `[2b §6.6]`) + traits dispatch (~5 ns) |
| Typed accessor — decimal (e.g., `NewOrderSingle::price(mr)`) | 20-tag message, warm, default `pod_decimal` trait | ≤ 75 ns | find (~15 ns) + `decimal_t::parse(bytes, mr)` (~50 ns per `[2a §6.5]`) — split per N-P2-2 from the v0.1 unified ≤ 20 ns. PMR-mandatory signature (RC#2 / `[const §XX]` v1.4); allocation-free for `pod_decimal`. Allocating substituted `FIXPP_DECIMAL_T` is outside this bench bar (its cost is the substituted trait's, not 2c's). |
| Typed accessor — `field_value(uint16_t)` runtime-keyed | 20-tag message, runtime tag | ≤ 25 ns | one `OffsetTable::find` + `field_view` construction; no per-type dispatch |
| `Dictionary::with_overlay` | overlay with 50 fields, 5 messages | ≤ 1 ms | one-time merge cost; complexity O(N_base + N_overlay log N_overlay); §1.2 |
| `dict::reify_as<Msg>` (typed) | 20-tag message, fresh PMR | ≤ 1 µs | byte-copy + offset-table rebuild; §1.2 |
| `dict::reify_as<Msg>` (typed) | 200-tag Instrument-heavy | ≤ 10 µs | dominated by byte-copy; §1.2 |
| `dict::reify` (runtime-dispatch, typed payload) | 20-tag, fresh PMR | ≤ 1.2 µs | adds one switch dispatch (~5 ns) over `reify_as`; §9 seam #15 |
| `XmlLoader::load` | FIX50SP2 standard XML, ~1700 fields | ≤ 100 ms | once per engine init; non-hot-path |

### 6.3 Multi-version coexistence

Per `[SYN §3.3 Q12]` and `[FIXT §5]` resolution. Per RC-1.

- **One binary, multiple versions.** A translator/gateway TU may `#include <fixpp/v42/Messages.hpp>` and `#include <fixpp/v50sp2/Messages.hpp>` simultaneously; the four codegen namespaces never collide. Compile cost rises as in §1.2; runtime cost is zero (each version's `constexpr` tables are independent ROM).
- **One `Dictionary` per `Session`, but with a *profile* not a single version byte.** A session is parameterised at open time by exactly one base `Dictionary` whose `version_profile` (§4.3) carries `(session_version, default_appl_version, has_per_message_override)`. For unified pre-FIXT.1.1 sessions (FIX 4.x), `session_version` and `default_appl_version` coincide and `has_per_message_override` is false. For FIXT.1.1 sessions, `session_version == vt11`, `default_appl_version` is the value from `DefaultApplVerID(1137)` at Logon time per `[FIXT §5.1]`, and `has_per_message_override == true` (allowing `ApplVerID(1128)` per message per `[FIXT §5.3]`).
- **Cross-vocabulary dispatch on a single FIXT.1.1 session.** The session FSM walks the resolution algorithm on inbound:

  1. Read `MsgType(35)` from the frame.
  2. If `MsgType` matches one of the 7 FIXT session-vocabulary admin types per `[FIXT §5]` (`Logon`, `Heartbeat`, `Logout`, `TestRequest`, `ResendRequest`, `SequenceReset`, `Reject`), dispatch as `fixpp::vt11::*` (the session-layer namespace).
  3. Otherwise, walk the application-version resolution: read `ApplVerID(1128)` if present and `which().has_per_message_override` is true; if absent, use `which().default_appl_version`. Pass the resolved `application_version` to the dispatch switch (`fixpp::v42::*` / `v44::*` / `v50sp2::*`).

  Worked example: a single FIXT.1.1 session carries the byte stream

  ```
  Frame 1: 8=FIXT.1.1|9=...|35=A|...|1137=9|...|10=...|        (Logon, DefaultApplVerID=v50sp2)
  Frame 2: 8=FIXT.1.1|9=...|35=D|1128=9|55=AAPL|...|10=...|    (NOS, ApplVerID=v50sp2)
  Frame 3: 8=FIXT.1.1|9=...|35=D|1128=6|55=AAPL|...|10=...|    (NOS, ApplVerID=v44 — per-message override; per `[FIXT §5]` wire value `6` = FIX 4.4)
  Frame 4: 8=FIXT.1.1|9=...|35=F|55=AAPL|...|10=...|           (OrderCancelRequest, no ApplVerID; uses default v50sp2)
  Frame 5: 8=FIXT.1.1|9=...|35=0|...|10=...|                   (Heartbeat — FIXT session vocabulary)
  ```

  Resolution per the algorithm above (each frame produces a `resolved_message_version` value before dispatch — per RC#1):

  - Frame 1 (`MsgType=A`) — FIXT admin → `resolved_message_version{kind::session_admin, vt11, Unknown}` → dispatch through `reify_dispatch_fixt.hpp` → `fixpp::vt11::Logon`.
  - Frame 2 (`MsgType=D`, `ApplVerID=9`) — application; `resolve_application_version(profile, "9")` → `v50sp2` (per `[FIXT §5]` wire value `9` = FIX 5.0 SP2) → `resolved_message_version{kind::application, vt11, v50sp2}` → dispatch through `reify_dispatch_application.hpp` → `fixpp::v50sp2::NewOrderSingle`.
  - Frame 3 (`MsgType=D`, `ApplVerID=6`) — application; `resolve_application_version(profile, "6")` → `v44` (per-message override; per `[FIXT §5]` wire value `6` = FIX 4.4 — see the wire→C++ enum mapping table in §4.3) → `resolved_message_version{kind::application, vt11, v44}` → `fixpp::v44::NewOrderSingle`.
  - Frame 4 (`MsgType=F`, no `ApplVerID`) — application; `resolve_application_version(profile, "")` → `v50sp2` (session default per `profile.default_appl`) → `resolved_message_version{kind::application, vt11, v50sp2}` → `fixpp::v50sp2::OrderCancelRequest`.
  - Frame 5 (`MsgType=0`) — FIXT admin → `resolved_message_version{kind::session_admin, vt11, Unknown}` → `fixpp::vt11::Heartbeat`.

  All five frames travel on the same `Session` with the same `Dictionary`. The `Dictionary` for a FIXT.1.1 session carries the FIXT session-layer FieldRef tables (the 7 admin types) merged with the `default_appl_version` application-layer tables; per-message override frames consult a *different* application version's metadata, which the FSM resolves via a `dict::version_registry` (§4.9) holding the four codegen-version dictionaries. The `dict::reify(view, profile, mr)` runtime variant performs the same resolution on the cross-strand-handoff path; for typed-known callers, the FSM constructs the right `fixpp::vXX::*` flyweight directly without going through `dict::reify`.

- **No implicit cross-version conversion.** `fixpp::v42::NewOrderSingle` and `fixpp::v50sp2::NewOrderSingle` are different types. A translator that converts FIX 4.2 → FIX 5.0 SP2 builds an explicit converter (read fields from one, write fields into a `Writer` in the other version's namespace, then `commit()`). 2c does not generate cross-version converters; this is application-specific, and a candidate for a v1.x utility.

### 6.4 Dialect-overlay additive merge contract

Per `[SYN §3.3 Q13]`:

- **Additive within the v1.0 grammar closure (§4.4.1).** An overlay extends the base dictionary with new fields, new MsgTypes, new components within the closure. Conditional rules and Length+Data pairs are rejected with documented errors; per RC-6.
- **Override-permitting (with policy).** An overlay may override a base field's `presence` rule for a specific MsgType (e.g., promote tag 109 `ClientID` from Optional to Required on a venue-specific NewOrderSingle); the conflict policy `OverlayWins | BaseWins | Reject` (§4.4) decides what happens on override collision. Default `OverlayWins`.
- **Merged once, frozen.** `Dictionary::with_overlay(...)` produces a new `Dictionary` with the merged tables; the original base is unchanged. The merged result is frozen for the session's lifetime per `[arch §5.6]`. Mid-session swap is rejected categorically in v1.0 (§7.2; Appendix D §5).
- **Lifetime relative to base.** The merged `Dictionary` borrows the base's metadata-handle storage via `shared_ptr` (sharing path; §6.5); the base outlives the merged dictionary as long as the merged dictionary is alive, regardless of whether the user has dropped their handle to the base. Additive entries are PMR-allocated into the merged metadata handle.
- **Promotion semantics for D-009 / COM-011.** A "previously-unknown custom" tag (a tag that the base dictionary has no `FieldRef` for, but that the overlay declares) is promoted: after merge, `Dictionary::field_valid_for(msg_type, custom_tag)` returns `true`; `Dictionary::was_dialect_promoted(custom_tag)` returns `true`; the typed accessors codegen'd from the *base* version do not include the named accessor (codegen ran against the standard XML, not the overlay), and runtime access via the typed `nos.field_value(custom_tag)` forwarder (§4.7.1) succeeds. Users wanting fully typed accessors regenerate the codegen against the venue XML (§4.7.1).
- **Conflict-policy granularity.** The conflict policy applies per-field, not per-overlay-as-a-whole. An overlay with five overrides under `OverlayWins` policy applies all five; a single conflicting override under `Reject` rejects the entire merge with `error::dict_overlay_conflict`. `BaseWins` silently drops the overlay's overrides while keeping its additions; useful for advisory overlays (telemetry-only dialect awareness).
- **Asymptotic complexity:** O(N_base + N_overlay log N_overlay) where the `O(N_base)` term is the metadata-handle clone-or-share cost (constant if `shared_ptr` is taken to the base; linear otherwise) and the `log N_overlay` term comes from sorted-merge of overlay additions into per-MsgType `FieldRef` arrays. Per N-P3-3.

### 6.5 Lifetime contract on typed-message flyweights

Inherited from `[2b §6.4]` and `[arch §5.5]`, plus the metadata-handle lifetime root (§4.3 / RC-3):

- A typed-message flyweight (e.g., `fixpp::v50sp2::NewOrderSingle`) holds a `wire::MessageView<Index>` by reference; the view aliases the originating frame buffer; the buffer's lifetime is the per-message arena's slot, reset by the session FSM after `fromApp` returns.
- Capturing a typed-message flyweight past `fromApp` return is undefined in release; debug builds trap via `[2b §6.4]`'s generation-counter mechanism (the typed accessor calls flow through `wire::MessageView::get<Tag>()` which calls `View::check_alive()`).
- The supported escape hatch is `dict::reify_as<Msg>(view, mr) → owning_message_t<Msg>` (§4.8); deep-copies the bytes plus offset table into caller-owned PMR storage, returns a value-typed owning copy that can be moved across thread/strand boundaries.
- `[[clang::lifetimebound]]` is on every view-returning accessor; codegen emits the attribute unconditionally; per `[arch §5.5]`. GCC honours it on parameters; MSVC ignores (accepted gap per `[const §IX.4]`).
- **Metadata-handle lifetime root (RC-3 / RC#2 / N-P1-1 / C-R2-P1-1).** The `Dictionary`'s metadata block lives on the heap behind `dict::detail::dict_metadata_handle_ptr` — a `shared_ptr<const dict_metadata_handle>` allocated via `std::allocate_shared` over a `std::pmr::polymorphic_allocator<dict_metadata_handle>` so the shared-control-block deallocator returns memory to the originating PMR resource. The metadata block's lifetime is the union of all `Dictionary::handle_` and `dict_metadata_handle::base_keepalive_` references that share the same control block. The merged-handle's `base_keepalive_` slot carries a *copy* of the base's `handle_` on the `with_overlay` sharing path (one `shared_ptr<const dict_metadata_handle>`, refcount = 2 after `with_overlay` returns; refcount → 1 after the user drops the base; refcount → 0 when the merged dict is finally destroyed). The block's address is stable across `Dictionary` moves; `table_view`s and `wire::dictionary_driven_validator` instances aliasing it remain valid after a `Dictionary` move into `SessionConfig`. The `with_overlay` sharing path keeps the base's metadata block alive for the merged `Dictionary`'s lifetime, so a user who constructs `auto base = XmlLoader::load(...); auto merged = base.with_overlay(...);` then drops `base` still has a working `merged`. Test seam #13 verifies the move + outstanding `table_view` invariant + the drop-base-keep-merged refcount path.

### 6.6 `owning_message_t<>` + `dict::reify` bridge contract

Per RC-2:

1. **Two entry points.**
   - **`dict::reify_as<Msg>(view, mr) noexcept`** — the typed entry point. Caller knows `Msg` at compile time. Returns `expected_t<owning_message_t<Msg>>`.
   - **`dict::reify(view, profile, mr) noexcept`** — the runtime-dispatch entry point. Caller has only a type-erased view + version_profile. Returns `expected_t<owning_message_handle>`.
   Both live in `<fixpp/dict/reify.hpp>` (a 2c-owned header). The bare `MessageView::reify(mr)` reference in `[2b §6.6]` is a documented synonym for `dict::reify(view, profile, mr)`; the `wire::MessageView` class itself is not retroactively modified.

2. **Allocation discipline (re-derived per N-P2-4).** ≤ 4 PMR allocations from `mr` per `dict::reify_as<Msg>`, itemised against the §4.8 `owning_<Msg>` declaration:
   - (1) `bytes_` `pmr::vector<std::byte>` storage (the deep-copied frame).
   - (2) The `unique_ptr<wire::OffsetTable, pmr_deleter>` for the `OffsetTable` *object itself* (`sizeof(OffsetTable)` — previously invisible in v1.0 prose).
   - (3) The `OffsetTable`'s entry array (per `[2b §4.4]`).
   - (4) The `OffsetTable`'s hash overlay (per `[2b §4.4]`; possibly fused with #3).
   The runtime-dispatch variant `dict::reify` may add one more allocation if `owning_message_handle` is heap-backed (small-buffer-optimised variant elides). No allocation outside `mr`.

3. **Version tagging.** The returned `owning_message_t<Msg>` carries `version_v` as a `constexpr` member (the resolved application version for the message class). `which()` returns it. The runtime-dispatch `owning_message_handle::version()` returns the resolved-message-version (`resolved_message_version` per RC#1) — the value the two-stage dispatch matched on. C-ABI's `fixpp_owning_msg_t` carries the resolved-message-version tag (§5 commitment 1).

4. **Lifetime.** Bounded by `mr`'s lifetime. The owning value is move-only; copy is deleted; move transfers all PMR allocations. Per the lazy-view design (§4.8 / N-P1-3 / N-P1-2), the move is **custom `noexcept`** (not `= default`) — the destination's `frame_cache_` / `view_cache_` `optional`s are constructed empty and the source's are explicitly `reset()`. `view()` is computed on access; first call after move rebuilds the cache against the post-move `bytes_.data()`. **Single-strand-only** discipline applies — the cache write is not synchronized; concurrent readers on the same instance are UB (per N-P1-2). §9 seam #14 verifies post-move accessor correctness.

5. **Re-validate on the receiver side?** The reified copy preserves the original bytes verbatim; if the original was `wire::Validator`-passed before reify, it remains valid (no fields changed). If a receiver wants to re-validate (e.g., suspicious of in-flight corruption between strands — extremely unlikely in-process, but a paranoid pattern), `wire::Validator::validate(reified.view(), scratch_mr)` is callable again; the receiver supplies a fresh scratch `mr`.

6. **Errors (per RC#1).** `dict::reify_as<Msg>` returns `expected_t<owning_message_t<Msg>>`; failures: `dict_reify_oom` (PMR), `dict_reify_msg_type_mismatch`. **`dict_reify_version_mismatch` is dropped** from this entry point (the caller named `Msg::version_v` by picking `Msg`; mismatch is caller bug, not engine validation; `wire::MessageView` carries no resolved-version inputs). `dict::reify(...)` adds `dict_reify_unknown_msg_type` (no codegen-emitted `owning_<Msg>` for the resolved-version + MsgType pair — e.g., a runtime-XML-only version), `dict_unknown_appl_ver_id` (per-message ApplVerID parse failed), and `dict_unresolved_application_version` (FIXT.1.1 session never set DefaultApplVerID and the inbound message lacks `ApplVerID(1128)`).

7. **Cross-strand safety.** Once constructed, the `owning_message_t<Msg>` (or `owning_message_handle`) is safe to `std::move` across thread/strand boundaries. The receiving thread's accessors return views aliasing the owning copy's bytes; `[[clang::lifetimebound]]` on `*this` chains the lifetime warning correctly.

8. **Runtime-dispatch table generation (per RC#1).** Two dispatch headers live under `_codegen/include/fixpp/_dispatch/` and are auto-generated alongside the typed messages: `reify_dispatch_fixt.hpp` (7 cases — the FIXT admin set `0`/`1`/`2`/`3`/`4`/`5`/`A`) and `reify_dispatch_application.hpp` (~470 cases — one per (application_version, MsgType) pair across the four codegen versions; runtime-XML-only versions hit a default arm and return `dict_reify_unknown_msg_type`). Total dispatch-shared cost is ~50 KiB once per all-versions TU (per N-P2-6). `dict::reify`'s two-stage walk (FIXT-admin test → application resolution + dispatch) lives in the `fixpp::dict::dispatch` CMake target (§7.6).

### 6.7 Errors introduced by this design

Per `[2b §6.7]`'s pattern, 2c collects every new `fixpp::core::error` variant it adds so **2i** has a stable C-ABI mapping target and `[const §X.4]` forwards-compat applies against a known list.

| `fixpp::core::error` variant | Source section | Remediation class |
|---|---|---|
| `dict_unknown_version` | §4.5 — `XmlLoader::load` rejects an XML with a `BeginString` outside the v1.0 supported set | configuration error — fix XML or upgrade to the v1.x supporting that version |
| `dict_unknown_appl_ver_id` | §4.3 / §6.3 — `Dictionary::resolve_application_version` got a non-empty `ApplVerID` value that doesn't parse to a known `application_version` | configuration error — counterparty sent unsupported ApplVerID |
| `dict_unresolved_application_version` | §4.3 / §6.3 / §6.6 — `Dictionary::resolve_application_version` was asked for a version on a FIXT.1.1 dictionary whose `default_appl == Unknown` AND the inbound message lacked `ApplVerID(1128)` (per RC#1; replaces the v1.0 `Unknown` success-sentinel path) | configuration error — Logon must set `DefaultApplVerID(1137)`, or per-message `ApplVerID(1128)` must be supplied, or session config must supply a fallback |
| `dict_no_dictionary_for_application_version` | §4.9 — `version_registry::get(application_version)` was given a valid `application_version` but the registry has no `Dictionary` registered for it (the wire `ApplVerID` parsed successfully but the engine config didn't load that version's Dictionary) | configuration error — engine operator must add the version to `EngineConfig::dictionaries`. Per N2-P3-2; distinct from `dict_unknown_appl_ver_id` (which is a parse failure on the wire string) |
| `dict_xml_parse_failed` | §4.5 — `XmlLoader::load*` rejects malformed XML | configuration error — fix XML |
| `dict_xml_schema_violation` | §4.5 / §4.4.2 — XML parses but violates QuickFIX schema (missing `<fields>`, unknown attribute, cyclical component reference, etc.) | configuration error — fix XML or schema |
| `dict_xml_too_large` | §4.4.2 — XML byte size exceeds `FIXPP_DICT_XML_MAX_BYTES` cap | configuration error — split or sanity-check XML |
| `dict_xml_too_deep` | §4.4.2 — XML nesting exceeds `FIXPP_DICT_XML_MAX_DEPTH` cap | configuration error — fix XML |
| `dict_xml_oom` | §4.5 / §6.1.1 — `XmlLoader::load*` PMR allocation failed (trapped via `[2a §4.2]` `trap_throw`) | runtime error — caller arena exhausted |
| `dict_overlay_conflict` | §4.4 / §6.4 — `with_overlay` rejects a collision under `Reject` policy | configuration error — reconcile overlay vs base |
| `dict_overlay_too_large` | §1.2 / §4.4.2 — overlay has > N entries (DoS guard against pathological overlays) | configuration error — split or sanity-check overlay |
| `dict_overlay_unsupported_rule` | §4.4.1 / RC-6 — overlay declares a conditional-required rule (out-of-grammar in v1.0) | configuration error — drop conditional rule or regenerate codegen |
| `dict_overlay_unsupported_length_pair` | §4.4.1 / RC-6 — overlay declares a Length+Data pair (out-of-grammar in v1.0) | configuration error — regenerate codegen against custom XML for typed access |
| `dict_overlay_oom` | §4.3 / §6.1.1 — `with_overlay` PMR allocation failed | runtime error — caller arena exhausted; expand `mr` |
| `dict_field_not_in_version` | §4.4 / §6.4 — overlay references a base FIX version that does not declare the field's enclosing component | configuration error — pick correct base version |
| `dict_msg_type_not_in_version` | §4.4 / §6.4 — overlay references a MsgType not in the base version (when promoting an existing message's grammar) | configuration error — pick correct base version or include the message in the overlay |
| `dict_length_pair_collision` | §4.4 — overlay's Length+Data pair conflicts with a base pair (only fires if a future v1.x grammar relaxation re-admits the pair declaration; v1.0 rejects via `dict_overlay_unsupported_length_pair` first) | configuration error — fix overlay |
| `dict_reify_oom` | §6.6 / §6.1.1 — `reify*(...)` PMR allocation failed | runtime error — caller arena exhausted; expand `mr` |
| `dict_reify_msg_type_mismatch` | §6.6 — `dict::reify_as<Msg>` got a view whose `MsgType` doesn't match `Msg::msg_type_v` | bug — caller picked wrong `Msg` |
| `dict_reify_unknown_msg_type` | §6.6 — `dict::reify` runtime-dispatch found no codegen-emitted `owning_<Msg>` for the resolved-version + MsgType pair (typically a runtime-XML-only version) | configuration error — caller cannot reify a runtime-XML-only message; use `view().get(uint16_t tag)` instead |

(20 variants. The v0.1 `dict_table_view_stale` is **dropped** per RC-3 / C-P2-6 — stale `table_view` access is treated like stale `wire::View` per `[2b §6.4]`: debug trap via generation counter, release UB, NOT a recoverable error. The v1.0 `dict_reify_version_mismatch` is **dropped** per RC#1 / C-P2-3 — `reify_as<Msg>`'s caller named `Msg::version_v` explicitly; mismatch is caller bug, not engine validation; `wire::MessageView` carries no resolved-version inputs in any case. New variants in v1.1: `dict_unresolved_application_version` (per RC#1 / C-P1-5; replaces the v1.0 `Unknown` success sentinel). New variant in v1.2: `dict_no_dictionary_for_application_version` (per N2-P3-2; the registry's null-lookup case for `version_registry::get`, distinct from the parse-failure variant `dict_unknown_appl_ver_id`). Variants added in v1.0 per RC-1 / RC-2 / RC-5 / RC-6 retained: `dict_unknown_appl_ver_id`, `dict_xml_oom`, `dict_overlay_unsupported_rule`, `dict_overlay_unsupported_length_pair`, `dict_overlay_oom`, `dict_reify_msg_type_mismatch`, `dict_reify_unknown_msg_type`, `dict_xml_too_large`, `dict_xml_too_deep`. 2i confirms the C-ABI mapping. Following 2a/2b's coalescing pattern: configuration errors → `FIXPP_ERR_DICT_CONFIG`; capacity → `FIXPP_ERR_DICT_LIMIT_EXCEEDED`; runtime allocation → reuses `FIXPP_ERR_OOM`. Final coalescing is 2i's call.)

## 7. Integration with adjacent modules

### 7.1 Wire (`[arch §4.3]`, owner **2b**)

Three integration points, plus a grammar-closure note for the Length+Data dialect path.

1. **Typed-message classes consume `wire::MessageView<Index>`.** Every generated typed message in `fixpp::vXX::*` is constructed from a `wire::MessageView<Index>`; the per-tag accessors delegate to `MessageView::get<Tag>()` and `group<NoTag, GroupT>()`; the `field_value(uint16_t)` forwarder delegates to `MessageView::get(uint16_t)`. 2c does not re-implement parse/serialize; it generates *typed shells* over 2b's primitives. Per `[2b §7.2]`.

2. **`dict::table_view` flows into `wire::dictionary_driven_validator`.** 2b's `dictionary_driven_validator` (per `[2b §4.6]`) holds a `dict::table_view` by value. 2c provides `Dictionary::as_table_view()` (§4.3); the session FSM passes the value at validator construction. Since `table_view` is value-typed and trivially copyable (§4.6) and the metadata-handle lifetime root is heap-pinned (§4.3 / RC-3), there is no virtual `wire/` → `dict/` runtime edge — `dictionary_driven_validator` consults the table by value-typed call without indirection through a virtual dictionary interface, and `Dictionary` moves do not invalidate the validator.

3. **`dict::reify` bridge satisfies `[2b §6.6]`'s view-escape contract.** The bare `MessageView::reify(mr)` reference 2b makes is satisfied by 2c-published free function templates in `<fixpp/dict/reify.hpp>` (§4.8 / RC-2). 2b's `wire::MessageView` class is not retroactively modified.

4. **Length+Data static table extension.** 2b's `field_iterator` (`[2b §4.3]`) uses a static `constexpr` table of FIX-standard Length+Data tag pairs to skip SOH bytes inside `data`-typed fields without consulting the runtime dictionary (Iter mode is dict-free by design). 2c **owns the source of truth for that table**: the per-version `Fields.hpp` (codegen versions only) includes a `constexpr std::array<length_pair, N>` derived from the standard XML's `<field type="LENGTH" />` declarations and their paired `<field type="DATA" />` neighbours. 2b's `field_iterator` `#include`s the FIX 5.0 SP2 version's table (the most permissive, since FIX 5.0 SP2 is a superset of 4.4/4.2 for Length+Data pairs by `[FIX50SP2 §3.3]`).

   **Dialect-overlay grammar closure (per RC-6).** Per §4.4.1, overlay XML containing a Length+Data pair declaration is rejected with `dict_overlay_unsupported_length_pair`. There is *no* runtime path to extend `field_iterator`'s static table at session open; Iter mode (tap, async logger) is documented as standard-FIX-only for Length+Data semantics. Index mode does consult the runtime `Dictionary::length_pair_data_tag(...)` for parsing, so a hypothetical future v1.x grammar relaxation that re-admits the pair declaration would still need `field_iterator`'s static table to be regenerated for Iter-mode consumers — which is a recompile, not a runtime concern.

   **Reverse direction — codegen output of dialect-promoted Length+Data pairs.** Users who need typed access to dialect-private Length+Data pairs *and* Iter-mode skipping for those pairs regenerate `Messages.hpp`/`Fields.hpp` against their dialect's XML using `fixpp-codegen` (the build-time host tool per `[const §III.5]`). The regenerated `Fields.hpp` carries the dialect's pair table; downstream Iter-mode TUs `#include` the regenerated header instead of the standard one. This is a *recompile-against-dialect-XML path*, not a runtime-overlay path; it is the documented v1.0 escape hatch for users with pair-extension requirements. Tracked in §10 with the v1.x grammar-extension question.

### 7.2 Session (2d, 2e — to be drafted)

- **Per-session `Dictionary`.** `SessionConfig` carries one `dict::Dictionary` value (per `[arch §4.4]`); the session's `Parser`/`Writer`/`Validator` consume it through `Dictionary::as_table_view()`.
- **Mid-session swap categorically rejected (per N-P2-6 / RC).** 2c v1.0 does **not** expose any `Session::swap_dialect_overlay(...)` or `swap_dictionary(...)` API. The supported pattern: `Listener::open(SessionConfig)` includes the overlay at session-create time; mid-session change closes-and-reopens. Appendix D §5 drafts the corresponding `[arch §5.6]` amendment to drop "dialect overlay swap" from the list of supported mutating ops. 2d's threading contract confirms there is no mid-session swap point.
- **MessageStore consumes raw frames, not typed messages.** Per `[2a §7.1]` v0.3 and `[2b §7.4]`, MessageStore journals raw bytes; 2c's typed messages are not the persistence shape. Typed messages are constructed *on the fly* in `fromApp` callbacks; `dict::reify_as<Msg>` is the supported "carry past the strand" path.

### 7.3 C ABI (`[arch §4.10]`, owner **2i**)

Summary of the §5 commitments (full text in §5):

- `fixpp_msg_t` carries a runtime resolved-message-version tag (per `[FIXT §5.3]` resolution).
- `fixpp_dict_t`, `fixpp_dialect_overlay_t`, `fixpp_owning_msg_t` are 2i-owned opaque handles wrapping the C++ types defined here.
- C-ABI accessor families wrap the typed-message accessor surface; 2i's choice between named (`fixpp_neworder_single_cl_ord_id`) and tag-keyed (`fixpp_msg_field_string(msg, 11, ...)`) shapes is independent of 2c.
- C-ABI surface speaks in C-shape only — engine-owned default arenas (with explicit destroy semantics) instead of PMR resource parameters.

### 7.4 Service (2j, control plane)

The control plane may **swap a session's dictionary at session-create time only** per `[arch §5.6]` and §6.3. Concretely: the gRPC `OpenSession` request (per `[arch §8.1]`) carries the FIX version and an optional `DialectOverlay` reference (a path or in-line XML); the service resolves them against engine-loaded `Dictionary` instances and constructs the per-session `Dictionary` before the session is exposed. The control plane does not expose a "change dictionary on a live session" RPC.

### 7.5 SWIG / Python (`[arch §4.12]`, owner **2m**)

- **Typed-message exposure shape.** Per-message classes in `fixpp::vXX::*` (codegen versions only) are SWIG-wrapped one-to-one. The Python side sees `fixpp.v50sp2.NewOrderSingle`, `fixpp.v42.NewOrderSingle`, etc., as distinct classes under distinct submodules. The version namespace maps to a Python submodule. Runtime-XML-only versions (FIX 4.0/4.1/4.3/5.0/5.0SP1) are exposed only through the runtime `Dictionary` + tag-keyed accessor wrappers — no Python typed classes.
- **Version tag.** `fixpp.v50sp2.NewOrderSingle.version` returns `dict.application_version.v50sp2` (Python enum); cross-version polymorphism in Python uses an `isinstance` dispatch on the typed class, with the version tag accessible for tooling.
- **Owning-message `reify`.** SWIG wraps the C-ABI `fixpp_msg_reify` family per **2i**; the Python wrapper hides the engine-owned arena handling.
- **Dialect overlay in Python.** SWIG wraps `XmlLoader::load_overlay` as `fixpp.dict.load_overlay(path)` returning a Python-side `DialectOverlay` object that flows into `fixpp.session.SessionConfig`.

### 7.6 CMake target shape (per C-P3-3)

The codegen-version namespaces are independently linkable interface targets so a downstream consumer that only wants FIX 4.4 does not pull in v50sp2's headers (and pays no compile-time cost for them). Per C-P3-3 (escalated to P2 by the Opus pass).

| Target | Installs |
|---|---|
| `fixpp::dict::v42` | `_codegen/include/fixpp/v42/{Messages,Fields,Validator,Reify,NormativeReferences}.{hpp,md}` |
| `fixpp::dict::v44` | `_codegen/include/fixpp/v44/{Messages,Fields,Validator,Reify,NormativeReferences}.{hpp,md}` |
| `fixpp::dict::v50sp2` | `_codegen/include/fixpp/v50sp2/{Messages,Fields,Validator,Reify,NormativeReferences}.{hpp,md}` |
| `fixpp::dict::vt11` | `_codegen/include/fixpp/vt11/{Messages,Fields,Validator,Reify,NormativeReferences}.{hpp,md}` |
| `fixpp::dict::all_versions` | umbrella `INTERFACE` target depending on all four above; convenience for translator/gateway TUs |
| `fixpp::dict::runtime` | the runtime dictionary surface (`Dictionary`, `XmlLoader`, `DialectOverlay`, `table_view`, `version_profile`, the `dict::reify` bridge); independent of any per-version codegen target; works with any of the 9 v1.0-supported versions for runtime XML loading |
| `fixpp::dict::dispatch` | the runtime-dispatch switch headers (`_codegen/include/fixpp/_dispatch/reify_dispatch_fixt.hpp` for the 7 FIXT admin MsgTypes + `_codegen/include/fixpp/_dispatch/reify_dispatch_application.hpp` for the ~470 application (version, MsgType) pairs); depends on `fixpp::dict::all_versions` because the switch references all four codegen versions' `owning_<Msg>` types |

A consumer that only wants FIX 4.4 typed messages depends on `fixpp::dict::v44` + `fixpp::dict::runtime`. A consumer that wants the runtime-dispatch `dict::reify` (e.g., a session FSM that handles cross-vocabulary FIXT.1.1) depends on `fixpp::dict::dispatch` (which transitively pulls in the four codegen versions). A consumer that wants only runtime-XML-loaded dictionaries (e.g., a tooling utility for FIX 4.0) depends only on `fixpp::dict::runtime`.

Each target carries the appropriate `INTERFACE_INCLUDE_DIRECTORIES` (pointing into the build tree's `_codegen/include/`) and an empty `INTERFACE_LINK_LIBRARIES` (header-only). Doxygen / IDE / packaging tooling discovers headers through the targets' `INTERFACE_INCLUDE_DIRECTORIES`.

## 8. PMR — recap

Three storage classes for 2c-owned data, all rooted in heap-pinned metadata-handle storage where lifetime survives `Dictionary` moves (per RC-3):

| Storage | Lifetime | Holds | Reset by |
|---|---|---|---|
| Per-version `constexpr` tables (`Fields.hpp`, `Validator.hpp`, `Reify.hpp` static parts) | static (program lifetime) | per-codegen-version `FieldRef`/`ComponentRef`/`GroupRef` arrays, conditional-rule tables, Length+Data pair tables, required-field sets | never (static storage) |
| `dict::detail::dict_metadata_handle` (heap-pinned via `dict_metadata_handle_ptr` = `shared_ptr<const dict_metadata_handle>`, allocated via `std::allocate_shared` over `std::pmr::polymorphic_allocator<dict_metadata_handle>` so the shared-control-block deallocator returns memory to the originating `mr`; `with_overlay` results carry a *copy* of the base's `handle_` in the merged handle's `base_keepalive_` slot, sharing the base's control block — RC#2 / N-P1-1 / C-R2-P1-1. `pmr_deleter<T>` is published in §4.3 as a generic PMR-aware deleter utility; the live ownership shape for `dict_metadata_handle` is `shared_ptr`-based.) | session lifetime (or longer if shared via `with_overlay`'s `base_keepalive_`) | merged `FieldRef`/`ComponentRef`/`GroupRef` views (spans into `constexpr` for codegen versions, PMR copies for runtime-XML versions), dialect-overlay overrides, additive entries, `version_profile`, `mr_` (owning resource), optional `base_keepalive_` | last `shared_ptr<const dict_metadata_handle>` reference (across all `Dictionary::handle_` and `dict_metadata_handle::base_keepalive_` slots that share the same control block) is destroyed; the control block deallocator returns memory to the originating `mr` |
| `owning_message_t<>` storage | caller `mr` lifetime | one `pmr::vector<std::byte>` (the deep-copied frame); offset-table entry array + hash overlay (≤ 2 PMR allocations, ≤ 4 total per N-P2-5) | caller resets `mr` |

- **Codegen output is `constexpr`** → static storage, zero allocation, no `new`/`delete` ever (`[const §VIII.5]`).
- **Metadata-handle ownership is the lifetime root (per RC-3 / RC#2 / N-P1-1 / C-R2-P1-1).** Constructed by `XmlLoader::load(...)` — allocates the `dict_metadata_handle` on the user-supplied `mr` via `std::allocate_shared<dict_metadata_handle>(std::pmr::polymorphic_allocator<dict_metadata_handle>{mr}, ...)` so the shared-control-block deallocator returns memory to the originating `mr`. Fills it with span references for codegen versions or PMR copies for runtime-XML versions. `with_overlay(...)` allocates a fresh `dict_metadata_handle` on the user-supplied `mr` (the merged dict's `mr`) via the same `std::allocate_shared` mechanism, and stores a *copy* of the base's `handle_` (the same `shared_ptr<const dict_metadata_handle>`, sharing the base's control block) in the merged handle's `base_keepalive_` slot. The base may go out of scope while the merged dict still holds the keepalive (the control block's refcount drops from 2 to 1, not to 0). Held by value in `SessionConfig` through the `Dictionary` owning the handle. The user supplies the `mr` at every construction step; the metadata block's address survives `Dictionary` moves so `table_view` and `dictionary_driven_validator` stay valid.
- **`DialectOverlay` storage.** Pinned to a non-null PMR resource at construction (per C-P2-5; via `DialectOverlay::create(mr)` or `XmlLoader::load_overlay(path, mr)`); lifetime = `DialectOverlay` value's lifetime (typically constructed at session open, consumed by `Dictionary::with_overlay(...)`, then dropped if the merged dictionary is the only retained handle).
- **`owning_message_t<>` storage.** Caller-supplied PMR per `dict::reify_as<Msg>(view, mr)`; the value carries `mr` and frees on move-end. ≤ 4 PMR allocations per N-P2-5. PMR allocation failure inside the `noexcept` reify path is wrapped in `trap_throw` and surfaces as `dict_reify_oom` per §6.1.1 / RC-5.
- **Decimal typed-accessor PMR (RC#2 / `[const §XX]` v1.4).** The decimal typed accessor is the one accessor that is *not* unconditionally allocation-free: the merged 2a parse entry point `decimal_t::parse(span, mr)` (`[2a §4.3]`, over `decimal_traits<FIXPP_DECIMAL_T>::from_chars`, `[2a §4.2]`) is PMR-mandatory. The accessor takes an explicit `std::pmr::memory_resource* mr`; on the borrowed flyweight the caller passes the per-message arena `[arch §5.2]` (the same arena the underlying `MessageView` aliases per `[2b §6.4]`); on `owning_<Msg>` it defaults to the instance's owned `mr_` (the §4.8 caller-`mr`-lifetime storage class above). This `mr` is **not** a 2c-owned storage class — it is the wire/caller-owned per-message arena — so it adds no row to the table; it is recorded here so the recap is not read as claiming the decimal arm is zero-alloc. For the default `pod_decimal` trait the parse ignores `mr` and is allocation-free; an allocating substituted `FIXPP_DECIMAL_T` draws from `mr` per call. No raw `new`/`delete`; `[const §VIII.5]` / `[const §XV.1]` preserved (arena/PMR is the sanctioned mechanism for the rare materialise case, `[const §XV.1]`).
- **Banned: `thread_local`.** Per `[const §XV]` and `[arch §5.4]`. Codegen never emits `thread_local`; runtime types never allocate via `thread_local`. Trace context (per `[const §XIII.3]`) is strand-stored, not `thread_local`; 2c is not involved.

## 9. Test seams

Per `[arch §10]` requirement (4) and `[const §VII]`: every design doc ends with the test seams it exposes. v1.0 expands beyond v0.1's 15 to cover the new error paths and lifetime invariants.

1. **Conformance corpus — every supported codegen version × every owned message round-trips.** `tests/codegen/conformance/` holds (a) public FIX corpora (QuickFIX `examples/*.dat`, public exchange specifications' sample messages, ICAP regression set) for FIX 4.2, 4.4, 5.0 SP2, FIXT.1.1 session messages; (b) parameterised GTests that parse each corpus message into the appropriate typed class, exercise every per-tag accessor, reify via `dict::reify_as<Msg>`, and re-serialize. Catches codegen-template regressions, accessor mismatches, version-namespace collisions. Runtime-XML-only versions (4.0/4.1/4.3/5.0/5.0SP1) get a separate seam (#10c).
2. **Compile-time cost regression.** Bench harness measures the wall-clock to compile a TU including (a) one version's `Messages.hpp` + `Reify.hpp`, (b) all four versions'. CI fails if median exceeds the §1.2 ceilings (single-version ≤ 3 s; all-version ≤ 15 s soft, with a configurable `FIXPP_BENCH_ALL_VERSIONS_CEILING` knob since the all-versions TU is "not supported by default" per N-P2-3). Also tracks per-header preprocessor expansion size to spot template-bloat regressions before they impact the wall-clock metric.
3. **Per-tag accessor latency regression.** Google Benchmark on `NewOrderSingle::cl_ord_id` (string), `::side` (char), `::order_qty` (decimal — split bench per N-P2-2), `::field_value(uint16_t)` (runtime-keyed), `::price` (decimal) over a warm-cache 20-tag message; CI fails on >5% regression vs baseline. Targets per §6.2.
4. **Dialect-overlay merge cost regression.** Bench `Dictionary::with_overlay(...)` with an overlay of 50 fields + 5 messages; target ≤ 1 ms (§1.2). Larger overlay tests (500 fields, 5000 fields) verify scaling matches the documented O(N_base + N_overlay log N_overlay) complexity (per N-P3-3). Worst-case test: 500-message + 5000-field overlay against each base codegen version with a wall-clock ceiling proportional to `N_base + N_overlay log N_overlay`.
5. **Codegen lookup latency regression.** Bench `Dictionary::field_ref`, `field_valid_for`, `required_fields`, `group_first_field`, `length_pair_data_tag`, `resolve_application_version` on the merged dictionary; CI fails on >5% regression. Targets per §6.2.
6. **`owning_message_t<>` reify latency regression.** Bench `dict::reify_as<NewOrderSingle>(view, mr)` on 20-tag and 200-tag messages; targets ≤ 1 µs and ≤ 10 µs (§1.2). Also benches `dict::reify(view, profile, mr)` (runtime-dispatch variant) at ≤ 1.2 µs (one switch dispatch over the typed variant). Verifies the resulting `owning_message_t<>` is move-safe across `std::thread` boundaries (a small smoke test that posts the value to a `std::thread` and reads back).
7. **Allocation guard (Linux).** `tools/check_alloc.py` runs the typed-accessor read loop and the reify/move/access loop under the `mallocnesia` interceptor; any allocation between the typed-message constructor and the typed accessor reads (the read path) fails CI; the reify path is allowed ≤ 4 PMR allocations and no more (`[const §VIII.5]` plus §1.2 budget per N-P2-5). Same Linux-only caveat as `[2a §9 seam #6]`.
8. **Fuzzer (libFuzzer) — XmlLoader malformed input.** `tests/fuzz/fuzz_dict_xml_loader.cpp` feeds arbitrary bytes to `XmlLoader::load_from_string` and `load_overlay_from_string`; targets ASan + UBSan invariants. Also tests the §4.4.2 caps (XML byte size, nesting depth, billion-laughs entity expansion, cyclical components). Required by `[const §IX.4]`.
9. **Fuzzer (libFuzzer) — dialect-overlay merge edge cases.** `tests/fuzz/fuzz_dict_overlay_merge.cpp` synthesizes random `DialectOverlay` values (random field additions, random conflict policies, random grammar-violation patterns) and merges them against each base codegen version; verifies no UB / crash, and that any returned `error::dict_overlay_*` matches the documented variant. Catches overlay-merge invariants the per-test corpus might miss.
10. **Multi-version coexistence test.** Single-process test split into:
    - **10a — multi-session multi-version (no namespace bleed).** Opens three sessions: one FIX 4.4, one FIX 5.0 SP2, one FIXT.1.1 + FIX 5.0 SP2 default-appl. Sends a `NewOrderSingle` on each, dispatches to the correct typed handler, verifies no namespace bleed (e.g., a `fixpp::v44::NewOrderSingle` cannot be implicitly converted to `fixpp::v50sp2::NewOrderSingle`). Lives in `tests/integration/multi_session_multi_version.cpp`. Per N-P3-2.
    - **10b — single FIXT.1.1 session with cross-vocabulary dispatch.** Opens one FIXT.1.1 session with `DefaultApplVerID=v50sp2`. Sends the worked-example byte stream from §6.3 (Logon → vt11; NOS with ApplVerID=9 → v50sp2; NOS with ApplVerID=6 → v44 per-message override; OrderCancelRequest no ApplVerID → v50sp2 default; Heartbeat → vt11). Verifies each frame dispatches to the correct typed namespace per the resolution algorithm (`[FIXT §5]`). Lives in `tests/integration/fixt_cross_vocabulary.cpp`. Per RC-1 / N-P3-2.
    - **10c — runtime-XML-only versions round-trip.** Loads FIX 4.0 / 4.1 / 4.3 / 5.0 / 5.0SP1 dictionaries via `XmlLoader::load`; verifies the runtime tag-keyed accessor (`view.get(uint16_t tag)`) works against parsed messages in those versions; verifies `dict::reify(view, profile, mr)` returns `dict_reify_unknown_msg_type` for those versions (no codegen-emitted `owning_<Msg>` for runtime-XML versions). Per §1.3 / RC-4.
11. **Dialect-overlay precedence + grammar-closure test.** With base FIX 5.0 SP2 and an overlay that overrides `Side(54)`'s `presence` for `NewOrderSingle`, verifies (a) overlay wins under `OverlayWins`, (b) base wins under `BaseWins`, (c) `Reject` policy errors out at `with_overlay`, (d) removing the overlay restores base behaviour (close-and-reopen pattern). Verifies `Dictionary::was_dialect_promoted(custom_tag)` returns `true` for genuinely-new tags and `false` for tags the base already had. Verifies grammar-closure rejects: an overlay XML containing a conditional-required tag returns `dict_overlay_unsupported_rule`; an overlay XML containing a Length+Data pair returns `dict_overlay_unsupported_length_pair`; an overlay XML attempting to add a group to an existing MsgType returns `dict_overlay_unsupported_rule`. Per RC-6.
12. **`owning_message_t<>` cross-strand handoff test.** Construct a `MessageView` on thread A, call `dict::reify_as<NewOrderSingle>(view, mr)`, `std::move` the `owning_NewOrderSingle` to thread B (via `std::async` or a `concurrent_queue`), read accessors on thread B after the per-message arena on thread A has been reset. Verifies (a) no UB, (b) values match what was read on thread A pre-reset, (c) the original `MessageView` (still on thread A) traps in debug if accessed post-reset (the documented footgun from `[2b §6.4]`).
13. **`Dictionary` move + outstanding `table_view` + shared_ptr refcount semantics (per C-R2-P1-1).** Construct a `Dictionary` via `XmlLoader::load(...)`; call `as_table_view()`; store the `table_view` in a `wire::dictionary_driven_validator`; `std::move` the `Dictionary` into a `SessionConfig`; verify the `table_view` continues to return the same metadata (per N-P1-1 / RC-3 — heap-pinned metadata-handle survives the move; the `shared_ptr` move is no-throw and touches no atomics). Also: build `auto base = XmlLoader::load(...); auto merged = base.with_overlay(...);` — verify the metadata-handle control block now has refcount 2 (one from `base.handle_`, one from `merged.handle_->base_keepalive_`). Drop `base` (the `shared_ptr` decrements refcount to 1 atomically); verify (a) the merged `Dictionary` continues to work — its `handle_` plus its `base_keepalive_` keep the metadata alive — and (b) any `table_view` taken from `merged.as_table_view()` continues to return correct metadata after the base is dropped. Final destruction of `merged` drops refcount to 0, the control block deallocator runs, memory returns to the originating `mr`. Per the `shared_ptr` sharing path (§6.5 / C-R2-P1-1).
14. **`owning_<Msg>` move + lazy view rebuild.** Construct an `owning_NewOrderSingle` via `dict::reify_as`; access `cl_ord_id()` (populates the lazy view cache); `std::move` it; access `cl_ord_id()` on the moved-to instance; verify the cached view is rebuilt against the post-move `bytes_` and returns the correct value (per N-P1-3 / N-P1-2 / C-P1-4). Verify the source's `frame_cache_` / `view_cache_` are `std::nullopt` after move (the custom move ctor explicitly resets both sides). Verify the destination's caches are `std::nullopt` immediately after the move and become populated only on first `view()` access. Also a static-assert variant: the `owning_NewOrderSingle` class has no reference members; `std::is_move_constructible_v<owning_NewOrderSingle>` is true; `std::is_nothrow_move_constructible_v<owning_NewOrderSingle>` is true; the move ctor is *not* `= default` (verified by SFINAE-detecting the non-trivial body if the toolchain exposes it, otherwise by behavioural inspection — the source's caches must be empty post-move, which a defaulted move on `optional<T>` would not deliver).
15. **`dict::reify` runtime-dispatch round-trip (split per RC#1 / C-P3-2).**
    - **15a — vt11 admin MsgTypes (FIXT-dispatch family).** For each of the 7 FIXT admin MsgTypes (`Logon`, `Heartbeat`, `TestRequest`, `Reject`, `SequenceReset`, `Logout`, `ResendRequest`), feed a `wire::MessageView<Index>` through `dict::reify(view, profile, mr)`; verify dispatch goes through `reify_dispatch_fixt.hpp` and yields a `vt11::owning_<Msg>` wrapped in `owning_message_handle`. Verify `handle.version().k == kind::session_admin` and `handle.version().session == vt11`. Smoke covers all 7 (the FIXT admin set is small and bounded).
    - **15b — application MsgTypes (application-dispatch family).** For each of the four codegen versions × a representative MsgType (`NewOrderSingle`, `ExecutionReport`, `MarketDataIncrementalRefresh`, `OrderCancelRequest` — application messages only; FIXT admin is in 15a), feed a `wire::MessageView<Index>` through `dict::reify(view, profile, mr)`, downcast the returned `owning_message_handle` via `as<Msg>()`, verify the typed accessors return the same values as a typed `dict::reify_as<Msg>` on the same view. Verify `handle.version().k == kind::application` and `handle.version().application` matches the resolved version. Catches `reify_dispatch_application.hpp` generation regressions (a missing case, a wrong type-erased payload). ~470 dispatch entries to exercise; the seam covers a representative subset (~20 messages per version) plus an exhaustive smoke run nightly. Runtime-XML-only versions' negative cases live in seam #10c. Per RC-2 / RC#1.
    - **15c — `dict_unresolved_application_version` propagation.** Construct a FIXT.1.1 `Dictionary` whose `default_appl == application_version::Unknown` (Logon never set `DefaultApplVerID(1137)` and the session config supplied no fallback). Feed a `MessageView` of an application MsgType (e.g., `NewOrderSingle`, no `ApplVerID(1128)` on the wire) through `dict::reify(view, profile, mr)`; verify the result is `expected_t::unexpected{dict_unresolved_application_version}` (NOT `dict_reify_unknown_msg_type` — the v1.0 misdiagnosis path is closed). Per RC#1 / C-P1-5.
16. **`trap_throw` PMR OOM injection.** Bounded `monotonic_buffer_resource` with a tracking upstream that fails after N bytes injected into:
    - `Dictionary::with_overlay(overlay, mr)` — verify `dict_overlay_oom`.
    - `dict::reify_as<NewOrderSingle>(view, mr)` — verify `dict_reify_oom`.
    - `dict::reify(view, profile, mr)` — verify `dict_reify_oom`.
    - `owning_NewOrderSingle::from_view(view, mr)` — verify `dict_reify_oom`.
    - **`XmlLoader::load_from_string(xml_text, mr)` (per C-P2-1).** Bounded-PMR injection during DOM build / FieldRef array allocation / name string pool; verify the construction-time exception is `dict::xml_oom_error` (XmlLoader is exception-API per §6.1.1 carve-out — not `expected_t`). Confirms the `bad_alloc → xml_oom_error` translation works.
    - **`XmlLoader::load_overlay_from_string(xml_text, mr)` (per C-P2-1).** Same bounded-PMR injection; verify `dict::xml_oom_error` is thrown (not `expected_t`).
    None of the calls terminate; the noexcept-API entries return the documented error variant; the exception-API entries (XmlLoader) throw the documented exception. Per RC-5 / `[2a §4.2]` pattern.
17. **Programmatic `DialectOverlay` PMR pinning test.** Construct a `DialectOverlay` via `DialectOverlay::create(mr)` where `mr` is a tracking PMR resource; populate `field_additions()` and `message_additions()`; verify all allocations land on `mr` and not on `std::pmr::get_default_resource()`. Per C-P2-5.
18. **Static-assert tests for typed-message flyweight size.** A per-message-class `static_assert(sizeof(NewOrderSingle) == sizeof(wire::MessageView<wire::access_mode::Index> const*))` — the typed message holds exactly one reference (which is pointer-sized on supported toolchains), no other state. Catches accidental member additions in the codegen template that would inflate every typed-message instance. Per N-P1-3 — moved from a brittle `sizeof(MessageView ref)` to the pointer-sized invariant.
19. **Length+Data static-table coverage test.** Verifies the codegen-emitted Length+Data pair table for each codegen version is exhaustive against the standard XML (every `<field type="LENGTH" />` paired with the documented `<field type="DATA" />` neighbour); cross-checks against `[FIX50SP2 §3.3]`'s field-pair list. Catches a codegen regression that drops a pair (which would make `wire::field_iterator` skip SOH incorrectly inside a `data`-typed field).
20. **Overlay-promoted tag access via `field_value` forwarder.** Apply an overlay that promotes a custom tag `9999=VenueRiskID` on a venue-specific NewOrderSingle. Verify `nos.field_value(9999)` returns the promoted value; verify `view().get(9999)` returns the same value; verify `Dictionary::was_dialect_promoted(9999)` returns true. Per N-P1-2.

## 10. Open questions

Cross-doc handoffs and within-2c follow-ups. v1.0 drops v0.1's Q4 (deferred conditional support) and Q2 (Iter-mode Length+Data dialect extension) per RC-6 — both are now explicitly out-of-scope-for-v1 and rejected at the loader. Replaced with a single grammar-extension question.

| # | Question | Disposition | Owner |
|---|---|---|---|
| 1 | Codegen-output compile-time cost spike on the all-versions TU — is the §1.2 ≤ 15 s soft ceiling defensible on the engine-target hardware? If not, the post-v1.0 modules / PCH adoption (per `[SYN §3.3 Q11]`) reopens earlier than planned. | Spike during 2c implementation against representative TUs (a translator that includes all four codegen versions); if the soft ceiling is hot, reopen `[SYN §3.3 Q11]` for a v1.x preview. The single-version ceiling (≤ 3 s) is the load-bearing one in v1.0; the all-versions TU is "not supported by default" per N-P2-3. | 2c (this doc, post-Gate-A spike) |
| 2 | v1.x dialect-overlay grammar extension — should the v1.x overlay grammar relax §4.4.1 to admit conditional rules and Length+Data pairs? The v1.0 closure rejects both with documented errors. **Disposition:** spike behind COM-011's first dialect customer pull. If the customer needs conditional rules: extend the overlay schema with a `conditional_rules` table parallel to the codegen'd `Validator.hpp` table and merge into the runtime validator. If they need Length+Data pairs in Iter mode: codegen a per-dialect `length_pair_iter_table_supplement` header that the user `#include`s alongside `field_iterator`, which is a recompile path. | DEFERRED to first dialect-overlay customer pull. The v1.0 closure (§4.4.1) is the conservative default; v1.x relaxation is opt-in at customer discovery. | 2c follow-up |
| 3 | Should `DialectOverlay` ever become a runtime virtual `DialectOverlayPlugin` interface? — For v1.0, value-typed is sufficient (§4.4). The trigger for revisiting is a real consumer asking for "plug in a regulator's rules engine that computes overlay rules at runtime"; the v1.0 surface accommodates the addition without breaking change. | DEFERRED to first user pull. Tracked here so future readers see the boundary. | 2c follow-up |
| 4 | Per-version codegen for the runtime-XML-only versions (FIX 4.0 / 4.1 / 4.3 / 5.0 / 5.0SP1) — Appendix D's proposed amendment defers these to "post-v1.0 best-effort." Should v1.x prioritise any of them? **Disposition:** prioritise FIX 4.3 first (most-used legacy version in the post-v1 backlog per the COM-coverage informal survey); 5.0SP1 second; 5.0 third; 4.0/4.1 last (vanishingly few production deployments). | DEFERRED post-v1; track in `[const §XVIII]` post-1.0 roadmap once Appendix D's amendment lands. | post-v1 follow-up |
| 5 | A-024 codegen — A-024 was dropped per `[SYN §4.4]` as a duplicate of A-018. Confirm `fixpp-codegen`'s fail-mode if A-024 appears in source XML: hard error (loud failure, recommended) vs filter at build time (codegen skips, build proceeds). v1.0 picks the hard-error path. | DECIDED hard-error. Reconsider if a future FIX dictionary refresh re-introduces A-024 with different semantics. | 2c |
| 6 | Confirm with **2e** that MessageStore's canonical model is raw frames (per `[2a §7.1]` v0.3 and `[2b §7.4]`); 2c's `owning_message_t<>` is the cross-strand handoff target, *not* the persistence shape. If 2e proposes a typed-payload variant for MessageStore (alongside the raw-frame default), 2c may need to expose `owning_message_t<>` serialise-back-to-bytes for the typed-payload path. v1.0 assumes that's not needed. | Confirm at **2e**. | 2c + 2e |
| 7 | `fixpp-codegen` `NormativeReferences.md` emission per C-P3-2 — confirm the codegen tool emits `_codegen/include/fixpp/<vXX>/NormativeReferences.md` per version, with per-message exact `[FIXxx §X.Y.Z]` citations resolved from the source XML's `<message>` and `<field>` references. v1.0 spec cites the generated file as the per-message reference (Appendix B); the generated file lives under the per-version CMake target so packaging picks it up automatically. | Confirm at 2c implementation; tool enhancement scoped alongside the codegen tool's output paths. | 2c |
| 8 | `_reserved` byte semantics in `FieldRef` / `ComponentRef` / `GroupRef` / `version_profile` — match 2a's pattern (zero on emit; ignore on read in v1.0; future minor version may use under `FIXPP_DICT_*_RESERVED_USED` macros) per C-P3-1. | DECIDED match 2a / `[2a §4.2]`. Applied uniformly to all four primitive types. Reopens only if Gate A pushes back. | 2c |
| 9 | `dict::reify` runtime-dispatch fall-through for runtime-XML-only versions — currently returns `dict_reify_unknown_msg_type`; users on those versions must use the runtime tag-keyed accessor (`view.get(uint16_t)`). Should v1.x add a "untyped owning message" variant (`untyped_owning_message_handle` — bytes + offset table + version_profile, no typed downcasts) for those versions? | DEFERRED post-v1; tracked alongside Q4. | post-v1 follow-up |
| 10 | `dict::version_registry` ownership model (per N-P2-7 / §4.9) — engine-owned-by-value vs session-borrowed; how the FSM reaches the registry at dispatch time; whether `EngineConfig::dictionaries` is the construction shape; whether the registry is per-engine or per-session. v1.0 / v1.1 publishes the *shape* (`get(application_version) → expected_t<Dictionary const*>`) but defers the ownership story to 2d threading + EngineConfig design. | DEFERRED to 2d. | 2c + 2d |

## 11. Hand-off

After Gate A and user sign-off, **2c** unblocks:

- **2d** (application threading contract) — knows the typed-message dispatch shape (per-version namespaces, flyweight constructor signatures, the `dict::reify` bridge for cross-strand handoffs); per-session `Dictionary` is a `SessionConfig` field; the FIXT.1.1 cross-vocabulary dispatch algorithm (§6.3) is the FSM's responsibility.
- **2e** (MessageStore async API) — knows that typed messages are flyweights over raw frames, that `owning_message_t<>` is the cross-strand handoff (not the persistence shape per `[2a §7.1]` / `[2b §7.4]`), and that any typed-payload-persistence variant needs a 2c-side serialise-back-to-bytes path (documented as §10 Q6).
- **2i** (C ABI message rep + error enum) — knows `fixpp_msg_t` carries a runtime resolved-message-version tag (§5), the `application_version` enum maps to a C-ABI constant set, the C-ABI accessor surface wraps the typed-message classes per the §5 commitments, and the C surface speaks in C-shape only (no PMR leakage). New `error::dict_*` variants from §6.7 enter the C-ABI mapping.
- **2j** (control-plane interface) — knows that dictionary swap is session-create-time only (§6.3 / §7.4), the gRPC `OpenSession` carries version + optional overlay, no live-swap RPC. Mid-session swap is rejected categorically per §7.2 / Appendix D §5.
- **2m** (SWIG / Python binding shape) — knows the per-version submodule shape (`fixpp.v42`, `fixpp.v44`, `fixpp.v50sp2`, `fixpp.vt11`), the typed-message exposure shape, the version-tag accessor, the dialect-overlay binding (per §7.5), and the runtime-XML-only versions' tag-keyed-only Python access.
- **Orchestrator** (parent session) applies the proposed constitutional amendment from Appendix D to `[const §I.1]`, `[const §VI]`-related clauses, `[const §XVIII]` (post-v1 roadmap note), and `[arch §5.6]` (mid-session swap wording) during the sign-off commit.

**2c does not add new catalogue rows.** D-001..D-011, OSS-001, OSS-010 are already OFFICIAL; the per-message typed-class rows (A-001..A-013, M-/P-/C-/R-/N- families) are already OFFICIAL and already split between this doc (typed classes + metadata) and 2b (parse/serialize/validate). 2c's contribution to those rows is the *typed-class generation*; the rows themselves already exist. `feature-catalogue.md` is **not edited** from this rewrite.

`feature-catalogue.md` does **not** need a row added when 2c lands. (Distinct from 2d's `NFR-015` clock row, tracked in `[arch §11]` row 7.)

---

## Appendix A — Catalogue row coverage

For each owned row, this appendix records *what the catalogue row actually says* (read against `library/spec/feature-catalogue.md` lines 70–80 and the per-family rows beneath), what 2c v1.0 *actually does* about it, and the v1.0 disposition (codegen + runtime-XML / runtime-XML only / deferred). Per RC-4: rewritten from scratch against the actual catalogue text; v0.1's Appendix A invented row meanings.

### Dictionary infrastructure rows (D-001 .. D-011)

| Row | Actual catalogue title | 2c v1.0 coverage | Disposition |
|---|---|---|---|
| **D-001** | "FIX 4.2 data dictionary — all standard messages, fields, components, groups" | `XmlLoader::load(...)` (§4.5) loads FIX 4.2 XML at runtime; `fixpp::v42::*` codegen emits `Messages.hpp` / `Fields.hpp` / `Validator.hpp` / `Reify.hpp` / `NormativeReferences.md` for FIX 4.2 (§4.7, §1.3 codegen scope). | **codegen + runtime XML.** |
| **D-002** | "FIX 4.4 data dictionary" | `XmlLoader::load(...)` loads FIX 4.4 XML; `fixpp::v44::*` codegen emits the per-version header pack. | **codegen + runtime XML.** |
| **D-003** | "FIX 5.0SP2 + FIXT.1.1 data dictionary" | `XmlLoader::load(...)` loads FIX 5.0 SP2 + FIXT.1.1 XML; `fixpp::v50sp2::*` and `fixpp::vt11::*` codegen emit the per-version header packs. The `version_profile` (§4.3) carries `(session_version::vt11, default_appl::v50sp2, has_per_message_override=true)` for FIXT.1.1 sessions; cross-vocabulary dispatch per §6.3 / RC-1. | **codegen + runtime XML.** |
| **D-004** | "FIX 4.0, 4.1 data dictionaries (older, minimal)" | `XmlLoader::load(...)` loads FIX 4.0 / 4.1 XML at runtime; runtime `Dictionary` works for field/required/group/length-pair lookups. **No `fixpp::v40::*` or `v41::*` namespace generated.** Users access fields through `view.get(uint16_t tag)` per `[2b §4.3]` and `field_valid_for(...)` against the runtime-loaded `Dictionary`. Codegen deferred to post-v1.0 best-effort per Appendix D's proposed amendment. | **runtime XML only.** |
| **D-005** | "FIX 4.3 data dictionary" | `XmlLoader::load(...)` for FIX 4.3 XML. No `fixpp::v43::*` namespace. | **runtime XML only.** |
| **D-006** | "FIX 5.0, 5.0SP1 data dictionaries" | `XmlLoader::load(...)` for FIX 5.0 / 5.0SP1 XML. No `fixpp::v50::*` or `fixpp::v50sp1::*` namespace. | **runtime XML only.** |
| **D-007** | "XML data dictionary format loader — parse FIX standard XML (QuickFIX-style) at runtime" | `XmlLoader::load(...)` (§4.5) accepts QuickFIX-style XML for any of the 9 v1.0-supported versions. Schema compatibility per OSS-001. DoS guards per §4.4.2 (max XML bytes, max depth, no external entities by default, cyclical-component detection). | **all 9 v1.0 versions.** |
| **D-008** | "Code-generated `constexpr` field metadata from data dictionary — zero-runtime-cost field lookup" | `tools/codegen/fixpp-codegen` emits per-version `Fields.hpp` (`constexpr std::array<FieldRef, N>`), `Messages.hpp` (typed flyweights), `Validator.hpp` (per-message rules), `Reify.hpp` (per-message `owning_<Msg>`; dispatch headers shared across versions), `NormativeReferences.md` for the **four codegen versions only**: v42, v44, v50sp2, vt11 (§1.3). The catalogue row title in `feature-catalogue.md` line 77 retains its broader 4.0–5.0SP2 scope (per the locked decision); the codegen-vs-runtime-XML disposition is recorded as a coverage-index supplemental note attached to D-008 (Appendix D §2). | **four codegen versions only** (codegen-vs-runtime-XML disposition recorded in coverage-index supplemental note per Appendix D §2 / RC#3). |
| **D-009** | "Custom dictionary extension — user-defined fields and messages" | `DialectOverlay` value type (§4.4) + `XmlLoader::load_overlay(...)` (§4.5) + `Dictionary::with_overlay(...)` (§4.3) + additive merge (§6.4), within the v1.0 grammar closure (§4.4.1). For overlay-promoted custom tags, `field_value(uint16_t)` forwarder on every typed message (§4.7.1) plus `Dictionary::was_dialect_promoted(...)` diagnostic. | **codegen + runtime XML, within v1.0 grammar closure.** |
| **D-010** | "Component definition support — reusable field groups (Instrument, Parties, etc.)" | `dict::ComponentRef` (§4.2) for the metadata shape; per-codegen-version generated component shells in `Messages.hpp`; runtime-XML-only versions reach components through `Dictionary::field_ref(msg_type, tag)` which resolves component-membership via the `FieldRef::component_index` indirection (§4.1). | **all 9 v1.0 versions** for the runtime metadata; **codegen versions only** for typed component shells. |
| **D-011** | "FIX Latest / FIX Orchestra repository format" | **Deferred post-v1.0** per `[const §XVIII.2]` (FIX-Latest is v1.2). v0.1's claim that this row was discharged by `DialectOverlay` is incorrect — Orchestra is a different XML schema and a different feature; `DialectOverlay` does not implement Orchestra-format ingest. v1.0 explicitly does not discharge D-011. | **deferred to v1.x** (`[deferred to v1.x]`). |

### OSS rows

| Row | Actual catalogue title | 2c v1.0 coverage | Disposition |
|---|---|---|---|
| **OSS-001** | (QuickFIX-XML compatible loader; row text per `feature-catalogue.md`'s OSS family) | `XmlLoader::load(...)` accepts the QuickFIX XML schema (`fields`, `messages`, `components`, `header`, `trailer` top-level elements); §4.5. | **discharged.** |
| **OSS-010** | (header-only generated typed messages with `constexpr` field metadata; row text per `feature-catalogue.md`'s OSS family) | per-codegen-version `Messages.hpp` + `Fields.hpp` are header-only with `constexpr` tables (§4.1, §4.7). | **discharged for the four codegen versions.** |

### Application-message rows

For all rows below, 2c's contribution is the *typed-class generation* under each codegen version's namespace; parse/serialize/validate of the message bytes is owned by **2b** (`Parser` / `Writer` / `Validator`); per-tag exact-spec citations live in the generated `_codegen/include/fixpp/<vXX>/NormativeReferences.md` (per C-P3-2). Runtime-XML-only versions get no typed class; users in those versions read/write through the runtime tag-keyed accessor.

| Row | Actual catalogue title | 2c v1.0 coverage |
|---|---|---|
| **A-001** | "NewOrderSingle (35=D)", FIX 4.0–5.0SP2 | Typed class under `fixpp::v42::NewOrderSingle`, `fixpp::v44::NewOrderSingle`, `fixpp::v50sp2::NewOrderSingle`. FIX 4.0 / 4.1 / 4.3 / 5.0 / 5.0SP1: runtime XML only (no typed class). |
| **A-002** | "NewOrderList (35=E)", 4.0–5.0SP2 | `fixpp::v42::NewOrderList`, v44, v50sp2. |
| **A-003** | "OrderCancelRequest (35=F)", 4.0–5.0SP2 | v42, v44, v50sp2. |
| **A-004** | "OrderCancelReplaceRequest (35=G) — amend", 4.0–5.0SP2 | v42, v44, v50sp2. |
| **A-005** | "OrderStatusRequest (35=H)", 4.0–5.0SP2 | v42, v44, v50sp2. |
| **A-006** | "ExecutionReport (35=8)", 4.0–5.0SP2 | v42, v44, v50sp2. |
| **A-007** | "OrderCancelReject (35=9)", 4.0–5.0SP2 | v42, v44, v50sp2. |
| **A-008** | "OrderMassCancelRequest (35=q)", 4.4–5.0SP2 | v44, v50sp2 (not v42 — first introduced in 4.4). |
| **A-009** | "OrderMassCancelReport (35=r)", 4.4–5.0SP2 | v44, v50sp2. |
| **A-010** | "OrderMassStatusRequest (35=AF)", 4.4–5.0SP2 | v44, v50sp2. |
| **A-011** | "MultilegOrderCancelReplace (35=AC)", 4.4–5.0SP2 | v44, v50sp2. |
| **A-012** | "CrossOrderCancelReplaceRequest (35=t)", 4.4–5.0SP2 | v44, v50sp2. |
| **A-013** | "CrossOrderCancelRequest (35=u)", 4.4–5.0SP2 | v44, v50sp2. |
| **A-014..A-034** | Additional order-management variants per `feature-catalogue.md` lines 291–311 (e.g., A-014 BusinessMessageReject 35=j; A-015 DontKnowTrade 35=Q; A-019 ListCancel/Execute/Status; A-025 SecurityList family 35=v/w/x/y; A-034 XMLnonFIX 35=n) | **`[deferred to v1.x — codegen]` and `[v1.0 — runtime-XML only]`.** v1.0 ships runtime-XML access via `view.get(uint16_t tag)` for these messages across the four codegen versions (v42, v44, v50sp2 — wherever the source XML defines the message); typed-message classes deferred to v1.x per the proposed `[const §XVIII.7]` sub-clause and the `[arch §4.2]` row 2c amendment in Appendix D §3. (A-024 stays dropped as a duplicate per `[SYN §4.4]`; if a future FIX dictionary refresh re-introduces it, §10 Q5 reopens.) Per RC#3 / C-P1-1. |
| **M-001** | "MarketDataRequest (35=V)", 4.2–5.0SP2 | v42, v44, v50sp2. |
| **M-002** | "MarketDataSnapshotFullRefresh (35=W)", 4.2–5.0SP2 | v42, v44, v50sp2. |
| **M-003** | "MarketDataIncrementalRefresh (35=X)", 4.2–5.0SP2 | v42, v44, v50sp2. |
| **M-004** | "MarketDataRequestReject (35=Y)", 4.2–5.0SP2 | v42, v44, v50sp2. |
| **M-005** | "SecurityDefinitionRequest (35=c) / SecurityDefinition (35=d)", 4.2–5.0SP2 | v42, v44, v50sp2. |
| **M-006** | "SecurityStatusRequest (35=e) / SecurityStatus (35=f)", 4.2–5.0SP2 | v42, v44, v50sp2. |
| **M-007** | "TradingSessionStatusRequest (35=g) / TradingSessionStatus (35=h)", 4.2–5.0SP2 | v42, v44, v50sp2. |
| **M-008** | "MassQuote (35=i) / MassQuoteAcknowledgement (35=b)", 4.2–5.0SP2 | v42, v44, v50sp2. |
| **M-009** | "Quote (35=S) / QuoteAcknowledgement (35=b)", 4.0–5.0SP2 | v42, v44, v50sp2. |
| **M-010** | "QuoteRequest (35=R) / QuoteRequestReject (35=AG)", 4.0–5.0SP2 | v42, v44, v50sp2. |
| **M-011** | "QuoteCancel (35=Z)", 4.2–5.0SP2 | v42, v44, v50sp2. |
| **M-012** | "QuoteStatusRequest (35=a)", 4.2–5.0SP2 | v42, v44, v50sp2. |
| **P-001** | "AllocationInstruction (35=J)", 4.0–5.0SP2 | v42, v44, v50sp2. |
| **P-002** | "AllocationInstructionAck (35=P)", 4.1–5.0SP2 | v42, v44, v50sp2. |
| **P-003** | "AllocationReport (35=AS)", 4.4–5.0SP2 | v44, v50sp2. |
| **P-004** | "AllocationReportAck (35=AT)", 4.4–5.0SP2 | v44, v50sp2. |
| **P-005** | "Confirmation (35=AK) / ConfirmationAck (35=AU) / ConfirmationRequest (35=BH)", 4.4–5.0SP2 | v44, v50sp2. |
| **P-006** | "SettlementInstructions (35=T)", 4.0–5.0SP2 | v42, v44, v50sp2. |
| **P-007** | "SettlementInstructionRequest (35=AV)", 4.4–5.0SP2 | v44, v50sp2. |
| **P-008** | "TradeCaptureReport (35=AE) / TradeCaptureReportRequest (35=AD) / TradeCaptureReportAck (35=AR) / TradeCaptureReportRequestAck (35=AQ)", 4.4–5.0SP2 | v44, v50sp2. |
| **C-001** | "CollateralRequest (35=AX) / CollateralAssignment (35=AY) / CollateralResponse (35=AZ) / CollateralReport (35=BA) / CollateralInquiry (35=BB) / CollateralInquiryAck (35=BG)", 4.4–5.0SP2 | v44, v50sp2. |
| **C-002** | "PositionMaintenance (35=AL) / RequestForPositions (35=AN) / RequestForPositionsAck (35=AO) / PositionReport (35=AP) / AdjustedPositionReport (35=BL)", 4.4–5.0SP2 | v44, v50sp2. |
| **C-003** | "AccountSummaryReport (35=CQ)", 5.0SP2 | v50sp2 only. |
| **R-001** | "RegistrationInstructions (35=o) / RegistrationInstructionsResponse (35=p)", 4.2–5.0SP2 | v42, v44, v50sp2. |
| **R-002** | "IndicationOfInterest (35=6)", 4.0–5.0SP2 | v42, v44, v50sp2. |
| **R-003** | "Advertisement (35=7)", 4.0–5.0SP2 | v42, v44, v50sp2. |
| **R-004** | "News (35=B)", 4.0–5.0SP2 | v42, v44, v50sp2. |
| **R-005** | "Email (35=C)", 4.0–5.0SP2 | v42, v44, v50sp2. |
| **N-001** | "NetworkCounterpartySystemStatusRequest (35=BC) / NetworkCounterpartySystemStatusResponse (35=BD)", 5.0–5.0SP2 | v50sp2 (5.0 / 5.0SP1: runtime XML only). |
| **N-002** | "UserRequest (35=BE) / UserResponse (35=BF)", 5.0–5.0SP2 | v50sp2 (5.0 / 5.0SP1: runtime XML only). |
| **N-003** | "ApplicationMessageRequest (35=BW) / ApplicationMessageRequestAck (35=BX) / ApplicationMessageReport (35=BY)", 5.0SP2 | v50sp2 only. |
| **A-035..A-065** | FIX-Latest application messages | **Deferred to v1.2** per `[const §XVIII.2]`. `[deferred to v1.x]` markers; codegen tool gates these behind `FIXPP_CODEGEN_ENABLE_FIX_LATEST` (§2). |

(Final per-tag mapping confirmed at codegen-tool implementation; the list above maps catalogue row ID → 2c v1.0 codegen version coverage. The runtime-XML-only versions are reachable through `XmlLoader::load(...)` for any row whose FIX-version range includes them, but with no typed class.)

## Appendix B — Normative References

Per `[const §VI.5]`, every `/specify` artifact lists the exact coverage-index references that inform it. Per C-P3-2, the per-message exhaustive references are *generated* into `_codegen/include/fixpp/<vXX>/NormativeReferences.md` by `fixpp-codegen` (one per codegen version) and cited from there; this appendix lists the spec-wide references that govern the doc as a whole.

| Topic | Source | Where applied |
|---|---|---|
| Tag=Value SOH encoding (typed messages reuse 2b's primitives) | `[FIX-SL §3]` Header / Body / Trailer; `[FIX50SP2 §3]` Encoding | §4.7 typed-message accessors, §7.1 wire integration |
| FIX field data types (incl. PRICE / QTY / AMT family for FLOAT, integer types, string types, timestamp types) | `[FIX50SP2 §3.3]` Field data types | §4.1 `data_type` enum, §4.7 typed accessors, §7.1 Length+Data table source-of-truth |
| Conditional-Required field semantics | `[FIX50SP2 §3.4]` Conditional fields | §4.1 conditional-rule encoding via `condition_index`, §6.4 dialect-overlay merge (codegen versions only; overlay grammar rejects per §4.4.1) |
| Application-version handling (FIXT.1.1 + multi-version codegen) | `[FIXT §5]` Application-version handling, `[FIXT §5.1]` `DefaultApplVerID(1137)`, `[FIXT §5.3]` `ApplVerID(1128)` per message | §4.3 `version_profile`, §6.3 multi-version coexistence rules + worked example |
| Per-message tables for application messages — *all families* | generated `_codegen/include/fixpp/<vXX>/NormativeReferences.md` per C-P3-2 (one per codegen version; auto-emitted from the dictionary XML by `fixpp-codegen` into the build tree) | §4.7 typed classes; Appendix A row mappings |
| Hot-path allocation discipline | `[const §VIII.5]` | §6.1, §6.2 (ceilings), §8 PMR recap |
| `noexcept` + PMR allocation discipline (`trap_throw`) | `[2a §4.2]` (the `fixpp::core::detail::trap_throw` reference helper) | §6.1.1, §6.7 (RC-5 fix) |
| C-ABI surface (deferred to 2i; 2c-side commitments only; no PMR leakage) | `[const §X]`, `[const §X.2]`, `[arch §4.10]` | §5 |
| Plugin interface ≤5 pure-virtual cap | `[const §XIV.2]` | §4.4 `DialectOverlay` value-typed decision (no virtual pure-virtuals declared at all in v1.0); §10 Q3 future-virtual question |
| Banned patterns — eager codegen with no runtime path mandates hybrid | `[const §XV.13]` | §3 inheritance from `[const §XV.13]` motivates `XmlLoader` + `DialectOverlay` runtime path alongside codegen; §1.3 disposition for runtime-XML-only versions |
| Banned patterns — no `thread_local` on hot path | `[const §XV]`, `[arch §5.4]` | §6.1 codegen output never emits `thread_local` |
| Banned patterns — no synchronous logging on hot path | `[const §XV]`, `[const §XIII]` | §6.1 codegen output never emits sync-log calls |
| Post-v1 roadmap — FIX-Latest A-035..A-065 + SOFH/SBE/FAST/FIXP/JSON/GPB/MMT incremental releases out of scope | `[const §XVIII.2]` | §2 non-goals, §10 Q1 |
| Constitutional amendment procedure | `[const §XX]` | Appendix D |
| Codegen output format — header-only `constexpr` arrays | `[SYN §3.3 Q11]` | §1 goal 2, §1.2 compile-cost ceilings, §10 Q1 follow-up |
| Multi-version coexistence — version-namespaced types | `[SYN §3.3 Q12]` | §1 goal 3, §4.7, §6.3, §5 (C-ABI version tag) |
| Dialect-extension layering — additive at runtime | `[SYN §3.3 Q13]` | §1 goal 4, §4.4 `DialectOverlay`, §4.4.1 v1.0 grammar closure, §6.4 merge contract, §7.1 Length+Data extension |
| Decimal extension point (typed accessors substitute `fixpp::decimal_t` at FLOAT) | `[2a §4.4]`, `[2a §7.2]` | §4.7 typed accessors |
| Wire surface (typed messages over `MessageView`; `dict::table_view` consumed by `wire::Validator`; three-arena pinning) | `[2b §4.3]`, `[2b §4.4]`, `[2b §4.6]`, `[2b §6.4]`, `[2b §6.6]`, `[2b §7.2]`, `[2b §7.4]` | §4.7, §7.1 |
| View-escape contract — `dict::reify_as<Msg>` / `dict::reify` bridge satisfies `[2b §6.6]`'s `MessageView::reify(mr)` reference | `[2b §6.6]` | §1 goal 6, §4.8, §6.6, §7.1 |
| Architectural inheritance | `[arch §1]`, `[arch §2]`, `[arch §3]`, `[arch §4.2]`, `[arch §5.2]`, `[arch §5.3]`, `[arch §5.4]`, `[arch §5.5]`, `[arch §5.6]`, `[arch §6]`, `[arch §7.3]`, `[arch §7.4]`, `[arch §9.1]`, `[arch §9.2]`, `[arch §10]` | §3 |
| Codex Gate A trigger (codegen layout — Appendix A trigger row) | `[const §XVII.1]`, `[const Appendix A]` row "Codegen layout" | this doc requires Gate A before `/tasks` |
| Normative-References completeness rule | `[const §VI.5]` | this Appendix B + the per-version generated `NormativeReferences.md` files |

Engineering-judgment decisions (16-byte `FieldRef`; `_reserved` discipline; `version_profile` 4-byte shape; `DialectOverlay` value-typed default vs virtual interface; per-version compile-cost ceilings; `owning_<Msg>` per-message-class generation choice; conflict-policy default `OverlayWins`; A-024 hard-error-on-codegen; lazy-view design on `owning_<Msg>`; `dict::reify` typed + runtime-dispatch split; v1.0 overlay grammar closure) cite `[SYN §3.3 Q11–Q13]`, `[const §XIV.2]`, `[FIXT §5]`, `[2a §4.2]` inline at point of use and are intentionally omitted from this appendix.

## Appendix C — Convergence log

Records the v0.1 → v1.0 RESET (preserved below), the v1.0 → v1.1 Gate A round 1 convergence pass (preserved below), the v1.1 → v1.2 Gate A round 2 convergence pass (preserved below), the v1.2 → v1.3 Gate A round 3 convergence pass (preserved below; round cap hit at round 3 / user-authorized one-pass post-cap convergence), and the v1.3 → v1.4 post-sign-off targeted amendment (this entry, prepended; **not a Gate A round** — a scoped `[const §XX]` amendment fixing RC#2 decimal-decoding API coherence).

---

### v1.3 → v1.4 (post-sign-off targeted amendment — RC#2)

Date: 2026-05-15.

**This is NOT a Gate A round.** It is a targeted post-sign-off design-doc amendment per `[const §XX]` (amendment process — conflict between a signed-off design and a merged upstream surface is resolved by amending the design, not by silently carrying the incoherence). Scope is **RC#2 only — decimal-decoding API coherence**. Every other v1.3 design decision is carried forward intact; no other section is reopened, no Gate A finding is re-litigated, and the non-decimal `field_traits<T>` / `decode_field<T>` route is untouched.

**Trigger.** Three rounds of `003-dictionary-codegen` Gate A review (round 1: `research/reviews/opus_003-dictionary-codegen_gate_a_adversarial_review.md` — Codex P2-1 escalated to P1 + finding N-P1-1 "decimal accessor has no arena/mr source"; rounds 2–3: `research/reviews/opus_003-dictionary-codegen_gate_a_3_adversarial_review.md` — RC#2 confirmed "correctly-documented-as-blocking, un-fixable in-bundle") surfaced that v1.3's typed-decimal route — `fixpp::decimal_t::from_chars(fv->bytes())` cited as `[2a §4.2]` — is **incoherent with the merged 2a v0.3 / 001-core-decimal surface**:

1. No such symbol exists. 2a's only decimal-parse entry points are `decimal_traits<T>::from_chars(std::span<const std::byte>, std::pmr::memory_resource*) noexcept -> expected_t<T>` (`[2a §4.2]`; mirrored at `specs/001-core-decimal/contracts/decimal_traits.hpp:98-100`) and the `decimal<T>` shell `decimal_t::parse(std::span<const std::byte>, std::pmr::memory_resource*) noexcept -> expected_t<decimal>` (`[2a §4.3]`; mirrored at `decimal_traits.hpp:162-163`) — **both PMR-mandatory**. 2a's own Gate A explicitly removed the single-argument form (`[2a §8]`; `2a-decimal.md:532`, Codex P2 #4 / Opus P1 #4).
2. The v1.3 generated decimal accessor (zero-arg, `inline noexcept`) had **no memory-resource in scope** — a PMR-mandatory parse is uncallable from it.
3. v1.3's "allocation-free / ≤ 20 ns" framing for the decimal route was false against the real API (the decimal arm was already split to a separate ≤ 75 ns row per N-P2-2, but the surrounding allocation prose still implied unconditional zero-alloc).

**Net design effect — where the `mr` now comes from.** The decimal typed accessor takes an **explicit `std::pmr::memory_resource* mr` argument** and calls the real merged entry point `decimal_t::parse(fv->bytes(), mr)` (`[2a §4.3]`; its `expected_t<decimal_t>` return type matches the accessor return type exactly). This is the minimal change consistent with the existing v1.3 typed-accessor shape and §8 PMR model: the borrowed flyweight holds only a `wire::MessageView<Index> const&` and the AC-G7 `sizeof == one pointer` invariant forbids adding an arena member, so the resource is *threaded as an argument* rather than stored — the caller passes the per-message arena `[arch §5.2]` (the same arena `MessageView` already aliases per `[2b §6.4]`). On `owning_<Msg>`, which already owns an `mr_` (§4.8 private members), the `mr` argument defaults to `nullptr` meaning "use my own `mr_`". For the default `pod_decimal` trait the parse ignores `mr` and stays allocation-free; an allocating substituted `FIXPP_DECIMAL_T` draws from the supplied arena — never raw `new`/`delete`, so `[const §VIII.5]` ("no `new`/`delete` between parse and `fromApp`"; arena/PMR is the sanctioned mechanism) and `[const §XV.1]` ("arena/PMR for the rare materialise cases") remain satisfied.

Sections touched (RC#2-scoped only): status block (v1.3 → v1.4, date 2026-05-15, convergence-log pointer); §4.1.3 (RC#2 amendment note added; preamble + the two code-block comments + the closing allocation/latency sentence corrected to the real PMR-mandatory `decimal_t::parse(span, mr)` route — non-decimal `field_traits`/`decode_field` text unchanged); §4.7 (typed-accessor preamble comment + the two flyweight decimal sketches `order_qty`/`price` rewritten to `decimal_t::parse(fv->bytes(), mr)` with an explicit `mr` parameter; the "Zero allocation per accessor" key-property bullet split into a genuinely-zero-alloc non-decimal arm + a PMR-mandatory decimal arm; flyweight `sizeof == one pointer` static_assert unchanged — no member added); §4.8 (the two `owning_<Msg>` decimal accessor declarations `order_qty`/`price` given the `mr = nullptr → use mr_` shape); §6.1 (allocation bullet split decimal vs non-decimal); §6.2 (decimal latency row annotated PMR-mandatory; ceiling unchanged at ≤ 75 ns); §8 (PMR-recap decimal-accessor bullet added; the three 2c-owned storage-class rows unchanged); Appendix C (Appendix C intro updated; this entry prepended). All prior Appendix C entries (v1.2 → v1.3, v1.1 → v1.2, v1.0 → v1.1, v0.1 → v1.0 RESET) are preserved verbatim below. No structural redesign; every changed line traces to the decimal-API incoherence or its directly-entailed allocation/latency-contract correction. Appendix D unchanged.

---

### v1.2 → v1.3 (Gate A round 3 converged)

Date: 2026-05-08. Source reviews: `research/reviews/codex_2c_3_codegen_review.md` (1 P1 / 1 P2 / 1 P3) and `research/reviews/opus_2c_3_codegen_adversarial_review.md` (judges every Codex finding "Confirm"; adds 1 new P1 + 0 new P2 + 0 new P3; 1 root cluster; verdict "BLOCKED at round cap" with closing recommendation "v1.3 can ship after a single convergence pass"). Opus's judgements on Codex findings were binding for this pass. **Round cap hit at round 3; user authorized one-pass post-cap convergence; no structural changes; pure prose/sketch line edits.**

**One root cluster — sibling-API divergence on `MessageView` typed-accessor surface (covers C-R3-P1-1 + N-R3-P1-1).** The invented `get_string<Tag>` / `get_char<Tag>` / `get_decimal<Tag>` family appeared in §4.7's typed-accessor codegen sketches (5 accessor bodies in `NewOrderSingle`) and in §4.8's `dict::reify` algorithm step 3 + the prose recap at the runtime-dispatch switch. The signed `[2b §4.3]` (lines 281–288) exposes only `get<Tag>() -> expected_t<field_view>` and `get(uint16_t) -> expected_t<field_view>`; typed decoding is layered outside `MessageView` via `dict::field_traits<...>` (declared here in 2c) and `decimal_t::from_chars` (declared in 2a). v1.3 rewrites the §4.7 sketches and the §4.8 algorithm + prose to call `view.template get<Tag>()` and decode via `dict::field_traits<T>::from_field_view(...)` (or, for `decimal_t`, `decimal_t::from_chars(fv->bytes())` per `[2a §4.2]`). The traits layer's formal home — the §4.1.3 sub-section declaring `dict::field_traits<T>` plus the `dict::decode_field<T>` helper — is added so the references in §4.7 / §4.8 compile against a single declaration. The §4.7 prose at line 988 (now expanded) was already correct ("each one is an `inline noexcept` shell over `wire::MessageView::get<Tag>()` with the field-traits dispatch baked in by codegen") — the sketches now match.

The other two findings (the Codex `ApplVerID=7 → v44` stragglers in §4.9 + §9 seam #10b; the cosmetic stale `unique_ptr<dict_metadata_handle>` aside in §8) are independent grep-level line edits with no structural cluster.

#### Per-finding resolution table

| Finding | Severity | Resolution | Section touched |
|---|---|---|---|
| C-R3-P1-1 | P1 | §4.8 `dict::reify` algorithm step 3 + the prose recap at the runtime-dispatch switch rewritten: replaced the invented `view.template get_string<1128>()` call with `view.template get<1128>()` (the actual `[2b §4.3]` typed-tag accessor returning `expected_t<field_view>`); decoding to `std::string_view` now goes through `dict::field_traits<std::string_view>::from_field_view(*fv)` from §4.1.3; `dict_field_not_present` from `get<1128>()` maps to an empty `std::string_view{}` for the resolution input. Citation `[2b §4.3]` retained (now accurate). The historical "no `view.appl_ver_id_or_empty()` accessor exists" parenthetical (added in v1.2) is removed in this pass since the new prose no longer needs the negative reference. | §4.8 (`dict::reify` algorithm step 3 prose; runtime-dispatch switch prose recap) |
| C-R3-P2-1 | P2 | §4.9 line 1477 prose + §9 seam #10b prose corrected from `ApplVerID=7 → v44` (FIX 5.0 wire value mis-mapped) to `ApplVerID=6 → v44` (FIX 4.4 wire value, matching the §4.3 mapping table at `7 = FIX 5.0 / v50` vs `6 = FIX 4.4 / v44`, and matching the §6.3 worked example's Frame 3 corrected in v1.2). The illustrative fact pattern (per-message override on a FIXT.1.1 session whose `default_appl == v50sp2`) is unchanged; only the wire value changes. | §4.9 (Use prose); §9 (seam #10b test description) |
| C-R3-P3-1 | P3 | §8 PMR-recap row (live shape) — struck the transient-`unique_ptr<dict_metadata_handle, pmr_deleter<...>>` aside that read as if it were part of the live shape. The row now states the `shared_ptr`-based live shape explicitly and notes `pmr_deleter<T>` is published in §4.3 as a generic PMR-aware deleter utility (no claim that the live `dict_metadata_handle` ownership uses `unique_ptr`). The historical Appendix C entries at v1.0 → v1.1 RC#2 (which correctly record what v1.0/v1.1 said) are NOT modified — they are valid as-history per the Opus review's explicit instruction. | §8 (PMR recap row) |
| N-R3-P1-1 | P1 | §4.7 typed-accessor codegen sketches (the 5 accessor bodies in `NewOrderSingle`: `cl_ord_id`, `symbol`, `side`, `order_qty`, `price`) rewritten to match the §4.7 prose at line 988 ("each one is an `inline noexcept` shell over `wire::MessageView::get<Tag>()` with the field-traits dispatch baked in by codegen"). Each body now calls `view_.template get<Tag>()` and decodes via either `dict::field_traits<T>::from_field_view(*fv)` (string/char) — factored through the `dict::decode_field<T>` helper for the cleaner sketches — or `fixpp::decimal_t::from_chars(fv->bytes())` (decimal, per `[2a §4.2]`). The expanded "get → check → decode" form is shown in full for `cl_ord_id` for clarity; the other accessors use the `decode_field` helper. The `field_value(uint16_t)` forwarder at the end of the class block already used the correct `view_.get(tag)` shape and is unchanged. The new §4.1.3 sub-section declares `dict::field_traits<T>` and `dict::decode_field<T>` as the formal home for the typed-decoding layer — referenced from §1.1 (already), §4.7 (now), and §4.8 (now); per `[2b §1]` line 28 / `[2b W-009]` line 806, this layer is 2c-owned. | §4.1.3 (new sub-section declaring `dict::field_traits<T>` + `dict::decode_field<T>`); §4.7 (5 accessor sketch bodies + accessor preamble comment) |

#### Disagreements (none)

The Opus pass judged every Codex round-3 finding "Confirm" — no Codex fixes were declined. All 4 findings (3 Codex + 1 new Opus, clustered by Opus's root-cause analysis into 1 root cluster + 2 stragglers) landed verbatim per the Opus review's counter-proposals.

#### Net-effect summary

Sections touched: status block (v1.2 → v1.3, convergence-log pointer); §4.1.3 (new sub-section declaring `dict::field_traits<T>` + `dict::decode_field<T>` as the formal home for typed decoding over `wire::field_view`); §4.7 (typed-accessor preamble comment expanded; 5 accessor bodies in `NewOrderSingle` rewritten to call `view_.template get<Tag>()` + field-traits dispatch / `decimal_t::from_chars`); §4.8 (`dict::reify` algorithm step 3 prose + the runtime-dispatch switch prose recap rewritten to use `view.template get<1128>()` + `dict::field_traits<std::string_view>::from_field_view(...)`); §4.9 (Use prose corrected from `ApplVerID=7` to `ApplVerID=6`); §8 (PMR recap row simplified — transient-`unique_ptr` aside struck); §9 seam #10b (test description corrected from `ApplVerID=7` to `ApplVerID=6`); Appendix C (this entry prepended). **Round cap hit at round 3; user authorized one-pass post-cap convergence; no structural changes; pure prose/sketch line edits.** The v1.2 surface shapes (`version_profile`, `resolved_message_version`, `dict_metadata_handle` shared-ownership, the dispatch-family split, A-014..A-034 amendment, the §6.3 worked example, the new `dict_no_dictionary_for_application_version` error variant) all carry forward intact. Doc grew ~70–90 lines (new §4.1.3 sub-section + new Appendix C entry; the §4.7 sketch bodies grew slightly; the §4.8 prose was rewritten in place); Appendix D unchanged from v1.2.

---

### v1.1 → v1.2 (Gate A round 2 converged)

Date: 2026-05-08. Source reviews: `research/reviews/codex_2c_2_codegen_review.md` (2 P1 / 1 P2 / 2 P3) and `research/reviews/opus_2c_2_codegen_adversarial_review.md` (judges every Codex finding "Confirm"; adds 0 new P1 + 1 new P2 + 2 new P3; 0 root causes; concludes "v1.2 can ship after a single convergence pass"). Opus's judgements on Codex findings were binding for this pass.

**No structural root causes — pure line edits.** All 8 findings are convergence-pass missed-edits or fence-post errors that the v1.1 rewrite left open: the keepalive ownership shape was internally inconsistent (`unique_ptr` primary + `shared_ptr` keepalive of the same object); §4.8 line 1339 still claimed `Move-ctor = default` despite the v1.1 class sketch declaring custom moves; `dict::reify` named `Dictionary::resolve_application_version(...)` from a context that has no `Dictionary`; §7.6 named the pre-split dispatch header; §7.3 + §11 still said "resolved-application-version"; §4.8 invented a non-existent 2b accessor `view.appl_ver_id_or_empty()`; the §6.3 worked example mis-mapped wire `ApplVerID(1128)` numeric values to `application_version` enum members; §4.9 reused `dict_unknown_appl_ver_id` for the registry's null-lookup case (a different failure mode from parse failure). v1.2 closes each one in place; the v1.1 structural shapes (`version_profile`, `resolved_message_version`, `dict_metadata_handle`, `pmr_deleter`, the dispatch-family split, A-014..A-034 amendment, the metadata-handle lifetime root, A-013-stop scope) carry forward unmodified.

#### Per-finding resolution table

| Finding | Severity | Resolution | Section touched |
|---|---|---|---|
| C-R2-P1-1 | P1 | Path (a) keepalive ownership: change `dict_metadata_handle_ptr` from `unique_ptr<dict_metadata_handle, pmr_deleter<...>>` to `shared_ptr<const dict_metadata_handle>` allocated via `std::allocate_shared` over `std::pmr::polymorphic_allocator<dict_metadata_handle>`. The `with_overlay` sharing path's `base_keepalive_` slot now holds a *copy* of the base's `handle_` (same control block). `pmr_deleter<T>` template stays published as a utility (used internally by `allocate_shared`'s control block); the public alias is shared-pointer-based. `Dictionary` move stays no-throw and allocates nothing (`shared_ptr` move touches no atomics). `table_view` lifetime contract restated: alive while *any* `Dictionary` (via `handle_` or `base_keepalive_`) referencing the metadata block is alive. | §4.3 (alias declaration + `dict_metadata_handle` member layout + `Dictionary::handle_` private member + class block-comment + `with_overlay` block-comment + post-class prose + Dictionary lifecycle move-ctor comment), §4.6 (`table_view` lifetime contract), §6.5 (lifetime root prose), §8 (PMR recap row + lifetime-root prose), §9 seam #13 (drop-base-keep-merged refcount semantics) |
| C-R2-P1-2 | P1 | §4.8 *Key properties* last bullet rewritten to match §6.6 contract bullet 4: "Move is custom `noexcept` (not `= default`); destination caches initialize to `std::nullopt`; source caches are explicitly `reset()`; first `view()` after move rebuilds against post-move `bytes_.data()`. See §6.6 contract bullet 4 and §9 seam #14." Replaces the stale "Move-ctor is `= default` and safe under the lazy-view design (per N-P1-3)" wording. | §4.8 line 1339 *Key properties* bullet |
| C-R2-P2-1 | P2 | Path (a): published a free function `dict::resolve_application_version(version_profile, std::string_view) noexcept` in `<fixpp/dict/version_profile.hpp>`. Algorithm is profile-only (no `Dictionary` needed). `Dictionary::resolve_application_version` becomes a thin wrapper that delegates to the free function with `this->which()`. `dict::reify`'s algorithm step 3 now calls the free function. | §4.3 (free function declaration + member docstring "thin wrapper" note + wire→C++ enum mapping table inline), §4.8 (line 1110 algorithm step rewritten; mid-§4.8 prose around `Runtime-dispatch switch` updated; `dict::reify` two-stage walk caption updated) |
| C-R2-P3-1 | P3 | §7.6 `fixpp::dict::dispatch` row updated to name both post-split dispatch headers (`reify_dispatch_fixt.hpp` for the 7 FIXT admin MsgTypes + `reify_dispatch_application.hpp` for the ~470 application (version, MsgType) pairs). | §7.6 table row |
| C-R2-P3-2 | P3 | §7.3 line 1587 + §11 hand-off line 1698 changed "resolved-application-version" → "resolved-message-version". §5 commitment 1 was already correct; this aligns the cross-references with the structural truth. | §7.3 (the `fixpp_msg_t` summary bullet), §11 (the 2i hand-off bullet) |
| N2-P2-1 | P2 | §4.8 line 1110 algorithm step 3 rewritten to call the actual `[2b §4.3]` API: `view.template get_string<1128>()`, mapping `dict_field_not_present`-style absent error to empty string for the resolution input. The invented `view.appl_ver_id_or_empty()` accessor is removed everywhere it appeared. | §4.8 (`dict::reify` algorithm step 3) |
| N2-P3-1 | P3 | §6.3 worked example Frame 3 wire value corrected: `1128=6` (was `1128=7`); resolution line corrected to `resolve_application_version(profile, "6") → v44`. Frames 2 and 4–5 verified spec-correct (Frame 2's `1128=9 → v50sp2`, Frame 4's no-`ApplVerID` → session default, Frame 5's `MsgType=0` → vt11 admin). The wire `ApplVerID(1128)` → C++ `application_version` mapping table added inline in §4.3 immediately after the `dict::resolve_application_version` free-function declaration so a future implementer doesn't recreate the conflation. Cites `[FIXT §5.1]` `DefaultApplVerID` and `[FIXT §5.3]` `ApplVerID` per message. | §4.3 (mapping table), §6.3 (Frame 3 wire value + resolution line; Frame 2 + 4 resolution lines updated to free-function call signature for consistency) |
| N2-P3-2 | P3 | New error variant `dict_no_dictionary_for_application_version` added to §6.7 errors table, scoped to the `version_registry::get(application_version)` null-lookup case (the wire string parsed but the engine config didn't load the Dictionary). The existing `dict_unknown_appl_ver_id` row stays scoped to the parse-failure path on the wire string. §4.9 docstring updated to name the new variant; the §6.7 table tally line updated from "19 variants" to "20 variants" (+1 net; no removals). | §4.9 (`version_registry::get` docstring), §6.7 (new errors-table row + tally line) |

#### Disagreements (none)

The Opus pass judged every Codex round-2 finding "Confirm" — no Codex fixes were declined. All 8 findings (5 Codex + 3 new Opus) landed verbatim per the Opus review's counter-proposals.

#### Net-effect summary

Sections touched: status block (status + convergence-log pointer), §4.3 (free function `dict::resolve_application_version` published; wire→C++ enum mapping table; `dict_metadata_handle_ptr` flipped from `unique_ptr` to `shared_ptr`; `pmr_deleter<T>` repositioned as utility; `Dictionary` class block-comment + `with_overlay` block-comment + post-class prose + `handle_` private member comment + Dictionary lifecycle move-ctor comment all updated; member `Dictionary::resolve_application_version` documented as thin wrapper), §4.6 (`table_view` lifetime contract restated), §4.8 (line 1110 algorithm step 3 rewritten; line 1339 key-properties bullet rewritten; mid-§4.8 prose updated), §4.9 (`version_registry::get` docstring named the new error variant), §6.3 (Frame 3 wire value `7 → 6`; resolution lines updated to free-function form), §6.5 (lifetime root prose updated), §6.7 (new error-variant row + tally line), §7.3 + §11 (resolved-application-version → resolved-message-version), §7.6 (split-dispatch headers in CMake target row), §8 (PMR recap row + lifetime-root prose updated to shared-pointer shape), §9 seam #13 (refcount sub-step added). No structural root causes; pure line edits. The v1.1 surface shapes carry forward intact; v1.2 closes the 8 fence-post / line-edit findings the v1.1 convergence pass left open. Doc grew ~50–80 lines (free-function declaration; wire→C++ mapping table; new error variant; new Appendix C entry; algorithm rewordings); Appendix D unchanged from v1.1.

---

### v1.0 → v1.1 (Gate A round 1 converged)

Date: 2026-05-08. Source reviews: `research/reviews/codex_2c_v1_codegen_review.md` (5 P1 / 3 P2 / 2 P3) and `research/reviews/opus_2c_v1_codegen_adversarial_review.md` (judges every Codex finding; adds 2 new P1 + 7 new P2 + 3 new P3; 3 root causes; concludes "v1.1 can ship after a single convergence pass"). Opus's judgements on Codex findings were binding for this pass.

**3 root causes — applied as the highest-leverage structural fixes:**

- **RC#1 — Resolved-message-version axis incompletely typed.** Introduced `dict::resolved_message_version` (4-byte struct: `kind ∈ {session_admin, application}`, `session_version`, `application_version`) in §4.3. `Dictionary::resolve_application_version` errors with `dict_unresolved_application_version` when the result would be `Unknown` (no more `Unknown` as a success sentinel). `dict::reify` peeks `MsgType`, tests against the FIXT admin set (7 names), dispatches through `_codegen/include/fixpp/_dispatch/reify_dispatch_fixt.hpp` (7 cases) for session-admin or `_codegen/include/fixpp/_dispatch/reify_dispatch_application.hpp` (~470 cases) for application. `owning_message_handle::version()` returns `resolved_message_version`. `dict_reify_version_mismatch` dropped from `reify_as<Msg>` (caller-bug, not engine-validation; `MessageView` carries no resolved-version inputs). Test seam #15 split into 15a (vt11 admin), 15b (application across 4 versions), 15c (`dict_unresolved_application_version` propagation). Closes Codex C-P1-2 + C-P1-5 + C-P2-3 + C-P3-2.

- **RC#2 — `dict_metadata_handle` ownership declared in API, not just prose.** Published `dict::detail::pmr_deleter<T>` and `using dict_metadata_handle_ptr = std::unique_ptr<dict_metadata_handle, pmr_deleter<dict_metadata_handle>>;` in §4.3. `Dictionary::handle_` uses `dict_metadata_handle_ptr`. The `with_overlay` sharing path stores a `shared_ptr<const dict_metadata_handle>` to the base in the merged handle's `base_keepalive_` slot (allocated via `std::allocate_shared` over a `pmr::polymorphic_allocator` so the shared-control-block deallocator returns memory to the base's `mr`). `dict_metadata_handle`'s member layout is published (no longer just forward-declared). §8 PMR recap and §6.5 lifetime root prose updated to match. `table_view`'s lifetime is explicitly tied to the heap-pinned handle. Closes Codex C-P1-3 + Opus N-P1-1.

- **RC#3 — Catalogue / architecture / Appendix D triangle made consistent.** Picked path B (extend amendment, do not restore A-014..A-034). Appendix D §3 adds `[const §XVIII.7]` ("Application-message codegen scope for v1.0" — A-014..A-034 codegen-deferred to v1.x) and `[arch §4.2]` row 2c amendment (line 206 enumeration changed from "A-001..A-034" to "A-001..A-013 (codegen). A-014..A-034 (runtime-XML only in v1.0; codegen deferred to v1.x)"). Appendix D §2 adds the coverage-index supplemental note for D-008 (orchestrator applies the `coverage-index.md` edit on sign-off; 2c rewrite agent does NOT edit `coverage-index.md` directly). §1.3 amendments spell out A-014..A-034 disposition. Appendix A — A-014..A-034 row added with `[deferred to v1.x — codegen]` and `[v1.0 — runtime-XML only]` markers. Closes Codex C-P1-1 + C-P2-2.

#### Per-finding resolution table

Codex v1.0 findings (5 P1 / 3 P2 / 2 P3):

| Finding | Severity | Resolution | Section touched |
|---|---|---|---|
| C-P1-1 | P1 | RC#3 path B: extend Appendix D §3 with `[const §XVIII.7]` + `[arch §4.2]` row 2c amendment; do not restore A-014..A-034 to typed-class scope | §1.3, Appendix A, Appendix D §3 |
| C-P1-2 | P1 | RC#1: introduce `resolved_message_version` discriminator; split dispatch into FIXT-admin + application | §4.3, §4.8, §6.3, §6.6 |
| C-P1-3 | P1 | RC#2: publish `dict_metadata_handle_ptr` with `pmr_deleter`; layout the handle's members; the `with_overlay` keepalive lives inside the handle as `shared_ptr<const dict_metadata_handle>` | §4.3, §6.5, §8 |
| C-P1-4 | P1 | Custom `noexcept` move ctor + move-assign for `owning_<Msg>` that explicitly resets both source's and destination's `frame_cache_` / `view_cache_` `optional`s; covered jointly with N-P1-2 | §4.8, §6.6, seam #14 |
| C-P1-5 | P1 | RC#1: `Dictionary::resolve_application_version` errors with `dict_unresolved_application_version`; `Unknown` is no longer a successful return value | §4.3, §6.7, seam #15c |
| C-P2-1 | P2 | Seam #16 extended with bounded-PMR cases for `XmlLoader::load_from_string` and `load_overlay_from_string`; assert `dict::xml_oom_error` is *thrown* (XmlLoader is exception-API per §6.1.1, not `expected_t`) | seam #16 |
| C-P2-2 | P2 | RC#3: coverage-index supplemental note attached to D-008 (Appendix D §2); `feature-catalogue.md` D-008 row title untouched per locked decision | §1.3, Appendix A D-008 row, Appendix D §2 |
| C-P2-3 | P2 | RC#1: drop `dict_reify_version_mismatch` from `reify_as<Msg>` (caller named `Msg::version_v` by picking `Msg`; mismatch is caller bug, not engine validation; `MessageView` carries no resolved-version inputs); error variant removed from §6.7 | §4.8, §6.6, §6.7 |
| C-P3-1 | P3 | Status-block citations replaced: `[FIX42]` / `[FIX44]` rewritten with the catalogue's actual `Spec ref` strings (`feature-catalogue.md` line 70 family); per-message exhaustive references stay routed through generated `_codegen/include/fixpp/<vXX>/NormativeReferences.md` | status block |
| C-P3-2 | P3 | RC#1: seam #15 split into 15a (vt11 admin) + 15b (application across 4 versions) + 15c (`dict_unresolved_application_version`); seam #15 is now implementable as written for vt11 | seam #15 |

Opus v1.0 new findings (2 new P1 + 7 new P2 + 3 new P3):

| Finding | Severity | Resolution | Section touched |
|---|---|---|---|
| N-P1-1 | P1 | RC#2: `pmr_deleter` declared; `dict_metadata_handle_ptr` published; the merged handle's keepalive lives inside the handle, not on the `Dictionary` value | §4.3, §6.5, §8 |
| N-P1-2 | P1 | `owning_<Msg>` documented as **single-strand-only** (lazy `view()` cache races on first-access concurrency); §4.8 + §6.1 spell this out; §6.1's "thread-safe-on-read" claim explicitly carves `owning_<Msg>` out; custom `noexcept` move ctor + move-assign per C-P1-4 | §4.8, §6.1, §6.6, seam #14 |
| N-P2-1 | P2 | Status-block "Owner: Opus" replaced with "Owner role: 2c codegen design lead" (impersonal, matches `[2a §0]` / `[2b §0]` style) | status block |
| N-P2-2 | P2 | §5 commitment 5 rephrased: "2i may expose a session-level version getter at its discretion; 2c's `version_profile.session` is the value that getter would return"; dropped unilateral commitment to a 2i symbol | §5 |
| N-P2-3 | P2 | §6.1 + §4.3 prose around concurrent `with_overlay` clarified: concurrent calls on the same *base* are safe (atomic ref-count); concurrent calls on the same *overlay value* are UB (`pmr::vector::push_back` race during build) | §4.3, §6.1 |
| N-P2-4 | P2 | `dict::reify_as<Msg>` PMR allocation budget re-derived against the §4.8 declaration; 4 allocations itemised: `bytes_` + `unique_ptr<OffsetTable>` for the OffsetTable object + entry array + hash overlay; the v1.0 phantom `pmr::vector` for view rebuild is dropped | §1.2, §6.6 |
| N-P2-5 | P2 | §6.1 typed-flyweight concurrency claim weakened from "trivially copyable" to "trivially-copy-constructible and address-immutable across copies"; trivial-copyability false in std C++ for class with reference member | §6.1 |
| N-P2-6 | P2 | Per-version `Reify.hpp` cost (~370 KiB) separated from dispatch-shared cost (~50 KiB total split between `reify_dispatch_fixt.hpp` and `reify_dispatch_application.hpp`); per-version cost no longer double-counts the dispatch switch across four versions | §1.2 |
| N-P2-7 | P2 | `dict::version_registry` shape published in new §4.9; `get(application_version) → expected_t<Dictionary const*>`; FSM uses it for cross-version per-message-override resolution; §10 Q10 defers ownership-model question to 2d threading + EngineConfig design | §4.9, §6.3, §10 Q10 |
| N-P3-1 | P3 | §6.1 parenthetical at L1156 disambiguated: "(across the per-message-arena reset boundary the underlying view is *not* alive — concurrency requires `dict::reify_as` first to materialize an `owning_<Msg>`)" | §6.1 |
| N-P3-2 | P3 | §1.2 per-version-field-table sizing tied via explicit cross-reference to `[2b §1.2]`'s per-occurrence sizing pattern | §1.2 |
| N-P3-3 | P3 | `FIXPP_APPL_VER_UNKNOWN = 0` added to §5 commitment 5 C-ABI constant list | §5 |

#### Disagreements (none)

The Opus pass judged every Codex finding "Confirm" — there were no "Disagree" judgements in this round. No Codex fixes were declined.

#### Net-effect summary

Sections touched: status block (citations + owner role), §1.2 (size budgets re-derived against declared shape), §1.3 (A-014..A-034 disposition spelled out), §4.3 (`resolved_message_version` introduced; `pmr_deleter` + `dict_metadata_handle_ptr` published; `dict_metadata_handle` layout published; concurrent-`with_overlay` wording refined; `resolve_application_version` errors on Unknown), §4.8 (custom move ctor; reify entry-points updated; dispatch sketches split), new §4.9 (`version_registry`), §5 commitments 1 + 5 (resolved-message-version exposed; `FIXPP_APPL_VER_UNKNOWN`; 2i-getter commitment softened), §6.1 (single-strand-only carve-out; trivial-copy wording weakened; concurrent-overlay wording refined; parenthetical disambiguated), §6.3 (worked example uses `resolved_message_version`), §6.5 (lifetime root prose matches declared shape), §6.6 (allocation budget itemised; errors updated), §6.7 (drop `dict_reify_version_mismatch`; add `dict_unresolved_application_version`), §8 (PMR recap matches declared shape), §9 (seam #14 + seam #15 split + seam #16 extended), §10 (new Q10 for `version_registry` ownership model), Appendix A (A-014..A-034 row added; D-008 row references supplemental note), Appendix D (§2 reframed to coverage-index supplemental note; §3 split into §3.1 / §3.2 / §3.3 with `[const §XVIII.7]` and `[arch §4.2]` row 2c amendments). No structural section was rewritten; the v1.0 surface shapes carry forward into v1.1. The `version_profile` axis is now end-to-end concretely modelled (kind, session, application bytes; `pmr_deleter`; `base_keepalive_`; `resolved_message_version`); the dispatch surface honours both vt11 admin and application messages; the `owning_<Msg>` move + cache contract is testable as written; the catalogue/architecture/coverage-index triangle is internally consistent.

---

### v0.1 → v1.0 (RESET)

Records the v0.1 → v1.0 RESET. v0.1 archived as `2c-codegen.draft-r1.md`. Full rewrite triggered by Opus closing recommendation (`research/reviews/opus_2c_codegen_adversarial_review.md`); user signed off Root cause #4 constitutional decision before the reset re-spawn.

### Summary

v0.1 was structurally sound at the surface level (per-version namespaces, the `FieldRef`/`ComponentRef`/`GroupRef` shape, the `DialectOverlay` value-typed default, the typed-flyweight + `owning_message_t<>` split, the §6.7 errors-introduced sub-table) but had six structural problems that were not local fixes: a constitutional version-scope drift, an incomplete model for FIXT.1.1's split-vocabulary case, a split-brained `reify` shape, a lifetime-pinning hole around `Dictionary` move semantics, `noexcept`-`expected_t` unsoundness over PMR allocations, and a dialect-overlay grammar that was structurally too narrow for `[SYN §3.3 Q13]`'s claims. The interfaces were keepable; the semantics under those interfaces were rebuilt. v1.0 keeps the public surface shapes (5 public surfaces + the typed flyweights + `owning_message_t<>` + `table_view`) and rebuilds the version model (`version_profile`), the `reify` bridge (free function templates in `<fixpp/dict/reify.hpp>`), the metadata-handle lifetime root, the catalogue mapping, the `trap_throw` discipline, and the dialect-overlay grammar closure.

### Per-finding resolution

Codex P1s (5/5 confirmed by Opus pass; all landed in v1.0):

| Finding | Resolution |
|---|---|
| C-P1-1 — restore full version set | Folded into RC-4. v1.0 §1.3 spells out the codegen-vs-runtime-XML split; Appendix D drafts the constitutional amendment. |
| C-P1-2 — FIXT.1.1 split-vocabulary | Folded into RC-1. v1.0 introduces `version_profile` + `Dictionary::resolve_application_version`; §6.3 carries the worked example; test seam #10b exercises cross-vocabulary dispatch. |
| C-P1-3 — `MessageView::reify` handoff | Folded into RC-2. v1.0 publishes `dict::reify_as<Msg>` + `dict::reify` free functions in `<fixpp/dict/reify.hpp>`; the bare 2b reference becomes a documented synonym. |
| C-P1-4 — no PMR leak in C-ABI | §5 rewritten to speak in C-shape only; engine-owned default arenas with explicit destroy semantics; no `mr` parameter on any commitment. |
| C-P1-5 — `table_view` lifetime | Folded into RC-3. v1.0 heap-pins the metadata block via `dict::detail::dict_metadata_handle`; `Dictionary` carries `unique_ptr<handle>` (or `shared_ptr`-flavoured for `with_overlay`); `dict_table_view_stale` deleted; stale access is debug-trap / release-UB. |

Codex P2s (7 — all addressed):

| Finding | Resolution |
|---|---|
| C-P2-1 — recompute static footprint | §1.2 corrected: ~3500 (MsgType, tag) entries × 16 bytes = ~56 KiB per codegen version; ~224 KiB for four versions. |
| C-P2-2 — XML / overlay DoS bounds | §4.4.2 caps: max XML bytes 4 MiB, max nesting depth 32, no external entities by default, cyclical-component detection, max overlay additions 1024 / 64 / 64. |
| C-P2-3 — `noexcept` `expected_t` PMR unsoundness | RC-5 / §6.1.1 — `trap_throw` discipline; new error variants `dict_overlay_oom`, `dict_reify_oom`, `dict_xml_oom`. |
| C-P2-4 — conditional rules in dialect overlays | RC-6 / §4.4.1 — explicit reject; new error `dict_overlay_unsupported_rule`. |
| C-P2-5 — programmatic `DialectOverlay` PMR pinning | §4.4: `DialectOverlay::create(mr)` factory; default ctor deleted; test seam #17 verifies. |
| C-P2-6 — `dict_table_view_stale` reachability | Variant deleted; stale-access is debug-trap / release-UB per RC-3. |
| C-P2-7 — Appendix A catalogue mapping | Rewritten from scratch against actual catalogue text per RC-4. |

Codex P3s (3 — 1 escalated to P2 by Opus, 2 confirmed):

| Finding | Resolution |
|---|---|
| C-P3-1 — `_reserved` discipline | §4.1 / §4.2 / §4.3: zero on emit, ignore on read, future use under `FIXPP_DICT_*_RESERVED_USED` macros (uniform across `FieldRef`, `ComponentRef`, `GroupRef`, `version_profile`). |
| C-P3-2 — generated `NormativeReferences.md` | Appendix B cites the generated file; §10 Q7 tracks the codegen tool enhancement. |
| C-P3-3 — CMake target shape (escalated to P2) | §7.6 — independently linkable per-version targets (`fixpp::dict::v42`, `v44`, `v50sp2`, `vt11`) + umbrella (`all_versions`) + runtime (`runtime`) + dispatch (`dispatch`). |

Opus new P1s (4 — all addressed):

| Finding | Resolution |
|---|---|
| N-P1-1 — `Dictionary` move invalidates `table_view` | RC-3 / §4.3 — heap-pinned metadata-handle. |
| N-P1-2 — overlay-promoted tag access | §4.7.1 — `field_value(uint16_t)` forwarder on every typed message; test seam #20. |
| N-P1-3 — `owning_<Msg>` member aliasing under move | §4.8 — lazy-view design; `view()` computed on access; cache invalidated on move; rebuilt on first access; test seam #14. |
| N-P1-4 — `table_view` public-or-internal status | §4.6 — public, value-typed, non-friend-constructible (engine-supplied only). |

Opus new P2s (6 — all addressed):

| Finding | Resolution |
|---|---|
| N-P2-1 — drop `constexpr` from per-tag accessors | §4.7 — `inline noexcept` only; `constexpr` retained on `msg_type_v` / `version_v`. |
| N-P2-2 — split accessor latency by type | §6.2 — string/int/char ≤ 20 ns; decimal ≤ 75 ns. |
| N-P2-3 — relax all-versions compile ceiling | §1.2 — single-version ≤ 3 s; all-versions soft ≤ 15 s, "not supported by default." |
| N-P2-4 — `Dictionary` lifecycle | §4.3 / §6.1 — move-only-on-init, frozen-after-handoff, thread-safe-on-read; `with_overlay` single-threaded. |
| N-P2-5 — pin reify allocation count | §1.2 / §6.6 — relaxed to ≤ 4 PMR allocations (no 2b co-amendment needed). |
| N-P2-6 — mid-session swap categorically rejected | §7.2; Appendix D §5 amends `[arch §5.6]`. |

Opus new P3s (3 — all addressed):

| Finding | Resolution |
|---|---|
| N-P3-1 — §4.1 example tag | Switched to `OrderID(37)` (genuinely differs in presence across MsgTypes). |
| N-P3-2 — test seam #10 split | Split into 10a (multi-session) + 10b (cross-vocabulary on a single FIXT.1.1) + 10c (runtime-XML-only round-trip). |
| N-P3-3 — `with_overlay` complexity | §6.4 — O(N_base + N_overlay log N_overlay); test seam #4 verifies. |

### Net effect by section

Sections rewritten: §1 (goals; new §1.3 version coverage), §2 (non-goals), §3 (inheritance — clarified with `[arch §5.6]` narrowing), §4.3 (`Dictionary` — `version_profile`, metadata-handle, `resolve_application_version`), §4.4 (`DialectOverlay` — factory, grammar closure §4.4.1, caps §4.4.2), §4.5 (`XmlLoader` — caps), §4.6 (`table_view` — visibility, lifetime), §4.7 (typed messages — drop `constexpr`, `field_value` forwarder, §4.7.1), §4.8 (`owning_message_t<>` + `dict::reify` bridge — full rewrite per RC-2; lazy-view), §5 (C ABI — C-shape only), §6.1 (alloc / threading / `trap_throw` §6.1.1), §6.2 (latency split), §6.3 (multi-version with worked example), §6.4 (merge complexity), §6.5 (lifetime root), §6.6 (reify contract), §6.7 (errors sub-table), §7.1 (wire integration with grammar-closure note), §7.2 (mid-session swap rejected), §7.6 (CMake target shape — new), §8 (PMR recap), §9 (test seams expanded to 20).

New sections vs v0.1: §1.3, §4.4.1 (grammar closure), §4.4.2 (caps), §4.7.1 (overlay-promoted access), §6.1.1 (`trap_throw`), §7.6 (CMake targets), Appendix D (proposed amendment).

Deleted from v0.1: `dict_table_view_stale` error variant; v0.1's `dict::version` single-byte enum (replaced by `version_profile`); v0.1's claim that A-001..A-034 maps to "minus A-024" universal coverage (corrected against actual catalogue ranges, e.g., A-008..A-013 are 4.4+); v0.1 Appendix A's invented row meanings.

## Appendix D — Proposed Constitutional Amendment

Per RC-4: drafted here for the orchestrator to apply on sign-off. **2c v1.0 does not edit `constitution.md` or `architecture.md` directly.** The orchestrator (parent session) applies the amendment text below during the sign-off commit, after the user reviews the proposed amendment language alongside the 2c v1.0 sign-off. Format matches the existing constitution / architecture amendment style per `[const §XX]`.

### §1 — Amendment to `[const §I.1]` (v1.0 version surface)

**Current text (`[const §I.1]`):**

> 1. **`fixpp` is a modern C++23 implementation of the FIX protocol.** Session layer + application layer for FIX 4.0 through 5.0SP2 + FIXT.1.1. v1.0 ships 100% of the official spec for those versions; FIX Latest, FIXP, SBE, FAST, SOFH, JSON, GPB, and FIX MMT are post-1.0 milestones (Article XVIII).

**Proposed text (`[const §I.1]` v0.2):**

> 1. **`fixpp` is a modern C++23 implementation of the FIX protocol.** Session layer + application layer for FIX 4.0 through 5.0SP2 + FIXT.1.1. v1.0 ships 100% of the official spec for those versions, with the following codegen-vs-runtime split:
>    - **Codegen scope (per `[2c §1.3]`):** FIX 4.2, FIX 4.4, FIX 5.0 SP2, FIXT.1.1. Typed-message classes, `constexpr` field metadata, per-message validators, `dict::reify` runtime-dispatch all generated under per-version namespaces (`fixpp::v42`, `fixpp::v44`, `fixpp::v50sp2`, `fixpp::vt11`).
>    - **Runtime-XML scope:** FIX 4.0, FIX 4.1, FIX 4.2, FIX 4.3, FIX 4.4, FIX 5.0, FIX 5.0 SP1, FIX 5.0 SP2, FIXT.1.1. `dict::XmlLoader` accepts QuickFIX-XML for any of these; runtime `Dictionary` works for field/required/group/length-pair lookups; users access fields through the runtime tag-keyed accessor.
>    The runtime-XML-only versions (4.0 / 4.1 / 4.3 / 5.0 / 5.0 SP1) ship without a typed-message namespace in v1.0. Per-version codegen for those versions is deferred to post-v1.0 best-effort per Article XVIII §6.
>
>    FIX Latest, FIXP, SBE, FAST, SOFH, JSON, GPB, and FIX MMT are post-1.0 milestones (Article XVIII).

### §2 — Coverage-index supplemental note for D-008 (per RC#3 / C-P2-2)

`feature-catalogue.md`'s D-008 row title (line 77) is **left intact** per the locked decision: the catalogue text stands. Instead, `library/spec/coverage-index.md` receives a single supplemental note attached to D-008 that records the codegen-vs-runtime-XML disposition. The orchestrator applies this `coverage-index.md` edit during the sign-off commit, **alongside** the constitutional amendments below; the 2c rewrite agent does NOT edit `coverage-index.md` directly.

**Supplemental note text (to append to `library/spec/coverage-index.md` D-008 entry):**

> **D-008 supplemental:** Codegen scope for v1.0 = FIX 4.2, FIX 4.4, FIX 5.0 SP2, FIXT.1.1. Runtime-XML-only scope = FIX 4.0, FIX 4.1, FIX 4.3, FIX 5.0, FIX 5.0 SP1. The row title in `feature-catalogue.md` covers the broader 4.0–5.0 SP2 surface; codegen vs runtime-XML disposition lives here, in the coverage index. Per `[2c §1.3]` and Appendix A.

`[const §VI]` itself does not need editing; the catalogue rows D-001..D-011 retain their current titles. The 2c v1.x doc (`[2c §1.3]` and Appendix A) records the dispositions; the coverage-index supplemental note is the bidirectional-traceability anchor (per `[const §VI.4]`).

### §3 — Amendments to `[const §XVIII]` (post-v1 roadmap) + `[arch §4.2]` row 2c (per RC#3 / C-P1-1)

#### §3.1 `[const §XVIII.6]` — Post-v1.0 codegen for runtime-XML-only versions (existing v1.0 amendment, retained)

Add a new sub-clause to Article XVIII:

> 6. **Post-v1.0 codegen for runtime-XML-only versions.** FIX 4.0, FIX 4.1, FIX 4.3, FIX 5.0, and FIX 5.0 SP1 ship in v1.0 with runtime-XML support only (no per-version codegen namespace). Per-version codegen for these versions is post-v1.0 best-effort, prioritised at the discretion of the maintainer team based on observed downstream demand. The recommended priority order is: FIX 4.3 first (most-used legacy version in the post-v1 backlog), 5.0 SP1 second, 5.0 third, 4.0 / 4.1 last (vanishingly few production deployments). Each version's codegen is its own minor-version Spec Kit cycle, gated by the same Tier 1 quality bar.

#### §3.2 `[const §XVIII.7]` — Application-message codegen scope for v1.0 (new in v1.1 per RC#3)

Add a further new sub-clause to Article XVIII:

> 7. **Application-message codegen scope for v1.0.** Application-message rows A-014..A-034 are codegen-deferred to v1.x for the four codegen versions. v1.0's typed-message scope under `fixpp::v42`, `fixpp::v44`, `fixpp::v50sp2` is A-001..A-013 plus the M-/P-/C-/R-/N- families per the catalogue. Runtime-XML access to A-014..A-034 via `view.get(uint16_t tag)` ships in v1.0 across all 9 supported FIX versions; typed accessors for those messages land in v1.x. The deferred set comprises (per `feature-catalogue.md` lines 291–311) BusinessMessageReject (A-014, 35=j), DontKnowTrade (A-015, 35=Q), the ListCancel/Execute/Status family (A-019), the SecurityList family (A-025, 35=v/w/x/y), XMLnonFIX (A-034, 35=n), and similar additional order-management variants; A-024 stays dropped as a duplicate per `[SYN §4.4]`.

#### §3.3 `[arch §4.2]` — Catalogue rows owned, line 206 amendment (new in v1.1 per RC#3)

Replace the line 206 enumeration in `architecture.md` `[arch §4.2]` row 2c:

**Current text:**

> Application-message generated typed-message classes and `constexpr` field metadata… A-001..A-034 (order-management).

**Proposed text:**

> Application-message generated typed-message classes and `constexpr` field metadata… A-001..A-013 (order-management; codegen). A-014..A-034 (additional order-management variants; runtime-XML only in v1.0; codegen deferred to v1.x — see `[const §XVIII.7]`).

(Re-numbering: `[const §XVIII]` existing sub-clauses 1–5 retain their numbers; sub-clauses 6 (§3.1 above) and 7 (§3.2 above) are appended.)

### §4 — Amendment procedure framing (`[const §XX]`)

This amendment follows `[const §XX]` standard procedure:

- PR titled `Constitution + architecture + coverage-index: amend §I.1, §XVIII.6, §XVIII.7, [arch §4.2] row 2c, [arch §5.6]; coverage-index D-008 supplemental — clarify codegen-vs-runtime-XML split and A-014..A-034 deferral for v1.0`.
- PR description: this Appendix D's text (§1, §2, §3.1–§3.3, §5), plus the 2c v1.1 Gate A round 1 sign-off rationale.
- Codex Gate A review on the amendment PR.
- User signs off.
- `_log.md` records the amendment with date and source (this 2c v1.1 sign-off).

The amendment is **backwards-incompatible** for any consumer who interpreted `[const §I.1]`'s "FIX 4.0 through 5.0SP2 + FIXT.1.1" as "all nine versions get typed codegen in v1.0," and for any consumer who interpreted `[arch §4.2]` row 2c's "A-001..A-034" as "all 34 application-message rows get typed codegen in v1.0." Both are documented as such per `[const §XX].4`; an entry in `CHANGELOG.md` notes the constitutional clarifications. No `v-major` bump is triggered because v1.0 is not yet released; the amendments land during the v1.0 development window.

### §5 — Amendment to `[arch §5.6]` (mid-session swap wording)

**Current text (`[arch §5.6]`):**

> - **`SessionConfig` is value-typed and frozen at session open.** No mid-session reconfiguration of: dictionary, security profile, message store, executor, lock policy. Mutating ops (e.g., pinset rotation, dialect overlay swap) go through their own APIs and are explicitly thread-aware.

**Proposed text (`[arch §5.6]` v0.3):**

> - **`SessionConfig` is value-typed and frozen at session open.** No mid-session reconfiguration of: dictionary, security profile, message store, executor, lock policy, dialect overlay. The supported pattern for any of these is close-and-reopen the session. Mutating ops on session-adjacent state that *do* admit mid-session change (e.g., pinset rotation per `[const §XII]`) go through their own APIs and are explicitly thread-aware. **Mid-session dialect-overlay swap is rejected categorically per `[2c §7.2]`** — there is no `Session::swap_dialect_overlay(...)` API in v1.0. The v0.2 wording's parenthetical "dialect overlay swap" is removed from the list of supported mutating ops.

Per N-P2-6: the v0.2 phrasing implied a mid-session swap API existed; 2c v1.0 explicitly rejects this. The amendment aligns the architecture wording with the 2c surface.

