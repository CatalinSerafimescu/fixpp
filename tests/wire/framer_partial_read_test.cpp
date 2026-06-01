// SPDX-License-Identifier: AGPL-3.0-or-later

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <fixpp/wire/framer.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using fixpp::wire::frame_view;
using fixpp::wire::Framer;
using fixpp::wire::pmr_carry_buffer;

constexpr char soh = '\x01';

[[nodiscard]] std::vector<std::byte> to_bytes(std::string_view text) {
    std::vector<std::byte> out;
    out.reserve(text.size());
    for (char ch : text) {
        out.push_back(static_cast<std::byte>(ch));
    }
    return out;
}

void append_checksum_field(std::string& frame, unsigned checksum) {
    frame.push_back('1');
    frame.push_back('0');
    frame.push_back('=');
    frame.push_back(static_cast<char>('0' + ((checksum / 100U) % 10U)));
    frame.push_back(static_cast<char>('0' + ((checksum / 10U) % 10U)));
    frame.push_back(static_cast<char>('0' + (checksum % 10U)));
    frame.push_back(soh);
}

[[nodiscard]] std::vector<std::byte> make_frame(std::string_view body) {
    std::string frame = "8=FIX.4.4";
    frame.push_back(soh);
    frame.append("9=");
    frame.append(std::to_string(body.size()));
    frame.push_back(soh);
    frame.append(body);

    unsigned checksum = 0;
    for (unsigned char ch : frame) {
        checksum += static_cast<unsigned>(ch);
    }
    checksum %= 256U;

    append_checksum_field(frame, checksum);
    return to_bytes(frame);
}

[[nodiscard]] std::vector<std::byte> concat(std::span<const std::byte> a,
                                            std::span<const std::byte> b) {
    std::vector<std::byte> out;
    out.reserve(a.size() + b.size());
    out.insert(out.end(), a.begin(), a.end());
    out.insert(out.end(), b.begin(), b.end());
    return out;
}

[[nodiscard]] std::vector<std::byte> copy_bytes(frame_view const& frame) {
    std::vector<std::byte> out;
    std::span<const std::byte> bytes = frame.bytes();
    out.insert(out.end(), bytes.begin(), bytes.end());
    return out;
}

[[nodiscard]] bool chunking_requires_partial(std::size_t total_size, std::size_t first_frame_size,
                                             std::size_t chunk_size) {
    for (std::size_t offset = chunk_size; offset < total_size; offset += chunk_size) {
        if (offset != first_frame_size) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST(WireFramerPartialRead, ReassemblesKnownPipelineAcrossEveryChunkSize) {
    std::vector<std::byte> const first =
        make_frame(std::string{"35=0\x01"
                               "49=SENDER\x01"
                               "56=TARGET\x01"});
    std::vector<std::byte> const second =
        make_frame(std::string{"35=1\x01"
                               "49=ALPHA\x01"
                               "56=BETA\x01"});
    std::vector<std::byte> const pipeline = concat(first, second);
    std::vector<std::vector<std::byte>> const expected{first, second};

    std::array<std::byte, 1024> arena_storage{};
    std::pmr::monotonic_buffer_resource arena{arena_storage.data(), arena_storage.size()};

    for (std::size_t chunk_size = 1; chunk_size <= pipeline.size(); ++chunk_size) {
        std::array<std::byte, 1024> arena_storage{};
        std::pmr::monotonic_buffer_resource arena{arena_storage.data(), arena_storage.size()};
        Framer framer{};
        pmr_carry_buffer carry{pipeline.size(), &arena};
        std::vector<std::vector<std::byte>> observed;
        bool saw_partial = false;

        for (std::size_t offset = 0; offset < pipeline.size(); offset += chunk_size) {
            std::size_t const count = std::min(chunk_size, pipeline.size() - offset);
            std::array<frame_view, 4> out{};
            auto result = framer.feed(std::span<const std::byte>{pipeline}.subspan(offset, count),
                                      carry, out);

            ASSERT_TRUE(result.has_value()) << static_cast<int>(result.error());
            for (frame_view const& frame : result.value()) {
                observed.push_back(copy_bytes(frame));
            }

            if (count != pipeline.size() && framer.pending_bytes() != 0U) {
                saw_partial = true;
            }
        }

        EXPECT_EQ(observed, expected) << "chunk_size=" << chunk_size;
        EXPECT_EQ(framer.pending_bytes(), 0U) << "chunk_size=" << chunk_size;
        if (chunking_requires_partial(pipeline.size(), first.size(), chunk_size)) {
            EXPECT_TRUE(saw_partial) << "chunk_size=" << chunk_size;
        }
    }
}
