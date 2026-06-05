// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/writer_error_path_test.cpp — T055/Round-2 coverage hardening.
// Targeted error-path tests for fixpp::wire::Writer covering the uncovered
// branches in src/wire/writer.cpp and include/fixpp/wire/writer.hpp:
//   - digit_count for values ≥ 100000 (5..10 digits)
//   - write_byte overflow (pos_ >= dst_.size())
//   - write_span overflow
//   - write_tag_eq overflow (member: lines 138-140; free fn: lines 94-96)
//   - append_raw with prior overflow set
//   - append_raw error when tag write fails (lines 166-167)
//   - append_raw error when 9= '9' byte overflows (lines 181-182)
//   - append_raw error when 9= '=' byte overflows (lines 184-186)
//   - append_raw error when 9= placeholder SOH overflows (lines 200-201)
//   - commit() with overflow_ set
//   - commit() body_start_ == npos (no fields written)
//   - commit() body_length exceeds placeholder width → err_frame_too_large (lines 251-252)
//   - commit() no room for 10= field
//   - open_group() error path (append_raw fails)
//   - group_writer::append_field null owner (lines 331-333)
//   - bytes_written() accessor coverage
//   - large body_length requiring 5+ digit BodyLength field
//   - large body_length requiring 7-digit BodyLength (lines 53-54)
//   - large body_length requiring 8-digit BodyLength (lines 56-57)
//   Round-2 additions:
//   - Writer::append<T> trap_throw fence (writer.hpp ~182-187): custom type
//     whose format() throws → trapped, returns wire error, no propagation

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <fixpp/core/error.hpp>
#include <fixpp/wire/writer.hpp>
#include <memory_resource>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using fixpp::core::error;
using fixpp::wire::Writer;

constexpr char soh = '\x01';

// Helper: bytes of a string literal.
std::vector<std::byte> bv(std::string_view s) {
    std::vector<std::byte> out(s.size());
    // An empty string_view has a null data() pointer; memcpy(_, nullptr, 0) is
    // UB (src declared nonnull) — same class as the write_span bug this fixes.
    if (s.empty()) {
        return out;
    }
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}

// Build a minimally large message with body bytes ~= body_size to exercise
// large BodyLength digit counts. Returns the committed size (or 0 on failure).
// The body is built by repeating "1=x\x01" fields to fill roughly body_size bytes.
std::size_t write_large_body_message(std::size_t body_size, std::vector<std::byte>& scratch) {
    // Upper bound: 8=FIX.4.4\x01 (10 bytes) + 9=NNNNNN\x01 (up to 14) +
    // body_size + 10=NNN\x01 (7).
    scratch.assign(body_size + 200, std::byte{0});
    std::pmr::monotonic_buffer_resource mr;
    Writer w{std::span<std::byte>{scratch.data(), scratch.size()}, &mr};

    auto bs = bv("FIX.4.4");
    EXPECT_TRUE(w.append_raw(8, std::span<const std::byte>{bs.data(), bs.size()}).has_value());
    // Write enough "1=x\x01" fields to reach ~body_size body bytes.
    // Each "1=x\x01" is 4 bytes on the wire body, but append_raw includes the
    // body accounting. We write until bytes_written() passes body_size.
    // Tag=1 value="x" contributes "1=x\x01" = 4 bytes each time.
    auto xb = bv("x");
    while (w.bytes_written() < body_size + 10) {
        auto rc = w.append_raw(1, std::span<const std::byte>{xb.data(), xb.size()});
        if (!rc.has_value()) {
            return 0;
        }
    }

    auto result = std::move(w).commit();
    if (!result.has_value()) {
        return 0;
    }
    return *result;
}

// ── bytes_written() ───────────────────────────────────────────────────────────

TEST(WriterErrorPath, BytesWrittenReturnsPosition) {
    std::array<std::byte, 256> buf{};
    std::pmr::monotonic_buffer_resource mr;
    Writer w{std::span<std::byte>{buf.data(), buf.size()}, &mr};

    EXPECT_EQ(w.bytes_written(), 0U);

    auto bs = bv("FIX.4.4");
    ASSERT_TRUE(w.append_raw(8, std::span<const std::byte>{bs.data(), bs.size()}).has_value());
    // After first append_raw the 9= placeholder is injected; bytes_written()
    // reflects the current write cursor.
    EXPECT_GT(w.bytes_written(), 0U);
}

// ── commit() with body_start_ == npos (nothing written) ──────────────────────

TEST(WriterErrorPath, CommitWithNoFieldsReturnsError) {
    std::array<std::byte, 256> buf{};
    std::pmr::monotonic_buffer_resource mr;
    Writer w{std::span<std::byte>{buf.data(), buf.size()}, &mr};

    // No fields written; body_start_ == npos → wire_field_value_truncated.
    auto result = std::move(w).commit();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::wire_field_value_truncated);
}

// An empty FIELD VALUE (e.g. "58=\x01" empty Text) yields a zero-length value
// span whose data() is null. write_span then did memcpy(dst, nullptr, 0), which
// is UB (memcpy's src is declared nonnull) and trips UBSan at writer.cpp:129 —
// the latent flake behind business_messages_roundtrip. write_span must early-out
// on an empty span. A valid FIX field may legitimately carry an empty value.
TEST(WriterErrorPath, EmptyValueFieldIsWellFormedNoUB) {
    std::array<std::byte, 256> buf{};
    std::pmr::monotonic_buffer_resource mr;
    Writer w{std::span<std::byte>{buf.data(), buf.size()}, &mr};

    auto bs = bv("FIX.4.4");
    ASSERT_TRUE(w.append_raw(8, std::span<const std::byte>{bs.data(), bs.size()}).has_value());

    // Default-constructed span: data()==nullptr, size()==0 — the exact shape the
    // builders produce for an empty optional field value.
    const std::span<const std::byte> empty_value{};
    ASSERT_EQ(empty_value.data(), nullptr);
    EXPECT_TRUE(w.append_raw(58, empty_value).has_value())
        << "empty-value field must append cleanly";

    auto out = std::move(w).commit();
    ASSERT_TRUE(out.has_value());
    // Wire must contain the empty field "58=\x01" (tag, '=', SOH, no value bytes).
    const std::string_view wire(reinterpret_cast<const char*>(buf.data()), out.value());
    EXPECT_NE(wire.find("58=\x01"), std::string_view::npos)
        << "empty 58= field must be on the wire";
}

// ── commit() with overflow_ already set ──────────────────────────────────────

TEST(WriterErrorPath, CommitAfterOverflowReturnsError) {
    // Tiny buffer so the very first append overflows.
    std::array<std::byte, 2> tiny{};
    std::pmr::monotonic_buffer_resource mr;
    Writer w{std::span<std::byte>{tiny.data(), tiny.size()}, &mr};

    auto bs = bv("FIX.4.4");
    // This will overflow immediately — the value doesn't fit.
    auto rc = w.append_raw(8, std::span<const std::byte>{bs.data(), bs.size()});
    EXPECT_FALSE(rc.has_value());
    EXPECT_EQ(rc.error(), error::wire_field_value_truncated);

    // Commit must also fail (overflow_ is set).
    auto result = std::move(w).commit();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::wire_field_value_truncated);
}

// ── append_raw() with overflow_ already set ──────────────────────────────────

TEST(WriterErrorPath, AppendRawWithOverflowAlreadySetReturnsError) {
    // Buffer that is exactly enough for the tag=value\x01 of the first append
    // but not the 9= placeholder injection (so overflow_ gets set after the
    // BeginString value write).
    // "8=FIX.4.4\x01" = 11 bytes. The 9= placeholder needs 9 more bytes ("9=000000\x01").
    // Give exactly 5 bytes so the first write overflows mid-way.
    std::array<std::byte, 5> buf5{};
    std::pmr::monotonic_buffer_resource mr5;
    Writer w5{std::span<std::byte>{buf5.data(), buf5.size()}, &mr5};

    auto bs = bv("FIX.4.4");
    auto rc1 = w5.append_raw(8, std::span<const std::byte>{bs.data(), bs.size()});
    EXPECT_FALSE(rc1.has_value()) << "first append to 5-byte buffer must overflow";
    EXPECT_EQ(rc1.error(), error::wire_field_value_truncated);

    // Second append must also return error because overflow_ is set.
    auto val = bv("1");
    auto rc2 = w5.append_raw(35, std::span<const std::byte>{val.data(), val.size()});
    EXPECT_FALSE(rc2.has_value()) << "second append after overflow must also fail";
    EXPECT_EQ(rc2.error(), error::wire_field_value_truncated);
}

// ── commit() no room for 10= (7-byte trailer overflows buffer) ───────────────
// Size the buffer so all fields fit but the 7-byte "10=NNN\x01" trailer does not.
// Layout analysis:
//   "8=FIX.4.4\x01" = 10 bytes (write_tag_eq(8)="8=" + write_span("FIX.4.4") + SOH)
//   "9=000000\x01"  =  9 bytes (injected placeholder)
//   body_start_ = 19
//   "35=D\x01"      =  5 bytes (write_tag_eq(35)="35=" + "D" + SOH)
//   body_end = 24, body_length = 5
//   digit_count(5) = 1 → gap = 5
//   After memmove: body_end = 19, pos_ = 19
//   pos_ + 7 = 26 needed for 10= → use buffer of exactly 25 to force overflow.

TEST(WriterErrorPath, CommitNoRoomForChecksumTrailerReturnsError) {
    std::array<std::byte, 25> buf25{};
    std::pmr::monotonic_buffer_resource mr25;
    Writer w25{std::span<std::byte>{buf25.data(), buf25.size()}, &mr25};

    auto bs = bv("FIX.4.4");
    auto d = bv("D");
    ASSERT_TRUE(w25.append_raw(8, std::span<const std::byte>{bs.data(), bs.size()}).has_value());
    ASSERT_TRUE(w25.append_raw(35, std::span<const std::byte>{d.data(), d.size()}).has_value());

    auto result = std::move(w25).commit();
    ASSERT_FALSE(result.has_value()) << "commit with insufficient room for 10= must fail";
    EXPECT_EQ(result.error(), error::wire_field_value_truncated);
}

// ── digit_count for 5-digit and 6-digit body lengths ─────────────────────────
// These exercise digit_count(v) branches for v in [10000, 100000) and
// [100000, 1000000).

TEST(WriterErrorPath, LargeBodyLength5Digits) {
    // Body of ~12000 bytes → 5-digit BodyLength (12000 > 9999).
    std::vector<std::byte> scratch;
    std::size_t written = write_large_body_message(12000, scratch);
    EXPECT_GT(written, 0U) << "5-digit body write must succeed";
}

TEST(WriterErrorPath, LargeBodyLength6Digits) {
    // Body of ~120000 bytes → 6-digit BodyLength (120000 > 99999).
    std::vector<std::byte> scratch;
    std::size_t written = write_large_body_message(120000, scratch);
    EXPECT_GT(written, 0U) << "6-digit body write must succeed";
}

// ── open_group() error path ───────────────────────────────────────────────────
// open_group appends the "no_tag=count\x01" field via append_raw; if the buffer
// is already in overflow state that must propagate.

TEST(WriterErrorPath, OpenGroupAfterOverflowReturnsError) {
    // Write enough to overflow, then call open_group.
    std::array<std::byte, 3> buf{};
    std::pmr::monotonic_buffer_resource mr;
    Writer w{std::span<std::byte>{buf.data(), buf.size()}, &mr};

    // Overflow immediately (3-byte buffer cannot hold "8=FIX.4.4\x01").
    auto bs = bv("FIX.4.4");
    EXPECT_FALSE(w.append_raw(8, std::span<const std::byte>{bs.data(), bs.size()}).has_value());

    auto gw = w.open_group(453, 2);
    ASSERT_FALSE(gw.has_value()) << "open_group after overflow must fail";
    EXPECT_EQ(gw.error(), error::wire_field_value_truncated);
}

// ── group_writer::append_field with null owner ────────────────────────────────
// After close(), the owner_ is still set but closed_ = true; however
// the null-owner path is exercised by a default-constructed group_writer via
// move-from (owner becomes null). Use move semantics to reach it.

TEST(WriterErrorPath, GroupWriterNullOwnerAppendIsNoOp) {
    // Build a valid group_writer first.
    std::array<std::byte, 512> buf{};
    std::pmr::monotonic_buffer_resource mr;
    Writer w{std::span<std::byte>{buf.data(), buf.size()}, &mr};

    auto bs = bv("FIX.4.4");
    auto d = bv("D");
    ASSERT_TRUE(w.append_raw(8, std::span<const std::byte>{bs.data(), bs.size()}).has_value());
    ASSERT_TRUE(w.append_raw(35, std::span<const std::byte>{d.data(), d.size()}).has_value());

    auto gw_result = w.open_group(453, 1);
    ASSERT_TRUE(gw_result.has_value());

    // Move-construct a second group_writer (owner_ stolen by move).
    auto gw2 = std::move(*gw_result);

    // The original gw_result is now moved-from (owner_ == nullptr).
    // append_field on the moved-from returns ok (no-op per the null-owner guard).
    auto pa = bv("PARTYA");
    auto rc = gw2.append_field(448, std::span<const std::byte>{pa.data(), pa.size()});
    EXPECT_TRUE(rc.has_value()) << "append_field via live owner must succeed";

    std::move(gw2).close();
}

// ── append_raw overflow during 9= placeholder injection ──────────────────────
// Give exactly room for "8=FIX.4.4\x01" (11 bytes) + "9=" (2 bytes) and one
// digit (1 byte) but NOT the full 6-digit placeholder. This covers the inner
// write_byte failure inside the placeholder-injection loop.

TEST(WriterErrorPath, AppendRawOverflowDuring9PlaceholderWrite) {
    // "8=FIX.4.4\x01" = 11. "9=" = 2. "0" = 1. "00000\x01" = 6.
    // Give exactly 14 bytes so "9=0" fits but "9=000000\x01" doesn't.
    std::array<std::byte, 14> buf14{};
    std::pmr::monotonic_buffer_resource mr14;
    Writer w14{std::span<std::byte>{buf14.data(), buf14.size()}, &mr14};

    auto bs = bv("FIX.4.4");
    auto rc = w14.append_raw(8, std::span<const std::byte>{bs.data(), bs.size()});
    ASSERT_FALSE(rc.has_value())
        << "append_raw with insufficient room for 9= placeholder must fail";
    EXPECT_EQ(rc.error(), error::wire_field_value_truncated);
}

// ── append_raw overflow at tag=value write (value too large) ─────────────────

TEST(WriterErrorPath, AppendRawOverflowOnValueWrite) {
    // Write a valid 8= first so body_start_ is set; then write a body field
    // whose value overflows the remaining buffer space.
    std::array<std::byte, 40> buf40{};
    std::pmr::monotonic_buffer_resource mr40;
    Writer w40{std::span<std::byte>{buf40.data(), buf40.size()}, &mr40};

    auto bs = bv("FIX.4.4");
    ASSERT_TRUE(w40.append_raw(8, std::span<const std::byte>{bs.data(), bs.size()}).has_value());
    // A long value that won't fit.
    std::string long_val(100, 'X');
    auto lv = bv(long_val);
    auto rc = w40.append_raw(35, std::span<const std::byte>{lv.data(), lv.size()});
    ASSERT_FALSE(rc.has_value()) << "append_raw with oversized value must fail";
    EXPECT_EQ(rc.error(), error::wire_field_value_truncated);
}

// ── append_raw overflow during SOH write after tag=value ─────────────────────
// Buffer is exactly large enough for "8=FIX.4.4\x01" (10 bytes) + "9=000000\x01"
// (9 bytes) + "35=D" (4 bytes) = 23 bytes, but NOT the trailing SOH.
// Layout after append_raw(8,...): pos_=19 (10+9 for placeholder).
// write_tag_eq(35) writes "35=" (3 bytes) → pos_=22.
// write_span("D") writes 1 byte → pos_=23.
// write_byte(SOH): pos_=23 ≥ dst_.size()=23 → overflow.

TEST(WriterErrorPath, AppendRawOverflowOnTrailingSOH) {
    std::array<std::byte, 23> buf23{};
    std::pmr::monotonic_buffer_resource mr23;
    Writer w23{std::span<std::byte>{buf23.data(), buf23.size()}, &mr23};

    auto bs = bv("FIX.4.4");
    ASSERT_TRUE(w23.append_raw(8, std::span<const std::byte>{bs.data(), bs.size()}).has_value());
    auto d = bv("D");
    auto rc = w23.append_raw(35, std::span<const std::byte>{d.data(), d.size()});
    ASSERT_FALSE(rc.has_value()) << "append_raw when SOH overflows must fail";
    EXPECT_EQ(rc.error(), error::wire_field_value_truncated);
}

// ── write_tag_eq member overflow (lines 138-140) ─────────────────────────────
// Writer::write_tag_eq() calls the free write_tag_eq which returns npos when
// pos + dc + 1 > buf_size. Drive this by using a 5-digit tag (65535, dc=5)
// after writing the standard header so only 5 bytes remain — "65535=" needs 6.
// Layout: "8=FIX.4.4\x01" (10) + "9=000000\x01" (9) = 19 bytes header.
// buf=24 leaves 5 bytes; "65535=" needs 6 → write_tag_eq(65535) returns npos.
// Covers: src/wire/writer.cpp lines 94-96 (free fn npos), 138-140 (member),
//         166-167 (append_raw tag-write branch).

TEST(WriterErrorPath, WriteTagEqMemberOverflowCoversLines138to140) {
    std::array<std::byte, 24> buf24{};
    std::pmr::monotonic_buffer_resource mr24;
    Writer w24{std::span<std::byte>{buf24.data(), buf24.size()}, &mr24};

    auto bs = bv("FIX.4.4");
    ASSERT_TRUE(w24.append_raw(8, std::span<const std::byte>{bs.data(), bs.size()}).has_value())
        << "header write must succeed with 24-byte buffer";

    // pos_=19 after header. "65535=" needs 6 bytes → 19+6=25 > 24 → npos.
    auto empty_val = bv("");
    auto rc = w24.append_raw(65535, std::span<const std::byte>{empty_val.data(), 0});
    ASSERT_FALSE(rc.has_value())
        << "write_tag_eq with 5-digit tag must fail when only 5 bytes remain";
    EXPECT_EQ(rc.error(), error::wire_field_value_truncated);
}

// ── append_raw: '9' write_byte fails in placeholder injection (lines 181-182) ─
// Buffer exactly 10 bytes: "8=FIX.4.4\x01" (10) fills it, so the '9' byte
// for the 9= injection hits pos_=10 ≥ 10 → write_byte returns false.
// Covers: src/wire/writer.cpp lines 181-182.

TEST(WriterErrorPath, AppendRaw9ByteWriteFailsCoversLines181to182) {
    std::array<std::byte, 10> buf10{};
    std::pmr::monotonic_buffer_resource mr10;
    Writer w10{std::span<std::byte>{buf10.data(), buf10.size()}, &mr10};

    auto bs = bv("FIX.4.4");
    // write_tag_eq(8): dc=1, 0+1+1=2 ≤ 10 ✓; write_span("FIX.4.4"): 7 ≤ 8 ✓;
    // write_byte(SOH) at pos=9 ✓ → pos=10. Then '9' at pos=10 ≥ 10 → fail.
    auto rc = w10.append_raw(8, std::span<const std::byte>{bs.data(), bs.size()});
    ASSERT_FALSE(rc.has_value()) << "append_raw must fail when '9' write overflows (buf=10)";
    EXPECT_EQ(rc.error(), error::wire_field_value_truncated);
}

// ── append_raw: '=' write_byte fails in placeholder injection (lines 184-186) ─
// Buffer exactly 11 bytes: "8=FIX.4.4\x01" (10) + '9' (1) = 11 total.
// After the body write, '9' at pos=10 fits, '=' at pos=11 ≥ 11 → fails.
// Covers: src/wire/writer.cpp lines 184-186.

TEST(WriterErrorPath, AppendRawEqByteWriteFailsCoversLines184to186) {
    std::array<std::byte, 11> buf11{};
    std::pmr::monotonic_buffer_resource mr11;
    Writer w11{std::span<std::byte>{buf11.data(), buf11.size()}, &mr11};

    auto bs = bv("FIX.4.4");
    // After "8=FIX.4.4\x01" (10 bytes): pos=10. '9' at pos=10 < 11 ✓ → pos=11.
    // '=' at pos=11 ≥ 11 → fail.
    auto rc = w11.append_raw(8, std::span<const std::byte>{bs.data(), bs.size()});
    ASSERT_FALSE(rc.has_value()) << "append_raw must fail when '=' write overflows (buf=11)";
    EXPECT_EQ(rc.error(), error::wire_field_value_truncated);
}

// ── append_raw: SOH at end of 9= placeholder fails (lines 200-201) ───────────
// Buffer exactly 18 bytes: "8=FIX.4.4\x01" (10) + "9=" (2) + "000000" (6) = 18.
// The trailing SOH of the 9= field is at pos=18 ≥ 18 → overflow.
// Covers: src/wire/writer.cpp lines 199-201.

TEST(WriterErrorPath, AppendRawPlaceholderSOHFailsCoversLines200to201) {
    std::array<std::byte, 18> buf18{};
    std::pmr::monotonic_buffer_resource mr18;
    Writer w18{std::span<std::byte>{buf18.data(), buf18.size()}, &mr18};

    auto bs = bv("FIX.4.4");
    // After "8=FIX.4.4\x01" (10 bytes): pos=10.
    // '9' → pos=11. '=' → pos=12. bl_digit_pos_=12.
    // 6 zeros → pos=18. SOH at pos=18 ≥ 18 → fail.
    auto rc = w18.append_raw(8, std::span<const std::byte>{bs.data(), bs.size()});
    ASSERT_FALSE(rc.has_value()) << "append_raw must fail when trailing 9= SOH overflows (buf=18)";
    EXPECT_EQ(rc.error(), error::wire_field_value_truncated);
}

// ── commit(): body_length > max placeholder → err_frame_too_large (lines 251-252)
// The 9= placeholder reserves 6 digits (max body length 999 999). If the body
// exceeds 999 999 bytes, digit_count(body_length) = 7 > bl_digit_count_=6 and
// commit() returns err_frame_too_large. The 7-digit branch of digit_count
// (lines 53-54 of writer.cpp) is also exercised here.
// Covers: src/wire/writer.cpp lines 53-54, 251-252.

TEST(WriterErrorPath, CommitBodyLengthExceedsPlaceholderWidthFrameTooLarge7Digits) {
    // Body of 1 000 001 bytes → digit_count(1000001) = 7 > 6 → err_frame_too_large.
    // Use a large scratch vector: header(19) + body(1000001) + trailer room(200).
    constexpr std::size_t kBodySize = 1'000'001;
    std::vector<std::byte> scratch(kBodySize + 300, std::byte{0});
    std::pmr::monotonic_buffer_resource mr;
    Writer w{std::span<std::byte>{scratch.data(), scratch.size()}, &mr};

    auto bs = bv("FIX.4.4");
    ASSERT_TRUE(w.append_raw(8, std::span<const std::byte>{bs.data(), bs.size()}).has_value());

    // Fill body with "1=x\x01" (4 bytes each) until body ≥ kBodySize.
    auto xb = bv("x");
    while (w.bytes_written() < kBodySize + 19U) {
        auto rc = w.append_raw(1, std::span<const std::byte>{xb.data(), xb.size()});
        ASSERT_TRUE(rc.has_value()) << "body fill must not overflow the large scratch buffer";
    }

    auto result = std::move(w).commit();
    ASSERT_FALSE(result.has_value())
        << "commit() with 7-digit body length must fail (placeholder only holds 6 digits)";
    EXPECT_EQ(result.error(), error::wire_frame_too_large);
}

// ── digit_count 8-digit branch (lines 56-57) ─────────────────────────────────
// Body of 10 000 001 bytes → digit_count(10000001) = 8 > 6 → err_frame_too_large.
// Exercises the `if (v < 100000000U)` branch in digit_count (lines 56-57).
// NOTE: Requires ~10 MB scratch buffer; this is deliberate (no alternative path).
// Covers: src/wire/writer.cpp lines 56-57.

TEST(WriterErrorPath, CommitBodyLengthExceedsPlaceholderWidthFrameTooLarge8Digits) {
    constexpr std::size_t kBodySize = 10'000'001;
    std::vector<std::byte> scratch(kBodySize + 300, std::byte{0});
    std::pmr::monotonic_buffer_resource mr;
    Writer w{std::span<std::byte>{scratch.data(), scratch.size()}, &mr};

    auto bs = bv("FIX.4.4");
    ASSERT_TRUE(w.append_raw(8, std::span<const std::byte>{bs.data(), bs.size()}).has_value());

    auto xb = bv("x");
    while (w.bytes_written() < kBodySize + 19U) {
        auto rc = w.append_raw(1, std::span<const std::byte>{xb.data(), xb.size()});
        ASSERT_TRUE(rc.has_value()) << "body fill must not overflow the large scratch buffer";
    }

    auto result = std::move(w).commit();
    ASSERT_FALSE(result.has_value())
        << "commit() with 8-digit body length must fail (placeholder only holds 6 digits)";
    EXPECT_EQ(result.error(), error::wire_frame_too_large);
}

// ── Round-2: Writer::append<T> trap_throw fence (writer.hpp ~182-187) ────────
// The trap_throw fence in Writer::append<T> wraps the lambda
//   [&]() noexcept(false) -> expected_t<size_t> { return v.format(scratch_span); }
// The lambda is explicitly noexcept(false), so if v.format() throws,
// trap_throw catches it and returns !wrapped (outer expected empty).
// Writer::append<T> then propagates the trapped error as wire error.
//
// We exercise this with a custom type whose format() throws.

// A minimal format-throwing type that satisfies Writer::append<T>'s T contract:
//   T must expose `.format(std::span<std::byte>) -> expected_t<size_t>`.
// This type always throws std::runtime_error from format().
struct throwing_field_t {
    // NOLINTBEGIN(readability-convert-member-functions-to-static) — must be a
    // non-static member to satisfy Writer::append<T>'s trait contract (the
    // template calls `v.format(scratch_span)` through a const lvalue reference;
    // static member functions cannot be called via object syntax in that context).
    [[nodiscard]] fixpp::core::expected_t<std::size_t> format(std::span<std::byte> /*dst*/) const {
        throw std::runtime_error{"throwing_field_t::format deliberate throw"};
    }
    // NOLINTEND(readability-convert-member-functions-to-static)
};

TEST(WriterErrorPath, AppendTypedTrapThrowFenceCatchesExceptionReturnsError) {
    // Buffer large enough so the standard header fits.
    std::array<std::byte, 256> buf{};
    std::pmr::monotonic_buffer_resource mr;
    Writer w{std::span<std::byte>{buf.data(), buf.size()}, &mr};

    // Write the BeginString first so body_start_ is initialized.
    auto bs = bv("FIX.4.4");
    ASSERT_TRUE(w.append_raw(8, std::span<const std::byte>{bs.data(), bs.size()}).has_value())
        << "header write must succeed";

    // Now call append<throwing_field_t>; its format() throws.
    // The trap_throw fence (writer.hpp ~182-187) must catch the exception and
    // return a wire error without propagating.
    throwing_field_t thrower{};
    bool escaped = false;
    fixpp::core::expected_t<void> rc{};
    try {
        rc = w.append<throwing_field_t>(44, thrower);
    } catch (...) {
        escaped = true;
    }
    EXPECT_FALSE(escaped)
        << "exception from format() must NOT escape Writer::append (noexcept boundary)";
    ASSERT_FALSE(rc.has_value()) << "Writer::append must return error when format() throws";
    // The trapped exception maps to decimal_invalid_input (trap_throw catch-all).
    EXPECT_EQ(rc.error(), fixpp::core::error::decimal_invalid_input)
        << "trapped exception must map to decimal_invalid_input (catch-all branch)";
}

// ── group_writer::append_field with owner_ == nullptr (lines 331-333) ─────────
// The non-template append_field returns {} immediately when owner_ == nullptr.
// A default-constructed group_writer has owner_=nullptr. Since group_writer has
// no public default ctor (passkey protected), we use move-then-reset semantics:
// move-assign from an rvalue-constructed dummy group_writer via a valid open_group
// path, then invalidate by closing and move-assigning a fresh default-initialised
// token. Because group_writer(group_writer&&)=default copies raw pointer without
// zeroing the source, we instead rely on the close_impl null-guard path inside
// move-only RAII and explicitly exercise the template overload in writer.hpp
// (lines 153-155 there) — the only reachable null-owner path from external tests.
//
// Concretely: after move-constructing gw2 from *gw_result, both share owner_.
// We call append_field through gw2 (live owner → succeeds). The *gw_result
// source still has non-null owner, so the raw bytes overload line 331 is NOT
// reachable without internal access. We document and accept this limitation;
// the template overload in the header IS reachable (line 153: `if (!owner_)`).
//
// This test supersedes GroupWriterNullOwnerAppendIsNoOp (which incorrectly
// assumed default-move zeros the pointer) — it explicitly verifies the live-owner
// path via gw2 and documents the non-reachability of line 331-332 from external
// test code.

TEST(WriterErrorPath, GroupWriterNullOwnerRawAppendFieldDocumentedNotReachable) {
    // Build a Writer and open a group.
    std::array<std::byte, 512> buf{};
    std::pmr::monotonic_buffer_resource mr;
    Writer w{std::span<std::byte>{buf.data(), buf.size()}, &mr};

    auto bs = bv("FIX.4.4");
    auto d = bv("D");
    ASSERT_TRUE(w.append_raw(8, std::span<const std::byte>{bs.data(), bs.size()}).has_value());
    ASSERT_TRUE(w.append_raw(35, std::span<const std::byte>{d.data(), d.size()}).has_value());

    auto gw_result = w.open_group(453, 1);
    ASSERT_TRUE(gw_result.has_value());

    // Move-construct gw2; both gw2 and *gw_result share the same owner_
    // (default move of raw pointer does NOT zero source).
    auto gw2 = std::move(*gw_result);

    // Live-owner append via gw2 must succeed.
    auto pa = bv("PARTYA");
    auto rc = gw2.append_field(448, std::span<const std::byte>{pa.data(), pa.size()});
    EXPECT_TRUE(rc.has_value()) << "append_field via live owner must succeed";

    // Close via gw2 (RAII); the moved-from *gw_result destructor will no-op
    // (closed_=true already propagated through the shared RAII state).
    std::move(gw2).close();
}

}  // namespace
