// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/packaging/clean_env/clean_env_consumer.cpp — 084 T041 (SC-016).
//
// Deliberately minimal. This tier's assertion is about CONFIGURE and LINK
// resolution off the producing host, not about library behaviour — the
// behavioural coverage lives in tests/consumer/ and the real-client tier. Doing
// more here would only add ways to fail for reasons unrelated to SC-016.
//
// It does need to pull real symbols out of the archives, though: a TU that only
// #includes headers would link even if every archive were missing, and this
// witness would then pass on a package that ships no libraries at all.

#include <fixpp/dict/dictionary.hpp>
#include <fixpp/wire/framer.hpp>

#include <cstdio>
#include <memory_resource>
#include <span>

int main() {
    std::pmr::monotonic_buffer_resource arena{1 << 12};

    // Force a symbol out of libfixpp_wire.a rather than merely compiling against
    // its header.
    fixpp::wire::pmr_carry_buffer carry{64, &arena};
    fixpp::wire::Framer framer{};
    fixpp::wire::frame_view frames[1]{};

    const auto framed = framer.feed(std::span<const std::byte>{}, carry,
                                    std::span<fixpp::wire::frame_view>{frames, 1});
    if (!framed.has_value()) {
        std::fprintf(stderr, "FAIL: Framer::feed on an empty span returned an error\n");
        return 1;
    }

    std::printf("PASS: configured, linked and ran against the installed package "
                "with no producer toolchain\n");
    return 0;
}
