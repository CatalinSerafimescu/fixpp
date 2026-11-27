// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/type_shape_pins_test.cpp
//
// W1 (`.specify/688-707-559-wire-headroom-token.md`): compile-time pins on the shapes of the wire
// types W1 changes, and the cells whose evidence is a compile. They sit apart from the
// behavioural cells because a pin that does not compile takes its whole TU down with it.
// Re-derive what each pin rejects by compiling this TU against the headers it pins at the
// branch's merge base (the unfixed header first on the include path).

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/unknown_fields.hpp>
#include <fixpp/wire/writer.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "support/frame_view_factory.hpp"
#include "support/implicit_lifetime_trait.hpp"

namespace {

// bit_castable is a template, so a type std::bit_cast rejects makes it false rather than a hard
// error; int is its known positive.
template <class T>
concept bit_castable = requires { std::bit_cast<T>(std::array<std::byte, sizeof(T)>{}); };
static_assert(bit_castable<int>, "bit_castable's known positive");

}  // namespace

// ── fixpp#559 site 1 (D-559-1): Writer is built from its destination alone ──────────────────
// The Writer keeps no group bookkeeping, so it takes no scratch resource.
static_assert(!std::is_constructible_v<fixpp::wire::Writer, std::span<std::byte>,
                                       std::pmr::memory_resource*>,
              "Writer still takes a scratch memory resource");
static_assert(std::is_nothrow_constructible_v<fixpp::wire::Writer, std::span<std::byte>>,
              "Writer(dst) is the constructor, and it does not throw");

// ── fixpp#707 (D-707-1): group_writer_token cannot be made outside Writer ───────────────────
// Its copy constructor is user-provided and private, so the key is neither trivially copyable
// (which std::bit_cast needs) nor copy-constructible outside Writer. It is implicit-lifetime (its
// defaulted default constructor is trivial), which is harmless only while no non-friend can copy
// it: an implicitly created object is an lvalue, which a by-value parameter has to copy.
namespace w1_pins {
class base_shape_friend;
// The base shape of the key (a defaulted private default constructor, an implicit copy): the
// same-shape known positive for every negative pin below.
class base_shape_key {
    base_shape_key() noexcept = default;
    friend class base_shape_friend;
};
}  // namespace w1_pins

static_assert(bit_castable<w1_pins::base_shape_key>,
              "the base shape is bit_cast-able, so bit_castable can say yes for this shape");
static_assert(!std::is_trivially_copyable_v<fixpp::wire::group_writer_token>,
              "group_writer_token is trivially copyable, so std::bit_cast can forge one");
static_assert(!std::is_copy_constructible_v<fixpp::wire::group_writer_token>,
              "group_writer_token is copy-constructible outside Writer");
static_assert(!bit_castable<fixpp::wire::group_writer_token>,
              "std::bit_cast can make a group_writer_token outside Writer");
#if defined(FIXPP_TEST_IMPLICIT_LIFETIME)
static_assert(FIXPP_TEST_IMPLICIT_LIFETIME(int), "the trait's known positive");
static_assert(FIXPP_TEST_IMPLICIT_LIFETIME(w1_pins::base_shape_key) &&
                  std::is_copy_constructible_v<w1_pins::base_shape_key>,
              "the conjunction holds for the base shape, so the pin below can fail");
static_assert(!(FIXPP_TEST_IMPLICIT_LIFETIME(fixpp::wire::group_writer_token) &&
                std::is_copy_constructible_v<fixpp::wire::group_writer_token>),
              "an implicitly created group_writer_token could be copied into a group_writer");
#endif

namespace {

// Where the compiler offers no implicit-lifetime query, the pins above are left out; this cell
// then skips and names the compiler, so the test log lists the lanes that evaluated them.
TEST(WireTypeShapePins, GroupWriterTokenImplicitLifetimePinIsEvaluatedOnThisLane) {
    namespace il = fixpp::test_support::implicit_lifetime;
    if (!il::kHasTrait) {
        GTEST_SKIP() << "no implicit-lifetime trait on " << il::kCompiler
                     << ": the implicit-lifetime pins are left out on this lane";
    }
#if defined(FIXPP_TEST_IMPLICIT_LIFETIME)
    EXPECT_TRUE(FIXPP_TEST_IMPLICIT_LIFETIME(w1_pins::base_shape_key));
    EXPECT_FALSE(FIXPP_TEST_IMPLICIT_LIFETIME(fixpp::wire::group_writer_token) &&
                 std::is_copy_constructible_v<fixpp::wire::group_writer_token>);
#endif
}

}  // namespace

// ── fixpp#688 (D-688-1): the unknown-field view and its iterator ────────────────────────────
// operator* returns kv by value: the iterator holds its scan state, not a kv, so a reference
// into it would dangle across ++.
static_assert(std::same_as<decltype(*std::declval<fixpp::wire::unknown_fields_view::iterator const&>()),
                           fixpp::wire::unknown_fields_view::kv>,
              "operator* returns kv by value");
// The view is built by MessageView::unknown_fields() alone: the span and pointer constructors of
// the materialised list are gone.
static_assert(!std::is_constructible_v<fixpp::wire::unknown_fields_view,
                                       std::span<fixpp::wire::unknown_fields_view::kv const>,
                                       fixpp::wire::detail::generation_token>,
              "unknown_fields_view is still constructible from a kv span");
static_assert(!std::is_constructible_v<fixpp::wire::unknown_fields_view::iterator,
                                       fixpp::wire::unknown_fields_view::kv const*>,
              "the iterator is still constructible from a kv pointer");
static_assert(std::is_nothrow_default_constructible_v<fixpp::wire::unknown_fields_view>,
              "the known positive: a default view still constructs");

namespace {

std::vector<std::byte> make_raw_frame(std::string const& body) {
    std::string const full =
        "8=FIX.4.4\x01" + ("9=" + std::to_string(body.size()) + "\x01") + body + "10=000\x01";
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

// An iterator taken from a temporary view stays valid while the MessageView lives: it holds its
// own scan state and points at the MessageView's parse storage, not at the view. begin() and
// end() therefore carry no [[clang::lifetimebound]], which would make clang reject this idiom
// under -Werror (-Wdangling).
TEST(WireTypeShapePins, IteratorFromATemporaryViewStaysValidWhileTheMessageViewLives) {
    fixpp::dict::table_view_builder b;
    b.add_valid("D", 35).add_valid("D", 34);
    fixpp::dict::table_view const dict = std::move(b).build();
    auto buf = make_raw_frame(
        "35=D\x01"
        "34=1\x01"
        "5000=first\x01"
        "5001=second\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    fixpp::wire::Parser<fixpp::wire::access_mode::Index> parser{dict};
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());

    auto it = mv->unknown_fields().begin();
    auto const end = mv->unknown_fields().end();
    std::vector<std::pair<std::uint16_t, std::string>> seen;
    for (; !(it == end); ++it) {
        auto const kv = *it;
        seen.emplace_back(kv.tag, std::string{reinterpret_cast<char const*>(kv.data), kv.len});
    }
    std::vector<std::pair<std::uint16_t, std::string>> const expected{{5000, "first"},
                                                                      {5001, "second"}};
    EXPECT_EQ(seen, expected);
}

}  // namespace
