// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/fuzz/fuzz_wire_parser.cpp
//
// T050 — Seam #11 — libFuzzer harness for Parser + OffsetTable.
//
// Feeds arbitrary bytes as a "frame" to Parser<Index>::parse (the path that
// builds the OffsetTable + hash overlay) and asserts the invariants:
//   - No crash/OOB (ASan catches any out-of-bounds read/write).
//   - No UB (UBSan catches signed overflow, misalignment, null deref, etc.).
//   - Bounded memory: OffsetTable rejects > default_max_offset_entries
//     occurrences with wire_offset_table_full (no unbounded growth).
//   - Any rejection is a defined wire_* error — never silent UB.
//   - No exception escapes the noexcept parse() boundary.
//
// The harness wraps arbitrary bytes in a minimal frame_view structure using
// the test factory (friend of frame_view). If the bytes are not valid FIX
// frames, the factory itself returns a wire_* error and parse() is skipped;
// both paths are exercised by the fuzzer.
//
// Validator harness: fuzz_wire_validator.cpp wired by 041-validation-gate-wiring
// (T026) — the feature-004 "US4 PAUSED" deferral is ended.
//
// Campaign note (T050): A full ≥10-min Tier-1 ASan+UBSan campaign is the
// CI/T055 responsibility. The in-PR campaign was run for 120 s under
// -fsanitize=fuzzer,address,undefined; see .specify/decisions/004-wire-codec-
// verify.md §T050 for actual runtime + iteration counts.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fixpp/wire/framer.hpp>
#include <fixpp/wire/offset_table.hpp>
#include <fixpp/wire/parser.hpp>
#include <memory_resource>
#include <span>

// Concrete dict::table_view definition (seam #1 — test double).
// The production table_view is forward-declared only in parser.hpp; the
// fuzzer harness includes the mock to instantiate Parser<Index>.
#include "support/mock_dict_table.hpp"
// Test-only factory — friend of frame_view.
#include "support/frame_view_factory.hpp"
// W1: the parse resource is the allocation counter for the unknown-field properties below.
#include "support/pmr_allocation_tracking_resource.hpp"
// detail::set_overlay_seed_for_testing (W-P3-2) — declaration lives here, not
// in the installed public offset_table.hpp.
#include "support/wire_test_hooks.hpp"

// Pin the overlay hash seed to a fixed value so crashes reproduce across runs
// (the production seed is randomised per process — W-P3-2). Runtime hook, not a
// build macro: a macro would compile a DIFFERENT offset_table.o than production.
extern "C" int LLVMFuzzerInitialize(int* /*argc*/, char*** /*argv*/) {
    fixpp::wire::detail::set_overlay_seed_for_testing(0xF1A9C0DEU);
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    using fixpp::wire::access_mode;
    using fixpp::wire::frame_view;
    using fixpp::wire::OffsetTable;
    using fixpp::wire::Parser;

    // Attempt to interpret the fuzzer input as a FIX frame by running it
    // through the test factory.  The factory locates "9=" and "10=" boundaries;
    // if absent, it returns a wire_* error and we skip the parse (still
    // exercising the factory's error paths).
    auto buf = std::span<const std::byte>{reinterpret_cast<const std::byte*>(data), size};
    auto fv_or_err = fixpp::wire::test::make_frame_view(buf);

    if (!fv_or_err) {
        // Malformed input from the factory's perspective — the parse path
        // is validly skipped; the factory's error handling is itself exercised.
        return 0;
    }

    frame_view const& fv = *fv_or_err;

    // Parse with Index mode (builds OffsetTable + hash overlay).
    // The per-message arena is stack-backed to keep heap usage bounded.
    std::array<std::byte, 256 * 1024> arena_buf{};
    std::pmr::monotonic_buffer_resource arena{arena_buf.data(), arena_buf.size(),
                                              std::pmr::null_memory_resource()};

    // Seed a NON-EMPTY grammar so group_member_fn returns true for real tags:
    // this drives the OffsetTable group sub-index (group_slices / field_start
    // pointer arithmetic) under adversarial offsets — the wire layer's most
    // intricate pointer math, previously never fuzzed because the harness built
    // Parser from an EMPTY table_view (wire-hostile-input-review T-1).
    // The grammar is input-independent, so it is built ONCE rather than per
    // iteration. This binds it by `table_view const&` and never casts away the
    // constness of a span accessor's pointer — the only write the type does not
    // prevent — which is what makes the `static` trivially safe: no iteration
    // leaves state for the next.
    static fixpp::dict::table_view const tv = [] {
        fixpp::dict::table_view_builder tvb;
        tvb.set_group_first(453, 448).add_group_member(453, 447);  // NoPartyIDs group
        // W1 (`.specify/688-707-559-wire-headroom-token.md` §1.6 item 7): a few common tags,
        // which add_valid_tag marks defined, so both sides of the unknown-field predicate fire
        // on ordinary frames, not only on a frame that carries a group tag.
        tvb.add_valid_tag("D", 34).add_valid_tag("D", 35).add_valid_tag("D", 49).add_valid_tag("D",
                                                                                               55);
        return std::move(tvb).build();
    }();
    Parser<access_mode::Index> p{tv};
    // W1: every allocation the parse resource serves is counted, the arena behind it unchanged.
    fixpp::test_support::pmr_allocation_tracking_resource counted{&arena};

    // parse() is noexcept; any exception escape -> std::terminate -> libFuzzer
    // crash report.
    auto result = p.parse(fv, &counted);

    if (result) {
        auto const& mv = *result;

        // Touch the OffsetTable to exercise the find() / entries() paths.
        auto const& tbl = mv.offsets();
        (void)tbl.size();
        (void)tbl.entries();

        // Look up a few tags to exercise the hash overlay lookup.
        (void)tbl.find(35);    // MsgType
        (void)tbl.find(34);    // MsgSeqNum
        (void)tbl.find(55);    // Symbol
        (void)tbl.find(0);     // invalid / out-of-range
        (void)tbl.find(9999);  // user-defined range

        // Touch the build_status() path.
        (void)tbl.build_status();

        // Exercise the group sub-index (materialization + field_start / slice
        // pointer arithmetic) now that the grammar makes 453 a real group.
        (void)tbl.group(453);
        (void)tbl.group_slices(453);

        // Touch msg_type() / msg_seq_num().
        (void)mv.msg_type();
        (void)mv.msg_seq_num();

        // W1 (`.specify/688-707-559-wire-headroom-token.md` §1.6 item 7): unknown_fields().
        // The counter check runs first, with its own message, so a failure names the counter.
        // The properties' reference is the harness table's own tag_defined, called directly
        // here: the check is that the view's yield agrees with that predicate entry by entry,
        // not that the predicate is right.
        std::size_t const before = counted.allocate_calls();
        auto const uf = mv.unknown_fields();
        for (auto const& kv : uf) (void)kv;
        if (counted.allocate_calls() != before) {
            std::fprintf(stderr, "unknown_fields() allocated from the parse resource\n");
            std::abort();
        }
        auto const* const base = mv.bytes().data();
        auto it = uf.begin();
        for (auto const& e : tbl.entries()) {
            bool const framing = e.tag == 8U || e.tag == 9U || e.tag == 10U;
            if (framing || tv.tag_defined(e.tag)) continue;
            if (it == uf.end()) {
                std::fprintf(stderr, "unknown_fields() missed an undefined entry\n");
                std::abort();
            }
            auto const kv = *it;
            if (kv.tag != e.tag || kv.data != base + e.offset || kv.len != e.length) {
                std::fprintf(stderr, "unknown_fields() yielded another entry, or out of order\n");
                std::abort();
            }
            ++it;
        }
        if (!(it == uf.end())) {
            std::fprintf(stderr, "unknown_fields() yielded a defined or framing tag\n");
            std::abort();
        }
    }
    // else: a wire_* error from parse() — correct rejection of malformed input.

    return 0;
}
