// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/test_077_builder_no_emit.cpp
//
// 077-builder-args-dedup T018 [US2] -- vt11/v42 emit-no-builders witness
// (specs/077-builder-args-dedup/tasks.md T018; contracts/generated-builder-
// dedup.md G4a; FR-006). Pure filesystem existence check against the real
// generated tree (fixpp_codegen_generate, FIXPP_CODEGEN_FIX_LATEST=ON) --
// no #include of any Builders.hpp, so this witness is cheap and
// isolation-safe (mirrors vlatest_compile_smoke_test.cpp's existence-check
// style, not test_077_builder_dedup_count.cpp's text-parse).
//
// Asserts:
//   - vt11/Builders.hpp is ABSENT (admin-only, 0 application messages;
//     write_file empty-skip -- G4a).
//   - v42/Builders.hpp is ABSENT (DESCOPED, issue #196 / L-063-1: FIX 4.2
//     NumInGroup=INT => 0 typed groups; the driver excludes v42 from
//     builder emission at T017, main.cpp's job loop, not a namespace gate
//     inside emit_builders.cpp).
//   - v44/Builders.hpp, v50sp2/Builders.hpp, vlatest/Builders.hpp ARE
//     present (positive control -- the three builder-bearing versions).
// Neither vt11 nor v42's absence is an error (FR-006, edge case, G4a).
//
// Anchors: specs/077-builder-args-dedup/tasks.md T018;
//          specs/077-builder-args-dedup/contracts/generated-builder-dedup.md
//          G4a;
//          .specify/decisions/077-builder-args-dedup-verify.md (v42 descope,
//          T017 driver-level exclusion).

#include <gtest/gtest.h>

#include <filesystem>

#ifndef FIXPP_CODEGEN_V42_BUILDERS_HPP
#error "FIXPP_CODEGEN_V42_BUILDERS_HPP must be set by CMake target_compile_definitions"
#endif
#ifndef FIXPP_CODEGEN_V44_BUILDERS_HPP
#error "FIXPP_CODEGEN_V44_BUILDERS_HPP must be set by CMake target_compile_definitions"
#endif
#ifndef FIXPP_CODEGEN_V50SP2_BUILDERS_HPP
#error "FIXPP_CODEGEN_V50SP2_BUILDERS_HPP must be set by CMake target_compile_definitions"
#endif
#ifndef FIXPP_CODEGEN_VT11_BUILDERS_HPP
#error "FIXPP_CODEGEN_VT11_BUILDERS_HPP must be set by CMake target_compile_definitions"
#endif
// FIXPP_CODEGEN_VLATEST_BUILDERS_HPP already defined by
// tests/codegen/CMakeLists.txt for this binary (codegen_vlatest_tests),
// consumed today by test_077_builder_dedup_count.cpp -- reused here for the
// positive-control check.

TEST(BuilderNoEmit077, Vt11EmitsNoBuilders) {
    EXPECT_FALSE(std::filesystem::exists(FIXPP_CODEGEN_VT11_BUILDERS_HPP))
        << "vt11 is FIXT admin-only (0 application messages); expected NO "
           "Builders.hpp (empty-skip, G4a). Found one at "
        << FIXPP_CODEGEN_VT11_BUILDERS_HPP;
}

TEST(BuilderNoEmit077, V42EmitsNoBuilders) {
    EXPECT_FALSE(std::filesystem::exists(FIXPP_CODEGEN_V42_BUILDERS_HPP))
        << "v42 is DESCOPED (issue #196 / L-063-1: FIX 4.2 NumInGroup=INT => "
           "0 typed groups); expected NO Builders.hpp (driver-level "
           "exclusion, T017). Found one at "
        << FIXPP_CODEGEN_V42_BUILDERS_HPP << " -- would mean the T017 exclusion regressed.";
}

// Positive control: the three builder-bearing versions DO emit.
TEST(BuilderNoEmit077, BuilderBearingVersionsPresent) {
    EXPECT_TRUE(std::filesystem::exists(FIXPP_CODEGEN_V44_BUILDERS_HPP))
        << "v44/Builders.hpp missing: " << FIXPP_CODEGEN_V44_BUILDERS_HPP;
    EXPECT_TRUE(std::filesystem::exists(FIXPP_CODEGEN_V50SP2_BUILDERS_HPP))
        << "v50sp2/Builders.hpp missing: " << FIXPP_CODEGEN_V50SP2_BUILDERS_HPP;
    EXPECT_TRUE(std::filesystem::exists(FIXPP_CODEGEN_VLATEST_BUILDERS_HPP))
        << "vlatest/Builders.hpp missing: " << FIXPP_CODEGEN_VLATEST_BUILDERS_HPP;
}
