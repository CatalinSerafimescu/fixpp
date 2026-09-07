// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/read_first_frame_bounded_test.cpp — direct-helper witness
// for read_first_frame_bounded (088-firstframe-budget-timer-lifetime).
//
// Phase 3 (User Story 1) — T012-T015: cells B1/B2/B3/B5 (research.md D-6.1 /
// D-6.11). Each cell drives read_first_frame_bounded directly via a
// mock_transport Script and asserts the DELIVERED contract: a complete frame
// always wins over the budget (checked first, every iteration); the budget
// only fires when no frame is extractable, with a strict `>` (exceeds, not
// reaches — FR-014). Every cell was RED against `main` when written — see
// research.md D-6.7's per-cell RED-basis table.
//
// Pre-fix source shape (src/session/read_first_frame_bounded.hpp AS IT STOOD
// WHEN THESE CELLS WERE WRITTEN — T016-T018 has since replaced it; nothing
// below describes the current tree):
//   :56  timer.expires_after(deadline) — armed once, before the loop.
//   :72  pmr_carry_buffer carry{max_bytes, ...} — capacity max_bytes, NOT max_bytes+1.
//   :78  site A — `if (buf.size() >= max_bytes)` at the loop top (unreachable pre-frame
//        on every cell below, since buf starts empty).
//   :83-84 the read is UNCLAMPED — always requests the full 4096-byte read_buf,
//        regardless of remaining budget ("room").
//   :96  site B — `if (buf.size() >= max_bytes)`, evaluated AFTER the insert but
//        BEFORE framer.feed. This was the budget-before-frame defect every cell
//        below actually hit (site A was unreachable from any of these four
//        constructions — buf starts empty at every site-A check).
//
// research.md D-6.11's B2/B5 iteration tables (room/want columns, "terminates at
// ~50ms") describe the DELIVERED (clamped) loop, which is what these cells now
// run against. Recorded so a reader does not mistake those tables for the
// (now-superseded) pre-fix trace above.
//
// Anchors: research.md D-5/D-6/D-6.7/D-6.11; tasks.md T012-T015;
//          contracts/read_first_frame_bounded.md.
//
// Phase 4 (User Story 2) — T020: cell T1 (SC-005/SC-006, research.md D-6.2/
// D-6.3/D-6.7). Unlike B1/B2/B3/B5 above, T1's RED basis was the TIMER defect
// (at the time this cell was written — src/session/read_first_frame_bounded.hpp
// :59 `bool timed_out`, :65 the by-reference `timer.async_wait` lambda, :68
// `transport.cancel()`, :83 `while (!timed_out)`), not the budget-before-frame
// defect T012-T015 already fixed. T1's fix (arm-once absolute-expiry timer +
// the `||` join) landed at T026/T027 — this cell is GREEN under ASan against
// the delivered tree.
//
// Phase 5 (User Story 3) — T031/T032: cells B4 (SC-003) and B6 (SC-004/D-1b).
// Both land after T016-T018/T026-T029 (the fix), so both are regression
// guards over the delivered design, not RED-against-`main` cells — B4 is
// GREEN on pre-fix source by construction (research.md D-6.7); B6's RED is a
// mutant of the delivered design (a per-iteration deadline RE-ARM — see B6's
// own comment for where that mutant lives after #377), not `main`. See each
// cell's own comment for its RED basis.

#include "session/read_first_frame_bounded.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <asio/awaitable.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <fixpp/core/clock.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/transport/test/mock_transport.hpp>
#include <future>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "support/pump_until_ready.hpp"

// ── #377: EVERY CELL NOW CHOOSES ITS TIMEBASE, AND MOST CHOOSE "FROZEN" ──────
//
// `read_first_frame_bounded` takes a `fixpp::core::Clock&`. THREE shapes are
// used below and the choice is per-cell, load-bearing, and NOT interchangeable.
// The axis that matters is advance-vs-freeze, not mock-vs-real — a mock clock
// that is ADVANCED is not a weaker deadline than a real one, it is a
// SCHEDULED one:
//
//   frozen mock_clock — constructed and NEVER advanced. Its sleep_until parks
//   the waiter in a map and completes it only on advance() or a per-op slot
//   cancel (src/core/test/mock_clock.cpp), so THE DEADLINE CANNOT FIRE. Used by
//   every cell that asserts a NON-timeout outcome. For those cells the deadline
//   is a termination bound that must never compete, and freezing it deletes the
//   competition instead of widening it.
//
//   ADVANCED mock_clock — constructed, then advanced by exactly the deadline
//   at a point the cell has STAGED on an observable. Used by B6, the cell whose
//   subject IS the deadline firing. The deadline genuinely fires; what the
//   advance removes is the scheduler's vote on WHEN, relative to the loop it is
//   supposed to interrupt. Freezing this cell WOULD make it vacuous (it would
//   assert a timeout that can never happen) — the advance is what separates the
//   two, and it is why "frozen" above is not a synonym for "mock".
//
//   system_clock_source — the real wall clock. Used ONLY by T1, whose subject
//   is the relative ORDER of two real elapsed waits. It stays real because both
//   instants have to come out of ONE timer queue for the order to be decided by
//   expiry rather than by asio internals; a mock sleep completes by
//   `asio::post`, whose order against an already-expired steady_timer is not a
//   property a test may assert. T1 pays for that with a wall-clock margin — the
//   only one left in this file — and asserts both of its preconditions
//   explicitly so a runner that breaks it says so instead of blaming the code.
//
// ⚠️ WHAT THIS REPLACED, so it is not "simplified" back. PR #376 raised three
// cells' deadlines 50 ms → 5 s after `ctest --parallel 4` on windows-msvc-asan
// diluted a 6 ms cell to 735 ms and the deadline beat the mechanism under test
// (issue #377). That raise was interim and said so: 5 s was ~7x an observed
// figure, not a proof, so a slow enough runner reproduces the same vacuous run.
// A frozen clock has no figure to outrun. Do not reintroduce a wall-clock
// deadline in a cell that asserts a non-timeout outcome, at any magnitude.
//
// ⚠️ THAT PREDICTION CAME TRUE, on the two cells #377 left on the real clock.
// Run 34074957982 (windows-msvc-asan, `push:main`) failed BOTH: T1 with the
// deadline winning the race it must lose, B6 with zero reads completed inside
// its window. Neither is a defect in `read_first_frame_bounded` — both are the
// ARM GAP, the interval between the helper computing `abs_deadline` at entry
// and the mock transport arming its latency timer a few instructions later. A
// starved runner stretches that interval past `deadline - read_latency` and the
// expiry order inverts. B6 answered it by leaving the real clock (above); T1
// cannot, so it answers with a checked-and-retried staging (see the cell).
// `ci/red-arms/first-frame-arm-gap.sh` re-derives the mechanism on T1 — the
// half that reproduces on Linux — in both directions: pre-fix constants under
// load go RED, this construction under the SAME load does not. B6's half is
// re-derived by mutation instead (the re-arm, at its own cell).
//
// ⚠️ A FROZEN CLOCK IS NOT A WEAKER ASSERTION. The deadline arm is still armed,
// still joined, and still cancelled by the group on the winning path — what is
// gone is only its ability to WIN A RACE IT WAS NEVER MEANT TO ENTER. The arm
// being genuinely live is what B6/T2a demonstrate on the other side.
namespace {

using fixpp::core::error;
using fixpp::core::expected_t;
using fixpp::session::detail::read_first_frame_bounded;
using fixpp::transport::test::mock_transport;
using fixpp::transport::test::Script;

std::vector<std::byte> to_bytes(std::string_view s) {
    std::vector<std::byte> v;
    v.reserve(s.size());
    for (char c : s) v.push_back(static_cast<std::byte>(c));
    return v;
}

// Build a well-formed, checksum-valid FIX frame: "8=FIX.4.2\x01 9=<len>\x01 <body>
// 10=<chk>\x01". `body` must already start with "35=X\x01" and be SOH-delimited.
// Same pattern as tests/session/test_business_messages_read.cpp::make_frame.
std::vector<std::byte> make_frame(std::string_view body) {
    std::string pre = std::string("8=FIX.4.2\x01") + "9=" + std::to_string(body.size()) + "\x01" +
                      std::string(body);
    unsigned sum = 0;
    for (unsigned char c : pre) sum += c;
    char checksum[16]{};
    std::snprintf(checksum, sizeof(checksum), "10=%03u\x01", sum % 256U);
    return to_bytes(pre + checksum);
}

// Builds a Logon(35=A) frame of EXACTLY `frame_len` bytes, padding the body with
// a filler tag (58=Text) to hit the requested size. B1/B2/B3's constructions
// (research.md D-6.1/D-6.11) are stated relative to exact byte counts.
std::vector<std::byte> make_logon_of_length(std::size_t frame_len) {
    static constexpr std::string_view kFixedFields =
        "35=A\x01"
        "34=1\x01"
        "49=SNDR\x01"
        "52=20260101-00:00:00.000\x01"
        "56=TGT\x01"
        "98=0\x01"
        "108=30\x01";
    // frame_len == 10 ("8=FIX.4.2\x01") + 2 ("9=") + digits(body.size()) + 1 (SOH)
    //            + body.size() + 7 ("10=NNN\x01"), and body.size() ==
    //            kFixedFields.size() + 4 ("58=" + SOH) + pad_len.
    // Solved assuming a 4-digit BodyLength (true for every frame_len this feature
    // uses) and self-verified below rather than merely assumed.
    constexpr std::size_t kOverhead = 10 + 2 + 4 /*digits*/ + 1 + 7 + 4 /*"58="+SOH*/;
    if (frame_len <= kOverhead + kFixedFields.size()) {
        ADD_FAILURE() << "make_logon_of_length(" << frame_len << "): too small";
        return {};
    }
    std::size_t const pad_len = frame_len - kOverhead - kFixedFields.size();
    std::string body = std::string(kFixedFields) + "58=" + std::string(pad_len, 'Z') + "\x01";
    if (std::to_string(body.size()).size() != 4) {
        ADD_FAILURE() << "make_logon_of_length(" << frame_len
                      << "): BodyLength digit-count assumption (4) violated for body.size()=="
                      << body.size();
        return {};
    }
    std::vector<std::byte> frame = make_frame(body);
    if (frame.size() != frame_len) {
        ADD_FAILURE() << "make_logon_of_length(" << frame_len << "): internal size mismatch, got "
                      << frame.size();
        return {};
    }
    return frame;
}

std::string describe(expected_t<std::size_t> const& r) {
    if (r.has_value()) return "value=" + std::to_string(*r);
    return std::string("error=") + std::string(fixpp::core::to_string(r.error()));
}

std::string describe_sizes(std::vector<std::size_t> const& v) {
    std::string out = "{";
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) out += ", ";
        out += std::to_string(v[i]);
    }
    return out + "}";
}

// A cancellation-attributable outcome per D-6.1's T2a row / D-6.10a's leg-A
// binding: one of the two errors a genuinely-cancelled read/deadline arm can
// surface — order[0] (unspecified asio scheduler internals) decides which,
// and this bundle elsewhere refuses to depend on that ordering (D-6.4/D-6.10a).
bool is_cancellation_attributable(expected_t<std::size_t> const& r) {
    if (r.has_value()) return false;
    return r.error() == error::transport_read_cancelled ||
           r.error() == error::transport_handshake_timeout;
}

}  // namespace

// ── B1 (SC-001) ───────────────────────────────────────────────────────────────
// Single delivery, cumulative EXACTLY max_bytes, complete Logon at its head (the
// Logon's own frame length IS max_bytes — no surplus). S4/FR-002/INV-B2 requires
// this be ADMITTED. Pre-fix rejects at site B (:96, `4096 >= 4096`) BEFORE
// framer.feed ever runs, so the complete frame already sitting in `buf` is never
// discovered. RED here is attributable to the COMPARISON: a strict `>` at
// cumulative exactly max_bytes would not fire, feed would run, the frame would be
// found. Kills the `>=` retained mutant (research.md D-6.1).
TEST(ReadFirstFrameBounded, B1) {
    constexpr std::size_t kMaxBytes = 4096;

    Script s;
    s.inbound_bytes = make_logon_of_length(kMaxBytes);
    ASSERT_EQ(s.inbound_bytes.size(), kMaxBytes);

    asio::io_context ioc;
    // Frozen (#377): never advanced, so the deadline cannot fire and cannot
    // race this cell's mechanism. See the timebase note at the top of file.
    fixpp::core::mock_clock clock{{}, {}, ioc.get_executor()};
    mock_transport mt{ioc.get_executor(), std::move(s)};
    std::vector<std::byte> buf;

    auto fut = asio::co_spawn(
        ioc, read_first_frame_bounded(mt, buf, clock, std::chrono::milliseconds{1000}, kMaxBytes),
        asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(ioc, fut, "ReadFirstFrameBounded::B1")) {
        return;
    }
    expected_t<std::size_t> const result = fut.get();

    EXPECT_TRUE(result.has_value())
        << "B1 (SC-001): expected the first frame's length (" << kMaxBytes << "), got "
        << describe(result) << " — pre-fix rejects at cumulative == max_bytes BEFORE framing "
        << "runs (site B, read_first_frame_bounded.hpp:96, `buf.size() >= max_bytes`).";
    if (result.has_value()) {
        EXPECT_EQ(*result, kMaxBytes) << "B1 (SC-001): the admitted frame's exact length.";
    }
}

// ── B3 (SC-002) ───────────────────────────────────────────────────────────────
// B1's delivery (single read, cumulative exactly max_bytes) plus surplus: the
// Logon completes at byte 3500, with 596 bytes of non-frame surplus after it,
// still inside the single max_bytes-sized read. Pre-fix rejects at the SAME site
// B line as B1 (4096 >= 4096) before feed ever runs, so this cell shares B1's RED
// mechanism; B3's OWN contribution is the VALUE assertion below, which kills the
// `return buf.size()` mutant (it would return 4096, the whole buffer, instead of
// 3500, the frame's exact length — S3 / research.md D-6.1).
TEST(ReadFirstFrameBounded, B3) {
    constexpr std::size_t kMaxBytes = 4096;
    constexpr std::size_t kLogonLen = 3500;

    std::vector<std::byte> stream = make_logon_of_length(kLogonLen);
    ASSERT_EQ(stream.size(), kLogonLen);
    std::vector<std::byte> const surplus = to_bytes(std::string(kMaxBytes - kLogonLen, 'Y'));
    stream.insert(stream.end(), surplus.begin(), surplus.end());
    ASSERT_EQ(stream.size(), kMaxBytes);

    Script s;
    s.inbound_bytes = std::move(stream);

    asio::io_context ioc;
    // Frozen (#377): never advanced, so the deadline cannot fire and cannot
    // race this cell's mechanism. See the timebase note at the top of file.
    fixpp::core::mock_clock clock{{}, {}, ioc.get_executor()};
    mock_transport mt{ioc.get_executor(), std::move(s)};
    std::vector<std::byte> buf;

    auto fut = asio::co_spawn(
        ioc, read_first_frame_bounded(mt, buf, clock, std::chrono::milliseconds{1000}, kMaxBytes),
        asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(ioc, fut, "ReadFirstFrameBounded::B3")) {
        return;
    }
    expected_t<std::size_t> const result = fut.get();

    EXPECT_TRUE(result.has_value())
        << "B3 (SC-002): expected success (frame length " << kLogonLen << "), got "
        << describe(result) << " — see B1's RED mechanism: site B (:96, `buf.size() >= "
        << "max_bytes`) fires before framing runs.";
    if (result.has_value()) {
        EXPECT_EQ(*result, kLogonLen)
            << "B3 (SC-002): S3 — must return the frame's EXACT length (" << kLogonLen
            << "), not the whole buffer (" << kMaxBytes << "). Kills the `return buf.size()` "
            << "mutant.";
    }
}

// ── B2 (SC-012) ───────────────────────────────────────────────────────────────
// Fragmented delivery via Script::inbound_chunks (mechanism 5): {1000, 3097},
// cumulative 4097 (max_bytes + 1), Logon completing at byte 3500. This is the
// only cell of the four that isolates the budget-before-frame ORDERING defect:
// B1/B3's single-read constructions never let a second read observe an
// already-complete frame, so they cannot discriminate ordering from comparison.
//
// tasks.md T013 asks this cell to RED against all three of budget-before-frame,
// carry@max_bytes (D-1a) and `>=` retained — NOT achievable from one run against
// `main`. Pre-fix carries all three simultaneously, and iter2's site B
// (`4097 >= 4096`) fires BEFORE framer.feed is ever called on chunk 1, so the
// carry-capacity path (F2b) is never reached — this is D-1a's own "F2b is
// pre-empted by F1's pre-feed position" point, one level up. Only the ORDERING
// defect is exercised here; `carry@max_bytes` and `comparison-only` are
// discharged at T019 against mutants of the DELIVERED design, per research.md
// D-6.11's own three-column derivation table. Recorded as an escalation in the
// verify record, not silently narrowed.
TEST(ReadFirstFrameBounded, B2) {
    constexpr std::size_t kMaxBytes = 4096;
    constexpr std::size_t kLogonLen = 3500;
    constexpr std::size_t kCumulative = kMaxBytes + 1;  // 4097

    std::vector<std::byte> stream = make_logon_of_length(kLogonLen);
    ASSERT_EQ(stream.size(), kLogonLen);
    std::vector<std::byte> const surplus = to_bytes(std::string(kCumulative - kLogonLen, 'Y'));
    stream.insert(stream.end(), surplus.begin(), surplus.end());
    ASSERT_EQ(stream.size(), kCumulative);

    Script s;
    s.inbound_chunks = {
        std::vector<std::byte>(stream.begin(), stream.begin() + 1000),
        std::vector<std::byte>(stream.begin() + 1000, stream.end()),
    };
    ASSERT_EQ(s.inbound_chunks[0].size(), 1000u);
    ASSERT_EQ(s.inbound_chunks[1].size(), 3097u);

    asio::io_context ioc;
    // Frozen (#377): never advanced, so the deadline cannot fire and cannot
    // race this cell's mechanism. See the timebase note at the top of file.
    fixpp::core::mock_clock clock{{}, {}, ioc.get_executor()};
    mock_transport mt{ioc.get_executor(), std::move(s)};
    std::vector<std::byte> buf;

    auto fut = asio::co_spawn(
        ioc, read_first_frame_bounded(mt, buf, clock, std::chrono::milliseconds{1000}, kMaxBytes),
        asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(ioc, fut, "ReadFirstFrameBounded::B2")) {
        return;
    }
    expected_t<std::size_t> const result = fut.get();

    EXPECT_TRUE(result.has_value())
        << "B2 (SC-012): expected success (frame length " << kLogonLen << "), got "
        << describe(result) << " — pre-fix rejects on the SECOND read at site B "
        << "(:96, `4097 >= 4096`) before framer.feed ever runs on the newly-read bytes, "
        << "discarding a frame that was already complete in the accumulated buffer.";
    if (result.has_value()) {
        EXPECT_EQ(*result, kLogonLen) << "B2 (SC-012): the admitted frame's exact length.";
    }
}

// ── B5 (edge / FR-013) ────────────────────────────────────────────────────────
// The clamp's `room == 1` case. Script::inbound_chunks (mechanism 5) =
// {4096 B (never a complete frame), 1 B}; deadline 50ms; read_latency 3ms
// (non-zero and load-bearing per D-6.11 — required for TERMINATION of the mutant
// this cell will later be run against at T019, not for pre-fix termination,
// which reaches its outcome after one read regardless). Driven with ioc.run().
//
// The delivered design requests exactly `room` bytes per read: {4096, 1}. Pre-fix
// NEVER clamps the request (:83-84 — always the full 4096-byte read_buf) AND
// rejects at site B immediately after the FIRST read (4096 >= 4096, before feed),
// so it never issues a second read at all. RED here is attributable to the
// MISSING CLAMP: read_sizes() stays length-1 ({4096}) instead of reaching a
// second, room-clamped request of 1.
//
// The outcome leg (wire_frame_too_large) is a SUPPORTING PIN, not a RED leg: both
// pre-fix (via site B on the first read) and the delivered design (via F1 after
// the clamped second read) reject this input with the same error — recorded so
// this does not overclaim a second RED.
// ⚠️ 5 s, NOT 50 ms — a termination bound, not a competitor. B4's derivation
// below applies verbatim: a real `steady_timer` deadline racing mock reads is
// decided by machine load, and this cell asserts the BUDGET decision, so the
// deadline must not be able to win. B5 did NOT go red in campaign run
// 33977674899; it has B4's shape and margin and was simply luckier, which is
// why it is raised too. Read B4 for the numbers and the derivation — they are
// stated once, there, so a re-measurement corrects one copy.
TEST(ReadFirstFrameBounded, B5) {
    constexpr std::size_t kMaxBytes = 4096;
    constexpr auto kDeadline = std::chrono::seconds{5};

    // A well-formed header whose declared BodyLength (200000) far exceeds what is
    // sent, so parse_frame classifies it partial — "no complete frame ever"
    // (same construction as engine_firstframe_test.cpp's
    // make_carried_over_budget_payload).
    std::string never_completes = std::string("8=FIX.4.2\x01") + "9=200000\x01";
    ASSERT_LT(never_completes.size(), kMaxBytes);
    never_completes.append(kMaxBytes - never_completes.size(), 'X');
    ASSERT_EQ(never_completes.size(), kMaxBytes);

    Script s;
    s.inbound_chunks = {to_bytes(never_completes), to_bytes(std::string(1, 'X'))};
    s.read_latency = std::chrono::milliseconds{3};

    asio::io_context ioc;
    // Frozen (#377): never advanced, so the deadline cannot fire and cannot
    // race this cell's mechanism. See the timebase note at the top of file.
    fixpp::core::mock_clock clock{{}, {}, ioc.get_executor()};
    mock_transport mt{ioc.get_executor(), std::move(s)};
    std::vector<std::byte> buf;

    auto fut = asio::co_spawn(
        ioc, read_first_frame_bounded(mt, buf, clock, kDeadline, kMaxBytes),
        asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(ioc, fut, "ReadFirstFrameBounded::B5")) {
        return;
    }
    expected_t<std::size_t> const result = fut.get();

    std::vector<std::size_t> const sizes = mt.read_sizes();
    std::vector<std::size_t> const expected_sizes{kMaxBytes, 1};
    EXPECT_EQ(sizes, expected_sizes)
        << "B5 (FR-013): expected read_sizes() == " << describe_sizes(expected_sizes)
        << " (the second request clamped to room=1). Got " << describe_sizes(sizes)
        << " — pre-fix issues only ONE unclamped 4096-byte request, then rejects at site B "
        << "before ever requesting again.";

    EXPECT_FALSE(result.has_value())
        << "B5 (FR-013) [supporting pin]: expected wire_frame_too_large (not admitted), got "
        << describe(result);
    if (!result.has_value()) {
        EXPECT_EQ(result.error(), error::wire_frame_too_large)
            << "B5 (FR-013) [supporting pin]: got " << describe(result);
    }
}

// ── T1 (SC-005 / SC-006) ──────────────────────────────────────────────────────
// The timer defect: the deadline's `timer.async_wait` handler captures
// coroutine-frame locals (`timed_out`, `transport`) BY REFERENCE (pre-fix
// :59-70). When the read completion and the deadline both expire with no
// handler having run (elapse-then-poll below), asio's timer queue releases
// them in expiry order — the shorter-latency read first — so the coroutine
// finds its frame, calls `timer.cancel()` (too late: the deadline handler is
// already queued and CANNOT be un-queued — research.md D-6.2/[[feedback_
// steady_timer_cancel_cannot_unqueue_a_completed_handler]]) and returns,
// freeing its frame. The deadline handler then runs and writes into that
// freed frame before calling `transport.cancel()` — a heap-use-after-free
// under ASan.
//
// Construction is D-6.2's elapse-then-poll, verbatim, NOT the "0ms deadline,
// inline read" first draft rejected there (no suspension point exists between
// the pre-fix `timer.async_wait` and the read, so a 0ms/inline construction
// reaches `timer.cancel()` before the scheduler ever runs and the RED never
// fires — a clean ASan run would then be misrecorded as "no finding").
//
// asio::detached, NOT use_future (unlike every cell above): per research.md
// D-6.3, the binding rule is to never `co_await` the helper from an enclosing
// TEST *coroutine* — that is what lets HALO elide the inner frame into an
// outer one that is still alive when the stranded handler fires, silently
// destroying the proof. That rule is about the *caller shape*, not the
// completion token: `read_first_frame_bounded(...)` is called here from the
// TEST body, which is not itself a coroutine, so no HALO-enabling co_await
// context exists regardless of the token used to observe the spawn's result.
// We therefore use `asio::detached` for the spawn itself (matching the
// brief/D-6.2 literally) but capture the coroutine's actual return value via
// a plain (non-coroutine) completion-handler lambda passed alongside it —
// this is still HALO-neutral for the reason above, and unlike `buf.size()`
// (which goes non-zero the instant a read lands data, whether or not a frame
// was ever found — an over-claiming proxy, not this cell's own defect but
// worth naming) it is the DIRECT observable of "the helper returned the
// frame's length" that D-6.2 names as the post-fix criterion.
TEST(ReadFirstFrameBounded, T1) {
    constexpr std::size_t kMaxBytes = 4096;
    constexpr std::size_t kLogonLen = 1024;  // well under max_bytes; also the smallest
                                             // value make_logon_of_length's 4-digit
                                             // BodyLength assumption admits.
    // ⚠️ THESE FIGURES ARE A MARGIN AGAINST THE *ARM GAP*, NOT A CADENCE.
    // What decides this cell is the ORDER of two real expiry instants:
    //   read     expires at  t_read_arm + kReadLatency
    //   deadline expires at  t0         + kDeadline   (t0 = the helper's entry,
    //                                                  where abs_deadline is computed)
    // so the read wins iff  (t_read_arm - t0) + kReadLatency < kDeadline. Both arms
    // are established inside step 1's single poll(), so that gap is microseconds of
    // CPU work — until the runner deschedules the process between them. At the
    // original 10 ms / 1 ms it took only a 9 ms gap to invert the order, and
    // windows-msvc-asan under `ctest --parallel 4` produced one (run 34074957982:
    // T1 reported `transport: handshake timeout` — the deadline winning — in 377 ms
    // against a ~55 ms nominal).
    //
    // ⚠️ AND WIDENING THE FIGURE IS *NOT* WHAT FIXES IT — that is the move #377
    // recorded against itself at the top of this file, and re-running it here
    // would earn the same epitaph. The margin below is only the cheap half. The
    // load-bearing half is that BOTH staging preconditions are CHECKED, and a
    // starved staging is RETRIED rather than reported: starvation is a property
    // of the runner, and a cell that cannot reach its own starting state has not
    // observed anything about the code under test. If every attempt is starved
    // the cell still fails — naming the harness, with the measured span.
    //
    // Re-derive both halves with `ci/red-arms/first-frame-arm-gap.sh`, which
    // reproduces the pre-fix inversion on Linux under load and re-runs this
    // construction against the same load.
    constexpr auto kDeadline = std::chrono::milliseconds{500};
    constexpr auto kReadLatency = std::chrono::milliseconds{50};
    constexpr int kAttempts = 4;

    std::vector<std::byte> const frame = make_logon_of_length(kLogonLen);
    ASSERT_EQ(frame.size(), kLogonLen);

    std::chrono::milliseconds worst_poll{0};

    for (int attempt = 1; attempt <= kAttempts; ++attempt) {
        Script s;
        s.inbound_bytes = frame;
        s.read_latency = kReadLatency;

        // ⚠️ `ioc` FIRST, so it is destroyed LAST. Nothing holding an executor
        // taken from the context may outlive the context — see PR #301's
        // measured heap-use-after-free. That ordering also makes the retry path
        // below safe to construct in a loop.
        asio::io_context ioc;
        // REAL clock (#377): this cell's subject is a genuine elapsed wait — a
        // frozen clock would make it vacuous. See the timebase note at top of
        // file for why B6's advanced-mock-clock construction is NOT available
        // here: a mock sleep completes by `asio::post`, and a posted handler's
        // order against an already-expired steady_timer is an asio
        // implementation detail, not a property this cell may assert. Both
        // instants have to come out of ONE timer queue.
        fixpp::core::system_clock_source clock{ioc.get_executor()};
        mock_transport mt{ioc.get_executor(), std::move(s)};
        std::vector<std::byte> buf;

        std::optional<expected_t<std::size_t>> result;
        asio::co_spawn(ioc, read_first_frame_bounded(mt, buf, clock, kDeadline, kMaxBytes),
                       [&result](std::exception_ptr ep, expected_t<std::size_t> r) {
                           EXPECT_FALSE(ep) << "T1: the spawned coroutine threw.";
                           result = std::move(r);
                       });

        // Step 1: run the spawn to its first real suspension. co_spawn's initial
        // resume is posted, not inline, so this poll() call executes the
        // coroutine synchronously through the helper's deadline resolution
        // (`clock.steady_now() + deadline`, which since #377 is arithmetic rather
        // than a timer arm and cannot suspend) and the callback-form
        // timer.async_wait(...) registration (async_wait with a callback starts the
        // wait without co_await'ing it) until it reaches the genuine suspension
        // inside async_read_some: the mock's own kReadLatency co_await.
        //
        // ⚠️ The deadline's own timer is created LATER than it used to be — #377
        // moved it inside await_deadline, i.e. inside the join, so it is armed on
        // the first loop iteration rather than before the loop. It is armed by the
        // time this poll() returns (the join is what suspends), which is all this
        // step needs; nothing is expired yet, so poll() returns with work
        // outstanding.
        //
        // The span of this poll() is measured because it UPPER-BOUNDS the arm
        // gap: t0 and t_read_arm both fall inside it.
        auto const poll_begin = std::chrono::steady_clock::now();
        ioc.poll();
        auto const poll_span = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - poll_begin);
        worst_poll = std::max(worst_poll, poll_span);

        // The two staging preconditions, checked rather than assumed:
        //
        //  (1) `poll_span < kReadLatency`, observed directly as `buf.empty()`.
        //      poll() drains for as long as handlers keep becoming ready, so a
        //      process descheduled INSIDE it can come back to an expired read
        //      timer and dispatch the read's own completion here. The frame is
        //      then found before the deadline could ever be expired, and every
        //      assertion below passes without D-6.2's race having happened —
        //      silently vacuous. MEASURED: a 97 ms poll against a 50 ms latency,
        //      1 run in 30 under 250 spinning hogs on linux-clang-asan. `buf` is
        //      appended per completed read (read_first_frame_bounded.hpp,
        //      `buf.insert(...)`), so emptiness is the direct observable.
        //
        //  (2) `poll_span + kReadLatency < kDeadline` — a sufficient condition
        //      for the read's expiry to precede the deadline's, since
        //      t0 >= poll_begin and t_read_arm <= poll_begin + poll_span. This
        //      is the one run 34074957982 broke.
        if (!buf.empty() || poll_span + kReadLatency >= kDeadline) {
            // Retire this attempt before retrying: the coroutine is still
            // suspended and holds references to `buf` and `mt`. run() returns
            // once it completes — bounded by the deadline arm, which fires at
            // t0 + kDeadline at the latest whichever way the staging went.
            ioc.run();
            continue;
        }

        // Step 2: elapse BOTH absolute deadlines with no handler running at all
        // (the context is not being driven). Derived from kDeadline rather than
        // written as a literal, so changing the margin above cannot leave a
        // sleep that no longer covers it.
        std::this_thread::sleep_for(kDeadline + std::chrono::milliseconds{100});

        // Step 3: both timers are now expired. asio's timer queue is a heap
        // ordered by expiry, so the ready queue is drained in expiry order: the
        // kReadLatency read completion first (resumes the coroutine -> feeds ->
        // finds the frame -> timer.cancel() [cannot un-queue the already-ready
        // deadline handler] -> co_return, freeing the frame), then the kDeadline
        // handler (writes into the freed frame, then transport.cancel()).
        // Precondition (2) is what makes "in expiry order" mean "read first".
        ioc.poll();

        // Drain to completion (D-6.2: "after the context is drained to
        // completion") in case anything remains posted (e.g. the completion
        // handler's own dispatch).
        ioc.run();

        ASSERT_TRUE(result.has_value())
            << "T1: the spawned coroutine never completed — the drain above is insufficient, not "
            << "the elapse-then-poll construction. Without a completed result, "
            << "cancels_observed()==0 would be a vacuous pass.";
        EXPECT_TRUE(result->has_value())
            << "T1 (SC-005/SC-006): expected the first frame's length (" << kLogonLen << "), got "
            << describe(*result) << " — the read that raced the deadline must still win the frame."
            << " Staging was verified before this ran (poll span " << poll_span.count()
            << " ms, read " << kReadLatency.count() << " ms, deadline " << kDeadline.count()
            << " ms), so this is NOT the arm-gap starvation of run 34074957982.";
        if (result->has_value()) {
            EXPECT_EQ(**result, kLogonLen)
                << "T1 (SC-005/SC-006): the admitted frame's exact length.";
        }
        EXPECT_EQ(mt.async_reads_observed(), 1u)
            << "T1: expected exactly one read (the whole frame arrives in it) — a second read "
            << "would mean framer.feed found nothing on the first and this cell is not exercising "
            << "the frame-found race D-6.2 describes.";

        EXPECT_EQ(mt.cancels_observed(), 0u)
            << "T1 (SC-005/SC-006) [S5 proxy — research.md D-6.2/N3, not the full postcondition: "
            << "this observes that no cancel() RAN, which is narrower than 'no handler armed by "
            << "this call is outstanding on return']: expected zero cancel() calls after the "
            << "context drains. A nonzero count means the stranded deadline handler survived the "
            << "coroutine's return and called transport.cancel() through its dangling reference — "
            << "the pre-fix defect this cell targets. Under linux-clang-asan this must instead "
            << "manifest as a heap-use-after-free abort (the write to `timed_out` lands first); a "
            << "clean ASan run here means the proof did not fire, not that there is no defect "
            << "(D-6.3).";
        return;  // one STAGED attempt is the whole cell.
    }

    FAIL() << "T1 [staging, NOT a claim about the code under test]: all " << kAttempts
           << " attempts were starved before the cell reached its own starting state — the worst "
           << "step-1 poll() ran for " << worst_poll.count()
           << " ms against kReadLatency=" << kReadLatency.count()
           << " ms and kDeadline=" << kDeadline.count()
           << " ms. Either the runner descheduled this process for that long on every attempt, "
           << "or the mock's read latency is no longer being honoured. Nothing here says the read "
           << "lost a race it should have won; run 34074957982 is the failure this message exists "
           << "to stop being mistaken for.";
}

// ── B4 (SC-003) — regression guard, NOT a RED cell against `main` ────────────
// Over-budget, no complete frame ever (a declared BodyLength of 200000 that
// never completes, X-padded to exactly max_bytes + 1 bytes — same shape as
// `engine_firstframe_test.cpp`'s `make_carried_over_budget_payload`). Per
// research.md D-6.7's per-cell RED-basis table: "none — GREEN on pre-fix
// source. A `budget + 1` no-frame payload is rejected identically by `>=`
// and `>`." Pre-fix rejects after ONE unclamped 4096-byte read (site B,
// `4096 >= 4096`, before feed); the delivered design rejects after TWO
// clamped reads (4096 then the room-clamped 1, `4097 > 4096` at the foot,
// AFTER feed found nothing) — same outcome, different mechanism, so no A/B
// against `main` can discriminate this cell. It proves the fix does not
// relax FR-003/FR-014's protective intent, not that the fix changed anything
// observable here.
//
// The two supporting pins (`async_reads_observed() == 2`, `buf.size() ==
// kMaxBytes + 1`) exist so this cell asserts the MECHANISM D-6.9 credits it
// with (the step-5 budget decision, "must fire") rather than merely a token
// match on the error value: `framer.feed` can independently return
// `wire_frame_too_large` on a badly-formed frame (D-6.8's FR-011 note), so a
// bare `error == wire_frame_too_large` check would pass "for the wrong
// reason" if some future change moved the rejection back into the framer.
//
// `read_latency = 3ms` (matching B5) is NOT needed for the assertion above —
// B4 is `read_latency`-silent for its own outcome, same as B5 was before
// D-6.11's correction. It is here because B4 shares B5's exposure to the
// `room == 0` mutant (`room = max_bytes - buf.size()`): once buf.size()
// reaches max_bytes, `want` clamps to 0, the mock returns a successful
// zero-byte completion (mechanism 6), and the loop re-enters forever unless
// a non-zero latency eventually lets the deadline arm be recorded first
// (research.md D-6.11's B5 derivation, D-6.8's round-4 correction). Without
// it this cell HANGS on that mutant instead of failing — it did, across two
// Gate A rounds (tasks.md T031). Giving it the same treatment B5 got does
// NOT earn B4 a matrix credit for that column — research.md D-6.11 states
// plainly "[w]hether B4 *also kills* this column is not claimed here", and
// none is claimed here either; the termination proof lives in the verify
// record, not as an added mutant-kill column.
// ⚠️ THE DEADLINE HERE IS A TERMINATION BOUND, NOT A COMPETITOR — 5 s, NOT 50 ms.
// This cell asserts that the BUDGET decision fires. The deadline exists only so
// a `room == 0` mutant fails instead of hanging (see the `room == 0` note in this cell). At 50 ms
// the two raced, and on `windows-msvc-asan` under `ctest --parallel 4` the
// deadline won: campaign run 33977674899 measured this cell at 735 ms against
// 6 ms unloaded — 122x — and it reported `transport: handshake timeout`. That
// is a VACUOUS run, not a product defect: the budget decision never happened,
// so nothing about it was tested.
//
// ⚠️ WHICH PINS FAIL DEPENDS ON *WHERE* THE DEADLINE LANDS, AND THE OBSERVED
// RUN FAILED ONLY TWO OF THE THREE. On Windows it was the error and
// `buf.size()`; `async_reads_observed() == 2` PASSED, because both reads had
// been ISSUED and the deadline won before the loop reached the budget decision
// at its foot. Force the deadline earlier — small enough that read 2 is never
// issued — and the read-count pin fails too. Two failures and three are the
// SAME defect at different points; neither count is a signature.
//
// ── RESOLVED (#377). THE RACE IS DELETED, NOT SHRUNK. ────────────────────────
// The history above is kept because it is what makes the fix legible; the fix
// itself is that THIS CELL NO LONGER RUNS A WALL CLOCK.
//
// PR #376's interim answer was to raise the deadline 50 ms -> 5 s, derived as
// ~7x the observed 735 ms. It said of itself that 5 s was a GUESS and not a
// proof, so a slow enough runner would reproduce the same vacuous run, just
// rarely. #377 removed the quantity instead: `read_first_frame_bounded` now
// takes a `fixpp::core::Clock&`, and this cell passes a FROZEN mock_clock that
// is never advanced. The deadline cannot fire at any runner speed.
//
// ⚠️ `kDeadline` BELOW IS THEREFORE INERT, and is kept only so the call reads
// like every other cell's. Do not tune it, do not "restore" 50 ms, and do not
// read it as a live bound — under this clock no value of it changes anything.
// If you find yourself reasoning about its magnitude, the clock has been
// changed out from under this comment.
//
// ⚠️ The deadline arm is still ARMED, still JOINED, and still CANCELLED by the
// group on the winning path — freezing removes only its ability to win a race
// it was never meant to enter. That the arm is genuinely live is demonstrated
// on the other side by B6 (the deadline firing) and T2a (the arm's cancel
// being delivered); both were measured against mutants after the port.
TEST(ReadFirstFrameBounded, B4) {
    constexpr std::size_t kMaxBytes = 4096;
    constexpr auto kDeadline = std::chrono::seconds{5};

    std::string never_completes = std::string("8=FIX.4.2\x01") + "9=200000\x01";
    ASSERT_LT(never_completes.size(), kMaxBytes + 1);
    never_completes.append(kMaxBytes + 1 - never_completes.size(), 'X');
    ASSERT_EQ(never_completes.size(), kMaxBytes + 1);

    Script s;
    s.inbound_bytes = to_bytes(never_completes);
    s.read_latency = std::chrono::milliseconds{3};

    asio::io_context ioc;
    // Frozen (#377): never advanced, so the deadline cannot fire and cannot
    // race this cell's mechanism. See the timebase note at the top of file.
    fixpp::core::mock_clock clock{{}, {}, ioc.get_executor()};
    mock_transport mt{ioc.get_executor(), std::move(s)};
    std::vector<std::byte> buf;

    auto fut = asio::co_spawn(ioc, read_first_frame_bounded(mt, buf, clock, kDeadline, kMaxBytes),
                              asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(ioc, fut, "ReadFirstFrameBounded::B4")) {
        return;
    }
    expected_t<std::size_t> const result = fut.get();

    EXPECT_FALSE(result.has_value())
        << "B4 (SC-003): expected the over-budget, never-completing payload to be rejected, got "
        << describe(result);
    if (!result.has_value()) {
        EXPECT_EQ(result.error(), error::wire_frame_too_large)
            << "B4 (SC-003): expected wire_frame_too_large (FR-003/FR-014's protective intent), "
               "got "
            << describe(result)
            << ". ⚠️ If that reads `transport: handshake timeout`, this run is VACUOUS rather "
               "than a product failure: the deadline beat the budget decision, so the mechanism "
               "under test never ran. Expect the `buf.size()` pin below to fail with it; the "
               "read-count pin fails only if the deadline landed early enough that the second "
               "read was never issued. The deadline is a termination bound and must not compete "
               "— see the derivation above this cell before touching any assertion.";
    }
    EXPECT_EQ(mt.async_reads_observed(), 2u)
        << "B4 (SC-003) [mechanism pin]: expected exactly two reads (4096 then the room-clamped 1) "
           "— "
        << "the step-5 budget decision, not an earlier framer-level reject.";
    EXPECT_EQ(buf.size(), kMaxBytes + 1)
        << "B4 (SC-003) [mechanism pin]: expected buf to hold exactly max_bytes + 1 bytes when the "
        << "budget decision fires.";
}

// ── B6 (SC-004 / D-1b) ────────────────────────────────────────────────────────
// The arm-once deadline. `max_bytes = 200`, `deadline = 50 ms`, `read_latency
// = 7 ms`, `inbound_chunks` = 201 chunks of 1 byte (mechanism 5). The cell is
// STAGED, not raced: it pumps until the loop has genuinely completed a read
// (observed on `buf`), and only then advances the mock clock by exactly
// `kDeadline` so the arm-once deadline is reached. Both halves of the subject
// survive that — the deadline still fires, and it still fires with the loop
// mid-flight and `buf.size()` far below the 200-byte budget.
//
// ⚠️ WHAT THIS REPLACED, AND WHY IT IS NOT A WEAKENING. Until run
// 34074957982 this cell ran on the REAL clock and let the 7 ms read series
// race the 50 ms deadline. That race is decided by the ARM GAP — the deadline
// instant is fixed at the helper's entry (`abs_deadline = steady_now() +
// deadline`) while the first read's 7 ms timer is armed a few instructions
// later — so it inverts whenever the runner deschedules the process for
// >43 ms between the two. windows-msvc-asan under `ctest --parallel 4` did
// exactly that: ZERO reads completed inside the window and the cell failed its
// own non-vacuity check (`buf.size() >= 1`) while the code under test was
// correct. Widening 50 ms would have moved that coin, not removed it — the
// same interim move #377 recorded against itself at the top of this file. The
// advance removes the coin: nothing about WHEN the deadline fires is left to
// the scheduler.
//
// ⚠️ AND IT IS NOT THE FROZEN CLOCK EITHER — a frozen clock here would assert
// a timeout that can never happen, which is what the top-of-file note forbids.
// The distinction is advance-vs-freeze, not mock-vs-real; see that note.
//
// What the advance costs, stated rather than implied: the cell no longer
// witnesses that a REAL elapsed 50 ms reaches the deadline. That is a property
// of `system_clock_source`, which is covered on its own, not of this helper —
// the helper only ever sees `Clock&`. In exchange the cell gained a mutant it
// could not previously see: computing `abs_deadline` from
// `std::chrono::steady_clock::now()` instead of `clock.steady_now()` is GREEN
// under a real clock and RED here.
//
// `7 ms` is no longer load-bearing for D-6.11's reason (there is no longer a
// window for two real timer series to co-expire inside), but it is kept: it is
// what makes the staging pump non-trivial, and it keeps the re-arm mutant's
// drain at the measured ~1.4 s rather than changing a second variable.
//
// `buf.size()` is asserted only on the side that discriminates (`< kMaxBytes`,
// which excludes the mutant's terminal 201). The lower bound moved OUT of the
// assertions and INTO the staging pump — see the note at the assertion site
// ([[feedback_timing_band_witness_range_admits_the_mutant_it_claims_to_kill]]
// is why it was a band and not an exact count in the first place; that reason
// still stands for never pinning the exact read count).
//
// Mutant killed: a per-iteration RE-ARM. #377 moved where that mutant can be
// written — `await_deadline` now takes an ABSOLUTE instant, so re-sleeping to
// it is idempotent and the arm itself can no longer push the deadline forward.
// The mutant is therefore written one level up, on the line that computes
// `abs_deadline` in read_first_frame_bounded.hpp: recomputing
// `clock.steady_now() + deadline` per iteration. Under it the deadline is reset
// every 7 ms and never fires; the loop drains all 201 chunks and reaches
// `201 > 200` at the foot.
//
// The mutant's terminal values (`wire_frame_too_large`, `buf.size() == 201`)
// are unchanged by the staging: it never fires the deadline at all, so it
// drains all 201 chunks whatever the clock does. Re-derive by applying the
// re-arm to read_first_frame_bounded.hpp's `abs_deadline` line and running
// this cell alone; B6 is the only cell in the file that reddens under it.
TEST(ReadFirstFrameBounded, B6) {
    constexpr std::size_t kMaxBytes = 200;
    constexpr auto kDeadline = std::chrono::milliseconds{50};

    // A well-formed header whose declared BodyLength (200000) far exceeds what
    // is sent, so parse_frame classifies it partial forever — same
    // never-completing shape as B4/B5 — split into 201 one-byte chunks
    // (mechanism 5) so the framer is fed byte-by-byte across 201 reads.
    std::string never_completes = std::string("8=FIX.4.2\x01") + "9=200000\x01";
    ASSERT_LT(never_completes.size(), kMaxBytes + 1);
    never_completes.append(kMaxBytes + 1 - never_completes.size(), 'X');
    ASSERT_EQ(never_completes.size(), kMaxBytes + 1);
    std::vector<std::byte> const payload = to_bytes(never_completes);

    Script s;
    s.inbound_chunks.reserve(payload.size());
    for (std::byte b : payload) s.inbound_chunks.push_back({b});
    ASSERT_EQ(s.inbound_chunks.size(), 201u);
    s.read_latency = std::chrono::milliseconds{7};

    asio::io_context ioc;
    // ADVANCED mock_clock — NOT the frozen one the non-timeout cells use, and
    // no longer the real clock. See the timebase note at top of file for the
    // three-way distinction. The deadline still genuinely fires; what is gone
    // is its ability to fire BEFORE the loop has run, which is the only thing
    // the real clock ever contributed here and the thing that broke.
    fixpp::core::mock_clock clock{{}, {}, ioc.get_executor()};
    mock_transport mt{ioc.get_executor(), std::move(s)};
    std::vector<std::byte> buf;

    auto fut = asio::co_spawn(ioc, read_first_frame_bounded(mt, buf, clock, kDeadline, kMaxBytes),
                              asio::use_future);

    // Stage on the OBSERVABLE, then fire the deadline — instead of racing them.
    // `buf` is appended per completed read (read_first_frame_bounded.hpp,
    // `buf.insert(...)`), so this pump returns exactly when the loop has
    // genuinely run once. It cannot race the deadline: the clock has not been
    // advanced, so `sleep_until(abs_deadline)` is parked in mock_clock's waiter
    // map and CANNOT complete (src/core/test/mock_clock.cpp).
    ASSERT_TRUE(fixpp::test_support::pump_until(
        ioc, [&buf] { return !buf.empty(); }, fixpp::test_support::kPumpBudget,
        fixpp::test_support::kPumpSlice, "ReadFirstFrameBounded::B6::first-read"))
        << "B6 [non-vacuity, staged]: no read completed within the pump budget, so the loop never "
           "genuinely ran and the timeout below would be vacuous. This is the assertion the "
           "wall-clock construction used to make AFTER the fact (`buf.size() >= 1`), where a "
           "starved runner failed it by inverting the arm order rather than by any defect.";

    // ⚠️ ADVANCE IN TWO STEPS WITH AN ITERATION BETWEEN THEM. A single
    // `advance(kDeadline)` here is GREEN under the re-arm mutant and was
    // MEASURED so before this shape was settled: with the clock parked at its
    // seed the whole time, `steady_now() + deadline` and a hoisted
    // `abs_deadline` name the SAME instant, so a mutant that recomputes it
    // every iteration recomputes the same value and the cell cannot see the
    // difference. What discriminates them is mock time MOVING while the loop
    // iterates. Hence:
    //
    //   advance #1 (half)  → arm-once deadline (kDeadline) NOT yet reached
    //   one more read      → the arm the loop is now parked on was computed
    //                        AFTER the advance: kDeadline for arm-once,
    //                        1.5 * kDeadline for the re-arm mutant
    //   advance #2 (half)  → reaches kDeadline exactly: arm-once FIRES, the
    //                        mutant's does not, and the mutant then drains all
    //                        201 chunks into wire_frame_too_large.
    //
    // A lost advance is not a failure mode here: mock_clock::sleep_until
    // completes IMMEDIATELY for a deadline already <= steady_now
    // (src/core/test/mock_clock.cpp), so re-sleeping to an absolute instant the
    // clock has already passed fires rather than parking
    // ([[feedback_mock_clock_advance_before_timer_armed_race]] is about a
    // relative re-arm, which is what #377 removed from await_deadline).
    static_assert(kDeadline.count() % 2 == 0,
                  "the two half-advances must sum to kDeadline exactly");
    clock.advance(kDeadline / 2);

    std::size_t const staged = buf.size();
    ASSERT_TRUE(fixpp::test_support::pump_until(
        ioc, [&buf, staged] { return buf.size() > staged; }, fixpp::test_support::kPumpBudget,
        fixpp::test_support::kPumpSlice, "ReadFirstFrameBounded::B6::iterate-after-advance"))
        << "B6 [mutant discrimination]: no further read completed after the first half-advance, so "
           "no loop iteration re-entered the join at the new time and the second half-advance "
           "below would reach an arm-once deadline and a re-armed one alike — the cell would go "
           "GREEN under the very mutant it exists to kill.";

    clock.advance(kDeadline / 2);

    if (!fixpp::test_support::run_to_exhaustion_or_report(ioc, fut, "ReadFirstFrameBounded::B6")) {
        return;
    }
    expected_t<std::size_t> const result = fut.get();

    EXPECT_FALSE(result.has_value())
        << "B6 (SC-004/D-1b): expected the deadline to win before the budget could be reached, got "
        << describe(result);
    if (!result.has_value()) {
        EXPECT_EQ(result.error(), error::transport_handshake_timeout)
            << "B6 (SC-004/D-1b): expected transport_handshake_timeout — the arm-once deadline "
            << "reached by the advance above — got " << describe(result)
            << ". A per-iteration re-arm would put the deadline kDeadline beyond every advance "
            << "and the loop would drain all 201 chunks instead.";
    }
    // ⚠️ NO `EXPECT_GE(buf.size(), 1u)` HERE ANY MORE, deliberately. Under the
    // wall-clock construction that was the non-vacuity check and it is the
    // assertion windows-msvc-asan failed (run 34074957982, buf.size() == 0 —
    // the arm gap, not a defect). It is now established BEFORE the deadline can
    // fire, by the staging pump above. Re-adding it here would be an assertion
    // that cannot go RED: the pump does not return until `!buf.empty()`, and
    // nothing between there and here removes bytes from `buf`.
    EXPECT_LT(buf.size(), kMaxBytes)
        << "B6 (SC-004/D-1b): expected buf.size() far below the " << kMaxBytes << "-byte budget "
        << "— the re-arm mutant drains all 201 chunks, reaching buf.size() == 201.";
}

// ── T2a (SC-015 / FR-015) ──────────────────────────────────────────────────────
// `total`-cancellation delivery (D-2/D-6.12): the wrapper coroutine below is
// NOT a test convenience — co_spawn's initial cancellation state is
// terminal-only (C2), so without an outer co_spawn whose FIRST statement
// resets to enable_total_cancellation(), the test's own `signal.emit(total)`
// dies before ever reaching read_first_frame_bounded's internal join, and the
// cell would fail with the delivered code CORRECT.
//
// Asserts a cancellation-ATTRIBUTABLE error SET
// (transport_read_cancelled OR transport_handshake_timeout), not the exact
// value: under the delivered design BOTH arms of the internal `||` join are
// cancelled together, and which one is recorded as order[0] (unspecified asio
// scheduler internals — D-6.4/D-6.10a) decides which surfaces. Binding the
// exact value would risk a RED on correct code (research.md D-6.1's T2a row,
// widened at Gate A round 4).
//
// Mutant killed: the BARE deadline arm — await_deadline (read_first_frame_
// bounded.hpp) replaced by a raw timer.async_wait(use_awaitable), dropping
// both the total-cancel reset and redirect_error. Per D-6.12b this mutant
// does NOT change the RETURNED VALUE (order[0]==0 either way, so the join
// still surfaces the read arm's transport_read_cancelled) — the
// discriminator is PROMPTNESS. Under the mutant the bare arm's own cancel is
// silently dropped (its terminal-only IN filter never sees `total`), it is
// never re-cancelled (the group's one-shot guard was already consumed
// cancelling the read arm), and it runs to the FULL `kDeadline` before the
// join retires — even though the read arm itself aborted almost immediately.
//
// ── THE PROMPTNESS CONSTRUCTION (#359 item 1, on top of #377's clock) ────────
// There is no longer ANY wall-clock quantity in the discriminator.
//
// WHAT IT WAS. An intermediate 100 ms `asio::steady_timer` the test owned, armed
// against a 500 ms helper deadline, with a flag set inside the timer's own
// handler if `result` was still unset. A 5x margin — and the same construction
// #357 removed from both cells in first_frame_stop_test.cpp for carrying a
// latent false-failure mode: a process stall >= 100 ms after the arm makes the
// reactor find the timer expired and enqueue its handler AHEAD of the join's
// remaining work, setting the flag although the join did no extra work. T2a's
// margin was TIGHTER than the 10x the engine cells had when one of them
// false-failed in CI. It was never observed failing here; it was a construction
// argument, and it is now moot rather than argued.
//
// WHAT IT IS. The clock is a FROZEN mock_clock, so the deadline CANNOT fire.
// The only way this join can retire is for the group's cancel to actually reach
// the deadline arm and abort its sleep. So:
//
//   delivered  — total reaches the arm (await_deadline resets to total), the
//                arm's sleep is slot-cancelled, both arms retire, `result` is
//                set.
//   mutant     — the arm's own cancel is silently dropped (terminal-only IN
//                filter never sees `total`), nothing ever wakes the frozen
//                clock's waiter, and the join CANNOT retire. Ever.
//
// MEASURED, both arms, against the ported code (linux-clang-asan):
//     delivered                              PASS,   0 ms
//     mutant (bare `co_await clock.sleep_until`, no reset, no absorption)
//                                            FAIL, 2003 ms — this assertion
// and T2a is the ONLY cell in the file that reddens under it, which is what
// "D-6.12b is witnessed at HELPER scope" means concretely. The binary's hash
// was compared across the pair, so the mutant is known to have reached it —
// a rebuild that silently did nothing would otherwise report the healthy
// result twice and read as a lethal mutant surviving.
//
// The assertion is therefore "did the join retire at all", which is structural.
//
// ⚠️ THE WATCHDOG IS A HANG-TO-MESSAGE CONVERTER, NOT THE DISCRIMINATOR, and its
// budget is derived from the ONE competing quantity rather than picked round.
// Under the mutant the coroutine stays outstanding, so co_spawn's work guard
// keeps the io_context alive and `run_one()` would BLOCK rather than return 0 —
// a 120 s ctest kill with no message instead of a named failure. `run_one_for`
// converts that into the assertion below.
//
// The budget must stay well under this cell's `read_latency`. That timer is
// REAL (mock_transport's own, which no Clock governs), and if it elapsed the
// read arm would complete NORMALLY, retiring the join and turning the mutant
// GREEN — a spurious hit, the exact class #337 shipped when a longer bounded
// pump let a real steady_timer satisfy a wait the mock clock was supposed to
// govern.
//
// ⚠️ THE WATCHDOG IS NOT THE ONLY THING THAT HAS TO CLEAR THAT TIMER, and the
// first version of this note missed the other half. Capping the watchdog only
// bounds how long THIS LOOP waits; the real read timer is running the whole
// time regardless, so a process stall longer than `read_latency` retires the
// join on its own and the mutant passes — with the watchdog never involved.
// That escape is bounded by `read_latency` and by nothing else, so the fix is
// to put `read_latency` beyond any stall this cell could plausibly survive
// rather than merely above the watchdog: 1 h against a 2 s budget is ~1800x,
// where 10 s was 5x. The delivered path retires in ~1 ms.
//
// ⚠️ `read_latency` here is a NEVER, not a duration to tune. The read must be
// resolved ONLY by cancellation; if you find yourself reasoning about how long
// it is, the cell has stopped testing what it claims to.
TEST(ReadFirstFrameBounded, T2a) {
    constexpr auto kJoinWatchdog = std::chrono::seconds{2};
    constexpr std::size_t kMaxBytes = 4096;
    // Inert under the frozen clock (it cannot fire); kept so the call reads like
    // every other cell's. The promptness discriminator is the join retiring, not
    // this value — see the construction note above.
    constexpr auto kDeadline = std::chrono::milliseconds{500};

    Script s;
    // Effectively NEVER: the read must be resolved ONLY by cancellation. This is
    // the mock's own REAL timer, which the frozen clock does not govern, so it is
    // the cell's second exit from the join — see the note above. 1 h makes that
    // exit unreachable in practice instead of merely unlikely.
    s.read_latency = std::chrono::hours{1};

    asio::io_context ioc;
    // Frozen (#377): never advanced, so the deadline cannot fire and cannot
    // race this cell's mechanism. See the timebase note at the top of file.
    fixpp::core::mock_clock clock{{}, {}, ioc.get_executor()};
    mock_transport mt{ioc.get_executor(), std::move(s)};
    std::vector<std::byte> buf;

    asio::cancellation_signal signal;
    bool entered_helper = false;
    std::optional<expected_t<std::size_t>> result;
    std::exception_ptr thrown;

    asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            entered_helper = true;
            result = co_await read_first_frame_bounded(mt, buf, clock, kDeadline, kMaxBytes);
        },
        asio::bind_cancellation_slot(signal.slot(), [&](std::exception_ptr ep) { thrown = ep; }));

    // Positive initiation barrier (D-6.13a): poll() until entered_helper is
    // set, then confirm a FOLLOWING poll() leaves `result` unset — genuinely
    // suspended inside the join, not merely queued. An unset completion flag
    // alone would be satisfied by a coroutine that never ran at all — exactly
    // what C2's bug produces (D-6.13a(a); withdrawn round-2 form).
    for (int i = 0; i < 10'000 && !entered_helper; ++i) ioc.poll();
    ASSERT_TRUE(entered_helper)
        << "T2a: the wrapper coroutine never reached the co_await of the subject "
        << "helper — vacuous cell (D-6.13a).";
    ioc.poll();
    ASSERT_FALSE(result.has_value())
        << "T2a: the helper completed before cancellation was ever emitted — the "
        << "barrier cannot certify suspension inside the join.";
    EXPECT_GE(mt.async_reads_observed(), 1u)
        << "T2a (mechanism 4): no read was initiated before cancellation — the read "
        << "arm never became genuinely in-flight, so this cell would not exercise "
        << "D-2's join at all.";

    signal.emit(asio::cancellation_type::total);

    // THE promptness assertion (SC-015/FR-015, D-6.12b) — see the construction
    // note above this cell. It kills the bare-deadline-arm mutant, and it does
    // so without comparing any elapsed time to any constant.
    while (!result.has_value()) {
        ASSERT_GT(ioc.run_one_for(kJoinWatchdog), 0u)
            << "T2a (SC-015/FR-015): the join did not retire. The clock here is a FROZEN "
            << "mock_clock, so the deadline cannot fire and the ONLY way out of the join is "
            << "for the group's cancel to reach the deadline arm and abort its sleep. That "
            << "did not happen, which is exactly what the bare-deadline-arm mutant does: "
            << "await_deadline without its reset to enable_total_cancellation() never sees "
            << "`total` through its terminal-only IN filter, so its sleep is never woken. "
            << "⚠️ Do NOT 'fix' this by raising kJoinWatchdog — at 10s this cell's own "
            << "read_latency would retire the join instead and the mutant would pass.";
    }

    ASSERT_FALSE(thrown) << "T2a: the wrapper coroutine threw.";

    EXPECT_TRUE(is_cancellation_attributable(*result))
        << "T2a (SC-015): expected a cancellation-attributable outcome (transport_read_"
        << "cancelled or transport_handshake_timeout), got " << describe(*result);
}

// ── CovSharedClockSweep (#377 round 2) — cancel_sleeps() MUST NOT drop a Logon ──
// THE HAZARD #377 CREATED, and the only cell that can see it.
//
// `Clock::cancel_sleeps()` is GLOBAL over the whole clock — it sweeps every
// registered sleeper, with no session scoping. Before #377 the first-frame
// deadline was a private asio::steady_timer registered with no Clock, so nothing
// could reach it. Putting it on the shared engine clock made the accept path
// reachable from every other session's ROUTINE traffic: a Session in LogoutSent
// receiving the peer's confirming 35=5 calls cancel_sleeps() to wake its own
// logout wait (src/session/session.cpp, LogoutSent case), and absent a
// per-session clock_override that Session's clock IS the engine clock.
//
// Without the disambiguation in await_deadline, that sweep completes the deadline
// arm, the join reports outcome.index() == 1, and the engine rejects a HEALTHY
// inbound connection as transport_handshake_timeout and closes it with no log at
// that site — one unrelated Logout silently dropping one inbound Logon.
//
// ⚠️ THE OTHER NINE CELLS CANNOT SEE THIS, BY CONSTRUCTION: each drives a clock
// with exactly one sleeper, so a global sweep and a per-op cancel are
// indistinguishable there. This cell is the one that separates them, which is why
// it exists rather than being folded into an existing cell.
//
// RED ARM (measured, do not assume): delete the `cs.cancelled()` /
// `steady_now() >= deadline` disambiguation in await_deadline so the sweep is
// treated as the join's cancel. This cell then returns transport_handshake_timeout
// instead of the frame length.
TEST(ReadFirstFrameBounded, CovSharedClockSweep) {
    constexpr std::size_t kMaxBytes = 4096;
    constexpr auto kDeadline = std::chrono::seconds{5};

    // A complete Logon, delivered in two chunks so the loop genuinely iterates and
    // the sweep lands with a read in flight rather than before the first one.
    // 2048 keeps make_logon_of_length's 4-digit-BodyLength precondition satisfied
    // (the same reason the other cells use kMaxBytes-scale lengths).
    std::vector<std::byte> const payload = make_logon_of_length(2048);
    ASSERT_EQ(payload.size(), 2048u) << "CovSharedClockSweep: fixture did not build a frame.";
    Script s;
    s.inbound_chunks.emplace_back(payload.begin(), payload.begin() + 8);
    s.inbound_chunks.emplace_back(payload.begin() + 8, payload.end());
    s.read_latency = std::chrono::milliseconds{2};

    asio::io_context ioc;
    // Frozen: the deadline must not fire. The sweep below is the subject.
    fixpp::core::mock_clock clock{{}, {}, ioc.get_executor()};
    mock_transport mt{ioc.get_executor(), std::move(s)};
    std::vector<std::byte> buf;

    auto fut = asio::co_spawn(ioc, read_first_frame_bounded(mt, buf, clock, kDeadline, kMaxBytes),
                              asio::use_future);

    // Drive until the helper is genuinely suspended inside the join with a read in
    // flight — a sweep before that would not exercise the deadline arm at all.
    for (int i = 0; i < 10'000 && mt.async_reads_observed() == 0; ++i) ioc.poll();
    ASSERT_GE(mt.async_reads_observed(), 1u)
        << "CovSharedClockSweep: no read was ever initiated — the sweep below would land "
           "before the deadline arm existed, making this cell vacuous.";

    // The sweep. This is what a routine Logout on ANY other session does.
    clock.cancel_sleeps();

    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, fut, "ReadFirstFrameBounded::CovSharedClockSweep")) {
        return;
    }
    expected_t<std::size_t> const result = fut.get();

    ASSERT_TRUE(result.has_value())
        << "CovSharedClockSweep (#377): a global Clock::cancel_sleeps() — which routine "
        << "traffic on an UNRELATED session performs — aborted this first-frame read and the "
        << "connection would have been closed as a handshake timeout. The deadline arm must "
        << "distinguish `the join cancelled me` from `the whole clock was swept`; both arrive "
        << "as operation_aborted, so the clock itself is the oracle. Got " << describe(result);
    EXPECT_EQ(*result, payload.size())
        << "CovSharedClockSweep: expected the complete Logon to be returned intact after the "
        << "sweep, not a truncated or partial frame.";
}

// ── COVERAGE CELL — framer-error propagation (Article IX §1) ─────────────────
// NOT one of the 13 mutation-proven witness cells and deliberately not named
// like one. Added at /speckit-verify to close a genuine uncovered error path:
// the `co_return std::unexpected(feed_r.error())` arm was the only 088-owned
// uncovered line in read_first_frame_bounded.hpp, and Article IX §1 makes an
// error return "genuine by default" — it must be TESTED, not waived.
//
// Construction: a payload that does not begin "8=" makes Framer::parse_frame
// reject with wire_framing_resync (src/wire/framer.cpp:79/:85) rather than
// merely carrying the bytes forward. It is kept far below the budget so the
// step-5 budget check cannot fire first — this cell must exercise the FRAMER
// arm specifically, not the budget arm that B1-B4 already cover.
// ⚠️ 5 s, NOT 50 ms — the deadline is a termination bound, not a competitor.
// This cell asserts the FRAMER arm fires. B4's sibling defect applies verbatim:
// a real `steady_timer` deadline racing a one-read mock is decided by machine
// load, and on `windows-msvc-asan` under `--parallel 4` that race is lost (B4
// measured 122x its unloaded time in campaign run 33977674899). This cell did
// not go red there, but it has the same shape and a smaller margin of work, so
// it was luck rather than design. No assertion here reads the deadline.
TEST(ReadFirstFrameBounded, CovFramerErrorPropagates) {
    constexpr std::size_t kMaxBytes = 4096;
    constexpr auto kDeadline = std::chrono::seconds{5};

    std::string const junk =
        "NOT-A-FIX-FRAME\x01"
        "more-junk\x01";
    ASSERT_LT(junk.size(), kMaxBytes) << "must stay under budget so the framer arm is what fires";

    Script s;
    s.inbound_bytes = to_bytes(junk);
    s.read_latency = std::chrono::milliseconds{1};

    asio::io_context ioc;
    // Frozen (#377): never advanced, so the deadline cannot fire and cannot
    // race this cell's mechanism. See the timebase note at the top of file.
    fixpp::core::mock_clock clock{{}, {}, ioc.get_executor()};
    mock_transport mt{ioc.get_executor(), std::move(s)};
    std::vector<std::byte> buf;

    auto fut = asio::co_spawn(ioc, read_first_frame_bounded(mt, buf, clock, kDeadline, kMaxBytes),
                              asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, fut, "ReadFirstFrameBounded::CovFramerErrorPropagates")) {
        return;
    }
    expected_t<std::size_t> const result = fut.get();

    ASSERT_FALSE(result.has_value())
        << "coverage cell: a non-FIX payload must surface the framer's error, got "
        << describe(result);
    EXPECT_EQ(result.error(), error::wire_framing_resync)
        << "coverage cell: expected the framer's own error to PROPAGATE VERBATIM "
           "(read_first_frame_bounded.hpp's feed-error arm), got "
        << describe(result);
    EXPECT_LT(buf.size(), kMaxBytes)
        << "coverage cell: the budget arm must NOT be what fired — buf stayed well under "
        << kMaxBytes << ", so this is the framer arm.";
}

// ── COVERAGE CELL — read-arm error propagation (Article IX §1) ───────────────
// Gate B (PR #239) B3: `:135`'s `co_return std::unexpected(read_r.error())` is
// covered by T2a but T2a's assertion (is_cancellation_attributable) is a
// two-element SET, not the named postcondition. Contract
// `contracts/read_first_frame_bounded.md:96-99` states transport-originated
// read errors are "Propagated verbatim ... No mapping changes" — that must be
// pinned with an EXACT value, on an error T2a's set does not admit.
//
// Construction: an empty Script (no inbound_bytes, no inbound_chunks) makes
// every async_read_some hit the mock's exhaustion path immediately
// (mock_transport.hpp:260-261, read_cursor_ >= inbound_bytes.size() == 0 ==>
// transport_read_eof) with no latency, so the deadline arm (500ms) cannot
// win the join.
TEST(ReadFirstFrameBounded, CovReadErrorPropagates) {
    constexpr std::size_t kMaxBytes = 4096;
    constexpr auto kDeadline = std::chrono::milliseconds{500};

    Script s;  // inbound_bytes left empty — immediate transport_read_eof.

    asio::io_context ioc;
    // Frozen (#377): never advanced, so the deadline cannot fire and cannot
    // race this cell's mechanism. See the timebase note at the top of file.
    fixpp::core::mock_clock clock{{}, {}, ioc.get_executor()};
    mock_transport mt{ioc.get_executor(), std::move(s)};
    std::vector<std::byte> buf;

    auto fut = asio::co_spawn(ioc, read_first_frame_bounded(mt, buf, clock, kDeadline, kMaxBytes),
                              asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, fut, "ReadFirstFrameBounded::CovReadErrorPropagates")) {
        return;
    }
    expected_t<std::size_t> const result = fut.get();

    ASSERT_FALSE(result.has_value())
        << "coverage cell: an exhausted transport must surface a read error, got "
        << describe(result);
    EXPECT_EQ(result.error(), error::transport_read_eof)
        << "coverage cell: expected the read arm's error to PROPAGATE VERBATIM "
           "(read_first_frame_bounded.hpp:135, contracts/read_first_frame_bounded.md:96-99), got "
        << describe(result);
    EXPECT_NE(result.error(), error::transport_read_cancelled)
        << "coverage cell: :135 mapping every read error to a cancellation-attributable "
           "value would leave T2a green while breaking verbatim propagation — this cell "
           "must fail if that mapping is reintroduced.";
}
