// tests/capi/version_test.cpp
// US1 (CA-004): fixpp_version() / fixpp_library_version() correctness (T004)
// TDD: written RED before version.h / version.cpp exist.

#include "fix/c_api/version.h"  // under test — direct include, not umbrella

#include <gtest/gtest.h>

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

// Concrete value assertion: pins the current C-ABI version. The GA freeze
// PRESERVED MINOR (it did not reset it to 0) so the minor-keyed forward-compat
// downgrade stays coherent: a reset would place the version below the
// introducing_minor of already-published codes. Every later C-ABI change bumps
// MINOR ([const §X.7]); what each minor changed is recorded in version.h.
TEST(CapiVersion, CApiVersionIsExactly_1_10_0) {
    fixpp_version_t v = fixpp_version();
    EXPECT_EQ(v.major, uint16_t{1});
    EXPECT_EQ(v.minor, uint16_t{10});
    EXPECT_EQ(v.patch, uint16_t{0});
}

// Composite macro: (MAJOR<<16)|(MINOR<<8)|PATCH
TEST(CapiVersion, CompositeMacroValue) {
    constexpr uint32_t expected = (static_cast<uint32_t>(FIXPP_C_ABI_VERSION_MAJOR) << 16U) |
                                  (static_cast<uint32_t>(FIXPP_C_ABI_VERSION_MINOR) << 8U) |
                                  static_cast<uint32_t>(FIXPP_C_ABI_VERSION_PATCH);
    EXPECT_EQ(static_cast<uint32_t>(FIXPP_C_ABI_VERSION), expected);
    // Exact numeric value of the version CApiVersionIsExactly_* pins
    // The `| 0U` spells out the PATCH term of the (MAJOR<<16)|(MINOR<<8)|PATCH encoding.
    // cppcheck-suppress badBitmaskCheck
    EXPECT_EQ(static_cast<uint32_t>(FIXPP_C_ABI_VERSION), uint32_t{(1U << 16U) | (10U << 8U) | 0U});
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
    fixpp_version_t lib = fixpp_library_version();
    // The two tracks advance independently; their MAJOR values differ, which
    // is the stablest discriminator across a freeze (the minors differ too).
    EXPECT_NE(cabi.major, lib.major);
}
