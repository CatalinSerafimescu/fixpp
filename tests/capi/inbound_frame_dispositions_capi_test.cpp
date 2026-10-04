// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/capi/inbound_frame_dispositions_capi_test.cpp — 093-inbound-frame-dispositions,
// the C-ABI surface (contract C-7 rows 7 and 8; data-model E-7, E-8; quickstart Q-1's
// C arm, Q-16's C arm, Q-30 and Q-33's C arms; tasks.md T090, T091).
//
// A C-ABI initiator engine faces a raw TCP acceptor peer
// (inbound_frame_dispositions_capi_support.hpp). The cells:
//   - Q-30: fixpp_session_config_set_logon_timeout_ms refuses a null handle and zero;
//     fixpp_session_garbled_frame_count refuses a null handle and a null `out`, writes
//     0 before the session exists, and reads the count after a garble (Q-1's C arm);
//   - the getter's "thread-safe" token: another thread reads the count while the
//     session counts garbles on its strand. TSan is what fails it on a race, so it
//     carries weight on linux-clang-tsan;
//   - Q-16's C arm: a timeout set through the setter is honoured at T, on wall time;
//   - Q-33's C arms: the C group getters inside a C callback at headroom exhaustion.
//
// Q-16's band. The C ABI has only a real-time clock, so the close is timed on wall
// time from a stamp taken before fixpp_engine_start, which precedes the deadline's
// start. The lower bound is T. The upper bound is half the default timeout, derived
// from the competing timeout rather than from expected latency: a setter that is
// ignored closes at the default, outside the band. That a setter-ignored build fails
// the upper bound is also what shows no other close source fires inside the band on a
// pre-Active initiator; re-derive it by making the setter store nothing and running
// the cell (tasks.md T090).
//
// Q-33's C arms. The frames are FIX 4.4 NewOrderSingles of exactly L = 65536 bytes:
// a NoPartyIDs(453) group, then "2=<SOH>" fields (tag 2 is not a NewOrderSingle field,
// so each is an unknown field) to fill the frame, so the parse leaves only the
// callback headroom. The assertions branch at runtime on the library's own condition,
// the parse arena's upstream (fixpp::detail::arena_upstream()), and no arm is skipped:
//   - null upstream: fixpp_msg_get_group reports FIXPP_ERR_TYPE_MISMATCH and
//     fixpp_group_get_nested_group FIXPP_ERR_WIRE_LIMIT_EXCEEDED (contract C-8 L-5);
//   - forwarding upstream (MSVC debug): each read succeeds and the spill witness
//     records the spill.
// Each arm has a control: the same group without the padding, which leaves the
// headroom intact, so the getter returns OK with the group's count. The parent group
// handle is taken while the headroom is intact: the C cursor shells allocate from the
// parse arena with no catch (L-17, fixpp#541), which is outside 093.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <memory_resource>
#include <string>
#include <string_view>
#include <thread>

#include "capi_internal.hpp"
#include "fix/c_api/message.h"
#include "fix/c_api/session.h"
#include "fixpp/core/pmr_arena_upstream.hpp"
#include "fixpp/session/session.hpp"
#include "inbound_frame_dispositions_capi_support.hpp"
#include "support/session_test_access.hpp"

using namespace std::chrono_literals;
using namespace fixpp::capi_test::ifd;

namespace {

std::shared_ptr<fixpp::session::Session> lookup(CInitiator& c) {
    auto* e = reinterpret_cast<fixpp_engine*>(c.engine);
    if (e->state_ == nullptr || !e->state_->engine_.has_value()) return nullptr;
    return e->state_->engine_->lookup(c.id);
}

// Starts `c`, accepts its connection, reads its Logon and answers it.
bool establish(CInitiator& c, RawAcceptor& peer, std::uint32_t heartbeat_s) {
    return c.start() && peer.accept() && peer.read_logon() &&
           peer.write(logon_reply(heartbeat_s)) && c.wait_established();
}

// ── Q-30: the setter ────────────────────────────────────────────────────────

TEST(CapiInboundFrameDispositionsQ30, SetLogonTimeoutRefusesANullHandleAndZero) {
    EXPECT_EQ(fixpp_session_config_set_logon_timeout_ms(nullptr, 500), FIXPP_ERR_NULL_HANDLE);

    fixpp_session_config_t* sc = nullptr;
    ASSERT_EQ(fixpp_session_config_create(&sc), FIXPP_ERR_OK);
    auto const stored = [sc] {
        return reinterpret_cast<fixpp_session_config*>(sc)->cfg.logon_timeout_ms;
    };
    EXPECT_EQ(fixpp_session_config_set_logon_timeout_ms(sc, 700), FIXPP_ERR_OK);
    EXPECT_EQ(stored(), 700U) << "the setter stores its value";
    EXPECT_EQ(fixpp_session_config_set_logon_timeout_ms(sc, 0), FIXPP_ERR_CAPI_CONFIG_INVALID);
    EXPECT_EQ(stored(), 700U) << "a refused zero leaves the stored value as it was";
    EXPECT_EQ(fixpp_session_config_set_logon_timeout_ms(sc, UINT32_MAX), FIXPP_ERR_OK);
    EXPECT_EQ(stored(), UINT32_MAX);
    fixpp_session_config_destroy(sc);
}

// ── Q-30: the getter's refusals and its value before the session exists ─────

TEST(CapiInboundFrameDispositionsQ30, GarbledFrameCountRefusals) {
    std::uint64_t out = 77;
    EXPECT_EQ(fixpp_session_garbled_frame_count(nullptr, nullptr), FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(fixpp_session_garbled_frame_count(nullptr, &out), FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(out, 0U) << "*out is written 0 before the handle is checked";

    RawAcceptor peer;
    CInitiator c{peer.port(), 30};
    ASSERT_TRUE(c.opened);
    EXPECT_EQ(fixpp_session_garbled_frame_count(c.session, nullptr), FIXPP_ERR_NULL_HANDLE);

    // Opened, engine not started: the session does not exist yet.
    out = 77;
    EXPECT_EQ(fixpp_session_garbled_frame_count(c.session, &out), FIXPP_ERR_OK);
    EXPECT_EQ(out, 0U) << "0 before the session exists";

    // A closed handle is invalidated.
    EXPECT_EQ(fixpp_session_close(c.session), FIXPP_ERR_THREAD_SESSION_LIFECYCLE);
    out = 77;
    EXPECT_EQ(fixpp_session_garbled_frame_count(c.session, &out), FIXPP_ERR_INVALID_HANDLE);
    EXPECT_EQ(out, 0U) << "*out is written 0 before the handle is checked";
}

// ── Q-1's C arm: the count after a garble, through C and C++ ────────────────

TEST(CapiInboundFrameDispositionsQ1, TheCountReadsOneAfterAGarble) {
    RawAcceptor peer;
    CInitiator c{peer.port(), 30};
    ASSERT_TRUE(establish(c, peer, 30)) << "setup";

    std::uint64_t before = 77;
    EXPECT_EQ(fixpp_session_garbled_frame_count(c.session, &before), FIXPP_ERR_OK);
    EXPECT_EQ(before, 0U);

    // Junk before a frame start is one garbled region (TC 2d).
    ASSERT_TRUE(peer.write("GARBLE" + heartbeat(2)));
    ASSERT_TRUE(peer.fence(3, "Q1")) << "the session answers the TestRequest after the junk";

    std::uint64_t after = 0;
    EXPECT_EQ(fixpp_session_garbled_frame_count(c.session, &after), FIXPP_ERR_OK);
    EXPECT_EQ(after, 1U) << "through C";
    auto const s = lookup(c);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->garbled_frame_count(), 1U) << "through C++";
    EXPECT_TRUE(c.established()) << "the session carried on";
}

// ── The getter's "thread-safe" token ────────────────────────────────────────
//
// A thread other than the engine's reads the count in a loop while the session counts
// garbles on its strand. The reads never decrease, and the last one, taken after the
// fence has shown every garble counted, equals the count. The reader starts only once
// the session is published, so it reads the live counter, and it is joined before the
// engine is destroyed (the engine asserts no lookup lease is outstanding).
TEST(CapiInboundFrameDispositionsQ30, TheCountIsReadFromAnotherThreadWhileTheSessionCounts) {
    constexpr std::uint32_t kGarbles = 20;
    RawAcceptor peer;
    CInitiator c{peer.port(), 30};
    ASSERT_TRUE(establish(c, peer, 30)) << "setup";

    std::atomic<bool> stop{false};
    bool calls_ok = true;
    bool monotonic = true;
    std::uint64_t last = 0;
    std::uint64_t reads = 0;
    std::thread reader([&] {
        std::uint64_t prev = 0;
        for (;;) {
            bool const final_read = stop.load(std::memory_order_acquire);
            std::uint64_t v = 0;
            if (fixpp_session_garbled_frame_count(c.session, &v) != FIXPP_ERR_OK) calls_ok = false;
            if (v < prev) monotonic = false;
            prev = v;
            ++reads;
            if (final_read) {
                last = v;
                return;
            }
        }
    });

    bool written = true;
    for (std::uint32_t i = 0; i < kGarbles; ++i) {
        written = written && peer.write("GARBLE" + heartbeat(2U + i));
    }
    bool const fenced = written && peer.fence(2U + kGarbles, "TS");
    stop.store(true, std::memory_order_release);
    reader.join();

    EXPECT_TRUE(written && fenced) << "every garble was counted before the last read";
    EXPECT_TRUE(calls_ok);
    EXPECT_TRUE(monotonic) << "a read went below an earlier one";
    EXPECT_EQ(last, kGarbles);
    EXPECT_GT(reads, 1U);
}

// ── Q-16's C arm ────────────────────────────────────────────────────────────

TEST(CapiInboundFrameDispositionsQ16, ATimeoutSetThroughTheSetterIsHonouredAtT) {
    constexpr std::chrono::milliseconds kT{500};
    constexpr std::chrono::milliseconds kDefault{10000};  // SessionConfig's default T
    constexpr std::chrono::milliseconds kUpper = kDefault / 2;

    RawAcceptor peer;
    CInitiator c{peer.port(), 30, [&](fixpp_session_config_t* sc) {
                     EXPECT_EQ(fixpp_session_config_set_logon_timeout_ms(
                                   sc, static_cast<std::uint32_t>(kT.count())),
                               FIXPP_ERR_OK);
                 }};
    auto const t0 = std::chrono::steady_clock::now();
    bool const started = c.start();
    bool const accepted = started && peer.accept();
    bool const logon = accepted && peer.read_logon();
    // The peer never answers the Logon.
    bool const closed = logon && peer.wait_eof(t0 + kUpper);
    auto const elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);
    bool const est = c.established();

    ASSERT_TRUE(started && accepted && logon) << "setup";
    EXPECT_TRUE(closed) << "no close within half the default timeout: elapsed " << elapsed.count()
                        << " ms";
    EXPECT_GE(elapsed, kT) << "closed before T";
    EXPECT_LT(elapsed, kUpper);
    EXPECT_FALSE(est);
}

// ── Q-33's C arms ───────────────────────────────────────────────────────────

constexpr std::size_t kInstances = 2000;
constexpr std::size_t kL = 65536;

std::string order_head(std::uint32_t seq, std::string_view cl_ord_id) {
    return "35=D\x01" + peer_header(seq) + order_fields(cl_ord_id);
}

// NoPartyIDs(453) with `parties` instances.
std::string flat_parties(std::size_t parties) {
    std::string g = "453=" + std::to_string(parties) + "\x01";
    for (std::size_t i = 0; i < parties; ++i) {
        g += "448=P\x01"
             "447=D\x01"
             "452=1\x01";
    }
    return g;
}

// NoPartyIDs(453) with one instance whose NoPartySubIDs(802) has `subs` instances.
std::string nested_parties(std::size_t subs) {
    std::string g =
        "453=1\x01"
        "448=P\x01"
        "447=D\x01"
        "452=1\x01"
        "802=" +
        std::to_string(subs) + "\x01";
    for (std::size_t i = 0; i < subs; ++i) {
        g += "523=S\x01"
             "803=1\x01";
    }
    return g;
}

enum class Read { flat, nested };

// What the receive callback read from the message whose ClOrdID is "Q33". Written on
// the engine's worker, read by the test after the fence.
struct GroupReads {
    Read read = Read::flat;
    Recorder rec;
    std::atomic<int> calls{0};
    std::atomic<int> group_rc{-1};
    std::atomic<std::size_t> group_count{0};
    std::atomic<int> nested_rc{-1};
    std::atomic<std::size_t> nested_count{0};
};

void read_groups(const fixpp_msg_t* inbound, void* ud) {
    auto* r = static_cast<GroupReads*>(ud);
    char const* value = nullptr;
    std::size_t len = 0;
    if (fixpp_msg_get_string(inbound, 11, &value, &len) == FIXPP_ERR_OK &&
        std::string_view{value, len} == "Q33") {
        r->calls.fetch_add(1);
        const fixpp_group_t* g = nullptr;
        std::size_t n = 0;
        fixpp_error_t const rc = fixpp_msg_get_group(inbound, 453, &g, &n);
        r->group_rc.store(rc);
        r->group_count.store(n);
        if (r->read == Read::nested && rc == FIXPP_ERR_OK && g != nullptr) {
            const fixpp_group_t* ng = nullptr;
            std::size_t nn = 0;
            r->nested_rc.store(fixpp_group_get_nested_group(g, 0, 802, &ng, &nn));
            r->nested_count.store(nn);
        }
    }
    record_receive(inbound, &r->rec);
}

bool null_upstream() { return fixpp::detail::arena_upstream() == std::pmr::null_memory_resource(); }

// The spill witness's count, read on the session's strand.
std::uint64_t parse_spills(CInitiator& c) {
    auto const s = lookup(c);
    if (!s) return 0;
    std::weak_ptr<fixpp::session::Session> const weak = s;
    auto fut = asio::co_spawn(
        s->executor().underlying(),
        [weak]() -> asio::awaitable<std::uint64_t> {
            if (auto p = weak.lock()) {
                co_return fixpp::session::session_test_access::parse_spills(*p);
            }
            co_return 0U;
        },
        asio::use_future);
    if (fut.wait_for(kStepBudget) != std::future_status::ready) return 0;
    return fut.get();
}

struct GroupRun {
    bool up = false;
    bool next = false;  // the session processed the next frame
    bool established = false;
    std::uint64_t spills = 0;
};

// Establishes a session whose receive callback reads groups per `reads`, delivers
// `frame` (ClOrdID "Q33", MsgSeqNum 2), then a NewOrderSingle "NEXT" and a fence.
GroupRun run_group_read(GroupReads& reads, std::string const& frame) {
    GroupRun r;
    RawAcceptor peer;
    CInitiator c{peer.port(), 30, {}, read_groups, &reads};
    r.up = !frame.empty() && establish(c, peer, 30) && peer.write(frame);
    r.next = r.up && peer.write(order(3, "NEXT")) && peer.fence(4, "Q33") &&
             reads.rec.received_id("NEXT");
    r.established = c.established();
    r.spills = parse_spills(c);
    return r;
}

TEST(CapiInboundFrameDispositionsQ33, MsgGetGroupAtHeadroomExhaustion) {
    GroupReads reads;
    reads.read = Read::flat;
    auto const r = run_group_read(
        reads,
        frame44_of_size(order_head(2, "Q33") + flat_parties(kInstances), kL, "2", Pad::dense));
    ASSERT_TRUE(r.up) << "setup";
    EXPECT_EQ(reads.calls.load(), 1) << "the frame reaches the receive callback";
    EXPECT_TRUE(r.next) << "the session processes the next frame";
    EXPECT_TRUE(r.established);
    if (null_upstream()) {
        EXPECT_EQ(reads.group_rc.load(), FIXPP_ERR_TYPE_MISMATCH)
            << "fixpp_msg_get_group reports exhaustion as FIXPP_ERR_TYPE_MISMATCH";
    } else {
        EXPECT_EQ(reads.group_rc.load(), FIXPP_ERR_OK) << "the read succeeds from the heap";
        EXPECT_EQ(reads.group_count.load(), kInstances);
        EXPECT_GT(r.spills, 0U) << "and the spill witness records the spill";
    }
}

// The control: the same group without the padding leaves the headroom intact.
TEST(CapiInboundFrameDispositionsQ33, MsgGetGroupControlWithTheHeadroomIntact) {
    GroupReads reads;
    reads.read = Read::flat;
    auto const r = run_group_read(reads, frame44(order_head(2, "Q33") + flat_parties(kInstances)));
    ASSERT_TRUE(r.up) << "setup";
    EXPECT_EQ(reads.calls.load(), 1);
    EXPECT_TRUE(r.next);
    EXPECT_EQ(reads.group_rc.load(), FIXPP_ERR_OK);
    EXPECT_EQ(reads.group_count.load(), kInstances);
}

TEST(CapiInboundFrameDispositionsQ33, NestedGroupAtHeadroomExhaustion) {
    GroupReads reads;
    reads.read = Read::nested;
    auto const r = run_group_read(
        reads,
        frame44_of_size(order_head(2, "Q33") + nested_parties(kInstances), kL, "2", Pad::dense));
    ASSERT_TRUE(r.up) << "setup";
    EXPECT_EQ(reads.calls.load(), 1) << "the frame reaches the receive callback";
    EXPECT_TRUE(r.next) << "the session processes the next frame";
    EXPECT_TRUE(r.established);
    EXPECT_EQ(reads.group_rc.load(), FIXPP_ERR_OK) << "the parent group, read with headroom left";
    EXPECT_EQ(reads.group_count.load(), 1U);
    if (null_upstream()) {
        EXPECT_EQ(reads.nested_rc.load(), FIXPP_ERR_WIRE_LIMIT_EXCEEDED)
            << "fixpp_group_get_nested_group reports exhaustion as "
               "FIXPP_ERR_WIRE_LIMIT_EXCEEDED";
    } else {
        EXPECT_EQ(reads.nested_rc.load(), FIXPP_ERR_OK) << "the read succeeds from the heap";
        EXPECT_EQ(reads.nested_count.load(), kInstances);
        EXPECT_GT(r.spills, 0U) << "and the spill witness records the spill";
    }
}

TEST(CapiInboundFrameDispositionsQ33, NestedGroupControlWithTheHeadroomIntact) {
    GroupReads reads;
    reads.read = Read::nested;
    auto const r =
        run_group_read(reads, frame44(order_head(2, "Q33") + nested_parties(kInstances)));
    ASSERT_TRUE(r.up) << "setup";
    EXPECT_EQ(reads.calls.load(), 1);
    EXPECT_TRUE(r.next);
    EXPECT_EQ(reads.group_rc.load(), FIXPP_ERR_OK);
    EXPECT_EQ(reads.group_count.load(), 1U);
    EXPECT_EQ(reads.nested_rc.load(), FIXPP_ERR_OK);
    EXPECT_EQ(reads.nested_count.load(), kInstances);
}

}  // namespace
