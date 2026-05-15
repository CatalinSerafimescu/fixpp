---
id: 003-dictionary-codegen
title: Dictionary codegen — `fixpp-codegen` + per-version typed messages + `dict::reify` bridge
module: dictionary/
phase: 4
status: drafted
verdict: TBD
spec_kit_step: /specify
last_updated: 2026-05-15
owner: fixpp::dict + fixpp::vXX (C++); second Phase 4 entry into the `dictionary/` module (D-008)
inherits_design: .specify/2c-codegen.md (v1.3, signed off 2026-05-10)
catalogue_rows: D-008 (code-generated constexpr field metadata — four codegen versions only), OSS-010 (header-only generated typed messages with constexpr field metadata)
blocked_on_replan: yes — Gate A round 1 (2026-05-15) surfaced two structural root causes that exceed a convergence text patch (see §8 "Upstream dependency audit" + plan.md `## Gate A`): RC#1 the `version_profile` / `resolve_application_version` / `field_traits`+`decode_field` surface is unshipped + unassigned (002 deferred it, 2c assigns no owner) → must be claimed as 003-owned with new files/ACs/error-taxonomy at re-`/plan`; RC#2 the inherited `[2c §4.1.3]`/§4.7 `decimal_t::from_chars` shape is incoherent with merged 2a/001 (no such symbol; PMR mandatory) → 2c must be reopened (not bundle-fixable).
gate_a_required: yes (touches public C++ API, codegen output surface, multi-version dispatch, cross-strand handoff bridge)
---

# 003-dictionary-codegen — codegen tool + per-version typed messages + reify bridge

> **/specify scope.** This spec captures *what* the dictionary codegen feature produces and *why*. The full *how* is locked in design doc [`2c-codegen.md`](../../.specify/2c-codegen.md) v1.3 — typed-message class shape (`[2c §4.7]`), the `owning_message_t<>` + `dict::reify` / `dict::reify_as` bridge (`[2c §4.8]`), the `dict::version_registry` shape (`[2c §4.9]`), the codegen pipeline (`[arch §4.2]`), CMake target layout (`[2c §7.6]`), latency ceilings (`[2c §6.2]`), and test seams (`[2c §9]`). This document does not re-derive any of that; it carves the second Phase 4 `dictionary/` feature out of 2c so `/clarify`, `/plan`, `/tasks`, and `/implement` have one starting artifact. It consumes the runtime `Dictionary`/`XmlLoader`/`FieldRef`/`ComponentRef`/`GroupRef` surface merged in **002-dictionary-xml-loader** (PR #66).

## 1. Summary

Ship the **dictionary codegen feature (D-008)**: the build-time host tool `tools/codegen/fixpp-codegen` and the per-version generated header packs it emits — `Messages.hpp` (typed-message flyweights, `[2c §4.7]`), `Fields.hpp` (`constexpr` field-metadata tables), `Validator.hpp` (per-message rule tables + Length+Data pair table), `Reify.hpp` (per-message `owning_<Msg>` classes, `[2c §4.8]`), `NormativeReferences.md` (per-message spec citations) — plus the 2c-owned runtime bridge `<fixpp/dict/reify.hpp>` (`dict::reify_as<Msg>` / `dict::reify` + `owning_message_handle`), the shared runtime-dispatch headers (`_dispatch/reify_dispatch_fixt.hpp` + `reify_dispatch_application.hpp`), the `dict::version_registry` header shape (`[2c §4.9]`), and the `[2c §7.6]` CMake target graph.

**Version coverage in this PR** (per /clarify 2026-05-15 Q1 → A, and `[2c §1.3]`): the four codegen-target versions — `fixpp::v42`, `fixpp::v44`, `fixpp::v50sp2`, `fixpp::vt11` (7 FIXT.1.1 admin types). The codegen tool emits the *full* standard message set per version from the checked-in XML (codegen is mechanical — there is no "half a generated header"); the **conformance corpus** (seam #1 / #15b) runs a representative ~20-message-per-version subset in the CI gate plus an exhaustive nightly run, exactly as `[2c §9]` seam #1/#15b scope it. Runtime-XML-only versions (FIX 4.0/4.1/4.3/5.0/5.0SP1) get **no** typed namespace per `[2c §1.3]`; `dict::reify` returns `dict_reify_unknown_msg_type` for them (AC-D5).

**Codegen host-tool implementation** (per /clarify Q2 → A): the language/host of `fixpp-codegen` is **deferred to /plan** with a 2–3 candidate evaluation (mirroring 002's XML-parser decision); user signs off at /plan, Codex Gate A reviews the choice. Tracked as follow-up **F1** in §10.

**`Validator.hpp` scope** (per /clarify Q3 → A): the codegen tool **emits** `Validator.hpp` (per-message rule tables) and the Length+Data pair table, and this PR **shape/exhaustiveness-tests** them against the source XML (seam #19). *Behavioral* validation (`wire::dictionary_driven_validator` actually rejecting a bad message per `[2b §4.6]`) is a downstream wire-layer feature, out of scope here. Rationale in §10 note.

> **Style note.** Per `[2c §4.7]` / N-P2-1, the per-tag typed accessors are **`inline noexcept`, not `constexpr`** (only `msg_type_v` / `version_v` are `constexpr`) because `wire::OffsetTable::find` is non-`constexpr` per `[2b §4.4]`. Codegen emits exactly this shape; the spec follows the design doc, not the looser "constexpr accessors" framing the catalogue row title (`feature-catalogue.md` line 77) carries.

## 2. Why (user value)

Per `[const §XV.13]`, eager-codegen-with-no-runtime-path is a *banned* pattern; 2c mandates the **hybrid** — codegen for the standard four versions (D-008, this feature), runtime XML for custom/legacy. **Gate A round 1 correction (Codex P1-2 / Opus Downgrade P1→P2):** 002 shipped **D-007 only** (`XmlLoader` + runtime `Dictionary` — the runtime dictionary path whose absence `[const §XV.13]` bans); 002 explicitly **deferred D-009** (`DialectOverlay`, the additive custom-extension overlay) to its follow-up F2 / a dedicated D-009 feature (`specs/002-dictionary-xml-loader/spec.md:26,100,235-239`). The constitutional obligation (`[const §XV.13]` = no eager codegen *without a runtime dictionary path*) is satisfied by **D-008 (this PR) + D-007 (002)** — a no-codegen venue has a fully working runtime `Dictionary` via D-007 alone; D-009 is an additive overlay, not "the runtime path" the ban targets. The hybrid is **not** "complete" — D-009 remains tracked (002 F2 / a future D-009 feature). 002 delivered the D-007 runtime half. Without the codegen half:

1. **No zero-cost typed field access.** Every application consumer (`session/`, user gateways, `bindings/python` via 2m) reaching a field today must use the untyped runtime accessor `view.get(uint16_t tag)` (`[2b §4.3]`) and hand-decode. Codegen gives `nos.cl_ord_id()` → `expected_t<std::string_view>` with the field-traits dispatch baked in at ≤ 20 ns (`[2c §6.2]`), zero allocation, `constexpr` tag numbers from the spec.
2. **No cross-strand message handoff.** `[2b §6.6]` names `MessageView::reify(mr)` as the supported deep-copy escape hatch for moving a parsed message across thread/strand boundaries; that reference is satisfied **only** by the 2c-owned `dict::reify` bridge (`[2c §4.8]` / RC-2). The session FSM cannot post a received message to a worker strand without it.
3. **No runtime-dispatch for the C ABI / FSM.** The C-ABI `fixpp_msg_reify` (2i, §5 commitment 4) and a session FSM handling cross-vocabulary FIXT.1.1 dispatch (`[2c §6.3]`) need the type-erased `dict::reify(view, profile, mr)` + the auto-generated (resolved-version, MsgType) dispatch switch.
4. **No per-message normative-reference packaging.** `[const §VI.5]` requires per-message spec citations; the generated `NormativeReferences.md` is the v1.0 mechanism (Appendix B).

This feature unblocks the typed surface every downstream module (`session/`, `capi/` 2i, `bindings/python` 2m) compiles against, and is a `dictionary/` module-exit prerequisite (`phase-4/dictionary/README.md` surface rows #8/#9).

## 3. User stories

### 3.1 Application developer — typed field access (Priority: P1)

> *As an engine integrator, I want `auto nos = fixpp::v44::NewOrderSingle{view}; auto id = nos.cl_ord_id();` so I read FIX fields with compile-time-correct names and types instead of hand-decoding `view.get(11)`.*

**Why this priority**: This is the MVP and the headline value of D-008 — the typed surface is the entire point of codegen. Nothing else in this feature is usable without the generated `Messages.hpp` existing and binding correctly to a `wire::MessageView<Index>`.

**Independent Test**: Parse a checked-in `NewOrderSingle` sample frame into `wire::MessageView<Index>`, construct `fixpp::v44::NewOrderSingle` over it, assert each headline accessor (`cl_ord_id`, `symbol`, `side`, `order_qty`, `price`) returns the expected typed value (AC-G1..G6).

### 3.2 Session FSM author — cross-strand handoff via reify (Priority: P1)

> *As the session-FSM author (**2d**), I want `dict::reify_as<v50sp2::NewOrderSingle>(view, mr)` to deep-copy a received message into caller-owned PMR storage so I can `std::move` it to a worker strand after the per-message arena is reset.*

**Why this priority**: `[2b §6.6]`'s view-escape contract is satisfied *only* here. Without `reify_as`, a parsed message cannot legally outlive `fromApp` return — the session layer is blocked on any async/queued processing pattern.

**Independent Test**: Parse a frame on thread A, `dict::reify_as<NewOrderSingle>(view, mr)`, `std::move` the `owning_NewOrderSingle` to thread B, reset thread A's arena, read accessors on B — values match pre-reset (AC-R1..R5, seam #12/#14).

### 3.3 Session FSM author — runtime-dispatch reify + FIXT cross-vocabulary (Priority: P2)

> *As the session-FSM author handling a FIXT.1.1 session, I want `dict::reify(view, profile, mr)` to peek `MsgType`, resolve the per-message `ApplVerID(1128)` against `version_profile`, and return a type-erased `owning_message_handle` so one code path handles admin (vt11) and application (v42/v44/v50sp2) frames per `[2c §6.3]`.*

**Why this priority**: Needed by the FSM and the C-ABI `fixpp_msg_reify` wrapper (2i), but the typed `reify_as` (3.2) is the load-bearing MVP; runtime dispatch layers on top. Exercises the §6.3 worked example.

**Independent Test**: Feed the `[2c §6.3]` worked-example byte stream through `dict::reify(view, profile, mr)`; assert each frame resolves to the correct namespace (Logon→vt11, NOS ApplVerID=9→v50sp2, NOS ApplVerID=6→v44 override, OCR no ApplVerID→v50sp2 default, Heartbeat→vt11) with correct `resolved_message_version` (AC-D1..D4, seam #15a/#15b/#15c).

### 3.4 Translator/gateway author — multi-version coexistence (Priority: P2)

> *As a translator author, I want to `#include <fixpp/v42/Messages.hpp>` and `#include <fixpp/v50sp2/Messages.hpp>` in one TU and have `fixpp::v42::NewOrderSingle` and `fixpp::v50sp2::NewOrderSingle` be distinct, non-convertible types so version-bridging logic is type-checked.*

**Why this priority**: Per `[2c §6.3]` / `[SYN §3.3 Q12]` multi-version coexistence is a v1.0 guarantee; the `[2c §7.6]` per-version CMake targets exist precisely so a consumer pays compile-time cost only for the versions it includes. Exercised by seam #10a.

**Independent Test**: A TU including v42 + v50sp2 `Messages.hpp` compiles; a `static_assert` that the two `NewOrderSingle` types are not implicitly convertible; depend only on `fixpp::dict::v44` + `fixpp::dict::runtime` and confirm v50sp2 headers are not pulled in (AC-C1..C3, seam #10a).

### 3.5 Codegen-tool maintainer — deterministic, XML-driven emission (Priority: P2)

> *As the `fixpp-codegen` maintainer, I want the tool to read `dictionaries/FIXxx.xml` and emit byte-identical headers across runs (build-tree only, never source-tree) so a dirty checkout never carries stale codegen and CI diffs are stable.*

**Why this priority**: Codegen correctness/determinism gates every other story; per `[arch §4.2]` step 3 outputs go to the build tree. Reproducibility is a Tier-1 concern.

**Independent Test**: Run `fixpp-codegen` twice against the same XML; assert byte-identical output; assert no file written under the source tree (AC-T1..T3, seam #1/#2).

### 3.6 Runtime-XML-only version consumer (Priority: P3 — negative path only)

> *As a consumer on FIX 4.3 (a runtime-XML-only version per `[2c §1.3]`), I expect `dict::reify(view, profile, mr)` to cleanly return `dict_reify_unknown_msg_type` (not crash, not misdispatch) since 4.3 has no codegen-emitted `owning_<Msg>`.*

**Why this priority**: Defines the boundary of the codegen scope; the positive path for these versions is the 002 runtime tag-keyed accessor, not this feature. Negative-path only.

**Independent Test** (revised Gate A round 1, Codex P2-3 / Opus Confirm @ P2 — `FIX43.xml` is a 002 F1 deferral, unavailable as a live test input): build a synthetic application `MessageView` whose resolved `application_version` is a runtime-XML-only version (v40/v41/v43/v50/v50sp1 — no codegen-emitted owner), feed it through `dict::reify`; assert the application switch's fail-loud default arm returns `dict_reify_unknown_msg_type` (`contracts/reify_dispatch.hpp:42`). No `FIX43.xml` dependency — the negative path is exercised by a hand-built fixture, not a deferred XML asset (AC-D5, seam #10c).

### Edge Cases

- **Source XML declares a `<message msgtype="...">` outside the locked set (e.g., a FIX-Latest A-035..A-065 message)?** Per `[2c §2]`: codegen emits a build-time warning, downgradable to error in CI; the message is *not* emitted in v1.0 (no `FIXPP_CODEGEN_ENABLE_FIX_LATEST` flag in v1.0). AC-G9.
- **Source XML declares A-014..A-034 (codegen-deferred to v1.x per `[2c §1.3]` / `[const §XVIII.7]`)?** Not emitted as typed classes in v1.0; reachable only via 002's runtime `view.get(uint16_t)`. The conformance corpus excludes them. AC-G10.
- **`dict::reify` on a FIXT.1.1 session whose `default_appl == Unknown` and the message has no `ApplVerID(1128)`?** Returns `dict_unresolved_application_version` (NOT `dict_reify_unknown_msg_type` — the v1.0 misdiagnosis path is closed per RC#1). AC-D6, seam #15c.
- **`std::move` of an `owning_<Msg>` after its lazy `view()` cache populated?** Custom `noexcept` move resets both source and destination `frame_cache_`/`view_cache_`; first `view()` on the moved-to instance rebuilds against post-move `bytes_.data()`. Concurrent reads on one instance are UB (single-strand-only per N-P1-2). AC-R4, seam #14.
- **`owning_message_handle::as<Msg>()` when resolved version/MsgType don't match `Msg`?** Returns `nullptr` (no UB, no throw). AC-R6.
- **PMR allocation failure inside the `noexcept` reify path?** Trapped via `[2a §4.2]` `trap_throw`; surfaces as `dict_reify_oom` (≤ 4 PMR allocations budget per N-P2-5). AC-R7, seam #16.
- **A typed-message flyweight captured past `fromApp` return?** Release UB; debug traps via `[2b §6.4]` generation counter (the accessor flows through `wire::MessageView::get<Tag>()` → `View::check_alive()`). The supported escape is `reify_as` (3.2). AC-G8.
- **Codegen template accidentally adds a member to a typed flyweight?** Caught by the emitted `static_assert(sizeof(NewOrderSingle) == sizeof(wire::MessageView<Index> const*))` (seam #18). AC-G7.

## Clarifications

### Session 2026-05-15 (resolved inline during /specify)

- **Q1: Message/conformance scope for the four codegen versions?** → **A** — codegen tool emits the *full* standard message set per version from the checked-in XML (mechanical; no partial headers). The conformance corpus runs a representative ~20-message-per-version subset in the CI gate + an exhaustive nightly run, exactly as `[2c §9]` seam #1 / #15b scope it. All four versions ship in this PR (one coherent codegen feature; staging by version would re-open the ~470-case dispatch switch + version_registry interaction three more times).
- **Q2: `fixpp-codegen` implementation language/host?** → **A** — deferred to /plan with a 2–3 candidate evaluation (mirrors 002 Q3 → XML-parser-defer). User signs off at /plan; Codex Gate A reviews. Tracked as follow-up **F1** in §10.
- **Q3: `Validator.hpp` scope?** → **A** — codegen tool **emits** `Validator.hpp` + the Length+Data pair table; this PR **shape/exhaustiveness-tests** them against the source XML (seam #19). *Behavioral* validation (`wire::dictionary_driven_validator` rejecting a bad message) is a downstream wire-layer feature, out of scope here. Validator.hpp is pure codegen output — cheap to emit now, expensive to retrofit later.

### Session 2026-05-15 (/clarify)

- Q: What selection principle governs the ~20-message/version CI conformance subset (seam #1 / #15b)? → A: **Curated must-include** — the CI subset MUST include all P1 headline messages, every message bearing a repeating group, all 7 FIXT.1.1 admin types, the AC-D4 worked-example messages, and msgtype-boundary cases (FIX-Latest / A-014..A-034 filter probes). Pinned as an acceptance criterion (AC-G12); nightly run remains exhaustive; Gate A reviews the must-include list.
- Q: How many golden headers anchor the determinism test (NFR-003-7 / R5)? → A: **One per version (4 total)** — a golden header for each of `v42`/`v44`/`v50sp2`/`vt11`; the determinism test asserts byte-identical re-emission against all four. The goldens are *generated codegen output* and are **checked in at `/implement`** (one `tasks.md` row), not present in this bundle at Gate A — `contracts/golden/` does not yet exist. Regenerated as a deliberate, reviewed step on any codegen-template change.

### Session 2026-05-15 (Gate A round 1)

- Q: Who ships `version_profile`, `dict::resolve_application_version`, and `<fixpp/dict/field_traits.hpp>` (`field_traits<T>`/`decode_field<T>`) — 002, or this feature? (Codex P1-1 / Opus RC#1 / N-P2-2 — the bundle asserted "002-shipped" but 002 deferred/never-shipped all three.) → A: **003 is the natural owner; this is locked at re-`/plan`, not asserted as shipped here.** 2c §4.3 publishes `resolve_application_version` as a free function precisely "so callers that don't hold a `Dictionary` (notably `dict::reify`) can run the resolution" and 2c §4.1.3 makes `field_traits`/`decode_field` the 2c-owned typed-decoding layer; 002 explicitly punted `version_profile`+`resolve_application_version` to "the wire/session integration feature" and never owned `field_traits`. 003 is therefore the natural owner — but materializing that ownership is **not** a convergence text patch: it is a surface expansion that re-`/plan` must derive, comprising (a) new `contracts/version_profile.hpp` + `contracts/field_traits.hpp` extracts, (b) matching `include/fixpp/dict/{version_profile,field_traits}.hpp` files-in-scope + Project-Structure rows, (c) **new ACs** for the `version_profile`/`field_traits`/`decode_field` surface, (d) the **error-taxonomy interactions** these introduce, and (e) the wire→C++ `ApplVerID(1128)` → `application_version` **enum-mapping table** per 2c §4.3 (`.specify/2c-codegen.md:478`). Adding the two contract headers alone does **not** close this. It is **locked at re-`/plan`**, not this convergence pass — `blocked_on_replan` front-matter gates `/tasks`. (§8 "Upstream dependency audit".)
- Q: The decimal typed-accessor (`AC-G4`) is inherited from 2c v1.3 as `decimal_t::from_chars(fv->bytes())`, a symbol that does not exist on the merged 001/2a surface (PMR-mandatory `decimal_traits<T>::from_chars(span,mr)` / `decimal<T>::parse(span,mr)` only). Can this be reconciled inside the 003 bundle? → A: **No — it is a defect in signed-off `2c-codegen.md` v1.3 itself, inherited verbatim.** A 003 bundle edit cannot fix an incoherent inherited design contract (2c is uneditable in this pass). Resolution requires reopening **2c §4.1.3/§4.7** to a PMR-mandatory decimal parse, then re-deriving AC-G4/AC-G4a/NFR-003-4/data-model PMR accounting. Documented under AC-G4 and recorded in `plan.md` `## Gate A` per `[const §XX]` (design-doc amendment precondition).
- Q: Does `[const §XV.13]` (no eager codegen without a runtime dictionary path) require D-009 to be shipped before this feature can green-check the hybrid mandate? (Codex P1-2.) → A: **No — D-007 (002) + D-008 (this PR) discharge it.** The banned pattern is eager codegen with *no runtime dictionary path*; D-007's runtime `Dictionary` is that path. D-009 (`DialectOverlay`) is an additive overlay, not the runtime path, and remains tracked (002 F2). The Constitution-Check claim is narrowed accordingly (§2; plan.md `[const §XV.13]` row).

## 4. Functional acceptance criteria

Lifted from `[2c §4.7]`, `[2c §4.8]`, `[2c §4.9]`, `[2c §6.2]`, `[2c §6.3]`, `[2c §7.6]`; one bullet per testable property.

### 4.1 Generated typed messages — `[2c §4.7]`

- **AC-G1.** For each codegen version, `fixpp-codegen` emits `_codegen/include/fixpp/<vXX>/Messages.hpp` containing one class per standard message declared in `dictionaries/<VER>.xml`, in `namespace fixpp::<vXX>`.
- **AC-G2.** Each typed message exposes `static constexpr std::string_view msg_type_v` and `static constexpr application_version version_v` (e.g., `v50sp2::NewOrderSingle::msg_type_v == "D"`), usable in `static_assert` / compile-time dispatch.
- **AC-G3.** Each typed message has an `explicit Msg(wire::MessageView<Index> const& [[clang::lifetimebound]]) noexcept` constructor binding the view by reference; it does not validate.
- **AC-G4.** Each declared field has an `[[nodiscard]] inline ... noexcept` accessor returning `expected_t<T>` with the `dict::field_traits<T>` / decimal dispatch baked in (string/int/char/decimal). View-returning accessors carry `[[clang::lifetimebound]]`.

  > **⚠ INHERITED DESIGN-DOC DEFECT — un-fixable by a 003 bundle edit (Gate A round 1, Codex P2-1 escalated → Opus root cause #2).** `[2c §4.1.3]`/§4.7 (`.specify/2c-codegen.md:270-271,1040`) bake `fixpp::decimal_t::from_chars(fv->bytes())` — a no-PMR member named `from_chars` on `decimal_t` — into the **signed-off** design 003 inherits. The merged 001/2a surface publishes **no such symbol**: the only parse entry points are `decimal_traits<T>::from_chars(std::span<const std::byte>, std::pmr::memory_resource*) noexcept` (`specs/001-core-decimal/contracts/decimal_traits.hpp:98-100`) and `decimal<T>::parse(std::span<const std::byte>, std::pmr::memory_resource*) noexcept` (`decimal_traits.hpp:162-163`) — both with **mandatory PMR**, and 2a's own Gate A explicitly *removed* the single-arg/no-PMR form (`decimal_traits.hpp:123-128` — "rejected by Codex P2 #4 + Opus P1 #4 in 2a's own Gate A history"). The inherited shape is therefore wrong on three axes (wrong type carrier — `decimal_t` vs `decimal_traits<T>`; wrong name vs `parse`; missing required `mr`). **This is a defect in signed-off `2c-codegen.md` v1.3, not in this bundle's text — a bundle-only edit cannot resolve it (the contract is inherited verbatim and 2c is uneditable here).** Resolution requires reopening **2c §4.1.3 / §4.7** to route decimal through the real PMR-mandatory `decimal_traits<decimal_t>::from_chars(span, mr)` (or `decimal<T>::parse(span, mr)`), then re-deriving this AC, NFR-003-4, and the data-model PMR accounting from the corrected design. Recorded in `plan.md` `## Gate A` per `[const §XX]` (design-doc must be amended before this AC can close). Coupled lifetime hole (Opus N-P1-1): the flyweight holds only `wire::MessageView<Index> const&` with **no `memory_resource*`** (AC-G7 / I-1 `sizeof == one pointer`), so a PMR-taking decimal accessor has no arena in scope on the borrowed read path — see AC-G4a.

- **AC-G4a (NEW — Opus N-P1-1, contingent on the AC-G4 / 2c §4.1.3 reopen).** Once 2c §4.1.3/§4.7 is corrected to a PMR-mandatory decimal parse, the typed-decimal-accessor contract MUST pin a coherent arena story: typed decimal accessors on the *borrowed flyweight* (which holds no `mr` and must keep `sizeof == one pointer` per AC-G7) are zero-allocation **only** for a non-allocating `FIXPP_DECIMAL_T` (e.g. `pod_decimal`); an allocating `FIXPP_DECIMAL_T` (`cpp_dec_float` is an explicitly-supported substitution per `2a-decimal.md`) requires the arena-backed `owning_<Msg>` path (which owns an `mr`, data-model Entity 4) — the borrowed flyweight cannot service it. NFR-003-4's "zero allocation on the typed-accessor read path" holds only under the non-allocating-trait constraint, which this AC makes explicit. This AC is **blocked on the 2c §4.1.3/§4.7 reopen** (it has no coherent form until the decimal parse signature is corrected upstream).
- **AC-G5.** Repeating groups expose a `wire::group_view<Leg>`-returning accessor (`[2b §4.7]`); nested group structs (`Leg`, …) are flyweights with the same accessor discipline + their own `field_value(uint16_t)` forwarder.
- **AC-G6.** Every typed message and nested group struct carries `field_value(std::uint16_t tag) -> expected_t<wire::field_view>` forwarding to `view().get(tag)` (overlay-promoted-tag escape per `[2c §4.7.1]`).
- **AC-G7.** Each typed-message header carries `static_assert(sizeof(Msg) == sizeof(wire::MessageView<wire::access_mode::Index> const*))` (flyweight = exactly one reference, no other state).
- **AC-G8.** Capturing a typed flyweight past the originating view's lifetime is release-UB and debug-traps via `[2b §6.4]`'s generation counter (verified through the accessor path, not by leaking a dangling reference in the test).
- **AC-G9.** A `<message msgtype="...">` outside the `[2c §1.3]` locked set produces a codegen build-time warning (CI-downgradable to error) and is **not** emitted (no v1.0 FIX-Latest flag).
- **AC-G10.** A-014..A-034 messages present in source XML are **not** emitted as typed classes in v1.0 (codegen-deferred per `[const §XVIII.7]`); they remain reachable only via 002's runtime `view.get(uint16_t)`.
- **AC-G11.** Per-tag accessors are `inline noexcept` and **not** `constexpr`; only `msg_type_v`/`version_v` are `constexpr` (N-P2-1).
- **AC-G12.** The CI conformance subset (seam #1 / #15b) satisfies the **curated must-include** rule: per version it MUST cover every P1 headline message, every message declaring a repeating group, all 7 FIXT.1.1 admin MsgTypes (`0/1/2/3/4/5/A`), every message in the `[2c §6.3]` AC-D4 worked example, and msgtype-boundary probes (a filtered FIX-Latest A-035..A-065 message and a codegen-deferred A-014..A-034 message per AC-G9/AC-G10). The nightly run is exhaustive over the full emitted set; a CI subset missing any must-include class fails the conformance test (the list is a checked-in, Gate-A-reviewed manifest, not an ad-hoc sample).

### 4.2 `Fields.hpp` / `Validator.hpp` / `NormativeReferences.md` — `[2c §4.2]` / `[2c §1.3]`

- **AC-V1.** For each codegen version, `fixpp-codegen` emits `Fields.hpp` with `constexpr` `FieldRef`/`ComponentRef`/`GroupRef` arrays (static storage; zero allocation, `[const §VIII.5]`) populated from the source XML; `FieldRef`/`ComponentRef`/`GroupRef` are the 002-shipped `fixpp::dict` types (`sizeof`/`alignof`/standard-layout invariants from `[2c §4.1]`/`[2c §4.2]` hold).
- **AC-V2.** `Fields.hpp` arrays are sorted for O(log N) `(MsgType, tag)` lookup consistent with the codegen-emitted per-version layout (`[2c §4.2]` — the per-version contiguous-slice layout, distinct from 002's runtime side-table layout per 002 spec §Clarifications Q4).
- **AC-V3.** `Validator.hpp` is emitted with per-message rule tables; this PR shape-asserts its structure and exhaustiveness against the source XML. *Behavioral* validation is out of scope (§5).
- **AC-V4.** The Length+Data pair table (in `Validator.hpp` or `Fields.hpp`, /plan-locked) is **exhaustive** against the source XML: every `<field type="LENGTH">` paired with its documented `<field type="DATA">` neighbour; cross-checked against `[FIX50SP2 §3.3]` (seam #19).
- **AC-V5.** `NormativeReferences.md` is emitted per version with per-message `[FIXxx §X.Y.Z]` citations resolved from the source XML `<message>`/`<field>` references (`[const §VI.5]`, Appendix B; `[2c §10]` Q7 confirmed at implementation).
- **AC-V6.** `_reserved` bytes in any emitted `FieldRef`/`ComponentRef`/`GroupRef` are zero on emit, ignored on read (`[2c §4.1]` discipline; `[2c §10]` Q8 → match 2a).

### 4.3 Reify bridge — `[2c §4.8]`

- **AC-R1.** `<fixpp/dict/reify.hpp>` declares `dict::reify_as<Msg>(view, mr) noexcept -> expected_t<owning_message_t<Msg>>` and `dict::reify(view, profile, mr) noexcept -> expected_t<owning_message_handle>`, free function templates in `namespace fixpp::dict` (no method added to `wire::MessageView`).
- **AC-R2.** Each codegen version emits `Reify.hpp` with one `owning_<Msg>` class per typed message: move-only (copy deleted), custom `noexcept` move ctor/assign that resets source and destination `frame_cache_`/`view_cache_`, owns `bytes_` + rebuilt `OffsetTable` on the supplied `mr`, exposes the same accessor surface as the flyweight, `which()` returns `version_v`.
- **AC-R3.** `dict::reify_as<Msg>(view, mr)` deep-copies the frame into `mr`-owned storage; the result is correct (accessors equal the source view's) and survives the source view's destruction.
- **AC-R4.** After `std::move` of an `owning_<Msg>` whose `view()` cache was populated, the moved-to instance's first `view()`/accessor call rebuilds correctly against post-move `bytes_`; source caches are `std::nullopt` (seam #14; static-asserts: no reference members, `is_nothrow_move_constructible_v`, move ctor not `= default`).
- **AC-R5.** A reified `owning_<Msg>` is safe to `std::move` across thread/strand boundaries; receiver-side accessors alias the owned bytes (`[[clang::lifetimebound]]` chains to `*this`) (seam #12).
- **AC-R6.** `owning_message_handle` is move-only; `version()` returns the `resolved_message_version`; `msg_type()`/`view()`/`field_value(tag)` behave per `[2c §4.8]`; `as<Msg>()` returns a borrowed `owning_<Msg> const*` or `nullptr` on version/MsgType mismatch.
- **AC-R7.** PMR allocation failure on any reify path surfaces as `dict_reify_oom` (trapped via `[2a §4.2]` `trap_throw`); ≤ 4 PMR allocations per `reify_as<Msg>` and no allocation outside `mr` (seam #7/#16).
- **AC-R8.** `reify_as<Msg>` returns `dict_reify_msg_type_mismatch` when the view's `MsgType` ≠ `Msg::msg_type_v`; `dict_reify_version_mismatch` is **not** a failure mode of `reify_as` (dropped per RC#1).

### 4.4 Runtime-dispatch — `[2c §4.8]` / `[2c §6.3]`

- **AC-D1.** Codegen emits the shared dispatch headers `_codegen/include/fixpp/_dispatch/reify_dispatch_fixt.hpp` (exactly the 7 FIXT admin MsgTypes `0/1/2/3/4/5/A`) and `reify_dispatch_application.hpp` (one case per (codegen `application_version`, MsgType) pair across v42/v44/v50sp2), included once per all-versions TU.
- **AC-D2.** `dict::reify` peeks `MsgType(35)`; on a FIXT-admin hit dispatches via the FIXT switch to `vt11::owning_<Msg>` with `resolved_message_version{session_admin, profile.session, Unknown}`.
- **AC-D3.** On FIXT-admin miss, `dict::reify` reads `ApplVerID(1128)` (mapping `dict_field_not_present` → empty `string_view`), calls the free function `dict::resolve_application_version(profile, appl_ver_id_value)`, and dispatches via the application switch with `resolved_message_version{application, profile.session, resolved}`. **Dependency note (Gate A round 1, Codex P1-1 / Opus RC#1):** `version_profile` and `dict::resolve_application_version` are NOT 002-shipped (they were claimed merged but 002 deferred them — §8 "Upstream dependency audit"). This AC is therefore **blocked pending re-`/plan`**: 003 must own and ship `version_profile` + `dict::resolve_application_version` + the wire→C++ `ApplVerID` enum-mapping table (2c §4.3, `.specify/2c-codegen.md:478`) with their own contract/files/ACs/tests before this AC is implementable.
- **AC-D4.** The `[2c §6.3]` worked-example byte stream resolves correctly: Logon→`vt11`, NOS ApplVerID=9→`v50sp2`, NOS ApplVerID=6→`v44` (per-message override), OrderCancelRequest no ApplVerID→`v50sp2` (session default), Heartbeat→`vt11` (seam #15a/#15b).
- **AC-D5.** For a runtime-XML-only resolved version (v40/v41/v43/v50/v50sp1), the application switch's default arm returns `dict_reify_unknown_msg_type` (no codegen-emitted owner). Exercised via a **hand-built synthetic `MessageView`** whose resolved version has no codegen owner — **no `FIX43.xml` dependency** (that file is a 002 F1 deferral; Gate A round 1 Codex P2-3 / Opus Confirm) (seam #10c).
- **AC-D6.** A FIXT.1.1 profile with `default_appl == Unknown` + a message lacking `ApplVerID(1128)` yields `dict_unresolved_application_version` from `dict::reify` (NOT `dict_reify_unknown_msg_type`) (seam #15c).
- **AC-D7.** `dict::reify` additionally maps: `dict_unknown_appl_ver_id` (non-empty ApplVerID that doesn't parse), `dict_reify_unknown_msg_type` (resolved-version + MsgType has no owner), `dict_reify_oom`.

### 4.5 `dict::version_registry` shape — `[2c §4.9]`

- **AC-X1.** `<fixpp/dict/version_registry.hpp>` declares `class version_registry` with `[[nodiscard]] expected_t<Dictionary const*> get(application_version) const noexcept [[clang::lifetimebound]]`.
- **AC-X2.** `version_registry::get` returns `dict_no_dictionary_for_application_version` when no `Dictionary` is registered for a resolvable version (distinct from `dict_unknown_appl_ver_id`, a wire-string parse failure).
- **AC-X3.** Only the **shape** ships; the ownership/construction model (engine-owned-by-value vs session-borrowed; `EngineConfig::dictionaries`) is deferred to 2d per `[2c §10]` Q10 (§10 follow-up **F3**). A minimal in-test harness exercises `get` against a hand-constructed registry; no engine wiring.

### 4.6 CMake target graph — `[2c §7.6]`

- **AC-C1.** Independent `INTERFACE` targets exist: `fixpp::dict::v42`, `::v44`, `::v50sp2`, `::vt11` (each installs that version's `{Messages,Fields,Validator,Reify,NormativeReferences}` files), `::all_versions` (umbrella), `::runtime` (002 runtime surface + `dict::reify` bridge declarations), `::dispatch` (the two `_dispatch/` headers; depends on `::all_versions`).
- **AC-C2.** A consumer depending only on `fixpp::dict::v44` + `fixpp::dict::runtime` compiles without pulling v50sp2/v42/vt11 headers (no compile-time cost for unused versions).
- **AC-C3.** `fixpp::v42::NewOrderSingle` and `fixpp::v50sp2::NewOrderSingle` are distinct, non-implicitly-convertible types when both `Messages.hpp` are included (seam #10a).
- **AC-C4.** All codegen outputs are written under the **build tree** (`build/<preset>/_codegen/include/...`), never the source tree; targets carry `INTERFACE_INCLUDE_DIRECTORIES` into the build tree; codegen runs at configure time (`fixpp::dict::generate-vXX`).

### 4.7 Determinism / threading

- **AC-T1.** `fixpp-codegen` is deterministic: byte-identical XML input → byte-identical generated headers across runs and machines.
- **AC-T2.** Codegen writes nothing under the source tree; a dirty checkout never carries stale codegen (build-tree-only per `[arch §4.2]` step 3).
- **AC-T3.** Typed-message flyweights and `owning_<Msg>` are **not** thread-safe-on-read as a blanket claim: flyweights inherit `[2b §6.4]` lifetime rules; `owning_<Msg>` is **single-strand-only** (lazy `view()` cache write is unsynchronized; concurrent reads on one instance are UB per N-P1-2). The thread-safe usage is reify-on-A → move → consume-on-B (AC-R5).

## 5. Out of scope

- **Behavioral message validation** — `wire::dictionary_driven_validator` (`[2b §4.6]`) consuming the emitted `Validator.hpp` to accept/reject messages. This PR emits + shape-tests the table only (per /clarify Q3 → A); behavior is a wire-layer feature.
- **`DialectOverlay` / `with_overlay` / overlay-regen codegen path** (`[2c §4.4]`, `[2c §6.4]`, `[2c §4.7.1]` path 2, `[2c §7.1]` reverse direction) — owned by D-009; deferred as 002 follow-up F2. The `field_value(uint16_t)` forwarder ships (AC-G6) but no overlay merge is exercised here.
- **`dict::version_registry` ownership/engine wiring** — only the header shape ships (AC-X3); construction model deferred to 2d (`[2c §10]` Q10).
- **C ABI surface** (`fixpp_msg_reify`, `fixpp_owning_msg_t`, `fixpp_dict_t`, the C accessor family) — delegated to 2i per `[arch §4.10]`; 2c records commitments only (`[2c §5]`). Not part of this PR.
- **SWIG / Python bindings** for typed messages — owned by 2m.
- **Codegen for runtime-XML-only versions** (v40/v41/v43/v50/v50sp1) — `[2c §1.3]` runtime-XML-only; post-v1.0 (`[2c §10]` Q4). Negative-path coverage only (AC-D5).
- **A-014..A-034 typed classes** — codegen-deferred to v1.x per `[const §XVIII.7]` / Appendix D §3.
- **FIX-Latest (A-035..A-065) / FIX-Orchestra (D-011)** — post-v1.0 per `[const §XVIII.2]`.
- **MessageStore typed-payload serialise-back** — `[2c §10]` Q6; v1.0 assumes raw-frame persistence (2e confirms).
- **Cross-version converters** (`v42::NewOrderSingle` → `v50sp2::NewOrderSingle`) — application-specific, explicitly not generated (`[2c §6.3]`).
- **All-versions-TU as a supported default build** — "not supported by default" per N-P2-3; the single-version compile ceiling is the load-bearing one (NFR-003-2).

## 6. Non-functional requirements

| NFR | Requirement | How verified |
|---|---|---|
| NFR-003-1 | Typed accessor latency (warm, 20-tag msg): string/int/char ≤ 20 ns; decimal ≤ 75 ns; `field_value(uint16_t)` ≤ 25 ns. CI fails on >5% regression vs baseline (`[2c §6.2]`). | Google Benchmark seam #3 (`NewOrderSingle::cl_ord_id`/`side`/`order_qty`/`price`/`field_value`). |
| NFR-003-2 | Single-version `Messages.hpp`+`Reify.hpp` TU compiles in ≤ 3 s (load-bearing). All-versions TU ≤ 15 s **soft** (configurable `FIXPP_BENCH_ALL_VERSIONS_CEILING`; not a default-supported build). | Compile-time bench seam #2; per-header preprocessor-expansion size tracked. |
| NFR-003-3 | `dict::reify_as<Msg>`: ≤ 1 µs (20-tag), ≤ 10 µs (200-tag). `dict::reify` (runtime-dispatch): ≤ 1.2 µs (20-tag). ≤ 4 PMR allocations per `reify_as`; no allocation outside `mr`. | Reify-latency bench seam #6; allocation guard seam #7 (`mallocnesia`, Linux). |
| NFR-003-4 | Zero allocation on the typed-accessor read path (every accessor delegates to `wire::MessageView::get<Tag>()`, allocation-free Index mode per `[2b §4.3]`). | Allocation guard seam #7; read loop under interceptor reads 0. |
| NFR-003-5 | Codegen output is `constexpr` static storage — no `new`/`delete` ever (`[const §VIII.5]`); no `thread_local` emitted (`[const §XV]`/`[arch §5.4]`). | Static-storage assertions; `tools/check_layers.py` + grep gate in CI; `Fields.hpp` inspected. |
| NFR-003-6 | All view-returning accessors carry `[[clang::lifetimebound]]`; all `expected_t<T>`-returning methods carry `[[nodiscard]]` (codegen emits unconditionally). | Codegen-template golden test; static inspection of a sample generated header. |
| NFR-003-7 | Determinism: byte-identical XML → byte-identical generated headers across runs **and machines** (sorted-emission invariant, locale-independent bytewise compare). | Seam #1/#2 — generate twice, hash, assert equal; **one golden header per codegen version** (v42/v44/v50sp2/vt11 — 4 total), checked in at `/implement` (generated codegen output — not present at Gate A); determinism test asserts byte-identical re-emission against all four; regenerated as a reviewed step on template change. |
| NFR-003-8 | Layer hygiene — **partially green, one open layer-amendment item (Gate A round 1, Codex P1-3 / Opus Confirm @ P1, root cause #3).** `dictionary → core` (from 002) and the codegen host-side build deps are clean. The `arch §2.4` carve-out ("Generated typed messages `fixpp::vXX::*` … compile against both modules") covers the *generated* `Messages.hpp`/`Reify.hpp`/`_dispatch` tree's `#include <fixpp/wire/...>`. It does **NOT** cover the **hand-written** public header `include/fixpp/dict/reify.hpp` (`contracts/reify.hpp:8`), which `#include`s `<fixpp/wire/message_view_contract.hpp>` — `reify.hpp` is not a generated `fixpp::vXX::*` header; it lives in `dict/`, which the `arch §2.3` whitelist pins to `core` only. An `#include` is a `check_layers.py` edge regardless of template-argument framing. Additionally this feature *ships a new header under `include/fixpp/wire/`* (a 2b-owned tree) as the R6 stub. **This is NOT green-checkable at Gate A**: it requires either an explicit `[const §XX]` / `arch §2.3` amendment adding a `dictionary(reify bridge) → wire(contract header)` edge (and teaching `check_layers.py` the carve-out), or relocating the contract header out of `dict/`'s include path — decided at re-`/plan`, not asserted here. | `tools/check_layers.py`; the `reify.hpp → wire` edge + the new `wire/` header write are an **open layer-amendment item** flagged for re-`/plan` + design-doc/arch amendment, not a clean check. |

## 7. Files in scope

> Full files-to-create list is /plan-locked; this section names the bright lines.

- **Codegen tool:** `tools/codegen/fixpp-codegen` (host-tool; language/host /plan-locked per F1) + its build integration (`fixpp::dict::generate-vXX` configure-time targets).
- **Runtime bridge headers (public, `dict/`):** `include/fixpp/dict/reify.hpp` (`reify_as` / `reify` / `owning_message_handle`), `include/fixpp/dict/version_registry.hpp` (shape only). Possibly a small `src/dictionary/reify.cpp` / `version_registry.cpp` (/plan-locked).
- **003-owned upstream surface NOT shipped by 002 (Gate A round 1, Codex P1-1 / N-P2-2 / Opus RC#1 — files added at re-`/plan`):** `include/fixpp/dict/version_profile.hpp` (the `version_profile` struct + the `dict::resolve_application_version` free function + the wire `ApplVerID(1128)` → C++ `application_version` enum-mapping table, 2c §4.3 / `.specify/2c-codegen.md:478`), `include/fixpp/dict/field_traits.hpp` (`dict::field_traits<T>` specialisations + `dict::decode_field<T>`, the typed-decoding hot-path layer per `[2c §4.1.3]`). These are NOT in 002's merged surface (§8 "Upstream dependency audit"); 003 must own them with their own `contracts/` extracts, ACs, and tests. **Blocking — locked at re-`/plan`, not this convergence pass.**
- **Generated header packs (build tree only):** `_codegen/include/fixpp/{v42,v44,v50sp2,vt11}/{Messages,Fields,Validator,Reify,NormativeReferences}.{hpp,md}`; `_codegen/include/fixpp/_dispatch/{reify_dispatch_fixt,reify_dispatch_application}.hpp`.
- **Dictionaries (XML data):** reuse the four checked-in by 002 (`dictionaries/{FIX42,FIX44,FIX50SP2,FIXT11}.xml`); no new XML in this PR.
- **Tests:** `tests/codegen/conformance/` (parameterised round-trip corpus — CI subset + nightly exhaustive), `tests/codegen/typed_accessor_test.cpp`, `tests/dictionary/reify_test.cpp` (reify_as/reify/handle), `tests/dictionary/reify_dispatch_test.cpp` (FIXT + application + unresolved), `tests/integration/multi_session_multi_version.cpp` (10a), `tests/integration/fixt_cross_vocabulary.cpp` (10b), `tests/dictionary/reify_move_test.cpp` (lazy-view rebuild, #14), `tests/dictionary/reify_cross_strand_test.cpp` (#12), shape/static-assert tests (#18), Length+Data table coverage (#19), determinism (#1/#2), version_registry shape test (AC-X*).
- **Bench:** `bench/codegen/typed_accessor_bench.cpp`, `bench/codegen/compile_time_bench` harness, `bench/dictionary/reify_bench.cpp`.
- **Fuzz:** none new required for codegen output; the XmlLoader fuzz harness (`tests/fuzz/fuzz_dict_xml_loader.cpp`) shipped with 002 already covers the XML input that drives codegen. (`[2c §9]` seam #8 is XmlLoader-side, already shipped; seam #9 overlay-fuzz defers with F2.)
- **CMake:** the `[2c §7.6]` target graph (`fixpp::dict::{v42,v44,v50sp2,vt11,all_versions,runtime,dispatch}`).
- **Layer lint:** `tools/check_layers.py` reviewed for the codegen-tool host edge classification.

## 8. Inheritance / dependencies

- **Inherits design from:** `[2c §4.7]` typed messages; `[2c §4.8]` `owning_message_t<>` + `dict::reify` bridge; `[2c §4.9]` `version_registry`; `[2c §1.3]` version coverage; `[2c §6.1]` allocation/exceptions/threading; `[2c §6.2]` latency ceilings; `[2c §6.3]` multi-version coexistence + worked example; `[2c §6.6]` reify contract; `[2c §6.7]` errors table; `[2c §7.6]` CMake targets; `[2c §9]` test seams. `[arch §4.2]` codegen pipeline; `[arch §3]` namespaces; `[arch §5.5]` flyweight/lifetimebound; `[const §XV.13]` hybrid mandate; `[const §VI.5]` normative references.
- **Depends on (in-tree, merged):** two distinct surfaces, both on `main` via PR #66 (taxonomic split corrected Gate A round 2, Codex F-2 / Opus Downgrade P2→P3 — see `plan.md` `## Gate A` → `### Round 2 — disagreements`):
  - **002-shipped dictionary surface** — `fixpp::dict::Dictionary`, `XmlLoader`, `FieldRef`, `ComponentRef`, `GroupRef`, the `session_version`/`application_version` enums, `Dictionary::which_session_version()`.
  - **Core surface consumed here** — `core::error`/`expected_t<T>` (001-origin, *extended additively by 002*: the `dict_*` error variants appended at slots 20–22) and `core::detail::trap_throw_or_throw` (*net-new in 002 / PR #66*, alongside the pre-existing `detail::trap_throw`). The `dict_*` error variants 003 consumes live in `core::error`, not a separate `dict::` namespace. `[2b]` `wire::MessageView<Index>`/`field_view`/`group_view`/`OffsetTable` (compile-time template dependency of generated headers; the wire feature itself is downstream — confirm seam at /plan, see R6).

### Upstream dependency audit (Gate A round 1, 2026-05-15 — Codex P1-1 / N-P2-2 / Opus root cause #1)

Codex P1-1 and Opus root cause #1 found this feature claimed three `[2c §4.x]`-*designed* symbols as "002-shipped / on `main` via PR #66" when the merged upstream surface does **not** provide them. Corrected status per the on-disk 002 contracts:

- **`version_profile` struct** — `[2c §4.3]`. **NOT shipped by 002.** `specs/002-dictionary-xml-loader/contracts/version_profile.hpp:5-7,59-66` explicitly: the struct "is out of scope for this PR (they ship with the wire/session integration features) … deferred to the wire/session integration feature." 002 ships only the `session_version`/`application_version` *enums*. 2c §4.3 (`.specify/2c-codegen.md:449-455`) designs the type but assigns it **no shipping owner**.
- **`dict::resolve_application_version` free function** — `[2c §4.3]`. **NOT shipped by 002.** Same `version_profile.hpp:59-66`; `contracts/dictionary.hpp:19-21` separately defers the `Dictionary::resolve_application_version` member. 2c §4.3 (`.specify/2c-codegen.md:449-473`) designs it as a free function "so callers that don't hold a `Dictionary` (notably `dict::reify`) can run the resolution" — i.e. the natural owner is **003 (or the wire/session feature), not 002** — but assigns no owner.
- **`dict::field_traits<T>` + `decode_field<T>` + `<fixpp/dict/field_traits.hpp>`** — `[2c §4.1.3]`. **NOT shipped by 002** (002 ships no `field_traits.hpp`; its seven dict headers do not include it — `specs/002-dictionary-xml-loader/spec.md:189`) and **never previously claimed in scope here** (Opus N-P2-2). It is the 2c-owned typed-decoding layer on the typed-accessor hot path (`[2c §4.1.3]` ≤20 ns), consumed by `contracts/generated_message.hpp:10` and every string/char accessor.

**Resolution status — BLOCKING, requires re-`/plan` (not closeable by a convergence text patch).** The natural resolution (consistent with 2c §4.3's free-function rationale and 2c §4.1.3's "the formal home is here") is that **003 owns and ships** `version_profile`, `dict::resolve_application_version`, and `<fixpp/dict/field_traits.hpp>` (`field_traits<T>`/`decode_field<T>`) with full `contracts/` extracts, files-in-scope rows, ACs, and error-taxonomy interactions. That materially expands 003's owned surface and must go through `/plan`. **Adding `contracts/version_profile.hpp` + `contracts/field_traits.hpp` extracts alone does NOT close RC#1** (Gate A round 2, Codex F-1 / Opus Confirm @ P1): the re-`/plan` must additionally derive (a) the new `version_profile`/`field_traits`/`decode_field` **acceptance criteria**, (b) the **error-taxonomy interactions** the new surface introduces, (c) the wire `ApplVerID(1128)` → C++ `application_version` **enum-mapping table** per 2c §4.3 (`.specify/2c-codegen.md:478`), (d) the matching `include/fixpp/dict/{version_profile,field_traits}.hpp` files-in-scope + plan Project-Structure rows, and (e) their seam→file test bindings. It is **not** a text patch. Until then these are **explicit blocking upstream dependencies**, not merged ones; AC-D3/AC-D4, story 3.3, the `dict::reify` runtime-dispatch half, and the `field_traits` typed-decoding layer (AC-G4) are specified against an interface no merged feature provides. Tracked in `plan.md` `## Gate A` → Round 1; the `blocked_on_replan` front-matter flag gates `/tasks`.
- **Depends on (third-party):** the codegen host tool may pull a host-side XML/templating dependency — selection deferred to /plan (F1), licence-compatible per `[const §V.3]` (no LGPL), user sign-off + Gate A.
- **Unblocks:** `session/` FSM (typed dispatch + `dict::reify` cross-strand handoff + FIXT cross-vocabulary `[2c §6.3]`); `capi/` 2i (`fixpp_msg_reify` / typed accessors over the stable `wire::MessageView` surface); `bindings/python` 2m; closes `dictionary/` module surface rows #8/#9 (`phase-4/dictionary/README.md`).

## 9. Test seams (carried from `[2c §9]`, scoped to this feature)

1. **Conformance corpus** — every codegen version × owned messages round-trip (parse → typed accessors → `reify_as` → re-serialize). CI: curated ~20-msg/version must-include subset per **AC-G12** (P1 headline + every group-bearing message + 7 FIXT admin + AC-D4 worked-example + msgtype-boundary probes); nightly: exhaustive. The must-include manifest is checked in and Gate-A-reviewed. Runtime-XML-only versions → seam #10c.
2. **Compile-time cost regression** — single-version (≤ 3 s) and all-versions (≤ 15 s soft) TU compile bench; per-header expansion size.
3. **Per-tag accessor latency regression** — `cl_ord_id`/`side`/`order_qty`/`price`/`field_value` (decimal split per N-P2-2).
5. **Codegen lookup latency regression** — `Dictionary::field_ref`/`field_valid_for`/`required_fields`/`group_first_field`/`length_pair_data_tag`/`resolve_application_version` on the codegen-emitted tables.
6. **Reify latency regression** — `reify_as` 20-tag/200-tag; `reify` runtime-dispatch ≤ 1.2 µs; move-across-`std::thread` smoke.
7. **Allocation guard (Linux)** — read path = 0 allocations; reify path ≤ 4 PMR; `mallocnesia`.
10. **Multi-version coexistence** — 10a (multi-session no namespace bleed), 10b (single FIXT.1.1 cross-vocabulary worked example), 10c (runtime-XML-only versions → `dict_reify_unknown_msg_type`).
12. **`owning_<Msg>` cross-strand handoff** — reify on A, move to B, A's arena reset, B reads correct values; original view traps in debug post-reset.
14. **`owning_<Msg>` move + lazy view rebuild** — populate cache, move, moved-to rebuilds; source caches `nullopt`; static-asserts (no ref members, nothrow-move, move not `=default`).
15. **`dict::reify` runtime-dispatch round-trip** — 15a (7 FIXT admin), 15b (4 versions × app MsgTypes; CI = the **AC-G12** curated must-include subset, exhaustive nightly), 15c (`dict_unresolved_application_version` propagation).
16. **`trap_throw` PMR OOM injection** — `reify_as` / `reify` / `owning_<Msg>::from_view` → `dict_reify_oom`; none terminate.
18. **Static-assert typed-flyweight size** — `sizeof(Msg) == sizeof(MessageView<Index> const*)` per message.
19. **Length+Data static-table coverage** — emitted pair table exhaustive vs source XML; cross-checked vs `[FIX50SP2 §3.3]`.

> **Seams intentionally not in this feature:** #4 / #9 / #11 / #17 / #20 (all `DialectOverlay`-merge — defer with 002 F2 / D-009); #8 (XmlLoader fuzz — shipped with 002); #13 (`Dictionary` move + `table_view` refcount — 002 / `table_view` consumer feature). Seam numbers above match `[2c §9]` for traceability; gaps are deliberate, not omissions.

## 10. Follow-ups & deferred work

> Resolutions from /clarify Session 2026-05-15 ratified the scope. Each item below is intentionally unshipped with a concrete trigger. The Spec-Kit `/clarify` marker count is **zero**.

### F1 — `fixpp-codegen` host-tool language/host: deferred to /plan

- **What's deferred:** the implementation language/host of `tools/codegen/fixpp-codegen` (e.g., C++ tool reusing 002's `XmlLoader`/`Dictionary` as IR, vs. a scripting host, vs. a generator DSL).
- **Why:** /clarify Q2 → A. `/specify` stays implementation-agnostic per Spec-Kit guidance; mirrors 002's parser decision (Q3 → defer).
- **Trigger / resolution:** /plan evaluates 2–3 candidates against `[2c §9]` seams + `[const §V.3]` licence; user signs off at /plan; Codex Gate A reviews. Closes at /plan (like 002 F3).

### F2 — `DialectOverlay` codegen-regen path (D-009)

- **What's deferred:** the regenerate-against-venue-XML path (`[2c §4.7.1]` path 2, `[2c §7.1]` reverse direction) producing dialect-private typed accessors + Length+Data supplement; depends on the `DialectOverlay` value type itself (002 follow-up F2 / D-009).
- **Why:** overlay end-to-end is owned by the future D-009 feature; this PR ships only the `field_value(uint16_t)` forwarder (AC-G6) so overlay-promoted tags are *reachable*, not *typed*.
- **Catalogue rows:** D-009, COM-011. **Trigger:** first dialect-overlay customer pull, alongside D-009.

### F3 — `dict::version_registry` ownership/engine wiring (→ 2d)

- **What's deferred:** the construction/ownership model (engine-owned-by-value vs session-borrowed; whether `EngineConfig::dictionaries` is the construction shape; per-engine vs per-session).
- **Why:** `[2c §10]` Q10 defers this to 2d threading + EngineConfig design; v1.0/v1.1 publishes only the `get(application_version) → expected_t<Dictionary const*>` shape (AC-X1..X3).
- **Trigger:** 2d threading / EngineConfig design feature.

### F4 — Compile-time cost spike on the all-versions TU

- **What's deferred:** confirming the ≤ 15 s all-versions soft ceiling on engine-target hardware; PCH adoption / `[SYN §3.3 Q11]` v1.x preview if hot.
- **Why:** `[2c §10]` Q1; the single-version ≤ 3 s ceiling is the load-bearing one in v1.0; all-versions TU is "not supported by default" (N-P2-3).
- **Trigger:** post-Gate-A implementation spike against a representative all-four-versions translator TU; reopen `[SYN §3.3 Q11]` if the soft ceiling is hot.

### F5 — Runtime-XML-only-version codegen (post-v1.0)

- **What's deferred:** typed namespaces for FIX 4.0/4.1/4.3/5.0/5.0SP1 (`[2c §10]` Q4 priority order: 4.3 first, then 5.0SP1, 5.0, 4.0/4.1). The XML data for these versions is itself a 002 follow-up F1 deferral.
- **Why:** `[2c §1.3]` runtime-XML-only; post-v1.0 best-effort per Appendix D amendment. This PR's only obligation is the clean negative path (AC-D5).
- **Trigger:** `[const §XVIII]` post-1.0 roadmap once Appendix D's amendment lands + a real consumer pull.

### F6 — D-010 component-shell codegen (per-version generated `Instrument`/`Parties`/… shells)

- **What's deferred:** the per-version generated *component* shells 2c §4.2 (`.specify/2c-codegen.md:1904`) designs — non-repeating component bundles (`Instrument`, `Parties`, `OrderQtyData`) emitted under each codegen namespace, distinct from the repeating-group `Leg` flyweight (AC-G5) this PR does ship.
- **Why:** Gate A round 1 (Codex P2-2 / Opus Confirm @ P2) — D-010 was claimed in front-matter/§13 but never made testable here (no AC/contract/test/seam). Honest scoping: drop the claim now, ship the component-shell surface as its own feature with explicit ACs/contract/test.
- **Catalogue rows:** D-010. **Trigger:** first consumer needing typed component access, scoped at its own `/specify`+`/plan`+Gate A; re-derive from 2c §4.2.

## 11. Risk register

- **R1 — Codegen host-tool dependency choice.** Open until /plan (F1). Mitigation: 2–3 candidate eval against `[2c §9]` seams; user sign-off + Gate A; licence anchor `[const §V.3]`.
- **R2 — Generated-header compile-time blow-up.** ~1.5 MB/all-versions; single-version ≤ 3 s is load-bearing. Mitigation: compile-time bench seam #2 in scope from day one; per-version CMake targets (`[2c §7.6]`) so consumers pay only for what they include; F4 spike.
- **R3 — Dispatch-switch generation regression (~470 cases).** A missing/wrong case silently misdispatches. Mitigation: seam #15b exhaustive nightly + representative CI; `dict_reify_unknown_msg_type` default arm is fail-loud.
- **R4 — `owning_<Msg>` lazy-view move correctness.** Defaulted move on `optional` would leave a stale cache aliasing pre-move bytes. Mitigation: custom `noexcept` move mandated (AC-R4); seam #14 incl. static-asserts that the move is not `= default`.
- **R5 — Codegen determinism / source-tree pollution.** Non-deterministic emission breaks CI diffs; source-tree writes corrupt checkouts. Mitigation: AC-T1/T2; build-tree-only (`[arch §4.2]` step 3); one golden header per codegen version (v42/v44/v50sp2/vt11 — 4 total) checked in at `/implement` (generated codegen output — not present at Gate A), determinism test asserts byte-identical re-emission against all four (NFR-003-7).
- **R6 — `wire::MessageView` dependency ordering + the `dict/` → `wire/` layer edge.** Generated headers depend on the `[2b]` wire surface at compile time, but the wire *feature* is downstream of `dictionary/` in module order. Mitigation: confirm the seam at /plan — the dependency is on the wire *header contract* (`MessageView<Index>::get<Tag>`), which `[2b §4.3]` locks; the wire header contract is vendored as a stable stub. **Gate A round 1 escalation (Codex P1-3 / Opus root cause #3):** the `arch §2.4` carve-out covers only *generated* `fixpp::vXX::*` headers — it does **not** sanction the **hand-written** `include/fixpp/dict/reify.hpp` `#include <fixpp/wire/message_view_contract.hpp>` (`contracts/reify.hpp:8`), nor this feature *writing a new header into the 2b-owned `include/fixpp/wire/` tree*. NFR-003-8 cannot be green-checked as previously asserted. **Open layer-amendment item for re-`/plan`:** either an explicit `[const §XX]` / `arch §2.3` whitelist amendment (add the `dictionary(reify bridge) → wire(contract header)` edge + teach `check_layers.py` the carve-out) or relocate the contract header out of `dict/`'s public include path. Flagged for Gate A; recorded in `plan.md` `## Gate A`.

## 12. Definition of done

- All AC-G*, AC-V*, AC-R*, AC-D*, AC-X*, AC-C*, AC-T* tests pass on all CI presets (debug, release, ASan + UBSan + TSan).
- All NFR-003-* checks pass on at least the release preset; latency/compile-time benches seeded with baselines.
- `fixpp-codegen` runs at configure time, build-tree-only, deterministic (golden-header check green).
- The `[2c §7.6]` CMake target graph builds; a v44-only consumer does not pull other versions (AC-C2).
- Conformance corpus: representative subset green in CI, exhaustive run green nightly.
- `tools/check_layers.py` clean; `clang-tidy`/`clang-format` clean per Tier-1 CI; no `thread_local`/`new`/`delete` in generated output.
- Codex Gate A converged (`[const §XVII.1]`).
- `/speckit-verify` record fully GREEN (`[const §XVII.8]`).
- Codex Gate B converged, high-severity = 0 (medium/low fixes or waivers documented in §10 + the feature sub-file).
- Documentation: `docs/src/dictionary/codegen.md` — "how codegen runs", "the per-version CMake targets", "typed-message accessor model", "the `dict::reify` cross-strand handoff", "supported codegen versions in v1.0".

## 13. References

- Constitution: `.specify/constitution.md` — `[const §I.1]` v1.0 surface; `[const §V.3]` no-LGPL (F1 licence anchor); `[const §VI.5]` Normative References; `[const §VIII.5]` zero-allocation hot path; `[const §XIV.2]` ≤5 pure-virtual; `[const §XV]`/`[const §XV.13]` banned patterns + hybrid mandate; `[const §XVII.1]` Codex Gate A; `[const §XVII.8]` `/speckit-verify` precondition; `[const §XVIII]`/`[const §XVIII.2]`/`[const §XVIII.7]` post-v1 roadmap (FIX-Latest, A-014..A-034 deferral).
- Architecture: `.specify/architecture.md` — `[arch §3]` namespaces; `[arch §4.2]` codegen pipeline; `[arch §4.10]` C-ABI delegation to 2i; `[arch §5.5]` flyweight/lifetimebound; `[arch §5.6]` SessionConfig frozen; `[arch §10]` row 2c handoff.
- Design doc: `.specify/2c-codegen.md` v1.3 — `[2c §1.3]` version coverage; `[2c §4.7]` typed messages; `[2c §4.7.1]` overlay-promoted access; `[2c §4.8]` reify bridge; `[2c §4.9]` version_registry; `[2c §5]` C-ABI commitments; `[2c §6.1]`/`[2c §6.2]`/`[2c §6.3]`/`[2c §6.6]`/`[2c §6.7]` behavioral contract; `[2c §7.6]` CMake targets; `[2c §9]` test seams; `[2c §10]` open questions (Q1/Q4/Q6/Q7/Q8/Q10).
- Sibling docs: `.specify/2a-decimal.md` — `[2a §4.2]` `trap_throw`, `[2a §6.5]` decimal parse latency. `.specify/2b-wire.md` — `[2b §4.3]` `MessageView::get`, `[2b §4.4]` `OffsetTable`, `[2b §4.6]` `dictionary_driven_validator`, `[2b §4.7]` `group_view`, `[2b §6.4]` flyweight lifetime, `[2b §6.6]` view-escape contract.
- Feature 002: `specs/002-dictionary-xml-loader/spec.md` — the merged runtime `Dictionary`/`XmlLoader`/`FieldRef` surface this feature consumes.
- Catalogue: `spec/feature-catalogue.md` — D-008 (codegen, four versions only — coverage-index supplemental note per Appendix D §2), OSS-010 (header-only generated typed messages). **D-010 (component definition support — per-version generated component shells) is NOT claimed by this feature** (Gate A round 1, Codex P2-2 / Opus Confirm @ P2): 2c §4.2 (`.specify/2c-codegen.md:1904`) designs per-version generated component shells (`Instrument`/`Parties`/`OrderQtyData`), but this bundle's ACs stop at messages/groups/Fields/Validator/reify/dispatch/registry — the only group-shell artifact is the *repeating-group* `Leg` flyweight (AC-G5), which is not a non-repeating *component* shell. No AC, no contract, no test, no seam binds component-shell emission, so D-010 is explicitly out of scope here and deferred to a follow-on feature (tracked §10 F6). Re-claiming D-010 would require explicit component-shell ACs/contract/test added at re-`/plan`.
- Spec references: `[FIX50SP2 §3.3]` field-type/Length+Data vocabulary; `[FIXT §5]`/`[FIXT §5.1]`/`[FIXT §5.3]` ApplVerID resolution; `[FIX42]`/`[FIX44]` application specs.

## Assumptions

- **A1.** The four `dictionaries/{FIX42,FIX44,FIX50SP2,FIXT11}.xml` files merged by 002 (pinned to the upstream QuickFIX commit recorded in `dictionaries/README.md`) are the codegen input; no new XML in this PR.
- **A2.** Generated headers depend on the `[2b]` `wire::MessageView<Index>` header contract at compile time. The contract (`get<Tag>()`/`get(uint16_t)`/`group<>()`) is locked by `[2b §4.3]`/`[2b §4.7]`; R6 records the build-ordering risk for /plan.
- **A3.** "Codegen emits the full standard message set" means the tool emits every `<message>` in the locked `[2c §1.3]` set present in the source XML; A-014..A-034 and FIX-Latest are filtered at emit (AC-G9/G10), not partially emitted.
- **A4.** Determinism (NFR-003-7) is achieved by sorted, locale-independent (bytewise) emission ordering — the same invariant 002 uses for `Dictionary` storage — so generated headers are byte-stable without a post-emit sort.
- **A5.** The conformance corpus uses public sample messages (QuickFIX `examples/*.dat`, public exchange specs, ICAP regression set per `[2c §9]` seam #1); no proprietary message data is checked in.
- **A6.** ~~`version_profile` (shipped by 002) is the resolution input for `dict::reify`; `dict::resolve_application_version` is the 002-shipped free function this feature calls (no re-derivation here).~~ **Retracted (Gate A round 1, Codex P1-1 / Opus root cause #1).** 002 did NOT ship `version_profile` or `dict::resolve_application_version` — both are explicitly deferred per `specs/002-dictionary-xml-loader/contracts/version_profile.hpp:59-66`. Corrected dependency status and resolution path (003-owned, requires re-`/plan`) are in §8 "Upstream dependency audit". `dict::reify` consumes `version_profile` as its resolution input and calls `dict::resolve_application_version`; both are now 003-owned blocking surface, re-derived at re-`/plan` from 2c §4.3 (including the wire `ApplVerID(1128)` → C++ `application_version` enum-mapping table at `.specify/2c-codegen.md:478`).
