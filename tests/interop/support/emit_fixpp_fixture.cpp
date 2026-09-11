// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/support/emit_fixpp_fixture.cpp — 089 T018.
//
// fixpp's driver for the C-7 cross-language golden fixture
// (phase-9-harness/interop-readback-fixture/, read-only from this submodule
// — gitignored parent tree, contract:parent-harness-gate-contract.md). Mirrors
// emit_fixture.cpp / EmitFixture.java: invokes fixpp's own readback_jsonl.hpp
// Stream directly on the SAME constructed record those two drivers use, so
// its `readback` and `terminal` lines can be diffed against the committed
// expected.jsonl.
//
// ⚠️ NOT wired into CMakeLists.txt / ctest. expected.jsonl lives in the
// gitignored parent phase-9-harness tree, which this submodule's own CI does
// not check out — a ctest entry depending on it would be a false-green (or a
// spurious CI failure) depending on which tree happens to be present. This is
// a MANUAL verification artifact for T018's own report and for whoever picks
// up T040 (the three-way fixture extension owns wiring this — or a
// successor — into the committed fixture properly). Build/run it directly:
//
//   g++ -std=c++23 -I tests/interop/support \
//       tests/interop/support/emit_fixpp_fixture.cpp -o /tmp/fixpp_fixture
//   /tmp/fixpp_fixture /tmp/fixpp.jsonl
//   tail -n +2 /tmp/fixpp.jsonl > /tmp/fixpp_lines23.jsonl
//   tail -n +2 <path-to-parent>/interop-readback-fixture/expected.jsonl \
//       > /tmp/expected_lines23.jsonl
//   cmp /tmp/fixpp_lines23.jsonl /tmp/expected_lines23.jsonl
//
// ⚠️ Line 1 (`hello`) is EXPECTED to differ. fixpp's hello follows
// data-model.md §1a, not §1 (readback_jsonl.hpp's Stream::hello() doc
// comment) — the counterparties' hello carries `engine`/`engine_version`/
// `readback_protocol`/`dictionary_enabled`/`counterparty_digest`/
// `typed_accessor_arm`, none of which describe fixpp. Only lines 2
// (`readback`) and 3 (`terminal`) are C-7-bound across all three emitters.
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

    // The SAME field set emit_fixture.cpp / EmitFixture.java construct
    // (deliberately scrambled — README: "paths given to the emitter in
    // scrambled order so the canonical sort is load-bearing"), so the sort
    // is exercised identically across all three drivers.
    std::vector<fixpp::interop::readback::FieldEntry> f = {
        {"453[1].448", "second"},
        {"453[0].802[0].523", "nested"},
        {"453[0].448", "first"},
        {"55", std::string("quote\" back\\slash \x01 SOH")},
        {"9", "ignored-order"},
        {"355", std::string("\xff\xfe", 2)},
        {"58", "e\xcc\x81 utf8 multi-byte"},
        {"453", "2"},
        // 089 T039/T040 (C-6/C-7): ApplExtID(1156) is not a FIX 4.4 field, so it
        // never reaches a readback record via a live cell -- this constructed
        // fixture is the only place it can be exercised. It is a CANDIDATE
        // field here; the filter below must remove it before the record is
        // written, or fixpp would diverge from both counterparties on C-6.
        // ⚠️ This is a FIXTURE-ONLY restatement of the canonical partition's
        // 1156 entry (contracts/readback-jsonl.md § THE CANONICAL PARTITION),
        // hardcoded because fixpp's live walker has no general C-6 partition
        // yet (089 T038, out of scope here) — there is no production function
        // to call. The two counterparty drivers apply the identical tag
        // through their own shared cross_engine_header_delta() /
        // RECONCILED_HEADER_TAGS; this is fixpp's twin of that one entry.
        {"1156", "should-be-excluded-as-header"},
    };
    // THE CANONICAL PARTITION: applies at TOP LEVEL only (a bare numeric
    // path) -- group members are body by construction.
    f.erase(std::remove_if(f.begin(), f.end(),
                            [](fixpp::interop::readback::FieldEntry const& e) {
                                return e.path == "1156";
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
