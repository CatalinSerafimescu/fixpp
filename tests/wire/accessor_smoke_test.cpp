// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/accessor_smoke_test.cpp — Round-2 coverage hardening (T055-R2).
// Direct unit tests for small accessor/helper items that are otherwise
// reachable only through integration paths:
//
//   E1) pmr_carry_buffer::capacity() — construct a pmr_carry_buffer with a
//       known capacity and assert capacity() returns that value.
//
//   E2) frame_view::body() — construct a frame_view via the test factory,
//       parse a well-formed frame and assert body() returns the correct
//       sub-span (between the 9= SOH and the '1' of "10=").
//
//   E3) wire::err_group_too_large<void>() — assert the helper returns an
//       unexpected whose .error() == core::error::wire_group_too_large.
//       The real caller (group_writer bookkeeping) is unreachable under the
//       DoS-cap guard; a direct helper unit test is the correct coverage.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fixpp/core/error.hpp>
#include <fixpp/wire/errors.hpp>
#include <fixpp/wire/framer.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/frame_view_factory.hpp"

namespace {

using fixpp::core::error;
using fixpp::wire::frame_view;
using fixpp::wire::Framer;
using fixpp::wire::pmr_carry_buffer;

// ── E1: pmr_carry_buffer::capacity() ─────────────────────────────────────────
// pmr_carry_buffer stores the capacity passed at construction and exposes it
// through capacity(). This is the session-lifetime allocation cap.

TEST(AccessorSmoke, PmrCarryBufferCapacityReturnsSizedValue) {
    std::array<std::byte, 512> arena_storage{};
    std::pmr::monotonic_buffer_resource arena{arena_storage.data(), arena_storage.size()};

    constexpr std::size_t kCap = 256;
    pmr_carry_buffer carry{kCap, &arena};

    EXPECT_EQ(carry.capacity(), kCap)
        << "capacity() must return the value passed to the constructor";
    EXPECT_EQ(carry.size(), 0U) << "freshly constructed carry buffer must be empty";
    EXPECT_TRUE(carry.empty()) << "empty() must return true for a freshly constructed buffer";
}

TEST(AccessorSmoke, PmrCarryBufferCapacityIsIndependentOfSize) {
    std::array<std::byte, 512> arena_storage{};
    std::pmr::monotonic_buffer_resource arena{arena_storage.data(), arena_storage.size()};

    constexpr std::size_t kCap = 100;
    pmr_carry_buffer carry{kCap, &arena};

    // Append some bytes; capacity must not change.
    std::array<std::byte, 10> data{};
    ASSERT_TRUE(carry.append(data)) << "append of 10 bytes into 100-byte buffer must succeed";

    EXPECT_EQ(carry.capacity(), kCap) << "capacity() must stay fixed after appending bytes";
    EXPECT_EQ(carry.size(), 10U) << "size() must reflect the bytes written";
    EXPECT_FALSE(carry.empty()) << "empty() must be false after append";
}

// ── E2: frame_view::body() ────────────────────────────────────────────────────
// frame_view::body() returns the span between the end of the 9= SOH and the
// first byte of "10=" (i.e. the BodyLength-covered region).
// Verified by feeding a well-formed frame through the Framer and checking that
// body().data() and body().size() match what was declared in the 9= field.

// Build "10=NNN\x01" without snprintf.
std::string make_checksum_field(unsigned chk) {
    std::string s = "10=";
    s.push_back(static_cast<char>('0' + ((chk / 100U) % 10U)));
    s.push_back(static_cast<char>('0' + ((chk / 10U) % 10U)));
    s.push_back(static_cast<char>('0' + (chk % 10U)));
    s.push_back('\x01');
    return s;
}

[[nodiscard]] std::vector<std::byte> make_well_formed_frame(std::string_view body_str) {
    std::string body{body_str};
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string pre = "8=FIX.4.4\x01" + nine + body;
    unsigned sum = 0;
    for (unsigned char ch : pre) {
        sum += static_cast<unsigned>(ch);
    }
    std::string full = pre + make_checksum_field(sum % 256U);
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

TEST(AccessorSmoke, FrameViewBodySpanMatchesBodyLengthField) {
    // Build a frame with a known body.
    std::string_view body_str =
        "35=D\x01"
        "34=1\x01"
        "49=SENDER\x01";
    auto raw = make_well_formed_frame(body_str);

    // Feed through the Framer to obtain a real frame_view (not factory-made).
    std::array<std::byte, 512> arena_storage{};
    std::pmr::monotonic_buffer_resource arena{arena_storage.data(), arena_storage.size()};
    Framer framer{};
    pmr_carry_buffer carry{raw.size() + 8, &arena};
    std::array<frame_view, 4> out{};

    auto result = framer.feed(std::span<const std::byte>{raw.data(), raw.size()}, carry, out);

    ASSERT_TRUE(result.has_value()) << "well-formed frame must parse cleanly";
    ASSERT_EQ(result.value().size(), 1U) << "exactly one frame must be emitted";

    frame_view const& fv = result.value()[0];

    // body() must span exactly the declared body_length bytes.
    auto body_span = fv.body();
    EXPECT_EQ(body_span.size(), body_str.size())
        << "body() size must match the 9=BodyLength value (" << body_str.size() << " bytes)";

    // body() data must be a sub-span of the frame bytes.
    auto frame_bytes = fv.bytes();
    EXPECT_GE(reinterpret_cast<uintptr_t>(body_span.data()),
              reinterpret_cast<uintptr_t>(frame_bytes.data()))
        << "body() must point into the frame buffer";
    EXPECT_LE(reinterpret_cast<uintptr_t>(body_span.data() + body_span.size()),
              reinterpret_cast<uintptr_t>(frame_bytes.data() + frame_bytes.size()))
        << "body() must not extend beyond the frame buffer";
}

TEST(AccessorSmoke, FrameViewBodyViaFactoryMatchesBodyLength) {
    // Also verify body() using the test factory for coverage of the factory path.
    std::string_view body_str =
        "35=0\x01"
        "49=A\x01";
    auto raw = make_well_formed_frame(body_str);

    auto fv_result =
        fixpp::wire::test::make_frame_view(std::span<const std::byte>{raw.data(), raw.size()});
    ASSERT_TRUE(fv_result.has_value()) << "factory must succeed on well-formed frame";

    auto body_span = fv_result->body();
    EXPECT_EQ(body_span.size(), body_str.size())
        << "factory-produced frame_view body() must match declared BodyLength";
}

// ── E3: wire::err_group_too_large<void>() helper ─────────────────────────────
// The err_group_too_large helper (errors.hpp ~51-53) is a thin wrapper that
// constructs the unexpected side of expected_t<void>. The real caller
// (group bookkeeping inside OffsetTable) is guarded by a DoS cap and is
// unreachable in normal test payloads. A direct helper unit test is the
// correct coverage: it asserts the helper returns the right error code.

TEST(AccessorSmoke, ErrGroupTooLargeHelperReturnsCorrectErrorCode) {
    auto r = fixpp::wire::err_group_too_large<void>();

    ASSERT_FALSE(r.has_value()) << "err_group_too_large() must return an unexpected (no value)";
    EXPECT_EQ(r.error(), error::wire_group_too_large)
        << "err_group_too_large() must carry wire_group_too_large (slot 36)";
}

TEST(AccessorSmoke, ErrGroupTooLargeHelperTemplateVariantInt) {
    // Also test the non-void variant to exercise the template parameter path.
    auto r = fixpp::wire::err_group_too_large<int>();

    ASSERT_FALSE(r.has_value()) << "err_group_too_large<int>() must return an unexpected";
    EXPECT_EQ(r.error(), error::wire_group_too_large)
        << "error code must be wire_group_too_large regardless of value type";
}

}  // namespace
