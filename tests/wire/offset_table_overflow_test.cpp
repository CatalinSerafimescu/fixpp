// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/wire/offset_table_overflow_test.cpp
//
// 040-inbound-tag-overflow-hardening Phase 4 US2 — T008/T012 (Index site).
//
// Witnesses that OffsetTable::build rejects forged wrap-and-continue tag tokens
// (fixed by site 1 — the in-loop accumulate_tag_digit helper). Pre-fix the
// post-loop `if (tag > 0xFFFFU)` check was absent (applied AFTER the digit loop),
// so a token like 429496729649 wraps uint32 to 49 and slips through as tag 49.
//
// Cells:
//   W1: token 429496729649 (wraps to 49 without fix) — status = wire_tag_out_of_range;
//       find(49) fails; forged value NOT queryable under tag 49.
//   W2: token 429496729634 (wraps to 34 without fix) — status = wire_tag_out_of_range;
//       find(34) fails; forged value NOT queryable under tag 34.
//   W3: conforming frame with valid tags (incl. 65535) — GREEN, no regression.
//
// Canonical vectors (research.md D-3 / D-6, data-model.md SC-001):
//   429496729649 → without bound: wraps to 49 (aliases SenderCompID)
//   429496729634 → without bound: wraps to 34 (aliases MsgSeqNum)
//
// [[feedback_witness_asserts_named_postcondition_not_proxy]]:
//   Each forged-field cell names the exact error code + asserts find(tag) fails +
//   pairs with a conforming-frame cell proving the same tag IS found on legit input.
//
// Anchors: research.md D-1/D-3/D-6; data-model.md SC-001; tasks.md T008/T012.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fixpp/core/error.hpp>
#include <fixpp/wire/offset_table.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <vector>

#include "support/frame_view_factory.hpp"

namespace {

using fixpp::core::error;
using fixpp::wire::OffsetTable;

// Build a minimal structurally-framed buffer whose body is exactly `body`.
// OffsetTable scans bytes; checksum correctness is not required — only the
// 9= and \x0110= structural markers matter for the factory.
std::vector<std::byte> make_raw_frame(std::string const& body) {
    std::string nine  = "9=" + std::to_string(body.size()) + "\x01";
    std::string full  = "8=FIX.4.4\x01" + nine + body + "10=000\x01";
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

// ── W1: token 429496729649 must NOT alias SenderCompID(49) ──────────────────
// Pre-fix: no in-loop bound → uint32 wraps from 429496729*10+9 → 4294967290,
//   then *10+4 = 42949672904 mod 2^32 = 42949672904 & 0xFFFFFFFF → 2652085768,
//   ... the exact wrap yields 49 as the final uint32 value → field stored as tag 49.
//   Actually, digit-by-digit: 4→2→9→4→9→6→7→2→9→6→4→9 accumulate:
//   wrap occurs when the pre-fix code does `(429496729 * 10 + 6)` which overflows.
//   The in-loop helper stops BEFORE that multiply, returning false for the '6' digit.
// Post-fix: helper fires on the overflowing digit → status = wire_tag_out_of_range.
TEST(OffsetTableOverflow, ForgedTag429496729649_RejectedNotSurfacedAsTag49) {
    // Forged field whose token wraps to tag 49 under the pre-fix accumulator.
    auto buf = make_raw_frame("429496729649=FORGE49\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    OffsetTable t{*fv, &arena};

    // The table must be RED with wire_tag_out_of_range.
    auto s = t.build_status();
    ASSERT_FALSE(s.has_value())
        << "Forged token 429496729649 must make OffsetTable RED";
    EXPECT_EQ(s.error(), error::wire_tag_out_of_range)
        << "Site 1 (T008): in-loop helper must fire wire_tag_out_of_range (SC-004)";

    // Table must be cleared — entries() / size() empty.
    EXPECT_EQ(t.size(), 0U) << "Table must be cleared on overflow rejection";

    // The forged field must NOT be queryable under tag 49.
    auto found49 = t.find(49);
    ASSERT_FALSE(found49.has_value())
        << "Forged value must NOT be queryable under tag 49 (SC-001)";

    // Conforming pair: a real 49= field in a clean frame must be found.
    auto cbuf = make_raw_frame("35=A\x01""49=REAL\x01""34=1\x01");
    auto cfv = fixpp::wire::test::make_frame_view(cbuf);
    ASSERT_TRUE(cfv.has_value());
    std::pmr::monotonic_buffer_resource carena;
    OffsetTable ct{*cfv, &carena};
    ASSERT_TRUE(ct.build_status().has_value()) << "Conforming frame must build OK";
    auto cf49 = ct.find(49);
    ASSERT_TRUE(cf49.has_value()) << "Conforming tag 49 must be found";
    auto const* raw = reinterpret_cast<char const*>(cbuf.data()) + cf49->offset;
    EXPECT_EQ(std::string_view(raw, cf49->length), "REAL")
        << "Conforming tag 49 must yield value REAL";
}

// ── W2: token 429496729634 must NOT alias MsgSeqNum(34) ─────────────────────
TEST(OffsetTableOverflow, ForgedTag429496729634_RejectedNotSurfacedAsTag34) {
    auto buf = make_raw_frame("429496729634=42\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    OffsetTable t{*fv, &arena};

    auto s = t.build_status();
    ASSERT_FALSE(s.has_value())
        << "Forged token 429496729634 must make OffsetTable RED";
    EXPECT_EQ(s.error(), error::wire_tag_out_of_range)
        << "Site 1 (T008): in-loop helper must fire wire_tag_out_of_range (SC-004)";

    EXPECT_EQ(t.size(), 0U) << "Table must be cleared on overflow rejection";

    auto found34 = t.find(34);
    ASSERT_FALSE(found34.has_value())
        << "Forged value must NOT be queryable under tag 34 (SC-001)";

    // Conforming pair: real 34= must be found.
    auto cbuf = make_raw_frame("35=A\x01""34=7\x01""49=SENDER\x01");
    auto cfv = fixpp::wire::test::make_frame_view(cbuf);
    ASSERT_TRUE(cfv.has_value());
    std::pmr::monotonic_buffer_resource carena;
    OffsetTable ct{*cfv, &carena};
    ASSERT_TRUE(ct.build_status().has_value()) << "Conforming frame must build OK";
    auto cf34 = ct.find(34);
    ASSERT_TRUE(cf34.has_value()) << "Conforming tag 34 must be found";
    auto const* raw = reinterpret_cast<char const*>(cbuf.data()) + cf34->offset;
    EXPECT_EQ(std::string_view(raw, cf34->length), "7")
        << "Conforming tag 34 must yield value 7";
}

// ── W3: conforming frame — no regression ────────────────────────────────────
// A well-formed frame with ordinary tags (including 65535 — maximum valid FIX tag)
// must parse cleanly (SC-003: no regression on conforming-path input).
TEST(OffsetTableOverflow, ConformingFrame_BuildsClean) {
    // 65535 is the maximum valid FIX tag; must be accepted per helper's static_assert.
    auto buf = make_raw_frame(
        "35=A\x01"
        "34=1\x01"
        "49=SENDER\x01"
        "65535=MAXVAL\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    OffsetTable t{*fv, &arena};

    ASSERT_TRUE(t.build_status().has_value())
        << "Conforming frame (incl. tag 65535) must build successfully";

    auto f35 = t.find(35);
    ASSERT_TRUE(f35.has_value()) << "tag 35 must be found in conforming frame";
    auto const* raw35 = reinterpret_cast<char const*>(buf.data()) + f35->offset;
    EXPECT_EQ(std::string_view(raw35, f35->length), "A");

    auto f49 = t.find(49);
    ASSERT_TRUE(f49.has_value()) << "tag 49 must be found in conforming frame";
    auto const* raw49 = reinterpret_cast<char const*>(buf.data()) + f49->offset;
    EXPECT_EQ(std::string_view(raw49, f49->length), "SENDER");

    // tag 65535 must also be found.
    auto f65535 = t.find(65535);
    ASSERT_TRUE(f65535.has_value()) << "tag 65535 (max valid) must be found";
    auto const* raw65535 = reinterpret_cast<char const*>(buf.data()) + f65535->offset;
    EXPECT_EQ(std::string_view(raw65535, f65535->length), "MAXVAL");
}

}  // namespace
