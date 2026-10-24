# Contract — offline witness (FR-001..FR-006)

**W1 — one per MsgType.** Exactly one test per tail MsgType, named `TailRows095.<RowId>_<Name>`
(data-model §A; `<RowId>` without its hyphen). `TailRows095Census.EveryTailMsgTypeHasExactlyOneWitness`
iterates the registered tests of suite `TailRows095` through `::testing::UnitTest::GetInstance()`
(not the tests that ran), maps each `<Name>` to its MsgType through the dictionary's
`<message name=… msgtype=…>`, and requires multiplicity 1 for each MsgType of the TU's literal tail
table, no unmapped name and no extra MsgType (FR-004a). It is therefore independent of run order,
`--gtest_filter` and `--gtest_shuffle`. Each witness also asserts that its parsed tag 35 equals the
MsgType its own `current_test_info()` name maps to.

**W2 — build.** Args are populated from the literals of the witness's **intent list** (data-model §C)
and built with `fixpp::v50sp2::build_<Name>` into a caller buffer; the build MUST succeed. Every
closure-required member is set; every required group has ≥ 2 instances, and each instance sets the
group's delimiter (first) field and its required members; at least two fields in total.

**W3 — independent parse.** The body is framed with BeginString `FIX.5.0SP2` and parsed by
`fixpp::wire::Parser<Index>` over a `table_view` loaded from `FIX50SP2.xml` by `XmlLoader`. No
builder-side table is consulted.

**W4 — readback, both directions.** The witness enumerates every parsed body entry
(`OffsetTable::entries()`), removes from **both** sides the tags W3's framing writes and tag 35
(checked by W1), assigns each remaining entry to message level
or to the `group_slices(count_tag)` instance that contains it, and forms its path (`<tag>` or
`<count_tag>[<k>].<tag>`). It compares that multiset with the intent list exactly: a path missing
from the frame, a path in the frame that was not intended, and a value mismatch each fail, naming the
path. Count fields are intent entries. Instance order is checked by the per-instance literals
(`_i<k>`). It also asserts that the intent list holds at least two entries.

**W5 — required presence.** `required_members_missing(mv)` (data-model §B) takes the MsgType from the
parsed frame's tag 35, returns the closure-required members it did not find, and the witness asserts
that list is empty, printing it otherwise.

**W6 — non-vacuity (recorded at verify with command and output):**
- **field-leg golden:** for every tail MsgType and for 35=E, the walk's required message-level field
  set equals the row of `tools/quickfix_required_golden/golden.csv` (measured from real QuickFIX-cpp
  1.16.0), read from the file at test time and never copied into the TU;
- **deficient frames:** three 35=E frames (literal bytes or the typed builder) — one with a single
  `NoOrders` instance, one whose second instance lacks `ClOrdID`, one with no `NoOrders` — and
  `required_members_missing` reports exactly each frame's deficiency;
- **nested fail-loud:** the walk's status for 35=i (MassQuote) is the nested-required-group failure;
  the walk returns a status per MsgType, so this arm does not break `SetUpTestSuite` for the 23;
- **Mutant A** (in the A-027 TradingSessionList witness, delete the `trading_sessions` assignment
  **and** its intent entries) → RED in W5, with the recorded failure W5's message;
- **Mutant B** (in the A-018 witness, delete the `ExecAckStatus(1036)` assignment **and** its intent
  entry) → RED in W5, recorded likewise;
- **Mutant C** (in the walk, ignore the only-child inheritance) → the deficient-frame assertions on
  `NoOrders` go RED, and Mutant A, re-applied on top, no longer goes RED in W5 — the rule, not a
  list, decides;
- **Mutant D** (delete one intent entry, setter kept) → RED in W4 ("parsed, not intended");
- **Mutant E** (cut the BK witness to one field: delete the other setters **and** their intent
  entries) → RED in W4's size assertion, with the recorded failure that assertion's message;
- **Mutant F** (duplicate one witness under a second name) → RED in W1's census;
- a literal changed after the build in one witness → RED in W4.
Mutants are scratch edits, reverted; the reverted tree is diffed clean before commit.

**W7 — no result recorded.** No comment, artifact or assertion literal states how many
closure-required members/groups any message has, or lists them (FR-005). **One narrow exception:**
the W6 known-positive assertions on 35=E (`NoOrders`, `ClOrdID`) and on 35=i (the nested
fail-loud) — they re-run on every build, so they cannot rot silently. The TU header records the rule and the recipe.

**W8 — existing coverage.** `test_077_allversions_builder_roundtrip.cpp` is byte-unchanged; its
tests still pass.
