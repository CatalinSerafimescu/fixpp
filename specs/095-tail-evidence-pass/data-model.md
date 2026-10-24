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
- per instance, for a `delim_group`: the nested count tag **present** with a value not equal to 1,
  or the nested delimiter absent. An absent nested count is not this leg's: the nested count tag is
  the outer group's `delim_tag`, so the delimiter leg reports it.

Each missing path is reported **once**, by exactly one leg:
- a `delim_tag` that is also a required instance field (E's `NoOrders`(73), whose delimiter `ClOrdID`(11)
  is required) is reported by the delimiter leg, and the field leg skips it;
- a `delim_tag` that is a nested count tag (CC's `NoAsgnReqs`(1499) → `NoPartyIDs`(453)) is reported
  by the delimiter leg when absent, and by the `delim_group` leg when present with a value not 1.

So each leg has a frame on which it alone fires (contracts W6). Each item names its path (`<tag>`,
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
- Spec ref cells are unchanged except A-033 → `[FIX50SP2] Market Data` and C-003 → `[FIX50SP2]
  Account Reporting` (FR-018; spec Clarifications, Gate A round 3). These edits come after §F's
  amendment lands.
- A-018's Verified names the generated `ExecutionAck`.

**Re-check the 12 refs against the amended `[const §VI.2]` (§F.1)**, at P7, recording the output in
the verify record.
- **Token leg.** For each row, take its Spec ref's `[DocAbbrev]` and each MsgType of the row
  (§A). Then `grep -c "msgtype='<MT>'" dictionaries/<DocAbbrev>.xml` must print 1 for every
  MsgType.
- **Category leg.** For each MsgType, print its category, the authority, with
  `grep -o "<fixr:message [^>]*msgType=\"<MT>\"[^>]*>" dictionaries/orchestra/OrchestraFIXLatest.xml | grep -o 'category="[^"]*"'`.
  The row's `Category` must name that category. As a cross-check, where the owner-local FIX
  Repository 2010 edition has the message, the record also shows its `FIX.5.0SP2/Base/Messages.xml`
  `<CategoryID>`.

The coverage index files CC, CD, CE and CQ under its own "Application Messages — Infrastructure
(FIX 4.2+)" heading. That heading is the index's grouping, not a Spec ref; 095 leaves the rows
there (FR-019 edits gap notes only).

Measured 2026-10-24 with this recipe:
- the Repository and Orchestra agreed for every tail MsgType the Repository has;
- the Repository has no CQ;
- the two corrected rows were the only category mismatches;
- every token passed.

N-003's `Application Sequencing` is the specification's heading for category `Application`, which
the clause admits. That heading was not checked against a FIX 5.0 volume, because none is local.

## F. Constitution amendment, v4.0 → v4.1 (FR-021)

Two clauses change, `[const §VI.2]` and `[const §VI.5]`, and so do the Status line and a new Sync
Impact Report. `[const §VI.3]`, `[const §VI.6]` and `[const §IX.6]` are read and left as they are. The
before-texts are `.specify/constitution.md` at `95fdf4c9`. Locate each clause by its heading
(`## Article VI`) and its leading `2.` / `5.`, never by line.

### F.1 Article VI §2

Before:
`2. Every OFFICIAL row's `Spec ref` uses canonical format `[DocAbbrev §X.Y.Z] Section title`. Vague refs (`§4`, "FIX spec") are a CI-linting failure.`

After:
`2. Every OFFICIAL row's `Spec ref` uses canonical format `[DocAbbrev §X.Y.Z] Section title`. A row whose subject is one or more application messages may instead use the message-level form `[DocAbbrev] Category`. In that form, `DocAbbrev` is an application-specification abbreviation from the DocAbbrev Registry in `coverage-index.md` (`FIX40` … `FIX50SP2`, `FIX-Latest`) for a version whose vendored dictionary under `dictionaries/` defines every MsgType the row names. `Category` names the message category in which the vendored FIX Orchestra repository under `dictionaries/orchestra/` (each message's `category`) places those MsgTypes. It is written as the identifier's words or as the specification's heading for it, may be preceded by the category's section (`Pre-Trade — Market Data`), and joins several categories with ` / `. A row whose subject is a version's whole application layer may use `[DocAbbrev] FIX <version> application specification`, or one bracketed abbreviation per version followed by `application specifications`. Every other row keeps the section form, except as §3 provides. Vague refs (`§4`, "FIX spec") do not conform. No CI job checks this clause yet; that check is owed.`

Design notes (not clause text):
- The form is what the catalogue's message-level rows already carry: one version token and a
  category title. The FIX Latest rows add the section prefix, and the data-dictionary rows use the
  whole-layer form.
- Volume is left out. The Repository has it (`Categories.xml` `<Volume>`), but no row carries it,
  and requiring it would make every message-level row non-conforming.
- The token is tied to a dictionary that defines the messages, not to the version that introduced
  them or to the row's version column. Orchestra's `added` disagrees with A-018's, A-029's, A-030's
  and N-002's tokens, and 095 narrows four version columns without moving their tokens. The dictionary
  test is checkable with `grep` (§E's recipe).
- The clause states no reason. The reason (no numbered section to cite) is a measurement over the
  sources that were checked, so it goes in the Sync Impact Report's Rationale, scoped to those
  sources.
- The category authority is the vendored Orchestra file, because the owed CI lint can read it. The
  FIX Repository 2010 edition is owner-local and not committed, so it is a cross-check (§E's recipe).
  Both are FIX Trading Community categories; on the tail MsgTypes they agreed wherever the Repository
  has the message (§E, measured 2026-10-24).
- The issue number of the owed check is kept out of the clause, because an issue number in a clause
  rots. It goes in §F.3 and the plan.
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
    for every row it was valid for. Its last sentence keeps vague refs non-conforming and records that
    no CI job checks the clause, which was already so. §5 adds a permission and keeps its obligation
    word for word, so a /specify artifact that lists only section entries conforms under both
    versions. A §5 that REQUIRED message-level entries would make such an artifact fail. That would be
    MAJOR, and it was not adopted.
    NOTE: the constitution's version is independent of the library's and of the C ABI's.
  Modified: Article VI §2 — an OFFICIAL row whose subject is application messages may carry the
    message-level Spec ref `[DocAbbrev] Category`. The version token names a version whose dictionary
    defines every MsgType of the row in its vendored dictionary. The category is the one the vendored
    FIX Orchestra repository places them in. A whole-application-layer form is admitted
    for version-level rows. Every other row keeps `[DocAbbrev §X.Y.Z] Section title`, except as §3
    provides. "Vague refs … are a CI-linting failure" becomes "do not conform", and the CI check is
    recorded as owed (follow-up fixpp#<N>).
  Modified: Article VI §5 — Normative References may also list such message-level refs. One does not
    stand in for a section entry.
  Read and unchanged: Article VI §3 covers design-decision rows (`[impl]`/`[constitution]`). It does
    not reach normative message rows; the 067 coverage-index comment that read it so is history, not
    an amendment. Article VI §6 is unchanged. Article IX §6 is unchanged: its Tier-1 "catalogue
    consistency check" has no job, a gap recorded in the same follow-up as §2's check.
  Rationale: in the sources checked, no message category is numbered. The FIX Repository 2010
    edition (Categories.xml) identifies one by CategoryID, SectionID and Volume. The FIX 4.4
    application volumes 3–5 head each "CATEGORY: …" and number no heading. The FIX 5.0–5.0SP2
    volumes were not available to check. So §2's section form had no value to cite for an
    application-message row, and the catalogue's application-message rows already carried a
    message-level ref. Feature 095's Gate A round 3 rejected a reading of §3 as covering them. Owner
    ruling 2026-10-24: amend the constitution in 095. §2's CI-lint sentence described a check that does
    not exist (`grep -n -i catalog .github/workflows/*.yml` returns nothing).
  Affected catalogue rows: the 12 rows feature 095 closes (A-018, A-023, A-027 … A-033, C-003,
    N-002, N-003) conform as amended, A-033 and C-003 after 095 corrects their category. Rows
    outside 095 are not re-checked here; the follow-up lint checks them.
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
  sign-off date;
- `fixpp#<N>` becomes the filed follow-up's number (that edit may land earlier, as soon as it is
  filed).

Only these record lines change at the flip. A change to either clause body after the scoped Gate A
round converges needs another scoped round (FR-021).

### F.4 Status line (`> **Status:**`)

PENDING form — the line's current opening `> **Status:** ` is replaced by:

`> **Status:** **v4.1 — PENDING** (2026-10-24) — backwards-compatible (Article XX §4 minor, no `CHANGELOG.md` entry); not ratified: pending a scoped Codex Gate A round on the amendment, Gate B, and the owner's Article XX §2 sign-off. Article VI §2 admits a message-level Spec ref, `[DocAbbrev] Category`, for a row whose subject is application messages, and records that its CI check is owed; Article VI §5 lets Normative References list such refs. Rides feature 095; its PR is titled in Article XX §2 form. Prior: `

The rest of the line, from `user-ratified **v4.0** (2026-10-23) — **backwards-incompatible**`
onward, is unchanged.

RATIFIED form: `**v4.1 — PENDING** (2026-10-24)` becomes `user-ratified **v4.1** (<sign-off date>)`,
and `not ratified: pending a scoped Codex Gate A round on the amendment, Gate B, and the owner's Article XX §2 sign-off`
becomes `scoped Codex Gate A on feature 095 converged at round <n>, then owner sign-off <date>; amendment PR #<pr>`.
