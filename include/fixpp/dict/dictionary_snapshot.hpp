// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/dict/dictionary_snapshot.hpp
//
// ⚠️ Superseded in part by `.specify/495-493-486-dict-reify-copy.md` §6 (D-4,
// fixpp#495, owner rulings R-B/R-C): the snapshot owns its table in the table's
// OWN control block, and shared_dictionary_view() hands out that owner instead of
// forming an aliasing pointer into the snapshot. A held view therefore keeps the
// table alive but neither the snapshot nor its Dictionary. 215's alias design
// (§3's helper, §5b's "third owner of the snapshot's control block", §6 seam 7's
// G2 count of one) is history; the pairing and the passkey below are unchanged.
//
// fixpp#215 item 1, Option C (`.specify/215-dictionary-view.md` §3) —
// `fixpp::dict::dictionary_snapshot`: an opaque, non-copyable, non-movable
// value object that pairs a `table_view` with the `Dictionary` it was built
// from. Replaces `SessionConfig::dictionary_view` (a bare
// `shared_ptr<const table_view>`), which had two defects (§2): C1 — the
// `const` on the pointee WAS not enforced, because `table_view` then carried
// its population surface in public (add_valid_tag, set_group_first,
// set_length_pair_data_tag, …), and a caller holding a non-const alias could
// repopulate a published view — and C4 — nothing checked that the view actually came
// from the paired `SessionConfig::dictionary`, so a mismatched pair silently
// drove inbound parsing/validation from the wrong grammar.
//
// C1 is closed AT THE INJECTION POINT by construction: the only way to seat
// a view on a `SessionConfig` is through `make_dictionary_snapshot`, and the
// object it returns exposes the view only with a `const` pointee —
// `table_view const&` from view(), `shared_ptr<const table_view>` from
// view_owner() — from a type that cannot be copied, moved, or value-constructed. C4 is rejected
// fail-closed at `Session::open()` (a runtime pointer-identity compare
// against `source()`, not a construction closure — see the design doc §3).
//
// `Dictionary::as_table_view()` (dictionary.hpp's own declaration) stays public and keeps
// returning a `table_view` BY VALUE — `dictionary_driven_validator` still holds one by
// value under the frozen SC-007 design point (validator.hpp's
// `dictionary_driven_validator` ctor). fixpp#456 sealed that view's population surface
// (`table_view.hpp`'s STORAGE banner); it is still returned by value into a non-`const`
// caller variable unless the caller declares otherwise. What this type does remains what
// it always did and is NOT subsumed by that seal — it is the PAIRING with the source
// `Dictionary` that closes C4, which the seal does not touch.
#pragma once

#include <fixpp/dict/table_view.hpp>
#include <memory>

namespace fixpp::dict {

class Dictionary;
class dictionary_snapshot;

// Declared BEFORE the passkey: a qualified friend declaration cannot
// introduce a name, so the factory must already be visible when
// snapshot_key befriends it.
[[nodiscard]] std::shared_ptr<const dictionary_snapshot> make_dictionary_snapshot(
    std::shared_ptr<const Dictionary> dict);

namespace detail {
// Passkey. NAMEABLE and COPYABLE from anywhere (it is a class in a named
// namespace in a public header); what is restricted is CONSTRUCTING one from
// nothing, which only make_dictionary_snapshot can do. That is what makes
// the constructor below public to the standard library (what make_shared
// needs) while leaving it callable by exactly one function (what closes
// C1). The friend list below IS the boundary — /speckit-verify Step 1's G1
// census pins that it stays a list of one.
class snapshot_key {
    snapshot_key() = default;
    friend std::shared_ptr<const dictionary_snapshot> fixpp::dict::make_dictionary_snapshot(
        std::shared_ptr<const Dictionary>);
};
}  // namespace detail

// A table_view PAIRED WITH the Dictionary it was built from. Copy AND move
// are deleted so the type is reachable ONLY through the shared_ptr the
// factory returns, and so a copy cannot silently duplicate an expensive
// table_view. The table itself lives behind its own shared_ptr (fixpp#495 D-4),
// whose pointee never relocates.
class dictionary_snapshot {
public:
    // Public so std::make_shared can reach it; unreachable without a
    // snapshot_key, which only make_dictionary_snapshot can mint. `tv` is BY
    // VALUE — see the design doc §3/§4 for the move/allocation accounting.
    dictionary_snapshot(detail::snapshot_key, std::shared_ptr<const Dictionary> src, table_view tv);

    dictionary_snapshot(dictionary_snapshot const&) = delete;
    dictionary_snapshot& operator=(dictionary_snapshot const&) = delete;
    dictionary_snapshot(dictionary_snapshot&&) = delete;
    dictionary_snapshot& operator=(dictionary_snapshot&&) = delete;

    [[nodiscard]] table_view const& view() const noexcept [[clang::lifetimebound]];
    // fixpp#495 D-4: the table's owner. Never null. The pointee is `const`, the same
    // one shared_dictionary_view() hands out; `SessionConfig` accepts only a
    // `dictionary_snapshot`, so a bare owner is no injection surface.
    [[nodiscard]] std::shared_ptr<const table_view> const& view_owner() const noexcept
        [[clang::lifetimebound]];
    [[nodiscard]] std::shared_ptr<const Dictionary> const& source() const noexcept
        [[clang::lifetimebound]];

private:
    std::shared_ptr<const Dictionary> source_;
    std::shared_ptr<const table_view> view_;
};

// The snapshot's table owner, for `Session::open()` and `fixpp_session_open`
// (seating `fixpp_session::tv_`), which both go through this. Since fixpp#495
// D-4 it returns `snap->view_owner()` — a held result keeps the table alive but
// not the snapshot or its Dictionary. Null snap -> null return.
[[nodiscard]] std::shared_ptr<const table_view> shared_dictionary_view(
    std::shared_ptr<const dictionary_snapshot> snap) noexcept;

}  // namespace fixpp::dict
