# Golden provenance — 061-slim exemplar write shape-oracle

These `*.fix` files are **body-only** FIX 4.4 application-message bodies authored
offline via QuickFIX-cpp, used as the independent (non-tautological) anchor for
the 061 exemplar builders (FR-006 / contracts §C5).

## Format

Each golden is a single line with a `> ` outbound-direction prefix followed by the
body bytes (`35=<MsgType>\x01<business fields>\x01…`), SOH shown as `\x01`. The
`> ` prefix is REQUIRED by `tests/interop/support/golden_diff.hpp::parse_golden`
(an unprefixed line is rejected at the direction check before any field compare).

The body carries NO session/framing tags (`8/9/34/49/52/56/10`) — the engine
stamps those at frame time (INV-2). Field order is the FIX44 dictionary order
QuickFIX emits; `diff_transcripts` is order-sensitive, so this order is the
builder's shape-oracle.

## Source

- **Engine**: QuickFIX-cpp (`reference-engines/quickfix-cpp`, `libquickfix.so.17`,
  v1.16.0 per `project_reference_engines_setup`).
- **Authoring**: each body is produced by serializing the exemplar seed (data-model
  §3.1) through QuickFIX's `FIX44::<Msg>` + `Message::toString`, then stripping the
  session header/trailer to body-only. D & 8 are re-derived body-only here (NOT
  taken from the parent `phase-9-harness/BM-*` full-frame transcripts).

| Golden file | MsgType | Exemplar | Seed ref |
|---|---|---|---|
| `new_order_single.fix`  | D  | NewOrderSingle  | data-model §3.1 D  |
| `execution_report.fix`  | 8  | ExecutionReport | data-model §3.1 8  |
| `order_cancel_reject.fix`| 9 | OrderCancelReject | data-model §3.1 9 |
| `new_order_list.fix`    | E  | NewOrderList    | data-model §3.1 E  |
| `allocation_report.fix` | AS | AllocationReport | data-model §3.1 AS |

Each golden's authoring command + QuickFIX version is recorded inline as a comment
line above the entry when the golden lands (T017/T018).

## `new_order_single.fix` (D) — landed T013/T018

> **CORRECTION (implement-time, 2026-07-08):** the original plan assumed D's
> refactor onto `wire::body_builder` would be byte-identical to the 020
> builder's legacy field order (`11,55,54,38,40,44,60`). Verified conflict:
> QuickFIX emits D's business fields in ASCENDING tag order
> (`11,38,40,44,54,55,60`), and `diff_transcripts` is order-sensitive
> (T009 note). SC-001 (the golden is authoritative) wins — the refactored
> `build_new_order_single` emits ascending order to match this golden; the
> legacy 020 byte order is NOT preserved (safe: the existing 020 build/read
> tests are order-insensitive, no production callers — see
> `specs/061-typed-app-messages/tasks.md` T013 CORRECTION note).

- **QuickFIX-cpp**: `libquickfix.so.17.0.0` (v1.16.0), `reference-engines/quickfix-cpp`.
- **Authoring**: offline generator (scratchpad, not checked in) built
  `FIX44::NewOrderSingle` with `ClOrdID=ORD-001`, `Side=1`(Buy),
  `TransactTime=20240101-10:00:00`, `OrdType=2`(Limit), `Symbol=MSFT`,
  `OrderQty=100`, `Price=190.5` (same seed values as the pre-existing 020
  `AS1_NOS_HappyPath_ParseBack` test, `tests/session/test_business_messages_build.cpp`).
  Compiled: `g++ -std=c++17 -I<quickfix-cpp>/include qf_new_order_single.cpp
  -lquickfix -Wl,-rpath,<quickfix-cpp>/lib`. Serialized via
  `Message::toString()`; header (`8,9,34,49,52,56`) + trailer (`10`) stripped
  to body-only, `35=D` + business fields kept in QuickFIX's EXACT emitted
  order (observed, not assumed):
  - **Root level (group-free message; no components)**: ascending tag order
    among `11,38,40,44,54,55,60` — i.e. `11(ClOrdID),38(OrderQty),
    40(OrdType),44(Price),54(Side),55(Symbol),60(TransactTime)`.
  - Full observed wire frame (SOH as `|`):
    `8=FIX.4.4|9=107|35=D|34=1|49=S|52=20240101-00:00:00|56=T|11=ORD-001|38=100|40=2|44=190.5|54=1|55=MSFT|60=20240101-10:00:00|10=121|`

## `execution_report.fix` (8) — landed T014/T018

> **CORRECTION (implement-time, 2026-07-08):** same class of correction as D
> above — QuickFIX emits 8's business fields ascending (`6,14,17,37,39,54,55,
> 150,151`), not the 020 builder's legacy order (`37,17,150,39,55,54,151,14,
> 6`). SC-001 wins; the refactored `build_execution_report` matches this
> golden's ascending order.

- **QuickFIX-cpp**: `libquickfix.so.17.0.0` (v1.16.0), `reference-engines/quickfix-cpp`.
- **Authoring**: offline generator (scratchpad, not checked in) built
  `FIX44::ExecutionReport` with `OrderID=ORD-XCH-001`, `ExecID=EXEC-001`,
  `ExecType=F`(Trade), `OrdStatus=2`(Filled), `Side=1`(Buy), `LeavesQty=0`,
  `CumQty=100`, `AvgPx=190.5`, `Symbol=MSFT` (same seed values as the
  pre-existing 020 `AS2_ExecRpt_HappyPath_ParseBack` test,
  `tests/session/test_business_messages_build.cpp`). Compiled:
  `g++ -std=c++17 -I<quickfix-cpp>/include qf_execution_report.cpp
  -lquickfix -Wl,-rpath,<quickfix-cpp>/lib`. Serialized via
  `Message::toString()`; header (`8,9,34,49,52,56`) + trailer (`10`) stripped
  to body-only, `35=8` + business fields kept in QuickFIX's EXACT emitted
  order (observed, not assumed):
  - **Root level (group-free message; no components)**: ascending tag order
    among `6,14,17,37,39,54,55,150,151` — i.e. `6(AvgPx),14(CumQty),
    17(ExecID),37(OrderID),39(OrdStatus),54(Side),55(Symbol),150(ExecType),
    151(LeavesQty)`.
  - Full observed wire frame (SOH as `|`):
    `8=FIX.4.4|9=113|35=8|34=1|49=S|52=20240101-00:00:00|56=T|6=190.5|14=100|17=EXEC-001|37=ORD-XCH-001|39=2|54=1|55=MSFT|150=F|151=0|10=010|`

## `new_order_list.fix` (E) — landed T017

- **QuickFIX-cpp**: `libquickfix.so.17.0.0` (v1.16.0), `reference-engines/quickfix-cpp`.
- **Authoring**: offline generator (scratchpad, not checked in) built
  `FIX44::NewOrderList` with `ListID=LIST1`, `BidType=3`, `TotNoOrders=2`;
  `NoOrders` group with 2 entries — order 1 (`ClOrdID=ORD1`, `ListSeqNo=1`,
  `Side=1`(Buy), `Symbol=MSFT`, `OrderQty=150.75`) carries a nested
  `NoPartyIDs`(1 entry: `PartyID=PARTY1`,`PartyIDSource=D`,`PartyRole=1`) →
  `NoPartySubIDs`(1 entry: `PartySubID=SUB1`,`PartySubIDType=1`); order 2
  (`ClOrdID=ORD2`,`ListSeqNo=2`,`Side=2`(Sell),`Symbol=IBM`,`OrderQty=50`) sets
  `NoPartyIDs=0` directly (present-but-empty, no `addGroup` call) —
  count-of-zero on the optional group (data-model §3.1 "Count-of-zero witness
  target"). Compiled: `g++ -std=c++17 -I<quickfix-cpp>/include
  qf_new_order_list.cpp -lquickfix -Wl,-rpath,<quickfix-cpp>/lib`. Serialized
  via `Message::toString()`; header (`8,9,34,49,52,56`) + trailer (`10`)
  stripped to body-only, `35=E` + business fields kept in QuickFIX's EXACT
  emitted order (observed, not assumed):
  - **Root level**: ascending tag order among `66,68,73,394` — i.e.
    `66,68,73(group+entries),394` (`394=BidType` ends up AFTER all order
    entries because the `NoOrders` group's content is emitted in place of the
    `73` count tag's ascending position).
  - **Order-entry level** (per-order fields, `NoOrders` dictionary
    `message_order`): `11,67,453(group+entries),55,54,38` — NOT tag-ascending
    (confirms the T009 golden-authoring-rules note: entries follow dictionary
    order, not ascending).
  - **Party-entry level** (`NoPartyIDs` dictionary order): `448,447,452,802`.
  - **Sub-entry level** (`NoPartySubIDs` dictionary order): `523,803`.
  - Full observed wire frame (SOH as `|`):
    `8=FIX.4.4|9=189|35=E|34=1|49=S|52=20240101-00:00:00|56=T|66=LIST1|68=2|73=2|11=ORD1|67=1|453=1|448=PARTY1|447=D|452=1|802=1|523=SUB1|803=1|55=MSFT|54=1|38=150.75|11=ORD2|67=2|453=0|55=IBM|54=2|38=50|394=3|10=203|`

## `order_cancel_reject.fix` (9) — landed T017/T018

- **QuickFIX-cpp**: `libquickfix.so.17.0.0` (v1.16.0), `reference-engines/quickfix-cpp`.
- **Authoring**: offline generator (scratchpad, not checked in) built `FIX44::OrderCancelReject`
  with `OrderID=ORDER1`, `ClOrdID=CLORD2`, `OrigClOrdID=CLORD1`, `OrdStatus='8'`(Rejected),
  `CxlRejResponseTo='1'`(OrderCancelRequest), plus optional `CxlRejReason=0` (int-type exercise).
  No groups (`9` is group-free per data-model §3.1). Compiled: `g++ -std=c++17
  -I<quickfix-cpp>/include qf_order_cancel_reject.cpp -lquickfix
  -Wl,-rpath,<quickfix-cpp>/lib`. Serialized via `Message::toString()`; header
  (`8,9,34,49,52,56`) + trailer (`10`) stripped to body-only, `35=9` + business
  fields kept in QuickFIX's EXACT emitted order (observed, not assumed):
  - **Root level (group-free message; no components)**: ascending tag order among
    `11,37,39,41,102,434` — i.e. `11(ClOrdID),37(OrderID),39(OrdStatus),
    41(OrigClOrdID),102(CxlRejReason),434(CxlRejResponseTo)`.
  - Full observed wire frame (SOH as `|`):
    `8=FIX.4.4|9=88|35=9|34=1|49=S|52=20240101-00:00:00|56=T|11=CLORD2|37=ORDER1|39=8|41=CLORD1|102=0|434=1|10=120|`

## `allocation_report.fix` (AS) — landed T017/T018

- **QuickFIX-cpp**: `libquickfix.so.17.0.0` (v1.16.0), `reference-engines/quickfix-cpp`.
- **Authoring**: offline generator (scratchpad, not checked in) built `FIX44::AllocationReport`
  with `AllocReportID=ALLOCRPT1`, `AllocTransType='0'`(New), `AllocReportType=9`(Accept),
  `AllocStatus=0`(Accepted), `AllocNoOrdersType=0` (seeded 0 per data-model §3.1 AS note, so
  QuickFIX does NOT treat `OrdAllocGrp NoOrders(73)` as conditionally required), `Side='1'`(Buy),
  `Quantity=1000`, `AvgPx=25.5`, `TradeDate=20240101`, plus `Symbol=MSFT`←Instrument. One
  top-level `Parties NoPartyIDs`(1 entry: `PartyID=PARTY1`,`PartyIDSource=D`,`PartyRole=1`) →
  `NoPartySubIDs`(1 entry: `PartySubID=SUB1`,`PartySubIDType=1`) — the 2-level nested chain
  (data-model §3.1 AS). `NoOrders(73)` NOT emitted (confirms the `AllocNoOrdersType=0` note —
  see below). Compiled: `g++ -std=c++17 -I<quickfix-cpp>/include qf_allocation_report.cpp
  -lquickfix -Wl,-rpath,<quickfix-cpp>/lib`. Serialized via `Message::toString()`; header
  (`8,9,34,49,52,56`) + trailer (`10`) stripped to body-only, `35=AS` + business fields kept in
  QuickFIX's EXACT emitted order (observed, not assumed):
  - **Root level**: ascending tag order among `6,53,54,55,71,75,87,453(group+entries),755,794,857`
    — i.e. `6(AvgPx),53(Quantity),54(Side),55(Symbol),71(AllocTransType),75(TradeDate),
    87(AllocStatus),453(NoPartyIDs group+entries),755(AllocReportID),794(AllocReportType),
    857(AllocNoOrdersType)` — `755/794/857` land AFTER the `453` group content because ascending
    tag order places them past `453` (mirrors the E golden's `394` landing after `73`'s content).
  - **Party-entry level** (`NoPartyIDs` dictionary order): `448,447,452,802`.
  - **Sub-entry level** (`NoPartySubIDs` dictionary order): `523,803`.
  - Full observed wire frame (SOH as `|`):
    `8=FIX.4.4|9=168|35=AS|34=1|49=S|52=20240101-00:00:00|56=T|6=25.5|53=1000|54=1|55=MSFT|71=0|75=20240101|87=0|453=1|448=PARTY1|447=D|452=1|802=1|523=SUB1|803=1|755=ALLOCRPT1|794=9|857=0|10=241|`
