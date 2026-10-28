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
fields}`. It moves to `tests/support/tail_rows_witness.hpp` (research R-2) with one new status value,
`wrapped_group`.

### B.1 v50sp2 leg

095's `Walker` over `FIXPP_DICT_DATA_DIR "/FIX50SP2.xml"`, unchanged, loaded once in the 096 v50sp2
fixture's `SetUpTestSuite`. Its rules, statuses, `required_members_missing` leg order and once-only
rule are 095 §B's.

### B.2 vlatest leg

`OrchestraWalker` over `FIXPP_ORCHESTRA_DATA_DIR "/OrchestraFIXLatest.xml"` (research R-4), loaded
once in the vlatest fixture's `SetUpTestSuite`; the synthetic arms pass `load_string` fragments to
the same entry point. Its output feeds the same `required_members_missing` and readback comparator.

```
walk(doc) -> WalkMap                       # keyed by fixr:message@msgType
  start:      fixr:message/fixr:structure
  fieldRef:   tag = @id;  required on the path iff every element on it has presence="required"
  componentRef: resolve @id in fixr:components; skip StandardHeader / StandardTrailer; AND its presence
  groupRef:   resolve @id in fixr:groups; count_tag = fixr:numInGroup/@id; AND its presence
  delim_tag:  first tag the group emits in declaration order (numInGroup excluded, components
              expanded, a nested groupRef emitting its count tag)
  statuses:   ok | nested_required_group | delimiter_chain | wrapped_group  (+ the element named)
```

- `nested_required_group`: a required group inside a required group's instance;
- `delimiter_chain`: `delim_group`'s own delimiter is again a count tag;
- `wrapped_group`: a required `fixr:componentRef` whose component, as written, has exactly one
  element child (`fixr:fieldRef`, `fixr:componentRef` or `fixr:groupRef`), and that child is a
  `fixr:groupRef` without `presence="required"`.

The deficient frames' expected lists for this leg are dictionary facts re-derived from the Orchestra
file, never pasted from the walk:

```
awk '/<fixr:message [^>]*msgType="<MT>"/,/<\/fixr:message>/' dictionaries/orchestra/OrchestraFIXLatest.xml
awk '/<fixr:group id="<ID>"/,/<\/fixr:group>/'           dictionaries/orchestra/OrchestraFIXLatest.xml
awk '/<fixr:component id="<ID>"/,/<\/fixr:component>/'   dictionaries/orchestra/OrchestraFIXLatest.xml
```

applying the rules above to each element on the path (35=E → its `NoOrders` group; 35=CC → its
`NoAsgnReqs` group → `Parties`), ordering the paths by 095 §B's sequence rule. Where a list equals
095's list for the same frame, the witness contract says so and the two TUs still each assert their
own literal.

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
diff(mt) -> vector<Finding>        # collected, then asserted empty for every tail MsgType
Finding: { msg_type, path, side ∈ {FIX50SP2.xml, Orchestra} }
compared, by tag:
  message-level required fields                          path <tag>
  required groups by count tag                           path <count_tag>
  per required group: delim_tag                          path <count_tag>/delim
  per required group: one instance's required fields     path <count_tag>[*].<tag>
  per required group: delim_group (count, delimiter)     path <count_tag>/delim_group
```

Both inputs are `WalkResult`s with status `ok`; a non-`ok` status on either side is a finding of its
own (`path` = the status name, `side` = the file). The comparator lists every finding, sorted by
MsgType, then path, then side.

Known positive (research R-6): 35=W yields exactly one finding, path `779`, side `FIX50SP2.xml`.
Synthetic pairs drive the group, delimiter and instance-member paths (contracts W6).

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

**Re-check the 31 refs against `[const §VI.2]`** (run from the library root as `bash`; `CAT` may point
at a scratch copy). It reads each row's MsgType from its Title (`35=<MT>`), as the clause does, and
checks: the token is `FIX-Latest` (registry leg; the DocAbbrev Registry lists it); the Orchestra file
defines the MsgType once with a category that is not `Session` (application-message leg); the ref is
`<category words>` or `<section words> — <category words>`, where `<section words>` are those of that
category's `section` (category leg). `words()` is the clause's rule: a space before each capital but
the first.

```bash
CAT=${CAT:-spec/feature-catalogue.md}
O=dictionaries/orchestra/OrchestraFIXLatest.xml
col() {   # 095 quickstart §7's: column $2 of the one well-formed row with ID $1
  awk -F'|' -v want="$1" -v c="$2" '{id=$2; gsub(/ /,"",id)} id==want {n++; nf=NF; v=$c}
    END {gsub(/^ +| +$/,"",v); if (n!=1 || nf!=13) exit 1; print v}' "$CAT"
}
words() { sed 's/\([A-Z]\)/ \1/g; s/^ //'; }
bad=0
for n in $(seq 35 65); do
  id=$(printf 'A-%03d' "$n")
  ref=$(col "$id" 7) || { echo "$id: not exactly one well-formed row"; bad=1; continue; }
  tok=$(printf '%s\n' "$ref" | sed -n 's/^\[\([A-Za-z0-9-]*\)\] .*/\1/p'); words_ref=${ref#*] }
  test "$tok" = FIX-Latest || { echo "$id: token '$tok' is not FIX-Latest"; bad=1; continue; }
  mts=$(col "$id" 5 | grep -o '35=[A-Za-z0-9]*' | cut -d= -f2)
  test -n "$mts" || { echo "$id: Title names no 35=<MT>"; bad=1; continue; }
  for mt in $mts; do
    m=$(grep -o "<fixr:message [^>]*msgType=\"$mt\"[^>]*>" "$O")
    test "$(printf '%s\n' "$m" | grep -c .)" = 1 || { echo "$id $mt: not one Orchestra message"; bad=1; continue; }
    c=$(printf '%s\n' "$m" | grep -o 'category="[^"]*"' | cut -d'"' -f2)
    test -n "$c" -a "$c" != Session || { echo "$id $mt: category '$c' is not an application category"; bad=1; continue; }
    s=$(grep -o "<fixr:category [^>]*name=\"$c\"[^>]*>" "$O" | grep -o 'section="[^"]*"' | cut -d'"' -f2)
    cw=$(printf '%s\n' "$c" | words); sw=$(printf '%s\n' "$s" | words)
    test "$words_ref" = "$cw" -o "$words_ref" = "$sw — $cw" \
      || { echo "$id $mt: Orchestra '$s — $c', ref names '$words_ref'"; bad=1; }
  done
done
exit $bad
```

Its RED arms, each recorded with what it printed:
- **positive control, before the catalogue edits:** on the unedited catalogue it exits 1 and flags
  every tail row except A-037 and A-038, and no other row;
- after the edits, on a scratch copy, one perturbation per run, each exiting 1 with its leg's
  message: (a) A-065's token changed to `[FIX44]` (registry leg); (b) A-065's Title given `35=A`
  (application-message leg: Orchestra's Logon is category `Session`); (c) A-065's ref written
  `Pre-Trade — Quotation Negotiation` (category leg, the hyphen spelling).

After the edits it exits 0.

**Tests cells**: `` `tests/session/test_096_tail_rows_roundtrip.cpp`: `TailRows096.<RowId>_<Name>`; `tests/session/test_096_tail_rows_roundtrip_vlatest.cpp`: `TailRows096Latest.<RowId>_<Name>`, `TailRows096Differential.TailMsgTypesAgree` ``.

**Verified cells**, byte for byte; no sentence contains `|`. Every row: S0, a space, S1, where
`<MT>` is the row's MsgType.

- **S0:** `offline bar (owner 2026-10-21) on both typed tiers (owner 2026-10-28): typed builder → wire → independent dict-driven parse → bidirectional per-field + per-group-instance readback, every closure-required field and group set; fixpp::v50sp2 checked against a runtime walk of FIX50SP2.xml, fixpp::vlatest against a runtime walk of OrchestraFIXLatest.xml, and the two walks agree for 35=<MT> (TailRows096Differential).`
- **S1:** `5.0SP2 here is the vendored FIX50SP2.xml (QuickFIX-cpp v1.16.0's file, which carries the 5.0SP2 Extension Pack that added 35=<MT>); QuickFIX/J 3.0.1's base edition does not define it. Live: no cell carries 35=<MT>; the typed tail's one live FIXT.1.1 scenario is A-018's (B-095-1). validate_<Msg> checks presence, not enum domains (L-069-1, fixpp#637). FIX Latest goes on the wire as ApplVerID 9 with no ApplExtID(1156) (L-074-1, REMAINING-WORK row 4b).`

**Coverage-index edits**, byte for byte, line count unchanged:
- the last column of each tail MsgType's row in "FIX Latest — MsgType entries only (FIX-Latest)":
  `—` becomes `` 096 witnesses: `TailRows096.<RowId>_<Name>`, `TailRows096Latest.<RowId>_<Name>` ``;
- appended after a space to the section's `Scope:` line, to its "074-orchestra-native-reader link"
  paragraph line, and to the Post-1.0 Gap Registry row naming A-035–A-065, the same comment:
  `<!-- SUPERSEDED (096-fix-latest-tail-evidence, constitution v4.2): the 31 MsgTypes of A-035–A-065 are also application messages of the vendored dictionaries/FIX50SP2.xml (5.0SP2 Extension Packs; absent from the base edition), and their catalogue rows are done, closed on fixpp::v50sp2 and fixpp::vlatest; the text before this comment is history. -->`

**B&L delta**, in the live `spec/behaviors-and-limitations.md`:
- appended to the L-069-1 bullet, after a space, the text of this block:

  ```
  **Widened by 096-fix-latest-tail-evidence (2026-10-28):** the same holds for every builder-bearing namespace (`fixpp::v42`, `fixpp::v44`, `fixpp::v50sp2`, `fixpp::vlatest`): one emitter (`tools/codegen/fixpp-codegen/emit_builders.cpp`) writes every `validate_<Msg>`, and its generic walk (`fixpp::wire::validate_required`, `include/fixpp/wire/builder_validate.hpp`) checks required presence and group counts only. The rows A-035 … A-065 close with this disclosed. **Re-derive:** `grep -n enum tools/codegen/fixpp-codegen/emit_builders.cpp`. **Status: follow-up** (fixpp#637).
  ```
- a new section at the end of the file, holding only what a witness or the differential exposes
  (`B-096-<n>` / `L-096-<n>`, numbered from 1), or, if nothing is exposed, no section. The verify
  record states which.

## F. Constitution amendment, v4.1 → v4.2 (FR-010)

### F.1 Article XVIII §2 — the v1.2 bullet

Before:

> - **v1.2 — FIX Latest application messages** (new MsgTypes A-035..A-065 + EP-level field additions to existing messages, per coverage-index Post-1.0 Gap Registry). *(Annotation, …)*

After (the annotation unchanged and kept in place, then the new one):

> - **v1.2 — FIX Latest application messages** (EP-level field additions to existing messages, per coverage-index Post-1.0 Gap Registry). *(Annotation, … [unchanged])* *(Amended v4.2, feature 096: the rows A-035..A-065 leave this milestone. Their MsgTypes are application messages of the vendored `FIX50SP2.xml`, which carries the FIX 5.0SP2 Extension Packs that added them, as well as of the vendored FIX Orchestra file, and feature 096 closes the rows in v1.0 (§7).)*

### F.2 Article XVIII §7 — two sentences

1. The opening sentence's namespace list, before: "v1.0's typed-message scope under `fixpp::v42`,
   `fixpp::v44`, `fixpp::v50sp2` is …"; after: "v1.0's typed-message scope under `fixpp::v42`,
   `fixpp::v44`, `fixpp::v50sp2` and `fixpp::vlatest` is …". The rest of the sentence is unchanged.
2. A new sentence, inserted right after "…feature 095 closes the catalogue rows A-018, A-023, A-027,
   A-028, A-029, A-030, A-031, A-032, A-033 and C-003 on it.":
   "**The rows A-035 through A-065** each name one MsgType that is an application message of the
   vendored `FIX50SP2.xml`, which carries the FIX 5.0SP2 Extension Packs that added it, and of the
   vendored FIX Orchestra file; feature 096 closes these catalogue rows on `fixpp::v50sp2` and on
   `fixpp::vlatest`."

### F.3 Sync Impact Report — a new first comment block, above the v4.0 → v4.1 block

```
<!--
Sync Impact Report — v4.1 → v4.2 (2026-10-28) — PENDING
  Pending. Article XX §2 sequence: a scoped Codex Gate A review of the amended clause bodies, THEN
    Gate B on the feature PR, THEN owner sign-off. This header reads PENDING until all three have
    happened; the flip to RATIFIED is the last commit before merge.
  Bump: MINOR, per Article XX §4; no CHANGELOG.md entry. Does a PR mergeable under v4.1 fail under
    v4.2? None does. §2 removes rows from a post-1.0 milestone because they ship in v1.0; §7 records
    which feature closes them and names the namespace it closes them on. Neither adds a banned
    pattern, tightens a budget or imposes an obligation.
    NOTE: the constitution's version is independent of the library's and of the C ABI's.
  Modified: Article XVIII §2 — the v1.2 milestone no longer names "new MsgTypes A-035..A-065"; it
    keeps the EP-level field additions. Its 076/077 annotation is unchanged.
  Modified: Article XVIII §7 — the opening sentence names `fixpp::vlatest` among the typed-message
    namespaces; a new sentence records that the rows A-035 through A-065 name application messages
    of the vendored FIX50SP2.xml and of the Orchestra file, and that feature 096 closes them on
    `fixpp::v50sp2` and `fixpp::vlatest`.
  Read and unchanged: Article I §1 (its FIX Latest bullet states what the FIX Latest tiers deliver and
    that ApplExtID(1156)=303 and session negotiation remain post-1.0; feature 096 changes neither);
    Article VI §2 (feature 096 corrects its rows' refs to the form v4.1 admits).
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

### F.4 Status line (`> **Status:**`)

Prepend, before "user-ratified **v4.1**": `PENDING **v4.2** (2026-10-28) — backwards-compatible
(Article XX §4 minor, no `CHANGELOG.md` entry); Article XVIII §2 drops A-035..A-065 from v1.2 and
Article XVIII §7 records them as closed by feature 096 on `fixpp::v50sp2` and `fixpp::vlatest`.
Rides feature 096; its PR is titled in Article XX §2 form. Prior: ` — and at the RATIFIED flip,
`PENDING` becomes `user-ratified` and the review/sign-off clause is inserted after the date, in
v4.1's form.

The Status line is the file's only current-version marker; every other `v4.1` in the file is in
the v4.0 → v4.1 Sync Impact Report, which is history and stays as written (re-check with
`grep -n 'v4\.1' .specify/constitution.md`).
