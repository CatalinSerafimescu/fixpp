// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/wire/framer_resync_test.cpp
//
// 093-inbound-frame-dispositions: the Framer's opt-in resync (contract C-1; data-model
// E-1), the Framer halves of quickstart.md Q-2 to Q-6 and Q-10.
//
// Every shape runs through `run()`, which drives the Framer the way the session's read
// pump does: `out` has one slot, and after each read the carry is drained with
// carry-only feeds until a feed produces nothing. Small shapes are fed whole, split at
// every byte boundary, and one byte per read; the large Q-4 shapes in reads of the
// pump's read size and one byte per read. For every segmentation the frames produced,
// the regions counted and the bytes discarded must equal the whole-feed result (C-1,
// State across feeds), the carry's resource must see no allocation during a feed
// (W-4), and the counted work must stay within C-1's bound after every feed
// (framer_test_access).
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/wire/framer.hpp>
#include <memory_resource>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "support/framer_test_access.hpp"
#include "support/pmr_allocation_tracking_resource.hpp"

namespace {

using fixpp::core::error;
using fixpp::test_support::pmr_allocation_tracking_resource;
using fixpp::wire::frame_view;
using fixpp::wire::Framer;
using fixpp::wire::framer_test_access;
using fixpp::wire::garble_summary;
using fixpp::wire::pmr_carry_buffer;

constexpr char soh = '\x01';

// R: the pump's per-read size, so the carry is L plus one read (data-model E-2).
// Re-derive from `read_buf` in run_read_pump (src/session/engine.cpp).
constexpr std::size_t kReadSize = 4096;

constexpr std::size_t kLargeL = 65536;
constexpr std::size_t kSmallL = 512;

// ── frame builders ──────────────────────────────────────────────────────────

[[nodiscard]] unsigned checksum_of(std::string_view bytes) {
    unsigned sum = 0;
    for (unsigned char ch : bytes) {
        sum += static_cast<unsigned>(ch);
    }
    return sum % 256U;
}

[[nodiscard]] std::string three_digits(unsigned value) {
    std::string out(3, '0');
    out[0] = static_cast<char>('0' + ((value / 100U) % 10U));
    out[1] = static_cast<char>('0' + ((value / 10U) % 10U));
    out[2] = static_cast<char>('0' + (value % 10U));
    return out;
}

struct frame_opts {
    std::string_view begin = "FIX.4.4";
    std::size_t body_length_width = 0;  // zero-pad BodyLength to this many digits
    bool bad_checksum = false;
};

[[nodiscard]] std::string make_frame(std::string_view body, frame_opts opts = {}) {
    std::string bl = std::to_string(body.size());
    if (bl.size() < opts.body_length_width) {
        bl.insert(0, opts.body_length_width - bl.size(), '0');
    }
    std::string frame = "8=" + std::string(opts.begin) + soh + "9=" + bl + soh + std::string(body);
    unsigned cs = checksum_of(frame);
    if (opts.bad_checksum) {
        cs = (cs + 1U) % 256U;
    }
    frame += "10=" + three_digits(cs) + soh;
    return frame;
}

[[nodiscard]] std::string heartbeat(unsigned seq) {
    return make_frame("35=0\x01"
                      "34=" +
                      std::to_string(seq) + "\x01");
}

// A well-formed frame of exactly `len` bytes (BeginString FIX.4.4), padding a 58 field.
// Its length is 10 ("8=FIX.4.4" SOH) + 3 + digits(body) ("9=", digits, SOH) + body + 7.
[[nodiscard]] std::string frame_of_length(std::size_t len, bool bad_checksum = false) {
    for (std::size_t digits = 1; digits <= 7; ++digits) {
        if (len < 20U + digits + 5U) {
            break;
        }
        std::size_t const body = len - 20U - digits;
        if (std::to_string(body).size() != digits) {
            continue;
        }
        std::string f =
            make_frame("58=" + std::string(body - 4U, 'x') + soh, {.bad_checksum = bad_checksum});
        EXPECT_EQ(f.size(), len);
        return f;
    }
    ADD_FAILURE() << "no frame of length " << len;
    return {};
}

// "8=FIX.4.4<SOH>9=<body_len><SOH>": a candidate header announcing body_len bytes.
[[nodiscard]] std::string candidate_header(std::size_t body_len) {
    return std::string("8=FIX.4.4") + soh + "9=" + std::to_string(body_len) + soh;
}

// ── the pump-shaped driver ──────────────────────────────────────────────────

struct call_record {
    garble_summary summary{};
    std::size_t produced = 0;
};

struct run_result {
    std::vector<std::string> frames;
    std::vector<call_record> calls;
    std::uint64_t regions = 0;
    std::uint64_t discarded = 0;
    std::vector<error> kinds;  // first_kind of each call that opened a region
    std::optional<error> failure;
    std::size_t received = 0;
    std::size_t pending_after = 0;
    std::size_t carry_allocations_during_feeds = 0;
    bool bound_held = true;
    std::string bound_violation;
    bool terminated = true;
};

struct run_opts {
    std::size_t limit = kLargeL;
    std::optional<std::size_t> max_begin_string_bytes;
    std::size_t out_slots = 1;
};

[[nodiscard]] Framer::Config resync_config(run_opts const& o) {
    Framer::Config cfg{.max_frame_bytes = o.limit, .resync_on_garble = true};
    if (o.max_begin_string_bytes) {
        cfg.max_begin_string_bytes = *o.max_begin_string_bytes;
    }
    return cfg;
}

// Feeds `stream` in reads of the given sizes, draining after each read.
[[nodiscard]] run_result run(std::string_view stream, std::vector<std::size_t> const& reads,
                             run_opts const& o = {}) {
    run_result res;
    pmr_allocation_tracking_resource tracker{std::pmr::new_delete_resource()};
    Framer framer{resync_config(o)};
    pmr_carry_buffer carry{o.limit + kReadSize, &tracker};
    std::size_t const allocations_at_construction = tracker.allocate_calls();
    std::vector<frame_view> out(o.out_slots);
    std::size_t const max_calls = (8U * stream.size()) + 64U;

    auto call = [&](std::span<const std::byte> incoming) -> bool {
        auto r = framer.feed(incoming, carry, out);
        call_record rec{.summary = framer.last_garbles(), .produced = r ? r->size() : 0U};
        res.calls.push_back(rec);
        res.regions += rec.summary.regions;
        res.discarded += rec.summary.discarded;
        if (rec.summary.regions > 0U) {
            res.kinds.push_back(rec.summary.first_kind);
        }
        if (!framer_test_access::within_work_bound(framer, res.received, o.limit, kReadSize) &&
            res.bound_held) {
            res.bound_held = false;
            std::ostringstream msg;
            msg << "work " << framer_test_access::total_work(framer) << " over the bound after "
                << res.received << " received bytes, call " << res.calls.size();
            res.bound_violation = msg.str();
        }
        if (!r) {
            res.failure = r.error();
            return false;
        }
        for (frame_view const& fv : *r) {
            auto const bytes = fv.bytes();
            res.frames.emplace_back(reinterpret_cast<char const*>(bytes.data()), bytes.size());
        }
        // Drain while a call produced frames: with one slot that is the pump's
        // `produced == out.size()`, and with more it reaches a call that stopped
        // before a later garble (C-1, Ordering).
        return rec.produced > 0U;
    };

    std::size_t pos = 0;
    for (std::size_t const n : reads) {
        auto const incoming = std::as_bytes(std::span<const char>{stream.data() + pos, n});
        pos += n;
        res.received += n;
        bool more = call(incoming);
        while (more && !res.failure) {
            if (res.calls.size() > max_calls) {
                res.terminated = false;
                break;
            }
            more = call({});
        }
        if (res.failure || !res.terminated) {
            break;
        }
    }
    res.pending_after = framer.pending_bytes();
    res.carry_allocations_during_feeds = tracker.allocate_calls() - allocations_at_construction;
    return res;
}

[[nodiscard]] std::vector<std::size_t> whole(std::size_t n) { return {n}; }

[[nodiscard]] std::vector<std::size_t> split_at(std::size_t n, std::size_t k) { return {k, n - k}; }

[[nodiscard]] std::vector<std::size_t> reads_of(std::size_t n, std::size_t step) {
    std::vector<std::size_t> out;
    for (std::size_t done = 0; done < n; done += step) {
        out.push_back(std::min(step, n - done));
    }
    return out;
}

void expect_run_invariants(run_result const& r, std::string const& label) {
    EXPECT_TRUE(r.terminated) << label << ": the feeds did not terminate";
    EXPECT_TRUE(r.bound_held) << label << ": " << r.bound_violation;
    EXPECT_EQ(r.carry_allocations_during_feeds, 0U) << label << ": a feed allocated (W-4)";
}

void expect_same_outcome(run_result const& r, run_result const& ref, std::string const& label) {
    EXPECT_EQ(r.frames, ref.frames) << label;
    EXPECT_EQ(r.regions, ref.regions) << label;
    EXPECT_EQ(r.discarded, ref.discarded) << label;
    EXPECT_EQ(r.failure, ref.failure) << label;
    ASSERT_FALSE(r.kinds.empty() != ref.kinds.empty()) << label;
    if (!ref.kinds.empty()) {
        EXPECT_EQ(r.kinds.front(), ref.kinds.front()) << label;
    }
}

// Whole, every split point (for streams up to one read), and one byte per read; or,
// for larger streams, reads of the pump's size and one byte per read. Each segmentation
// must reproduce the first run's outcome. Returns that first run.
run_result run_every_segmentation(std::string_view stream, run_opts const& o = {}) {
    run_result const ref = stream.size() <= kReadSize ? run(stream, whole(stream.size()), o)
                                                      : run(stream, reads_of(stream.size(), kReadSize), o);
    expect_run_invariants(ref, "first run");
    if (stream.size() <= kReadSize) {
        for (std::size_t k = 1; k < stream.size(); ++k) {
            run_result const r = run(stream, split_at(stream.size(), k), o);
            std::string const label = "split at " + std::to_string(k);
            expect_run_invariants(r, label);
            expect_same_outcome(r, ref, label);
        }
    }
    run_result const bytewise = run(stream, reads_of(stream.size(), 1), o);
    expect_run_invariants(bytewise, "one byte per read");
    expect_same_outcome(bytewise, ref, "one byte per read");
    return ref;
}

// ── the configuration this file relies on ───────────────────────────────────

TEST(FramerResync, ConfigDefaultsAreStrictAndCapTheLongestProfileIdentifier) {
    Framer::Config const cfg{};
    EXPECT_FALSE(cfg.resync_on_garble);
    // The longest supported profile identifier (FIX.4.0 to FIX.4.4, FIXT.1.1), spelled
    // here rather than read from the Framer's own list.
    EXPECT_EQ(cfg.max_begin_string_bytes, std::string_view{"FIXT.1.1"}.size());
}

// ── Q-2: resync recovery ────────────────────────────────────────────────────

TEST(FramerResync, Q2_GarbageNotEndingInSohBetweenGoodFrames) {
    for (std::size_t const limit : {kLargeL, kSmallL}) {
        SCOPED_TRACE(limit);
        std::string const g1 = heartbeat(1);
        std::string const g2 = heartbeat(2);
        run_result const r = run_every_segmentation(g1 + "XYZ" + g2, {.limit = limit});
        EXPECT_EQ(r.frames, (std::vector<std::string>{g1, g2}));
        EXPECT_EQ(r.regions, 1U);
        EXPECT_EQ(r.discarded, 3U);
        EXPECT_EQ(r.kinds, (std::vector<error>{error::wire_framing_resync}));
        EXPECT_FALSE(r.failure.has_value());
    }
}

TEST(FramerResync, Q2_TruncatedFrameThenGoodFrame) {
    for (std::size_t const limit : {kLargeL, kSmallL}) {
        SCOPED_TRACE(limit);
        std::string const g1 = heartbeat(1);
        std::string const truncated = g1.substr(0, g1.size() - 5U);  // ends in a value byte
        std::string const g2 = heartbeat(2);
        run_result const r = run_every_segmentation(truncated + g2, {.limit = limit});
        EXPECT_EQ(r.frames, (std::vector<std::string>{g2}));
        EXPECT_EQ(r.regions, 1U);
        EXPECT_EQ(r.discarded, truncated.size());
        EXPECT_EQ(r.kinds, (std::vector<error>{error::wire_invalid_body_length}));
    }
}

TEST(FramerResync, Q2_GarbageOnlyBufferIsConsumedInFiniteSteps) {
    for (std::size_t const limit : {kLargeL, kSmallL}) {
        SCOPED_TRACE(limit);
        // Junk with every proper prefix of "8=FIX" in it but never "8=FIX", ending in
        // the longest such prefix, which is all the carry may keep.
        std::string junk = "Q";
        while (junk.size() < 2000U) {
            junk += "ab8c8=d8=Fe8=FIf";
        }
        junk += "8=FI";
        run_result const r = run_every_segmentation(junk, {.limit = limit});
        EXPECT_TRUE(r.frames.empty());
        EXPECT_EQ(r.regions, 1U);
        EXPECT_EQ(r.discarded, junk.size() - 4U);
        EXPECT_EQ(r.pending_after, 4U);
        EXPECT_FALSE(r.failure.has_value());
    }
}

// ── Q-3: resync extent ──────────────────────────────────────────────────────

TEST(FramerResync, Q3_WrongChecksumFrameIsOneGarbleThroughItsOwnEnd) {
    for (std::size_t const limit : {kLargeL, kSmallL}) {
        SCOPED_TRACE(limit);
        std::string const inner = heartbeat(1);
        std::string const outer =
            make_frame("35=0\x01"
                       "58=" +
                           inner + soh,
                       {.bad_checksum = true});
        std::string const g2 = heartbeat(2);
        run_result const r = run_every_segmentation(outer + g2, {.limit = limit});
        EXPECT_EQ(r.frames, (std::vector<std::string>{g2})) << "the inner frame is part of the garble (L-15)";
        EXPECT_EQ(r.regions, 1U);
        EXPECT_EQ(r.discarded, outer.size());
        EXPECT_EQ(r.kinds, (std::vector<error>{error::wire_checksum_mismatch}));
    }
}

TEST(FramerResync, Q3_WellFormedFrameAfterAMalformedCandidateIsFramed) {
    for (std::size_t const limit : {kLargeL, kSmallL}) {
        SCOPED_TRACE(limit);
        std::string const head = candidate_header(5);  // its counted offset lands inside g1
        std::string const g1 = heartbeat(1);
        ASSERT_NE((head + g1).substr(head.size() + 5U, 3U), "10=");
        run_result const r = run_every_segmentation(head + g1, {.limit = limit});
        EXPECT_EQ(r.frames, (std::vector<std::string>{g1})) << "L-8";
        EXPECT_EQ(r.regions, 1U);
        EXPECT_EQ(r.discarded, head.size());
        EXPECT_EQ(r.kinds, (std::vector<error>{error::wire_invalid_body_length}));
    }
}

// ── Q-4: bounded work (L = 65536; the bound is checked after every feed) ────

// The candidate's counted offset (where its "10=" should start) for a header of
// `header_len` bytes announcing `body_len`.
[[nodiscard]] std::size_t counted_offset(std::size_t header_len, std::size_t body_len) {
    return header_len + body_len;
}

TEST(FramerResync, Q4_SohTriplesBehindAFailedLargeCandidate) {
    std::string const head = candidate_header(60000);
    std::string stream = head;
    while (stream.size() < counted_offset(head.size(), 60000) + 7U) {
        stream += "8=";
        stream += soh;
    }
    ASSERT_NE(stream.substr(counted_offset(head.size(), 60000), 3U), "10=");
    std::size_t const garbage = stream.size();
    std::string const last = heartbeat(1);
    stream += last;

    run_result const r = run_every_segmentation(stream);
    EXPECT_EQ(r.frames, (std::vector<std::string>{last}));
    EXPECT_EQ(r.regions, 1U);
    EXPECT_EQ(r.discarded, garbage);
    EXPECT_EQ(r.kinds, (std::vector<error>{error::wire_invalid_body_length}));
}

TEST(FramerResync, Q4_SmallFramesBehindAFailedLargeCandidateAreDrainedOnePerFeed) {
    std::string const head = candidate_header(60000);
    std::string stream = head;
    std::vector<std::string> small;
    unsigned seq = 1;
    while (stream.size() < kLargeL - 64U) {
        small.push_back(heartbeat(seq++));
        stream += small.back();
    }
    ASSERT_NE(stream.substr(counted_offset(head.size(), 60000), 3U), "10=");

    run_result const r = run_every_segmentation(stream);
    EXPECT_EQ(r.frames, small);
    EXPECT_EQ(r.regions, 1U);
    EXPECT_EQ(r.discarded, head.size());
}

TEST(FramerResync, Q4_StaggeredNestedCandidatesSharingOneChecksumField) {
    // Candidates at 18-byte steps, each announcing a BodyLength that ends at the same
    // "10=", followed by filler and that one CheckSum field (wrong for the outermost).
    constexpr std::size_t kCandidates = 2500;
    constexpr std::size_t kChecksumAt = 60000;
    std::string stream;
    for (std::size_t i = 0; i < kCandidates; ++i) {
        std::size_t const body_off = stream.size() + candidate_header(10000).size();
        stream += candidate_header(kChecksumAt - body_off);
    }
    ASSERT_EQ(stream.size(), kCandidates * candidate_header(10000).size());
    stream += std::string(kChecksumAt - 1U - stream.size(), 'A');
    stream += soh;
    unsigned const outer_sum = checksum_of(stream);
    stream += "10=" + three_digits((outer_sum + 1U) % 256U) + soh;
    std::size_t const garbage = stream.size();
    std::string const last = heartbeat(1);
    stream += last;

    run_result const r = run_every_segmentation(stream);
    EXPECT_EQ(r.frames, (std::vector<std::string>{last}));
    EXPECT_EQ(r.regions, 1U);
    EXPECT_EQ(r.discarded, garbage);
    EXPECT_EQ(r.kinds, (std::vector<error>{error::wire_checksum_mismatch}));
}

TEST(FramerResync, Q4_NestedCandidatesResolvedOnePerByte) {
    // research R-2: candidates at 18-byte steps whose counted ends are consecutive
    // bytes, so each arriving byte fails one candidate and leaves the next, about L
    // bytes long, pending at a non-zero offset in the carry.
    constexpr std::size_t kCandidates = 2500;
    constexpr std::size_t kFirstEnd = 60000;  // the first candidate needs this many bytes
    std::string stream;
    for (std::size_t i = 0; i < kCandidates; ++i) {
        std::size_t const header = candidate_header(10000).size();
        std::size_t const body_off = stream.size() + header;
        std::size_t const checksum_off = kFirstEnd + i - 7U;
        stream += candidate_header(checksum_off - body_off);
    }
    stream += std::string(kFirstEnd + kCandidates + 8U - stream.size(), 'A');
    std::size_t const garbage = stream.size();
    std::string const last = heartbeat(1);
    stream += last;

    run_result const r = run_every_segmentation(stream);
    EXPECT_EQ(r.frames, (std::vector<std::string>{last}));
    EXPECT_EQ(r.regions, kCandidates) << "each failed candidate is its own region";
    EXPECT_EQ(r.discarded, garbage);
}

TEST(FramerResync, Q4_EightEqualsFixRepeatedWithNoSoh) {
    std::string stream;
    while (stream.size() < 60000U) {
        stream += "8=FIX";
    }
    std::size_t const garbage = stream.size();
    std::string const last = heartbeat(1);
    stream += last;

    run_result const r = run_every_segmentation(stream);
    EXPECT_EQ(r.frames, (std::vector<std::string>{last}));
    EXPECT_EQ(r.regions, garbage / 5U) << "each 8=FIX is a failed candidate";
    EXPECT_EQ(r.discarded, garbage);
}

TEST(FramerResync, Q4_BoundaryEightEqualsWithNoSoh) {
    std::string stream = "8=" + std::string(60000, 'A');
    std::size_t const garbage = stream.size();
    std::string const last = heartbeat(1);
    stream += last;

    run_result const r = run_every_segmentation(stream);
    EXPECT_EQ(r.frames, (std::vector<std::string>{last}));
    EXPECT_EQ(r.regions, 1U);
    EXPECT_EQ(r.discarded, garbage);
    EXPECT_EQ(r.kinds, (std::vector<error>{error::wire_framing_resync}));
}

TEST(FramerResync, Q4_RunOfZerosAfterBodyLengthTagWithNoSoh) {
    std::string stream = std::string("8=FIX.4.4") + soh + "9=" + std::string(60000, '0');
    std::size_t const garbage = stream.size();
    std::string const last = heartbeat(1);
    stream += last;

    run_result const r = run_every_segmentation(stream);
    EXPECT_EQ(r.frames, (std::vector<std::string>{last}));
    EXPECT_EQ(r.regions, 1U);
    EXPECT_EQ(r.discarded, garbage);
    EXPECT_EQ(r.kinds, (std::vector<error>{error::wire_invalid_body_length}));
}

TEST(FramerResync, Q4_BodyLengthZeroPaddedToTheDigitCapIsFramedAndOneMoreIsAGarble) {
    std::string const body = "35=0\x01";
    std::string const at_cap = make_frame(body, {.body_length_width = Framer::kBodyLengthDigitCap});
    std::string const over_cap =
        make_frame(body, {.body_length_width = Framer::kBodyLengthDigitCap + 1U});
    std::string const g2 = heartbeat(2);

    run_result const framed = run_every_segmentation(at_cap + g2);
    EXPECT_EQ(framed.frames, (std::vector<std::string>{at_cap, g2}));
    EXPECT_EQ(framed.regions, 0U);

    run_result const garbled = run_every_segmentation(over_cap + g2);
    EXPECT_EQ(garbled.frames, (std::vector<std::string>{g2}));
    EXPECT_EQ(garbled.regions, 1U);
    EXPECT_EQ(garbled.discarded, over_cap.size());
    EXPECT_EQ(garbled.kinds, (std::vector<error>{error::wire_invalid_body_length}));
}

// ── Q-5 (Framer half): accounting across feeds ──────────────────────────────

TEST(FramerResync, Q5_RegionSplitAcrossFeedsIsCountedOnce) {
    std::string const junk(100, 'J');
    std::string const g1 = heartbeat(1);
    run_result const r = run(junk + g1, {50, 50 + g1.size()});
    ASSERT_GE(r.calls.size(), 2U);
    EXPECT_EQ(r.calls[0].summary.regions, 1U);
    EXPECT_EQ(r.calls[0].summary.first_kind, error::wire_framing_resync);
    EXPECT_EQ(r.calls[0].summary.discarded, 50U);
    EXPECT_EQ(r.calls[1].summary.regions, 0U) << "a continued region is not counted again";
    EXPECT_EQ(r.calls[1].summary.discarded, 50U) << "but its bytes are";
    EXPECT_EQ(r.frames, (std::vector<std::string>{g1}));
}

TEST(FramerResync, Q5_GarblesReportedByACallPrecedeEveryFrameItProduces) {
    std::string const g1 = heartbeat(1);
    std::string const g2 = heartbeat(2);
    std::string const stream = "XYZ" + g1 + "QQ" + g2;
    run_result const r = run(stream, whole(stream.size()), {.out_slots = 4});
    ASSERT_GE(r.calls.size(), 2U);
    EXPECT_EQ(r.calls[0].summary.regions, 1U);
    EXPECT_EQ(r.calls[0].summary.discarded, 3U);
    EXPECT_EQ(r.calls[0].produced, 1U) << "the call stops before resolving the later garble";
    EXPECT_EQ(r.calls[1].summary.regions, 1U);
    EXPECT_EQ(r.calls[1].summary.discarded, 2U);
    EXPECT_EQ(r.calls[1].produced, 1U);
    EXPECT_EQ(r.frames, (std::vector<std::string>{g1, g2}));
}

TEST(FramerResync, Q5_TwoAdjacentFailedCandidatesCountTwo) {
    std::string const bad1 = std::string("8=FIX.4.4") + soh + "9=1X" + soh;
    std::string const bad2 = std::string("8=FIX.4.4") + soh + "9=2Y" + soh;
    std::string const g1 = heartbeat(1);
    run_result const r = run_every_segmentation(bad1 + bad2 + g1);
    EXPECT_EQ(r.frames, (std::vector<std::string>{g1}));
    EXPECT_EQ(r.regions, 2U);
    EXPECT_EQ(r.discarded, bad1.size() + bad2.size());
    ASSERT_FALSE(r.kinds.empty());
    EXPECT_EQ(r.kinds.front(), error::wire_invalid_body_length);
}

TEST(FramerResync, Q5_WrongChecksumThenJunkThenGoodCountsOneRegion) {
    std::string const wrong = make_frame("35=0\x01", {.bad_checksum = true});
    std::string const g1 = heartbeat(1);
    run_result const r = run_every_segmentation(wrong + "JUNK" + g1);
    EXPECT_EQ(r.frames, (std::vector<std::string>{g1}));
    EXPECT_EQ(r.regions, 1U) << "the byte after a wrong-CheckSum frame is a search position";
    EXPECT_EQ(r.discarded, wrong.size() + 4U);
    EXPECT_EQ(r.kinds, (std::vector<error>{error::wire_checksum_mismatch}));
}

// ── Q-6 (Framer half): over L closes, whatever the CheckSum ─────────────────

TEST(FramerResync, Q6_FrameOfExactlyLIsFramedAndLPlusOneIsTooLarge) {
    for (std::size_t const limit : {kLargeL, kSmallL}) {
        SCOPED_TRACE(limit);
        std::string const at_limit = frame_of_length(limit);
        run_result const ok = run_every_segmentation(at_limit, {.limit = limit});
        EXPECT_EQ(ok.frames, (std::vector<std::string>{at_limit}));
        EXPECT_FALSE(ok.failure.has_value());

        for (bool const bad : {false, true}) {
            SCOPED_TRACE(bad ? "bad CheckSum" : "good CheckSum");
            std::string const over = frame_of_length(limit + 1U, bad);
            run_result const r = run_every_segmentation(over, {.limit = limit});
            EXPECT_TRUE(r.frames.empty());
            EXPECT_EQ(r.failure, error::wire_frame_too_large);
            EXPECT_EQ(r.regions, 0U) << "an over-L frame is never disregarded as a garble";
        }
    }
}

TEST(FramerResync, Q6_OverLIsRefusedAsSoonAsTheBodyOffsetIsKnown) {
    for (std::size_t const limit : {kLargeL, kSmallL}) {
        SCOPED_TRACE(limit);
        // BodyLength within L, frame length over L, and no body bytes sent at all.
        std::string const head = candidate_header(limit - 5U);
        run_result const r = run_every_segmentation(head, {.limit = limit});
        EXPECT_EQ(r.failure, error::wire_frame_too_large);
    }
}

TEST(FramerResync, Q6_OverLBodyLengthAtACandidateTheSearchFoundIsTooLarge) {
    for (std::size_t const limit : {kLargeL, kSmallL}) {
        SCOPED_TRACE(limit);
        std::string const stream = "XYZ" + candidate_header(limit + 1U) + "35=0" + soh;
        run_result const r = run_every_segmentation(stream, {.limit = limit});
        EXPECT_TRUE(r.frames.empty());
        EXPECT_EQ(r.failure, error::wire_frame_too_large) << "FR-013 prevails over FR-001";
        EXPECT_EQ(r.regions, 1U) << "the leading junk was a garble";
    }
}

TEST(FramerResync, OD20_AFrameBeforeAnOverLCandidateIsDeliveredBeforeTheRefusal) {
    // Plan OD-20: a call that produced a frame stops at the next candidate whatever
    // its outcome, so the frame ahead of an over-L BodyLength in the same read is
    // delivered, and the next call returns wire_frame_too_large.
    for (std::size_t const limit : {kLargeL, kSmallL}) {
        SCOPED_TRACE(limit);
        std::string const g1 = heartbeat(1);
        std::string const stream = g1 + candidate_header(limit + 1U) + "35=0" + soh;
        pmr_allocation_tracking_resource tracker{std::pmr::new_delete_resource()};
        Framer framer{Framer::Config{.max_frame_bytes = limit, .resync_on_garble = true}};
        pmr_carry_buffer carry{limit + kReadSize, &tracker};
        std::vector<frame_view> out(4);

        auto first = framer.feed(std::as_bytes(std::span<const char>{stream.data(), stream.size()}),
                                 carry, out);
        ASSERT_TRUE(first.has_value()) << "the produced frame is not discarded by the refusal";
        ASSERT_EQ(first->size(), 1U);
        auto const bytes = (*first)[0].bytes();
        EXPECT_EQ(std::string(reinterpret_cast<char const*>(bytes.data()), bytes.size()), g1);

        auto second = framer.feed({}, carry, out);
        ASSERT_FALSE(second.has_value());
        EXPECT_EQ(second.error(), error::wire_frame_too_large);
    }
}

// ── Q-10 (Framer half): the BeginString cap ─────────────────────────────────

TEST(FramerResync, Q10_BeginStringLongerThanTheCapWithNoSohIsAGarble) {
    for (std::size_t const limit : {kLargeL, kSmallL}) {
        SCOPED_TRACE(limit);
        Framer::Config const defaults{};
        std::string const long_value(defaults.max_begin_string_bytes + 1U, 'F');
        std::string const head = "8=" + long_value;
        std::string const g1 = heartbeat(1);
        run_result const r = run_every_segmentation(head + g1, {.limit = limit});
        EXPECT_EQ(r.frames, (std::vector<std::string>{g1}));
        EXPECT_EQ(r.regions, 1U);
        EXPECT_EQ(r.discarded, head.size());
        EXPECT_EQ(r.kinds, (std::vector<error>{error::wire_framing_resync}));
    }
}

TEST(FramerResync, Q10_BeginStringWithinTheCapIsFramedWhateverItsValue) {
    // A mismatched but short BeginString is the session's to judge, not the Framer's.
    std::string const other = make_frame("35=0\x01", {.begin = "FIX.4.2"});
    run_result const r = run_every_segmentation(other);
    EXPECT_EQ(r.frames, (std::vector<std::string>{other}));
    EXPECT_EQ(r.regions, 0U);
}

TEST(FramerResync, Q10_AConfiguredLongCapFramesItsOwnLongBeginString) {
    std::string_view const long_begin = "FIX.4.4.CONFIGURED";
    std::string const own = make_frame("35=0\x01", {.begin = long_begin});
    run_result const r = run_every_segmentation(own, {.max_begin_string_bytes = long_begin.size()});
    EXPECT_EQ(r.frames, (std::vector<std::string>{own}));
    EXPECT_EQ(r.regions, 0U);

    run_result const capped = run(own, whole(own.size()));
    EXPECT_TRUE(capped.frames.empty()) << "under the default cap the same frame is a garble";
    EXPECT_EQ(capped.regions, 1U);
}

TEST(FramerResync, Q10_ANonFixBeginStringIsFramedAtABoundaryButNotFoundAfterAGarble) {
    std::string const non_fix = make_frame("35=0\x01", {.begin = "ABC.1.0"});
    run_result const at_boundary = run_every_segmentation(non_fix);
    EXPECT_EQ(at_boundary.frames, (std::vector<std::string>{non_fix}));

    // L-16: after a garble the search looks for 8=FIX only.
    run_result const after_garble = run_every_segmentation("XYZ" + non_fix);
    EXPECT_TRUE(after_garble.frames.empty());
    EXPECT_EQ(after_garble.regions, 1U);
    EXPECT_EQ(after_garble.discarded, 3U + non_fix.size());
}

}  // namespace
