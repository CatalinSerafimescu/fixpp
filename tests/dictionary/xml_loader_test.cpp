// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/xml_loader_test.cpp — AC-L1 / AC-L10 — positive-path FIX44 load

#include <gtest/gtest.h>

#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/version_profile.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory_resource>
#include <set>
#include <sstream>
#include <string>

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

TEST(XmlLoaderLoad, LoadFromStringEquivalent) {
    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX44.xml";

    std::ifstream ifs{path};
    ASSERT_TRUE(ifs.is_open()) << "Cannot open FIX44.xml at: " << path;
    std::ostringstream oss;
    oss << ifs.rdbuf();
    std::string const xml_text = oss.str();

    std::array<std::byte, k1MiB> buffer{};
    std::pmr::monotonic_buffer_resource mr{buffer.data(), buffer.size()};

    auto d = fixpp::dict::XmlLoader{}.load_from_string(xml_text, &mr);

    EXPECT_EQ(d.which_session_version(), fixpp::dict::session_version::v44);
    EXPECT_FALSE(d.messages().empty());
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
