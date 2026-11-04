#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_099_fixt_support.hpp
//
// 099-fixt-appl-version-validation, tasks.md T007: the rig the session witnesses of
// contracts/witnesses.md share. A real Session, a mock clock, raw frames through
// on_inbound_frame, NextNumIn through session_test_access::seqnum_mgr, fromApp and
// fromAdmin counted, and the ending as a value.
//
// Each witness of the keyed logs (research R-18) runs that log's input and configuration,
// so a run under FIXPP_099_TRACE=<file> can be compared line by line with the parent
// repository's research/probes/099_fixt_session_today/{T,A,L}_{on,off}.log and mtd.log.
// A cell computes its ending, appends it to the trace in the log's form, and only then
// asserts E-7's literal, so a RED cell still leaves its line.
//
// The cost of a cell (research R-3): dictionaries load once per process, each shape's S is
// the shared registry's own Dictionary*, and `adopt_fixture_routes` hands every session a set
// built once per (registry, S), so an open() walks nothing. A cell that sets
// RigOptions::private_build keeps open()'s own build, and a test's direct `Session` does.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/dict/load_any.hpp>
#include <fixpp/dict/orchestra_loader.hpp>
#include <fixpp/dict/version_registry.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/compid_authorization_policy.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_event.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/tls/peer_identity.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>
#include <fstream>
#include <functional>
#include <future>
#include <initializer_list>
#include <iterator>
#include <map>
#include <memory>
#include <memory_resource>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "session/fixt_route_set.hpp"
#include "support/session_test_access.hpp"

namespace fixpp::session::test099 {

namespace dd = fixpp::dict;
using fixpp::core::expected_t;
using Frames = std::vector<std::string>;

// ── Frames, as research R-18's harness builds them ──────────────────────────────────────

inline std::string wire(const std::string& begin_string, const std::string& body) {
    std::string s =
        "8=" + begin_string + "\x01" + "9=" + std::to_string(body.size()) + "\x01" + body;
    unsigned sum = 0;
    for (unsigned char ch : s) {
        sum += ch;
    }
    char trailer[16];
    std::snprintf(trailer, sizeof trailer, "10=%03u\x01", sum % 256);
    return s + trailer;
}

struct Ctx {
    std::string snd;
    std::string tgt;
    unsigned seq;
};

inline constexpr const char* kFresh = "20240101-00:00:00.000";
inline constexpr const char* kStale = "20200101-00:00:00.000";
inline const std::string kFT = "FIXT.1.1";
inline const std::string kNos =
    "11=C\x01"
    "21=1\x01"
    "55=X\x01"
    "54=1\x01"
    "60=20240101-00:00:00\x01"
    "40=1\x01"
    "38=1\x01";
inline const std::string kNosNo11 =
    "21=1\x01"
    "55=X\x01"
    "54=1\x01"
    "60=20240101-00:00:00\x01"
    "40=1\x01"
    "38=1\x01";
inline const std::string kLogonBody =
    "98=0\x01"
    "108=30\x01";

inline std::string hdr(const Ctx& c, const char* sending_time = kFresh) {
    return "34=" + std::to_string(c.seq) + "\x01" + "49=" + c.snd + "\x01" + "52=" + sending_time +
           "\x01" + "56=" + c.tgt + "\x01";
}

inline std::string frame_35(const std::string& msg_type, const Ctx& c, const std::string& body = {},
                            const std::string& before_34 = {}) {
    return wire(kFT, "35=" + msg_type + "\x01" + before_34 + hdr(c) + body);
}

inline std::string logon_frame(const Ctx& c, const std::string& extra,
                               const char* sending_time = kFresh,
                               const std::string& begin_string = kFT) {
    return wire(begin_string, "35=A\x01" + hdr(c, sending_time) + kLogonBody + extra);
}

// ── The keyed inputs: research R-18's ids, each spelled as its log spells it ────────────

struct Input {
    std::string id;
    std::function<std::string(const Ctx&)> make;
    bool mtls = false;
    bool posture = false;
};

inline std::vector<Input> make_inputs() {
    std::vector<Input> v;
    auto add = [&](std::string id, std::function<std::string(const Ctx&)> f, bool mtls = false) {
        v.push_back(Input{std::move(id), std::move(f), mtls, false});
    };
    auto logon = [](std::string extra, const char* st, std::string bs) {
        return [extra = std::move(extra), st, bs = std::move(bs)](const Ctx& c) {
            return logon_frame(c, extra, st, bs);
        };
    };
    auto ahead = [](Ctx c) {
        c.seq += 5;
        return c;
    };
    const std::string nos = kNos;
    const std::string bad = kNosNo11;
    // Repeated 35.
    add("rep35 D..0 past", [=](const Ctx& c) { return frame_35("D", c, nos + "35=0\x01"); });
    add("rep35 D..3 past", [=](const Ctx& c) { return frame_35("D", c, nos + "35=3\x01"); });
    add("rep35 D..4 past", [=](const Ctx& c) { return frame_35("D", c, nos + "35=4\x01"); });
    add("rep35 D..A past", [=](const Ctx& c) { return frame_35("D", c, nos + "35=A\x01"); });
    add("rep35 D..5 past", [=](const Ctx& c) { return frame_35("D", c, nos + "35=5\x01"); });
    add("rep35 5..D past", [](const Ctx& c) {
        return frame_35("5", c,
                        "58=x\x01"
                        "35=D\x01");
    });
    add("rep35 0..D past", [](const Ctx& c) {
        return frame_35("0", c,
                        "112=x\x01"
                        "35=D\x01");
    });
    add("rep35 3..D past", [](const Ctx& c) {
        return frame_35("3", c,
                        "45=1\x01"
                        "35=D\x01");
    });
    add("rep35 4..D past", [](const Ctx& c) {
        return frame_35("4", c,
                        "36=9\x01"
                        "35=D\x01");
    });
    add("rep35 A..0 past", [](const Ctx& c) {
        return frame_35("A", c,
                        kLogonBody +
                            "1137=9\x01"
                            "35=0\x01");
    });
    add("rep35 D..A logon-fields", [](const Ctx& c) {
        return frame_35("D", c,
                        kLogonBody +
                            "1137=9\x01"
                            "35=A\x01");
    });
    add("rep35 0..A logon-fields", [](const Ctx& c) {
        return frame_35("0", c,
                        kLogonBody +
                            "1137=9\x01"
                            "35=A\x01");
    });
    add("rep35 0,D header", [](const Ctx& c) {
        return wire(kFT,
                    "35=0\x01"
                    "35=D\x01" +
                        hdr(c));
    });
    add("rep35 D,D header", [=](const Ctx& c) {
        return wire(kFT,
                    "35=D\x01"
                    "35=D\x01" +
                        hdr(c) + nos);
    });
    // Route refusal.
    add("1128=99 hdr", [=](const Ctx& c) { return frame_35("D", c, nos, "1128=99\x01"); });
    add("1128=abc hdr", [=](const Ctx& c) { return frame_35("D", c, nos, "1128=abc\x01"); });
    add("1128=4 hdr", [=](const Ctx& c) { return frame_35("D", c, nos, "1128=4\x01"); });
    add("1128=empty hdr", [=](const Ctx& c) { return frame_35("D", c, nos, "1128=\x01"); });
    // Past-header 1128.
    add("1128=6 past", [=](const Ctx& c) { return frame_35("D", c, nos + "1128=6\x01"); });
    add("1128=abc past", [=](const Ctx& c) { return frame_35("D", c, nos + "1128=abc\x01"); });
    // Ordinary application frames.
    add("NOS", [=](const Ctx& c) { return frame_35("D", c, nos); });
    add("NOS 1128=9", [=](const Ctx& c) { return frame_35("D", c, nos, "1128=9\x01"); });
    add("NOS 1128=10", [=](const Ctx& c) { return frame_35("D", c, nos, "1128=10\x01"); });
    add("NOS missing 11", [=](const Ctx& c) { return frame_35("D", c, bad); });
    add("NOS missing 11 seq+5", [=](const Ctx& c) { return frame_35("D", ahead(c), bad); });
    add("rep35 D..0 past seq+5",
        [=](const Ctx& c) { return frame_35("D", ahead(c), nos + "35=0\x01"); });
    // Admin controls.
    add("Heartbeat 1128=99 hdr", [](const Ctx& c) { return frame_35("0", c, {}, "1128=99\x01"); });
    add("Heartbeat 1128=10 hdr", [](const Ctx& c) { return frame_35("0", c, {}, "1128=10\x01"); });
    add("TestRequest 1128=6 past", [](const Ctx& c) {
        return frame_35("1", c,
                        "112=x\x01"
                        "1128=6\x01");
    });
    add("Heartbeat", [](const Ctx& c) { return frame_35("0", c); });
    add("TestRequest missing 112", [](const Ctx& c) { return frame_35("1", c); });
    add("TestRequest", [](const Ctx& c) { return frame_35("1", c, "112=x\x01"); });
    add("Logout", [](const Ctx& c) { return frame_35("5", c); });
    // Logon 1137.
    add("Logon 1137=9", logon("1137=9\x01", kFresh, kFT));
    add("Logon 1137=10", logon("1137=10\x01", kFresh, kFT));
    add("Logon 1137 absent", logon("", kFresh, kFT));
    add("Logon 1137=empty", logon("1137=\x01", kFresh, kFT));
    add("Logon 1137=99", logon("1137=99\x01", kFresh, kFT));
    add("Logon undeclared 5999", logon("1137=9\x01"
                                       "5999=1\x01",
                                       kFresh, kFT));
    add("Logon 1137=abc", logon("1137=abc\x01", kFresh, kFT));
    add("Logon 1137=4", logon("1137=4\x01", kFresh, kFT));
    // Wrong BeginString.
    add("wrongBS Logon 1137=9", logon("1137=9\x01", kFresh, "FIX.4.4"));
    add("wrongBS Logon 1137=99", logon("1137=99\x01", kFresh, "FIX.4.4"));
    add("wrongBS Logon 1137=4", logon("1137=4\x01", kFresh, "FIX.4.4"));
    add("wrongBS Logon 1137 absent", logon("", kFresh, "FIX.4.4"));
    add("wrongBS NOS", [=](const Ctx& c) { return wire("FIX.4.4", "35=D\x01" + hdr(c) + nos); });
    // Stale SendingTime.
    add("stale Logon 1137=9", logon("1137=9\x01", kStale, kFT));
    add("stale Logon 1137=99", logon("1137=99\x01", kStale, kFT));
    add("stale Logon 1137=4", logon("1137=4\x01", kStale, kFT));
    add("stale Logon 1137 absent", logon("", kStale, kFT));
    add("stale NOS", [=](const Ctx& c) { return wire(kFT, "35=D\x01" + hdr(c, kStale) + nos); });
    add("stale Heartbeat", [](const Ctx& c) { return wire(kFT, "35=0\x01" + hdr(c, kStale)); });
    // Off-list mTLS identity: the frame is the Logon, the session mTLS with an off-list identity.
    add("mTLS-offlist Logon 1137=9", logon("1137=9\x01", kFresh, kFT), true);
    add("mTLS-offlist Logon 1137=99", logon("1137=99\x01", kFresh, kFT), true);
    add("mTLS-offlist Logon 1137=4", logon("1137=4\x01", kFresh, kFT), true);
    add("mTLS-offlist Logon 1137 absent", logon("", kFresh, kFT), true);
    // Posture: production posture, the peer's Logon says 464=Y.
    auto post = [&](std::string id, std::string extra) {
        v.push_back(Input{std::move(id),
                          [extra = std::move(extra)](const Ctx& c) {
                              return logon_frame(c, extra + "464=Y\x01");
                          },
                          false, true});
    };
    post("posture-mismatch Logon 1137=9", "1137=9\x01");
    post("posture-mismatch Logon 1137=99", "1137=99\x01");
    post("posture-mismatch Logon 1137=4", "1137=4\x01");
    post("posture-mismatch Logon 1137 absent", "");
    // mtd.log's inputs.
    add("Logon 1137 absent + 5999 after", logon("5999=1\x01", kFresh, kFT));
    add("Logon 5999 + 1137 absent",
        [](const Ctx& c) { return wire(kFT, "35=A\x01" + hdr(c) + "5999=1\x01" + kLogonBody); });
    add("Logon 1137=99 + 5999 after", logon("1137=99\x01"
                                            "5999=1\x01",
                                            kFresh, kFT));
    add("Logon 1137=4 + 5999 after", logon("1137=4\x01"
                                           "5999=1\x01",
                                           kFresh, kFT));
    add("Logon 141=Y 1137=4", logon("141=Y\x01"
                                    "1137=4\x01",
                                    kFresh, kFT));
    add("Logon 141=Y 1137=9", logon("141=Y\x01"
                                    "1137=9\x01",
                                    kFresh, kFT));
    add("Logon Message Type Default", logon("1137=9\x01"
                                            "384=1\x01"
                                            "372=D\x01"
                                            "1130=10\x01",
                                            kFresh, kFT));
    add("Logon Message Type Default 385=S 1410=Y", logon("1137=9\x01"
                                                         "384=1\x01"
                                                         "372=D\x01"
                                                         "385=S\x01"
                                                         "1130=10\x01"
                                                         "1410=Y\x01",
                                                         kFresh, kFT));
    // E-7's inputs with no log line, read against their row.
    add("1128=0 hdr", [=](const Ctx& c) { return frame_35("D", c, nos, "1128=0\x01"); });
    add("1128=1 hdr", [=](const Ctx& c) { return frame_35("D", c, nos, "1128=1\x01"); });
    add("rep35 D..0 past 1128=99",
        [=](const Ctx& c) { return frame_35("D", c, nos + "35=0\x01", "1128=99\x01"); });
    add("1128=99 hdr stale", [=](const Ctx& c) {
        return wire(kFT,
                    "35=D\x01"
                    "1128=99\x01" +
                        hdr(c, kStale) + nos);
    });
    return v;
}

inline const Input& input(std::string_view id) {
    static const std::vector<Input> all = make_inputs();
    for (const auto& i : all) {
        if (i.id == id) {
            return i;
        }
    }
    ADD_FAILURE() << "no input named " << id;
    return all.front();
}

// ── The dictionaries: one load per process ──────────────────────────────────────────────

using DictPtr = std::shared_ptr<const dd::Dictionary>;

inline std::pmr::memory_resource* dict_resource() {
    static std::pmr::monotonic_buffer_resource mr;
    return &mr;
}

inline DictPtr load_vendored(const char* relative) {
    return std::make_shared<const dd::Dictionary>(
        dd::load_any(std::filesystem::path{FIXPP_DICT_DATA_DIR} / relative, dict_resource()));
}

inline const DictPtr& fixt11() {
    static const DictPtr d = load_vendored("FIXT11.xml");
    return d;
}
inline const DictPtr& fix50sp2() {
    static const DictPtr d = load_vendored("FIX50SP2.xml");
    return d;
}
inline const DictPtr& latest() {
    static const DictPtr d = load_vendored("orchestra/OrchestraFIXLatest.xml");
    return d;
}
inline const DictPtr& fix44() {
    static const DictPtr d = load_vendored("FIX44.xml");
    return d;
}
inline const DictPtr& fix42() {
    static const DictPtr d = load_vendored("FIX42.xml");
    return d;
}

// A dictionary a cell builds from a vendored file plus an overlay held in the test's own
// strings. It lives for the process: a witness builds it once.
inline DictPtr overlay_on(const char* vendored_relative, const std::string& overlay_xml) {
    std::ifstream in{std::filesystem::path{FIXPP_DICT_DATA_DIR} / vendored_relative,
                     std::ios::binary};
    const std::string base{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    return std::make_shared<const dd::Dictionary>(
        dd::XmlLoader{}.load_with_overlay_from_string(base, overlay_xml, dict_resource()));
}

inline DictPtr orchestra_overlay_on_latest(const std::string& overlay_xml) {
    std::ifstream in{
        std::filesystem::path{FIXPP_DICT_DATA_DIR} / "orchestra/OrchestraFIXLatest.xml",
        std::ios::binary};
    const std::string base{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    return std::make_shared<const dd::Dictionary>(
        dd::OrchestraLoader{}.load_with_overlay_from_string(base, overlay_xml, dict_resource()));
}

// A dictionary built from a vendored file's text with one edit, for a cell whose transport
// dictionary the vendored file is not: a partial FIXT11, or 1137 optional.
inline DictPtr edited_vendored(const char* relative,
                               const std::function<std::string(std::string)>& edit) {
    std::ifstream in{std::filesystem::path{FIXPP_DICT_DATA_DIR} / relative, std::ios::binary};
    std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    return std::make_shared<const dd::Dictionary>(
        dd::XmlLoader{}.load_from_string(edit(std::move(text)), dict_resource()));
}

// A value `d` accepts for the field: its first enum value, or one of its type. Empty for a type
// this cannot spell, which the recompute below then skips.
inline std::string plain_value(const dd::Dictionary& d, const dd::FieldRef& f) {
    const auto values = d.enum_values(f.tag);
    if (!values.empty()) {
        return std::string(values.front().value);
    }
    switch (f.type) {
        case dd::field_data_type::String:
        case dd::field_data_type::Char:
            return "x";
        case dd::field_data_type::Int:
        case dd::field_data_type::Float:
            return "1";
        case dd::field_data_type::Boolean:
            return "Y";
        default:
            return {};
    }
}

// The first plain field of `msg_type` in `only` that none of `others` accepts there: a set
// difference recomputed over the vendored files, so a dictionary refresh cannot leave a stale
// literal. Zero if the difference holds no plain field, which the caller must fail on.
inline std::uint16_t only_field(const dd::Dictionary& only, std::string_view msg_type,
                                std::initializer_list<const dd::Dictionary*> others) {
    // A FIX 4.x message's fields include its header's and trailer's, which are not this message's.
    const dd::table_view own = only.as_table_view();
    for (const dd::FieldRef& f : only.message_fields(msg_type)) {
        if (f.group_no_tag != 0 || f.length_pair_data_tag != 0 || plain_value(only, f).empty() ||
            own.is_header_tag(f.tag) || f.tag == 10 || f.tag == 89 || f.tag == 93) {
            continue;
        }
        bool elsewhere = false;
        for (const dd::Dictionary* d : others) {
            elsewhere = elsewhere || d->field_valid_for(msg_type, f.tag);
        }
        if (!elsewhere) {
            return f.tag;
        }
    }
    return 0;
}

// A (MsgType, tag) only `only` declares among the application messages it shares with `others`:
// the FIX 4.4-only pair of contracts/witnesses.md's preamble. A group's first field counts, with
// its group's count tag, since a frame can carry it there.
struct OnlyPair {
    std::string msg_type;
    std::uint16_t tag = 0;
    std::uint16_t group_no_tag = 0;
    std::string value;
};

inline OnlyPair only_pair(const dd::Dictionary& only,
                          std::initializer_list<const dd::Dictionary*> others) {
    const dd::table_view own = only.as_table_view();
    for (const dd::MessageEntry& m : only.messages()) {
        bool shared = true;
        for (const dd::Dictionary* d : others) {
            shared = shared && !d->message_fields(m.msg_type).empty();
        }
        const bool admin = m.msg_type.size() == 1 &&
                           std::string_view{"012345A"}.find(m.msg_type) != std::string_view::npos;
        if (!shared || admin) {
            continue;
        }
        for (const dd::FieldRef& f : only.message_fields(m.msg_type)) {
            if (f.length_pair_data_tag != 0 || plain_value(only, f).empty() ||
                own.is_header_tag(f.tag) || f.tag == 10 || f.tag == 89 || f.tag == 93 ||
                (f.group_no_tag != 0 && only.group_first_field(f.group_no_tag) != f.tag)) {
                continue;
            }
            bool elsewhere = false;
            for (const dd::Dictionary* d : others) {
                elsewhere = elsewhere || d->field_valid_for(m.msg_type, f.tag);
            }
            if (!elsewhere) {
                return OnlyPair{std::string(m.msg_type), f.tag, f.group_no_tag,
                                plain_value(only, f)};
            }
        }
    }
    return {};
}

// A body with every required field of `msg_type` in `d`: the first enum value, or a value of its
// type. The header's tags are the frame builder's.
inline std::string required_body(const dd::Dictionary& d, std::string_view msg_type) {
    std::string out;
    for (const std::uint16_t tag : d.required_fields(msg_type)) {
        if (tag == 8 || tag == 9 || tag == 10 || tag == 34 || tag == 35 || tag == 49 || tag == 52 ||
            tag == 56) {
            continue;
        }
        const auto values = d.enum_values(tag);
        out += std::to_string(tag) + "=" +
               (values.empty() ? std::string("1") : std::string(values.front().value)) + "\x01";
    }
    return out;
}

// ── A Length+Data pair only an overlay declares ─────────────────────────────────────────
//
// Every Length+Data pair the vendored files declare is in the standard table
// (core/length_data_pairs.hpp), which a table's hooks answer whatever the dictionary, so no
// vendored dictionary can split a frame differently from another. A pair on a route therefore
// comes from an overlay: 5030 and 5031, tags no vendored file declares.

inline const std::string kPairFields =
    "<fields>"
    "<field number='5030' name='VenuePairLen' type='LENGTH' />"
    "<field number='5031' name='VenuePairData' type='DATA' />"
    "</fields>";
inline const std::string kPairMembers =
    "<field name='VenuePairLen' required='N' /><field name='VenuePairData' required='N' />";

// `5030=<len>|5031=<data>|`.
inline std::string pair_field(const std::string& data) {
    return "5030=" + std::to_string(data.size()) + "\x01" + "5031=" + data + "\x01";
}

// FIX50SP2 whose NewOrderSingle declares the pair in its body.
inline const DictPtr& fix50sp2_with_body_pair() {
    static const DictPtr d =
        overlay_on("FIX50SP2.xml",
                   "<fix type='FIX' major='5' minor='0' servicepack='2'><messages>"
                   "<message name='NewOrderSingle' msgtype='D' msgcat='app'>" +
                       kPairMembers + "</message></messages>" + kPairFields + "</fix>");
    return d;
}

// ── Shapes and fixtures ─────────────────────────────────────────────────────────────────

enum class Shape { T, A, L };
enum class Arm { NotConnected, LogonSent, Active, LogoutSent };

inline const char* shape_name(Shape s) {
    switch (s) {
        case Shape::T:
            return "T";
        case Shape::A:
            return "A";
        case Shape::L:
            return "L";
    }
    return "?";
}

inline const char* arm_name(Arm a) {
    switch (a) {
        case Arm::NotConnected:
            return "NotConnected";
        case Arm::LogonSent:
            return "LogonSent";
        case Arm::Active:
            return "Active";
        case Arm::LogoutSent:
            return "LogoutSent";
    }
    return "?";
}

// A registry and a session dictionary: the shared set of tasks.md T007, or a cell's own.
struct Fixture {
    std::shared_ptr<dd::version_registry> registry;
    DictPtr session_dict;
    dd::application_version default_appl = dd::application_version::v50sp2;
    const char* peer_1137 = "9";
};

// One registry per list of dictionaries, for the process: a registry is immutable, and a set is
// built per registry (`adopt_fixture_routes`), so a fixture a cell makes again costs no new walk.
inline std::shared_ptr<dd::version_registry> make_registry(std::vector<DictPtr> dicts) {
    // Ahead of `all`, so that it outlives the dictionaries a registry here holds, whichever a test
    // asks for first. A static is destroyed in reverse order of construction.
    (void)dict_resource();
    static std::map<std::vector<const dd::Dictionary*>, std::shared_ptr<dd::version_registry>> all;
    std::vector<const dd::Dictionary*> key;
    for (const DictPtr& d : dicts) {
        key.push_back(d.get());
    }
    auto& slot = all[key];
    if (!slot) {
        slot = std::make_shared<dd::version_registry>(dicts);
    }
    return slot;
}

// Research R-18's configuration: FIXT11, FIX50SP2 and FIX Latest, default v50sp2.
inline const std::shared_ptr<dd::version_registry>& shared_registry() {
    static const std::shared_ptr<dd::version_registry> r =
        make_registry({fixt11(), fix50sp2(), latest()});
    return r;
}

inline const DictPtr& shape_dict(Shape s) {
    switch (s) {
        case Shape::T:
            return fixt11();
        case Shape::A:
            return fix50sp2();
        case Shape::L:
            return latest();
    }
    return fixt11();
}

inline Fixture shape_fixture(Shape s) { return Fixture{shared_registry(), shape_dict(s)}; }

// An own-registry fixture on a shape (tasks.md T007): the shape's S with these engine
// dictionaries.
inline Fixture own_fixture(Shape s, std::vector<DictPtr> dicts) {
    return Fixture{make_registry(std::move(dicts)), shape_dict(s)};
}

// Hands the fixture's route set to the session, right after construction and before open(), as
// the role loops do (tasks.md T007). Research R-18's registry has one set, over its three S's, as
// an Engine with three FIXT sessions would; any other (registry, S) has its own, built once, each
// holding its registry so that an address cannot come back as another's. Built here, outside any
// pump. A fixture's set holds S, since no production path adopts one lacking it.
inline void adopt_fixture_routes(Session& session, const Fixture& fixture) {
    using Set = std::shared_ptr<const detail::fixt_route_set>;
    struct Held {
        std::shared_ptr<dd::version_registry> registry;
        DictPtr session_dict;
        Set routes;
    };
    static std::map<std::pair<const dd::version_registry*, const dd::Dictionary*>, Held> sets;

    const bool shared_one = fixture.registry == shared_registry();
    const dd::Dictionary* const key_s =
        shared_one && (fixture.session_dict == fixt11() || fixture.session_dict == fix50sp2() ||
                       fixture.session_dict == latest())
            ? nullptr
            : fixture.session_dict.get();
    Held& held = sets[{fixture.registry.get(), key_s}];
    if (!held.routes) {
        const auto built =
            key_s == nullptr
                ? detail::fixt_route_set::build(
                      *fixture.registry,
                      std::vector<const dd::Dictionary*>{fixt11().get(), fix50sp2().get(),
                                                         latest().get()})
                : detail::fixt_route_set::build(*fixture.registry, std::views::single(key_s));
        if (!built) {
            ADD_FAILURE() << "the fixture's route set did not build";
            return;
        }
        held = Held{fixture.registry, fixture.session_dict, *built};
    }
    if (held.routes->find(fixture.session_dict.get(), nullptr) == nullptr) {
        ADD_FAILURE() << "the fixture's route set lacks its S";
        return;
    }
    session_test_access::adopt_routes(session, held.routes);
}

// ── The trace ───────────────────────────────────────────────────────────────────────────

inline void append_trace(const std::string& line) {
    const char* path = std::getenv("FIXPP_099_TRACE");
    if (path == nullptr) {
        return;
    }
    std::ofstream f{path, std::ios::app | std::ios::binary};
    f << line << '\n';
}

// ── The ending as a value ───────────────────────────────────────────────────────────────

inline std::string tag_value(const std::string& frame, int tag) {
    const std::string needle = "\x01" + std::to_string(tag) + "=";
    auto p = frame.find(needle);
    if (p == std::string::npos) {
        return {};
    }
    p += needle.size();
    return frame.substr(p, frame.find('\x01', p) - p);
}

inline const char* state_name(fsm_state s) {
    switch (s) {
        case fsm_state::NotConnected:
            return "NotConnected";
        case fsm_state::LogonSent:
            return "LogonSent";
        case fsm_state::LogonReceived:
            return "LogonReceived";
        case fsm_state::Active:
            return "Active";
        case fsm_state::LogoutSent:
            return "LogoutSent";
        case fsm_state::Disconnected:
            return "Disconnected";
    }
    return "?";
}

// What the session sent, as research R-18's log prints it.
inline std::string log_form_frames(const Frames& out) {
    if (out.empty()) {
        return "none";
    }
    std::string s;
    bool first = true;
    for (const auto& f : out) {
        if (!first) {
            s += " + ";
        }
        first = false;
        const auto t = tag_value(f, 35);
        if (t == "3") {
            s += "Reject{45=" + tag_value(f, 45) + " 372=" + tag_value(f, 372) +
                 " 373=" + tag_value(f, 373) + " 371=" + tag_value(f, 371) +
                 " 58=" + tag_value(f, 58) + "}";
        } else if (t == "5") {
            s += "Logout{58=" + tag_value(f, 58) + "}";
        } else if (t == "A") {
            s += "Logon{1137=" + tag_value(f, 1137) + "}";
        } else if (t == "2") {
            s += "ResendRequest{7=" + tag_value(f, 7) + " 16=" + tag_value(f, 16) + "}";
        } else if (t == "0") {
            s += "Heartbeat{112=" + tag_value(f, 112) + "}";
        } else {
            s += "35=" + t;
        }
    }
    return s;
}

// The same, in data-model E-7's notation, without the Text(58) E-7 does not fix.
inline std::string short_form_frames(const Frames& out) {
    if (out.empty()) {
        return "-";
    }
    std::string s;
    bool first = true;
    for (const auto& f : out) {
        if (!first) {
            s += " + ";
        }
        first = false;
        const auto t = tag_value(f, 35);
        if (t == "3") {
            s += "R " + tag_value(f, 373) + "/" + tag_value(f, 371) + " " + tag_value(f, 372) +
                 " 45=" + tag_value(f, 45);
        } else if (t == "5") {
            s += "Logout";
        } else if (t == "A") {
            s += "Logon";
        } else if (t == "2") {
            s += "ResendRequest " + tag_value(f, 7) + "-" + tag_value(f, 16);
        } else if (t == "0") {
            s += "Heartbeat 112=" + tag_value(f, 112);
        } else {
            s += "35=" + t;
        }
    }
    return s;
}

struct Ending {
    fsm_state before = fsm_state::NotConnected;
    fsm_state after = fsm_state::NotConnected;
    unsigned next_in_before = 0;
    unsigned next_in_after = 0;
    int from_app = 0;          // counted over the session's life, as the log prints it
    int from_admin_delta = 0;  // counted over this frame, as the log prints it
    bool auth_failed = false;
    std::string err;
    Frames out;
};

// Research R-18's log form, byte for byte.
inline std::string log_form(const Ending& e) {
    char buf[256];
    std::snprintf(buf, sizeof buf, "%s->%s | NextNumIn %u->%u %s | fromApp=%d fromAdmin=%d%s%s",
                  state_name(e.before), state_name(e.after), e.next_in_before, e.next_in_after,
                  e.next_in_after != e.next_in_before ? "consumed" : "kept", e.from_app,
                  e.from_admin_delta, e.auth_failed ? " authFailedEvent" : "",
                  e.err.empty() ? "" : (" " + e.err).c_str());
    return std::string(buf) + " | out: " + log_form_frames(e.out);
}

// What a cell asserts: `<sent> | <state> | <k or c> | app=<n> adm=<n>[ | auth]`, where a
// state is `stays` when unchanged and `Disc` for Disconnected.
inline std::string short_form(const Ending& e) {
    std::string state = e.after == e.before                  ? "stays"
                        : e.after == fsm_state::Disconnected ? "Disc"
                                                             : state_name(e.after);
    std::string s = short_form_frames(e.out) + " | " + state + " | " +
                    (e.next_in_after != e.next_in_before ? "c" : "k") +
                    " | app=" + std::to_string(e.from_app) +
                    " adm=" + std::to_string(e.from_admin_delta);
    if (e.auth_failed) {
        s += " | auth";
    }
    return s;
}

// ── The rig ─────────────────────────────────────────────────────────────────────────────

class CountingApp final : public Application {
public:
    using View = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;
    int app = 0;
    int admin = 0;
    std::function<void(const View&)> on_app;
    std::function<void(const View&)> on_admin;
    std::function<void(const View&)> on_to_app;

    expected_t<void> fromApp(const View& v, const SessionId&) override {
        ++app;
        if (on_app) {
            on_app(v);
        }
        return {};
    }
    expected_t<void> fromAdmin(const View& v, const SessionId&) override {
        ++admin;
        if (on_admin) {
            on_admin(v);
        }
        return {};
    }
    expected_t<void> toApp(const View& v, const SessionId&) override {
        if (on_to_app) {
            on_to_app(v);
        }
        return {};
    }
};

// attach_accepted_transport routes live writes through the attached transport, so a cell
// that attaches one captures there, not in cfg.transport_send.
class CaptureTransport final : public fixpp::transport::Transport {
public:
    explicit CaptureTransport(Frames* out) : out_(out) {}
    asio::awaitable<expected_t<fixpp::transport::ConnectInfo>> async_connect(
        fixpp::transport::Endpoint const& ep) override {
        fixpp::transport::ConnectInfo i;
        i.remote = ep;
        i.local = fixpp::transport::Endpoint{"127.0.0.1", 0};
        i.family = 2;
        co_return i;
    }
    asio::awaitable<expected_t<std::size_t>> async_read_some(std::span<std::byte>) override {
        co_return std::unexpected{fixpp::core::error::transport_read_eof};
    }
    asio::awaitable<expected_t<std::size_t>> async_write(std::span<const std::byte> b) override {
        out_->emplace_back(reinterpret_cast<const char*>(b.data()), b.size());
        co_return b.size();
    }
    expected_t<void> cancel() noexcept override { return {}; }
    expected_t<void> close() noexcept override { return {}; }

private:
    Frames* out_;
};

// Poll until ready, as research R-18's harness does: no window, so a cell costs what its
// handlers cost.
template <class F>
bool pump_ready(asio::io_context& ioc, F& fut, std::chrono::milliseconds budget) {
    const auto end = std::chrono::steady_clock::now() + budget;
    while (fut.wait_for(std::chrono::seconds{0}) != std::future_status::ready) {
        if (std::chrono::steady_clock::now() > end) {
            return false;
        }
        ioc.restart();
        ioc.poll();
    }
    return true;
}

// What one session's configuration varies by cell.
struct RigOptions {
    bool validate = true;
    session_role role = session_role::acceptor;
    bool mtls = false;
    bool posture = false;
    bool private_build = false;  // a cell that keeps open()'s own route build (tasks.md T007)
    std::function<void(SessionConfig&)> tweak;
};

class Rig {
public:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;
    Fixture fixture;
    std::shared_ptr<CountingApp> app = std::make_shared<CountingApp>();
    Frames out;
    std::unique_ptr<Session> sess;
    std::shared_ptr<std::future<expected_t<void>>> close_fut;
    SessionConfig cfg;
    RigOptions options;

    Rig(Fixture fx, RigOptions opts) : fixture(std::move(fx)), options(std::move(opts)) {
        const auto utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
        const auto stp = fixpp::core::steady_time_point{} + std::chrono::seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
        engine.application = app;

        const bool acceptor = options.role == session_role::acceptor;
        cfg.sender_comp_id = acceptor ? "ISLD" : "TW";
        cfg.target_comp_id = acceptor ? "TW" : "ISLD";
        cfg.begin_string = kFT;
        cfg.heartbeat_interval = std::chrono::seconds{30};
        cfg.security_profile = SecurityProfile{options.mtls ? SecurityProfile::kind::mtls_ca
                                                            : SecurityProfile::kind::one_way_ca};
        if (options.mtls) {
            cfg.compid_authorization_policy.add_binding("ON-LIST", cfg.target_comp_id);
        }
        cfg.dictionary = fixture.session_dict;
        cfg.executor_override = ioc.get_executor();
        cfg.role = options.role;
        cfg.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
        cfg.default_appl_ver_id = fixture.default_appl;
        cfg.validate_inbound_messages = options.validate;
        if (options.posture) {
            cfg.posture = session_posture::production;
        }
        cfg.logout_disconnect_timeout_ms = 20000;
        cfg.transport_send = [this](std::span<const std::byte> f) {
            out.emplace_back(reinterpret_cast<const char*>(f.data()), f.size());
        };
        if (options.tweak) {
            options.tweak(cfg);
        }
    }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;
    Rig(Rig&&) = delete;
    Rig& operator=(Rig&&) = delete;
    // Finishes a close a LogoutSent cell left in flight. Reports nothing: a report here
    // could throw under --gtest_throw_on_failure, and the cell has already asserted.
    ~Rig() {
        if (close_fut) {
            clock->advance(std::chrono::seconds{21});
            (void)pump_ready(ioc, *close_fut, std::chrono::seconds{30});
        }
        sess.reset();
    }

    // Constructs and opens. On failure returns the setup message.
    [[nodiscard]] std::optional<std::string> open() {
        sess = std::make_unique<Session>(engine, cfg, fixture.registry.get());
        if (!options.private_build) {
            adopt_fixture_routes(*sess, fixture);
        }
        auto fut = asio::co_spawn(ioc, sess->open(), asio::use_future);
        if (!pump_ready(ioc, fut, std::chrono::seconds{120})) {
            return std::string("OPEN-PUMP-MISS");
        }
        auto o = fut.get();
        if (!o) {
            return "OPEN-REFUSED err=" + std::to_string(static_cast<int>(o.error()));
        }
        return std::nullopt;
    }

    [[nodiscard]] Ctx peer_ctx(unsigned seq) const {
        return Ctx{options.role == session_role::acceptor ? "TW" : "ISLD",
                   options.role == session_role::acceptor ? "ISLD" : "TW", seq};
    }

    // Feeds a frame and pumps what it posted. A pump miss is reported in err.
    void feed(const std::string& frame, std::string* err) {
        auto bytes = std::as_bytes(std::span{frame.data(), frame.size()});
        auto fut = asio::co_spawn(ioc, sess->on_inbound_frame(bytes), asio::use_future);
        if (!pump_ready(ioc, fut, std::chrono::seconds{60})) {
            if (err != nullptr) {
                *err = "PUMP-MISS";
            }
            return;
        }
        auto r = fut.get();
        if (!r && err != nullptr) {
            *err = "err=" + std::to_string(static_cast<int>(r.error()));
        }
        for (int i = 0; i < 4; ++i) {
            ioc.restart();
            ioc.poll();
        }
    }

    // Sends an application payload and pumps what it posted; `out` holds what the peer receives.
    // Empty on success, else the failure's text.
    [[nodiscard]] std::string send(const std::string& payload) {
        out.clear();
        auto bytes = std::as_bytes(std::span{payload.data(), payload.size()});
        auto fut = asio::co_spawn(ioc, sess->send(bytes), asio::use_future);
        if (!pump_ready(ioc, fut, std::chrono::seconds{60})) {
            return "PUMP-MISS";
        }
        auto r = fut.get();
        if (!r) {
            return "err=" + std::to_string(static_cast<int>(r.error()));
        }
        for (int i = 0; i < 4; ++i) {
            ioc.restart();
            ioc.poll();
        }
        return {};
    }

    [[nodiscard]] unsigned next_in() {
        return static_cast<unsigned>(session_test_access::seqnum_mgr(*sess).next_inbound_unsafe());
    }

    // Feeds one frame and returns its ending; `out` holds only what this frame drew.
    [[nodiscard]] Ending deliver(const std::string& frame) {
        Ending e;
        out.clear();
        const int adm0 = app->admin;
        e.next_in_before = next_in();
        e.before = sess->state();
        feed(frame, &e.err);
        e.next_in_after = next_in();
        e.after = sess->state();
        e.from_app = app->app;
        e.from_admin_delta = app->admin - adm0;
        for (auto const& ev : sess->recent_events()) {
            if (std::holds_alternative<session_event_compid_authorization_failed>(ev)) {
                e.auth_failed = true;
            }
        }
        e.out = out;
        return e;
    }

    // Drives the peer's Logon to Active. On failure returns the setup message.
    [[nodiscard]] std::optional<std::string> establish() {
        std::string err;
        std::string extra = std::string("1137=") + fixture.peer_1137 + "\x01";
        feed(logon_frame(peer_ctx(1), extra), &err);
        if (sess->state() != fsm_state::Active) {
            return std::string("CANNOT-ESTABLISH state=") + state_name(sess->state()) +
                   " out=" + log_form_frames(out);
        }
        return std::nullopt;
    }

    // From Active, a graceful close to LogoutSent.
    [[nodiscard]] std::optional<std::string> enter_logout_sent() {
        close_fut = std::make_shared<std::future<expected_t<void>>>(
            asio::co_spawn(ioc, sess->close(close_mode::graceful), asio::use_future));
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{10};
        while (sess->state() != fsm_state::LogoutSent && std::chrono::steady_clock::now() < end) {
            ioc.restart();
            ioc.poll();
        }
        if (sess->state() != fsm_state::LogoutSent) {
            return std::string("NO-LOGOUTSENT state=") + state_name(sess->state());
        }
        return std::nullopt;
    }

    // The off-list identity of column h, delivered as research R-18's harness does.
    void attach_offlist_identity() {
        fixpp::tls::peer_identity pid;
        pid.subject_dn = "CN=STRANGER,O=Unknown,C=US";
        fixpp::transport::handshake_result hr{};
        hr.peer_id = std::move(pid);
        sess->attach_accepted_transport(std::make_unique<CaptureTransport>(&out), std::move(hr));
    }
};

// ── A cell: one session taken to an arm, one frame, one ending ──────────────────────────

struct CellRun {
    std::unique_ptr<Rig> rig;
    Ending ending;
    std::string setup_failure;  // non-empty: the cell never reached its frame
};

inline session_role role_of(Arm a) {
    return a == Arm::LogonSent ? session_role::initiator : session_role::acceptor;
}

// Takes a session of `fixture` to `arm`. On failure sets setup_failure.
inline CellRun enter_arm(Fixture fixture, Arm arm, RigOptions opts) {
    CellRun run;
    run.rig = std::make_unique<Rig>(std::move(fixture), std::move(opts));
    Rig& r = *run.rig;
    if (auto f = r.open()) {
        run.setup_failure = *f;
        return run;
    }
    if (r.options.mtls) {
        r.attach_offlist_identity();
    }
    if (arm == Arm::Active || arm == Arm::LogoutSent) {
        if (auto f = r.establish()) {
            run.setup_failure = *f;
            return run;
        }
    }
    if (arm == Arm::LogoutSent) {
        if (auto f = r.enter_logout_sent()) {
            run.setup_failure = *f;
            return run;
        }
    }
    return run;
}

inline unsigned first_seq_of(Arm a) { return (a == Arm::Active || a == Arm::LogoutSent) ? 2U : 1U; }

// A keyed cell: the shared set, research R-18's configuration, appended to the trace.
inline CellRun run_keyed_cell(Shape shape, bool validate, Arm arm, const Input& in) {
    RigOptions opts;
    opts.validate = validate;
    opts.role = role_of(arm);
    opts.mtls = in.mtls;
    opts.posture = in.posture;
    CellRun run = enter_arm(shape_fixture(shape), arm, std::move(opts));
    std::string text;
    if (run.setup_failure.empty()) {
        run.ending = run.rig->deliver(in.make(run.rig->peer_ctx(first_seq_of(arm))));
        text = log_form(run.ending);
    } else {
        text = run.setup_failure;
    }
    append_trace(std::string(shape_name(shape)) + "\t" + (validate ? "on" : "off") + "\t" +
                 arm_name(arm) + "\t" + in.id + "\t" + text);
    return run;
}

// A cell with no log line of its own: a fixture and a frame the witness builds. Its trace line
// names the witness, so the comparison never takes it for a keyed cell.
inline CellRun run_own_cell(const std::string& label, Fixture fixture, bool validate, Arm arm,
                            const std::function<std::string(const Rig&)>& make,
                            RigOptions opts = {}) {
    opts.validate = validate;
    opts.role = role_of(arm);
    CellRun run = enter_arm(std::move(fixture), arm, std::move(opts));
    std::string text;
    if (run.setup_failure.empty()) {
        run.ending = run.rig->deliver(make(*run.rig));
        text = log_form(run.ending);
    } else {
        text = run.setup_failure;
    }
    append_trace(std::string("-\t") + (validate ? "on" : "off") + "\t" + arm_name(arm) + "\t" +
                 label + "\t" + text);
    return run;
}

// Reads a field through the view the callback was given.
inline std::string field_text(const CountingApp::View& v, std::uint16_t tag) {
    const auto f = v.get(tag);
    if (!f) {
        return "<absent>";
    }
    const auto b = f->bytes();
    return {reinterpret_cast<const char*>(b.data()), b.size()};
}

inline void expect_ending(const CellRun& run, const std::string& expected) {
    if (!run.setup_failure.empty()) {
        ADD_FAILURE() << "setup: " << run.setup_failure;
        return;
    }
    EXPECT_EQ(short_form(run.ending), expected) << "log form: " << log_form(run.ending);
}

// ── An Engine that registers and is stopped ─────────────────────────────────────────────

// Register-time cells: no start() unless the cell asks. The destructor stops the engine, as
// ~Engine requires, and reports nothing.
class EngineProbe {
public:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    std::shared_ptr<CountingApp> app = std::make_shared<CountingApp>();
    std::unique_ptr<Engine> engine;

    explicit EngineProbe(std::vector<DictPtr> dicts,
                         std::shared_ptr<fixpp::core::Logger> logger = nullptr) {
        const auto utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
        const auto stp = fixpp::core::steady_time_point{} + std::chrono::seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        fixpp::core::EngineConfig cfg;
        cfg.executor = ioc.get_executor();
        cfg.clock = clock;
        cfg.application = app;
        cfg.dictionaries = std::move(dicts);
        cfg.logger = std::move(logger);
        engine = std::make_unique<Engine>(ioc.get_executor(), std::move(cfg));
    }
    EngineProbe(const EngineProbe&) = delete;
    EngineProbe& operator=(const EngineProbe&) = delete;
    EngineProbe(EngineProbe&&) = delete;
    EngineProbe& operator=(EngineProbe&&) = delete;
    ~EngineProbe() {
        auto fut = asio::co_spawn(ioc, engine->stop(), asio::use_future);
        (void)pump_ready(ioc, fut, std::chrono::seconds{30});
    }
};

// A session config for register_session and for a direct open: FIXT, acceptor, no transport.
inline SessionConfig plain_fixt_config(asio::io_context& ioc, DictPtr s, bool validate,
                                       std::optional<dd::application_version> default_appl,
                                       const std::string& sender = "ISLD") {
    SessionConfig cfg;
    cfg.sender_comp_id = sender;
    cfg.target_comp_id = "TW";
    cfg.begin_string = kFT;
    cfg.heartbeat_interval = std::chrono::seconds{30};
    cfg.security_profile = SecurityProfile{SecurityProfile::kind::one_way_ca};
    cfg.dictionary = std::move(s);
    cfg.executor_override = ioc.get_executor();
    cfg.role = session_role::acceptor;
    cfg.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
    if (default_appl) {
        cfg.default_appl_ver_id = *default_appl;
    }
    cfg.validate_inbound_messages = validate;
    cfg.transport_send = [](std::span<const std::byte>) {};
    return cfg;
}

// ── Keyed tables, one row per input of data-model E-7 ───────────────────────────────────

// `a` null means "A answers as T" (E-7's `#### After`); `l` null means L answers as T too.
struct KeyedRow {
    const char* cell;  // E-7's cell id, or the witness's name for a row E-7 has no column for
    const char* input;
    const char* t;
    const char* a = nullptr;
    const char* l = nullptr;
};

struct KeyedCell {
    KeyedRow row;
    Shape shape;
};

inline std::string expected_on(const KeyedRow& r, Shape s) {
    switch (s) {
        case Shape::T:
            return r.t;
        case Shape::A:
            return r.a != nullptr ? r.a : r.t;
        case Shape::L:
            return r.l != nullptr ? r.l : r.t;
    }
    return r.t;
}

template <std::size_t N>
std::vector<KeyedCell> on_every_shape(const KeyedRow (&rows)[N]) {
    std::vector<KeyedCell> cells;
    for (Shape s : {Shape::T, Shape::A, Shape::L}) {
        for (const auto& r : rows) {
            cells.push_back(KeyedCell{r, s});
        }
    }
    return cells;
}

inline std::string name_token(std::string_view s) {
    std::string out;
    for (char c : s) {
        out += std::isalnum(static_cast<unsigned char>(c)) != 0 ? c : '_';
    }
    return out;
}

inline std::string keyed_cell_name(const testing::TestParamInfo<KeyedCell>& info) {
    return name_token(info.param.row.cell) + "_" + shape_name(info.param.shape) + "_" +
           name_token(info.param.row.input);
}

inline void PrintTo(const KeyedCell& c, std::ostream* os) {
    *os << c.row.cell << " " << shape_name(c.shape) << " " << c.row.input;
}

}  // namespace fixpp::session::test099
