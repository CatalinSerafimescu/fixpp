# Contract — offline witness (FR-001..FR-006)

**W1 — one per MsgType.** Exactly one test per tail MsgType, named `TailRows095.<Row>_<Name>`
(data-model §A), and `TailRows095Census.EveryTailMsgTypeHasAWitness` holds set-equality between the
literal tail set and the witnesses that ran.

**W2 — build.** Args are populated from literals (data-model §C) and built with
`fixpp::v50sp2::build_<Name>` into a caller buffer; the build MUST succeed. Every required field and
every required group (≥ 2 instances, each with its required members, recursively) is set; at least
two fields in total.

**W3 — independent parse.** The body is framed with BeginString `FIX.5.0SP2` and parsed by
`fixpp::wire::Parser<Index>` over a `table_view` loaded from `FIX50SP2.xml` by `XmlLoader`. No
builder-side table is consulted.

**W4 — readback.** Every set message-level field is compared to its literal; every set group's count
field equals the instance count; every set instance field is compared to its per-instance literal,
instance order preserved.

**W5 — required presence.** `expect_required_members_present(msg_type, mv)` (data-model §B) runs on
the parsed frame and passes.

**W6 — non-vacuity (recorded at verify with command and output):**
- known positive: the walk yields `NoOrders` + `ClOrdID` for 35=E;
- Mutant A (drop a required group from one witness) → RED in W5;
- Mutant B (drop a required scalar from one witness) → RED in W5;
- Mutant C (disable only-child inheritance in the walk) → known positive RED;
- a literal changed after the build in one witness → RED in W4.
Mutants are scratch edits, reverted; the reverted tree is diffed clean before commit.

**W7 — no result recorded.** No comment, artifact or assertion literal states how many required
members/groups any message has (FR-005). The TU header records the rule and the recipe.

**W8 — existing coverage.** `test_077_allversions_builder_roundtrip.cpp` is byte-unchanged; its
tests still pass.
