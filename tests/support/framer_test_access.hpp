#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/framer_test_access.hpp — TEST-ONLY access to Framer's counted work
// (093-inbound-frame-dispositions; quickstart.md §2, contract C-1 "The bound").
//
// fixpp#511 (B21): `Framer` declares `friend struct framer_test_access;`
// unconditionally. Define this struct only in this header; a differing definition of
// this struct elsewhere in the program is an ODR violation (ill-formed, no diagnostic
// required). Add new accessors here, never as members of `Framer` gated behind a test
// macro. Never installed; production targets must not include this header. Needs no
// test macro.

#include <cstddef>
#include <cstdint>
#include <fixpp/wire/framer.hpp>

namespace fixpp::wire {

struct framer_test_access {
    struct work_counts {
        std::uint64_t read = 0;
        std::uint64_t summed = 0;
        std::uint64_t moved = 0;
    };

    // The resync path's counted work since construction: bytes read, summed into a
    // CheckSum, and moved (appended into or compacted within the carry).
    [[nodiscard]] static work_counts work(Framer const& f) noexcept {
        return {.read = f.work_read_, .summed = f.work_summed_, .moved = f.work_moved_};
    }

    [[nodiscard]] static std::uint64_t total_work(Framer const& f) noexcept {
        return f.work_read_ + f.work_summed_ + f.work_moved_;
    }

    // The constant K of contract C-1's bound: after N received bytes, a resync-mode
    // Framer whose carry holds L plus R bytes has done at most
    //     K * N * (1 + L/R + max_begin_string_bytes + Framer::kBodyLengthDigitCap)
    // units of counted work.
    // Precondition: max_begin_string_bytes is no smaller than the Config default
    // (every Framer the session builds). within_work_bound() reports a Framer that
    // breaks it as out of bound, so a cell or fuzz input using one fails rather than
    // skipping the check.
    // Recipe (tasks.md T017a): charge one unit per byte read, summed or moved, and
    // bound each term from the clause that limits it: the appends; W-1's compactions
    // (each moves at most L bytes, and more than a read's worth of bytes is appended
    // across two consecutive ones); the search's reads and the per-feed header rescan
    // under the two caps (W-2); the CheckSum sums (W-3). Their sum has a constant
    // term, which the precondition lets the caps term absorb; that fold gives K. The
    // derivation, step by step, is in research R-2.
    static constexpr std::uint64_t kWorkBoundConstant = 6;

    // The bound's precondition, above.
    [[nodiscard]] static bool work_bound_precondition_holds(Framer const& f) noexcept {
        return f.cfg_.max_begin_string_bytes >= Framer::Config{}.max_begin_string_bytes;
    }

    // True while the counted work is within the bound for `received` bytes, at limit
    // L and read size R (the carry's capacity minus L), and false whenever the
    // precondition fails. Integer form: multiplied through by R.
    [[nodiscard]] static bool within_work_bound(Framer const& f, std::uint64_t received,
                                                std::uint64_t limit,
                                                std::uint64_t read_size) noexcept {
        if (!work_bound_precondition_holds(f)) {
            return false;
        }
        std::uint64_t const caps =
            static_cast<std::uint64_t>(f.cfg_.max_begin_string_bytes) +
            static_cast<std::uint64_t>(Framer::kBodyLengthDigitCap);
        std::uint64_t const per_byte_times_r = read_size + limit + (read_size * caps);
        return total_work(f) * read_size <= kWorkBoundConstant * received * per_byte_times_r;
    }
};

}  // namespace fixpp::wire
