// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/table_view_seal_compile_test.cpp — fixpp#456, design §6 seam 1.
//
// THE RED. `fixpp::dict::table_view` documented itself as immutable after
// construction; until fixpp#456 it enforced none of it, and #456's own witness
// mutated a default-constructed view that never came from a `Dictionary`. This TU
// asserts, at compile time, that the sixteen population members and both
// assignment operators are unreachable through the view and reachable (the
// mutators) through `table_view_builder`.
//
// ⚠️ THIS IS DELIBERATELY NOT A MUST-FAIL BUILD TARGET (`WILL_FAIL TRUE` over
// `cmake --build --target …`), and §6 rejects that form with reasons: such a target
// passes for ANY compile error — a typo, a missing include, a renamed header — so it
// is the canonical instrument that cannot report anything but clean. This repo has
// already written that argument down once, in tests/consumer/CMakeLists.txt's rule 2,
// which inverted its own must-not-resolve cells so that they must COMPILE. Do not
// reintroduce the must-fail form here. The inverted precedent is
// tests/sync/test_consumer_contract_compile.cpp.
//
// ⚠️ EVERY NEGATIVE IS PAIRED WITH THE SAME CONCEPT INSTANTIATED ON THE BUILDER, in
// this TU, on the line below it. That pairing is not decoration — it is the whole
// reason a negative means anything. A `requires`-expression is false for a MISSPELLED
// member exactly as it is for a PRIVATE one, so `!can_X<table_view>` alone would stay
// green forever if the mutator were renamed or if the concept's argument list drifted.
// An unpaired negative is a probe with no proof that it is not blind. Keep them
// adjacent: sixteen negatives in one block followed by sixteen positives in another
// hides a positive that instantiates the wrong concept twice.
//
// ⚠️ WHAT THESE ASSERTIONS ARE NOT: a completeness proof for the mutation boundary.
// All sixteen probes are NAME-KEYED, so they cannot see a newly added `clear()`, a
// differently spelled mutator, a public data member or a mutable-returning accessor.
// There is no automated completeness check over `table_view`'s public surface, by
// decision (§6 seam 1 declines an AST gate as disproportionate); the standing
// obligation is that the public surface is READ at each change, with the class-scoped
// access-label census pasted into the decision record.
//
// Nor do they see the channels the seal never closed, because none of those is a
// POPULATION mutator: move-construction from a non-`const` view (`table_view(
// table_view&&)` is public and load-bearing, §5a), and `const_cast` through a
// span-returning accessor (`required_fields`, `group_member_tags`,
// `group_required_members`) reaching non-`const` backing storage — defined
// behaviour on a `const` view as much as a non-`const` one, since the elements
// behind the span are allocated by the member vectors and are not themselves
// `const` objects. Those two sit OUTSIDE this TU's subject rather than being gaps
// in its coverage of it — §5b and `B-456-1` carry them — and the remaining §5b
// channel, destroy-and-reconstruct at the same address, is pinned at runtime by
// `DictHooksCustomPair.ABundleKeepsItsNullPairCallbackAcrossAReSeatThatAddsThePair`,
// which pins the pair-callback latch specifically, not the view as a whole —
// every other `dict_hooks` callback dereferences the stored address live and does
// follow the re-seat.
//
// Re-derivation recipe for the structural half (§6 seam 2) — the counts are
// deliberately NOT written here, because nothing re-runs a comment:
//   grep -nE '^[[:space:]]*friend[[:space:]]+class[[:space:]]+(table_view|table_view_builder);' \
//        include/fixpp/dict/table_view.hpp
//   awk '/^class table_view \{/{f=1} f&&/^(public|private|protected):/{print NR": "$0} \
//        f&&/^\};/{exit}' include/fixpp/dict/table_view.hpp
//
// Mutation procedure — PROVE THESE PROBES CAN REPORT NON-ZERO before trusting a green
// build, because a green build is also the signature of sixteen blind probes:
//   (a) move one mutator back above the `private:` in table_view.hpp — that mutator's
//       NEGATIVE must fire, naming it;
//   (b) misspell one member inside one concept's requires-expression — that concept's
//       BUILDER-SIDE POSITIVE must fire, and its negative must stay green, which is
//       precisely the blindness the pairing exists to catch.

#include <gtest/gtest.h>

#include <cstdint>
#include <fixpp/dict/table_view.hpp>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

using fixpp::dict::field_type;
using fixpp::dict::table_view;
using fixpp::dict::table_view_builder;

// ── one concept per mutator, all sixteen ────────────────────────────────────
// Spelled against a non-const lvalue, which is what a caller holding a view has.
// The four context-scoped mutators take a span, so it is a second requires-parameter
// rather than a braced temporary (a `std::span` from an initializer list would bind
// to a dangling array).

template <class T>
concept can_add_valid_tag = requires(T& t) {
    t.add_valid_tag(std::string_view{"D"}, std::uint16_t{11});
};

template <class T>
concept can_add_required_tag = requires(T& t) {
    t.add_required_tag(std::string_view{"D"}, std::uint16_t{11});
};

template <class T>
concept can_set_field_type = requires(T& t) { t.set_field_type(std::uint16_t{11}, field_type::Int); };

template <class T>
concept can_add_group_member = requires(T& t) {
    t.add_group_member(std::uint16_t{73}, std::uint16_t{37});
};

template <class T>
concept can_add_group_required_member = requires(T& t) {
    t.add_group_required_member(std::uint16_t{73}, std::uint16_t{37});
};

template <class T>
concept can_add_valid = requires(T& t) { t.add_valid(std::string_view{"D"}, std::uint16_t{11}); };

template <class T>
concept can_add_required = requires(T& t) {
    t.add_required(std::string_view{"D"}, std::uint16_t{11});
};

template <class T>
concept can_set_type = requires(T& t) { t.set_type(std::uint16_t{11}, field_type::Int); };

template <class T>
concept can_set_group_first = requires(T& t) {
    t.set_group_first(std::uint16_t{73}, std::uint16_t{37});
};

template <class T>
concept can_add_enum = requires(T& t) { t.add_enum(std::uint16_t{54}, std::string_view{"1"}); };

template <class T>
concept can_set_multi_value = requires(T& t) { t.set_multi_value(std::uint16_t{54}, true); };

template <class T>
concept can_add_group_member_ctx = requires(T& t, std::span<std::uint16_t const> path) {
    t.add_group_member_ctx(std::string_view{"D"}, path, std::uint16_t{73}, std::uint16_t{37});
};

template <class T>
concept can_add_group_required_member_ctx = requires(T& t, std::span<std::uint16_t const> path) {
    t.add_group_required_member_ctx(std::string_view{"D"}, path, std::uint16_t{73},
                                    std::uint16_t{37});
};

template <class T>
concept can_set_group_first_ctx = requires(T& t, std::span<std::uint16_t const> path) {
    t.set_group_first_ctx(std::string_view{"D"}, path, std::uint16_t{73}, std::uint16_t{37});
};

template <class T>
concept can_add_fixt_framing_tag = requires(T& t) {
    t.add_fixt_framing_tag(std::uint16_t{1128}, field_type::String);
};

template <class T>
concept can_set_length_pair_data_tag = requires(T& t) {
    t.set_length_pair_data_tag(std::uint16_t{5001}, std::uint16_t{5002});
};

// ── the seal: sixteen NEGATIVE/POSITIVE pairs, adjacent ─────────────────────
// Read each pair together. The negative says that one named mutator is not
// callable on a `table_view&`; the positive directly beneath says the concept is
// capable of matching at all.

static_assert(!can_add_valid_tag<table_view>, "SEAL LEAK: add_valid_tag");
static_assert(can_add_valid_tag<table_view_builder>, "PROBE BLIND: can_add_valid_tag");

static_assert(!can_add_required_tag<table_view>, "SEAL LEAK: add_required_tag");
static_assert(can_add_required_tag<table_view_builder>, "PROBE BLIND: can_add_required_tag");

static_assert(!can_set_field_type<table_view>, "SEAL LEAK: set_field_type");
static_assert(can_set_field_type<table_view_builder>, "PROBE BLIND: can_set_field_type");

static_assert(!can_add_group_member<table_view>, "SEAL LEAK: add_group_member");
static_assert(can_add_group_member<table_view_builder>, "PROBE BLIND: can_add_group_member");

static_assert(!can_add_group_required_member<table_view>, "SEAL LEAK: add_group_required_member");
static_assert(can_add_group_required_member<table_view_builder>,
              "PROBE BLIND: can_add_group_required_member");

static_assert(!can_add_valid<table_view>, "SEAL LEAK: add_valid");
static_assert(can_add_valid<table_view_builder>, "PROBE BLIND: can_add_valid");

static_assert(!can_add_required<table_view>, "SEAL LEAK: add_required");
static_assert(can_add_required<table_view_builder>, "PROBE BLIND: can_add_required");

static_assert(!can_set_type<table_view>, "SEAL LEAK: set_type");
static_assert(can_set_type<table_view_builder>, "PROBE BLIND: can_set_type");

static_assert(!can_set_group_first<table_view>, "SEAL LEAK: set_group_first");
static_assert(can_set_group_first<table_view_builder>, "PROBE BLIND: can_set_group_first");

static_assert(!can_add_enum<table_view>, "SEAL LEAK: add_enum");
static_assert(can_add_enum<table_view_builder>, "PROBE BLIND: can_add_enum");

static_assert(!can_set_multi_value<table_view>, "SEAL LEAK: set_multi_value");
static_assert(can_set_multi_value<table_view_builder>, "PROBE BLIND: can_set_multi_value");

static_assert(!can_add_group_member_ctx<table_view>, "SEAL LEAK: add_group_member_ctx");
static_assert(can_add_group_member_ctx<table_view_builder>,
              "PROBE BLIND: can_add_group_member_ctx");

static_assert(!can_add_group_required_member_ctx<table_view>,
              "SEAL LEAK: add_group_required_member_ctx");
static_assert(can_add_group_required_member_ctx<table_view_builder>,
              "PROBE BLIND: can_add_group_required_member_ctx");

static_assert(!can_set_group_first_ctx<table_view>, "SEAL LEAK: set_group_first_ctx");
static_assert(can_set_group_first_ctx<table_view_builder>, "PROBE BLIND: can_set_group_first_ctx");

static_assert(!can_add_fixt_framing_tag<table_view>, "SEAL LEAK: add_fixt_framing_tag");
static_assert(can_add_fixt_framing_tag<table_view_builder>,
              "PROBE BLIND: can_add_fixt_framing_tag");

static_assert(!can_set_length_pair_data_tag<table_view>, "SEAL LEAK: set_length_pair_data_tag");
static_assert(can_set_length_pair_data_tag<table_view_builder>,
              "PROBE BLIND: can_set_length_pair_data_tag");

// ── assignment: the other half of the seal (design §3.2) ────────────────────
// A caller who can write `published = other;` has not been sealed — copy-assignment
// carried the `has_nonstandard_pair()` bit onto a published view, which is #456's own
// defect wearing a different spelling.
static_assert(!std::is_copy_assignable_v<table_view>, "SEAL LEAK: copy-assign");
static_assert(!std::is_move_assignable_v<table_view>, "SEAL LEAK: move-assign");

// ── what must SURVIVE (design §1a(iv), and RC-1) ────────────────────────────
// `wire::dictionary_driven_validator` holds its `table_view` BY VALUE behind the
// frozen SC-007 "no virtual edge" design point, so every validating session
// copy-constructs one. Move construction is how `build()` returns, how
// `optional::emplace` seats a view, and how that by-value copy is moved into place.
//
// ⚠️ These assert that the two operations SURVIVED the seal — nothing about their
// exception specifications. The move constructor's specification is INFERRED from
// the members, and on the Microsoft STL it infers to potentially-throwing because
// `unordered_map`/`unordered_set` move construction is not `noexcept` there. That is
// a platform fact, recorded as `L-456-2`; it is deliberately NOT an assertion,
// because an assertion here could only be satisfied by re-spelling the promise the
// type stopped making. Asserting `is_nothrow_move_constructible_v` in this block is
// what made all three Tier-2 legs red, and it duplicated a claim that already had a
// home.
static_assert(std::is_copy_constructible_v<table_view>,
              "SEAL OVERREACH: copy construction is load-bearing (SC-007)");
static_assert(std::is_move_constructible_v<table_view>,
              "SEAL OVERREACH: move construction is how build() returns");

// The builder is itself a plain stack-local scaffold: default-constructible, and
// consumed by an &&-qualified build(). A named builder cannot be built from an
// lvalue — the friction is the design (§3.3).
// ⚠️ Both spelled through a CONCEPT, not through a bare `requires(...)` on the
// concrete type. A requires-expression only absorbs an ill-formed expression while it
// is DEPENDENT; over a concrete type clang rejects the ref-qualifier mismatch as a
// hard error and the TU does not compile at all. That applies to every probe in this
// file, which is why all sixteen above are concepts too.
template <class T>
concept can_build_from_lvalue = requires(T& b) { b.build(); };
template <class T>
concept can_build_from_rvalue = requires(T& b) { std::move(b).build(); };

static_assert(std::is_default_constructible_v<table_view_builder>);
static_assert(!can_build_from_lvalue<table_view_builder>,
              "build() must be &&-qualified: a named builder is consumed visibly");
static_assert(can_build_from_rvalue<table_view_builder>,
              "PROBE BLIND: can_build_from_lvalue's negative is unproven without this");

}  // namespace

// gtest needs a case to run, or the binary reports no tests and `ctest` scores an
// empty corpus. The assertions above have already fired by the time this executes.
TEST(TableViewSeal, TheSealHoldsAtCompileTime) {
    SUCCEED() << "every assertion in this TU is a static_assert; reaching here is the pass";
}
