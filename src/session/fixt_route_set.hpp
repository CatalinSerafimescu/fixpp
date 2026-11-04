// SPDX-License-Identifier: AGPL-3.0-or-later
// src/session/fixt_route_set.hpp
//
// 099-fixt-appl-version-validation (data-model E-1, NFR-003, FR-028): one validator per distinct
// route table, built once per Engine, or once by a directly constructed session, and read without
// a lock after that. Private to the session library: session.hpp and engine.hpp forward-declare it
// and hold it through a shared_ptr, so neither public header gains validator.hpp.
//
// A route table is `{d, null}` for every registry slot, the transport dictionary and each listed
// session dictionary, and `{d, S}` for each listed S that declares a header pair and every other
// `d`. Each is an as_table_view(fixt_framing) form moved into its validator, so one dictionary
// has one table, and the parse route's owner is an aliasing shared_ptr onto it.
//
// Written once, in build(), and read-only after. It is handed out only after build() returns, so a
// reader on another strand sees a fully built object: the edge is the role loop's co_spawn in
// Engine::start(), and on a C engine also the worker's thread creation. Entries are heap-held and
// never moved or reassigned, so `&entry.owner` is stable (the owner-object rule at Parser's owned
// route constructor). The set dies with its last holder, so no declaration order matters, and a
// view's clone or reify copies `owner`, which keeps its table alive on its own. find() runs only
// in Session::open(), while the config and the registry hold the dictionaries it is keyed on, so a
// freed and reused address cannot match.
#pragma once

#include <cstddef>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/version_registry.hpp>
#include <fixpp/wire/validator.hpp>
#include <memory>
#include <new>
#include <ranges>
#include <span>
#include <vector>

namespace fixpp::session::detail {

class fixt_route_set {
public:
    struct entry {
        std::shared_ptr<const fixpp::wire::dictionary_driven_validator>
            validator;                                         // holds the one table by value
        std::shared_ptr<const fixpp::dict::table_view> owner;  // aliases validator->table()
    };

    fixt_route_set(const fixt_route_set&) = delete;
    fixt_route_set& operator=(const fixt_route_set&) = delete;
    fixt_route_set(fixt_route_set&&) = delete;
    fixt_route_set& operator=(fixt_route_set&&) = delete;
    ~fixt_route_set() = default;

    // Builds every entry, over a forward range of `const dict::Dictionary*`, or none: an
    // allocation failure anywhere in it is `out_of_memory`, and nothing is published (FR-028).
    template <std::ranges::forward_range Dictionaries>
    [[nodiscard]] static fixpp::core::expected_t<std::shared_ptr<const fixt_route_set>> build(
        const fixpp::dict::version_registry& registry,
        Dictionaries&& session_dictionaries) noexcept {
        try {
            std::vector<const fixpp::dict::Dictionary*> listed;
            for (const fixpp::dict::Dictionary* d : session_dictionaries) {
                if (d != nullptr) {
                    listed.push_back(d);
                }
            }
            return make(registry, listed);
        } catch (const std::bad_alloc&) {
            return std::unexpected(fixpp::core::error::out_of_memory);
        }
    }

    // The entry for `d` with `pair_source`'s header pairs, or null if the set holds none. S's own
    // entry is find(S, nullptr), since `{d, S}` exists only for every other `d` (data-model E-6).
    [[nodiscard]] const entry* find(const fixpp::dict::Dictionary* d,
                                    const fixpp::dict::Dictionary* pair_source) const noexcept;

    // Whether any entry carries `s`'s header pairs. It reads what build() decided, so a session
    // asks it without walking `s`, and cannot fail.
    [[nodiscard]] bool has_pair_source(const fixpp::dict::Dictionary* s) const noexcept;

private:
    struct keyed_entry {
        const fixpp::dict::Dictionary* d = nullptr;
        const fixpp::dict::Dictionary* pair_source = nullptr;
        entry routes;
    };

    fixt_route_set() = default;

    [[nodiscard]] static std::shared_ptr<const fixt_route_set> make(
        const fixpp::dict::version_registry& registry,
        std::span<const fixpp::dict::Dictionary* const> listed);
    void add(const fixpp::dict::Dictionary& d, const fixpp::dict::Dictionary* pair_source,
             fixpp::dict::table_view table);

    std::vector<std::unique_ptr<const keyed_entry>> entries_;
};

}  // namespace fixpp::session::detail
