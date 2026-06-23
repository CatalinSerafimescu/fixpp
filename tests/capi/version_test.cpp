// tests/capi/version_test.cpp
// US1 (CA-004): fixpp_version() / fixpp_library_version() correctness (T004)
// TDD: written RED before version.h / version.cpp exist.

#include <gtest/gtest.h>

#include "fix/c_api/version.h"  // under test — direct include, not umbrella

// ── C-ABI version accessors ──────────────────────────────────────────────────

TEST(CapiVersion, CApiVersionMatchesMajorMacro) {
    fixpp_version_t v = fixpp_version();
    EXPECT_EQ(v.major, static_cast<uint16_t>(FIXPP_C_ABI_VERSION_MAJOR));
}

TEST(CapiVersion, CApiVersionMatchesMinorMacro) {
    fixpp_version_t v = fixpp_version();
    EXPECT_EQ(v.minor, static_cast<uint16_t>(FIXPP_C_ABI_VERSION_MINOR));
}

TEST(CapiVersion, CApiVersionMatchesPatchMacro) {
    fixpp_version_t v = fixpp_version();
    EXPECT_EQ(v.patch, static_cast<uint16_t>(FIXPP_C_ABI_VERSION_PATCH));
}

// Concrete value assertions (AC-2: MAJOR=0, MINOR=2, PATCH=0 for this feature)
TEST(CapiVersion, CApiVersionIsExactly_0_2_0) {
    fixpp_version_t v = fixpp_version();
    EXPECT_EQ(v.major, uint16_t{0});
    EXPECT_EQ(v.minor, uint16_t{2});
    EXPECT_EQ(v.patch, uint16_t{0});
}

// Composite macro: (MAJOR<<16)|(MINOR<<8)|PATCH
TEST(CapiVersion, CompositeMacroValue) {
    constexpr uint32_t expected =
        (static_cast<uint32_t>(FIXPP_C_ABI_VERSION_MAJOR) << 16u) |
        (static_cast<uint32_t>(FIXPP_C_ABI_VERSION_MINOR) << 8u) |
        static_cast<uint32_t>(FIXPP_C_ABI_VERSION_PATCH);
    EXPECT_EQ(static_cast<uint32_t>(FIXPP_C_ABI_VERSION), expected);
    // Exact numeric value for MAJOR=0, MINOR=2, PATCH=0
    EXPECT_EQ(static_cast<uint32_t>(FIXPP_C_ABI_VERSION), uint32_t{(0u << 16u) | (2u << 8u) | 0u});
}

// ── Library version accessors ─────────────────────────────────────────────────

TEST(CapiVersion, LibraryVersionIsExactly_0_0_1) {
    fixpp_version_t lv = fixpp_library_version();
    EXPECT_EQ(lv.major, uint16_t{0});
    EXPECT_EQ(lv.minor, uint16_t{0});
    EXPECT_EQ(lv.patch, uint16_t{1});
}

// The two tracks are independent ([arch §9.2] / AC-2): C-ABI minor != library minor
TEST(CapiVersion, CApiAndLibraryVersionsAreDecoupled) {
    fixpp_version_t cabi = fixpp_version();
    fixpp_version_t lib  = fixpp_library_version();
    // Library is 0.0.1; C-ABI is 0.2.0 — the minor values differ
    EXPECT_NE(cabi.minor, lib.minor);
}
