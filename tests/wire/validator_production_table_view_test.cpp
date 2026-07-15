// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/validator_production_table_view_test.cpp
//
// T009 compile-witness + T009a Float parse-error remap tests.
//
// Purpose:
//  (1) Prove that `dictionary_driven_validator` instantiates from a PRODUCTION
//      `dict::table_view` (built by `Dictionary::as_table_view()`) WITHOUT any
//      include of the test mock — closing RC-A. This is the RC-A compile-
//      witness (no mock included in this TU).
//  (2) T009a: Float garbage value (e.g. "abc") → validate() returns
//      `wire_field_value_out_of_range` (slot 40, SessionRejectReason=5),
//      NOT a raw decimal error (decimal_invalid_input=10). Proves the remap
//      in `validator.hpp:307-313` (data-model E-4 / FR-004).
//
// Anchors:
//   spec: 041-validation-gate-wiring/spec.md FR-003/FR-004
//   data-model: E-2 / E-2a / E-4 (Float parse-error remapping note)
//   research: R-1 / R-1a
//   contracts: C-1

// NOTE: NO mock_dict_table.hpp include — this TU is the production-path
// compile-witness. The complete type comes from the production headers.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/field_type.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/validator.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/frame_view_factory.hpp"

namespace {

using fixpp::core::error;
using fixpp::dict::field_type;
using fixpp::dict::table_view;
using fixpp::wire::access_mode;
using fixpp::wire::dictionary_driven_validator;
using fixpp::wire::MessageView;
using fixpp::wire::Parser;

// Load a tiny dictionary with an Int field (tag 98) and a Float field (tag 38).
fixpp::dict::Dictionary load_tiny_dict(std::pmr::memory_resource* mr) {
    constexpr std::string_view kXml = R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
                                      R"(<fields>)"
                                      R"(<field number='8'  name='BeginString' type='STRING'/>)"
                                      R"(<field number='9'  name='BodyLength'  type='LENGTH'/>)"
                                      R"(<field number='10' name='CheckSum'    type='STRING'/>)"
                                      R"(<field number='35' name='MsgType'     type='STRING'/>)"
                                      R"(<field number='38' name='OrderQty'    type='QTY'/>)"
                                      R"(<field number='98' name='EncryptMethod' type='INT'/>)"
                                      R"(</fields>)"
                                      R"(<messages>)"
                                      R"(<message name='Logon' msgtype='A' msgcat='admin'>)"
                                      R"(  <field name='BeginString'   required='N'/>)"
                                      R"(  <field name='BodyLength'    required='N'/>)"
                                      R"(  <field name='CheckSum'      required='N'/>)"
                                      R"(  <field name='MsgType'       required='N'/>)"
                                      R"(  <field name='EncryptMethod' required='N'/>)"
                                      R"(  <field name='OrderQty'      required='N'/>)"
                                      R"(</message>)"
                                      R"(</messages>)"
                                      R"(</fix>)";
    return fixpp::dict::XmlLoader{}.load_from_string(kXml, mr);
}

// Dictionary with a repeating group whose DECLARATION delimiter is NOT its
// lowest-tag member: NoThings(100) → [delimiter ThingA(200), then ThingB(150)].
// message_fields() is tag-sorted (xml_loader.cpp), so the per-context member
// list is [150, 200] and members.front() is 150 — NOT the real delimiter 200.
// Mirrors real dicts (e.g. FIX44 NoPartyIDs(453): delimiter PartyID(448) but
// lowest member PartyIDSource(447)).
fixpp::dict::Dictionary load_group_dict(std::pmr::memory_resource* mr) {
    constexpr std::string_view kXml = R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
                                      R"(<fields>)"
                                      R"(<field number='8'  name='BeginString' type='STRING'/>)"
                                      R"(<field number='9'  name='BodyLength'  type='LENGTH'/>)"
                                      R"(<field number='10' name='CheckSum'    type='STRING'/>)"
                                      R"(<field number='35' name='MsgType'     type='STRING'/>)"
                                      R"(<field number='100' name='NoThings'   type='NUMINGROUP'/>)"
                                      R"(<field number='150' name='ThingB'     type='INT'/>)"
                                      R"(<field number='200' name='ThingA'     type='STRING'/>)"
                                      R"(</fields>)"
                                      R"(<messages>)"
                                      R"(<message name='Things' msgtype='U' msgcat='app'>)"
                                      R"(  <field name='BeginString'   required='N'/>)"
                                      R"(  <field name='BodyLength'    required='N'/>)"
                                      R"(  <field name='CheckSum'      required='N'/>)"
                                      R"(  <field name='MsgType' required='N'/>)"
                                      R"(  <group name='NoThings' required='N'>)"
                                      R"(    <field name='ThingA' required='N'/>)"  // 200 = delimiter
                                      R"(    <field name='ThingB' required='N'/>)"  // 150
                                      R"(  </group>)"
                                      R"(</message>)"
                                      R"(</messages>)"
                                      R"(</fix>)";
    return fixpp::dict::XmlLoader{}.load_from_string(kXml, mr);
}

// Build a complete FIX frame from body fields (8/9/10 framing added).
std::string make_checksum_field(unsigned chk) {
    std::string s = "10=";
    s.push_back(static_cast<char>('0' + ((chk / 100U) % 10U)));
    s.push_back(static_cast<char>('0' + ((chk / 10U) % 10U)));
    s.push_back(static_cast<char>('0' + (chk % 10U)));
    s.push_back('\x01');
    return s;
}

std::vector<std::byte> make_frame(std::string_view body_fields) {
    std::string body{body_fields};
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string pre = "8=FIX.4.4\x01" + nine + body;
    unsigned sum = 0;
    for (unsigned char c : pre) sum += c;
    std::string full = pre + make_checksum_field(sum % 256U);
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
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

}  // namespace

// ── T009-1: compile-witness — instantiate from production table_view ─────────
// If this TU compiles without the mock, RC-A is closed: the production path
// no longer depends on test/support/mock_dict_table.hpp being included first.

TEST(ValidatorProductionTableView, InstantiatesFromProductionTableView) {
    std::vector<std::byte> buf(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};
    auto dict = load_tiny_dict(&mr);
    auto tv = dict.as_table_view();

    // Constructing dictionary_driven_validator from a PRODUCTION table_view.
    dictionary_driven_validator v{std::move(tv)};

    // Basic sanity: field_valid_for via the production path.
    EXPECT_TRUE(v.field_valid_for("A", 98))
        << "EncryptMethod(98) must be valid for Logon via production table_view";
    EXPECT_FALSE(v.field_valid_for("A", 9999)) << "unknown tag must not be valid for Logon";
}

// ── Regression: group delimiter must come from the WIRE, not the tag-sorted
//    member list (063 tag-sort delimiter bug) ──────────────────────────────
// The per-context store's group_first is set from members.front(), but
// members come from tag-sorted message_fields(), so it is the lowest-tag
// member (150), not the declaration delimiter (200). A valid single-instance
// group [delimiter 200, then 150] must VALIDATE. Pre-fix, validate() used the
// stored group_first(=150) as the expected delimiter and rejected ents[i+1]=200
// with wire_required_field_missing — a false rejection of a valid real-dict
// group (e.g. any FIX44 message carrying a NoPartyIDs(453) Parties group).
TEST(ValidatorProductionTableView, GroupDelimiterFromWireNotTagSortedMember) {
    std::vector<std::byte> buf(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};
    auto dict = load_group_dict(&mr);
    auto tv = dict.as_table_view();
    dictionary_driven_validator v{std::move(tv)};

    // One valid instance: delimiter ThingA(200) first, then ThingB(150).
    auto frame = make_frame(
        "35=U\x01"
        "100=1\x01"
        "200=x\x01"
        "150=7\x01");
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(frame, stack, arena);

    std::array<std::byte, 2048> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};

    auto result = v.validate(mv, &scratch_mr, nullptr);
    EXPECT_TRUE(result.has_value())
        << "valid single-instance group (delimiter 200 first) must validate; "
        << "pre-fix the validator used tag-sorted members.front()=150 as the "
        << "expected delimiter and rejected the real delimiter 200; slot "
        << (result.has_value() ? -1 : static_cast<int>(result.error()));
}

// ── Gate B r1 (PR #194 FIX 1.a): valid_tags_for nullptr-equivalence ─────────
// through validate(). An unknown MsgType(35) must reject the SAME way the
// pre-hoist per-field field_valid_for(msg_type, tag) path would: the hoisted
// dict_.valid_tags_for(msg_type) view's contains() must be false for every
// tag when msg_type is unknown, exactly like field_valid_for(msg_type, tag)
// returning false for every tag. Frame carries a syntactically valid Logon
// body (98=0) under an unregistered MsgType "Z" (only "A"/Logon is declared
// in load_tiny_dict) — the body field IS a known tag (for "A"), so a bug that
// silently fell back to "any known tag accepts" would NOT be caught by an
// all-unknown-tags frame; this frame's body fields are otherwise-known tags,
// per the brief.
TEST(ValidatorProductionTableView, UnknownMsgTypeRejectsLikePreHoistFieldValidFor) {
    std::vector<std::byte> buf(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};
    auto dict = load_tiny_dict(&mr);
    auto tv_direct = dict.as_table_view();  // separate copy for the direct assertion below
    auto tv = dict.as_table_view();         // consumed by the validator below
    dictionary_driven_validator v{std::move(tv)};

    // "Z" is not a declared msg_type in load_tiny_dict; 98 IS a known tag
    // (EncryptMethod, declared for Logon "A").
    auto frame = make_frame(
        "35=Z\x01"
        "98=0\x01");
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(frame, stack, arena);

    std::array<std::byte, 2048> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};

    std::uint16_t ref_tag = 0;
    auto result = v.validate(mv, &scratch_mr, &ref_tag);
    ASSERT_FALSE(result.has_value())
        << "an unregistered MsgType must reject every field via validate()";
    EXPECT_EQ(result.error(), error::wire_unexpected_tag)
        << "an unregistered MsgType must reject with wire_unexpected_tag, exactly as the "
           "pre-hoist per-field field_valid_for(msg_type, tag) path would for every tag";
    // Direct equivalence: the reported offending tag must ALSO be reported as
    // invalid by the un-hoisted field_valid_for("Z", ref_tag) — proving the
    // hoisted valid_tags_for(...) view and the per-field lookup agree exactly
    // at the tag validate() actually rejected on.
    EXPECT_FALSE(tv_direct.field_valid_for("Z", ref_tag))
        << "the tag validate() rejected (" << ref_tag
        << ") must ALSO be reported invalid by the un-hoisted field_valid_for path";
    // Gate B r2 (P2): pin the EXACT offending tag, not just "some tag that is
    // also invalid" (tag 0 — ref_tag's zero-init — would ALSO satisfy the
    // field_valid_for("Z", ref_tag) check above for unknown "Z", so that
    // assertion alone does not prove validate() actually wrote *ref_tag_out*
    // on this path). make_frame's body is "35=Z\x01" "98=0\x01" prefixed with
    // "8=FIX.4.4\x01" + the 9= length field; Step-1 walks msg.begin()..end()
    // from byte 0, so the FIRST present field is BeginString(8) itself — the
    // very first field validate() examines against the (empty, since "Z" is
    // unknown) valid_tags view, hence the first (and only, since validate()
    // returns on the first failure) rejection.
    EXPECT_EQ(ref_tag, std::uint16_t{8})
        << "validate() must report the FIRST field it examined (BeginString=8) as the "
           "offending tag, not merely 'some tag that is also invalid for msg_type Z'";
}

// ── T009a-1 RED→GREEN: Float garbage value → wire_field_value_out_of_range ───
// Data-model E-4 / FR-004 Float parse-error remap:
//   decimal_t::parse("abc") → decimal_invalid_input (slot 10) — a non-wire_*
//   error. The validator MUST remap to wire_field_value_out_of_range (slot 40)
//   so validate() never leaks raw decimal errors.
//
// This test is RED before T009a's remap and GREEN after.

TEST(ValidatorProductionTableView, FloatGarbageValueRemappedToWireOutOfRange) {
    std::vector<std::byte> buf(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};
    auto dict = load_tiny_dict(&mr);
    auto tv = dict.as_table_view();
    dictionary_driven_validator v{std::move(tv)};

    // OrderQty(38) is QTY → field_type::Float.
    // Value "abc" is not a valid decimal → decimal_invalid_input before fix,
    // wire_field_value_out_of_range after fix (T009a).
    auto frame = make_frame(
        "35=A\x01"
        "38=abc\x01");
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(frame, stack, arena);

    std::array<std::byte, 2048> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};

    auto result = v.validate(mv, &scratch_mr, nullptr);
    ASSERT_FALSE(result.has_value()) << "validate() with garbage Float value must return an error";
    EXPECT_EQ(result.error(), error::wire_field_value_out_of_range)
        << "garbage Float must map to wire_field_value_out_of_range (slot 40), "
        << "not a raw decimal error (slot 10 = decimal_invalid_input); "
        << "got slot " << static_cast<int>(result.error());
}

// ── T009a-2: any Float parse error maps to a wire_* slot (not a raw decimal)──
// Structural invariant (E-4): whatever error the Float arm returns from
// validate_field(), it MUST be a wire_* slot (38–42), never a raw decimal
// error slot (10/11/12). This witnesses the invariant across two input shapes:
//   (a) garbage bytes ("abc") → decimal_invalid_input → wire_field_value_out_of_range
//   (b) a valid decimal with many sig digits → may succeed OR return a wire_* error
//
// NOTE: the decimal_precision_loss → wire_field_value_truncated path requires
// the decimal implementation to emit decimal_precision_loss (slot 12); whether
// that fires depends on decimal_t's internal precision semantics. Since the
// path is already covered in validator_type_check_test.cpp (FloatFieldValid*
// tests via the mock) and the unit we're testing here is the production
// table_view integration, we test the structural invariant instead.

TEST(ValidatorProductionTableView, FloatParseErrorAlwaysMapsToWireSlot) {
    table_view tv;
    tv.set_field_type(38, field_type::Float);
    dictionary_driven_validator v{std::move(tv)};

    // (a) Garbage value — guaranteed to fail decimal parse.
    {
        std::string const garbage = "not_a_number";
        std::vector<std::byte> val(garbage.size());
        std::memcpy(val.data(), garbage.data(), garbage.size());
        auto rc = v.validate_field(38, std::span<const std::byte>{val.data(), val.size()});
        ASSERT_FALSE(rc.has_value()) << "garbage Float value must produce an error";
        // Must be a wire_* slot (38–42), not a raw decimal slot (10–12).
        auto const slot = static_cast<int>(rc.error());
        EXPECT_GE(slot, 38) << "Float error must be a wire_* slot (>=38); got " << slot;
        EXPECT_LE(slot, 42) << "Float error must be a wire_* slot (<=42); got " << slot;
    }

    // (b) A valid short decimal — must succeed.
    {
        std::string const valid_dec = "123.45";
        std::vector<std::byte> val(valid_dec.size());
        std::memcpy(val.data(), valid_dec.data(), valid_dec.size());
        auto rc = v.validate_field(38, std::span<const std::byte>{val.data(), val.size()});
        EXPECT_TRUE(rc.has_value()) << "valid Float value must succeed via production table_view";
    }
}
