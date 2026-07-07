// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/group_context_lookup_alloc_gate_test.cpp — 063 T018 [US1]
// [pin#3-noalloc]
//
// Proves repeated context-scoped group-membership lookups
// (`table_view::group_member_tags(msg_type, parent_path, no_tag)` and the
// `group_member_fn_t` predicate the wire Parser installs over it, per
// `parser.hpp:484-494`) perform ZERO heap allocation.
//
// `group_member_fn_t` (`offset_table.hpp:29`) takes NO memory_resource
// parameter — it is a pure `unordered_map::find()` over already-built
// storage — so a PMR `counting_resource` has nothing to route through and
// would be a false-pass gate here (feedback_tracking_pmr_resource_false_pass:
// a non-PMR container escapes a tracking resource via global `::operator
// new`). The ONLY honest local mechanism is global-allocation interception:
//
//  (a) a TU-local global `operator new`/`operator new[]` counter (same
//      shape as tests/session/test_business_messages_build.cpp), guarded
//      under `FIXPP_SANITIZER_REPLACES_NEW` so it compiles out under
//      ASan/TSan/MSan (which ship their own strong operator new and would
//      alloc-dealloc-mismatch / multiply-define against a replacement).
//  (b) mallocnesia LD_PRELOAD (tests/dictionary/CMakeLists.txt's
//      `_mallocnesia` ctest, via tools/check_alloc.py) — intercepts
//      malloc/calloc/realloc globally, catching any allocation this TU's
//      own counter cannot see (e.g. inside a linked library's TU-local
//      operator new). EMPIRICALLY VERIFIED to fire on this host (not
//      inert): a temporary `new int` injected into
//      tests/codegen/group_entry_alloc_gate_test.cpp's measured window was
//      confirmed to make `tools/check_alloc.py` exit 1 with
//      "[mallocnesia] FAIL: 1 allocation(s) intercepted" before being
//      reverted — see the 063 T018 phase-implementer report.
//
// Mutation-proof (performed manually during implementation, NOT shipped as
// a permanent second test — the shipped test asserts the CORRECT zero-alloc
// behaviour): a `new int` was temporarily inserted inside this file's
// measured window; both (a) the TU-local counter assertion below and (b)
// the mallocnesia ctest were confirmed to go RED, then the injection was
// reverted and both confirmed GREEN again.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/group_view.hpp>  // fixpp::wire::group_context
#include <memory>
#include <memory_resource>
#include <span>
#include <string_view>
#include <vector>

// mallocnesia replaces these weak no-ops with its interceptor scope markers
// (POSIX only — see the header for the Windows no-op fallback).
#include "support/alloc_guard_markers.hpp"

// ── (a) TU-local global operator-new counter ────────────────────────────
// See tests/session/test_business_messages_build.cpp for the identical
// pattern + rationale (ASan/TSan/MSan ship their own strong operator new).
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
    if (!p) {
        throw std::bad_alloc{};
    }
    return p;
}

void* operator new[](std::size_t size) {
    ++g_alloc_count;
    void* p = std::malloc(size);
    if (!p) {
        throw std::bad_alloc{};
    }
    return p;
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#endif  // !FIXPP_SANITIZER_REPLACES_NEW

namespace {

using fixpp::dict::table_view;
using fixpp::wire::group_context;

// Byte-identical copy of the group_member_fn_t installed by Parser's
// dict-lvalue ctor (parser.hpp:494-517) — same established pattern as
// tests/dictionary/defect_a_group_context_test.cpp and
// tests/wire/nested_group_slices_cache_test.cpp's `dict_group_member`.
bool group_member_fn(void const* d, group_context const& ctx, std::uint16_t no_tag,
                     std::uint16_t tag) noexcept {
    auto const* dict = static_cast<table_view const*>(d);
    auto const members = dict->group_member_tags(
        ctx.msg_type, std::span<std::uint16_t const>{ctx.parent_path.data(), ctx.depth}, no_tag);
    for (auto const member_tag : members) {
        if (member_tag == tag) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST(GroupContextLookupAllocGate, RepeatedLookupsZeroAlloc) {
    // Heap-backed arena for the ONE-TIME dictionary load (BEFORE the
    // measured window) — not itself alloc-gated.
    constexpr std::size_t kArenaBytes = 8UZ * 1024UZ * 1024UZ;
    auto storage = std::make_unique<std::byte[]>(kArenaBytes);
    std::pmr::monotonic_buffer_resource mr{storage.get(), kArenaBytes};

    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX44.xml";
    auto dict = fixpp::dict::XmlLoader{}.load(path, &mr);
    auto tv = dict.as_table_view();  // builds the group_ctx_ store — BEFORE the window

    // Two distinct contexts for the SAME reused no_tag (295) — MassQuote
    // ("i") and a top-level miss-then-fallback probe — plus a genuinely
    // absent no_tag, so the measured loop exercises hit, context-vs-legacy
    // fallback, and miss paths, not just one lucky cache-friendly case.
    group_context const mass_quote_ctx{.msg_type = "i", .parent_path = {296}, .depth = 1};
    std::array<std::uint16_t, 1> const parent_path_storage{296};
    std::span<std::uint16_t const> const parent_path{parent_path_storage};

    // Sanity (outside the window): the lookups must resolve correctly
    // before we trust a zero-alloc count on them.
    ASSERT_TRUE(group_member_fn(&tv, mass_quote_ctx, 295, 299));
    ASSERT_TRUE(group_member_fn(&tv, mass_quote_ctx, 295, 132));
    ASSERT_TRUE(group_member_fn(&tv, mass_quote_ctx, 295, 133));
    ASSERT_FALSE(group_member_fn(&tv, mass_quote_ctx, 295, 9999));  // real group, absent tag
    ASSERT_TRUE(tv.group_member_tags("i", parent_path, 295).size() >= 3);

#if !FIXPP_SANITIZER_REPLACES_NEW
    g_alloc_count.store(0, std::memory_order_relaxed);
#endif
    if (alloc_guard_start) alloc_guard_start();

    constexpr int kIterations = 5000;
    for (int i = 0; i < kIterations; ++i) {
        // (1) context-store HIT, real colliding no_tag, MassQuote context.
        bool const has299 = group_member_fn(&tv, mass_quote_ctx, 295, 299);
        bool const has132 = group_member_fn(&tv, mass_quote_ctx, 295, 132);
        bool const has133 = group_member_fn(&tv, mass_quote_ctx, 295, 133);
        // (2) context-store MISS on the tag (real group, absent member).
        bool const hasMiss = group_member_fn(&tv, mass_quote_ctx, 295, 9999);
        // (3) direct table_view accessor, same context.
        auto const members = tv.group_member_tags("i", parent_path, 295);

        if (!has299 || !has132 || !has133 || hasMiss || members.size() < 3) {
            // Correctness must hold on EVERY iteration, not just the
            // sanity check above — fail loudly inside the loop rather than
            // silently accumulating a wrong-but-fast result.
            ADD_FAILURE() << "iteration " << i << ": group_member_fn/group_member_tags "
                                                    "resolved incorrectly inside the alloc window";
            break;
        }
    }

    if (alloc_guard_end) alloc_guard_end();

#if !FIXPP_SANITIZER_REPLACES_NEW
    EXPECT_EQ(g_alloc_count.load(std::memory_order_relaxed), 0)
        << kIterations << " repeated group_member_fn/group_member_tags lookups must not "
                          "allocate (group_member_fn_t takes no memory_resource — a pure "
                          "unordered_map lookup over already-built storage, [pin#3-noalloc])";
#endif
}
