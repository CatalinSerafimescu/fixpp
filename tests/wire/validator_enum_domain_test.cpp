// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/validator_enum_domain_test.cpp
//
// 075-live-wire-enum-validation T022/T022a/T023 (US1).
//
// Witnesses the live enum domain check through the PRODUCTION path — real
// shipped dictionaries (FIX44.xml, FIXT11.xml) -> Dictionary::as_table_view()
// -> dictionary_driven_validator::validate() / validate_field().
//
// Anchors:
//   spec: specs/075-live-wire-enum-validation/spec.md FR-003/FR-006/FR-008/
//         FR-015/FR-023, SC-001/SC-004/SC-009
//   tasks: specs/075-live-wire-enum-validation/tasks.md T022/T022a/T023
//
// RefTagID note (075 T020a, FR-006 delivery): `dictionary_driven_validator::
// validate()` now threads the offending tag out via a required
// `std::uint16_t* ref_tag_out` parameter (see include/fixpp/wire/
// validator.hpp), and `session.cpp`'s `validate_inbound_` reads it into
// `RejectDecision::ref_tag_id`, which the session emits as the outbound
// Reject's tag 371. Below, "RefTagID=54" etc. is asserted by reading the
// LITERAL value written to `ref_tag_out`, not "by construction" — see also
// the session-level 371-on-the-wire witnesses in
// tests/session/test_validate_gate_inbound.cpp (W2/W3/W4), which prove the
// tag actually reaches the emitted frame, not just that validate() reports
// it.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/field_type.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/reject_reason_map.hpp>
#include <fixpp/wire/validator.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/frame_view_factory.hpp"

namespace {

using fixpp::core::error;
using fixpp::wire::access_mode;
using fixpp::wire::dictionary_driven_validator;
using fixpp::wire::MessageView;
using fixpp::wire::Parser;
using fixpp::wire::wire_error_to_session_reject_reason;

// ── shared frame-building helpers (mirrors
//    validator_production_table_view_test.cpp) ────────────────────────────

std::string make_checksum_field(unsigned chk) {
    std::string s = "10=";
    s.push_back(static_cast<char>('0' + ((chk / 100U) % 10U)));
    s.push_back(static_cast<char>('0' + ((chk / 10U) % 10U)));
    s.push_back(static_cast<char>('0' + (chk % 10U)));
    s.push_back('\x01');
    return s;
}

std::vector<std::byte> make_frame(std::string_view begin_string, std::string_view body_fields) {
    std::string body{body_fields};
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string pre = "8=" + std::string{begin_string} + "\x01" + nine + body;
    unsigned sum = 0;
    for (unsigned char c : pre) sum += c;
    std::string full = pre + make_checksum_field(sum % 256U);
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

std::vector<std::byte> make_fix44_frame(std::string_view body_fields) {
    return make_frame("FIX.4.4", body_fields);
}

MessageView<access_mode::Index> parse_index(std::vector<std::byte> const& buf,
                                            std::array<std::byte, 4096>& stack_buf,
                                            std::pmr::monotonic_buffer_resource& arena_out) {
    new (&arena_out) std::pmr::monotonic_buffer_resource{stack_buf.data(), stack_buf.size(),
                                                         std::pmr::null_memory_resource()};
    auto fv = fixpp::wire::test::make_frame_view(buf);
    if (!fv.has_value()) {
        ADD_FAILURE() << "make_frame_view failed";
        return {};
    }
    Parser<access_mode::Index> parser{};
    auto mv = parser.parse(*fv, &arena_out);
    if (!mv.has_value()) {
        ADD_FAILURE() << "parser.parse failed";
        return {};
    }
    return std::move(*mv);
}

// Loads a real shipped dictionary and returns its table_view. Per
// dictionary.hpp:193-205, as_table_view() legally outlives the Dictionary it
// was built from (the enum-domain table OWNS copies of the code bytes), so
// the Dictionary going out of scope at the end of this function is safe —
// only the pmr arena backing `buf` must outlive the *loader call*, which it
// does (declared in this function, alive through `as_table_view()`).
fixpp::dict::table_view load_shipped_table_view(char const* filename) {
    std::vector<std::byte> buf(8u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};
    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / filename;
    auto dict = fixpp::dict::XmlLoader{}.load(path, &mr);
    return dict.as_table_view();
}

std::span<const std::byte> as_bytes(std::string_view s) {
    return {reinterpret_cast<std::byte const*>(s.data()), s.size()};
}

// NewOrderSingle(D) required fields beyond the header: ClOrdID(11), Side(54),
// TransactTime(60), OrdType(40) — measured via the production table_view
// (Instrument/OrderQtyData are required COMPONENTS but declare zero required
// LEAF fields, matching tools/quickfix_enum_golden/main.cpp's nos_required).
// `side_value` is inserted last so every field before it is known-valid —
// the offending-tag-by-construction argument for the Side(54) tests below.
constexpr std::string_view kSendingTime = "20260714-12:00:00";

std::string nos_prefix() {
    std::string s = "35=D\x01";
    s += "49=SENDER\x01";
    s += "56=TARGET\x01";
    s += "34=1\x01";
    s += "52=";
    s += kSendingTime;
    s += "\x01";
    s += "11=CLORDID1\x01";
    s += "40=1\x01";  // OrdType=Market, in-domain (measured)
    s += "60=";
    s += kSendingTime;
    s += "\x01";
    return s;
}

}  // namespace

// ── T022: Side(54)=Z out-of-domain -> reject/5, RefTagID=54 ────────────────
// AC US1 #1/#2, SC-001/FR-006. RefTagID is asserted by reading the literal
// value validate() writes to ref_tag_out (075 T020a).
TEST(ValidatorEnumDomain, Side54OutOfDomainRejectsReason5) {
    auto tv = load_shipped_table_view("FIX44.xml");
    dictionary_driven_validator v{std::move(tv)};

    auto body = nos_prefix() + "54=Z\x01";
    auto frame = make_fix44_frame(body);
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(frame, stack, arena);

    std::array<std::byte, 2048> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};
    std::uint16_t ref_tag = 0;
    auto result = v.validate(mv, &scratch_mr, &ref_tag);
    ASSERT_FALSE(result.has_value()) << "Side(54)=Z is not one of the 16 declared FIX44 codes "
                                        "(1-9,A-G) and must reject";
    EXPECT_EQ(result.error(), error::wire_field_value_out_of_range)
        << "got slot " << static_cast<int>(result.error());
    EXPECT_EQ(wire_error_to_session_reject_reason(result.error()), 5)
        << "wire_field_value_out_of_range must map to SessionRejectReason 5";
    EXPECT_EQ(ref_tag, 54) << "FR-006: RefTagID must name the offending tag (Side/54)";
}

TEST(ValidatorEnumDomain, Side54InDomainAccepts) {
    auto tv = load_shipped_table_view("FIX44.xml");
    dictionary_driven_validator v{std::move(tv)};

    auto body = nos_prefix() + "54=1\x01";
    auto frame = make_fix44_frame(body);
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(frame, stack, arena);

    std::array<std::byte, 2048> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};
    auto result = v.validate(mv, &scratch_mr, nullptr);
    EXPECT_TRUE(result.has_value()) << "Side(54)=1 is a declared code and must accept; slot "
                                     << (result.has_value() ? -1 : static_cast<int>(result.error()));
}

// ── T022: ClOrdID(11) declares ZERO codes -> accept regardless of content ──
// SC-004: the absent-tag/empty-codeset accept floor (FR-003), pinned
// DIRECTLY via validate_field() rather than incidentally through a message
// that would accept anyway. ClOrdID is field_type::String (unconstrained by
// the type arm), so a garbage value can ONLY be caught by the enum arm — an
// ACCEPT here can only come from the Floor-1 accept path.
TEST(ValidatorEnumDomain, ClOrdIDZeroCodesAcceptsRegardlessOfContent) {
    auto tv = load_shipped_table_view("FIX44.xml");
    dictionary_driven_validator v{std::move(tv)};

    std::string_view const garbage = "!!!NOT_A_DECLARED_CODE_999###";
    auto rc = v.validate_field(11, as_bytes(garbage));
    EXPECT_TRUE(rc.has_value())
        << "ClOrdID(11) declares zero codes (measured) -- Floor 1 must accept any content; slot "
        << (rc.has_value() ? -1 : static_cast<int>(rc.error()));
}

// ── T022: FIXT11's 8 message types all accept via the empty-set arm ────────
// FR-016's load-bearing dependency on FR-003: FIXT11's MsgType(35) declares
// ZERO <value> children (measured), so every declared message type must
// accept via Floor 1.
TEST(ValidatorEnumDomain, Fixt11MsgTypeAllEightAcceptViaEmptySetFloor) {
    auto tv = load_shipped_table_view("FIXT11.xml");
    dictionary_driven_validator v{std::move(tv)};

    // The 8 msgtype codes declared in FIXT11.xml's <messages> (measured).
    constexpr std::array<std::string_view, 8> kMsgTypes{"0", "1", "2", "3", "4", "5", "A", "n"};
    for (auto const mt : kMsgTypes) {
        auto rc = v.validate_field(35, as_bytes(mt));
        EXPECT_TRUE(rc.has_value())
            << "FIXT11 MsgType(35)=" << mt
            << " must accept via the empty-codeset floor (FIXT11's MsgType declares 0 codes)";
    }
}

// ── T022 (FR-015): header field PossDupFlag(43)=X -> reject/5, RefTagID=43 ─
// PossDupFlag is BOOLEAN, whose type arm imposes NO constraint
// (validator.hpp:419-425) -- so ONLY the enum arm can catch "X". A
// body-only Step-1 walk would silently accept this (mutation c below).
TEST(ValidatorEnumDomain, PossDupFlag43HeaderFieldOutOfDomainRejectsReason5) {
    auto tv = load_shipped_table_view("FIX44.xml");
    dictionary_driven_validator v{std::move(tv)};

    // Logon(A): header + EncryptMethod(98)=0 [in-domain] + HeartBtInt(108)=30
    // [unconstrained INT] + PossDupFlag(43)=X [the offending header field].
    std::string body = "35=A\x01";
    body += "49=SENDER\x01";
    body += "56=TARGET\x01";
    body += "34=1\x01";
    body += "52=";
    body += kSendingTime;
    body += "\x01";
    body += "98=0\x01";
    body += "108=30\x01";
    body += "43=X\x01";
    auto frame = make_fix44_frame(body);
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(frame, stack, arena);

    std::array<std::byte, 2048> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};
    std::uint16_t ref_tag = 0;
    auto result = v.validate(mv, &scratch_mr, &ref_tag);
    ASSERT_FALSE(result.has_value())
        << "PossDupFlag(43)=X is not Y/N -- a body-only walk would silently accept this "
           "(FR-015/SC-009), and validate() must reject it";
    EXPECT_EQ(result.error(), error::wire_field_value_out_of_range)
        << "got slot " << static_cast<int>(result.error());
    EXPECT_EQ(wire_error_to_session_reject_reason(result.error()), 5);
    EXPECT_EQ(ref_tag, 43) << "FR-006: RefTagID must name the offending header tag (PossDupFlag/43)";
}

// ═══ T022a: the empty-value witness (FR-008, DV-1/DV-2) ════════════════════
// Nothing in the tree pins the empty x String accept case today; a missing
// Floor-2 bypass would leave the pre-existing empty x Char pin
// (validator_type_check_test.cpp:387-396) green while silently regressing
// ExecInst(18)=.

// DV-1: Side(54)="" (empty x Char) -> reject, but via the TYPE arm
// (value.size() != 1), not the enum arm. Parity with QuickFIX by coincidence.
// RefTagID assertion is a 075 T020a side benefit: the TYPE arm now carries
// RefTagID too (pre-existing 041-era type-arm rejects previously shipped
// without 371 at all).
TEST(ValidatorEnumDomain, EmptyValueCharRejectsViaTypeArmNotEnumArm) {
    auto tv = load_shipped_table_view("FIX44.xml");
    dictionary_driven_validator v{std::move(tv)};

    auto body = nos_prefix() + "54=\x01";
    auto frame = make_fix44_frame(body);
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(frame, stack, arena);

    std::array<std::byte, 2048> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};
    std::uint16_t ref_tag = 0;
    auto result = v.validate(mv, &scratch_mr, &ref_tag);
    ASSERT_FALSE(result.has_value()) << "Side(54)= (empty x Char) must reject (DV-1)";
    EXPECT_EQ(result.error(), error::wire_field_value_out_of_range)
        << "got slot " << static_cast<int>(result.error());
    EXPECT_EQ(ref_tag, 54)
        << "FR-006 side benefit: the TYPE arm must also carry RefTagID (Side/54)";
}

// DV-2: ExecInst(18)="" (empty x String) -> ACCEPT. This is the assertion
// that does the work -- a missing Floor-2 empty-value bypass flips this to
// reject (mutation below), while EmptyValueCharRejectsViaTypeArmNotEnumArm
// stays green either way (its reject comes from the type arm regardless).
TEST(ValidatorEnumDomain, EmptyValueStringAcceptsViaBypassedEnumCheck) {
    auto tv = load_shipped_table_view("FIX44.xml");
    dictionary_driven_validator v{std::move(tv)};

    auto body = nos_prefix() + "54=1\x01" + "18=\x01";
    auto frame = make_fix44_frame(body);
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(frame, stack, arena);

    std::array<std::byte, 2048> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};
    auto result = v.validate(mv, &scratch_mr, nullptr);
    EXPECT_TRUE(result.has_value())
        << "ExecInst(18)= (empty x String) must ACCEPT (DV-2): the enum check is bypassed "
           "on empty values (FR-008) and the String type arm imposes no constraint; slot "
        << (result.has_value() ? -1 : static_cast<int>(result.error()));
}

// ═══ T023: the DV-3 pin -- group-member and nested-group-member enums ══════
// MUST drive the group path through validate() / the Step-1 walk, NOT
// validate_field() (which has no message context -- FR-020, a separate
// surface). Frames mirror tools/quickfix_enum_golden/main.cpp rows 10/11.

// Depth-1: NoPartyIDs(453) -> PartyRole(452) out-of-domain member.
TEST(ValidatorEnumDomain, GroupMemberOutOfDomainRejectsReason5) {
    auto tv = load_shipped_table_view("FIX44.xml");
    dictionary_driven_validator v{std::move(tv)};

    auto body = nos_prefix() + "54=1\x01";
    body += "453=1\x01";  // NoPartyIDs=1
    body += "448=PID1\x01";  // PartyID (delimiter)
    body += "447=D\x01";     // PartyIDSource -- valid
    body += "452=9999\x01";  // PartyRole -- out-of-domain (valid set is 1-38)
    auto frame = make_fix44_frame(body);
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(frame, stack, arena);

    std::array<std::byte, 2048> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};
    std::uint16_t ref_tag = 0;
    auto result = v.validate(mv, &scratch_mr, &ref_tag);
    ASSERT_FALSE(result.has_value())
        << "PartyRole(452)=9999 inside a NoPartyIDs(453) group member must reject (DV-3): "
           "fixpp enum-checks group members at every depth (QuickFIX never does)";
    EXPECT_EQ(result.error(), error::wire_field_value_out_of_range)
        << "got slot " << static_cast<int>(result.error());
    EXPECT_EQ(wire_error_to_session_reject_reason(result.error()), 5);
    EXPECT_EQ(ref_tag, 452)
        << "FR-006: RefTagID must name the offending GROUP MEMBER tag (PartyRole/452), not the "
           "group's count tag (NoPartyIDs/453)";
}

// Same shape but a fully in-domain group must still accept (paired positive
// control -- otherwise a reject-everything-in-groups regression would leave
// this suite green).
TEST(ValidatorEnumDomain, GroupMemberInDomainAccepts) {
    auto tv = load_shipped_table_view("FIX44.xml");
    dictionary_driven_validator v{std::move(tv)};

    auto body = nos_prefix() + "54=1\x01";
    body += "453=1\x01";
    body += "448=PID1\x01";
    body += "447=D\x01";
    body += "452=1\x01";  // PartyRole -- in-domain
    auto frame = make_fix44_frame(body);
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(frame, stack, arena);

    std::array<std::byte, 2048> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};
    auto result = v.validate(mv, &scratch_mr, nullptr);
    EXPECT_TRUE(result.has_value())
        << "a fully in-domain group must accept; slot "
        << (result.has_value() ? -1 : static_cast<int>(result.error()));
}

// Depth-2: NoPartyIDs(453) -> NoPartySubIDs(802) -> PartySubIDType(803)
// out-of-domain nested-group member.
TEST(ValidatorEnumDomain, NestedGroupMemberOutOfDomainRejectsReason5) {
    auto tv = load_shipped_table_view("FIX44.xml");
    dictionary_driven_validator v{std::move(tv)};

    auto body = nos_prefix() + "54=1\x01";
    body += "453=1\x01";
    body += "448=PID1\x01";
    body += "447=D\x01";
    body += "452=1\x01";      // PartyRole -- valid
    body += "802=1\x01";      // NoPartySubIDs=1 (nested group)
    body += "523=SUB1\x01";   // PartySubID (nested delimiter)
    body += "803=999\x01";    // PartySubIDType -- out-of-domain (valid set is 1-26)
    auto frame = make_fix44_frame(body);
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(frame, stack, arena);

    std::array<std::byte, 2048> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};
    std::uint16_t ref_tag = 0;
    auto result = v.validate(mv, &scratch_mr, &ref_tag);
    ASSERT_FALSE(result.has_value())
        << "PartySubIDType(803)=999 inside a depth-2 nested group member must reject (DV-3, "
           "at depth): pins that fixpp's flat Step-1 walk reaches every depth";
    EXPECT_EQ(result.error(), error::wire_field_value_out_of_range)
        << "got slot " << static_cast<int>(result.error());
    EXPECT_EQ(wire_error_to_session_reject_reason(result.error()), 5);
    EXPECT_EQ(ref_tag, 803)
        << "FR-006: RefTagID must name the offending NESTED group member tag (PartySubIDType/803)";
}

TEST(ValidatorEnumDomain, NestedGroupMemberInDomainAccepts) {
    auto tv = load_shipped_table_view("FIX44.xml");
    dictionary_driven_validator v{std::move(tv)};

    auto body = nos_prefix() + "54=1\x01";
    body += "453=1\x01";
    body += "448=PID1\x01";
    body += "447=D\x01";
    body += "452=1\x01";
    body += "802=1\x01";
    body += "523=SUB1\x01";
    body += "803=1\x01";  // PartySubIDType -- in-domain
    auto frame = make_fix44_frame(body);
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(frame, stack, arena);

    std::array<std::byte, 2048> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};
    auto result = v.validate(mv, &scratch_mr, nullptr);
    EXPECT_TRUE(result.has_value())
        << "a fully in-domain depth-2 nested group must accept; slot "
        << (result.has_value() ? -1 : static_cast<int>(result.error()));
}

// ═══ T020a: Step-3 group-structure failure -- RefTagID = the group's own
// delimiter tag (the known-missing field), NOT the group's NoXXX count tag ═
// NoPartyIDs(453)=1 declares one PartyID group instance, but the delimiter
// PartyID(448) never appears -- consume_group's "first instance must open
// with the delimiter" branch (validator.hpp) fires wire_required_field_missing
// (reason=1). RefTagID must name 448 (the missing delimiter), not 453 (the
// count field, which IS present).
TEST(ValidatorEnumDomain, GroupMissingDelimiterRejectsReason1RefTagIsDelimiter) {
    auto tv = load_shipped_table_view("FIX44.xml");
    dictionary_driven_validator v{std::move(tv)};

    auto body = nos_prefix() + "54=1\x01";
    body += "453=1\x01";  // NoPartyIDs=1, but PartyID(448) never follows.
    auto frame = make_fix44_frame(body);
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(frame, stack, arena);

    std::array<std::byte, 2048> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};
    std::uint16_t ref_tag = 0;
    auto result = v.validate(mv, &scratch_mr, &ref_tag);
    ASSERT_FALSE(result.has_value())
        << "NoPartyIDs(453)=1 with no PartyID(448) delimiter must reject";
    EXPECT_EQ(result.error(), error::wire_required_field_missing)
        << "got slot " << static_cast<int>(result.error());
    EXPECT_EQ(wire_error_to_session_reject_reason(result.error()), 1);
    EXPECT_EQ(ref_tag, 448)
        << "FR-006: Step-3 group-structure RefTagID must name the missing delimiter "
           "(PartyID/448), not the count tag (NoPartyIDs/453)";
}
