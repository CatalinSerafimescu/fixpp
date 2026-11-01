// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_098_header_send.cpp
//
// 098-dialect-overlay, contracts/witnesses.md W-6 (US1 AS-7, FR-022, SC-008):
// a header tag the session's dictionary declares goes out in the header,
// before the first body field, wherever the application placed it.
//
// The dictionary is a hand-written edit of the vendored FIX44.xml, never one
// produced by overlay code: `edited_fix44_text` inserts lines before an anchor
// line that must occur exactly once in the base file, comparing each line with
// one trailing CR stripped (research R-10, line endings). The edit declares, in
// `<header>`, the scalar 5020 and the custom Length+Data pair 5030 (LENGTH) /
// 5031 (DATA), adjacent so that XmlLoader pairs them, and defines the three
// fields. It is loaded with the existing `XmlLoader::load_from_string`, so this
// file compiles without the overlay API.
//
// Arms:
//   - the scalar 5020, placed after a body field, sits before the first body
//     field;
//   - the pair 5030/5031, placed after a body field in a raw payload passed to
//     `Session::send` (`body_builder::field_data` refuses a custom Data tag), is
//     in the header, adjacent, Length first, its Data value (holding SOH) read
//     back verbatim by its count;
//   - the 5020 message replayed through ResendRequest carries 5020, 43 and 122
//     inside the header. Its 43/122 assertions are non-fatal and independent of
//     the 5020 assertion, so a run reports both halves.
// Each frame is read back through a dictionary-aware `Parser<Index>` over the
// edited dictionary, so the counted Data value is one field.

#include <gtest/gtest.h>

#include <algorithm>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/clock.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/memory_store_factory.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fstream>
#include <iterator>
#include <memory>
#include <memory_resource>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "support/app_message_read_scaffold.hpp"
#include "support/extract_tag.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"

using namespace std::chrono_literals;

namespace fixpp::session::test {
namespace {

constexpr auto kWindow = 200ms;

constexpr std::uint32_t kHeaderScalarTag = 5020;
constexpr std::uint32_t kHeaderLengthTag = 5030;
constexpr std::uint32_t kHeaderDataTag = 5031;
// The first body field of every payload below.
constexpr std::uint32_t kFirstBodyTag = 11;

// ── The edited dictionary ────────────────────────────────────────────────────

// Inserts `lines` before the one line of `text` equal to `anchor`, comparing each
// line with one trailing CR stripped. Sets `matches` to the number of lines equal
// to the anchor; the text is edited only when that number is one.
std::string insert_before_unique_line(std::string const& text, std::string_view anchor,
                                      std::string_view lines, int& matches) {
    matches = 0;
    std::size_t insert_at = std::string::npos;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t const nl = text.find('\n', pos);
        std::size_t const end = nl == std::string::npos ? text.size() : nl;
        std::string_view line{text.data() + pos, end - pos};
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line == anchor) {
            ++matches;
            insert_at = pos;
        }
        pos = nl == std::string::npos ? text.size() : nl + 1;
    }
    if (matches != 1) {
        return text;
    }
    std::string out = text.substr(0, insert_at);
    out.append(lines.data(), lines.size());
    out += text.substr(insert_at);
    return out;
}

std::string read_file(std::string const& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

constexpr std::string_view kHeaderAnchor = " </header>";
constexpr std::string_view kHeaderLines =
    "  <field name='Custom098HeaderScalar' required='N' />\n"
    "  <field name='Custom098HeaderLen' required='N' />\n"
    "  <field name='Custom098HeaderData' required='N' />\n";
constexpr std::string_view kFieldsAnchor = " </fields>";
constexpr std::string_view kFieldLines =
    "  <field number='5020' name='Custom098HeaderScalar' type='STRING' />\n"
    "  <field number='5030' name='Custom098HeaderLen' type='LENGTH' />\n"
    "  <field number='5031' name='Custom098HeaderData' type='DATA' />\n";

// The edited FIX44.xml text; `header_matches` / `fields_matches` report how many
// lines equal each anchor.
std::string edited_fix44_text(int& header_matches, int& fields_matches) {
    std::string const base = read_file(std::string(FIXPP_DICT_DATA_DIR) + "/FIX44.xml");
    std::string const with_header =
        insert_before_unique_line(base, kHeaderAnchor, kHeaderLines, header_matches);
    return insert_before_unique_line(with_header, kFieldsAnchor, kFieldLines, fields_matches);
}

// One allocation owns the arena and the dictionary built in it; members are
// destroyed in reverse declaration order, so the dictionary goes before its arena.
struct EditedDictionary {
    std::pmr::monotonic_buffer_resource mr{4U * 1024U * 1024U};
    fixpp::dict::Dictionary dict;
    explicit EditedDictionary(std::string const& xml)
        : dict{fixpp::dict::XmlLoader{}.load_from_string(xml, &mr)} {}
};

// ── Reading a frame back ─────────────────────────────────────────────────────

struct Reparsed {
    std::vector<std::uint32_t> tags;
    std::vector<std::string> values;

    [[nodiscard]] std::size_t count(std::uint32_t tag) const {
        return static_cast<std::size_t>(std::ranges::count(tags, tag));
    }
    // Index of the first field carrying `tag`, or tags.size() when absent.
    [[nodiscard]] std::size_t index_of(std::uint32_t tag) const {
        return static_cast<std::size_t>(std::ranges::find(tags, tag) - tags.begin());
    }
    [[nodiscard]] std::string value_of(std::uint32_t tag) const {
        std::size_t const i = index_of(tag);
        return i < values.size() ? values[i] : std::string{};
    }
    [[nodiscard]] std::string order() const {
        std::string s;
        for (auto const t : tags) {
            s += std::to_string(t) + ' ';
        }
        return s;
    }
};

Reparsed reparse(std::vector<std::byte> const& frame, fixpp::dict::table_view const& tv) {
    std::pmr::monotonic_buffer_resource arena{64U * 1024U};
    auto mv = fixpp_test_support::parse_dict(frame, tv, &arena);
    Reparsed r;
    for (auto const& e : mv.offsets().entries()) {
        r.tags.push_back(e.tag);
        r.values.emplace_back(reinterpret_cast<char const*>(frame.data()) + e.offset, e.length);
    }
    return r;
}

std::vector<std::byte> to_bytes(std::string_view sv) {
    auto const b = std::as_bytes(std::span{sv});
    return {b.begin(), b.end()};
}

// ── Fixture ──────────────────────────────────────────────────────────────────

class HeaderSend098 : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine{};
    std::vector<std::vector<std::byte>> captured;
    std::shared_ptr<EditedDictionary> edited;
    std::shared_ptr<const fixpp::dict::Dictionary> dict;
    std::shared_ptr<const fixpp::dict::table_view> tv;

    void SetUp() override {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};  // 2024-01-01
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();

        int header_matches = 0;
        int fields_matches = 0;
        std::string const xml = edited_fix44_text(header_matches, fields_matches);
        ASSERT_EQ(header_matches, 1) << "the <header> anchor must match exactly one line";
        ASSERT_EQ(fields_matches, 1) << "the <fields> anchor must match exactly one line";
        edited = std::make_shared<EditedDictionary>(xml);
        dict = std::shared_ptr<const fixpp::dict::Dictionary>{edited, &edited->dict};
        tv = std::make_shared<const fixpp::dict::table_view>(dict->as_table_view());
    }

    SessionConfig make_cfg() {
        SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.4";
        cfg.heartbeat_interval = 0s;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = dict;
        cfg.executor_override = ioc.get_executor();
        cfg.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
        MemoryStore::Config store_cfg;
        store_cfg.inbound_capacity = 16;
        store_cfg.outbound_capacity = 16;
        store_cfg.max_frame_bytes = 4096;
        cfg.store_factory = std::make_shared<MemoryStoreFactory>(store_cfg);
        cfg.transport_send = [this](std::span<const std::byte> frame) {
            captured.emplace_back(frame.begin(), frame.end());
        };
        return cfg;
    }

    template <class Fut>
    bool pump(Fut& fut, char const* site) {
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, kWindow, site)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, site);
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << site;
            return false;
        }
        return true;
    }

    void drive_to_active(Session& sess) {
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        if (!pump(fut, "HeaderSend098::open")) {
            return;
        }
        ASSERT_TRUE(fut.get().has_value()) << "open() failed";
        auto const logon = fixpp_test_support::make_frame("FIX.4.4",
                                                          "35=A\x01"
                                                          "34=1\x01"
                                                          "49=TW\x01"
                                                          "52=20240101-00:00:00.000\x01"
                                                          "56=ISLD\x01"
                                                          "98=0\x01"
                                                          "108=30\x01");
        auto fut2 = asio::co_spawn(ioc, sess.on_inbound_frame(logon), asio::use_future);
        if (!pump(fut2, "HeaderSend098::logon")) {
            return;
        }
        ASSERT_TRUE(fut2.get().has_value()) << "peer Logon feed failed";
        ASSERT_EQ(sess.state(), fsm_state::Active);
        captured.clear();  // the Logon reply
    }

    // Sends `payload` and returns the one transmitted frame.
    std::vector<std::byte> send_one(Session& sess, std::string_view payload, char const* site) {
        auto const bytes = to_bytes(payload);
        auto fut =
            asio::co_spawn(ioc, sess.send(std::span<const std::byte>(bytes)), asio::use_future);
        if (!pump(fut, site)) {
            return {};
        }
        auto const r = fut.get();
        EXPECT_TRUE(r.has_value()) << site << ": the send must not be refused";
        if (captured.size() != 1U) {
            ADD_FAILURE() << site << ": expected one transmitted frame, got " << captured.size();
            return {};
        }
        auto frame = captured.back();
        captured.clear();
        return frame;
    }
};

constexpr std::string_view kScalarPayload =
    "35=D\x01"
    "11=ORD098\x01"
    "54=1\x01"
    "5020=H098\x01"
    "55=AAPL\x01";

// Arm 1: the scalar header tag.
TEST_F(HeaderSend098, EditedDictionary_ScalarHeaderTagAfterBody_SentBeforeTheFirstBodyField) {
    Session sess(engine, make_cfg());
    drive_to_active(sess);
    if (HasFatalFailure()) return;

    auto const frame = send_one(sess, kScalarPayload, "HeaderSend098/scalar");
    ASSERT_FALSE(frame.empty());
    Reparsed const rp = reparse(frame, *tv);
    ASSERT_EQ(rp.count(kHeaderScalarTag), 1U) << "wire order: " << rp.order();
    ASSERT_EQ(rp.count(kFirstBodyTag), 1U) << "wire order: " << rp.order();
    EXPECT_LT(rp.index_of(kHeaderScalarTag), rp.index_of(kFirstBodyTag))
        << "5020 must sit before the first body field (11); wire order: " << rp.order();
}

// Arm 2: the custom Length+Data pair declared in the header.
TEST_F(HeaderSend098, EditedDictionary_CustomHeaderPairAfterBody_SentInTheHeaderAdjacent) {
    Session sess(engine, make_cfg());
    drive_to_active(sess);
    if (HasFatalFailure()) return;

    std::string const value = std::string{"<v>"} + '\x01' + "58=X" + '\x01' + "</v>";
    std::string const payload = std::string{"35=D"} + '\x01' + "11=ORD098" + '\x01' + "54=1" +
                                '\x01' + "5030=" + std::to_string(value.size()) + '\x01' +
                                "5031=" + value + '\x01' + "55=AAPL" + '\x01';
    auto const frame = send_one(sess, payload, "HeaderSend098/pair");
    ASSERT_FALSE(frame.empty());
    Reparsed const rp = reparse(frame, *tv);
    ASSERT_EQ(rp.count(kHeaderLengthTag), 1U) << "wire order: " << rp.order();
    ASSERT_EQ(rp.count(kHeaderDataTag), 1U) << "wire order: " << rp.order();
    ASSERT_EQ(rp.count(kFirstBodyTag), 1U) << "wire order: " << rp.order();
    EXPECT_LT(rp.index_of(kHeaderLengthTag), rp.index_of(kFirstBodyTag))
        << "5030 must sit before the first body field (11); wire order: " << rp.order();
    EXPECT_LT(rp.index_of(kHeaderDataTag), rp.index_of(kFirstBodyTag))
        << "5031 must sit before the first body field (11); wire order: " << rp.order();
    EXPECT_EQ(rp.index_of(kHeaderDataTag), rp.index_of(kHeaderLengthTag) + 1U)
        << "the Data field must follow its Length field; wire order: " << rp.order();
    EXPECT_EQ(rp.value_of(kHeaderLengthTag), std::to_string(value.size()));
    EXPECT_EQ(rp.value_of(kHeaderDataTag), value) << "the Data value must read back verbatim";
}

// Arm 3: the 5020 message replayed through ResendRequest.
TEST_F(HeaderSend098, EditedDictionary_ReplayOfTheScalarMessage_Keeps5020And43And122InTheHeader) {
    Session sess(engine, make_cfg());
    drive_to_active(sess);
    if (HasFatalFailure()) return;

    auto const original = send_one(sess, kScalarPayload, "HeaderSend098/replay-send");
    ASSERT_FALSE(original.empty());
    std::string const seq = fixpp::test_support::extract_tag(original, 34);
    ASSERT_FALSE(seq.empty());

    std::string rr = "35=2\x01";
    rr += "34=2\x01";
    rr += "49=TW\x01";
    rr += "52=20240101-00:00:00.000\x01";
    rr += "56=ISLD\x01";
    rr += "7=" + seq + "\x01";
    rr += "16=" + seq + "\x01";
    auto const rr_frame = fixpp_test_support::make_frame("FIX.4.4", rr);
    auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(rr_frame), asio::use_future);
    if (!pump(fut, "HeaderSend098/replay-resend")) {
        return;
    }
    (void)fut.get();

    std::vector<Reparsed> replays;
    for (auto const& f : captured) {
        if (fixpp::test_support::extract_tag(f, 35) == "D" &&
            fixpp::test_support::extract_tag(f, 34) == seq) {
            replays.push_back(reparse(f, *tv));
        }
    }
    ASSERT_EQ(replays.size(), 1U) << "the ResendRequest must be answered with one replay";
    Reparsed const& rp = replays.front();
    ASSERT_EQ(rp.count(kFirstBodyTag), 1U) << "wire order: " << rp.order();
    // The 5020 half.
    EXPECT_EQ(rp.count(kHeaderScalarTag), 1U) << "wire order: " << rp.order();
    EXPECT_LT(rp.index_of(kHeaderScalarTag), rp.index_of(kFirstBodyTag))
        << "replay: 5020 must sit before the first body field (11); wire order: " << rp.order();
    // The 43/122 half, independent of the 5020 half.
    EXPECT_EQ(rp.count(43), 1U) << "wire order: " << rp.order();
    EXPECT_EQ(rp.count(122), 1U) << "wire order: " << rp.order();
    EXPECT_LT(rp.index_of(43), rp.index_of(kFirstBodyTag))
        << "replay: 43 must sit before the first body field (11); wire order: " << rp.order();
    EXPECT_LT(rp.index_of(122), rp.index_of(kFirstBodyTag))
        << "replay: 122 must sit before the first body field (11); wire order: " << rp.order();
}

}  // namespace
}  // namespace fixpp::session::test
