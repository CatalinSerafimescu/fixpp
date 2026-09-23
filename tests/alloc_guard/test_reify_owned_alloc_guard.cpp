// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/alloc_guard/test_reify_owned_alloc_guard.cpp
//
// fixpp#495 (`.specify/495-493-486-dict-reify-copy.md` §2.5, §10 T-14): on the
// OWNED route, `dict::reify` draws every allocation from the caller's `mr` — the
// handle's impl (D-1c), the frame bytes, the re-parse's OffsetTable — and shares
// the source's membership table by reference count instead of copying it onto the
// global heap. The window is `dict::reify(view, profile, &mr)`, one field read, and
// the handle's destruction, with the owner `sp` held throughout.
//
// `mr` is a monotonic arena over a stack buffer with `null_memory_resource()`
// upstream: an overflow refuses with dict_reify_oom instead of spilling to the
// heap, and the cell asserts success, so it cannot pass on an early return.
//
// Dual gate, as tests/alloc_guard/test_dict066_grouped_read_alloc_guard.cpp:
//  (a) a TU-local global `operator new`/`operator new[]` counter, compiled out
//      under ASan/TSan/MSan (they own the allocator);
//  (b) mallocnesia through tools/check_alloc.py (tests/alloc_guard/CMakeLists.txt
//      registers the owned cell as a gate and the borrowed cell as an
//      EXPECT_VIOLATION control, each selected by GTEST_FILTER).
// `ReifyOwnedAllocGuard.BorrowedRouteAllocates` is the positive control: the same
// window on the BORROWED route deep-copies the table on the global heap, so both
// instruments must report non-zero there.
//
// Mutations, each turning the owned cell RED alone: revert D-1c (the impl from
// the global heap); replace shared_membership()'s arm 1 with arm 2 (the table
// copied on the global heap).

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <fixpp/dict/reify.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/wire/parser.hpp>
#include <memory>
#include <memory_resource>
#include <new>
#include <span>
#include <vector>

#include "support/fix44_dictionary.hpp"
#include "support/frame_view_factory.hpp"
#include "support/msvc_debug_arena_skip.hpp"  // FIXPP_SKIP_ON_MSVC_DEBUG_GLOBAL_HEAP_GUARD
#include "support/reify_test_frame.hpp"

// mallocnesia replaces these weak no-ops with its interceptor scope markers.
#include "support/alloc_guard_markers.hpp"

// ── (a) TU-local global operator-new counter ────────────────────────────
#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || \
    __has_feature(memory_sanitizer)
#define FIXPP_SANITIZER_REPLACES_NEW 1
#endif
#endif
#if !defined(FIXPP_SANITIZER_REPLACES_NEW) && \
    (defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__))
#define FIXPP_SANITIZER_REPLACES_NEW 1
#endif
#ifndef FIXPP_SANITIZER_REPLACES_NEW
#define FIXPP_SANITIZER_REPLACES_NEW 0
#endif

#if !FIXPP_SANITIZER_REPLACES_NEW
namespace {
std::atomic<long> g_alloc_count{0};
}  // namespace

void* operator new(std::size_t size) {
    ++g_alloc_count;
    void* p = std::malloc(size);
    if (!p) throw std::bad_alloc{};
    return p;
}
void* operator new[](std::size_t size) {
    ++g_alloc_count;
    void* p = std::malloc(size);
    if (!p) throw std::bad_alloc{};
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#endif  // !FIXPP_SANITIZER_REPLACES_NEW

namespace {

using fixpp::dict::application_version;
using fixpp::dict::session_version;
using fixpp::dict::version_profile;
using fixpp::wire::access_mode;
using fixpp::wire::Parser;

constexpr version_profile kProfileV44{.session = session_version::v44,
                                      .default_appl = application_version::v44,
                                      .has_per_message_override = false,
                                      ._reserved = 0};

// The handle arena. Sized with margin over one reify of the NewOrderSingle
// frame; an overflow is a refusal (null upstream), which the cells assert against.
constexpr std::size_t kHandleArena = 32768;

struct WindowResult {
    bool reified = false;
    bool shared = false;  // the handle's view uses the owner's table (owned arm ran)
    bool read = false;
    long global_new = 0;
};

// One measured window reifying `src`.
WindowResult reify_window(fixpp::wire::MessageView<access_mode::Index> const& src,
                          fixpp::dict::table_view const* owner_table) {
    std::array<std::byte, kHandleArena> buf{};
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size(),
                                           std::pmr::null_memory_resource()};
    WindowResult out;
#if !FIXPP_SANITIZER_REPLACES_NEW
    g_alloc_count.store(0, std::memory_order_relaxed);
#endif
    if (alloc_guard_start) alloc_guard_start();
    {
        auto h = fixpp::dict::reify(src, kProfileV44, &mr);
        if (h.has_value()) {
            out.reified = true;
            out.shared = h->view().hooks().opaque_dict() == owner_table;
            auto clord = h->field_value(11);
            out.read = clord.has_value() && clord->as_string() == "ORD1";
        }
    }  // the handle is destroyed inside the window
    if (alloc_guard_end) alloc_guard_end();
#if !FIXPP_SANITIZER_REPLACES_NEW
    out.global_new = g_alloc_count.load(std::memory_order_relaxed);
#endif
    return out;
}

class ReifyOwnedAllocGuard : public ::testing::Test {
protected:
    ReifyOwnedAllocGuard()
        : dict_(fixpp::test_support::make_fix44_dictionary()),
          sp_(std::make_shared<const fixpp::dict::table_view>(dict_->as_table_view())),
          frame_(fixpp::test_support::make_nos_frame()),
          fv_(fixpp::wire::test::make_frame_view(frame_)) {}

    std::shared_ptr<const fixpp::dict::Dictionary> dict_;
    std::shared_ptr<const fixpp::dict::table_view> sp_;  // the owner, held throughout
    std::vector<std::byte> frame_;
    fixpp::core::expected_t<fixpp::wire::frame_view> fv_;
    std::pmr::monotonic_buffer_resource parse_arena_;
};

TEST_F(ReifyOwnedAllocGuard, OwnedRouteZeroGlobalHeap) {
    FIXPP_SKIP_ON_MSVC_DEBUG_GLOBAL_HEAP_GUARD();
    ASSERT_TRUE(fv_.has_value());
    Parser<access_mode::Index> parser{fixpp::wire::detail::owned_route_key{}, sp_};
    auto src = parser.parse(*fv_, &parse_arena_);
    ASSERT_TRUE(src.has_value());

    (void)reify_window(*src, sp_.get());  // warm-up, outside the measured run
    WindowResult const r = reify_window(*src, sp_.get());

    ASSERT_TRUE(r.reified) << "the window must reach a live handle (arena too small?)";
    EXPECT_TRUE(r.shared) << "the owned arm must have run: the handle shares the owner's table";
    EXPECT_TRUE(r.read);
#if !FIXPP_SANITIZER_REPLACES_NEW
    EXPECT_EQ(r.global_new, 0)
        << "owned-route reify must draw every allocation from mr (NFR-003-3 as amended by "
           "fixpp#495)";
#endif
}

TEST_F(ReifyOwnedAllocGuard, BorrowedRouteAllocates) {
    FIXPP_SKIP_ON_MSVC_DEBUG_GLOBAL_HEAP_GUARD();
    ASSERT_TRUE(fv_.has_value());
    Parser<access_mode::Index> parser{*sp_};
    auto src = parser.parse(*fv_, &parse_arena_);
    ASSERT_TRUE(src.has_value());

    (void)reify_window(*src, sp_.get());
    WindowResult const r = reify_window(*src, sp_.get());

    ASSERT_TRUE(r.reified);
    EXPECT_FALSE(r.shared) << "the borrowed route has no owner to share: it copies";
    EXPECT_TRUE(r.read);
#if !FIXPP_SANITIZER_REPLACES_NEW
    EXPECT_GT(r.global_new, 0)
        << "positive control: the borrowed route's table copy must be visible to the counter";
#endif
}

}  // namespace
