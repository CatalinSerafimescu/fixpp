// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/test_078_builder_mixing_us3.cpp
//
// 078-precompiled-builder-libs T026 [US3] [TESTS-FIRST]: builder-side mixing
// witness (FR-007 headline / New-4). This TU force-inlines ONE message's
// build_ body (NewOrderSingle, via FIXPP_BUILDERS_HEADER_ONLY_NewOrderSingle
// defined BEFORE the slim header is included) while linking
// fixpp::builders::v44 for everything else -- exercised here by calling
// build_ExecutionReport, which resolves from the linked archive.
//
// Foundational (T002-T019) already landed the real emitter split + the
// per-message FIXPP_BUILDERS_HEADER_ONLY_<Msg> macro surface, so this
// witness is BORN-GREEN (the property it asserts already holds) --
// feedback_fail_placeholder_red_test: no fake red-first cycle staged here.
// Real-lib counterpart to the T002 R2a mock probe, at per-message
// granularity (T002 mocked only a whole-TU-vs-whole-lib split, not the
// FIXPP_BUILDERS_HEADER_ONLY_<Msg> per-message macro path).
//
// Three discrimination legs, wired in tests/codegen/CMakeLists.txt:
//   (a) NO DUPLICATE-SYMBOL at link: fixpp_builders_v44.a's own
//       NewOrderSingle.builder.o defines a STRONG, non-weak
//       fixpp::v44::build_NewOrderSingle -- while this TU's object defines
//       the SAME mangled symbol WEAKLY (linkonce_odr, via the `inline`
//       keyword the header-only path adds). Per ELF/COFF static-archive
//       linking, the compiled TU's weak definition satisfies every
//       reference to build_NewOrderSingle in the final link BEFORE the
//       linker ever needs to pull NewOrderSingle.builder.o out of the
//       archive to resolve an undefined symbol -- so that archive member is
//       silently never linked in, and no duplicate-definition error can
//       occur. This IS the real FR-007 "no duplicate-symbol" property; a
//       naive non-inline per-message emission (missing the `inline`
//       keyword on the .builder.inl body) would instead produce a hard
//       link error the moment both this TU and fixpp_builders_v44.a defined
//       the same STRONG symbol -- the test IS the link succeeding, mirroring
//       test_078_odr_sc003_probe.cpp's legs (i)/(iii).
//   (b) Compiled-OBJECT nm check (test_078_builder_mixing_us3_object_nm_
//       check.cmake), inspecting the OBJECT before it is folded into the
//       final binary: fixpp::v44::build_NewOrderSingle must be DEFINED
//       locally (the force-inlined body really was emitted into this TU,
//       not just declared) while fixpp::v44::build_ExecutionReport must be
//       UNDEFINED in that same object (its body was never pulled in here --
//       it resolves only at final link, from the archive). This is the
//       positive proof that the MIXING actually happened as designed, not
//       merely that the final binary happened to link.
//   (c) Byte-identity (US3 AC2 / SC-004, builder inline mode): the
//       force-inlined NewOrderSingle build below is driven by the EXACT
//       same literal Args as tests/session/test_078_builder_roundtrip_
//       linked_us1.cpp's `BuilderRoundtripLinkedUS1V44.NewOrderSingle` cell
//       (cl_ord_id="078-NOS-CLORDID", symbol="078NOS", side='1',
//       order_qty=10.5, price=42.75) -- that test proved the identical
//       inputs, reached PURELY via the linked fixpp::builders::v44 archive
//       (no macro defined there), produce those exact dict-readback field
//       values. build_<Msg> never calls validate_<Msg> (validation is off
//       the serialize path -- test_067_builder_validate.cpp), so build_
//       output depends only on the Args supplied, not on which translation
//       unit/mode compiled the body; the generated body_builder call
//       sequence is byte-for-byte the SAME code (the emitter's .builder.inl
//       and .builder.cpp differ only in the `inline` keyword -- see
//       tools/codegen/fixpp-codegen/emit_builders.cpp). Reproducing the
//       SAME field-for-field dict-readback outcome here, via the
//       force-inlined body, over the SAME literal inputs, is therefore
//       byte-identical-output evidence for the inline leg of FR-009/SC-004,
//       auditable by diffing the two literal-input blocks directly.
//       ExecutionReport below similarly reuses T021's exact literals for
//       its LINKED leg (already proven byte-correct by T021; this TU proves
//       it ALSO resolves correctly when co-present with a force-inlined
//       sibling message, i.e. under real mixing, not in isolation).
//
// Anchors: specs/078-precompiled-builder-libs/spec.md US3 (lines 80-92),
// FR-006/FR-007, SC-004; quickstart.md Scenario 4b;
// tests/codegen/test_078_odr_sc003_probe.cpp (mock ODR precedent);
// tests/session/test_078_builder_roundtrip_linked_us1.cpp (literal-input
// oracle, linked-mode NewOrderSingle/ExecutionReport cells).

#define FIXPP_BUILDERS_HEADER_ONLY_NewOrderSingle
#include <fixpp/v44/messages/NewOrderSingle.hpp>  // force-inlined build_ body
#undef FIXPP_BUILDERS_HEADER_ONLY_NewOrderSingle

#include <fixpp/v44/messages/ExecutionReport.hpp>  // declaration only -- resolves from fixpp::builders::v44

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <vector>

#include "support/app_message_read_scaffold.hpp"

namespace {

using fixpp_test_support::bytes_to_string;
using fixpp_test_support::expect_decimal;
using fixpp_test_support::expect_text;
using fixpp_test_support::make_decimal;
using fixpp_test_support::make_frame;
using fixpp_test_support::parse_dict;
using IndexView = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;

}  // namespace

// Force-inlined leg: build_NewOrderSingle's body was emitted directly into
// THIS TU (FIXPP_BUILDERS_HEADER_ONLY_NewOrderSingle above) -- co-present in
// the same binary as fixpp::builders::v44, which is linked for the rest of
// v44 (ExecutionReport below). Same literal inputs as T021's linked-mode
// NewOrderSingle cell (see file-header note (c)).
TEST(BuilderMixingUS3, ForceInlinedNewOrderSingle_ByteIdenticalToLinkedModeInputs) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v44::NewOrderSingleArgs args{};
    args.cl_ord_id = "078-NOS-CLORDID";
    args.symbol = "078NOS";
    args.side = '1';
    args.order_qty = make_decimal("10.5", &arena);
    args.price = make_decimal("42.75", &arena);

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_NewOrderSingle(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "force-inlined build_NewOrderSingle failed";
    std::string const body = bytes_to_string(*built);

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> const frame = make_frame("FIX.4.4", body);
    auto const mv = parse_dict(frame, tv, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of force-inlined NewOrderSingle frame failed (see ADD_FAILURE above)";

    expect_text(mv, 11, "078-NOS-CLORDID", "cl_ord_id");
    expect_text(mv, 55, "078NOS", "symbol");
    expect_text(mv, 54, "1", "side");
    expect_decimal(mv, 38, "10.5", &read_arena, "order_qty");
    expect_decimal(mv, 44, "42.75", &read_arena, "price");
}

// Linked leg: build_ExecutionReport resolves from fixpp::builders::v44 in
// THIS binary, alongside the force-inlined NewOrderSingle above -- proving
// "every other build_<Msg> resolves from the lib" under real mixing (US3
// AC1). Same literal inputs as T021's linked-mode ExecutionReport cell.
TEST(BuilderMixingUS3, LinkedExecutionReportResolvesUnderMixing) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v44::ExecutionReportArgs args{};
    args.avg_px = make_decimal("101.25", &arena);
    args.cl_ord_id = "078-ER-CLORDID";
    args.exec_id = "078-EXECID";
    args.order_id = "078-ORDERID";
    args.ord_status = '0';
    args.side = '2';
    args.symbol = "078ER";
    args.leaves_qty = make_decimal("5.0", &arena);

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_ExecutionReport(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "linked build_ExecutionReport failed";
    std::string const body = bytes_to_string(*built);

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> const frame = make_frame("FIX.4.4", body);
    auto const mv = parse_dict(frame, tv, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of linked ExecutionReport frame failed (see ADD_FAILURE above)";

    expect_decimal(mv, 6, "101.25", &read_arena, "avg_px");
    expect_text(mv, 11, "078-ER-CLORDID", "cl_ord_id");
    expect_text(mv, 17, "078-EXECID", "exec_id");
    expect_text(mv, 37, "078-ORDERID", "order_id");
    expect_text(mv, 39, "0", "ord_status");
    expect_text(mv, 54, "2", "side");
    expect_text(mv, 55, "078ER", "symbol");
    expect_decimal(mv, 151, "5.0", &read_arena, "leaves_qty");
}
