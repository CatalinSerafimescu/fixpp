// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/dict/dictionary_snapshot.hpp
//
// fixpp#215 item 1, Option C (`.specify/215-dictionary-view.md` §3) —
// `fixpp::dict::dictionary_snapshot`: an opaque, non-copyable, non-movable
// value object that pairs a `table_view` with the `Dictionary` it was built
// from. Replaces `SessionConfig::dictionary_view` (a bare
// `shared_ptr<const table_view>`), which had two defects (§2): C1 — the
// `const` on the pointee is not enforced, since `table_view` carries 15
// public mutators — and C4 — nothing checked that the view actually came
// from the paired `SessionConfig::dictionary`, so a mismatched pair silently
// drove inbound parsing/validation from the wrong grammar.
//
// C1 is closed AT THE INJECTION POINT by construction: the only way to seat
// a view on a `SessionConfig` is through `make_dictionary_snapshot`, and the
// object it returns exposes the view only as `table_view const&` from a type
// that cannot be copied, moved, or value-constructed. C4 is rejected
// fail-closed at `Session::open()` (a runtime pointer-identity compare
// against `source()`, not a construction closure — see the design doc §3).
//
// `Dictionary::as_table_view()` (dictionary.hpp:212) stays public and keeps
// returning a mutable `table_view` BY VALUE — `dictionary_driven_validator`
// still holds one by value under the frozen SC-007 design point
// (validator.hpp:112) — so this type does not make `table_view` itself
// immutable. It makes the session-config injection point stop admitting a
// mutable alias.
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
// table_view. NOT for pointer stability — a shared_ptr's pointee never
// relocates, so the aliasing pointer formed by shared_dictionary_view()
// below is safe regardless.
class dictionary_snapshot {
public:
    // Public so std::make_shared can reach it; unreachable without a
    // snapshot_key, which only make_dictionary_snapshot can mint. `tv` is BY
    // VALUE — see the design doc §3/§4 for the move/allocation accounting.
    dictionary_snapshot(detail::snapshot_key, std::shared_ptr<const Dictionary> src,
                        table_view tv);

    dictionary_snapshot(dictionary_snapshot const&) = delete;
    dictionary_snapshot& operator=(dictionary_snapshot const&) = delete;
    dictionary_snapshot(dictionary_snapshot&&) = delete;
    dictionary_snapshot& operator=(dictionary_snapshot&&) = delete;

    [[nodiscard]] table_view const& view() const noexcept [[clang::lifetimebound]];
    [[nodiscard]] std::shared_ptr<const Dictionary> const& source() const noexcept
        [[clang::lifetimebound]];

private:
    std::shared_ptr<const Dictionary> source_;
    table_view view_;
};

// The ONE production site that forms the aliasing view pointer. Both
// `Session::open()` and `fixpp_session_open` (seating `fixpp_session::tv_`)
// MUST go through this. Null snap -> null return.
[[nodiscard]] std::shared_ptr<const table_view> shared_dictionary_view(
    std::shared_ptr<const dictionary_snapshot> snap) noexcept;

}  // namespace fixpp::dict
