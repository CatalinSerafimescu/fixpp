// SPDX-License-Identifier: AGPL-3.0-or-later
// src/session/fixt_route_set.cpp
//
// 099-fixt-appl-version-validation: fixt_route_set::make and find (data-model E-1, E-3).

#include "fixt_route_set.hpp"

#include <algorithm>
#include <cstddef>
#include <fixpp/core/length_data_pairs.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/dict/version_registry.hpp>
#include <fixpp/wire/validator.hpp>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace fixpp::session::detail {

namespace {

void add_distinct(std::vector<const fixpp::dict::Dictionary*>& into,
                  const fixpp::dict::Dictionary* d) {
    if (d != nullptr && std::ranges::find(into, d) == into.end()) {
        into.push_back(d);
    }
}

}  // namespace

std::shared_ptr<const fixt_route_set> fixt_route_set::make(
    const fixpp::dict::version_registry& registry,
    std::span<const fixpp::dict::Dictionary* const> listed) {
    using fixpp::dict::application_version;

    // Every registry slot, the transport dictionary and each listed session dictionary. A slot's
    // get() also answers the one-way fallback, which is the same pointer as the slot it reads.
    std::vector<const fixpp::dict::Dictionary*> dictionaries;
    for (auto v = static_cast<std::size_t>(application_version::Unknown) + 1;
         v <= static_cast<std::size_t>(application_version::vlatest); ++v) {
        if (auto const d = registry.get(static_cast<application_version>(v))) {
            add_distinct(dictionaries, *d);
        }
    }
    if (auto const t = registry.transport()) {
        add_distinct(dictionaries, *t);
    }
    for (const fixpp::dict::Dictionary* d : listed) {
        add_distinct(dictionaries, d);
    }

    std::shared_ptr<fixt_route_set> set{new fixt_route_set{}};
    for (const fixpp::dict::Dictionary* d : dictionaries) {
        set->add(*d, nullptr, d->as_table_view(fixpp::dict::fixt_framing));
    }

    // Each listed S that declares a header pair, once however many sessions list it, on every
    // other dictionary's table (research R-20).
    std::vector<const fixpp::dict::Dictionary*> pair_sources;
    for (const fixpp::dict::Dictionary* s : listed) {
        if (std::ranges::find(pair_sources, s) != pair_sources.end()) {
            continue;
        }
        pair_sources.push_back(s);
        auto const pairs = fixpp::dict::header_length_pairs(*s);
        if (pairs.empty()) {
            continue;
        }
        for (const fixpp::dict::Dictionary* d : dictionaries) {
            if (d != s) {
                set->add(*d, s, d->as_table_view(fixpp::dict::fixt_framing, pairs));
            }
        }
    }
    return set;
}

void fixt_route_set::add(const fixpp::dict::Dictionary& d,
                         const fixpp::dict::Dictionary* pair_source,
                         fixpp::dict::table_view table) {
    auto validator =
        std::make_shared<const fixpp::wire::dictionary_driven_validator>(std::move(table));
    std::shared_ptr<const fixpp::dict::table_view> owner{validator, &validator->table()};
    entries_.push_back(std::make_unique<const keyed_entry>(keyed_entry{
        .d = &d,
        .pair_source = pair_source,
        .routes = entry{.validator = std::move(validator), .owner = std::move(owner)},
    }));
}

const fixt_route_set::entry* fixt_route_set::find(
    const fixpp::dict::Dictionary* d, const fixpp::dict::Dictionary* pair_source) const noexcept {
    for (auto const& e : entries_) {
        if (e->d == d && e->pair_source == pair_source) {
            return &e->routes;
        }
    }
    return nullptr;
}

}  // namespace fixpp::session::detail
