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
// Seam 4: lifetime and IDENTITY, tested on shared_dictionary_view — the
// production helper — not on std::shared_ptr directly. It pins identity and
// shared ownership, not just validity, so a helper that COPIES instead of
// sharing cannot satisfy it. The helper shares the snapshot's TABLE owner
// rather than aliasing the snapshot (fixpp#495 D-4,
// `.specify/495-493-486-dict-reify-copy.md` §6).
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
static_assert(
    !std::is_constructible_v<dictionary_snapshot, std::shared_ptr<const Dictionary>, table_view>);

// A3 — view() must expose const&, never a mutable reference or a copy.
static_assert(
    std::same_as<decltype(std::declval<dictionary_snapshot const&>().view()), table_view const&>);

// A4 — the factory must hand back a const snapshot, never a mutable one.
static_assert(std::same_as<
              decltype(make_dictionary_snapshot(std::declval<std::shared_ptr<const Dictionary>>())),
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

// ── Seam 4 — shared_dictionary_view shares the snapshot's TABLE OWNER ────────
//
// fixpp#495 D-4 (`.specify/495-493-486-dict-reify-copy.md` §6, T-19(a)): the
// snapshot owns its table in its own control block, so the helper hands out that
// owner — identity with the snapshot's table, not a copy — and a held view keeps
// the TABLE alive but neither the snapshot nor its Dictionary. The order is
// load-bearing: the identity assertion needs `snap` alive, the rest need it gone.
TEST(DictionarySnapshot, SharedDictionaryViewSharesTheTableOwner) {
    auto dict = fixpp::test_support::make_validation_test_dictionary();
    long const dict_refs_before_snapshot = dict.use_count();
    auto snap = make_dictionary_snapshot(dict);
    ASSERT_NE(snap, nullptr);
    auto owner = shared_dictionary_view(snap);
    ASSERT_NE(owner, nullptr);

    // WHILE snap is alive — identity: the snapshot's own table, not a copy.
    EXPECT_EQ(owner.get(), &snap->view())
        << "shared_dictionary_view must share the snapshot's own table_view, not copy it";

    snap.reset();  // AFTER: lifetime
    EXPECT_EQ(owner.use_count(), 1)
        << "the held view must be the SOLE remaining owner of the table";
    EXPECT_EQ(dict.use_count(), dict_refs_before_snapshot)
        << "a held view must not keep the snapshot, and so its Dictionary, alive (D-4)";
    // UAF here under a non-owning impl (ASan would catch it); a valid read
    // proves the owner kept the table alive.
    EXPECT_TRUE(owner->field_valid_for("A", 98))
        << "EncryptMethod(98) is a required field of Logon(A) in the test dictionary";
}

TEST(DictionarySnapshot, SharedDictionaryViewOfNullSnapshotIsNull) {
    std::shared_ptr<const dictionary_snapshot> null_snap;
    auto owner = shared_dictionary_view(null_snap);
    EXPECT_EQ(owner, nullptr);
}

}  // namespace
