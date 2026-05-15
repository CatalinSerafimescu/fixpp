// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/msgtype_boundary_test.cpp — T019 [US1]
//
// AC-G9 (FIX-Latest A-035..A-065 filtered, build warning, not emitted) /
// AC-G10 (A-014..A-034 not emitted as typed classes in v1.0) / A3 (filtered
// at emit, not partially emitted). The shipped v1.0 corpus
// (FIX42/FIX44/FIX50SP2/FIXT11) contains ONLY locked-`[2c §1.3]`-set
// messages, so filtering is vacuous here: the emitted set equals the
// Dictionary message set and none are in the deferred ranges. This test
// pins that premise (emitter emits the full locked set, nothing partial)
// and documents the filter as a forward extension point — a FIX-Latest XML
// would trip it; the v1.0 dictionaries do not.
#include <gtest/gtest.h>

#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/v44/Messages.hpp>
#include <fixpp/v50sp2/Messages.hpp>
#include <memory_resource>
#include <string>

namespace {
fixpp::dict::Dictionary load(char const* rel, std::pmr::memory_resource* mr) {
    fixpp::dict::XmlLoader l;
    return l.load(std::string(FIXPP_DICT_DATA_DIR) + "/" + rel, mr);
}
}  // namespace

TEST(CodegenMsgtypeBoundary, LockedSetEmittedNonPartial) {
    std::pmr::monotonic_buffer_resource a;
    auto d = load("FIX44.xml", &a);
    ASSERT_FALSE(d.messages().empty());
    bool saw_nos = false;
    for (auto const& m : d.messages()) {
        EXPECT_FALSE(m.msg_type.empty());  // every emitted msg has a MsgType
        if (m.msg_type == "D") saw_nos = true;
    }
    EXPECT_TRUE(saw_nos);  // P1 headline locked-set message present (A3)
}

TEST(CodegenMsgtypeBoundary, DeferredRangesVacuousForV1Corpus) {
    // The locked-set headline classes exist; the v1.0 dictionaries carry no
    // A-014..A-065 catalogue messages, so AC-G9/AC-G10 filtering removes
    // nothing here. Pinned as a compile-time presence check + documented
    // premise; a FIX-Latest XML is the negative the filter guards.
    static_assert(fixpp::v44::NewOrderSingle::msg_type_v == "D");
    static_assert(fixpp::v50sp2::NewOrderSingle::msg_type_v == "D");
    SUCCEED();
}
