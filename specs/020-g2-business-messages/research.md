# Phase 0 Research: G2 Business-Message Interop

Resolves the unknowns behind the typed NewOrderSingle/ExecutionReport surface and the live interop drive. Each decision is grounded in the merged codebase (003 codegen, 019 callbacks, 016/018 harness) and the FIX 4.4 dictionary.

---

## D1 — MsgType wire position on app sends (THE headline interop risk)

**Decision**: The send path MUST emit **MsgType(35) as the third wire field** (immediately after `9=BodyLength`) for application messages. Implement by hoisting the leading `35=…` field of the `Engine::send` app payload into header position; the remaining body follows the engine-stamped `49/56/34/52`.

**Rationale**: `Session::send_impl` (`src/session/session.cpp` ~2763–2810) writes `8=`, `9=`, `34=`, `49=`, `52=`, `56=`, then appends the caller payload whose first field is `35=…`. On the wire MsgType therefore lands **7th**. The FIX session protocol requires the first three fields to be `BeginString(8)`, `BodyLength(9)`, `MsgType(35)` in that order; QuickFIX-cpp and QuickFIX-J (and Fix8) **reject** a message with MsgType out of position. fixpp's own parser is lenient about header field order, which is exactly why the 019 opaque loopback witness (`test_019_g2_enablement_witness.cpp`) passed — a passed-for-wrong-reason artifact of a single-engine harness ([[feedback_single_threaded_harness_masks_strand_races]] class). The admin builders (`build_logon` etc.) already place 35 correctly (they assemble the full frame), which is why 018's live admin interop succeeded; only the app-payload path is affected.

**Scope/blast radius**: a small, contained change on the emit path. It also corrects 019's latent opaque-path ordering (any future opaque app send becomes interop-correct). No `Engine::send` signature change. The 019 witness still passes (it asserts the *value* of MsgType at `fromApp`, not its wire offset).

**Validation**: empirically gated — the first US2 live cell (fixpp-initiator × QFJ-acceptor) immediately reveals acceptance/rejection. A unit regression in `test_business_messages_roundtrip.cpp` asserts MsgType is the third field on the produced wire bytes (RED before the fix).

**Alternatives considered**: (a) leave order as-is — rejected, blocks all live interop; (b) pre-assemble the full header inside each builder (builder writes 8/9/35/… itself) — rejected, duplicates header/seqnum/time logic the engine owns and breaks the `Engine::send` "payload = body" contract; (c) teach only the typed builders to lead differently — rejected, the defect is in the shared send path, not the builders.

---

## D2 — Read side: consume the generated `fixpp::v44` flyweights (no new accessors)

**Decision**: Read inbound NewOrderSingle/ExecutionReport via the **already-generated** `fixpp::v44::NewOrderSingle` / `fixpp::v44::ExecutionReport` flyweights constructed over the `wire::MessageView<Index>` delivered to `fromApp`. No hand-written accessors.

**Rationale**: the 003 codegen (merged) emits complete read flyweights with exactly the minimal fields: NOS — `cl_ord_id()`, `symbol()`, `side()`, `order_qty(mr)`, `ord_type()`, `price(mr)`, `transact_time()`; ExecRpt — `order_id()`, `exec_id()`, `exec_type()`, `ord_status()`, `symbol()`, `side()`, `leaves_qty(mr)`, `cum_qty(mr)`, `avg_px(mr)`. Each returns `expected_t<T>` (string/char/`decimal_t`). `[arch §4.2/§4.4]` mandates codegen output for typed messages — consuming it is the sanctioned path. Verified in `build/<preset>/_codegen/include/fixpp/v44/Messages.hpp`.

**Alternatives considered**: hand-write accessors (rejected — duplicates merged codegen, violates `[arch §4.2]`); use `owning_<Msg>` from `Reify.hpp` (only needed for cross-strand survival of a parsed frame; `fromApp` reads on-strand, so the flyweight over the live view suffices — `owning_` is available if a test needs to hold a copy).

---

## D3 — Write side: minimal hand-written builders (the genuine new surface)

**Decision**: Add two `noexcept` free-function builders in `include/fixpp/session/business_messages.hpp` (+ `.cpp`), mirroring `admin_messages.hpp`:
- `build_new_order_single(out, cl_ord_id, symbol, side, order_qty, price, transact_time)` — OrdType fixed to Limit (`40=2`); emits app body `35=D` + `11/55/54/38/40/44/60`.
- `build_execution_report(out, order_id, exec_id, exec_type, ord_status, symbol, side, leaves_qty, cum_qty, avg_px)` — emits app body `35=8` + `37/17/150/39/55/54/151/14/6`.

Both write into a caller `std::span<std::byte>` via `wire::Writer`, return `expected_t<std::span<std::byte>>` of the written body, and emit **only** the app body (lead `35=…`, then business fields — **no** `8/9/34/49/52/56/10`; the engine stamps those). Numeric fields are `core::Decimal`, serialized via `Decimal::format(span)`.

**Rationale**: the codegen emits **no writer** (`owning_<Msg>` is a read-only deep-copy of an already-parsed frame — no setters, no serialize-from-fields). A typed write path is the irreducible new work. The `admin_messages.hpp` pattern (span-in, `noexcept`, `expected_t`, `wire::Writer`, stack-buffer discipline) is the proven house shape for engine-side body builders. Output feeds straight into 019's `Engine::send(SessionId, payload)`.

**Alternatives considered**: extend the codegen emitter to generate writers (rejected for v1.0 — generates writers for the *entire* message set = the deferred full-coverage scope FR-015a; disproportionate, emitter-touching); hand-assemble bytes at each call site as the 019 witness does (rejected — that is precisely the untyped status quo this feature replaces; the user chose the typed library surface).

---

## D4 — Numeric field type & formatting → `core::Decimal`

**Decision**: Price, OrderQty, LeavesQty, CumQty, AvgPx are `core::Decimal` on the builder API; serialize via `Decimal::format(std::span<std::byte>)` (canonical, locale-independent, no scientific notation). On read, the generated accessors already return `decimal_t` via `decimal_t::parse(bytes, mr)`.

**Rationale**: clarification 2026-06-04 (Q3). `Decimal` gives type safety + canonical `to_chars` that both reference engines parse, directly satisfying FR-007; no new formatting code; symmetric with the generated read path (`decimal_t`).

**Alternatives considered**: `string_view` pass-through (pushes FR-007 onto the caller — weaker guarantee); `double` (locale/precision/scientific-notation drift — the exact FR-007 hazard).

---

## D5 — Order type & execution semantics (clarification-locked)

**Decision**: NewOrderSingle is **Limit-only** for v1.0 — builder fixes `OrdType=2` and always requires `Price(44)`. The round-trip's responding ExecutionReport is **fully-filled**: `ExecType=F` (150=F, FIX 4.4), `OrdStatus=2` (39=2), `LeavesQty=0`, `CumQty=OrderQty`, `AvgPx=Price`.

**Rationale**: clarifications 2026-06-04 (Q1, Q2), grounded in the FIX 4.4 dictionary sweep — NOS dict-required = ClOrdID/Symbol/Side/TransactTime/OrderQty/OrdType; `Price(44)` is `required='N'` (conditionally required: Limit needs it). Limit-only keeps Price always-present (matches the minimal field list, interop-safe). Fully-filled exercises every minimal numeric field (CumQty/AvgPx non-zero) with one reply per order. Note: FIX 4.4 ExecType "Filled" is `F` (the 019 witness's 4.2-style `2` would be rejected by a 4.4 peer — covered by D1's "test against real engines" discipline).

**Alternatives considered**: include Market orders (conditional Price validation — deferred FR-015a); New-ack-only (CumQty/AvgPx stay 0 — weaker coverage); New-then-Filled (doubles asserted frames — beyond minimal).

---

## D6 — Malformed inbound → reuse 019 `BusinessMessageReject(35=j)`

**Decision**: An inbound `35=D`/`35=8` that fails typed-field validation drives the existing 019 `BusinessMessageReject(35=j)` path (catalogue A-014) via the `fromApp` reject return value; the session survives.

**Rationale**: 019 already ships the `35=j` stack-buffer builder and the `fromApp`-rejection→`35=j` wiring. G2 reuses it — no new reject surface. FR-009/SC-005.

**Alternatives considered**: a new business-reject path (rejected — 019 owns it).

---

## D7 — Live harness: extend 016/018, add responding counterparty Applications

**Decision**: Reuse the 016/018 live harness (engine-log seam goldens, both-role orchestration, `one_way_ca` TLS, skip-without-counterparty). Add: (a) a responding `Application` on the QuickFIX-J and QuickFIX-cpp counterparties that emits one ExecutionReport per inbound NewOrderSingle; (b) business-message cells (fixpp init/acc × QFJ/QFcpp); (c) business-message goldens normalizing non-deterministic fields (`52=` SendingTime, `60=` TransactTime, `34=` seqnum, and the ID fields `11/37/17`).

**Rationale**: 018 proved the both-role live orchestration + seam capture for admin messages; G2 is the app-message analogue. The counterparty `Application` is the only genuinely new harness logic. Goldens follow the 016 P4 normalization discipline.

**Alternatives considered**: byte-exact MITM wire capture (rejected at 016 — engine-log seam decision (b) stands); new harness from scratch (rejected — reuse).

---

## D8 — FIX version & dictionary for the live cells → FIX 4.4

**Decision**: Live business-message cells negotiate **FIX 4.4** (matching 016/018), using the merged `dictionaries/FIX44.xml` + generated `fixpp::v44`. fixpp builder emits `35=D`/`35=8` bodies; the counterparty runs a FIX 4.4 data dictionary.

**Rationale**: 016/018 live cells already run FIX 4.4 over `one_way_ca`; the generated read flyweights are `fixpp::v44`. The 019 witness's `FIX.4.2` begin-string was an in-process loopback choice, not a live-interop constraint. All-version coverage (4.2/5.0SP2/FIXT.1.1) is the deferred FR-015b obligation.

**Alternatives considered**: 5.0SP2/FIXT (deferred FR-015b); 4.2 (incidental in unit fixtures; not the live wire).

---

## Forward obligations (tracked, NOT implemented — FR-015)

- **FR-015a — full FIX 4.4 field/group coverage** for NOS/ExecRpt (the codegen *writer-emitter* path is the natural vehicle): record in `spec/behaviors-and-limitations.md` + deferred-work registry.
- **FR-015b — all-protocol-version coverage** (FIX 4.2 / 5.0 SP2 / FIXT.1.1) of these business messages — **scheduled as a post-v1.0 obligation** (interop roadmap G4 axis); record in B&L + registry.
