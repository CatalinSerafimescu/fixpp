// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/context_group_registered_fn.hpp
//
// 100-wire-group-read-integrity (research D2): the registration predicate the wire
// Parser's `dict_hooks::for_table_view` installs, as a free function — the sibling of
// tests/support/context_group_member_fn.hpp and context_group_delim_fn.hpp, resolving
// through the SAME `dict::table_view` and the SAME context key
// (`table_view::group_registered(msg_type, parent_path, no_tag)`).
//
// A test that builds a bundle through `dict_hooks_test_access::make` over a real
// `table_view` passes this beside its membership stub, so the two answer from one
// table. A null registration callback is the bundle a dict-aware read declines.
//
// Alloc-free: hash probes and bit tests only.
#pragma once

#include <cstdint>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/wire/group_view.hpp>  // fixpp::wire::group_context
#include <span>

namespace fixpp_test_support {

inline bool context_group_registered_fn(void const* d, fixpp::wire::group_context const& ctx,
                                        std::uint16_t no_tag) noexcept {
    auto const* dict = static_cast<fixpp::dict::table_view const*>(d);
    return dict->group_registered(
        ctx.msg_type, std::span<std::uint16_t const>{ctx.parent_path.data(), ctx.depth}, no_tag);
}

}  // namespace fixpp_test_support
