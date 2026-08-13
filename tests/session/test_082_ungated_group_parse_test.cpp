// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_082_ungated_group_parse_test.cpp
//
// 082-structural-group-detection T021 [US1] RED pin (written BEFORE T023 per
// the RED-first ordering rule) -- contracts/group-detection.md C4.1, FR-006a,
// SC-008 / SC-008a (first leg).
//
// C4.1: "Read/parse (ungated). For FIX40/41/42/43, a tag inside a
// newly-registered repeating group resolves membership-bounded instead of
// absent or positionally-wrong. This holds independently of
// validate_inbound_messages -- inbound_tv_ is built in open() and consumed
// by parse_and_dispatch_ with no flag on the path."
//
// This test drives a REAL Session (real-Session-dispatch harness, mirroring
// tests/session/support/group_dispatch_fixture.hpp's shape) through all
// THREE of FIX40/FIX41/FIX42 (not FIX44, which 066 already covers), with
// `validate_inbound_messages` left at its default OFF, to keep the parse
// axis and the validation axis from being conflated (per the task text).
//
// Host message: Allocation(msgtype 'J') / NoOrders(73). Chosen because it is
// the ONE group tag common to all three dictionaries at a stable, top-level
// (non-nested) position in the SAME host message across all three versions
// (dictionaries/FIX40.xml:316, FIX41.xml:452, FIX42.xml:670 -- each declares
// `<group name='NoOrders' ...>` directly inside Allocation, immediately
// followed there by the plain field Side(54) -- confirmed by direct
// inspection of the vendored XML). Members used: ClOrdID(11), OrderID(37)
// (declared in NoOrders for all three versions).
//
// Two assertions, both from C4.1/SC-008a:
//   (a) DISCRIMINATING: group_slices(73) returns exactly 2 instances, each
//       carrying ONLY its own leg's ClOrdID/OrderID -- and the trailing
//       outer field Side(54), which immediately follows the group in the
//       wire body, is ABSENT from the last instance (group-scoped, not
//       flat/positionally-absorbed-to-end-of-message). RED today: TODAY
//       none of FIX40/41/42 register NoOrders(73) as a group at all (the
//       `fr.type == NumInGroup` gate in Dictionary::as_table_view()'s
//       population loops -- dictionary.cpp:398/441/446 -- filters out
//       every group-count tag in these three dictionaries, which type
//       their group-count fields `INT`, never `NUMINGROUP`), so
//       group_slices(73) returns an EMPTY span pre-T023.
//   (b) SC-008a's FIRST leg (non-discriminating, must hold both pre- and
//       post-T023): with validate_inbound_messages left OFF, the message is
//       NOT newly rejected -- it still reaches fromApp and the session
//       stays Active. Read-shape movement alone (leg (a)) must never imply
//       acceptance movement.
//
// Anchors: tasks.md T021; contracts/group-detection.md C4.1; spec.md
// FR-006a, SC-008, SC-008a; tests/session/support/group_dispatch_fixture.hpp
// (066 dispatch-harness precedent, FIX44-specific -- not reused directly
// here since this test needs FIX40/FIX41/FIX42, each with a different
// begin_string and dictionary; a local, per-version fixture is used instead
// of editing that shared FIX44-only header).

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/view.hpp>

#include "support/app_message_read_scaffold.hpp"     // fixpp_test_support::make_frame
#include "support/minimal_security_profile.hpp"

namespace fixpp::session::test082 {
namespace {

// In-order, SOH-boundary-aware scan for "<tag>=<value>" within one group
// instance's raw bytes -- mirrors test_066_group_membership_red_test.cpp's
// `slice_has_tag` shape (genuine tag-boundary match, no substring false
// positive), extended to also check the VALUE so leg #1 vs leg #2 can be
// told apart.
bool slice_has_field(fixpp::wire::group_slice const& s, std::uint16_t tag, std::string_view value) {
    std::string_view const sv{reinterpret_cast<char const*>(s.data), s.len};
    std::string const needle = std::to_string(tag) + "=" + std::string(value);
    auto at_boundary = [&](std::size_t pos) {
        return sv.compare(pos, needle.size(), needle) == 0 &&
               (pos + needle.size() == sv.size() || sv[pos + needle.size()] == '\x01');
    };
    if (at_boundary(0)) return true;
    std::size_t pos = 0;
    while ((pos = sv.find('\x01', pos)) != std::string_view::npos) {
        ++pos;
        if (pos < sv.size() && at_boundary(pos)) return true;
    }
    return false;
}

class CallbackCapturingApplication : public Application {
public:
    using ViewCallback = std::function<void(fixpp::wire::MessageView<fixpp::wire::access_mode::Index> const&)>;
    ViewCallback on_from_app;
    int from_app_calls = 0;

    fixpp::core::expected_t<void> fromApp(
        fixpp::wire::MessageView<fixpp::wire::access_mode::Index> const& msg,
        SessionId const& /*id*/) override {
        ++from_app_calls;
        if (on_from_app) on_from_app(msg);
        return {};
    }
    fixpp::core::expected_t<void> fromAdmin(
        fixpp::wire::MessageView<fixpp::wire::access_mode::Index> const& /*msg*/,
        SessionId const& /*id*/) override {
        return {};
    }
};

// Loads the REAL dictionaries/<filename> into a heap-owned
// shared_ptr<const Dictionary>, mirroring tests/support/fix44_dictionary.hpp
// exactly (PMR-buffer-in-shared_ptr-deleter pattern), parameterized by
// filename since this test needs three distinct dictionaries.
std::shared_ptr<const fixpp::dict::Dictionary> load_real_dictionary(std::string_view filename) {
    constexpr std::size_t kBufSize = 4u * 1024u * 1024u;
    auto buf = std::make_unique<std::array<std::byte, kBufSize>>();
    auto* mr = new std::pmr::monotonic_buffer_resource{buf->data(), buf->size()};
    std::string const path = std::string(FIXPP_DICT_DATA_DIR) + "/" + std::string(filename);
    fixpp::dict::Dictionary d = fixpp::dict::XmlLoader{}.load(path, mr);
    auto* raw_dict = new fixpp::dict::Dictionary{std::move(d)};
    auto* raw_buf = buf.release();
    return std::shared_ptr<const fixpp::dict::Dictionary>{
        raw_dict, [mr, raw_buf](fixpp::dict::Dictionary const* p) {
            delete p;
            delete mr;
            delete raw_buf;
        }};
}

// Per-version (FIX40/FIX41/FIX42) real-Session-dispatch fixture. Same
// mechanics as tests/session/support/group_dispatch_fixture.hpp
// (GroupDispatchFixture), generalized over begin_string + dictionary
// filename instead of being hardcoded to FIX44.
struct AllocationFixture {
    std::string begin_string;
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine_cfg;
    std::shared_ptr<CallbackCapturingApplication> app;

    explicit AllocationFixture(std::string_view dict_filename, std::string_view begin_string_)
        : begin_string(begin_string_) {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine_cfg.clock = clock;
        engine_cfg.executor = ioc.get_executor();
        app = std::make_shared<CallbackCapturingApplication>();
        engine_cfg.application = app;
        dict_ = load_real_dictionary(dict_filename);
    }

    SessionConfig make_cfg() {
        using namespace std::chrono_literals;
        SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = begin_string;
        cfg.heartbeat_interval = 0s;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = dict_;
        cfg.executor_override = ioc.get_executor();
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        cfg.transport_send = [](std::span<std::byte const> /*frame*/) {};
        return cfg;
    }

    void open_to_active(Session& sess) {
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        ioc.run_for(std::chrono::milliseconds{200});
        ioc.restart();
        ASSERT_TRUE(fut.get().has_value()) << "open() failed";

        std::string body = "35=A\x01"
                            "34=1\x01"
                            "49=TW\x01"
                            "52=20240101-00:00:00.000\x01"
                            "56=ISLD\x01"
                            "98=0\x01"
                            "108=30\x01";
        auto logon = fixpp_test_support::make_frame(begin_string, body);
        auto fut2 = asio::co_spawn(ioc, sess.on_inbound_frame(logon), asio::use_future);
        ioc.run_for(std::chrono::milliseconds{200});
        ioc.restart();
        ASSERT_TRUE(fut2.get().has_value()) << "Logon feed failed";
        ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active);
    }

    void feed(Session& sess, std::span<std::byte const> frame) {
        auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(frame), asio::use_future);
        ioc.run_for(std::chrono::milliseconds{200});
        ioc.restart();
        (void)fut.get();
    }

private:
    std::shared_ptr<const fixpp::dict::Dictionary> dict_;
};

// Allocation(J) body, common tag numbers across FIX40/FIX41/FIX42
// (AllocID=70, AllocTransType=71, NoOrders=73/ClOrdID=11/OrderID=37,
// Side=54, Symbol=55, Shares=53, AvgPx=6, TradeDate=75 -- confirmed
// identical declarations in all three vendored dictionaries' Allocation
// message). NoOrders(73)=2, immediately followed by the plain field
// Side(54) -- the TRAILING outer field used to prove group-scoped bounding.
std::vector<std::byte> make_allocation_frame(std::string_view begin_string, std::uint32_t seq) {
    std::string body = "35=J\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=TW\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=ISLD\x01";
    body += "70=ALLOC1\x01";  // AllocID
    body += "71=0\x01";       // AllocTransType
    body += "73=2\x01";       // NoOrders = 2
    body += "11=CLA\x01";     // leg #1: ClOrdID
    body += "37=OA\x01";      // leg #1: OrderID
    body += "11=CLB\x01";     // leg #2: ClOrdID
    body += "37=OB\x01";      // leg #2: OrderID
    body += "54=1\x01";       // TRAILING outer field, immediately AFTER the group: Side
    body += "55=SYM\x01";     // Symbol
    body += "53=100\x01";     // Shares
    body += "6=10.5\x01";     // AvgPx
    body += "75=20240101\x01";  // TradeDate
    return fixpp_test_support::make_frame(begin_string, body);
}

struct VersionCase {
    char const* label;
    char const* dict_filename;
    char const* begin_string;
};

std::vector<VersionCase> const kVersions{
    {"FIX40", "FIX40.xml", "FIX.4.0"},
    {"FIX41", "FIX41.xml", "FIX.4.1"},
    {"FIX42", "FIX42.xml", "FIX.4.2"},
};

}  // namespace

TEST(UngatedGroupParse, AllocationNoOrdersGroupScopedNotFlatAcrossFix40Fix41Fix42) {
    for (auto const& v : kVersions) {
        SCOPED_TRACE(v.label);

        AllocationFixture f{v.dict_filename, v.begin_string};
        auto cfg = f.make_cfg();
        // validate_inbound_messages left at its default (false/OFF) --
        // deliberate, per the task text: keeps the parse axis and the
        // validation axis from being conflated.
        ASSERT_FALSE(cfg.validate_inbound_messages)
            << v.label << ": SessionConfig default changed -- this test relies on OFF being default";
        Session sess(f.engine_cfg, cfg);
        f.open_to_active(sess);

        ASSERT_EQ(f.app->from_app_calls, 0) << v.label << ": no app message dispatched yet";

        std::size_t count = 0;
        bool leg0_has_own = false;
        bool leg1_has_own = false;
        bool last_has_trailing_side = true;  // default true: an un-run callback must not silently pass

        f.app->on_from_app =
            [&](fixpp::wire::MessageView<fixpp::wire::access_mode::Index> const& msg) {
                auto slices = msg.offsets().group_slices(73);
                count = slices.size();
                if (count >= 1) {
                    leg0_has_own = slice_has_field(slices[0], 11, "CLA") &&
                                   slice_has_field(slices[0], 37, "OA");
                    last_has_trailing_side = slice_has_field(slices[count - 1], 54, "1");
                }
                if (count >= 2) {
                    leg1_has_own = slice_has_field(slices[1], 11, "CLB") &&
                                   slice_has_field(slices[1], 37, "OB");
                }
            };

        auto frame = make_allocation_frame(v.begin_string, /*seq=*/2);
        ASSERT_FALSE(frame.empty()) << v.label;

        f.feed(sess, frame);

        // (b) SC-008a first leg: read-shape movement must not imply a NEW
        // rejection. Must hold BOTH before and after T023.
        EXPECT_EQ(f.app->from_app_calls, 1)
            << v.label << ": Allocation(J) must reach fromApp with validate_inbound_messages OFF "
               "(SC-008a first leg -- read shape changes, acceptance must not)";
        EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active)
            << v.label << ": session must remain Active";

        // (a) DISCRIMINATING: group-scoped resolution. RED today (pre-T023):
        // NoOrders(73) is not registered as a group in any of FIX40/41/42
        // (INT-typed group-count field filtered out before the structural
        // predicate is ever consulted), so group_slices(73) returns EMPTY
        // (count == 0), not 2.
        EXPECT_EQ(count, 2u)
            << v.label << ": NoOrders(73)=2 must yield exactly 2 group instances "
               "(group-scoped resolution) -- RED pre-T023 (group unregistered -> empty span)";
        EXPECT_TRUE(leg0_has_own) << v.label << ": leg #1's own ClOrdID(11)=CLA/OrderID(37)=OA must be present";
        EXPECT_TRUE(leg1_has_own) << v.label << ": leg #2's own ClOrdID(11)=CLB/OrderID(37)=OB must be present";

        // DISCRIMINATING: proves group-scoped, not flat/positionally-
        // absorbed-to-end-of-message -- the trailing outer field Side(54),
        // declared immediately after NoOrders in the dictionary and placed
        // immediately after the group in the wire body, must NOT be part of
        // the last group instance.
        EXPECT_FALSE(last_has_trailing_side)
            << v.label << ": trailing Side(54) must NOT be part of the last NoOrders(73) instance "
               "(group-scoped, not flat/positional)";
    }
}

}  // namespace fixpp::session::test082
