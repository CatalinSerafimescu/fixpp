// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_098_dialect_overlay_inbound.cpp
//
// 098-dialect-overlay, contracts/witnesses.md W-5, the overlay and edited arms
// (US1 AS-1..AS-6, SC-002, FR-018). Two FIX.4.4 sessions with
// validate_inbound_messages = true are fed each W-5 frame
// (test_098_inbound_frames.hpp):
//   - the overlay session, whose dictionary is dictionaries/FIX44.xml with W-2's
//     FIX44 overlay (tests/dictionary/fixtures/098/real/FIX44_overlay.xml) loaded
//     through `dict::load_any_with_overlay`;
//   - the edited session, whose dictionary is FIX44.xml with that overlay's
//     hand-written insertion patch (FIX44.patch beside it) applied by the 098
//     patch helper and loaded with `XmlLoader::load_from_string`.
// The enum-closure frame has its own pair, over FIX44_enum_closure_overlay.xml and
// FIX44_enum_closure.patch, so the main pair's dictionaries leave Text(58) open.
// The base session over the unmodified FIX44.xml is
// test_098_dialect_overlay_inbound_base.cpp.
//
// Every frame asserts that the two sessions agree: both accept it, or both send
// one Reject with the same fields. On a frame both accept, each session's
// application callback reads what the scenario names, and the read is asserted
// on each. The callback reads and the validator are two consumers of one
// dictionary, so agreement on accept or reject alone would not show the reads
// (FR-018). A frame the scenario says the edited dictionary accepts is asserted
// accepted, so a read can never be skipped by both sessions refusing it.
//
// The refused frames' expected Reject fields are derived from the validator as
// test_098_dialect_overlay_inbound_base.cpp's header describes: a missing
// Required field is step 2 (373 = 1, the missing tag in 371); a value outside a
// field's listed values is step 1's enum check (373 = 5, the field in 371).
// Every frame is fed to its own freshly established session at MsgSeqNum 2.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fixpp/core/clock.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/load_any.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dictionary/overlay_patch.hpp"
#include "support/extract_tag.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"
#include "test_098_inbound_frames.hpp"

namespace fixpp::session::test {
namespace {

namespace f098 = fixpp::session::test098;
using fixpp::core::expected_t;
using fixpp::wire::access_mode;
using fixpp::wire::MessageView;

std::filesystem::path dict_dir() { return FIXPP_DICT_DATA_DIR; }
std::filesystem::path real_dir() { return std::filesystem::path{FIXPP_098_FIXTURE_DIR} / "real"; }

std::string read_file(std::filesystem::path const& p) {
    std::ifstream in(p, std::ios::binary);
    EXPECT_TRUE(in.good()) << "cannot open " << p;
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

using dict_ptr = std::shared_ptr<fixpp::dict::Dictionary const>;

// FIX44.xml with `overlay` through `dict::load_any_with_overlay`.
dict_ptr overlay_dictionary(char const* overlay) {
    return std::make_shared<fixpp::dict::Dictionary const>(fixpp::dict::load_any_with_overlay(
        dict_dir() / "FIX44.xml", real_dir() / overlay, std::pmr::new_delete_resource()));
}

// FIX44.xml with `patch` applied by the patch helper; a patch that does not apply
// fails the test and gives a null dictionary.
dict_ptr edited_dictionary(char const* patch) {
    auto const parsed = overlay_test::parse_patch(read_file(real_dir() / patch));
    EXPECT_TRUE(parsed.error.empty()) << patch << ": " << parsed.error;
    auto const applied =
        overlay_test::apply_patch(read_file(dict_dir() / "FIX44.xml"), parsed.hunks);
    EXPECT_TRUE(applied.error.empty()) << patch << ": " << applied.error;
    if (!parsed.error.empty() || !applied.error.empty()) {
        return nullptr;
    }
    return std::make_shared<fixpp::dict::Dictionary const>(
        fixpp::dict::XmlLoader{}.load_from_string(applied.text, std::pmr::new_delete_resource()));
}

struct dictionaries {
    dict_ptr overlay;
    dict_ptr edited;
};

dictionaries const& main_pair() {
    static dictionaries const d{overlay_dictionary("FIX44_overlay.xml"),
                                edited_dictionary("FIX44.patch")};
    return d;
}

dictionaries const& enum_closure_pair() {
    static dictionaries const d{overlay_dictionary("FIX44_enum_closure_overlay.xml"),
                                edited_dictionary("FIX44_enum_closure.patch")};
    return d;
}

// What the application callback read from one message.
struct app_read {
    std::string msg_type;
    std::optional<std::string> custom_field;  // 5001 by tag
    std::optional<std::string> ord_type;      // 40 by tag
    std::optional<std::string> data_value;    // 5011 by tag, its full counted bytes
    // Parties(453), through the group read: its instance count, and 5002 read
    // inside the first instance's slice.
    std::optional<std::size_t> parties_entries;
    std::optional<std::string> parties_member;
};

// The value of `tag` inside one group entry's bytes (tag=value fields, each
// ending in SOH, none of them counted).
std::optional<std::string> field_in_entry(std::string_view entry, std::uint16_t tag) {
    std::string const head = std::to_string(tag) + "=";
    std::size_t pos = 0;
    while (pos < entry.size()) {
        std::size_t const soh = entry.find('\x01', pos);
        std::string_view const f =
            entry.substr(pos, soh == std::string_view::npos ? entry.size() - pos : soh - pos);
        if (f.starts_with(head)) {
            return std::string{f.substr(head.size())};
        }
        if (soh == std::string_view::npos) {
            break;
        }
        pos = soh + 1;
    }
    return std::nullopt;
}

class ReadingApp final : public Application {
public:
    std::vector<app_read> reads;

    expected_t<void> fromApp(const MessageView<access_mode::Index>& msg,
                             const SessionId& /*id*/) override {
        app_read r;
        r.msg_type = std::string{msg.msg_type()};
        auto const str = [&](std::uint16_t tag) -> std::optional<std::string> {
            auto const v = msg.get(tag);
            if (!v.has_value()) {
                return std::nullopt;
            }
            return std::string{v->as_string()};
        };
        r.custom_field = str(f098::kCustomFieldTag);
        r.ord_type = str(f098::kOrdTypeTag);
        r.data_value = str(f098::kDataTag);
        if (auto const g = msg.offsets().group(453); g.has_value()) {
            auto const slices = msg.offsets().group_slices(453);
            r.parties_entries = slices.size();
            if (!slices.empty()) {
                std::string_view const entry{reinterpret_cast<char const*>(slices[0].data),
                                             slices[0].len};
                r.parties_member = field_in_entry(entry, f098::kPartiesMemberTag);
            }
        }
        reads.push_back(std::move(r));
        return {};
    }
};

// What one frame drew from a freshly established session.
struct Run {
    std::vector<std::vector<std::byte>> sent;  // frames the session sent while fed
    std::vector<app_read> reads;
    fsm_state state = fsm_state::Disconnected;
    bool established = false;
};

struct Fixture {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    std::shared_ptr<ReadingApp> app = std::make_shared<ReadingApp>();
    fixpp::core::EngineConfig engine;
    std::vector<std::vector<std::byte>> captured;

    Fixture() {
        using namespace std::chrono;
        // 2024-01-01T00:00:00Z, the instant f098::kSendingTime names.
        auto utc = system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
        engine.application = app;
    }

    SessionConfig make_cfg(dict_ptr const& dictionary) {
        SessionConfig cfg;
        cfg.sender_comp_id = std::string{f098::kOurCompId};
        cfg.target_comp_id = std::string{f098::kPeerCompId};
        cfg.begin_string = std::string{f098::kBeginString};
        cfg.heartbeat_interval = std::chrono::seconds{0};
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = dictionary;
        cfg.executor_override = ioc.get_executor();
        cfg.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
        cfg.validate_inbound_messages = true;
        cfg.transport_send = [this](std::span<const std::byte> frame) {
            captured.emplace_back(frame.begin(), frame.end());
        };
        return cfg;
    }

    // Runs `fut` for a bounded window; false (after reporting) when it missed.
    template <class Fut>
    bool pump(Fut& fut, const char* site) {
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, std::chrono::milliseconds{200},
                                                        site)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, site);
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << site;
            return false;
        }
        return true;
    }

    // open() (the Logon goes out at 34=1), then the peer's Logon at 34=1.
    bool open_to_active(Session& sess) {
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        if (!pump(fut, "DialectOverlayInbound098::open")) {
            return false;
        }
        if (!fut.get().has_value()) {
            ADD_FAILURE() << "open() failed";
            return false;
        }
        std::string logon_body = f098::field(98, "0");
        logon_body += f098::field(108, "30");
        auto const logon = f098::make_frame("A", 1, logon_body);
        auto fut2 = asio::co_spawn(ioc, sess.on_inbound_frame(logon), asio::use_future);
        if (!pump(fut2, "DialectOverlayInbound098::logon")) {
            return false;
        }
        (void)fut2.get();
        if (sess.state() != fsm_state::Active) {
            ADD_FAILURE() << "the session did not reach Active";
            return false;
        }
        return true;
    }

    Run feed_one(dict_ptr const& dictionary, std::vector<std::byte> const& frame) {
        Run run;
        if (dictionary == nullptr) {
            ADD_FAILURE() << "no dictionary";
            return run;
        }
        Session sess{engine, make_cfg(dictionary)};
        if (!open_to_active(sess)) {
            return run;
        }
        run.established = true;
        std::size_t const before = captured.size();
        std::size_t const reads_before = app->reads.size();
        auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(frame), asio::use_future);
        if (pump(fut, "DialectOverlayInbound098::feed")) {
            (void)fut.get();
        }
        run.sent.assign(captured.begin() + static_cast<std::ptrdiff_t>(before), captured.end());
        run.reads.assign(app->reads.begin() + static_cast<std::ptrdiff_t>(reads_before),
                         app->reads.end());
        run.state = sess.state();
        return run;
    }
};

// A run's observable outcome: every Reject of either kind it sent, by the fields
// W-5 compares, and what reached fromApp.
struct outcome {
    std::vector<std::vector<std::string>> rejects;  // 35, 45, 372, 373, 371, 58
    std::vector<std::string> from_app_types;
    fsm_state state = fsm_state::Disconnected;
    bool operator==(outcome const&) const = default;
};

outcome outcome_of(Run const& run) {
    outcome o;
    for (auto const& f : run.sent) {
        std::string const mt{fixpp::test_support::extract_tag(f, 35)};
        if (mt != "3" && mt != "j") {
            continue;
        }
        std::vector<std::string> fields;
        for (std::uint32_t const tag : {35U, 45U, 372U, 373U, 371U, 58U}) {
            fields.emplace_back(fixpp::test_support::extract_tag(f, tag));
        }
        o.rejects.push_back(std::move(fields));
    }
    for (auto const& r : run.reads) {
        o.from_app_types.push_back(r.msg_type);
    }
    o.state = run.state;
    return o;
}

std::string describe(outcome const& o) {
    std::string s = "rejects:";
    for (auto const& r : o.rejects) {
        s += " [35=" + r[0] + " 45=" + r[1] + " 372=" + r[2] + " 373=" + r[3] + " 371=" + r[4] +
             " 58=" + r[5] + "]";
    }
    s += " fromApp:";
    for (auto const& t : o.from_app_types) {
        s += " " + t;
    }
    return s;
}

std::string describe_reads(Run const& run) {
    std::string s;
    for (auto const& r : run.reads) {
        auto const opt = [](std::optional<std::string> const& v) {
            return v ? "\"" + *v + "\"" : std::string{"absent"};
        };
        std::string data = r.data_value.value_or("");
        for (char& c : data) {
            c = c == '\x01' ? '|' : c;
        }
        s += " [35=" + r.msg_type + " 5001=" + opt(r.custom_field) + " 40=" + opt(r.ord_type) +
             " 5011=" +
             (r.data_value ? "\"" + data + "\" (" + std::to_string(r.data_value->size()) +
                                 " bytes, SOH shown as |)"
                           : std::string{"absent"}) +
             " Parties instances=" +
             (r.parties_entries ? std::to_string(*r.parties_entries) : std::string{"absent"}) +
             " 5002 in the instance=" + opt(r.parties_member) + "]";
    }
    return s;
}

// Both runs, established, and agreeing on the outcome; the report names both.
void expect_agree(std::string_view row, Run const& overlay, Run const& edited) {
    std::cout << "[098 W-5 " << row << "] overlay: " << describe(outcome_of(overlay))
              << " reads:" << describe_reads(overlay) << "\n[098 W-5 " << row
              << "] edited: " << describe(outcome_of(edited)) << " reads:" << describe_reads(edited)
              << '\n';
    ASSERT_TRUE(overlay.established) << "the overlay session did not reach Active";
    ASSERT_TRUE(edited.established) << "the edited session did not reach Active";
    EXPECT_EQ(outcome_of(overlay), outcome_of(edited))
        << "overlay: " << describe(outcome_of(overlay))
        << "\nedited: " << describe(outcome_of(edited));
}

// The frame reached fromApp once as `msg_type`, with no Reject of either kind.
void expect_accepted(Run const& run, std::string_view msg_type, char const* who) {
    outcome const o = outcome_of(run);
    EXPECT_TRUE(o.rejects.empty()) << who << ": " << describe(o);
    ASSERT_EQ(run.reads.size(), 1U) << who << ": fromApp called once; " << describe(o);
    EXPECT_EQ(run.reads.front().msg_type, msg_type) << who;
    EXPECT_EQ(run.state, fsm_state::Active) << who << ": the session stays Active";
}

// The frame drew exactly one Reject(35=3) with these fields and nothing reached
// fromApp.
void expect_reject(Run const& run, std::string_view ref_msg_type, std::string_view reason,
                   std::string_view ref_tag, char const* who) {
    outcome const o = outcome_of(run);
    ASSERT_EQ(o.rejects.size(), 1U) << who << ": exactly one Reject; " << describe(o);
    EXPECT_EQ(o.rejects[0][0], "3") << who << ": Reject(35=3)";
    EXPECT_EQ(o.rejects[0][1], "2") << who << ": RefSeqNum(45)";
    EXPECT_EQ(o.rejects[0][2], ref_msg_type) << who << ": RefMsgType(372)";
    EXPECT_EQ(o.rejects[0][3], reason) << who << ": SessionRejectReason(373)";
    EXPECT_EQ(o.rejects[0][4], ref_tag) << who << ": RefTagID(371)";
    EXPECT_TRUE(run.reads.empty()) << who << ": nothing reaches fromApp";
    EXPECT_EQ(run.state, fsm_state::Active) << who << ": the session stays Active";
}

struct pair_runs {
    Run overlay;
    Run edited;
};

template <class MakeFrame>
pair_runs feed_both(dictionaries const& d, MakeFrame make) {
    pair_runs r;
    {
        Fixture fx;
        r.overlay = fx.feed_one(d.overlay, make(2));
    }
    {
        Fixture fx;
        r.edited = fx.feed_one(d.edited, make(2));
    }
    return r;
}

// AS-1: 5001 is declared on NewOrderSingle; both read it by tag.
TEST(DialectOverlayInbound098, AS1_CustomFieldOnNewOrderSingle_AcceptedAndRead) {
    auto const r = feed_both(main_pair(), f098::frame_as1_custom_field);
    expect_agree("AS-1", r.overlay, r.edited);
    for (auto const* run : {&r.overlay, &r.edited}) {
        char const* who = run == &r.overlay ? "overlay" : "edited";
        expect_accepted(*run, f098::kCustomFieldMsgType, who);
        if (!run->reads.empty()) {
            EXPECT_EQ(run->reads.front().custom_field, std::string{f098::kCustomFieldValue})
                << who << ": 5001 by tag";
        }
    }
}

// AS-2: 5002 is a member of Parties' NoPartyIDs(453); both read it inside the
// Parties entry through the group read.
TEST(DialectOverlayInbound098, AS2_CustomPartiesMember_AcceptedAndReadInTheEntry) {
    auto const r = feed_both(main_pair(), f098::frame_as2_parties_member);
    expect_agree("AS-2", r.overlay, r.edited);
    for (auto const* run : {&r.overlay, &r.edited}) {
        char const* who = run == &r.overlay ? "overlay" : "edited";
        expect_accepted(*run, f098::kPartiesMemberMsgType, who);
        if (!run->reads.empty()) {
            EXPECT_EQ(run->reads.front().parties_entries, std::size_t{1})
                << who << ": the group read sees one Parties instance";
            EXPECT_EQ(run->reads.front().parties_member, std::string{f098::kPartiesMemberValue})
                << who << ": 5002 inside the Parties instance";
        }
    }
}

// AS-3: U1 with its Required field reaches the callback; without it, step 2
// refuses it.
TEST(DialectOverlayInbound098, AS3_PrivateMsgTypeWithRequired_ReachesTheCallback) {
    auto const r = feed_both(main_pair(), f098::frame_as3_private_with_required);
    expect_agree("AS-3 with", r.overlay, r.edited);
    expect_accepted(r.overlay, f098::kPrivateMsgType, "overlay");
    expect_accepted(r.edited, f098::kPrivateMsgType, "edited");
}

TEST(DialectOverlayInbound098, AS3_PrivateMsgTypeMissingRequired_Rejected) {
    auto const r = feed_both(main_pair(), f098::frame_as3_private_missing_required);
    expect_agree("AS-3 missing", r.overlay, r.edited);
    expect_reject(r.overlay, f098::kPrivateMsgType, "1", "5003", "overlay");
    expect_reject(r.edited, f098::kPrivateMsgType, "1", "5003", "edited");
}

// AS-4: the custom Length+Data pair; both read its Data field as its full counted
// bytes, SOH included.
TEST(DialectOverlayInbound098, AS4_CustomLengthDataWithSoh_AcceptedAndReadWhole) {
    auto const r = feed_both(main_pair(), f098::frame_as4_length_data);
    expect_agree("AS-4", r.overlay, r.edited);
    for (auto const* run : {&r.overlay, &r.edited}) {
        char const* who = run == &r.overlay ? "overlay" : "edited";
        expect_accepted(*run, f098::kLengthDataMsgType, who);
        if (!run->reads.empty()) {
            EXPECT_EQ(run->reads.front().data_value, std::string{f098::kDataValue})
                << who << ": 5011 holds the full counted value";
        }
    }
}

// AS-5: Account(1) is promoted to Required on OrderCancelRequest only; missing
// there it is refused, missing on NewOrderSingle it is accepted.
TEST(DialectOverlayInbound098, AS5_PromotedFieldMissing_Rejected) {
    auto const r = feed_both(main_pair(), f098::frame_as5_promoted_missing);
    expect_agree("AS-5 promoted", r.overlay, r.edited);
    expect_reject(r.overlay, f098::kPromotedMsgType, "1", "1", "overlay");
    expect_reject(r.edited, f098::kPromotedMsgType, "1", "1", "edited");
}

TEST(DialectOverlayInbound098, AS5_PromotedFieldMissingOnOtherMsgType_Accepted) {
    auto const r = feed_both(main_pair(), f098::frame_as5_other_msgtype);
    expect_agree("AS-5 other", r.overlay, r.edited);
    expect_accepted(r.overlay, f098::kPromotedStaysOptionalMsgType, "overlay");
    expect_accepted(r.edited, f098::kPromotedStaysOptionalMsgType, "edited");
}

// AS-6: Z is listed on OrdType(40); both read it.
TEST(DialectOverlayInbound098, AS6_CustomOrdTypeValue_AcceptedAndRead) {
    auto const r = feed_both(main_pair(), f098::frame_as6_ordtype_custom);
    expect_agree("AS-6", r.overlay, r.edited);
    for (auto const* run : {&r.overlay, &r.edited}) {
        char const* who = run == &r.overlay ? "overlay" : "edited";
        expect_accepted(*run, f098::kOrdTypeMsgType, who);
        if (!run->reads.empty()) {
            EXPECT_EQ(run->reads.front().ord_type, std::string{f098::kOrdTypeCustomValue})
                << who << ": 40 by tag";
        }
    }
}

// Enum closure (N-2): the closure overlay lists a first value on Text(58), so a
// value it does not list is refused by step 1's enum check (the base session
// accepts it).
TEST(DialectOverlayInbound098, EnumClosure_UnlistedTextValue_Rejected) {
    auto const r = feed_both(enum_closure_pair(), f098::frame_enum_closure);
    expect_agree("enum closure", r.overlay, r.edited);
    expect_reject(r.overlay, f098::kEnumClosureMsgType, "5", "58", "overlay");
    expect_reject(r.edited, f098::kEnumClosureMsgType, "5", "58", "edited");
}

}  // namespace
}  // namespace fixpp::session::test
