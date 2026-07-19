// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/fixt_header_validate_test.cpp
//
// 081-strict-validation-residuals US1 (Concern A / #203 / L-041-2) T003/T004:
// on the strict `validate_inbound_messages` path, a well-formed FIX50/
// FIX50SP1/FIX50SP2 application frame carrying the FIXT.1.1-owned standard
// header (34/49/52/56) + trailer (10) must be ACCEPTED, not rejected
// `wire_unexpected_tag` on tag 8 — the vendored FIX50SPx dictionaries ship
// an empty `<header/>` (FIXT.1.1 session/application split), so before this
// feature `dictionary_driven_validator::validate()` Step 1 rejects the very
// first field it examines (BeginString=8, guaranteed present by the Framer)
// for EVERY message of EVERY FIX50SPx msg_type — a pre-existing, escalated
// defect (see the T006 finding in tests/wire/validator_type_check_test.cpp,
// which shipped NO real-frame FIX50SP2 validate() test for exactly this
// reason).
//
// Anchors:
//   research:  specs/081-strict-validation-residuals/research.md D-1/D-2
//   data-model: specs/081-strict-validation-residuals/data-model.md E-1/E-2
//   contract:  specs/081-strict-validation-residuals/contracts/
//              validation-acceptance.md clauses 1-4
//   quickstart: specs/081-strict-validation-residuals/quickstart.md
//               Scenario A1/A2
//   tasks:     specs/081-strict-validation-residuals/tasks.md T003/T004
//
// Frame-construction pattern (make_frame/parse_index) mirrors
// tests/wire/validator_production_table_view_test.cpp /
// tests/wire/validator_type_check_test.cpp — real XmlLoader::load() over the
// vendored dictionaries, real Parser<Index>, real dictionary_driven_validator.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/validator.hpp>
#include <memory_resource>
#include <string>
#include <string_view>
#include <vector>

#include "support/frame_view_factory.hpp"

namespace {

using fixpp::core::error;
using fixpp::dict::Dictionary;
using fixpp::wire::access_mode;
using fixpp::wire::dictionary_driven_validator;
using fixpp::wire::MessageView;
using fixpp::wire::Parser;

Dictionary load_real_dict(char const* file, std::pmr::memory_resource* mr) {
    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / file;
    return fixpp::dict::XmlLoader{}.load(path, mr);
}

std::string make_checksum_field(unsigned chk) {
    std::string s = "10=";
    s.push_back(static_cast<char>('0' + ((chk / 100U) % 10U)));
    s.push_back(static_cast<char>('0' + ((chk / 10U) % 10U)));
    s.push_back(static_cast<char>('0' + (chk % 10U)));
    s.push_back('\x01');
    return s;
}

// Builds a complete FIX frame: "8=FIXT.1.1" + 9=<len> + body_fields + 10=<chk>
// — a FIXT.1.1-session-carried application frame (the exact 081 scenario:
// the wire BeginString is FIXT.1.1 even though the application dictionary
// loaded is a bare FIX50/FIX50SP1/FIX50SP2 XML).
std::vector<std::byte> make_frame(std::string_view body_fields) {
    std::string body{body_fields};
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string pre = "8=FIXT.1.1\x01" + nine + body;
    unsigned sum = 0;
    for (unsigned char c : pre) {
        sum += c;
    }
    std::string full = pre + make_checksum_field(sum % 256U);
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

template <std::size_t N>
MessageView<access_mode::Index> parse_index(std::vector<std::byte> const& buf,
                                            std::array<std::byte, N>& stack_buf,
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

constexpr std::size_t kScratch = 2048;

// A well-formed NewOrderSingle(D) body: standard FIXT.1.1 header fields
// (34/49/52/56, NOT declared in the empty FIX50SPx <header/>) + the message's
// genuinely-required application fields (ClOrdID(11), Side(54),
// TransactTime(60), OrdType(40) — verified message-level required set,
// identical across FIX50.xml/FIX50SP1.xml/FIX50SP2.xml: neither the
// Instrument nor OrderQtyData wrapping component declares any required=Y
// scalar field, so 11/54/60/40 are the ENTIRE message-level required set).
// `sending_time`/`seq_num`/`appl_ext_id`/`omit_tag` let individual tests
// perturb exactly one field without duplicating the whole builder.
struct NosFields {
    std::string sending_time = "20240101-00:00:00";
    std::string seq_num = "1";
    std::string appl_ext_id;  // empty => omitted (1156 is optional)
    std::uint16_t omit_required_tag = 0;  // 0 => omit nothing
};

std::vector<std::byte> well_formed_new_order_single(NosFields const& f = {}) {
    std::string body = "35=D\x01";
    body += "34=" + f.seq_num + "\x01";
    body += "49=SENDER\x01";
    body += "52=" + f.sending_time + "\x01";
    body += "56=TARGET\x01";
    if (!f.appl_ext_id.empty()) {
        body += "1156=" + f.appl_ext_id + "\x01";
    }
    if (f.omit_required_tag != 11) {
        body += "11=CLORD1\x01";
    }
    if (f.omit_required_tag != 54) {
        body += "54=1\x01";
    }
    if (f.omit_required_tag != 60) {
        body += "60=20240101-00:00:00\x01";
    }
    if (f.omit_required_tag != 40) {
        body += "40=2\x01";
    }
    return make_frame(body);
}

struct FixtHeaderValidateTest : ::testing::TestWithParam<char const*> {};

// ── T003: standalone accept (SC-001) ────────────────────────────────────────
// Load ONLY the given FIX50/FIX50SP1/FIX50SP2 dictionary (no FIXT11.xml),
// enable strict validation (dictionary_driven_validator IS the strict path),
// feed a well-formed NewOrderSingle carrying standard header+trailer.
// GREEN: accepted. (RED, pre-fix: rejected wire_unexpected_tag, ref_tag==8 —
// see the mechanism pin tests/wire/validator_production_table_view_test.cpp
// :270 for the identical "empty valid-tag view -> first field is 8" proof.)
TEST_P(FixtHeaderValidateTest, WellFormedApplicationFrameAccepted) {
    std::pmr::monotonic_buffer_resource mr;
    auto dict = load_real_dict(GetParam(), &mr);
    dictionary_driven_validator v{dict.as_table_view()};

    auto buf = well_formed_new_order_single();
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(buf, stack, arena);

    std::array<std::byte, kScratch> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};
    std::uint16_t ref_tag = 0;
    auto result = v.validate(mv, &scratch_mr, &ref_tag);
    EXPECT_TRUE(result.has_value())
        << GetParam() << ": well-formed FIXT-header NewOrderSingle must be accepted; err="
        << (result.has_value() ? 0 : static_cast<int>(result.error())) << " ref_tag=" << ref_tag;
}

// ── T004(a): accept-only guard — omitting a genuinely-required APPLICATION
// field must still reject (header acceptance did not weaken app-field
// checks) ───────────────────────────────────────────────────────────────────
TEST_P(FixtHeaderValidateTest, OmittedRequiredApplicationFieldStillRejects) {
    std::pmr::monotonic_buffer_resource mr;
    auto dict = load_real_dict(GetParam(), &mr);
    dictionary_driven_validator v{dict.as_table_view()};

    NosFields f;
    f.omit_required_tag = 40;  // OrdType — required='Y' at message level
    auto buf = well_formed_new_order_single(f);
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(buf, stack, arena);

    std::array<std::byte, kScratch> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};
    std::uint16_t ref_tag = 0;
    auto result = v.validate(mv, &scratch_mr, &ref_tag);
    ASSERT_FALSE(result.has_value())
        << GetParam() << ": omitting OrdType(40) must still be rejected";
    EXPECT_EQ(result.error(), error::wire_required_field_missing);
    EXPECT_EQ(ref_tag, std::uint16_t{40});
}

// ── T004(b): accept-only guard — omitting a session-owned HEADER field
// (SendingTime=52) must NOT be rejected by validate() (the session FSM owns
// header required-presence, not the dictionary validator) ──────────────────
TEST_P(FixtHeaderValidateTest, OmittedHeaderFieldNotRejectedByValidate) {
    std::pmr::monotonic_buffer_resource mr;
    auto dict = load_real_dict(GetParam(), &mr);
    dictionary_driven_validator v{dict.as_table_view()};

    std::string body = "35=D\x01"
                        "34=1\x01" "49=SENDER\x01" "56=TARGET\x01"  // NOTE: 52 omitted
                        "11=CLORD1\x01" "54=1\x01" "60=20240101-00:00:00\x01" "40=2\x01";
    auto buf = make_frame(body);
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(buf, stack, arena);

    std::array<std::byte, kScratch> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};
    auto result = v.validate(mv, &scratch_mr, nullptr);
    EXPECT_TRUE(result.has_value())
        << GetParam() << ": omitting SendingTime(52) must NOT be rejected by validate(); err="
        << (result.has_value() ? 0 : static_cast<int>(result.error()));
}

// ── T004(c): F2 no-false-accept pin — a malformed numeric header field
// (34=abc SeqNum->Int, 1156=abc ApplExtID->Int) MUST be rejected at the Int
// arm with the EXACT offending tag reported. RED before fixt_framing_types_
// exists (accepted — framing tag's type defaulted to String, no structural
// constraint); GREEN after. ─────────────────────────────────────────────────
TEST_P(FixtHeaderValidateTest, MalformedMsgSeqNumRejected) {
    std::pmr::monotonic_buffer_resource mr;
    auto dict = load_real_dict(GetParam(), &mr);
    dictionary_driven_validator v{dict.as_table_view()};

    NosFields f;
    f.seq_num = "abc";
    auto buf = well_formed_new_order_single(f);
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(buf, stack, arena);

    std::array<std::byte, kScratch> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};
    std::uint16_t ref_tag = 0;
    auto result = v.validate(mv, &scratch_mr, &ref_tag);
    ASSERT_FALSE(result.has_value()) << GetParam() << ": MsgSeqNum(34)=abc must be rejected";
    EXPECT_EQ(result.error(), error::wire_field_value_out_of_range);
    EXPECT_EQ(ref_tag, std::uint16_t{34});
}

TEST_P(FixtHeaderValidateTest, MalformedApplExtIdRejected) {
    std::pmr::monotonic_buffer_resource mr;
    auto dict = load_real_dict(GetParam(), &mr);
    dictionary_driven_validator v{dict.as_table_view()};

    NosFields f;
    f.appl_ext_id = "abc";
    auto buf = well_formed_new_order_single(f);
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(buf, stack, arena);

    std::array<std::byte, kScratch> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};
    std::uint16_t ref_tag = 0;
    auto result = v.validate(mv, &scratch_mr, &ref_tag);
    ASSERT_FALSE(result.has_value()) << GetParam() << ": ApplExtID(1156)=abc must be rejected";
    EXPECT_EQ(result.error(), error::wire_field_value_out_of_range);
    EXPECT_EQ(ref_tag, std::uint16_t{1156});
}

// ── T004(d): documented limitation, NOT a reject pin — a malformed
// UtcTimestamp header field (SendingTime=52) is structurally undetectable to
// the Phase-1 validator (UtcTimestamp -> field_type::String, field_type.hpp)
// and stays ACCEPTED. ────────────────────────────────────────────────────────
TEST_P(FixtHeaderValidateTest, MalformedSendingTimeStaysAccepted) {
    std::pmr::monotonic_buffer_resource mr;
    auto dict = load_real_dict(GetParam(), &mr);
    dictionary_driven_validator v{dict.as_table_view()};

    NosFields f;
    f.sending_time = "notatime";
    auto buf = well_formed_new_order_single(f);
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(buf, stack, arena);

    std::array<std::byte, kScratch> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};
    auto result = v.validate(mv, &scratch_mr, nullptr);
    EXPECT_TRUE(result.has_value())
        << GetParam()
        << ": malformed SendingTime(52) is structurally undetectable (UtcTimestamp->String) "
           "and must stay accepted (documented limitation, not a reject pin)";
}

INSTANTIATE_TEST_SUITE_P(Fix50Family, FixtHeaderValidateTest,
                        ::testing::Values("FIX50.xml", "FIX50SP1.xml", "FIX50SP2.xml"));

}  // namespace
