# Data model — 096 FIX Latest tail evidence pass

## A. Rows → MsgTypes → generated names → witnesses

The generated name is the dictionary's message name; `FIX50SP2.xml` and the Orchestra file use the
same name for every tail MsgType, and it is the name in the catalogue Title. Re-check:
`grep -o "<message name='[^']*' msgtype='<MT>'" dictionaries/FIX50SP2.xml` against
`grep -o '<fixr:message [^>]*msgType="<MT>"' dictionaries/orchestra/OrchestraFIXLatest.xml`.

Witness IDs: `TailRows096.<RowId>_<Name>` (v50sp2) and `TailRows096Latest.<RowId>_<Name>` (vlatest),
`<RowId>` without its hyphen. The TU's literal tail table is this one; each census holds its suite to
it (contracts W1).

| Row | MsgType | Name |
|---|---|---|
| A-035 | DS | CrossRequest |
| A-036 | DT | CrossRequestAck |
| A-037 | DJ | MassOrder |
| A-038 | DK | MassOrderAck |
| A-039 | DC | TradeMatchReport |
| A-040 | DR | MarketDataReport |
| A-041 | DO | MarketDataStatisticsRequest |
| A-042 | DP | MarketDataStatisticsReport |
| A-043 | CF | PartyDetailsListRequest |
| A-044 | CG | PartyDetailsListReport |
| A-045 | CK | PartyDetailsListUpdateReport |
| A-046 | CX | PartyDetailsDefinitionRequest |
| A-047 | CY | PartyDetailsDefinitionRequestAck |
| A-048 | CU | PartyEntitlementsRequest |
| A-049 | CV | PartyEntitlementsReport |
| A-050 | CZ | PartyEntitlementsUpdateReport |
| A-051 | DA | PartyEntitlementsDefinitionRequest |
| A-052 | DB | PartyEntitlementsDefinitionRequestAck |
| A-053 | CL | PartyRiskLimitsRequest |
| A-054 | CM | PartyRiskLimitsReport |
| A-055 | CR | PartyRiskLimitsUpdateReport |
| A-056 | CS | PartyRiskLimitsDefinitionRequest |
| A-057 | CT | PartyRiskLimitsDefinitionRequestAck |
| A-058 | DE | PartyRiskLimitsReportAck |
| A-059 | DH | PartyActionRequest |
| A-060 | DI | PartyActionReport |
| A-061 | DF | PartyRiskLimitCheckRequest |
| A-062 | DG | PartyRiskLimitCheckRequestAck |
| A-063 | CN | SecurityMassStatusRequest |
| A-064 | CO | SecurityMassStatus |
| A-065 | CW | QuoteAck |

## B. Closure-required tree (per MsgType and leg, derived at test runtime)

The result type is 095's (`specs/095-tail-evidence-pass/data-model.md` §B): `WalkMap` of
`WalkResult{status, element, fields, groups}`, `RequiredGroup{count_tag, delim_tag, delim_group,
fields}`. It moves to `tests/support/tail_rows_witness.hpp` (research R-2) unchanged; no status value
is added.

### B.1 v50sp2 leg

095's `Walker` over `FIXPP_DICT_DATA_DIR "/FIX50SP2.xml"`, unchanged, loaded once in the 096 v50sp2
fixture's `SetUpTestSuite`. Its rules, statuses, `required_members_missing` leg order and once-only
rule are 095 §B's.

### B.2 vlatest leg

`OrchestraWalker` over `FIXPP_ORCHESTRA_DATA_DIR "/OrchestraFIXLatest.xml"` (research R-4), loaded
once in the vlatest fixture's `SetUpTestSuite`; the synthetic arms pass `load_string` fragments to
the same entry point. Its output feeds the same `required_members_missing` and readback comparator.

```
walk_all() -> OrchestraWalk{WalkMap walk; std::string error}   # walk keyed by fixr:message@msgType
  keys:       a msgType seen twice sets error = "duplicate fixr:message msgType <MT>" (no
              last-writer-wins); error is empty otherwise
  start:      fixr:message/fixr:structure (a reference outside it is not walked)
  fieldRef:   tag = @id;  required on the path iff every element on it has presence="required"
  componentRef: resolve @id in fixr:components; skip StandardHeader / StandardTrailer; AND its presence
  groupRef:   resolve @id in fixr:groups; count_tag = fixr:numInGroup/@id; AND its presence
  delim_tag:  first tag the group emits in declaration order (numInGroup excluded, components
              expanded, a nested groupRef emitting its count tag)
  statuses:   ok | nested_required_group | delimiter_chain  (+ the element named)
  element:    a group is named by its count field's name (the fixr:field@name whose id is the
              group's fixr:numInGroup id), as 095's walk names it; a component by fixr:component@name
```

- `nested_required_group`: a required group inside a required group's instance;
- `delimiter_chain`: `delim_group`'s own delimiter is again a count tag.

The duplicate-`msgType` rule reports through `OrchestraWalk::error`, a type local to the vlatest TU,
so the shared `WalkMap`, `WalkResult` and `walk_status` stay as W0 moves them. The fixture's
`SetUpTestSuite` copies a non-empty `error` into its `load_error`, so `SetUp` fails every test of the
suite with it (095's fixture pattern; no `ADD_FAILURE` in `SetUpTestSuite`, which throws under
`--gtest_throw_on_failure`). Fragment S5 asserts the exact string (contracts W6).

A required `fixr:componentRef` whose component holds only a non-required `fixr:groupRef` gets no
status: the path rule makes that group not required (spec Clarifications, Gate A round 1).

The deficient frames' expected lists for this leg are dictionary facts re-derived from the Orchestra
file, never pasted from the walk:

```
awk '/<fixr:message [^>]*msgType="<MT>"/,/<\/fixr:message>/' dictionaries/orchestra/OrchestraFIXLatest.xml
awk '/<fixr:group id="<ID>"/,/<\/fixr:group>/'           dictionaries/orchestra/OrchestraFIXLatest.xml
awk '/<fixr:component id="<ID>"/,/<\/fixr:component>/'   dictionaries/orchestra/OrchestraFIXLatest.xml
```

applying the rules above to each element on the path (35=E → its `NoOrders` group; 35=CC → its
`NoAsgnReqs` group → `Parties`), ordering the paths by 095 §B's sequence rule. Each TU asserts its
own expected list for each frame; neither copies the other's lists. The frame bodies are 095's,
copied byte for byte into the vlatest TU (quickstart §1b).

## C. Chosen values and the intent list (per witness)

095 §C applies to each witness, against its own tier's dictionary, with these substitutions:
- strings `"<Row>_<Msg>_<field>"`, inside instance k `"<Row>_<Msg>_<field>_i<k>"`, where `<Row>` is
  the row ID without its hyphen. The vlatest witness of a row carries the same strings as the v50sp2
  witness; the two are separate TUs, and nothing compares them;
- a field with declared enum values: its first declared value in that tier's dictionary (the first
  `<value enum=…>` of the field in `FIX50SP2.xml`; the first `fixr:code value` of the field's
  `fixr:codeSet` in the Orchestra file). If the two firsts differ for one field, each witness uses its
  own; L-069-1 is why the builder would not notice either way;
- UTCTimestamp `"20261028-12:00:00.000"`; LocalMktDate `"20261028"`.

Every other rule of 095 §C holds as written: two statements per value (intent entry and setter, neither
reading the other), NumInGroup = instance count (≥ 2; 1 for a delimiter's nested group), an int
without an enum = its tag (plus k in instance k), per-instance distinguishing literals, and the stop
rule for a required group with no string or int member.

## D. Cross-dictionary differential

```
diff(qf_walk, orch_walk, msg_types) -> vector<Finding>   # every finding of every given MsgType, then sorted
Finding: { msg_type, path, fix50sp2, orchestra }   # the value each dictionary holds at path
compared, by tag:                                        path                      each value
  message-level required fields                          <tag>                     required | absent
  required groups by count tag                           <count_tag>               required | absent
  per group required on both sides: delim_tag            <count_tag>/delim         the delimiter tag
  per group required on both sides: instance members     <count_tag>[*].<tag>      required | absent
  per group required on both sides: delim_group          <count_tag>/delim_group   "<count>,<delim>" | none
  a walk status other than ok on either side             status                    "<status> <element>" | ok
```

A finding is emitted only where the two values differ, except `status`: when either side's status
is not `ok`, that MsgType yields exactly one `status` finding carrying both sides' values and no
other finding, since its sets are not comparable. A group required on one side only yields its
`<count_tag>` finding and no member, delimiter or `delim_group` finding. The comparator collects
every finding of every MsgType it is given before it returns, then sorts them by `msg_type`, then
`path`, each compared as a `std::string` (`operator<`, so `"1865"` sorts before `"779"` and
`"9100[*].9104"` before `"9102"`). `TailMsgTypesAgree` passes the 31 tail MsgTypes and asserts the
result empty; each known positive passes its own MsgTypes. Each finding is printed whole; a test
asserting a finding compares the whole struct.

Known positives (research R-6), each a W7 exception re-run on every invocation: 35=W yields exactly
`[{W, 779, required, absent}]`; 35=i yields exactly one `status` finding whose two values are
`nested_required_group NoQuoteEntries`. Synthetic pairs drive the group, delimiter, instance-member
and `delim_group` paths, and pair M drives collect-every-difference, the sort and the one-sided-group
rule (contracts W6).

## E. Catalogue edits (US3)

For each row of §A: Status `done`; FIX version(s) `5.0SP2, FIX Latest`; Spec ref per the table below;
`/specify` `096-fix-latest-tail-evidence`; PR `#<pr>`; Tests and Verified as below. No other cell of
these rows, and no other row, changes.

**Spec refs** — `[FIX-Latest] <Section words> — <Category words>`:

| Rows | MsgTypes | Orchestra category / section | After | Before |
|---|---|---|---|---|
| A-035, A-036 | DS, DT | Indication / PreTrade | `[FIX-Latest] Pre Trade — Indication` | `[FIX-Latest] Trade — Cross Orders` |
| A-037, A-038 | DJ, DK | OrderMassHandling / Trade | unchanged | `[FIX-Latest] Trade — Order Mass Handling` |
| A-039 | DC | TradeCapture / PostTrade | `[FIX-Latest] Post Trade — Trade Capture` | `[FIX-Latest] Post-Trade` |
| A-040 … A-042 | DR, DO, DP | MarketData / PreTrade | `[FIX-Latest] Pre Trade — Market Data` | `[FIX-Latest] Pre-Trade — Market Data` |
| A-043 … A-047 | CF, CG, CK, CX, CY | PartiesReferenceData / PreTrade | `[FIX-Latest] Pre Trade — Parties Reference Data` | `[FIX-Latest] Pre-Trade — Party Details` |
| A-048 … A-052 | CU, CV, CZ, DA, DB | PartiesReferenceData / PreTrade | `[FIX-Latest] Pre Trade — Parties Reference Data` | `[FIX-Latest] Pre-Trade — Party Entitlements` |
| A-053 … A-058 | CL, CM, CR, CS, CT, DE | PartiesReferenceData / PreTrade | `[FIX-Latest] Pre Trade — Parties Reference Data` | `[FIX-Latest] Pre-Trade — Party Risk Limits` |
| A-059, A-060 | DH, DI | PartiesAction / PreTrade | `[FIX-Latest] Pre Trade — Parties Action` | `[FIX-Latest] Pre-Trade — Party Details` |
| A-061, A-062 | DF, DG | PartiesAction / PreTrade | `[FIX-Latest] Pre Trade — Parties Action` | `[FIX-Latest] Pre-Trade — Party Risk Limits` |
| A-063, A-064 | CN, CO | SecuritiesReferenceData / PreTrade | `[FIX-Latest] Pre Trade — Securities Reference Data` | `[FIX-Latest] Pre-Trade — Securities Reference Data` |
| A-065 | CW | QuotationNegotiation / PreTrade | `[FIX-Latest] Pre Trade — Quotation Negotiation` | `[FIX-Latest] Pre-Trade — Quotation Negotiation` |

**Re-check the 31 refs against `[const §VI.2]`**: family A of quickstart §5a's records checker. It
reads each row's MsgType from its Title (`35=<MT>`), as the clause does, and checks: the token is
`FIX-Latest` (registry leg; the DocAbbrev Registry lists it); the Orchestra file defines the MsgType
once with a category that is not `Session` (application-message leg); the ref is `<category words>`
or `<section words> — <category words>`, where `<section words>` are those of that category's
`section` (category leg). A word break is a space before each capital but the first (the clause's
"identifier's words" rule). Its RED arms are quickstart §5a's: before the edits it flags every tail
row except A-037 and A-038, and the three family-A perturbations each exit 1 with their leg's
message. The same checker holds every other cell below to its template (families B–E).

**Tests cells**: `` `tests/session/test_096_tail_rows_roundtrip.cpp`: `TailRows096.<RowId>_<Name>`; `tests/session/test_096_tail_rows_roundtrip_vlatest.cpp`: `TailRows096Latest.<RowId>_<Name>`, `TailRows096Differential.TailMsgTypesAgree` ``.

**Verified cells**, byte for byte; no sentence contains `|`. Every row: S0, a space, S1, where
`<MT>` is the row's MsgType.

- **S0:** `offline bar (owner 2026-10-21) on both typed tiers (owner 2026-10-28): typed builder → wire → independent dict-driven parse → bidirectional per-field + per-group-instance readback, every closure-required field and group set; fixpp::v50sp2 checked against a runtime walk of FIX50SP2.xml, fixpp::vlatest against a runtime walk of OrchestraFIXLatest.xml, and the two walks agree for 35=<MT> (TailRows096Differential).`
- **S1:** `` 5.0SP2 here is the vendored FIX50SP2.xml (QuickFIX-cpp v1.16.0's file, which carries the 5.0SP2 Extension Pack that added 35=<MT>); QuickFIX/J 3.0.1's base edition does not define it. Live: no cell carries 35=<MT>; the typed tail's one live FIXT.1.1 scenario is A-018's (B-095-1). `validate_<Msg>` checks presence, not enum domains (L-069-1, fixpp#637). FIX Latest goes on the wire as ApplVerID 9 with no ApplExtID(1156) (L-074-1, REMAINING-WORK row 4b). ``

**Coverage-index edits**, byte for byte, line count unchanged:
- the last column of each tail MsgType's row in "FIX Latest — MsgType entries only (FIX-Latest)":
  `—` becomes `` 096 witnesses: `TailRows096.<RowId>_<Name>`, `TailRows096Latest.<RowId>_<Name>` ``;
- appended after a space to the section's `Scope:` line, to its "074-orchestra-native-reader link"
  paragraph line, to its `**Conclusion:**` line, and to the Post-1.0 Gap Registry row naming
  A-035–A-065, the same comment:
  `<!-- SUPERSEDED (096-fix-latest-tail-evidence, constitution v4.2): the 31 MsgTypes of A-035–A-065 are also application messages of the vendored dictionaries/FIX50SP2.xml (5.0SP2 Extension Packs; absent from the base edition), and their catalogue rows are done, closed on fixpp::v50sp2 and fixpp::vlatest; the text before this comment is history. -->`

**B&L delta**, in the live `spec/behaviors-and-limitations.md`:
- appended to the L-069-1 bullet, after a space, the text of this block:

  ```
  **Widened by 096-fix-latest-tail-evidence (2026-10-28):** the same holds for every builder-bearing namespace (`fixpp::v42`, `fixpp::v44`, `fixpp::v50sp2`, `fixpp::vlatest`): one emitter (`tools/codegen/fixpp-codegen/emit_builders.cpp`) writes every `validate_<Msg>`, and its generic walk (`fixpp::wire::validate_required`, `include/fixpp/wire/builder_validate.hpp`) iterates only `writer_traits<T>::required_checks` and `writer_traits<T>::group_checks`, so it checks required presence and group counts and no enum domain; a value's type comes from the C++ member type of `<Msg>Args`, not from the validator. The rows A-035 … A-065 close with this disclosed. **Re-derive:** read the loops of `validate_required` in `include/fixpp/wire/builder_validate.hpp`, and the two arrays in any generated `*.validator.inl`. **Status: follow-up** (fixpp#637).
  ```
- a new section at the end of the file, holding only what a witness or the differential exposes
  (`B-096-<n>` / `L-096-<n>`, numbered from 1), or, if nothing is exposed, no section. The verify
  record states which. One candidate is already known: vlatest Mutant A′ (contracts W6) shows that
  `fixpp::vlatest::build_<Msg>` writes `<count_tag>=0` for an empty required group and that only
  `validate_<Msg>` rejects it. The verify record greps the live B&L for an existing disclosure of
  `build_<Msg>` not calling `validate_<Msg>`; it cites that entry if one exists, and otherwise
  records whether a new row is written and why.

## F. Constitution amendment, v4.1 → v4.2 (FR-010)

### F.1 Article XVIII §2 — the v1.2 bullet

Before:

> - **v1.2 — FIX Latest application messages** (new MsgTypes A-035..A-065 + EP-level field additions to existing messages, per coverage-index Post-1.0 Gap Registry). *(Annotation, …)*

After (the annotation unchanged and kept in place, then the new one):

> - **v1.2 — FIX Latest application messages** (EP-level field additions to existing messages, per coverage-index Post-1.0 Gap Registry). *(Annotation, … [unchanged])* *(Amended v4.2, feature 096: the rows A-035..A-065 leave this milestone. Their MsgTypes are application messages of the vendored `FIX50SP2.xml`, which carries the FIX 5.0SP2 Extension Packs that added them, as well as of the vendored FIX Orchestra file, and feature 096 closes the rows in v1.0 (§7).)*

### F.2 Article XVIII §7 — one new sentence

The opening sentence is unchanged. It already names `fixpp::v50sp2` and "the further A- rows this
clause names below, by row ID, as delivered or closed", so naming the rows below brings them into
the scope it states. Adding `fixpp::vlatest` to its namespace list would claim A-001..A-013 and the
M-/P-/C-/R-/N- families as v1.0 typed scope under `fixpp::vlatest`, which 096 does not verify.

A new sentence, inserted right after "…feature 095 closes the catalogue rows A-018, A-023, A-027,
A-028, A-029, A-030, A-031, A-032, A-033 and C-003 on it.":

> **The rows A-035, A-036, A-037, A-038, A-039, A-040, A-041, A-042, A-043, A-044, A-045, A-046,
> A-047, A-048, A-049, A-050, A-051, A-052, A-053, A-054, A-055, A-056, A-057, A-058, A-059, A-060,
> A-061, A-062, A-063, A-064 and A-065** each name one MsgType that is an application message of the
> vendored `FIX50SP2.xml`, which carries the FIX 5.0SP2 Extension Packs that added it, and of the
> vendored FIX Orchestra file; feature 096 closes these catalogue rows on both `fixpp::v50sp2` and
> `fixpp::vlatest`.

Each row is written out by its ID, never as a range: the opening sentence scopes "the further A-
rows this clause names below, by row ID", and the ratified v4.1 text named each of 095's rows
individually (the v4.1 Sync Impact Report's range spellings are history, not the clause). Family C
of quickstart §5a's records checker holds §XVIII.7's line to its merge-base text with this sentence
inserted, byte for byte, and §XVIII.2's bullet likewise to F.1's after-text.

### F.3 Sync Impact Report — a new first comment block, above the v4.0 → v4.1 block

The block is inserted immediately above the v4.0 → v4.1 block, so it opens the file. It has two
states, PENDING and RATIFIED. The header's last word selects the state, and the opening span, the
review span and the Status line (§F.4) must all be in that state, never a mix. The records checker
(quickstart §5a, family C) reads both templates from this section.

**PENDING** — the text as first committed, in the amendment's commit:

```
<!--
Sync Impact Report — v4.1 → v4.2 (2026-10-28) — PENDING
  Pending. Article XX §2 sequence: a scoped Codex Gate A review of the amended clause bodies, THEN
    Gate B on the feature PR, THEN owner sign-off. This header reads PENDING until all three have
    happened; the flip to RATIFIED is the last commit before merge.
  Bump: MINOR, per Article XX §4; no CHANGELOG.md entry. Does a PR mergeable under v4.1 fail under
    v4.2? None does. §2 removes rows from a post-1.0 milestone because they ship in v1.0; §7 records
    which feature closes them and on which namespace. Neither adds a banned
    pattern, tightens a budget or imposes an obligation.
    NOTE: the constitution's version is independent of the library's and of the C ABI's.
  Modified: Article XVIII §2 — the v1.2 milestone no longer names "new MsgTypes A-035..A-065"; it
    keeps the EP-level field additions. Its 076/077 annotation is unchanged.
  Modified: Article XVIII §7 — one new sentence records that the rows A-035, A-036, A-037, A-038,
    A-039, A-040, A-041, A-042, A-043, A-044, A-045, A-046, A-047, A-048, A-049, A-050, A-051, A-052,
    A-053, A-054, A-055, A-056, A-057, A-058, A-059, A-060, A-061, A-062, A-063, A-064 and A-065,
    each named by its row ID, name application messages of the vendored FIX50SP2.xml and of the
    Orchestra file, and that feature 096 closes them on both `fixpp::v50sp2` and `fixpp::vlatest`. The opening sentence is unchanged: it
    already takes in the A- rows the clause names by row ID, and adding `fixpp::vlatest` to its
    namespace list would claim every row it scopes for that namespace.
  Read and unchanged: Article I §1 (its FIX Latest bullet states what the FIX Latest tiers deliver and
    that ApplExtID(1156)=303 and session negotiation remain post-1.0; feature 096 changes neither);
    Article VI §2 (feature 096 corrects its rows' refs to the form v4.1 admits); Article XVIII §1
    ("FIX 4.0–5.0SP2 + FIXT.1.1 session + application layers": the 31 MsgTypes are FIX 5.0SP2
    Extension Pack application messages, inside that scope); Article XVIII §5 (no early shipping of
    post-1.0 protocols: after §2's edit the rows are not post-1.0 content, and §4 makes that move an
    amendment, which this is).
  Rationale: the plan of record called A-035..A-065 FIX-Latest-only rows with no QuickFIX peer. The
    vendored FIX50SP2.xml, byte-identical to QuickFIX-cpp v1.16.0's, defines all of their MsgTypes as
    application messages, so each has a `fixpp::v50sp2` typed builder as well as a `fixpp::vlatest`
    one. Owner ruling 2026-10-28: close the rows on both tiers in v1.0. Leaving §2 and §7 as they were
    would contradict the catalogue (Article XVIII §4, Article XX §1).
  Affected catalogue rows: A-035 through A-065 (version, Spec ref, Status, /specify, PR, Tests,
    Verified; specs/096-fix-latest-tail-evidence/data-model.md §E).
  Affected feature specs: specs/096-fix-latest-tail-evidence. Merged specs/<id>/ bundles stay as written.
  Process: rides feature 096. Its PR is titled in Article XX §2 form, "Constitution: amend §XVIII.2,
    §XVIII.7 — FIX Latest tail rows A-035..A-065 delivered in v1.0 on v50sp2 and vlatest (v4.2) (+ 096
    FIX Latest tail evidence pass)", and its description states the change, the rationale and the
    affected rows and specs.
  Review and sign-off: [filled at each step].
-->
```

**RATIFIED** — the flip commit's form, as v4.1's flip wrote it (`023f2406`). `{OPENING}`, `{REVIEW}`
and `{DATE}` are slots:

```
<!--
Sync Impact Report — v4.1 → v4.2 (2026-10-28) — RATIFIED
  Ratified. Article XX §2 sequence observed in order: {OPENING}
  Bump: MINOR, per Article XX §4; no CHANGELOG.md entry. Does a PR mergeable under v4.1 fail under
    v4.2? None does. §2 removes rows from a post-1.0 milestone because they ship in v1.0; §7 records
    which feature closes them and on which namespace. Neither adds a banned
    pattern, tightens a budget or imposes an obligation.
    NOTE: the constitution's version is independent of the library's and of the C ABI's.
  Modified: Article XVIII §2 — the v1.2 milestone no longer names "new MsgTypes A-035..A-065"; it
    keeps the EP-level field additions. Its 076/077 annotation is unchanged.
  Modified: Article XVIII §7 — one new sentence records that the rows A-035, A-036, A-037, A-038,
    A-039, A-040, A-041, A-042, A-043, A-044, A-045, A-046, A-047, A-048, A-049, A-050, A-051, A-052,
    A-053, A-054, A-055, A-056, A-057, A-058, A-059, A-060, A-061, A-062, A-063, A-064 and A-065,
    each named by its row ID, name application messages of the vendored FIX50SP2.xml and of the
    Orchestra file, and that feature 096 closes them on both `fixpp::v50sp2` and `fixpp::vlatest`. The opening sentence is unchanged: it
    already takes in the A- rows the clause names by row ID, and adding `fixpp::vlatest` to its
    namespace list would claim every row it scopes for that namespace.
  Read and unchanged: Article I §1 (its FIX Latest bullet states what the FIX Latest tiers deliver and
    that ApplExtID(1156)=303 and session negotiation remain post-1.0; feature 096 changes neither);
    Article VI §2 (feature 096 corrects its rows' refs to the form v4.1 admits); Article XVIII §1
    ("FIX 4.0–5.0SP2 + FIXT.1.1 session + application layers": the 31 MsgTypes are FIX 5.0SP2
    Extension Pack application messages, inside that scope); Article XVIII §5 (no early shipping of
    post-1.0 protocols: after §2's edit the rows are not post-1.0 content, and §4 makes that move an
    amendment, which this is).
  Rationale: the plan of record called A-035..A-065 FIX-Latest-only rows with no QuickFIX peer. The
    vendored FIX50SP2.xml, byte-identical to QuickFIX-cpp v1.16.0's, defines all of their MsgTypes as
    application messages, so each has a `fixpp::v50sp2` typed builder as well as a `fixpp::vlatest`
    one. Owner ruling 2026-10-28: close the rows on both tiers in v1.0. Leaving §2 and §7 as they were
    would contradict the catalogue (Article XVIII §4, Article XX §1).
  Affected catalogue rows: A-035 through A-065 (version, Spec ref, Status, /specify, PR, Tests,
    Verified; specs/096-fix-latest-tail-evidence/data-model.md §E).
  Affected feature specs: specs/096-fix-latest-tail-evidence. Merged specs/<id>/ bundles stay as written.
  Process: rides feature 096. Its PR is titled in Article XX §2 form, "Constitution: amend §XVIII.2,
    §XVIII.7 — FIX Latest tail rows A-035..A-065 delivered in v1.0 on v50sp2 and vlatest (v4.2) (+ 096
    FIX Latest tail evidence pass)", and its description states the change, the rationale and the
    affected rows and specs.
  Review and sign-off: {REVIEW} owner sign-off {DATE}.
-->
```

The slots are the only lines that differ between the two templates or change while the gates run.
Every other line of the block is fixed text, byte for byte the same in both states:

| Slot | PENDING | RATIFIED |
|---|---|---|
| the header's last word | `PENDING` | `RATIFIED` |
| opening span: from the line after the header up to, not including, the `  Bump:` line | first line fullmatches `  Pending\. Article XX §2 sequence.*`; every further line `    \S.*`; it may be filled in as each step completes | first line fullmatches `  Ratified\. Article XX §2 sequence observed in order: .*\S`; every further line `    \S.*`; in v4.1's form, it names each step's rounds and the sign-off |
| review span: from the `  Review and sign-off:` line up to, not including, `-->` | first line fullmatches `  Review and sign-off: .*\S`; every further line `    \S.*` | the same, and its last line ends `owner sign-off {DATE}.` |

The header's date stays 2026-10-28 in both states. `{DATE}` is the owner's sign-off date,
`YYYY-MM-DD`, the same here and at both places in the Status line.

### F.4 Status line (`> **Status:**`)

The line is the state's template below, one space, then `{PRIOR}`: the merge-base Status line's
text after `> **Status:** `, from `user-ratified **v4.1**` to its end. v4.1's Status text thus
becomes v4.2's "Prior:". Under PENDING nothing in the template varies. Under RATIFIED the slots are
`{DATE}`, `{REVIEW}` and `{PR}`, at the positions v4.1's flip used. Everything else is fixed
text, the 31 row IDs included.

**PENDING** — the amendment's commit:

```
> **Status:** PENDING **v4.2** (2026-10-28) — backwards-compatible (Article XX §4 minor, no `CHANGELOG.md` entry); Article XVIII §2 drops A-035..A-065 from v1.2 and Article XVIII §7 records the rows A-035, A-036, A-037, A-038, A-039, A-040, A-041, A-042, A-043, A-044, A-045, A-046, A-047, A-048, A-049, A-050, A-051, A-052, A-053, A-054, A-055, A-056, A-057, A-058, A-059, A-060, A-061, A-062, A-063, A-064 and A-065 as closed by feature 096 on `fixpp::v50sp2` and `fixpp::vlatest`. Rides feature 096; its PR is titled in Article XX §2 form. Prior:
```

**RATIFIED** — the flip commit:

```
> **Status:** user-ratified **v4.2** ({DATE}) — backwards-compatible (Article XX §4 minor, no `CHANGELOG.md` entry); {REVIEW} owner sign-off {DATE}; amendment PR #{PR}. Article XVIII §2 drops A-035..A-065 from v1.2 and Article XVIII §7 records the rows A-035, A-036, A-037, A-038, A-039, A-040, A-041, A-042, A-043, A-044, A-045, A-046, A-047, A-048, A-049, A-050, A-051, A-052, A-053, A-054, A-055, A-056, A-057, A-058, A-059, A-060, A-061, A-062, A-063, A-064 and A-065 as closed by feature 096 on `fixpp::v50sp2` and `fixpp::vlatest`. Rides feature 096; its PR is titled in Article XX §2 form. Prior:
```

- `{DATE}`: the owner's sign-off date, the same at both places and in the Sync Impact Report's review
  span (§F.3);
- `{REVIEW}`: free text naming the scoped Gate A rounds and Gate B, in v4.1's form (v4.1's reads
  "scoped Codex Gate A on feature 095 converged (…), then Gate B and");
- `{PR}`: the feature PR's number, the records checker's `PR`.

The Status line is the file's only current-version marker; every other `v4.1` in the file is in
the v4.0 → v4.1 Sync Impact Report, which is history and stays as written (re-check with
`grep -n 'v4\.1' .specify/constitution.md`).
