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

#include <array>
#include <cstddef>
#include <cstring>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <memory>
#include <memory_resource>
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
    explicit Fixture(const char* msg_type = "D", std::string_view xml = kLengthDataFix42Xml) {
        EXPECT_EQ(fixpp_engine_create(make_engine_cfg(), FIXPP_C_ABI_VERSION_MAJOR,
                                      FIXPP_C_ABI_VERSION_MINOR, &eng),
                  FIXPP_ERR_OK);
        fixpp_session_config_t* sc =
            make_length_data_session_cfg("CLI", "SRV", FIXPP_ROLE_INITIATOR, xml);
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

// An open outbound NewOrderSingle on a session with no dictionary: the pairs are the
// standard table alone, and a group can be opened on any tag.
struct DictFreeFixture {
    fixpp_engine_t* eng = nullptr;
    fixpp_session_t* sess = nullptr;
    fixpp_msg_t* msg = nullptr;
    DictFreeFixture() {
        EXPECT_EQ(fixpp_engine_create(make_engine_cfg(), FIXPP_C_ABI_VERSION_MAJOR,
                                      FIXPP_C_ABI_VERSION_MINOR, &eng),
                  FIXPP_ERR_OK);
        fixpp_session_config_t* sc = nullptr;
        EXPECT_EQ(fixpp_session_config_create(&sc), FIXPP_ERR_OK);
        EXPECT_EQ(fixpp_session_config_set_comp_ids(sc, "DFA", "DFB"), FIXPP_ERR_OK);
        EXPECT_EQ(fixpp_session_config_set_begin_string(sc, "FIX.4.2"), FIXPP_ERR_OK);
        EXPECT_EQ(fixpp_session_config_set_role(sc, FIXPP_ROLE_ACCEPTOR), FIXPP_ERR_OK);
        EXPECT_EQ(fixpp_session_config_set_heartbeat_seconds(sc, 30), FIXPP_ERR_OK);
        EXPECT_EQ(fixpp_session_config_set_security(sc, FIXPP_SECURITY_INSECURE_PLAIN_TCP, nullptr,
                                                    nullptr),
                  FIXPP_ERR_OK);
        set_loopback_endpoint(sc, "127.0.0.1", 0);
        EXPECT_EQ(fixpp_session_open(eng, sc, &sess), FIXPP_ERR_OK);
        EXPECT_EQ(fixpp_msg_create_outbound(sess, "D", 1, &msg), FIXPP_ERR_OK);
    }
    ~DictFreeFixture() {
        if (msg) fixpp_msg_destroy(msg);
        if (eng) fixpp_engine_destroy(eng);
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
    // Removing the stray half makes the same call succeed. 090/D-1 (fixpp#447):
    // remove_tag now refuses this erase while a group builder is open — not
    // the case here, no group builder has been opened on `f.msg` at all — so
    // this setup use is unaffected by that refusal.
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

TEST(CapiEntrySetData, NestedGroupDelimiterIsLookedUpUnderItsParentGroup) {
    // Group 5000 is declared only at the top of "D", where its delimiter is the Data field
    // 5004. Nested under a NoAllocs(78) entry it has no declaration, so the exact-context
    // lookup misses and the setter defers to commit, which refuses the undeclared nesting.
    // A lookup that drops the parent group finds D's top-level declaration and refuses
    // 5004 at the setter instead.
    Fixture f;
    fixpp_group_builder_t* gb = nullptr;
    ASSERT_EQ(fixpp_msg_group_begin(f.msg, 78, &gb), FIXPP_ERR_OK);
    fixpp_entry_t* e = nullptr;
    ASSERT_EQ(fixpp_group_builder_add_entry(gb, &e), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_entry_set_string(e, 79, "ACC", 3), FIXPP_ERR_OK);
    fixpp_group_builder_t* nested = nullptr;
    ASSERT_EQ(fixpp_entry_group_begin(e, 5000, &nested), FIXPP_ERR_OK);
    fixpp_entry_t* ne = nullptr;
    ASSERT_EQ(fixpp_group_builder_add_entry(nested, &ne), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_entry_set_data(ne, 5004, as_u8("blob"), 4), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_msg_group_end(f.msg, nested), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_msg_group_end(f.msg, gb), FIXPP_ERR_OK);
    fixpp_error_t rc{};
    (void)f.commit(rc);
    EXPECT_NE(rc, FIXPP_ERR_OK);
}

// §5.2: a Data setter refuses a pair whose Length or Data tag is already a group in the
// target container, and writes nothing. A scalar Data right after the group makes the two
// look like an adjacent pair, which would otherwise be overwritten in place, group and all.
TEST(CapiSetData, RefusesAPairThatCollidesWithAGroup) {
    DictFreeFixture f;
    fixpp_group_builder_t* gb = nullptr;
    ASSERT_EQ(fixpp_msg_group_begin(f.msg, 354, &gb), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_msg_group_end(f.msg, gb), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_msg_set_string(f.msg, 355, "abc", 3), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_msg_set_data(f.msg, 355, as_u8("xyz"), 3), FIXPP_ERR_TYPE_MISMATCH);
}

TEST(CapiEntrySetData, RefusesAPairThatCollidesWithANestedGroup) {
    DictFreeFixture f;
    fixpp_group_builder_t* gb = nullptr;
    ASSERT_EQ(fixpp_msg_group_begin(f.msg, 78, &gb), FIXPP_ERR_OK);
    fixpp_entry_t* e = nullptr;
    ASSERT_EQ(fixpp_group_builder_add_entry(gb, &e), FIXPP_ERR_OK);
    fixpp_group_builder_t* nested = nullptr;
    ASSERT_EQ(fixpp_entry_group_begin(e, 354, &nested), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_msg_group_end(f.msg, nested), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_entry_set_string(e, 355, "abc", 3), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_entry_set_data(e, 355, as_u8("xyz"), 3), FIXPP_ERR_TYPE_MISMATCH);
}

TEST(CapiSetData, RefusesAFramingTagAsTheDerivedLengthHalf) {
    // The dictionary pairs BeginString(8) with the Data field 5100, so the Length half
    // either setter would derive is a framing tag. Both refuse and write nothing.
    Fixture f("D", kFramingLengthPairFix42Xml);
    ASSERT_EQ(f.set_string(11, "C1"), FIXPP_ERR_OK);
    EXPECT_EQ(f.set_data(5100, "blob"), FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN);

    fixpp_group_builder_t* gb = nullptr;
    ASSERT_EQ(fixpp_msg_group_begin(f.msg, 5101, &gb), FIXPP_ERR_OK);
    fixpp_entry_t* e = nullptr;
    ASSERT_EQ(fixpp_group_builder_add_entry(gb, &e), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_entry_set_string(e, 79, "ACC", 3), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_entry_set_data(e, 5100, as_u8("blob"), 4), FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN);
    ASSERT_EQ(fixpp_msg_group_end(f.msg, gb), FIXPP_ERR_OK);

    fixpp_error_t rc{};
    auto const payload = f.commit(rc);
    ASSERT_EQ(rc, FIXPP_ERR_OK);
    EXPECT_EQ(payload.find("\x01"
                           "8="),
              std::string::npos)
        << payload;
    EXPECT_EQ(payload.find("5100="), std::string::npos) << payload;
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

// A group's count field is a field of its container, so a Length directly before a group
// is a Length not followed by its Data, even when that Data comes after the group.
TEST(CapiCommitPairs, RefusesALengthSeparatedFromItsDataByAGroup) {
    Fixture f;
    ASSERT_EQ(fixpp_msg_set_int(f.msg, 354, 3), FIXPP_ERR_OK);
    fixpp_group_builder_t* gb = nullptr;
    ASSERT_EQ(fixpp_msg_group_begin(f.msg, 78, &gb), FIXPP_ERR_OK);
    fixpp_entry_t* e = nullptr;
    ASSERT_EQ(fixpp_group_builder_add_entry(gb, &e), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_entry_set_string(e, 79, "ACC", 3), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_msg_group_end(f.msg, gb), FIXPP_ERR_OK);
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

// ── 091 FR-021: the commit check feeds a group node an empty value ──────────
//
// A group's count field is one field of its container, and `check_length_data` feeds
// it to the pair check with an empty value, as `body_builder::commit` does. So a group
// whose count tag is a pair half is never read as that half: as a Length its empty count
// is refused, and as a Data it is either not preceded by its Length or, when it is, its
// value is empty. A group whose count tag is not a pair half passes, and a Length right
// before it is still a Length not followed by its Data. Every group instance starts with
// a non-pair field (79), so no cell here reaches INV-4's FIXPP_ERR_TYPE_MISMATCH.

namespace {

// A commit's return code and payload. `shown` renders the payload with SOH as '|'
// (or "<none>"), so a failure message carries the bytes a commit produced.
struct Committed {
    fixpp_error_t rc{};
    bool has_payload = false;
    std::string shown;
};

Committed commit_msg(fixpp_msg_t* msg) {
    const uint8_t* p = nullptr;
    size_t n = 0;
    Committed c;
    c.rc = fixpp_msg_commit(msg, &p, &n);
    c.has_payload = p != nullptr || n != 0;
    if (p == nullptr) {
        c.shown = "<none>";
    } else {
        c.shown.assign(reinterpret_cast<const char*>(p), n);
        for (auto& ch : c.shown) {
            if (ch == '\x01') ch = '|';
        }
    }
    return c;
}

// Opens a group on `tag` in the message with one instance holding 79=A1, and closes it.
void add_one_instance_group(fixpp_msg_t* msg, uint16_t tag) {
    fixpp_group_builder_t* gb = nullptr;
    ASSERT_EQ(fixpp_msg_group_begin(msg, tag, &gb), FIXPP_ERR_OK);
    fixpp_entry_t* e = nullptr;
    ASSERT_EQ(fixpp_group_builder_add_entry(gb, &e), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_entry_set_string(e, 79, "A1", 2), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_msg_group_end(msg, gb), FIXPP_ERR_OK);
}

struct MsgDeleter {
    void operator()(fixpp_msg_t* m) const noexcept { fixpp_msg_destroy(m); }
};

}  // namespace

// The Length shape: a group tagged 354 with one instance, then a one-byte 355.
TEST(CapiCommitGroupNode, GroupTaggedAsALengthDoesNotCompleteThePair) {
    DictFreeFixture f;
    ASSERT_NO_FATAL_FAILURE(add_one_instance_group(f.msg, 354));
    ASSERT_EQ(fixpp_msg_set_bytes(f.msg, 355, as_u8("x"), 1), FIXPP_ERR_OK);
    auto const c = commit_msg(f.msg);
    EXPECT_EQ(c.rc, FIXPP_ERR_WIRE_CONFORMANCE) << "payload [" << c.shown << "]";
    EXPECT_FALSE(c.has_payload) << "payload [" << c.shown << "]";
}

// The Data shape: 354=1, then a group tagged 355 with one instance.
TEST(CapiCommitGroupNode, GroupTaggedAsTheDataDoesNotCompleteThePair) {
    DictFreeFixture f;
    ASSERT_EQ(fixpp_msg_set_int(f.msg, 354, 1), FIXPP_ERR_OK);
    ASSERT_NO_FATAL_FAILURE(add_one_instance_group(f.msg, 355));
    auto const c = commit_msg(f.msg);
    EXPECT_EQ(c.rc, FIXPP_ERR_WIRE_CONFORMANCE) << "payload [" << c.shown << "]";
    EXPECT_FALSE(c.has_payload) << "payload [" << c.shown << "]";
}

// The Data shape with zero instances: 354=1, then an empty group tagged 355.
TEST(CapiCommitGroupNode, EmptyGroupTaggedAsTheDataDoesNotCompleteThePair) {
    DictFreeFixture f;
    ASSERT_EQ(fixpp_msg_set_int(f.msg, 354, 1), FIXPP_ERR_OK);
    fixpp_group_builder_t* gb = nullptr;
    ASSERT_EQ(fixpp_msg_group_begin(f.msg, 355, &gb), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_msg_group_end(f.msg, gb), FIXPP_ERR_OK);
    auto const c = commit_msg(f.msg);
    EXPECT_EQ(c.rc, FIXPP_ERR_WIRE_CONFORMANCE) << "payload [" << c.shown << "]";
    EXPECT_FALSE(c.has_payload) << "payload [" << c.shown << "]";
}

// The Data shape on a dictionary session whose dictionary pairs 5001 with 5002 and declares
// a group on 5002. The preconditions establish, two-sided, that the dictionary pairs the
// tags and that it (not an absent gate) accepts the group; they run on a separate
// dictionary load and on a control message, never on the message under test.
TEST(CapiCommitGroupNode, EmptyGroupTaggedAsADictionaryDataDoesNotCompleteThePair) {
    // Load: the session over the new dictionary opens (the fixture EXPECTs every step).
    Fixture f("D", kGroupOnPairHalfFix42Xml);
    ASSERT_NE(f.sess, nullptr);
    ASSERT_NE(f.msg, nullptr);

    // Pair: the same document, loaded again, pairs 5001 with 5002 in both directions, and a
    // declared field that is not LENGTH pairs nothing.
    {
        constexpr std::size_t kBufSize = 128U * 1024U;
        auto buf = std::make_unique<std::array<std::byte, kBufSize>>();
        std::pmr::monotonic_buffer_resource mr{buf->data(), buf->size()};
        fixpp::dict::Dictionary const dict =
            fixpp::dict::XmlLoader{}.load_from_string(kGroupOnPairHalfFix42Xml, &mr);
        auto const tv = dict.as_table_view();
        ASSERT_EQ(dict.length_pair_data_tag(5001), 5002);
        ASSERT_EQ(tv.data_pair_length_tag(5002), 5001);
        ASSERT_EQ(dict.length_pair_data_tag(11), 0);
    }

    // Group: on a control message, never committed, the dictionary accepts a group on 5002
    // and refuses one on 5001, which it declares no group for.
    {
        fixpp_msg_t* raw = nullptr;
        ASSERT_EQ(fixpp_msg_create_outbound(f.sess, "D", 1, &raw), FIXPP_ERR_OK);
        std::unique_ptr<fixpp_msg_t, MsgDeleter> const ctl{raw};
        fixpp_group_builder_t* b = nullptr;
        ASSERT_EQ(fixpp_msg_group_begin(ctl.get(), 5002, &b), FIXPP_ERR_OK);
        ASSERT_EQ(fixpp_msg_group_end(ctl.get(), b), FIXPP_ERR_OK);
        fixpp_group_builder_t* b2 = nullptr;
        ASSERT_EQ(fixpp_msg_group_begin(ctl.get(), 5001, &b2), FIXPP_ERR_TYPE_MISMATCH);
    }

    ASSERT_EQ(fixpp_msg_set_bytes(f.msg, 5001, as_u8("1"), 1), FIXPP_ERR_OK);
    fixpp_group_builder_t* g = nullptr;
    ASSERT_EQ(fixpp_msg_group_begin(f.msg, 5002, &g), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_msg_group_end(f.msg, g), FIXPP_ERR_OK);
    auto const c = commit_msg(f.msg);
    EXPECT_EQ(c.rc, FIXPP_ERR_WIRE_CONFORMANCE) << "payload [" << c.shown << "]";
    EXPECT_FALSE(c.has_payload) << "payload [" << c.shown << "]";
}

// The Length shape inside a group instance, which is checked as its own container: in an
// instance of a group tagged 78, a nested group tagged 354, then a one-byte 355.
TEST(CapiCommitGroupNode, NestedGroupTaggedAsALengthDoesNotCompleteThePair) {
    DictFreeFixture f;
    fixpp_group_builder_t* gb = nullptr;
    ASSERT_EQ(fixpp_msg_group_begin(f.msg, 78, &gb), FIXPP_ERR_OK);
    fixpp_entry_t* e = nullptr;
    ASSERT_EQ(fixpp_group_builder_add_entry(gb, &e), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_entry_set_string(e, 79, "A1", 2), FIXPP_ERR_OK);
    fixpp_group_builder_t* nested = nullptr;
    ASSERT_EQ(fixpp_entry_group_begin(e, 354, &nested), FIXPP_ERR_OK);
    fixpp_entry_t* ne = nullptr;
    ASSERT_EQ(fixpp_group_builder_add_entry(nested, &ne), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_entry_set_string(ne, 79, "A1", 2), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_msg_group_end(f.msg, nested), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_entry_set_string(e, 355, "x", 1), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_msg_group_end(f.msg, gb), FIXPP_ERR_OK);
    auto const c = commit_msg(f.msg);
    EXPECT_EQ(c.rc, FIXPP_ERR_WIRE_CONFORMANCE) << "payload [" << c.shown << "]";
    EXPECT_FALSE(c.has_payload) << "payload [" << c.shown << "]";
}

// Twin of the Length shape with the group tagged 78: the 355 is an orphan Data.
TEST(CapiCommitGroupNode, TwinNonPairGroupWithABareSiblingDataIsRefused) {
    DictFreeFixture f;
    ASSERT_NO_FATAL_FAILURE(add_one_instance_group(f.msg, 78));
    ASSERT_EQ(fixpp_msg_set_bytes(f.msg, 355, as_u8("x"), 1), FIXPP_ERR_OK);
    auto const c = commit_msg(f.msg);
    EXPECT_EQ(c.rc, FIXPP_ERR_WIRE_CONFORMANCE) << "payload [" << c.shown << "]";
    EXPECT_FALSE(c.has_payload) << "payload [" << c.shown << "]";
}

TEST(CapiCommitGroupNode, TwinNonPairGroupAloneCommits) {
    DictFreeFixture f;
    ASSERT_NO_FATAL_FAILURE(add_one_instance_group(f.msg, 78));
    auto const c = commit_msg(f.msg);
    EXPECT_EQ(c.rc, FIXPP_ERR_OK) << "payload [" << c.shown << "]";
    EXPECT_EQ(c.shown, "35=D|78=1|79=A1|");
}

TEST(CapiCommitGroupNode, TwinWellFormedPairNextToAGroupCommits) {
    DictFreeFixture f;
    ASSERT_NO_FATAL_FAILURE(add_one_instance_group(f.msg, 78));
    ASSERT_EQ(fixpp_msg_set_data(f.msg, 355, as_u8("x"), 1), FIXPP_ERR_OK);
    auto const c = commit_msg(f.msg);
    EXPECT_EQ(c.rc, FIXPP_ERR_OK) << "payload [" << c.shown << "]";
    EXPECT_EQ(c.shown, "35=D|78=1|79=A1|354=1|355=x|");
}

// A message holding only a group tagged 354: as a Length it has no Data after it.
TEST(CapiCommitGroupNode, TwinLonePairLengthGroupIsRefused) {
    DictFreeFixture f;
    ASSERT_NO_FATAL_FAILURE(add_one_instance_group(f.msg, 354));
    auto const c = commit_msg(f.msg);
    EXPECT_EQ(c.rc, FIXPP_ERR_WIRE_CONFORMANCE) << "payload [" << c.shown << "]";
    EXPECT_FALSE(c.has_payload) << "payload [" << c.shown << "]";
}

// A message holding only a group tagged 355: as a Data it is not preceded by its Length.
TEST(CapiCommitGroupNode, TwinLonePairDataGroupIsRefused) {
    DictFreeFixture f;
    ASSERT_NO_FATAL_FAILURE(add_one_instance_group(f.msg, 355));
    auto const c = commit_msg(f.msg);
    EXPECT_EQ(c.rc, FIXPP_ERR_WIRE_CONFORMANCE) << "payload [" << c.shown << "]";
    EXPECT_FALSE(c.has_payload) << "payload [" << c.shown << "]";
}

// 354=1, then an empty group tagged 78, then a one-byte 355: the Length is followed by the
// group's count field, not by its Data, whatever value that count field is fed.
TEST(CapiCommitGroupNode, TwinLengthBeforeAnEmptyNonPairGroupIsRefused) {
    DictFreeFixture f;
    ASSERT_EQ(fixpp_msg_set_int(f.msg, 354, 1), FIXPP_ERR_OK);
    fixpp_group_builder_t* gb = nullptr;
    ASSERT_EQ(fixpp_msg_group_begin(f.msg, 78, &gb), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_msg_group_end(f.msg, gb), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_msg_set_bytes(f.msg, 355, as_u8("x"), 1), FIXPP_ERR_OK);
    auto const c = commit_msg(f.msg);
    EXPECT_EQ(c.rc, FIXPP_ERR_WIRE_CONFORMANCE) << "payload [" << c.shown << "]";
    EXPECT_FALSE(c.has_payload) << "payload [" << c.shown << "]";
}
