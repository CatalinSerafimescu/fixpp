// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/dictionary_snapshot_test.cpp
//
// fixpp#215 item 1, Option C (`.specify/215-dictionary-view.md` §6 seams 4 +
// 5) — pins on `fixpp::dict::dictionary_snapshot` / `make_dictionary_snapshot`
// / `shared_dictionary_view`.
//
// Seam 5 (A1-A5): boundary pins on the properties C1's closure rests on.
// These live in a TU OUTSIDE fixpp::dict deliberately — the qualification is
// load-bearing, since access checking in is_*_constructible is performed as
// if in an unrelated context (that is what makes A5 meaningful: it goes RED
// exactly when the passkey's friend list is opened).
//
// Seam 4: alias lifetime and IDENTITY, tested on shared_dictionary_view — the
// production helper — not on std::shared_ptr directly. v0.2's version of this
// seam was GREEN for a helper that COPIES instead of aliasing (measured, not
// argued, in the design doc); the fix is to pin identity and shared
// ownership, not just validity.
//
// This file is also G1's A5TU allowlist entry
// (tools/check_dictionary_snapshot_exclusivity.sh) — relocating these
// assertions requires editing that script's allowlist AND its per-file
// liveness loop, or the gate goes DEAD on a legitimate move.

#include <gtest/gtest.h>

#include <concepts>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/dictionary_snapshot.hpp>
#include <memory>
#include <type_traits>

#include "support/validation_test_dictionary.hpp"

using namespace fixpp::dict;

// ── Seam 5 — A1-A5 boundary pins ─────────────────────────────────────────────

// A1 — the type must be neither copyable nor movable. Measures TYPE SHAPE,
// not closure: kept because the shape survives a later public value
// constructor or a `table_view&` accessor, either of which reopens the
// injection hole while this alone stays green (hence A2-A5 below).
static_assert(!std::is_copy_constructible_v<dictionary_snapshot> &&
              !std::is_move_constructible_v<dictionary_snapshot>);

// A2 — the value ctor must stay unreachable. Pins the absence of a
// two-argument constructor; independent of the passkey (would hold with no
// passkey at all) — NOT the passkey boundary. See A5.
static_assert(!std::is_constructible_v<dictionary_snapshot, std::shared_ptr<const Dictionary>,
                                       table_view>);

// A3 — view() must expose const&, never a mutable reference or a copy.
static_assert(std::same_as<decltype(std::declval<dictionary_snapshot const&>().view()),
                           table_view const&>);

// A4 — the factory must hand back a const snapshot, never a mutable one.
static_assert(
    std::same_as<decltype(make_dictionary_snapshot(std::declval<std::shared_ptr<const Dictionary>>())),
                std::shared_ptr<const dictionary_snapshot>>);

// A5 — the passkey boundary itself: nobody outside the friend list may mint a
// key. Access checking in is_*_constructible is performed as if in an
// unrelated context, so this goes RED exactly when the key is opened (moving
// detail::snapshot_key's ctor to `public:`) — the mutation A1-A4 cannot see.
static_assert(!std::is_default_constructible_v<detail::snapshot_key>);

namespace {

// ── make_dictionary_snapshot: basic contract ─────────────────────────────────

TEST(DictionarySnapshot, NullDictionaryYieldsNullSnapshot) {
    std::shared_ptr<const Dictionary> null_dict;
    auto snap = make_dictionary_snapshot(null_dict);
    EXPECT_EQ(snap, nullptr);
}

TEST(DictionarySnapshot, NonNullDictionaryYieldsSnapshotPairedWithIt) {
    auto dict = fixpp::test_support::make_validation_test_dictionary();
    auto snap = make_dictionary_snapshot(dict);
    ASSERT_NE(snap, nullptr);
    EXPECT_EQ(snap->source(), dict);
}

// ── Seam 4 — alias lifetime and identity on shared_dictionary_view ──────────
//
// The order is load-bearing: the first three assertions require `snap` to
// still be alive, the last two require it dropped.
TEST(DictionarySnapshot, SharedDictionaryViewAliasesRatherThanCopies) {
    auto dict = fixpp::test_support::make_validation_test_dictionary();
    auto snap = make_dictionary_snapshot(dict);
    ASSERT_NE(snap, nullptr);
    auto alias = shared_dictionary_view(snap);
    ASSERT_NE(alias, nullptr);

    // WHILE snap is alive — identity and shared control block:
    EXPECT_EQ(alias.get(), &snap->view())       // points INTO the snapshot, not at a copy
        << "shared_dictionary_view must alias the snapshot's own table_view, not copy it";
    EXPECT_FALSE(alias.owner_before(snap));     // same control block, both directions
    EXPECT_FALSE(snap.owner_before(alias));

    snap.reset();  // AFTER: lifetime
    EXPECT_EQ(alias.use_count(), 1)
        << "the alias must be the SOLE remaining owner of the snapshot's control block";
    // UAF here under a non-owning impl (ASan would catch it); a valid read
    // proves the alias kept the snapshot (and therefore its table_view) alive.
    EXPECT_TRUE(alias->field_valid_for("A", 98))
        << "EncryptMethod(98) is a required field of Logon(A) in the test dictionary";
}

TEST(DictionarySnapshot, SharedDictionaryViewOfNullSnapshotIsNull) {
    std::shared_ptr<const dictionary_snapshot> null_snap;
    auto alias = shared_dictionary_view(null_snap);
    EXPECT_EQ(alias, nullptr);
}

}  // namespace
