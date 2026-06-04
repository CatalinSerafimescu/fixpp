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
| OrderQty | 38 | `const fixpp::decimal_t&` | Y | quantity |
| OrdType | 40 | (fixed `2`=Limit) | Y | builder emits Limit; not a caller parameter for v1.0 |
| Price | 44 | `const fixpp::decimal_t&` | Y | limit price (always required given Limit) |
| TransactTime | 60 | `std::string_view` | Y | UTCTimestamp (caller-pre-formatted, same contract as SendingTime; builder validates length+shape fail-closed) |

**Read**: `fixpp::v44::NewOrderSingle{view}` → `cl_ord_id()`, `symbol()`, `side()`, `order_qty(mr)`, `ord_type()`, `price(mr)`, `transact_time()` (all `expected_t<T>`; numerics are `decimal_t` via `decimal_t::parse(bytes, mr)` — caller-supplied arena `mr`).

**Validation (builder)**: every field above is required and validated fail-closed on invalid value — empty `cl_ord_id`/`symbol`, out-of-range `side` char, unformattable `order_qty`/`price` decimal, ill-formed `transact_time` UTCTimestamp, or too-small `out` — returning a typed error and emitting **no** usable frame. Numeric fields serialize via `decimal_t::format(span)` (canonical). OrdType is always `2`. (Omission is impossible — positional signature → compile error.)

---

## E2 — ExecutionReport (35=8, catalogue A-006)

An execution/acknowledgement of an order. Round-trip carries **fully-filled** semantics.

| Field | Tag | Type (builder API) | Required | Round-trip value |
|-------|-----|--------------------|----------|------------------|
| OrderID | 37 | `std::string_view` | Y | exchange order id |
| ExecID | 17 | `std::string_view` | Y | execution id |
| ExecType | 150 | `char` | Y | `F`=Trade (FIX 4.4 wire value for a fill) |
| OrdStatus | 39 | `char` | Y | `2`=Filled |
| Symbol | 55 | `std::string_view` | Y | echoes order symbol |
| Side | 54 | `char` | Y | echoes order side |
| LeavesQty | 151 | `const fixpp::decimal_t&` | Y | `0` when fully filled |
| CumQty | 14 | `const fixpp::decimal_t&` | Y | `= OrderQty` when fully filled |
| AvgPx | 6 | `const fixpp::decimal_t&` | Y | `= Price` when fully filled |

**Read**: `fixpp::v44::ExecutionReport{view}` → `order_id()`, `exec_id()`, `exec_type()`, `ord_status()`, `symbol()`, `side()`, `leaves_qty(mr)`, `cum_qty(mr)`, `avg_px(mr)` (numerics are `decimal_t` via `decimal_t::parse(bytes, mr)`).

**Validation (builder)**: all required as above; fail-closed on invalid value (empty string / out-of-range enum char / unformattable decimal / too-small buffer); numerics via `decimal_t::format(span)`. ExecType/OrdStatus are caller-supplied `char` (the responding harness Application supplies `F`/`2`).

---

## E3 — Responding counterparty Application (test/harness-side)

A test/harness-side `Application` that, on `fromApp(NewOrderSingle)`, emits one fully-filled `ExecutionReport` (ExecType=F/OrdStatus=2, LeavesQty=0, CumQty=OrderQty, AvgPx=Price, echoing Symbol/Side, fresh OrderID/ExecID). **Both kinds are test fixtures, not fixpp production entities**: (a) the reference-engine responders (QuickFIX-J + QuickFIX-cpp Applications); (b) the **fixpp-acceptor responder** for the both-role cells — a test-fixture fixpp `Application` (built on 019 + the E2 builder) that replies by calling `Engine::send` **from inside its own `fromApp`** (send-from-callback re-entrancy). That re-entrancy relies on 019's any-thread re-entrant `Engine::send` (exec-hop + keepalive + RAII send-drain) and is the path 019's single-threaded harness never exercised (L-019-3) — covered by a named loopback test (INV-7) before the live cells.

---

## Invariants

- **INV-1 (wire field order + unpadded BodyLength)**: on the emitted/stored wire frame, the first three fields are `8=BeginString`, `9=BodyLength`, `35=MsgType`, in that order (D1); the `9=BodyLength` value is **digit-only / unpadded** (no leading zeros — `.specify/2b-wire.md`), and the `10=CheckSum` is valid. The builder emits the app body leading with `35=…`; the send path places that MsgType in field-3 position and writes digit-only BodyLength (FR-004a). **Asserted on the captured `transport_send` / stored bytes** (not just the `fromApp` MsgType value, which fixpp's lenient parser masks) — named test `test_business_messages_roundtrip.cpp::SendPath_StoredFrame_Field3MsgType_UnpaddedBodyLength_ValidChecksum`, **RED before the send-path fix**.
- **INV-2 (body-only builder)**: the builder emits **no** session header tags (`8/9/34/49/52/56`) and **no** trailer (`10`); those are engine-stamped. A builder output containing any of them is a defect. Named test `test_business_messages_build.cpp::Builder_Output_ContainsNoEngineTags` (scans the produced body for `8=`/`9=`/`34=`/`49=`/`52=`/`56=`/`10=` and asserts absence).
- **INV-3 (numeric fidelity)**: every `fixpp::decimal_t` field round-trips build→wire→`decimal_t::parse` and compares by **`decimal_t` value-equality** to the originated value (FR-007/SC-002), with no locale/scientific-notation drift — and tolerant of a counterparty's trailing-zero form (`190.5` vs `190.50`).
- **INV-4 (fail-closed build atomicity)**: an invalid required field yields a typed `expected_t` error and the **returned span is absent**; the builder builds into a local stack scratch buffer and copies into the caller `out` **only on full success**, so on error `out`'s contents are unspecified and the caller MUST NOT inspect them (no partial frame). Named test `test_business_messages_build.cpp::Builder_InvalidField_NoUsableOutput`.
- **INV-5 (malformed inbound → user reject)**: when the user's `Application` reads an inbound business message via the typed accessors, obtains an `expected_t` validation error, and returns a reject from `fromApp`, the engine drives `BusinessMessageReject(35=j)` (019/A-014) and the session stays Active (FR-009/SC-005). (The engine does not itself run the v44 validator before `fromApp` — this is user-`fromApp` behavior wired by 019.)
- **INV-6 (no NEW concurrency)**: the builders are pure `noexcept` functions off any strand; sends go through 019's `Engine::send` strand/keepalive contract unchanged (no new threading surface). The send path adds opaque-payload validation (INV-8) on the existing send path, not a new concurrency surface.
- **INV-7 (re-entrant send-from-fromApp)**: the fixpp-acceptor responder calls `Engine::send` from inside `fromApp`; this re-entrant path must not deadlock or UAF. Named test `test_business_messages_roundtrip.cpp::SendFromInsideFromApp_NoDeadlockNoUAF` under a **multi-threaded `io_context`** (not single-thread loopback), RED-verified to exercise the exec-hop, BEFORE the live cells (L-019-3).
- **INV-8 (opaque-payload validation, fail-closed pre-seqnum)**: the app-send path validates the opaque `Engine::send` payload before assigning a seqnum or transmitting — exactly one leading `35=`, no empty payload, no duplicate `35=`, no embedded session header/trailer tag (`8/9/34/49/52/56/10`) — rejecting with `error::app_payload_malformed` (slot 131) and consuming no seqnum (FR-016). Named test `test_business_messages_roundtrip.cpp::OpaquePayload_Malformed_RejectedNoSeqnumConsumed`.

---

## State / lifecycle

No new FSM. The order round-trip rides the established session lifecycle: `Active → (NewOrderSingle out / in) → (ExecutionReport in / out) → Logout`. The builders are stateless; the responding Application holds only per-order id-generation state (test-side).
