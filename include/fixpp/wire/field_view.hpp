#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/wire/field_view.hpp
// [2b §4.3] field_view : View — the type MessageView::get<Tag>()/get(tag)
// return and the cutover-load-bearing shape merged-003 dict::reify /
// dict::field_traits::decode_field consume (003 I-1/RC#2: it calls
// fv->bytes(), the decimal arm then fixpp::decimal_t::parse(fv->bytes(), mr)).
// Authority: .specify/2b-wire.md v0.2; shape oracle contracts/field_view.hpp
// (D-16). bytes()/empty() are inherited from View ([2b §4.1]); as_string()
// is retained from the R6 frozen stub + 003 oracle so the cutover is a pure
// surface migration (no merged-003 call site loses a member it binds).

#include <cstddef>
#include <span>
#include <string_view>

#include "view.hpp"

namespace fixpp::wire {

class field_view : public View {
public:
    constexpr field_view() noexcept = default;

    // String-typed convenience over bytes() — aliases the originating frame.
    [[nodiscard]] std::string_view
    as_string() const noexcept [[clang::lifetimebound]] {
        auto b = bytes();
        return {reinterpret_cast<char const*>(b.data()), b.size()};
    }

protected:
    constexpr field_view(std::byte const* data [[clang::lifetimebound]],
                          std::size_t len,
                          detail::generation_token gen) noexcept
        : View{data, len, gen} {}

    // Only the parser (and the test factory) mint a populated field_view,
    // always over the originating frame buffer.
    friend struct field_view_access;
};

// Friend seam: lets the header-only Parser/OffsetTable construct a
// field_view over a sub-span of the frame without exposing the ctor.
struct field_view_access {
    static field_view make(std::byte const* data, std::size_t len,
                            detail::generation_token gen) noexcept {
        return field_view{data, len, gen};
    }
};

}  // namespace fixpp::wire
