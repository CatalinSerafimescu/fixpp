// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/log/test_file_sink_backpressure.cpp
//
// #211: FileSink under producer backpressure.
//
// THE GAP THIS CLOSES. The logger's overflow/drop behaviour was tested only
// against in-memory mocks (test_overflow_drop_newest.cpp,
// test_block_overflow_raw_thread.cpp use PausingSink / ExactPausingSink), and
// the FileSink tests exercise real files but never fill the queue -- their
// `drop_count() == 0` is INCIDENTAL, a side effect of a fast tmpfs sink keeping
// up, not a deliberate backpressure assertion. So a regression in drop
// accounting on the real-sink path was caught by neither family: the mock tests
// do not use FileSink, and the FileSink tests never overflow.
//
// THE STIMULUS, AND WHY NOT THE OBVIOUS ONE. Injecting a blocking
// `FileSinkConfig::fsync_fn` does NOT produce backpressure: FileSink::flush()
// dispatches the fsync to the OWNED WORKER thread and wait_for(deadline)s on it,
// so a blocked fsync_fn stalls the worker, not the drain -- the drain loop keeps
// consuming and the queue never fills. (FileSinkFsyncTest.ProducerDoesNotBlockOn
// Fsync pins exactly that property.) The stall has to be on the drain thread's
// OWN write path, i.e. inside FileSink::emit().
//
// So the lever is a ROTATION STORM: a small `max_file_bytes` makes emit()'s
// post-write `bytes_written_ > max_file_bytes` check fire every few records and
// call rotate() INLINE on the drain thread -- stop-worker + fflush + fclose +
// rename + archive-directory scan + prune + fopen + start-worker. That is real
// drain-side slowness through the production code path, with no fake seam and no
// synthetic slow sink standing in for the thing under test.
//
// WHAT IS ASSERTED. Not "drops happened" alone -- a count-only assertion would
// false-pass a torn-write regression. The invariant is BOTH halves together:
// under backpressure the real file sink drops ACCOUNTABLY and never corrupts
// what it does write.
//
// Anchors:
//   [2k §4.3]              — drop_newest preserves oldest in-flight
//   [2k §4.5]              — FileSink config + rotation semantics
//   contracts/log-core.md  — FR-003/FR-004 drop accounting
//   contracts/log-sinks.md §FileSink

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory_resource>
#include <ranges>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <fixpp/log/file_sink.hpp>
#include <fixpp/log/level.hpp>
#include <fixpp/log/logger.hpp>
#include <fixpp/log/record.hpp>
#include <fixpp/log/sink.hpp>

namespace {

// ── Line parser ───────────────────────────────────────────────────────────────
//
// FileSink writes, per record (src/log/file_sink.cpp format_line):
//
//     <secs>.<6-digit micros> [<LEVEL>] cat=<n> msg <payload>\n
//
// A record survives intact only if the WHOLE line is well formed. Parsing just
// the trailing integer would accept a line torn mid-write and silently rejoined
// with another -- which is the corruption this test exists to detect -- so the
// parser validates every field and reports the first thing that is wrong.

struct ParsedLine {
    bool          ok{false};
    std::uint64_t payload{0};
    std::string   why;  // non-empty iff !ok
};

bool all_digits(std::string_view s) noexcept {
    return !s.empty() && std::ranges::all_of(s, [](char c) { return c >= '0' && c <= '9'; });
}

ParsedLine parse_line(std::string const& line) {
    // "<secs>.<micros> [LEVEL] cat=<n> msg <payload>"
    auto fail = [&](char const* why) { return ParsedLine{.ok = false, .payload = 0, .why = why}; };

    auto dot = line.find('.');
    if (dot == std::string::npos) return fail("no '.' separating seconds from microseconds");
    if (!all_digits(std::string_view{line}.substr(0, dot))) return fail("seconds not numeric");

    // Exactly 6 microsecond digits, then a space (the "%06lld " of format_line).
    if (line.size() < dot + 8) return fail("truncated after the timestamp");
    if (!all_digits(std::string_view{line}.substr(dot + 1, 6))) return fail("micros not 6 digits");
    if (line[dot + 7] != ' ') return fail("no space after the timestamp");

    auto lb = line.find('[', dot + 7);
    auto rb = line.find(']', dot + 7);
    if (lb == std::string::npos || rb == std::string::npos || rb < lb) return fail("no [LEVEL]");
    if (rb == lb + 1) return fail("empty [LEVEL]");

    constexpr std::string_view k_cat = " cat=";
    if (line.compare(rb + 1, k_cat.size(), k_cat) != 0) return fail("no ' cat=' after [LEVEL]");

    constexpr std::string_view k_body = " msg ";
    auto body = line.find(k_body, rb + 1);
    if (body == std::string::npos) return fail("no ' msg ' body (unregistered format_id?)");

    auto cat_first = rb + 1 + k_cat.size();
    if (!all_digits(std::string_view{line}.substr(cat_first, body - cat_first)))
        return fail("category not numeric");

    auto tail = std::string_view{line}.substr(body + k_body.size());
    if (!all_digits(tail)) return fail("payload not numeric");
    // ⚠️ all_digits is NOT enough to make std::stoull safe. A torn line that
    // merges " msg 42" with a following record's timestamp yields a 20+ digit
    // run that passes all_digits and then throws out_of_range -- so the one
    // input this parser exists to describe would ABORT the test instead of
    // being reported as malformed. An integrity check that crashes rather than
    // reports is the wrong failure mode for a corruption detector.
    if (tail.size() > 19) return fail("payload implausibly long (torn/merged line?)");

    return ParsedLine{.ok = true, .payload = std::stoull(std::string{tail}), .why = {}};
}

// One log file's worth of parsed records, in the order they appear in the file.
struct FileRecords {
    std::filesystem::path      path;
    std::vector<std::uint64_t> payloads;
    std::vector<std::string>   malformed;  // "<file>:<lineno>: <why>: <line>"
};

// Read every *.log in `dir` -- the live file AND the archives.
//
// ⚠️ The files are NOT ordered here, deliberately. rotate() disambiguates
// same-second archive names by appending "_<counter>", so a storm produces
// <base>.<ts>_1.log, _2.log ... _10.log, which sort LEXICOGRAPHICALLY as
// _1, _10, _2. Reconstructing record order from sorted filenames would be
// wrong. Ordering is asserted from CONTENT instead (see the test body).
std::vector<FileRecords> read_all_logs(std::filesystem::path const& dir) {
    std::vector<FileRecords> out;
    std::error_code          ec;
    for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (entry.path().extension() != ".log") continue;

        FileRecords fr;
        fr.path = entry.path();

        std::ifstream in{entry.path(), std::ios::binary};
        std::string   line;
        int           lineno = 0;
        while (std::getline(in, line)) {
            ++lineno;
            if (line.empty()) continue;
            auto parsed = parse_line(line);
            if (parsed.ok) {
                fr.payloads.push_back(parsed.payload);
            } else {
                fr.malformed.push_back(fr.path.filename().string() + ":" +
                                       std::to_string(lineno) + ": " + parsed.why + ": [" + line +
                                       "]");
            }
        }
        out.push_back(std::move(fr));
    }
    return out;
}

// ── Fixture ───────────────────────────────────────────────────────────────────

class FileSinkBackpressureTest : public ::testing::Test {
protected:
    void SetUp() override {
        tmpdir_ = std::filesystem::temp_directory_path() /
                  ("fixpp_backpressure_test_" +
                   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(tmpdir_);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(tmpdir_, ec);
    }

    // Enqueue payloads [first, first+count) as fast as the producer can. The
    // payload is the global record index, so a surviving line names exactly
    // which record it is.
    static void burst(fixpp::log::Logger& logger, std::uint64_t first, std::uint64_t count) {
        std::array<std::uint8_t, 16> zeroed_trace_id{};
        auto ts = fixpp::core::utc_time_point{std::chrono::system_clock::now().time_since_epoch()};
        constexpr auto fmt_id = static_cast<std::uint32_t>(fixpp::log::detail::crc32_str("msg {}"));

        for (std::uint64_t i = 0; i < count; ++i) {
            logger.enqueue(fixpp::log::Level::info, fixpp::log::cat::session, fmt_id,
                           zeroed_trace_id, 0u, ts,
                           {fixpp::log::ArgValue::from_u64(first + i)});
        }
    }

    // How many *.log files exist right now. Reading the DIRECTORY rather than
    // the sink's rotation_count() is deliberate: rotation_count_ is a plain
    // uint64 mutated by the drain thread, so the producer thread cannot read it
    // without a data race, and this test has to observe rotation progress WHILE
    // the drain is running. Filesystem state carries no such hazard.
    [[nodiscard]] std::size_t log_file_count() const {
        std::size_t     n = 0;
        std::error_code ec;
        for (auto const& e : std::filesystem::directory_iterator(tmpdir_, ec)) {
            if (ec) break;
            if (e.path().extension() == ".log") ++n;
        }
        return n;
    }

    std::filesystem::path tmpdir_;
};

}  // namespace

// ── #211: the witness ─────────────────────────────────────────────────────────

TEST_F(FileSinkBackpressureTest, RealFileSinkDropsAccountablyUnderRotationStorm) {
    fixpp::log::FileSinkConfig cfg;
    cfg.directory = tmpdir_;
    cfg.base_name = "backpressure";
    // THE STALL LEVER. ~200 bytes is a handful of ~40-byte lines, so rotate()
    // fires every few records on the drain thread. Deliberately not 1 byte:
    // one record per file would make the within-file ordering assertion below
    // vacuous, and it multiplies rotate()'s O(archives) directory scan.
    cfg.max_file_bytes = 200u;
    // Retain EVERY archive. Pruning would delete records the accounting below
    // expects to read back, turning a clean drop into an apparent loss.
    cfg.max_keep_count = 1000000u;
    // Production default. Each rotate() therefore also stops and restarts the
    // owned fsync worker, which is part of the real drain-side cost.
    cfg.async_fsync = true;

    auto* sink_raw = new fixpp::log::FileSink(std::move(cfg));
    std::pmr::vector<std::unique_ptr<fixpp::log::Sink>> sinks{};
    sinks.push_back(std::unique_ptr<fixpp::log::Sink>(sink_raw));

    fixpp::log::LoggerConfig lcfg;
    lcfg.capacity      = 64u;  // power of 2, far smaller than any chunk below
    lcfg.on_overflow   = fixpp::log::overflow_policy::drop_newest;
    lcfg.drain_timeout = std::chrono::seconds{60};

    auto logger = std::make_unique<fixpp::log::Logger>(std::move(lcfg), std::move(sinks));

    // ── Produce until backpressure and the rotation storm are OBSERVED to
    //    coexist ────────────────────────────────────────────────────────────
    //
    // ⚠️ A SINGLE FIXED BURST IS THE WRONG SHAPE HERE, AND IT WAS MEASURED SO --
    // record that rather than leaving the next author to rediscover it. Bursting
    // a fixed 4000 records at a 64-slot ring reported drops=3936 on five
    // consecutive runs: exactly 4000 - 64, i.e. the drain consumed NOTHING while
    // the producer ran and only drained the ring at shutdown. Those drops are
    // producer speed, not sink stall -- an in-memory mock would have produced the
    // identical number, so `drop_count() > 0` was passing for a reason that had
    // nothing to do with FileSink. That is a SPURIOUS HIT, and no amount of
    // widening the burst fixes it: an unpaced producer outruns any sink.
    //
    // What discriminates is not how many were dropped but WHEN. So: produce in
    // chunks until an archive appears on disk -- proof that rotate() has run to
    // completion on the drain thread -- then keep producing and require the drop
    // count to rise AFTER that point. Drops accrued while the real FileSink was
    // demonstrably rotating is the concurrency claim stated structurally, with no
    // clock and no tuned rate.
    //
    // ⚠️ AND THE ISSUE'S OWN SECONDARY LEVER IS UNREACHABLE -- do not reach for
    // it. #211 suggests "a blocking fsync_fn AND a long flush deadline, so
    // flush()'s wait_for(deadline) genuinely parks the drain thread". It cannot:
    // Logger's drain loop calls Sink::flush() exactly once, AFTER the loop exits
    // (src/log/logger.cpp, "Flush all sinks after draining"), and async_flush()
    // posts a ring sentinel that runs a completion callback without touching the
    // sink. So no fsync gate is reachable while records are being produced, and
    // emit()/rotate() is the only drain-side stall there is.
    constexpr std::uint64_t k_chunk = 20000;
    // A failure escape, not a tuned parameter: it exists so a build where the
    // drain never runs fails loudly instead of producing forever.
    constexpr std::uint64_t k_max_records = 400000;

    std::uint64_t produced                 = 0;
    std::uint64_t drops_at_first_rotation  = 0;
    bool          rotation_seen            = false;

    while (produced < k_max_records) {
        burst(*logger, produced, k_chunk);
        produced += k_chunk;

        if (!rotation_seen) {
            // > 1 file means the live file PLUS at least one archive, i.e. a
            // rotation completed.
            //
            // ⚠️ YIELD-AND-POLL before producing another chunk. The drain needs
            // only a few records of progress to rotate, but on a starved runner
            // it might not get them within the record bound, and then
            // `rotation_seen` stays false and the ASSERT below fires on a
            // CORRECT implementation -- the exact flake class this PR exists to
            // remove, reintroduced by the test that ships with the fix. Waiting
            // costs nothing: this chunk's drops are already banked, so the
            // concurrency claim below is unweakened.
            //
            // ⚠️ AND THE WAIT MUST BE BOUNDED, or red arm 4 HANGS INSTEAD OF
            // FAILING. That arm removes the storm lever, so no rotation is ever
            // possible and an unbounded poll would spin forever -- an arm that
            // hangs grades nothing. The budget is a yield COUNT, not a duration:
            // it says "give the scheduler this many chances", which does not
            // rot the way a millisecond ceiling does. Exhausting it is not a
            // failure; it falls through to produce another chunk, and only the
            // record bound above ends the loop.
            constexpr int k_yields = 100000;
            for (int y = 0; y < k_yields && log_file_count() <= 1; ++y) {
                std::this_thread::yield();
            }
            if (log_file_count() > 1) {
                rotation_seen           = true;
                drops_at_first_rotation = logger->drop_count();
            }
            continue;
        }
        if (logger->drop_count() > drops_at_first_rotation) break;
    }

    ASSERT_TRUE(rotation_seen) << "no archive ever appeared after producing " << produced
                               << " records — rotate() never completed on the drain thread, so "
                               << "nothing here exercises the real FileSink write path";

    // ── Backpressure, stated WITHOUT a clock ─────────────────────────────────
    //
    // Records were dropped AFTER the sink had begun rotating. A producer that
    // had waited for the drain would have found room for every record and
    // dropped none; a rise here is proof it ran ahead and discarded instead,
    // while the real sink was busy in rotate(). This deliberately replaces "the
    // burst took < N ms", which would be one more absolute wall-clock ceiling on
    // a shared runner (#400).
    // Read once and reused for the accounting below: drop_count_ is incremented
    // only inside enqueue() (src/log/logger.cpp), the producer loop has ended,
    // and shutdown() enqueues nothing — so a second read after shutdown would be
    // the same number under a different name, and the assertion message here and
    // the accounting messages below would only LOOK like they might disagree.
    auto const drops = logger->drop_count();
    EXPECT_GT(drops, drops_at_first_rotation)
        << "no record was dropped after the first rotation completed (drops were "
        << drops_at_first_rotation << " then and " << drops << " after producing "
        << produced << " records) — the ring stopped overflowing once the storm started, so this "
        << "run does not witness backpressure against a rotating FileSink";

    // A drain timeout here is a real outcome, not a test bug: it is reported
    // through timeout_drop_count() and folded into the accounting below rather
    // than being asserted away.
    auto const shutdown_result = logger->shutdown(std::chrono::seconds{60});

    // timeouts MUST be read after shutdown(): timeout_drop_count_ is bumped
    // inside Logger::shutdown itself.
    auto const timeouts   = logger->timeout_drop_count();
    auto const filtered   = logger->filter_count();
    auto const rotations  = sink_raw->rotation_count();

    // Destroy the logger BEFORE reading: ~Logger closes the sink, which fcloses
    // the live file. Reading it while still open would race the stdio buffer.
    logger.reset();

    // ── The lever actually engaged ───────────────────────────────────────────
    //
    // Without this, a green run cannot distinguish "the real sink was slow" from
    // "the producer was simply faster than a fast sink" — and the test would no
    // longer be about FileSink at all.
    EXPECT_GT(rotations, 0u) << "no rotation happened — the stall lever did not engage, so this "
                                "run says nothing about the real FileSink write path";

    EXPECT_EQ(filtered, 0u) << "no category filter is configured; a nonzero filter_count means "
                               "records went missing down a path this accounting does not model";

    auto const files = read_all_logs(tmpdir_);

    // The shape of the run, for whoever reads the log of a failure. Diagnostics,
    // never an assertion: every one of these numbers is load-dependent, and
    // pinning any of them would put back the class of ceiling #400 removed.
    GTEST_LOG_(INFO) << "storm shape: enqueued=" << produced << " drops=" << drops
                     << " timeouts=" << timeouts << " rotations=" << rotations
                     << " files=" << files.size();

    // ── Integrity: nothing torn, nothing interleaved ─────────────────────────

    std::vector<std::string> malformed;
    for (auto const& f : files) {
        malformed.insert(malformed.end(), f.malformed.begin(), f.malformed.end());
    }
    EXPECT_TRUE(malformed.empty())
        << malformed.size() << " malformed line(s) — a drop must be a CLEAN loss, never a torn or "
        << "interleaved write. First: " << (malformed.empty() ? std::string{} : malformed.front());

    // Within a file, the drain wrote sequentially, so payloads must strictly
    // increase. (drop_newest discards the NEWEST arrival when the ring is full,
    // so what survives is a subsequence of the enqueue order, never a reordering.)
    for (auto const& f : files) {
        // STRICTLY increasing: is_sorted would accept a duplicated payload, which
        // is exactly what a record written twice looks like.
        EXPECT_EQ(std::ranges::adjacent_find(f.payloads, std::ranges::greater_equal{}),
                  f.payloads.end())
            << "payloads not strictly increasing within " << f.path.filename().string()
            << " — the drain does not reorder or repeat, so this is a torn or interleaved write";
    }

    // Across files: each file's payload range must be DISJOINT from every other's.
    // Together with strict increase within a file, that is the real no-interleaving
    // claim, and unlike "sort the archives and concatenate" it does not assume an
    // ordering of the files (see read_all_logs).
    std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
    for (auto const& f : files) {
        if (!f.payloads.empty()) ranges.emplace_back(f.payloads.front(), f.payloads.back());
    }
    std::ranges::sort(ranges);
    for (std::size_t i = 1; i < ranges.size(); ++i) {
        EXPECT_GT(ranges[i].first, ranges[i - 1].second)
            << "payload ranges [" << ranges[i - 1].first << ".." << ranges[i - 1].second << "] and ["
            << ranges[i].first << ".." << ranges[i].second << "] overlap — records from one file "
            << "were interleaved into another";
    }

    std::vector<std::uint64_t> all;
    for (auto const& fr : files) all.insert(all.end(), fr.payloads.begin(), fr.payloads.end());
    std::size_t const surviving = all.size();
    std::ranges::sort(all);
    EXPECT_EQ(std::ranges::adjacent_find(all), all.end())
        << "a payload appears more than once across the log files — a record was written twice";

    // ── Accounting ───────────────────────────────────────────────────────────
    //
    // The HARD invariant is `<=`, not `==`, and the reason is in the sink rather
    // than in this test: FileSink has three loss paths that increment NO counter
    // at all -- rotate() falls through to fopen(live, "wb") (TRUNCATE) if the
    // rename fails, emit() only counts `written > 0` so a short fwrite loses
    // bytes silently, and emit() early-returns when a failed rotate left
    // stream_ == nullptr. All three are swallowed by the enclosing catch(...).
    // A strict `==` would therefore turn a transient filesystem hiccup on a
    // shared runner into a red -- precisely the failure mode #400 is about, and
    // shipping it in the PR that fixes #400 would be an odd way to spend the
    // lesson.
    // ⚠️ `timeout_drop_count()` IS NOT A RECORD COUNT and must not be added to
    // one. Despite the name, Logger::shutdown does `timeout_drop_count_
    // .fetch_add(1)` ONCE per timed-out shutdown call (src/log/logger.cpp) — it
    // counts timeout EVENTS, not the records abandoned by them. An earlier
    // revision of this test folded it into the sum and read 40001 <= 40000 on a
    // run where the drain timed out. Caught on MSVC, where a stale mutant binary
    // made shutdown time out; the arithmetic defect was mine either way and
    // would have fired on any genuinely slow runner.
    //
    // The two real record counts are `surviving` and `drops`. `<=` rather than
    // `==` because a timed-out shutdown legitimately abandons records still in
    // the ring, and because of the uncounted sink-side loss paths described
    // below. `timeouts` appears only as a gate and in diagnostics.
    EXPECT_LE(surviving + drops, produced)
        << "surviving(" << surviving << ") + drops(" << drops << ") exceeds the " << produced
        << " records enqueued — the drop accounting over-counts, or a record was "
        << "written more than once";

    // The exact equality IS asserted, but only behind a witness that the silent
    // loss paths above did not fire: one rotation produces exactly one archive
    // when nothing is pruned, so archives == rotations means every rename
    // succeeded and every live file was preserved. When it does not hold, say so
    // loudly instead of failing the equality — that is a diagnosis, not a defect
    // in the accounting under test.
    // Guard the subtraction: read_all_logs returns empty if open() ever failed,
    // and an unsigned underflow would print SIZE_MAX in the diagnosis below --
    // nonsense in exactly the message someone reads when something went wrong.
    auto const archives = files.empty() ? 0u : files.size() - 1;  // minus the live file
    if (archives == rotations && shutdown_result.has_value()) {
        // shutdown_result.has_value() is what makes the EQUALITY sound: a clean
        // shutdown means the ring was fully drained, so every enqueued record
        // either reached the file or was counted as dropped. On a timed-out
        // shutdown records can still be sitting in the ring, and only `<=` holds.
        EXPECT_EQ(surviving + drops, produced)
            << "drop accounting does not balance: surviving(" << surviving << ") + drops(" << drops
            << ") != " << produced;
    } else {
        GTEST_LOG_(WARNING) << "accounting equality not checked this run: archives=" << archives
                            << " rotations=" << rotations
                            << " shutdown_ok=" << shutdown_result.has_value()
                            << " — a sink-side loss path may have fired (see comment above)";
    }
}

// ── Calibration: the reader can account for EVERY record when none is dropped ─
//
// ⚠️ Prove the instrument can report the other answer. The witness above
// concludes "records were dropped" partly from a shortfall in what the file
// reader found — so a reader that silently misses lines would manufacture that
// shortfall and the witness would pass for the wrong reason.
//
// Same producer burst, same real FileSink, two differences that make a drop
// STRUCTURALLY impossible: the ring has more slots than the burst has records
// (so it can never be full), and max_file_bytes is large enough that rotate()
// never fires. Every record must then come back, exactly once, in order.
TEST_F(FileSinkBackpressureTest, EveryRecordSurvivesWhenTheRingCannotFill) {
    // Local: the witness above paces itself by chunks and does not share this
    // number. (It was at namespace scope with a comment claiming both tests used
    // it — they never did.)
    constexpr std::uint64_t k_control_records = 4000;

    fixpp::log::FileSinkConfig cfg;
    cfg.directory      = tmpdir_;
    cfg.base_name      = "calibration";
    cfg.max_file_bytes = 256u * 1024u * 1024u;  // no rotation
    cfg.max_keep_count = 8u;
    cfg.async_fsync    = true;

    std::pmr::vector<std::unique_ptr<fixpp::log::Sink>> sinks{};
    sinks.push_back(std::make_unique<fixpp::log::FileSink>(std::move(cfg)));

    fixpp::log::LoggerConfig lcfg;
    // Power of 2 (LoggerConfig::capacity requires it) and strictly greater than
    // the burst, so the ring cannot fill however slow the drain is. This is a
    // structural guarantee, not a race the drain has to win.
    static_assert(k_control_records < 8192u, "control ring must exceed the burst");
    lcfg.capacity      = 8192u;
    lcfg.on_overflow   = fixpp::log::overflow_policy::drop_newest;
    lcfg.drain_timeout = std::chrono::seconds{60};

    auto logger = std::make_unique<fixpp::log::Logger>(std::move(lcfg), std::move(sinks));

    burst(*logger, 0, k_control_records);
    auto const shutdown_result = logger->shutdown(std::chrono::seconds{60});
    EXPECT_TRUE(shutdown_result.has_value()) << "the control run must drain fully";

    auto const drops    = logger->drop_count();
    auto const timeouts = logger->timeout_drop_count();
    logger.reset();

    EXPECT_EQ(drops, 0u) << "the ring has more slots than the burst has records — a drop here means "
                            "drop accounting fires when the ring is not full";
    EXPECT_EQ(timeouts, 0u) << "the control run timed out draining";

    auto const files = read_all_logs(tmpdir_);
    ASSERT_EQ(files.size(), 1u) << "the control run must not rotate";

    EXPECT_TRUE(files.front().malformed.empty())
        << files.front().malformed.size() << " malformed line(s) in the control run. First: "
        << (files.front().malformed.empty() ? std::string{} : files.front().malformed.front());

    // THE CALIBRATION: the reader accounts for all of them, in order. If this
    // passes and the witness still shows a shortfall, the shortfall is a real
    // drop and not a parser miss.
    ASSERT_EQ(files.front().payloads.size(), k_control_records)
        << "the file reader found " << files.front().payloads.size() << " of " << k_control_records
        << " records that were all written — the reader under-counts, which would make the "
        << "witness test's drop shortfall unattributable";

    std::vector<std::uint64_t> expected(k_control_records);
    for (std::uint64_t i = 0; i < k_control_records; ++i) expected[i] = i;
    EXPECT_EQ(files.front().payloads, expected)
        << "the control run's records are not exactly 0..N-1 in order";
}
