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
- N-003: `BuilderCompleteness077V50sp2.N003_ApplicationMessageRequestAckReportRegistered`, `TailRouting095.InboundBeAndBwReachFromApp` (BW leg).
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
  status : ok | nested_required_group | delimiter_chain | group_among_siblings  (+ the element named)
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
inheritance applies only when the component's element children number exactly one. Children are
counted on the component's definition as written, before expansion, and a nested component
reference counts as one child. So, in a chain of component references each holding exactly one
child, the group inherits the `required` flag of the reference that directly wraps it, and every
reference above that one is an element of the group's path (the AND of the field rule). The delimiter is
the group's first emitted tag in dictionary order with components expanded, the loader's rule
(research R-1). When it is a nested group's count tag, `delim_group` holds that nested group's count
and its own first emitted tag. For every MsgType it walks, the walk sets a non-`ok` status, naming
the element, when it meets:
- a required group inside a required group's instance (`nested_required_group`);
- a delimiter chain longer than one nested group, i.e. `delim_group`'s own delimiter is again a count
  tag (`delimiter_chain`, naming the inner group);
- a required component reference whose component holds a group among siblings
  (`group_among_siblings`).

No reading is guessed, and there is no recursive readback beyond the one-instance delimiter group.
A non-`ok` status fails a tail witness, and the known positives require `ok` for 35=E and 35=CC.
Nothing about the result is written into the TU or any artifact (FR-005).

`required_members_missing(...)` on a parsed frame takes the MsgType from its tag 35 and reads the body
enumeration W4 uses (each entry attributed by extent, research R-4). Its signature has no MsgType
parameter, so no caller can pass one; and because the 35=E and 35=CC frames go through the same call
in one test (`KnownPositive_DeficientFrames`), a helper that used any fixed MsgType would return a
wrong list for one of them. It returns an **ordered sequence** of paths (a `std::vector`, never a
set), duplicates kept. The order is leg-major: the legs in the order of the list below; within a
leg, instances in wire order, and within one instance or at message level, the walk's dictionary
order with components expanded. Duplicates are kept so a path reported by two legs shows twice: a
set would collapse it, and Mutant P could not fire. Each item of the sequence is:
- each `fields` tag absent at message level;
- each required group absent from the frame;
- each group whose count value is below 2;
- each group whose instance count (`group_slices(count_tag).size()`) differs from its count value;
- per instance, its `delim_tag` absent from that instance (the **delimiter leg**);
- per instance, each `fields` tag other than `delim_tag` absent from that instance (the **field
  leg**);
- per instance, for a `delim_group` whose nested count tag is present in that instance (the
  **`delim_group` leg**): the nested count's value not equal to 1, or the nested delimiter absent.
  When the nested count tag is absent from the instance, this leg reports nothing, neither path:
  the nested count tag is the outer group's `delim_tag`, so the delimiter leg reports it.

Each missing path is reported **once**, by exactly one leg:
- a `delim_tag` that is also a required instance field (E's `NoOrders`(73), whose delimiter `ClOrdID`(11)
  is required) is reported by the delimiter leg, and the field leg skips it (frame E-nodelim,
  Mutant P);
- a `delim_tag` that is a nested count tag (CC's `NoAsgnReqs`(1499) → `NoPartyIDs`(453)) is reported
  by the delimiter leg when absent, and by the `delim_group` leg's first disjunct when present with a
  value not 1;
- the nested delimiter (CC's `PartyID`(448)) is reported only by the `delim_group` leg's second
  disjunct, and only when the nested count tag is present.

So each leg, each disjunct of the `delim_group` leg and the once-only rule has a frame on which it
alone fires (contracts W6: E-nodelim, CC-nodelim, CC-count2, CC-nested). Each item names its path
(`<tag>`, `<count_tag>`, `<count_tag>[<k>].<tag>`), so every known-positive assertion, and Mutant
P's, compares the returned sequence exactly, in that order and with its duplicates.

The deficient frames' expected lists are dictionary facts. Re-derive them from the message and the
components it references: `sed -n "/<message name='<Name>'/,/<\/message>/p" dictionaries/FIX50SP2.xml`,
then `sed -n "/<component name='<Comp>'>/,/<\/component>/p" dictionaries/FIX50SP2.xml` for each
component on the path (`NewOrderList` → `ListOrdGrp`; `StreamAssignmentRequest` → `StrmAsgnReqGrp`
→ `Parties`), applying R-1's rules by hand and ordering the paths by the sequence rule above. They
are written from that reading, never pasted from the walk's output.

The field leg's known positive reads `tools/quickfix_required_golden/golden.csv` (rows
`FIX50SP2,<MsgType>,"<tags>"`) through a compile definition on the target, for the 23 tail
MsgTypes and 35=E. The file's header records its scope (body-only: the message-level
component-AND set, QuickFIX's `DataDictionary::isRequiredField`), the QuickFIX version it was
measured against and the SHA-1 of each dictionary. Its generator, `tools/quickfix_required_golden/main.cpp`,
measures it from a locally built QuickFIX, and `tools/quickfix_required_golden/regen_and_diff.py`
re-measures it (target `quickfix_required_golden_regen_diff`, defined only with
`FIXPP_BUILD_QUICKFIX_GOLDEN=ON`). It is derived neither from this walk nor from fixpp's loader.
The group and per-instance legs have no measured positive (spec FR-004 states why).

## C. Chosen values and the intent list (per witness)

Each witness holds one intent list of `(path, literal)` entries, chosen before the build (077/069
convention; never captured). Paths are `<tag>` at message level and `<count_tag>[<k>].<tag>` inside
instance k; each count field is an entry with the instance count as its literal. A delimiter's
nested group takes paths in the outer instance: `1499[<k>].453` with literal 1, then
`1499[<k>].448`. Each value is written twice in the TU, as the same literal: once in its intent
entry and once in its `Args` setter. The two are separate statements and neither reads the other
(no setter reads the intent list, and no intent entry is derived from the Args), so a mutant can
delete either statement, or both, and the TU still compiles with no unused name left behind
(contracts W6, Mutants B, D, E and H). W4 compares the list with the parsed body in both directions.

A witness sets no repeating group outside its closure-required set, except a delimiter's nested
group (FR-002): W4 attributes entries only to the instances of required groups (contracts W4).

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
  `"20261024"`;
- inside group instance k, an int without an enum takes its tag number plus k. A NumInGroup count
  is not covered by this rule: it keeps the instance count (a delimiter's nested count stays 1).

Every instance of a required group differs from the others in at least one literal, so an
instance swap or a misattribution between instances fails W4. Where every entry attributed to the
instance (its `<count_tag>[<k>].*` paths, a delimiter's nested group included) is a NumInGroup
count or an enum, char, bool, decimal or timestamp field, the witness also sets one string or int member
of that group (an optional one is allowed) under the rules above. A required group with no string
or int member stops the implementer (tasks.md Execution rules); W4's instance-order check would not
cover it.

Every intent entry is read back (W4); every closure-required member is covered by §B's check (W5).

## D. Live-cell record

Per cell (`HP-QF{cpp,j}-init-fixt11-fix50sp2-app-bn`); the registration fields are in
the live-cell contract's clause L0:
- session: FIXT.1.1, DefaultApplVerID 9 (FIX.5.0SP2), fixpp initiator, TLS as in 033;
- fixpp hello: `dictionary_digest` = SHA-256 of the `FIX50SP2.xml` bytes the shared fixture loaded
  and registered under ApplVerID 9 (L1);
- fixpp `sent` record: step F-01, MsgType BN, MsgSeqNum captured in `toApp` (the pairing key, and
  the only value captured), the chosen fields. Their values are step F-01's literals in
  `tests/interop/conversation/fixt_app_script.yaml`, written before the build and read from there,
  never captured from the outgoing frame or in `toApp`. They follow §C's literal rules (an enum
  field takes its first declared value) and are independent of the A-018 witness's intent list;
  only the field set is research R-7's;
- peer readback: the counterparty's generic field enumeration of the received BN;
- comparison: 089's `compare_streams` with the decimal resolver loaded from `FIX50SP2.xml`;
- template row in `tests/interop/cell_results.yaml` (the fields of research R-11), schema-checked. Its
  status stays the pre-live `skip:counterparty-unavailable` on the PR and after it (R-11 has the
  precedent); the pass is recorded in the verify record and in the Gate B close-out record;
- peer identity: the hello's `image_digest` is whatever `INTEROP_COUNTERPARTY_IMAGE_DIGEST` gives
  `build_conversation_meta`; `interop-live.yml` does not set it, so in CI the record carries the
  sentinel `local-build:unpinned`. The image digest the post-merge `interop-live` run on `main`
  pulled is read from its job log and recorded at close-out; the local pre-merge run's peer builds
  are recorded in the verify record (research R-10). Setting the env in the lane is out of scope.

## E. Catalogue edits (US4)

For each row in §A: Status `done`; `/specify` `095-tail-evidence-pass`; PR the 095 PR, written
`#<pr>` (C-PRCELL, tasks T104); Tests the witness IDs of §A (plus, for N-002 and N-003, the pin and
the routing cell; for A-018, the two live-cell IDs); Verified the text below. Only those five cells
of the 12 rows, the version cell of four and the Spec ref cell of four change. A-024
(`dropped(duplicate: same MsgType as A-018)`, version `5.0SP1–5.0SP2`) stays byte-unchanged on
purpose: it is not a tail row (spec FR-018).

**Tests cells**, in this form (each ID in backticks, `, ` between IDs of one file, `; ` between
files):
- every row: `` `tests/session/test_095_tail_rows_roundtrip.cpp`: `` followed by the row's witness
  IDs of §A, in §A's order;
- N-002 and N-003, then: `` ; `tests/codegen/test_077_v50sp2_builder_completeness.cpp`: `` and the
  row's pin of §A, then `` ; `<routing TU>`: `TailRouting095.InboundBeAndBwReachFromApp` (BE leg) ``
  for N-002 or `(BW leg)` for N-003, where `<routing TU>` is the path tasks T042 reports;
- A-018, then: `` ; live: `HP-QFcpp-init-fixt11-fix50sp2-app-bn`, `HP-QFj-init-fixt11-fix50sp2-app-bn` ``.

**Verified cells**, written byte for byte (T062, T063). No sentence contains `|`, so quickstart §7's
`col` still splits each row into its 13 fields. Every row's cell opens with sentence S0:

- **S0** (all 12 rows): `offline bar (owner 2026-10-21): typed builder → wire → independent dict-driven parse → bidirectional per-field + per-group-instance readback; every closure-required field and group set, checked against a runtime walk of FIX50SP2.xml.`

Then, after S0 and a space:
- **A-018:** `Generated name: ExecutionAck (catalogue title: ExecutionAcknowledgement). Live: one 35=BN built by fixpp::v50sp2::build_ExecutionAck and sent by fixpp on FIXT.1.1 with DefaultApplVerID 9 and fixpp inbound validation off (L-041-2), read back field by field by QuickFIX-cpp v1.16.0 (HP-QFcpp-init-fixt11-fix50sp2-app-bn) and QuickFIX/J 3.0.1 (HP-QFj-init-fixt11-fix50sp2-app-bn).`
- **N-002:** `Registry pin BuilderCompleteness077V50sp2.N002_UserRequestUserResponseRegistered (BE, BF). An inbound BE reaches Application::fromApp, not fromAdmin, and draws no Reject (TailRouting095.InboundBeAndBwReachFromApp, BE leg). Not witnessed: the resend path's replay of these MsgTypes as application messages rather than gap-filling them.`
- **N-003:** `Registry pin BuilderCompleteness077V50sp2.N003_ApplicationMessageRequestAckReportRegistered (BW, BX, BY). An inbound BW reaches Application::fromApp, not fromAdmin, and draws no Reject (TailRouting095.InboundBeAndBwReachFromApp, BW leg). Not witnessed: the resend path's replay of these MsgTypes as application messages rather than gap-filling them.`
- every other row: nothing more, apart from the version sentence below for A-029 and A-030.

- Version column → `5.0SP2` for A-018, A-029, A-030 and N-002 (T063). Each of their Verified cells
  ends with its version sentence, after a space:
  - A-018, A-029, A-030: `FIX 5.0SP1: no typed tier; runtime-XML path only. The Spec ref's token names a vendored dictionary that defines the MsgType (constitution Article VI §2), not this row's version.`
  - N-002: `FIX 5.0 and 5.0SP1: no typed tier; runtime-XML path only. FIX 4.4 BE/BF: present in FIX44.xml, excluded from v44 codegen by kN002N003Excluded; lifting it is fixpp#625. The Spec ref's token names a vendored dictionary that defines the MsgType (constitution Article VI §2), not this row's version.`
  - N-002's sentence states the v44 exclusion as it stands at 095. A catalogue cell is a dated
    evidence record, not a standing rule, so it may name the symbol that §F.5's clause leaves out;
    the feature that lifts the exclusion (fixpp#625) rewrites the sentence, as a later feature
    rewrites any Verified cell whose truth it changes.
- Spec ref cells are unchanged except four (FR-018; spec Clarifications, Gate A round 3 and the
  post-exhaustion pass). These edits come after §F's amendment lands.
  - A-033 → `[FIX50SP2] Market Data` and C-003 → `[FIX50SP2] Account Reporting`: the old refs name
    a category other than the one Orchestra places their MsgTypes in.
  - A-030 → `[FIX50SP1] Settlement Instruction` and N-003 → `[FIX50SP2] Application`: the old refs
    name the right category, but in a heading spelling (`Settlement Instructions`, `Application
    Sequencing`) that is not the identifier's words, which is the only spelling the amended clause
    admits (§F.1). A-030's token is unchanged: `dictionaries/FIX50SP1.xml` defines BQ.

**Re-check the 12 refs against the amended `[const §VI.2]` (§F.1)**, at P7, recording the output in
the evidence file (tasks.md Execution rules), whose entries T085 copies into the verify record. Run it from the library root as a script (`bash`); `CAT` may point at a scratch
copy. It reads each row's MsgTypes from its `Title` cell (`35=<MT>`), as the clause does, and checks
four legs: the token names a vendored dictionary (file leg) and is an application version of the
coverage index's DocAbbrev Registry (registry leg; the Registry writes them as one
`FIX40`…`FIX50SP2` row, which the `case` expands); per MsgType, the token's dictionary defines it as
`msgcat='app'` exactly once (application-message leg), and the identifier's words of its Orchestra
`category`, by the clause's own rule, equal the ref's `Category` (category leg). `words()` is that
rule as written, a space before each capital but the first, consecutive capitals included; the
follow-up lint (fixpp#626) takes the same rule from the clause. All 12 rows are single-category without a section prefix and none uses
`FIX-Latest`, so the script implements neither the clause's prefix or ` / ` rules nor its
`FIX-Latest` dictionary; a row that needed one would be reported as a mismatch, not passed.

```bash
CAT=${CAT:-spec/feature-catalogue.md}
O=dictionaries/orchestra/OrchestraFIXLatest.xml
col() {   # quickstart §7's: column $2 of the one well-formed row with ID $1
  awk -F'|' -v want="$1" -v c="$2" '{id=$2; gsub(/ /,"",id)} id==want {n++; nf=NF; v=$c}
    END {gsub(/^ +| +$/,"",v); if (n!=1 || nf!=13) exit 1; print v}' "$CAT"
}
words() { sed 's/\([A-Z]\)/ \1/g; s/^ //'; }   # the clause's rule: a space before each capital but the first
bad=0
for id in A-018 A-023 A-027 A-028 A-029 A-030 A-031 A-032 A-033 C-003 N-002 N-003; do
  ref=$(col "$id" 7) || { echo "$id: not exactly one well-formed row"; bad=1; continue; }
  tok=$(printf '%s\n' "$ref" | sed -n 's/^\[\([A-Z0-9]*\)\] .*/\1/p'); cat=${ref#*] }
  test -n "$tok" -a -f "dictionaries/$tok.xml" || { echo "$id: '$ref' names no vendored dictionary"; bad=1; continue; }
  case $tok in FIX40|FIX41|FIX42|FIX43|FIX44|FIX50|FIX50SP1|FIX50SP2) ;;   # the Registry's FIX40…FIX50SP2 row
    *) echo "$id: '$tok' is not a DocAbbrev Registry application version"; bad=1; continue ;; esac
  mts=$(col "$id" 5 | grep -o '35=[A-Za-z0-9]*' | cut -d= -f2)
  test -n "$mts" || { echo "$id: Title names no 35=<MT>"; bad=1; continue; }
  for mt in $mts; do
    test "$(grep -o "<message [^>]*msgtype='$mt'[^>]*>" "dictionaries/$tok.xml" | grep -c "msgcat='app'")" = 1 \
      || { echo "$id $mt: not one msgcat='app' message in $tok.xml"; bad=1; }
    oc=$(grep -o "<fixr:message [^>]*msgType=\"$mt\"[^>]*>" "$O" | grep -o 'category="[^"]*"' | cut -d'"' -f2)
    test -n "$oc" -a "$(printf '%s\n' "$oc" | wc -l)" = 1 || { echo "$id $mt: not one Orchestra message"; bad=1; continue; }
    test "$(printf '%s\n' "$oc" | words)" = "$cat" || { echo "$id $mt: Orchestra category $oc, ref names '$cat'"; bad=1; }
  done
done
exit $bad
```

Its RED arms, each recorded with what it printed:
- **positive control, before P7's edits:** on the unedited catalogue it must exit 1 and flag
  exactly the four rows above, and no other row;
- after P7, on a scratch copy, one perturbation per run, each of which must exit 1 with its leg's
  message: (a) one ref's token changed to a dictionary that lacks its MsgType (A-030 → `[FIX44]`,
  application-message leg); (b) one Title cell given an admin MsgType (`35=A`, application-message
  leg); (c) A-030's token changed to `[FIXT11]`, a vendored dictionary that is no Registry
  application version (registry leg); (d) A-030's token changed to `[FIX51]`, which names no
  vendored dictionary (file leg). The positive control is the category leg's arm.

After P7 it must exit 0. As a cross-check, where the owner-local FIX Repository 2010 edition has
the message, the record also shows its `FIX.5.0SP2/Base/Messages.xml` `<CategoryID>`.

The coverage index files CC, CD, CE and CQ under its own "Application Messages — Infrastructure
(FIX 4.2+)" heading. That heading is the index's grouping, not a Spec ref; 095 leaves the rows
there (FR-019 edits gap notes, plus the one comment below).

**Coverage-index edits (T066), byte for byte:**
- gap note of each tail MsgType's existing row (its last column): a cell reading `—` becomes
  `` 095 witness: `TailRows095.<RowId>_<Name>` `` (the row's witness ID of §A); a cell with text keeps
  it and gains `` ; 095 witness: `TailRows095.<RowId>_<Name>` `` at its end. No row is added or moved.
- one comment, appended after a space to the line holding the closing `-->` of 067's "ART VI §3 DISPOSITION" comment, so the index's line count is unchanged and no line citation into it moves (T085, 2026-10-25; a separate line moved 39 frozen citations):
  `<!-- SUPERSEDED (095-tail-evidence-pass, constitution v4.1): message-level Spec refs on application-message rows are governed by Article VI §2 as amended in v4.1, not by §3; the comment above is history. -->`

**B&L delta (T067, FR-013, FR-019, contracts L8), byte for byte**, as a new section at the end of
the live `spec/behaviors-and-limitations.md`:

```
## 095-tail-evidence-pass — the 12 FIX 5.0SP2 tail rows close on offline witnesses and one live FIXT cell (2026-10-24)

### Behaviors

- **B-095-1 — one live FIXT.1.1 / FIX.5.0SP2 interop cell carries a typed-built application message.** fixpp, as initiator, sends one 35=BN built by `fixpp::v50sp2::build_ExecutionAck`, and QuickFIX-cpp v1.16.0 (`HP-QFcpp-init-fixt11-fix50sp2-app-bn`) and QuickFIX/J 3.0.1 (`HP-QFj-init-fixt11-fix50sp2-app-bn`) each read it back field by field with their data dictionaries on. fixpp's inbound validation is off on that session (`SessionConfig::validate_inbound_messages`, its default) because of L-041-2: the session validator takes one dictionary. The cell witnesses that one MsgType in that one direction; every other MsgType of the rows A-018, A-023, A-027 … A-033, C-003, N-002 and N-003 closes on its offline witness only (`TailRows095.*`, `tests/session/test_095_tail_rows_roundtrip.cpp`). *(095 FR-009…FR-013; contracts L8.)*

### Limitations

- **L-095-1 — FIX 4.4 BE (UserRequest) and BF (UserResponse) have no typed builder: the `fixpp::v44` codegen excludes them, although `dictionaries/FIX44.xml` defines both as application messages.** Their typed builders are `fixpp::v50sp2`'s, on which N-002 closes. **Re-derive:** `grep -n kN002N003Excluded tools/codegen/fixpp-codegen/emit_builders.cpp`. **Status: follow-up** (fixpp#625: lift the exclusion).
```

Anything a witness exposes adds rows to this section, none of them written in advance: an exposed
production defect is an issue first (FR-020) and, if the owner rules that its row still flips, an
`L-095-<n>` row naming that issue with its status; an exposed fact that is no defect but that a user
must know is a `B-095-<n>` row. Numbering continues from the rows above.

Measured 2026-10-24 (Gate A round 3 and the post-exhaustion review): the Repository and Orchestra
agreed for every tail MsgType the Repository has, and the Repository has no CQ.

## F. Constitution amendment, v4.0 → v4.1 (FR-021)

Three clauses change, `[const §VI.2]`, `[const §VI.5]` and four sentences of `[const §XVIII.7]`
(§F.5, owner ruling 2026-10-24), and so do the Status line and a new Sync Impact Report.
`[const §VI.3]`, `[const §VI.6]` and `[const §IX.6]` are read and left as they are. The before-texts
are `.specify/constitution.md` at `95fdf4c9` (§F.1, §F.2) and at `ee39a894` (§F.5; `git diff
95fdf4c9 ee39a894 -- .specify/constitution.md` is empty). Locate each clause by its heading
(`## Article VI`, `## Article XVIII`) and its leading `2.` / `5.` / `7.`, never by line.

### F.1 Article VI §2

Before:
`2. Every OFFICIAL row's `Spec ref` uses canonical format `[DocAbbrev §X.Y.Z] Section title`. Vague refs (`§4`, "FIX spec") are a CI-linting failure.`

After:
`2. Every OFFICIAL row's `Spec ref` uses canonical format `[DocAbbrev §X.Y.Z] Section title`. A row whose `Title` cell names at least one MsgType as `35=<MT>`, where `<MT>` is the run of letters and digits that follows `35=`, may instead use the message-level form `[DocAbbrev] Category`. In that form, `DocAbbrev` is an application-specification abbreviation from the DocAbbrev Registry in `coverage-index.md` (`FIX40` … `FIX50SP2`, `FIX-Latest`) whose vendored dictionary defines, as an application message, every MsgType that the row's `Title` cell names as `35=<MT>`. That dictionary is `dictionaries/<DocAbbrev>.xml`, where an application message is a `message` with `msgcat='app'`; for `FIX-Latest` it is the vendored FIX Orchestra file `dictionaries/orchestra/OrchestraFIXLatest.xml`, where it is a `fixr:message` whose `category` is not `Session`. `Category` names the category, or categories, in which that Orchestra file (each message's `category`) places those MsgTypes, each written as its identifier's words: the identifier with a space before each capital letter but the first (`SettlementInstruction` is written `Settlement Instruction`). Each named category may be preceded by the words of its Orchestra `section`, written the same way, and ` — ` (`Pre Trade — Market Data`). When those MsgTypes lie in several categories, each of their categories is named once, in that form, joined with ` / `, and no other category is named. Every other row keeps the section form, except as §3 provides. Vague refs (`§4`, "FIX spec") do not conform.`

Design notes (not clause text):
- The form follows the catalogue's message-level rows: one version token and a category. Every
  term is decidable from committed files alone: the token from `dictionaries/`, the
  application-message test from `msgcat` (or Orchestra's `category`), and the category, its words,
  its section and the ` / ` join from the Orchestra file. The data-dictionary rows' whole-layer
  refs (`[FIX42] FIX 4.2 application specification` and the like) fit neither form, as under v4.0,
  and are among fixpp#626's findings. A whole-layer form was drafted and dropped at Gate A round 5.
  Its eligibility ('a version's whole application layer') had no decidable test, and recognising
  its string alone would admit a version-qualified 'FIX spec'.
- **One spelling per category, no heading or title alternative** (post-exhaustion review F-1). The
  candidates were read and rejected:
  - Orchestra's category `SYNOPSIS` annotations are not titles. Where a category has one, it is
    either its identifier's words or a prose description of the category. Re-derive:
    `sed -n '/<fixr:categories>/,/<\/fixr:categories>/p' dictionaries/orchestra/OrchestraFIXLatest.xml | grep -B2 'purpose="SYNOPSIS"'`.
  - The FIX Repository 2010 edition has no category title.
  - The FIX 4.4 volumes' `CATEGORY:` headings differ from the identifiers (`Security and Trading
    Session Definition/Status` where the identifier is `SecuritiesReferenceData`), and they bind
    FIX 4.4 only. The FIX 5.0–5.0SP2 volumes are not local.

  So a heading alternative would let whoever picks "the specification" decide conformance. Rows
  outside 095 that use a heading spelling (`Settlement Instructions`, `Program/List Trading`) or
  another section spelling (`Pre-Trade — …`) are fixpp#626's findings, not 095's.
- The section prefix and the ` / ` join are kept because Orchestra decides both: each category
  carries a `section` attribute, and the categories of a row's MsgTypes are a set. None of the 12
  rows uses either.
- "Application message" is `msgcat='app'`, not the catalogue's `Category` column. N-002 and N-003
  are catalogue category `session`, but BE, BF, BW, BX and BY are `msgcat='app'` in their tokens'
  dictionaries (DECISION 2: they are application messages). The row's MsgTypes are the `35=<MT>` of
  its `Title` cell, as each of the 12 rows writes them.
- `FIX-Latest` has no `dictionaries/<DocAbbrev>.xml`; its vendored dictionary is the Orchestra file,
  whose message attribute is `msgType="<MT>"`, and which has no `msgcat`. Its `Session` category
  holds the admin messages, so "not `Session`" is the application-message test there. 095 uses no
  `FIX-Latest` token; FUNCTIONAL TRACK row 3 does.
- Volume is left out. The Repository has it (`Categories.xml` `<Volume>`), but no row carries it,
  and requiring it would make every message-level row non-conforming.
- The token is tied to a dictionary that defines the messages, not to the version that introduced
  them or to the row's version column. Orchestra's `added` disagrees with A-018's, A-029's, A-030's
  and N-002's tokens, and 095 narrows four version columns without moving their tokens. The dictionary
  test is checkable with `grep` (§E's recipe).
- The clause states no reason. The reason (no numbered section to cite) is a measurement over the
  sources that were checked, so it goes in the Sync Impact Report's Rationale, scoped to those
  sources.
- The clause states no CI state. v4.0's "are a CI-linting failure" described a check that does not
  exist. A replacement such as "no CI job checks this clause yet" would turn false the day the
  check lands, and deleting it then would be a clause-body change with its own Article XX cycle
  (post-exhaustion review F-3). The CI obligation stays where it already is: `[const §IX.6]`'s
  Tier-1 "catalogue consistency check", unchanged. The dated Sync Impact Report records that the
  check is owed (fixpp#626).
- The category authority is the vendored Orchestra file, because a CI lint can read it. The
  FIX Repository 2010 edition is owner-local and not committed, so it is a cross-check (§E's recipe).
  Both are FIX Trading Community categories; on the tail MsgTypes they agreed wherever the Repository
  has the message (§E, measured 2026-10-24).
- The issue number of the owed check is kept out of the clause, because an issue number in a clause
  rots. It goes in §F.3, the spec and the plan (fixpp#626).
- Candidates outside 095 for the follow-up lint are the OFFICIAL rows whose Spec ref carries no `§`
  and no `[impl]`/`[constitution]` token. List them with:
  `awk -F'|' 'NF>=13 && $3 ~ /OFFICIAL/ && $7 !~ /§/ && $7 !~ /\[(impl|constitution)\]/ {print $2 $7}' spec/feature-catalogue.md`.
  A message-level row that is not an application-message row (D-011, a dictionary-format row) is
  among them. Which candidates fit the clause is the lint's output, not this file's.

### F.2 Article VI §5

Before:
`5. Every `/specify` artifact must include a **Normative References** section listing the exact `[DocAbbrev §X.Y.Z] Title` entries from the coverage index that inform the spec.`

After:
`5. Every `/specify` artifact must include a **Normative References** section listing the exact `[DocAbbrev §X.Y.Z] Title` entries from the coverage index that inform the spec. An application message that informs the spec may also be listed by its catalogue row's Spec ref in §2's message-level form; such an entry does not stand in for a section entry that also informs the spec.`

The first sentence is byte-identical. The added sentence is a permission, not an obligation. That
is what keeps the bump MINOR (§F.3, "Bump").

### F.3 Sync Impact Report — the first comment block, above the v3.0 → v4.0 block

PENDING form (lands with the amendment, P7):

```
<!--
Sync Impact Report — v4.0 → v4.1 (2026-10-24) — PENDING
  Not ratified. Article XX §2 sequence, in order: a scoped Codex Gate A review of this amendment's
    clause bodies (done: feature 095's scoped rounds 5 and 6 on Article VI §2/§5, converged at round
    6 with no finding, and its narrow scoped round on the Article XVIII §7 correction, converged),
    Gate B on the 095 PR (pending), owner sign-off (pending). This header reads PENDING until all three have happened. The flip to RATIFIED, with
    the rounds and the sign-off date filled in below, is the last commit before merge, not a claim
    made in advance (the v1.0 and v4.0 precedent).
  Bump: MINOR, per Article XX §4; no CHANGELOG.md entry. The test is the v4.0 report's: does any PR
    mergeable under v4.0 fail under v4.1? None does. §2 adds a form and leaves the section form valid
    for every row it was valid for. Its last sentence keeps vague refs non-conforming; "are a
    CI-linting failure" becomes "do not conform", which removes no PR obligation, since no CI job
    checked the clause under v4.0 either. §5 adds a permission and keeps its obligation word for
    word, so a /specify artifact that lists only section entries conforms under both
    versions. A §5 that REQUIRED message-level entries would make such an artifact fail. That would be
    MAJOR, and it was not adopted. The Article XVIII §7 correction replaces two deferral statements
    that feature 077 and owner DECISION 2 had already ended, and records which feature closes those
    rows, and widens its opening sentence to the A- rows the clause already records as delivered. It adds no banned pattern, tightens no budget and imposes no obligation, so no PR mergeable
    under v4.0 fails under v4.1 because of it.
    NOTE: the constitution's version is independent of the library's and of the C ABI's.
  Modified: Article VI §2 — an OFFICIAL row whose Title names at least one MsgType as `35=<MT>` may carry the
    message-level Spec ref `[DocAbbrev] Category`. The version token names a version whose vendored
    dictionary defines every MsgType of the row as an application message (`msgcat='app'`; for
    `FIX-Latest`, the vendored Orchestra file). The category is the one the vendored FIX Orchestra
    repository places them in, written as its identifier's words, optionally after its section's
    words. Every other row keeps
    `[DocAbbrev §X.Y.Z] Section title`, except as §3 provides. "Vague refs … are a CI-linting
    failure" becomes "do not conform". No CI job checks §2: that check is owed, as is Article IX §6's
    Tier-1 "catalogue consistency check" (follow-up fixpp#626).
  Modified: Article VI §5 — Normative References may also list such message-level refs. One does not
    stand in for a section entry.
  Modified: Article XVIII §7 (owner ruling 2026-10-24) — the FIX50-only rows (A-018, A-023, A-027 …
    A-033, C-003) no longer "remain deferred to future `fixpp::v50sp2`/all-version widening": their
    typed tier is `fixpp::v50sp2`, delivered by feature 077, and feature 095 closes them on it.
    N-002/N-003 are application messages (owner DECISION 2, 2026-10-21), no longer a session-FSM pair
    deferred to the v1.0-tagging gate; feature 095 closes both rows on `fixpp::v50sp2`. Feature 095
    leaves the `fixpp::v44` codegen exclusion of BE/BF unchanged (fixpp#625). Its opening scope sentence also names the further A- rows the clause records as delivered or closed; since feature 069 it had named only A-001..A-013 among the A- rows.
  Read and unchanged: Article VI §3 covers design-decision rows (`[impl]`/`[constitution]`). It does
    not reach normative message rows; the 067 coverage-index comment that read it so is history, not
    an amendment. Article VI §6 is unchanged: feature 095 meets its (a)–(d) as written. Article IX §6 is unchanged: its Tier-1 "catalogue
    consistency check" has no job, a gap recorded in the same follow-up as §2's check (fixpp#626).
  Rationale: in the sources checked, no message category is numbered. The FIX Repository 2010
    edition (Categories.xml) identifies one by CategoryID, SectionID and Volume. The FIX 4.4
    application volumes 3–5 head each "CATEGORY: …" and number no heading. The FIX 5.0–5.0SP2
    volumes were not available to check. So §2's section form had no value to cite for an
    application-message row, and the catalogue's application-message rows already carried a
    message-level ref. Feature 095's Gate A round 3 rejected a reading of §3 as covering them. Owner
    ruling 2026-10-24: amend the constitution in 095. §2's CI-lint sentence described a check that does
    not exist (`grep -n -i catalog .github/workflows/*.yml` returns nothing). Article XVIII §7 still
    described as deferred the rows feature 095 closes, so it would have contradicted the catalogue it
    governs; owner ruling 2026-10-24: fold its correction into v4.1.
  Affected catalogue rows: every OFFICIAL row whose Spec ref carries no `§` and no
    `[impl]`/`[constitution]` token, since the amended §2 decides their conformance (list them with
    the awk in specs/095-tail-evidence-pass/data-model.md §F.1). Of these, feature 095 re-checked
    only its 12 rows (A-018, A-023, A-027 … A-033, C-003, N-002, N-003), with the recipe in the same
    file's §E, and corrects four refs: A-033 and C-003 named another category than Orchestra's;
    A-030 and N-003 named Orchestra's category in a heading spelling. Which of the other rows
    conform is fixpp#626's first-run output. Article XVIII §7's correction concerns the same 12 rows,
    which feature 095 flips to done.
  Affected feature specs: specs/095-tail-evidence-pass. Merged specs/<id>/ bundles stay as written. Companion document annotated by feature 095: .specify/architecture.md (§4.2 marker, Appendix Z-10).
  Process: rides feature 095 (specs/095-tail-evidence-pass). Its PR is titled in Article XX §2 form,
    "Constitution: amend §VI.2, §VI.5, §XVIII.7 — message-level Spec refs for application-message
    rows; FIX50-only tail rows delivered (v4.1) (+ 095 tail evidence pass)", and its description states the change, the rationale and the
    affected rows and specs, so §2's four steps are met in order.
  Review and sign-off: pending.
-->
```

RATIFIED form, written in the last commit before merge. Its slots are `<pr>` (the PR number),
`<gate-b rounds>` (the Gate B record's round count and final tally) and `<sign-off date>`; the
placeholder grep of tasks.md's Execution rules is derived from this list. Each round's tally is
copied from plan `## Gate A`.
- the header's `— PENDING` becomes `— RATIFIED`;
- the first paragraph (from `  Not ratified.` to the line before `  Bump:`) becomes:

```
  Ratified. Article XX §2 sequence observed in order: a scoped Codex Gate A review of this
    amendment's clause bodies (feature 095's scoped round 5 on Article VI §2/§5, Codex P1=1 P2=1,
    Opus post-judging P1=0 P2=2 P3=1; round 6, Codex P1=0 P2=0 P3=0, converged; narrow round 7 on
    the Article XVIII §7 correction, Codex P1=0 P2=0 P3=0, converged), THEN Gate B on PR #<pr>
    (<gate-b rounds>), THEN owner sign-off <sign-off date>. This header read PENDING until all three
    had happened.
```
- `  Review and sign-off: pending.` becomes
  `  Review and sign-off: scoped Codex Gate A rounds 5–7 (above); Gate B on PR #<pr>, <gate-b rounds>; owner sign-off <sign-off date>.`

Only these record lines change at the flip. A change to any of the three amended clause texts (§F.1,
§F.2, §F.5) after its scoped Gate A round converges needs another scoped round (FR-021).

### F.4 Status line (`> **Status:**`)

PENDING form — the line's current opening `> **Status:** ` is replaced by:

`> **Status:** **v4.1 — PENDING** (2026-10-24) — backwards-compatible (Article XX §4 minor, no `CHANGELOG.md` entry); not ratified: scoped Codex Gate A on the amendment converged (feature 095: round 6 on Article VI §2/§5, then a narrow round on Article XVIII §7); pending Gate B and the owner's Article XX §2 sign-off. Article VI §2 admits a message-level Spec ref, `[DocAbbrev] Category`, for a row whose `Title` names its application MsgTypes as `35=<MT>`; Article VI §5 lets Normative References list such refs; Article XVIII §7 records the FIX50-only tail rows and N-002/N-003 as closed by feature 095 on the `fixpp::v50sp2` tier. Rides feature 095; its PR is titled in Article XX §2 form. Prior: `

The rest of the line, from `user-ratified **v4.0** (2026-10-23) — **backwards-incompatible**`
onward, is unchanged.

RATIFIED form: `**v4.1 — PENDING** (2026-10-24)` becomes `user-ratified **v4.1** (<sign-off date>)`,
and `not ratified: scoped Codex Gate A on the amendment converged (feature 095: round 6 on Article VI §2/§5, then a narrow round on Article XVIII §7); pending Gate B and the owner's Article XX §2 sign-off`
becomes `scoped Codex Gate A on feature 095 converged (round 6 on Article VI §2/§5, then narrow round 7 on Article XVIII §7), then Gate B and owner sign-off <sign-off date>; amendment PR #<pr>` (slots as in §F.3).

### F.5 Article XVIII §7 — four sentences (owner ruling 2026-10-24)

The owner folded this correction into v4.1. Only the four sentences below change; the rest of the
clause, which is one line, stays byte-identical. Locate it by `## Article XVIII` and its leading
`7.`; `grep -F` each before-text and require exactly one match. Narrow scoped Codex Gate A round 7
reviewed the first two pairs and converged with no finding (plan `## Gate A`); the last two pairs come from Gate B round 1 and go through T090's narrow round.

Before (the end of the sentence that lists the FIX50-only rows; its bold opening, "The FIX50-only
rows … are NOT delivered by 069", is unchanged):
`they carry no FIX44 message and remain deferred to future `fixpp::v50sp2`/all-version widening.`

After:
`they carry no FIX44 message. Their typed tier is `fixpp::v50sp2`, delivered by feature 077 below; feature 095 closes the catalogue rows A-018, A-023, A-027, A-028, A-029, A-030, A-031, A-032, A-033 and C-003 on it.`

Before:
`The N-002/N-003 session-FSM pair (BE UserRequest / BF UserResponse) remains deferred to the separate v1.0-tagging gate.`

After:
`N-002 (BE UserRequest / BF UserResponse) and N-003 (BW / BX / BY) are application messages, not a session-FSM pair (owner DECISION 2, 2026-10-21); feature 095 closes both rows on the `fixpp::v50sp2` tier. Feature 095 leaves the `fixpp::v44` codegen exclusion of BE/BF unchanged.`

Before (the scope sentence that opens the clause):
`v1.0's typed-message scope under `fixpp::v42`, `fixpp::v44`, `fixpp::v50sp2` is A-001..A-013 plus the M-/P-/C-/R-/N- families per the catalogue.`

After:
`v1.0's typed-message scope under `fixpp::v42`, `fixpp::v44`, `fixpp::v50sp2` is A-001..A-013, the M-/P-/C-/R-/N- families, and the further A- rows this clause records as delivered or closed below, per the catalogue.`

Before (inside the bold `fixpp::v44` sentence, whose other words are unchanged; the N-002/N-003 sentence assigns BE/BF to N-002 alone):
`85 app minus the N-002/N-003 pair BE/BF`

After:
`85 app minus N-002's BE/BF`

Design notes (not clause text):
- "Their typed tier" covers every MsgType the sentence lists, the mixed-row siblings BO, BR and BL
  included (each is `msgcat='app'` in `dictionaries/FIX50SP2.xml`). "Closes" names only the tail
  rows: A-022, A-026 and C-002, the siblings' rows, are not 095's. Re-derive the siblings' class with
  `grep -o "<message [^>]*msgtype='<MT>'[^>]*>" dictionaries/FIX50SP2.xml`.
- The first two after-texts are history tied to a feature (077 delivered, 095 closes, 095 leaves the v44
  exclusion unchanged), not a state of the tree, so none turns false when the exclusion is lifted.
  Per §F.1's note, no issue number sits in the clause (orchestrator ruling 2026-10-24); the lifting
  is tracked as fixpp#625 in the SIR's Modified entry.
- The v44 exclusion's code is `kN002N003Excluded` in `tools/codegen/fixpp-codegen/emit_builders.cpp`
  (re-derive what it holds and where it applies: `grep -n kN002N003Excluded tools/codegen/fixpp-codegen/emit_builders.cpp`).
  The clause names its effect on BE/BF, the N-002 MsgTypes `dictionaries/FIX44.xml` defines
  (`grep -o "<message [^>]*msgtype='B[EF]'[^>]*>" dictionaries/FIX44.xml`), and not the symbol, which
  can be renamed.
- No count is written: the rows are named, so nothing is derived.
- The opening sentence names the further A- rows by pointer to the clause's own records, not by list or count, so it stays true when a later feature closes another row.
- Classification: MINOR (§F.3 "Bump"). The two statements described a deferral that feature 077
  and DECISION 2 had already ended; replacing them adds no banned pattern, tightens no budget and
  imposes no obligation. Widening the opening sentence to rows the clause already records as delivered imposes none either.
- Check after C-AMEND (T061): `command grep -qF 'are application messages, not a session-FSM pair'`
  on the constitution, and the `## Article XVIII` diff changes the §7 line only.
