// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/app_message_read_scaffold.hpp
//
// 061-typed-app-messages (061-slim) T008 — shared read-scaffold for the exemplar
// round-trip (T020) and independent inbound-read (T022) witnesses.
//
// Factors the proven DICT-AWARE (5-arg) parse path out of
// tests/codegen/group_entry_read_test.cpp:224-249 (XmlLoader ->
// Dictionary::as_table_view() -> Framer -> Parser<Index>{tv} -> MessageView<Index>).
// The dict-aware Parser<Index>{tv} ctor installs the context-scoped
// group_member_fn, so grouped/nested typed reads (062/063) enumerate correctly —
// the 2-arg heuristic MessageView{frame, mr} CANNOT do this (data-model.md §5).
//
// The BeginString is a make_frame() parameter (data-model §5) so a future
// non-v44 exemplar can reuse the scaffold; the 5 exemplars are all "FIX.4.4".
//
// Consuming test targets MUST define FIXPP_DICT_DATA_DIR (the dictionaries/ path;
// mirror tests/codegen/CMakeLists.txt:47) so load_fix44() resolves FIX44.xml.
#pragma once

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/parser.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fixpp_test_support {

// Parse an ASCII decimal literal into a decimal_t backed by `mr`. ADD_FAILURE on
// a malformed literal (test-only helper; shared by the 067/069 builder round-trip
// TUs, which compile into one binary — one definition, not per-TU copies).
inline fixpp::decimal_t make_decimal(std::string_view sv, std::pmr::memory_resource* mr) {
    auto bytes =
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(sv.data()), sv.size()};
    auto r = fixpp::decimal_t::parse(bytes, mr);
    EXPECT_TRUE(r.has_value()) << "make_decimal failed for: " << sv;
    return r.value_or(fixpp::decimal_t{});
}

// View a byte span as a std::string (raw copy; SOH bytes preserved verbatim).
inline std::string bytes_to_string(std::span<const std::byte> b) {
    return std::string{reinterpret_cast<const char*>(b.data()), b.size()};
}

// Assemble a complete FIX frame "8=<begin_string>\x01 9=<len>\x01 <body> 10=<chk>\x01"
// from a body that already leads with "35=<MsgType>\x01". BodyLength = byte count
// from the start of the body (35=…) up to but excluding the CheckSum field.
inline std::vector<std::byte> make_frame(std::string_view begin_string, std::string_view body) {
    std::string pre = "8=" + std::string(begin_string) + "\x01" + "9=" +
                      std::to_string(body.size()) + "\x01" + std::string(body);
    unsigned sum = 0;
    for (unsigned char c : pre) sum += c;
    char checksum[16]{};
    std::snprintf(checksum, sizeof(checksum), "10=%03u\x01", sum % 256U);
    std::string full = pre + checksum;
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

// Load the real FIX 4.4 dictionary. The returned Dictionary OWNS the metadata;
// it MUST outlive any table_view / MessageView derived from it (caller keeps it
// in scope). Mirrors group_entry_read_test.cpp:230-231.
inline fixpp::dict::Dictionary load_fix44(std::pmr::memory_resource* mr) {
    fixpp::dict::XmlLoader loader;
    return loader.load(std::string(FIXPP_DICT_DATA_DIR) + "/FIX44.xml", mr);
}

// Parse a raw frame via the dict-aware Parser<Index>{tv} path (grouped/nested
// reads enumerate). `tv` and `mr` must outlive the returned MessageView. On
// failure ADD_FAILURE() and return a default MessageView (mirrors the
// established parse_index/parse_frame test-helper contract).
inline fixpp::wire::MessageView<fixpp::wire::access_mode::Index> parse_dict(
    std::vector<std::byte> const& buf, fixpp::dict::table_view const& tv,
    std::pmr::memory_resource* mr) {
    fixpp::wire::pmr_carry_buffer carry{buf.size(), mr};
    fixpp::wire::Framer fr{};
    fixpp::wire::frame_view fvs[1]{};
    auto framed = fr.feed(std::span<const std::byte>{buf.data(), buf.size()}, carry,
                          std::span<fixpp::wire::frame_view>{fvs, 1});
    if (!framed.has_value() || framed->empty()) {
        ADD_FAILURE() << "make_frame/Framer::feed produced no frame";
        return {};
    }
    fixpp::wire::Parser<fixpp::wire::access_mode::Index> parser{tv};
    auto mv = parser.parse((*framed)[0], mr);
    if (!mv.has_value()) {
        ADD_FAILURE() << "dict-aware Parser<Index>::parse failed";
        return {};
    }
    // MUST move, not copy: a MessageView COPY runs std::pmr's
    // select_on_container_copy_construction, which re-roots the OffsetTable's
    // allocator to the default (new_delete) resource — its lazily-built nested
    // sub-OffsetTables would then allocate from the global heap and leak (they
    // are placement-new'd and reclaimed wholesale with the arena, never
    // individually freed). A move preserves the `mr` (arena) allocator binding.
    return std::move(*mv);
}

}  // namespace fixpp_test_support
