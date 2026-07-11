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

## 069-v44-all-families T011/T012 — 8 fixed exemplar external goldens (C4)

All 8 golden bodies below share the same authoring/stripping discipline as
the 061/067 goldens above: QuickFIX-cpp `FIX44::<Msg>` + `Message::toString()`,
header (`8,9,34,49,52,56`) + trailer (`10`) stripped to body-only, `35=<Type>`
+ business fields kept in QuickFIX's EXACT emitted order (observed, not
assumed — root-level scalar fields sort ASCENDING by tag (QuickFIX `FieldMap`
is a `std::map<int,...>`, matching the generated `fixpp::v44::build_<Msg>`
emitter's own ascending-tag root order); group-entry-level fields follow the
FIX44 dictionary's declared `message_order` (NOT ascending) at every nesting
depth. `test_069_family_exemplar_golden.cpp` seeds the IDENTICAL values used
in each `golden/gen/qf_*.cpp` generator; all 8 cases matched the checked-in
golden bytes exactly on the FIRST attempt — no seed-alignment fixups and no
emitter defects were needed (case (a)/(b)/(c) triage per the phase brief: not
applicable, all 8 green as authored).

- **QuickFIX-cpp**: `libquickfix.so.17.0.0` (v1.16.0), `reference-engines/quickfix-cpp` (parent-repo path).
- **Re-run**: `g++ -std=c++17 -I<quickfix-cpp>/include gen/qf_<msg>.cpp -L<quickfix-cpp>/lib -lquickfix -Wl,-rpath,<quickfix-cpp>/lib -o /tmp/qf_gen && /tmp/qf_gen` (per `gen/README.md`).

### `069_tradecapturereport_ae.fix` (AE) — group-heavy/nested (NoSides / NoLegs)

- Required='Y' (`dictionaries/FIX44.xml:1435-1494`): TradeReportID(571),
  PreviouslyReported(570), Instrument(component — Symbol(55) seeded, no
  individually-required sub-field), LastQty(32), LastPx(31), TradeDate(75),
  TransactTime(60), TrdCapRptSideGrp/NoSides(552) (component required; entry
  requires Side(54)+OrderID(37)).
- NoLegs(555, `TrdInstrmtLegGrp`, required='N') populated with 1 entry
  (LegSymbol(600)="IBM") per C4's table note (group-heavy/nested exemplar).
  NoSides carries a nested NoPartyIDs(453) entry (PartyID="PARTY1",
  PartyIDSource='D', PartyRole=1) — the required group-in-group case.
- Seed: TradeReportID="TCR001", PreviouslyReported=false, LastQty=100,
  LastPx=50.25, TradeDate="20240101", TransactTime="20240101-00:00:00",
  Symbol="MSFT".
- **Root level**: ascending among `31,32,55,60,75,552,555,570,571` (NoSides
  and NoLegs group content lands at their own tag's ascending slot).
- **NoSides entry level**: `54,37,453(+entries)` (Side, OrderID,
  NoPartyIDs) — matches `TrdCapRptSideGrp`'s dictionary `message_order`.
- **NoPartyIDs entry level**: `448,447,452` (PartyID, PartyIDSource, PartyRole).
- **NoLegs entry level**: `600` (LegSymbol) only.
- Full observed wire frame (SOH as `|`):
  `8=FIX.4.4|9=180|35=AE|34=1|49=S|52=20240101-00:00:00|56=T|31=50.25|32=100|55=MSFT|60=20240101-00:00:00|75=20240101|552=1|54=1|37=ORDER1|453=1|448=PARTY1|447=D|452=1|555=1|600=IBM|570=N|571=TCR001|10=055|`

### `069_positionreport_ap.fix` (AP) — NoPositions group

- Required='Y' (`dictionaries/FIX44.xml:1815-1843`, `FIX44::PositionReport`
  ctor): PosMaintRptID(721), PosReqResult(728), ClearingBusinessDate(715),
  Account(1), AccountType(581), SettlPrice(730), SettlPriceType(731),
  PriorSettlPrice(734). `PositionQty`/`NoPositions`(702) is required='Y' at
  the component level but the internal group is required='N'
  (`dictionaries/FIX44.xml:2530-2538`) — populated (1 entry: PosType="TQ",
  LongQty=100) per C4's table note.
- **Root level**: ascending among `1,581,702,715,721,728,730,731,734`.
- **NoPositions entry level**: `703,704` (PosType, LongQty).
- Full observed wire frame: `8=FIX.4.4|9=134|35=AP|34=1|49=S|52=20240101-00:00:00|56=T|1=ACCT1|581=1|702=1|703=TQ|704=100|715=20240101|721=POSRPT1|728=0|730=100.5|731=1|734=99.75|10=002|`

### `069_collateralinquiry_bb.fix` (BB) — no required fields

- CollateralInquiry has NO required='Y' fields at all
  (`dictionaries/FIX44.xml:2231-2273`; confirmed via the QuickFIX header's
  default-constructible ctor with no required-args overload). Seeded a sane
  illustrative set: Account(1)="ACCT1", AccountType(581)=1,
  CollInquiryID(909)="COLLINQ1", NoCollInquiryQualifier(938, 1 entry:
  CollInquiryQualifier(896)=0).
- **Root level**: ascending among `1,581,909,938(+896)`.
- Full observed wire frame: `8=FIX.4.4|9=81|35=BB|34=1|49=S|52=20240101-00:00:00|56=T|1=ACCT1|581=1|909=COLLINQ1|938=1|896=0|10=255|`

### `069_securitylist_y.fix` (y) — NoRelatedSym group

- Required='Y' (`dictionaries/FIX44.xml:1218-1224`, `FIX44::SecurityList`
  ctor): SecurityReqID(320), SecurityResponseID(322),
  SecurityRequestResult(560). `SecListGrp`/`NoRelatedSym`(146) required='N'
  but populated (1 entry: Symbol(55)="MSFT") per C4's table note.
- **Root level**: ascending among `146(+55),320,322,560` — NoRelatedSym's
  group tag (146) sorts BEFORE the three required scalars (320/322/560).
- Full observed wire frame: `8=FIX.4.4|9=86|35=y|34=1|49=S|52=20240101-00:00:00|56=T|146=1|55=MSFT|320=SECREQ1|322=SECRESP1|560=0|10=189|`

### `069_confirmation_ak.fix` (AK) — required NoCapacities group

- Required='Y' (`dictionaries/FIX44.xml:1646-1673`, `FIX44::Confirmation`
  ctor): ConfirmID(664), ConfirmTransType(666), ConfirmType(773),
  ConfirmStatus(665), TransactTime(60), TradeDate(75), AllocQty(80),
  Side(54), AllocAccount(79), AvgPx(6), GrossTradeAmt(381), NetMoney(118).
  `CpctyConfGrp`/`NoCapacities`(862) is a REQUIRED group
  (`dictionaries/FIX44.xml:2798-2804`, group required='Y') — 1 entry,
  OrderCapacity(528)='A' + OrderCapacityQty(863)=100 (both required='Y'
  within the group).
- **Root level**: ascending among `6,54,60,75,79,80,118,381,664,665,666,773,862(+528,863)`.
- Full observed wire frame: `8=FIX.4.4|9=170|35=AK|34=1|49=S|52=20240101-00:00:00|56=T|6=50.25|54=1|60=20240101-00:00:00|75=20240101|79=ACCT1|80=100|118=5025|381=5025|664=CONF1|665=1|666=0|773=1|862=1|528=A|863=100|10=009|`

### `069_registrationinstructions_o.fix` (o) — nested NoRegistDtls

- Required='Y' (`dictionaries/FIX44.xml:1009-1021`, `FIX44::
  RegistrationInstructions` ctor): RegistID(513), RegistTransType(514),
  RegistRefID(508). `RgstDtlsGrp`/`NoRegistDtls`(473) required='N' but
  populated (1 entry: RegistDtls(509)="DETAILS1") per C4's table note.
- **Root level**: ascending among `473(+509),508,513,514`.
- Full observed wire frame: `8=FIX.4.4|9=89|35=o|34=1|49=S|52=20240101-00:00:00|56=T|473=1|509=DETAILS1|508=REGREF1|513=REGID1|514=0|10=065|`

### `069_liststatus_n.fix` (N) — required NoOrders group

- Required='Y' (`dictionaries/FIX44.xml:628-639`, `FIX44::ListStatus` ctor):
  ListID(66), ListStatusType(429), NoRpts(82), ListOrderStatus(431),
  RptSeq(83), TotNoOrders(68). `OrdListStatGrp`/`NoOrders`(73) is a REQUIRED
  group (`dictionaries/FIX44.xml:3137-3145`, group required='Y') — 1 entry,
  ClOrdID(11)+CumQty(14)+OrdStatus(39)+LeavesQty(151)+CxlQty(84), all
  required='Y' within the group.
- **Root level**: ascending among `66,68,73(+11,14,39,151,84),82,83,429,431`.
- Full observed wire frame: `8=FIX.4.4|9=113|35=N|34=1|49=S|52=20240101-00:00:00|56=T|66=LIST1|68=1|73=1|11=ORD1|14=100|39=2|151=0|84=0|82=1|83=1|429=1|431=1|10=028|`

### `069_businessmessagereject_j.fix` (j) — flat, ref-tag echo

- Required='Y' (`dictionaries/FIX44.xml:956-964`, `FIX44::
  BusinessMessageReject` ctor): RefMsgType(372), BusinessRejectReason(380).
  No groups (flat message). Seed: RefMsgType="D", BusinessRejectReason=3
  (UnsupportedMessageType).
- **Root level (group-free message)**: ascending among `372,380`.
- Full observed wire frame: `8=FIX.4.4|9=53|35=j|34=1|49=S|52=20240101-00:00:00|56=T|372=D|380=3|10=251|`
