// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/validator_legacy_char_type_test.cpp
//
// 075-live-wire-enum-validation T044 — QuickFIX parity for pre-FIX.4.2
// dictionaries: `BeginString(8)` and `CheckSum(10)` (and every other
// CHAR-typed field) are declared `type='CHAR'` in `dictionaries/FIX40.xml`
// and `dictionaries/FIX41.xml` (byte-exact upstream copies) even though
// `8=FIX.4.1` / `10=047` are multi-byte. Before T044, fixpp's
// `dictionary_driven_validator` type-checked every present field — including
// the framing tags 8/9/10 — against `table_view::field_type_of()`, so the
// `ft::Char` arm's `value.size() == 1` requirement rejected EVERY FIX40/FIX41
// message on `BeginString(8)`, before reaching any other field.
//
// T044 mirrors QuickFIX's own compensating rule — DataDictionary::
// XMLTypeToType, src/C++/DataDictionary.cpp:589-592:
//     if (m_beginString < "FIX.4.2" && type == "CHAR") return TYPE::String;
// — in `xml_loader.cpp`'s `resolve_field_type()` (the XML-type-string ->
// `field_data_type` mapping), NOT in `dictionaries/*.xml` (untouched,
// supply-chain-hash-gated) and NOT in `validator.hpp`'s `ft::Char` arm
// (correct as written — the dictionary's own type MAPPING was wrong).
//
// This drives fixpp's REAL production path (XmlLoader -> as_table_view() ->
// dictionary_driven_validator::validate()) over real shipped dictionaries —
// no mock table_view — because the defect is specifically in how the loader
// interprets `CHAR` for these two dictionaries; a mock-table per-version
// matrix (validator_per_version_test.cpp) cannot reproduce it.
//
// Anchors:
//   spec:  user disposition "mirror QuickFIX" (2026-07-14, this task)
//   prod:  src/dictionary/xml_loader.cpp resolve_field_type()
//          (legacy_char_is_string)
//   sibling pattern: tests/wire/enum_golden_parity_test.cpp
//          (load_shipped_table_view / make_frame / parse_index)
//
// Mutation (performed manually, see report): reverting
// `legacy_char_is_string` to always-false (i.e. always mapping CHAR ->
// field_data_type::Char, the pre-T044 behavior) must turn
// LegacyCharType.Fix41HeartbeatAccepted and Fix40HeartbeatAccepted RED with
// wire_field_value_out_of_range / RefTagID=8. Reverted before commit.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <fixpp/core/error.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/field_type.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/validator.hpp>

#include "support/frame_view_factory.hpp"

namespace {

using fixpp::core::error;
using fixpp::dict::field_type;
using fixpp::wire::access_mode;
using fixpp::wire::dictionary_driven_validator;
using fixpp::wire::MessageView;
using fixpp::wire::Parser;

// ── frame-building helpers (mirrors enum_golden_parity_test.cpp) ────────────

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
    for (unsigned char c : pre) {
        sum += c;
    }
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

// Loads a real shipped dictionary and returns its table_view — the same
// production path (Dictionary::as_table_view()) the parity gate uses.
fixpp::dict::table_view load_shipped_table_view(char const* filename) {
    std::vector<std::byte> buf(8u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};
    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / filename;
    auto dict = fixpp::dict::XmlLoader{}.load(path, &mr);
    return dict.as_table_view();
}

constexpr std::string_view kSendingTime = "20260714-12:00:00";

// Minimal well-formed Heartbeat(0), required header fields only:
// SenderCompID(49), TargetCompID(56), MsgSeqNum(34), SendingTime(52) — the
// FIX40/FIX41 header's full required set beyond framing tags 8/9/35 (per
// <header required='Y'> in dictionaries/FIX40.xml, FIX41.xml).
std::string heartbeat_body() {
    std::string s = "35=0\x01";
    s += "49=SENDER\x01";
    s += "56=TARGET\x01";
    s += "34=1\x01";
    s += "52=";
    s += kSendingTime;
    s += "\x01";
    return s;
}

struct Outcome {
    bool accepted = false;
    error err{};
    std::uint16_t ref_tag = 0;
};

Outcome run(char const* dict_file, std::string_view begin_string) {
    auto tv = load_shipped_table_view(dict_file);
    dictionary_driven_validator v{std::move(tv)};

    auto frame = make_frame(begin_string, heartbeat_body());
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(frame, stack, arena);

    std::array<std::byte, 2048> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};
    Outcome out;
    std::uint16_t ref_tag = 0;
    auto result = v.validate(mv, &scratch_mr, &ref_tag);
    if (result.has_value()) {
        out.accepted = true;
    } else {
        out.accepted = false;
        out.err = result.error();
        out.ref_tag = ref_tag;
    }
    return out;
}

// ── FIX41: BeginString(8) = "FIX.4.1" (7 bytes) must NOT trip the Char arm ──
TEST(LegacyCharType, Fix41HeartbeatAccepted) {
    auto const outcome = run("FIX41.xml", "FIX.4.1");
    EXPECT_TRUE(outcome.accepted) << "FIX41 Heartbeat must be accepted; got reject error="
                                  << static_cast<int>(outcome.err)
                                  << " ref_tag=" << outcome.ref_tag
                                  << " (pre-T044 this rejected on BeginString(8), "
                                     "wire_field_value_out_of_range, ref_tag=8)";
}

// ── FIX40: same defect, same fix — BeginString(8) = "FIX.4.0" (7 bytes) ─────
TEST(LegacyCharType, Fix40HeartbeatAccepted) {
    auto const outcome = run("FIX40.xml", "FIX.4.0");
    EXPECT_TRUE(outcome.accepted) << "FIX40 Heartbeat must be accepted; got reject error="
                                  << static_cast<int>(outcome.err)
                                  << " ref_tag=" << outcome.ref_tag;
}

// ── FIX42 is UNAFFECTED: BeginString(8) was already declared STRING there,
// and this must remain the correct accept for an unrelated reason (the
// version predicate must not accidentally widen to FIX42+). ────────────────
TEST(LegacyCharType, Fix42HeartbeatStillAccepted) {
    auto const outcome = run("FIX42.xml", "FIX.4.2");
    EXPECT_TRUE(outcome.accepted) << "FIX42 Heartbeat must remain accepted (unaffected by T044)";
}

// ── CheckSum(10) (3-byte "047"-style modulo-256 value) does not trip the
// Char arm either — same field_type_of(10) mapping as BeginString(8). ───────
TEST(LegacyCharType, Fix41CheckSumThreeBytesNotFlagged) {
    auto tv = load_shipped_table_view("FIX41.xml");
    EXPECT_EQ(tv.field_type_of(std::uint16_t{10}), field_type::String)
        << "FIX41 CheckSum(10) must map to field_type::String (T044), not Char — a 3-byte "
           "checksum would otherwise trip the Char arm's size()==1 requirement.";
}

// ── Data-model pin: FIX44's Side(54) — a REAL multi-value-irrelevant, single-
// byte CHAR field unrelated to tags 8/10 — must remain field_type::Char.
// T044 must be scoped to {FIX40, FIX41} only; an over-broad rule that also
// caught FIX42+ would silently relax genuine Char fields like Side(54). ─────
TEST(LegacyCharType, Fix44Side54StillTypedChar) {
    auto tv = load_shipped_table_view("FIX44.xml");
    EXPECT_EQ(tv.field_type_of(std::uint16_t{54}), field_type::Char)
        << "FIX44 Side(54) must remain field_type::Char — T044 is scoped to FIX40/FIX41 only";
}

// ── And FIX40/FIX41's OWN CHAR fields (e.g. Side(54), still single-byte in
// practice) are ALSO relaxed to String under T044 — this is not a tag-8/10-
// only carve-out, it mirrors QuickFIX's dictionary-WIDE CHAR->String rule.
// Stated explicitly so a future reader does not "fix" this as a narrowing. ──
TEST(LegacyCharType, Fix41Side54RelaxedToStringDictionaryWide) {
    auto tv = load_shipped_table_view("FIX41.xml");
    EXPECT_EQ(tv.field_type_of(std::uint16_t{54}), field_type::String)
        << "FIX41's CHAR->String relaxation is dictionary-wide (QuickFIX parity), not scoped to "
           "tags 8/10 alone — Side(54) is also affected.";
}

}  // namespace
