// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/capi/capi_082_group_detection_cross_path_test.cpp
//
// 082-structural-group-detection T022 [US1] RED pin (written BEFORE T023 per
// the RED-first ordering rule) -- contracts/group-detection.md K6b / FR-006.
//
// P4's *named-source* leg, which K6 does not reach: the predicate used by
// `Dictionary::as_table_view()`'s bare-store registration loop must be the
// SAME accessor the C-ABI outbound WRITE path uses
// (`Dictionary::group_first_field(t)`, unconditional/structural since
// XML-load time -- src/capi/message_write.cpp:812:
// `h->dict_->group_first_field(group_tag) == 0 => FIXPP_ERR_TYPE_MISMATCH`).
//
// Per contracts/group-detection.md C4.4: the WRITE family (this test)
// ALREADY works correctly TODAY -- `fixpp_msg_group_begin(t)` succeeds for
// FIX42's real 18 group tags right now, because `message_write.cpp` calls
// `Dictionary::group_first_field(t)` directly (unconditional, load-time
// data), NOT the datatype-gated `table_view` bare-store map that
// `as_table_view()`'s registration loops populate. This is EXACTLY the
// "divergent second structural realization" K6b exists to catch: pre-T023
// there are TWO predicates live in the codebase (structural on the write
// path, datatype-gated on the as_table_view()/READ registration path), and
// their cross-path exact-set comparison is RED *today* for that reason --
// not because the write path is broken, but because the READ-side bare
// store (T015's own bare_registered_group_tags sweep) is still empty for
// FIX42. This pin is expected to flip GREEN once T023 unifies both tiers
// onto the same predicate (`group_first_field(t) != 0` at ALL
// `as_table_view()` sites), at which point the exact-set equality below
// becomes true by construction rather than accidentally.
//
// Uses the REAL vendored dictionaries/FIX42.xml (all 18 group tags), not a
// synthetic inline dict -- a synthetic subset would not exercise the real
// C2 struct-set this pin cross-checks against.
//
// Anchors: tasks.md T022; contracts/group-detection.md K6b, C1.1, C4.4;
// spec.md FR-006.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <set>
#include <sstream>
#include <string>

#include "fix/c_api/engine.h"
#include "fix/c_api/message.h"
#include "fix/c_api/session.h"

#include "capi_internal.hpp"
#include "capi_loopback_support.hpp"

#include "fixpp/dict/dictionary.hpp"
#include "fixpp/dict/table_view.hpp"
#include "fixpp/dict/xml_loader.hpp"

using namespace fixpp::capi_test;

namespace {

// Build a session-config builder over the REAL vendored FIX42.xml (not the
// synthetic minimal/app dict `make_session_cfg`/`make_test_dict_handle` use
// elsewhere in this directory) -- mirrors message_write_test.cpp's
// `make_session_cfg_app_dict` PMR/shared_ptr construction shape, swapped to
// load the real file via `XmlLoader::load` (path), same pattern as
// tests/support/fix44_dictionary.hpp / tests/dictionary/
// required_scope_census_test.cpp.
fixpp_session_config_t* make_session_cfg_real_fix42(char const* sender, char const* target,
                                                     fixpp_session_role role) {
    using namespace fixpp::dict;
    constexpr std::size_t kBufSize = 8u * 1024u * 1024u;
    auto buf = std::make_unique<std::array<std::byte, kBufSize>>();
    auto* mr = new std::pmr::monotonic_buffer_resource{buf->data(), buf->size()};

    std::string const path = std::string(FIXPP_DICT_DATA_DIR) + "/FIX42.xml";
    Dictionary d = XmlLoader{}.load(path, mr);
    auto* raw_dict = new Dictionary{std::move(d)};
    auto* raw_buf = buf.release();
    auto dict_ptr = std::shared_ptr<const Dictionary>{
        raw_dict, [mr, raw_buf](const Dictionary* p) {
            delete p;
            delete mr;
            delete raw_buf;
        }};

    auto* fd = new fixpp_dict{dict_ptr};
    auto* dict_handle = reinterpret_cast<fixpp_dict_t*>(fd);

    fixpp_session_config_t* sc = nullptr;
    EXPECT_EQ(fixpp_session_config_create(&sc), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_comp_ids(sc, sender, target), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_begin_string(sc, "FIX.4.2"), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_role(sc, role), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_heartbeat_seconds(sc, 30), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_security(sc, FIXPP_SECURITY_INSECURE_PLAIN_TCP, nullptr,
                                                nullptr),
              FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_reset_on_logon(sc, role == FIXPP_ROLE_INITIATOR),
              FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_dictionary(sc, dict_handle), FIXPP_ERR_OK);
    delete fd;  // setter copied the shared_ptr; fd shell no longer needed
    return sc;
}

// T015's own bare-store sweep methodology, reproduced here (dictionary.hpp's
// table_view exposes no direct "list every registered no_tag" accessor):
// sweep the whole uint16_t tag space and collect every tag for which
// `table_view::group_first_field(t) != 0`. Mirrors
// tests/dictionary/required_scope_census_test.cpp's
// `bare_registered_group_tags` exactly (same loop shape) -- kept as a LOCAL
// copy (not a shared header) since this file lives in a different binary
// (tests/capi/) with its own link-deps; it is not asserting a DIFFERENT
// oracle, only measuring the SAME production `table_view::group_first_field`
// this feature's other pins already sweep.
std::set<std::uint16_t> bare_registered_group_tags(fixpp::dict::table_view const& tv) {
    std::set<std::uint16_t> tags;
    for (std::uint32_t t = 1; t <= 0xFFFFU; ++t) {
        auto const tag = static_cast<std::uint16_t>(t);
        if (tv.group_first_field(tag) != 0) {
            tags.insert(tag);
        }
    }
    return tags;
}

std::string describe_diff(std::set<std::uint16_t> const& expected, std::set<std::uint16_t> const& actual) {
    std::ostringstream oss;
    oss << "missing-from-actual{";
    for (auto t : expected) {
        if (!actual.contains(t)) oss << t << ",";
    }
    oss << "} extra-in-actual{";
    for (auto t : actual) {
        if (!expected.contains(t)) oss << t << ",";
    }
    oss << "}";
    return oss.str();
}

}  // namespace

// T022 [US1]: fixpp_msg_group_begin(t) succeeds for EXACTLY the bare store's
// registered tag set, both directions (FR-006 / K6b).
TEST(GroupDetectionCrossPath, WriteGroupBeginMatchesBareStoreRegisteredSetBothDirections) {
    // ── LHS: the C-ABI WRITE path's own predicate ───────────────────────────
    fixpp_engine_t* eng = nullptr;
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 1, 4, &eng), FIXPP_ERR_OK);

    fixpp_session_config_t* sc = make_session_cfg_real_fix42("SND", "TGT", FIXPP_ROLE_ACCEPTOR);
    set_loopback_endpoint(sc, "127.0.0.1", 0);
    fixpp_session_t* sess = nullptr;
    ASSERT_EQ(fixpp_session_open(eng, sc, &sess), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_start(eng), FIXPP_ERR_OK);

    // Heartbeat("0") is declared by every FIX schema and carries no group of
    // its own -- fixpp_msg_group_begin's gate is dictionary-WIDE
    // (`h->dict_->group_first_field(group_tag)`, single-arg, no msg_type
    // parameter), so which outbound msg_type hosts the probe is immaterial
    // to which tags succeed.
    fixpp_msg_t* msg = nullptr;
    ASSERT_EQ(fixpp_msg_create_outbound(sess, "0", 1, &msg), FIXPP_ERR_OK);
    ASSERT_NE(msg, nullptr);

    // Full uint16_t sweep, mirroring the READ-side bare-store sweep exactly
    // (T015's bare_registered_group_tags) -- only successful opens allocate
    // (message_write.cpp:812 returns FIXPP_ERR_TYPE_MISMATCH BEFORE any
    // arena allocation on failure), so this is cheap: only the real 18 FIX42
    // group tags ever reach the allocating path.
    std::set<std::uint16_t> write_succeeds;
    for (std::uint32_t t = 1; t <= 0xFFFFU; ++t) {
        auto const tag = static_cast<std::uint16_t>(t);
        fixpp_group_builder_t* gb = nullptr;
        fixpp_error_t const r = fixpp_msg_group_begin(msg, tag, &gb);
        if (r == FIXPP_ERR_OK) {
            write_succeeds.insert(tag);
            ASSERT_NE(gb, nullptr);
            ASSERT_EQ(fixpp_msg_group_end(msg, gb), FIXPP_ERR_OK);
        }
    }

    // ── RHS: the READ/registration path's own predicate ─────────────────────
    // Independently loaded Dictionary (own PMR arena), NOT sharing the
    // engine's dict instance -- pure measurement, no engine-internal
    // reach-through.
    constexpr std::size_t kBufSize = 8u * 1024u * 1024u;
    auto buf2 = std::make_unique<std::array<std::byte, kBufSize>>();
    std::pmr::monotonic_buffer_resource mr2{buf2->data(), buf2->size()};
    std::string const path = std::string(FIXPP_DICT_DATA_DIR) + "/FIX42.xml";
    auto const dict = fixpp::dict::XmlLoader{}.load(path, &mr2);
    auto const tv = dict.as_table_view();
    auto const bare_registered = bare_registered_group_tags(tv);

    // Sanity pin over the LHS derivation: FIX42 declares 18 real groups
    // (contracts/group-detection.md C2) -- if this count drifted, the
    // exact-set comparison below would be checking against a stale fixture,
    // not this feature's regression.
    EXPECT_EQ(write_succeeds.size(), 18u)
        << "C-ABI write-path fixpp_msg_group_begin succeeded for a tag count other than the "
           "pinned 18 (FIX42 real group tags) -- the write-family predicate itself may have "
           "regressed, independent of the 082 read-side predicate swap";

    // THE PIN: both directions. RED today -- write_succeeds (already
    // structural, 18 tags) vs bare_registered (still datatype-gated, 0 tags
    // for FIX42 pre-T023) diverge. Expected to converge once T023 unifies
    // both tiers onto Dictionary::group_first_field.
    EXPECT_EQ(write_succeeds, bare_registered)
        << "C-ABI write-path group_begin success set vs as_table_view() bare-store registered "
           "set: " << describe_diff(write_succeeds, bare_registered)
        << " -- a divergent second structural realization (K6b) is exactly what this pin exists "
           "to catch; RED pre-T023, expected GREEN post-T023";

    ASSERT_EQ(fixpp_msg_destroy(msg), FIXPP_ERR_OK);
    fixpp_engine_destroy(eng);
}
