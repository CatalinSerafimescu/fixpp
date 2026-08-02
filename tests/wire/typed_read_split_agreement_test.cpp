// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/typed_read_split_agreement_test.cpp
//
// 083-group-delimiter-resolution T011 (FR-021e / SC-016;
// contracts/typed_read_splitter.md C-8.0c, W-10a) — W-10a **leg 1 only**,
// observed RED (quickstart.md §2a):
//
//   35=X | 100=2 | 200=1 201=A | 200=1 201=B
//
// The SAME #208 B-2 shape T010 (tests/wire/consume_group_nested_delim_test.cpp)
// reproduces on the VALIDATION path, read back through the **read** path
// instead: `Parser<Index>{tv}.parse()` -> `offsets().group(100)`. This is a
// second scanner with the same asymmetry, in a different subsystem
// (`OffsetTable::consume_group_extent`, src/wire/offset_table.cpp:438-503):
// it consumes the instance-opening delimiter with a bare `++k` (:475) and
// descends into a nested group only for members scanned AFTER the delimiter
// (:485-488), never AT the delimiter position itself. Outer group 100
// (NoOuter) is delimited by 200 — which is itself nested group 200's
// (NoInner's) own count tag — so the second instance's opening tag (the
// second `200=1`) is never reached via a nesting-aware descent, and the
// extent walk under-reports.
//
// T020 adds legs 2 and 3 (instance count both directions; agreement with
// validation, SC-016). Leg 4 (the depth-cap early return) is T021's scope.
//
// Anchors:
//   tasks:     specs/083-group-delimiter-resolution/tasks.md T011, T020
//   quickstart: specs/083-group-delimiter-resolution/quickstart.md §2a
//   contract:  specs/083-group-delimiter-resolution/contracts/typed_read_splitter.md
//              (C-8.0c, W-10a legs 1-3)

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/offset_table.hpp>
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/validator.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/frame_view_factory.hpp"
#include "support/wire_test_hooks.hpp"  // 083 T056 (W-10): reserve-bound seam

namespace {

using fixpp::dict::table_view;
using fixpp::wire::access_mode;
using fixpp::wire::dictionary_driven_validator;
using fixpp::wire::group_slice;
using fixpp::wire::MessageView;
using fixpp::wire::Parser;

// ── Shared wire-frame helpers (mirrors consume_group_nested_delim_test.cpp) ──
std::string make_checksum_field(unsigned chk) {
    std::string s = "10=";
    s.push_back(static_cast<char>('0' + ((chk / 100U) % 10U)));
    s.push_back(static_cast<char>('0' + ((chk / 10U) % 10U)));
    s.push_back(static_cast<char>('0' + (chk % 10U)));
    s.push_back('\x01');
    return s;
}

std::vector<std::byte> make_frame(std::string_view body_fields) {
    std::string body{body_fields};
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string pre = "8=FIX.4.4\x01" + nine + body;
    unsigned sum = 0;
    for (unsigned char c : pre) {
        sum += c;
    }
    std::string full = pre + make_checksum_field(sum % 256U);
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

// ── T010's bare fixture, rebuilt here (T010's version is file-scoped to
// consume_group_nested_delim_test.cpp) — SAME bytes and SAME dictionary
// wiring, so this case and T010/T020's later leg-3 agreement check share one
// fixture. msg_type "X". Outer group NoOuter(100), delimiter NoInner(200) —
// itself the nested group NoInner's own count tag, delimiter InnerField(201).
// 201 is registered only as NoInner's member — never as a member of NoOuter
// itself (contract C-4.1's "the tags [inside the nested group] are not
// members of the outer group" framing).
table_view make_bare_nested_delim_dict() {
    table_view tv;
    for (std::uint16_t const t : {std::uint16_t{8}, std::uint16_t{9}, std::uint16_t{10},
                                  std::uint16_t{35}, std::uint16_t{100}, std::uint16_t{200},
                                  std::uint16_t{201}}) {
        tv.add_valid("X", t);
    }
    tv.set_group_first(100, 200);  // NoOuter: delimiter = NoInner's own count tag
    tv.set_group_first(200, 201);  // NoInner: delimiter = InnerField
    return tv;
}

// ── T020 (W-10a legs 2/3): the SAME shape on a POPULATED context store ──────
// A real XmlLoader-loaded dictionary, msg_type "Y", so NoInner(200) registers
// in `group_ctx_` under the non-empty parent path [100] — the exact key
// consume_group_extent's descent probe uses (`child = ctx.pushed(100)`).
// NoOuter(100) itself stays at message level, so the entry layout is
// byte-identical to the bare fixture's and one hand-derivation covers both.
//
// What this twin does and does NOT prove — recorded here rather than implied,
// because the contract's W-10a fixture wording ("a registered context") reads
// as if it buys probe-key discrimination, and it cannot:
//   * PROVES: a loader-produced dictionary really does register this shape,
//     so the descent is not an artifact of a hand-built `table_view` whose
//     `group_ctx_` is empty (table_view.hpp:346-349).
//   * Does NOT prove the probe uses the RIGHT key. `add_group_member` unions
//     with dedup into the bare store (table_view.hpp:528-536), so bare ⊇ every
//     per-context member set. A wrong key MISSES and falls back to bare, which
//     can only ever answer a false POSITIVE relative to the context answer —
//     never a false negative. W-10a's descent probe must answer TRUE, so no
//     read-path fixture of this shape can distinguish a correct key from a
//     missed one. Discriminating a wrong key needs the opposite shape (context
//     says NO, bare says YES), which is not W-10a's subject.
constexpr std::string_view kNestedDelimContextXml =
    R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
    R"(<fields>)"
    R"(<field number='8' name='BeginString' type='STRING'/>)"
    R"(<field number='9' name='BodyLength' type='INT'/>)"
    R"(<field number='10' name='CheckSum' type='STRING'/>)"
    R"(<field number='35' name='MsgType' type='STRING'/>)"
    R"(<field number='100' name='NoOuter' type='NUMINGROUP'/>)"
    R"(<field number='200' name='NoInner' type='NUMINGROUP'/>)"
    R"(<field number='201' name='InnerField' type='STRING'/>)"
    R"(</fields>)"
    R"(<messages>)"
    R"(<message name='YMsg' msgtype='Y' msgcat='app'>)"
    R"(<field name='BeginString' required='N'/>)"
    R"(<field name='BodyLength' required='N'/>)"
    R"(<field name='MsgType' required='N'/>)"
    R"(<field name='CheckSum' required='N'/>)"
    R"(<group name='NoOuter' required='N'>)"
    R"(<group name='NoInner' required='N'>)"
    R"(<field name='InnerField' required='N'/>)"
    R"(</group></group>)"
    R"(</message>)"
    R"(</messages></fix>)";

// The two #208 B-2 forms, as bytes. `MT` selects the fixture's msg_type so the
// bare ("X") and populated-store ("Y") twins share one byte layout.
std::vector<std::byte> make_two_instance_frame(char mt) {
    std::string body = "35=?\x01"
                       "100=2\x01"
                       "200=1\x01" "201=A\x01"
                       "200=1\x01" "201=B\x01";
    body[3] = mt;
    return make_frame(body);
}

std::vector<std::byte> make_one_instance_frame(char mt) {
    std::string body = "35=?\x01"
                       "100=1\x01"
                       "200=1\x01" "201=A\x01";
    body[3] = mt;
    return make_frame(body);
}

// Renders a materialized instance slice as text, so an instance BOUNDARY (the
// contract's leg-3 requirement — "the same instance boundaries", not the count
// alone, since two different splits can yield the same count) is asserted
// against hand-derived bytes rather than against a captured golden.
std::string_view slice_text(group_slice const& gs) {
    return {reinterpret_cast<char const*>(gs.data), gs.len};
}

// The hand-derived post-fix instance boundaries for the two-instance form.
// Each instance runs from the '2' of its own "200=" delimiter tag through the
// last byte of its InnerField value — the delimiter count field itself plus
// the one member of the nested group it heads. Derived from the fixture's
// bytes; NEVER captured from the implementation.
constexpr std::string_view kInstance1 = "200=1\x01" "201=A";
constexpr std::string_view kInstance2 = "200=1\x01" "201=B";

}  // namespace

// ============================================================================
// W-10a leg 1 — the extent walk's reported extent for group 100, read back
// through the typed-read path rather than validated (contracts/
// typed_read_splitter.md C-8.0c.1, W-10a leg 1).
// ============================================================================
TEST(TypedReadSplitAgreement, ExtentWalkDescendsAtNestedGroupDelimiter_Leg1ExtentByConstruction) {
    // Two instances of the #208 B-2 shape: outer group 100 declares count=2,
    // each instance opened by its delimiter 200 (itself NoInner's count tag,
    // with one nested InnerField(201) member per instance).
    auto tv = make_bare_nested_delim_dict();
    auto buf = make_frame("35=X\x01"
                          "100=2\x01"
                          "200=1\x01" "201=A\x01"
                          "200=1\x01" "201=B\x01");

    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value()) << "make_frame_view failed";

    // Dict-AWARE parser (constructed over `tv`, not the default dict-free
    // ctor) — offsets().group() only exercises consume_group_extent's
    // membership-driven descent when opaque_dict_/group_member_fn_ are
    // threaded; the dict-free fallback degrades to rest-of-message (offset_
    // table.hpp:150-157) and would not exercise the defect at all.
    Parser<access_mode::Index> parser{tv};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value()) << "parser.parse failed";

    auto gi = mv->offsets().group(100);
    ASSERT_TRUE(gi.has_value())
        << "group(100) lookup itself failed, error=" << static_cast<int>(gi.error());

    // ── Hand-derivation of the expected post-fix entry_count(), from the
    // fixture's own entry layout — NEVER captured from the implementation.
    //
    // OffsetTable::entries_ for this frame (index : tag : value):
    //   0:8=FIX.4.4  1:9=<len>  2:35=X  3:100=2  4:200=1  5:201=A
    //   6:200=1      7:201=B    8:10=<chk>
    //
    // group(100): count_idx=3 (tag 100), first=4 (tag 200, the delimiter).
    //
    // consume_group_extent(count_idx=3, ctx=root, depth=0) — POST-FIX
    // (C-8.0c.1: probe whether `delim` is itself a group in the child
    // context BEFORE consuming it with a bare ++k; if so, descend via
    // recursion rather than skipping past it):
    //
    //   Instance 1 (outer k starts at 4, delim=200):
    //     - probe: is 200 (the delimiter) itself a group under child=[100]?
    //       yes (set_group_first(200,201) registers 200 as a group whose
    //       first member is 201) -> descend via consume_group_extent(4,
    //       [100], depth=1) instead of bare ++k.
    //       - that recursion: count_idx=4 (tag 200), first=5 (tag 201,
    //         itself now the delimiter). delim'=201; declared'=1 (the
    //         value of THIS 200 field). Consumes the single instance
    //         {201=A} (entries 5), then entries_[6].tag=200 != 201 (the
    //         nested group's own delimiter) ends the nested loop.
    //         Returns k=6 -> nested extent = entries [4,6) = {200, 201} =
    //         2 entries (the delimiter itself + its one member).
    //     - k is now 6. Outer inner-loop test: entries_[6].tag(200) ==
    //       outer delim(200) -> instance 1 ends here (no further scalar
    //       members after the nested descent). Instance 1 spans entries
    //       [4,6) = 2 entries.
    //   Instance 2 (outer k=6, inst=1 < declared=2, entries_[6].tag==200):
    //     - same probe/descent at k=6: consume_group_extent(6, [100],
    //       depth=1) — count_idx=6 (tag 200), first=7 (tag 201, "B").
    //       declared'=1, consumes entry 7, then entries_[8].tag=10
    //       (CheckSum) != 201 ends the nested loop; 10 is also not a
    //       member of group 200 in context [100] -> nested loop returns
    //       k=8. Nested extent = entries [6,8) = {200, 201} = 2 entries.
    //     - k is now 8. Outer inner-loop test: entries_[8].tag(10) !=
    //       outer delim(200) -> inner loop runs once more: t=10, NOT a
    //       member of group 100 under root ctx -> breaks (ends instance
    //       AND the group). Instance 2 spans entries [6,8) = 2 entries.
    //     - inst becomes 2 == declared(2) -> outer loop exits. Returns k=8.
    //
    //   group_end = 8, first = 4 -> entry_count() = group_end - first = 4.
    //
    // Per-instance arithmetic, generalised as the brief asks (delimiter
    // count field + nested extent + remaining members), per instance:
    //   1 (delimiter tag 200, itself NoInner's own count field)
    //   + 1 (NoInner's single declared member, InnerField(201))
    //   + 0 (no further outer-group scalar members follow the nested group
    //        in this fixture — NoOuter's only child is NoInner)
    //   = 2 entries/instance x 2 declared instances = 4.
    //
    // Expected POST-FIX entry_count() = 4 (spans BOTH instances' full
    // nested content: {200,201,200,201} = entries 4,5,6,7). The trailing
    // CheckSum(10) at entry 8 correctly stays OUTSIDE the group extent.
    constexpr std::size_t kExpectedPostFixEntryCount = 4;

    EXPECT_EQ(gi->entry_count(), kExpectedPostFixEntryCount)
        << "W-10a leg 1 (C-8.0c, FR-021e): group(100)'s reported extent must span ALL "
           "declared instances (hand-derived = 4 entries: {200,201,200,201}), not just the "
           "first instance's opening delimiter. TODAY this is expected RED — "
           "consume_group_extent's bare `++k` at the instance-opening delimiter (offset_"
           "table.cpp:475) never descends into the nested group headed by that same "
           "delimiter, so the second instance's opening tag is never reached. observed="
        << gi->entry_count();
}

// ============================================================================
// W-10a leg 2 — instance count, BOTH directions (contracts/typed_read_
// splitter.md W-10a leg 2, C-8.0c.1). `group_slices_status(no_tag)` yields
// N = `declared` for the two-instance form, and the ONE-instance form still
// yields exactly 1 — the regression guard, since single-instance groups are
// why this defect stayed invisible (contracts/consume_group.md, Problem).
// ============================================================================
TEST(TypedReadSplitAgreement, ExtentWalkDescendsAtNestedGroupDelimiter_Leg2InstanceCountTwoUp) {
    auto tv = make_bare_nested_delim_dict();
    auto buf = make_two_instance_frame('X');

    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value()) << "make_frame_view failed";
    Parser<access_mode::Index> parser{tv};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value()) << "parser.parse failed";

    auto const res = mv->offsets().group_slices_status(100);
    // Guard the attribution: a bad_alloc degrade returns an EMPTY span with
    // alloc_failed set (offset_table.hpp:224-232). Without this, an arena
    // exhaustion would read as "0 slices" and be misattributed to C-8.0c.
    ASSERT_FALSE(res.alloc_failed)
        << "group_slices_status degraded on allocation — this case's slice count would be "
           "measuring the arena, not the extent walk.";

    // `declared` is the fixture's own 100=2. The expectation is that value,
    // not a captured count.
    constexpr std::size_t kDeclared = 2;
    ASSERT_EQ(res.slices.size(), kDeclared)
        << "W-10a leg 2 (C-8.0c, SC-016): group_slices_status(100) must materialize one slice "
           "per DECLARED instance. TODAY this is expected RED at 1 — consume_group_extent's "
           "bare `++k` at the instance-opening delimiter (offset_table.cpp:475) truncates the "
           "extent to entries [4,5), so the boundary loop (offset_table.cpp:658-660) has only "
           "the first delimiter to split on. observed=" << res.slices.size();

    // Boundaries, not merely the count: two different splits can yield the
    // same N. Hand-derived above from the fixture's bytes.
    EXPECT_EQ(slice_text(res.slices[0]), kInstance1);
    EXPECT_EQ(slice_text(res.slices[1]), kInstance2);
}

// The regression guard. NOTE its control is NOT a RED observation: the slice
// COUNT is 1 both pre- and post-fix, so this case cannot fail today. What it
// guards is a class of over-eager fixes — an unconditional descent, or one
// that double-counts the delimiter — which would split a single instance in
// two. Its second assertion (the instance's EXTENT) is a genuine post-fix
// target and IS red today.
TEST(TypedReadSplitAgreement, ExtentWalkDescendsAtNestedGroupDelimiter_Leg2OneInstanceGuard) {
    auto tv = make_bare_nested_delim_dict();
    auto buf = make_one_instance_frame('X');

    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value()) << "make_frame_view failed";
    Parser<access_mode::Index> parser{tv};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value()) << "parser.parse failed";

    auto const res = mv->offsets().group_slices_status(100);
    ASSERT_FALSE(res.alloc_failed) << "group_slices_status degraded on allocation";

    ASSERT_EQ(res.slices.size(), 1U)
        << "W-10a leg 2 regression guard: the ONE-instance form must still yield exactly one "
           "slice. A fix that descends unconditionally, or that consumes the delimiter twice, "
           "would split this single instance. observed=" << res.slices.size();

    // Post-fix target, RED today: pre-fix the extent stops after the bare
    // `++k`, so the lone slice covers "200=1" only and omits its nested member.
    EXPECT_EQ(slice_text(res.slices[0]), kInstance1)
        << "W-10a leg 2: the single instance must span its full nested extent, not just the "
           "opening delimiter.";
}

// ============================================================================
// W-10a leg 3 — agreement with validation (SC-016, FR-021b). The same bytes
// through the INBOUND path establish N; the read path must report the same N
// and the same instance boundaries.
//
// How N is established on the validation side, stated because the validator
// exposes no boundaries of its own: `consume_group` rejects when
// `actual_count != declared_count` (validator.hpp, the check after the
// instance loop). So ACCEPTANCE of a frame declaring 100=2 is exactly the
// statement "the validator walked 2 instances". The `100=3` control below
// makes that inference non-vacuous by showing acceptance really is
// count-sensitive on this fixture.
//
// Deviation from the contract's literal wording, recorded rather than
// glossed: the contract asks for "the same instance boundaries" from both
// sides. The validator has no boundary output, so the read path's boundaries
// are asserted against HAND-DERIVED bytes instead. That is a stronger
// assertion than a two-sided comparison (which could agree on a shared
// error), but it is a different one.
// ============================================================================
TEST(TypedReadSplitAgreement, ExtentWalkDescendsAtNestedGroupDelimiter_Leg3ValidationAgreement) {
    auto buf = make_two_instance_frame('X');
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value()) << "make_frame_view failed";

    // ── Validation side: N = declared = 2, established by acceptance ────────
    {
        dictionary_driven_validator v{make_bare_nested_delim_dict()};
        Parser<access_mode::Index> plain{};
        std::pmr::monotonic_buffer_resource varena;
        auto vmv = plain.parse(*fv, &varena);
        ASSERT_TRUE(vmv.has_value()) << "parser.parse failed";
        std::array<std::byte, 2048> scratch_buf{};
        std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                       std::pmr::null_memory_resource()};
        auto const r = v.validate(*vmv, &scratch_mr, nullptr);
        ASSERT_TRUE(r.has_value())
            << "leg 3 precondition: the inbound path must ACCEPT this shape (green since T017). "
               "error=" << (r.has_value() ? 0 : static_cast<int>(r.error()));
    }
    // Control — acceptance above is count-sensitive, so it really does pin
    // N = 2 rather than merely "the frame parsed".
    {
        auto bad = make_frame("35=X\x01"
                              "100=3\x01"
                              "200=1\x01" "201=A\x01"
                              "200=1\x01" "201=B\x01");
        auto bad_fv = fixpp::wire::test::make_frame_view(bad);
        ASSERT_TRUE(bad_fv.has_value()) << "make_frame_view failed";
        dictionary_driven_validator v{make_bare_nested_delim_dict()};
        Parser<access_mode::Index> plain{};
        std::pmr::monotonic_buffer_resource varena;
        auto vmv = plain.parse(*bad_fv, &varena);
        ASSERT_TRUE(vmv.has_value()) << "parser.parse failed";
        std::array<std::byte, 2048> scratch_buf{};
        std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                       std::pmr::null_memory_resource()};
        EXPECT_FALSE(v.validate(*vmv, &scratch_mr, nullptr).has_value())
            << "leg 3 control: a declared count of 3 against 2 present instances MUST be "
               "rejected, otherwise acceptance of the 100=2 frame would not establish N=2.";
    }

    // ── Read side: same N, same boundaries ──────────────────────────────────
    auto tv = make_bare_nested_delim_dict();
    Parser<access_mode::Index> parser{tv};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value()) << "parser.parse failed";

    auto const res = mv->offsets().group_slices_status(100);
    ASSERT_FALSE(res.alloc_failed) << "group_slices_status degraded on allocation";

    constexpr std::size_t kValidatedInstanceCount = 2;
    ASSERT_EQ(res.slices.size(), kValidatedInstanceCount)
        << "W-10a leg 3 (SC-016 / FR-021b): the typed-read split must report the SAME instance "
           "count the inbound validator accepted. TODAY this is expected RED — validation "
           "accepts 2 (since T017) while the extent walk still truncates to 1, which is exactly "
           "the silent instance loss C-8.0c exists to close. observed=" << res.slices.size();
    EXPECT_EQ(slice_text(res.slices[0]), kInstance1);
    EXPECT_EQ(slice_text(res.slices[1]), kInstance2);
}

// ============================================================================
// W-10a legs 2/3 on a POPULATED context store — the twin of W-1a
// (tests/wire/consume_group_nested_delim_test.cpp), on the read path. See
// kNestedDelimContextXml above for exactly what this twin does and does not
// prove; in particular it is NOT a probe-key discriminator, and the reason is
// structural.
// ============================================================================
TEST(TypedReadSplitAgreement, ExtentWalkDescendsAtNestedGroupDelimiter_PopulatedContextStore) {
    std::vector<std::byte> dict_buf(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource dict_mr{dict_buf.data(), dict_buf.size()};
    auto dict = fixpp::dict::XmlLoader{}.load_from_string(kNestedDelimContextXml, &dict_mr);
    auto tv = dict.as_table_view();

    // Fixture pin (mirrors W-1a's): the context-keyed member set for NoInner
    // (200) under NoOuter's child path [100] must genuinely live in
    // `group_ctx_`, not fall through to the bare store — otherwise this case
    // degrades into a second copy of the bare-fixture twin above.
    std::array<std::uint16_t, 1> const path100{100};
    ASSERT_NE(tv.group_member_tags("Y", path100, std::uint16_t{200}).data(),
              tv.group_member_tags(std::uint16_t{200}).data())
        << "the populated-store twin requires group_member_tags(\"Y\", [100], 200) to HIT the "
           "context store under NoInner's real parent path.";

    auto buf = make_two_instance_frame('Y');
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value()) << "make_frame_view failed";

    Parser<access_mode::Index> parser{tv};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value()) << "parser.parse failed";

    auto const res = mv->offsets().group_slices_status(100);
    ASSERT_FALSE(res.alloc_failed) << "group_slices_status degraded on allocation";

    ASSERT_EQ(res.slices.size(), 2U)
        << "W-10a legs 2/3 on a loader-produced dictionary: the descent must fire on the member "
           "sets a real XmlLoader registers, not only on a hand-built table_view. TODAY expected "
           "RED at 1. observed=" << res.slices.size();
    EXPECT_EQ(slice_text(res.slices[0]), kInstance1);
    EXPECT_EQ(slice_text(res.slices[1]), kInstance2);
}

// ============================================================================
// W-10a leg 4 — the depth cap RETURNS, rather than burning `declared` no-op
// iterations (contracts/typed_read_splitter.md C-8.0c.3, W-10a leg 4; T021).
//
// ── Why this leg cannot be observed RED, and what stands in for that ────────
// Pre-C-8.0c there is no delimiter-position descent at all, so the branch this
// leg discriminates — the `if (overflow) { return k; }` MIRROR of :489-491 —
// does not yet exist. Its control is therefore the ASSERTION ITSELF (an
// invariance that fails deterministically against an un-mirrored
// implementation), demonstrated to be discriminating by T023, which deletes
// the mirror and confirms this case goes red.
//
// ── Why the error code is NOT the discriminator ─────────────────────────────
// `overflow` is a `bool&` threaded from `group()`'s `bool overflow = false`
// (offset_table.cpp:549), so the depth-cap branch's `overflow = true` (:443)
// reaches `group()`'s check at :551-553 and `err_group_too_large` is returned
// WHETHER OR NOT the mirror is present. The mirror controls *when* the walk
// returns, never *what* it reports. The error code is asserted below because
// the contract requires it, but it decides nothing.
//
// ── The discriminator: a `group_member_fn_` invocation count ────────────────
// Supplied through the EXISTING construction-time `group_member_fn_t` seam
// (offset_table.hpp:79-80, :119-121) — a plain function pointer, so no
// production change and no new seam.
//
// The arithmetic, derived in the frame whose descent hits the cap (the frame
// at depth 15, which processes chain tag base+15): the depth-16 recursion
// returns at :442-444 BEFORE any probe call and leaves `k` unadvanced, and the
// inner member loop at :476 contributes nothing because `entries_[k].tag ==
// delim` still holds on entry. So each wasted outer iteration costs EXACTLY
// ONE further `group_member_fn_` evaluation — the delimiter-position descent
// probe. A mirrored implementation performs it once and returns; an
// un-mirrored one performs it `declared` times.
//
// The assertion is therefore INVARIANCE, not an absolute total: the same
// fixture at `declared = 2` and `declared = 8` OF THAT FRAME'S count field
// must record EQUAL invocation totals. An absolute constant would have to
// account for all 16 enclosing frames' probes and would be un-re-derivable by
// a later reader — exactly the magic number leg 1 forbids.
//
// ── Why the CAP-HITTING frame's count, and not the outermost ───────────────
// Only the cap-adjacent frame fails to advance `k`. Every enclosing frame DOES
// advance, so the un-mirrored total is invariant under their `declared` too —
// varying the outermost count would false-green. Because `k` never advances in
// the cap-hitting frame, `entries_[k].tag == delim` holds on every iteration of
// THAT frame's loop regardless of how many instances are physically present,
// which is what makes this invariance structural rather than argued.
// ============================================================================

namespace {

// Chain fixture: group tag base+i is delimited by base+(i+1)'s own count tag,
// `total_groups` levels deep, terminated by the scalar leaf base+total_groups.
// Every level therefore descends via the C-8.0c delimiter-position probe.
constexpr std::uint16_t kChainBase = 2000;
// 17 groups (base+0 .. base+16) + a leaf at base+17. The frame at depth 15
// (tag base+15) descends with depth = 16 = kMaxGroupDepth and trips the guard
// at offset_table.cpp:442-445, so THAT is the cap-hitting frame.
constexpr std::size_t kChainGroups = 17;
constexpr std::size_t kCapHittingIndex = 15;

table_view make_chain_dict() {
    table_view tv;
    for (std::uint16_t const t : {std::uint16_t{8}, std::uint16_t{9}, std::uint16_t{10},
                                  std::uint16_t{35}}) {
        tv.add_valid("Z", t);
    }
    for (std::size_t i = 0; i <= kChainGroups; ++i) {
        tv.add_valid("Z", static_cast<std::uint16_t>(kChainBase + i));
    }
    for (std::size_t i = 0; i < kChainGroups; ++i) {
        tv.set_group_first(static_cast<std::uint16_t>(kChainBase + i),
                           static_cast<std::uint16_t>(kChainBase + i + 1));
    }
    return tv;
}

// One physical instance at every level. `deep_count` is written into the
// cap-hitting frame's count field ONLY; every other level declares 1. The two
// runs below differ in exactly that one digit.
std::vector<std::byte> make_chain_frame(unsigned deep_count) {
    std::string body = "35=Z\x01";
    for (std::size_t i = 0; i < kChainGroups; ++i) {
        unsigned const c = (i == kCapHittingIndex) ? deep_count : 1U;
        body += std::to_string(static_cast<unsigned>(kChainBase + i)) + "=" + std::to_string(c) +
                "\x01";
    }
    body += std::to_string(static_cast<unsigned>(kChainBase + kChainGroups)) + "=L\x01";
    return make_frame(body);
}

// The counting wrapper. `group_member_fn_t` is a plain function pointer, so
// the tally is file-scope state rather than a capture; gtest runs this binary
// single-threaded, and it is reset at the start of each run.
std::size_t g_probe_calls = 0;

bool counting_group_member(void const* d, fixpp::wire::group_context const& ctx,
                           std::uint16_t no_tag, std::uint16_t tag) noexcept {
    ++g_probe_calls;
    // Same body as the production lambda at parser.hpp:601-612 (which is not
    // reachable from here) — context-keyed lookup with the table_view's own
    // bare fallback.
    auto const* tv = static_cast<table_view const*>(d);
    auto const members = tv->group_member_tags(
        ctx.msg_type, std::span<std::uint16_t const>{ctx.parent_path.data(), ctx.depth}, no_tag);
    for (auto const member_tag : members) {
        if (member_tag == tag) {
            return true;
        }
    }
    return false;
}

struct ChainRun {
    std::size_t probe_calls = 0;
    bool group_too_large = false;
    std::vector<fixpp::wire::OffsetTable::entry> layout;
    std::size_t cap_frame_tag_occurrences = 0;
};

ChainRun run_chain(table_view const& tv, std::vector<std::byte> const& buf,
                   std::pmr::memory_resource* mr) {
    ChainRun out;
    auto fv = fixpp::wire::test::make_frame_view(buf);
    if (!fv.has_value()) {
        ADD_FAILURE() << "make_frame_view failed";
        return out;
    }
    g_probe_calls = 0;
    fixpp::wire::OffsetTable table{*fv, mr, &tv, &counting_group_member};
    auto const gi = table.group(kChainBase);
    out.probe_calls = g_probe_calls;
    out.group_too_large =
        !gi.has_value() && gi.error() == fixpp::core::error::wire_group_too_large;
    for (auto const& e : table.entries()) {
        out.layout.push_back(e);
        if (e.tag == static_cast<std::uint16_t>(kChainBase + kCapHittingIndex)) {
            ++out.cap_frame_tag_occurrences;
        }
    }
    return out;
}

}  // namespace

TEST(TypedReadSplitAgreement, ExtentWalkDescendsAtNestedGroupDelimiter_Leg4DepthCapReturns) {
    auto const tv = make_chain_dict();
    auto const buf2 = make_chain_frame(2);
    auto const buf8 = make_chain_frame(8);

    std::pmr::monotonic_buffer_resource arena2;
    std::pmr::monotonic_buffer_resource arena8;
    auto const run2 = run_chain(tv, buf2, &arena2);
    auto const run8 = run_chain(tv, buf8, &arena8);

    // ── "Same fixture", made STRUCTURAL rather than argued ──────────────────
    // Entry LAYOUT byte-identical: same entry count, and every entry's tag,
    // value offset and value length equal. (The frame bytes themselves differ
    // in the one count digit and, consequently, in the CheckSum value — whose
    // offset and length are unchanged, which is what "layout" means here.)
    ASSERT_EQ(run2.layout.size(), run8.layout.size()) << "fixture layout differs in entry count";
    for (std::size_t i = 0; i < run2.layout.size(); ++i) {
        ASSERT_EQ(run2.layout[i].tag, run8.layout[i].tag) << "layout differs at entry " << i;
        ASSERT_EQ(run2.layout[i].offset, run8.layout[i].offset) << "layout differs at entry " << i;
        ASSERT_EQ(run2.layout[i].length, run8.layout[i].length) << "layout differs at entry " << i;
    }
    // PHYSICAL instance count unchanged: the cap-hitting frame's group is
    // present exactly once in both frames. Only its DECLARED count varies.
    ASSERT_EQ(run2.cap_frame_tag_occurrences, 1U);
    ASSERT_EQ(run8.cap_frame_tag_occurrences, 1U);
    // And the two frames really do differ in exactly one count digit: the
    // declared values written were 2 and 8.
    ASSERT_EQ(buf2.size(), buf8.size()) << "the two frames must be the same length";

    // ── Non-vacuity: the fixture must actually REACH the depth cap ──────────
    // Required by the contract as leg 4's item (d), and asserted
    // unconditionally rather than as a post-fix courtesy: without it the
    // invocation-count equality below would pass on a fixture that never
    // descends at all — which is precisely its state TODAY (pre-C-8.0c the
    // chain stops at the first delimiter, 3 probe calls, no cap reached), so
    // this assertion is RED until T022 and is what pins the shape.
    // It is NOT the discriminator: `overflow` is a `bool&` that reaches
    // group()'s check at offset_table.cpp:551-553 with or without the mirror.
    EXPECT_TRUE(run2.group_too_large)
        << "leg 4 fixture: a chain nested to kMaxGroupDepth must trip the depth guard and report "
           "err_group_too_large. TODAY this is expected RED — the delimiter-position descent does "
           "not exist yet, so the walk never gets past depth 0 and no cap is reached.";
    EXPECT_TRUE(run8.group_too_large)
        << "leg 4 fixture: the declared=8 run must reach the cap identically.";

    // ── The discriminator ───────────────────────────────────────────────────
    ASSERT_GT(run2.probe_calls, 0U)
        << "the counting wrapper recorded no invocations at all — the equality below would be "
           "vacuous. The fixture is not reaching consume_group_extent.";
    EXPECT_EQ(run2.probe_calls, run8.probe_calls)
        << "W-10a leg 4 (C-8.0c.3): once the depth cap trips, consume_group_extent must RETURN "
           "— mirroring the `if (overflow) { return k; }` at offset_table.cpp:489-491 — not burn "
           "`declared` no-op outer iterations. Each wasted iteration costs exactly one further "
           "group_member_fn_ evaluation (the delimiter-position descent probe), so an un-mirrored "
           "implementation's total GROWS with the cap-hitting frame's declared count while a "
           "mirrored one's is invariant. declared=2 -> " << run2.probe_calls
        << " calls; declared=8 -> " << run8.probe_calls << " calls.";

}

// ============================================================================
// W-10 (T056) — OUT-OF-SCOPE WIRE PROBES UNCHANGED.
//
// On a DIVERGENT context (the resolved per-context delimiter differs from the
// bare global one), exactly ONE of the four wire-side probes may move:
//
//   MOVES     `group_slices_status(no_tag)`'s split          (C-8.2 / T058)
//   UNCHANGED `consume_group_extent`'s extent bound          (C-8.0, `:454`)
//   UNCHANGED `group(no_tag)`'s group_index (no_tag, first_entry, entry_count)
//   UNCHANGED `group_slices_reserve_bound()`                 (C-8.0, `:597`)
//
// ── The pre-083 oracle is RUN, not hardcoded ────────────────────────────────
// A second `OffsetTable` is built over the SAME frame and the SAME dictionary
// with `group_delim_fn == nullptr`. That is not an approximation of pre-083:
// C-8.4 defines the null-callback path as "today's wire-derived
// `entries_[first].tag`, behaviour-preserving", so the oracle table executes
// the pre-083 splitter rule verbatim. The three unchanged probes never consult
// `group_delim_fn_` at all, so they run one shared code path in both tables —
// which is why their equality below is a real invariance check and not a
// tautology dressed up as one.
//
// ── Why the two exclusions are ASSERTED, not assumed ────────────────────────
// The oracle is faithful only if the delimiter callback is the ONLY 083 delta
// that can reach this fixture. Two other deltas could, and both are excluded
// inside the case rather than argued in prose — because without them this
// pin's passing condition would be "a defect is still present":
//
//   Exclusion 1 — the delimiter is NOT itself a registered group's count tag.
//     That is C-8.0c's entire population (FR-021 mode (c)). Inside it the
//     extent bound legitimately MOVES (T009/W-10a leg 2 pin that movement as a
//     change), so an unexcluded fixture would assert "unchanged" against a
//     value 083 deliberately changed.
//
//   Exclusion 2 — the member sets are identical pre/post-083: this context's
//     own AND every nested context the extent walk descends through. The
//     `table_view.hpp:645` injection of the global first field is what
//     pollutes a member set; on the 52 polluted contexts removing it MOVES the
//     extent, and that movement is #210 Consequence 2, pinned as a change by
//     T009. Here the injected tag is already a declared member, so the
//     injection is provably a no-op — asserted below — and the walk descends
//     through no nested context at all, so "every nested context" is the empty
//     set by construction rather than by inspection.
//
// ── Why this case is not RED-first, and what stands in for that ─────────────
// W-10's subject is an INVARIANCE (three probes must not move), which has no
// pre-fix red state to observe — pre-083 both tables are the same table. The
// RED observation is supplied IN-BAND instead, on every run rather than once
// at authoring time: the oracle below IS the pre-083 splitter, and it is
// asserted to produce 2 slices where the shipped one produces 3. That is
// exactly the assertion that would have failed before T058, executed as part
// of this case. `ASSERT_NE` on the two sizes then makes the whole case
// non-vacuous — a silent revert of C-8.2 collapses the oracle onto the
// shipped path and trips it.
//
// ── The fixture ─────────────────────────────────────────────────────────────
// `NoDivergent(100)` is declared by TWO messages with its two members in
// opposite order: D -> [201, 202] (so the global first-seen delimiter is 201),
// E -> [202, 201] (so E's per-context delimiter is 202). The frame carries
// msg_type E with the group written in D's order — precisely FR-019a class
// (b), the accepted-today / rejected-after construction order this feature
// discloses. Neither 201 nor 202 is a NumInGroup, so no descent exists.
// ============================================================================

namespace {

constexpr std::string_view kDivergentDelimXml =
    R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
    R"(<fields>)"
    R"(<field number='8' name='BeginString' type='STRING'/>)"
    R"(<field number='9' name='BodyLength' type='INT'/>)"
    R"(<field number='10' name='CheckSum' type='STRING'/>)"
    R"(<field number='35' name='MsgType' type='STRING'/>)"
    R"(<field number='100' name='NoDivergent' type='NUMINGROUP'/>)"
    R"(<field number='201' name='FieldA' type='STRING'/>)"
    R"(<field number='202' name='FieldB' type='STRING'/>)"
    R"(</fields>)"
    R"(<messages>)"
    // Declared FIRST, so 201 is the global first-seen delimiter for 100.
    R"(<message name='DMsg' msgtype='D' msgcat='app'>)"
    R"(<field name='BeginString' required='N'/>)"
    R"(<field name='BodyLength' required='N'/>)"
    R"(<field name='MsgType' required='N'/>)"
    R"(<field name='CheckSum' required='N'/>)"
    R"(<group name='NoDivergent' required='N'>)"
    R"(<field name='FieldA' required='N'/>)"
    R"(<field name='FieldB' required='N'/>)"
    R"(</group></message>)"
    // SAME group tag, SAME member set, OPPOSITE order -> E's delimiter is 202.
    R"(<message name='EMsg' msgtype='E' msgcat='app'>)"
    R"(<field name='BeginString' required='N'/>)"
    R"(<field name='BodyLength' required='N'/>)"
    R"(<field name='MsgType' required='N'/>)"
    R"(<field name='CheckSum' required='N'/>)"
    R"(<group name='NoDivergent' required='N'>)"
    R"(<field name='FieldB' required='N'/>)"
    R"(<field name='FieldA' required='N'/>)"
    R"(</group></message>)"
    R"(</messages></fix>)";

// The Parser's own membership lambda (parser.hpp:605-618), lifted to a named
// function so the PRE-083 oracle table can be constructed by hand with the
// IDENTICAL membership oracle and a null delimiter callback. Copied rather
// than shared because the Parser's is an unnamed closure with no other
// accessor; any divergence between the two would show up as the three
// unchanged probes disagreeing, which is the very thing this case asserts.
bool divergent_member_fn(void const* d, fixpp::wire::group_context const& ctx,
                         std::uint16_t no_tag, std::uint16_t tag) noexcept {
    auto const members = static_cast<table_view const*>(d)->group_member_tags(
        ctx.msg_type, std::span<std::uint16_t const>{ctx.parent_path.data(), ctx.depth}, no_tag);
    for (auto const member_tag : members) {
        if (member_tag == tag) {
            return true;
        }
    }
    return false;
}

// msg_type E, group written in D's (global) member order, two instances.
std::vector<std::byte> make_divergent_frame() {
    return make_frame("35=E\x01"
                      "100=2\x01"
                      "201=A\x01" "202=x\x01"
                      "201=B\x01" "202=y\x01");
}

}  // namespace

TEST(TypedReadSplitAgreement, OutOfScopeWireProbesUnchanged) {
    std::vector<std::byte> dict_buf(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource dict_mr{dict_buf.data(), dict_buf.size()};
    auto dict = fixpp::dict::XmlLoader{}.load_from_string(kDivergentDelimXml, &dict_mr);
    auto tv = dict.as_table_view();

    std::span<std::uint16_t const> const root_path{};

    // ── Precondition: the context really is DIVERGENT ───────────────────────
    ASSERT_EQ(tv.group_first_field(std::uint16_t{100}), 201U)
        << "fixture precondition: DMsg is declared first, so the bare global first-seen delimiter "
           "for NoDivergent(100) must be FieldA(201).";
    ASSERT_EQ(tv.group_first_field("E", root_path, std::uint16_t{100}), 202U)
        << "fixture precondition: EMsg declares FieldB(202) first, so E's per-context delimiter "
           "must be 202. Equal to 201 here would mean the context store MISSED and fell through "
           "to the bare global — a non-divergent fixture, on which this whole case is vacuous.";

    // ── Exclusion 1: the delimiter is NOT a registered group's count tag ────
    // C-8.0c's population is exactly the complement of this. Both candidate
    // delimiters are checked, globally and in-context, so the fixture cannot
    // drift into mode (c) by a later dictionary edit.
    for (std::uint16_t const t : {std::uint16_t{201}, std::uint16_t{202}}) {
        ASSERT_EQ(tv.group_first_field(t), 0U)
            << "exclusion 1: tag " << t << " must not be a group globally.";
        ASSERT_EQ(tv.group_first_field("E", root_path, t), 0U)
            << "exclusion 1: tag " << t << " must not be a group in context E either — if it "
               "were, C-8.0c's delimiter-position descent would fire and the extent bound this "
               "case asserts UNCHANGED is one 083 deliberately moves (T009 / W-10a leg 2).";
    }

    // ── Exclusion 2: member sets identical pre/post-083 ─────────────────────
    auto const ctx_members = tv.group_member_tags("E", root_path, std::uint16_t{100});
    auto const bare_members = tv.group_member_tags(std::uint16_t{100});
    // (a) The context store is genuinely populated for this key — otherwise
    //     the comparison below is a span against itself.
    ASSERT_NE(ctx_members.data(), bare_members.data())
        << "exclusion 2: group_member_tags(\"E\", [], 100) must HIT the context store.";
    // (b) Same member SET (order is the delimiter's business, not membership's).
    //     Equal sets are what makes the pre-083 walk and the post-083 walk take
    //     identical decisions at every entry.
    std::vector<std::uint16_t> ctx_sorted(ctx_members.begin(), ctx_members.end());
    std::vector<std::uint16_t> bare_sorted(bare_members.begin(), bare_members.end());
    std::sort(ctx_sorted.begin(), ctx_sorted.end());
    std::sort(bare_sorted.begin(), bare_sorted.end());
    ASSERT_EQ(ctx_sorted, bare_sorted)
        << "exclusion 2: this context must not be POLLUTED or newly registering — a member-set "
           "delta would move the extent independently of the delimiter, and that movement is "
           "#210 Consequence 2 (pinned as a CHANGE by T009), not something to assert unchanged.";
    ASSERT_EQ(ctx_sorted, (std::vector<std::uint16_t>{201, 202}))
        << "exclusion 2: the declared member set, hand-derived from the fixture XML.";
    // (c) The pre-083 `table_view.hpp:645` injection would have added the
    //     GLOBAL first field (201) to E's member set. It is already a declared
    //     member, so the injection was provably a no-op on this fixture — the
    //     precise statement of "divergent but not polluted".
    ASSERT_NE(std::find(ctx_sorted.begin(), ctx_sorted.end(), std::uint16_t{201}),
              ctx_sorted.end())
        << "exclusion 2: the global first field must already be a declared member of E's group, "
           "so the removed injection cannot have changed this member set.";
    // (d) "every nested context the extent walk descends through" is EMPTY:
    //     no member of the group heads a group under the child path [100], so
    //     neither descent site in consume_group_extent can fire.
    std::array<std::uint16_t, 1> const child_path{100};
    for (auto const member_tag : ctx_members) {
        ASSERT_EQ(tv.group_first_field("E", child_path, member_tag), 0U)
            << "exclusion 2: member " << member_tag << " must not head a nested group — the "
               "extent walk must descend through NO nested context, so the set of nested member "
               "sets to compare is empty by construction rather than by inspection.";
    }

    auto buf = make_divergent_frame();
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value()) << "make_frame_view failed";

    // ── POST-083: the shipped wiring ────────────────────────────────────────
    Parser<access_mode::Index> parser{tv};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value()) << "parser.parse failed";
    auto const& post = mv->offsets();

    // ── PRE-083 oracle: same frame, same dict, same membership oracle, NO
    //    delimiter callback (C-8.4's dict-free fallback == the pre-083 rule) ──
    std::pmr::monotonic_buffer_resource oracle_arena;
    fixpp::wire::OffsetTable pre{*fv, &oracle_arena, &tv, &divergent_member_fn, nullptr};
    ASSERT_TRUE(pre.build_status().has_value()) << "oracle table failed to build";
    // The ROOT context MessageView seeds unconditionally (parser.hpp:139-147);
    // reproduce it so the two tables differ in the delimiter callback ALONE.
    pre.set_group_context(fixpp::wire::group_context{.msg_type = "E"});

    // ── UNCHANGED probe 1: the extent bound ─────────────────────────────────
    // Read through the same entry point production uses, on the group's own
    // count-field index, under the root context.
    auto const count_idx = [&] {
        auto const entries = post.entries();
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].tag == 100U) {
                return i;
            }
        }
        return entries.size();
    }();
    ASSERT_LT(count_idx, post.entries().size()) << "fixture: NoDivergent(100) not found in frame";

    bool post_overflow = false;
    bool pre_overflow = false;
    fixpp::wire::group_context const root_ctx{.msg_type = "E"};
    auto const post_extent = post.consume_group_extent(count_idx, root_ctx, 0, post_overflow);
    auto const pre_extent = pre.consume_group_extent(count_idx, root_ctx, 0, pre_overflow);
    EXPECT_FALSE(post_overflow);
    EXPECT_FALSE(pre_overflow);
    EXPECT_EQ(post_extent, pre_extent)
        << "C-8.0: consume_group_extent's bound is membership-driven and its local `delim` stays "
           "WIRE-derived (offset_table.cpp:454). With both exclusions asserted above, 083 must "
           "not move it. pre=" << pre_extent << " post=" << post_extent;

    // ── UNCHANGED probe 2: group(no_tag)'s group_index ──────────────────────
    auto const post_gi = post.group(100);
    auto const pre_gi = pre.group(100);
    ASSERT_TRUE(post_gi.has_value()) << "group(100) failed post-083";
    ASSERT_TRUE(pre_gi.has_value()) << "group(100) failed on the oracle";
    EXPECT_EQ(post_gi->no_tag(), pre_gi->no_tag());
    EXPECT_EQ(post_gi->first_entry(), pre_gi->first_entry());
    EXPECT_EQ(post_gi->entry_count(), pre_gi->entry_count())
        << "group()'s reported extent must be unchanged: pre=" << pre_gi->entry_count()
        << " post=" << post_gi->entry_count();

    // ── UNCHANGED probe 3: the reserve bound ────────────────────────────────
    // `:597` keeps its wire-derived membership-probe role (C-8.0) — it never
    // consults a delimiter, and this feature must not perturb the reservation
    // made in the fixed 16 KiB inbound arena (research.md D-6 / C-8.0a).
    using fixpp::wire::reserve_bound_access_for_testing;
    EXPECT_EQ(reserve_bound_access_for_testing::get(post),
              reserve_bound_access_for_testing::get(pre))
        << "C-8.0a: the reserve estimator's inputs are member sets alone, so with exclusion 2 "
           "asserted it must be byte-for-byte the same reservation.";

    // ── CHANGED probe: the split ────────────────────────────────────────────
    auto const post_res = post.group_slices_status(100);
    auto const pre_res = pre.group_slices_status(100);
    ASSERT_FALSE(post_res.alloc_failed) << "post-083 splitter degraded on allocation";
    ASSERT_FALSE(pre_res.alloc_failed) << "oracle splitter degraded on allocation";

    // Hand-derived from the fixture's bytes, NEVER captured. Pre-083 splits on
    // the wire's first tag (201): two instances. Post-083 splits on E's
    // dictionary delimiter (202): the leading "201=A" is its own instance, and
    // each subsequent 202 opens the next.
    ASSERT_EQ(pre_res.slices.size(), 2U)
        << "oracle: the pre-083 wire-derived rule splits this frame at 201.";
    EXPECT_EQ(slice_text(pre_res.slices[0]), "201=A\x01" "202=x");
    EXPECT_EQ(slice_text(pre_res.slices[1]), "201=B\x01" "202=y");

    ASSERT_EQ(post_res.slices.size(), 3U)
        << "W-10: the split — and ONLY the split — must move on a divergent context. Post-083 the "
           "boundary is E's dictionary delimiter FieldB(202), not the wire's first tag. "
           "observed=" << post_res.slices.size();
    EXPECT_EQ(slice_text(post_res.slices[0]), "201=A");
    EXPECT_EQ(slice_text(post_res.slices[1]), "202=x\x01" "201=B");
    EXPECT_EQ(slice_text(post_res.slices[2]), "202=y");

    // Non-vacuity: the two splits must actually DIFFER, so a future change that
    // silently reverted C-8.2 could not leave this case green.
    ASSERT_NE(post_res.slices.size(), pre_res.slices.size())
        << "W-10 is vacuous unless the delimiter callback demonstrably changes the split on this "
           "fixture — that is the single permitted movement the three probes above bound.";
}

// ============================================================================
// W-8 / W-9 fixture (b) (T054 / T055) — RECORDED NEGATIVE RESULT, with the
// shapes tried. This is FR-021a's own escape clause exercised, not a gap.
//
// ── What C-8.5 targets ──────────────────────────────────────────────────────
// A splitter MIS-SPLIT needs the outer group's delimiter tag `D` to reappear
// INSIDE one of that group's own nested groups, so a deep `D` reads as a new
// outer instance to the flat `entries_[k].tag == delim` boundary test at
// `src/wire/offset_table.cpp:678-680`.
//
// ── Measured: that precondition occurs ZERO times ───────────────────────────
// Swept all ten shipped dictionaries under the POST-FIX (per-context)
// delimiters: for every context `(msg_type, path, no_tag)` with delimiter `D`,
// does any group nested directly inside it carry `D` as a member?
//
//   FIX40 0 · FIX41 0 · FIX42 0 · FIX43 0 · FIX44 0 · FIX50 0 · FIX50SP1 0 ·
//   FIX50SP2 0 · FIXT11 0 · Orchestra FIX Latest 0        TOTAL = 0
//
// ── And the shape is not merely absent, it is UNPARSEABLE ───────────────────
// Two synthetic dialects were built to witness the mis-split, and both are
// genuinely ambiguous on the wire rather than merely awkward:
//
//   (1) nested delimiter == outer delimiter (both 101). The nested group's own
//       instance scan cannot tell its second instance from the outer group's
//       next one. `dictionary_driven_validator` rejects the frame outright
//       (declared-vs-actual mismatch, error 38) — so the read path is never
//       reached. This is exactly the layout 072's load-time nested==parent
//       delimiter guard exists to reject.
//   (2) nested delimiter distinct (103) but `D`(101) a LATER member of the
//       nested group. The nested extent then ends only at a non-member, and
//       `D` is a member — so the nested walk swallows the next outer instance.
//       Ambiguous for the same reason, one position over.
//
// A wire layout in which a tag both opens outer instances and appears inside
// them has no unique parse. That is why no shipped dictionary contains one,
// and it is the mechanism behind the measured zero — not a coincidence of the
// current schemas.
//
// ── Consequence for C-8.5, stated rather than quietly dropped ───────────────
// C-8.5 ("boundary detection skips nested extents") is therefore NOT
// implemented, and this is a deliberate scope decision:
//
//   * its target population is empty on every shipped dictionary;
//   * the shapes that would exercise it have no well-defined parse, so a
//     "fix" would be choosing one reading of an ambiguous frame;
//   * and implemented literally it would BREAK the shape that IS reachable:
//     in FR-021 mode (c) the outer delimiter IS a nested group's count tag, so
//     skipping past the nested extent before testing the boundary would skip
//     the very tag that opens each instance — collapsing W-10a leg 2's two
//     slices back to one. C-8.0c and C-8.5 are a pair only where their
//     populations are disjoint; on mode (c) they are in direct conflict.
//
// L-063-4 / issue #180 accordingly keeps BOTH of its legs open. T071 already
// records that #180 is not closed by this feature; this note is the evidence
// for why leg 1 is not closed either.
//
// FR-021b's agreement claim is carried instead by W-10a legs 2/3, which run on
// mode (c) — the population that IS reachable, at 485 contexts.
// ============================================================================
