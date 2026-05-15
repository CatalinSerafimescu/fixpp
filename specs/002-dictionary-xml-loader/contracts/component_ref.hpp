// SPDX-License-Identifier: AGPL-3.0-or-later
// extract from .specify/2c-codegen.md v1.3 §4.2 (signed off 2026-05-10)
// This file is a /plan Phase 1 contract extract — the canonical declaration
// lives at include/fixpp/dict/component_ref.hpp once 002-dictionary-xml-loader
// reaches /implement. Verbatim from §4.2; no /plan-time shape edits.
//
// Components are FIX 4.4+'s reusable field bundles (e.g., `Instrument`,
// `Parties`, `OrderQtyData`). The `_reserved` discipline mirrors §4.1: zero
// on emit, ignored on read in v1.0; future minor version may use under
// `FIXPP_DICT_COMPONENTREF_RESERVED_USED` per C-P3-1.

#pragma once

#include <cstdint>
#include <type_traits>

namespace fixpp::dict {

struct ComponentRef {
    std::uint16_t component_id;        // unique per version
    std::uint16_t name_offset;         // offset into per-version name string
                                       // pool (constexpr std::string_view;
                                       // for diagnostics).
    std::uint16_t first_field_index;   // index into the per-version FieldRef
                                       // array. RUNTIME-MVS NOTE: under the
                                       // XmlLoader runtime path, this indexes
                                       // into the per-component side table
                                       // returned by Dictionary::component_fields(name),
                                       // NOT the per-MsgType-concatenated fields_
                                       // array that field_ref() searches. The
                                       // codegen-emitted per-version layout
                                       // uses a single global FieldRef array
                                       // where components have contiguous slices.
    std::uint16_t field_count;         // number of fields in this component.
    std::uint16_t parent_component_id; // 0 if top-level; otherwise the
                                       // enclosing component (components
                                       // nest in FIX 4.4+).
    std::uint16_t _reserved;           // forward-compat; zero on emit, ignore
                                       // on read in v1.0; reserved under
                                       // FIXPP_DICT_COMPONENTREF_RESERVED_USED
                                       // (per C-P3-1).
};

// AC-F2 per spec.md §4.3 — re-asserted in tests/dictionary/ref_shape_test.cpp
// (seam #4) for ABI-drift detection.
static_assert(sizeof(ComponentRef) == 12);
static_assert(std::is_standard_layout_v<ComponentRef>);
static_assert(std::is_trivially_copyable_v<ComponentRef>);

}  // namespace fixpp::dict
