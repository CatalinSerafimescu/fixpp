#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/overlay_test_support.hpp — helpers shared by the 098 overlay
// witnesses (overlay_equivalence_test.cpp, W-1/W-2; overlay_refusal_test.cpp, W-3).
// Test-only. Reads files, writes unique temporary files, and runs a load through
// research R-5's read seam (src/dictionary/overlay_merge.hpp).

#include <gtest/gtest.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/error.hpp>
#include <fixpp/dict/loader_policy.hpp>
#include <fstream>
#include <ios>
#include <iostream>
#include <iterator>
#include <memory_resource>
#include <sstream>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <vector>

#include "overlay_merge.hpp"
#include "overlay_patch.hpp"

namespace overlay_test {

namespace ov = fixpp::dict::detail::overlay;

[[nodiscard]] inline std::string read_file(std::filesystem::path const& p) {
    std::ifstream in(p, std::ios::binary);
    EXPECT_TRUE(in.good()) << "cannot open " << p;
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

[[nodiscard]] inline std::filesystem::path dict_dir() { return FIXPP_DICT_DATA_DIR; }
[[nodiscard]] inline std::filesystem::path fixture_dir() { return FIXPP_098_FIXTURE_DIR; }

// A file in the temporary directory, unique to this process and call, removed on
// destruction.
class temp_file {
public:
    explicit temp_file(std::string_view contents) {
        static std::atomic<unsigned> seq{0};
        path_ = std::filesystem::temp_directory_path() /
                ("fixpp_098_overlay_" + std::to_string(::getpid()) + "_" +
                 std::to_string(seq.fetch_add(1)) + ".xml");
        std::ofstream out(path_, std::ios::binary | std::ios::trunc);
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        EXPECT_TRUE(out.good()) << "cannot write " << path_;
    }
    ~temp_file() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
    temp_file(temp_file const&) = delete;
    temp_file& operator=(temp_file const&) = delete;
    temp_file(temp_file&&) = delete;
    temp_file& operator=(temp_file&&) = delete;
    [[nodiscard]] std::filesystem::path const& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

[[nodiscard]] inline char const* origin_name(ov::refusal_origin o) noexcept {
    switch (o) {
        case ov::refusal_origin::none:
            return "none";
        case ov::refusal_origin::budget:
            return "pre-walk budget";
        case ov::refusal_origin::side_table:
            return "pre-walk side-table fit";
        case ov::refusal_origin::counter:
            return "build counter";
    }
    return "?";
}

// The refusal a probe carries, as type and message; empty when it loaded.
struct refusal {
    bool refused = false;
    std::string type;  // typeid(e).name() of the dynamic type
    std::string what;
};

[[nodiscard]] inline refusal refusal_of(ov::xml_overlay_probe const& p) {
    refusal r;
    if (!p.error) {
        return r;
    }
    r.refused = true;
    try {
        std::rethrow_exception(p.error);
    } catch (std::exception const& e) {
        r.type = typeid(e).name();
        r.what = e.what();
    } catch (...) {
        r.type = "(not a std::exception)";
    }
    return r;
}

// A probe's whole account, for a failure message.
[[nodiscard]] inline std::string describe(ov::xml_overlay_probe const& p) {
    std::ostringstream os;
    refusal const r = refusal_of(p);
    os << "loaded=" << (p.dictionary.has_value() ? "yes" : "no")
       << " origin=" << origin_name(p.origin) << " counter=" << p.counter;
    if (p.guards) {
        os << " predicted=" << p.guards->merged_total << " base_total=" << p.guards->base_total;
    }
    if (r.refused) {
        os << " error=" << r.type << ": " << r.what;
    }
    return os.str();
}

// Every overlay row that loads asserts this: it loaded, no counter-origin refusal,
// and the build's step counter equals step 6's predicted total.
inline void expect_loaded_at_prediction(ov::xml_overlay_probe const& p, std::string const& row) {
    EXPECT_TRUE(p.dictionary.has_value()) << row << ": " << describe(p);
    EXPECT_NE(p.origin, ov::refusal_origin::counter) << row << ": " << describe(p);
    ASSERT_TRUE(p.guards.has_value()) << row << ": " << describe(p);
    std::cout << "[098 " << row << "] step counter " << p.counter << ", step 6's prediction "
              << p.guards->merged_total << ", base total " << p.guards->base_total << '\n';
    EXPECT_EQ(p.counter, p.guards->merged_total)
        << row << ": the step counter differs from step 6's prediction; " << describe(p);
}

}  // namespace overlay_test
