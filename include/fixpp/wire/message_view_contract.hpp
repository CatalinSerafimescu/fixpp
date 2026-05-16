// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/wire/message_view_contract.hpp
//
// 2b CUTOVER (004-wire-codec, T028) — this header WAS the R6 frozen-thin
// vendored stub (003-dictionary-codegen, research.md D-2). It is now a thin
// RE-EXPORT of the real OffsetTable-backed wire surface in
// <fixpp/wire/parser.hpp>. The include path is preserved verbatim so every
// merged-003 consumer (dict::reify / dict::field_traits / the generated
// <vXX>/Messages.hpp) keeps binding `#include <fixpp/wire/message_view_
// contract.hpp>` with NO call-site edit — but the SURFACE is now the real
// [2b §4.3] `MessageView<Mode> : public View` (R6 / I-12: 2b owns the body
// AND now ships the real surface; the stub is retired).
//
// Surface compatibility (verified by the static_asserts below + the seam #18
// drift guard tests/codegen/flyweight_shape_test.cpp):
//   * access_mode — the frozen stub exposed ONLY Index; the real enum is
//     {Iter, Index}. 003 only ever names access_mode::Index, so every 003
//     call site still resolves.
//   * field_view — bytes() (inherited from View) + as_string() retained.
//   * group_view<T> — size()/operator[] retained (real adds iter()/begin()/
//     end(), additive).
//   * MessageView<Index> — get<Tag>()/get(tag)/group<NoTag,T>()/
//     unknown_fields() retained; real adds msg_type/msg_seq_num/offsets/
//     get_decimal (additive). A default-constructed MessageView reports
//     every field absent (empty OffsetTable -> wire_required_field_missing),
//     preserving the frozen stub's field-absent behaviour shape.
//
// Bridge surface (arch §2.4 v0.3 dual-compile carve-out, RC#3): still NOT a
// cyclic dictionary->wire MODULE edge — check_layers.py
// BRIDGE_EXEMPT_INCLUDES. NOTE: the real surface introduces a LINK edge
// (OffsetTable/Framer bodies live in fixpp_wire); fixpp_dictionary and the
// dictionary/codegen test targets now link fixpp_wire (CMake, T028).
#pragma once

#include <fixpp/wire/parser.hpp>  // the real [2b §4.3] surface (re-exported):
                                  // access_mode, field_view, group_view,
                                  // MessageView<Mode>, OffsetTable, frame_view.

namespace fixpp::wire {

// Seam #18 / I-12 drift guard, co-located with the re-export so any future
// surface drift fails HERE (not only in flyweight_shape_test.cpp).
static_assert(std::is_base_of_v<View, field_view>,
              "field_view must be the real `: public View` shape (cutover)");
static_assert(std::is_base_of_v<View, MessageView<access_mode::Index>>,
              "MessageView<Index> must be the real `: public View` shape");
static_assert(static_cast<int>(access_mode::Index) >= 0,
              "access_mode::Index must remain a valid enumerator (003 binds "
              "MessageView<access_mode::Index>)");

}  // namespace fixpp::wire
