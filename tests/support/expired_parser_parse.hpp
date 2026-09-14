#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/expired_parser_parse.hpp
// TEST-ONLY: parse through a Parser that is destroyed before the returned
// MessageView is used.
//
// Parser::parse() is [[clang::lifetimebound]] on `this` (the [2b] contract a
// consumer must honour), yet the view copies the dictionary pointer, the
// callbacks and the arena by value, so a view outliving its Parser is
// well-defined (PR68-10, gate-b/r3 commit 55b13459). Tests that pin that
// robustness breach the annotation on purpose; they go through this helper so
// the breach is suppressed in one place instead of at every call site.

#include <fixpp/dict/table_view.hpp>
#include <fixpp/wire/parser.hpp>
#include <memory_resource>
#include <utility>

namespace fixpp::wire::test {

template <class... Cfg>
[[nodiscard]] auto parse_with_expired_parser(dict::table_view const& dict, frame_view const& frame,
                                             std::pmr::memory_resource* mr, Cfg&&... cfg) {
    Parser<access_mode::Index> parser{dict};
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-stack-address"
#endif
    return parser.parse(frame, mr, std::forward<Cfg>(cfg)...);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
}

}  // namespace fixpp::wire::test
