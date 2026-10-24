# Data model — 095 tail evidence pass

## A. Rows → MsgTypes → generated names → witnesses

The catalogue row is the authority for the MsgTypes it names. The generated name is the
`<message name=…>` in `dictionaries/FIX50SP2.xml`, re-derive:
`grep -o "<message name='[^']*' msgtype='<MT>'" dictionaries/FIX50SP2.xml`.

| Row | MsgType | Generated name (`fixpp::v50sp2::build_<Name>`) | Offline witness |
|---|---|---|---|
| A-018 | BN | ExecutionAck *(catalogue: ExecutionAcknowledgement)* | `TailRows095.A018_ExecutionAck` |
| A-023 | CA | OrderMassActionRequest | `TailRows095.A023_OrderMassActionRequest` |
| A-023 | BZ | OrderMassActionReport | `TailRows095.A023_OrderMassActionReport` |
| A-027 | BI | TradingSessionListRequest | `TailRows095.A027_TradingSessionListRequest` |
| A-027 | BJ | TradingSessionList | `TailRows095.A027_TradingSessionList` |
| A-027 | BS | TradingSessionListUpdateReport | `TailRows095.A027_TradingSessionListUpdateReport` |
| A-028 | BT | MarketDefinitionRequest | `TailRows095.A028_MarketDefinitionRequest` |
| A-028 | BU | MarketDefinition | `TailRows095.A028_MarketDefinition` |
| A-028 | BV | MarketDefinitionUpdateReport | `TailRows095.A028_MarketDefinitionUpdateReport` |
| A-029 | BK | SecurityListUpdateReport | `TailRows095.A029_SecurityListUpdateReport` |
| A-029 | BP | SecurityDefinitionUpdateReport | `TailRows095.A029_SecurityDefinitionUpdateReport` |
| A-030 | BQ | SettlementObligationReport | `TailRows095.A030_SettlementObligationReport` |
| A-031 | BM | AllocationInstructionAlert | `TailRows095.A031_AllocationInstructionAlert` |
| A-032 | CB | UserNotification | `TailRows095.A032_UserNotification` |
| A-033 | CC | StreamAssignmentRequest | `TailRows095.A033_StreamAssignmentRequest` |
| A-033 | CD | StreamAssignmentReport | `TailRows095.A033_StreamAssignmentReport` |
| A-033 | CE | StreamAssignmentReportACK | `TailRows095.A033_StreamAssignmentReportACK` |
| C-003 | CQ | AccountSummaryReport | `TailRows095.C003_AccountSummaryReport` |
| N-002 | BE | UserRequest | `TailRows095.N002_UserRequest` |
| N-002 | BF | UserResponse | `TailRows095.N002_UserResponse` |
| N-003 | BW | ApplicationMessageRequest | `TailRows095.N003_ApplicationMessageRequest` |
| N-003 | BX | ApplicationMessageRequestAck | `TailRows095.N003_ApplicationMessageRequestAck` |
| N-003 | BY | ApplicationMessageReport | `TailRows095.N003_ApplicationMessageReport` |

Additional per-row evidence:
- N-002: `BuilderCompleteness077V50sp2.N002_UserRequestUserResponseRegistered`, `TailRouting095.InboundBeAndBwReachFromApp` (BE leg).
- N-003: `BuilderCompleteness077V50sp2.N003_ApplicationSequencingRegistered`, `TailRouting095.InboundBeAndBwReachFromApp` (BW leg).
- A-018: the live cells `HP-QFcpp-init-fixt11-fix50sp2-app-bn`, `HP-QFj-init-fixt11-fix50sp2-app-bn`.
- Census: `TailRows095Census.KnownPositive_NewOrderList`.

A tail MsgType set check is part of the witness TU: a test,
`TailRows095Census.EveryTailMsgTypeHasAWitness`, compares the 23 MsgTypes of the table above (a
literal in the TU) with the MsgTypes the witnesses registered. Each witness registers its MsgType
through a fixture helper that is called at its start. A missing or extra witness turns it RED.

## B. Required-member tree (per MsgType, derived at test runtime)

```
RequiredSet(msg_type):
  fields : set<tag>                       # required message-level fields (R-1 field rule)
  groups : list<RequiredGroup>            # required groups (R-1 group rule)
RequiredGroup:
  count_tag : tag                         # the NumInGroup field
  members   : RequiredSet                 # required members of ONE instance, same rules, from the group
```

Derivation: pugixml over `FIXPP_DICT_DATA_DIR "/FIX50SP2.xml"`, components resolved by name. The
only-child inheritance applies only when the component's element children number exactly one; a
required component reference whose component holds a group among siblings fails the walk with a
message naming it (no reading is guessed). Nothing about the result is written into the TU or any
artifact (FR-005).

Checks on a parsed frame `mv`:
- each `fields` tag: `mv.get(tag).has_value()` at message level;
- each group: count field value ≥ 2, `group_slices(count_tag).size()` equals it, and each slice
  carries each `members.fields` tag; nested groups recursively.

## C. Chosen values (per witness)

Literals, chosen before the build (077/069 convention; never captured):
- strings `"<Row>_<Msg>_<field>"`, inside group instance k `"<Row>_<Msg>_<field>_i<k>"`;
- char / enum-coded char `'1'` unless the field's dictionary enum excludes it, then the first
  declared enum value (L-069-1: the builder does not check, but a literal outside the domain would
  make the peer-facing live cell fail and is avoided everywhere for uniformity);
- int: the field's own tag number; NumInGroup: the instance count (≥ 2);
- bool `true` → `Y`; decimal `"10.5"`; UTCTimestamp `"20261024-12:00:00.000"`; LocalMktDate
  `"20261024"`.

Every set field is read back and compared; every required member is covered by §B's check.

## D. Live-cell record

Per cell (`HP-QF{cpp,j}-init-fixt11-fix50sp2-app-bn`):
- session: FIXT.1.1, DefaultApplVerID 9 (FIX.5.0SP2), fixpp initiator, TLS as in 033;
- fixpp `sent` record: step F-01, MsgType BN, MsgSeqNum captured in `to_app`, the chosen fields;
- peer readback: the counterparty's generic field enumeration of the received BN;
- comparison: 089's `compare_streams` with the decimal resolver loaded from `FIX50SP2.xml`;
- outcome row in `tests/interop/cell_results.yaml` (`kind: happy`), schema-checked;
- peer identity: the counterparty build digest in the hello record; the image digest of the run.

## E. Catalogue edits (US4)

For each row in §A: Status `done`; `/specify` `095-tail-evidence-pass`; PR the 095 PR; Tests the
witness IDs of §A; Verified: "offline bar (owner 2026-10-21): typed builder → wire → independent
dict-driven parse → per-field + per-group-instance readback; every required field and group set,
checked against a runtime walk of FIX50SP2.xml" plus, for A-018, the live cells, and for N-002/N-003
the pin and routing cell. A-018/A-029/A-030 version column → `5.0SP2`, Verified adds "FIX 5.0SP1: no
typed tier; runtime-XML path only". A-018's Verified names the generated `ExecutionAck`.
