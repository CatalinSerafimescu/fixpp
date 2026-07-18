// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/test_077_builder_dedup_count.cpp
//
// 077-builder-args-dedup T013 [US1] -- dedup struct-count assertion
// (specs/077-builder-args-dedup/tasks.md T013; contracts/generated-builder-
// dedup.md G1/G5; SC-002). Pure TEXT parse of the generated
// fixpp::vlatest::groups.hpp -- does NOT #include it (that TU-compile risk
// is T012/T015's concern; see the OOM protocol in this feature's brief) --
// so this witness is cheap and isolation-safe.
//
// 078-precompiled-builder-libs re-point (census T016): the `G_...Args`
// structs moved out of the (now-gone) Builders.hpp monolith into the
// data-only per-version groups.hpp (Entity 1, FR-012) -- this text-parse
// targets that file, NOT all.hpp (which holds only #includes + the
// builder_registry array).
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
#include <filesystem>
#include <fstream>
#include <string>

namespace {

// FIXPP_CODEGEN_VLATEST_BUILDERS_HPP is already defined by
// tests/codegen/CMakeLists.txt for this binary (codegen_vlatest_tests),
// consumed today by vlatest_compile_smoke_test.cpp's
// BuildersHeaderEmittedDeduped existence check -- it now names the
// per-version all.hpp aggregator. groups.hpp (the G_...Args structs' new
// data-only home, 078-precompiled-builder-libs) is a sibling in the same
// per-version directory.
// 078-precompiled-builder-libs SC-001 fix: groups.hpp became an UMBRELLA of
// #includes (no struct bodies); the 576 `struct G_...Args` definitions now
// live one-per-file under the sibling `groups/` directory (per-plan headers).
// This gate walks that directory instead of the (now bodyless) umbrella.
std::string const kGroupsDir =
    (std::filesystem::path(FIXPP_CODEGEN_VLATEST_BUILDERS_HPP).parent_path() / "groups").string();

// Count top-level `struct G_...Args {` declarations at column 0 (the real
// emitted format -- NOT 2-space-indented) across every per-plan header in the
// `groups/` directory. Returns -1 if the directory cannot be read
// (distinguishes "absent/unreadable" from "found zero"). `file_size` is the
// summed byte size of all per-plan headers.
long count_group_structs(std::string const& dir, bool& saw_groups_namespace,
                          long& file_size) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return -1;
    long count = 0;
    file_size = 0;
    saw_groups_namespace = false;
    for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.path().extension() != ".hpp") continue;
        std::ifstream in(entry.path(), std::ios::binary | std::ios::ate);
        if (!in) continue;
        file_size += static_cast<long>(in.tellg());
        in.seekg(0);
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("struct G_", 0) == 0) {
                ++count;
            }
            if (line.find("namespace fixpp::vlatest::groups") != std::string::npos) {
                saw_groups_namespace = true;
            }
        }
    }
    return count;
}

}  // namespace

TEST(BuilderDedupCount077, VlatestStructCountIs576) {
    bool saw_groups_namespace = false;
    long file_size = 0;
    long const count = count_group_structs(kGroupsDir, saw_groups_namespace, file_size);
    ASSERT_GE(count, 0)
        << "fixpp::vlatest per-plan groups/ dir not found/readable at " << kGroupsDir
        << " -- expected the deduped emitter (FIXPP_CODEGEN_FIX_LATEST=ON) to have "
           "emitted it and cmake/Codegen.cmake to have kept it (T014); 078 SC-001 "
           "fix moved the G_...Args structs from the umbrella groups.hpp into "
           "one-per-file per-plan headers under groups/.";
    EXPECT_EQ(count, 576)
        << "vlatest dedup plan count changed -- investigate before re-pinning "
           "(.specify/decisions/077-builder-args-dedup-verify.md).";
    EXPECT_TRUE(saw_groups_namespace)
        << "namespace fixpp::vlatest::groups not found in any per-plan header under " << kGroupsDir;

    // SC-002: generous sanity band over the SUMMED per-plan header sizes --
    // rules out an empty/truncated write (too small) and the pre-dedup ~137 MB
    // uncompilable shape (too large), without pinning a tight byte count the
    // emitter's own formatting could drift on.
    EXPECT_GT(file_size, 1L * 1024 * 1024)
        << "groups/ per-plan headers suspiciously small (" << file_size << " bytes total) -- possibly truncated";
    EXPECT_LT(file_size, 150L * 1024 * 1024)
        << "groups/ per-plan headers suspiciously large (" << file_size << " bytes total) -- dedup may not "
           "be collapsing shared plans (SC-002)";
}
