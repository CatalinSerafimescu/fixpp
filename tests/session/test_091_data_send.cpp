// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_091_data_send.cpp
//
// 091-data-field-bytes T039 [US1] -- the session send path for Length+Data
// pairs (specs/091-data-field-bytes/quickstart.md §5; research.md R-9).
//
// Session::send_impl's fixpp#422 header partition moves header-class fields
// ahead of the body. A counted Data value inherits its Length's classification
// (the `field->counted ? prev_header : ...` arm), so the pair leaves together
// and a SOH inside the value never splits it. No production change backs this
// file; it witnesses the path end to end:
//
//   Witness 1 (XmlData 212+213, SecureData 90+91): a payload whose pair
//   follows body fields and whose Data value holds SOH goes out with the pair
//   adjacent, Length first, inside the header, and the value re-parses
//   verbatim through the dictionary-aware Parser<Index>.
//
//   Witness 2 (message_encoding): a generated fixpp::v44 NewOrderSingle with
//   `message_encoding` and a non-ASCII `encoded_text` goes out with 347 in the
//   header and 354+355 in the body, the Data bytes verbatim.
//
// Each witness pins the complete wire tag order of the transmitted frame as
// the production parser reads it, and the outbound MsgSeqNum consumed by the
// send (seqnum_mgr_test_access, FIXPP_TEST_HOOKS).
//
// Build: cmake --build build/linux-clang-debug --target session_091_data_send
// Run:   ctest --test-dir build/linux-clang-debug -R session_091_data_send -V

#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/v44/all.hpp>  // GENERATED -- build_NewOrderSingle / NewOrderSingleArgs
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "session/support/frame_field_extract.hpp"
#include "support/app_message_read_scaffold.hpp"
#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"

using namespace std::chrono_literals;
using fixpp::session::test_support::extract_field;

namespace fixpp::session::test {
namespace {

constexpr auto kWindow = 200ms;

std::vector<std::byte> to_bytes(std::string_view sv) {
    auto const bytes = std::as_bytes(std::span{sv});
    return {bytes.begin(), bytes.end()};
}

std::vector<std::byte> make_fix44_frame(std::string_view body) {
    return fixpp_test_support::make_frame("FIX.4.4", body);
}

std::vector<std::byte> make_peer_logon_44() {
    return make_fix44_frame(
        "35=A\x01"
        "34=1\x01"
        "49=TW\x01"
        "52=20240101-00:00:00.000\x01"
        "56=ISLD\x01"
        "98=0\x01"
        "108=30\x01");
}

// The transmitted frame as the dictionary-aware production parser reads it:
// every field's tag in wire order, and each field's value bytes.
struct Reparsed {
    std::vector<std::uint32_t> tags;
    std::vector<std::string> values;

    // The value of the one field carrying `tag`; empty when absent or repeated.
    [[nodiscard]] std::string value_of(std::uint32_t tag) const {
        std::string found;
        int seen = 0;
        for (std::size_t i = 0; i < tags.size(); ++i) {
            if (tags[i] == tag) {
                found = values[i];
                ++seen;
            }
        }
        return seen == 1 ? found : std::string{};
    }
};

Reparsed reparse(std::vector<std::byte> const& frame, fixpp::dict::table_view const& tv) {
    std::pmr::monotonic_buffer_resource arena{16384};
    auto mv = fixpp_test_support::parse_dict(frame, tv, &arena);
    Reparsed r;
    for (auto const& e : mv.offsets().entries()) {
        r.tags.push_back(e.tag);
        r.values.emplace_back(reinterpret_cast<char const*>(frame.data()) + e.offset, e.length);
    }
    return r;
}

Reparsed reparse(std::vector<std::byte> const& frame) {
    std::pmr::monotonic_buffer_resource arena{16384};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&arena);
    return reparse(frame, dict.as_table_view());
}

// A dictionary declaring one custom Length+Data pair whose halves classify
// differently in send_impl's header partition: the Length 5001 is a body tag,
// the Data 142 (SenderLocationID) is in the header set, and neither is a
// standard pair tag, so the pair comes from this dictionary alone. The
// adjacency inside the message is what XmlLoader pairs.
constexpr std::string_view kStraddlingPairXml = R"xml(
<fix major="4" minor="4">
  <header>
    <field number="8"  name="BeginString"  required="Y"/>
    <field number="9"  name="BodyLength"   required="Y"/>
    <field number="35" name="MsgType"      required="Y"/>
    <field number="49" name="SenderCompID" required="Y"/>
    <field number="56" name="TargetCompID" required="Y"/>
    <field number="34" name="MsgSeqNum"    required="Y"/>
    <field number="52" name="SendingTime"  required="Y"/>
  </header>
  <trailer>
    <field number="10" name="CheckSum" required="Y"/>
  </trailer>
  <messages>
    <message name="NewOrderSingle" msgtype="D" msgcat="app">
      <field number="11"   name="ClOrdID"          required="N"/>
      <field number="54"   name="Side"             required="N"/>
      <field number="5001" name="CustomLen"        required="N"/>
      <field number="142"  name="SenderLocationID" required="N"/>
      <field number="55"   name="Symbol"           required="N"/>
    </message>
  </messages>
  <fields>
    <field number="8"    name="BeginString"      type="STRING"/>
    <field number="9"    name="BodyLength"       type="INT"/>
    <field number="35"   name="MsgType"          type="STRING"/>
    <field number="49"   name="SenderCompID"     type="STRING"/>
    <field number="56"   name="TargetCompID"     type="STRING"/>
    <field number="34"   name="MsgSeqNum"        type="INT"/>
    <field number="52"   name="SendingTime"      type="UTCTIMESTAMP"/>
    <field number="10"   name="CheckSum"         type="STRING"/>
    <field number="11"   name="ClOrdID"          type="STRING"/>
    <field number="54"   name="Side"             type="CHAR"/>
    <field number="55"   name="Symbol"           type="STRING"/>
    <field number="5001" name="CustomLen"        type="LENGTH"/>
    <field number="142"  name="SenderLocationID" type="DATA"/>
  </fields>
</fix>
)xml";

// Same ownership shape as make_minimal_dictionary (tests/support).
std::shared_ptr<const fixpp::dict::Dictionary> make_straddling_pair_dictionary() {
    auto mr = std::make_shared<std::pmr::monotonic_buffer_resource>(64U * 1024U);
    auto* d = new fixpp::dict::Dictionary{
        fixpp::dict::XmlLoader{}.load_from_string(kStraddlingPairXml, mr.get())};
    return std::shared_ptr<const fixpp::dict::Dictionary>{
        d, [mr](fixpp::dict::Dictionary const* p) { delete p; }};
}

class DataSend091 : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine{};
    std::vector<std::vector<std::byte>> captured_frames;

    void SetUp() override {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    SessionConfig make_cfg(std::shared_ptr<const fixpp::dict::Dictionary> dict = nullptr) {
        SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.4";
        cfg.heartbeat_interval = 0s;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = dict ? std::move(dict) : fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
        cfg.transport_send = [this](std::span<const std::byte> frame) {
            captured_frames.emplace_back(frame.begin(), frame.end());
        };
        return cfg;
    }

    void drive_to_active(Session& sess) {
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, kWindow,
                                                        "DataSend091::drive_to_active/open")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "DataSend091::drive_to_active/open");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "DataSend091::drive_to_active/open";
            return;
        }
        ASSERT_TRUE(fut.get().has_value()) << "open() failed";

        auto const logon = make_peer_logon_44();
        auto fut2 = asio::co_spawn(ioc, sess.on_inbound_frame(logon), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut2, kWindow,
                                                        "DataSend091::drive_to_active/logon")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "DataSend091::drive_to_active/logon");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "DataSend091::drive_to_active/logon";
            return;
        }
        ASSERT_TRUE(fut2.get().has_value()) << "peer Logon feed failed";
        ASSERT_EQ(sess.state(), fsm_state::Active);
        captured_frames.clear();  // discard the Logon reply
    }

    fixpp::core::expected_t<void> send_payload(Session& sess, std::span<const std::byte> payload,
                                               char const* label) {
        auto fut = asio::co_spawn(ioc, sess.send(payload), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, kWindow, label)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, label);
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << label;
            return {};  // the ADD_FAILURE above already fails the test
        }
        return fut.get();
    }

    // Sends `payload` on an Active session and returns the one transmitted
    // frame; asserts the send consumed exactly one outbound MsgSeqNum and that
    // the frame carries it.
    std::vector<std::byte> send_one(std::span<const std::byte> payload, char const* label,
                                    std::shared_ptr<const fixpp::dict::Dictionary> dict = nullptr) {
        Session sess(engine, make_cfg(std::move(dict)));
        drive_to_active(sess);
        if (HasFatalFailure()) return {};
        seqnum_t const before = sess.seqnum_mgr_test_access().peek_outbound();
        auto const r = send_payload(sess, payload, label);
        EXPECT_TRUE(r.has_value()) << label << ": a well-formed counted pair must not be refused";
        EXPECT_EQ(sess.seqnum_mgr_test_access().peek_outbound(), before + 1U) << label;
        if (captured_frames.size() != 1U) {
            ADD_FAILURE() << label << ": expected one transmitted frame, got "
                          << captured_frames.size();
            return {};
        }
        std::vector<std::byte> frame = captured_frames.back();
        EXPECT_EQ(extract_field(std::span<const std::byte>(frame), 34),
                  std::to_string(static_cast<std::uint32_t>(before)))
            << label;
        return frame;
    }
};

// ── Witness 1: a header-class pair after body fields, SOH in the Data value ──

class HeaderPairSend091 : public DataSend091 {
protected:
    // The pair follows body fields 11 and 54, so the header pass must move it;
    // the Data value holds SOH and `34=`/`10=` pieces a SOH-split scanner would
    // read as fields.
    void expect_header_pair_moved_together(std::uint32_t length_tag, std::uint32_t data_tag) {
        std::string const value =
            std::string{"<x a=\"1\">"} + '\x01' + "34=99" + '\x01' + "10=000</x>";
        std::string const payload =
            std::string{"35=D"} + '\x01' + "11=ORD" + '\x01' + "54=1" + '\x01' +
            std::to_string(length_tag) + "=" + std::to_string(value.size()) + '\x01' +
            std::to_string(data_tag) + "=" + value + '\x01' + "55=AAPL" + '\x01';
        auto const bytes = to_bytes(payload);
        auto const frame = send_one(bytes, "expect_header_pair_moved_together");
        ASSERT_FALSE(frame.empty());

        Reparsed const rp = reparse(frame);
        std::vector<std::uint32_t> const expected_tags{8,          9,        35, 34, 49, 52, 56,
                                                       length_tag, data_tag, 11, 54, 55, 10};
        EXPECT_EQ(rp.tags, expected_tags)
            << "the pair must go out adjacent, Length first, inside the header";
        EXPECT_EQ(rp.value_of(length_tag), std::to_string(value.size()));
        EXPECT_EQ(rp.value_of(data_tag), value) << "the Data value must re-parse verbatim";
    }
};

TEST_F(HeaderPairSend091, XmlData_212_213_WithSohInValue_MovedIntoTheHeaderTogether) {
    expect_header_pair_moved_together(212, 213);
}

TEST_F(HeaderPairSend091, SecureData_90_91_WithSohInValue_MovedIntoTheHeaderTogether) {
    expect_header_pair_moved_together(90, 91);
}

// ── Witness 1c: a dictionary pair whose halves classify differently ──

// The Data half is a header-set tag but must travel with its body-class
// Length: a counted field inherits its Length's classification, so the pair
// stays adjacent, Length first, in the body. This is the input on which that
// inheritance differs from classifying the Data by its own tag.
TEST_F(DataSend091, DictionaryPair_BodyLengthHeaderSetData_StaysTogetherInTheBody) {
    auto const dict = make_straddling_pair_dictionary();
    auto const tv = dict->as_table_view();
    ASSERT_EQ(tv.length_pair_data_tag(5001), 142U) << "the dictionary must pair 5001 with 142";

    std::string const value = std::string{"<x a=\"1\">"} + '\x01' + "34=99" + '\x01' + "10=000</x>";
    std::string const payload = std::string{"35=D"} + '\x01' + "11=ORD" + '\x01' + "54=1" + '\x01' +
                                "5001=" + std::to_string(value.size()) + '\x01' + "142=" + value +
                                '\x01' + "55=AAPL" + '\x01';
    auto const bytes = to_bytes(payload);
    auto const frame = send_one(bytes, "DictionaryPair_BodyLengthHeaderSetData", dict);
    ASSERT_FALSE(frame.empty());

    Reparsed const rp = reparse(frame, tv);
    std::vector<std::uint32_t> const expected_tags{8,  9,  35,   34,  49, 52, 56,
                                                   11, 54, 5001, 142, 55, 10};
    EXPECT_EQ(rp.tags, expected_tags) << "the pair must stay adjacent, Length first, in the body";
    EXPECT_EQ(rp.value_of(5001), std::to_string(value.size()));
    EXPECT_EQ(rp.value_of(142), value) << "the Data value must re-parse verbatim";
}

// ── Witness 2: a generated builder's message_encoding + non-ASCII encoded_text ──

TEST_F(DataSend091, GeneratedNewOrderSingle_MessageEncodingInHeader_EncodedTextVerbatimInBody) {
    // Non-ASCII UTF-8 octets, with a SOH between them.
    std::string const encoded = std::string{"\xE6\x97\xA5"} + '\x01' + "\xE6\x9C\xAC\xE8\xAA\x9E";

    fixpp::v44::NewOrderSingleArgs args{};
    args.cl_ord_id = "ORD";
    args.side = '1';
    args.symbol = "AAPL";
    args.encoded_text = encoded;
    args.message_encoding = "UTF-8";

    std::array<std::byte, 1024> out{};
    auto const built = fixpp::v44::build_NewOrderSingle(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << fixpp::core::to_string(built.error());

    auto const frame = send_one(*built, "GeneratedNewOrderSingle/send");
    ASSERT_FALSE(frame.empty());

    Reparsed const rp = reparse(frame);
    std::vector<std::uint32_t> const expected_tags{8,   9,  35, 34, 49,  52,  56,
                                                   347, 11, 54, 55, 354, 355, 10};
    EXPECT_EQ(rp.tags, expected_tags)
        << "347 must sit in the header; 354/355 stay in the body, adjacent";
    EXPECT_EQ(rp.value_of(347), "UTF-8");
    EXPECT_EQ(rp.value_of(354), std::to_string(encoded.size()));
    EXPECT_EQ(rp.value_of(355), encoded) << "the EncodedText octets must re-parse verbatim";
}

}  // namespace
}  // namespace fixpp::session::test
