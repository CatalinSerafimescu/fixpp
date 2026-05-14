// SPDX-License-Identifier: AGPL-3.0-or-later
// extract from .specify/2c-codegen.md v1.3 §4.2 (signed off 2026-05-10)
// This file is a /plan Phase 1 contract extract — the canonical declaration
// lives at include/fixpp/dict/group_ref.hpp once 002-dictionary-xml-loader
// reaches /implement. Verbatim from §4.2; no /plan-time shape edits.
//
// Groups are repeating-field tuples (e.g., `NoLegs`/`Legs`,
// `NoMDEntries`/`MDEntries`). `_reserved` discipline mirrors §4.1.

#pragma once

#include <cstdint>
#include <type_traits>

namespace fixpp::dict {

struct GroupRef {
    std::uint16_t no_tag;              // NoXxx tag (e.g., 73 for NoOrders,
                                       // 555 for NoLegs).
    std::uint16_t first_field_tag;     // First field of group rule per
                                       // `[FIX50SP2 §3]` — used by
                                       // wire::Validator (`[2b §4.6]`'s
                                       // group_first_field) and by
                                       // group_view::iter() (`[2b §4.7]`).
    std::uint16_t first_field_index;   // index into the per-version FieldRef
                                       // array for the group's field list.
                                       // RUNTIME-MVS NOTE: under the XmlLoader
                                       // runtime path, this indexes into the
                                       // per-group side table returned by
                                       // Dictionary::group_fields(no_tag), NOT
                                       // the per-MsgType-concatenated fields_
                                       // array that field_ref() searches.
    std::uint16_t field_count;
    std::uint16_t parent_group_no_tag; // 0 if not nested; otherwise the
                                       // enclosing group's NoXxx tag
                                       // (handles W-007 nested repeating
                                       // groups per `[2b §4.7]`).
    std::uint16_t _reserved;           // forward-compat; zero on emit, ignore
                                       // on read in v1.0; reserved under
                                       // FIXPP_DICT_GROUPREF_RESERVED_USED
                                       // (per C-P3-1).
};

// AC-F3 per spec.md §4.3 — re-asserted in tests/dictionary/ref_shape_test.cpp
// (seam #4) for ABI-drift detection.
static_assert(sizeof(GroupRef) == 12);
static_assert(std::is_standard_layout_v<GroupRef>);
static_assert(std::is_trivially_copyable_v<GroupRef>);

}  // namespace fixpp::dict
