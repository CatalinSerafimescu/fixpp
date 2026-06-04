# Implementation Plan: G2 Business-Message Interop — typed NewOrderSingle + ExecutionReport

**Branch**: `020-g2-business-messages` | **Date**: 2026-06-04 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/020-g2-business-messages/spec.md`

## Summary

Deliver a **minimal typed business-message surface** for **NewOrderSingle (35=D, A-001)** and **ExecutionReport (35=8, A-006)** and use it to drive a live `Logon → NewOrderSingle → ExecutionReport → Logout` round-trip against QuickFIX-J and QuickFIX-cpp in both roles — discharging the open `[const §VII.6]` v1.0-GA business-message interop clause (FR-027/SC-008).

**Key design pivot (from [research.md](./research.md)):** the **read** half already exists. The 003 codegen (merged) emits `fixpp::v44::NewOrderSingle` / `fixpp::v44::ExecutionReport` flyweight accessors (`cl_ord_id()`, `symbol()`, `side()`, `order_qty(mr)`, `ord_type()`, `price(mr)`, `transact_time()`, …) returning `expected_t<T>` (`decimal_t` for numerics) — G2 **uses** these, it does not re-implement them (D2). What the codegen does **not** emit is a **write/serialize** path (`owning_<Msg>` in `Reify.hpp` is a read-only deep-copy of an already-parsed frame, no setters/serialize) — so G2's genuine new library surface is a **minimal hand-written builder** that turns typed fields (incl. `fixpp::decimal_t`) into the app-body wire bytes consumed by 019's `Engine::send` (D3). The order type is **Limit-only** (OrdType=2, Price always required); the responding ExecutionReport is **fully-filled** (150=F Trade / 39=2 Filled, LeavesQty=0, CumQty=OrderQty, AvgPx=Price); numeric fields are typed as `fixpp::decimal_t` (= `core::decimal<FIXPP_DECIMAL_T>`; clarifications 2026-06-04).

**Primary Gate-A risk (D1) — TWO send-path framing defects, not one:** `Session::send_impl` stamps `8/9/34/49/52/56` then appends the app payload whose first field is `35=…`, so (a) on the wire **MsgType(35) lands 7th, not 3rd**, AND (b) it backpatches **zero-padded** BodyLength (`9=000045`), violating fixpp's own digit-only `9=` wire contract (`.specify/2b-wire.md`; `wire::Writer::commit()` memmoves to avoid exactly this). fixpp's lenient parser accepts both (why the 019 opaque loopback witness passed), but QuickFIX/Fix8 enforce the "BeginString, BodyLength, MsgType = first three fields" rule and reject non-canonical `Length`. The send path therefore must **(i) hoist MsgType to field-3, (ii) emit digit-only BodyLength, (iii) keep the checksum valid, AND (iv) validate the opaque payload fail-closed** (one leading `35=`, no embedded session tags, `error::app_payload_malformed` slot 131) — recommended via routing app sends through `wire::Writer` (FR-004a/FR-016). Fixing only MsgType is a half-restructure ([[feedback_half_restructure_symmetric_api]]). This touches the **proven 015/019 production send path** → carries a real Gate B (Complexity Tracking). Validated empirically by the first US2 cell AND by a RED unit on the **captured/stored bytes** (INV-1/INV-8) — not the lenient `fromApp` value.

Builds directly on **019** (`Application` `fromApp`/`toApp`, any-thread `Engine::send`, `BusinessMessageReject(35=j)` builder) and the **016/018** live-interop harness (engine-log seam goldens, both-role orchestration, `one_way_ca` TLS, skip-without-counterparty). Fix8 stays corpus-only.

## Technical Context

**Language/Version**: C++23 (Clang; `std::expected`, coroutines) — [const §II]
**Primary Dependencies**: existing `wire::Writer`/`MessageView`, generated `fixpp::v44::{NewOrderSingle,ExecutionReport}` (003 codegen), `fixpp::decimal_t` (= `core::decimal<FIXPP_DECIMAL_T>`, 001), 019 `Application`/`Engine::send`, `core::{error,expected_t}` — no new third-party deps
**Storage**: N/A (reuses `Session::send` durable-before-transmit path; no new persistence)
**Testing**: GoogleTest + GoogleMock; sanitizers ASan/UBSan/TSan; coverage llvm-cov; live interop ctest cells (skip-without-counterparty) — [const §VII, §IX]
**Target Platform**: Linux/Clang (Tier 1); Windows/MSVC (Tier 2)
**Project Type**: single C++ library (`fixpp`) + tests-only interop harness extension (parent `phase-9-harness/`)
**Performance Goals**: build/parse on the app send/recv path stays allocation-disciplined — the **builder write path** writes into a caller stack buffer via hand-written body-only `tag=value\x01` field append (no `wire::Writer` body-only mode) (no heap; `counting_resource` witness); the **read path** is NOT zero-copy — numeric accessors (`order_qty(mr)`/`price(mr)`/…) materialize a `decimal_t` via `decimal_t::parse(bytes, mr)`, allocating from the **caller-supplied PMR arena** (no global `new`) — [const §VIII.5]
**Constraints**: builder is `noexcept` + `expected_t` (house style); numeric fields serialize via `decimal_t::format(span)` canonical locale-independent form (FR-007); the builder emits the app **body** only (no session header tags `8/9/34/49/52/56`, no `10=` trailer — those are engine-stamped), leading with `35=`; the send path must place MsgType at field-3 + emit digit-only BodyLength + validate the opaque payload (D1/FR-004a/FR-016); no `std::mutex` in awaitable headers ([const §XV.9])
**Scale/Scope**: 2 typed builders (NOS + ExecRpt, minimal fields) + 1 send-path MsgType-ordering fix + read-side consumption of generated v44 flyweights + live interop cells (QFJ + QFcpp, both roles) + a responding counterparty `Application` per engine; bounded — no codegen-emitter change, no new message types, no full-field coverage

## Constitution Check

*GATE: must pass before Phase 0 (passed) and re-checked after Phase 1 (below).*

| Article | Gate | Status |
|---------|------|--------|
| **II** Language | C++23/Clang, no new deps | ✅ PASS |
| **VI** Spec coverage | A-001/A-006 are codegen-owned **all-version (4.0–5.0SP2) official** rows; this minimal-FIX-4.4 hand-written slice does NOT close them. Catalogue update = A-001/A-006 stay `backlog` + **gap-note** (partial G2 interop evidence, cite 020) + coverage-index **partial-evidence note** (NOT a closure) at Polish (FR-014) | ⚠ RESOLVED (no row flip — partial-evidence note only; exact coverage-index delta below) |
| **VII** Testing/TDD | builder build/round-trip, missing-required, decimal/timestamp fidelity, live cells land red-first; GoogleTest | ✅ planned |
| **VII.6** Interop | **this feature DISCHARGES the open v1.0-GA business-message interop clause** (`Logon→NOS→ExecRpt→Logout` vs QFJ/QFcpp both roles) | ✅ discharging |
| **VIII.5** Allocator | builder **write** path writes into caller buffer via hand-written body-only `tag=value\x01` field append (no `wire::Writer` body-only mode) (no heap — `counting_resource` witness); **read** path is NOT zero-copy — `decimal_t::parse(mr)` materializes decimals into the **caller-supplied PMR arena** (no global `new`); both arms have an alloc-discipline witness | ✅ by design (write=no-heap, read=caller-arena) |
| **IX.1** Coverage | ≥95/85 on the new builder TU + touched send path; missing-required + decimal-edge are genuine error paths ⇒ tested | ✅ planned |
| **IX.2** Sanitizers | ASan/UBSan/TSan on the send-path MsgType-ordering change + interop ctest (per 018 discipline) | ✅ planned |
| **X** ABI | C ABI for typed messages explicitly **out of scope** (Phase-5 later) ⇒ abidiff does not bind new typed surface; the send-path change is internal | ✅ N/A |
| **XI.4** Threading | reuses 019's strand/keepalive contract for `Engine::send`; no NEW concurrency surface — but the fixpp-acceptor responder calls `Engine::send` **from inside `fromApp`** (re-entrant), a path 019's single-threaded harness never exercised (L-019-3); RELIES on 019's any-thread re-entrant `Engine::send`, **demonstrated** by a named multi-threaded loopback test (INV-7) before live cells, not assumed (D9) | ✅ PASS (re-entrancy test-gated) |
| **XIV.2** Pluggable ≤5 pure-virtual | no new pluggable interface; builders are free functions, read is a generated flyweight | ✅ N/A |
| **XV.9** Banned (std::mutex in awaitable hdr) | builder header is a plain (non-awaitable) header — `noexcept` free functions, no asio, no mutex | ✅ PASS |
| **XV.13** No eager codegen w/o runtime path | we **consume** generated read flyweights (the sanctioned codegen output) and **hand-write** a minimal builder (the opposite of eager codegen — targeted, two messages). Codegen-emitted *builders* (full coverage) are tracked as the deferred path (FR-015a), not eagerly generated now | ✅ PASS |
| **XV.15** No app-message drop | send uses 019's awaited `Engine::send` backpressure; no new queue | ✅ PASS |
| **XVI.3** /clarify before /plan | `/speckit-clarify` Session 2026-06-04 (3 axes: order-type, exec semantics, numeric type) ✅ | ✅ PASS |
| **XVII.1** Gate A before /tasks | mandatory — runs after this plan, before `/speckit-tasks` | ⚠ Gate A PENDING |

**Result**: PASS to proceed. The send-path framing change (D1 — MsgType field-3 + digit-only BodyLength + opaque-payload validation, FR-004a/FR-016) is a real production-behavior change → carries a genuine Gate B; it is surfaced in Complexity Tracking for Gate-A scrutiny. A-001/A-006 are NOT closed (partial-evidence note only, FR-014/§VI). No unjustified violations; the only outstanding *gates* are the mandatory downstream controls (Gate A, `/analyze`, `/plan` sign-off).

**Exact coverage-index delta (§VI, written before `/speckit-tasks`):** in `spec/coverage-index.md`, against the FIX Application Layer rows for **A-001 (NewOrderSingle 35=D)** and **A-006 (ExecutionReport 35=8)**, add a **Gap note** of the form: *"Partial G2 interop evidence (020-g2-business-messages): minimal FIX-4.4 typed NOS→ExecRpt builder + live both-role interop vs QuickFIX-J/cpp; full-field + all-version (4.2/5.0SP2/FIXT.1.1) coverage deferred (FR-015a/FR-015b). Row stays backlog."* In `spec/feature-catalogue.md`, A-001/A-006 **status stays `backlog`**; append the same partial-evidence gap-note in the row's notes column (mirroring how 016/018 recorded interop evidence without closing wire-class rows). No row flips to `done`.

## Project Structure

### Documentation (this feature)

```text
specs/020-g2-business-messages/
├── plan.md              # this file
├── research.md          # Phase 0 (D1–D8)
├── data-model.md        # Phase 1 (the two messages + minimal fields + validation + send-order invariant)
├── quickstart.md        # Phase 1 (user-facing build→send→read example)
├── contracts/
│   └── business-messages.md   # Phase 1 (the typed builder contract)
└── tasks.md             # Phase 2 (/speckit-tasks — NOT created here)
```

### Source Code (repository root)

```text
include/fixpp/session/
├── business_messages.hpp   # NEW — minimal typed builders: build_new_order_single(...), build_execution_report(...)
│                           #       span-in, noexcept, expected_t<span<byte>> out; same house build-shape as admin_messages.hpp
│                           #       (span-in/noexcept/expected_t; hand-written body-only append — not `wire::Writer`) — but emits app BODY (not a complete frame);
│                           #       leads with 35=D/35=8, NO 8/9/34/49/52/56/10; decimal_t fields via decimal_t::format(span)
└── session.hpp / session.cpp  # EDIT — app-send path: hoist MsgType(35) to field-3 + digit-only BodyLength + opaque-payload
                                #        validation (error::app_payload_malformed slot 131) (D1/FR-004a/FR-016)

src/session/
└── business_messages.cpp   # NEW — builder bodies via hand-written body-only field append + fixpp::decimal_t::format

include/fixpp/core/
└── error.hpp               # EDIT — new enumerator app_payload_malformed = 131 (next free after 019's 129/130)

# READ side: NO new source — consume generated build/<preset>/_codegen/include/fixpp/v44/{Messages,Reify}.hpp
#            (fixpp::v44::NewOrderSingle / ExecutionReport flyweight accessors)

tests/session/
├── test_business_messages_build.cpp     # US1: build NOS/ExecRpt → wire-conformant body; parse-back fidelity (decimal_t value-equality);
│                                         #      invalid-field (empty/enum/decimal/timestamp/too-small) → typed error + no usable output (INV-4);
│                                         #      Builder_Output_ContainsNoEngineTags — body has no 8/9/34/49/52/56/10 (INV-2);
│                                         #      Builder_InvalidField_NoUsableOutput — atomicity, out unspecified on failure (INV-4);
│                                         #      Builder_NumericFidelity_DecimalValueEquality (INV-3); Builder_NoHeap_CountingResource (§VIII.5 witness)
├── test_business_messages_read.cpp      # US1: read inbound 35=D/35=8 via generated fixpp::v44 accessors with field fidelity;
│                                         #      missing/ill-typed required field → accessor expected_t error (INV-6 read; FR-006)
├── test_business_messages_roundtrip.cpp # US1: loopback engine send (typed) → fromApp → v44 flyweight read fidelity;
│                                         #      SendPath_StoredFrame_Field3MsgType_UnpaddedBodyLength_ValidChecksum — assert CAPTURED
│                                         #        transport_send/stored bytes: field-3==35, digit-only 9=, valid 10= (INV-1, RED before fix);
│                                         #      OpaquePayload_Malformed_RejectedNoSeqnumConsumed — FR-016 fail-closed (INV-8);
│                                         #      SendFromInsideFromApp_NoDeadlockNoUAF — re-entrant Engine::send under multi-threaded
│                                         #        io_context, BEFORE live cells (INV-7, D9);
│                                         #      InboundReject_EmitsBusinessMessageReject_SessionSurvives — fromApp reject → 35=j, Active (INV-5, FR-009/SC-005)
└── (interop cells below — tests/interop/)

tests/interop/   (in-repo SUT side, per 016/018)
└── test_business_message_interop.cpp    # US2/US3: live NOS→ExecRpt vs QFJ + QFcpp, both roles; seam capture; skip-without-counterparty

# Parent live-harness (tests-only, NOT the submodule):
research/.../phase-9-harness/
├── quickfixj/  + quickfix-cpp/counterparty/   # EDIT — responding Application: emit ExecutionReport per NewOrderSingle
├── tools/run_interop_cell.py + emit_matrix.py # EDIT — new business-message cells + goldens
└── golden/<cell>.fix                          # NEW — business-message goldens (52=/60=/IDs/seqnum normalized;
                                                #        decimal fields 38/44/151/14/6 compared by decimal_t value, not byte — D7/FR-012)
```

**Structure Decision**: the **read** surface is the generated `fixpp::v44` codegen output (sanctioned by `[arch §4.2]`, header-only, already merged) — consumed, not added. The **write** surface (the minimal builders) is hand-written and placed in the existing **`session/`** module.

**Placement grounding (corrected, Gate A round 1).** `[arch §4.4]` enumerates the `session/` public surface as `Session/Application/MessageStore/.../Engine/SessionId` — it does **not** list message-body builders, and `admin_messages.hpp` is itself unlisted yet lives in `session/`. So the placement rests on **two real grounds, not a §4.4 sanction**: (i) **de-facto precedent** — `admin_messages.hpp` already places body/frame builders in `session/`; (ii) the **`tools/check_layers.py` gate** enforces actual layer direction at `/implement` ([[feedback_gate_b_check_layers_post_fixer]] — architecture.md is the layer authority, design prose is not binding). **Two earlier prose claims are corrected as factually wrong:** (a) these builders do **not** "mirror the `admin_messages` emit path" — admin builders (`build_logon`/…) take `begin_string`/`sender`/`target`/`seqnum`/`sending_time` and emit **complete self-contained frames** (8/9/34/35/…) sent via `store_then_emit(seq, frame)`; the 020 builders emit **bodies only** (no header/trailer) consumed by `send_impl`'s app-payload-append path. They share only the *house build-shape* (span-in / `noexcept` / `expected_t` / `wire::Writer` / stack-buffer), not the output shape or the send path. (b) §4.2 places typed messages in **codegen**, so a hand-written body builder is a **genuinely new hand-written surface** — it needs an explicit one-line `architecture.md` amendment sanctioning `session/business_messages.hpp` as a hand-written body-builder bridge (flagged as a Gate-A-eligible justification; the FR-015a codegen writer-emitter is the deferred full path), NOT a claim that §4.4 already covers it. **Gate-A note:** hand-writing builders (vs codegen-emitting them) is a deliberate minimal-scope deviation from "typed messages are codegen output" — justified in Complexity Tracking. The live harness work is parent-tracked `phase-9-harness/` (tests-only), reusing 016/018 infrastructure.

## Complexity Tracking

> Two items surfaced for Codex Gate A.

| Item | Why needed | Why the simpler form is insufficient |
|------|-----------|--------------------------------------|
| **Hand-written minimal builders** for NOS/ExecRpt rather than codegen-emitted typed writers (`[arch §4.2]` says typed messages are codegen output) | v1.0 is **minimal** (user-directed): two messages, minimal field set. The codegen emits *read* flyweights for all messages but **no writer** today; adding a writer emitter would generate writers for the *entire* message set — exactly the deferred full-coverage scope (FR-015a) — and is a far larger, emitter-touching change. | A codegen writer-emitter is the *correct full-coverage path* but disproportionate to a minimal two-message v1.0 slice; hand-writing two `build_*` functions (mirroring the merged `admin_messages.hpp` pattern) is the smallest surface that ships the interop proof. The full-coverage codegen path is tracked as FR-015a so this is a documented bridge, not a dead-end. |
| **Send-path framing change** (hoist 35 to field-3 + digit-only BodyLength + opaque-payload validation on app sends) touches the proven 015/019 production send path | QuickFIX/Fix8 reject app messages whose MsgType is not the third field AND non-canonical (zero-padded) BodyLength; 019's `send_impl` emits MsgType 7th AND `9=000045`. Public `Engine::send` also copies arbitrary opaque payloads with zero validation. Without all three, **no live business-message interop is possible** (US2/US3 fail at the peer's parser) and malformed opaque sends corrupt the session. | Leaving order/padding as-is "works" only against fixpp's own lenient parser (the 019 loopback witness — a passed-for-wrong-reason artifact per [[feedback_single_threaded_harness_masks_strand_races]] class). Fixing only MsgType is a half-restructure ([[feedback_half_restructure_symmetric_api]]) leaving the same path emitting padded `9=`. The contained fix routes app sends through `wire::Writer` (field-3 + digit-only `9=` + checksum) and adds fail-closed payload validation (`error::app_payload_malformed`=131) before seqnum assignment; it additionally corrects 019's latent opaque-path defects. The alternative (per-message header pre-assembly in every builder) duplicates header logic across builders. |

## Normative References

Per `[const §VI.5]` (exact `[DocAbbrev §X.Y.Z] Title` coverage-index form): `[const §VII.6]` (the business-message interop clause this feature discharges), `[const §VIII.5]` (allocation discipline — builder write = no-heap, read = caller-arena), `[const §IX.1/§IX.2]` (coverage + sanitizers on the send-path change + interop), `[const §XV.13]` (codegen/runtime hybrid — read flyweights consumed, builder hand-written minimal), `[arch §4.2]` (`fixpp::v44` generated typed read messages = codegen output), `[arch §4.4]` (`session/` public surface — `Session/Application/Engine/…`; the hand-written `session/business_messages.hpp` body builders need an explicit §4.4 amendment per the Structure Decision, grounded on the `admin_messages.hpp` precedent + `check_layers.py`, NOT a claim §4.4 already lists builders), `[FIX50SP2] Single General Order Handling` (NewOrderSingle 35=D / ExecutionReport 35=8 field semantics; matches the A-001/A-006 catalogue rows), `[FIX50SP2 §3.1] Standard header` (header field-order rule: BeginString(8)/BodyLength(9)/MsgType(35) first three, mandatory ordering + digit-only BodyLength — D1/FR-004a; coverage-index §3.1 ↔ W-002/W-004), `[FIX50SP2] Infrastructure / Business Rejects` (A-014 `BusinessMessageReject(35=j)` reuse from 019). Interop roadmap FR-027/SC-008 (G2).

## Gate A

- _Runs after this plan, before `/speckit-tasks` (per [const §XVII.1])._
- Round 1 applied 2026-06-04: Codex P1=4 P2=6 P3=3; Opus post-judging P1=4 P2=10 P3=4; rewrite addresses root causes RC1 (catalogue overclaim — A-001/A-006 stay backlog + gap note), RC2 (numeric-type fiction — core::Decimal→decimal_t pervasively), RC3 (half-scoped send-path fix — MsgType field-3 + digit-only BodyLength + new opaque-payload validation slot 131), RC4 (false admin_messages/§4.4 grounding). Reviews: research/reviews/codex_020-g2-business-messages_gate_a_review.md, research/reviews/opus_020-g2-business-messages_gate_a_adversarial_review.md.
