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
// Asserts (078-precompiled-builder-libs: Builders.hpp -> all.hpp, the new
// per-version emission sentinel; census table T014):
//   - vt11/all.hpp AND vt11/messages/ are ABSENT (admin-only, 0 application
//     messages; write_file empty-skip -- G4a).
//   - v42/all.hpp AND v42/messages/ are ABSENT (DESCOPED, issue #196 /
//     L-063-1: FIX 4.2 NumInGroup=INT => 0 typed groups; the driver excludes
//     v42 from builder emission at T017, main.cpp's job loop, not a
//     namespace gate inside emit_builders.cpp).
//   - v44/all.hpp, v50sp2/all.hpp, vlatest/all.hpp ARE present (positive
//     control -- the three builder-bearing versions).
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
           "all.hpp (empty-skip, G4a). Found one at "
        << FIXPP_CODEGEN_VT11_BUILDERS_HPP;
    EXPECT_FALSE(std::filesystem::exists(
        std::filesystem::path(FIXPP_CODEGEN_VT11_BUILDERS_HPP).parent_path() / "messages"))
        << "vt11 is FIXT admin-only (0 application messages); expected NO messages/ dir (G4a).";
}

// 082-structural-group-detection T032 [US2] — FR-016b: this assertion was
// `V42EmitsNoBuilders` and is **INVERTED, not deleted**, so the descope's
// retirement is witnessed by the same pin that enforced it.
//
// 077's premise was L-063-1: "FIX 4.2 NumInGroup=INT => 0 typed groups", so a v42
// builder tier had nothing to carry and `main.cpp`'s `if (ir.ns != "v42")`
// excluded it (077 T017). 082 removes that premise — detection is now structural,
// so FIX42's 18 declared groups ARE visible (T023–T025) — and T035 deletes the
// exclusion with no replacement version predicate.
//
// ⚠️ `Vt11EmitsNoBuilders` above is deliberately UNCHANGED. vt11 must keep
// emitting nothing, but for a *different and genuine* reason: it has zero
// application messages, so `emit_builders`' `is_application` gate empties its
// registry by construction rather than by a version check (FR-010 / T038). If
// both tests had been inverted together, that distinction would be lost.
TEST(BuilderNoEmit077, V42EmitsBuilders) {
    EXPECT_TRUE(std::filesystem::exists(FIXPP_CODEGEN_V42_BUILDERS_HPP))
        << "v42's builder tier is IN SCOPE as of 082 (issue #196): the L-063-1 descope premise is "
           "retired and T035 removed main.cpp's `if (ir.ns != \"v42\")` exclusion, so all.hpp must "
           "be emitted. Missing at "
        << FIXPP_CODEGEN_V42_BUILDERS_HPP;
    EXPECT_TRUE(std::filesystem::exists(
        std::filesystem::path(FIXPP_CODEGEN_V42_BUILDERS_HPP).parent_path() / "messages"))
        << "v42 must emit a messages/ dir -- 39 application messages are in builder scope under "
           "`--families all` (T031's derivation).";
}

// Positive control: the three builder-bearing versions DO emit.
TEST(BuilderNoEmit077, BuilderBearingVersionsPresent) {
    EXPECT_TRUE(std::filesystem::exists(FIXPP_CODEGEN_V44_BUILDERS_HPP))
        << "v44/all.hpp missing: " << FIXPP_CODEGEN_V44_BUILDERS_HPP;
    EXPECT_TRUE(std::filesystem::exists(FIXPP_CODEGEN_V50SP2_BUILDERS_HPP))
        << "v50sp2/all.hpp missing: " << FIXPP_CODEGEN_V50SP2_BUILDERS_HPP;
    EXPECT_TRUE(std::filesystem::exists(FIXPP_CODEGEN_VLATEST_BUILDERS_HPP))
        << "vlatest/all.hpp missing: " << FIXPP_CODEGEN_VLATEST_BUILDERS_HPP;
}
