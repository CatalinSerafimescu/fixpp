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
- Census and known positives (suite `TailRows095Census`): `EveryTailMsgTypeHasExactlyOneWitness`,
  `FieldLegMatchesQuickFixCppGolden`, `KnownPositive_DeficientFrames`,
  `NestedRequiredGroupFailsLoud_MassQuote`, `WalkStatusesOnSyntheticDictionaries`.
- Live-message selection (FR-014, verify-time only): `TailRows095Selection.DISABLED_LiveMessageAgreesOnBothAxes`
  (research R-7).

The tail-set census is part of the witness TU. `TailRows095Census.EveryTailMsgTypeHasExactlyOneWitness`
holds the table above as a literal (row → MsgTypes, the catalogue's authority, not a dictionary
result), iterates the **registered** tests of suite `TailRows095` (`::testing::UnitTest`), maps each
name's `<Name>` to a MsgType through the dictionary, and requires multiplicity 1 per MsgType, the
name's row equal to the table's row, no unmapped name and no extra MsgType (FR-004a). It collects
every finding before asserting. Registration happens at static initialisation, so the census does
not depend on another test having run.

## B. Closure-required tree (per MsgType, derived at test runtime)

```
walk(doc : pugi::xml_document const&) -> map<msg_type, WalkResult>
WalkResult(msg_type):
  status : ok | nested_required_group | group_among_siblings  (+ the element named)
  set    : ClosureRequiredSet                 # meaningful only when status == ok
ClosureRequiredSet:
  fields : set<tag>                           # required message-level fields (R-1 field rule)
  groups : list<RequiredGroup>                # required groups (R-1 group rule)
RequiredGroup:
  count_tag : tag                             # the NumInGroup field
  delim_tag : tag                             # the group's first emitted tag (may be a count tag)
  delim_group : optional<(count_tag, delim_tag)>  # set when delim_tag is a nested group's count tag
  fields    : set<tag>                        # required fields of ONE instance, same rules, from the group
```

Derivation: pugixml over a loaded document, components resolved by name. The production call loads
`FIXPP_DICT_DATA_DIR "/FIX50SP2.xml"` once in `SetUpTestSuite`; the W6 synthetic arms pass
`load_string` literals to the same entry point. One `WalkResult` per MsgType. The only-child
inheritance applies only when the component's element children number exactly one. The delimiter is
the group's first emitted tag in dictionary order with components expanded, the loader's rule
(research R-1). When it is a nested group's count tag, `delim_group` holds that nested group's count
and its own first emitted tag. For every MsgType it walks, the walk sets a non-`ok` status, naming
the element, when it meets:
- a required group inside a required group's instance (`nested_required_group`);
- a delimiter chain longer than one nested group, i.e. `delim_group`'s own delimiter is again a count
  tag (`nested_required_group`, naming the inner group);
- a required component reference whose component holds a group among siblings
  (`group_among_siblings`).

No reading is guessed, and there is no recursive readback beyond the one-instance delimiter group.
A non-`ok` status fails a tail witness, and the known positives require `ok` for 35=E and 35=CC.
Nothing about the result is written into the TU or any artifact (FR-005).

`required_members_missing(...)` on a parsed frame takes the MsgType from its tag 35 and reads the body
enumeration W4 uses (each entry attributed by extent, research R-4). It returns:
- each `fields` tag absent at message level;
- each required group absent from the frame;
- each group whose count value is below 2;
- each group whose instance count (`group_slices(count_tag).size()`) differs from its count value;
- per instance, its `delim_tag` and each `fields` tag absent from that instance;
- per instance, for a `delim_group`, the nested count tag absent or not equal to 1, or the nested
  delimiter absent.

Each item names its path (`<tag>`, `<count_tag>`, `<count_tag>[<k>].<tag>`), so a known-positive
assertion compares exact lists.

The field leg's known positive reads `tools/quickfix_required_golden/golden.csv` (rows
`FIX50SP2,<MsgType>,"<tags>"`) through a compile definition on the target, for the 23 tail
MsgTypes and 35=E.

## C. Chosen values and the intent list (per witness)

Each witness holds one intent list of `(path, literal)` entries, chosen before the build (077/069
convention; never captured). Paths are `<tag>` at message level and `<count_tag>[<k>].<tag>` inside
instance k; each count field is an entry with the instance count as its literal. A delimiter's
nested group takes paths in the outer instance: `1499[<k>].453` with literal 1, then
`1499[<k>].448`. The Args are
populated from the same literals, and W4 compares the list with the parsed body in both directions.

Literals:
- strings `"<Row>_<Msg>_<field>"`, inside group instance k `"<Row>_<Msg>_<field>_i<k>"`;
- any field with declared enum values, whatever its type (char, int, String, Boolean, …): its first
  declared value in the dictionary. This rule takes precedence over every line below (L-069-1: the
  builder does not check, but a literal outside the domain would make the peer-facing live cell fail
  and is avoided everywhere for uniformity);
- char without an enum: `'1'`;
- int without an enum: the field's own tag number; NumInGroup: the instance count (≥ 2; 1 for a
  delimiter's nested group);
- bool `true` → `Y`; decimal `"10.5"`; UTCTimestamp `"20261024-12:00:00.000"`; LocalMktDate
  `"20261024"`.

Every intent entry is read back (W4); every closure-required member is covered by §B's check (W5).

## D. Live-cell record

Per cell (`HP-QF{cpp,j}-init-fixt11-fix50sp2-app-bn`); the registration fields are in
the live-cell contract's clause L0:
- session: FIXT.1.1, DefaultApplVerID 9 (FIX.5.0SP2), fixpp initiator, TLS as in 033;
- fixpp hello: `dictionary_digest` = SHA-256 of the `FIX50SP2.xml` bytes the shared fixture loaded
  and registered under ApplVerID 9 (L1);
- fixpp `sent` record: step F-01, MsgType BN, MsgSeqNum captured in `to_app`, the chosen fields;
- peer readback: the counterparty's generic field enumeration of the received BN;
- comparison: 089's `compare_streams` with the decimal resolver loaded from `FIX50SP2.xml`;
- outcome row in `tests/interop/cell_results.yaml` (the fields of research R-11), schema-checked;
- peer identity: the hello's `image_digest` is whatever `INTEROP_COUNTERPARTY_IMAGE_DIGEST` gives
  `build_conversation_meta`; `interop-live.yml` does not set it, so in CI the record carries the
  sentinel `local-build:unpinned`. The image digest the post-merge `interop-live` run on `main`
  pulled is read from its job log and recorded at close-out; the local pre-merge run's peer builds
  are recorded in the verify record (research R-10). Setting the env in the lane is out of scope.

## E. Catalogue edits (US4)

For each row in §A: Status `done`; `/specify` `095-tail-evidence-pass`; PR the 095 PR; Tests the
witness IDs of §A; Verified: "offline bar (owner 2026-10-21): typed builder → wire → independent
dict-driven parse → bidirectional per-field + per-group-instance readback; every closure-required
field and group set, checked against a runtime walk of FIX50SP2.xml" plus, for A-018, the live cells,
and for N-002/N-003 the pin and routing cell.
- Version column → `5.0SP2` for A-018, A-029, A-030 (Verified adds "FIX 5.0SP1: no typed tier;
  runtime-XML path only") and N-002 (Verified adds "FIX 5.0 and 5.0SP1: no typed tier; runtime-XML
  path only. FIX 4.4 BE/BF: present in FIX44.xml, excluded from v44 codegen by `kN002N003Excluded`;
  lifting it is fixpp#625").
- Spec ref cells are unchanged (spec Clarifications, Gate A round 1).
- A-018's Verified names the generated `ExecutionAck`.
