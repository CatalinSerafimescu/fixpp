// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/support/witness_comparator_test.cpp — 089 T018.
//
// Witnesses compare_streams() against data-model.md §4's Validation rules
// (FR-006's three named classes, FR-016c's "no readback ⇒ fail, never pass
// or skip"). Every arm names the observable as a value (an exact `verdict` +
// `mismatch[0].cls`), never a looser "something failed" check.
//
// Decimal-comparison arms (gate-b fix round) are keyed on the field's FIX
// TYPE, resolved through a REAL FIX 4.4 dictionary (test_resolver() below) —
// never on the value's spelling. "Does this value contain '.'" was tried and
// is wrong in BOTH directions: ClOrdID(11)/Text(58) are STRING fields whose
// values may legitimately contain '.', and PRICE(44) may legitimately have
// no fractional part at all.
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include "support/readback_jsonl.hpp"
#include "support/sent_record_intent.hpp"
#include "support/witness_comparator.hpp"

using namespace fixpp::interop::readback;

namespace {

WitnessIdentity test_identity()
{
    return WitnessIdentity{
        .run_id = "run-1",
        .combo_id = "C1",
        .cell_id = "cell-1",
        .config = "normal",
        .arm = "validation-on",
        .kind = "conformance",
        .authoritative = true,
    };
}

// Real FIX 4.4 dictionary, real fixpp::dict resolution — no hand-picked
// type list. Prefers FIXPP_FIX44_DICT_XML (the same env var the harness sets
// for the conversation gtests, data-model.md §1/R-4a) so a shim-driven run
// exercises the identical path; falls back to the tree-relative dictionary
// (FIXPP_DICT_DATA_DIR, a compile definition set below in CMakeLists.txt —
// same pattern as tests/dictionary/CMakeLists.txt) so this binary is
// self-contained under a plain, non-shim `ctest` run. Loaded once and
// reused across every TEST — the dictionary load is a config-time cost, not
// a per-comparison one ([const §XV.1]).
DecimalTagResolver const& test_resolver()
{
    static DecimalTagResolver const resolver = [] {
        std::string path;
        if (char const* env = std::getenv("FIXPP_FIX44_DICT_XML"); env != nullptr && env[0] != '\0') {
            path = env;
        } else {
#ifdef FIXPP_DICT_DATA_DIR
            path = std::string(FIXPP_DICT_DATA_DIR) + "/FIX44.xml";
#endif
        }
        return make_fix44_decimal_resolver(path);
    }();
    return resolver;
}

}  // namespace

TEST(WitnessComparator, ExactMatchPasses)
{
    std::string const dir = testing::TempDir();
    {
        Stream sender(dir + "wc_pass_sender.jsonl");
        sender.sent("D", 7, "fixpp-to-peer", 0, "B-01", {{"1", "ACCT0001"}, {"55", "AAPL"}});
    }
    {
        Stream receiver(dir + "wc_pass_receiver.jsonl");
        receiver.readback("D", 7, "fixpp-to-peer", 0, false, {{"1", "ACCT0001"}, {"55", "AAPL"}}, {});
    }
    auto const a = parse_stream(dir + "wc_pass_sender.jsonl");
    auto const b = parse_stream(dir + "wc_pass_receiver.jsonl");
    auto const rows = compare_streams(a, b, test_identity(), test_resolver());
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].verdict, "pass");
    EXPECT_TRUE(rows[0].mismatch.empty());
    // W-1: identity fields are stamped, not derived, and must survive.
    EXPECT_EQ(rows[0].run_id, "run-1");
    EXPECT_EQ(rows[0].combo_id, "C1");
    EXPECT_EQ(rows[0].cell_id, "cell-1");
    EXPECT_EQ(rows[0].config, "normal");
    EXPECT_EQ(rows[0].arm, "validation-on");
    EXPECT_EQ(rows[0].kind, "conformance");
    EXPECT_TRUE(rows[0].authoritative);
    EXPECT_EQ(rows[0].script_step_id, "B-01");
}

// FR-006 class 1: both declare the path; values differ.
TEST(WitnessComparator, ForcedValueMismatchNamesThePath)
{
    std::string const dir = testing::TempDir();
    {
        Stream sender(dir + "wc_vm_sender.jsonl");
        sender.sent("D", 7, "fixpp-to-peer", 0, "B-03", {{"1", "ACCT0001"}});
    }
    {
        // Forced miss: the peer reports a DIFFERENT value for the same path.
        Stream receiver(dir + "wc_vm_receiver.jsonl");
        receiver.readback("D", 7, "fixpp-to-peer", 0, false, {{"1", "ACCT0009"}}, {});
    }
    auto const a = parse_stream(dir + "wc_vm_sender.jsonl");
    auto const b = parse_stream(dir + "wc_vm_receiver.jsonl");
    auto const rows = compare_streams(a, b, test_identity(), test_resolver());
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].verdict, "fail");
    ASSERT_EQ(rows[0].mismatch.size(), 1u);
    EXPECT_EQ(rows[0].mismatch[0].cls, "value_mismatch");
    EXPECT_EQ(rows[0].mismatch[0].path, "1");
    EXPECT_EQ(rows[0].mismatch[0].sent_value, "ACCT0001");
    EXPECT_EQ(rows[0].mismatch[0].readback_value, "ACCT0009");
}

// FR-006 class 2: intent declares the path; the peer did not report it.
TEST(WitnessComparator, ForcedMissingNamesThePath)
{
    std::string const dir = testing::TempDir();
    {
        Stream sender(dir + "wc_missing_sender.jsonl");
        sender.sent("D", 7, "fixpp-to-peer", 0, "B-01", {{"1", "ACCT0001"}, {"55", "AAPL"}});
    }
    {
        // Forced miss: the peer never reports path "1" at all.
        Stream receiver(dir + "wc_missing_receiver.jsonl");
        receiver.readback("D", 7, "fixpp-to-peer", 0, false, {{"55", "AAPL"}}, {});
    }
    auto const a = parse_stream(dir + "wc_missing_sender.jsonl");
    auto const b = parse_stream(dir + "wc_missing_receiver.jsonl");
    auto const rows = compare_streams(a, b, test_identity(), test_resolver());
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].verdict, "fail");
    ASSERT_EQ(rows[0].mismatch.size(), 1u);
    EXPECT_EQ(rows[0].mismatch[0].cls, "missing");
    EXPECT_EQ(rows[0].mismatch[0].path, "1");
}

// FR-006 class 3: the peer reported a path the intent does not declare.
TEST(WitnessComparator, ForcedSpuriousNamesThePath)
{
    std::string const dir = testing::TempDir();
    {
        Stream sender(dir + "wc_spurious_sender.jsonl");
        sender.sent("D", 7, "fixpp-to-peer", 0, "B-01", {{"1", "ACCT0001"}});
    }
    {
        // Forced miss: the peer reports an extra field the sender never declared
        // -- exactly the "builder emitting a field it never intended" case
        // FR-006 requires exact-set (not subset) comparison to catch.
        Stream receiver(dir + "wc_spurious_receiver.jsonl");
        receiver.readback("D", 7, "fixpp-to-peer", 0, false, {{"1", "ACCT0001"}, {"11", "extra"}}, {});
    }
    auto const a = parse_stream(dir + "wc_spurious_sender.jsonl");
    auto const b = parse_stream(dir + "wc_spurious_receiver.jsonl");
    auto const rows = compare_streams(a, b, test_identity(), test_resolver());
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].verdict, "fail");
    ASSERT_EQ(rows[0].mismatch.size(), 1u);
    EXPECT_EQ(rows[0].mismatch[0].cls, "spurious");
    EXPECT_EQ(rows[0].mismatch[0].path, "11");
    EXPECT_EQ(rows[0].mismatch[0].readback_value, "extra");
}

// FR-016c: "No readback record ⇒ fail, never pass and never skip." The
// discriminating case: sent.fields is EMPTY, so a mis-implementation that
// modeled "absent readback" as "compare against an empty field set" would
// find zero mismatches and wrongly report pass. This is the forced arm named
// in the task brief -- it fails for the WRONG reason (a false pass) under
// exactly that mis-derivation, which is why the fields set is seeded empty
// rather than non-empty (a non-empty sent set would go "fail" either way and
// not discriminate the two implementations).
TEST(WitnessComparator, AbsentReadbackFailsEvenWithNoDeclaredFields)
{
    std::string const dir = testing::TempDir();
    {
        Stream sender(dir + "wc_absent_sender.jsonl");
        sender.sent("0", 3, "fixpp-to-peer", 0, "B-Heartbeat", {});
    }
    auto const a = parse_stream(dir + "wc_absent_sender.jsonl");
    std::vector<ParsedRecord> const no_readback_at_all;  // peer stream never wrote this key
    auto const rows = compare_streams(a, no_readback_at_all, test_identity(), test_resolver());
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].verdict, "fail");
    ASSERT_EQ(rows[0].mismatch.size(), 1u);
    EXPECT_EQ(rows[0].mismatch[0].cls, "missing");
    // Pins WHICH "missing" branch fired -- the "no readback record at all"
    // one (FR-016c), never the ∅-vs-∅ "empty intent vs empty readback" one
    // (FR-018 / T049, EmptyIntentVsEmptyReadbackRejectsRatherThanPassing
    // below), even though both classify "missing" and this record's
    // declared fields are also empty. The two are structurally disjoint: the
    // T049 branch only runs once a readback record has been FOUND at the
    // key, which never happens here.
    EXPECT_EQ(rows[0].mismatch[0].sent_value, "<record>");
}

// §4: "Value comparison is on decoded values, not rendered text — decimals
// compare numerically." Price(44) is dictionary-typed PRICE (Float category)
// -- trailing-zero spellings of the same decimal must not be reported as a
// value_mismatch.
TEST(WitnessComparator, DecimalValuesCompareNumerically)
{
    std::string const dir = testing::TempDir();
    {
        Stream sender(dir + "wc_decimal_sender.jsonl");
        sender.sent("D", 8, "fixpp-to-peer", 0, "B-02", {{"44", "190.500"}});
    }
    {
        Stream receiver(dir + "wc_decimal_receiver.jsonl");
        receiver.readback("D", 8, "fixpp-to-peer", 0, false, {{"44", "190.5"}}, {});
    }
    auto const a = parse_stream(dir + "wc_decimal_sender.jsonl");
    auto const b = parse_stream(dir + "wc_decimal_receiver.jsonl");
    auto const rows = compare_streams(a, b, test_identity(), test_resolver());
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].verdict, "pass");
}

// Account(1) is dictionary-typed STRING, not decimal -- a leading-zero
// spelling difference is a REAL value_mismatch, never a numeric collapse.
// This is the arm that was previously discriminating for the wrong reason
// (a "no '.' present" heuristic, which happens to agree with the dictionary
// here but disagrees on ClOrdID/Text below) -- now discriminating because
// Account genuinely is not a decimal type.
TEST(WitnessComparator, AccountStringDoesNotCollapseOnLeadingZero)
{
    std::string const dir = testing::TempDir();
    {
        Stream sender(dir + "wc_int_sender.jsonl");
        sender.sent("D", 9, "fixpp-to-peer", 0, "B-03", {{"1", "0100"}});
    }
    {
        Stream receiver(dir + "wc_int_receiver.jsonl");
        receiver.readback("D", 9, "fixpp-to-peer", 0, false, {{"1", "100"}}, {});
    }
    auto const a = parse_stream(dir + "wc_int_sender.jsonl");
    auto const b = parse_stream(dir + "wc_int_receiver.jsonl");
    auto const rows = compare_streams(a, b, test_identity(), test_resolver());
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].verdict, "fail");
    ASSERT_EQ(rows[0].mismatch.size(), 1u);
    EXPECT_EQ(rows[0].mismatch[0].cls, "value_mismatch");
}

// QTY(38, OrderQty) IS dictionary-typed decimal (Float category) -- a
// leading-zero spelling of the SAME quantity must pass. Sibling/contrast of
// AccountStringDoesNotCollapseOnLeadingZero directly above: same input
// shape ("0100" vs "100"), opposite verdict, because the TYPE differs.
TEST(WitnessComparator, QtyLeadingZeroPasses)
{
    std::string const dir = testing::TempDir();
    {
        Stream sender(dir + "wc_qty_sender.jsonl");
        sender.sent("D", 11, "fixpp-to-peer", 0, "B-06", {{"38", "0100"}});
    }
    {
        Stream receiver(dir + "wc_qty_receiver.jsonl");
        receiver.readback("D", 11, "fixpp-to-peer", 0, false, {{"38", "100"}}, {});
    }
    auto const a = parse_stream(dir + "wc_qty_sender.jsonl");
    auto const b = parse_stream(dir + "wc_qty_receiver.jsonl");
    auto const rows = compare_streams(a, b, test_identity(), test_resolver());
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].verdict, "pass");
}

// PRICE(44) with vs without a fractional part -- "190" and "190.0" are the
// SAME decimal; a spelling-only rule requiring a '.' on both sides (the
// pre-fix heuristic) would have reported this as a value_mismatch.
TEST(WitnessComparator, PriceWithAndWithoutFractionalZeroPasses)
{
    std::string const dir = testing::TempDir();
    {
        Stream sender(dir + "wc_price_int_sender.jsonl");
        sender.sent("D", 12, "fixpp-to-peer", 0, "B-07", {{"44", "190"}});
    }
    {
        Stream receiver(dir + "wc_price_int_receiver.jsonl");
        receiver.readback("D", 12, "fixpp-to-peer", 0, false, {{"44", "190.0"}}, {});
    }
    auto const a = parse_stream(dir + "wc_price_int_sender.jsonl");
    auto const b = parse_stream(dir + "wc_price_int_receiver.jsonl");
    auto const rows = compare_streams(a, b, test_identity(), test_resolver());
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].verdict, "pass");
}

// PRICE(44), genuinely different values -- the decimal fallback must still
// be exact comparison, not a trapdoor that makes every PRICE pass.
TEST(WitnessComparator, PriceDifferingValuesMismatch)
{
    std::string const dir = testing::TempDir();
    {
        Stream sender(dir + "wc_price_diff_sender.jsonl");
        sender.sent("D", 13, "fixpp-to-peer", 0, "B-08", {{"44", "190.5"}});
    }
    {
        Stream receiver(dir + "wc_price_diff_receiver.jsonl");
        receiver.readback("D", 13, "fixpp-to-peer", 0, false, {{"44", "190.6"}}, {});
    }
    auto const a = parse_stream(dir + "wc_price_diff_sender.jsonl");
    auto const b = parse_stream(dir + "wc_price_diff_receiver.jsonl");
    auto const rows = compare_streams(a, b, test_identity(), test_resolver());
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].verdict, "fail");
    ASSERT_EQ(rows[0].mismatch.size(), 1u);
    EXPECT_EQ(rows[0].mismatch[0].cls, "value_mismatch");
}

// PRICE(44), exact vs a value differing only PAST the decimal fallback's
// exactness -- proves the fallback is EXACT decimal comparison, never a
// float cast (which would round "190.50000000000001" to the same double as
// "190.5" on any IEEE-754 double and wrongly pass).
TEST(WitnessComparator, PriceNearMissDoesNotCollapseViaFloatRounding)
{
    std::string const dir = testing::TempDir();
    {
        Stream sender(dir + "wc_price_nearmiss_sender.jsonl");
        sender.sent("D", 14, "fixpp-to-peer", 0, "B-09", {{"44", "190.5"}});
    }
    {
        Stream receiver(dir + "wc_price_nearmiss_receiver.jsonl");
        receiver.readback("D", 14, "fixpp-to-peer", 0, false, {{"44", "190.50000000000001"}}, {});
    }
    auto const a = parse_stream(dir + "wc_price_nearmiss_sender.jsonl");
    auto const b = parse_stream(dir + "wc_price_nearmiss_receiver.jsonl");
    auto const rows = compare_streams(a, b, test_identity(), test_resolver());
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].verdict, "fail");
    ASSERT_EQ(rows[0].mismatch.size(), 1u);
    EXPECT_EQ(rows[0].mismatch[0].cls, "value_mismatch");
}

// ClOrdID(11) is dictionary-typed STRING. This is the SPURIOUS-HIT arm the
// spelling-based heuristic ("both sides contain '.'") got wrong: "ORD.10"
// and "ORD.1" normalize to the identical trailing-zero-stripped spelling
// ("ORD.1") and would have compared EQUAL under that heuristic -- a false
// PASS FR-006's exact-set equality must not produce.
TEST(WitnessComparator, ClOrdIdWithDotIsNotTreatedAsDecimal)
{
    std::string const dir = testing::TempDir();
    {
        Stream sender(dir + "wc_clordid_sender.jsonl");
        sender.sent("D", 15, "fixpp-to-peer", 0, "B-10", {{"11", "ORD.10"}});
    }
    {
        Stream receiver(dir + "wc_clordid_receiver.jsonl");
        receiver.readback("D", 15, "fixpp-to-peer", 0, false, {{"11", "ORD.1"}}, {});
    }
    auto const a = parse_stream(dir + "wc_clordid_sender.jsonl");
    auto const b = parse_stream(dir + "wc_clordid_receiver.jsonl");
    auto const rows = compare_streams(a, b, test_identity(), test_resolver());
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].verdict, "fail");
    ASSERT_EQ(rows[0].mismatch.size(), 1u);
    EXPECT_EQ(rows[0].mismatch[0].cls, "value_mismatch");
    EXPECT_EQ(rows[0].mismatch[0].path, "11");
}

// Text(58) is dictionary-typed STRING -- the sibling spurious-hit case with
// a value that looks even MORE like a decimal ("1.50" vs "1.5").
TEST(WitnessComparator, TextWithDotIsNotTreatedAsDecimal)
{
    std::string const dir = testing::TempDir();
    {
        Stream sender(dir + "wc_text_sender.jsonl");
        sender.sent("D", 16, "fixpp-to-peer", 0, "B-11", {{"58", "1.50"}});
    }
    {
        Stream receiver(dir + "wc_text_receiver.jsonl");
        receiver.readback("D", 16, "fixpp-to-peer", 0, false, {{"58", "1.5"}}, {});
    }
    auto const a = parse_stream(dir + "wc_text_sender.jsonl");
    auto const b = parse_stream(dir + "wc_text_receiver.jsonl");
    auto const rows = compare_streams(a, b, test_identity(), test_resolver());
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].verdict, "fail");
    ASSERT_EQ(rows[0].mismatch.size(), 1u);
    EXPECT_EQ(rows[0].mismatch[0].cls, "value_mismatch");
    EXPECT_EQ(rows[0].mismatch[0].path, "58");
}

// MUTATION PROOF: the decimal fallback is TYPE-GATED, not free-standing. A
// resolver that (wrongly) reports every tag decimal reproduces the exact
// spurious-hit ClOrdIdWithDotIsNotTreatedAsDecimal exists to kill -- "ORD.10"
// and "ORD.1" now WRONGLY compare equal. Contrast this row's verdict
// ("pass") against that test's ("fail", same input shape) to see the
// resolver argument is load-bearing, not decorative: deleting the
// `is_decimal_tag(leaf_tag(fe.path))` call in compare_streams (hardcoding
// `true`) collapses BOTH tests to this row's outcome, which is what makes
// ClOrdIdWithDotIsNotTreatedAsDecimal itself the discriminating regression
// guard for that mutation.
TEST(WitnessComparator, MutationProofAlwaysDecimalResolverWronglyPassesClOrdId)
{
    std::string const dir = testing::TempDir();
    {
        Stream sender(dir + "wc_mutant_sender.jsonl");
        sender.sent("D", 15, "fixpp-to-peer", 0, "B-10", {{"11", "ORD.10"}});
    }
    {
        Stream receiver(dir + "wc_mutant_receiver.jsonl");
        receiver.readback("D", 15, "fixpp-to-peer", 0, false, {{"11", "ORD.1"}}, {});
    }
    auto const a = parse_stream(dir + "wc_mutant_sender.jsonl");
    auto const b = parse_stream(dir + "wc_mutant_receiver.jsonl");
    DecimalTagResolver const always_decimal = [](std::uint16_t) { return true; };
    auto const rows = compare_streams(a, b, test_identity(), always_decimal);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].verdict, "pass") << "mutant resolver should wrongly collapse ORD.10/ORD.1 -- if "
                                           "this is 'fail', compare_streams stopped consulting the "
                                           "resolver at all (a DIFFERENT regression)";
    EXPECT_TRUE(rows[0].mismatch.empty());
}

// A sent `value` and a readback `value_b64` over the SAME raw bytes must
// compare equal -- both are decoded to raw bytes before comparison, never at
// the JSON-key level (§4's "decoded values" rule, generalized past decimals).
TEST(WitnessComparator, ValueAndValueB64OverSameBytesCompareEqual)
{
    std::string const dir = testing::TempDir();
    std::string const raw_bytes(1, static_cast<char>(0xff));
    {
        // "\xff" alone is not valid UTF-8 -> this Stream also writes it as
        // value_b64, but the point under test is the COMPARATOR's byte-level
        // normalization, not the writer's classification -- so both sides
        // here happen to render value_b64, proving the decode-then-compare
        // path independent of which key either side chose.
        Stream sender(dir + "wc_b64_sender.jsonl");
        sender.sent("D", 10, "fixpp-to-peer", 0, "B-05", {{"355", raw_bytes}});
    }
    {
        Stream receiver(dir + "wc_b64_receiver.jsonl");
        receiver.readback("D", 10, "fixpp-to-peer", 0, false, {{"355", raw_bytes}}, {});
    }
    auto const a = parse_stream(dir + "wc_b64_sender.jsonl");
    auto const b = parse_stream(dir + "wc_b64_receiver.jsonl");
    auto const rows = compare_streams(a, b, test_identity(), test_resolver());
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].verdict, "pass");
}

// ── write_witness_rows / parse_witness_rows — the comparator→promotion
// hand-off (data-model.md §4, pinned `a7923892`) ────────────────────────────

// Round-trips BOTH shapes compare_streams() can produce: a pass row (empty
// `mismatch`) and a fail row (non-empty `mismatch`, exercising
// render_mismatch_array's/parse_mismatch_array's path/cls/sent_value/
// readback_value fields). Every W-1 field is asserted individually, not via
// a struct-level operator== (WitnessRow declares none), so a field silently
// dropped by either direction shows up by name, not as an opaque `false`.
TEST(WitnessComparator, WitnessRowsRoundTripThroughFile)
{
    std::string const dir = testing::TempDir();
    {
        Stream sender(dir + "wc_rt_sender.jsonl");
        sender.sent("D", 20, "fixpp-to-peer", 0, "B-20", {{"1", "ACCT0001"}});
        sender.sent("D", 21, "fixpp-to-peer", 0, "B-21", {{"1", "ACCT0002"}});
    }
    {
        Stream receiver(dir + "wc_rt_receiver.jsonl");
        receiver.readback("D", 20, "fixpp-to-peer", 0, false, {{"1", "ACCT0001"}}, {});  // pass
        receiver.readback("D", 21, "fixpp-to-peer", 0, false, {{"1", "ACCT0009"}}, {});  // fail
    }
    auto const a = parse_stream(dir + "wc_rt_sender.jsonl");
    auto const b = parse_stream(dir + "wc_rt_receiver.jsonl");
    auto const original = compare_streams(a, b, test_identity(), test_resolver());
    ASSERT_EQ(original.size(), 2u);

    std::string const witnesses_path = dir + "wc_rt_witnesses.jsonl";
    ASSERT_TRUE(write_witness_rows(witnesses_path, original));
    auto const round_tripped = parse_witness_rows(witnesses_path);
    ASSERT_EQ(round_tripped.size(), original.size());

    for (std::size_t i = 0; i < original.size(); ++i) {
        WitnessRow const& want = original[i];
        WitnessRow const& got = round_tripped[i];
        EXPECT_EQ(got.witness_id, want.witness_id) << "row " << i;
        EXPECT_EQ(got.run_id, want.run_id) << "row " << i;
        EXPECT_EQ(got.authoritative, want.authoritative) << "row " << i;
        EXPECT_EQ(got.combo_id, want.combo_id) << "row " << i;
        EXPECT_EQ(got.cell_id, want.cell_id) << "row " << i;
        EXPECT_EQ(got.config, want.config) << "row " << i;
        EXPECT_EQ(got.arm, want.arm) << "row " << i;
        EXPECT_EQ(got.kind, want.kind) << "row " << i;
        EXPECT_EQ(got.script_step_id, want.script_step_id) << "row " << i;
        EXPECT_EQ(got.msg_type, want.msg_type) << "row " << i;
        EXPECT_EQ(got.direction, want.direction) << "row " << i;
        EXPECT_EQ(got.occurrence, want.occurrence) << "row " << i;
        EXPECT_EQ(got.verdict, want.verdict) << "row " << i;
        ASSERT_EQ(got.mismatch.size(), want.mismatch.size()) << "row " << i;
        for (std::size_t m = 0; m < want.mismatch.size(); ++m) {
            EXPECT_EQ(got.mismatch[m].path, want.mismatch[m].path) << "row " << i << " mismatch " << m;
            EXPECT_EQ(got.mismatch[m].cls, want.mismatch[m].cls) << "row " << i << " mismatch " << m;
            EXPECT_EQ(got.mismatch[m].sent_value, want.mismatch[m].sent_value)
                << "row " << i << " mismatch " << m;
            EXPECT_EQ(got.mismatch[m].readback_value, want.mismatch[m].readback_value)
                << "row " << i << " mismatch " << m;
        }
    }
    // Confirms the two rows really did cover both shapes -- if compare_streams
    // ever stopped producing a fail row for this input, the loop above would
    // still pass on an all-empty-mismatch round trip and the arm would be
    // silently vacuous.
    EXPECT_EQ(original[0].verdict, "pass");
    EXPECT_EQ(original[1].verdict, "fail");
    ASSERT_EQ(original[1].mismatch.size(), 1u);
}

// FORCED MISS: a row on disk missing a required §4 field (here `kind`,
// exactly the field witness-evidence.md itself names as the one whose
// silent absence "drops [a row] from the population it should have joined")
// must fail the round-trip with a diagnostic naming the field -- not
// silently parse into a WitnessRow with kind=="".
TEST(WitnessComparator, ParseWitnessRowsThrowsOnMissingRequiredField)
{
    std::string const path = testing::TempDir() + "wc_missing_kind_witnesses.jsonl";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        // Byte-for-byte a well-formed row per write_witness_rows()'s own
        // shape, with the "kind" key deliberately removed.
        out << "{\"witness_id\":\"cell-1:B-20:fixpp-to-peer:0\",\"run_id\":\"run-1\","
               "\"authoritative\":true,\"combo_id\":\"C1\",\"cell_id\":\"cell-1\","
               "\"config\":\"normal\",\"arm\":\"validation-on\","
               "\"script_step_id\":\"B-20\",\"msg_type\":\"D\",\"direction\":\"fixpp-to-peer\","
               "\"occurrence\":0,\"verdict\":\"pass\",\"mismatch\":[]}\n";
    }
    bool threw = false;
    try {
        (void)parse_witness_rows(path);
    } catch (std::runtime_error const& e) {
        threw = true;
        std::string const what = e.what();
        EXPECT_NE(what.find("kind"), std::string::npos) << "diagnostic did not name the missing field: "
                                                          << what;
    }
    EXPECT_TRUE(threw) << "a witness row missing a required §4 field must throw, not default it";
}

// ── 089 T037 — direction is part of the join key, not decorative ───────────
// spec.md § "Conversation census": direction values are ABSOLUTE wire
// strings, identical from every emitter. If `Key` ever stopped comparing
// `direction`, a readback recorded under the WRONG direction would silently
// satisfy a sent record it does not correspond to. This arm pins the
// opposite: a readback at the correct (seq_num, occurrence) but the WRONG
// direction spelling must NOT be found -- the sent record reports `missing`,
// not `pass`.
TEST(WitnessComparator, DirectionIsPartOfTheJoinKeyNotDecorative)
{
    std::string const dir = testing::TempDir();
    {
        Stream sender(dir + "wc_dir_sender.jsonl");
        sender.sent("D", 40, kDirectionFixppToPeer, 0, "T037-dir", {{"1", "ACCT0001"}});
    }
    {
        // Same seq_num/occurrence, WRONG direction spelling.
        Stream receiver(dir + "wc_dir_receiver.jsonl");
        receiver.readback("D", 40, kDirectionPeerToFixpp, 0, false, {{"1", "ACCT0001"}}, {});
    }
    auto const a = parse_stream(dir + "wc_dir_sender.jsonl");
    auto const b = parse_stream(dir + "wc_dir_receiver.jsonl");
    auto const rows = compare_streams(a, b, test_identity(), test_resolver());
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].verdict, "fail")
        << "a readback recorded under the wrong direction must not satisfy the sent record -- "
           "direction is part of the join key";
    ASSERT_EQ(rows[0].mismatch.size(), 1u);
    EXPECT_EQ(rows[0].mismatch[0].cls, "missing");
}

// ── 089 T047 — TRUNCATE mode is load-bearing (R-4) ──────────────────────────
// Two Stream opens on the SAME path -- representing run n-1 then run n
// reusing a run directory's file -- must not let run n-1's stale readback
// satisfy run n's sent record. Pinned via the REAL open()/write path
// (Stream's mandated single-arg constructor, always TRUNCATE), not a
// hand-written appended fixture.
TEST(WitnessComparator, ReopeningOnSamePathTruncatesRunNMinus1sStaleReadback)
{
    std::string const dir = testing::TempDir();
    std::string const receiver_path = dir + "wc_r4_receiver.jsonl";
    // Run n-1: peer answers seq_num=50 with "ACCT0001".
    {
        Stream receiver(receiver_path);
        receiver.readback("D", 50, kDirectionFixppToPeer, 0, false, {{"1", "ACCT0001"}}, {});
    }
    // Run n reuses the SAME path (a fresh process, same run directory) but
    // the peer never answers this time -- no readback() call at all. The
    // mandated single-arg constructor truncates on reopen.
    {
        Stream receiver(receiver_path);
        (void)receiver;
    }
    std::string const sender_path = dir + "wc_r4_sender.jsonl";
    {
        Stream sender(sender_path);
        sender.sent("D", 50, kDirectionFixppToPeer, 0, "T047-run-n", {{"1", "ACCT0001"}});
    }
    auto const a = parse_stream(sender_path);
    auto const b = parse_stream(receiver_path);
    auto const rows = compare_streams(a, b, test_identity(), test_resolver());
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].verdict, "fail")
        << "run n received no readback -- TRUNCATE must have wiped run n-1's stale record, or "
           "this would wrongly pass";
    ASSERT_EQ(rows[0].mismatch.size(), 1u);
    EXPECT_EQ(rows[0].mismatch[0].cls, "missing");
}

// THE DANGER THIS GUARDS AGAINST -- the flipped-mode mutant of the test
// above, using append_mode_for_test() (089 T047: the SAME real open()/write
// path, mode APPEND instead of the mandated TRUNCATE). Run n-1's stale
// "ACCT0001" readback survives and wrongly satisfies run n's sent record,
// which never received a real reply: the test above's RED (fail/missing)
// turns GREEN (pass) under this mutant. Kept as a permanent, LABELLED
// expected-priced-survivor witness proving the hazard by contrast with the
// test above -- production never reaches this mode (Stream's single-arg
// constructor never opens append; append_mode_for_test() is gated behind
// FIXPP_TEST_HOOKS and reachable only from test code).
TEST(WitnessComparator, AppendModeAcrossTwoRunsWronglyPassesOnRunNMinus1sStaleReadback)
{
    std::string const dir = testing::TempDir();
    std::string const receiver_path = dir + "wc_r4_append_receiver.jsonl";
    {
        Stream receiver(receiver_path);
        receiver.readback("D", 51, kDirectionFixppToPeer, 0, false, {{"1", "ACCT0001"}}, {});
    }
    {
        // The mode a defect would flip -- real open() path, real write path,
        // APPEND instead of TRUNCATE. Writes nothing further (the peer
        // still never answers run n).
        auto receiver = Stream::append_mode_for_test(receiver_path);
        (void)receiver;
    }
    std::string const sender_path = dir + "wc_r4_append_sender.jsonl";
    {
        Stream sender(sender_path);
        sender.sent("D", 51, kDirectionFixppToPeer, 0, "T047-append-mutant", {{"1", "ACCT0001"}});
    }
    auto const a = parse_stream(sender_path);
    auto const b = parse_stream(receiver_path);
    auto const rows = compare_streams(a, b, test_identity(), test_resolver());
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].verdict, "pass")
        << "append mode let run n-1's stale readback satisfy run n's sent record -- this IS the "
           "hazard R-4's mandated truncate mode prevents, not a correct outcome";
    EXPECT_TRUE(rows[0].mismatch.empty());
}

// ── 089 T048 — C-8 spurious-hit: sent.fields MUST be builder-derived ───────
// Exercises the PRODUCTION functions in sent_record_intent.hpp (built on
// fixpp::wire::body_builder, the shipped 061 body-only serializer), not a
// test-local re-implementation -- see that file's header for why they had
// to be added: no such seam existed anywhere in the tree before this arm.
TEST(WitnessComparator, SpuriousHitFrameDerivedSentMasksPostCaptureMutation_C8)
{
    std::string const cl_ord_id = "ORD0001";
    std::string const orig_cl_ord_id = "ORIG0001";
    std::string const account = "ACCT0001";

    // The REAL production write path.
    std::array<std::byte, 256> buf{};
    auto built = build_order_cancel_request(buf, cl_ord_id, orig_cl_ord_id, account);
    ASSERT_TRUE(built.has_value());
    std::span<std::byte> const frame = *built;

    // Stage-1 intent capture -- BEFORE the frame is mutated (C-8: "reading
    // MsgSeqNum(34) and direction from the outbound seam is required... the
    // restriction is on WHAT is read there, not on WHERE").
    auto const intent_fields =
        order_cancel_request_sent_fields_from_intent(cl_ord_id, orig_cl_ord_id, account);

    // Test-only hook: rewrite Account(1) in the ALREADY-SERIALIZED frame,
    // AFTER intent capture -- same length ("ACCT0001" -> "ACCT0009"),
    // nothing else touched. quickstart.md's C-8 arm names B-03/Account(1) on
    // the live conversation script; that script has no support-layer
    // equivalent yet (T052), so this uses the same message shape
    // (OrderCancelRequest/Account(1)) built directly here.
    {
        std::string_view const view(reinterpret_cast<char const*>(frame.data()), frame.size());
        std::size_t const pos = view.find("1=ACCT0001\x01");
        ASSERT_NE(pos, std::string_view::npos) << "mutation anchor not found in the built frame";
        frame[pos + std::string_view("1=ACCT000").size()] = std::byte{'9'};  // trailing '1' -> '9'
    }

    // The "peer's readback" always reflects the wire bytes actually
    // received -- the MUTATED frame -- independent of which sent-derivation
    // is under test.
    auto const readback_fields = order_cancel_request_sent_fields_from_frame(frame);
    ASSERT_EQ(readback_fields.size(), 3u);

    std::string const dir = testing::TempDir();

    // -- half 1: BUILDER-DERIVED sent (the correct, mandated derivation) --
    {
        Stream sender(dir + "c8_builder_sender.jsonl");
        sender.sent("F", 60, kDirectionFixppToPeer, 0, "T048-builder", intent_fields);
    }
    {
        Stream receiver(dir + "c8_builder_receiver.jsonl");
        receiver.readback("F", 60, kDirectionFixppToPeer, 0, false, readback_fields, {});
    }
    auto const rows1 = compare_streams(parse_stream(dir + "c8_builder_sender.jsonl"),
                                        parse_stream(dir + "c8_builder_receiver.jsonl"),
                                        test_identity(), test_resolver());
    ASSERT_EQ(rows1.size(), 1u);
    EXPECT_EQ(rows1[0].verdict, "fail") << "builder-derived sent must catch the post-capture mutation";
    ASSERT_EQ(rows1[0].mismatch.size(), 1u);
    EXPECT_EQ(rows1[0].mismatch[0].cls, "value_mismatch");
    EXPECT_EQ(rows1[0].mismatch[0].path, "1");
    EXPECT_EQ(rows1[0].mismatch[0].sent_value, "ACCT0001");
    EXPECT_EQ(rows1[0].mismatch[0].readback_value, "ACCT0009");

    // -- half 2: FRAME-DERIVED sent (the MIRROR MUTANT C-8 forbids) --
    // Reads the SAME mutated bytes the readback is derived from, so the two
    // trivially agree -- this is what makes half 2 discriminating: it
    // measures the writer against its OWN reader, not against reality. Both
    // halves' readback record is produced by the SAME frame parser (the
    // support layer has no independent decoder), so this proves
    // intent-vs-frame provenance, not reader independence.
    auto const frame_derived_sent = order_cancel_request_sent_fields_from_frame(frame);
    {
        Stream sender2(dir + "c8_frame_sender.jsonl");
        sender2.sent("F", 61, kDirectionFixppToPeer, 0, "T048-frame-mutant", frame_derived_sent);
    }
    {
        Stream receiver2(dir + "c8_frame_receiver.jsonl");
        receiver2.readback("F", 61, kDirectionFixppToPeer, 0, false, readback_fields, {});
    }
    auto const rows2 = compare_streams(parse_stream(dir + "c8_frame_sender.jsonl"),
                                        parse_stream(dir + "c8_frame_receiver.jsonl"),
                                        test_identity(), test_resolver());
    ASSERT_EQ(rows2.size(), 1u);
    EXPECT_EQ(rows2[0].verdict, "pass")
        << "frame-derived sent wrongly masks the mutation it should have caught -- the exact "
           "violation C-8 forbids";
    EXPECT_TRUE(rows2[0].mismatch.empty());
}

// ── 089 T049 — ∅ intent vs ∅ readback must reject, not pass ────────────────
// spec.md:954: "Emit a message whose declared intent set is empty -- the
// comparator must reject rather than pass on ∅ == ∅." Distinct from
// AbsentReadbackFailsEvenWithNoDeclaredFields (T042) above: THAT arm's
// readback record does not exist at all. THIS arm's readback record EXISTS,
// at the correct key, and ALSO declares zero fields -- the exact case a
// mis-implementation modeling "compare field sets" as "find zero mismatches"
// would wrongly accept, since an empty set trivially equals an empty set.
TEST(WitnessComparator, EmptyIntentVsEmptyReadbackRejectsRatherThanPassing)
{
    std::string const dir = testing::TempDir();
    {
        Stream sender(dir + "wc_empty_intent_sender.jsonl");
        sender.sent("0", 4, kDirectionFixppToPeer, 0, "T049-empty", {});
    }
    {
        Stream receiver(dir + "wc_empty_intent_receiver.jsonl");
        receiver.readback("0", 4, kDirectionFixppToPeer, 0, false, {}, {});
    }
    auto const a = parse_stream(dir + "wc_empty_intent_sender.jsonl");
    auto const b = parse_stream(dir + "wc_empty_intent_receiver.jsonl");
    auto const rows = compare_streams(a, b, test_identity(), test_resolver());
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].verdict, "fail")
        << "an empty declared intent set must never pass, even when the readback also reports "
           "zero fields -- ∅ == ∅ is not proof of fidelity";
    ASSERT_EQ(rows[0].mismatch.size(), 1u);
    EXPECT_EQ(rows[0].mismatch[0].cls, "missing");
    // Distinguishes this branch's reject from AbsentReadbackFailsEvenWithNoDeclaredFields's --
    // both use cls=="missing" (FR-006's three-class vocabulary), but this one's readback
    // record genuinely exists at the correct key.
    EXPECT_EQ(rows[0].mismatch[0].sent_value, "<empty intent vs empty readback>");
}
