// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/xml_loader_test.cpp — AC-L1 / AC-L10 — positive-path FIX44 load

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fstream>
#include <memory_resource>
#include <set>
#include <sstream>
#include <string>
#include <string_view>

namespace {

constexpr std::size_t k1MiB = 1024UZ * 1024UZ;

}  // namespace

TEST(XmlLoaderLoad, Fix44Loads) {
    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX44.xml";

    std::array<std::byte, k1MiB> buffer{};
    std::pmr::monotonic_buffer_resource mr{buffer.data(), buffer.size()};

    auto d = fixpp::dict::XmlLoader{}.load(path, &mr);

    EXPECT_EQ(d.which_session_version(), fixpp::dict::session_version::v44);
    EXPECT_FALSE(d.messages().empty());
}

// AC-L10: load_from_string() produces a Dictionary that is structurally
// identical to the one produced by load(path) for the same XML content.
// R4 (F2.1): extend beyond the minimal version/non-empty check to assert
// that both paths produce structurally equivalent dictionaries.
TEST(XmlLoaderLoad, LoadFromStringEquivalent) {
    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX44.xml";

    // Load via path.
    std::array<std::byte, k1MiB> buf_file{};
    std::pmr::monotonic_buffer_resource mr_file{buf_file.data(), buf_file.size()};
    auto d_file = fixpp::dict::XmlLoader{}.load(path, &mr_file);

    // Load via string.
    std::ifstream ifs{path};
    ASSERT_TRUE(ifs.is_open()) << "Cannot open FIX44.xml at: " << path;
    std::ostringstream oss;
    oss << ifs.rdbuf();
    std::string const xml_text = oss.str();

    std::array<std::byte, k1MiB> buf_str{};
    std::pmr::monotonic_buffer_resource mr_str{buf_str.data(), buf_str.size()};
    auto d_str = fixpp::dict::XmlLoader{}.load_from_string(xml_text, &mr_str);

    // 1. Session version.
    EXPECT_EQ(d_str.which_session_version(), fixpp::dict::session_version::v44);
    EXPECT_EQ(d_str.which_session_version(), d_file.which_session_version());

    // 2. messages().size() and msg_type/name sequence equality.
    auto const msgs_file = d_file.messages();
    auto const msgs_str = d_str.messages();
    EXPECT_FALSE(msgs_str.empty());
    ASSERT_EQ(msgs_file.size(), msgs_str.size())
        << "messages().size() differs between load() and load_from_string()";
    for (std::size_t i = 0; i < msgs_file.size(); ++i) {
        EXPECT_EQ(msgs_file[i].msg_type, msgs_str[i].msg_type)
            << "msg_type mismatch at index " << i;
        EXPECT_EQ(msgs_file[i].name, msgs_str[i].name) << "message name mismatch at index " << i;
    }

    // 3. Spot-check field_ref() for known (msg_type, tag) pairs.
    //    ClOrdID(11) on NewOrderSingle(D): required in FIX44.
    {
        auto const fr_file = d_file.field_ref("D", std::uint16_t{11});
        auto const fr_str = d_str.field_ref("D", std::uint16_t{11});
        EXPECT_EQ(fr_file.rule, fr_str.rule) << "field_ref(D, 11).rule differs between load paths";
        EXPECT_EQ(fr_file.type, fr_str.type) << "field_ref(D, 11).type differs between load paths";
    }
    //    OrdStatus(39) on ExecutionReport(8).
    {
        auto const fr_file = d_file.field_ref("8", std::uint16_t{39});
        auto const fr_str = d_str.field_ref("8", std::uint16_t{39});
        EXPECT_EQ(fr_file.rule, fr_str.rule) << "field_ref(8, 39).rule differs between load paths";
    }
    //    SenderCompID(49) on Logon(A): header field on all versions.
    {
        auto const fr_file = d_file.field_ref("A", std::uint16_t{49});
        auto const fr_str = d_str.field_ref("A", std::uint16_t{49});
        EXPECT_EQ(fr_file.rule, fr_str.rule) << "field_ref(A, 49).rule differs between load paths";
    }

    // 4. required_fields("A") returns the same sorted set on both paths.
    {
        auto const req_file = d_file.required_fields("A");
        auto const req_str = d_str.required_fields("A");
        EXPECT_EQ(req_file.size(), req_str.size())
            << "required_fields(A).size() differs between load paths";
        for (std::size_t i = 0; i < std::min(req_file.size(), req_str.size()); ++i) {
            EXPECT_EQ(req_file[i], req_str[i])
                << "required_fields(A)[" << i << "] differs between load paths";
        }
    }

    // 5. component("Instrument") payload equivalence (depends on R1).
    {
        auto const comp_file = d_file.component("Instrument");
        auto const comp_str = d_str.component("Instrument");
        ASSERT_TRUE(comp_file.has_value()) << "Instrument missing in load()";
        ASSERT_TRUE(comp_str.has_value()) << "Instrument missing in load_from_string()";
        EXPECT_EQ(comp_file->field_count, comp_str->field_count)
            << "Instrument field_count differs between load paths";
        EXPECT_EQ(comp_file->component_id, comp_str->component_id)
            << "Instrument component_id differs between load paths";
    }

    // 6. group(453) first_field_tag equality (NoPartyIDs delimiter).
    {
        auto const grp_file = d_file.group(std::uint16_t{453});
        auto const grp_str = d_str.group(std::uint16_t{453});
        ASSERT_TRUE(grp_file.has_value()) << "NoPartyIDs(453) missing in load()";
        ASSERT_TRUE(grp_str.has_value()) << "NoPartyIDs(453) missing in load_from_string()";
        EXPECT_EQ(grp_file->first_field_tag, grp_str->first_field_tag)
            << "group(453).first_field_tag differs between load paths";
        EXPECT_EQ(grp_file->field_count, grp_str->field_count)
            << "group(453).field_count differs between load paths";
    }

    // 7. length_pair_data_tag(95) equality.
    EXPECT_EQ(d_file.length_pair_data_tag(std::uint16_t{95}),
              d_str.length_pair_data_tag(std::uint16_t{95}))
        << "length_pair_data_tag(95) differs between load paths";

    // 8. Full (msg_type, tag) field_ref equality across both load paths.
    //    Replaces the 3-spot-check comparison (R8 / F2.1 / P2-B): for every
    //    (msg_type, tag) pair in the file-loaded Dictionary, assert that both
    //    paths agree on rule, type, group_no_tag, component_index, and
    //    length_pair_data_tag. Uses the same probe-walk shape as
    //    round_trip_test.cpp:316-329.
    for (auto const& m_file : msgs_file) {
        for (std::uint32_t t = 0; t < 65536u; ++t) {
            auto const tag = static_cast<std::uint16_t>(t);
            auto const fr_file = d_file.field_ref(m_file.msg_type, tag);
            auto const fr_str = d_str.field_ref(m_file.msg_type, tag);
            EXPECT_EQ(fr_file.rule, fr_str.rule) << "field_ref(" << m_file.msg_type << ", " << tag
                                                 << ").rule mismatch between load paths";
            if (fr_file.rule != fixpp::dict::field_presence::NotDeclared) {
                EXPECT_EQ(fr_file.type, fr_str.type) << "field_ref(" << m_file.msg_type << ", "
                                                     << tag << ").type mismatch between load paths";
                EXPECT_EQ(fr_file.group_no_tag, fr_str.group_no_tag)
                    << "field_ref(" << m_file.msg_type << ", " << tag
                    << ").group_no_tag mismatch between load paths";
                EXPECT_EQ(fr_file.component_index, fr_str.component_index)
                    << "field_ref(" << m_file.msg_type << ", " << tag
                    << ").component_index mismatch between load paths";
                EXPECT_EQ(fr_file.length_pair_data_tag, fr_str.length_pair_data_tag)
                    << "field_ref(" << m_file.msg_type << ", " << tag
                    << ").length_pair_data_tag mismatch between load paths";
            }
        }
    }

    // 9. required_fields() equality for every declared msg_type.
    for (auto const& m : msgs_file) {
        auto const req_f = d_file.required_fields(m.msg_type);
        auto const req_s = d_str.required_fields(m.msg_type);
        ASSERT_EQ(req_f.size(), req_s.size())
            << "required_fields(" << m.msg_type << ").size() mismatch between load paths";
        for (std::size_t i = 0; i < req_f.size(); ++i) {
            EXPECT_EQ(req_f[i], req_s[i])
                << "required_fields(" << m.msg_type << ")[" << i << "] mismatch between load paths";
        }
    }

    // 10. component_fields("Instrument") span equality across both load paths
    //     (R6-dependent; verifies that load_from_string also populates the
    //     per-component side table correctly).
    {
        auto const span_f = d_file.component_fields("Instrument");
        auto const span_s = d_str.component_fields("Instrument");
        ASSERT_EQ(span_f.size(), span_s.size())
            << "component_fields(\"Instrument\").size() mismatch between load paths";
        for (std::size_t i = 0; i < span_f.size(); ++i) {
            EXPECT_EQ(span_f[i].tag, span_s[i].tag)
                << "component_fields(\"Instrument\")[" << i << "].tag mismatch between load paths";
            EXPECT_EQ(span_f[i].rule, span_s[i].rule)
                << "component_fields(\"Instrument\")[" << i << "].rule mismatch between load paths";
            EXPECT_EQ(span_f[i].type, span_s[i].type)
                << "component_fields(\"Instrument\")[" << i << "].type mismatch between load paths";
        }
    }
}

TEST(XmlLoaderLoad, StatelessRepeatedLoads) {
    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX44.xml";

    fixpp::dict::XmlLoader loader{};

    std::array<std::byte, k1MiB> buf1{};
    std::pmr::monotonic_buffer_resource mr1{buf1.data(), buf1.size()};
    auto d1 = loader.load(path, &mr1);

    std::array<std::byte, k1MiB> buf2{};
    std::pmr::monotonic_buffer_resource mr2{buf2.data(), buf2.size()};
    auto d2 = loader.load(path, &mr2);

    EXPECT_EQ(d1.which_session_version(), fixpp::dict::session_version::v44);
    EXPECT_FALSE(d1.messages().empty());

    EXPECT_EQ(d2.which_session_version(), fixpp::dict::session_version::v44);
    EXPECT_FALSE(d2.messages().empty());
}

TEST(XmlLoaderLoad, Fix44Headlines) {
    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX44.xml";

    std::array<std::byte, k1MiB> buffer{};
    std::pmr::monotonic_buffer_resource mr{buffer.data(), buffer.size()};

    auto d = fixpp::dict::XmlLoader{}.load(path, &mr);

    std::set<std::string> msg_types;
    for (auto const& entry : d.messages()) {
        msg_types.emplace(entry.msg_type);
    }

    EXPECT_TRUE(msg_types.count("D")) << "Missing NewOrderSingle (D)";
    EXPECT_TRUE(msg_types.count("8")) << "Missing ExecutionReport (8)";
    EXPECT_TRUE(msg_types.count("A")) << "Missing Logon (A)";
    EXPECT_TRUE(msg_types.count("0")) << "Missing Heartbeat (0)";
    EXPECT_TRUE(msg_types.count("3")) << "Missing Reject (3)";
}

// US1 (064-fix4041-legacy-types / D-004): FIX 4.0/4.1 load to completion once
// the loader accepts the legacy field types TIME/DATE, and both resolve to
// their sanctioned enum (TIME -> UtcTimestamp, DATE -> LocalMktDate — mapping
// rationale lives on the kFieldTypeTable rows in src/dictionary/xml_loader.cpp).
// RED-before/GREEN-after: before those two rows land, load() throws
// xml_parse_error on the first TIME/DATE field and this test fails; after, the
// load completes and the field_ref() types match. (US1 scenarios 1,3,4; SC-001)
namespace {

void expect_fix4x_legacy_load(std::string_view filename, fixpp::dict::session_version expected) {
    SCOPED_TRACE(filename);
    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / filename;

    std::array<std::byte, k1MiB> buffer{};
    std::pmr::monotonic_buffer_resource mr{buffer.data(), buffer.size()};

    // An uncaught xml_parse_error here fails the test — the RED-first witness
    // that the file no longer fail-closes on its TIME/DATE fields (SC-001).
    auto const d = fixpp::dict::XmlLoader{}.load(path, &mr);

    EXPECT_EQ(d.which_session_version(), expected);
    EXPECT_FALSE(d.messages().empty());

    // TIME field: SendingTime (tag 52, header) under NewOrderSingle "D".
    EXPECT_EQ(d.field_ref("D", std::uint16_t{52}).type, fixpp::dict::field_data_type::UtcTimestamp)
        << "SendingTime (TIME) must resolve to UtcTimestamp";
    // DATE field: FutSettDate (tag 64) under NewOrderSingle "D".
    EXPECT_EQ(d.field_ref("D", std::uint16_t{64}).type, fixpp::dict::field_data_type::LocalMktDate)
        << "FutSettDate (DATE) must resolve to LocalMktDate, NOT UtcDateOnly (retired hypothesis)";
}

}  // namespace

TEST(XmlLoaderLoad, Fix40LoadsLegacyTypes) {
    expect_fix4x_legacy_load("FIX40.xml", fixpp::dict::session_version::v40);
}

TEST(XmlLoaderLoad, Fix41LoadsLegacyTypes) {
    expect_fix4x_legacy_load("FIX41.xml", fixpp::dict::session_version::v41);
}
