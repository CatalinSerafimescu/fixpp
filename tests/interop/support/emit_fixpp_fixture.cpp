// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/support/emit_fixpp_fixture.cpp — 089 T018.
//
// fixpp's driver for the C-7 cross-language golden fixture
// (phase-9-harness/interop-readback-fixture/, read-only from this submodule
// — gitignored parent tree, contract:parent-harness-gate-contract.md). Mirrors
// emit_fixture.cpp / EmitFixture.java: invokes fixpp's own readback_jsonl.hpp
// Stream directly on the SAME constructed record those two drivers use, so
// its `sent`/`readback`/`terminal` lines can be diffed against the committed
// expected.jsonl. Its C-6 classification (which top-level tags are excluded
// from `fields`) calls the PRODUCTION function
// is_canonical_header_or_trailer_tag() in readback_jsonl.hpp, not a
// fixture-local restatement (089 T040 round-b) — 089 T052's inbound readback
// builder is meant to call the same function.
//
// ⚠️ NOT wired into CMakeLists.txt / ctest. expected.jsonl lives in the
// gitignored parent phase-9-harness tree, which this submodule's own CI does
// not check out — a ctest entry depending on it would be a false-green (or a
// spurious CI failure) depending on which tree happens to be present. This is
// a MANUAL verification artifact, run by phase-9-harness/interop-readback-
// fixture/three_way_check.sh (089 T040). Build/run it directly:
//
//   g++ -std=c++23 -I tests/interop/support \
//       tests/interop/support/emit_fixpp_fixture.cpp -o /tmp/fixpp_fixture
//   /tmp/fixpp_fixture /tmp/fixpp.jsonl
//   tail -n +2 /tmp/fixpp.jsonl > /tmp/fixpp_lines234.jsonl
//   tail -n +2 <path-to-parent>/interop-readback-fixture/expected.jsonl \
//       > /tmp/expected_lines234.jsonl
//   cmp /tmp/fixpp_lines234.jsonl /tmp/expected_lines234.jsonl
//
// ⚠️ Line 1 (`hello`) is EXPECTED to differ. fixpp's hello follows
// data-model.md §1a, not §1 (readback_jsonl.hpp's Stream::hello() doc
// comment) — the counterparties' hello carries `engine`/`engine_version`/
// `readback_protocol`/`dictionary_enabled`/`counterparty_digest`/
// `typed_accessor_arm`, none of which describe fixpp. Only lines 2-4
// (`sent`/`readback`/`terminal`) are C-7-bound across all three emitters.
#include "readback_jsonl.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <output-path>\n", argv[0]);
        return 2;
    }

    fixpp::interop::readback::Stream s(argv[1]);
    if (!s.ok()) {
        std::fprintf(stderr, "failed to open %s for writing\n", argv[1]);
        return 1;
    }

    // fixpp's own §1a hello — deliberately NOT the §1 shape emit_fixture.cpp
    // and EmitFixture.java write. See file header.
    s.hello("run-1", "cell-1", "normal", "abc", "validation-on", true, "sha256:dict-d");

    // 089 T040 round-b: `sent` is one of the records C-7 shares across all
    // three emitters (data-model §3) and shares the SAME path grammar, sort
    // and escaping rule as `readback` -- so it needs the same escape classes
    // and nested group exercised, not a smaller stand-in. Reuses the
    // identical scrambled field content `readback` below uses (minus tag
    // 1156, which is a readback-only C-6 probe, not a `sent`-record concern).
    std::vector<fixpp::interop::readback::FieldEntry> sent_f = {
        {"453[1].448", "second"},
        {"453[0].802[0].523", "nested"},
        {"453[0].448", "first"},
        {"55", std::string("quote\" back\\slash \x01 SOH")},
        // ⚠️ 089 T040 round-b: was tag "9" -- collides with BodyLength(9), a
        // genuine header field in both engines' built-in lists. Wiring in the
        // PRODUCTION is_canonical_header_or_trailer_tag() (below) correctly
        // excludes it, exposing that the counterparty fixture drivers never
        // exercised the full built-in union (only the QuickFIX-J-only delta)
        // and so never noticed the collision. Fixed at the data, not the
        // function: "11" (ClOrdID) is body on both engines and matches this
        // codebase's existing convention for a body-field probe (see
        // witness_comparator_test.cpp's ClOrdId tests). This is the reason
        // emit_fixture.cpp / EmitFixture.java (the counterparties) must make
        // the identical substitution -- see interop-readback-fixture/README.md.
        {"11", "ignored-order"},
        {"355", std::string("\xff\xfe", 2)},
        {"58", "e\xcc\x81 utf8 multi-byte"},
        {"453", "2"},
    };
    s.sent("D", 7, "fixpp-to-peer", 0, "B-01", sent_f);

    // The SAME field set emit_fixture.cpp / EmitFixture.java construct
    // (deliberately scrambled — README: "paths given to the emitter in
    // scrambled order so the canonical sort is load-bearing"), so the sort
    // is exercised identically across all three drivers.
    std::vector<fixpp::interop::readback::FieldEntry> f = {
        {"453[1].448", "second"},
        {"453[0].802[0].523", "nested"},
        {"453[0].448", "first"},
        {"55", std::string("quote\" back\\slash \x01 SOH")},
        {"11", "ignored-order"},  // was "9" -- see sent_f's comment above
        {"355", std::string("\xff\xfe", 2)},
        {"58", "e\xcc\x81 utf8 multi-byte"},
        {"453", "2"},
        // 089 T039/T040 (C-6/C-7): ApplExtID(1156) is not a FIX 4.4 field, so it
        // never reaches a readback record via a live cell -- this constructed
        // fixture is the only place it can be exercised. It is a CANDIDATE
        // field here; the filter below must remove it before the record is
        // written, or fixpp would diverge from both counterparties on C-6.
        {"1156", "should-be-excluded-as-header"},
    };
    // THE CANONICAL PARTITION: applies at TOP LEVEL only (a bare numeric
    // path) -- group members are body by construction. Calls the PRODUCTION
    // function (readback_jsonl.hpp: is_canonical_header_or_trailer_tag) --
    // not a fixture-local restatement -- so this fixture proves fixpp's
    // ACTUAL C-6 classification, the same function 089 T052's inbound
    // readback builder is meant to call.
    f.erase(std::remove_if(f.begin(), f.end(),
                            [](fixpp::interop::readback::FieldEntry const& e) {
                                bool const top_level = !e.path.empty()
                                    && std::all_of(e.path.begin(), e.path.end(),
                                                    [](char c) { return c >= '0' && c <= '9'; });
                                return top_level
                                    && fixpp::interop::readback::is_canonical_header_or_trailer_tag(
                                           std::stoi(e.path));
                            }),
            f.end());
    s.readback("D", 7, "fixpp-to-peer", 0, false, f,
               {{"55", "STRING", fixpp::interop::readback::canonical_typed_value("STRING", "AAPL")},
                {"44", "PRICE", fixpp::interop::readback::canonical_typed_value("PRICE", "190.500")},
                {"60", "UTCTIMESTAMP",
                 fixpp::interop::readback::canonical_typed_value("UTCTIMESTAMP", "20260101-00:00:00")}});

    s.terminal("completed", "run-1", "cell-1", "normal", "abc");
    return 0;
}
