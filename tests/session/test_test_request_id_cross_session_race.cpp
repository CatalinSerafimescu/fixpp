// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_test_request_id_cross_session_race.cpp
//
// 009-session-fsm-finalize T019 [US4] — Per-session TestReqID counter race-freedom.
//
// Scenarios (spec.md FR-010 / SC-003 / [const §XI.4]):
//
//   1. CrossSessionDisjoint — two concurrent sessions' TestReqID sequences are
//      disjoint (FR-010: per-session counter, not process-global static).
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
// SC-003 quantified threshold: 10^4 TestRequests per session.
// In practice we use a smaller count (100) to keep the test fast; the TSan
// stress comes from the pool threads, not the TestRequest count.
//
// RED phase (before T020): the existing `static tr_counter` in
// run_liveness_loop is shared across all sessions → assertion (a) will fail
// (sequences interleave) AND TSan will fire a data race on the static.
//
// GREEN phase (after T020): ++next_test_request_id_ is per-session on the
// session strand → disjoint, monotone, race-free.
//
// Anchors:
//   spec.md FR-010, SC-003
//   [const §XI.4] per-session strand isolation
//   research.md D-3 (wrap-around at UINT32_MAX acceptable)

#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
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
// What bounds the exposure is WHICH runs can reach it. There are two:
//   - a real `CrossSessionDisjoint` residual, where the guard's ADD_FAILURE has
//     already made the binary RED, so nothing green is hiding anything;
//   - the two `CrossSessionTeardown` witnesses that drive the residual path
//     through `EXPECT_NONFATAL_FAILURE` and therefore leave the binary GREEN:
//     `ResidualPathReleasesTheFixtures` and `ThrowingPumpStillReleasesTheFixtures`.
//     Both ARE a real hole: an unrelated leak reachable through either witness's
//     own fixtures would be suppressed.
//
// The size of that hole is MEASURED, not estimated, and stated structurally so it
// survives the next witness added to this suite rather than going stale: the
// stable invariant is the per-fixture root, not the total. Replacing this macro
// with a no-op and re-running (leak detection on) attributes every leaked byte to
// a `make_unique<Session>` inside a `SessionFixture` (266272 bytes each) reachable
// from one of the two witnesses above -- nothing else in the binary leaks:
//   - `ResidualPathReleasesTheFixtures` releases TWO fixtures (`sA`, `sB`):
//     671075 byte(s) in 61 allocation(s).
//   - `ThrowingPumpStillReleasesTheFixtures` releases ONE fixture:
//     333568 byte(s) in 12 allocation(s).
//   - total (derived, not the claim): 1004643 byte(s) in 73 allocation(s), three
//     `SessionFixture` roots across the two witnesses.
// So the suppressed set in a green run is bounded to fixtures that open no store
// and register no listener under these two named witnesses -- a real cost,
// bounded and named, on the same footing as
// tests/interop/support/interop_fixture.cpp's larger version of it.
//
// Measured on `linux-clang-asan` (clang 22, libstdc++, `-fsanitize=address`) at
// `6163b9d3`. The per-fixture byte count (266272 B) is allocator- and
// stdlib-dependent, so treat a different NUMBER THERE as a re-measurement, not a
// regression. It does NOT license a change in the fixture or witness COUNT above
// without updating this comment -- that split is a structural fact about which
// witnesses reach the residual path, not an artifact of the allocator.
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

// ── Wire-field extractor (local copy avoids TARGET_OBJECTS dependency) ─────────
//
// Extract the value of a FIX tag from a SOH-delimited frame.
// Returns "" if the tag is not present.
static std::string extract_tag(std::span<const std::byte> frame, std::uint32_t tag) {
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    std::string needle = std::to_string(tag) + "=";
    auto pos = wire.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    pos += needle.size();
    auto end = wire.find('\x01', pos);
    if (end == std::string::npos) {
        return {};
    }
    return wire.substr(pos, end - pos);
}

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

// ── CaptureTransport: thread-safe outbound frame capture ────────────────────────
//
// The session's transport_send is called from the session strand; we collect all
// outbound frames into a vector under a std::mutex.  The mutex is ONLY for the
// test's collector (not on the session-internal path), so it doesn't affect TSan
// annotations on the session's own counters.
struct CaptureTransport {
    std::mutex mtx;
    std::vector<std::vector<std::byte>> frames;

    void capture(std::span<const std::byte> frame) {
        std::lock_guard<std::mutex> lock{mtx};
        frames.emplace_back(frame.begin(), frame.end());
    }

    // Collect all 112= (TestReqID) values from frames where 35=1 (TestRequest).
    std::vector<std::string> collect_test_req_ids() {
        std::lock_guard<std::mutex> lock{mtx};
        std::vector<std::string> ids;
        for (const auto& f : frames) {
            auto sp = std::span<const std::byte>{f.data(), f.size()};
            if (extract_tag(sp, 35) == "1") {
                auto id = extract_tag(sp, 112);
                if (!id.empty()) {
                    ids.push_back(std::move(id));
                }
            }
        }
        return ids;
    }
};

// ── TestReqID numeric extractor: "TR<N>" → N ──────────────────────────────────
// Returns 0 if parsing fails.
static std::uint32_t parse_tr_id(std::string_view s) {
    if (s.size() < 3) {
        return 0;
    }
    if (s[0] != 'T' || s[1] != 'R') {
        return 0;
    }
    std::uint32_t v = 0;
    for (std::size_t i = 2; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') {
            return 0;
        }
        v = v * 10u + static_cast<std::uint32_t>(s[i] - '0');
    }
    return v;
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

// ── (#303) Teardown guard: quiesce, or RELEASE the fixtures ───────────────────
//
// Replaces `quiesce_on_exit` at this one site. The shared guard only OBSERVES
// residual work and says so itself; on the budget-exhausted path it returns, the
// fixtures are destroyed, and only afterwards does `~io_context` destroy coroutine
// frames that borrowed them. This guard closes that by making the same observation
// DECIDE something: if the context did not quiesce, the fixtures are deliberately
// released (leaked) so the frames `~io_context` destroys still have live referents.
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
//     A RELEASED FIXTURE IS NEVER DESTROYED, THEREFORE ITS SESSION'S STRAND HANDLE
//     IS NEVER DESTROYED, THEREFORE THE ORDER THAT WOULD FAULT NEVER ARISES.
//
// Stated as an invariant rather than a mechanism because a future cleanup pass that
// "fixes the leak" by destroying these on the way out would reintroduce exactly the
// use-after-free the reorder was refuted for.
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
// THE POPULATION, COUNTED — and counted TWICE, because the first two versions of
// this paragraph were both wrong and each was wrong in the direction that made
// staying file-local look better than the evidence supports.
//
//   v1 said "one site is not a pattern."          Wrong: the shape repeats.
//   v2 said "SEVEN more sites, but each attaches
//            a live Transport, so they have a
//            forcing lever this one lacks."       Wrong twice over, below.
//
// The count is EIGHTEEN, not seven (`grep` for a `quiesce_on_exit` OBJECT
// declaration, excluding the helper's own self-test):
//
//   tests/session/logout_exchange_test.cpp          11 sites
//   tests/session/test_live_outbound_serialized.cpp  7 sites
//
// And the forcing-lever claim is false for most of them. `.transport = ` is set at
// 6 sites, ALL of them in test_live_outbound_serialized.cpp; logout_exchange_test.cpp
// sets it ZERO times. So its 11 sites each declare `Session sess(engine, cfg);`
// followed immediately by `quiesce_on_exit quiesce{ioc, *clock};` with no transport
// attached — which is exactly this file's shape, with exactly this file's absence of
// a way to force quiescence.
//
// Stated plainly, because the understated version is the one that keeps getting
// written: this is NOT a lone site. What was actually measured (below) is the
// state `poll_one()` observes AT THE GUARD — zero outstanding work, at every one
// of the 18, on the path CI takes today. Whether a suspended frame could reach
// that same guard on an ASSERT_*-early-return path is a DIFFERENT question, and
// it is unmeasured except at the one site built to take such a path on purpose
// (`test_live_outbound_serialized.cpp:1174`, `BudgetMissQuiescesBeforeSessionTeardown`).
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
// WHAT THE ZERO ACTUALLY MEASURES, AND WHAT IT DOES NOT. The 18 sites were
// instrumented and run: at every one, `poll_one()` reports the `io_context` holds
// ZERO outstanding work by the time the guard's destructor finishes running. But
// that reading is taken INSIDE the guard, AFTER `transport->close()` and
// `clock.cancel_sleeps()` have already run — so at the six lever-bearing sites
// (`test_live_outbound_serialized.cpp`, `.transport = ` set) it records "the lever
// drained the suspended frame", not "no suspended frame existed". Measured
// directly: setting `BudgetMissQuiescesBeforeSessionTeardown`'s guard budget to
// `0ms` reads zero WITH its forcing lever in place, and with the lever removed
// ("`teardown_guard.transport = raw_ptr;`" commented out) prints "terminate called
// without an active exception" instead. So the earlier claim that no suspended
// frames are left behind anywhere is false at that site — deleted rather than
// kept, because the guard's own zero is exactly what a lever produces there, not
// evidence a lever was unnecessary.
//
// A release seam nonetheless has exactly ONE user — this site — but the ground for
// that is different from "the 18 are measured empty": it is what each of the 18
// borrows, checkable by reading rather than by an unreproducible instrumentation
// run. The 11 `logout_exchange_test.cpp` sites need no seam because they borrow
// nothing that dies first — `feed_inbound` copies each inbound frame into
// `inbound_frames`, a fixture-owned deque declared before `ioc`
// (`logout_exchange_test.cpp:172, :218-226`), and spans THAT, not the caller's
// temporary. The 7 `test_live_outbound_serialized.cpp` sites each carry their own
// `frames` deque declared before `ioc` the same way, plus, at the one site that
// deliberately reaches the guard on an abnormal path, a working transport lever
// that closes what the deque-ordering argument alone does not cover. Both are
// facts about the tree rather than an assumption about it. Hoisting a generic
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
    asio::io_context& ioc;
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
                ioc.restart();
                ioc.run_for(budget);
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
                // (#305): both `quiesce_on_exit` and `drain_or_report` carry the same
                // probe, and the header's claim is corrected in place. The probe stays
                // duplicated here because this guard does not delegate to either of
                // them — it computes the verdict itself so the release decision cannot
                // drift from it — not because the shared version is still wrong.
                (void)ioc.poll_one();
                quiesced = ioc.stopped();
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

            ADD_FAILURE()
                << "quiesce_or_release_on_exit: the io_context did not run out of work within "
                   "the configured quiesce window, so a coroutine frame is still suspended and "
                   "will be destroyed by ~io_context after this scope unwinds. The "
                   "SessionFixtures were RELEASED deliberately (#303) so those frames still "
                   "have live referents, and so that no Session strand handle is destroyed "
                   "after its io_context. This guard reports the residual; it does not claim "
                   "to have found its cause.";
        } catch (...) {
            // Nothing may escape a destructor.
        }
    }
};

}  // anonymous namespace

// ── Test 1: CrossSessionDisjoint ──────────────────────────────────────────────
//
// RED assertion (a): session A's TestReqID set is disjoint from session B's.
// With a `static tr_counter`, both sessions share the counter so IDs interleave
// → the assertion fails in the RED phase.
// With `++next_test_request_id_` (per-session), the sequences are disjoint.
//
// GREEN assertion (b): within each session, IDs increment monotonically.
//
// SC-003: we advance the clock enough for each session to emit ≥ 100
// TestRequests (fast test; TSan is the stress mechanism, not the count).
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
    // below only fixes the ORDER of destruction relative to itself; it cannot
    // force quiescence (see its definition — it reports residual work via
    // ADD_FAILURE rather than eliminating it). On the budget-exhausted path a
    // suspended frame survives the guard and is destroyed by ~io_context, so
    // only storage declared before `ioc` is guaranteed to still be alive then.
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
    asio::io_context ioc;
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
    quiesce_or_release_on_exit quiesce{ioc, *clock, {&sA, &sB}};

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
    // Heartbeat frame (35=0) with TestReqID (112) to clear the pending TR.
    auto make_heartbeat = [](std::string_view bs, std::uint32_t seq, std::string_view sender,
                             std::string_view target,
                             std::string_view tr_id) -> std::vector<std::byte> {
        std::string body;
        body += "35=0\x01";
        body += "34=" + std::to_string(seq) + "\x01";
        body += "49=" + std::string(sender) + "\x01";
        body += "52=20240101-00:00:00.000\x01";
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
    };

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

        if (!tr_ids_a.empty()) {
            std::string latest_a = tr_ids_a.back();
            auto& hb = frames.emplace_back(
                make_heartbeat("FIX.4.2", hb_seq_a++, "TARGET_A", "SENDER_A", latest_a));
            auto fut = asio::co_spawn(
                ioc,
                sA->session->on_inbound_frame(std::span<const std::byte>{hb.data(), hb.size()}),
                asio::use_future);
            ASSERT_TRUE(pump_until_ready(ioc, fut))
                << kPumpBudgetMiss << "feeding session A's Heartbeat at iteration " << i;
            (void)fut.get();
        }

        if (!tr_ids_b.empty()) {
            std::string latest_b = tr_ids_b.back();
            auto& hb = frames.emplace_back(
                make_heartbeat("FIX.4.2", hb_seq_b++, "TARGET_B", "SENDER_B", latest_b));
            auto fut = asio::co_spawn(
                ioc,
                sB->session->on_inbound_frame(std::span<const std::byte>{hb.data(), hb.size()}),
                asio::use_future);
            ASSERT_TRUE(pump_until_ready(ioc, fut))
                << kPumpBudgetMiss << "feeding session B's Heartbeat at iteration " << i;
            (void)fut.get();
        }
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
    {
        // Extract numeric values and verify they form a contiguous sequence 1..N.
        auto check_contiguous = [](const std::vector<std::string>& ids,
                                   const char* session_name) -> bool {
            if (ids.empty()) {
                return true;
            }
            std::vector<std::uint32_t> ns;
            ns.reserve(ids.size());
            for (const auto& id : ids) {
                std::uint32_t n = parse_tr_id(id);
                if (n == 0) {
                    return false;
                }
                ns.push_back(n);
            }
            // Sequence must start at 1 and be strictly increasing by 1.
            for (std::size_t i = 0; i < ns.size(); ++i) {
                if (ns[i] != static_cast<std::uint32_t>(i + 1)) {
                    return false;
                }
            }
            return true;
        };

        bool a_contiguous = check_contiguous(ids_a, "A");
        bool b_contiguous = check_contiguous(ids_b, "B");

        EXPECT_TRUE(a_contiguous)
            << "Session A's TestReqID sequence is not contiguous 1..N — "
            << "the counter may be shared with another session (static tr_counter bug). "
            << "IDs: " << ids_a.size() << " total";
        EXPECT_TRUE(b_contiguous)
            << "Session B's TestReqID sequence is not contiguous 1..N — "
            << "the counter may be shared with another session (static tr_counter bug). "
            << "IDs: " << ids_b.size() << " total";
    }

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
// Clock advances are sequential (one session's clock advanced at a time) to
// avoid races on the mock_clock's internal state (which is a test concern,
// not the SUT concern). The TSan stress comes from both sessions running
// their liveness loops concurrently on the pool threads.
//
// #284 disposition: the six `fut.get()` calls below are NOT the #284 defect.
// This test runs on a `thread_pool`, whose own threads service the work, so a
// get() here waits on a context that is still being pumped. #284 is specific to
// the single-threaded io_context in test 1, where the test thread was both the
// only pump and the blocked waiter.
TEST(CrossSessionTestReqID, ConcurrentSessionsTSanStress) {
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

    // Open both sessions.
    {
        auto fa = asio::co_spawn(pool, sA.session->open(), asio::use_future);
        auto fb = asio::co_spawn(pool, sB.session->open(), asio::use_future);
        ASSERT_TRUE(fa.get().has_value()) << "Session A open failed";
        ASSERT_TRUE(fb.get().has_value()) << "Session B open failed";
    }

    // Drive to Active (sequential to avoid concurrent on_inbound_frame calls
    // on the same executor before the strand is fully set up).
    {
        auto logon_a = make_logon_frame("FIX.4.2", 1, "TARGET_A", "SENDER_A", 1);
        auto fa = asio::co_spawn(pool,
                                 sA.session->on_inbound_frame(
                                     std::span<const std::byte>{logon_a.data(), logon_a.size()}),
                                 asio::use_future);
        fa.get();
    }
    {
        auto logon_b = make_logon_frame("FIX.4.2", 1, "TARGET_B", "SENDER_B", 1);
        auto fb = asio::co_spawn(pool,
                                 sB.session->on_inbound_frame(
                                     std::span<const std::byte>{logon_b.data(), logon_b.size()}),
                                 asio::use_future);
        fb.get();
    }

    ASSERT_EQ(sA.session->state(), fixpp::session::fsm_state::Active);
    ASSERT_EQ(sB.session->state(), fixpp::session::fsm_state::Active);

    // Advance both clocks sequentially (not concurrently) to avoid races on
    // the mock_clock's internals. The TSan stress is on the `tr_counter` that
    // both sessions' liveness loops (running on separate pool strands) access.
    //
    // Each clock advance triggers the respective session's liveness loop to
    // emit a TestRequest. The two liveness loops may run concurrently on the
    // pool's threads → TSan window for `static tr_counter` race.
    for (int i = 0; i < 10; ++i) {
        // Advance session A's clock.
        clock_a->advance(std::chrono::milliseconds{1500});
        // Advance session B's clock.
        clock_b->advance(std::chrono::milliseconds{1500});
        // Allow both liveness loops to process (they run on pool threads
        // concurrently; we don't block here, TSan instruments the actual access).
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }

    // Close both sessions cleanly (sequential to avoid concurrent close races).
    {
        auto fa = asio::co_spawn(pool, sA.session->close(fixpp::session::close_mode::terminal),
                                 asio::use_future);
        fa.get();
    }
    {
        auto fb = asio::co_spawn(pool, sB.session->close(fixpp::session::close_mode::terminal),
                                 asio::use_future);
        fb.get();
    }

    pool.join();

    // Collect results and verify per-session contiguity.
    auto ids_a = sA.transport.collect_test_req_ids();
    auto ids_b = sB.transport.collect_test_req_ids();

    // Per-session isolation: each session's sequence must be contiguous 1..N.
    auto check_contiguous = [](const std::vector<std::string>& ids) -> bool {
        if (ids.empty()) {
            return true;
        }
        for (std::size_t i = 0; i < ids.size(); ++i) {
            if (parse_tr_id(ids[i]) != static_cast<std::uint32_t>(i + 1)) {
                return false;
            }
        }
        return true;
    };

    if (!ids_a.empty()) {
        EXPECT_TRUE(check_contiguous(ids_a))
            << "Session A TestReqID sequence has gaps (shared static counter?)";
    }
    if (!ids_b.empty()) {
        EXPECT_TRUE(check_contiguous(ids_b))
            << "Session B TestReqID sequence has gaps (shared static counter?)";
    }

    // Monotone within each session (assertion (b)).
    if (!ids_a.empty()) {
        std::uint32_t prev = 0;
        bool ok = true;
        for (const auto& id : ids_a) {
            auto n = parse_tr_id(id);
            if (n > 0 && n <= prev) {
                ok = false;
                break;
            }
            if (n > 0) {
                prev = n;
            }
        }
        EXPECT_TRUE(ok) << "Session A TestReqID sequence is not monotone";
    }
    if (!ids_b.empty()) {
        std::uint32_t prev = 0;
        bool ok = true;
        for (const auto& id : ids_b) {
            auto n = parse_tr_id(id);
            if (n > 0 && n <= prev) {
                ok = false;
                break;
            }
            if (n > 0) {
                prev = n;
            }
        }
        EXPECT_TRUE(ok) << "Session B TestReqID sequence is not monotone";
    }
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

    EXPECT_NONFATAL_FAILURE(
        ([&destructions, &destructions_b, &weak_clock] {
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

            asio::io_context ioc;
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
            quiesce_or_release_on_exit guard{ioc, *clock, {&sA, &sB}, std::chrono::milliseconds{0}};

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
        "the io_context did not run out of work");

    // THE DIRECT OBSERVATION. A mutant that deletes the release() while keeping the
    // message above passes the SPI matcher and fails here.
    EXPECT_EQ(destructions.load(std::memory_order_relaxed), 0)
        << "the SessionFixture was DESTROYED on the residual teardown path. Its Session's "
           "strand handle is then destroyed after ~io_context destroys the frames that "
           "borrow it, which is the ordering #303 exists to prevent.";

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

    EXPECT_NONFATAL_FAILURE(
        ([&destructions] {
            asio::io_context ioc;
            auto clock = make_witness_clock(ioc);

            auto sA =
                std::make_unique<SessionFixture>(ioc.get_executor(), clock, "SENDER_A", "TARGET_A");
            sA->destructions = &destructions;

            asio::post(ioc, [] { throw std::runtime_error("gate-b/r1 F6: injected pump fault"); });

            quiesce_or_release_on_exit guard{ioc, *clock, {&sA}, std::chrono::milliseconds{0}};
        }()),
        "the io_context did not run out of work");

    // THE DIRECT OBSERVATION. A mutant that turns the inner catch's
    // `quiesced = false` into `quiesced = true` would destroy the fixture here
    // instead of releasing it, and this assertion catches that.
    EXPECT_EQ(destructions.load(std::memory_order_relaxed), 0)
        << "the SessionFixture was DESTROYED after a throwing pump handler. The inner "
           "catch(...) must treat a mid-pump exception as residual (quiesced=false) and "
           "release, not destroy, since the exception leaves the outstanding work in an "
           "unknown state.";
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

    {
        asio::io_context ioc;
        auto clock = make_witness_clock(ioc);
        weak_clock = clock;

        auto sA =
            std::make_unique<SessionFixture>(ioc.get_executor(), clock, "SENDER_A", "TARGET_A");
        sA->destructions = &destructions;

        // Nothing spawned and a real budget, so the guard drains and takes the
        // quiesced branch.
        quiesce_or_release_on_exit guard{ioc, *clock, {&sA}, std::chrono::seconds{1}};

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
    asio::io_context ioc;
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
    quiesce_or_release_on_exit guard{ioc, *clock, {}, std::chrono::milliseconds{0}};
    // ~guard runs here and must add no failure.
}

}  // namespace fixpp::session::test
