#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/dict_hooks_test_access.hpp — TEST-ONLY dict_hooks::make.
//
// `wire::dict_hooks`'s field-by-field constructor is private (fixpp#426,
// design §3): production code can only build a bundle through `none()` or
// `for_table_view()`, both of which fill every field from ONE dictionary.
// A stub-dictionary test (a hand-rolled uint16 token, a counting/null-member
// fixture, a deliberately half-threaded probe such as
// `context_group_delim_fn.hpp`'s own note) needs the raw constructor — this
// is that seam, mirroring the `offset_table_test_access` precedent in
// wire_test_hooks.hpp. Never installed; production targets must not include
// this header.
// The friend declaration in dict_hooks.hpp is unconditional (fixpp#511), so
// this struct is too: it needs no test macro.

#include <fixpp/wire/dict_hooks.hpp>

namespace fixpp::wire {

struct dict_hooks_test_access {
    [[nodiscard]] static constexpr dict_hooks make(
        void const* opaque_dict, dict_hooks::classify_fn_t classify,
        dict_hooks::group_member_fn_t group_member, dict_hooks::group_delim_fn_t group_delim,
        dict_hooks::length_pair_fn_t length_pair) noexcept {
        return dict_hooks{opaque_dict, classify, group_member, group_delim, length_pair};
    }
};

}  // namespace fixpp::wire
