// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/test_100_record_store_oom.cpp — 100-wire-group-read-integrity (T067 ruling 1;
// research D2 "Per-occurrence records", "Section records").
//
// `table_view_builder::add_occurrence_group` and `add_section_group` are public, and each stores a
// record without repeats unless its key is already held. They are written for the STRONG
// guarantee: the record is built before it is inserted, so a `std::bad_alloc` anywhere in the
// operation leaves the store as it was. This witness fails every allocation the operation makes,
// one at a time, on a fresh builder, and after each caught `std::bad_alloc` requires the built
// view's store to hold no key.
//
// Mechanism: a TU-local global `operator new` that throws on one armed call number, the seam
// tests/dictionary/table_view_pair_oom_test.cpp uses (`table_view`'s stores use the default
// allocator). Compiled out under ASan/TSan/MSan, which own the allocator. The sweep runs over the
// operation's own allocation count, counted on an unarmed call first, so a different STL only
// changes how many indices run.
//
// Mutation procedure: insert the key first and fill its record after (`try_emplace(key)`, then
// the members pushed into the stored record): a failure while the members are copied then leaves
// a key holding a partial record, which the `store(tv).empty()` check rejects.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fixpp/dict/table_view.hpp>
#include <new>
#include <string_view>
#include <utility>
#include <vector>

#include "../support/msvc_debug_arena_skip.hpp"
#include "../support/table_view_test_access.hpp"

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

using fixpp::dict::group_record;
using fixpp::dict::table_view;
using fixpp::dict::table_view_builder;
using fixpp::dict::table_view_test_access;

// A message type longer than any small-string buffer, so building the key allocates too.
constexpr std::string_view kMsgType = "MSGTYPE-LONGER-THAN-ANY-SSO-BUFFER";
constexpr std::array<std::uint16_t, 1> kPath{600};
constexpr std::uint16_t kNo = 700;

// Members with a repeat, enough of them that copying them grows the stored vector more than once.
group_record record_with_repeats() {
    return group_record{.delimiter = 701,
                        .members = {701, 702, 703, 702, 704, 705, 706, 707, 708, 709, 701}};
}

using store_of = table_view_test_access::record_store const& (*)(table_view const&) noexcept;

// Runs `op` on a fresh builder once per allocation index of `op`, failing that allocation, and
// checks the built view's store (read through `store`) after each caught `std::bad_alloc`.
template <class Op>
void sweep(char const* where, Op op, store_of store) {
    long budget = 0;
    {
        table_view_builder b;
        long const before = g_alloc_count;
        op(b);
        budget = g_alloc_count - before;
        auto const tv = std::move(b).build();
        auto const& s = store(tv);
        ASSERT_EQ(s.size(), 1U) << where << ": positive control, the record is stored";
        auto const& members = s.begin()->second.members;
        EXPECT_EQ(members,
                  (std::vector<std::uint16_t>{701, 702, 703, 704, 705, 706, 707, 708, 709}))
            << where << ": positive control, the members without repeats, in order";
    }
    ASSERT_GT(budget, 1) << where
                         << ": the operation made too few allocations for a failure to land "
                            "mid-copy, so this witness would pass vacuously";
    std::size_t threw = 0;
    for (long k = 1; k <= budget + 2; ++k) {
        table_view_builder b;
        g_fail_at = g_alloc_count + k;
        bool caught = false;
        try {
            op(b);
        } catch (std::bad_alloc const&) {
            caught = true;
        }
        g_fail_at = -1;  // disarm BEFORE the build and the assertions, which allocate
        if (caught) {
            ++threw;
            auto const tv = std::move(b).build();
            EXPECT_TRUE(store(tv).empty()) << where << ": allocation " << k << " of " << budget
                                           << " failed and the store kept a key (a partial record)";
        }
    }
    EXPECT_GT(threw, 0U) << where << ": no allocation was made to fail (arming is broken)";
}

TEST(RecordStoreOom100, OccurrenceRecordIsAllOrNothing) {
    FIXPP_SKIP_ON_MSVC_DEBUG_GLOBAL_NEW_SWEEP();
    group_record const rec = record_with_repeats();
    sweep(
        "add_occurrence_group",
        [&rec](table_view_builder& b) { b.add_occurrence_group(kMsgType, kPath, kNo, rec); },
        &table_view_test_access::occurrence_records);
}

TEST(RecordStoreOom100, SectionRecordIsAllOrNothing) {
    FIXPP_SKIP_ON_MSVC_DEBUG_GLOBAL_NEW_SWEEP();
    group_record const rec = record_with_repeats();
    sweep(
        "add_section_group",
        [&rec](table_view_builder& b) { b.add_section_group(kPath, kNo, rec); },
        &table_view_test_access::section_records);
}

}  // namespace

#else  // FIXPP_SANITIZER_REPLACES_NEW

TEST(RecordStoreOom100, SkippedUnderSanitizers) {
    GTEST_SKIP() << "the sanitizer owns the allocator; a global operator new override would "
                    "fight it (feedback_operator_new_witness_breaks_sanitizers)";
}

#endif  // !FIXPP_SANITIZER_REPLACES_NEW
