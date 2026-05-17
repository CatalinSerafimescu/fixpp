// SPDX-License-Identifier: AGPL-3.0-or-later
// CONTRACT SHAPE ORACLE — extract of [2b §4.3] (the field_view returned by
// MessageView::get<Tag>()/get(tag)). Authority: .specify/2b-wire.md v0.2.
// This is not the implementation; src/include carries the real header.
//
// Why this oracle exists (Gate A round 1, Root cause #1): field_view is the
// type MessageView::get<Tag>()/get(tag) return ([2b §4.3] lines 282-288) and
// the type merged-003's dict::reify / dict::field_traits::decode_field consume
// across the 2b cutover (it calls fv->bytes() and the decimal arm calls
// fixpp::decimal_t::parse(fv->bytes(), mr) — 003 data-model I-1 / RC#2). It is
// a [2b §4.1] View-derived flyweight ([2b §612]: every View-derived type —
// MessageView, frame_view, field_view, group_view, unknown_fields_view — is a
// flyweight over the originating frame buffer). [2b §4.3] forward-declares it
// (`expected_t<field_view>`) but enumerates no class body; the R6 frozen stub
// (include/fixpp/wire/message_view_contract.hpp:53-59) and 003's oracle
// (specs/003-dictionary-codegen/contracts/wire_message_view_contract.hpp:28-31)
// both pin bytes()+as_string() with NO View base. This oracle reconciles those
// against the [2b §4.1]/[2b §612] View-derived requirement so the cutover
// surface migration has a single authoritative field_view shape.
#pragma once
#include <cstddef>
#include <span>
#include <string_view>
#include "view.hpp"

namespace fixpp::wire {

// Borrowed view over one field's bytes ([2b §4.3]; View-derived per
// [2b §4.1]/[2b §612]). Aliases the originating frame buffer; lifetime is the
// frame buffer's (per-message arena slot, reset after fromApp). The member set
// below is the cutover constraint: merged-003 dict::reify / decode_field call
// bytes() (and the decimal accessor reads fv->bytes() before
// decimal_t::parse(span, mr) — 003 I-1/RC#2). as_string() is retained from the
// frozen-stub / 003-oracle surface so the migration is source-compatible with
// every merged-003 call site.
class field_view : public View {
public:
    // Returned span aliases the View's data (the originating frame bytes).
    // Inherited from View ([2b §4.1]) — [[clang::lifetimebound]] on *this:
    //   [[nodiscard]] std::span<const std::byte> bytes() const noexcept;
    //   [[nodiscard]] bool empty() const noexcept;

    // String-typed convenience over bytes() — retained from the R6 frozen
    // stub + 003 oracle so the cutover is a pure surface migration (no
    // merged-003 call site loses a member it already binds).
    [[nodiscard]] std::string_view
    as_string() const noexcept [[clang::lifetimebound]];
};

}  // namespace fixpp::wire
