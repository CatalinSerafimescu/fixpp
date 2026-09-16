// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/table_view_pair_oom_test.cpp — fixpp#426, Gate B r7 N-3.
//
// `table_view`'s Length+Data pair state is THREE members that must agree:
// `length_pair_data_tag_`, its inverse `data_pair_length_tag_`, and the
// `has_nonstandard_pair_` flag that `wire::dict_hooks::for_table_view` reads to
// decide whether to install the pair callback at all. A pair present in the maps
// while the flag reads false is invisible to every scanner built afterwards, and a
// pair present in one map but not the other breaks the "two directions never
// disagree" invariant (Gate B r1 G-4).
//
// Both mutating paths — `set_length_pair_data_tag` and copy-assignment — are
// written for the STRONG guarantee: prepare, then commit with operations the
// compiler proves `noexcept`. This witness is what makes that claim falsifiable:
// it fails EVERY allocation the operation performs, one at a time, and after each
// caught `std::bad_alloc` requires the observable pair state to be byte-for-byte
// what it was before the call.
//
// Mechanism: a TU-local global `operator new` that throws on one armed call
// number, the same seam `reify_membership_copy_oom_test.cpp` and
// `capi/dict066_clone_membership_copy_oom_test.cpp` use — `table_view`'s tables
// use the DEFAULT allocator, so a pmr harness cannot intercept them. Compiled out
// under ASan/TSan/MSan, which own the allocator
// (feedback_operator_new_witness_breaks_sanitizers).
//
// Unlike the reify witness this needs NO calibrated ordinal and therefore no
// libstdc++ gate: it sweeps every index from 1 to the operation's own allocation
// count, so a different STL merely changes how many iterations run. An index that
// does not throw is not a failure — the assertion is about the state after the
// ones that do.
//
// Mutation procedure: delete the try/catch rollback in `set_length_pair_data_tag`
// so both maps are assigned directly — the insertion and re-pair cases then observe
// a pair that landed in one direction only. Re-default `operator=(table_view
// const&)` — the copy-assignment cases then observe a target whose maps and flag
// disagree. (Both mutants are anchored on code, not on a count of which cases go
// RED: that count moves with the allocation pattern of the STL underneath.)

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <fixpp/dict/table_view.hpp>
#include <new>
#include <tuple>
#include <vector>

// ── Sanitizer gate (mirrors reify_membership_copy_oom_test.cpp) ──────────────
#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || \
    __has_feature(memory_sanitizer)
#define FIXPP_SANITIZER_REPLACES_NEW 1
#endif
#endif
#if !defined(FIXPP_SANITIZER_REPLACES_NEW) && \
    (defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__))
#define FIXPP_SANITIZER_REPLACES_NEW 1
#endif
#ifndef FIXPP_SANITIZER_REPLACES_NEW
#define FIXPP_SANITIZER_REPLACES_NEW 0
#endif

#if !FIXPP_SANITIZER_REPLACES_NEW

namespace {
long g_alloc_count = 0;
long g_fail_at = -1;  // -1 = never fail
}  // namespace

void* operator new(std::size_t size) {
    if (++g_alloc_count == g_fail_at) {
        throw std::bad_alloc{};
    }
    void* p = std::malloc(size);
    if (p == nullptr) {
        throw std::bad_alloc{};
    }
    return p;
}
void* operator new[](std::size_t size) {
    if (++g_alloc_count == g_fail_at) {
        throw std::bad_alloc{};
    }
    void* p = std::malloc(size);
    if (p == nullptr) {
        throw std::bad_alloc{};
    }
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {

using fixpp::dict::table_view;

// Every tag the cases below touch, so a snapshot covers both directions for all
// of them rather than only the pair under test.
constexpr std::array<std::uint16_t, 8> kTags{5001, 5002, 5003, 5011, 5012, 6001, 6002, 95};

// The observable pair state: the flag plus both lookup directions for every tag.
struct pair_state {
    bool flag = false;
    std::vector<std::uint16_t> forward;
    std::vector<std::uint16_t> inverse;

    friend bool operator==(pair_state const&, pair_state const&) = default;
};

pair_state snapshot(table_view const& tv) {
    pair_state s;
    s.flag = tv.has_nonstandard_pair();
    for (auto const tag : kTags) {
        s.forward.push_back(tv.length_pair_data_tag(tag));
        s.inverse.push_back(tv.data_pair_length_tag(tag));
    }
    return s;
}

// The G-4 invariant, checked directly: every forward entry has its inverse, and
// every inverse entry has its forward.
void expect_directions_agree(table_view const& tv, char const* where) {
    for (auto const tag : kTags) {
        if (auto const data = tv.length_pair_data_tag(tag); data != 0) {
            EXPECT_EQ(tv.data_pair_length_tag(data), tag)
                << where << ": forward " << tag << "->" << data << " has no inverse";
        }
        if (auto const length = tv.data_pair_length_tag(tag); length != 0) {
            EXPECT_EQ(tv.length_pair_data_tag(length), tag)
                << where << ": inverse " << tag << "->" << length << " has no forward";
        }
    }
}

// Runs `op` on a freshly built subject once per allocation index, failing that
// allocation. `build` must produce the same subject every time.
template <class Build, class Op>
void sweep_allocation_failures(char const* where, Build build, Op op) {
    // How many allocations does the operation itself perform? Measured, not guessed:
    // a different STL simply changes the sweep length.
    long budget = 0;
    {
        auto subject = build();
        long const before = g_alloc_count;
        op(subject);
        budget = g_alloc_count - before;
    }
    ASSERT_GT(budget, 0) << where
                         << ": the operation allocated nothing, so this witness "
                            "cannot fail an allocation — it would pass vacuously";

    std::size_t threw = 0;
    for (long k = 1; k <= budget + 2; ++k) {
        auto subject = build();
        auto const before = snapshot(subject);

        g_fail_at = g_alloc_count + k;
        bool caught = false;
        try {
            op(subject);
        } catch (std::bad_alloc const&) {
            caught = true;
        }
        g_fail_at = -1;  // disarm BEFORE the assertions below, which allocate

        if (caught) {
            ++threw;
            EXPECT_EQ(snapshot(subject), before)
                << where << ": allocation " << k << " failed and left the pair state changed";
            expect_directions_agree(subject, where);
        }
    }
    EXPECT_GT(threw, 0U) << where
                         << ": no allocation was ever made to fail — the sweep proves "
                            "nothing (arming is broken)";
}

table_view with_one_pair() {
    table_view tv;
    tv.set_length_pair_data_tag(5001, 5002);
    return tv;
}

TEST(TableViewPairOom, NewPairInsertionIsAllOrNothing) {
    sweep_allocation_failures(
        "new pair", [] { return with_one_pair(); },
        [](table_view& tv) { tv.set_length_pair_data_tag(6001, 6002); });
}

TEST(TableViewPairOom, RepairingTheLengthSideIsAllOrNothing) {
    sweep_allocation_failures(
        "re-pair length", [] { return with_one_pair(); },
        [](table_view& tv) { tv.set_length_pair_data_tag(5001, 5003); });
}

TEST(TableViewPairOom, RepairingTheDataSideIsAllOrNothing) {
    sweep_allocation_failures(
        "re-pair data", [] { return with_one_pair(); },
        [](table_view& tv) { tv.set_length_pair_data_tag(5011, 5002); });
}

// The flag lives beside the maps, so a half-applied copy could leave a target
// holding pairs while `has_nonstandard_pair()` still reads false — pairs no bundle
// built from that target would ever honour.
TEST(TableViewPairOom, CopyAssignmentIsAllOrNothing) {
    table_view source;
    source.set_length_pair_data_tag(5001, 5002);
    source.set_length_pair_data_tag(5011, 5012);

    sweep_allocation_failures(
        "copy assignment", [] { return table_view{}; }, [&source](table_view& tv) { tv = source; });
}

// A target that already holds pairs must not lose them to a failed assignment.
TEST(TableViewPairOom, CopyAssignmentOverAPopulatedTargetIsAllOrNothing) {
    table_view source;
    source.set_length_pair_data_tag(6001, 6002);

    sweep_allocation_failures(
        "copy assignment over populated", [] { return with_one_pair(); },
        [&source](table_view& tv) { tv = source; });
}

}  // namespace

#else  // FIXPP_SANITIZER_REPLACES_NEW

TEST(TableViewPairOom, SkippedUnderSanitizers) {
    GTEST_SKIP() << "the sanitizer owns the allocator; a global operator new override would "
                    "fight it (feedback_operator_new_witness_breaks_sanitizers)";
}

#endif  // !FIXPP_SANITIZER_REPLACES_NEW
