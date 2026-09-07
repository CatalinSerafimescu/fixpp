// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/context_group_delim_fn.hpp
//
// 384: shared copy of the context-aware group_delim_fn_t the wire Parser's
// dict-lvalue ctor installs (include/fixpp/wire/parser.hpp, 083 T057 / C-8.1)
// — the SIBLING of tests/support/context_group_member_fn.hpp, resolving the
// instance delimiter from a `dict::table_view` through the SAME context key
// (`group_first_field(msg_type, parent_path, no_tag)`).
//
// Why it exists: 384 removed the `= nullptr` default from every dict-aware
// `OffsetTable` / `MessageView` constructor, so a test that wants the
// PRODUCTION shape must now name its delimiter oracle. This is that name.
// A test that deliberately wants the pre-083 wire-derived split passes an
// explicit `nullptr` instead and says why — the two are no longer spelled the
// same way.
//
// Alloc-free (a hash lookup returning a scalar; the bare-global fallback is
// likewise a lookup), so it is safe under the alloc-gate cells — the same
// property context_group_member_fn.hpp documents for itself.
//
// ⚠️ A stub returning 0 is NOT equivalent to threading this: `group_slices_
// status()` keeps the wire-derived delimiter when the callback answers 0, so a
// zero-returning stub yields THE SAME DELIMITER AND THE SAME SLICES as
// `nullptr`. Precisely — the two are indistinguishable *in the split*, not in
// every respect: a non-null callback is still INVOKED, so a stub that counts
// or logs its calls is observable at its own counter. That is the trap, not an
// escape from it: the counter then reads "threaded" while the split is the
// un-informed one. The split is the only observation that matters here.
// Thread THIS, or pass `nullptr` deliberately; do not invent a third spelling
// that looks threaded and is not.
#pragma once

#include <cstdint>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/wire/group_view.hpp>  // fixpp::wire::group_context
#include <span>

namespace fixpp_test_support {

inline std::uint16_t context_group_delim_fn(void const* d, fixpp::wire::group_context const& ctx,
                                            std::uint16_t no_tag) noexcept {
    auto const* dict = static_cast<fixpp::dict::table_view const*>(d);
    return dict->group_first_field(
        ctx.msg_type, std::span<std::uint16_t const>{ctx.parent_path.data(), ctx.depth}, no_tag);
}

}  // namespace fixpp_test_support
