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
// mutant of the delivered design (`expires_after` moved into the loop), not
// `main`. See each cell's own comment for its RED basis.

#include "session/read_first_frame_bounded.hpp"

#include <gtest/gtest.h>

#include <asio/awaitable.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <fixpp/core/error.hpp>
#include <fixpp/transport/test/mock_transport.hpp>
#include <future>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

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
    mock_transport mt{ioc.get_executor(), std::move(s)};
    std::vector<std::byte> buf;

    auto fut = asio::co_spawn(
        ioc, read_first_frame_bounded(mt, buf, std::chrono::milliseconds{1000}, kMaxBytes),
        asio::use_future);
    ioc.run();
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
    mock_transport mt{ioc.get_executor(), std::move(s)};
    std::vector<std::byte> buf;

    auto fut = asio::co_spawn(
        ioc, read_first_frame_bounded(mt, buf, std::chrono::milliseconds{1000}, kMaxBytes),
        asio::use_future);
    ioc.run();
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
    mock_transport mt{ioc.get_executor(), std::move(s)};
    std::vector<std::byte> buf;

    auto fut = asio::co_spawn(
        ioc, read_first_frame_bounded(mt, buf, std::chrono::milliseconds{1000}, kMaxBytes),
        asio::use_future);
    ioc.run();
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
TEST(ReadFirstFrameBounded, B5) {
    constexpr std::size_t kMaxBytes = 4096;

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
    mock_transport mt{ioc.get_executor(), std::move(s)};
    std::vector<std::byte> buf;

    auto fut = asio::co_spawn(
        ioc, read_first_frame_bounded(mt, buf, std::chrono::milliseconds{50}, kMaxBytes),
        asio::use_future);
    ioc.run();
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
    constexpr auto kDeadline = std::chrono::milliseconds{10};

    std::vector<std::byte> const frame = make_logon_of_length(kLogonLen);
    ASSERT_EQ(frame.size(), kLogonLen);

    Script s;
    s.inbound_bytes = frame;
    s.read_latency = std::chrono::milliseconds{1};

    asio::io_context ioc;
    mock_transport mt{ioc.get_executor(), std::move(s)};
    std::vector<std::byte> buf;

    std::optional<expected_t<std::size_t>> result;
    asio::co_spawn(ioc, read_first_frame_bounded(mt, buf, kDeadline, kMaxBytes),
                   [&result](std::exception_ptr ep, expected_t<std::size_t> r) {
                       EXPECT_FALSE(ep) << "T1: the spawned coroutine threw.";
                       result = std::move(r);
                   });

    // Step 1: run the spawn to its first real suspension. co_spawn's initial
    // resume is posted, not inline, so this poll() call executes the
    // coroutine synchronously through timer.expires_after(10ms) and the
    // callback-form timer.async_wait(...) registration (neither suspends —
    // async_wait with a callback starts the wait without co_await'ing it)
    // until it reaches the genuine suspension inside async_read_some: the
    // mock's own 1ms read_latency co_await. Both timers are now armed;
    // nothing is expired yet, so poll() returns with work outstanding.
    ioc.poll();

    // Step 2: elapse BOTH absolute deadlines with no handler running at all
    // (the context is not being driven). This is a one-sided, 5x-slack
    // margin on elapsing two wall-clock expiries, not a race.
    std::this_thread::sleep_for(std::chrono::milliseconds{50});

    // Step 3: both timers are now expired. asio's timer queue is a heap
    // ordered by expiry, so the ready queue is drained in expiry order: the
    // 1ms read completion first (resumes the coroutine -> feeds -> finds the
    // frame -> timer.cancel() [cannot un-queue the already-ready deadline
    // handler] -> co_return, freeing the frame), then the 10ms deadline
    // handler (writes into the freed frame, then transport.cancel()).
    ioc.poll();

    // Drain to completion (D-6.2: "after the context is drained to
    // completion") in case anything remains posted (e.g. the completion
    // handler's own dispatch).
    ioc.run();

    ASSERT_TRUE(result.has_value())
        << "T1: the spawned coroutine never completed — the drain above is insufficient, not the "
        << "elapse-then-poll construction. Without a completed result, cancels_observed()==0 would "
        << "be a vacuous pass.";
    EXPECT_TRUE(result->has_value())
        << "T1 (SC-005/SC-006): expected the first frame's length (" << kLogonLen << "), got "
        << describe(*result) << " — the read that raced the deadline must still win the frame.";
    if (result->has_value()) {
        EXPECT_EQ(**result, kLogonLen) << "T1 (SC-005/SC-006): the admitted frame's exact length.";
    }
    EXPECT_EQ(mt.async_reads_observed(), 1u)
        << "T1: expected exactly one read (the whole frame arrives in it) — a second read would "
        << "mean framer.feed found nothing on the first and this cell is not exercising the "
        << "frame-found race D-6.2 describes.";

    EXPECT_EQ(mt.cancels_observed(), 0u)
        << "T1 (SC-005/SC-006) [S5 proxy — research.md D-6.2/N3, not the full postcondition: "
        << "this observes that no cancel() RAN, which is narrower than 'no handler armed by this "
        << "call is outstanding on return']: expected zero cancel() calls after the context "
        << "drains. A nonzero count means the stranded deadline handler survived the coroutine's "
        << "return and called transport.cancel() through its dangling reference — the pre-fix "
        << "defect this cell targets. Under linux-clang-asan this must instead manifest as a "
        << "heap-use-after-free abort (the write to `timed_out` lands first); a clean ASan run "
        << "here means the proof did not fire, not that there is no defect (D-6.3).";
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
TEST(ReadFirstFrameBounded, B4) {
    constexpr std::size_t kMaxBytes = 4096;
    constexpr auto kDeadline = std::chrono::milliseconds{50};

    std::string never_completes = std::string("8=FIX.4.2\x01") + "9=200000\x01";
    ASSERT_LT(never_completes.size(), kMaxBytes + 1);
    never_completes.append(kMaxBytes + 1 - never_completes.size(), 'X');
    ASSERT_EQ(never_completes.size(), kMaxBytes + 1);

    Script s;
    s.inbound_bytes = to_bytes(never_completes);
    s.read_latency = std::chrono::milliseconds{3};

    asio::io_context ioc;
    mock_transport mt{ioc.get_executor(), std::move(s)};
    std::vector<std::byte> buf;

    auto fut = asio::co_spawn(ioc, read_first_frame_bounded(mt, buf, kDeadline, kMaxBytes),
                              asio::use_future);
    ioc.run();
    expected_t<std::size_t> const result = fut.get();

    EXPECT_FALSE(result.has_value())
        << "B4 (SC-003): expected the over-budget, never-completing payload to be rejected, got "
        << describe(result);
    if (!result.has_value()) {
        EXPECT_EQ(result.error(), error::wire_frame_too_large)
            << "B4 (SC-003): expected wire_frame_too_large (FR-003/FR-014's protective intent), "
               "got "
            << describe(result);
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
// = 7 ms`, `inbound_chunks` = 201 chunks of 1 byte (mechanism 5). Reads
// complete at 7, 14, 21, 28, 35, 42, 49 ms; the 8th read's latency wait is
// still in flight when the deadline expires at 50 ms, with `buf.size() == 7`
// — far below the 200-byte budget — so the deadline arm must win.
//
// `7 ms` is load-bearing (research.md D-6.11): the two timer series MUST NOT
// share a common multiple inside the deadline window, or the cell depends on
// an ordering §D-6.4/SC-014 explicitly refuse to depend on (5 ms would
// co-expire the 10th read with the 50 ms deadline in the same drain).
//
// `buf.size()` is asserted as a BAND (`[1, kMaxBytes)`), not the exact
// derived value 7: pinning the wall-clock-derived exact count is a <2%
// margin against real timer/scheduler slop (49ms vs a 50ms deadline) and
// would false-RED under ASan/TSan or a loaded CI lane
// ([[feedback_timing_band_witness_range_admits_the_mutant_it_claims_to_kill]]).
// The band is tight on the side that matters: `< kMaxBytes` still excludes
// the mutant's terminal value of 201 (all chunks drained), and `>= 1` keeps
// the cell non-vacuous (the loop genuinely ran at least once). The 7-ms/
// 14-ms.../49-ms derivation is recorded here and in the failure message, not
// pinned as an assertion.
//
// Mutant killed: `expires_after` moved into the loop (or into
// `await_deadline`) — a per-iteration re-arm. Under it the deadline is reset
// every 7 ms and never fires; the loop drains all 201 chunks and reaches
// `201 > 200` at the foot ⇒ `wire_frame_too_large`, with `buf.size() == 201`.
// `await_deadline` is documented (read_first_frame_bounded.hpp) as forbidden
// from calling `expires_after` for exactly this reason.
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
    mock_transport mt{ioc.get_executor(), std::move(s)};
    std::vector<std::byte> buf;

    auto fut = asio::co_spawn(ioc, read_first_frame_bounded(mt, buf, kDeadline, kMaxBytes),
                              asio::use_future);
    ioc.run();
    expected_t<std::size_t> const result = fut.get();

    EXPECT_FALSE(result.has_value())
        << "B6 (SC-004/D-1b): expected the deadline to win before the budget could be reached, got "
        << describe(result);
    if (!result.has_value()) {
        EXPECT_EQ(result.error(), error::transport_handshake_timeout)
            << "B6 (SC-004/D-1b): expected transport_handshake_timeout (the arm-once deadline "
               "firing "
            << "at 50ms, derived buf.size() == 7 from reads completing at 7/14/.../49ms), got "
            << describe(result)
            << " — a per-iteration re-arm would let the deadline keep being pushed "
            << "forward and the loop would drain all 201 chunks instead.";
    }
    EXPECT_GE(buf.size(), 1u)
        << "B6 (SC-004/D-1b) [non-vacuity]: expected at least one completed read before the "
           "deadline "
        << "fired (derived: 7, from reads at 7/14/.../49ms) — buf.size() == 0 would mean the loop "
        << "never genuinely ran.";
    EXPECT_LT(buf.size(), kMaxBytes)
        << "B6 (SC-004/D-1b): expected buf.size() far below the " << kMaxBytes << "-byte budget "
        << "(derived: 7) — the re-arm mutant drains all 201 chunks, reaching buf.size() == 201.";
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
// Deterministic construction (T045/SC-016 — NOT a wall-clock threshold): an
// intermediate timer the TEST owns is armed at `kIntermediate` (100ms)
// against a helper `kDeadline` of 500ms — D-6.12's own figures, a one-sided
// 5x margin (the delivered code retires in microseconds; the mutant cannot
// retire before 500ms). `timer_fired_before_result` is set inside the
// timer's own handler ONLY if `result` is still unset at that instant, so
// the assertion is an ORDERING between two test-controlled events (did the
// timer's handler run before the join retired?), not a comparison of
// elapsed wall-clock against a constant. The context is always drained to
// `result.has_value()` regardless of which fires first, matching D-6.2's
// "drain to completion" discipline (T1, above) rather than aborting mid-flight.
TEST(ReadFirstFrameBounded, T2a) {
    constexpr std::size_t kMaxBytes = 4096;
    constexpr auto kDeadline = std::chrono::milliseconds{500};
    constexpr auto kIntermediate = std::chrono::milliseconds{100};

    Script s;
    // 10s >> kDeadline (500ms): the read must be resolved ONLY by
    // cancellation, never by the mock's own latency timer racing it.
    s.read_latency = std::chrono::seconds{10};

    asio::io_context ioc;
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
            result = co_await read_first_frame_bounded(mt, buf, kDeadline, kMaxBytes);
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

    bool timer_fired_before_result = false;
    asio::steady_timer intermediate{ioc.get_executor()};
    intermediate.expires_after(kIntermediate);
    intermediate.async_wait([&](std::error_code ec) {
        if (!ec && !result.has_value()) timer_fired_before_result = true;
    });

    signal.emit(asio::cancellation_type::total);

    while (!result.has_value()) {
        ASSERT_GT(ioc.run_one(), 0u)
            << "T2a: io_context ran out of work before the join completed — a broken "
            << "cell (mis-wired timers), not a RED proof.";
    }
    intermediate.cancel();
    ioc.poll();

    ASSERT_FALSE(thrown) << "T2a: the wrapper coroutine threw.";

    // THE promptness assertion — kills the bare-deadline-arm mutant (D-6.12b).
    EXPECT_FALSE(timer_fired_before_result)
        << "T2a (SC-015/FR-015): the intermediate " << kIntermediate.count() << "ms timer "
        << "fired BEFORE the join completed, against a " << kDeadline.count() << "ms helper "
        << "deadline. Under the bare-deadline-arm mutant the deadline arm's own cancel is "
        << "silently dropped, so the join cannot retire before the FULL deadline elapses "
        << "(D-6.12b) — cancellation must be PROMPT, not merely eventual.";

    EXPECT_TRUE(is_cancellation_attributable(*result))
        << "T2a (SC-015): expected a cancellation-attributable outcome (transport_read_"
        << "cancelled or transport_handshake_timeout), got " << describe(*result);
}
