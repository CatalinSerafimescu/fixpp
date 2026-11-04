// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/capi/capi_099_fixt_live_test.cpp
//
// 099-fixt-appl-version-validation, contracts/witnesses.md W-12 (tasks.md T092), FR-014, FR-016,
// NFR-004, SC-006: a FIXT session from pure C, on shapes T and A. A real TCP peer writes the frames
// byte for byte; the session under test is a C-ABI initiator. The setters, add-dictionary, Active,
// a routed Reject and an accept, a refused config at fixpp_session_open with the builder unconsumed,
// and fixpp_msg_version in the callback and on a clone that a second thread reads while the worker
// runs, which T134's TSan run judges. Error codes are the literals of contracts/surface.md.
//
// After the first fixpp_engine_start every check is non-fatal, so a failing check still reaches
// the closes and destroys and the binary exits with a test failure.

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fixpp/dict/version_profile.hpp>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "capi_internal.hpp"
#include "fix/c_api/dict.h"
#include "fix/c_api/engine.h"
#include "fix/c_api/error.h"
#include "fix/c_api/message.h"
#include "fix/c_api/session.h"
#include "fix/c_api/version.h"
#include "support/fixt_raw_peer.hpp"
#include "support/wait_until.hpp"

namespace {

namespace dd = fixpp::dict;
using namespace std::chrono_literals;
using fixpp::test_support::fix_utc_now;
using fixpp::test_support::fixt_frame;
using fixpp::test_support::FixtRawPeer;
using fixpp::test_support::frame_field;

constexpr auto kStep = 15s;

// ── The struct and the constants ────────────────────────────────────────────────────────

static_assert(sizeof(fixpp_resolved_msg_version_t) == 4);
static_assert(offsetof(fixpp_resolved_msg_version_t, kind) == 0);
static_assert(offsetof(fixpp_resolved_msg_version_t, session) == 1);
static_assert(offsetof(fixpp_resolved_msg_version_t, application) == 2);
static_assert(offsetof(fixpp_resolved_msg_version_t, _reserved) == 3);

static_assert(FIXPP_APPL_VER_UNKNOWN == static_cast<int>(dd::application_version::Unknown));
static_assert(FIXPP_APPL_VER_FIX40 == static_cast<int>(dd::application_version::v40));
static_assert(FIXPP_APPL_VER_FIX41 == static_cast<int>(dd::application_version::v41));
static_assert(FIXPP_APPL_VER_FIX42 == static_cast<int>(dd::application_version::v42));
static_assert(FIXPP_APPL_VER_FIX43 == static_cast<int>(dd::application_version::v43));
static_assert(FIXPP_APPL_VER_FIX44 == static_cast<int>(dd::application_version::v44));
static_assert(FIXPP_APPL_VER_FIX50 == static_cast<int>(dd::application_version::v50));
static_assert(FIXPP_APPL_VER_FIX50SP1 == static_cast<int>(dd::application_version::v50sp1));
static_assert(FIXPP_APPL_VER_FIX50SP2 == static_cast<int>(dd::application_version::v50sp2));
static_assert(FIXPP_APPL_VER_FIXLATEST == static_cast<int>(dd::application_version::vlatest));

TEST(W12Layout, FourBytesInOrder_AtRunTime) {
    EXPECT_EQ(sizeof(fixpp_resolved_msg_version_t), 4U);
    EXPECT_EQ(offsetof(fixpp_resolved_msg_version_t, kind), 0U);
    EXPECT_EQ(offsetof(fixpp_resolved_msg_version_t, session), 1U);
    EXPECT_EQ(offsetof(fixpp_resolved_msg_version_t, application), 2U);
    EXPECT_EQ(offsetof(fixpp_resolved_msg_version_t, _reserved), 3U);
}

// Each value spelled, and the enum's own read at run time, so neither side is derived from the
// other.
TEST(W12Constants, EqualTheEnums_AtRunTime) {
    const auto appl = [](dd::application_version v) { return static_cast<int>(v); };
    EXPECT_EQ(FIXPP_APPL_VER_UNKNOWN, 0);
    EXPECT_EQ(appl(dd::application_version::Unknown), 0);
    EXPECT_EQ(FIXPP_APPL_VER_FIX40, 1);
    EXPECT_EQ(appl(dd::application_version::v40), 1);
    EXPECT_EQ(FIXPP_APPL_VER_FIX41, 2);
    EXPECT_EQ(appl(dd::application_version::v41), 2);
    EXPECT_EQ(FIXPP_APPL_VER_FIX42, 3);
    EXPECT_EQ(appl(dd::application_version::v42), 3);
    EXPECT_EQ(FIXPP_APPL_VER_FIX43, 4);
    EXPECT_EQ(appl(dd::application_version::v43), 4);
    EXPECT_EQ(FIXPP_APPL_VER_FIX44, 5);
    EXPECT_EQ(appl(dd::application_version::v44), 5);
    EXPECT_EQ(FIXPP_APPL_VER_FIX50, 6);
    EXPECT_EQ(appl(dd::application_version::v50), 6);
    EXPECT_EQ(FIXPP_APPL_VER_FIX50SP1, 7);
    EXPECT_EQ(appl(dd::application_version::v50sp1), 7);
    EXPECT_EQ(FIXPP_APPL_VER_FIX50SP2, 8);
    EXPECT_EQ(appl(dd::application_version::v50sp2), 8);
    EXPECT_EQ(FIXPP_APPL_VER_FIXLATEST, 9);
    EXPECT_EQ(appl(dd::application_version::vlatest), 9);

    const auto sess = [](dd::session_version v) { return static_cast<int>(v); };
    EXPECT_EQ(FIXPP_SESSION_VER_UNKNOWN, 0);
    EXPECT_EQ(sess(dd::session_version::Unknown), 0);
    EXPECT_EQ(FIXPP_SESSION_VER_FIX40, 1);
    EXPECT_EQ(sess(dd::session_version::v40), 1);
    EXPECT_EQ(FIXPP_SESSION_VER_FIX41, 2);
    EXPECT_EQ(sess(dd::session_version::v41), 2);
    EXPECT_EQ(FIXPP_SESSION_VER_FIX42, 3);
    EXPECT_EQ(sess(dd::session_version::v42), 3);
    EXPECT_EQ(FIXPP_SESSION_VER_FIX43, 4);
    EXPECT_EQ(sess(dd::session_version::v43), 4);
    EXPECT_EQ(FIXPP_SESSION_VER_FIX44, 5);
    EXPECT_EQ(sess(dd::session_version::v44), 5);
    EXPECT_EQ(FIXPP_SESSION_VER_FIX50, 6);
    EXPECT_EQ(sess(dd::session_version::v50), 6);
    EXPECT_EQ(FIXPP_SESSION_VER_FIX50SP1, 7);
    EXPECT_EQ(sess(dd::session_version::v50sp1), 7);
    EXPECT_EQ(FIXPP_SESSION_VER_FIX50SP2, 8);
    EXPECT_EQ(sess(dd::session_version::v50sp2), 8);
    EXPECT_EQ(FIXPP_SESSION_VER_FIXT11, 9);
    EXPECT_EQ(sess(dd::session_version::vt11), 9);
    EXPECT_EQ(FIXPP_SESSION_VER_FIXLATEST, 10);
    EXPECT_EQ(sess(dd::session_version::vlatest), 10);

    EXPECT_EQ(FIXPP_MSG_KIND_SESSION_ADMIN, 0);
    EXPECT_EQ(FIXPP_MSG_KIND_APPLICATION, 1);
    EXPECT_EQ(static_cast<int>(dd::resolved_message_version::kind::session_admin), 0);
    EXPECT_EQ(static_cast<int>(dd::resolved_message_version::kind::application), 1);
}

// ── The setters' table, contracts/surface.md ────────────────────────────────────────────

TEST(W12Setters, DefaultApplVerId_RefusesWhatTheTableRefuses) {
    EXPECT_EQ(fixpp_session_config_set_default_appl_ver_id(nullptr, "9"), FIXPP_ERR_NULL_HANDLE);
    fixpp_session_config_t* sc = nullptr;
    ASSERT_EQ(fixpp_session_config_create(&sc), FIXPP_ERR_OK);
    auto* internal = reinterpret_cast<fixpp_session_config*>(sc);
    EXPECT_EQ(fixpp_session_config_set_default_appl_ver_id(sc, nullptr),
              FIXPP_ERR_CAPI_CONFIG_INVALID);
    for (const char* bad : {"", "0", "1", "11", "abc", "9x", " 9"}) {
        EXPECT_EQ(fixpp_session_config_set_default_appl_ver_id(sc, bad),
                  FIXPP_ERR_CAPI_CONFIG_INVALID)
            << "\"" << bad << "\"";
    }
    EXPECT_FALSE(internal->cfg.default_appl_ver_id.has_value());
    fixpp_session_config_destroy(sc);
}

TEST(W12Setters, DefaultApplVerId_EachWireValueStoresItsVersion) {
    const std::vector<std::pair<const char*, dd::application_version>> wire = {
        {"2", dd::application_version::v40},    {"3", dd::application_version::v41},
        {"4", dd::application_version::v42},    {"5", dd::application_version::v43},
        {"6", dd::application_version::v44},    {"7", dd::application_version::v50},
        {"8", dd::application_version::v50sp1}, {"9", dd::application_version::v50sp2},
        {"10", dd::application_version::vlatest},
    };
    fixpp_session_config_t* sc = nullptr;
    ASSERT_EQ(fixpp_session_config_create(&sc), FIXPP_ERR_OK);
    auto* internal = reinterpret_cast<fixpp_session_config*>(sc);
    for (const auto& [value, version] : wire) {
        EXPECT_EQ(fixpp_session_config_set_default_appl_ver_id(sc, value), FIXPP_ERR_OK) << value;
        EXPECT_EQ(internal->cfg.default_appl_ver_id, std::optional{version}) << value;
    }
    fixpp_session_config_destroy(sc);
}

TEST(W12Setters, DefaultApplVerId_ALaterValidCallReplaces_ARejectedCallLeaves) {
    fixpp_session_config_t* sc = nullptr;
    ASSERT_EQ(fixpp_session_config_create(&sc), FIXPP_ERR_OK);
    auto* internal = reinterpret_cast<fixpp_session_config*>(sc);
    EXPECT_EQ(fixpp_session_config_set_default_appl_ver_id(sc, "9"), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_default_appl_ver_id(sc, "10"), FIXPP_ERR_OK);
    EXPECT_EQ(internal->cfg.default_appl_ver_id, std::optional{dd::application_version::vlatest});
    EXPECT_EQ(fixpp_session_config_set_default_appl_ver_id(sc, "abc"),
              FIXPP_ERR_CAPI_CONFIG_INVALID);
    EXPECT_EQ(internal->cfg.default_appl_ver_id, std::optional{dd::application_version::vlatest});
    fixpp_session_config_destroy(sc);
}

TEST(W12Setters, ValidateInboundMessages_WritesTheField) {
    EXPECT_EQ(fixpp_session_config_set_validate_inbound_messages(nullptr, true),
              FIXPP_ERR_NULL_HANDLE);
    fixpp_session_config_t* sc = nullptr;
    ASSERT_EQ(fixpp_session_config_create(&sc), FIXPP_ERR_OK);
    auto* internal = reinterpret_cast<fixpp_session_config*>(sc);
    EXPECT_FALSE(internal->cfg.validate_inbound_messages);
    EXPECT_EQ(fixpp_session_config_set_validate_inbound_messages(sc, true), FIXPP_ERR_OK);
    EXPECT_TRUE(internal->cfg.validate_inbound_messages);
    EXPECT_EQ(fixpp_session_config_set_validate_inbound_messages(sc, false), FIXPP_ERR_OK);
    EXPECT_FALSE(internal->cfg.validate_inbound_messages);
    fixpp_session_config_destroy(sc);
}

// ── add-dictionary ──────────────────────────────────────────────────────────────────────

std::string dict_path(std::string_view relative) {
    return (std::filesystem::path{FIXPP_DICT_DATA_DIR} / relative).string();
}

fixpp_dict_t* load_dict(std::string_view relative) {
    const std::string path = dict_path(relative);
    fixpp_dict_t* d = nullptr;
    EXPECT_EQ(fixpp_dict_load_from_xml(path.c_str(), &d), FIXPP_ERR_OK) << path;
    return d;
}

// One load of each vendored file for the process: the handles' dictionaries are immutable.
fixpp_dict_t* fixt11() {
    static fixpp_dict_t* const d = load_dict("FIXT11.xml");
    return d;
}
fixpp_dict_t* fix50sp2() {
    static fixpp_dict_t* const d = load_dict("FIX50SP2.xml");
    return d;
}
fixpp_dict_t* latest() {
    static fixpp_dict_t* const d = load_dict("orchestra/OrchestraFIXLatest.xml");
    return d;
}

TEST(W12AddDictionary, TheHandleErrorsAreSetDictionarys) {
    fixpp_engine_config_t* ec = nullptr;
    ASSERT_EQ(fixpp_engine_config_create(&ec), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_engine_config_add_dictionary(nullptr, fixt11()), FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(fixpp_engine_config_add_dictionary(ec, nullptr), FIXPP_ERR_CAPI_CONFIG_INVALID);

    fixpp_dict empty_wrapper{nullptr};
    EXPECT_EQ(fixpp_engine_config_add_dictionary(ec, reinterpret_cast<fixpp_dict_t*>(&empty_wrapper)),
              FIXPP_ERR_CAPI_CONFIG_INVALID);

    fixpp_dict_t* destroyed = load_dict("FIXT11.xml");
    fixpp_dict_destroy(destroyed);
    EXPECT_EQ(fixpp_engine_config_add_dictionary(ec, destroyed), FIXPP_ERR_INVALID_HANDLE);

    // Another handle type, cast: the tag gate answers before the shared_ptr is read.
    EXPECT_EQ(fixpp_engine_config_add_dictionary(ec, reinterpret_cast<fixpp_dict_t*>(ec)),
              FIXPP_ERR_INVALID_HANDLE);

    EXPECT_TRUE(reinterpret_cast<fixpp_engine_config*>(ec)->dictionaries.empty());
    fixpp_engine_config_destroy(ec);
}

TEST(W12AddDictionary, SeveralCallsAccumulateInOrder) {
    fixpp_engine_config_t* ec = nullptr;
    ASSERT_EQ(fixpp_engine_config_create(&ec), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_engine_config_add_dictionary(ec, fix50sp2()), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_engine_config_add_dictionary(ec, fixt11()), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_engine_config_add_dictionary(ec, fix50sp2()), FIXPP_ERR_OK);
    const auto& list = reinterpret_cast<fixpp_engine_config*>(ec)->dictionaries;
    ASSERT_EQ(list.size(), 3U);
    EXPECT_EQ(list[0], reinterpret_cast<fixpp_dict*>(fix50sp2())->dict);
    EXPECT_EQ(list[1], reinterpret_cast<fixpp_dict*>(fixt11())->dict);
    EXPECT_EQ(list[2], reinterpret_cast<fixpp_dict*>(fix50sp2())->dict);
    fixpp_engine_config_destroy(ec);
}

// ── Shapes ──────────────────────────────────────────────────────────────────────────────

enum class Shape { T, A };

const char* shape_name(Shape s) { return s == Shape::T ? "T" : "A"; }

fixpp_dict_t* shape_dict(Shape s) { return s == Shape::T ? fixt11() : fix50sp2(); }

fixpp_session_config_t* fixt_config(Shape s, std::uint16_t port) {
    fixpp_session_config_t* sc = nullptr;
    EXPECT_EQ(fixpp_session_config_create(&sc), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_comp_ids(sc, "ISLD", "TW"), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_begin_string(sc, "FIXT.1.1"), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_role(sc, FIXPP_ROLE_INITIATOR), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_heartbeat_seconds(sc, 30), FIXPP_ERR_OK);
    EXPECT_EQ(
        fixpp_session_config_set_security(sc, FIXPP_SECURITY_INSECURE_PLAIN_TCP, nullptr, nullptr),
        FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_tcp_endpoint(sc, "127.0.0.1", port), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_dictionary(sc, shape_dict(s)), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_default_appl_ver_id(sc, "9"), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_validate_inbound_messages(sc, true), FIXPP_ERR_OK);
    return sc;
}

fixpp_engine_t* engine_over(std::initializer_list<fixpp_dict_t*> dicts) {
    fixpp_engine_config_t* ec = nullptr;
    EXPECT_EQ(fixpp_engine_config_create(&ec), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_engine_config_set_realtime_clock(ec), FIXPP_ERR_OK);
    for (fixpp_dict_t* d : dicts) {
        EXPECT_EQ(fixpp_engine_config_add_dictionary(ec, d), FIXPP_ERR_OK);
    }
    fixpp_engine_t* e = nullptr;
    EXPECT_EQ(fixpp_engine_create(ec, FIXPP_C_ABI_VERSION_MAJOR, FIXPP_C_ABI_VERSION_MINOR, &e),
              FIXPP_ERR_OK);
    return e;
}

class W12Shape : public testing::TestWithParam<Shape> {};

// ── A refused config ────────────────────────────────────────────────────────────────────

// T's engine lacks the session's default slot; A's lacks a transport dictionary. Either way the
// refusal comes from fixpp_session_open, and the builder is still the caller's: the same builder
// opens on an engine that has them.
TEST_P(W12Shape, ARefusedConfig_ThreadConfig_BuilderUnconsumed) {
    const Shape shape = GetParam();
    fixpp_engine_t* refusing = engine_over({shape_dict(shape)});
    fixpp_engine_t* able = engine_over({fixt11(), fix50sp2(), latest()});
    fixpp_session_config_t* sc = fixt_config(shape, 1);
    fixpp_session_t* h = nullptr;
    const fixpp_error_t refused = fixpp_session_open(refusing, sc, &h);
    EXPECT_EQ(refused, FIXPP_ERR_THREAD_CONFIG);
    EXPECT_EQ(h, nullptr);
    if (h != nullptr) {  // it opened, so the builder went with it: no second call can read it
        fixpp_engine_destroy(refusing);
        fixpp_engine_destroy(able);
        return;
    }

    fixpp_session_t* opened = nullptr;
    EXPECT_EQ(fixpp_session_open(able, sc, &opened), FIXPP_ERR_OK)
        << "the refused call consumed the builder, or the second engine refused it";
    EXPECT_NE(opened, nullptr);
    if (opened == nullptr) {
        fixpp_session_config_destroy(sc);
    }
    fixpp_engine_destroy(refusing);
    fixpp_engine_destroy(able);
}

// A validating session with no default: the first clause of data-model E-9, then the setter fixes
// the builder. It proves the setter reaches `register_session`, which the table above cannot.
TEST(W12Refusal, NoDefault_ThreadConfig_ThenTheSetterOpens) {
    fixpp_engine_t* e = engine_over({fixt11(), fix50sp2(), latest()});
    fixpp_session_config_t* sc = nullptr;
    ASSERT_EQ(fixpp_session_config_create(&sc), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_comp_ids(sc, "ISLD", "TW"), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_begin_string(sc, "FIXT.1.1"), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_role(sc, FIXPP_ROLE_INITIATOR), FIXPP_ERR_OK);
    EXPECT_EQ(
        fixpp_session_config_set_security(sc, FIXPP_SECURITY_INSECURE_PLAIN_TCP, nullptr, nullptr),
        FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_tcp_endpoint(sc, "127.0.0.1", 1), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_dictionary(sc, fix50sp2()), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_validate_inbound_messages(sc, true), FIXPP_ERR_OK);
    fixpp_session_t* h = nullptr;
    const fixpp_error_t refused = fixpp_session_open(e, sc, &h);
    EXPECT_EQ(refused, FIXPP_ERR_THREAD_CONFIG);
    if (h != nullptr) {  // it opened, so the builder went with it: no second call can read it
        fixpp_engine_destroy(e);
        return;
    }
    EXPECT_EQ(fixpp_session_config_set_default_appl_ver_id(sc, "9"), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_open(e, sc, &h), FIXPP_ERR_OK);
    if (h == nullptr) {
        fixpp_session_config_destroy(sc);
    }
    fixpp_engine_destroy(e);
}

// ── The live arm ────────────────────────────────────────────────────────────────────────

struct Bytes {
    fixpp_error_t rc = FIXPP_ERR_UNKNOWN;
    int kind = -1;
    int session = -1;
    int application = -1;
    int reserved = -1;
};

Bytes read_version(const fixpp_msg_t* m) {
    fixpp_resolved_msg_version_t v{0xAA, 0xAA, 0xAA, 0xAA};
    Bytes b;
    b.rc = fixpp_msg_version(m, &v);
    b.kind = v.kind;
    b.session = v.session;
    b.application = v.application;
    b.reserved = v._reserved;
    return b;
}

void expect_bytes(const Bytes& b, int kind, int session, int application) {
    EXPECT_EQ(b.rc, FIXPP_ERR_OK);
    EXPECT_EQ(b.kind, kind);
    EXPECT_EQ(b.session, session);
    EXPECT_EQ(b.application, application);
    EXPECT_EQ(b.reserved, 0);
}

// What the receive callback saw. The first message's clone outlives the callback.
struct CallbackLog {
    std::atomic<int> calls{0};
    std::array<Bytes, 8> bytes{};
    std::atomic<fixpp_msg_t*> clone{nullptr};
};

void on_message(const fixpp_msg_t* inbound, void* user) {
    auto* log = static_cast<CallbackLog*>(user);
    const int at = log->calls.load(std::memory_order_relaxed);
    if (at < static_cast<int>(log->bytes.size())) {
        log->bytes[static_cast<std::size_t>(at)] = read_version(inbound);
    }
    if (at == 0) {
        fixpp_msg_t* cloned = nullptr;
        if (fixpp_msg_clone(inbound, &cloned) == FIXPP_ERR_OK) {
            log->clone.store(cloned, std::memory_order_release);
        }
    }
    log->calls.store(at + 1, std::memory_order_release);
}

std::string header(unsigned seq) {
    return "34=" + std::to_string(seq) + "\x01" + "49=TW\x01" + "52=" + fix_utc_now() + "\x01" +
           "56=ISLD\x01";
}

const std::string kNos =
    "11=C\x01"
    "21=1\x01"
    "55=X\x01"
    "54=1\x01"
    "60=20240101-00:00:00\x01"
    "40=1\x01"
    "38=1\x01";

std::string nos(unsigned seq, const std::string& before_34, const std::string& tail) {
    return fixt_frame("35=D\x01" + before_34 + header(seq) + kNos + tail);
}

const std::string k3115 = "3115=1.5\x01";

// Waits until the callback has run `n` times.
bool wait_calls(const CallbackLog& log, int n) {
    return fixpp::test_support::wait_until_observed(
        [&] { return log.calls.load(std::memory_order_acquire) >= n; }, kStep);
}

TEST_P(W12Shape, ReachesActive_RoutesAccepts_RoutesRejects_ReadsItsVersion) {
    const Shape shape = GetParam();
    FixtRawPeer peer;
    fixpp_engine_t* engine = engine_over({fixt11(), fix50sp2(), latest()});
    ASSERT_NE(engine, nullptr);
    fixpp_session_config_t* sc = fixt_config(shape, peer.port());
    fixpp_session_t* h = nullptr;
    const fixpp_error_t opened = fixpp_session_open(engine, sc, &h);
    EXPECT_EQ(opened, FIXPP_ERR_OK) << "fixpp_session_open refused the config";
    if (opened != FIXPP_ERR_OK) {
        fixpp_session_config_destroy(sc);
        fixpp_engine_destroy(engine);
        return;
    }
    CallbackLog log;
    EXPECT_EQ(fixpp_session_register_callback(h, on_message, &log), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_engine_start(engine), FIXPP_ERR_OK);

    std::optional<std::string> reject_3115;
    std::optional<std::string> reject_99;
    int calls_at_end = -1;
    if (peer.accept(kStep)) {
        const auto logon = peer.read_msg_type("A", kStep);
        EXPECT_TRUE(logon.has_value()) << "no Logon from the initiator";
        EXPECT_EQ(frame_field(logon.value_or(""), 1137), "9");
        // Answers 141 as the session's Logon asked, since the default policy wants both to say it.
        const std::string reset = frame_field(logon.value_or(""), 141) == "Y" ? "141=Y\x01" : "";
        EXPECT_TRUE(peer.write(fixt_frame("35=A\x01" + header(1) + "98=0\x01" + "108=30\x01" +
                                          "1137=9\x01" + reset)));
        bool established = false;
        EXPECT_TRUE(fixpp::test_support::wait_until_observed(
            [&] {
                return fixpp_session_is_established(h, &established) == FIXPP_ERR_OK && established;
            },
            kStep))
            << "the session did not reach Active";

        // An accept routed by 1128: only FIX Latest declares 3115.
        EXPECT_TRUE(peer.write(nos(2, "1128=10\x01", k3115)));
        EXPECT_TRUE(wait_calls(log, 1)) << "the routed frame did not reach the callback";

        // A second thread reads the clone while the worker handles the frames below.
        std::atomic<bool> stop_reading{false};
        std::vector<Bytes> clone_reads;
        std::thread reader{[&] {
            fixpp_msg_t* const clone = log.clone.load(std::memory_order_acquire);
            while (clone != nullptr && !stop_reading.load(std::memory_order_acquire)) {
                if (clone_reads.size() < 4096U) {
                    clone_reads.push_back(read_version(clone));
                } else {
                    (void)read_version(clone);
                }
            }
        }};

        // The negotiated version answers a frame with no 1128.
        EXPECT_TRUE(peer.write(nos(3, "", "")));
        EXPECT_TRUE(wait_calls(log, 2)) << "the plain NOS did not reach the callback";
        // Routed to FIX50SP2, which does not declare 3115.
        EXPECT_TRUE(peer.write(nos(4, "", k3115)));
        reject_3115 = peer.read_msg_type("3", kStep);
        // The validation setter's own cell: a non-validating session delivers this frame.
        EXPECT_TRUE(peer.write(nos(5, "1128=99\x01", "")));
        reject_99 = peer.read_msg_type("3", kStep);
        calls_at_end = log.calls.load(std::memory_order_acquire);

        stop_reading.store(true, std::memory_order_release);
        reader.join();
        EXPECT_FALSE(clone_reads.empty()) << "the clone was not read, or there is none";
        for (const Bytes& b : clone_reads) {
            expect_bytes(b, FIXPP_MSG_KIND_APPLICATION, FIXPP_SESSION_VER_FIXT11,
                         FIXPP_APPL_VER_FIXLATEST);
        }
    } else {
        ADD_FAILURE() << "the session did not connect";
    }

    EXPECT_TRUE(reject_3115.has_value()) << "no Reject for 3115 on the negotiated version";
    EXPECT_EQ(frame_field(reject_3115.value_or(""), 373), "2");
    EXPECT_EQ(frame_field(reject_3115.value_or(""), 371), "3115");
    EXPECT_EQ(frame_field(reject_3115.value_or(""), 372), "D");
    EXPECT_EQ(frame_field(reject_3115.value_or(""), 45), "4");
    EXPECT_TRUE(reject_99.has_value()) << "no Reject for 1128=99: a non-validating session";
    EXPECT_EQ(frame_field(reject_99.value_or(""), 373), "5");
    EXPECT_EQ(frame_field(reject_99.value_or(""), 371), "1128");
    EXPECT_EQ(frame_field(reject_99.value_or(""), 372), "D");
    EXPECT_EQ(frame_field(reject_99.value_or(""), 45), "5");
    EXPECT_EQ(calls_at_end, 2) << "a refused frame reached the callback";

    expect_bytes(log.bytes[0], FIXPP_MSG_KIND_APPLICATION, FIXPP_SESSION_VER_FIXT11,
                 FIXPP_APPL_VER_FIXLATEST);
    expect_bytes(log.bytes[1], FIXPP_MSG_KIND_APPLICATION, FIXPP_SESSION_VER_FIXT11,
                 FIXPP_APPL_VER_FIX50SP2);

    EXPECT_EQ(fixpp_session_close(h), FIXPP_ERR_OK);
    fixpp_engine_destroy(engine);
    peer.close();
    if (fixpp_msg_t* const clone = log.clone.load(std::memory_order_acquire); clone != nullptr) {
        EXPECT_EQ(fixpp_msg_destroy(clone), FIXPP_ERR_OK);
    }
}

INSTANTIATE_TEST_SUITE_P(W12, W12Shape, testing::Values(Shape::T, Shape::A),
                         [](const testing::TestParamInfo<Shape>& i) {
                             return std::string{shape_name(i.param)};
                         });

}  // namespace
