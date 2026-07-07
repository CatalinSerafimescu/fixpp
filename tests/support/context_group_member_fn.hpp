// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/context_group_member_fn.hpp
//
// Shared copy of the context-aware group_member_fn_t the wire Parser's
// dict-lvalue ctor installs (include/fixpp/wire/parser.hpp) — resolves group
// membership from a `dict::table_view` via the context-scoped accessor
// (`group_member_tags(msg_type, parent_path, no_tag)`), which falls back to the
// bare-no_tag store on a context miss (table_view.hpp).
//
// Extracted from three byte-identical local copies:
//   - tests/dictionary/defect_a_group_context_test.cpp
//   - tests/dictionary/group_context_lookup_alloc_gate_test.cpp
//   - tests/wire/nested_group_extent_test.cpp
// `inline` ⇒ one definition across all TUs; taking its address yields the same
// group_member_fn_t pointer everywhere. Alloc-free (group_member_tags returns a
// span), so it is safe to use under the alloc-gate tests.
#pragma once

#include <cstdint>
#include <span>

#include <fixpp/dict/table_view.hpp>
#include <fixpp/wire/group_view.hpp>  // fixpp::wire::group_context

namespace fixpp_test_support {

inline bool context_group_member_fn(void const* d, fixpp::wire::group_context const& ctx,
                                    std::uint16_t no_tag, std::uint16_t tag) noexcept {
    auto const* dict = static_cast<fixpp::dict::table_view const*>(d);
    auto const members = dict->group_member_tags(
        ctx.msg_type, std::span<std::uint16_t const>{ctx.parent_path.data(), ctx.depth}, no_tag);
    for (auto const member_tag : members) {
        if (member_tag == tag) {
            return true;
        }
    }
    return false;
}

}  // namespace fixpp_test_support
