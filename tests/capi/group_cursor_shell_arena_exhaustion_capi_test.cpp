// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/capi/group_cursor_shell_arena_exhaustion_capi_test.cpp — fixpp#541
// reproduction (tests only). Drives the real C-ABI inbound
// dispatch path (RawAcceptor peer -> C-ABI engine -> registered receive
// callback) and, inside the callback, calls fixpp_msg_get_group /
// fixpp_group_get_nested_group repeatedly on the SAME group until the message's
// parse arena is exhausted.
//
// MECHANISM (re-derive by reading the files named):
//   - Both getters allocate a fixpp_group cursor shell with
//     `std::pmr::polymorphic_allocator<fixpp_group>(arena).new_object<...>()`
//     (src/capi/message_read.cpp), where `arena` is the message's parse
//     OffsetTable resource. Neither getter is noexcept; the file has no catch.
//   - On the inbound callback path the arena is a monotonic_buffer_resource
//     over the session's B(L) buffer, upstream the spill witness, which
//     forwards to fixpp::detail::arena_upstream(). On every non-MSVC-debug lane
//     that upstream is std::pmr::null_memory_resource(), so an exhausting
//     allocate() THROWS std::bad_alloc.
//   - group_slices_status caches per no_tag in group_index_, and the nested
//     path caches in nested_cache_, so after the first (cold) call a loop on
//     the SAME group allocates only the cursor shell.
//
// An uncaught throw reaches CapiApplication::fromApp's catch(...), which calls
// std::abort() (src/capi/engine.cpp), so the process would die before gtest could
// report. The callback therefore wraps each getter call in try/catch and records
// what it caught; nothing is rethrown.
//
// CONTRACT ASSERTED by the claim cells ([2i] "no exception crosses extern C"): the
// exhaustion loop ends with a non-OK fixpp_error_t and the callback catches nothing.
// A caught exception fails the cell, and the failure message names its type and the
// call at which it was thrown.
//
// GATING: a throw only occurs when arena_upstream() is null. Where it forwards to
// the heap (MSVC debug) the shell allocation succeeds, so the claim cells
// GTEST_SKIP there. The control cells run everywhere.
//
// Every getter call is through the public C ABI. The one internal header named
// here, session/parse_capacity.hpp, only sizes the loop cap from B(L).

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <new>
#include <string>
#include <string_view>

#include "fixpp/core/pmr_arena_upstream.hpp"            // arena_upstream()
#include "inbound_frame_dispositions_capi_support.hpp"  // RawAcceptor / CInitiator / frames
#include "session/parse_capacity.hpp"                   // buffer_bytes(L), kDefaultInboundLimit

using namespace std::chrono_literals;
using namespace fixpp::capi_test;
using namespace fixpp::capi_test::ifd;

namespace {

// True on the lanes where arena_upstream() is the throwing null resource (a shell
// allocation past the fixed buffer throws), false where it forwards to the heap.
bool null_upstream() { return fixpp::detail::arena_upstream() == std::pmr::null_memory_resource(); }

// An upper bound on the successful getter calls one dispatch can make: every
// successful call draws a non-empty cursor shell from the arena, so there cannot be
// more of them than the arena B(L) has bytes. A loop that reaches this cap did not
// draw from B(L): "path not driven", not a verdict.
std::size_t call_cap() {
    return fixpp::session::detail::parse_capacity::buffer_bytes(
               fixpp::session::kDefaultInboundLimit) +
           2U;
}

enum class LoopEnd : int { not_run = 0, by_throw, by_error, by_cap };
enum class Caught : int { none = 0, bad_alloc, other_std, ellipsis };

char const* caught_name(int c) {
    switch (static_cast<Caught>(c)) {
        case Caught::none:
            return "nothing";
        case Caught::bad_alloc:
            return "std::bad_alloc";
        case Caught::other_std:
            return "a std::exception other than std::bad_alloc";
        case Caught::ellipsis:
            return "a non-std exception";
    }
    return "?";
}

// NoPartyIDs(453) with `n` instances (each a minimal Parties member set).
std::string flat_group(std::size_t n) {
    std::string g = "453=" + std::to_string(n) + "\x01";
    for (std::size_t i = 0; i < n; ++i) g += "448=P\x01" "447=D\x01" "452=1\x01";
    return g;
}

// NoPartyIDs(453)=1 whose NoPartySubIDs(802) has `subs` instances.
std::string nested_group(std::size_t subs) {
    std::string g = "453=1\x01" "448=P\x01" "447=D\x01" "452=1\x01"
                    "802=" + std::to_string(subs) + "\x01";
    for (std::size_t i = 0; i < subs; ++i) g += "523=S\x01" "803=1\x01";
    return g;
}

// An inbound NewOrderSingle (seq 2) carrying ClOrdID(11)=`id` and `group_body`.
std::string order_with_group(std::string_view id, std::string const& group_body) {
    return "35=D\x01" + peer_header(2) + order_fields(id) + group_body;
}

// What the callback observed while driving one getter in a loop. Written on the
// engine's worker, read by the test after fence().
struct Ctx {
    std::string id;
    bool nested = false;      // drive fixpp_group_get_nested_group, else fixpp_msg_get_group
    std::size_t cap = 0;      // loop ceiling
    std::size_t max_calls = 0;  // >0 => control: stop after this many OK calls

    std::atomic<bool> fired{false};
    std::atomic<int> first_rc{-99};
    std::atomic<std::size_t> first_count{0};
    std::atomic<int> loop_end{static_cast<int>(LoopEnd::not_run)};
    std::atomic<int> caught{static_cast<int>(Caught::none)};
    std::atomic<std::size_t> calls{0};   // getter calls made in the loop
    std::atomic<int> last_rc{-99};       // the non-OK rc that ended the loop, if any
    std::atomic<int> control_ok{0};      // control: number of consecutive OK calls
};

void drive_getter(const fixpp_msg_t* inbound, void* ud) {
    auto* c = static_cast<Ctx*>(ud);
    char const* v = nullptr;
    std::size_t vlen = 0;
    if (fixpp_msg_get_string(inbound, 11, &v, &vlen) != FIXPP_ERR_OK || v == nullptr ||
        std::string_view{v, vlen} != c->id) {
        return;
    }
    c->fired.store(true, std::memory_order_release);

    // Cold first call: materializes the slice array (cached) and the first shell.
    const fixpp_group_t* outer = nullptr;
    std::size_t oc = 0;
    fixpp_error_t const frc = fixpp_msg_get_group(inbound, 453, &outer, &oc);
    c->first_rc.store(frc);
    c->first_count.store(oc);
    if (frc != FIXPP_ERR_OK || outer == nullptr) {
        // First read did not succeed; the shell-exhaustion path is not reachable
        // from here. The test's first_rc assertion diagnoses this.
        return;
    }

    // CONTROL: a few repeated reads, all expected OK (proves the loop drives the
    // real getter path and that ordinary usage does not throw).
    if (c->max_calls > 0) {
        int ok = 1;  // the first call above already succeeded
        for (std::size_t i = 1; i < c->max_calls; ++i) {
            if (c->nested) {
                const fixpp_group_t* ng = nullptr;
                std::size_t nn = 0;
                if (fixpp_group_get_nested_group(outer, 0, 802, &ng, &nn) == FIXPP_ERR_OK) ++ok;
            } else {
                const fixpp_group_t* g = nullptr;
                std::size_t n = 0;
                if (fixpp_msg_get_group(inbound, 453, &g, &n) == FIXPP_ERR_OK) ++ok;
            }
        }
        c->control_ok.store(ok);
        return;
    }

    // CLAIM: loop the SAME getter until the arena is exhausted, recording how the
    // loop ended and anything that was thrown out of the getter.
    for (std::size_t i = 0; i < c->cap; ++i) {
        fixpp_error_t rc = FIXPP_ERR_OK;
        try {
            if (c->nested) {
                const fixpp_group_t* ng = nullptr;
                std::size_t nn = 0;
                rc = fixpp_group_get_nested_group(outer, 0, 802, &ng, &nn);
            } else {
                const fixpp_group_t* g = nullptr;
                std::size_t n = 0;
                rc = fixpp_msg_get_group(inbound, 453, &g, &n);
            }
        } catch (std::bad_alloc const&) {
            c->caught.store(static_cast<int>(Caught::bad_alloc));
            c->loop_end.store(static_cast<int>(LoopEnd::by_throw));
            c->calls.store(i + 1);
            return;
        } catch (std::exception const&) {
            c->caught.store(static_cast<int>(Caught::other_std));
            c->loop_end.store(static_cast<int>(LoopEnd::by_throw));
            c->calls.store(i + 1);
            return;
        } catch (...) {
            c->caught.store(static_cast<int>(Caught::ellipsis));
            c->loop_end.store(static_cast<int>(LoopEnd::by_throw));
            c->calls.store(i + 1);
            return;
        }
        if (rc != FIXPP_ERR_OK) {
            c->last_rc.store(rc);
            c->loop_end.store(static_cast<int>(LoopEnd::by_error));
            c->calls.store(i + 1);
            return;
        }
    }
    c->loop_end.store(static_cast<int>(LoopEnd::by_cap));
    c->calls.store(c->cap);
}

// Establishes a session whose receive callback is `drive_getter` with userdata `ctx`,
// delivers `frame` (ClOrdID ctx.id, MsgSeqNum 2), then a "NEXT" order and a fence so the
// frame has been processed before the test asserts. Returns true if setup + delivery ran.
bool run(Ctx& ctx, std::string const& frame) {
    RawAcceptor peer;
    CInitiator c{peer.port(), 30, {}, drive_getter, &ctx};
    bool const up = !frame.empty() && establish(c, peer, 30) && peer.write(frame);
    bool const next = up && peer.write(order(3, "NEXT")) && peer.fence(4, ctx.id) &&
                      c.rec.received_id("NEXT");
    EXPECT_TRUE(c.established()) << "the session stayed established (no abort/disconnect)";
    (void)next;
    return up;
}

// The contract every claim cell asserts once its loop has run.
void expect_exhaustion_is_an_error_code(Ctx const& ctx, char const* getter) {
    // Path driven: the loop ended by exhaustion, not by the cap.
    EXPECT_NE(ctx.loop_end.load(), static_cast<int>(LoopEnd::by_cap))
        << "the loop made " << ctx.calls.load() << " calls without exhausting the arena";
    EXPECT_EQ(ctx.caught.load(), static_cast<int>(Caught::none))
        << getter << " let " << caught_name(ctx.caught.load())
        << " cross the extern \"C\" boundary on call " << ctx.calls.load()
        << " of the exhaustion loop";
    EXPECT_EQ(ctx.loop_end.load(), static_cast<int>(LoopEnd::by_error))
        << getter << " must report arena exhaustion as a non-OK fixpp_error_t; last_rc="
        << ctx.last_rc.load();
    ::testing::Test::RecordProperty("calls_to_exhaustion", static_cast<int>(ctx.calls.load()));
}

// ── Claim: fixpp_msg_get_group, small frame (most of B(L) free after the parse) ──
TEST(GroupCursorShellArenaExhaustionCapi, MsgGetGroupReportsArenaExhaustionAsAnErrorCode) {
    if (!null_upstream()) GTEST_SKIP() << "arena_upstream() forwards to the heap on this lane";
    Ctx ctx;
    ctx.id = "G541F";
    ctx.cap = call_cap();
    ASSERT_TRUE(run(ctx, frame44(order_with_group(ctx.id, flat_group(3)))));
    ASSERT_TRUE(ctx.fired.load()) << "the frame reached the receive callback";
    ASSERT_EQ(ctx.first_rc.load(), FIXPP_ERR_OK) << "the first get_group must succeed";
    EXPECT_EQ(ctx.first_count.load(), 3U);
    expect_exhaustion_is_an_error_code(ctx, "fixpp_msg_get_group");
}

// ── Claim: fixpp_msg_get_group, a frame of exactly L bytes padded at the densest
// layout, three bytes a field: the parse's reserve and overlay then take all of B(L)
// but the callback-read headroom (src/session/parse_capacity.hpp) ──
TEST(GroupCursorShellArenaExhaustionCapi,
     MsgGetGroupOnAFrameAtTheInboundLimitReportsArenaExhaustionAsAnErrorCode) {
    if (!null_upstream()) GTEST_SKIP() << "arena_upstream() forwards to the heap on this lane";
    Ctx ctx;
    ctx.id = "G541N";
    ctx.cap = call_cap();
    std::string const head = order_with_group(ctx.id, flat_group(3));
    std::string const frame =
        frame44_of_size(head, fixpp::session::kDefaultInboundLimit, "2", Pad::dense);
    ASSERT_FALSE(frame.empty()) << "frame builder failed";
    ASSERT_TRUE(run(ctx, frame));
    ASSERT_TRUE(ctx.fired.load()) << "the frame reached the receive callback";
    ASSERT_EQ(ctx.first_rc.load(), FIXPP_ERR_OK) << "the first get_group must succeed";
    expect_exhaustion_is_an_error_code(ctx, "fixpp_msg_get_group");
}

// ── Control: fixpp_msg_get_group, a few calls, all OK ──
TEST(GroupCursorShellArenaExhaustionCapi, MsgGetGroupControlFewCallsAllOk) {
    Ctx ctx;
    ctx.id = "G541FC";
    ctx.max_calls = 5;
    ASSERT_TRUE(run(ctx, frame44(order_with_group(ctx.id, flat_group(3)))));
    ASSERT_TRUE(ctx.fired.load());
    EXPECT_EQ(ctx.first_rc.load(), FIXPP_ERR_OK);
    EXPECT_EQ(ctx.first_count.load(), 3U);
    EXPECT_EQ(ctx.control_ok.load(), 5) << "every get_group call returns OK";
}

// ── Claim: fixpp_group_get_nested_group, small frame ──
TEST(GroupCursorShellArenaExhaustionCapi, NestedGroupReportsArenaExhaustionAsAnErrorCode) {
    if (!null_upstream()) GTEST_SKIP() << "arena_upstream() forwards to the heap on this lane";
    Ctx ctx;
    ctx.id = "G541X";
    ctx.nested = true;
    ctx.cap = call_cap();
    ASSERT_TRUE(run(ctx, frame44(order_with_group(ctx.id, nested_group(2)))));
    ASSERT_TRUE(ctx.fired.load()) << "the frame reached the receive callback";
    ASSERT_EQ(ctx.first_rc.load(), FIXPP_ERR_OK) << "the outer get_group must succeed";
    EXPECT_EQ(ctx.first_count.load(), 1U);
    expect_exhaustion_is_an_error_code(ctx, "fixpp_group_get_nested_group");
}

// ── Control: fixpp_group_get_nested_group, a few calls, all OK ──
TEST(GroupCursorShellArenaExhaustionCapi, NestedGroupControlFewCallsAllOk) {
    Ctx ctx;
    ctx.id = "G541XC";
    ctx.nested = true;
    ctx.max_calls = 5;
    ASSERT_TRUE(run(ctx, frame44(order_with_group(ctx.id, nested_group(2)))));
    ASSERT_TRUE(ctx.fired.load());
    EXPECT_EQ(ctx.first_rc.load(), FIXPP_ERR_OK);
    EXPECT_EQ(ctx.control_ok.load(), 5) << "every get_nested_group call returns OK";
}

}  // namespace
