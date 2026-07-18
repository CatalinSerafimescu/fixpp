// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/vlatest_compile_smoke_test.cpp
//
// 076-fix-latest-typed-codegen T008 [US1] -- compile-smoke witness
// (specs/076-fix-latest-typed-codegen/tasks.md T008; US1 acceptance
// scenario 1): the generated fixpp::vlatest {Fields,Messages,Validator,
// Reify}.hpp headers exist and compile, and carry real fixpp::vlatest
// symbols -- confirming emit_messages/emit_validator/emit_reify (T002/T003/
// T005's ir.ns == "vlatest" partition) produce the tier "for free" once
// Foundational (Phase 2) lands, with NO emitter code change needed for this
// leg (mirrors tests/session/test_069_us1_smoke.cpp's per-tier smoke-witness
// role).
//
// SCOPE NOTE: deliberately does NOT include fixpp/vlatest/Builders.hpp (nor
// reference fixpp::vlatest::builder_registry). Builders.hpp was 131MB /
// ~2.07M lines for the 173-message app-subset under 076's per-message,
// non-deduplicated emitter (its nested-group Args structs grew
// combinatorially with nesting depth -- unlike v44/Builders.hpp: 83
// messages / 3.7MB) -- a single TU #including it was empirically measured
// (076 phase-3) to exceed 22GB RSS and 6m51s wall-clock without finishing
// compilation, SIGKILL'd to protect the 24GB build host.
//
// 077-builder-args-dedup re-enables the tier via component-identity Args
// dedup (576 shared plans instead of ~26k message-rooted structs, ~78MB;
// T017 fixed a version-unscoped N-002/N-003 exclusion bug that had put the
// build-tree count at 573 -- see BuilderDedupCount077.VlatestStructCountIs576) --
// see BuildersHeaderEmittedDeduped below (flipped to assert PRESENCE) and
// tests/session/test_077_vlatest_builder_roundtrip.cpp / this binary's
// BuilderDedupCount077.VlatestStructCountIs576 (T012/T013) for the
// #include/compile leg this TU still deliberately avoids -- this TU only
// needs the read-tier headers named in the task -- {Fields,Messages,
// Validator,Reify} -- which are v50sp2-comparable in size/cost.
//
// Anchors: specs/076-fix-latest-typed-codegen/tasks.md T008;
//          tests/session/test_069_us1_smoke.cpp (per-tier smoke precedent).

#include <gtest/gtest.h>

#include <filesystem>
#include <fixpp/dict/field_ref.hpp>
#include <fixpp/vlatest/Fields.hpp>
#include <fixpp/vlatest/Messages.hpp>
#include <fixpp/vlatest/Reify.hpp>
#include <fixpp/vlatest/Validator.hpp>
#include <string_view>
#include <type_traits>

// Compile-time: real fixpp::vlatest symbols from all four headers are
// visible and usable. A missing/misnamed emitter output (wrong namespace,
// wrong ir.ns partition) fails this TU to COMPILE, not just a runtime
// assertion.
static_assert(fixpp::vlatest::fields::Heartbeat_fields[0].tag == 8,
              "Fields.hpp: fixpp::vlatest::fields::Heartbeat_fields must be visible/populated");
static_assert(fixpp::vlatest::owning_Heartbeat::msg_type_v == "0",
              "Reify.hpp: fixpp::vlatest::owning_Heartbeat::msg_type_v must be MsgType 0");
static_assert(fixpp::vlatest::validator::Heartbeat_rules.size() > 0,
              "Validator.hpp: fixpp::vlatest::validator::Heartbeat_rules must be non-empty");

TEST(VlatestCompileSmoke076, FourReadTierHeadersCarryRealSymbols) {
    // Messages.hpp: owning_<Msg> is at least forward-declared (full
    // definition lives in Reify.hpp, also included above) -- naming it here
    // exercises Messages.hpp's own declaration.
    using MsgFwdDecl = fixpp::vlatest::owning_TestRequest;
    static_assert(!std::is_void_v<MsgFwdDecl>);

    EXPECT_EQ(fixpp::vlatest::owning_Heartbeat::msg_type_v, "0");
    EXPECT_EQ(fixpp::vlatest::owning_Heartbeat::which(),
              fixpp::dict::application_version::v50sp2);
    EXPECT_GT(fixpp::vlatest::validator::Heartbeat_rules.size(), 0U);
    EXPECT_EQ(fixpp::vlatest::fields::Heartbeat_fields[4].tag, 35)
        << "MsgType(35) must be present in the generated Heartbeat field table";
}

// 077-builder-args-dedup T014 [US1]: the typed builder tier, descoped by 076
// (gate-b/r1 P2 -- see the SCOPE NOTE above), is RE-ENABLED by 077's
// component-identity Args-dedup emitter (cmake/Codegen.cmake no longer
// deletes vlatest/Builders.hpp on an ON configure -- FR-004/FR-012, G4a).
// This test's invariant is exactly reversed from 076: pin that the header
// NOW EXISTS and has the deduped shape (struct count is pinned precisely by
// BuilderDedupCount077.VlatestStructCountIs576 in this same binary, T013).
//
// 078-precompiled-builder-libs re-point (census T015): the monolithic
// Builders.hpp is GONE (FR-008) -- existence now targets the per-version
// all.hpp aggregator (Entity 5) plus the per-message messages/ set (Entity
// 2/3/4). The pre-dedup-era ~137MB / ~100MB monolith size band is retired
// outright, not retargeted: there is no monolith left to bound -- all.hpp
// holds only #includes + the builder_registry array, and no single split
// artifact approaches that scale.
TEST(VlatestCompileSmoke076, BuildersHeaderEmittedDeduped) {
    ASSERT_TRUE(std::filesystem::exists(FIXPP_CODEGEN_VLATEST_BUILDERS_HPP))
        << "fixpp::vlatest::all.hpp must be emitted -- 077 re-enables "
           "the typed builder tier via structural-plan dedup "
           "(specs/077-builder-args-dedup/tasks.md T014).";
    auto const messages_dir = std::filesystem::path(FIXPP_CODEGEN_VLATEST_BUILDERS_HPP).parent_path() / "messages";
    ASSERT_TRUE(std::filesystem::exists(messages_dir)) << "fixpp::vlatest::messages/ set must be emitted alongside "
                                                            "all.hpp (078-precompiled-builder-libs Entity 2/3/4).";
    EXPECT_GT(std::distance(std::filesystem::directory_iterator(messages_dir), std::filesystem::directory_iterator{}),
              0)
        << "fixpp::vlatest::messages/ must be non-empty.";
}
