// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/capi/length_data_setters_test.cpp — fixpp#428
//
// The C-ABI Length+Data contract (design `.specify/426-428-length-data-pairs.md` §5):
//   §5.1  SOH is refused only in a value that cannot be well-formed: a setter
//         refuses it on a tag that is not the Data half of a pair, and commit
//         refuses it in any such scalar, whichever setter wrote it.
//   §5.2  fixpp_msg_set_data / fixpp_entry_set_data append Length then Data, or
//         overwrite an adjacent Length-first pair in place, and refuse anything
//         else without writing.
//   §5.3  commit refuses a message whose pairs are malformed: Data not immediately
//         preceded by its Length, a Length not immediately followed by its Data, a
//         Length that is not digits or whose value is zero or differs from the Data
//         byte count, and an empty Data value. Leading zeros are accepted.
// Every refusal is judged against the FIX standard (TagValue v1.0 §4.2.4/§4.2.5), so
// no call sequence that yields a well-formed message is refused.

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <string_view>

#include "length_data_capi_support.hpp"

using namespace fixpp::capi_test;

namespace {

// An open outbound NewOrderSingle over the Length+Data test dictionary.
struct Fixture {
    fixpp_engine_t* eng = nullptr;
    fixpp_session_t* sess = nullptr;
    fixpp_msg_t* msg = nullptr;
    explicit Fixture(const char* msg_type = "D") {
        EXPECT_EQ(fixpp_engine_create(make_engine_cfg(), FIXPP_C_ABI_VERSION_MAJOR,
                                      FIXPP_C_ABI_VERSION_MINOR, &eng),
                  FIXPP_ERR_OK);
        fixpp_session_config_t* sc =
            make_length_data_session_cfg("CLI", "SRV", FIXPP_ROLE_INITIATOR);
        set_loopback_endpoint(sc, "127.0.0.1", 0);
        EXPECT_EQ(fixpp_session_open(eng, sc, &sess), FIXPP_ERR_OK);
        EXPECT_EQ(fixpp_msg_create_outbound(sess, msg_type, std::strlen(msg_type), &msg),
                  FIXPP_ERR_OK);
    }
    ~Fixture() {
        if (msg) fixpp_msg_destroy(msg);
        if (eng) fixpp_engine_destroy(eng);
    }
    fixpp_error_t set_string(uint16_t tag, std::string_view v) {
        return fixpp_msg_set_string(msg, tag, v.data(), v.size());
    }
    fixpp_error_t set_data(uint16_t tag, std::string_view v) {
        return fixpp_msg_set_data(msg, tag, as_u8(v), v.size());
    }
    // Commits and returns the payload as a string ("" on any error, with `rc` set).
    std::string commit(fixpp_error_t& rc) {
        const uint8_t* p = nullptr;
        size_t n = 0;
        rc = fixpp_msg_commit(msg, &p, &n);
        return rc == FIXPP_ERR_OK ? std::string{as_sv(p, n)} : std::string{};
    }
};

constexpr std::string_view kBinary{
    "A\x01"
    "B\xff",
    4};

}  // namespace

// ── §5.2 fixpp_msg_set_data ──────────────────────────────────────────────────

TEST(CapiSetData, AppendsLengthThenDataVerbatim) {
    Fixture f;
    ASSERT_EQ(f.set_string(11, "C1"), FIXPP_ERR_OK);
    ASSERT_EQ(f.set_data(355, kBinary), FIXPP_ERR_OK);
    fixpp_error_t rc{};
    auto const payload = f.commit(rc);
    ASSERT_EQ(rc, FIXPP_ERR_OK);
    EXPECT_NE(payload.find(std::string{"11=C1\x01"
                                       "354=4\x01"
                                       "355="} +
                           std::string{kBinary} + "\x01"),
              std::string::npos)
        << payload;
}

TEST(CapiSetData, OverwritesAnAdjacentPairInPlace) {
    Fixture f;
    ASSERT_EQ(f.set_string(11, "C1"), FIXPP_ERR_OK);
    ASSERT_EQ(f.set_data(355, "first"), FIXPP_ERR_OK);
    ASSERT_EQ(f.set_string(55, "SYM"), FIXPP_ERR_OK);
    ASSERT_EQ(f.set_data(355, "2nd"), FIXPP_ERR_OK);
    fixpp_error_t rc{};
    auto const payload = f.commit(rc);
    ASSERT_EQ(rc, FIXPP_ERR_OK);
    EXPECT_NE(payload.find("11=C1\x01"
                           "354=3\x01"
                           "355=2nd\x01"
                           "55=SYM\x01"),
              std::string::npos)
        << payload;
    EXPECT_EQ(payload.find("first"), std::string::npos) << payload;
}

TEST(CapiSetData, RefusesWhenOnlyOneHalfIsPresentAndWritesNothing) {
    Fixture f;
    ASSERT_EQ(fixpp_msg_set_int(f.msg, 354, 3), FIXPP_ERR_OK);
    EXPECT_EQ(f.set_data(355, "abc"), FIXPP_ERR_TYPE_MISMATCH);
    // Removing the stray half makes the same call succeed.
    ASSERT_EQ(fixpp_msg_remove_tag(f.msg, 354), FIXPP_ERR_OK);
    EXPECT_EQ(f.set_data(355, "abc"), FIXPP_ERR_OK);
}

TEST(CapiSetData, RefusesDataFirstHalves) {
    Fixture f;
    ASSERT_EQ(f.set_string(355, "abc"), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_msg_set_int(f.msg, 354, 3), FIXPP_ERR_OK);
    EXPECT_EQ(f.set_data(355, "xyz"), FIXPP_ERR_TYPE_MISMATCH);
}

TEST(CapiSetData, RefusesATagThatIsNotTheDataHalfOfAPair) {
    Fixture f;
    EXPECT_EQ(f.set_data(58, "text"), FIXPP_ERR_TYPE_MISMATCH);   // STRING
    EXPECT_EQ(f.set_data(354, "text"), FIXPP_ERR_TYPE_MISMATCH);  // a Length half
}

TEST(CapiSetData, RefusesAnEmptyValue) {
    Fixture f;
    EXPECT_EQ(fixpp_msg_set_data(f.msg, 355, as_u8("x"), 0), FIXPP_ERR_WIRE_CONFORMANCE);
}

TEST(CapiSetData, RefusesAFramingTagFirst) {
    Fixture f;
    EXPECT_EQ(f.set_data(10, "x"), FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN);
}

TEST(CapiSetData, NullArguments) {
    Fixture f;
    EXPECT_EQ(fixpp_msg_set_data(nullptr, 355, as_u8("x"), 1), FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(fixpp_msg_set_data(f.msg, 355, nullptr, 1), FIXPP_ERR_NULL_HANDLE);
}

TEST(CapiSetData, DataTagAbsentFromTheMsgTypeGrammar) {
    Fixture f;
    EXPECT_EQ(f.set_data(96, "x"), FIXPP_ERR_DICT_CONFIG);
}

TEST(CapiSetData, CustomDictionaryPair) {
    Fixture f;
    ASSERT_EQ(f.set_data(5002, kBinary), FIXPP_ERR_OK);
    fixpp_error_t rc{};
    auto const payload = f.commit(rc);
    ASSERT_EQ(rc, FIXPP_ERR_OK);
    EXPECT_NE(payload.find(std::string{"5001=4\x01"
                                       "5002="} +
                           std::string{kBinary}),
              std::string::npos);
}

// An append never shifts an index an open builder holds (design §5.2, r2 R2-3).
TEST(CapiSetData, AppendingWhileAGroupBuilderIsOpenKeepsTheBuilderOnItsGroup) {
    Fixture f;
    fixpp_group_builder_t* gb = nullptr;
    ASSERT_EQ(fixpp_msg_group_begin(f.msg, 78, &gb), FIXPP_ERR_OK);
    fixpp_entry_t* e = nullptr;
    ASSERT_EQ(fixpp_group_builder_add_entry(gb, &e), FIXPP_ERR_OK);
    ASSERT_EQ(f.set_data(355, "top"), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_entry_set_string(e, 79, "ACC", 3), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_msg_group_end(f.msg, gb), FIXPP_ERR_OK);
    fixpp_error_t rc{};
    auto const payload = f.commit(rc);
    ASSERT_EQ(rc, FIXPP_ERR_OK);
    EXPECT_NE(payload.find("78=1\x01"
                           "79=ACC\x01"
                           "354=3\x01"
                           "355=top\x01"),
              std::string::npos)
        << payload;
}

// ── §5.2 fixpp_entry_set_data ────────────────────────────────────────────────

TEST(CapiEntrySetData, PairInsideAGroupInstance) {
    Fixture f;
    fixpp_group_builder_t* gb = nullptr;
    ASSERT_EQ(fixpp_msg_group_begin(f.msg, 78, &gb), FIXPP_ERR_OK);
    fixpp_entry_t* e = nullptr;
    ASSERT_EQ(fixpp_group_builder_add_entry(gb, &e), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_entry_set_string(e, 79, "ACC", 3), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_entry_set_data(e, 361, as_u8(kBinary), kBinary.size()), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_msg_group_end(f.msg, gb), FIXPP_ERR_OK);
    fixpp_error_t rc{};
    auto const payload = f.commit(rc);
    ASSERT_EQ(rc, FIXPP_ERR_OK);
    EXPECT_NE(payload.find(std::string{"79=ACC\x01"
                                       "360=4\x01"
                                       "361="} +
                           std::string{kBinary}),
              std::string::npos)
        << payload;
}

TEST(CapiEntrySetData, RefusesADataTagThatIsTheGroupDelimiter) {
    Fixture f;
    fixpp_group_builder_t* gb = nullptr;
    ASSERT_EQ(fixpp_msg_group_begin(f.msg, 5000, &gb), FIXPP_ERR_OK);
    fixpp_entry_t* e = nullptr;
    ASSERT_EQ(fixpp_group_builder_add_entry(gb, &e), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_entry_set_data(e, 5004, as_u8("blob"), 4), FIXPP_ERR_TYPE_MISMATCH);
}

TEST(CapiEntrySetData, DelimiterRuleUsesTheGroupsOwnContext) {
    // Group 5000 opens with the Data field 5004 in "D", but with its Length 5003 in "E".
    // The dictionary-wide first-seen delimiter is D's, so a context-free lookup refuses
    // a pair that is well-formed in "E".
    Fixture f("E");
    fixpp_group_builder_t* gb = nullptr;
    ASSERT_EQ(fixpp_msg_group_begin(f.msg, 5000, &gb), FIXPP_ERR_OK);
    fixpp_entry_t* e = nullptr;
    ASSERT_EQ(fixpp_group_builder_add_entry(gb, &e), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_entry_set_data(e, 5004, as_u8("blob"), 4), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_msg_group_end(f.msg, gb), FIXPP_ERR_OK);
    fixpp_error_t rc{};
    auto const payload = f.commit(rc);
    ASSERT_EQ(rc, FIXPP_ERR_OK);
    EXPECT_NE(payload.find("5003=4\x01"
                           "5004=blob\x01"),
              std::string::npos)
        << payload;
}

// ── fixpp_msg_create_outbound: MsgType (Gate B r1 G-1) ──────────────────────

TEST(CapiCreateOutbound, DictFreeSessionRefusesAnEmptyOrSohMsgType) {
    fixpp_engine_t* eng = nullptr;
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), FIXPP_C_ABI_VERSION_MAJOR,
                                  FIXPP_C_ABI_VERSION_MINOR, &eng),
              FIXPP_ERR_OK);
    fixpp_session_config_t* sc = nullptr;
    ASSERT_EQ(fixpp_session_config_create(&sc), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_session_config_set_comp_ids(sc, "DFA", "DFB"), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_session_config_set_begin_string(sc, "FIX.4.2"), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_session_config_set_role(sc, FIXPP_ROLE_ACCEPTOR), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_session_config_set_heartbeat_seconds(sc, 30), FIXPP_ERR_OK);
    ASSERT_EQ(
        fixpp_session_config_set_security(sc, FIXPP_SECURITY_INSECURE_PLAIN_TCP, nullptr, nullptr),
        FIXPP_ERR_OK);
    set_loopback_endpoint(sc, "127.0.0.1", 0);
    fixpp_session_t* sess = nullptr;
    ASSERT_EQ(fixpp_session_open(eng, sc, &sess), FIXPP_ERR_OK);

    fixpp_msg_t* msg = nullptr;
    constexpr std::string_view kInjected{
        "D\x01"
        "11=INJECTED",
        13};
    EXPECT_EQ(fixpp_msg_create_outbound(sess, kInjected.data(), kInjected.size(), &msg),
              FIXPP_ERR_WIRE_CONFORMANCE);
    EXPECT_EQ(msg, nullptr);
    EXPECT_EQ(fixpp_msg_create_outbound(sess, "", 0, &msg), FIXPP_ERR_WIRE_CONFORMANCE);
    EXPECT_EQ(msg, nullptr);
    ASSERT_EQ(fixpp_msg_create_outbound(sess, "D", 1, &msg), FIXPP_ERR_OK)
        << "an ordinary MsgType is still accepted without a dictionary";
    fixpp_msg_destroy(msg);
    fixpp_engine_destroy(eng);
}

// ── §5.1 SOH in the string setters ───────────────────────────────────────────

TEST(CapiStringSetterSoh, RefusedOnANonDataTagAndNothingIsWritten) {
    Fixture f;
    EXPECT_EQ(f.set_string(58, std::string_view{"a\x01"
                                                "49=EVIL",
                                                8}),
              FIXPP_ERR_WIRE_CONFORMANCE);
    ASSERT_EQ(f.set_string(11, "C1"), FIXPP_ERR_OK);
    fixpp_error_t rc{};
    auto const payload = f.commit(rc);
    ASSERT_EQ(rc, FIXPP_ERR_OK);
    EXPECT_EQ(payload.find("58="), std::string::npos) << payload;
}

TEST(CapiStringSetterSoh, EntryTwinRefusedOnANonDataTag) {
    Fixture f;
    fixpp_group_builder_t* gb = nullptr;
    ASSERT_EQ(fixpp_msg_group_begin(f.msg, 78, &gb), FIXPP_ERR_OK);
    fixpp_entry_t* e = nullptr;
    ASSERT_EQ(fixpp_group_builder_add_entry(gb, &e), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_entry_set_string(e, 79,
                                     "a\x01"
                                     "b",
                                     3),
              FIXPP_ERR_WIRE_CONFORMANCE);
}

// r2 R2-1: a hand-built pair whose Data value holds SOH is well-formed and accepted.
TEST(CapiStringSetterSoh, AcceptedOnADataHalfWhoseLengthMatches) {
    Fixture f;
    ASSERT_EQ(fixpp_msg_set_int(f.msg, 354, 3), FIXPP_ERR_OK);
    ASSERT_EQ(f.set_string(355, std::string_view{"A\x01"
                                                 "B",
                                                 3}),
              FIXPP_ERR_OK);
    fixpp_error_t rc{};
    auto const payload = f.commit(rc);
    ASSERT_EQ(rc, FIXPP_ERR_OK);
    EXPECT_NE(payload.find("354=3\x01"
                           "355=A\x01"
                           "B\x01"),
              std::string::npos)
        << payload;
}

TEST(CapiStringSetterSoh, EntryTwinAcceptedOnADataHalfWhoseLengthMatches) {
    Fixture f;
    fixpp_group_builder_t* gb = nullptr;
    ASSERT_EQ(fixpp_msg_group_begin(f.msg, 78, &gb), FIXPP_ERR_OK);
    fixpp_entry_t* e = nullptr;
    ASSERT_EQ(fixpp_group_builder_add_entry(gb, &e), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_entry_set_string(e, 79, "ACC", 3), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_entry_set_int(e, 360, 3), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_entry_set_string(e, 361,
                                     "A\x01"
                                     "B",
                                     3),
              FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_msg_group_end(f.msg, gb), FIXPP_ERR_OK);
    fixpp_error_t rc{};
    (void)f.commit(rc);
    EXPECT_EQ(rc, FIXPP_ERR_OK);
}

TEST(CapiStringSetterSoh, OtherControlBytesAreStillAccepted) {
    Fixture f;
    ASSERT_EQ(f.set_string(11, "C1"), FIXPP_ERR_OK);
    EXPECT_EQ(f.set_string(58, "line1\nline2\t"), FIXPP_ERR_OK);
    fixpp_error_t rc{};
    (void)f.commit(rc);
    EXPECT_EQ(rc, FIXPP_ERR_OK);
}

// ── §5.3 commit-time conformance ─────────────────────────────────────────────

TEST(CapiCommitPairs, RefusesDataBeforeItsLength) {
    Fixture f;
    ASSERT_EQ(f.set_string(355, "abc"), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_msg_set_int(f.msg, 354, 3), FIXPP_ERR_OK);
    fixpp_error_t rc{};
    (void)f.commit(rc);
    EXPECT_EQ(rc, FIXPP_ERR_WIRE_CONFORMANCE);
}

TEST(CapiCommitPairs, RefusesALengthThatDisagreesWithTheByteCount) {
    Fixture f;
    ASSERT_EQ(fixpp_msg_set_int(f.msg, 354, 5), FIXPP_ERR_OK);
    ASSERT_EQ(f.set_string(355, "abc"), FIXPP_ERR_OK);
    fixpp_error_t rc{};
    (void)f.commit(rc);
    EXPECT_EQ(rc, FIXPP_ERR_WIRE_CONFORMANCE);
}

TEST(CapiCommitPairs, RefusesALoneLength) {
    Fixture f;
    ASSERT_EQ(fixpp_msg_set_int(f.msg, 354, 3), FIXPP_ERR_OK);
    fixpp_error_t rc{};
    (void)f.commit(rc);
    EXPECT_EQ(rc, FIXPP_ERR_WIRE_CONFORMANCE);
}

TEST(CapiCommitPairs, RefusesALoneData) {
    Fixture f;
    ASSERT_EQ(f.set_string(355, "abc"), FIXPP_ERR_OK);
    fixpp_error_t rc{};
    (void)f.commit(rc);
    EXPECT_EQ(rc, FIXPP_ERR_WIRE_CONFORMANCE);
}

// r3 R3-2: TagValue v1.0 Table 1 — `int` may contain leading zeros.
TEST(CapiCommitPairs, AcceptsALengthWithLeadingZeros) {
    Fixture f;
    ASSERT_EQ(f.set_string(354, "003"), FIXPP_ERR_OK);
    ASSERT_EQ(f.set_string(355, "abc"), FIXPP_ERR_OK);
    fixpp_error_t rc{};
    (void)f.commit(rc);
    EXPECT_EQ(rc, FIXPP_ERR_OK);
}

TEST(CapiCommitPairs, RefusesALengthThatIsNotPositiveDigits) {
    for (std::string_view const bad : {"+3", "-3", "0", "3 ", ""}) {
        Fixture f;
        ASSERT_EQ(f.set_string(354, bad), FIXPP_ERR_OK) << "value [" << bad << "]";
        ASSERT_EQ(f.set_string(355, "abc"), FIXPP_ERR_OK);
        fixpp_error_t rc{};
        (void)f.commit(rc);
        EXPECT_EQ(rc, FIXPP_ERR_WIRE_CONFORMANCE) << "value [" << bad << "]";
    }
}

TEST(CapiCommitPairs, RefusesSohWrittenByTheBytesSetterOnANonDataTag) {
    Fixture f;
    ASSERT_EQ(fixpp_msg_set_bytes(f.msg, 58,
                                  as_u8("a\x01"
                                        "b"),
                                  3),
              FIXPP_ERR_OK);
    fixpp_error_t rc{};
    (void)f.commit(rc);
    EXPECT_EQ(rc, FIXPP_ERR_WIRE_CONFORMANCE);
}

TEST(CapiCommitPairs, AcceptsAHandBuiltPairFromTheBytesSetter) {
    Fixture f;
    ASSERT_EQ(fixpp_msg_set_int(f.msg, 354, 4), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_msg_set_bytes(f.msg, 355, as_u8(kBinary), kBinary.size()), FIXPP_ERR_OK);
    fixpp_error_t rc{};
    (void)f.commit(rc);
    EXPECT_EQ(rc, FIXPP_ERR_OK);
}

TEST(CapiCommitPairs, RulesApplyInsideAGroupInstance) {
    Fixture f;
    fixpp_group_builder_t* gb = nullptr;
    ASSERT_EQ(fixpp_msg_group_begin(f.msg, 78, &gb), FIXPP_ERR_OK);
    fixpp_entry_t* e = nullptr;
    ASSERT_EQ(fixpp_group_builder_add_entry(gb, &e), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_entry_set_string(e, 79, "ACC", 3), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_entry_set_string(e, 361, "abc", 3), FIXPP_ERR_OK);  // no Length
    ASSERT_EQ(fixpp_msg_group_end(f.msg, gb), FIXPP_ERR_OK);
    fixpp_error_t rc{};
    (void)f.commit(rc);
    EXPECT_EQ(rc, FIXPP_ERR_WIRE_CONFORMANCE);
}
