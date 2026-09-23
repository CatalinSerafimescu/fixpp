// SPDX-License-Identifier: AGPL-3.0-or-later
// src/dictionary/dictionary_snapshot.cpp
//
// fixpp#215 item 1, Option C (`.specify/215-dictionary-view.md` §3); the table's
// ownership is superseded by `.specify/495-493-486-dict-reify-copy.md` §6 (D-4).

#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/dictionary_snapshot.hpp>
#include <memory>
#include <utility>

namespace fixpp::dict {

dictionary_snapshot::dictionary_snapshot(detail::snapshot_key,
                                         std::shared_ptr<const Dictionary> src, table_view tv)
    : source_(std::move(src)), view_(std::make_shared<const table_view>(std::move(tv))) {}

table_view const& dictionary_snapshot::view() const noexcept { return *view_; }

std::shared_ptr<const table_view> const& dictionary_snapshot::view_owner() const noexcept {
    return view_;
}

std::shared_ptr<const Dictionary> const& dictionary_snapshot::source() const noexcept {
    return source_;
}

// Walks `dict` ONCE and pairs the result with it. Config-time only
// [const §XV.1].
std::shared_ptr<const dictionary_snapshot> make_dictionary_snapshot(
    std::shared_ptr<const Dictionary> dict) {
    if (!dict) {
        return nullptr;  // null dict -> null return
    }
    auto tv = dict->as_table_view();                     // SEQUENCED: walk first, ...
    return std::make_shared<const dictionary_snapshot>(  // ... then hand over ownership
        detail::snapshot_key{}, std::move(dict), std::move(tv));
}

// src/session/session.cpp and src/capi/session.cpp both take their table through
// this. fixpp#495 D-4: a copy of the snapshot's table owner — the returned pointer
// shares the TABLE's control block, so it does not keep the snapshot (and its
// Dictionary) alive.
std::shared_ptr<const table_view> shared_dictionary_view(
    std::shared_ptr<const dictionary_snapshot> snap) noexcept {
    if (!snap) {
        return nullptr;
    }
    return snap->view_owner();
}

}  // namespace fixpp::dict
