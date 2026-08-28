// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_test_request_id_cross_session_race.cpp
//
// 009-session-fsm-finalize T019 [US4] — Per-session TestReqID counter race-freedom.
//
// Scenarios (spec.md FR-010 / SC-003 / [const §XI.4]):
//
//   1. CrossSessionDisjoint — each of two concurrent sessions' TestReqID
//      sequences is contiguous from TR1 (FR-010: per-session counter, not
//      process-global static); duplicate values ACROSS sessions are expected.
//
//   2. WithinSessionMonotone — within each session, TestReqIDs increment
//      monotonically from TR1 upward (no gaps, no reset).
//
// Test structure:
//   - asio::thread_pool(4) with TWO Sessions, each on its own strand.
//   - Both sessions use mock_clock + HeartBtInt=1s so the liveness loop fires
//     often. We advance the mock clock rapidly so each session emits many
//     TestRequest frames.
//   - Each session's transport_send captures outbound frames into a
//     mutex-protected vector.
//   - After enough clock ticks, close both sessions and analyse captures.
//
// SC-003 quantifies 10^4 TestRequests per session. `ConcurrentSessionsTSanStress`
// now pins exactly that (#317); `CrossSessionDisjoint` deliberately keeps a far
// smaller corpus, on the basis that ITS stress is the concurrency, not the count.
// Each pins its own corpus size with an equality at its own `kIterations` — read
// that, not this header: a figure repeated here is a second thing to keep true,
// and the one that used to sit in this paragraph described neither test (#309).
//
// RED phase (before T020): the existing `static tr_counter` in
// run_liveness_loop is shared across all sessions → assertion (a) will fail
// (sequences interleave) AND TSan will fire a data race on the static.
//
// GREEN phase (after T020): ++next_test_request_id_ is per-session on the
// session strand → per-session contiguous, monotone, race-free.
//
// Anchors:
//   spec.md FR-010, SC-003
//   [const §XI.4] per-session strand isolation
//   research.md D-3 (wrap-around at UINT32_MAX acceptable)

#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <asio/co_spawn.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/fix_time.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/extract_tag.hpp"
#include "support/pump_until_ready.hpp"

// (#303) The teardown guard below deliberately LEAKS the SessionFixtures on the
// residual path (see `quiesce_or_release_on_exit`). Tell LeakSanitizer, or the
// intentional leak reddens the binary and buries the named ADD_FAILURE that path
// exists to produce.
//
// MEASURED, in THIS binary, before writing this block: a deliberate unreachable
// allocation produced `LeakSanitizer: detected memory leaks` **after** its test
// had already printed `[ OK ]`. So `detect_leaks` is on here and an un-ignored
// leak from a PASSING test fails the run. That is why this is mandatory rather
// than tidy.
//
// COST, stated rather than understated. `__lsan_ignore_object` makes the ignored
// chunk a ROOT, so the whole graph reachable from each released SessionFixture
// also drops out of leak reports: its EngineConfig (clock, executor), its
// SessionConfig (dictionary, security profile), its CaptureTransport frame
// vectors, and the entire Session (buffers, arena, validator, strand). Those
// resources are retained until process exit.
//
// What bounds the exposure is WHICH runs can reach it:
//   - a real `CrossSessionDisjoint` residual, where the guard's ADD_FAILURE has
//     already made the binary RED, so nothing green is hiding anything;
//   - every `CrossSessionTeardown` witness that drives the residual path through
//     `EXPECT_NONFATAL_FAILURE` and therefore leaves the binary GREEN. Those ARE a
//     real hole: an unrelated leak reachable through such a witness's own fixtures
//     would be suppressed.
//
// ⚠️ THE NUMBERS BELOW WENT STALE THREE ROUNDS RUNNING, EACH TIME BECAUSE A LATER
// COMMIT IN THE SAME PR ADDED A WITNESS THAT REACHES THE RELEASE PATH. Read the
// rule before the figures, because the rule is the durable part:
//
//     ADDING, REMOVING, OR RETARGETING ANY TEST THAT REACHES THE FIXTURE-RELEASE
//     PATH INVALIDATES THIS BLOCK. Re-measure and rewrite it in the SAME commit.
//
// Calling a figure "structural" does not make it survive; only re-deriving it does.
// Two prior versions of this paragraph said they were stated structurally to
// survive the next witness, and both were falsified by the next witness.
//
// Re-derive: edit the `#define FIXPP_XSESSION_LSAN_IGNORE` below so it expands to
// `((void)(p))` instead of the sanitizer call, then rebuild and run:
//   cmake --build build/linux-clang-asan --target session_test_request_id_cross_session_race -j2
//   ./build/linux-clang-asan/bin/session_test_request_id_cross_session_race
// then RESTORE the macro. Leak detection must be ON (no ASAN_OPTIONS).
//
// ⚠️ EDIT THAT LINE BY HAND — do NOT script it by matching the sanitizer call's
// text. An earlier revision of this block spelled the re-derivation as a `sed`
// over that literal, which made the string appear TWICE in this file (here and at
// the #define). The command would then have rewritten its own instructions, and an
// anchor-count assertion of 1 -- the discipline this repo uses to prove a mutation
// applied -- would read 2 and refuse. A comment that quotes the token it tells you
// to replace is its own second occurrence. That token in fact appears a THIRD time
// (in the COST paragraph above and in the PRIOR ART paragraph below), so an
// anchor-count assertion built against the sanitizer-call TEXT is never safe here.
// Locate the `#define FIXPP_XSESSION_LSAN_IGNORE` line itself and mutate it BY
// ADDRESS instead — that anchor is unique regardless of how many times the
// sanitizer-call text appears elsewhere in the file.
//
// The one genuinely stable invariant is the RELEASED ROOT: every leaked byte is
// REACHABLE FROM something the guard released -- a `SessionFixture`, or the
// `io_context` itself. Reachable-from, not owned-by, and the distinction is the
// reason the suppression works at all — a sizeable minority of the blocks are asio
// coroutine frames (`Session::run_liveness_loop`, allocated by
// `asio::detail::thread_info_base`), which no fixture owns but every one keeps
// alive. `__lsan_ignore_object` suppresses by REACHABILITY, so those are covered;
// the leak-clean baseline with the suppression intact is the proof.
//
// The io_context became a released root when the guard started releasing it (see
// the guard's `ioc` member: releasing the fixtures alone strands the context's
// outstanding-work count, which POSIX ignores and Windows spins on forever). That
// is the fourth time a change to which objects the release path covers invalidated
// the figures below, which is why the rule above is stated before them.
//
// Deliberately no per-component byte split here. An earlier revision carried one
// and got the arithmetic wrong — it omitted a 40-byte `_Sp_counted_deleter` control
// block allocated inside `make_minimal_dictionary()`, so its dictionary subtotal
// was short by 40 B per fixture. A split is a second set of figures to keep true,
// on the same moving target, for no decision anyone makes from this block. The
// per-witness table below is the measurement; this paragraph is the invariant.
//
// Measured on `linux-clang-asan` (clang 22, libstdc++, `-fsanitize=address`). Not
// pinned to a specific commit as "the last code-affecting one" -- an earlier
// version of this line named one and was wrong: the named commit's own diff
// touched only comments, in a file this binary does not even link. Trust the
// re-measurement RULE above (not a commit citation) to decide whether this table
// is still current:
//
//   witness                                  fixtures released   leaked
//   ResidualPathReleasesTheFixtures                  2           674846 B / 68
//   ThrowingPumpStillReleasesTheFixtures             1           334053 B / 17
//   OuterCatchSwallowsAThrowingAddFailure            2           674846 B / 68
//   QuiescedPathDestroysTheFixtures                  0           none
//   ZeroBudgetOnAnEmptyContextIsNotResidual          0           none
//   PositiveBudgetWithNoFixturesIsStillResidual      0           485 B / 5
//   ------------------------------------------------------------------------
//   total (DERIVED, not the claim)                   5          1684230 B / 158
//
// The three fixture-releasing rows each also release one io_context, which is
// why every one of them grew against the previous measurement while the fixture
// column did not. The new row (gate-b/r2 C1) releases no fixture at all -- it
// exists specifically to reach the release with an empty `fixtures` vector.
//
// Three cross-checks that make the table self-auditing, and that a future reader
// should re-run rather than trust: the per-witness bytes sum EXACTLY to the total
// (674846 + 334053 + 674846 + 485 = 1684230); so do the allocation counts
// (68 + 17 + 68 + 5 = 158) -- together these are what license "nothing else in
// the binary leaks"; and `grep -c 'leak of 266272 byte'` on the run reports 5,
// unchanged, matching the fixture column (the new row releases no fixture, so it
// contributes no 266272-byte entry). If any identity breaks, the table is wrong,
// not the allocator.
//
// Also measured, and NOT derivable from the table: the two real tests
// (`--gtest_filter=CrossSessionTestReqID.*`) leak NOTHING with the suppression
// removed. The whole leak is the witnesses'.
//
// The per-fixture byte count is allocator- and stdlib-dependent, so a different
// NUMBER in the last column is a re-measurement, not a regression. That licence
// covers the bytes ONLY — never the witness list or the fixture column, which are
// facts about which tests reach the residual path.
//
// So the suppressed set in a green run is bounded to fixtures that open no store
// and register no listener, under the named witnesses above — a real cost, bounded
// and named, on the same footing as tests/interop/support/interop_fixture.cpp's
// larger version of it.
//
// That same experiment is what proves the suppression is load-bearing rather than
// decorative, and that the release() on the residual path actually executes: with
// the ignore removed the leak APPEARS, from tests that still report [ OK ].
//
// Shape follows the repo's established sanitizer-detection idiom (two separate
// #if blocks, not an #elif chain) — see tests/interop/support/interop_fixture.cpp:49-62.
// An #elif chain would skip the __SANITIZE_ADDRESS__ arm on any compiler that
// defines __has_feature without reporting address_sanitizer through it.
//
// MSVC is excluded deliberately: windows-msvc-asan is a real tier2 lane whose
// profile sets /fsanitize=address, so __SANITIZE_ADDRESS__ IS defined there, but
// MSVC's ASan ships no LeakSanitizer and no <sanitizer/lsan_interface.h> — the
// include would be a hard compile error, and there is no leak detector to appease.
#if !defined(_MSC_VER)
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define FIXPP_XSESSION_HAVE_LSAN 1
#endif
#endif
#if !defined(FIXPP_XSESSION_HAVE_LSAN) && defined(__SANITIZE_ADDRESS__)
#define FIXPP_XSESSION_HAVE_LSAN 1
#endif
#endif

#if defined(FIXPP_XSESSION_HAVE_LSAN)
#include <sanitizer/lsan_interface.h>
#define FIXPP_XSESSION_LSAN_IGNORE(p) __lsan_ignore_object(p)
#else
#define FIXPP_XSESSION_LSAN_IGNORE(p) ((void)(p))
#endif

using namespace std::chrono_literals;

namespace fixpp::session::test {

namespace {

using fixpp::test_support::extract_tag;

// ── Build a minimal Logon frame for feeding into a session ────────────────────
static std::vector<std::byte> make_logon_frame(std::string_view begin_string, std::uint32_t seq,
                                               std::string_view sender, std::string_view target,
                                               int heartbt = 1) {
    std::string body;
    body += "35=A\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target) + "\x01";
    body += "98=0\x01";
    body += "108=" + std::to_string(heartbt) + "\x01";

    std::string hdr;
    hdr += "8=" + std::string(begin_string) + "\x01";
    hdr += "9=" + std::to_string(body.size()) + "\x01";
    std::string full = hdr + body;
    unsigned int cs = 0;
    for (unsigned char c : full) {
        cs += c;
    }
    cs &= 0xFFu;
    char csbuf[8];
    std::snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> result;
    result.reserve(full.size());
    for (char c : full) {
        result.push_back(static_cast<std::byte>(c));
    }
    return result;
}

// ── Build a Heartbeat (35=0) carrying a TestReqID (112) ───────────────────────
//
// Echoing the session's own TestReqID back clears the pending TestRequest, so
// the grace window does not expire and the session stays Active. WITHOUT this
// reply a session emits exactly ONE TestRequest and then disconnects — which is
// what capped `ConcurrentSessionsTSanStress`'s corpus at one (#309).
//
// File-local rather than a lambda inside `CrossSessionDisjoint`, because both
// emission-driving tests now need it and a second copy is a second thing to keep
// in step with the seqnum bookkeeping its callers do.
// ── SendingTime(52) for a peer-authored frame ─────────────────────────────────
//
// #317: this used to be the literal "20240101-00:00:00.000" in both builders,
// which is the seed instant of the tests' mock_clock (1704067200 = 2024-01-01T00:00Z).
// That is correct only while the clock has not moved. `Session` checks inbound
// SendingTime against a threshold that defaults to 120 s
// (`cfg_.sending_time_threshold`), so a test that
// advances its own clock past that and keeps stamping the seed instant has its
// frames REJECTED as stale — correctly, by a guard doing its job.
//
// A real peer stamps the time it sent at. Stamping from the same clock the session
// reads keeps the guard ARMED (the threshold is left at its default) while making
// the peer behave like a peer. It is what lifts the ~80-iteration ceiling measured
// on `ConcurrentSessionsTSanStress`; see that test's header for the numbers.
//
// WHICH guard, established by ISOLATION rather than by reading. There are three
// `check_sending_time` call sites and ALL THREE carry the same 120 s default, so
// picking one by inspection is exactly how a citation lands on a plausible twin.
// Raising ONLY the acceptor-Logon site's default to 24 h left the ceiling exactly
// where it was (still iteration 81); raising ONLY the ESTABLISHED-SESSION site's
// let 100 iterations through. So it is the established-session guard — "Guard (3):
// SendingTime MaxLatency", the Reject(10)/Logout/Disconnect path — that these
// Heartbeats meet, because both sessions are Active before any Heartbeat is fed.
// Named by its guard label rather than by line number, because line numbers move.
// (Gate: Codex review of this branch, P3. The first version of this comment cited
// the acceptor-Logon site; the isolation above falsifies that.)
// ⚠️ FIFTH copy of this format-a-SendingTime shape in tests/: the same body is
// hand-rolled at engine_acceptor_test.cpp:75, engine_acceptor_failclosed_test.cpp:77,
// engine_connect_test.cpp:89 and engine_readpump_test.cpp:89. Those four format
// `system_clock::now()`; this one takes the time point, because a mock-clock test
// must stamp the clock the SESSION reads, not the wall clock. Not hoisted here —
// that is #315's class of work and would inflate this review target — but recorded
// so the census does not have to be rediscovered.
static std::string fix_sending_time(fixpp::core::utc_time_point tp) {
    char buf[32];
    auto r = fixpp::core::utc_time_to_fix_string(tp, fixpp::core::fix_time_precision::millis, buf);
    // A formatting failure here would silently produce an empty 52= and surface as
    // an unrelated rejection several layers away, so it is reported at its source.
    if (!r) {
        ADD_FAILURE() << "#317: utc_time_to_fix_string failed while stamping SendingTime";
        return {};
    }
    return std::string(r->data(), r->size());
}

static std::vector<std::byte> make_heartbeat(std::string_view bs, std::uint32_t seq,
                                             std::string_view sender, std::string_view target,
                                             std::string_view tr_id,
                                             std::string_view sending_time) {
    std::string body;
    body += "35=0\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=" + std::string(sending_time) + "\x01";
    body += "56=" + std::string(target) + "\x01";
    if (!tr_id.empty()) {
        body += "112=" + std::string(tr_id) + "\x01";
    }
    std::string hdr;
    hdr += "8=" + std::string(bs) + "\x01";
    hdr += "9=" + std::to_string(body.size()) + "\x01";
    std::string full = hdr + body;
    unsigned int cs = 0;
    for (unsigned char c : full) {
        cs += c;
    }
    cs &= 0xFFu;
    char csbuf[8];
    std::snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";
    std::vector<std::byte> result;
    result.reserve(full.size());
    for (char c : full) {
        result.push_back(static_cast<std::byte>(c));
    }
    return result;
}

// ── CaptureTransport: thread-safe outbound frame capture ────────────────────────
//
// The session's transport_send is called from the session strand; we collect all
// outbound frames into a vector under a std::mutex.  The mutex is ONLY for the
// test's collector (not on the session-internal path), so it doesn't affect TSan
// annotations on the session's own counters.
struct CaptureTransport {
    std::mutex mtx;
    std::condition_variable cv;
    std::vector<std::vector<std::byte>> frames;
    // #317: the TestReqID corpus, classified ONCE on the writer side. Was rebuilt
    // from `frames` on every read; see `await_test_req_ids` for why that mattered.
    std::vector<std::string> test_req_ids;

    void capture(std::span<const std::byte> frame) {
        std::lock_guard<std::mutex> lock{mtx};
        frames.emplace_back(frame.begin(), frame.end());
        // Classify here, under the lock this function already takes, rather than in
        // every reader. Same predicate as the old `collect_test_req_ids` body, so
        // the corpus this test asserts on is byte-for-byte what it was.
        auto sp = std::span<const std::byte>{frames.back().data(), frames.back().size()};
        if (extract_tag(sp, 35) == "1") {
            auto id = extract_tag(sp, 112);
            if (!id.empty()) {
                test_req_ids.push_back(std::move(id));
                cv.notify_all();
            }
        }
    }

    // Snapshot of the 112= (TestReqID) values from frames where 35=1 (TestRequest).
    // O(corpus) COPY, no rescan. Kept as the analysis surface; the hot path is
    // `await_test_req_ids` / `test_req_id_count` below.
    std::vector<std::string> collect_test_req_ids() {
        std::lock_guard<std::mutex> lock{mtx};
        return test_req_ids;
    }

    std::size_t test_req_id_count() {
        std::lock_guard<std::mutex> lock{mtx};
        return test_req_ids.size();
    }

    // Newest TestReqID. Copies ONE string; `collect_test_req_ids().back()` copied
    // the whole corpus to reach it, which is a second O(iterations^2) term and was
    // measurable on its own: with only the rescan fixed, 10^4 iterations still took
    // 40.7 s and the curve was still superlinear (2x N -> 3.3x time). Replacing
    // these two loop-body calls took it to 11.5 s and 2x N -> ~2.1x time.
    std::string latest_test_req_id() {
        std::lock_guard<std::mutex> lock{mtx};
        // The callers reach here only after an `await_test_req_ids` returned true,
        // so the corpus is non-empty by construction — the same property the
        // `collect_test_req_ids().back()` this replaces relied on. Reported rather
        // than returned silently: an empty string here becomes a Heartbeat with no
        // 112 field, which the session accepts, so the miss would surface several
        // iterations later as an unrelated wait-budget failure naming the wrong
        // thing. A bare `return {}` would be that silent path.
        if (test_req_ids.empty()) {
            ADD_FAILURE() << "#317: latest_test_req_id() on an empty corpus — a wait "
                             "was skipped or returned false unchecked";
            return {};
        }
        return test_req_ids.back();
    }

    // Block until at least `want` TestReqIDs have been emitted, or `budget`
    // elapses. Returns false on budget exhaustion.
    //
    // #317: this replaces a 1 ms sleep-poll whose predicate rebuilt the whole
    // corpus — `extract_tag` copies each frame into a std::string, so the wait cost
    // was O(corpus) per tick and the test's total work was O(iterations^2).
    // Measured on the unfixed code with the #317 SendingTime ceiling already
    // lifted: doubling `kIterations` cost ~3.9x wall-clock (300 -> 3.0 s,
    // 600 -> 11.0 s, 1200 -> 41.9 s, 2400 -> 163.5 s).
    //
    // Waiting on the writer's notification rather than polling removes BOTH terms:
    // the rescan AND the 1 ms slice that put a floor of `kIterations` milliseconds
    // under the test no matter how fast the pool was.
    //
    // Takes an ABSOLUTE deadline, not a relative budget, and that is load-bearing
    // rather than stylistic.
    //
    // The count only ever grows, so waiting for A and then for B reaches the same
    // state as one predicate over both. But `cv.wait_for(lock, budget, pred)`
    // computes a FRESH deadline per call, so two sequential relative waits admit a
    // schedule the single predicate they replace would have failed: A ready at
    // 9.5 s, B at 19 s, both waits return, ~19 s against a 10 s contract. Passing
    // one deadline computed before the first wait restores the bound.
    //
    // (Gate: Codex review of this branch, P2. The earlier form claimed the two
    // shapes were equivalent; they are equivalent in READINESS and not in the
    // BOUNDED-WAIT contract, which is the half this test relies on.)
    //
    // ⚠️ AND THE EVIDENCE FOR THIS IS CONSTRUCTION, NOT MEASUREMENT. The commit that
    // made this change cited the zero-advance and reply-never-sent mutants still
    // failing at 10010 ms. That is CONSISTENT with a shared deadline but does not
    // discriminate: in both mutants session A never emits, so the FIRST wait spends
    // the whole deadline and the second never runs — the old per-call budget would
    // have produced the same 10010 ms. The schedule that would tell them apart (A
    // ready at 9.5 s, B at 19 s) is not one those mutants produce, and no mutant
    // here produces it. The bound holds because one `time_point` is computed before
    // the first wait and both `wait_until` calls take it, which is checkable by
    // reading — not because it was seen to fail at 10 s rather than 20 s.
    [[nodiscard]] bool await_test_req_ids(std::size_t want,
                                          std::chrono::steady_clock::time_point deadline) {
        std::unique_lock<std::mutex> lock{mtx};
        return cv.wait_until(lock, deadline, [&] { return test_req_ids.size() >= want; });
    }
};

// ── TestReqID numeric extractor: "TR<N>" → N ──────────────────────────────────
// Returns 0 if parsing fails. 0 is usable as the failure value because the
// counter is pre-incremented, so a real TestReqID is never TR0.
//
// #309: this used to accumulate into a `std::uint32_t` with no overflow check and
// to accept leading zeros, which made two DEGENERATE CORPORA parse as a clean
// 1..N and satisfy every downstream check:
//   - `TR4294967297 .. TR4294967306` wraps modulo 2^32 to 1..10;
//   - `TR0001 .. TR0010` is 1..10 in a spelling the session never emits.
// Both are rejected now. The emitter builds the id from an integer, so canonical
// spelling is a property of the SUT this parser is entitled to require — and
// requiring it is what stops the parser from laundering a corpus into validity.
static std::uint32_t parse_tr_id(std::string_view s) {
    if (s.size() < 3) {
        return 0;
    }
    if (s[0] != 'T' || s[1] != 'R') {
        return 0;
    }
    if (s[2] == '0' && s.size() > 3) {
        return 0;  // non-canonical leading zero
    }
    std::uint32_t v = 0;
    for (std::size_t i = 2; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') {
            return 0;
        }
        const auto d = static_cast<std::uint32_t>(s[i] - '0');
        if (v > (UINT32_MAX - d) / 10u) {
            return 0;  // would wrap; a wrapped value is indistinguishable from a small one
        }
        v = v * 10u + d;
    }
    return v;
}

// ── Is the corpus exactly TR1, TR2, ... TRN, in order? ───────────────────────
//
// The per-session isolation oracle. A per-session counter gives each session a
// sequence contiguous from 1; a process-global one gives at least one session a
// gap (TR1, TR3, TR5, ...) or a non-1 start.
//
// File-scope, not a lambda per test: both tests below need it, and this file's own
// history is the argument. Its two copies had drifted on other axes — one took a
// `session_name` parameter it never used; the other materialised an intermediate
// `std::vector<std::uint32_t>` and rejected `n == 0` explicitly, a check the first
// gets for free because `parse_tr_id` returning 0 already fails the `!= i + 1`
// comparison. BOTH copies carried the same empty-corpus early return (#309);
// neither was safe by it — unifying them removes one spelling of the predicate,
// not a defect unique to either copy. An empty corpus is a collapse, not a
// pass: the first line of the function rejects it directly, so no caller has
// to pin the size for this oracle to be sound.
static bool check_contiguous(const std::vector<std::string>& ids) {
    if (ids.empty()) {
        return false;  // an empty corpus is a COLLAPSE, not a pass (#309)
    }
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (parse_tr_id(ids[i]) != static_cast<std::uint32_t>(i + 1)) {
            return false;
        }
    }
    return true;
}

// ── Bounded pump (#284) ───────────────────────────────────────────────────────
//
// Every `co_spawn(..., use_future)` → `run_for(window)` → `restart()` →
// `fut.get()` site below now waits with `pump_until_ready` instead: it drives
// the context UNTIL the operation completes, bounded by a real-time budget.
// The helper is the one logout_exchange_test.cpp introduced for this same
// defect, hoisted to tests/support; its header carries the full rationale, the
// failure text, and the teardown guard the budget path needs.
using fixpp::test_support::kPumpBudgetMiss;
using fixpp::test_support::pump_until;
using fixpp::test_support::pump_until_ready;

// ── Bounded wait for a SELF-DRIVING executor (#309/#317) ─────────────────────
//
// `pump_until` above does not apply to `ConcurrentSessionsTSanStress`: it runs on
// an `asio::thread_pool`, whose own threads service the work, so there is nothing
// for the test thread to pump and calling `run_for` on it is not even expressible.
// The waiting still has to be on the OBSERVABLE EVENT rather than on a fixed
// `sleep_for`, for the reason #284 records: a fixed window that under-serves does
// not hang, it silently shortens the corpus every assertion downstream runs over.
//
// #317 REPLACED the generic `wait_until_observed(pred, budget, slice)` that used to
// live here — a 1 ms sleep-poll over a caller-supplied predicate — with
// `CaptureTransport::await_test_req_ids`, which blocks on the writer's own
// condition_variable. The poll had two costs, and only the first is the one #317
// was filed about:
//   1. its predicate rebuilt the whole TestReqID corpus per tick (O(corpus) per
//      tick => O(iterations^2) overall);
//   2. the 1 ms slice put a floor of ~1 ms x iterations under the test regardless
//      of how fast the pool actually was.
// Notifying from the writer removes both. Nothing else in this file waited on that
// helper, so it is deleted rather than left as a second way to do this.
//
// ⚠️ This deletion removes ONE of the three file-local copies #315 catalogues
// (`wait_pred_nodrive` in test_engine_session_strand.cpp and
// `wait_for_pred_nodrive` in test_business_messages_roundtrip.cpp remain). It does
// NOT close #315, and it is not a template for it: the cv works here only because
// this file's waiter and its writer are the same object. A hoisted general helper
// still needs the poll-and-yield shape for predicates with no writer to hook.
inline constexpr auto kWaitBudget = std::chrono::seconds{10};

// Failure text for a wait that ran out of budget. Distinct from
// `kPumpBudgetMiss` because the mechanism is distinct: a miss here means the pool
// never produced the event, not that this thread failed to drive a context.
inline constexpr const char* kWaitBudgetMiss =
    "#309: the pool did not produce the awaited event within the wait budget. Site: ";

// ── Session fixture helper ─────────────────────────────────────────────────────

struct SessionFixture {
    fixpp::core::EngineConfig engine;
    fixpp::session::SessionConfig cfg;
    CaptureTransport transport;
    std::unique_ptr<fixpp::session::Session> session;

    // (#303) DIRECT observation of whether this fixture was destroyed, for the
    // teardown witnesses below. nullptr everywhere else, so the two real tests
    // are unaffected.
    //
    // This exists because the obvious probe is FORGEABLE. The natural witness for
    // "the guard released the fixtures" is a weak_ptr on the fixture-owned clock:
    // if the fixture was destroyed, its EngineConfig shared_ptr copy dies and the
    // weak_ptr expires. But that observes CLOCK RETENTION, not fixture identity —
    // a mutant that drops the release() while copying the clock somewhere else
    // keeps the weak_ptr live and passes. #292 hit exactly this and had to record
    // it as an accepted gap, because closing it there needed a src/ seam
    // (tests/interop/support/interop_fixture_test.cpp:191-203).
    //
    // Here it costs nothing: SessionFixture is file-local, so it can count its own
    // destructions and no spelling of the failure message or of the clock graph can
    // fake a destructor that did not run. The counter must be declared OUTSIDE the
    // scope under test — it is read after that scope has exited.
    std::atomic<int>* destructions = nullptr;

    SessionFixture(asio::any_io_executor ex, std::shared_ptr<fixpp::core::mock_clock> clk,
                   std::string_view sender, std::string_view target) {
        engine.executor = ex;
        engine.clock = clk;

        cfg.sender_comp_id = std::string(sender);
        cfg.target_comp_id = std::string(target);
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = std::chrono::seconds{1};
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ex;
        cfg.transport_send = [this](std::span<const std::byte> frame) { transport.capture(frame); };
        // RC#C (gate-b/r1): bilateral_lenient — test exercises TestReqID cross-session, not reset.
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;

        session = std::make_unique<fixpp::session::Session>(engine, cfg);
    }

    ~SessionFixture() {
        if (destructions != nullptr) {
            destructions->fetch_add(1, std::memory_order_relaxed);
        }
    }

    // Open the session (initiator: emits Logon, waits for Logon-ack).
    // Returns the future for the open() awaitable.
    [[nodiscard]] std::future<fixpp::core::expected_t<void>> async_open(asio::thread_pool& pool) {
        return asio::co_spawn(pool, session->open(), asio::use_future);
    }

    // Feed a peer Logon-ack to drive session into Active state.
    [[nodiscard]] std::future<fixpp::core::expected_t<void>> async_feed_logon(
        asio::thread_pool& pool) {
        auto logon_bytes =
            make_logon_frame("FIX.4.2", 1, cfg.target_comp_id, cfg.sender_comp_id, 1);
        return asio::co_spawn(pool,
                              session->on_inbound_frame(std::span<const std::byte>{
                                  logon_bytes.data(), logon_bytes.size()}),
                              asio::use_future);
    }

    // Close the session.
    [[nodiscard]] std::future<fixpp::core::expected_t<void>> async_close(asio::thread_pool& pool) {
        return asio::co_spawn(pool, session->close(fixpp::session::close_mode::terminal),
                              asio::use_future);
    }
};

// (#311 review) REPLACES an earlier `observe_ioc_release`, which read the
// caller's owner after the guard and reported `owner == nullptr`. That is the
// weaker observable and it admitted a mutant that violates the exact invariant:
//
//     auto* p = ioc.release();
//     delete p;                 // owner is still null; the context is gone
//
// The property is the DESTRUCTION, so the destruction is what gets counted. A
// second probe was tried and also rejected -- a shared_ptr sentinel captured by a
// handler queued on the context dies when the handler is DISPATCHED just as
// readily as when it is destroyed, so it cannot see an unconditional release on
// any path that pumps. Measured on the sibling interop fixture: that version
// passed 21/21 against precisely that mutant.
//
// Derives from `asio::io_context`, which has no virtual destructor: instances are
// only ever owned by `std::unique_ptr<counting_io_context>` and only ever deleted
// through it, never through a base pointer. `~counting_io_context` runs before
// `~io_context`, so the count is taken as destruction begins.
struct counting_io_context : asio::io_context {
    std::atomic<int>* destructions = nullptr;

    ~counting_io_context() {
        if (destructions != nullptr) {
            destructions->fetch_add(1, std::memory_order_relaxed);
        }
    }
};

// ── (#303) Teardown guard: quiesce, or RELEASE the fixtures ───────────────────
//
// Replaces `quiesce_on_exit` at this one site. (#322) The premise here used to be
// "the shared guard only OBSERVES residual work and says so itself", citing a
// header sentence that has since been deleted -- the guard now carries forcing
// levers (a per-slice `cancel_sleeps()`, and a transport close when one is set)
// and its comment says so. What it still does NOT have, which is the whole reason
// this seam exists, is a RELEASE branch: on the budget-exhausted path it returns,
// the fixtures are destroyed, and only afterwards does `~io_context` destroy
// coroutine frames that borrowed them. This guard closes that by making the same observation
// DECIDE something: if the context did not quiesce, the fixtures are deliberately
// released (leaked).
//
// WHY LEAKING IS THE FIX AND NOT A SHORTCUT. The obvious symmetry — declare the
// fixtures before `ioc` so they outlive it — is REFUTED by measurement.
// `SessionConfig::mode` defaults to `per_session_strand`, so `Session::exec_` holds
// an `asio::make_strand(...)` handle, and `~strand_impl` unlinks through a raw
// `service_` pointer that `~execution_context` has already destroyed. Reproduced in
// THIS binary while preparing this change: an `any_io_executor` holding a strand and
// destroyed after its `io_context` reports `heap-use-after-free` at
// `asio/detail/impl/strand_executor_service.ipp:88`, while the bare-executor control
// arm is clean. Reordering would convert a conditional, budget-exhausted hazard into
// an unconditional one on every run.
//
// So the load-bearing property of the release is not "we skipped a free". It is:
//
//     ON THE RESIDUAL PATH, EVERY ROOT OF THE ASYNC OBJECT GRAPH -- EVERY
//     SessionFixture, AND THE io_context -- IS RELEASED, SO NOTHING IN THAT GRAPH
//     IS DESTRUCTED.
//
// Which subsumes the narrower form this paragraph used to state ("a released fixture
// is never destroyed, therefore its Session's strand handle is never destroyed,
// therefore the order that would fault never arises"). The narrower form was not
// wrong, it was incomplete, and the missing half was not free: releasing the
// fixtures while still destroying the io_context strands `outstanding_work_`, which
// POSIX ignores and Windows spins on forever. See the guard's `ioc` member for the
// measurement.
//
// Stated as an invariant rather than a mechanism because a future cleanup pass that
// "fixes the leak" by destroying any of these on the way out would reintroduce
// either the use-after-free the reorder was refuted for, or the Windows wedge.
//
// THE WHOLE FIXTURE, NOT JUST THE SESSION: `Session` holds
// `const fixpp::core::EngineConfig& engine_` — a REFERENCE into
// `SessionFixture::engine` (include/fixpp/session/session.hpp:621) — and its
// by-value `cfg_` copy carries a `transport_send` lambda capturing
// `SessionFixture*`. Releasing only the Session would leave both dangling.
//
// ONE COMPUTATION OF "DID IT QUIESCE". The quiesce and the release live together
// here on purpose. Keeping `quiesce_on_exit` and adding a second guard that re-read
// `ioc.stopped()` afterwards would split one decision across two files, and a
// divergence between them would fail toward NOT releasing — silently, on the path
// that is already failing.
//
// THE POPULATION — recurs elsewhere in the tree; not a lone site. The first two
// versions of this paragraph tried to pin that population as a fixed number and
// were each wrong in the direction that made staying file-local look better than
// the evidence supports:
//
//   v1 said "one site is not a pattern."          Wrong: the shape repeats.
//   v2 said "a handful more sites, but each attaches
//            a live Transport, so they have a
//            forcing lever this one lacks."       Wrong twice over, below.
//
// A third number would go stale the same way: #319 added a site to the
// population it counts (see `logout_exchange_test.cpp`'s doc comment on
// `FeedInboundSpansTheArenaCopyNotTheCallersBuffer`). So this states the
// CONDITION instead of a count: the population is every PLAIN `quiesce_on_exit`
// OBJECT DECLARATION under `tests/` — deliberately NOT `quiesce_or_release_on_exit`
// (the seam this file defines below), since the whole point of this paragraph is
// to compare the population of plain-shape sites against this file's one seam
// user, not to fold the seam into the count it's being compared to. Also
// excluded: this helper's own self-test (`test_quiesce_on_exit_residual.cpp`),
// the struct definition, its destructor, and any comment or string literal that
// merely names it. Re-derive on demand:
//
//   grep -rn 'quiesce_on_exit [a-zA-Z_]*{' tests/
//
// Not a count in itself — resolve every hit by hand. Run at HEAD, its hits fall
// into: real object declarations (the population); `test_quiesce_on_exit_residual.cpp`
// (excluded, the self-test); the struct definition itself
// (`tests/support/pump_until_ready.hpp`, `struct quiesce_on_exit {` — also a
// zero-width match of this pattern); and the comment hits inside THIS file --
// the parenthetical just above and the sentence a few paragraphs down that names
// the shape by example (each readable in context, neither a declaration) — note this pattern, by
// construction, does NOT match `quiesce_or_release_on_exit` (no shared contiguous substring past
// `quiesce_`), so it never picks up this file's own seam declarations, which is
// the intended scope. The pattern also misses paren-init (`quiesce_on_exit
// q(...)`), a declaration split across lines, and `auto`/alias-typed forms, so a
// future absence of those forms is not itself evidence none exist.
//
// And the TRANSPORT lever is absent for most of the population: `.transport = `
// is set only in `test_live_outbound_serialized.cpp` — `logout_exchange_test.cpp`
// sets it nowhere. So `logout_exchange_test.cpp`'s sites each declare
// `Session sess(engine, cfg);` followed immediately by
// `quiesce_on_exit quiesce{ioc, *clock};` with no transport attached — which is
// exactly this file's shape.
//
// ⚠️ (#322) NARROWED FROM "no way to force quiescence", WHICH IS NOW FALSE. Every
// guard, two-argument included, forces one thing: it delegates to
// `cancel_and_drain_or_report`, whose alternating cancel-then-drain loop releases
// a clock sleep armed DURING the drain. What a transport-less site lacks is only
// the lever for a coroutine parked in async_write/async_read_some, which no
// amount of sleep-cancelling reaches. The distinction matters here because this
// paragraph is a POPULATION argument: the old wording made every site it
// enumerates sound leverless.
//
// Stated plainly, because the understated version is the one that keeps getting
// written: this is NOT a lone site. What was actually measured (below) is the
// state `poll_one()` observes AT THE GUARD — zero outstanding work, at every
// population member, on the path CI takes today. Whether a suspended frame could
// reach that same guard on an ASSERT_*-early-return path is a DIFFERENT question,
// and it is unmeasured except at the one site built to take such a path on purpose
// (`test_live_outbound_serialized.cpp`'s `BudgetMissQuiescesBeforeSessionTeardown`).
// Both statements are true at once; an earlier draft of this comment let the first
// read as proof of the second, which it is not.
//
// PRIOR ART, named so nobody has to rediscover it: release-on-residual is not
// invented here. `tests/interop/support/interop_fixture.cpp:95-160`
// (`~InteropEngineFixture`) already does drive-to-completion, then `release()` +
// `__lsan_ignore_object` + a named `ADD_FAILURE`, for a single `Engine`. This is the
// mechanism's SECOND occurrence, not its first.
//
// WHY IT STAYS FILE-LOCAL — and this reason REPLACED an earlier one that has since
// expired, which is worth saying rather than quietly editing. The first version of
// this paragraph said the seam stays here because `tests/support/pump_until_ready.hpp`
// was owned by an open PR and could not be edited from this branch. That was true
// when written and is no longer: #301 merged, and the file is editable now. A
// justification that rests on a temporary fact has to be re-earned when the fact
// changes, not left standing because it once held.
//
// WHAT THE ZERO ACTUALLY MEASURES, AND WHAT IT DOES NOT. Every population member
// (condition above) was instrumented and run: at each one, `poll_one()` reports
// the `io_context` holds ZERO outstanding work by the time the guard's destructor
// finishes running. But that reading is taken INSIDE the guard, AFTER
// `transport->close()` and `clock.cancel_sleeps()` have already run — so at the
// lever-bearing sites (`test_live_outbound_serialized.cpp`, `.transport = ` set)
// it records "the lever drained the suspended frame", not "no suspended frame
// existed". Measured directly: setting `BudgetMissQuiescesBeforeSessionTeardown`'s
// guard budget to `0ms` reads zero WITH its forcing lever in place, and with the
// lever removed ("`teardown_guard.transport = raw_ptr;`" commented out) prints
// "terminate called without an active exception" instead. So the earlier claim
// that no suspended frames are left behind anywhere is false at that site —
// deleted rather than kept, because the guard's own zero is exactly what a lever
// produces there, not evidence a lever was unnecessary.
//
// A release seam nonetheless has exactly ONE user — this site — but the ground for
// that is not "the population is measured empty": it is what each member borrows,
// checkable by reading rather than by an unreproducible instrumentation run.
// `logout_exchange_test.cpp`'s `feed_inbound`-driven sites need no seam because
// they borrow nothing that dies first: `feed_inbound_spawn` copies each inbound
// frame into `LogoutExchangeTest::inbound_frames`, a fixture-owned deque declared
// before `ioc` (see that member's own doc comment, and `feed_inbound_spawn`'s
// body), and the spawned coroutine spans THAT, not the caller's temporary. That
// argument covers every site that reaches the guard through `feed_inbound`; it
// does NOT cover every `quiesce_on_exit` declaration in the file — one named
// exception:
//
//   - `SessionGracefulCloseFlushesFileStore.FlushRunsAndFramesDurableAfterClose`
//     hands `on_inbound_frame` a body-local `std::vector` directly, with no
//     `feed_inbound` in the picture. It is safe for a DIFFERENT reason: that
//     buffer is declared BEFORE its `quiesce_on_exit`, in the same block, so the
//     guard's drain runs while the buffer is still alive.
//
// `FeedInboundSpansTheArenaCopyNotTheCallersBuffer` (this PR's own witness) is
// NOT a second exception, even though it deliberately lets the caller's buffer
// die BEFORE the pump — that is the point of the test: it is the argument's
// clearest instance, safe only because the arena copy is what gets spanned,
// which is the `feed_inbound` argument verbatim, not a case of "nothing here
// ever dies first".
//
// `test_live_outbound_serialized.cpp`'s sites each carry their own `frames`
// deque declared before `ioc` the same way, plus, at the one site that
// deliberately reaches the guard on an abnormal path, a working transport lever
// that closes what the deque-ordering argument alone does not cover. All of this
// is a fact about the tree rather than an assumption about it. Hoisting a generic
// seam for one user is the speculative generality the repo's own extraction rule
// warns against.
//
// When a SECOND site genuinely needs it, the right hoist is NOT templating on
// `SessionFixture` — it is a `std::vector<std::function<void()>>` of release-closures,
// which decouples the guard from `SessionFixture` and `Engine` alike and would let the
// interop fixture above collapse into the same seam instead of remaining a second
// implementation.
//
// `quiesce_on_exit`'s `transport` arm is deliberately absent: it exists because
// cancelling clock sleeps does not unstick a coroutine parked in
// async_write/async_read_some, and only closing the transport does. This test drives
// a `mock_clock` and a plain `transport_send` lambda; there is no
// `fixpp::transport::Transport` in play, so there is nothing to close.
struct quiesce_or_release_on_exit {
    // OWNED, not merely referenced, and released with the fixtures on the residual
    // path. That is a Windows requirement, not a tidiness preference, and it was
    // found by CI rather than by reading: `~io_context` is NOT symmetric across
    // platforms.
    //
    //   asio/detail/impl/scheduler.ipp        `scheduler::shutdown` (POSIX)
    //       drains its own op queue and RETURNS. A work count nothing will ever
    //       decrement is simply ignored.
    //   asio/detail/impl/win_iocp_io_context.ipp `win_iocp_io_context::shutdown`
    //       runs `while (outstanding_work_ > 0)`, and can only decrement by
    //       destroying operations it can FIND — its timer queues, `completed_ops_`,
    //       and whatever `GetQueuedCompletionStatus` hands back. Work counted by a
    //       guard held inside a LEAKED object is none of those. The loop never
    //       terminates.
    //
    // Releasing the fixtures is precisely what can strand that count, and
    // destroying the fixtures is what used to release it. So on Windows the old
    // shape wedged `~io_context` forever — all
    // three MSVC legs of PR #304 timed out at 120 s in exactly this test, at
    // exactly this line, while Tier 1 and Tier 3 were green.
    //
    // THE RESIDUAL BRANCH IS TWO DIFFERENT STATES, and an earlier version of this
    // paragraph collapsed them — it said the branch means `outstanding_work_ > 0`
    // "by definition". That is true of only one of them:
    //
    //   OBSERVED residual — the pump returned and `poll_one()` left the context
    //       unstopped. `poll_one()` stops the context iff the count is zero, so
    //       here the count really is positive.
    //   UNKNOWN residual — `run_for` or `poll_one` THREW, and the inner
    //       `catch (...)` set `quiesced = false`. A throwing handler can have been
    //       the last outstanding operation, so the count may well be zero by now.
    //
    // The release is right in both cases, but for different reasons: in the first
    // because work demonstrably remains, in the second because the state is
    // unknown and destroying a graph whose frames may still be suspended is the
    // unsafe direction. Only the first is a claim about the work count. The
    // failure text below is worded to cover both without asserting the count.
    //
    // Measured on a 9-line standalone probe, no coroutines, no Session, no clock —
    // a stack `io_context` plus one leaked `executor_work_guard`:
    //
    //             arm                                     MSVC 19.39     clang/Linux
    //   A  stack io_context, work stranded            HANGS (killed 12 s)  returns
    //   B  io_context itself leaked                        returns         returns
    //   C  control, nothing stranded                       returns         returns
    //
    // Arm B is this member. The invariant it buys is SHORTER than the one it
    // replaces, which is the real argument for it:
    //
    //     ON THE RESIDUAL PATH, EVERY ROOT OF THE ASYNC OBJECT GRAPH -- EVERY
    //     SessionFixture, AND THE io_context -- IS RELEASED, SO NOTHING IN THAT
    //     GRAPH IS DESTRUCTED.
    //
    // Scoped to the GRAPH, not to the scope. An earlier wording said "nothing in
    // this scope is destroyed", which is simply false and worth correcting rather
    // than quietly rephrasing: the guard itself, its vector, the emptied
    // `unique_ptr`s, the local futures and `shared_ptr`s, the alias, and the frame
    // arena are all destroyed — `CrossSessionDisjoint`'s `frames` deque
    // deliberately so, AFTER the released context. What must not be destructed is
    // the graph those roots own.
    //
    // No destruction of the graph means no destruction ORDER within it, so the
    // strand-after-io_context fault and the frame-over-dead-storage fault are both
    // unreachable by construction rather than by an ordering argument that has to
    // be re-derived every time a member moves.
    std::unique_ptr<counting_io_context>& ioc;
    fixpp::core::Clock& clock;
    // Released, in order, if the context does not quiesce. Pointers rather than
    // values so the guard can null the caller's own owners — leaving a released
    // `unique_ptr` behind means the caller's later destruction is a no-op rather
    // than a double free.
    std::vector<std::unique_ptr<SessionFixture>*> fixtures;
    std::chrono::steady_clock::duration budget = std::chrono::seconds{5};

    ~quiesce_or_release_on_exit() {
        // Nothing may escape a destructor. ADD_FAILURE can throw under
        // --gtest_throw_on_failure, and the pump can throw out of a handler; neither
        // may skip the release or propagate.
        try {
            bool quiesced = false;
            try {
                clock.cancel_sleeps();
                ioc->restart();
                ioc->run_for(budget);
                // THE PROBE, and it is not decoration. `io_context::run_for` is
                // `run_until`, and `run_one_until` tests `now < abs_time` BEFORE
                // entering the scheduler (asio impl/io_context.hpp:108-131), so a run
                // whose deadline has already passed returns WITHOUT ever consulting
                // the work count — leaving the just-restarted context unstopped even
                // when it holds no work at all. That is unconditional for a zero
                // budget and reachable at any budget's deadline boundary.
                //
                // `poll_one()` closes it: it calls `stop()` when `outstanding_work_`
                // is already zero (asio detail/impl/scheduler.ipp:289-295), so after
                // this line `stopped()` reflects the work count rather than the
                // deadline.
                //
                // ⚠️ IT MAY DISPATCH ONE HANDLER, AND A DISPATCH RESUMES A SUSPENDED
                // COROUTINE. That is safe with respect to the FIXTURES — they are all
                // still alive at this point, which is exactly why draining here and
                // not later is the right place — but it inherits the obligation the
                // `run_for` above already carries: any storage a suspended frame
                // borrowed must outlive this guard, or the resumption reads dead
                // memory. `CrossSessionDisjoint`'s frame arena is declared before
                // `ioc` for this reason. Not hypothetical: the residual witness below
                // was written with a block-local buffer first and this probe faulted
                // it under ASan.
                //
                // Because that obligation is NEW relative to the guard this replaces,
                // the fault oracle was re-run against THIS guard rather than against
                // `quiesce_on_exit` — the first run predated the probe, so it never
                // exercised a resumption at all and could not have spoken to it. Two
                // arms at a zero budget, one with a frame parked at its initial
                // suspend point and one with two Active sessions plus a frame
                // deliberately left mid-flight for this probe to resume: both reach
                // the residual branch, both are ASan- and LSan-clean.
                //
                // This guard was written first, when the shared header still claimed
                // that `stopped()==false` necessarily means outstanding work — a claim
                // that omits this window. That defect is now fixed at the source
                // (#305), and (#322) the probe now lives in ONE place there:
                // `cancel_and_drain_or_report`. `drain_or_report` carries its own copy
                // and `~quiesce_on_exit` reaches it by DELEGATING — the guard no longer
                // carries a probe of its own, so this is not a two-member list of
                // copies. The header's claim is corrected in place. The probe stays
                // duplicated here because this guard does not delegate to any of
                // them — it computes the verdict itself so the release decision cannot
                // drift from it — not because the shared version is still wrong.
                (void)ioc->poll_one();
                quiesced = ioc->stopped();
            } catch (...) {
                // An exception mid-pump leaves the residual UNKNOWN. Fail safe:
                // treat it as residual and release, rather than destroy fixtures
                // whose frames may still be suspended.
                quiesced = false;
            }

            if (quiesced) {
                return;  // fixtures destroy normally on the caller's own unwind
            }

            // Release BEFORE reporting, so a throwing ADD_FAILURE cannot leave the
            // fixtures to be destroyed under still-suspended frames.
            for (auto* owner : fixtures) {
                auto* leaked = owner->release();
                FIXPP_XSESSION_LSAN_IGNORE(leaked);
            }

            // And the io_context itself. Releasing the fixtures without releasing
            // this one is the shape that wedges `~io_context` on Windows forever —
            // see the member's own comment for the measurement. The two releases
            // are ONE decision and must not be separated, and neither is
            // conditional on anything but `quiesced`.
            //
            // The release is its own statement, exactly as the loop above does it,
            // and NOT `FIXPP_XSESSION_LSAN_IGNORE(ioc.release())`. That spelling
            // works today only because the non-sanitizer expansion happens to be
            // `((void)(p))`; a later `((void)0)` -- the ordinary way to silence an
            // unused-parameter warning -- would silently delete this release on
            // every non-ASan build, which is EVERY MSVC build, which is the only
            // platform that wedges without it.
            auto* leaked_ioc = ioc.release();
            FIXPP_XSESSION_LSAN_IGNORE(leaked_ioc);

            ADD_FAILURE()
                << "quiesce_or_release_on_exit: the io_context was not observed to run out "
                   "of work within the configured quiesce window -- either it demonstrably "
                   "still holds work, or the pump threw and the state is unknown -- so a "
                   "coroutine frame may still be suspended. Every root of the async graph, "
                   "the SessionFixtures AND the io_context, was RELEASED deliberately "
                   "(#303/#311), so nothing in that graph is destructed: no frame is resumed "
                   "over dead storage, no Session strand handle is destroyed after its "
                   "io_context, and ~io_context never runs -- which on Windows can otherwise "
                   "spin forever, since win_iocp_io_context::shutdown loops while "
                   "(outstanding_work_ > 0) and decrements only by destroying operations it "
                   "can find. This guard reports the residual; it does not claim to have "
                   "found its cause.";
        } catch (...) {
            // Nothing may escape a destructor.
        }
    }
};

}  // anonymous namespace

// ── CrossSessionTestReqIDParser — proves the hardening guards can fail (#309) ─
//
// #309 Gate B F5: `parse_tr_id`'s leading-zero and overflow guards (added in
// this PR) shipped with no instrument that could see them fail — deleting
// either guard left the whole binary green. The two EXPECT_FALSE lines below
// are the minimum discriminating instrument, verified by hand and by deleting
// each guard in turn and confirming this test goes RED (see the commit message
// for both RED runs). A small positive/negative table around them pins
// `parse_tr_id` more broadly; the two lines above it are what matter.
//
// #309 Gate B F3a: also proves `extract_tag`'s field-boundary anchoring — a
// tag-112 lookalike (`9112=`) must not be laundered into the corpus as tag 112.
//
// LSan-block discharge (see the CAPS-LOCKED rule above
// `FIXPP_XSESSION_HAVE_LSAN`): this test constructs no `SessionFixture`, no
// `io_context`/`counting_io_context`, and no `quiesce_or_release_on_exit`
// guard — it calls only the pure file-local helpers `parse_tr_id`,
// `check_contiguous`, and `extract_tag`. It cannot reach the fixture-release
// path, so the witness list, the fixture column, and the byte/allocation
// totals in that block stay valid unchanged.
TEST(CrossSessionTestReqIDParser, RejectsNonCanonicalAndOverflowCorpora) {
    // The two discriminating lines (F5). With the guards in place, parse_tr_id
    // returns 0 for both inputs and 0 != 1 fails contiguity. Without the
    // leading-zero guard, "0001" accumulates to 1; without the overflow guard,
    // "4294967297" wraps mod 2^32 to 1 — either way check_contiguous would
    // return true and these EXPECT_FALSE would fire.
    EXPECT_FALSE(check_contiguous({"TR0001"}));
    EXPECT_FALSE(check_contiguous({"TR4294967297"}));

    // #309 Gate B round 2, P1-1(b): an empty corpus must not read as a pass —
    // the zero-iteration loop falls through to `true` unless rejected directly.
    EXPECT_FALSE(check_contiguous({}));

    // Cheap positive/negative table for parse_tr_id.
    struct Case {
        std::string_view input;
        std::uint32_t want;
    };
    const Case cases[] = {
        {"TR1", 1},  {"TR10", 10}, {"TR4294967295", 4294967295u},
        {"TR0", 0},  {"TR01", 0},  {"TR", 0},
        {"TRx", 0},  {"XR1", 0},   {"TR1x", 0},
        {"TR1 ", 0},
    };
    for (const auto& c : cases) {
        EXPECT_EQ(parse_tr_id(c.input), c.want) << "input: " << c.input;
    }

    // F3a: a tag-112 lookalike ("9112=") must not be laundered into the corpus.
    // Before the field-boundary fix, extract_tag's `wire.find("112=")` matched
    // inside "9112=" and returned "TR1"; after the fix it returns "" because
    // "9112=" has no frame-start / SOH immediately before "112=".
    const std::string frame =
        "8=FIX.4.2\x01"
        "35=1\x01"
        "9112=TR1\x01"
        "10=000\x01";
    EXPECT_EQ(
        extract_tag(std::span<const std::byte>{reinterpret_cast<const std::byte*>(frame.data()),
                                               frame.size()},
                    112),
        "");

    // F3a resume: a rejected lookalike must not abort the search — the later
    // boundary-anchored 112= is the real tag.
    const std::string resume_frame =
        "8=FIX.4.2\x01"
        "35=1\x01"
        "9112=decoy\x01"
        "112=TR1\x01"
        "10=000\x01";
    EXPECT_EQ(extract_tag(
                  std::span<const std::byte>{
                      reinterpret_cast<const std::byte*>(resume_frame.data()), resume_frame.size()},
                  112),
              "TR1");

    // #318: the UNTERMINATED-VALUE arm. An accepted, boundary-anchored tag whose
    // value has no closing SOH must extract to "" rather than to the rest of the
    // buffer. Inherited from before #314 (verbatim in a7680342's removed hunk,
    // under a bare `wire.find(needle)` with no boundary check), and named as the
    // excluded mutant by #314's Gate B stopping bound.
    //
    // A SEPARATELY NAMED frame, not an extension of `frame` / `resume_frame` /
    // `start_frame`: each of those is the only instrument killing its own mutant,
    // so extending one in place would trade a surviving mutant for another.
    //
    // WHAT THIS KILLS, enumerated rather than declared exhaustive:
    //   - deleting the `end == npos` guard  → `substr(vstart, npos - vstart)`
    //     yields the rest of the buffer, "TR1" != "". KILLED by the 112 line.
    //   - `return {}` → `return wire.substr(vstart)`. Same, KILLED.
    //   - `return {}` → `continue` / `break`. These are EQUIVALENT mutants, not a
    //     gap, and no assertion can kill them. Proof: `end == npos` means there is
    //     no SOH at or after `vstart`. A later hit at `pos' > pos` is accepted only
    //     if `wire[pos' - 1] == '\x01'`. If `pos' - 1 >= vstart` that contradicts
    //     the npos. Otherwise `pos' - 1` lies inside the needle span
    //     [pos, vstart), whose bytes are the needle's own ("112=") and none is SOH.
    //     So no later hit is ever accepted; `continue` falls through to the
    //     function's trailing `return {}` and yields "" too. Recorded as an
    //     argument because a test claiming to kill it would be a false instrument.
    //
    // The tag-35 line is the NON-VACUITY control: it proves this frame is
    // parseable and that the "" above comes from the unterminated arm, not from a
    // malformed corpus that would return "" for every tag.
    const std::string unterminated_frame =
        "8=FIX.4.2\x01"
        "35=1\x01"
        "112=TR1";  // deliberately NO trailing SOH
    EXPECT_EQ(extract_tag(std::span<const std::byte>{
                              reinterpret_cast<const std::byte*>(unterminated_frame.data()),
                              unterminated_frame.size()},
                          112),
              "");
    EXPECT_EQ(extract_tag(std::span<const std::byte>{
                              reinterpret_cast<const std::byte*>(unterminated_frame.data()),
                              unterminated_frame.size()},
                          35),
              "1");

    // #320: the EMPTY-span CONTRACT for the hoisted helper — an empty frame yields
    // "". Worth pinning because twelve call sites now share this function.
    //
    // ⚠️ THIS IS A CONTRACT ASSERTION, NOT A MUTATION-KILLING INSTRUMENT, and the
    // distinction is measured rather than asserted: deleting the helper's
    // `frame.empty()` guard leaves this line PASSING (verified by deleting it and
    // re-running). `std::string(nullptr, 0)` builds an empty string silently on
    // this toolchain even under -fsanitize=address,undefined, so there is no fault
    // for this assertion to observe. An earlier version of this comment claimed it
    // proved the guard; it does not. Do not cite it as evidence for the guard.
    EXPECT_EQ(extract_tag(std::span<const std::byte>{}, 112), "");

    // F3a frame start: the boundary rule also ACCEPTS a hit at byte 0 (`pos != 0`
    // in the guard). Tag 8 is mandatorily the first field of a FIX frame, so this
    // is the branch that keeps the helper a general FIX-tag extractor.
    const std::string start_frame =
        "8=FIX.4.2\x01"
        "35=1\x01"
        "112=TR1\x01"
        "10=000\x01";
    EXPECT_EQ(extract_tag(
                  std::span<const std::byte>{reinterpret_cast<const std::byte*>(start_frame.data()),
                                             start_frame.size()},
                  8),
              "FIX.4.2");
}

// ── Test 1: CrossSessionDisjoint ──────────────────────────────────────────────
//
// RED assertion (a): session A's TestReqID sequence is contiguous from TR1,
// and so is session B's. With a `static tr_counter`, both sessions share the
// counter so IDs interleave → each session's own sequence has GAPS and the
// assertion fails in the RED phase.
// With `++next_test_request_id_` (per-session), each sequence is contiguous
// (the two sequences are IDENTICAL, not disjoint — see the note below).
//
// GREEN assertion (b): within each session, IDs increment monotonically.
//
// SC-003 quantifies 10^4 TestRequests per session; this test pins its own
// smaller corpus with a fatal equality at `kIterations` below (fast test;
// TSan is the stress mechanism, not the count).
TEST(CrossSessionTestReqID, CrossSessionDisjoint) {
    // Arena for inbound frame buffers. `on_inbound_frame` takes its span by
    // value into the coroutine frame, so a block-scoped buffer dies with its
    // block on ASSERT_TRUE's early return while the suspended coroutine still
    // holds a span over it — `deque::push_back` never invalidates references to
    // existing elements, so a span over `frames.back()` stays valid for the
    // arena's whole lifetime.
    //
    // Declared BEFORE `ioc` (#293), therefore destroyed AFTER it. The invariant
    // that matters is NOT "the arena outlives the guard" — that is the weaker
    // property, and it was all the previous comment here claimed. It is: THE
    // ARENA MUST OUTLIVE EVERY READ THROUGH THE BORROW — every resume of, and
    // every cleanup that dereferences, a frame holding a span into it.
    //
    // Stated that way rather than as "outlives every frame `ioc` destroys"
    // (gate-b/r2 P3-2), which is stronger than necessary and would misdescribe
    // why this is safe: destroying a suspended frame destroys a `std::span` by
    // value, and that trivial destructor does not touch the pointed-to bytes.
    // Declaring the arena before `ioc` satisfies the requirement with margin —
    // it outlives the frames themselves, hence every read through them. `quiesce_on_exit`
    // below fixes the ORDER of destruction relative to itself. (#322) It also has a
    // forcing lever now, and this sentence used to deny it: the guard delegates to
    // `cancel_and_drain_or_report`, whose alternating cancel-then-drain loop releases a
    // clock sleep armed DURING the drain, which a one-shot cancel misses. It still only
    // REPORTS a residual neither that lever nor a transport close can reach; either way
    // this declaration order does not depend on it. On the budget-exhausted path the
    // guard now releases the context rather than destroying it (#311), so no
    // frame is destroyed at all here and this declaration order is
    // belt-and-braces rather than load-bearing.
    //
    // `frames` is pure storage — it holds no executor and no strand — so it is
    // safe for it to outlive `ioc`. That is NOT true of `sA`/`sB` below, which
    // own Sessions whose executors wrap `asio::make_strand` (session.hpp:360):
    // a strand handle destroyed after its io_context dereferences an already-
    // destroyed service (asio strand_executor_service.ipp:83-94 unlinks through
    // `service_`, which ~execution_context has already destroyed —
    // execution_context.ipp:60-64). Verified with a standalone repro plus a
    // bare-executor control arm. So they deliberately stay AFTER `ioc`.
    //
    // The suspended `on_inbound_frame` frame references `frames` AND `sA`/`sB`.
    // This ordering closes only the `frames` half; the session half is closed
    // below by `quiesce_or_release_on_exit`, which releases the fixtures rather
    // than reordering them (#303 — see that guard for why reordering is refuted).
    std::deque<std::vector<std::byte>> frames;

    // Single io_context so we can coordinate clock advancement.
    // Heap-owned so ~quiesce_or_release_on_exit can RELEASE it on the residual
    // path (see its `ioc` member for why that is mandatory on Windows). `ioc`
    // aliases the same object, so every use below is unchanged.
    auto ioc_owner = std::make_unique<counting_io_context>();
    asio::io_context& ioc = *ioc_owner;
    // Both sessions share the same mock_clock driven by the ioc executor.
    using sc = std::chrono::system_clock;
    auto utc_2024 = sc::time_point{} + std::chrono::seconds{1704067200};
    auto clock = std::make_shared<fixpp::core::mock_clock>(
        utc_2024, fixpp::core::steady_time_point{}, ioc.get_executor());

    // Held by unique_ptr so the teardown guard can RELEASE them (#303). They stay
    // declared AFTER `ioc` deliberately: their Sessions own strand handles, and a
    // strand destroyed after its io_context faults — see the guard.
    auto sA = std::make_unique<SessionFixture>(ioc.get_executor(), clock, "SENDER_A", "TARGET_A");
    auto sB = std::make_unique<SessionFixture>(ioc.get_executor(), clock, "SENDER_B", "TARGET_B");

    // #284 teardown, plus #303's release. On the budget-exhausted path the awaited
    // coroutine is still SUSPENDED and its frame references sA/sB, the clock, and
    // (for on_inbound_frame) a span into `frames`. Declared AFTER the fixtures so
    // it runs BEFORE them, on every exit path including the early `return` an
    // ASSERT_* performs — and so that it can release them while they are still
    // owned. (`frames` is declared above `ioc` — see there — so it is destroyed
    // after BOTH this guard and `ioc`.)
    quiesce_or_release_on_exit quiesce{ioc_owner, *clock, {&sA, &sB}};

    // Open both sessions (initiator path: each emits a Logon immediately).
    auto fut_open_a = asio::co_spawn(ioc, sA->session->open(), asio::use_future);
    ASSERT_TRUE(pump_until_ready(ioc, fut_open_a)) << kPumpBudgetMiss << "opening session A";
    ASSERT_TRUE(fut_open_a.get().has_value()) << "Session A failed to open";

    auto fut_open_b = asio::co_spawn(ioc, sB->session->open(), asio::use_future);
    ASSERT_TRUE(pump_until_ready(ioc, fut_open_b)) << kPumpBudgetMiss << "opening session B";
    ASSERT_TRUE(fut_open_b.get().has_value()) << "Session B failed to open";

    // Drive both sessions to Active by feeding them peer Logon-acks.
    {
        auto& logon_a =
            frames.emplace_back(make_logon_frame("FIX.4.2", 1, "TARGET_A", "SENDER_A", 1));
        auto fut_a = asio::co_spawn(ioc,
                                    sA->session->on_inbound_frame(
                                        std::span<const std::byte>{logon_a.data(), logon_a.size()}),
                                    asio::use_future);
        ASSERT_TRUE(pump_until_ready(ioc, fut_a))
            << kPumpBudgetMiss << "feeding session A's Logon-ack";
        (void)fut_a.get();
    }
    {
        auto& logon_b =
            frames.emplace_back(make_logon_frame("FIX.4.2", 1, "TARGET_B", "SENDER_B", 1));
        auto fut_b = asio::co_spawn(ioc,
                                    sB->session->on_inbound_frame(
                                        std::span<const std::byte>{logon_b.data(), logon_b.size()}),
                                    asio::use_future);
        ASSERT_TRUE(pump_until_ready(ioc, fut_b))
            << kPumpBudgetMiss << "feeding session B's Logon-ack";
        (void)fut_b.get();
    }

    ASSERT_EQ(sA->session->state(), fixpp::session::fsm_state::Active)
        << "Session A must be Active before advancing clock";
    ASSERT_EQ(sB->session->state(), fixpp::session::fsm_state::Active)
        << "Session B must be Active before advancing clock";

    // Advance mock clock to generate TestRequests.
    // HeartBtInt=1s; each 2-second advance (1s window + 1s grace without reply)
    // generates one TestRequest then disconnects. We want many TestRequests without
    // disconnecting, so we feed Heartbeat replies to keep the session alive.
    //
    // Strategy: advance 1s → session emits TestRequest → feed Heartbeat reply to
    // both sessions → repeat 50 times. Each session emits one TR per iteration → 50 each.
    //
    // Advance clock and feed Heartbeat replies to keep sessions alive.
    // Each iteration generates one TestRequest per session.
    std::uint32_t hb_seq_a = 2;  // peer (TARGET_A) inbound seqnum (Logon was 1)
    std::uint32_t hb_seq_b = 2;
    const int kIterations = 50;

    for (int i = 0; i < kIterations; ++i) {
        // Advance by 1.5s to trigger the TestRequest emission, then pump until
        // BOTH sessions have actually emitted this iteration's TestRequest.
        //
        // #284: this was the last fixed window in the test, and it was the one
        // that mattered most — it is the emission driver, so an under-served
        // 20 ms here silently shrinks the corpus every assertion below runs
        // over rather than hanging. Waiting on the emission itself is what lets
        // the count be pinned EXACTLY at kIterations instead of to a band, and
        // a band wide enough to tolerate the old window would have admitted the
        // very collapse it was meant to detect.
        clock->advance(std::chrono::milliseconds{1500});
        const auto want = static_cast<std::size_t>(i + 1);
        ASSERT_TRUE(pump_until(ioc,
                               [&] {
                                   return sA->transport.collect_test_req_ids().size() >= want &&
                                          sB->transport.collect_test_req_ids().size() >= want;
                               }))
            << kPumpBudgetMiss << "waiting for both TestRequests at iteration " << i;

        // Find the most recently emitted TR from each session and echo it back.
        auto tr_ids_a = sA->transport.collect_test_req_ids();
        auto tr_ids_b = sB->transport.collect_test_req_ids();

        std::string latest_a = tr_ids_a.back();
        auto& hb_a = frames.emplace_back(
            make_heartbeat("FIX.4.2", hb_seq_a++, "TARGET_A", "SENDER_A", latest_a,
                           fix_sending_time(clock->now())));
        auto fut_a = asio::co_spawn(
            ioc,
            sA->session->on_inbound_frame(std::span<const std::byte>{hb_a.data(), hb_a.size()}),
            asio::use_future);
        ASSERT_TRUE(pump_until_ready(ioc, fut_a))
            << kPumpBudgetMiss << "feeding session A's Heartbeat at iteration " << i;
        (void)fut_a.get();

        std::string latest_b = tr_ids_b.back();
        auto& hb_b = frames.emplace_back(
            make_heartbeat("FIX.4.2", hb_seq_b++, "TARGET_B", "SENDER_B", latest_b,
                           fix_sending_time(clock->now())));
        auto fut_b = asio::co_spawn(
            ioc,
            sB->session->on_inbound_frame(std::span<const std::byte>{hb_b.data(), hb_b.size()}),
            asio::use_future);
        ASSERT_TRUE(pump_until_ready(ioc, fut_b))
            << kPumpBudgetMiss << "feeding session B's Heartbeat at iteration " << i;
        (void)fut_b.get();
    }

    // Close both sessions before analysis.
    {
        auto fut_a = asio::co_spawn(ioc, sA->session->close(fixpp::session::close_mode::terminal),
                                    asio::use_future);
        ASSERT_TRUE(pump_until_ready(ioc, fut_a)) << kPumpBudgetMiss << "closing session A";
        (void)fut_a.get();
    }
    {
        auto fut_b = asio::co_spawn(ioc, sB->session->close(fixpp::session::close_mode::terminal),
                                    asio::use_future);
        ASSERT_TRUE(pump_until_ready(ioc, fut_b)) << kPumpBudgetMiss << "closing session B";
        (void)fut_b.get();
    }

    // Collect all TestRequest IDs from both sessions.
    auto ids_a = sA->transport.collect_test_req_ids();
    auto ids_b = sB->transport.collect_test_req_ids();

    // Assertion: each session emitted exactly one TestRequest per iteration.
    //
    // #284: the previous `> 0` form was satisfiable by a SINGLE emission, so any
    // change that pumped less would have collapsed the corpus every assertion
    // below runs over while leaving all of them green — the #283/#286 vacuity
    // class. An EQUALITY is only assertable because the loop above now waits on
    // the emission instead of on a fixed window; a tolerance band wide enough to
    // survive that window would have admitted the collapse it claims to catch.
    ASSERT_EQ(ids_a.size(), static_cast<std::size_t>(kIterations))
        << "Session A emitted " << ids_a.size() << " TestRequests, expected exactly " << kIterations
        << " — check clock/liveness wiring";
    ASSERT_EQ(ids_b.size(), static_cast<std::size_t>(kIterations))
        << "Session B emitted " << ids_b.size() << " TestRequests, expected exactly " << kIterations
        << " — check clock/liveness wiring";

    // ── Assertion (a): per-session isolation — sequences are contiguous ───────
    //
    // With per-session ++next_test_request_id_ (GREEN): each session independently
    // counts from 1. Session A produces TR1, TR2, TR3, ... and session B also
    // independently produces TR1, TR2, TR3, ... Both sequences are contiguous
    // (no gaps). A contiguous sequence from 1..N proves the counter was NOT shared
    // with another session (which would cause alternating skips: TR1, TR3, TR5, ...).
    //
    // With static tr_counter (RED): A and B share the global counter. If A and B
    // interleave, A might produce TR1, TR3, TR5... and B: TR2, TR4, TR6... —
    // both sequences have GAPS. The contiguous check catches this.
    //
    // Note: both sessions start at next_test_request_id_=0, so both produce
    // TR1, TR2, TR3... with per-session counters (expected duplicate values
    // across sessions; uniqueness is NOT the isolation invariant — isolation means
    // each session's sequence is contiguous within itself).
    EXPECT_TRUE(check_contiguous(ids_a))
        << "Session A's TestReqID sequence is not contiguous 1..N — "
        << "the counter may be shared with another session (static tr_counter bug). "
        << "IDs: " << ids_a.size() << " total";
    EXPECT_TRUE(check_contiguous(ids_b))
        << "Session B's TestReqID sequence is not contiguous 1..N — "
        << "the counter may be shared with another session (static tr_counter bug). "
        << "IDs: " << ids_b.size() << " total";

    // ── Assertion (b): monotone within each session ────────────────────────
    // IDs within each session must be strictly increasing (1, 2, 3, ...).
    {
        std::uint32_t prev = 0;
        bool monotone = true;
        for (const auto& id : ids_a) {
            std::uint32_t n = parse_tr_id(id);
            if (n == 0 || n <= prev) {
                monotone = false;
                break;
            }
            prev = n;
        }
        EXPECT_TRUE(monotone) << "Session A's TestReqID sequence is not strictly increasing";
    }
    {
        std::uint32_t prev = 0;
        bool monotone = true;
        for (const auto& id : ids_b) {
            std::uint32_t n = parse_tr_id(id);
            if (n == 0 || n <= prev) {
                monotone = false;
                break;
            }
            prev = n;
        }
        EXPECT_TRUE(monotone) << "Session B's TestReqID sequence is not strictly increasing";
    }
}

// ── Test 2: Concurrent sessions on a thread_pool (TSan stress) ──────────────
//
// Runs two sessions concurrently on a thread_pool(4) to maximise the TSan
// window for racing on the `static tr_counter` (RED) or proving per-session
// isolation (GREEN).
//
// RED (before T020): the `static tr_counter` in run_liveness_loop is shared
// across sessions → TSan fires a data race when both sessions' liveness loops
// run concurrently on different pool threads.
//
// GREEN (after T020): ++next_test_request_id_ is per-session on the session
// strand; no shared state → TSan clean.
//
// Clock advances are issued from the test thread, one call at a time, to avoid
// racing on the mock_clock's internal state (a test concern, not the SUT's).
// They are NOT interleaved with the waiting: both clocks are advanced back to
// back and only then is the emission awaited, so the two liveness loops overlap
// on the pool threads. Advancing A, waiting for A, feeding A, then doing the
// same for B would serialise them and delete the very window this test exists to
// open.
//
// #309: this test used to advance both clocks and then `sleep_for(5ms)`, with no
// reply fed back. MEASURED on that shape (linux-clang-asan, 3 runs): each session
// emitted exactly ONE TestRequest and both were `Disconnected` by the analysis —
// the unanswered TestRequest's grace window expires on the second advance and the
// session tears down, so iterations 2..10 drove nothing at all. On that measured
// one-element corpus every assertion still passed — not because an empty-corpus
// guard fired (it never entered: the corpus had exactly one element, so
// `!ids.empty()` was true), but because every predicate the test carried is
// TRIVIAL at n = 1: contiguity and monotonicity over a single element hold for
// any parseable `TR1`. This file used to carry a second, latent vacuity path
// this run never took: the `!empty()` wrapper guards (removed) and
// `check_contiguous` itself returning `true` for an empty sequence (now
// rejected outright, see the guard above) — the same corpus-collapse vacuity
// class as #283/#286, just not the one this particular run exercised.
// Note also: with a shared counter the second session to emit would have produced
// `TR2`, and `check_contiguous(["TR2"])` returns false — so this run was
// schedule-dependent on which session emitted first, not wholly vacuous, which is
// why iteration 0 is now serialized below. It is now fixed the way
// `CrossSessionDisjoint` was: echo each TestReqID back as a Heartbeat so the
// session survives, wait on the EMISSION rather than on a fixed window, and pin
// the count with an equality.
//
// #284 disposition: the `fut.get()` calls below are NOT the #284 defect. This test
// runs on a `thread_pool`, whose own threads service the work, so a get() here
// waits on a context that is still being serviced. #284 is specific to the
// single-threaded io_context in test 1, where the test thread was both the only
// pump and the blocked waiter. Stated as the condition rather than as a count —
// the count this sentence used to carry ("the six") was already false by the time
// #309 added two more.
TEST(CrossSessionTestReqID, ConcurrentSessionsTSanStress) {
    // Arena for the inbound Heartbeat buffers (#309). `on_inbound_frame` takes its
    // span BY VALUE into the coroutine frame, so an iteration-scoped buffer would
    // die at the end of its iteration while a frame the pool has not finished with
    // still held a span over it. `deque::emplace_back` never invalidates references
    // to existing elements, so a span over an earlier element stays valid for the
    // arena's whole lifetime.
    //
    // Declared BEFORE `pool`, therefore destroyed AFTER it — including on the early
    // `return` an ASSERT_* performs, where `~thread_pool` may still be destroying
    // frames that borrow from it.
    std::deque<std::vector<std::byte>> frames;

    asio::thread_pool pool{4};

    using sc = std::chrono::system_clock;
    auto utc_2024 = sc::time_point{} + std::chrono::seconds{1704067200};

    // Each session gets its own mock_clock bound to the pool executor.
    auto clock_a = std::make_shared<fixpp::core::mock_clock>(
        utc_2024, fixpp::core::steady_time_point{}, pool.get_executor());
    auto clock_b = std::make_shared<fixpp::core::mock_clock>(
        utc_2024, fixpp::core::steady_time_point{}, pool.get_executor());

    SessionFixture sA{pool.get_executor(), clock_a, "SENDER_A", "TARGET_A"};
    SessionFixture sB{pool.get_executor(), clock_b, "SENDER_B", "TARGET_B"};

    // #309: `pool` is declared ABOVE the fixtures, so unwinding destroys sB and sA
    // while the pool's four threads are still RUNNING. On the happy path the
    // explicit join below closes that window, but every ASSERT_* in this test
    // performs an early `return` — including the wait-budget miss that IS the
    // acceptance mutant's path — and on those the sessions die under a live pool
    // with their liveness coroutines suspended and holding `this`.
    //
    // This is NOT the #303 shape and a clean ASan run would not refute it: there,
    // nothing resumed the surviving frames, so the dead `Session*` was never
    // dereferenced. Here the pool threads are still executing, so a resume CAN
    // happen concurrently with the destructor.
    //
    // Declared AFTER the fixtures, therefore it runs BEFORE them on every exit
    // path. `thread_pool::join()` is safe to call twice (`thread_group::join`
    // unlinks as it joins), so the explicit join on the happy path stands.
    struct stop_pool_on_exit {
        asio::thread_pool& pool;
        ~stop_pool_on_exit() {
            pool.stop();
            pool.join();
        }
    } stop_pool{pool};

    // Open both sessions.
    {
        auto fa = asio::co_spawn(pool, sA.session->open(), asio::use_future);
        auto fb = asio::co_spawn(pool, sB.session->open(), asio::use_future);
        ASSERT_TRUE(fa.get().has_value()) << "Session A open failed";
        ASSERT_TRUE(fb.get().has_value()) << "Session B open failed";
    }

    // ── Spawn on the session's UNDERLYING strand, not on the wrapper (#309) ───
    //
    // `Session::executor()` returns a `session_executor`, which WRAPS the resolved
    // inner strand. `co_spawn(session_executor, …)` is supported (session_executor.hpp
    // "seam 21"), but it erases the WRAPPER into a fresh ref-counted
    // `shared_target_executor<session_executor>` per call site; the call-site temporary
    // is then released on the TEST thread while the spawned coroutine's copy deletes
    // the target on a POOL thread, and TSan reports that, deterministically.
    //
    // `.underlying()` is the established answer here, not a discovery of this commit.
    // `tests/capi/error_live_test.cpp` names this race by its asio frame
    // (`any_executor.hpp:475`) and takes `.underlying()` for it; so does
    // `tests/capi/send_recv_test.cpp`, in a poll loop. Current in-repository
    // production callers (`src/session/engine.cpp`, `src/capi/session.cpp`) also
    // take `.underlying()`. The condition that justifies this is merge-order
    // independent, not a count of today's callers: spawn on the resolved
    // underlying strand so session operations stay serialized without
    // type-erasing the wrapper at each call site. (asio's refcount does read as
    // correct — `fetch_sub(release)` then `atomic_thread_fence(acquire)`,
    // atomic_count.hpp — so a fence TSan cannot see is the likely reason it fires
    // at all. That half is an inference and nothing here rests on it.)
    //
    // A repo-wide caller census and a specific TSan-report count were measured
    // for this decision at one commit; both rot the moment a new caller lands or
    // the toolchain changes, so neither lives here — see the PR/gate record for
    // the identified measurement.
    //
    // Bound as references to a Session-owned member: no second owner, so no
    // cross-thread release of one. Valid only after a successful `open()`, which is why
    // this sits here and not with the fixtures.
    const asio::any_io_executor& ex_a = sA.session->executor().underlying();
    const asio::any_io_executor& ex_b = sB.session->executor().underlying();

    // Drive to Active. Spawned on each session's OWN executor — `Session::executor()`
    // is the resolved per-session strand, valid once open() has succeeded.
    //
    // #309: this used to spawn on the bare `pool`, and that quietly falsified the
    // premise of the whole test. `on_inbound_frame` is a session-strand-only
    // surface (session.hpp's reentrancy contract), and the Active transition
    // co_spawns `run_liveness_loop` on `co_await this_coro::executor`
    // (src/session/session.cpp) — i.e. on whatever executor the caller used. Fed
    // from the bare pool, BOTH liveness loops ran unstranded, so the "each session
    // on its own strand" the doc block above claims was never true, and any race
    // TSan saw here could have been the test's own contract violation rather than
    // the shared-counter defect under test.
    //
    // The concurrency this test wants survives: two DISTINCT strands still run in
    // parallel on the pool's four threads. What it loses is self-inflicted
    // intra-session overlap, which was never the thing being measured.
    {
        auto logon_a = make_logon_frame("FIX.4.2", 1, "TARGET_A", "SENDER_A", 1);
        auto fa = asio::co_spawn(ex_a,
                                 sA.session->on_inbound_frame(
                                     std::span<const std::byte>{logon_a.data(), logon_a.size()}),
                                 asio::use_future);
        ASSERT_TRUE(fa.get().has_value()) << "Session A rejected its Logon-ack";
    }
    {
        auto logon_b = make_logon_frame("FIX.4.2", 1, "TARGET_B", "SENDER_B", 1);
        auto fb = asio::co_spawn(ex_b,
                                 sB.session->on_inbound_frame(
                                     std::span<const std::byte>{logon_b.data(), logon_b.size()}),
                                 asio::use_future);
        ASSERT_TRUE(fb.get().has_value()) << "Session B rejected its Logon-ack";
    }

    // Pre-existing, and deliberately not multiplied. `state()` is a strand-owned
    // single-writer surface, so reading it from the test thread is the same contract
    // violation the frame feeds above just stopped committing — see the loop below,
    // which declines to add more of these for exactly that reason. These two are kept
    // because they are the establishment pin the whole test rests on and they read a
    // state that is quiescent at this instant: each Logon disposition has already been
    // awaited, and the liveness loop cannot leave Active until a clock advance, which
    // has not happened yet. Inside the loop that argument does not hold.
    ASSERT_EQ(sA.session->state(), fixpp::session::fsm_state::Active);
    ASSERT_EQ(sB.session->state(), fixpp::session::fsm_state::Active);

    // HeartBtInt=1s. Each iteration advances both clocks past the liveness window
    // so both sessions emit this iteration's TestRequest, waits for both emissions
    // through two per-session `await_test_req_ids` calls sharing ONE absolute
    // deadline (see `await_test_req_ids` for why the SHARED deadline, not the
    // single predicate, is what carries the bound), then echoes each session's
    // newest TestReqID back as a
    // Heartbeat. The reply is what clears the pending TestRequest; without it the
    // grace window expires on the next advance and the session disconnects.
    //
    // The TSan stress is that both liveness loops run concurrently on the pool's
    // four threads, on their own per-session strands, incrementing what used to be
    // a `static tr_counter`.
    std::uint32_t hb_seq_a = 2;  // peer (TARGET_A) inbound seqnum (Logon was 1)
    std::uint32_t hb_seq_b = 2;
    // Total corpus per session, and the value the final equality pins. The serialized
    // prologue below contributes 1 and the concurrent loop contributes kIterations - 1;
    // stated because the loop starts at 1, and a reader who "fixes" that to 0 breaks
    // the equality rather than the loop.
    //
    // #317: this was 10 (a 9-iteration window) against SC-003's quantified 10^4 per
    // session. The shortfall was waived at Gate B on PR #314 with a named structural
    // prerequisite, and TWO independent ceilings had to come down before the sample
    // could be raised at all — see the SendingTime commit and `await_test_req_ids`.
    //
    // MEASURED, not projected, on linux-clang-tsan (ctest TIMEOUT is 120 s):
    //     N=1200  0.86 s   N=2400  1.57 s   N=5000  3.52 s   N=10000  6.70 s
    // i.e. linear, with ~18x headroom to the timeout at the shipped value. The same
    // sweep before this work: N=1200 43.6 s, N=2400 163.5 s (already over timeout),
    // and 10^4 extrapolates to ~47 min.
    //
    // Raise it further only WITH a fresh sweep. The per-iteration clock advance is
    // NOT freely raisable as a shortcut: at 3000 ms one jump clears both the
    // liveness and grace windows and the session disconnects at iteration 2.
    const int kIterations = 10000;

    // Feed one Heartbeat carrying `tr_id` into `sx`, on THAT SESSION's strand, and
    // return the pending disposition so the caller can spawn both before awaiting
    // either. Returns rather than asserts because an ASSERT_* inside a lambda
    // returns from the LAMBDA, not from the test — the failure would be recorded
    // and then walked straight past.
    // #317: `clk` is THIS session's clock, not either one that happens to be in
    // scope. The two clocks advance together here, but a peer stamps the time its
    // OWN counterparty reads, and pinning that at the parameter keeps it true if
    // they ever diverge.
    auto feed_heartbeat = [&](SessionFixture& sx, const asio::any_io_executor& ex,
                              fixpp::core::mock_clock& clk, std::uint32_t& hb_seq,
                              std::string_view sender, std::string_view target,
                              std::string_view tr_id) {
        auto& hb = frames.emplace_back(
            make_heartbeat("FIX.4.2", hb_seq++, sender, target, tr_id, fix_sending_time(clk.now())));
        return asio::co_spawn(
            ex, sx.session->on_inbound_frame(std::span<const std::byte>{hb.data(), hb.size()}),
            asio::use_future);
    };

    // ── Iteration 0, SERIALIZED: the discriminator for a shared counter ────────
    //
    // Under full concurrency the corpus alone cannot separate the per-session
    // counter from the historical shared `static tr_counter`. Two racing
    // read-modify-writes on one word can LOSE an update and hand both sessions a
    // clean 1..N, which satisfies every check below. TSan is expected to catch the
    // race itself, but the plain and ASan legs would be green — so on three of the
    // four legs the corpus oracle would be carrying no weight of its own.
    //
    // So session A's first emission is driven to completion AND acknowledged
    // before session B's clock is touched at all. There is no concurrency in this
    // phase, therefore no lost update is available: a shared counter MUST hand B a
    // TR2. That is asserted directly below, by name, on every leg.
    //
    // The concurrent phase after this is unchanged and is still what opens the
    // TSan window; this phase only removes the oracle's dependence on a lucky
    // schedule.
    clock_a->advance(std::chrono::milliseconds{1500});
    // Its own deadline, deliberately NOT shared with session B's wait below. These
    // two are sequential but UNRELATED — B's clock is not advanced until after A's
    // emission is observed — so there is no single event for one deadline to bound.
    // The loop's paired wait is the opposite case and does share one; see it.
    ASSERT_TRUE(
        sA.transport.await_test_req_ids(1, std::chrono::steady_clock::now() + kWaitBudget))
        << kWaitBudgetMiss << "waiting for session A's first TestRequest";
    ASSERT_EQ(sA.transport.collect_test_req_ids().size(), 1u)
        << "session A emitted more than one TestRequest before its first Heartbeat";
    ASSERT_TRUE(sB.transport.collect_test_req_ids().empty())
        << "session B emitted a TestRequest before its clock was ever advanced; the "
           "serialized discriminator below is only meaningful while B has not counted";
    {
        const std::string latest_a = sA.transport.collect_test_req_ids().back();
        auto f = feed_heartbeat(sA, ex_a, *clock_a, hb_seq_a, "TARGET_A", "SENDER_A", latest_a);
        ASSERT_TRUE(f.get().has_value()) << "session A rejected its first Heartbeat";
    }

    clock_b->advance(std::chrono::milliseconds{1500});
    ASSERT_TRUE(
        sB.transport.await_test_req_ids(1, std::chrono::steady_clock::now() + kWaitBudget))
        << kWaitBudgetMiss << "waiting for session B's first TestRequest";
    ASSERT_EQ(sB.transport.collect_test_req_ids().size(), 1u)
        << "session B emitted more than one TestRequest before its first Heartbeat";
    {
        const std::string latest_b = sB.transport.collect_test_req_ids().back();
        auto f = feed_heartbeat(sB, ex_b, *clock_b, hb_seq_b, "TARGET_B", "SENDER_B", latest_b);
        ASSERT_TRUE(f.get().has_value()) << "session B rejected its first Heartbeat";
    }

    // The whole point of the serialization above. A is guaranteed to have counted
    // exactly once before B counted at all, so a per-session counter gives B TR1
    // and a process-global one gives B TR2.
    ASSERT_EQ(sA.transport.collect_test_req_ids().front(), "TR1")
        << "session A's first TestReqID is not TR1 — the per-session counter does not "
           "start at 1";
    ASSERT_EQ(sB.transport.collect_test_req_ids().front(), "TR1")
        << "session B's first TestReqID is not TR1, and session A had already emitted "
           "exactly one — the TestReqID counter is SHARED across sessions (FR-010)";

    // ── Iterations 1..N-1, CONCURRENT: the TSan window ────────────────────────
    for (int i = 1; i < kIterations; ++i) {
        clock_a->advance(std::chrono::milliseconds{1500});
        clock_b->advance(std::chrono::milliseconds{1500});

        const auto want = static_cast<std::size_t>(i + 1);
        // #317: two sequential blocking waits, not one polling predicate over both.
        // ONE deadline spans both, so the pair carries the same 10 s bound the single
        // predicate did — see `await_test_req_ids`.
        const auto deadline = std::chrono::steady_clock::now() + kWaitBudget;
        ASSERT_TRUE(sA.transport.await_test_req_ids(want, deadline))
            << kWaitBudgetMiss << "waiting for session A's TestRequest at iteration " << i;
        ASSERT_TRUE(sB.transport.await_test_req_ids(want, deadline))
            << kWaitBudgetMiss << "waiting for session B's TestRequest at iteration " << i;

        // #309 Gate B F2: `>= want` alone tolerates a batched emission cadence
        // (e.g. +2 this iteration, +0 the next) while the cumulative equality
        // after the loop only reports a confusing final count. Safe to assert as
        // an equality here: after the wait returns, each emitter is parked on its
        // grace sleep (session.cpp:4924-4926) and the only clock advancer is this
        // blocked test thread, so the size is stable at exactly `want`.
        ASSERT_EQ(sA.transport.test_req_id_count(), want)
            << "session A emitted more than one TestRequest at iteration " << i;
        ASSERT_EQ(sB.transport.test_req_id_count(), want)
            << "session B emitted more than one TestRequest at iteration " << i;

        // `.back()` is unconditional by construction: the wait above returned
        // true, so each session has at least `want` >= 2 IDs. A `!empty()` guard
        // here would be the #309 defect back in its original spelling.
        const std::string latest_a = sA.transport.latest_test_req_id();
        const std::string latest_b = sB.transport.latest_test_req_id();

        // Both spawned before either is awaited, so the two `on_inbound_frame`
        // coroutines run concurrently on their two distinct strands.
        auto fa = feed_heartbeat(sA, ex_a, *clock_a, hb_seq_a, "TARGET_A", "SENDER_A", latest_a);
        auto fb = feed_heartbeat(sB, ex_b, *clock_b, hb_seq_b, "TARGET_B", "SENDER_B", latest_b);

        // The disposition is asserted, not discarded. A Heartbeat rejected for a
        // seqnum gap, a checksum, or a dictionary miss used to surface only as the
        // NEXT iteration's 10-second wait-budget miss, which names the wrong thing.
        //
        // Deliberately NOT paired with an `ASSERT_EQ(session->state(), Active)`
        // here: `state()` is a session-strand surface with a single writer on that
        // strand, and the liveness loop is live on it throughout this loop, so
        // reading it from the test thread would be the same contract violation this
        // commit just removed from the frame feeds. A session that stops responding
        // is caught by the exact-count equality after the loop.
        ASSERT_TRUE(fa.get().has_value()) << "session A rejected its Heartbeat at iteration " << i;
        ASSERT_TRUE(fb.get().has_value()) << "session B rejected its Heartbeat at iteration " << i;
    }

    // Close both sessions cleanly, each on its own strand (#309 — same reason as
    // the frame feeds above; `close()` cancels the liveness loop through the root
    // cancellation slot, which is session state).
    {
        auto fa = asio::co_spawn(ex_a, sA.session->close(fixpp::session::close_mode::terminal),
                                 asio::use_future);
        fa.get();
    }
    {
        auto fb = asio::co_spawn(ex_b, sB.session->close(fixpp::session::close_mode::terminal),
                                 asio::use_future);
        fb.get();
    }

    pool.join();

    // Collect results and verify per-session contiguity.
    auto ids_a = sA.transport.collect_test_req_ids();
    auto ids_b = sB.transport.collect_test_req_ids();

    // #309: pin the corpus size FIRST — see the block above this TEST for how the old
    // form read a collapsed corpus as a pass. An EQUALITY is only assertable because
    // the loop above waits on the emission rather than on a fixed window; a tolerance
    // band wide enough to survive that window would admit the very collapse it claims
    // to detect (`CrossSessionDisjoint` records the same reasoning).
    ASSERT_EQ(ids_a.size(), static_cast<std::size_t>(kIterations))
        << "Session A emitted " << ids_a.size() << " TestRequests, expected exactly " << kIterations
        << " — check clock/liveness wiring";
    ASSERT_EQ(ids_b.size(), static_cast<std::size_t>(kIterations))
        << "Session B emitted " << ids_b.size() << " TestRequests, expected exactly " << kIterations
        << " — check clock/liveness wiring";

    EXPECT_TRUE(check_contiguous(ids_a))
        << "Session A TestReqID sequence has gaps (shared static counter?)";
    EXPECT_TRUE(check_contiguous(ids_b))
        << "Session B TestReqID sequence has gaps (shared static counter?)";

    // No separate monotonicity check here. `check_contiguous` requires the corpus to
    // be positionally EQUAL to 1..N, which is strictly increasing by construction, so
    // a monotonicity predicate over the same vector can only fail where contiguity has
    // already failed. `CrossSessionDisjoint` above still carries its own inline
    // assertion-(b) loops, which are dead for the same reason; they are pre-existing
    // and left alone rather than deleted here — flagged, not folded in.
}

// ══════════════════════════════════════════════════════════════════════════════
// (#303) Teardown witnesses for `quiesce_or_release_on_exit`
//
// WHY THESE ARE BEHAVIOURAL AND NOT A SANITIZER SWEEP. The fault oracle for this
// issue was run before the fix and came back a REAL zero: with the unfixed order
// and a forced residual, no ASan diagnostic appears at any frame depth — while
// three positive controls in the SAME binary went red, including an
// `any_io_executor` holding a strand and outliving its `io_context`
// (`heap-use-after-free` at `asio/detail/impl/strand_executor_service.ipp:88`).
//
// The reason is mechanical, and it inverts the intuitive test: after
// `~io_context` destroys the surviving frames, NOTHING RESUMES THEM. Destroying a
// suspended frame runs the destructors of its in-scope locals; it does not
// re-enter the body, so the dead `Session*` is never dereferenced. The only drain
// that outlives the fixtures is inside `~io_context`'s own shutdown, and that
// destroys rather than resumes.
//
// ⚠️ So a clean ASan run over `CrossSessionDisjoint` is NOT evidence this hazard
// is absent — it is evidence the instrument has nothing to observe. The fix is
// by-construction hardening, exactly as #302 recorded #293's arena half, and the
// acceptance instrument has to be the BEHAVIOUR: on a residual teardown the
// fixtures were released, and on a quiesced teardown they were destroyed.
//
// Both directions are pinned, because either alone is satisfiable by a guard that
// answers unconditionally, and BOTH assert the destruction count directly rather
// than only the clock proxy — see `SessionFixture::destructions`.
// ══════════════════════════════════════════════════════════════════════════════

namespace {

// Shared construction for the witnesses. utc_2024 matches the tests above.
inline std::shared_ptr<fixpp::core::mock_clock> make_witness_clock(asio::io_context& ioc) {
    using sc = std::chrono::system_clock;
    return std::make_shared<fixpp::core::mock_clock>(
        sc::time_point{} + std::chrono::seconds{1704067200}, fixpp::core::steady_time_point{},
        ioc.get_executor());
}

}  // namespace

// ── Direction 1: RESIDUAL ⇒ the fixture is RELEASED, and it is reported ───────
//
// The fixture MUST be scoped INSIDE the macro's statement. Declaring it before
// EXPECT_NONFATAL_FAILURE would run the guard after the macro stopped intercepting
// failures, and this test would pass while asserting nothing.
// (gate-b/r1 F6) Two fixtures, not one: `fixtures` is a `std::vector`, and a
// single-element vector cannot distinguish a correct `for (auto* owner :
// fixtures)` loop from one that only ever touches `fixtures[0]` (an
// off-by-one, or a `break` after the first iteration). `sB` is otherwise
// idle — no session opened, nothing spawned on it — because the ordering
// property under test is "every owner in the vector is released", not
// anything about sB's own residual state.
TEST(CrossSessionTeardown, ResidualPathReleasesTheFixtures) {
    std::atomic<int> destructions{0};
    std::atomic<int> destructions_b{0};
    std::weak_ptr<fixpp::core::Clock> weak_clock;
    std::atomic<int> ioc_destructions{0};

    EXPECT_NONFATAL_FAILURE(
        ([&destructions, &destructions_b, &weak_clock, &ioc_destructions] {
            // Arena for the inbound frame, declared BEFORE `ioc` for the same reason
            // the real test's is — and this witness is where that reason was
            // MEASURED rather than reasoned about. An earlier revision declared the
            // buffer as a plain local after the guard; the guard's own `poll_one()`
            // then RESUMED the suspended `on_inbound_frame` frame over the
            // already-destroyed buffer and ASan reported heap-use-after-free in
            // `scan_frame_header` with `quiesce_or_release_on_exit::~...` four frames
            // down the stack.
            //
            // The lesson generalises and is easy to get backwards: what faults is not
            // a frame being DESTROYED — that runs trivial destructors and is usually
            // silent — it is a frame being RESUMED after its borrowed storage died.
            // Every drain in this guard (`run_for`, and the `poll_one()` probe) is
            // such a resumption, so any storage a suspended frame borrows must
            // outlive the guard.
            std::deque<std::vector<std::byte>> frames;

            // Heap-owned so ~quiesce_or_release_on_exit can RELEASE it on the residual
            // path (see its `ioc` member for why that is mandatory on Windows). `ioc`
            // aliases the same object, so every use below is unchanged.
            auto ioc_owner = std::make_unique<counting_io_context>();
            asio::io_context& ioc = *ioc_owner;
            auto clock = make_witness_clock(ioc);
            weak_clock = clock;

            auto sA =
                std::make_unique<SessionFixture>(ioc.get_executor(), clock, "SENDER_A", "TARGET_A");
            sA->destructions = &destructions;

            auto sB =
                std::make_unique<SessionFixture>(ioc.get_executor(), clock, "SENDER_B", "TARGET_B");
            sB->destructions = &destructions_b;

            // 0 ms budget, so the guard cannot drain. Its `poll_one()` probe is what
            // makes this a REAL residual rather than the deadline artefact: with the
            // probe in place, an empty context at a zero budget reports QUIESCED (the
            // third witness below pins exactly that), so reaching the residual branch
            // here means work genuinely remained.
            ioc_owner->destructions = &ioc_destructions;
            quiesce_or_release_on_exit guard{
                ioc_owner, *clock, {&sA, &sB}, std::chrono::milliseconds{0}};

            // Real outstanding work: open the session (so its liveness loop is live
            // and re-arms), then spawn an inbound frame that is never pumped.
            auto fut_open = asio::co_spawn(ioc, sA->session->open(), asio::use_future);
            ASSERT_TRUE(pump_until_ready(ioc, fut_open))
                << kPumpBudgetMiss << "opening the witness session";
            ASSERT_TRUE(fut_open.get().has_value());

            auto& logon =
                frames.emplace_back(make_logon_frame("FIX.4.2", 1, "TARGET_A", "SENDER_A", 1));
            auto fut = asio::co_spawn(ioc,
                                      sA->session->on_inbound_frame(
                                          std::span<const std::byte>{logon.data(), logon.size()}),
                                      asio::use_future);
            (void)fut;  // deliberately never pumped

            // Drop the test's own strong reference, so after the scope the ONLY thing
            // that can still hold the clock alive is a RELEASED fixture. Sampled here
            // rather than before construction: the fixture copies the config, so an
            // earlier sample would be inflated by copies that are about to die anyway.
            //
            // `guard.clock` is a reference to *clock and is used during the guard's
            // destructor, which runs while sA is still alive and still owns a
            // shared_ptr copy — so this reset cannot leave that reference dangling.
            clock.reset();
        }()),
        "was not observed to run out of work");

    // THE DIRECT OBSERVATION. A mutant that deletes the release() while keeping the
    // message above passes the SPI matcher and fails here.
    EXPECT_EQ(destructions.load(std::memory_order_relaxed), 0)
        << "the SessionFixture was DESTROYED on the residual teardown path, while residual "
           "async frames may still reference its graph, which is the ordering #303 exists "
           "to prevent.";

    // The n>1 case: a loop that stops after the first owner (an off-by-one, or a
    // stray `break`) would release sA and leave sB destroyed — this is the only
    // assertion that would catch that, since a single-fixture test cannot.
    EXPECT_EQ(destructions_b.load(std::memory_order_relaxed), 0)
        << "the SECOND SessionFixture in `fixtures` was destroyed on the residual "
           "path -- the release loop must release every owner, not only the first.";

    // An independent check on a different observable: the released fixture's graph is
    // retained. This proves RETENTION, not fixture identity — a compound mutant that
    // drops release() while copying the clock elsewhere would keep it live. The
    // destruction count above is what covers that; the two are kept because they fail
    // for different reasons.
    EXPECT_FALSE(weak_clock.expired())
        << "the fixture-owned clock did not survive the residual path, so the fixture's "
           "EngineConfig shared_ptr copy died with it.";

    // The io_context half of the same decision, and the only portable way to see
    // it. On Windows the failure mode is not a wrong value anywhere -- it is
    // `~io_context` never returning (`win_iocp_io_context::shutdown` loops
    // `while (outstanding_work_ > 0)` and this branch exists precisely because
    // that count is non-zero), so the symptom there is a ctest timeout with no
    // diagnostic. This assertion turns it into a named failure, on every
    // platform, including the one where the bug cannot manifest at all.
    EXPECT_EQ(ioc_destructions.load(std::memory_order_relaxed), 0)
        << "~io_context RAN on the residual path, so the io_context was destroyed rather "
           "than released. Releasing the fixtures without releasing it can strand the "
           "outstanding-work count -- POSIX ignores such a count at shutdown, Windows spins "
           "on it forever, and all three MSVC legs of PR #304 timed out at 120 s in this "
           "exact test before the io_context was released too. Counting the destruction "
           "rather than checking the owner for null is deliberate: `release()` followed by "
           "`delete` leaves the owner null and still destroys the context.";
}

// ── (gate-b/r1 F6) The fail-safe `catch(...)`: a throwing pump still releases ─
//
// `~quiesce_or_release_on_exit`'s inner `catch (...) { quiesced = false; }`
// exists so an exception mid-pump is treated as residual rather than as
// quiesced — the fail-SAFE direction, since it means "release, don't destroy
// fixtures whose frames' fate is unknown". Nothing exercised it: the two
// witnesses above never make the pump throw.
//
// A zero budget means `run_for(0)` returns without dispatching anything (the
// #305 deadline artefact), so the throw is engineered to come from the
// guard's OWN `poll_one()` call instead — which does dispatch one ready
// handler, and does so from inside the same inner `try`.
TEST(CrossSessionTeardown, ThrowingPumpStillReleasesTheFixtures) {
    std::atomic<int> destructions{0};
    // (gate-b/r1 A2) ResidualPathReleasesTheFixtures above pins this with TWO
    // fixtures; a mutant gated on `fixtures.size() == 2` survives it and would
    // still destroy the io_context on this one-fixture throwing-pump path.
    std::atomic<int> ioc_destructions{0};

    EXPECT_NONFATAL_FAILURE(
        ([&destructions, &ioc_destructions] {
            // Heap-owned so ~quiesce_or_release_on_exit can RELEASE it on the residual
            // path (see its `ioc` member for why that is mandatory on Windows). `ioc`
            // aliases the same object, so every use below is unchanged.
            auto ioc_owner = std::make_unique<counting_io_context>();
            asio::io_context& ioc = *ioc_owner;
            auto clock = make_witness_clock(ioc);

            auto sA =
                std::make_unique<SessionFixture>(ioc.get_executor(), clock, "SENDER_A", "TARGET_A");
            sA->destructions = &destructions;

            asio::post(ioc, [] { throw std::runtime_error("gate-b/r1 F6: injected pump fault"); });

            ioc_owner->destructions = &ioc_destructions;
            quiesce_or_release_on_exit guard{
                ioc_owner, *clock, {&sA}, std::chrono::milliseconds{0}};
        }()),
        "was not observed to run out of work");

    // THE DIRECT OBSERVATION. A mutant that turns the inner catch's
    // `quiesced = false` into `quiesced = true` would destroy the fixture here
    // instead of releasing it, and this assertion catches that.
    EXPECT_EQ(destructions.load(std::memory_order_relaxed), 0)
        << "the SessionFixture was DESTROYED after a throwing pump handler. The inner "
           "catch(...) must treat a mid-pump exception as residual (quiesced=false) and "
           "release, not destroy, since the exception leaves the outstanding work in an "
           "unknown state.";
    EXPECT_EQ(ioc_destructions.load(std::memory_order_relaxed), 0)
        << "~io_context RAN on the throwing-pump residual path. See "
           "ResidualPathReleasesTheFixtures for why this must be 0 rather than destroyed "
           "(#311).";
}

// ── (gate-b/r2 finding 4) the OUTER `catch(...)` is reachable and swallows a ──
//    throwing ADD_FAILURE, after the release has already run ────────────────
//
// `~quiesce_or_release_on_exit`'s outer `catch (...) { }` exists because
// `ADD_FAILURE()` can throw under `--gtest_throw_on_failure`, and "nothing may
// escape a destructor" is unconditional: this destructor is implicitly
// `noexcept`, so letting the throw through is `std::terminate`. No CI lane sets
// `--gtest_throw_on_failure`, so that branch never ran and its own comment's
// claim -- "the release has already happened above it, and ADD_FAILURE has
// already recorded the failure before throwing" -- was untested.
//
// RAII-scoped `GTEST_FLAG_SET(throw_on_failure, true)`, not a subprocess: the
// residual path's own `ADD_FAILURE()` is enough to drive the throw in-process,
// and driving it is exactly what exercises the catch this test pins. Restoring
// the previous value is mandatory -- left set, every later `EXPECT_*` in this
// binary would throw instead of merely failing.
struct throw_on_failure_scope {
    bool previous = GTEST_FLAG_GET(throw_on_failure);
    throw_on_failure_scope() { GTEST_FLAG_SET(throw_on_failure, true); }
    ~throw_on_failure_scope() { GTEST_FLAG_SET(throw_on_failure, previous); }
};

TEST(CrossSessionTeardown, OuterCatchSwallowsAThrowingAddFailure) {
    std::atomic<int> destructions{0};
    std::atomic<int> destructions_b{0};
    // (gate-b/r1 A3) The last residual witness with no io_context assertion --
    // see ResidualPathReleasesTheFixtures and ThrowingPumpStillReleasesTheFixtures.
    std::atomic<int> ioc_destructions{0};

    EXPECT_NONFATAL_FAILURE(
        ([&destructions, &destructions_b, &ioc_destructions] {
            // Setup runs with throw_on_failure at its ordinary (false) value, so
            // a bounded-pump budget miss here (#284/#289 -- expected under CI
            // load) is a normal ASSERT_TRUE failure, not std::terminate. The
            // reason is gtest's, not `guard`'s: gtest's own
            // HandleExceptionsInMethodIfSupported deliberately RETHROWS a
            // GoogleTestFailureException once GTEST_FLAG(throw_on_failure) is
            // set, so any gtest failure inside `throw_on_failure_scope`'s
            // window throws, and aborts unless something in scope catches it --
            // which is exactly what `~quiesce_or_release_on_exit`'s blanket
            // `catch (...)` does, and what this test pins (this is NOT the "two
            // exceptions unwinding at once" rule: [except.terminate] fires only
            // when a function invoked during unwinding EXITS via an exception,
            // and the blanket catch here means it never does). Constructing
            // `throw_scope` and `guard` only after every setup ASSERT_TRUE has
            // already succeeded keeps the setup failure path entirely outside
            // the flag's window, which is the actual reason it stays a named
            // failure instead of a terminate.
            //
            // Same residual shape as ResidualPathReleasesTheFixtures (two
            // fixtures, 0 ms budget, real outstanding work) -- reused here rather
            // than simplified, so this witness forces the guard down the exact
            // path whose ADD_FAILURE is under test.
            std::deque<std::vector<std::byte>> frames;
            // Heap-owned so ~quiesce_or_release_on_exit can RELEASE it on the residual
            // path (see its `ioc` member for why that is mandatory on Windows). `ioc`
            // aliases the same object, so every use below is unchanged.
            auto ioc_owner = std::make_unique<counting_io_context>();
            asio::io_context& ioc = *ioc_owner;
            auto clock = make_witness_clock(ioc);

            auto sA =
                std::make_unique<SessionFixture>(ioc.get_executor(), clock, "SENDER_A", "TARGET_A");
            sA->destructions = &destructions;
            auto sB =
                std::make_unique<SessionFixture>(ioc.get_executor(), clock, "SENDER_B", "TARGET_B");
            sB->destructions = &destructions_b;

            auto fut_open = asio::co_spawn(ioc, sA->session->open(), asio::use_future);
            ASSERT_TRUE(pump_until_ready(ioc, fut_open))
                << kPumpBudgetMiss << "opening the witness session";
            ASSERT_TRUE(fut_open.get().has_value());

            // throw_scope is declared FIRST so it is destroyed AFTER guard:
            // throw_on_failure is still true when ~quiesce_or_release_on_exit's
            // ADD_FAILURE() runs, and is restored immediately afterward.
            throw_on_failure_scope throw_scope;
            ioc_owner->destructions = &ioc_destructions;
            quiesce_or_release_on_exit guard{
                ioc_owner, *clock, {&sA, &sB}, std::chrono::milliseconds{0}};

            auto& logon =
                frames.emplace_back(make_logon_frame("FIX.4.2", 1, "TARGET_A", "SENDER_A", 1));
            auto fut = asio::co_spawn(ioc,
                                      sA->session->on_inbound_frame(
                                          std::span<const std::byte>{logon.data(), logon.size()}),
                                      asio::use_future);
            (void)fut;  // deliberately never pumped

            // ~guard runs here, while throw_on_failure is still true: it takes
            // the residual branch, releases sA and sB, then ADD_FAILURE() throws
            // GoogleTestFailureException straight into the outer catch(...).
        }()),
        "was not observed to run out of work");

    // (a) NOTHING ESCAPED. If the outer catch did not swallow the throw, it
    // would propagate out of an implicitly-noexcept destructor and the process
    // would already have called std::terminate -- reaching this line at all is
    // the assertion.
    SUCCEED() << "control reached past ~quiesce_or_release_on_exit without "
                 "std::terminate, so the outer catch(...) swallowed the throw";

    // (b) THE RELEASE STILL HAPPENED. The throw must not have preempted the
    // release loop, which runs above the ADD_FAILURE() that throws. A mutant
    // that reorders ADD_FAILURE() before the release loop would destroy the
    // fixtures here instead of releasing them.
    EXPECT_EQ(destructions.load(std::memory_order_relaxed), 0)
        << "the SessionFixture was destroyed even though ~quiesce_or_release_on_exit "
           "throws under --gtest_throw_on_failure -- the release loop must run, and "
           "complete, before ADD_FAILURE() can throw.";
    EXPECT_EQ(destructions_b.load(std::memory_order_relaxed), 0)
        << "the SECOND SessionFixture was destroyed even though "
           "~quiesce_or_release_on_exit throws under --gtest_throw_on_failure.";
    EXPECT_EQ(ioc_destructions.load(std::memory_order_relaxed), 0)
        << "~io_context RAN on the residual path even though the outer catch(...) had "
           "to swallow a throwing ADD_FAILURE(). See ResidualPathReleasesTheFixtures for "
           "why this must be 0 rather than destroyed (#311).";
}

// ── Direction 2: QUIESCED ⇒ the fixture is DESTROYED, and nothing is reported ─
//
// Without this, direction 1 is satisfied by a guard that releases unconditionally —
// which would leak on every run of every test using it — and by a `destructions`
// probe that can never increment. Any non-fatal failure inside this test fails it
// outright, which is exactly the second assertion.
TEST(CrossSessionTeardown, QuiescedPathDestroysTheFixtures) {
    std::atomic<int> destructions{0};
    std::weak_ptr<fixpp::core::Clock> weak_clock;
    std::atomic<int> ioc_destructions{0};

    {
        // Heap-owned so ~quiesce_or_release_on_exit can RELEASE it on the residual
        // path (see its `ioc` member for why that is mandatory on Windows). `ioc`
        // aliases the same object, so every use below is unchanged.
        auto ioc_owner = std::make_unique<counting_io_context>();
        asio::io_context& ioc = *ioc_owner;
        auto clock = make_witness_clock(ioc);
        weak_clock = clock;

        auto sA =
            std::make_unique<SessionFixture>(ioc.get_executor(), clock, "SENDER_A", "TARGET_A");
        sA->destructions = &destructions;

        // Nothing spawned and a real budget, so the guard drains and takes the
        // quiesced branch.
        ioc_owner->destructions = &ioc_destructions;
        quiesce_or_release_on_exit guard{ioc_owner, *clock, {&sA}, std::chrono::seconds{1}};

        clock.reset();
    }

    EXPECT_EQ(destructions.load(std::memory_order_relaxed), 1)
        << "the SessionFixture was NOT destroyed on a quiesced teardown — the guard is "
           "releasing unconditionally, which leaks on every ordinary run and makes the "
           "residual witness vacuous.";
    EXPECT_TRUE(weak_clock.expired())
        << "the fixture-owned clock outlived a quiesced teardown, so the weak_ptr probe "
           "used by the residual witness cannot distinguish release from destruction and "
           "proves nothing there.";

    // Without this, the residual witness's destruction assertion is satisfied by a
    // guard that releases the io_context UNCONDITIONALLY -- which would leak one per
    // guarded scope on every ordinary run, and, worse, would silently stop exercising
    // ~io_context anywhere this guard is used.
    EXPECT_EQ(ioc_destructions.load(std::memory_order_relaxed), 1)
        << "~io_context did NOT run on a QUIESCED teardown, so the io_context was released "
           "there too. The release is the residual branch's decision only; taking it "
           "unconditionally leaks an io_context per guarded scope.";
}

// ── Direction 3: the `poll_one()` probe is load-bearing ──────────────────────
//
// `io_context::run_for` is `run_until`, and `run_one_until` tests `now < abs_time`
// BEFORE entering the scheduler (asio impl/io_context.hpp:108-131). A run whose
// deadline has already passed therefore returns without ever consulting the work
// count, leaving a just-restarted context UNSTOPPED even when it holds no work —
// unconditionally so at a zero budget.
//
// Without the guard's `poll_one()`, this scope would take the residual branch on an
// EMPTY context: it would report a residual that does not exist and leak a fixture
// on a healthy teardown. Deleting that one line turns this test red, and it is the
// only test here that it turns red.
TEST(CrossSessionTeardown, ZeroBudgetOnAnEmptyContextIsNotResidual) {
    // Heap-owned so ~quiesce_or_release_on_exit can RELEASE it on the residual
    // path (see its `ioc` member for why that is mandatory on Windows). `ioc`
    // aliases the same object, so every use below is unchanged.
    auto ioc_owner = std::make_unique<counting_io_context>();
    asio::io_context& ioc = *ioc_owner;
    auto clock = make_witness_clock(ioc);

    // ASSERT the emptiness rather than assume it. "No fixtures and nothing spawned"
    // is not by itself proof the context holds no work — constructing the mock_clock
    // against `ioc.get_executor()` could have posted. It does not (measured), but a
    // test whose whole argument is "the context is empty, so any residual report is
    // the deadline artefact" must not leave that premise unchecked: if the clock ctor
    // ever starts posting, this test would pass for the wrong reason — `poll_one()`
    // draining that one handler — and would stop being a pin on anything.
    ASSERT_EQ(ioc.poll(), 0u) << "the context is not empty at entry, so a residual "
                                 "report below would no longer isolate the deadline artefact";
    ioc.restart();

    // The context provably holds no work, so the ONLY thing that can make the guard
    // report a residual is the deadline artefact its poll_one() probe exists to close.
    quiesce_or_release_on_exit guard{ioc_owner, *clock, {}, std::chrono::milliseconds{0}};
    // ~guard runs here and must add no failure.
}

// ── (gate-b/r2 C1) a positive budget with an EMPTY `fixtures` is still residual ──
//
// This witness reaches the release with a positive budget (1 ms) and an empty
// `fixtures` vector, so a release narrowed to `budget == zero` or to
// `!fixtures.empty()` fails here.
//
// The residual is forced by a stack-scoped `executor_work_guard`, not by timing:
// with outstanding work permanently held, `run_for` returns only because its
// deadline elapsed and `poll_one()` cannot call `stop()` (asio only stops when
// the work count is zero), so `stopped() == false` deterministically — no flake
// surface from the real-clock `run_for` racing a dispatch.
//
// `ioc_destructions` is declared OUTSIDE the lambda and `ioc_owner` INSIDE it
// (the same shape `ThrowingPumpStillReleasesTheFixtures` uses above), not merely
// stylistically: it is what makes the counter observation safe under a mutant
// that skips the release. `ioc_owner`, and anything it may still own, is fully
// torn down at the closing brace of the immediately-invoked lambda, which runs
// to completion before control returns to this scope — so `ioc_destructions`
// outlives `ioc_owner` regardless of whether the release actually ran. Declaring
// `ioc_owner` in this outer scope instead measurably breaks that: under a mutant
// that skips the release, `ioc_owner`'s natural (non-released) destruction would
// then run at THIS function's end, after `ioc_destructions` -- declared later in
// the same scope, so destroyed first -- had already gone out of scope
// (stack-use-after-scope, caught by ASan while preparing this witness).
//
// Declaration order inside the lambda matters too, and is the reason
// `work_guard` sits between `ioc_owner` and the guard: `guard` is the
// innermost-declared local, so ~guard runs first (work count still > 0,
// residual branch taken, `ioc_owner` released via `ioc.release()` — NOT
// destroyed); ~work_guard runs next, decrementing the work count on the
// still-alive, merely-released io_context, which is safe precisely because
// release() did not delete it; ~ioc_owner then runs on an already-released
// unique_ptr, a no-op. No new suppression is needed beyond the existing
// FIXPP_XSESSION_LSAN_IGNORE(leaked_ioc) call the release path already makes.
TEST(CrossSessionTeardown, PositiveBudgetWithNoFixturesIsStillResidual) {
    std::atomic<int> ioc_destructions{0};

    EXPECT_NONFATAL_FAILURE(
        ([&ioc_destructions] {
            auto ioc_owner = std::make_unique<counting_io_context>();
            asio::io_context& ioc = *ioc_owner;
            auto clock = make_witness_clock(ioc);
            ioc_owner->destructions = &ioc_destructions;

            asio::executor_work_guard<asio::io_context::executor_type> work_guard(
                ioc.get_executor());

            quiesce_or_release_on_exit guard{
                ioc_owner, *clock, {}, std::chrono::milliseconds{1}};
        }()),
        "was not observed to run out of work");

    // THE DIRECT OBSERVATION. A mutant narrowed on `budget == zero` or on
    // `!fixtures.empty()` would still destroy (rather than release) the
    // io_context at this call site, and this assertion catches that.
    EXPECT_EQ(ioc_destructions.load(std::memory_order_relaxed), 0)
        << "~io_context RAN on a positive-budget, empty-fixtures residual path. "
           "The release decision must not be narrowed to `budget == zero` or to "
           "`!fixtures.empty()` -- either narrowing silently restores the Windows "
           "wedge (#304) for any call site shaped like this one.";
}

}  // namespace fixpp::session::test
