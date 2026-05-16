// tests/alloc_guard/wire_alloc_guard_test.cpp
// 004-wire-codec T051 — FR-012 / SC-002 zero-heap-alloc guard for the wire
// hot path. Run under the mallocnesia interceptor via tools/check_alloc.py:
// ZERO global malloc/free between alloc_guard_start() and alloc_guard_end()
// over the full parse + serialize path (Framer-produced frame_view → Index
// parse + OffsetTable build → typed field reads → Iter parse → Writer
// append_raw → commit). All per-message storage is drawn from a
// stack-backed std::pmr::monotonic_buffer_resource with a NULL upstream, so
// any over-run is a hard, visible failure rather than a hidden heap hit —
// exactly the [2b §1.2] bounded-arena contract.
//
// Mirrors tests/alloc_guard/decimal_alloc_guard_test.cpp (seam #10).

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory_resource>
#include <span>
#include <string>
#include <vector>

#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/writer.hpp>

#include "support/frame_view_factory.hpp"
#include "support/mock_dict_table.hpp"  // concrete dict::table_view (seam #1)

using fixpp::wire::access_mode;
using fixpp::wire::MessageView;
using fixpp::wire::Parser;
using fixpp::wire::Writer;

// mallocnesia replaces these weak no-ops with its interceptor scope markers.
extern "C" {
__attribute__((weak)) void alloc_guard_start() {}
__attribute__((weak)) void alloc_guard_end() {}
}

namespace {

// Build a complete, CheckSum-correct FIX 4.4 frame: 8=…|9=N|<body>|10=CCC|.
// (Same construction as tests/wire/round_trip_property_test.cpp::make_frame.)
std::vector<std::byte> make_frame(std::string_view body_fields) {
    std::string body{body_fields};
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string pre = "8=FIX.4.4\x01" + nine + body;
    unsigned sum = 0;
    for (char c : pre) {
        sum += static_cast<unsigned char>(c);
    }
    char chk[16];
    std::snprintf(chk, sizeof(chk), "10=%03u\x01", sum % 256U);
    std::string full = pre + chk;
    std::vector<std::byte> out(full.size());
    for (std::size_t i = 0; i < full.size(); ++i) {
        out[i] = static_cast<std::byte>(full[i]);
    }
    return out;
}

constexpr bool is_auto_framing_tag(std::uint16_t tag) noexcept {
    return tag == 9U || tag == 10U;
}

}  // namespace

// The frame buffer + all stack arenas are constructed BEFORE the guard
// markers; only the parse/serialize hot path is measured.
TEST(WireAllocGuard, ParseSerializeLoopNoHeapAlloc) {
    auto const frame = make_frame(
        "35=D\x01" "49=SENDER\x01" "56=TARGET\x01" "34=42\x01"
        "11=ORD-1\x01" "55=AAPL\x01" "54=1\x01" "38=100\x01"
        "40=2\x01" "44=185.25\x01" "59=0\x01");

    auto fv = fixpp::wire::test::make_frame_view(frame);
    ASSERT_TRUE(fv.has_value());

    // Stack backing for the per-message arena and the Writer scratch/dst.
    // Sized far above one NewOrderSingle's OffsetTable footprint
    // (entry[]+overlay, see T049 spike: ~404 B for ~23 occ). NULL upstream
    // ⇒ an over-run cannot silently fall back to malloc.
    std::array<std::byte, 16384> parse_buf{};
    std::array<std::byte, 4096> write_scratch{};
    std::array<std::byte, 1024> dst_buf{};

    alloc_guard_start();
    for (int i = 0; i < 1000; ++i) {
        std::pmr::monotonic_buffer_resource parse_arena{
            parse_buf.data(), parse_buf.size(),
            std::pmr::null_memory_resource()};

        Parser<access_mode::Index> idx{fixpp::dict::table_view{}};
        auto mv = idx.parse(*fv, &parse_arena);
        ASSERT_TRUE(mv.has_value());

        // Typed random-access reads exercise the OffsetTable overlay.
        auto cl_ord_id = mv->get(11);
        ASSERT_TRUE(cl_ord_id.has_value());
        auto symbol = mv->template get<55>();
        ASSERT_TRUE(symbol.has_value());

        // Full serialize path: dict-free Iter walk → Writer → commit.
        std::pmr::monotonic_buffer_resource write_mr{
            write_scratch.data(), write_scratch.size(),
            std::pmr::null_memory_resource()};
        Parser<access_mode::Iter> itp{fixpp::dict::table_view{}};
        auto iv = itp.parse_iter(*fv);
        ASSERT_TRUE(iv.has_value());

        Writer writer{std::span<std::byte>{dst_buf.data(), dst_buf.size()},
                      &write_mr};
        for (auto it = iv->begin(); !(it == iv->end()); ++it) {
            auto const& f = *it;
            if (is_auto_framing_tag(f.tag)) {
                continue;  // Writer recomputes 9= and 10=.
            }
            auto rc = writer.append_raw(f.tag, f.value);
            ASSERT_TRUE(rc.has_value());
        }
        auto committed = std::move(writer).commit();
        ASSERT_TRUE(committed.has_value());
    }
    alloc_guard_end();
}
