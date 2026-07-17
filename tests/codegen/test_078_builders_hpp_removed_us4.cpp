// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/test_078_builders_hpp_removed_us4.cpp
//
// 078-precompiled-builder-libs T031 [US4] [TESTS-FIRST]: negative check --
// the old include path is gone (US4 AC2 / SC-005 removal leg / FR-008).
//
// Discrimination: for v44/v50sp2 (always built, no ON/OFF gate), TWO
// __has_include guards, so a mis-wired include root cannot green-pass
// vacuously (a positive control alongside the negative one); vlatest is
// runtime-filesystem-only (see below -- it is gated behind
// FIXPP_CODEGEN_FIX_LATEST and not built locally on this host):
//   (a) `#if __has_include(<fixpp/<ns>/Builders.hpp>)` -> #error. If the
//       monolith is genuinely gone, this TU only compiles BECAUSE the
//       branch is not taken.
//   (b) `#if !__has_include(<fixpp/<ns>/all.hpp>)` -> #error. Proves the
//       include root actually resolves this version's directory at all --
//       without this, a wrong/empty -I path would make (a) pass for the
//       wrong reason (nothing under that root resolves, monolith or not).
// Both guards passing together is the real "old path gone, new path there"
// property; (a) alone is vacuous against a broken include root.
//
// Runtime std::filesystem leg (US4 AC2, quickstart.md Scenario 5): the
// SAME two facts, re-checked at RUNTIME against the resolved codegen output
// directory, for all three builder-bearing versions (v44/v50sp2/vlatest) --
// catches a stale LEFTOVER Builders.hpp on disk from a pre-078 generation
// run that an incrementally-reconfigured build tree failed to clean (main.cpp's
// `remove_builder_split_tree` is only invoked on the empty-emitted-file
// branch -- vt11/v42 -- never on the v44/v50sp2/vlatest non-empty branch;
// see this task's escalation note in the phase report). A FRESH build tree
// never writes Builders.hpp in the first place (write_file's v44/v50sp2/
// vlatest file list never includes it post-emitter-split), so this leg is
// correct-by-construction for CI; locally it is a genuine regression guard
// against exactly the staleness class this session found and cleaned.
//
// Anchors: specs/078-precompiled-builder-libs/spec.md US4 AC2, SC-005,
// FR-008; quickstart.md Scenario 5; tests/codegen/test_077_builder_no_emit.cpp
// (filesystem existence-check style precedent); tools/codegen/fixpp-codegen/
// main.cpp (write_file / remove_builder_split_tree).

#include <gtest/gtest.h>

#include <filesystem>

#ifndef FIXPP_CODEGEN_OUT_DIR
#error "FIXPP_CODEGEN_OUT_DIR must be set by CMake target_compile_definitions"
#endif

// ── v44 ──────────────────────────────────────────────────────────────────
#if __has_include(<fixpp/v44/Builders.hpp>)
#error "fixpp/v44/Builders.hpp still resolves -- the pre-078 monolith was not removed (FR-008)"
#endif
#if !__has_include(<fixpp/v44/all.hpp>)
#error "fixpp/v44/all.hpp does not resolve -- include root is mis-wired (positive control)"
#endif
#include <fixpp/v44/all.hpp>

// ── v50sp2 ───────────────────────────────────────────────────────────────
#if __has_include(<fixpp/v50sp2/Builders.hpp>)
#error "fixpp/v50sp2/Builders.hpp still resolves -- the pre-078 monolith was not removed (FR-008)"
#endif
#if !__has_include(<fixpp/v50sp2/all.hpp>)
#error "fixpp/v50sp2/all.hpp does not resolve -- include root is mis-wired (positive control)"
#endif
#include <fixpp/v50sp2/all.hpp>

// vlatest: NOT built locally on this WSL2 host (orchestrator brief -- OOM
// risk, feedback_build_resource_cap_oom) and gated behind
// FIXPP_CODEGEN_FIX_LATEST (default ON, but must compile with it OFF per
// FR-003/B-1 "build succeeds when OFF" -- test_077_builder_no_emit.cpp's own
// vlatest handling is a filesystem-existence check for the same reason, not
// a static #include). No compile-time __has_include guard here for vlatest;
// the runtime filesystem leg below covers it (and degrades gracefully with
// FIXPP_CODEGEN_FIX_LATEST=OFF via the exists()-guarded skip).

TEST(BuildersHppRemovedUS4, CompileTimeGuardsPassed) {
    // If this TU compiled at all, every __has_include guard above already
    // passed -- SUCCEED() just gives the compile-time proof a runnable test
    // name/result in the ctest log (repo convention, e.g.
    // tests/sync/test_consumer_contract_compile.cpp:135).
    SUCCEED() << "v44/v50sp2 Builders.hpp negative + all.hpp positive-control "
                 "__has_include guards enforced at compile time (see #if above).";
}

TEST(BuildersHppRemovedUS4, RuntimeFilesystemCheckAcrossBuilderBearingVersions) {
    std::filesystem::path const root(FIXPP_CODEGEN_OUT_DIR);
    for (char const* ns : {"v44", "v50sp2", "vlatest"}) {
        std::filesystem::path const ns_dir = root / "fixpp" / ns;
        if (!std::filesystem::exists(ns_dir)) {
            continue;  // vlatest with FIXPP_CODEGEN_FIX_LATEST=OFF
        }
        EXPECT_FALSE(std::filesystem::exists(ns_dir / "Builders.hpp"))
            << ns << "/Builders.hpp exists on disk -- stale pre-078 monolith "
            << "left by an incremental reconfigure (main.cpp never removes it "
            << "on the non-empty-emit branch); expected NO monolith (FR-008).";
        EXPECT_TRUE(std::filesystem::exists(ns_dir / "all.hpp"))
            << ns << "/all.hpp missing -- the new aggregator should be present "
            << "for every builder-bearing version.";
    }
}
