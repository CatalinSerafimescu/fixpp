// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/capi/dict495_pinned_table_lifetime_test.cpp
//
// fixpp#495 (`.specify/495-493-486-dict-reify-copy.md` §3.3, §6, §10 T-11): a reify
// handle and a C clone minted from an owned-route view pin the snapshot's TABLE,
// which is self-contained, and nothing of the Dictionary or its load arena. The
// Dictionary is loaded into a HEAP-allocated arena (so a late deallocation into it
// is a heap-use-after-free, not a stack-use-after-scope). The main thread destroys
// the source view, frame and parse arena, then the owner, the Dictionary and the
// snapshot, and LAST the load arena; only then does a second thread read NoLegs
// through the handle and the clone and drop both — the last references.
//
// Run under ASan and TSan. RED arm (ASan): the alias mutant — in
// `shared_dictionary_view`, take the snapshot's table pointer and return the
// aliasing construction over the moved snapshot — makes the second thread's drop
// run ~Dictionary into the destroyed arena.
//
// Standalone (`[const §VII.8]`): a second thread and sanitizer arms.

#include <gtest/gtest.h>

#include <cstddef>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/dictionary_snapshot.hpp>
#include <fixpp/dict/reify.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/parser.hpp>
#include <latch>
#include <memory>
#include <memory_resource>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "capi_internal.hpp"
#include "fix/c_api/message.h"
#include "support/fix44_group_frame_bodies.hpp"
#include "support/frame_view_factory.hpp"

namespace {

using fixpp::wire::access_mode;
using MV = fixpp::wire::MessageView<access_mode::Index>;

constexpr fixpp::dict::version_profile kProfileV44{
    .session = fixpp::dict::session_version::v44,
    .default_appl = fixpp::dict::application_version::v44,
    .has_per_message_override = false,
    ._reserved = 0};
constexpr std::uint16_t kNoLegs = 555;

TEST(Dict495PinnedTableLifetime, HandleAndCloneOutliveTheDictionaryAndItsLoadArena) {
    // The Dictionary's load arena, and its buffer, on the heap.
    constexpr std::size_t kLoadArenaBytes = std::size_t{4} * 1024U * 1024U;
    auto load_buf = std::make_unique<std::byte[]>(kLoadArenaBytes);
    auto load_arena = std::make_unique<std::pmr::monotonic_buffer_resource>(
        load_buf.get(), kLoadArenaBytes, std::pmr::new_delete_resource());
    auto dict = std::make_shared<const fixpp::dict::Dictionary>(fixpp::dict::XmlLoader{}.load(
        std::string(FIXPP_DICT_DATA_DIR) + "/FIX44.xml", load_arena.get()));
    auto snap = fixpp::dict::make_dictionary_snapshot(dict);
    ASSERT_NE(snap, nullptr);
    auto sp = fixpp::dict::shared_dictionary_view(snap);
    ASSERT_NE(sp, nullptr);

    // Owned-route parse of a NoLegs frame, in its own heap parse arena.
    auto frame = std::make_unique<std::vector<std::byte>>(
        fixpp_test_support::make_execution_report_frame(
            fixpp_test_support::execution_report_two_legs_trailing_suffix(), /*seq=*/4, "S",
            "T"));
    auto parse_arena = std::make_unique<std::pmr::monotonic_buffer_resource>();
    auto fv = fixpp::wire::test::make_frame_view(*frame);
    ASSERT_TRUE(fv.has_value());
    std::optional<MV> src;
    {
        fixpp::wire::Parser<access_mode::Index> parser{fixpp::wire::detail::owned_route_key{},
                                                       sp};
        auto parsed = parser.parse(*fv, parse_arena.get());
        ASSERT_TRUE(parsed.has_value());
        src.emplace(std::move(*parsed));
    }

    // The handle's arena lives to the end of the test.
    std::pmr::monotonic_buffer_resource handle_arena;
    auto handle = fixpp::dict::reify(*src, kProfileV44, &handle_arena);
    ASSERT_TRUE(handle.has_value());
    ASSERT_EQ(handle->view().hooks().opaque_dict(), sp.get());

    fixpp_msg inbound{};
    inbound.view = &*src;
    fixpp_msg_t* clone = nullptr;
    ASSERT_EQ(fixpp_msg_clone(reinterpret_cast<const fixpp_msg_t*>(&inbound), &clone),
              FIXPP_ERR_OK);
    ASSERT_NE(clone, nullptr);

    std::latch released{1};
    bool handle_read = false;
    bool clone_read = false;
    std::thread reader{[&, h = std::move(*handle), c = clone]() mutable {
        released.wait();
        handle_read = h.view().offsets().group_slices(kNoLegs).size() == 2U;
        const fixpp_group_t* grp = nullptr;
        std::size_t count = 0;
        clone_read = fixpp_msg_get_group(c, kNoLegs, &grp, &count) == FIXPP_ERR_OK && count == 2U;
        (void)fixpp_msg_destroy(c);
        // `h` — the last reference to the table — is dropped with this lambda.
    }};

    // Main thread: tear everything else down, the load arena last.
    // (`handle` is moved-from: the handle itself lives in `reader`.)
    src.reset();
    frame.reset();
    parse_arena.reset();
    sp.reset();
    dict.reset();
    snap.reset();
    load_arena.reset();
    load_buf.reset();
    released.count_down();
    reader.join();

    EXPECT_TRUE(handle_read) << "the handle reads NoLegs through the table it pins";
    EXPECT_TRUE(clone_read) << "the clone reads NoLegs through the table it pins";
}

}  // namespace
