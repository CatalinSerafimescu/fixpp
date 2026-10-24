# Contract — offline witness (FR-001..FR-006)

**W1 — one per MsgType.** Exactly one test per tail MsgType, named `TailRows095.<RowId>_<Name>`
(data-model §A; `<RowId>` without its hyphen). `TailRows095Census.EveryTailMsgTypeHasExactlyOneWitness`
iterates the registered tests of suite `TailRows095` through `::testing::UnitTest::GetInstance()`
(not the tests that ran), maps each `<Name>` to its MsgType through the dictionary's
`<message name=… msgtype=…>`, and requires multiplicity 1 for each MsgType of the TU's literal tail
table, the name's row equal to the table's row, no unmapped name and no extra MsgType (FR-004a). It
collects every finding before it asserts. It is therefore independent of run order,
`--gtest_filter` and `--gtest_shuffle`. Each witness also asserts that its parsed tag 35 equals the
MsgType its own `current_test_info()` name maps to.

**W2 — build.** Args are populated from the literals of the witness's **intent list** (data-model §C)
and built with `fixpp::v50sp2::build_<Name>` into a caller buffer; the build MUST succeed. Every
closure-required member is set; every required group has ≥ 2 instances, and each instance sets the
group's delimiter and its required members; at least two fields in total. The delimiter is the
group's first emitted tag (data-model §B). When it is a nested group's count tag (CC's `NoAsgnReqs`),
each instance carries that nested group with exactly one instance, which sets its own delimiter.

**W3 — independent parse.** The body is framed with BeginString `FIX.5.0SP2` and parsed by
`fixpp::wire::Parser<Index>` over a `table_view` loaded from `FIX50SP2.xml` by `XmlLoader`. No
builder-side table is consulted.

**W4 — readback, both directions.** The witness enumerates every parsed body entry
(`OffsetTable::entries()`), removes from **both** sides the tags W3's framing writes and tag 35
(checked by W1), assigns each remaining entry to message level
or to the `group_slices(count_tag)` instance of the outermost required group whose extent contains
it, and forms its path (`<tag>` or `<count_tag>[<k>].<tag>`; a delimiter's nested group's entries,
its count included, take paths in the outer instance, e.g. `1499[<k>].453`, `1499[<k>].448`). It
compares that multiset with the intent list exactly: a path missing
from the frame, a path in the frame that was not intended, and a value mismatch each fail, naming the
path. Count fields are intent entries. Instance order is checked by the per-instance literals
(`_i<k>`). It also asserts that the intent list holds at least two entries.

**W5 — required presence.** `required_members_missing(...)` (data-model §B) takes the MsgType from
the parsed frame's tag 35 and reads W4's enumeration, so message-level and per-instance entries are
attributed by extent, never by first occurrence. It returns the closure-required members it did not
find, each with its path, and the witness asserts that list is empty, printing it otherwise.

**W6 — non-vacuity: one arm per output (recorded at verify with the command and the message that
fired).** Every status the walk can return, every direction of W4's comparator, every leg of W5's
helper and every census finding has a row. A committed known positive re-runs on every build; a
mutant is a scratch edit, reverted, and the reverted tree is diffed clean before commit. Where no real
dictionary can drive an output, the arm is an inline synthetic dictionary (`load_string` literal) fed
to the same walk entry point (data-model §B).

| Output | Arm | Expected RED / assertion |
|---|---|---|
| walk `ok`, field leg | `FieldLegMatchesQuickFixCppGolden`: it first asserts that `golden.csv` holds exactly one `FIX50SP2,<MT>,` row for each of the 24 MsgTypes (the 23 tail MsgTypes and 35=E), so an absent row cannot read as an empty set; then, for each, the walk's message-level field set equals that row of `tools/quickfix_required_golden/golden.csv` (measured QuickFIX-cpp 1.16.0 output), read from the file at test time and never copied into the TU | a mismatch names the MsgType and both sets |
| walk `ok`, group leg (only-child inheritance) | **Mutant C**: the walk ignores the only-child inheritance | the deficient-frame assertions on `NoOrders`(73) go RED; Mutant A, re-applied on top, no longer goes RED in W5 (the rule, not a list, decides) |
| walk `nested_required_group` (required group in an instance) | `NestedRequiredGroupFailsLoud_MassQuote`: the status for 35=i; the walk returns a status per MsgType, so this does not break `SetUpTestSuite` for the 23 | mutant: the detector returns `ok` → RED naming 35=i |
| walk `nested_required_group` (delimiter chain) | `WalkStatusesOnSyntheticDictionaries`, document S2: a message requiring a group whose first emitted element is a group whose own first emitted element is again a group | the status and the inner group's name; mutant: the chain check disabled → RED |
| walk `group_among_siblings` | `WalkStatusesOnSyntheticDictionaries`, document S1: a message with a required component holding one field and one group | the status and the component's name; mutant: the detector disabled → RED |
| W4 "parsed, not intended" | **Mutant D**: delete one intent entry, setter kept | RED in W4 naming the path |
| W4 "intended, not parsed" | **Mutant H**: in the A-018 witness, delete the setter of an optional field (e.g. CumQty(14)) and keep its intent entry | RED in W4 naming the path; W5 stays green, since the field is optional |
| W4 value mismatch | a literal changed after the build in one witness | RED in W4 naming the path |
| W4 intent-size assertion | **Mutant E**: cut the BK witness to one field (delete the other setters **and** their intent entries) | RED in the size assertion, with that assertion's message |
| W5 message-level field | **Mutant B**: in the A-018 witness, delete the `ExecAckStatus(1036)` setter **and** its intent entry | RED in W5 (not W4), naming `1036` |
| W5 group absent | **Mutant A**: in the A-027 TradingSessionList witness, delete the `trading_sessions` assignment **and** its intent entries; and frame E-absent (35=E without `NoOrders`) | RED in W5 naming the group; the frame's exact list |
| W5 count below 2 | frame E-one: 35=E with one `NoOrders` instance | the exact list |
| W5 instance count ≠ count value | frame E-short: literal bytes, `73=2` followed by one complete instance | the exact list |
| W5 instance member | frame E-member: two instances, the second lacking `ListSeqNo`(67) | the exact list (`73[1].67`) |
| W5 instance delimiter | frame CC-nodelim: 35=CC with `1499=2`, instance 0 carrying only its `NoRelatedSym` group and no `NoPartyIDs` | the exact list (`1499[0].453`), reported once, by the delimiter leg; the `delim_group` leg is silent, because the nested count tag is absent (data-model §B) |
| W5 instance delimiter, alone | **Mutant L**: disable only the delimiter leg | RED on CC-nodelim (the list loses `1499[0].453`); no other leg reports that path |
| W5 delimiter's nested group, delimiter absent | frame CC-nested: instance 0 carries `453=1` and a PartyIDSource(447) but no PartyID(448) | the exact list (`1499[0].448`), reported by the `delim_group` leg's second disjunct alone (the count is present and equals 1; the delimiter leg sees 453) |
| W5 delimiter's nested group, count ≠ 1 | frame CC-count2: 35=CC, `1499=2`, instance 0 carrying `453=2` with two complete 448-delimited nested instances, instance 1 well-formed | the exact list (`1499[0].453`), reported by the `delim_group` leg's first disjunct alone (the count is present; both nested instances carry 448) |
| W5 nested count ≠ 1, alone | **Mutant M**: disable only the `!= 1` comparison | RED on CC-count2 (the list comes back empty) |
| W5 nested delimiter absent, alone | **Mutant N**: disable only the `delim_group` leg's nested-delimiter-absent disjunct | RED on CC-nested (the list comes back empty) |
| census multiplicity 0 | **Mutant G**: delete one witness (e.g. `TailRows095.C003_AccountSummaryReport`) | RED naming CQ with multiplicity 0 |
| census multiplicity > 1, row mismatch | **Mutant F**: duplicate the A-018 witness as `TailRows095.A024_ExecutionAck` (A-024 is BN's duplicate catalogue row) | RED naming BN with multiplicity 2 **and** the row mismatch (A024 against A-018); gtest names are unique in a suite, so a second name always carries another row |
| census unmapped name | **Mutant I**: add `TailRows095.A018_ExecutionAcknowledgement` (the catalogue title, which is no dictionary message name) | RED naming the unmapped name |
| census extra MsgType | **Mutant J**: add `TailRows095.A001_NewOrderSingle` | RED naming 35=D as extra |
| W1 per-witness tag 35 | **Mutant K**: the name → MsgType lookup returns, for one generated name, another tail MsgType (scratch) | RED in that witness's tag-35 assertion, naming both MsgTypes (the census goes RED too) |

All frames (E-*, CC-*) live in `KnownPositive_DeficientFrames` and assert the exact list
`required_members_missing` returns. They are literal bytes or typed-builder output. The implementer
confirms in P0 that the parser accepts each frame. If it refuses one, the leg that frame serves cannot
be reached from a parsed frame: the implementer stops and reports, and a parse error is never
recorded as that leg's RED. Mutants F, G, I and J may share one rebuild, because the census reports
every finding; the record must show each one's message. CC-count2's two nested instances parse as
one outer instance: `consume_group_extent` consumes the nested 453 extent through `consume_one`, and
`group_slices_status` splits on the outer delimiter only at the outer level (`src/wire/offset_table.cpp`).
The implementer confirms that reading in P0, under the same stop rule.

**W7 — no result recorded.** No comment, artifact or assertion literal states how many
closure-required members/groups any message has, or lists them (FR-005). **One narrow exception:**
the known-positive assertions. The committed, enabled ones below re-run on every invocation of their
binary, so they cannot rot silently:
- the deficient-frame assertions on 35=E (`NoOrders`(73), `ClOrdID`(11), `ListSeqNo`(67)) and on
  35=CC (`NoAsgnReqs`(1499), `NoPartyIDs`(453), `PartyID`(448), `PartyIDSource`(447), `NoRelatedSym`),
  CC-count2 included;
- the 35=i nested fail-loud;
- the synthetic dictionaries S1 and S2 and their expected statuses.

The selection check's BN-agrees and BM-differs assertions (research R-7) are also allowed, on a
narrower basis. `TailRows095Selection` is `DISABLED_` and needs `FIXPP_095_QFJ_DICT_DIR`, so they
run only when the check is invoked with the jar (verify, quickstart §3), never on a build or a CI
lane. A dictionary change that falsifies them shows up only at the next run of that check (FR-005).

The TU header records the rule and the recipe.

**W8 — existing coverage.** `test_077_allversions_builder_roundtrip.cpp` is byte-unchanged; its
tests still pass.
