// tests/core/core_smoke_test.cpp
// Smoke test: verifies the core module compiles and links correctly.

#include <gtest/gtest.h>

#include "fixpp/core/version.hpp"

TEST(CoreSmoke, Compiles) { SUCCEED(); }

TEST(CoreSmoke, VersionStringNonEmpty) {
    ASSERT_NE(nullptr, fixpp::core::FIXPP_VERSION);
    ASSERT_GT(std::char_traits<char>::length(fixpp::core::FIXPP_VERSION), 0u);
}
