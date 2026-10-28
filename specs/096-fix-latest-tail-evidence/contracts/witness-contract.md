# Contract — witnesses, walks and differential (FR-001..FR-009)

Clauses W1–W8 are 095's (`specs/095-tail-evidence-pass/contracts/witness-contract.md`), applied
per tier with the changes stated here. Where this file is silent, 095's clause holds as written,
with `TailRows095` read as the suite named below and `FIX50SP2.xml`/`XmlLoader` read as the tier's
dictionary and loader.

**W0 — the move (FR-003).** The items research R-2 lists, starting at 095's `using IndexView` alias,
leave `test_095_tail_rows_roundtrip.cpp` for `tests/support/tail_rows_witness.hpp`, namespace
`fixpp_test_support::tail_rows`, in 095's declaration order, followed by `check_witness` as a free
function
`check_witness(WalkMap const& walk, fixpp::dict::table_view const& tv, std::pmr::memory_resource* read_arena, std::string_view expected_mt, std::string const& body, Intent const& intent)`.
The contract is stated on the moved region after normalisation, not on lines:
- **Outside the region, free:** the header's `#pragma once`, its `#include`s, the namespace wrapper
  and comments; in 095's TU, the `#include` of the header, `using` declarations and the forwarding
  method's body.
- **Inside the region, allowed as a class:** a leading `inline` on a function, and whitespace,
  line breaks and comments (clang-format's reflow and realignment). Nothing else: no statement, type,
  name or literal changes, and no declaration is added, dropped or reordered.
- **`check_witness`:** the method's statements in order, with three substitutions: `walk()`,
  `tv()` and `&read_arena_` become the parameters `walk`, `tv` and `read_arena`; and the statement
  declaring `expected_mt` from `msgtype_for_generated_name(doc(), …)` is dropped, `expected_mt`
  being the parameter. 095's fixture method keeps its signature, computes that MsgType as before
  and forwards.

No 095 test, assertion or literal changes. Verified by quickstart §1's normalised two-way diff (with
its controls), by the 095 TU no longer defining the moved items, and by 095's suites passing in the
same binary.

**W1 — one per MsgType, per suite.** `TailRows096Census.EveryTailMsgTypeHasExactlyOneWitness` holds
suite `TailRows096` to data-model §A's table, and
`TailRows096LatestCensus.EveryTailMsgTypeHasExactlyOneWitness` holds `TailRows096Latest` to the same
table, each mapping a generated name to its MsgType through its own tier's dictionary (`<message
name=… msgtype=…>`; `fixr:message@name`/`@msgType`). A census selects its suite by exact name,
`test_suite_name() == "TailRows096"` (resp. `"TailRows096Latest"`), never by prefix:
`TailRows096` is a prefix of `TailRows096Census`, `TailRows096Latest`, `TailRows096LatestCensus` and
`TailRows096Differential`. Both are 095 W1 otherwise (registered tests, order-free, every finding
collected before asserting; each witness asserts its parsed tag 35).

**W2 — build.** v50sp2: `fixpp::v50sp2::build_<Name>`; vlatest: `fixpp::vlatest::build_<Name>`;
each against its own walk's closure-required set (data-model §B, §C).

**W3 — independent parse.** v50sp2: 095 W3. vlatest: BeginString `FIX.5.0SP2`, `make_frame`,
`fixpp::wire::Parser<Index>` over the `table_view` of a `Dictionary` loaded from the Orchestra file by
`dict::OrchestraLoader`; no builder-side table consulted. The stop rules of 095 W3 hold.

**W4 — readback, both directions.** 095 W4, through the shared `check_witness`, against the tier's
`WalkMap`.

**W5 — required presence.** 095 W5, `required_members_missing` against the tier's `WalkMap`.

**W6 — non-vacuity.** One arm per output, recorded at verify with its command and the message that
fired. A mutant is a scratch edit, reverted, the reverted tree diffed clean before commit. An arm's
RED is the named assertion's own message; a RED from another assertion, a compile error or a parse
error is not that arm's RED. Re-derive the table's completeness rather than trusting it: list the
Orchestra walk's outputs and rules (data-model §B.2: key rule, start, header/trailer skip, field
leg, componentRef AND, groupRef AND, instance members, delimiter with each of its two emission
kinds, `delim_group`, each status, element naming), W1's five census findings plus order-freedom
for each suite, W4's three directions and size assertion, W5's legs, the differential's paths
(data-model §D) and its non-`ok` finding; each must have a row or a stated carry-over.

*Carried over from the shared header, not re-run per tier.* `check_witness` and
`required_members_missing` are one header's functions, compiled into both binaries (W0). Their
mutants run once: the comparator mutants D, H, E and value-mismatch in the v50sp2 table below, and
095's presence-leg mutants L, M, N and P as 095's verify record holds them. This carry-over holds
only while quickstart §1 reads clean; a W0 hunk outside its allowed class voids it.

*v50sp2 TU (`test_077_allversions_builder_roundtrip`).* 095's walk and presence legs get no new
mutants here. What guards them after the move: 095's committed known positives (deficient frames,
35=i, S1/S2) still run in the same binary, and the comparator mutants D, H, E and value-mismatch
below run through the shared `check_witness`.

| Output | Arm | Expected RED / assertion |
|---|---|---|
| field leg = golden, tail set | `TailRows096Census.FieldLegMatchesQuickFixCppGolden`: row-count precondition (exactly one `FIX50SP2,<MT>,` row per tail MsgType), then set equality per MsgType, read from `FIXPP_095_QUICKFIX_GOLDEN_CSV` at test time | mutant: one tag dropped from the walk's field set for one tail MsgType → RED naming it and both sets; mutant: a MsgType absent from `golden.csv` added to the asserted set → RED in the precondition |
| census multiplicity 0 | **Mutant G**: delete `TailRows096.A065_QuoteAck` | RED naming CW, multiplicity 0 |
| census multiplicity > 1 + row mismatch | **Mutant F**: duplicate the A-035 witness as `TailRows096.A036_CrossRequest` | RED naming DS multiplicity 2 and the row mismatch (A036 against A-035) |
| census row mismatch alone | **Mutant O** (own rebuild): rename `TailRows096.A043_PartyDetailsListRequest` to `TailRows096.A044_PartyDetailsListRequest` | RED naming CF's row mismatch, no multiplicity finding |
| census order-freedom | Mutant G, census run alone with `--gtest_filter='TailRows096Census.EveryTailMsgTypeHasExactlyOneWitness'` | RED naming CW, no witness having run |
| census unmapped name | **Mutant I**: add `TailRows096.A035_CrossOrderRequest` | RED naming the unmapped name |
| census extra MsgType | **Mutant J**: add `TailRows096.A001_NewOrderSingle` | RED naming 35=D as extra |
| W1 tag 35 | **Mutant K**: the name → MsgType lookup returns DT for `CrossRequest` (scratch) | RED in the A-035 witness's tag-35 assertion, naming DS and DT |
| W4 parsed, not intended | **Mutant D**: delete one intent entry, setter kept | RED in W4 naming the path |
| W4 intended, not parsed | **Mutant H**: in the A-065 witness, delete an optional field's setter, keep its intent entry | RED in W4 naming the path; W5 stays green |
| W4 value mismatch | after the build and before `make_frame`, one letter of one string literal in the built buffer changed (scratch) | RED in W4 naming the path and both values |
| W4 intent size | **Mutant E**: cut the A-064 witness to one entry (setters and intent entries) | RED in the size assertion |
| W5 message-level field | **Mutant B**: in the A-065 witness, delete the `QuoteAckStatus(1865)` setter and its intent entry | RED in W5 (not W4) naming `1865` |
| W5 group absent | **Mutant A**: in the A-037 witness, delete the required group's assignment and its intent entries | RED in W5 naming the group |

*vlatest TU (`test_077_allversions_builder_roundtrip_vlatest`).* Re-derived from the vlatest
surface: one row per Orchestra-walk output (data-model §B.2), per census finding, per W5 leg as the
vlatest builder can reach it, and per differential path (data-model §D). The generated vlatest Args
were read before each mutant's expected message was written: a closure-required group is a
non-optional span that the builder opens unconditionally (spec Clarifications, Gate A round 1).
Fragments S2, S4 and S5 are inline Orchestra documents in
`TailRows096LatestCensus.OrchestraWalkOnSyntheticFragments`, fed to the walk's entry point.

| Output | Arm | Expected RED / assertion |
|---|---|---|
| key rule (a `msgType` seen twice fails loudly) | fragment S5: two `fixr:message` elements sharing one `msgType` → the walk fails naming it | mutant: the checked insertion replaced by plain assignment (last writer wins) → RED in S5's assertion |
| field leg = golden | `TailRows096LatestCensus.FieldLegMatchesQuickFixCppGolden` (same shape as the v50sp2 row, over the Orchestra walk) | the same two mutants as the v50sp2 row, applied to the Orchestra walk |
| header/trailer skip | the field-leg row above | mutant: the StandardHeader/StandardTrailer skip removed → RED in the field-leg assertion naming the header tags |
| componentRef AND | fragment S4: an optional `fixr:componentRef` holding a required `fixr:fieldRef` → the field is not required | mutant: componentRef presence ignored → RED in S4's assertion |
| groupRef AND (group leg) | `TailRows096LatestCensus.KnownPositive_DeficientFrames` (row below) | mutant: every groupRef treated as required → RED in `KnownPositive_DeficientFrames`; mutant: no groupRef treated as required → RED in E-absent's list |
| instance members (`RequiredGroup.fields`) | frame E-member | mutant: instance members not collected → RED in E-member's list |
| delimiter, component emission | `TailRows096Differential.TailMsgTypesAgree` on 35=DP: its required group's first child is a `fixr:componentRef` that supplies the delimiter (re-derive: the §B.2 `awk` on the group that DP's required `fixr:groupRef` names, then on the component its first child names) | mutant: component emissions ignored when taking the first emission → RED in `TailMsgTypesAgree` naming DP and the path `<count_tag>/delim` with both values; the A-042 witness may also go RED and is not this arm's RED |
| delimiter, nested-group emission | frame CC-nodelim (group 2148's first child is a `fixr:groupRef`, whose count tag is the delimiter) | mutant: nested-group emissions ignored when taking the first emission → RED in CC-nodelim's list |
| `delim_group` | frames CC-nested, CC-count2 | mutant: `delim_group` never set → RED in CC-nested's and CC-count2's lists (095's `delim_group` leg falls silent) |
| status `nested_required_group` and element naming | `TailRows096LatestCensus.NestedRequiredGroupFailsLoud_MassQuote`: the status for 35=i and the element `NoQuoteEntries` (the `numInGroup` field's name, §B.2), as literals | mutant: detector returns `ok` → RED naming 35=i; mutant: the element named by `fixr:group@name` → RED naming the element |
| status `delimiter_chain` | fragment S2 (Orchestra form of 095's S2) → that status and the inner group's count-field name | mutant: chain check disabled → RED in S2's assertion |
| presence legs on real frames | `TailRows096LatestCensus.KnownPositive_DeficientFrames`: 095's frames E-absent, E-one, E-short, E-member, E-nodelim, CC-nodelim, CC-nested, CC-count2, parsed under the Orchestra dictionary, each against its exact list re-derived from the Orchestra file (data-model §B.2) | each frame's exact list; a frame the parser refuses or splits otherwise than 095's table states stops the implementer |
| census (five findings + order-freedom) | Mutants G, F, I, J (one rebuild), O (own rebuild), K, on `TailRows096Latest` with the same names as the v50sp2 rows; K's lookup is the Orchestra `fixr:message@name` one | as in the v50sp2 table |
| W5 message-level field, vlatest witness | **Mutant B**: in the A-065 witness, delete the `QuoteAckStatus(1865)` setter and its intent entry (an optional member, emitted only when set) | RED in W5 (not W4) naming `1865` |
| W5 count-below-two, vlatest witness | **Mutant A′**: in the A-037 witness, delete the `order_entries` assignment and its intent entries; the builder writes `2428=0` | RED in W5's count-below-two leg naming `2428`, recorded together with W4's `parsed, not intended: 2428=0`, which co-fires by construction |
| W5 group absent, vlatest | frame E-absent (row above): on this tier no real witness can omit a required group's count tag | E-absent's exact list |
| differential agrees, tail set | `TailRows096Differential.TailMsgTypesAgree`: findings over the 31 tail MsgTypes, asserted empty, printed otherwise | mutant: 35=W appended to the compared list (scratch) → RED printing the finding `{W, 779, required, absent}` |
| differential field path | `TailRows096Differential.KnownDifference_MarketDataSnapshotFullRefresh`: the finding list equals exactly `[{W, 779, required, absent}]` (W7 exception) | mutant: the field comparison skipped → RED (empty findings) |
| differential non-`ok` path | `TailRows096Differential.KnownNonOk_MassQuote`: the finding list for 35=i equals exactly one finding, path `status`, each side's value `nested_required_group NoQuoteEntries` (W7 exception) | mutant: the non-`ok` check skipped → RED in that assertion |
| differential group, delimiter, instance-member, `delim_group` paths | `TailRows096Differential.SyntheticPairs`: one QuickFIX-XML fragment and one Orchestra fragment per path, identical but for that path; each pair's finding list equals exactly one whole `Finding` (MsgType, path, both values) | per path, mutant: that comparison skipped → RED in that pair's assertion |

**W7 — no result recorded.** 095 W7. The committed exceptions in 096: the vlatest deficient-frame
lists, the 35=i status and element, the synthetic fragments (S2, S4, S5 and the differential pairs)
and their expected statuses or findings, the 35=W finding and the 35=i differential finding. Each
re-runs on every invocation of its binary. Any other reading over all messages is stated as a
condition and a recipe, never as an outcome.

**W8 — existing coverage.** Both 077 TUs are byte-unchanged
(`git diff --exit-code origin/main -- tests/session/test_077_allversions_builder_roundtrip.cpp tests/session/test_077_allversions_builder_roundtrip_vlatest.cpp`);
095's TU changes only by W0's move; every 076, 077 and 095 test passes.
