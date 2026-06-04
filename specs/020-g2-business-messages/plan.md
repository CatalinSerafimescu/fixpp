# Implementation Plan: G2 Business-Message Interop — typed NewOrderSingle + ExecutionReport

**Branch**: `020-g2-business-messages` | **Date**: 2026-06-04 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/020-g2-business-messages/spec.md`

## Summary

Deliver a **minimal typed business-message surface** for **NewOrderSingle (35=D, A-001)** and **ExecutionReport (35=8, A-006)** and use it to drive a live `Logon → NewOrderSingle → ExecutionReport → Logout` round-trip against QuickFIX-J and QuickFIX-cpp in both roles — discharging the open `[const §VII.6]` v1.0-GA business-message interop clause (FR-027/SC-008).

**Key design pivot (from [research.md](./research.md)):** the **read** half already exists. The 003 codegen (merged) emits `fixpp::v44::NewOrderSingle` / `fixpp::v44::ExecutionReport` flyweight accessors (`cl_ord_id()`, `symbol()`, `side()`, `order_qty(mr)`, `ord_type()`, `price(mr)`, `transact_time()`, …) returning `expected_t<T>` (`decimal_t` for numerics) — G2 **uses** these, it does not re-implement them (D2). What the codegen does **not** emit is a **write/serialize** path (`owning_<Msg>` in `Reify.hpp` is a read-only deep-copy of an already-parsed frame, no setters/serialize) — so G2's genuine new library surface is a **minimal hand-written builder** that turns typed fields (incl. `core::Decimal`) into the app-body wire bytes consumed by 019's `Engine::send` (D3). The order type is **Limit-only** (OrdType=2, Price always required); the responding ExecutionReport is **fully-filled** (150=F/39=2, LeavesQty=0, CumQty=OrderQty, AvgPx=Price); numeric fields are typed as `core::Decimal` (clarifications 2026-06-04).

**Primary Gate-A risk (D1):** `Session::send_impl` stamps `8/9/34/49/52/56` then appends the app payload whose first field is `35=…`, so on the wire **MsgType(35) lands 7th, not 3rd**. fixpp's lenient parser accepts it (why the 019 opaque loopback witness passed), but QuickFIX/Fix8 enforce the "BeginString, BodyLength, MsgType = first three fields" rule and will reject an app message with MsgType out of position. The send path therefore must **hoist MsgType to field-3 position** for app sends — a small, contained production-send change that *also* corrects 019's latent opaque-path ordering. This is the headline item for live-interop validation (proved empirically by the first US2 cell) and Gate-A scrutiny.

Builds directly on **019** (`Application` `fromApp`/`toApp`, any-thread `Engine::send`, `BusinessMessageReject(35=j)` builder) and the **016/018** live-interop harness (engine-log seam goldens, both-role orchestration, `one_way_ca` TLS, skip-without-counterparty). Fix8 stays corpus-only.

## Technical Context

**Language/Version**: C++23 (Clang; `std::expected`, coroutines) — [const §II]
**Primary Dependencies**: existing `wire::Writer`/`MessageView`, generated `fixpp::v44::{NewOrderSingle,ExecutionReport}` (003 codegen), `core::Decimal`/`decimal_t` (001), 019 `Application`/`Engine::send`, `core::{error,expected_t}` — no new third-party deps
**Storage**: N/A (reuses `Session::send` durable-before-transmit path; no new persistence)
**Testing**: GoogleTest + GoogleMock; sanitizers ASan/UBSan/TSan; coverage llvm-cov; live interop ctest cells (skip-without-counterparty) — [const §VII, §IX]
**Target Platform**: Linux/Clang (Tier 1); Windows/MSVC (Tier 2)
**Project Type**: single C++ library (`fixpp`) + tests-only interop harness extension (parent `phase-9-harness/`)
**Performance Goals**: build/parse on the app send/recv path stays allocation-disciplined — builder writes into a caller stack buffer via `wire::Writer` (no heap); read uses zero-copy flyweight accessors (`decimal_t::parse` takes a caller `mr`) — [const §VIII.5]
**Constraints**: builder is `noexcept` + `expected_t` (house style); numeric fields serialize via `Decimal`'s canonical locale-independent form (FR-007); the builder emits the app **body** only (no session header tags `8/9/34/49/52/56` — those are engine-stamped), leading with `35=`; MsgType must reach field-3 on the wire (D1); no `std::mutex` in awaitable headers ([const §XV.9])
**Scale/Scope**: 2 typed builders (NOS + ExecRpt, minimal fields) + 1 send-path MsgType-ordering fix + read-side consumption of generated v44 flyweights + live interop cells (QFJ + QFcpp, both roles) + a responding counterparty `Application` per engine; bounded — no codegen-emitter change, no new message types, no full-field coverage

## Constitution Check

*GATE: must pass before Phase 0 (passed) and re-checked after Phase 1 (below).*

| Article | Gate | Status |
|---------|------|--------|
| **II** Language | C++23/Clang, no new deps | ✅ PASS |
| **VI** Spec coverage | catalogue **A-001/A-006 → done** + coverage-index at Polish | ⚠ TODO (catalogue step) |
| **VII** Testing/TDD | builder build/round-trip, missing-required, decimal/timestamp fidelity, live cells land red-first; GoogleTest | ✅ planned |
| **VII.6** Interop | **this feature DISCHARGES the open v1.0-GA business-message interop clause** (`Logon→NOS→ExecRpt→Logout` vs QFJ/QFcpp both roles) | ✅ discharging |
| **VIII.5** Allocator | builder writes into caller buffer via `wire::Writer` (no heap); read accessors zero-copy, `decimal_t::parse(mr)` caller-provided arena | ✅ by design |
| **IX.1** Coverage | ≥95/85 on the new builder TU + touched send path; missing-required + decimal-edge are genuine error paths ⇒ tested | ✅ planned |
| **IX.2** Sanitizers | ASan/UBSan/TSan on the send-path MsgType-ordering change + interop ctest (per 018 discipline) | ✅ planned |
| **X** ABI | C ABI for typed messages explicitly **out of scope** (Phase-5 later) ⇒ abidiff does not bind new typed surface; the send-path change is internal | ✅ N/A |
| **XI.4** Threading | reuses 019's strand/keepalive contract for `Engine::send`; no new concurrency surface (research assumption) | ✅ PASS |
| **XIV.2** Pluggable ≤5 pure-virtual | no new pluggable interface; builders are free functions, read is a generated flyweight | ✅ N/A |
| **XV.9** Banned (std::mutex in awaitable hdr) | builder header is a plain (non-awaitable) header — `noexcept` free functions, no asio, no mutex | ✅ PASS |
| **XV.13** No eager codegen w/o runtime path | we **consume** generated read flyweights (the sanctioned codegen output) and **hand-write** a minimal builder (the opposite of eager codegen — targeted, two messages). Codegen-emitted *builders* (full coverage) are tracked as the deferred path (FR-015a), not eagerly generated now | ✅ PASS |
| **XV.15** No app-message drop | send uses 019's awaited `Engine::send` backpressure; no new queue | ✅ PASS |
| **XVI.3** /clarify before /plan | `/speckit-clarify` Session 2026-06-04 (3 axes: order-type, exec semantics, numeric type) ✅ | ✅ PASS |
| **XVII.1** Gate A before /tasks | mandatory — runs after this plan, before `/speckit-tasks` | ⚠ Gate A PENDING |

**Result**: PASS to proceed. The send-path MsgType-ordering change (D1) is a real production-behavior change → carries a genuine Gate B; it is surfaced in Complexity Tracking for Gate-A scrutiny. The only outstanding *gates* are the mandatory downstream controls (Gate A, `/analyze`, `/plan` sign-off). No unjustified violations.

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
│                           #       span-in, noexcept, expected_t<span<byte>> out; parallels admin_messages.hpp;
│                           #       emits app body (leads with 35=D/35=8, NO 8/9/34/49/52/56); Decimal fields via Decimal::format
└── session.hpp / session.cpp  # EDIT — send path hoists MsgType(35) to field-3 position on app sends (D1)

src/session/
└── business_messages.cpp   # NEW — builder bodies over wire::Writer + core::Decimal::format

# READ side: NO new source — consume generated build/<preset>/_codegen/include/fixpp/v44/{Messages,Reify}.hpp
#            (fixpp::v44::NewOrderSingle / ExecutionReport flyweight accessors)

tests/session/
├── test_business_messages_build.cpp     # US1: build NOS/ExecRpt → wire-conformant body; parse-back fidelity;
│                                         #      missing-required → typed error + no emission; decimal/timestamp edges
├── test_business_messages_roundtrip.cpp # US1: loopback engine send (typed) → fromApp → v44 flyweight read fidelity;
│                                         #      MsgType-on-wire-field-3 assertion (D1 regression, RED before send-path fix)
└── (interop cells below — tests/interop/)

tests/interop/   (in-repo SUT side, per 016/018)
└── test_business_message_interop.cpp    # US2/US3: live NOS→ExecRpt vs QFJ + QFcpp, both roles; seam capture; skip-without-counterparty

# Parent live-harness (tests-only, NOT the submodule):
research/.../phase-9-harness/
├── quickfixj/  + quickfix-cpp/counterparty/   # EDIT — responding Application: emit ExecutionReport per NewOrderSingle
├── tools/run_interop_cell.py + emit_matrix.py # EDIT — new business-message cells + goldens
└── golden/<cell>.fix                          # NEW — business-message goldens (52=/60=/IDs/seqnum normalized)
```

**Structure Decision**: the **read** surface is the generated `fixpp::v44` codegen output (sanctioned by `[arch §4.2/§4.4]`, header-only, already merged) — consumed, not added. The **write** surface (the minimal builders) is hand-written and placed in the existing **`session/`** module alongside `admin_messages.hpp`, which already owns the engine-side message-body builders for the send path (`build_logon`/`build_heartbeat`/…). Header placement + layer direction are cross-checked against `.specify/architecture.md` §4.4 at `/implement` via `tools/check_layers.py` ([[feedback_gate_b_check_layers_post_fixer]]). **Gate-A note:** hand-writing builders (vs codegen-emitting them) is a deliberate minimal-scope deviation from "typed messages are codegen output" — justified in Complexity Tracking; codegen-emitted builders are the deferred full-coverage path (FR-015a). The live harness work is parent-tracked `phase-9-harness/` (tests-only), reusing 016/018 infrastructure.

## Complexity Tracking

> Two items surfaced for Codex Gate A.

| Item | Why needed | Why the simpler form is insufficient |
|------|-----------|--------------------------------------|
| **Hand-written minimal builders** for NOS/ExecRpt rather than codegen-emitted typed writers (`[arch §4.2]` says typed messages are codegen output) | v1.0 is **minimal** (user-directed): two messages, minimal field set. The codegen emits *read* flyweights for all messages but **no writer** today; adding a writer emitter would generate writers for the *entire* message set — exactly the deferred full-coverage scope (FR-015a) — and is a far larger, emitter-touching change. | A codegen writer-emitter is the *correct full-coverage path* but disproportionate to a minimal two-message v1.0 slice; hand-writing two `build_*` functions (mirroring the merged `admin_messages.hpp` pattern) is the smallest surface that ships the interop proof. The full-coverage codegen path is tracked as FR-015a so this is a documented bridge, not a dead-end. |
| **Send-path MsgType-ordering change** (hoist 35 to field-3 on app sends) touches the proven 015/019 production send path | QuickFIX/Fix8 reject app messages whose MsgType is not the third field; 019's `send_impl` emits it 7th. Without this, **no live business-message interop is possible** (US2/US3 fail at the peer's parser). | Leaving the order as-is "works" only against fixpp's own lenient parser (the 019 loopback witness — a passed-for-wrong-reason artifact per [[feedback_single_threaded_harness_masks_strand_races]] class). The fix is small and contained (relocate one field on emit) and additionally corrects 019's latent opaque-path defect; the alternative (per-message header pre-assembly in every builder) duplicates header logic across builders. |

## Normative References

Per `[const §VI.5]`: `[const §VII.6]` (the business-message interop clause this feature discharges), `[const §VIII.5]` (allocation discipline on build/read), `[const §IX.1/§IX.2]` (coverage + sanitizers on the send-path change + interop), `[const §XV.13]` (codegen/runtime hybrid — read flyweights consumed, builder hand-written minimal), `[arch §4.2/§4.4]` (`fixpp::v44` generated typed messages; `session/` message-body builders), `[FIX44 §Single General Order Handling]` (NewOrderSingle 35=D / ExecutionReport 35=8 field semantics; A-001/A-006), `[FIX-SL §header]` (header field-order rule: BeginString/BodyLength/MsgType first three — D1), `[FIX50SP2] Business Rejects` (A-014 `BusinessMessageReject(35=j)` reuse from 019). Interop roadmap FR-027/SC-008 (G2).

## Gate A

- _Pending — runs after this plan, before `/speckit-tasks` (per [const §XVII.1])._
