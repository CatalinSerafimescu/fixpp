// tests/capi/capi_smoke_test.cpp
// Smoke test: verifies the C ABI compiles and the one wired symbol works.

#include <gtest/gtest.h>
#include "fix/c_api.h"
#include <cstring>

TEST(CapiSmoke, Compiles) {
    SUCCEED();
}

TEST(CapiSmoke, VersionStringReturnsNonEmpty) {
    const char* v = fixpp_version_string();
    ASSERT_NE(nullptr, v);
    ASSERT_GT(std::strlen(v), 0u);
}
