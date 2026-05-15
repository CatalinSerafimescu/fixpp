// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/dict/component_ref.hpp
//
// `ComponentRef` — per-component metadata POD. Canonical declaration;
// mirrors `[2c §4.2]` and the extract recorded at
// `specs/002-dictionary-xml-loader/contracts/component_ref.hpp`.
//
// Components are FIX 4.4+'s reusable field bundles (e.g., `Instrument`,
// `Parties`, `OrderQtyData`). `_reserved` discipline mirrors §4.1: zero on
// emit, ignored on read in v1.0; future minor version may use under
// `FIXPP_DICT_COMPONENTREF_RESERVED_USED` per C-P3-1.

#pragma once

#include <cstdint>
#include <type_traits>

namespace fixpp::dict {

struct ComponentRef {
    std::uint16_t component_id;
    std::uint16_t name_offset;
    std::uint16_t first_field_index;
    std::uint16_t field_count;
    std::uint16_t parent_component_id;
    std::uint16_t _reserved;
};

// AC-F2 per spec.md §4.3 — re-asserted in tests/dictionary/ref_shape_test.cpp
// (seam #4) for ABI-drift detection.
static_assert(sizeof(ComponentRef) == 12);
static_assert(std::is_standard_layout_v<ComponentRef>);
static_assert(std::is_trivially_copyable_v<ComponentRef>);

}  // namespace fixpp::dict
