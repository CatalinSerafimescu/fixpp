// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/dict/group_ref.hpp
//
// `GroupRef` — per-repeating-group metadata POD. Canonical declaration;
// mirrors `[2c §4.2]` and the extract recorded at
// `specs/002-dictionary-xml-loader/contracts/group_ref.hpp`.
//
// Groups are repeating-field tuples (e.g., `NoLegs`/`Legs`,
// `NoMDEntries`/`MDEntries`). `_reserved` discipline mirrors §4.1.

#pragma once

#include <cstdint>
#include <type_traits>

namespace fixpp::dict {

struct GroupRef {
    std::uint16_t no_tag;
    std::uint16_t first_field_tag;
    std::uint16_t first_field_index;
    std::uint16_t field_count;
    std::uint16_t parent_group_no_tag;
    std::uint16_t _reserved;
};

// AC-F3 per spec.md §4.3 — re-asserted in tests/dictionary/ref_shape_test.cpp
// (seam #4) for ABI-drift detection.
static_assert(sizeof(GroupRef) == 12);
static_assert(std::is_standard_layout_v<GroupRef>);
static_assert(std::is_trivially_copyable_v<GroupRef>);

}  // namespace fixpp::dict
