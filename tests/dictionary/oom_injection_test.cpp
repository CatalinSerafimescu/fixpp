// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/oom_injection_test.cpp
//
// Seam #9 — AC-L9 / AC-P2
//
// Verifies that PMR allocation failures inside XmlLoader::load_from_string are
// translated to dict::xml_oom_error (AC-L9) and never escape as raw
// std::bad_alloc (AC-P2).  Uses the failing_pmr_resource test seam to inject
// std::bad_alloc at a configurable call site.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fixpp/dict/error.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fstream>
#include <memory_resource>
#include <sstream>
#include <string>

#include "support/failing_pmr_resource.hpp"

namespace {

constexpr std::size_t k4MiB = 4UZ * 1024UZ * 1024UZ;

// Read FIX44.xml once into a std::string shared across all test cases.
// Done as a fixture class so we can ASSERT on open success without aborting
// the whole binary.
class OomInjection : public ::testing::Test {
protected:
    void SetUp() override {
        auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX44.xml";
        std::ifstream ifs{path};
        ASSERT_TRUE(ifs.is_open()) << "Cannot open FIX44.xml at: " << path;
        std::ostringstream oss;
        oss << ifs.rdbuf();
        xml_text_ = oss.str();
        ASSERT_FALSE(xml_text_.empty()) << "FIX44.xml read as empty string";
    }

    std::string xml_text_;
};

}  // namespace

// ---------------------------------------------------------------------------
// AC-L9: first allocation fails → xml_oom_error thrown (not std::bad_alloc).
// ---------------------------------------------------------------------------
TEST_F(OomInjection, EarlyAllocateFailsBecomesXmlOomError) {
    std::array<std::byte, k4MiB> buffer{};
    std::pmr::monotonic_buffer_resource upstream{buffer.data(), buffer.size()};
    fixpp::test_support::failing_pmr_resource fail{&upstream,
                                                   /*fail_on_call_n=*/1};

    EXPECT_THROW(
        {
            try {
                fixpp::dict::XmlLoader{}.load_from_string(xml_text_, &fail);
            } catch (fixpp::dict::xml_oom_error const& e) {
                EXPECT_EQ(e.code(), fixpp::core::error::dict_xml_oom);
                throw;  // re-throw so EXPECT_THROW sees it
            }
        },
        fixpp::dict::xml_oom_error);
}

// ---------------------------------------------------------------------------
// AC-L9: mid-parse allocation failure (call #10) also maps to xml_oom_error.
// ---------------------------------------------------------------------------
TEST_F(OomInjection, MidAllocateFailsBecomesXmlOomError) {
    std::array<std::byte, k4MiB> buffer{};
    std::pmr::monotonic_buffer_resource upstream{buffer.data(), buffer.size()};
    // fail_on_call_n = 10 is well into the parse; chosen as a representative
    // mid-parse injection point that exercises partial-build teardown.
    fixpp::test_support::failing_pmr_resource fail{&upstream,
                                                   /*fail_on_call_n=*/10};

    try {
        fixpp::dict::XmlLoader{}.load_from_string(xml_text_, &fail);
        FAIL() << "expected dict::xml_oom_error but load_from_string returned";
    } catch (fixpp::dict::xml_oom_error const& e) {
        // AC-L9: correct typed exception.
        EXPECT_EQ(e.code(), fixpp::core::error::dict_xml_oom);
        // what() — overridden message identifying the loader allocation site.
        EXPECT_NE(std::string_view{e.what()}.find("xml_oom_error"), std::string_view::npos)
            << "what() must identify the typed exception, got: " << e.what();
    } catch (std::bad_alloc const&) {
        // AC-P2 violation — raw std::bad_alloc must not escape.
        FAIL() << "std::bad_alloc escaped at call #10 — AC-P2 violation";
    } catch (std::exception const& ex) {
        FAIL() << "unexpected std::exception: " << ex.what();
    } catch (...) {
        FAIL() << "unexpected non-std exception type";
    }
}

// ---------------------------------------------------------------------------
// Sanity: fail_on_call_n == 0 means "never fail"; successful load expected.
// ---------------------------------------------------------------------------
TEST_F(OomInjection, NoFailMeansNoThrow) {
    std::array<std::byte, k4MiB> buffer{};
    std::pmr::monotonic_buffer_resource upstream{buffer.data(), buffer.size()};
    // fail_on_call_n == 0 disables injection; load must succeed.
    fixpp::test_support::failing_pmr_resource fail{&upstream,
                                                   /*fail_on_call_n=*/0};

    EXPECT_NO_THROW(fixpp::dict::XmlLoader{}.load_from_string(xml_text_, &fail));
}

// ---------------------------------------------------------------------------
// AC-P2 structural: std::bad_alloc MUST NOT escape; catch ladder order is
// intentional — xml_oom_error derives from std::bad_alloc so it MUST be
// caught first; the std::bad_alloc catch is the escape-detection arm.
// ---------------------------------------------------------------------------
TEST_F(OomInjection, BadAllocDoesNotEscape) {
    std::array<std::byte, k4MiB> buffer{};
    std::pmr::monotonic_buffer_resource upstream{buffer.data(), buffer.size()};
    fixpp::test_support::failing_pmr_resource fail{&upstream,
                                                   /*fail_on_call_n=*/1};

    try {
        fixpp::dict::XmlLoader{}.load_from_string(xml_text_, &fail);
        FAIL() << "expected throw";
    } catch (fixpp::dict::xml_oom_error const& e) {
        // AC-L9 + AC-P2 satisfied: PMR failure surfaced as xml_oom_error.
        EXPECT_EQ(e.code(), fixpp::core::error::dict_xml_oom);
    } catch (std::bad_alloc const&) {
        // This arm fires only if the raw std::bad_alloc escaped instead of
        // being wrapped — that is an AC-P2 violation.
        FAIL() << "std::bad_alloc escaped — AC-P2 violation";
    } catch (...) {
        FAIL() << "unexpected exception type";
    }
}
