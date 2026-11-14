// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/dict/component_ref.hpp
//
// `ComponentRef` — per-component metadata POD. Canonical declaration;
// mirrors `[2c §4.2]` and the extract recorded at
// `specs/002-dictionary-xml-loader/contracts/component_ref.hpp`.
//
// Components are FIX 4.4+'s reusable field bundles (e.g., `Instrument`,
// `Parties`, `OrderQtyData`).
//
// 102-dict-width-multichar-type D1 supersedes the 16-bit layout of `[2c §4.2]`
// and of the 002 extract: every member is `std::uint32_t`, in the declared
// order, so a component's id, name offset, side-table start, member count and
// parent id are its own while each table's size fits in `std::uint32_t`. The
// `_reserved` member, and with it the `FIXPP_DICT_COMPONENTREF_RESERVED_USED`
// forward-compatibility slot of C-P3-1, is removed.
// `component_id` is the 0-based declaration index in both formats, not an
// Orchestra document's `fixr:component id`.

#pragma once

#include <cstdint>
#include <type_traits>

namespace fixpp::dict {

struct ComponentRef {
    std::uint32_t component_id;
    std::uint32_t name_offset;
    std::uint32_t first_field_index;
    std::uint32_t field_count;
    std::uint32_t parent_component_id;  // 0 = top level, else the parent's component_id + 1
};

// AC-F2 per spec.md §4.3 — re-asserted in tests/dictionary/ref_shape_test.cpp
// (seam #4) for ABI-drift detection.
static_assert(sizeof(ComponentRef) == 20);
static_assert(alignof(ComponentRef) == 4);
static_assert(std::is_standard_layout_v<ComponentRef>);
static_assert(std::is_trivially_copyable_v<ComponentRef>);

}  // namespace fixpp::dict
