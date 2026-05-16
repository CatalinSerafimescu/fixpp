// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/conformance/conformance_test.cpp — T022 [US1] / seam #1/#15b
//
// AC-G12 curated must-include conformance. The behavioural round-trip
// (parse -> typed accessors -> reify -> re-serialize) is R6-blocked: the
// vendored frozen wire stub carries no frame state, so a real frame cannot
// be bound until 2b swaps in the OffsetTable body (spec §11 R6). The
// runnable US1 conformance is therefore (a) the checked-in, Gate-A-reviewed
// manifest is structurally valid and covers the AC-G12 categories, and
// (b) the representative must-include classes COMPILE and carry the right
// msg_type_v across versions (compile-time presence/shape). The nightly
// exhaustive + behavioural round-trip arrive with 2b.
#include <gtest/gtest.h>

#include <fixpp/v42/Messages.hpp>
#include <fixpp/v44/Messages.hpp>
#include <fixpp/v50sp2/Messages.hpp>
#include <fixpp/vt11/Messages.hpp>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Entry {
    std::string ver;
    std::string ident;
    std::string mt;
};

std::vector<Entry> parse_manifest() {
    std::ifstream f(FIXPP_CODEGEN_MANIFEST);
    std::vector<Entry> out;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        Entry e;
        ls >> e.ver >> e.ident >> e.mt;
        if (!e.ver.empty() && !e.ident.empty() && !e.mt.empty()) {
            out.push_back(std::move(e));
        }
    }
    return out;
}

}  // namespace

TEST(CodegenConformance, ManifestStructurallyValidAndCovered) {
    auto const m = parse_manifest();
    ASSERT_FALSE(m.empty());
    std::map<std::string, int> per_ver;
    int fixt_admin = 0;
    bool nos_v42 = false, nos_v44 = false, nos_v50 = false;
    for (auto const& e : m) {
        EXPECT_TRUE(e.ver == "v42" || e.ver == "v44" || e.ver == "v50sp2" || e.ver == "vt11")
            << e.ver;
        ++per_ver[e.ver];
        if (e.ver == "vt11") ++fixt_admin;
        if (e.ident == "NewOrderSingle" && e.ver == "v42") nos_v42 = true;
        if (e.ident == "NewOrderSingle" && e.ver == "v44") nos_v44 = true;
        if (e.ident == "NewOrderSingle" && e.ver == "v50sp2") nos_v50 = true;
    }
    EXPECT_GE(fixt_admin, 7);                    // 7 FIXT.1.1 admin MsgTypes (AC-G12)
    EXPECT_TRUE(nos_v42 && nos_v44 && nos_v50);  // P1 headline, all app versions
}

// Compile-time presence/shape pin of representative must-include classes
// (a template that dropped any would fail to compile here — seam #1).
TEST(CodegenConformance, MustIncludeClassesPresent) {
    static_assert(fixpp::vt11::Heartbeat::msg_type_v == "0");
    static_assert(fixpp::vt11::TestRequest::msg_type_v == "1");
    static_assert(fixpp::vt11::ResendRequest::msg_type_v == "2");
    static_assert(fixpp::vt11::Reject::msg_type_v == "3");
    static_assert(fixpp::vt11::SequenceReset::msg_type_v == "4");
    static_assert(fixpp::vt11::Logout::msg_type_v == "5");
    static_assert(fixpp::vt11::Logon::msg_type_v == "A");
    static_assert(fixpp::v42::NewOrderSingle::msg_type_v == "D");
    static_assert(fixpp::v44::NewOrderSingle::msg_type_v == "D");
    static_assert(fixpp::v50sp2::NewOrderSingle::msg_type_v == "D");
    static_assert(fixpp::v44::ExecutionReport::msg_type_v == "8");
    static_assert(fixpp::v50sp2::ExecutionReport::msg_type_v == "8");
    SUCCEED();
}
