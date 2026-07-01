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

// Concrete value assertions (MAJOR=1, MINOR=5, PATCH=0 — the 0->1 GA freeze:
// MAJOR 0->1 declares the C-ABI surface stable; MINOR is PRESERVED at 5 (the
// fifth additive minor) so the minor-keyed forward-compat downgrade stays
// coherent — resetting it to 0 would place the version below the introducing_minor
// of already-published codes. PY-001..005 validated the 0.5.0 surface and
// surfaced no C-ABI gap, so it froze unchanged in shape at 1.5.0).
TEST(CapiVersion, CApiVersionIsExactly_1_5_0) {
    fixpp_version_t v = fixpp_version();
    EXPECT_EQ(v.major, uint16_t{1});
    EXPECT_EQ(v.minor, uint16_t{5});
    EXPECT_EQ(v.patch, uint16_t{0});
}

// Composite macro: (MAJOR<<16)|(MINOR<<8)|PATCH
TEST(CapiVersion, CompositeMacroValue) {
    constexpr uint32_t expected =
        (static_cast<uint32_t>(FIXPP_C_ABI_VERSION_MAJOR) << 16u) |
        (static_cast<uint32_t>(FIXPP_C_ABI_VERSION_MINOR) << 8u) |
        static_cast<uint32_t>(FIXPP_C_ABI_VERSION_PATCH);
    EXPECT_EQ(static_cast<uint32_t>(FIXPP_C_ABI_VERSION), expected);
    // Exact numeric value for MAJOR=1, MINOR=5, PATCH=0 (the 0->1 GA freeze)
    EXPECT_EQ(static_cast<uint32_t>(FIXPP_C_ABI_VERSION), uint32_t{(1u << 16u) | (5u << 8u) | 0u});
}

// ── Library version accessors ─────────────────────────────────────────────────

TEST(CapiVersion, LibraryVersionIsExactly_0_0_1) {
    fixpp_version_t lv = fixpp_library_version();
    EXPECT_EQ(lv.major, uint16_t{0});
    EXPECT_EQ(lv.minor, uint16_t{0});
    EXPECT_EQ(lv.patch, uint16_t{1});
}

// The two tracks are independent ([arch §9.2] / AC-2): the C-ABI surface version
// and the C++ library SemVer advance separately.
TEST(CapiVersion, CApiAndLibraryVersionsAreDecoupled) {
    fixpp_version_t cabi = fixpp_version();
    fixpp_version_t lib  = fixpp_library_version();
    // Library is 0.0.1; C-ABI froze at 1.5.0 — the major values differ (the
    // stablest discriminator across the freeze; the minors differ too, 5 vs 0).
    EXPECT_NE(cabi.major, lib.major);
}
