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
- per instance, its `delim_tag` absent from that instance (the **delimiter leg**);
- per instance, each `fields` tag other than `delim_tag` absent from that instance (the **field
  leg**);
- per instance, for a `delim_group` whose nested count tag is present in that instance (the
  **`delim_group` leg**): the nested count's value not equal to 1, or the nested delimiter absent.
  When the nested count tag is absent from the instance, this leg reports nothing, neither path:
  the nested count tag is the outer group's `delim_tag`, so the delimiter leg reports it.

Each missing path is reported **once**, by exactly one leg:
- a `delim_tag` that is also a required instance field (E's `NoOrders`(73), whose delimiter `ClOrdID`(11)
  is required) is reported by the delimiter leg, and the field leg skips it;
- a `delim_tag` that is a nested count tag (CC's `NoAsgnReqs`(1499) → `NoPartyIDs`(453)) is reported
  by the delimiter leg when absent, and by the `delim_group` leg's first disjunct when present with a
  value not 1;
- the nested delimiter (CC's `PartyID`(448)) is reported only by the `delim_group` leg's second
  disjunct, and only when the nested count tag is present.

So each leg, and each disjunct of the `delim_group` leg, has a frame on which it alone fires
(contracts W6: CC-nodelim, CC-count2, CC-nested). Each item names its path (`<tag>`,
`<count_tag>`, `<count_tag>[<k>].<tag>`), so a known-positive assertion compares exact lists.

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
- Spec ref cells are unchanged except four (FR-018; spec Clarifications, Gate A round 3 and the
  post-exhaustion pass). These edits come after §F's amendment lands.
  - A-033 → `[FIX50SP2] Market Data` and C-003 → `[FIX50SP2] Account Reporting`: the old refs name
    a category other than the one Orchestra places their MsgTypes in.
  - A-030 → `[FIX50SP1] Settlement Instruction` and N-003 → `[FIX50SP2] Application`: the old refs
    name the right category, but in a heading spelling (`Settlement Instructions`, `Application
    Sequencing`) that is not the identifier's words, which is the only spelling the amended clause
    admits (§F.1). A-030's token is unchanged: `dictionaries/FIX50SP1.xml` defines BQ.
- A-018's Verified names the generated `ExecutionAck`.

**Re-check the 12 refs against the amended `[const §VI.2]` (§F.1)**, at P7, recording the output in
the verify record. Run it from the library root as a script (`bash`); `CAT` may point at a scratch
copy. It reads each row's MsgTypes from its `Title` cell (`35=<MT>`), as the clause does, and checks
three legs per MsgType: the token's dictionary defines it as `msgcat='app'` exactly once (token and
application-message legs), and the identifier's words of its Orchestra `category` equal the ref's
`Category` (category leg). All 12 rows are single-category without a section prefix and none uses
`FIX-Latest`, so the script implements neither the clause's prefix or ` / ` rules nor its
`FIX-Latest` dictionary; a row that needed one would be reported as a mismatch, not passed.

```bash
CAT=${CAT:-spec/feature-catalogue.md}
O=dictionaries/orchestra/OrchestraFIXLatest.xml
col() {   # quickstart §7's: column $2 of the one well-formed row with ID $1
  awk -F'|' -v want="$1" -v c="$2" '{id=$2; gsub(/ /,"",id)} id==want {n++; nf=NF; v=$c}
    END {gsub(/^ +| +$/,"",v); if (n!=1 || nf!=13) exit 1; print v}' "$CAT"
}
words() { sed 's/\([a-z]\)\([A-Z]\)/\1 \2/g'; }
bad=0
for id in A-018 A-023 A-027 A-028 A-029 A-030 A-031 A-032 A-033 C-003 N-002 N-003; do
  ref=$(col "$id" 7) || { echo "$id: not exactly one well-formed row"; bad=1; continue; }
  tok=$(printf '%s\n' "$ref" | sed -n 's/^\[\([A-Z0-9]*\)\] .*/\1/p'); cat=${ref#*] }
  test -n "$tok" -a -f "dictionaries/$tok.xml" || { echo "$id: '$ref' names no vendored dictionary"; bad=1; continue; }
  mts=$(col "$id" 5 | grep -o '35=[A-Z0-9]*' | cut -d= -f2)
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
- after P7, on a scratch copy: one ref's token changed to a dictionary that lacks its MsgType
  (A-030 → `[FIX44]`), and one Title cell given an admin MsgType (`35=A`); each must exit 1.

After P7 it must exit 0. As a cross-check, where the owner-local FIX Repository 2010 edition has
the message, the record also shows its `FIX.5.0SP2/Base/Messages.xml` `<CategoryID>`.

The coverage index files CC, CD, CE and CQ under its own "Application Messages — Infrastructure
(FIX 4.2+)" heading. That heading is the index's grouping, not a Spec ref; 095 leaves the rows
there (FR-019 edits gap notes only).

Measured 2026-10-24 (Gate A round 3 and the post-exhaustion review): the Repository and Orchestra
agreed for every tail MsgType the Repository has, and the Repository has no CQ.

## F. Constitution amendment, v4.0 → v4.1 (FR-021)

Two clauses change, `[const §VI.2]` and `[const §VI.5]`, and so do the Status line and a new Sync
Impact Report. `[const §VI.3]`, `[const §VI.6]` and `[const §IX.6]` are read and left as they are. The
before-texts are `.specify/constitution.md` at `95fdf4c9`. Locate each clause by its heading
(`## Article VI`) and its leading `2.` / `5.`, never by line.

### F.1 Article VI §2

Before:
`2. Every OFFICIAL row's `Spec ref` uses canonical format `[DocAbbrev §X.Y.Z] Section title`. Vague refs (`§4`, "FIX spec") are a CI-linting failure.`

After:
`2. Every OFFICIAL row's `Spec ref` uses canonical format `[DocAbbrev §X.Y.Z] Section title`. A row whose subject is one or more application messages may instead use the message-level form `[DocAbbrev] Category`. In that form, `DocAbbrev` is an application-specification abbreviation from the DocAbbrev Registry in `coverage-index.md` (`FIX40` … `FIX50SP2`, `FIX-Latest`) whose vendored dictionary defines, as an application message, every MsgType that the row's `Title` cell names as `35=<MT>`, of which there is at least one. That dictionary is `dictionaries/<DocAbbrev>.xml`, where an application message is a `message` with `msgcat='app'`; for `FIX-Latest` it is the vendored FIX Orchestra file `dictionaries/orchestra/OrchestraFIXLatest.xml`, where it is a `fixr:message` whose `category` is not `Session`. `Category` names the category, or categories, in which that Orchestra file (each message's `category`) places those MsgTypes, each written as its identifier's words: the identifier with a space before each capital letter but the first (`SettlementInstruction` is written `Settlement Instruction`). Each named category may be preceded by the words of its Orchestra `section`, written the same way, and ` — ` (`Pre Trade — Market Data`). When those MsgTypes lie in several categories, each of their categories is named once, in that form, joined with ` / `, and no other category is named. A row whose subject is a version's whole application layer may use `[DocAbbrev] FIX <version> application specification`, or one bracketed abbreviation per version followed by `application specifications`. Every other row keeps the section form, except as §3 provides. Vague refs (`§4`, "FIX spec") do not conform.`

Design notes (not clause text):
- The form follows the catalogue's message-level rows: one version token and a category. Every
  term is decidable from committed files alone: the token from `dictionaries/`, the
  application-message test from `msgcat` (or Orchestra's `category`), and the category, its words,
  its section and the ` / ` join from the Orchestra file. The data-dictionary rows use the
  whole-layer form.
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

### F.3 Sync Impact Report — new block at line 1, above the v3.0 → v4.0 block

PENDING form (lands with the amendment, P7):

```
<!--
Sync Impact Report — v4.0 → v4.1 (2026-10-24) — PENDING
  Not ratified. Article XX §2 sequence, in order: a scoped Codex Gate A round on this amendment's
    two clause bodies and this report (pending; feature 095's Gate A loop exhausted at round 3 and
    the owner's post-exhaustion pass skipped Codex), Gate B on the 095 PR (pending), owner sign-off
    (pending). This header reads PENDING until all three have happened. The flip to RATIFIED, with
    the rounds and the sign-off date filled in below, is the last commit before merge, not a claim
    made in advance (the v1.0 and v4.0 precedent).
  Bump: MINOR, per Article XX §4; no CHANGELOG.md entry. The test is the v4.0 report's: does any PR
    mergeable under v4.0 fail under v4.1? None does. §2 adds a form and leaves the section form valid
    for every row it was valid for. Its last sentence keeps vague refs non-conforming; "are a
    CI-linting failure" becomes "do not conform", which removes no PR obligation, since no CI job
    checked the clause under v4.0 either. §5 adds a permission and keeps its obligation word for
    word, so a /specify artifact that lists only section entries conforms under both
    versions. A §5 that REQUIRED message-level entries would make such an artifact fail. That would be
    MAJOR, and it was not adopted.
    NOTE: the constitution's version is independent of the library's and of the C ABI's.
  Modified: Article VI §2 — an OFFICIAL row whose subject is application messages may carry the
    message-level Spec ref `[DocAbbrev] Category`. The version token names a version whose vendored
    dictionary defines every MsgType of the row as an application message (`msgcat='app'`; for
    `FIX-Latest`, the vendored Orchestra file). The category is the one the vendored FIX Orchestra
    repository places them in, written as its identifier's words, optionally after its section's
    words. A whole-application-layer form is admitted for version-level rows. Every other row keeps
    `[DocAbbrev §X.Y.Z] Section title`, except as §3 provides. "Vague refs … are a CI-linting
    failure" becomes "do not conform". No CI job checks §2: that check is owed, as is Article IX §6's
    Tier-1 "catalogue consistency check" (follow-up fixpp#626).
  Modified: Article VI §5 — Normative References may also list such message-level refs. One does not
    stand in for a section entry.
  Read and unchanged: Article VI §3 covers design-decision rows (`[impl]`/`[constitution]`). It does
    not reach normative message rows; the 067 coverage-index comment that read it so is history, not
    an amendment. Article VI §6 is unchanged. Article IX §6 is unchanged: its Tier-1 "catalogue
    consistency check" has no job, a gap recorded in the same follow-up as §2's check (fixpp#626).
  Rationale: in the sources checked, no message category is numbered. The FIX Repository 2010
    edition (Categories.xml) identifies one by CategoryID, SectionID and Volume. The FIX 4.4
    application volumes 3–5 head each "CATEGORY: …" and number no heading. The FIX 5.0–5.0SP2
    volumes were not available to check. So §2's section form had no value to cite for an
    application-message row, and the catalogue's application-message rows already carried a
    message-level ref. Feature 095's Gate A round 3 rejected a reading of §3 as covering them. Owner
    ruling 2026-10-24: amend the constitution in 095. §2's CI-lint sentence described a check that does
    not exist (`grep -n -i catalog .github/workflows/*.yml` returns nothing).
  Affected catalogue rows: every OFFICIAL row whose Spec ref carries no `§` and no
    `[impl]`/`[constitution]` token, since the amended §2 decides their conformance (list them with
    the awk in specs/095-tail-evidence-pass/data-model.md §F.1). Of these, feature 095 re-checked
    only its 12 rows (A-018, A-023, A-027 … A-033, C-003, N-002, N-003), with the recipe in the same
    file's §E, and corrects four refs: A-033 and C-003 named another category than Orchestra's;
    A-030 and N-003 named Orchestra's category in a heading spelling. Which of the other rows
    conform is fixpp#626's first-run output.
  Affected feature specs: specs/095-tail-evidence-pass. Merged specs/<id>/ bundles stay as written.
  Process: rides feature 095 (specs/095-tail-evidence-pass). Its PR is titled in Article XX §2 form,
    "Constitution: amend §VI.2, §VI.5 — message-level Spec refs for application-message rows (v4.1)
    (+ 095 tail evidence pass)", and its description states the change, the rationale and the
    affected rows and specs, so §2's four steps are met in order.
  Review and sign-off: pending.
-->
```

RATIFIED form, written in the last commit before merge:
- the header's `— PENDING` becomes `— RATIFIED`;
- the first paragraph becomes "Article XX §2 sequence observed in order: …", with each round's tally
  (Codex and Opus post-judging) and "THEN owner sign-off <date>. This header read PENDING until all
  three had happened.";
- "Review and sign-off: pending." becomes the rounds, the Gate B rounds on PR #<pr>, and the owner's
  sign-off date.

Only these record lines change at the flip. A change to either clause body after the scoped Gate A
round converges needs another scoped round (FR-021).

### F.4 Status line (`> **Status:**`)

PENDING form — the line's current opening `> **Status:** ` is replaced by:

`> **Status:** **v4.1 — PENDING** (2026-10-24) — backwards-compatible (Article XX §4 minor, no `CHANGELOG.md` entry); not ratified: pending a scoped Codex Gate A round on the amendment, Gate B, and the owner's Article XX §2 sign-off. Article VI §2 admits a message-level Spec ref, `[DocAbbrev] Category`, for a row whose subject is application messages; Article VI §5 lets Normative References list such refs. Rides feature 095; its PR is titled in Article XX §2 form. Prior: `

The rest of the line, from `user-ratified **v4.0** (2026-10-23) — **backwards-incompatible**`
onward, is unchanged.

RATIFIED form: `**v4.1 — PENDING** (2026-10-24)` becomes `user-ratified **v4.1** (<sign-off date>)`,
and `not ratified: pending a scoped Codex Gate A round on the amendment, Gate B, and the owner's Article XX §2 sign-off`
becomes `scoped Codex Gate A on feature 095 converged at round <n>, then owner sign-off <date>; amendment PR #<pr>`.
