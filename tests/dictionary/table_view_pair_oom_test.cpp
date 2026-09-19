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
// `set_length_pair_data_tag` is written for the STRONG guarantee: prepare, then
// commit with operations the compiler proves `noexcept`. This witness is what makes
// that claim falsifiable: it fails EVERY allocation the operation performs, one at a
// time, and after each caught `std::bad_alloc` requires the observable pair state to
// be byte-for-byte what it was before the call.
//
// fixpp#456 re-grounded these cases on `table_view_builder`: the mutator is private
// on the view now, so the subject of the sweep is the builder. THE THREE MUTATION
// CASES BELOW ARE THE ONLY REMAINING COVERAGE OF THAT ROLLBACK. The invariant was
// enforced twice — once here on the mutation path, once on the copy-assignment path
// that #456 deleted — so losing these alongside the assignment cases would silently
// drop the half of the guarantee the seal claims to preserve. The two
// copy-assignment cases are gone with their subject; that is a deletion, not a
// coverage gap that was overlooked.
//
// The builder forwards exactly three `const` readbacks — `length_pair_data_tag`,
// `data_pair_length_tag`, `has_nonstandard_pair` — which is precisely the state this
// witness observes. There is deliberately no `peek()` returning the view under
// construction (design §5d item 4).
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
// a pair that landed in one direction only. (The mutant is anchored on code, not on
// a count of which cases go RED: that count moves with the allocation pattern of the
// STL underneath.)

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <fixpp/dict/table_view.hpp>
#include <new>
#include <tuple>
#include <vector>

// fixpp#426 (Gate B r11): MSVC's debug STL draws a hidden _Container_proxy per
// std::pmr container through GLOBAL operator new, so the sweep below fails one of
// THOSE instead of the operation's own allocation and terminates inside
// member-initialisation. See the macro's own note for why this is a runtime skip
// rather than an STL-keyed compile-out.
#include "../support/msvc_debug_arena_skip.hpp"

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

using fixpp::dict::table_view_builder;

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

pair_state snapshot(table_view_builder const& tv) {
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
void expect_directions_agree(table_view_builder const& tv, char const* where) {
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

table_view_builder with_one_pair() {
    table_view_builder b;
    b.set_length_pair_data_tag(5001, 5002);
    return b;
}

TEST(TableViewPairOom, NewPairInsertionIsAllOrNothing) {
    FIXPP_SKIP_ON_MSVC_DEBUG_GLOBAL_NEW_SWEEP();
    sweep_allocation_failures(
        "new pair", [] { return with_one_pair(); },
        [](table_view_builder& tv) { tv.set_length_pair_data_tag(6001, 6002); });
}

TEST(TableViewPairOom, RepairingTheLengthSideIsAllOrNothing) {
    FIXPP_SKIP_ON_MSVC_DEBUG_GLOBAL_NEW_SWEEP();
    sweep_allocation_failures(
        "re-pair length", [] { return with_one_pair(); },
        [](table_view_builder& tv) { tv.set_length_pair_data_tag(5001, 5003); });
}

TEST(TableViewPairOom, RepairingTheDataSideIsAllOrNothing) {
    FIXPP_SKIP_ON_MSVC_DEBUG_GLOBAL_NEW_SWEEP();
    sweep_allocation_failures(
        "re-pair data", [] { return with_one_pair(); },
        [](table_view_builder& tv) { tv.set_length_pair_data_tag(5011, 5002); });
}

}  // namespace

#else  // FIXPP_SANITIZER_REPLACES_NEW

TEST(TableViewPairOom, SkippedUnderSanitizers) {
    GTEST_SKIP() << "the sanitizer owns the allocator; a global operator new override would "
                    "fight it (feedback_operator_new_witness_breaks_sanitizers)";
}

#endif  // !FIXPP_SANITIZER_REPLACES_NEW
