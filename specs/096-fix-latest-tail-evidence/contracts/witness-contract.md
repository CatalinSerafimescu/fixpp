# Contract — witnesses, walks and differential (FR-001..FR-009)

Clauses W1–W8 are 095's (`specs/095-tail-evidence-pass/contracts/witness-contract.md`), applied
per tier with the changes stated here. Where this file is silent, 095's clause holds as written,
with `TailRows095` read as the suite named below and `FIX50SP2.xml`/`XmlLoader` read as the tier's
dictionary and loader.

**W0 — the move (FR-003).** The items research R-2 lists leave `test_095_tail_rows_roundtrip.cpp`
for `tests/support/tail_rows_witness.hpp`, namespace `fixpp_test_support::tail_rows`, with exactly
the changes R-2 lists: a leading `inline` on each moved function, `walk_status::wrapped_group` and
its `to_string` case, and `check_witness` as a free function
`check_witness(WalkMap const& walk, fixpp::dict::table_view const& tv, std::pmr::memory_resource* read_arena, std::string_view expected_mt, std::string const& body, Intent const& intent)`
whose statements are the method's, in order, with `walk()`, `tv()`, `&read_arena_` and the computed
expected MsgType replaced by those parameters. 095's fixture method forwards to it. No 095 test,
assertion or literal changes. Verified by quickstart §1's line check and by 095's suites passing.

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
Orchestra walk's statuses and rules (data-model §B.2), W1's five census findings plus order-freedom
for each suite, W4's three directions and size assertion, W5's legs, the differential's paths
(data-model §D) and its non-`ok` finding; each must have a row.

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

*vlatest TU (`test_077_allversions_builder_roundtrip_vlatest`).*

| Output | Arm | Expected RED / assertion |
|---|---|---|
| Orchestra field leg = golden | `TailRows096LatestCensus.FieldLegMatchesQuickFixCppGolden` (same shape as the v50sp2 row, over the Orchestra walk) | the same two mutants, applied to the Orchestra walk |
| header/trailer skip | mutant: the StandardHeader/StandardTrailer skip removed | RED in the field-leg assertion naming the header tags |
| path AND over componentRef | `TailRows096LatestCensus.OrchestraWalkOnSyntheticFragments`, fragment S4: an optional `fixr:componentRef` holding a required `fixr:fieldRef` → the field is not required | mutant: componentRef presence ignored → RED in S4's assertion |
| groupRef presence | the deficient frames below | mutant: every groupRef treated as required → RED in `TailRows096LatestCensus.KnownPositive_DeficientFrames` (and the tail witnesses); mutant: no groupRef treated as required → RED in E-absent's list |
| delimiter rule | frame CC-nodelim below | mutant: the delimiter taken as the first `fixr:fieldRef` only (component and group emissions ignored) → RED in CC-nodelim's list |
| status `nested_required_group` | `TailRows096LatestCensus.NestedRequiredGroupFailsLoud_MassQuote`: status for 35=i and the nested group named, as literals | mutant: detector returns `ok` → RED naming 35=i |
| status `delimiter_chain` | `OrchestraWalkOnSyntheticFragments`, fragment S2 (Orchestra form of 095's S2) | status and inner group's name; mutant: chain check disabled → RED |
| status `wrapped_group` | `OrchestraWalkOnSyntheticFragments`, fragment S3: a required componentRef whose component's only child is a non-required groupRef | status and the component's name; mutant: detector disabled → RED |
| presence legs on real frames | `TailRows096LatestCensus.KnownPositive_DeficientFrames`: 095's frames E-absent, E-one, E-short, E-member, E-nodelim, CC-nodelim, CC-nested, CC-count2, parsed under the Orchestra dictionary, each against its exact list re-derived from the Orchestra file (data-model §B.2) | each frame's exact list; a frame the parser refuses or splits otherwise than 095's table states stops the implementer |
| census (five findings + order-freedom) | Mutants G, F, I, J (one rebuild), O (own rebuild), K, on `TailRows096Latest` with the same names as the v50sp2 rows | as in the v50sp2 table |
| W5 on vlatest witnesses | **Mutant B** (A-065, `1865`) and **Mutant A** (A-037 group), in `TailRows096Latest` | RED in W5 naming the member |
| differential agrees, tail set | `TailRows096Differential.TailMsgTypesAgree`: findings over the 31 tail MsgTypes, asserted empty, printed otherwise | mutant: 35=W appended to the compared list (scratch) → RED naming W, path `779`, side `FIX50SP2.xml` |
| differential field path | `TailRows096Differential.KnownDifference_MarketDataSnapshotFullRefresh`: exactly one finding, `{W, 779, FIX50SP2.xml}` (W7 exception) | mutant: the field comparison skipped → RED (empty findings) |
| differential group, delimiter, instance-member, delim_group, non-`ok` paths | `TailRows096Differential.SyntheticPairs`: one QuickFIX-XML fragment and one Orchestra fragment per path, identical but for that path, each → its exact finding | per path, mutant: that comparison skipped → RED in that pair's assertion |

**W7 — no result recorded.** 095 W7. The committed exceptions in 096: the vlatest deficient-frame
lists, the 35=i status, the synthetic fragments and their expected statuses or findings, and the 35=W
finding. Each re-runs on every invocation of its binary.

**W8 — existing coverage.** Both 077 TUs are byte-unchanged
(`git diff --exit-code origin/main -- tests/session/test_077_allversions_builder_roundtrip.cpp tests/session/test_077_allversions_builder_roundtrip_vlatest.cpp`);
095's TU changes only by W0's move; every 076, 077 and 095 test passes.
