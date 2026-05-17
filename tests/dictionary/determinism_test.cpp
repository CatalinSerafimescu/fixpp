// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/determinism_test.cpp — seam #5 — NFR-002-4 within-process determinism

#include <gtest/gtest.h>

#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/field_ref.hpp>
#include <fixpp/dict/xml_loader.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory_resource>
#include <string>
#include <vector>

namespace {

constexpr std::size_t k4MiB = 4UZ * 1024UZ * 1024UZ;

// Byte-concatenate the msg_type field of every MessageEntry in d.messages().
// Acts as the "hash" for message iteration order; a single std::string
// comparison is sufficient for within-process determinism.
std::string msg_type_bytes(fixpp::dict::Dictionary const& d) {
    std::string out;
    for (auto const& entry : d.messages()) {
        out.append(entry.msg_type);
        out.push_back('\0');  // separator so adjacent types don't merge
    }
    return out;
}

// Byte-concatenate (tag, type, rule) for every field_ref("D", tag) call over
// the representative tag span [1, 600].  Tags where rule == NotDeclared still
// contribute three bytes so the string lengths are equal across dictionaries
// regardless of which tags happen to be declared.
std::string field_ref_bytes(fixpp::dict::Dictionary const& d,
                             std::string_view msg_type,
                             std::uint16_t first_tag,
                             std::uint16_t last_tag) {
    std::string out;
    for (std::uint16_t tag = first_tag; tag <= last_tag; ++tag) {
        auto const fr = d.field_ref(msg_type, tag);
        out.push_back(static_cast<char>(static_cast<std::uint8_t>(fr.type)));
        out.push_back(static_cast<char>(static_cast<std::uint8_t>(fr.rule)));
        // Include tag bytes so a mis-ordering of tag values is also caught.
        out.push_back(static_cast<char>(static_cast<std::uint8_t>(tag & 0xFFu)));
        out.push_back(static_cast<char>(static_cast<std::uint8_t>(tag >> 8u)));
    }
    return out;
}

}  // namespace

// Load FIX44.xml twice into independent arenas and assert that messages()
// iteration order is byte-identical (NFR-002-4 within-process determinism).
TEST(Determinism, MessagesIterationByteIdentical) {
    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX44.xml";

    std::vector<std::byte> buf1(k4MiB);
    std::pmr::monotonic_buffer_resource mr1{buf1.data(), buf1.size()};
    auto d1 = fixpp::dict::XmlLoader{}.load(path, &mr1);

    std::vector<std::byte> buf2(k4MiB);
    std::pmr::monotonic_buffer_resource mr2{buf2.data(), buf2.size()};
    auto d2 = fixpp::dict::XmlLoader{}.load(path, &mr2);

    std::string const h1 = msg_type_bytes(d1);
    std::string const h2 = msg_type_bytes(d2);

    ASSERT_FALSE(h1.empty()) << "messages() must not be empty after a successful load";
    EXPECT_EQ(h1, h2)
        << "messages() iteration order differs between two independent loads of FIX44.xml "
           "(NFR-002-4 within-process determinism violation)";
}

// Load FIX44.xml twice and assert that field_ref() results for MsgType "D"
// (NewOrderSingle) over tags 1..600 are byte-identical (NFR-002-4).
TEST(Determinism, FieldRefSequenceByteIdentical) {
    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX44.xml";

    std::vector<std::byte> buf1(k4MiB);
    std::pmr::monotonic_buffer_resource mr1{buf1.data(), buf1.size()};
    auto d1 = fixpp::dict::XmlLoader{}.load(path, &mr1);

    std::vector<std::byte> buf2(k4MiB);
    std::pmr::monotonic_buffer_resource mr2{buf2.data(), buf2.size()};
    auto d2 = fixpp::dict::XmlLoader{}.load(path, &mr2);

    // Verify "D" (NewOrderSingle) is actually present in both loads before
    // comparing field_ref byte strings.
    bool d_present_d1 = false;
    bool d_present_d2 = false;
    for (auto const& e : d1.messages()) {
        if (e.msg_type == "D") { d_present_d1 = true; break; }
    }
    for (auto const& e : d2.messages()) {
        if (e.msg_type == "D") { d_present_d2 = true; break; }
    }
    ASSERT_TRUE(d_present_d1) << "FIX44.xml must contain MsgType D (NewOrderSingle)";
    ASSERT_TRUE(d_present_d2) << "second load of FIX44.xml must contain MsgType D";

    std::string const h1 = field_ref_bytes(d1, "D", 1, 600);
    std::string const h2 = field_ref_bytes(d2, "D", 1, 600);

    EXPECT_EQ(h1, h2)
        << "field_ref sequence for MsgType D differs between two independent loads of FIX44.xml "
           "(NFR-002-4 within-process determinism violation)";
}

// Load FIX44.xml, record the messages() hash, move-construct a new Dictionary,
// and assert the hash is unchanged — i.e. move preserves iteration order.
TEST(Determinism, SurvivesMove) {
    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX44.xml";

    std::vector<std::byte> buf(k4MiB);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};
    auto d1 = fixpp::dict::XmlLoader{}.load(path, &mr);

    std::string const h_before = msg_type_bytes(d1);
    ASSERT_FALSE(h_before.empty()) << "messages() must not be empty before move";

    auto d2 = std::move(d1);  // NOLINT(performance-move-const-arg)

    std::string const h_after = msg_type_bytes(d2);

    EXPECT_EQ(h_before, h_after)
        << "messages() iteration order changed after Dictionary move "
           "(NFR-002-4 determinism / move semantics regression)";
}
