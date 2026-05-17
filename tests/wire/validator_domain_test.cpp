// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/validator_domain_test.cpp — T043 (US4, seam #14).
// Red-first validator-domain tests for fixpp::wire::dictionary_driven_validator.
//
// Phase structure:
//   § 1 — Compile-time interface assertions (GREEN now: interface is authored).
//   § 2 — Behavioural GTest cases (RED until T046 defines the 5 overrides).
//
// Include order is mandatory (seam #1 single-definition rule):
//   mock_dict_table.hpp (complete fixpp::dict::table_view) MUST precede
//   <fixpp/wire/validator.hpp> so dictionary_driven_validator::dict_ is a
//   complete object type at instantiation.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

// ── seam #1 — must come BEFORE validator.hpp (supplies complete table_view) ──
#include "support/mock_dict_table.hpp"
#include <fixpp/wire/validator.hpp>

// parser.hpp / frame_view_factory are needed for building Index MessageViews.
#include <fixpp/wire/parser.hpp>
#include "support/frame_view_factory.hpp"

// ── §1: Compile-time interface assertions (already-authored interface) ────────
// These must COMPILE now; they test frozen contracts in validator.hpp.

// Validator is abstract (EXACTLY 5 pure-virtual declared).
static_assert(std::is_abstract_v<fixpp::wire::Validator>,
              "[const §XIV.2] Validator must be abstract (5 pure-virtual)");

// dictionary_driven_validator is final — no further virtual extension.
static_assert(std::is_final_v<fixpp::wire::dictionary_driven_validator>,
              "[2b §6.5] dictionary_driven_validator must be final");

// dictionary_driven_validator is constructible by value from table_view.
static_assert(
    std::is_constructible_v<fixpp::wire::dictionary_driven_validator,
                             fixpp::dict::table_view>,
    "[2b §6.5] must be explicitly constructible from dict::table_view by value");

// table_view is held BY VALUE (SC-007: no virtual edge). It must be
// copy-constructible so dictionary_driven_validator can store a local copy.
static_assert(std::is_copy_constructible_v<fixpp::dict::table_view>,
              "dict::table_view (mock) must be copy-constructible for by-value hold");

// dictionary_driven_validator derives from Validator — but must not be abstract
// itself once all 5 overrides are defined (compile-time check via is_final, not
// is_abstract, since the overrides are declared-but-undefined at this phase).
// We instead assert the polymorphic relationship.
static_assert(
    std::is_base_of_v<fixpp::wire::Validator,
                      fixpp::wire::dictionary_driven_validator>,
    "dictionary_driven_validator must derive from Validator");

// SC-007: table_view held by value — no virtual wire->dict edge.
static_assert(!std::is_polymorphic_v<fixpp::dict::table_view>,
              "SC-007: table_view held by value — no virtual wire->dict edge");

// ── §2: Behavioural tests (RED until T046 defines the overrides) ──────────────
namespace {

using fixpp::wire::access_mode;
using fixpp::wire::MessageView;
using fixpp::wire::Parser;
using fixpp::wire::dictionary_driven_validator;
using fixpp::dict::table_view;
using fixpp::dict::field_type;
using fixpp::core::error;

// ── Helpers ───────────────────────────────────────────────────────────────────

// Build a CheckSum+BodyLength-correct FIX 4.4 frame (same pattern as
// parser_index_test.cpp and wire_alloc_guard_test.cpp).
std::vector<std::byte> make_frame(std::string_view body_fields) {
    std::string body{body_fields};
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string pre = "8=FIX.4.4\x01" + nine + body;
    unsigned sum = 0;
    for (unsigned char c : pre) {
        sum += c;
    }
    char chk[16];
    std::snprintf(chk, sizeof(chk), "10=%03u\x01", sum % 256U);
    std::string full = pre + chk;
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

// Parse a byte buffer into a MessageView<Index> using a default-constructed
// (empty) table_view. Returns the parsed view via the ASSERT_TRUE path.
// stack_buf is the arena backing; caller keeps it alive.
MessageView<access_mode::Index>
parse_index(std::vector<std::byte> const& buf,
            std::array<std::byte, 4096>& stack_buf,
            std::pmr::monotonic_buffer_resource& arena_out) {
    // Construct the arena each call from the caller's stack buffer.
    new (&arena_out) std::pmr::monotonic_buffer_resource{
        stack_buf.data(), stack_buf.size(),
        std::pmr::null_memory_resource()};

    auto fv = fixpp::wire::test::make_frame_view(buf);
    if (!fv.has_value()) {
        ADD_FAILURE() << "make_frame_view failed: "
                      << static_cast<int>(fv.error());
        return {};
    }
    // Parser constructor receives a default table_view (the parser is dict-free;
    // the mock table_view completes the type for instantiation).
    Parser<access_mode::Index> parser{table_view{}};
    auto mv = parser.parse(*fv, &arena_out);
    if (!mv.has_value()) {
        ADD_FAILURE() << "parser.parse failed: "
                      << static_cast<int>(mv.error());
        return {};
    }
    return *mv;
}

// Build the "NewOrderSingle D" mock grammar used across all domain tests.
//   required: 35 (MsgType), 49 (SenderCompID), 56 (TargetCompID),
//             34 (MsgSeqNum), 11 (ClOrdID), 55 (Symbol), 54 (Side)
//   valid (optional): 38 (OrderQty), 40 (OrdType), 453 (NoPartyIDs group)
//   tag 54 (Side) is enumerated: allowed values "1" (Buy) and "2" (Sell)
//   tag 453 starts a repeating group; first field is 448 (PartyID)
table_view make_d_grammar() {
    table_view t;
    // Header fields (always required/valid for every message)
    t.add_required("D", 8)    // BeginString
     .add_required("D", 9)    // BodyLength
     .add_required("D", 35)   // MsgType
     .add_required("D", 49)   // SenderCompID
     .add_required("D", 56)   // TargetCompID
     .add_required("D", 34)   // MsgSeqNum
     .add_required("D", 11)   // ClOrdID
     .add_required("D", 55)   // Symbol
     .add_required("D", 54)   // Side
     .add_required("D", 10)   // CheckSum
     // optional body fields
     .add_valid("D", 38)      // OrderQty
     .add_valid("D", 40)      // OrdType
     .add_valid("D", 453)     // NoPartyIDs (group count)
     .add_valid("D", 448)     // PartyID (group member)
     .add_valid("D", 447)     // PartyIDSource (group member)
     // type annotations
     .set_type(11, field_type::String)   // ClOrdID
     .set_type(38, field_type::Float)    // OrderQty
     .set_type(34, field_type::Int)      // MsgSeqNum
     .set_type(54, field_type::Char)     // Side
     // enum constraint for Side (54): only "1" and "2" are allowed
     .add_enum(54, "1")
     .add_enum(54, "2")
     // repeating group: tag 453 (NoPartyIDs), first delimiter is tag 448
     .set_group_first(453, 448);
    return t;
}

// ── Scratch arena for validator calls (≤ 600 B working set per spec). ─────────
// Give it generous stack backing (2 KiB) so even a naive implementation fits.
constexpr std::size_t kScratchSize = 2048;

// ── Test cases ────────────────────────────────────────────────────────────────

// (a) Conforming message validates to has_value().
TEST(ValidatorDomain, ConformingMessageAccepted) {
    auto gram = make_d_grammar();
    dictionary_driven_validator v{std::move(gram)};

    auto buf = make_frame(
        "35=D\x01" "49=SENDER\x01" "56=TARGET\x01" "34=1\x01"
        "11=ORD-1\x01" "55=AAPL\x01" "54=1\x01");

    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(buf, stack, arena);

    std::array<std::byte, kScratchSize> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{
        scratch_buf.data(), scratch_buf.size(),
        std::pmr::null_memory_resource()};

    auto result = v.validate(mv, &scratch_mr);
    EXPECT_TRUE(result.has_value())
        << "conforming NewOrderSingle must validate OK; error="
        << (result.has_value() ? 0 : static_cast<int>(result.error()));
}

// (b) Missing required field → wire_required_field_missing.
TEST(ValidatorDomain, MissingRequiredFieldRejected) {
    auto gram = make_d_grammar();
    dictionary_driven_validator v{std::move(gram)};

    // ClOrdID (tag 11) is required for "D" but absent here.
    auto buf = make_frame(
        "35=D\x01" "49=SENDER\x01" "56=TARGET\x01" "34=1\x01"
        "55=AAPL\x01" "54=1\x01");

    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(buf, stack, arena);

    std::array<std::byte, kScratchSize> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{
        scratch_buf.data(), scratch_buf.size(),
        std::pmr::null_memory_resource()};

    auto result = v.validate(mv, &scratch_mr);
    ASSERT_FALSE(result.has_value())
        << "message with missing required ClOrdID (11) must be rejected";
    EXPECT_EQ(result.error(), error::wire_required_field_missing)
        << "expected wire_required_field_missing (38), got "
        << static_cast<int>(result.error());
}

// (c) Field not valid for msg_type → wire_unexpected_tag.
TEST(ValidatorDomain, UnexpectedTagRejected) {
    auto gram = make_d_grammar();
    dictionary_driven_validator v{std::move(gram)};

    // Tag 9999 is not registered as valid for "D".
    auto buf = make_frame(
        "35=D\x01" "49=SENDER\x01" "56=TARGET\x01" "34=1\x01"
        "11=ORD-1\x01" "55=AAPL\x01" "54=1\x01"
        "9999=BOGUS\x01");

    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(buf, stack, arena);

    std::array<std::byte, kScratchSize> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{
        scratch_buf.data(), scratch_buf.size(),
        std::pmr::null_memory_resource()};

    auto result = v.validate(mv, &scratch_mr);
    ASSERT_FALSE(result.has_value())
        << "message with unexpected tag 9999 must be rejected";
    EXPECT_EQ(result.error(), error::wire_unexpected_tag)
        << "expected wire_unexpected_tag (42), got "
        << static_cast<int>(result.error());
}

// (e) Malformed repeating-group: count > actual delimiter occurrences.
//     NoPartyIDs=2 but only one PartyID (448) instance follows.
//     Expected: wire_required_field_missing (count mismatch).
TEST(ValidatorDomain, MalformedGroupCountRejected) {
    auto gram = make_d_grammar();
    dictionary_driven_validator v{std::move(gram)};

    // 453=2 declares 2 instances, but only 1 delimiter (448) follows.
    auto buf = make_frame(
        "35=D\x01" "49=SENDER\x01" "56=TARGET\x01" "34=1\x01"
        "11=ORD-1\x01" "55=AAPL\x01" "54=1\x01"
        "453=2\x01" "448=PA\x01" "447=D\x01");   // only 1 instance, not 2

    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(buf, stack, arena);

    std::array<std::byte, kScratchSize> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{
        scratch_buf.data(), scratch_buf.size(),
        std::pmr::null_memory_resource()};

    auto result = v.validate(mv, &scratch_mr);
    ASSERT_FALSE(result.has_value())
        << "group count=2 with only 1 instance must be rejected";
    EXPECT_EQ(result.error(), error::wire_required_field_missing)
        << "expected wire_required_field_missing for count mismatch, got "
        << static_cast<int>(result.error());
}

// (f) Malformed repeating-group: wrong first field (not the delimiter).
//     The first entry after the count is NOT the delimiter tag.
TEST(ValidatorDomain, MalformedGroupFirstFieldRejected) {
    auto gram = make_d_grammar();
    dictionary_driven_validator v{std::move(gram)};

    // 453=1 but first group field is 447 (PartyIDSource), not 448 (PartyID).
    auto buf = make_frame(
        "35=D\x01" "49=SENDER\x01" "56=TARGET\x01" "34=1\x01"
        "11=ORD-1\x01" "55=AAPL\x01" "54=1\x01"
        "453=1\x01" "447=D\x01" "448=PA\x01");   // delimiter 448 is NOT first

    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(buf, stack, arena);

    std::array<std::byte, kScratchSize> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{
        scratch_buf.data(), scratch_buf.size(),
        std::pmr::null_memory_resource()};

    auto result = v.validate(mv, &scratch_mr);
    ASSERT_FALSE(result.has_value())
        << "group with non-delimiter first field must be rejected";
    EXPECT_EQ(result.error(), error::wire_required_field_missing)
        << "expected wire_required_field_missing for wrong first field, got "
        << static_cast<int>(result.error());
}

// (g) Well-formed repeating group: count=2 with exactly 2 delimiter occurrences.
TEST(ValidatorDomain, WellFormedGroupAccepted) {
    auto gram = make_d_grammar();
    dictionary_driven_validator v{std::move(gram)};

    auto buf = make_frame(
        "35=D\x01" "49=SENDER\x01" "56=TARGET\x01" "34=1\x01"
        "11=ORD-1\x01" "55=AAPL\x01" "54=1\x01"
        "453=2\x01"
        "448=PA\x01" "447=D\x01"   // instance 1
        "448=PB\x01" "447=E\x01"); // instance 2

    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(buf, stack, arena);

    std::array<std::byte, kScratchSize> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{
        scratch_buf.data(), scratch_buf.size(),
        std::pmr::null_memory_resource()};

    auto result = v.validate(mv, &scratch_mr);
    EXPECT_TRUE(result.has_value())
        << "well-formed 2-instance group must validate OK; error="
        << (result.has_value() ? 0 : static_cast<int>(result.error()));
}

// (d) Out-of-range enum value → wire_field_value_out_of_range.
TEST(ValidatorDomain, EnumViolationRejected) {
    auto gram = make_d_grammar();
    dictionary_driven_validator v{std::move(gram)};

    // Side (tag 54) has value "X" which is not in the {"1","2"} enum set.
    auto buf = make_frame(
        "35=D\x01" "49=SENDER\x01" "56=TARGET\x01" "34=1\x01"
        "11=ORD-1\x01" "55=AAPL\x01" "54=X\x01");

    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(buf, stack, arena);

    std::array<std::byte, kScratchSize> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{
        scratch_buf.data(), scratch_buf.size(),
        std::pmr::null_memory_resource()};

    auto result = v.validate(mv, &scratch_mr);
    ASSERT_FALSE(result.has_value())
        << "message with enum-violating Side=X must be rejected";
    EXPECT_EQ(result.error(), error::wire_field_value_out_of_range)
        << "expected wire_field_value_out_of_range (40), got "
        << static_cast<int>(result.error());
}

}  // namespace
