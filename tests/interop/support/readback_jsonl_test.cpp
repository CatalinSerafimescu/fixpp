// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/support/readback_jsonl_test.cpp — 089 T018.
//
// Witnesses readback_jsonl.hpp's Stream against
// contracts/readback-jsonl.md's own clauses. Three rules from
// quickstart.md § Step 4 bind every arm here: name the observable as a
// value, assert your own diagnostic, and confirm the arm runs against the
// UNMUTATED tree first (every EXPECT_EQ below asserts the exact byte string
// the contract's canonical form produces, so a regression that breaks that
// form goes RED by construction — the same property the README's
// delete-the-sort-line recipe checks manually for the cross-language
// fixture).
#include <gtest/gtest.h>

#include <fstream>
#include <string>
#include <vector>

#include "support/readback_jsonl.hpp"

using namespace fixpp::interop::readback;

namespace {

// Reads back a just-written stream file as raw text (one string per line is
// unnecessary here — every test writes exactly one record after the hello).
std::vector<std::string> read_lines(std::string const& path)
{
    std::ifstream in(path, std::ios::binary);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    return lines;
}

}  // namespace

// ── spec.md § "Conversation census" — direction wire values (089 T037) ─────
// "these two strings are the enum's wire values... The value is ABSOLUTE."
// Pinned so a typo in either constant (wrong casing, an arrow, an
// underscore) fails HERE rather than surfacing later as a silent join-key
// mismatch at a live cell.

TEST(ReadbackJsonl, DirectionWireValuesMatchTheCensusExactly)
{
    EXPECT_STREQ(kDirectionFixppToPeer, "fixpp-to-peer");
    EXPECT_STREQ(kDirectionPeerToFixpp, "peer-to-fixpp");
}

// ── contract § Escaping and encoding — witness required, per emitter ───────

TEST(ReadbackJsonl, EscapesQuoteBackslashAndControlByte)
{
    std::string const path = testing::TempDir() + "escape_basic.jsonl";
    {
        Stream s(path);
        ASSERT_TRUE(s.ok());
        s.readback("D", 1, "fixpp-to-peer", 0, false,
                   {{"58", std::string("quote\" back\\slash \x01 ctrl")}}, {});
    }
    auto const lines = read_lines(path);
    ASSERT_EQ(lines.size(), 1u);
    // `"` -> \", `\` -> \\, byte < 0x20 -> \u00xx LOWER hex — no other escape
    // spelling is permitted (contract table: "No optional escaping").
    EXPECT_NE(lines[0].find("\"value\":\"quote\\\" back\\\\slash \\u0001 ctrl\""), std::string::npos)
        << lines[0];
}

TEST(ReadbackJsonl, MultiByteUtf8IsEmittedLiterally)
{
    std::string const path = testing::TempDir() + "escape_utf8.jsonl";
    {
        Stream s(path);
        s.readback("D", 1, "fixpp-to-peer", 0, false, {{"58", "e\xcc\x81 utf8 multi-byte"}}, {});
    }
    auto const lines = read_lines(path);
    ASSERT_EQ(lines.size(), 1u);
    // Valid multi-byte UTF-8 is NOT escaped as \uXXXX — literal bytes, `value` key.
    EXPECT_NE(lines[0].find("\"value\":\"e\xcc\x81 utf8 multi-byte\""), std::string::npos) << lines[0];
    EXPECT_EQ(lines[0].find("value_b64"), std::string::npos) << lines[0];
}

TEST(ReadbackJsonl, NonUtf8BytesRouteToValueB64)
{
    std::string const path = testing::TempDir() + "escape_nonutf8.jsonl";
    {
        Stream s(path);
        s.readback("D", 1, "fixpp-to-peer", 0, false, {{"355", std::string("\xff\xfe", 2)}}, {});
    }
    auto const lines = read_lines(path);
    ASSERT_EQ(lines.size(), 1u);
    // 0xff 0xfe is not valid UTF-8 in any position -> value_b64, no `value` key
    // for that entry. base64("\xff\xfe") == "//4=".
    EXPECT_NE(lines[0].find("{\"path\":\"355\",\"value_b64\":\"//4=\"}"), std::string::npos) << lines[0];
}

// ── contract § Canonical form — sort order removes the engine's walk order ─

TEST(ReadbackJsonl, CanonicalSortRemovesWalkOrderForFieldsAndTypedReads)
{
    std::string const path = testing::TempDir() + "sort_order.jsonl";
    {
        Stream s(path);
        // Deliberately scrambled, INCLUDING a numeric-vs-lexicographic trap
        // (9 before 55: a lexicographic-string sort would put "453" before
        // "55" before "9"; the contract's numeric tuple sort must not).
        s.readback("D", 7, "fixpp-to-peer", 0, false,
                   {
                       {"453[1].448", "second"},
                       {"453[0].802[0].523", "nested"},
                       {"453[0].448", "first"},
                       {"55", "AAPL"},
                       {"9", "ignored-order"},
                       {"453", "2"},
                   },
                   {
                       {"60", "UTCTIMESTAMP", canonical_typed_value("UTCTIMESTAMP", "20260101-00:00:00")},
                       {"9", "SEQNUM", "1"},
                       {"44", "PRICE", canonical_typed_value("PRICE", "190.500")},
                   });
    }
    auto const lines = read_lines(path);
    ASSERT_EQ(lines.size(), 1u);
    // Asserts the EXACT byte string in canonical (numeric-tuple, ascending)
    // order — deleting the sort call in render_fields()/render_typed()
    // changes this byte string (manually verified: emit_fixpp_fixture.cpp
    // built against a copy of this header with the `std::sort` call in
    // render_fields() deleted produces a DIFFERENT byte stream, RED against
    // this assertion — see that file's header for the exact recipe).
    std::string const expected =
        "{\"type\":\"readback\",\"msg_type\":\"D\",\"seq_num\":7,\"direction\":\"fixpp-to-peer\","
        "\"occurrence\":0,\"poss_dup\":false,"
        "\"fields\":[{\"path\":\"9\",\"value\":\"ignored-order\"},"
        "{\"path\":\"55\",\"value\":\"AAPL\"},"
        "{\"path\":\"453\",\"value\":\"2\"},"
        "{\"path\":\"453[0].448\",\"value\":\"first\"},"
        "{\"path\":\"453[0].802[0].523\",\"value\":\"nested\"},"
        "{\"path\":\"453[1].448\",\"value\":\"second\"}],"
        "\"typed_reads\":[{\"path\":\"9\",\"fix_type\":\"SEQNUM\",\"value\":\"1\"},"
        "{\"path\":\"44\",\"fix_type\":\"PRICE\",\"value\":\"190.5\"},"
        "{\"path\":\"60\",\"fix_type\":\"UTCTIMESTAMP\",\"value\":\"20260101-00:00:00.000\"}]}";
    EXPECT_EQ(lines[0], expected);
}

// ── contract § "Group instances must be present, not merely their count" ───
// (C-3/C-4): the count field AND the instance members must both survive the
// walk into the record — a scalar-only emitter would drop 453[0].448 etc.
// while keeping 453=2.

TEST(ReadbackJsonl, GroupCountAndInstancesBothPresent)
{
    std::string const path = testing::TempDir() + "group_shape.jsonl";
    {
        Stream s(path);
        s.readback("D", 7, "fixpp-to-peer", 0, false,
                   {{"453", "2"}, {"453[0].448", "first"}, {"453[1].448", "second"}}, {});
    }
    auto const lines = read_lines(path);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_NE(lines[0].find("{\"path\":\"453\",\"value\":\"2\"}"), std::string::npos) << lines[0];
    EXPECT_NE(lines[0].find("{\"path\":\"453[0].448\",\"value\":\"first\"}"), std::string::npos) << lines[0];
    EXPECT_NE(lines[0].find("{\"path\":\"453[1].448\",\"value\":\"second\"}"), std::string::npos)
        << lines[0];
}

// ── data-model.md §1a — fixpp's hello is NOT data-model.md §1's shape ──────

TEST(ReadbackJsonl, HelloFollowsSection1aNotSection1)
{
    std::string const path = testing::TempDir() + "hello_1a.jsonl";
    {
        Stream s(path);
        s.hello("run-1", "cell-1", "normal", "abc", "validation-on", true, "sha256:dict-d");
    }
    auto const lines = read_lines(path);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0],
              "{\"type\":\"hello\",\"run_id\":\"run-1\",\"cell_id\":\"cell-1\",\"config\":\"normal\","
              "\"script_digest\":\"abc\",\"arm\":\"validation-on\",\"has_validator\":true,"
              "\"dictionary_digest\":\"sha256:dict-d\"}");
    // §1's counterparty-only fields must NEVER appear on fixpp's hello.
    for (char const* forbidden :
         {"\"engine\"", "\"engine_version\"", "\"readback_protocol\"", "\"dictionary_enabled\"",
          "\"counterparty_digest\"", "\"typed_accessor_arm\""}) {
        EXPECT_EQ(lines[0].find(forbidden), std::string::npos) << forbidden << " in " << lines[0];
    }
}

// ── data-model.md §12 — terminal is written exactly once ───────────────────

TEST(ReadbackJsonl, TerminalWrittenExactlyOnce)
{
    std::string const path = testing::TempDir() + "terminal_once.jsonl";
    {
        Stream s(path);
        s.readback("D", 1, "fixpp-to-peer", 0, false, {{"1", "x"}}, {});
        s.terminal("completed", "run-1", "cell-1", "normal", "abc");
        // A second call (e.g. a graceful-then-forced double teardown) must
        // not overwrite the first disposition.
        s.terminal("aborted", "run-1", "cell-1", "normal", "abc");
    }
    auto const lines = read_lines(path);
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_NE(lines[1].find("\"terminal_state\":\"completed\""), std::string::npos) << lines[1];
    EXPECT_NE(lines[1].find("\"sent_count\":0"), std::string::npos) << lines[1];
    EXPECT_NE(lines[1].find("\"readback_count\":1"), std::string::npos) << lines[1];
}

// ── data-model.md §13 — the disposition record (089 T061a) ─────────────────

TEST(ReadbackJsonl, DispositionAcceptedCarriesNoRejectObject)
{
    std::string const path = testing::TempDir() + "disposition_accepted.jsonl";
    {
        Stream s(path);
        s.disposition("D", 7, kDirectionPeerToFixpp, 0, "accepted");
    }
    auto const lines = read_lines(path);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0],
              "{\"type\":\"disposition\",\"msg_type\":\"D\",\"seq_num\":7,"
              "\"direction\":\"peer-to-fixpp\",\"occurrence\":0,\"disposition\":\"accepted\"}");
    EXPECT_EQ(lines[0].find("\"reject\""), std::string::npos) << lines[0];
}

TEST(ReadbackJsonl, DispositionRejectedCarries45_373_371_58WhenAllPresent)
{
    std::string const path = testing::TempDir() + "disposition_rejected_full.jsonl";
    {
        Stream s(path);
        RejectInfo reject;
        reject.ref_seq_num = 7;
        reject.reason = 5;
        reject.ref_tag = 55;
        reject.text = "out of context";
        s.disposition("D", 7, kDirectionPeerToFixpp, 0, "rejected", reject);
    }
    auto const lines = read_lines(path);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0],
              "{\"type\":\"disposition\",\"msg_type\":\"D\",\"seq_num\":7,"
              "\"direction\":\"peer-to-fixpp\",\"occurrence\":0,\"disposition\":\"rejected\","
              "\"reject\":{\"ref_seq_num\":7,\"reason\":5,\"ref_tag\":55,"
              "\"text\":\"out of context\"}}");
}

// data-model §13: ref_tag(371)/text(58) are absent when the emitting Reject
// omits them — never a stand-in for a joined-elsewhere value.
TEST(ReadbackJsonl, DispositionRejectedOmitsRefTagAndTextWhenTheRejectDidnt)
{
    std::string const path = testing::TempDir() + "disposition_rejected_partial.jsonl";
    {
        Stream s(path);
        RejectInfo reject;
        reject.ref_seq_num = 3;
        reject.reason = 99;
        s.disposition("D", 3, kDirectionPeerToFixpp, 1, "rejected", reject);
    }
    auto const lines = read_lines(path);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0],
              "{\"type\":\"disposition\",\"msg_type\":\"D\",\"seq_num\":3,"
              "\"direction\":\"peer-to-fixpp\",\"occurrence\":1,\"disposition\":\"rejected\","
              "\"reject\":{\"ref_seq_num\":3,\"reason\":99}}");
    EXPECT_EQ(lines[0].find("\"ref_tag\""), std::string::npos) << lines[0];
    EXPECT_EQ(lines[0].find("\"text\""), std::string::npos) << lines[0];
}

// ── THE CANONICAL PARTITION — is_canonical_header_or_trailer_tag() (089 T039
// round-b, C-6) ──────────────────────────────────────────────────────────
//
// This is the production function 089's C-7 fixture driver
// (emit_fixpp_fixture.cpp) calls, and the one 089 T052's inbound readback
// builder is meant to call — not a fixture-local restatement. Four cells,
// each a distinct branch of the canonical partition (contract §
// THE CANONICAL PARTITION): ApplExtID(1156) is the QuickFIX-J-only built-in
// addition; MsgType(35) is on BOTH engines' built-in lists; CheckSum(10) is
// trailer, not header; ClOrdID(11) is body — in neither list.

TEST(ReadbackJsonl, CanonicalPartitionClassifiesApplExtIdAsHeader)
{
    // Tag 1156 is not in QuickFIX-cpp's built-in isHeaderField(int) switch
    // (verified against reference-engines/quickfix-cpp/src/C++/Message.cpp)
    // but IS in QuickFIX-J's (case ApplExtID.FIELD) — the one engine-delta
    // member the canonical partition's union adds. C-6: 1156 is header on
    // BOTH emitters regardless.
    EXPECT_TRUE(is_canonical_header_or_trailer_tag(1156));
}

TEST(ReadbackJsonl, CanonicalPartitionClassifiesMsgTypeAsHeader)
{
    // MsgType(35) is header on BOTH engines' own built-in lists — the union's
    // first two terms agreeing, not the engine-delta term 1156 exercises.
    EXPECT_TRUE(is_canonical_header_or_trailer_tag(35));
}

TEST(ReadbackJsonl, CanonicalPartitionClassifiesCheckSumAsTrailerNotHeader)
{
    // CheckSum(10) is TRAILER (Message::isTrailerField(int) on both engines,
    // byte-for-byte identical), a different exclusion set from `header` but
    // the same observable here: excluded from `fields`.
    EXPECT_TRUE(is_canonical_header_or_trailer_tag(10));
}

TEST(ReadbackJsonl, CanonicalPartitionClassifiesClOrdIdAsBody)
{
    // ClOrdID(11) is in neither engine's built-in header list, neither
    // engine's built-in trailer list, and (no dictionary predicate supplied
    // here) not header via the dictionary's <header> block either --
    // genuinely body, and must NOT be excluded.
    EXPECT_FALSE(is_canonical_header_or_trailer_tag(11));
}

TEST(ReadbackJsonl, CanonicalPartitionConsultsTheSuppliedDictionaryPredicate)
{
    // The third term (the dictionary's <header> block) is a caller-supplied
    // predicate, since this std-library-only header has no fixpp dictionary
    // dependency. A tag neither built-in list carries (ClOrdID(11) again)
    // still becomes header when the caller's dictionary predicate says so --
    // and the predicate is NOT consulted when a built-in term already
    // decided (1156 must not require it).
    EXPECT_TRUE(is_canonical_header_or_trailer_tag(11, [](int tag) { return tag == 11; }));
    EXPECT_FALSE(is_canonical_header_or_trailer_tag(12, [](int tag) { return tag == 11; }));
}
