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
