// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_credential_store_redaction.cpp
//
// 034-credential-store-redaction — witness suite.
//
// Phase 1 (T001): skeleton + CMake registration.
// Phase 2 (T002/T003): standalone masker unit — Masker_SameLength_FieldAnchored_unit.
// Later phases (T005–T011) append cells to this file.
//
// TDD: Phase 2 cells are RED until T003 (masker impl) ships; the masker is in
// include/fixpp/session/logon_credentials.hpp (sibling of redact_tag554).
//
// FileStore-fixture headers (_fixtures_/store_temp_dir.hpp,
// _fixtures_/test_double_fsm.hpp) are included for later phases (T005 US1
// disk-byte witnesses); they add zero link cost for the Phase-2 masker unit.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <fixpp/session/logon_credentials.hpp>

#include "_fixtures_/store_temp_dir.hpp"
#include "_fixtures_/test_double_fsm.hpp"

namespace {

using fixpp::session::mask_tag554_same_length_inplace;

// ── helper ───────────────────────────────────────────────────────────────────

// Build a mutable byte buffer from a char literal (no NUL appended).
// SOH characters in the source string are represented as '\x01'.
template <std::size_t N>
std::array<std::byte, N - 1> make_buf(const char (&src)[N]) {
    std::array<std::byte, N - 1> buf{};
    for (std::size_t i = 0; i < N - 1; ++i) {
        buf[i] = std::byte{static_cast<std::uint8_t>(src[i])};
    }
    return buf;
}

// Returns true iff every byte in [begin, end) equals '*' (0x2A).
bool all_stars(const std::byte* begin, const std::byte* end) {
    for (auto p = begin; p != end; ++p) {
        if (*p != std::byte{0x2A}) return false;
    }
    return true;
}

// ── Masker_SameLength_FieldAnchored_unit ─────────────────────────────────────
//
// Witnesses C1 / E1 / I-E1-* from contracts/store-redaction.md:
//
//   (a) A genuine \x01554=secret\x01 value is overwritten with an EQUAL-LENGTH
//       run of '*' (0x2A).
//   (b) frame.size() (the span's extent) is unchanged — and no byte outside the
//       value extent is modified (I-E1-1 full form: snapshot + compare).
//   (c) A decoy "554=" inside a tag-58 free-text value is NOT touched (field-
//       boundary anchored; C1 field-detection rule).
//   (d) Calling the masker a second time on the already-masked buffer is
//       byte-stable (idempotent, I-E1-2) — the function returns true (found the
//       field) but no byte changes.
//   (e) A buffer with no genuine 554 field returns false and is unchanged.
//
// Additional cells not enumerated in (a)–(e) but required by the spec:
//   (f) Empty-value case \x01554=\x01: returns true, frame is byte-unchanged
//       (zero-length overwrite, no UB — I-E1-4).
//   (g) Frame-start occurrence "554=secret\x01" (no leading SOH): matched and
//       masked correctly (data-model.md E1 field-detection rule, offset-0 arm).

TEST(Masker_SameLength_FieldAnchored_unit, A_MasksGenuineField_StarRun) {
    // Minimal frame: just the \x01554=secret\x01 sequence (mid-frame occurrence).
    // The 554 value "secret" is 6 bytes; after masking it must be "******".
    auto buf = make_buf("\x01""554=secret\x01");
    const std::size_t orig_size = buf.size();

    // Save a copy to compare untouched regions.
    auto before = buf;

    bool result = mask_tag554_same_length_inplace(std::span<std::byte>{buf});

    EXPECT_TRUE(result) << "expected true: at least one genuine 554 field found";

    // (a) The value bytes [after "554=", before the trailing SOH] must be '*'.
    // Offset breakdown: \x01(0), 5(1), 5(2), 4(3), =(4), v0(5)..v5(10), \x01(11)
    // Value extent: indices 5..10 inclusive (6 bytes).
    EXPECT_TRUE(all_stars(buf.data() + 5, buf.data() + 11))
        << "value extent must be entirely '*'";

    // (b) frame.size() unchanged; no byte outside the value extent changed.
    EXPECT_EQ(buf.size(), orig_size) << "span extent must not change";
    // Bytes before the value (the \x01554= prefix) unchanged.
    for (std::size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(buf[i], before[i]) << "byte " << i << " (prefix) must be unchanged";
    }
    // Byte after the value (the trailing SOH at index 11) unchanged.
    EXPECT_EQ(buf[11], before[11]) << "trailing SOH must be unchanged";

    // Confirm star count equals original value length.
    std::size_t star_count = 0;
    for (std::size_t i = 5; i < 11; ++i) {
        if (buf[i] == std::byte{0x2A}) ++star_count;
    }
    EXPECT_EQ(star_count, 6u) << "must have exactly 6 stars (same length as 'secret')";
}

TEST(Masker_SameLength_FieldAnchored_unit, B_FrameSizeUnchanged_NoByteOutsideValueModified) {
    // Full FIX-like frame with 554 in the middle.
    // "8=FIX.4.2\x0149=S\x01554=pass\x0156=T\x01"
    auto buf = make_buf("8=FIX.4.2\x01""49=S\x01""554=pass\x01""56=T\x01");
    auto before = buf;
    const std::size_t orig_size = buf.size();

    bool result = mask_tag554_same_length_inplace(std::span<std::byte>{buf});
    EXPECT_TRUE(result) << "genuine 554 field present → must return true";

    EXPECT_EQ(buf.size(), orig_size);

    // Locate the 554 field in the raw source to find value offset.
    // "8=FIX.4.2\x01" = 10 bytes (indices 0..9), "49=S\x01" = 5 bytes (10..14;
    // the SOH is at index 14), then "554=" at 15..18, "pass" at 19..22, SOH at 23.
    // Value extent: indices 19..22 inclusive (4 bytes).
    const std::size_t val_start = 19;
    const std::size_t val_end   = 23;  // exclusive

    for (std::size_t i = 0; i < orig_size; ++i) {
        if (i >= val_start && i < val_end) {
            EXPECT_EQ(buf[i], std::byte{0x2A})
                << "value byte at index " << i << " must be '*'";
        } else {
            EXPECT_EQ(buf[i], before[i])
                << "non-value byte at index " << i << " must be unchanged";
        }
    }
}

TEST(Masker_SameLength_FieldAnchored_unit, C_DecoyInFreeTextUntouched) {
    // Frame where "554=" appears inside tag 58 (free text), not as a real field.
    // The decoy is: \x0158=foo554=bar\x01
    // No real \x01554= or leading "554=" exists.
    auto buf = make_buf("\x01""58=foo554=bar\x01""49=S\x01");
    auto before = buf;

    bool result = mask_tag554_same_length_inplace(std::span<std::byte>{buf});

    EXPECT_FALSE(result) << "no genuine 554 field: must return false";
    // Every byte must be unchanged.
    for (std::size_t i = 0; i < buf.size(); ++i) {
        EXPECT_EQ(buf[i], before[i]) << "byte " << i << " must be unchanged";
    }
}

TEST(Masker_SameLength_FieldAnchored_unit, D_Idempotent_SecondPassByteStable) {
    // (I-E1-2): masking an already-masked frame is a no-op.
    auto buf = make_buf("\x01""554=abc\x01");
    auto before_first = buf;

    bool r1 = mask_tag554_same_length_inplace(std::span<std::byte>{buf});
    EXPECT_TRUE(r1) << "first pass: must return true (field found)";

    // Capture after first mask.
    auto after_first = buf;

    bool r2 = mask_tag554_same_length_inplace(std::span<std::byte>{buf});
    EXPECT_TRUE(r2) << "second pass: must return true (field still found — '***')";

    // Byte-stability: second pass produces exactly the same bytes as after first.
    for (std::size_t i = 0; i < buf.size(); ++i) {
        EXPECT_EQ(buf[i], after_first[i])
            << "byte " << i << " must be byte-stable after second mask";
    }

    // Sanity: the value region is still all stars.
    // \x01(0), 5(1), 5(2), 4(3), =(4), v0(5), v1(6), v2(7), \x01(8)
    EXPECT_TRUE(all_stars(buf.data() + 5, buf.data() + 8))
        << "value must remain all stars after second pass";
}

TEST(Masker_SameLength_FieldAnchored_unit, E_NoGenuine554_ReturnsFalse_NoChange) {
    // Frame with no 554 field at all.
    auto buf = make_buf("8=FIX.4.2\x01""35=A\x01""49=SENDER\x01");
    auto before = buf;

    bool result = mask_tag554_same_length_inplace(std::span<std::byte>{buf});

    EXPECT_FALSE(result) << "no 554 field: must return false";
    for (std::size_t i = 0; i < buf.size(); ++i) {
        EXPECT_EQ(buf[i], before[i]) << "byte " << i << " must be unchanged";
    }
}

TEST(Masker_SameLength_FieldAnchored_unit, F_EmptyValue_ReturnsTrueByteUnchanged) {
    // Empty value: \x01554=\x01 — zero value bytes. Must return true; no bytes
    // are overwritten (zero-length overwrite), but the field was found (I-E1).
    auto buf = make_buf("\x01""554=\x01""49=S\x01");
    auto before = buf;

    bool result = mask_tag554_same_length_inplace(std::span<std::byte>{buf});

    EXPECT_TRUE(result) << "empty value: field was found, must return true";
    // Frame must be byte-for-byte unchanged (nothing to overwrite).
    for (std::size_t i = 0; i < buf.size(); ++i) {
        EXPECT_EQ(buf[i], before[i])
            << "byte " << i << " must be unchanged (empty value)";
    }
}

TEST(Masker_SameLength_FieldAnchored_unit, G_FrameStartOccurrence_Masked) {
    // 554 at offset 0 (no leading SOH): "554=secret\x01rest"
    auto buf = make_buf("554=secret\x01""49=S\x01");
    auto before = buf;

    bool result = mask_tag554_same_length_inplace(std::span<std::byte>{buf});

    EXPECT_TRUE(result) << "offset-0 554 field: must return true";

    // "554=" is 4 bytes (indices 0..3), value is indices 4..9 (6 bytes "secret"),
    // then SOH at index 10.
    EXPECT_TRUE(all_stars(buf.data() + 4, buf.data() + 10))
        << "value extent must be all '*'";

    // Prefix "554=" (indices 0..3) and SOH (index 10) unchanged.
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(buf[i], before[i]) << "prefix byte " << i << " must be unchanged";
    }
    EXPECT_EQ(buf[10], before[10]) << "trailing SOH must be unchanged";
}

}  // namespace
