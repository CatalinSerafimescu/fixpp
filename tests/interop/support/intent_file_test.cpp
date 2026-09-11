// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/support/intent_file_test.cpp — 089 T053a (FR-008d).
//
// Witnesses intent_file.hpp's parser: grouping, the zero-field marker, and
// the two REJECT classes (wrong column count, unknown originator) each
// naming the offending line number. quickstart.md's THREE RULES apply: name
// the observable, assert the specific diagnostic (not a bare throw), and
// every case below is exercised on a small hand-built buffer so the
// UNMUTATED behaviour is unambiguous before any mutation is considered.
#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "support/intent_file.hpp"

using namespace fixpp::interop::intent;

TEST(IntentFile, SingleMessageOneLinePerField)
{
    std::string const buf = "B-01\tfixpp\tD\t11\tX\nB-01\tfixpp\tD\t54\t1\n";
    auto const messages = parse_intent_bytes(buf);
    ASSERT_EQ(messages.size(), 1u);
    EXPECT_EQ(messages[0].step_id, "B-01");
    EXPECT_EQ(messages[0].originator, "fixpp");
    EXPECT_EQ(messages[0].msg_type, "D");
    ASSERT_EQ(messages[0].fields.size(), 2u);
    EXPECT_EQ(messages[0].fields[0].path, "11");
    EXPECT_EQ(messages[0].fields[0].value, "X");
    EXPECT_EQ(messages[0].fields[1].path, "54");
    EXPECT_EQ(messages[0].fields[1].value, "1");
}

TEST(IntentFile, ConsecutiveDifferentKeysAreSeparateMessages)
{
    std::string const buf =
        "A-1\tpeer\t1\t112\ta\n"
        "A-1\tfixpp\t0\t112\ta\n"
        "B-02\tfixpp\tD\t1\tb\n";
    auto const messages = parse_intent_bytes(buf);
    ASSERT_EQ(messages.size(), 3u);
    EXPECT_EQ(messages[0].originator, "peer");
    EXPECT_EQ(messages[1].originator, "fixpp");
    EXPECT_EQ(messages[1].msg_type, "0");
    EXPECT_EQ(messages[2].step_id, "B-02");
}

TEST(IntentFile, ZeroFieldMessageYieldsOneEmptyMessageNoFieldEntries)
{
    // A-GAPFILL's ResendRequest shape: one line, empty path and value.
    std::string const buf = "A-GAPFILL\tfixpp\t2\t\t\n";
    auto const messages = parse_intent_bytes(buf);
    ASSERT_EQ(messages.size(), 1u);
    EXPECT_EQ(messages[0].step_id, "A-GAPFILL");
    EXPECT_EQ(messages[0].msg_type, "2");
    EXPECT_TRUE(messages[0].fields.empty());
}

TEST(IntentFile, NonUtf8ValueBytePassedThroughUnchanged)
{
    std::string const raw_value = std::string("ENCODED") + '\xff' + "TEXT";
    std::string const buf = "B-05\tfixpp\tG\t355\t" + raw_value + "\n";
    auto const messages = parse_intent_bytes(buf);
    ASSERT_EQ(messages.size(), 1u);
    ASSERT_EQ(messages[0].fields.size(), 1u);
    EXPECT_EQ(messages[0].fields[0].value, raw_value);
    EXPECT_EQ(messages[0].fields[0].value.size(), raw_value.size());
}

TEST(IntentFile, NoTrailingNewlineStillParsesLastLine)
{
    std::string const buf = "B-01\tfixpp\tD\t11\tX";  // no trailing LF
    auto const messages = parse_intent_bytes(buf);
    ASSERT_EQ(messages.size(), 1u);
    ASSERT_EQ(messages[0].fields.size(), 1u);
    EXPECT_EQ(messages[0].fields[0].value, "X");
}

TEST(IntentFile, EmptyBufferYieldsNoMessages)
{
    EXPECT_TRUE(parse_intent_bytes("").empty());
}

// ── reject: wrong column count ──────────────────────────────────────────────
TEST(IntentFile, RejectsLineWithTooFewColumns)
{
    std::string const buf = "B-01\tfixpp\tD\t11\tX\n"     // line 1: ok
                             "B-02\tfixpp\tD\t54\n";       // line 2: 4 columns
    try {
        parse_intent_bytes(buf);
        FAIL() << "expected std::runtime_error";
    } catch (std::runtime_error const& e) {
        std::string const msg = e.what();
        EXPECT_NE(msg.find("line 2"), std::string::npos) << msg;
        EXPECT_NE(msg.find('4'), std::string::npos) << msg;
    }
}

TEST(IntentFile, RejectsLineWithTooManyColumns)
{
    std::string const buf = "B-01\tfixpp\tD\t11\tX\tEXTRA\n";  // 6 columns
    try {
        parse_intent_bytes(buf);
        FAIL() << "expected std::runtime_error";
    } catch (std::runtime_error const& e) {
        std::string const msg = e.what();
        EXPECT_NE(msg.find("line 1"), std::string::npos) << msg;
        EXPECT_NE(msg.find('6'), std::string::npos) << msg;
    }
}

// ── reject: unknown originator ──────────────────────────────────────────────
TEST(IntentFile, RejectsUnknownOriginator)
{
    std::string const buf = "B-01\tfixpp\tD\t11\tX\n"        // line 1: ok
                             "B-02\tbogus\tD\t54\t1\n";       // line 2: bad originator
    try {
        parse_intent_bytes(buf);
        FAIL() << "expected std::runtime_error";
    } catch (std::runtime_error const& e) {
        std::string const msg = e.what();
        EXPECT_NE(msg.find("line 2"), std::string::npos) << msg;
        EXPECT_NE(msg.find("bogus"), std::string::npos) << msg;
    }
}

// ── a dropped/corrupted middle line must not be silently skipped ───────────
TEST(IntentFile, MalformedMiddleLineIsNotSilentlySkipped)
{
    std::string const buf = "B-01\tfixpp\tD\t11\tX\n"
                             "CORRUPT\n"                       // 1 column
                             "B-02\tfixpp\tD\t54\t1\n";
    EXPECT_THROW(parse_intent_bytes(buf), std::runtime_error);
}
