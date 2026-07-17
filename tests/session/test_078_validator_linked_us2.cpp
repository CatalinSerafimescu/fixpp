// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_078_validator_linked_us2.cpp
//
// 078-precompiled-builder-libs T025 [US2] [TESTS-FIRST]: validator-linked
// behavior (FR-003/FR-004 AC2). A binary linking `fixpp::validators::v44`
// calls `validate_NewOrderSingle` on an Args missing a required field --
// asserts it reports the missing required field (RESULT-identity, not wire
// bytes), consistent with the 077 typed validator
// (test_067_builder_validate.cpp's ClOrdID(11) case).
//
// This is the US2 counterpart to T024: T024 proves the send-only consumer
// that does NOT link the validator lib carries zero validator code; T025
// proves the consumer that DOES want the outbound check links
// fixpp::validators::v44 and gets a real, correct result -- not a stub. The
// TU includes ONLY the slim per-message header (not all.hpp), demonstrating
// the "link the one lib you need" US2 shape.
//
// Foundational (T002-T019) already landed the real emitter split + the
// always-built fixpp::validators::v44 target, so this witness is BORN-GREEN
// (the property it asserts already holds) --
// feedback_fail_placeholder_red_test: no fake red-first cycle staged here.
// Discrimination: the missing-field case must actually return an ERROR (not
// silently OK) -- a positive control with the same shape but the field
// present validates clean, proving the failure is specifically the omitted
// field, not a broken validator.
//
// Anchors: specs/078-precompiled-builder-libs/spec.md US2 (lines 64-67),
// FR-003/FR-004 AC2; quickstart.md Scenario 2;
// tests/session/test_067_builder_validate.cpp (expected error shape --
// fixpp::core::error::wire_required_field_missing).

#include <gtest/gtest.h>

#include <cstddef>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/v44/messages/NewOrderSingle.hpp>  // slim decl header -- NOT all.hpp
#include <memory_resource>
#include <span>
#include <string_view>

namespace {

using fixpp::decimal_t;

// Mirrors test_067_builder_validate.cpp's local make_decimal -- deliberately
// NOT the tests/support/app_message_read_scaffold.hpp helper, to keep this
// TU's link surface minimal (fixpp_wire + fixpp::validators::v44 only, no
// fixpp_dictionary), matching the 067 precedent's link-dependency shape.
decimal_t make_decimal(std::string_view sv, std::pmr::memory_resource* mr) {
    auto bytes =
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(sv.data()), sv.size()};
    auto r = decimal_t::parse(bytes, mr);
    EXPECT_TRUE(r.has_value()) << "make_decimal failed for: " << sv;
    return r.value_or(decimal_t{});
}

fixpp::v44::NewOrderSingleArgs make_valid_args(std::pmr::memory_resource* mr) {
    fixpp::v44::NewOrderSingleArgs args{};
    args.cl_ord_id = "US2-VALID-1";
    args.symbol = "US2SYM";
    args.side = '1';
    args.order_qty = make_decimal("10", mr);
    args.ord_type = '2';
    args.transact_time = "20240101-10:00:00";
    return args;
}

}  // namespace

TEST(ValidatorLinkedUS2, NewOrderSingle_MissingClOrdID_ReportsRequiredFieldMissing) {
    std::pmr::monotonic_buffer_resource arena{4096};
    auto args = make_valid_args(&arena);
    args.cl_ord_id = std::nullopt;  // the discriminating missing field

    auto r = fixpp::v44::validate_NewOrderSingle(args);
    ASSERT_FALSE(r.has_value())
        << "missing top-level required ClOrdID(11) must fail validate_ via the linked "
           "fixpp::validators::v44 lib";
    EXPECT_EQ(r.error(), fixpp::core::error::wire_required_field_missing);

    // Positive control: same shape, ClOrdID present -> validates clean --
    // proves the rejection above is specifically the ClOrdID omission, not a
    // broken/always-failing validator.
    auto valid_args = make_valid_args(&arena);
    auto r_ok = fixpp::v44::validate_NewOrderSingle(valid_args);
    EXPECT_TRUE(r_ok.has_value())
        << "with ClOrdID present the identical shape must validate clean";
}
