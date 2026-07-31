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

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/validator.hpp>
#include <memory_resource>
#include <string>
#include <string_view>
#include <vector>

#include "support/frame_view_factory.hpp"

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
