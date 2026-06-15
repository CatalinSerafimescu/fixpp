// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/wire/parser_overflow_test.cpp
//
// 040-inbound-tag-overflow-hardening Phase 4 US2 — T009/T012 (Scan/Iter site).
//
// Witnesses that field_iterator::advance (access_mode::Iter) terminates
// iteration at a forged wrap-and-continue tag token and never yields the
// field under the aliased tag number. Pre-fix the digit loop had no per-digit
// bound; a token like 429496729649 wraps uint32 to 49 and the field would be
// yielded as tag 49 by the iterator.
//
// Cells:
//   W1: token 429496729649 — iteration terminates at the forged field (done_=true);
//       tag 49 ("FORGE49") is NEVER yielded in the collected tag set.
//   W2: token 429496729634 — same; tag 34 ("42") is NEVER yielded.
//   W3: conforming frame — all fields yielded correctly (no regression).
//
// Iter leniency note (documented in parser_error_path_test.cpp): done_=true may
// fire mid-buffer without pos_ reaching buf_.size(), so range-for is bounded by
// a step count rather than relying on it == end() for corrupt streams. We collect
// all (tag, value) pairs up to a generous step limit and verify tag 49/34 absent.
//
// Anchors: research.md D-1/D-3/D-6; data-model.md SC-001; tasks.md T009/T012.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// seam: mock_dict_table must precede parser.hpp (single-definition rule).
// clang-format off
#include "support/mock_dict_table.hpp"
// clang-format on
#include <fixpp/wire/parser.hpp>

#include "support/frame_view_factory.hpp"

namespace {

using fixpp::wire::access_mode;
using fixpp::wire::MessageView;

// Build a minimal structurally-valid FIX frame. The body argument supplies the
// raw bytes between the 9= header and the 10= trailer; checksum correctness is
// not required for Iter iteration.
std::vector<std::byte> make_raw_frame(std::string const& body) {
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string full = "8=FIX.4.4\x01" + nine + body + "10=000\x01";
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

// Collect all (tag, value_string) pairs emitted by the Iter iterator, bounded
// to at most `max_steps` steps (guards against non-terminating-on-corrupt loops).
struct TagVal {
    std::uint32_t tag;
    std::string value;
};
std::vector<TagVal> collect_iter_fields(MessageView<access_mode::Iter>& mv,
                                        std::size_t max_steps = 64) {
    std::vector<TagVal> out;
    auto it = mv.begin();
    auto en = mv.end();
    for (std::size_t steps = 0; steps < max_steps && !(it == en); ++steps) {
        auto const& f = *it;
        out.push_back(
            {f.tag, std::string(reinterpret_cast<char const*>(f.value.data()), f.value.size())});
        ++it;
    }
    return out;
}

// ── W1: token 429496729649 must NOT yield tag 49 ────────────────────────────
// Iter leniency: done_=true fires at the forged field and stops the iterator;
// fields AFTER the forged token are not yielded. The conforming-pair assertion
// uses a SEPARATE frame (no forged field) to prove tag 49 IS yielded normally.
// Discrimination: pre-fix "FORGE49" would appear as tag 49; post-fix it doesn't.
TEST(ParserOverflow, ForgedTag429496729649_NeverYieldedAsTag49) {
    // Frame containing ONLY the forged field (plus framing tags).
    // Pre-fix: "FORGE49" yielded as tag 49. Post-fix: done_=true, never yielded.
    auto buf = make_raw_frame(
        "429496729649=FORGE49\x01"
        "35=A\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    MessageView<access_mode::Iter> mv{*fv};
    auto fields = collect_iter_fields(mv);

    // The forged value "FORGE49" must never appear with tag 49.
    bool found_forged = false;
    for (auto const& f : fields) {
        if (f.tag == 49 && f.value == "FORGE49") {
            found_forged = true;
        }
    }
    EXPECT_FALSE(found_forged)
        << "Forged token 429496729649=FORGE49 must NOT be yielded as tag 49 (SC-001)";

    // Conforming pair (separate frame): a real 49= field must be yielded.
    auto cbuf = make_raw_frame(
        "35=A\x01"
        "49=REAL\x01"
        "34=1\x01");
    auto cfv = fixpp::wire::test::make_frame_view(cbuf);
    ASSERT_TRUE(cfv.has_value());
    MessageView<access_mode::Iter> cmv{*cfv};
    auto cfields = collect_iter_fields(cmv);
    bool found_real49 = false;
    for (auto const& f : cfields) {
        if (f.tag == 49 && f.value == "REAL") {
            found_real49 = true;
        }
    }
    EXPECT_TRUE(found_real49)
        << "Conforming tag 49=REAL in a clean frame must be yielded (no regression)";
}

// ── W2: token 429496729634 must NOT yield tag 34 ────────────────────────────
TEST(ParserOverflow, ForgedTag429496729634_NeverYieldedAsTag34) {
    auto buf = make_raw_frame(
        "429496729634=42\x01"
        "35=A\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    MessageView<access_mode::Iter> mv{*fv};
    auto fields = collect_iter_fields(mv);

    // The forged value "42" must never appear with tag 34.
    bool found_forged = false;
    for (auto const& f : fields) {
        if (f.tag == 34 && f.value == "42") {
            found_forged = true;
        }
    }
    EXPECT_FALSE(found_forged)
        << "Forged token 429496729634=42 must NOT be yielded as tag 34 (SC-001)";

    // Conforming pair (separate frame): a real 34= field must be yielded.
    auto cbuf = make_raw_frame(
        "35=A\x01"
        "34=7\x01"
        "49=SENDER\x01");
    auto cfv = fixpp::wire::test::make_frame_view(cbuf);
    ASSERT_TRUE(cfv.has_value());
    MessageView<access_mode::Iter> cmv{*cfv};
    auto cfields = collect_iter_fields(cmv);
    bool found_real34 = false;
    for (auto const& f : cfields) {
        if (f.tag == 34 && f.value == "7") {
            found_real34 = true;
        }
    }
    EXPECT_TRUE(found_real34)
        << "Conforming tag 34=7 in a clean frame must be yielded (no regression)";
}

// ── W3: conforming frame — no regression ────────────────────────────────────
// All fields in a well-formed frame must be yielded with correct tags/values.
TEST(ParserOverflow, ConformingFrame_AllFieldsYielded) {
    auto buf = make_raw_frame(
        "35=A\x01"
        "34=3\x01"
        "49=SENDER\x01"
        "56=TARGET\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    MessageView<access_mode::Iter> mv{*fv};
    auto fields = collect_iter_fields(mv);

    bool found35 = false, found34 = false, found49 = false, found56 = false;
    for (auto const& f : fields) {
        if (f.tag == 35 && f.value == "A") {
            found35 = true;
        }
        if (f.tag == 34 && f.value == "3") {
            found34 = true;
        }
        if (f.tag == 49 && f.value == "SENDER") {
            found49 = true;
        }
        if (f.tag == 56 && f.value == "TARGET") {
            found56 = true;
        }
    }
    EXPECT_TRUE(found35) << "tag 35=A must be yielded in conforming frame";
    EXPECT_TRUE(found34) << "tag 34=3 must be yielded in conforming frame";
    EXPECT_TRUE(found49) << "tag 49=SENDER must be yielded in conforming frame";
    EXPECT_TRUE(found56) << "tag 56=TARGET must be yielded in conforming frame";
}

}  // namespace
