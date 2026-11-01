// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_098_inbound_frames.hpp
//
// 098-dialect-overlay, contracts/witnesses.md W-5: the inbound frames W-5's
// sessions are fed, one per US1 scenario plus the enum-closure frame, and the
// constants that name what each frame exercises. Every W-5 session reads its
// frames from here (contracts W-5 names the files that hold those sessions).
//
// The constants are a contract with the dictionaries W-5 loads (contracts W-2):
// the FIX44 overlay under tests/dictionary/fixtures/098/real/, and the edited
// FIX44 that is its hand-written reference, each declare every addition named
// below with exactly the value named here. Conditions on those dictionaries, so
// that each frame is valid on the edited dictionary but for its one scenario
// defect:
//   - an addition a frame meets is Optional, except the one its own scenario is
//     about (the promoted field on its MsgType, and U1's required field);
//   - a Required header or trailer addition is on every MsgType, and no frame
//     here carries one, so the overlay adds none;
//   - the custom fields carry the types named below, which the frame values fit;
//   - FIX44.xml lists a closed set of values on MsgType(35), so the dictionary
//     that declares message U1 also lists U1 among MsgType's values, as a hand
//     edit of FIX44.xml would; otherwise both U1 frames meet 35's enum check.
//
// Every frame is a peer-to-us application message on a FIX.4.4 session whose
// own SenderCompID is kOurCompId, with SendingTime kSendingTime (the fixtures
// set their mock clock to that instant). Each D frame starts from the same
// required NewOrderSingle body, so the D frame the base session accepts shows
// that body complete for every other D frame.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace fixpp::session::test098 {

// ── Session identity and header values ───────────────────────────────────────
inline constexpr std::string_view kBeginString = "FIX.4.4";
inline constexpr std::string_view kPeerCompId = "TW";   // the frames' SenderCompID(49)
inline constexpr std::string_view kOurCompId = "ISLD";  // the frames' TargetCompID(56)
inline constexpr std::string_view kSendingTime = "20240101-00:00:00.000";

// ── Scenario constants (contracts W-2 declares each with exactly these values) ─

// AS-1: a custom field on NewOrderSingle.
inline constexpr std::uint16_t kCustomFieldTag = 5001;
inline constexpr std::string_view kCustomFieldMsgType = "D";
inline constexpr std::string_view kCustomFieldValue = "AS1VALUE";

// AS-2: a custom member of the Parties component's NoPartyIDs(453) group.
inline constexpr std::uint16_t kPartiesMemberTag = 5002;
inline constexpr std::string_view kPartiesMemberMsgType = "D";
inline constexpr std::string_view kPartiesMemberValue = "AS2VALUE";

// AS-3: a private MsgType and the field it declares Required.
inline constexpr std::string_view kPrivateMsgType = "U1";
inline constexpr std::uint16_t kPrivateRequiredTag = 5003;
inline constexpr std::string_view kPrivateRequiredValue = "AS3VALUE";

// AS-4: a custom Length+Data pair whose Data value contains SOH.
inline constexpr std::uint16_t kLengthTag = 5010;
inline constexpr std::uint16_t kDataTag = 5011;
inline constexpr std::string_view kLengthDataMsgType = "D";
// The value is read by its count: the bytes after its SOH contain no '=', so a
// reader that splits the value at its SOH meets a field with no tag.
inline constexpr std::string_view kDataValue{
    "AB\x01"
    "CD",
    5};

// AS-5: an Optional base field promoted to Required on one MsgType and left
// Optional on a second.
inline constexpr std::uint16_t kPromotedTag = 1;           // Account
inline constexpr std::string_view kPromotedMsgType = "F";  // OrderCancelRequest
inline constexpr std::string_view kPromotedStaysOptionalMsgType = "D";

// AS-6: a custom value on OrdType(40).
inline constexpr std::uint16_t kOrdTypeTag = 40;
inline constexpr std::string_view kOrdTypeCustomValue = "Z";
inline constexpr std::string_view kOrdTypeMsgType = "D";

// Enum closure (N-2): its own small overlay lists kEnumClosureListedValue as the
// first value on Text(58), which FIX44.xml declares with no values; the frame
// carries kEnumClosureFrameValue, which that overlay does not list.
inline constexpr std::uint16_t kEnumClosureTag = 58;
inline constexpr std::string_view kEnumClosureMsgType = "D";
inline constexpr std::string_view kEnumClosureListedValue = "LISTED";
inline constexpr std::string_view kEnumClosureFrameValue = "UNLISTED";

// The QuickFIX XML types the custom fields are declared with.
inline constexpr std::string_view kCustomFieldType = "STRING";  // 5001, 5002, 5003
inline constexpr std::string_view kLengthType = "LENGTH";       // 5010
inline constexpr std::string_view kDataType = "DATA";           // 5011

// ── Frame construction ───────────────────────────────────────────────────────

inline std::string field(std::uint32_t tag, std::string_view value) {
    std::string out = std::to_string(tag);
    out += '=';
    out.append(value.data(), value.size());
    out += '\x01';
    return out;
}

// Wraps `body_fields` (every field after the standard header) in the header
// fields 8, 9, 35, 34, 49, 52 and 56, and the trailing CheckSum(10).
inline std::vector<std::byte> make_frame(std::string_view msg_type, std::uint32_t seq,
                                         std::string_view body_fields) {
    std::string body = field(35, msg_type);
    body += field(34, std::to_string(seq));
    body += field(49, kPeerCompId);
    body += field(52, kSendingTime);
    body += field(56, kOurCompId);
    body.append(body_fields.data(), body_fields.size());
    std::string full = field(8, kBeginString);
    full += field(9, std::to_string(body.size()));
    full += body;
    unsigned int cs = 0;
    for (unsigned char const c : full) {
        cs += c;
    }
    char csbuf[4];
    std::snprintf(csbuf, sizeof(csbuf), "%03u", cs & 0xFFU);
    full += field(10, csbuf);
    std::vector<std::byte> frame;
    frame.reserve(full.size());
    for (char const c : full) {
        frame.push_back(static_cast<std::byte>(c));
    }
    return frame;
}

// The NewOrderSingle body every D frame starts from: FIX44.xml's Required
// NewOrderSingle fields, OrdType last so a frame can choose its value.
inline std::string nos_body(std::string_view ord_type) {
    std::string b = field(11, "ORD098");
    b += field(54, "1");
    b += field(60, kSendingTime);
    b += field(kOrdTypeTag, ord_type);
    return b;
}

// AS-1: 35=D carrying 5001.
inline std::vector<std::byte> frame_as1_custom_field(std::uint32_t seq) {
    return make_frame(kCustomFieldMsgType, seq,
                      nos_body("1") + field(kCustomFieldTag, kCustomFieldValue));
}

// AS-2: 35=D with one Parties entry carrying 5002 after its standard members.
inline std::vector<std::byte> frame_as2_parties_member(std::uint32_t seq) {
    std::string b = nos_body("1");
    b += field(453, "1");
    b += field(448, "PARTY098");
    b += field(447, "D");
    b += field(452, "1");
    b += field(kPartiesMemberTag, kPartiesMemberValue);
    return make_frame(kPartiesMemberMsgType, seq, b);
}

// AS-3: U1 with its Required field, and without it.
inline std::vector<std::byte> frame_as3_private_with_required(std::uint32_t seq) {
    return make_frame(kPrivateMsgType, seq, field(kPrivateRequiredTag, kPrivateRequiredValue));
}
inline std::vector<std::byte> frame_as3_private_missing_required(std::uint32_t seq) {
    return make_frame(kPrivateMsgType, seq, "");
}

// AS-4: 35=D carrying 5010=<count of kDataValue> and 5011=kDataValue.
inline std::vector<std::byte> frame_as4_length_data(std::uint32_t seq) {
    std::string b = nos_body("1");
    b += field(kLengthTag, std::to_string(kDataValue.size()));
    b += field(kDataTag, kDataValue);
    return make_frame(kLengthDataMsgType, seq, b);
}

// AS-5: the promoted field absent on the MsgType it is promoted on, and absent on
// the MsgType where it stays Optional.
inline std::vector<std::byte> frame_as5_promoted_missing(std::uint32_t seq) {
    std::string b = field(41, "ORIG098");
    b += field(11, "CXL098");
    b += field(54, "1");
    b += field(60, kSendingTime);
    return make_frame(kPromotedMsgType, seq, b);
}
inline std::vector<std::byte> frame_as5_other_msgtype(std::uint32_t seq) {
    return make_frame(kPromotedStaysOptionalMsgType, seq, nos_body("1"));
}

// AS-6: 35=D with OrdType(40)=Z.
inline std::vector<std::byte> frame_as6_ordtype_custom(std::uint32_t seq) {
    return make_frame(kOrdTypeMsgType, seq, nos_body(kOrdTypeCustomValue));
}

// Enum closure: 35=D with Text(58) carrying a value the closure overlay does not
// list.
inline std::vector<std::byte> frame_enum_closure(std::uint32_t seq) {
    return make_frame(kEnumClosureMsgType, seq,
                      nos_body("1") + field(kEnumClosureTag, kEnumClosureFrameValue));
}

}  // namespace fixpp::session::test098
