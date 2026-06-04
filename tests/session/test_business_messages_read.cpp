// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_business_messages_read.cpp
//
// 020-g2-business-messages T006 — typed read flyweight tests (AS4; FR-006).
//
// Tests the READ side: construct fixpp::v44::NewOrderSingle / ExecutionReport
// flyweights over a parsed MessageView<Index> and assert each accessor returns
// the expected typed value. Also asserts that a missing/ill-typed required field
// surfaces as the accessor's expected_t error.
//
// Frame assembly strategy: use a hand-written make_frame() helper (the same
// pattern as tests/dictionary/reify_test.cpp) that computes a valid BodyLength
// and CheckSum, then parse it via Framer + MessageView.
//
// Anchors: contracts/business-messages.md (Read contract); data-model.md E1/E2;
// research.md D2; tasks.md T006; fixpp::v44 generated Messages.hpp.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/v44/Messages.hpp>
#include <fixpp/wire/message_view_contract.hpp>
#include <fixpp/wire/parser.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using MV = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;
using fixpp::decimal_t;

// Build a well-formed FIX frame: "8=FIX.4.4\x01 9=<len>\x01 <body> 10=<chk>\x01"
// body must already begin with "35=X\x01" and contain SOH-delimited fields.
std::vector<std::byte> make_frame(std::string_view body) {
    // Compute the "body" for BodyLength purposes: 35=D\x01 ... (the body we pass)
    // BodyLength = everything between BodyLength and CheckSum, exclusive.
    // That is: everything from "35=" up to (but not including) "10=".
    std::string pre = "8=FIX.4.4\x01" + std::string("9=") + std::to_string(body.size()) + "\x01" +
                      std::string(body);
    unsigned sum = 0;
    for (unsigned char c : pre) {
        sum += c;
    }
    char checksum[16]{};
    std::snprintf(checksum, sizeof(checksum), "10=%03u\x01", sum % 256U);
    std::string full = pre + checksum;
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

// Parse a raw FIX frame into a MessageView using the production Framer.
MV parse_frame(std::vector<std::byte> const& buf, std::pmr::memory_resource* mr) {
    fixpp::wire::pmr_carry_buffer carry{buf.size(), mr};
    fixpp::wire::Framer fr{};
    fixpp::wire::frame_view fvs[1]{};
    auto framed = fr.feed(
        std::span<const std::byte>{buf.data(), buf.size()}, carry,
        std::span<fixpp::wire::frame_view>{fvs, 1});
    EXPECT_TRUE(framed.has_value()) << "Framer::feed failed";
    EXPECT_FALSE(framed->empty())   << "Framer produced no frames";
    return MV{(*framed)[0], mr};
}

// Helper: convert string_view to bytes for decimal parse
decimal_t parse_decimal(std::string_view sv, std::pmr::memory_resource* mr) {
    auto r = decimal_t::parse(
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(sv.data()), sv.size()}, mr);
    EXPECT_TRUE(r.has_value()) << "parse_decimal failed for: " << sv;
    return r.value_or(decimal_t{});
}

}  // namespace

// ── AS4a: NewOrderSingle flyweight accessors ────────────────────────────────────
// Parse a 35=D frame and verify each accessor returns the correct typed value.
TEST(BusinessMessagesRead, AS4a_NOS_AccessorsReturnSuppliedValues) {
    // Build body: field order matches the NOS wire body (35=D first, then fields)
    std::string body =
        "35=D\x01"
        "11=CLORD-READ-001\x01"
        "55=NVDA\x01"
        "54=1\x01"
        "38=250\x01"
        "40=2\x01"
        "44=430.75\x01"
        "60=20240601-14:30:00\x01";

    std::pmr::monotonic_buffer_resource arena{8192};
    auto buf = make_frame(body);
    auto mv  = parse_frame(buf, &arena);

    fixpp::v44::NewOrderSingle nos{mv};

    // String accessors
    auto cl_ord = nos.cl_ord_id();
    ASSERT_TRUE(cl_ord.has_value()) << "cl_ord_id() must have value";
    EXPECT_EQ(*cl_ord, "CLORD-READ-001");

    auto sym = nos.symbol();
    ASSERT_TRUE(sym.has_value()) << "symbol() must have value";
    EXPECT_EQ(*sym, "NVDA");

    auto tt = nos.transact_time();
    ASSERT_TRUE(tt.has_value()) << "transact_time() must have value";
    EXPECT_EQ(*tt, "20240601-14:30:00");

    // Char accessors
    auto sd = nos.side();
    ASSERT_TRUE(sd.has_value()) << "side() must have value";
    EXPECT_EQ(*sd, '1');

    auto ot = nos.ord_type();
    ASSERT_TRUE(ot.has_value()) << "ord_type() must have value";
    EXPECT_EQ(*ot, '2');  // Limit

    // Decimal accessors (with arena)
    auto order_qty = nos.order_qty(&arena);
    ASSERT_TRUE(order_qty.has_value()) << "order_qty() must have value";
    auto expected_qty = parse_decimal("250", &arena);
    EXPECT_EQ(*order_qty, expected_qty);

    auto price = nos.price(&arena);
    ASSERT_TRUE(price.has_value()) << "price() must have value";
    auto expected_px = parse_decimal("430.75", &arena);
    EXPECT_EQ(*price, expected_px);
}

// ── AS4b: ExecutionReport flyweight accessors ───────────────────────────────────
// Parse a 35=8 frame and verify each accessor returns the correct typed value.
TEST(BusinessMessagesRead, AS4b_ExecRpt_AccessorsReturnSuppliedValues) {
    std::string body =
        "35=8\x01"
        "37=XCH-ORD-999\x01"
        "17=EXEC-999\x01"
        "150=F\x01"
        "39=2\x01"
        "55=NVDA\x01"
        "54=1\x01"
        "151=0\x01"
        "14=250\x01"
        "6=430.75\x01";

    std::pmr::monotonic_buffer_resource arena{8192};
    auto buf = make_frame(body);
    auto mv  = parse_frame(buf, &arena);

    fixpp::v44::ExecutionReport er{mv};

    // String accessors
    auto oid = er.order_id();
    ASSERT_TRUE(oid.has_value()) << "order_id() must have value";
    EXPECT_EQ(*oid, "XCH-ORD-999");

    auto eid = er.exec_id();
    ASSERT_TRUE(eid.has_value()) << "exec_id() must have value";
    EXPECT_EQ(*eid, "EXEC-999");

    auto sym = er.symbol();
    ASSERT_TRUE(sym.has_value()) << "symbol() must have value";
    EXPECT_EQ(*sym, "NVDA");

    // Char accessors
    auto et = er.exec_type();
    ASSERT_TRUE(et.has_value()) << "exec_type() must have value";
    EXPECT_EQ(*et, 'F');

    auto os = er.ord_status();
    ASSERT_TRUE(os.has_value()) << "ord_status() must have value";
    EXPECT_EQ(*os, '2');

    auto sd = er.side();
    ASSERT_TRUE(sd.has_value()) << "side() must have value";
    EXPECT_EQ(*sd, '1');

    // Decimal accessors
    auto lq = er.leaves_qty(&arena);
    ASSERT_TRUE(lq.has_value()) << "leaves_qty() must have value";
    EXPECT_EQ(*lq, parse_decimal("0", &arena));

    auto cq = er.cum_qty(&arena);
    ASSERT_TRUE(cq.has_value()) << "cum_qty() must have value";
    EXPECT_EQ(*cq, parse_decimal("250", &arena));

    auto ap = er.avg_px(&arena);
    ASSERT_TRUE(ap.has_value()) << "avg_px() must have value";
    EXPECT_EQ(*ap, parse_decimal("430.75", &arena));
}

// ── AS4c: Missing required field surfaces as expected_t error ──────────────────
// A frame with no Symbol(55) field: nos.symbol() must return an error, not crash.
TEST(BusinessMessagesRead, AS4c_MissingRequiredField_ReturnsError) {
    // NOS body with symbol (55) deliberately absent
    std::string body =
        "35=D\x01"
        "11=CLORD-MISSING\x01"
        // 55 omitted intentionally
        "54=1\x01"
        "38=10\x01"
        "40=2\x01"
        "44=100\x01"
        "60=20240601-10:00:00\x01";

    std::pmr::monotonic_buffer_resource arena{8192};
    auto buf = make_frame(body);
    auto mv  = parse_frame(buf, &arena);

    fixpp::v44::NewOrderSingle nos{mv};

    // Present fields still work
    auto cl = nos.cl_ord_id();
    ASSERT_TRUE(cl.has_value());
    EXPECT_EQ(*cl, "CLORD-MISSING");

    // Missing field returns an error
    auto sym = nos.symbol();
    EXPECT_FALSE(sym.has_value()) << "symbol() must return error when field is absent";
    EXPECT_EQ(sym.error(), fixpp::core::error::wire_required_field_missing);
}

// ── AS4d: NOS decimal field value-equality across trailing-zero forms ───────────
// Wire carries "190.50"; accessor must return a decimal_t that equals "190.5".
TEST(BusinessMessagesRead, AS4d_NOS_DecimalTrailingZeroEquality) {
    std::string body =
        "35=D\x01"
        "11=CLORD-TZ\x01"
        "55=AMD\x01"
        "54=2\x01"
        "38=5\x01"
        "40=2\x01"
        "44=190.50\x01"
        "60=20240601-10:00:00\x01";

    std::pmr::monotonic_buffer_resource arena{8192};
    auto buf = make_frame(body);
    auto mv  = parse_frame(buf, &arena);

    fixpp::v44::NewOrderSingle nos{mv};

    auto price = nos.price(&arena);
    ASSERT_TRUE(price.has_value());

    auto d_1905 = parse_decimal("190.5", &arena);
    EXPECT_EQ(*price, d_1905) << "190.50 wire form must equal 190.5 by decimal_t value";
}
