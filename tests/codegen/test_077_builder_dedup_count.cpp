// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/test_077_builder_dedup_count.cpp
//
// 077-builder-args-dedup T013 [US1] -- dedup struct-count assertion
// (specs/077-builder-args-dedup/tasks.md T013; contracts/generated-builder-
// dedup.md G1/G5; SC-002). Pure TEXT parse of the generated
// fixpp::vlatest::Builders.hpp -- does NOT #include it (that TU-compile risk
// is T012/T015's concern; see the OOM protocol in this feature's brief) --
// so this witness is cheap and isolation-safe.
//
// Asserts:
//   (1) the emitted `struct G_<no_tag>[_<ordinal>]Args {` count is exactly
//       576 -- the T005 census pin for vlatest (NOT ~26k message-rooted).
//       T017 (US2) found the build-tree's pre-fix 573 traced to a real bug:
//       emit_builders.cpp's `all`-mode N-002/N-003 exclusion set
//       {BE,BF,BW,BX,BY} was applied version-UNSCOPED, so vlatest (like
//       v50sp2) lost 5 genuine application messages it should never have
//       excluded (that set is v44-specific -- research.md R4). Fixing the
//       exclusion to `ir.ns == "v44"`-gated recovers exactly the T005
//       census's 576, confirming 573 was the bug, not the golden authority
//       (.specify/decisions/077-builder-args-dedup-verify.md's "reconcile
//       the -3 at golden-gen" note is now closed). Struct declarations sit
//       at column 0 directly inside
//       `namespace fixpp::vlatest::groups` (confirmed empirically against
//       the real emitted file -- NOT the 2-space-indented illustration in
//       quickstart.md/contracts/generated-builder-dedup.md G1, which does
//       not match emit_builders.cpp's actual output format).
//   (2) `namespace fixpp::vlatest::groups` is present (the dedup flyweight
//       namespace, G1).
//   (3) file size is in a generous "not ~137 MB uncompilable, not empty/
//       truncated" band (SC-002) -- a loose sanity bound, not a tight ~10 MB
//       pin (the measured size is ~74 MB; quickstart's "~10 MB order"
//       estimate was low, per this feature's T013 brief).
//
// RED before T014: cmake/Codegen.cmake currently deletes
// vlatest/Builders.hpp unconditionally on every ON configure that does not
// otherwise trigger a full regen (076 Gate B P2 leftover, cmake/
// Codegen.cmake:330-342) -- so the file is ABSENT and this test fails to
// even open it. T014 removes that unconditional delete + wires a proper
// regen-guard marker, making this GREEN.
//
// Anchors: specs/077-builder-args-dedup/tasks.md T013;
//          specs/077-builder-args-dedup/contracts/generated-builder-dedup.md
//          G1/G1a/G5;
//          .specify/decisions/077-builder-args-dedup-verify.md (576 pin,
//          T017 N-002/N-003 version-scoping fix).

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>

namespace {

// FIXPP_CODEGEN_VLATEST_BUILDERS_HPP is already defined by
// tests/codegen/CMakeLists.txt for this binary (codegen_vlatest_tests),
// consumed today by vlatest_compile_smoke_test.cpp's
// BuildersHeaderEmittedDeduped existence check -- reused here for the
// text-parse.
constexpr char const* kBuildersPath = FIXPP_CODEGEN_VLATEST_BUILDERS_HPP;

// Count top-level `struct G_...Args {` declarations at column 0 (the real
// emitted format -- NOT 2-space-indented). Returns -1 if the file cannot be
// opened (distinguishes "absent/unreadable" from "found zero").
long count_group_structs(std::string const& path, bool& saw_groups_namespace,
                          long& file_size) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return -1;
    file_size = static_cast<long>(in.tellg());
    in.seekg(0);

    long count = 0;
    saw_groups_namespace = false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("struct G_", 0) == 0) {
            ++count;
        }
        if (line.find("namespace fixpp::vlatest::groups") != std::string::npos) {
            saw_groups_namespace = true;
        }
    }
    return count;
}

}  // namespace

TEST(BuilderDedupCount077, VlatestStructCountIs576) {
    bool saw_groups_namespace = false;
    long file_size = 0;
    long const count = count_group_structs(kBuildersPath, saw_groups_namespace, file_size);
    ASSERT_GE(count, 0)
        << "fixpp::vlatest::Builders.hpp not found/readable at " << kBuildersPath
        << " -- expected the deduped emitter (FIXPP_CODEGEN_FIX_LATEST=ON) to have "
           "emitted it and cmake/Codegen.cmake to have kept it (T014).";
    EXPECT_EQ(count, 576)
        << "vlatest dedup plan count changed -- investigate before re-pinning "
           "(.specify/decisions/077-builder-args-dedup-verify.md).";
    EXPECT_TRUE(saw_groups_namespace)
        << "namespace fixpp::vlatest::groups not found in " << kBuildersPath;

    // SC-002: generous sanity band -- rules out an empty/truncated write (too
    // small) and the pre-dedup ~137 MB uncompilable shape (too large), without
    // pinning a tight byte count the emitter's own formatting could drift on.
    EXPECT_GT(file_size, 1L * 1024 * 1024)
        << "Builders.hpp suspiciously small (" << file_size << " bytes) -- possibly truncated";
    EXPECT_LT(file_size, 150L * 1024 * 1024)
        << "Builders.hpp suspiciously large (" << file_size << " bytes) -- dedup may not "
           "be collapsing shared plans (SC-002)";
}
