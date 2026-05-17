// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/round_trip_property_test.cpp — T033 (US2, seam #3).
// Structural round-trip property test:
//   parse a frame into MessageView<Iter> → iterate ALL fields in document order
//   (including opaque/unknown fields) → re-emit each via Writer::append_raw →
//   Writer::commit → re-parse → assert byte-identical.
//
// Uses MessageView<Iter>::field_iterator (the dict-free streaming path) so
// every on-wire field — including custom/opaque ones — is preserved in document
// order without a dictionary. This is the structural seam #3 round-trip; it
// MUST NOT use dict::reify_as<Msg> (T059 scope, not US2).
//
// Authored RED (T033) — must fail/not link before Writer lands (T034/T035).
// Goes GREEN at T036.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <fixpp/core/decimal_alias.hpp>  // decimal_t (append<T> coverage)
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/writer.hpp>

#include "support/frame_view_factory.hpp"
#include "support/mock_dict_table.hpp"

namespace {

using fixpp::wire::access_mode;
using fixpp::wire::MessageView;
using fixpp::wire::Parser;
using fixpp::wire::Writer;

// ── Helpers ──────────────────────────────────────────────────────────────────

// Tags the Writer auto-computes; skip them when re-emitting via Writer.
// tag=9 (BodyLength) is injected by the Writer after the 8= field.
// tag=10 (CheckSum) is appended by commit().
static bool is_auto_framing_tag(std::uint16_t tag) noexcept {
    return tag == 9 || tag == 10;
}

// Build a single well-formed FIX 4.4 frame from the body fields (the span
// between 9=...\x01 and 10=...\x01).
std::vector<std::byte> make_frame(std::string_view body_fields) {
    std::string body{body_fields};
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string pre = "8=FIX.4.4\x01" + nine + body;
    unsigned sum = 0;
    for (unsigned char c : pre) {
        sum += c;
    }
    char chk[8];
    std::snprintf(chk, sizeof(chk), "10=%03u\x01", sum % 256U);
    std::string full = pre + chk;
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

// Parse the given byte buffer into MessageView<Iter> so we can walk fields.
// Returns the MessageView on success or fails the test.
MessageView<access_mode::Iter>
parse_iter(std::span<const std::byte> buf) {
    auto fv = fixpp::wire::test::make_frame_view(buf);
    if (!fv.has_value()) {
        ADD_FAILURE() << "make_frame_view failed: " << static_cast<int>(fv.error());
        return MessageView<access_mode::Iter>{};
    }
    Parser<access_mode::Iter> parser{};
    auto mv = parser.parse_iter(*fv);
    if (!mv.has_value()) {
        ADD_FAILURE() << "parse_iter failed: " << static_cast<int>(mv.error());
        return MessageView<access_mode::Iter>{};
    }
    return *mv;
}

// Structural round-trip: parse `original` → iterate all fields → re-emit
// via Writer → commit → return the committed bytes.
// `scratch_buf` is the destination buffer for the Writer.
//
// The Writer automatically injects 9=BodyLength after the 8= field and appends
// 10=CheckSum at commit(), so we skip those when iterating (they are framing
// fields that the Writer recomputes). All other fields (including opaque/custom
// tags) are written verbatim in document order.
std::vector<std::byte> structural_round_trip(std::span<const std::byte> original,
                                              std::vector<std::byte>& scratch_buf) {
    auto mv = parse_iter(original);

    // The Writer injects a 6-digit placeholder for 9= after the 8= field.
    // The real body_length will have fewer digits, so the memmove closes the gap.
    // Allocate enough space: original.size() + 6 (max placeholder overshoot).
    constexpr std::size_t max_bl_placeholder_overhead = 6;
    scratch_buf.assign(original.size() + max_bl_placeholder_overhead, std::byte{0});

    std::pmr::monotonic_buffer_resource scratch_mr;
    Writer writer{std::span<std::byte>{scratch_buf.data(), scratch_buf.size()},
                  &scratch_mr};

    // Iterate ALL fields in document order (dict-free Iter path), skipping
    // the auto-framing fields (9=BodyLength and 10=CheckSum) that the Writer
    // recomputes at commit().
    for (auto it = mv.begin(); !(it == mv.end()); ++it) {
        auto const& f = *it;
        if (is_auto_framing_tag(f.tag)) {
            continue;  // Writer handles tag=9 and tag=10 automatically.
        }
        auto rc = writer.append_raw(f.tag, f.value);
        if (!rc.has_value()) {
            ADD_FAILURE() << "append_raw failed for tag=" << f.tag
                          << " err=" << static_cast<int>(rc.error());
            return {};
        }
    }

    auto result = std::move(writer).commit();
    if (!result.has_value()) {
        ADD_FAILURE() << "commit() failed: " << static_cast<int>(result.error());
        return {};
    }
    std::size_t written = *result;
    scratch_buf.resize(written);
    return scratch_buf;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

// Helper: write all non-auto-framing fields from `mv` into `writer`.
// Skips tag=9 (BodyLength) and tag=10 (CheckSum) which Writer recomputes.
template <access_mode Mode>
void emit_fields(MessageView<Mode> const& mv, Writer& writer) {
    for (auto it = mv.begin(); !(it == mv.end()); ++it) {
        auto const& f = *it;
        if (is_auto_framing_tag(f.tag)) {
            continue;
        }
        ASSERT_TRUE(writer.append_raw(f.tag, f.value).has_value())
            << "append_raw failed for tag=" << f.tag;
    }
}

// Golden frame test: Writer → commit must be byte-identical to a known-good
// golden frame authored in the W-013 conformance corpus.
TEST(RoundTripProperty, GoldenFrameHeartbeat) {
    // Golden: 8=FIX.4.4\x01 9=10\x01 35=0\x01 34=1\x01 10=165\x01
    // Build it via make_frame and assert Writer reproduces it.
    auto golden = make_frame("35=0\x01" "34=1\x01");

    // +6 for the placeholder overhead (6-digit 9= that gets memmoved away).
    std::vector<std::byte> scratch(golden.size() + 6);
    std::pmr::monotonic_buffer_resource scratch_mr;
    Writer writer{std::span<std::byte>{scratch.data(), scratch.size()}, &scratch_mr};

    auto fv = fixpp::wire::test::make_frame_view(golden);
    ASSERT_TRUE(fv.has_value());
    Parser<access_mode::Iter> parser{};
    auto mv = parser.parse_iter(*fv);
    ASSERT_TRUE(mv.has_value());

    emit_fields(*mv, writer);

    auto result = std::move(writer).commit();
    ASSERT_TRUE(result.has_value()) << "commit() failed: "
                                    << static_cast<int>(result.error());
    std::size_t written = *result;
    ASSERT_EQ(written, golden.size()) << "committed size mismatch";

    // Byte-identical comparison.
    EXPECT_EQ(std::memcmp(scratch.data(), golden.data(), written), 0)
        << "Writer output is not byte-identical to the golden frame";
}

TEST(RoundTripProperty, GoldenFrameNewOrderSingle) {
    auto golden = make_frame("35=D\x01" "34=1\x01" "49=SENDER\x01" "56=TARGET\x01");

    std::vector<std::byte> scratch(golden.size() + 6);
    std::pmr::monotonic_buffer_resource scratch_mr;
    Writer writer{std::span<std::byte>{scratch.data(), scratch.size()}, &scratch_mr};

    auto fv = fixpp::wire::test::make_frame_view(golden);
    ASSERT_TRUE(fv.has_value());
    Parser<access_mode::Iter> parser{};
    auto mv = parser.parse_iter(*fv);
    ASSERT_TRUE(mv.has_value());

    emit_fields(*mv, writer);
    auto result = std::move(writer).commit();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(*result, golden.size());
    EXPECT_EQ(std::memcmp(scratch.data(), golden.data(), *result), 0)
        << "byte-identical check failed";
}

// Structural round-trip: parse any message, re-emit, re-parse, fields match.
TEST(RoundTripProperty, StructuralRoundTripSimple) {
    auto original = make_frame("35=D\x01" "34=42\x01"
                                "49=ACME\x01" "56=BROKER\x01"
                                "11=ORD123\x01" "55=AAPL\x01");
    std::vector<std::byte> scratch_buf;
    auto round_tripped = structural_round_trip(original, scratch_buf);
    ASSERT_EQ(round_tripped.size(), original.size())
        << "round-trip size mismatch";
    EXPECT_EQ(std::memcmp(round_tripped.data(), original.data(), original.size()), 0)
        << "round-trip bytes not identical";
}

// Opaque/custom field preservation: any unknown high-numbered tags must survive
// the structural round-trip byte-identical.
TEST(RoundTripProperty, OpaqueCustomFieldPreservation) {
    // Tags 9999 and 9998 are "custom" / unknown to any standard dictionary.
    auto original = make_frame("35=0\x01" "34=1\x01"
                                "9999=custom_value\x01"
                                "9998=another\x01");
    std::vector<std::byte> scratch_buf;
    auto round_tripped = structural_round_trip(original, scratch_buf);
    ASSERT_EQ(round_tripped.size(), original.size());
    EXPECT_EQ(std::memcmp(round_tripped.data(), original.data(), original.size()), 0)
        << "custom field bytes not byte-identical after round-trip";
}

// Document-order preservation: multiple occurrences of the same tag must survive
// in original order.
TEST(RoundTripProperty, RepeatedTagOrderPreservation) {
    auto original = make_frame("35=D\x01" "34=1\x01"
                                "448=PARTY-A\x01"
                                "448=PARTY-B\x01"
                                "448=PARTY-C\x01");
    std::vector<std::byte> scratch_buf;
    auto round_tripped = structural_round_trip(original, scratch_buf);
    ASSERT_EQ(round_tripped.size(), original.size());
    EXPECT_EQ(std::memcmp(round_tripped.data(), original.data(), original.size()), 0)
        << "repeated-tag document order not preserved";
}

// Too-small buffer must return a defined error (no OOB write).
TEST(RoundTripProperty, TooSmallBufferReturnsError) {
    std::array<std::byte, 4> tiny_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr;
    Writer writer{std::span<std::byte>{tiny_buf.data(), tiny_buf.size()},
                  &scratch_mr};

    // Append a field that won't fit.
    std::string val = "FIX.4.4";
    std::vector<std::byte> vbytes(val.size());
    std::memcpy(vbytes.data(), val.data(), val.size());
    // Either append_raw or commit must return an error — not an OOB write.
    auto rc = writer.append_raw(8, std::span<const std::byte>{vbytes.data(), vbytes.size()});
    if (rc.has_value()) {
        // Buffer filled; commit should fail.
        auto cr = std::move(writer).commit();
        EXPECT_FALSE(cr.has_value()) << "commit to oversized message should fail";
    } else {
        EXPECT_FALSE(rc.has_value()) << "append to tiny buffer should fail";
    }
}

// 10^4-sample structural round-trip over a generated corpus of varied messages.
// Each sample: parse → iterate fields → re-emit → byte-identical.
TEST(RoundTripProperty, TenThousandSampleCorpus) {
    // Generate a varied corpus: different body lengths, different tag counts,
    // different values (including high-numbered "custom" tags).
    static const char* const msg_types[] = {"0", "D", "8", "A", "5"};
    static const std::uint16_t extra_tags[] = {49, 56, 34, 35, 11, 55, 448,
                                                9000, 9001, 9999};
    static const char* const extra_vals[] = {
        "SENDER", "TARGET", "1", "D", "ORD001", "AAPL",
        "PARTYID", "cust1", "cust2", "opaque"};
    static constexpr std::size_t n_extra =
        sizeof(extra_tags) / sizeof(extra_tags[0]);

    constexpr std::size_t N = 10000;
    std::size_t failures = 0;

    for (std::size_t i = 0; i < N; ++i) {
        // Build varied body from index-derived fields.
        std::string body;
        // MsgType (35) from corpus slot.
        body += "35=";
        body += msg_types[i % 5];
        body += '\x01';
        // MsgSeqNum (34).
        body += "34=" + std::to_string(i + 1) + "\x01";
        // Add varying number of extra fields (1..4 based on i).
        std::size_t nf = 1 + (i % 4);
        for (std::size_t j = 0; j < nf; ++j) {
            std::size_t slot = (i + j) % n_extra;
            body += std::to_string(extra_tags[slot]);
            body += '=';
            body += extra_vals[slot];
            body += '\x01';
        }

        auto original = make_frame(body);
        // +6 for the 6-digit BodyLength placeholder overshoot.
        std::vector<std::byte> scratch_buf(original.size() + 6);
        std::pmr::monotonic_buffer_resource scratch_mr;
        Writer writer{
            std::span<std::byte>{scratch_buf.data(), scratch_buf.size()},
            &scratch_mr};

        auto fv = fixpp::wire::test::make_frame_view(original);
        if (!fv.has_value()) {
            ++failures;
            continue;
        }
        Parser<access_mode::Iter> parser{};
        auto mv = parser.parse_iter(*fv);
        if (!mv.has_value()) {
            ++failures;
            continue;
        }

        bool ok = true;
        for (auto it = mv->begin(); !(it == mv->end()); ++it) {
            auto const& f = *it;
            if (is_auto_framing_tag(f.tag)) {
                continue;  // Writer handles 9= and 10= automatically.
            }
            if (!writer.append_raw(f.tag, f.value).has_value()) {
                ok = false;
                break;
            }
        }
        if (!ok) {
            ++failures;
            continue;
        }

        auto result = std::move(writer).commit();
        if (!result.has_value()) {
            ++failures;
            continue;
        }
        std::size_t written = *result;
        if (written != original.size()) {
            ++failures;
            continue;
        }
        if (std::memcmp(scratch_buf.data(), original.data(), written) != 0) {
            ++failures;
        }
    }

    EXPECT_EQ(failures, 0U)
        << failures << " of " << N << " round-trip samples failed";
}

// ── Coverage for the typed append<T> path (T034/T035 hardening) ───────────────
// append_raw is exercised everywhere above; append<T> is the 2a-decimal
// encode path (decimal<U>::format -> traits_type::to_chars). This pins that
// the bytes the Writer puts on the wire are EXACTLY what the 2a trait emits.

std::vector<std::byte> bytes_of(std::string_view s) {
    std::vector<std::byte> v(s.size());
    std::memcpy(v.data(), s.data(), s.size());
    return v;
}

TEST(RoundTripProperty, AppendTypedDecimalEmitsTraitBytes) {
    std::pmr::monotonic_buffer_resource arena;
    auto dbytes = bytes_of("1234.56");
    auto dec = fixpp::decimal_t::parse(
        std::span<const std::byte>{dbytes.data(), dbytes.size()}, &arena);
    ASSERT_TRUE(dec.has_value()) << "decimal_t::parse failed";

    // Independently compute what the 2a trait to_chars produces for this value.
    std::array<std::byte, 64> exp_buf{};
    auto exp_n = dec->format(std::span<std::byte>{exp_buf.data(), exp_buf.size()});
    ASSERT_TRUE(exp_n.has_value());
    std::string expected(*exp_n, '\0');
    std::memcpy(expected.data(), exp_buf.data(), *exp_n);

    std::array<std::byte, 256> scratch{};
    std::pmr::monotonic_buffer_resource scratch_mr;
    Writer writer{std::span<std::byte>{scratch.data(), scratch.size()}, &scratch_mr};
    auto bs = bytes_of("FIX.4.4");
    auto d = bytes_of("D");
    auto one = bytes_of("1");
    ASSERT_TRUE(writer.append_raw(8, std::span<const std::byte>{bs.data(), bs.size()}).has_value());
    ASSERT_TRUE(writer.append_raw(35, std::span<const std::byte>{d.data(), d.size()}).has_value());
    ASSERT_TRUE(writer.append_raw(34, std::span<const std::byte>{one.data(), one.size()}).has_value());
    // The typed path under test.
    ASSERT_TRUE(writer.append<fixpp::decimal_t>(44, *dec).has_value())
        << "Writer::append<decimal_t> (2a trait to_chars path) failed";
    auto committed = std::move(writer).commit();
    ASSERT_TRUE(committed.has_value());

    std::vector<std::byte> frame(scratch.data(), scratch.data() + *committed);
    auto fv = fixpp::wire::test::make_frame_view(frame);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource parse_mr;
    Parser<access_mode::Index> parser{};
    auto mv = parser.parse(*fv, &parse_mr);
    ASSERT_TRUE(mv.has_value());
    auto px = mv->template get<44>();
    ASSERT_TRUE(px.has_value());
    EXPECT_EQ(px->as_string(), expected)
        << "tag 44 on the wire must equal the 2a trait-encoded bytes";
}

// ── Coverage for open_group / group_writer (T035 hardening) ───────────────────
// No round-trip test above exercises the group path; this builds a frame
// containing a real FIX repeating group via open_group + group_writer and
// asserts it is byte-identical to a hand-built golden frame.

TEST(RoundTripProperty, OpenGroupProducesByteIdenticalRepeatingGroup) {
    // NoPartyIDs(453)=2 with two {PartyID(448), PartyRole(452)} entries.
    auto golden = make_frame("35=D\x01" "34=1\x01"
                             "453=2\x01"
                             "448=PARTYA\x01" "452=1\x01"
                             "448=PARTYB\x01" "452=2\x01");

    std::vector<std::byte> scratch(golden.size() + 6);
    std::pmr::monotonic_buffer_resource scratch_mr;
    Writer writer{std::span<std::byte>{scratch.data(), scratch.size()}, &scratch_mr};

    auto bs = bytes_of("FIX.4.4");
    auto d = bytes_of("D");
    auto one = bytes_of("1");
    ASSERT_TRUE(writer.append_raw(8, std::span<const std::byte>{bs.data(), bs.size()}).has_value());
    ASSERT_TRUE(writer.append_raw(35, std::span<const std::byte>{d.data(), d.size()}).has_value());
    ASSERT_TRUE(writer.append_raw(34, std::span<const std::byte>{one.data(), one.size()}).has_value());

    auto gw = writer.open_group(453, 2);
    ASSERT_TRUE(gw.has_value()) << "open_group failed";
    auto pa = bytes_of("PARTYA");
    auto pb = bytes_of("PARTYB");
    auto r1 = bytes_of("1");
    auto r2 = bytes_of("2");
    ASSERT_TRUE(gw->append_field(448, std::span<const std::byte>{pa.data(), pa.size()}).has_value());
    ASSERT_TRUE(gw->append_field(452, std::span<const std::byte>{r1.data(), r1.size()}).has_value());
    ASSERT_TRUE(gw->append_field(448, std::span<const std::byte>{pb.data(), pb.size()}).has_value());
    ASSERT_TRUE(gw->append_field(452, std::span<const std::byte>{r2.data(), r2.size()}).has_value());
    std::move(*gw).close();

    auto committed = std::move(writer).commit();
    ASSERT_TRUE(committed.has_value())
        << "commit() failed: " << static_cast<int>(committed.error());
    ASSERT_EQ(*committed, golden.size()) << "group frame size mismatch";
    EXPECT_EQ(std::memcmp(scratch.data(), golden.data(), *committed), 0)
        << "open_group/group_writer output not byte-identical to golden";
}

}  // namespace
