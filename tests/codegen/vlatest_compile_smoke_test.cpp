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
// reference fixpp::vlatest::builder_registry). Builders.hpp is 131MB /
// ~2.07M lines for the 173-message app-subset (its per-message,
// non-deduplicated nested-group Args structs grow combinatorially with
// nesting depth -- unlike v44/Builders.hpp: 83 messages / 3.7MB). A single
// TU #including it was empirically measured (this implementer, 076 phase-3)
// to exceed 22GB RSS and 6m51s wall-clock without finishing compilation,
// SIGKILL'd to protect the 24GB build host. Escalated to the orchestrator;
// T010/T009(a) (which need Builders.hpp) are not attempted in this phase.
// This TU only needs the read-tier headers named in the task -- {Fields,
// Messages,Validator,Reify} -- which are v50sp2-comparable in size/cost.
//
// Anchors: specs/076-fix-latest-typed-codegen/tasks.md T008;
//          tests/session/test_069_us1_smoke.cpp (per-tier smoke precedent).

#include <gtest/gtest.h>

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
