// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/fuzz/fuzz_wire_nested_slice.cpp
//
// 062 T024b — Seam #11 extension — libFuzzer harness for the NEW input
// shape 062 introduces: a NON-ENVELOPED, mid-frame slice-scoped `{data,
// len+1}` byte range fed into `OffsetTable::build_nested_subview()` (T005)
// via the dict-aware nested-build entry point `OffsetTable::nested_group_
// slices()` (T006, `src/wire/offset_table.cpp:527/554`). The existing
// fuzz_wire_parser.cpp only ever generates FULL, checksum-terminated frames
// (via the `frame_view_access` friend factory) — it never drives this
// slice-scoped shape, which skips frame envelope validation entirely and
// hands raw interior bytes straight to the dict-aware OffsetTable ctor.
//
// Harness shape: a FIXED, deterministic, minimal valid root frame builds a
// root OffsetTable (so 100% of fuzzer entropy targets the path under test,
// not incidental root-parse rejects). The fuzzer bytes are then split so the
// LAST byte plays the role RC1 assigns to `data[len]` (the byte immediately
// following a real slice, which production callers guarantee is already a
// valid in-bounds byte of the parent frame buffer — the terminal SOH of a
// well-formed frame) and the REMAINING bytes are the slice content itself.
// This preserves the memory-safety invariant every real caller upholds
// (`data+len` is always in-bounds) while making the CONTENT of both the
// slice and that trailing byte fully adversarial — exactly the "malformed
// mid-frame slice" shape T024b asks to be fuzzed (a slice whose trailing
// byte is NOT actually SOH exercises the `end>=n || buf[end]!=SOH` guard's
// reject arm; one that IS SOH exercises the accept arm with arbitrary
// interior content).
//
// Invariants asserted (same class as fuzz_wire_parser.cpp):
//   - No crash/OOB (ASan).
//   - No UB (UBSan).
//   - Bounded memory: the per-message arena is a fixed-size stack buffer
//     with a null upstream resource, so any DoS-cap breach or runaway
//     allocation degrades to a defined wire_* / empty-span result rather
//     than growing unbounded.
//   - nested_group_slices()/build_nested_subview() are noexcept — no
//     exception may escape.
//
// Campaign note: mirrors fuzz_wire_parser.cpp's T050 precedent — a full
// ≥10-min Tier-1 ASan+UBSan campaign is the CI/T055 responsibility; the
// in-PR/local campaign here (if FIXPP_BUILD_FUZZ is enabled locally) is a
// short compile+smoke-iteration run, recorded in the phase-4 062 doc.
//
// 063 T024 — nesting-aware-walk (Defect B, consume_group_extent) extension:
// build_nested_subview() constructs a fresh dict-aware OffsetTable over the
// fuzzer-controlled slice bytes, and nested_group_slices() -> group_slices()
// -> group() on that sub-table already drives consume_group_extent's
// recursive walk over 100% adversarial content (`always_group_member`
// accepts every tag as a member of every group, so ANY "TAG=value<SOH>"
// pair the fuzzer happens to produce is treated as heading a nested group —
// exercising malformed/short declared counts via arbitrary digit runs, and
// deep nesting up to the K=16 depth cap via repeated adjacent count/delim
// pairs, [const Art VII §7]). This extension adds:
//   (a) a THIRD `nested_group_slices` call with a different `nested_no_tag`
//       over the SAME slice, widening which byte position is treated as the
//       count-field entry point per input;
//   (b) a DETERMINISTIC zero-count exposer (a fixed "900=0<SOH>" prefix
//       ahead of the fuzzer-controlled suffix) so the declared-count==0
//       short-circuit (B-004-7) is guaranteed exercised every run rather
//       than left to coverage-guided mutation to discover by chance.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fixpp/wire/group_view.hpp>  // group_context (063 T003)
#include <fixpp/wire/offset_table.hpp>
#include <fixpp/wire/view.hpp>
#include <memory_resource>
#include <span>

#include "support/frame_view_factory.hpp"

namespace {

// A permissive group-member function: treats every tag as a member of every
// group. This maximizes how much of the fuzzer-controlled slice content
// build_nested_subview()'s subsequent lazy group_slices() walk touches
// (nested_group_slices() calls group_slices() on the freshly built
// sub-table before returning), without needing a real dictionary — the
// bytes under test are the slice content, not the dict membership rules.
// 063 T003: widened with an ignored `group_context const&` param to match the
// widened group_member_fn_t (Phase 2 seam — context carried-but-unused).
bool always_group_member(void const*, fixpp::wire::group_context const&, std::uint16_t,
                         std::uint16_t) noexcept {
    return true;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    using fixpp::wire::OffsetTable;

    if (size == 0) {
        return 0;  // need at least the RC1 trailing byte
    }

    // Fixed, deterministic, minimal valid root frame — not fuzzer-derived —
    // so the root OffsetTable always builds successfully and every byte of
    // fuzzer entropy is spent on the nested slice path under test.
    static constexpr char kRootFrame[] =
        "8=FIX.4.4\x01"
        "9=5\x01"
        "35=D\x01"
        "10=000\x01";
    auto root_bytes = std::span<const std::byte>{reinterpret_cast<const std::byte*>(kRootFrame),
                                                 sizeof(kRootFrame) - 1};
    auto fv_or_err = fixpp::wire::test::make_frame_view(root_bytes);
    if (!fv_or_err) {
        return 0;  // should not happen for the fixed frame; skip defensively
    }

    // Bounded, stack-backed arena (null upstream — no unbounded global-heap
    // growth on a DoS-shaped input), same pattern as fuzz_wire_parser.cpp.
    std::array<std::byte, 256 * 1024> arena_buf{};
    std::pmr::monotonic_buffer_resource arena{arena_buf.data(), arena_buf.size(),
                                              std::pmr::null_memory_resource()};

    int dict_token = 0;  // arbitrary non-null opaque_dict identity
    // Dict-aware ctor is MANDATORY on the nested-descent path (INV-G7).
    OffsetTable root{*fv_or_err, &arena, &dict_token, &always_group_member};

    // The slice-scoped input shape under test: `data[0 .. size-2]` is the
    // slice content, `data[size-1]` plays the RC1-guaranteed in-bounds
    // trailing byte (arbitrary — may or may not actually be SOH).
    auto const* slice_data = reinterpret_cast<std::byte const*>(data);
    std::size_t const slice_len = size - 1U;

    // Vary nested_no_tag with the fuzzer input so both matching (via
    // always_group_member, i.e. always) and boundary tag values (0,
    // 65535) are exercised across the corpus.
    std::uint16_t const nested_no_tag = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(data[0]) |
        (size > 1U ? (static_cast<std::uint16_t>(data[1]) << 8) : 0U));

    // 063 T008: the new context arg — carried but unused in Phase 2.
    fixpp::wire::group_context const ctx{};

    // nested_group_slices() is noexcept; any exception escape -> terminate
    // -> libFuzzer crash report.
    auto slices = root.nested_group_slices(slice_data, slice_len, nested_no_tag, &dict_token,
                                           &always_group_member,
                                           fixpp::wire::detail::generation_token{}, ctx).slices;
    (void)slices;

    // Second call with the SAME (slice, no_tag) key exercises the T006
    // build-once/fetch-cached path over the same adversarial content.
    auto slices_again = root.nested_group_slices(slice_data, slice_len, nested_no_tag, &dict_token,
                                                 &always_group_member,
                                                 fixpp::wire::detail::generation_token{}, ctx).slices;
    (void)slices_again;

    // Third call with a DIFFERENT no_tag over the same slice content widens
    // which byte position is treated as the "count field" entry point into
    // OffsetTable::consume_group_extent (063 T021/T024) — broadening entry-
    // point diversity beyond the single `nested_no_tag` derived above.
    std::uint16_t const nested_no_tag2 = static_cast<std::uint16_t>(
        size > 3U ? (static_cast<std::uint16_t>(data[2]) |
                    (static_cast<std::uint16_t>(data[3]) << 8))
                  : ~nested_no_tag);
    auto slices2 = root.nested_group_slices(slice_data, slice_len, nested_no_tag2, &dict_token,
                                            &always_group_member,
                                            fixpp::wire::detail::generation_token{}, ctx).slices;
    (void)slices2;

    // Deterministic zero-count exposer (T024): a FIXED "<no_tag>=0<SOH>"
    // prefix ahead of the fuzzer-controlled suffix guarantees the
    // declared-count==0 short-circuit (consume_group_extent's `declared ==
    // 0U` arm, B-004-7) is exercised every run, not left to coverage-guided
    // luck to discover a digit sequence that happens to parse to zero. The
    // suffix bytes remain fully adversarial (malformed/short/deep-nesting
    // content after the zero-count group).
    constexpr std::uint16_t kZeroCountTag = 900;
    constexpr char kZeroCountPrefix[] = "900=0\x01";
    std::array<std::byte, sizeof(kZeroCountPrefix) - 1 + 4096> zc_buf{};
    std::size_t const prefix_len = sizeof(kZeroCountPrefix) - 1;
    std::size_t const suffix_len = std::min(slice_len, zc_buf.size() - prefix_len);
    std::memcpy(zc_buf.data(), kZeroCountPrefix, prefix_len);
    if (suffix_len > 0) {
        std::memcpy(zc_buf.data() + prefix_len, slice_data, suffix_len);
    }
    auto zc_slices = root.nested_group_slices(zc_buf.data(), prefix_len + suffix_len, kZeroCountTag,
                                              &dict_token, &always_group_member,
                                              fixpp::wire::detail::generation_token{}, ctx).slices;
    (void)zc_slices;

    return 0;
}
