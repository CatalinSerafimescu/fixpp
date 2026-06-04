# Phase 1 Data Model: G2 Business-Message Interop

Entities, minimal fields, types, validation rules, and the wire-order invariant. Read flyweights are generated (`fixpp::v44`); write builders are new (`session/business_messages.hpp`).

---

## E1 — NewOrderSingle (35=D, catalogue A-001)

A client single-order instruction. **Limit-only** for v1.0 (OrdType fixed to `2`).

| Field | Tag | Type (builder API) | Required | Notes |
|-------|-----|--------------------|----------|-------|
| ClOrdID | 11 | `std::string_view` | Y | client order id |
| Symbol | 55 | `std::string_view` | Y | instrument |
| Side | 54 | `char` | Y | `1`=Buy, `2`=Sell (enum char) |
| OrderQty | 38 | `core::Decimal` | Y | quantity |
| OrdType | 40 | (fixed `2`=Limit) | Y | builder emits Limit; not a caller parameter for v1.0 |
| Price | 44 | `core::Decimal` | Y | limit price (always required given Limit) |
| TransactTime | 60 | `std::string_view` | Y | UTCTimestamp (pre-formatted, same contract as SendingTime) |

**Read**: `fixpp::v44::NewOrderSingle{view}` → `cl_ord_id()`, `symbol()`, `side()`, `order_qty(mr)`, `ord_type()`, `price(mr)`, `transact_time()` (all `expected_t<T>`).

**Validation (builder)**: every field above is required; a build omitting any returns a typed error and emits **no** frame (fail-closed). Numeric fields serialize via `Decimal::format` (canonical). OrdType is always `2`.

---

## E2 — ExecutionReport (35=8, catalogue A-006)

An execution/acknowledgement of an order. Round-trip carries **fully-filled** semantics.

| Field | Tag | Type (builder API) | Required | Round-trip value |
|-------|-----|--------------------|----------|------------------|
| OrderID | 37 | `std::string_view` | Y | exchange order id |
| ExecID | 17 | `std::string_view` | Y | execution id |
| ExecType | 150 | `char` | Y | `F`=Filled (FIX 4.4) |
| OrdStatus | 39 | `char` | Y | `2`=Filled |
| Symbol | 55 | `std::string_view` | Y | echoes order symbol |
| Side | 54 | `char` | Y | echoes order side |
| LeavesQty | 151 | `core::Decimal` | Y | `0` when fully filled |
| CumQty | 14 | `core::Decimal` | Y | `= OrderQty` when fully filled |
| AvgPx | 6 | `core::Decimal` | Y | `= Price` when fully filled |

**Read**: `fixpp::v44::ExecutionReport{view}` → `order_id()`, `exec_id()`, `exec_type()`, `ord_status()`, `symbol()`, `side()`, `leaves_qty(mr)`, `cum_qty(mr)`, `avg_px(mr)`.

**Validation (builder)**: all required as above; fail-closed on omission; numerics via `Decimal::format`. ExecType/OrdStatus are caller-supplied `char` (the responding harness Application supplies `F`/`2`).

---

## E3 — Responding counterparty Application (test/harness-side)

A reference-engine-side `Application` (QuickFIX-J + QuickFIX-cpp) that, on `fromApp(NewOrderSingle)`, emits one fully-filled `ExecutionReport` (ExecType=F/OrdStatus=2, LeavesQty=0, CumQty=OrderQty, AvgPx=Price, echoing Symbol/Side, fresh OrderID/ExecID). Not a fixpp production entity. The fixpp-acceptor role uses the symmetric fixpp `Application` (built on 019 + the E2 builder).

---

## Invariants

- **INV-1 (wire field order)**: on the emitted wire, the first three fields are `8=BeginString`, `9=BodyLength`, `35=MsgType`, in that order (D1). The builder emits the app body leading with `35=…`; the send path places that MsgType in field-3 position. Asserted as a regression in `test_business_messages_roundtrip.cpp`.
- **INV-2 (body-only builder)**: the builder emits **no** session header tags (`8/9/34/49/52/56`) and **no** trailer (`10`); those are engine-stamped. A builder output containing any of them is a defect.
- **INV-3 (numeric fidelity)**: every `core::Decimal` field round-trips build→wire→`decimal_t::parse` to the originated value (FR-007/SC-002), with no locale/scientific-notation drift.
- **INV-4 (fail-closed build)**: a missing required field yields a typed `expected_t` error and produces zero output bytes (no partial frame).
- **INV-5 (malformed inbound)**: an inbound business message failing typed validation drives `BusinessMessageReject(35=j)` (019/A-014); the session stays Active (FR-009/SC-005).
- **INV-6 (no new concurrency)**: the builders are pure `noexcept` functions off any strand; sends go through 019's `Engine::send` strand/keepalive contract unchanged (no new threading surface).

---

## State / lifecycle

No new FSM. The order round-trip rides the established session lifecycle: `Active → (NewOrderSingle out / in) → (ExecutionReport in / out) → Logout`. The builders are stateless; the responding Application holds only per-order id-generation state (test-side).
