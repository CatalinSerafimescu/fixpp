// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/offset_table_error_path_test.cpp — T055 coverage hardening.
// Targeted error-path tests for fixpp::wire::OffsetTable covering uncovered
// branches in src/wire/offset_table.cpp:
//   - lines 134-137: ctor bad_alloc degrade (catch block)
//   - lines 142-143: find() on RED table returns status error
//   - lines 165-166: group() on RED table returns status error
//   - lines 231-232: group_slices() bad_alloc degrade (catch block, empty span)
//
// Lines NOT covered here (documented as unreachable/waived):
//   - 125-127 (kMaxBuildProbe DoS bound — adversarial-only, waived)
//   - 157-158 (find() probe-cap break — unreachable under load-factor < 1, waived)
//   - 183-184 (group() err_group_too_large — provably unreachable: max table
//              entries = 4096, so avail ≤ 4095 < default_max_group_entries_per_instance)

#include <gtest/gtest.h>
#include "../support/msvc_debug_arena_skip.hpp"

#include <cstddef>
#include <cstring>
#include <fixpp/core/error.hpp>
#include <fixpp/wire/offset_table.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <vector>

#include "support/failing_pmr_resource.hpp"
#include "support/frame_view_factory.hpp"

namespace {

using fixpp::core::error;
using fixpp::test_support::failing_pmr_resource;
using fixpp::wire::OffsetTable;

// Build a raw FIX frame buffer over `body`. The frame_view_factory only needs
// the structural markers (9=, 10=); checksum correctness is not required.
std::vector<std::byte> make_raw_frame(std::string const& body) {
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string full = "8=FIX.4.4\x01" + nine + body + "10=000\x01";
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

// ── ctor bad_alloc degrade (lines 134-137) ───────────────────────────────────
// When the PMR resource throws bad_alloc during the first push_back into
// entries_, the catch block degrades the table: entries_.clear(),
// overlay_.clear(), status_ = fail(error::out_of_memory).
// After degrade:
//   - build_status() returns error::out_of_memory
//   - find() returns the status error (not wire_required_field_missing)
//   - size() == 0 (empty)
//
// fail_on_call_n=1: the very first PMR allocation (entries_ push_back for the
// first field) throws → catch fires → lines 134-137 covered.

TEST(OffsetTableErrorPath, CtorBadAllocDegradeCoversLines134to137) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    auto buf = make_raw_frame("35=D\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource upstream;
    failing_pmr_resource fail_mr{&upstream, /*fail_on_call_n=*/1};

    OffsetTable t{*fv, &fail_mr};

    // build_status must report out_of_memory.
    auto s = t.build_status();
    ASSERT_FALSE(s.has_value()) << "OOM on first alloc must degrade to out_of_memory";
    EXPECT_EQ(s.error(), error::out_of_memory)
        << "lines 134-137: catch(bad_alloc) must set status_ = out_of_memory";

    // Table must be empty.
    EXPECT_EQ(t.size(), 0U) << "OOM-degraded table must be empty";

    // find() must return the status error (out_of_memory), not field-absent.
    auto found = t.find(35);
    ASSERT_FALSE(found.has_value());
    EXPECT_EQ(found.error(), error::out_of_memory)
        << "find() on OOM-degraded table must propagate out_of_memory";
}

// ── find() on RED table (lines 142-143) ──────────────────────────────────────
// A RED table has status_ set to a non-ok error (e.g. wire_invalid_field_format
// for a malformed frame). find() checks `!status_` first and returns the status
// error — lines 141-143 in offset_table.cpp.
//
// "nofieldsep\x01" has no '=' separator → wire_invalid_field_format.

TEST(OffsetTableErrorPath, FindOnRedTableReturnsStatusErrorCoversLines142to143) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    auto buf = make_raw_frame("nofieldsep\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    OffsetTable t{*fv, &arena};

    // Confirm the table is RED.
    auto s = t.build_status();
    ASSERT_FALSE(s.has_value()) << "malformed frame must produce a RED table";
    EXPECT_EQ(s.error(), error::wire_invalid_field_format);

    // find() must propagate the RED status, not wire_required_field_missing.
    auto found = t.find(35);
    ASSERT_FALSE(found.has_value());
    EXPECT_EQ(found.error(), error::wire_invalid_field_format)
        << "lines 142-143: find() on RED table must return fail<entry>(status_.error())";
}

// ── group() on RED table (lines 165-166) ─────────────────────────────────────
// group() has the same RED-guard as find(): checks `!status_` first.
// Lines 164-166 in offset_table.cpp.

TEST(OffsetTableErrorPath, GroupOnRedTableReturnsStatusErrorCoversLines165to166) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    auto buf = make_raw_frame("nofieldsep\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    OffsetTable t{*fv, &arena};

    ASSERT_FALSE(t.build_status().has_value()) << "table must be RED";

    auto g = t.group(453);
    ASSERT_FALSE(g.has_value());
    EXPECT_EQ(g.error(), error::wire_invalid_field_format)
        << "lines 165-166: group() on RED table must return fail<group_index>(status_.error())";
}

// ── group_slices() bad_alloc degrade (lines 231-232) ─────────────────────────
// group_slices() is noexcept and catches bad_alloc internally, returning an
// empty span on failure (lines 230-232).
//
// Strategy: build an OffsetTable successfully with a failing_pmr_resource that
// only fails on the Nth call, where N > the number of allocations needed for
// construction. Then the group_slices() call triggers allocation N → throws →
// catch fires → empty span returned.
//
// The frame body "453=1\x01" "448=A\x01" produces 5 total fields when wrapped
// in the standard frame envelope (8=, 9=, 453=, 448=, 10=). Measured allocation
// counts during construction (with monotonic upstream):
//   Call 1: entries_ push_back (capacity 0→1,  12 bytes)
//   Call 2: entries_ push_back (capacity 1→2,  24 bytes, old freed to monotonic)
//   Call 3: entries_ push_back (capacity 2→4,  48 bytes)
//   Call 4: entries_ push_back (capacity 4→8,  96 bytes)
//   Call 5: overlay_.assign(cap=8, 0)          (32 bytes)
// Total: 5 allocations during construction.
//
// Setting fail_on_call_n=6 allows construction to complete (5 allocs succeed),
// then the first group_slices(453) call triggers:
//   Call 6: group_slices_.reserve(5) → bad_alloc → catch(bad_alloc) → return {}
// The reserve call fails before group_slices_reserved_ is set to true, so
// the catch block is the ONLY exit from the try-block at that point (lines 231-232).
// The failing_pmr_resource only fails on the exact Nth call, so subsequent calls
// succeed — a second group_slices(453) invocation will rebuild from scratch and
// succeed normally (verifies the noexcept catch is not a permanent degradation).

TEST(OffsetTableErrorPath, GroupSlicesBadAllocDegradeCoversLines231to232) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    auto buf = make_raw_frame(
        "453=1\x01"
        "448=A\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource upstream;
    // fail_on_call_n=6: allow 5 construction allocs (verified by measurement),
    // fail the first group_slices alloc (group_slices_.reserve).
    failing_pmr_resource fail_mr{&upstream, /*fail_on_call_n=*/6};

    OffsetTable t{*fv, &fail_mr};

    // Construction must succeed (5 allocs complete before call #6).
    ASSERT_TRUE(t.build_status().has_value())
        << "table construction must succeed with the first 5 allocations allowed";
    EXPECT_GT(t.size(), 0U) << "table must have entries after successful build";

    // group_slices(453) triggers alloc #6 (reserve) → bad_alloc → catch → {}.
    auto slices = t.group_slices(453);
    EXPECT_TRUE(slices.empty())
        << "lines 231-232: group_slices() must return empty span on bad_alloc";

    // Verify noexcept guarantee: second call must not crash (alloc #6 already
    // consumed; subsequent allocs succeed → group_slices rebuilds normally).
    auto slices2 = t.group_slices(453);
    // slices2 may be non-empty on the second call (the failing resource only
    // fires once); the invariant is just that the call completes without throwing.
    SUCCEED() << "second group_slices() after OOM must not crash";
    (void)slices2;
}

}  // namespace
