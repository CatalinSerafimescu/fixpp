#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/frame_view_factory.hpp
// TEST-ONLY friend-of-frame_view factory (analyze finding I1). Mints a
// verified fixpp::wire::frame_view over a caller-owned buffer by computing
// the 9=BodyLength SOH boundary and the 10=CheckSum boundary, so US1 parser
// tests go red->green WITHOUT the US3 Framer algorithm (src/wire/framer.cpp).
// NOT production code: lives under tests/support, never linked into fixpp_wire.
//
// frame_view_access is the friend struct declared in <fixpp/wire/framer.hpp>
// (`friend struct frame_view_access;`); it is the single seam that can call
// frame_view's protected ctor from test code.

#include <cstddef>
#include <cstring>
#include <fixpp/core/error.hpp>
#include <fixpp/wire/framer.hpp>
#include <fixpp/wire/view.hpp>
#include <span>
#include <string_view>

namespace fixpp::wire {

// Friend of frame_view (see framer.hpp). Bridges the protected ctor for
// test producers only.
struct frame_view_access {
    static frame_view make(std::byte const* frame, std::size_t frame_len, std::size_t body_off,
                           std::size_t body_len, detail::generation_token gen) noexcept {
        return frame_view{frame, frame_len, body_off, body_len, gen};
    }
};

namespace test {

// Computes the BodyLength/CheckSum boundaries of a single well-formed FIX
// frame and returns a frame_view aliasing `buf`. Returns wire_invalid_*
// codes if the structural markers are absent (tests assert on these too).
[[nodiscard]] inline core::expected_t<frame_view> make_frame_view(
    std::span<const std::byte> buf, detail::generation_token gen = {}) noexcept {
    constexpr char SOH = '\x01';
    std::string_view s{reinterpret_cast<char const*>(buf.data()), buf.size()};

    // Locate "9=" (BodyLength). It is the second standard-header field, but
    // search defensively for the "<SOH>9=" / leading "9=" form.
    std::size_t p9 = s.starts_with("9=") ? 0
                                         : s.find(
                                               "\x01"
                                               "9=");
    if (p9 == std::string_view::npos) {
        return core::expected_t<frame_view>{std::unexpect, core::error::wire_invalid_body_length};
    }
    if (s[p9] == SOH) {
        ++p9;  // step over the SOH onto '9'
    }
    std::size_t soh_after_9 = s.find(SOH, p9);
    if (soh_after_9 == std::string_view::npos) {
        return core::expected_t<frame_view>{std::unexpect, core::error::wire_invalid_body_length};
    }
    std::size_t body_off = soh_after_9 + 1;  // first body byte

    // Locate "<SOH>10=" (CheckSum). body() spans up to the '1' of "10=".
    std::size_t p10 = s.find(
        "\x01"
        "10=",
        body_off);
    if (p10 == std::string_view::npos) {
        return core::expected_t<frame_view>{std::unexpect, core::error::wire_checksum_mismatch};
    }
    std::size_t checksum_digit = p10 + 1;  // the '1' of "10="
    std::size_t body_len = checksum_digit - body_off;

    return frame_view_access::make(buf.data(), buf.size(), body_off, body_len, gen);
}

}  // namespace test
}  // namespace fixpp::wire
