// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/concurrent_readers_test.cpp — seam #6 — AC-T1 / AC-T2 / NFR-002-3

#include <gtest/gtest.h>

#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/xml_loader.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory_resource>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::size_t k4MiB = 4UZ * 1024UZ * 1024UZ;
constexpr int         kNumThreads  = 8;
constexpr int         kIterations  = 10'000;

}  // namespace

// ConcurrentReaders.NReadersStayConsistent
// Real verification: run the test binary under TSan. The test is structural:
// it exercises all const read paths from N threads simultaneously so TSan can
// detect any data race on the metadata-handle internals.
TEST(ConcurrentReaders, NReadersStayConsistent) {
    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX44.xml";

    std::array<std::byte, k4MiB> buffer{};
    std::pmr::monotonic_buffer_resource mr{buffer.data(), buffer.size()};
    auto d = fixpp::dict::XmlLoader{}.load(path, &mr);

    // Capture a stable reference value from the main thread before spawning.
    auto const expected_clordid_tag = d.field_by_name("ClOrdID");
    auto const expected_msg_count   = d.messages().size();

    std::atomic<int> failure_count{0};

    std::vector<std::thread> threads;
    threads.reserve(kNumThreads);

    for (int t = 0; t < kNumThreads; ++t) {
        threads.emplace_back([&d,
                              &failure_count,
                              expected_clordid_tag,
                              expected_msg_count]() {
            for (int i = 0; i < kIterations; ++i) {
                // Cycle tags 1..600 across iterations.
                auto const tag = static_cast<std::uint16_t>((i % 600) + 1);

                // AC-T2: concurrent field_ref read — must not crash or race.
                (void)d.field_ref("D", tag);

                // AC-T1 + AC-T2: field_by_name must return the same tag every
                // iteration across all threads.
                auto const got = d.field_by_name("ClOrdID");
                if (got != expected_clordid_tag) {
                    failure_count.fetch_add(1, std::memory_order_relaxed);
                }

                // AC-T2: messages() span must be stable.
                auto const msgs = d.messages();
                if (msgs.size() != expected_msg_count) {
                    failure_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    ASSERT_EQ(failure_count.load(), 0)
        << "Concurrent readers observed inconsistent Dictionary state.";
}

// ConcurrentReaders.PublicAccessorsNoexcept
// AC-T1: every public accessor is const and noexcept (compile-time check).
TEST(ConcurrentReaders, PublicAccessorsNoexcept) {
    using D = fixpp::dict::Dictionary const&;

    static_assert(noexcept(std::declval<D>().field_ref({}, 0)));
    static_assert(noexcept(std::declval<D>().field({}, 0)));
    static_assert(noexcept(std::declval<D>().field_by_name({})));
    static_assert(noexcept(std::declval<D>().component({})));
    static_assert(noexcept(std::declval<D>().group(0)));
    static_assert(noexcept(std::declval<D>().messages()));
    static_assert(noexcept(std::declval<D>().which_session_version()));
    static_assert(noexcept(std::declval<D>().required_fields({})));
    static_assert(noexcept(std::declval<D>().field_valid_for({}, 0)));
    static_assert(noexcept(std::declval<D>().group_first_field(0)));
    static_assert(noexcept(std::declval<D>().length_pair_data_tag(0)));

    SUCCEED();
}

// ConcurrentReaders.MessagesSpanStable
// AC-T2: the span returned by messages() aliases metadata-handle storage and
// remains valid for the Dictionary's lifetime; a reader thread can iterate the
// span captured from the main thread without the Dictionary moving or being
// destroyed under it.
TEST(ConcurrentReaders, MessagesSpanStable) {
    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX44.xml";

    std::array<std::byte, k4MiB> buffer{};
    std::pmr::monotonic_buffer_resource mr{buffer.data(), buffer.size()};
    auto d = fixpp::dict::XmlLoader{}.load(path, &mr);

    // Capture the span in the main thread; d must outlive the reader thread.
    auto const msgs = d.messages();
    ASSERT_GT(msgs.size(), 0u) << "FIX44.xml must declare at least one message.";

    std::string result;

    std::thread reader{[&msgs, &result]() {
        for (std::size_t i = 0; i < msgs.size(); ++i) {
            result += msgs[i].msg_type;
        }
    }};

    reader.join();

    ASSERT_FALSE(result.empty())
        << "Reader thread failed to read any MsgType strings from messages() span.";
}
