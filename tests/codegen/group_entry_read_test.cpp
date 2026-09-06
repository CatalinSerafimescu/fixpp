// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/group_entry_read_test.cpp — 062 T009 [US1]
//
// US1 AC1/AC2 / FR-001 / FR-005 / SC-001: typed scalar+decimal reads on a
// GENERATED repeating-group entry flyweight (NewOrderList `orders()` / G_73,
// tag 73/NoOrders — a one-level grouped message distinct from the nested
// MassQuote case reserved for T015/US2), over a REAL parsed frame. Every
// value asserted below is a value THIS test controls in the wire bytes
// (discriminating, not a tautology); entry[0] and entry[1] deliberately hold
// DIFFERENT values so a per-instance bug (e.g. always reading entry 0) fails.
//
// Frame assembly follows the established make_frame()/parse_frame() pattern
// (tests/session/test_business_messages_read.cpp) — dict-free Parser<Index>,
// same as production session.cpp:316/1869 (Parser<Index> pd_parser{}).

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/v44/Messages.hpp>
#include <fixpp/wire/message_view_contract.hpp>
#include <fixpp/wire/parser.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/typed_group_table_views.hpp"

namespace {

using MV = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;
using fixpp::decimal_t;

// Build a well-formed FIX frame: "8=FIX.4.4\x01 9=<len>\x01 <body> 10=<chk>\x01"
// body must already begin with "35=X\x01" and contain SOH-delimited fields.
std::vector<std::byte> make_frame(std::string_view body) {
    std::string pre =
        "8=FIX.4.4\x01" + std::string("9=") + std::to_string(body.size()) + "\x01" +
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
//
// 220: DICT-AWARE, and it has to be. This helper used to build the view
// dict-free, justified as "same as production session.cpp Parser<Index>
// pd_parser{}" — a comment that was already stale, because 066 dict-backed
// `Session::parse_and_dispatch_` (session.cpp threads `{*inbound_tv_}`). Since
// fixpp#220, group identification is a dictionary-only operation, so a
// dict-free parse yields NO typed group at all and every cell below that reads
// `nol.orders()` would see an empty group.
//
// This file already knew the dict-free extent rule was wrong:
// EmptyGroupSizeZeroNoDeref below went dict-aware precisely because the
// dict-free fallback "misclassifies the checksum tag itself as a single
// phantom NoOrders member". That is the same defect #220 removed; these cells
// simply never had a frame that exposed it.
MV parse_frame(std::vector<std::byte> const& buf, std::pmr::memory_resource* mr) {
    fixpp::wire::pmr_carry_buffer carry{buf.size(), mr};
    fixpp::wire::Framer fr{};
    fixpp::wire::frame_view fvs[1]{};
    auto framed = fr.feed(
        std::span<const std::byte>{buf.data(), buf.size()}, carry,
        std::span<fixpp::wire::frame_view>{fvs, 1});
    EXPECT_TRUE(framed.has_value()) << "Framer::feed failed";
    EXPECT_FALSE(framed->empty()) << "Framer produced no frames";
    fixpp::wire::Parser<fixpp::wire::access_mode::Index> parser{
        fixpp_test_support::group73_table_view()};
    auto mv = parser.parse((*framed)[0], mr);
    if (!mv.has_value()) {
        // MessageView is move-only, so no value_or here; fail loudly and hand
        // back a well-formed object so the caller's own assertions still run.
        ADD_FAILURE() << "dict-aware parse failed";
        return MV{(*framed)[0], mr};
    }
    return std::move(*mv);
}

decimal_t parse_decimal(std::string_view sv, std::pmr::memory_resource* mr) {
    auto r = decimal_t::parse(
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(sv.data()), sv.size()}, mr);
    EXPECT_TRUE(r.has_value()) << "parse_decimal failed for: " << sv;
    return r.value_or(decimal_t{});
}

}  // namespace

// US1 AC1 / FR-001 / FR-005 / SC-001 — two orders() entries with DISTINCT
// per-instance values for every field asserted (string ClOrdID/OrderID, char
// Side, decimal OrderQty via order_qty(mr)); a bug that always reads entry 0
// (or that reads the wrong entry's span) fails this.
TEST(GroupEntryRead, OneLevelScalarAndDecimalReadExactValues) {
    std::string body =
        "35=E\x01"
        "73=2\x01"
        "11=CLORD-0\x01"
        "37=ORDID-0\x01"
        "38=100.5\x01"
        "54=1\x01"
        "11=CLORD-1\x01"
        "37=ORDID-1\x01"
        "38=200.25\x01"
        "54=2\x01";

    std::pmr::monotonic_buffer_resource arena{8192};
    auto buf = make_frame(body);
    auto mv = parse_frame(buf, &arena);

    fixpp::v44::NewOrderList nol{mv};
    auto orders = nol.orders();
    ASSERT_EQ(orders.size(), 2U);

    struct expected {
        std::string_view cl_ord_id;
        std::string_view order_id;
        char side;
        std::string_view qty;
    };
    expected const exp[2] = {
        {"CLORD-0", "ORDID-0", '1', "100.5"},
        {"CLORD-1", "ORDID-1", '2', "200.25"},
    };

    for (std::size_t i = 0; i < 2; ++i) {
        auto entry = orders[i];

        auto cl = entry.cl_ord_id();
        ASSERT_TRUE(cl.has_value()) << "entry[" << i << "].cl_ord_id() must have value";
        EXPECT_EQ(*cl, exp[i].cl_ord_id) << "entry[" << i << "]";

        auto oid = entry.order_id();
        ASSERT_TRUE(oid.has_value()) << "entry[" << i << "].order_id() must have value";
        EXPECT_EQ(*oid, exp[i].order_id) << "entry[" << i << "]";

        auto sd = entry.side();
        ASSERT_TRUE(sd.has_value()) << "entry[" << i << "].side() must have value";
        EXPECT_EQ(*sd, exp[i].side) << "entry[" << i << "]";

        auto qty = entry.order_qty(&arena);
        ASSERT_TRUE(qty.has_value()) << "entry[" << i << "].order_qty() must have value";
        EXPECT_EQ(*qty, parse_decimal(exp[i].qty, &arena)) << "entry[" << i << "]";
    }
}

// US1 AC2 / FR-001 — the ABSENT arm: entry[1] omits OrderID(37) entirely;
// its order_id() accessor must return the typed not-found error (not a
// defaulted/empty value). entry[0] (which DOES carry 37) is asserted present
// for contrast, so the absent-arm assertion is not vacuously true of every
// entry.
TEST(GroupEntryRead, AbsentEntryFieldReturnsTypedError) {
    std::string body =
        "35=E\x01"
        "73=2\x01"
        "11=CLORD-0\x01"
        "37=ORDID-0\x01"
        "38=100.5\x01"
        "54=1\x01"
        "11=CLORD-1\x01"
        "38=200.25\x01"
        "54=2\x01";

    std::pmr::monotonic_buffer_resource arena{8192};
    auto buf = make_frame(body);
    auto mv = parse_frame(buf, &arena);

    fixpp::v44::NewOrderList nol{mv};
    auto orders = nol.orders();
    ASSERT_EQ(orders.size(), 2U);

    auto entry0 = orders[0];
    auto oid0 = entry0.order_id();
    ASSERT_TRUE(oid0.has_value()) << "entry[0] DOES carry OrderID — contrast case";
    EXPECT_EQ(*oid0, "ORDID-0");

    auto entry1 = orders[1];
    auto oid1 = entry1.order_id();
    ASSERT_FALSE(oid1.has_value()) << "entry[1] omits OrderID(37) — must be typed-absent";
    EXPECT_EQ(oid1.error(), fixpp::core::error::wire_required_field_missing);
}

// Edge: absent vs present-but-empty. entry[0] carries "37=<SOH>" (present,
// zero-length value) — order_id() must SUCCEED with an empty string_view.
// entry[1] omits tag 37 entirely — order_id() must FAIL with the typed
// not-found error. The two dispositions must differ.
TEST(GroupEntryRead, AbsentVsPresentButEmptyField) {
    std::string body =
        "35=E\x01"
        "73=2\x01"
        "11=CLORD-0\x01"
        "37=\x01"
        "54=1\x01"
        "11=CLORD-1\x01"
        "54=2\x01";

    std::pmr::monotonic_buffer_resource arena{8192};
    auto buf = make_frame(body);
    auto mv = parse_frame(buf, &arena);

    fixpp::v44::NewOrderList nol{mv};
    auto orders = nol.orders();
    ASSERT_EQ(orders.size(), 2U);

    auto entry0 = orders[0];
    auto present_empty = entry0.order_id();
    ASSERT_TRUE(present_empty.has_value())
        << "entry[0] carries 37 present-but-empty — must SUCCEED";
    EXPECT_EQ(*present_empty, "");

    auto entry1 = orders[1];
    auto absent = entry1.order_id();
    ASSERT_FALSE(absent.has_value()) << "entry[1] omits 37 entirely — must FAIL";
    EXPECT_EQ(absent.error(), fixpp::core::error::wire_required_field_missing);
}

// Edge: NoOrders=0 — orders().size()==0, begin()==end(), and a range-for
// over the (empty) group never dereferences an entry.
//
// NOTE: this witness parses DICT-AWARE (real v44 XML dictionary via
// XmlLoader + Dictionary::as_table_view()), unlike the other four cases in
// this file. `73=0` is immediately followed only by the frame's trailing
// checksum field (10=NNN — always present, an OffsetTable::build() invariant
// for any whole-frame scan); under a dict-FREE Parser, OffsetTable::group()'s
// fallback (`group_end = entries_.size()`, offset_table.cpp:440-443) cannot
// distinguish "genuinely empty group" from "count field followed by exactly
// one more field", so it misclassifies the checksum tag itself as a single
// phantom NoOrders member. The dict-aware path validates that the field
// immediately after the count (the delimiter candidate) is an actual member
// of group 73 (offset_table.cpp:401-411) — 10 (CheckSum) never is — so it
// correctly reports an empty group. This is a pre-existing wire-layer
// dict-free-group-boundary property, unrelated to and out of scope for 062
// (062 does not touch OffsetTable::group()/group_slices()).
TEST(GroupEntryRead, EmptyGroupSizeZeroNoDeref) {
    std::string body =
        "35=E\x01"
        "73=0\x01";

    std::pmr::monotonic_buffer_resource arena{8192};
    fixpp::dict::XmlLoader loader;
    auto dict = loader.load(std::string(FIXPP_DICT_DATA_DIR) + "/FIX44.xml", &arena);
    auto tv = dict.as_table_view();

    auto buf = make_frame(body);
    fixpp::wire::pmr_carry_buffer carry{buf.size(), &arena};
    fixpp::wire::Framer fr{};
    fixpp::wire::frame_view fvs[1]{};
    auto framed = fr.feed(
        std::span<const std::byte>{buf.data(), buf.size()}, carry,
        std::span<fixpp::wire::frame_view>{fvs, 1});
    ASSERT_TRUE(framed.has_value());
    ASSERT_FALSE(framed->empty());

    fixpp::wire::Parser<fixpp::wire::access_mode::Index> parser{tv};
    auto mv_exp = parser.parse((*framed)[0], &arena);
    ASSERT_TRUE(mv_exp.has_value());

    fixpp::v44::NewOrderList nol{*mv_exp};
    auto orders = nol.orders();
    EXPECT_EQ(orders.size(), 0U);
    EXPECT_TRUE(orders.begin() == orders.end());

    int dereferenced = 0;
    for (auto const& e : orders) {
        (void)e;
        ++dereferenced;
    }
    EXPECT_EQ(dereferenced, 0) << "empty group must never dereference an entry";
}

// Edge (spec.md:71): single-entry group — the entry is simultaneously the
// FIRST and the LAST occurrence, so its extent has no following delimiter to
// bound it (relies on end-of-frame accounting). Assert every field, INCLUDING
// the entry's LAST wire field (side, tag 54), reads its exact value: a
// truncated extent would corrupt/empty the tail field; an off-by-one on the
// string fields would produce a mismatched (not just shorter) value.
TEST(GroupEntryRead, LastEntryDelimiterExtentExact) {
    std::string body =
        "35=E\x01"
        "73=1\x01"
        "11=ONLY-ENTRY\x01"
        "37=ORDID-TAIL\x01"
        "54=9\x01";

    std::pmr::monotonic_buffer_resource arena{8192};
    auto buf = make_frame(body);
    auto mv = parse_frame(buf, &arena);

    fixpp::v44::NewOrderList nol{mv};
    auto orders = nol.orders();
    ASSERT_EQ(orders.size(), 1U);

    auto entry = orders[0];
    auto cl = entry.cl_ord_id();
    ASSERT_TRUE(cl.has_value());
    EXPECT_EQ(*cl, "ONLY-ENTRY");

    auto oid = entry.order_id();
    ASSERT_TRUE(oid.has_value());
    EXPECT_EQ(*oid, "ORDID-TAIL");

    // The entry's LAST wire field: a truncated/overrun extent would make this
    // fail the exact single-byte-char decode.
    auto sd = entry.side();
    ASSERT_TRUE(sd.has_value());
    EXPECT_EQ(*sd, '9');
}
