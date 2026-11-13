// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/capi/test_102_dict_load_capi.cpp — 102-dict-width-multichar-type, spec FR-007 (contract
// §6, the C-ABI result classes; tasks T021a).
//
// Each cell writes its documents to a scratch directory and calls the C ABI. The documents come
// from tests/support: tests/support/dict_width_fixtures_102.hpp's generators and
// tests/support/overlay_side_table_docs_102.hpp's side-table builders, never restated here; a
// cell that needs a variant (a conflicting attribute, an id past the schema's bound) builds it
// from that text. The C++ endings of the same documents are asserted by the dictionary cells;
// these assert the C-ABI result. Every successful load's handle is destroyed with
// `fixpp_dict_destroy`, and `*out_dict` starts at a non-null sentinel so a NULL read back was
// written by the call.
//
// Classes (f) and (g) get no cell here: the handle is a pass-through of the C++ `Dictionary`
// (`fixpp_dict` holds a `shared_ptr<const Dictionary>`), so their witnesses are the C++ cells.

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>

#include "fix/c_api/dict.h"
#include "fix/c_api/error.h"
#include "overlay_merge.hpp"
#include "support/dict_width_fixtures_102.hpp"
#include "support/overlay_side_table_docs_102.hpp"
#include "support/temp_dir.hpp"

namespace {

namespace w = fixpp::test102::width;
namespace side = fixpp::test102::side;
namespace ov = fixpp::dict::detail::overlay;

// A non-null value no load returns.
fixpp_dict_t* sentinel() {
    static int storage = 0;
    return reinterpret_cast<fixpp_dict_t*>(&storage);
}

// The documents of one cell, written to a scratch directory removed on destruction.
class scratch {
public:
    scratch() : dir_(fixpp::test_support::unique_temp_dir("capi_102")) {}
    ~scratch() { (void)fixpp::test_support::try_remove_temp_dir(dir_); }
    scratch(scratch const&) = delete;
    scratch& operator=(scratch const&) = delete;
    scratch(scratch&&) = delete;
    scratch& operator=(scratch&&) = delete;

    [[nodiscard]] std::string write(std::string_view name, std::string_view text) const {
        auto const p = dir_ / std::string{name};
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        EXPECT_TRUE(out.good()) << "cannot write " << p;
        return p.string();
    }

private:
    std::filesystem::path dir_;
};

// The code `fixpp_dict_load_from_xml_with_overlay` returns for (base, overlay); asserts the
// handle is non-null exactly on FIXPP_ERR_OK and destroys it.
fixpp_error_t overlay_code(std::string_view base, std::string_view overlay) {
    scratch const s;
    std::string const b = s.write("base.xml", base);
    std::string const o = s.write("overlay.xml", overlay);
    fixpp_dict_t* out = sentinel();
    fixpp_error_t const code = fixpp_dict_load_from_xml_with_overlay(b.c_str(), o.c_str(), &out);
    if (code == FIXPP_ERR_OK) {
        EXPECT_NE(out, nullptr);
        EXPECT_NE(out, sentinel());
        fixpp_dict_destroy(out);
    } else {
        EXPECT_EQ(out, nullptr) << "a failure must leave *out_dict NULL";
    }
    return code;
}

// The code `fixpp_dict_load_from_xml` returns for `doc`; as `overlay_code`.
fixpp_error_t load_code(std::string_view doc) {
    scratch const s;
    std::string const p = s.write("dict.xml", doc);
    fixpp_dict_t* out = sentinel();
    fixpp_error_t const code = fixpp_dict_load_from_xml(p.c_str(), &out);
    if (code == FIXPP_ERR_OK) {
        EXPECT_NE(out, nullptr);
        EXPECT_NE(out, sentinel());
        fixpp_dict_destroy(out);
    } else {
        EXPECT_EQ(out, nullptr) << "a failure must leave *out_dict NULL";
    }
    return code;
}

std::string replace_all(std::string text, std::string_view from, std::string_view to) {
    for (std::size_t at = text.find(from); at != std::string::npos;
         at = text.find(from, at + to.size())) {
        text.replace(at, from.size(), to);
    }
    return text;
}

// The side-table start-push rows' documents (tests/dictionary/overlay_refusal_test.cpp).
std::string xml_start_push_overlay(std::string const& extra_components = {}) {
    return side::side_overlay(side::grow_e0_e1(side::xml_start_push_each()) + extra_components);
}
std::string orch_start_push_overlay(std::string const& extra_components = {}) {
    return side::orch_side_overlay(side::orch_grow_e0_e1() + extra_components,
                                   side::orch_start_push_parts());
}

// The Orchestra header fixture with every reference to component 70001, and its definition,
// given an id one past the schema's `maxInclusive`.
std::string header_fixture_past_the_bound() {
    std::string const& text = w::orch_header_fixture_text().text;
    std::string out = replace_all(text, "id='70001'", "id='2147483648'");
    EXPECT_NE(out, text) << "the fixture no longer declares component 70001";
    return out;
}

}  // namespace

// Class (a): an overlay whose only refusal at the parent base was the side-table fit.
TEST(DictLoadCapi102, ClassAQuickFixSideTablePushLoads) {
    EXPECT_EQ(overlay_code(side::side_table_base(), xml_start_push_overlay()), FIXPP_ERR_OK);
}

TEST(DictLoadCapi102, ClassAOrchestraSideTablePushLoads) {
    EXPECT_EQ(overlay_code(side::orch_side_table_base(), orch_start_push_overlay()), FIXPP_ERR_OK);
}

// Class (a′): the same base and overlay with a step-7 conflict added (an invalid presence
// vocabulary value on a restated element) is that conflict's code.
TEST(DictLoadCapi102, ClassAPrimeQuickFixSideTablePushWithAConflictIsTheConflictsCode) {
    std::string const conflict =
        "  <component name='Late'>\n   <field name='BeginString' required='X' />\n"
        "  </component>\n";
    EXPECT_EQ(overlay_code(side::side_table_base(), xml_start_push_overlay(conflict)),
              FIXPP_ERR_DICT_CONFIG);
}

TEST(DictLoadCapi102, ClassAPrimeOrchestraSideTablePushWithAConflictIsTheConflictsCode) {
    std::string const conflict = side::ocomp(3, "Late", side::oref("fieldRef", 8, "X"));
    EXPECT_EQ(overlay_code(side::orch_side_table_base(), orch_start_push_overlay(conflict)),
              FIXPP_ERR_DICT_CONFIG);
}

// Class (b): an Orchestra base or overlay whose ids pass 65535, on the overlay call.
TEST(DictLoadCapi102, ClassBHeaderFixtureAsABaseLoads) {
    auto const& f = w::orch_header_fixture_text();
    ASSERT_EQ(f.precondition_failure(), "");
    std::string const version_only = std::string{w::kOrchOpen} + std::string{w::kOrchClose};
    EXPECT_EQ(overlay_code(f.text, version_only), FIXPP_ERR_OK);
}

TEST(DictLoadCapi102, ClassBComponentOverlayPast65535Loads) {
    auto const& f = w::component_overlay_fixture_text();
    ASSERT_EQ(f.precondition_failure(), "");
    EXPECT_EQ(overlay_code(f.base, f.overlay), FIXPP_ERR_OK);
}

// Class (b′), budget half: a doubling chain through component ids past 65535 is the budget's
// refusal.
TEST(DictLoadCapi102, ClassBPrimeDoublingChainPast65535IsTheBudgetsRefusal) {
    auto const f = w::make_doubling_fixture(ov::kMaxNewComponents, ov::kMaxNewGroups);
    ASSERT_EQ(f.precondition_failure(ov::kExpansionBudget), "");
    EXPECT_EQ(overlay_code(f.base, f.overlay), FIXPP_ERR_DICT_LIMIT_EXCEEDED);
}

// Class (b′), cap half: restating more base components past 65535 than the new-component cap
// admits is not a new definition, so the cap does not refuse it.
TEST(DictLoadCapi102, ClassBPrimeRestatementsPast65535AreNotNewComponents) {
    auto const f = w::make_restatement_fixture(ov::kMaxNewComponents);
    ASSERT_EQ(f.precondition_failure(ov::kMaxNewComponents), "");
    EXPECT_EQ(overlay_code(f.base, f.overlay), FIXPP_ERR_OK);
}

// Class (c): the Orchestra header fixture through `fixpp_dict_load_from_xml`.
TEST(DictLoadCapi102, ClassCHeaderFixtureLoads) {
    auto const& f = w::orch_header_fixture_text();
    ASSERT_EQ(f.precondition_failure(), "");
    EXPECT_EQ(load_code(f.text), FIXPP_ERR_OK);
}

// Controls (contract §6 "Unchanged"): an id one past the schema's bound is still refused, with
// the code each call gave before.
TEST(DictLoadCapi102, ControlIdPastTheSchemaBoundIsRefusedByLoad) {
    EXPECT_EQ(load_code(header_fixture_past_the_bound()), FIXPP_ERR_CAPI_CONFIG_INVALID);
}

TEST(DictLoadCapi102, ControlIdPastTheSchemaBoundIsRefusedByTheOverlayCall) {
    std::string const version_only = std::string{w::kOrchOpen} + std::string{w::kOrchClose};
    EXPECT_EQ(overlay_code(header_fixture_past_the_bound(), version_only), FIXPP_ERR_DICT_CONFIG);
}
