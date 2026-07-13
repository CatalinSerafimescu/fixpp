// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/capi/message_read_failloud_test.cpp — 073 (L-065-2, #184) T006.
//
// Faithful exhaustion harness (research.md D6 / quickstart.md "Faithful
// exhaustion harness"): a tiny-capacity `std::pmr::monotonic_buffer_resource`
// over `std::pmr::null_memory_resource()` is used as the SAME parse arena
// for the whole message (top-level OffsetTable + its group_slices_/
// nested_cache_ + the nested sub-`OffsetTable`) — `OffsetTable::
// build_nested_subview` allocates the nested sub-table from
// `OffsetTable::resource()`, which is `entries_.get_allocator().resource()`,
// i.e. the *same* arena the top-level table was built with
// (src/wire/offset_table.cpp:698,758; include/fixpp/wire/offset_table.hpp:195-196).
// This is NOT a hand-built 16 KiB-exhausting message and NOT a post-hoc
// "alloc failed" flag flip — it reproduces the exact failure the fixed
// null-upstream arena exhibits at its cap ([[feedback_fault_injection_posthoc_flag_unfaithful]]).
//
// WIDE-MARGIN FIXTURE (hardened, research.md D6 "Platform-robust mode
// pinning"): this consumer witness cannot introspect the sub-table (that is
// T001's job, wire-level, mode-pinned by introspection), so it uses a
// wide-margin fixture instead of a knife-edge cap tuned to one toolchain's
// `sizeof(OffsetTable)`. The nested group (539) carries kNestedInstances=40
// occurrences (all within a single outer 453 occurrence) instead of 1 —
// mirrors the wide-margin pattern of T001/T008 (both also use a
// 20-to-40-instance nested group to widen the "outer succeeds / nested
// fails" arena-cap band).
//
// Empirical sweep against this exact fixture (byte step 40, IDENTICAL
// transition points on both `linux-clang-debug` and `linux-gcc-release`):
//   cap <  3600: top-level Parser::parse() itself fails (out of the usable
//                window — unrelated to the nested sub-view build).
//   cap in [3600, 4240]: top-level parses, but the OUTER group(453) read
//                itself already comes back empty (also excluded).
//   cap in [4280, 4320]: the UNRELATED top-level `fixpp_msg_get_group`/
//                `fixpp_group_get_nested_group` cursor-shell
//                `new_object<fixpp_group>()` allocation (a bare
//                `std::pmr::polymorphic_allocator`, not `noexcept`-wrapped)
//                itself throws uncaught `bad_alloc` — out of scope for this
//                feature (which is specifically about the nested sub-view
//                build); this narrow throw window must be avoided.
//   cap >= 4360 (verified stable through 8000, the top of the sweep — the
//                nested group's full 40-instance build needs far more than
//                8000 bytes so the band does not close within the swept
//                range): OUTER_OK (`fixpp_msg_get_group` returns
//                FIXPP_ERR_OK, count==1), and
//                `fixpp_group_get_nested_group` returns
//                FIXPP_ERR_WIRE_LIMIT_EXCEEDED / nc==0 — our target band,
//                >=3640 bytes wide (vs. the prior 1-instance fixture's
//                544-byte [576,1120] band).
// kTinyCap = 6000 sits deep in the middle of [4360, 8000+] (1640 bytes of
// margin below the nearest verified boundary, thousands above), clear of
// both the throw window and the top-level-fails windows, so the fixture is
// robust to `sizeof(OffsetTable)` / allocator-overhead drift across
// clang-debug / gcc-release / MSVC
// ([[feedback_local_verify_clang_only_misses_gcc_release_ci_job]]).

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <memory_resource>
#include <string>
#include <vector>

// C-ABI under test
#include "fix/c_api/error.h"
#include "fix/c_api/message.h"

// Engine-internal concrete fixpp_msg definition (test-only access)
#include "capi_internal.hpp"

// Wire surface
#include <fixpp/wire/parser.hpp>

// Test support
#include "support/frame_view_factory.hpp"
#include <fixpp/dict/table_view.hpp>

using fixpp::wire::access_mode;
using fixpp::wire::Parser;

namespace {

// Same tiny-cap value used by every test below (see file header for the
// empirical derivation against this exact fixture).
constexpr std::size_t kTinyCap = 6000;
constexpr int kNestedInstances = 40;

// Build a structurally-valid FIX.4.4 frame around `body`.
std::vector<std::byte> make_raw_frame(std::string const& body) {
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string full = "8=FIX.4.4\x01" + nine + body + "10=000\x01";
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

// Nested group: within each 453 instance, nest a sub-group under tag 539
// (NoNestedPartyIDs), delimiter 524 (NestedPartyID), member 525. Mirrors
// message_read_test.cpp::make_nested_group_dict() exactly (independent copy
// per file-locality convention — no shared test-support header for this
// dict exists yet).
fixpp::dict::table_view make_nested_group_dict() {
    fixpp::dict::table_view dict;
    dict.add_valid("D", 35)
        .add_valid("D", 34)
        .add_valid("D", 453)
        .add_valid("D", 448)
        .add_valid("D", 447)
        .add_valid("D", 539)
        .add_valid("D", 524)
        .add_valid("D", 525)
        .set_group_first(453, 448)
        .add_group_member(453, 447)
        .add_group_member(453, 539)
        .add_group_member(453, 524)
        .add_group_member(453, 525)
        .set_group_first(539, 524)
        .add_group_member(539, 525);
    return dict;
}

// Wrap a stack MessageView<Index> as a fixpp_msg_t for the C-ABI calls
// (identical pattern to message_read_test.cpp::InboundHandle).
struct InboundHandle {
    fixpp_msg msg{};
    const fixpp_msg_t* ptr() const noexcept {
        return reinterpret_cast<const fixpp_msg_t*>(&msg);
    }
};

// Raw frame bytes for "a present outer group (453=1) with a present,
// kNestedInstances-instance nested group (539=<N>/524=.../525=C each)" —
// shared by both the main SC-001 witness and the repeated-read witness
// below. `MessageView` is move-CONSTRUCTION-only (move-assignment is
// deleted — see include/fixpp/wire/parser.hpp:96-107, the per-message-arena
// allocator leak guard), so each call site parses inline into its own
// `auto mv_res` rather than threading a MessageView through an
// out-parameter.
std::vector<std::byte> present_nested_group_frame() {
    std::string body =
        "35=D\x01"
        "453=1\x01"
        "448=PA\x01"
        "447=D\x01";
    body += "539=" + std::to_string(kNestedInstances) + "\x01";
    for (int i = 0; i < kNestedInstances; ++i) {
        body += "524=N" + std::to_string(i) + "\x01" "525=C\x01";
    }
    return make_raw_frame(body);
}

}  // namespace

// ── SC-001 (US1 T006 main assertion) ──────────────────────────────────────
//
// A present nested group whose sub-view build is forced to fail by the tiny
// arena MUST return FIXPP_ERR_WIRE_LIMIT_EXCEEDED (FR-003/FR-008), NOT
// FIXPP_ERR_OK with entry-count 0. Asserted DIRECTLY on the returned code
// (not an `nc == 0` proxy) per SC-001/SC-002 discrimination requirement.
//
// Mutation-proof: removing the `if (r.alloc_failed) return
// WIRE_LIMIT_EXCEEDED;` arm in `fixpp_group_get_nested_group`
// (src/capi/message_read.cpp:489-499) makes this go RED — it falls through
// to `scan_slice_for_tag` finding "539" present in the raw slice bytes and
// returns FIXPP_ERR_OK / nc=0 (the exact silent-truncation bug #184/L-065-2
// this witness exists to close).
TEST(MessageReadFailloud, PresentNestedGroup_ArenaExhausted_ReturnsWireLimitExceeded) {
    std::vector<std::byte> arena_buf(kTinyCap);
    std::pmr::monotonic_buffer_resource arena{arena_buf.data(), arena_buf.size(),
                                               std::pmr::null_memory_resource()};

    auto dict = make_nested_group_dict();
    auto buf = present_nested_group_frame();
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    Parser<access_mode::Index> parser{dict};
    auto mv_res = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv_res.has_value());
    InboundHandle h;
    h.msg.view = &mv_res.value();

    const fixpp_group_t* grp = nullptr;
    size_t count = 0;
    ASSERT_EQ(fixpp_msg_get_group(h.ptr(), 453, &grp, &count), FIXPP_ERR_OK)
        << "top-level parse + outer group(453) read must succeed at kTinyCap "
           "(fixture/cap invariant broken)";
    ASSERT_EQ(count, 1U);

    const fixpp_group_t* nested = nullptr;
    size_t nc = 0;
    fixpp_error_t err = fixpp_group_get_nested_group(grp, 0, 539, &nested, &nc);

    EXPECT_EQ(err, FIXPP_ERR_WIRE_LIMIT_EXCEEDED)
        << "actual err=" << static_cast<int>(err) << " nc=" << nc
        << " — a present nested group whose sub-view allocation fails must "
           "NOT be reported as OK/nc==0 (silent truncation, #184/L-065-2)";
}

// ── D2 cache-exit discriminator (quickstart.md Scenario 3) ────────────────
//
// A repeated read of the SAME exhausted nested group must signal failure on
// BOTH reads — the second read is served from `nested_cache_`'s cached
// (possibly null) row (offset_table.cpp:744-750) and must not silently serve
// a stale "empty" result that hides the earlier failure (spec.md Edge Case
// "Repeated read after a failed build").
TEST(MessageReadFailloud, RepeatedReadAfterArenaExhaustion_SignalsFailureBothTimes) {
    std::vector<std::byte> arena_buf(kTinyCap);
    std::pmr::monotonic_buffer_resource arena{arena_buf.data(), arena_buf.size(),
                                               std::pmr::null_memory_resource()};

    auto dict = make_nested_group_dict();
    auto buf = present_nested_group_frame();
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    Parser<access_mode::Index> parser{dict};
    auto mv_res = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv_res.has_value());
    InboundHandle h;
    h.msg.view = &mv_res.value();

    const fixpp_group_t* grp = nullptr;
    size_t count = 0;
    ASSERT_EQ(fixpp_msg_get_group(h.ptr(), 453, &grp, &count), FIXPP_ERR_OK)
        << "top-level parse + outer group(453) read must succeed at kTinyCap "
           "(fixture/cap invariant broken)";
    ASSERT_EQ(count, 1U);

    const fixpp_group_t* nested1 = nullptr;
    size_t nc1 = 0;
    fixpp_error_t err1 = fixpp_group_get_nested_group(grp, 0, 539, &nested1, &nc1);
    EXPECT_EQ(err1, FIXPP_ERR_WIRE_LIMIT_EXCEEDED)
        << "read #1: actual err=" << static_cast<int>(err1) << " nc=" << nc1;

    const fixpp_group_t* nested2 = nullptr;
    size_t nc2 = 0;
    fixpp_error_t err2 = fixpp_group_get_nested_group(grp, 0, 539, &nested2, &nc2);
    EXPECT_EQ(err2, FIXPP_ERR_WIRE_LIMIT_EXCEEDED)
        << "read #2 (cached-row exit): actual err=" << static_cast<int>(err2)
        << " nc=" << nc2;
}

// ── SC-003 controls: legitimate emptiness must NOT raise the failure signal ─
//
// These run against an AMPLE (default, effectively unbounded) arena — the
// new failure signal must never fire for absence or genuine zero-entry
// groups (FR-007).

TEST(MessageReadFailloud, Control_AbsentNestedGroup_AmpleArena_ReturnsTagNotFound) {
    std::pmr::monotonic_buffer_resource arena;  // ample: default new_delete upstream
    auto dict = make_nested_group_dict();
    auto buf = make_raw_frame(
        "35=D\x01"
        "453=1\x01"
        "448=PA\x01"
        "447=D\x01");  // no 539 nested group at all
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    Parser<access_mode::Index> parser{dict};
    auto mv_res = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv_res.has_value());
    InboundHandle h;
    h.msg.view = &mv_res.value();

    const fixpp_group_t* grp = nullptr;
    size_t count = 0;
    ASSERT_EQ(fixpp_msg_get_group(h.ptr(), 453, &grp, &count), FIXPP_ERR_OK);
    ASSERT_EQ(count, 1U);

    const fixpp_group_t* nested = nullptr;
    size_t nc = 0;
    EXPECT_EQ(fixpp_group_get_nested_group(grp, 0, 539, &nested, &nc), FIXPP_ERR_TAG_NOT_FOUND);
    EXPECT_EQ(nc, 0U);
}

TEST(MessageReadFailloud, Control_CountZeroNestedGroup_AmpleArena_ReturnsOkZero) {
    std::pmr::monotonic_buffer_resource arena;  // ample: default new_delete upstream
    auto dict = make_nested_group_dict();
    // 539=0 present (declared count field) with no instances following.
    auto buf = make_raw_frame(
        "35=D\x01"
        "453=1\x01"
        "448=PA\x01"
        "447=D\x01"
        "539=0\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    Parser<access_mode::Index> parser{dict};
    auto mv_res = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv_res.has_value());
    InboundHandle h;
    h.msg.view = &mv_res.value();

    const fixpp_group_t* grp = nullptr;
    size_t count = 0;
    ASSERT_EQ(fixpp_msg_get_group(h.ptr(), 453, &grp, &count), FIXPP_ERR_OK);
    ASSERT_EQ(count, 1U);

    const fixpp_group_t* nested = nullptr;
    size_t nc = 0;
    EXPECT_EQ(fixpp_group_get_nested_group(grp, 0, 539, &nested, &nc), FIXPP_ERR_OK);
    EXPECT_EQ(nc, 0U);
}
