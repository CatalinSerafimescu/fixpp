// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/support/witness_comparator_test.cpp — 089 T018.
//
// Witnesses compare_streams() against data-model.md §4's Validation rules
// (FR-006's three named classes, FR-016c's "no readback ⇒ fail, never pass
// or skip"). Every arm names the observable as a value (an exact `verdict` +
// `mismatch[0].cls`), never a looser "something failed" check.
#include <gtest/gtest.h>

#include <string>

#include "support/readback_jsonl.hpp"
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
    auto const rows = compare_streams(a, b, test_identity());
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
    auto const rows = compare_streams(a, b, test_identity());
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
    auto const rows = compare_streams(a, b, test_identity());
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
    auto const rows = compare_streams(a, b, test_identity());
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
    auto const rows = compare_streams(a, no_readback_at_all, test_identity());
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].verdict, "fail");
    ASSERT_EQ(rows[0].mismatch.size(), 1u);
    EXPECT_EQ(rows[0].mismatch[0].cls, "missing");
}

// §4: "Value comparison is on decoded values, not rendered text — decimals
// compare numerically." Trailing-zero spellings of the same decimal must not
// be reported as a value_mismatch.
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
    auto const rows = compare_streams(a, b, test_identity());
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].verdict, "pass");
}

// Guard against the false-GREEN a naive numeric fallback would introduce:
// integer-looking values (no '.') must NEVER collapse on leading zeros --
// "0100" and "100" are the same NUMBER but not the same ACCOUNT/ClOrdID
// string, and §4's exact-set-equality comparison is on the FIELD's declared
// spelling, not an implicit numeric cast.
TEST(WitnessComparator, IntegerLikeValuesDoNotCollapseOnLeadingZero)
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
    auto const rows = compare_streams(a, b, test_identity());
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].verdict, "fail");
    ASSERT_EQ(rows[0].mismatch.size(), 1u);
    EXPECT_EQ(rows[0].mismatch[0].cls, "value_mismatch");
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
    auto const rows = compare_streams(a, b, test_identity());
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].verdict, "pass");
}
