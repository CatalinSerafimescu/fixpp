// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/test_102_run_readers.cpp — 102-dict-width-multichar-type, spec FR-008
// (research D4 "readers", D7; tasks T018).
//
// Each cell builds a `detail::dict_metadata_handle` by hand (its stores are public members
// of an internal class; this binary has `src/dictionary` on its include path), gives one
// reader a record whose 32-bit start is near `UINT32_MAX` with a small count over a short
// table, and asserts the reader returns an empty span. Each asserts `.empty()` only and reads
// no element: a reader whose bounds arithmetic wraps returns a span that points past its
// table, and reading it would be an out-of-bounds read, not the named failure.
//
// The condition a cell witnesses: the reader's check cannot be passed by a start and a count
// whose 32-bit sum wraps. A reader that sums in `std::uint32_t` and compares with the table's
// size passes such a record; one that sums in `std::size_t`, or tests
// `start <= size && count <= size - start`, does not.
//
// The readers covered here are those whose record already holds a 32-bit start (a
// `MsgFieldsRun` or an `EnumRun`). Re-derive the set: research D7's two recipes and tasks
// `## Census manifest`'s D7 sum supplement over `src/dictionary`. `for_each_group_ctx_record`
// has no cell of its own: it reads through `msg_group_ctx_delim_run`, the guard
// `msg_group_ctx_delims_impl`'s cell witnesses, and under a wrapped guard it would read past
// its pool. `run_at` already sums in `std::size_t`; its cell is a control.

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <memory_resource>
#include <string_view>
#include <utility>

#include "dictionary_internal.hpp"

namespace {

using fixpp::dict::GroupRef;
using fixpp::dict::MessageEntry;
using fixpp::dict::detail::dict_metadata_handle;
using fixpp::dict::detail::EnumRun;
using fixpp::dict::detail::MsgFieldsRun;

// The table each cell's record reaches past.
constexpr std::size_t kTableSize = 4;
// A start whose 32-bit sum with `kCount` wraps to a value inside the table.
constexpr std::uint32_t kStart = std::numeric_limits<std::uint32_t>::max() - 1U;
constexpr std::uint32_t kCount = 3;

static_assert(static_cast<std::uint32_t>(kStart + kCount) < kTableSize,
              "the fixture's start and count must wrap to a sum inside the table");

constexpr std::string_view kMsgType = "A";

// One message, so `find_msg_index` finds row 0 of every per-message run table.
void one_message(dict_metadata_handle& h) {
    h.messages_.push_back(MessageEntry{.msg_type = kMsgType, .name = "Msg"});
}

std::pmr::memory_resource* mr() { return std::pmr::new_delete_resource(); }

}  // namespace

TEST(RunReaders102, MessageFieldsOverrunIsEmpty) {
    dict_metadata_handle h{mr()};
    one_message(h);
    h.fields_.resize(kTableSize);
    h.per_msg_field_offsets_.push_back(MsgFieldsRun{.start = kStart, .count = kCount});
    EXPECT_TRUE(h.message_fields_impl(kMsgType).empty());
}

TEST(RunReaders102, GroupRequiredMembersOverrunIsEmpty) {
    dict_metadata_handle h{mr()};
    GroupRef g{};
    g.no_tag = 100;
    h.groups_.push_back(g);
    h.group_required_pool_.resize(kTableSize);
    h.group_required_offsets_.push_back(MsgFieldsRun{.start = kStart, .count = kCount});
    EXPECT_TRUE(h.group_required_members_impl(100).empty());
}

TEST(RunReaders102, MsgGroupRequiredPairsOverrunIsEmpty) {
    dict_metadata_handle h{mr()};
    one_message(h);
    h.msg_group_required_pool_.resize(kTableSize);
    h.per_msg_group_required_offsets_.push_back(MsgFieldsRun{.start = kStart, .count = kCount});
    EXPECT_TRUE(h.msg_group_required_pairs_impl(kMsgType).empty());
}

// The guard is `msg_group_ctx_delim_run`'s, which `for_each_group_ctx_record` also reads.
TEST(RunReaders102, MsgGroupCtxDelimsOverrunIsEmpty) {
    dict_metadata_handle h{mr()};
    one_message(h);
    h.group_ctx_delim_pool_.resize(kTableSize);
    h.per_msg_group_ctx_delim_offsets_.push_back(MsgFieldsRun{.start = kStart, .count = kCount});
    EXPECT_TRUE(h.msg_group_ctx_delims_impl(kMsgType).empty());
    EXPECT_EQ(h.msg_group_ctx_delim_run(kMsgType).count, 0U);
}

TEST(RunReaders102, EnumValuesOverrunIsEmpty) {
    dict_metadata_handle h{mr()};
    h.enum_values_.resize(kTableSize);
    h.enum_runs_.push_back(EnumRun{.tag = 54, .start = kStart, .count = kCount});
    EXPECT_TRUE(h.enum_values_impl(54).empty());
}

// Control: `run_at` sums in `std::size_t`, so this cell holds before and after 102.
TEST(RunReaders102, RunAtOverrunIsEmpty) {
    std::pmr::vector<MsgFieldsRun> runs{mr()};
    std::pmr::vector<std::uint16_t> pool{mr()};
    pool.resize(kTableSize);
    runs.push_back(MsgFieldsRun{.start = kStart, .count = kCount});
    EXPECT_TRUE(fixpp::dict::detail::run_at(runs, pool, 0).empty());
}
